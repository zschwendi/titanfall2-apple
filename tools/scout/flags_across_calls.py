#!/usr/bin/env python3
"""Do any of the game's functions read an arithmetic flag that a call or a return left behind?

    .venv/bin/python tools/scout/flags_across_calls.py MODULE.dll [...]
    .venv/bin/python tools/scout/flags_across_calls.py --all

Kinds of site (conservative: a walk that cannot be decided is reported as a site):
  E  a function that reads a flag at its entry before writing it;
  A  code after a call that reads a flag before writing it (the flag comes from the callee
     or from before the call).
One JSON object per module, on one line.  Read-only; OriginSDK.dll is never opened.
All addresses are RVAs.
"""

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/scout")
from flow_lib import get, fmt_imp  # noqa: E402
from capstone import x86_const as X  # noqa: E402

SIX = ["CF", "PF", "AF", "ZF", "SF", "OF"]
BIT = {f: 1 << i for i, f in enumerate(SIX)}
ALL = (1 << 6) - 1
MAIN_MODS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "gate3", "modules.def")
FORBIDDEN = {"originsdk.dll", "titanfall2.exe", "titanfall2_trial.exe"}
MAX_WALK = 80

# Condition suffix -> flags it reads (used for jcc, setcc, cmovcc).
COND = {"o": "OF", "no": "OF", "b": "CF", "c": "CF", "nae": "CF", "nb": "CF", "nc": "CF",
        "ae": "CF", "e": "ZF", "z": "ZF", "ne": "ZF", "nz": "ZF", "be": "CF ZF", "na": "CF ZF",
        "a": "CF ZF", "nbe": "CF ZF", "s": "SF", "ns": "SF", "p": "PF", "pe": "PF", "np": "PF",
        "po": "PF", "l": "SF OF", "nge": "SF OF", "ge": "SF OF", "nl": "SF OF",
        "le": "ZF SF OF", "ng": "ZF SF OF", "g": "ZF SF OF", "nle": "ZF SF OF"}
EXTRA_READ = {"adc": "CF", "sbb": "CF", "rcl": "CF", "rcr": "CF", "cmc": "CF",
              "pushfq": "CF PF AF ZF SF OF", "pushf": "CF PF AF ZF SF OF",
              "pushfd": "CF PF AF ZF SF OF", "lahf": "CF PF AF ZF SF"}
EXTRA_WRITE = {"popfq": "CF PF AF ZF SF OF", "popf": "CF PF AF ZF SF OF",
               "popfd": "CF PF AF ZF SF OF", "sahf": "CF PF AF ZF SF", "cmc": "CF"}
VAR_SHIFT = {"shl", "shr", "sar", "sal", "rol", "ror", "rcl", "rcr", "shld", "shrd"}
ALL_SIX = "CF PF AF ZF SF OF"
FALLBACK_WRITE = {m: ALL_SIX for m in (
    "test", "cmp", "add", "sub", "and", "or", "xor", "neg", "imul", "mul", "div", "idiv",
    "shl", "shr", "sar", "sal", "rol", "ror", "xadd", "cmpxchg", "popcnt", "bsf", "bsr",
    "lzcnt", "tzcnt", "shld", "shrd")}
FALLBACK_WRITE.update({"inc": "PF AF ZF SF OF", "dec": "PF AF ZF SF OF", "bt": "CF",
                       "bts": "CF", "btr": "CF", "btc": "CF"})
LOOPS = {"loop", "loope", "loopne", "jrcxz", "jecxz"}   # branch, read no flag


def mask(names):
    m = 0
    for n in names.split():
        m |= BIT[n]
    return m


def names_of(m):
    return [f for f in SIX if m & BIT[f]]


def cap_flags(ins):
    """(reads, writes) bit masks of the six flags: capstone's eflags plus the mnemonic tables."""
    try:
        e = ins.eflags
    except Exception:
        e = 0
    reads = writes = 0
    for f in SIX:
        if e & (getattr(X, "X86_EFLAGS_TEST_" + f, 0) | getattr(X, "X86_EFLAGS_PRIOR_" + f, 0)):
            reads |= BIT[f]
        if e & (getattr(X, "X86_EFLAGS_MODIFY_" + f, 0) | getattr(X, "X86_EFLAGS_SET_" + f, 0)
                | getattr(X, "X86_EFLAGS_RESET_" + f, 0) | getattr(X, "X86_EFLAGS_UNDEFINED_" + f, 0)):
            writes |= BIT[f]
    mn = ins.mnemonic
    if writes == 0 and not e and mn in FALLBACK_WRITE:
        # capstone reports no flag bits for some flag-setting forms (e.g. test with a memory
        # operand): use the Intel manual's list for the mnemonic
        writes = mask(FALLBACK_WRITE[mn])
    if mn[0] == "j" and mn != "jmp" and mn[1:] in COND:
        reads |= mask(COND[mn[1:]])
    elif mn.startswith("set") and mn[3:] in COND:
        reads |= mask(COND[mn[3:]])
    elif mn.startswith("cmov") and mn[4:] in COND:
        reads |= mask(COND[mn[4:]])
    reads |= mask(EXTRA_READ.get(mn, ""))
    writes |= mask(EXTRA_WRITE.get(mn, ""))
    return reads, writes


def write_is_certain(ins):
    """A shift whose count may be 0 leaves the flags alone: not counted as a write."""
    if ins.mnemonic in VAR_SHIFT and len(ins.operands) >= 2:
        op = ins.operands[1]
        return op.type == X.X86_OP_IMM and (op.imm & 0x1F) != 0
    return True


def callee_name(M, ct):
    if ct is None:
        return "?"
    k, v = ct
    if k == "import":
        return fmt_imp(v)
    if k in ("direct", "gptr"):
        return hex(v)
    if k == "vslot":
        return "vslot %s+%#x" % v
    return "%s:%s" % (k, v)


def analyse(M, func):
    """Walks from the entry and after every call of one function.
    Returns (E sites, A sites, number of calls)."""
    insns = M.insns(func)
    by_addr = {ins.address: ins for ins in insns}
    ranges = M.pieces(func)

    def inside(a):
        return any(b <= a < e for b, e in ranges)

    def walk(start, callee):
        sites = []
        seen = set()
        stack = [(start, 0)]
        while stack:
            addr, written = stack.pop()
            if (addr, written) in seen:
                continue
            seen.add((addr, written))
            a, n = addr, 0
            while n < MAX_WALK:
                ins = by_addr.get(a)
                if ins is None:
                    break
                n += 1
                reads, writes = cap_flags(ins)
                bad = reads & ~written
                if bad:
                    sites.append({"func": hex(func), "at": hex(ins.address),
                                  "mnemonic": ins.mnemonic, "flags": names_of(bad),
                                  "after_call_to": callee})
                    break
                if write_is_certain(ins):
                    written |= writes
                if written == ALL:
                    break
                mn = ins.mnemonic
                op = ins.operands[0] if ins.operands else None
                target = op.imm if op is not None and op.type == X.X86_OP_IMM else None
                if mn in ("ret", "call", "int3", "ud2", "hlt"):
                    break
                if mn == "jmp":
                    if target is not None and inside(target):
                        stack.append((target, written))
                    break
                if (mn[0] == "j" or mn in LOOPS):
                    # conditional: the target and the fall-through are both walked
                    if target is not None and inside(target):
                        stack.append((target, written))
                a = ins.address + ins.size
        return sites

    E = walk(func, None)
    A, ncalls = [], 0
    for ins in insns:
        if ins.mnemonic != "call":
            continue
        ncalls += 1
        A.extend(walk(ins.address + ins.size, callee_name(M, M.call_target(ins))))
    return E, A, ncalls


def run(name):
    M = get(name)
    M._build_pieces()
    out = {"module": M.name, "functions": 0, "calls": 0, "E": [], "A": []}
    seenE, seenA = set(), set()
    for f in sorted(M._pieces.keys()):
        out["functions"] += 1
        E, A, nc = analyse(M, f)
        out["calls"] += nc
        for s in E:
            k = (s["func"], s["at"], tuple(s["flags"]))
            if k not in seenE:
                seenE.add(k)
                out["E"].append(s)
        for s in A:
            k = (s["func"], s["at"], tuple(s["flags"]), s["after_call_to"])
            if k not in seenA:
                seenA.add(k)
                out["A"].append(s)
    return out


def modules_all():
    names = []
    with open(MAIN_MODS) as fh:
        for line in fh:
            f = line.split()
            if line.startswith("#") or len(f) < 2 or not f[1].lower().endswith(".dll"):
                continue
            if f[1].lower() not in FORBIDDEN:
                names.append(f[1])
    return names


def main(argv):
    names = modules_all() if argv == ["--all"] else argv
    for n in names:
        if n.lower() in FORBIDDEN:
            continue
        try:
            res = run(n)
        except SystemExit:
            raise
        except Exception as ex:  # report and carry on with the other modules
            res = {"module": n, "error": "%s: %s" % (type(ex).__name__, ex)}
        print(json.dumps(res), flush=True)


if __name__ == "__main__":
    main(sys.argv[1:])
