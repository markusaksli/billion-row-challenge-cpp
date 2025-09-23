#pragma once
#include <cstdint>

#include "vector.h"

#ifndef _WIN32
#include <sys/uio.h>
#include <unistd.h>
#include <cerrno>
#else
#define NOMINMAX
#include <windows.h>
#endif

using u64 = std::uint64_t;

// ------------------------------------------------------------------------------------------------
// Common data structures
// ------------------------------------------------------------------------------------------------

struct WriteData
{
    void* data;
    u64 bytes;
};

struct FileHandle
{
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
#else
    int fd = -1;
#endif

    bool Good() const
    {
#ifdef _WIN32
        return handle != INVALID_HANDLE_VALUE;
#else
        return fd >= 0;
#endif
    }

    void Close()
    {
#ifdef _WIN32
        if (handle != INVALID_HANDLE_VALUE)
        {
            ::CloseHandle(handle);
            handle = INVALID_HANDLE_VALUE;
        }
#else
        if (fd >= 0)
        {
            ::close(fd);
            fd = -1;
        }
#endif
    }
};

// ------------------------------------------------------------------------------------------------
// POSIX implementations
// ------------------------------------------------------------------------------------------------

#ifndef _WIN32

inline bool WriteAllPosix(const FileHandle& fh, Array<WriteData> bufs)
{
    u64 idx = 0;
    while (idx < bufs.size)
    {
        // Build an iovec array from what remains
        const size_t maxIov = IOV_MAX;
        size_t batch = (bufs.size - idx < maxIov) ? (size_t)(bufs.size - idx) : maxIov;

        std::vector<iovec> iovs(batch);
        for (size_t i = 0; i < batch; i++)
        {
            iovs[i].iov_base = bufs.data[idx + i].data;
            iovs[i].iov_len = (size_t)bufs.data[idx + i].bytes;
        }

        ssize_t r = ::writev(fh.fd, iovs.data(), (int)batch);
        if (r < 0)
        {
            if (errno == EINTR) continue; // retry
            return false;
        }

        // Advance through consumed bytes
        ssize_t consumed = r;
        while (consumed > 0 && idx < bufs.size)
        {
            if ((u64)consumed >= bufs.data[idx].bytes)
            {
                consumed -= (ssize_t)bufs.data[idx].bytes;
                idx++;
            }
            else
            {
                bufs.data[idx].data = (char*)bufs.data[idx].data + consumed;
                bufs.data[idx].bytes -= (u64)consumed;
                consumed = 0;
            }
        }
    }
    return true;
}
#endif // !_WIN32

// ------------------------------------------------------------------------------------------------
// Windows implementations
// ------------------------------------------------------------------------------------------------

#ifdef _WIN32

inline bool WriteAllWinGather(const FileHandle& fh, Array<WriteData> bufs)
{
    if (bufs.size == 0) return true;

    Array<FILE_SEGMENT_ELEMENT> segments;
    segments.InitMallocZero(bufs.size + 1); // +1 null terminator

    for (u64 i = 0; i < bufs.size; ++i)
    {
        segments[i].Buffer = bufs.data[i].data;
        // Alignment requirement: buffers must be sector aligned (512 bytes)
        if (((uintptr_t)bufs.data[i].data % 512) != 0)
        {
            fprintf(stderr, "Buffer %llu not 512-byte aligned. Cannot use WriteFileGather.\n", i);
            return false;
        }
        if (bufs.data[i].bytes % 512 != 0)
        {
            fprintf(stderr, "Buffer %llu size not multiple of 512. Cannot use WriteFileGather.\n", i);
            return false;
        }
    }
    // Null terminate
    segments[bufs.size].Buffer = nullptr;

    u32 li = {};
    BOOL result = ::WriteFileGather(fh.handle, segments.data, 0, &li, nullptr);
    if (!result)
    {
        DWORD err = ::GetLastError();
        fprintf(stderr, "WriteFileGather failed: %lu\n", err);
        return false;
    }
    return true;
}

#endif // _WIN32

// ------------------------------------------------------------------------------------------------
// Cross-platform wrappers
// ------------------------------------------------------------------------------------------------

inline bool WriteAll(const FileHandle& fh, Array<WriteData> bufs)
{
#ifdef _WIN32
    return WriteAllWinGather(fh, bufs);
#else
    return WriteAllPosix(fh, bufs);
#endif
}

inline bool WriteSingle(const FileHandle& fh, const void* data, u64 bytes)
{
#ifdef _WIN32
    DWORD written = 0;
    const char* ptr = (const char*)data;
    u64 remaining = bytes;

    while (remaining > 0)
    {
        DWORD chunk = (remaining > U32_MAX) ? U32_MAX: (DWORD)remaining;
        if (!::WriteFile(fh.handle, ptr, chunk, &written, nullptr))
        {
            return false;
        }
        ptr += written;
        remaining -= written;
    }
    return true;
#else
    const char* ptr = (const char*)data;
    u64 remaining = bytes;

    while (remaining > 0)
    {
        ssize_t r = ::write(fh.fd, ptr, remaining);
        if (r < 0)
        {
            if (errno == EINTR) continue;
            return false;
        }
        ptr += r;
        remaining -= r;
    }
    return true;
#endif
}

inline bool AppendSingle(const FileHandle& fh, const void* data, u64 bytes)
{
#ifdef _WIN32
    LARGE_INTEGER li;
    li.QuadPart = 0;
    if (::SetFilePointerEx(fh.handle, li, NULL, FILE_END) == 0)
        return false;
    return WriteSingle(fh, data, bytes);
#else
    if (::lseek(fh.fd, 0, SEEK_END) < 0) return false;
    return WriteSingle(fh, data, bytes);
#endif
}