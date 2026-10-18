#!/usr/bin/env python3
"""Shared decoding for the CPU-feature scouts (tools/scout/cpu_*.py).

Read-only over the game binaries. Helpers:

  functions(pe)          .pdata ranges grouped by the function they belong to. A split
                         (chained) fragment joins the function it was cut from.
  Dis(pe)                one-instruction decoder keyed by RVA, with Ins records
  Dis.linear(begin,end)  census-style linear sweep (one-byte resync), returns Ins list
  descend(dis, ranges, starts)
                         recursive descent inside the function's ranges. Follows direct
                         jumps, both sides of conditional jumps, and MSVC switch tables
                         (lea + movsxd + add + jmp reg). Returns the reached instructions,
                         the successor map and the indirect jumps it could not resolve.
  preds(succ)            predecessor map of a successor map.

Ins fields: rva, size, mn (mnemonic), text (operand string, for console output only),
wr / rd (register families written / read, from Capstone's regs_write / regs_read),
ops (tuples: ('r', family, name) | ('i', value, size) | ('m', base, index, scale, disp,
seg, base_name, size) | ('o',)), grp (frozenset of short group names), target (direct
branch target RVA or None).

Register families fold al/ax/eax/rax into 'a', and so on.  Nothing here writes files.
"""

import bisect
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "inventory"))
from pe_inventory import PE, find_pe_files  # noqa: E402

import capstone  # noqa: E402
from capstone import x86_const as X  # noqa: E402

GROUP_NAMES = {
    X.X86_GRP_JUMP: "jump", X.X86_GRP_CALL: "call", X.X86_GRP_RET: "ret", X.X86_GRP_INT: "int",
    X.X86_GRP_IRET: "iret", X.X86_GRP_PRIVILEGE: "priv", X.X86_GRP_BRANCH_RELATIVE: "relbr",
    X.X86_GRP_AVX: "avx", X.X86_GRP_AVX2: "avx2", X.X86_GRP_AES: "aes", X.X86_GRP_PCLMUL: "pclmul",
    X.X86_GRP_SHA: "sha", X.X86_GRP_BMI: "bmi", X.X86_GRP_BMI2: "bmi2", X.X86_GRP_FMA: "fma",
    X.X86_GRP_AVX512: "avx512", X.X86_GRP_FPU: "x87", X.X86_GRP_SSE1: "sse1", X.X86_GRP_SSE2: "sse2",
    X.X86_GRP_SSE3: "sse3", X.X86_GRP_SSSE3: "ssse3", X.X86_GRP_SSE41: "sse4.1", X.X86_GRP_SSE42: "sse4.2",
}
# Groups the task asks about (AVX, AVX2, AES, SHA, BMI2, PCLMUL). BMI1 and FMA are reported apart.
ISA_GROUPS = {"avx", "avx2", "aes", "sha", "bmi2", "pclmul"}

_FAM = {}
for _f, _names in [("a", "al ah ax eax rax"), ("b", "bl bh bx ebx rbx"), ("c", "cl ch cx ecx rcx"),
                   ("d", "dl dh dx edx rdx"), ("si", "sil si esi rsi"), ("di", "dil di edi rdi"),
                   ("bp", "bpl bp ebp rbp"), ("sp", "spl sp esp rsp"), ("ip", "rip eip ip")]:
    for _n in _names.split():
        _FAM[_n] = _f
for _i in range(8, 16):
    for _suf in ("", "d", "w", "b"):
        _FAM["r%d%s" % (_i, _suf)] = "r%d" % _i


def fam(name):
    return _FAM.get(name, name)


def in_mask(value, size):
    """Immediate as an unsigned value of the destination width (Capstone may sign-extend)."""
    if size in (1, 2, 4, 8):
        return value & ((1 << (8 * size)) - 1)
    return value


BRANCH_TERMINAL = ("ret", "iret", "ud2", "hlt", "int3", "int1", "retf")
# Mnemonics whose first operand is read, not written.
NO_DEST_WRITE = {"bt", "cmp", "test", "push", "call", "jmp", "ptest", "ldmxcsr", "nop", "ud2", "int3", "int",
                 "pause", "lfence", "mfence", "sfence", "out", "fxam", "comiss", "comisd", "ucomiss",
                 "ucomisd", "vcomiss", "vcomisd", "vucomiss", "vucomisd", "endbr64", "xtest"}

# ---- instruction records ---------------------------------------------------


class Ins:
    __slots__ = ("rva", "size", "mn", "text", "wr", "rd", "ops", "grp", "target")

    def __repr__(self):
        return "<%x %s %s>" % (self.rva, self.mn, self.text)

    @property
    def is_ret(self):
        return self.mn.startswith(("ret", "iret"))

    @property
    def is_jmp(self):
        return self.mn == "jmp"

    @property
    def is_jcc(self):
        return "jump" in self.grp and self.mn != "jmp"

    @property
    def is_call(self):
        return self.mn == "call"

    @property
    def is_indirect_jmp(self):
        return self.mn == "jmp" and self.ops and self.ops[0][0] != "i"

    @property
    def terminal(self):
        return self.mn in BRANCH_TERMINAL or (self.mn == "int" and self.ops and self.ops[0][0] == "i")


def make_ins(md, ci, names):
    """Capstone insn (detail mode) -> Ins. `names` caches reg id -> name."""
    def name(rid):
        n = names.get(rid)
        if n is None:
            n = md.reg_name(rid) or ""
            names[rid] = n
        return n

    ins = Ins()
    ins.rva = ci.address
    ins.size = ci.size
    ins.mn = ci.mnemonic
    ins.text = ci.op_str
    ops = []
    for op in ci.operands:
        if op.type == X.X86_OP_REG:
            ops.append(("r", fam(name(op.reg)), name(op.reg), op.size))
        elif op.type == X.X86_OP_IMM:
            ops.append(("i", op.imm, op.size))
        elif op.type == X.X86_OP_MEM:
            m = op.mem
            ops.append(("m", fam(name(m.base)) if m.base else None,
                        fam(name(m.index)) if m.index else None, m.scale, m.disp,
                        name(m.segment) if m.segment else None, name(m.base) if m.base else None,
                        op.size))
        else:
            ops.append(("o",))
    ins.ops = tuple(ops)
    # Capstone's regs_read / regs_write hold only the implicit registers (rax for cpuid, rdx for
    # mul). Explicit operands are added here: the first operand is the destination for most
    # mnemonics, and a source too unless the mnemonic is a pure write (mov, lea, set, pop, cvt).
    impl_w = {fam(name(r)) for r in ci.regs_write}
    impl_r = {fam(name(r)) for r in ci.regs_read}
    dest = ops[0] if ops and ops[0][0] == "r" else None
    writes_dest = dest is not None and ins.mn not in NO_DEST_WRITE and not ins.mn.startswith(
        ("cmp", "test", "ucomi", "comi", "vucomi", "vcomi", "ptest"))
    pure = ins.mn == "lea" or ins.mn.startswith(("mov", "set", "cvt", "pop"))
    zeroing = ins.mn in ("xor", "sub", "pxor", "vpxor", "vxorps", "xorps") and len(ops) == 2 \
        and ops[0][0] == "r" and ops[1][0] == "r" and ops[0][2] == ops[1][2]
    src = set()
    for i, o in enumerate(ops):
        if o[0] == "r":
            if i == 0 and (pure or zeroing):
                continue
            src.add(o[1])
        elif o[0] == "m":
            for f in (o[1], o[2]):
                if f:
                    src.add(f)
    if writes_dest:
        impl_w.add(dest[1])
    if ins.mn in ("xchg", "xadd") and len(ops) == 2 and ops[1][0] == "r":
        impl_w.add(ops[1][1])
    ins.wr = frozenset(impl_w)
    ins.rd = frozenset(src | impl_r)
    ins.grp = frozenset(GROUP_NAMES[g] for g in ci.groups if g in GROUP_NAMES)
    ins.target = None
    if ci.mnemonic in ("jmp", "call") or "jump" in ins.grp:
        if ops and ops[0][0] == "i":
            ins.target = ops[0][1] & 0xFFFFFFFFFFFFFFFF
    return ins


def rip_target(ins, op):
    """Absolute RVA addressed by a RIP-relative memory operand."""
    if op[6] == "rip":
        return ins.rva + ins.size + op[4]
    return None


# ---- sections, bytes and decoding -----------------------------------------


class Dis:
    def __init__(self, pe):
        self.pe = pe
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        self.md.detail = True
        self.names = {}
        self._cache = {}

    def span(self, begin, end):
        """Raw bytes for [begin, end), clipped to the section's file data."""
        s = self.pe.section_of(begin)
        if s is None or not s["rsize"]:
            return b""
        off = s["rptr"] + (begin - s["va"])
        stop = min(end, s["va"] + s["rsize"])
        return self.pe.data[off:off + max(0, stop - begin)]

    def bytes_at(self, rva, n=16):
        s = self.pe.section_of(rva)
        if s is None or not s["rsize"]:
            return b""
        off = s["rptr"] + (rva - s["va"])
        avail = s["rptr"] + s["rsize"] - off
        return self.pe.data[off:off + min(n, avail)]

    def at(self, rva):
        """Decode one instruction at rva (cached)."""
        if rva in self._cache:
            return self._cache[rva]
        buf = self.bytes_at(rva, 16)
        ins = None
        if buf:
            for ci in self.md.disasm(buf, rva, 1):
                ins = make_ins(self.md, ci, self.names)
        self._cache[rva] = ins
        return ins

    def linear(self, begin, end):
        """Census-style linear sweep over [begin, end) with one-byte resync."""
        buf = self.span(begin, end)
        out = []
        pos = 0
        while pos < len(buf):
            last = pos
            for ci in self.md.disasm(buf[pos:], begin + pos):
                out.append(make_ins(self.md, ci, self.names))
                last = (ci.address - begin) + ci.size
            if last >= len(buf):
                break
            pos = last + 1
        return out

    def cache_clear(self):
        self._cache.clear()


# ---- .pdata functions ------------------------------------------------------


def function_table(pe):
    """Return (entries, groups).

    entries: list of (begin, end, parent_begin) from .pdata, parent_begin == begin for
             a primary entry, else the begin of the function a split fragment belongs to.
    groups:  dict parent_begin -> sorted list of (begin, end) ranges of that function.
    """
    rva, size = pe.dirs[3]
    off = pe.rva_to_off(rva) if rva else None
    entries = []
    if off is None or pe.machine != 0x8664:
        return entries, {}
    for i in range(size // 12):
        b, e, u = struct.unpack_from("<III", pe.data, off + 12 * i)
        if e <= b:
            continue
        parent = b
        uoff = pe.rva_to_off(u & ~1)
        if uoff is not None and uoff + 4 <= len(pe.data):
            flags_ver, _, ncodes, _ = struct.unpack_from("<BBBB", pe.data, uoff)
            if (flags_ver >> 3) & 0x4:  # UNW_FLAG_CHAININFO
                chain = uoff + 4 + 2 * ((ncodes + 1) & ~1)
                parent, _, _ = struct.unpack_from("<III", pe.data, chain)
        entries.append((b, e, parent))
    groups = {}
    for b, e, p in entries:
        groups.setdefault(p, []).append((b, e))
    for p in groups:
        groups[p].sort()
    return sorted(entries), groups


class RangeIndex:
    """Map an RVA to the .pdata range that contains it."""

    def __init__(self, entries):
        self.entries = sorted(entries)
        self.starts = [e[0] for e in self.entries]

    def find(self, rva):
        """(begin, end, parent) of the range containing rva, or None."""
        i = bisect.bisect_right(self.starts, rva) - 1
        for j in range(i, max(-1, i - 8), -1):
            b, e, p = self.entries[j]
            if b <= rva < e:
                return b, e, p
        return None


# ---- recursive descent -----------------------------------------------------


def prev_linear(dis, rva, ranges, keep=40):
    """The last `keep` instructions before `rva`, from a linear sweep of the range that
    contains it (MSVC does not put data inside functions). Address order."""
    for b, e in ranges:
        if b <= rva < e:
            seq = [i for i in dis.linear(b, e) if i.rva < rva]
            return seq[-keep:]
    return []


# jump rva -> True when the switch's index bound (cmp/ja) was recovered; False when the table
# was read up to 256 entries and may over-run into data.
SWITCH_BOUNDED = {}


def resolve_switch(dis, jmp, ranges, span=40):
    """MSVC x64 switch: `cmp idx,N; ja; lea base,[rip+T]; movsxd r,[base+idx*4]; add r,base; jmp r`.
    Returns the list of case targets, or None when the pattern is not recognised."""
    seq = prev_linear(dis, jmp.rva, ranges)
    if not seq:
        return None
    # the register the jump reads
    if not jmp.ops or jmp.ops[0][0] != "r":
        return None
    reg = jmp.ops[0][1]
    add_base = None
    load = None
    for ins in reversed(seq):
        if load is None and add_base is None and ins.mn == "add" and reg in ins.wr and len(ins.ops) == 2 \
                and ins.ops[1][0] == "r":
            add_base = ins.ops[1][1]
            continue
        if add_base is not None and load is None and reg in ins.wr and ins.mn in ("movsxd", "mov", "movzx") \
                and ins.ops and ins.ops[1][0] == "m":
            load = ins
            break
    if load is None:
        return None
    m = load.ops[1]
    base_fam, idx_fam, scale = m[1], m[2], m[3]
    entry_size = scale or 4
    table = None
    count = None
    for ins in reversed(seq[:seq.index(load)]):
        if table is None and ins.mn == "lea" and base_fam in ins.wr and ins.ops[1][0] == "m" \
                and ins.ops[1][6] == "rip":
            table = ins.rva + ins.size + ins.ops[1][4]
        if count is None and ins.mn == "cmp" and idx_fam is not None and len(ins.ops) == 2 \
                and ins.ops[0][0] == "r" and ins.ops[0][1] == idx_fam and ins.ops[1][0] == "i":
            count = ins.ops[1][1] + 1
    if table is None:
        return None
    SWITCH_BOUNDED[jmp.rva] = count is not None and count <= 4096
    if count is None or count > 4096:
        count = 256  # unbounded: over-approximate, scan until the entry leaves the function
    targets = []
    lo = min(b for b, e in ranges)
    hi = max(e for b, e in ranges)
    for i in range(count):
        raw = dis.bytes_at(table + entry_size * i, entry_size)
        if len(raw) < entry_size:
            break
        if entry_size == 4:
            (rel,) = struct.unpack("<i", raw)
        else:
            break  # absolute 8-byte tables are not used by the MSVC switch layout
        t = table + rel
        if not (lo <= t < hi) or rel == 0:
            if count == 256:
                break
            continue
        targets.append(t)
    return targets


def descend(dis, ranges, starts):
    """Recursive descent over `ranges` (list of (begin, end)) from `starts`.

    Returns (insns, succ, unresolved). insns: rva -> Ins. succ: rva -> successor RVAs
    inside the ranges. unresolved: indirect jumps with no recognised switch pattern.
    """
    def inside(x):
        return any(b <= x < e for b, e in ranges)

    insns, succ, unresolved = {}, {}, []
    work = [s for s in starts if inside(s)]
    while work:
        a = work.pop()
        while inside(a) and a not in insns:
            ins = dis.at(a)
            if ins is None:
                break
            insns[a] = ins
            nxt = a + ins.size
            if ins.terminal or ins.is_ret:
                succ[a] = []
                break
            if ins.is_jmp and ins.target is not None:
                succ[a] = [ins.target]
                if inside(ins.target):
                    work.append(ins.target)
                break
            if ins.is_indirect_jmp:
                targets = resolve_switch(dis, ins, ranges)
                if targets:
                    succ[a] = targets
                    work.extend(t for t in targets if inside(t))
                else:
                    succ[a] = []
                    unresolved.append(a)
                break
            if ins.is_jcc and ins.target is not None:
                succ[a] = [nxt, ins.target]
                if inside(ins.target):
                    work.append(ins.target)
                a = nxt
                continue
            succ[a] = [nxt]
            a = nxt
    return insns, succ, unresolved


def preds(succ):
    p = {}
    for a, outs in succ.items():
        for b in outs:
            p.setdefault(b, []).append(a)
    return p


# ---- image helpers --------------------------------------------------------


def image_name(pe):
    return os.path.basename(pe.path)


def game_images(root):
    """The task's scope: game/bin/x64_retail/*.dll and game/server.dll, and nothing else.
    (find_pe_files walks the whole tree, which would also pick up stray files under game/r2.)"""
    out = []
    for p in find_pe_files(root):
        rel = os.path.relpath(p, root).replace(os.sep, "/")
        if (rel.startswith("bin/x64_retail/") and "/" not in rel[len("bin/x64_retail/"):]) or rel == "server.dll":
            out.append(p)
    return sorted(out)


def load(path):
    pe = PE(path)
    if pe.machine != 0x8664:
        return None
    return pe
