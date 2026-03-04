#ifndef ITRANSPORT_H
#define ITRANSPORT_H

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <QString>
#include <QHostAddress>
#include "LDds_Global.h"

namespace LDdsFramework {

class LMessage;

enum class TransportProtocol {
    UDP,
    TCP
};

struct LDDSCORE_EXPORT TransportConfig {
    QString bindAddress;
    quint16 bindPort;
    QString remoteAddress;
    quint16 remotePort;
    bool enableBroadcast;
    int receiveBufferSize;
    int sendBufferSize;
    int maxConnections;
    bool reuseAddress;

    TransportConfig();
};

using ReceiveCallback = std::function<void(const LMessage&, const QHostAddress&, quint16)>;

enum class TransportState {
    Stopped,
    Starting,
    Running,
    Stopping,
    Error
};

class LDDSCORE_EXPORT ITransport
{
public:
    ITransport();
    virtual ~ITransport();

    ITransport(const ITransport& other) = delete;
    ITransport& operator=(const ITransport& other) = delete;

    virtual TransportProtocol getProtocol() const noexcept = 0;
    virtual bool start() = 0;
    virtual void stop() = 0;

    // Required stage-2 API: send to the configured/default remote endpoint.
    virtual bool sendMessage(const LMessage& message) = 0;

    // Reserved API. UDP overrides this, TCP keeps "send to all connections".
    virtual bool broadcastMessage(const LMessage& message, quint16 broadcastPort);

    // Shared helper for transports that send to a configured default endpoint.
    virtual bool setDefaultRemote(const QHostAddress& targetAddress, quint16 targetPort);

    void setReceiveCallback(ReceiveCallback callback);
    TransportState getState() const noexcept;
    TransportConfig getConfig() const;
    void setConfig(const TransportConfig& config);
    QString getLastError() const noexcept;
    quint16 getBoundPort() const noexcept;

    static std::unique_ptr<ITransport> createTransport(
        TransportProtocol protocol = TransportProtocol::UDP
    );

protected:
    void setState(TransportState state) noexcept;
    void setError(const QString& error);
    void notifyReceiveCallback(const LMessage& message,
                               const QHostAddress& senderAddress,
                               quint16 senderPort);
    void setBoundPort(quint16 port) noexcept;

protected:
    TransportConfig m_config;
    std::atomic<TransportState> m_state;
    std::atomic<quint16> m_boundPort;
    ReceiveCallback m_receiveCallback;
    mutable std::mutex m_callbackMutex;
    QString m_lastError;
    mutable std::mutex m_errorMutex;
    mutable std::mutex m_configMutex;
};

} // namespace LDdsFramework

#endif // ITRANSPORT_H
