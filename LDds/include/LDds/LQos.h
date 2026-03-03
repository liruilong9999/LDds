/**
 * @file LQos.h
 * @brief QoS (Quality of Service) Policy Management Component
 *
 * Provides DDS QoS policy definitions, configuration, and management.
 * Supports reliability, durability, deadline, and other QoS policies.
 */

#pragma once

#include <cstdint>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace LDdsFramework {

/**
 * @brief Infinite duration constant (never expires)
 */
constexpr int64_t DURATION_INFINITY = -1;

/**
 * @brief Invalid QoS policy ID
 */
constexpr uint32_t INVALID_QOS_POLICY_ID = 0;

/**
 * @brief QoS policy type enumeration
 */
enum class QosPolicyType : uint32_t
{
    Invalid         = 0,    ///< Invalid policy
    Reliability     = 1,    ///< Reliability policy
    Durability      = 2,    ///< Durability policy
    Deadline        = 3,    ///< Deadline policy
    LatencyBudget   = 4,    ///< Latency budget policy
    Ownership       = 5,    ///< Ownership policy
    Liveliness      = 6,    ///< Liveliness policy
    History         = 7,    ///< History policy
    ResourceLimits  = 8,    ///< Resource limits policy
    TransportPriority = 9,  ///< Transport priority policy
    Lifespan        = 10,   ///< Lifespan policy
    UserData        = 11,   ///< User data policy
    TopicData       = 12,   ///< Topic data policy
    GroupData       = 13,   ///< Group data policy
    Partition       = 14,   ///< Partition policy
    EntityFactory   = 15,   ///< Entity factory policy
    Presentation    = 16,   ///< Presentation policy
    TimeBasedFilter = 17,   ///< Time-based filter policy
    ContentFilter   = 18,   ///< Content filter policy
};

/**
 * @brief Reliability kind enumeration
 */
enum class ReliabilityKind : uint32_t
{
    BestEffort = 0,     ///< Best effort (no delivery guarantee)
    Reliable   = 1      ///< Reliable delivery (guaranteed)
};

/**
 * @brief Durability kind enumeration
 */
enum class DurabilityKind : uint32_t
{
    Volatile        = 0,    ///< Volatile (not persisted)
    TransientLocal  = 1,    ///< Transient local (visible to late joiners)
    Transient       = 2,    ///< Transient (service persisted)
    Persistent      = 3     ///< Persistent (storage persisted)
};

/**
 * @brief History kind enumeration
 */
enum class HistoryKind : uint32_t
{
    KeepLast  = 0,      ///< Keep last N samples
    KeepAll   = 1       ///< Keep all samples
};

/**
 * @brief Liveliness kind enumeration
 */
enum class LivelinessKind : uint32_t
{
    Automatic           = 0,    ///< Automatic liveliness management
    ManualByParticipant = 1,    ///< Manual by participant
    ManualByTopic       = 2     ///< Manual by topic
};

/**
 * @brief Ownership kind enumeration
 */
enum class OwnershipKind : uint32_t
{
    Shared      = 0,    ///< Shared ownership
    Exclusive   = 1     ///< Exclusive ownership
};

/**
 * @brief Presentation mode enumeration
 */
enum class PresentationMode : uint32_t
{
    Instance      = 0,  ///< Instance presentation
    Topic         = 1,  ///< Topic presentation
    Group         = 2   ///< Group presentation
};

/**
 * @struct Duration
 * @brief Duration structure
 */
struct Duration
{
    int64_t seconds;        ///< Seconds
    uint32_t nanoseconds;   ///< Nanoseconds

    /**
     * @brief Default constructor, initializes to zero
     */
    Duration() noexcept
        : seconds(0)
        , nanoseconds(0)
    {}

    /**
     * @brief Construct from seconds
     * @param[in] secs Seconds, negative for infinite
     */
    explicit Duration(int64_t secs) noexcept
        : seconds(secs)
        , nanoseconds(0)
    {}

    /**
     * @brief Check if infinite
     * @return true if infinite
     */
    bool isInfinite() const noexcept
    {
        return seconds < 0;
    }

    /**
     * @brief Convert to chrono duration
     * @return chrono milliseconds
     */
    std::chrono::milliseconds toChrono() const
    {
        if (isInfinite()) {
            return std::chrono::milliseconds::max();
        }
        auto totalMs = std::chrono::seconds(seconds) +
                       std::chrono::nanoseconds(nanoseconds);
        return std::chrono::duration_cast<std::chrono::milliseconds>(totalMs);
    }
};

/**
 * @brief Reliability QoS policy
 */
struct ReliabilityQosPolicy
{
    QosPolicyType policyId;         ///< Policy ID
    bool enabled;                   ///< Enabled flag
    ReliabilityKind kind;           ///< Reliability kind
    Duration maxBlockingTime;       ///< Maximum blocking time

    /**
     * @brief Default constructor
     */
    ReliabilityQosPolicy() noexcept
        : policyId(QosPolicyType::Reliability)
        , enabled(true)
        , kind(ReliabilityKind::Reliable)
        , maxBlockingTime()
    {}
};

/**
 * @brief Durability QoS policy
 */
struct DurabilityQosPolicy
{
    QosPolicyType policyId;         ///< Policy ID
    bool enabled;                   ///< Enabled flag
    DurabilityKind kind;            ///< Durability kind

    /**
     * @brief Default constructor
     */
    DurabilityQosPolicy() noexcept
        : policyId(QosPolicyType::Durability)
        , enabled(true)
        , kind(DurabilityKind::Volatile)
    {}
};

/**
 * @brief Deadline QoS policy
 */
struct DeadlineQosPolicy
{
    QosPolicyType policyId;         ///< Policy ID
    bool enabled;                   ///< Enabled flag
    Duration period;                ///< Deadline period

    /**
     * @brief Default constructor
     */
    DeadlineQosPolicy() noexcept
        : policyId(QosPolicyType::Deadline)
        , enabled(false)
        , period(DURATION_INFINITY)
    {}
};

/**
 * @brief Latency budget QoS policy
 */
struct LatencyBudgetQosPolicy
{
    QosPolicyType policyId;         ///< Policy ID
    bool enabled;                   ///< Enabled flag
    Duration duration;              ///< Latency budget duration

    /**
     * @brief Default constructor
     */
    LatencyBudgetQosPolicy() noexcept
        : policyId(QosPolicyType::LatencyBudget)
        , enabled(false)
        , duration()
    {}
};

/**
 * @brief History QoS policy
 */
struct HistoryQosPolicy
{
    QosPolicyType policyId;         ///< Policy ID
    bool enabled;                   ///< Enabled flag
    HistoryKind kind;               ///< History kind
    int32_t depth;                  ///< Depth (valid for KeepLast)

    /**
     * @brief Default constructor
     */
    HistoryQosPolicy() noexcept
        : policyId(QosPolicyType::History)
        , enabled(true)
        , kind(HistoryKind::KeepLast)
        , depth(1)
    {}
};

/**
 * @brief Resource limits QoS policy
 */
struct ResourceLimitsQosPolicy
{
    QosPolicyType policyId;         ///< Policy ID
    bool enabled;                   ///< Enabled flag
    int32_t maxSamples;             ///< Maximum samples limit
    int32_t maxInstances;           ///< Maximum instances limit
    int32_t maxSamplesPerInstance; ///< Maximum samples per instance

    /**
     * @brief Default constructor
     */
    ResourceLimitsQosPolicy() noexcept
        : policyId(QosPolicyType::ResourceLimits)
        , enabled(true)
        , maxSamples(-1)  // Unlimited
        , maxInstances(-1)
        , maxSamplesPerInstance(-1)
    {}
};

/**
 * @brief User data QoS policy
 */
struct UserDataQosPolicy
{
    QosPolicyType policyId;         ///< Policy ID
    bool enabled;                   ///< Enabled flag
    std::vector<uint8_t> value;    ///< User data byte sequence

    /**
     * @brief Default constructor
     */
    UserDataQosPolicy() noexcept
        : policyId(QosPolicyType::UserData)
        , enabled(false)
        , value()
    {}
};

/**
 * @brief Comprehensive QoS configuration class
 *
 * Contains all DDS QoS policies for configuring entity behavior.
 */
class LQos final
{
public:
    /**
     * @brief Default constructor
     *
     * Creates QoS configuration with default policies.
     */
    LQos() noexcept;

    /**
     * @brief Copy constructor
     */
    LQos(const LQos& other);

    /**
     * @brief Copy assignment operator
     */
    LQos& operator=(const LQos& other);

    /**
     * @brief Move constructor
     */
    LQos(LQos&& other) noexcept;

    /**
     * @brief Move assignment operator
     */
    LQos& operator=(LQos&& other) noexcept;

    /**
     * @brief Destructor
     */
    ~LQos() noexcept;

    /**
     * @brief Reset to default QoS configuration
     */
    void resetToDefaults() noexcept;

    // ==================== QoS Policy Access Interfaces ====================

    /**
     * @brief Set reliability policy
     * @param[in] policy Reliability policy configuration
     */
    void setReliability(const ReliabilityQosPolicy& policy) noexcept;

    /**
     * @brief Get reliability policy
     * @return Const reference to current reliability policy
     */
    const ReliabilityQosPolicy& getReliability() const noexcept;

    /**
     * @brief Set durability policy
     * @param[in] policy Durability policy configuration
     */
    void setDurability(const DurabilityQosPolicy& policy) noexcept;

    /**
     * @brief Get durability policy
     * @return Const reference to current durability policy
     */
    const DurabilityQosPolicy& getDurability() const noexcept;

    /**
     * @brief Set deadline policy
     * @param[in] policy Deadline policy configuration
     */
    void setDeadline(const DeadlineQosPolicy& policy) noexcept;

    /**
     * @brief Get deadline policy
     * @return Const reference to current deadline policy
     */
    const DeadlineQosPolicy& getDeadline() const noexcept;

    /**
     * @brief Set latency budget policy
     * @param[in] policy Latency budget policy configuration
     */
    void setLatencyBudget(const LatencyBudgetQosPolicy& policy) noexcept;

    /**
     * @brief Get latency budget policy
     * @return Const reference to current latency budget policy
     */
    const LatencyBudgetQosPolicy& getLatencyBudget() const noexcept;

    /**
     * @brief Set history policy
     * @param[in] policy History policy configuration
     */
    void setHistory(const HistoryQosPolicy& policy) noexcept;

    /**
     * @brief Get history policy
     * @return Const reference to current history policy
     */
    const HistoryQosPolicy& getHistory() const noexcept;

    /**
     * @brief Set resource limits policy
     * @param[in] policy Resource limits policy configuration
     */
    void setResourceLimits(const ResourceLimitsQosPolicy& policy) noexcept;

    /**
     * @brief Get resource limits policy
     * @return Const reference to current resource limits policy
     */
    const ResourceLimitsQosPolicy& getResourceLimits() const noexcept;

    /**
     * @brief Set user data policy
     * @param[in] policy User data policy configuration
     */
    void setUserData(const UserDataQosPolicy& policy);

    /**
     * @brief Get user data policy
     * @return Const reference to current user data policy
     */
    const UserDataQosPolicy& getUserData() const noexcept;

    /**
     * @brief Validate QoS configuration
     *
     * Checks if all policy configurations are consistent and valid.
     *
     * @param[out] errorMessage Contains error description if validation fails
     * @return true Configuration is valid
     * @return false Configuration is invalid, errorMessage contains reason
     */
    bool validate(std::string& errorMessage) const;

    /**
     * @brief Check compatibility with another QoS configuration
     *
     * Used to check if publisher and subscriber QoS are compatible.
     *
     * @param[in] other Another QoS configuration to check
     * @param[out] errorMessage Contains reason if incompatible
     * @return true Configurations are compatible
     * @return false Incompatible
     */
    bool isCompatibleWith(
        const LQos& other,           /* Another QoS configuration */
        std::string& errorMessage   /* Error message output */
    ) const;

    /**
     * @brief Merge two QoS configurations
     *
     * Moves explicit settings from another configuration to current,
     * values already set in current configuration remain unchanged.
     *
     * @param[in] other QoS configuration to merge
     */
    void merge(const LQos& other);

private:
    /**
     * @brief Reliability policy
     */
    ReliabilityQosPolicy m_reliability;

    /**
     * @brief Durability policy
     */
    DurabilityQosPolicy m_durability;

    /**
     * @brief Deadline policy
     */
    DeadlineQosPolicy m_deadline;

    /**
     * @brief Latency budget policy
     */
    LatencyBudgetQosPolicy m_latencyBudget;

    /**
     * @brief History policy
     */
    HistoryQosPolicy m_history;

    /**
     * @brief Resource limits policy
     */
    ResourceLimitsQosPolicy m_resourceLimits;

    /**
     * @brief User data policy
     */
    UserDataQosPolicy m_userData;
};

} // namespace LDdsFramework
