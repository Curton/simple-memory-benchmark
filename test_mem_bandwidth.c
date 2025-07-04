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
#define LATENCY_ACCESSES 1000000  // Reduced for pointer chasing - each access is serialized
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
            snprintf(cache->type, sizeof(cache->type), "%.15s", buffer);
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
    printf("--------------------------------------------------------------------------------\n");
    
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
        // Use high-order bits of rand() to generate indices. The low-order bits
        // from rand() can be non-random, and using `%` with a power-of-two
        // `max_index` isolates these non-random bits. This scaling method
        // provides a more uniform distribution.
        indices[i] = (size_t)((unsigned long long)rand() * max_index / (RAND_MAX + 1ULL));
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

// True memory latency test using proper pointer chasing
double test_memory_latency(void* buffer, size_t size, size_t num_accesses) {
    // Use cache line sized elements to avoid false sharing
    size_t cache_line_size = 64;  // bytes
    size_t elements = size / cache_line_size;
    char* data = (char*)buffer;
    
    if (elements < 2) {
        return -1.0;  // Buffer too small
    }
    
    // Create a circular linked list with random permutation
    // Each cache line contains a pointer to the next cache line
    size_t* indices = malloc(elements * sizeof(size_t));
    if (!indices) {
        fprintf(stderr, "Failed to allocate indices for latency test\n");
        return -1.0;
    }
    
    // Initialize with sequential indices
    for (size_t i = 0; i < elements; i++) {
        indices[i] = i;
    }
    
    // Fisher-Yates shuffle for true randomization
    for (size_t i = elements - 1; i > 0; i--) {
        size_t j = (size_t)((unsigned long long)rand() * (i + 1) / (RAND_MAX + 1ULL));
        size_t temp = indices[i];
        indices[i] = indices[j];
        indices[j] = temp;
    }
    
    // Create circular pointer chain: each element points to next in random order
    for (size_t i = 0; i < elements; i++) {
        size_t next_idx = indices[(i + 1) % elements];
        // Store pointer at beginning of cache line
        *((size_t*)(data + i * cache_line_size)) = next_idx * cache_line_size;
    }
    
    free(indices);
    
    // Force all memory to be allocated by writing to every cache line
    for (size_t i = 0; i < elements; i++) {
        // Write to end of cache line to ensure full line is allocated
        *(data + i * cache_line_size + cache_line_size - 1) = (char)(i & 0xFF);
    }
    
    // Memory fence to ensure writes complete
    __sync_synchronize();
    
    // Warmup: traverse the chain several times
    char* ptr = data;  // Start at first cache line
    for (size_t i = 0; i < elements * 3; i++) {
        ptr = data + *((size_t*)ptr);
    }
    
    // Another memory fence
    __sync_synchronize();
    
    // Measure latency: time how long it takes to traverse the chain
    double start_time = get_time();
    
    // Pointer chasing with memory dependencies
    // Use volatile to prevent optimization
    volatile char* volatile_ptr = data;
    for (size_t i = 0; i < num_accesses; i++) {
        volatile_ptr = data + *((volatile size_t*)volatile_ptr);
    }
    
    double end_time = get_time();
    
    // Use volatile_ptr to prevent dead code elimination
    if ((uintptr_t)volatile_ptr == 0) {
        printf("Unexpected ptr value\n");
    }
    
    // Debug: print timing info for troubleshooting
    double total_time = end_time - start_time;
    if (total_time < 1e-6) {  // Less than 1 microsecond suggests timing issues
        fprintf(stderr, "Warning: Very short test time (%.9f s) for %zu accesses in %zu byte buffer\n", 
                total_time, num_accesses, size);
    }
    
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
    
    // Initialize buffer with data - force actual memory allocation
    // Use volatile writes to ensure memory is actually allocated and not optimized away
    volatile long long* init_data = (volatile long long*)buffer;
    size_t elements = buffer_size / sizeof(long long);
    for (size_t i = 0; i < elements; i++) {
        init_data[i] = (long long)(i ^ 0xCCCCCCCCCCCCCCCC);  // Unique pattern per element
    }
    
    // Run latency test
    double latency_time = test_memory_latency(buffer, buffer_size, LATENCY_ACCESSES);
    if (latency_time > 0) {
        display_latency(size_name, latency_time, LATENCY_ACCESSES, buffer_size);
    }
    
    free(buffer);
}

// Generate dynamic test sizes based on cache hierarchy
void generate_dynamic_test_sizes(size_t** test_sizes, char*** size_names, int* num_tests) {
    const int max_tests = 20;
    *test_sizes = malloc(max_tests * sizeof(size_t));
    *size_names = malloc(max_tests * sizeof(char*));
    *num_tests = 0;
    
    if (!*test_sizes || !*size_names) {
        fprintf(stderr, "Failed to allocate memory for test size arrays\n");
        return;
    }
    
    // If we have cache information, generate tests around each cache level
    if (num_cache_levels > 0) {
        // Sort cache levels by size for proper ordering
        for (int i = 0; i < num_cache_levels - 1; i++) {
            for (int j = i + 1; j < num_cache_levels; j++) {
                if (cache_levels[i].size_kb > cache_levels[j].size_kb) {
                    cache_info_t temp = cache_levels[i];
                    cache_levels[i] = cache_levels[j];
                    cache_levels[j] = temp;
                }
            }
        }
        
        // Add a small test to start with
        (*test_sizes)[*num_tests] = KB_TO_BYTES(4);
        (*size_names)[*num_tests] = strdup("4KB");
        (*num_tests)++;
        
        for (int i = 0; i < num_cache_levels; i++) {
            cache_info_t* cache = &cache_levels[i];
            
            // Skip instruction caches for memory tests
            if (strcmp(cache->type, "Instruction") == 0) continue;
            
            size_t cache_size_bytes = cache->size_kb * 1024;
            
            // Test size that fits comfortably in this cache level (50% of cache)
            size_t fit_size = cache_size_bytes / 2;
            if (fit_size >= KB_TO_BYTES(8) && *num_tests < max_tests - 1) {
                (*test_sizes)[*num_tests] = fit_size;
                (*size_names)[*num_tests] = malloc(32);
                if (fit_size >= MB_TO_BYTES(1)) {
                    snprintf((*size_names)[*num_tests], 32, "%zuMB(L%d)", fit_size / (1024*1024), cache->level);
                } else {
                    snprintf((*size_names)[*num_tests], 32, "%zuKB(L%d)", fit_size / 1024, cache->level);
                }
                (*num_tests)++;
            }
            
            // Test size that exceeds this cache level (150% of cache)
            size_t exceed_size = cache_size_bytes + cache_size_bytes / 2;
            if (*num_tests < max_tests - 1) {
                (*test_sizes)[*num_tests] = exceed_size;
                (*size_names)[*num_tests] = malloc(32);
                if (exceed_size >= MB_TO_BYTES(1)) {
                    snprintf((*size_names)[*num_tests], 32, "%zuMB(>L%d)", exceed_size / (1024*1024), cache->level);
                } else {
                    snprintf((*size_names)[*num_tests], 32, "%zuKB(>L%d)", exceed_size / 1024, cache->level);
                }
                (*num_tests)++;
            }
        }
        
        // Add some larger sizes to test main memory
        size_t large_sizes[] = {MB_TO_BYTES(32), MB_TO_BYTES(64), MB_TO_BYTES(128)};
        const char* large_names[] = {"32MB(RAM)", "64MB(RAM)", "128MB(RAM)"};
        
        for (int i = 0; i < 3 && *num_tests < max_tests; i++) {
            (*test_sizes)[*num_tests] = large_sizes[i];
            (*size_names)[*num_tests] = strdup(large_names[i]);
            (*num_tests)++;
        }
    } else {
        // Fallback to default sizes if no cache info available
        size_t default_sizes[] = {
            KB_TO_BYTES(4), KB_TO_BYTES(16), KB_TO_BYTES(256),
            MB_TO_BYTES(1), MB_TO_BYTES(4), MB_TO_BYTES(16), MB_TO_BYTES(64)
        };
        const char* default_names[] = {
            "4KB", "16KB", "256KB", "1MB", "4MB", "16MB", "64MB"
        };
        
        for (int i = 0; i < 7 && *num_tests < max_tests; i++) {
            (*test_sizes)[*num_tests] = default_sizes[i];
            (*size_names)[*num_tests] = strdup(default_names[i]);
            (*num_tests)++;
        }
    }
}

// Free memory allocated for dynamic test sizes
void free_dynamic_test_sizes(size_t* test_sizes, char** size_names, int num_tests) {
    if (test_sizes) free(test_sizes);
    if (size_names) {
        for (int i = 0; i < num_tests; i++) {
            if (size_names[i]) free(size_names[i]);
        }
        free(size_names);
    }
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
    printf("--------------------------------------------------------------------------------\n");
    
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
    
    // Memory access latency tests with dynamic sizes based on cache hierarchy
    printf("\n");
    printf("Running memory access latency tests...\n");
    printf("%-12s %-8s  %-40s %-12s\n", "Buffer Size", "Unit", "Average Latency", "Cache Level");
    printf("--------------------------------------------------------------------------------\n");
    
    // Generate dynamic test sizes based on detected cache hierarchy
    size_t* test_sizes;
    char** size_names;
    int num_tests;
    
    generate_dynamic_test_sizes(&test_sizes, &size_names, &num_tests);
    
    if (num_cache_levels > 0) {
        printf("Test sizes generated based on detected cache hierarchy:\n");
    } else {
        printf("Using default test sizes (cache hierarchy not available):\n");
    }
    
    // Run all generated tests
    for (int i = 0; i < num_tests; i++) {
        run_latency_test(test_sizes[i], size_names[i]);
    }
    
    // Clean up dynamically allocated memory
    free_dynamic_test_sizes(test_sizes, size_names, num_tests);
    
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
