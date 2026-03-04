#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

#include "LDds.h"

using namespace LDdsFramework;

namespace {

struct DiscoveryMsg
{
    int value;
};

bool check(bool condition, const std::string & message)
{
    if (!condition)
    {
        std::cerr << "[stage10_discovery] FAIL(失败): " << message << "\n";
        return false;
    }
    return true;
}

bool waitForAtLeast(
    const std::atomic<int> & counter,
    int expected,
    int timeoutMs)
{
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - start)
               .count() < timeoutMs)
    {
        if (counter.load() >= expected)
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
    return counter.load() >= expected;
}

bool runDiscoveryCase()
{
    bool ok = true;
    const uint32_t topic = 9911;

    LDds sender;
    LDds receiver;
    LDds foreignDomainReceiver;

    sender.registerType<DiscoveryMsg>("DiscoveryMsg", topic);
    receiver.registerType<DiscoveryMsg>("DiscoveryMsg", topic);
    foreignDomainReceiver.registerType<DiscoveryMsg>("DiscoveryMsg", topic);

    std::atomic<int> receiverCount(0);
    std::atomic<int> foreignCount(0);
    std::atomic<int> receiverLastValue(-1);

    receiver.subscribeTopic<DiscoveryMsg>(topic, [&](const DiscoveryMsg & msg) {
        receiverLastValue.store(msg.value);
        receiverCount.fetch_add(1);
    });
    foreignDomainReceiver.subscribeTopic<DiscoveryMsg>(topic, [&](const DiscoveryMsg &) {
        foreignCount.fetch_add(1);
    });

    LQos sameDomainQos;
    sameDomainQos.transportType = TransportType::UDP;
    sameDomainQos.domainId = 21;
    sameDomainQos.reliable = false;
    sameDomainQos.historyDepth = 4;
    sameDomainQos.deadlineMs = 0;

    LQos foreignDomainQos = sameDomainQos;
    foreignDomainQos.domainId = 22;

    TransportConfig cfg;
    cfg.bindAddress = "0.0.0.0";
    cfg.bindPort = 28110;
    cfg.reuseAddress = true;
    cfg.remoteAddress = "";
    cfg.remotePort = 0;
    cfg.enableDiscovery = true;
    cfg.discoveryIntervalMs = 300;
    cfg.peerTtlMs = 1200;
    cfg.discoveryPort = 0;
    cfg.enableBroadcast = true;
    cfg.enableMulticast = true;

    ok &= check(receiver.initialize(sameDomainQos, cfg), "接收端初始化应成功");
    ok &= check(sender.initialize(sameDomainQos, cfg), "发送端初始化应成功");
    ok &= check(
        foreignDomainReceiver.initialize(foreignDomainQos, cfg),
        "异域接收端初始化应成功");
    if (!ok)
    {
        foreignDomainReceiver.shutdown();
        sender.shutdown();
        receiver.shutdown();
        return false;
    }

    bool discovered = false;
    for (int attempt = 0; attempt < 20; ++attempt)
    {
        (void)sender.publishTopicByTopic<DiscoveryMsg>(topic, DiscoveryMsg{100 + attempt});
        if (waitForAtLeast(receiverCount, 1, 300))
        {
            discovered = true;
            break;
        }
    }

    ok &= check(discovered, "同域节点无需 remote 配置应可自动发现");
    ok &= check(receiverCount.load() >= 1, "接收端至少应收到一条消息");
    ok &= check(receiverLastValue.load() >= 100, "接收载荷应有效");
    ok &= check(foreignCount.load() == 0, "异域接收端不应收到消息");

    receiver.shutdown();
    std::this_thread::sleep_for(std::chrono::milliseconds(3200));

    const bool publishAfterPeerOffline =
        sender.publishTopicByTopic<DiscoveryMsg>(topic, DiscoveryMsg{999});
    ok &= check(!publishAfterPeerOffline, "TTL 过期后应从 peer 表移除路由");

    foreignDomainReceiver.shutdown();
    sender.shutdown();
    return ok;
}

} // namespace

int main()
{
    const bool ok = runDiscoveryCase();
    std::cout << "[stage10_discovery] result=" << (ok ? "ok" : "fail")
              << " 说明=" << (ok ? "通过" : "失败") << "\n";
    return ok ? 0 : 1;
}
