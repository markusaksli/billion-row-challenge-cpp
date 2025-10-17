#pragma once
#include <immintrin.h>

#include "type_macros.h"

inline void SIMD_SeekToChar64(char*& pos, const char c)
{
	const __m256i target = _mm256_set1_epi8(c);
	while (true)
	{
		__m256i chunk1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(pos));
		__m256i chunk2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(pos + 32));

		// compare and build 64-bit mask
		u32 mask1 = _mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk1, target));
		u32 mask2 = _mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk2, target));
		u64 mask = static_cast<u64>(mask2) << 32 | mask1;

		if (mask == 0)
		{
			pos += 64;
			continue;
		}

		u64 offset = _tzcnt_u64(mask);  // first matching byte
		pos += offset;
		break;
	}
}

inline void SIMD_SeekToChar32(char*& pos, const char c)
{
	const __m256i target = _mm256_set1_epi8(c);
	while (true)
	{
		__m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(pos));

		// compare and build 64-bit mask
		u32 mask = _mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, target));
		if (mask == 0)
		{
			pos += 32;
			continue;
		}

		u32 offset = _tzcnt_u32(mask);  // first matching byte
		pos += offset;
		break;
	}
}

inline void SIMD_SeekToChar(char*& pos, const char c)
{
	SIMD_SeekToChar32(pos, c);
}

inline void SIMD_Prefetch(const char* pos)
{
	_mm_prefetch(pos + 256, _MM_HINT_T0);
}
