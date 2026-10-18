#!/usr/bin/env python3
"""seed.py - the hardware oracle's answers that `make test` needs, shipped with the repository.

The float checks of tests/run_selftests.py compare the lifted code with a real x86-64 CPU
(tools/hworacle).  Its answers are cached under inventory/hworacle_cache (git-ignored), so a fresh
clone without an x86 machine has none and those checks are inconclusive.  The answers to this
repository's OWN test vectors (the synthetic images of tests/vectors, families `insn-selftest_i`
and `known`) are kept in tests/oracle_seed/<family>/, as ordinary cache segments of at most
1.9 MB each with neutral metadata, and `make test` installs them first.

    python tools/hworacle/seed.py export FAMILY... --out DIR   rewrite those families of the local
                                                              cache as seed segments (the owner)
    python tools/hworacle/seed.py install [--seed DIR]        copy the seed segments that are missing
                                                              into the local cache (make test)
    python tools/hworacle/seed.py selftest

Only families of our own test vectors belong here: never a family of a game module (insn-<module>,
xc-<module>).  tools/publish/check.py lets a binary file under tests/oracle_seed/ through only when
it starts with the segment magic.
"""
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import cache as C  # noqa: E402

SEED = os.path.join(REPO, "tests", "oracle_seed")
OURS = ("insn-selftest_i", "insn-selftest_unasked", "known")     # families of our own test vectors only
MAX_SEGMENT = 1_900_000
META = {"seed": True, "what": "answers of a real x86-64 CPU to this repository's own test vectors"}


def export(families, out):
    for fam in families:
        if fam not in OURS:
            raise SystemExit("seed: %s is not a family of our own test vectors (%s)" % (fam, ", ".join(OURS)))
        src = C.Cache(family=fam)
        src._ensure_loaded()
        items = sorted(src._data.items())
        dst = C.Cache(root=out, family=fam)
        chunk, size, written = [], 0, 0
        for key, value in items:
            need = 32 + 4 + len(value)
            if chunk and size + need > MAX_SEGMENT - 4096:
                dst.put_many(chunk, META); written += 1
                chunk, size = [], 0
            chunk.append((key, value)); size += need
        if chunk:
            dst.put_many(chunk, META); written += 1
        print("seed: %s: %d answers in %d segments" % (fam, len(items), written))
    return 0


def install(seed=SEED, root=None):
    root = C.DEFAULT_ROOT if root is None else root
    copied = 0
    if not os.path.isdir(seed):
        print("seed: no seed directory at %s" % seed)
        return 0
    for fam in sorted(os.listdir(seed)):
        sdir = os.path.join(seed, fam)
        if not os.path.isdir(sdir):
            continue
        ddir = os.path.join(root, fam)
        os.makedirs(ddir, exist_ok=True)
        for name in sorted(os.listdir(sdir)):
            if not (name.startswith("seg-") and name.endswith(".hwc")) or os.path.exists(os.path.join(ddir, name)):
                continue
            tmp = os.path.join(ddir, ".tmp-" + name)       # readers ignore .tmp- files; the move is atomic
            shutil.copyfile(os.path.join(sdir, name), tmp)
            os.replace(tmp, os.path.join(ddir, name))
            copied += 1
    print("seed: %d segment(s) installed into %s" % (copied, root))
    return 0


def selftest():
    with tempfile.TemporaryDirectory() as d:
        live, out, dest = (os.path.join(d, x) for x in ("live", "seed", "dest"))
        values = {bytes([i]) * 32: os.urandom(300) for i in range(1, 200)}
        C.Cache(root=live, family="known").put_many(values, {"host": "somewhere"})
        old = C.DEFAULT_ROOT
        C.DEFAULT_ROOT = live
        try:
            global MAX_SEGMENT
            keep, MAX_SEGMENT = MAX_SEGMENT, 20_000
            export(["known"], out)
        finally:
            C.DEFAULT_ROOT, MAX_SEGMENT = old, keep
        segs = sorted(os.listdir(os.path.join(out, "known")))
        assert len(segs) >= 3 and all(os.path.getsize(os.path.join(out, "known", s)) <= 20_000 for s in segs), segs
        for s in segs:
            assert b"somewhere" not in open(os.path.join(out, "known", s), "rb").read(), "the live metadata is not carried over"
        install(out, dest); install(out, dest)
        got = C.Cache(root=dest, family="known")
        assert len(got) == len(values) and all(got.get(k) == v for k, v in values.items())
        assert len(os.listdir(os.path.join(dest, "known"))) == len(segs), "a second install copies nothing"
        try:
            export(["insn-tier0"], out)
            raise AssertionError("a game module's family was exported")
        except SystemExit:
            pass
    print("seed selftest: ok")
    return 0


def main(argv):
    if argv[:1] == ["selftest"]:
        return selftest()
    if argv[:1] == ["install"]:
        seed = argv[argv.index("--seed") + 1] if "--seed" in argv else SEED
        return install(seed)
    if argv[:1] == ["export"] and "--out" in argv:
        i = argv.index("--out")
        return export(argv[1:i] + argv[i + 2:], argv[i + 1])
    print(__doc__.strip().split("\n\n")[1], file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
