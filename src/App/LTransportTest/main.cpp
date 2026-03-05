#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ITransport.h"
#include "LMessage.h"

namespace {

using LDdsFramework::ITransport;
using LDdsFramework::LMessage;
using LDdsFramework::TransportConfig;
using LDdsFramework::TransportProtocol;

enum class RunMode {
    Sender,
    Receiver
};

struct Options {
    TransportProtocol protocol = TransportProtocol::UDP;
    RunMode mode = RunMode::Receiver;

    std::string bindAddress = "0.0.0.0";
    uint16_t bindPort = 0;

    std::string remoteAddress;
    uint16_t remotePort = 0;

    bool enableBroadcast = false;

    uint32_t topic = 1;
    uint64_t sequenceStart = 1;
    std::string payload = "hello";

    int sendCount = 1;
    int sendIntervalMs = 200;
    int expectCount = 1;
    int timeoutMs = 5000;
    bool showHelp = false;
};

void printUsage(const char* exe)
{
    std::cout << "用法:\n";
    std::cout << "  " << exe << " --protocol <udp|tcp> --mode <sender|receiver> [选项]\n\n";
    std::cout << "选项:\n";
    std::cout << "  --bind <ip:port>          本地绑定端点，默认 0.0.0.0:0\n";
    std::cout << "  --remote <ip:port>        远端端点（发送端必填，除非启用广播）\n";
    std::cout << "  --broadcast               启用 UDP 广播并发送到 255.255.255.255\n";
    std::cout << "  --topic <u32>             消息 Topic，默认 1\n";
    std::cout << "  --sequence <u64>          起始序列号，默认 1\n";
    std::cout << "  --payload <text>          消息文本载荷，默认 hello\n";
    std::cout << "  --count <n>               发送消息数量，默认 1\n";
    std::cout << "  --interval-ms <n>         发送间隔毫秒，默认 200\n";
    std::cout << "  --expect <n>              接收端期望消息数，默认 1\n";
    std::cout << "  --timeout-ms <n>          等待超时毫秒，默认 5000\n";
    std::cout << "  --help                    显示帮助\n\n";
    std::cout << "示例:\n";
    std::cout << "  # UDP 接收端\n";
    std::cout << "  " << exe << " --protocol udp --mode receiver --bind 127.0.0.1:7001 --expect 3\n";
    std::cout << "  # UDP 发送端\n";
    std::cout << "  " << exe << " --protocol udp --mode sender --remote 127.0.0.1:7001 --count 3 --payload test\n";
    std::cout << "  # TCP 接收端(服务端)\n";
    std::cout << "  " << exe << " --protocol tcp --mode receiver --bind 127.0.0.1:7002 --expect 3\n";
    std::cout << "  # TCP 发送端(客户端)\n";
    std::cout << "  " << exe << " --protocol tcp --mode sender --remote 127.0.0.1:7002 --count 3\n";
}

bool parseEndpoint(const std::string& text, std::string& ip, uint16_t& port)
{
    const std::size_t pos = text.rfind(':');
    if (pos == std::string::npos || pos == 0 || pos == text.size() - 1) {
        return false;
    }

    ip = text.substr(0, pos);
    try {
        const unsigned long parsed = std::stoul(text.substr(pos + 1));
        if (parsed > 65535UL) {
            return false;
        }
        port = static_cast<uint16_t>(parsed);
    } catch (...) {
        return false;
    }
    return true;
}

bool parseProtocol(const std::string& text, TransportProtocol& protocol)
{
    if (text == "udp" || text == "UDP") {
        protocol = TransportProtocol::UDP;
        return true;
    }
    if (text == "tcp" || text == "TCP") {
        protocol = TransportProtocol::TCP;
        return true;
    }
    return false;
}

bool parseMode(const std::string& text, RunMode& mode)
{
    if (text == "sender") {
        mode = RunMode::Sender;
        return true;
    }
    if (text == "receiver") {
        mode = RunMode::Receiver;
        return true;
    }
    return false;
}

bool parsePositiveInt(const std::string& text, int& value)
{
    try {
        const int parsed = std::stoi(text);
        if (parsed < 0) {
            return false;
        }
        value = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

bool parseU32(const std::string& text, uint32_t& value)
{
    try {
        const unsigned long parsed = std::stoul(text);
        if (parsed > 0xFFFFFFFFUL) {
            return false;
        }
        value = static_cast<uint32_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool parseU64(const std::string& text, uint64_t& value)
{
    try {
        value = std::stoull(text);
        return true;
    } catch (...) {
        return false;
    }
}

bool parseArgs(int argc, char* argv[], Options& options)
{
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            options.showHelp = true;
            return true;
        }

        if (i + 1 >= argc &&
            arg != "--broadcast") {
            std::cerr << "参数缺少取值: " << arg << "\n";
            return false;
        }

        if (arg == "--protocol") {
            if (!parseProtocol(argv[++i], options.protocol)) {
                std::cerr << "无效的协议，支持 udp/tcp\n";
                return false;
            }
            continue;
        }
        if (arg == "--mode") {
            if (!parseMode(argv[++i], options.mode)) {
                std::cerr << "无效的模式，支持 sender/receiver\n";
                return false;
            }
            continue;
        }
        if (arg == "--bind") {
            if (!parseEndpoint(argv[++i], options.bindAddress, options.bindPort)) {
                std::cerr << "无效的本地绑定端点，格式应为 ip:port\n";
                return false;
            }
            continue;
        }
        if (arg == "--remote") {
            if (!parseEndpoint(argv[++i], options.remoteAddress, options.remotePort)) {
                std::cerr << "无效的远端端点，格式应为 ip:port\n";
                return false;
            }
            continue;
        }
        if (arg == "--broadcast") {
            options.enableBroadcast = true;
            continue;
        }
        if (arg == "--payload") {
            options.payload = argv[++i];
            continue;
        }
        if (arg == "--topic") {
            if (!parseU32(argv[++i], options.topic)) {
                std::cerr << "无效的 topic\n";
                return false;
            }
            continue;
        }
        if (arg == "--sequence") {
            if (!parseU64(argv[++i], options.sequenceStart)) {
                std::cerr << "无效的 sequence\n";
                return false;
            }
            continue;
        }
        if (arg == "--count") {
            if (!parsePositiveInt(argv[++i], options.sendCount)) {
                std::cerr << "无效的 count\n";
                return false;
            }
            continue;
        }
        if (arg == "--interval-ms") {
            if (!parsePositiveInt(argv[++i], options.sendIntervalMs)) {
                std::cerr << "无效的 interval-ms\n";
                return false;
            }
            continue;
        }
        if (arg == "--expect") {
            if (!parsePositiveInt(argv[++i], options.expectCount)) {
                std::cerr << "无效的 expect\n";
                return false;
            }
            continue;
        }
        if (arg == "--timeout-ms") {
            if (!parsePositiveInt(argv[++i], options.timeoutMs)) {
                std::cerr << "无效的 timeout-ms\n";
                return false;
            }
            continue;
        }

        std::cerr << "未知参数: " << arg << "\n";
        return false;
    }

    if (options.mode == RunMode::Sender) {
        if (options.remotePort == 0) {
            std::cerr << "发送端必须提供 --remote ip:port\n";
            return false;
        }
    }

    if (options.enableBroadcast && options.protocol != TransportProtocol::UDP) {
        std::cerr << "--broadcast 仅支持 UDP\n";
        return false;
    }

    return true;
}

std::vector<uint8_t> toPayloadBytes(const std::string& payload)
{
    return std::vector<uint8_t>(payload.begin(), payload.end());
}

TransportConfig toConfig(const Options& options)
{
    TransportConfig config;
    config.bindAddress = LString::fromStdString(options.bindAddress);
    config.bindPort = options.bindPort;
    config.remoteAddress = LString::fromStdString(options.remoteAddress);
    config.remotePort = options.remotePort;
    config.enableBroadcast = options.enableBroadcast;
    return config;
}

int runSender(const Options& options)
{
    auto transport = ITransport::createTransport(options.protocol);
    transport->setConfig(toConfig(options));

    if (!transport->start()) {
        std::cerr << "启动传输失败: " << transport->getLastError().toStdString() << "\n";
        return EXIT_FAILURE;
    }

    if (options.enableBroadcast) {
        const LHostAddress broadcastAddress = LHostAddress::Broadcast;
        if (!transport->setDefaultRemote(broadcastAddress, options.remotePort)) {
            std::cerr << "设置广播默认远端失败: "
                      << transport->getLastError().toStdString() << "\n";
            transport->stop();
            return EXIT_FAILURE;
        }
    } else {
        const LHostAddress remoteAddress(LString::fromStdString(options.remoteAddress));
        if (!transport->setDefaultRemote(remoteAddress, options.remotePort)) {
            std::cerr << "设置默认远端失败: "
                      << transport->getLastError().toStdString() << "\n";
            transport->stop();
            return EXIT_FAILURE;
        }
    }

    const std::vector<uint8_t> payload = toPayloadBytes(options.payload);
    int sentOk = 0;

    for (int i = 0; i < options.sendCount; ++i) {
        LMessage message(options.topic, options.sequenceStart + static_cast<uint64_t>(i), payload);
        const bool ok = transport->sendMessage(message);
        if (ok) {
            ++sentOk;
            std::cout << "[SEND] seq=" << (options.sequenceStart + static_cast<uint64_t>(i))
                      << " 大小=" << payload.size() << " 字节\n";
        } else {
            std::cout << "[SEND-FAIL] seq=" << (options.sequenceStart + static_cast<uint64_t>(i))
                      << " 错误=" << transport->getLastError().toStdString() << "\n";
        }

        if (i + 1 < options.sendCount && options.sendIntervalMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(options.sendIntervalMs));
        }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    transport->stop();

    std::cout << "发送端完成，成功=" << sentOk << "/" << options.sendCount << "\n";
    return (sentOk == options.sendCount) ? EXIT_SUCCESS : EXIT_FAILURE;
}

int runReceiver(const Options& options)
{
    auto transport = ITransport::createTransport(options.protocol);
    transport->setConfig(toConfig(options));

    std::atomic<int> receivedCount(0);
    std::mutex waitMutex;
    std::condition_variable waitCv;

    transport->setReceiveCallback(
        [&](const LMessage& message, const LHostAddress& sender, quint16 senderPort) {
            const int current = receivedCount.fetch_add(1) + 1;
            const std::string payloadText(
                message.getPayload().begin(),
                message.getPayload().end()
            );

            std::cout << "[RECV] #" << current
                      << " topic=" << message.getTopic()
                      << " seq=" << message.getSequence()
                      << " 大小=" << message.getPayloadSize()
                      << " 来源=" << sender.toString().toStdString()
                      << ":" << senderPort
                      << " 载荷=\"" << payloadText << "\"\n";

            waitCv.notify_all();
        }
    );

    if (!transport->start()) {
        std::cerr << "启动传输失败: " << transport->getLastError().toStdString() << "\n";
        return EXIT_FAILURE;
    }

    std::cout << "接收端已启动，协议="
              << (options.protocol == TransportProtocol::UDP ? "udp" : "tcp")
              << " 绑定="
              << options.bindAddress
              << ":" << transport->getBoundPort()
              << " 期望消息数=" << options.expectCount
              << " 超时毫秒=" << options.timeoutMs
              << "\n";

    bool reachedTarget = false;
    {
        std::unique_lock<std::mutex> lock(waitMutex);
        reachedTarget = waitCv.wait_for(
            lock,
            std::chrono::milliseconds(options.timeoutMs),
            [&] { return receivedCount.load() >= options.expectCount; }
        );
    }

    transport->stop();

    std::cout << "接收端结束，已接收=" << receivedCount.load()
              << " 期望=" << options.expectCount << "\n";
    return reachedTarget ? EXIT_SUCCESS : EXIT_FAILURE;
}

} // namespace

int main(int argc, char* argv[])
{
    Options options;
    if (!parseArgs(argc, argv, options)) {
        printUsage(argv[0]);
        return EXIT_FAILURE;
    }
    if (options.showHelp) {
        printUsage(argv[0]);
        return EXIT_SUCCESS;
    }

    try {
        if (options.mode == RunMode::Sender) {
            return runSender(options);
        }
        return runReceiver(options);
    } catch (const std::exception& ex) {
        std::cerr << "未处理异常: " << ex.what() << "\n";
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << "未处理的未知异常\n";
        return EXIT_FAILURE;
    }
}
