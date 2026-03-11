#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

#include "LDds.h"
#include "LCoreRuntime_topic.h"
#include "LTopType_topic.h"

using namespace LDdsFramework;

namespace {

uint64_t nowTimestampMs()
{
    const auto now = std::chrono::system_clock::now();
    const auto ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
    return static_cast<uint64_t>(ms.time_since_epoch().count());
}

} // namespace

int main(int argc, char * argv[])
{
    (void)argc;
    (void)argv;

    /*
      用法:
      1. 先执行:
         .\bin\LIdl.exe -V .\bin\lidl\LCoreRuntime.lidl
      2. 运行时参数全部来自:
         - bin/config/qos.xml
         - bin/config/ddsRely.xml
      3. main 里不需要构造 LDds，也不需要手写序列化。
    */

    dds().setLogCallback(
        [](const std::string & line) {
            std::cerr << "[pub][ldds] " << line << std::endl;
        });

    if (!initialize())
    {
        std::cerr << "[pub] initialize failed error=" << dds().getLastError() << std::endl;
        return EXIT_FAILURE;
    }

    P1::P2::Handle handle;
    handle.handle = 1001;
    handle.datatime = static_cast<int64_t>(nowTimestampMs());

    P3::TestParam testParam;
    testParam.handle = 2001;
    testParam.datatime = static_cast<int64_t>(nowTimestampMs());
    testParam.a = 88;

    bool handlePublished = false;
    bool testParamPublished = false;

    for (int attempt = 0; attempt < 30; ++attempt)
    {
        if (!handlePublished)
        {
            handlePublished = publish(LTOPTYPE_TOPIC_KEY_HANDLE_TOPIC, handle.get());
        }
        if (!testParamPublished)
        {
            testParamPublished = publish(LCORERUNTIME_TOPIC_KEY_TESTPARAM_TOPIC, testParam.get());
        }
        if (handlePublished && testParamPublished)
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    if (!handlePublished || !testParamPublished)
    {
        std::cerr << "[pub] publish failed error=" << dds().getLastError() << std::endl;
        shutdown();
        return EXIT_FAILURE;
    }

    std::cout << "[pub] result=ok"
              << " handleTopic=" << LTOPTYPE_TOPIC_KEY_HANDLE_TOPIC
              << " testParamTopic=" << LCORERUNTIME_TOPIC_KEY_TESTPARAM_TOPIC
              << " handle.handle=" << handle.handle
              << " testParam.a=" << testParam.a << std::endl;

    shutdown();
    return EXIT_SUCCESS;
}
