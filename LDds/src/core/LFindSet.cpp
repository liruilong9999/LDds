/**
 * @file LFindSet.cpp
 * @brief LFindSet类实现
 */

#include "LDds/LFindSet.h"

namespace LDdsFramework {

LFindSet::LFindSet() noexcept
    : m_parent()
    , m_rank()
    , m_setCount(0)
{
}

LFindSet::LFindSet(size_t capacity)
    : m_parent(capacity)
    , m_rank(capacity, 0)
    , m_setCount(capacity)
{
    for (size_t i = 0; i < capacity; ++i) {
        m_parent[i] = static_cast<ElementId>(i);
    }
}

LFindSet::~LFindSet() noexcept = default;

LFindSet::LFindSet(LFindSet&& other) noexcept
    : m_parent(std::move(other.m_parent))
    , m_rank(std::move(other.m_rank))
    , m_setCount(other.m_setCount)
{
    other.m_setCount = 0;
}

LFindSet& LFindSet::operator=(LFindSet&& other) noexcept
{
    if (this != &other) {
        m_parent = std::move(other.m_parent);
        m_rank = std::move(other.m_rank);
        m_setCount = other.m_setCount;
        other.m_setCount = 0;
    }
    return *this;
}

void LFindSet::init(size_t count)
{
    m_parent.resize(count);
    m_rank.assign(count, 0);
    m_setCount = count;

    for (size_t i = 0; i < count; ++i) {
        m_parent[i] = static_cast<ElementId>(i);
    }
}

void LFindSet::clear() noexcept
{
    m_parent.clear();
    m_rank.clear();
    m_setCount = 0;
}

ElementId LFindSet::find(ElementId element)
{
    (void)element;
    return INVALID_ELEMENT_ID;
}

bool LFindSet::unite(ElementId elementA, ElementId elementB)
{
    (void)elementA;
    (void)elementB;
    return false;
}

bool LFindSet::isConnected(ElementId elementA, ElementId elementB)
{
    (void)elementA;
    (void)elementB;
    return false;
}

size_t LFindSet::getSetCount() const noexcept
{
    return m_setCount;
}

size_t LFindSet::getElementCount() const noexcept
{
    return m_parent.size();
}

bool LFindSet::isValidElement(ElementId element) const noexcept
{
    return element < m_parent.size();
}

void LFindSet::enumerateSet(
    ElementId setId,
    const std::function<bool(ElementId)>& callback) const
{
    (void)setId;
    (void)callback;
}

} // namespace LDdsFramework
