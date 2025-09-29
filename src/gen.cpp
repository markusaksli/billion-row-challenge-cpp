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

u64 pos = 0;
SIMD_Int simdChunk;
String data;
StringBuffer strbuf(4 * MB);

String ReadFile(const char* filename)
{
	strbuf.Clear();
	pos = 0;
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

struct StationData
{
	String name;
	double min, max;
};

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

void GenerateLine(Xoroshiro128Plus::Random& rnd, StringBuffer& writeBuf, Vector<StationData>* stations)
{
	const StationData& stationData = (*stations)[rnd.Next() % stations->size];
	writeBuf.PushF(stationData.name, ';');
	Push1DecimalFloat(writeBuf, static_cast<float>(rnd.NextDouble(stationData.min, stationData.max)));
	writeBuf.Push('\n');
}

void GenerateDataJob(GenerateDataJobInfo* info, Vector<StationData>* stations)
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

int main(int argc, char* argv[])
{
	u64 totalLines = NUM_BN;
	u32 numStationsToUse = 100;
	double bufferSize = 4.0;
	String inputDir = "../data/";
	String outputPath = "../data/1brc.txt";

	if (argc > 1)
	{
		for (int i = 1; i < argc; i++)
		{
			if (_stricmp(argv[i], "-help") == 0 || _stricmp(argv[i], "-h") == 0)
			{
				printf("-inputdir [dir (default ../data/)]\t\tPath to a directory that contains weather_stations.csv\n");
				printf("-output [file (default ../data/1brc.txt)]\tPath to the output file\n");
				printf("-stations [int (default 100)]\t\t\tNumber of station names to use (up to 41343)\n");
				printf("-lines [int (default 1000000000)]\t\tNumber of lines to generate\n");
				printf("-buffersize [double (default 4.0)]\t\tThe size of the generation buffer in GB\n");
				return 0;
			}

			if (_stricmp(argv[i], "-buffersize") == 0)
			{
				i++;
				if (i >= argc)
				{
					printf("missing buffer size arg value");
					return 1;
				}
				bufferSize = strtod(argv[i], nullptr);
			}
			else if (_stricmp(argv[i], "-stations") == 0)
			{
				i++;
				if (i >= argc)
				{
					printf("missing stations arg value");
					return 1;
				}
				numStationsToUse = strtol(argv[i], nullptr, 10);
			}
			else if (_stricmp(argv[i], "-lines") == 0)
			{
				i++;
				if (i >= argc)
				{
					printf("missing lines arg value");
					return 1;
				}
				totalLines = strtoll(argv[i], nullptr, 10);
			}
			else if (_stricmp(argv[i], "-inputdir") == 0)
			{
				i++;
				if (i >= argc)
				{
					printf("missing inputdir arg value");
					return 1;
				}
				inputDir.data = argv[i];
				inputDir.len = strlen(argv[i]);
			}
			else if (_stricmp(argv[i], "-output") == 0)
			{
				i++;
				if (i >= argc)
				{
					printf("missing output arg value");
					return 1;
				}
				outputPath.data = argv[i];
				outputPath.len = strlen(argv[i]);
			}
			else
			{
				printf("unknown parameter %s", argv[i]);
				return 1;
			}
		}
	}

	// Not sure why the original challenge didn't scrape the unique station names first
	// I'm leaving the original input file in here to just use the same input for spiritual reasons

	String stationFilePath = strbuf.PushStringF(inputDir, "only_stations.txt");
	data = ReadFile(stationFilePath);
	if (data.Empty())
	{
		data = ReadFile(strbuf.PushStringF(inputDir, "weather_stations.csv"));
		if (data.Empty()) return 1;

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
			if (stationName.Bytes() > 0 && stationName.Bytes() <= 100) // The challenge specified only up to 100 byte names (seems like they all are in the file)
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

		FileHandle fh = OpenUTF8FileWrite(stationFilePath);
		if (!fh.Append(writeBuf))
		{
			return 1;
		}
		fh.Close();

		data = ReadFile(stationFilePath);
	}
	if (data.Empty()) return 1;

	// Guranteed all unique at this point so just fill a vector

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
	assert(allStations.Last() == String("Nordvik"));

	// Get a random selection and create a compact representation

	Xoroshiro128Plus::Random rnd;
	Vector<StationData> stations(numStationsToUse);

	Permute64 p = allStations.GetPermute();
	for (u64 i = 0; i < numStationsToUse; i++)
	{
		StationData &stationData = stations.PushReuse();
		stationData.name = allStations[p.Permute(i)];
		stationData.min = rnd.NextDouble(-99.9, 99.9);
		stationData.max = rnd.NextDouble(-99.9, 99.9);
		if (stationData.max < stationData.min)
		{
			std::swap(stationData.min, stationData.max);
		}
		if (i > 0)
		{
			assert(stations[i].name != stations[i - 1].name);
		}
	}

	allStations.~Vector();

	// Give each thread some memory to fill, write the results, iterate until we have some small number of lines left to fill

	u64 totalMemory = static_cast<u64>(round(bufferSize * GB));
	const u32 numWorkers = std::thread::hardware_concurrency() - 2; // Leave physical core(s) for kernel and I/O to be nice

	// Use page-aligned buffers just in case
	const u64 workerMemory = ceil((double)(totalMemory / numWorkers) / PAGE_SIZE) * PAGE_SIZE;
	assert(workerMemory % PAGE_SIZE == 0);
	totalMemory = workerMemory * numWorkers;

	StringBuffer writeBuf;
	writeBuf.data = (char*)_aligned_malloc(totalMemory, PAGE_SIZE);
	writeBuf.reserved = totalMemory;
	assert((u64)writeBuf.data % PAGE_SIZE == 0);

	Vector<GenerateDataJobInfo> jobs;
	jobs.InitZero(numWorkers);
	jobs.size = jobs.reserved;
	Vector<std::thread*> threads(numWorkers);

	for (u64 i = 0; i < numWorkers; i++)
	{
		jobs[i].writeBuf.data = &writeBuf.data[i * workerMemory];
		jobs[i].writeBuf.reserved = workerMemory;
		threads.Push(new std::thread(GenerateDataJob, &jobs[i], &stations));
	}

	FileHandle fh = OpenUTF8FileWrite(outputPath);
	if (!fh.Good()) return 1;

	u64 linesRemaining = totalLines;
	while (linesRemaining > 10000) // We can leave the remainder for the main thread
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
			linesRemaining -= jobs[i].lines;
		}

		system("cls");
		printf("%.1f%% generated, writing %.2f GB:", 100.0 - ((double)(linesRemaining) / totalLines) * 100, (double)(totalToWrite) / GB);

		ForVector(jobs, i)
		{
			if (!fh.Append(jobs[i].writeBuf))
			{
				return 1;
			}
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

	for (u64 i = linesRemaining; i > 0; i--)
	{
		GenerateLine(rnd, writeBuf, &stations);
	}

	if (!fh.Append(writeBuf.data, writeBuf.Bytes()))
	{
		return 1;
	}

	fh.Close();
	system("cls");
	printf("Generated %lld lines using %ld stations in %s", totalLines, numStationsToUse, (const char*)outputPath);

	return 0;
}
