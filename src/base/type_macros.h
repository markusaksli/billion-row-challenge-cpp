#pragma once
typedef unsigned char u8;
typedef signed char s8;
typedef unsigned short u16;
typedef signed short s16;
typedef unsigned long u32;
typedef signed long s32;
typedef signed long long s64;
typedef unsigned long long u64;

constexpr u64 KB = 1024ULL;
constexpr u64 MB = KB * 1024ULL;
constexpr u64 GB = MB * 1024ULL;
constexpr u64 TB = GB * 1024ULL;
constexpr u32 PAGE_SIZE = 4 * KB;
constexpr u32 U32_MAX = 4294967294;

constexpr u64 NUM_M = 1000 * 1000;
constexpr u64 NUM_BN = NUM_M * 1000;


#define HASH_T u64

