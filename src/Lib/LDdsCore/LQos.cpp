/**
 * @file LQos.cpp
 * @brief LQos class implementation
 */

#include "LQos.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>
#include <string>

#include "pugixml.hpp"

namespace LDdsFramework {
namespace {

int32_t clampToInt32(int64_t value)
{
    if (value > static_cast<int64_t>(std::numeric_limits<int32_t>::max()))
    {
        return std::numeric_limits<int32_t>::max();
    }
    if (value < static_cast<int64_t>(std::numeric_limits<int32_t>::min()))
    {
        return std::numeric_limits<int32_t>::min();
    }
    return static_cast<int32_t>(value);
}

int32_t durationToMs(const Duration & value)
{
    if (value.isInfinite())
    {
        return 0;
    }

    const int64_t millis =
        (value.seconds * 1000LL) + static_cast<int64_t>(value.nanoseconds / 1000000U);
    return clampToInt32(millis);
}

Duration durationFromMs(int32_t ms)
{
    Duration result;
    if (ms <= 0)
    {
        result = Duration(DURATION_INFINITY);
        return result;
    }

    result.seconds = static_cast<int64_t>(ms / 1000);
    result.nanoseconds = static_cast<uint32_t>((ms % 1000) * 1000000);
    return result;
}

std::string toLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
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

bool parseTransportTypeText(const std::string & text, TransportType & type)
{
    const std::string lower = toLower(trim(text));
    if (lower == "udp")
    {
        type = TransportType::UDP;
        return true;
    }
    if (lower == "tcp")
    {
        type = TransportType::TCP;
        return true;
    }
    return false;
}

bool parseDurabilityKindText(const std::string & text, DurabilityKind & kind)
{
    const std::string lower = toLower(trim(text));
    if (lower == "volatile")
    {
        kind = DurabilityKind::Volatile;
        return true;
    }
    if (lower == "transientlocal" || lower == "transient_local" || lower == "transient-local")
    {
        kind = DurabilityKind::TransientLocal;
        return true;
    }
    if (lower == "transient")
    {
        kind = DurabilityKind::Transient;
        return true;
    }
    if (lower == "persistent")
    {
        kind = DurabilityKind::Persistent;
        return true;
    }
    return false;
}

bool parseOwnershipKindText(const std::string & text, OwnershipKind & kind)
{
    const std::string lower = toLower(trim(text));
    if (lower == "shared")
    {
        kind = OwnershipKind::Shared;
        return true;
    }
    if (lower == "exclusive")
    {
        kind = OwnershipKind::Exclusive;
        return true;
    }
    return false;
}

bool parseIntText(const std::string & text, int32_t & valueOut)
{
    const std::string normalized = trim(text);
    if (normalized.empty())
    {
        return false;
    }

    try
    {
        size_t pos = 0;
        const long long parsed = std::stoll(normalized, &pos, 10);
        if (pos != normalized.size())
        {
            return false;
        }
        valueOut = clampToInt32(parsed);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool tryReadInt(const pugi::xml_node & root, const char * childName, int32_t & valueOut)
{
    const pugi::xml_node child = root.child(childName);
    if (!child)
    {
        return false;
    }
    return parseIntText(child.text().as_string(), valueOut);
}

bool tryReadIntAttr(
    const pugi::xml_node & node,
    const char *           attrName,
    int32_t &              valueOut)
{
    const pugi::xml_attribute attr = node.attribute(attrName);
    if (!attr)
    {
        return false;
    }
    return parseIntText(attr.as_string(), valueOut);
}

bool tryReadBool(const pugi::xml_node & root, const char * childName, bool & valueOut)
{
    const pugi::xml_node child = root.child(childName);
    if (!child)
    {
        return false;
    }
    valueOut = parseBoolText(child.text().as_string(), valueOut);
    return true;
}

bool tryReadBoolAttr(const pugi::xml_node & node, const char * attrName, bool & valueOut)
{
    const pugi::xml_attribute attr = node.attribute(attrName);
    if (!attr)
    {
        return false;
    }
    valueOut = parseBoolText(attr.as_string(), valueOut);
    return true;
}

} // namespace

LQos::LQos() noexcept
    : historyDepth(1)
    , deadlineMs(0)
    , reliable(false)
    , durabilityKind(DurabilityKind::Volatile)
    , ownershipKind(OwnershipKind::Shared)
    , ownershipStrength(0)
    , transportType(TransportType::UDP)
    , domainId(0)
    , enableDomainPortMapping(false)
    , basePort(20000)
    , domainPortOffset(10)
    , durabilityDbPath()
    , enableMetrics(false)
    , metricsPort(0)
    , metricsBindAddress("127.0.0.1")
    , structuredLogEnabled(false)
    , securityEnabled(false)
    , securityEncryptPayload(false)
    , securityPsk()
    , m_reliability()
    , m_durability()
    , m_deadline()
    , m_latencyBudget()
    , m_history()
    , m_resourceLimits()
    , m_ownership()
    , m_userData()
{
    historyDepth = m_history.depth > 0 ? m_history.depth : 1;
    deadlineMs = durationToMs(m_deadline.period);
    reliable = m_reliability.enabled && (m_reliability.kind == ReliabilityKind::Reliable);
    durabilityKind = m_durability.kind;
    ownershipKind = m_ownership.kind;
    ownershipStrength = std::max(0, m_ownership.strength);
}

LQos::LQos(const LQos & other) = default;

LQos & LQos::operator=(const LQos & other) = default;

LQos::LQos(LQos && other) noexcept = default;

LQos & LQos::operator=(LQos && other) noexcept = default;

LQos::~LQos() noexcept = default;

void LQos::resetToDefaults() noexcept
{
    m_reliability    = ReliabilityQosPolicy();
    m_durability     = DurabilityQosPolicy();
    m_deadline       = DeadlineQosPolicy();
    m_latencyBudget  = LatencyBudgetQosPolicy();
    m_history        = HistoryQosPolicy();
    m_resourceLimits = ResourceLimitsQosPolicy();
    m_ownership      = OwnershipQosPolicy();
    m_userData       = UserDataQosPolicy();
    historyDepth     = 1;
    deadlineMs       = 0;
    reliable         = false;
    durabilityKind   = DurabilityKind::Volatile;
    ownershipKind    = OwnershipKind::Shared;
    ownershipStrength = 0;
    transportType    = TransportType::UDP;
    domainId         = 0;
    enableDomainPortMapping = false;
    basePort         = 20000;
    domainPortOffset = 10;
    durabilityDbPath.clear();
    enableMetrics = false;
    metricsPort = 0;
    metricsBindAddress = "127.0.0.1";
    structuredLogEnabled = false;
    securityEnabled = false;
    securityEncryptPayload = false;
    securityPsk.clear();
}

void LQos::setTransportType(TransportType type) noexcept
{
    transportType = type;
}

TransportType LQos::getTransportType() const noexcept
{
    return transportType;
}

void LQos::setReliability(const ReliabilityQosPolicy & policy) noexcept
{
    m_reliability = policy;
    reliable = m_reliability.enabled && (m_reliability.kind == ReliabilityKind::Reliable);
}

const ReliabilityQosPolicy & LQos::getReliability() const noexcept
{
    return m_reliability;
}

void LQos::setDurability(const DurabilityQosPolicy & policy) noexcept
{
    m_durability = policy;
    durabilityKind = m_durability.kind;
}

const DurabilityQosPolicy & LQos::getDurability() const noexcept
{
    return m_durability;
}

void LQos::setDeadline(const DeadlineQosPolicy & policy) noexcept
{
    m_deadline = policy;
    deadlineMs = m_deadline.enabled ? durationToMs(m_deadline.period) : 0;
}

const DeadlineQosPolicy & LQos::getDeadline() const noexcept
{
    return m_deadline;
}

void LQos::setLatencyBudget(const LatencyBudgetQosPolicy & policy) noexcept
{
    m_latencyBudget = policy;
}

const LatencyBudgetQosPolicy & LQos::getLatencyBudget() const noexcept
{
    return m_latencyBudget;
}

void LQos::setHistory(const HistoryQosPolicy & policy) noexcept
{
    m_history = policy;
    historyDepth = m_history.depth > 0 ? m_history.depth : 1;
}

const HistoryQosPolicy & LQos::getHistory() const noexcept
{
    return m_history;
}

void LQos::setResourceLimits(const ResourceLimitsQosPolicy & policy) noexcept
{
    m_resourceLimits = policy;
}

const ResourceLimitsQosPolicy & LQos::getResourceLimits() const noexcept
{
    return m_resourceLimits;
}

void LQos::setOwnership(const OwnershipQosPolicy & policy) noexcept
{
    m_ownership = policy;
    ownershipKind = m_ownership.kind;
    ownershipStrength = std::max(0, m_ownership.strength);
}

const OwnershipQosPolicy & LQos::getOwnership() const noexcept
{
    return m_ownership;
}

void LQos::setUserData(const UserDataQosPolicy & policy)
{
    m_userData = policy;
}

const UserDataQosPolicy & LQos::getUserData() const noexcept
{
    return m_userData;
}

bool LQos::validate(std::string & errorMessage) const
{
    if (transportType != TransportType::UDP && transportType != TransportType::TCP)
    {
        errorMessage = "invalid transportType";
        return false;
    }

    if (historyDepth <= 0)
    {
        errorMessage = "historyDepth must be > 0";
        return false;
    }

    if (deadlineMs < 0)
    {
        errorMessage = "deadlineMs must be >= 0";
        return false;
    }

    if (ownershipStrength < 0)
    {
        errorMessage = "ownershipStrength must be >= 0";
        return false;
    }

    if (static_cast<uint32_t>(domainId) > 255U)
    {
        errorMessage = "domainId must be in [0,255]";
        return false;
    }

    if (enableDomainPortMapping)
    {
        if (basePort == 0)
        {
            errorMessage = "basePort must be > 0 when enableDomainPortMapping=true";
            return false;
        }
        if (domainPortOffset == 0)
        {
            errorMessage = "domainPortOffset must be > 0 when enableDomainPortMapping=true";
            return false;
        }

        const uint32_t mappedPort =
            static_cast<uint32_t>(basePort) +
            (static_cast<uint32_t>(domainId) * static_cast<uint32_t>(domainPortOffset));
        if (mappedPort > 65535U)
        {
            errorMessage = "mapped port exceeds 65535";
            return false;
        }
    }

    if (securityEnabled && securityPsk.empty())
    {
        errorMessage = "securityPsk must not be empty when securityEnabled=true";
        return false;
    }

    errorMessage.clear();
    return true;
}

bool LQos::isCompatibleWith(const LQos & other, std::string & errorMessage) const
{
    if (transportType != other.transportType)
    {
        errorMessage = "transportType mismatch";
        return false;
    }

    if (reliable != other.reliable)
    {
        errorMessage = "reliability mismatch";
        return false;
    }

    if (historyDepth != other.historyDepth)
    {
        errorMessage = "historyDepth mismatch";
        return false;
    }

    if (deadlineMs != other.deadlineMs)
    {
        errorMessage = "deadlineMs mismatch";
        return false;
    }

    if (domainId != other.domainId)
    {
        errorMessage = "domainId mismatch";
        return false;
    }

    if (durabilityKind != other.durabilityKind)
    {
        errorMessage = "durabilityKind mismatch";
        return false;
    }

    if (ownershipKind != other.ownershipKind)
    {
        errorMessage = "ownershipKind mismatch";
        return false;
    }

    if (enableDomainPortMapping != other.enableDomainPortMapping)
    {
        errorMessage = "enableDomainPortMapping mismatch";
        return false;
    }

    if (enableDomainPortMapping &&
        (basePort != other.basePort || domainPortOffset != other.domainPortOffset))
    {
        errorMessage = "domain port mapping parameters mismatch";
        return false;
    }

    if (securityEnabled != other.securityEnabled)
    {
        errorMessage = "securityEnabled mismatch";
        return false;
    }

    if (securityEnabled &&
        (securityEncryptPayload != other.securityEncryptPayload || securityPsk != other.securityPsk))
    {
        errorMessage = "security configuration mismatch";
        return false;
    }

    errorMessage.clear();
    return true;
}

void LQos::merge(const LQos & other)
{
    m_reliability = other.m_reliability;
    m_durability = other.m_durability;
    m_deadline = other.m_deadline;
    m_latencyBudget = other.m_latencyBudget;
    m_history = other.m_history;
    m_resourceLimits = other.m_resourceLimits;
    m_ownership = other.m_ownership;
    m_userData = other.m_userData;

    historyDepth = other.historyDepth;
    deadlineMs = other.deadlineMs;
    reliable = other.reliable;
    durabilityKind = other.durabilityKind;
    ownershipKind = other.ownershipKind;
    ownershipStrength = other.ownershipStrength;
    transportType = other.transportType;
    domainId = other.domainId;
    enableDomainPortMapping = other.enableDomainPortMapping;
    basePort = other.basePort;
    domainPortOffset = other.domainPortOffset;
    durabilityDbPath = other.durabilityDbPath;
    enableMetrics = other.enableMetrics;
    metricsPort = other.metricsPort;
    metricsBindAddress = other.metricsBindAddress;
    structuredLogEnabled = other.structuredLogEnabled;
    securityEnabled = other.securityEnabled;
    securityEncryptPayload = other.securityEncryptPayload;
    securityPsk = other.securityPsk;
}

bool LQos::loadFromXmlFile(const std::string & filePath, std::string * errorMessage)
{
    pugi::xml_document document;
    const pugi::xml_parse_result result = document.load_file(filePath.c_str());
    if (!result)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = std::string("failed to parse qos xml file: ") + result.description();
        }
        return false;
    }

    const pugi::xml_node root =
        document.child("qos")
            ? document.child("qos")
            : (document.child("QoS")
                   ? document.child("QoS")
                   : document.document_element());
    if (!root)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "qos xml is empty";
        }
        return false;
    }

    std::ostringstream xmlStream;
    document.save(xmlStream);
    return loadFromXmlString(xmlStream.str(), errorMessage);
}

bool LQos::loadFromXmlString(const std::string & xmlText, std::string * errorMessage)
{
    pugi::xml_document document;
    const pugi::xml_parse_result result = document.load_string(xmlText.c_str());
    if (!result)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = std::string("failed to parse qos xml text: ") + result.description();
        }
        return false;
    }

    const pugi::xml_node root =
        document.child("qos")
            ? document.child("qos")
            : (document.child("QoS")
                   ? document.child("QoS")
                   : document.document_element());
    if (!root)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "qos xml has no root node";
        }
        return false;
    }

    TransportType parsedTransport = transportType;
    int32_t parsedHistoryDepth = historyDepth;
    int32_t parsedDeadlineMs = deadlineMs;
    bool parsedReliable = reliable;
    DurabilityKind parsedDurabilityKind = durabilityKind;
    OwnershipKind parsedOwnershipKind = ownershipKind;
    int32_t parsedOwnershipStrength = ownershipStrength;
    int32_t parsedDomainId = static_cast<int32_t>(domainId);
    bool parsedEnableDomainPortMapping = enableDomainPortMapping;
    int32_t parsedBasePort = static_cast<int32_t>(basePort);
    int32_t parsedDomainPortOffset = static_cast<int32_t>(domainPortOffset);
    std::string parsedDurabilityDbPath = durabilityDbPath;
    bool parsedEnableMetrics = enableMetrics;
    int32_t parsedMetricsPort = static_cast<int32_t>(metricsPort);
    std::string parsedMetricsBindAddress = metricsBindAddress;
    bool parsedStructuredLogEnabled = structuredLogEnabled;
    bool parsedSecurityEnabled = securityEnabled;
    bool parsedSecurityEncryptPayload = securityEncryptPayload;
    std::string parsedSecurityPsk = securityPsk;

    if (const pugi::xml_attribute attr = root.attribute("transportType"))
    {
        parseTransportTypeText(attr.as_string(), parsedTransport);
    }
    if (const pugi::xml_attribute attr = root.attribute("historyDepth"))
    {
        parseIntText(attr.as_string(), parsedHistoryDepth);
    }
    if (const pugi::xml_attribute attr = root.attribute("deadlineMs"))
    {
        parseIntText(attr.as_string(), parsedDeadlineMs);
    }
    if (const pugi::xml_attribute attr = root.attribute("reliable"))
    {
        parsedReliable = parseBoolText(attr.as_string(), parsedReliable);
    }
    if (const pugi::xml_attribute attr = root.attribute("durability"))
    {
        parseDurabilityKindText(attr.as_string(), parsedDurabilityKind);
    }
    if (const pugi::xml_attribute attr = root.attribute("ownershipKind"))
    {
        parseOwnershipKindText(attr.as_string(), parsedOwnershipKind);
    }
    if (const pugi::xml_attribute attr = root.attribute("ownershipStrength"))
    {
        parseIntText(attr.as_string(), parsedOwnershipStrength);
    }
    if (const pugi::xml_attribute attr = root.attribute("domainId"))
    {
        parseIntText(attr.as_string(), parsedDomainId);
    }
    if (const pugi::xml_attribute attr = root.attribute("enableDomainPortMapping"))
    {
        parsedEnableDomainPortMapping = parseBoolText(attr.as_string(), parsedEnableDomainPortMapping);
    }
    if (const pugi::xml_attribute attr = root.attribute("basePort"))
    {
        parseIntText(attr.as_string(), parsedBasePort);
    }
    if (const pugi::xml_attribute attr = root.attribute("domainPortOffset"))
    {
        parseIntText(attr.as_string(), parsedDomainPortOffset);
    }
    if (const pugi::xml_attribute attr = root.attribute("durabilityDbPath"))
    {
        parsedDurabilityDbPath = trim(attr.as_string());
    }
    if (const pugi::xml_attribute attr = root.attribute("enableMetrics"))
    {
        parsedEnableMetrics = parseBoolText(attr.as_string(), parsedEnableMetrics);
    }
    if (const pugi::xml_attribute attr = root.attribute("metricsPort"))
    {
        parseIntText(attr.as_string(), parsedMetricsPort);
    }
    if (const pugi::xml_attribute attr = root.attribute("metricsBindAddress"))
    {
        parsedMetricsBindAddress = trim(attr.as_string());
    }
    if (const pugi::xml_attribute attr = root.attribute("structuredLogEnabled"))
    {
        parsedStructuredLogEnabled = parseBoolText(attr.as_string(), parsedStructuredLogEnabled);
    }
    if (const pugi::xml_attribute attr = root.attribute("securityEnabled"))
    {
        parsedSecurityEnabled = parseBoolText(attr.as_string(), parsedSecurityEnabled);
    }
    if (const pugi::xml_attribute attr = root.attribute("securityEncryptPayload"))
    {
        parsedSecurityEncryptPayload = parseBoolText(attr.as_string(), parsedSecurityEncryptPayload);
    }
    if (const pugi::xml_attribute attr = root.attribute("securityPsk"))
    {
        parsedSecurityPsk = trim(attr.as_string());
    }

    if (const pugi::xml_node transportNode = root.child("transport"))
    {
        if (const pugi::xml_attribute attr = transportNode.attribute("type"))
        {
            parseTransportTypeText(attr.as_string(), parsedTransport);
        }
        else
        {
            parseTransportTypeText(transportNode.text().as_string(), parsedTransport);
        }
    }
    else if (const pugi::xml_node transportTypeNode = root.child("transportType"))
    {
        parseTransportTypeText(transportTypeNode.text().as_string(), parsedTransport);
    }

    if (!tryReadInt(root, "historyDepth", parsedHistoryDepth))
    {
        if (const pugi::xml_node historyNode = root.child("history"))
        {
            if (!tryReadIntAttr(historyNode, "depth", parsedHistoryDepth))
            {
                tryReadInt(historyNode, "depth", parsedHistoryDepth);
            }
        }
    }

    if (!tryReadInt(root, "deadlineMs", parsedDeadlineMs))
    {
        if (const pugi::xml_node deadlineNode = root.child("deadline"))
        {
            if (!tryReadIntAttr(deadlineNode, "ms", parsedDeadlineMs) &&
                !tryReadIntAttr(deadlineNode, "periodMs", parsedDeadlineMs))
            {
                tryReadInt(deadlineNode, "ms", parsedDeadlineMs);
            }
        }
    }

    tryReadInt(root, "domainId", parsedDomainId);
    tryReadBool(root, "enableDomainPortMapping", parsedEnableDomainPortMapping);
    tryReadInt(root, "basePort", parsedBasePort);
    tryReadInt(root, "domainPortOffset", parsedDomainPortOffset);
    tryReadBool(root, "enableMetrics", parsedEnableMetrics);
    tryReadInt(root, "metricsPort", parsedMetricsPort);
    tryReadBool(root, "structuredLogEnabled", parsedStructuredLogEnabled);
    tryReadBool(root, "securityEnabled", parsedSecurityEnabled);
    tryReadBool(root, "securityEncryptPayload", parsedSecurityEncryptPayload);

    if (const pugi::xml_node metricsBindNode = root.child("metricsBindAddress"))
    {
        parsedMetricsBindAddress = trim(metricsBindNode.text().as_string());
    }
    if (const pugi::xml_node securityPskNode = root.child("securityPsk"))
    {
        parsedSecurityPsk = trim(securityPskNode.text().as_string());
    }

    if (const pugi::xml_node domainNode = root.child("domain"))
    {
        tryReadIntAttr(domainNode, "id", parsedDomainId);
    }

    if (const pugi::xml_node mappingNode = root.child("portMapping"))
    {
        tryReadBoolAttr(mappingNode, "enable", parsedEnableDomainPortMapping);
        tryReadBoolAttr(mappingNode, "enabled", parsedEnableDomainPortMapping);
        tryReadIntAttr(mappingNode, "basePort", parsedBasePort);
        tryReadIntAttr(mappingNode, "domainPortOffset", parsedDomainPortOffset);

        tryReadInt(mappingNode, "basePort", parsedBasePort);
        tryReadInt(mappingNode, "domainPortOffset", parsedDomainPortOffset);
    }

    if (const pugi::xml_node metricsNode = root.child("metrics"))
    {
        tryReadBoolAttr(metricsNode, "enable", parsedEnableMetrics);
        tryReadBoolAttr(metricsNode, "enabled", parsedEnableMetrics);
        tryReadIntAttr(metricsNode, "port", parsedMetricsPort);
        if (const pugi::xml_attribute bindAttr = metricsNode.attribute("bindAddress"))
        {
            parsedMetricsBindAddress = trim(bindAttr.as_string());
        }
        if (const pugi::xml_attribute bindAttr = metricsNode.attribute("address"))
        {
            parsedMetricsBindAddress = trim(bindAttr.as_string());
        }
        if (!metricsNode.text().empty())
        {
            const std::string text = trim(metricsNode.text().as_string());
            if (!text.empty())
            {
                parsedMetricsBindAddress = text;
            }
        }
    }

    if (const pugi::xml_node loggingNode = root.child("logging"))
    {
        tryReadBoolAttr(loggingNode, "structured", parsedStructuredLogEnabled);
        tryReadBoolAttr(loggingNode, "structuredEnabled", parsedStructuredLogEnabled);
    }

    if (const pugi::xml_node securityNode = root.child("security"))
    {
        tryReadBoolAttr(securityNode, "enable", parsedSecurityEnabled);
        tryReadBoolAttr(securityNode, "enabled", parsedSecurityEnabled);
        tryReadBoolAttr(securityNode, "encryptPayload", parsedSecurityEncryptPayload);
        if (const pugi::xml_attribute pskAttr = securityNode.attribute("psk"))
        {
            parsedSecurityPsk = trim(pskAttr.as_string());
        }
        if (const pugi::xml_node pskNode = securityNode.child("psk"))
        {
            const std::string text = trim(pskNode.text().as_string());
            if (!text.empty())
            {
                parsedSecurityPsk = text;
            }
        }
    }

    if (const pugi::xml_node durabilityNode = root.child("durability"))
    {
        if (const pugi::xml_attribute kindAttr = durabilityNode.attribute("kind"))
        {
            parseDurabilityKindText(kindAttr.as_string(), parsedDurabilityKind);
        }
        else
        {
            parseDurabilityKindText(durabilityNode.text().as_string(), parsedDurabilityKind);
        }

        if (const pugi::xml_attribute dbPathAttr = durabilityNode.attribute("dbPath"))
        {
            parsedDurabilityDbPath = trim(dbPathAttr.as_string());
        }
    }

    if (const pugi::xml_node ownershipNode = root.child("ownership"))
    {
        if (const pugi::xml_attribute kindAttr = ownershipNode.attribute("kind"))
        {
            parseOwnershipKindText(kindAttr.as_string(), parsedOwnershipKind);
        }

        if (const pugi::xml_attribute strengthAttr = ownershipNode.attribute("strength"))
        {
            parseIntText(strengthAttr.as_string(), parsedOwnershipStrength);
        }

        int32_t ownershipStrengthNodeValue = parsedOwnershipStrength;
        if (tryReadInt(ownershipNode, "strength", ownershipStrengthNodeValue))
        {
            parsedOwnershipStrength = ownershipStrengthNodeValue;
        }
    }

    if (const pugi::xml_node reliableNode = root.child("reliable"))
    {
        parsedReliable = parseBoolText(reliableNode.text().as_string(), parsedReliable);
    }
    else if (const pugi::xml_node reliabilityNode = root.child("reliability"))
    {
        if (const pugi::xml_attribute attr = reliabilityNode.attribute("reliable"))
        {
            parsedReliable = parseBoolText(attr.as_string(), parsedReliable);
        }
        else if (const pugi::xml_attribute kindAttr = reliabilityNode.attribute("kind"))
        {
            const std::string kind = toLower(kindAttr.as_string());
            if (kind == "besteffort" || kind == "best_effort" || kind == "best-effort")
            {
                parsedReliable = false;
            }
            else if (kind == "reliable")
            {
                parsedReliable = true;
            }
        }
        else
        {
            parsedReliable = parseBoolText(reliabilityNode.text().as_string(), parsedReliable);
        }
    }

    if (parsedHistoryDepth <= 0)
    {
        parsedHistoryDepth = 1;
    }
    if (parsedDeadlineMs < 0)
    {
        parsedDeadlineMs = 0;
    }
    if (parsedDomainId < 0 || parsedDomainId > 255)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "domainId must be in [0,255]";
        }
        return false;
    }
    if (parsedBasePort < 0 || parsedBasePort > 65535)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "basePort must be in [0,65535]";
        }
        return false;
    }
    if (parsedDomainPortOffset < 0 || parsedDomainPortOffset > 65535)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "domainPortOffset must be in [0,65535]";
        }
        return false;
    }
    if (parsedMetricsPort < 0 || parsedMetricsPort > 65535)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "metricsPort must be in [0,65535]";
        }
        return false;
    }
    if (parsedOwnershipStrength < 0)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "ownershipStrength must be >= 0";
        }
        return false;
    }

    transportType = parsedTransport;
    historyDepth = parsedHistoryDepth;
    deadlineMs = parsedDeadlineMs;
    reliable = parsedReliable;
    durabilityKind = parsedDurabilityKind;
    ownershipKind = parsedOwnershipKind;
    ownershipStrength = parsedOwnershipStrength;
    domainId = static_cast<uint8_t>(parsedDomainId);
    enableDomainPortMapping = parsedEnableDomainPortMapping;
    basePort = static_cast<uint16_t>(parsedBasePort);
    domainPortOffset = static_cast<uint16_t>(parsedDomainPortOffset);
    durabilityDbPath = parsedDurabilityDbPath;
    enableMetrics = parsedEnableMetrics;
    metricsPort = static_cast<uint16_t>(parsedMetricsPort);
    metricsBindAddress = parsedMetricsBindAddress.empty() ? std::string("127.0.0.1") : parsedMetricsBindAddress;
    structuredLogEnabled = parsedStructuredLogEnabled;
    securityEnabled = parsedSecurityEnabled;
    securityEncryptPayload = parsedSecurityEncryptPayload;
    securityPsk = parsedSecurityPsk;

    m_history.enabled = true;
    m_history.kind = HistoryKind::KeepLast;
    m_history.depth = historyDepth;

    m_deadline.enabled = (deadlineMs > 0);
    m_deadline.period = durationFromMs(deadlineMs);

    m_reliability.enabled = reliable;
    m_reliability.kind = reliable ? ReliabilityKind::Reliable : ReliabilityKind::BestEffort;
    m_durability.enabled = true;
    m_durability.kind = durabilityKind;
    m_ownership.enabled = true;
    m_ownership.kind = ownershipKind;
    m_ownership.strength = ownershipStrength;

    std::string validateError;
    if (!validate(validateError))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = validateError;
        }
        return false;
    }

    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }
    return true;
}

} // namespace LDdsFramework
