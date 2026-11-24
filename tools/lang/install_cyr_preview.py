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
EXTRA_FILE = "THEME-EXTRAS.json"


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
    if manifest.get("ukrainian_apostrophe_forms_retained", 0) != d["apostrophe_words"]:
        raise ValueError(f"{code}: apostrophe spelling count differs from dictionary")
    t = tiles.verify(assets["tiles.bin"], assets["dict.bin"])
    p = pools.verify(assets["dict.bin"], assets["pools.bin"])
    # Require the actual language's alphabet; both have 33 symbols but NOT
    # the same codepoints, and English/Latin cannot masquerade as Russian.
    cps = struct.unpack_from("<33I", assets["dict.bin"], 20)
    if tuple(dictionary.alphabet(code)) != cps or t["symbols"] != 33:
        raise ValueError(f"{code}: dictionary/tiles do not use the expected Cyrillic alphabet")
    if manifest.get("apostrophe_password_count", 0) != p["apostrophe_words"]:
        raise ValueError(f"{code}: apostrophe password count differs from pools")
    if p["themes"] != 40 or p["words"] < 160 or d["words"] < p["words"]:
        raise ValueError(f"{code}: incomplete dictionary or draft 40-theme pools")
    if (manifest.get("starter_theme_count") != 40 or
            manifest.get("starter_password_count") != 160 or
            manifest.get("retained_word_count") != d["words"]):
        raise ValueError(f"{code}: source counts contradict binary packs")
    extra_count = manifest.get("extra_password_count", 0)
    total = manifest.get("total_password_count", 160)
    if (type(extra_count) is not int or extra_count < 0 or
            type(total) is not int or total != 160 + extra_count or
            total != p["words"]):
        raise ValueError(f"{code}: theme expansion counts do not match WPP2")
    expansion_file = folder / EXTRA_FILE
    if extra_count:
        extras = expansion_file.read_bytes()
        if (manifest.get("theme_expansion_status") != "unreviewed-candidates" or
                not extras or manifest.get("theme_expansion_sha256") != _sha(extras)):
            raise ValueError(f"{code}: theme expansion source has changed")
        info = json.loads(extras.decode("utf-8"))
        if (not isinstance(info, dict) or info.get("language") != code or
                info.get("review_status") != "unreviewed-candidates" or
                not isinstance(info.get("themes"), dict) or
                sum(len(words) for words in info["themes"].values()
                    if isinstance(words, list)) != extra_count):
            raise ValueError(f"{code}: malformed theme expansion provenance")
        assets[EXTRA_FILE] = extras
    elif (expansion_file.exists() or
          manifest.get("theme_expansion_sha256") not in (None, "") or
          manifest.get("theme_expansion_status", "none") != "none"):
        raise ValueError(f"{code}: unexpected theme expansion file/manifest")
    assets["LICENSE-SOURCE.txt"] = notice
    assets["SOURCE-MANIFEST.json"] = manifest_bytes
    return assets, d, p


def install(source, dest, synthetic=False, _test_fail_promotion=False):
    """Validate both source packs, stage them together and preserve old data.

    A complete old installation moves to a sibling backup rather than being
    deleted before the new staged image is ready. Failed promotion restores
    the backup; the next call also recovers an interrupted prior promotion.
    """
    destination = Path(dest)
    origin = Path(source)
    if origin.resolve() == destination.resolve():
        raise ValueError("source and destination cannot be the same directory")
    validated = {}
    for code in ("ru", "uk"):
        validated[code] = _inspect(source, code, synthetic)
    destination.parent.mkdir(parents=True, exist_ok=True)
    stage = destination.parent / (destination.name + ".installing")
    backup = destination.parent / (destination.name + ".previous")
    # Recover a previously interrupted two-rename promotion. Never discard
    # the only available copy of a user's prior preview packs.
    if backup.exists():
        if not destination.exists():
            backup.rename(destination)
        else:
            # Both directories exist if the new stage was promoted before a
            # crash during backup cleanup. Keep the newly installed image
            # only if BOTH languages and attribution are still valid.
            try:
                for code in ("ru", "uk"):
                    _inspect(destination, code, synthetic)
            except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError):
                shutil.rmtree(destination)
                backup.rename(destination)
            else:
                shutil.rmtree(backup)
    if stage.exists():
        shutil.rmtree(stage)
    try:
        for code, (assets, _, _) in validated.items():
            folder = stage / code
            folder.mkdir(parents=True, exist_ok=True)
            for filename, payload in assets.items():
                (folder / filename).write_bytes(payload)
        old_exists = destination.exists()
        if old_exists:
            destination.rename(backup)
        try:
            if _test_fail_promotion:
                raise OSError("injected staging failure for rollback test")
            stage.rename(destination)
        except BaseException:
            # Directory rename cannot expose a half-written pack. If it
            # failed, restore the old complete directory before rethrowing.
            if old_exists and backup.exists() and not destination.exists():
                backup.rename(destination)
            raise
    finally:
        if stage.exists():
            shutil.rmtree(stage)
    if backup.exists():
        shutil.rmtree(backup)
    for code, (_, d, p) in validated.items():
        print(f"CYR IMPORTED PREVIEW {code}: {d['words']} sourced words, "
              f"{p['themes']} themes, {p['words']} unreviewed candidate passwords "
              "(NOT a certified playable locale)")


def selftest(folder):
    from import_cyr_hunspell import selftest as import_selftest
    root = Path(folder)
    source = root / "intake"
    import_selftest(source)
    dest = root / "installed"
    install(source, dest, synthetic=True)
    for code in ("ru", "uk"):
        for file in (*RESOURCE_MAX, *META_FILES, EXTRA_FILE):
            assert (dest / code / file).read_bytes() == (source / code / file).read_bytes()
    before = (dest / "ru" / "dict.bin").read_bytes()
    # Exercise failure after old destination was moved to a recovery backup:
    # previous RU and UK data must both remain present and no partial stage
    # may survive. This is a real file-system rollback test, not a mock pass.
    try:
        install(source, dest, synthetic=True, _test_fail_promotion=True)
    except OSError:
        pass
    else:
        raise AssertionError("injected failed pack promotion unexpectedly succeeded")
    assert (dest / "ru" / "dict.bin").read_bytes() == before
    assert (dest / "uk" / EXTRA_FILE).is_file()
    assert not (root / "installed.installing").exists()
    assert not (root / "installed.previous").exists()
    # Simulate interruption after moving a valid installation to backup.
    backup = root / "installed.previous"
    dest.rename(backup)
    install(source, dest, synthetic=True)
    assert (dest / "ru" / "dict.bin").read_bytes() == before
    assert not backup.exists(), "recovery must clean up stale backup"
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
    # A changed editorial-candidate file must not install even if its
    # dictionary and pool binaries remain otherwise valid.
    extra_path = source / "uk" / EXTRA_FILE
    original_extra = extra_path.read_bytes()
    extra_path.write_bytes(original_extra + b" ")
    try:
        try:
            install(source, dest, synthetic=True)
        except ValueError:
            pass
        else:
            raise AssertionError("tampered thematic candidate attribution accepted")
        assert (dest / "ru" / "dict.bin").read_bytes() == before
    finally:
        extra_path.write_bytes(original_extra)
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
