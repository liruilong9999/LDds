#ifndef PUBSUB_DEMO_DEFINE_H
#define PUBSUB_DEMO_DEFINE_H

#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

#include "pubsub_demo_export.h"
#include "LByteBuffer.h"

namespace LDdsFramework {
namespace idl_detail {
template<typename T>
inline void writePod(LByteBuffer & buffer, const T & value)
{
    static_assert(std::is_trivially_copyable<T>::value, "writePod requires POD type");
    buffer.writeBytes(&value, sizeof(T));
}

template<typename T>
inline bool readPod(const std::vector<uint8_t> & data, size_t & offset, T & value)
{
    static_assert(std::is_trivially_copyable<T>::value, "readPod requires POD type");
    if (offset + sizeof(T) > data.size()) { return false; }
    std::memcpy(&value, data.data() + offset, sizeof(T));
    offset += sizeof(T);
    return true;
}

inline void writeString(LByteBuffer & buffer, const std::string & value)
{
    const uint32_t size = static_cast<uint32_t>(value.size());
    buffer.writeUInt32(size);
    if (size > 0) { buffer.writeBytes(value.data(), size); }
}

inline bool readString(const std::vector<uint8_t> & data, size_t & offset, std::string & value)
{
    uint32_t size = 0;
    if (!readPod(data, offset, size)) { return false; }
    if (offset + size > data.size()) { return false; }
    value.assign(reinterpret_cast<const char *>(data.data() + offset), size);
    offset += size;
    return true;
}
template<typename T>
inline typename std::enable_if<std::is_enum<T>::value, void>::type
writeEnum(LByteBuffer & buffer, T value)
{
    const int32_t raw = static_cast<int32_t>(value);
    writePod(buffer, raw);
}

template<typename T>
inline typename std::enable_if<std::is_enum<T>::value, bool>::type
readEnum(const std::vector<uint8_t> & data, size_t & offset, T & value)
{
    int32_t raw = 0;
    if (!readPod(data, offset, raw)) { return false; }
    value = static_cast<T>(raw);
    return true;
}
} // namespace idl_detail
} // namespace LDdsFramework

namespace Demo {
struct SensorSample
{
    SensorSample() = default;
    int32_t id;
    float temperature;
    uint64_t timestampMs;

    void serialize(LDdsFramework::LByteBuffer & buffer) const
    {
        LDdsFramework::idl_detail::writePod(buffer, id);
        LDdsFramework::idl_detail::writePod(buffer, temperature);
        LDdsFramework::idl_detail::writePod(buffer, timestampMs);
    }

    bool deserialize(const std::vector<uint8_t> & data, size_t & offset)
    {
        if (!LDdsFramework::idl_detail::readPod(data, offset, id)) { return false; }
        if (!LDdsFramework::idl_detail::readPod(data, offset, temperature)) { return false; }
        if (!LDdsFramework::idl_detail::readPod(data, offset, timestampMs)) { return false; }
        return true;
    }

    bool deserialize(const std::vector<uint8_t> & data)
    {
        size_t offset = 0;
        return deserialize(data, offset) && offset == data.size();
    }

    std::vector<uint8_t> serialize() const
    {
        LDdsFramework::LByteBuffer buffer;
        serialize(buffer);
        return std::vector<uint8_t>(buffer.data(), buffer.data() + buffer.size());
    }

    static uint32_t getTypeId() noexcept { return 1969566058U; }
    static const char * getTypeName() noexcept { return "Demo::SensorSample"; }
};
} // namespace Demo

#endif // PUBSUB_DEMO_DEFINE_H
