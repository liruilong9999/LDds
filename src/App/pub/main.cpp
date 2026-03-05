#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "LDds.h"
#include "pubsub_demo_topic.h"

using namespace LDdsFramework;

namespace {

constexpr DomainId DEMO_DOMAIN_ID = 52U;
constexpr quint16 SUBSCRIBER_PORT = 26601;
constexpr quint16 PUBLISHER_PORT = 26602;

LQos makeQos()
{
    LQos qos;
    qos.transportType = TransportType::UDP;
    qos.reliable = false;
    qos.historyDepth = 4;
    qos.domainId = static_cast<uint8_t>(DEMO_DOMAIN_ID);
    return qos;
}

TransportConfig makeTransportConfig()
{
    TransportConfig config;
    config.bindAddress = LStringLiteral("127.0.0.1");
    config.bindPort = PUBLISHER_PORT;
    config.remoteAddress = LStringLiteral("127.0.0.1");
    config.remotePort = SUBSCRIBER_PORT;
    config.enableDiscovery = false;
    config.enableDomainPortMapping = false;
    return config;
}

bool registerGeneratedSensorSampleType(LDds & dds)
{
    return dds.registerType<Demo::SensorSample>(
        Demo::SensorSample::getTypeName(),
        Demo::SensorSample::getTypeId(),
        [](const Demo::SensorSample & object, std::vector<uint8_t> & outPayload) -> bool {
            outPayload = object.serialize();
            return true;
        },
        [](const std::vector<uint8_t> & payload, Demo::SensorSample & object) -> bool {
            return object.deserialize(payload);
        });
}

uint64_t nowTimestampMs()
{
    const auto now = std::chrono::system_clock::now();
    const auto ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
    return static_cast<uint64_t>(ms.time_since_epoch().count());
}

} // namespace

int main(int argc, char * argv[])
{
    (void)argc;
    (void)argv;

    const std::string topicName = PUBSUB_DEMO_TOPIC_NAME_SENSOR_SAMPLE_TOPIC;
    uint32_t topicId = 0;
    if (!tryResolvePubsubDemoTopicId(topicName, topicId))
    {
        std::cerr << "[pub] unresolved topic name: " << topicName << "\n";
        return EXIT_FAILURE;
    }
    if (topicId != PUBSUB_DEMO_TOPIC_ID_SENSOR_SAMPLE_TOPIC)
    {
        std::cerr << "[pub] topic id mismatch for " << topicName << "\n";
        return EXIT_FAILURE;
    }

    const std::string typeName = Demo::SensorSample::getTypeName();

    LDds publisher;
    if (!publisher.initialize(makeQos(), makeTransportConfig(), DEMO_DOMAIN_ID))
    {
        std::cerr << "[pub] initialize failed error=" << publisher.getLastError() << "\n";
        return EXIT_FAILURE;
    }

    if (!registerGeneratedSensorSampleType(publisher))
    {
        std::cerr << "[pub] registerType failed topic=" << topicId
                  << " typeName=" << typeName << "\n";
        publisher.shutdown();
        return EXIT_FAILURE;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    Demo::SensorSample sample;
    sample.id = 1;
    sample.temperature = 26.5F;
    sample.timestampMs = nowTimestampMs();

    const bool published = publisher.publishTopicByTopic<Demo::SensorSample>(topicId, sample);
    if (!published)
    {
        std::cerr << "[pub] publish failed error=" << publisher.getLastError() << "\n";
        publisher.shutdown();
        return EXIT_FAILURE;
    }

    publisher.shutdown();

    std::cout << "[pub] result=ok"
              << " topicId=" << topicId
              << " typeName=" << typeName
              << " sample=(" << sample.id << "," << sample.temperature << "," << sample.timestampMs << ")\n";
    return EXIT_SUCCESS;
}
