#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

#include "LDds.h"
#include "pubsub_demo_topic.h"

using namespace LDdsFramework;

namespace {

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

    LDds publisher;
    if (!publisher.initialize())
    {
        std::cerr << "[pub] initialize failed error=" << publisher.getLastError() << "\n";
        return EXIT_FAILURE;
    }

    Demo::SensorSample sample;
    sample.id = 1;
    sample.temperature = 26.5F;
    sample.timestampMs = nowTimestampMs();

    bool published = false;
    for (int attempt = 0; attempt < 20; ++attempt)
    {
        published = publisher.publish(PUBSUB_DEMO_TOPIC_KEY_SENSOR_SAMPLE_TOPIC, sample.get());
        if (published)
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    if (!published)
    {
        std::cerr << "[pub] publish failed error=" << publisher.getLastError() << "\n";
        publisher.shutdown();
        return EXIT_FAILURE;
    }

    std::cout << "[pub] result=ok"
              << " topicKey=" << PUBSUB_DEMO_TOPIC_KEY_SENSOR_SAMPLE_TOPIC
              << " topicId=" << PUBSUB_DEMO_TOPIC_ID_SENSOR_SAMPLE_TOPIC
              << " sample=(" << sample.id << "," << sample.temperature << "," << sample.timestampMs << ")\n";

    publisher.shutdown();
    system("pause");
    return EXIT_SUCCESS;
}
