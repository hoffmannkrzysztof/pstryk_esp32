#!/usr/bin/env python3
"""Reconstruct the PEM public key that is COMPILED INTO the firmware.

Reads the byte array in src/net/ota_public_key.h and writes it back out as PEM.

Why this exists: the release job used to self-verify each signed image against a
public key re-extracted from the same private key it had just signed with. That
check is circular -- it passes even if the embedded key and the signing key have
drifted apart, which is the one failure that bricks OTA for every device in the
field (the public key is compiled in and cannot be rotated without a USB reflash).
Verifying against THIS key instead makes the check meaningful: it answers "will the
firmware we are about to publish accept this signature?"

Usage: python tools/extract_embedded_pubkey.py [--header PATH] --out public_key.pem
"""

import argparse
import re
import sys

DEFAULT_HEADER = "src/net/ota_public_key.h"


def extract(header_path):
    src = open(header_path, "r", encoding="utf-8").read()
    # Everything between the array's '{' and its closing '};'
    match = re.search(r"PUBLIC_KEY\s*\[\s*\]\s*=\s*\{(.*?)\}\s*;", src, re.S)
    if not match:
        raise SystemExit(f"{header_path}: PUBLIC_KEY[] array not found")
    data = bytes(int(b, 16) for b in re.findall(r"0x([0-9a-fA-F]{2})", match.group(1)))
    data = data.rstrip(b"\x00")  # the header keeps a trailing NUL for mbedtls
    text = data.decode("ascii")
    if "BEGIN PUBLIC KEY" not in text or "END PUBLIC KEY" not in text:
        raise SystemExit(f"{header_path}: decoded bytes are not a PEM public key")
    return text


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--header", default=DEFAULT_HEADER)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    pem = extract(args.header)
    with open(args.out, "w", encoding="utf-8") as f:
        f.write(pem)
    print(f"wrote {args.out} ({len(pem)} bytes) from {args.header}", file=sys.stderr)


if __name__ == "__main__":
    main()
