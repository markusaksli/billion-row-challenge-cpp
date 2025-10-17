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
	double max = -DBL_MAX;
	double sum = 0;
	u32 count = 0;

	inline void Add(double temp)
	{
		if (temp > max) max = temp;
		if (temp < min) min = temp;
		count++;
		sum += temp;
	}

	inline void Merge(const StationData& other)
	{
		if (other.max > max) max = other.max;
		if (other.min < min) min = other.min;
		sum += other.sum;
		count += other.count;
	}
};

struct ThreadMemory
{
	std::thread* thread;
	HashMap<String, StationData> map;
	char* pos;
	const char* parseEnd;
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

void Parse(ThreadMemory* mem)
{
	mem->map.InitAuto(100);
	while (mem->pos < mem->parseEnd)
	{
		String readString;
		readString.data = mem->pos;
		SIMD_SeekToChar(mem->pos, ';');
		readString.len = mem->pos - readString.data;

		u32* insertionIndex;
		auto result = mem->map.FindOrGetInsertionIndex(readString, insertionIndex);
		StationData* stationData;
		if (result)
		{
			stationData = &result->v;
		}
		else
		{
			mem->map.InsertIndexed(readString, StationData(), insertionIndex);
			stationData = &mem->map.items.Last().v;
		}
		mem->pos++;

		stationData->Add(ParseTempAsDouble(mem->pos));
	}
}

int main(int argc, char* argv[])
{
	MappedFileHandle file;
	file.OpenRead(argv[1]);
	char* fileEnd = &file.data[file.length];
	char* pos = file.data + 3; // Skip BOM

	// u32 numThreads = 2;
	u32 numThreads = std::thread::hardware_concurrency() - 1;
	u64 perThreadBytes = file.length / numThreads;
	Array<ThreadMemory> mem;
	mem.InitMallocZero(numThreads);

	// Partition the file
	for (u32 i = 0; i < numThreads; i++)
	{
		mem[i].pos = pos;
		if (i != numThreads - 1)
		{
			pos += perThreadBytes;
			SIMD_SeekToChar(pos, '\n');
			pos++;
			mem[i].parseEnd = pos;
		}
	}
	ThreadMemory& mainMem = mem[numThreads - 1];
	mainMem.parseEnd = fileEnd;

	// Parse
	for (u32 i = 0; i < numThreads - 1; i++)
	{
		mem[i].thread = new std::thread(Parse, &mem[i]);
	}
	Parse(&mainMem);

	// Merge results
	for (u32 i = 0; i < numThreads - 1; i++)
	{
		ThreadMemory& other = mem[i];
		other.thread->join();
		for(u64 j = 0; j < other.map.items.size; j++)
		{
			u32* insertionIndex;
			const auto& otherPair = other.map.items[j];
			auto result = mainMem.map.FindOrGetInsertionIndex(otherPair.k, insertionIndex);
			if (result)
			{
				result->v.Merge(otherPair.v);
			}
			else
			{
				mainMem.map.InsertIndexed(otherPair.k, otherPair.v, insertionIndex);
			}
		}
	}

	Array<u64> sortedStations;
	sortedStations.InitMalloc(mainMem.map.items.size);
	ForVector(sortedStations, i)
	{
		sortedStations[i] = i;
	}

	std::sort(sortedStations.data, sortedStations.data + sortedStations.size,
		[&](const u64 a, const u64 b) {
			return mainMem.map.items[a].k < mainMem.map.items[b].k;
		});

	StringBuffer writeBuf(4 * KB);

	writeBuf.Push('{');

	bool first = true;
	for (u64 i = 0; i < mainMem.map.items.size; i++)
	{
		if (!first)
		{
			writeBuf.Push(", ");
		}
		const auto& pair = mainMem.map.items[sortedStations[i]];
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