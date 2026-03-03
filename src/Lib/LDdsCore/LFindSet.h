/**
 * @file LFindSet.h
 * @brief 并查集数据结构实现
 *
 * 提供高效的集合合并与查询操作，用于：
 * - 实体分组管理
 * - 连通性检测
 * - 等价类划分
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
#include "LDds_Global.h"

namespace LDdsFramework {

/**
 * @brief 元素标识符类型
 */
using ElementId = size_t;

/**
 * @brief 无效元素ID常量
 */
constexpr ElementId INVALID_ELEMENT_ID = static_cast<ElementId>(-1);

/**
 * @brief 集合标识符类型
 */
using SetId = size_t;

/**
 * @brief 无效集合ID常量
 */
constexpr SetId INVALID_SET_ID = static_cast<SetId>(-1);

/**
 * @class LFindSet
 * @brief 并查集(Union-Find)数据结构
 *
 * 使用路径压缩和按秩合并优化，提供接近常数时间的
 * 集合查询和合并操作。
 */
class LDDSCORE_EXPORT LFindSet final
{
public:
    /**
     * @brief 默认构造函数
     *
     * 创建一个空的并查集，需要调用init()初始化。
     */
    LFindSet() noexcept;

    /**
     * @brief 带容量构造函数
     *
     * 创建并初始化指定容量的并查集。
     *
     * @param[in] capacity 初始元素容量
     */
    explicit LFindSet(size_t capacity);

    /**
     * @brief 析构函数
     */
    ~LFindSet() noexcept;

    /**
     * @brief 禁止拷贝构造
     */
    LFindSet(const LFindSet & other) = delete;

    /**
     * @brief 禁止拷贝赋值
     */
    LFindSet & operator=(const LFindSet & other) = delete;

    /**
     * @brief 允许移动构造
     */
    LFindSet(LFindSet && other) noexcept;

    /**
     * @brief 允许移动赋值
     */
    LFindSet & operator=(LFindSet && other) noexcept;

    /**
     * @brief 初始化并查集
     *
     * 重置并查集，创建指定数量的独立集合。
     * 每个元素初始时属于独立的集合。
     *
     * @param[in] count 元素数量，每个元素初始为独立集合
     */
    void init(size_t count);

    /**
     * @brief 清空并查集
     *
     * 移除所有元素，释放内存。
     */
    void clear() noexcept;

    /**
     * @brief 查找元素所在集合的代表元
     *
     * 使用路径压缩优化，使后续查询更快。
     *
     * @param[in] element 要查找的元素ID
     * @return 元素所在集合的代表元ID，无效元素返回INVALID_ELEMENT_ID
     */
    ElementId find(ElementId element);

    /**
     * @brief 合并两个元素所在的集合
     *
     * 使用按秩合并优化，将较小秩的集合合并到较大秩的集合。
     *
     * @param[in] elementA 第一个元素ID
     * @param[in] elementB 第二个元素ID
     * @return true 合并成功，两元素原本在不同集合
     * @return false 合并失败，两元素已在同一集合或参数无效
     */
    bool unite(ElementId elementA, ElementId elementB);

    /**
     * @brief 检查两个元素是否在同一个集合
     *
     * @param[in] elementA 第一个元素ID
     * @param[in] elementB 第二个元素ID
     * @return true 两元素在同一集合
     * @return false 两元素在不同集合或参数无效
     */
    bool isConnected(ElementId elementA, ElementId elementB);

    /**
     * @brief 获取集合数量
     *
     * @return 当前独立集合的数量
     */
    size_t getSetCount() const noexcept;

    /**
     * @brief 获取元素数量
     *
     * @return 当前管理的元素总数
     */
    size_t getElementCount() const noexcept;

    /**
     * @brief 检查元素ID是否有效
     *
     * @param[in] element 要检查的元素ID
     * @return true 元素ID有效
     * @return false 元素ID无效
     */
    bool isValidElement(ElementId element) const noexcept;

    /**
     * @brief 遍历指定集合中的所有元素
     *
     * @param[in] setId 集合ID（代表元）
     * @param[in] callback 回调函数，参数为元素ID，返回false停止遍历
     */
    void enumerateSet(
        ElementId                              setId,   /* 集合代表元 */
        const std::function<bool(ElementId)> & callback /* 遍历回调 */
    ) const;

private:
    /**
     * @brief 父节点数组，m_parent[i]表示元素i的父节点
     */
    std::vector<ElementId> m_parent;

    /**
     * @brief 秩数组，m_rank[i]表示以i为代表元的集合的秩
     */
    std::vector<uint8_t> m_rank;

    /**
     * @brief 当前独立集合数量
     */
    size_t m_setCount;
};

} // namespace LDdsFramework
