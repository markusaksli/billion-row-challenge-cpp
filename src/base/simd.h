#pragma once
#include <immintrin.h>

#define SIMD_Size 256
#define SIMD_U16Size SIMD_Size / 16
#define SIMD_CompU16EqMask(a, b) _mm256_cmpeq_epu16_mask(a, b)
#define SIMD_BroadcastU16(a) _mm256_set1_epi16(a)
#define SIMD_LoadUnAlignedU16(a) _mm256_loadu_si256(a)
#define SIMD_MoveMaskU16(a) _mm256_loadu_si256(a)

typedef __mmask16 SIMD_U16Mask;
typedef __m256i SIMD_Int;

SIMD_U16Mask SIMD_CmpEqWideChar(const SIMD_Int i, const wchar_t c)
{
	return SIMD_CompU16EqMask(i, SIMD_BroadcastU16(c));
}