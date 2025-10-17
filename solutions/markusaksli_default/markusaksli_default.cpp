#include <algorithm>
#include <iomanip>
#include <iostream>

#include "../../src/base/buf_string.h"
#include "../../src/base/hash_map.h"
#include "../../src/base/platform_io.h"
#include "../../src/base/simd.h"

void Push1DecimalDouble(StringBuffer& writeBuf, const s64 scaled)
{
	s64 intPart = scaled / 10;
	s64 decimal = std::abs(scaled % 10);

	if (intPart == 0 && scaled < 0)
	{
		writeBuf.Push('-');
	}

	writeBuf.Push(intPart);
	writeBuf.Push('.');
	writeBuf.Push(static_cast<char>('0' + decimal));
}

void Push1DecimalDoubleRoundTowardPositive(StringBuffer& writeBuf, const double d)
{
	s64 scaled = static_cast<s64>(ceil(d * 10));
	Push1DecimalDouble(writeBuf, scaled);
}

void Push1DecimalDouble(StringBuffer& writeBuf, const double d)
{
	s64 scaled = static_cast<s64>(round(d * 10));
	Push1DecimalDouble(writeBuf, scaled);
}

struct StationData
{
	double min = DBL_MAX;
	double sum = 0;
	u32 count = 0;
	double max = -DBL_MAX;

	inline void Add(double temp)
	{
		if (temp > max) max = temp;
		if (temp < min) min = temp;
		count++;
		sum += temp;
	}
};

inline double ParseTempAsDouble(char*& pos)
{
	int sign = 1;
	char c = *pos;
	if (c == '-')
	{
		sign = -1;
		++pos;
		c = *pos;
	}
	int tens = c - '0';

	++pos;
	c = *pos;
	if (c != '.') {
		tens = tens * 10 + (c - '0');
		++pos;
	}
	++pos;

	int frac = *pos - '0';
	pos += 2;

	return sign * (tens + frac * 0.1);
}

int main(int argc, char* argv[])
{
	MappedFileHandle file;
	file.OpenRead(argv[1]);
	char* fileEnd = &file.data[file.length];
	char* pos = file.data + 3; // Skip BOM

	HashMap<String, StationData> map(100);
	while (pos < fileEnd)
	{
		String readString;
		readString.data = pos;
		SIMD_SeekToChar(pos, ';');
		readString.len = pos - readString.data;

		u32* insertionIndex;
		auto result = map.FindOrGetInsertionIndex(readString, insertionIndex);
		StationData* stationData;
		if (result)
		{
			stationData = &result->v;
		}
		else
		{
			map.InsertIndexed(readString, StationData(), insertionIndex);
			stationData = &map.items.Last().v;
		}
		pos++;

		stationData->Add(ParseTempAsDouble(pos));
	}

	Array<u64> sortedStations;
	sortedStations.InitMalloc(map.items.size);
	ForVector(sortedStations, i)
	{
		sortedStations[i] = i;
	}

	std::sort(sortedStations.data, sortedStations.data + sortedStations.size,
		[&](const u64 a, const u64 b) {
			return map.items[a].k < map.items[b].k;
		});

	StringBuffer writeBuf(4 * KB);

	writeBuf.Push('{');

	bool first = true;
	for (u64 i = 0; i < map.items.size; i++)
	{
		if (!first)
		{
			writeBuf.Push(", ");
		}
		const auto& pair = map.items[sortedStations[i]];
		const StationData& stationData = pair.v;
		writeBuf.PushF(pair.k, '=');
		Push1DecimalDouble(writeBuf, stationData.min);
		writeBuf.Push('/');
		Push1DecimalDoubleRoundTowardPositive(writeBuf, stationData.sum / stationData.count);
		writeBuf.Push('/');
		Push1DecimalDouble(writeBuf, stationData.max);
		first = false;
	}

	writeBuf.Push('}');

#ifdef _WIN32
	SetConsoleOutputCP(CP_UTF8);
#endif
	setvbuf(stdout, nullptr, _IOFBF, 4 * KB);

	std::cout.write(writeBuf.data, writeBuf.size);

	return 0;
}