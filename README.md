# Memory Bandwidth and Latency Test

A high-performance C18-based memory testing tool that measures system memory bandwidth and access latency characteristics. This tool provides comprehensive analysis of memory subsystem performance including sequential/random access patterns and cache behavior analysis.

## Features

- **Sequential Memory Tests**: Linear read/write performance measurement
- **Random Memory Tests**: Random access pattern performance with configurable access counts
- **Memory Copy Performance**: `memcpy()` bandwidth testing (combined read+write)
- **Latency Analysis**: Access latency measurement across different buffer sizes (4KB to 16MB)
- **Cache Hierarchy Analysis**: Multi-level cache performance characterization
- **MIOPS Metrics**: Million I/O Operations Per Second calculation for random access tests
- **Configurable Buffer Sizes**: Support for custom memory buffer sizes


## Requirements

- **Compiler**: GCC with C18 support
- **Operating System**: Linux with POSIX.1-2008 support
- **Libraries**: 
  - `libc` (standard C library)
  - `librt` (POSIX real-time extensions)
- **Memory**: Sufficient RAM for test buffer allocation (default: 64MB, configurable)

## Building

### Quick Build
```bash
make
```

### Build with Debug Info
```bash
make CFLAGS="-O2 -Wall -Wextra -std=c18 -D_POSIX_C_SOURCE=200809L -g"
```

### Clean Build Environment
```bash
make clean
make
```

## Usage

### Basic Usage
```bash
# Run with default 64MB buffer
./test_mem_bandwidth

# Run with custom buffer size (in MB)
./test_mem_bandwidth 128
./test_mem_bandwidth 1024
```

### Makefile Targets
```bash
# Run with default settings
make run

# Run with large buffer (1GB)
make run-large

# Run with small buffer (16MB)  
make run-small
```

## Sample Output

```
Memory Bandwidth Test
===========================
Buffer size: 64 MB (67108864 bytes)
Iterations: 3
Random accesses per iteration: 1000000
CPU cores available: 1

CPU Cache Hierarchy:
===================
Level Type         Size       Line Size    Associativity  
-------------------------------------------------------------
L1    Data         32 KB      64           8              
L1    Instruction  32 KB      64           8              
L2    Unified      512 KB     64           8              
L3    Unified      16 MB      64           16             

Initializing buffers...

Running bandwidth tests...
Test                  Bandwidth                                         
------------------------------------------------------------------------
Sequential Read     :   13.356 GB/s ( 13676.1 MB/s) - Time: 0.014 seconds
Sequential Write    :   17.809 GB/s ( 18236.7 MB/s) - Time: 0.011 seconds
Random Read         :    1.071 GB/s (  1096.4 MB/s) - 143.7 MIOPS - Time: 0.021 seconds
Random Write        :    1.315 GB/s (  1346.7 MB/s) - 176.5 MIOPS - Time: 0.017 seconds
Memory Copy         :   27.603 GB/s ( 28265.5 MB/s) - Time: 0.014 seconds

Running memory access latency tests...
Buffer Size  Unit      Average Latency                                    Cache Level 
--------------------------------------------------------------------------------
Test sizes generated based on detected cache hierarchy:
4KB          (    KB):      0.6 ns/access (  0.00 us/access) - L1 Cache     - 100000 accesses
16KB(L1)     (    KB):      0.5 ns/access (  0.00 us/access) - L1 Cache     - 100000 accesses
48KB(>L1)    (    KB):      0.6 ns/access (  0.00 us/access) - L2 Cache     - 100000 accesses
256KB(L2)    (    KB):      0.6 ns/access (  0.00 us/access) - L2 Cache     - 100000 accesses
768KB(>L2)   (    KB):      1.0 ns/access (  0.00 us/access) - L3 Cache     - 100000 accesses
8MB(L3)      (    MB):      4.8 ns/access (  0.00 us/access) - L3 Cache     - 100000 accesses
24MB(>L3)    (    MB):      5.0 ns/access (  0.00 us/access) - Main Memory  - 100000 accesses
32MB(RAM)    (    MB):      5.7 ns/access (  0.01 us/access) - Main Memory  - 100000 accesses
64MB(RAM)    (    MB):      7.5 ns/access (  0.01 us/access) - Main Memory  - 100000 accesses
128MB(RAM)   (    MB):      9.8 ns/access (  0.01 us/access) - Main Memory  - 100000 accesses

Notes:
- Sequential Read/Write: Measures linear memory access patterns
- Random Read/Write: Measures random memory access patterns
- Memory Copy: Measures combined read+write bandwidth (memcpy)
- MIOPS: Million I/O Operations Per Second
- Random tests use 1000000 accesses per iteration
- Latency tests use 100000 random accesses per test
- Latency tests measure average time per memory access
- Cache Level indicates the likely memory hierarchy level being accessed
- Cache hierarchy is detected from /sys/devices/system/cpu/ when available
- Results may vary based on CPU cache, memory type, and system load
```

## Understanding the Results

### Bandwidth Tests

- **Sequential Read/Write**: Measures linear memory access patterns typical of streaming operations
- **Random Read/Write**: Measures performance with random access patterns that stress the memory hierarchy
- **Memory Copy**: Tests `memcpy()` performance, representing combined read+write operations
- **MIOPS**: Million I/O Operations Per Second - useful for comparing random access performance

### Latency Tests

The latency tests reveal cache hierarchy characteristics:
- **4KB-16KB**: L1 cache performance (typically 1-3 ns)
- **256KB**: L2 cache performance (typically 3-10 ns)
- **1MB-4MB**: L3 cache performance (typically 10-50 ns)
- **16MB+**: Main memory performance (typically 50-100+ ns)

### Performance Interpretation

- Higher bandwidth values indicate better memory subsystem performance
- Lower latency values indicate faster memory access times
- Large differences between sequential and random performance suggest cache-sensitive workloads
- MIOPS values help compare random access efficiency across different systems

## Technical Details

### Memory Allocation
- Uses `posix_memalign()` for 64-byte aligned buffers
- Alignment optimizes cache line usage and prevents false sharing
- Buffers are initialized with distinct patterns (0xAA, 0x55, 0xCC) to ensure valid memory access

### Timing Methodology
- High-precision timing using `clock_gettime(CLOCK_MONOTONIC)`
- Pre-generated random indices to exclude RNG overhead from measurements
- Volatile variables prevent compiler optimizations that could skew results
- Warmup phases ensure memory is resident before latency measurements

### Test Parameters
- **Default Buffer Size**: 64MB (configurable)
- **Iterations**: 3 (for bandwidth tests)
- **Random Accesses**: 1,000,000 per iteration
- **Latency Test Accesses**: 100,000 per buffer size

## Factors Affecting Results

### System-Level Factors
- **CPU Architecture**: Cache sizes, memory controllers, and NUMA topology
- **Memory Type**: DDR4/DDR5, speed ratings, and channel configuration
- **System Load**: Background processes can affect memory bandwidth
- **CPU Frequency Scaling**: Turbo boost and frequency governors
- **Operating System**: Kernel version, memory management policies

### Test-Specific Factors
- **Buffer Size**: Larger buffers may exceed cache sizes and show main memory performance
- **Access Patterns**: Sequential vs. random access stress different parts of the memory hierarchy
- **Compiler Optimizations**: Different optimization levels can affect results
- **CPU Affinity**: Process scheduling across NUMA nodes can impact results

## Troubleshooting

### Compilation Issues
```bash
# Install build dependencies (Ubuntu/Debian)
sudo apt-get install build-essential

# Install real-time library if missing
sudo apt-get install libc6-dev
```

### Memory Allocation Failures
- Reduce buffer size if system has insufficient RAM
- Check system memory limits: `ulimit -v`
- Ensure sufficient free memory: `free -h`

### Inconsistent Results
- Close unnecessary applications to reduce system load
- Run multiple times and average results
- Use `taskset` to pin process to specific CPU cores
- Disable CPU frequency scaling for consistent timing

## Advanced Usage

### Running with CPU Affinity
```bash
# Pin to CPU core 0
taskset -c 0 ./test_mem_bandwidth 64

# Pin to multiple cores
taskset -c 0-3 ./test_mem_bandwidth 128
```

### Performance Monitoring
```bash
# Monitor during execution
perf stat -e cache-references,cache-misses,instructions,cycles ./test_mem_bandwidth

# Profile cache behavior
valgrind --tool=cachegrind ./test_mem_bandwidth 64
```

### Batch Testing
```bash
# Test multiple buffer sizes
for size in 16 32 64 128 256 512 1024; do
    echo "Testing ${size}MB:"
    ./test_mem_bandwidth $size
    echo ""
done
```

## License

This project is designed for educational and benchmarking purposes. Modify and distribute as needed for your testing requirements.

## See Also

- [Intel Memory Latency Checker](https://www.intel.com/content/www/us/en/developer/articles/tool/intelr-memory-latency-checker.html)
- [STREAM Benchmark](https://www.cs.virginia.edu/stream/)
- [Linux perf tools](https://perf.wiki.kernel.org/index.php/Main_Page) 