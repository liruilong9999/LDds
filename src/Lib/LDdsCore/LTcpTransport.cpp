#include "LTcpTransport.h"
#include "LMessage.h"

#include <QAbstractSocket>
#include <QByteArray>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>

#include <chrono>
#include <thread>

namespace LDdsFramework {
namespace {

constexpr uint32_t kMaxTcpMessageSize = 10U * 1024U * 1024U;

uint32_t readLeUInt32(const uint8_t* data) noexcept
{
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

bool sendAll(QTcpSocket& socket, const uint8_t* data, size_t size)
{
    size_t sentBytes = 0;
    int idleLoops = 0;

    while (sentBytes < size) {
        const qint64 chunk = socket.write(
            reinterpret_cast<const char*>(data + sentBytes),
            static_cast<qint64>(size - sentBytes)
        );

        if (chunk < 0) {
            return false;
        }

        if (chunk == 0) {
            ++idleLoops;
            if (idleLoops > 5) {
                return false;
            }
            if (!socket.waitForBytesWritten(200)) {
                if (socket.state() != QAbstractSocket::ConnectedState) {
                    return false;
                }
            }
            continue;
        }

        idleLoops = 0;
        sentBytes += static_cast<size_t>(chunk);

        if (socket.bytesToWrite() > 0) {
            if (!socket.waitForBytesWritten(1000) &&
                socket.state() != QAbstractSocket::ConnectedState) {
                return false;
            }
        }
    }

    return true;
}

} // namespace

struct LTcpTransport::TcpConnection {
    TcpConnection(
        quint64 connectionIdValue,
        const std::shared_ptr<QTcpSocket>& socketValue,
        const QHostAddress& remoteAddressValue,
        quint16 remotePortValue)
        : connectionId(connectionIdValue)
        , socket(socketValue)
        , remoteAddress(remoteAddressValue)
        , remotePort(remotePortValue)
        , receiveBuffer()
        , bytesSent(0)
        , bytesReceived(0)
        , connected(true)
        , mutex()
    {
    }

    quint64 connectionId;
    std::shared_ptr<QTcpSocket> socket;
    QHostAddress remoteAddress;
    quint16 remotePort;
    std::vector<uint8_t> receiveBuffer;
    std::atomic<uint64_t> bytesSent;
    std::atomic<uint64_t> bytesReceived;
    std::atomic<bool> connected;
    mutable std::mutex mutex;
};

LTcpTransport::LTcpTransport()
    : m_server()
    , m_serverMutex()
    , m_connections()
    , m_connectionsMutex()
    , m_acceptThread()
    , m_receiveThread()
    , m_sendThread()
    , m_sendQueue()
    , m_sendQueueMutex()
    , m_sendCondition()
    , m_running(false)
    , m_sendThreadRunning(false)
    , m_nextConnectionId(1)
    , m_networkThread(nullptr)
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

    m_receiveThread = std::thread(&LTcpTransport::receiveThreadFunc, this);

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

    if (m_receiveThread.joinable()) {
        m_receiveThread.join();
    }

    m_networkThread.store(nullptr);
    closeServer();
    disconnectAll();

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

    const LByteBuffer serialized = message.serialize();
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

    const LByteBuffer serialized = message.serialize();
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

    const LByteBuffer serialized = message.serialize();
    std::vector<uint8_t> bytes(serialized.data(), serialized.data() + serialized.size());
    return enqueueSendData(std::move(bytes), 0);
}

bool LTcpTransport::connectToHost(const QHostAddress& address, quint16 port)
{
    if (address.isNull() || port == 0) {
        setError(QStringLiteral("Invalid remote endpoint"));
        return false;
    }

    auto socket = std::make_shared<QTcpSocket>();

    const TransportConfig config = getConfig();
    if (config.receiveBufferSize > 0) {
        socket->setSocketOption(
            QAbstractSocket::ReceiveBufferSizeSocketOption,
            config.receiveBufferSize
        );
    }
    if (config.sendBufferSize > 0) {
        socket->setSocketOption(
            QAbstractSocket::SendBufferSizeSocketOption,
            config.sendBufferSize
        );
    }

    socket->connectToHost(address, port);
    if (!socket->waitForConnected(3000)) {
        setError(QStringLiteral("TCP connect failed: %1").arg(socket->errorString()));
        return false;
    }

    QThread* networkThread = m_networkThread.load();
    for (int i = 0; i < 20 && networkThread == nullptr; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        networkThread = m_networkThread.load();
    }
    if (networkThread != nullptr && socket->thread() != networkThread) {
        socket->moveToThread(networkThread);
    }

    if (addConnection(socket, address, port) == 0) {
        socket->disconnectFromHost();
        socket->close();
        setError(QStringLiteral("Connection limit reached"));
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
    const TransportConfig config = getConfig();

    QHostAddress bindAddress = config.bindAddress.isEmpty()
        ? QHostAddress::AnyIPv4
        : QHostAddress(config.bindAddress);
    if (bindAddress.isNull()) {
        setError(QStringLiteral("Bind address must be valid"));
        return false;
    }

    auto server = std::make_shared<QTcpServer>();
    if (!server->listen(bindAddress, config.bindPort)) {
        setError(QStringLiteral("TCP listen failed: %1").arg(server->errorString()));
        return false;
    }

    setBoundPort(server->serverPort());
    {
        std::lock_guard<std::mutex> lock(m_serverMutex);
        m_server = std::move(server);
    }

    return true;
}

void LTcpTransport::closeServer()
{
    std::shared_ptr<QTcpServer> server;
    {
        std::lock_guard<std::mutex> lock(m_serverMutex);
        server = std::move(m_server);
    }

    if (server) {
        server->close();
    }
    setBoundPort(0);
}

void LTcpTransport::acceptThreadFunc()
{
    // The TCP accept path is integrated into receiveThreadFunc() so that
    // all QTcpServer/QTcpSocket operations run on a single thread.
}

void LTcpTransport::receiveThreadFunc()
{
    struct SocketEntry {
        quint64 connectionId;
        ConnectionPtr connection;
    };

    m_networkThread.store(QThread::currentThread());

    std::shared_ptr<QTcpServer> server;
    {
        std::lock_guard<std::mutex> lock(m_serverMutex);
        server = m_server;
    }
    if (server && server->thread() != QThread::currentThread()) {
        server->moveToThread(QThread::currentThread());
    }

    while (m_running.load()) {
        if (server && server->waitForNewConnection(5)) {
            while (server->hasPendingConnections()) {
                QTcpSocket* pending = server->nextPendingConnection();
                if (!pending) {
                    break;
                }

                pending->setParent(nullptr);
                std::shared_ptr<QTcpSocket> socket(pending);

                const TransportConfig config = getConfig();
                if (config.receiveBufferSize > 0) {
                    socket->setSocketOption(
                        QAbstractSocket::ReceiveBufferSizeSocketOption,
                        config.receiveBufferSize
                    );
                }
                if (config.sendBufferSize > 0) {
                    socket->setSocketOption(
                        QAbstractSocket::SendBufferSizeSocketOption,
                        config.sendBufferSize
                    );
                }

                if (addConnection(socket, socket->peerAddress(), socket->peerPort()) == 0) {
                    socket->disconnectFromHost();
                    socket->close();
                }
            }
        }

        {
            std::queue<SendTask> localQueue;
            {
                std::lock_guard<std::mutex> lock(m_sendQueueMutex);
                std::swap(localQueue, m_sendQueue);
            }

            while (!localQueue.empty()) {
                SendTask task = std::move(localQueue.front());
                localQueue.pop();

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

        std::vector<SocketEntry> entries;
        {
            std::lock_guard<std::mutex> lock(m_connectionsMutex);
            entries.reserve(m_connections.size());
            for (const auto& pair : m_connections) {
                entries.push_back({pair.first, pair.second});
            }
        }

        if (entries.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        bool hadData = false;

        for (const SocketEntry& entry : entries) {
            QByteArray incoming;
            bool shouldRemove = false;

            {
                std::lock_guard<std::mutex> connLock(entry.connection->mutex);
                if (!entry.connection->connected.load() || !entry.connection->socket) {
                    shouldRemove = true;
                } else {
                    QTcpSocket& socket = *entry.connection->socket;

                    if (socket.bytesAvailable() <= 0) {
                        socket.waitForReadyRead(10);
                    }

                    if (socket.bytesAvailable() > 0) {
                        incoming = socket.readAll();
                        while (socket.bytesAvailable() > 0) {
                            incoming.append(socket.readAll());
                        }
                    } else if (socket.state() != QAbstractSocket::ConnectedState) {
                        shouldRemove = true;
                    }
                }
            }

            if (shouldRemove) {
                removeConnection(entry.connectionId);
                continue;
            }

            if (incoming.isEmpty()) {
                continue;
            }

            hadData = true;
            {
                std::lock_guard<std::mutex> connLock(entry.connection->mutex);
                entry.connection->receiveBuffer.insert(
                    entry.connection->receiveBuffer.end(),
                    reinterpret_cast<const uint8_t*>(incoming.constData()),
                    reinterpret_cast<const uint8_t*>(incoming.constData()) + incoming.size()
                );
                entry.connection->bytesReceived.fetch_add(static_cast<uint64_t>(incoming.size()));
            }

            processConnectionBuffer(entry.connection);
        }

        if (!hadData) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    m_networkThread.store(nullptr);
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
        if (!connection->connected.load() || !connection->socket) {
            shouldRemove = true;
        } else if (connection->socket->state() != QAbstractSocket::ConnectedState) {
            shouldRemove = true;
        } else if (sendAll(*connection->socket, data.data(), data.size())) {
            connection->bytesSent.fetch_add(static_cast<uint64_t>(data.size()));
            return true;
        } else {
            connection->connected.store(false);
            connection->socket->disconnectFromHost();
            connection->socket->close();
            shouldRemove = true;
        }
    }

    if (shouldRemove) {
        removeConnection(connectionId);
    }
    return false;
}

quint64 LTcpTransport::addConnection(const std::shared_ptr<QTcpSocket>& socket,
                                     const QHostAddress& remoteAddress,
                                     quint16 remotePort)
{
    if (!socket) {
        return 0;
    }

    const TransportConfig config = getConfig();
    const size_t maxConnections = config.maxConnections > 0
        ? static_cast<size_t>(config.maxConnections)
        : static_cast<size_t>(1);

    const quint64 connectionId = m_nextConnectionId.fetch_add(1);
    const ConnectionPtr connection = std::make_shared<TcpConnection>(
        connectionId,
        socket,
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
    if (connection->socket) {
        connection->socket->disconnectFromHost();
        connection->socket->close();
        connection->socket.reset();
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
            if (buffer.size() < LMessageHeader::LEGACY_HEADER_SIZE) {
                break;
            }

            const uint32_t totalSize = readLeUInt32(buffer.data());
            if (totalSize < LMessageHeader::LEGACY_HEADER_SIZE || totalSize > kMaxTcpMessageSize) {
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
