#ifndef LDDS_H
#define LDDS_H

#include "LDds_global.h"
#include <string>
#include <functional>
#include <memory>
#include <vector>
#include <mutex>
#include <unordered_map>

#include <any>

class LDDS_EXPORT LDds
{
public:
    // 禁止拷贝（在源文件中不定义以防使用）
    LDds(const LDds& other);
    LDds& operator=(const LDds& other);

    // 获取单例
    static LDds& instance();

    // 初始化与释放
    // domainId: DDS 域 ID（默认 0）
    bool init(int domainId = 0);
    void shutdown();

    // 启动/停止数据分发（与 init/shutdown 区分生命周期）
    bool start();
    bool stop();
    bool isRunning() const;
    bool isInitialized() const;

    // 注册数据类型（typeSupportName 可用于桥接具体中间件）
    bool registerType(const std::string& typeName, const std::string& typeSupportName);

    // 创建主题
    bool createTopic(const std::string& topicName, const std::string& typeName);

    // 发布接口（通用二进制负载）
    // payload: 序列化后的数据
    bool publish(const std::string& topicName, const std::vector<uint8_t>& payload);

    // 订阅接口：回调接收二进制负载
    using DataCallback = std::function<void(const std::vector<uint8_t>& payload)>;
    // 返回 true 表示订阅成功
    bool subscribe(const std::string& topicName, DataCallback callback);

    // QoS 占位：根据实现可扩展具体策略
    bool setQoS(const std::string& topicName, const std::string& qosProfile);

    // 获取底层实现状态（调试/监控用）
    std::string describe() const;

    ~LDds(); 

private:
    // 私有构造，强制单例
    LDds(); 

    // PIMPL：隐藏具体 DDS 实现细节
    struct Impl;
    std::unique_ptr<Impl> m_pImpl;

    // 保护状态的互斥锁
    mutable std::mutex m_mutex;
};

#endif // LDDS_H
