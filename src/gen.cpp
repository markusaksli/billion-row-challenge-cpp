#include <codecvt>
#include <fstream>
#include <locale>
#include <sstream>

#include "base/buf_string.h"
#include "base/hash_map.h"
#include "base/simd.h"
#include "base/type_macros.h"
#include "base/Xoroshiro128Plus.h"

struct Utf8FileWriter
{
	std::ofstream fs;
	std::wstring_convert<std::codecvt_utf8<wchar_t>> conv; // TODO: This is really slow, should be a way to generate in utf-8 in the first place

	Utf8FileWriter(const char* filename, bool withBom = true)
	{
		fs.open(filename, std::ios::binary | std::ios::out);
		if (withBom && fs) {
			const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
			fs.write(reinterpret_cast<const char*>(bom), 3);
		}
	}

	void Write(const wchar_t* data, size_t len)
	{
		if (!fs) return;
		std::string utf8 = conv.to_bytes(data, data + len);
		fs.write(utf8.data(), utf8.size());
	}
};

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

	HashMap<WString, WString> stations(42000);
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

	Utf8FileWriter writer("data/only_stations.txt");
	writer.Write(writeBuf.data, writeBuf.size);
}

// ---------------------------------------------------------------------------------------------------------------------
// Actual data generation ----------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------

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

float GetRandomTemp(Xoroshiro128Plus::Random &rnd)
{
	double u = rnd.NextDouble();
	return static_cast<float>(u * 199.8 - 99.9);
}

void GenerateDataJob(GenerateDataJobInfo* info, Vector<WString>* stations)
{
	Xoroshiro128Plus::Random rnd; // Faster random since we don't need perfect distributions

	while (true)
	{
		if (info->kill) return;

		if (!info->processing)
		{
			std::this_thread::sleep_for(std::chrono::microseconds(100));
			continue;
		}

		while (true)
		{
			if (info->lines >= info->maxLines) goto endGen;
			if (info->writeBuf.Remaining() < 100) goto endGen; // A line is probably never longer than this
			GenerateLine(info->writeBuf, (*stations)[rnd.Next() % 100], GetRandomTemp(rnd));
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
	if (data.empty())
	{
		ScrapeStations();
	}

	// Guranteed all unique at this point so just fill a vector

	data = ReadFile("data/only_stations.txt");

	//TODO: Arena arrays not vectors
	Vector<WString> allStations(42000);
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

	// Get a random selection and create a compact representation

	u32 numStationsToUse = 100;
	Vector<WString> stations(numStationsToUse);

	Permute64 p = allStations.GetPermute();
	for (u64 i = 0; i < numStationsToUse; i++)
	{
		stations.Push(allStations[p.Permute(i)]);
		if (i > 0)
		{
			assert(wcscmp(stations[i], stations[i - 1]) != 0);
		}
	}

	allStations.~Vector();

	// Give each thread some memory to fill and iterate until we have some small number of lines left to fill

	constexpr u64 totalLines = 1 * NUM_BN; // CBA counting the zeros myself
	constexpr u64 totalMemory = 16 * GB;

	const u32 numWorkers = std::thread::hardware_concurrency() - 2; // Leave physical core(s) for kernel and I/O to be nice
	// const u32 numWorkers = 1;
	const u64 workerMemory = totalMemory / numWorkers;
	const u64 workerWChars = workerMemory / sizeof(wchar_t);

	Vector<GenerateDataJobInfo> jobs;
	jobs.InitZero(numWorkers);
	jobs.size = jobs.reserved;
	Vector<std::thread*> threads(numWorkers);

	WStringBuffer writeBuf(totalMemory / sizeof(wchar_t));

	for (u64 i = 0; i < numWorkers; i++)
	{
		jobs[i].writeBuf.data = &writeBuf.data[i * workerWChars];
		jobs[i].writeBuf.reserved = workerWChars;
		threads.Push(new std::thread(GenerateDataJob, &jobs[i], &stations));
	}

	Utf8FileWriter writer("data/1brc.data");

	u64 linesRemaining = totalLines;
	while(linesRemaining > 10000) // We can leave the remainder for the main thread
	{
		ForVector(jobs, i)
		{
			jobs[i].lines = 0;
			jobs[i].writeBuf.Clear();
			jobs[i].maxLines = linesRemaining / numWorkers;
			jobs[i].processing = true;
		}

		while (true)
		{
			bool wait = false;
			system("cls");
			printf("%.1f%% generated, threads:", 100.0 - (double)linesRemaining / totalLines * 100);
			ForVector(jobs, i)
			{
				if (jobs[i].processing)
				{
					wait = true;
				}
				printf("\n[%d:\t%.1f%%]", i, (double)jobs[i].writeBuf.size / (jobs[i].writeBuf.reserved - 100) * 100);
			}
			if (!wait)
			{
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}

		u64 totalToWrite = 0;
		ForVector(jobs, i)
		{
			totalToWrite += jobs[i].writeBuf.Bytes();
		}
		system("cls");
		printf("%.1f%% generated, writing %.2f GB:", 100.0 - ((double)(linesRemaining) / totalLines) * 100, (double)(totalToWrite) / GB);

		ForVector(jobs, i)
		{
			writer.Write(jobs[i].writeBuf.data, jobs[i].writeBuf.size);
			linesRemaining -= jobs[i].lines;
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

	Xoroshiro128Plus::Random rnd;

	for (u64 i = linesRemaining; i > 0; i--)
	{
		GenerateLine(writeBuf, stations[rnd.Next() % 100], GetRandomTemp(rnd));
	}

	writer.Write(writeBuf.data, writeBuf.size);
	printf("\r100.0%% generated");
}

int main()
{
	GenerateData();
	return 0;
}
