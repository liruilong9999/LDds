#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

#include "LDds.h"
#include "pubsub_demo_topic.h"

using namespace LDdsFramework;

namespace {

constexpr DomainId DEMO_DOMAIN_ID = 52U;
constexpr quint16 SUBSCRIBER_PORT = 26601;

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
    config.bindPort = SUBSCRIBER_PORT;
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

} // namespace

int main(int argc, char * argv[])
{
    (void)argc;
    (void)argv;

    const std::string topicName = PUBSUB_DEMO_TOPIC_NAME_SENSOR_SAMPLE_TOPIC;
    uint32_t topicId = 0;
    if (!tryResolvePubsubDemoTopicId(topicName, topicId))
    {
        std::cerr << "[sub] unresolved topic name: " << topicName << "\n";
        return EXIT_FAILURE;
    }
    if (topicId != PUBSUB_DEMO_TOPIC_ID_SENSOR_SAMPLE_TOPIC)
    {
        std::cerr << "[sub] topic id mismatch for " << topicName << "\n";
        return EXIT_FAILURE;
    }

    const std::string typeName = Demo::SensorSample::getTypeName();

    LDds subscriber;
    if (!subscriber.initialize(makeQos(), makeTransportConfig(), DEMO_DOMAIN_ID))
    {
        std::cerr << "[sub] initialize failed error=" << subscriber.getLastError() << "\n";
        return EXIT_FAILURE;
    }

    if (!registerGeneratedSensorSampleType(subscriber))
    {
        std::cerr << "[sub] registerType failed topic=" << topicId
                  << " typeName=" << typeName << "\n";
        subscriber.shutdown();
        return EXIT_FAILURE;
    }

    std::atomic<bool> gotMessage(false);
    std::mutex waitMutex;
    std::condition_variable waitCv;
    Demo::SensorSample receivedSample{};

    subscriber.subscribeTopic<Demo::SensorSample>(
        topicId,
        [&](const Demo::SensorSample & sample) {
            receivedSample = sample;
            gotMessage.store(true);
            waitCv.notify_all();
        });

    bool received = false;
    {
        std::unique_lock<std::mutex> lock(waitMutex);
        received = waitCv.wait_for(
            lock,
            std::chrono::seconds(10),
            [&]() { return gotMessage.load(); });
    }

    subscriber.shutdown();

    if (!received)
    {
        std::cerr << "[sub] timeout waiting message"
                  << " topicId=" << topicId
                  << " typeName=" << typeName << "\n";
        return EXIT_FAILURE;
    }

    std::cout << "[sub] result=ok"
              << " topicId=" << topicId
              << " typeName=" << typeName
              << " sample=(" << receivedSample.id << ","
              << receivedSample.temperature << ","
              << receivedSample.timestampMs << ")\n";
    return EXIT_SUCCESS;
}
