# OnePlus Watch 4

WearOut's primary target watch.

## What is confirmed

| | |
|---|---|
| Announced | April 2026 |
| Software | Wear OS 6, skinned as OxygenOS Watch 8 |
| Chips | Qualcomm Snapdragon W5 (runs Wear OS) + BES2800 (runs a low-power RTOS) |
| Memory | 2 GB RAM, 32 GB storage |
| Battery | 632 mAh; ~16 days power-saver, ~5 days "smart mode", ~3 days heavy use |
| Setup app | **OHealth** (`com.heytap.health.international`) — per OnePlus' Watch 3 FAQ, which also requires Android 9+ and Google Play services ≥ 23.45.23 on the phone. Assumed identical for Watch 4. |

## How OHealth actually works (from scanning the APK)

The APK scan (`python3 tools/apk_wear_scan.py OHealth.apks`, full output in
[ohealth-scan.md](ohealth-scan.md)) settled the architecture, and it is
friendlier than feared:

```
                 ┌──────── OHealth (phone) ────────┐
                 │ Google Wear Companion Library    │  setup, pairing,
                 │  (com.google.android.libraries   │  account copy, OTA
                 │   .wear.companion.*)             │
                 │ OPPO link layer                  │  all OnePlus features
                 │  (com.oplus.wearable.linkservice)│  as com.op.proto msgs
                 └───────────────┬──────────────────┘
                                 │ ONE Wear OS Data Layer link
                                 │ (WearableBt RFCOMM — what WearOut speaks)
                 ┌───────────────┴──────────────────┐
                 │            OnePlus Watch 4         │
                 └───────────────────────────────────┘
```

Three findings that shape WearOut:

1. **There is one link, not two.** OnePlus's own service,
   `com.oplus.wearable.linkservice.transport.gms.GMSListenerService`,
   is a *standard Wear OS `WearableListenerService`*. It does not open a
   second Bluetooth socket — it tunnels OnePlus's protocol **over the Google
   Data Layer**, on these path prefixes (from the manifest):

   | Prefix | Direction |
   |---|---|
   | `/heytap/watch/message`, `/heytap/watch/data`, `/heytap/watch/ctrl` | phone → watch |
   | `/heytap/phone/message`, `/heytap/phone/data`, `/heytap/phone/ctrl` | watch → phone |

   So **WearOut's existing transport already carries OnePlus traffic.** The
   BES2800 co-processor still exists for battery life, but OHealth reaches it
   through the Data Layer, not a separate app-visible link. These paths and a
   path classifier are in `core/.../oppo.h`.

2. **OnePlus's payloads are `com.op.proto` protobufs.** 25 message types were
   recovered by name (listed in `oppo.cpp` and the scan report): the handshake
   set (`DeviceInfo`, `ConnectResult`, `CapabilityHand`, `CapabilityPass`,
   `StartSetting`, `SyncOOBEState`, `MigrateStatus`), settings (`TimeFormat`,
   `NightModeData`, `RaiseWristData`, `StepGoal`, `CalorieGoal`), health
   (`HeartRateMeasure`, `HeartRateWarn`, `OximetryState`, `ECGMeasureType`),
   and actions (`FindDeviceProto`, `FindPhoneProto`, `LaunchWatchApp`). Field
   layouts still need a capture.

3. **Setup is Google's, wrapped by OHealth.** OHealth embeds the Google Wear
   Companion Library, whose setup engine steps are all present by name
   (`WatchPairingSetupStep`, `TermsOfServiceSetupStep`,
   `GoogleAccountsSetupStep`, `FactoryResetProtectionSetupStep`,
   `GmsCheckInSetupStep`, `PhoneSwitchingSetupStep`, `RestoreSetupStep`, …).
   This is the **same companion flow microG is working to support** (GmsCore
   PRs #3837/#3849), so that work applies directly here.

**What this means:** a WearOut that speaks the Google Data Layer well enough
to (a) complete the companion setup and (b) relay `com.op.proto` messages on
the six `/heytap/*` paths can, in principle, drive the whole watch — no
second radio, no separate BLE stack to reverse first.

## Plan of attack

Ordered so each step is useful even if the next one stalls.

### Phase A — get the watch set up once (needs an Android phone, one time)
WearOut cannot do first-time setup yet. Borrow/keep an Android phone, set
the watch up with OHealth, and leave it set up. Everything below assumes
this.

### Phase B — learn OHealth's identity and paths (no watch needed)
- [x] Download the OHealth APK and run
      `python3 tools/apk_identity.py OHealth.apks` → package + cert digest.
      OHealth uses **signing-key rotation** (APK Signature Scheme v3.1), so
      it has two certificates:

      | Key | Where seen | SHA-1 hex |
      |---|---|---|
      | New (rotated) — default | Google Play bundle, v3.1 block | `030ac13604c0782011aa95a18bda33dc93a94f7f` |
      | Original | Play bundle v3 + v2 blocks (and earlier APK) | `b58d9a4ce294abe1ab8e577d20233122b1a49199` |

      Android 13+ reports the rotated key as the app's signer, and phones that
      pair a Watch 4 run Android 13+, so WearOut tries it first.
- [x] Re-run the updated tool on the Play bundle: its v3 and v2 blocks
      hold the original key `b58d9a4c…` and its v3.1 block the new key
      `030ac136…`. Confirmed rotation of one app's key, no third certificate.
- [ ] Confirm which key the watch checks, and in which encoding (the first
      real `Request` captured from OHealth will show it).
- [x] Get the Play Store build of `com.heytap.health.international`
      (`.apks` bundle) and check it.
- [x] Scan the APK for the Wear OS surface with
      `python3 tools/apk_wear_scan.py OHealth.apks` → listener services, the
      six `/heytap/*` tunnel paths, `com.op.proto` message names, and the
      companion setup steps. Captured in [ohealth-scan.md](ohealth-scan.md).
- [ ] Decompile `base.apk` with jadx and read the `com.op.proto` message
      classes to recover each message's **field numbers and types**, then
      model the handshake set (`DeviceInfo`, `ConnectResult`,
      `CapabilityHand`/`CapabilityPass`, `StartSetting`, `SyncOOBEState`) in
      `core/`. Payloads ride the `/heytap/watch/*` and `/heytap/phone/*` paths.
- [ ] Confirmed there is no separate BLE service to reverse first — OnePlus
      rides the Data Layer. Revisit only if a capture shows otherwise.

### Phase C — sniff a real session (needs Android phone + watch)
- [ ] Enable *Developer options › Enable Bluetooth HCI snoop log* on the
      Android phone, pair/sync with OHealth, pull the log with
      `adb bugreport`, open in Wireshark.
- [ ] Load `wear_dissector.lua` from wearos-research for the RFCOMM framing,
      then read the reassembled `RootMessage`s: the `rpcRequest`/`setDataItem`
      on `/heytap/watch/*` carry the `com.op.proto` payloads to match against
      Phase B's field layouts.
- [ ] Capture one full OHealth setup to see the companion step order and
      which signing-digest encoding the watch checks.

### Phase D — impersonate the phone (needs the Callback or any Sailfish/Linux box)
- [ ] Unpair the Android phone *without* resetting the watch (the watch
      only allows one phone; this may force a reset — test it).
- [ ] Pair the watch from Sailfish Bluetooth settings, start `wearoutd`,
      watch the log for the watch's `Connect`.
- [ ] Send notifications using OHealth's identity + paths from Phase B.

## Data Layer paths

OnePlus tunnel (payloads are `com.op.proto` messages):

| Direction | Path | Payload | Meaning |
|---|---|---|---|
| phone → watch | `/heytap/watch/message` | com.op.proto | MessageClient RPCs |
| phone → watch | `/heytap/watch/data` | com.op.proto | DataItem sync |
| phone → watch | `/heytap/watch/ctrl` | com.op.proto | control/handshake |
| watch → phone | `/heytap/phone/message` | com.op.proto | MessageClient RPCs |
| watch → phone | `/heytap/phone/data` | com.op.proto | DataItem sync |
| watch → phone | `/heytap/phone/ctrl` | com.op.proto | control/handshake |

Standard Google companion paths also seen (handled by the Wear Companion
Library, not OnePlus-specific): `/call/rpc/*` (telecom bridging),
`/companion_unpaired/{rpc,ack}` (unpair), `/contacts3/node_migration_done`
and `/data/MigrateActivity` (phone switching). Full list in
[ohealth-scan.md](ohealth-scan.md).

## Open questions

- Exact Bluetooth name the watch advertises (WearOut assumes it starts with
  "OnePlus Watch 4").
- Field layout of each `com.op.proto` message (needs jadx or a capture).
- Does the watch accept a new phone without a factory reset?
- Is the Google account copy (`GoogleAccountsSetupStep`) mandatory, or can it
  be skipped for a watch that only needs notifications/health?
- Which signing-digest encoding the watch enforces on `Request`.
