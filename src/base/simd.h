#pragma once
#include <immintrin.h>

#define SIMD_Size 256
#define SIMD_LoadUnAligned(a) _mm256_loadu_si256(a)
#define SIMD_StoreUnAligned(dst, a) _mm256_storeu_si256(dst, a)

#define SIMD_U16Size SIMD_Size / 16
#define SIMD_CompU16EqMask(a, b) _mm256_cmpeq_epu16_mask(a, b)
#define SIMD_BroadcastU16(a) _mm256_set1_epi16(a)

#define SIMD_U8Size SIMD_Size / 8
#define SIMD_CompU8EqMask(a, b) _mm256_cmpeq_epu8_mask(a, b)
#define SIMD_BroadcastU8(a) _mm256_set1_epi8(a)

typedef __mmask16 SIMD_U16Mask;
typedef	u32 SIMD_U8Mask;
typedef __m256i SIMD_Int;

inline SIMD_U16Mask SIMD_CmpEqWideChar(const SIMD_Int i, const wchar_t c)
{
	return SIMD_CompU16EqMask(i, SIMD_BroadcastU16(c));
}

inline SIMD_U8Mask SIMD_CmpEqChar(const SIMD_Int i, const char c)
{
	return SIMD_CompU8EqMask(i, SIMD_BroadcastU8(c));
}