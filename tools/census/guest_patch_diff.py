#!/usr/bin/env python3
"""Does the lifted C with the guest patches differ from the lifted C without them only at the patches?

    make gate3-build && make gate3-build GUEST_PATCHES=off
    .venv/bin/python tools/census/guest_patch_diff.py build/gate3/lifted build/gate3-unpatched/lifted
    .venv/bin/python tools/census/guest_patch_diff.py lifted build/lifted-unpatched        # the Gate 2 pair

Each argument is a directory with one subdirectory (or link) per module, holding what the
lifter wrote for it: the first lifted with the guest patches (lifter/guest_patches.py), the
second with --no-patches.  For every module in both, all of this must hold:

  * the same files; every file but the report byte-identical, except C files in which the
    only lines that differ are the one line after the comment of an instruction that the
    patched report lists as a patch, each listed patch once;
  * at such a line the unpatched C is the lifter's translation of a 64-bit load into a
    register, `W64(b, LD64(..))`, and the patched C is `GUEST PATCH ..` and `W64(b, R64(a))`:
    the same destination b, and a is the register that the load at the patch's `reuse`
    address, a few lines up in the same function and the same in both files, writes;
  * the two reports agree in everything but the time taken, the digests of the C files that
    differ, and what they say about patches: the unpatched one says the patches were off,
    the patched one lists them in the summary and in the records of their functions.

Reads lifted C, which is derived from the game: prints module names, rvas and counts, never a
line of it.  Exit status 0 only if every module passes and at least one patch was seen.
"""

import json
import os
import re
import sys

LOAD = re.compile(r"^\{ uint64_t m1 = \(.*\); W64\((\d+), LD64\(m1\)\); \}$")
COPY = re.compile(r"^\{ /\* GUEST PATCH (\S+) \(lifter/guest_patches\.toml\): takes the value loaded at 0x([0-9a-f]+); "
                  r"the game's instruction reads the memory again \*/ W64\((\d+), R64\((\d+)\)\); \}$")
INSN = re.compile(r"^/\* ([0-9a-f]+): .* \*/$")


def read_report(d, mod):
    with open(os.path.join(d, mod + ".report.json")) as f:
        return json.load(f)


def strip_patch_keys(report):
    """The report without what may differ between the two lifts."""
    r = dict(report, summary=dict(report["summary"]), functions=[dict(f) for f in report["functions"]])
    r["summary"].pop("seconds", None)
    r["summary"].pop("guest_patches", None)
    r.pop("generated", None)
    for f in r["functions"]:
        f.pop("guest_patches", None)
    return r


def compare(mod, pd, ud):
    """-> (number of patches seen, [problems])."""
    bad = []
    pr, ur = read_report(pd, mod), read_report(ud, mod)
    gp, gu = pr["summary"].get("guest_patches", {}), ur["summary"].get("guest_patches", {})
    if gp.get("enabled") is not True:
        bad.append("the first directory's report does not say it was lifted with patches")
    if gu.get("enabled") is not False or gu.get("applied"):
        bad.append("the second directory's report does not say it was lifted without patches")
    applied = {int(r["at"], 16): r for r in gp.get("applied", [])}
    in_funcs = {int(x["at"], 16) for f in pr["functions"] for x in f.get("guest_patches", [])}
    if in_funcs != set(applied):
        bad.append("the summary's patches and the function records' patches are not the same set")
    if any("guest_patches" in f for f in ur["functions"]):
        bad.append("a function record of the unpatched lift lists a patch")
    if strip_patch_keys(pr) != strip_patch_keys(ur):
        bad.append("the reports differ in more than the patches, the time and the digests")
    pf, uf = sorted(os.listdir(pd)), sorted(os.listdir(ud))
    if pf != uf:
        bad.append("the two directories do not hold the same files")
    seen, differing = set(), []
    for name in pf:
        if name.endswith(".report.json") or name not in uf:
            continue
        with open(os.path.join(pd, name), "rb") as f:
            a = f.read()
        with open(os.path.join(ud, name), "rb") as f:
            b = f.read()
        if a == b:
            continue
        differing.append(name)
        la, lb = a.decode(errors="replace").split("\n"), b.decode(errors="replace").split("\n")
        if not name.endswith(".c") or len(la) != len(lb):
            bad.append("%s differs and is not a C file with the same number of lines" % name)
            continue
        where = {}                                  # rva -> line index of the instruction's comment, patched file
        for i, ln in enumerate(la):
            if ln != lb[i]:
                m = INSN.match(la[i - 1].strip()) if la[i - 1] == lb[i - 1] else None
                at = int(m.group(1), 16) if m else None
                copy, load = COPY.match(ln.strip()), LOAD.match(lb[i].strip())
                if at is None or at not in applied or at in seen:
                    bad.append("%s: a line differs that is not the line of a listed patch, or of one already seen" % name)
                    continue
                seen.add(at)
                rec = applied[at]
                if not (copy and load and copy.group(3) == load.group(1) and copy.group(1) == rec["fix"]
                        and int(copy.group(2), 16) == int(rec["reuse"], 16)):
                    bad.append("%s, patch at 0x%x: the two lines are not a load and its replacement by a copy into the same register" % (name, at))
                    continue
                if not where:
                    where = {int(m.group(1), 16): k for k, x in enumerate(la) for m in [INSN.match(x.strip())] if m}
                k = where.get(int(rec["reuse"], 16))
                first = LOAD.match(la[k + 1].strip()) if k is not None and k < i and i - k < 40 else None
                if not (first and first.group(1) == copy.group(4) and la[k + 1] == lb[k + 1]):
                    bad.append("%s, patch at 0x%x: the copied register is not the one the load at %s writes" % (name, at, rec["reuse"]))
    if seen != set(applied):
        bad.append("patches the report lists but the C does not show: %s" % ", ".join("0x%x" % a for a in sorted(set(applied) - seen)))
    digests = {n for n in pr.get("generated", {}) if pr["generated"][n] != ur.get("generated", {}).get(n)}
    if digests != set(differing):
        bad.append("the files whose digests differ in the reports are not the files that differ")
    print("%-20s %4d files, %d differ; %d patches listed, %d lines differ, each a load replaced by a copy of the first load's "
          "register: %s%s" % (mod, len(pf), len(differing), len(applied), len(seen),
                              ", ".join("0x%x" % a for a in sorted(seen)) or "-", "".join("\n    PROBLEM: " + b for b in bad)))
    return len(seen), bad


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    pdir, udir = sys.argv[1:]
    def mods(d):
        return {m for m in os.listdir(d) if os.path.isfile(os.path.join(d, m, m + ".report.json"))}
    both = sorted(mods(pdir) & mods(udir))
    only = sorted(mods(pdir) ^ mods(udir))
    total, failed = 0, 0
    for m in both:
        n, bad = compare(m, os.path.join(pdir, m), os.path.join(udir, m))
        total += n
        failed += bool(bad)
    ok = both and not failed and not only and total > 0
    print("%d modules compared, %d patches; %s%s" % (len(both), total, "the patched C differs from the unpatched only by the reuse at each patch"
                                                      if ok else "NOT as it must be", "; in one directory only: " + " ".join(only) if only else ""))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
