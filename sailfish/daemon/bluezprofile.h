// SPDX-License-Identifier: Apache-2.0
//
// Registers an org.bluez.Profile1 for one RFCOMM service UUID. BlueZ does
// the SDP lookup and the RFCOMM connect for us and hands over a connected
// socket file descriptor in NewConnection(). This avoids linking against
// libbluetooth and works the same for client and server roles.
#pragma once

#include <QDBusAbstractAdaptor>
#include <QDBusObjectPath>
#include <QDBusUnixFileDescriptor>
#include <QObject>
#include <QVariantMap>

class BluezProfile : public QObject
{
    Q_OBJECT
public:
    enum Role { Client, Server };

    BluezProfile(const QString &uuid, Role role, const QString &objectPath, QObject *parent = nullptr);

    bool registerWithBluez();
    // Ask BlueZ to connect this profile to the given device (client role).
    // `devicePath` looks like /org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF
    void connectDevice(const QString &devicePath);

    QString uuid() const { return m_uuid; }
    static QString devicePathFor(const QString &address, const QString &adapter = QStringLiteral("hci0"));

signals:
    // fd is owned by the receiver (already dup()'d).
    void newConnection(const QString &devicePath, int fd);
    void disconnectRequested(const QString &devicePath);
    void error(const QString &message);

private:
    friend class BluezProfileAdaptor;
    QString m_uuid;
    Role m_role;
    QString m_path;
};

// D-Bus surface BlueZ calls into.
class BluezProfileAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.bluez.Profile1")
public:
    explicit BluezProfileAdaptor(BluezProfile *profile);

public slots:
    void Release();
    void NewConnection(const QDBusObjectPath &device, const QDBusUnixFileDescriptor &fd,
                       const QVariantMap &properties);
    void RequestDisconnection(const QDBusObjectPath &device);

private:
    BluezProfile *m_profile;
};
