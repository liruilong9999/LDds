#include "LFindSet.h"

#include "LTypeRegistry.h"

#include <utility>

namespace LDdsFramework {

LFindSet::LFindSet() noexcept
    : m_snapshot()
    , m_metadata()
    , m_cursor(0)
    , m_objects()
    , m_pTypeRegistry(nullptr)
    , m_topic(0)
{
}

LFindSet::LFindSet(std::vector<std::vector<uint8_t>> snapshot)
    : m_snapshot(std::move(snapshot))
    , m_metadata(m_snapshot.size())
    , m_cursor(0)
    , m_objects(m_snapshot.size())
    , m_pTypeRegistry(nullptr)
    , m_topic(0)
{
}

LFindSet::LFindSet(
    std::vector<std::vector<uint8_t>> snapshot,
    std::vector<DdsSampleMetadata> metadata)
    : m_snapshot(std::move(snapshot))
    , m_metadata(std::move(metadata))
    , m_cursor(0)
    , m_objects(m_snapshot.size())
    , m_pTypeRegistry(nullptr)
    , m_topic(0)
{
    if (m_metadata.size() < m_snapshot.size())
    {
        m_metadata.resize(m_snapshot.size());
    }
}

LFindSet::LFindSet(const std::deque<std::vector<uint8_t>> & topicHistory)
    : m_snapshot()
    , m_metadata()
    , m_cursor(0)
    , m_objects()
    , m_pTypeRegistry(nullptr)
    , m_topic(0)
{
    m_snapshot.reserve(topicHistory.size());
    for (auto it = topicHistory.rbegin(); it != topicHistory.rend(); ++it)
    {
        m_snapshot.push_back(*it);
    }
    m_objects.resize(m_snapshot.size());
    m_metadata.resize(m_snapshot.size());
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

const DdsSampleMetadata * LFindSet::getFirstMetadata() const noexcept
{
    return getMetadata(0);
}

const DdsSampleMetadata * LFindSet::getMetadata(std::size_t index) const noexcept
{
    if (index >= m_metadata.size())
    {
        return nullptr;
    }
    return &m_metadata[index];
}

void LFindSet::bindTypeRegistry(const LTypeRegistry * typeRegistry, uint32_t topic) noexcept
{
    m_pTypeRegistry = typeRegistry;
    m_topic = topic;
    m_objects.clear();
    m_objects.resize(m_snapshot.size());
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

std::shared_ptr<void> LFindSet::getDataObjectAt(std::size_t index) const
{
    if (index >= m_snapshot.size() || m_pTypeRegistry == nullptr || m_topic == 0)
    {
        return nullptr;
    }

    if (m_objects.size() != m_snapshot.size())
    {
        m_objects.resize(m_snapshot.size());
    }

    std::shared_ptr<void> & object = m_objects[index];
    if (!object)
    {
        object = m_pTypeRegistry->createByTopic(m_topic);
        if (!object)
        {
            return nullptr;
        }

        if (!m_pTypeRegistry->deserializeByTopic(m_topic, m_snapshot[index], object.get()))
        {
            object.reset();
            return nullptr;
        }
    }

    return object;
}

} // namespace LDdsFramework
