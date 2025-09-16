#include <codecvt>
#include <fstream>
#include <random>
#include <windows.h>
#include <vector>

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

int main()
{
	std::ofstream file;
	file.open("data/1brc.data", std::ios::out | std::ios::binary);

	std::random_device dev;
	std::mt19937 rng(dev());
	std::uniform_int_distribution<std::mt19937::result_type> dist6(1, 6); // distribution in range [1, 6]

	char lineBuffer[500];
	const wchar_t* str = L"test;1.000\n";
	to_utf8(str, lineBuffer, wcslen(str));

	for (int i = 0; i < 10000; i++)
	{
		file << lineBuffer;
	}

	return 0;
}
