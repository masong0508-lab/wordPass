#!/usr/bin/env python3
"""Create and verify a deterministic, clearly labeled WordPass ROM-set ZIP.

The two DS builds remain separate inside the archive. Only the retail image
is intended as the regular 11-language release; Russian/Ukrainian are gated
to the independent experimental Cyrillic preview ROM.

Examples:
  python3 tools/check/package_roms.py --selftest
  python3 tools/check/package_roms.py --retail wordPass-retail.nds \
      --preview wordPass-cyr-preview.nds --output wordPass-ROM-set.zip \
      --commit "$GITHUB_SHA"
  python3 tools/check/package_roms.py --verify wordPass-ROM-set.zip
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]
ROM_NAMES = ("wordPass.nds", "wordPass-cyr-preview.nds")
DOCS = (
    "LICENSE",
    "NOTICE",
    "data/NOTICE",
    "data/lang/NOTICE",
    "LICENSES/GPL-2.0.txt",
    "LICENSES/WordNet-3.0.txt",
)
STAMP = (1980, 1, 1, 0, 0, 0)
HOW_TO = """WORDPASS — COMPLETE NINTENDO DS HOMEBREW ROM SET

wordPass.nds
  Regular retail-style homebrew game, with English and ten other playable
  Latin-alphabet languages. This is the normal release to try first.

wordPass-cyr-preview.nds
  Separate experimental Russian/Ukrainian engineering preview. It is NOT
  a finished translation or a replacement for the regular game. The
  Russian/Ukrainian demo does not yet have full retail feature parity.
  Use separate emulator/storage folders for the experimental preview.

PLAY
  Connect 3+ touching letter gems into words. Press A or touch SUBMIT to
  score a word and earn a numbered clue at its final gem. Hold L or R to
  inspect/peel; find all hidden password letters to clear the shelf.
  SELECT demonstrates a playable word if hints remain. The in-game guide
  explains deduction, burning shelves, and the Daily mode.

INSTALL
  Load a .nds in an NDS-compatible homebrew emulator or compatible hardware.
  Saving requires working FAT/storage support. Real hardware performance,
  flashcart support and compatibility vary; check the game's TESTING.md and
  GitHub build logs before treating a development build as final.
  Back up existing wordPass.cfg saves before trying experimental builds.

INTEGRITY AND LICENSES
  MANIFEST.json and SHA256SUMS.txt record the uncompressed ROM hashes.
  The accompanying .sha256 file (outside the ZIP) verifies the entire ZIP.
  Third-party data and license notices are included inside the archive.
  Repository: https://github.com/masong0508-lab/wordPass
"""


def digest(data):
    return hashlib.sha256(data).hexdigest()


def archive_member(name, data):
    info = zipfile.ZipInfo(name, STAMP)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o644 << 16
    return info, data


def verify(path):
    """Prove archive membership, both uncompressed hashes and source notices."""
    with zipfile.ZipFile(path, "r") as z:
        names = z.namelist()
        expected = set(ROM_NAMES) | set(DOCS) | {
            "HOW-TO-PLAY.txt", "MANIFEST.json", "SHA256SUMS.txt"
        }
        if set(names) != expected or len(names) != len(expected):
            raise ValueError("ROM bundle is missing an expected member or has duplicates")
        for name in names:
            if name.startswith("/") or ".." in Path(name).parts:
                raise ValueError("unsafe ZIP entry")
            if z.getinfo(name).date_time != STAMP:
                raise ValueError("non-deterministic timestamp")
            if z.getinfo(name).file_size == 0:
                raise ValueError("empty ZIP entry")
        manifest = json.loads(z.read("MANIFEST.json").decode("utf-8"))
        if manifest.get("format") != "wordPass-ROM-set-v1":
            raise ValueError("unrecognized manifest")
        if set(manifest.get("roms", {})) != set(ROM_NAMES):
            raise ValueError("missing ROM manifest entry")
        actual = []
        for name in ROM_NAMES:
            blob = z.read(name)
            rec = manifest["roms"][name]
            if len(blob) < 512 or len(blob) != rec.get("bytes") or digest(blob) != rec.get("sha256"):
                raise ValueError("ROM size/hash verification failed for " + name)
            actual.append(f"{digest(blob)}  {name}")
        expected_sum = "\n".join(actual) + "\n"
        if z.read("SHA256SUMS.txt").decode("ascii") != expected_sum:
            raise ValueError("SHA256SUMS does not match uncompressed ROMs")
        if z.read(ROM_NAMES[0]) == z.read(ROM_NAMES[1]):
            raise ValueError("retail and experimental preview are identical")
    return manifest


def create(retail, preview, output, commit, root=ROOT):
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("commit must be a full lowercase 40-character Git SHA")
    blobs = {}
    for name, path in zip(ROM_NAMES, (retail, preview)):
        blob = path.read_bytes()
        if len(blob) < 512:
            raise ValueError(name + " is too small for a Nintendo DS ROM")
        blobs[name] = blob
    if blobs[ROM_NAMES[0]] == blobs[ROM_NAMES[1]]:
        raise ValueError("retail and experimental images must not be identical")
    for name in DOCS:
        data = (root / name).read_bytes()
        if not data:
            raise ValueError("missing/empty license or notice: " + name)
        blobs[name] = data
    manifest = {
        "format": "wordPass-ROM-set-v1",
        "source_commit": commit,
        "roms": {
            "wordPass.nds": {
                "role": "retail-11-language",
                "bytes": len(blobs[ROM_NAMES[0]]),
                "sha256": digest(blobs[ROM_NAMES[0]]),
            },
            "wordPass-cyr-preview.nds": {
                "role": "experimental-RU-UK-engineering-preview",
                "bytes": len(blobs[ROM_NAMES[1]]),
                "sha256": digest(blobs[ROM_NAMES[1]]),
            },
        },
    }
    blobs["HOW-TO-PLAY.txt"] = HOW_TO.encode("utf-8")
    blobs["MANIFEST.json"] = (json.dumps(manifest, sort_keys=True, indent=2) + "\n").encode("utf-8")
    blobs["SHA256SUMS.txt"] = (
        "".join(f"{digest(blobs[name])}  {name}\n" for name in ROM_NAMES)
    ).encode("ascii")
    output.parent.mkdir(parents=True, exist_ok=True)
    # Stable member order and timestamps keep byte-for-byte identical outputs.
    with zipfile.ZipFile(output, "w") as z:
        for name in (*ROM_NAMES, "HOW-TO-PLAY.txt", "MANIFEST.json",
                     "SHA256SUMS.txt", *DOCS):
            info, data = archive_member(name, blobs[name])
            z.writestr(info, data, compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
    verified = verify(output)
    if verified != manifest:
        raise ValueError("generated archive manifest changed on readback")
    checksum_file = output.with_suffix(".sha256")
    checksum_file.write_text(f"{digest(output.read_bytes())}  {output.name}\n",
                             encoding="ascii")
    return output, checksum_file


def selftest():
    with tempfile.TemporaryDirectory(prefix="wordpass-rom-bundle-") as folder:
        root = Path(folder)
        for name in DOCS:
            dst = root / name
            dst.parent.mkdir(parents=True, exist_ok=True)
            dst.write_text("Test notice " + name + "\n", encoding="utf-8")
        retail, preview = root / "normal.nds", root / "preview.nds"
        retail.write_bytes(b"RETAIL" + bytes(1024))
        preview.write_bytes(b"PREVIEW" + bytes(1024))
        one, _ = create(retail, preview, root / "a" / "roms.zip", "a" * 40, root)
        two, _ = create(retail, preview, root / "b" / "roms.zip", "a" * 40, root)
        assert one.read_bytes() == two.read_bytes(), "archive is not reproducible"
        assert verify(one)["roms"]["wordPass.nds"]["role"] == "retail-11-language"
        with zipfile.ZipFile(one, "a") as z:
            z.writestr("extra.txt", "unsolicited")
        try:
            verify(one)
        except ValueError:
            pass
        else:
            raise AssertionError("unexpected member passed bundle verification")
        try:
            create(retail, retail, root / "bad.zip", "a" * 40, root)
        except ValueError:
            pass
        else:
            raise AssertionError("identical ROM builds passed bundle verification")
    print("ROM BUNDLE SELFTEST OK: deterministic, labeled, verified, rejects errors")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--retail", type=Path)
    p.add_argument("--preview", type=Path)
    p.add_argument("--output", type=Path)
    p.add_argument("--commit")
    p.add_argument("--verify", type=Path, help="check existing ZIP and print the manifest")
    p.add_argument("--selftest", action="store_true")
    args = p.parse_args()
    if args.selftest:
        selftest()
        return
    if args.verify:
        print(json.dumps(verify(args.verify), indent=2))
        print("ROM BUNDLE VERIFIED")
        return
    if not all((args.retail, args.preview, args.output, args.commit)):
        p.error("creation requires --retail, --preview, --output and --commit")
    archive, checksum = create(args.retail, args.preview, args.output, args.commit)
    print(f"ROM BUNDLE OK: {archive} and {checksum}")


if __name__ == "__main__":
    main()
