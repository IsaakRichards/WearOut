// SPDX-License-Identifier: Apache-2.0
//
// Byte-stream framing for the Wearable transport:
//
//   [uint32 big-endian length][MessagePiece protobuf] [length][piece] ...
//
// A RootMessage larger than one piece is split; every piece carries the
// digest of the *whole* RootMessage and pieces of one message share a
// queueId. The receiver reassembles per queue and verifies the digest.
#pragma once

#include "wearout/messages.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <stdexcept>
#include <vector>

namespace wearout {

struct FramingError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct FramingLimits {
    uint32_t maxFrameBytes = 20u * 1024u * 1024u;   // what one length prefix may claim
    size_t maxMessageBytes = 16u * 1024u * 1024u;   // reassembled RootMessage
    size_t maxOpenQueues = 8;                       // concurrent partial messages
    size_t pieceDataBytes = 64u * 1024u;            // our outgoing piece size
};

// Prefix one encoded MessagePiece with its length.
pb::Bytes frame(const msg::MessagePiece &piece);

// Encode a RootMessage into one or more framed pieces, ready to write to
// the socket back-to-back.
pb::Bytes encodeMessage(const msg::RootMessage &m, int32_t queueId, const FramingLimits &lim = {});

// Incremental decoder: feed arbitrary socket chunks, pull out complete
// RootMessages. Throws FramingError on protocol violations; the caller
// should drop the connection when that happens.
class StreamDecoder {
public:
    explicit StreamDecoder(FramingLimits lim = {}) : m_lim(lim) {}

    void feed(const uint8_t *data, size_t len);
    void feed(const pb::Bytes &b) { feed(b.data(), b.size()); }

    // Returns the next complete message, if any. Raw bytes are returned as
    // well so callers can log/dump exactly what arrived.
    struct Decoded {
        msg::RootMessage message;
        pb::Bytes raw;
    };
    std::optional<Decoded> next();

    size_t buffered() const { return m_buf.size(); }

private:
    std::optional<msg::MessagePiece> nextPiece();
    std::optional<Decoded> accept(msg::MessagePiece piece);

    FramingLimits m_lim;
    pb::Bytes m_buf;
    struct Queue {
        std::string digest;
        int32_t total = 0;
        int32_t received = 0;
        pb::Bytes data;
    };
    std::map<int32_t, Queue> m_queues;
};

} // namespace wearout
