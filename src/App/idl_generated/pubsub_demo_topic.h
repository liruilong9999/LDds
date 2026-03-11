#ifndef PUBSUB_DEMO_TOPIC_H
#define PUBSUB_DEMO_TOPIC_H

#include <cstdint>
#include <string>

#include "pubsub_demo_export.h"
#include "pubsub_demo_define.h"
#include "LTypeRegistry.h"

namespace LDdsFramework {

#define PUBSUB_DEMO_TOPIC_NAME_SENSOR_SAMPLE_TOPIC "SENSOR_SAMPLE_TOPIC"
#define PUBSUB_DEMO_TOPIC_KEY_SENSOR_SAMPLE_TOPIC "pubsub_demo::SENSOR_SAMPLE_TOPIC"
#define PUBSUB_DEMO_TOPIC_ID_SENSOR_SAMPLE_TOPIC LDdsFramework::LTypeRegistry::makeTopicId(PUBSUB_DEMO_TOPIC_KEY_SENSOR_SAMPLE_TOPIC)

PUBSUB_DEMO_IDL_API bool registerPubsubDemoTypes(LTypeRegistry & registry);
extern "C" PUBSUB_DEMO_IDL_API bool registerPubsubDemoIdlModule(LTypeRegistry & registry);

inline bool tryResolvePubsubDemoTopicId(const std::string & topicKey, uint32_t & topicId)
{
    if (topicKey == PUBSUB_DEMO_TOPIC_KEY_SENSOR_SAMPLE_TOPIC ||
        topicKey == PUBSUB_DEMO_TOPIC_NAME_SENSOR_SAMPLE_TOPIC)
    {
        topicId = PUBSUB_DEMO_TOPIC_ID_SENSOR_SAMPLE_TOPIC;
        return true;
    }
    topicId = 0;
    return false;
}

inline bool tryResolvePubsubDemoTopicKey(uint32_t topicId, const char * & topicKey)
{
    if (topicId == PUBSUB_DEMO_TOPIC_ID_SENSOR_SAMPLE_TOPIC)
    {
        topicKey = PUBSUB_DEMO_TOPIC_KEY_SENSOR_SAMPLE_TOPIC;
        return true;
    }
    topicKey = nullptr;
    return false;
}

} // namespace LDdsFramework

#endif // PUBSUB_DEMO_TOPIC_H
