# Commodore Callback 8020

WearOut's primary target phone.

## What is known (pre-release, from launch coverage)

| | |
|---|---|
| OS | Jolla's Sailfish OS (version not announced) |
| SoC | MediaTek Helio G81 |
| Memory | 4 GB RAM, 64 GB + microSD |
| Main screen | 3.25" IPS, 480 × 640 (touch not confirmed) |
| Outer screen | 1.77" VFD-style |
| Input | T9 keypad, Fn keys, Commodore key |
| Radios | Dual-SIM 4G, Wi-Fi, Bluetooth (version not listed), GPS, hotspot |
| Battery | Removable (1550 mAh per one report, 1850 per another) |
| Android apps | Compatibility layer; browsers and social media blocked at system level |
| Shipping | "This winter" — not yet in hand |

## What this means for WearOut

**Good**
- It is a real Sailfish OS device: BlueZ 5 on the system bus, systemd user
  services, Qt 5 + Silica. That is exactly what `wearoutd` targets.
- Android App Support can't reach external Bluetooth devices anyway, so
  the native-daemon approach was the only option regardless.

**Risks to check the day it arrives**
1. **Can you install third-party native RPMs?** The phone blocks browsers
   and social apps "at the system level". If that is done with an app
   allow-list or a locked-down store, sideloading WearOut may be blocked.
   Check: *Settings › Untrusted software* and *Developer mode*. This is the
   single biggest go/no-go question.
2. **Developer mode / root.** Needed for `devel-su`, `journalctl`, and
   to install the RPM. Same check as above.
3. **Keypad UI.** Silica's pull-down menus assume touch. The WearOut UI is
   laid out for 480 × 640 and every action has a number-key shortcut
   (`1` connect, `2` stop, `3` rescan, `0` emulator). Needs testing on the
   real keypad: the D-pad may not map to Qt arrow keys.
4. **Bluetooth stack.** Confirm BlueZ accepts external
   `ProfileManager1.RegisterProfile` calls on this device (some Sailfish
   ports restrict it). Check with:
   `busctl --system call org.bluez /org/bluez org.bluez.ProfileManager1 RegisterProfile ...`
   or simply run `wearoutd` and read its log.
5. **Build target.** The Helio G81 is 64-bit ARM; build with the
   `SailfishOS-<ver>-aarch64` target once the shipped OS version is known.

## Day-one checklist

```sh
# on the Callback, in a terminal (Developer mode on)
ssh-keygen ...                       # or use the on-device Terminal app
devel-su pkcon install-local harbour-wearout-*.aarch64.rpm
systemctl --user enable --now wearoutd
journalctl --user -u wearoutd -f     # watch the protocol log
bluetoothctl                         # `devices` → note the watch's exact name
```

Report back: Sailfish version (`cat /etc/sailfish-release`), whether the
RPM installed, and the first 50 lines of the `wearoutd` log.
