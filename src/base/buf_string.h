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

	WString(const wchar_t* data) : data((wchar_t*)data), len(wcslen(data)) 
	{
		assert(data != nullptr);
		assert(len > 0);
	}

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

	u64 Bytes() const
	{
		AssertNotEmpty();
		return size * sizeof(wchar_t);
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

	void Push(const wchar_t* str)
	{
		assert(str != nullptr);
		PushStringCopy({(wchar_t*)str});
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

struct String
{
	char* data = nullptr;
	u64 len = 0;

	String() = default;

	String(char* data, u64 len) : data(data), len(len) {}

	String(const char* data) : data((char*)data), len(strlen(data))
	{
		assert(data != nullptr);
		assert(len > 0);
	}

	u64 Bytes() const
	{
		return len * sizeof(char);
	}

	bool Empty() const
	{
		return len == 0 || data == nullptr;
	}

	void AssertNotEmpty() const
	{
		assert(!Empty());
	}

	bool operator==(const String& other) const
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

	operator char* () const
	{
		AssertNotEmpty();
		return data;
	}

	char& operator[](u64 index) const
	{
		AssertNotEmpty();
		assert(index < len);
		return data[index];
	}

	void Copy(const String& other) const
	{
		AssertNotEmpty();
		other.AssertNotEmpty();
		assert(other.len == len);
		memcpy(data, other.data, Bytes());
	}
};

template<>
struct std::hash<String>
{
	HASH_T operator()(const String& str) const noexcept
	{
		str.AssertNotEmpty();
		return fnv1a(str.data, str.len);
	}
};

inline u8 U64ToStringTreeTable(u64 x, char* out)
{
	static const char table[200] = {
		0x30, 0x30, 0x30, 0x31, 0x30, 0x32, 0x30, 0x33, 0x30, 0x34, 0x30, 0x35,
		0x30, 0x36, 0x30, 0x37, 0x30, 0x38, 0x30, 0x39, 0x31, 0x30, 0x31, 0x31,
		0x31, 0x32, 0x31, 0x33, 0x31, 0x34, 0x31, 0x35, 0x31, 0x36, 0x31, 0x37,
		0x31, 0x38, 0x31, 0x39, 0x32, 0x30, 0x32, 0x31, 0x32, 0x32, 0x32, 0x33,
		0x32, 0x34, 0x32, 0x35, 0x32, 0x36, 0x32, 0x37, 0x32, 0x38, 0x32, 0x39,
		0x33, 0x30, 0x33, 0x31, 0x33, 0x32, 0x33, 0x33, 0x33, 0x34, 0x33, 0x35,
		0x33, 0x36, 0x33, 0x37, 0x33, 0x38, 0x33, 0x39, 0x34, 0x30, 0x34, 0x31,
		0x34, 0x32, 0x34, 0x33, 0x34, 0x34, 0x34, 0x35, 0x34, 0x36, 0x34, 0x37,
		0x34, 0x38, 0x34, 0x39, 0x35, 0x30, 0x35, 0x31, 0x35, 0x32, 0x35, 0x33,
		0x35, 0x34, 0x35, 0x35, 0x35, 0x36, 0x35, 0x37, 0x35, 0x38, 0x35, 0x39,
		0x36, 0x30, 0x36, 0x31, 0x36, 0x32, 0x36, 0x33, 0x36, 0x34, 0x36, 0x35,
		0x36, 0x36, 0x36, 0x37, 0x36, 0x38, 0x36, 0x39, 0x37, 0x30, 0x37, 0x31,
		0x37, 0x32, 0x37, 0x33, 0x37, 0x34, 0x37, 0x35, 0x37, 0x36, 0x37, 0x37,
		0x37, 0x38, 0x37, 0x39, 0x38, 0x30, 0x38, 0x31, 0x38, 0x32, 0x38, 0x33,
		0x38, 0x34, 0x38, 0x35, 0x38, 0x36, 0x38, 0x37, 0x38, 0x38, 0x38, 0x39,
		0x39, 0x30, 0x39, 0x31, 0x39, 0x32, 0x39, 0x33, 0x39, 0x34, 0x39, 0x35,
		0x39, 0x36, 0x39, 0x37, 0x39, 0x38, 0x39, 0x39,
	};
	u64 top = x / 100000000;
	u64 bottom = x % 100000000;
	u64 toptop = top / 10000;
	u64 topbottom = top % 10000;
	u64 bottomtop = bottom / 10000;
	u64 bottombottom = bottom % 10000;
	u64 toptoptop = toptop / 100;
	u64 toptopbottom = toptop % 100;
	u64 topbottomtop = topbottom / 100;
	u64 topbottombottom = topbottom % 100;
	u64 bottomtoptop = bottomtop / 100;
	u64 bottomtopbottom = bottomtop % 100;
	u64 bottombottomtop = bottombottom / 100;
	u64 bottombottombottom = bottombottom % 100;
	//
	memcpy(out, &table[2 * toptoptop], 2);
	memcpy(out + 2, &table[2 * toptopbottom], 2);
	memcpy(out + 4, &table[2 * topbottomtop], 2);
	memcpy(out + 6, &table[2 * topbottombottom], 2);
	memcpy(out + 8, &table[2 * bottomtoptop], 2);
	memcpy(out + 10, &table[2 * bottomtopbottom], 2);
	memcpy(out + 12, &table[2 * bottombottomtop], 2);
	memcpy(out + 14, &table[2 * bottombottombottom], 2);

	u8 len = 16;
	u8 pos = 0;
	while (pos < 15 && out[pos] == '0') ++pos;
	memmove(out, out + pos, len - pos);
	return len - pos;
}

struct StringBuffer
{
	char* data = nullptr;
	u64 reserved = 0;
	u64 size = 0;

	void Init(u64 toReserve)
	{
		assert(toReserve > 0);
		data = (char*)malloc(toReserve * sizeof(char));
		reserved = toReserve;
	}

	explicit StringBuffer(u64 toReserve)
	{
		Init(toReserve);
	}

	StringBuffer(){}

	~StringBuffer()
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

	operator char* () const
	{
		AssertNotEmpty();
		return data;
	}

	u64 Bytes() const
	{
		AssertNotEmpty();
		return size * sizeof(char);
	}

	char& Last() const
	{
		AssertNotEmpty();
		assert(size < reserved);
		return data[size];
	}

	char* Back() const
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

	String PushUninitString(u64 len = 0) const
	{
		return { Back(), len };
	}

	String PushString(u64 len)
	{
		assert(len > 0);
		String ret = PushUninitString(len);
		Grow(len);
		return ret;
	}

	String PushStringCopy(const String& str)
	{
		str.AssertNotEmpty();
		String ret = PushString(str.len);
		ret.Copy(str);
		return ret;
	}

	String PushLoweredStringCopy(const String& str)
	{
		String lowered = PushStringCopy(str);
		Terminate();
		for (int i = 0; i < str.len; i++)
		{
			lowered[i] = tolower(lowered[i]);
		}
		return lowered;
	}

	void Push(const String& str)
	{
		PushStringCopy(str);
	}

	void Push(const char c)
	{
		Last() = c;
		Grow();
	}

	template <typename T>
	typename std::enable_if<std::is_integral<T>::value && !std::is_same<T, char>::value>::type
		Push(T i) {
		using U = typename std::make_unsigned<T>::type;
		AssertNotEmpty();

		if (std::is_signed<T>::value && i < 0) {
			Push('-');
			u8 written = U64ToStringTreeTable(static_cast<u64>(-i), Back());
			Grow(written);
		}
		else {
			u8 written = U64ToStringTreeTable(static_cast<u64>(i), Back());
			Grow(written);
		}
	}

	void Push(const char* str)
	{
		assert(str != nullptr);
		PushStringCopy({ (char*)str });
	}

	static void PushF() {} // base case

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

	void Pop(const String& str)
	{
		str.AssertNotEmpty();
		Pop(str.len);
	}

	void PopTerminated(const String& str)
	{
		str.AssertNotEmpty();
		Pop(str.len + 1);
	}

	void Clear()
	{
		size = 0;
	}
};

raddbg_type_view(String, array(data, len));
raddbg_type_view(StringBuffer, array(data, size));
raddbg_type_view(WString, array(data, len));
raddbg_type_view(WStringBuffer, array(data, size));
