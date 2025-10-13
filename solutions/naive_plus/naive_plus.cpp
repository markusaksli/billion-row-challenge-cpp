#include <algorithm>
#include <cfloat>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

struct StationData
{
    double min = DBL_MAX;
    double max = -DBL_MAX;
    double sum = 0;
    int count = 0;

    inline void Add(double temp)
    {
        if (temp < min) min = temp;
        if (temp > max) max = temp;
        sum += temp;
        ++count;
    }
};

static inline double RoundTowardPositive1Decimal(const double d)
{
    return std::ceil(d * 10.0) * 0.1;
}

int main(int argc, char* argv[])
{
    std::ifstream fs(argv[1], std::ios::binary);
    fs.seekg(0, std::ios::end);
    const size_t fileSize = static_cast<size_t>(fs.tellg());
    fs.seekg(0, std::ios::beg);

    std::vector<char> buffer(fileSize + 1);
    fs.read(buffer.data(), fileSize);
    buffer[fileSize] = '\0';

    char* ptr = buffer.data();
	ptr += 3;

    std::unordered_map<std::string_view, StationData> map;
    map.reserve(100);

    char* lineStart = ptr;
    while (*ptr)
    {
        if (*ptr == '\n' || *ptr == '\r')
        {
            *ptr = '\0';
            if (lineStart[0] != '\0')
            {
                char* sep = static_cast<char*>(std::memchr(lineStart, ';', ptr - lineStart));
                if (sep)
                {
                    *sep = '\0';
                    std::string_view name(lineStart);
                    double temp = std::strtod(sep + 1, nullptr);
                    map[name].Add(temp);
                }
            }
            ++ptr;
            if (*ptr == '\n' || *ptr == '\r') ++ptr;
            lineStart = ptr;
        }
        else
            ++ptr;
    }

    std::vector<std::pair<std::string_view, StationData>> sorted(map.begin(), map.end());
    std::sort(sorted.begin(), sorted.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });

#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    setvbuf(stdout, nullptr, _IOFBF, 8192);

    std::cout << std::fixed << std::setprecision(1);

    std::cout << u8"{";
    bool first = true;
    for (const auto& [name, data] : sorted)
    {
        if (!first) std::cout << u8", ";
        first = false;
        std::cout << name << u8"="
            << data.min << u8"/"
            << RoundTowardPositive1Decimal(data.sum / data.count) << u8"/"
            << data.max;
    }
    std::cout << u8"}";
}