#include <codecvt>
#include <fstream>
#include <iomanip>
#include <locale>

#include "base/buf_string.h"
#include "base/hash_map.h"
#include "base/platform_io.h"
#include "base/simd.h"
#include "base/type_macros.h"
#include "base/Xoroshiro128Plus.h"

char* pos = nullptr;
char* dataEnd = nullptr;
String data;
StringBuffer strbuf(4 * KB);
StringBuffer readbuf(4 * MB);

String ReadFile(const char* filename)
{
	readbuf.Clear();
	String str = readbuf.PushUninitString();
	std::ifstream fs(filename);
	if (!fs.good())
	{
		return str;
	}

	fs.seekg(0, std::ios::end);
	size_t size = fs.tellg();
	assert(size < readbuf.reserved);

	fs.seekg(0);
	fs.read(readbuf.data, size);
	readbuf.size = size;
	if(readbuf.data[0] == (char)0xEF && readbuf.data[1] == (char)0xBB && str.data[2] == (char)0xBF)
	{
		readbuf.data = &readbuf[3];
		readbuf.size -= 3;
		str.data = readbuf.data;
	}

	for (int i = 0; i < 64; i++)
	{
		readbuf.Push('\0');
	}

	str.len = readbuf.size;
	pos = str.data;
	dataEnd = &str.data[str.len];
	return str;
}

void SeekToNextLine()
{
	SIMD_SeekToChar(pos, '\n');
	pos++;
}

struct StationData
{
	String name;
	double min, max;
	double rMin, rMax, rSum;
	u32 count;

	void Merge(const StationData& other)
	{
		if (other.count == 0) return;
		assert(name == other.name);

		rMin = std::min(rMin, other.rMin);
		rMax = std::max(rMax, other.rMax);
		rSum += other.rSum;
		count += other.count;
	}
};

struct GenerateDataJobInfo
{
	std::atomic_bool processing, kill{false};
	StringBuffer writeBuf;
	u64 lines = 0;
	u64 maxLines = 0;
};

s64 ScaledRoundTowardPositive1Decimal(const double d)
{
	return static_cast<s64>(ceil(d * 10));
}

// Faster than double conversions
void Push1DecimalDouble(StringBuffer& writeBuf, const s64 scaled)
{
	s64 intPart = scaled / 10;
	s64 decimal = std::abs(scaled % 10);

	if (intPart == 0 && scaled < 0)
	{
		writeBuf.Push('-');
	}

	writeBuf.Push(intPart);
	writeBuf.Push('.');
	writeBuf.Push(static_cast<char>('0' + decimal));
}

void Push1DecimalDouble(StringBuffer& writeBuf, const double d)
{
	s64 scaled = ScaledRoundTowardPositive1Decimal(d);
	Push1DecimalDouble(writeBuf, scaled);
}

void GenerateLine(Xoroshiro128Plus::Random& rnd, StringBuffer& writeBuf, Array<StationData>* stations)
{
	StationData& stationData = (*stations)[rnd.Next() % stations->size];
	writeBuf.PushF(stationData.name, ';');

	double temp = rnd.NextDouble(stationData.min, stationData.max);
	s64 scaled = ScaledRoundTowardPositive1Decimal(temp);

	Push1DecimalDouble(writeBuf, scaled);

	writeBuf.Push('\n');

	if (std::abs(temp) < 1.0)
	{
		printf("");
	}

	temp = static_cast<double>(scaled) / 10.0;

	stationData.rMax = std::max(stationData.rMax, temp);
	stationData.rMin = std::min(stationData.rMin, temp);
	stationData.count++;
	stationData.rSum += temp;
}

void GenerateDataJob(GenerateDataJobInfo* info, Array<StationData>* stations)
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
	String validationPath = "../data/validation.txt";

	if (argc > 1)
	{
		for (int i = 1; i < argc; i++)
		{
			if (_stricmp(argv[i], "-help") == 0 || _stricmp(argv[i], "-h") == 0)
			{
				printf("-inputdir [dir (default ../data/)]\t\tPath to a directory that contains weather_stations.csv\n");
				printf("-output [file (default ../data/1brc.txt)]\tPath to the output file\n");
				printf("-validation [file (default ../data/validation.txt)]\tPath to the output validation file\n");
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
			else if (_stricmp(argv[i], "-validation") == 0)
			{
				i++;
				if (i >= argc)
				{
					printf("missing validation arg value");
					return 1;
				}
				validationPath.data = argv[i];
				validationPath.len = strlen(argv[i]);
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
			if (pos >= dataEnd || *pos == '\0') break;
			if (*pos == '#')
			{
				SeekToNextLine();
				continue;
			}
			String stationName;
			stationName.data = pos;

			SIMD_SeekToChar(pos, ';');

			stationName.len = pos - stationName.data;
			if (stationName.Bytes() > 0 && stationName.Bytes() <= 100) // The challenge specified only up to 100 byte names (seems like they all are in the file)
			{
				lowered = readbuf.PushLoweredStringCopy(stationName);
				if (stations.Insert(lowered, stationName) != nullptr)
				{
					readbuf.PopTerminated(lowered);
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
		if (!fh.Append(writeBuf)) return 1;
		fh.Close();

		data = ReadFile(stationFilePath);
	}
	if (data.Empty()) return 1;

	// Guranteed all unique at this point so just fill a vector

	//TODO: Arena arrays not vectors
	Vector<String> allStations(42000);
	while (true)
	{
		if (pos >= dataEnd || *pos == '\0') break;
		String stationName;
		stationName.data = pos;

		SIMD_SeekToChar(pos, '\n');

		stationName.len = pos - stationName.data;
		allStations.Push(stationName);
		pos++;
	}
	assert(allStations.size == 41343);
	assert(allStations[0] == String("Tokyo"));
	assert(allStations.Last() == String("Nordvik"));

	// Get a random selection and create a compact representation

	Xoroshiro128Plus::Random rnd;
	const u32 numWorkers = std::thread::hardware_concurrency() - 2; // Leave physical core(s) for kernel and I/O to be nice
	Array<Array<StationData>> stations;
	stations.InitMallocZero(numWorkers);

	for (u64 i = 0; i < numWorkers; i++)
	{
		stations[i].InitMalloc(numStationsToUse);
	}

	Permute64 p = allStations.GetPermute();
	for (u64 i = 0; i < numStationsToUse; i++)
	{
		StationData& stationData = stations[0][i];
		stationData.name = allStations[p.Permute(i)];

		stationData.min = rnd.NextDouble(-99.9, 99.9);
		stationData.max = rnd.NextDouble(-99.9, 99.9);
		if (stationData.max < stationData.min)
		{
			std::swap(stationData.min, stationData.max);
		}

		stationData.rMin = DBL_MAX;
		stationData.rMax = -DBL_MAX;
		stationData.rSum = 0;
		stationData.count = 0;

		if (i > 0)
		{
			assert(stations[0][i].name != stations[0][i - 1].name);
		}
	}

	allStations.~Vector();

	for (u64 i = 1; i < numWorkers; i++)
	{
		stations[i].Copy(stations[0]); // Give each thread a copy of the station info so we can merge them later to generate the validation file
	}

	// Give each thread some memory to fill, write the results, iterate until we have some small number of lines left to fill

	u64 totalMemory = static_cast<u64>(round(bufferSize * GB));

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
		threads.Push(new std::thread(GenerateDataJob, &jobs[i], &stations[i]));
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
		printf("%.1f%% generated, writing %.2f GB", 100.0 - ((double)(linesRemaining) / totalLines) * 100, (double)(totalToWrite) / GB);

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
		GenerateLine(rnd, writeBuf, &stations[0]);
	}

	if (!fh.Append(writeBuf)) return 1;
	fh.Close();

	system("cls");
	printf("Generated %lld lines using %ld stations in %s\n", totalLines, numStationsToUse, (const char*)outputPath);

	// Create validation file

	for (u64 i = 1; i < numWorkers; i++)
	{
		for (u64 j = 0; j < numStationsToUse; j++)
		{
			stations[0][j].Merge(stations[i][j]);
		}
	}

	Array<u64> sortedStations;
	sortedStations.InitMalloc(numStationsToUse);
	ForVector(sortedStations, i)
	{
		sortedStations[i] = i;
	}

	std::sort(sortedStations.data, sortedStations.data + sortedStations.size,
		[&](const u64 a, const u64 b) {
			return stations[0][a].name < stations[0][b].name;
		});

	writeBuf.Clear();
	writeBuf.Push('{');

	u64 total = 0;

	bool first = true;
	for (u64 i = 0; i < numStationsToUse; i++)
	{
		if (!first)
		{
			writeBuf.Push(", ");
		}
		const StationData& stationData = stations[0][sortedStations[i]];
		writeBuf.PushF(stationData.name, '=');
		Push1DecimalDouble(writeBuf, stationData.rMin);
		writeBuf.Push('/');
		Push1DecimalDouble(writeBuf, stationData.rSum / stationData.count);
		writeBuf.Push('/');
		Push1DecimalDouble(writeBuf, stationData.rMax);
		first = false;

		total += stationData.count;
	}
	assert(total == totalLines);

	writeBuf.Push('}');

	fh = OpenUTF8FileWrite(validationPath);
	if (!fh.Append(writeBuf)) return 1;
	fh.Close();

	printf("Generated validation file in %s", (const char*)validationPath);

	return 0;
}
