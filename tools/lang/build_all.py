#!/usr/bin/env python3
"""Build data/lang/<code>/{dict.bin,pools.bin,letters.bin} and data/lang/manifest.txt.

    build_all.py RAW_DIR WORK_DIR [CODE ...] [--skip-words] [--skip-pools]
    build_all.py --manifest

RAW_DIR holds the downloads (tools/lang/fetch.sh), WORK_DIR the intermediate lists (not committed).
Steps per language: build_words.py (validation list), gen_dict.py build (WPD3, tier from the top 30k
of the frequency list), letters.py (WPL1), build_pools.py (WPP1); then the manifest for all languages.
"""
import hashlib, os, shutil, struct, sys
import common
import build_words, build_pools, letters, gen_dict

def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()

def manifest(root):
    lines = ["# wordPass languages: written by tools/lang/build_all.py, read by tools/check/lang_test.c.",
             "# code<TAB>native name<TAB>English name<TAB>words<TAB>themes<TAB>pool words<TAB>qu<TAB>dict bytes<TAB>pools bytes<TAB>letters bytes",
             "# en is built into the ROM; its three files are the ones in data/ and their sha256 pins them (sha256 dict pools variants)."]
    ddir = os.path.join(root, "data")
    lines.append("en\tENGLISH\tEnglish\t-\t40\t-\t1\t%d\t%d\t-" % (os.path.getsize(os.path.join(ddir, "dict.bin")),
                                                         os.path.getsize(os.path.join(ddir, "pools.bin"))))
    lines.append("#sha256 en %s %s %s" % tuple(sha(os.path.join(ddir, f)) for f in ("dict.bin", "pools.bin", "variants.bin")))
    total = 0
    for code, native, english, _ in common.LANGS:
        d = os.path.join(ddir, "lang", code)
        if not os.path.exists(os.path.join(d, "letters.bin")):
            continue
        dict_ = open(os.path.join(d, "dict.bin"), "rb").read()
        pools = open(os.path.join(d, "pools.bin"), "rb").read()
        let = letters.parse(open(os.path.join(d, "letters.bin"), "rb").read())
        counts = struct.unpack_from("<18I", dict_, 4)
        themes = struct.unpack_from("<I", pools, 4)[0]
        pw = 0
        for t in range(themes):
            pw += sum(struct.unpack_from("<6H", pools, 8 + 20 * t + 8))
        sizes = [len(dict_), len(pools), 68]
        total += sum(sizes)
        lines.append("\t".join([code, native, english, str(sum(counts)), str(themes), str(pw), str(let["qu"])] + [str(s) for s in sizes]))
    lines.append(f"# total {total} bytes in data/lang")
    open(os.path.join(ddir, "lang", "manifest.txt"), "w").write("\n".join(lines) + "\n")
    print("\n".join(lines))

def main():
    if "--manifest" in sys.argv:        # only rewrite data/lang/manifest.txt (also re-pins the English files)
        manifest(common.ROOT)
        return
    a = [x for x in sys.argv[1:] if not x.startswith("--")]
    raw, work, codes = a[0], a[1], a[2:] or common.CODES
    for code in codes:
        out = os.path.join(common.ROOT, "data", "lang", code)
        os.makedirs(out, exist_ok=True)
        if "--skip-words" not in sys.argv:
            build_words.build(code, raw, work)
        gen_dict.build(os.path.join(work, code, "words.txt"), os.path.join(work, code, "dict.bin"),
                       os.path.join(work, code, "common.txt"))
        gen_dict.verify(os.path.join(work, code, "dict.bin"), os.path.join(work, code, "words.txt"),
                        os.path.join(work, code, "common.txt"))
        shutil.copy(os.path.join(work, code, "dict.bin"), os.path.join(out, "dict.bin"))
        letters.build(code, work, os.path.join(out, "letters.bin"))
        if "--skip-pools" not in sys.argv:
            build_pools.build(code, raw, work, os.path.join(out, "pools.bin"))
    manifest(common.ROOT)

if __name__ == "__main__":
    main()
