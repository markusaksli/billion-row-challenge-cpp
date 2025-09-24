#pragma once

#include "vector.h"
#include "buf_string.h"

#ifndef _WIN32
#include <sys/uio.h>
#include <unistd.h>
#include <cerrno>
#else
#define NOMINMAX
#include <windows.h>
#endif

#ifdef _WIN32

static void PrintLastWinError(const char* prefix)
{
	DWORD err = ::GetLastError();
	if (err == 0)
	{
		printf("%sNo Windows error.\n", prefix);
		return;
	}

	LPVOID msgBuffer = nullptr;

	DWORD size = FormatMessageA(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr,
		err,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPSTR)&msgBuffer,
		0,
		nullptr
	);

	if (size)
	{
		printf("%sError 0x%08lX: %s", prefix, err, (char*)msgBuffer);
		LocalFree(msgBuffer);
	}
	else
	{
		printf("%sError 0x%08lX: <failed to get error message>\n", prefix, err);
	}
}

#endif // _WIN32

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

	// ------------------------------------------------------------------------------------------------
	// POSIX implementations
	// ------------------------------------------------------------------------------------------------

#ifndef _WIN32

#endif // !_WIN32

	// ------------------------------------------------------------------------------------------------
	// Windows implementations
	// ------------------------------------------------------------------------------------------------

#ifdef _WIN32

#endif // _WIN32

	// ------------------------------------------------------------------------------------------------
	// Cross-platform wrappers
	// ------------------------------------------------------------------------------------------------

	bool Write(const void* data, u64 bytes) const
	{
	    if (!Good())
	    {
	        return false;
	    }

	#ifdef _WIN32
	    DWORD written = 0;
	    const char* ptr = (const char*)data;
	    u64 remaining = bytes;

	    while (remaining > 0)
	    {
	        DWORD chunk = (remaining > U32_MAX) ? U32_MAX: (DWORD)remaining;
	        if (!::WriteFile(handle, ptr, chunk, &written, nullptr))
	        {
	            PrintLastWinError("WriteFile failed: ");
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
	        ssize_t r = ::write(fd, ptr, remaining);
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

	bool Append(const void* data, u64 bytes) const
	{
	    if (!Good())
	    {
	        return false;
	    }

	#ifdef _WIN32
	    LARGE_INTEGER li;
	    li.QuadPart = 0;
	    if (::SetFilePointerEx(handle, li, NULL, FILE_END) == 0)
	        return false;
	    return Write(data, bytes);
	#else
	    if (::lseek(fd, 0, SEEK_END) < 0) return false;
	    return Write(data, bytes);
	#endif
	}

	bool Append(const StringBuffer &strbuf) const
	{
		return Append(strbuf.data, strbuf.Bytes());
	}
};

inline FileHandle OpenUTF8FileWrite(const char* filename)
{
	FileHandle fh;
#ifndef _WIN32
	fh.fd = ::open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
#else
	fh.handle = ::CreateFileA(filename,
		GENERIC_WRITE,
		0,
		NULL,
		CREATE_ALWAYS,
		NULL,
		NULL);
	if (!fh.Good())
	{
		PrintLastWinError("Opening file: ");
	}
#endif

	if (fh.Good()) {
		const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
		if (!fh.Write(bom, 3)) return {};
	}

	return fh;
}
