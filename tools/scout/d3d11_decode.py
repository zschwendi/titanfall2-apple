#!/usr/bin/env python3
"""Function-level decoding and control-flow graphs for the Direct3D 11 scout (issue #31).

Builds on xref_lib.Module (PE parsing, .pdata, chained unwind info).  For every function the
instructions are decoded once with Capstone's detail mode and reduced to small tuples, so the
flow analysis in d3d11_flow.py can run several passes cheaply.

Instruction record (all fields plain Python values):

    (addr, size, mn, ops, kills)

    mn     lower-case mnemonic
    ops    tuple of operands, each one of
             ('r', gpr_index, width_bits)         general register (0..15 in rax,rcx,rdx,rbx,rsp,rbp,rsi,rdi,r8..r15 order)
             ('x',)                               any other register (xmm, flags, segment)
             ('i', value)                         immediate
             ('m', base, index, scale, disp, size, flags)
                  base/index: gpr index, -1 for none, -2 for rip (then disp is the absolute RVA)
                  flags: 1 if a segment override (fs/gs) is present
    kills  tuple of gpr indices the instruction overwrites (explicit destinations and implicit
           ones), computed from Capstone's access flags; the flow code uses it for instructions
           it has no special handler for

Nothing here writes game bytes anywhere.
"""

import bisect
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import xref_lib  # noqa: E402

import capstone  # noqa: E402
from capstone import x86_const as X  # noqa: E402

GPR_NAMES = ["rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
             "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"]
RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI = range(8)
R8, R9, R10, R11, R12, R13, R14, R15 = range(8, 16)
ARG_REGS = (RCX, RDX, R8, R9)
VOLATILE = (RAX, RCX, RDX, R8, R9, R10, R11)

# capstone register id -> (gpr index, width)
REGMAP = {}


def _build_regmap():
    names = {
        64: GPR_NAMES,
        32: ["eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"] + ["r%dd" % i for i in range(8, 16)],
        16: ["ax", "cx", "dx", "bx", "sp", "bp", "si", "di"] + ["r%dw" % i for i in range(8, 16)],
        8: ["al", "cl", "dl", "bl", "spl", "bpl", "sil", "dil"] + ["r%db" % i for i in range(8, 16)],
    }
    for width, lst in names.items():
        for idx, nm in enumerate(lst):
            const = getattr(X, "X86_REG_" + nm.upper(), None)
            if const is not None:
                REGMAP[const] = (idx, width)
    for nm, idx in (("ah", 0), ("ch", 1), ("dh", 2), ("bh", 3)):
        REGMAP[getattr(X, "X86_REG_" + nm.upper())] = (idx, 8)


_build_regmap()
RIP = X.X86_REG_RIP

CS_AC_WRITE = 2


def _digest(ins):
    ops = []
    kills = []
    for op in ins.operands:
        t = op.type
        if t == X.X86_OP_REG:
            r = REGMAP.get(op.reg)
            if r:
                ops.append(("r", r[0], r[1]))
                if op.access & CS_AC_WRITE:
                    kills.append(r[0])
            else:
                ops.append(("x",))
        elif t == X.X86_OP_IMM:
            ops.append(("i", op.imm))
        elif t == X.X86_OP_MEM:
            m = op.mem
            seg = 1 if m.segment else 0
            if m.base == RIP:
                ops.append(("m", -2, -1, 1, ins.address + ins.size + m.disp, op.size, seg))
            else:
                b = REGMAP.get(m.base)
                i = REGMAP.get(m.index)
                ops.append(("m", b[0] if b and b[1] == 64 else (-1 if m.base == 0 else -3),
                            i[0] if i and i[1] == 64 else (-1 if m.index == 0 else -3),
                            m.scale, m.disp, op.size, seg))
        else:
            ops.append(("x",))
    try:
        for r in ins.regs_write:
            g = REGMAP.get(r)
            if g and g[0] not in kills:
                kills.append(g[0])
    except Exception:
        pass
    return (ins.address, ins.size, ins.mnemonic, tuple(ops), tuple(kills))


class Func:
    """One function: the primary .pdata entry plus its chained pieces, decoded and split into blocks."""

    __slots__ = ("begin", "ranges", "insns", "addr_index", "blocks", "block_of", "succ", "tables")

    def __init__(self, begin, ranges):
        self.begin = begin
        self.ranges = ranges
        self.insns = []
        self.addr_index = {}
        self.blocks = []      # list of (start_index, end_index) into insns
        self.block_of = []    # insn index -> block number
        self.succ = []        # per block: list of successor block numbers
        self.tables = {}      # jmp insn addr -> list of target addrs (resolved jump tables)

    def contains(self, a):
        for b, e in self.ranges:
            if b <= a < e:
                return True
        return False


_COND_JUMPS = None


def is_cond_jump(mn):
    return mn[0] == "j" and mn != "jmp"


class Decoder:
    """Decodes every function of one module on demand."""

    def __init__(self, mod, hints=()):
        """hints: addresses known to be function entries (direct call targets, vtable slots, code
        addresses taken with lea); they split unwind-less leaf functions that have no padding."""
        self.mod = mod
        self.hints = set(hints)
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        self.md.detail = True
        pm = mod.parent
        groups = {}
        for b, e, u in mod.entries:
            groups.setdefault(pm.get(b, b), []).append((b, e))
        self.groups = groups
        # leaf functions that have no unwind entry: gaps between .pdata ranges, split at int3 padding
        self.gap_funcs = self._find_gap_funcs()
        self.begins = sorted(set(groups) | set(self.gap_funcs))
        self._funcs = {}

    # ---- discovery ----------------------------------------------------------------------
    def _find_gap_funcs(self):
        mod = self.mod
        out = {}
        covered = sorted((b, e) for b, e, u in mod.entries)
        for s in mod.exec_secs:
            lo, hi = s["va"], s["va"] + min(s["vsize"], s["rsize"])
            cur = lo
            gaps = []
            for b, e in covered:
                if e <= lo or b >= hi:
                    continue
                if b > cur:
                    gaps.append((cur, b))
                cur = max(cur, e)
            if cur < hi:
                gaps.append((cur, hi))
            for gb, ge in gaps:
                buf = mod.read(gb, ge - gb)
                if not buf:
                    continue
                md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
                lst = []
                pos = 0
                n = len(buf)
                while pos < n:
                    got = False
                    for addr, size, mn, ops in md.disasm_lite(buf[pos:], gb + pos):
                        got = True
                        pos = addr - gb + size
                        lst.append((addr, size, mn, ops))
                    if pos < n and not got:
                        pos += 1
                targets = set()
                for addr, size, mn, ops in lst:
                    if (mn == "jmp" or (mn[0] == "j")) and ops.startswith("0x"):
                        targets.add(int(ops, 16))
                # a unit ends at int3 padding, or after ret/jmp when the next instruction is
                # 16-byte aligned and no branch inside the gap targets it
                cur = None
                prev_term = False
                for k, (addr, size, mn, ops) in enumerate(lst):
                    if mn == "int3":
                        if cur is not None:
                            out[cur] = [(cur, addr)]
                            cur = None
                        prev_term = False
                        continue
                    if cur is None:
                        if mn == "nop" or (mn == "xchg" and ops == "ax, ax"):
                            continue
                        cur = addr
                    elif prev_term and addr in self.hints:
                        out[cur] = [(cur, addr)]
                        cur = addr
                    prev_term = mn == "ret" or (mn == "jmp")
                if cur is not None:
                    last = lst[-1]
                    out[cur] = [(cur, last[0] + last[1])]
        return out

    def ranges_of(self, begin):
        return self.groups.get(begin) or self.gap_funcs.get(begin)

    def func_of(self, rva):
        """Primary start of the function that contains rva, or None."""
        f = self.mod.func_of(rva)
        if f is not None:
            return f
        i = bisect.bisect_right(self.begins, rva) - 1
        if i >= 0:
            b = self.begins[i]
            for rb, re_ in self.ranges_of(b) or []:
                if rb <= rva < re_:
                    return b
        return None

    # ---- decode + CFG ---------------------------------------------------------------------
    def func(self, begin):
        f = self._funcs.get(begin)
        if f is not None:
            return f
        ranges = sorted(self.ranges_of(begin) or [])
        f = Func(begin, ranges)
        for b, e in ranges:
            buf = self.mod.read(b, e - b)
            if not buf:
                continue
            pos = 0
            while pos < len(buf):
                got = False
                for ins in self.md.disasm(buf[pos:], b + pos):
                    f.insns.append(_digest(ins))
                    pos = ins.address - b + ins.size
                    got = True
                if pos < len(buf):
                    pos += 1  # undecodable byte: skip and resume
        f.insns.sort(key=lambda r: r[0])
        f.addr_index = {r[0]: i for i, r in enumerate(f.insns)}
        # jump tables: resolve them, then drop the table bytes that were decoded as code
        holes = []
        for k, (a, sz, mn, ops, kills) in enumerate(f.insns):
            if mn == "jmp" and ops and ops[0][0] == "r":
                jt = self._jump_table(f, k)
                if jt:
                    f.tables[a] = jt[0]
                    holes.append((jt[1], jt[2]))
        if holes:
            keep = [r for r in f.insns if not any(lo <= r[0] < hi for lo, hi in holes)]
            f.insns = keep
            f.addr_index = {r[0]: i for i, r in enumerate(f.insns)}
        self._build_cfg(f)
        self._funcs[begin] = f
        return f

    def drop(self, begin):
        self._funcs.pop(begin, None)

    def _jump_table(self, f, k):
        """Targets of the MSVC switch idiom ending at insns[k] (jmp reg), or None."""
        insns = f.insns
        mod = self.mod
        # look back for: mov r32, [base + idx*4 + table_rva]
        tbl = None
        for j in range(k - 1, max(k - 10, -1), -1):
            a, sz, mn, ops, kills = insns[j]
            if mn == "mov" and len(ops) == 2 and ops[1][0] == "m":
                m = ops[1]
                if m[3] == 4 and m[2] >= 0 and m[1] >= 0 and m[4] > 0x1000 and m[5] == 4:
                    tbl = m[4]
                    break
        if tbl is None:
            return None
        count = None
        for j in range(k - 1, max(k - 24, -1), -1):
            a, sz, mn, ops, kills = insns[j]
            if mn == "cmp" and len(ops) == 2 and ops[1][0] == "i" and ops[0][0] == "r":
                count = ops[1][1] + 1
                break
        targets = []
        n = count if count is not None and 0 < count <= 1024 else 512
        for i in range(n):
            v = mod.u32(tbl + 4 * i)
            if v is None or not f.contains(v):
                if count is None:
                    break
                continue
            targets.append(v)
        return (targets, tbl, tbl + 4 * n) if targets else None

    def _build_cfg(self, f):
        insns = f.insns
        n = len(insns)
        leaders = {0} if n else set()
        succ_addrs = {}  # index -> list of target addrs for terminators
        for k, (a, sz, mn, ops, kills) in enumerate(insns):
            if mn == "jmp":
                if ops and ops[0][0] == "i":
                    t = ops[0][1]
                    if f.contains(t):
                        leaders.add(f.addr_index.get(t, 0) if t in f.addr_index else 0)
                        succ_addrs[k] = [t]
                    else:
                        succ_addrs[k] = []  # tail call
                elif ops and ops[0][0] == "r":
                    tg = f.tables.get(a)
                    if tg:
                        succ_addrs[k] = tg
                        for t in tg:
                            if t in f.addr_index:
                                leaders.add(f.addr_index[t])
                    else:
                        succ_addrs[k] = []
                else:
                    succ_addrs[k] = []
                if k + 1 < n:
                    leaders.add(k + 1)
            elif is_cond_jump(mn) and ops and ops[0][0] == "i":
                t = ops[0][1]
                if t in f.addr_index:
                    leaders.add(f.addr_index[t])
                succ_addrs[k] = [t]
                if k + 1 < n:
                    leaders.add(k + 1)
            elif mn in ("ret", "retf", "ud2", "int3", "hlt") or (mn == "ret"):
                succ_addrs[k] = []
                if k + 1 < n:
                    leaders.add(k + 1)
        starts = sorted(leaders)
        f.blocks = []
        f.block_of = [0] * n
        for bi, s in enumerate(starts):
            e = starts[bi + 1] if bi + 1 < len(starts) else n
            f.blocks.append((s, e))
            for k in range(s, e):
                f.block_of[k] = bi
        start_to_block = {s: bi for bi, s in enumerate(starts)}
        f.succ = []
        for bi, (s, e) in enumerate(f.blocks):
            last = e - 1
            a, sz, mn, ops, kills = insns[last]
            nxt = []
            if last in succ_addrs:
                for t in succ_addrs[last]:
                    k = f.addr_index.get(t)
                    if k is not None and k in start_to_block:
                        nxt.append(start_to_block[k])
                if is_cond_jump(mn) and e < n:
                    nxt.append(start_to_block[e])
            elif e < n:
                nxt.append(start_to_block[e])  # fall through
            f.succ.append(sorted(set(nxt)))


def load_module(path):
    return xref_lib.Module(path)
