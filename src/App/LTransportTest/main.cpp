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
    std::cout << "Usage:\n";
    std::cout << "  " << exe << " --protocol <udp|tcp> --mode <sender|receiver> [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --bind <ip:port>          Local bind endpoint, default 0.0.0.0:0\n";
    std::cout << "  --remote <ip:port>        Remote endpoint (required for sender unless broadcast)\n";
    std::cout << "  --broadcast               Enable UDP broadcast and send to 255.255.255.255\n";
    std::cout << "  --topic <u32>             Message topic, default 1\n";
    std::cout << "  --sequence <u64>          Start sequence, default 1\n";
    std::cout << "  --payload <text>          Message payload text, default hello\n";
    std::cout << "  --count <n>               Sender message count, default 1\n";
    std::cout << "  --interval-ms <n>         Sender interval ms, default 200\n";
    std::cout << "  --expect <n>              Receiver expected messages, default 1\n";
    std::cout << "  --timeout-ms <n>          Wait timeout ms, default 5000\n";
    std::cout << "  --help                    Show this help\n\n";
    std::cout << "Examples:\n";
    std::cout << "  # UDP receiver\n";
    std::cout << "  " << exe << " --protocol udp --mode receiver --bind 127.0.0.1:7001 --expect 3\n";
    std::cout << "  # UDP sender\n";
    std::cout << "  " << exe << " --protocol udp --mode sender --remote 127.0.0.1:7001 --count 3 --payload test\n";
    std::cout << "  # TCP receiver(server)\n";
    std::cout << "  " << exe << " --protocol tcp --mode receiver --bind 127.0.0.1:7002 --expect 3\n";
    std::cout << "  # TCP sender(client)\n";
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
            std::cerr << "Missing value for argument: " << arg << "\n";
            return false;
        }

        if (arg == "--protocol") {
            if (!parseProtocol(argv[++i], options.protocol)) {
                std::cerr << "Invalid protocol\n";
                return false;
            }
            continue;
        }
        if (arg == "--mode") {
            if (!parseMode(argv[++i], options.mode)) {
                std::cerr << "Invalid mode\n";
                return false;
            }
            continue;
        }
        if (arg == "--bind") {
            if (!parseEndpoint(argv[++i], options.bindAddress, options.bindPort)) {
                std::cerr << "Invalid bind endpoint, expected ip:port\n";
                return false;
            }
            continue;
        }
        if (arg == "--remote") {
            if (!parseEndpoint(argv[++i], options.remoteAddress, options.remotePort)) {
                std::cerr << "Invalid remote endpoint, expected ip:port\n";
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
                std::cerr << "Invalid topic\n";
                return false;
            }
            continue;
        }
        if (arg == "--sequence") {
            if (!parseU64(argv[++i], options.sequenceStart)) {
                std::cerr << "Invalid sequence\n";
                return false;
            }
            continue;
        }
        if (arg == "--count") {
            if (!parsePositiveInt(argv[++i], options.sendCount)) {
                std::cerr << "Invalid count\n";
                return false;
            }
            continue;
        }
        if (arg == "--interval-ms") {
            if (!parsePositiveInt(argv[++i], options.sendIntervalMs)) {
                std::cerr << "Invalid interval-ms\n";
                return false;
            }
            continue;
        }
        if (arg == "--expect") {
            if (!parsePositiveInt(argv[++i], options.expectCount)) {
                std::cerr << "Invalid expect\n";
                return false;
            }
            continue;
        }
        if (arg == "--timeout-ms") {
            if (!parsePositiveInt(argv[++i], options.timeoutMs)) {
                std::cerr << "Invalid timeout-ms\n";
                return false;
            }
            continue;
        }

        std::cerr << "Unknown argument: " << arg << "\n";
        return false;
    }

    if (options.mode == RunMode::Sender) {
        if (options.remotePort == 0) {
            std::cerr << "Sender requires --remote ip:port\n";
            return false;
        }
    }

    if (options.enableBroadcast && options.protocol != TransportProtocol::UDP) {
        std::cerr << "--broadcast is only valid with UDP\n";
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
    config.bindAddress = QString::fromStdString(options.bindAddress);
    config.bindPort = options.bindPort;
    config.remoteAddress = QString::fromStdString(options.remoteAddress);
    config.remotePort = options.remotePort;
    config.enableBroadcast = options.enableBroadcast;
    return config;
}

int runSender(const Options& options)
{
    auto transport = ITransport::createTransport(options.protocol);
    transport->setConfig(toConfig(options));

    if (!transport->start()) {
        std::cerr << "start() failed: " << transport->getLastError().toStdString() << "\n";
        return EXIT_FAILURE;
    }

    if (options.enableBroadcast) {
        const QHostAddress broadcastAddress = QHostAddress::Broadcast;
        if (!transport->setDefaultRemote(broadcastAddress, options.remotePort)) {
            std::cerr << "setDefaultRemote(broadcast) failed: "
                      << transport->getLastError().toStdString() << "\n";
            transport->stop();
            return EXIT_FAILURE;
        }
    } else {
        const QHostAddress remoteAddress(QString::fromStdString(options.remoteAddress));
        if (!transport->setDefaultRemote(remoteAddress, options.remotePort)) {
            std::cerr << "setDefaultRemote() failed: "
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
                      << " size=" << payload.size() << " bytes\n";
        } else {
            std::cout << "[SEND-FAIL] seq=" << (options.sequenceStart + static_cast<uint64_t>(i))
                      << " error=" << transport->getLastError().toStdString() << "\n";
        }

        if (i + 1 < options.sendCount && options.sendIntervalMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(options.sendIntervalMs));
        }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    transport->stop();

    std::cout << "Sender done, success=" << sentOk << "/" << options.sendCount << "\n";
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
        [&](const LMessage& message, const QHostAddress& sender, quint16 senderPort) {
            const int current = receivedCount.fetch_add(1) + 1;
            const std::string payloadText(
                message.getPayload().begin(),
                message.getPayload().end()
            );

            std::cout << "[RECV] #" << current
                      << " topic=" << message.getTopic()
                      << " seq=" << message.getSequence()
                      << " size=" << message.getPayloadSize()
                      << " from=" << sender.toString().toStdString()
                      << ":" << senderPort
                      << " payload=\"" << payloadText << "\"\n";

            waitCv.notify_all();
        }
    );

    if (!transport->start()) {
        std::cerr << "start() failed: " << transport->getLastError().toStdString() << "\n";
        return EXIT_FAILURE;
    }

    std::cout << "Receiver started, protocol="
              << (options.protocol == TransportProtocol::UDP ? "udp" : "tcp")
              << " bind=" << options.bindAddress
              << ":" << transport->getBoundPort()
              << " expect=" << options.expectCount
              << " timeoutMs=" << options.timeoutMs
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

    std::cout << "Receiver done, received=" << receivedCount.load()
              << " expected=" << options.expectCount << "\n";
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
        std::cerr << "Unhandled exception: " << ex.what() << "\n";
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << "Unhandled unknown exception\n";
        return EXIT_FAILURE;
    }
}
