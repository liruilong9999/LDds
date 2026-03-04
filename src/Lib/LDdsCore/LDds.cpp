#include "LDds.h"

#include <utility>

namespace LDdsFramework {
namespace {

std::atomic<bool> g_initialized(false);

} // namespace

LDds::LDds()
    : m_qos()
    , m_domain()
    , m_pTransport()
    , m_pTypeRegistry(std::make_shared<LTypeRegistry>())
    , m_running(false)
    , m_sequence(0)
    , m_subscribers()
    , m_subscribersMutex()
    , m_errorMutex()
    , m_lastError()
{
}

LDds::~LDds()
{
    shutdown();
}

bool LDds::initialize(const LQos & qos)
{
    TransportConfig config;
    return initialize(qos, config, DEFAULT_DOMAIN_ID);
}

bool LDds::initialize(const LQos & qos, const TransportConfig & transportConfig, DomainId domainId)
{
    if (m_running.load())
    {
        return true;
    }

    if (!m_domain.isValid() || m_domain.getDomainId() != domainId)
    {
        m_domain.destroy();
        if (!m_domain.create(domainId, &qos))
        {
            setLastError("failed to create domain");
            return false;
        }
    }

    m_qos = qos;

    if (!createTransportFromQos(qos, transportConfig))
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
        setLastError("failed to start transport: " + m_pTransport->getLastError().toStdString());
        m_pTransport.reset();
        return false;
    }

    m_sequence.store(0);
    m_running.store(true);
    return true;
}

void LDds::shutdown() noexcept
{
    m_running.store(false);

    if (m_pTransport)
    {
        m_pTransport->stop();
        m_pTransport.reset();
    }

    {
        std::lock_guard<std::mutex> lock(m_subscribersMutex);
        m_subscribers.clear();
    }

    m_domain.destroy();
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

    if (!m_pTransport->sendMessage(message))
    {
        setLastError("sendMessage failed: " + m_pTransport->getLastError().toStdString());
        return false;
    }

    m_domain.cacheTopicData(static_cast<int>(topic), payload, dataType);

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

    const uint32_t topic = message.getTopic();
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
