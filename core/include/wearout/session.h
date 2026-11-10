// SPDX-License-Identifier: Apache-2.0
//
// Transport-agnostic "phone side" of a Wearable link.
//
// The session never touches sockets: you hand it bytes that arrived
// (onBytes) and it hands you bytes to write (Callbacks::send). That keeps it
// usable from the Qt/BlueZ daemon on Sailfish, the TCP CLI used against the
// Wear OS emulator, and unit tests alike.
//
// IMPORTANT: the handshake constants below are *provisional*. They come from
// the 2015-era microG implementation and public research. Modern Wear OS
// (4/5/6) may require different values or extra steps; captures from a real
// pairing are needed to confirm them. They are all overridable via Config.
#pragma once

#include "wearout/framing.h"
#include "wearout/messages.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace wearout {

struct SessionConfig {
    std::string localNodeId;          // random hex, stable per install
    std::string localName = "Sailfish";
    int64_t localAndroidId = 0;

    // Connect.unknown4 / peerVersion / peerMinimumVersion that we advertise.
    int32_t connectUnknown4 = 3;
    int32_t protocolVersion = 2;
    int32_t minimumVersion = 0;

    // SyncStart.version we advertise after the peer's Connect.
    int32_t syncVersion = 2;
    bool sendSyncStartOnConnect = true;

    // Heartbeats we send while connected (0 disables).
    int64_t heartbeatIntervalMs = 30'000;
    // Drop the link if nothing at all arrives for this long (0 disables).
    int64_t peerTimeoutMs = 120'000;
};

enum class SessionState {
    Idle,         // not started
    Handshaking,  // our Connect sent, waiting for theirs
    Connected,    // peer Connect received
    Closed,       // protocol error or timeout; recreate the session
};

const char *stateName(SessionState s);

struct PeerInfo {
    std::string nodeId;
    std::string name;
    int64_t androidId = 0;
    int32_t version = 0;
    int32_t minimumVersion = 0;
    std::string networkId;
};

struct SessionEvent {
    enum class Type {
        StateChanged,
        PeerConnected,
        MessageReceived,   // MessageClient (rpcRequest)
        DataItemChanged,   // DataClient (setDataItem)
        ChannelTraffic,    // ChannelClient (not decoded yet)
        Unhandled,         // anything else - see `raw`
        Error,
    };
    explicit SessionEvent(Type t = Type::Unhandled) : type(t) {}

    Type type;
    SessionState state = SessionState::Idle;
    std::string path;        // message path or data item uri
    std::string packageName; // target/owning app
    std::string sourceNodeId;
    pb::Bytes data;
    pb::Bytes raw;           // full RootMessage bytes, for logging/dumping
    std::string error;
};

class Session {
public:
    struct Callbacks {
        std::function<void(const pb::Bytes &)> send;
        std::function<void(const SessionEvent &)> event;
        // Optional wire log: direction is "<<" (received) or ">>" (sent).
        std::function<void(const char *dir, const msg::RootMessage &, const pb::Bytes &raw)> trace;
    };

    Session(SessionConfig cfg, Callbacks cb);

    // Sends our Connect. Call once the transport is up.
    void start(int64_t nowMs);
    // Feed bytes read from the transport.
    void onBytes(const uint8_t *data, size_t len, int64_t nowMs);
    // Drive timers (heartbeat, timeout). Call periodically, e.g. every second.
    void tick(int64_t nowMs);

    // MessageClient.sendMessage equivalent. Returns the request id used.
    // packageName/signatureDigest must match the watch-side app's package and
    // signing certificate, otherwise Wear OS drops the message.
    int32_t sendMessage(const std::string &path, const pb::Bytes &data, const std::string &packageName,
                        const std::string &signatureDigest);

    // DataClient.putDataItem equivalent (uri = wear://<localNode><path>).
    void putDataItem(const std::string &path, const pb::Bytes &data, const std::string &packageName,
                     const std::string &signatureDigest, int64_t nowMs);

    SessionState state() const { return m_state; }
    const PeerInfo &peer() const { return m_peer; }
    const SessionConfig &config() const { return m_cfg; }

    // Data items received from the peer, keyed by uri.
    const std::map<std::string, msg::SetDataItem> &dataItems() const { return m_items; }

    static std::string randomNodeId();

private:
    void sendRoot(const msg::RootMessage &m, int64_t nowMs);
    void sendOrQueue(msg::RootMessage m, int64_t nowMs);
    void handle(const StreamDecoder::Decoded &d, int64_t nowMs);
    void setState(SessionState s);
    void fail(const std::string &why);

    SessionConfig m_cfg;
    Callbacks m_cb;
    StreamDecoder m_decoder;
    SessionState m_state = SessionState::Idle;
    PeerInfo m_peer;
    int32_t m_nextRequestId = 1;
    int32_t m_nextQueueId = 1;
    int64_t m_localSeq = 0;
    int64_t m_peerSeqSeen = -1;
    int64_t m_lastSentMs = 0;
    int64_t m_lastRecvMs = 0;
    std::map<std::string, msg::SetDataItem> m_items;
    // App traffic queued until the handshake completes.
    std::vector<msg::RootMessage> m_pending;
};

} // namespace wearout
