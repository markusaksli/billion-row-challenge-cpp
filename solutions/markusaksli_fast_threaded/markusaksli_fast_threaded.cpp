#include <algorithm>
#include <cfloat>
#include <iomanip>
#include <iostream>

#include "../../src/base/buf_string.h"
#include "../../src/base/platform_io.h"
#include "../../src/base/simd.h"
#include "../../src/base/type_macros.h"

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
	u32 entryIndex = 0;

	__forceinline void Add(double temp)
	{
		if (temp < min) min = temp;
		if (temp > max) max = temp;
		sum += temp;
		++count;
	}

	__forceinline void Merge(const StationData& other)
	{
		if (other.max > max) max = other.max;
		if (other.min < min) min = other.min;
		sum += other.sum;
		count += other.count;
	}
};

// Use a fixed size flat power of 2 map with linear probing
struct FixedFlatHashMapPow2
{
	static constexpr u64 capacity = 512;

	struct Entry // Compact key struct to fit more in cache
	{
		const char* name;
		u32 namelen;
		u32 valueIndex;
		HASH_T hash; // Didn't see much of a difference keeping the hash in here on single threaded but it saves us recalculating it during the merge

		__forceinline bool operator<(const Entry& other) const
		{
			const int cmp = strncmp(name, other.name, (namelen < other.namelen) ? namelen : other.namelen);

			if (cmp < 0) return true;
			if (cmp > 0) return false;

			return namelen < other.namelen;
		}
	};
	Entry items[capacity];

	void Init()
	{
		memset(items, 0, sizeof(Entry) * capacity);
	}

	__forceinline u32 FindOrInsert(const String& k, const HASH_T hash, u32& numStations, StationData* stations)
	{
		u32 idx = hash & (capacity - 1); // Requires power of 2 size
		Entry* __restrict entries = items;

		for (;;)
		{
			Entry& e = entries[idx];
			if (e.namelen == 0)
			{
				assert(numStations < 100);
				e.hash = hash;
				e.name = k.data;
				e.namelen = k.len;
				e.valueIndex = numStations;
				stations[numStations].entryIndex = idx;
				u32 ret = numStations;
				++numStations;
				return ret;
			}
			if (e.hash == hash && k.Equals(e.name, e.namelen)) return e.valueIndex;
			idx = (idx + 1) & (capacity - 1);
		}
	}
};

struct ThreadMemory
{
	std::thread* thread;
	char* pos;
	const char* parseEnd;
	FixedFlatHashMapPow2 map;
	Array<StationData> stations;
	u32 numStations = 0;
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

__forceinline void SeekAndHash_1(char*& pos, HASH_T& hash)
{
	while (*pos != ';')
	{
		fnv1aStep(*pos, hash);
		++pos;
	}
}

void Parse(ThreadMemory* mem)
{
	mem->map.Init();
	mem->stations.InitMalloc(100);
	ForVector(mem->stations, i)
	{
		mem->stations[i] = StationData();
	}

	while (mem->pos < mem->parseEnd)
	{
		String readString;
		readString.data = mem->pos;
		HASH_T hash = FNV_PRIME;
		SeekAndHash_1(mem->pos, hash);
		readString.len = mem->pos - readString.data;

		u32 result = mem->map.FindOrInsert(readString, hash, mem->numStations, mem->stations.data);
		StationData& stationData = mem->stations[result];
		mem->pos++;

		stationData.Add(ParseTempAsDouble(mem->pos));
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
		for (u64 j = 0; j < other.numStations; j++)
		{
			const StationData& otherData = other.stations[j];
			const auto& otherEntry = other.map.items[otherData.entryIndex];
			u32 result = mainMem.map.FindOrInsert(String((char*)otherEntry.name, otherEntry.namelen), otherEntry.hash, mainMem.numStations, mainMem.stations.data);
			StationData& stationData = mainMem.stations[result];
			stationData.Merge(otherData);
		}
	}

	std::sort(mainMem.stations.data, mainMem.stations.data + mainMem.stations.size,
		[&](const StationData& a, const StationData& b) {
			return mainMem.map.items[a.entryIndex] < mainMem.map.items[b.entryIndex];
		});

	StringBuffer writeBuf(4 * KB);

	writeBuf.Push('{');

	bool first = true;
	for (u64 i = 0; i < mainMem.numStations; i++)
	{
		if (!first)
		{
			writeBuf.Push(", ");
		}
		const StationData& stationData = mainMem.stations[i];
		writeBuf.Push(mainMem.map.items[stationData.entryIndex].name, mainMem.map.items[stationData.entryIndex].namelen);
		writeBuf.Push('=');
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