/**
 * @file LDomain.h
 * @brief DDS域管理组件
 *
 * 提供DDS域的创建、管理和销毁功能。
 * 域是DDS实体通信的隔离边界，不同域的实体无法直接通信。
 */

#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include "LFindSet.h"
#include "LDds_Global.h"

namespace LDdsFramework {

// 前向声明
class LQos;
class LParticipant;

/**
 * @brief 域标识符类型
 */
using DomainId = uint32_t;

/**
 * @brief 无效域ID常量
 */
constexpr DomainId INVALID_DOMAIN_ID = static_cast<DomainId>(-1);

/**
 * @brief 默认域ID常量
 */
constexpr DomainId DEFAULT_DOMAIN_ID = 0;

/**
 * @class LDomain
 * @brief DDS域管理类
 *
 * 管理DDS域的生命周期，提供域内实体的创建和管理接口。
 * 每个域实例代表一个独立的DDS通信域。
 */
class LDDSCORE_EXPORT LDomain final
{
public:
    /**
     * @brief 默认构造函数
     *
     * 创建一个未初始化的域实例。
     * 必须调用create()后才能使用。
     */
    LDomain() noexcept;

    /**
     * @brief 析构函数
     *
     * 自动销毁域并释放相关资源。
     */
    ~LDomain() noexcept;

    /**
     * @brief 禁止拷贝构造
     */
    LDomain(const LDomain & other) = delete;

    /**
     * @brief 禁止拷贝赋值
     */
    LDomain & operator=(const LDomain & other) = delete;

    /**
     * @brief 允许移动构造
     */
    LDomain(LDomain && other) noexcept;

    /**
     * @brief 允许移动赋值
     */
    LDomain & operator=(LDomain && other) noexcept;

    /**
     * @brief 创建域
     *
     * 初始化域实例，分配域ID和相关资源。
     *
     * @param[in] domainId 域标识符，使用DEFAULT_DOMAIN_ID表示默认域
     * @param[in] pQos 域QoS配置指针，nullptr表示使用默认QoS
     * @return true 创建成功
     * @return false 创建失败，域已存在或资源不足
     */
    bool create(
        DomainId     domainId, /* 域标识符 */
        const LQos * pQos      /* QoS配置指针 */
    );

    /**
     * @brief 销毁域
     *
     * 释放域占用的所有资源，域内所有实体将被销毁。
     * 可以安全地多次调用。
     */
    void destroy() noexcept;

    /**
     * @brief 检查域是否有效
     * @return true 域已创建且有效
     * @return false 域未创建或已销毁
     */
    bool isValid() const noexcept;

    /**
     * @brief 获取域ID
     * @return 域标识符，无效域返回INVALID_DOMAIN_ID
     */
    DomainId getDomainId() const noexcept;

    /**
     * @brief 获取域名称
     * @return 域名称字符串引用
     */
    const std::string & getName() const noexcept;

    /**
     * @brief 设置域名称
     * @param[in] name 新的域名称
     */
    void setName(const std::string & name);

    /**
     * @brief 获取域内参与者数量
     * @return 当前域内的参与者数量
     */
    size_t getParticipantCount() const noexcept;

    void setHistoryDepth(size_t depth) noexcept;
    size_t getHistoryDepth() const noexcept;

    void cacheTopicData(
        int                       topic,
        const std::vector<uint8_t> & data,
        const std::string &       dataType = std::string()
    );
    std::string getDataTypeByTopic(int topic) const;
    LFindSet getFindSetByTopic(int topic) const;
    bool getTopicData(int topic, std::vector<uint8_t> & data) const;
    bool getNextTopicData(int topic, size_t & cursorFromNewest, std::vector<uint8_t> & data) const;
    bool hasTopicData(int topic) const;
    void clearTopicCache() noexcept;

private:
    /**
     * @brief 域ID
     */
    DomainId m_domainId;

    /**
     * @brief 域名称
     */
    std::string m_name;

    /**
     * @brief 参与者数量
     */
    size_t m_participantCount;

    /**
     * @brief 有效性标志
     */
    bool m_valid;
    size_t m_historyDepth;
    std::map<int, std::deque<std::vector<uint8_t>>> m_topicCache;
    std::map<int, std::string>                       m_topicDataTypes;
    mutable std::mutex                               m_topicCacheMutex;
};

} // namespace LDdsFramework
