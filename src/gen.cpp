#include <codecvt>
#include <fstream>
#include <locale>
#include <sstream>

#include "base/buf_string.h"
#include "base/hash_map.h"
#include "base/platform_io.h"
#include "base/simd.h"
#include "base/type_macros.h"
#include "base/Xoroshiro128Plus.h"

std::ofstream OpenUTF8FileWrite(const char* filename)
{
	std::ofstream fs(filename, std::ios::binary | std::ios::out);
	if (fs.good()) {
		const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
		fs.write(reinterpret_cast<const char*>(bom), 3);
	}
	return fs;
}

u64 pos = 0;
SIMD_Int simdChunk;
String data;
StringBuffer strbuf(4 * MB);

String ReadFile(const char* filename)
{
	strbuf.Clear();
	String str = strbuf.PushUninitString();
	std::ifstream fs(filename);
	if (!fs.good())
	{
		return str;
	}

	fs.seekg(0, std::ios::end);
	size_t size = fs.tellg();
	assert(size < strbuf.reserved);

	fs.seekg(0);
	fs.read(strbuf.data, size);
	strbuf.size = size;
	if(strbuf.data[0] == (char)0xEF && strbuf.data[1] == (char)0xBB && str.data[2] == (char)0xBF)
	{
		strbuf.data = &strbuf[3];
		strbuf.size -= 3;
		str.data = strbuf.data;
	}

	for (int i = 0; i < SIMD_U8Size; i++)
	{
		strbuf.Push('\0');
	}

	str.len = strbuf.size;
	return str;
}

void Read()
{
	simdChunk = SIMD_LoadUnAligned(reinterpret_cast<SIMD_Int const*>(&data[pos]));
}

void SeekToChar(const char c)
{
	while (true)
	{
		Read();
		SIMD_U8Mask mask = SIMD_CmpEqChar(simdChunk, c);
		if (mask == 0)
		{
			pos += SIMD_U8Size;
			continue;
		}

		u32 scanResult = 0;
		_BitScanForward(&scanResult, mask);
		pos += scanResult;
		break;
	}
}

void SeekToNextLine()
{
	SeekToChar('\n');
	pos++;
}

void ScrapeStations()
{
	data = ReadFile("data/weather_stations.csv");

	HashMap<String, String> stations(42000);
	String lowered;

	while (true)
	{
		if (pos >= data.len || data[pos] == '\0') break;
		if (data[pos] == '#')
		{
			SeekToNextLine();
			continue;
		}
		const u64 stationNameStart = pos;
		String stationName;
		stationName.data = &data[pos];

		SeekToChar(';');

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

	StringBuffer writeBuf(4 * MB);

	assert(stations.items.size == 41343);
	for (int i = 0; i < stations.items.size; i++)
	{
		if (i > 0)
		{
			assert(stations.items[i].k != stations.items[i - 1].k);
			assert(stations.items[i].v != stations.items[i - 1].v);
		}

		writeBuf.PushF(stations.items[i].v, '\n');
	}

	std::ofstream fs = OpenUTF8FileWrite("data/only_stations.txt");
	writeBuf.WriteToFilestream(fs);
	fs.close();
}

// ---------------------------------------------------------------------------------------------------------------------
// Actual data generation ----------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------

struct GenerateDataJobInfo
{
	std::atomic_bool processing, kill{false};
	StringBuffer writeBuf;
	u64 lines = 0;
	u64 maxLines = 0;
};

// Faster than float conversions
void Push1DecimalFloat(StringBuffer& writeBuf, const float f)
{
	s32 scaled = static_cast<s32>(round(f * 10.0f));
	s32 intPart = scaled / 10;
	s32 decimal = std::abs(scaled % 10);

	writeBuf.Push(intPart);
	writeBuf.Push('.');
	writeBuf.Push(static_cast<char>('0' + decimal));
}

void GenerateLine(Xoroshiro128Plus::Random& rnd, StringBuffer& writeBuf, Vector<String>* stations)
{
	writeBuf.PushF((*stations)[rnd.Next() % 100], ';');
	Push1DecimalFloat(writeBuf, static_cast<float>(rnd.NextDouble() * 199.8 - 99.9));
	writeBuf.Push('\n');
}

void GenerateDataJob(GenerateDataJobInfo* info, Vector<String>* stations)
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
			GenerateLine(rnd, info->writeBuf, stations);
			info->lines++;
		}
		
		endGen:
		info->processing = false;
	}
}

void GenerateData(double bufferSize)
{
	// Not sure why the original challenge didn't do this first
	// I'm leaving the original input file in here to just use the same input for spiritual reasons

	data = ReadFile("data/only_stations.txt");
	if (data.Empty())
	{
		ScrapeStations();
	}

	// Guranteed all unique at this point so just fill a vector

	data = ReadFile("data/only_stations.txt");

	//TODO: Arena arrays not vectors
	Vector<String> allStations(42000);
	while (true)
	{
		if (pos >= data.len || data[pos] == '\0') break;
		const u64 stationNameStart = pos;
		String stationName;
		stationName.data = &data[pos];

		SeekToChar('\n');

		stationName.len = pos - stationNameStart;
		allStations.Push(stationName);
		pos++;
	}
	assert(allStations.size == 41343);
	assert(allStations[0] == String("Tokyo"));

	// Get a random selection and create a compact representation

	u32 numStationsToUse = 100;
	Vector<String> stations(numStationsToUse);

	Permute64 p = allStations.GetPermute();
	for (u64 i = 0; i < numStationsToUse; i++)
	{
		stations.Push(allStations[p.Permute(i)]);
		if (i > 0)
		{
			assert(stations[i] != stations[i - 1]);
		}
	}

	allStations.~Vector();

	// Give each thread some memory to fill and iterate until we have some small number of lines left to fill

	constexpr u64 totalLines = NUM_BN; // CBA counting the zeros myself
	const u64 totalMemory = static_cast<u64>(round(bufferSize * GB));

	const u32 numWorkers = std::thread::hardware_concurrency() - 2; // Leave physical core(s) for kernel and I/O to be nice
	const u64 workerMemory = totalMemory / numWorkers;
	const u64 workerChars = workerMemory / sizeof(char);

	Vector<GenerateDataJobInfo> jobs;
	jobs.InitZero(numWorkers);
	jobs.size = jobs.reserved;
	Vector<std::thread*> threads(numWorkers);

	StringBuffer writeBuf(totalMemory / sizeof(char));

	for (u64 i = 0; i < numWorkers; i++)
	{
		jobs[i].writeBuf.data = &writeBuf.data[i * workerChars];
		jobs[i].writeBuf.reserved = workerChars;
		threads.Push(new std::thread(GenerateDataJob, &jobs[i], &stations));
	}

	FileHandle fh;
#ifndef _WIN32
	fh.fd = ::open("data/1brc.data", O_WRONLY | O_CREAT | O_TRUNC, 0644);
#else
	fh.handle = ::CreateFileA("data/1brc.data",
		GENERIC_WRITE,
		0,
		NULL,
		CREATE_ALWAYS,
		FILE_FLAG_NO_BUFFERING,
		NULL);
#endif

	const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
	WriteSingle(fh, bom, 3);

	Array<WriteData> writeSegments;
	writeSegments.InitMalloc(numWorkers);

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
			writeSegments[i].data = jobs[i].writeBuf.data;
			writeSegments[i].bytes = jobs[i].writeBuf.Bytes();
			linesRemaining -= jobs[i].lines;
		}
		system("cls");
		printf("%.1f%% generated, writing %.2f GB:", 100.0 - ((double)(linesRemaining) / totalLines) * 100, (double)(totalToWrite) / GB);

		WriteAll(fh, writeSegments);
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
		GenerateLine(rnd, writeBuf, &stations);
	}

	AppendSingle(fh, writeBuf.data, writeBuf.Bytes());
	fh.Close();
	printf("\r100.0%% generated");
}

int main(int argc, char* argv[])
{
	double bufferSize;
	if (argc <= 1)
	{
		bufferSize = 4.0;
	}
	else
	{
		if (_stricmp(argv[1], "-buffersize") != 0)
		{
			printf("unknown parameter %s", argv[1]);
			return 1;
		}
		if (argc < 3)
		{
			printf("missing buffer size value");
			return 1;
		}
		bufferSize = strtod(argv[2], nullptr);
	}
	GenerateData(bufferSize);
	return 0;
}
