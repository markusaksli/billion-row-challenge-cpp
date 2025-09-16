#pragma once
#include <cassert>
#include <cstdlib>
#include <cstring>

#include "type_macros.h"

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

	void _init(u64 toReserve)
	{
		data = malloc(Bytes((toReserve)));
		reserved = toReserve;
	}

	explicit Vector(const u64 toReserve)
	{
		_init(toReserve);
	}

	~Vector()
	{
		if (data != nullptr)
		{
			free(data);
		}
	}

	void _reserve(const u64 toReserve)
	{
		assert(reserved > 0);
		assert(toReserve > reserved);

		data = realloc(data, Bytes(toReserve));
		reserved = toReserve;
	}

	void _grow()
	{
		assert(reserved > 0);

		u64 newSize = size + 1;
		if (newSize > reserved)
		{
			_reserve(reserved * 2);
		}
		size = newSize;
	}

	T& operator[](u64 index)
	{
		assert(reserved > 0);
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

	void Push(const T& val)
	{
		PushReuse();
		Last() = val;
	}

	void Clear()
	{
		assert(reserved > 0);
		size = 0;
	}
};
