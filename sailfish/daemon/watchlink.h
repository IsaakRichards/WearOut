// SPDX-License-Identifier: Apache-2.0
//
// Glue between a connected socket (RFCOMM from BlueZ, or TCP for the
// emulator) and the transport-agnostic wearout::Session.
#pragma once

#include "wearout/session.h"

#include <QObject>
#include <QSocketNotifier>
#include <QTimer>

#include <memory>

class WatchLink : public QObject
{
    Q_OBJECT
public:
    WatchLink(int fd, const wearout::SessionConfig &cfg, QObject *parent = nullptr);
    ~WatchLink() override;

    wearout::Session &session() { return *m_session; }
    QString stateName() const;

    void sendMessage(const QString &path, const QByteArray &data, const QString &pkg, const QString &digest);

signals:
    void stateChanged(const QString &state);
    void peerConnected(const QString &nodeId, const QString &name);
    void messageReceived(const QString &path, const QString &sourceNode, const QString &package,
                         const QByteArray &data);
    void dataItemChanged(const QString &uri, const QString &package, const QByteArray &data);
    void trace(const QString &line);
    void closed(const QString &reason);

private:
    void onReadable();
    void shutdown(const QString &reason);

    int m_fd;
    std::unique_ptr<wearout::Session> m_session;
    QSocketNotifier *m_notifier;
    QTimer m_ticker;
    bool m_closed = false;
};
