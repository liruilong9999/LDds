#ifndef FILE1_DEFINE_H
#define FILE1_DEFINE_H

#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

#include "file1_export.h"
#include "LByteBuffer.h"

#include "LTypeRegistry.h"

#ifndef LDDSFRAMEWORK_IDL_DETAIL_HELPERS_H
#define LDDSFRAMEWORK_IDL_DETAIL_HELPERS_H

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

#endif // LDDSFRAMEWORK_IDL_DETAIL_HELPERS_H

namespace P1 {
namespace P2 {
struct Handle
{
    Handle() = default;
    int32_t handle;
    int64_t datatime;

    void serialize(LDdsFramework::LByteBuffer & buffer) const
    {
        LDdsFramework::idl_detail::writePod(buffer, handle);
        LDdsFramework::idl_detail::writePod(buffer, datatime);
    }

    bool deserialize(const std::vector<uint8_t> & data, size_t & offset)
    {
        if (!LDdsFramework::idl_detail::readPod(data, offset, handle)) { return false; }
        if (!LDdsFramework::idl_detail::readPod(data, offset, datatime)) { return false; }
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

    Handle * get() noexcept { return this; }
    const Handle * get() const noexcept { return this; }
    static uint32_t getTypeId() noexcept { return LDdsFramework::LTypeRegistry::makeTopicId(getTopicKey()); }
    static const char * getTypeName() noexcept { return "P1::P2::Handle"; }
    static const char * getTopicKey() noexcept { return "file1::HANDLE_TOPIC"; }
};
} // namespace P2
} // namespace P1

namespace P1 {
struct Param1 : public P1::P2::Handle
{
    Param1() : P1::P2::Handle() {}
    std::string str1;
    double data1;
    int32_t data2;
    std::vector<int32_t> data1Vec;

    void serialize(LDdsFramework::LByteBuffer & buffer) const
    {
        P1::P2::Handle::serialize(buffer);
        LDdsFramework::idl_detail::writeString(buffer, str1);
        LDdsFramework::idl_detail::writePod(buffer, data1);
        LDdsFramework::idl_detail::writePod(buffer, data2);
        buffer.writeUInt32(static_cast<uint32_t>(data1Vec.size()));
        for (const auto & item : data1Vec)
        {
            LDdsFramework::idl_detail::writePod(buffer, item);
        }
    }

    bool deserialize(const std::vector<uint8_t> & data, size_t & offset)
    {
        if (!P1::P2::Handle::deserialize(data, offset)) { return false; }
        if (!LDdsFramework::idl_detail::readString(data, offset, str1)) { return false; }
        if (!LDdsFramework::idl_detail::readPod(data, offset, data1)) { return false; }
        if (!LDdsFramework::idl_detail::readPod(data, offset, data2)) { return false; }
        uint32_t data1VecSize = 0;
        if (!LDdsFramework::idl_detail::readPod(data, offset, data1VecSize)) { return false; }
        data1Vec.clear();
        data1Vec.reserve(data1VecSize);
        for (uint32_t i = 0; i < data1VecSize; ++i)
        {
            int32_t item{};
            if (!LDdsFramework::idl_detail::readPod(data, offset, item)) { return false; }
            data1Vec.push_back(std::move(item));
        }
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

    Param1 * get() noexcept { return this; }
    const Param1 * get() const noexcept { return this; }
    static uint32_t getTypeId() noexcept { return LDdsFramework::LTypeRegistry::makeTopicId(getTopicKey()); }
    static const char * getTypeName() noexcept { return "P1::Param1"; }
    static const char * getTopicKey() noexcept { return "file1::PARAM1_TOPIC"; }
};
} // namespace P1

#endif // FILE1_DEFINE_H
