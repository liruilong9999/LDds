#ifndef LSOCKET_UTIL_H
#define LSOCKET_UTIL_H

#include "LQtCompat.h"

#include <cstring>
#include <mutex>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace LDdsFramework {
namespace Net {

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#endif

class SocketRuntime
{
public:
    static bool ensureInitialized(std::string* error)
    {
        static SocketRuntime runtime;
        if (!runtime.initialized_ && error != nullptr)
        {
            *error = runtime.error_;
        }
        return runtime.initialized_;
    }

private:
    SocketRuntime()
        : initialized_(true)
        , error_()
    {
#ifdef _WIN32
        WSADATA data;
        const int result = WSAStartup(MAKEWORD(2, 2), &data);
        if (result != 0)
        {
            initialized_ = false;
            error_ = "WSAStartup failed: " + std::to_string(result);
        }
#endif
    }

    ~SocketRuntime()
    {
#ifdef _WIN32
        if (initialized_)
        {
            WSACleanup();
        }
#endif
    }

private:
    bool initialized_;
    std::string error_;
};

inline int getLastErrorCode() noexcept
{
#ifdef _WIN32
    return static_cast<int>(WSAGetLastError());
#else
    return errno;
#endif
}

inline std::string errorCodeToString(int errorCode)
{
#ifdef _WIN32
    char* messageBuffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                        FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD languageId = MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT);
    const DWORD size = FormatMessageA(
        flags,
        nullptr,
        static_cast<DWORD>(errorCode),
        languageId,
        reinterpret_cast<LPSTR>(&messageBuffer),
        0,
        nullptr);

    std::string message = "socket error=" + std::to_string(errorCode);
    if (size != 0 && messageBuffer != nullptr)
    {
        message += ", detail=";
        message += std::string(messageBuffer, size);
        LocalFree(messageBuffer);
    }
    return message;
#else
    return "socket error=" + std::to_string(errorCode) + ", detail=" + std::strerror(errorCode);
#endif
}

inline bool isWouldBlockError(int errorCode) noexcept
{
#ifdef _WIN32
    return errorCode == WSAEWOULDBLOCK;
#else
    return errorCode == EWOULDBLOCK || errorCode == EAGAIN;
#endif
}

inline bool isInProgressError(int errorCode) noexcept
{
#ifdef _WIN32
    return errorCode == WSAEINPROGRESS || errorCode == WSAEWOULDBLOCK || errorCode == WSAEALREADY;
#else
    return errorCode == EINPROGRESS || errorCode == EALREADY;
#endif
}

inline int closeSocketRaw(SocketHandle socket) noexcept
{
#ifdef _WIN32
    return closesocket(socket);
#else
    return close(socket);
#endif
}

inline void closeSocketQuietly(SocketHandle& socket) noexcept
{
    if (socket == kInvalidSocket)
    {
        return;
    }

    (void)closeSocketRaw(socket);
    socket = kInvalidSocket;
}

inline bool setNonBlocking(SocketHandle socket, bool enabled, std::string* error)
{
#ifdef _WIN32
    u_long mode = enabled ? 1UL : 0UL;
    if (ioctlsocket(socket, FIONBIO, &mode) != 0)
    {
        if (error != nullptr)
        {
            *error = errorCodeToString(getLastErrorCode());
        }
        return false;
    }
#else
    const int flags = fcntl(socket, F_GETFL, 0);
    if (flags < 0)
    {
        if (error != nullptr)
        {
            *error = errorCodeToString(getLastErrorCode());
        }
        return false;
    }

    int newFlags = flags;
    if (enabled)
    {
        newFlags |= O_NONBLOCK;
    }
    else
    {
        newFlags &= ~O_NONBLOCK;
    }

    if (fcntl(socket, F_SETFL, newFlags) != 0)
    {
        if (error != nullptr)
        {
            *error = errorCodeToString(getLastErrorCode());
        }
        return false;
    }
#endif

    return true;
}

inline bool setSocketReuseAddress(SocketHandle socket, bool enabled, std::string* error)
{
    const int flag = enabled ? 1 : 0;
    if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&flag), sizeof(flag)) != 0)
    {
        if (error != nullptr)
        {
            *error = errorCodeToString(getLastErrorCode());
        }
        return false;
    }
    return true;
}

inline bool setSocketBufferSize(SocketHandle socket, int optionName, int size, std::string* error)
{
    if (size <= 0)
    {
        return true;
    }

    if (setsockopt(socket, SOL_SOCKET, optionName, reinterpret_cast<const char*>(&size), sizeof(size)) != 0)
    {
        if (error != nullptr)
        {
            *error = errorCodeToString(getLastErrorCode());
        }
        return false;
    }
    return true;
}

inline bool setSocketBroadcast(SocketHandle socket, bool enabled, std::string* error)
{
    const int flag = enabled ? 1 : 0;
    if (setsockopt(socket, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&flag), sizeof(flag)) != 0)
    {
        if (error != nullptr)
        {
            *error = errorCodeToString(getLastErrorCode());
        }
        return false;
    }
    return true;
}

inline bool setSocketMulticastTtl(SocketHandle socket, int ttl, std::string* error)
{
    if (ttl <= 0)
    {
        return true;
    }

    const unsigned char value = static_cast<unsigned char>(ttl & 0xFF);
    if (setsockopt(socket, IPPROTO_IP, IP_MULTICAST_TTL, reinterpret_cast<const char*>(&value), sizeof(value)) != 0)
    {
        if (error != nullptr)
        {
            *error = errorCodeToString(getLastErrorCode());
        }
        return false;
    }
    return true;
}

inline sockaddr_in toSockAddr(const LHostAddress& address, quint16 port) noexcept
{
    sockaddr_in result;
    std::memset(&result, 0, sizeof(result));
    result.sin_family = AF_INET;
    result.sin_port = htons(port);
    result.sin_addr.s_addr = htonl(address.toIPv4Address());
    return result;
}

inline LHostAddress fromSockAddr(const sockaddr_in& address) noexcept
{
    return LHostAddress::fromIPv4Address(ntohl(address.sin_addr.s_addr));
}

inline quint16 portFromSockAddr(const sockaddr_in& address) noexcept
{
    return static_cast<quint16>(ntohs(address.sin_port));
}

inline bool isSocketValid(SocketHandle socket) noexcept
{
    return socket != kInvalidSocket;
}

inline bool tryGetBoundPort(SocketHandle socket, quint16& port)
{
    sockaddr_in localAddr;
    std::memset(&localAddr, 0, sizeof(localAddr));
#ifdef _WIN32
    int len = static_cast<int>(sizeof(localAddr));
#else
    socklen_t len = static_cast<socklen_t>(sizeof(localAddr));
#endif
    if (getsockname(socket, reinterpret_cast<sockaddr*>(&localAddr), &len) != 0)
    {
        return false;
    }

    port = static_cast<quint16>(ntohs(localAddr.sin_port));
    return true;
}

inline int selectRead(SocketHandle socket, int timeoutMs)
{
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(socket, &readSet);

    timeval timeout;
    timeout.tv_sec = timeoutMs / 1000;
    timeout.tv_usec = static_cast<long>((timeoutMs % 1000) * 1000);

#ifdef _WIN32
    return select(0, &readSet, nullptr, nullptr, &timeout);
#else
    return select(socket + 1, &readSet, nullptr, nullptr, &timeout);
#endif
}

inline bool waitForSocketWritable(SocketHandle socket, int timeoutMs)
{
    fd_set writeSet;
    FD_ZERO(&writeSet);
    FD_SET(socket, &writeSet);

    timeval timeout;
    timeout.tv_sec = timeoutMs / 1000;
    timeout.tv_usec = static_cast<long>((timeoutMs % 1000) * 1000);

#ifdef _WIN32
    const int result = select(0, nullptr, &writeSet, nullptr, &timeout);
#else
    const int result = select(socket + 1, nullptr, &writeSet, nullptr, &timeout);
#endif
    if (result <= 0)
    {
        return false;
    }

    int socketError = 0;
#ifdef _WIN32
    int optLen = static_cast<int>(sizeof(socketError));
#else
    socklen_t optLen = static_cast<socklen_t>(sizeof(socketError));
#endif
    if (getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&socketError), &optLen) != 0)
    {
        return false;
    }

    return socketError == 0;
}

inline bool connectWithTimeout(SocketHandle socket, const sockaddr_in& remote, int timeoutMs, std::string* error)
{
    std::string setError;
    if (!setNonBlocking(socket, true, &setError))
    {
        if (error != nullptr)
        {
            *error = setError;
        }
        return false;
    }

    const int result = connect(socket, reinterpret_cast<const sockaddr*>(&remote), sizeof(remote));
    if (result == 0)
    {
        return true;
    }

    const int lastError = getLastErrorCode();
    if (!isInProgressError(lastError))
    {
        if (error != nullptr)
        {
            *error = errorCodeToString(lastError);
        }
        return false;
    }

    if (!waitForSocketWritable(socket, timeoutMs))
    {
        if (error != nullptr)
        {
            *error = "connect timeout after " + std::to_string(timeoutMs) + "ms";
        }
        return false;
    }

    return true;
}

} // namespace Net
} // namespace LDdsFramework

#endif // LSOCKET_UTIL_H
