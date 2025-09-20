#pragma once
#include <cassert>
#include <cstdlib>
#include <cstring>

#include "hash_map.h"
#include "type_macros.h"

inline const HASH_T fnv1a(const void* key, const HASH_T len)
{
	const char* data = (char*)key;
	HASH_T hash = 0xcbf29ce484222325;
	HASH_T prime = 0x100000001b3;

	for (int i = 0; i < len; ++i) {
		uint8_t value = data[i];
		hash = hash ^ value;
		hash *= prime;
	}

	return hash;
}

struct WString
{
	wchar_t* data = nullptr;
	u64 len = 0;

	WString() = default;

	WString(wchar_t* data, u64 len) : data(data), len(len) {}

	u64 Bytes() const
	{
		return len * sizeof(wchar_t);
	}

	void AssertNotEmpty() const
	{
		assert(data != nullptr);
		assert(len > 0);
	}

	bool operator==(const WString &other) const
	{
		AssertNotEmpty();
		other.AssertNotEmpty();
		if (len != other.len
			|| data[0] != other.data[0]
			|| data[len - 1] != other.data[len - 1])
		{
			return false;
		}

		return memcmp(data, other.data, Bytes()) == 0;
	}

	operator wchar_t*() const
	{
		AssertNotEmpty();
		return data;
	}

	wchar_t& operator[](u64 index)
	{
		assert(index > 0 && index < len);
		AssertNotEmpty();
		return data[index];
	}

	void Copy(const WString &other)
	{
		AssertNotEmpty();
		other.AssertNotEmpty();
		assert(other.len == len);
		memcpy(data, other.data, Bytes());
	}
};

template<>
struct std::hash<WString>
{
	HASH_T operator()(const WString& str) const noexcept
	{
		str.AssertNotEmpty();
		return fnv1a(str.data, str.len);
	}
};

struct WStringBuffer
{
	wchar_t* data = nullptr;
	u64 reserved = 0;
	u64 size = 0;

	void Init(u64 toReserve)
	{
		assert(toReserve > 0);
		data = (wchar_t*)malloc(toReserve * sizeof(wchar_t));
		reserved = toReserve;
	}

	explicit WStringBuffer(u64 toReserve)
	{
		Init(toReserve);
	}

	~WStringBuffer()
	{
		AssertNotEmpty();
		if (data != nullptr)
		{
			data = nullptr;
			size = 0;
			reserved = 0;
		}
	}

	void AssertNotEmpty() const
	{
		assert(reserved > 0);
		assert(data != nullptr);
	}

	operator wchar_t* () const
	{
		AssertNotEmpty();
		return data;
	}

	wchar_t& Last() const
	{
		AssertNotEmpty();
		assert(size < reserved);
		return data[size];
	}

	wchar_t* Back() const
	{
		return &Last();
	}

	u64 Remaining() const
	{
		AssertNotEmpty();
		return reserved - size;
	}

	void Grow(u64 toAdd = 1)
	{
		AssertNotEmpty();
		assert(toAdd > 0);
		assert(size + toAdd < reserved);
		size += toAdd;
	}

	void Terminate()
	{
		Last() = L'\0';
		Grow();
	}

	WString PushUninitWString(u64 len) const
	{
		assert(len > 0);
		return {Back(), len};
	}

	WString PushString(u64 len)
	{
		assert(len > 0);
		WString ret = PushUninitWString(len);
		Grow(len);
		return ret;
	}

	WString PushStringCopy(const WString& str)
	{
		str.AssertNotEmpty();
		WString ret = PushString(str.len);
		ret.Copy(str);
		return ret;
	}

	WString PushLoweredStringCopy(const WString& str)
	{
		WString lowered = PushStringCopy(str);
		Terminate();
		_wcslwr_s(lowered.data, lowered.Bytes());
		return lowered;
	}

	void Push(const WString& str)
	{
		PushStringCopy(str);
	}

	void Push(const wchar_t c)
	{
		Last() = c;
		Grow();
	}

	void Push(float f, int precision = 1)
	{
		// forward to double overload to avoid duplicate code
		Push(static_cast<double>(f), precision);
	}

	void Push(double f, int precision = 1)
	{
		AssertNotEmpty();

		int written = swprintf(Back(), Remaining(), L"%.*f", precision, f);
		if (written <= 0) {
			return;
		}

		Grow(static_cast<u64>(written));
	}

	void PushF() {} // base case

	template<class First, class... Rest>
	void PushF(First const& first, Rest const&... rest)
	{
		Push(first);
		PushF(rest...);
	}

	void Pop(u64 toPop = 1)
	{
		AssertNotEmpty();
		assert(toPop > 0);
		assert(toPop <= size);
		size -= toPop;
	}

	void Pop(const WString& str)
	{
		str.AssertNotEmpty();
		Pop(str.len);
	}

	void PopTerminated(const WString& str)
	{
		str.AssertNotEmpty();
		Pop(str.len + 1);
	}

	void Clear()
	{
		size = 0;
	}
};
