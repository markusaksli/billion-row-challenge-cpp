#pragma once
#include <cassert>
#include <cstdlib>
#include <cstring>

#include "raddbg_markup.h"
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

	void Init(u64 toReserve)
	{
		assert(toReserve > 0);
		data = (T*)malloc(Bytes((toReserve)));
		reserved = toReserve;
	}

	void InitZero(u64 toReserve)
	{
		Init(toReserve);
		memset(data, 0, Bytes());
	}

	Vector() = default;

	explicit Vector(const u64 toReserve)
	{
		Init(toReserve);
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

		data = (T*)realloc(data, Bytes(toReserve));
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

raddbg_type_view(Vector< ? >, array(data, size));
