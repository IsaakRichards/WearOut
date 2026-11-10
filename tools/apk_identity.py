#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""
Print the signing-certificate digests of an APK.

Wear OS Data Layer traffic is only delivered to a watch app if it claims to
come from the phone-side app with the same package name AND signing
certificate. To talk to the OnePlus Watch 4's OHealth watch app, WearOut has
to present OHealth's identity, so we need the digest of OHealth's cert.

    python3 tools/apk_identity.py OHealth.apk

Reads APK Signature Scheme v3.1/v3/v2 blocks directly (stdlib only). Falls
back to the v1 META-INF/*.RSA|DSA|EC PKCS#7 via the `openssl` binary.
"""
import base64
import hashlib
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile

APK_SIG_MAGIC = b"APK Sig Block 42"
SCHEMES = {
    0x1B93AD61: "v3.1",
    0xF05368C0: "v3",
    0x7109871A: "v2",
}


class ApkError(Exception):
    pass


def _find_eocd(data: bytes) -> int:
    # EOCD is at least 22 bytes and may be followed by a <=65535 byte comment.
    start = max(0, len(data) - 22 - 0xFFFF)
    idx = data.rfind(b"PK\x05\x06", start)
    if idx < 0:
        raise ApkError("not a zip file (no end of central directory)")
    return idx


def _signing_block(data: bytes):
    eocd = _find_eocd(data)
    cd_offset = struct.unpack_from("<I", data, eocd + 16)[0]
    if cd_offset < 32 or data[cd_offset - 16:cd_offset] != APK_SIG_MAGIC:
        return None
    size = struct.unpack_from("<Q", data, cd_offset - 24)[0]
    block_start = cd_offset - size - 8
    if block_start < 0 or struct.unpack_from("<Q", data, block_start)[0] != size:
        raise ApkError("corrupt APK signing block")
    # id-value pairs between the leading size and the trailing size+magic
    pos, end = block_start + 8, cd_offset - 24
    pairs = {}
    while pos < end:
        plen = struct.unpack_from("<Q", data, pos)[0]
        pid = struct.unpack_from("<I", data, pos + 8)[0]
        pairs[pid] = data[pos + 12:pos + 8 + plen]
        pos += 8 + plen
    return pairs


def _lp(buf: bytes, pos: int):
    """Read a uint32 length-prefixed slice."""
    (n,) = struct.unpack_from("<I", buf, pos)
    return buf[pos + 4:pos + 4 + n], pos + 4 + n


def _first_cert_from_scheme(value: bytes) -> bytes:
    signers, _ = _lp(value, 0)
    signer, _ = _lp(signers, 0)
    signed_data, _ = _lp(signer, 0)
    _digests, p = _lp(signed_data, 0)
    certs, _ = _lp(signed_data, p)
    cert, _ = _lp(certs, 0)
    return cert


def _cert_from_v1(path: str) -> bytes:
    with zipfile.ZipFile(path) as z:
        names = [n for n in z.namelist()
                 if n.upper().startswith("META-INF/") and n.upper().endswith((".RSA", ".DSA", ".EC"))]
        if not names:
            raise ApkError("APK has no v2/v3 block and no v1 signature")
        pkcs7 = z.read(names[0])
    if not shutil.which("openssl"):
        raise ApkError("v1-only APK and no openssl binary to parse it")
    with tempfile.NamedTemporaryFile() as f:
        f.write(pkcs7)
        f.flush()
        pem = subprocess.run(["openssl", "pkcs7", "-inform", "DER", "-in", f.name, "-print_certs"],
                             check=True, capture_output=True).stdout
    first_end = pem.find(b"-----END CERTIFICATE-----")
    first = pem[:first_end]
    b64 = b"".join(l for l in first.splitlines() if l and not l.startswith((b"-----", b"subject", b"issuer")))
    return base64.b64decode(b64)


def signing_cert(path: str):
    with open(path, "rb") as f:
        data = f.read()
    pairs = _signing_block(data)
    if pairs:
        for scheme_id, name in SCHEMES.items():
            if scheme_id in pairs:
                return name, _first_cert_from_scheme(pairs[scheme_id])
    return "v1", _cert_from_v1(path)


def digests(cert: bytes) -> dict:
    s1 = hashlib.sha1(cert).digest()
    s256 = hashlib.sha256(cert).digest()
    return {
        "sha1_hex": s1.hex(),
        "sha1_b64url": base64.urlsafe_b64encode(s1).rstrip(b"=").decode(),
        "sha256_hex": s256.hex(),
    }


def main(argv):
    if len(argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    try:
        scheme, cert = signing_cert(argv[1])
    except (ApkError, OSError, struct.error, subprocess.CalledProcessError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    d = digests(cert)
    print(f"scheme      {scheme}")
    print(f"sha1 hex    {d['sha1_hex']}")
    print(f"sha1 b64url {d['sha1_b64url']}")
    print(f"sha256 hex  {d['sha256_hex']}")
    print()
    print("Which form the watch expects in Request.signatureDigest is not yet")
    print("confirmed; microG uses lowercase SHA-1 hex. Try that first.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
