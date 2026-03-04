#include "LTcpTransport.h"
#include "LMessage.h"

#include <QAbstractSocket>
#include <QByteArray>
#include <QDebug>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>

#include <chrono>
#include <cmath>
#include <limits>
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
        quint16 remotePortValue,
        const QString& endpointKeyValue)
        : connectionId(connectionIdValue)
        , socket(socketValue)
        , remoteAddress(remoteAddressValue)
        , remotePort(remotePortValue)
        , endpointKey(endpointKeyValue)
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
    QString endpointKey;
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
    , m_endpointStates()
    , m_endpointMutex()
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
    , m_dropCount(0)
    , m_lastQueueDropLogAt(std::chrono::steady_clock::time_point::min())
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

    QHostAddress remoteAddress;
    quint16 remotePort = 0;
    if (resolveRemoteFromConfig(remoteAddress, remotePort)) {
        ensureEndpointEntry(remoteAddress, remotePort);
        if (shouldAutoReconnect()) {
            (void)connectToHost(remoteAddress, remotePort);
        }
    }

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
        m_sendQueue.clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_endpointMutex);
        m_endpointStates.clear();
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

    ConnectionPtr connection = findConnectionById(onlyConnectionId);
    if (!connection) {
        setError(QStringLiteral("No active single TCP connection"));
        return false;
    }

    const LByteBuffer serialized = message.serialize();
    std::vector<uint8_t> bytes(serialized.data(), serialized.data() + serialized.size());
    return enqueueSendData(
        std::move(bytes),
        onlyConnectionId,
        connection->remoteAddress,
        connection->remotePort,
        false);
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

    ensureEndpointEntry(targetAddress, targetPort);

    ConnectionPtr connection = findConnection(targetAddress, targetPort);
    if (!connection) {
        const bool connectedNow = connectToHost(targetAddress, targetPort);
        if (!connectedNow && !shouldAutoReconnect()) {
            setError(QStringLiteral("Failed to connect target endpoint and autoReconnect is disabled"));
            return false;
        }
        connection = findConnection(targetAddress, targetPort);
    }

    const LByteBuffer serialized = message.serialize();
    std::vector<uint8_t> bytes(serialized.data(), serialized.data() + serialized.size());
    const quint64 connectionId = connection ? connection->connectionId : 0;
    return enqueueSendData(std::move(bytes), connectionId, targetAddress, targetPort, false);
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
    return enqueueSendData(std::move(bytes), 0, QHostAddress(), 0, true);
}

bool LTcpTransport::connectToHost(const QHostAddress& address, quint16 port)
{
    if (address.isNull() || port == 0) {
        setError(QStringLiteral("Invalid remote endpoint"));
        return false;
    }

    const QString endpointKey = makeEndpointKey(address, port);
    const auto now = std::chrono::steady_clock::now();

    quint64 existingConnectionId = 0;
    {
        std::lock_guard<std::mutex> lock(m_endpointMutex);
        EndpointStateEntry& entry = m_endpointStates[endpointKey];
        entry.address = address;
        entry.port = port;
        if (entry.reconnectDelayMs <= 0) {
            entry.reconnectDelayMs = getReconnectMinMs();
        }

        if (entry.state == EndpointConnectionState::Connected && entry.connectionId != 0) {
            existingConnectionId = entry.connectionId;
        } else if (entry.state == EndpointConnectionState::Connecting) {
            return true;
        } else if (entry.state == EndpointConnectionState::Backoff && now < entry.nextRetryAt) {
            return false;
        }

        entry.state = EndpointConnectionState::Connecting;
        entry.lastStateChangeAt = now;
    }

    if (existingConnectionId != 0) {
        ConnectionPtr existing = findConnectionById(existingConnectionId);
        if (existing && existing->connected.load()) {
            return true;
        }
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
        const QString errorText = QStringLiteral("TCP connect failed: %1").arg(socket->errorString());
        markEndpointFailure(endpointKey, errorText);
        setError(errorText);
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

    const quint64 connectionId = addConnection(socket, address, port, endpointKey);
    if (connectionId == 0) {
        socket->disconnectFromHost();
        socket->close();
        const QString errorText = QStringLiteral("Connection limit reached");
        markEndpointFailure(endpointKey, errorText);
        setError(errorText);
        return false;
    }

    markEndpointConnected(endpointKey, connectionId);
    return true;
}

void LTcpTransport::disconnect(quint64 connectionId)
{
    removeConnection(connectionId, QStringLiteral("manual disconnect"), false);
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
        removeConnection(id, QStringLiteral("disconnect all"), false);
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
    ConnectionPtr connection = findConnectionById(connectionId);
    if (!connection) {
        return false;
    }

    bytesSent = connection->bytesSent.load();
    bytesReceived = connection->bytesReceived.load();
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

                const QString endpointKey = makeEndpointKey(socket->peerAddress(), socket->peerPort());
                if (addConnection(socket, socket->peerAddress(), socket->peerPort(), endpointKey) == 0) {
                    socket->disconnectFromHost();
                    socket->close();
                }
            }
        }

        processReconnects();
        processSendQueue();

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
                removeConnection(entry.connectionId, QStringLiteral("socket state changed"), true);
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
    // Send processing is handled in receiveThreadFunc() to keep socket operations
    // on a single thread.
}

bool LTcpTransport::enqueueSendData(
    std::vector<uint8_t>&& data,
    quint64 connectionId,
    const QHostAddress& targetAddress,
    quint16 targetPort,
    bool broadcast)
{
    if (!m_running.load()) {
        return false;
    }

    SendTask task;
    task.data = std::move(data);
    task.connectionId = connectionId;
    task.targetAddress = targetAddress;
    task.targetPort = targetPort;
    task.broadcast = broadcast;
    task.notBefore = std::chrono::steady_clock::now();
    task.attempts = 0;
    if (!broadcast && !targetAddress.isNull() && targetPort != 0) {
        task.endpointKey = makeEndpointKey(targetAddress, targetPort);
    }

    return pushTaskWithPolicy(std::move(task), false);
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
        removeConnection(connectionId, QStringLiteral("write failed"), true);
    }
    return false;
}

quint64 LTcpTransport::addConnection(const std::shared_ptr<QTcpSocket>& socket,
                                     const QHostAddress& remoteAddress,
                                     quint16 remotePort,
                                     const QString& endpointKey)
{
    if (!socket) {
        return 0;
    }

    const TransportConfig config = getConfig();
    const size_t maxConnections = config.maxConnections > 0
        ? static_cast<size_t>(config.maxConnections)
        : static_cast<size_t>(1);

    {
        std::lock_guard<std::mutex> lock(m_connectionsMutex);
        if (!endpointKey.isEmpty()) {
            for (const auto& pair : m_connections) {
                const ConnectionPtr& existing = pair.second;
                if (existing->endpointKey == endpointKey && existing->connected.load()) {
                    return existing->connectionId;
                }
            }
        }
        if (m_connections.size() >= maxConnections) {
            return 0;
        }

        const quint64 connectionId = m_nextConnectionId.fetch_add(1);
        const ConnectionPtr connection = std::make_shared<TcpConnection>(
            connectionId,
            socket,
            remoteAddress,
            remotePort,
            endpointKey
        );
        m_connections.emplace(connectionId, connection);
        if (!endpointKey.isEmpty()) {
            markEndpointConnected(endpointKey, connectionId);
        }
        return connectionId;
    }
}

void LTcpTransport::removeConnection(
    quint64 connectionId,
    const QString& reason,
    bool scheduleReconnect)
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

    {
        std::lock_guard<std::mutex> connLock(connection->mutex);
        connection->connected.store(false);
        if (connection->socket) {
            connection->socket->disconnectFromHost();
            connection->socket->close();
            connection->socket.reset();
        }
    }

    if (connection->endpointKey.isEmpty()) {
        return;
    }

    if (scheduleReconnect) {
        markEndpointFailure(connection->endpointKey, reason);
    } else {
        std::lock_guard<std::mutex> lock(m_endpointMutex);
        const auto endpointIt = m_endpointStates.find(connection->endpointKey);
        if (endpointIt != m_endpointStates.end()) {
            endpointIt->second.connectionId = 0;
            endpointIt->second.state = EndpointConnectionState::Disconnected;
            endpointIt->second.lastStateChangeAt = std::chrono::steady_clock::now();
            endpointIt->second.lastError = reason;
        }
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

LTcpTransport::ConnectionPtr LTcpTransport::findConnectionById(quint64 connectionId) const
{
    std::lock_guard<std::mutex> lock(m_connectionsMutex);
    const auto it = m_connections.find(connectionId);
    if (it == m_connections.end()) {
        return nullptr;
    }
    return it->second;
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

void LTcpTransport::processSendQueue()
{
    std::vector<SendTask> tasks;
    {
        std::lock_guard<std::mutex> lock(m_sendQueueMutex);
        if (m_sendQueue.empty()) {
            return;
        }
        tasks.reserve(m_sendQueue.size());
        while (!m_sendQueue.empty()) {
            tasks.push_back(std::move(m_sendQueue.front()));
            m_sendQueue.pop_front();
        }
    }

    for (SendTask& task : tasks) {
        if (!m_running.load()) {
            break;
        }

        const auto now = std::chrono::steady_clock::now();
        if (task.notBefore > now) {
            (void)pushTaskWithPolicy(std::move(task), false);
            continue;
        }

        if (task.broadcast) {
            std::vector<quint64> ids;
            {
                std::lock_guard<std::mutex> lock(m_connectionsMutex);
                ids.reserve(m_connections.size());
                for (const auto& pair : m_connections) {
                    ids.push_back(pair.first);
                }
            }

            for (const quint64 id : ids) {
                (void)sendToConnection(id, task.data);
            }
            continue;
        }

        quint64 connectionId = task.connectionId;
        if (connectionId != 0) {
            ConnectionPtr active = findConnectionById(connectionId);
            if (!active || !active->connected.load()) {
                connectionId = 0;
            }
        }

        if (connectionId == 0 && !task.endpointKey.isEmpty()) {
            (void)getEndpointConnectionId(task.endpointKey, connectionId);
        }

        if (connectionId == 0 && !task.targetAddress.isNull() && task.targetPort != 0) {
            const QString endpointKey = makeEndpointKey(task.targetAddress, task.targetPort);
            if (canAttemptConnectNow(endpointKey)) {
                (void)connectToHost(task.targetAddress, task.targetPort);
                (void)getEndpointConnectionId(endpointKey, connectionId);
            }
        }

        if (connectionId == 0) {
            if (shouldAutoReconnect()) {
                task.connectionId = 0;
                task.attempts += 1;
                task.notBefore = std::chrono::steady_clock::now() +
                                 std::chrono::milliseconds(getReconnectMinMs());
                (void)pushTaskWithPolicy(std::move(task), false);
            } else {
                setError(QStringLiteral("No connection available for queued TCP message"));
            }
            continue;
        }

        if (sendToConnection(connectionId, task.data)) {
            continue;
        }

        if (shouldAutoReconnect()) {
            task.connectionId = 0;
            task.attempts += 1;
            task.notBefore = std::chrono::steady_clock::now() +
                             std::chrono::milliseconds(getReconnectMinMs());
            (void)pushTaskWithPolicy(std::move(task), false);
            continue;
        }

        setError(QStringLiteral("send queued TCP message failed"));
    }
}

void LTcpTransport::processReconnects()
{
    if (!shouldAutoReconnect()) {
        return;
    }

    std::vector<std::pair<QHostAddress, quint16>> candidates;
    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(m_endpointMutex);
        for (auto& pair : m_endpointStates) {
            EndpointStateEntry& entry = pair.second;
            if (entry.address.isNull() || entry.port == 0) {
                continue;
            }
            if (entry.state == EndpointConnectionState::Connected ||
                entry.state == EndpointConnectionState::Connecting) {
                continue;
            }
            if (entry.state == EndpointConnectionState::Backoff && now < entry.nextRetryAt) {
                continue;
            }
            entry.state = EndpointConnectionState::Disconnected;
            candidates.push_back({entry.address, entry.port});
        }
    }

    for (const auto& candidate : candidates) {
        (void)connectToHost(candidate.first, candidate.second);
    }
}

QString LTcpTransport::makeEndpointKey(const QHostAddress& address, quint16 port)
{
    return QStringLiteral("%1:%2").arg(address.toString()).arg(port);
}

bool LTcpTransport::shouldAutoReconnect() const
{
    return getConfig().autoReconnect;
}

int LTcpTransport::getReconnectMinMs() const
{
    return std::max(20, getConfig().reconnectMinMs);
}

int LTcpTransport::getReconnectMaxMs() const
{
    const int minMs = getReconnectMinMs();
    return std::max(minMs, getConfig().reconnectMaxMs);
}

double LTcpTransport::getReconnectMultiplier() const
{
    const double multiplier = getConfig().reconnectMultiplier;
    return multiplier < 1.0 ? 1.0 : multiplier;
}

int LTcpTransport::computeNextBackoffMs(int currentDelayMs) const
{
    const int minMs = getReconnectMinMs();
    const int maxMs = getReconnectMaxMs();
    if (currentDelayMs <= 0) {
        return minMs;
    }

    const double scaled = std::ceil(static_cast<double>(currentDelayMs) * getReconnectMultiplier());
    if (scaled >= static_cast<double>(maxMs)) {
        return maxMs;
    }
    return std::max(minMs, static_cast<int>(scaled));
}

void LTcpTransport::ensureEndpointEntry(const QHostAddress& address, quint16 port)
{
    if (address.isNull() || port == 0) {
        return;
    }

    const QString endpointKey = makeEndpointKey(address, port);
    std::lock_guard<std::mutex> lock(m_endpointMutex);
    EndpointStateEntry& entry = m_endpointStates[endpointKey];
    entry.address = address;
    entry.port = port;
    if (entry.reconnectDelayMs <= 0) {
        entry.reconnectDelayMs = getReconnectMinMs();
    }
}

void LTcpTransport::markEndpointConnected(const QString& endpointKey, quint64 connectionId)
{
    if (endpointKey.isEmpty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_endpointMutex);
    auto it = m_endpointStates.find(endpointKey);
    if (it == m_endpointStates.end()) {
        return;
    }

    EndpointStateEntry& entry = it->second;
    entry.state = EndpointConnectionState::Connected;
    entry.connectionId = connectionId;
    entry.reconnectDelayMs = getReconnectMinMs();
    entry.nextRetryAt = std::chrono::steady_clock::now();
    entry.lastStateChangeAt = entry.nextRetryAt;
    entry.failureCount = 0;
    entry.lastError.clear();
}

void LTcpTransport::markEndpointFailure(const QString& endpointKey, const QString& error)
{
    if (endpointKey.isEmpty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_endpointMutex);
    auto it = m_endpointStates.find(endpointKey);
    if (it == m_endpointStates.end()) {
        return;
    }

    EndpointStateEntry& entry = it->second;
    entry.connectionId = 0;
    entry.lastError = error;
    entry.failureCount += 1;
    entry.lastStateChangeAt = std::chrono::steady_clock::now();

    if (!shouldAutoReconnect()) {
        entry.state = EndpointConnectionState::Disconnected;
        entry.nextRetryAt = entry.lastStateChangeAt;
        return;
    }

    entry.reconnectDelayMs = computeNextBackoffMs(entry.reconnectDelayMs);
    entry.state = EndpointConnectionState::Backoff;
    entry.nextRetryAt = entry.lastStateChangeAt + std::chrono::milliseconds(entry.reconnectDelayMs);
}

bool LTcpTransport::getEndpointConnectionId(const QString& endpointKey, quint64& connectionId) const
{
    if (endpointKey.isEmpty()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_endpointMutex);
    const auto it = m_endpointStates.find(endpointKey);
    if (it == m_endpointStates.end()) {
        return false;
    }
    if (it->second.state != EndpointConnectionState::Connected || it->second.connectionId == 0) {
        return false;
    }

    connectionId = it->second.connectionId;
    return true;
}

bool LTcpTransport::canAttemptConnectNow(const QString& endpointKey) const
{
    if (endpointKey.isEmpty()) {
        return false;
    }

    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(m_endpointMutex);
    const auto it = m_endpointStates.find(endpointKey);
    if (it == m_endpointStates.end()) {
        return false;
    }

    if (it->second.state == EndpointConnectionState::Connected ||
        it->second.state == EndpointConnectionState::Connecting) {
        return false;
    }
    if (it->second.state == EndpointConnectionState::Backoff && now < it->second.nextRetryAt) {
        return false;
    }

    return true;
}

bool LTcpTransport::pushTaskWithPolicy(SendTask&& task, bool front)
{
    const TransportConfig config = getConfig();
    const int maxPending = (config.maxPendingMessages > 0)
        ? config.maxPendingMessages
        : std::numeric_limits<int>::max();

    std::lock_guard<std::mutex> lock(m_sendQueueMutex);
    if (static_cast<int>(m_sendQueue.size()) >= maxPending) {
        const uint64_t dropped = m_dropCount.fetch_add(1) + 1;
        if (config.sendQueueOverflowPolicy == SendQueueOverflowPolicy::DropOldest) {
            m_sendQueue.pop_front();
            logQueueDrop("drop_oldest", m_sendQueue.size(), dropped);
        } else if (config.sendQueueOverflowPolicy == SendQueueOverflowPolicy::DropNewest) {
            logQueueDrop("drop_newest", m_sendQueue.size(), dropped);
            return false;
        } else {
            setError(QStringLiteral("send queue overflow (fail-fast)"));
            logQueueDrop("fail_fast", m_sendQueue.size(), dropped);
            return false;
        }
    }

    if (front) {
        m_sendQueue.push_front(std::move(task));
    } else {
        m_sendQueue.push_back(std::move(task));
    }
    return true;
}

void LTcpTransport::logQueueDrop(const char* reason, size_t queueSize, uint64_t dropCount) const
{
    const auto now = std::chrono::steady_clock::now();
    if (m_lastQueueDropLogAt != std::chrono::steady_clock::time_point::min() &&
        (now - m_lastQueueDropLogAt) < std::chrono::milliseconds(200)) {
        return;
    }
    m_lastQueueDropLogAt = now;

    qWarning() << "[tcp] send queue overflow"
               << "reason=" << reason
               << "queueSize=" << static_cast<qulonglong>(queueSize)
               << "dropCount=" << static_cast<qulonglong>(dropCount);
}

} // namespace LDdsFramework
