#include "LMessage.h"

#include <cstring>

namespace LDdsFramework {

void LMessageHeader::serialize(LByteBuffer& buffer) const
{
    buffer.writeUInt32(totalSize);
    buffer.writeUInt32(topic);
    buffer.writeUInt64(sequence);
    buffer.writeUInt32(payloadSize);
}

bool LMessageHeader::deserialize(const uint8_t* data, size_t size)
{
    if (size < HEADER_SIZE) {
        return false;
    }

    // 小端序读取
    totalSize = static_cast<uint32_t>(data[0]) |
                (static_cast<uint32_t>(data[1]) << 8) |
                (static_cast<uint32_t>(data[2]) << 16) |
                (static_cast<uint32_t>(data[3]) << 24);

    topic = static_cast<uint32_t>(data[4]) |
            (static_cast<uint32_t>(data[5]) << 8) |
            (static_cast<uint32_t>(data[6]) << 16) |
            (static_cast<uint32_t>(data[7]) << 24);

    sequence = static_cast<uint64_t>(data[8]) |
               (static_cast<uint64_t>(data[9]) << 8) |
               (static_cast<uint64_t>(data[10]) << 16) |
               (static_cast<uint64_t>(data[11]) << 24) |
               (static_cast<uint64_t>(data[12]) << 32) |
               (static_cast<uint64_t>(data[13]) << 40) |
               (static_cast<uint64_t>(data[14]) << 48) |
               (static_cast<uint64_t>(data[15]) << 56);

    payloadSize = static_cast<uint32_t>(data[16]) |
                  (static_cast<uint32_t>(data[17]) << 8) |
                  (static_cast<uint32_t>(data[18]) << 16) |
                  (static_cast<uint32_t>(data[19]) << 24);

    return true;
}

LMessage::LMessage()
    : m_messageType(LMessageType::Data)
    , m_topic(0)
    , m_sequence(0)
    , m_senderPort(0)
{
}

LMessage::LMessage(uint32_t topic, uint64_t sequence, const std::vector<uint8_t>& payload)
    : m_messageType(topic == HEARTBEAT_TOPIC_ID ? LMessageType::Heartbeat : LMessageType::Data)
    , m_topic(topic)
    , m_sequence(sequence)
    , m_payload(payload)
    , m_senderPort(0)
{
}

LMessage::LMessage(
    uint32_t                    topic,
    uint64_t                    sequence,
    const std::vector<uint8_t>& payload,
    LMessageType                messageType)
    : m_messageType(messageType)
    , m_topic(topic)
    , m_sequence(sequence)
    , m_payload(payload)
    , m_senderPort(0)
{
    if (m_messageType == LMessageType::Heartbeat) {
        m_topic = HEARTBEAT_TOPIC_ID;
    } else if (m_topic == HEARTBEAT_TOPIC_ID) {
        m_messageType = LMessageType::Heartbeat;
    }
}

uint32_t LMessage::getTopic() const noexcept
{
    return m_topic;
}

void LMessage::setTopic(uint32_t topic) noexcept
{
    m_topic = topic;
    if (topic == HEARTBEAT_TOPIC_ID) {
        m_messageType = LMessageType::Heartbeat;
    } else if (m_messageType == LMessageType::Heartbeat) {
        m_messageType = LMessageType::Data;
    }
}

uint64_t LMessage::getSequence() const noexcept
{
    return m_sequence;
}

void LMessage::setSequence(uint64_t sequence) noexcept
{
    m_sequence = sequence;
}

LMessageType LMessage::getMessageType() const noexcept
{
    return m_messageType;
}

void LMessage::setMessageType(LMessageType type) noexcept
{
    m_messageType = type;
    if (type == LMessageType::Heartbeat) {
        m_topic = HEARTBEAT_TOPIC_ID;
    }
}

bool LMessage::isHeartbeat() const noexcept
{
    return m_messageType == LMessageType::Heartbeat || m_topic == HEARTBEAT_TOPIC_ID;
}

LMessage LMessage::makeHeartbeat(uint64_t sequence, uint64_t timestampMs)
{
    std::vector<uint8_t> payload(sizeof(uint64_t), 0);
    std::memcpy(payload.data(), &timestampMs, sizeof(uint64_t));
    return LMessage(HEARTBEAT_TOPIC_ID, sequence, payload, LMessageType::Heartbeat);
}

const std::vector<uint8_t>& LMessage::getPayload() const noexcept
{
    return m_payload;
}

std::vector<uint8_t>& LMessage::getPayload() noexcept
{
    return m_payload;
}

void LMessage::setPayload(const std::vector<uint8_t>& payload)
{
    m_payload = payload;
}

void LMessage::setPayload(const void* data, size_t size)
{
    if (data && size > 0) {
        m_payload.resize(size);
        std::memcpy(m_payload.data(), data, size);
    } else {
        m_payload.clear();
    }
}

size_t LMessage::getTotalSize() const noexcept
{
    return LMessageHeader::HEADER_SIZE + m_payload.size();
}

size_t LMessage::getPayloadSize() const noexcept
{
    return m_payload.size();
}

LByteBuffer LMessage::serialize() const
{
    LByteBuffer buffer;

    // 写入头部
    buffer.writeUInt32(static_cast<uint32_t>(getTotalSize()));
    buffer.writeUInt32(m_topic);
    buffer.writeUInt64(m_sequence);
    buffer.writeUInt32(static_cast<uint32_t>(m_payload.size()));

    // 写入payload
    if (!m_payload.empty()) {
        buffer.writeBytes(m_payload.data(), m_payload.size());
    }

    return buffer;
}

bool LMessage::deserialize(const uint8_t* data, size_t size)
{
    if (size < LMessageHeader::HEADER_SIZE) {
        return false;
    }

    // 解析头部
    LMessageHeader header;
    if (!header.deserialize(data, size)) {
        return false;
    }

    // 验证总大小
    if (header.totalSize != size) {
        return false;
    }

    // 验证payload大小
    if (LMessageHeader::HEADER_SIZE + header.payloadSize != size) {
        return false;
    }

    // 设置字段
    m_topic = header.topic;
    m_sequence = header.sequence;
    m_messageType = (m_topic == HEARTBEAT_TOPIC_ID) ? LMessageType::Heartbeat : LMessageType::Data;

    // 复制payload
    if (header.payloadSize > 0) {
        m_payload.resize(header.payloadSize);
        std::memcpy(m_payload.data(), data + LMessageHeader::HEADER_SIZE, header.payloadSize);
    } else {
        m_payload.clear();
    }

    return true;
}

bool LMessage::deserialize(const LByteBuffer& buffer)
{
    return deserialize(buffer.data(), buffer.size());
}

void LMessage::clear() noexcept
{
    m_messageType = LMessageType::Data;
    m_topic = 0;
    m_sequence = 0;
    m_payload.clear();
    m_senderAddress.clear();
    m_senderPort = 0;
}

QHostAddress LMessage::getSenderAddress() const noexcept
{
    return m_senderAddress;
}

void LMessage::setSenderAddress(const QHostAddress& address) noexcept
{
    m_senderAddress = address;
}

quint16 LMessage::getSenderPort() const noexcept
{
    return m_senderPort;
}

void LMessage::setSenderPort(quint16 port) noexcept
{
    m_senderPort = port;
}

bool LMessage::isValid() const noexcept
{
    return getTotalSize() >= LMessageHeader::HEADER_SIZE;
}

} // namespace LDdsFramework
