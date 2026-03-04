/**
 * @file LDomain.h
 * @brief DDS Domain 与历史缓存管理接口。
 */

#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "LDds_Global.h"
#include "LFindSet.h"

namespace LDdsFramework {

class LQos;
class LParticipant;

/**
 * @brief Domain 标识类型。
 */
using DomainId = uint32_t;

/**
 * @brief 无效 Domain ID。
 */
constexpr DomainId INVALID_DOMAIN_ID = static_cast<DomainId>(-1);

/**
 * @brief 默认 Domain ID。
 */
constexpr DomainId DEFAULT_DOMAIN_ID = 0;

/**
 * @class LDomain
 * @brief 管理 Domain 生命周期与 topic 历史缓存。
 *
 * 该类在阶段 4 中承担缓存层职责：
 * - 每个 topic 保存最近 N 条序列化数据
 * - 提供线程安全写入与读取
 * - 提供快照遍历能力（LFindSet）
 */
class LDDSCORE_EXPORT LDomain final
{
public:
    /**
     * @brief 构造未初始化的 Domain。
     */
    LDomain() noexcept;

    /**
     * @brief 析构函数。
     */
    ~LDomain() noexcept;

    LDomain(const LDomain& other) = delete;
    LDomain& operator=(const LDomain& other) = delete;

    /**
     * @brief 移动构造。
     */
    LDomain(LDomain&& other) noexcept;

    /**
     * @brief 移动赋值。
     */
    LDomain& operator=(LDomain&& other) noexcept;

    /**
     * @brief 创建 Domain。
     * @param domainId 目标 Domain ID。
     * @param pQos 可选 QoS，用于初始化 historyDepth。
     * @return 创建成功返回 true。
     */
    bool create(DomainId domainId, const LQos* pQos);

    /**
     * @brief 销毁 Domain 并清理缓存。
     */
    void destroy() noexcept;

    /**
     * @brief Domain 是否有效。
     */
    bool isValid() const noexcept;

    /**
     * @brief 获取 Domain ID。
     */
    DomainId getDomainId() const noexcept;

    /**
     * @brief 获取 Domain 名称。
     */
    const std::string& getName() const noexcept;

    /**
     * @brief 设置 Domain 名称。
     */
    void setName(const std::string& name);

    /**
     * @brief 获取参与者数量（当前阶段为占位统计）。
     */
    size_t getParticipantCount() const noexcept;

    /**
     * @brief 设置每个 topic 的历史深度。
     */
    void setHistoryDepth(size_t depth) noexcept;

    /**
     * @brief 获取每个 topic 的历史深度。
     */
    size_t getHistoryDepth() const noexcept;

    /**
     * @brief 写入 topic 缓存（线程安全）。
     * @param topic topic id。
     * @param data 序列化 payload。
     * @param dataType 类型名（可选）。
     */
    void cacheTopicData(int topic, const std::vector<uint8_t>& data, const std::string& dataType = std::string());

    /**
     * @brief 根据 topic 查询数据类型名。
     */
    std::string getDataTypeByTopic(int topic) const;

    /**
     * @brief 获取 topic 历史快照视图。
     */
    LFindSet getFindSetByTopic(int topic) const;

    /**
     * @brief 获取最新一条 topic 数据。
     */
    bool getTopicData(int topic, std::vector<uint8_t>& data) const;

    /**
     * @brief 从新到旧遍历 topic 历史数据。
     * @param topic topic id。
     * @param cursorFromNewest 迭代游标（0 表示最新）。
     * @param data 输出数据。
     * @return 若还有数据返回 true，并自动推进游标。
     */
    bool getNextTopicData(int topic, size_t& cursorFromNewest, std::vector<uint8_t>& data) const;

    /**
     * @brief topic 是否存在缓存数据。
     */
    bool hasTopicData(int topic) const;

    /**
     * @brief 清空所有 topic 缓存。
     */
    void clearTopicCache() noexcept;

private:
    DomainId m_domainId;
    std::string m_name;
    size_t m_participantCount;
    bool m_valid;

    size_t m_historyDepth;
    std::map<int, std::deque<std::vector<uint8_t>>> m_topicCache;
    std::map<int, std::string> m_topicDataTypes;
    mutable std::mutex m_topicCacheMutex;
};

} // namespace LDdsFramework
