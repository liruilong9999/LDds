#include "LDds.h"
#include "LUdpTransport.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <exception>
#include <limits>
#include <string>
#include <utility>

namespace LDdsFramework {
namespace {

std::atomic<bool> g_initialized(false);

constexpr uint32_t RELIABLE_MIN_WINDOW_SIZE = 16U;
constexpr uint32_t RELIABLE_MAX_WINDOW_SIZE = 4096U;
constexpr uint32_t RELIABLE_DEFAULT_MAX_RESEND = 32U;
constexpr int32_t RELIABLE_DEFAULT_RETRANSMIT_MS = 200;
constexpr int32_t RELIABLE_MIN_RETRANSMIT_MS = 80;
constexpr int32_t RELIABLE_MAX_RETRANSMIT_MS = 1000;
constexpr int32_t RELIABLE_MIN_HEARTBEAT_PROBE_MS = 300;
constexpr uint8_t DISCOVERY_ANNOUNCE_VERSION = 1U;
constexpr uint32_t DISCOVERY_CAP_RELIABLE_UDP = 0x01U;
constexpr uint32_t DISCOVERY_CAP_TCP = 0x02U;
constexpr uint32_t DISCOVERY_CAP_MULTICAST = 0x04U;
constexpr int32_t DISCOVERY_MIN_INTERVAL_MS = 300;
constexpr int32_t DISCOVERY_MIN_PEER_TTL_MS = 1000;
constexpr int32_t DISCOVERY_MAX_TOPICS = 1024;

void appendU8(std::vector<uint8_t> & out, uint8_t value)
{
    out.push_back(value);
}

void appendU16(std::vector<uint8_t> & out, uint16_t value)
{
    out.push_back(static_cast<uint8_t>(value & 0xFFU));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFFU));
}

void appendU32(std::vector<uint8_t> & out, uint32_t value)
{
    out.push_back(static_cast<uint8_t>(value & 0xFFU));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFFU));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFFU));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFFU));
}

bool readU8(const std::vector<uint8_t> & data, size_t & offset, uint8_t & value)
{
    if (offset + 1 > data.size())
    {
        return false;
    }
    value = data[offset];
    ++offset;
    return true;
}

bool readU16(const std::vector<uint8_t> & data, size_t & offset, uint16_t & value)
{
    if (offset + 2 > data.size())
    {
        return false;
    }
    value = static_cast<uint16_t>(data[offset]) |
            static_cast<uint16_t>(static_cast<uint16_t>(data[offset + 1]) << 8);
    offset += 2;
    return true;
}

bool readU32(const std::vector<uint8_t> & data, size_t & offset, uint32_t & value)
{
    if (offset + 4 > data.size())
    {
        return false;
    }
    value = static_cast<uint32_t>(data[offset]) |
            (static_cast<uint32_t>(data[offset + 1]) << 8) |
            (static_cast<uint32_t>(data[offset + 2]) << 16) |
            (static_cast<uint32_t>(data[offset + 3]) << 24);
    offset += 4;
    return true;
}

QHostAddress resolveDomainMulticastGroup(DomainId domainId)
{
    return QHostAddress(QStringLiteral("239.255.0.%1").arg(static_cast<uint32_t>(domainId)));
}

uint32_t fnv1aHash32(const std::string & value)
{
    uint32_t hash = 2166136261U;
    for (const unsigned char ch : value)
    {
        hash ^= static_cast<uint32_t>(ch);
        hash *= 16777619U;
    }
    return (hash == 0U) ? 1U : hash;
}

int32_t resolveDeadlineMs(const LQos & qos)
{
    if (qos.deadlineMs > 0)
    {
        return qos.deadlineMs;
    }

    const DeadlineQosPolicy & policy = qos.getDeadline();
    if (!policy.enabled || policy.period.isInfinite())
    {
        return 0;
    }

    const int64_t millis =
        (policy.period.seconds * 1000LL) + static_cast<int64_t>(policy.period.nanoseconds / 1000000U);
    return millis > 0 ? static_cast<int32_t>(millis) : 0;
}

std::chrono::milliseconds resolveHeartbeatInterval(int32_t deadlineMs)
{
    if (deadlineMs <= 0)
    {
        return std::chrono::milliseconds(1000);
    }

    const int32_t candidate = std::max(200, std::min(2000, deadlineMs / 3));
    return std::chrono::milliseconds(candidate > 0 ? candidate : 200);
}

DomainId resolveEffectiveDomainId(const LQos & qos, DomainId requestedDomainId)
{
    if (requestedDomainId != INVALID_DOMAIN_ID)
    {
        return requestedDomainId;
    }
    return static_cast<DomainId>(qos.domainId);
}

bool applyDomainPortMapping(
    TransportConfig & config,
    DomainId          domainId,
    std::string &     errorMessage)
{
    if (!config.enableDomainPortMapping)
    {
        return true;
    }

    if (config.basePort == 0)
    {
        errorMessage = "basePort must be > 0 when enableDomainPortMapping=true";
        return false;
    }
    if (config.domainPortOffset == 0)
    {
        errorMessage = "domainPortOffset must be > 0 when enableDomainPortMapping=true";
        return false;
    }

    const uint32_t mappedPort =
        static_cast<uint32_t>(config.basePort) +
        (static_cast<uint32_t>(domainId) * static_cast<uint32_t>(config.domainPortOffset));
    if (mappedPort > 65535U)
    {
        errorMessage = "mapped port exceeds 65535";
        return false;
    }

    config.bindPort = static_cast<quint16>(mappedPort);
    if (!config.remoteAddress.isEmpty() || config.remotePort != 0)
    {
        config.remotePort = static_cast<quint16>(mappedPort);
    }

    errorMessage.clear();
    return true;
}

} // namespace

LDds::LDds()
    : m_qos()
    , m_domain()
    , m_effectiveDomainId(DEFAULT_DOMAIN_ID)
    , m_pTransport()
    , m_pTypeRegistry(std::make_shared<LTypeRegistry>())
    , m_running(false)
    , m_sequence(0)
    , m_qosThreadRunning(false)
    , m_subscribers()
    , m_subscribersMutex()
    , m_deadlineMissedCallback()
    , m_errorMutex()
    , m_lastError()
    , m_qosThread()
    , m_qosCondition()
    , m_qosMutex()
    , m_lastTopicActivity()
    , m_deadlineMissedTopics()
    , m_lastHeartbeatSend(std::chrono::steady_clock::now())
    , m_lastHeartbeatReceive(std::chrono::steady_clock::now())
    , m_deadlineCheckInterval(200)
    , m_heartbeatInterval(1000)
    , m_reliableUdpEnabled(false)
    , m_reliableWriterId(1)
    , m_reliableRetransmitInterval(RELIABLE_DEFAULT_RETRANSMIT_MS)
    , m_reliableHeartbeatProbeInterval(std::chrono::milliseconds(RELIABLE_MIN_HEARTBEAT_PROBE_MS * 2))
    , m_reliableWindowSize(RELIABLE_MIN_WINDOW_SIZE)
    , m_reliableMaxResendCount(RELIABLE_DEFAULT_MAX_RESEND)
    , m_lastReliableHeartbeatProbe(std::chrono::steady_clock::now())
    , m_reliablePending()
    , m_reliableReceivers()
    , m_reliableMutex()
    , m_discoveryEnabled(false)
    , m_discoveryUseMulticast(false)
    , m_discoveryNodeId(1)
    , m_discoveryPort(0)
    , m_discoveryInterval(1000)
    , m_peerTtl(5000)
    , m_lastDiscoverySend(std::chrono::steady_clock::now())
    , m_discoveryMulticastGroup()
    , m_discoveryPeers()
    , m_discoveryMutex()
    , m_knownTopics()
    , m_knownTopicsMutex()
{
}

LDds::~LDds()
{
    shutdown();
}

bool LDds::initialize(const LQos & qos)
{
    TransportConfig config;
    return initialize(qos, config, INVALID_DOMAIN_ID);
}

bool LDds::initialize(const LQos & qos, const TransportConfig & transportConfig, DomainId domainId)
{
    if (m_running.load())
    {
        return true;
    }

    LQos effectiveQos = qos;
    const DomainId effectiveDomainId = resolveEffectiveDomainId(qos, domainId);
    if (effectiveDomainId > 255U)
    {
        setLastError("invalid domainId=" + std::to_string(effectiveDomainId) + ", must be in [0,255]");
        return false;
    }

    effectiveQos.domainId = static_cast<uint8_t>(effectiveDomainId);
    effectiveQos.historyDepth = (effectiveQos.historyDepth <= 0) ? 1 : effectiveQos.historyDepth;
    effectiveQos.deadlineMs = std::max(0, effectiveQos.deadlineMs);

    HistoryQosPolicy history = effectiveQos.getHistory();
    history.depth = effectiveQos.historyDepth;
    history.enabled = true;
    effectiveQos.setHistory(history);

    DeadlineQosPolicy deadline = effectiveQos.getDeadline();
    deadline.enabled = (effectiveQos.deadlineMs > 0);
    if (deadline.enabled)
    {
        deadline.period.seconds = static_cast<int64_t>(effectiveQos.deadlineMs / 1000);
        deadline.period.nanoseconds = static_cast<uint32_t>((effectiveQos.deadlineMs % 1000) * 1000000);
    }
    else
    {
        deadline.period = Duration(DURATION_INFINITY);
    }
    effectiveQos.setDeadline(deadline);

    ReliabilityQosPolicy reliability = effectiveQos.getReliability();
    reliability.enabled = effectiveQos.reliable;
    reliability.kind = effectiveQos.reliable ? ReliabilityKind::Reliable : ReliabilityKind::BestEffort;
    effectiveQos.setReliability(reliability);

    std::string validateError;
    if (!effectiveQos.validate(validateError))
    {
        setLastError(
            "invalid qos (domain=" + std::to_string(effectiveDomainId) + "): " + validateError);
        return false;
    }

    TransportConfig effectiveTransportConfig = transportConfig;
    if (!effectiveTransportConfig.enableDomainPortMapping && effectiveQos.enableDomainPortMapping)
    {
        effectiveTransportConfig.enableDomainPortMapping = true;
        effectiveTransportConfig.basePort = effectiveQos.basePort;
        effectiveTransportConfig.domainPortOffset = effectiveQos.domainPortOffset;
    }

    const bool isUdpTransport = (effectiveQos.transportType == TransportType::UDP);
    if (isUdpTransport && effectiveTransportConfig.enableDiscovery)
    {
        const bool hasConfiguredRemote =
            !effectiveTransportConfig.remoteAddress.isEmpty() &&
            effectiveTransportConfig.remotePort != 0;
        if (hasConfiguredRemote)
        {
            // Preserve legacy point-to-point behavior when remote is explicitly configured.
            effectiveTransportConfig.enableDiscovery = false;
        }

        if (effectiveTransportConfig.enableDiscovery &&
            effectiveTransportConfig.discoveryIntervalMs < DISCOVERY_MIN_INTERVAL_MS)
        {
            effectiveTransportConfig.discoveryIntervalMs = DISCOVERY_MIN_INTERVAL_MS;
        }
        if (effectiveTransportConfig.enableDiscovery &&
            effectiveTransportConfig.peerTtlMs < DISCOVERY_MIN_PEER_TTL_MS)
        {
            effectiveTransportConfig.peerTtlMs = DISCOVERY_MIN_PEER_TTL_MS;
        }

        if (effectiveTransportConfig.enableDiscovery &&
            effectiveTransportConfig.bindPort == 0 &&
            !effectiveTransportConfig.enableDomainPortMapping)
        {
            effectiveTransportConfig.enableDomainPortMapping = true;
            if (effectiveTransportConfig.basePort == 0)
            {
                effectiveTransportConfig.basePort = 20000;
            }
            if (effectiveTransportConfig.domainPortOffset == 0)
            {
                effectiveTransportConfig.domainPortOffset = 10;
            }
        }

        if (effectiveTransportConfig.enableDiscovery)
        {
            effectiveTransportConfig.enableBroadcast = true;
        }
        if (effectiveTransportConfig.enableDiscovery &&
            effectiveTransportConfig.enableMulticast &&
            effectiveTransportConfig.multicastGroup.isEmpty())
        {
            effectiveTransportConfig.multicastGroup =
                resolveDomainMulticastGroup(effectiveDomainId).toString();
        }
    }

    std::string mappingError;
    if (!applyDomainPortMapping(effectiveTransportConfig, effectiveDomainId, mappingError))
    {
        setLastError(
            "invalid transport config (domain=" + std::to_string(effectiveDomainId) + "): " +
            mappingError);
        return false;
    }

    if (!m_domain.isValid() || m_domain.getDomainId() != effectiveDomainId)
    {
        m_domain.destroy();
        if (!m_domain.create(effectiveDomainId, &effectiveQos))
        {
            setLastError("failed to create domain=" + std::to_string(effectiveDomainId));
            return false;
        }
    }

    m_qos = effectiveQos;
    m_effectiveDomainId = effectiveDomainId;

    if (!createTransportFromQos(effectiveQos, effectiveTransportConfig))
    {
        return false;
    }

    m_pTransport->setReceiveCallback(
        [this](const LMessage & message, const QHostAddress & senderAddress, quint16 senderPort) {
            handleTransportMessage(message, senderAddress, senderPort);
        }
    );

    if (!m_pTransport->start())
    {
        setLastError(
            "failed to start transport (domain=" + std::to_string(m_effectiveDomainId) + "): " +
            m_pTransport->getLastError().toStdString());
        m_pTransport.reset();
        return false;
    }

    initializeReliableState();
    initializeDiscoveryState(effectiveTransportConfig);

    m_sequence.store(0);
    m_running.store(true);

    {
        std::lock_guard<std::mutex> lock(m_qosMutex);
        m_lastTopicActivity.clear();
        m_deadlineMissedTopics.clear();
        m_lastHeartbeatSend = std::chrono::steady_clock::now();
        m_lastHeartbeatReceive = m_lastHeartbeatSend;
    }

    startQosThread();
    return true;
}

bool LDds::initializeFromQosXml(
    const std::string & qosXmlPath,
    const TransportConfig & transportConfig,
    DomainId domainId)
{
    LQos parsedQos;
    std::string error;
    if (!parsedQos.loadFromXmlFile(qosXmlPath, &error))
    {
        setLastError("failed to load qos xml: " + error);
        return false;
    }
    return initialize(parsedQos, transportConfig, domainId);
}

void LDds::shutdown() noexcept
{
    m_running.store(false);
    stopQosThread();
    clearReliableState();
    clearDiscoveryState();

    if (m_pTransport)
    {
        m_pTransport->stop();
        m_pTransport.reset();
    }

    {
        std::lock_guard<std::mutex> lock(m_subscribersMutex);
        m_subscribers.clear();
    }

    {
        std::lock_guard<std::mutex> lock(m_qosMutex);
        m_lastTopicActivity.clear();
        m_deadlineMissedTopics.clear();
    }

    {
        std::lock_guard<std::mutex> lock(m_knownTopicsMutex);
        m_knownTopics.clear();
    }

    m_domain.destroy();
    m_effectiveDomainId = DEFAULT_DOMAIN_ID;
    m_sequence.store(0);
}

bool LDds::isRunning() const noexcept
{
    return m_running.load();
}

void LDds::setTypeRegistry(std::shared_ptr<LTypeRegistry> typeRegistry)
{
    if (typeRegistry)
    {
        m_pTypeRegistry = std::move(typeRegistry);
    }
}

std::shared_ptr<LTypeRegistry> LDds::getTypeRegistry() const
{
    return m_pTypeRegistry;
}

bool LDds::registerType(
    const std::string &         typeName,
    uint32_t                    topic,
    LTypeRegistry::TypeFactory  factory,
    LTypeRegistry::SerializeFn  serializer,
    LTypeRegistry::DeserializeFn deserializer
)
{
    const bool ok = m_pTypeRegistry->registerType(
        typeName,
        topic,
        std::move(factory),
        std::move(serializer),
        std::move(deserializer)
    );
    if (ok)
    {
        rememberKnownTopic(topic);
    }
    return ok;
}

bool LDds::publishTopic(uint32_t topic, const std::vector<uint8_t> & payload)
{
    const std::string typeName = m_pTypeRegistry->getTypeNameByTopic(topic);
    return publishSerializedTopic(topic, std::vector<uint8_t>(payload), typeName);
}

bool LDds::publishTopic(const std::string & typeName, const std::shared_ptr<void> & object)
{
    if (!object)
    {
        setLastError("publish object is null");
        return false;
    }

    const uint32_t topic = m_pTypeRegistry->getTopicByTypeName(typeName);
    if (topic == 0)
    {
        setLastError("topic not registered for type: " + typeName);
        return false;
    }

    std::vector<uint8_t> payload;
    if (!m_pTypeRegistry->serializeByTopic(topic, object.get(), payload))
    {
        setLastError("serialize failed for topic=" + std::to_string(topic));
        return false;
    }

    return publishSerializedTopic(topic, std::move(payload), typeName);
}

void LDds::subscribeTopic(uint32_t topic, TopicCallback callback)
{
    if (topic == 0 || !callback)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(m_subscribersMutex);
    m_subscribers[topic].push_back(std::move(callback));
    rememberKnownTopic(topic);
}

void LDds::unsubscribeTopic(uint32_t topic)
{
    std::lock_guard<std::mutex> lock(m_subscribersMutex);
    m_subscribers.erase(topic);
}

void LDds::setDeadlineMissedCallback(DeadlineMissedCallback callback)
{
    std::lock_guard<std::mutex> lock(m_qosMutex);
    m_deadlineMissedCallback = std::move(callback);
}

const LQos & LDds::getQos() const noexcept
{
    return m_qos;
}

TransportProtocol LDds::getTransportProtocol() const noexcept
{
    if (!m_pTransport)
    {
        return (m_qos.transportType == TransportType::TCP)
            ? TransportProtocol::TCP
            : TransportProtocol::UDP;
    }
    return m_pTransport->getProtocol();
}

LDomain & LDds::domain() noexcept
{
    return m_domain;
}

const LDomain & LDds::domain() const noexcept
{
    return m_domain;
}

std::string LDds::getLastError() const
{
    std::lock_guard<std::mutex> lock(m_errorMutex);
    return m_lastError;
}

bool LDds::createTransportFromQos(const LQos & qos, const TransportConfig & transportConfig)
{
    const TransportProtocol protocol =
        (qos.transportType == TransportType::TCP) ? TransportProtocol::TCP : TransportProtocol::UDP;

    m_pTransport = ITransport::createTransport(protocol);
    if (!m_pTransport)
    {
        setLastError("failed to create transport");
        return false;
    }

    m_pTransport->setConfig(transportConfig);
    return true;
}

bool LDds::sendMessageThroughTransport(
    const LMessage & message,
    const QHostAddress * targetAddress,
    quint16 targetPort)
{
    if (!m_pTransport)
    {
        return false;
    }

    if (targetAddress != nullptr && !targetAddress->isNull() && targetPort != 0)
    {
        if (m_pTransport->getProtocol() == TransportProtocol::UDP)
        {
            auto * udpTransport = dynamic_cast<LUdpTransport *>(m_pTransport.get());
            if (udpTransport != nullptr)
            {
                return udpTransport->sendMessageTo(message, *targetAddress, targetPort);
            }
        }
    }

    return m_pTransport->sendMessage(message);
}

void LDds::initializeReliableState()
{
    const auto now = std::chrono::steady_clock::now();
    const bool reliableEnabled =
        m_qos.reliable &&
        m_pTransport &&
        m_pTransport->getProtocol() == TransportProtocol::UDP;

    const int32_t deadlineMs = resolveDeadlineMs(m_qos);
    int32_t retransmitMs = RELIABLE_DEFAULT_RETRANSMIT_MS;
    if (deadlineMs > 0)
    {
        retransmitMs = std::max(
            RELIABLE_MIN_RETRANSMIT_MS,
            std::min(RELIABLE_MAX_RETRANSMIT_MS, deadlineMs / 2));
    }
    const int32_t heartbeatProbeMs = std::max(
        RELIABLE_MIN_HEARTBEAT_PROBE_MS,
        retransmitMs * 2);

    const uint32_t historyDepth = static_cast<uint32_t>(
        std::max(1, m_qos.historyDepth));
    const uint32_t windowSize = std::max(
        RELIABLE_MIN_WINDOW_SIZE,
        std::min(RELIABLE_MAX_WINDOW_SIZE, historyDepth * 8U));

    quint16 bindPort = 0;
    std::string bindAddress;
    if (m_pTransport)
    {
        bindPort = m_pTransport->getBoundPort();
        const TransportConfig config = m_pTransport->getConfig();
        if (bindPort == 0)
        {
            bindPort = config.bindPort;
        }
        bindAddress = config.bindAddress.toStdString();
    }

    const std::string writerSeed =
        std::to_string(static_cast<uint32_t>(m_effectiveDomainId)) +
        "|" + bindAddress +
        "|" + std::to_string(static_cast<uint32_t>(bindPort)) +
        "|" + std::to_string(static_cast<uintptr_t>(reinterpret_cast<uintptr_t>(this)));
    const uint32_t writerId = fnv1aHash32(writerSeed);

    std::lock_guard<std::mutex> lock(m_reliableMutex);
    m_reliableUdpEnabled = reliableEnabled;
    m_reliableWriterId = writerId;
    m_reliableRetransmitInterval = std::chrono::milliseconds(retransmitMs);
    m_reliableHeartbeatProbeInterval = std::chrono::milliseconds(heartbeatProbeMs);
    m_reliableWindowSize = windowSize;
    m_reliableMaxResendCount = RELIABLE_DEFAULT_MAX_RESEND;
    m_lastReliableHeartbeatProbe = now;
    m_reliablePending.clear();
    m_reliableReceivers.clear();
}

void LDds::clearReliableState() noexcept
{
    std::lock_guard<std::mutex> lock(m_reliableMutex);
    m_reliableUdpEnabled = false;
    m_reliableWriterId = 1;
    m_reliablePending.clear();
    m_reliableReceivers.clear();
    m_lastReliableHeartbeatProbe = std::chrono::steady_clock::now();
}

void LDds::processReliableOutgoing(const std::chrono::steady_clock::time_point & now)
{
    if (!m_running.load() || !m_pTransport)
    {
        return;
    }

    std::vector<LMessage> retryMessages;
    bool shouldProbe = false;
    uint64_t firstPendingSeq = 0;
    uint64_t lastPendingSeq = 0;

    {
        std::lock_guard<std::mutex> lock(m_reliableMutex);
        if (!m_reliableUdpEnabled)
        {
            return;
        }

        for (auto it = m_reliablePending.begin(); it != m_reliablePending.end();)
        {
            ReliablePendingEntry & entry = it->second;
            if (now - entry.lastSendAt >= m_reliableRetransmitInterval)
            {
                if (m_reliableMaxResendCount > 0 && entry.resendCount >= m_reliableMaxResendCount)
                {
                    setLastError(
                        "reliable udp drop pending seq=" + std::to_string(it->first) +
                        ", maxResendCount=" + std::to_string(m_reliableMaxResendCount));
                    it = m_reliablePending.erase(it);
                    continue;
                }

                entry.lastSendAt = now;
                ++entry.resendCount;
                retryMessages.push_back(entry.message);
            }
            ++it;
        }

        if (!m_reliablePending.empty() &&
            now - m_lastReliableHeartbeatProbe >= m_reliableHeartbeatProbeInterval)
        {
            firstPendingSeq = m_reliablePending.begin()->first;
            lastPendingSeq = m_reliablePending.rbegin()->first;
            m_lastReliableHeartbeatProbe = now;
            shouldProbe = true;
        }
    }

    for (const LMessage & pending : retryMessages)
    {
        if (!sendMessageThroughTransport(pending))
        {
            setLastError(
                "reliable udp retransmit failed seq=" + std::to_string(pending.getSequence()) +
                ", error=" + m_pTransport->getLastError().toStdString());
        }
    }

    if (shouldProbe)
    {
        LMessage heartbeatReq = LMessage::makeHeartbeatReq(m_reliableWriterId, firstPendingSeq, lastPendingSeq);
        heartbeatReq.setDomainId(static_cast<uint8_t>(m_effectiveDomainId));
        heartbeatReq.setSequence(m_sequence.fetch_add(1) + 1);
        if (!sendMessageThroughTransport(heartbeatReq))
        {
            setLastError(
                "reliable udp heartbeat probe failed: " + m_pTransport->getLastError().toStdString());
        }
    }
}

void LDds::handleReliableControlMessage(
    const LMessage & message,
    const QHostAddress & senderAddress,
    quint16 senderPort)
{
    if (!m_running.load())
    {
        return;
    }

    bool reliableUdpEnabled = false;
    {
        std::lock_guard<std::mutex> lock(m_reliableMutex);
        reliableUdpEnabled = m_reliableUdpEnabled;
    }
    if (!reliableUdpEnabled)
    {
        return;
    }

    if (message.isHeartbeat())
    {
        std::lock_guard<std::mutex> lock(m_qosMutex);
        m_lastHeartbeatReceive = std::chrono::steady_clock::now();
    }

    const LMessageType type = message.getMessageType();
    if (type == LMessageType::Ack || type == LMessageType::HeartbeatRsp)
    {
        const uint32_t targetWriterId = message.getWriterId();
        if (targetWriterId != 0 && targetWriterId != m_reliableWriterId)
        {
            return;
        }

        const uint64_t ackSeq = message.getAckSeq() > 0 ? message.getAckSeq() : message.getSequence();
        std::lock_guard<std::mutex> lock(m_reliableMutex);
        for (auto it = m_reliablePending.begin(); it != m_reliablePending.end() && it->first <= ackSeq;)
        {
            it = m_reliablePending.erase(it);
        }
        return;
    }

    if (type == LMessageType::Nack)
    {
        const uint32_t targetWriterId = message.getWriterId();
        if (targetWriterId != 0 && targetWriterId != m_reliableWriterId)
        {
            return;
        }

        const uint64_t first = message.getFirstSeq() > 0 ? message.getFirstSeq() : 1;
        const uint64_t last = message.getLastSeq() >= first
            ? message.getLastSeq()
            : std::numeric_limits<uint64_t>::max();
        std::vector<LMessage> nackedMessages;

        {
            std::lock_guard<std::mutex> lock(m_reliableMutex);
            for (auto it = m_reliablePending.lower_bound(first);
                 it != m_reliablePending.end() && it->first <= last;
                 ++it)
            {
                it->second.lastSendAt = std::chrono::steady_clock::now();
                ++it->second.resendCount;
                nackedMessages.push_back(it->second.message);
            }
        }

        for (const LMessage & pending : nackedMessages)
        {
            (void)sendMessageThroughTransport(pending);
        }
        return;
    }

    if (type != LMessageType::HeartbeatReq && type != LMessageType::Heartbeat)
    {
        return;
    }

    const uint32_t remoteWriterId = resolveReliableWriterId(message, senderAddress, senderPort);
    uint64_t ackSeq = 0;
    uint64_t windowStart = 1;

    {
        std::lock_guard<std::mutex> lock(m_reliableMutex);
        ReliableReceiveState & state = m_reliableReceivers[remoteWriterId];
        if (state.expectedSeq == 0)
        {
            state.expectedSeq = (message.getFirstSeq() > 0) ? message.getFirstSeq() : 1;
        }
        windowStart = state.expectedSeq;
        ackSeq = (state.expectedSeq > 0) ? (state.expectedSeq - 1) : 0;
    }

    const LMessage reply = (type == LMessageType::HeartbeatReq)
        ? LMessage::makeHeartbeatRsp(remoteWriterId, ackSeq, windowStart, m_reliableWindowSize)
        : LMessage::makeAck(remoteWriterId, ackSeq, windowStart, m_reliableWindowSize);

    LMessage response = reply;
    response.setDomainId(static_cast<uint8_t>(m_effectiveDomainId));
    response.setSequence(m_sequence.fetch_add(1) + 1);
    (void)sendMessageThroughTransport(response, &senderAddress, senderPort);
}

void LDds::handleReliableDataMessage(
    const LMessage & message,
    const QHostAddress & senderAddress,
    quint16 senderPort)
{
    if (!m_running.load())
    {
        return;
    }

    const uint32_t writerId = resolveReliableWriterId(message, senderAddress, senderPort);
    const uint64_t seq = message.getLastSeq() > 0 ? message.getLastSeq() : message.getSequence();
    if (seq == 0)
    {
        return;
    }

    bool reliableUdpEnabled = false;
    {
        std::lock_guard<std::mutex> lock(m_reliableMutex);
        reliableUdpEnabled = m_reliableUdpEnabled;
    }
    if (!reliableUdpEnabled)
    {
        deliverDataMessage(message);
        return;
    }

    std::vector<LMessage> readyToDeliver;
    bool shouldAck = false;
    uint64_t ackSeq = 0;
    uint64_t windowStart = 1;
    bool shouldSendHeartbeatReq = false;
    uint64_t heartbeatFirst = 0;
    uint64_t heartbeatLast = 0;

    {
        std::lock_guard<std::mutex> lock(m_reliableMutex);
        ReliableReceiveState & state = m_reliableReceivers[writerId];
        if (state.expectedSeq == 0)
        {
            state.expectedSeq = seq;
        }

        const uint64_t maxAcceptableSeq =
            state.expectedSeq + static_cast<uint64_t>(std::max(1U, m_reliableWindowSize));

        if (seq < state.expectedSeq)
        {
            shouldAck = true;
        }
        else if (seq > maxAcceptableSeq)
        {
            shouldAck = true;
            shouldSendHeartbeatReq = true;
            heartbeatFirst = state.expectedSeq;
            heartbeatLast = seq;
        }
        else if (seq == state.expectedSeq)
        {
            readyToDeliver.push_back(message);
            ++state.expectedSeq;

            while (true)
            {
                const auto buffered = state.bufferedMessages.find(state.expectedSeq);
                if (buffered == state.bufferedMessages.end())
                {
                    break;
                }

                readyToDeliver.push_back(buffered->second);
                state.bufferedMessages.erase(buffered);
                ++state.expectedSeq;
            }
            shouldAck = true;
        }
        else
        {
            const auto insertResult = state.bufferedMessages.emplace(seq, message);
            shouldAck = true;
            if (!insertResult.second)
            {
                // Duplicate in receive window; ACK current cumulative sequence.
            }
        }

        while (state.bufferedMessages.size() > static_cast<size_t>(std::max(1U, m_reliableWindowSize)))
        {
            auto newest = state.bufferedMessages.end();
            --newest;
            state.bufferedMessages.erase(newest);
        }

        windowStart = state.expectedSeq;
        ackSeq = (state.expectedSeq > 0) ? (state.expectedSeq - 1) : 0;
    }

    for (const LMessage & pending : readyToDeliver)
    {
        deliverDataMessage(pending);
    }

    if (shouldSendHeartbeatReq)
    {
        LMessage heartbeatReq = LMessage::makeHeartbeatReq(writerId, heartbeatFirst, heartbeatLast);
        heartbeatReq.setDomainId(static_cast<uint8_t>(m_effectiveDomainId));
        heartbeatReq.setSequence(m_sequence.fetch_add(1) + 1);
        (void)sendMessageThroughTransport(heartbeatReq, &senderAddress, senderPort);
    }

    if (shouldAck)
    {
        LMessage ack = LMessage::makeAck(writerId, ackSeq, windowStart, m_reliableWindowSize);
        ack.setDomainId(static_cast<uint8_t>(m_effectiveDomainId));
        ack.setSequence(m_sequence.fetch_add(1) + 1);
        (void)sendMessageThroughTransport(ack, &senderAddress, senderPort);
    }
}

void LDds::deliverDataMessage(const LMessage & message)
{
    const uint32_t topic = message.getTopic();
    if (topic == 0)
    {
        return;
    }

    auto object = m_pTypeRegistry->createByTopic(topic);
    if (!object)
    {
        return;
    }

    if (!m_pTypeRegistry->deserializeByTopic(topic, message.getPayload(), object.get()))
    {
        return;
    }

    const std::string dataType = m_pTypeRegistry->getTypeNameByTopic(topic);
    m_domain.cacheTopicData(static_cast<int>(topic), message.getPayload(), dataType);
    markTopicActivity(topic);

    std::vector<TopicCallback> callbacks;
    {
        std::lock_guard<std::mutex> lock(m_subscribersMutex);
        const auto it = m_subscribers.find(topic);
        if (it != m_subscribers.end())
        {
            callbacks = it->second;
        }
    }

    for (auto & callback : callbacks)
    {
        if (callback)
        {
            callback(topic, object);
        }
    }
}

bool LDds::isDiscoveryMessage(const LMessage & message) const noexcept
{
    return message.getMessageType() == LMessageType::DiscoveryAnnounce;
}

bool LDds::encodeDiscoveryAnnounce(std::vector<uint8_t> & payload) const
{
    payload.clear();

    quint16 endpointPort = 0;
    bool discoveryEnabled = false;
    bool useMulticast = false;
    uint32_t nodeId = 0;
    quint16 announcePort = 0;
    {
        std::lock_guard<std::mutex> lock(m_discoveryMutex);
        discoveryEnabled = m_discoveryEnabled;
        useMulticast = m_discoveryUseMulticast;
        nodeId = m_discoveryNodeId;
        announcePort = m_discoveryPort;
    }

    if (!discoveryEnabled || !m_pTransport)
    {
        return false;
    }

    endpointPort = m_pTransport->getBoundPort();
    if (endpointPort == 0)
    {
        endpointPort = announcePort;
    }
    if (endpointPort == 0)
    {
        return false;
    }

    const std::vector<uint32_t> topics = snapshotKnownTopics();
    const uint16_t topicCount = static_cast<uint16_t>(
        std::min(static_cast<size_t>(DISCOVERY_MAX_TOPICS), topics.size()));

    uint32_t capabilities = 0;
    if (m_qos.reliable && m_pTransport->getProtocol() == TransportProtocol::UDP)
    {
        capabilities |= DISCOVERY_CAP_RELIABLE_UDP;
    }
    if (m_pTransport->getProtocol() == TransportProtocol::TCP)
    {
        capabilities |= DISCOVERY_CAP_TCP;
    }
    if (useMulticast)
    {
        capabilities |= DISCOVERY_CAP_MULTICAST;
    }

    appendU8(payload, DISCOVERY_ANNOUNCE_VERSION);
    appendU32(payload, nodeId);
    appendU8(payload, static_cast<uint8_t>(m_effectiveDomainId));
    appendU16(payload, endpointPort);
    appendU32(payload, capabilities);
    appendU16(payload, topicCount);
    for (size_t i = 0; i < topicCount; ++i)
    {
        appendU32(payload, topics[i]);
    }

    return true;
}

bool LDds::decodeDiscoveryAnnounce(
    const LMessage & message,
    DiscoveryAnnounce & announce) const
{
    if (!isDiscoveryMessage(message))
    {
        return false;
    }

    const std::vector<uint8_t> & payload = message.getPayload();
    size_t offset = 0;
    uint16_t topicCount = 0;

    if (!readU8(payload, offset, announce.version))
    {
        return false;
    }
    if (!readU32(payload, offset, announce.nodeId))
    {
        return false;
    }
    if (!readU8(payload, offset, announce.domainId))
    {
        return false;
    }
    if (!readU16(payload, offset, announce.endpointPort))
    {
        return false;
    }
    if (!readU32(payload, offset, announce.capabilities))
    {
        return false;
    }
    if (!readU16(payload, offset, topicCount))
    {
        return false;
    }
    if (topicCount > DISCOVERY_MAX_TOPICS)
    {
        return false;
    }

    announce.topics.clear();
    announce.topics.reserve(topicCount);
    for (uint16_t i = 0; i < topicCount; ++i)
    {
        uint32_t topic = 0;
        if (!readU32(payload, offset, topic))
        {
            return false;
        }
        if (topic != 0)
        {
            announce.topics.push_back(topic);
        }
    }

    return true;
}

void LDds::handleDiscoveryMessage(
    const LMessage & message,
    const QHostAddress & senderAddress,
    quint16 senderPort)
{
    DiscoveryAnnounce announce;
    if (!decodeDiscoveryAnnounce(message, announce))
    {
        return;
    }

    if (announce.version == 0U || announce.version > DISCOVERY_ANNOUNCE_VERSION)
    {
        return;
    }
    if (announce.domainId != static_cast<uint8_t>(m_effectiveDomainId))
    {
        return;
    }

    uint32_t localNodeId = 0;
    {
        std::lock_guard<std::mutex> lock(m_discoveryMutex);
        localNodeId = m_discoveryNodeId;
    }
    if (announce.nodeId == 0 || announce.nodeId == localNodeId)
    {
        return;
    }

    const quint16 endpointPort = (announce.endpointPort != 0) ? announce.endpointPort : senderPort;
    if (endpointPort == 0 || senderAddress.isNull())
    {
        return;
    }

    bool isNewPeer = false;
    bool endpointChanged = false;
    {
        std::lock_guard<std::mutex> lock(m_discoveryMutex);
        auto it = m_discoveryPeers.find(announce.nodeId);
        if (it == m_discoveryPeers.end())
        {
            DiscoveryPeerInfo peer;
            peer.address = senderAddress;
            peer.endpointPort = endpointPort;
            peer.lastSeen = std::chrono::steady_clock::now();
            peer.topics = announce.topics;
            peer.capabilities = announce.capabilities;
            peer.online = true;
            m_discoveryPeers.emplace(announce.nodeId, std::move(peer));
            isNewPeer = true;
        }
        else
        {
            endpointChanged = (it->second.address != senderAddress) || (it->second.endpointPort != endpointPort);
            it->second.address = senderAddress;
            it->second.endpointPort = endpointPort;
            it->second.lastSeen = std::chrono::steady_clock::now();
            it->second.topics = announce.topics;
            it->second.capabilities = announce.capabilities;
            it->second.online = true;
        }
    }

    if (isNewPeer || endpointChanged)
    {
        setLastError(
            "discovery peer online nodeId=" + std::to_string(announce.nodeId) +
            ", endpoint=" + senderAddress.toString().toStdString() +
            ":" + std::to_string(static_cast<uint32_t>(endpointPort)));
    }
}

void LDds::initializeDiscoveryState(const TransportConfig & transportConfig)
{
    const auto now = std::chrono::steady_clock::now();

    bool enabled =
        m_pTransport &&
        m_pTransport->getProtocol() == TransportProtocol::UDP &&
        transportConfig.enableDiscovery;

    quint16 discoveryPort = transportConfig.discoveryPort;
    if (discoveryPort == 0 && m_pTransport)
    {
        discoveryPort = m_pTransport->getBoundPort();
    }
    if (discoveryPort == 0)
    {
        discoveryPort = transportConfig.bindPort;
    }

    int intervalMs = std::max(DISCOVERY_MIN_INTERVAL_MS, transportConfig.discoveryIntervalMs);
    int peerTtlMs = std::max(DISCOVERY_MIN_PEER_TTL_MS, transportConfig.peerTtlMs);
    if (peerTtlMs < intervalMs * 2)
    {
        peerTtlMs = intervalMs * 2;
    }

    const quint16 seedPort =
        (m_pTransport != nullptr && m_pTransport->getBoundPort() != 0)
            ? m_pTransport->getBoundPort()
            : transportConfig.bindPort;
    const std::string nodeSeed =
        "discovery|" + std::to_string(static_cast<uint32_t>(m_effectiveDomainId)) +
        "|" + std::to_string(static_cast<uint32_t>(seedPort)) +
        "|" + std::to_string(static_cast<uintptr_t>(reinterpret_cast<uintptr_t>(this)));
    const uint32_t nodeId = fnv1aHash32(nodeSeed);

    QHostAddress multicastGroup;
    bool useMulticast = enabled && transportConfig.enableMulticast;
    if (useMulticast)
    {
        if (!transportConfig.multicastGroup.isEmpty())
        {
            multicastGroup = QHostAddress(transportConfig.multicastGroup);
        }
        if (multicastGroup.isNull())
        {
            multicastGroup = resolveDomainMulticastGroup(m_effectiveDomainId);
        }
        if (!multicastGroup.isMulticast())
        {
            useMulticast = false;
        }
    }

    std::lock_guard<std::mutex> lock(m_discoveryMutex);
    m_discoveryEnabled = enabled;
    m_discoveryUseMulticast = useMulticast;
    m_discoveryNodeId = nodeId;
    m_discoveryPort = discoveryPort;
    m_discoveryInterval = std::chrono::milliseconds(intervalMs);
    m_peerTtl = std::chrono::milliseconds(peerTtlMs);
    m_lastDiscoverySend = now - m_discoveryInterval;
    m_discoveryMulticastGroup = multicastGroup;
    m_discoveryPeers.clear();
}

void LDds::clearDiscoveryState() noexcept
{
    std::lock_guard<std::mutex> lock(m_discoveryMutex);
    m_discoveryEnabled = false;
    m_discoveryUseMulticast = false;
    m_discoveryNodeId = 1;
    m_discoveryPort = 0;
    m_discoveryPeers.clear();
    m_discoveryMulticastGroup.clear();
}

void LDds::processDiscovery(const std::chrono::steady_clock::time_point & now)
{
    if (!m_running.load() || !m_pTransport)
    {
        return;
    }

    bool discoveryEnabled = false;
    bool shouldAnnounce = false;
    bool useMulticast = false;
    quint16 discoveryPort = 0;
    QHostAddress multicastGroup;
    std::vector<uint32_t> expiredPeers;

    {
        std::lock_guard<std::mutex> lock(m_discoveryMutex);
        discoveryEnabled = m_discoveryEnabled;
        if (!discoveryEnabled)
        {
            return;
        }

        for (auto it = m_discoveryPeers.begin(); it != m_discoveryPeers.end();)
        {
            if (now - it->second.lastSeen > m_peerTtl)
            {
                expiredPeers.push_back(it->first);
                it = m_discoveryPeers.erase(it);
            }
            else
            {
                ++it;
            }
        }

        if (now - m_lastDiscoverySend >= m_discoveryInterval)
        {
            shouldAnnounce = true;
            m_lastDiscoverySend = now;
        }

        useMulticast = m_discoveryUseMulticast;
        multicastGroup = m_discoveryMulticastGroup;
        discoveryPort = m_discoveryPort;
    }

    for (uint32_t nodeId : expiredPeers)
    {
        setLastError("discovery peer offline nodeId=" + std::to_string(nodeId));
    }

    if (!shouldAnnounce || discoveryPort == 0)
    {
        return;
    }

    std::vector<uint8_t> payload;
    if (!encodeDiscoveryAnnounce(payload))
    {
        return;
    }

    const uint64_t sequence = m_sequence.fetch_add(1) + 1;
    LMessage announce(HEARTBEAT_TOPIC_ID, sequence, payload, LMessageType::DiscoveryAnnounce);
    announce.setDomainId(static_cast<uint8_t>(m_effectiveDomainId));
    announce.setWriterId(m_discoveryNodeId);

    const TransportConfig config = m_pTransport->getConfig();
    if (useMulticast && !multicastGroup.isNull())
    {
        (void)sendMessageThroughTransport(announce, &multicastGroup, discoveryPort);
    }
    if (config.enableBroadcast)
    {
        const QHostAddress broadcastAddress = QHostAddress::Broadcast;
        (void)sendMessageThroughTransport(announce, &broadcastAddress, discoveryPort);
    }
}

std::vector<std::pair<QHostAddress, quint16>> LDds::snapshotDiscoveryTargets(uint32_t topic) const
{
    std::vector<std::pair<QHostAddress, quint16>> targets;
    const auto now = std::chrono::steady_clock::now();

    std::lock_guard<std::mutex> lock(m_discoveryMutex);
    if (!m_discoveryEnabled)
    {
        return targets;
    }

    for (const auto & pair : m_discoveryPeers)
    {
        const DiscoveryPeerInfo & peer = pair.second;
        if (!peer.online || peer.endpointPort == 0 || peer.address.isNull())
        {
            continue;
        }
        if (now - peer.lastSeen > m_peerTtl)
        {
            continue;
        }

        if (!peer.topics.empty() &&
            std::find(peer.topics.begin(), peer.topics.end(), topic) == peer.topics.end())
        {
            continue;
        }

        targets.push_back({peer.address, peer.endpointPort});
    }

    return targets;
}

void LDds::rememberKnownTopic(uint32_t topic)
{
    if (topic == 0)
    {
        return;
    }
    std::lock_guard<std::mutex> lock(m_knownTopicsMutex);
    m_knownTopics.insert(topic);
}

std::vector<uint32_t> LDds::snapshotKnownTopics() const
{
    std::vector<uint32_t> topics;
    std::lock_guard<std::mutex> lock(m_knownTopicsMutex);
    topics.reserve(m_knownTopics.size());
    for (const uint32_t topic : m_knownTopics)
    {
        if (topic != 0)
        {
            topics.push_back(topic);
        }
    }
    std::sort(topics.begin(), topics.end());
    return topics;
}

uint32_t LDds::resolveReliableWriterId(
    const LMessage & message,
    const QHostAddress & senderAddress,
    quint16 senderPort) const
{
    if (message.getWriterId() != 0)
    {
        return message.getWriterId();
    }

    const std::string endpoint =
        senderAddress.toString().toStdString() + ":" + std::to_string(static_cast<uint32_t>(senderPort));
    return fnv1aHash32(endpoint);
}

bool LDds::publishSerializedTopic(
    uint32_t                topic,
    std::vector<uint8_t> && payload,
    const std::string &     dataType
)
{
    if (topic == 0)
    {
        setLastError("invalid topic=0");
        return false;
    }

    if (!m_running.load() || !m_pTransport)
    {
        setLastError("dds is not running");
        return false;
    }
    rememberKnownTopic(topic);

    const uint64_t sequence = m_sequence.fetch_add(1) + 1;
    LMessage       message(topic, sequence, payload);
    message.setDomainId(static_cast<uint8_t>(m_effectiveDomainId));
    message.setMessageType(LMessageType::Data);

    bool reliableUdpEnabled = false;
    uint32_t writerId = 0;
    uint32_t windowSize = 0;
    {
        std::lock_guard<std::mutex> lock(m_reliableMutex);
        reliableUdpEnabled = m_reliableUdpEnabled;
        writerId = m_reliableWriterId;
        windowSize = m_reliableWindowSize;
    }

    bool discoveryEnabled = false;
    {
        std::lock_guard<std::mutex> lock(m_discoveryMutex);
        discoveryEnabled = m_discoveryEnabled;
    }
    const std::vector<std::pair<QHostAddress, quint16>> discoveryTargets =
        snapshotDiscoveryTargets(topic);
    const TransportConfig currentTransportConfig = m_pTransport->getConfig();
    const bool hasDefaultRemote =
        !currentTransportConfig.remoteAddress.isEmpty() && currentTransportConfig.remotePort != 0;

    if (reliableUdpEnabled)
    {
        const auto now = std::chrono::steady_clock::now();
        message.setWriterId(writerId);
        message.setFirstSeq(sequence);
        message.setLastSeq(sequence);
        message.setWindowStart(sequence);
        message.setWindowSize(windowSize);

        bool hasExplicitTarget = false;
        QHostAddress targetAddress;
        quint16 targetPort = 0;
        if (!hasDefaultRemote && !discoveryTargets.empty())
        {
            hasExplicitTarget = true;
            targetAddress = discoveryTargets.front().first;
            targetPort = discoveryTargets.front().second;
        }
        if (!hasDefaultRemote && !hasExplicitTarget && discoveryEnabled)
        {
            setLastError(
                "reliable udp send skipped: no discovered peer for topic=" + std::to_string(topic));
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(m_reliableMutex);
            m_reliablePending[sequence] = ReliablePendingEntry{message, now, 0U};
        }

        const bool sent = hasExplicitTarget
            ? sendMessageThroughTransport(message, &targetAddress, targetPort)
            : sendMessageThroughTransport(message);
        if (!sent)
        {
            {
                std::lock_guard<std::mutex> lock(m_reliableMutex);
                m_reliablePending.erase(sequence);
            }
            setLastError(
                "sendMessage failed (reliable udp, domain=" + std::to_string(m_effectiveDomainId) +
                "): " + m_pTransport->getLastError().toStdString());
            return false;
        }
    }
    else
    {
        bool sentAny = false;
        for (const auto & endpoint : discoveryTargets)
        {
            if (sendMessageThroughTransport(message, &endpoint.first, endpoint.second))
            {
                sentAny = true;
            }
        }

        if (hasDefaultRemote)
        {
            if (sendMessageThroughTransport(message))
            {
                sentAny = true;
            }
        }

        if (!sentAny)
        {
            if (discoveryEnabled && !hasDefaultRemote)
            {
                setLastError(
                    "sendMessage skipped: no discovered peers for topic=" + std::to_string(topic));
            }
            else
            {
                setLastError(
                    "sendMessage failed (domain=" + std::to_string(m_effectiveDomainId) + "): " +
                    m_pTransport->getLastError().toStdString());
            }
            return false;
        }
    }

    m_domain.cacheTopicData(static_cast<int>(topic), payload, dataType);
    markTopicActivity(topic);

    return true;
}

void LDds::handleTransportMessage(
    const LMessage &  message,
    const QHostAddress & senderAddress,
    quint16           senderPort
)
{
    if (message.getDomainId() != static_cast<uint8_t>(m_effectiveDomainId))
    {
        return;
    }

    try
    {
        if (isDiscoveryMessage(message))
        {
            handleDiscoveryMessage(message, senderAddress, senderPort);
            return;
        }

        bool reliableUdpEnabled = false;
        {
            std::lock_guard<std::mutex> lock(m_reliableMutex);
            reliableUdpEnabled = m_reliableUdpEnabled;
        }

        if (!reliableUdpEnabled)
        {
            if (message.isHeartbeat())
            {
                std::lock_guard<std::mutex> lock(m_qosMutex);
                m_lastHeartbeatReceive = std::chrono::steady_clock::now();
                return;
            }

            deliverDataMessage(message);
            return;
        }

        if (message.isControlMessage() || message.getTopic() == HEARTBEAT_TOPIC_ID)
        {
            handleReliableControlMessage(message, senderAddress, senderPort);
            return;
        }

        handleReliableDataMessage(message, senderAddress, senderPort);
    }
    catch (const std::exception & ex)
    {
        setLastError(std::string("handleTransportMessage exception: ") + ex.what());
    }
    catch (...)
    {
        setLastError("handleTransportMessage unknown exception");
    }
}

void LDds::markTopicActivity(uint32_t topic)
{
    if (topic == 0)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(m_qosMutex);
    m_lastTopicActivity[topic] = std::chrono::steady_clock::now();
    m_deadlineMissedTopics.erase(topic);
}

void LDds::startQosThread()
{
    if (m_qosThreadRunning.load())
    {
        return;
    }

    const int32_t deadlineMs = resolveDeadlineMs(m_qos);
    m_deadlineCheckInterval = (deadlineMs > 0) ? std::chrono::milliseconds(100) : std::chrono::milliseconds(1000);
    m_heartbeatInterval = resolveHeartbeatInterval(deadlineMs);

    m_qosThreadRunning.store(true);
    m_qosThread = std::thread(&LDds::qosThreadFunc, this);
}

void LDds::stopQosThread() noexcept
{
    m_qosThreadRunning.store(false);
    m_qosCondition.notify_all();
    if (m_qosThread.joinable())
    {
        m_qosThread.join();
    }
}

void LDds::qosThreadFunc()
{
    while (m_qosThreadRunning.load() && m_running.load())
    {
        try
        {
            const auto now = std::chrono::steady_clock::now();
            bool sendHeartbeat = false;
            std::vector<std::pair<uint32_t, uint64_t>> deadlineMissed;
            DeadlineMissedCallback deadlineCallback;

            {
                std::lock_guard<std::mutex> lock(m_qosMutex);

                if (now - m_lastHeartbeatSend >= m_heartbeatInterval)
                {
                    sendHeartbeat = true;
                    m_lastHeartbeatSend = now;
                }

                const int32_t deadlineMs = resolveDeadlineMs(m_qos);
                if (deadlineMs > 0)
                {
                    for (const auto & pair : m_lastTopicActivity)
                    {
                        const uint32_t topic = pair.first;
                        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - pair.second).count();
                        if (elapsed > deadlineMs)
                        {
                            if (m_deadlineMissedTopics.insert(topic).second)
                            {
                                deadlineMissed.push_back({topic, static_cast<uint64_t>(elapsed)});
                            }
                        }
                    }
                }

                deadlineCallback = m_deadlineMissedCallback;
            }

            if (sendHeartbeat && m_pTransport && m_running.load())
            {
                const uint64_t sequence = m_sequence.fetch_add(1) + 1;
                const uint64_t nowMs = static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()
                );
                LMessage heartbeat = LMessage::makeHeartbeat(sequence, nowMs);
                heartbeat.setDomainId(static_cast<uint8_t>(m_effectiveDomainId));
                heartbeat.setWriterId(m_reliableWriterId);
                (void)sendMessageThroughTransport(heartbeat);
            }

            processDiscovery(now);
            processReliableOutgoing(now);

            for (const auto & missed : deadlineMissed)
            {
                setLastError(
                    "deadline missed topic=" + std::to_string(missed.first) +
                    ", elapsedMs=" + std::to_string(missed.second)
                );

                if (deadlineCallback)
                {
                    deadlineCallback(missed.first, missed.second);
                }
            }
        }
        catch (const std::exception & ex)
        {
            setLastError(std::string("qos thread exception: ") + ex.what());
        }
        catch (...)
        {
            setLastError("qos thread unknown exception");
        }

        std::unique_lock<std::mutex> lock(m_qosMutex);
        m_qosCondition.wait_for(
            lock,
            m_deadlineCheckInterval,
            [this] { return !m_qosThreadRunning.load() || !m_running.load(); }
        );
    }
}

void LDds::setLastError(const std::string & message)
{
    std::lock_guard<std::mutex> lock(m_errorMutex);
    m_lastError = message;
}

const char * getVersion() noexcept
{
    return "0.3.0";
}

const char * getBuildTime() noexcept
{
    return __DATE__ " " __TIME__;
}

bool initialize() noexcept
{
    g_initialized.store(true);
    return true;
}

void shutdown() noexcept
{
    g_initialized.store(false);
}

bool isInitialized() noexcept
{
    return g_initialized.load();
}

} // namespace LDdsFramework
