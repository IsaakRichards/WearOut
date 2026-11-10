// SPDX-License-Identifier: Apache-2.0
#include "wearout/framing.h"

#include "wearout/sha1.h"

#include <algorithm>

namespace wearout {

pb::Bytes frame(const msg::MessagePiece &piece) {
    pb::Bytes body = piece.encode();
    pb::Bytes out;
    out.reserve(body.size() + 4);
    uint32_t n = static_cast<uint32_t>(body.size());
    out.push_back(static_cast<uint8_t>(n >> 24));
    out.push_back(static_cast<uint8_t>(n >> 16));
    out.push_back(static_cast<uint8_t>(n >> 8));
    out.push_back(static_cast<uint8_t>(n));
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

pb::Bytes encodeMessage(const msg::RootMessage &m, int32_t queueId, const FramingLimits &lim) {
    pb::Bytes body = m.encode();
    if (body.size() > lim.maxMessageBytes)
        throw FramingError("message too large to send");
    const std::string digest = wearDigest(body);
    const size_t chunk = std::max<size_t>(1, lim.pieceDataBytes);
    const int32_t total = body.empty() ? 1 : static_cast<int32_t>((body.size() + chunk - 1) / chunk);

    pb::Bytes out;
    for (int32_t i = 0; i < total; ++i) {
        msg::MessagePiece p;
        size_t from = static_cast<size_t>(i) * chunk;
        size_t to = std::min(body.size(), from + chunk);
        p.data.assign(body.begin() + from, body.begin() + to);
        p.digest = digest;
        p.thisPiece = i + 1;
        p.totalPieces = total;
        if (total > 1)
            p.queueId = queueId;
        pb::Bytes f = frame(p);
        out.insert(out.end(), f.begin(), f.end());
    }
    return out;
}

void StreamDecoder::feed(const uint8_t *data, size_t len) {
    m_buf.insert(m_buf.end(), data, data + len);
}

std::optional<msg::MessagePiece> StreamDecoder::nextPiece() {
    if (m_buf.size() < 4)
        return std::nullopt;
    uint32_t n = (uint32_t(m_buf[0]) << 24) | (uint32_t(m_buf[1]) << 16) | (uint32_t(m_buf[2]) << 8) |
                 uint32_t(m_buf[3]);
    if (n > m_lim.maxFrameBytes)
        throw FramingError("frame length " + std::to_string(n) + " exceeds limit");
    if (m_buf.size() < 4 + static_cast<size_t>(n))
        return std::nullopt;
    pb::Bytes body(m_buf.begin() + 4, m_buf.begin() + 4 + n);
    m_buf.erase(m_buf.begin(), m_buf.begin() + 4 + n);
    try {
        return msg::MessagePiece::decode(body);
    } catch (const pb::DecodeError &e) {
        throw FramingError(std::string("bad MessagePiece: ") + e.what());
    }
}

std::optional<StreamDecoder::Decoded> StreamDecoder::accept(msg::MessagePiece p) {
    auto finish = [&](pb::Bytes bytes, const std::string &digest) -> Decoded {
        if (!digest.empty() && wearDigest(bytes) != digest)
            throw FramingError("digest mismatch on reassembled message");
        try {
            return Decoded{msg::RootMessage::decode(bytes), std::move(bytes)};
        } catch (const pb::DecodeError &e) {
            throw FramingError(std::string("bad RootMessage: ") + e.what());
        }
    };

    if (p.totalPieces <= 1)
        return finish(std::move(p.data), p.digest);

    if (p.thisPiece < 1 || p.thisPiece > p.totalPieces)
        throw FramingError("piece index out of range");

    const int32_t q = p.queueId.value_or(0);
    if (p.thisPiece == 1) {
        // A new message on a queue abandons whatever was there (matches the
        // reference implementation's behaviour, but we don't kill the link).
        if (m_queues.find(q) == m_queues.end() && m_queues.size() >= m_lim.maxOpenQueues)
            throw FramingError("too many concurrent partial messages");
        Queue nq;
        nq.digest = p.digest;
        nq.total = p.totalPieces;
        nq.received = 1;
        nq.data = std::move(p.data);
        m_queues[q] = std::move(nq);
        return std::nullopt;
    }

    auto it = m_queues.find(q);
    if (it == m_queues.end() || it->second.digest != p.digest)
        throw FramingError("piece " + std::to_string(p.thisPiece) + " without a first piece");
    Queue &qu = it->second;
    if (p.thisPiece != qu.received + 1 || p.totalPieces != qu.total)
        throw FramingError("out-of-order piece");
    if (qu.data.size() + p.data.size() > m_lim.maxMessageBytes)
        throw FramingError("reassembled message exceeds limit");
    qu.data.insert(qu.data.end(), p.data.begin(), p.data.end());
    qu.received++;
    if (qu.received < qu.total)
        return std::nullopt;

    pb::Bytes bytes = std::move(qu.data);
    std::string digest = qu.digest;
    m_queues.erase(it);
    return finish(std::move(bytes), digest);
}

std::optional<StreamDecoder::Decoded> StreamDecoder::next() {
    while (auto piece = nextPiece()) {
        if (auto done = accept(std::move(*piece)))
            return done;
    }
    return std::nullopt;
}

} // namespace wearout
