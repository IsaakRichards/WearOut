#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""
Scan an Android APK (or .apks/.xapk/.apkm bundle) for its Wear OS
interoperability surface, without decompiling to Java.

It reports, as plain interop facts:
  * WearableListenerService declarations and the wear:// path prefixes they
    listen on (from the binary AndroidManifest),
  * Data Layer message/data paths found in the DEX string pools,
  * CapabilityClient capability names,
  * protobuf message class names (helps identify a vendor's wire format),
  * Google Wear Companion Library setup steps present.

    python3 tools/apk_wear_scan.py OHealth.apks
    python3 tools/apk_wear_scan.py OHealth.apks --markdown > report.md

Stdlib only. Reuses apk_identity for bundle handling.
"""
import argparse
import glob
import os
import re
import struct
import sys
import tempfile
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import apk_identity  # noqa: E402


# ---- binary AndroidManifest (AXML) -----------------------------------------

class Axml:
    START = 0x0102
    END = 0x0103

    def __init__(self, data: bytes):
        self.d = data
        if self._u16(0) != 0x0003:
            raise ValueError("not a binary AndroidManifest")
        self._parse_strings()

    def _u16(self, o): return struct.unpack_from("<H", self.d, o)[0]
    def _u32(self, o): return struct.unpack_from("<I", self.d, o)[0]
    def _i32(self, o): return struct.unpack_from("<i", self.d, o)[0]

    def _parse_strings(self):
        sp = 8
        self.sp_size = self._u32(sp + 4)
        count = self._u32(sp + 8)
        flags = self._u32(sp + 16)
        self.utf8 = bool(flags & 0x100)
        start = sp + self._u32(sp + 20)
        self.sp = sp
        self._offs = [self._u32(sp + 28 + 4 * i) for i in range(count)]
        self._str_start = start
        self._cache = {}

    def s(self, i):
        if i < 0:
            return None
        if i in self._cache:
            return self._cache[i]
        o = self._str_start + self._offs[i]
        if self.utf8:
            n = self.d[o]; p = o + 1
            if n & 0x80:
                p += 1
            blen = self.d[p]; p += 1
            if blen & 0x80:
                blen = ((blen & 0x7f) << 8) | self.d[p]; p += 1
            v = self.d[p:p + blen].decode("utf-8", "replace")
        else:
            n = self._u16(o); p = o + 2
            if n & 0x8000:
                n = ((n & 0x7fff) << 16) | self._u16(p); p += 2
            v = self.d[p:p + 2 * n].decode("utf-16-le", "replace")
        self._cache[i] = v
        return v

    def elements(self):
        """Yield (event, tag, attrs) for every start element."""
        pos = self.sp + self.sp_size
        while pos < len(self.d):
            ct = self._u16(pos)
            cs = self._u32(pos + 4)
            if cs == 0:
                break
            if ct == self.START:
                name = self.s(self._i32(pos + 20))
                n_attr = self._u16(pos + 28)
                attrs = {}
                ao = pos + 36
                for _ in range(n_attr):
                    a_name = self.s(self._i32(ao + 4))
                    a_raw = self._i32(ao + 8)
                    a_type = self.d[ao + 15]
                    a_val = self._u32(ao + 16)
                    if a_raw != -1:
                        v = self.s(a_raw)
                    elif a_type == 0x10:
                        v = str(self._i32(ao + 16))
                    elif a_type == 0x12:
                        v = "true" if a_val else "false"
                    else:
                        v = "@%08x" % a_val
                    attrs[a_name] = v
                    ao += 20
                yield ("start", name, attrs)
            pos += cs


def manifest_wear_services(axml: Axml):
    """List (service_name, [actions], [data-filters]) for wearable services."""
    services = []
    cur = None
    actions, datas = [], []

    def flush():
        if cur and (actions or datas):
            services.append((cur, list(actions), list(datas)))

    for _, tag, attrs in axml.elements():
        if tag == "service":
            flush()
            cur = attrs.get("name", "?")
            actions, datas = [], []
        elif tag == "action":
            n = attrs.get("name", "")
            if "wearable" in n.lower() or n.endswith((".WEARABLE",)):
                actions.append(n)
        elif tag == "data":
            if attrs.get("scheme") == "wear" or attrs.get("pathPrefix"):
                datas.append(attrs)
    flush()
    # keep only services that actually reference the wear scheme or gms wearable
    out = []
    for name, acts, ds in services:
        if any("gms.wearable" in a for a in acts) or any(d.get("scheme") == "wear" for d in ds):
            out.append((name, acts, ds))
    return out


# ---- DEX string pools ------------------------------------------------------

def dex_strings(apk_path: str):
    strings = set()
    pat = re.compile(rb"[\x20-\x7e]{3,}")
    with zipfile.ZipFile(apk_path) as z:
        names = [n for n in z.namelist() if re.fullmatch(r"classes\d*\.dex", n)]
        for n in names:
            for m in pat.finditer(z.read(n)):
                strings.add(m.group())
    return sorted(s.decode("latin1") for s in strings), names


def _resolve_apk(path):
    """Return a real .apk path (extracting base.apk from a bundle if needed)
    plus an optional TemporaryDirectory to keep alive."""
    members = apk_identity._bundle_members(path)
    if not members:
        return path, None
    tmp = tempfile.TemporaryDirectory()
    with zipfile.ZipFile(path) as z:
        z.extract(members[0], tmp.name)
    return os.path.join(tmp.name, members[0]), tmp


FILTERS = {
    "message_paths": r"^/[A-Za-z][A-Za-z0-9_]*(?:/[A-Za-z0-9_][A-Za-z0-9_.-]*){1,6}$",
    "capabilities": None,  # handled specially
    "proto_classes": r"^L[A-Za-z0-9_/$]+/proto/[A-Za-z0-9_$]+;$",
    "setup_steps": r"wear/companion/setup/.*SetupStep",
}


def scan(path):
    apk, holder = _resolve_apk(path)
    try:
        with zipfile.ZipFile(apk) as z:
            axml = Axml(z.read("AndroidManifest.xml"))
            pkg = next((a.get("package") for _, t, a in axml.elements() if t == "manifest"), None)
        services = manifest_wear_services(axml)
        S, dexnames = dex_strings(apk)
    finally:
        if holder:
            holder.cleanup()

    def grep(rx):
        r = re.compile(rx)
        return sorted({s for s in S if r.search(s)})

    # Real Data Layer paths: short, few segments, no spaces, not a class path.
    # Path-shaped strings are plentiful; keep those that look like Data Layer
    # endpoints by matching interop-relevant keywords. Widen KEEP as needed.
    KEEP = re.compile(r"(?i)(heytap|watch|wear|/node|/rpc|/sync|setup|pair|"
                      r"notif|/call|telecom|media|music|find|account|oobe|"
                      r"migrat|capab|health|sport|ecg|contact|weather|/ota)")
    paths = [p for p in grep(r"^/[A-Za-z][A-Za-z0-9_]*(/[A-Za-z0-9_][A-Za-z0-9_.-]*)+$")
             if len(p) <= 48 and p.count("/") <= 5 and ".." not in p and KEEP.search(p)]
    caps = grep(r"^[a-z][a-z0-9_]{4,}_capability$")

    # Class descriptors can appear with a leading MUTF-8 length byte; pull the
    # Lx/y/z; descriptor out of each string and reduce to short class names.
    def classes(substr, suffix):
        r = re.compile(r"L[A-Za-z0-9_/$]*" + substr + r"[A-Za-z0-9_/$]*;")
        found = set()
        for s in S:
            for m in r.finditer(s):
                desc = m.group()
                if desc.endswith(suffix):
                    found.add(desc[1:-1].replace("/", "."))
        return sorted(found)

    # Top-level proto messages only (drop nested $Builder / $Foo helpers).
    protos = sorted({c for c in classes("/proto/", ";") if "$" not in c})
    setup = sorted({c for c in classes("/companion/setup/", "Step;")
                    if c.endswith("Step") and "$" not in c and not c.startswith("L")})
    return {
        "package": pkg,
        "dex_count": len(dexnames),
        "services": services,
        "paths": paths,
        "capabilities": caps,
        "proto_classes": protos,
        "setup_steps": setup,
    }


def render(r, markdown=False):
    h1 = (lambda t: f"\n## {t}\n") if markdown else (lambda t: f"\n=== {t} ===")
    bullet = "- " if markdown else "  "
    lines = []
    lines.append(f"# Wear OS interop scan: {r['package']}" if markdown else f"package: {r['package']}")
    lines.append(f"{r['dex_count']} dex files")

    lines.append(h1("Wearable listener services"))
    for name, acts, ds in r["services"]:
        lines.append(f"\n**{name}**" if markdown else f"\n{name}")
        for a in acts:
            lines.append(f"{bullet}action {a}")
        for d in ds:
            seg = " ".join(f"{k}={v}" for k, v in d.items() if v)
            lines.append(f"{bullet}data   {seg}")

    for title, key in [("Data Layer paths", "paths"), ("Capability names", "capabilities"),
                       ("protobuf message classes", "proto_classes"),
                       ("Companion setup steps", "setup_steps")]:
        lines.append(h1(f"{title} ({len(r[key])})"))
        for x in r[key]:
            lines.append(f"{bullet}{x}")
    return "\n".join(lines)


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("apk")
    ap.add_argument("--markdown", action="store_true")
    args = ap.parse_args(argv[1:])
    try:
        r = scan(args.apk)
    except (OSError, ValueError, zipfile.BadZipFile, struct.error) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    print(render(r, args.markdown))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
