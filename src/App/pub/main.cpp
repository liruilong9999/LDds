#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

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

} // namespace

int main(int argc, char * argv[])
{
    (void)argc;
    (void)argv;

    /*
      Usage:
      1. Generate and install file1/file2 first:
         .\bin\LIdl.exe -V .\bin\lidl\file2.lidl
      2. Do not set domainId, ports, or qos in code.
         LDdsCore loads them from bin/config/qos.xml.
      3. Do not manually load file1/file2 dll in code.
         LDdsCore loads them from bin/config/ddsRely.xml.
      4. Use the process singleton directly:
         initialize();
         publish(topicKey, object.get());
         shutdown();
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
            handlePublished = publish(FILE1_TOPIC_KEY_HANDLE_TOPIC, handle.get());
        }
        if (!testParamPublished)
        {
            testParamPublished = publish(FILE2_TOPIC_KEY_TESTPARAM_TOPIC, testParam.get());
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
              << " handleTopic=" << FILE1_TOPIC_KEY_HANDLE_TOPIC
              << " testParamTopic=" << FILE2_TOPIC_KEY_TESTPARAM_TOPIC
              << " handle.handle=" << handle.handle
              << " testParam.a=" << testParam.a << std::endl;

    shutdown();
    return EXIT_SUCCESS;
}
