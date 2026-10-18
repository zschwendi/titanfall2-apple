#!/usr/bin/env python3
"""Build and run variants of the lifted side for the gap analysis (bench/README.md).

    .venv/bin/python bench/variants.py list
    .venv/bin/python bench/variants.py run O3 restrict -- --rounds 15 --target-ms 3
    .venv/bin/python bench/variants.py gap            # every timing variant, then the report
    .venv/bin/python bench/variants.py mutants        # does the comparison notice a broken translation?

A variant is a copy of the lifter's C output (bench/build/lifted, made by `make bench-build`)
with an optional text transformation, compiled with the variant's flags and linked with the
same driver, the same generated cases and the same native side.  Every variant runs the full
comparison before it is timed, so a transformation that changes behaviour is caught.  Nothing
here touches the lifter or its output; the copies live under bench/build/v/<variant>/.

Transformations (all on the copy):
  restrict   `Ctx *ctx` becomes `Ctx *restrict ctx` in every function definition: the compiler
             may assume that guest memory accesses do not touch the Ctx.  (True by
             construction: the guest never learns the address of its register file.)
  deadflags  the six status flags are overwritten with 0 immediately before every ret, call
             and tail call.  The Windows x64 convention makes them dead there; storing a
             constant makes every earlier flag computation that nobody read a dead store the
             compiler can delete (given `restrict`), at the price of one small store.
  split      one translation unit per function, so that the compiler cannot inline one lifted
             function into another or specialise across them.
  hand:SET   the functions in bench/hand/SET/*.c replace the lifter's version (SET is
             `deadflags`: flag writes nobody reads removed by hand; `locals`: that, and the
             guest registers kept in C locals).  Each replacement names the SHA-256 of the
             lifter's text it was made from and is refused if the lifter's output has changed
             (`variants.py hashes one_calls_1000 ...` prints the current hashes).
"""

import argparse
import concurrent.futures
import glob
import hashlib
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
BB = os.path.join(REPO, "bench", "build")
LIFT_FLAGS = ["-g0", "-fno-strict-aliasing", "-fwrapv", "-I" + os.path.join(REPO, "harness")]
DRIVER_FLAGS = ["-O2", "-g", "-Wall", "-Wextra", "-fno-strict-aliasing", "-I" + os.path.join(REPO, "harness"),
                "-I" + os.path.join(REPO, "bench")]

VARIANTS = {
    # name: (what it is, options)
    "O2": ("the lifter's C as it is, -O2, one translation unit per module", {}),
    "O1": ("-O1", {"opt": "-O1"}),
    "O3": ("-O3", {"opt": "-O3"}),
    "Os": ("-Os", {"opt": "-Os"}),
    "split": ("-O2, one translation unit per function", {"split": True}),
    "restrict": ("-O2, Ctx pointer declared restrict", {"xform": ["restrict"]}),
    "deadflags": ("-O2, restrict, status flags declared dead at ret and call", {"xform": ["restrict", "deadflags"]}),
    "deadflags-O3": ("-O3, restrict, status flags declared dead at ret and call", {"opt": "-O3", "xform": ["restrict", "deadflags"]}),
    "direct": ("-O2, indirect calls dispatched through a direct-mapped table instead of a binary search",
               {"defs": ["-DBENCH_DISPATCH_DIRECT"]}),
    "deadflags-direct": ("-O2, restrict, flags dead at ret and call, direct-mapped dispatch",
                         {"xform": ["restrict", "deadflags"], "defs": ["-DBENCH_DISPATCH_DIRECT"]}),
    "hand-deadflags": ("-O2, dead flag writes removed by hand in three functions (bench/hand/deadflags)",
                       {"xform": ["hand:deadflags"]}),
    "hand-deadflags-restrict": ("-O2, restrict, dead flag writes removed by hand in three functions",
                                {"xform": ["hand:deadflags", "restrict"]}),
    "hand-locals": ("-O2, dead flags removed and guest registers in C locals, by hand, in three functions (bench/hand/locals)",
                    {"xform": ["hand:locals"]}),
}
GAP = ["O2", "hand-locals", "hand-deadflags", "hand-deadflags-restrict", "restrict", "deadflags", "direct",
       "deadflags-direct", "split", "O1", "O3", "deadflags-O3"]

# Deliberately wrong translations, to see whether the comparison notices (name: (pattern, replacement)).
MUTANTS = {
    "sub32-as-add32": (r"\bx_sub32\(", "x_add32("),
    "add64-as-sub64": (r"\bx_add64\(", "x_sub64("),
    "less-as-less-or-equal": (r"\bCC_L\b", "CC_LE"),
    "below-as-below-or-equal": (r"\bCC_B\b", "CC_BE"),
    "equal-as-not-equal": (r"\bCC_E\b", "CC_NE"),
    "sign-as-not-sign": (r"\bCC_S\b", "CC_NS"),
    "store8-dropped": (r"\bST8\(([^;]*)\);", r"(void)(\1);"),
    "load32-as-load16": (r"\bLD32\(", "LD16("),
    "shl32-as-shr32": (r"\bx_shl32\(", "x_shr32("),
    "imul32-low-bit": (r"\bx_imul32\(ctx, ", "1 ^ x_imul32(ctx, "),
    "write32-keeps-upper-half": (r"\bW32\((\d+), ", r"ctx->r[\1] = (ctx->r[\1] & ~UINT64_C(0xFFFFFFFF)) | (uint32_t)("),
    "inc-as-add2": (r"\bx_inc64\(ctx, ([^;]*?)\);", r"x_add64(ctx, \1, 2);"),
}

FUNC_RE = re.compile(r"^void (\w+)\(Ctx \*ctx\) \{\n.*?^\}\n", re.S | re.M)


def sh(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if r.returncode != 0:
        sys.stderr.write("FAILED: %s\n%s\n%s\n" % (" ".join(cmd), r.stdout[-4000:], r.stderr[-4000:]))
        raise SystemExit(2)
    return r


def modules():
    return sorted(os.path.basename(p)[:-len(".stamp")] for p in glob.glob(os.path.join(BB, "lifted", "one_*.stamp")))


def load_hand(which):
    """{function name: (sha256 of the lifter's text it replaces, replacement text)}"""
    out = {}
    for path in sorted(glob.glob(os.path.join(HERE, "hand", which, "*.c"))):
        text = open(path).read()
        for m in re.finditer(r"/\* replaces (\w+), lifter text sha256 ([0-9a-f]{64}) \*/\n(void \1\(Ctx \*ctx\) \{\n.*?^\}\n)",
                             text, re.S | re.M):
            out[m.group(1)] = (m.group(2), m.group(3))
    return out


def xform_restrict(text):
    return text.replace("(Ctx *ctx) {", "(Ctx *restrict ctx) {")


def xform_deadflags(text):
    macro = ("#define BENCH_FLAGS_DEAD() (ctx->cf = ctx->pf = ctx->af = ctx->zf = ctx->sf = ctx->of = 0)\n")
    text = re.sub(r"\b(RET|TAIL_DIRECT|TAIL_INDIRECT|CALL_DIRECT|CALL_INDIRECT)\(", r"BENCH_FLAGS_DEAD(); \1(", text)
    return text.replace("#define BASE ", macro + "#define BASE ", 1)


def xform_hand(text, hand, used, stale):
    def repl(m):
        name = m.group(1)
        if name not in hand:
            return m.group(0)
        digest, new = hand[name]
        if hashlib.sha256(m.group(0).encode()).hexdigest() != digest:
            stale.append(name)
            return m.group(0)
        used.append(name)
        return new
    return FUNC_RE.sub(repl, text)


def build(name, opts, mutant=None, quiet=False):
    """Returns the path of the variant's driver."""
    vdir = os.path.join(BB, "v", name)
    opt = opts.get("opt", "-O2")
    hand = {}
    for x in opts.get("xform", []):
        if x.startswith("hand:"):
            hand = load_hand(x[5:])
    used, stale = [], []
    jobs, objs = [], []
    for mod in modules():
        src_dir = os.path.join(BB, "lifted", mod)
        out_dir = os.path.join(vdir, mod)
        os.makedirs(out_dir, exist_ok=True)
        for old in glob.glob(os.path.join(out_dir, "*")):
            os.remove(old)
        for path in sorted(glob.glob(os.path.join(src_dir, mod + "_*.c"))):
            text = open(path).read()
            base = os.path.basename(path)[:-2]
            is_table = base.endswith("_table")
            if not is_table:
                for x in opts.get("xform", []):
                    if x.startswith("hand:"):
                        text = xform_hand(text, hand, used, stale)
                    elif x == "restrict":
                        text = xform_restrict(text)
                    elif x == "deadflags":
                        text = xform_deadflags(text)
                if mutant:
                    text = re.sub(mutant[0], mutant[1], text)
            pieces = [(base, text)]
            if opts.get("split") and not is_table:
                head = text[:FUNC_RE.search(text).start()]
                pieces = [("%s_f%04d" % (base, i), head + m.group(0)) for i, m in enumerate(FUNC_RE.finditer(text))]
            for pname, ptext in pieces:
                c = os.path.join(out_dir, pname + ".c")
                with open(c, "w") as f:
                    f.write(ptext)
                o = c[:-2] + ".o"
                objs.append(o)
                jobs.append(["clang", opt] + LIFT_FLAGS + ["-I" + src_dir, "-c", c, "-o", o])
    if stale:
        sys.stderr.write("bench/hand: the lifter's output changed for %s; these replacements were NOT applied\n" % ", ".join(stale))
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as ex:
        list(ex.map(sh, jobs))
    driver = os.path.join(vdir, "driver")
    sh(["clang"] + DRIVER_FLAGS + opts.get("defs", []) + [os.path.join(HERE, "driver.c"), os.path.join(BB, "gen", "cases.c")]
       + objs + ["-o", driver])
    if not quiet:
        print("built %s (%s)%s" % (name, VARIANTS[name][0] if name in VARIANTS else "mutant",
                                    "; hand-edited: " + ", ".join(sorted(used)) if used else ""))
    return driver, used


def run(name, driver, extra):
    out = os.path.join(BB, "v", name, "results.json")
    cmd = [driver, "--known", os.path.join(HERE, "known_mismatches.tbl"), "--json", out, "--label", name] + extra
    r = subprocess.run(cmd, cwd=REPO, capture_output=True, text=True)
    with open(os.path.join(BB, "v", name, "run.log"), "w") as f:
        f.write(r.stdout + r.stderr)
    return r.returncode, out, r.stdout


def cmd_run(names, extra):
    worst = 0
    for name in names:
        if name not in VARIANTS:
            raise SystemExit("unknown variant %r (try: list)" % name)
        driver, used = build(name, VARIANTS[name][1])
        code, out, stdout = run(name, driver, extra)
        tail = [l for l in stdout.splitlines() if l.startswith("correctness:") or "all but probe" in l]
        print("  %s: exit %d\n    %s" % (name, code, "\n    ".join(tail)))
        if used:
            data = json.load(open(out))
            data["hand_edited"] = sorted(used)
            json.dump(data, open(out, "w"), indent=1)
        worst = max(worst, code)
    return worst


def cmd_mutants(extra):
    """For each mutation: the functions whose lifted text changed (directly; the mutation of a
    callee is credited to the callee only) against the functions the comparison flagged."""
    base_driver, _ = build("O2", {}, quiet=True)
    code, out, _ = run("O2", base_driver, ["--cases", "3000"] + extra)
    base = {(f["file"], f["name"]): f for f in json.load(open(out))["functions"]}
    originals = {}
    for mod in modules():
        for path in sorted(glob.glob(os.path.join(BB, "lifted", mod, mod + "_[0-9]*.c"))):
            for m in FUNC_RE.finditer(open(path).read()):
                originals[m.group(1)] = m.group(0)
    rows = []
    for mname, mutant in MUTANTS.items():
        changed = {n for n, t in originals.items() if re.sub(mutant[0], mutant[1], t) != t}
        driver, _ = build("mutant-" + mname, {}, mutant=mutant, quiet=True)
        code, out, _ = run("mutant-" + mname, driver, ["--cases", "3000"] + extra)
        res = json.load(open(out))["functions"]
        described_changed, caught, missed = 0, 0, []
        also = 0
        for f in res:
            key = (f["file"], f["name"])
            if base[key]["status"] != "pass":
                continue
            lifted_name = "one_%s_%x" % (f["file"], int(f["rva"], 16))
            if lifted_name in changed:
                described_changed += 1
                if f["status"] == "MISMATCH":
                    caught += 1
                else:
                    missed.append("%s/%s" % key)
            elif f["status"] == "MISMATCH":
                also += 1
        rows.append((mname, described_changed, caught, also, missed))
        print("%-28s functions changed %3d, caught %3d, missed %3d%s" % (
            mname, described_changed, caught, len(missed), ": " + " ".join(missed[:12]) if missed else ""))
    with open(os.path.join(BB, "mutants.json"), "w") as f:
        json.dump([{"mutant": r[0], "changed": r[1], "caught": r[2], "caught_through_a_callee": r[3], "missed": r[4]}
                   for r in rows], f, indent=1)
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=["list", "run", "gap", "mutants", "hashes"])
    ap.add_argument("names", nargs="*", help="variants to run; arguments after -- go to the driver")
    argv, extra = sys.argv[1:], []
    if "--" in argv:
        argv, extra = argv[:argv.index("--")], argv[argv.index("--") + 1:]
    args = ap.parse_args(argv)
    if not modules():
        raise SystemExit("run `make bench-build` first")
    if args.command == "list":
        for n, (what, _) in VARIANTS.items():
            print("%-14s %s" % (n, what))
        return 0
    if args.command == "run":
        return cmd_run(args.names, extra)
    if args.command == "hashes":
        for mod in modules():
            for path in sorted(glob.glob(os.path.join(BB, "lifted", mod, mod + "_[0-9]*.c"))):
                for m in FUNC_RE.finditer(open(path).read()):
                    if m.group(1) in args.names:
                        print("/* replaces %s, lifter text sha256 %s */" % (m.group(1), hashlib.sha256(m.group(0).encode()).hexdigest()))
        return 0
    if args.command == "gap":
        return cmd_run(GAP, extra or ["--cases", "1000", "--rounds", "15", "--max-rounds", "60", "--target-ms", "0.5"])
    return cmd_mutants(extra)


if __name__ == "__main__":
    sys.exit(main())
