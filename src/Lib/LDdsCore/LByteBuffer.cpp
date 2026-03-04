#include "LByteBuffer.h"

namespace LDdsFramework {

LByteBuffer::LByteBuffer(size_t initialCapacity)
    : m_buffer(initialCapacity)
    , m_writePos(0)
    , m_readPos(0)
{
}

void LByteBuffer::writeUInt32(uint32_t value)
{
    ensureCapacity(m_writePos + sizeof(uint32_t));
    // 小端序写入
    m_buffer[m_writePos++] = static_cast<uint8_t>(value & 0xFF);
    m_buffer[m_writePos++] = static_cast<uint8_t>((value >> 8) & 0xFF);
    m_buffer[m_writePos++] = static_cast<uint8_t>((value >> 16) & 0xFF);
    m_buffer[m_writePos++] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

void LByteBuffer::writeUInt64(uint64_t value)
{
    ensureCapacity(m_writePos + sizeof(uint64_t));
    // 小端序写入
    m_buffer[m_writePos++] = static_cast<uint8_t>(value & 0xFF);
    m_buffer[m_writePos++] = static_cast<uint8_t>((value >> 8) & 0xFF);
    m_buffer[m_writePos++] = static_cast<uint8_t>((value >> 16) & 0xFF);
    m_buffer[m_writePos++] = static_cast<uint8_t>((value >> 24) & 0xFF);
    m_buffer[m_writePos++] = static_cast<uint8_t>((value >> 32) & 0xFF);
    m_buffer[m_writePos++] = static_cast<uint8_t>((value >> 40) & 0xFF);
    m_buffer[m_writePos++] = static_cast<uint8_t>((value >> 48) & 0xFF);
    m_buffer[m_writePos++] = static_cast<uint8_t>((value >> 56) & 0xFF);
}

void LByteBuffer::writeBytes(const void* data, size_t size)
{
    if (size == 0 || data == nullptr) {
        return;
    }
    ensureCapacity(m_writePos + size);
    std::memcpy(&m_buffer[m_writePos], data, size);
    m_writePos += size;
}

uint32_t LByteBuffer::readUInt32()
{
    if (m_readPos + sizeof(uint32_t) > m_writePos) {
        throw std::out_of_range("LByteBuffer: insufficient data for uint32");
    }
    // 小端序读取
    uint32_t value = 0;
    value |= static_cast<uint32_t>(m_buffer[m_readPos++]);
    value |= static_cast<uint32_t>(m_buffer[m_readPos++]) << 8;
    value |= static_cast<uint32_t>(m_buffer[m_readPos++]) << 16;
    value |= static_cast<uint32_t>(m_buffer[m_readPos++]) << 24;
    return value;
}

uint64_t LByteBuffer::readUInt64()
{
    if (m_readPos + sizeof(uint64_t) > m_writePos) {
        throw std::out_of_range("LByteBuffer: insufficient data for uint64");
    }
    // 小端序读取
    uint64_t value = 0;
    value |= static_cast<uint64_t>(m_buffer[m_readPos++]);
    value |= static_cast<uint64_t>(m_buffer[m_readPos++]) << 8;
    value |= static_cast<uint64_t>(m_buffer[m_readPos++]) << 16;
    value |= static_cast<uint64_t>(m_buffer[m_readPos++]) << 24;
    value |= static_cast<uint64_t>(m_buffer[m_readPos++]) << 32;
    value |= static_cast<uint64_t>(m_buffer[m_readPos++]) << 40;
    value |= static_cast<uint64_t>(m_buffer[m_readPos++]) << 48;
    value |= static_cast<uint64_t>(m_buffer[m_readPos++]) << 56;
    return value;
}

void LByteBuffer::readBytes(void* data, size_t size)
{
    if (size == 0) {
        return;
    }
    if (m_readPos + size > m_writePos) {
        throw std::out_of_range("LByteBuffer: insufficient data for bytes");
    }
    std::memcpy(data, &m_buffer[m_readPos], size);
    m_readPos += size;
}

void LByteBuffer::clear()
{
    m_writePos = 0;
    m_readPos = 0;
}

size_t LByteBuffer::size() const noexcept
{
    return m_writePos;
}

const uint8_t* LByteBuffer::data() const noexcept
{
    return m_buffer.data();
}

uint8_t* LByteBuffer::data() noexcept
{
    return m_buffer.data();
}

void LByteBuffer::setReadPos(size_t pos)
{
    if (pos > m_writePos) {
        throw std::out_of_range("LByteBuffer: invalid read position");
    }
    m_readPos = pos;
}

size_t LByteBuffer::getReadPos() const noexcept
{
    return m_readPos;
}

void LByteBuffer::ensureCapacity(size_t requiredSize)
{
    if (requiredSize > m_buffer.size()) {
        size_t newCapacity = m_buffer.size() * 2;
        if (newCapacity < requiredSize) {
            newCapacity = requiredSize;
        }
        m_buffer.resize(newCapacity);
    }
}

} // namespace LDdsFramework
