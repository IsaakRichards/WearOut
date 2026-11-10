// SPDX-License-Identifier: Apache-2.0
#include "watchlink.h"

#include <QDateTime>
#include <QDebug>

#include <cerrno>
#include <sys/socket.h>
#include <unistd.h>

namespace {
int64_t now() { return QDateTime::currentMSecsSinceEpoch(); }
QString qs(const std::string &s) { return QString::fromStdString(s); }
QByteArray qb(const wearout::pb::Bytes &b) { return QByteArray(reinterpret_cast<const char *>(b.data()), int(b.size())); }
} // namespace

WatchLink::WatchLink(int fd, const wearout::SessionConfig &cfg, QObject *parent)
    : QObject(parent), m_fd(fd)
{
    wearout::Session::Callbacks cb;
    cb.send = [this](const wearout::pb::Bytes &bytes) {
        size_t off = 0;
        while (off < bytes.size()) {
            ssize_t n = ::send(m_fd, bytes.data() + off, bytes.size() - off, MSG_NOSIGNAL);
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0) {
                // Defer: we may be inside Session code right now. The owner
                // deletes the link when it sees closed().
                QTimer::singleShot(0, this, [this] { shutdown(QStringLiteral("write failed")); });
                return;
            }
            off += size_t(n);
        }
    };
    cb.event = [this](const wearout::SessionEvent &e) {
        using T = wearout::SessionEvent::Type;
        switch (e.type) {
        case T::StateChanged: emit stateChanged(qs(wearout::stateName(e.state))); break;
        case T::PeerConnected:
            emit peerConnected(qs(m_session->peer().nodeId), qs(m_session->peer().name));
            break;
        case T::MessageReceived:
            emit messageReceived(qs(e.path), qs(e.sourceNodeId), qs(e.packageName), qb(e.data));
            break;
        case T::DataItemChanged: emit dataItemChanged(qs(e.path), qs(e.packageName), qb(e.data)); break;
        case T::Error: emit trace(QStringLiteral("error: ") + qs(e.error)); break;
        default: break;
        }
    };
    cb.trace = [this](const char *dir, const wearout::msg::RootMessage &m, const wearout::pb::Bytes &raw) {
        emit trace(QStringLiteral("%1 %2 (%3 bytes)").arg(QLatin1String(dir),
                                                       QLatin1String(wearout::msg::kindName(m.kind())))
                       .arg(raw.size()));
        if (m.kind() == wearout::msg::RootMessage::Kind::Unknown || !m.unknown.empty())
            emit trace(qs(wearout::pb::dump(raw, 1)));
    };

    m_session.reset(new wearout::Session(cfg, cb));

    m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, [this] { onReadable(); });

    m_ticker.setInterval(1000);
    connect(&m_ticker, &QTimer::timeout, this, [this] {
        m_session->tick(now());
        if (m_session->state() == wearout::SessionState::Closed)
            shutdown(QStringLiteral("session closed"));
    });
    m_ticker.start();

    m_session->start(now());
}

WatchLink::~WatchLink()
{
    if (m_fd >= 0)
        ::close(m_fd);
}

QString WatchLink::stateName() const
{
    return qs(wearout::stateName(m_session->state()));
}

void WatchLink::onReadable()
{
    uint8_t buf[16384];
    ssize_t n = ::recv(m_fd, buf, sizeof buf, 0);
    if (n < 0 && (errno == EAGAIN || errno == EINTR))
        return;
    if (n <= 0) {
        shutdown(QStringLiteral("remote closed"));
        return;
    }
    m_session->onBytes(buf, size_t(n), now());
    if (m_session->state() == wearout::SessionState::Closed)
        shutdown(QStringLiteral("protocol error"));
}

void WatchLink::sendMessage(const QString &path, const QByteArray &data, const QString &pkg, const QString &digest)
{
    wearout::pb::Bytes b(data.begin(), data.end());
    m_session->sendMessage(path.toStdString(), b, pkg.toStdString(), digest.toStdString());
}

void WatchLink::shutdown(const QString &reason)
{
    if (m_closed)
        return;
    m_closed = true;
    m_ticker.stop();
    m_notifier->setEnabled(false);
    emit closed(reason);
}
