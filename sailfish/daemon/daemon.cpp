// SPDX-License-Identifier: Apache-2.0
#include "daemon.h"

#include "wearout/devices.h"

#include <QDBusConnection>
#include <QDBusMetaType>
#include <QDBusReply>
#include <QDebug>

#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
// WearableBt: the watch is the RFCOMM server, the phone connects as client.
// Source: teccheck/wearos-research docs/btcomm.md (Pixel Watch 1).
const QString kWearableBtUuid = QStringLiteral("5e8945b0-9525-11e3-a5e2-0800200c9a66");
} // namespace

Daemon::Daemon(QObject *parent)
    : QObject(parent), m_settings(QStringLiteral("wearout"), QStringLiteral("wearoutd"))
{
}

bool Daemon::start()
{
    qDBusRegisterMetaType<BluezInterfaces>();
    qDBusRegisterMetaType<BluezManagedObjects>();

    m_wearableBt = new BluezProfile(kWearableBtUuid, BluezProfile::Client,
                                    QStringLiteral("/org/wearout/profile/wearablebt"), this);
    connect(m_wearableBt, &BluezProfile::error, this, &Daemon::log);
    connect(m_wearableBt, &BluezProfile::newConnection, this, [this](const QString &dev, int fd) {
        emit log(QStringLiteral("RFCOMM up from %1").arg(dev));
        attach(fd);
    });
    connect(m_wearableBt, &BluezProfile::disconnectRequested, this, [this](const QString &) { disconnectWatch(); });
    if (!m_wearableBt->registerWithBluez())
        qWarning() << "BlueZ profile registration failed; only emulator mode will work";

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerService(QStringLiteral("org.wearout.daemon"))) {
        qCritical() << "org.wearout.daemon already taken - is another wearoutd running?";
        return false;
    }
    bus.registerObject(QStringLiteral("/org/wearout/Daemon"), this,
                       QDBusConnection::ExportScriptableSlots | QDBusConnection::ExportScriptableSignals);

    QString target = watchAddress();
    if (target.isEmpty()) {
        // First run: pick the first paired watch we recognise.
        const QStringList found = findWatches();
        if (!found.isEmpty())
            target = found.first().section(QLatin1Char('|'), 0, 0);
    }
    if (!target.isEmpty())
        connectWatch(target);
    return true;
}

wearout::SessionConfig Daemon::sessionConfig()
{
    QString node = m_settings.value(QStringLiteral("localNodeId")).toString();
    if (node.isEmpty()) {
        node = QString::fromStdString(wearout::Session::randomNodeId());
        m_settings.setValue(QStringLiteral("localNodeId"), node);
    }
    wearout::SessionConfig cfg;
    cfg.localNodeId = node.toStdString();
    cfg.localName = m_settings.value(QStringLiteral("deviceName"), QStringLiteral("Sailfish")).toString().toStdString();
    // Experimental knobs, see docs/PROTOCOL.md
    cfg.protocolVersion = m_settings.value(QStringLiteral("protocolVersion"), cfg.protocolVersion).toInt();
    cfg.connectUnknown4 = m_settings.value(QStringLiteral("connectUnknown4"), cfg.connectUnknown4).toInt();
    return cfg;
}

void Daemon::setState(const QString &s)
{
    if (s == m_state)
        return;
    m_state = s;
    emit stateChanged(s);
}

void Daemon::attach(int fd)
{
    if (m_link)
        m_link->deleteLater();
    m_link = new WatchLink(fd, sessionConfig(), this);
    connect(m_link, &WatchLink::stateChanged, this, &Daemon::setState);
    connect(m_link, &WatchLink::trace, this, &Daemon::log);
    connect(m_link, &WatchLink::trace, this, [](const QString &l) { qDebug().noquote() << l; });
    connect(m_link, &WatchLink::peerConnected, this, [this](const QString &id, const QString &name) {
        m_peerName = name.isEmpty() ? id : name;
        emit peerChanged(m_peerName);
    });
    connect(m_link, &WatchLink::messageReceived, this,
            [this](const QString &path, const QString &, const QString &pkg, const QByteArray &data) {
                emit messageReceived(path, pkg, QString::fromUtf8(data));
            });
    connect(m_link, &WatchLink::closed, this, [this](const QString &why) {
        emit log(QStringLiteral("link closed: %1").arg(why));
        setState(QStringLiteral("Disconnected"));
        if (m_link)
            m_link->deleteLater();
    });
    setState(m_link->stateName());
}

QStringList Daemon::findWatches() const
{
    QStringList out;
    QDBusInterface om(QStringLiteral("org.bluez"), QStringLiteral("/"),
                      QStringLiteral("org.freedesktop.DBus.ObjectManager"), QDBusConnection::systemBus());
    QDBusReply<BluezManagedObjects> reply = om.call(QStringLiteral("GetManagedObjects"));
    if (!reply.isValid()) {
        qWarning() << "GetManagedObjects failed:" << reply.error().message();
        return out;
    }
    const BluezManagedObjects objects = reply.value();
    for (auto it = objects.constBegin(); it != objects.constEnd(); ++it) {
        const QVariantMap dev = it.value().value(QStringLiteral("org.bluez.Device1"));
        if (dev.isEmpty() || !dev.value(QStringLiteral("Paired")).toBool())
            continue;
        QString name = dev.value(QStringLiteral("Alias")).toString();
        if (name.isEmpty())
            name = dev.value(QStringLiteral("Name")).toString();
        const wearout::WatchProfile *p = wearout::matchWatch(name.toStdString());
        if (!p)
            continue;
        out << QStringLiteral("%1|%2|%3").arg(dev.value(QStringLiteral("Address")).toString(), name,
                                             QString::fromStdString(p->id));
    }
    return out;
}

QString Daemon::activeProfile() const { return m_profileId; }

void Daemon::connectWatch(const QString &address)
{
    m_profileId.clear();
    for (const QString &w : findWatches()) {
        if (w.section(QLatin1Char('|'), 0, 0).compare(address, Qt::CaseInsensitive) == 0)
            m_profileId = w.section(QLatin1Char('|'), 2, 2);
    }
    emit log(QStringLiteral("profile: %1").arg(m_profileId.isEmpty() ? QStringLiteral("unknown") : m_profileId));
    m_settings.setValue(QStringLiteral("watchAddress"), address);
    const QString path = BluezProfile::devicePathFor(address);
    emit log(QStringLiteral("connecting WearableBt to %1").arg(path));
    setState(QStringLiteral("Connecting"));
    m_wearableBt->connectDevice(path);
}

void Daemon::connectEmulator(const QString &hostPort)
{
    const int colon = hostPort.lastIndexOf(QLatin1Char(':'));
    const QByteArray host = (colon < 0 ? QStringLiteral("127.0.0.1") : hostPort.left(colon)).toUtf8();
    const QByteArray port = (colon < 0 ? hostPort : hostPort.mid(colon + 1)).toUtf8();

    addrinfo hints{}, *res = nullptr;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.constData(), port.constData(), &hints, &res) != 0) {
        emit log(QStringLiteral("cannot resolve %1").arg(hostPort));
        return;
    }
    int fd = -1;
    for (addrinfo *a = res; a; a = a->ai_next) {
        fd = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd >= 0 && ::connect(fd, a->ai_addr, a->ai_addrlen) == 0)
            break;
        if (fd >= 0)
            ::close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) {
        emit log(QStringLiteral("cannot connect to %1").arg(hostPort));
        return;
    }
    emit log(QStringLiteral("TCP up to %1").arg(hostPort));
    attach(fd);
}

void Daemon::disconnectWatch()
{
    if (m_link)
        m_link->deleteLater();
    setState(QStringLiteral("Disconnected"));
}

void Daemon::sendMessage(const QString &path, const QString &text, const QString &package,
                         const QString &signatureDigest)
{
    if (!m_link) {
        emit log(QStringLiteral("not connected"));
        return;
    }
    m_link->sendMessage(path, text.toUtf8(), package, signatureDigest);
}

QString Daemon::state() const { return m_state; }
QString Daemon::watchAddress() const { return m_settings.value(QStringLiteral("watchAddress")).toString(); }
QString Daemon::peerName() const { return m_peerName; }
