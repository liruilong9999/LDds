#include "TopicCache.h"

void TopicCache::push(const std::any & data)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_data.push_back(data);
}

std::any TopicCache::front()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_data.empty())
        return std::any();
    return m_data.front();
}

std::any TopicCache::pop()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_data.empty())
        return std::any();
    auto v = m_data.front();
    m_data.erase(m_data.begin());
    return v;
}

size_t TopicCache::size() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_data.size();
}
