#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

#include "LDds.h"
#include "LCoreRuntime_topic.h"
#include "LTopType_topic.h"

using namespace LDdsFramework;

int main(int argc, char * argv[])
{
    (void)argc;
    (void)argv;

    /*
      用法:
      1. 先执行:
         .\bin\LIdl.exe -V .\bin\lidl\LCoreRuntime.lidl
      2. 运行时参数全部来自 qos.xml 和 ddsRely.xml
      3. sub(topicKey) + getFirstData<T>() 直接拿生成结构体，不需要手写反序列化。
    */

    dds().setLogCallback(
        [](const std::string & line) {
            std::cerr << "[sub][ldds] " << line << std::endl;
        });

    if (!initialize())
    {
        std::cerr << "[sub] initialize failed error=" << dds().getLastError() << std::endl;
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
            LFindSet * handleSet = sub(LTOPTYPE_TOPIC_KEY_HANDLE_TOPIC);
            if (handleSet != nullptr)
            {
                P1::P2::Handle * data = handleSet->getFirstData<P1::P2::Handle>();
                if (data != nullptr)
                {
                    receivedHandle = *data;
                    gotHandle = true;
                }
            }
        }

        if (!gotTestParam)
        {
            LFindSet * testParamSet = sub(LCORERUNTIME_TOPIC_KEY_TESTPARAM_TOPIC);
            if (testParamSet != nullptr)
            {
                P3::TestParam * data = testParamSet->getFirstData<P3::TestParam>();
                if (data != nullptr)
                {
                    receivedTestParam = *data;
                    gotTestParam = true;
                }
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
                  << " handleTopic=" << LTOPTYPE_TOPIC_KEY_HANDLE_TOPIC
                  << " testParamTopic=" << LCORERUNTIME_TOPIC_KEY_TESTPARAM_TOPIC
                  << " error=" << dds().getLastError() << std::endl;
        shutdown();
        return EXIT_FAILURE;
    }

    std::cout << "[sub] result=ok"
              << " handle.handle=" << receivedHandle.handle
              << " handle.datatime=" << receivedHandle.datatime
              << " testParam.handle=" << receivedTestParam.handle
              << " testParam.a=" << receivedTestParam.a << std::endl;

    shutdown();
    return EXIT_SUCCESS;
}
