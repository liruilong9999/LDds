#include "LDds.h"
#include "LTcpTransport.h"
#include "LUdpTransport.h"
#include "pugixml.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <chrono>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace LDdsFramework {
namespace {

constexpr uint32_t RELIABLE_MIN_WINDOW_SIZE = 16U;
constexpr uint32_t RELIABLE_MAX_WINDOW_SIZE = 4096U;
constexpr uint32_t RELIABLE_DEFAULT_MAX_RESEND = 32U;
constexpr int32_t RELIABLE_DEFAULT_RETRANSMIT_MS = 200;
constexpr int32_t RELIABLE_MIN_RETRANSMIT_MS = 80;
constexpr int32_t RELIABLE_MAX_RETRANSMIT_MS = 1000;
constexpr int32_t RELIABLE_MIN_HEARTBEAT_PROBE_MS = 300;
constexpr uint8_t DISCOVERY_ANNOUNCE_VERSION = 1U;
constexpr uint32_t DISCOVERY_CAP_RELIABLE_UDP = 0x01U;
constexpr uint32_t DISCOVERY_CAP_TCP = 0x02U;
constexpr uint32_t DISCOVERY_CAP_MULTICAST = 0x04U;
constexpr int32_t DISCOVERY_MIN_INTERVAL_MS = 300;
constexpr int32_t DISCOVERY_MIN_PEER_TTL_MS = 1000;
constexpr int32_t DISCOVERY_MAX_TOPICS = 1024;
constexpr int32_t QOS_HOT_RELOAD_INTERVAL_MS = 1000;
constexpr uint8_t SECURITY_ENVELOPE_MAGIC = 0xA5U;
constexpr uint8_t SECURITY_ENVELOPE_VERSION = 1U;
constexpr uint8_t SECURITY_FLAG_ENCRYPTED = 0x01U;
constexpr size_t SECURITY_ENVELOPE_PREFIX_SIZE =
    sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint64_t) + sizeof(uint64_t);
constexpr const char * DEFAULT_QOS_RELATIVE_PATH = "config/qos.xml";
constexpr const char * DEFAULT_RELY_RELATIVE_PATH = "config/ddsRely.xml";
constexpr uint8_t SAMPLE_ENVELOPE_MAGIC = 0xD1U;
constexpr uint8_t SAMPLE_ENVELOPE_VERSION = 1U;

int64_t getFileWriteTick(const std::string & filePath)
{
    if (filePath.empty())
    {
        return -1;
    }

    std::error_code ec;
    if (!std::filesystem::exists(filePath, ec) || ec)
    {
        return -1;
    }

    const auto writeTime = std::filesystem::last_write_time(filePath, ec);
    if (ec)
    {
        return -1;
    }
    return static_cast<int64_t>(writeTime.time_since_epoch().count());
}

std::string trim(std::string value)
{
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
    {
        return std::string();
    }

    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::string toLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool parseBoolText(const std::string & text, bool defaultValue = false)
{
    const std::string lower = toLower(trim(text));
    if (lower == "1" || lower == "true" || lower == "yes" || lower == "on")
    {
        return true;
    }
    if (lower == "0" || lower == "false" || lower == "no" || lower == "off")
    {
        return false;
    }
    return defaultValue;
}

std::string currentExecutableDirectoryImpl()
{
#if defined(_WIN32)
    std::vector<char> buffer(static_cast<size_t>(MAX_PATH), '\0');
    DWORD length = ::GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    while (length >= buffer.size())
    {
        buffer.resize(buffer.size() * 2U, '\0');
        length = ::GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    }
    if (length == 0)
    {
        return std::filesystem::current_path().string();
    }
    return std::filesystem::path(std::string(buffer.data(), length)).parent_path().string();
#else
    return std::filesystem::current_path().string();
#endif
}

uint32_t currentProcessIdValue() noexcept
{
#if defined(_WIN32)
    return static_cast<uint32_t>(::GetCurrentProcessId());
#else
    return static_cast<uint32_t>(::getpid());
#endif
}

std::string resolveExistingPath(const std::vector<std::filesystem::path> & candidates)
{
    for (const auto & candidate : candidates)
    {
        if (candidate.empty())
        {
            continue;
        }

        std::error_code ec;
        if (!std::filesystem::exists(candidate, ec) || ec)
        {
            continue;
        }

        const std::filesystem::path normalized = std::filesystem::weakly_canonical(candidate, ec);
        if (!ec)
        {
            return normalized.string();
        }
        return candidate.lexically_normal().string();
    }

    return std::string();
}

void * loadSharedModuleHandle(const std::string & modulePath)
{
#if defined(_WIN32)
    return reinterpret_cast<void *>(::LoadLibraryA(modulePath.c_str()));
#else
    return ::dlopen(modulePath.c_str(), RTLD_NOW | RTLD_GLOBAL);
#endif
}

void unloadSharedModuleHandle(void * handle) noexcept
{
    if (handle == nullptr)
    {
        return;
    }
#if defined(_WIN32)
    ::FreeLibrary(reinterpret_cast<HMODULE>(handle));
#else
    ::dlclose(handle);
#endif
}

std::string getSharedModuleErrorText()
{
#if defined(_WIN32)
    const DWORD errorCode = ::GetLastError();
    if (errorCode == 0U)
    {
        return std::string();
    }

    LPSTR messageBuffer = nullptr;
    const DWORD size = ::FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        errorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&messageBuffer),
        0,
        nullptr);
    std::string text = (size > 0U && messageBuffer != nullptr) ? std::string(messageBuffer, size) : std::to_string(errorCode);
    if (messageBuffer != nullptr)
    {
        ::LocalFree(messageBuffer);
    }
    return text;
#else
    const char * errorText = ::dlerror();
    return errorText == nullptr ? std::string() : std::string(errorText);
#endif
}

void appendU8(std::vector<uint8_t> & out, uint8_t value)
{
    out.push_back(value);
}

void appendU16(std::vector<uint8_t> & out, uint16_t value)
{
    out.push_back(static_cast<uint8_t>(value & 0xFFU));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFFU));
}

void appendU32(std::vector<uint8_t> & out, uint32_t value)
{
    out.push_back(static_cast<uint8_t>(value & 0xFFU));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFFU));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFFU));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFFU));
}

void appendU64(std::vector<uint8_t> & out, uint64_t value)
{
    for (int i = 0; i < 8; ++i)
    {
        out.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFFU));
    }
}

bool readU8(const std::vector<uint8_t> & data, size_t & offset, uint8_t & value)
{
    if (offset + 1 > data.size())
    {
        return false;
    }
    value = data[offset];
    ++offset;
    return true;
}

bool readU16(const std::vector<uint8_t> & data, size_t & offset, uint16_t & value)
{
    if (offset + 2 > data.size())
    {
        return false;
    }
    value = static_cast<uint16_t>(data[offset]) |
            static_cast<uint16_t>(static_cast<uint16_t>(data[offset + 1]) << 8);
    offset += 2;
    return true;
}

bool readU32(const std::vector<uint8_t> & data, size_t & offset, uint32_t & value)
{
    if (offset + 4 > data.size())
    {
        return false;
    }
    value = static_cast<uint32_t>(data[offset]) |
            (static_cast<uint32_t>(data[offset + 1]) << 8) |
            (static_cast<uint32_t>(data[offset + 2]) << 16) |
            (static_cast<uint32_t>(data[offset + 3]) << 24);
    offset += 4;
    return true;
}

bool readU64(const std::vector<uint8_t> & data, size_t & offset, uint64_t & value)
{
    if (offset + 8 > data.size())
    {
        return false;
    }
    value = 0;
    for (int i = 0; i < 8; ++i)
    {
        value |= (static_cast<uint64_t>(data[offset + static_cast<size_t>(i)]) << (8 * i));
    }
    offset += 8;
    return true;
}

LHostAddress resolveDomainMulticastGroup(DomainId domainId)
{
    return LHostAddress(LStringLiteral("239.255.0.%1").arg(static_cast<uint32_t>(domainId)));
}

uint32_t fnv1aHash32(const std::string & value)
{
    uint32_t hash = 2166136261U;
    for (const unsigned char ch : value)
    {
        hash ^= static_cast<uint32_t>(ch);
        hash *= 16777619U;
    }
    return (hash == 0U) ? 1U : hash;
}

uint64_t fnv1aHash64Bytes(const uint8_t * data, size_t size, uint64_t seed = 1469598103934665603ULL)
{
    uint64_t hash = seed;
    for (size_t i = 0; i < size; ++i)
    {
        hash ^= static_cast<uint64_t>(data[i]);
        hash *= 1099511628211ULL;
    }
    return hash;
}

uint64_t fnv1aHash64(const std::string & value, uint64_t seed = 1469598103934665603ULL)
{
    return fnv1aHash64Bytes(
        reinterpret_cast<const uint8_t *>(value.data()),
        value.size(),
        seed);
}

void hashAppendU8(uint64_t & hash, uint8_t value)
{
    hash = fnv1aHash64Bytes(&value, sizeof(value), hash);
}

void hashAppendU32(uint64_t & hash, uint32_t value)
{
    uint8_t data[4];
    data[0] = static_cast<uint8_t>(value & 0xFFU);
    data[1] = static_cast<uint8_t>((value >> 8) & 0xFFU);
    data[2] = static_cast<uint8_t>((value >> 16) & 0xFFU);
    data[3] = static_cast<uint8_t>((value >> 24) & 0xFFU);
    hash = fnv1aHash64Bytes(data, sizeof(data), hash);
}

void hashAppendU64(uint64_t & hash, uint64_t value)
{
    uint8_t data[8];
    for (int i = 0; i < 8; ++i)
    {
        data[i] = static_cast<uint8_t>((value >> (i * 8)) & 0xFFU);
    }
    hash = fnv1aHash64Bytes(data, sizeof(data), hash);
}

uint64_t xorshift64Star(uint64_t & state)
{
    state ^= state >> 12;
    state ^= state << 25;
    state ^= state >> 27;
    return state * 2685821657736338717ULL;
}

void applySymmetricCipher(std::vector<uint8_t> & payload, const std::string & psk, uint64_t nonce)
{
    uint64_t state = fnv1aHash64(psk);
    state ^= nonce + 0x9E3779B97F4A7C15ULL;
    if (state == 0)
    {
        state = 0xA5A5A5A5A5A5A5A5ULL;
    }

    for (size_t i = 0; i < payload.size(); ++i)
    {
        const uint64_t stream = xorshift64Star(state);
        payload[i] ^= static_cast<uint8_t>((stream >> ((i % 8) * 8)) & 0xFFU);
    }
}

uint64_t computeSecurityTag(
    const std::string & psk,
    const LMessage & message,
    uint8_t flags,
    uint64_t nonce,
    const std::vector<uint8_t> & body)
{
    uint64_t hash = fnv1aHash64(psk);
    hashAppendU8(hash, message.getDomainId());
    hashAppendU8(hash, static_cast<uint8_t>(message.getMessageType()));
    hashAppendU32(hash, message.getTopic());
    hashAppendU64(hash, message.getSequence());
    hashAppendU32(hash, message.getWriterId());
    hashAppendU64(hash, nonce);
    hashAppendU8(hash, flags);
    if (!body.empty())
    {
        hash = fnv1aHash64Bytes(body.data(), body.size(), hash);
    }
    return hash;
}

std::string messageTypeToText(LMessageType type)
{
    switch (type)
    {
    case LMessageType::Data:
        return "Data";
    case LMessageType::Heartbeat:
        return "Heartbeat";
    case LMessageType::Ack:
        return "Ack";
    case LMessageType::Nack:
        return "Nack";
    case LMessageType::HeartbeatReq:
        return "HeartbeatReq";
    case LMessageType::HeartbeatRsp:
        return "HeartbeatRsp";
    case LMessageType::DiscoveryAnnounce:
        return "DiscoveryAnnounce";
    default:
        return "Unknown";
    }
}

int32_t resolveDeadlineMs(const LQos & qos)
{
    if (qos.deadlineMs > 0)
    {
        return qos.deadlineMs;
    }

    const DeadlineQosPolicy & policy = qos.getDeadline();
    if (!policy.enabled || policy.period.isInfinite())
    {
        return 0;
    }

    const int64_t millis =
        (policy.period.seconds * 1000LL) + static_cast<int64_t>(policy.period.nanoseconds / 1000000U);
    return millis > 0 ? static_cast<int32_t>(millis) : 0;
}

std::chrono::milliseconds resolveHeartbeatInterval(int32_t deadlineMs)
{
    if (deadlineMs <= 0)
    {
        return std::chrono::milliseconds(1000);
    }

    const int32_t candidate = std::max(200, std::min(2000, deadlineMs / 3));
    return std::chrono::milliseconds(candidate > 0 ? candidate : 200);
}

DomainId resolveEffectiveDomainId(const LQos & qos, DomainId requestedDomainId)
{
    if (requestedDomainId != INVALID_DOMAIN_ID)
    {
        return requestedDomainId;
    }
    return static_cast<DomainId>(qos.domainId);
}

bool applyDomainPortMapping(
    TransportConfig & config,
    DomainId          domainId,
    std::string &     errorMessage)
{
    if (!config.enableDomainPortMapping)
    {
        return true;
    }

    if (config.basePort == 0)
    {
        errorMessage = "basePort must be > 0 when enableDomainPortMapping=true";
        return false;
    }
    if (config.domainPortOffset == 0)
    {
        errorMessage = "domainPortOffset must be > 0 when enableDomainPortMapping=true";
        return false;
    }

    const uint32_t mappedPort =
        static_cast<uint32_t>(config.basePort) +
        (static_cast<uint32_t>(domainId) * static_cast<uint32_t>(config.domainPortOffset));
    if (mappedPort > 65535U)
    {
        errorMessage = "mapped port exceeds 65535";
        return false;
    }

    config.bindPort = static_cast<quint16>(mappedPort);
    if (!config.remoteAddress.isEmpty() || config.remotePort != 0)
    {
        config.remotePort = static_cast<quint16>(mappedPort);
    }

    errorMessage.clear();
    return true;
}

} // namespace

LDds::LDds()
    : m_qos()
    , m_domain()
    , m_effectiveDomainId(DEFAULT_DOMAIN_ID)
    , m_pTransport()
    , m_pTypeRegistry(std::make_shared<LTypeRegistry>())
    , m_running(false)
    , m_sequence(0)
    , m_qosThreadRunning(false)
    , m_subscribers()
    , m_subscribersMutex()
    , m_deadlineMissedCallback()
    , m_errorMutex()
    , m_lastError()
    , m_qosThread()
    , m_qosCondition()
    , m_qosMutex()
    , m_lastTopicActivity()
    , m_deadlineMissedTopics()
    , m_lastHeartbeatSend(std::chrono::steady_clock::now())
    , m_lastHeartbeatReceive(std::chrono::steady_clock::now())
    , m_deadlineCheckInterval(200)
    , m_heartbeatInterval(1000)
    , m_reliableUdpEnabled(false)
    , m_reliableWriterId(1)
    , m_reliableRetransmitInterval(RELIABLE_DEFAULT_RETRANSMIT_MS)
    , m_reliableHeartbeatProbeInterval(std::chrono::milliseconds(RELIABLE_MIN_HEARTBEAT_PROBE_MS * 2))
    , m_reliableWindowSize(RELIABLE_MIN_WINDOW_SIZE)
    , m_reliableMaxResendCount(RELIABLE_DEFAULT_MAX_RESEND)
    , m_lastReliableHeartbeatProbe(std::chrono::steady_clock::now())
    , m_reliablePending()
    , m_reliableReceivers()
    , m_reliableMutex()
    , m_discoveryEnabled(false)
    , m_discoveryUseMulticast(false)
    , m_discoveryNodeId(1)
    , m_discoveryPort(0)
    , m_discoveryInterval(1000)
    , m_peerTtl(5000)
    , m_lastDiscoverySend(std::chrono::steady_clock::now())
    , m_discoveryMulticastGroup()
    , m_discoveryPeers()
    , m_discoveryMutex()
    , m_knownTopics()
    , m_knownTopicsMutex()
    , m_topicOwnership()
    , m_ownershipMutex()
    , m_qosHotReloadEnabled(false)
    , m_qosXmlPath()
    , m_qosLastWriteTick(-1)
    , m_qosReloadInterval(QOS_HOT_RELOAD_INTERVAL_MS)
    , m_lastQosReloadCheck(std::chrono::steady_clock::now())
    , m_qosReloadMutex()
    , m_metrics()
    , m_metricsMutex()
    , m_lossEstimateByWriter()
    , m_securityMutex()
    , m_securityConfig{false, false, std::string()}
    , m_logMutex()
    , m_structuredLogEnabled(false)
    , m_logCallback()
    , m_findSetMutex()
    , m_findSetCache()
    , m_runtimeModuleMutex()
    , m_runtimeModules()
    , m_identityMutex()
    , m_runtimeIdentity()
    , m_relyConfigPathOverride()
{
}

LDds::~LDds()
{
    shutdown();
}

LDds & LDds::instance() noexcept
{
    static LDds instance;
    return instance;
}

bool LDds::initialize()
{
    TransportConfig config;
    return initialize(config);
}

bool LDds::initialize(const TransportConfig & transportConfig)
{
    const std::string qosPath = resolveRuntimePath(DEFAULT_QOS_RELATIVE_PATH);

    LQos qos;
    std::string loadError;
    std::error_code ec;
    if (!qosPath.empty() && std::filesystem::exists(qosPath, ec) && !ec)
    {
        if (qos.loadFromXmlFile(qosPath, &loadError))
        {
            if (!initialize(qos, transportConfig, INVALID_DOMAIN_ID))
            {
                return false;
            }
            initializeQosHotReload(qosPath);
            return true;
        }

        setLastError("failed to load qos xml, fallback to defaults: " + loadError);
    }

    return initialize(qos, transportConfig, INVALID_DOMAIN_ID);
}

void LDds::applyInitOptions(const DdsInitOptions & options)
{
    {
        std::lock_guard<std::mutex> lock(m_identityMutex);
        m_runtimeIdentity.profileName = options.profileName;
        m_runtimeIdentity.sourceApp = options.sourceApp;
        m_runtimeIdentity.runId = options.runId;
    }

    std::lock_guard<std::mutex> lock(m_runtimeModuleMutex);
    m_relyConfigPathOverride = options.relyFile;
}

bool LDds::initialize(const DdsInitOptions & options)
{
    applyInitOptions(options);

    const DomainId domainId =
        (options.domainId >= 0) ? static_cast<DomainId>(options.domainId) : INVALID_DOMAIN_ID;

    if (!options.qosFile.empty())
    {
        return initializeFromQosXml(options.qosFile, options.transportConfig, domainId);
    }

    if (domainId != INVALID_DOMAIN_ID)
    {
        LQos qos;
        return initialize(qos, options.transportConfig, domainId);
    }

    return initialize(options.transportConfig);
}

bool LDds::initialize(const LQos & qos)
{
    TransportConfig config;
    return initialize(qos, config, INVALID_DOMAIN_ID);
}

bool LDds::initialize(const LQos & qos, const TransportConfig & transportConfig, DomainId domainId)
{
    if (m_running.load())
    {
        return true;
    }

    if (!loadConfiguredRuntimeModules())
    {
        return false;
    }
    if (!applyGeneratedModules())
    {
        return false;
    }
    rememberRegisteredTopics();

    LQos effectiveQos = qos;
    const DomainId effectiveDomainId = resolveEffectiveDomainId(qos, domainId);
    if (effectiveDomainId > 255U)
    {
        setLastError("invalid domainId=" + std::to_string(effectiveDomainId) + ", must be in [0,255]");
        return false;
    }

    effectiveQos.domainId = static_cast<uint8_t>(effectiveDomainId);
    effectiveQos.historyDepth = (effectiveQos.historyDepth <= 0) ? 1 : effectiveQos.historyDepth;
    effectiveQos.deadlineMs = std::max(0, effectiveQos.deadlineMs);

    HistoryQosPolicy history = effectiveQos.getHistory();
    history.depth = effectiveQos.historyDepth;
    history.enabled = true;
    effectiveQos.setHistory(history);

    DeadlineQosPolicy deadline = effectiveQos.getDeadline();
    deadline.enabled = (effectiveQos.deadlineMs > 0);
    if (deadline.enabled)
    {
        deadline.period.seconds = static_cast<int64_t>(effectiveQos.deadlineMs / 1000);
        deadline.period.nanoseconds = static_cast<uint32_t>((effectiveQos.deadlineMs % 1000) * 1000000);
    }
    else
    {
        deadline.period = Duration(DURATION_INFINITY);
    }
    effectiveQos.setDeadline(deadline);

    ReliabilityQosPolicy reliability = effectiveQos.getReliability();
    reliability.enabled = effectiveQos.reliable;
    reliability.kind = effectiveQos.reliable ? ReliabilityKind::Reliable : ReliabilityKind::BestEffort;
    effectiveQos.setReliability(reliability);

    DurabilityQosPolicy durability = effectiveQos.getDurability();
    durability.enabled = true;
    durability.kind = effectiveQos.durabilityKind;
    effectiveQos.setDurability(durability);

    OwnershipQosPolicy ownership = effectiveQos.getOwnership();
    ownership.enabled = true;
    ownership.kind = effectiveQos.ownershipKind;
    ownership.strength = std::max(0, effectiveQos.ownershipStrength);
    effectiveQos.setOwnership(ownership);

    std::string validateError;
    if (!effectiveQos.validate(validateError))
    {
        setLastError(
            "invalid qos (domain=" + std::to_string(effectiveDomainId) + "): " + validateError);
        return false;
    }

    TransportConfig effectiveTransportConfig = transportConfig;
    const bool hasExplicitBindPort = effectiveTransportConfig.bindPort != 0;
    const bool hasExplicitRemote =
        !effectiveTransportConfig.remoteAddress.isEmpty() &&
        effectiveTransportConfig.remotePort != 0;
    if (!effectiveTransportConfig.enableDomainPortMapping &&
        effectiveQos.enableDomainPortMapping &&
        !hasExplicitBindPort &&
        !hasExplicitRemote)
    {
        effectiveTransportConfig.enableDomainPortMapping = true;
        effectiveTransportConfig.basePort = effectiveQos.basePort;
        effectiveTransportConfig.domainPortOffset = effectiveQos.domainPortOffset;
    }

    const bool isUdpTransport = (effectiveQos.transportType == TransportType::UDP);
    if (isUdpTransport && effectiveTransportConfig.enableDiscovery)
    {
        const bool hasConfiguredRemote =
            !effectiveTransportConfig.remoteAddress.isEmpty() &&
            effectiveTransportConfig.remotePort != 0;
        if (hasConfiguredRemote)
        {
            // Preserve legacy point-to-point behavior when remote is explicitly configured.
            effectiveTransportConfig.enableDiscovery = false;
        }

        if (effectiveTransportConfig.enableDiscovery &&
            effectiveTransportConfig.discoveryIntervalMs < DISCOVERY_MIN_INTERVAL_MS)
        {
            effectiveTransportConfig.discoveryIntervalMs = DISCOVERY_MIN_INTERVAL_MS;
        }
        if (effectiveTransportConfig.enableDiscovery &&
            effectiveTransportConfig.peerTtlMs < DISCOVERY_MIN_PEER_TTL_MS)
        {
            effectiveTransportConfig.peerTtlMs = DISCOVERY_MIN_PEER_TTL_MS;
        }

        if (effectiveTransportConfig.enableDiscovery &&
            effectiveTransportConfig.bindPort == 0 &&
            !effectiveTransportConfig.enableDomainPortMapping)
        {
            effectiveTransportConfig.enableDomainPortMapping = true;
            if (effectiveTransportConfig.basePort == 0)
            {
                effectiveTransportConfig.basePort = 20000;
            }
            if (effectiveTransportConfig.domainPortOffset == 0)
            {
                effectiveTransportConfig.domainPortOffset = 10;
            }
        }

        if (effectiveTransportConfig.enableDiscovery)
        {
            effectiveTransportConfig.enableBroadcast = true;
            effectiveTransportConfig.enableMulticast = true;
        }
        if (effectiveTransportConfig.enableDiscovery &&
            effectiveTransportConfig.enableMulticast &&
            effectiveTransportConfig.multicastGroup.isEmpty())
        {
            effectiveTransportConfig.multicastGroup =
                resolveDomainMulticastGroup(effectiveDomainId).toString();
        }
    }

    std::string mappingError;
    if (!applyDomainPortMapping(effectiveTransportConfig, effectiveDomainId, mappingError))
    {
        setLastError(
            "invalid transport config (domain=" + std::to_string(effectiveDomainId) + "): " +
            mappingError);
        return false;
    }

    if (!m_domain.isValid() || m_domain.getDomainId() != effectiveDomainId)
    {
        m_domain.destroy();
        if (!m_domain.create(effectiveDomainId, &effectiveQos))
        {
            setLastError("failed to create domain=" + std::to_string(effectiveDomainId));
            return false;
        }
    }

    m_qos = effectiveQos;
    m_effectiveDomainId = effectiveDomainId;
    m_topicQosOverrides.clear();
    for (const auto & overrideValue : effectiveQos.getTopicOverrides())
    {
        const uint32_t topic =
            !overrideValue.topicKey.empty()
                ? LTypeRegistry::makeTopicId(overrideValue.topicKey)
                : 0U;
        if (topic == 0)
        {
            continue;
        }

        m_topicQosOverrides[topic] = overrideValue;
        if (overrideValue.historyDepth > 0)
        {
            m_domain.setTopicHistoryDepth(topic, static_cast<size_t>(overrideValue.historyDepth));
        }
    }
    {
        std::lock_guard<std::mutex> lock(m_securityMutex);
        m_securityConfig.enabled = effectiveQos.securityEnabled;
        m_securityConfig.encryptPayload = effectiveQos.securityEncryptPayload;
        m_securityConfig.psk = effectiveQos.securityPsk;
    }
    m_structuredLogEnabled = effectiveQos.structuredLogEnabled;
    resetRuntimeMetrics();

    if (!createTransportFromQos(effectiveQos, effectiveTransportConfig))
    {
        return false;
    }

    m_pTransport->setReceiveCallback(
        [this](const LMessage & message, const LHostAddress & senderAddress, quint16 senderPort) {
            handleTransportMessage(message, senderAddress, senderPort);
        }
    );

    if (!m_pTransport->start())
    {
        setLastError(
            "failed to start transport (domain=" + std::to_string(m_effectiveDomainId) + "): " +
            m_pTransport->getLastError().toStdString());
        m_pTransport.reset();
        return false;
    }

    initializeReliableState();
    initializeDiscoveryState(effectiveTransportConfig);
    updateRuntimeGauges();

    m_sequence.store(0);
    m_running.store(true);

    {
        std::lock_guard<std::mutex> lock(m_qosMutex);
        m_lastTopicActivity.clear();
        m_deadlineMissedTopics.clear();
        m_lastHeartbeatSend = std::chrono::steady_clock::now();
        m_lastHeartbeatReceive = m_lastHeartbeatSend;
    }

    startQosThread();
    clearQosHotReloadState();
    emitStructuredLog(
        "info",
        "ldds",
        "initialize success transport=" +
            std::string(effectiveQos.transportType == TransportType::TCP ? "tcp" : "udp"));
    return true;
}

bool LDds::initializeFromQosXml(
    const std::string & qosXmlPath,
    const TransportConfig & transportConfig,
    DomainId domainId)
{
    LQos parsedQos;
    std::string error;
    if (!parsedQos.loadFromXmlFile(qosXmlPath, &error))
    {
        setLastError("failed to load qos xml: " + error);
        return false;
    }
    if (!initialize(parsedQos, transportConfig, domainId))
    {
        return false;
    }

    initializeQosHotReload(qosXmlPath);
    return true;
}

void LDds::shutdown() noexcept
{
    m_running.store(false);
    stopQosThread();
    clearReliableState();
    clearDiscoveryState();
    clearQosHotReloadState();

    if (m_pTransport)
    {
        m_pTransport->stop();
        m_pTransport.reset();
    }

    {
        std::lock_guard<std::mutex> lock(m_subscribersMutex);
        m_subscribers.clear();
    }

    {
        std::lock_guard<std::mutex> lock(m_qosMutex);
        m_lastTopicActivity.clear();
        m_deadlineMissedTopics.clear();
    }

    {
        std::lock_guard<std::mutex> lock(m_knownTopicsMutex);
        m_knownTopics.clear();
    }

    {
        std::lock_guard<std::mutex> lock(m_findSetMutex);
        m_findSetCache.clear();
    }

    {
        std::lock_guard<std::mutex> lock(m_ownershipMutex);
        m_topicOwnership.clear();
    }

    {
        std::lock_guard<std::mutex> lock(m_metricsMutex);
        m_lossEstimateByWriter.clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_securityMutex);
        m_securityConfig = SecurityRuntimeConfig{false, false, std::string()};
    }
    {
        std::lock_guard<std::mutex> lock(m_identityMutex);
        m_runtimeIdentity = RuntimeIdentity();
    }
    m_relyConfigPathOverride.clear();

    clearRuntimeModules();
    m_domain.destroy();
    m_effectiveDomainId = DEFAULT_DOMAIN_ID;
    m_sequence.store(0);
    updateRuntimeGauges();
}

bool LDds::isRunning() const noexcept
{
    return m_running.load();
}

void LDds::setTypeRegistry(std::shared_ptr<LTypeRegistry> typeRegistry)
{
    if (typeRegistry)
    {
        m_pTypeRegistry = std::move(typeRegistry);
        (void)applyGeneratedModules();
        rememberRegisteredTopics();
    }
}

std::shared_ptr<LTypeRegistry> LDds::getTypeRegistry() const
{
    return m_pTypeRegistry;
}

bool LDds::registerType(
    const std::string &         typeName,
    uint32_t                    topic,
    LTypeRegistry::TypeFactory  factory,
    LTypeRegistry::SerializeFn  serializer,
    LTypeRegistry::DeserializeFn deserializer
)
{
    const bool ok = m_pTypeRegistry->registerType(
        typeName,
        topic,
        std::move(factory),
        std::move(serializer),
        std::move(deserializer)
    );
    if (ok)
    {
        rememberKnownTopic(topic);
    }
    return ok;
}

bool LDds::serializeTopic(
    const std::string & topicKey,
    const void * object,
    std::vector<uint8_t> & payload) const
{
    const uint32_t topic = m_pTypeRegistry->getTopicByTopicKey(topicKey);
    if (topic == 0)
    {
        return false;
    }
    return m_pTypeRegistry->serializeByTopic(topic, object, payload);
}

bool LDds::deserializeTopic(
    const std::string & topicKey,
    const std::vector<uint8_t> & payload,
    void * object) const
{
    const uint32_t topic = m_pTypeRegistry->getTopicByTopicKey(topicKey);
    if (topic == 0)
    {
        return false;
    }
    return m_pTypeRegistry->deserializeByTopic(topic, payload, object);
}

bool LDds::getTopicInfo(const std::string & topicKey, DdsTopicInfo & topicInfo) const
{
    return m_pTypeRegistry->getTopicInfo(topicKey, topicInfo);
}

std::vector<DdsTopicInfo> LDds::listTopicInfos() const
{
    return m_pTypeRegistry->listTopicInfos();
}

bool LDds::publishTopic(uint32_t topic, const std::vector<uint8_t> & payload)
{
    const std::string typeName = m_pTypeRegistry->getTypeNameByTopic(topic);
    return publishSerializedTopic(topic, std::vector<uint8_t>(payload), typeName);
}

bool LDds::publish(const std::string & topicKey, const std::shared_ptr<void> & object)
{
    if (!object)
    {
        setLastError("publish object is null");
        return false;
    }

    const uint32_t topic = m_pTypeRegistry->getTopicByTopicKey(topicKey);
    if (topic == 0)
    {
        setLastError("topic not registered for key: " + topicKey);
        return false;
    }

    std::vector<uint8_t> payload;
    if (!m_pTypeRegistry->serializeByTopic(topic, object.get(), payload))
    {
        setLastError("serialize failed for topic=" + std::to_string(topic));
        return false;
    }

    return publishSerializedTopic(topic, std::move(payload), m_pTypeRegistry->getTypeNameByTopic(topic));
}

bool LDds::publish(
    const std::string & topicKey,
    const std::shared_ptr<void> & object,
    const DdsPublishOptions & publishOptions)
{
    if (!object)
    {
        setLastError("publish object is null");
        return false;
    }

    const uint32_t topic = m_pTypeRegistry->getTopicByTopicKey(topicKey);
    if (topic == 0)
    {
        setLastError("topic not registered for key: " + topicKey);
        return false;
    }

    std::vector<uint8_t> payload;
    if (!m_pTypeRegistry->serializeByTopic(topic, object.get(), payload))
    {
        setLastError("serialize failed for topic=" + std::to_string(topic));
        return false;
    }

    return publishSerializedTopic(
        topic,
        std::move(payload),
        m_pTypeRegistry->getTypeNameByTopic(topic),
        &publishOptions);
}

bool LDds::publishTopic(const std::string & typeName, const std::shared_ptr<void> & object)
{
    if (!object)
    {
        setLastError("publish object is null");
        return false;
    }

    const uint32_t topic = m_pTypeRegistry->getTopicByTypeName(typeName);
    if (topic == 0)
    {
        setLastError("topic not registered for type: " + typeName);
        return false;
    }

    std::vector<uint8_t> payload;
    if (!m_pTypeRegistry->serializeByTopic(topic, object.get(), payload))
    {
        setLastError("serialize failed for topic=" + std::to_string(topic));
        return false;
    }

    return publishSerializedTopic(topic, std::move(payload), typeName);
}

void LDds::subscribeTopic(uint32_t topic, TopicCallback callback)
{
    if (topic == 0 || !callback)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(m_subscribersMutex);
    m_subscribers[topic].push_back(std::move(callback));
    rememberKnownTopic(topic);
}

LFindSet * LDds::sub(const std::string & topicKey)
{
    const uint32_t topic = m_pTypeRegistry->getTopicByTopicKey(topicKey);
    if (topic == 0)
    {
        setLastError("topic not registered for key: " + topicKey);
        return nullptr;
    }

    rememberKnownTopic(topic);

    LFindSet findSet = m_domain.getFindSetByTopic(topic);
    findSet.bindTypeRegistry(m_pTypeRegistry.get(), topic);

    std::lock_guard<std::mutex> lock(m_findSetMutex);
    auto & cache = m_findSetCache[topic];
    cache = std::move(findSet);
    return &cache;
}

bool LDds::readNextSerialized(
    const std::string & topicKey,
    DdsCursor & cursor,
    std::vector<uint8_t> & payload,
    DdsSampleMetadata * metadata) const
{
    const uint32_t topic = m_pTypeRegistry->getTopicByTopicKey(topicKey);
    if (topic == 0)
    {
        return false;
    }

    LFindSet findSet = m_domain.getFindSetByTopic(topic);
    findSet.reset();
    bool found = false;
    std::size_t selectedIndex = 0;
    uint64_t selectedSequence = 0;
    std::vector<uint8_t> selectedPayload;
    DdsSampleMetadata selectedMetadata;

    for (std::size_t index = 0; index < findSet.size(); ++index)
    {
        std::vector<uint8_t> candidatePayload;
        if (!findSet.getNextTopicData(candidatePayload))
        {
            break;
        }
        const DdsSampleMetadata * candidateMetadata = findSet.getMetadata(index);
        if (candidateMetadata == nullptr || candidateMetadata->sequence <= cursor.lastSequence)
        {
            continue;
        }
        if (!found || candidateMetadata->sequence < selectedSequence)
        {
            found = true;
            selectedIndex = index;
            selectedSequence = candidateMetadata->sequence;
            selectedPayload = std::move(candidatePayload);
            selectedMetadata = *candidateMetadata;
        }
    }
    if (!found)
    {
        return false;
    }

    L_UNUSED(selectedIndex)
    payload = std::move(selectedPayload);
    cursor.lastSequence = selectedSequence;
    if (metadata != nullptr)
    {
        *metadata = std::move(selectedMetadata);
    }
    return true;
}

bool LDds::getTopicQos(const std::string & topicKey, TopicQosOverride & topicQos) const
{
    return m_qos.resolveTopicOverride(topicKey, topicQos);
}

void LDds::unsubscribeTopic(uint32_t topic)
{
    std::lock_guard<std::mutex> lock(m_subscribersMutex);
    m_subscribers.erase(topic);
}

void LDds::setDeadlineMissedCallback(DeadlineMissedCallback callback)
{
    std::lock_guard<std::mutex> lock(m_qosMutex);
    m_deadlineMissedCallback = std::move(callback);
}

void LDds::setLogCallback(LogCallback callback)
{
    std::lock_guard<std::mutex> lock(m_logMutex);
    m_logCallback = std::move(callback);
}

std::string LDds::exportMetricsText() const
{
    const uint64_t sentMessages = m_metrics.sentMessages.load();
    const uint64_t receivedMessages = m_metrics.receivedMessages.load();
    const uint64_t estimatedDrops = m_metrics.estimatedDrops.load();
    const uint64_t retransmitCount = m_metrics.retransmitCount.load();
    const uint64_t queueLength = m_metrics.queueLength.load();
    const uint64_t queueDropCount = m_metrics.queueDropCount.load();
    const uint64_t connectionCount = m_metrics.connectionCount.load();
    const uint64_t deadlineMissCount = m_metrics.deadlineMissCount.load();
    const uint64_t authRejectedCount = m_metrics.authRejectedCount.load();
    const uint64_t sentBytes = m_metrics.sentBytes.load();
    const uint64_t receivedBytes = m_metrics.receivedBytes.load();

    std::ostringstream output;
    output << "# TYPE ldds_messages_sent_total counter\n";
    output << "ldds_messages_sent_total{domain=\"" << static_cast<uint32_t>(m_effectiveDomainId)
           << "\"} " << sentMessages << "\n";
    output << "# TYPE ldds_messages_received_total counter\n";
    output << "ldds_messages_received_total{domain=\"" << static_cast<uint32_t>(m_effectiveDomainId)
           << "\"} " << receivedMessages << "\n";
    output << "# TYPE ldds_drop_estimated_total counter\n";
    output << "ldds_drop_estimated_total{domain=\"" << static_cast<uint32_t>(m_effectiveDomainId)
           << "\"} " << estimatedDrops << "\n";
    output << "# TYPE ldds_retransmit_total counter\n";
    output << "ldds_retransmit_total{domain=\"" << static_cast<uint32_t>(m_effectiveDomainId)
           << "\"} " << retransmitCount << "\n";
    output << "# TYPE ldds_queue_length gauge\n";
    output << "ldds_queue_length{domain=\"" << static_cast<uint32_t>(m_effectiveDomainId)
           << "\"} " << queueLength << "\n";
    output << "# TYPE ldds_queue_drop_total counter\n";
    output << "ldds_queue_drop_total{domain=\"" << static_cast<uint32_t>(m_effectiveDomainId)
           << "\"} " << queueDropCount << "\n";
    output << "# TYPE ldds_connections gauge\n";
    output << "ldds_connections{domain=\"" << static_cast<uint32_t>(m_effectiveDomainId)
           << "\"} " << connectionCount << "\n";
    output << "# TYPE ldds_deadline_miss_total counter\n";
    output << "ldds_deadline_miss_total{domain=\"" << static_cast<uint32_t>(m_effectiveDomainId)
           << "\"} " << deadlineMissCount << "\n";
    output << "# TYPE ldds_auth_rejected_total counter\n";
    output << "ldds_auth_rejected_total{domain=\"" << static_cast<uint32_t>(m_effectiveDomainId)
           << "\"} " << authRejectedCount << "\n";
    output << "# TYPE ldds_sent_bytes_total counter\n";
    output << "ldds_sent_bytes_total{domain=\"" << static_cast<uint32_t>(m_effectiveDomainId)
           << "\"} " << sentBytes << "\n";
    output << "# TYPE ldds_received_bytes_total counter\n";
    output << "ldds_received_bytes_total{domain=\"" << static_cast<uint32_t>(m_effectiveDomainId)
           << "\"} " << receivedBytes << "\n";
    return output.str();
}

const LQos & LDds::getQos() const noexcept
{
    return m_qos;
}

TransportProtocol LDds::getTransportProtocol() const noexcept
{
    if (!m_pTransport)
    {
        return (m_qos.transportType == TransportType::TCP)
            ? TransportProtocol::TCP
            : TransportProtocol::UDP;
    }
    return m_pTransport->getProtocol();
}

LDomain & LDds::domain() noexcept
{
    return m_domain;
}

const LDomain & LDds::domain() const noexcept
{
    return m_domain;
}

std::string LDds::getLastError() const
{
    std::lock_guard<std::mutex> lock(m_errorMutex);
    return m_lastError;
}

LDdsContext::LDdsContext()
    : m_options()
    , m_dds()
{
}

LDdsContext::LDdsContext(const DdsInitOptions & options)
    : m_options(options)
    , m_dds()
{
}

bool LDdsContext::initialize()
{
    return m_dds.initialize(m_options);
}

bool LDdsContext::initialize(const DdsInitOptions & options)
{
    m_options = options;
    return m_dds.initialize(m_options);
}

void LDdsContext::shutdown() noexcept
{
    m_dds.shutdown();
}

bool LDdsContext::isRunning() const noexcept
{
    return m_dds.isRunning();
}

LDds & LDdsContext::dds() noexcept
{
    return m_dds;
}

const LDds & LDdsContext::dds() const noexcept
{
    return m_dds;
}

const DdsInitOptions & LDdsContext::options() const noexcept
{
    return m_options;
}

LFindSet * LDdsContext::sub(const std::string & topicKey)
{
    return m_dds.sub(topicKey);
}

bool LDdsContext::getTopicInfo(const std::string & topicKey, DdsTopicInfo & topicInfo) const
{
    return m_dds.getTopicInfo(topicKey, topicInfo);
}

std::vector<DdsTopicInfo> LDdsContext::listTopicInfos() const
{
    return m_dds.listTopicInfos();
}

std::shared_ptr<LDdsContext> createContext(const DdsInitOptions & options)
{
    return std::make_shared<LDdsContext>(options);
}

bool LDds::applyGeneratedModules()
{
    if (!m_pTypeRegistry)
    {
        setLastError("type registry is null");
        return false;
    }

    std::vector<std::string> appliedModules;
    if (!m_pTypeRegistry->applyGeneratedModules(&appliedModules))
    {
        setLastError("failed to apply generated modules");
        return false;
    }

    if (!appliedModules.empty())
    {
        rememberRegisteredTopics();
    }
    return true;
}

void appendString(std::vector<uint8_t> & out, const std::string & value)
{
    appendU16(out, static_cast<uint16_t>(std::min<std::size_t>(value.size(), 65535U)));
    out.insert(out.end(), value.begin(), value.begin() + static_cast<std::ptrdiff_t>(std::min<std::size_t>(value.size(), 65535U)));
}

bool readString(const std::vector<uint8_t> & data, size_t & offset, std::string & value)
{
    uint16_t size = 0;
    if (!readU16(data, offset, size))
    {
        return false;
    }
    if (offset + size > data.size())
    {
        return false;
    }
    value.assign(reinterpret_cast<const char *>(data.data() + offset), size);
    offset += size;
    return true;
}

bool encodeSampleEnvelope(
    const DdsSampleMetadata & metadata,
    const std::vector<uint8_t> & payload,
    std::vector<uint8_t> & out)
{
    out.clear();
    out.reserve(payload.size() + metadata.sourceApp.size() + metadata.runId.size() + 64U);
    appendU8(out, SAMPLE_ENVELOPE_MAGIC);
    appendU8(out, SAMPLE_ENVELOPE_VERSION);
    appendU64(out, metadata.simTimestamp);
    appendU64(out, metadata.publishTimestamp);
    appendU64(out, metadata.sequence);
    appendString(out, metadata.sourceApp);
    appendString(out, metadata.runId);
    appendU32(out, static_cast<uint32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
    return true;
}

bool decodeSampleEnvelope(
    const std::vector<uint8_t> & encoded,
    DdsSampleMetadata & metadata,
    std::vector<uint8_t> & payload)
{
    payload.clear();
    metadata = DdsSampleMetadata();

    size_t offset = 0;
    uint8_t magic = 0;
    uint8_t version = 0;
    uint32_t payloadSize = 0;
    if (!readU8(encoded, offset, magic) ||
        !readU8(encoded, offset, version) ||
        magic != SAMPLE_ENVELOPE_MAGIC ||
        version != SAMPLE_ENVELOPE_VERSION ||
        !readU64(encoded, offset, metadata.simTimestamp) ||
        !readU64(encoded, offset, metadata.publishTimestamp) ||
        !readU64(encoded, offset, metadata.sequence) ||
        !readString(encoded, offset, metadata.sourceApp) ||
        !readString(encoded, offset, metadata.runId) ||
        !readU32(encoded, offset, payloadSize))
    {
        return false;
    }
    if (offset + payloadSize > encoded.size())
    {
        return false;
    }

    payload.assign(
        encoded.begin() + static_cast<std::ptrdiff_t>(offset),
        encoded.begin() + static_cast<std::ptrdiff_t>(offset + payloadSize));
    return true;
}

void LDds::rememberRegisteredTopics()
{
    if (!m_pTypeRegistry)
    {
        return;
    }

    const std::vector<uint32_t> topics = m_pTypeRegistry->getRegisteredTopics();
    for (uint32_t topic : topics)
    {
        rememberKnownTopic(topic);
    }
}

bool LDds::loadConfiguredRuntimeModules()
{
    const std::string configPath =
        m_relyConfigPathOverride.empty()
            ? resolveRuntimePath(DEFAULT_RELY_RELATIVE_PATH)
            : resolvePathAgainstBase(currentExecutableDirectory(), m_relyConfigPathOverride);
    std::error_code ec;
    if (configPath.empty() || !std::filesystem::exists(configPath, ec) || ec)
    {
        return true;
    }
    return loadConfiguredRuntimeModules(configPath);
}

bool LDds::loadConfiguredRuntimeModules(const std::string & configPath)
{
    pugi::xml_document document;
    const pugi::xml_parse_result result = document.load_file(configPath.c_str());
    if (!result)
    {
        setLastError("failed to parse ddsRely xml: " + std::string(result.description()));
        return false;
    }

    const pugi::xml_node root =
        document.child("ddsRely")
            ? document.child("ddsRely")
            : (document.child("DDSRely")
                   ? document.child("DDSRely")
                   : document.document_element());
    if (!root)
    {
        setLastError("ddsRely xml is empty");
        return false;
    }

    for (const pugi::xml_node libraryNode : root.children())
    {
        const std::string nodeName = toLower(trim(libraryNode.name()));
        if (nodeName != "library" && nodeName != "module" && nodeName != "dll")
        {
            continue;
        }

        std::string moduleName = trim(libraryNode.attribute("name").as_string());
        std::string modulePath = trim(libraryNode.attribute("path").as_string());
        bool required = parseBoolText(libraryNode.attribute("required").as_string(), false);
        if (libraryNode.attribute("optional"))
        {
            required = !parseBoolText(libraryNode.attribute("optional").as_string(), false);
        }

        if (modulePath.empty())
        {
            if (const pugi::xml_node pathNode = libraryNode.child("path"))
            {
                modulePath = trim(pathNode.text().as_string());
            }
        }
        if (moduleName.empty() && !modulePath.empty())
        {
            moduleName = std::filesystem::path(modulePath).stem().string();
        }
        if (moduleName.empty())
        {
            moduleName = trim(libraryNode.text().as_string());
        }
        if (modulePath.empty())
        {
            modulePath = moduleName;
        }

        const std::string resolvedModulePath = resolvePathAgainstBase(configPath, modulePath);
        if (!loadRuntimeModule(moduleName, resolvedModulePath, required))
        {
            return false;
        }
    }

    return true;
}

bool LDds::loadRuntimeModule(
    const std::string & moduleName,
    const std::string & modulePath,
    bool                required)
{
    if (modulePath.empty())
    {
        if (!required)
        {
            return true;
        }
        setLastError("runtime module path is empty: " + moduleName);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_runtimeModuleMutex);
        for (const RuntimeModuleHandle & loadedModule : m_runtimeModules)
        {
            if (loadedModule.modulePath == modulePath ||
                (!moduleName.empty() && loadedModule.moduleName == moduleName))
            {
                return true;
            }
        }
    }

    std::error_code ec;
    if (!std::filesystem::exists(modulePath, ec) || ec)
    {
        if (!required)
        {
            return true;
        }
        setLastError("runtime module not found: " + modulePath);
        return false;
    }

    void * handle = loadSharedModuleHandle(modulePath);
    if (handle == nullptr)
    {
        if (!required)
        {
            return true;
        }
        setLastError("failed to load runtime module: " + modulePath + ", error=" + getSharedModuleErrorText());
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_runtimeModuleMutex);
        m_runtimeModules.push_back(RuntimeModuleHandle{moduleName, modulePath, handle});
    }

    if (!applyGeneratedModules())
    {
        {
            std::lock_guard<std::mutex> lock(m_runtimeModuleMutex);
            m_runtimeModules.erase(
                std::remove_if(
                    m_runtimeModules.begin(),
                    m_runtimeModules.end(),
                    [&modulePath](const RuntimeModuleHandle & loadedModule) {
                        return loadedModule.modulePath == modulePath;
                    }),
                m_runtimeModules.end());
        }
        unloadSharedModuleHandle(handle);
        return false;
    }

    rememberRegisteredTopics();
    return true;
}

void LDds::clearRuntimeModules() noexcept
{
    std::vector<RuntimeModuleHandle> modules;
    {
        std::lock_guard<std::mutex> lock(m_runtimeModuleMutex);
        modules.swap(m_runtimeModules);
    }

    for (auto it = modules.rbegin(); it != modules.rend(); ++it)
    {
        unloadSharedModuleHandle(it->handle);
    }
}

std::string LDds::currentExecutableDirectory()
{
    return currentExecutableDirectoryImpl();
}

std::string LDds::resolveRuntimePath(const std::string & relativePath)
{
    return resolvePathAgainstBase(currentExecutableDirectory(), relativePath);
}

std::string LDds::resolvePathAgainstBase(
    const std::string & basePath,
    const std::string & candidatePath)
{
    if (candidatePath.empty())
    {
        return std::string();
    }

    const std::filesystem::path candidate(candidatePath);
    if (candidate.is_absolute())
    {
        return candidate.lexically_normal().string();
    }

    std::vector<std::filesystem::path> candidates;
    if (!basePath.empty())
    {
        std::filesystem::path base(basePath);
        if (base.has_extension())
        {
            base = base.parent_path();
        }
        candidates.push_back(base / candidate);
    }

    candidates.push_back(std::filesystem::current_path() / candidate);
    candidates.push_back(std::filesystem::path(currentExecutableDirectory()) / candidate);
    candidates.push_back(candidate);

    const std::string resolved = resolveExistingPath(candidates);
    if (!resolved.empty())
    {
        return resolved;
    }

    return candidates.front().lexically_normal().string();
}

bool LDds::createTransportFromQos(const LQos & qos, const TransportConfig & transportConfig)
{
    const TransportProtocol protocol =
        (qos.transportType == TransportType::TCP) ? TransportProtocol::TCP : TransportProtocol::UDP;

    m_pTransport = ITransport::createTransport(protocol);
    if (!m_pTransport)
    {
        setLastError("failed to create transport");
        return false;
    }

    m_pTransport->setConfig(transportConfig);
    return true;
}

bool LDds::sendMessageThroughTransport(
    const LMessage & message,
    const LHostAddress * targetAddress,
    quint16 targetPort)
{
    if (!m_pTransport)
    {
        return false;
    }

    LMessage outgoing = message;
    if (!applyOutgoingSecurity(outgoing))
    {
        return false;
    }

    bool sent = false;
    if (targetAddress != nullptr && !targetAddress->isNull() && targetPort != 0)
    {
        if (m_pTransport->getProtocol() == TransportProtocol::UDP)
        {
            auto * udpTransport = dynamic_cast<LUdpTransport *>(m_pTransport.get());
            if (udpTransport != nullptr)
            {
                sent = udpTransport->sendMessageTo(outgoing, *targetAddress, targetPort);
            }
            else
            {
                sent = m_pTransport->sendMessage(outgoing);
            }
        }
        else
        {
            sent = m_pTransport->sendMessage(outgoing);
        }
    }
    else
    {
        sent = m_pTransport->sendMessage(outgoing);
    }

    if (sent)
    {
        m_metrics.sentMessages.fetch_add(1);
        m_metrics.sentBytes.fetch_add(static_cast<uint64_t>(outgoing.getTotalSize()));
        emitStructuredLog(
            "info",
            "transport",
            "send",
            &outgoing,
            targetAddress,
            targetPort);
    }

    return sent;
}

void LDds::initializeReliableState()
{
    const auto now = std::chrono::steady_clock::now();
    const bool reliableEnabled =
        m_qos.reliable &&
        m_pTransport &&
        m_pTransport->getProtocol() == TransportProtocol::UDP;

    const int32_t deadlineMs = resolveDeadlineMs(m_qos);
    int32_t retransmitMs = RELIABLE_DEFAULT_RETRANSMIT_MS;
    if (deadlineMs > 0)
    {
        retransmitMs = std::max(
            RELIABLE_MIN_RETRANSMIT_MS,
            std::min(RELIABLE_MAX_RETRANSMIT_MS, deadlineMs / 2));
    }
    const int32_t heartbeatProbeMs = std::max(
        RELIABLE_MIN_HEARTBEAT_PROBE_MS,
        retransmitMs * 2);

    const uint32_t historyDepth = static_cast<uint32_t>(
        std::max(1, m_qos.historyDepth));
    const uint32_t windowSize = std::max(
        RELIABLE_MIN_WINDOW_SIZE,
        std::min(RELIABLE_MAX_WINDOW_SIZE, historyDepth * 8U));

    quint16 bindPort = 0;
    std::string bindAddress;
    if (m_pTransport)
    {
        bindPort = m_pTransport->getBoundPort();
        const TransportConfig config = m_pTransport->getConfig();
        if (bindPort == 0)
        {
            bindPort = config.bindPort;
        }
        bindAddress = config.bindAddress.toStdString();
    }

    const std::string writerSeed =
        std::to_string(static_cast<uint32_t>(m_effectiveDomainId)) +
        "|" + bindAddress +
        "|" + std::to_string(static_cast<uint32_t>(bindPort)) +
        "|" + std::to_string(static_cast<uintptr_t>(reinterpret_cast<uintptr_t>(this)));
    const uint32_t writerId = fnv1aHash32(writerSeed);

    std::lock_guard<std::mutex> lock(m_reliableMutex);
    m_reliableUdpEnabled = reliableEnabled;
    m_reliableWriterId = writerId;
    m_reliableRetransmitInterval = std::chrono::milliseconds(retransmitMs);
    m_reliableHeartbeatProbeInterval = std::chrono::milliseconds(heartbeatProbeMs);
    m_reliableWindowSize = windowSize;
    m_reliableMaxResendCount = RELIABLE_DEFAULT_MAX_RESEND;
    m_lastReliableHeartbeatProbe = now;
    m_reliablePending.clear();
    m_reliableReceivers.clear();
}

void LDds::clearReliableState() noexcept
{
    std::lock_guard<std::mutex> lock(m_reliableMutex);
    m_reliableUdpEnabled = false;
    m_reliableWriterId = 1;
    m_reliablePending.clear();
    m_reliableReceivers.clear();
    m_lastReliableHeartbeatProbe = std::chrono::steady_clock::now();
}

void LDds::processReliableOutgoing(const std::chrono::steady_clock::time_point & now)
{
    if (!m_running.load() || !m_pTransport)
    {
        return;
    }

    std::vector<LMessage> retryMessages;
    bool shouldProbe = false;
    uint64_t firstPendingSeq = 0;
    uint64_t lastPendingSeq = 0;

    {
        std::lock_guard<std::mutex> lock(m_reliableMutex);
        if (!m_reliableUdpEnabled)
        {
            return;
        }

        for (auto it = m_reliablePending.begin(); it != m_reliablePending.end();)
        {
            ReliablePendingEntry & entry = it->second;
            if (now - entry.lastSendAt >= m_reliableRetransmitInterval)
            {
                if (m_reliableMaxResendCount > 0 && entry.resendCount >= m_reliableMaxResendCount)
                {
                    m_metrics.estimatedDrops.fetch_add(1);
                    setLastError(
                        "reliable udp drop pending seq=" + std::to_string(it->first) +
                        ", maxResendCount=" + std::to_string(m_reliableMaxResendCount));
                    it = m_reliablePending.erase(it);
                    continue;
                }

                entry.lastSendAt = now;
                ++entry.resendCount;
                m_metrics.retransmitCount.fetch_add(1);
                retryMessages.push_back(entry.message);
            }
            ++it;
        }

        if (!m_reliablePending.empty() &&
            now - m_lastReliableHeartbeatProbe >= m_reliableHeartbeatProbeInterval)
        {
            firstPendingSeq = m_reliablePending.begin()->first;
            lastPendingSeq = m_reliablePending.rbegin()->first;
            m_lastReliableHeartbeatProbe = now;
            shouldProbe = true;
        }
    }

    for (const LMessage & pending : retryMessages)
    {
        emitStructuredLog("info", "reliable", "retransmit", &pending);
        if (!sendMessageThroughTransport(pending))
        {
            setLastError(
                "reliable udp retransmit failed seq=" + std::to_string(pending.getSequence()) +
                ", error=" + m_pTransport->getLastError().toStdString());
        }
    }

    if (shouldProbe)
    {
        LMessage heartbeatReq = LMessage::makeHeartbeatReq(m_reliableWriterId, firstPendingSeq, lastPendingSeq);
        heartbeatReq.setDomainId(static_cast<uint8_t>(m_effectiveDomainId));
        heartbeatReq.setSequence(m_sequence.fetch_add(1) + 1);
        if (!sendMessageThroughTransport(heartbeatReq))
        {
            setLastError(
                "reliable udp heartbeat probe failed: " + m_pTransport->getLastError().toStdString());
        }
    }
}

void LDds::handleReliableControlMessage(
    const LMessage & message,
    const LHostAddress & senderAddress,
    quint16 senderPort)
{
    if (!m_running.load())
    {
        return;
    }

    bool reliableUdpEnabled = false;
    {
        std::lock_guard<std::mutex> lock(m_reliableMutex);
        reliableUdpEnabled = m_reliableUdpEnabled;
    }
    if (!reliableUdpEnabled)
    {
        return;
    }

    if (message.isHeartbeat())
    {
        std::lock_guard<std::mutex> lock(m_qosMutex);
        m_lastHeartbeatReceive = std::chrono::steady_clock::now();
    }

    const LMessageType type = message.getMessageType();
    if (type == LMessageType::Ack || type == LMessageType::HeartbeatRsp)
    {
        const uint32_t targetWriterId = message.getWriterId();
        if (targetWriterId != 0 && targetWriterId != m_reliableWriterId)
        {
            return;
        }

        const uint64_t ackSeq = message.getAckSeq() > 0 ? message.getAckSeq() : message.getSequence();
        std::lock_guard<std::mutex> lock(m_reliableMutex);
        for (auto it = m_reliablePending.begin(); it != m_reliablePending.end() && it->first <= ackSeq;)
        {
            it = m_reliablePending.erase(it);
        }
        return;
    }

    if (type == LMessageType::Nack)
    {
        const uint32_t targetWriterId = message.getWriterId();
        if (targetWriterId != 0 && targetWriterId != m_reliableWriterId)
        {
            return;
        }

        const uint64_t first = message.getFirstSeq() > 0 ? message.getFirstSeq() : 1;
        const uint64_t last = message.getLastSeq() >= first
            ? message.getLastSeq()
            : std::numeric_limits<uint64_t>::max();
        std::vector<LMessage> nackedMessages;

        {
            std::lock_guard<std::mutex> lock(m_reliableMutex);
            for (auto it = m_reliablePending.lower_bound(first);
                 it != m_reliablePending.end() && it->first <= last;
                 ++it)
            {
                it->second.lastSendAt = std::chrono::steady_clock::now();
                ++it->second.resendCount;
                m_metrics.retransmitCount.fetch_add(1);
                nackedMessages.push_back(it->second.message);
            }
        }

        for (const LMessage & pending : nackedMessages)
        {
            (void)sendMessageThroughTransport(pending);
        }
        return;
    }

    if (type != LMessageType::HeartbeatReq && type != LMessageType::Heartbeat)
    {
        return;
    }

    const uint32_t remoteWriterId = resolveReliableWriterId(message, senderAddress, senderPort);
    uint64_t ackSeq = 0;
    uint64_t windowStart = 1;

    {
        std::lock_guard<std::mutex> lock(m_reliableMutex);
        ReliableReceiveState & state = m_reliableReceivers[remoteWriterId];
        if (state.expectedSeq == 0)
        {
            state.expectedSeq = (message.getFirstSeq() > 0) ? message.getFirstSeq() : 1;
        }
        windowStart = state.expectedSeq;
        ackSeq = (state.expectedSeq > 0) ? (state.expectedSeq - 1) : 0;
    }

    const LMessage reply = (type == LMessageType::HeartbeatReq)
        ? LMessage::makeHeartbeatRsp(remoteWriterId, ackSeq, windowStart, m_reliableWindowSize)
        : LMessage::makeAck(remoteWriterId, ackSeq, windowStart, m_reliableWindowSize);

    LMessage response = reply;
    response.setDomainId(static_cast<uint8_t>(m_effectiveDomainId));
    response.setSequence(m_sequence.fetch_add(1) + 1);
    (void)sendMessageThroughTransport(response, &senderAddress, senderPort);
}

void LDds::handleReliableDataMessage(
    const LMessage & message,
    const LHostAddress & senderAddress,
    quint16 senderPort)
{
    if (!m_running.load())
    {
        return;
    }

    const uint32_t writerId = resolveReliableWriterId(message, senderAddress, senderPort);
    const uint64_t seq = message.getLastSeq() > 0 ? message.getLastSeq() : message.getSequence();
    if (seq == 0)
    {
        return;
    }

    bool reliableUdpEnabled = false;
    {
        std::lock_guard<std::mutex> lock(m_reliableMutex);
        reliableUdpEnabled = m_reliableUdpEnabled;
    }
    if (!reliableUdpEnabled)
    {
        deliverDataMessage(message);
        return;
    }

    std::vector<LMessage> readyToDeliver;
    bool shouldAck = false;
    uint64_t ackSeq = 0;
    uint64_t windowStart = 1;
    bool shouldSendHeartbeatReq = false;
    uint64_t heartbeatFirst = 0;
    uint64_t heartbeatLast = 0;

    {
        std::lock_guard<std::mutex> lock(m_reliableMutex);
        ReliableReceiveState & state = m_reliableReceivers[writerId];
        if (state.expectedSeq == 0)
        {
            state.expectedSeq = seq;
        }

        const uint64_t maxAcceptableSeq =
            state.expectedSeq + static_cast<uint64_t>(std::max(1U, m_reliableWindowSize));

        if (seq < state.expectedSeq)
        {
            shouldAck = true;
        }
        else if (seq > maxAcceptableSeq)
        {
            shouldAck = true;
            shouldSendHeartbeatReq = true;
            heartbeatFirst = state.expectedSeq;
            heartbeatLast = seq;
        }
        else if (seq == state.expectedSeq)
        {
            readyToDeliver.push_back(message);
            ++state.expectedSeq;

            while (true)
            {
                const auto buffered = state.bufferedMessages.find(state.expectedSeq);
                if (buffered == state.bufferedMessages.end())
                {
                    break;
                }

                readyToDeliver.push_back(buffered->second);
                state.bufferedMessages.erase(buffered);
                ++state.expectedSeq;
            }
            shouldAck = true;
        }
        else
        {
            const auto insertResult = state.bufferedMessages.emplace(seq, message);
            shouldAck = true;
            if (!insertResult.second)
            {
                // Duplicate in receive window; ACK current cumulative sequence.
            }
        }

        while (state.bufferedMessages.size() > static_cast<size_t>(std::max(1U, m_reliableWindowSize)))
        {
            auto newest = state.bufferedMessages.end();
            --newest;
            state.bufferedMessages.erase(newest);
        }

        windowStart = state.expectedSeq;
        ackSeq = (state.expectedSeq > 0) ? (state.expectedSeq - 1) : 0;
    }

    for (const LMessage & pending : readyToDeliver)
    {
        deliverDataMessage(pending);
    }

    if (shouldSendHeartbeatReq)
    {
        LMessage heartbeatReq = LMessage::makeHeartbeatReq(writerId, heartbeatFirst, heartbeatLast);
        heartbeatReq.setDomainId(static_cast<uint8_t>(m_effectiveDomainId));
        heartbeatReq.setSequence(m_sequence.fetch_add(1) + 1);
        (void)sendMessageThroughTransport(heartbeatReq, &senderAddress, senderPort);
    }

    if (shouldAck)
    {
        LMessage ack = LMessage::makeAck(writerId, ackSeq, windowStart, m_reliableWindowSize);
        ack.setDomainId(static_cast<uint8_t>(m_effectiveDomainId));
        ack.setSequence(m_sequence.fetch_add(1) + 1);
        (void)sendMessageThroughTransport(ack, &senderAddress, senderPort);
    }
}

void LDds::deliverDataMessage(const LMessage & message)
{
    const uint32_t topic = message.getTopic();
    if (topic == 0)
    {
        return;
    }

    if (!shouldAcceptMessageByOwnership(message, topic))
    {
        return;
    }

    DdsSampleMetadata sampleMetadata;
    std::vector<uint8_t> payload;
    if (!decodeSampleEnvelope(message.getPayload(), sampleMetadata, payload))
    {
        payload = message.getPayload();
        sampleMetadata.sequence = message.getSequence();
    }
    if (sampleMetadata.sequence == 0)
    {
        sampleMetadata.sequence = message.getSequence();
    }

    auto object = m_pTypeRegistry->createByTopic(topic);
    if (!object)
    {
        return;
    }

    if (!m_pTypeRegistry->deserializeByTopic(topic, payload, object.get()))
    {
        return;
    }

    const std::string dataType = m_pTypeRegistry->getTypeNameByTopic(topic);
    m_domain.cacheTopicData(topic, payload, dataType, sampleMetadata);
    markTopicActivity(topic);
    const LHostAddress senderAddress = message.getSenderAddress();
    emitStructuredLog(
        "info",
        "dispatch",
        "deliver",
        &message,
        &senderAddress,
        message.getSenderPort());

    std::vector<TopicCallback> callbacks;
    {
        std::lock_guard<std::mutex> lock(m_subscribersMutex);
        const auto it = m_subscribers.find(topic);
        if (it != m_subscribers.end())
        {
            callbacks = it->second;
        }
    }

    for (auto & callback : callbacks)
    {
        if (callback)
        {
            callback(topic, object);
        }
    }
}

bool LDds::isDiscoveryMessage(const LMessage & message) const noexcept
{
    return message.getMessageType() == LMessageType::DiscoveryAnnounce;
}

bool LDds::encodeDiscoveryAnnounce(std::vector<uint8_t> & payload) const
{
    payload.clear();

    quint16 endpointPort = 0;
    bool discoveryEnabled = false;
    bool useMulticast = false;
    uint32_t nodeId = 0;
    quint16 announcePort = 0;
    {
        std::lock_guard<std::mutex> lock(m_discoveryMutex);
        discoveryEnabled = m_discoveryEnabled;
        useMulticast = m_discoveryUseMulticast;
        nodeId = m_discoveryNodeId;
        announcePort = m_discoveryPort;
    }

    if (!discoveryEnabled || !m_pTransport)
    {
        return false;
    }

    endpointPort = m_pTransport->getBoundPort();
    if (endpointPort == 0)
    {
        endpointPort = announcePort;
    }
    if (endpointPort == 0)
    {
        return false;
    }

    const std::vector<uint32_t> topics = snapshotKnownTopics();
    const uint16_t topicCount = static_cast<uint16_t>(
        std::min(static_cast<size_t>(DISCOVERY_MAX_TOPICS), topics.size()));

    uint32_t capabilities = 0;
    if (m_qos.reliable && m_pTransport->getProtocol() == TransportProtocol::UDP)
    {
        capabilities |= DISCOVERY_CAP_RELIABLE_UDP;
    }
    if (m_pTransport->getProtocol() == TransportProtocol::TCP)
    {
        capabilities |= DISCOVERY_CAP_TCP;
    }
    if (useMulticast)
    {
        capabilities |= DISCOVERY_CAP_MULTICAST;
    }

    appendU8(payload, DISCOVERY_ANNOUNCE_VERSION);
    appendU32(payload, nodeId);
    appendU8(payload, static_cast<uint8_t>(m_effectiveDomainId));
    appendU16(payload, endpointPort);
    appendU32(payload, capabilities);
    appendU16(payload, topicCount);
    for (size_t i = 0; i < topicCount; ++i)
    {
        appendU32(payload, topics[i]);
    }

    return true;
}

bool LDds::decodeDiscoveryAnnounce(
    const LMessage & message,
    DiscoveryAnnounce & announce) const
{
    if (!isDiscoveryMessage(message))
    {
        return false;
    }

    const std::vector<uint8_t> & payload = message.getPayload();
    size_t offset = 0;
    uint16_t topicCount = 0;

    if (!readU8(payload, offset, announce.version))
    {
        return false;
    }
    if (!readU32(payload, offset, announce.nodeId))
    {
        return false;
    }
    if (!readU8(payload, offset, announce.domainId))
    {
        return false;
    }
    if (!readU16(payload, offset, announce.endpointPort))
    {
        return false;
    }
    if (!readU32(payload, offset, announce.capabilities))
    {
        return false;
    }
    if (!readU16(payload, offset, topicCount))
    {
        return false;
    }
    if (topicCount > DISCOVERY_MAX_TOPICS)
    {
        return false;
    }

    announce.topics.clear();
    announce.topics.reserve(topicCount);
    for (uint16_t i = 0; i < topicCount; ++i)
    {
        uint32_t topic = 0;
        if (!readU32(payload, offset, topic))
        {
            return false;
        }
        if (topic != 0)
        {
            announce.topics.push_back(topic);
        }
    }

    return true;
}

void LDds::handleDiscoveryMessage(
    const LMessage & message,
    const LHostAddress & senderAddress,
    quint16 senderPort)
{
    DiscoveryAnnounce announce;
    if (!decodeDiscoveryAnnounce(message, announce))
    {
        return;
    }

    if (announce.version == 0U || announce.version > DISCOVERY_ANNOUNCE_VERSION)
    {
        return;
    }
    if (announce.domainId != static_cast<uint8_t>(m_effectiveDomainId))
    {
        return;
    }

    uint32_t localNodeId = 0;
    {
        std::lock_guard<std::mutex> lock(m_discoveryMutex);
        localNodeId = m_discoveryNodeId;
    }
    if (announce.nodeId == 0 || announce.nodeId == localNodeId)
    {
        return;
    }

    const quint16 endpointPort = (announce.endpointPort != 0) ? announce.endpointPort : senderPort;
    if (endpointPort == 0 || senderAddress.isNull())
    {
        return;
    }

    bool isNewPeer = false;
    bool endpointChanged = false;
    {
        std::lock_guard<std::mutex> lock(m_discoveryMutex);
        auto it = m_discoveryPeers.find(announce.nodeId);
        if (it == m_discoveryPeers.end())
        {
            DiscoveryPeerInfo peer;
            peer.address = senderAddress;
            peer.endpointPort = endpointPort;
            peer.lastSeen = std::chrono::steady_clock::now();
            peer.topics = announce.topics;
            peer.capabilities = announce.capabilities;
            peer.online = true;
            m_discoveryPeers.emplace(announce.nodeId, std::move(peer));
            isNewPeer = true;
        }
        else
        {
            endpointChanged = (it->second.address != senderAddress) || (it->second.endpointPort != endpointPort);
            it->second.address = senderAddress;
            it->second.endpointPort = endpointPort;
            it->second.lastSeen = std::chrono::steady_clock::now();
            it->second.topics = announce.topics;
            it->second.capabilities = announce.capabilities;
            it->second.online = true;
        }
    }

    if (isNewPeer || endpointChanged)
    {
        setLastError(
            "discovery peer online nodeId=" + std::to_string(announce.nodeId) +
            ", endpoint=" + senderAddress.toString().toStdString() +
            ":" + std::to_string(static_cast<uint32_t>(endpointPort)));
    }
}

void LDds::initializeDiscoveryState(const TransportConfig & transportConfig)
{
    const auto now = std::chrono::steady_clock::now();

    bool enabled =
        m_pTransport &&
        m_pTransport->getProtocol() == TransportProtocol::UDP &&
        transportConfig.enableDiscovery;

    quint16 discoveryPort = transportConfig.discoveryPort;
    if (discoveryPort == 0 && m_pTransport)
    {
        discoveryPort = m_pTransport->getBoundPort();
    }
    if (discoveryPort == 0)
    {
        discoveryPort = transportConfig.bindPort;
    }

    int intervalMs = std::max(DISCOVERY_MIN_INTERVAL_MS, transportConfig.discoveryIntervalMs);
    int peerTtlMs = std::max(DISCOVERY_MIN_PEER_TTL_MS, transportConfig.peerTtlMs);
    if (peerTtlMs < intervalMs * 2)
    {
        peerTtlMs = intervalMs * 2;
    }

    const quint16 seedPort =
        (m_pTransport != nullptr && m_pTransport->getBoundPort() != 0)
            ? m_pTransport->getBoundPort()
            : transportConfig.bindPort;
    const uint32_t processId = currentProcessIdValue();
    const std::string nodeSeed =
        "discovery|" + std::to_string(static_cast<uint32_t>(m_effectiveDomainId)) +
        "|" + std::to_string(static_cast<uint32_t>(seedPort)) +
        "|" + std::to_string(processId) +
        "|" + std::to_string(static_cast<uintptr_t>(reinterpret_cast<uintptr_t>(this)));
    const uint32_t nodeId = fnv1aHash32(nodeSeed);

    LHostAddress multicastGroup;
    bool useMulticast = enabled && transportConfig.enableMulticast;
    if (useMulticast)
    {
        if (!transportConfig.multicastGroup.isEmpty())
        {
            multicastGroup = LHostAddress(transportConfig.multicastGroup);
        }
        if (multicastGroup.isNull())
        {
            multicastGroup = resolveDomainMulticastGroup(m_effectiveDomainId);
        }
        if (!multicastGroup.isMulticast())
        {
            useMulticast = false;
        }
    }

    std::lock_guard<std::mutex> lock(m_discoveryMutex);
    m_discoveryEnabled = enabled;
    m_discoveryUseMulticast = useMulticast;
    m_discoveryNodeId = nodeId;
    m_discoveryPort = discoveryPort;
    m_discoveryInterval = std::chrono::milliseconds(intervalMs);
    m_peerTtl = std::chrono::milliseconds(peerTtlMs);
    m_lastDiscoverySend = now - m_discoveryInterval;
    m_discoveryMulticastGroup = multicastGroup;
    m_discoveryPeers.clear();
}

void LDds::clearDiscoveryState() noexcept
{
    std::lock_guard<std::mutex> lock(m_discoveryMutex);
    m_discoveryEnabled = false;
    m_discoveryUseMulticast = false;
    m_discoveryNodeId = 1;
    m_discoveryPort = 0;
    m_discoveryPeers.clear();
    m_discoveryMulticastGroup.clear();
}

void LDds::processDiscovery(const std::chrono::steady_clock::time_point & now)
{
    if (!m_running.load() || !m_pTransport)
    {
        return;
    }

    bool discoveryEnabled = false;
    bool shouldAnnounce = false;
    bool useMulticast = false;
    quint16 discoveryPort = 0;
    LHostAddress multicastGroup;
    std::vector<uint32_t> expiredPeers;

    {
        std::lock_guard<std::mutex> lock(m_discoveryMutex);
        discoveryEnabled = m_discoveryEnabled;
        if (!discoveryEnabled)
        {
            return;
        }

        for (auto it = m_discoveryPeers.begin(); it != m_discoveryPeers.end();)
        {
            if (now - it->second.lastSeen > m_peerTtl)
            {
                expiredPeers.push_back(it->first);
                it = m_discoveryPeers.erase(it);
            }
            else
            {
                ++it;
            }
        }

        if (now - m_lastDiscoverySend >= m_discoveryInterval)
        {
            shouldAnnounce = true;
            m_lastDiscoverySend = now;
        }

        useMulticast = m_discoveryUseMulticast;
        multicastGroup = m_discoveryMulticastGroup;
        discoveryPort = m_discoveryPort;
    }

    for (uint32_t nodeId : expiredPeers)
    {
        setLastError("discovery peer offline nodeId=" + std::to_string(nodeId));
    }

    if (!shouldAnnounce || discoveryPort == 0)
    {
        return;
    }

    std::vector<uint8_t> payload;
    if (!encodeDiscoveryAnnounce(payload))
    {
        return;
    }

    const uint64_t sequence = m_sequence.fetch_add(1) + 1;
    LMessage announce(HEARTBEAT_TOPIC_ID, sequence, payload, LMessageType::DiscoveryAnnounce);
    announce.setDomainId(static_cast<uint8_t>(m_effectiveDomainId));
    announce.setWriterId(m_discoveryNodeId);

    const TransportConfig config = m_pTransport->getConfig();
    if (useMulticast && !multicastGroup.isNull())
    {
        (void)sendMessageThroughTransport(announce, &multicastGroup, discoveryPort);
    }
    if (config.enableBroadcast)
    {
        const LHostAddress broadcastAddress = LHostAddress::Broadcast;
        (void)sendMessageThroughTransport(announce, &broadcastAddress, discoveryPort);
    }
}

std::vector<std::pair<LHostAddress, quint16>> LDds::snapshotDiscoveryTargets(uint32_t topic) const
{
    std::vector<std::pair<LHostAddress, quint16>> targets;
    const auto now = std::chrono::steady_clock::now();

    std::lock_guard<std::mutex> lock(m_discoveryMutex);
    if (!m_discoveryEnabled)
    {
        return targets;
    }

    for (const auto & pair : m_discoveryPeers)
    {
        const DiscoveryPeerInfo & peer = pair.second;
        if (!peer.online || peer.endpointPort == 0 || peer.address.isNull())
        {
            continue;
        }
        if (now - peer.lastSeen > m_peerTtl)
        {
            continue;
        }

        if (!peer.topics.empty() &&
            std::find(peer.topics.begin(), peer.topics.end(), topic) == peer.topics.end())
        {
            continue;
        }

        targets.push_back({peer.address, peer.endpointPort});
    }

    return targets;
}

int32_t LDds::resolveTopicDeadlineMs(uint32_t topic) const
{
    const auto it = m_topicQosOverrides.find(topic);
    if (it != m_topicQosOverrides.end() && it->second.deadlineMs > 0)
    {
        return it->second.deadlineMs;
    }
    return resolveDeadlineMs(m_qos);
}

void LDds::rememberKnownTopic(uint32_t topic)
{
    if (topic == 0)
    {
        return;
    }
    std::lock_guard<std::mutex> lock(m_knownTopicsMutex);
    m_knownTopics.insert(topic);
}

std::vector<uint32_t> LDds::snapshotKnownTopics() const
{
    std::vector<uint32_t> topics;
    std::lock_guard<std::mutex> lock(m_knownTopicsMutex);
    topics.reserve(m_knownTopics.size());
    for (const uint32_t topic : m_knownTopics)
    {
        if (topic != 0)
        {
            topics.push_back(topic);
        }
    }
    std::sort(topics.begin(), topics.end());
    return topics;
}

bool LDds::shouldAcceptMessageByOwnership(const LMessage & message, uint32_t topic)
{
    const OwnershipQosPolicy ownership = m_qos.getOwnership();
    if (!ownership.enabled || ownership.kind == OwnershipKind::Shared)
    {
        return true;
    }

    uint32_t writerId = message.getWriterId();
    if (writerId == 0)
    {
        writerId = resolveReliableWriterId(message, message.getSenderAddress(), message.getSenderPort());
    }
    const uint32_t writerStrength = static_cast<uint32_t>(message.getAckSeq());

    std::lock_guard<std::mutex> lock(m_ownershipMutex);
    auto it = m_topicOwnership.find(topic);
    if (it == m_topicOwnership.end())
    {
        m_topicOwnership.emplace(
            topic,
            OwnershipTopicState{writerId, writerStrength, std::chrono::steady_clock::now()});
        return true;
    }

    OwnershipTopicState & current = it->second;
    if (current.writerId == writerId)
    {
        current.strength = writerStrength;
        current.lastSeen = std::chrono::steady_clock::now();
        return true;
    }

    const bool stronger = writerStrength > current.strength;
    const bool tieBreakTakeover = (writerStrength == current.strength) && (writerId > current.writerId);
    if (stronger || tieBreakTakeover)
    {
        current.writerId = writerId;
        current.strength = writerStrength;
        current.lastSeen = std::chrono::steady_clock::now();
        return true;
    }
    return false;
}

void LDds::initializeQosHotReload(const std::string & qosXmlPath)
{
    std::lock_guard<std::mutex> lock(m_qosReloadMutex);
    m_qosXmlPath = qosXmlPath;
    m_qosLastWriteTick = getFileWriteTick(qosXmlPath);
    m_qosHotReloadEnabled = !qosXmlPath.empty() && (m_qosLastWriteTick >= 0);
    m_qosReloadInterval = std::chrono::milliseconds(QOS_HOT_RELOAD_INTERVAL_MS);
    m_lastQosReloadCheck = std::chrono::steady_clock::now();
}

void LDds::clearQosHotReloadState() noexcept
{
    std::lock_guard<std::mutex> lock(m_qosReloadMutex);
    m_qosHotReloadEnabled = false;
    m_qosXmlPath.clear();
    m_qosLastWriteTick = -1;
    m_lastQosReloadCheck = std::chrono::steady_clock::now();
}

void LDds::processQosHotReload(const std::chrono::steady_clock::time_point & now)
{
    std::string qosPath;
    int64_t lastWriteTick = -1;
    {
        std::lock_guard<std::mutex> lock(m_qosReloadMutex);
        if (!m_qosHotReloadEnabled)
        {
            return;
        }

        if (now - m_lastQosReloadCheck < m_qosReloadInterval)
        {
            return;
        }

        m_lastQosReloadCheck = now;
        qosPath = m_qosXmlPath;
        lastWriteTick = m_qosLastWriteTick;
    }

    const int64_t newWriteTick = getFileWriteTick(qosPath);
    if (newWriteTick < 0 || newWriteTick == lastWriteTick)
    {
        return;
    }

    LQos loadedQos;
    std::string loadError;
    if (!loadedQos.loadFromXmlFile(qosPath, &loadError))
    {
        setLastError("qos hot reload parse failed: " + loadError);
        return;
    }

    if (!applyHotReloadQos(loadedQos))
    {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_qosReloadMutex);
        m_qosLastWriteTick = newWriteTick;
    }
}

bool LDds::applyHotReloadQos(const LQos & loadedQos)
{
    bool reliabilityChanged = false;
    {
        std::lock_guard<std::mutex> lock(m_qosMutex);
        if (loadedQos.deadlineMs >= 0 && loadedQos.deadlineMs != m_qos.deadlineMs)
        {
            m_qos.deadlineMs = loadedQos.deadlineMs;
            DeadlineQosPolicy deadline = m_qos.getDeadline();
            deadline.enabled = (m_qos.deadlineMs > 0);
            deadline.period = Duration(m_qos.deadlineMs > 0 ? static_cast<int64_t>(m_qos.deadlineMs / 1000) : DURATION_INFINITY);
            if (m_qos.deadlineMs > 0)
            {
                deadline.period.seconds = static_cast<int64_t>(m_qos.deadlineMs / 1000);
                deadline.period.nanoseconds = static_cast<uint32_t>((m_qos.deadlineMs % 1000) * 1000000);
            }
            m_qos.setDeadline(deadline);
        }

        if (loadedQos.historyDepth > 0 && loadedQos.historyDepth != m_qos.historyDepth)
        {
            m_qos.historyDepth = loadedQos.historyDepth;
            HistoryQosPolicy history = m_qos.getHistory();
            history.depth = m_qos.historyDepth;
            history.enabled = true;
            m_qos.setHistory(history);
            m_domain.setHistoryDepth(static_cast<size_t>(m_qos.historyDepth));
        }

        if (loadedQos.reliable != m_qos.reliable)
        {
            m_qos.reliable = loadedQos.reliable;
            ReliabilityQosPolicy reliability = m_qos.getReliability();
            reliability.enabled = m_qos.reliable;
            reliability.kind = m_qos.reliable ? ReliabilityKind::Reliable : ReliabilityKind::BestEffort;
            m_qos.setReliability(reliability);
            reliabilityChanged = true;
        }

        const int32_t deadlineMs = resolveDeadlineMs(m_qos);
        m_deadlineCheckInterval = (deadlineMs > 0) ? std::chrono::milliseconds(100) : std::chrono::milliseconds(1000);
        m_heartbeatInterval = resolveHeartbeatInterval(deadlineMs);
    }

    if (reliabilityChanged)
    {
        initializeReliableState();
    }

    if (loadedQos.transportType != m_qos.transportType ||
        loadedQos.domainId != m_qos.domainId)
    {
        setLastError("qos hot reload ignored transport/domain changes (restart required)");
    }
    else
    {
        setLastError("qos hot reload applied (deadline/history/reliability)");
    }
    return true;
}

uint32_t LDds::resolveReliableWriterId(
    const LMessage & message,
    const LHostAddress & senderAddress,
    quint16 senderPort) const
{
    if (message.getWriterId() != 0)
    {
        return message.getWriterId();
    }

    const std::string endpoint =
        senderAddress.toString().toStdString() + ":" + std::to_string(static_cast<uint32_t>(senderPort));
    return fnv1aHash32(endpoint);
}

bool LDds::publishSerializedTopic(
    uint32_t                topic,
    std::vector<uint8_t> && payload,
    const std::string &     dataType,
    const DdsPublishOptions * publishOptions
)
{
    if (topic == 0)
    {
        setLastError("invalid topic=0");
        return false;
    }

    if (!m_running.load() || !m_pTransport)
    {
        setLastError("dds is not running");
        return false;
    }
    rememberKnownTopic(topic);

    const uint64_t sequence = m_sequence.fetch_add(1) + 1;
    DdsSampleMetadata sampleMetadata;
    sampleMetadata.sequence = sequence;
    sampleMetadata.publishTimestamp = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    {
        std::lock_guard<std::mutex> lock(m_identityMutex);
        sampleMetadata.sourceApp = m_runtimeIdentity.sourceApp;
        sampleMetadata.runId = m_runtimeIdentity.runId;
    }
    if (publishOptions != nullptr)
    {
        sampleMetadata.simTimestamp = publishOptions->simTimestamp;
        if (!publishOptions->sourceApp.empty())
        {
            sampleMetadata.sourceApp = publishOptions->sourceApp;
        }
        if (!publishOptions->runId.empty())
        {
            sampleMetadata.runId = publishOptions->runId;
        }
    }

    std::vector<uint8_t> encodedPayload;
    (void)encodeSampleEnvelope(sampleMetadata, payload, encodedPayload);

    LMessage       message(topic, sequence, encodedPayload);
    message.setDomainId(static_cast<uint8_t>(m_effectiveDomainId));
    message.setMessageType(LMessageType::Data);

    bool reliableUdpEnabled = false;
    uint32_t writerId = 0;
    uint32_t windowSize = 0;
    {
        std::lock_guard<std::mutex> lock(m_reliableMutex);
        reliableUdpEnabled = m_reliableUdpEnabled;
        writerId = m_reliableWriterId;
        windowSize = m_reliableWindowSize;
    }
    if (writerId == 0)
    {
        writerId = fnv1aHash32(std::to_string(reinterpret_cast<uintptr_t>(this)));
    }
    message.setWriterId(writerId);
    message.setAckSeq(static_cast<uint64_t>(std::max(0, m_qos.ownershipStrength)));

    bool discoveryEnabled = false;
    {
        std::lock_guard<std::mutex> lock(m_discoveryMutex);
        discoveryEnabled = m_discoveryEnabled;
    }
    const std::vector<std::pair<LHostAddress, quint16>> discoveryTargets =
        snapshotDiscoveryTargets(topic);
    const TransportConfig currentTransportConfig = m_pTransport->getConfig();
    const bool hasDefaultRemote =
        !currentTransportConfig.remoteAddress.isEmpty() && currentTransportConfig.remotePort != 0;

    if (reliableUdpEnabled)
    {
        const auto now = std::chrono::steady_clock::now();
        message.setFirstSeq(sequence);
        message.setLastSeq(sequence);
        message.setWindowStart(sequence);
        message.setWindowSize(windowSize);

        bool hasExplicitTarget = false;
        LHostAddress targetAddress;
        quint16 targetPort = 0;
        if (!hasDefaultRemote && !discoveryTargets.empty())
        {
            hasExplicitTarget = true;
            targetAddress = discoveryTargets.front().first;
            targetPort = discoveryTargets.front().second;
        }
        if (!hasDefaultRemote && !hasExplicitTarget && discoveryEnabled)
        {
            setLastError(
                "reliable udp send skipped: no discovered peer for topic=" + std::to_string(topic));
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(m_reliableMutex);
            m_reliablePending[sequence] = ReliablePendingEntry{message, now, 0U};
        }

        const bool sent = hasExplicitTarget
            ? sendMessageThroughTransport(message, &targetAddress, targetPort)
            : sendMessageThroughTransport(message);
        if (!sent)
        {
            {
                std::lock_guard<std::mutex> lock(m_reliableMutex);
                m_reliablePending.erase(sequence);
            }
            setLastError(
                "sendMessage failed (reliable udp, domain=" + std::to_string(m_effectiveDomainId) +
                "): " + m_pTransport->getLastError().toStdString());
            return false;
        }
    }
    else
    {
        bool sentAny = false;
        for (const auto & endpoint : discoveryTargets)
        {
            if (sendMessageThroughTransport(message, &endpoint.first, endpoint.second))
            {
                sentAny = true;
            }
        }

        if (hasDefaultRemote)
        {
            if (sendMessageThroughTransport(message))
            {
                sentAny = true;
            }
        }

        if (!sentAny)
        {
            if (discoveryEnabled && !hasDefaultRemote)
            {
                setLastError(
                    "sendMessage skipped: no discovered peers for topic=" + std::to_string(topic));
            }
            else
            {
                setLastError(
                    "sendMessage failed (domain=" + std::to_string(m_effectiveDomainId) + "): " +
                    m_pTransport->getLastError().toStdString());
            }
            return false;
        }
    }

    m_domain.cacheTopicData(topic, payload, dataType, sampleMetadata);
    markTopicActivity(topic);

    return true;
}

void LDds::handleTransportMessage(
    const LMessage &  message,
    const LHostAddress & senderAddress,
    quint16           senderPort
)
{
    if (message.getDomainId() != static_cast<uint8_t>(m_effectiveDomainId))
    {
        m_metrics.estimatedDrops.fetch_add(1);
        return;
    }

    LMessage workingMessage = message;
    workingMessage.setSenderAddress(senderAddress);
    workingMessage.setSenderPort(senderPort);

    try
    {
        if (!verifyIncomingSecurity(workingMessage))
        {
            return;
        }

        m_metrics.receivedMessages.fetch_add(1);
        m_metrics.receivedBytes.fetch_add(static_cast<uint64_t>(message.getTotalSize()));
        emitStructuredLog(
            "info",
            "transport",
            "receive",
            &workingMessage,
            &senderAddress,
            senderPort);

        if (workingMessage.getMessageType() == LMessageType::Data)
        {
            updateDropEstimate(workingMessage, senderAddress, senderPort);
        }

        if (isDiscoveryMessage(workingMessage))
        {
            handleDiscoveryMessage(workingMessage, senderAddress, senderPort);
            return;
        }

        bool reliableUdpEnabled = false;
        {
            std::lock_guard<std::mutex> lock(m_reliableMutex);
            reliableUdpEnabled = m_reliableUdpEnabled;
        }

        if (!reliableUdpEnabled)
        {
            if (workingMessage.isHeartbeat())
            {
                std::lock_guard<std::mutex> lock(m_qosMutex);
                m_lastHeartbeatReceive = std::chrono::steady_clock::now();
                return;
            }

            deliverDataMessage(workingMessage);
            return;
        }

        if (workingMessage.isControlMessage() || workingMessage.getTopic() == HEARTBEAT_TOPIC_ID)
        {
            handleReliableControlMessage(workingMessage, senderAddress, senderPort);
            return;
        }

        handleReliableDataMessage(workingMessage, senderAddress, senderPort);
    }
    catch (const std::exception & ex)
    {
        setLastError(std::string("handleTransportMessage exception: ") + ex.what());
    }
    catch (...)
    {
        setLastError("handleTransportMessage unknown exception");
    }
}

void LDds::markTopicActivity(uint32_t topic)
{
    if (topic == 0)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(m_qosMutex);
    m_lastTopicActivity[topic] = std::chrono::steady_clock::now();
    m_deadlineMissedTopics.erase(topic);
}

void LDds::startQosThread()
{
    if (m_qosThreadRunning.load())
    {
        return;
    }

    const int32_t deadlineMs = resolveDeadlineMs(m_qos);
    m_deadlineCheckInterval = (deadlineMs > 0) ? std::chrono::milliseconds(100) : std::chrono::milliseconds(1000);
    m_heartbeatInterval = resolveHeartbeatInterval(deadlineMs);

    m_qosThreadRunning.store(true);
    m_qosThread = std::thread(&LDds::qosThreadFunc, this);
}

void LDds::stopQosThread() noexcept
{
    m_qosThreadRunning.store(false);
    m_qosCondition.notify_all();
    if (m_qosThread.joinable())
    {
        m_qosThread.join();
    }
}

void LDds::qosThreadFunc()
{
    while (m_qosThreadRunning.load() && m_running.load())
    {
        try
        {
            const auto now = std::chrono::steady_clock::now();
            bool sendHeartbeat = false;
            std::vector<std::pair<uint32_t, uint64_t>> deadlineMissed;
            DeadlineMissedCallback deadlineCallback;

            {
                std::lock_guard<std::mutex> lock(m_qosMutex);

                if (now - m_lastHeartbeatSend >= m_heartbeatInterval)
                {
                    sendHeartbeat = true;
                    m_lastHeartbeatSend = now;
                }

                if (!m_lastTopicActivity.empty())
                {
                    for (const auto & pair : m_lastTopicActivity)
                    {
                        const uint32_t topic = pair.first;
                        const int32_t deadlineMs = resolveTopicDeadlineMs(topic);
                        if (deadlineMs <= 0)
                        {
                            continue;
                        }
                        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - pair.second).count();
                        if (elapsed > deadlineMs)
                        {
                            if (m_deadlineMissedTopics.insert(topic).second)
                            {
                                deadlineMissed.push_back({topic, static_cast<uint64_t>(elapsed)});
                            }
                        }
                    }
                }

                deadlineCallback = m_deadlineMissedCallback;
            }

            if (sendHeartbeat && m_pTransport && m_running.load())
            {
                const uint64_t sequence = m_sequence.fetch_add(1) + 1;
                const uint64_t nowMs = static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()
                );
                LMessage heartbeat = LMessage::makeHeartbeat(sequence, nowMs);
                heartbeat.setDomainId(static_cast<uint8_t>(m_effectiveDomainId));
                heartbeat.setWriterId(m_reliableWriterId);
                (void)sendMessageThroughTransport(heartbeat);
            }

            processQosHotReload(now);
            processDiscovery(now);
            processReliableOutgoing(now);
            updateRuntimeGauges();

            for (const auto & missed : deadlineMissed)
            {
                m_metrics.deadlineMissCount.fetch_add(1);
                setLastError(
                    "deadline missed topic=" + std::to_string(missed.first) +
                    ", elapsedMs=" + std::to_string(missed.second)
                );

                if (deadlineCallback)
                {
                    deadlineCallback(missed.first, missed.second);
                }
            }
        }
        catch (const std::exception & ex)
        {
            setLastError(std::string("qos thread exception: ") + ex.what());
        }
        catch (...)
        {
            setLastError("qos thread unknown exception");
        }

        std::unique_lock<std::mutex> lock(m_qosMutex);
        m_qosCondition.wait_for(
            lock,
            m_deadlineCheckInterval,
            [this] { return !m_qosThreadRunning.load() || !m_running.load(); }
        );
    }
}

LDds::SecurityRuntimeConfig LDds::snapshotSecurityConfig() const
{
    std::lock_guard<std::mutex> lock(m_securityMutex);
    return m_securityConfig;
}

bool LDds::applyOutgoingSecurity(LMessage & message)
{
    const SecurityRuntimeConfig security = snapshotSecurityConfig();
    if (!security.enabled)
    {
        return true;
    }
    if (security.psk.empty())
    {
        setLastError("security enabled but securityPsk is empty");
        return false;
    }

    std::vector<uint8_t> body = message.getPayload();
    uint8_t flags = 0U;
    if (security.encryptPayload)
    {
        flags |= SECURITY_FLAG_ENCRYPTED;
        if (!body.empty())
        {
            applySymmetricCipher(body, security.psk, message.getSequence());
        }
    }

    const uint64_t nonce = message.getSequence();
    const uint64_t authTag = computeSecurityTag(security.psk, message, flags, nonce, body);
    std::vector<uint8_t> envelope;
    envelope.reserve(SECURITY_ENVELOPE_PREFIX_SIZE + body.size());
    envelope.push_back(SECURITY_ENVELOPE_MAGIC);
    envelope.push_back(SECURITY_ENVELOPE_VERSION);
    envelope.push_back(flags);
    appendU64(envelope, nonce);
    appendU64(envelope, authTag);
    envelope.insert(envelope.end(), body.begin(), body.end());
    message.setPayload(envelope);
    return true;
}

bool LDds::verifyIncomingSecurity(LMessage & message)
{
    const SecurityRuntimeConfig security = snapshotSecurityConfig();
    if (!security.enabled)
    {
        return true;
    }
    if (security.psk.empty())
    {
        m_metrics.authRejectedCount.fetch_add(1);
        setLastError("security enabled but securityPsk is empty");
        return false;
    }

    const std::vector<uint8_t> payload = message.getPayload();
    if (payload.size() < SECURITY_ENVELOPE_PREFIX_SIZE)
    {
        m_metrics.authRejectedCount.fetch_add(1);
        m_metrics.estimatedDrops.fetch_add(1);
        setLastError("security reject: invalid envelope size");
        return false;
    }

    size_t offset = 0;
    uint8_t magic = 0;
    uint8_t version = 0;
    uint8_t flags = 0;
    uint64_t nonce = 0;
    uint64_t authTag = 0;
    if (!readU8(payload, offset, magic) ||
        !readU8(payload, offset, version) ||
        !readU8(payload, offset, flags) ||
        !readU64(payload, offset, nonce) ||
        !readU64(payload, offset, authTag))
    {
        m_metrics.authRejectedCount.fetch_add(1);
        m_metrics.estimatedDrops.fetch_add(1);
        setLastError("security reject: envelope decode failed");
        return false;
    }

    if (magic != SECURITY_ENVELOPE_MAGIC || version != SECURITY_ENVELOPE_VERSION)
    {
        m_metrics.authRejectedCount.fetch_add(1);
        m_metrics.estimatedDrops.fetch_add(1);
        setLastError("security reject: envelope magic/version mismatch");
        return false;
    }

    const bool encrypted = (flags & SECURITY_FLAG_ENCRYPTED) != 0;
    if (encrypted != security.encryptPayload)
    {
        m_metrics.authRejectedCount.fetch_add(1);
        m_metrics.estimatedDrops.fetch_add(1);
        setLastError("security reject: encryption mode mismatch");
        return false;
    }

    std::vector<uint8_t> body;
    if (offset < payload.size())
    {
        body.assign(payload.begin() + static_cast<size_t>(offset), payload.end());
    }

    const uint64_t expectedTag = computeSecurityTag(security.psk, message, flags, nonce, body);
    if (expectedTag != authTag)
    {
        m_metrics.authRejectedCount.fetch_add(1);
        m_metrics.estimatedDrops.fetch_add(1);
        setLastError("security reject: auth tag mismatch");
        return false;
    }

    if (encrypted && !body.empty())
    {
        applySymmetricCipher(body, security.psk, nonce);
    }
    message.setPayload(body);
    return true;
}

void LDds::updateDropEstimate(
    const LMessage & message,
    const LHostAddress & senderAddress,
    quint16 senderPort)
{
    const uint64_t seq = message.getSequence();
    if (seq == 0)
    {
        return;
    }

    uint32_t writerId = message.getWriterId();
    if (writerId == 0)
    {
        writerId = resolveReliableWriterId(message, senderAddress, senderPort);
    }

    std::lock_guard<std::mutex> lock(m_metricsMutex);
    auto it = m_lossEstimateByWriter.find(writerId);
    if (it == m_lossEstimateByWriter.end())
    {
        m_lossEstimateByWriter.emplace(writerId, seq);
        return;
    }

    uint64_t & lastSeq = it->second;
    if (seq > lastSeq + 1)
    {
        m_metrics.estimatedDrops.fetch_add(seq - lastSeq - 1);
        lastSeq = seq;
        return;
    }
    if (seq > lastSeq)
    {
        lastSeq = seq;
    }
}

void LDds::updateRuntimeGauges()
{
    uint64_t queueLength = 0;
    uint64_t connectionCount = 0;
    uint64_t queueDropCount = m_metrics.queueDropCount.load();

    if (m_pTransport)
    {
        if (auto * tcp = dynamic_cast<LTcpTransport *>(m_pTransport.get()))
        {
            queueLength = static_cast<uint64_t>(tcp->getPendingQueueSize());
            connectionCount = static_cast<uint64_t>(tcp->getConnectionCount());
            queueDropCount = tcp->getQueueDropCount();
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_reliableMutex);
        if (m_reliableUdpEnabled)
        {
            queueLength = std::max(
                queueLength,
                static_cast<uint64_t>(m_reliablePending.size()));
        }
    }

    m_metrics.queueLength.store(queueLength);
    m_metrics.connectionCount.store(connectionCount);
    m_metrics.queueDropCount.store(queueDropCount);
}

void LDds::resetRuntimeMetrics() noexcept
{
    m_metrics.sentMessages.store(0);
    m_metrics.receivedMessages.store(0);
    m_metrics.estimatedDrops.store(0);
    m_metrics.retransmitCount.store(0);
    m_metrics.deadlineMissCount.store(0);
    m_metrics.authRejectedCount.store(0);
    m_metrics.sentBytes.store(0);
    m_metrics.receivedBytes.store(0);
    m_metrics.queueDropCount.store(0);
    m_metrics.queueLength.store(0);
    m_metrics.connectionCount.store(0);
    {
        std::lock_guard<std::mutex> lock(m_metricsMutex);
        m_lossEstimateByWriter.clear();
    }
}

void LDds::emitStructuredLog(
    const char * level,
    const char * module,
    const std::string & text,
    const LMessage * message,
    const LHostAddress * peerAddress,
    quint16 peerPort)
{
    LogCallback callback;
    bool consoleEnabled = false;
    {
        std::lock_guard<std::mutex> lock(m_logMutex);
        callback = m_logCallback;
        consoleEnabled = m_structuredLogEnabled;
    }
    if (!consoleEnabled && !callback)
    {
        return;
    }

    std::ostringstream line;
    line << "[ldds][" << (level ? level : "info") << "]"
         << " module=" << (module ? module : "ldds")
         << " domain=" << static_cast<uint32_t>(m_effectiveDomainId);

    if (message != nullptr)
    {
        line << " topic=" << message->getTopic()
             << " seq=" << message->getSequence()
             << " type=" << messageTypeToText(message->getMessageType())
             << " messageId=" << makeMessageId(*message);
    }
    else
    {
        line << " topic=0 seq=0 type=None messageId=0-0";
    }

    if (peerAddress != nullptr && !peerAddress->isNull() && peerPort != 0)
    {
        line << " peer=" << peerAddress->toString().toStdString()
             << ":" << static_cast<uint32_t>(peerPort);
    }
    else
    {
        line << " peer=-";
    }

    line << " text=\"" << text << "\"";
    const std::string output = line.str();
    if (callback)
    {
        callback(output);
    }
    if (consoleEnabled)
    {
        std::cerr << output << std::endl;
    }
}

std::string LDds::makeMessageId(const LMessage & message) const
{
    uint32_t writerId = message.getWriterId();
    if (writerId == 0)
    {
        writerId = resolveReliableWriterId(
            message,
            message.getSenderAddress(),
            message.getSenderPort());
    }
    return std::to_string(writerId) + "-" + std::to_string(message.getSequence());
}

void LDds::setLastError(const std::string & message)
{
    {
        std::lock_guard<std::mutex> lock(m_errorMutex);
        m_lastError = message;
    }
    emitStructuredLog("error", "ldds", message);
}

const char * getVersion() noexcept
{
    return "0.3.0";
}

const char * getBuildTime() noexcept
{
    return __DATE__ " " __TIME__;
}

bool initialize() noexcept
{
    try
    {
        return LDds::instance().initialize();
    }
    catch (...)
    {
        return false;
    }
}

bool initialize(const DdsInitOptions & options) noexcept
{
    try
    {
        return LDds::instance().initialize(options);
    }
    catch (...)
    {
        return false;
    }
}

bool initialize(const TransportConfig & transportConfig) noexcept
{
    try
    {
        return LDds::instance().initialize(transportConfig);
    }
    catch (...)
    {
        return false;
    }
}

bool initialize(const LQos & qos) noexcept
{
    try
    {
        return LDds::instance().initialize(qos);
    }
    catch (...)
    {
        return false;
    }
}

bool initialize(
    const LQos & qos,
    const TransportConfig & transportConfig,
    DomainId domainId) noexcept
{
    try
    {
        return LDds::instance().initialize(qos, transportConfig, domainId);
    }
    catch (...)
    {
        return false;
    }
}

bool initializeFromQosXml(
    const std::string & qosXmlPath,
    const TransportConfig & transportConfig,
    DomainId domainId) noexcept
{
    try
    {
        return LDds::instance().initializeFromQosXml(qosXmlPath, transportConfig, domainId);
    }
    catch (...)
    {
        return false;
    }
}

void shutdown() noexcept
{
    LDds::instance().shutdown();
}

bool isInitialized() noexcept
{
    return LDds::instance().isRunning();
}

bool isRunning() noexcept
{
    return LDds::instance().isRunning();
}

LDds & dds() noexcept
{
    return LDds::instance();
}

} // namespace LDdsFramework
