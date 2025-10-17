#include <algorithm>
#include <iomanip>
#include <iostream>

#include "../../src/base/buf_string.h"
#include "../../src/base/platform_io.h"
#include "../../src/base/simd.h"

#define NUM_STATIONS 100

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
	s16 min = 32767;
	s16 max = -32768;
	u32 count = 0;
	s64 sum = 0;

	__forceinline void Add(s16 temp)
	{
		if (temp < min) min = temp;
		if (temp > max) max = temp;
		++count;
		sum += temp;
	}

	__forceinline void Merge(const StationData& other)
	{
		if (other.max > max) max = other.max;
		if (other.min < min) min = other.min;
		count += other.count;
		sum += other.sum;
	}
};

struct StationMapping
{
	u32 header;
	u32 data;
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

	__forceinline u32 FindOrInsert(const String& k, const HASH_T hash, u32& numStations, u32 stationToHeader[NUM_STATIONS])
	{
		u32 idx = hash & (capacity - 1); // Requires power of 2 size
		Entry* __restrict entries = items;

		for (;;)
		{
			Entry& e = entries[idx];
			if (e.namelen == 0)
			{
				assert(numStations < NUM_STATIONS);
				e.hash = hash;
				e.name = k.data;
				e.namelen = k.len;
				e.valueIndex = numStations;
				stationToHeader[numStations] = idx;
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
	FixedArray<StationData, NUM_STATIONS> stations;
	FixedArray<u32, NUM_STATIONS> stationToHeader;
	u32 numStations = 0;
};

__forceinline s16 ParseTempAsS16SingleLoad(char*& pos)
{
	s16 sign = 1;
	u64 data = *((u64*)pos);
	u8 c = data & 0xff;
	data = data >> 8;
	if (c == '-')
	{
		sign = -1;
		++pos;
		c = data & 0xff;
		data = data >> 8;
	}
	s16 tens = (c - '0') * 10;

	c = data & 0xff;
	data = data >> 8;
	if (c != '.') {
		tens = tens * 10 + (c - '0') * 10;
		++pos;
		data = data >> 8;
	}

	c = data & 0xff;
	tens = sign * (tens + c - '0');
	pos += 4;

	return tens;
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
	for (u32 i = 0; i < NUM_STATIONS; i++)
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

		u32 result = mem->map.FindOrInsert(readString, hash, mem->numStations, mem->stationToHeader.data);
		StationData& stationData = mem->stations[result];
		mem->pos++;

		stationData.Add(ParseTempAsS16SingleLoad(mem->pos));
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
			// No matter what kind of prefetching I try it just doesn't seem to beat default paging on windows
			// PrefetchVirtualMemory(file.data, 64 * MB, 4 * MB);
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
			const auto& otherEntry = other.map.items[other.stationToHeader[j]];
			u32 result = mainMem.map.FindOrInsert(String((char*)otherEntry.name, otherEntry.namelen), otherEntry.hash, mainMem.numStations, mainMem.stationToHeader.data);
			StationData& stationData = mainMem.stations[result];
			stationData.Merge(otherData);
		}
	}

	FixedArray<StationMapping, NUM_STATIONS> mapping;
	for (u32 i = 0; i < NUM_STATIONS; i++)
	{
		mapping[i].data = i;
		mapping[i].header = mainMem.stationToHeader[i];
	}

	// Sort and output
	std::sort(mapping.data, mapping.data + NUM_STATIONS,
		[&](const StationMapping& a, const StationMapping& b) {
			return mainMem.map.items[a.header] < mainMem.map.items[b.header];
		});

	StringBuffer writeBuf(4 * KB);

	writeBuf.Push('{');

	bool first = true;
	for (u32 i = 0; i < NUM_STATIONS; i++)
	{
		if (!first)
		{
			writeBuf.Push(", ");
		}
		const StationMapping m = mapping[i];
		const StationData& stationData = mainMem.stations[m.data];
		const auto header = mainMem.map.items[m.header];
		writeBuf.Push(header.name, header.namelen);
		writeBuf.Push('=');
		Push1DecimalDouble(writeBuf, stationData.min * 0.1);
		writeBuf.Push('/');
		Push1DecimalDoubleRoundTowardPositive(writeBuf, (stationData.sum * 0.1) / stationData.count);
		writeBuf.Push('/');
		Push1DecimalDouble(writeBuf, stationData.max * 0.1);
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