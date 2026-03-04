#ifndef ITRANSPORT_H
#define ITRANSPORT_H

/**
 * @file ITransport.h
 * @brief 传输层抽象接口与通用配置定义。
 *
 * 设计目标：
 * 1. 统一 UDP/TCP 两种协议在 LDds 内部的使用方式。
 * 2. 将配置、状态、回调机制收敛到同一抽象，便于扩展新传输实现。
 * 3. 将连接/发现/重连等策略参数集中在 `TransportConfig` 统一管理。
 */

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <QString>
#include <QHostAddress>
#include "LDds_Global.h"

namespace LDdsFramework {

class LMessage;

/**
 * @brief 传输协议类型。
 */
enum class TransportProtocol {
    /**
     * @brief 使用 UDP 套接字发送与接收。
     */
    UDP,
    /**
     * @brief 使用 TCP 连接发送与接收。
     */
    TCP
};

/**
 * @brief 发送队列溢出策略。
 */
enum class SendQueueOverflowPolicy {
    /**
     * @brief 丢弃最旧消息，保留最新消息。
     */
    DropOldest = 0,
    /**
     * @brief 丢弃新消息，保持队列已有内容。
     */
    DropNewest = 1,
    /**
     * @brief 快速失败，直接返回发送失败。
     */
    FailFast = 2
};

/**
 * @brief 传输层配置。
 */
struct LDDSCORE_EXPORT TransportConfig {
    /**
     * @brief 本地绑定地址（如 `0.0.0.0` 或 `127.0.0.1`）。
     */
    QString bindAddress;
    /**
     * @brief 本地绑定端口。
     */
    quint16 bindPort;
    /**
     * @brief 默认远端地址（点对点发送时使用）。
     */
    QString remoteAddress;
    /**
     * @brief 默认远端端口。
     */
    quint16 remotePort;
    /**
     * @brief 是否启用按 Domain 映射端口。
     */
    bool enableDomainPortMapping;
    /**
     * @brief 端口映射基准端口。
     */
    quint16 basePort;
    /**
     * @brief Domain 端口偏移量（effectivePort = basePort + domainId * domainPortOffset）。
     */
    quint16 domainPortOffset;
    /**
     * @brief 是否启用广播能力（主要用于 UDP）。
     */
    bool enableBroadcast;
    /**
     * @brief 接收缓冲区大小（字节，<=0 表示使用系统默认）。
     */
    int receiveBufferSize;
    /**
     * @brief 发送缓冲区大小（字节，<=0 表示使用系统默认）。
     */
    int sendBufferSize;
    /**
     * @brief 最大连接数（TCP 服务端接入上限）。
     */
    int maxConnections;
    /**
     * @brief 是否允许地址复用（SO_REUSEADDR）。
     */
    bool reuseAddress;
    /**
     * @brief 连接断开后是否自动重连（TCP）。
     */
    bool autoReconnect;
    /**
     * @brief 重连最小间隔（毫秒）。
     */
    int reconnectMinMs;
    /**
     * @brief 重连最大间隔（毫秒）。
     */
    int reconnectMaxMs;
    /**
     * @brief 重连指数退避倍率。
     */
    double reconnectMultiplier;
    /**
     * @brief 发送队列最大缓存条数。
     */
    int maxPendingMessages;
    /**
     * @brief 发送队列溢出策略。
     */
    SendQueueOverflowPolicy sendQueueOverflowPolicy;
    /**
     * @brief 是否启用发现协议。
     */
    bool enableDiscovery;
    /**
     * @brief 发现报文发送间隔（毫秒）。
     */
    int discoveryIntervalMs;
    /**
     * @brief peer 失活超时（毫秒）。
     */
    int peerTtlMs;
    /**
     * @brief 发现通道端口。
     */
    quint16 discoveryPort;
    /**
     * @brief 是否启用组播发现。
     */
    bool enableMulticast;
    /**
     * @brief 组播地址（如 `239.255.0.x`）。
     */
    QString multicastGroup;
    /**
     * @brief 组播 TTL（生存跳数）。
     */
    int multicastTtl;

    /**
     * @brief 使用默认值构造传输配置。
     */
    TransportConfig();
};

/**
 * @brief 接收回调签名。
 * @param message 已解码消息对象。
 * @param senderAddress 发送方地址。
 * @param senderPort 发送方端口。
 */
using ReceiveCallback = std::function<void(const LMessage&, const QHostAddress&, quint16)>;

/**
 * @brief 传输状态机。
 */
enum class TransportState {
    /**
     * @brief 已停止，未持有网络资源。
     */
    Stopped,
    /**
     * @brief 启动中，正在初始化套接字/线程。
     */
    Starting,
    /**
     * @brief 运行中，可收发消息。
     */
    Running,
    /**
     * @brief 停止中，正在回收线程与套接字。
     */
    Stopping,
    /**
     * @brief 异常状态，最近一次操作失败。
     */
    Error
};

/**
 * @class ITransport
 * @brief 传输抽象基类，统一 UDP/TCP 接口。
 */
class LDDSCORE_EXPORT ITransport
{
public:
    ITransport();
    virtual ~ITransport();

    ITransport(const ITransport& other) = delete;
    ITransport& operator=(const ITransport& other) = delete;

    virtual TransportProtocol getProtocol() const noexcept = 0;
    virtual bool start() = 0;
    virtual void stop() = 0;

    /**
     * @brief 发送到默认远端。
     */
    virtual bool sendMessage(const LMessage& message) = 0;

    /**
     * @brief 广播发送（默认不支持，由子类覆写）。
     */
    virtual bool broadcastMessage(const LMessage& message, quint16 broadcastPort);

    /**
     * @brief 设置默认远端地址。
     */
    virtual bool setDefaultRemote(const QHostAddress& targetAddress, quint16 targetPort);

    /**
     * @brief 设置接收回调。
     * @note 新回调会覆盖旧回调；传入空函数可禁用回调。
     */
    void setReceiveCallback(ReceiveCallback callback);
    /**
     * @brief 获取当前传输状态。
     */
    TransportState getState() const noexcept;
    /**
     * @brief 线程安全读取当前配置快照。
     */
    TransportConfig getConfig() const;
    /**
     * @brief 覆盖当前传输配置。
     * @note 是否立即生效取决于子类实现，一般建议在 `start()` 前设置。
     */
    void setConfig(const TransportConfig& config);
    /**
     * @brief 获取最近一次错误信息。
     */
    QString getLastError() const noexcept;
    /**
     * @brief 获取当前绑定端口（0 表示未绑定成功）。
     */
    quint16 getBoundPort() const noexcept;

    /**
     * @brief 工厂函数：按协议创建具体传输实现。
     */
    static std::unique_ptr<ITransport> createTransport(
        TransportProtocol protocol = TransportProtocol::UDP
    );

protected:
    /**
     * @brief 更新内部状态机状态。
     */
    void setState(TransportState state) noexcept;
    /**
     * @brief 设置错误信息并将状态置为 Error。
     */
    void setError(const QString& error);
    /**
     * @brief 派发接收回调（若已注册）。
     */
    void notifyReceiveCallback(const LMessage& message,
                               const QHostAddress& senderAddress,
                               quint16 senderPort);
    /**
     * @brief 记录本地绑定端口。
     */
    void setBoundPort(quint16 port) noexcept;

protected:
    /**
     * @brief 当前传输配置。
     */
    TransportConfig m_config;
    /**
     * @brief 运行状态。
     */
    std::atomic<TransportState> m_state;
    /**
     * @brief 实际绑定端口。
     */
    std::atomic<quint16> m_boundPort;
    /**
     * @brief 接收回调函数。
     */
    ReceiveCallback m_receiveCallback;
    /**
     * @brief 回调读写锁。
     */
    mutable std::mutex m_callbackMutex;
    /**
     * @brief 最近一次错误信息。
     */
    QString m_lastError;
    /**
     * @brief 错误信息读写锁。
     */
    mutable std::mutex m_errorMutex;
    /**
     * @brief 配置读写锁。
     */
    mutable std::mutex m_configMutex;
};

} // namespace LDdsFramework

#endif // ITRANSPORT_H
