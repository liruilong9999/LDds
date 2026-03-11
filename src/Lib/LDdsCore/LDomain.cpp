/**
 * @file LDomain.cpp
 * @brief LDomain class implementation
 */

#include "LDomain.h"

#include "LQos.h"
#include "SqliteDurabilityStore.h"

#include <utility>

namespace LDdsFramework {

LDomain::LDomain() noexcept
    : m_domainId(INVALID_DOMAIN_ID)
    , m_name()
    , m_participantCount(0)
    , m_valid(false)
    , m_historyDepth(1)
    , m_topicHistoryDepthOverrides()
    , m_persistentDurabilityEnabled(false)
    , m_durabilityDbPath()
    , m_localSequence(0)
    , m_sqliteStore()
    , m_topicCache()
    , m_topicDataTypes()
{
}

LDomain::~LDomain() noexcept
{
}

LDomain::LDomain(LDomain && other) noexcept
    : m_domainId(other.m_domainId)
    , m_name(std::move(other.m_name))
    , m_participantCount(other.m_participantCount)
    , m_valid(other.m_valid)
    , m_historyDepth(other.m_historyDepth)
    , m_persistentDurabilityEnabled(other.m_persistentDurabilityEnabled)
    , m_durabilityDbPath(std::move(other.m_durabilityDbPath))
    , m_localSequence(other.m_localSequence)
    , m_sqliteStore(std::move(other.m_sqliteStore))
{
    {
        std::lock_guard<std::mutex> lock(other.m_topicCacheMutex);
        m_topicCache     = std::move(other.m_topicCache);
        m_topicDataTypes = std::move(other.m_topicDataTypes);
    }

    other.m_domainId         = INVALID_DOMAIN_ID;
    other.m_participantCount = 0;
    other.m_valid            = false;
    other.m_historyDepth     = 1;
    other.m_topicHistoryDepthOverrides.clear();
    other.m_persistentDurabilityEnabled = false;
    other.m_localSequence = 0;
}

LDomain & LDomain::operator=(LDomain && other) noexcept
{
    if (this != &other)
    {
        m_domainId         = other.m_domainId;
        m_name             = std::move(other.m_name);
        m_participantCount = other.m_participantCount;
        m_valid            = other.m_valid;
        m_historyDepth     = other.m_historyDepth;
        m_topicHistoryDepthOverrides = std::move(other.m_topicHistoryDepthOverrides);
        m_persistentDurabilityEnabled = other.m_persistentDurabilityEnabled;
        m_durabilityDbPath = std::move(other.m_durabilityDbPath);
        m_localSequence = other.m_localSequence;
        m_sqliteStore = std::move(other.m_sqliteStore);
        {
            std::lock_guard<std::mutex> lockThis(m_topicCacheMutex);
            std::lock_guard<std::mutex> lockOther(other.m_topicCacheMutex);
            m_topicCache     = std::move(other.m_topicCache);
            m_topicDataTypes = std::move(other.m_topicDataTypes);
        }

        other.m_domainId         = INVALID_DOMAIN_ID;
        other.m_participantCount = 0;
        other.m_valid            = false;
        other.m_historyDepth     = 1;
        other.m_topicHistoryDepthOverrides.clear();
        other.m_persistentDurabilityEnabled = false;
        other.m_localSequence = 0;
    }
    return *this;
}

bool LDomain::create(DomainId domainId, const LQos * pQos)
{
    if (m_valid)
    {
        return false;
    }

    if (domainId == INVALID_DOMAIN_ID)
    {
        domainId = DEFAULT_DOMAIN_ID;
    }

    m_domainId         = domainId;
    m_participantCount = 0;
    m_valid            = true;
    m_historyDepth     = 1;
    m_topicHistoryDepthOverrides.clear();
    m_persistentDurabilityEnabled = false;
    m_localSequence = 0;
    m_durabilityDbPath.clear();
    m_sqliteStore.reset();
    clearTopicCache();

    if (pQos != nullptr)
    {
        const int configuredDepth = pQos->getHistory().depth;
        if (configuredDepth > 0)
        {
            m_historyDepth = static_cast<size_t>(configuredDepth);
        }

        const DurabilityQosPolicy durability = pQos->getDurability();
        if (durability.enabled && durability.kind == DurabilityKind::Persistent)
        {
            m_persistentDurabilityEnabled = true;
            m_durabilityDbPath = pQos->durabilityDbPath;
            auto sqliteStore = std::make_unique<SqliteDurabilityStore>();
            std::string storeError;
            if (!sqliteStore->open(
                    m_durabilityDbPath,
                    static_cast<uint32_t>(m_domainId),
                    m_historyDepth,
                    &storeError))
            {
                destroy();
                return false;
            }

            if (!sqliteStore->loadRecent(m_historyDepth, m_topicCache, m_topicDataTypes, &storeError))
            {
                destroy();
                return false;
            }

            m_sqliteStore = std::move(sqliteStore);
        }
    }

    if (m_name.empty())
    {
        m_name = "domain_" + std::to_string(m_domainId);
    }

    (void)pQos;
    return true;
}

void LDomain::destroy() noexcept
{
    m_valid            = false;
    m_domainId         = INVALID_DOMAIN_ID;
    m_participantCount = 0;
    m_persistentDurabilityEnabled = false;
    m_localSequence = 0;
    m_durabilityDbPath.clear();
    if (m_sqliteStore)
    {
        m_sqliteStore->close();
        m_sqliteStore.reset();
    }
    clearTopicCache();
}

bool LDomain::isValid() const noexcept
{
    return m_valid;
}

DomainId LDomain::getDomainId() const noexcept
{
    return m_domainId;
}

const std::string & LDomain::getName() const noexcept
{
    return m_name;
}

void LDomain::setName(const std::string & name)
{
    m_name = name;
}

size_t LDomain::getParticipantCount() const noexcept
{
    return m_participantCount;
}

void LDomain::setHistoryDepth(size_t depth) noexcept
{
    std::lock_guard<std::mutex> lock(m_topicCacheMutex);
    m_historyDepth = depth == 0 ? 1 : depth;
    if (m_sqliteStore)
    {
        m_sqliteStore->setHistoryDepth(m_historyDepth);
    }
}

void LDomain::setTopicHistoryDepth(uint32_t topic, size_t depth) noexcept
{
    if (topic == 0)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(m_topicCacheMutex);
    if (depth == 0)
    {
        m_topicHistoryDepthOverrides.erase(topic);
    }
    else
    {
        m_topicHistoryDepthOverrides[topic] = depth;
    }
}

size_t LDomain::getHistoryDepth() const noexcept
{
    std::lock_guard<std::mutex> lock(m_topicCacheMutex);
    return m_historyDepth;
}

void LDomain::cacheTopicData(
    uint32_t                    topic,
    const std::vector<uint8_t> & data,
    const std::string &         dataType,
    const DdsSampleMetadata &   metadata)
{
    if (topic == 0)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(m_topicCacheMutex);

    auto & topicQueue = m_topicCache[topic];
    topicQueue.push_back(data);
    auto & metadataQueue = m_topicMetadata[topic];
    metadataQueue.push_back(metadata);

    size_t depth = m_historyDepth == 0 ? 1 : m_historyDepth;
    const auto depthIt = m_topicHistoryDepthOverrides.find(topic);
    if (depthIt != m_topicHistoryDepthOverrides.end() && depthIt->second > 0)
    {
        depth = depthIt->second;
    }
    while (topicQueue.size() > depth)
    {
        topicQueue.pop_front();
        if (!metadataQueue.empty())
        {
            metadataQueue.pop_front();
        }
    }

    if (!dataType.empty())
    {
        m_topicDataTypes[topic] = dataType;
    }

    ++m_localSequence;
    if (m_sqliteStore && m_persistentDurabilityEnabled)
    {
        std::string ignoredError;
        (void)m_sqliteStore->append(topic, data, dataType, m_localSequence, &ignoredError);
    }
}

std::string LDomain::getDataTypeByTopic(uint32_t topic) const
{
    std::lock_guard<std::mutex> lock(m_topicCacheMutex);
    const auto                  it = m_topicDataTypes.find(topic);
    if (it == m_topicDataTypes.end())
    {
        return std::string();
    }
    return it->second;
}

LFindSet LDomain::getFindSetByTopic(uint32_t topic) const
{
    std::lock_guard<std::mutex> lock(m_topicCacheMutex);
    const auto                  it = m_topicCache.find(topic);
    if (it == m_topicCache.end())
    {
        return LFindSet();
    }

    std::vector<std::vector<uint8_t>> snapshot;
    snapshot.reserve(it->second.size());
    for (auto payloadIt = it->second.rbegin(); payloadIt != it->second.rend(); ++payloadIt)
    {
        snapshot.push_back(*payloadIt);
    }

    std::vector<DdsSampleMetadata> metadata;
    const auto metaIt = m_topicMetadata.find(topic);
    if (metaIt != m_topicMetadata.end())
    {
        metadata.reserve(metaIt->second.size());
        for (auto itemIt = metaIt->second.rbegin(); itemIt != metaIt->second.rend(); ++itemIt)
        {
            metadata.push_back(*itemIt);
        }
    }

    if (metadata.size() < snapshot.size())
    {
        metadata.resize(snapshot.size());
    }

    return LFindSet(std::move(snapshot), std::move(metadata));
}

bool LDomain::getTopicData(
    uint32_t topic,
    std::vector<uint8_t> & data,
    DdsSampleMetadata * metadata) const
{
    std::lock_guard<std::mutex> lock(m_topicCacheMutex);
    const auto                  it = m_topicCache.find(topic);
    if (it == m_topicCache.end() || it->second.empty())
    {
        return false;
    }

    data = it->second.back();
    if (metadata != nullptr)
    {
        const auto metaIt = m_topicMetadata.find(topic);
        if (metaIt != m_topicMetadata.end() && !metaIt->second.empty())
        {
            *metadata = metaIt->second.back();
        }
    }
    return true;
}

bool LDomain::getNextTopicData(
    uint32_t topic,
    size_t & cursorFromNewest,
    std::vector<uint8_t> & data,
    DdsSampleMetadata * metadata) const
{
    std::lock_guard<std::mutex> lock(m_topicCacheMutex);
    const auto                  it = m_topicCache.find(topic);
    if (it == m_topicCache.end())
    {
        return false;
    }

    const auto & topicQueue = it->second;
    if (cursorFromNewest >= topicQueue.size())
    {
        return false;
    }

    const size_t index = topicQueue.size() - 1 - cursorFromNewest;
    data               = topicQueue[index];
    if (metadata != nullptr)
    {
        const auto metaIt = m_topicMetadata.find(topic);
        if (metaIt != m_topicMetadata.end() && index < metaIt->second.size())
        {
            *metadata = metaIt->second[index];
        }
    }
    ++cursorFromNewest;
    return true;
}

bool LDomain::hasTopicData(uint32_t topic) const
{
    std::lock_guard<std::mutex> lock(m_topicCacheMutex);
    const auto                  it = m_topicCache.find(topic);
    return it != m_topicCache.end() && !it->second.empty();
}

void LDomain::clearTopicCache() noexcept
{
    std::lock_guard<std::mutex> lock(m_topicCacheMutex);
    m_topicCache.clear();
    m_topicMetadata.clear();
    m_topicDataTypes.clear();
}

} // namespace LDdsFramework
