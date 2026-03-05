#include "LUdpTransport.h"
#include "LMessage.h"

#include <LAbstractSocket>
#include <LByteArray>
#include <LUdpSocket>

#include <chrono>
#include <thread>
#include <utility>
#include <vector>

namespace LDdsFramework {

LUdpTransport::LUdpTransport()
    : m_receiveSocket()
    , m_sendSocket()
    , m_receiveThread()
    , m_running(false)
    , m_maxPacketSize(65507)
    , m_sentPacketCount(0)
    , m_recvPacketCount(0)
    , m_sentByteCount(0)
    , m_recvByteCount(0)
    , m_receiveSocketMutex()
    , m_sendSocketMutex()
{
}

LUdpTransport::~LUdpTransport()
{
    stop();
}

TransportProtocol LUdpTransport::getProtocol() const noexcept
{
    return TransportProtocol::UDP;
}

bool LUdpTransport::start()
{
    if (getState() == TransportState::Running) {
        return true;
    }

    setState(TransportState::Starting);

    if (!initializeSocket()) {
        setState(TransportState::Error);
        return false;
    }

    m_running.store(true);
    m_receiveThread = std::thread(&LUdpTransport::receiveThreadFunc, this);

    setState(TransportState::Running);
    return true;
}

void LUdpTransport::stop()
{
    const TransportState currentState = getState();
    if (currentState == TransportState::Stopped) {
        return;
    }

    setState(TransportState::Stopping);

    m_running.store(false);
    closeSocket();

    if (m_receiveThread.joinable()) {
        m_receiveThread.join();
    }

    setState(TransportState::Stopped);
}

bool LUdpTransport::sendMessage(const LMessage& message)
{
    LHostAddress targetAddress;
    quint16 targetPort = 0;
    if (!resolveRemoteFromConfig(targetAddress, targetPort)) {
        setError(LStringLiteral("Default UDP remote endpoint is not configured"));
        return false;
    }
    return sendMessageTo(message, targetAddress, targetPort);
}

bool LUdpTransport::sendMessageTo(const LMessage& message,
                                  const LHostAddress& targetAddress,
                                  quint16 targetPort)
{
    if (getState() != TransportState::Running) {
        setError(LStringLiteral("Transport is not running"));
        return false;
    }

    if (targetAddress.isNull() || targetPort == 0) {
        setError(LStringLiteral("Invalid target endpoint"));
        return false;
    }

    const LByteBuffer serialized = message.serialize();
    if (serialized.size() > m_maxPacketSize.load()) {
        setError(LStringLiteral("UDP packet exceeds max packet size"));
        return false;
    }

    qint64 sentBytes = -1;
    {
        std::lock_guard<std::mutex> lock(m_sendSocketMutex);
        if (!m_sendSocket) {
            setError(LStringLiteral("UDP socket is not initialized"));
            return false;
        }

        sentBytes = m_sendSocket->writeDatagram(
            reinterpret_cast<const char*>(serialized.data()),
            static_cast<qint64>(serialized.size()),
            targetAddress,
            targetPort
        );
        if (sentBytes != static_cast<qint64>(serialized.size())) {
            setError(LStringLiteral("UDP send failed: %1").arg(m_sendSocket->errorString()));
            return false;
        }
    }

    m_sentPacketCount.fetch_add(1);
    m_sentByteCount.fetch_add(static_cast<uint64_t>(sentBytes));
    return true;
}

bool LUdpTransport::broadcastMessage(const LMessage& message, quint16 broadcastPort)
{
    if (!isBroadcastEnabled()) {
        setError(LStringLiteral("UDP broadcast is disabled"));
        return false;
    }

    return sendMessageTo(message, LHostAddress::Broadcast, broadcastPort);
}

void LUdpTransport::setMaxPacketSize(size_t maxSize) noexcept
{
    m_maxPacketSize.store(maxSize == 0 ? 1 : maxSize);
}

size_t LUdpTransport::getMaxPacketSize() const noexcept
{
    return m_maxPacketSize.load();
}

bool LUdpTransport::setBroadcastEnabled(bool enable)
{
    TransportConfig config = getConfig();
    config.enableBroadcast = enable;
    setConfig(config);
    return true;
}

bool LUdpTransport::isBroadcastEnabled() const
{
    return getConfig().enableBroadcast;
}

uint64_t LUdpTransport::getSentPacketCount() const noexcept
{
    return m_sentPacketCount.load();
}

uint64_t LUdpTransport::getReceivedPacketCount() const noexcept
{
    return m_recvPacketCount.load();
}

uint64_t LUdpTransport::getSentByteCount() const noexcept
{
    return m_sentByteCount.load();
}

uint64_t LUdpTransport::getReceivedByteCount() const noexcept
{
    return m_recvByteCount.load();
}

void LUdpTransport::receiveThreadFunc()
{
    while (m_running.load()) {
        std::vector<LMessage> readyMessages;
        std::vector<std::pair<LHostAddress, quint16>> endpoints;

        {
            std::unique_lock<std::mutex> lock(m_receiveSocketMutex);
            LUdpSocket* socket = m_receiveSocket.get();
            if (!socket) {
                lock.unlock();
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }

            const bool ready = socket->waitForReadyRead(200);
            if (!m_running.load()) {
                break;
            }

            if (!ready) {
                continue;
            }

            while (socket->hasPendingDatagrams()) {
                const qint64 pendingSize = socket->pendingDatagramSize();
                if (pendingSize <= 0) {
                    break;
                }

                LByteArray packet;
                packet.resize(static_cast<int>(pendingSize));

                LHostAddress senderAddress;
                quint16 senderPort = 0;
                const qint64 recvBytes = socket->readDatagram(
                    packet.data(),
                    packet.size(),
                    &senderAddress,
                    &senderPort
                );

                if (recvBytes <= 0) {
                    continue;
                }

                const size_t packetSize = static_cast<size_t>(recvBytes);
                if (packetSize > m_maxPacketSize.load()) {
                    continue;
                }

                LMessage message;
                if (!message.deserialize(
                        reinterpret_cast<const uint8_t*>(packet.constData()),
                        packetSize)) {
                    continue;
                }

                message.setSenderAddress(senderAddress);
                message.setSenderPort(senderPort);

                m_recvPacketCount.fetch_add(1);
                m_recvByteCount.fetch_add(static_cast<uint64_t>(packetSize));
                readyMessages.push_back(std::move(message));
                endpoints.emplace_back(senderAddress, senderPort);
            }
        }

        for (size_t i = 0; i < readyMessages.size(); ++i) {
            notifyReceiveCallback(readyMessages[i], endpoints[i].first, endpoints[i].second);
        }
    }
}

bool LUdpTransport::initializeSocket()
{
    const TransportConfig config = getConfig();

    LHostAddress bindAddress = config.bindAddress.isEmpty()
        ? LHostAddress::AnyIPv4
        : LHostAddress(config.bindAddress);
    if (bindAddress.isNull()) {
        setError(LStringLiteral("Bind address must be valid"));
        return false;
    }

    auto receiveSocket = std::make_unique<LUdpSocket>();
    auto sendSocket = std::make_unique<LUdpSocket>();

    if (config.receiveBufferSize > 0) {
        receiveSocket->setSocketOption(
            LAbstractSocket::ReceiveBufferSizeSocketOption,
            config.receiveBufferSize
        );
        sendSocket->setSocketOption(
            LAbstractSocket::ReceiveBufferSizeSocketOption,
            config.receiveBufferSize
        );
    }

    if (config.sendBufferSize > 0) {
        receiveSocket->setSocketOption(
            LAbstractSocket::SendBufferSizeSocketOption,
            config.sendBufferSize
        );
        sendSocket->setSocketOption(
            LAbstractSocket::SendBufferSizeSocketOption,
            config.sendBufferSize
        );
    }

    LAbstractSocket::BindMode bindMode = LAbstractSocket::DefaultForPlatform;
    if (config.reuseAddress) {
        bindMode = LAbstractSocket::ShareAddress | LAbstractSocket::ReuseAddressHint;
    }

    if (!receiveSocket->bind(bindAddress, config.bindPort, bindMode)) {
        setError(LStringLiteral("UDP bind failed: %1").arg(receiveSocket->errorString()));
        return false;
    }

    if (!sendSocket->bind(LHostAddress::AnyIPv4, 0, bindMode)) {
        setError(LStringLiteral("UDP send socket bind failed: %1").arg(sendSocket->errorString()));
        return false;
    }

    if (config.enableMulticast) {
        const LString multicastGroupText = config.multicastGroup.trimmed();
        if (multicastGroupText.isEmpty()) {
            setError(LStringLiteral("Multicast is enabled but multicastGroup is empty"));
            return false;
        }

        const LHostAddress multicastGroup(multicastGroupText);
        if (multicastGroup.isNull() || !multicastGroup.isMulticast()) {
            setError(LStringLiteral("Invalid multicast group: %1").arg(multicastGroupText));
            return false;
        }

        if (!receiveSocket->joinMulticastGroup(multicastGroup)) {
            setError(
                LStringLiteral("joinMulticastGroup failed (%1): %2")
                    .arg(multicastGroupText, receiveSocket->errorString()));
            return false;
        }

        if (config.multicastTtl > 0) {
            sendSocket->setSocketOption(
                LAbstractSocket::MulticastTtlOption,
                config.multicastTtl
            );
        }
    }

    setBoundPort(receiveSocket->localPort());

    {
        std::lock_guard<std::mutex> recvLock(m_receiveSocketMutex);
        std::lock_guard<std::mutex> sendLock(m_sendSocketMutex);
        m_receiveSocket = std::move(receiveSocket);
        m_sendSocket = std::move(sendSocket);
    }

    return true;
}

void LUdpTransport::closeSocket()
{
    std::unique_ptr<LUdpSocket> receiveSocket;
    std::unique_ptr<LUdpSocket> sendSocket;
    {
        std::lock_guard<std::mutex> recvLock(m_receiveSocketMutex);
        std::lock_guard<std::mutex> sendLock(m_sendSocketMutex);
        receiveSocket = std::move(m_receiveSocket);
        sendSocket = std::move(m_sendSocket);
    }

    if (receiveSocket) {
        receiveSocket->close();
    }
    if (sendSocket) {
        sendSocket->close();
    }

    setBoundPort(0);
}

bool LUdpTransport::resolveRemoteFromConfig(LHostAddress& targetAddress, quint16& targetPort) const
{
    const TransportConfig config = getConfig();
    if (config.remoteAddress.isEmpty() || config.remotePort == 0) {
        return false;
    }

    const LHostAddress parsed(config.remoteAddress);
    if (parsed.isNull()) {
        return false;
    }

    targetAddress = parsed;
    targetPort = config.remotePort;
    return true;
}

} // namespace LDdsFramework
