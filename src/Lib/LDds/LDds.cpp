 
/* 详尽设计（伪代码，逐步说明）
- 定义 `LDds::Impl`，包含内部状态：
    - bool initialized = false
    - bool running = false
    - int domain_id = 0
    - unordered_map<string, string> types
    - unordered_map<string, string> topics  // topic -> type_name
    - unordered_map<string, vector<LDds::DataCallback>> subscribers
    - unordered_map<string, string> qos
- `LDds()` 构造：创建 `impl_`
- `~LDds()` 析构：调用 `shutdown()` 和 `stop()`，释放 `impl_`
- `static LDds& instance()`：
    - 使用局部静态单例（Meyers singleton）
- 所有对状态的访问均在 `mutex_` 保护下：
    - `init(domain_id)`：
        - 如果已初始化返回 true
        - 设置 impl_->initialized = true, impl_->domain_id = domain_id
    - `shutdown()`：
        - 如果未初始化直接返回
        - 停止运行，清理类型/主题/订阅（保留或清空，选择清空）
        - impl_->initialized = false
    - `start()`：
        - 需要已初始化且未运行 -> 设置 running = true
    - `stop()`：
        - 如果运行 -> 设置 running = false
    - `isRunning()`, `isInitialized()` 返回对应标志（加锁）
    - `registerType(type_name, type_support_name)`：
        - 如果已存在则覆盖或返回 true
        - 存储到 impl_->types
    - `createTopic(topic_name, type_name)`：
        - 仅当对应 `type_name` 已注册时创建 topic（store in topics）
    - `publish(topic_name, payload)`：
        - 需要已运行且 topic 存在
        - 查找所有订阅者并以 try/catch 逐个调用回调（同步调用以保持实现简单）
    - `subscribe(topic_name, callback)`：
        - 如果 topic 不存在，仍允许订阅以支持先订阅后创建的场景（可改）
        - 将 callback 推入 impl_->subscribers[topic_name]
    - `setQoS(topic_name, qos_profile)`：
        - 存储到 impl_->qos（覆盖）
    - `describe()`：
        - 构建包含 domain_id、initialized、running、注册类型数量、主题数量、订阅者数量的字符串
- 异常安全：
    - 回调调用中捕获异常，避免破坏发布流程
- 线程安全：
    - 使用 `mutex_` 保护共享状态
*/

#include "LDds.h"
#include <sstream>

struct LDds::Impl
{
    bool m_initialized{false};
    bool m_running{false};
    int  m_domainId{0};

    std::unordered_map<std::string, std::string> m_types;         // typeName -> typeSupportName
    std::unordered_map<std::string, std::string> m_topics;        // topicName -> typeName
    std::unordered_map<std::string, std::vector<LDds::DataCallback>> m_subscribers; // topicName -> callbacks
    std::unordered_map<std::string, std::string> m_qos;           // topicName -> qosProfile
};

LDds::LDds()
    : m_pImpl(std::make_unique<Impl>())
{
}

LDds::~LDds()
{
    try
    {
        stop();
        shutdown();
    }
    catch (...)
    {
        // 析构时忽略异常
    }
}

LDds& LDds::instance()
{
    static LDds s_instance;
    return s_instance;
}

bool LDds::init(int domainId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_pImpl)
        m_pImpl = std::make_unique<Impl>();

    if (m_pImpl->m_initialized)
    {
        // 已初始化：更新 domainId 并返回
        m_pImpl->m_domainId = domainId;
        return true;
    }

    m_pImpl->m_domainId = domainId;
    m_pImpl->m_initialized = true;
    return true;
}

void LDds::shutdown()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_pImpl || !m_pImpl->m_initialized)
        return;

    m_pImpl->m_running = false;
    m_pImpl->m_types.clear();
    m_pImpl->m_topics.clear();
    m_pImpl->m_subscribers.clear();
    m_pImpl->m_qos.clear();
    m_pImpl->m_initialized = false;
}

bool LDds::start()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_pImpl || !m_pImpl->m_initialized)
        return false;
    if (m_pImpl->m_running)
        return true;

    m_pImpl->m_running = true;
    return true;
}

bool LDds::stop()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_pImpl || !m_pImpl->m_running)
        return true;

    m_pImpl->m_running = false;
    return true;
}

bool LDds::isRunning() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_pImpl && m_pImpl->m_running;
}

bool LDds::isInitialized() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_pImpl && m_pImpl->m_initialized;
}

bool LDds::registerType(const std::string& typeName, const std::string& typeSupportName)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_pImpl || !m_pImpl->m_initialized)
        return false;
    m_pImpl->m_types[typeName] = typeSupportName;
    return true;
}

bool LDds::createTopic(const std::string& topicName, const std::string& typeName)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_pImpl || !m_pImpl->m_initialized)
        return false;
    auto it = m_pImpl->m_types.find(typeName);
    if (it == m_pImpl->m_types.end())
        return false; // 未注册类型不能创建主题
    m_pImpl->m_topics[topicName] = typeName;
    return true;
}

bool LDds::publish(const std::string& topicName, const std::vector<uint8_t>& payload)
{
    std::vector<LDds::DataCallback> callbacks;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_pImpl || !m_pImpl->m_initialized || !m_pImpl->m_running)
            return false;
        if (m_pImpl->m_topics.find(topicName) == m_pImpl->m_topics.end())
            return false; // 未注册主题

        auto sit = m_pImpl->m_subscribers.find(topicName);
        if (sit != m_pImpl->m_subscribers.end())
            callbacks = sit->second;
    }

    for (auto& cb : callbacks)
    {
        try
        {
            if (cb)
                cb(payload);
        }
        catch (...)
        {
            // 忽略回调异常
        }
    }
    return true;
}

bool LDds::subscribe(const std::string& topicName, DataCallback callback)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_pImpl || !m_pImpl->m_initialized)
        return false;

    // 即使 topic 尚未创建，也允许订阅（支持先订阅后创建）
    m_pImpl->m_subscribers[topicName].push_back(std::move(callback));
    return true;
}

bool LDds::setQoS(const std::string& topicName, const std::string& qosProfile)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_pImpl || !m_pImpl->m_initialized)
        return false;
    m_pImpl->m_qos[topicName] = qosProfile;
    return true;
}

std::string LDds::describe() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::ostringstream oss;
    if (!m_pImpl)
    {
        oss << "LDds: uninitialized (no impl)";
        return oss.str();
    }

    oss << "domainId=" << m_pImpl->m_domainId
        << " initialized=" << (m_pImpl->m_initialized ? "true" : "false")
        << " running=" << (m_pImpl->m_running ? "true" : "false")
        << " types=" << m_pImpl->m_types.size()
        << " topics=" << m_pImpl->m_topics.size();

    size_t subs = 0;
    for (const auto& p : m_pImpl->m_subscribers)
        subs += p.second.size();
    oss << " subscribers=" << subs;

    return oss.str();
}