#include <codecvt>
#include <fstream>
#include <random>
#include <sstream>
#include <windows.h>

#include "base/buf_string.h"
#include "base/hash_map.h"
#include "base/simd.h"
#include "base/type_macros.h"

raddbg_type_view(WString, array(data, len));
raddbg_type_view(Vector<?>, array(data, size));

void to_utf8(const wchar_t* buffer, char* outBuffer, int len)
{
	int nChars = WideCharToMultiByte(
		CP_UTF8,
		0,
		buffer,
		len,
		NULL,
		0,
		NULL,
		NULL);
	if (nChars == 0) return;

	WideCharToMultiByte(
		CP_UTF8,
		0,
		buffer,
		len,
		outBuffer,
		nChars,
		NULL,
		NULL);
}

u64 pos = 0;
SIMD_Int simdChunk;
std::wstring data;
WStringBuffer strbuf(4 * MB);

void Read()
{
	simdChunk = SIMD_LoadUnAlignedU16(reinterpret_cast<SIMD_Int const*>(&data[pos]));
}

void SeekToChar(const wchar_t c)
{
	while (true)
	{
		Read();
		SIMD_U16Mask mask = SIMD_CmpEqWideChar(simdChunk, c);
		if (mask == 0)
		{
			pos += SIMD_U16Size;
			continue;
		}

		u32 bitScanMask = mask;
		u32 scanResult = 0;
		_BitScanForward(&scanResult, bitScanMask);
		pos += scanResult;
		break;
	}
}

void SeekToNextLine()
{
	SeekToChar(L'\n');
	pos++;
}

std::wstring ReadFile(const char* filename)
{
	std::wifstream wif(filename);
	wif.imbue(std::locale(std::locale(), new std::codecvt_utf8<wchar_t>));
	std::wstringstream wss;
	wss << wif.rdbuf();
	return wss.str();
}

WString LowerWStr(const WString& str)
{
	WString lowered = strbuf.PushStringCopy(str);
	for (int i = 0; i < lowered.len; i++)
	{
		lowered
	}
}

int main()
{
	data = ReadFile("data/weather_stations.csv");

	HashSet<WString> stations(10000);
	WString stationName;

	while (true)
	{
		if (pos >= data.length()) break;
		if (data[pos] == L'#')
		{
			SeekToNextLine();
			continue;
		}
		u64 stationNameStart = pos;
		stationName.data = &data[pos];
		SeekToChar(L';');
		stationName.len = pos - stationNameStart;

		stations.Insert(stationName);
		SeekToNextLine();
	}

	return 0;
}
