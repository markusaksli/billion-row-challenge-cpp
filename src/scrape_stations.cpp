#include <codecvt>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <windows.h>

#include "base/buf_string.h"
#include "base/hash_map.h"
#include "base/simd.h"
#include "base/type_macros.h"

raddbg_type_view(WString, array(data, len));
raddbg_type_view(WStringBuffer, array(data, size));
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
	if (!wif.good())
	{
		return std::wstring();
	}

	wif.imbue(std::locale(std::locale(), new std::codecvt_utf8<wchar_t>));
	std::wstringstream wss;
	wss << wif.rdbuf();
	for (int i = 0; i < SIMD_U16Size; i++)
	{
		wss << L'\0';
	}
	pos = 0;
	strbuf.Clear();
	return wss.str();
}

void ScrapeStations()
{
	data = ReadFile("data/weather_stations.csv");

	HashMap<WString, WString> stations(10000);
	WString lowered;

	while (true)
	{
		if (pos >= data.length() || data[pos] == '\0') break;
		if (data[pos] == L'#')
		{
			SeekToNextLine();
			continue;
		}
		const u64 stationNameStart = pos;
		WString stationName;
		stationName.data = &data[pos];

		SeekToChar(L';');

		stationName.len = pos - stationNameStart;
		if (stationName.Bytes() > 0 && stationName.Bytes() <= 100) // The challenge specified only up to 100 byte names
		{
			lowered = strbuf.PushLoweredStringCopy(stationName);
			if (stations.Insert(lowered, stationName) != nullptr)
			{
				strbuf.PopTerminated(lowered);
			}
		}
		SeekToNextLine();
	}

	WStringBuffer writeBuf(4 * MB);

	for (int i = 0; i < stations.items.size; i++)
	{
		if (i > 0)
		{
			assert(wcscmp(stations.items[i].k, stations.items[i - 1].k) != 0);
			assert(wcscmp(stations.items[i].v, stations.items[i - 1].v) != 0);
		}

		writeBuf.PushF(stations.items[i].v, L'\n');
	}

	writeBuf.Push(L'\0');

	std::wofstream wof("data/only_stations.txt");
	wof.imbue(std::locale(std::locale(), new std::codecvt_utf8<wchar_t>));
	wof.write(writeBuf.data, writeBuf.size);
}

WStringBuffer* workerWriteBuffers;

struct GenerateDataJobInfo
{
	std::atomic_bool processing, kill{false};
	WStringBuffer writeBuf;
	u64 lines = 0;
	u64 maxLines = 0;
};

void GenerateLine(WStringBuffer& writeBuf, const WString& station, const double temp)
{
	writeBuf.PushF(station, L';', temp, L'\n');
}

void GenerateDataJob(u64 memory, GenerateDataJobInfo* info, Vector<WString>* stations)
{
	//TODO: Arenas
	info->writeBuf.Init(memory);

	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_real_distribution<float> tempDist(-99.9, 99.9); // Don't care about double for generation, just need one decimal
	std::uniform_int_distribution<u32> stationDist(0, stations->size - 1);

	while (true)
	{
		if (info->kill) return;

		if (!info->processing) continue;

		while (true)
		{
			if (info->lines >= info->maxLines) goto endGen;
			if (info->writeBuf.Remaining() < 200) goto endGen; // A line is probably never longer than this
			GenerateLine(info->writeBuf, (*stations)[stationDist(gen)], tempDist(gen));
			info->lines++;
		}
		
		endGen:
		info->processing = false;
	}
}

void GenerateData()
{
	// Not sure why the original challenge didn't do this first
	// I'm leaving the original input file in here to just use the same input for spiritual reasons

	data = ReadFile("data/only_stations.txt");
	if (data.length() < 1)
	{
		ScrapeStations();
	}

	// Guranteed all unique at this point so just fill a vector

	data = ReadFile("data/only_stations.txt");
	Vector<WString> allStations(10000);
	while (true)
	{
		if (pos >= data.length() || data[pos] == '\0') break;
		const u64 stationNameStart = pos;
		WString stationName;
		stationName.data = &data[pos];

		SeekToChar(L'\n');

		stationName.len = pos - stationNameStart - 1;
		allStations.Push(stationName);
		pos++;
	}

	// Get a random selection and create a sparse representation

	u32 numStationsToUse = 100;
	Vector<WString> stations(numStationsToUse);

	Permute64 p = allStations.GetPermute();
	for (int i = 0; i < numStationsToUse; i++)
	{
		stations.Push(allStations[p.Permute(i)]);
		if (i > 0)
		{
			assert(wcscmp(stations[i], stations[i - 1]) != 0);
		}
	}

	allStations.~Vector();

	// Give each thread some memory to fill and iterate until we have some small number of lines left to fill

	u64 totalLines = 1 * NUM_M; // CBA counting the zeros myself
	constexpr u64 totalMemory = 1 * MB;

	const u32 numWorkers = std::thread::hardware_concurrency() - 2; // Leave physical core(s) for kernel and I/O to be nice
	const u64 workerMemory = totalMemory / numWorkers;

	//TODO: Arena arrays not vectors
	Vector<GenerateDataJobInfo> jobs;
	jobs.InitZero(numWorkers);
	jobs.size = jobs.reserved;
	Vector<std::thread*> threads(numWorkers);
	for (int i = 0; i < numWorkers; i++)
	{
		threads.Push(new std::thread(GenerateDataJob, workerMemory, &jobs[i], &stations));
	}

	std::wofstream wof("data/1brc.data");
	wof.imbue(std::locale(std::locale(), new std::codecvt_utf8<wchar_t>));

	while(totalLines > 10 * 1000) // We can leave the remainder for the main thread
	{
		ForVector(jobs, i)
		{
			jobs[i].lines = 0;
			jobs[i].writeBuf.Clear();
			jobs[i].maxLines = totalLines / numWorkers;
			jobs[i].processing = true;
		}

		while (true)
		{
			wait:
			ForVector(jobs, i)
			{
				if (jobs[i].processing)
				{
					goto wait;
				}
			}
			goto merge;
		}

		merge:
		ForVector(jobs, i)
		{
			wof.write(jobs[i].writeBuf.data, jobs[i].writeBuf.size);
			totalLines -= jobs[i].lines;
		}
	}

	ForVector(jobs, i)
	{
		jobs[i].kill = true;
	}
	ForVector(jobs, i)
	{
		threads[i]->join();
	}

	// Fill the remainder on the main thread and we're done

	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_real_distribution<float> tempDist(-99.9, 99.9); // Don't care about double for generation, just need one decimal
	std::uniform_int_distribution<u32> stationDist(0, stations.size - 1);

	WStringBuffer writeBuf(1 * MB);

	for (int i = totalLines; i > 0; i--)
	{
		GenerateLine(writeBuf, stations[stationDist(gen)], tempDist(gen));
	}

	wof.write(writeBuf.data, writeBuf.size);
}

int main()
{
	GenerateData();
	return 0;
}
