#include "LMessage.h"

#include <cstring>

namespace LDdsFramework {
namespace {

uint32_t readLeUInt32(const uint8_t * data) noexcept
{
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

uint64_t readLeUInt64(const uint8_t * data) noexcept
{
    return static_cast<uint64_t>(data[0]) |
           (static_cast<uint64_t>(data[1]) << 8) |
           (static_cast<uint64_t>(data[2]) << 16) |
           (static_cast<uint64_t>(data[3]) << 24) |
           (static_cast<uint64_t>(data[4]) << 32) |
           (static_cast<uint64_t>(data[5]) << 40) |
           (static_cast<uint64_t>(data[6]) << 48) |
           (static_cast<uint64_t>(data[7]) << 56);
}

} // namespace

void LMessageHeader::serialize(LByteBuffer & buffer) const
{
    buffer.writeUInt32(totalSize);
    buffer.writeUInt32(topic);
    buffer.writeUInt64(sequence);
    buffer.writeUInt32(payloadSize);
    buffer.writeBytes(&protocolVersion, sizeof(protocolVersion));
    buffer.writeBytes(&domainId, sizeof(domainId));
    buffer.writeBytes(&messageType, sizeof(messageType));
    buffer.writeUInt32(writerId);
    buffer.writeUInt64(firstSeq);
    buffer.writeUInt64(lastSeq);
    buffer.writeUInt64(ackSeq);
    buffer.writeUInt64(windowStart);
    buffer.writeUInt32(windowSize);
}

bool LMessageHeader::deserialize(const uint8_t * data, size_t size)
{
    if (size < LEGACY_HEADER_SIZE)
    {
        return false;
    }

    totalSize = readLeUInt32(data);
    topic = readLeUInt32(data + 4);
    sequence = readLeUInt64(data + 8);
    payloadSize = readLeUInt32(data + 16);
    protocolVersion = 0;
    domainId = 0;
    messageType = static_cast<uint8_t>(LMessageType::Data);
    writerId = 0;
    firstSeq = 0;
    lastSeq = 0;
    ackSeq = 0;
    windowStart = 0;
    windowSize = 0;

    if (totalSize != size)
    {
        return false;
    }

    if (LEGACY_HEADER_SIZE + payloadSize == size)
    {
        return true;
    }

    if (size >= V1_HEADER_SIZE && (V1_HEADER_SIZE + payloadSize == size))
    {
        protocolVersion = data[20];
        domainId = data[21];
        messageType = (topic == HEARTBEAT_TOPIC_ID)
            ? static_cast<uint8_t>(LMessageType::Heartbeat)
            : static_cast<uint8_t>(LMessageType::Data);
        return true;
    }

    if (size >= HEADER_SIZE && (HEADER_SIZE + payloadSize == size))
    {
        protocolVersion = data[20];
        domainId = data[21];
        messageType = data[22];
        writerId = readLeUInt32(data + 23);
        firstSeq = readLeUInt64(data + 27);
        lastSeq = readLeUInt64(data + 35);
        ackSeq = readLeUInt64(data + 43);
        windowStart = readLeUInt64(data + 51);
        windowSize = readLeUInt32(data + 59);
        return true;
    }

    return false;
}

LMessage::LMessage()
    : m_messageType(LMessageType::Data)
    , m_topic(0)
    , m_sequence(0)
    , m_domainId(0)
    , m_writerId(0)
    , m_firstSeq(0)
    , m_lastSeq(0)
    , m_ackSeq(0)
    , m_windowStart(0)
    , m_windowSize(0)
    , m_senderPort(0)
{
}

LMessage::LMessage(uint32_t topic, uint64_t sequence, const std::vector<uint8_t> & payload)
    : m_messageType(topic == HEARTBEAT_TOPIC_ID ? LMessageType::Heartbeat : LMessageType::Data)
    , m_topic(topic)
    , m_sequence(sequence)
    , m_domainId(0)
    , m_writerId(0)
    , m_firstSeq(sequence)
    , m_lastSeq(sequence)
    , m_ackSeq(0)
    , m_windowStart(0)
    , m_windowSize(0)
    , m_payload(payload)
    , m_senderPort(0)
{
}

LMessage::LMessage(
    uint32_t topic,
    uint64_t sequence,
    const std::vector<uint8_t> & payload,
    LMessageType messageType)
    : m_messageType(messageType)
    , m_topic(topic)
    , m_sequence(sequence)
    , m_domainId(0)
    , m_writerId(0)
    , m_firstSeq(sequence)
    , m_lastSeq(sequence)
    , m_ackSeq(0)
    , m_windowStart(0)
    , m_windowSize(0)
    , m_payload(payload)
    , m_senderPort(0)
{
    if (m_messageType != LMessageType::Data)
    {
        m_topic = HEARTBEAT_TOPIC_ID;
    }
    else if (m_topic == HEARTBEAT_TOPIC_ID)
    {
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
    if (m_messageType == LMessageType::Data)
    {
        if (topic == HEARTBEAT_TOPIC_ID)
        {
            m_messageType = LMessageType::Heartbeat;
        }
    }
    else if (m_messageType == LMessageType::Heartbeat)
    {
        if (topic != HEARTBEAT_TOPIC_ID)
        {
            m_messageType = LMessageType::Data;
        }
    }
    else
    {
        m_topic = HEARTBEAT_TOPIC_ID;
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

uint8_t LMessage::getDomainId() const noexcept
{
    return m_domainId;
}

void LMessage::setDomainId(uint8_t domainId) noexcept
{
    m_domainId = domainId;
}

uint32_t LMessage::getWriterId() const noexcept
{
    return m_writerId;
}

void LMessage::setWriterId(uint32_t writerId) noexcept
{
    m_writerId = writerId;
}

uint64_t LMessage::getFirstSeq() const noexcept
{
    return m_firstSeq;
}

void LMessage::setFirstSeq(uint64_t firstSeq) noexcept
{
    m_firstSeq = firstSeq;
}

uint64_t LMessage::getLastSeq() const noexcept
{
    return m_lastSeq;
}

void LMessage::setLastSeq(uint64_t lastSeq) noexcept
{
    m_lastSeq = lastSeq;
}

uint64_t LMessage::getAckSeq() const noexcept
{
    return m_ackSeq;
}

void LMessage::setAckSeq(uint64_t ackSeq) noexcept
{
    m_ackSeq = ackSeq;
}

uint64_t LMessage::getWindowStart() const noexcept
{
    return m_windowStart;
}

void LMessage::setWindowStart(uint64_t windowStart) noexcept
{
    m_windowStart = windowStart;
}

uint32_t LMessage::getWindowSize() const noexcept
{
    return m_windowSize;
}

void LMessage::setWindowSize(uint32_t windowSize) noexcept
{
    m_windowSize = windowSize;
}

LMessageType LMessage::getMessageType() const noexcept
{
    return m_messageType;
}

void LMessage::setMessageType(LMessageType type) noexcept
{
    m_messageType = type;
    if (type == LMessageType::Heartbeat ||
        type == LMessageType::HeartbeatReq ||
        type == LMessageType::HeartbeatRsp ||
        type == LMessageType::Ack ||
        type == LMessageType::Nack ||
        type == LMessageType::DiscoveryAnnounce)
    {
        m_topic = HEARTBEAT_TOPIC_ID;
    }
}

bool LMessage::isHeartbeat() const noexcept
{
    return m_messageType == LMessageType::Heartbeat ||
           m_messageType == LMessageType::HeartbeatReq ||
           m_messageType == LMessageType::HeartbeatRsp;
}

bool LMessage::isControlMessage() const noexcept
{
    return m_messageType == LMessageType::Ack ||
           m_messageType == LMessageType::Nack ||
           m_messageType == LMessageType::HeartbeatReq ||
           m_messageType == LMessageType::HeartbeatRsp ||
           m_messageType == LMessageType::Heartbeat ||
           m_messageType == LMessageType::DiscoveryAnnounce;
}

LMessage LMessage::makeHeartbeat(uint64_t sequence, uint64_t timestampMs)
{
    std::vector<uint8_t> payload(sizeof(uint64_t), 0);
    std::memcpy(payload.data(), &timestampMs, sizeof(uint64_t));
    LMessage message(HEARTBEAT_TOPIC_ID, sequence, payload, LMessageType::Heartbeat);
    message.setFirstSeq(sequence);
    message.setLastSeq(sequence);
    return message;
}

LMessage LMessage::makeAck(
    uint32_t writerId,
    uint64_t ackSeq,
    uint64_t windowStart,
    uint32_t windowSize)
{
    LMessage message(HEARTBEAT_TOPIC_ID, ackSeq, std::vector<uint8_t>(), LMessageType::Ack);
    message.setWriterId(writerId);
    message.setAckSeq(ackSeq);
    message.setWindowStart(windowStart);
    message.setWindowSize(windowSize);
    return message;
}

LMessage LMessage::makeHeartbeatReq(
    uint32_t writerId,
    uint64_t firstSeq,
    uint64_t lastSeq)
{
    LMessage message(HEARTBEAT_TOPIC_ID, lastSeq, std::vector<uint8_t>(), LMessageType::HeartbeatReq);
    message.setWriterId(writerId);
    message.setFirstSeq(firstSeq);
    message.setLastSeq(lastSeq);
    return message;
}

LMessage LMessage::makeHeartbeatRsp(
    uint32_t writerId,
    uint64_t ackSeq,
    uint64_t windowStart,
    uint32_t windowSize)
{
    LMessage message(HEARTBEAT_TOPIC_ID, ackSeq, std::vector<uint8_t>(), LMessageType::HeartbeatRsp);
    message.setWriterId(writerId);
    message.setAckSeq(ackSeq);
    message.setWindowStart(windowStart);
    message.setWindowSize(windowSize);
    return message;
}

const std::vector<uint8_t> & LMessage::getPayload() const noexcept
{
    return m_payload;
}

std::vector<uint8_t> & LMessage::getPayload() noexcept
{
    return m_payload;
}

void LMessage::setPayload(const std::vector<uint8_t> & payload)
{
    m_payload = payload;
}

void LMessage::setPayload(const void * data, size_t size)
{
    if (data && size > 0)
    {
        m_payload.resize(size);
        std::memcpy(m_payload.data(), data, size);
    }
    else
    {
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

    LMessageHeader header;
    header.totalSize = static_cast<uint32_t>(getTotalSize());
    header.topic = m_topic;
    header.sequence = m_sequence;
    header.payloadSize = static_cast<uint32_t>(m_payload.size());
    header.protocolVersion = LMessageHeader::CURRENT_PROTOCOL_VERSION;
    header.domainId = m_domainId;
    header.messageType = static_cast<uint8_t>(m_messageType);
    header.writerId = m_writerId;
    header.firstSeq = m_firstSeq;
    header.lastSeq = m_lastSeq;
    header.ackSeq = m_ackSeq;
    header.windowStart = m_windowStart;
    header.windowSize = m_windowSize;
    header.serialize(buffer);

    if (!m_payload.empty())
    {
        buffer.writeBytes(m_payload.data(), m_payload.size());
    }

    return buffer;
}

bool LMessage::deserialize(const uint8_t * data, size_t size)
{
    if (size < LMessageHeader::LEGACY_HEADER_SIZE)
    {
        return false;
    }

    LMessageHeader header;
    if (!header.deserialize(data, size))
    {
        return false;
    }

    m_topic = header.topic;
    m_sequence = header.sequence;
    m_domainId = header.domainId;
    if (header.protocolVersion >= 2 &&
        header.messageType <= static_cast<uint8_t>(LMessageType::DiscoveryAnnounce))
    {
        m_messageType = static_cast<LMessageType>(header.messageType);
    }
    else
    {
        m_messageType = (m_topic == HEARTBEAT_TOPIC_ID) ? LMessageType::Heartbeat : LMessageType::Data;
    }
    m_writerId = header.writerId;
    m_firstSeq = header.firstSeq;
    m_lastSeq = header.lastSeq;
    m_ackSeq = header.ackSeq;
    m_windowStart = header.windowStart;
    m_windowSize = header.windowSize;

    if (header.payloadSize > 0)
    {
        m_payload.resize(header.payloadSize);
        const size_t payloadOffset =
            (size == (LMessageHeader::HEADER_SIZE + header.payloadSize))
                ? LMessageHeader::HEADER_SIZE
                : ((size == (LMessageHeader::V1_HEADER_SIZE + header.payloadSize))
                       ? LMessageHeader::V1_HEADER_SIZE
                       : LMessageHeader::LEGACY_HEADER_SIZE);
        std::memcpy(m_payload.data(), data + payloadOffset, header.payloadSize);
    }
    else
    {
        m_payload.clear();
    }

    return true;
}

bool LMessage::deserialize(const LByteBuffer & buffer)
{
    return deserialize(buffer.data(), buffer.size());
}

void LMessage::clear() noexcept
{
    m_messageType = LMessageType::Data;
    m_topic = 0;
    m_sequence = 0;
    m_domainId = 0;
    m_writerId = 0;
    m_firstSeq = 0;
    m_lastSeq = 0;
    m_ackSeq = 0;
    m_windowStart = 0;
    m_windowSize = 0;
    m_payload.clear();
    m_senderAddress.clear();
    m_senderPort = 0;
}

LHostAddress LMessage::getSenderAddress() const noexcept
{
    return m_senderAddress;
}

void LMessage::setSenderAddress(const LHostAddress & address) noexcept
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
    return getTotalSize() >= LMessageHeader::LEGACY_HEADER_SIZE;
}

} // namespace LDdsFramework
