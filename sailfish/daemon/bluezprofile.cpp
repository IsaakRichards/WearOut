// SPDX-License-Identifier: Apache-2.0
#include "bluezprofile.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDebug>

#include <unistd.h>

BluezProfile::BluezProfile(const QString &uuid, Role role, const QString &objectPath, QObject *parent)
    : QObject(parent), m_uuid(uuid), m_role(role), m_path(objectPath)
{
    new BluezProfileAdaptor(this);
}

QString BluezProfile::devicePathFor(const QString &address, const QString &adapter)
{
    QString a = address.toUpper();
    a.replace(QLatin1Char(':'), QLatin1Char('_'));
    return QStringLiteral("/org/bluez/%1/dev_%2").arg(adapter, a);
}

bool BluezProfile::registerWithBluez()
{
    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.registerObject(m_path, this, QDBusConnection::ExportAdaptors)) {
        emit error(QStringLiteral("cannot export profile object %1").arg(m_path));
        return false;
    }

    QVariantMap opts;
    opts.insert(QStringLiteral("Name"), QStringLiteral("WearOut"));
    opts.insert(QStringLiteral("Role"), m_role == Client ? QStringLiteral("client") : QStringLiteral("server"));
    opts.insert(QStringLiteral("RequireAuthentication"), true);
    opts.insert(QStringLiteral("RequireAuthorization"), false);
    opts.insert(QStringLiteral("AutoConnect"), m_role == Server);

    QDBusInterface mgr(QStringLiteral("org.bluez"), QStringLiteral("/org/bluez"),
                       QStringLiteral("org.bluez.ProfileManager1"), bus);
    QDBusMessage reply = mgr.call(QStringLiteral("RegisterProfile"), QVariant::fromValue(QDBusObjectPath(m_path)),
                                  m_uuid, opts);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        emit error(QStringLiteral("RegisterProfile(%1) failed: %2").arg(m_uuid, reply.errorMessage()));
        return false;
    }
    qInfo() << "registered BlueZ profile" << m_uuid << (m_role == Client ? "client" : "server");
    return true;
}

void BluezProfile::connectDevice(const QString &devicePath)
{
    QDBusInterface dev(QStringLiteral("org.bluez"), devicePath, QStringLiteral("org.bluez.Device1"),
                       QDBusConnection::systemBus());
    // Async: BlueZ answers only after SDP + RFCOMM complete (or fail).
    QDBusPendingCall call = dev.asyncCall(QStringLiteral("ConnectProfile"), m_uuid);
    auto *w = new QDBusPendingCallWatcher(call, this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, devicePath](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<> r = *w;
        if (r.isError())
            emit error(QStringLiteral("ConnectProfile %1 on %2: %3").arg(m_uuid, devicePath, r.error().message()));
        w->deleteLater();
    });
}

BluezProfileAdaptor::BluezProfileAdaptor(BluezProfile *profile)
    : QDBusAbstractAdaptor(profile), m_profile(profile)
{
}

void BluezProfileAdaptor::Release()
{
    qInfo() << "BlueZ released profile" << m_profile->m_uuid;
}

void BluezProfileAdaptor::NewConnection(const QDBusObjectPath &device, const QDBusUnixFileDescriptor &fd,
                                        const QVariantMap &properties)
{
    Q_UNUSED(properties)
    // QDBusUnixFileDescriptor closes its fd on destruction; keep our own copy.
    int own = ::dup(fd.fileDescriptor());
    if (own < 0) {
        emit m_profile->error(QStringLiteral("dup() of RFCOMM fd failed"));
        return;
    }
    qInfo() << "RFCOMM connection for" << m_profile->m_uuid << "from" << device.path();
    emit m_profile->newConnection(device.path(), own);
}

void BluezProfileAdaptor::RequestDisconnection(const QDBusObjectPath &device)
{
    emit m_profile->disconnectRequested(device.path());
}
