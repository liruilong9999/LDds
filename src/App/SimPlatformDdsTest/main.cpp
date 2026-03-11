#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "LDds.h"
#include "LDdsRecordReplay.h"
#include "LCoreRuntime_topic.h"
#include "LTopType_topic.h"

using namespace LDdsFramework;

namespace {

std::filesystem::path executablePath()
{
#ifdef _WIN32
    std::vector<char> buffer(static_cast<size_t>(MAX_PATH), '\0');
    DWORD length = GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    while (length >= buffer.size())
    {
        buffer.resize(buffer.size() * 2U, '\0');
        length = GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    }
    return std::filesystem::path(std::string(buffer.data(), length));
#else
    return std::filesystem::current_path() / "SimPlatformDdsTest";
#endif
}

std::filesystem::path repoRoot()
{
    return executablePath().parent_path().parent_path();
}

void fail(const std::string & message)
{
    std::cerr << "[SimPlatformDdsTest] FAIL: " << message << std::endl;
    std::exit(EXIT_FAILURE);
}

void check(bool condition, const std::string & message)
{
    if (!condition)
    {
        fail(message);
    }
}

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

    const std::filesystem::path root = repoRoot();
    const std::filesystem::path qosPath = root / "bin" / "config" / "qos.xml";
    const std::filesystem::path relyPath = root / "bin" / "config" / "ddsRely.xml";

    DdsInitOptions subOptions;
    subOptions.qosFile = qosPath.string();
    subOptions.relyFile = relyPath.string();
    subOptions.domainId = 0;
    subOptions.sourceApp = "sim-sub";
    subOptions.runId = "case-001";
    subOptions.transportConfig.bindAddress = LStringLiteral("127.0.0.1");
    subOptions.transportConfig.bindPort = 26201;
    subOptions.transportConfig.enableDiscovery = false;
    subOptions.transportConfig.enableDomainPortMapping = false;

    DdsInitOptions pubOptions = subOptions;
    pubOptions.sourceApp = "sim-pub";
    pubOptions.runId = "case-001";
    pubOptions.transportConfig.bindPort = 26202;
    pubOptions.transportConfig.remoteAddress = LStringLiteral("127.0.0.1");
    pubOptions.transportConfig.remotePort = 26201;

    std::shared_ptr<LDdsContext> subscriber = createContext(subOptions);
    std::shared_ptr<LDdsContext> publisher = createContext(pubOptions);

    check(subscriber != nullptr, "createContext(subscriber) returned null");
    check(publisher != nullptr, "createContext(publisher) returned null");

    check(subscriber->initialize(), "subscriber context initialize failed");
    check(publisher->initialize(), "publisher context initialize failed");

    DdsTopicInfo handleInfo;
    DdsTopicInfo testParamInfo;
    check(subscriber->getTopicInfo(LTOPTYPE_TOPIC_KEY_HANDLE_TOPIC, handleInfo), "handle topic info missing");
    check(subscriber->getTopicInfo(LCORERUNTIME_TOPIC_KEY_TESTPARAM_TOPIC, testParamInfo), "testParam topic info missing");
    check(handleInfo.typeName == "P1::P2::Handle", "handle topic typeName mismatch");
    check(handleInfo.moduleName == "LTopType", "handle topic moduleName mismatch");
    check(handleInfo.version == "1.0", "handle topic version mismatch");
    check(testParamInfo.typeName == "P3::TestParam", "testParam topic typeName mismatch");
    check(testParamInfo.moduleName == "LCoreRuntime", "testParam topic moduleName mismatch");
    check(testParamInfo.version == "1.0", "testParam topic version mismatch");

    TopicQosOverride topicQos;
    check(subscriber->dds().getTopicQos(LTOPTYPE_TOPIC_KEY_HANDLE_TOPIC, topicQos), "topic qos override missing");
    check(topicQos.historyDepth > 0, "topic qos historyDepth not resolved");

    const std::vector<DdsTopicInfo> allTopics = subscriber->listTopicInfos();
    check(allTopics.size() >= 3U, "topic info list size too small");

    P1::P2::Handle handle{};
    handle.handle = 1001;
    handle.datatime = static_cast<int64_t>(nowTimestampMs());

    std::vector<uint8_t> handleBytes;
    check(publisher->serializeTopic(LTOPTYPE_TOPIC_KEY_HANDLE_TOPIC, handle, handleBytes), "serialize handle failed");
    P1::P2::Handle decodedHandle{};
    check(publisher->deserializeTopic(LTOPTYPE_TOPIC_KEY_HANDLE_TOPIC, handleBytes, decodedHandle), "deserialize handle failed");
    check(decodedHandle.handle == handle.handle, "deserialize handle.handle mismatch");
    check(decodedHandle.datatime == handle.datatime, "deserialize handle.datatime mismatch");

    P3::TestParam testParam{};
    testParam.handle = 2001;
    testParam.datatime = static_cast<int64_t>(nowTimestampMs());
    testParam.a = 88;

    DdsPublishOptions publishOptions;
    publishOptions.simTimestamp = 123456789ULL;
    publishOptions.sourceApp = "sim-pub";
    publishOptions.runId = "run-42";

    check(publisher->publish(LTOPTYPE_TOPIC_KEY_HANDLE_TOPIC, handle.get(), publishOptions), "publish handle failed");
    check(publisher->publish(LCORERUNTIME_TOPIC_KEY_TESTPARAM_TOPIC, testParam.get(), publishOptions), "publish testParam failed");

    DdsCursor handleCursor;
    DdsCursor testParamCursor;
    P1::P2::Handle receivedHandle{};
    P3::TestParam receivedTestParam{};
    DdsSampleMetadata handleMetadata;
    DdsSampleMetadata testParamMetadata;

    bool gotHandle = false;
    bool gotTestParam = false;
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        if (!gotHandle)
        {
            gotHandle = subscriber->readNext(
                LTOPTYPE_TOPIC_KEY_HANDLE_TOPIC,
                handleCursor,
                receivedHandle,
                &handleMetadata);
        }

        if (!gotTestParam)
        {
            gotTestParam = subscriber->readNext(
                LCORERUNTIME_TOPIC_KEY_TESTPARAM_TOPIC,
                testParamCursor,
                receivedTestParam,
                &testParamMetadata);
        }

        if (gotHandle && gotTestParam)
        {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    check(gotHandle, "did not receive handle by cursor");
    check(gotTestParam, "did not receive testParam by cursor");
    check(receivedHandle.handle == handle.handle, "received handle.handle mismatch");
    check(receivedTestParam.a == testParam.a, "received testParam.a mismatch");
    check(handleMetadata.sequence > 0, "handle metadata.sequence missing");
    check(handleMetadata.simTimestamp == publishOptions.simTimestamp, "handle metadata.simTimestamp mismatch");
    check(handleMetadata.sourceApp == publishOptions.sourceApp, "handle metadata.sourceApp mismatch");
    check(handleMetadata.runId == publishOptions.runId, "handle metadata.runId mismatch");
    check(testParamMetadata.sequence > handleMetadata.sequence, "testParam metadata.sequence order mismatch");

    for (int index = 0; index < 2; ++index)
    {
        P1::P2::Handle batchHandle{};
        batchHandle.handle = 3000 + index;
        batchHandle.datatime = static_cast<int64_t>(nowTimestampMs());

        DdsPublishOptions batchOptions;
        batchOptions.simTimestamp = 5000 + static_cast<uint64_t>(index);
        batchOptions.sourceApp = "sim-batch";
        batchOptions.runId = "run-batch";

        check(publisher->publish(LTOPTYPE_TOPIC_KEY_HANDLE_TOPIC, batchHandle.get(), batchOptions), "batch publish failed");
    }

    std::vector<P1::P2::Handle> batchObjects;
    std::vector<DdsSampleMetadata> batchMetadata;
    for (int attempt = 0; attempt < 50 && batchObjects.size() < 2U; ++attempt)
    {
        batchObjects.clear();
        batchMetadata.clear();
        subscriber->readBatch(
            LTOPTYPE_TOPIC_KEY_HANDLE_TOPIC,
            handleCursor,
            batchObjects,
            &batchMetadata);
        if (batchObjects.size() >= 2U)
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    check(batchObjects.size() >= 2U, "readBatch did not return two handle samples");
    check(batchMetadata.size() == batchObjects.size(), "batch metadata size mismatch");
    check(batchObjects.front().handle == 3000, "batch first handle mismatch");
    check(batchObjects.back().handle == 3001, "batch second handle mismatch");
    check(batchMetadata.front().sourceApp == "sim-batch", "batch metadata sourceApp mismatch");

    const std::filesystem::path recordFile = root / "build" / "sim_platform_record.ldds";
    DdsRecorder recorder;
    std::string recordError;
    check(recorder.open(recordFile.string(), &recordError), "recorder open failed: " + recordError);
    check(recorder.record(handleInfo, handleMetadata, handleBytes, &recordError), "recorder write failed: " + recordError);
    recorder.close();

    DdsPlaybackParser playbackParser;
    check(playbackParser.open(recordFile.string(), &recordError), "playback open failed: " + recordError);
    DdsRecordedSample recordedSample;
    check(playbackParser.readNext(recordedSample, &recordError), "playback read failed: " + recordError);
    check(recordedSample.topicInfo.topicKey == LTOPTYPE_TOPIC_KEY_HANDLE_TOPIC, "playback topicKey mismatch");
    check(recordedSample.metadata.sourceApp == publishOptions.sourceApp, "playback metadata sourceApp mismatch");
    P1::P2::Handle replayHandle{};
    check(subscriber->deserializeTopic(recordedSample.topicInfo.topicKey, recordedSample.payload, replayHandle), "playback deserialize failed");
    check(replayHandle.handle == handle.handle, "playback handle mismatch");
    playbackParser.close();

    std::vector<DdsRecordIndexEntry> indexEntries;
    check(DdsRecordIndexBuilder::build(recordFile.string(), indexEntries, &recordError), "index build failed: " + recordError);
    check(indexEntries.size() == 1U, "index entry count mismatch");
    check(indexEntries.front().topicKey == LTOPTYPE_TOPIC_KEY_HANDLE_TOPIC, "index topicKey mismatch");

    subscriber->shutdown();
    publisher->shutdown();

    std::cout << "[SimPlatformDdsTest] PASS" << std::endl;
    return EXIT_SUCCESS;
}
