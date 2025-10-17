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


struct MappedFileHandle
{
#ifdef _WIN32
	HANDLE fileHandle = INVALID_HANDLE_VALUE;
	HANDLE mappingHandle = NULL;
#else
	int fd = -1;
#endif

	char* data = nullptr;
	u64 length = 0;

	bool Good() const
	{
		return data != nullptr && length > 0;
	}

	void Close()
	{
		if (data)
		{
#ifdef _WIN32
			UnmapViewOfFile(data);
#else
			munmap(data, (size_t)length);
#endif
			data = nullptr;
		}

#ifdef _WIN32
		if (mappingHandle)
		{
			CloseHandle(mappingHandle);
			mappingHandle = NULL;
		}
		if (fileHandle != INVALID_HANDLE_VALUE)
		{
			CloseHandle(fileHandle);
			fileHandle = INVALID_HANDLE_VALUE;
		}
#else
		if (fd >= 0)
		{
			close(fd);
			fd = -1;
		}
#endif
		length = 0;
	}

	// ------------------------------------------------------------------------------------------------
	// POSIX implementations
	// ------------------------------------------------------------------------------------------------

#ifndef _WIN32

	bool OpenRead(const char* filename)
	{
		Close();

		fd = ::open(filename, O_RDONLY);
		if (fd < 0) return false;

		struct stat st;
		if (fstat(fd, &st) != 0)
		{
			close(fd);
			fd = -1;
			return false;
		}

		length = (u64)st.st_size;
		if (length == 0)
		{
			// nothing to map
			close(fd);
			fd = -1;
			length = 0;
			return false;
		}

		void* ptr = mmap(nullptr, (size_t)length, PROT_READ, MAP_SHARED, fd, 0);
		if (ptr == MAP_FAILED)
		{
			close(fd);
			fd = -1;
			length = 0;
			return false;
		}

		data = (char*)ptr;
		return true;
	}

#endif // !_WIN32

	// ------------------------------------------------------------------------------------------------
	// Windows implementations
	// ------------------------------------------------------------------------------------------------

#ifdef _WIN32

	bool OpenRead(const char* filename)
	{
		Close();

		fileHandle = ::CreateFileA(filename,
			GENERIC_READ,
			FILE_SHARE_READ,
			NULL,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
			NULL);

		if (fileHandle == INVALID_HANDLE_VALUE)
		{
			PrintLastWinError("CreateFileA(OpenRead) failed: ");
			return false;
		}

		LARGE_INTEGER fileSizeLi;
		if (!GetFileSizeEx(fileHandle, &fileSizeLi))
		{
			PrintLastWinError("GetFileSizeEx failed: ");
			Close();
			return false;
		}

		if (fileSizeLi.QuadPart == 0)
		{
			Close();
			return false;
		}

		length = (u64)fileSizeLi.QuadPart;

		mappingHandle = ::CreateFileMappingA(fileHandle, NULL, PAGE_READONLY, 0, 0, NULL);
		if (!mappingHandle)
		{
			PrintLastWinError("CreateFileMappingA failed: ");
			Close();
			return false;
		}

		void* ptr = ::MapViewOfFile(mappingHandle, FILE_MAP_READ, 0, 0, 0);
		if (!ptr)
		{
			PrintLastWinError("MapViewOfFile failed: ");
			Close();
			return false;
		}

		data = (char*)ptr;
		return true;
	}
#endif // _WIN32
};

#ifdef _WIN32
static bool PrefetchVirtualMemory(const void* base, const size_t bytes, const size_t prefetchChunk)
{
	// Prepare in chunks to avoid giant requests
	SIZE_T offset = 0;
	while (offset < bytes)
	{
		SIZE_T chunk = std::min((SIZE_T)prefetchChunk, bytes - offset);
		WIN32_MEMORY_RANGE_ENTRY range;
		range.VirtualAddress = (PVOID)((char*)base + offset);
		range.NumberOfBytes = (SIZE_T)chunk;
		// Prefetch; returns nonzero on success
		BOOL ok = PrefetchVirtualMemory(GetCurrentProcess(), 1, &range, 0);
		if (!ok)
		{
			return false;
		}
		offset += chunk;
	}
	return true;
}
#endif
