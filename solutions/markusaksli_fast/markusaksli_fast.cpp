#include <algorithm>
#include <iomanip>
#include <iostream>

#include "../../src/base/buf_string.h"
#include "../../src/base/platform_io.h"

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

struct StationData // Only 16 bytes!
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
};

struct StationMapping
{
	u32 header;
	u32 data;
};

// Compact representation for key strings in a buffer, also allows us to index into the buffer instead of storing full pointers for the key strings
StringBuffer strbuf(1 * KB);

// Fixed size flat power of 2 map with linear probing
struct FixedFlatHashMapPow2
{
	static constexpr u64 capacity = 512;

	struct Entry // Compact key struct to fit more in cache (only 4 bytes)
	{
		u16 name;
		u8 namelen;
		u8 valueIndex;

		__forceinline bool operator<(const Entry& other) const
		{
			const int cmp = strncmp(&strbuf[name], &strbuf[other.name], (namelen < other.namelen) ? namelen : other.namelen);

			if (cmp < 0) return true;
			if (cmp > 0) return false;

			return namelen < other.namelen;
		}
	};
	Entry items[capacity];

	FixedFlatHashMapPow2()
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
				e.name = strbuf.Bytes();
				e.namelen = k.len;
				String str = strbuf.PushStringCopy(k);
				e.valueIndex = numStations;
				stationToHeader[numStations] = idx;
				u32 ret = numStations;
				++numStations;
				return ret;
			}
			if (k.Equals(&strbuf[e.name], e.namelen)) return e.valueIndex;
			idx = (idx + 1) & (capacity - 1);
		}
	}
};

_forceinline void GetByteAndShiftU64(u8& c, u64& x, const u8 n = 1)
{
	c = x & 0xff;
	x = x >> (8 * n);
}

// Parsing as s16 avoids FP in the hot loop and allows us to make the temp struct smaller.

// Assembly is different thanks to the strategy here but hard to tell if there's a performance impact, single load is maybe ever so slightly faster?
// Everything should be in cache anyway so realistically the pointer dereferences in the normal version should all come from cache.
/*
__forceinline s16 ParseTempAsS16(char*& pos)
{
	s16 sign = 1;
	char c = *pos;
	if (c == '-') 
	{
		sign = -1;
		++pos;
		c = *pos;
	}
	s16 tens = (c - '0') * 10;

	++pos;
	c = *pos;
	if (c != '.') {
		tens = tens * 10 + (c - '0') * 10;
		++pos;
	}
	++pos;

	tens = sign * (tens + *pos - '0');
	pos += 2;

	return tens;
}
*/

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

// None of these are faster than a simple byte by byte read where we touch each byte once... go figure
/*
__forceinline void SeekAndHash_8(char*& pos, HASH_T& hash)
{
	while (true)
	{
		u64 chunk = *reinterpret_cast<const u64*>(pos);
		// Check for ';' in 8 bytes
		u64 test = ((chunk ^ 0x3B3B3B3B3B3B3B3BULL) - 0x0101010101010101ULL) & ~chunk & 0x8080808080808080ULL;
		if (test)
		{
			int offset = _tzcnt_u64(test) >> 3;
			for (int i = 0; i < offset; ++i)
				fnv1aStep(static_cast<char>(chunk >> (i * 8)), hash);
			pos += offset;
			return;
		}

		// Hash all 8 bytes
		for (int i = 0; i < 8; ++i)
			fnv1aStep(static_cast<char>(chunk >> (i * 8)), hash);
		pos += 8;
	}
}

__forceinline void SeekAndHash_32(char*& pos, HASH_T& hash)
{
	const __m256i target = _mm256_set1_epi8(';');
	while (true)
	{
		__m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(pos));
		u32 mask = _mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, target));

		if (mask == 0)
		{
			for (int i = 0; i < 32; ++i)
				fnv1aStep(pos[i], hash);
			pos += 32;
			continue;
		}

		u64 offset = _tzcnt_u32(mask); // first matching byte
		for (int i = 0; i < offset; i++)
		{
			fnv1aStep(pos[i], hash);
		}
		pos += offset;
		break;
	}
}

__forceinline void SeekAndHash_64(char*& pos, HASH_T& hash)
{
	const __m256i target = _mm256_set1_epi8(';');
	__m256i chunk1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(pos));
	__m256i chunk2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(pos + 32));

	// compare and build 64-bit mask
	u32 mask1 = _mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk1, target));
	u32 mask2 = _mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk2, target));
	u64 mask = static_cast<u64>(mask2) << 32 | mask1;

	u32 offset = _tzcnt_u64(mask); // first matching byte
	for (int i = 0; i < offset; i++)
	{
		fnv1aStep(pos[i], hash);
	}
	pos += offset;
}

__forceinline void SeekAndHash_SIMDHash64(char*& pos, HASH_T& hash)
{
	const __m256i target = _mm256_set1_epi8(';');
	const __m256i chunk1 = _mm256_loadu_si256((__m256i*)pos);
	const __m256i chunk2 = _mm256_loadu_si256((__m256i*)(pos + 32));
	const u32 mask1 = _mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk1, target));
	const u32 mask2 = _mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk2, target));
	const u64 mask = (static_cast<u64>(mask2) << 32) | mask1;
	const u32 offset = _tzcnt_u64(mask);

	// SIMD hash
	const u64 before_mask = (1ull << offset) - 1ull;

	const __m256i blend_mask1 = _mm256_set_epi64x(
		(before_mask >> 48) & 0xFFFF, (before_mask >> 32) & 0xFFFF,
		(before_mask >> 16) & 0xFFFF, (before_mask >> 0) & 0xFFFF);

	__m256i masked1 = chunk1;
	__m256i masked2 = chunk2;
	if (offset < 32) {
		const __m256i cmpmask = _mm256_setr_epi8(
			0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
			16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31);
		__m256i active = _mm256_cmpgt_epi8(_mm256_set1_epi8(offset), cmpmask);
		masked1 = _mm256_and_si256(masked1, active);
		masked2 = _mm256_setzero_si256();
	}
	else if (offset < 64) {
		const int off2 = offset - 32;
		const __m256i cmpmask = _mm256_setr_epi8(
			0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
			16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31);
		__m256i active2 = _mm256_cmpgt_epi8(_mm256_set1_epi8(off2), cmpmask);
		masked2 = _mm256_and_si256(masked2, active2);
	}

	const __m256i xor32 = _mm256_xor_si256(masked1, masked2);
	__m128i xor_lo = _mm256_castsi256_si128(xor32);
	__m128i xor_hi = _mm256_extracti128_si256(xor32, 1);
	__m128i folded = _mm_xor_si128(xor_lo, xor_hi);

	u64 partial;
	{
		__m128i tmp = _mm_xor_si128(folded, _mm_srli_si128(folded, 8));
		partial = _mm_cvtsi128_si64(tmp);
	}

	hash ^= partial;
	hash *= FNV_PRIME;

	pos += offset;
}
*/

int main(int argc, char* argv[])
{
	MappedFileHandle file;
	file.OpenRead(argv[1]);
	char* fileEnd = &file.data[file.length];
	char* pos = file.data + 3; // Skip BOM

	FixedFlatHashMapPow2 map;
	FixedArray<StationData, NUM_STATIONS> stations;
	FixedArray<u32, NUM_STATIONS> stationToHeader;
	for (u32 i = 0; i < NUM_STATIONS; i++)
	{
		stations[i] = StationData();
	}

	u32 numStations = 0;

	while (pos < fileEnd)
	{
		String readString;
		readString.data = pos;
		HASH_T hash = FNV_PRIME;
		SeekAndHash_1(pos, hash);
		readString.len = pos - readString.data;

		u32 result = map.FindOrInsert(readString, hash, numStations, stationToHeader.data);
		StationData& stationData = stations[result];
		pos++;

		stationData.Add(ParseTempAsS16SingleLoad(pos));
	}

	// 95% spent above, don't really care about the sort
	FixedArray<StationMapping, NUM_STATIONS> mapping;
	for (u32 i = 0; i < NUM_STATIONS; i++)
	{
		mapping[i].data = i;
		mapping[i].header = stationToHeader[i];
	}

	std::sort(mapping.data, mapping.data + NUM_STATIONS,
		[&](const StationMapping& a, const StationMapping& b) {
			return map.items[a.header] < map.items[b.header];
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
		const StationData& stationData = stations[m.data];
		const auto header = map.items[m.header];
		writeBuf.Push(&strbuf[header.name], header.namelen);
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