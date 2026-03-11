#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

#include "LDds.h"
#include "pubsub_demo_topic.h"

using namespace LDdsFramework;

int main(int argc, char * argv[])
{
    (void)argc;
    (void)argv;

    LDds subscriber;
    if (!subscriber.initialize())
    {
        std::cerr << "[sub] initialize failed error=" << subscriber.getLastError() << "\n";
        return EXIT_FAILURE;
    }

    Demo::SensorSample receivedSample{};
    bool received = false;

    for (int attempt = 0; attempt < 100; ++attempt)
    {
        LFindSet * findSet = subscriber.sub(PUBSUB_DEMO_TOPIC_KEY_SENSOR_SAMPLE_TOPIC);
        if (findSet != nullptr)
        {
            Demo::SensorSample * data = findSet->getFirstData<Demo::SensorSample>();
            if (data != nullptr)
            {
                receivedSample = *data;
                received = true;
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!received)
    {
        std::cerr << "[sub] timeout waiting message"
                  << " topicKey=" << PUBSUB_DEMO_TOPIC_KEY_SENSOR_SAMPLE_TOPIC
                  << " topicId=" << PUBSUB_DEMO_TOPIC_ID_SENSOR_SAMPLE_TOPIC
                  << " error=" << subscriber.getLastError() << "\n";
        subscriber.shutdown();
        return EXIT_FAILURE;
    }

    std::cout << "[sub] result=ok"
              << " topicKey=" << PUBSUB_DEMO_TOPIC_KEY_SENSOR_SAMPLE_TOPIC
              << " topicId=" << PUBSUB_DEMO_TOPIC_ID_SENSOR_SAMPLE_TOPIC
              << " sample=(" << receivedSample.id << ","
              << receivedSample.temperature << ","
              << receivedSample.timestampMs << ")\n";

    subscriber.shutdown();
    system("pause");
    return EXIT_SUCCESS;
}
