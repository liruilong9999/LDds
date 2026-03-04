#ifndef LMESSAGE_H
#define LMESSAGE_H

#include <cstdint>
#include <string>
#include <vector>

#include <QHostAddress>

#include "LByteBuffer.h"
#include "LDds_Global.h"

namespace LDdsFramework {

enum class LMessageType : uint8_t
{
    Data = 0,
    Heartbeat = 1
};

constexpr uint32_t HEARTBEAT_TOPIC_ID = 0;

#pragma pack(push, 1)
struct LMessageHeader {
    uint32_t totalSize;
    uint32_t topic;
    uint64_t sequence;
    uint32_t payloadSize;
    uint8_t protocolVersion;
    uint8_t domainId;

    static constexpr size_t LEGACY_HEADER_SIZE = sizeof(uint32_t) + sizeof(uint32_t) +
                                                 sizeof(uint64_t) + sizeof(uint32_t);
    static constexpr size_t HEADER_SIZE = LEGACY_HEADER_SIZE + sizeof(uint8_t) + sizeof(uint8_t);
    static constexpr uint8_t CURRENT_PROTOCOL_VERSION = 1;

    void serialize(LByteBuffer& buffer) const;
    bool deserialize(const uint8_t* data, size_t size);
};
#pragma pack(pop)

class LDDSCORE_EXPORT LMessage
{
public:
    LMessage();
    LMessage(uint32_t topic, uint64_t sequence, const std::vector<uint8_t>& payload);
    LMessage(
        uint32_t topic,
        uint64_t sequence,
        const std::vector<uint8_t>& payload,
        LMessageType messageType
    );

    ~LMessage() = default;

    uint32_t getTopic() const noexcept;
    void setTopic(uint32_t topic) noexcept;

    uint64_t getSequence() const noexcept;
    void setSequence(uint64_t sequence) noexcept;

    uint8_t getDomainId() const noexcept;
    void setDomainId(uint8_t domainId) noexcept;

    LMessageType getMessageType() const noexcept;
    void setMessageType(LMessageType type) noexcept;

    bool isHeartbeat() const noexcept;
    static LMessage makeHeartbeat(uint64_t sequence, uint64_t timestampMs = 0);

    const std::vector<uint8_t>& getPayload() const noexcept;
    std::vector<uint8_t>& getPayload() noexcept;

    void setPayload(const std::vector<uint8_t>& payload);
    void setPayload(const void* data, size_t size);

    size_t getTotalSize() const noexcept;
    size_t getPayloadSize() const noexcept;

    LByteBuffer serialize() const;
    bool deserialize(const uint8_t* data, size_t size);
    bool deserialize(const LByteBuffer& buffer);

    void clear() noexcept;

    QHostAddress getSenderAddress() const noexcept;
    void setSenderAddress(const QHostAddress& address) noexcept;

    quint16 getSenderPort() const noexcept;
    void setSenderPort(quint16 port) noexcept;

    bool isValid() const noexcept;

private:
    LMessageType m_messageType;
    uint32_t m_topic;
    uint64_t m_sequence;
    uint8_t m_domainId;
    std::vector<uint8_t> m_payload;
    QHostAddress m_senderAddress;
    quint16 m_senderPort;
};

} // namespace LDdsFramework

#endif // LMESSAGE_H
