/**
 * @file LQos.h
 * @brief QoS policies
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "LDds_Global.h"

namespace LDdsFramework {

constexpr int64_t DURATION_INFINITY = -1;
constexpr uint32_t INVALID_QOS_POLICY_ID = 0;

enum class QosPolicyType : uint32_t
{
    Invalid = 0,
    Reliability = 1,
    Durability = 2,
    Deadline = 3,
    LatencyBudget = 4,
    Ownership = 5,
    Liveliness = 6,
    History = 7,
    ResourceLimits = 8,
    TransportPriority = 9,
    Lifespan = 10,
    UserData = 11,
    TopicData = 12,
    GroupData = 13,
    Partition = 14,
    EntityFactory = 15,
    Presentation = 16,
    TimeBasedFilter = 17,
    ContentFilter = 18,
};

enum class ReliabilityKind : uint32_t
{
    BestEffort = 0,
    Reliable = 1,
};

enum class DurabilityKind : uint32_t
{
    Volatile = 0,
    TransientLocal = 1,
    Transient = 2,
    Persistent = 3,
};

enum class HistoryKind : uint32_t
{
    KeepLast = 0,
    KeepAll = 1,
};

enum class LivelinessKind : uint32_t
{
    Automatic = 0,
    ManualByParticipant = 1,
    ManualByTopic = 2,
};

enum class OwnershipKind : uint32_t
{
    Shared = 0,
    Exclusive = 1,
};

enum class PresentationMode : uint32_t
{
    Instance = 0,
    Topic = 1,
    Group = 2,
};

enum class TransportType : uint32_t
{
    UDP = 0,
    TCP = 1,
};

struct Duration
{
    int64_t seconds;
    uint32_t nanoseconds;

    Duration() noexcept
        : seconds(0)
        , nanoseconds(0)
    {
    }

    explicit Duration(int64_t secs) noexcept
        : seconds(secs)
        , nanoseconds(0)
    {
    }

    bool isInfinite() const noexcept
    {
        return seconds < 0;
    }

    std::chrono::milliseconds toChrono() const
    {
        if (isInfinite())
        {
            return std::chrono::milliseconds::max();
        }
        const auto total = std::chrono::seconds(seconds) + std::chrono::nanoseconds(nanoseconds);
        return std::chrono::duration_cast<std::chrono::milliseconds>(total);
    }
};

struct ReliabilityQosPolicy
{
    QosPolicyType policyId;
    bool enabled;
    ReliabilityKind kind;
    Duration maxBlockingTime;

    ReliabilityQosPolicy() noexcept
        : policyId(QosPolicyType::Reliability)
        , enabled(true)
        , kind(ReliabilityKind::Reliable)
        , maxBlockingTime()
    {
    }
};

struct DurabilityQosPolicy
{
    QosPolicyType policyId;
    bool enabled;
    DurabilityKind kind;

    DurabilityQosPolicy() noexcept
        : policyId(QosPolicyType::Durability)
        , enabled(true)
        , kind(DurabilityKind::Volatile)
    {
    }
};

struct DeadlineQosPolicy
{
    QosPolicyType policyId;
    bool enabled;
    Duration period;

    DeadlineQosPolicy() noexcept
        : policyId(QosPolicyType::Deadline)
        , enabled(false)
        , period(DURATION_INFINITY)
    {
    }
};

struct LatencyBudgetQosPolicy
{
    QosPolicyType policyId;
    bool enabled;
    Duration duration;

    LatencyBudgetQosPolicy() noexcept
        : policyId(QosPolicyType::LatencyBudget)
        , enabled(false)
        , duration()
    {
    }
};

struct HistoryQosPolicy
{
    QosPolicyType policyId;
    bool enabled;
    HistoryKind kind;
    int32_t depth;

    HistoryQosPolicy() noexcept
        : policyId(QosPolicyType::History)
        , enabled(true)
        , kind(HistoryKind::KeepLast)
        , depth(1)
    {
    }
};

struct ResourceLimitsQosPolicy
{
    QosPolicyType policyId;
    bool enabled;
    int32_t maxSamples;
    int32_t maxInstances;
    int32_t maxSamplesPerInstance;

    ResourceLimitsQosPolicy() noexcept
        : policyId(QosPolicyType::ResourceLimits)
        , enabled(true)
        , maxSamples(-1)
        , maxInstances(-1)
        , maxSamplesPerInstance(-1)
    {
    }
};

struct UserDataQosPolicy
{
    QosPolicyType policyId;
    bool enabled;
    std::vector<uint8_t> value;

    UserDataQosPolicy() noexcept
        : policyId(QosPolicyType::UserData)
        , enabled(false)
        , value()
    {
    }
};

class LDDSCORE_EXPORT LQos final
{
public:
    LQos() noexcept;
    LQos(const LQos & other);
    LQos & operator=(const LQos & other);
    LQos(LQos && other) noexcept;
    LQos & operator=(LQos && other) noexcept;
    ~LQos() noexcept;

    void resetToDefaults() noexcept;

    void setTransportType(TransportType type) noexcept;
    TransportType getTransportType() const noexcept;

    void setReliability(const ReliabilityQosPolicy & policy) noexcept;
    const ReliabilityQosPolicy & getReliability() const noexcept;

    void setDurability(const DurabilityQosPolicy & policy) noexcept;
    const DurabilityQosPolicy & getDurability() const noexcept;

    void setDeadline(const DeadlineQosPolicy & policy) noexcept;
    const DeadlineQosPolicy & getDeadline() const noexcept;

    void setLatencyBudget(const LatencyBudgetQosPolicy & policy) noexcept;
    const LatencyBudgetQosPolicy & getLatencyBudget() const noexcept;

    void setHistory(const HistoryQosPolicy & policy) noexcept;
    const HistoryQosPolicy & getHistory() const noexcept;

    void setResourceLimits(const ResourceLimitsQosPolicy & policy) noexcept;
    const ResourceLimitsQosPolicy & getResourceLimits() const noexcept;

    void setUserData(const UserDataQosPolicy & policy);
    const UserDataQosPolicy & getUserData() const noexcept;

    bool validate(std::string & errorMessage) const;
    bool isCompatibleWith(const LQos & other, std::string & errorMessage) const;
    void merge(const LQos & other);

    // Stage-7 simplified QoS knobs:
    // - historyDepth: cache depth for each topic, minimum 1
    // - deadlineMs: deadline timeout in milliseconds, <=0 means disabled
    // - reliable: reserved flag for future reliable delivery behavior
    int32_t historyDepth;
    int32_t deadlineMs;
    bool    reliable;

    TransportType transportType;

    // QoS XML config (preferred): parse and apply known fields.
    bool loadFromXmlFile(const std::string & filePath, std::string * errorMessage = nullptr);
    bool loadFromXmlString(const std::string & xmlText, std::string * errorMessage = nullptr);

private:
    ReliabilityQosPolicy m_reliability;
    DurabilityQosPolicy m_durability;
    DeadlineQosPolicy m_deadline;
    LatencyBudgetQosPolicy m_latencyBudget;
    HistoryQosPolicy m_history;
    ResourceLimitsQosPolicy m_resourceLimits;
    UserDataQosPolicy m_userData;
};

} // namespace LDdsFramework
