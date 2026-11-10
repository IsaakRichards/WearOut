// SPDX-License-Identifier: Apache-2.0
// Dependency-free unit tests for the WearOut protocol core.
#include "wearout/devices.h"
#include "wearout/framing.h"
#include "wearout/messages.h"
#include "wearout/protobuf.h"
#include "wearout/session.h"
#include "wearout/sha1.h"

#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

using namespace wearout;
using pb::Bytes;

static int g_failed = 0, g_passed = 0;
#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (cond) {                                                                   \
            ++g_passed;                                                               \
        } else {                                                                      \
            ++g_failed;                                                               \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
        }                                                                             \
    } while (0)

static Bytes str(const std::string &s) { return Bytes(s.begin(), s.end()); }

static void testSha1() {
    // FIPS 180-1 vectors.
    auto h = sha1(reinterpret_cast<const uint8_t *>("abc"), 3);
    const uint8_t expect[20] = {0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a, 0xba, 0x3e,
                                0x25, 0x71, 0x78, 0x50, 0xc2, 0x6c, 0x9c, 0xd0, 0xd8, 0x9d};
    CHECK(std::memcmp(h.data(), expect, 20) == 0);
    std::string longIn = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    auto h2 = sha1(reinterpret_cast<const uint8_t *>(longIn.data()), longIn.size());
    const uint8_t expect2[20] = {0x84, 0x98, 0x3e, 0x44, 0x1c, 0x3b, 0xd2, 0x6e, 0xba, 0xae,
                                 0x4a, 0xa1, 0xf9, 0x51, 0x29, 0xe5, 0xe5, 0x46, 0x70, 0xf1};
    CHECK(std::memcmp(h2.data(), expect2, 20) == 0);
    // Empty input digest, url-safe, unpadded: base64 of da39a3ee... is 2jmj7l5rSw0yVb_vlWAYkK_YBwk
    CHECK(wearDigest(Bytes{}) == "2jmj7l5rSw0yVb_vlWAYkK_YBwk");
}

static void testVarints() {
    pb::Writer w;
    w.int32(1, -1);   // negative int32 -> 10 byte varint
    w.int64(2, 300);
    w.string(3, "hi");
    auto fields = pb::parse(w.data());
    CHECK(fields.size() == 3);
    CHECK(fields[0].asInt32() == -1);
    CHECK(fields[1].asInt64() == 300);
    CHECK(fields[2].asString() == "hi");
    // 300 encodes as ac 02
    pb::Writer w2;
    w2.int64(1, 300);
    CHECK(w2.data() == (Bytes{0x08, 0xac, 0x02}));

    bool threw = false;
    try {
        pb::parse(Bytes{0x0a, 0x05, 'a'}); // length past end
    } catch (const pb::DecodeError &) {
        threw = true;
    }
    CHECK(threw);
}

static void testConnectRoundTrip() {
    msg::Connect c;
    c.id = "abcd1234";
    c.name = "Sailfish";
    c.peerAndroidId = -42;
    c.unknown4 = 3;
    c.peerVersion = 2;
    c.peerMinimumVersion = 0;
    c.networkId = "abcd1234";
    // Unknown field must survive.
    pb::Field extra;
    extra.number = 99;
    extra.type = pb::WireType::Varint;
    extra.value = 7;
    c.unknown.push_back(extra);

    auto root = msg::RootMessage::makeConnect(c);
    auto back = msg::RootMessage::decode(root.encode());
    CHECK(back.kind() == msg::RootMessage::Kind::Connect);
    CHECK(back.connect->id == c.id);
    CHECK(back.connect->peerAndroidId == -42);
    CHECK(back.connect->peerVersion == 2);
    CHECK(back.connect->unknown.size() == 1 && back.connect->unknown[0].number == 99);
    CHECK(back.encode() == root.encode());
}

static void testHeartbeatAndUnknownRoot() {
    auto hb = msg::RootMessage::makeHeartbeat();
    auto enc = hb.encode();
    CHECK(enc == (Bytes{0x5a, 0x00})); // field 11, LEN, 0
    CHECK(msg::RootMessage::decode(enc).kind() == msg::RootMessage::Kind::Heartbeat);

    pb::Writer w;
    w.string(42, "future");
    auto m = msg::RootMessage::decode(w.data());
    CHECK(m.kind() == msg::RootMessage::Kind::Unknown);
    CHECK(m.encode() == w.data());
}

static void testSinglePieceFraming() {
    auto m = msg::RootMessage::makeHeartbeat();
    Bytes wire = encodeMessage(m, 1);
    uint32_t len = (wire[0] << 24) | (wire[1] << 16) | (wire[2] << 8) | wire[3];
    CHECK(len == wire.size() - 4);

    // Feed one byte at a time.
    StreamDecoder dec;
    int got = 0;
    for (uint8_t b : wire) {
        dec.feed(&b, 1);
        while (auto d = dec.next()) {
            CHECK(d->message.kind() == msg::RootMessage::Kind::Heartbeat);
            ++got;
        }
    }
    CHECK(got == 1);
    CHECK(dec.buffered() == 0);
}

static void testMultiPieceFraming() {
    msg::Request r;
    r.path = "/big";
    r.rawData = Bytes(200'000, 0x5a);
    auto m = msg::RootMessage::makeRpc(r);

    FramingLimits lim;
    lim.pieceDataBytes = 1000;
    Bytes wire = encodeMessage(m, 7, lim);

    StreamDecoder dec(lim);
    dec.feed(wire);
    auto d = dec.next();
    CHECK(d.has_value());
    CHECK(d && d->message.rpcRequest && d->message.rpcRequest->rawData->size() == 200'000);
    CHECK(!dec.next().has_value());

    // Corrupt one payload byte deep in the stream -> digest mismatch.
    Bytes bad = wire;
    bad[bad.size() / 2] ^= 0xff;
    StreamDecoder dec2(lim);
    dec2.feed(bad);
    bool threw = false;
    try {
        while (dec2.next()) {
        }
    } catch (const FramingError &) {
        threw = true;
    }
    CHECK(threw);
}

static void testOversizeFrameRejected() {
    StreamDecoder dec;
    Bytes huge{0x7f, 0xff, 0xff, 0xff};
    dec.feed(huge);
    bool threw = false;
    try {
        dec.next();
    } catch (const FramingError &) {
        threw = true;
    }
    CHECK(threw);
}

// A fake watch that answers our Connect with its own Connect, then sends a
// message and a data item. Exercises the full Session path in-process.
static void testSessionLoopback() {
    std::vector<Bytes> toWatch;
    std::vector<SessionEvent> events;

    SessionConfig cfg;
    cfg.localNodeId = "phone001";
    cfg.heartbeatIntervalMs = 1000;
    cfg.peerTimeoutMs = 5000;
    Session s(cfg, {[&](const Bytes &b) { toWatch.push_back(b); },
                    [&](const SessionEvent &e) { events.push_back(e); },
                    nullptr});

    s.start(0);
    CHECK(s.state() == SessionState::Handshaking);
    CHECK(toWatch.size() == 1);

    // Watch decodes our Connect.
    StreamDecoder watchSide;
    watchSide.feed(toWatch[0]);
    auto ours = watchSide.next();
    CHECK(ours && ours->message.connect && ours->message.connect->id == std::string("phone001"));

    // Watch replies.
    msg::Connect wc;
    wc.id = "watch42";
    wc.name = "Pixel Watch";
    wc.peerVersion = 2;
    wc.peerMinimumVersion = 1;
    Bytes reply = encodeMessage(msg::RootMessage::makeConnect(wc), 1);

    msg::Request rpc;
    rpc.path = "/hello";
    rpc.packageName = "com.example.wear";
    rpc.rawData = str("ping");
    rpc.sourceNodeId = "watch42";
    Bytes rpcWire = encodeMessage(msg::RootMessage::makeRpc(rpc), 2);
    reply.insert(reply.end(), rpcWire.begin(), rpcWire.end());

    msg::RootMessage di;
    di.setDataItem = msg::SetDataItem{};
    di.setDataItem->uri = "wear://watch42/battery";
    di.setDataItem->data = str("87");
    di.setDataItem->seqId = 5;
    Bytes diWire = encodeMessage(di, 3);
    reply.insert(reply.end(), diWire.begin(), diWire.end());

    s.onBytes(reply.data(), reply.size(), 10);
    CHECK(s.state() == SessionState::Connected);
    CHECK(s.peer().nodeId == "watch42");
    CHECK(s.peer().name == "Pixel Watch");
    CHECK(s.dataItems().count("wear://watch42/battery") == 1);

    bool sawMsg = false, sawItem = false, sawPeer = false;
    for (auto &e : events) {
        if (e.type == SessionEvent::Type::PeerConnected) sawPeer = true;
        if (e.type == SessionEvent::Type::MessageReceived && e.path == "/hello" && e.data == str("ping"))
            sawMsg = true;
        if (e.type == SessionEvent::Type::DataItemChanged && e.data == str("87")) sawItem = true;
    }
    CHECK(sawPeer && sawMsg && sawItem);

    // We should have sent SyncStart after their Connect.
    CHECK(toWatch.size() == 2);
    watchSide.feed(toWatch[1]);
    auto sync = watchSide.next();
    CHECK(sync && sync->message.syncStart && sync->message.syncStart->receivedSeqId == -1);

    // Outgoing message is addressed to the watch.
    s.sendMessage("/notify", str("x"), "org.wearout.bridge", "AAAA");
    watchSide.feed(toWatch.back());
    auto out = watchSide.next();
    CHECK(out && out->message.rpcRequest && out->message.rpcRequest->targetNodeId == std::string("watch42"));

    // Heartbeat after interval, timeout after silence.
    size_t before = toWatch.size();
    s.tick(10 + 1500);
    CHECK(toWatch.size() == before + 1);
    s.tick(10 + 6000);
    CHECK(s.state() == SessionState::Closed);
}

static void testQueuedBeforeHandshake() {
    std::vector<Bytes> toWatch;
    Session s({}, {[&](const Bytes &b) { toWatch.push_back(b); }, nullptr, nullptr});
    s.start(0);
    s.sendMessage("/early", str("x"), "pkg", "dig");
    CHECK(toWatch.size() == 1); // only our Connect so far

    msg::Connect wc;
    wc.id = "watchQ";
    Bytes reply = encodeMessage(msg::RootMessage::makeConnect(wc), 1);
    s.onBytes(reply.data(), reply.size(), 1);
    CHECK(toWatch.size() == 3); // + SyncStart + flushed message

    StreamDecoder dec;
    dec.feed(toWatch[2]);
    auto m = dec.next();
    CHECK(m && m->message.rpcRequest && m->message.rpcRequest->path == std::string("/early"));
    CHECK(m && m->message.rpcRequest->targetNodeId == std::string("watchQ"));
}

static void testVersionRejection() {
    std::string err;
    Session s({}, {[](const Bytes &) {}, [&](const SessionEvent &e) {
                       if (e.type == SessionEvent::Type::Error) err = e.error;
                   }, nullptr});
    s.start(0);
    msg::Connect wc;
    wc.id = "w";
    wc.peerMinimumVersion = 99;
    Bytes reply = encodeMessage(msg::RootMessage::makeConnect(wc), 1);
    s.onBytes(reply.data(), reply.size(), 1);
    CHECK(s.state() == SessionState::Closed);
    CHECK(err.find("99") != std::string::npos);
}

static void testDeviceMatching() {
    const WatchProfile *p = matchWatch("OnePlus Watch 4 A1B2");
    CHECK(p && p->id == "oneplus-watch-4");
    CHECK(p && p->dualEngine);
    CHECK(p && p->companion.packageName == "com.heytap.health.international");
    CHECK(p && p->companion.signatureDigest.size() == 40);
    p = matchWatch("oneplus watch 4");
    CHECK(p && p->id == "oneplus-watch-4");
    p = matchWatch("Pixel Watch 3 1234");
    CHECK(p && p->id == "wear-os-generic");
    CHECK(matchWatch("OnePlus Buds Pro 3") == nullptr);
    CHECK(matchWatch("") == nullptr);
}

int main() {
    testDeviceMatching();
    testSha1();
    testVarints();
    testConnectRoundTrip();
    testHeartbeatAndUnknownRoot();
    testSinglePieceFraming();
    testMultiPieceFraming();
    testOversizeFrameRejected();
    testSessionLoopback();
    testQueuedBeforeHandshake();
    testVersionRejection();
    std::printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed ? 1 : 0;
}
