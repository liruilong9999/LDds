#ifndef LMESSAGE_H
#define LMESSAGE_H

#include <cstdint>
#include <vector>
#include <string>
#include <QHostAddress>
#include "LDds_Global.h"
#include "LByteBuffer.h"

namespace LDdsFramework {

/**
 * @brief 消息头部结构
 *
 * 固定大小的消息头部，包含消息元数据
 * 所有多字节字段使用小端序
 *
 * 格式:
 * uint32_t totalSize    // 总大小（头部 + payload）
 * uint32_t topic        // 主题ID
 * uint64_t sequence     // 序列号
 * uint32_t payloadSize  // payload大小
 */
#pragma pack(push, 1)
struct LMessageHeader {
    uint32_t totalSize;    ///< 消息总大小（字节）
    uint32_t topic;        ///< 主题ID
    uint64_t sequence;     ///< 序列号
    uint32_t payloadSize;  ///< payload大小

    /**
     * @brief 头部大小（字节）
     */
    static constexpr size_t HEADER_SIZE = sizeof(uint32_t) + sizeof(uint32_t) +
                                          sizeof(uint64_t) + sizeof(uint32_t);

    /**
     * @brief 序列化到缓冲区
     * @param buffer 目标缓冲区
     */
    void serialize(LByteBuffer& buffer) const;

    /**
     * @brief 从缓冲区反序列化
     * @param buffer 源缓冲区
     * @return true 成功
     * @return false 数据不足
     */
    bool deserialize(const uint8_t* data, size_t size);
};
#pragma pack(pop)

/**
 * @brief 统一消息类
 *
 * 封装DDS消息，提供序列化和反序列化功能
 * 消息格式：头部 + payload
 */
class LDDSCORE_EXPORT LMessage
{
public:
    /**
     * @brief 默认构造函数
     */
    LMessage();

    /**
     * @brief 带参数的构造函数
     * @param topic 主题ID
     * @param sequence 序列号
     * @param payload 负载数据
     */
    LMessage(uint32_t topic, uint64_t sequence, const std::vector<uint8_t>& payload);

    /**
     * @brief 析构函数
     */
    ~LMessage() = default;

    /**
     * @brief 获取主题ID
     * @return 主题ID
     */
    uint32_t getTopic() const noexcept;

    /**
     * @brief 设置主题ID
     * @param topic 主题ID
     */
    void setTopic(uint32_t topic) noexcept;

    /**
     * @brief 获取序列号
     * @return 序列号
     */
    uint64_t getSequence() const noexcept;

    /**
     * @brief 设置序列号
     * @param sequence 序列号
     */
    void setSequence(uint64_t sequence) noexcept;

    /**
     * @brief 获取payload
     * @return payload数据的常量引用
     */
    const std::vector<uint8_t>& getPayload() const noexcept;

    /**
     * @brief 获取payload（可变）
     * @return payload数据的引用
     */
    std::vector<uint8_t>& getPayload() noexcept;

    /**
     * @brief 设置payload
     * @param payload payload数据
     */
    void setPayload(const std::vector<uint8_t>& payload);

    /**
     * @brief 设置payload
     * @param data 数据指针
     * @param size 数据大小
     */
    void setPayload(const void* data, size_t size);

    /**
     * @brief 获取消息总大小
     * @return 总大小（字节）
     */
    size_t getTotalSize() const noexcept;

    /**
     * @brief 获取payload大小
     * @return payload大小（字节）
     */
    size_t getPayloadSize() const noexcept;

    /**
     * @brief 序列化消息
     * @return 包含完整消息的缓冲区
     */
    LByteBuffer serialize() const;

    /**
     * @brief 从缓冲区反序列化消息
     * @param data 数据指针
     * @param size 数据大小
     * @return true 成功
     * @return false 失败（格式错误）
     */
    bool deserialize(const uint8_t* data, size_t size);

    /**
     * @brief 从缓冲区反序列化消息（使用LByteBuffer）
     * @param buffer 字节缓冲区
     * @return true 成功
     * @return false 失败
     */
    bool deserialize(const LByteBuffer& buffer);

    /**
     * @brief 清空消息内容
     */
    void clear() noexcept;

    /**
     * @brief 获取发送方地址（接收时设置）
     * @return 发送方地址
     */
    QHostAddress getSenderAddress() const noexcept;

    /**
     * @brief 设置发送方地址
     * @param address 地址
     */
    void setSenderAddress(const QHostAddress& address) noexcept;

    /**
     * @brief 获取发送方端口（接收时设置）
     * @return 发送方端口
     */
    quint16 getSenderPort() const noexcept;

    /**
     * @brief 设置发送方端口
     * @param port 端口
     */
    void setSenderPort(quint16 port) noexcept;

    /**
     * @brief 检查消息是否有效
     * @return true 有效
     * @return false 无效
     */
    bool isValid() const noexcept;

private:
    uint32_t m_topic;                ///< 主题ID
    uint64_t m_sequence;             ///< 序列号
    std::vector<uint8_t> m_payload;  ///< payload数据
    QHostAddress m_senderAddress;    ///< 发送方地址
    quint16 m_senderPort;            ///< 发送方端口
};

} // namespace LDdsFramework

#endif // LMESSAGE_H
