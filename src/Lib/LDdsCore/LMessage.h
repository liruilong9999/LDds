#ifndef LMESSAGE_H
#define LMESSAGE_H

/**
 * @file LMessage.h
 * @brief LDds 消息对象与线协议头定义。
 *
 * 说明：
 * 1. `LMessageHeader` 描述跨网络传输的头字段布局。
 * 2. `LMessage` 封装消息头+payload，并提供序列化/反序列化能力。
 * 3. 当前协议版本通过 `protocolVersion` 字段管理（包含兼容旧格式的解析逻辑）。
 */

#include <cstdint>
#include <string>
#include <vector>

#include <QHostAddress>

#include "LByteBuffer.h"
#include "LDds_Global.h"

namespace LDdsFramework {

/**
 * @brief 消息类型。
 */
enum class LMessageType : uint8_t
{
    /**
     * @brief 普通业务数据消息。
     */
    Data = 0,
    /**
     * @brief 心跳消息（旧路径保留）。
     */
    Heartbeat = 1,
    /**
     * @brief 累计确认消息（可靠传输）。
     */
    Ack = 2,
    /**
     * @brief 否认确认消息（请求重传）。
     */
    Nack = 3,
    /**
     * @brief 心跳请求（请求对端回报窗口状态）。
     */
    HeartbeatReq = 4,
    /**
     * @brief 心跳响应（携带 ack/window 信息）。
     */
    HeartbeatRsp = 5,
    /**
     * @brief 发现协议公告消息。
     */
    DiscoveryAnnounce = 6
};

/**
 * @brief 保留的系统 topic（心跳与控制面使用）。
 */
constexpr uint32_t HEARTBEAT_TOPIC_ID = 0;

#pragma pack(push, 1)
/**
 * @brief 线协议消息头（紧凑布局，按字节对齐）。
 */
struct LMessageHeader {
    /**
     * @brief 整包总长度（头+payload）。
     */
    uint32_t totalSize;
    /**
     * @brief topic id。
     */
    uint32_t topic;
    /**
     * @brief 发送序列号。
     */
    uint64_t sequence;
    /**
     * @brief payload 长度。
     */
    uint32_t payloadSize;
    /**
     * @brief 协议版本号。
     */
    uint8_t protocolVersion;
    /**
     * @brief Domain ID（0-255）。
     */
    uint8_t domainId;
    /**
     * @brief 消息类型（`LMessageType`）。
     */
    uint8_t messageType;
    /**
     * @brief writer 标识，用于可靠传输状态隔离。
     */
    uint32_t writerId;
    /**
     * @brief 首个序列号（窗口/区间起点）。
     */
    uint64_t firstSeq;
    /**
     * @brief 最后序列号（窗口/区间终点）。
     */
    uint64_t lastSeq;
    /**
     * @brief 累计确认序列号。
     */
    uint64_t ackSeq;
    /**
     * @brief 接收窗口起始序列号。
     */
    uint64_t windowStart;
    /**
     * @brief 接收窗口大小。
     */
    uint32_t windowSize;

    /**
     * @brief 旧格式头长度（仅 topic/seq/payload）。
     */
    static constexpr size_t LEGACY_HEADER_SIZE = sizeof(uint32_t) + sizeof(uint32_t) +
                                                 sizeof(uint64_t) + sizeof(uint32_t);
    /**
     * @brief v1 头长度（增加 protocolVersion/domainId）。
     */
    static constexpr size_t V1_HEADER_SIZE =
        LEGACY_HEADER_SIZE + sizeof(uint8_t) + sizeof(uint8_t);
    /**
     * @brief 当前完整头长度（v2）。
     */
    static constexpr size_t HEADER_SIZE = V1_HEADER_SIZE + sizeof(uint8_t) + sizeof(uint32_t) +
                                          sizeof(uint64_t) + sizeof(uint64_t) + sizeof(uint64_t) +
                                          sizeof(uint64_t) + sizeof(uint32_t);
    /**
     * @brief 当前协议版本号。
     */
    static constexpr uint8_t CURRENT_PROTOCOL_VERSION = 2;

    /**
     * @brief 将头写入缓冲区。
     */
    void serialize(LByteBuffer& buffer) const;
    /**
     * @brief 从原始字节解析头。
     * @return 解析成功返回 true。
     */
    bool deserialize(const uint8_t* data, size_t size);
};
#pragma pack(pop)

/**
 * @class LMessage
 * @brief LDds 消息对象（头字段 + payload + 发送方元信息）。
 */
class LDDSCORE_EXPORT LMessage
{
public:
    /**
     * @brief 构造空消息（默认 Data 类型）。
     */
    LMessage();
    /**
     * @brief 构造 Data 消息。
     */
    LMessage(uint32_t topic, uint64_t sequence, const std::vector<uint8_t>& payload);
    /**
     * @brief 构造指定类型消息。
     */
    LMessage(
        uint32_t topic,
        uint64_t sequence,
        const std::vector<uint8_t>& payload,
        LMessageType messageType
    );

    ~LMessage() = default;

    /**
     * @brief 获取/设置 topic。
     */
    uint32_t getTopic() const noexcept;
    void setTopic(uint32_t topic) noexcept;

    /**
     * @brief 获取/设置序列号。
     */
    uint64_t getSequence() const noexcept;
    void setSequence(uint64_t sequence) noexcept;

    /**
     * @brief 获取/设置域号（0-255）。
     */
    uint8_t getDomainId() const noexcept;
    void setDomainId(uint8_t domainId) noexcept;
    /**
     * @brief 获取/设置 writerId。
     */
    uint32_t getWriterId() const noexcept;
    void setWriterId(uint32_t writerId) noexcept;
    /**
     * @brief 获取/设置区间起始序列号。
     */
    uint64_t getFirstSeq() const noexcept;
    void setFirstSeq(uint64_t firstSeq) noexcept;
    /**
     * @brief 获取/设置区间结束序列号。
     */
    uint64_t getLastSeq() const noexcept;
    void setLastSeq(uint64_t lastSeq) noexcept;
    /**
     * @brief 获取/设置累计确认序列号。
     */
    uint64_t getAckSeq() const noexcept;
    void setAckSeq(uint64_t ackSeq) noexcept;
    /**
     * @brief 获取/设置窗口起点。
     */
    uint64_t getWindowStart() const noexcept;
    void setWindowStart(uint64_t windowStart) noexcept;
    /**
     * @brief 获取/设置窗口大小。
     */
    uint32_t getWindowSize() const noexcept;
    void setWindowSize(uint32_t windowSize) noexcept;

    /**
     * @brief 获取/设置消息类型。
     */
    LMessageType getMessageType() const noexcept;
    void setMessageType(LMessageType type) noexcept;

    /**
     * @brief 是否为传统心跳消息。
     */
    bool isHeartbeat() const noexcept;
    /**
     * @brief 是否为控制消息（非 Data）。
     */
    bool isControlMessage() const noexcept;
    /**
     * @brief 构造心跳消息。
     */
    static LMessage makeHeartbeat(uint64_t sequence, uint64_t timestampMs = 0);
    /**
     * @brief 构造 ACK 消息。
     */
    static LMessage makeAck(
        uint32_t writerId,
        uint64_t ackSeq,
        uint64_t windowStart,
        uint32_t windowSize);
    /**
     * @brief 构造心跳请求消息。
     */
    static LMessage makeHeartbeatReq(
        uint32_t writerId,
        uint64_t firstSeq,
        uint64_t lastSeq);
    /**
     * @brief 构造心跳响应消息。
     */
    static LMessage makeHeartbeatRsp(
        uint32_t writerId,
        uint64_t ackSeq,
        uint64_t windowStart,
        uint32_t windowSize);

    /**
     * @brief 获取 payload（只读/可写）。
     */
    const std::vector<uint8_t>& getPayload() const noexcept;
    std::vector<uint8_t>& getPayload() noexcept;

    /**
     * @brief 设置 payload（vector 或裸指针）。
     */
    void setPayload(const std::vector<uint8_t>& payload);
    void setPayload(const void* data, size_t size);

    /**
     * @brief 获取总长度与 payload 长度。
     */
    size_t getTotalSize() const noexcept;
    size_t getPayloadSize() const noexcept;

    /**
     * @brief 序列化到字节缓冲区。
     */
    LByteBuffer serialize() const;
    /**
     * @brief 从字节数组/缓冲区反序列化。
     */
    bool deserialize(const uint8_t* data, size_t size);
    bool deserialize(const LByteBuffer& buffer);

    /**
     * @brief 重置为默认空消息状态。
     */
    void clear() noexcept;

    /**
     * @brief 获取/设置发送方地址端口（接收路径填充）。
     */
    QHostAddress getSenderAddress() const noexcept;
    void setSenderAddress(const QHostAddress& address) noexcept;

    quint16 getSenderPort() const noexcept;
    void setSenderPort(quint16 port) noexcept;

    /**
     * @brief 校验消息字段是否满足基本合法性。
     */
    bool isValid() const noexcept;

private:
    LMessageType m_messageType;
    uint32_t m_topic;
    uint64_t m_sequence;
    uint8_t m_domainId;
    uint32_t m_writerId;
    uint64_t m_firstSeq;
    uint64_t m_lastSeq;
    uint64_t m_ackSeq;
    uint64_t m_windowStart;
    uint32_t m_windowSize;
    std::vector<uint8_t> m_payload;
    QHostAddress m_senderAddress;
    quint16 m_senderPort;
};

} // namespace LDdsFramework

#endif // LMESSAGE_H
