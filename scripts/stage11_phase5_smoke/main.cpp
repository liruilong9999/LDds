#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "LDds.h"

using namespace LDdsFramework;

namespace {

struct Phase5Msg
{
    int value;
};

bool check(bool condition, const std::string & message)
{
    if (!condition)
    {
        std::cerr << "[stage11_phase5] FAIL(失败): " << message << "\n";
        return false;
    }
    return true;
}

bool waitForAtLeast(const std::atomic<int> & counter, int expected, int timeoutMs)
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

bool runDurabilityPersistentCase()
{
    bool ok = true;
    const uint32_t topic = 11001;
    const std::filesystem::path dbPath = "build/stage11_phase5_smoke/persistent_case.sqlite";
    std::error_code ec;
    std::filesystem::remove(dbPath, ec);
    std::filesystem::create_directories(dbPath.parent_path(), ec);

    LQos receiverQos;
    receiverQos.transportType = TransportType::UDP;
    receiverQos.domainId = 41;
    receiverQos.historyDepth = 2;
    receiverQos.durabilityKind = DurabilityKind::Persistent;
    receiverQos.durabilityDbPath = dbPath.string();

    LQos senderQos = receiverQos;
    senderQos.durabilityKind = DurabilityKind::Volatile;

    TransportConfig receiverCfg;
    receiverCfg.bindAddress = "127.0.0.1";
    receiverCfg.bindPort = 29410;
    receiverCfg.enableDiscovery = false;

    TransportConfig senderCfg;
    senderCfg.bindAddress = "127.0.0.1";
    senderCfg.bindPort = 29411;
    senderCfg.remoteAddress = "127.0.0.1";
    senderCfg.remotePort = 29410;
    senderCfg.enableDiscovery = false;

    std::atomic<int> recvCount(0);
    LDds receiver;
    LDds sender;
    receiver.registerType<Phase5Msg>("Phase5Msg", topic);
    sender.registerType<Phase5Msg>("Phase5Msg", topic);
    receiver.subscribeTopic<Phase5Msg>(topic, [&](const Phase5Msg &) {
        recvCount.fetch_add(1);
    });

    ok &= check(receiver.initialize(receiverQos, receiverCfg), "持久化接收端初始化");
    ok &= check(sender.initialize(senderQos, senderCfg), "持久化发送端初始化");
    ok &= check(sender.publishTopicByTopic<Phase5Msg>(topic, Phase5Msg{10}), "持久化发布 10");
    ok &= check(sender.publishTopicByTopic<Phase5Msg>(topic, Phase5Msg{20}), "持久化发布 20");
    ok &= check(waitForAtLeast(recvCount, 2, 3000), "持久化接收端应收到 2 条消息");

    sender.shutdown();
    receiver.shutdown();

    LDds replayReceiver;
    replayReceiver.registerType<Phase5Msg>("Phase5Msg", topic);
    ok &= check(
        replayReceiver.initialize(receiverQos, receiverCfg),
        "持久化回放接收端初始化");

    std::vector<uint8_t> latestRaw;
    ok &= check(
        replayReceiver.domain().getTopicData(static_cast<int>(topic), latestRaw),
        "持久化回放应存在最新 topic 数据");
    if (ok)
    {
        ok &= check(latestRaw.size() == sizeof(Phase5Msg), "持久化回放载荷大小");
        if (latestRaw.size() == sizeof(Phase5Msg))
        {
            Phase5Msg latest{};
            std::memcpy(&latest, latestRaw.data(), sizeof(Phase5Msg));
            ok &= check(latest.value == 20, "持久化回放最新值应为 20");
        }
    }

    replayReceiver.shutdown();
    return ok;
}

bool runOwnershipExclusiveCase()
{
    bool ok = true;
    const uint32_t topic = 11002;

    LQos receiverQos;
    receiverQos.transportType = TransportType::UDP;
    receiverQos.domainId = 42;
    receiverQos.ownershipKind = OwnershipKind::Exclusive;
    receiverQos.ownershipStrength = 0;

    LQos lowWriterQos = receiverQos;
    lowWriterQos.ownershipStrength = 1;
    LQos highWriterQos = receiverQos;
    highWriterQos.ownershipStrength = 10;

    TransportConfig receiverCfg;
    receiverCfg.bindAddress = "127.0.0.1";
    receiverCfg.bindPort = 29420;
    receiverCfg.enableDiscovery = false;

    TransportConfig writerLowCfg;
    writerLowCfg.bindAddress = "127.0.0.1";
    writerLowCfg.bindPort = 29421;
    writerLowCfg.remoteAddress = "127.0.0.1";
    writerLowCfg.remotePort = 29420;
    writerLowCfg.enableDiscovery = false;

    TransportConfig writerHighCfg;
    writerHighCfg.bindAddress = "127.0.0.1";
    writerHighCfg.bindPort = 29422;
    writerHighCfg.remoteAddress = "127.0.0.1";
    writerHighCfg.remotePort = 29420;
    writerHighCfg.enableDiscovery = false;

    LDds receiver;
    LDds writerLow;
    LDds writerHigh;
    receiver.registerType<Phase5Msg>("Phase5Msg", topic);
    writerLow.registerType<Phase5Msg>("Phase5Msg", topic);
    writerHigh.registerType<Phase5Msg>("Phase5Msg", topic);

    std::atomic<int> callbackCount(0);
    std::atomic<int> lastValue(-1);
    receiver.subscribeTopic<Phase5Msg>(topic, [&](const Phase5Msg & msg) {
        lastValue.store(msg.value);
        callbackCount.fetch_add(1);
    });

    ok &= check(receiver.initialize(receiverQos, receiverCfg), "Ownership 接收端初始化");
    ok &= check(writerLow.initialize(lowWriterQos, writerLowCfg), "Ownership 低优先级写者初始化");
    ok &= check(writerHigh.initialize(highWriterQos, writerHighCfg), "Ownership 高优先级写者初始化");

    ok &= check(writerLow.publishTopicByTopic<Phase5Msg>(topic, Phase5Msg{1}), "低优先级写者发布 1");
    ok &= check(waitForAtLeast(callbackCount, 1, 2000), "应收到低优先级写者消息");

    ok &= check(writerHigh.publishTopicByTopic<Phase5Msg>(topic, Phase5Msg{2}), "高优先级写者发布 2");
    ok &= check(waitForAtLeast(callbackCount, 2, 2000), "应收到高优先级写者消息");

    ok &= check(writerLow.publishTopicByTopic<Phase5Msg>(topic, Phase5Msg{3}), "低优先级写者发布 3");
    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    ok &= check(callbackCount.load() == 2, "高优先级接管后低优先级写者应被忽略");
    ok &= check(lastValue.load() == 2, "最终值应保持为高优先级写者数据");

    writerHigh.shutdown();
    writerLow.shutdown();
    receiver.shutdown();
    return ok;
}

bool writeTextFile(const std::filesystem::path & path, const std::string & text)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open())
    {
        return false;
    }
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.flush();
    return output.good();
}

bool runQosHotReloadCase()
{
    bool ok = true;
    const std::filesystem::path qosPath = "build/stage11_phase5_smoke/hot_reload_qos.xml";
    std::error_code ec;
    std::filesystem::create_directories(qosPath.parent_path(), ec);

    const std::string initialXml =
        "<qos transportType=\"udp\" domainId=\"43\" historyDepth=\"1\" deadlineMs=\"0\" reliable=\"false\"/>";
    const std::string updatedXml =
        "<qos transportType=\"udp\" domainId=\"43\" historyDepth=\"4\" deadlineMs=\"600\" reliable=\"true\"/>";
    const std::string brokenXml = "<qos><broken>";

    ok &= check(writeTextFile(qosPath, initialXml), "写入初始 qos.xml");

    TransportConfig cfg;
    cfg.bindAddress = "127.0.0.1";
    cfg.bindPort = 29430;
    cfg.remoteAddress = "127.0.0.1";
    cfg.remotePort = 29431;
    cfg.enableDiscovery = false;

    LDds dds;
    ok &= check(dds.initializeFromQosXml(qosPath.string(), cfg), "热更新场景 initializeFromQosXml");
    if (!ok)
    {
        dds.shutdown();
        return false;
    }

    ok &= check(writeTextFile(qosPath, updatedXml), "写入更新后的 qos.xml");

    bool reloaded = false;
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - start)
               .count() < 6000)
    {
        const LQos current = dds.getQos();
        if (current.reliable && current.historyDepth == 4 && current.deadlineMs == 600)
        {
            reloaded = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    ok &= check(reloaded, "热更新应应用 deadline/history/reliability");

    ok &= check(writeTextFile(qosPath, brokenXml), "写入错误 qos.xml");
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    ok &= check(dds.isRunning(), "热更新解析失败不应导致进程退出");

    dds.shutdown();
    return ok;
}

} // namespace

int main()
{
    bool ok = true;
    ok &= runDurabilityPersistentCase();
    ok &= runOwnershipExclusiveCase();
    ok &= runQosHotReloadCase();

    std::cout << "[stage11_phase5] result=" << (ok ? "ok" : "fail")
              << " 说明=" << (ok ? "通过" : "失败") << "\n";
    return ok ? 0 : 1;
}
