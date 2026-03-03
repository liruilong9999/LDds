/**
 * @file LQos.cpp
 * @brief LQos class implementation
 */

#include "LDds/LQos.h"

namespace LDdsFramework {

LQos::LQos() noexcept
    : m_reliability()
    , m_durability()
    , m_deadline()
    , m_latencyBudget()
    , m_history()
    , m_resourceLimits()
    , m_userData()
{
}

LQos::LQos(const LQos& other) = default;

LQos& LQos::operator=(const LQos& other) = default;

LQos::LQos(LQos&& other) noexcept = default;

LQos& LQos::operator=(LQos&& other) noexcept = default;

LQos::~LQos() noexcept = default;

void LQos::resetToDefaults() noexcept
{
    m_reliability = ReliabilityQosPolicy();
    m_durability = DurabilityQosPolicy();
    m_deadline = DeadlineQosPolicy();
    m_latencyBudget = LatencyBudgetQosPolicy();
    m_history = HistoryQosPolicy();
    m_resourceLimits = ResourceLimitsQosPolicy();
    m_userData = UserDataQosPolicy();
}

void LQos::setReliability(const ReliabilityQosPolicy& policy) noexcept
{
    m_reliability = policy;
}

const ReliabilityQosPolicy& LQos::getReliability() const noexcept
{
    return m_reliability;
}

void LQos::setDurability(const DurabilityQosPolicy& policy) noexcept
{
    m_durability = policy;
}

const DurabilityQosPolicy& LQos::getDurability() const noexcept
{
    return m_durability;
}

void LQos::setDeadline(const DeadlineQosPolicy& policy) noexcept
{
    m_deadline = policy;
}

const DeadlineQosPolicy& LQos::getDeadline() const noexcept
{
    return m_deadline;
}

void LQos::setLatencyBudget(const LatencyBudgetQosPolicy& policy) noexcept
{
    m_latencyBudget = policy;
}

const LatencyBudgetQosPolicy& LQos::getLatencyBudget() const noexcept
{
    return m_latencyBudget;
}

void LQos::setHistory(const HistoryQosPolicy& policy) noexcept
{
    m_history = policy;
}

const HistoryQosPolicy& LQos::getHistory() const noexcept
{
    return m_history;
}

void LQos::setResourceLimits(const ResourceLimitsQosPolicy& policy) noexcept
{
    m_resourceLimits = policy;
}

const ResourceLimitsQosPolicy& LQos::getResourceLimits() const noexcept
{
    return m_resourceLimits;
}

void LQos::setUserData(const UserDataQosPolicy& policy)
{
    m_userData = policy;
}

const UserDataQosPolicy& LQos::getUserData() const noexcept
{
    return m_userData;
}

bool LQos::validate(std::string& errorMessage) const
{
    (void)errorMessage;
    return true;
}

bool LQos::isCompatibleWith(const LQos& other, std::string& errorMessage) const
{
    (void)other;
    (void)errorMessage;
    return true;
}

void LQos::merge(const LQos& other)
{
    (void)other;
}

} // namespace LDdsFramework
