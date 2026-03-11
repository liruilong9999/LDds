#ifndef LTYPEREGISTRY_H
#define LTYPEREGISTRY_H

/**
 * @file LTypeRegistry.h
 * @brief 类型系统注册中心（topic 与类型映射、序列化函数注册）。
 */

#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "LDdsTypes.h"
#include "LDds_Global.h"

namespace LDdsFramework {

class LTypeRegistry;

using GeneratedModuleRegisterFn = bool (*)(LTypeRegistry & registry);

LDDSCORE_EXPORT bool registerGeneratedModule(
    const char *              moduleName,
    GeneratedModuleRegisterFn registerFn);

/**
 * @class LTypeRegistry
 * @brief topic 与类型元信息注册中心（线程安全）。
 *
 * 核心职责：
 * - registerType：注册类型工厂与序列化函数
 * - createByTopic：按 topic 动态创建对象
 * - getTopicByTypeName：按类型名反查 topic
 */
class LDDSCORE_EXPORT LTypeRegistry
{
public:
    /**
     * @brief 创建对象工厂：返回指定类型实例（以 `std::shared_ptr<void>` 承载）。
     */
    using TypeFactory = std::function<std::shared_ptr<void>()>;
    /**
     * @brief 序列化函数：将对象编码为 payload。
     */
    using SerializeFn = std::function<bool(const void * object, std::vector<uint8_t> & outPayload)>;
    /**
     * @brief 反序列化函数：将 payload 解码到对象实例。
     */
    using DeserializeFn =
        std::function<bool(const std::vector<uint8_t> & payload, void * object)>;

    LTypeRegistry() = default;
    ~LTypeRegistry() = default;

    LTypeRegistry(const LTypeRegistry & other) = delete;
    LTypeRegistry & operator=(const LTypeRegistry & other) = delete;

    /**
     * @brief 注册类型与 topic 的完整元信息。
     * @param typeName 业务类型名（如 `MyPkg::Pose`）。
     * @param topic 业务 topic id。
     * @param factory 对象工厂函数。
     * @param serializer 序列化函数。
     * @param deserializer 反序列化函数。
     * @return 注册成功返回 true；若 topic 或 typeName 冲突返回 false。
     */
    bool registerType(
        const std::string & typeName,
        uint32_t            topic,
        TypeFactory         factory,
        SerializeFn         serializer,
        DeserializeFn       deserializer
    );

    bool registerTopicType(
        const std::string & topicKey,
        const std::string & typeName,
        TypeFactory         factory,
        SerializeFn         serializer,
        DeserializeFn       deserializer
    );

    /**
     * @brief 使用调用方提供的序列化/反序列化 lambda 注册类型。
     */
    template<typename T, typename Serializer, typename Deserializer>
    bool registerType(
        const std::string & typeName,
        uint32_t            topic,
        Serializer &&       serializer,
        Deserializer &&     deserializer
    )
    {
        TypeFactory factory = [] {
            return std::static_pointer_cast<void>(std::make_shared<T>());
        };

        SerializeFn serializeFn = [serializer = std::forward<Serializer>(serializer)](
                                      const void * object,
                                      std::vector<uint8_t> & outPayload
                                  ) -> bool {
            if (object == nullptr)
            {
                return false;
            }
            return serializer(*static_cast<const T *>(object), outPayload);
        };

        DeserializeFn deserializeFn =
            [deserializer = std::forward<Deserializer>(deserializer)](
                const std::vector<uint8_t> & payload,
                void *                       object
            ) -> bool {
            if (object == nullptr)
            {
                return false;
            }
            return deserializer(payload, *static_cast<T *>(object));
        };

        return registerType(
            typeName,
            topic,
            std::move(factory),
            std::move(serializeFn),
            std::move(deserializeFn)
        );
    }

    template<typename T, typename Serializer, typename Deserializer>
    bool registerTopicType(
        const std::string & topicKey,
        const std::string & typeName,
        Serializer &&       serializer,
        Deserializer &&     deserializer
    )
    {
        TypeFactory factory = [] {
            return std::static_pointer_cast<void>(std::make_shared<T>());
        };

        SerializeFn serializeFn = [serializer = std::forward<Serializer>(serializer)](
                                      const void * object,
                                      std::vector<uint8_t> & outPayload
                                  ) -> bool {
            if (object == nullptr)
            {
                return false;
            }
            return serializer(*static_cast<const T *>(object), outPayload);
        };

        DeserializeFn deserializeFn =
            [deserializer = std::forward<Deserializer>(deserializer)](
                const std::vector<uint8_t> & payload,
                void *                       object
            ) -> bool {
            if (object == nullptr)
            {
                return false;
            }
            return deserializer(payload, *static_cast<T *>(object));
        };

        return registerTopicType(
            topicKey,
            typeName,
            std::move(factory),
            std::move(serializeFn),
            std::move(deserializeFn)
        );
    }

    /**
     * @brief 使用默认规则注册类型。
     *
     * 默认规则：
     * 1. `std::string`：按字节文本写入/恢复。
     * 2. `std::vector<uint8_t>`：直接透传。
     * 3. 可平凡拷贝类型：按内存块原样拷贝。
     * 4. 其他类型：默认不支持，返回 false。
     */
    template<typename T>
    bool registerType(const std::string & typeName, uint32_t topic)
    {
        auto serializer = [](const T & object, std::vector<uint8_t> & outPayload) -> bool {
            if constexpr (std::is_same_v<T, std::string>)
            {
                outPayload.assign(object.begin(), object.end());
                return true;
            }
            else if constexpr (std::is_same_v<T, std::vector<uint8_t>>)
            {
                outPayload = object;
                return true;
            }
            else if constexpr (std::is_trivially_copyable_v<T>)
            {
                const auto * ptr  = reinterpret_cast<const uint8_t *>(&object);
                outPayload.assign(ptr, ptr + sizeof(T));
                return true;
            }
            else
            {
                return false;
            }
        };

        auto deserializer = [](const std::vector<uint8_t> & payload, T & object) -> bool {
            if constexpr (std::is_same_v<T, std::string>)
            {
                object.assign(payload.begin(), payload.end());
                return true;
            }
            else if constexpr (std::is_same_v<T, std::vector<uint8_t>>)
            {
                object = payload;
                return true;
            }
            else if constexpr (std::is_trivially_copyable_v<T>)
            {
                if (payload.size() != sizeof(T))
                {
                    return false;
                }
                std::memcpy(&object, payload.data(), sizeof(T));
                return true;
            }
            else
            {
                return false;
            }
        };

        return registerType<T>(typeName, topic, serializer, deserializer);
    }

    template<typename T>
    bool registerTopicType(const std::string & topicKey, const std::string & typeName)
    {
        auto serializer = [](const T & object, std::vector<uint8_t> & outPayload) -> bool {
            if constexpr (std::is_same_v<T, std::string>)
            {
                outPayload.assign(object.begin(), object.end());
                return true;
            }
            else if constexpr (std::is_same_v<T, std::vector<uint8_t>>)
            {
                outPayload = object;
                return true;
            }
            else if constexpr (std::is_trivially_copyable_v<T>)
            {
                const auto * ptr  = reinterpret_cast<const uint8_t *>(&object);
                outPayload.assign(ptr, ptr + sizeof(T));
                return true;
            }
            else
            {
                return false;
            }
        };

        auto deserializer = [](const std::vector<uint8_t> & payload, T & object) -> bool {
            if constexpr (std::is_same_v<T, std::string>)
            {
                object.assign(payload.begin(), payload.end());
                return true;
            }
            else if constexpr (std::is_same_v<T, std::vector<uint8_t>>)
            {
                object = payload;
                return true;
            }
            else if constexpr (std::is_trivially_copyable_v<T>)
            {
                if (payload.size() != sizeof(T))
                {
                    return false;
                }
                std::memcpy(&object, payload.data(), sizeof(T));
                return true;
            }
            else
            {
                return false;
            }
        };

        return registerTopicType<T>(topicKey, typeName, serializer, deserializer);
    }

    /**
     * @brief 按 topic 创建对象实例。
     * @return 成功返回实例；未注册返回空指针。
     */
    std::shared_ptr<void> createByTopic(uint32_t topic) const;
    /**
     * @brief 通过类型名查询 topic。
     * @return 查询失败返回 0。
     */
    uint32_t getTopicByTypeName(const std::string & typeName) const;
    uint32_t getTopicByTopicKey(const std::string & topicKey) const;
    /**
     * @brief 通过 topic 查询类型名。
     */
    std::string getTypeNameByTopic(uint32_t topic) const;
    std::string getTopicKeyByTopic(uint32_t topic) const;
    bool setTopicInfo(
        const std::string & topicKey,
        const std::string & moduleName,
        const std::string & version = "1.0");
    bool getTopicInfo(const std::string & topicKey, DdsTopicInfo & topicInfo) const;
    bool getTopicInfoByTopic(uint32_t topic, DdsTopicInfo & topicInfo) const;
    std::vector<DdsTopicInfo> listTopicInfos() const;
    std::vector<uint32_t> getRegisteredTopics() const;
    bool applyGeneratedModules(std::vector<std::string> * appliedModules = nullptr);
    static uint32_t makeTopicId(const std::string & topicKey) noexcept;

    /**
     * @brief 按 topic 执行序列化。
     */
    bool serializeByTopic(uint32_t topic, const void * object, std::vector<uint8_t> & outPayload)
        const;
    /**
     * @brief 按 topic 执行反序列化。
     */
    bool deserializeByTopic(uint32_t topic, const std::vector<uint8_t> & payload, void * object)
        const;
    /**
     * @brief 判断指定 topic 是否已注册。
     */
    bool hasTopic(uint32_t topic) const;

private:
    /**
     * @brief 单个类型注册项。
     */
    struct TypeEntry
    {
        /**
         * @brief 类型全名。
         */
        std::string typeName;
        std::string topicKey;
        std::string moduleName;
        std::string version;
        /**
         * @brief topic id。
         */
        uint32_t    topic = 0;
        /**
         * @brief 对象工厂。
         */
        TypeFactory factory;
        /**
         * @brief 序列化函数。
         */
        SerializeFn serializer;
        /**
         * @brief 反序列化函数。
         */
        DeserializeFn deserializer;
    };

    /**
     * @brief topic -> 类型条目映射。
     */
    std::unordered_map<uint32_t, std::shared_ptr<TypeEntry>> m_entriesByTopic;
    /**
     * @brief typeName -> topic 反向索引。
     */
    std::unordered_map<std::string, uint32_t>                m_topicByTypeName;
    std::unordered_map<std::string, uint32_t>                m_topicByKey;
    std::unordered_set<std::string>                          m_appliedGeneratedModules;
    /**
     * @brief 读写锁（读多写少场景）。
     */
    mutable std::shared_mutex                                m_mutex;
};

} // namespace LDdsFramework

#endif // LTYPEREGISTRY_H
