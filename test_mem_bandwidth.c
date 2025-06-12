#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdint.h>
#include <dirent.h>

#define DEFAULT_SIZE_MB 64
#define ITERATIONS 3
#define RANDOM_ACCESSES 1000000  // Number of random accesses per iteration
#define LATENCY_ACCESSES 100000  // Number of accesses for latency testing
#define MB_TO_BYTES(mb) ((size_t)(mb) * 1024 * 1024)
#define KB_TO_BYTES(kb) ((size_t)(kb) * 1024)
#define MAX_CACHE_LEVELS 4

// Cache information structure
typedef struct {
    int level;
    char type[16];  // "Data", "Instruction", "Unified"
    size_t size_kb;
    int line_size;
    int associativity;
    int shared_cpu_map_count;
} cache_info_t;

// Global cache information
static cache_info_t cache_levels[MAX_CACHE_LEVELS];
static int num_cache_levels = 0;

// Latency measurement results
typedef struct {
    const char* size_name;
    size_t buffer_size;
    double latency_ns;
    const char* cache_level_hint;
} latency_result_t;

// Read cache information from /sys/devices/system/cpu/
void read_cache_info() {
    char path[256];
    FILE* fp;
    char buffer[256];
    
    num_cache_levels = 0;
    
    // Try to read cache info for CPU0 (assume uniform cache hierarchy)
    for (int index = 0; index < MAX_CACHE_LEVELS * 2; index++) {  // Allow more indices for L1I/L1D
        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu0/cache/index%d/type", index);
        fp = fopen(path, "r");
        if (!fp) break;
        
        cache_info_t* cache = &cache_levels[num_cache_levels];
        
        // Read cache type
        if (fgets(buffer, sizeof(buffer), fp)) {
            buffer[strcspn(buffer, "\n")] = 0;  // Remove newline
            strncpy(cache->type, buffer, sizeof(cache->type) - 1);
            cache->type[sizeof(cache->type) - 1] = 0;
        }
        fclose(fp);
        
        // Read cache size
        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu0/cache/index%d/size", index);
        fp = fopen(path, "r");
        if (fp) {
            if (fgets(buffer, sizeof(buffer), fp)) {
                cache->size_kb = atoi(buffer);
                if (strstr(buffer, "M")) cache->size_kb *= 1024;  // Convert MB to KB
            }
            fclose(fp);
        }
        
        // Read cache line size
        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu0/cache/index%d/coherency_line_size", index);
        fp = fopen(path, "r");
        if (fp) {
            if (fgets(buffer, sizeof(buffer), fp)) {
                cache->line_size = atoi(buffer);
            }
            fclose(fp);
        }
        
        // Read associativity
        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu0/cache/index%d/ways_of_associativity", index);
        fp = fopen(path, "r");
        if (fp) {
            if (fgets(buffer, sizeof(buffer), fp)) {
                cache->associativity = atoi(buffer);
            }
            fclose(fp);
        }
        
        // Count CPUs sharing this cache
        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu0/cache/index%d/shared_cpu_list", index);
        fp = fopen(path, "r");
        if (fp) {
            cache->shared_cpu_map_count = 1;  // Simplified - just mark as available
            fclose(fp);
        }
        
        // Determine actual cache level based on type and size
        if (strcmp(cache->type, "Data") == 0 || strcmp(cache->type, "Instruction") == 0) {
            cache->level = 1;  // L1 cache
        } else if (strcmp(cache->type, "Unified") == 0) {
            // For unified caches, determine level by size
            if (cache->size_kb <= 1024) {  // <= 1MB, likely L2
                cache->level = 2;
            } else {  // > 1MB, likely L3
                cache->level = 3;
            }
        } else {
            cache->level = index + 1;  // Fallback
        }
        
        num_cache_levels++;
        if (num_cache_levels >= MAX_CACHE_LEVELS) break;
    }
}

// Display cache hierarchy information
void display_cache_hierarchy() {
    printf("\nCPU Cache Hierarchy:\n");
    printf("===================\n");
    
    if (num_cache_levels == 0) {
        printf("Cache information not available (requires /sys/devices/system/cpu/ access)\n");
        return;
    }
    
    printf("%-5s %-12s %-10s %-12s %-15s\n", "Level", "Type", "Size", "Line Size", "Associativity");
    printf("-------------------------------------------------------------\n");
    
    for (int i = 0; i < num_cache_levels; i++) {
        cache_info_t* cache = &cache_levels[i];
        char size_str[32];
        
        if (cache->size_kb >= 1024) {
            snprintf(size_str, sizeof(size_str), "%zu MB", cache->size_kb / 1024);
        } else {
            snprintf(size_str, sizeof(size_str), "%zu KB", cache->size_kb);
        }
        
        printf("L%-4d %-12s %-10s %-12d %-15d\n", 
               cache->level, cache->type, size_str, 
               cache->line_size, cache->associativity);
    }
    printf("\n");
}

// Analyze latency results to identify cache levels
const char* analyze_cache_level(size_t buffer_size, double latency_ns) {
    // If we have cache info, use it for more accurate analysis
    if (num_cache_levels > 0) {
        for (int i = 0; i < num_cache_levels; i++) {
            cache_info_t* cache = &cache_levels[i];
            if (strcmp(cache->type, "Data") == 0 || strcmp(cache->type, "Unified") == 0) {
                size_t cache_size_bytes = cache->size_kb * 1024;
                if (buffer_size <= cache_size_bytes) {
                    static char level_str[32];
                    snprintf(level_str, sizeof(level_str), "L%d Cache", cache->level);
                    return level_str;
                }
            }
        }
        return "Main Memory";
    }
    
    // Fallback heuristic analysis based on common cache sizes and latency
    if (buffer_size <= 32 * 1024) {  // <= 32KB
        if (latency_ns < 5.0) return "L1 Cache";
        else return "L2 Cache";
    } else if (buffer_size <= 512 * 1024) {  // <= 512KB
        if (latency_ns < 15.0) return "L2 Cache";
        else return "L3 Cache";
    } else if (buffer_size <= 8 * 1024 * 1024) {  // <= 8MB
        if (latency_ns < 50.0) return "L3 Cache";
        else return "Main Memory";
    } else {
        return "Main Memory";
    }
}

// Utility function to get current time in seconds
double get_time() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

// Initialize random number generator with high quality seed
void init_random() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    srand((unsigned int)(ts.tv_sec ^ ts.tv_nsec));
}

// Generate random indices for memory access
void generate_random_indices(size_t* indices, size_t count, size_t max_index) {
    for (size_t i = 0; i < count; i++) {
        indices[i] = (size_t)rand() % max_index;
    }
}

// Aligned memory allocation function
void* aligned_malloc(size_t alignment, size_t size) {
    void* ptr;
    if (posix_memalign(&ptr, alignment, size) != 0) {
        return NULL;
    }
    return ptr;
}

// Sequential read test
double test_sequential_read(void* buffer, size_t size, int iterations) {
    double start_time = get_time();
    volatile long long sum = 0;  // volatile to prevent optimization
    
    for (int iter = 0; iter < iterations; iter++) {
        long long* data = (long long*)buffer;
        size_t elements = size / sizeof(long long);
        
        for (size_t i = 0; i < elements; i++) {
            sum += data[i];
        }
    }
    
    double end_time = get_time();
    return end_time - start_time;
}

// Sequential write test
double test_sequential_write(void* buffer, size_t size, int iterations) {
    double start_time = get_time();
    
    for (int iter = 0; iter < iterations; iter++) {
        long long* data = (long long*)buffer;
        size_t elements = size / sizeof(long long);
        
        for (size_t i = 0; i < elements; i++) {
            data[i] = i;
        }
    }
    
    double end_time = get_time();
    return end_time - start_time;
}

// Random read test
double test_random_read(void* buffer, size_t size, int iterations) {
    size_t elements = size / sizeof(long long);
    long long* data = (long long*)buffer;
    
    // Pre-generate random indices to avoid including RNG overhead in timing
    size_t* indices = malloc(RANDOM_ACCESSES * sizeof(size_t));
    if (!indices) {
        fprintf(stderr, "Failed to allocate indices array\n");
        return -1.0;
    }
    
    generate_random_indices(indices, RANDOM_ACCESSES, elements);
    
    double start_time = get_time();
    volatile long long sum = 0;  // volatile to prevent optimization
    
    for (int iter = 0; iter < iterations; iter++) {
        for (size_t i = 0; i < RANDOM_ACCESSES; i++) {
            sum += data[indices[i]];
        }
    }
    
    double end_time = get_time();
    free(indices);
    return end_time - start_time;
}

// Random write test
double test_random_write(void* buffer, size_t size, int iterations) {
    size_t elements = size / sizeof(long long);
    long long* data = (long long*)buffer;
    
    // Pre-generate random indices to avoid including RNG overhead in timing
    size_t* indices = malloc(RANDOM_ACCESSES * sizeof(size_t));
    if (!indices) {
        fprintf(stderr, "Failed to allocate indices array\n");
        return -1.0;
    }
    
    generate_random_indices(indices, RANDOM_ACCESSES, elements);
    
    double start_time = get_time();
    
    for (int iter = 0; iter < iterations; iter++) {
        for (size_t i = 0; i < RANDOM_ACCESSES; i++) {
            data[indices[i]] = (long long)i;
        }
    }
    
    double end_time = get_time();
    free(indices);
    return end_time - start_time;
}

// Memory copy test
double test_memory_copy(void* src, void* dst, size_t size, int iterations) {
    double start_time = get_time();
    
    for (int iter = 0; iter < iterations; iter++) {
        memcpy(dst, src, size);
    }
    
    double end_time = get_time();
    return end_time - start_time;
}

// Memory access latency test
double test_memory_latency(void* buffer, size_t size, size_t num_accesses) {
    size_t elements = size / sizeof(long long);
    long long* data = (long long*)buffer;
    
    // Pre-generate random indices to avoid including RNG overhead in timing
    size_t* indices = malloc(num_accesses * sizeof(size_t));
    if (!indices) {
        fprintf(stderr, "Failed to allocate indices array for latency test\n");
        return -1.0;
    }
    
    generate_random_indices(indices, num_accesses, elements);
    
    // Warmup - touch all memory locations to ensure they're in memory
    volatile long long warmup_sum = 0;
    for (size_t i = 0; i < elements; i++) {
        warmup_sum += data[i];
    }
    
    double start_time = get_time();
    volatile long long sum = 0;  // volatile to prevent optimization
    
    // Perform random accesses
    for (size_t i = 0; i < num_accesses; i++) {
        sum += data[indices[i]];
    }
    
    double end_time = get_time();
    free(indices);
    
    return end_time - start_time;
}

// Display latency results with cache level analysis
void display_latency(const char* test_name, double time_taken, size_t num_accesses, size_t buffer_size) {
    double avg_latency_ns = (time_taken * 1e9) / num_accesses;
    double avg_latency_us = avg_latency_ns / 1000.0;
    const char* cache_level = analyze_cache_level(buffer_size, avg_latency_ns);
    
    printf("%-12s (%6s): %8.1f ns/access (%6.2f us/access) - %-12s - %zu accesses\n", 
           test_name, 
           buffer_size >= 1024*1024 ? "MB" : "KB",
           avg_latency_ns, avg_latency_us, cache_level, num_accesses);
}

// Run latency test for a specific buffer size
void run_latency_test(size_t buffer_size, const char* size_name) {
    void* buffer = aligned_malloc(64, buffer_size);
    if (!buffer) {
        fprintf(stderr, "Failed to allocate %s buffer for latency test\n", size_name);
        return;
    }
    
    // Initialize buffer with data
    memset(buffer, 0xCC, buffer_size);
    
    // Run latency test
    double latency_time = test_memory_latency(buffer, buffer_size, LATENCY_ACCESSES);
    if (latency_time > 0) {
        display_latency(size_name, latency_time, LATENCY_ACCESSES, buffer_size);
    }
    
    free(buffer);
}

// Calculate and display bandwidth for sequential tests
void display_bandwidth(const char* test_name, double time_taken, size_t data_size, int iterations) {
    double total_data_gb = (double)(data_size * iterations) / (1024.0 * 1024.0 * 1024.0);
    double bandwidth_gbps = total_data_gb / time_taken;
    double bandwidth_mbps = bandwidth_gbps * 1024.0;
    
    printf("%-20s: %8.3f GB/s (%8.1f MB/s) - Time: %.3f seconds\n", 
           test_name, bandwidth_gbps, bandwidth_mbps, time_taken);
}

// Calculate and display bandwidth for random access tests
void display_random_bandwidth(const char* test_name, double time_taken, int iterations) {
    size_t total_accesses = (size_t)RANDOM_ACCESSES * iterations;
    size_t total_data_bytes = total_accesses * sizeof(long long);
    double total_data_gb = (double)total_data_bytes / (1024.0 * 1024.0 * 1024.0);
    double bandwidth_gbps = total_data_gb / time_taken;
    double bandwidth_mbps = bandwidth_gbps * 1024.0;
    double iops = (double)total_accesses / time_taken / 1000000.0; // Million operations per second
    
    printf("%-20s: %8.3f GB/s (%8.1f MB/s) - %.1f MIOPS - Time: %.3f seconds\n", 
           test_name, bandwidth_gbps, bandwidth_mbps, iops, time_taken);
}

int main(int argc, char* argv[]) {
    size_t size_mb = DEFAULT_SIZE_MB;
    
    // Parse command line arguments
    if (argc > 1) {
        size_mb = atoi(argv[1]);
        if (size_mb == 0) {
            fprintf(stderr, "Invalid size specified. Using default %d MB\n", DEFAULT_SIZE_MB);
            size_mb = DEFAULT_SIZE_MB;
        }
    }
    
    size_t buffer_size = MB_TO_BYTES(size_mb);
    
    printf("Memory Bandwidth Test\n");
    printf("===========================\n");
    printf("Buffer size: %zu MB (%zu bytes)\n", size_mb, buffer_size);
    printf("Iterations: %d\n", ITERATIONS);
    printf("Random accesses per iteration: %d\n", RANDOM_ACCESSES);
    printf("CPU cores available: %ld\n", sysconf(_SC_NPROCESSORS_ONLN));
    
    // Read and display cache hierarchy information
    read_cache_info();
    display_cache_hierarchy();
    
    // Initialize random number generator
    init_random();
    
    // Allocate memory buffers
    void* buffer1 = aligned_malloc(64, buffer_size);  // 64-byte aligned for cache efficiency
    void* buffer2 = aligned_malloc(64, buffer_size);
    
    if (!buffer1 || !buffer2) {
        fprintf(stderr, "Failed to allocate memory buffers\n");
        if (buffer1) free(buffer1);
        if (buffer2) free(buffer2);
        return 1;
    }
    
    // Initialize buffers with data
    printf("Initializing buffers...\n");
    memset(buffer1, 0xAA, buffer_size);
    memset(buffer2, 0x55, buffer_size);
    
    // Perform tests
    printf("\nRunning bandwidth tests...\n");
    printf("%-20s  %-50s\n", "Test", "Bandwidth");
    printf("------------------------------------------------------------------------\n");
    
    // Sequential tests
    double read_time = test_sequential_read(buffer1, buffer_size, ITERATIONS);
    display_bandwidth("Sequential Read", read_time, buffer_size, ITERATIONS);
    
    double write_time = test_sequential_write(buffer1, buffer_size, ITERATIONS);
    display_bandwidth("Sequential Write", write_time, buffer_size, ITERATIONS);
    
    // Random tests
    double random_read_time = test_random_read(buffer1, buffer_size, ITERATIONS);
    if (random_read_time > 0) {
        display_random_bandwidth("Random Read", random_read_time, ITERATIONS);
    }
    
    double random_write_time = test_random_write(buffer1, buffer_size, ITERATIONS);
    if (random_write_time > 0) {
        display_random_bandwidth("Random Write", random_write_time, ITERATIONS);
    }
    
    // Memory copy test (measures both read and write)
    double copy_time = test_memory_copy(buffer1, buffer2, buffer_size, ITERATIONS);
    display_bandwidth("Memory Copy", copy_time, buffer_size * 2, ITERATIONS); // *2 for read+write
    
    // Memory access latency tests
    printf("\n");
    printf("Running memory access latency tests...\n");
    printf("%-12s %-8s  %-50s %-12s\n", "Buffer Size", "Unit", "Average Latency", "Cache Level");
    printf("--------------------------------------------------------------------------------\n");
    
    run_latency_test(KB_TO_BYTES(4), "4KB");
    run_latency_test(KB_TO_BYTES(16), "16KB");
    run_latency_test(KB_TO_BYTES(256), "256KB");
    run_latency_test(MB_TO_BYTES(1), "1MB");
    run_latency_test(MB_TO_BYTES(4), "4MB");
    run_latency_test(MB_TO_BYTES(16), "16MB");
    
    printf("\nNotes:\n");
    printf("- Sequential Read/Write: Measures linear memory access patterns\n");
    printf("- Random Read/Write: Measures random memory access patterns\n");
    printf("- Memory Copy: Measures combined read+write bandwidth (memcpy)\n");
    printf("- MIOPS: Million I/O Operations Per Second\n");
    printf("- Random tests use %d accesses per iteration\n", RANDOM_ACCESSES);
    printf("- Latency tests use %d random accesses per test\n", LATENCY_ACCESSES);
    printf("- Latency tests measure average time per memory access\n");
    printf("- Cache Level indicates the likely memory hierarchy level being accessed\n");
    printf("- Cache hierarchy is detected from /sys/devices/system/cpu/ when available\n");
    printf("- Results may vary based on CPU cache, memory type, and system load\n");
    
    // Cleanup
    free(buffer1);
    free(buffer2);
    
    return 0;
}
