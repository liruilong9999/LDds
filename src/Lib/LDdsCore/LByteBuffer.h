#ifndef LBYTEBUFFER_H
#define LBYTEBUFFER_H

/**
 * @file LByteBuffer.h
 * @brief 二进制字节缓冲区读写工具。
 *
 * 特点：
 * 1. 面向网络协议编解码，统一采用小端序；
 * 2. 提供顺序写入与顺序读取接口；
 * 3. 内部自动扩容并维护读写游标。
 */

#include <cstdint>
#include <vector>
#include <stdexcept>
#include <cstring>
#include "LDds_Global.h"

namespace LDdsFramework {

/**
 * @brief 字节缓冲区类
 *
 * 提供字节数据的读写操作，自动管理内存
 * 使用小端序进行数据传输
 */
class LDDSCORE_EXPORT LByteBuffer
{
public:
    /**
     * @brief 构造函数
     * @param initialCapacity 初始容量（字节）
     */
    explicit LByteBuffer(size_t initialCapacity = 1024);

    /**
     * @brief 析构函数
     */
    ~LByteBuffer() = default;

    /**
     * @brief 写入无符号32位整数（小端序）
     * @param value 要写入的值
     */
    void writeUInt32(uint32_t value);

    /**
     * @brief 写入无符号64位整数（小端序）
     * @param value 要写入的值
     */
    void writeUInt64(uint64_t value);

    /**
     * @brief 写入字节数组
     * @param data 数据指针
     * @param size 数据大小
     */
    void writeBytes(const void* data, size_t size);

    /**
     * @brief 读取无符号32位整数（小端序）
     * @return 读取的值
     * @throws std::out_of_range 如果数据不足
     */
    uint32_t readUInt32();

    /**
     * @brief 读取无符号64位整数（小端序）
     * @return 读取的值
     * @throws std::out_of_range 如果数据不足
     */
    uint64_t readUInt64();

    /**
     * @brief 读取字节数组
     * @param data 目标缓冲区
     * @param size 要读取的大小
     * @throws std::out_of_range 如果数据不足
     */
    void readBytes(void* data, size_t size);

    /**
     * @brief 清空缓冲区
     */
    void clear();

    /**
     * @brief 获取当前数据大小
     * @return 数据大小（字节）
     */
    size_t size() const noexcept;

    /**
     * @brief 获取数据指针
     * @return 指向数据的常量指针
     */
    const uint8_t* data() const noexcept;

    /**
     * @brief 获取数据指针（可变）
     * @return 指向数据的可变指针
     */
    uint8_t* data() noexcept;

    /**
     * @brief 设置读取位置
     * @param pos 新的读取位置
     */
    void setReadPos(size_t pos);

    /**
     * @brief 获取读取位置
     * @return 当前读取位置
     */
    size_t getReadPos() const noexcept;

private:
    std::vector<uint8_t> m_buffer;  ///< 数据缓冲区
    size_t m_writePos;               ///< 写入位置
    size_t m_readPos;                ///< 读取位置

    /**
     * @brief 确保有足够的容量
     * @param requiredSize 需要的容量
     */
    void ensureCapacity(size_t requiredSize);
};

// Stage-2 compatibility name.
class LDDSCORE_EXPORT ByteBuffer : public LByteBuffer
{
public:
    using LByteBuffer::LByteBuffer;
};

} // namespace LDdsFramework

#endif // LBYTEBUFFER_H
