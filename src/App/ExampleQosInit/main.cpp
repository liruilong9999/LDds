#include <cstdlib>
#include <iostream>
#include <string>

#include "LDds.h"

using namespace LDdsFramework;

namespace {

const char * transportText(TransportType type)
{
    return (type == TransportType::TCP) ? "tcp" : "udp";
}

} // namespace

int main(int argc, char * argv[])
{
    const std::string qosPath = (argc > 1) ? argv[1] : "qos.example.xml";

    LQos qos;
    std::string loadError;
    if (!qos.loadFromXmlFile(qosPath, &loadError))
    {
        std::cerr << "[example_qos_init] QoS加载失败 path=" << qosPath
                  << " error=" << loadError << "\n";
        return EXIT_FAILURE;
    }

    TransportConfig config;
    config.bindAddress = QStringLiteral("127.0.0.1");
    config.bindPort = 26201;
    config.remoteAddress = QStringLiteral("127.0.0.1");
    config.remotePort = 26201;
    config.enableDiscovery = false;
    config.enableDomainPortMapping = false;

    LDds dds;
    dds.setLogCallback([](const std::string & line) {
        std::cout << line << "\n";
    });

    if (!dds.initialize(qos, config))
    {
        std::cerr << "[example_qos_init] 初始化失败 error="
                  << dds.getLastError() << "\n";
        return EXIT_FAILURE;
    }

    const LQos & runningQos = dds.getQos();
    std::cout << "[example_qos_init] result=ok"
              << " 状态=成功"
              << " domain=" << static_cast<uint32_t>(runningQos.domainId)
              << " transport=" << transportText(runningQos.transportType)
              << " reliable=" << (runningQos.reliable ? "true" : "false")
              << " historyDepth=" << runningQos.historyDepth
              << " deadlineMs=" << runningQos.deadlineMs
              << "\n";

    dds.shutdown();
    return EXIT_SUCCESS;
}
