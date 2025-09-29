#include <fstream>
#include <string>
#include <unordered_map>

struct StationData
{
	double min = DBL_MAX;
	double sum = 0;
	uint32_t sampleCount = 0;
	double max = -DBL_MAX;
};

int main()
{
	std::ifstream fs("data/1brc.txt");
	std::string line, stationName, tempStr;
	std::unordered_map<std::string, StationData> map;

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

	int i = 0;
	uint64_t totalSamples = 0;
	for (auto& iter : map)
	{
		printf("%d:\t%s %.1f %.1f %.1f %u\n", i, iter.first.c_str(), iter.second.min, iter.second.sum / iter.second.sampleCount,
		       iter.second.max, iter.second.sampleCount);
		i++;
		totalSamples += iter.second.sampleCount;
	}

	printf("%llu\n", totalSamples);
}
