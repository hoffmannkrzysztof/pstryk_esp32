# tools/

## `bin_signing.py`

Vendored verbatim from **espressif/arduino-esp32, tag `3.3.8`**
(`tools/bin_signing.py`, sha256
`e88884e27aea66489a94988e8709300856a49b8be798c29d64a8daaac37accd9`).

It used to be `curl`ed from the `master` branch inside the release job — unpinned,
unchecksummed, and executed in the same job that writes the OTA private key from
`secrets.OTA_SIGNING_KEY`. The signature it produces is the *only* trust anchor for
OTA on both boards (`OtaUpdater` uses `setInsecure()`, and the public key is
compiled in and cannot be rotated without a USB reflash), so that tool must be as
pinned as the platform in `platformio.ini` — and reviewable in a PR.

The version matters beyond supply chain: the firmware side is hard-coupled to this
script's output format. `Updater.cpp` assumes a fixed 512-byte signature block and
`Updater_Signing.cpp` *derives* the PSS salt length (`key_len - hash_size - 2`)
rather than reading it, with a comment saying "to match bin_signing.py". Bump this
file deliberately, together with the `platform =` pin, and re-verify an OTA
end-to-end afterwards.

At the time of vendoring, `master` and `3.3.8` were byte-identical, and upstream had
exactly one commit touching this file (`77ab98f`, 2025-12-18).

### Local deviation from upstream

One change, in `main()`: upstream discards `verify_signature()`'s return value, so
`--verify` exits **0 even when verification fails**. The release workflow's
self-verify step was therefore a decoration that `set -euo pipefail` could never
catch — a signature that did not match would have shipped. The vendored copy exits
non-zero instead. The signing path is untouched, so the bytes it produces are
identical to upstream's. Re-apply this when bumping the file.

## `extract_embedded_pubkey.py`

Reconstructs the PEM public key from the `PUBLIC_KEY[]` byte array in
`src/net/ota_public_key.h`, so the release can verify each signed image against the
key the **firmware** will actually use. Verifying against a key re-extracted from
the signing private key is circular: it passes even when the embedded key and the
signing key have drifted apart, which is precisely the failure that bricks OTA for
every device in the field.
