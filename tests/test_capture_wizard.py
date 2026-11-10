#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Tests for the pure helpers in tools/capture_wizard.py (no Tkinter needed)."""
import io
import os
import sys
import unittest
import zipfile

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import capture_wizard as cw  # noqa: E402


class HexTest(unittest.TestCase):
    def test_plain(self):
        self.assertEqual(cw.hexfields_to_bytes("0a1f08"), b"\x0a\x1f\x08")

    def test_separators_and_lines(self):
        self.assertEqual(cw.hexfields_to_bytes("0a:1f\n08,09\n"), b"\x0a\x1f\x08\x09")

    def test_odd_nibble_dropped(self):
        self.assertEqual(cw.hexfields_to_bytes("0a1"), b"\x0a")

    def test_empty(self):
        self.assertEqual(cw.hexfields_to_bytes("\n\n"), b"")


class EndpointTest(unittest.TestCase):
    def test_ranking_and_filtering(self):
        out = "\n".join([
            "aa:bb\tcc:dd\t3",
            "aa:bb\tcc:dd\t3",
            "cc:dd\taa:bb\t3",
            "\t\t",            # empty row ignored
            "aa:bb\tcc:dd\t",  # missing channel ignored
            "aa:bb\tcc:dd\t12",
        ])
        ranked = cw.parse_rfcomm_endpoints(out)
        self.assertEqual(ranked[0], (3, "aa:bb", "cc:dd", 2))
        # three distinct (ch,src,dst) keys survive
        self.assertEqual(len(ranked), 3)
        self.assertIn((12, "aa:bb", "cc:dd", 1), ranked)

    def test_multichannel_field_takes_first(self):
        ranked = cw.parse_rfcomm_endpoints("aa\tbb\t3,5\n")
        self.assertEqual(ranked, [(3, "aa", "bb", 1)])


class CmdTest(unittest.TestCase):
    def test_export_cmd_direction(self):
        cmd = cw.export_cmd("tshark", "f.log", 3, src="aa:bb", dst="cc:dd")
        self.assertIn("btrfcomm.channel==3 && bluetooth.src==aa:bb && bluetooth.dst==cc:dd", cmd)
        self.assertIn("data.data", cmd)

    def test_export_cmd_channel_only(self):
        cmd = cw.export_cmd("tshark", "f.log", 7)
        joined = " ".join(cmd)
        self.assertIn("btrfcomm.channel==7", joined)
        self.assertNotIn("bluetooth.src", joined)

    def test_decode_cmd(self):
        self.assertEqual(cw.decode_cmd("cli", "x.bin"), ["cli", "-v", "decode", "x.bin"])
        self.assertEqual(cw.decode_cmd("cli", "x.bin", verbose=False), ["cli", "decode", "x.bin"])

    def test_scan_cmd(self):
        cmd = cw.channel_scan_cmd("tshark", "f.log")
        self.assertEqual(cmd[:3], ["tshark", "-r", "f.log"])
        self.assertIn("btrfcomm", cmd)


class ZipTest(unittest.TestCase):
    def _zip(self, path, names):
        with zipfile.ZipFile(path, "w") as z:
            for n in names:
                z.writestr(n, b"data-" + n.encode())

    def test_find_and_extract(self):
        import tempfile
        d = tempfile.mkdtemp()
        zp = os.path.join(d, "bugreport.zip")
        self._zip(zp, ["FS/data/misc/bluetooth/logs/btsnoop_hci.log",
                       "FS/data/misc/bluetooth/logs/btsnoop_hci.log.last",
                       "bugreport-main.txt"])
        self.assertTrue(cw.find_btsnoop_in_zip(zp).endswith("btsnoop_hci.log"))
        out = cw.extract_btsnoop(zp, os.path.join(d, "out"))
        self.assertTrue(out.endswith("btsnoop_hci.log"))
        with open(out, "rb") as f:
            self.assertEqual(f.read(), b"data-FS/data/misc/bluetooth/logs/btsnoop_hci.log")

    def test_no_btsnoop(self):
        import tempfile
        d = tempfile.mkdtemp()
        zp = os.path.join(d, "b.zip")
        self._zip(zp, ["bugreport-main.txt"])
        self.assertIsNone(cw.find_btsnoop_in_zip(zp))
        self.assertIsNone(cw.extract_btsnoop(zp, d))


if __name__ == "__main__":
    unittest.main()
