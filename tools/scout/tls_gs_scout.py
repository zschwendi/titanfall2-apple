#!/usr/bin/env python3
"""Thread-local-state scout for the game's DLLs.

Questions answered, per module (server.dll and the 23 DLLs in game/bin/x64_retail other than
d3dcompiler_46.dll):

  1. Which gs: memory operands are code, and what the register-based ones compute.
  2. For each reached gs:[0x58] (TLS array) access: does it follow the MSVC static-TLS pattern
     (load the TLS array, index by _tls_index, add a constant offset), and which block offsets it touches.
  3. The TLS directory: template size, zero fill, callbacks, RVA of _tls_index.
  4. The gs:[0x10] (StackLimit) accesses: stack probes, and their callers.
  5. Call sites of the dynamic TLS/FLS APIs, from the import tables (through the import thunks).

    .venv/bin/python tools/scout/tls_gs_scout.py game [name-substring]

Method. A linear decode of a function range is not enough: MSVC places jump tables and constants
after a function's last instruction, inside its unwind range, and the decoder happily decodes them as
instructions. The scout uses these steps per module:

  - resync sweep: decode linearly from each unwind-table function start; at an undecodable byte (or an
    instruction that runs past the range end), skip one byte and keep going. Every gs: memory operand found
    is a candidate. The total over the 24 modules is the 277 figure. Candidates found before the first
    failure are "in sequence" (run 0); later ones are "after decode failure" (run > 0).
  - reachability: decode from entry points, following fall-through, direct branches and switch tables
    (MSVC idioms, with the table base taken from lea/rip values). Entry points are every unwind-table
    function start, plus code that is called, exported, pointed to by a data relocation, or reached through
    a call from code already reached (functions with no unwind entry). A candidate counts as code only if it
    is reached. Register-based (computed) operands count only when reached from an entry that something
    actually calls, exports or points to: a bogus unwind range that starts inside data is not code.
  - an unreached candidate whose function contains an unresolved register jump is "unproven" rather than data.

Addresses are RVAs. Byte-level cross-checks (raw scans for gs-prefixed absolute addressing and for
"call/jmp [rip+slot]" to import slots) do not depend on decoding at all.

Runs one module per worker, four workers at most. A full run takes several minutes.
"""

import bisect
import os
import struct
import sys
from collections import Counter, deque
from multiprocessing import Pool

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "inventory"))
from pe_inventory import PE  # noqa: E402

import capstone  # noqa: E402
from capstone import x86_const as X  # noqa: E402

EXCLUDE = {"d3dcompiler_46.dll"}
TERMINATORS = {"ret", "retf", "int3", "int", "ud2", "hlt", "iretq", "sysret"}
CALLER_SAVED = {"rax", "rcx", "rdx", "r8", "r9", "r10", "r11"}
TEB_NAMES = {0x08: "StackBase", 0x10: "StackLimit", 0x30: "TEB self pointer",
             0x58: "ThreadLocalStoragePointer (TLS array)", 0x60: "PEB pointer"}

# Map sub-register names to their 64-bit register.
SUB = {}


def _alias(full, *names):
    for n in names:
        SUB[n] = full


for _f, _e, _w, _l, _h in [("rax", "eax", "ax", "al", "ah"), ("rbx", "ebx", "bx", "bl", "bh"),
                           ("rcx", "ecx", "cx", "cl", "ch"), ("rdx", "edx", "dx", "dl", "dh")]:
    _alias(_f, _f, _e, _w, _l, _h)
for _f, _e, _w, _l in [("rsi", "esi", "si", "sil"), ("rdi", "edi", "di", "dil"),
                       ("rbp", "ebp", "bp", "bpl"), ("rsp", "esp", "sp", "spl")]:
    _alias(_f, _f, _e, _w, _l)
for _i in range(8, 16):
    _alias("r%d" % _i, "r%d" % _i, "r%dd" % _i, "r%dw" % _i, "r%db" % _i)


def canon(name):
    if name is None:
        return None
    return SUB.get(name, name)


class Ins:
    """Light record of a decoded instruction, kept for the reachable code only."""
    __slots__ = ("addr", "size", "mnem", "opstr", "ops", "w", "prev")


NO_WRITE = {"cmp", "test", "push", "call", "jmp", "ret", "retf", "nop", "bt", "ucomiss", "ucomisd",
            "comiss", "comisd", "int3", "int", "ud2", "hlt", "lfence", "sfence", "mfence", "pause",
            "endbr64", "prefetcht0", "prefetcht1", "prefetchnta", "prefetchw", "cmc", "stc", "clc",
            "std", "cld", "fcomi", "fucomi"}


def explicit_writes(mnem, ops, implicit):
    """Registers written: capstone's implicit list plus the destination operand (operand 0)."""
    w = set(implicit)
    if mnem[0] != "j" and mnem not in NO_WRITE and ops and ops[0][0] == "reg":
        w.add(ops[0][1])
    return frozenset(w)


def make_ins(ins, prev):
    """ops: ("reg", name) | ("imm", value) | ("mem", seg, base, index, scale, disp, size, rip_target)."""
    ops = []
    for op in ins.operands:
        if op.type == X.X86_OP_REG:
            ops.append(("reg", canon(ins.reg_name(op.reg))))
        elif op.type == X.X86_OP_IMM:
            ops.append(("imm", op.imm))
        elif op.type == X.X86_OP_MEM:
            m = op.mem
            base = canon(ins.reg_name(m.base)) if m.base else None
            index = canon(ins.reg_name(m.index)) if m.index else None
            seg = ins.reg_name(m.segment) if m.segment else None
            target = ins.address + ins.size + m.disp if base == "rip" else None
            ops.append(("mem", seg, base, index, m.scale, m.disp, op.size, target))
        else:
            ops.append(("other",))
    r = Ins()
    r.addr, r.size, r.mnem, r.opstr = ins.address, ins.size, ins.mnemonic, ins.op_str
    r.ops = ops
    r.w = explicit_writes(r.mnem, ops, (canon(ins.reg_name(x)) for x in ins.regs_write))
    r.prev = prev
    return r


def gs_mem(ins):
    for op in ins.operands:
        if op.type == X.X86_OP_MEM and op.mem.segment == X.X86_REG_GS:
            return op.mem
    return None


def first_gs(ops):
    for op in ops:
        if op[0] == "mem" and op[1] == "gs":
            return op
    return None


class Module:
    def __init__(self, path):
        self.path = path
        self.name = os.path.basename(path)
        self.pe = PE(path)
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        self.md.detail = True
        self.mem = []         # (va, end, bytes) for every section with raw data
        self.exec_mem = []    # (va, end, bytes) for executable sections
        for s in self.pe.sections:
            if s["rsize"] == 0:
                continue
            buf = self.pe.data[s["rptr"]:s["rptr"] + s["rsize"]]
            self.mem.append((s["va"], s["va"] + s["rsize"], buf))
            if self.pe.is_exec(s):
                self.exec_mem.append((s["va"], s["va"] + s["rsize"], buf))
        self.code_top = max(hi for _, hi, _ in self.exec_mem)

    def is_code(self, rva):
        return any(lo <= rva < hi for lo, hi, _ in self.exec_mem)

    def fetch(self, rva, n):
        for lo, hi, buf in self.mem:
            if lo <= rva < hi:
                i = rva - lo
                return buf[i:i + n]
        return b""

    def raw(self, rva):
        b = self.fetch(rva, 15)
        if not b:
            return None
        for ins in self.md.disasm(b, rva, 1):
            return ins
        return None

    def decode(self, rva, prev=None):
        ins = self.raw(rva)
        return None if ins is None else make_ins(ins, prev)


def pdata_functions(pe):
    rva, _ = pe.dirs[3]
    off = pe.rva_to_off(rva) if rva else None
    out = set()
    if off is None:
        return []
    n = pe.dirs[3][1] // 12
    for i in range(n):
        b, e, _ = struct.unpack_from("<III", pe.data, off + 12 * i)
        if e > b:
            out.add((b, e))
    return sorted(out)


# ---- sweep 1: resync candidates ---------------------------------------------

def cand_info(ins, begin, end, run, prev_raw):
    m = gs_mem(ins)
    return {
        "addr": ins.address, "func": begin, "func_end": end, "run": run,
        "text": ins.mnemonic + " " + ins.op_str,
        "base": canon(ins.reg_name(m.base)) if m.base else None,
        "index": canon(ins.reg_name(m.index)) if m.index else None,
        "scale": m.scale, "disp": m.disp,
        "prev": [(p.address, p.mnemonic + " " + p.op_str, make_ins(p, None).w) for p in prev_raw],
        "reached": False,
    }


def resync(mod, begin, end, cands):
    pos, run = begin, 0
    while pos < end:
        a = pos
        prev = deque(maxlen=8)
        while a < end:
            ins = mod.raw(a)
            if ins is None or a + ins.size > end:
                break
            if gs_mem(ins) is not None and a not in cands:
                cands[a] = cand_info(ins, begin, end, run, list(prev))
            prev.append(ins)
            a += ins.size
        pos = a + 1
        run += 1


# ---- sweep 2: reachability ---------------------------------------------------

def _find(chain, start, pred):
    for k in range(start, len(chain)):
        if pred(chain[k]):
            return k
    return None


def _value_of(chain, start, reg, depth=0):
    """Constant value of reg at the point just before chain[start-1], from the nearest older write.

    lea reg, [rip+X] gives the RVA X (the MSVC switch idiom uses lea of a rip-relative target that
    is the image base, so the value is often 0)."""
    if reg is None or depth > 4:
        return None
    for k in range(start, len(chain)):
        i = chain[k]
        if reg not in i.w:
            continue
        if i.mnem == "lea" and len(i.ops) == 2 and i.ops[0] == ("reg", reg) and i.ops[1][0] == "mem" \
                and i.ops[1][2] == "rip" and i.ops[1][3] is None:
            return i.ops[1][7]
        if i.mnem == "mov" and len(i.ops) == 2 and i.ops[0] == ("reg", reg):
            if i.ops[1][0] == "reg":
                return _value_of(chain, k + 1, i.ops[1][1], depth + 1)
            if i.ops[1][0] == "imm":
                return i.ops[1][1]
        if i.mnem == "xor" and len(i.ops) == 2 and i.ops[0] == ("reg", reg) and i.ops[0] == i.ops[1]:
            return 0
        return None
    return None


def _value_of_addr(seen, before, reg):
    """Same as _value_of, but searches the whole function: the last write to reg before 'before'."""
    for k in range(before - 1, before - 4096, -1):
        i = seen.get(k)
        if i is None or reg not in i.w:
            continue
        if i.mnem == "lea" and len(i.ops) == 2 and i.ops[0] == ("reg", reg) and i.ops[1][0] == "mem" \
                and i.ops[1][2] == "rip" and i.ops[1][3] is None:
            return i.ops[1][7]
        if i.mnem == "xor" and len(i.ops) == 2 and i.ops[0] == ("reg", reg) and i.ops[0] == i.ops[1]:
            return 0
        return None
    return None


def _bound(chain, start, reg):
    """Case count from 'cmp reg, imm' followed by ja/jae (the default branch) in address order."""
    for k in range(start, len(chain)):
        i = chain[k]
        if i.mnem == "cmp" and len(i.ops) == 2 and i.ops[0] == ("reg", reg) and i.ops[1][0] == "imm" \
                and 0 <= i.ops[1][1] < 0x1000 and k >= 1:
            nxt = chain[k - 1].mnem
            if nxt in ("ja", "jnbe"):
                return i.ops[1][1] + 1
            if nxt in ("jae", "jnb"):
                return i.ops[1][1]
    return None


def switch_targets(mod, seen, a, begin, end):
    """Targets of 'jmp reg' that ends an MSVC switch table, or None if the idiom is not recognised.

    Idiom (x64, MSVC), one level:  cmp idx, N-1 ; ja default ; lea B, [rip+X] ;
                                   mov R, dword [B+idx*4+D] ; add R, S ; jmp R
    where S and B hold the image base or the table base, so target = value(S) + entry.
    Byte-indexed form: movzx idx, byte [B2+idx2+D2] sits between the cmp and the dword load; the
    byte table maps each case to a dword slot, and the case count comes from the cmp on idx2.
    Predecessors are taken by address (fall-through only), so a case block that starts a new run
    still sees the cmp and the table setup before it.
    """
    pred = {}
    for k, v in seen.items():
        pred[v.addr + v.size] = k
    chain = []
    cur = a
    while cur is not None and cur in seen and len(chain) < 40:
        chain.append(seen[cur])
        nxt = pred.get(cur)
        if nxt is not None and seen[nxt].mnem in TERMINATORS | {"jmp"}:
            nxt = None
        cur = nxt
    jmp = chain[0]
    if not jmp.ops or jmp.ops[0][0] != "reg":
        return None
    jreg = jmp.ops[0][1]

    def value(start, reg):
        v = _value_of(chain, start, reg)
        return v if v is not None else _value_of_addr(seen, a, reg)

    p = _find(chain, 1, lambda i: i.mnem == "add" and len(i.ops) == 2 and i.ops[0] == ("reg", jreg)
              and i.ops[1][0] == "reg")
    if p is None:
        return None
    v_add = value(p + 1, chain[p].ops[1][1])
    q = _find(chain, p + 1, lambda i: i.mnem in ("mov", "movsxd") and len(i.ops) == 2
              and i.ops[0] == ("reg", jreg) and i.ops[1][0] == "mem" and i.ops[1][6] == 4
              and i.ops[1][4] == 4 and i.ops[1][2] and i.ops[1][3])
    if q is None or v_add is None:
        return None
    b1, i1, d1 = _base_index(chain[q].ops[1], q, chain, value, 4)
    if b1 is None:
        return None
    v1 = value(q + 1, b1)
    if v1 is None:
        return None
    table1 = v1 + d1
    r = _find(chain, q + 1, lambda i: len(i.ops) == 2 and i.ops[0][0] == "reg" and i.ops[1][0] == "mem"
              and i.ops[1][6] == 1 and i.ops[1][2] and i.ops[1][3])
    if r is None:
        n = _bound(chain, q + 1, i1)
        if n is None or not (0 < n <= 4096):
            return None
        data = mod.fetch(table1, 4 * n)
        if len(data) < 4 * n:
            return None
        entries = [struct_i32(data, 4 * k) for k in range(n)]
    else:
        b2, i2, d2 = _base_index(chain[r].ops[1], r, chain, value, 1)
        if b2 is None:
            return None
        v2 = value(r + 1, b2)
        if v2 is None:
            return None
        n = _bound(chain, r + 1, i2)
        if n is None or not (0 < n <= 4096):
            return None
        bts = mod.fetch(v2 + d2, n)
        if len(bts) < n:
            return None
        slots = sorted(set(bts))
        data = mod.fetch(table1, 4 * (max(slots) + 1))
        if len(data) < 4 * (max(slots) + 1):
            return None
        entries = [struct_i32(data, 4 * k) for k in slots]
    targets = []
    for e in entries:
        t = v_add + e
        if begin <= t < end:
            targets.append(t)
    return targets


def _base_index(mem, k, chain, value, scale):
    """Split [x + y*scale + d] into (table base, index, disp): the operand with a known value is the base."""
    _, _, b, idx, sc, d, _, _ = mem
    if sc != scale and scale == 4:
        return None, None, None
    vb = value(k + 1, b)
    vi = value(k + 1, idx)
    if vb is not None:
        return b, idx, d
    if vi is not None and scale == 1:
        return idx, b, d
    return b, idx, d


def struct_i32(data, off):
    return struct.unpack_from("<i", data, off)[0]



def reach(mod, begin, end, st, unresolved_sites):
    seen = {}
    switch_cases = []
    work = [begin]
    while work:
        a = work.pop()
        prev = None
        while begin <= a < end and a not in seen:
            ins = mod.decode(a, prev)
            if ins is None or a + ins.size > end:
                break
            seen[a] = ins
            prev = a
            m, ops = ins.mnem, ins.ops
            if m in TERMINATORS:
                break
            if m == "jmp":
                if ops and ops[0][0] == "imm":
                    work.append(ops[0][1])
                elif ops and ops[0][0] == "mem":
                    st["jmp_mem"] += 1          # tail jump through a pointer (vtable thunk, etc.)
                else:
                    t = switch_targets(mod, seen, a, begin, end)
                    if t is None:
                        st["jmp_reg_unresolved"] += 1
                        unresolved_sites.append(a)
                    else:
                        st["switch_resolved"] += 1
                        st["switch_cases"] += len(t)
                        work.extend(t)
                        switch_cases.extend(t)
                break
            if m[0] == "j" and ops and ops[0][0] == "imm":
                work.append(ops[0][1])
            a += ins.size
    # every switch case must be an instruction the walk reached, or the table reading was wrong
    st["switch_case_reached"] += sum(1 for t in switch_cases if t in seen)
    return seen


def _eval_mem(mem, derived, consts):
    """Address of a memory operand as ('D', offset from the TLS block) or ('C', constant), or None.

    Exactly one register may be block-derived, with scale 1; the other registers must be known constants."""
    _, seg, b, i, sc, d, _, _ = mem
    if seg is not None:
        return None
    off, nd = d, 0
    for reg, scale in ((b, 1), (i, sc)):
        if reg is None:
            continue
        if reg in derived:
            if scale != 1:
                return None
            off += derived[reg]
            nd += 1
        elif reg in consts:
            off += consts[reg] * scale
        else:
            return None
    if nd == 0:
        return ("C", off)
    if nd == 1:
        return ("D", off)
    return None


def analyze_tls58(seen, a, tls_idx):
    """Match the MSVC static-TLS access pattern starting at mov R, gs:[0x58].

    Walks forward from the gs load, tracking which registers hold (block pointer + offset) and which hold
    constants, and records every memory operand that addresses the block."""
    i0 = seen[a]
    res = {"addr": a, "std": False, "idx_match": False, "idx_rva": None, "load": None,
           "direct": [], "indexed": [], "taken": [], "why": ""}
    if not (i0.ops and i0.ops[0][0] == "reg"):
        res["why"] = "destination is not a register"
        return res
    dst = i0.ops[0][1]
    # block-pointer load: mov B, [R + Ri*8] within 8 instructions
    load, ri, breg = None, None, None
    cur = a + i0.size
    for _ in range(8):
        ins = seen.get(cur)
        if ins is None:
            break
        if ins.mnem == "mov" and len(ins.ops) == 2 and ins.ops[0][0] == "reg" and ins.ops[1][0] == "mem":
            m = ins.ops[1]
            if m[1] is None and m[4] == 8 and m[5] == 0 and m[2] and m[3] and m[2] != "rip" and m[3] != "rip":
                other = m[3] if m[2] == dst else (m[2] if m[3] == dst else None)
                if other:
                    load, ri, breg = ins, other, ins.ops[0][1]
                    break
        cur += ins.size
    if load is None:
        res["why"] = "no [R+idx*8] block load within 8 instructions"
        return res
    res["load"] = load.addr
    # the index register must come from [rip+X]; the cmp/mov pair before the block load can be a few steps back
    pool = []
    cur = load.prev
    for _ in range(10):
        ins = seen.get(cur) if cur is not None else None
        if ins is None:
            break
        pool.append(ins)
        cur = ins.prev
    cur = a + i0.size
    while cur in seen and cur != load.addr and len(pool) < 40:
        pool.append(seen[cur])
        cur += seen[cur].size
    idx_ins = None
    for ins in pool:
        if (ins.mnem in ("mov", "movsxd", "movsx", "movzx") and len(ins.ops) == 2
                and ins.ops[0] == ("reg", ri) and ins.ops[1][0] == "mem" and ins.ops[1][2] == "rip"):
            idx_ins = ins
            break
    if idx_ins is None:
        res["why"] = "index register %s not loaded from [rip+X] nearby" % ri
    else:
        res["idx_rva"] = idx_ins.ops[1][7]
        res["idx_match"] = idx_ins.ops[1][7] == tls_idx
    res["std"] = res["idx_match"]

    derived, consts = {}, {}
    cur = a
    for _ in range(80):
        ins = seen.get(cur)
        if ins is None:
            break
        m, ops = ins.mnem, ins.ops
        if cur == load.addr:
            for r in ins.w:
                derived.pop(r, None)
                consts.pop(r, None)
            derived[breg] = 0
            cur += ins.size
            continue
        newd = newc = None
        for op in ops:
            if op[0] != "mem" or op[1] is not None:
                continue
            if not ({op[2], op[3]} & set(derived)):
                continue
            ev = _eval_mem(op, derived, consts)
            if ev and ev[0] == "D":
                res["direct"].append((ev[1], m, ins.addr))
            else:
                res["indexed"].append((None, m, ins.addr))
        if m == "mov" and len(ops) == 2 and ops[0][0] == "reg":
            d = ops[0][1]
            if ops[1][0] == "imm":
                newc = (d, ops[1][1])
            elif ops[1][0] == "reg":
                s_ = ops[1][1]
                if s_ in derived:
                    newd = (d, derived[s_])
                elif s_ in consts:
                    newc = (d, consts[s_])
        elif m == "lea" and len(ops) == 2 and ops[0][0] == "reg" and ops[1][0] == "mem":
            ev = _eval_mem(ops[1], derived, consts)
            if ev and ev[0] == "D":
                newd = (ops[0][1], ev[1])
                res["taken"].append(ev[1])
            elif ev and ev[0] == "C":
                newc = (ops[0][1], ev[1])
        elif m in ("add", "sub") and len(ops) == 2 and ops[0][0] == "reg":
            d, src, sign = ops[0][1], ops[1], (1 if m == "add" else -1)
            if src[0] == "imm":
                if d in derived:
                    newd = (d, derived[d] + sign * src[1])
                elif d in consts:
                    newc = (d, consts[d] + sign * src[1])
            elif src[0] == "reg":
                s_ = src[1]
                if d in derived and s_ in consts:
                    newd = (d, derived[d] + sign * consts[s_])
                elif d in consts and s_ in derived and sign == 1:
                    newd = (d, derived[s_] + consts[d])
                elif d in consts and s_ in consts:
                    newc = (d, consts[d] + sign * consts[s_])
        elif m == "xor" and len(ops) == 2 and ops[0][0] == "reg" and ops[0] == ops[1]:
            newc = (ops[0][1], 0)
        if m == "mov" and len(ops) == 2 and ops[0][0] == "mem" and ops[1][0] == "reg" and ops[1][1] in derived:
            res["taken"].append(derived[ops[1][1]])
        for r in ins.w:
            if not (newd and newd[0] == r) and not (newc and newc[0] == r):
                derived.pop(r, None)
                consts.pop(r, None)
        if newd:
            derived[newd[0]] = newd[1]
            consts.pop(newd[0], None)
        if newc:
            consts[newc[0]] = newc[1]
            derived.pop(newc[0], None)
        if m == "call":
            for r in CALLER_SAVED:
                derived.pop(r, None)
                consts.pop(r, None)
        if m in TERMINATORS or m == "jmp":
            break
        cur += ins.size
    return res


def classify_gs(seen, a):
    """Classify a reached gs: operand: 'fixed' or the computed offset, if it can be worked out."""
    ins = seen[a]
    g = first_gs(ins.ops)
    base, index, disp = g[2], g[3], g[5]
    if base is None and index is None:
        return "fixed", disp, None
    if index is None and base:
        cur, k = ins.prev, 0
        while cur is not None and cur in seen and k < 8:
            p = seen[cur]
            if base in p.w:
                if p.mnem == "mov" and p.ops and p.ops[0][0] == "reg" and len(p.ops) == 2 and p.ops[1][0] == "imm":
                    v = (p.ops[1][1] + disp) & 0xFFFFFFFFFFFFFFFF
                    return "computed", v, "base=%#x set by '%s %s'" % (p.ops[1][1], p.mnem, p.opstr)
                if p.mnem == "xor" and len(p.ops) == 2 and p.ops[0] == p.ops[1]:
                    return "computed", disp, "base=0 by '%s %s'" % (p.mnem, p.opstr)
                return "computed", None, "base written by '%s %s' (not evaluable)" % (p.mnem, p.opstr)
            cur, k = p.prev, k + 1
        return "computed", None, "base not written in the 8 preceding decoded instructions"
    return "computed", None, "indexed (index %s): not evaluable" % index


def analyze_fixed(seen, a, disp):
    ins = seen[a]
    if disp == 0x10:
        R = ins.ops[0][1] if ins.ops and ins.ops[0][0] == "reg" else None
        res = {"reg": R, "cmp": False, "jcc": None}
        cur = a + ins.size
        for _ in range(6):
            nx = seen.get(cur)
            if nx is None:
                break
            if nx.mnem == "cmp" and any(op == ("reg", R) for op in nx.ops):
                res["cmp"] = True
                after = seen.get(cur + nx.size)
                if after is not None and after.mnem[0] == "j":
                    res["jcc"] = after.mnem
                break
            cur += nx.size
        return res
    if disp == 0x30:
        nxt = seen.get(a + ins.size)
        return {"next": (nxt.mnem + " " + nxt.opstr) if nxt else None}
    return {}


def import_slots(pe):
    """IAT slot RVA -> (dll, name) for every named import, including delay imports."""
    out = {}

    def walk(dir_idx, delay):
        rva, _ = pe.dirs[dir_idx]
        off = pe.rva_to_off(rva) if rva else None
        if off is None:
            return
        step = 32 if delay else 20
        while off + step <= len(pe.data):
            if delay:
                attrs, name_rva, _, iat_rva, int_rva, _, _, _ = struct.unpack_from("<IIIIIIII", pe.data, off)
                if not (name_rva or int_rva):
                    break
                ilt_rva = int_rva
            else:
                oft, _, _, name_rva, ft = struct.unpack_from("<IIIII", pe.data, off)
                if not (oft or name_rva or ft):
                    break
                ilt_rva, iat_rva = (oft or ft), ft
            dll = (pe.cstr(name_rva) or "?").lower()
            ilt_off = pe.rva_to_off(ilt_rva)
            i = 0
            while ilt_off is not None and ilt_off + 8 * (i + 1) <= len(pe.data):
                (v,) = struct.unpack_from("<Q", pe.data, ilt_off + 8 * i)
                if v == 0:
                    break
                if not (v >> 63):
                    nm = pe.cstr((v & 0x7FFFFFFF) + 2) or "?"
                    out[iat_rva + 8 * i] = (dll, nm)
                i += 1
            off += step

    walk(1, False)
    walk(13, True)
    return out


def tls_directory(pe):
    rva, _ = pe.dirs[9]
    if not rva:
        return None
    off = pe.rva_to_off(rva)
    sr, er, idx, cb, zf, ch = struct.unpack_from("<QQQQII", pe.data, off)
    ib = pe.image_base
    start, end, idx_rva = sr - ib, er - ib, idx - ib
    ncb = 0
    if cb:
        coff = pe.rva_to_off(cb - ib)
        while coff is not None and coff + 8 <= len(pe.data):
            (v,) = struct.unpack_from("<Q", pe.data, coff)
            if v == 0:
                break
            ncb += 1
            coff += 8
    toff = pe.rva_to_off(start)
    tmpl = end - start
    tbytes = pe.data[toff:toff + tmpl] if toff is not None else b""
    nonzero = sum(1 for b in tbytes if b)
    return {"template": tmpl, "zerofill": zf, "characteristics": ch, "callbacks": ncb,
            "index_rva": idx_rva, "start_rva": start, "end_rva": end,
            "index_in_file": pe.rva_to_off(idx_rva) is not None,
            "index_section": (pe.section_of(idx_rva) or {}).get("name"),
            "template_section": (pe.section_of(start) or {}).get("name"),
            "template_nonzero_bytes": nonzero, "dir_size": pe.dirs[9][1]}


def gs_abs_positions(mod, disp):
    """Raw byte scan: RVAs of gs-prefixed absolute addressing (SIB no base, no index) with this disp32."""
    want = struct.pack("<I", disp)
    out = []
    for lo, _, buf in mod.exec_mem:
        p = buf.find(b"\x65")
        while p != -1:
            q = p + 1
            if q + 8 < len(buf):
                if 0x40 <= buf[q] <= 0x4F:
                    q += 1
                q += 2 if buf[q] == 0x0F else 1
                if (buf[q] & 0xC7) == 0x04 and buf[q + 1] == 0x25 and buf[q + 2:q + 6] == want:
                    out.append(lo + p)
            p = buf.find(b"\x65", p + 1)
    return out


def call_slot_count(mod, slots):
    c = Counter()
    for lo, _, buf in mod.exec_mem:
        p = buf.find(b"\xff\x15")
        while p != -1:
            if p + 6 <= len(buf):
                (d,) = struct.unpack_from("<i", buf, p + 2)
                t = lo + p + 6 + d
                if t in slots:
                    c[t] += 1
            p = buf.find(b"\xff\x15", p + 1)
    return c


def _cover_factory(funcs):
    starts = [b for b, _ in funcs]
    ends = [e for _, e in funcs]

    def covered(x):
        i = bisect.bisect_right(starts, x) - 1
        for j in range(i, max(-1, i - 8), -1):
            if j >= 0 and ends[j] > x:
                return True
        return False

    def next_start_after(x):
        i = bisect.bisect_right(starts, x)
        return starts[i] if i < len(starts) else None

    return covered, next_start_after


def reloc_code_pointers(mod):
    """RVAs of code addresses stored as 64-bit pointers (DIR64 base relocations): function pointers,
    vtables. These are entry points that no direct call reaches."""
    pe = mod.pe
    rva, size = pe.dirs[5]
    off = pe.rva_to_off(rva) if rva else None
    out = set()
    if off is None:
        return out
    end = off + size
    while off + 8 <= end and off + 8 <= len(pe.data):
        page, block = struct.unpack_from("<II", pe.data, off)
        if block < 8:
            break
        for k in range((block - 8) // 2):
            (e,) = struct.unpack_from("<H", pe.data, off + 8 + 2 * k)
            if e >> 12 == 10:
                lo = pe.rva_to_off(page + (e & 0xFFF))
                if lo is not None and lo + 8 <= len(pe.data):
                    (v,) = struct.unpack_from("<Q", pe.data, lo)
                    t = v - pe.image_base
                    if mod.is_code(t):
                        out.add(t)
        off += block
    return out


def export_entries(mod):
    """Code addresses of exported functions (forwarders excluded): entry points for other modules."""
    pe = mod.pe
    rva, size = pe.dirs[0]
    off = pe.rva_to_off(rva) if rva else None
    out = set()
    if off is None or off + 40 > len(pe.data):
        return out
    nfunc, aof = struct.unpack_from("<I", pe.data, off + 20)[0], struct.unpack_from("<I", pe.data, off + 28)[0]
    base = pe.rva_to_off(aof)
    if base is None:
        return out
    for i in range(nfunc):
        if base + 4 * i + 4 > len(pe.data):
            break
        (f,) = struct.unpack_from("<I", pe.data, base + 4 * i)
        if f and not (rva <= f < rva + size) and mod.is_code(f):
            out.add(f)
    return out


def branch_targets(mod):
    """Targets of byte-level E8 (call rel32) and E9 (jmp rel32) inside code: entries that something calls."""
    out = set()
    for lo, _, buf in mod.exec_mem:
        for op in (b"\xe8", b"\xe9"):
            p = buf.find(op)
            while p != -1:
                if p + 5 <= len(buf):
                    (d,) = struct.unpack_from("<i", buf, p + 1)
                    out.add(lo + p + 5 + d)
                p = buf.find(op, p + 1)
    return out


def thunk_addresses(mod, slots):
    """Addresses of 'jmp qword ptr [rip+slot]' stubs (import thunks), as {slot: [thunk rva, ...]}."""
    out = {}
    for lo, _, buf in mod.exec_mem:
        p = buf.find(b"\xff\x25")
        while p != -1:
            if p + 6 <= len(buf):
                (d,) = struct.unpack_from("<i", buf, p + 2)
                t = lo + p + 6 + d
                if t in slots:
                    out.setdefault(t, []).append(lo + p)
            p = buf.find(b"\xff\x25", p + 1)
    return out


def cat_of(c):
    if c["base"] is None and c["index"] is None:
        return "fixed:%#x" % c["disp"]
    return "computed"


def unreached_note(c):
    """For a candidate: the nearest preceding decoded instruction that writes its base register."""
    if c["base"] is None:
        return "no base register"
    for addr, text, w in reversed(c["prev"]):
        if c["base"] in w:
            return "nearest write to %s: %#x '%s'" % (c["base"], addr, text)
    return "no write to %s among the %d preceding decoded instructions" % (c["base"], len(c["prev"]))


def run_module(path):
    """Everything for one module. Returns (name, report lines, totals) so it can run in a pool."""
    mod = Module(path)
    pe = mod.pe
    funcs = pdata_functions(pe)
    tls = tls_directory(pe)
    tls_idx = tls["index_rva"] if tls else None
    cands = {}
    for b, e in funcs:
        resync(mod, b, e, cands)

    bitmap = bytearray(mod.code_top + 16)
    st = Counter()
    unresolved_sites = []
    calls, riref, iatcall, iatcall_call = Counter(), Counter(), Counter(), Counter()
    rd_gs, tls58, ten, thirty, computed, fixed_other = {}, {}, {}, {}, {}, {}
    covered, next_start_after = _cover_factory(funcs)

    # roots: the PE entry point (DllMain, which runs the CRT's TLS/FLS setup), exports, pointers in data,
    # and anything a call or jump reaches
    entry = {pe.entry_rva} if mod.is_code(pe.entry_rva) else set()
    rooted_set = branch_targets(mod) | export_entries(mod) | reloc_code_pointers(mod) | entry

    def process(b, e, rooted=None):
        if rooted is None:
            rooted = b in rooted_set
        seen = reach(mod, b, e, st, unresolved_sites)
        for a, ins in seen.items():
            if a >= len(bitmap) or bitmap[a]:
                continue
            bitmap[a] = 1
            st["reached_insns"] += 1
            if ins.mnem == "call" and ins.ops and ins.ops[0][0] == "imm" and ins.ops[0][1] >= 0:
                calls[ins.ops[0][1]] += 1
            for op in ins.ops:
                if op[0] == "mem" and op[7] is not None:
                    riref[op[7]] += 1
                    if ins.mnem in ("call", "jmp"):
                        iatcall[op[7]] += 1
                    if ins.mnem == "call":
                        iatcall_call[op[7]] += 1
            if first_gs(ins.ops) is None:
                continue
            kind, val, note = classify_gs(seen, a)
            info = {"func": b, "text": ins.mnem + " " + ins.opstr, "kind": kind, "val": val, "note": note,
                    "rooted": rooted}
            rd_gs[a] = info
            if kind == "fixed" and val == 0x58 and tls_idx is not None:
                r = analyze_tls58(seen, a, tls_idx)
                r["func"] = b
                tls58[a] = r
            elif kind == "fixed" and val == 0x10:
                r = analyze_fixed(seen, a, 0x10)
                r["func"], r["func_end"] = b, e
                ten[a] = r
            elif kind == "fixed" and val == 0x30:
                r = analyze_fixed(seen, a, 0x30)
                r["func"] = b
                thirty[a] = r
            elif kind == "computed":
                computed[a] = info
            else:
                fixed_other[a] = info

    # 1. every unwind-table function, 2. then functions that only direct calls reach (no unwind entry)
    for b, e in sorted(funcs, key=lambda f: f[0] not in rooted_set):
        if not bitmap[b]:
            process(b, e)
    gap_entries = 0
    done = set()
    hints = reloc_code_pointers(mod) | export_entries(mod) | entry
    while True:
        todo = [t for t in set(calls) | hints if t not in done and 0 <= t < len(bitmap) and not bitmap[t]
                and mod.is_code(t) and not covered(t)]
        if not todo:
            break
        for t in todo:
            done.add(t)
            if bitmap[t]:
                continue
            e = next_start_after(t)
            gap_entries += 1
            process(t, e if e is not None else mod.code_top, rooted=True)

    for c in cands.values():
        info = rd_gs.get(c["addr"], {})
        c["unrooted"] = bool(c["addr"] < len(bitmap) and bitmap[c["addr"]]) and not info.get("rooted", True) \
            and cat_of(c) == "computed"
        c["reached"] = bool(c["addr"] < len(bitmap) and bitmap[c["addr"]]) and not c["unrooted"]
        c["unproven"] = (not c["reached"]) and any(c["func"] <= x < c["func_end"] for x in unresolved_sites)

    imports = import_slots(pe)
    tls_names = sorted(((s_, dn) for s_, dn in imports.items() if dn[1].startswith(("Tls", "Fls"))),
                       key=lambda x: x[1][1])
    byte_calls = call_slot_count(mod, set(s_ for s_, _ in tls_names))
    thunks = thunk_addresses(mod, set(s_ for s_, _ in tls_names))
    bpos = {disp: gs_abs_positions(mod, disp) for disp in (0x10, 0x30, 0x58)}
    bl = {disp: len(v) for disp, v in bpos.items()}
    bl_unreached = {disp: [x for x in v if not (x < len(bitmap) and bitmap[x])] for disp, v in bpos.items()}

    # ---- report lines
    name = mod.name
    out = ["### %s" % name]
    if tls:
        out.append("TLS dir: template=%d zerofill=%d chars=%#x callbacks=%d _tls_index=RVA %#x (section %s, "
                   "in file: %s); template at RVA %#x (section %s), nonzero template bytes=%d" % (
                       tls["template"], tls["zerofill"], tls["characteristics"], tls["callbacks"],
                       tls["index_rva"], tls["index_section"], tls["index_in_file"], tls["start_rva"],
                       tls["template_section"], tls["template_nonzero_bytes"]))
    else:
        out.append("TLS dir: none")
    cv = list(cands.values())
    out.append("pdata gs candidates (resync): %d; in sequence: %d; after decode failure: %d; reached: %d; "
               "not reached: %d (unproven, inside a function with an unresolved register jump: %d)" % (
                   len(cv), sum(1 for c in cv if c["run"] == 0), sum(1 for c in cv if c["run"] > 0),
                   sum(1 for c in cv if c["reached"]), sum(1 for c in cv if not c["reached"]),
                   sum(1 for c in cv if c["unproven"])))
    gap_gs = [a for a in rd_gs if not covered(a)]
    gap_cat = Counter(("fixed:%#x" % rd_gs[a]["val"]) if rd_gs[a]["kind"] == "fixed" else "computed" for a in gap_gs)
    out.append("reached gs in code with no unwind entry: %d (from %d unwind-less entries): %s" % (
        len(gap_gs), gap_entries, dict(gap_cat)))
    out.append("reachability: pdata functions=%d; reached insns=%d; switch tables resolved=%d (cases %d, all reached: "
               "%s); indirect jmp via memory=%d; unresolved register jmp=%d" % (
                   len(funcs), st["reached_insns"], st["switch_resolved"], st["switch_cases"],
                   st["switch_cases"] == st["switch_case_reached"], st["jmp_mem"], st["jmp_reg_unresolved"]))
    t = tls58
    std = [v for v in t.values() if v["std"]]
    direct = set(o for v in std for o, _, _ in v["direct"])
    taken = set(o for v in std for o in v["taken"])
    allo = direct | taken
    tmpl = tls["template"] if tls else 0
    out.append("0x58 reached: %d (pdata %d, unwind-less %d); standard MSVC pattern: %d; non-standard: %d" % (
        len(t), sum(1 for a in t if covered(a)), sum(1 for a in t if not covered(a)), len(std), len(t) - len(std)))
    out.append("  distinct TLS offsets, direct access: %d; address taken or stored: %d (new over direct: %d); union: %d; "
               "largest: %s; offsets >= template size (%d): %s; indexed accesses: %d" % (
                   len(direct), len(taken), len(taken - direct), len(allo),
                   ("%#x" % max(allo)) if allo else "-", tmpl,
                   sorted(hex(o) for o in allo if o >= tmpl), sum(len(v["indexed"]) for v in std)))
    for a, v in sorted(t.items()):
        if not v["std"]:
            out.append("  non-standard 0x58 at %#x: %s" % (a, v["why"] or "index not matched"))
    helpers = Counter(v["func"] for v in ten.values())
    for f in helpers:
        fe = next(v["func_end"] for v in ten.values() if v["func"] == f)
        out.append("0x10 stack-limit probe in function %#x (size %#x): accesses %d, cmp after load %s, "
                   "reached call sites %d" % (
                       f, fe - f, helpers[f], all(v["cmp"] for v in ten.values() if v["func"] == f),
                       calls.get(f, 0)))
    if not helpers:
        out.append("0x10 accesses reached: none")
    for a, v in sorted(thirty.items()):
        out.append("0x30 access at %#x in %#x, next: %s" % (a, v["func"], v.get("next")))
    for c in sorted(cv, key=lambda c: c["addr"]):
        if cat_of(c) == "computed":
            out.append("computed candidate func=%#x insn=%#x run=%d reached=%s unproven=%s unrooted=%s: %s | %s" % (
                c["func"], c["addr"], c["run"], c["reached"], c["unproven"], c["unrooted"], c["text"],
                unreached_note(c) if not c["reached"] else "reached"))
        elif not c["reached"]:
            out.append("fixed candidate NOT reached func=%#x insn=%#x run=%d unproven=%s: %s" % (
                c["func"], c["addr"], c["run"], c["unproven"], c["text"]))
    for a, v in sorted(computed.items()):
        out.append("computed (register-based) gs reached at %#x in entry %#x, entry rooted=%s: %s [%s] %s" % (
            a, v["func"], v["rooted"], v["text"], v["kind"], v["note"]))
    tot_calls = 0
    for s_, (dll, nm) in tls_names:
        th = thunks.get(s_, [])
        via_thunk = sum(calls.get(t, 0) for t in th)
        c_reach = via_thunk + iatcall_call.get(s_, 0)
        tot_calls += c_reach
        out.append("import %s!%s slot %#x: thunk %s; reached call sites: via thunk=%d, call [slot]=%d, total=%d; "
                   "(check: all-bytes ff15 scan=%d)" % (
                       dll, nm, s_, ["%#x" % t for t in th] or "none", via_thunk, iatcall_call.get(s_, 0),
                       c_reach, byte_calls.get(s_, 0)))
    out.append("dynamic TLS/FLS imports: %d; reached call sites: %d" % (len(tls_names), tot_calls))
    out.append("byte-scan gs absolute (all exec bytes): 0x58=%d 0x10=%d 0x30=%d" % (bl[0x58], bl[0x10], bl[0x30]))
    for disp in (0x58, 0x10, 0x30):
        un = bl_unreached[disp]
        if un:
            out.append("  byte-scan gs:[%#x] sites not reached as instructions: %d; outside every unwind range: %d; "
                       "at %s" % (disp, len(un), sum(1 for x in un if not covered(x)),
                                  ", ".join("%#x%s" % (x, "" if covered(x) else " (no unwind)") for x in un[:12])))

    totals = Counter()
    totals["modules"] += 1
    totals["pdata_cands"] += len(cv)
    totals["pdata_cands_run0"] += sum(1 for c in cv if c["run"] == 0)
    totals["pdata_cands_run_gt0"] += sum(1 for c in cv if c["run"] > 0)
    totals["pdata_cands_unproven"] += sum(1 for c in cv if c["unproven"])
    for c in cv:
        totals[("pdata_reached:" if c["reached"] else "pdata_unreached:") + cat_of(c)] += 1
        totals["pdata_unrooted_computed"] += 1 if c["unrooted"] else 0
    for a in gap_gs:
        totals["gap_reached:" + ("fixed:%#x" % rd_gs[a]["val"] if rd_gs[a]["kind"] == "fixed" else "computed")] += 1
    totals["gap_entries"] += gap_entries
    totals["bytescan_0x58"] += bl[0x58]
    totals["bytescan_0x10"] += bl[0x10]
    totals["bytescan_0x30"] += bl[0x30]
    totals["switch_resolved"] += st["switch_resolved"]
    totals["switch_cases"] += st["switch_cases"]
    totals["jmp_reg_unresolved"] += st["jmp_reg_unresolved"]
    totals["jmp_mem"] += st["jmp_mem"]
    totals["reached_insns"] += st["reached_insns"]
    totals["tls58_std"] += len(std)
    totals["tls58_total"] += len(t)
    totals["tls_dir_modules"] += 1 if tls else 0
    totals["dyn_tls_calls"] += tot_calls
    return name, out, totals, tls


def main():
    root = sys.argv[1]
    only = sys.argv[2] if len(sys.argv) > 2 else None
    paths = []
    srv = os.path.join(root, "server.dll")
    if os.path.exists(srv):
        paths.append(srv)
    d = os.path.join(root, "bin", "x64_retail")
    for f in sorted(os.listdir(d)):
        if f.lower().endswith(".dll") and f.lower() not in EXCLUDE:
            paths.append(os.path.join(d, f))
    if only:
        paths = [p for p in paths if only in os.path.basename(p)]
    with Pool(min(4, len(paths))) as pool:
        results = pool.map(run_module, paths)
    total = Counter()
    for name, lines, t, tls in results:
        print("\n".join(lines))
        print()
        total.update(t)
    print("## totals over %d modules (excluding d3dcompiler_46.dll; ui(11).dll under game/r2 is not one of them)"
          % len(paths))
    for k in sorted(total):
        print("%s: %s" % (k, total[k]))


if __name__ == "__main__":
    main()
