#include "LFindSet.h"

#include <utility>

namespace LDdsFramework {

LFindSet::LFindSet() noexcept
    : m_snapshot()
    , m_cursor(0)
{
}

LFindSet::LFindSet(std::vector<std::vector<uint8_t>> snapshot)
    : m_snapshot(std::move(snapshot))
    , m_cursor(0)
{
}

LFindSet::LFindSet(const std::deque<std::vector<uint8_t>> & topicHistory)
    : m_snapshot()
    , m_cursor(0)
{
    m_snapshot.reserve(topicHistory.size());
    for (auto it = topicHistory.rbegin(); it != topicHistory.rend(); ++it)
    {
        m_snapshot.push_back(*it);
    }
}

LFindSet::~LFindSet() noexcept = default;

bool LFindSet::getTopicData(std::vector<uint8_t> & data) const
{
    if (m_cursor >= m_snapshot.size())
    {
        return false;
    }

    data = m_snapshot[m_cursor];
    return true;
}

bool LFindSet::getNextTopicData(std::vector<uint8_t> & data)
{
    if (m_cursor >= m_snapshot.size())
    {
        return false;
    }

    data = m_snapshot[m_cursor];
    ++m_cursor;
    return true;
}

void LFindSet::reset() noexcept
{
    m_cursor = 0;
}

std::size_t LFindSet::size() const noexcept
{
    return m_snapshot.size();
}

bool LFindSet::empty() const noexcept
{
    return m_snapshot.empty();
}

} // namespace LDdsFramework
