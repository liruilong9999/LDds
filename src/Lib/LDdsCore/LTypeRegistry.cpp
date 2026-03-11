#include "LTypeRegistry.h"

#include <algorithm>
#include <mutex>

namespace LDdsFramework {
namespace {

struct GeneratedModuleEntry
{
    std::string moduleName;
    GeneratedModuleRegisterFn registerFn;
};

std::mutex & generatedModuleMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::vector<GeneratedModuleEntry> & generatedModules()
{
    static std::vector<GeneratedModuleEntry> modules;
    return modules;
}

uint32_t fnv1aHash32(const std::string & value) noexcept
{
    uint32_t hash = 2166136261U;
    for (const unsigned char ch : value)
    {
        hash ^= static_cast<uint32_t>(ch);
        hash *= 16777619U;
    }
    return (hash == 0U) ? 1U : hash;
}

} // namespace

bool registerGeneratedModule(
    const char *              moduleName,
    GeneratedModuleRegisterFn registerFn)
{
    if (moduleName == nullptr || moduleName[0] == '\0' || registerFn == nullptr)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(generatedModuleMutex());
    auto & modules = generatedModules();
    const auto it = std::find_if(
        modules.begin(),
        modules.end(),
        [moduleName](const GeneratedModuleEntry & entry) {
            return entry.moduleName == moduleName;
        });
    if (it != modules.end())
    {
        return true;
    }

    modules.push_back({moduleName, registerFn});
    return true;
}

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

    auto newEntry          = std::make_shared<TypeEntry>();
    newEntry->typeName     = typeName;
    newEntry->topic        = topic;
    newEntry->factory      = std::move(factory);
    newEntry->serializer   = std::move(serializer);
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

    if (topicIt != m_entriesByTopic.end() && topicIt->second != nullptr)
    {
        newEntry->topicKey = topicIt->second->topicKey;
        newEntry->moduleName = topicIt->second->moduleName;
        newEntry->version = topicIt->second->version;
    }

    m_entriesByTopic[topic] = std::move(newEntry);
    m_topicByTypeName[typeName] = topic;
    return true;
}

bool LTypeRegistry::registerTopicType(
    const std::string & topicKey,
    const std::string & typeName,
    TypeFactory         factory,
    SerializeFn         serializer,
    DeserializeFn       deserializer
)
{
    const uint32_t topic = makeTopicId(topicKey);
    if (topicKey.empty() || typeName.empty() || topic == 0 || !factory || !serializer || !deserializer)
    {
        return false;
    }

    auto newEntry          = std::make_shared<TypeEntry>();
    newEntry->typeName     = typeName;
    newEntry->topicKey     = topicKey;
    newEntry->topic        = topic;
    newEntry->factory      = std::move(factory);
    newEntry->serializer   = std::move(serializer);
    newEntry->deserializer = std::move(deserializer);

    std::unique_lock<std::shared_mutex> lock(m_mutex);

    const auto topicIt = m_entriesByTopic.find(topic);
    if (topicIt != m_entriesByTopic.end())
    {
        if (topicIt->second->typeName != typeName)
        {
            return false;
        }
        if (!topicIt->second->topicKey.empty() && topicIt->second->topicKey != topicKey)
        {
            return false;
        }
    }

    const auto nameIt = m_topicByTypeName.find(typeName);
    if (nameIt != m_topicByTypeName.end() && nameIt->second != topic)
    {
        return false;
    }

    const auto keyIt = m_topicByKey.find(topicKey);
    if (keyIt != m_topicByKey.end() && keyIt->second != topic)
    {
        return false;
    }

    m_entriesByTopic[topic] = std::move(newEntry);
    m_topicByTypeName[typeName] = topic;
    m_topicByKey[topicKey] = topic;
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

uint32_t LTypeRegistry::getTopicByTopicKey(const std::string & topicKey) const
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    const auto                          it = m_topicByKey.find(topicKey);
    if (it == m_topicByKey.end())
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

std::string LTypeRegistry::getTopicKeyByTopic(uint32_t topic) const
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    const auto                          it = m_entriesByTopic.find(topic);
    if (it == m_entriesByTopic.end() || !it->second)
    {
        return std::string();
    }
    return it->second->topicKey;
}

bool LTypeRegistry::setTopicInfo(
    const std::string & topicKey,
    const std::string & moduleName,
    const std::string & version)
{
    if (topicKey.empty())
    {
        return false;
    }

    std::unique_lock<std::shared_mutex> lock(m_mutex);
    const auto keyIt = m_topicByKey.find(topicKey);
    if (keyIt == m_topicByKey.end())
    {
        return false;
    }

    const auto entryIt = m_entriesByTopic.find(keyIt->second);
    if (entryIt == m_entriesByTopic.end() || !entryIt->second)
    {
        return false;
    }

    entryIt->second->moduleName = moduleName;
    entryIt->second->version = version;
    return true;
}

bool LTypeRegistry::getTopicInfo(const std::string & topicKey, DdsTopicInfo & topicInfo) const
{
    if (topicKey.empty())
    {
        return false;
    }

    std::shared_lock<std::shared_mutex> lock(m_mutex);
    const auto keyIt = m_topicByKey.find(topicKey);
    if (keyIt == m_topicByKey.end())
    {
        return false;
    }

    const auto entryIt = m_entriesByTopic.find(keyIt->second);
    if (entryIt == m_entriesByTopic.end() || !entryIt->second)
    {
        return false;
    }

    topicInfo.topicId = entryIt->second->topic;
    topicInfo.topicKey = entryIt->second->topicKey;
    topicInfo.typeName = entryIt->second->typeName;
    topicInfo.moduleName = entryIt->second->moduleName;
    topicInfo.version = entryIt->second->version;
    return true;
}

bool LTypeRegistry::getTopicInfoByTopic(uint32_t topic, DdsTopicInfo & topicInfo) const
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    const auto entryIt = m_entriesByTopic.find(topic);
    if (entryIt == m_entriesByTopic.end() || !entryIt->second)
    {
        return false;
    }

    topicInfo.topicId = entryIt->second->topic;
    topicInfo.topicKey = entryIt->second->topicKey;
    topicInfo.typeName = entryIt->second->typeName;
    topicInfo.moduleName = entryIt->second->moduleName;
    topicInfo.version = entryIt->second->version;
    return true;
}

std::vector<DdsTopicInfo> LTypeRegistry::listTopicInfos() const
{
    std::vector<DdsTopicInfo> topics;

    std::shared_lock<std::shared_mutex> lock(m_mutex);
    topics.reserve(m_entriesByTopic.size());
    for (const auto & pair : m_entriesByTopic)
    {
        if (!pair.second)
        {
            continue;
        }

        DdsTopicInfo info;
        info.topicId = pair.second->topic;
        info.topicKey = pair.second->topicKey;
        info.typeName = pair.second->typeName;
        info.moduleName = pair.second->moduleName;
        info.version = pair.second->version;
        topics.push_back(std::move(info));
    }

    std::sort(
        topics.begin(),
        topics.end(),
        [](const DdsTopicInfo & lhs, const DdsTopicInfo & rhs) {
            return lhs.topicId < rhs.topicId;
        });
    return topics;
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

std::vector<uint32_t> LTypeRegistry::getRegisteredTopics() const
{
    std::vector<uint32_t> topics;

    std::shared_lock<std::shared_mutex> lock(m_mutex);
    topics.reserve(m_entriesByTopic.size());
    for (const auto & entry : m_entriesByTopic)
    {
        if (entry.first != 0)
        {
            topics.push_back(entry.first);
        }
    }

    std::sort(topics.begin(), topics.end());
    return topics;
}

bool LTypeRegistry::applyGeneratedModules(std::vector<std::string> * appliedModules)
{
    std::vector<GeneratedModuleEntry> modules;
    {
        std::lock_guard<std::mutex> lock(generatedModuleMutex());
        modules = generatedModules();
    }

    for (const auto & module : modules)
    {
        if (module.moduleName.empty() || module.registerFn == nullptr)
        {
            continue;
        }

        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            if (m_appliedGeneratedModules.find(module.moduleName) != m_appliedGeneratedModules.end())
            {
                continue;
            }
        }

        if (!module.registerFn(*this))
        {
            return false;
        }

        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            m_appliedGeneratedModules.insert(module.moduleName);
        }

        if (appliedModules != nullptr)
        {
            appliedModules->push_back(module.moduleName);
        }
    }

    return true;
}

uint32_t LTypeRegistry::makeTopicId(const std::string & topicKey) noexcept
{
    if (topicKey.empty())
    {
        return 0;
    }
    return fnv1aHash32(topicKey);
}

} // namespace LDdsFramework
