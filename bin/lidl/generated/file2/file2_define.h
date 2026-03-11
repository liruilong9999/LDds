#ifndef FILE2_DEFINE_H
#define FILE2_DEFINE_H

#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

#include "file2_export.h"
#include "LByteBuffer.h"

#include "LTypeRegistry.h"

#include "file1_define.h"

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

namespace P3 {
struct TestParam : public P1::P2::Handle
{
    TestParam() : P1::P2::Handle() {}
    int32_t a;

    void serialize(LDdsFramework::LByteBuffer & buffer) const
    {
        P1::P2::Handle::serialize(buffer);
        LDdsFramework::idl_detail::writePod(buffer, a);
    }

    bool deserialize(const std::vector<uint8_t> & data, size_t & offset)
    {
        if (!P1::P2::Handle::deserialize(data, offset)) { return false; }
        if (!LDdsFramework::idl_detail::readPod(data, offset, a)) { return false; }
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

    TestParam * get() noexcept { return this; }
    const TestParam * get() const noexcept { return this; }
    static uint32_t getTypeId() noexcept { return LDdsFramework::LTypeRegistry::makeTopicId(getTopicKey()); }
    static const char * getTypeName() noexcept { return "P3::TestParam"; }
    static const char * getTopicKey() noexcept { return "file2::TESTPARAM_TOPIC"; }
};
} // namespace P3

#endif // FILE2_DEFINE_H
