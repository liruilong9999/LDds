#ifndef LTCPTRANSPORT_H
#define LTCPTRANSPORT_H

#include "ITransport.h"
#include <atomic>
#include <cstdint>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace LDdsFramework {

class LDDSCORE_EXPORT LTcpTransport : public ITransport
{
public:
    LTcpTransport();
    ~LTcpTransport() override;

    TransportProtocol getProtocol() const noexcept override;
    bool start() override;
    void stop() override;

    bool sendMessage(const LMessage& message) override;
    bool sendMessageTo(const LMessage& message,
                       const QHostAddress& targetAddress,
                       quint16 targetPort);
    bool broadcastMessage(const LMessage& message, quint16 broadcastPort) override;

    bool connectToHost(const QHostAddress& address, quint16 port);
    void disconnect(quint64 connectionId);
    void disconnectAll();
    size_t getConnectionCount() const noexcept;
    bool getConnectionStats(quint64 connectionId, uint64_t& bytesSent, uint64_t& bytesReceived) const;

private:
    struct TcpConnection;
    using ConnectionPtr = std::shared_ptr<TcpConnection>;

    struct SendTask {
        std::vector<uint8_t> data;
        quint64 connectionId; // 0 = broadcast to all connections
    };

private:
    bool initializeServer();
    void closeServer();

    void acceptThreadFunc();
    void receiveThreadFunc();
    void sendThreadFunc();

    bool enqueueSendData(std::vector<uint8_t>&& data, quint64 connectionId);
    bool sendToConnection(quint64 connectionId, const std::vector<uint8_t>& data);

    quint64 addConnection(std::intptr_t socketFd,
                          const QHostAddress& remoteAddress,
                          quint16 remotePort);
    void removeConnection(quint64 connectionId);
    ConnectionPtr findConnection(const QHostAddress& address, quint16 port);

    bool resolveRemoteFromConfig(QHostAddress& targetAddress, quint16& targetPort) const;
    bool getSingleConnectionId(quint64& connectionId) const;
    void processConnectionBuffer(const ConnectionPtr& connection);

private:
    std::atomic<std::intptr_t> m_listenSocket;
    std::map<quint64, ConnectionPtr> m_connections;
    mutable std::mutex m_connectionsMutex;

    std::thread m_acceptThread;
    std::thread m_receiveThread;
    std::thread m_sendThread;

    std::queue<SendTask> m_sendQueue;
    std::mutex m_sendQueueMutex;
    std::condition_variable m_sendCondition;

    std::atomic<bool> m_running;
    std::atomic<bool> m_sendThreadRunning;
    std::atomic<quint64> m_nextConnectionId;
};

} // namespace LDdsFramework

#endif // LTCPTRANSPORT_H
