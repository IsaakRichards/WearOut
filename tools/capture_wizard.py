#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""
WearOut capture wizard — a click-through GUI that walks you through recording
real Wear OS watch <-> phone Bluetooth traffic and decoding it with WearOut.

It drives the process described in docs/capturing.md:
  1. check tools (adb, tshark, wearout-cli)
  2. enable the phone's Bluetooth HCI snoop log + confirm the phone is attached
  3. let you reproduce the watch activity
  4. retrieve btsnoop_hci.log (adb pull, or extract from `adb bugreport`)
  5. detect the WearableBt RFCOMM channel and the watch's address
  6. export each direction and run `wearout-cli decode`, showing the result

Needs only Python's standard library (Tkinter). Run on the machine that has
adb/tshark; the watch's companion phone must be reachable over adb.

    python3 tools/capture_wizard.py

The non-GUI helpers below are pure and unit-tested (tests/test_capture_wizard.py).
"""
import os
import re
import shutil
import zipfile


# --------------------------------------------------------------------------
# Pure helpers (no Tkinter, unit-tested)
# --------------------------------------------------------------------------

REQUIRED_TOOLS = ("adb", "tshark")


def which(tool: str):
    """Absolute path to `tool`, or None. Honours WEAROUT_<TOOL> override."""
    env = os.environ.get("WEAROUT_" + tool.upper().replace("-", "_"))
    if env and os.path.exists(env):
        return env
    return shutil.which(tool)


def find_wearout_cli(start: str = None):
    """Locate the wearout-cli binary near the repo, or on PATH."""
    cand = []
    if start:
        cand.append(start)
    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.dirname(here)
    cand += [
        os.path.join(repo, "build", "wearout-cli"),
        os.path.join(repo, "build", "tools", "cli", "wearout-cli"),
    ]
    for c in cand:
        if c and os.path.isfile(c) and os.access(c, os.X_OK):
            return c
    return which("wearout-cli")


BTSNOOP_NAMES = ("btsnoop_hci.log", "btsnoop_hci.log.last")


def find_btsnoop_in_zip(zip_path: str):
    """Return the archive member path of a btsnoop log inside a bugreport zip,
    or None. Prefers the primary log over the '.last' rotation."""
    try:
        with zipfile.ZipFile(zip_path) as z:
            names = z.namelist()
    except (zipfile.BadZipFile, OSError):
        return None
    hits = [n for n in names if os.path.basename(n) in BTSNOOP_NAMES]
    if not hits:
        hits = [n for n in names if "btsnoop" in os.path.basename(n).lower()]
    if not hits:
        return None
    hits.sort(key=lambda n: (os.path.basename(n).endswith(".last"), len(n)))
    return hits[0]


def extract_btsnoop(zip_path: str, out_dir: str):
    """Extract the btsnoop log from a bugreport zip to out_dir; return its path."""
    member = find_btsnoop_in_zip(zip_path)
    if not member:
        return None
    os.makedirs(out_dir, exist_ok=True)
    dest = os.path.join(out_dir, "btsnoop_hci.log")
    with zipfile.ZipFile(zip_path) as z, open(dest, "wb") as f:
        f.write(z.read(member))
    return dest


def channel_scan_cmd(tshark: str, btsnoop: str):
    """tshark command that lists one RFCOMM frame per line as
    src<TAB>dst<TAB>channel, for channel/direction detection."""
    return [tshark, "-r", btsnoop, "-Y", "btrfcomm",
            "-T", "fields", "-e", "bluetooth.src", "-e", "bluetooth.dst",
            "-e", "btrfcomm.channel"]


def parse_rfcomm_endpoints(tshark_output: str):
    """Parse channel_scan_cmd output into ranked (channel, src, dst, frames).

    Busiest first — the WearableBt data channel carries the most frames. Rows
    missing a channel or address are ignored.
    """
    counts = {}
    for line in tshark_output.splitlines():
        parts = line.rstrip("\n").split("\t")
        if len(parts) != 3:
            continue
        src, dst, ch = (p.strip() for p in parts)
        if not ch or not src or not dst:
            continue
        # A frame can list several channels if reassembled; take the first.
        ch = ch.split(",")[0].strip()
        if not ch.isdigit():
            continue
        key = (int(ch), src, dst)
        counts[key] = counts.get(key, 0) + 1
    ranked = [(ch, src, dst, n) for (ch, src, dst), n in counts.items()]
    ranked.sort(key=lambda r: r[3], reverse=True)
    return ranked


def export_cmd(tshark: str, btsnoop: str, channel: int, src=None, dst=None):
    """tshark command to dump one direction's RFCOMM payload as hex.

    Filter by channel and, if given, by source/destination BD_ADDR for a
    single direction. Run WITHOUT the Lua dissector so the payload stays raw.
    """
    flt = ["btrfcomm.channel==%d" % channel]
    if src:
        flt.append("bluetooth.src==%s" % src)
    if dst:
        flt.append("bluetooth.dst==%s" % dst)
    return [tshark, "-r", btsnoop, "-Y", " && ".join(flt),
            "-T", "fields", "-e", "data.data"]


_HEX = re.compile(r"[^0-9a-fA-F]")


def hexfields_to_bytes(text: str) -> bytes:
    """Concatenate tshark `-e data.data` output (hex, possibly colon- or
    comma-separated, one packet per line) into a byte string, in order."""
    h = _HEX.sub("", text)
    if len(h) % 2:
        h = h[:-1]  # drop a stray nibble rather than crash
    return bytes.fromhex(h)


def decode_cmd(cli: str, bin_path: str, verbose: bool = True):
    cmd = [cli]
    if verbose:
        cmd.append("-v")
    cmd += ["decode", bin_path]
    return cmd


def logcat_clear_cmd(adb: str):
    return [adb, "logcat", "-c"]


def logcat_cmd(adb: str):
    """Stream the phone's logcat with timestamps; the GUI redirects it to a
    file. Filtering is left to decode time so nothing relevant is dropped."""
    return [adb, "logcat", "-v", "time"]


OHEALTH_PKG = "com.heytap.health.international"


def device_info_cmds(adb: str, package: str = OHEALTH_PKG):
    """(label, cmd) pairs whose output describes the phone and the companion
    app, for correlating a capture with a device + software version."""
    return [
        ("phone properties", [adb, "shell", "getprop"]),
        ("companion app version",
         [adb, "shell", "dumpsys package %s | grep -iE 'versionName|versionCode|firstInstallTime|lastUpdateTime'" % package]),
        ("bonded bluetooth devices",
         [adb, "shell", "dumpsys bluetooth_manager | grep -iE 'Bonded|BondedDevices|name:|address' | head -40"]),
    ]


# Patterns for the optional best-effort redaction of text artifacts.
_EMAIL = re.compile(r"[\w.+-]+@[\w-]+\.[\w.-]+")
_LONGNUM = re.compile(r"\d{7,}")
REDACTABLE_EXT = (".txt", ".md", ".log.txt")


def redact_text(s: str) -> str:
    """Mask the obvious personal identifiers (emails, long digit runs) in a
    text artifact. Best effort only, and never applied to binary captures."""
    s = _EMAIL.sub("[email]", s)
    s = _LONGNUM.sub("[number]", s)
    return s


def is_text_artifact(name: str) -> bool:
    low = name.lower()
    if low.endswith(".bin") or low == "btsnoop_hci.log" or low.endswith(".zip"):
        return False
    return low.endswith((".txt", ".md")) or "decoded" in low or "logcat" in low


def files_to_package(workdir: str):
    """Artifacts to include in the send-zip: everything in workdir except the
    bulky bugreport archive and any previously built package."""
    out = []
    try:
        names = sorted(os.listdir(workdir))
    except OSError:
        return out
    for n in names:
        p = os.path.join(workdir, n)
        if not os.path.isfile(p):
            continue
        low = n.lower()
        if low.endswith(".zip"):          # bugreport.zip or a prior package
            continue
        out.append(n)
    return out


def package_zip_path(parent_dir: str, when=None):
    import datetime
    ts = (when or datetime.datetime.now()).strftime("%Y%m%d-%H%M%S")
    return os.path.join(parent_dir, "wearout-capture-%s.zip" % ts)


def build_manifest(files, meta: dict):
    """Markdown describing the package contents and environment."""
    lines = ["# WearOut capture package", ""]
    for k in ("generated", "tool", "phone", "companion"):
        if meta.get(k):
            lines.append("- **%s:** %s" % (k, meta[k]))
    lines += ["", "## Files", ""]
    for name, size in files:
        lines.append("- `%s` (%d bytes)" % (name, size))
    lines += [
        "",
        "## What each file is",
        "- `btsnoop_hci.log` — raw Bluetooth HCI capture from the phone.",
        "- `*.bin` — one direction of the WearableBt RFCOMM payload, reassembled.",
        "- `*.decoded.txt` — that payload decoded by `wearout-cli decode`.",
        "- `logcat.txt` — the phone's log during the capture (paths, node ids).",
        "- `device_info.txt` — phone properties and companion-app version.",
        "",
        "## Note",
        "This capture can contain personal data (notification text, account ids,",
        "health readings). The raw `btsnoop`/`.bin` files are NOT redacted.",
        "Review before sharing beyond a trusted recipient.",
    ]
    return "\n".join(lines) + "\n"


# --------------------------------------------------------------------------
# GUI (Tkinter imported lazily so the helpers import without a display)
# --------------------------------------------------------------------------

STEPS = [
    ("Tools", "Check that adb, tshark and wearout-cli are available."),
    ("Enable logging", "Turn on the phone's Bluetooth HCI snoop log."),
    ("Reproduce", "Do the watch activity you want to capture."),
    ("Retrieve log", "Pull btsnoop_hci.log from the phone."),
    ("Pick channel", "Find the WearableBt channel and the watch."),
    ("Decode", "Export each direction and decode it."),
    ("Package", "Collect device info and zip everything to send."),
]


def run_gui():  # pragma: no cover - needs a display
    import queue
    import subprocess
    import threading
    import tkinter as tk
    from tkinter import filedialog, messagebox, scrolledtext, ttk

    class Wizard(tk.Tk):
        def __init__(self):
            super().__init__()
            self.title("WearOut capture wizard")
            self.geometry("920x640")
            self.minsize(760, 520)

            self.workdir = os.path.abspath("wearout-capture")
            self.tshark = which("tshark")
            self.adb = which("adb")
            self.cli = find_wearout_cli()
            self.btsnoop = None
            self.endpoints = []          # ranked (ch, src, dst, n)
            self.msgq = queue.Queue()
            self.step = 0
            self.logcat_proc = None
            self.logcat_path = None
            self.saved = []              # files written into workdir

            self._build()
            self.protocol("WM_DELETE_WINDOW", self._on_close)
            self.after(100, self._drain_log)
            self._show_step()

        def _on_close(self):
            self._stop_logcat()
            self.destroy()

        def _note_saved(self, path):
            if path and path not in self.saved:
                self.saved.append(path)
                self._log("saved " + path)

        # ---- layout ----
        def _build(self):
            root = ttk.Frame(self, padding=12)
            root.pack(fill="both", expand=True)

            side = ttk.Frame(root)
            side.pack(side="left", fill="y", padx=(0, 12))
            ttk.Label(side, text="WearOut", font=("", 15, "bold")).pack(anchor="w")
            ttk.Label(side, text="capture wizard").pack(anchor="w", pady=(0, 12))
            self.step_labels = []
            for i, (name, _) in enumerate(STEPS):
                lab = ttk.Label(side, text=f"{i + 1}. {name}")
                lab.pack(anchor="w", pady=2)
                self.step_labels.append(lab)

            main = ttk.Frame(root)
            main.pack(side="left", fill="both", expand=True)
            self.header = ttk.Label(main, text="", font=("", 14, "bold"))
            self.header.pack(anchor="w")
            self.subhead = ttk.Label(main, text="", wraplength=560, foreground="#555")
            self.subhead.pack(anchor="w", pady=(0, 8))
            self.body = ttk.Frame(main)
            self.body.pack(fill="both", expand=True)

            self.log = scrolledtext.ScrolledText(main, height=10, font=("monospace", 9),
                                                 state="disabled", wrap="word")
            self.log.pack(fill="both", expand=False, pady=(8, 8))

            nav = ttk.Frame(main)
            nav.pack(fill="x")
            self.back_btn = ttk.Button(nav, text="← Back", command=self._back)
            self.back_btn.pack(side="left")
            self.next_btn = ttk.Button(nav, text="Next →", command=self._next)
            self.next_btn.pack(side="right")

        # ---- helpers ----
        def _log(self, line):
            self.msgq.put(line)

        def _drain_log(self):
            try:
                while True:
                    line = self.msgq.get_nowait()
                    self.log.configure(state="normal")
                    self.log.insert("end", line + "\n")
                    self.log.see("end")
                    self.log.configure(state="disabled")
            except queue.Empty:
                pass
            self.after(100, self._drain_log)

        def _run(self, cmd, on_done=None, save_stdout=None):
            """Run a command in a worker thread, stream output to the log."""
            self._log("$ " + " ".join(cmd))

            def work():
                buf = []
                try:
                    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                         text=True, bufsize=1)
                    for line in p.stdout:
                        buf.append(line)
                        self._log(line.rstrip("\n"))
                    p.wait()
                    rc = p.returncode
                except OSError as e:
                    self._log(f"! {e}")
                    rc = -1
                if save_stdout is not None:
                    try:
                        with open(save_stdout, "w") as f:
                            f.write("".join(buf))
                    except OSError as e:
                        self._log(f"! cannot write {save_stdout}: {e}")
                if on_done:
                    self.after(0, lambda: on_done(rc, "".join(buf)))

            threading.Thread(target=work, daemon=True).start()

        def _clear_body(self):
            for w in self.body.winfo_children():
                w.destroy()

        # ---- step router ----
        def _show_step(self):
            for i, lab in enumerate(self.step_labels):
                lab.configure(font=("", 10, "bold") if i == self.step else ("", 10))
            name, sub = STEPS[self.step]
            self.header.configure(text=f"Step {self.step + 1}: {name}")
            self.subhead.configure(text=sub)
            self.back_btn.configure(state="normal" if self.step else "disabled")
            self.next_btn.configure(state="normal")
            self._clear_body()
            [self._step_tools, self._step_enable, self._step_reproduce,
             self._step_retrieve, self._step_channel, self._step_decode,
             self._step_package][self.step]()

        def _back(self):
            if self.step:
                self.step -= 1
                self._show_step()

        def _next(self):
            if self.step < len(STEPS) - 1:
                self.step += 1
                self._show_step()

        # ---- step 1: tools ----
        def _step_tools(self):
            os.makedirs(self.workdir, exist_ok=True)
            grid = ttk.Frame(self.body)
            grid.pack(anchor="w", pady=4)
            rows = [("adb", self.adb), ("tshark", self.tshark), ("wearout-cli", self.cli)]
            for r, (name, path) in enumerate(rows):
                ok = bool(path)
                ttk.Label(grid, text=("✓" if ok else "✗") + " " + name,
                          foreground="#1a7f37" if ok else "#b00020",
                          width=16).grid(row=r, column=0, sticky="w")
                ttk.Label(grid, text=path or "not found on PATH").grid(row=r, column=1, sticky="w")
            ttk.Label(self.body, wraplength=560, foreground="#555", text=(
                "adb comes with Android platform-tools; tshark with Wireshark; "
                "wearout-cli is built from this repo (cmake --build build). "
                "Set WEAROUT_WEAROUT_CLI / WEAROUT_TSHARK to override a path."
            )).pack(anchor="w", pady=8)
            ttk.Button(self.body, text="Re-check", command=self._recheck_tools).pack(anchor="w")
            ttk.Label(self.body, text=f"Working folder: {self.workdir}",
                      foreground="#555").pack(anchor="w", pady=(8, 0))

        def _recheck_tools(self):
            self.tshark, self.adb, self.cli = which("tshark"), which("adb"), find_wearout_cli()
            self._show_step()

        # ---- step 2: enable logging ----
        def _step_enable(self):
            steps = (
                "On the phone paired with the watch:\n"
                "  1. Settings → About phone → tap Build number 7×\n"
                "  2. Developer options → enable 'Bluetooth HCI snoop log'\n"
                "  3. Toggle Bluetooth off and on (some phones need a reboot)\n"
                "  4. Plug the phone into USB and allow USB debugging"
            )
            ttk.Label(self.body, text=steps, justify="left").pack(anchor="w")
            ttk.Button(self.body, text="Check phone (adb devices)",
                       command=lambda: self._run([self.adb, "devices", "-l"])).pack(anchor="w", pady=8)

        # ---- step 3: reproduce ----
        def _step_reproduce(self):
            ttk.Label(self.body, justify="left", text=(
                "Now make the watch do the thing you want to understand, e.g.:\n"
                "  • open OHealth and let it sync\n"
                "  • send yourself a notification and watch it arrive\n"
                "  • press 'find my phone'\n\n"
                "The Bluetooth snoop log records on its own. Start the clear-text "
                "logcat here so you capture the paths alongside the bytes:"
            )).pack(anchor="w")
            b = ttk.Frame(self.body)
            b.pack(anchor="w", pady=8)
            self.logcat_start_btn = ttk.Button(b, text="● Start logcat", command=self._start_logcat)
            self.logcat_start_btn.grid(row=0, column=0, padx=(0, 6))
            self.logcat_stop_btn = ttk.Button(b, text="■ Stop logcat", command=self._stop_logcat,
                                              state="normal" if self.logcat_proc else "disabled")
            self.logcat_stop_btn.grid(row=0, column=1)
            self.logcat_status = ttk.Label(self.body, foreground="#555",
                                           text="logcat: running" if self.logcat_proc else "logcat: not running")
            self.logcat_status.pack(anchor="w")
            ttk.Label(self.body, foreground="#555", wraplength=560, text=(
                "Suggested order: Start logcat → do the watch activity → Stop "
                "logcat → go to the next step and retrieve the snoop log. For a "
                "fresh-watch pairing, see docs/capturing.md (one shot per reset)."
            )).pack(anchor="w", pady=8)

        def _start_logcat(self):
            if self.logcat_proc:
                return
            if not self.adb:
                self._log("! adb not found; cannot capture logcat")
                return
            os.makedirs(self.workdir, exist_ok=True)
            self.logcat_path = os.path.join(self.workdir, "logcat.txt")
            try:
                subprocess.run(logcat_clear_cmd(self.adb), timeout=15)
            except (OSError, subprocess.SubprocessError) as e:
                self._log(f"! logcat -c failed: {e}")
            try:
                f = open(self.logcat_path, "w")
                self.logcat_proc = subprocess.Popen(logcat_cmd(self.adb), stdout=f,
                                                    stderr=subprocess.STDOUT, text=True)
            except OSError as e:
                self._log(f"! cannot start logcat: {e}")
                self.logcat_proc = None
                return
            self._log("logcat recording to " + self.logcat_path)
            if hasattr(self, "logcat_status"):
                self.logcat_status.configure(text="logcat: running")
                self.logcat_stop_btn.configure(state="normal")
                self.logcat_start_btn.configure(state="disabled")

        def _stop_logcat(self):
            if not self.logcat_proc:
                return
            try:
                self.logcat_proc.terminate()
                self.logcat_proc.wait(timeout=5)
            except (OSError, subprocess.SubprocessError):
                pass
            self.logcat_proc = None
            self._note_saved(self.logcat_path)
            if hasattr(self, "logcat_status") and self.logcat_status.winfo_exists():
                self.logcat_status.configure(text="logcat: stopped")
                self.logcat_stop_btn.configure(state="disabled")
                self.logcat_start_btn.configure(state="normal")

        # ---- step 4: retrieve ----
        def _step_retrieve(self):
            ttk.Label(self.body, text="Fetch btsnoop_hci.log from the phone:").pack(anchor="w")
            b = ttk.Frame(self.body)
            b.pack(anchor="w", pady=6)
            ttk.Button(b, text="Via bugreport (no root)",
                       command=self._pull_bugreport).grid(row=0, column=0, padx=(0, 6))
            ttk.Button(b, text="Pull /sdcard/btsnoop_hci.log",
                       command=self._pull_sdcard).grid(row=0, column=1, padx=6)
            ttk.Button(b, text="Choose a file I already have…",
                       command=self._choose_btsnoop).grid(row=0, column=2, padx=6)
            self.retrieve_status = ttk.Label(self.body, foreground="#555",
                                             text="No log loaded yet.")
            self.retrieve_status.pack(anchor="w", pady=8)

        def _set_btsnoop(self, path):
            self.btsnoop = path
            if path:
                self._note_saved(path)
            msg = f"Loaded: {path}" if path else "Could not find a btsnoop log."
            self.retrieve_status.configure(text=msg,
                                           foreground="#1a7f37" if path else "#b00020")

        def _pull_bugreport(self):
            zpath = os.path.join(self.workdir, "bugreport.zip")
            self._log("Running adb bugreport (this can take a few minutes)…")

            def done(rc, _out):
                if rc != 0 or not os.path.exists(zpath):
                    self._set_btsnoop(None)
                    return
                self._note_saved(zpath)
                dest = extract_btsnoop(zpath, self.workdir)
                if dest:
                    self._log(f"Extracted {dest}")
                else:
                    self._log("! no btsnoop log inside the bugreport (vendor may omit it)")
                self._set_btsnoop(dest)

            self._run([self.adb, "bugreport", zpath], on_done=done)

        def _pull_sdcard(self):
            dest = os.path.join(self.workdir, "btsnoop_hci.log")
            self._run([self.adb, "pull", "/sdcard/btsnoop_hci.log", dest],
                      on_done=lambda rc, _o: self._set_btsnoop(dest if rc == 0 and os.path.exists(dest) else None))

        def _choose_btsnoop(self):
            path = filedialog.askopenfilename(title="Select btsnoop_hci.log or bugreport.zip")
            if not path:
                return
            if path.lower().endswith(".zip"):
                path = extract_btsnoop(path, self.workdir)
            self._set_btsnoop(path)

        # ---- step 5: channel ----
        def _step_channel(self):
            if not self.btsnoop:
                ttk.Label(self.body, foreground="#b00020",
                          text="Load a btsnoop log in the previous step first.").pack(anchor="w")
                return
            ttk.Button(self.body, text="Scan for RFCOMM channels",
                       command=self._scan_channels).pack(anchor="w")
            self.chan_list = tk.Listbox(self.body, height=8, font=("monospace", 9))
            self.chan_list.pack(fill="both", expand=True, pady=8)
            ttk.Label(self.body, foreground="#555", wraplength=560, text=(
                "The busiest channel is almost always WearableBt. Select the row "
                "for the watch→phone direction (watch address as source)."
            )).pack(anchor="w")

        def _scan_channels(self):
            def done(_rc, out):
                self.endpoints = parse_rfcomm_endpoints(out)
                self.chan_list.delete(0, "end")
                if not self.endpoints:
                    self.chan_list.insert("end", "no RFCOMM frames found")
                    return
                for ch, src, dst, n in self.endpoints:
                    self.chan_list.insert("end", f"ch {ch:<3} {src} → {dst}   {n} frames")
            self._run(channel_scan_cmd(self.tshark, self.btsnoop), on_done=done)

        # ---- step 6: decode ----
        def _step_decode(self):
            ttk.Label(self.body, text=(
                "Export the selected direction and decode it. If nothing is "
                "selected, the busiest channel (both directions) is used."
            ), wraplength=560).pack(anchor="w")
            ttk.Button(self.body, text="Export + decode",
                       command=self._export_and_decode).pack(anchor="w", pady=6)
            self.decode_out = scrolledtext.ScrolledText(self.body, height=12,
                                                        font=("monospace", 9), wrap="word")
            self.decode_out.pack(fill="both", expand=True)
            self.saved_label = ttk.Label(self.body, foreground="#555", justify="left")
            self.saved_label.pack(anchor="w", pady=(8, 0))
            self._refresh_saved()

        def _selected_endpoint(self):
            try:
                idx = self.chan_list.curselection()[0]
                return self.endpoints[idx]
            except (AttributeError, IndexError):
                return self.endpoints[0] if self.endpoints else None

        def _export_and_decode(self):
            if not (self.btsnoop and self.tshark and self.cli):
                messagebox.showerror("WearOut", "Need btsnoop log, tshark and wearout-cli.")
                return
            ep = self._selected_endpoint()
            if not ep:
                messagebox.showinfo("WearOut", "Scan for channels first (step 5).")
                return
            ch, src, dst, _n = ep
            binp = os.path.join(self.workdir, f"ch{ch}_{src or 'any'}_to_{dst or 'any'}.bin".replace(":", ""))
            self.decode_out.delete("1.0", "end")

            def exported(_rc, out):
                data = hexfields_to_bytes(out)
                with open(binp, "wb") as f:
                    f.write(data)
                self._note_saved(binp)
                self._log(f"wrote {len(data)} bytes to {binp}")
                if not data:
                    self._log("! no payload on this channel/direction; try another row")
                    return
                decoded_txt = binp + ".decoded.txt"
                self._run(decode_cmd(self.cli, binp), save_stdout=decoded_txt,
                          on_done=lambda rc, o: self._show_decode(rc, o, decoded_txt))

            self._run(export_cmd(self.tshark, self.btsnoop, ch, src, dst), on_done=exported)

        # ---- step 7: package ----
        def _step_package(self):
            ttk.Label(self.body, wraplength=560, justify="left", text=(
                "Collect the phone's device info, then bundle everything in the "
                "working folder into one zip you can attach to the chat."
            )).pack(anchor="w")

            self.redact_var = tk.BooleanVar(value=False)
            ttk.Checkbutton(self.body, variable=self.redact_var,
                            text="Best-effort redact emails/long numbers in text files "
                                 "(not the raw captures)").pack(anchor="w", pady=6)

            self.pkg_list = tk.Listbox(self.body, height=8, font=("monospace", 9))
            self.pkg_list.pack(fill="both", expand=True, pady=4)
            self._refresh_pkg_list()

            row = ttk.Frame(self.body)
            row.pack(anchor="w", pady=6)
            ttk.Button(row, text="Refresh list", command=self._refresh_pkg_list).grid(row=0, column=0, padx=(0, 6))
            ttk.Button(row, text="Collect info + build zip",
                       command=self._collect_and_package).grid(row=0, column=1)
            self.pkg_status = ttk.Label(self.body, foreground="#555", wraplength=560,
                                        text="Nothing packaged yet.")
            self.pkg_status.pack(anchor="w", pady=(6, 0))

        def _refresh_pkg_list(self):
            if not (hasattr(self, "pkg_list") and self.pkg_list.winfo_exists()):
                return
            self.pkg_list.delete(0, "end")
            names = files_to_package(self.workdir)
            if not names:
                self.pkg_list.insert("end", "(working folder is empty)")
            for n in names:
                try:
                    sz = os.path.getsize(os.path.join(self.workdir, n))
                except OSError:
                    sz = 0
                self.pkg_list.insert("end", f"{n}  ({sz} bytes)")

        def _collect_and_package(self):
            import datetime
            self.pkg_status.configure(text="Collecting device info and building zip…")

            def work():
                # 1. device info (best effort; skip if no adb)
                if self.adb:
                    info_path = os.path.join(self.workdir, "device_info.txt")
                    try:
                        with open(info_path, "w") as f:
                            for label, cmd in device_info_cmds(self.adb):
                                f.write(f"==== {label} ====\n")
                                try:
                                    r = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
                                    f.write(r.stdout or "")
                                    if r.stderr:
                                        f.write("[stderr] " + r.stderr)
                                except (OSError, subprocess.SubprocessError) as e:
                                    f.write(f"[failed: {e}]\n")
                                f.write("\n")
                        self.after(0, lambda: self._note_saved(info_path))
                    except OSError as e:
                        self.after(0, lambda: self._log(f"! device info failed: {e}"))

                # 2. manifest + zip
                names = files_to_package(self.workdir)
                sized = [(n, os.path.getsize(os.path.join(self.workdir, n))) for n in names]
                meta = {
                    "generated": datetime.datetime.now().isoformat(timespec="seconds"),
                    "tool": "WearOut capture wizard",
                }
                manifest = build_manifest(sized, meta)
                man_path = os.path.join(self.workdir, "MANIFEST.md")
                try:
                    with open(man_path, "w") as f:
                        f.write(manifest)
                except OSError:
                    pass

                redact = bool(self.redact_var.get())
                parent = os.path.dirname(os.path.abspath(self.workdir)) or "."
                zpath = package_zip_path(parent)
                top = "wearout-capture"
                try:
                    import zipfile as _zip
                    with _zip.ZipFile(zpath, "w", _zip.ZIP_DEFLATED) as z:
                        # MANIFEST.md was just written into workdir, so
                        # files_to_package already includes it.
                        for n in files_to_package(self.workdir):
                            src = os.path.join(self.workdir, n)
                            if not os.path.isfile(src):
                                continue
                            arc = f"{top}/{n}"
                            if redact and is_text_artifact(n):
                                with open(src, "r", errors="replace") as fh:
                                    z.writestr(arc, redact_text(fh.read()))
                            else:
                                z.write(src, arc)
                    size = os.path.getsize(zpath)
                    msg = f"Built {zpath}  ({size} bytes). Attach this to the chat."
                except OSError as e:
                    zpath, msg = None, f"Could not build zip: {e}"

                def done():
                    self._log(msg)
                    self.pkg_status.configure(text=msg,
                                              foreground="#1a7f37" if zpath else "#b00020")
                self.after(0, done)

            threading.Thread(target=work, daemon=True).start()

        def _show_decode(self, _rc, out, saved_path=None):
            self.decode_out.insert("end", out)
            self.decode_out.see("end")
            if saved_path:
                self._note_saved(saved_path)
            self._refresh_saved()

        def _refresh_saved(self):
            if hasattr(self, "saved_label") and self.saved_label.winfo_exists():
                names = "\n".join("  • " + os.path.basename(p) for p in self.saved) or "  (none yet)"
                self.saved_label.configure(text=f"Saved in {self.workdir}:\n{names}")

    Wizard().mainloop()


def main():
    try:
        import tkinter  # noqa: F401
    except Exception:
        print("Tkinter is not available. Install it (e.g. `sudo apt install python3-tk`)\n"
              "and run on a machine with a display.")
        return 1
    run_gui()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
