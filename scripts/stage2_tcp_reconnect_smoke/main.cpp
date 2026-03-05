#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

#include "LMessage.h"
#include "LTcpTransport.h"

using namespace LDdsFramework;

namespace {

bool check(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "[stage2_tcp] FAIL(失败): " << message << "\n";
        return false;
    }
    return true;
}

LMessage makeMessage(uint64_t seq, uint8_t value)
{
    std::vector<uint8_t> payload = {value};
    LMessage msg(9101, seq, payload);
    return msg;
}

bool waitForCount(const std::atomic<int>& value, int expectedMin, int timeoutMs)
{
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - start)
               .count() < timeoutMs) {
        if (value.load() >= expectedMin) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return value.load() >= expectedMin;
}

bool runReconnectCase()
{
    bool ok = true;

    LTcpTransport receiver;
    TransportConfig receiverCfg;
    receiverCfg.bindAddress = "127.0.0.1";
    receiverCfg.bindPort = 27600;
    receiver.setConfig(receiverCfg);

    std::atomic<int> receivedCount(0);
    receiver.setReceiveCallback(
        [&](const LMessage&, const LHostAddress&, quint16) {
            receivedCount.fetch_add(1);
        }
    );

    ok &= check(receiver.start(), "接收端启动应成功");

    LTcpTransport sender;
    TransportConfig senderCfg;
    senderCfg.bindAddress = "127.0.0.1";
    senderCfg.bindPort = 27601;
    senderCfg.remoteAddress = "127.0.0.1";
    senderCfg.remotePort = 27600;
    senderCfg.autoReconnect = true;
    senderCfg.reconnectMinMs = 100;
    senderCfg.reconnectMaxMs = 1000;
    senderCfg.reconnectMultiplier = 2.0;
    senderCfg.maxPendingMessages = 128;
    senderCfg.sendQueueOverflowPolicy = SendQueueOverflowPolicy::DropOldest;

    sender.setConfig(senderCfg);
    ok &= check(sender.start(), "发送端启动应成功");
    if (!ok) {
        sender.stop();
        receiver.stop();
        return false;
    }

    for (int i = 0; i < 10; ++i) {
        ok &= check(sender.sendMessage(makeMessage(static_cast<uint64_t>(i + 1), 0x11)), "初始发送应成功");
    }

    ok &= check(waitForCount(receivedCount, 1, 3000), "接收端应收到初始消息");
    ok &= check(sender.getConnectionCount() <= 1, "同一端点不应创建重复连接");

    receiver.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    for (int i = 0; i < 30; ++i) {
        (void)sender.sendMessage(makeMessage(static_cast<uint64_t>(100 + i), 0x22));
    }

    receiver.setConfig(receiverCfg);
    ok &= check(receiver.start(), "断线后接收端重启应成功");

    for (int i = 0; i < 20; ++i) {
        (void)sender.sendMessage(makeMessage(static_cast<uint64_t>(200 + i), 0x33));
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }

    const int beforeRecovery = receivedCount.load();
    ok &= check(waitForCount(receivedCount, beforeRecovery + 1, 5000), "应恢复连接并继续接收");

    sender.stop();
    receiver.stop();
    return ok;
}

bool runQueuePolicyCase()
{
    bool ok = true;

    auto runPolicyCase = [](SendQueueOverflowPolicy policy, int& successCount, int& failCount) {
        LTcpTransport sender;
        TransportConfig cfg;
        cfg.bindAddress = "127.0.0.1";
        cfg.bindPort = (policy == SendQueueOverflowPolicy::DropOldest) ? 27611 :
                       (policy == SendQueueOverflowPolicy::DropNewest) ? 27612 : 27613;
        cfg.remoteAddress = "127.0.0.1";
        cfg.remotePort = 29999; // no listener
        cfg.autoReconnect = true;
        cfg.reconnectMinMs = 100;
        cfg.reconnectMaxMs = 500;
        cfg.reconnectMultiplier = 2.0;
        cfg.maxPendingMessages = 4;
        cfg.sendQueueOverflowPolicy = policy;

        successCount = 0;
        failCount = 0;

        sender.setConfig(cfg);
        if (!sender.start()) {
            return false;
        }

        for (int i = 0; i < 20; ++i) {
            if (sender.sendMessage(makeMessage(static_cast<uint64_t>(500 + i), 0x44))) {
                successCount += 1;
            } else {
                failCount += 1;
            }
        }

        sender.stop();
        return true;
    };

    int dropOldestOk = 0;
    int dropOldestFail = 0;
    int dropNewestOk = 0;
    int dropNewestFail = 0;
    int failFastOk = 0;
    int failFastFail = 0;

    ok &= check(
        runPolicyCase(SendQueueOverflowPolicy::DropOldest, dropOldestOk, dropOldestFail),
        "DropOldest 场景应可执行"
    );
    ok &= check(
        runPolicyCase(SendQueueOverflowPolicy::DropNewest, dropNewestOk, dropNewestFail),
        "DropNewest 场景应可执行"
    );
    ok &= check(
        runPolicyCase(SendQueueOverflowPolicy::FailFast, failFastOk, failFastFail),
        "FailFast 场景应可执行"
    );

    ok &= check(dropOldestOk > 0, "DropOldest 在溢出下应仍接收部分消息");
    ok &= check(dropNewestFail > 0, "DropNewest 在溢出下应拒绝部分消息");
    ok &= check(failFastFail > 0, "FailFast 在队列满时应快速失败");

    return ok;
}

} // namespace

int main()
{
    bool ok = true;
    ok &= runReconnectCase();
    ok &= runQueuePolicyCase();

    std::cout << "[stage2_tcp] result=" << (ok ? "ok" : "fail")
              << " 说明=" << (ok ? "通过" : "失败") << "\n";
    return ok ? 0 : 1;
}
