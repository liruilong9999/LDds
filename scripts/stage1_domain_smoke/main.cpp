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

struct DomainMsg {
    int value;
};

bool check(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "[stage1_domain] FAIL(失败): " << message << "\n";
        return false;
    }
    return true;
}

bool runQosAndXmlChecks()
{
    bool ok = true;
    std::string error;

    LQos qos;
    ok &= check(
        qos.loadFromXmlString(
            "<qos domainId=\"7\" transportType=\"udp\" historyDepth=\"4\" deadlineMs=\"120\" reliable=\"true\" "
            "enableDomainPortMapping=\"true\" basePort=\"21000\" domainPortOffset=\"8\"/>",
            &error),
        "包含 domainId/端口映射 的 XML 应成功加载"
    );
    if (!ok) {
        std::cerr << "[stage1_domain] XML错误: " << error << "\n";
        return false;
    }

    ok &= check(qos.domainId == 7, "XML读取后的 domainId 应为 7");
    ok &= check(qos.enableDomainPortMapping, "enableDomainPortMapping 应为 true");
    ok &= check(qos.basePort == 21000, "basePort 应为 21000");
    ok &= check(qos.domainPortOffset == 8, "domainPortOffset 应为 8");

    LQos publisherQos;
    LQos subscriberQos;
    publisherQos.domainId = 0;
    subscriberQos.domainId = 1;

    std::string compatibleError;
    const bool compatible = publisherQos.isCompatibleWith(subscriberQos, compatibleError);
    ok &= check(!compatible, "不同 domainId 应判定为不兼容");
    ok &= check(
        compatibleError.find("domainId") != std::string::npos,
        "不兼容原因应包含 domainId"
    );

    return ok;
}

bool runDomainPortMappingChecks()
{
    bool ok = true;

    LQos qosDomain0;
    qosDomain0.transportType = TransportType::TCP;
    qosDomain0.domainId = 0;
    qosDomain0.enableDomainPortMapping = true;
    qosDomain0.basePort = 25000;
    qosDomain0.domainPortOffset = 10;

    LQos qosDomain1 = qosDomain0;
    qosDomain1.domainId = 1;

    TransportConfig cfg0;
    cfg0.bindAddress = "127.0.0.1";
    cfg0.bindPort = 11111;

    TransportConfig cfg1;
    cfg1.bindAddress = "127.0.0.1";
    cfg1.bindPort = 11112;

    LDds ddsDomain0;
    LDds ddsDomain1;

    ok &= check(ddsDomain0.initialize(qosDomain0, cfg0), "启用映射时 domain0 应初始化成功");
    ok &= check(ddsDomain1.initialize(qosDomain1, cfg1), "启用映射时 domain1 应初始化成功");

    ddsDomain1.shutdown();
    ddsDomain0.shutdown();

    LQos qosNoMapping;
    qosNoMapping.transportType = TransportType::TCP;
    qosNoMapping.enableDomainPortMapping = false;

    TransportConfig noMapCfgA;
    noMapCfgA.bindAddress = "127.0.0.1";
    noMapCfgA.bindPort = 25150;

    TransportConfig noMapCfgB = noMapCfgA;

    LDds noMapA;
    LDds noMapB;

    ok &= check(noMapA.initialize(qosNoMapping, noMapCfgA, 0), "关闭映射时实例A应初始化成功");
    const bool secondNoMapInit = noMapB.initialize(qosNoMapping, noMapCfgB, 1);
    ok &= check(!secondNoMapInit, "关闭映射且同端口时实例B应初始化失败");

    noMapB.shutdown();
    noMapA.shutdown();

    return ok;
}

bool runCrossDomainFilteringChecks()
{
    bool ok = true;

    const uint32_t topic = 9701;
    std::atomic<int> receivedCount(0);
    std::atomic<int> lastValue(-1);
    std::mutex mutex;
    std::condition_variable cv;

    LDds receiver;
    LDds senderCrossDomain;
    LDds senderSameDomain;

    receiver.registerType<DomainMsg>("DomainMsg", topic);
    senderCrossDomain.registerType<DomainMsg>("DomainMsg", topic);
    senderSameDomain.registerType<DomainMsg>("DomainMsg", topic);

    receiver.subscribeTopic<DomainMsg>(topic, [&](const DomainMsg& msg) {
        lastValue.store(msg.value);
        receivedCount.fetch_add(1);
        cv.notify_all();
    });

    LQos receiverQos;
    receiverQos.transportType = TransportType::UDP;
    receiverQos.domainId = 0;

    LQos crossSenderQos = receiverQos;
    crossSenderQos.domainId = 1;

    LQos sameSenderQos = receiverQos;
    sameSenderQos.domainId = 0;

    TransportConfig receiverCfg;
    receiverCfg.bindAddress = "127.0.0.1";
    receiverCfg.bindPort = 25200;

    TransportConfig crossSenderCfg;
    crossSenderCfg.bindAddress = "127.0.0.1";
    crossSenderCfg.bindPort = 25201;
    crossSenderCfg.remoteAddress = "127.0.0.1";
    crossSenderCfg.remotePort = 25200;

    TransportConfig sameSenderCfg;
    sameSenderCfg.bindAddress = "127.0.0.1";
    sameSenderCfg.bindPort = 25202;
    sameSenderCfg.remoteAddress = "127.0.0.1";
    sameSenderCfg.remotePort = 25200;

    ok &= check(receiver.initialize(receiverQos, receiverCfg), "接收端初始化应成功");
    ok &= check(
        senderCrossDomain.initialize(crossSenderQos, crossSenderCfg),
        "跨域发送端初始化应成功"
    );
    ok &= check(
        senderSameDomain.initialize(sameSenderQos, sameSenderCfg),
        "同域发送端初始化应成功"
    );

    ok &= check(
        senderCrossDomain.publishTopicByTopic<DomainMsg>(topic, DomainMsg{101}),
        "跨域发布应成功"
    );

    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    ok &= check(receivedCount.load() == 0, "接收端应忽略跨域消息");

    ok &= check(
        senderSameDomain.publishTopicByTopic<DomainMsg>(topic, DomainMsg{202}),
        "同域发布应成功"
    );

    bool gotSameDomainMessage = false;
    {
        std::unique_lock<std::mutex> lock(mutex);
        gotSameDomainMessage = cv.wait_for(lock, std::chrono::milliseconds(3000), [&] {
            return receivedCount.load() >= 1;
        });
    }

    ok &= check(gotSameDomainMessage, "接收端应收到同域消息");
    ok &= check(lastValue.load() == 202, "同域消息载荷应匹配");

    senderSameDomain.shutdown();
    senderCrossDomain.shutdown();
    receiver.shutdown();

    return ok;
}

} // namespace

int main()
{
    bool ok = true;

    ok &= runQosAndXmlChecks();
    ok &= runDomainPortMappingChecks();
    ok &= runCrossDomainFilteringChecks();

    std::cout << "[stage1_domain] domain_stage=" << (ok ? "ok" : "fail")
              << " 说明=" << (ok ? "通过" : "失败") << "\n";
    return ok ? 0 : 1;
}
