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

#define PUGIXML_HEADER_ONLY
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

} // namespace

LQos::LQos() noexcept
    : historyDepth(1)
    , deadlineMs(0)
    , reliable(false)
    , transportType(TransportType::UDP)
    , m_reliability()
    , m_durability()
    , m_deadline()
    , m_latencyBudget()
    , m_history()
    , m_resourceLimits()
    , m_userData()
{
    historyDepth = m_history.depth > 0 ? m_history.depth : 1;
    deadlineMs = durationToMs(m_deadline.period);
    reliable = m_reliability.enabled && (m_reliability.kind == ReliabilityKind::Reliable);
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
    m_userData       = UserDataQosPolicy();
    historyDepth     = 1;
    deadlineMs       = 0;
    reliable         = false;
    transportType    = TransportType::UDP;
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
    m_userData = other.m_userData;

    historyDepth = other.historyDepth;
    deadlineMs = other.deadlineMs;
    reliable = other.reliable;
    transportType = other.transportType;
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

    transportType = parsedTransport;
    historyDepth = parsedHistoryDepth;
    deadlineMs = parsedDeadlineMs;
    reliable = parsedReliable;

    m_history.enabled = true;
    m_history.kind = HistoryKind::KeepLast;
    m_history.depth = historyDepth;

    m_deadline.enabled = (deadlineMs > 0);
    m_deadline.period = durationFromMs(deadlineMs);

    m_reliability.enabled = reliable;
    m_reliability.kind = reliable ? ReliabilityKind::Reliable : ReliabilityKind::BestEffort;

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
