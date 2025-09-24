# Billion Row Challenge C++
See: [1BRC](https://github.com/gunnarmorling/1brc)
I made this for fun to take a shot at the Java 1BRC in C++.

Contains performant code to generate data and a couple of solutions.

## How to generate data
[bin/gen.exe](https://github.com/markusaksli/billion-row-challenge-cpp/blob/master/bin/gen.exe) is the built Windows binary for [src/gen.cpp](https://github.com/markusaksli/billion-row-challenge-cpp/blob/master/src/gen.cpp).
Either run the built binary or build and run it yourself to generate data/1brc.data. It has a couple of args to configure the generation:
- `-stations [int (default 100)]` - Number of station names to use (up to 41343)
- `-lines [int (default 1bn)]` - Number of lines to generate
- `-buffersize [float (default 4.0)]` - The size of the generation buffer (in GB). The generation works by filling a buffer and then writing it to memory. The bigger the better.
