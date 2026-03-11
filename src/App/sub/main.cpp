#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

#include "LDds.h"
#include "file1_topic.h"
#include "file2_topic.h"

using namespace LDdsFramework;

namespace {

TransportConfig makeLocalSubscribeTransport()
{
    TransportConfig config;
    config.bindAddress = LStringLiteral("127.0.0.1");
    config.bindPort = 26101;
    config.enableDiscovery = false;
    config.enableDomainPortMapping = false;
    return config;
}

bool registerLocalGeneratedTypes(LDds & dds)
{
    const bool handleOk = dds.registerType<P1::P2::Handle>(
        P1::P2::Handle::getTypeName(),
        P1::P2::Handle::getTypeId(),
        [](const P1::P2::Handle & object, std::vector<uint8_t> & outPayload) -> bool {
            outPayload = object.serialize();
            return true;
        },
        [](const std::vector<uint8_t> & payload, P1::P2::Handle & object) -> bool {
            return object.deserialize(payload);
        });

    const bool testParamOk = dds.registerType<P3::TestParam>(
        P3::TestParam::getTypeName(),
        P3::TestParam::getTypeId(),
        [](const P3::TestParam & object, std::vector<uint8_t> & outPayload) -> bool {
            outPayload = object.serialize();
            return true;
        },
        [](const std::vector<uint8_t> & payload, P3::TestParam & object) -> bool {
            return object.deserialize(payload);
        });

    return handleOk && testParamOk;
}

} // namespace

int main(int argc, char * argv[])
{
    (void)argc;
    (void)argv;

    /*
      使用说明:
      1. 生成头文件后，应用可以直接构造 P1::P2::Handle / P3::TestParam。
      2. 这里演示“从 Domain 缓存取原始 payload，再用生成类型 deserialize” 的写法，
         这样最直观，也方便你确认收到的数据字节和业务结构体是一一对应的。
      3. 如果后续你更偏向高层接口，也可以改为:
         subscriber.sub(FILE1_TOPIC_KEY_HANDLE_TOPIC)
         或 subscriber.subscribeTopic<T>(...)
    */

    LDds subscriber;
    subscriber.setLogCallback(
        [](const std::string & line) {
            std::cerr << "[sub][ldds] " << line << std::endl;
        });
    if (!subscriber.initialize(makeLocalSubscribeTransport()))
    {
        std::cerr << "[sub] initialize failed error=" << subscriber.getLastError() << std::endl;
        return EXIT_FAILURE;
    }

    if (!registerLocalGeneratedTypes(subscriber))
    {
        std::cerr << "[sub] register generated types failed" << std::endl;
        return EXIT_FAILURE;
    }

    bool gotHandle = false;
    bool gotTestParam = false;
    P1::P2::Handle receivedHandle{};
    P3::TestParam receivedTestParam{};

    for (int attempt = 0; attempt < 100; ++attempt)
    {
        if (!gotHandle)
        {
            std::vector<uint8_t> payload;
            if (subscriber.domain().getTopicData(FILE1_TOPIC_ID_HANDLE_TOPIC, payload) &&
                receivedHandle.deserialize(payload))
            {
                gotHandle = true;
            }
        }

        if (!gotTestParam)
        {
            std::vector<uint8_t> payload;
            if (subscriber.domain().getTopicData(FILE2_TOPIC_ID_TESTPARAM_TOPIC, payload) &&
                receivedTestParam.deserialize(payload))
            {
                gotTestParam = true;
            }
        }

        if (gotHandle && gotTestParam)
        {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!gotHandle || !gotTestParam)
    {
        std::cerr << "[sub] timeout waiting message"
                  << " handleTopic=" << FILE1_TOPIC_KEY_HANDLE_TOPIC
                  << " testParamTopic=" << FILE2_TOPIC_KEY_TESTPARAM_TOPIC
                  << " error=" << subscriber.getLastError() << std::endl;
        subscriber.shutdown();
        return EXIT_FAILURE;
    }

    std::cout << "[sub] result=ok"
              << " handle.handle=" << receivedHandle.handle
              << " handle.datatime=" << receivedHandle.datatime
              << " testParam.handle=" << receivedTestParam.handle
              << " testParam.a=" << receivedTestParam.a << std::endl;

    subscriber.shutdown();
    return EXIT_SUCCESS;
}
