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
	double sum = 0;
	uint32_t sampleCount = 0;
	double max = -DBL_MAX;
};

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

		StationData& data = map[stationName];
		double temp = std::stod(tempStr);
		data.max = std::max(data.max, temp);
		data.min = std::min(data.min, temp);
		data.sum += temp;
		data.sampleCount++;
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
			<< kv.second.sum / kv.second.sampleCount << u8"/"
			<< kv.second.max;
		first = false;
	}

	std::cout << u8"}";
}
