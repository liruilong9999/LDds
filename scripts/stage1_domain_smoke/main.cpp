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
        std::cerr << "[stage1_domain] FAIL: " << message << "\n";
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
        "xml with domainId/port mapping should load"
    );
    if (!ok) {
        std::cerr << "[stage1_domain] xml error: " << error << "\n";
        return false;
    }

    ok &= check(qos.domainId == 7, "domainId should be 7 from xml");
    ok &= check(qos.enableDomainPortMapping, "enableDomainPortMapping should be true");
    ok &= check(qos.basePort == 21000, "basePort should be 21000");
    ok &= check(qos.domainPortOffset == 8, "domainPortOffset should be 8");

    LQos publisherQos;
    LQos subscriberQos;
    publisherQos.domainId = 0;
    subscriberQos.domainId = 1;

    std::string compatibleError;
    const bool compatible = publisherQos.isCompatibleWith(subscriberQos, compatibleError);
    ok &= check(!compatible, "different domainId should be incompatible");
    ok &= check(
        compatibleError.find("domainId") != std::string::npos,
        "incompatible reason should mention domainId"
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

    ok &= check(ddsDomain0.initialize(qosDomain0, cfg0), "domain0 with mapping should initialize");
    ok &= check(ddsDomain1.initialize(qosDomain1, cfg1), "domain1 with mapping should initialize");

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

    ok &= check(noMapA.initialize(qosNoMapping, noMapCfgA, 0), "no-mapping instance A should initialize");
    const bool secondNoMapInit = noMapB.initialize(qosNoMapping, noMapCfgB, 1);
    ok &= check(!secondNoMapInit, "no-mapping instance B same bind port should fail");

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

    ok &= check(receiver.initialize(receiverQos, receiverCfg), "receiver initialize should succeed");
    ok &= check(
        senderCrossDomain.initialize(crossSenderQos, crossSenderCfg),
        "cross-domain sender initialize should succeed"
    );
    ok &= check(
        senderSameDomain.initialize(sameSenderQos, sameSenderCfg),
        "same-domain sender initialize should succeed"
    );

    ok &= check(
        senderCrossDomain.publishTopicByTopic<DomainMsg>(topic, DomainMsg{101}),
        "cross-domain publish should succeed"
    );

    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    ok &= check(receivedCount.load() == 0, "receiver should ignore cross-domain message");

    ok &= check(
        senderSameDomain.publishTopicByTopic<DomainMsg>(topic, DomainMsg{202}),
        "same-domain publish should succeed"
    );

    bool gotSameDomainMessage = false;
    {
        std::unique_lock<std::mutex> lock(mutex);
        gotSameDomainMessage = cv.wait_for(lock, std::chrono::milliseconds(3000), [&] {
            return receivedCount.load() >= 1;
        });
    }

    ok &= check(gotSameDomainMessage, "receiver should get same-domain message");
    ok &= check(lastValue.load() == 202, "same-domain message payload should match");

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

    std::cout << "[stage1_domain] domain_stage=" << (ok ? "ok" : "fail") << "\n";
    return ok ? 0 : 1;
}
