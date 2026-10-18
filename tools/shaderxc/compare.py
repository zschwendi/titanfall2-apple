#!/usr/bin/env python3
"""compare.py DATA [--n N] [--jobs J] [--seed S]: the game's pixel shaders translated two ways, counted.

For N pixel shaders of the recording (DATA = build/trace/data; a seeded sample of shaders.tsv's `ps` rows):
  DXMT:   third_party/dxmt/build-native/src/airconv/darwin/airconv BLOB -S        (DXMT's own AIR, LLVM text)
  HLSLcc: build/shaderxc/hlslcc2msl BLOB -> Metal source -> xcrun metal -O2 -S -emit-llvm   (Apple's compiler)
and counts, in the bodies of the defined functions, the instructions: all, floating-point arithmetic,
texture samples, loads and stores.  Writes one row a shader to build/shaderxc/compare.tsv and prints the
totals only.  NOTHING OF A SHADER (bytes, source, IR text) IS PRINTED: the work files are deleted.
"""
import os, random, re, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
AIRCONV = os.path.join(REPO, "third_party/dxmt/build-native/src/airconv/darwin/airconv")
H2M = os.path.join(REPO, "build/shaderxc/hlslcc2msl")
FP = re.compile(r"=\s*(?:\w+\s+)*(fadd|fsub|fmul|fdiv|frem|fneg)\b|call .*@(?:llvm|air)\.(?:fast_)?fma\b")
SAMPLE = re.compile(r"call .*@air\.(?:sample|gather|read)_texture")
LOAD = re.compile(r"=\s*load\b")
STORE = re.compile(r"^\s*store\b")


def count(ll):
    n = fp = sm = ld = st = 0
    inside = False
    for line in open(ll, errors="replace"):
        if line.startswith("define "):
            inside = True
            continue
        if inside and line.startswith("}"):
            inside = False
            continue
        if not inside or not line.startswith("  ") or line.strip().endswith(":"):
            continue
        n += 1
        fp += bool(FP.search(line))
        sm += bool(SAMPLE.search(line))
        ld += bool(LOAD.search(line))
        st += bool(STORE.search(line))
    return n, fp, sm, ld, st


def one(data, h):
    blob = os.path.join(data, "blobs", h[:2], h)
    with tempfile.TemporaryDirectory() as d:
        a, m, b = os.path.join(d, "a.ll"), os.path.join(d, "s.metal"), os.path.join(d, "b.ll")
        r = subprocess.run([AIRCONV, blob, "-S", "-o", a], capture_output=True)
        if r.returncode or not os.path.exists(a):
            return h, None, None, "dxmt-failed"
        ca = count(a)
        r = subprocess.run([H2M, blob, m], capture_output=True)
        if r.returncode:
            return h, ca, None, "hlslcc-failed"
        r = subprocess.run(["xcrun", "metal", "-O2", "-std=metal3.0", "-S", "-emit-llvm", m, "-o", b], capture_output=True)
        if r.returncode or not os.path.exists(b):
            return h, ca, None, "metal-failed"
        return h, ca, count(b), "ok"


def main():
    args = sys.argv[1:]
    data = args[0]
    opt = lambda k, d: int(args[args.index(k) + 1]) if k in args else d
    n, jobs, seed = opt("--n", 300), opt("--jobs", 2), opt("--seed", 1)
    ps = [l.split("\t")[0] for l in open(os.path.join(data, "shaders.tsv")) if l.split("\t")[2:3] == ["ps"]]
    random.Random(seed).shuffle(ps)
    ps = ps[:n]
    out = os.path.join(REPO, "build/shaderxc/compare.tsv")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    rows = []
    with ThreadPoolExecutor(jobs) as ex:
        for h, ca, cb, st in ex.map(lambda h: one(data, h), ps):
            rows.append((h, ca, cb, st))
    with open(out, "w") as f:
        f.write("hash\tstatus\tdxmt_all\tdxmt_fp\tdxmt_sample\tdxmt_load\tdxmt_store\thlslcc_all\thlslcc_fp\thlslcc_sample\thlslcc_load\thlslcc_store\n")
        for h, ca, cb, st in rows:
            f.write("\t".join([h[:16], st] + [str(x) for x in (ca or ("",) * 5)] + [str(x) for x in (cb or ("",) * 5)]) + "\n")
    ok = [(ca, cb) for _, ca, cb, st in rows if st == "ok"]
    from collections import Counter
    print("shaders: %d sampled, %s" % (len(rows), dict(Counter(st for *_, st in rows))))
    if ok:
        names = ("all", "fp", "sample", "load", "store")
        for i, nm in enumerate(names):
            sa, sb = sum(a[i] for a, _ in ok), sum(b[i] for _, b in ok)
            ratios = sorted(b[i] / a[i] for a, b in ok if a[i])
            med = ratios[len(ratios) // 2] if ratios else float("nan")
            print("%-6s DXMT %8d  HLSLcc+Apple %8d  ratio of sums %.3f  median ratio %.3f" % (nm, sa, sb, sb / sa if sa else float("nan"), med))
    print("rows: " + out)


if __name__ == "__main__":
    main()
