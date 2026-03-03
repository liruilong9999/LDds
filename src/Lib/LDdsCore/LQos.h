/**
 * @file LQos.h
 * @brief QoS（服务质量）策略管理组件
 *
 * 提供DDS QoS策略定义、配置和管理功能。
 * 支持可靠性、持久性、截止时间等多种QoS策略。
 */

#pragma once

#include <cstdint>
#include <chrono>
#include <memory>
#include <string>
#include <vector>
#include "LDds_Global.h"

namespace LDdsFramework {

/**
 * @brief 无限时长常量（永不过期）
 */
constexpr int64_t DURATION_INFINITY = -1;

/**
 * @brief 无效QoS策略ID
 */
constexpr uint32_t INVALID_QOS_POLICY_ID = 0;

/**
 * @brief QoS策略类型枚举
 */
enum class QosPolicyType : uint32_t
{
    Invalid           = 0,  ///< 无效策略
    Reliability       = 1,  ///< 可靠性策略
    Durability        = 2,  ///< 持久性策略
    Deadline          = 3,  ///< 截止时间策略
    LatencyBudget     = 4,  ///< 延迟预算策略
    Ownership         = 5,  ///< 所有权策略
    Liveliness        = 6,  ///< 活跃度策略
    History           = 7,  ///< 历史策略
    ResourceLimits    = 8,  ///< 资源限制策略
    TransportPriority = 9,  ///< 传输优先级策略
    Lifespan          = 10, ///< 生命周期策略
    UserData          = 11, ///< 用户数据策略
    TopicData         = 12, ///< 主题数据策略
    GroupData         = 13, ///< 组数据策略
    Partition         = 14, ///< 分区策略
    EntityFactory     = 15, ///< 实体工厂策略
    Presentation      = 16, ///< 表示策略
    TimeBasedFilter   = 17, ///< 基于时间的过滤策略
    ContentFilter     = 18, ///< 内容过滤策略
};

/**
 * @brief 可靠性类型枚举
 */
enum class ReliabilityKind : uint32_t
{
    BestEffort = 0, ///< 尽力而为（无传递保证）
    Reliable   = 1  ///< 可靠传递（有保证）
};

/**
 * @brief 持久性类型枚举
 */
enum class DurabilityKind : uint32_t
{
    Volatile       = 0, ///< 易失性（不持久化）
    TransientLocal = 1, ///< 本地临时（对后加入者可见）
    Transient      = 2, ///< 临时性（服务持久化）
    Persistent     = 3  ///< 持久性（存储持久化）
};

/**
 * @brief 历史类型枚举
 */
enum class HistoryKind : uint32_t
{
    KeepLast = 0, ///< 保留最后N个样本
    KeepAll  = 1  ///< 保留所有样本
};

/**
 * @brief 活跃度类型枚举
 */
enum class LivelinessKind : uint32_t
{
    Automatic           = 0, ///< 自动活跃度管理
    ManualByParticipant = 1, ///< 按参与者手动管理
    ManualByTopic       = 2  ///< 按主题手动管理
};

/**
 * @brief 所有权类型枚举
 */
enum class OwnershipKind : uint32_t
{
    Shared    = 0, ///< 共享所有权
    Exclusive = 1  ///< 独占所有权
};

/**
 * @brief 表示模式枚举
 */
enum class PresentationMode : uint32_t
{
    Instance = 0, ///< 实例表示
    Topic    = 1, ///< 主题表示
    Group    = 2  ///< 组表示
};

/**
 * @struct Duration
 * @brief 时长结构
 */
struct Duration
{
    int64_t  seconds;     ///< 秒
    uint32_t nanoseconds; ///< 纳秒

    /**
     * @brief 默认构造函数，初始化为零
     */
    Duration() noexcept
        : seconds(0)
        , nanoseconds(0)
    {}

    /**
     * @brief 从秒数构造
     * @param[in] secs 秒数，负数表示无限
     */
    explicit Duration(int64_t secs) noexcept
        : seconds(secs)
        , nanoseconds(0)
    {}

    /**
     * @brief 检查是否为无限时长
     * @return true 表示无限
     */
    bool isInfinite() const noexcept
    {
        return seconds < 0;
    }

    /**
     * @brief 转换为chrono时长
     * @return chrono毫秒
     */
    std::chrono::milliseconds toChrono() const
    {
        if (isInfinite())
        {
            return std::chrono::milliseconds::max();
        }
        auto totalMs = std::chrono::seconds(seconds) +
                       std::chrono::nanoseconds(nanoseconds);
        return std::chrono::duration_cast<std::chrono::milliseconds>(totalMs);
    }
};

/**
 * @brief 可靠性QoS策略
 */
struct ReliabilityQosPolicy
{
    QosPolicyType   policyId;        ///< 策略ID
    bool            enabled;         ///< 启用标志
    ReliabilityKind kind;            ///< 可靠性类型
    Duration        maxBlockingTime; ///< 最大阻塞时间

    /**
     * @brief 默认构造函数
     */
    ReliabilityQosPolicy() noexcept
        : policyId(QosPolicyType::Reliability)
        , enabled(true)
        , kind(ReliabilityKind::Reliable)
        , maxBlockingTime()
    {}
};

/**
 * @brief 持久性QoS策略
 */
struct DurabilityQosPolicy
{
    QosPolicyType  policyId; ///< 策略ID
    bool           enabled;  ///< 启用标志
    DurabilityKind kind;     ///< 持久性类型

    /**
     * @brief 默认构造函数
     */
    DurabilityQosPolicy() noexcept
        : policyId(QosPolicyType::Durability)
        , enabled(true)
        , kind(DurabilityKind::Volatile)
    {}
};

/**
 * @brief 截止时间QoS策略
 */
struct DeadlineQosPolicy
{
    QosPolicyType policyId; ///< 策略ID
    bool          enabled;  ///< 启用标志
    Duration      period;   ///< 截止时间周期

    /**
     * @brief 默认构造函数
     */
    DeadlineQosPolicy() noexcept
        : policyId(QosPolicyType::Deadline)
        , enabled(false)
        , period(DURATION_INFINITY)
    {}
};

/**
 * @brief 延迟预算QoS策略
 */
struct LatencyBudgetQosPolicy
{
    QosPolicyType policyId; ///< 策略ID
    bool          enabled;  ///< 启用标志
    Duration      duration; ///< 延迟预算时长

    /**
     * @brief 默认构造函数
     */
    LatencyBudgetQosPolicy() noexcept
        : policyId(QosPolicyType::LatencyBudget)
        , enabled(false)
        , duration()
    {}
};

/**
 * @brief 历史QoS策略
 */
struct HistoryQosPolicy
{
    QosPolicyType policyId; ///< 策略ID
    bool          enabled;  ///< 启用标志
    HistoryKind   kind;     ///< 历史类型
    int32_t       depth;    ///< 深度（对KeepLast有效）

    /**
     * @brief 默认构造函数
     */
    HistoryQosPolicy() noexcept
        : policyId(QosPolicyType::History)
        , enabled(true)
        , kind(HistoryKind::KeepLast)
        , depth(1)
    {}
};

/**
 * @brief 资源限制QoS策略
 */
struct ResourceLimitsQosPolicy
{
    QosPolicyType policyId;              ///< 策略ID
    bool          enabled;               ///< 启用标志
    int32_t       maxSamples;            ///< 最大样本数限制
    int32_t       maxInstances;          ///< 最大实例数限制
    int32_t       maxSamplesPerInstance; ///< 每个实例最大样本数

    /**
     * @brief 默认构造函数
     */
    ResourceLimitsQosPolicy() noexcept
        : policyId(QosPolicyType::ResourceLimits)
        , enabled(true)
        , maxSamples(-1) // 无限制
        , maxInstances(-1)
        , maxSamplesPerInstance(-1)
    {}
};

/**
 * @brief 用户数据QoS策略
 */
struct UserDataQosPolicy
{
    QosPolicyType        policyId; ///< 策略ID
    bool                 enabled;  ///< 启用标志
    std::vector<uint8_t> value;    ///< 用户数据字节序列

    /**
     * @brief 默认构造函数
     */
    UserDataQosPolicy() noexcept
        : policyId(QosPolicyType::UserData)
        , enabled(false)
        , value()
    {}
};

/**
 * @brief 综合QoS配置类
 *
 * 包含用于配置实体行为的所有DDS QoS策略。
 */
class LDDSCORE_EXPORT LQos final
{
public:
    /**
     * @brief 默认构造函数
     *
     * 创建带有默认策略的QoS配置。
     */
    LQos() noexcept;

    /**
     * @brief 拷贝构造函数
     */
    LQos(const LQos & other);

    /**
     * @brief 拷贝赋值操作符
     */
    LQos & operator=(const LQos & other);

    /**
     * @brief 移动构造函数
     */
    LQos(LQos && other) noexcept;

    /**
     * @brief 移动赋值操作符
     */
    LQos & operator=(LQos && other) noexcept;

    /**
     * @brief 析构函数
     */
    ~LQos() noexcept;

    /**
     * @brief 重置为默认QoS配置
     */
    void resetToDefaults() noexcept;

    // ==================== QoS策略访问接口 ====================

    /**
     * @brief 设置可靠性策略
     * @param[in] policy 可靠性策略配置
     */
    void setReliability(const ReliabilityQosPolicy & policy) noexcept;

    /**
     * @brief 获取可靠性策略
     * @return 当前可靠性策略的常量引用
     */
    const ReliabilityQosPolicy & getReliability() const noexcept;

    /**
     * @brief 设置持久性策略
     * @param[in] policy 持久性策略配置
     */
    void setDurability(const DurabilityQosPolicy & policy) noexcept;

    /**
     * @brief 获取持久性策略
     * @return 当前持久性策略的常量引用
     */
    const DurabilityQosPolicy & getDurability() const noexcept;

    /**
     * @brief 设置截止时间策略
     * @param[in] policy 截止时间策略配置
     */
    void setDeadline(const DeadlineQosPolicy & policy) noexcept;

    /**
     * @brief 获取截止时间策略
     * @return 当前截止时间策略的常量引用
     */
    const DeadlineQosPolicy & getDeadline() const noexcept;

    /**
     * @brief 设置延迟预算策略
     * @param[in] policy 延迟预算策略配置
     */
    void setLatencyBudget(const LatencyBudgetQosPolicy & policy) noexcept;

    /**
     * @brief 获取延迟预算策略
     * @return 当前延迟预算策略的常量引用
     */
    const LatencyBudgetQosPolicy & getLatencyBudget() const noexcept;

    /**
     * @brief 设置历史策略
     * @param[in] policy 历史策略配置
     */
    void setHistory(const HistoryQosPolicy & policy) noexcept;

    /**
     * @brief 获取历史策略
     * @return 当前历史策略的常量引用
     */
    const HistoryQosPolicy & getHistory() const noexcept;

    /**
     * @brief 设置资源限制策略
     * @param[in] policy 资源限制策略配置
     */
    void setResourceLimits(const ResourceLimitsQosPolicy & policy) noexcept;

    /**
     * @brief 获取资源限制策略
     * @return 当前资源限制策略的常量引用
     */
    const ResourceLimitsQosPolicy & getResourceLimits() const noexcept;

    /**
     * @brief 设置用户数据策略
     * @param[in] policy 用户数据策略配置
     */
    void setUserData(const UserDataQosPolicy & policy);

    /**
     * @brief 获取用户数据策略
     * @return 当前用户数据策略的常量引用
     */
    const UserDataQosPolicy & getUserData() const noexcept;

    /**
     * @brief 验证QoS配置
     *
     * 检查所有策略配置是否一致且有效。
     *
     * @param[out] errorMessage 验证失败时包含错误描述
     * @return true 配置有效
     * @return false 配置无效，errorMessage包含原因
     */
    bool validate(std::string & errorMessage) const;

    /**
     * @brief 检查与另一个QoS配置的兼容性
     *
     * 用于检查发布者和订阅者的QoS是否兼容。
     *
     * @param[in] other 要检查的另一个QoS配置
     * @param[out] errorMessage 不兼容时包含原因
     * @return true 配置兼容
     * @return false 不兼容
     */
    bool isCompatibleWith(
        const LQos &  other,       /* 另一个QoS配置 */
        std::string & errorMessage /* 错误消息输出 */
    ) const;

    /**
     * @brief 合并两个QoS配置
     *
     * 将另一个配置中的显式设置移动到当前配置，
     * 当前配置中已设置的值保持不变。
     *
     * @param[in] other 要合并的QoS配置
     */
    void merge(const LQos & other);

private:
    /**
     * @brief 可靠性策略
     */
    ReliabilityQosPolicy m_reliability;

    /**
     * @brief 持久性策略
     */
    DurabilityQosPolicy m_durability;

    /**
     * @brief 截止时间策略
     */
    DeadlineQosPolicy m_deadline;

    /**
     * @brief 延迟预算策略
     */
    LatencyBudgetQosPolicy m_latencyBudget;

    /**
     * @brief 历史策略
     */
    HistoryQosPolicy m_history;

    /**
     * @brief 资源限制策略
     */
    ResourceLimitsQosPolicy m_resourceLimits;

    /**
     * @brief 用户数据策略
     */
    UserDataQosPolicy m_userData;
};

} // namespace LDdsFramework
