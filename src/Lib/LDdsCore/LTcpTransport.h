#ifndef LTCPTRANSPORT_H
#define LTCPTRANSPORT_H

/**
 * @file LTcpTransport.h
 * @brief TCP 传输实现定义（连接管理、发送队列与重连状态机）。
 */

#include "ITransport.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class QTcpServer;
class QTcpSocket;
class QThread;

namespace LDdsFramework {

/**
 * @class LTcpTransport
 * @brief 基于 Qt TCP 套接字的传输实现。
 *
 * 支持服务端监听、客户端主动连接、单播发送与连接广播发送，
 * 并通过后台线程完成连接接入、收包解析和发送队列处理。
 */
class LDDSCORE_EXPORT LTcpTransport : public ITransport
{
public:
    /**
     * @brief 构造函数。
     */
    LTcpTransport();
    /**
     * @brief 析构函数，内部会自动 stop()。
     */
    ~LTcpTransport() override;

    /**
     * @brief 获取传输协议类型。
     * @return 固定返回 TransportProtocol::TCP。
     */
    TransportProtocol getProtocol() const noexcept override;
    /**
     * @brief 启动传输层并创建后台线程。
     */
    bool start() override;
    /**
     * @brief 停止传输层并回收连接资源。
     */
    void stop() override;

    /**
     * @brief 发送消息到默认远端或唯一连接。
     */
    bool sendMessage(const LMessage& message) override;
    /**
     * @brief 发送消息到指定远端（不存在连接则尝试建立）。
     */
    bool sendMessageTo(const LMessage& message,
                       const QHostAddress& targetAddress,
                       quint16 targetPort);
    /**
     * @brief 将消息广播到所有已建立连接。
     */
    bool broadcastMessage(const LMessage& message, quint16 broadcastPort) override;

    /**
     * @brief 主动连接远端主机。
     */
    bool connectToHost(const QHostAddress& address, quint16 port);
    /**
     * @brief 断开指定连接。
     */
    void disconnect(quint64 connectionId);
    /**
     * @brief 断开全部连接。
     */
    void disconnectAll();
    /**
     * @brief 获取当前连接数。
     */
    size_t getConnectionCount() const noexcept;
    /**
     * @brief 查询指定连接的收发字节统计。
     */
    bool getConnectionStats(quint64 connectionId, uint64_t& bytesSent, uint64_t& bytesReceived) const;
    size_t getPendingQueueSize() const noexcept;
    uint64_t getQueueDropCount() const noexcept;

private:
    struct TcpConnection;
    using ConnectionPtr = std::shared_ptr<TcpConnection>;

    enum class EndpointConnectionState {
        Disconnected = 0,
        Connecting = 1,
        Connected = 2,
        Backoff = 3
    };

    struct EndpointStateEntry {
        QHostAddress address;
        quint16 port;
        EndpointConnectionState state;
        quint64 connectionId;
        int reconnectDelayMs;
        std::chrono::steady_clock::time_point nextRetryAt;
        std::chrono::steady_clock::time_point lastStateChangeAt;
        uint64_t failureCount;
        QString lastError;

        EndpointStateEntry()
            : address()
            , port(0)
            , state(EndpointConnectionState::Disconnected)
            , connectionId(0)
            , reconnectDelayMs(0)
            , nextRetryAt(std::chrono::steady_clock::now())
            , lastStateChangeAt(std::chrono::steady_clock::now())
            , failureCount(0)
            , lastError()
        {
        }
    };

    struct SendTask {
        std::vector<uint8_t> data;
        quint64 connectionId;
        QString endpointKey;
        QHostAddress targetAddress;
        quint16 targetPort;
        bool broadcast;
        std::chrono::steady_clock::time_point notBefore;
        uint32_t attempts;

        SendTask()
            : data()
            , connectionId(0)
            , endpointKey()
            , targetAddress()
            , targetPort(0)
            , broadcast(false)
            , notBefore(std::chrono::steady_clock::now())
            , attempts(0)
        {
        }
    };

private:
    bool initializeServer();
    void closeServer();

    void acceptThreadFunc();
    void receiveThreadFunc();
    void sendThreadFunc();

    bool enqueueSendData(std::vector<uint8_t>&& data,
                         quint64 connectionId,
                         const QHostAddress& targetAddress,
                         quint16 targetPort,
                         bool broadcast);
    bool sendToConnection(quint64 connectionId, const std::vector<uint8_t>& data);
    void processSendQueue();
    void processReconnects();

    quint64 addConnection(const std::shared_ptr<QTcpSocket>& socket,
                          const QHostAddress& remoteAddress,
                          quint16 remotePort,
                          const QString& endpointKey);
    void removeConnection(quint64 connectionId, const QString& reason, bool scheduleReconnect);
    ConnectionPtr findConnection(const QHostAddress& address, quint16 port);
    ConnectionPtr findConnectionById(quint64 connectionId) const;

    bool resolveRemoteFromConfig(QHostAddress& targetAddress, quint16& targetPort) const;
    bool getSingleConnectionId(quint64& connectionId) const;
    void processConnectionBuffer(const ConnectionPtr& connection);
    static QString makeEndpointKey(const QHostAddress& address, quint16 port);
    bool shouldAutoReconnect() const;
    int getReconnectMinMs() const;
    int getReconnectMaxMs() const;
    double getReconnectMultiplier() const;
    int computeNextBackoffMs(int currentDelayMs) const;
    void ensureEndpointEntry(const QHostAddress& address, quint16 port);
    void markEndpointConnected(const QString& endpointKey, quint64 connectionId);
    void markEndpointFailure(const QString& endpointKey, const QString& error);
    bool getEndpointConnectionId(const QString& endpointKey, quint64& connectionId) const;
    bool canAttemptConnectNow(const QString& endpointKey) const;
    bool pushTaskWithPolicy(SendTask&& task, bool front);
    void logQueueDrop(const char* reason, size_t queueSize, uint64_t dropCount) const;

private:
    std::shared_ptr<QTcpServer> m_server;
    mutable std::mutex m_serverMutex;
    std::map<quint64, ConnectionPtr> m_connections;
    mutable std::mutex m_connectionsMutex;
    std::map<QString, EndpointStateEntry> m_endpointStates;
    mutable std::mutex m_endpointMutex;

    std::thread m_acceptThread;
    std::thread m_receiveThread;
    std::thread m_sendThread;

    std::deque<SendTask> m_sendQueue;
    mutable std::mutex m_sendQueueMutex;
    std::condition_variable m_sendCondition;

    std::atomic<bool> m_running;
    std::atomic<bool> m_sendThreadRunning;
    std::atomic<quint64> m_nextConnectionId;
    std::atomic<QThread*> m_networkThread;
    std::atomic<uint64_t> m_dropCount;
    mutable std::chrono::steady_clock::time_point m_lastQueueDropLogAt;
};

} // namespace LDdsFramework

#endif // LTCPTRANSPORT_H
