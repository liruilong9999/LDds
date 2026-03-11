#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

#include "LDds.h"
#include "file1_topic.h"
#include "file2_topic.h"

using namespace LDdsFramework;

int main(int argc, char * argv[])
{
    (void)argc;
    (void)argv;

    /*
      Usage:
      1. Generate and install file1/file2 first:
         .\bin\LIdl.exe -V .\bin\lidl\file2.lidl
      2. Do not set ports or domain in code.
         initialize() reads bin/config/qos.xml.
      3. Do not manually deserialize in main.
         sub(topicKey) + getFirstData<T>() returns the generated type directly.
      4. Use the process singleton directly:
         initialize();
         LFindSet * set = sub(topicKey);
         shutdown();
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
            LFindSet * handleSet = sub(FILE1_TOPIC_KEY_HANDLE_TOPIC);
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
            LFindSet * testParamSet = sub(FILE2_TOPIC_KEY_TESTPARAM_TOPIC);
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
                  << " handleTopic=" << FILE1_TOPIC_KEY_HANDLE_TOPIC
                  << " testParamTopic=" << FILE2_TOPIC_KEY_TESTPARAM_TOPIC
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
