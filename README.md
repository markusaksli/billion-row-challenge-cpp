# Billion Row Challenge C++
I made this for fun to take a shot at the Java 1BRC in C++. I managed to get a 64x speedup compared to the naive solution (from 196 seconds to 3).

Contains much more performant code to generate the input data.

## Results

|Program                         |Average|Best  |Worst |Cold  |
|--------------------------------|-------|------|------|------|
|[markusaksli_fast_threaded](https://github.com/markusaksli/billion-row-challenge-cpp/blob/master/solutions/markusaksli_fast_threaded/markusaksli_fast_threaded.cpp)|3.05s  |3.04s |3.06s |3.04s |
|[markusaksli_default_threaded](https://github.com/markusaksli/billion-row-challenge-cpp/blob/master/solutions/markusaksli_default_threaded/markusaksli_default_threaded.cpp)|4.06s  |4.06s |4.07s |4.07s |
|[markusaksli_fast](https://github.com/markusaksli/billion-row-challenge-cpp/blob/master/solutions/markusaksli_fast/markusaksli_fast.cpp)|21.49s |21.28s|22.3s |21.29s|
|[markusaksli_default](https://github.com/markusaksli/billion-row-challenge-cpp/blob/master/solutions/markusaksli_default/markusaksli_default.cpp)|29.99s |28.36s|33.44s|28.36s|
|[naive_plus](https://github.com/markusaksli/billion-row-challenge-cpp/blob/master/solutions/naive_plus/naive_plus.cpp)|96.65s |95.13s|102.62s|102.62s|
|[naive](https://github.com/markusaksli/billion-row-challenge-cpp/blob/master/solutions/naive/naive.cpp)|195.99s|194.37s|197.43s|194.37s|
### Hardware
- CPU: AMD Ryzen 9 7950X 16-Core Processor
- RAM: 64.0 GB
- Storage: Samsung SSD 990 PRO 2TB (NVMe)
  - Sequential Read 7124 MB/s
  - Random Read 1359375 IOPS

## Getting started
[bin/gen.exe](https://github.com/markusaksli/billion-row-challenge-cpp/blob/master/bin/gen.exe) is the built Windows binary for [src/gen.cpp](https://github.com/markusaksli/billion-row-challenge-cpp/blob/master/src/gen.cpp).
There is a [gen_data](https://github.com/markusaksli/billion-row-challenge-cpp/blob/master/gen_data.bat) script in the repository root that will run this exe and generate the default `1brc.txt` file in the `data` folder.

To find out what args the exe has run it or the script with `-h`, but the main ones are:
- `-stations [int (default 100)]` - Number of station names to use (up to 41343)
- `-lines [int (default 1000000000)]` - Number of lines to generate
- `-buffersize [double (default 4.0)]` - The size of the generation buffer (in GB). The bigger the better, and around 16 GB the buffer will only need to be filled once.

The [build_all.bat](https://github.com/markusaksli/billion-row-challenge-cpp/blob/master/build_all.bat) script will build every solution in `solutions`, and [benchmark.bat](https://github.com/markusaksli/billion-row-challenge-cpp/blob/master/benchmark.bat) will benchmark each solution and save the results in a CSV file. To run [benchmark.bat](https://github.com/markusaksli/billion-row-challenge-cpp/blob/master/benchmark.bat), you need to have the [sync.exe](https://learn.microsoft.com/en-us/sysinternals/downloads/sync) Sysinternals tool in your Path and will need admin privileges to run it to flush the file system cache between solutions.

**To add a new solution, open a PR with**
- The new solution
- Updated [build_all.bat](https://github.com/markusaksli/billion-row-challenge-cpp/blob/master/build_all.bat)
- Updated [run_benchmark.ps1](https://github.com/markusaksli/billion-row-challenge-cpp/blob/master/run_benchmark.ps1)

## Original challenge description
See [1BRC Rules and limits](https://github.com/gunnarmorling/1brc?tab=readme-ov-file#rules-and-limits).

Worth noting that the actual longest station name in the input data is `Dolores Hidalgo Cuna de la Independencia Nacional` at 49 bytes, even though it's stated it will be < 100 bytes.

## Included Solutions
### [naive](https://github.com/markusaksli/billion-row-challenge-cpp/blob/master/solutions/naive/naive.cpp)
The canonical simple way to solve the challenge in C++ (although it already needs some platform-specific code on Windows to output in UTF-8).

### [naive_plus](https://github.com/markusaksli/billion-row-challenge-cpp/blob/master/solutions/naive_plus/naive_plus.cpp)
Basic attempt at optimizing the naive solution, primarily reducing string copying.

### [markusaksli_default](https://github.com/markusaksli/billion-row-challenge-cpp/blob/master/solutions/markusaksli_default/markusaksli_default.cpp)
My default code for parsing text and using my simple base layer data structures. This is mostly how I would write my basic solution to any text parsing problem.

**Improvements**
- Memory mapping the input file
- Seeking to the delimiter `;` using SIMD
- Fast double parsing (if I needed full double parsing would use a library, but it's simpler to just write the char-by-char parsing yourself in this case)
- No string copying (just using views into the file data)
- Simple inlineable hash function and table lookup

### [markusaksli_fast](https://github.com/markusaksli/billion-row-challenge-cpp/blob/master/solutions/markusaksli_fast/markusaksli_fast.cpp)
An actual attempt at optimizing my default approach.

**Improvements**
- Using a fixed-size flat power-of-2 hash map with linear probing for even simpler lookups
- Custom compact key structure for the map so more of them can fit in cache
- Did some experimentation on the quickest way to parse the delimiter, turns out just a basic char-by-char loop that combines calculating the hash worked better than anything smart.
- Didn't do any hash seed searching in the source station names for a perfect hash function, felt too hacky or cheap.

### [markusaksli_default_threaded](https://github.com/markusaksli/billion-row-challenge-cpp/blob/master/solutions/markusaksli_default_threaded/markusaksli_default_threaded.cpp)
Multithreaded version of [markusaksli_default](https://github.com/markusaksli/billion-row-challenge-cpp/blob/master/solutions/markusaksli_default/markusaksli_default.cpp).

- Partitions the file by dividing the content by the number of threads and seeking to line boundaries
- Each thread fills its own hash map
- Simple merge where we loop through each pair in the thread hash map and do a lookup into the main thread's hash map.

### [markusaksli_fast_threaded](https://github.com/markusaksli/billion-row-challenge-cpp/blob/master/solutions/markusaksli_fast_threaded/markusaksli_fast_threaded.cpp)
Multithreaded version of [markusaksli_fast](https://github.com/markusaksli/billion-row-challenge-cpp/blob/master/solutions/markusaksli_fast/markusaksli_fast.cpp) with the same principles.

### Potential unexplored optimizations
- Running a search to make a perfect hash function (probably the biggest improvement)
- Reducing the number of page faults in the memory-mapped read or swapping for buffered sequential reads
- Post-parse per-thread restructuring and partial sorting for faster merge at the end
- Replacing std::sort (quicksort) with a radix sort
