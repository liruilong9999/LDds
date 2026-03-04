#include "LTcpTransport.h"
#include "LMessage.h"

#include <chrono>
#include <cstring>

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
constexpr uint32_t kMaxTcpMessageSize = 10U * 1024U * 1024U;

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

void shutdownNativeSocket(SocketHandle socketFd)
{
    if (socketFd == kInvalidSocket) {
        return;
    }
#ifdef _WIN32
    ::shutdown(socketFd, SD_BOTH);
#else
    ::shutdown(socketFd, SHUT_RDWR);
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

bool setSocketNonBlocking(SocketHandle socketFd, bool enable)
{
#ifdef _WIN32
    u_long mode = enable ? 1UL : 0UL;
    return ::ioctlsocket(socketFd, FIONBIO, &mode) == 0;
#else
    const int flags = ::fcntl(socketFd, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    const int newFlags = enable ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    return ::fcntl(socketFd, F_SETFL, newFlags) == 0;
#endif
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

uint32_t readLeUInt32(const uint8_t* data) noexcept
{
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

bool sendAll(SocketHandle socketFd, const uint8_t* data, size_t size)
{
    size_t sentBytes = 0;
    while (sentBytes < size) {
        const int remaining = static_cast<int>(size - sentBytes);
        const int chunkSent = ::send(
            socketFd,
            reinterpret_cast<const char*>(data + sentBytes),
            remaining,
            0
        );

        if (chunkSent > 0) {
            sentBytes += static_cast<size_t>(chunkSent);
            continue;
        }

        if (chunkSent == 0) {
            return false;
        }

        const int errorCode = getLastSocketError();
        if (isInterruptedOrWouldBlock(errorCode)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        return false;
    }
    return true;
}

} // namespace

struct LTcpTransport::TcpConnection {
    TcpConnection(quint64 id,
                  SocketHandle socket,
                  const QHostAddress& address,
                  quint16 port)
        : connectionId(id)
        , socketFd(socket)
        , remoteAddress(address)
        , remotePort(port)
        , bytesSent(0)
        , bytesReceived(0)
        , connected(true)
    {
    }

    quint64 connectionId;
    SocketHandle socketFd;
    QHostAddress remoteAddress;
    quint16 remotePort;
    std::vector<uint8_t> receiveBuffer;
    std::atomic<uint64_t> bytesSent;
    std::atomic<uint64_t> bytesReceived;
    std::atomic<bool> connected;
    mutable std::mutex mutex;
};

LTcpTransport::LTcpTransport()
    : m_listenSocket(kInvalidSocketStorage)
    , m_running(false)
    , m_sendThreadRunning(false)
    , m_nextConnectionId(1)
{
}

LTcpTransport::~LTcpTransport()
{
    stop();
}

TransportProtocol LTcpTransport::getProtocol() const noexcept
{
    return TransportProtocol::TCP;
}

bool LTcpTransport::start()
{
    if (getState() == TransportState::Running) {
        return true;
    }

    setState(TransportState::Starting);

    if (!initializeServer()) {
        setState(TransportState::Error);
        return false;
    }

    m_running.store(true);
    m_sendThreadRunning.store(true);

    m_acceptThread = std::thread(&LTcpTransport::acceptThreadFunc, this);
    m_receiveThread = std::thread(&LTcpTransport::receiveThreadFunc, this);
    m_sendThread = std::thread(&LTcpTransport::sendThreadFunc, this);

    setState(TransportState::Running);
    return true;
}

void LTcpTransport::stop()
{
    const TransportState currentState = getState();
    if (currentState == TransportState::Stopped) {
        return;
    }

    setState(TransportState::Stopping);

    m_running.store(false);
    m_sendThreadRunning.store(false);
    m_sendCondition.notify_all();

    closeServer();
    disconnectAll();

    if (m_acceptThread.joinable()) {
        m_acceptThread.join();
    }
    if (m_receiveThread.joinable()) {
        m_receiveThread.join();
    }
    if (m_sendThread.joinable()) {
        m_sendThread.join();
    }

    {
        std::lock_guard<std::mutex> lock(m_sendQueueMutex);
        std::queue<SendTask> empty;
        std::swap(m_sendQueue, empty);
    }

    setState(TransportState::Stopped);
}

bool LTcpTransport::sendMessage(const LMessage& message)
{
    if (getState() != TransportState::Running) {
        setError(QStringLiteral("Transport is not running"));
        return false;
    }

    QHostAddress targetAddress;
    quint16 targetPort = 0;
    if (resolveRemoteFromConfig(targetAddress, targetPort)) {
        return sendMessageTo(message, targetAddress, targetPort);
    }

    quint64 onlyConnectionId = 0;
    if (!getSingleConnectionId(onlyConnectionId)) {
        setError(QStringLiteral("No default TCP remote endpoint configured"));
        return false;
    }

    LByteBuffer serialized = message.serialize();
    std::vector<uint8_t> bytes(serialized.data(), serialized.data() + serialized.size());
    return enqueueSendData(std::move(bytes), onlyConnectionId);
}

bool LTcpTransport::sendMessageTo(const LMessage& message,
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

    ConnectionPtr connection = findConnection(targetAddress, targetPort);
    if (!connection) {
        if (!connectToHost(targetAddress, targetPort)) {
            setError(QStringLiteral("Failed to connect to target endpoint"));
            return false;
        }
        connection = findConnection(targetAddress, targetPort);
        if (!connection) {
            setError(QStringLiteral("Connection not available after connect"));
            return false;
        }
    }

    LByteBuffer serialized = message.serialize();
    std::vector<uint8_t> bytes(serialized.data(), serialized.data() + serialized.size());
    return enqueueSendData(std::move(bytes), connection->connectionId);
}

bool LTcpTransport::broadcastMessage(const LMessage& message, quint16 broadcastPort)
{
    Q_UNUSED(broadcastPort)

    if (getState() != TransportState::Running) {
        setError(QStringLiteral("Transport is not running"));
        return false;
    }

    LByteBuffer serialized = message.serialize();
    std::vector<uint8_t> bytes(serialized.data(), serialized.data() + serialized.size());
    return enqueueSendData(std::move(bytes), 0);
}

bool LTcpTransport::connectToHost(const QHostAddress& address, quint16 port)
{
    if (address.isNull() || port == 0) {
        setError(QStringLiteral("Invalid remote endpoint"));
        return false;
    }

    if (!initializeSocketApi()) {
        setError(QStringLiteral("Socket subsystem initialization failed"));
        return false;
    }

    sockaddr_in remoteAddr {};
    if (!toSockAddr(address, port, remoteAddr)) {
        setError(QStringLiteral("Remote address must be valid IPv4"));
        return false;
    }

    SocketHandle socketFd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socketFd == kInvalidSocket) {
        setError(QStringLiteral("Failed to create TCP socket, error code=%1").arg(getLastSocketError()));
        return false;
    }

    const TransportConfig config = getConfig();
    if (config.receiveBufferSize > 0) {
        setSocketOptionInt(socketFd, SOL_SOCKET, SO_RCVBUF, config.receiveBufferSize);
    }
    if (config.sendBufferSize > 0) {
        setSocketOptionInt(socketFd, SOL_SOCKET, SO_SNDBUF, config.sendBufferSize);
    }

    if (::connect(socketFd, reinterpret_cast<sockaddr*>(&remoteAddr), sizeof(remoteAddr)) != 0) {
        closeNativeSocket(socketFd);
        setError(QStringLiteral("TCP connect failed, error code=%1").arg(getLastSocketError()));
        return false;
    }

    setSocketNonBlocking(socketFd, true);

    if (addConnection(socketFd, address, port) == 0) {
        closeNativeSocket(socketFd);
        return false;
    }

    return true;
}

void LTcpTransport::disconnect(quint64 connectionId)
{
    removeConnection(connectionId);
}

void LTcpTransport::disconnectAll()
{
    std::vector<quint64> allIds;
    {
        std::lock_guard<std::mutex> lock(m_connectionsMutex);
        allIds.reserve(m_connections.size());
        for (const auto& pair : m_connections) {
            allIds.push_back(pair.first);
        }
    }

    for (const quint64 id : allIds) {
        removeConnection(id);
    }
}

size_t LTcpTransport::getConnectionCount() const noexcept
{
    std::lock_guard<std::mutex> lock(m_connectionsMutex);
    return m_connections.size();
}

bool LTcpTransport::getConnectionStats(quint64 connectionId,
                                       uint64_t& bytesSent,
                                       uint64_t& bytesReceived) const
{
    std::lock_guard<std::mutex> lock(m_connectionsMutex);
    const auto it = m_connections.find(connectionId);
    if (it == m_connections.end()) {
        return false;
    }

    bytesSent = it->second->bytesSent.load();
    bytesReceived = it->second->bytesReceived.load();
    return true;
}

bool LTcpTransport::initializeServer()
{
    if (!initializeSocketApi()) {
        setError(QStringLiteral("Socket subsystem initialization failed"));
        return false;
    }

    const TransportConfig config = getConfig();

    SocketHandle socketFd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socketFd == kInvalidSocket) {
        setError(QStringLiteral("Failed to create TCP listen socket, error code=%1").arg(getLastSocketError()));
        return false;
    }

    if (config.reuseAddress) {
        setSocketOptionInt(socketFd, SOL_SOCKET, SO_REUSEADDR, 1);
    }

    sockaddr_in localAddr {};
    const QHostAddress bindAddress = config.bindAddress.isEmpty()
        ? QHostAddress::Any
        : QHostAddress(config.bindAddress);

    if (!toSockAddr(bindAddress, config.bindPort, localAddr)) {
        closeNativeSocket(socketFd);
        setError(QStringLiteral("Bind address must be valid IPv4"));
        return false;
    }

    if (::bind(socketFd, reinterpret_cast<sockaddr*>(&localAddr), sizeof(localAddr)) != 0) {
        closeNativeSocket(socketFd);
        setError(QStringLiteral("TCP bind failed, error code=%1").arg(getLastSocketError()));
        return false;
    }

    const int backlog = config.maxConnections > 0 ? config.maxConnections : 1;
    if (::listen(socketFd, backlog) != 0) {
        closeNativeSocket(socketFd);
        setError(QStringLiteral("TCP listen failed, error code=%1").arg(getLastSocketError()));
        return false;
    }

    setSocketNonBlocking(socketFd, true);

    sockaddr_in actualAddr {};
    SockLenType actualLen = static_cast<SockLenType>(sizeof(actualAddr));
    if (::getsockname(socketFd, reinterpret_cast<sockaddr*>(&actualAddr), &actualLen) == 0) {
        setBoundPort(ntohs(actualAddr.sin_port));
    } else {
        setBoundPort(config.bindPort);
    }

    m_listenSocket.store(toStoredSocket(socketFd));
    return true;
}

void LTcpTransport::closeServer()
{
    const std::intptr_t stored = m_listenSocket.exchange(kInvalidSocketStorage);
    if (stored != kInvalidSocketStorage) {
        const SocketHandle socketFd = toNativeSocket(stored);
        shutdownNativeSocket(socketFd);
        closeNativeSocket(socketFd);
    }
    setBoundPort(0);
}

void LTcpTransport::acceptThreadFunc()
{
    while (m_running.load()) {
        const SocketHandle listenSocket = toNativeSocket(m_listenSocket.load());
        if (listenSocket == kInvalidSocket) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(listenSocket, &readSet);

        timeval timeout {};
        timeout.tv_sec = 0;
        timeout.tv_usec = 200 * 1000;

        const int ready = ::select(static_cast<int>(listenSocket) + 1, &readSet, nullptr, nullptr, &timeout);
        if (!m_running.load()) {
            break;
        }

        if (ready <= 0) {
            continue;
        }

        sockaddr_in remoteAddr {};
        SockLenType remoteLen = static_cast<SockLenType>(sizeof(remoteAddr));
        SocketHandle clientSocket = ::accept(
            listenSocket,
            reinterpret_cast<sockaddr*>(&remoteAddr),
            &remoteLen
        );

        if (clientSocket == kInvalidSocket) {
            continue;
        }

        setSocketNonBlocking(clientSocket, true);
        const QHostAddress remoteAddress = fromSockAddr(remoteAddr);
        const quint16 remotePort = ntohs(remoteAddr.sin_port);
        if (addConnection(clientSocket, remoteAddress, remotePort) == 0) {
            closeNativeSocket(clientSocket);
        }
    }
}

void LTcpTransport::receiveThreadFunc()
{
    struct SocketEntry {
        quint64 connectionId;
        ConnectionPtr connection;
        SocketHandle socketFd;
    };

    std::vector<uint8_t> readBuffer(64 * 1024);

    while (m_running.load()) {
        std::vector<SocketEntry> entries;
        {
            std::lock_guard<std::mutex> lock(m_connectionsMutex);
            entries.reserve(m_connections.size());
            for (const auto& pair : m_connections) {
                const ConnectionPtr& connection = pair.second;
                std::lock_guard<std::mutex> connLock(connection->mutex);
                if (!connection->connected.load() || connection->socketFd == kInvalidSocket) {
                    continue;
                }
                entries.push_back({pair.first, connection, connection->socketFd});
            }
        }

        if (entries.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        fd_set readSet;
        FD_ZERO(&readSet);

        SocketHandle maxFd = 0;
        for (const SocketEntry& entry : entries) {
            FD_SET(entry.socketFd, &readSet);
            if (entry.socketFd > maxFd) {
                maxFd = entry.socketFd;
            }
        }

        timeval timeout {};
        timeout.tv_sec = 0;
        timeout.tv_usec = 200 * 1000;

        const int ready = ::select(static_cast<int>(maxFd) + 1, &readSet, nullptr, nullptr, &timeout);
        if (!m_running.load()) {
            break;
        }

        if (ready <= 0) {
            continue;
        }

        for (const SocketEntry& entry : entries) {
            if (!FD_ISSET(entry.socketFd, &readSet)) {
                continue;
            }

            const int recvBytes = ::recv(
                entry.socketFd,
                reinterpret_cast<char*>(readBuffer.data()),
                static_cast<int>(readBuffer.size()),
                0
            );

            if (recvBytes > 0) {
                {
                    std::lock_guard<std::mutex> connLock(entry.connection->mutex);
                    entry.connection->receiveBuffer.insert(
                        entry.connection->receiveBuffer.end(),
                        readBuffer.begin(),
                        readBuffer.begin() + recvBytes
                    );
                    entry.connection->bytesReceived.fetch_add(static_cast<uint64_t>(recvBytes));
                }
                processConnectionBuffer(entry.connection);
                continue;
            }

            if (recvBytes == 0) {
                removeConnection(entry.connectionId);
                continue;
            }

            const int errorCode = getLastSocketError();
            if (!isInterruptedOrWouldBlock(errorCode)) {
                removeConnection(entry.connectionId);
            }
        }
    }
}

void LTcpTransport::sendThreadFunc()
{
    while (m_sendThreadRunning.load() || !m_sendQueue.empty()) {
        SendTask task;
        {
            std::unique_lock<std::mutex> lock(m_sendQueueMutex);
            m_sendCondition.wait(lock, [this] {
                return !m_sendThreadRunning.load() || !m_sendQueue.empty();
            });

            if (m_sendQueue.empty()) {
                continue;
            }

            task = std::move(m_sendQueue.front());
            m_sendQueue.pop();
        }

        if (task.connectionId == 0) {
            std::vector<quint64> ids;
            {
                std::lock_guard<std::mutex> lock(m_connectionsMutex);
                ids.reserve(m_connections.size());
                for (const auto& pair : m_connections) {
                    ids.push_back(pair.first);
                }
            }
            for (const quint64 id : ids) {
                sendToConnection(id, task.data);
            }
        } else {
            sendToConnection(task.connectionId, task.data);
        }
    }
}

bool LTcpTransport::enqueueSendData(std::vector<uint8_t>&& data, quint64 connectionId)
{
    if (!m_sendThreadRunning.load()) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_sendQueueMutex);
        m_sendQueue.push(SendTask {std::move(data), connectionId});
    }
    m_sendCondition.notify_one();
    return true;
}

bool LTcpTransport::sendToConnection(quint64 connectionId, const std::vector<uint8_t>& data)
{
    ConnectionPtr connection;
    {
        std::lock_guard<std::mutex> lock(m_connectionsMutex);
        const auto it = m_connections.find(connectionId);
        if (it == m_connections.end()) {
            return false;
        }
        connection = it->second;
    }

    bool shouldRemove = false;
    {
        std::lock_guard<std::mutex> connLock(connection->mutex);
        if (!connection->connected.load() || connection->socketFd == kInvalidSocket) {
            shouldRemove = true;
        } else if (sendAll(connection->socketFd, data.data(), data.size())) {
            connection->bytesSent.fetch_add(static_cast<uint64_t>(data.size()));
            return true;
        } else {
            connection->connected.store(false);
            shutdownNativeSocket(connection->socketFd);
            closeNativeSocket(connection->socketFd);
            connection->socketFd = kInvalidSocket;
            shouldRemove = true;
        }
    }

    if (shouldRemove) {
        removeConnection(connectionId);
    }
    return false;
}

quint64 LTcpTransport::addConnection(std::intptr_t socketFdStored,
                                     const QHostAddress& remoteAddress,
                                     quint16 remotePort)
{
    const SocketHandle socketFd = toNativeSocket(socketFdStored);
    if (socketFd == kInvalidSocket) {
        return 0;
    }

    const TransportConfig config = getConfig();
    const size_t maxConnections = config.maxConnections > 0
        ? static_cast<size_t>(config.maxConnections)
        : static_cast<size_t>(1);

    const quint64 connectionId = m_nextConnectionId.fetch_add(1);
    const ConnectionPtr connection = std::make_shared<TcpConnection>(
        connectionId,
        socketFd,
        remoteAddress,
        remotePort
    );

    {
        std::lock_guard<std::mutex> lock(m_connectionsMutex);
        if (m_connections.size() >= maxConnections) {
            return 0;
        }
        m_connections.emplace(connectionId, connection);
    }

    return connectionId;
}

void LTcpTransport::removeConnection(quint64 connectionId)
{
    ConnectionPtr connection;
    {
        std::lock_guard<std::mutex> lock(m_connectionsMutex);
        const auto it = m_connections.find(connectionId);
        if (it == m_connections.end()) {
            return;
        }
        connection = it->second;
        m_connections.erase(it);
    }

    std::lock_guard<std::mutex> connLock(connection->mutex);
    connection->connected.store(false);
    if (connection->socketFd != kInvalidSocket) {
        shutdownNativeSocket(connection->socketFd);
        closeNativeSocket(connection->socketFd);
        connection->socketFd = kInvalidSocket;
    }
}

LTcpTransport::ConnectionPtr LTcpTransport::findConnection(const QHostAddress& address, quint16 port)
{
    std::lock_guard<std::mutex> lock(m_connectionsMutex);
    for (const auto& pair : m_connections) {
        const ConnectionPtr& connection = pair.second;
        if (connection->remoteAddress == address &&
            connection->remotePort == port &&
            connection->connected.load()) {
            return connection;
        }
    }
    return nullptr;
}

bool LTcpTransport::resolveRemoteFromConfig(QHostAddress& targetAddress, quint16& targetPort) const
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

bool LTcpTransport::getSingleConnectionId(quint64& connectionId) const
{
    std::lock_guard<std::mutex> lock(m_connectionsMutex);
    if (m_connections.size() != 1) {
        return false;
    }
    connectionId = m_connections.begin()->first;
    return true;
}

void LTcpTransport::processConnectionBuffer(const ConnectionPtr& connection)
{
    std::vector<LMessage> readyMessages;
    {
        std::lock_guard<std::mutex> connLock(connection->mutex);
        std::vector<uint8_t>& buffer = connection->receiveBuffer;

        while (true) {
            if (buffer.size() < LMessageHeader::HEADER_SIZE) {
                break;
            }

            const uint32_t totalSize = readLeUInt32(buffer.data());
            if (totalSize < LMessageHeader::HEADER_SIZE || totalSize > kMaxTcpMessageSize) {
                buffer.erase(buffer.begin());
                continue;
            }

            if (buffer.size() < static_cast<size_t>(totalSize)) {
                break;
            }

            LMessage message;
            const bool valid = message.deserialize(buffer.data(), static_cast<size_t>(totalSize));
            buffer.erase(buffer.begin(), buffer.begin() + totalSize);
            if (!valid) {
                continue;
            }

            message.setSenderAddress(connection->remoteAddress);
            message.setSenderPort(connection->remotePort);
            readyMessages.push_back(std::move(message));
        }
    }

    for (const LMessage& message : readyMessages) {
        notifyReceiveCallback(message, message.getSenderAddress(), message.getSenderPort());
    }
}

} // namespace LDdsFramework
