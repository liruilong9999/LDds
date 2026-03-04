#ifndef LTYPEREGISTRY_H
#define LTYPEREGISTRY_H

#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "LDds_Global.h"

namespace LDdsFramework {

class LDDSCORE_EXPORT LTypeRegistry
{
public:
    using TypeFactory = std::function<std::shared_ptr<void>()>;
    using SerializeFn = std::function<bool(const void * object, std::vector<uint8_t> & outPayload)>;
    using DeserializeFn =
        std::function<bool(const std::vector<uint8_t> & payload, void * object)>;

    LTypeRegistry() = default;
    ~LTypeRegistry() = default;

    LTypeRegistry(const LTypeRegistry & other) = delete;
    LTypeRegistry & operator=(const LTypeRegistry & other) = delete;

    bool registerType(
        const std::string & typeName,
        uint32_t            topic,
        TypeFactory         factory,
        SerializeFn         serializer,
        DeserializeFn       deserializer
    );

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

    std::shared_ptr<void> createByTopic(uint32_t topic) const;
    uint32_t getTopicByTypeName(const std::string & typeName) const;

    bool serializeByTopic(uint32_t topic, const void * object, std::vector<uint8_t> & outPayload)
        const;
    bool deserializeByTopic(uint32_t topic, const std::vector<uint8_t> & payload, void * object)
        const;
    bool hasTopic(uint32_t topic) const;

private:
    struct TypeEntry
    {
        std::string typeName;
        uint32_t    topic = 0;
        TypeFactory factory;
        SerializeFn serializer;
        DeserializeFn deserializer;
    };

    std::unordered_map<uint32_t, std::shared_ptr<TypeEntry>> m_entriesByTopic;
    std::unordered_map<std::string, uint32_t>                m_topicByTypeName;
    mutable std::shared_mutex                                m_mutex;
};

} // namespace LDdsFramework

#endif // LTYPEREGISTRY_H
