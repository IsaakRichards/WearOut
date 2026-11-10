# The Wearable protocol, as far as we know

This is a living document. Everything marked **unverified** needs a packet
capture from a real watch before we trust it. If you capture something that
contradicts this page, that is a win: open an issue with the log.

## Big picture

On Android, a Wear OS watch and a phone talk through Google Play services
("GMS") on both ends. Apps never open sockets themselves; they call the
Wearable Data Layer API (`MessageClient`, `DataClient`, `ChannelClient`,
`CapabilityClient`) and GMS multiplexes everything over one link.

For WearOut, Sailfish OS has to play the *phone-side GMS* role. The watch
runs stock Wear OS and does not know or care that the phone isn't Android,
as long as we speak its language.

```
 Watch app ─ Data Layer API ─ GMS (watch) ══ Bluetooth RFCOMM ══ wearoutd (Sailfish)
                                              "WearableBt"
```

## Transports

| Name | UUID | Who is server | Purpose |
|---|---|---|---|
| WearableBt | `5e8945b0-9525-11e3-a5e2-0800200c9a66` | watch | Main Data Layer link (protobuf) |
| Flow | `fafbdd20-83f0-4389-addf-917ac9dae5b2` | phone | Opened by the companion app; purpose unknown |
| Flow15 | `6a1eafb1-61c0-42a0-8bb0-a336fb1c3f00` | phone | Opened by the companion app; purpose unknown |
| BLE service | `C9C92447-8F77-4C63-9C79-25E71B4CA9B3`, char `ABDD3056-28FA-441D-A470-55A75A52553A` | ? | Used by GMS; purpose unknown |
| TCP 5601 | — | watch (emulator) | Same byte stream as WearableBt, used by the Android Studio emulator pairing flow |

UUIDs come from the teccheck/wearos-research notes (Pixel Watch 1). WearOut
currently implements only WearableBt (via BlueZ) and TCP 5601 (CLI/emulator).

## Framing

The stream is a sequence of frames:

```
uint32 big-endian length | MessagePiece (protobuf, `length` bytes)
```

`MessagePiece`:

| # | Field | Notes |
|---|---|---|
| 1 | data | A slice of an encoded `RootMessage` |
| 2 | digest | URL-safe base64 (no padding) of SHA-1 over the **whole** RootMessage |
| 3 | thisPiece | 1-based |
| 4 | totalPieces | |
| 5 | queueId | Groups pieces of one message; several messages may interleave |

Small messages are a single piece. The receiver reassembles per queue and
checks the digest. WearOut caps frames at 20 MiB, messages at 16 MiB, and
open queues at 8 (mirrors limits used in recent microG work).

## RootMessage

Exactly one of these is normally set:

| # | Field | Data Layer meaning | WearOut status |
|---|---|---|---|
| 4 | setAsset | Asset upload | kept raw |
| 5 | ackAsset | | kept raw |
| 6 | fetchAsset | | kept raw |
| 7 | connect | Handshake | ✅ decoded |
| 8 | syncStart | DataItem sync negotiation | ✅ decoded |
| 9 | setDataItem | `DataClient` put/delete | ✅ decoded |
| 10 | rpcRequest | `MessageClient` message | ✅ decoded |
| 11 | heartbeat | Keep-alive (empty) | ✅ |
| 12 | filePiece | File transfer | kept raw |
| 13 | hasAsset | | ✅ |
| 16 | channelRequest | `ChannelClient` | outer `Request` decoded, channel body raw |

Anything else lands in `unknown` and is printed with a generic protobuf
dump, so modern additions show up in logs.

## Handshake (unverified on modern Wear OS)

1. Transport comes up.
2. Each side sends `RootMessage{connect}`:
   `id` = node id, `name`, `peerAndroidId`, `unknown4`, `peerVersion`,
   `peerMinimumVersion`, `networkId`.
3. After receiving the peer's `connect`, send `syncStart` with
   `receivedSeqId` (last DataItem seq seen from the peer, `-1` if none), a
   `syncTable` of node→seq, and `version`.
4. Heartbeats keep the link alive.

WearOut's defaults (`unknown4=3`, `peerVersion=2`, `syncVersion=2`) come
from microG's 2015-era implementation. Wear OS 5/6 may expect higher
versions; the daemon exposes `protocolVersion` and `connectUnknown4`
settings, and the CLI honours `WEAROUT_PROTO_VERSION` / `WEAROUT_CONNECT_U4`,
so they can be swept experimentally.

## App identity

Data Layer traffic is addressed to an app by **package name + signing
certificate digest**. A watch app only receives messages that claim to come
from its phone-side twin (same package, same signature). To talk to an
existing watch app, WearOut must send that app's package name and the
digest of its *phone* app's signing certificate. That is public information
(it's in the APK), but it means every watch app we want to support needs a
small adapter that knows its paths and payload format.

## The hard part: pairing and setup (open problem)

The data link above is the *easy* part. A factory-fresh Wear OS watch
refuses to do anything useful until the phone-side companion app (Pixel
Watch / Galaxy Wearable / Wear OS by Google) has run its setup flow:
Bluetooth bonding, accepting terms, "watch ready" events, and on current
versions copying a Google account to the watch (UKEY2/D2D-encrypted,
CryptAuth-signed). The research notes show the companion looping at
"Waiting for watch ready resumable event", i.e. this flow is not fully
understood yet.

Realistic paths, in order of difficulty:

1. **Emulator first.** Pair a Wear OS emulator image with WearOut over TCP
   5601 and grow the protocol support there.
2. **Already-set-up watch.** Set the watch up once with an Android phone,
   then teach WearOut to impersonate that phone's node (same BT address is
   not possible, so this may still trigger a re-pair; needs testing).
3. **Native setup.** Reimplement enough of the companion setup flow that a
   fresh watch accepts Sailfish directly, ideally skipping the Google
   account step where Wear OS allows it.

## Prior art and references

- microG `wearable` library (Apache-2.0): original protobuf schema and
  framing — the basis of `proto/wearable.proto`.
- teccheck/wearos-research: Bluetooth UUIDs, setup-flow logs, Wireshark
  dissector for this framing.
- microG GmsCore issue #2843 and PRs #3702, #3837, #3849 (2025–2026):
  ongoing work to make the official companion app work against microG.
  #3849 reports pairing, terms, notifications, media and Data Layer working
  against emulator images (Wear OS 5.1–6.1), which is strong evidence this
  protocol is still the one in use.
- harbour-amazfish: the native Sailfish watch daemon whose architecture
  (system daemon + Silica UI over D-Bus) WearOut follows.
