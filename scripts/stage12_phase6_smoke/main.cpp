#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "LDds.h"

using namespace LDdsFramework;

namespace {

struct Phase6Msg
{
    int value;
};

bool check(bool condition, const std::string & message)
{
    if (!condition)
    {
        std::cerr << "[stage12_phase6] FAIL(失败): " << message << "\n";
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
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return counter.load() >= expected;
}

uint64_t parseMetricValue(const std::string & text, const std::string & metricName)
{
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line))
    {
        if (line.rfind(metricName + "{", 0) != 0 && line.rfind(metricName + " ", 0) != 0)
        {
            continue;
        }

        const size_t sep = line.find_last_of(' ');
        if (sep == std::string::npos || sep + 1 >= line.size())
        {
            continue;
        }

        try
        {
            return static_cast<uint64_t>(std::stoull(line.substr(sep + 1)));
        }
        catch (...)
        {
            return 0;
        }
    }
    return 0;
}

bool runMetricsAndLoggingCase()
{
    bool ok = true;
    const uint32_t topic = 12001;

    LQos receiverQos;
    receiverQos.transportType = TransportType::UDP;
    receiverQos.domainId = 51;
    receiverQos.structuredLogEnabled = true;
    receiverQos.enableMetrics = true;

    LQos senderQos = receiverQos;

    TransportConfig receiverCfg;
    receiverCfg.bindAddress = "127.0.0.1";
    receiverCfg.bindPort = 29510;
    receiverCfg.enableDiscovery = false;

    TransportConfig senderCfg;
    senderCfg.bindAddress = "127.0.0.1";
    senderCfg.bindPort = 29511;
    senderCfg.remoteAddress = "127.0.0.1";
    senderCfg.remotePort = 29510;
    senderCfg.enableDiscovery = false;

    LDds receiver;
    LDds sender;
    receiver.registerType<Phase6Msg>("Phase6Msg", topic);
    sender.registerType<Phase6Msg>("Phase6Msg", topic);

    std::atomic<int> recvCount(0);
    receiver.subscribeTopic<Phase6Msg>(topic, [&](const Phase6Msg &) {
        recvCount.fetch_add(1);
    });

    std::vector<std::string> logs;
    std::mutex logsMutex;
    auto appendLog = [&](const std::string & line) {
        std::lock_guard<std::mutex> lock(logsMutex);
        logs.push_back(line);
    };
    receiver.setLogCallback(appendLog);
    sender.setLogCallback(appendLog);

    ok &= check(receiver.initialize(receiverQos, receiverCfg), "指标/日志接收端初始化");
    ok &= check(sender.initialize(senderQos, senderCfg), "指标/日志发送端初始化");

    ok &= check(sender.publishTopicByTopic<Phase6Msg>(topic, Phase6Msg{1}), "发布 #1");
    ok &= check(sender.publishTopicByTopic<Phase6Msg>(topic, Phase6Msg{2}), "发布 #2");
    ok &= check(sender.publishTopicByTopic<Phase6Msg>(topic, Phase6Msg{3}), "发布 #3");
    ok &= check(waitForAtLeast(recvCount, 3, 3000), "接收端应收到 3 条消息");

    const std::string senderMetrics = sender.exportMetricsText();
    const std::string receiverMetrics = receiver.exportMetricsText();

    ok &= check(parseMetricValue(senderMetrics, "ldds_messages_sent_total") >= 3,
                "发送计数指标应 >= 3");
    ok &= check(parseMetricValue(receiverMetrics, "ldds_messages_received_total") >= 3,
                "接收计数指标应 >= 3");

    bool hasTraceLog = false;
    {
        std::lock_guard<std::mutex> lock(logsMutex);
        for (const std::string & line : logs)
        {
            if (line.find("topic=" + std::to_string(topic)) != std::string::npos &&
                line.find("messageId=") != std::string::npos &&
                line.find("peer=") != std::string::npos)
            {
                hasTraceLog = true;
                break;
            }
        }
    }
    ok &= check(hasTraceLog, "结构化日志应包含 topic/messageId/peer 字段");

    sender.shutdown();
    receiver.shutdown();
    return ok;
}

bool runSecurityCase()
{
    bool ok = true;
    const uint32_t topic = 12002;

    LQos receiverQos;
    receiverQos.transportType = TransportType::UDP;
    receiverQos.domainId = 52;
    receiverQos.securityEnabled = true;
    receiverQos.securityEncryptPayload = true;
    receiverQos.securityPsk = "phase6-secret";

    LQos authorizedSenderQos = receiverQos;
    LQos unauthorizedSenderQos = receiverQos;
    unauthorizedSenderQos.securityPsk = "phase6-wrong";

    TransportConfig receiverCfg;
    receiverCfg.bindAddress = "127.0.0.1";
    receiverCfg.bindPort = 29520;
    receiverCfg.enableDiscovery = false;

    TransportConfig senderCfg;
    senderCfg.bindAddress = "127.0.0.1";
    senderCfg.bindPort = 29521;
    senderCfg.remoteAddress = "127.0.0.1";
    senderCfg.remotePort = 29520;
    senderCfg.enableDiscovery = false;

    TransportConfig badCfg;
    badCfg.bindAddress = "127.0.0.1";
    badCfg.bindPort = 29522;
    badCfg.remoteAddress = "127.0.0.1";
    badCfg.remotePort = 29520;
    badCfg.enableDiscovery = false;

    LDds receiver;
    LDds sender;
    LDds unauthorized;

    receiver.registerType<Phase6Msg>("Phase6Msg", topic);
    sender.registerType<Phase6Msg>("Phase6Msg", topic);
    unauthorized.registerType<Phase6Msg>("Phase6Msg", topic);

    std::atomic<int> recvCount(0);
    std::atomic<int> lastValue(-1);
    receiver.subscribeTopic<Phase6Msg>(topic, [&](const Phase6Msg & msg) {
        lastValue.store(msg.value);
        recvCount.fetch_add(1);
    });

    ok &= check(receiver.initialize(receiverQos, receiverCfg), "安全接收端初始化");
    ok &= check(sender.initialize(authorizedSenderQos, senderCfg), "安全授权发送端初始化");
    ok &= check(unauthorized.initialize(unauthorizedSenderQos, badCfg), "安全未授权发送端初始化");

    ok &= check(sender.publishTopicByTopic<Phase6Msg>(topic, Phase6Msg{111}), "授权发送端发布");
    ok &= check(unauthorized.publishTopicByTopic<Phase6Msg>(topic, Phase6Msg{222}), "未授权发送端发布");

    ok &= check(waitForAtLeast(recvCount, 1, 3000), "接收端应接收授权消息");
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    ok &= check(lastValue.load() == 111, "未授权消息应被拒绝");

    const std::string receiverMetrics = receiver.exportMetricsText();
    ok &= check(parseMetricValue(receiverMetrics, "ldds_auth_rejected_total") >= 1,
                "鉴权拒绝指标应 >= 1");

    unauthorized.shutdown();
    sender.shutdown();
    receiver.shutdown();
    return ok;
}

bool runThroughputRound(bool secureEnabled, uint32_t topic, uint8_t domainId, quint16 recvPort, quint16 sendPort,
                        int messageCount, double & elapsedMs)
{
    LQos receiverQos;
    receiverQos.transportType = TransportType::TCP;
    receiverQos.domainId = domainId;
    receiverQos.securityEnabled = secureEnabled;
    receiverQos.securityEncryptPayload = secureEnabled;
    receiverQos.securityPsk = secureEnabled ? "phase6-bench" : "";

    LQos senderQos = receiverQos;

    TransportConfig receiverCfg;
    receiverCfg.bindAddress = "127.0.0.1";
    receiverCfg.bindPort = recvPort;
    receiverCfg.enableDiscovery = false;

    TransportConfig senderCfg;
    senderCfg.bindAddress = "127.0.0.1";
    senderCfg.bindPort = sendPort;
    senderCfg.remoteAddress = "127.0.0.1";
    senderCfg.remotePort = recvPort;
    senderCfg.enableDiscovery = false;

    LDds receiver;
    LDds sender;

    receiver.registerType<Phase6Msg>("Phase6Msg", topic);
    sender.registerType<Phase6Msg>("Phase6Msg", topic);

    std::atomic<int> recvCount(0);
    receiver.subscribeTopic<Phase6Msg>(topic, [&](const Phase6Msg &) {
        recvCount.fetch_add(1);
    });

    if (!receiver.initialize(receiverQos, receiverCfg))
    {
        return false;
    }
    if (!sender.initialize(senderQos, senderCfg))
    {
        receiver.shutdown();
        return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    const auto begin = std::chrono::steady_clock::now();
    for (int i = 0; i < messageCount; ++i)
    {
        if (!sender.publishTopicByTopic<Phase6Msg>(topic, Phase6Msg{i}))
        {
            sender.shutdown();
            receiver.shutdown();
            return false;
        }
    }

    const bool receivedAll = waitForAtLeast(recvCount, messageCount, 10000);
    const auto end = std::chrono::steady_clock::now();
    elapsedMs = static_cast<double>(
        std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count());

    sender.shutdown();
    receiver.shutdown();
    return receivedAll;
}

bool runPerformanceCase(double & plainMs, double & secureMs)
{
    const int messageCount = 400;
    if (!runThroughputRound(false, 12003, 53, 29600, 29601, messageCount, plainMs))
    {
        return false;
    }
    if (!runThroughputRound(true, 12004, 54, 29610, 29611, messageCount, secureMs))
    {
        return false;
    }
    return true;
}

} // namespace

int main()
{
    bool ok = true;
    ok &= runMetricsAndLoggingCase();
    ok &= runSecurityCase();

    double plainMs = 0.0;
    double secureMs = 0.0;
    ok &= check(runPerformanceCase(plainMs, secureMs), "性能基准应执行完成");

    if (plainMs <= 0.0)
    {
        plainMs = 1.0;
    }
    if (secureMs <= 0.0)
    {
        secureMs = 1.0;
    }

    const double plainMps = 400.0 / (plainMs / 1000.0);
    const double secureMps = 400.0 / (secureMs / 1000.0);
    const double deltaPct = ((secureMs - plainMs) / plainMs) * 100.0;

    std::cout << "[stage12_phase6] perf_plain_ms=" << plainMs
              << " perf_secure_ms=" << secureMs
              << " delta_pct=" << deltaPct
              << " plain_mps=" << plainMps
              << " secure_mps=" << secureMps
              << " 说明=性能对比"
              << "\n";
    std::cout << "[stage12_phase6] result=" << (ok ? "ok" : "fail")
              << " 说明=" << (ok ? "通过" : "失败") << "\n";
    return ok ? 0 : 1;
}
