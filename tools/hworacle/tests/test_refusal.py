#!/usr/bin/env python3
"""Tests for the byte inspection in tools/hworacle/runner.c: hwo_decode, hwo_check_program, hwo_sha256.

    cd <repo> && .venv/bin/python tools/hworacle/tests/test_refusal.py

The test compiles runner.c with -DHWO_CHECK_TOOL into build/hworacle/refuse_check (and fails on any compiler
warning), then drives it in bulk (thousands of lines per subprocess call) and compares it with

  * two hand-written tables (must-refuse with the expected reason, must-allow with exact length and flags),
  * Capstone, on the project's own vectors (tests/vectors.py), on several hundred thousand systematic and
    random byte strings, and on the executable sections of the game DLLs and the test corpus when present,
  * hashlib (SHA-256) and hand-written programs (the static check of free-running programs).

The decoder is an allow-list.  An encoding it ALLOWS that policy says must be refused, or whose length, class or
flags are wrong, is a DANGEROUS finding and fails the test.  An ordinary encoding it refuses is only listed.

Game DLL bytes are only read at run time and never written anywhere; failure messages about game DLLs name the
module and RVA instead of printing bytes (set HWO_SHOW_GAME_BYTES=1 to print them to the terminal).
"""
import collections
import hashlib
import os
import random
import re
import struct
import subprocess
import sys
import time
import unittest
from pathlib import Path

try:
    import capstone
    import capstone.x86 as cx86
except ImportError:                                   # pragma: no cover
    sys.exit("capstone is required: run this with the repository's .venv/bin/python")

HERE = Path(__file__).resolve().parent                # <root>/tools/hworacle/tests
ROOT = HERE.parents[2]
SRC = ROOT / "tools" / "hworacle" / "runner.c"
EXE = ROOT / "build" / "hworacle" / "refuse_check"
CAP = 30                                              # examples printed per category
SEED = 0x48574F52                                     # "HWOR"
SHOW_GAME_BYTES = os.environ.get("HWO_SHOW_GAME_BYTES") == "1"

# ---- the enum of runner.c ---------------------------------------------------------------------------------

REASONS = {0: "OK", 1: "CTL", 16: "UNLISTED", 17: "SYSCALL", 18: "INT", 19: "HLT", 20: "IO", 21: "PRIV",
           22: "NONDET", 23: "SEG", 24: "X87MMX", 25: "VEX", 26: "FAR", 27: "INVALID64", 28: "TRUNC",
           29: "PREFIX", 30: "STATE", 31: "CTL_NO_STOP", 32: "NOT_EXEC", 33: "PROGRAM", 34: "UNCHECKED"}
R_UNLISTED, R_TRUNC, R_PREFIX, R_PROGRAM = 16, 28, 29, 33


def rname(c):
    return "%d/%s" % (c, REASONS.get(c, "?"))


# ---- building and driving the tool ------------------------------------------------------------------------

_BUILD_ERROR = None
_BUILT = False


def ensure_built():
    """Compile runner.c into the check tool, once.  Any compiler output at all is a failure."""
    global _BUILD_ERROR, _BUILT
    if not _BUILT:
        _BUILT = True
        EXE.parent.mkdir(parents=True, exist_ok=True)
        cmd = ["cc", "-O1", "-DHWO_CHECK_TOOL", "-Wall", "-Wextra", "-o", str(EXE), str(SRC)]
        p = subprocess.run(cmd, cwd=str(ROOT), capture_output=True, text=True, timeout=300)
        if p.returncode != 0:
            _BUILD_ERROR = "compiler failed (exit %d):\n%s%s" % (p.returncode, p.stdout, p.stderr)
        elif p.stdout.strip() or p.stderr.strip():
            _BUILD_ERROR = "compiler printed warnings or notes:\n%s%s" % (p.stdout, p.stderr)
    if _BUILD_ERROR:
        raise AssertionError(_BUILD_ERROR)


def run_tool(mode, lines, timeout=240):
    ensure_built()
    args = [str(EXE)] + ([mode] if mode else [])
    p = subprocess.run(args, input="".join(l + "\n" for l in lines), capture_output=True, text=True, timeout=timeout)
    if p.returncode != 0 or p.stderr:
        raise AssertionError("refuse_check %s exited %d: %s" % (mode, p.returncode, p.stderr[:500]))
    out = p.stdout.splitlines()
    if len(out) != len(lines):
        raise AssertionError("refuse_check %s: %d lines in, %d lines out" % (mode, len(lines), len(out)))
    return out


Res = collections.namedtuple("Res", "len cls rep pushf direct uncond cri rel")


def decode_many(blobs):
    """blobs: list of bytes.  One subprocess call.  Returns a list of Res."""
    out = run_tool("", [b.hex() for b in blobs])
    res = []
    for l in out:
        v = l.split()
        if len(v) != 8:
            raise AssertionError("unexpected refuse_check output %r" % l)
        res.append(Res(*map(int, v)))
    return res


def fmt_res(r):
    if r.cls >= 16:
        return "refused %s" % rname(r.cls)
    return "allowed cls=%d len=%d rep=%d pushf=%d direct=%d uncond=%d cri=%d rel=%d" % (
        r.cls, r.len, r.rep, r.pushf, r.direct, r.uncond, r.cri, r.rel)


# ---- findings, capped per category ------------------------------------------------------------------------

class Findings:
    """Counts every finding; keeps up to CAP examples per category, preferring ones with a new `key`."""

    def __init__(self):
        self.cats = collections.OrderedDict()

    def add(self, cat, msg, key=None):
        e = self.cats.setdefault(cat, {"n": 0, "ex": [], "keys": set(), "spill": []})
        e["n"] += 1
        if len(e["ex"]) < CAP and (key is None or key not in e["keys"]):
            e["ex"].append(msg)
            e["keys"].add(key)
        elif len(e["spill"]) < CAP:
            e["spill"].append(msg)

    def count(self):
        return sum(e["n"] for e in self.cats.values())

    def render(self, title):
        lines = []
        for cat, e in self.cats.items():
            ex = e["ex"] + e["spill"][:max(0, CAP - len(e["ex"]))]
            lines.append("%s: %s: %d total, showing %d" % (title, cat, e["n"], len(ex)))
            lines.extend("    " + m for m in ex)
        return "\n".join(lines)


# ---- Capstone ---------------------------------------------------------------------------------------------

CAP_ADDR = 0x10_0000_0000                             # far from 0 and 2**64 so a rel32 target stays positive
_md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
_md.detail = True
_mdl = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)       # lite: size and mnemonic only


def cap_one(b):
    """The first instruction Capstone decodes from b (a CsInsn with detail), or None."""
    for i in _md.disasm(b, CAP_ADDR, 1):
        return i
    return None


def cap_size(b):
    for i in _mdl.disasm_lite(b, CAP_ADDR, 1):
        return i[1]
    return 0


def _regset(rx):
    pat = re.compile(rx)
    return frozenset(getattr(cx86, n) for n in dir(cx86) if pat.match(n))


SEG_REGS = frozenset(getattr(cx86, "X86_REG_" + n) for n in ("CS", "DS", "ES", "FS", "GS", "SS"))
MM_REGS = _regset(r"X86_REG_MM\d+$")
ST_REGS = _regset(r"X86_REG_ST\d+$")
YZ_REGS = _regset(r"X86_REG_[YZ]MM\d+$")
CRDR_REGS = _regset(r"X86_REG_(CR|DR)\d+$")
FS_REG = cx86.X86_REG_FS

# Mnemonics (the last word, so "lock xadd" and "rep movsb" count by their instruction) that the policy refuses.
DENY_MN = frozenset("""syscall sysenter sysret sysretq sysexit sysexitq int int1 icebp into hlt in out insb insw insd outsb outsw
outsd rdtsc rdtscp cpuid rdrand rdseed rdpmc xgetbv xsetbv rdmsr wrmsr cli sti lgdt lidt sgdt sidt sldt str smsw lmsw
lldt ltr invlpg invlpga invd wbinvd swapgs clts wrfsbase wrgsbase rdfsbase rdgsbase lar lsl verr verw lcall ljmp lfs
lgs lss emms femms xbegin xend xabort xtest rdpid rdpkru wrpkru monitor mwait mwaitx monitorx vmcall vmlaunch vmresume
vmxoff vmxon vmread vmwrite vmclear vmptrld vmptrst vmfunc vmmcall vmrun vmload vmsave stgi clgi skinit invept invvpid
rsm getsec wait fwait lds les pusha popa pushal popal bound aaa aad aam aas daa das salc arpl senduipi clui stui
testui uiret rdsspd rdsspq incsspd incsspq setssbsy clrssbsy wrssd wrssq wrussd wrussq enclu encls enclv sysexitl
""".split())
DENY_MN_PREFIX = ("xsave", "xrstor", "fxsave", "fxrstor", "iret", "retf", "v")   # v*: VEX/EVEX/VMX
DENY_GROUPS = {cx86.X86_GRP_IRET: "iret group", cx86.X86_GRP_PRIVILEGE: "privileged group",
               cx86.X86_GRP_FPU: "x87 group", cx86.X86_GRP_MMX: "MMX group"}
LOCKABLE = frozenset("add adc and btc btr bts cmpxchg cmpxchg8b cmpxchg16b dec inc neg not or sbb sub xor xadd xchg".split())

LEGACY = frozenset((0x66, 0x67, 0xF2, 0xF3, 0xF0, 0x64, 0x65, 0x2E, 0x36, 0x3E, 0x26))


def last_word(mn):
    return mn.split()[-1]


def cap_policy_denies(ins):
    """Why Capstone's reading of this instruction is something the policy refuses (list of strings)."""
    why = []
    mn = last_word(ins.mnemonic)
    if mn in DENY_MN or mn.startswith(DENY_MN_PREFIX):
        why.append("mnemonic %s" % ins.mnemonic)
    for g in ins.groups:
        if g in DENY_GROUPS:
            why.append(DENY_GROUPS[g])
        elif g == cx86.X86_GRP_INT and mn != "int3":
            why.append("int group")
    for o in ins.operands:
        if o.type == cx86.X86_OP_REG:
            r = o.reg
            if r in SEG_REGS:
                why.append("segment register operand %s" % ins.reg_name(r))
            elif r in MM_REGS:
                why.append("MMX register %s" % ins.reg_name(r))
            elif r in ST_REGS:
                why.append("x87 register %s" % ins.reg_name(r))
            elif r in YZ_REGS:
                why.append("YMM/ZMM register %s" % ins.reg_name(r))
            elif r in CRDR_REGS:
                why.append("control/debug register %s" % ins.reg_name(r))
        elif o.type == cx86.X86_OP_MEM and o.mem.segment == FS_REG:
            why.append("fs: override")
    return why


def cap_is_ctl(ins):
    mn = last_word(ins.mnemonic)
    return (ins.group(capstone.CS_GRP_JUMP) or ins.group(capstone.CS_GRP_CALL) or ins.group(capstone.CS_GRP_RET)
            or mn.startswith("loop") or mn in ("jrcxz", "jecxz", "jcxz"))


# ---- my own reading of the prefixes and the opcode (independent of runner.c's code) -----------------------

class P:
    """Prefixes and opcode of a byte string, split the way the architecture does it: legacy prefixes in any
    order, then a REX that must sit right before the opcode, then the opcode map."""
    __slots__ = ("leg", "rex", "mp", "op", "opi", "modrm", "mod", "reg", "rm")


def parse(b):
    i = 0
    n = len(b)
    while i < n and b[i] in LEGACY:
        i += 1
    p = P()
    p.leg = bytes(b[:i])
    p.rex = 0
    if i < n and (b[i] & 0xF0) == 0x40:
        p.rex = b[i]
        i += 1
    if i >= n:
        return None
    if b[i] == 0x0F:
        if i + 1 >= n:
            return None
        if b[i + 1] in (0x38, 0x3A):
            if i + 2 >= n:
                return None
            p.mp = "0f38" if b[i + 1] == 0x38 else "0f3a"
            p.opi = i + 2
        else:
            p.mp = "0f"
            p.opi = i + 1
    else:
        p.mp = "1"
        p.opi = i
    p.op = b[p.opi]
    if p.opi + 1 < n:
        p.modrm = b[p.opi + 1]
        p.mod, p.reg, p.rm = p.modrm >> 6, (p.modrm >> 3) & 7, p.modrm & 7
    else:
        p.modrm = p.mod = p.reg = p.rm = None
    return p


def mandatory_prefix(leg):
    """The SSE mandatory prefix the way the manuals describe it: the last of F2/F3 wins over 66."""
    rep = None
    for x in leg:
        if x in (0xF2, 0xF3):
            rep = x
    if rep == 0xF3:
        return "f3"
    if rep == 0xF2:
        return "f2"
    return "66" if 0x66 in leg else "np"


IO_OPS = frozenset((0x6C, 0x6D, 0x6E, 0x6F, 0xE4, 0xE5, 0xE6, 0xE7, 0xEC, 0xED, 0xEE, 0xEF))
INVALID64_OPS = frozenset((0x06, 0x07, 0x0E, 0x16, 0x17, 0x1E, 0x1F, 0x27, 0x2F, 0x37, 0x3F, 0x60, 0x61, 0x82, 0x9A,
                           0xCE, 0xD4, 0xD5, 0xD6, 0xEA))
MAP1_DENY = (IO_OPS | INVALID64_OPS
             | frozenset((0x62, 0xC4, 0xC5, 0xCD, 0xF1, 0xF4, 0xFA, 0xFB, 0x8C, 0x8E, 0x9B, 0xCA, 0xCB, 0xCF))
             | frozenset(range(0xD8, 0xE0)))
MAP0F_DENY = frozenset((0x00, 0x01, 0x02, 0x03, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0E, 0x0F, 0x20, 0x21, 0x22, 0x23, 0x30,
                        0x31, 0x32, 0x33, 0x34, 0x35, 0x37, 0x77, 0x78, 0x79, 0xA0, 0xA1, 0xA2, 0xA8, 0xA9, 0xAA, 0xB2,
                        0xB4, 0xB5))
STRING_OPS = frozenset((0xA4, 0xA5, 0xA6, 0xA7, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF))
MAP1_DIRECT = frozenset(list(range(0x70, 0x80)) + [0xE0, 0xE1, 0xE2, 0xE3, 0xE8, 0xE9, 0xEB])


def policy_violations(p):
    """Byte-level policy from the project brief, written from the Intel opcode tables (not from runner.c)."""
    v = []
    if 0x64 in p.leg:
        v.append("fs: override prefix")
    op = p.op
    if p.mp == "1":
        if op in MAP1_DENY:
            v.append("one-byte opcode %02x is refused by policy" % op)
        if op == 0x8F and p.reg not in (None, 0):
            v.append("8f /%d is XOP" % p.reg)
        if op == 0xFF and p.reg in (3, 5):
            v.append("ff /%d is a far transfer" % p.reg)
        if op in (0xC6, 0xC7) and p.modrm == 0xF8:
            v.append("%02x f8 is xabort/xbegin" % op)
    elif p.mp == "0f":
        if op in MAP0F_DENY:
            v.append("0f %02x is refused by policy" % op)
        np = mandatory_prefix(p.leg) == "np"
        if op == 0xAE:
            if 0xF3 in p.leg:
                v.append("f3 0f ae is rd/wr fs/gs base (or CET)")
            if np and p.mod not in (None, 3) and p.reg in (0, 1, 4, 5, 6):
                v.append("0f ae /%d is a state dump/load" % p.reg)
        if op == 0xC7:
            if p.reg in (6, 7):
                v.append("0f c7 /%d is rdrand/rdseed/rdpid/vm*" % p.reg)
            if p.mod not in (None, 3) and p.reg in (3, 4, 5):
                v.append("0f c7 /%d is xrstors/xsavec/xsaves" % p.reg)
    return v


def expected_flags(p, cap=None):
    """(ctl, direct, uncond, cri, rep_string, pushf) the way the opcode tables say.  cap is Capstone's reading,
    used only to decide the 66+REX.W pushf size, where the manuals are not what a decoder author guesses."""
    ctl = direct = uncond = cri = False
    if p.mp == "1":
        op = p.op
        direct = op in MAP1_DIRECT
        uncond = op in (0xE9, 0xEB)
        cri = op in (0xC2, 0xC3, 0xE8) or (op == 0xFF and p.reg in (2, 4))
        ctl = direct or cri
    elif p.mp == "0f":
        direct = ctl = 0x80 <= p.op <= 0x8F
    rep = p.mp == "1" and p.op in STRING_OPS and (0xF2 in p.leg or 0xF3 in p.leg)
    pushf = 0
    if p.mp == "1" and p.op == 0x9C:
        if cap is not None:
            pushf = 2 if last_word(cap.mnemonic) == "pushf" else 8
        else:
            pushf = 2 if 0x66 in p.leg else 8
    return ctl, direct, uncond, cri, rep, pushf


# ---- Capstone's known blind spots and the encodings that are #UD on a real CPU ----------------------------------

def _sub(b, idx, newbytes, drop=0):
    return b[:idx] + newbytes + b[idx + drop:]


class _Len:
    """Stands in for a checker result where only the length matters."""
    def __init__(self, n):
        self.len = n


def explain_capstone_invalid(b, r, p):
    """The checker allowed `b` (length r.len) and Capstone cannot decode it.  Returns a short name when this is one
    of a few documented classes, else None.  Classes that are not simply #UD are verified by re-decoding an
    equivalent encoding with Capstone and requiring the same length."""
    L = r.len
    # 1. LOCK on something that cannot be locked: #UD (SDM vol 2, LOCK: only ADD ADC AND BTC BTR BTS CMPXCHG
    #    CMPXCH8B CMPXCHG16B DEC INC NEG NOT OR SBB SUB XOR XADD XCHG with a memory destination).
    if p is not None and 0xF0 in p.leg:
        nf0 = p.leg.count(0xF0)
        stripped = bytes(x for x in b[:p.opi] if x != 0xF0) + b[p.opi:]
        # the 0xF0 bytes are all in the prefix area, so removing them removes exactly nf0 bytes
        ins2 = cap_one(stripped.ljust(15, b"\x90")[:15]) if stripped else None
        if ins2 is not None and ins2.size == L - nf0:
            mn = last_word(ins2.mnemonic)
            ops = ins2.operands
            if mn == "xchg":
                lockable = any(o.type == cx86.X86_OP_MEM for o in ops)
            else:
                lockable = mn in LOCKABLE and len(ops) > 0 and ops[0].type == cx86.X86_OP_MEM
            if not lockable:
                return "lock prefix on a non-lockable instruction (#UD)"
            return None
        # Capstone cannot decode the unlocked form either: it is one of the classes below, and LOCK on it is #UD too
        if stripped and len(stripped) == len(b) - nf0:
            why = explain_capstone_invalid(stripped, _Len(L - nf0), parse(stripped))
            if why:
                return "lock prefix on a non-lockable instruction (#UD)"
        return None
    if p is None:
        return None
    # 0. long runs of repeated prefixes: Capstone gives up, the architecture does not (up to 15 bytes in all).
    #    The same instruction with each prefix once (the last F2/F3 and the last 64/65 kept) must decode to the
    #    same length minus the bytes removed.
    if len(p.leg) > len(set(p.leg)):
        keep, rep, seg = [], None, None
        for x in p.leg:
            if x in (0xF2, 0xF3):
                rep = x
            elif x in (0x64, 0x65):
                seg = x
            elif x not in keep:
                keep.append(x)
        short = bytes(keep + ([seg] if seg else []) + ([rep] if rep else [])) + b[len(p.leg):]
        removed = len(b) - len(short)
        if cap_size(short.ljust(15, b"\x90")[:15]) == L - removed:
            return "repeated prefixes (Capstone gives up on long prefix runs)"
        why = explain_capstone_invalid(short, _Len(L - removed), parse(short))
        if why:
            return why
    mp, op, mod, reg = p.mp, p.op, p.mod, p.reg
    mand = mandatory_prefix(p.leg)
    # 2. memory-only encodings with a register ModRM (#UD): LEA, MOVLPS/MOVLPD/MOVHPS/MOVHPD stores, MOVNTPS/PD,
    #    MOVNTI, MOVNTDQ, MOVNTDQA, LDDQU, MOVBE.
    if mod == 3:
        if mp == "1" and op == 0x8D:
            return "memory-only opcode with a register operand (#UD)"
        if mp == "0f" and (op in (0x13, 0x17, 0x2B, 0xC3) or (op == 0x12 and mand == "66")
                           or (op == 0x16 and mand == "66") or (op == 0xE7 and mand == "66")
                           or (op == 0xF0 and mand == "f2")):
            return "memory-only opcode with a register operand (#UD)"
        if mp == "0f38" and ((op in (0xF0, 0xF1) and mand != "f2") or (op == 0x2A and mand == "66")):
            return "memory-only opcode with a register operand (#UD)"
    # 3. register-only encodings with a memory ModRM (#UD): MOVMSKPS/PD, PMOVMSKB, PEXTRW r32,xmm, MASKMOVDQU
    if mod is not None and mod != 3 and mp == "0f" and (op == 0x50 or (op in (0xD7, 0xC5, 0xF7) and mand == "66")):
        return "register-only opcode with a memory operand (#UD)"
    # 4. undefined /reg of the shift-by-immediate groups (#UD)
    if mp == "0f" and mod == 3 and mand == "66":
        valid = {0x71: (2, 4, 6), 0x72: (2, 4, 6), 0x73: (2, 3, 6, 7)}.get(op)
        if valid is not None and reg not in valid:
            return "undefined ModRM.reg in a shift-by-immediate group (#UD)"
    # 5. the reserved-NOP / hint block that Capstone does not know: 0f 0d, 0f 18..0f 1e behave like 0f 1f
    if mp == "0f" and op in (0x0D, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E):
        alt = _sub(b, p.opi, b"\x1f", 1)
        if cap_size(alt) == L:
            return "hint/reserved-NOP block (same length as 0f 1f, which Capstone knows)"
    # 6. 0f ae with mod=3 /5 /6 /7 is LFENCE/MFENCE/SFENCE for every rm (Intel group-15 table); Capstone wants rm=0
    if mp == "0f" and op == 0xAE and mod == 3 and mand == "np" and reg in (5, 6, 7):
        alt = _sub(b, p.opi + 1, bytes([b[p.opi + 1] & 0xF8]), 1)
        if cap_size(alt) == L:
            return "fence with rm != 0 (group 15 table ignores rm)"
    # 7. movsxd with an address-size prefix: Capstone has no 67h form of 63 /r
    if mp == "1" and op == 0x63 and 0x67 in p.leg:
        alt = _sub(b, p.opi, b"\x8b", 1)
        if cap_size(alt) == L:
            return "movsxd with 67h (Capstone gap; same layout as mov 8b)"
    # 8. 66 + F3 + REX + 90: no operand bytes, only the prefixes, which Capstone declines to combine
    if mp == "1" and op == 0x90 and 0xF3 in p.leg and 0x66 in p.leg:
        nf3 = p.leg.count(0xF3)
        alt = bytes(x for x in b[:p.opi] if x != 0xF3) + b[p.opi:]
        if cap_size(alt.ljust(15, b"\x90")[:15]) == L - nf3:
            return "66+F3+REX nop/pause (Capstone declines to combine; no operand bytes)"
    return None


# ---- the comparison of one allowed result with everything else ---------------------------------------------

def hexw(b, L=None):
    """Hex with a bar after the instruction when its length L is known."""
    h = b.hex()
    return h if not L else h[:2 * L] + "|" + h[2 * L:]


def check_allowed(b, r, F, where="", notes=None, strict_len=None):
    """b: the bytes handed to the checker (up to 15), r: its answer (cls 0 or 1).  Everything checked here is
    independent of how the checker works inside; findings go to F, harmless oddities to notes."""
    L = r.len
    tag = "%s%s -> %s" % (where, hexw(b, L), fmt_res(r))
    p = parse(b)
    ins = cap_one(b)
    key_op = (p.mp, p.op) if p else None

    # shape of the answer
    if not 1 <= L <= 15 or L > len(b):
        F.add("allowed with an impossible length", tag, key_op)
        return
    if strict_len is not None and L != strict_len:
        F.add("allowed with a length different from the known encoding", tag + " | known length %d" % strict_len, key_op)

    # length against Capstone
    if ins is None:
        why = explain_capstone_invalid(b, r, p)
        if why is None:
            F.add("allowed but Capstone cannot decode it (not a documented #UD / Capstone gap)", tag, key_op)
        elif notes is not None:
            notes[why] = notes.get(why, 0) + 1
            notes.setdefault("example: " + why, hexw(b, L))
    else:
        if ins.size != L:
            F.add("LENGTH differs from Capstone", "%s | Capstone: %d bytes %s %s" % (tag, ins.size, ins.mnemonic, ins.op_str),
                  key_op)
        for w in cap_policy_denies(ins):
            F.add("ALLOWED but Capstone says the policy refuses it",
                  "%s | Capstone: %s %s (%s)" % (tag, ins.mnemonic, ins.op_str, w), key_op)

    # byte-level policy
    if p is None:
        F.add("allowed but the bytes have no opcode", tag)
        return
    for w in policy_violations(p):
        F.add("ALLOWED but opcode-table policy refuses it", "%s | %s" % (tag, w), key_op)

    # class and flags
    ctl, direct, uncond, cri, rep, pushf = expected_flags(p, ins)
    if ins is not None and ins.size == L:
        cap_ctl = bool(cap_is_ctl(ins))
        if cap_ctl != bool(r.cls == 1):
            F.add("CLASS differs from Capstone", "%s | Capstone: %s %s (control transfer: %s)" % (
                tag, ins.mnemonic, ins.op_str, cap_ctl), key_op)
        if cap_ctl and ctl and bool(direct) and ins.operands and ins.operands[0].type == cx86.X86_OP_IMM:
            want = ins.operands[0].imm - (CAP_ADDR + ins.size)
            if r.rel != want:
                F.add("REL differs from Capstone's branch target", "%s | Capstone: %s %s, rel should be %d" % (
                    tag, ins.mnemonic, ins.op_str, want), key_op)
    if (r.cls == 1) != ctl:
        F.add("CLASS differs from the opcode tables", "%s | expected control transfer: %s" % (tag, ctl), key_op)
    if r.direct != int(direct) or r.uncond != int(uncond) or r.cri != int(cri):
        F.add("direct/uncond/call_ret_ind flags wrong",
              "%s | expected direct=%d uncond=%d cri=%d" % (tag, direct, uncond, cri), key_op)
    if r.rep != int(rep):
        F.add("rep_string wrong", "%s | expected %d (opcode %s%02x, legacy prefixes %s)" % (
            tag, int(rep), "" if p.mp == "1" else p.mp + " ", p.op, p.leg.hex()), key_op)
    if r.pushf != pushf:
        F.add("pushf wrong", "%s | expected %d" % (tag, pushf), key_op)
    if not ctl and (r.direct or r.uncond or r.cri or r.rel):
        F.add("branch fields set on a non-branch", tag, key_op)


def check_refused(b, r, F):
    """The invariants of a refusal: a reason code, length 0, all fields clear."""
    if r.cls not in REASONS or r.cls < 16:
        F.add("refusal with an unknown class", "%s -> %s" % (b.hex(), fmt_res(r)))
    elif (r.len, r.rep, r.pushf, r.direct, r.uncond, r.cri, r.rel) != (0,) * 7:
        F.add("refusal that leaves fields set (a caller that looks at them is misled)",
              "%s -> %d %d %d %d %d %d %d %d" % ((b.hex(),) + tuple(r)))


# ---- the hand-written tables -------------------------------------------------------------------------------

def H(s):
    return bytes.fromhex(s.replace(" ", ""))


# (hex, accepted reason codes or None for "any refusal", what it is, mandatory-prefix sensitive)
# msens: the same bytes with a 66/F2/F3 in front are a different (SSE) instruction, so only the other prefixes are tried.
MUST_REFUSE = [
    # syscall family -> 17
    ("0f05", (17,), "syscall"), ("0f34", (17,), "sysenter"), ("0f35", (17,), "sysexit"), ("0f07", (17,), "sysret"),
    ("480f07", (17,), "sysretq"),
    # int n, int1 -> 18 (int3 = cc is allowed)
    ("cd80", (18,), "int 0x80"), ("cd29", (18,), "int 0x29"), ("cd2e", (18,), "int 0x2e"), ("cd03", (18,), "int 3, two-byte form"),
    ("cd00", (18,), "int 0"), ("f1", (18,), "int1"),
    ("f4", (19,), "hlt"),
    # port I/O -> 20
    ("e410", (20,), "in al, 0x10"), ("e510", (20,), "in eax, 0x10"), ("e610", (20,), "out 0x10, al"), ("e710", (20,), "out 0x10, eax"),
    ("ec", (20,), "in al, dx"), ("ed", (20,), "in eax, dx"), ("ee", (20,), "out dx, al"), ("ef", (20,), "out dx, eax"),
    ("6c", (20,), "insb"), ("6d", (20,), "insd"), ("6e", (20,), "outsb"), ("6f", (20,), "outsd"),
    ("f36c", (20,), "rep insb"), ("f36f", (20,), "rep outsd"),
    # privileged -> 21
    ("0f20c0", (21,), "mov rax, cr0"), ("0f20d8", (21,), "mov rax, cr3"), ("0f22c0", (21,), "mov cr0, rax"), ("0f22e0", (21,), "mov cr4, rax"),
    ("0f21c0", (21,), "mov rax, dr0"), ("0f23c0", (21,), "mov dr0, rax"), ("440f20c0", (21,), "mov rax, cr8"),
    ("fa", (21,), "cli"), ("fb", (21,), "sti"),
    ("0f0100", (21,), "sgdt [rax]"), ("0f0108", (21,), "sidt [rax]"), ("0f0110", (21,), "lgdt [rax]"), ("0f0118", (21,), "lidt [rax]"),
    ("0f0120", (21,), "smsw [rax]"), ("0f0130", (21,), "lmsw [rax]"), ("0f0138", (21,), "invlpg [rax]"), ("0f01f8", (21,), "swapgs"),
    ("0f01c8", (21,), "monitor"), ("0f01c9", (21,), "mwait"), ("0f01d1", (21,), "xsetbv"), ("0f01d5", (21,), "xend"),
    ("0f01d6", (21,), "xtest"), ("0f01ee", (21,), "rdpkru"), ("0f01ef", (21,), "wrpkru"), ("0f01c1", (21,), "vmcall"),
    ("0f01c2", (21,), "vmlaunch"), ("0f01c3", (21,), "vmresume"), ("0f01c4", (21,), "vmxoff"), ("0f01ca", (21,), "clac"),
    ("0f01cb", (21,), "stac"), ("0f01d8", (21,), "vmrun"), ("0f01fa", (21,), "monitorx"),
    ("0f0000", (21,), "sldt [rax]"), ("0f0008", (21,), "str [rax]"), ("0f0010", (21,), "lldt [rax]"), ("0f0018", (21,), "ltr [rax]"),
    ("0f0020", (21,), "verr [rax]"), ("0f0028", (21,), "verw [rax]"), ("0f00c0", (21,), "sldt eax"),
    ("0f02c1", (21,), "lar eax, ecx"), ("0f03c1", (21,), "lsl eax, ecx"), ("0f06", (21,), "clts"), ("0f08", (21,), "invd"),
    ("0f09", (21,), "wbinvd"), ("0f30", (21,), "wrmsr"), ("0f32", (21,), "rdmsr"), ("0f37", (21,), "getsec"), ("0faa", (21,), "rsm"),
    ("0f78c1", (21,), "vmread"), ("0f79c1", (21,), "vmwrite"), ("0fc730", (21,), "vmptrld [rax]"), ("0fc738", (21,), "vmptrst [rax]"),
    ("660fc730", (21,), "vmclear [rax]"), ("f30fc730", (21,), "vmxon [rax]"),
    # the 0f 01 group mixes privileged and non-deterministic ones; either reason refuses
    ("0f01d0", (21, 22), "xgetbv"), ("0f01f9", (21, 22), "rdtscp"),
    # answers that are the machine's -> 22
    ("0f31", (22,), "rdtsc"), ("0fa2", (22,), "cpuid"), ("0fc7f0", (22,), "rdrand eax"), ("0fc7f8", (22,), "rdseed eax"),
    ("480fc7f0", (22,), "rdrand rax"), ("660fc7f0", (22,), "rdrand ax"), ("480fc7f8", (22,), "rdseed rax"), ("0f33", (22,), "rdpmc"),
    ("f30fc7f8", (22,), "rdpid eax"),
    # segment registers and fs: -> 23
    ("8ed8", (23,), "mov ds, eax"), ("8cd8", (23,), "mov eax, ds"), ("8e00", (23,), "mov es, [rax]"), ("8c18", (23,), "mov [rax], ds"),
    ("488cd8", (23,), "mov rax, ds"), ("8ee0", (23,), "mov fs, eax"), ("8ee8", (23,), "mov gs, eax"), ("8ed0", (23,), "mov ss, eax"),
    ("64488b042500000000", (23,), "mov rax, fs:[0]"), ("648b00", (23,), "mov eax, fs:[rax]"), ("6490", (23,), "fs nop"),
    ("64ff20", (23,), "fs jmp [rax]"), ("6401c0", (23,), "fs add eax, eax"), ("64a100000000000000 00".replace(" ", ""), (23,), "mov eax, fs:[moffs]"),
    ("f3480faec0", (23,), "rdfsbase rax"), ("f3480faec8", (23,), "rdgsbase rax"), ("f3480faed0", (23,), "wrfsbase rax"),
    ("f3480faed8", (23,), "wrgsbase rax"), ("f30faec0", (23,), "rdfsbase eax"),
    ("0fa0", (23,), "push fs"), ("0fa1", (23,), "pop fs"), ("0fa8", (23,), "push gs"), ("0fa9", (23,), "pop gs"),
    ("0fb200", (23,), "lss [rax]"), ("0fb400", (23,), "lfs [rax]"), ("0fb500", (23,), "lgs [rax]"),
    # x87 and MMX -> 24
    ("d9c0", (24,), "fld st0"), ("d8c1", (24,), "fadd st0, st1"), ("dd00", (24,), "fld qword [rax]"), ("dbe3", (24,), "fninit"),
    ("dfe0", (24,), "fnstsw ax"), ("d9e8", (24,), "fld1"), ("d97c24fe", (24,), "fnstcw [rsp-2]"), ("dec1", (24,), "faddp"),
    ("d9ee", (24,), "fldz"), ("9b", (24,), "fwait"), ("0f77", (24,), "emms"), ("0f0e", (24,), "femms"), ("0f0fc10d", (24,), "3DNow! pfcmpge"),
    ("0f6fc1", (24,), "movq mm0, mm1", True), ("0fefc1", (24,), "pxor mm0, mm1", True), ("0f6ec0", (24,), "movd mm0, eax", True),
    ("0f7ec1", (24,), "movd ecx, mm0", True), ("0f6f00", (24,), "movq mm0, [rax]", True), ("0f7f00", (24,), "movq [rax], mm0", True),
    ("0f3800c1", (24,), "pshufb mm0, mm1", True), ("0f3a0fc108", (24,), "palignr mm0, mm1, 8", True),
    ("0f2ac1", (24,), "cvtpi2ps xmm0, mm1", True), ("0f2cc1", (24,), "cvttps2pi mm0, xmm1", True), ("0f2dc1", (24,), "cvtps2pi mm0, xmm1", True),
    ("0fd7c1", (24,), "pmovmskb eax, mm1", True), ("0fc4c101", (24,), "pinsrw mm0, ecx, 1", True), ("0fc5c101", (24,), "pextrw eax, mm1, 1", True),
    ("0fe707", (24,), "movntq [rdi], mm0", True), ("0ff7c1", (24,), "maskmovq mm0, mm1", True), ("0f70c11b", (24,), "pshufw mm0, mm1, 0x1b", True),
    ("0f71d003", (24,), "psrlw mm0, 3", True), ("0f60c1", (24,), "punpcklbw mm0, mm1", True), ("0fdbc1", (24,), "pand mm0, mm1", True),
    ("0ffcc1", (24,), "paddb mm0, mm1", True),
    ("f30fd6c1", (24,), "movq2dq xmm0, mm1"), ("f20fd6c1", (24,), "movdq2q mm0, xmm1"),
    # VEX, EVEX, XOP -> 25
    ("c5f877", (25,), "vzeroupper"), ("c5f858c1", (25,), "vaddps xmm0, xmm0, xmm1"), ("c4e17858c1", (25,), "vaddps, 3-byte VEX"),
    ("c4e27d18c1", (25,), "vbroadcastss ymm0, xmm1"), ("62f17c4858c1", (25,), "vaddps zmm0, zmm0, zmm1 (EVEX)"),
    ("62", (25,), "bare EVEX byte"), ("c5", (25,), "bare VEX byte"), ("c4", (25,), "bare 3-byte VEX byte"),
    ("8fe97801c1", (25,), "XOP"), ("8fe978c1c1", (25,), "XOP vphaddbw"), ("c5fc10 00".replace(" ", ""), (25,), "vmovups ymm0, [rax]"),
    # far transfers -> 26
    ("cb", (26,), "retf"), ("ca0800", (26,), "retf 8"), ("cf", (26,), "iretd"), ("48cf", (26,), "iretq"), ("ff28", (26,), "ljmp [rax]"),
    ("ff18", (26,), "lcall [rax]"), ("48ff28", (26,), "rex.w ljmp [rax]"), ("48ff18", (26,), "rex.w lcall [rax]"), ("ff2b", (26,), "ljmp [rbx]"),
    ("ff1b", (26,), "lcall [rbx]"), ("ff2d10000000", (26,), "ljmp [rip+16]"), ("ffd8", (26,), "ff /3, register form"),
    # not instructions in 64-bit mode -> 27 (the far ones, 9a and ea, are 26 in spirit; either refuses)
    ("06", (27,), "push es"), ("07", (27,), "pop es"), ("0e", (27,), "push cs"), ("16", (27,), "push ss"), ("17", (27,), "pop ss"),
    ("1e", (27,), "push ds"), ("1f", (27,), "pop ds"), ("27", (27,), "daa"), ("2f", (27,), "das"), ("37", (27,), "aaa"), ("3f", (27,), "aas"),
    ("60", (27,), "pusha"), ("61", (27,), "popa"), ("82c001", (27,), "82 /0 alias"), ("ce", (27,), "into"), ("d40a", (27,), "aam"),
    ("d50a", (27,), "aad"), ("d6", (27,), "salc"), ("9a112233445566", (26, 27), "call far ptr16:32"), ("ea112233445566", (26, 27), "jmp far ptr16:32"),
    # state dumps -> 30
    ("0fae00", (30,), "fxsave [rax]"), ("0fae08", (30,), "fxrstor [rax]"), ("0fae20", (30,), "xsave [rax]"), ("0fae28", (30,), "xrstor [rax]"),
    ("0fae30", (30,), "xsaveopt [rax]"), ("480fae00", (30,), "fxsave64 [rax]"), ("480fae08", (30,), "fxrstor64 [rax]"),
    ("480fae20", (30,), "xsave64 [rax]"), ("0fae8000010000", (30,), "fxsave [rax+0x100]"),
    ("0fc718", (30, 16), "xrstors [rax]"), ("0fc720", (30, 16), "xsavec [rax]"), ("0fc728", (30, 16), "xsaves [rax]"),
    # transactional memory: refused, reason not important
    ("c6f801", None, "xabort 1"), ("c7f800000000", None, "xbegin"),
]

# Refusals the author documents (prefix combinations) and truncations: exact bytes, no prefix variants.
EDGE_REFUSE = [
    ("66e800000000", 29, "call rel32 with 66"), ("66e900000000", 29, "jmp rel32 with 66"), ("66c3", 29, "ret with 66"),
    ("66c20800", 29, "ret 8 with 66"), ("6675fe", 29, "jne with 66"), ("660f8400000000", 29, "je rel32 with 66"),
    ("66ffe2", 29, "jmp rdx with 66"), ("66ffd2", 29, "call rdx with 66"), ("66eb00", 29, "jmp rel8 with 66"), ("66e300", 29, "jrcxz with 66"),
    ("66e200", 29, "loop with 66"), ("66ff2500000000", 29, "jmp [rip] with 66"),
    ("66f20f58c1", 29, "66 together with F2 on an SSE op"), ("66f30f58c1", 29, "66 together with F3 on an SSE op"),
    ("f30f40c1", 29, "cmov with F3"), ("f20fbcc1", 29, "bsf with F2"), ("660fc8", 29, "bswap ax"),
    # truncations: every instruction cut one byte short of its layout is TRUNC, not an invented length
    ("0f", 28, "0f alone"), ("66", 28, "66 alone"), ("48", 28, "REX alone"), ("f3", 28, "F3 alone"), ("66" * 15, 28, "15 prefixes"),
    ("8b", 28, "mov, no ModRM"), ("8b04", 28, "SIB missing"), ("8b4424", 28, "disp8 missing"), ("8b8424000000", 28, "disp32 cut"),
    ("8b05000000", 28, "rip disp32 cut"), ("8b0425000000", 28, "SIB base-5 disp32 cut by one"),
    ("e8000000", 28, "call rel32 cut"), ("e900", 28, "jmp rel32 cut"), ("eb", 28, "jmp rel8 cut"), ("7f", 28, "jg rel8 cut"),
    ("0f84000000", 28, "jcc rel32 cut"), ("b8000000", 28, "mov eax, imm32 cut"), ("48b81122334455667788"[:-2], 28, "movabs cut by a byte"),
    ("c70000000000"[:-2], 28, "mov [rax], imm32 cut"), ("c800", 28, "enter cut"), ("c208", 28, "ret imm16 cut"),
    ("a1" + "11" * 7, 28, "moffs64 cut"), ("67a1000000", 28, "moffs32 cut"), ("660f3a0fc1", 28, "palignr without imm8"),
    ("660f3a0f", 28, "0f3a opcode without ModRM"), ("0f38", 28, "0f38 alone"), ("660f3800", 28, "0f38 00 without ModRM"),
    ("f6c0", 28, "test r/m8, imm8 without imm8"), ("f7c0000000", 28, "test r/m32, imm32 cut"), ("66f7c000", 28, "test r/m16, imm16 cut"),
    ("6681c000", 28, "add ax, imm16 cut"), ("6a", 28, "push imm8 cut"), ("68000000", 28, "push imm32 cut"), ("0fbae0", 28, "bt imm8 cut"),
    ("660f70c1", 28, "pshufd imm8 cut"), ("0fc2c1", 28, "cmpps imm8 cut"), ("ff", 16, "ff alone: no /reg, so no class and no length"), ("f6", 28, "f6 alone"),
    ("66" * 14 + "01c0", 28, "16 bytes: longer than any instruction"), ("66" * 13 + "01", 28, "15 bytes, ModRM missing"),
    ("0f05"[:2], 28, "0f alone again"),
]


def A(h, n, cls=0, **kw):
    return (h.replace(" ", ""), n, cls, kw)


# (hex, length, class, flags).  Class 1 = control transfer.  Flags: rep, pushf, direct, uncond, cri, rel.
MUST_ALLOW = [
    # ModRM: register, [reg], [reg+disp8], [reg+disp32], SIB with and without a base, RIP-relative, 67h
    A("01c8", 2), A("4801c8", 3), A("4d01c8", 3), A("00c8", 2), A("6601c8", 3), A("8b00", 2), A("8b03", 2), A("8b06", 2),
    A("8b4005", 3), A("8b5df8", 3), A("8b8000010000", 6), A("8b8bf0ffffff", 6), A("8b0488", 3), A("8b04c8", 3),
    A("8b04 25 78563412", 7), A("8b04 8d 78563412", 7), A("8b04 e5 78563412", 7), A("8b44 88 10", 4), A("8b4c2408", 4),
    A("8b44 8d 10", 4), A("8b84 8d 10000000", 7), A("8b05 10000000", 6), A("488d05 10000000", 7), A("488b05 00000000", 7),
    A("4c8b0d 10000000", 7), A("418b4500", 4), A("418b04 25 78563412", 8), A("8b8424 10000000", 7), A("4a8b04 e5 00000000", 8),
    A("678b00", 3), A("678b05 10000000", 7), A("678b04 88", 4), A("678b04 25 78563412", 8), A("67488b4010", 5),
    A("8b042400", 3) if False else A("8b0424", 3), A("8b6c2400", 4),
    # immediates: imm8, imm16, imm32, imm64
    A("83c001", 3), A("83c0ff", 3), A("b001", 2), A("b4ff", 2), A("6a01", 2), A("6aff", 2), A("a801", 2), A("3c01", 2), A("0401", 2),
    A("c0e004", 3), A("c1e004", 3), A("48c1e83f", 4), A("6bc005", 3), A("486bc005", 4), A("c60001", 3), A("c6400501", 4), A("803801", 3),
    A("c8100000", 4), A("c8ffff1f", 4), A("c9", 1),
    A("b801000000", 5), A("0500010000", 5), A("6800010000", 5), A("c70078563412", 6), A("c7400578563412", 7), A("81c078563412", 6),
    A("4881c078563412", 7), A("48c7c0ffffffff", 7), A("69c078563412", 6), A("4869c078563412", 7), A("f7c078563412", 6),
    A("a978563412", 5), A("3d78563412", 5), A("4181c078563412", 7), A("81 38 78563412", 6), A("81 78 05 78563412", 7),
    A("66b80100", 4), A("66a93412", 4), A("66053412", 4), A("663d3412", 4), A("66c7003412", 5), A("66c74005 3412", 6), A("6681c03412", 5),
    A("66813834 12", 5), A("6669c03412", 5), A("666bc001", 4), A("66683412", 4), A("666a01", 3), A("66f7c03412", 5), A("66f7d0", 3),
    A("6681c0 3412", 5), A("664881c078563412", 8), A("6648b81122334455667788", 11), A("66b11 2".replace(" ", ""), 2) if False else A("66b012", 3),
    A("48b81122334455667788", 10), A("49b81122334455667788", 10), A("48bf1122334455667788", 10), A("b811223344", 5), A("66b81122", 4),
    A("b011", 2), A("41b811223344", 6), A("48b8ffffffffffffffff", 10),
    # moffs
    A("a111 22 33 44 55 66 77 88", 9), A("a011 22 33 44 55 66 77 88", 9), A("a211 22 33 44 55 66 77 88", 9), A("a311 22 33 44 55 66 77 88", 9),
    A("48a111 22 33 44 55 66 77 88", 10), A("67a111223344", 6), A("67a011223344", 6), A("67a311223344", 6), A("6748a111223344", 7),
    A("66a111 22 33 44 55 66 77 88", 10),
    # F6/F7: /0 and /1 take an immediate, /2../7 do not
    A("f6c001", 3), A("f6d0", 2), A("f6d8", 2), A("f6e1", 2), A("f6e9", 2), A("f6f1", 2), A("f6f9", 2), A("f7d0", 2), A("f7d8", 2),
    A("f7e1", 2), A("f7e9", 2), A("f7f1", 2), A("f7f9", 2), A("48f7f1", 3), A("f6400501", 4), A("f7400578563412", 7), A("f75005", 3),
    A("f76005", 3), A("f6 14 25 00000000", 7), A("f7 14 25 00000000", 7), A("f6 04 25 00000000 01", 8),
    # string instructions: rep_string only with a REP/REPNE prefix
    A("a4", 1), A("a5", 1), A("a6", 1), A("a7", 1), A("aa", 1), A("ab", 1), A("ac", 1), A("ad", 1), A("ae", 1), A("af", 1),
    A("f3a4", 2, rep=1), A("f2a4", 2, rep=1), A("f3a5", 2, rep=1), A("f348a5", 3, rep=1), A("66f3a5", 3, rep=1), A("f3aa", 2, rep=1),
    A("f348ab", 3, rep=1), A("f3ac", 2, rep=1), A("f3ad", 2, rep=1), A("f2ae", 2, rep=1), A("f3a6", 2, rep=1), A("f2a7", 2, rep=1),
    A("f3af", 2, rep=1), A("f2ae", 2, rep=1), A("67f3a4", 3, rep=1), A("f3f2a4", 3, rep=1), A("f2f3a4", 3, rep=1), A("2ef3a4", 3, rep=1),
    A("65f3a4", 3, rep=1), A("f3 f3 a4", 3, rep=1), A("48a5", 2), A("66a5", 2), A("f390", 2), A("f30f58c1", 4), A("f30fb8c1", 4),
    A("f2 0f 58 c1", 4), A("f3a801", 3), A("f3 8b 00", 3), A("f2 8b 00", 3),
    # pushf / popf
    A("9c", 1, pushf=8), A("669c", 2, pushf=2), A("489c", 2, pushf=8), A("9d", 1), A("669d", 2), A("489d", 2), A("679c", 2, pushf=8),
    # control transfers
    A("7405", 2, 1, direct=1, rel=5), A("75fa", 2, 1, direct=1, rel=-6), A("7000", 2, 1, direct=1, rel=0), A("7f7f", 2, 1, direct=1, rel=127),
    A("7c80", 2, 1, direct=1, rel=-128), A("eb fe", 2, 1, direct=1, uncond=1, rel=-2), A("eb00", 2, 1, direct=1, uncond=1, rel=0),
    A("e9fbffffff", 5, 1, direct=1, uncond=1, rel=-5), A("e910000000", 5, 1, direct=1, uncond=1, rel=16),
    A("e800000000", 5, 1, direct=1, cri=1, rel=0), A("e810000000", 5, 1, direct=1, cri=1, rel=16), A("e8f0ffffff", 5, 1, direct=1, cri=1, rel=-16),
    A("0f8400010000", 6, 1, direct=1, rel=0x100), A("0f8ff0ffffff", 6, 1, direct=1, rel=-16), A("0f8000000080", 6, 1, direct=1, rel=-2**31),
    A("0f8fffffff7f", 6, 1, direct=1, rel=2**31 - 1), A("0f8500000000", 6, 1, direct=1, rel=0),
    A("e2fe", 2, 1, direct=1, rel=-2), A("e105", 2, 1, direct=1, rel=5), A("e0f0", 2, 1, direct=1, rel=-16), A("e305", 2, 1, direct=1, rel=5),
    A("67e302", 3, 1, direct=1, rel=2), A("f27405", 3, 1, direct=1, rel=5), A("f2e800000000", 6, 1, direct=1, cri=1, rel=0),
    A("f2e910000000", 6, 1, direct=1, uncond=1, rel=16), A("2e7405", 3, 1, direct=1, rel=5), A("3e7405", 3, 1, direct=1, rel=5),
    A("ffd0", 2, 1, cri=1), A("ffd1", 2, 1, cri=1), A("ffe0", 2, 1, cri=1), A("41ffe0", 3, 1, cri=1), A("48ffe0", 3, 1, cri=1),
    A("ff1510000000", 6, 1, cri=1), A("ff2510000000", 6, 1, cri=1), A("ff24c510000000", 7, 1, cri=1), A("ff1424", 3, 1, cri=1),
    A("ff5008", 3, 1, cri=1), A("ff6008", 3, 1, cri=1), A("ff1500000000", 6, 1, cri=1), A("c3", 1, 1, cri=1), A("c20800", 3, 1, cri=1),
    A("c2ffff", 3, 1, cri=1), A("f3c3", 2, 1, cri=1), A("f2c3", 2, 1, cri=1), A("3effe0", 3, 1, cri=1), A("f2ffe0", 3, 1, cri=1),
    A("ffc0", 2), A("ffc8", 2), A("ff30", 2), A("ff7508", 3), A("fec0", 2), A("fec8", 2), A("fe00", 2), A("ff00", 2), A("ff08", 2),
    A("6650", 2), A("50", 1), A("5f", 1), A("4150", 2), A("415f", 2), A("8f00", 2), A("8fc0", 2), A("8f4005", 3), A("ff 34 24", 3),
    # SSE with each mandatory prefix, SSE2 integer, 0f 38 and 0f 3a with and without imm8
    A("0f58c1", 3), A("660f58c1", 4), A("f30f58c1", 4), A("f20f58c1", 4), A("0f28c1", 3), A("660f28c1", 4), A("0f1000", 3),
    A("0f1105 10000000", 7), A("f30f100424", 5), A("f20f1005 10000000", 8), A("660f6ec0", 4), A("66480f6ec0", 5), A("660f7ec0", 4),
    A("f30f7ec1", 4), A("660fd6c1", 4), A("660fefc0", 4), A("660f70c01b", 5), A("f30f70c01b", 5), A("f20f70c01b", 5), A("0fc6c11b", 4),
    A("660fc6c11b", 5), A("0fc2c102", 4), A("f20fc2c104", 5), A("f30fc2c104", 5), A("660fc2c104", 5), A("660f73d808", 5), A("660f71d003", 5),
    A("660f72f003", 5), A("660fc4c001", 5), A("660fc5c001", 5), A("660fd7c1", 4), A("0f50c1", 3), A("0f12c1", 3), A("0f16c1", 3),
    A("0f1400", 3), A("660f1400", 4), A("0f2ec1", 3), A("660f2ec1", 4), A("0f2fc1", 3), A("f30f2cc1", 4), A("f20f2cc1", 4), A("f30f2ac1", 4),
    A("f2480f2ac1", 5), A("f30f5bc1", 4), A("0f5bc1", 3), A("660f5bc1", 4), A("f30fe6c1", 4), A("f20fe6c1", 4), A("660fe6c1", 4),
    A("f20f12c1", 4), A("f30f16c1", 4), A("f20f7cc1", 4), A("660f7cc1", 4), A("f20ff000", 4), A("0f2b07", 3), A("660fe707", 4),
    A("660f382a07", 5), A("0fc307", 3), A("480fc307", 4), A("0f5100", 3), A("0f5300", 3), A("f30f5300", 4), A("0f5400", 3), A("660f5700", 4),
    A("660f6000", 4), A("660f6f00", 4), A("f30f6f00", 4), A("660f7f00", 4), A("f30f7f00", 4), A("660ff400", 4), A("660ffe00", 4),
    A("660f3800c1", 5), A("660f3817c1", 5), A("660f381cc1", 5), A("660f3820c1", 5), A("660f3830c1", 5), A("660f3840c1", 5),
    A("660f3841c1", 5), A("660f3837c1", 5), A("660f38dcc1", 5), A("660f38dbc1", 5), A("660f3810c1", 5), A("660f3814c1", 5),
    A("660f3815c1", 5), A("660f380b 00", 5), A("0f38f000", 4), A("0f38f100", 4), A("660f38f100", 5), A("f20f38f0c1", 5), A("f20f38f1c1", 5),
    A("66f20f38f1c1", 6), A("f2480f38f1c1", 6), A("480f38f100", 5),
    A("660f3a0fc108", 6), A("660f3a0bc104", 6), A("660f3a08c104", 6), A("660f3a0ec104", 6), A("660f3a14c001", 6), A("660f3a16c001", 6),
    A("66480f3a16c001", 7), A("660f3a17c001", 6), A("660f3a20c001", 6), A("660f3a21c110", 6), A("660f3a22c001", 6), A("660f3a40c1ff", 6),
    A("660f3a42c101", 6), A("660f3a44c100", 6), A("660f3a60c100", 6), A("660f3a63c10a", 6), A("660f3adfc101", 6), A("660f3a0f0008", 6),
    A("660f3a0f400508", 7), A("660f3a0f05 10000000 08", 10),
    # other two-byte opcodes
    A("0fa3c1", 3), A("0fabc1", 3), A("0fb3c1", 3), A("0fbbc1", 3), A("0fbae004", 4), A("0fbaf804", 4), A("0fba2004", 4), A("0fa4c104", 4),
    A("0fa5c1", 3), A("0fadc1", 3), A("0facc104", 4), A("0fb10b", 3), A("f00fb10b", 4), A("0fb00b", 3), A("0fc10b", 3), A("0fc00b", 3),
    A("0fc70b", 3), A("480fc70b", 4), A("f00fc70b", 4), A("0fc8", 2), A("480fc8", 3), A("410fc8", 3), A("0fcf", 2), A("0fb6c1", 3),
    A("0fb7c1", 3), A("0fbec1", 3), A("0fbfc1", 3), A("480fbfc1", 4), A("0fb600", 3), A("0fafc1", 3), A("0f4400", 3), A("0f44c1", 3),
    A("0f95c0", 3), A("0f9fc0", 3), A("f30fb8c1", 4), A("f3480fb8c1", 5), A("f30fbcc1", 4), A("f30fbdc1", 4), A("0fbcc1", 3), A("0fbdc1", 3),
    A("0f0b", 2), A("cc", 1), A("0f1808", 3), A("0f1800", 3), A("0f0d08", 3), A("0f0d00", 3), A("0faee8", 3), A("0faef0", 3), A("0faef8", 3),
    A("0fae10", 3), A("0fae18", 3), A("0fae9000010000", 7),
    A("0f1f00", 3), A("0f1f4000", 4), A("0f1f440000", 5), A("660f1f440000", 6), A("0f1f8000000000", 7), A("0f1f840000000000", 8),
    A("660f1f840000000000", 9), A("66660f1f840000000000", 10),
    # one-byte odds and ends
    A("98", 1), A("99", 1), A("4898", 2), A("4899", 2), A("6698", 2), A("9e", 1), A("9f", 1), A("d7", 1), A("f5", 1), A("f8", 1), A("f9", 1),
    A("fc", 1), A("fd", 1), A("90", 1), A("6690", 2), A("4190", 2), A("4890", 2), A("91", 1), A("96", 1), A("4891", 2), A("87c1", 2), A("8708", 2),
    A("86c1", 2), A("84c0", 2), A("85c0", 2), A("8d0488", 3), A("488d4c2408", 5), A("63c1", 2), A("4863c1", 3), A("486300", 3), A("d1e0", 2),
    A("d3e0", 2), A("d0e0", 2), A("d2e0", 2), A("d1f8", 2), A("48d1e8", 3), A("31c0", 2), A("33c0", 2), A("29c8", 2), A("3bc1", 2),
    A("8a00", 2), A("8800", 2), A("8900", 2), A("8b0c24", 3),
    # gs: is allowed; cs/ds/es/ss overrides are ignored in 64-bit mode; lock
    A("658b00", 3), A("65488b042510000000", 9), A("65488b042560000000", 9), A("2e8b00", 3), A("3e8b00", 3), A("268b00", 3), A("368b00", 3),
    A("f0ff00", 3), A("f0480fb10b", 5), A("f00108", 3), A("66f00108", 4), A("65f00108", 4),
    # long prefix runs and the 15-byte limit
    A("66" * 13 + "01c0", 15), A("66" * 5 + "0f1f840000000000"[:0] + "90", 6), A("2e" * 3 + "66" * 3 + "0f1f840000000000", 14),
]


# ---- test helpers -----------------------------------------------------------------------------------------

def pad15(b, filler):
    """b followed by deterministic filler up to 15 bytes."""
    return (bytes(b) + filler)[:15] if len(b) < 15 else bytes(b[:15])


FILLER = bytes((0xA5, 0x5A, 0x3C, 0xC3, 0x0F, 0x05, 0xCD, 0x80, 0xF4, 0x90, 0xEB, 0xFE, 0x01, 0x02, 0x03))
NOTES = {}


def note(msg):
    print("NOTE: " + msg, flush=True)


# ---- the tests ---------------------------------------------------------------------------------------------

class RefusalTests(unittest.TestCase):
    maxDiff = None

    @classmethod
    def setUpClass(cls):
        cls.t0 = time.perf_counter()

    def setUp(self):
        ensure_built()

    # ---- build ----

    def test_01_build(self):
        """runner.c compiles with -Wall -Wextra and no output at all; the tool reports its versions."""
        p = subprocess.run([str(EXE), "version"], capture_output=True, text=True, timeout=30)
        self.assertEqual(p.returncode, 0)
        self.assertRegex(p.stdout.strip(), r"^\d+ \d+$")

    # ---- 1. must-refuse ----

    def test_02_must_refuse_table(self):
        F = Findings()
        blobs, metas = [], []
        for e in MUST_REFUSE:
            h, codes, label = e[0], e[1], e[2]
            for variant, b in (("exact", H(h)), ("padded", pad15(H(h), FILLER))):
                blobs.append(b)
                metas.append((h, codes, label, variant))
        res = decode_many(blobs)
        for b, (h, codes, label, variant), r in zip(blobs, metas, res):
            msg = "%s (%s, %s) -> %s" % (b.hex(), label, variant, fmt_res(r))
            if r.cls < 16:
                F.add("ALLOWED but must be refused", msg + (" | expected %s" % (codes,) if codes else ""), h)
                continue
            check_refused(b, r, F)
            if codes is not None and r.cls not in codes and not (variant == "padded" and r.cls == R_TRUNC):
                F.add("refused with a different reason than the table expects", msg + " | expected %s" % (codes,), h)
        note("must-refuse table: %d encodings, %d lines checked" % (len(MUST_REFUSE), len(blobs)))
        if F.count():
            self.fail("\n" + F.render("MUST-REFUSE"))

    def test_03_must_refuse_with_prefixes(self):
        """The dangerous encodings stay refused behind every legal prefix (66 f2 f3 f0 2e 3e 36 26 65 67 and REX)."""
        prefixes = [b"", b"\x66", b"\xf2", b"\xf3", b"\xf0", b"\x2e", b"\x3e", b"\x36", b"\x26", b"\x65", b"\x67",
                    b"\x40", b"\x41", b"\x44", b"\x48", b"\x49", b"\x4c", b"\x4f", b"\x66\x48", b"\xf3\x48", b"\x66\xf2",
                    b"\xf2\xf3", b"\xf0\x66", b"\x65\x67\x48", b"\x2e\x3e\x36", b"\x66" * 3, b"\xf2\x66\x48",
                    b"\x64", b"\x66\x64", b"\x64\x66", b"\xf3\x64\x48"]
        F = Findings()
        blobs, metas = [], []
        for e in MUST_REFUSE:
            h, codes, label = e[0], e[1], e[2]
            msens = len(e) > 3 and e[3]
            base = H(h)
            for pre in prefixes:
                if not pre:
                    continue
                if msens and any(x in pre for x in (0x66, 0xF2, 0xF3)):
                    continue                          # 66/F2/F3 + this opcode is a different (SSE) instruction
                if len(pre) + len(base) > 15:
                    continue
                for tail in (False, True):
                    b = pre + base
                    b = pad15(b, FILLER) if tail else b
                    blobs.append(b)
                    metas.append((pre.hex() + "+" + h, label))
        res = decode_many(blobs)
        for b, (what, label), r in zip(blobs, metas, res):
            if r.cls < 16:
                F.add("ALLOWED behind a legal prefix but must be refused", "%s (%s) -> %s" % (b.hex(), label, fmt_res(r)), what.split("+")[1])
            else:
                check_refused(b, r, F)
        note("prefix variants: %d encodings checked" % len(blobs))
        if F.count():
            self.fail("\n" + F.render("PREFIX-VARIANTS"))

    def test_04_edge_refusals(self):
        """Truncations are TRUNC (never an invented length) and the documented prefix refusals are PREFIX."""
        F = Findings()
        blobs = [H(e[0]) for e in EDGE_REFUSE]
        for b, e, r in zip(blobs, EDGE_REFUSE, decode_many(blobs)):
            if r.cls != e[1]:
                F.add("edge refusal with a different outcome", "%s (%s) -> %s | expected %s" % (b.hex(), e[2], fmt_res(r), rname(e[1])))
            else:
                check_refused(b, r, F)
        if F.count():
            self.fail("\n" + F.render("EDGE"))

    # ---- 2. must-allow ----

    def test_05_must_allow_table(self):
        F = Findings()
        blobs, metas = [], []
        for h, n, cls, kw in MUST_ALLOW:
            b = H(h)
            if len(b) != n and not (n == 15 and len(b) == 15):
                pass
            exact = b[:n]
            for variant, bb in (("exact", exact), ("padded", pad15(exact, FILLER))):
                blobs.append(bb)
                metas.append((h, n, cls, kw, variant))
        res = decode_many(blobs)
        for bb, (h, n, cls, kw, variant), r in zip(blobs, metas, res):
            want = Res(n, cls, kw.get("rep", 0), kw.get("pushf", 0), kw.get("direct", 0), kw.get("uncond", 0), kw.get("cri", 0),
                       kw.get("rel", 0))
            msg = "%s (%s) -> %s | expected %s" % (hexw(bb, n), variant, fmt_res(r), fmt_res(want))
            if r.cls >= 16:
                F.add("REFUSED but the table says it is allowed", msg, h)
            elif r != want:
                F.add("allowed with the wrong length/class/flags", msg, h)
            # the table itself must agree with Capstone, or the table is wrong
            if variant == "padded":
                cs = cap_size(bb)
                if cs != n:
                    F.add("TABLE ERROR: my length differs from Capstone's", "%s | I say %d, Capstone says %d" % (h, n, cs), h)
        note("must-allow table: %d encodings" % len(MUST_ALLOW))
        if F.count():
            self.fail("\n" + F.render("MUST-ALLOW"))

    # ---- 3. the project's own vectors ----

    def test_06_vectors_against_capstone(self):
        sys.path.insert(0, str(ROOT / "tests"))
        try:
            import vectors
        finally:
            sys.path.pop(0)
        encs = []                                      # (source, hex)
        for family, rows in sorted(vectors.FAMILY_VECTORS.items()):
            for e in rows:
                encs.append(("FAMILY_VECTORS/" + family, e[1]))
        for family, rows in sorted(vectors.FAMILY_REFUSED.items()):
            for e in rows:
                encs.append(("FAMILY_REFUSED/" + family, e[1]))
        for table in ("PROGRAMS", "JUMP_PROGRAMS", "TAIL_PROGRAMS"):
            for name, (base, insns) in getattr(vectors, table).items():
                for hx, _text in insns:
                    encs.append((table + "/" + name, hx))
        for table in ("UNSUPPORTED_VECTORS", "NAME_CLASH_VECTORS", "ORACLE_BUG_VECTORS"):
            for e in getattr(vectors, table):
                encs.append((table, e[1]))
        seen, uniq = set(), []
        for src, hx in encs:
            if hx not in seen:
                seen.add(hx)
                uniq.append((src, hx))
        F = Findings()
        blobs = []
        for src, hx in uniq:
            b = bytes.fromhex(hx)
            blobs.append((src, b, b))                  # exact bytes
            blobs.append((src, b, pad15(b, FILLER)))   # followed by other bytes
        res = decode_many([x[2] for x in blobs])
        allowed = refused = 0
        reasons = collections.Counter()
        for (src, enc, b), r in zip(blobs, res):
            if b is enc:
                if r.cls >= 16:
                    refused += 1
                    reasons[r.cls] += 1
                else:
                    allowed += 1
            if r.cls >= 16:
                check_refused(b, r, F)
                continue
            if r.len != len(enc):
                F.add("allowed with a length different from the encoding in the vectors",
                      "%s: %s -> %s | encoding is %d bytes" % (src, hexw(b, r.len), fmt_res(r), len(enc)), enc.hex())
            cs = cap_size(enc)
            if cs != len(enc):
                F.add("vector encoding is not one Capstone instruction", "%s: %s Capstone says %d bytes" % (src, enc.hex(), cs))
            check_allowed(b, r, F, where=src + ": ", strict_len=len(enc))
        note("vectors: %d distinct encodings: %d allowed, %d refused %s" % (
            len(uniq), allowed, refused, dict((rname(k), v) for k, v in sorted(reasons.items()))))
        if F.count():
            self.fail("\n" + F.render("VECTORS"))

    # ---- 4. systematic and random comparison with Capstone ----

    @staticmethod
    def candidates():
        rng = random.Random(SEED)
        prefixes = [b"", b"\x66", b"\xf2", b"\xf3", b"\x67", b"\xf0", b"\x65", b"\x2e", b"\x48", b"\x41", b"\x4c",
                    b"\x66\x48", b"\xf3\x48", b"\x66\xf2", b"\x66\xf3", b"\xf2\xf3"]
        more = [b"\x66\x67", b"\x67\x48", b"\x66\x67\x48", b"\xf3\x66", b"\x40", b"\x4f", b"\x49", b"\x66\x41", b"\x64", b"\xf2\x48",
                b"\xf0\x66", b"\x3e"]
        opcodes = ([bytes([i]) for i in range(256)] + [bytes([0x0F, i]) for i in range(256)]
                   + [bytes([0x0F, 0x38, i]) for i in range(256)] + [bytes([0x0F, 0x3A, i]) for i in range(256)])

        def modrm_stratified(k):
            """ModRM bytes that walk through mod, reg and rm, with extra weight on rm=4 (SIB) and rm=5."""
            m = (k % 4) << 6 | rng.randrange(8) << 3 | rng.randrange(8)
            r = rng.random()
            if r < 0.15:
                m = (m & 0xF8) | 4
            elif r < 0.30:
                m = (m & 0xF8) | 5
            return m

        def sib():
            s = rng.getrandbits(8)
            return (s & 0xF8) | 5 if rng.random() < 0.35 else s

        def finish(head):
            return (head + rng.randbytes(15))[:15]

        out = []
        for pre in prefixes:                                       # A: every prefix x every opcode x 12 ModRM bytes
            for op in opcodes:
                for k in range(12):
                    out.append(finish(pre + op + bytes([modrm_stratified(k), sib()])))
        for pre in more:
            for op in opcodes:
                for k in range(6):
                    out.append(finish(pre + op + bytes([modrm_stratified(k), sib()])))
        for pre in (b"", b"\x66", b"\xf2", b"\xf3"):               # B: the mandatory-prefix classes, 32 (mod,rm) x 2 reg
            for op in opcodes:
                for mod in range(4):
                    for rm in range(8):
                        for _ in range(2):
                            out.append(finish(pre + op + bytes([mod << 6 | rng.randrange(8) << 3 | rm, sib()])))
        groups = [bytes([i]) for i in (0x80, 0x81, 0x83, 0x8F, 0xC0, 0xC1, 0xC6, 0xC7, 0xD0, 0xD1, 0xD2, 0xD3, 0xF6, 0xF7, 0xFE, 0xFF)]
        groups += [bytes([0x0F, i]) for i in (0x00, 0x01, 0x0D, 0x18, 0x1A, 0x1B, 0x1C, 0x1E, 0x71, 0x72, 0x73, 0xAE, 0xBA, 0xC7)]
        for pre in (b"", b"\x66", b"\xf2", b"\xf3", b"\x48"):       # C: every ModRM byte of the group opcodes
            for op in groups:
                for m in range(256):
                    out.append(finish(pre + op + bytes([m, sib()])))
        soup = [0x66, 0x67, 0xF2, 0xF3, 0xF0, 0x64, 0x65, 0x2E, 0x36, 0x3E, 0x26, 0x66, 0x66, 0xF3, 0xF2]
        for _ in range(60000):                                      # D: prefix soup up to the 15-byte limit
            n = rng.choice((0, 1, 1, 2, 2, 3, 4, 6, 10, 12, 13, 14))
            head = bytes(rng.choice(soup) for _ in range(n))
            if rng.random() < 0.4:
                head += bytes([rng.choice((0x40, 0x41, 0x44, 0x48, 0x49, 0x4C, 0x4F))])
            out.append(finish(head + rng.choice(opcodes)))
        for _ in range(60000):                                      # E: purely random
            out.append(rng.randbytes(15))
        return out

    def test_07_systematic_and_random_vs_capstone(self):
        t0 = time.perf_counter()
        cands = self.candidates()
        self.assertGreaterEqual(len(cands), 300000)
        res = decode_many(cands)
        t1 = time.perf_counter()
        F = Findings()
        notes = {}
        by_cls = collections.Counter(r.cls for r in res)
        n_allowed = 0
        for b, r in zip(cands, res):
            if r.cls >= 16:
                check_refused(b, r, F)
                continue
            n_allowed += 1
            check_allowed(b, r, F, notes=notes)
        t2 = time.perf_counter()
        note("systematic/random: %d candidates (seed %#x), %d allowed and compared, %d refused; outcomes %s" % (
            len(cands), SEED, n_allowed, len(cands) - n_allowed, dict((rname(k), v) for k, v in sorted(by_cls.items()))))
        note("  checker %.1fs, comparison %.1fs" % (t1 - t0, t2 - t1))
        classes = {k: v for k, v in notes.items() if not k.startswith("example: ")}
        for k in sorted(classes):
            note("  allowed by the checker, not a valid encoding on a real CPU or unknown to Capstone: %s: %d (e.g. %s)" % (
                k, classes[k], notes["example: " + k]))
        if F.count():
            self.fail("\n" + F.render("CAPSTONE"))

    # ---- 5. the static program check ----

    def test_08_programs(self):
        def prog(addr, code, stops):
            return "%x %s %s" % (addr, code.hex() if code else "-", ",".join("%x" % s for s in stops) if stops else ",")

        def jcc8(op, rel):
            return bytes([op, rel & 0xFF])

        loop = H("31c0") + H("b90a000000") + H("ffc0") + H("ffc9") + jcc8(0x75, -6)    # xor; mov ecx,10; inc; dec; jne -> inc
        self.assertEqual(len(loop), 13)
        cases = []                                    # (name, addr, code, stops, accepted?, expected code or None)

        def acc(name, addr, code, stops):
            cases.append((name, addr, code, stops, True, 0))

        def rej(name, addr, code, stops, want=None):
            cases.append((name, addr, code, stops, False, want))

        acc("counted loop, falls off the end into the stop", 0x1000, loop, [0x100D])
        acc("counted loop ending in jmp to the stop", 0x1000, loop + H("eb00"), [0x100F])
        acc("counted loop ending in jmp rel32 to the stop", 0x1000, loop + H("e910000000"), [0x1000 + 13 + 5 + 0x10])
        acc("stop list with several stops", 0x1000, loop, [0x5000, 0x100D, 0x6000])
        acc("forward conditional skipping an instruction", 0x4000, H("85c0") + jcc8(0x74, 2) + H("ffc0") + H("90"), [0x4007])
        acc("conditional branch straight to a stop outside the code", 0x2000, H("90") + jcc8(0x74, 0x10) + H("90"), [0x2004, 0x2003 + 0x10])
        acc("jmp past the end to a stop", 0x2000, H("90eb10"), [0x2013])
        acc("loop and jrcxz", 0x3000, jcc8(0xE3, 2) + jcc8(0xE2, -2), [0x3004])
        acc("backward jump to the first instruction", 0x3000, H("ffc0") + jcc8(0x7F, -4) + H("90"), [0x3005])
        acc("rep string instruction", 0x3000, H("f3a4") + H("90"), [0x3003])
        acc("pushf and popf", 0x3000, H("9c9d"), [0x3002])
        acc("int3 and ud2 are plain traps", 0x3000, H("cc0f0b"), [0x3003])
        acc("gs: access", 0x3000, H("658b00"), [0x3003])
        acc("a stop that is also the first instruction", 0x3000, H("ffc0") + jcc8(0x75, -4), [0x3000, 0x3004])
        acc("4096 bytes (the limit)", 0x10000, b"\x90" * 4096, [0x11000])

        rej("contains ret", 0x1000, H("31c0c3"), [0x1003], 33)
        rej("ends in ret", 0x1000, loop + H("c3"), [0x100E], 33)
        rej("ret imm16", 0x1000, H("c20800"), [0x1003], 33)
        rej("contains call rel32", 0x1000, H("e800000000") + H("90"), [0x1006], 33)
        rej("indirect jmp rax", 0x1000, H("ffe0"), [0x1002], 33)
        rej("indirect jmp r8", 0x1000, H("41ffe0"), [0x1003], 33)
        rej("indirect call rax", 0x1000, H("ffd0"), [0x1002], 33)
        rej("jmp [rip+0]", 0x1000, H("ff2500000000"), [0x1006], 33)
        rej("branch into the middle of mov imm32", 0x1000, H("eb02") + H("b890909090"), [0x1007], 33)
        rej("backward branch into the middle of an instruction", 0x1000, H("b801000000") + jcc8(0xEB, -5), [0x1007], 33)
        rej("branch outside the extent, not a stop", 0x1000, H("eb7f"), [0x1002], 33)
        rej("backward branch before the start, not a stop", 0x1000, H("90") + jcc8(0xEB, -0x20), [0x1002], 33)
        rej("falls off the end, no stop there", 0x2000, H("9090"), [0x9999], 33)
        rej("falls off the end, stop one byte too far", 0x2000, H("90"), [0x2002], 33)
        rej("ends in a conditional jump with no stop at the end", 0x2000, jcc8(0x74, 0), [0x5000], 33)
        rej("no stops at all", 0x1000, H("90"), [], 33)
        rej("empty code", 0x1000, b"", [0x1000], 33)
        rej("4097 bytes (over the limit)", 0x10000, b"\x90" * 4097, [0x11001], 33)
        rej("contains syscall", 0x1000, H("90") + H("0f05") + H("90"), [0x1004], 17)
        rej("contains sysenter", 0x1000, H("0f34"), [0x1002], 17)
        rej("contains int 0x80", 0x1000, H("cd80"), [0x1002], 18)
        rej("contains hlt", 0x1000, H("f4"), [0x1001], 19)
        rej("contains in", 0x1000, H("ec"), [0x1001], 20)
        rej("contains cli", 0x1000, H("fa"), [0x1001], 21)
        rej("contains rdtsc", 0x1000, H("0f31"), [0x1002], 22)
        rej("contains cpuid", 0x1000, H("0fa2"), [0x1002], 22)
        rej("contains an fs: access", 0x1000, H("648b00"), [0x1003], 23)
        rej("contains x87", 0x1000, H("d9c0"), [0x1002], 24)
        rej("contains MMX", 0x1000, H("0f6fc1"), [0x1003], 24)
        rej("contains VEX", 0x1000, H("c5f877"), [0x1003], 25)
        rej("contains retf", 0x1000, H("cb"), [0x1001], 26)
        rej("contains an instruction invalid in 64-bit mode", 0x1000, H("06"), [0x1001], 27)
        rej("contains fxsave", 0x1000, H("0fae00"), [0x1003], 30)
        rej("jcc with 66", 0x1000, H("6674fe"), [0x1003], 29)
        rej("bytes end in the middle of an instruction", 0x1000, H("90b80100"), [0x1004], 28)
        rej("bytes end after a lone 0f", 0x1000, H("0f"), [0x1001], 28)
        rej("bytes end in the middle of call rel32", 0x1000, H("e80000"), [0x1003], 28)
        rej("bytes end in the middle of movabs", 0x1000, H("48b8112233"), [0x1005], 28)
        rej("refused instruction after a good loop", 0x1000, loop + H("0f05"), [0x100F], 17)

        lines = [prog(a, c, s) for _n, a, c, s, _a, _w in cases]
        out = run_tool("program", lines)
        F = Findings()
        for (name, addr, code, stops, ok, want), line, v in zip(cases, lines, out):
            msg = "%s: addr %#x code %s stops %s -> %s" % (name, addr, code.hex()[:80] + ("..." if len(code) > 40 else ""),
                                                          [hex(s) for s in stops], v)
            if ok and v != "0":
                F.add("a good program was rejected", msg + " | expected 0")
            elif not ok:
                if v == "0":
                    F.add("a bad program was ACCEPTED", msg + " | expected %s" % ("a refusal" if want is None else rname(want)))
                elif want is not None and v != str(want):
                    F.add("a bad program was rejected with a different code", msg + " | expected %s" % rname(want))
        # the line format has no way to write an empty code field: such a line must be rejected too
        for line in ("1000  1000", "1000 ", ""):
            v = run_tool("program", [line])[0]
            if v == "0":
                F.add("a malformed program line was ACCEPTED", repr(line) + " -> " + v)
        note("programs: %d cases (%d accepted by design)" % (len(cases), sum(1 for c in cases if c[4])))
        if F.count():
            self.fail("\n" + F.render("PROGRAMS"))

    # ---- 6. sha256 ----

    def test_09_sha256(self):
        rng = random.Random(SEED ^ 0x5A5A)
        lengths = list(range(0, 301)) + [55, 56, 63, 64, 65, 119, 120, 119, 120, 55, 56, 63, 64, 65, 127, 128, 129, 183, 184, 247, 248]
        lengths += [rng.randrange(0, 301) for _ in range(300)]
        blobs = [rng.randbytes(n) for n in lengths]
        blobs += [b"", b"\x00", b"\xff" * 64, b"\x00" * 300, b"abc"]
        out = run_tool("sha256", [b.hex() for b in blobs])
        F = Findings()
        for b, got in zip(blobs, out):
            want = hashlib.sha256(b).hexdigest()
            if got != want:
                F.add("SHA-256 differs from hashlib", "%d bytes %s -> %s | hashlib %s" % (len(b), b.hex()[:40], got, want), len(b))
        for n in (55, 56, 63, 64, 65, 119, 120):
            self.assertIn(n, lengths)
        note("sha256: %d inputs, lengths 0..300 all present" % len(blobs))
        if F.count():
            self.fail("\n" + F.render("SHA256"))

    # ---- 7. game DLLs and the corpus, when present ----

    def test_10_game_dlls_and_corpus(self):
        files = [("game", ROOT / "game" / "bin" / "x64_retail" / "tier0.dll"),
                 ("game", ROOT / "game" / "bin" / "x64_retail" / "vstdlib.dll"),
                 ("corpus", ROOT / "tests" / "corpus" / "build" / "corpus_O1.dll"),
                 ("corpus", ROOT / "tests" / "corpus" / "build" / "corpus_O2.dll"),
                 ("corpus", ROOT / "tests" / "corpus" / "build" / "corpus_Os.dll")]
        present = [(k, p) for k, p in files if p.is_file()]
        if not present:
            return
        F = Findings()
        summary = {}
        for kind, path in present:
            sections = pe_exec_sections(path)
            seen = {}                                  # instruction bytes -> (15-byte window, rva, mnemonic)
            count = collections.Counter()              # instruction bytes -> occurrences
            n_ins = n_skip = 0
            md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
            md.skipdata = True
            for name, rva, data in sections:
                for addr, size, mn, _ops in md.disasm_lite(data, rva):
                    if mn == ".byte":
                        n_skip += 1
                        continue
                    n_ins += 1
                    off = addr - rva
                    ib = bytes(data[off:off + size])
                    count[ib] += 1
                    if ib not in seen:
                        seen[ib] = (bytes(data[off:off + 15]), addr, mn)
            items = list(seen.items())
            res = decode_many([v[0] for _k, v in items])
            reasons = collections.Counter()               # distinct encodings by outcome
            reasons_n = collections.Counter()             # occurrences by outcome
            mn_refused = collections.Counter()            # occurrences by mnemonic, refused
            mn_omitted = collections.Counter()            # ... refused by omission (16, 27, 29), not by policy
            n_allowed = 0
            module_notes = {}
            for (ib, (win, rva, mn)), r in zip(items, res):
                reasons[r.cls] += 1
                reasons_n[r.cls] += count[ib]
                where = "%s %s RVA %#x: " % (kind, path.name, rva)
                if r.cls >= 16:
                    check_refused(win, r, F)
                    w = last_word(mn)
                    mn_refused[w] += count[ib]
                    if r.cls in (R_UNLISTED, 27, R_PREFIX):
                        mn_omitted[w] += count[ib]
                    continue
                n_allowed += 1
                sub = Findings()
                check_allowed(win, r, sub, where="", notes=module_notes)
                for cat, e in sub.cats.items():
                    for m in e["ex"] + e["spill"]:
                        if kind == "game" and not SHOW_GAME_BYTES:
                            m = "%s%s (bytes withheld: game module; HWO_SHOW_GAME_BYTES=1 shows them) checker len=%d cls=%d, Capstone: %s" % (
                                where, "RVA", r.len, r.cls, mn)
                        else:
                            m = where + m
                        F.add(cat, m, (cat, mn))
                    F.cats[cat]["n"] += e["n"] - len(e["ex"]) - len(e["spill"])
            s = summary.setdefault(kind, {"ins": 0, "skip": 0, "distinct": 0, "allowed": 0, "reasons": collections.Counter(),
                                          "reasons_n": collections.Counter(), "refused": collections.Counter(),
                                          "omitted": collections.Counter(), "files": []})
            s["ins"] += n_ins
            s["skip"] += n_skip
            s["distinct"] += len(items)
            s["allowed"] += n_allowed
            s["reasons"].update(reasons)
            s["reasons_n"].update(reasons_n)
            s["refused"].update(mn_refused)
            s["omitted"].update(mn_omitted)
            s["files"].append("%s (%d executable sections, %d instructions, %d distinct)" % (path.name, len(sections), n_ins, len(items)))
            for k, v in module_notes.items():
                if not k.startswith("example: "):
                    NOTES[(kind, k)] = NOTES.get((kind, k), 0) + v
        for kind, s in summary.items():
            note("item 7 [%s]: %s" % (kind, "; ".join(s["files"])))
            note("  instructions seen %d (+%d bytes skipped as data), distinct encodings %d, allowed %d (%.1f%%), refused %d" % (
                s["ins"], s["skip"], s["distinct"], s["allowed"], 100.0 * s["allowed"] / max(1, s["distinct"]), s["distinct"] - s["allowed"]))
            note("  distinct encodings by outcome: " + ", ".join("%s %d" % (rname(k), v) for k, v in sorted(s["reasons"].items())))
            note("  occurrences by outcome:        " + ", ".join("%s %d" % (rname(k), v) for k, v in sorted(s["reasons_n"].items())))
            note("  25 most frequent mnemonics among refused instructions (occurrences): " +
                 ", ".join("%s %d" % kv for kv in s["refused"].most_common(25)))
            note("  25 most frequent mnemonics refused by omission (UNLISTED/INVALID64/PREFIX, not by policy): " +
                 ", ".join("%s %d" % kv for kv in s["omitted"].most_common(25)))
        for (kind, k), v in sorted(NOTES.items()):
            note("  [%s] allowed, not a valid encoding on a real CPU or unknown to Capstone: %s: %d" % (kind, k, v))
        if F.count():
            self.fail("\n" + F.render("DLLS"))


def pe_exec_sections(path):
    """[(name, rva, bytes)] of the executable sections of a PE file, read with struct."""
    raw = Path(path).read_bytes()
    if raw[:2] != b"MZ":
        raise AssertionError("%s is not a PE file" % path)
    pe = struct.unpack_from("<I", raw, 0x3C)[0]
    if raw[pe:pe + 4] != b"PE\0\0":
        raise AssertionError("%s has no PE signature" % path)
    nsec, = struct.unpack_from("<H", raw, pe + 6)
    opt_size, = struct.unpack_from("<H", raw, pe + 20)
    sec = pe + 24 + opt_size
    out = []
    for i in range(nsec):
        name, vsize, va, rsize, rptr, _r, _l, _nr, _nl, chars = struct.unpack_from("<8sIIIIIIHHI", raw, sec + 40 * i)
        if chars & 0x20000000 or chars & 0x20:          # MEM_EXECUTE or CNT_CODE
            n = min(rsize, vsize) if vsize else rsize
            out.append((name.rstrip(b"\0").decode("latin1"), va, raw[rptr:rptr + n]))
    return out


if __name__ == "__main__":
    unittest.main(verbosity=2)
