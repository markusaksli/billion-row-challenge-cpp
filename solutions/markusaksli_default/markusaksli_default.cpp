#include <algorithm>
#include <cfloat>
#include <iomanip>
#include <iostream>

#include "../../src/base/buf_string.h"
#include "../../src/base/hash_map.h"
#include "../../src/base/platform_io.h"
#include "../../src/base/type_macros.h"
#include "../../src/base/simd.h"

struct StationData
{
	double min = DBL_MAX;
	double mean = 0;
	u32 sampleCount = 0;
	double max = -DBL_MAX;

	void AddMeasurement(double temp)
	{
		max = std::max(max, temp);
		min = std::min(min, temp);
		sampleCount++;
		mean += (temp - mean) / sampleCount;
	}

	// TODO: Gather and add multiple measurements at once
};

StringBuffer strbuf(4 * MB);
String readString;
MappedFileHandle file;
u64 pos = 3; // Skip BOM
SIMD_Int simdChunk;

void Read()
{
	simdChunk = SIMD_LoadUnAligned(reinterpret_cast<SIMD_Int const*>(&file.data[pos]));
}

void SeekToChar(const char c)
{
	u64 seekStart = pos;
	readString = strbuf.PushUninitString(); // Store the station name string as we skip to the delimiter
	while (true)
	{
		Read();
		SIMD_U8Mask mask = SIMD_CmpEqChar(simdChunk, c);
		if (mask == 0)
		{
			strbuf.Grow(SIMD_Size);
			SIMD_StoreUnAligned((SIMD_Int*)strbuf.Back(), simdChunk);
			pos += SIMD_U8Size;
			continue;
		}

		u32 scanResult = 0;
		_BitScanForward(&scanResult, mask);
		memcpy(strbuf.Back(), &simdChunk, scanResult);
		strbuf.Grow(scanResult);
		pos += scanResult;
		break;
	}
	readString.len = pos - seekStart;
}

double RoundTowardPositive1Decimal(const double d)
{
	return ceil(d * 10) / 10.0;
}

int main(int argc, char* argv[])
{
	HashMap<String, StationData> map(100000);
	file.OpenRead(argv[1]);

	while (true)
	{
		if (pos >= file.length || file.data[pos] == '\0') break;
		SeekToChar(';');

		StationData* stationData;
		auto result = map.Find(readString);
		if (result != nullptr)
		{
			strbuf.Pop(readString);
			stationData = &result->v;
		}
		else
		{
			map.InsertLastIndexed(readString, StationData());
			stationData = &map.items.Last().v;
		}

		// Minimal char by char parsing that matches the challenge spec
		pos++;
		s32 sign = 1;
		s32 scaled;
		char c = file.data[pos];
		if (c == '-')
		{
			sign = -1;
			pos++;
			c = file.data[pos];
		}
		scaled = (c - '0') * 10L;

		pos++;
		c = file.data[pos];
		if (c == '.')
		{
			pos++;
		}
		else
		{
			scaled *= 10;
			scaled += (c - '0') * 10L;
			pos += 2;
		}

		c = file.data[pos];
		scaled += (c - '0');
		scaled *= sign;
		stationData->AddMeasurement(scaled / 10.0);
		pos += 2;
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

#ifdef _WIN32
	SetConsoleOutputCP(CP_UTF8);
#endif
	setvbuf(stdout, nullptr, _IOFBF, 1000);

	std::cout << std::fixed << std::setprecision(1);
	std::cout << u8"{";

	bool first = true;
	ForVector(map.items, i)
	{
		if (!first) std::cout << u8", ";

		auto pair = map.items[sortedStations[i]];
		std::cout.write(pair.k.data, pair.k.len);
		std::cout << u8"=";
		std::cout << pair.v.min << u8"/"
			<< RoundTowardPositive1Decimal(pair.v.mean) << u8"/"
			<< pair.v.max;
		first = false;
	}

	std::cout << u8"}";
}