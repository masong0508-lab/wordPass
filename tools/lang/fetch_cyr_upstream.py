#!/usr/bin/env python3
"""Fetch exact licensed RU/UK Hunspell sources for an opt-in DS preview.

Only pinned LibreOffice dictionary source files are downloaded. The expected
Git blob SHA-1 of every file is checked BEFORE use; any upstream mutation,
truncation, transfer failure or missing source notice aborts. Original
readmes and sources are retained beside the derived experimental pack.

Use:
 python3 tools/lang/fetch_cyr_upstream.py --out ../wordpass-lexicon-upstream
 python3 tools/lang/fetch_cyr_upstream.py --selftest
"""
import argparse
import hashlib
import json
from pathlib import Path
import tempfile
from urllib.request import Request, urlopen

REPO = "LibreOffice/dictionaries"
PIN = "c0865a85eb67dc377f2cf8e8fe2c4fa9e53f0dcb"
FILES = {
    "ru": {
        "dic": ("ru_RU/ru_RU.dic", "e5cfd3614c2303be12da481198b5d914e592da9c"),
        "aff": ("ru_RU/ru_RU.aff", "b6bef6d261914c579542b59c89dd03b83ae5927c"),
        "notice": ("ru_RU/README_ru_RU.txt", "4620b48a8a37d8f3f29dda667ea0aacc992bd26c"),
    },
    "uk": {
        "dic": ("uk_UA/uk_UA.dic", "ff848bbb610332bec2fa867f91d7489154c3585d"),
        "aff": ("uk_UA/uk_UA.aff", "2a604054953b9997b1b7250427fb244736a1c66b"),
        "notice": ("uk_UA/README_uk_UA.txt", "bce4936be9f0c6cf7ac6d729d3f81ce0b4d79bf8"),
    },
}
MAX_BYTES = 20 * 1024 * 1024


def git_blob_sha(payload):
    return hashlib.sha1(
        f"blob {len(payload)}\0".encode("ascii") + payload
    ).hexdigest()


def accept(payload, expected_sha):
    if not payload or len(payload) > MAX_BYTES:
        raise ValueError("upstream input empty or exceeds fixed 20 MiB limit")
    if git_blob_sha(payload) != expected_sha:
        raise ValueError("upstream input Git blob hash differs from pinned source")
    return payload


def fetch(out):
    out = Path(out)
    manifest = {
        "upstream": REPO,
        "commit": PIN,
        "purpose": "unreviewed experimental wordPass RU/UK sourced preview",
        "languages": {},
    }
    for code, entries in FILES.items():
        assets = {}
        for kind, (path, blob) in entries.items():
            url = f"https://raw.githubusercontent.com/{REPO}/{PIN}/{path}"
            request = Request(url, headers={
                "User-Agent": "wordPass-RU-UK-source-pinned-builder/1.0"
            })
            with urlopen(request, timeout=120) as response:
                payload = response.read(MAX_BYTES + 1)
            accept(payload, blob)
            # Strict decoding protects the later Hunspell reader: malformed
            # bytes must not silently become a smaller, altered dictionary.
            payload.decode("utf-8")
            dest = out / path
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(payload)
            assets[kind] = {
                "path": path, "pinned_blob_sha1": blob,
                "sha256": hashlib.sha256(payload).hexdigest(),
                "bytes": len(payload),
                "url": f"https://github.com/{REPO}/blob/{PIN}/{path}",
            }
        manifest["languages"][code] = assets
    (out / "SOURCE-LOCK.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    return manifest


def selftest():
    with tempfile.TemporaryDirectory(prefix="wordpass-source-lock-") as td:
        b = "КНИГА\nКНИЖКА\n".encode("utf-8")
        sha = git_blob_sha(b)
        assert len(sha) == 40 and accept(b, sha) == b
        for bad in (b + b"x", b"", b[:-1]):
            try:
                accept(bad, sha)
            except ValueError:
                pass
            else:
                raise AssertionError("corrupted upstream payload accepted")
    print("CYR UPSTREAM LOCK OK: immutable Git blobs, fail-closed corrupt source")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--out", type=Path)
    p.add_argument("--selftest", action="store_true")
    args = p.parse_args()
    if args.selftest:
        selftest()
        return
    if not args.out:
        p.error("--out is required for fetching pinned sources")
    m = fetch(args.out)
    for code in ("ru", "uk"):
        print(f"{code}: {m['languages'][code]['dic']['bytes']} source bytes, "
              f"pinned LibreOffice {PIN[:12]}, verified")
    print("CYR UPSTREAM FETCH OK: exact RU/UK dictionary, affix, notices")


if __name__ == "__main__":
    main()
