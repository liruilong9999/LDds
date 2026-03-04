#ifndef LUDPTRANSPORT_H
#define LUDPTRANSPORT_H

#include "ITransport.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>

namespace LDdsFramework {

class LDDSCORE_EXPORT LUdpTransport : public ITransport
{
public:
    LUdpTransport();
    ~LUdpTransport() override;

    TransportProtocol getProtocol() const noexcept override;
    bool start() override;
    void stop() override;

    bool sendMessage(const LMessage& message) override;
    bool sendMessageTo(const LMessage& message,
                       const QHostAddress& targetAddress,
                       quint16 targetPort);
    bool broadcastMessage(const LMessage& message, quint16 broadcastPort) override;

    void setMaxPacketSize(size_t maxSize) noexcept;
    size_t getMaxPacketSize() const noexcept;

    bool setBroadcastEnabled(bool enable);
    bool isBroadcastEnabled() const;

    uint64_t getSentPacketCount() const noexcept;
    uint64_t getReceivedPacketCount() const noexcept;
    uint64_t getSentByteCount() const noexcept;
    uint64_t getReceivedByteCount() const noexcept;

private:
    void receiveThreadFunc();
    bool initializeSocket();
    void closeSocket();
    bool resolveRemoteFromConfig(QHostAddress& targetAddress, quint16& targetPort) const;

private:
    std::intptr_t m_socket;
    std::thread m_receiveThread;
    std::atomic<bool> m_running;

    std::atomic<size_t> m_maxPacketSize;
    std::atomic<uint64_t> m_sentPacketCount;
    std::atomic<uint64_t> m_recvPacketCount;
    std::atomic<uint64_t> m_sentByteCount;
    std::atomic<uint64_t> m_recvByteCount;

    mutable std::mutex m_socketMutex;
};

} // namespace LDdsFramework

#endif // LUDPTRANSPORT_H
