// SPDX-License-Identifier: Apache-2.0
#include "wearout/messages.h"

namespace wearout::msg {

using pb::Field;
using pb::WireType;
using pb::Writer;

namespace {

void requireType(const Field &f, WireType t, const char *what) {
    if (f.type != t)
        throw pb::DecodeError(std::string("unexpected wire type for ") + what + " field " +
                              std::to_string(f.number));
}

template <typename T, typename F>
void putOpt(const std::optional<T> &v, F &&fn) {
    if (v)
        fn(*v);
}

void putUnknown(Writer &w, const std::vector<Field> &unknown) {
    // Re-emit unknown fields verbatim so a decode->encode round trip is
    // lossless. Useful when proxying captured traffic.
    for (const Field &f : unknown) {
        switch (f.type) {
        case WireType::Varint:
            w.varint(f.number, f.value);
            break;
        case WireType::LengthDelimited:
            w.bytes(f.number, f.bytes);
            break;
        default:
            // fixed32/64 never observed in this protocol so far; drop rather
            // than mis-encode.
            break;
        }
    }
}

} // namespace

// ---- Connect ---------------------------------------------------------------

Bytes Connect::encode() const {
    Writer w;
    putOpt(id, [&](auto &v) { w.string(1, v); });
    putOpt(name, [&](auto &v) { w.string(2, v); });
    putOpt(peerAndroidId, [&](auto v) { w.int64(3, v); });
    putOpt(unknown4, [&](auto v) { w.int32(4, v); });
    putOpt(peerVersion, [&](auto v) { w.int32(5, v); });
    putOpt(peerMinimumVersion, [&](auto v) { w.int32(6, v); });
    putOpt(networkId, [&](auto &v) { w.string(7, v); });
    putUnknown(w, unknown);
    return w.take();
}

Connect Connect::decode(const Bytes &b) {
    Connect c;
    for (Field &f : pb::parse(b)) {
        switch (f.number) {
        case 1: requireType(f, WireType::LengthDelimited, "Connect"); c.id = f.asString(); break;
        case 2: requireType(f, WireType::LengthDelimited, "Connect"); c.name = f.asString(); break;
        case 3: requireType(f, WireType::Varint, "Connect"); c.peerAndroidId = f.asInt64(); break;
        case 4: requireType(f, WireType::Varint, "Connect"); c.unknown4 = f.asInt32(); break;
        case 5: requireType(f, WireType::Varint, "Connect"); c.peerVersion = f.asInt32(); break;
        case 6: requireType(f, WireType::Varint, "Connect"); c.peerMinimumVersion = f.asInt32(); break;
        case 7: requireType(f, WireType::LengthDelimited, "Connect"); c.networkId = f.asString(); break;
        default: c.unknown.push_back(std::move(f));
        }
    }
    return c;
}

// ---- SyncStart -------------------------------------------------------------

Bytes SyncStart::encode() const {
    Writer w;
    putOpt(receivedSeqId, [&](auto v) { w.int64(1, v); });
    for (const auto &e : syncTable) {
        Writer ew;
        ew.string(1, e.key);
        ew.int64(2, e.value);
        w.message(2, ew.data());
    }
    putOpt(version, [&](auto v) { w.int32(3, v); });
    putUnknown(w, unknown);
    return w.take();
}

SyncStart SyncStart::decode(const Bytes &b) {
    SyncStart s;
    for (Field &f : pb::parse(b)) {
        switch (f.number) {
        case 1: requireType(f, WireType::Varint, "SyncStart"); s.receivedSeqId = f.asInt64(); break;
        case 2: {
            requireType(f, WireType::LengthDelimited, "SyncStart");
            SyncTableEntry e;
            for (Field &ef : pb::parse(f.bytes)) {
                if (ef.number == 1 && ef.type == WireType::LengthDelimited) e.key = ef.asString();
                else if (ef.number == 2 && ef.type == WireType::Varint) e.value = ef.asInt64();
            }
            s.syncTable.push_back(std::move(e));
            break;
        }
        case 3: requireType(f, WireType::Varint, "SyncStart"); s.version = f.asInt32(); break;
        default: s.unknown.push_back(std::move(f));
        }
    }
    return s;
}

// ---- SetDataItem -----------------------------------------------------------

Bytes SetDataItem::encode() const {
    Writer w;
    putOpt(packageName, [&](auto &v) { w.string(1, v); });
    putOpt(uri, [&](auto &v) { w.string(2, v); });
    putOpt(data, [&](auto &v) { w.bytes(4, v); });
    putOpt(seqId, [&](auto v) { w.int64(5, v); });
    putOpt(deleted, [&](auto v) { w.boolean(6, v); });
    putOpt(source, [&](auto &v) { w.string(7, v); });
    for (const auto &a : assetsRaw) w.bytes(8, a);
    putOpt(signatureDigest, [&](auto &v) { w.string(9, v); });
    putOpt(lastModified, [&](auto v) { w.int64(10, v); });
    putUnknown(w, unknown);
    return w.take();
}

SetDataItem SetDataItem::decode(const Bytes &b) {
    SetDataItem d;
    for (Field &f : pb::parse(b)) {
        switch (f.number) {
        case 1: requireType(f, WireType::LengthDelimited, "SetDataItem"); d.packageName = f.asString(); break;
        case 2: requireType(f, WireType::LengthDelimited, "SetDataItem"); d.uri = f.asString(); break;
        case 4: requireType(f, WireType::LengthDelimited, "SetDataItem"); d.data = f.bytes; break;
        case 5: requireType(f, WireType::Varint, "SetDataItem"); d.seqId = f.asInt64(); break;
        case 6: requireType(f, WireType::Varint, "SetDataItem"); d.deleted = f.asBool(); break;
        case 7: requireType(f, WireType::LengthDelimited, "SetDataItem"); d.source = f.asString(); break;
        case 8: requireType(f, WireType::LengthDelimited, "SetDataItem"); d.assetsRaw.push_back(f.bytes); break;
        case 9: requireType(f, WireType::LengthDelimited, "SetDataItem"); d.signatureDigest = f.asString(); break;
        case 10: requireType(f, WireType::Varint, "SetDataItem"); d.lastModified = f.asInt64(); break;
        default: d.unknown.push_back(std::move(f));
        }
    }
    return d;
}

// ---- Request ---------------------------------------------------------------

Bytes Request::encode() const {
    Writer w;
    putOpt(requestId, [&](auto v) { w.int32(1, v); });
    putOpt(packageName, [&](auto &v) { w.string(2, v); });
    putOpt(signatureDigest, [&](auto &v) { w.string(3, v); });
    putOpt(targetNodeId, [&](auto &v) { w.string(4, v); });
    putOpt(unknown5, [&](auto v) { w.int32(5, v); });
    putOpt(path, [&](auto &v) { w.string(6, v); });
    putOpt(rawData, [&](auto &v) { w.bytes(7, v); });
    putOpt(sourceNodeId, [&](auto &v) { w.string(8, v); });
    putOpt(channelRaw, [&](auto &v) { w.bytes(9, v); });
    putOpt(generation, [&](auto v) { w.int32(10, v); });
    putUnknown(w, unknown);
    return w.take();
}

Request Request::decode(const Bytes &b) {
    Request r;
    for (Field &f : pb::parse(b)) {
        switch (f.number) {
        case 1: requireType(f, WireType::Varint, "Request"); r.requestId = f.asInt32(); break;
        case 2: requireType(f, WireType::LengthDelimited, "Request"); r.packageName = f.asString(); break;
        case 3: requireType(f, WireType::LengthDelimited, "Request"); r.signatureDigest = f.asString(); break;
        case 4: requireType(f, WireType::LengthDelimited, "Request"); r.targetNodeId = f.asString(); break;
        case 5: requireType(f, WireType::Varint, "Request"); r.unknown5 = f.asInt32(); break;
        case 6: requireType(f, WireType::LengthDelimited, "Request"); r.path = f.asString(); break;
        case 7: requireType(f, WireType::LengthDelimited, "Request"); r.rawData = f.bytes; break;
        case 8: requireType(f, WireType::LengthDelimited, "Request"); r.sourceNodeId = f.asString(); break;
        case 9: requireType(f, WireType::LengthDelimited, "Request"); r.channelRaw = f.bytes; break;
        case 10: requireType(f, WireType::Varint, "Request"); r.generation = f.asInt32(); break;
        default: r.unknown.push_back(std::move(f));
        }
    }
    return r;
}

// ---- RootMessage -----------------------------------------------------------

RootMessage::Kind RootMessage::kind() const {
    if (connect) return Kind::Connect;
    if (syncStart) return Kind::SyncStart;
    if (setDataItem) return Kind::SetDataItem;
    if (rpcRequest) return Kind::RpcRequest;
    if (channelRequest) return Kind::ChannelRequest;
    if (heartbeat) return Kind::Heartbeat;
    for (const auto &f : assetRaw) {
        switch (f.number) {
        case kSetAsset: return Kind::SetAsset;
        case kAckAsset: return Kind::AckAsset;
        case kFetchAsset: return Kind::FetchAsset;
        case kFilePiece: return Kind::FilePiece;
        }
    }
    if (!unknown.empty()) return Kind::Unknown;
    return Kind::Empty;
}

const char *kindName(RootMessage::Kind k) {
    switch (k) {
    case RootMessage::Kind::Empty: return "Empty";
    case RootMessage::Kind::SetAsset: return "SetAsset";
    case RootMessage::Kind::AckAsset: return "AckAsset";
    case RootMessage::Kind::FetchAsset: return "FetchAsset";
    case RootMessage::Kind::Connect: return "Connect";
    case RootMessage::Kind::SyncStart: return "SyncStart";
    case RootMessage::Kind::SetDataItem: return "SetDataItem";
    case RootMessage::Kind::RpcRequest: return "RpcRequest";
    case RootMessage::Kind::Heartbeat: return "Heartbeat";
    case RootMessage::Kind::FilePiece: return "FilePiece";
    case RootMessage::Kind::ChannelRequest: return "ChannelRequest";
    case RootMessage::Kind::Unknown: return "Unknown";
    }
    return "?";
}

Bytes RootMessage::encode() const {
    Writer w;
    for (const auto &f : assetRaw) w.bytes(f.number, f.bytes);
    if (connect) w.message(kConnect, connect->encode());
    if (syncStart) w.message(kSyncStart, syncStart->encode());
    if (setDataItem) w.message(kSetDataItem, setDataItem->encode());
    if (rpcRequest) w.message(kRpcRequest, rpcRequest->encode());
    if (heartbeat) w.message(kHeartbeat, Bytes{});
    if (hasAsset) w.boolean(kHasAsset, *hasAsset);
    if (channelRequest) w.message(kChannelRequest, channelRequest->encode());
    putUnknown(w, unknown);
    return w.take();
}

RootMessage RootMessage::decode(const Bytes &b) {
    RootMessage m;
    for (Field &f : pb::parse(b)) {
        switch (f.number) {
        case kConnect: requireType(f, WireType::LengthDelimited, "RootMessage"); m.connect = Connect::decode(f.bytes); break;
        case kSyncStart: requireType(f, WireType::LengthDelimited, "RootMessage"); m.syncStart = SyncStart::decode(f.bytes); break;
        case kSetDataItem: requireType(f, WireType::LengthDelimited, "RootMessage"); m.setDataItem = SetDataItem::decode(f.bytes); break;
        case kRpcRequest: requireType(f, WireType::LengthDelimited, "RootMessage"); m.rpcRequest = Request::decode(f.bytes); break;
        case kChannelRequest: requireType(f, WireType::LengthDelimited, "RootMessage"); m.channelRequest = Request::decode(f.bytes); break;
        case kHeartbeat: m.heartbeat = true; break;
        case kHasAsset: requireType(f, WireType::Varint, "RootMessage"); m.hasAsset = f.asBool(); break;
        case kSetAsset:
        case kAckAsset:
        case kFetchAsset:
        case kFilePiece:
            m.assetRaw.push_back(std::move(f));
            break;
        default: m.unknown.push_back(std::move(f));
        }
    }
    return m;
}

RootMessage RootMessage::makeConnect(Connect c) { RootMessage m; m.connect = std::move(c); return m; }
RootMessage RootMessage::makeSyncStart(SyncStart s) { RootMessage m; m.syncStart = std::move(s); return m; }
RootMessage RootMessage::makeHeartbeat() { RootMessage m; m.heartbeat = true; return m; }
RootMessage RootMessage::makeRpc(Request r) { RootMessage m; m.rpcRequest = std::move(r); return m; }

// ---- MessagePiece ----------------------------------------------------------

Bytes MessagePiece::encode() const {
    Writer w;
    w.bytes(1, data);
    w.string(2, digest);
    w.int32(3, thisPiece);
    w.int32(4, totalPieces);
    putOpt(queueId, [&](auto v) { w.int32(5, v); });
    putUnknown(w, unknown);
    return w.take();
}

MessagePiece MessagePiece::decode(const Bytes &b) {
    MessagePiece p;
    p.thisPiece = 0;
    p.totalPieces = 0;
    for (Field &f : pb::parse(b)) {
        switch (f.number) {
        case 1: requireType(f, WireType::LengthDelimited, "MessagePiece"); p.data = std::move(f.bytes); break;
        case 2: requireType(f, WireType::LengthDelimited, "MessagePiece"); p.digest = f.asString(); break;
        case 3: requireType(f, WireType::Varint, "MessagePiece"); p.thisPiece = f.asInt32(); break;
        case 4: requireType(f, WireType::Varint, "MessagePiece"); p.totalPieces = f.asInt32(); break;
        case 5: requireType(f, WireType::Varint, "MessagePiece"); p.queueId = f.asInt32(); break;
        default: p.unknown.push_back(std::move(f));
        }
    }
    return p;
}

} // namespace wearout::msg
