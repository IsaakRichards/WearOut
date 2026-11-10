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

## Why this watch is harder than a Pixel Watch

It is really **two computers sharing one Bluetooth radio story**:

```
              ┌──────────── OnePlus Watch 4 ────────────┐
 Sailfish ◄──►│ Snapdragon W5: Wear OS 6 + GMS          │  "WearableBt" RFCOMM (Google)
   phone  ◄──►│ BES2800: RTOS (notifications, health    │  OPPO/HeyTap link (proprietary,
              │  sensors, watch faces in power-saver)   │   probably BLE) — UNKNOWN
              └─────────────────────────────────────────┘
```

1. **The Google side** (W5 / Wear OS) speaks the Wearable protocol WearOut
   already implements. This carries Data Layer traffic between OHealth on
   the phone and OHealth's watch-side app.
2. **The OPPO side** (BES2800) is how OnePlus gets 5–16 day battery life:
   in smart/power-saver mode Wear OS sleeps and the RTOS handles
   notifications and sensors. OHealth almost certainly talks to it over a
   separate proprietary link. **Nothing public documents this yet.**
3. **Setup is done by OHealth, not Google's Wear OS app.** OHealth wraps
   Google's setup flow (Google account copy to the watch) inside its own
   OPPO account + pairing flow.

A WearOut that only speaks the Google side would work while the watch is in
full Wear OS mode and go quiet when it drops to RTOS mode.

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
- [ ] Decompile with jadx; grep for `MessageClient`, `DataClient`,
      `sendMessage(`, `PutDataRequest.create(` and `/` path literals.
      Write each path and payload format into the table below.
- [ ] Find the BLE side: grep for `BluetoothGatt`, `UUID.fromString`, and
      the BES / "heytap wearable" SDK classes. Record service and
      characteristic UUIDs.

### Phase C — sniff a real session (needs Android phone + watch)
- [ ] Enable *Developer options › Enable Bluetooth HCI snoop log* on the
      Android phone, pair/sync with OHealth, pull the log with
      `adb bugreport`, open in Wireshark.
- [ ] Load `wear_dissector.lua` from wearos-research for the RFCOMM side.
- [ ] Note which traffic is RFCOMM (Google) vs BLE GATT (OPPO), and what
      happens when the watch switches between smart mode and full mode.

### Phase D — impersonate the phone (needs the Callback or any Sailfish/Linux box)
- [ ] Unpair the Android phone *without* resetting the watch (the watch
      only allows one phone; this may force a reset — test it).
- [ ] Pair the watch from Sailfish Bluetooth settings, start `wearoutd`,
      watch the log for the watch's `Connect`.
- [ ] Send notifications using OHealth's identity + paths from Phase B.

## Data Layer paths (fill in from Phase B/C)

| Direction | Path | Payload | Meaning |
|---|---|---|---|
| | | | |

## BLE services (fill in from Phase B/C)

| Service UUID | Characteristic | Notes |
|---|---|---|
| | | |

## Open questions

- Exact Bluetooth name the watch advertises (WearOut assumes it starts with
  "OnePlus Watch 4").
- Does the watch accept a new phone without a factory reset?
- Is the Google account copy mandatory, or can OxygenOS Watch skip it?
- Does the RTOS side need to be "bound" to the phone with an OPPO account
  token before it will deliver notifications?
