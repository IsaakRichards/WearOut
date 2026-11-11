# Capturing real watch ↔ phone traffic

> **In a hurry? Use the wizard.** `python3 tools/capture_wizard.py` opens a
> click-through GUI that does everything on this page: checks your tools
> (adb, tshark, cmake, wearout-cli) and offers to install or build any that are
> missing,
> helps you enable the snoop log, records `adb logcat` while you reproduce,
> retrieves `btsnoop_hci.log` via `adb bugreport`, finds the WearableBt
> channel, exports + decodes each direction, collects the phone's device info,
> and zips everything into one `wearout-capture-<timestamp>.zip` ready to send.
> It saves the pieces into a `wearout-capture/` folder (`btsnoop_hci.log`,
> `logcat.txt`, the per-direction `*.bin`, `*.decoded.txt`, `device_info.txt`,
> `MANIFEST.md`) and bundles them on the final step, with an optional
> best-effort redaction of emails/long numbers in the text files. Enabling the
> snoop toggle is the only manual part. The steps below are the reference it
> follows.

The watch and the Android phone talk over Bluetooth **RFCOMM** (the
"WearableBt" channel), carrying the length-prefixed protobuf WearOut
implements. Recording it is how we turn the APK scan's *guesses* into
confirmed message layouts.

There are two complementary views:

* **The bytes** — a Bluetooth HCI snoop log on the phone. This is the real
  wire format: framing, `RootMessage`s, `com.op.proto` payloads.
* **The meaning** — logcat from the watch (and/or phone), which prints Data
  Layer paths, node ids and capability names in clear text. Much easier to
  read, and it tells you *what* a given byte blob was for.

Do both for one activity (e.g. "raise to wake, then a notification arrives")
and you can line the paths up against the bytes.

> ⚠️ A capture contains personal data: notification text, account ids,
> health readings. Keep captures out of the repo and scrub before sharing.

---

## A. The bytes: Bluetooth HCI snoop log (no root)

On the **Android phone** paired with the watch:

1. Settings → About phone → tap *Build number* seven times to unlock
   *Developer options*.
2. Developer options → enable **Bluetooth HCI snoop log**.
3. Toggle Bluetooth off and on (some phones need a reboot) so logging starts.
4. Do the thing you want to understand: open OHealth, force a sync, let a
   notification reach the watch, press "find my phone", etc. Keep it short —
   a 20-second window is easier to read than ten minutes.
5. Pull the log:
   ```sh
   adb bugreport bugreport.zip
   ```
   The snoop log is inside the zip, usually at
   `FS/data/misc/bluetooth/logs/btsnoop_hci.log` (path varies by vendor).
   Some phones also expose it directly at `/sdcard/btsnoop_hci.log`.

### Viewing it

Open `btsnoop_hci.log` in **Wireshark**. To dissect our framing, drop
`wear_dissector.lua` (from the teccheck/wearos-research repo) into Wireshark's
plugin folder — it understands the `MessagePiece` layout and shows
`RootMessage`s directly.

Find the WearableBt channel: filter `btrfcomm`, look at the connection to the
watch's Bluetooth address, and note the RFCOMM channel number.

### Feeding it to WearOut

`wearout-cli decode` replays one direction of the RFCOMM payload through the
exact parser the daemon uses, so you see it decoded the same way WearOut will.
Export each direction to a raw file, then decode.

Using `tshark` (run it **without** the Lua dissector so the payload stays as
raw `data.data`; `CH` = the RFCOMM channel, `WATCH` = the watch's BD_ADDR):

```sh
# watch -> phone
tshark -r btsnoop_hci.log \
  -Y "btrfcomm.channel==CH && bluetooth.src==WATCH" \
  -T fields -e data.data | tr -d '\n ' | xxd -r -p > watch_to_phone.bin

# phone -> watch
tshark -r btsnoop_hci.log \
  -Y "btrfcomm.channel==CH && bluetooth.dst==WATCH" \
  -T fields -e data.data | tr -d '\n ' | xxd -r -p > phone_to_watch.bin

./build/wearout-cli -v decode watch_to_phone.bin
./build/wearout-cli -v decode phone_to_watch.bin
```

`decode` reassembles the pieces, checks the SHA-1 digests and prints each
message; unknown protobuf is dumped field by field. It resyncs if the capture
starts mid-stream, so a partial log is still useful. (Field names like
`data.data` and `bluetooth.src` can vary slightly between Wireshark versions;
if a filter returns nothing, click a WearableBt data frame and read the field
names from the detail pane.)

Save the `.bin` files and the `decode` output and send me the output — that is
what lets me model the `com.op.proto` handshake messages.

---

## B. The meaning: logcat from the watch

Wear OS watches run adb. This view shows paths and node ids in clear text.

1. On the **watch**: Settings → System → About → tap *Build number* 7×, then
   Developer options → enable **ADB debugging** and **Debug over Wi-Fi** (or
   **Debug over Bluetooth**).
2. Connect from the computer:
   ```sh
   adb connect <watch-ip>:<port>        # Wi-Fi; the watch shows the address
   adb devices                          # confirm it's listed
   ```
3. Watch the Data Layer:
   ```sh
   adb logcat -v time | grep -iE "WearableLS|DataLayer|MessageClient|DataClient|/heytap/|com.op.proto|CapabilityClient"
   ```
4. Trigger the same activity as in part A. You'll see the paths
   (`/heytap/watch/...`), node ids and capability names scroll by.

The phone side can be logged the same way with `adb logcat` on the phone,
which also shows OHealth's own tags (`oplus.wearable`, `heytap`).

---

## C. Live development without a capture

- **Emulator:** pair a Wear OS AVD, `adb forward tcp:5601 tcp:5601`, then
  `wearout-cli connect` — the same bytes flow over TCP (see the README).
- **Man in the middle:** `wearout-cli proxy` sits between phone and emulator
  and logs both directions live, forwarding untouched.

---

## Capturing the pairing (you get one shot per factory reset)

The pairing/first-sync handshake is the most valuable capture and the hardest
to repeat — it happens once per factory reset. Prepare so you don't waste it.

**Prepare the phone *before* resetting the watch:**

1. Install OHealth on the phone and sign in, so setup isn't waiting on a download.
2. Enable Developer options → **Bluetooth HCI snoop log**, then toggle
   Bluetooth off/on (or reboot) so logging is actually running.
3. Plug in USB, confirm `adb devices` shows the phone, and start the semantic
   log in a terminal (the fresh watch has no adb yet, so log the phone):
   ```sh
   adb logcat -v time > pairing_logcat.txt &
   ```
4. Keep the watch on its charger right next to the phone — a strong link
   avoids retransmits and keeps traffic on Bluetooth.
5. To stop anything routing over the cloud, turn the phone's **Wi-Fi off** for
   the core pairing (mobile data still tethers the watch over Bluetooth).
   Re-enable Wi-Fi only once setup needs the internet (account copy, OTA).

**Beware the buffer.** The snoop log is a capped ring buffer. A full Wear OS
setup — Google **account copy**, a **day-zero OTA**, app installs — floods it
and can evict the early `Connect` / `SyncStart` / `DeviceInfo` frames you most
want. To protect the handshake:

- If OnePlus lets you **skip or defer the Google account** during setup, do so;
  that removes the largest flood and isolates the OnePlus `com.op.proto`
  handshake. Add the account in a later session if you need that flow.
- **Pull the log as soon as the watch reports "connected / setting up"**, before
  the OTA. You can continue setup afterward and pull again.

**Then:** note the wall-clock time you tap *Pair*, complete the core pairing,
and immediately run the wizard (or `adb bugreport`) to grab `btsnoop_hci.log`.
Decode the `/heytap/watch/ctrl` and `/heytap/phone/ctrl` directions first —
that is where `DeviceInfo` / `ConnectResult` / `CapabilityHand` ride. Save the
raw `.bin` files and `pairing_logcat.txt` (scrubbed of personal data).

---

## What to capture first (most useful, in order)

1. **Pairing / first sync** — the `connect` + `SyncStart` handshake and the
   `com.op.proto` `DeviceInfo` / `ConnectResult` / `CapabilityHand` exchange.
   This is the spine everything else hangs off.
2. **A notification arriving on the watch** — the smallest useful feature to
   reproduce from Sailfish.
3. **"Find my phone"** — tiny, self-contained (`FindPhoneProto`), a good first
   message to send *from* WearOut.
4. **A settings change** (e.g. raise-to-wake on/off) — maps to a single
   `com.op.proto` message, easy to diff.
