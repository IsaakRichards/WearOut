// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "bluezprofile.h"
#include "watchlink.h"

#include <QDBusObjectPath>
#include <QMap>
#include <QObject>
#include <QVariantMap>
#include <QPointer>
#include <QSettings>

typedef QMap<QString, QVariantMap> BluezInterfaces;
typedef QMap<QDBusObjectPath, BluezInterfaces> BluezManagedObjects;
Q_DECLARE_METATYPE(BluezInterfaces)
Q_DECLARE_METATYPE(BluezManagedObjects)

// Session-bus service org.wearout.daemon at /org/wearout/Daemon, consumed by
// the Silica UI through Nemo.DBus.
class Daemon : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.wearout.Daemon")
public:
    explicit Daemon(QObject *parent = nullptr);
    bool start();

public slots:
    Q_SCRIPTABLE void connectWatch(const QString &address);
    Q_SCRIPTABLE void connectEmulator(const QString &hostPort);
    Q_SCRIPTABLE void disconnectWatch();
    Q_SCRIPTABLE void sendMessage(const QString &path, const QString &text, const QString &package,
                                  const QString &signatureDigest);
    Q_SCRIPTABLE QString state() const;
    Q_SCRIPTABLE QString watchAddress() const;
    Q_SCRIPTABLE QString peerName() const;
    // Paired BlueZ devices that match a known watch profile, as
    // "AA:BB:CC:DD:EE:FF|Bluetooth name|profile-id" strings.
    Q_SCRIPTABLE QStringList findWatches() const;
    Q_SCRIPTABLE QString activeProfile() const;

signals:
    Q_SCRIPTABLE void stateChanged(const QString &state);
    Q_SCRIPTABLE void peerChanged(const QString &name);
    Q_SCRIPTABLE void log(const QString &line);
    Q_SCRIPTABLE void messageReceived(const QString &path, const QString &package, const QString &text);

private:
    void attach(int fd);
    void setState(const QString &s);
    wearout::SessionConfig sessionConfig();

    QSettings m_settings;
    BluezProfile *m_wearableBt = nullptr;
    QPointer<WatchLink> m_link;
    QString m_state = QStringLiteral("Disconnected");
    QString m_peerName;
    QString m_profileId;
};
