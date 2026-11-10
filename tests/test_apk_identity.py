#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Tests for tools/apk_identity.py using synthetic APKs."""
import hashlib
import io
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import apk_identity  # noqa: E402


def lp(b: bytes) -> bytes:
    return struct.pack("<I", len(b)) + b


def make_zip(files: dict) -> bytes:
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as z:
        for name, data in files.items():
            z.writestr(name, data)
    return buf.getvalue()


def insert_v2_block(zip_bytes: bytes, cert: bytes, scheme_id=0x7109871A) -> bytes:
    eocd = zip_bytes.rfind(b"PK\x05\x06")
    cd_off = struct.unpack_from("<I", zip_bytes, eocd + 16)[0]
    signed_data = lp(b"") + lp(lp(cert)) + lp(b"")          # digests, certs, attrs
    signer = lp(signed_data) + lp(b"") + lp(b"pubkey")         # signed data, sigs, key
    value = lp(lp(signer))
    pair = struct.pack("<QI", len(value) + 4, scheme_id) + value
    size = len(pair) + 24
    block = struct.pack("<Q", size) + pair + struct.pack("<Q", size) + apk_identity.APK_SIG_MAGIC
    out = bytearray(zip_bytes[:cd_off] + block + zip_bytes[cd_off:])
    struct.pack_into("<I", out, eocd + len(block) + 16, cd_off + len(block))
    return bytes(out)


class ApkIdentityTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def write(self, name, data):
        p = os.path.join(self.tmp, name)
        with open(p, "wb") as f:
            f.write(data)
        return p

    def test_v2_block(self):
        cert = b"\x30\x82fake-der-certificate"
        apk = insert_v2_block(make_zip({"AndroidManifest.xml": b"x", "classes.dex": b"y"}), cert)
        scheme, got = apk_identity.signing_cert(self.write("a.apk", apk))
        self.assertEqual(scheme, "v2")
        self.assertEqual(got, cert)
        # The zip must still be readable after inserting the block.
        with zipfile.ZipFile(os.path.join(self.tmp, "a.apk")) as z:
            self.assertEqual(z.read("classes.dex"), b"y")

    def test_v3_preferred_over_v2(self):
        apk = insert_v2_block(make_zip({"a": b"1"}), b"v3cert", scheme_id=0xF05368C0)
        scheme, got = apk_identity.signing_cert(self.write("b.apk", apk))
        self.assertEqual((scheme, got), ("v3", b"v3cert"))

    def test_digests(self):
        d = apk_identity.digests(b"abc")
        self.assertEqual(d["sha1_hex"], hashlib.sha1(b"abc").hexdigest())
        self.assertNotIn("=", d["sha1_b64url"])

    @unittest.skipUnless(shutil.which("openssl"), "openssl not installed")
    def test_v1_fallback(self):
        key = os.path.join(self.tmp, "k.pem")
        crt = os.path.join(self.tmp, "c.pem")
        p7 = os.path.join(self.tmp, "c.p7")
        subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", key,
                        "-out", crt, "-subj", "/CN=wearout-test", "-days", "1"],
                       check=True, capture_output=True)
        subprocess.run(["openssl", "crl2pkcs7", "-nocrl", "-certfile", crt, "-outform", "DER", "-out", p7],
                       check=True, capture_output=True)
        der = subprocess.run(["openssl", "x509", "-in", crt, "-outform", "DER"],
                             check=True, capture_output=True).stdout
        with open(p7, "rb") as f:
            apk = make_zip({"AndroidManifest.xml": b"x", "META-INF/CERT.RSA": f.read()})
        scheme, got = apk_identity.signing_cert(self.write("c.apk", apk))
        self.assertEqual(scheme, "v1")
        self.assertEqual(got, der)

    def test_not_zip(self):
        with self.assertRaises(apk_identity.ApkError):
            apk_identity.signing_cert(self.write("d.apk", b"nope"))


if __name__ == "__main__":
    unittest.main()
