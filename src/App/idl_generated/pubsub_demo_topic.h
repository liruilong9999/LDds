#ifndef PUBSUB_DEMO_TOPIC_H
#define PUBSUB_DEMO_TOPIC_H

#include <cstdint>
#include <string>

#include "pubsub_demo_export.h"
#include "pubsub_demo_define.h"
#include "LTypeRegistry.h"

namespace LDdsFramework {
enum class PubsubDemoTopicId : uint32_t
{
    Invalid = 0,
    SENSOR_SAMPLE_TOPIC = 1969566058
};

#define PUBSUB_DEMO_TOPIC_NAME_SENSOR_SAMPLE_TOPIC "SENSOR_SAMPLE_TOPIC"
#define PUBSUB_DEMO_TOPIC_ID_SENSOR_SAMPLE_TOPIC static_cast<uint32_t>(LDdsFramework::PubsubDemoTopicId::SENSOR_SAMPLE_TOPIC)

PUBSUB_DEMO_IDL_API void registerPubsubDemoTypes(LTypeRegistry & registry);
inline bool tryResolvePubsubDemoTopicId(const std::string & topicName, uint32_t & topicId)
{
    if (topicName == "SENSOR_SAMPLE_TOPIC")
    {
        topicId = static_cast<uint32_t>(PubsubDemoTopicId::SENSOR_SAMPLE_TOPIC);
        return true;
    }
    topicId = 0;
    return false;
}

inline bool tryResolvePubsubDemoTopicName(uint32_t topicId, const char * & topicName)
{
    switch (static_cast<PubsubDemoTopicId>(topicId))
    {
    case PubsubDemoTopicId::SENSOR_SAMPLE_TOPIC:
        topicName = "SENSOR_SAMPLE_TOPIC";
        return true;
    default:
        topicName = nullptr;
        return false;
    }
}
} // namespace LDdsFramework

#endif // PUBSUB_DEMO_TOPIC_H
