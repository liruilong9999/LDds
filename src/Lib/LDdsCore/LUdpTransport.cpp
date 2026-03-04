#include "LUdpTransport.h"
#include "LMessage.h"

#include <chrono>
#include <cstring>
#include <vector>

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
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace LDdsFramework {
namespace {

#ifdef _WIN32
using SocketHandle = SOCKET;
using SockLenType = int;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
using SocketHandle = int;
using SockLenType = socklen_t;
constexpr SocketHandle kInvalidSocket = -1;
#endif

constexpr std::intptr_t kInvalidSocketStorage = static_cast<std::intptr_t>(-1);

bool initializeSocketApi()
{
#ifdef _WIN32
    static std::once_flag once;
    static bool initialized = false;
    std::call_once(once, [] {
        WSADATA wsaData {};
        initialized = (::WSAStartup(MAKEWORD(2, 2), &wsaData) == 0);
    });
    return initialized;
#else
    return true;
#endif
}

SocketHandle toNativeSocket(std::intptr_t stored) noexcept
{
    return static_cast<SocketHandle>(stored);
}

std::intptr_t toStoredSocket(SocketHandle socketFd) noexcept
{
    return static_cast<std::intptr_t>(socketFd);
}

void closeNativeSocket(SocketHandle socketFd)
{
    if (socketFd == kInvalidSocket) {
        return;
    }
#ifdef _WIN32
    ::closesocket(socketFd);
#else
    ::close(socketFd);
#endif
}

int getLastSocketError() noexcept
{
#ifdef _WIN32
    return ::WSAGetLastError();
#else
    return errno;
#endif
}

bool isInterruptedOrWouldBlock(int errorCode) noexcept
{
#ifdef _WIN32
    return errorCode == WSAEINTR || errorCode == WSAEWOULDBLOCK;
#else
    return errorCode == EINTR || errorCode == EAGAIN || errorCode == EWOULDBLOCK;
#endif
}

bool setSocketOptionInt(SocketHandle socketFd, int level, int optName, int value)
{
    return ::setsockopt(
               socketFd,
               level,
               optName,
               reinterpret_cast<const char*>(&value),
               static_cast<SockLenType>(sizeof(value))
           ) == 0;
}

bool toSockAddr(const QHostAddress& address, quint16 port, sockaddr_in& output)
{
    std::memset(&output, 0, sizeof(output));
    output.sin_family = AF_INET;
    output.sin_port = htons(port);

    if (address.isNull() || address == QHostAddress::Any || address == QHostAddress::AnyIPv4) {
        output.sin_addr.s_addr = htonl(INADDR_ANY);
        return true;
    }

    if (address == QHostAddress::Broadcast) {
        output.sin_addr.s_addr = htonl(INADDR_BROADCAST);
        return true;
    }

    const QByteArray ip = address.toString().toLatin1();
    return ::inet_pton(AF_INET, ip.constData(), &output.sin_addr) == 1;
}

QHostAddress fromSockAddr(const sockaddr_in& input)
{
    char ipBuffer[INET_ADDRSTRLEN] = {0};
    const char* result = ::inet_ntop(AF_INET, &input.sin_addr, ipBuffer, sizeof(ipBuffer));
    if (!result) {
        return QHostAddress();
    }
    return QHostAddress(QString::fromLatin1(ipBuffer));
}

} // namespace

LUdpTransport::LUdpTransport()
    : m_socket(kInvalidSocketStorage)
    , m_running(false)
    , m_maxPacketSize(65507)
    , m_sentPacketCount(0)
    , m_recvPacketCount(0)
    , m_sentByteCount(0)
    , m_recvByteCount(0)
{
}

LUdpTransport::~LUdpTransport()
{
    stop();
}

TransportProtocol LUdpTransport::getProtocol() const noexcept
{
    return TransportProtocol::UDP;
}

bool LUdpTransport::start()
{
    if (getState() == TransportState::Running) {
        return true;
    }

    setState(TransportState::Starting);

    if (!initializeSocket()) {
        setState(TransportState::Error);
        return false;
    }

    m_running.store(true);
    m_receiveThread = std::thread(&LUdpTransport::receiveThreadFunc, this);

    setState(TransportState::Running);
    return true;
}

void LUdpTransport::stop()
{
    const TransportState currentState = getState();
    if (currentState == TransportState::Stopped) {
        return;
    }

    setState(TransportState::Stopping);

    m_running.store(false);
    closeSocket();

    if (m_receiveThread.joinable()) {
        m_receiveThread.join();
    }

    setState(TransportState::Stopped);
}

bool LUdpTransport::sendMessage(const LMessage& message)
{
    QHostAddress targetAddress;
    quint16 targetPort = 0;
    if (!resolveRemoteFromConfig(targetAddress, targetPort)) {
        setError(QStringLiteral("Default UDP remote endpoint is not configured"));
        return false;
    }
    return sendMessageTo(message, targetAddress, targetPort);
}

bool LUdpTransport::sendMessageTo(const LMessage& message,
                                  const QHostAddress& targetAddress,
                                  quint16 targetPort)
{
    if (getState() != TransportState::Running) {
        setError(QStringLiteral("Transport is not running"));
        return false;
    }

    if (targetAddress.isNull() || targetPort == 0) {
        setError(QStringLiteral("Invalid target endpoint"));
        return false;
    }

    LByteBuffer serialized = message.serialize();
    if (serialized.size() > m_maxPacketSize.load()) {
        setError(QStringLiteral("UDP packet exceeds max packet size"));
        return false;
    }

    sockaddr_in remoteAddr {};
    if (!toSockAddr(targetAddress, targetPort, remoteAddr)) {
        setError(QStringLiteral("Target address must be valid IPv4"));
        return false;
    }

    SocketHandle socketFd = kInvalidSocket;
    {
        std::lock_guard<std::mutex> lock(m_socketMutex);
        socketFd = toNativeSocket(m_socket);
    }

    if (socketFd == kInvalidSocket) {
        setError(QStringLiteral("UDP socket is not initialized"));
        return false;
    }

    const int dataSize = static_cast<int>(serialized.size());
    const int sentBytes = ::sendto(
        socketFd,
        reinterpret_cast<const char*>(serialized.data()),
        dataSize,
        0,
        reinterpret_cast<sockaddr*>(&remoteAddr),
        static_cast<SockLenType>(sizeof(remoteAddr))
    );

    if (sentBytes < 0) {
        setError(QStringLiteral("UDP send failed, error code=%1").arg(getLastSocketError()));
        return false;
    }

    m_sentPacketCount.fetch_add(1);
    m_sentByteCount.fetch_add(static_cast<uint64_t>(sentBytes));
    return true;
}

bool LUdpTransport::broadcastMessage(const LMessage& message, quint16 broadcastPort)
{
    if (!isBroadcastEnabled()) {
        setError(QStringLiteral("UDP broadcast is disabled"));
        return false;
    }

    return sendMessageTo(message, QHostAddress::Broadcast, broadcastPort);
}

void LUdpTransport::setMaxPacketSize(size_t maxSize) noexcept
{
    m_maxPacketSize.store(maxSize == 0 ? 1 : maxSize);
}

size_t LUdpTransport::getMaxPacketSize() const noexcept
{
    return m_maxPacketSize.load();
}

bool LUdpTransport::setBroadcastEnabled(bool enable)
{
    TransportConfig config = getConfig();
    config.enableBroadcast = enable;
    setConfig(config);

    SocketHandle socketFd = kInvalidSocket;
    {
        std::lock_guard<std::mutex> lock(m_socketMutex);
        socketFd = toNativeSocket(m_socket);
    }

    if (socketFd == kInvalidSocket) {
        return true;
    }

    return setSocketOptionInt(socketFd, SOL_SOCKET, SO_BROADCAST, enable ? 1 : 0);
}

bool LUdpTransport::isBroadcastEnabled() const
{
    return getConfig().enableBroadcast;
}

uint64_t LUdpTransport::getSentPacketCount() const noexcept
{
    return m_sentPacketCount.load();
}

uint64_t LUdpTransport::getReceivedPacketCount() const noexcept
{
    return m_recvPacketCount.load();
}

uint64_t LUdpTransport::getSentByteCount() const noexcept
{
    return m_sentByteCount.load();
}

uint64_t LUdpTransport::getReceivedByteCount() const noexcept
{
    return m_recvByteCount.load();
}

void LUdpTransport::receiveThreadFunc()
{
    while (m_running.load()) {
        SocketHandle socketFd = kInvalidSocket;
        {
            std::lock_guard<std::mutex> lock(m_socketMutex);
            socketFd = toNativeSocket(m_socket);
        }

        if (socketFd == kInvalidSocket) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(socketFd, &readSet);

        timeval timeout {};
        timeout.tv_sec = 0;
        timeout.tv_usec = 200 * 1000;

        const int ready = ::select(static_cast<int>(socketFd) + 1, &readSet, nullptr, nullptr, &timeout);
        if (!m_running.load()) {
            break;
        }

        if (ready <= 0) {
            continue;
        }

        std::vector<uint8_t> packetBuffer(m_maxPacketSize.load() + 1);
        sockaddr_in senderAddr {};
        SockLenType senderLen = static_cast<SockLenType>(sizeof(senderAddr));

        const int recvBytes = ::recvfrom(
            socketFd,
            reinterpret_cast<char*>(packetBuffer.data()),
            static_cast<int>(packetBuffer.size()),
            0,
            reinterpret_cast<sockaddr*>(&senderAddr),
            &senderLen
        );

        if (recvBytes <= 0) {
            const int errorCode = getLastSocketError();
            if (!isInterruptedOrWouldBlock(errorCode) && m_running.load()) {
                setError(QStringLiteral("UDP receive failed, error code=%1").arg(errorCode));
            }
            continue;
        }

        const size_t packetSize = static_cast<size_t>(recvBytes);
        if (packetSize > m_maxPacketSize.load()) {
            // Truncated/oversized datagrams are rejected in this stage.
            continue;
        }

        LMessage message;
        if (!message.deserialize(packetBuffer.data(), packetSize)) {
            continue;
        }

        const QHostAddress senderAddress = fromSockAddr(senderAddr);
        const quint16 senderPort = ntohs(senderAddr.sin_port);
        message.setSenderAddress(senderAddress);
        message.setSenderPort(senderPort);

        m_recvPacketCount.fetch_add(1);
        m_recvByteCount.fetch_add(static_cast<uint64_t>(packetSize));
        notifyReceiveCallback(message, senderAddress, senderPort);
    }
}

bool LUdpTransport::initializeSocket()
{
    if (!initializeSocketApi()) {
        setError(QStringLiteral("Socket subsystem initialization failed"));
        return false;
    }

    const TransportConfig config = getConfig();

    SocketHandle socketFd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socketFd == kInvalidSocket) {
        setError(QStringLiteral("Failed to create UDP socket, error code=%1").arg(getLastSocketError()));
        return false;
    }

    if (config.reuseAddress) {
        setSocketOptionInt(socketFd, SOL_SOCKET, SO_REUSEADDR, 1);
    }

    if (config.receiveBufferSize > 0) {
        setSocketOptionInt(socketFd, SOL_SOCKET, SO_RCVBUF, config.receiveBufferSize);
    }

    if (config.sendBufferSize > 0) {
        setSocketOptionInt(socketFd, SOL_SOCKET, SO_SNDBUF, config.sendBufferSize);
    }

    if (config.enableBroadcast) {
        setSocketOptionInt(socketFd, SOL_SOCKET, SO_BROADCAST, 1);
    }

    const QHostAddress bindAddress = config.bindAddress.isEmpty()
        ? QHostAddress::Any
        : QHostAddress(config.bindAddress);

    sockaddr_in localAddr {};
    if (!toSockAddr(bindAddress, config.bindPort, localAddr)) {
        closeNativeSocket(socketFd);
        setError(QStringLiteral("Bind address must be valid IPv4"));
        return false;
    }

    if (::bind(socketFd, reinterpret_cast<sockaddr*>(&localAddr), sizeof(localAddr)) != 0) {
        closeNativeSocket(socketFd);
        setError(QStringLiteral("UDP bind failed, error code=%1").arg(getLastSocketError()));
        return false;
    }

    sockaddr_in actualAddr {};
    SockLenType actualLen = static_cast<SockLenType>(sizeof(actualAddr));
    if (::getsockname(socketFd, reinterpret_cast<sockaddr*>(&actualAddr), &actualLen) == 0) {
        setBoundPort(ntohs(actualAddr.sin_port));
    } else {
        setBoundPort(config.bindPort);
    }

    {
        std::lock_guard<std::mutex> lock(m_socketMutex);
        m_socket = toStoredSocket(socketFd);
    }

    return true;
}

void LUdpTransport::closeSocket()
{
    SocketHandle socketFd = kInvalidSocket;
    {
        std::lock_guard<std::mutex> lock(m_socketMutex);
        if (m_socket != kInvalidSocketStorage) {
            socketFd = toNativeSocket(m_socket);
            m_socket = kInvalidSocketStorage;
        }
    }

    closeNativeSocket(socketFd);
    setBoundPort(0);
}

bool LUdpTransport::resolveRemoteFromConfig(QHostAddress& targetAddress, quint16& targetPort) const
{
    const TransportConfig config = getConfig();
    if (config.remoteAddress.isEmpty() || config.remotePort == 0) {
        return false;
    }

    const QHostAddress parsed(config.remoteAddress);
    if (parsed.isNull()) {
        return false;
    }

    targetAddress = parsed;
    targetPort = config.remotePort;
    return true;
}

} // namespace LDdsFramework
