// SPDX-License-Identifier: Apache-2.0
#include "wearout/session.h"

#include <cstdio>
#include <random>

namespace wearout {

const char *stateName(SessionState s) {
    switch (s) {
    case SessionState::Idle: return "Idle";
    case SessionState::Handshaking: return "Handshaking";
    case SessionState::Connected: return "Connected";
    case SessionState::Closed: return "Closed";
    }
    return "?";
}

std::string Session::randomNodeId() {
    std::random_device rd;
    std::uniform_int_distribution<uint32_t> d;
    char buf[9];
    std::snprintf(buf, sizeof buf, "%08x", d(rd));
    return buf;
}

Session::Session(SessionConfig cfg, Callbacks cb) : m_cfg(std::move(cfg)), m_cb(std::move(cb)) {
    if (m_cfg.localNodeId.empty())
        m_cfg.localNodeId = randomNodeId();
}

void Session::setState(SessionState s) {
    if (s == m_state)
        return;
    m_state = s;
    if (m_cb.event) {
        SessionEvent e{SessionEvent::Type::StateChanged};
        e.state = s;
        m_cb.event(e);
    }
}

void Session::fail(const std::string &why) {
    if (m_cb.event) {
        SessionEvent e{SessionEvent::Type::Error};
        e.error = why;
        m_cb.event(e);
    }
    setState(SessionState::Closed);
}

void Session::sendRoot(const msg::RootMessage &m, int64_t nowMs) {
    if (m_state == SessionState::Closed)
        return;
    pb::Bytes wire = encodeMessage(m, m_nextQueueId);
    m_nextQueueId = (m_nextQueueId % 1'000'000) + 1;
    if (m_cb.trace)
        m_cb.trace(">>", m, m.encode());
    if (m_cb.send)
        m_cb.send(wire);
    m_lastSentMs = nowMs;
}

void Session::sendOrQueue(msg::RootMessage m, int64_t nowMs) {
    if (m_state == SessionState::Connected)
        sendRoot(m, nowMs);
    else if (m_state != SessionState::Closed)
        m_pending.push_back(std::move(m));
}

void Session::start(int64_t nowMs) {
    if (m_state != SessionState::Idle)
        return;
    m_lastRecvMs = nowMs;
    setState(SessionState::Handshaking);

    msg::Connect c;
    c.id = m_cfg.localNodeId;
    c.name = m_cfg.localName;
    c.peerAndroidId = m_cfg.localAndroidId;
    c.unknown4 = m_cfg.connectUnknown4;
    c.peerVersion = m_cfg.protocolVersion;
    c.peerMinimumVersion = m_cfg.minimumVersion;
    c.networkId = m_cfg.localNodeId;
    sendRoot(msg::RootMessage::makeConnect(std::move(c)), nowMs);
}

void Session::onBytes(const uint8_t *data, size_t len, int64_t nowMs) {
    if (m_state == SessionState::Closed)
        return;
    m_lastRecvMs = nowMs;
    try {
        m_decoder.feed(data, len);
        while (auto d = m_decoder.next()) {
            handle(*d, nowMs);
            if (m_state == SessionState::Closed)
                return;
        }
    } catch (const FramingError &e) {
        fail(e.what());
    }
}

void Session::handle(const StreamDecoder::Decoded &d, int64_t nowMs) {
    const msg::RootMessage &m = d.message;
    if (m_cb.trace)
        m_cb.trace("<<", m, d.raw);

    auto emit = [&](SessionEvent e) {
        e.raw = d.raw;
        if (m_cb.event)
            m_cb.event(e);
    };

    switch (m.kind()) {
    case msg::RootMessage::Kind::Connect: {
        const msg::Connect &c = *m.connect;
        m_peer.nodeId = c.id.value_or("");
        m_peer.name = c.name.value_or("");
        m_peer.androidId = c.peerAndroidId.value_or(0);
        m_peer.version = c.peerVersion.value_or(0);
        m_peer.minimumVersion = c.peerMinimumVersion.value_or(0);
        m_peer.networkId = c.networkId.value_or("");
        if (m_peer.minimumVersion > m_cfg.protocolVersion) {
            fail("peer requires protocol version >= " + std::to_string(m_peer.minimumVersion) +
                 ", we speak " + std::to_string(m_cfg.protocolVersion) +
                 " (override with --proto-version to experiment)");
            return;
        }
        setState(SessionState::Connected);
        emit(SessionEvent{SessionEvent::Type::PeerConnected});

        if (m_cfg.sendSyncStartOnConnect) {
            msg::SyncStart s;
            s.receivedSeqId = m_peerSeqSeen;
            s.version = m_cfg.syncVersion;
            s.syncTable.push_back({m_cfg.localNodeId, m_localSeq});
            if (!m_peer.nodeId.empty())
                s.syncTable.push_back({m_peer.nodeId, m_peerSeqSeen});
            sendRoot(msg::RootMessage::makeSyncStart(std::move(s)), nowMs);
        }
        for (auto &p : m_pending) {
            if (p.rpcRequest && p.rpcRequest->targetNodeId.value_or("").empty())
                p.rpcRequest->targetNodeId = m_peer.nodeId;
            sendRoot(p, nowMs);
        }
        m_pending.clear();
        break;
    }
    case msg::RootMessage::Kind::Heartbeat:
        // Nothing to do: receipt already refreshed m_lastRecvMs.
        break;
    case msg::RootMessage::Kind::RpcRequest: {
        const msg::Request &r = *m.rpcRequest;
        SessionEvent e{SessionEvent::Type::MessageReceived};
        e.path = r.path.value_or("");
        e.packageName = r.packageName.value_or("");
        e.sourceNodeId = r.sourceNodeId.value_or(m_peer.nodeId);
        e.data = r.rawData.value_or(pb::Bytes{});
        emit(std::move(e));
        break;
    }
    case msg::RootMessage::Kind::SetDataItem: {
        const msg::SetDataItem &item = *m.setDataItem;
        const std::string uri = item.uri.value_or("");
        if (item.seqId && *item.seqId > m_peerSeqSeen)
            m_peerSeqSeen = *item.seqId;
        if (item.deleted.value_or(false))
            m_items.erase(uri);
        else
            m_items[uri] = item;
        SessionEvent e{SessionEvent::Type::DataItemChanged};
        e.path = uri;
        e.packageName = item.packageName.value_or("");
        e.sourceNodeId = item.source.value_or("");
        e.data = item.data.value_or(pb::Bytes{});
        emit(std::move(e));
        break;
    }
    case msg::RootMessage::Kind::ChannelRequest:
        emit(SessionEvent{SessionEvent::Type::ChannelTraffic});
        break;
    default:
        emit(SessionEvent{SessionEvent::Type::Unhandled});
        break;
    }
}

void Session::tick(int64_t nowMs) {
    if (m_state == SessionState::Idle || m_state == SessionState::Closed)
        return;
    if (m_cfg.peerTimeoutMs > 0 && nowMs - m_lastRecvMs > m_cfg.peerTimeoutMs) {
        fail("peer silent for too long");
        return;
    }
    if (m_state == SessionState::Connected && m_cfg.heartbeatIntervalMs > 0 &&
        nowMs - m_lastSentMs >= m_cfg.heartbeatIntervalMs) {
        sendRoot(msg::RootMessage::makeHeartbeat(), nowMs);
    }
}

int32_t Session::sendMessage(const std::string &path, const pb::Bytes &data, const std::string &packageName,
                             const std::string &signatureDigest) {
    msg::Request r;
    r.requestId = m_nextRequestId++;
    r.packageName = packageName;
    r.signatureDigest = signatureDigest;
    r.targetNodeId = m_peer.nodeId;
    r.path = path;
    r.rawData = data;
    r.sourceNodeId = m_cfg.localNodeId;
    sendOrQueue(msg::RootMessage::makeRpc(r), m_lastSentMs);
    return *r.requestId;
}

void Session::putDataItem(const std::string &path, const pb::Bytes &data, const std::string &packageName,
                          const std::string &signatureDigest, int64_t nowMs) {
    msg::SetDataItem item;
    item.packageName = packageName;
    item.uri = "wear://" + m_cfg.localNodeId + (path.empty() || path[0] != '/' ? "/" : "") + path;
    item.data = data;
    item.seqId = ++m_localSeq;
    item.deleted = false;
    item.source = m_cfg.localNodeId;
    item.signatureDigest = signatureDigest;
    item.lastModified = nowMs;
    msg::RootMessage m;
    m.setDataItem = std::move(item);
    sendOrQueue(std::move(m), nowMs);
}

} // namespace wearout
