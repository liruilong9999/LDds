#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "LByteBuffer.h"
#include "LDomain.h"
#include "LDds.h"
#include "LIdlParser.h"
#include "LMessage.h"
#include "LQos.h"
#include "LTypeRegistry.h"
#include "App/idl_generated/pubsub_demo_topic.h"

using namespace LDdsFramework;

namespace {

struct TestContext
{
    std::vector<std::string> failures;

    void expect(bool condition, const std::string & message)
    {
        if (!condition)
        {
            failures.push_back(message);
        }
    }

    template<typename T1, typename T2>
    void expectEqual(const T1 & lhs, const T2 & rhs, const std::string & message)
    {
        if (!(lhs == rhs))
        {
            failures.push_back(message);
        }
    }
};

using TestFn = std::function<void(TestContext &)>;

struct TestCase
{
    std::string name;
    TestFn fn;
};

struct TcpStressSample
{
    uint32_t seq;
    uint32_t checksum;
};

static_assert(
    std::is_trivially_copyable<TcpStressSample>::value,
    "TcpStressSample must be trivially copyable");

uint32_t makeSampleChecksum(uint32_t seq) noexcept
{
    return (seq * 2654435761U) ^ 0x9E3779B9U;
}

bool runTcpBurstRound(
    TestContext & ctx,
    DomainId domainId,
    uint32_t topic,
    quint16 receiverPort,
    quint16 senderPort,
    int messageCount,
    int timeoutMs,
    const std::string & roundTag)
{
    LQos qos;
    qos.transportType = TransportType::TCP;
    qos.reliable = true;
    qos.historyDepth = 16;
    qos.domainId = static_cast<uint8_t>(domainId);

    TransportConfig receiverConfig;
    receiverConfig.bindAddress = LStringLiteral("127.0.0.1");
    receiverConfig.bindPort = receiverPort;
    receiverConfig.enableDiscovery = false;
    receiverConfig.enableDomainPortMapping = false;

    TransportConfig senderConfig;
    senderConfig.bindAddress = LStringLiteral("127.0.0.1");
    senderConfig.bindPort = senderPort;
    senderConfig.remoteAddress = LStringLiteral("127.0.0.1");
    senderConfig.remotePort = receiverPort;
    senderConfig.enableDiscovery = false;
    senderConfig.enableDomainPortMapping = false;

    LDds receiver;
    LDds sender;
    const bool receiverInit = receiver.initialize(qos, receiverConfig, domainId);
    const bool senderInit = sender.initialize(qos, senderConfig, domainId);
    ctx.expect(receiverInit, roundTag + " receiver initialize failed: " + receiver.getLastError());
    ctx.expect(senderInit, roundTag + " sender initialize failed: " + sender.getLastError());
    if (!receiverInit || !senderInit)
    {
        sender.shutdown();
        receiver.shutdown();
        return false;
    }

    const std::string typeName = "stress::TcpStressSample";
    const bool receiverRegistered = receiver.registerType<TcpStressSample>(typeName, topic);
    const bool senderRegistered = sender.registerType<TcpStressSample>(typeName, topic);
    ctx.expect(receiverRegistered, roundTag + " receiver registerType failed");
    ctx.expect(senderRegistered, roundTag + " sender registerType failed");
    if (!receiverRegistered || !senderRegistered)
    {
        sender.shutdown();
        receiver.shutdown();
        return false;
    }

    std::atomic<int> receivedCount(0);
    std::atomic<int> invalidCount(0);
    std::mutex waitMutex;
    std::condition_variable waitCv;

    receiver.subscribeTopic<TcpStressSample>(
        topic,
        [&](const TcpStressSample & sample) {
            if (sample.checksum != makeSampleChecksum(sample.seq))
            {
                invalidCount.fetch_add(1);
            }
            const int current = receivedCount.fetch_add(1) + 1;
            if (current >= messageCount || invalidCount.load() > 0)
            {
                waitCv.notify_all();
            }
        });

    std::this_thread::sleep_for(std::chrono::milliseconds(350));

    int publishFailures = 0;
    for (int i = 0; i < messageCount; ++i)
    {
        TcpStressSample sample{};
        sample.seq = static_cast<uint32_t>(i);
        sample.checksum = makeSampleChecksum(sample.seq);

        bool sent = false;
        for (int attempt = 0; attempt < 30; ++attempt)
        {
            if (sender.publishTopicByTopic<TcpStressSample>(topic, sample))
            {
                sent = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (!sent)
        {
            ++publishFailures;
        }
    }

    bool allReceived = false;
    {
        std::unique_lock<std::mutex> lock(waitMutex);
        allReceived = waitCv.wait_for(
            lock,
            std::chrono::milliseconds(timeoutMs),
            [&] {
                return receivedCount.load() >= messageCount || invalidCount.load() > 0;
            });
    }

    const int finalReceived = receivedCount.load();
    const int finalInvalid = invalidCount.load();
    sender.shutdown();
    receiver.shutdown();

    ctx.expect(publishFailures == 0, roundTag + " publish failures=" + std::to_string(publishFailures));
    ctx.expect(allReceived, roundTag + " timed out waiting all messages");
    ctx.expect(finalReceived == messageCount,
               roundTag + " received mismatch expected=" + std::to_string(messageCount) +
                   " actual=" + std::to_string(finalReceived));
    ctx.expect(finalInvalid == 0,
               roundTag + " invalid payload count=" + std::to_string(finalInvalid));
    return (publishFailures == 0) && allReceived && (finalReceived == messageCount) && (finalInvalid == 0);
}

void testByteBufferReadWrite(TestContext & ctx)
{
    LByteBuffer buffer;
    buffer.writeUInt32(0x12345678U);
    buffer.writeUInt64(0x1122334455667788ULL);
    const uint8_t bytes[3] = {0xAAU, 0xBBU, 0xCCU};
    buffer.writeBytes(bytes, sizeof(bytes));

    ctx.expectEqual(buffer.size(), static_cast<size_t>(15), "buffer size mismatch");

    buffer.setReadPos(0);
    ctx.expectEqual(buffer.readUInt32(), 0x12345678U, "readUInt32 mismatch");
    ctx.expectEqual(buffer.readUInt64(), 0x1122334455667788ULL, "readUInt64 mismatch");

    uint8_t readBytes[3] = {0U, 0U, 0U};
    buffer.readBytes(readBytes, sizeof(readBytes));
    ctx.expect(readBytes[0] == 0xAAU && readBytes[1] == 0xBBU && readBytes[2] == 0xCCU,
               "readBytes mismatch");
}

void testByteBufferBounds(TestContext & ctx)
{
    LByteBuffer buffer;
    buffer.writeUInt32(7U);
    buffer.setReadPos(0);
    (void)buffer.readUInt32();

    bool threw = false;
    try
    {
        (void)buffer.readUInt32();
    }
    catch (const std::out_of_range &)
    {
        threw = true;
    }
    ctx.expect(threw, "read beyond boundary should throw std::out_of_range");
}

void testByteBufferLargePayloadStress(TestContext & ctx)
{
    constexpr size_t payloadSize = 1024U * 1024U;
    constexpr int rounds = 12;

    std::vector<uint8_t> payload(payloadSize);
    uint32_t seed = 0x13572468U;
    for (size_t i = 0; i < payloadSize; ++i)
    {
        seed = seed * 1664525U + 1013904223U;
        payload[i] = static_cast<uint8_t>(seed & 0xFFU);
    }

    for (int r = 0; r < rounds; ++r)
    {
        LByteBuffer buffer(8);
        buffer.writeUInt32(0xABCDEF01U);
        buffer.writeUInt64(0x1122334455667788ULL + static_cast<uint64_t>(r));
        buffer.writeBytes(payload.data(), payload.size());

        ctx.expectEqual(
            buffer.size(),
            sizeof(uint32_t) + sizeof(uint64_t) + payloadSize,
            "large payload buffer size mismatch");

        buffer.setReadPos(0);
        const uint32_t magic = buffer.readUInt32();
        const uint64_t stamp = buffer.readUInt64();
        std::vector<uint8_t> out(payloadSize, 0U);
        buffer.readBytes(out.data(), out.size());

        ctx.expectEqual(magic, 0xABCDEF01U, "large payload magic mismatch");
        ctx.expectEqual(stamp, 0x1122334455667788ULL + static_cast<uint64_t>(r),
                        "large payload stamp mismatch");
        ctx.expect(out == payload, "large payload content mismatch");
    }
}

void testMessageRoundTrip(TestContext & ctx)
{
    const std::vector<uint8_t> payload = {1U, 2U, 3U, 4U, 5U};
    LMessage original(1001U, 77U, payload);
    original.setDomainId(9U);
    original.setMessageType(LMessageType::Data);
    original.setWriterId(1234U);
    original.setFirstSeq(70U);
    original.setLastSeq(77U);
    original.setAckSeq(68U);
    original.setWindowStart(65U);
    original.setWindowSize(32U);

    const LByteBuffer serialized = original.serialize();
    LMessage decoded;
    ctx.expect(decoded.deserialize(serialized), "deserialize from serialized buffer failed");
    ctx.expectEqual(decoded.getTopic(), original.getTopic(), "topic mismatch");
    ctx.expectEqual(decoded.getSequence(), original.getSequence(), "sequence mismatch");
    ctx.expectEqual(decoded.getDomainId(), original.getDomainId(), "domainId mismatch");
    ctx.expectEqual(decoded.getMessageType(), original.getMessageType(), "messageType mismatch");
    ctx.expectEqual(decoded.getWriterId(), original.getWriterId(), "writerId mismatch");
    ctx.expectEqual(decoded.getFirstSeq(), original.getFirstSeq(), "firstSeq mismatch");
    ctx.expectEqual(decoded.getLastSeq(), original.getLastSeq(), "lastSeq mismatch");
    ctx.expectEqual(decoded.getAckSeq(), original.getAckSeq(), "ackSeq mismatch");
    ctx.expectEqual(decoded.getWindowStart(), original.getWindowStart(), "windowStart mismatch");
    ctx.expectEqual(decoded.getWindowSize(), original.getWindowSize(), "windowSize mismatch");
    ctx.expect(decoded.getPayload() == payload, "payload mismatch");
}

void testMessageAckFactory(TestContext & ctx)
{
    LMessage ack = LMessage::makeAck(42U, 888U, 777U, 16U);
    ack.setDomainId(3U);
    ack.setSequence(999U);

    ctx.expectEqual(ack.getMessageType(), LMessageType::Ack, "ack message type mismatch");
    ctx.expectEqual(ack.getTopic(), HEARTBEAT_TOPIC_ID, "ack topic mismatch");
    ctx.expectEqual(ack.getWriterId(), 42U, "ack writerId mismatch");
    ctx.expectEqual(ack.getAckSeq(), 888U, "ack ackSeq mismatch");
    ctx.expectEqual(ack.getWindowStart(), 777U, "ack windowStart mismatch");
    ctx.expectEqual(ack.getWindowSize(), 16U, "ack windowSize mismatch");

    LMessage decoded;
    ctx.expect(decoded.deserialize(ack.serialize()), "ack deserialize failed");
    ctx.expectEqual(decoded.getMessageType(), LMessageType::Ack, "decoded ack type mismatch");
    ctx.expectEqual(decoded.getAckSeq(), 888U, "decoded ackSeq mismatch");
}

void testMessageDeserializeFuzzStress(TestContext & ctx)
{
    constexpr int fuzzCases = 20000;
    std::mt19937 rng(0xC0FFEEU);
    std::uniform_int_distribution<int> sizeDist(0, 96);
    std::uniform_int_distribution<int> byteDist(0, 255);

    int trueCount = 0;
    int falseCount = 0;

    for (int i = 0; i < fuzzCases; ++i)
    {
        const size_t size = static_cast<size_t>(sizeDist(rng));
        std::vector<uint8_t> bytes(size, 0U);
        for (size_t j = 0; j < size; ++j)
        {
            bytes[j] = static_cast<uint8_t>(byteDist(rng));
        }

        LMessage message;
        bool ok = false;
        try
        {
            ok = message.deserialize(bytes.data(), bytes.size());
        }
        catch (const std::exception & ex)
        {
            ctx.expect(false, std::string("deserialize should not throw, got: ") + ex.what());
            return;
        }
        catch (...)
        {
            ctx.expect(false, "deserialize should not throw unknown exception");
            return;
        }

        if (ok)
        {
            ++trueCount;
        }
        else
        {
            ++falseCount;
        }
    }

    ctx.expect(falseCount > 0, "fuzz should produce invalid messages");

    LMessage valid(1001U, 123U, std::vector<uint8_t>{1U, 2U, 3U, 4U});
    const LByteBuffer encoded = valid.serialize();
    std::vector<uint8_t> broken(encoded.data(), encoded.data() + encoded.size());
    if (broken.size() >= 4)
    {
        broken[0] = 0U;
        broken[1] = 0U;
        broken[2] = 0U;
        broken[3] = 0U;
    }

    LMessage decoded;
    ctx.expect(!decoded.deserialize(broken.data(), broken.size()),
               "tampered totalSize should fail deserialize");
}

void testQosXmlLoad(TestContext & ctx)
{
    const std::string xml =
        "<qos transportType=\"tcp\" historyDepth=\"8\" deadlineMs=\"1500\" "
        "reliable=\"true\" domainId=\"7\" enableDomainPortMapping=\"true\" "
        "basePort=\"22000\" domainPortOffset=\"12\" enableMetrics=\"true\" "
        "metricsPort=\"9100\" metricsBindAddress=\"0.0.0.0\">"
        "  <durability kind=\"persistent\" dbPath=\"build/test_qos.sqlite\"/>"
        "  <ownership kind=\"exclusive\" strength=\"3\"/>"
        "  <security enabled=\"true\" encryptPayload=\"true\" psk=\"abc123\"/>"
        "</qos>";

    LQos qos;
    std::string error;
    ctx.expect(qos.loadFromXmlString(xml, &error), "loadFromXmlString failed: " + error);
    ctx.expectEqual(qos.transportType, TransportType::TCP, "transportType mismatch");
    ctx.expectEqual(qos.historyDepth, 8, "historyDepth mismatch");
    ctx.expectEqual(qos.deadlineMs, 1500, "deadlineMs mismatch");
    ctx.expect(qos.reliable, "reliable should be true");
    ctx.expectEqual(static_cast<uint32_t>(qos.domainId), 7U, "domainId mismatch");
    ctx.expect(qos.enableDomainPortMapping, "enableDomainPortMapping should be true");
    ctx.expectEqual(qos.basePort, static_cast<uint16_t>(22000), "basePort mismatch");
    ctx.expectEqual(qos.domainPortOffset, static_cast<uint16_t>(12), "domainPortOffset mismatch");
    ctx.expectEqual(qos.durabilityKind, DurabilityKind::Persistent, "durabilityKind mismatch");
    ctx.expectEqual(qos.ownershipKind, OwnershipKind::Exclusive, "ownershipKind mismatch");
    ctx.expectEqual(qos.ownershipStrength, 3, "ownershipStrength mismatch");
    ctx.expect(qos.securityEnabled, "securityEnabled should be true");
    ctx.expect(qos.securityEncryptPayload, "securityEncryptPayload should be true");
    ctx.expectEqual(qos.securityPsk, std::string("abc123"), "securityPsk mismatch");

    std::string validateError;
    ctx.expect(qos.validate(validateError), "qos validate failed: " + validateError);
}

void testQosCompatibility(TestContext & ctx)
{
    LQos lhs;
    LQos rhs;
    lhs.domainId = 10U;
    rhs.domainId = 11U;

    std::string compatibilityError;
    const bool compatible = lhs.isCompatibleWith(rhs, compatibilityError);
    ctx.expect(!compatible, "different domainId should be incompatible");
    ctx.expect(compatibilityError.find("domainId") != std::string::npos,
               "compatibility error should mention domainId");
}

void testQosBoundaryValidation(TestContext & ctx)
{
    {
        LQos qos;
        qos.historyDepth = 0;
        std::string error;
        ctx.expect(!qos.validate(error), "historyDepth=0 should fail validate");
        ctx.expect(error.find("historyDepth") != std::string::npos,
                   "historyDepth validate error mismatch");
    }

    {
        LQos qos;
        qos.securityEnabled = true;
        qos.securityPsk.clear();
        std::string error;
        ctx.expect(!qos.validate(error), "securityEnabled=true without psk should fail validate");
        ctx.expect(error.find("securityPsk") != std::string::npos,
                   "security validate error mismatch");
    }

    {
        LQos qos;
        qos.enableDomainPortMapping = true;
        qos.basePort = 65530;
        qos.domainPortOffset = 100;
        qos.domainId = 10;
        std::string error;
        ctx.expect(!qos.validate(error), "overflow mapped port should fail validate");
        ctx.expect(error.find("mapped port") != std::string::npos,
                   "mapped port validate error mismatch");
    }

    {
        LQos qos;
        std::string error;
        const std::string badXml = "<qos domainId=\"300\"/>";
        ctx.expect(!qos.loadFromXmlString(badXml, &error), "domainId=300 XML should fail");
    }
}

void testTypeRegistryBasic(TestContext & ctx)
{
    LTypeRegistry registry;
    ctx.expect(registry.registerType<int32_t>("example::Int32", 60001U), "registerType<int32_t> failed");
    ctx.expectEqual(registry.getTopicByTypeName("example::Int32"), 60001U, "topic lookup mismatch");
    ctx.expectEqual(registry.getTypeNameByTopic(60001U), std::string("example::Int32"), "type lookup mismatch");
    ctx.expect(registry.hasTopic(60001U), "hasTopic should be true");
    ctx.expect(registry.createByTopic(60001U) != nullptr, "createByTopic should return object");

    const int32_t value = 42;
    std::vector<uint8_t> payload;
    ctx.expect(registry.serializeByTopic(60001U, &value, payload), "serializeByTopic failed");
    ctx.expectEqual(payload.size(), sizeof(int32_t), "serialized payload size mismatch");

    int32_t decoded = 0;
    ctx.expect(registry.deserializeByTopic(60001U, payload, &decoded), "deserializeByTopic failed");
    ctx.expectEqual(decoded, value, "decoded value mismatch");

    ctx.expect(!registry.registerType<int32_t>("example::Other", 60001U),
               "same topic with different type should fail");
    ctx.expect(!registry.registerType<int32_t>("example::Int32", 60002U),
               "same type with different topic should fail");
}

void testTypeRegistryConcurrentSerializeStress(TestContext & ctx)
{
    constexpr uint32_t topic = 60011U;
    constexpr int threadCount = 8;
    constexpr int iterationsPerThread = 20000;

    LTypeRegistry registry;
    ctx.expect(registry.registerType<uint64_t>("stress::UInt64", topic),
               "registerType<uint64_t> failed");

    std::atomic<int> failures(0);
    std::vector<std::thread> threads;
    threads.reserve(threadCount);
    for (int t = 0; t < threadCount; ++t)
    {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < iterationsPerThread; ++i)
            {
                const uint64_t value =
                    (static_cast<uint64_t>(static_cast<uint32_t>(t)) << 32) |
                    static_cast<uint64_t>(static_cast<uint32_t>(i));
                std::vector<uint8_t> payload;
                if (!registry.serializeByTopic(topic, &value, payload))
                {
                    failures.fetch_add(1);
                    continue;
                }

                uint64_t decoded = 0;
                if (!registry.deserializeByTopic(topic, payload, &decoded))
                {
                    failures.fetch_add(1);
                    continue;
                }
                if (decoded != value)
                {
                    failures.fetch_add(1);
                    continue;
                }

                if (registry.createByTopic(topic) == nullptr)
                {
                    failures.fetch_add(1);
                }
            }
        });
    }
    for (auto & thread : threads)
    {
        thread.join();
    }

    ctx.expectEqual(failures.load(), 0, "concurrent serialize/deserialize has failures");
}

void testDomainCacheBehavior(TestContext & ctx)
{
    LQos qos;
    HistoryQosPolicy history = qos.getHistory();
    history.depth = 2;
    qos.setHistory(history);
    qos.historyDepth = 2;

    LDomain domain;
    ctx.expect(domain.create(9U, &qos), "domain create failed");
    ctx.expect(domain.isValid(), "domain should be valid");
    ctx.expectEqual(domain.getDomainId(), static_cast<DomainId>(9U), "domain id mismatch");

    domain.cacheTopicData(100, {1U}, "bytes");
    domain.cacheTopicData(100, {2U}, "bytes");
    domain.cacheTopicData(100, {3U}, "bytes");

    std::vector<uint8_t> latest;
    ctx.expect(domain.getTopicData(100, latest), "getTopicData failed");
    ctx.expect(latest == std::vector<uint8_t>({3U}), "latest payload mismatch");

    LFindSet findSet = domain.getFindSetByTopic(100);
    ctx.expectEqual(findSet.size(), static_cast<size_t>(2), "findSet size mismatch");
    std::vector<uint8_t> item;
    ctx.expect(findSet.getNextTopicData(item), "findSet next #1 failed");
    ctx.expect(item == std::vector<uint8_t>({3U}), "findSet first payload mismatch");
    ctx.expect(findSet.getNextTopicData(item), "findSet next #2 failed");
    ctx.expect(item == std::vector<uint8_t>({2U}), "findSet second payload mismatch");
    ctx.expect(!findSet.getNextTopicData(item), "findSet should be exhausted");

    size_t cursor = 0;
    ctx.expect(domain.getNextTopicData(100, cursor, item), "domain getNextTopicData #1 failed");
    ctx.expect(item == std::vector<uint8_t>({3U}), "domain getNextTopicData #1 payload mismatch");
    ctx.expect(domain.getNextTopicData(100, cursor, item), "domain getNextTopicData #2 failed");
    ctx.expect(item == std::vector<uint8_t>({2U}), "domain getNextTopicData #2 payload mismatch");
    ctx.expect(!domain.getNextTopicData(100, cursor, item), "domain getNextTopicData should end");

    domain.destroy();
    ctx.expect(!domain.isValid(), "domain should be invalid after destroy");
}

void testDomainConcurrentCacheStress(TestContext & ctx)
{
    constexpr int topic = 70001;
    constexpr int writerThreads = 4;
    constexpr int readerThreads = 2;
    constexpr int writesPerThread = 3000;

    LQos qos;
    HistoryQosPolicy history = qos.getHistory();
    history.depth = 128;
    qos.setHistory(history);
    qos.historyDepth = 128;

    LDomain domain;
    ctx.expect(domain.create(21U, &qos), "domain create failed in concurrent stress");
    if (!domain.isValid())
    {
        return;
    }

    std::atomic<bool> stopReaders(false);
    std::atomic<int> readerChecks(0);
    std::vector<std::thread> readers;
    readers.reserve(readerThreads);
    for (int r = 0; r < readerThreads; ++r)
    {
        readers.emplace_back([&]() {
            while (!stopReaders.load())
            {
                std::vector<uint8_t> data;
                (void)domain.getTopicData(topic, data);
                size_t cursor = 0;
                std::vector<uint8_t> item;
                while (domain.getNextTopicData(topic, cursor, item))
                {
                }
                readerChecks.fetch_add(1);
            }
        });
    }

    std::vector<std::thread> writers;
    writers.reserve(writerThreads);
    for (int w = 0; w < writerThreads; ++w)
    {
        writers.emplace_back([&, w]() {
            for (int i = 0; i < writesPerThread; ++i)
            {
                std::vector<uint8_t> payload(8U, 0U);
                payload[0] = static_cast<uint8_t>(w & 0xFF);
                payload[1] = static_cast<uint8_t>(i & 0xFF);
                payload[2] = static_cast<uint8_t>((i >> 8) & 0xFF);
                payload[3] = static_cast<uint8_t>((i >> 16) & 0xFF);
                payload[4] = static_cast<uint8_t>((i >> 24) & 0xFF);
                domain.cacheTopicData(topic, payload, "bytes");
            }
        });
    }
    for (auto & writer : writers)
    {
        writer.join();
    }

    stopReaders.store(true);
    for (auto & reader : readers)
    {
        reader.join();
    }

    LFindSet findSet = domain.getFindSetByTopic(topic);
    ctx.expect(findSet.size() <= 128U, "findSet size should not exceed history depth");
    ctx.expect(findSet.size() > 0U, "findSet size should be > 0 after writes");
    ctx.expect(readerChecks.load() > 0, "readers should have executed");

    domain.destroy();
}

void testLDdsInProcessPubSub(TestContext & ctx)
{
    constexpr DomainId domainId = 88U;
    constexpr uint32_t topic = 60100U;
    constexpr quint16 receiverPort = 26551;
    constexpr quint16 senderPort = 26552;

    LQos qos;
    qos.transportType = TransportType::UDP;
    qos.reliable = false;
    qos.historyDepth = 4;
    qos.domainId = static_cast<uint8_t>(domainId);

    TransportConfig receiverConfig;
    receiverConfig.bindAddress = LStringLiteral("127.0.0.1");
    receiverConfig.bindPort = receiverPort;
    receiverConfig.enableDiscovery = false;
    receiverConfig.enableDomainPortMapping = false;

    TransportConfig senderConfig;
    senderConfig.bindAddress = LStringLiteral("127.0.0.1");
    senderConfig.bindPort = senderPort;
    senderConfig.remoteAddress = LStringLiteral("127.0.0.1");
    senderConfig.remotePort = receiverPort;
    senderConfig.enableDiscovery = false;
    senderConfig.enableDomainPortMapping = false;

    LDds receiver;
    LDds sender;

    std::atomic<bool> got(false);
    std::mutex waitMutex;
    std::condition_variable waitCv;
    std::string receivedText;

    const bool receiverInit = receiver.initialize(qos, receiverConfig, domainId);
    const bool senderInit = sender.initialize(qos, senderConfig, domainId);
    ctx.expect(receiverInit, "receiver initialize failed: " + receiver.getLastError());
    ctx.expect(senderInit, "sender initialize failed: " + sender.getLastError());
    if (!receiverInit || !senderInit)
    {
        sender.shutdown();
        receiver.shutdown();
        return;
    }

    ctx.expect(receiver.registerType<std::vector<uint8_t>>("example::bytes", topic),
               "receiver registerType failed");
    ctx.expect(sender.registerType<std::vector<uint8_t>>("example::bytes", topic),
               "sender registerType failed");

    receiver.subscribeTopic<std::vector<uint8_t>>(
        topic,
        [&](const std::vector<uint8_t> & payload) {
            receivedText.assign(payload.begin(), payload.end());
            got.store(true);
            waitCv.notify_all();
        });

    const std::string text = "unit-test-loopback";
    const std::vector<uint8_t> payload(text.begin(), text.end());
    ctx.expect(sender.publishTopicByTopic<std::vector<uint8_t>>(topic, payload),
               "sender publishTopicByTopic failed: " + sender.getLastError());

    bool arrived = false;
    {
        std::unique_lock<std::mutex> lock(waitMutex);
        arrived = waitCv.wait_for(lock, std::chrono::seconds(3), [&] { return got.load(); });
    }

    sender.shutdown();
    receiver.shutdown();

    ctx.expect(arrived, "subscribe callback timeout");
    if (arrived)
    {
        ctx.expectEqual(receivedText, text, "received payload text mismatch");
    }
}

void testLDdsTcpBurstPubSub(TestContext & ctx)
{
    constexpr DomainId domainId = 131U;
    constexpr uint32_t topic = 60201U;
    constexpr quint16 receiverPort = 26911;
    constexpr quint16 senderPort = 26912;
    (void)runTcpBurstRound(
        ctx,
        domainId,
        topic,
        receiverPort,
        senderPort,
        600,
        12000,
        "tcp-burst-single-round");
}

void testLDdsTcpBurstMultiRound(TestContext & ctx)
{
    constexpr int rounds = 6;
    constexpr int perRoundMessages = 220;

    int succeededRounds = 0;
    for (int round = 0; round < rounds; ++round)
    {
        const DomainId domainId = static_cast<DomainId>(140U + static_cast<uint32_t>(round));
        const uint32_t topic = 60300U + static_cast<uint32_t>(round);
        const quint16 receiverPort = static_cast<quint16>(27010 + round * 3);
        const quint16 senderPort = static_cast<quint16>(receiverPort + 1);

        if (runTcpBurstRound(
                ctx,
                domainId,
                topic,
                receiverPort,
                senderPort,
                perRoundMessages,
                9000,
                "tcp-burst-round-" + std::to_string(round)))
        {
            ++succeededRounds;
        }
    }

    ctx.expectEqual(succeededRounds, rounds, "not all tcp burst rounds succeeded");
}

void testLDdsSecurityMismatchBoundary(TestContext & ctx)
{
    constexpr DomainId domainId = 151U;
    constexpr uint32_t topic = 60401U;
    constexpr quint16 receiverPort = 27151;
    constexpr quint16 senderPort = 27152;

    LQos receiverQos;
    receiverQos.transportType = TransportType::UDP;
    receiverQos.reliable = false;
    receiverQos.historyDepth = 8;
    receiverQos.domainId = static_cast<uint8_t>(domainId);
    receiverQos.securityEnabled = true;
    receiverQos.securityEncryptPayload = true;
    receiverQos.securityPsk = "receiver-secret";

    LQos senderQos = receiverQos;
    senderQos.securityPsk = "sender-secret-mismatch";

    TransportConfig receiverConfig;
    receiverConfig.bindAddress = LStringLiteral("127.0.0.1");
    receiverConfig.bindPort = receiverPort;
    receiverConfig.enableDiscovery = false;
    receiverConfig.enableDomainPortMapping = false;

    TransportConfig senderConfig;
    senderConfig.bindAddress = LStringLiteral("127.0.0.1");
    senderConfig.bindPort = senderPort;
    senderConfig.remoteAddress = LStringLiteral("127.0.0.1");
    senderConfig.remotePort = receiverPort;
    senderConfig.enableDiscovery = false;
    senderConfig.enableDomainPortMapping = false;

    LDds receiver;
    LDds sender;
    ctx.expect(receiver.initialize(receiverQos, receiverConfig, domainId),
               "security receiver initialize failed: " + receiver.getLastError());
    ctx.expect(sender.initialize(senderQos, senderConfig, domainId),
               "security sender initialize failed: " + sender.getLastError());
    if (!receiver.isRunning() || !sender.isRunning())
    {
        sender.shutdown();
        receiver.shutdown();
        return;
    }

    const std::string typeName = "stress::bytes";
    ctx.expect(receiver.registerType<std::vector<uint8_t>>(typeName, topic),
               "security receiver registerType failed");
    ctx.expect(sender.registerType<std::vector<uint8_t>>(typeName, topic),
               "security sender registerType failed");

    std::atomic<bool> got(false);
    std::mutex waitMutex;
    std::condition_variable waitCv;
    receiver.subscribeTopic<std::vector<uint8_t>>(
        topic,
        [&](const std::vector<uint8_t> &) {
            got.store(true);
            waitCv.notify_all();
        });

    const std::vector<uint8_t> payload = {1U, 3U, 5U, 7U, 9U};
    const bool publishOk = sender.publishTopicByTopic<std::vector<uint8_t>>(topic, payload);
    ctx.expect(publishOk, "security mismatch publish should still send frame");

    bool arrived = false;
    {
        std::unique_lock<std::mutex> lock(waitMutex);
        arrived = waitCv.wait_for(lock, std::chrono::seconds(2), [&] { return got.load(); });
    }

    sender.shutdown();
    receiver.shutdown();

    ctx.expect(!arrived, "security mismatch should reject payload at receiver");
}

void testIdlTopicIdDeterministic(TestContext & ctx)
{
    const std::string idlA =
        "package Demo {\n"
        "  struct SensorSample {\n"
        "    int32 id;\n"
        "  }\n"
        "  struct Status {\n"
        "    int32 code;\n"
        "  }\n"
        "}\n"
        "SENSOR_TOPIC = Demo::SensorSample;\n"
        "STATUS_TOPIC = Demo::Status;\n";

    const std::string idlB =
        "package Demo {\n"
        "  struct SensorSample {\n"
        "    int32 id;\n"
        "  }\n"
        "  struct Status {\n"
        "    int32 code;\n"
        "  }\n"
        "}\n"
        "STATUS_TOPIC = Demo::Status;\n"
        "SENSOR_TOPIC = Demo::SensorSample;\n";

    auto collectTopicMap = [&](const ParseResult & result) -> std::unordered_map<std::string, uint32_t> {
        std::unordered_map<std::string, uint32_t> output;
        auto idlFile = std::dynamic_pointer_cast<LIdlFile>(result.astRoot);
        if (!idlFile)
        {
            return output;
        }
        for (const auto & topic : idlFile->topics)
        {
            output[topic.name] = topic.id;
        }
        return output;
    };

    LIdlParser parser;
    const ParseResult resultA = parser.parseString(idlA, "inmem_a.lidl");
    ctx.expect(resultA.success, "parseString for idlA failed");
    const auto idsA = collectTopicMap(resultA);
    ctx.expect(idsA.find("SENSOR_TOPIC") != idsA.end(), "SENSOR_TOPIC missing in idlA");
    ctx.expect(idsA.find("STATUS_TOPIC") != idsA.end(), "STATUS_TOPIC missing in idlA");
    if (idsA.find("SENSOR_TOPIC") != idsA.end())
    {
        ctx.expect(idsA.at("SENSOR_TOPIC") != 0U, "SENSOR_TOPIC id should not be zero");
    }
    if (idsA.find("STATUS_TOPIC") != idsA.end())
    {
        ctx.expect(idsA.at("STATUS_TOPIC") != 0U, "STATUS_TOPIC id should not be zero");
    }
    if (idsA.find("SENSOR_TOPIC") != idsA.end() && idsA.find("STATUS_TOPIC") != idsA.end())
    {
        ctx.expect(idsA.at("SENSOR_TOPIC") != idsA.at("STATUS_TOPIC"),
                   "topic ids should be unique for different topic bindings");
    }

    const ParseResult resultB = parser.parseString(idlB, "inmem_b.lidl");
    ctx.expect(resultB.success, "parseString for idlB failed");
    const auto idsB = collectTopicMap(resultB);
    if (idsA.find("SENSOR_TOPIC") != idsA.end() && idsB.find("SENSOR_TOPIC") != idsB.end())
    {
        ctx.expectEqual(idsA.at("SENSOR_TOPIC"), idsB.at("SENSOR_TOPIC"),
                        "SENSOR_TOPIC id should be deterministic across order");
    }
    if (idsA.find("STATUS_TOPIC") != idsA.end() && idsB.find("STATUS_TOPIC") != idsB.end())
    {
        ctx.expectEqual(idsA.at("STATUS_TOPIC"), idsB.at("STATUS_TOPIC"),
                        "STATUS_TOPIC id should be deterministic across order");
    }

    const std::string idlSingle =
        "package Demo {\n"
        "  struct SensorSample {\n"
        "    int32 id;\n"
        "  }\n"
        "}\n"
        "SENSOR_TOPIC = Demo::SensorSample;\n";
    const ParseResult resultSingle = parser.parseString(idlSingle, "inmem_single.lidl");
    ctx.expect(resultSingle.success, "parseString for idlSingle failed");
    const auto idsSingle = collectTopicMap(resultSingle);
    if (idsA.find("SENSOR_TOPIC") != idsA.end() && idsSingle.find("SENSOR_TOPIC") != idsSingle.end())
    {
        ctx.expectEqual(idsA.at("SENSOR_TOPIC"), idsSingle.at("SENSOR_TOPIC"),
                        "same topic binding should map to same id even in single-file parse");
    }
}

void testIdlTopicIdExplicitAnnotation(TestContext & ctx)
{
    const std::string idl =
        "package Demo {\n"
        "  struct ExplicitTopic {\n"
        "    int32 value;\n"
        "  }\n"
        "}\n"
        "[id, 61001, \"explicit\"]\n"
        "EXPLICIT_TOPIC = Demo::ExplicitTopic;\n";

    LIdlParser parser;
    const ParseResult result = parser.parseString(idl, "inmem_explicit.lidl");
    ctx.expect(result.success, "parseString with explicit id annotation failed");

    const auto idlFile = std::dynamic_pointer_cast<LIdlFile>(result.astRoot);
    ctx.expect(idlFile != nullptr, "astRoot should be LIdlFile for explicit id parse");
    if (!idlFile || idlFile->topics.empty())
    {
        return;
    }

    ctx.expectEqual(idlFile->topics.front().id, static_cast<uint32_t>(61001U),
                    "explicit [id, N] should be used as topic id");
}

void testIdlTopicIdCollisionDetect(TestContext & ctx)
{
    const std::string idl =
        "package Demo {\n"
        "  struct A {\n"
        "    int32 value;\n"
        "  }\n"
        "  struct B {\n"
        "    int32 value;\n"
        "  }\n"
        "}\n"
        "[id, 50001]\n"
        "A_TOPIC = Demo::A;\n"
        "[id, 50001]\n"
        "B_TOPIC = Demo::B;\n";

    LIdlParser parser;
    const ParseResult result = parser.parseString(idl, "inmem_collision.lidl");
    ctx.expect(!result.success, "duplicate explicit topic id should fail parse");

    bool hasCollision = false;
    for (const auto & error : result.errors)
    {
        if (error.message.find("topic id collision") != std::string::npos)
        {
            hasCollision = true;
            break;
        }
    }
    ctx.expect(hasCollision, "collision parse result should report topic id collision");
}

void testIdlParserManyTopicsStress(TestContext & ctx)
{
    constexpr int topicCount = 180;
    std::ostringstream idl;
    idl << "package Stress {\n";
    for (int i = 0; i < topicCount; ++i)
    {
        idl << "  struct S" << i << " {\n";
        idl << "    int32 value;\n";
        idl << "  }\n";
    }
    idl << "}\n";
    for (int i = 0; i < topicCount; ++i)
    {
        idl << "TOPIC_" << i << " = Stress::S" << i << ";\n";
    }

    auto parseMap = [&](const std::string & sourceName) -> std::unordered_map<std::string, uint32_t> {
        LIdlParser parser;
        const ParseResult result = parser.parseString(idl.str(), sourceName);
        ctx.expect(result.success, "large idl parse failed for " + sourceName);
        std::unordered_map<std::string, uint32_t> ids;
        const auto file = std::dynamic_pointer_cast<LIdlFile>(result.astRoot);
        if (!file)
        {
            ctx.expect(false, "large idl astRoot cast failed for " + sourceName);
            return ids;
        }
        ctx.expectEqual(static_cast<int>(file->topics.size()), topicCount,
                        "topic count mismatch for " + sourceName);
        std::unordered_set<uint32_t> uniqueIds;
        for (const auto & topic : file->topics)
        {
            ids[topic.name] = topic.id;
            uniqueIds.insert(topic.id);
        }
        ctx.expectEqual(static_cast<int>(uniqueIds.size()), topicCount,
                        "topic id uniqueness mismatch for " + sourceName);
        return ids;
    };

    const auto idsA = parseMap("stress_many_topics_a.lidl");
    const auto idsB = parseMap("stress_many_topics_b.lidl");
    for (const auto & pair : idsA)
    {
        const auto it = idsB.find(pair.first);
        if (it == idsB.end())
        {
            ctx.expect(false, "topic missing in second parse: " + pair.first);
            continue;
        }
        ctx.expectEqual(pair.second, it->second, "topic id should be deterministic: " + pair.first);
    }
}

void testIdlTopicIdInvalidAnnotationBoundary(TestContext & ctx)
{
    const std::string idl =
        "package Demo {\n"
        "  struct X {\n"
        "    int32 value;\n"
        "  }\n"
        "}\n"
        "[id, 0]\n"
        "X_TOPIC = Demo::X;\n";

    LIdlParser parser;
    const ParseResult result = parser.parseString(idl, "inmem_invalid_id_annotation.lidl");
    ctx.expect(!result.success, "id=0 annotation should fail parse");

    bool hasInvalidIdMessage = false;
    for (const auto & error : result.errors)
    {
        if (error.message.find("invalid topic id annotation") != std::string::npos)
        {
            hasInvalidIdMessage = true;
            break;
        }
    }
    ctx.expect(hasInvalidIdMessage, "missing invalid topic id annotation error message");
}

void testGeneratedTopicNameResolveBoundary(TestContext & ctx)
{
    uint32_t topicId = 0;
    const bool resolvedId =
        LDdsFramework::tryResolvePubsubDemoTopicId(PUBSUB_DEMO_TOPIC_NAME_SENSOR_SAMPLE_TOPIC, topicId);
    ctx.expect(resolvedId, "generated resolver should resolve known topic name");
    if (resolvedId)
    {
        ctx.expectEqual(
            topicId,
            static_cast<uint32_t>(LDdsFramework::PubsubDemoTopicId::SENSOR_SAMPLE_TOPIC),
            "resolved topic id mismatch");
    }

    const char * topicName = nullptr;
    const bool resolvedName = LDdsFramework::tryResolvePubsubDemoTopicName(topicId, topicName);
    ctx.expect(resolvedName, "generated resolver should resolve known topic id");
    if (resolvedName)
    {
        ctx.expect(std::string(topicName) == PUBSUB_DEMO_TOPIC_NAME_SENSOR_SAMPLE_TOPIC,
                   "resolved topic name mismatch");
    }

    topicId = 123U;
    const bool unknownNameResolved =
        LDdsFramework::tryResolvePubsubDemoTopicId("UNKNOWN_TOPIC_NAME", topicId);
    ctx.expect(!unknownNameResolved, "unknown topic name should not resolve");
    ctx.expectEqual(topicId, 0U, "unknown topic name should reset topicId to 0");

    topicName = reinterpret_cast<const char *>(0x1);
    const bool unknownIdResolved = LDdsFramework::tryResolvePubsubDemoTopicName(0U, topicName);
    ctx.expect(!unknownIdResolved, "invalid topic id should not resolve");
    ctx.expect(topicName == nullptr, "invalid topic id should reset topicName to nullptr");
}

std::vector<TestCase> allTests()
{
    return {
        {"ByteBuffer.ReadWrite", testByteBufferReadWrite},
        {"ByteBuffer.Bounds", testByteBufferBounds},
        {"ByteBuffer.LargePayloadStress", testByteBufferLargePayloadStress},
        {"Message.RoundTrip", testMessageRoundTrip},
        {"Message.AckFactory", testMessageAckFactory},
        {"Message.DeserializeFuzzStress", testMessageDeserializeFuzzStress},
        {"Qos.LoadXml", testQosXmlLoad},
        {"Qos.Compatibility", testQosCompatibility},
        {"Qos.BoundaryValidation", testQosBoundaryValidation},
        {"TypeRegistry.Basic", testTypeRegistryBasic},
        {"TypeRegistry.ConcurrentSerializeStress", testTypeRegistryConcurrentSerializeStress},
        {"Domain.CacheBehavior", testDomainCacheBehavior},
        {"Domain.ConcurrentCacheStress", testDomainConcurrentCacheStress},
        {"LDds.InProcessPubSub", testLDdsInProcessPubSub},
        {"LDds.TcpBurstPubSub", testLDdsTcpBurstPubSub},
        {"LDds.TcpBurstMultiRound", testLDdsTcpBurstMultiRound},
        {"LDds.SecurityMismatchBoundary", testLDdsSecurityMismatchBoundary},
        {"Idl.TopicIdDeterministic", testIdlTopicIdDeterministic},
        {"Idl.TopicIdExplicitAnnotation", testIdlTopicIdExplicitAnnotation},
        {"Idl.TopicIdCollisionDetect", testIdlTopicIdCollisionDetect},
        {"Idl.ParserManyTopicsStress", testIdlParserManyTopicsStress},
        {"Idl.TopicIdInvalidAnnotationBoundary", testIdlTopicIdInvalidAnnotationBoundary},
        {"Generated.TopicNameResolveBoundary", testGeneratedTopicNameResolveBoundary},
    };
}

} // namespace

int main()
{
    const auto tests = allTests();

    int passed = 0;
    int failed = 0;
    const auto suiteStart = std::chrono::steady_clock::now();

    for (const auto & test : tests)
    {
        TestContext ctx;
        const auto start = std::chrono::steady_clock::now();

        try
        {
            test.fn(ctx);
        }
        catch (const std::exception & ex)
        {
            ctx.failures.push_back(std::string("uncaught exception: ") + ex.what());
        }
        catch (...)
        {
            ctx.failures.push_back("uncaught unknown exception");
        }

        const auto end = std::chrono::steady_clock::now();
        const double durationMs =
            std::chrono::duration<double, std::milli>(end - start).count();

        if (ctx.failures.empty())
        {
            ++passed;
            std::cout << "[ut][case] PASS " << test.name
                      << " duration_ms=" << durationMs << "\n";
        }
        else
        {
            ++failed;
            std::ostringstream detail;
            for (size_t i = 0; i < ctx.failures.size(); ++i)
            {
                if (i != 0)
                {
                    detail << " | ";
                }
                detail << ctx.failures[i];
            }
            std::cout << "[ut][case] FAIL " << test.name
                      << " duration_ms=" << durationMs
                      << " detail=" << detail.str() << "\n";
        }
    }

    const auto suiteEnd = std::chrono::steady_clock::now();
    const double totalMs =
        std::chrono::duration<double, std::milli>(suiteEnd - suiteStart).count();

    std::cout << "[ut][summary] total=" << tests.size()
              << " passed=" << passed
              << " failed=" << failed
              << " duration_ms=" << totalMs << "\n";
    return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
