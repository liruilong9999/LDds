#pragma once

#include <cstddef>
#include <deque>
#include <string>
#include <vector>

#include "LDds_Global.h"

namespace LDdsFramework {

// Stage-4 find-set snapshot view:
// - takes a point-in-time copy of topic history
// - traversal order is newest -> oldest
// - later cache writes do not affect this instance
class LDDSCORE_EXPORT LFindSet final
{
public:
    LFindSet() noexcept;
    explicit LFindSet(std::vector<std::vector<uint8_t>> snapshot);
    explicit LFindSet(const std::deque<std::vector<uint8_t>> & topicHistory);
    ~LFindSet() noexcept;

    LFindSet(const LFindSet & other)            = default;
    LFindSet & operator=(const LFindSet & other) = default;
    LFindSet(LFindSet && other) noexcept        = default;
    LFindSet & operator=(LFindSet && other) noexcept = default;

    bool getTopicData(std::vector<uint8_t> & data) const;
    bool getNextTopicData(std::vector<uint8_t> & data);

    void reset() noexcept;
    std::size_t size() const noexcept;
    bool empty() const noexcept;

private:
    std::vector<std::vector<uint8_t>> m_snapshot;
    std::size_t                       m_cursor;
};

} // namespace LDdsFramework

