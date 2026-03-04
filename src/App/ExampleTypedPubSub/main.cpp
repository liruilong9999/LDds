#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <type_traits>

#include "LDds.h"

using namespace LDdsFramework;

namespace {

constexpr uint32_t TOPIC_ID = 30002U;
constexpr DomainId DOMAIN_ID = 42U;
constexpr quint16 RECEIVER_PORT = 26311;
constexpr quint16 SENDER_PORT = 26312;

struct Pose
{
    float x;
    float y;
    float yaw;
    uint64_t timestampMs;
};

static_assert(std::is_trivially_copyable<Pose>::value, "Pose must be trivially copyable");

LQos makeQos()
{
    LQos qos;
    qos.transportType = TransportType::UDP;
    qos.reliable = false;
    qos.historyDepth = 8;
    qos.domainId = static_cast<uint8_t>(DOMAIN_ID);
    return qos;
}

TransportConfig makeReceiverConfig()
{
    TransportConfig config;
    config.bindAddress = QStringLiteral("127.0.0.1");
    config.bindPort = RECEIVER_PORT;
    config.enableDiscovery = false;
    config.enableDomainPortMapping = false;
    return config;
}

TransportConfig makeSenderConfig()
{
    TransportConfig config;
    config.bindAddress = QStringLiteral("127.0.0.1");
    config.bindPort = SENDER_PORT;
    config.remoteAddress = QStringLiteral("127.0.0.1");
    config.remotePort = RECEIVER_PORT;
    config.enableDiscovery = false;
    config.enableDomainPortMapping = false;
    return config;
}

} // namespace

int main()
{
    LDds receiver;
    LDds sender;

    const std::string typeName = "example::Pose";
    std::atomic<bool> received(false);
    std::mutex waitMutex;
    std::condition_variable waitCv;
    Pose receivedPose{0.0F, 0.0F, 0.0F, 0U};

    const LQos qos = makeQos();
    if (!receiver.initialize(qos, makeReceiverConfig(), DOMAIN_ID))
    {
        std::cerr << "[example_typed_pubsub] 接收端初始化失败 error="
                  << receiver.getLastError() << "\n";
        return EXIT_FAILURE;
    }
    if (!sender.initialize(qos, makeSenderConfig(), DOMAIN_ID))
    {
        std::cerr << "[example_typed_pubsub] 发送端初始化失败 error="
                  << sender.getLastError() << "\n";
        receiver.shutdown();
        return EXIT_FAILURE;
    }

    if (!receiver.registerType<Pose>(typeName, TOPIC_ID) ||
        !sender.registerType<Pose>(typeName, TOPIC_ID))
    {
        std::cerr << "[example_typed_pubsub] 类型注册失败\n";
        sender.shutdown();
        receiver.shutdown();
        return EXIT_FAILURE;
    }

    receiver.subscribeTopic<Pose>(
        TOPIC_ID,
        [&](const Pose & pose) {
            receivedPose = pose;
            received.store(true);
            waitCv.notify_all();
        });

    const Pose sentPose{1.25F, -2.50F, 0.75F, 12345678U};
    if (!sender.publishTopicByTopic<Pose>(TOPIC_ID, sentPose))
    {
        std::cerr << "[example_typed_pubsub] 发布失败 error="
                  << sender.getLastError() << "\n";
        sender.shutdown();
        receiver.shutdown();
        return EXIT_FAILURE;
    }

    bool callbackArrived = false;
    {
        std::unique_lock<std::mutex> lock(waitMutex);
        callbackArrived = waitCv.wait_for(
            lock,
            std::chrono::seconds(3),
            [&] { return received.load(); });
    }

    sender.shutdown();
    receiver.shutdown();

    if (!callbackArrived)
    {
        std::cerr << "[example_typed_pubsub] 等待订阅回调超时\n";
        return EXIT_FAILURE;
    }

    std::cout << "[example_typed_pubsub] result=ok"
              << " 状态=成功"
              << " pose=(" << receivedPose.x << ","
              << receivedPose.y << ","
              << receivedPose.yaw << ","
              << receivedPose.timestampMs << ")\n";
    return EXIT_SUCCESS;
}
