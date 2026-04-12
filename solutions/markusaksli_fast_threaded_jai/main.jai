#import "Basic";
#import "Math";
#import "Thread";
#import "File";
#import "System";
#import "Sort";
#import "Windows";

NUM_STATIONS :: 100;
MAP_CAPACITY :: 512;

Station_Data :: struct
{
    min : s16 = 32767;
    max : s16 = -32768;
    count : u32;
    sum : s64;
}

Station_Mapping :: struct
{
    header : u32;
    data : u32;
}

compare_mapping :: (a: Station_Mapping, b: Station_Mapping) -> s64
{
    return compare_strings(string.{main_mem.map.items[a.header].namelen, main_mem.map.items[a.header].name}, string.{main_mem.map.items[b.header].namelen, main_mem.map.items[b.header].name});
}

Thread_Memory :: struct
{
    thread : Thread;
    pos : *u8;
    parse_end : *u8;
    map : Flat_Map;
    num_station : u32;
    stations : [NUM_STATIONS] Station_Data;
    station_to_header : [NUM_STATIONS] u32;
}

main_mem : *Thread_Memory;

FNV_SEED : u64 : 14695981039346656037;
FNV_PRIME : u64 : 1099511628211;

fnv1a_step :: inline (c : u8, hash : *u64)
{
    hash.* = hash.* ^ c;
    hash.* *= FNV_PRIME;
}

round :: inline (n: float64) -> float64 {
    return floor(n + 0.5);
}

push_1_decimal_double :: inline (sb : *String_Builder, scaled : s64)
{
    int_part := scaled / 10;
    decimal := abs(scaled % 10);

    if int_part == 0 && scaled < 0
    {
        append(sb, "-");
    }

    print_to_builder(sb, "%.%", int_part, decimal);
}

push_1_decimal_double_round_toward_positive :: inline (sb : *String_Builder, x : float64)
{
    scaled := cast(s64) ceil(x * 10);
    push_1_decimal_double(sb, scaled);
}

push_1_decimal_double :: inline (sb : *String_Builder, x : float64)
{
    scaled := cast(s64) round(x * 10);
    push_1_decimal_double(sb, scaled);
}

add_measurement :: inline (using station : *Station_Data, temp : s16)
{
    if temp < min min = temp;
    if temp > max max = temp;
    count += 1;
    sum += temp;
}

merge_station_data :: inline (using station : *Station_Data, other : *Station_Data)
{
    if other.min < min min = other.min;
    if other.max > max max = other.max;
    count += other.count;
    sum += other.sum;
}

Flat_Map :: struct
{
    Entry :: struct
    {
        name : *u8;
        namelen, value_index : u32;
        hash : u64;
    }

    items : [MAP_CAPACITY] Entry;
}

find_or_insert :: inline (using map : *Flat_Map, k : string, hash : u64, num_stations : *u32, station_to_header : [] u32) -> u32
{
    idx := cast(u32) hash & (MAP_CAPACITY - 1);
    while true
    {
        e : *Entry = *items[idx];
        if e.namelen == 0
        {
            assert(num_stations.* < NUM_STATIONS);
            e.hash = hash;
            e.name = k.data;
            e.namelen = cast(u32) k.count;
            e.value_index = num_stations.*;
            station_to_header[num_stations.*] = idx;
            ret := num_stations.*;
            num_stations.* += 1;
            return ret;
        }
        if (e.hash == hash && k == string.{e.namelen, e.name})
        {
            return e.value_index;
        }
        idx = (idx + 1) & (MAP_CAPACITY - 1);
    }
    return 0;
}

parse_temp_as_s16 :: inline (pos : **u8) -> s16
{
    sign : s16 = 1;
    data : u64 = (cast,no_check(*u64) pos.*).*;
    c : u8 = cast,no_check(u8) data & 0xff;
    data = data >> 8;
    if c == #char "-"
    {
        sign = -1;
        pos.* += 1;
        c = cast,no_check(u8) data & 0xff;
        data = data >> 8;
    }

    tens : s16 = (c - #char "0") * 10;

    c = cast,no_check(u8) data & 0xff;
    data = data >> 8;
    if (c != #char ".")
    {
        tens = tens * 10 + (c - #char "0") * 10;
        pos.* += 1;
        data = data >> 8;
    }

    c = cast,no_check(u8) data & 0xff;
    tens = sign * (tens + c - #char "0");
    pos.* += 4;

    return tens;
}

seek_and_hash :: inline (pos : **u8, hash : *u64)
{
    while (true)
    {
        c : u8 = pos.*.*;
        if c == #char ";" return;
        fnv1a_step(c, hash);
        pos.* += 1;
    }
}

parse :: (mem : *Thread_Memory)
{
    while (mem.pos < mem.parse_end)
    {
        read : string = .{data=mem.pos};
        hash := FNV_PRIME;
        seek_and_hash(*mem.pos, *hash);
        read.count = mem.pos - read.data;

        result := find_or_insert(*mem.map, read, hash, *mem.num_station, mem.station_to_header);
        data := *mem.stations[result];
        mem.pos += 1;

        add_measurement(data, parse_temp_as_s16(*mem.pos));
    }
}

parse_thread :: (thread : *Thread) -> s64
{
    parse(cast(*Thread_Memory) thread.data);
    return 0;
}

seek_to_char_32 :: inline (pos : **u8, c : u8)
{
    #asm AVX2
    {
        pbroadcastb.y test:, [*c];
    }

    while true
    {
        mask : u32 = ---;
        read_pos := pos.*;
        #asm AVX,AVX2
        {
            movdqu.y chunk:, [read_pos];
            pcmpeqb.y chunk, test, chunk;
            pmovmskb.y mask, chunk;
        }

        if (mask == 0)
        {
            pos.* += 32;
            continue;
        }

        advance : u32 = ---;
        #asm
        {
            bsf.d advance, mask;
        }
        pos.* += advance;
        break;
    }
}

main :: ()
{
    file_name := get_command_line_arguments()[1];
    fileinfo, success := map_entire_file_start(file_name);
    if !success
    {
        return;
    }
    defer map_entire_file_end(*fileinfo);
    file_end := *fileinfo.data.data[fileinfo.data.count];
    pos := fileinfo.data.data + 3;

    num_threads := get_number_of_processors(.PERFORMANCE_PHYSICAL);
    per_thread_bytes := fileinfo.data.count / num_threads;
    mem := NewArray(num_threads, Thread_Memory);

    for i: 0 .. num_threads - 1
    {
        mem[i].pos = pos;
        if (i != num_threads - 1)
        {
            thread_init(*mem[i].thread, parse_thread);
            mem[i].thread.data = *mem[i];

            pos += per_thread_bytes;
            seek_to_char_32(*pos, #char "\n");
            pos += 1;
            mem[i].parse_end = pos;
        }
    }

    main_mem = *mem[num_threads - 1];
    main_mem.parse_end = file_end;

    for i: 0 .. num_threads - 2
    {
        thread_start(*mem[i].thread);
    }
    parse(main_mem);

    for i: 0 .. num_threads - 2
    {
        other : *Thread_Memory = *mem[i];
        if thread_is_done(*other.thread, -1)
        {
            thread_deinit(*other.thread);
            for j: 0 .. other.num_station - 1
            {
                other_data := *other.stations[j];
                other_entry := other.map.items[other.station_to_header[j]];
                result := find_or_insert(*main_mem.map, string.{other_entry.namelen, other_entry.name}, other_entry.hash, *main_mem.num_station, main_mem.station_to_header);
                station_data := *main_mem.stations[result];
                merge_station_data(station_data, other_data);
            }
        }
    }

    mapping : [NUM_STATIONS] Station_Mapping;
    for *m, i: mapping
    {
        m.data = cast,no_check(u32) i;
        m.header = main_mem.station_to_header[i];
    }

    sorted := quick_sort(mapping, compare_mapping);

    sb : String_Builder;
    init_string_builder(*sb, 4 * 1024);

    append(*sb, #char "{");
    first := true;
    for *m, i: mapping
    {
        if !first
        {
            append(*sb, ", ");
        }
        first = false;

        sd := *main_mem.stations[m.data];
        h := main_mem.map.items[m.header];
        append(*sb, string.{h.namelen, h.name});
        append(*sb, #char "=");
        push_1_decimal_double(*sb, sd.min * 0.1);
        append(*sb, #char "/");
        push_1_decimal_double_round_toward_positive(*sb, (sd.sum * 0.1) / sd.count);
        append(*sb, #char "/");
        push_1_decimal_double(*sb, sd.max * 0.1);
    }
    append(*sb, #char "}");

    SetConsoleOutputCP(CP_UTF8);

    write_string(builder_to_string(*sb));
}
