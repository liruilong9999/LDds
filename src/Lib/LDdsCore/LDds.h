/**
 * @file LDds.h
 * @brief LDds public headers
 */

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ITransport.h"
#include "LDomain.h"
#include "LFindSet.h"
#include "LIdlGenerator.h"
#include "LIdlParser.h"
#include "LMessage.h"
#include "LQos.h"
#include "LTypeRegistry.h"
#include "LDds_Global.h"

namespace LDdsFramework {

class LDDSCORE_EXPORT LDds final
{
public:
    using TopicCallback = std::function<void(uint32_t topic, const std::shared_ptr<void> & object)>;
    using DeadlineMissedCallback = std::function<void(uint32_t topic, uint64_t elapsedMs)>;
    using LogCallback = std::function<void(const std::string & line)>;

    LDds();
    ~LDds();

    LDds(const LDds & other) = delete;
    LDds & operator=(const LDds & other) = delete;

    bool initialize(const LQos & qos);
    bool initialize(const LQos & qos, const TransportConfig & transportConfig, DomainId domainId = INVALID_DOMAIN_ID);
    bool initializeFromQosXml(
        const std::string & qosXmlPath,
        const TransportConfig & transportConfig = TransportConfig(),
        DomainId domainId = INVALID_DOMAIN_ID
    );
    void shutdown() noexcept;

    bool isRunning() const noexcept;

    void setTypeRegistry(std::shared_ptr<LTypeRegistry> typeRegistry);
    std::shared_ptr<LTypeRegistry> getTypeRegistry() const;

    bool registerType(
        const std::string & typeName,
        uint32_t topic,
        LTypeRegistry::TypeFactory factory,
        LTypeRegistry::SerializeFn serializer,
        LTypeRegistry::DeserializeFn deserializer
    );

    template<typename T>
    bool registerType(const std::string & typeName, uint32_t topic)
    {
        const bool ok = m_pTypeRegistry->registerType<T>(typeName, topic);
        if (ok)
        {
            rememberKnownTopic(topic);
        }
        return ok;
    }

    template<typename T, typename Serializer, typename Deserializer>
    bool registerType(
        const std::string & typeName,
        uint32_t topic,
        Serializer && serializer,
        Deserializer && deserializer
    )
    {
        const bool ok = m_pTypeRegistry->registerType<T>(
            typeName,
            topic,
            std::forward<Serializer>(serializer),
            std::forward<Deserializer>(deserializer)
        );
        if (ok)
        {
            rememberKnownTopic(topic);
        }
        return ok;
    }

    bool publishTopic(uint32_t topic, const std::vector<uint8_t> & payload);
    bool publishTopic(const std::string & typeName, const std::shared_ptr<void> & object);

    template<typename T>
    bool publishTopic(const std::string & typeName, const T & object)
    {
        const uint32_t topic = m_pTypeRegistry->getTopicByTypeName(typeName);
        if (topic == 0)
        {
            setLastError("topic not registered for type: " + typeName);
            return false;
        }

        std::vector<uint8_t> payload;
        if (!m_pTypeRegistry->serializeByTopic(topic, &object, payload))
        {
            setLastError("serialize failed for topic=" + std::to_string(topic));
            return false;
        }

        return publishSerializedTopic(topic, std::move(payload), typeName);
    }

    template<typename T>
    bool publishTopicByTopic(uint32_t topic, const T & object)
    {
        std::vector<uint8_t> payload;
        if (!m_pTypeRegistry->serializeByTopic(topic, &object, payload))
        {
            setLastError("serialize failed for topic=" + std::to_string(topic));
            return false;
        }

        const std::string typeName = m_pTypeRegistry->getTypeNameByTopic(topic);
        return publishSerializedTopic(topic, std::move(payload), typeName);
    }

    void subscribeTopic(uint32_t topic, TopicCallback callback);

    template<typename T>
    void subscribeTopic(uint32_t topic, std::function<void(const T &)> callback)
    {
        subscribeTopic(
            topic,
            [cb = std::move(callback)](uint32_t, const std::shared_ptr<void> & object) {
                if (!cb || !object)
                {
                    return;
                }
                cb(*std::static_pointer_cast<T>(object));
            }
        );
    }

    void unsubscribeTopic(uint32_t topic);
    void setDeadlineMissedCallback(DeadlineMissedCallback callback);
    void setLogCallback(LogCallback callback);
    std::string exportMetricsText() const;

    const LQos & getQos() const noexcept;
    TransportProtocol getTransportProtocol() const noexcept;

    LDomain & domain() noexcept;
    const LDomain & domain() const noexcept;

    std::string getLastError() const;

private:
    struct DiscoveryAnnounce
    {
        uint8_t version;
        uint32_t nodeId;
        uint8_t domainId;
        quint16 endpointPort;
        uint32_t capabilities;
        std::vector<uint32_t> topics;
    };

    struct DiscoveryPeerInfo
    {
        QHostAddress address;
        quint16 endpointPort;
        std::chrono::steady_clock::time_point lastSeen;
        std::vector<uint32_t> topics;
        uint32_t capabilities;
        bool online;
    };

    struct ReliablePendingEntry
    {
        LMessage message;
        std::chrono::steady_clock::time_point lastSendAt;
        uint32_t resendCount;
    };

    struct ReliableReceiveState
    {
        uint64_t expectedSeq;
        std::map<uint64_t, LMessage> bufferedMessages;
    };

    struct OwnershipTopicState
    {
        uint32_t writerId;
        uint32_t strength;
        std::chrono::steady_clock::time_point lastSeen;
    };

    struct SecurityRuntimeConfig
    {
        bool enabled;
        bool encryptPayload;
        std::string psk;
    };

    struct RuntimeMetrics
    {
        std::atomic<uint64_t> sentMessages;
        std::atomic<uint64_t> receivedMessages;
        std::atomic<uint64_t> estimatedDrops;
        std::atomic<uint64_t> retransmitCount;
        std::atomic<uint64_t> deadlineMissCount;
        std::atomic<uint64_t> authRejectedCount;
        std::atomic<uint64_t> sentBytes;
        std::atomic<uint64_t> receivedBytes;
        std::atomic<uint64_t> queueDropCount;
        std::atomic<uint64_t> queueLength;
        std::atomic<uint64_t> connectionCount;

        RuntimeMetrics() noexcept
            : sentMessages(0)
            , receivedMessages(0)
            , estimatedDrops(0)
            , retransmitCount(0)
            , deadlineMissCount(0)
            , authRejectedCount(0)
            , sentBytes(0)
            , receivedBytes(0)
            , queueDropCount(0)
            , queueLength(0)
            , connectionCount(0)
        {
        }
    };

    bool createTransportFromQos(const LQos & qos, const TransportConfig & transportConfig);
    bool sendMessageThroughTransport(
        const LMessage & message,
        const QHostAddress * targetAddress = nullptr,
        quint16 targetPort = 0);
    void initializeReliableState();
    void clearReliableState() noexcept;
    void processReliableOutgoing(const std::chrono::steady_clock::time_point & now);
    void handleReliableControlMessage(
        const LMessage & message,
        const QHostAddress & senderAddress,
        quint16 senderPort);
    void handleReliableDataMessage(
        const LMessage & message,
        const QHostAddress & senderAddress,
        quint16 senderPort);
    void deliverDataMessage(const LMessage & message);
    bool isDiscoveryMessage(const LMessage & message) const noexcept;
    bool encodeDiscoveryAnnounce(std::vector<uint8_t> & payload) const;
    bool decodeDiscoveryAnnounce(
        const LMessage & message,
        DiscoveryAnnounce & announce) const;
    void handleDiscoveryMessage(
        const LMessage & message,
        const QHostAddress & senderAddress,
        quint16 senderPort);
    void initializeDiscoveryState(const TransportConfig & transportConfig);
    void clearDiscoveryState() noexcept;
    void processDiscovery(const std::chrono::steady_clock::time_point & now);
    std::vector<std::pair<QHostAddress, quint16>> snapshotDiscoveryTargets(uint32_t topic) const;
    void rememberKnownTopic(uint32_t topic);
    std::vector<uint32_t> snapshotKnownTopics() const;
    bool shouldAcceptMessageByOwnership(const LMessage & message, uint32_t topic);
    void initializeQosHotReload(const std::string & qosXmlPath);
    void clearQosHotReloadState() noexcept;
    void processQosHotReload(const std::chrono::steady_clock::time_point & now);
    bool applyHotReloadQos(const LQos & loadedQos);
    SecurityRuntimeConfig snapshotSecurityConfig() const;
    bool applyOutgoingSecurity(LMessage & message);
    bool verifyIncomingSecurity(LMessage & message);
    void updateDropEstimate(const LMessage & message, const QHostAddress & senderAddress, quint16 senderPort);
    void updateRuntimeGauges();
    void resetRuntimeMetrics() noexcept;
    void emitStructuredLog(
        const char * level,
        const char * module,
        const std::string & text,
        const LMessage * message = nullptr,
        const QHostAddress * peerAddress = nullptr,
        quint16 peerPort = 0);
    std::string makeMessageId(const LMessage & message) const;
    uint32_t resolveReliableWriterId(
        const LMessage & message,
        const QHostAddress & senderAddress,
        quint16 senderPort) const;
    bool publishSerializedTopic(
        uint32_t                topic,
        std::vector<uint8_t> && payload,
        const std::string &     dataType
    );
    void handleTransportMessage(const LMessage & message, const QHostAddress & senderAddress, quint16 senderPort);
    void markTopicActivity(uint32_t topic);
    void startQosThread();
    void stopQosThread() noexcept;
    void qosThreadFunc();

    void setLastError(const std::string & message);

private:
    LQos                        m_qos;
    LDomain                     m_domain;
    DomainId                    m_effectiveDomainId;
    std::unique_ptr<ITransport> m_pTransport;
    std::shared_ptr<LTypeRegistry> m_pTypeRegistry;

    std::atomic<bool>     m_running;
    std::atomic<uint64_t> m_sequence;
    std::atomic<bool>     m_qosThreadRunning;

    std::unordered_map<uint32_t, std::vector<TopicCallback>> m_subscribers;
    mutable std::mutex                                        m_subscribersMutex;
    DeadlineMissedCallback                                    m_deadlineMissedCallback;

    mutable std::mutex m_errorMutex;
    std::string        m_lastError;

    std::thread              m_qosThread;
    std::condition_variable  m_qosCondition;
    mutable std::mutex       m_qosMutex;
    std::unordered_map<uint32_t, std::chrono::steady_clock::time_point> m_lastTopicActivity;
    std::unordered_set<uint32_t> m_deadlineMissedTopics;
    std::chrono::steady_clock::time_point m_lastHeartbeatSend;
    std::chrono::steady_clock::time_point m_lastHeartbeatReceive;
    std::chrono::milliseconds m_deadlineCheckInterval;
    std::chrono::milliseconds m_heartbeatInterval;

    bool m_reliableUdpEnabled;
    uint32_t m_reliableWriterId;
    std::chrono::milliseconds m_reliableRetransmitInterval;
    std::chrono::milliseconds m_reliableHeartbeatProbeInterval;
    uint32_t m_reliableWindowSize;
    uint32_t m_reliableMaxResendCount;
    std::chrono::steady_clock::time_point m_lastReliableHeartbeatProbe;
    std::map<uint64_t, ReliablePendingEntry> m_reliablePending;
    std::unordered_map<uint32_t, ReliableReceiveState> m_reliableReceivers;
    mutable std::mutex m_reliableMutex;

    bool m_discoveryEnabled;
    bool m_discoveryUseMulticast;
    uint32_t m_discoveryNodeId;
    quint16 m_discoveryPort;
    std::chrono::milliseconds m_discoveryInterval;
    std::chrono::milliseconds m_peerTtl;
    std::chrono::steady_clock::time_point m_lastDiscoverySend;
    QHostAddress m_discoveryMulticastGroup;
    std::unordered_map<uint32_t, DiscoveryPeerInfo> m_discoveryPeers;
    mutable std::mutex m_discoveryMutex;

    std::unordered_set<uint32_t> m_knownTopics;
    mutable std::mutex m_knownTopicsMutex;

    std::unordered_map<uint32_t, OwnershipTopicState> m_topicOwnership;
    mutable std::mutex m_ownershipMutex;

    bool m_qosHotReloadEnabled;
    std::string m_qosXmlPath;
    int64_t m_qosLastWriteTick;
    std::chrono::milliseconds m_qosReloadInterval;
    std::chrono::steady_clock::time_point m_lastQosReloadCheck;
    mutable std::mutex m_qosReloadMutex;

    RuntimeMetrics m_metrics;
    mutable std::mutex m_metricsMutex;
    std::unordered_map<uint32_t, uint64_t> m_lossEstimateByWriter;

    mutable std::mutex m_securityMutex;
    SecurityRuntimeConfig m_securityConfig;

    mutable std::mutex m_logMutex;
    bool m_structuredLogEnabled;
    LogCallback m_logCallback;
};

const char * getVersion() noexcept;
const char * getBuildTime() noexcept;

bool initialize() noexcept;
void shutdown() noexcept;
bool isInitialized() noexcept;

} // namespace LDdsFramework
