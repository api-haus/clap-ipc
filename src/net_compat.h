#pragma once

#include <cstddef>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <afunix.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace mr
{

#if defined(_WIN32)
using socket_t = SOCKET;
#else
using socket_t = int;
#endif

inline bool socket_valid(socket_t s)
{
#if defined(_WIN32)
    return s != INVALID_SOCKET;
#else
    return s >= 0;
#endif
}

inline void socket_close(socket_t s)
{
    if (!socket_valid(s))
    {
        return;
    }
#if defined(_WIN32)
    ::closesocket(s);
#else
    ::close(s);
#endif
}

inline std::ptrdiff_t socket_recv(socket_t s, void* buf, std::size_t len)
{
    return ::recv(s, static_cast<char*>(buf), static_cast<int>(len), 0);
}

inline std::ptrdiff_t socket_send(socket_t s, const void* buf, std::size_t len)
{
    return ::send(s, static_cast<const char*>(buf), static_cast<int>(len), 0);
}

struct WinsockScope
{
#if defined(_WIN32)
    bool ok = false;
    WinsockScope()
    {
        WSADATA data;
        ok = ::WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }
    ~WinsockScope()
    {
        if (ok)
        {
            ::WSACleanup();
        }
    }
#else
    bool ok = true;
#endif
};

}
