#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>

#ifndef _WIN32

#else
#define NOMINMAX
#include <windows.h>
#endif

struct StationData
{
	double min = DBL_MAX;
	double max = -DBL_MAX;
	double sum = 0;
	int count = 0;

	void AddMeasurement(double temp)
	{
		max = std::max(max, temp);
		min = std::min(min, temp);
		count++;
		sum += temp;
	}
};

static inline double RoundTowardPositive1Decimal(const double d)
{
	return std::ceil(d * 10.0) * 0.1;
}

int main(int argc, char* argv[])
{
	std::ifstream fs(argv[1]);
	std::string line, stationName, tempStr;
	std::map<std::string, StationData> map;
	fs.seekg(3); // Skip BOM

	while (std::getline(fs, line))
	{
		auto splitPos = line.find(';');
		stationName = line.substr(0, splitPos);
		tempStr = line.substr(splitPos + 1, line.length());

		map[stationName].AddMeasurement(std::stod(tempStr));
	}

#ifdef _WIN32
	SetConsoleOutputCP(CP_UTF8);
#endif
	setvbuf(stdout, nullptr, _IOFBF, 1000);

	std::cout << std::fixed << std::setprecision(1);
	std::cout << u8"{";

	bool first = true;
	for (auto& kv : map)
	{
		if (!first) std::cout << u8", ";
		std::cout << kv.first << u8"="
			<< kv.second.min << u8"/"
			<< RoundTowardPositive1Decimal(kv.second.sum / kv.second.count) << u8"/"
			<< kv.second.max;
		first = false;
	}

	std::cout << u8"}";
}
