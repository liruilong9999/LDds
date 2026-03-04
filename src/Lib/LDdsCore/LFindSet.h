/**
 * @file LFindSet.h
 * @brief Domain 历史缓存快照遍历器。
 */

#pragma once

#include <cstddef>
#include <deque>
#include <string>
#include <vector>

#include "LDds_Global.h"

namespace LDdsFramework {

/**
 * @class LFindSet
 * @brief topic 历史缓存的只读快照视图。
 *
 * 特性：
 * - 构造时复制快照，后续写入不影响当前遍历
 * - 遍历顺序固定为“从新到旧”
 */
class LDDSCORE_EXPORT LFindSet final
{
public:
    /**
     * @brief 构造空快照。
     */
    LFindSet() noexcept;

    /**
     * @brief 用快照数据构造。
     */
    explicit LFindSet(std::vector<std::vector<uint8_t>> snapshot);

    /**
     * @brief 用 topic 历史队列构造。
     */
    explicit LFindSet(const std::deque<std::vector<uint8_t>>& topicHistory);

    /**
     * @brief 析构函数。
     */
    ~LFindSet() noexcept;

    LFindSet(const LFindSet& other) = default;
    LFindSet& operator=(const LFindSet& other) = default;
    LFindSet(LFindSet&& other) noexcept = default;
    LFindSet& operator=(LFindSet&& other) noexcept = default;

    /**
     * @brief 获取最新一条数据，不推进游标。
     */
    bool getTopicData(std::vector<uint8_t>& data) const;

    /**
     * @brief 获取下一条数据（新到旧），并推进游标。
     */
    bool getNextTopicData(std::vector<uint8_t>& data);

    /**
     * @brief 重置游标到最新位置。
     */
    void reset() noexcept;

    /**
     * @brief 快照条目数。
     */
    std::size_t size() const noexcept;

    /**
     * @brief 是否为空快照。
     */
    bool empty() const noexcept;

private:
    std::vector<std::vector<uint8_t>> m_snapshot;
    std::size_t m_cursor;
};

} // namespace LDdsFramework
