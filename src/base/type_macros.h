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
constexpr u64 PAGE_SIZE = 4096;
constexpr u32 U32_MAX = 4294967294;

#define HASH_T u64

struct FloatV2
{
    float x, y;
};

struct s16v2
{
    s16 x, y;
};
