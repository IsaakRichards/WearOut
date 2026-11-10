# WearOut

Connect a modern **Wear OS** smartwatch to a **Sailfish OS** phone — no
Android, no Google companion app.

WearOut reimplements the phone side of the Wear OS "Wearable" protocol (the
Bluetooth link Google Play services uses for the Data Layer API) as a native
Sailfish daemon.

> **Status: early reverse-engineering stage.** The protocol core is real and
> tested; the Sailfish daemon and UI are scaffolded but have not yet run on
> a device. No watch has been paired yet. See [the roadmap](#roadmap).

## Target devices

WearOut is being built first for one watch and one phone:

| | Device | Notes |
|---|---|---|
| ⌚ | **OnePlus Watch 4** | Wear OS 6 + an RTOS co-processor; set up by OHealth. [Dossier](docs/devices/oneplus-watch-4.md) |
| 📱 | **Commodore Callback 8020** | Sailfish OS keypad flip phone, 480×640 screen. [Dossier](docs/devices/commodore-callback-8020.md) |

Other Wear OS watches use the same Google transport and should work as a
side effect, without the OnePlus-specific extras.

## Why this is hard (and why it might work)

Sailfish's Android App Support cannot open Bluetooth connections to
external devices, so running the official companion app there is a dead
end. Instead WearOut speaks the watch's protocol directly. The link format
— length-prefixed protobuf over RFCOMM — was documented by microG years ago,
and recent microG work (2025–26) shows the same protocol still carries
pairing, notifications, media and Data Layer traffic on Wear OS 5–6.

The unsolved part is the **first-time setup flow** the companion app runs
on a fresh watch. Details in [docs/PROTOCOL.md](docs/PROTOCOL.md).

## Layout

```
core/            portable C++17 protocol core (no Qt, no libprotobuf)
  protobuf       tiny wire-format codec that keeps unknown fields
  messages       RootMessage / Connect / SyncStart / SetDataItem / Request / MessagePiece
  framing        length-prefixed pieces, multi-piece reassembly, SHA-1 digest checks
  session        phone-side state machine: handshake, sync, heartbeat, messages
tools/apk_identity.py  signing-cert digest of an APK (who the watch trusts)
tools/apk_wear_scan.py scan an APK for its Wear OS interop surface (paths, services, protos)
tools/capture_wizard.py GUI wizard: record watch<->phone traffic and decode it
tools/cli/       wearout-cli: talk to the Wear OS emulator over TCP, MITM proxy, fake watch
tests/           unit + loopback tests for the core
sailfish/        wearoutd (Qt5, BlueZ Profile1 RFCOMM) + Silica UI + systemd user unit
rpm/             Sailfish SDK spec
proto/           reference .proto schema (from microG, Apache-2.0)
docs/            protocol notes, capturing guide, docs/devices/ per-device dossiers
```

## Building the core and CLI (any Linux)

```sh
cmake -S . -B build
cmake --build build
./build/wearout_tests
```

### Talking to a Wear OS emulator

```sh
# start a Wear OS AVD in Android Studio, then:
adb -s emulator-5554 forward tcp:5601 tcp:5601
./build/wearout-cli connect 127.0.0.1:5601
```

Type `msg /some/path hello`, `put /some/path value`, `app <package> <digest>`,
`items` or `quit`. Unknown traffic is dumped as decoded protobuf so new
fields can be identified. Add `-v` to dump everything.

### Replaying a real capture

Record watchâphone Bluetooth traffic on an Android phone and replay one
direction through WearOut's own parser:

```sh
./build/wearout-cli -v decode watch_to_phone.bin
```

Or run the guided GUI:

```sh
python3 tools/capture_wizard.py
```

See [docs/capturing.md](docs/capturing.md) for how to capture and export it by hand.

### Capturing a real phone ↔ watch session

`wearout-cli proxy 5602 127.0.0.1:5601` sits between an Android phone
running the companion app and the emulator watch, forwards bytes untouched
and prints every decoded message in both directions. This is how the
handshake defaults get verified.

### Self-test without an emulator

```sh
./build/wearout-cli fake-watch 15601 &
./build/wearout-cli connect 127.0.0.1:15601
```

## Building for Sailfish OS

Inside the Sailfish SDK (Platform SDK or `sfdk`):

```sh
sfdk build      # uses rpm/harbour-wearout.spec
```

Install the RPM, then `systemctl --user enable --now wearoutd`. Pair the
watch in *Settings › Bluetooth*, open WearOut, enter its address and pick
*Connect watch* from the pull-down menu.

Because the daemon needs the system bus (BlueZ), WearOut cannot be
published in the Jolla Harbour store; it targets Chum / OpenRepos, like
Amazfish.

## Roadmap

- [x] Protocol core: framing, pieces, digest, Connect/SyncStart/DataItem/Message
- [x] CLI: emulator client, MITM proxy, fake watch
- [x] Sailfish daemon + UI scaffold, BlueZ RFCOMM via `ProfileManager1`
- [x] Device profiles (OnePlus Watch 4), paired-watch discovery, keypad-first UI for the Callback
- [x] `tools/apk_identity.py`: extract OHealth's signing digest
- [ ] **Run the CLI against a real Wear OS emulator; record and fix the handshake**
- [x] OnePlus Watch 4: scanned OHealth — it tunnels over the Google Data Layer on `/heytap/*` paths with `com.op.proto` messages; no separate BLE link to reverse (see dossier)
- [ ] Model the `com.op.proto` handshake messages and relay the `/heytap/*` paths
- [ ] Capture a full companion setup with the proxy; document every message
- [ ] First build + run of `wearoutd` on a device
- [ ] Notifications: mirror Sailfish (lipstick) notifications to the watch
- [ ] Media controls (MPRIS ↔ watch)
- [ ] Time / battery / find-my-phone
- [ ] Native first-time setup of a fresh watch
- [ ] ChannelClient, assets, file transfer

## Credits

- [microG](https://microg.org) — the original `wearable` protobuf schema
  and framing (Apache-2.0), reproduced in `proto/wearable.proto`.
- teccheck's *wearos-research* notes — Bluetooth UUIDs and setup logs.
- [Amazfish](https://github.com/piggz/harbour-amazfish) — the model for a
  native Sailfish watch daemon.

## License

Apache-2.0. See [LICENSE](LICENSE) and [NOTICE](NOTICE).
