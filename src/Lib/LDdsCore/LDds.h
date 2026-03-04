/**
 * @file LDds.h
 * @brief LDds public headers
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
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

    LDds();
    ~LDds();

    LDds(const LDds & other) = delete;
    LDds & operator=(const LDds & other) = delete;

    bool initialize(const LQos & qos);
    bool initialize(const LQos & qos, const TransportConfig & transportConfig, DomainId domainId = DEFAULT_DOMAIN_ID);
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
        return m_pTypeRegistry->registerType<T>(typeName, topic);
    }

    template<typename T, typename Serializer, typename Deserializer>
    bool registerType(
        const std::string & typeName,
        uint32_t topic,
        Serializer && serializer,
        Deserializer && deserializer
    )
    {
        return m_pTypeRegistry->registerType<T>(
            typeName,
            topic,
            std::forward<Serializer>(serializer),
            std::forward<Deserializer>(deserializer)
        );
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

        auto cachedObject = std::static_pointer_cast<void>(std::make_shared<T>(object));
        return publishSerializedTopic(topic, std::move(payload), std::move(cachedObject));
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

        auto cachedObject = std::static_pointer_cast<void>(std::make_shared<T>(object));
        return publishSerializedTopic(topic, std::move(payload), std::move(cachedObject));
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

    const LQos & getQos() const noexcept;
    TransportProtocol getTransportProtocol() const noexcept;

    LDomain & domain() noexcept;
    const LDomain & domain() const noexcept;

    std::string getLastError() const;

private:
    bool createTransportFromQos(const LQos & qos, const TransportConfig & transportConfig);
    bool publishSerializedTopic(uint32_t topic, std::vector<uint8_t> && payload, std::shared_ptr<void> localObject);
    void handleTransportMessage(const LMessage & message, const QHostAddress & senderAddress, quint16 senderPort);

    void setLastError(const std::string & message);

private:
    LQos                        m_qos;
    LDomain                     m_domain;
    std::unique_ptr<ITransport> m_pTransport;
    std::shared_ptr<LTypeRegistry> m_pTypeRegistry;

    std::atomic<bool>     m_running;
    std::atomic<uint64_t> m_sequence;

    std::unordered_map<uint32_t, std::vector<TopicCallback>> m_subscribers;
    mutable std::mutex                                        m_subscribersMutex;

    mutable std::mutex m_errorMutex;
    std::string        m_lastError;
};

const char * getVersion() noexcept;
const char * getBuildTime() noexcept;

bool initialize() noexcept;
void shutdown() noexcept;
bool isInitialized() noexcept;

} // namespace LDdsFramework
