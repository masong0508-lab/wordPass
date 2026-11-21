#!/usr/bin/env python3
"""Install *externally sourced* Russian/Ukrainian packs into an opt-in DS ROM.

Use only after the operator has run import_cyr_hunspell.py with source license,
URL and reviewed spelling dictionaries. No network fetches, no automatic
downloads, and never alters the normal 11-language wordPass.nds ROM.

Example (after independently sourcing/licensing each Hunspell pair):
  python3 tools/lang/install_cyr_preview.py \
    --input-root build/cyr-sourced --output-root nitrofiles/cyrpreview

The input root has ru/ and uk/ directories, each with dict.bin, tiles.bin,
pools.bin, SOURCE-MANIFEST.json and LICENSE-SOURCE.txt created by the importer.
Synthetic ~170-word fixtures are only allowed in --selftest mode.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct

import dict_v4 as dictionary
import tiles_v2 as tiles
import pools_v2 as pools

RESOURCE_MAX = {"dict.bin": 1536 * 1024, "tiles.bin": 512, "pools.bin": 256 * 1024}
META_FILES = ("SOURCE-MANIFEST.json", "LICENSE-SOURCE.txt")


def _sha(data):
    return hashlib.sha256(data).hexdigest()


def _inspect(source, code, synthetic=False):
    folder = Path(source) / code
    assets = {}
    for name, maximum in RESOURCE_MAX.items():
        p = folder / name
        payload = p.read_bytes()
        if not payload or len(payload) > maximum:
            raise ValueError(f"{code}/{name}: empty or larger than the preview budget")
        assets[name] = payload
    notice = (folder / "LICENSE-SOURCE.txt").read_bytes()
    manifest_bytes = (folder / "SOURCE-MANIFEST.json").read_bytes()
    manifest = json.loads(manifest_bytes.decode("utf-8"))
    if (not isinstance(manifest, dict) or manifest.get("language") != code or
            not str(manifest.get("status", "")).startswith("EXPERIMENTAL") or
            not str(manifest.get("origin", "")).startswith("https://") or
            len(notice) < 12 or
            manifest.get("license_notice_sha256") != _sha(notice)):
        raise ValueError(f"{code}: origin, status or source license manifest is invalid")
    if not synthetic and ("example.invalid" in manifest["origin"] or
                          manifest.get("retained_word_count", 0) < 1000):
        raise ValueError(f"{code}: synthetic/demo-sized source pack; "
                         "use the standard built-in engineering preview")
    # Source-internal byte counts must match the exact files being installed.
    for filename, key in (("dict.bin", "dictionary_bytes"),
                          ("tiles.bin", "tile_bytes"),
                          ("pools.bin", "pool_bytes")):
        if manifest.get(key) != len(assets[filename]):
            raise ValueError(f"{code}: {filename} byte count differs from manifest")
    d = dictionary.verify(assets["dict.bin"])
    t = tiles.verify(assets["tiles.bin"], assets["dict.bin"])
    p = pools.verify(assets["dict.bin"], assets["pools.bin"])
    # Require the actual language's alphabet; both have 33 symbols but NOT
    # the same codepoints, and English/Latin cannot masquerade as Russian.
    cps = struct.unpack_from("<33I", assets["dict.bin"], 20)
    if tuple(dictionary.alphabet(code)) != cps or t["symbols"] != 33:
        raise ValueError(f"{code}: dictionary/tiles do not use the expected Cyrillic alphabet")
    if p["themes"] != 40 or p["words"] < 160 or d["words"] < p["words"]:
        raise ValueError(f"{code}: incomplete dictionary or draft 40-theme pools")
    if (manifest.get("starter_theme_count") != 40 or
            manifest.get("starter_password_count") != 160 or
            manifest.get("retained_word_count") != d["words"]):
        raise ValueError(f"{code}: source counts contradict binary packs")
    assets["LICENSE-SOURCE.txt"] = notice
    assets["SOURCE-MANIFEST.json"] = manifest_bytes
    return assets, d, p


def install(source, dest, synthetic=False):
    """Validate BOTH languages before publishing either. On failure leave dest
    untouched. Images, original notices and manifests stay paired."""
    validated = {}
    for code in ("ru", "uk"):
        validated[code] = _inspect(source, code, synthetic)
    destination = Path(dest)
    destination.parent.mkdir(parents=True, exist_ok=True)
    stage = destination.parent / (destination.name + ".installing")
    if stage.exists():
        shutil.rmtree(stage)
    try:
        for code, (assets, _, _) in validated.items():
            folder = stage / code
            folder.mkdir(parents=True, exist_ok=True)
            for filename, payload in assets.items():
                (folder / filename).write_bytes(payload)
        # The pack files are immutable inputs to the ROM builder. We publish
        # only after both complete sets exist, keeping half-installed languages
        # out of the preview's folder.
        if destination.exists():
            shutil.rmtree(destination)
        stage.rename(destination)
    finally:
        if stage.exists():
            shutil.rmtree(stage)
    for code, (_, d, p) in validated.items():
        print(f"CYR IMPORTED PREVIEW {code}: {d['words']} sourced words, "
              f"{p['themes']} starter themes, {p['words']} draft passes "
              "(NOT a certified playable locale)")


def selftest(folder):
    from import_cyr_hunspell import selftest as import_selftest
    root = Path(folder)
    source = root / "intake"
    import_selftest(source)
    dest = root / "installed"
    install(source, dest, synthetic=True)
    for code in ("ru", "uk"):
        for file in (*RESOURCE_MAX, *META_FILES):
            assert (dest / code / file).read_bytes() == (source / code / file).read_bytes()
    before = (dest / "ru" / "dict.bin").read_bytes()
    license_path = source / "uk" / "LICENSE-SOURCE.txt"
    original = license_path.read_bytes()
    license_path.write_bytes(original + b"altered")
    try:
        try:
            install(source, dest, synthetic=True)
        except ValueError:
            pass
        else:
            raise AssertionError("tampered license notice was accepted")
        assert (dest / "ru" / "dict.bin").read_bytes() == before
    finally:
        license_path.write_bytes(original)
    # A checksum-free identity attack is also rejected: IDs for Ukrainian
    # and Russian cannot be interchanged even with 33-symbol headers.
    bad_dict = source / "ru" / "dict.bin"
    saved = bad_dict.read_bytes()
    bad_dict.write_bytes((source / "uk" / "dict.bin").read_bytes())
    try:
        try:
            install(source, dest, synthetic=True)
        except ValueError:
            pass
        else:
            raise AssertionError("cross-language dictionary was accepted")
    finally:
        bad_dict.write_bytes(saved)
    assert (dest / "ru" / "dict.bin").read_bytes() == before
    print("CYR PACK INSTALL OK: both languages, attribution, consistency, atomic failure")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input-root")
    parser.add_argument("--output-root")
    parser.add_argument("--selftest")
    args = parser.parse_args()
    if args.selftest:
        selftest(args.selftest)
    elif args.input_root and args.output_root:
        install(args.input_root, args.output_root)
    else:
        parser.error("use --selftest FOLDER or --input-root ROOT --output-root ROOT")


if __name__ == "__main__":
    main()
