/**
 * @file LQos.h
 * @brief QoS 策略与配置模型定义。
 *
 * 该文件定义了：
 * 1. 常用 DDS 风格策略枚举与策略结构体；
 * 2. `LQos` 统一配置对象；
 * 3. XML 加载、配置校验、兼容性检查与合并入口。
 */
#ifndef LDDSFRAMEWORK_LQOS_H_
#define LDDSFRAMEWORK_LQOS_H_

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "LDds_Global.h"

namespace LDdsFramework {

/**
 * @brief 无限时长常量（秒字段使用负值表示）。
 */
constexpr int64_t DURATION_INFINITY = -1;
/**
 * @brief 无效 QoS 策略 ID。
 */
constexpr uint32_t INVALID_QOS_POLICY_ID = 0;

/**
 * @brief QoS 策略类型标识。
 */
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

/**
 * @brief 可靠性等级。
 */
enum class ReliabilityKind : uint32_t
{
    BestEffort = 0,
    Reliable = 1,
};

/**
 * @brief 持久化等级。
 */
enum class DurabilityKind : uint32_t
{
    Volatile = 0,
    TransientLocal = 1,
    Transient = 2,
    Persistent = 3,
};

/**
 * @brief 历史缓存策略。
 */
enum class HistoryKind : uint32_t
{
    KeepLast = 0,
    KeepAll = 1,
};

/**
 * @brief 存活检测模式。
 */
enum class LivelinessKind : uint32_t
{
    Automatic = 0,
    ManualByParticipant = 1,
    ManualByTopic = 2,
};

/**
 * @brief 所有权模式。
 */
enum class OwnershipKind : uint32_t
{
    Shared = 0,
    Exclusive = 1,
};

/**
 * @brief 展示一致性范围。
 */
enum class PresentationMode : uint32_t
{
    Instance = 0,
    Topic = 1,
    Group = 2,
};

/**
 * @brief 传输类型。
 */
enum class TransportType : uint32_t
{
    UDP = 0,
    TCP = 1,
};

/**
 * @brief 时长结构（秒+纳秒）。
 */
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

struct OwnershipQosPolicy
{
    QosPolicyType policyId;
    bool enabled;
    OwnershipKind kind;
    int32_t strength;

    OwnershipQosPolicy() noexcept
        : policyId(QosPolicyType::Ownership)
        , enabled(true)
        , kind(OwnershipKind::Shared)
        , strength(0)
    {
    }
};

class LDDSCORE_EXPORT LQos final
{
public:
    /**
     * @brief 构造与拷贝/移动语义。
     */
    LQos() noexcept;
    LQos(const LQos & other);
    LQos & operator=(const LQos & other);
    LQos(LQos && other) noexcept;
    LQos & operator=(LQos && other) noexcept;
    ~LQos() noexcept;

    /**
     * @brief 恢复默认配置。
     */
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

    void setOwnership(const OwnershipQosPolicy & policy) noexcept;
    const OwnershipQosPolicy & getOwnership() const noexcept;

    void setUserData(const UserDataQosPolicy & policy);
    const UserDataQosPolicy & getUserData() const noexcept;

    /**
     * @brief 校验当前 QoS 是否自洽。
     * @param errorMessage 输出错误信息。
     */
    bool validate(std::string & errorMessage) const;
    /**
     * @brief 检查与另一组 QoS 的兼容性。
     */
    bool isCompatibleWith(const LQos & other, std::string & errorMessage) const;
    /**
     * @brief 将另一组 QoS 合并到当前对象。
     */
    void merge(const LQos & other);

    /**
     * @brief 每个 topic 的历史缓存深度（最小 1）。
     */
    int32_t historyDepth;
    /**
     * @brief 截止时间阈值（毫秒），<=0 表示关闭。
     */
    int32_t deadlineMs;
    /**
     * @brief 可靠性预留开关（阶段 7 预留位）。
     */
    bool    reliable;
    DurabilityKind durabilityKind;
    OwnershipKind ownershipKind;
    int32_t ownershipStrength;

    /**
     * @brief 传输类型选择（UDP/TCP）。
     */
    TransportType transportType;
    /**
     * @brief Domain ID（0-255）。
     */
    uint8_t domainId;
    /**
     * @brief 是否启用按 Domain 映射端口。
     */
    bool enableDomainPortMapping;
    /**
     * @brief 映射基准端口。
     */
    uint16_t basePort;
    /**
     * @brief Domain 端口偏移量。
     */
    uint16_t domainPortOffset;
    /**
     * @brief 持久化数据库路径（Durability=Persistent 时使用）。
     */
    std::string durabilityDbPath;
    /**
     * @brief 是否启用指标输出。
     */
    bool enableMetrics;
    /**
     * @brief 指标服务端口。
     */
    uint16_t metricsPort;
    /**
     * @brief 指标服务绑定地址。
     */
    std::string metricsBindAddress;
    /**
     * @brief 是否启用结构化日志。
     */
    bool structuredLogEnabled;
    /**
     * @brief 是否启用安全能力。
     */
    bool securityEnabled;
    /**
     * @brief 是否启用 payload 加密。
     */
    bool securityEncryptPayload;
    /**
     * @brief 预共享密钥（PSK）。
     */
    std::string securityPsk;

    /**
     * @brief 从 XML 文件加载 QoS（优先配置方式）。
     */
    bool loadFromXmlFile(const std::string & filePath, std::string * errorMessage = nullptr);
    /**
     * @brief 从 XML 文本加载 QoS。
     */
    bool loadFromXmlString(const std::string & xmlText, std::string * errorMessage = nullptr);

private:
    ReliabilityQosPolicy m_reliability;
    DurabilityQosPolicy m_durability;
    DeadlineQosPolicy m_deadline;
    LatencyBudgetQosPolicy m_latencyBudget;
    HistoryQosPolicy m_history;
    ResourceLimitsQosPolicy m_resourceLimits;
    OwnershipQosPolicy m_ownership;
    UserDataQosPolicy m_userData;
};

} // namespace LDdsFramework

#endif // LDDSFRAMEWORK_LQOS_H_
