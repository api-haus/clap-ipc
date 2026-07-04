#pragma once

#include <cstdio>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace mr
{

inline void* dl_open(const char* path)
{
#if defined(_WIN32)
    return reinterpret_cast<void*>(::LoadLibraryA(path));
#else
    return ::dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}

inline void* dl_symbol(void* handle, const char* name)
{
#if defined(_WIN32)
    return reinterpret_cast<void*>(::GetProcAddress(reinterpret_cast<HMODULE>(handle), name));
#else
    return ::dlsym(handle, name);
#endif
}

inline void dl_close(void* handle)
{
    if (handle == nullptr)
    {
        return;
    }
#if defined(_WIN32)
    ::FreeLibrary(reinterpret_cast<HMODULE>(handle));
#else
    ::dlclose(handle);
#endif
}

inline const char* dl_last_error()
{
#if defined(_WIN32)
    static thread_local char buffer[256];
    DWORD code = ::GetLastError();
    DWORD n = ::FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                               code, 0, buffer, sizeof(buffer), nullptr);
    if (n == 0)
    {
        std::snprintf(buffer, sizeof(buffer), "error %lu", static_cast<unsigned long>(code));
    }
    return buffer;
#else
    return ::dlerror();
#endif
}

}
