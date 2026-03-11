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

uint64_t nowTimestampMs()
{
    const auto now = std::chrono::system_clock::now();
    const auto ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
    return static_cast<uint64_t>(ms.time_since_epoch().count());
}

TransportConfig makeLocalPublishTransport()
{
    TransportConfig config;
    config.bindAddress = LStringLiteral("127.0.0.1");
    config.bindPort = 26102;
    config.remoteAddress = LStringLiteral("127.0.0.1");
    config.remotePort = 26101;
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
      1. 先用 lidl 生成并安装 file1/file2:
         .\bin\LIdl.exe -V .\bin\lidl\file2.lidl
      2. 头文件直接包含安装后的 topic 头:
         #include "file1_topic.h"
         #include "file2_topic.h"
      3. 应用代码只写结构体赋值和 publish(topicKey, object.get())。
      4. 这里的 TransportConfig 仅用于同机自测。
         跨机器部署时，可以改为 publisher.initialize()，
         然后通过 qos.xml + ddsRely.xml 统一控制域、QoS 和运行时模块加载。
    */

    LDds publisher;
    publisher.setLogCallback(
        [](const std::string & line) {
            std::cerr << "[pub][ldds] " << line << std::endl;
        });
    if (!publisher.initialize(makeLocalPublishTransport()))
    {
        std::cerr << "[pub] initialize failed error=" << publisher.getLastError() << std::endl;
        return EXIT_FAILURE;
    }

    /*
      如果 ddsRely.xml 已经正确加载 file1/file2，对应类型会在运行时自动注册。
      为了让示例在本机调试时更直接，这里额外显式注册一次。
      业务接入时可以二选一:
      1. 只依赖 ddsRely.xml 自动注册
      2. 在进程启动时手工调用 registerFile1Types/registerFile2Types
    */
    if (!registerLocalGeneratedTypes(publisher))
    {
        std::cerr << "[pub] register generated types failed" << std::endl;
        return EXIT_FAILURE;
    }

    /*
      file1 里既有 Handle，也有更复杂的 Param1。
      为了让自测链路更稳定，这里先发布最简单的 Handle。
      如果业务需要字符串、数组字段，可以直接改成 P1::Param1 并按同样方式 publish。
    */
    P1::P2::Handle handle;
    handle.handle = 1001;
    handle.datatime = static_cast<int64_t>(nowTimestampMs());

    P3::TestParam testParam;
    testParam.handle = 2001;
    testParam.datatime = static_cast<int64_t>(nowTimestampMs());
    testParam.a = 88;

    bool handlePublished = false;
    bool testParamPublished = false;

    for (int attempt = 0; attempt < 20; ++attempt)
    {
        if (!handlePublished)
        {
            handlePublished = publisher.publish(FILE1_TOPIC_KEY_HANDLE_TOPIC, handle.get());
        }
        if (!testParamPublished)
        {
            testParamPublished = publisher.publish(FILE2_TOPIC_KEY_TESTPARAM_TOPIC, testParam.get());
        }
        if (handlePublished && testParamPublished)
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!handlePublished || !testParamPublished)
    {
        std::cerr << "[pub] publish failed error=" << publisher.getLastError() << std::endl;
        publisher.shutdown();
        return EXIT_FAILURE;
    }

    std::cout << "[pub] result=ok"
              << " handleTopic=" << FILE1_TOPIC_KEY_HANDLE_TOPIC
              << " testParamTopic=" << FILE2_TOPIC_KEY_TESTPARAM_TOPIC
              << " handle.handle=" << handle.handle
              << " testParam.a=" << testParam.a << std::endl;

    publisher.shutdown();
    return EXIT_SUCCESS;
}
