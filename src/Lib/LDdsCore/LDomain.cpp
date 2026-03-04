/**
 * @file LDomain.cpp
 * @brief LDomain class implementation
 */

#include "LDomain.h"

#include "LQos.h"

#include <utility>

namespace LDdsFramework {

LDomain::LDomain() noexcept
    : m_domainId(INVALID_DOMAIN_ID)
    , m_name()
    , m_participantCount(0)
    , m_valid(false)
    , m_historyDepth(1)
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

    if (pQos != nullptr)
    {
        const int configuredDepth = pQos->getHistory().depth;
        if (configuredDepth > 0)
        {
            m_historyDepth = static_cast<size_t>(configuredDepth);
        }
    }

    if (m_name.empty())
    {
        m_name = "domain_" + std::to_string(m_domainId);
    }

    clearTopicCache();

    (void)pQos;
    return true;
}

void LDomain::destroy() noexcept
{
    m_valid            = false;
    m_domainId         = INVALID_DOMAIN_ID;
    m_participantCount = 0;
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
}

size_t LDomain::getHistoryDepth() const noexcept
{
    std::lock_guard<std::mutex> lock(m_topicCacheMutex);
    return m_historyDepth;
}

void LDomain::cacheTopicData(
    int                         topic,
    const std::vector<uint8_t> & data,
    const std::string &         dataType)
{
    if (topic <= 0)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(m_topicCacheMutex);

    auto & topicQueue = m_topicCache[topic];
    topicQueue.push_back(data);

    const size_t depth = m_historyDepth == 0 ? 1 : m_historyDepth;
    while (topicQueue.size() > depth)
    {
        topicQueue.pop_front();
    }

    if (!dataType.empty())
    {
        m_topicDataTypes[topic] = dataType;
    }
}

std::string LDomain::getDataTypeByTopic(int topic) const
{
    std::lock_guard<std::mutex> lock(m_topicCacheMutex);
    const auto                  it = m_topicDataTypes.find(topic);
    if (it == m_topicDataTypes.end())
    {
        return std::string();
    }
    return it->second;
}

LFindSet LDomain::getFindSetByTopic(int topic) const
{
    std::lock_guard<std::mutex> lock(m_topicCacheMutex);
    const auto                  it = m_topicCache.find(topic);
    if (it == m_topicCache.end())
    {
        return LFindSet();
    }
    return LFindSet(it->second);
}

bool LDomain::getTopicData(int topic, std::vector<uint8_t> & data) const
{
    std::lock_guard<std::mutex> lock(m_topicCacheMutex);
    const auto                  it = m_topicCache.find(topic);
    if (it == m_topicCache.end() || it->second.empty())
    {
        return false;
    }

    data = it->second.back();
    return true;
}

bool LDomain::getNextTopicData(int topic, size_t & cursorFromNewest, std::vector<uint8_t> & data) const
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
    ++cursorFromNewest;
    return true;
}

bool LDomain::hasTopicData(int topic) const
{
    std::lock_guard<std::mutex> lock(m_topicCacheMutex);
    const auto                  it = m_topicCache.find(topic);
    return it != m_topicCache.end() && !it->second.empty();
}

void LDomain::clearTopicCache() noexcept
{
    std::lock_guard<std::mutex> lock(m_topicCacheMutex);
    m_topicCache.clear();
    m_topicDataTypes.clear();
}

} // namespace LDdsFramework
