#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include "LByteBuffer.h"
#include "LDomain.h"
#include "LDds.h"
#include "LMessage.h"
#include "LQos.h"
#include "LTypeRegistry.h"

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

std::vector<TestCase> allTests()
{
    return {
        {"ByteBuffer.ReadWrite", testByteBufferReadWrite},
        {"ByteBuffer.Bounds", testByteBufferBounds},
        {"Message.RoundTrip", testMessageRoundTrip},
        {"Message.AckFactory", testMessageAckFactory},
        {"Qos.LoadXml", testQosXmlLoad},
        {"Qos.Compatibility", testQosCompatibility},
        {"TypeRegistry.Basic", testTypeRegistryBasic},
        {"Domain.CacheBehavior", testDomainCacheBehavior},
        {"LDds.InProcessPubSub", testLDdsInProcessPubSub},
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
