#pragma once
#include <cassert>
#include <cstdlib>
#include <cstring>

#include "raddbg_markup.h"
#include "type_macros.h"

constexpr u64 PERMUTE_SEED = 0x5eeda628748fc822;

// https://github.com/camel-cdr/cauldron/blob/main/tools/random/permute/README.md
// For nice bijective function for sampling a random range of unique elements
struct Permute64 {
	u64 mask, len, seed;

	void Init(u64 len, u64 seed)
	{
		u64 mask = len - 1;
		mask |= mask >> 1;
		mask |= mask >> 2;
		mask |= mask >> 4;
		mask |= mask >> 8;
		mask |= mask >> 16;
		mask |= mask >> 32;
		this->mask = mask;
		this->len = len;
		this->seed = seed;
	}

	u64 Permute(u64 idx)
	{
		u64 const mask = this->mask;
		u64 const len = this->len;
		u64 const seed = this->seed;
		do {
			idx ^= seed;
			/* splittable64 */
			idx ^= (idx & mask) >> 30; idx *= UINT64_C(0xBF58476D1CE4E5B9);
			idx ^= (idx & mask) >> 27; idx *= UINT64_C(0x94D049BB133111EB);
			idx ^= (idx & mask) >> 31;
			idx *= UINT64_C(0xBF58476D1CE4E5B9);

			idx ^= seed >> 32;
			idx &= mask;
			idx *= UINT32_C(0xED5AD4BB);

			idx ^= seed >> 48;
			///* hash16_xm3 */
			idx ^= (idx & mask) >> 7; idx *= 0x2993u;
			idx ^= (idx & mask) >> 5; idx *= 0xE877u;
			idx ^= (idx & mask) >> 9; idx *= 0x0235u;
			idx ^= (idx & mask) >> 10;

			/* From Andrew Kensler: "Correlated Multi-Jittered Sampling" */
			idx ^= seed; idx *= 0xe170893d;
			idx ^= seed >> 16;
			idx ^= (idx & mask) >> 4;
			idx ^= seed >> 8; idx *= 0x0929eb3f;
			idx ^= seed >> 23;
			idx ^= (idx & mask) >> 1; idx *= 1 | seed >> 27;
			idx *= 0x6935fa69;
			idx ^= (idx & mask) >> 11; idx *= 0x74dcb303;
			idx ^= (idx & mask) >> 2; idx *= 0x9e501cc3;
			idx ^= (idx & mask) >> 2; idx *= 0xc860a3df;
			idx &= mask;
			idx ^= idx >> 5;
		} while (idx >= len);
		return idx;
	}
};

template<typename T>
struct Vector
{
	u64 size = 0;
	u64 reserved = 0;
	T* data = nullptr;

	static u64 Bytes(const u64 num)
	{
		return num * sizeof(T);
	}

	u64 Bytes() const
	{
		return Bytes(size);
	}

	u64 ReservedBytes() const
	{
		return Bytes(reserved);
	}

	void Init(u64 toReserve)
	{
		assert(toReserve > 0);
		assert(data == nullptr && reserved == 0 && size == 0);
		data = (T*)malloc(Bytes((toReserve)));
		reserved = toReserve;
	}

	void InitZero(u64 toReserve)
	{
		Init(toReserve);
		memset(data, 0, ReservedBytes());
	}

	Vector() = default;

	explicit Vector(const u64 toReserve)
	{
		Init(toReserve);
	}

	void AssertNotEmpty()
	{
		assert(reserved > 0 && data != nullptr);
	}

	~Vector()
	{
		if (data != nullptr)
		{
			free(data);
			data = nullptr;
			size = 0;
			reserved = 0;
		}
	}

	void _reserve(const u64 toReserve)
	{
		AssertNotEmpty();
		assert(toReserve > reserved);

		data = (T*)realloc(data, Bytes(toReserve));
		reserved = toReserve;
	}

	void _grow()
	{
		AssertNotEmpty();

		u64 newSize = size + 1;
		if (newSize > reserved)
		{
			_reserve(reserved * 2);
		}
		size = newSize;
	}

	T& operator[](u64 index)
	{
		AssertNotEmpty();
		assert(size > 0);
		assert(index < size);
		return data[index];
	}

	T* Get(u64 index)
	{
		return &operator[](index);
	}

	T& Last()
	{
		return operator[](size - 1);
	}

	T* Back()
	{
		return &Last();
	}

	void PushReuse()
	{
		_grow();
	}

	void PushZero()
	{
		PushReuse();
		memset(Back(), 0, sizeof(T));
	}

	void PushCopy(const T* val)
	{
		PushReuse();
		memcpy(Back(), val, sizeof(T));
	}

	void PushMove(const T&& val)
	{
		PushReuse();
		Last() = std::move(val);
	}

	void Push(const T& val)
	{
		PushReuse();
		Last() = val;
	}

	void Clear()
	{
		AssertNotEmpty();
		size = 0;
	}

	Permute64 GetPermute() const
	{
		Permute64 p;
		p.Init(size, PERMUTE_SEED + std::time(0));
		return p;
	}
};

#define ForVector(vec, iter) for (int iter = 0; i < vec.size; i++)
