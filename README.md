# Billion Row Challenge C++
I made this for fun to take a shot at the Java 1BRC in C++.

Contains performant code to generate data and a couple of solutions.

## How to generate data
[bin/gen.exe](https://github.com/markusaksli/billion-row-challenge-cpp/blob/master/bin/gen.exe) is the built Windows binary for [src/gen.cpp](https://github.com/markusaksli/billion-row-challenge-cpp/blob/master/src/gen.cpp).
There is a [gen_data](https://github.com/markusaksli/billion-row-challenge-cpp/blob/master/gen_data.bat) script in the repository root that will run this exe and generate the default `1brc.txt` file in the `data` folder.

To find out what args the exe has run it or the script with `-h` but the main ones are:
- `-stations [int (default 100)]` - Number of station names to use (up to 41343)
- `-lines [int (default 1000000000)]` - Number of lines to generate
- `-buffersize [double (default 4.0)]` - The size of the generation buffer (in GB). The bigger the better and around 16 GB the buffer will only need to be filled once.

## Original challenge description
See [1BRC Rules and limits](https://github.com/gunnarmorling/1brc?tab=readme-ov-file#rules-and-limits).
