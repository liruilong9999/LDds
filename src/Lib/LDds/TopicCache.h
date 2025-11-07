#pragma once

#include <vector>
#include <any>
#include <mutex>

class TopicCache
{
public:
    TopicCache()  = default;
    ~TopicCache() = default;

    void     push(const std::any & data);
    std::any front();
    std::any pop();
    size_t   size() const;

private:
    std::vector<std::any> m_data;
    mutable std::mutex    m_mutex;
};
