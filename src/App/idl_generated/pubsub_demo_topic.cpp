#include "pubsub_demo_topic.h"

namespace LDdsFramework {
void registerPubsubDemoTypes(LTypeRegistry & registry)
{
    registry.registerType<Demo::SensorSample>(
        "Demo::SensorSample",
        static_cast<uint32_t>(PubsubDemoTopicId::SENSOR_SAMPLE_TOPIC),
        [](const Demo::SensorSample & object, std::vector<uint8_t> & outPayload) -> bool {
            outPayload = object.serialize();
            return true;
        },
        [](const std::vector<uint8_t> & payload, Demo::SensorSample & object) -> bool {
            return object.deserialize(payload);
        }
    );
}
} // namespace LDdsFramework
