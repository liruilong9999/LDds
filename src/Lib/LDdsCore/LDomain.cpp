/**
 * @file LDomain.cpp
 * @brief LDomain class implementation
 */

#include "LDomain.h"

#include <utility>

namespace LDdsFramework {

LDomain::LDomain() noexcept
    : m_domainId(INVALID_DOMAIN_ID)
    , m_name()
    , m_participantCount(0)
    , m_valid(false)
    , m_topicCache()
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
{
    {
        std::lock_guard<std::mutex> lock(other.m_topicCacheMutex);
        m_topicCache = std::move(other.m_topicCache);
    }

    other.m_domainId         = INVALID_DOMAIN_ID;
    other.m_participantCount = 0;
    other.m_valid            = false;
}

LDomain & LDomain::operator=(LDomain && other) noexcept
{
    if (this != &other)
    {
        m_domainId         = other.m_domainId;
        m_name             = std::move(other.m_name);
        m_participantCount = other.m_participantCount;
        m_valid            = other.m_valid;
        {
            std::lock_guard<std::mutex> lockThis(m_topicCacheMutex);
            std::lock_guard<std::mutex> lockOther(other.m_topicCacheMutex);
            m_topicCache = std::move(other.m_topicCache);
        }

        other.m_domainId         = INVALID_DOMAIN_ID;
        other.m_participantCount = 0;
        other.m_valid            = false;
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

void LDomain::cacheTopicData(uint32_t topic, const std::shared_ptr<void> & object)
{
    if (topic == 0 || !object)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(m_topicCacheMutex);
    m_topicCache[topic] = object;
}

std::shared_ptr<void> LDomain::getTopicData(uint32_t topic) const
{
    std::lock_guard<std::mutex> lock(m_topicCacheMutex);
    const auto                  it = m_topicCache.find(topic);
    if (it == m_topicCache.end())
    {
        return nullptr;
    }
    return it->second;
}

bool LDomain::hasTopicData(uint32_t topic) const
{
    std::lock_guard<std::mutex> lock(m_topicCacheMutex);
    return m_topicCache.find(topic) != m_topicCache.end();
}

void LDomain::clearTopicCache() noexcept
{
    std::lock_guard<std::mutex> lock(m_topicCacheMutex);
    m_topicCache.clear();
}

} // namespace LDdsFramework
