#include "pubsub_demo_topic.h"

namespace LDdsFramework {

bool registerPubsubDemoTypes(LTypeRegistry & registry)
{
    return registry.registerTopicType<Demo::SensorSample>(
        PUBSUB_DEMO_TOPIC_KEY_SENSOR_SAMPLE_TOPIC,
        "Demo::SensorSample",
        [](const Demo::SensorSample & object, std::vector<uint8_t> & outPayload) -> bool {
            outPayload = object.serialize();
            return true;
        },
        [](const std::vector<uint8_t> & payload, Demo::SensorSample & object) -> bool {
            return object.deserialize(payload);
        }
    );
}

extern "C" PUBSUB_DEMO_IDL_API bool registerPubsubDemoIdlModule(LTypeRegistry & registry)
{
    return registerPubsubDemoTypes(registry);
}

} // namespace LDdsFramework

namespace {

struct PubsubDemoAutoModuleRegistrar
{
    PubsubDemoAutoModuleRegistrar()
    {
        LDdsFramework::registerGeneratedModule(
            "pubsub_demo",
            &LDdsFramework::registerPubsubDemoIdlModule);
    }
};

static PubsubDemoAutoModuleRegistrar g_pubsubDemoAutoModuleRegistrar;

} // namespace
