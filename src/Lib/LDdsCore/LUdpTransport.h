#ifndef LUDPTRANSPORT_H
#define LUDPTRANSPORT_H

/**
 * @file LUdpTransport.h
 * @brief UDP 传输实现定义（单播/广播发送与接收线程）。
 */

#include "ITransport.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

class QUdpSocket;

namespace LDdsFramework {

/**
 * @class LUdpTransport
 * @brief 基于 Qt UDP 套接字的传输实现。
 *
 * 该类实现了 DDS 传输抽象层中的 UDP 协议能力，支持：
 * - 默认远端发送与定向发送
 * - 广播发送
 * - 后台接收线程回调分发
 * - 发送/接收统计
 */
class LDDSCORE_EXPORT LUdpTransport : public ITransport
{
public:
    /**
     * @brief 构造函数。
     */
    LUdpTransport();
    /**
     * @brief 析构函数，内部会自动 stop()。
     */
    ~LUdpTransport() override;

    /**
     * @brief 获取传输协议类型。
     * @return 固定返回 TransportProtocol::UDP。
     */
    TransportProtocol getProtocol() const noexcept override;
    /**
     * @brief 启动传输层并创建接收线程。
     * @return 启动成功返回 true。
     */
    bool start() override;
    /**
     * @brief 停止传输层并释放套接字。
     */
    void stop() override;

    /**
     * @brief 发送消息到默认远端。
     * @param message 待发送消息。
     * @return 发送成功返回 true。
     */
    bool sendMessage(const LMessage& message) override;
    /**
     * @brief 发送消息到指定远端。
     * @param message 待发送消息。
     * @param targetAddress 目标地址。
     * @param targetPort 目标端口。
     * @return 发送成功返回 true。
     */
    bool sendMessageTo(const LMessage& message,
                       const QHostAddress& targetAddress,
                       quint16 targetPort);
    /**
     * @brief 广播发送消息。
     * @param message 待发送消息。
     * @param broadcastPort 广播端口。
     * @return 发送成功返回 true。
     */
    bool broadcastMessage(const LMessage& message, quint16 broadcastPort) override;

    /**
     * @brief 设置允许发送的最大 UDP 包大小（字节）。
     * @param maxSize 最大包长。
     */
    void setMaxPacketSize(size_t maxSize) noexcept;
    /**
     * @brief 获取当前最大 UDP 包大小（字节）。
     */
    size_t getMaxPacketSize() const noexcept;

    /**
     * @brief 设置是否允许广播。
     * @param enable true 表示允许广播。
     */
    bool setBroadcastEnabled(bool enable);
    /**
     * @brief 查询广播是否允许。
     */
    bool isBroadcastEnabled() const;

    /**
     * @brief 获取累计发送报文数。
     */
    uint64_t getSentPacketCount() const noexcept;
    /**
     * @brief 获取累计接收报文数。
     */
    uint64_t getReceivedPacketCount() const noexcept;
    /**
     * @brief 获取累计发送字节数。
     */
    uint64_t getSentByteCount() const noexcept;
    /**
     * @brief 获取累计接收字节数。
     */
    uint64_t getReceivedByteCount() const noexcept;

private:
    void receiveThreadFunc();
    bool initializeSocket();
    void closeSocket();
    bool resolveRemoteFromConfig(QHostAddress& targetAddress, quint16& targetPort) const;

private:
    std::unique_ptr<QUdpSocket> m_receiveSocket;
    std::unique_ptr<QUdpSocket> m_sendSocket;
    std::thread m_receiveThread;
    std::atomic<bool> m_running;

    std::atomic<size_t> m_maxPacketSize;
    std::atomic<uint64_t> m_sentPacketCount;
    std::atomic<uint64_t> m_recvPacketCount;
    std::atomic<uint64_t> m_sentByteCount;
    std::atomic<uint64_t> m_recvByteCount;

    mutable std::mutex m_receiveSocketMutex;
    mutable std::mutex m_sendSocketMutex;
};

} // namespace LDdsFramework

#endif // LUDPTRANSPORT_H
