#include "ITransport.h"
#include "LMessage.h"
#include "LUdpTransport.h"
#include "LTcpTransport.h"

namespace LDdsFramework {

TransportConfig::TransportConfig()
    : bindAddress(LStringLiteral("0.0.0.0"))
    , bindPort(0)
    , remoteAddress()
    , remotePort(0)
    , enableDomainPortMapping(false)
    , basePort(20000)
    , domainPortOffset(10)
    , enableBroadcast(false)
    , receiveBufferSize(65536)
    , sendBufferSize(65536)
    , maxConnections(100)
    , reuseAddress(true)
    , autoReconnect(true)
    , reconnectMinMs(200)
    , reconnectMaxMs(5000)
    , reconnectMultiplier(2.0)
    , maxPendingMessages(4096)
    , sendQueueOverflowPolicy(SendQueueOverflowPolicy::DropOldest)
    , enableDiscovery(true)
    , discoveryIntervalMs(1000)
    , peerTtlMs(5000)
    , discoveryPort(0)
    , enableMulticast(false)
    , multicastGroup()
    , multicastTtl(1)
{
}

ITransport::ITransport()
    : m_state(TransportState::Stopped)
    , m_boundPort(0)
    , m_receiveCallback(nullptr)
{
}

ITransport::~ITransport() = default;

bool ITransport::broadcastMessage(const LMessage& message, quint16 broadcastPort)
{
    L_UNUSED(message)
    L_UNUSED(broadcastPort)
    setError(LStringLiteral("Broadcast is not supported by this transport"));
    return false;
}

bool ITransport::setDefaultRemote(const LHostAddress& targetAddress, quint16 targetPort)
{
    if (targetAddress.isNull() || targetPort == 0) {
        setError(LStringLiteral("Invalid default remote endpoint"));
        return false;
    }

    std::lock_guard<std::mutex> lock(m_configMutex);
    m_config.remoteAddress = targetAddress.toString();
    m_config.remotePort = targetPort;
    return true;
}

void ITransport::setReceiveCallback(ReceiveCallback callback)
{
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_receiveCallback = std::move(callback);
}

TransportState ITransport::getState() const noexcept
{
    return m_state.load();
}

TransportConfig ITransport::getConfig() const
{
    std::lock_guard<std::mutex> lock(m_configMutex);
    return m_config;
}

void ITransport::setConfig(const TransportConfig& config)
{
    std::lock_guard<std::mutex> lock(m_configMutex);
    m_config = config;
}

LString ITransport::getLastError() const noexcept
{
    std::lock_guard<std::mutex> lock(m_errorMutex);
    return m_lastError;
}

quint16 ITransport::getBoundPort() const noexcept
{
    return m_boundPort.load();
}

std::unique_ptr<ITransport> ITransport::createTransport(TransportProtocol protocol)
{
    switch (protocol) {
    case TransportProtocol::UDP:
        return std::make_unique<LUdpTransport>();
    case TransportProtocol::TCP:
        return std::make_unique<LTcpTransport>();
    default:
        return std::make_unique<LUdpTransport>();
    }
}

void ITransport::setState(TransportState state) noexcept
{
    m_state.store(state);
}

void ITransport::setError(const LString& error)
{
    std::lock_guard<std::mutex> lock(m_errorMutex);
    m_lastError = error;
}

void ITransport::notifyReceiveCallback(const LMessage& message,
                                       const LHostAddress& senderAddress,
                                       quint16 senderPort)
{
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    if (m_receiveCallback) {
        m_receiveCallback(message, senderAddress, senderPort);
    }
}

void ITransport::setBoundPort(quint16 port) noexcept
{
    m_boundPort.store(port);
}

} // namespace LDdsFramework
