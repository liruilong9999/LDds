#include "LTypeRegistry.h"

namespace LDdsFramework {

bool LTypeRegistry::registerType(
    const std::string & typeName,
    uint32_t            topic,
    TypeFactory         factory,
    SerializeFn         serializer,
    DeserializeFn       deserializer
)
{
    if (typeName.empty() || topic == 0 || !factory || !serializer || !deserializer)
    {
        return false;
    }

    auto newEntry         = std::make_shared<TypeEntry>();
    newEntry->typeName    = typeName;
    newEntry->topic       = topic;
    newEntry->factory     = std::move(factory);
    newEntry->serializer  = std::move(serializer);
    newEntry->deserializer = std::move(deserializer);

    std::unique_lock<std::shared_mutex> lock(m_mutex);

    const auto topicIt = m_entriesByTopic.find(topic);
    if (topicIt != m_entriesByTopic.end() && topicIt->second->typeName != typeName)
    {
        return false;
    }

    const auto nameIt = m_topicByTypeName.find(typeName);
    if (nameIt != m_topicByTypeName.end() && nameIt->second != topic)
    {
        return false;
    }

    m_entriesByTopic[topic]     = std::move(newEntry);
    m_topicByTypeName[typeName] = topic;
    return true;
}

std::shared_ptr<void> LTypeRegistry::createByTopic(uint32_t topic) const
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    const auto                          it = m_entriesByTopic.find(topic);
    if (it == m_entriesByTopic.end() || !it->second->factory)
    {
        return nullptr;
    }
    return it->second->factory();
}

uint32_t LTypeRegistry::getTopicByTypeName(const std::string & typeName) const
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    const auto                          it = m_topicByTypeName.find(typeName);
    if (it == m_topicByTypeName.end())
    {
        return 0;
    }
    return it->second;
}

std::string LTypeRegistry::getTypeNameByTopic(uint32_t topic) const
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    const auto                          it = m_entriesByTopic.find(topic);
    if (it == m_entriesByTopic.end() || !it->second)
    {
        return std::string();
    }
    return it->second->typeName;
}

bool LTypeRegistry::serializeByTopic(
    uint32_t               topic,
    const void *           object,
    std::vector<uint8_t> & outPayload
) const
{
    std::shared_ptr<TypeEntry> entry;
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        const auto                          it = m_entriesByTopic.find(topic);
        if (it == m_entriesByTopic.end())
        {
            return false;
        }
        entry = it->second;
    }

    if (!entry || !entry->serializer)
    {
        return false;
    }

    outPayload.clear();
    return entry->serializer(object, outPayload);
}

bool LTypeRegistry::deserializeByTopic(
    uint32_t                     topic,
    const std::vector<uint8_t> & payload,
    void *                       object
) const
{
    std::shared_ptr<TypeEntry> entry;
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        const auto                          it = m_entriesByTopic.find(topic);
        if (it == m_entriesByTopic.end())
        {
            return false;
        }
        entry = it->second;
    }

    if (!entry || !entry->deserializer)
    {
        return false;
    }

    return entry->deserializer(payload, object);
}

bool LTypeRegistry::hasTopic(uint32_t topic) const
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_entriesByTopic.find(topic) != m_entriesByTopic.end();
}

} // namespace LDdsFramework
