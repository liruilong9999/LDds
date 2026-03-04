#include "LDds.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace LDdsFramework {
namespace {

std::atomic<bool> g_initialized(false);

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
    return m_pTypeRegistry->registerType(
        typeName,
        topic,
        std::move(factory),
        std::move(serializer),
        std::move(deserializer)
    );
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

    const uint64_t sequence = m_sequence.fetch_add(1) + 1;
    LMessage       message(topic, sequence, payload);
    message.setDomainId(static_cast<uint8_t>(m_effectiveDomainId));

    if (!m_pTransport->sendMessage(message))
    {
        setLastError(
            "sendMessage failed (domain=" + std::to_string(m_effectiveDomainId) + "): " +
            m_pTransport->getLastError().toStdString());
        return false;
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
    (void)senderAddress;
    (void)senderPort;

    if (message.isHeartbeat())
    {
        if (message.getDomainId() != static_cast<uint8_t>(m_effectiveDomainId))
        {
            return;
        }
        std::lock_guard<std::mutex> lock(m_qosMutex);
        m_lastHeartbeatReceive = std::chrono::steady_clock::now();
        return;
    }

    if (message.getDomainId() != static_cast<uint8_t>(m_effectiveDomainId))
    {
        return;
    }

    const uint32_t topic = message.getTopic();
    if (topic == 0)
    {
        return;
    }

    auto           object = m_pTypeRegistry->createByTopic(topic);
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
        const auto                  it = m_subscribers.find(topic);
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
            (void)m_pTransport->sendMessage(heartbeat);
        }

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
