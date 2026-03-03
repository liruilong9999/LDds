/**
 * @file LDomain.cpp
 * @brief LDomain class implementation
 */

#include "LDomain.h"

namespace LDdsFramework {

LDomain::LDomain() noexcept
    : m_domainId(INVALID_DOMAIN_ID)
    , m_name()
    , m_participantCount(0)
    , m_valid(false)
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

        other.m_domainId         = INVALID_DOMAIN_ID;
        other.m_participantCount = 0;
        other.m_valid            = false;
    }
    return *this;
}

bool LDomain::create(DomainId domainId, const LQos * pQos)
{
    (void)domainId;
    (void)pQos;
    return false;
}

void LDomain::destroy() noexcept
{
    m_valid            = false;
    m_domainId         = INVALID_DOMAIN_ID;
    m_participantCount = 0;
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

} // namespace LDdsFramework
