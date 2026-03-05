#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "LDds.h"

using namespace LDdsFramework;

namespace {

constexpr uint32_t TOPIC_ID = 30001U;
constexpr DomainId DOMAIN_ID = 41U;
constexpr quint16 RECEIVER_PORT = 26301;
constexpr quint16 SENDER_PORT = 26302;

LQos makeQos()
{
    LQos qos;
    qos.transportType = TransportType::UDP;
    qos.reliable = false;
    qos.historyDepth = 4;
    qos.deadlineMs = 0;
    qos.domainId = static_cast<uint8_t>(DOMAIN_ID);
    return qos;
}

TransportConfig makeReceiverConfig()
{
    TransportConfig config;
    config.bindAddress = LStringLiteral("127.0.0.1");
    config.bindPort = RECEIVER_PORT;
    config.enableDiscovery = false;
    config.enableDomainPortMapping = false;
    return config;
}

TransportConfig makeSenderConfig()
{
    TransportConfig config;
    config.bindAddress = LStringLiteral("127.0.0.1");
    config.bindPort = SENDER_PORT;
    config.remoteAddress = LStringLiteral("127.0.0.1");
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

    std::atomic<bool> received(false);
    std::mutex waitMutex;
    std::condition_variable waitCv;
    std::string receivedText;

    const LQos qos = makeQos();
    if (!receiver.initialize(qos, makeReceiverConfig(), DOMAIN_ID))
    {
        std::cerr << "[example_udp_bytes] 接收端初始化失败 error="
                  << receiver.getLastError() << "\n";
        return EXIT_FAILURE;
    }
    if (!sender.initialize(qos, makeSenderConfig(), DOMAIN_ID))
    {
        std::cerr << "[example_udp_bytes] 发送端初始化失败 error="
                  << sender.getLastError() << "\n";
        receiver.shutdown();
        return EXIT_FAILURE;
    }

    if (!receiver.registerType<std::vector<uint8_t>>("example::Bytes", TOPIC_ID) ||
        !sender.registerType<std::vector<uint8_t>>("example::Bytes", TOPIC_ID))
    {
        std::cerr << "[example_udp_bytes] 类型注册失败\n";
        sender.shutdown();
        receiver.shutdown();
        return EXIT_FAILURE;
    }

    receiver.subscribeTopic<std::vector<uint8_t>>(
        TOPIC_ID,
        [&](const std::vector<uint8_t> & payload) {
            receivedText.assign(payload.begin(), payload.end());
            received.store(true);
            waitCv.notify_all();
        });

    const std::string text = "hello from ExampleUdpBytesPubSub";
    const std::vector<uint8_t> payload(text.begin(), text.end());
    if (!sender.publishTopicByTopic<std::vector<uint8_t>>(TOPIC_ID, payload))
    {
        std::cerr << "[example_udp_bytes] 发布失败 error="
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
        std::cerr << "[example_udp_bytes] 等待订阅回调超时\n";
        return EXIT_FAILURE;
    }

    std::cout << "[example_udp_bytes] result=ok"
              << " 状态=成功"
              << " topic=" << TOPIC_ID
              << " payload=\"" << receivedText << "\"\n";
    return EXIT_SUCCESS;
}
