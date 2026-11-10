// SPDX-License-Identifier: Apache-2.0
//
// Hand-written bindings for the Wear OS "Wearable" transport messages.
// Field numbers follow proto/wearable.proto (derived from microG's
// Apache-2.0 schema). Every message keeps the fields it does not understand
// in `unknown`, so new/changed fields in modern Wear OS show up in logs
// instead of silently vanishing.
#pragma once

#include "wearout/protobuf.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace wearout::msg {

using pb::Bytes;

struct Connect {
    std::optional<std::string> id;           // 1  node id of the sender
    std::optional<std::string> name;         // 2  human readable device name
    std::optional<int64_t> peerAndroidId;    // 3
    std::optional<int32_t> unknown4;         // 4  (role/type? see PROTOCOL.md)
    std::optional<int32_t> peerVersion;      // 5  protocol version spoken
    std::optional<int32_t> peerMinimumVersion; // 6
    std::optional<std::string> networkId;    // 7
    std::vector<pb::Field> unknown;

    Bytes encode() const;
    static Connect decode(const Bytes &b);
};

struct SyncTableEntry {
    std::string key;   // 1  node id
    int64_t value = 0; // 2  last seq id seen from that node
};

struct SyncStart {
    std::optional<int64_t> receivedSeqId;  // 1
    std::vector<SyncTableEntry> syncTable; // 2
    std::optional<int32_t> version;        // 3
    std::vector<pb::Field> unknown;

    Bytes encode() const;
    static SyncStart decode(const Bytes &b);
};

struct SetDataItem {
    std::optional<std::string> packageName;     // 1
    std::optional<std::string> uri;             // 2  wear://<node>/<path>
    std::optional<Bytes> data;                  // 4
    std::optional<int64_t> seqId;               // 5
    std::optional<bool> deleted;                // 6
    std::optional<std::string> source;          // 7  source node id
    std::vector<Bytes> assetsRaw;               // 8  AssetEntry (not modelled yet)
    std::optional<std::string> signatureDigest; // 9
    std::optional<int64_t> lastModified;        // 10
    std::vector<pb::Field> unknown;

    Bytes encode() const;
    static SetDataItem decode(const Bytes &b);
};

// Used for both RootMessage.rpcRequest (MessageClient messages) and
// RootMessage.channelRequest (ChannelClient traffic, in `channelRaw`).
struct Request {
    std::optional<int32_t> requestId;           // 1
    std::optional<std::string> packageName;     // 2  target app package
    std::optional<std::string> signatureDigest; // 3  target app signing cert digest
    std::optional<std::string> targetNodeId;    // 4
    std::optional<int32_t> unknown5;            // 5
    std::optional<std::string> path;            // 6  e.g. "/my/message/path"
    std::optional<Bytes> rawData;               // 7  message payload
    std::optional<std::string> sourceNodeId;    // 8
    std::optional<Bytes> channelRaw;            // 9  ChannelRequest (not modelled yet)
    std::optional<int32_t> generation;          // 10
    std::vector<pb::Field> unknown;

    Bytes encode() const;
    static Request decode(const Bytes &b);
};

struct RootMessage {
    enum class Kind {
        Empty,
        SetAsset,
        AckAsset,
        FetchAsset,
        Connect,
        SyncStart,
        SetDataItem,
        RpcRequest,
        Heartbeat,
        FilePiece,
        ChannelRequest,
        Unknown,
    };

    // Field numbers inside RootMessage.
    static constexpr uint32_t kSetAsset = 4;
    static constexpr uint32_t kAckAsset = 5;
    static constexpr uint32_t kFetchAsset = 6;
    static constexpr uint32_t kConnect = 7;
    static constexpr uint32_t kSyncStart = 8;
    static constexpr uint32_t kSetDataItem = 9;
    static constexpr uint32_t kRpcRequest = 10;
    static constexpr uint32_t kHeartbeat = 11;
    static constexpr uint32_t kFilePiece = 12;
    static constexpr uint32_t kHasAsset = 13;
    static constexpr uint32_t kChannelRequest = 16;

    std::optional<Connect> connect;
    std::optional<SyncStart> syncStart;
    std::optional<SetDataItem> setDataItem;
    std::optional<Request> rpcRequest;
    std::optional<Request> channelRequest;
    bool heartbeat = false;
    std::optional<bool> hasAsset;
    // Asset traffic is kept raw for now (field number -> bytes).
    std::vector<pb::Field> assetRaw;
    std::vector<pb::Field> unknown;

    Kind kind() const;
    Bytes encode() const;
    static RootMessage decode(const Bytes &b);

    static RootMessage makeConnect(Connect c);
    static RootMessage makeSyncStart(SyncStart s);
    static RootMessage makeHeartbeat();
    static RootMessage makeRpc(Request r);
};

const char *kindName(RootMessage::Kind k);

// Transport-level envelope: every RootMessage travels inside one or more
// MessagePieces, each prefixed on the wire by a 32-bit big-endian length.
struct MessagePiece {
    Bytes data;                  // 1
    std::string digest;          // 2  base64url(sha1(whole message))
    int32_t thisPiece = 1;       // 3  1-based
    int32_t totalPieces = 1;     // 4
    std::optional<int32_t> queueId; // 5
    std::vector<pb::Field> unknown;

    Bytes encode() const;
    static MessagePiece decode(const Bytes &b);
};

} // namespace wearout::msg
