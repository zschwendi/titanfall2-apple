#!/usr/bin/env python3
"""Questions 2 and 3 for ui(11).dll: the shape of its exported functions, and whether the
code is position-independent with no external calls.

Function model: an x64 unwind entry that is not a chained entry starts a function. Chained
entries are continuation pieces (hot/cold splits) of the function named by their parent entry,
so a function is the set of pieces that share a parent begin. Exports with no unwind entry are
leaf functions; they run from their RVA to the next known boundary, cut at the first ret or
unconditional jmp followed by 0xCC padding.

Per export: instruction count, rets, direct and indirect calls, tail jumps out of the function,
conditional jumps, backward branches (loops), memory reads and writes, SSE/AVX arithmetic and
moves, the stack frame from "sub rsp, imm", RIP-relative data references, the argument
registers read before being overwritten on some path (dataflow over the CFG; Microsoft x64
positions rcx/xmm0, rdx/xmm1, r8/xmm2, r9/xmm3), and where the base registers of memory writes
and indirect calls come from (traced back to an argument register, a stack slot, a global, or
a load).

Question 3 (whole image): RIP-relative operands and targets, absolute disp32 operands,
immediates in the preferred image range, direct branch targets outside .text, indirect call
sites and the slots they use, and qwords in data sections that look like absolute image
pointers (each would need a base relocation).

Capstone 5.0.7 in this venv: operands from ins.operands, register accesses from
ins.regs_access() (explicit and implicit).

    .venv/bin/python tools/scout/ui11_shape.py "game/r2/paks/Win64/ui(11).dll" [--dump NAME ...]
"""

import argparse
import bisect
import collections
import os
import re
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "inventory"))
from pe_inventory import PE, UNW_FLAG_CHAININFO  # noqa: E402

import capstone  # noqa: E402
from capstone import Cs, CS_ARCH_X86, CS_MODE_64  # noqa: E402
from capstone import x86 as X  # noqa: E402

AC_READ, AC_WRITE = capstone.CS_AC_READ, capstone.CS_AC_WRITE
GRP_JUMP, GRP_RET, GRP_INT = capstone.CS_GRP_JUMP, capstone.CS_GRP_RET, capstone.CS_GRP_INT

ARG_INT = ["rcx", "rdx", "r8", "r9"]
ARG_FLT = ["xmm0", "xmm1", "xmm2", "xmm3"]
ARG_BIT = {f: i for i, f in enumerate(ARG_INT + ARG_FLT)}
ALL_ARG_MASK = (1 << len(ARG_BIT)) - 1

FP_ARITH = re.compile(r"^v?(add|sub|mul|div|min|max|sqrt|rcp|rsqrt|round|cmp|and|andn|or|xor|dp|hadd|hsub|"
                      r"fmadd\d*|fmsub\d*|fnmadd\d*|fnmsub\d*)(ss|sd|ps|pd)$")
FP_CMP_CVT = re.compile(r"^v?(u?comi(ss|sd)|cvt\w*)$")
VEC_MOVE = re.compile(r"^v?(mov(ss|sd|aps|ups|apd|upd|dqa|dqu|d|q|hlps|lhps|lps|hps|hpd|lpd)|movmsk\w*|"
                      r"pshufd|shufps|shufpd|unpck\w*|blend\w*|insert\w*|extract\w*|pxor|xorp[sd]|pand\w*|por)$")
CALL_CLOBBERED = {"rax", "rcx", "rdx", "r8", "r9", "r10", "r11"}
GPR_BASE = {"ax": "rax", "bx": "rbx", "cx": "rcx", "dx": "rdx", "si": "rsi", "di": "rdi", "bp": "rbp", "sp": "rsp"}


def reg_info(name):
    """Capstone register name -> (family, width in bits), e.g. 'ecx' -> ('rcx', 32)."""
    if not name:
        return None, 0
    m = re.fullmatch(r"r(\d+)([dwb]?)", name)
    if m:
        return "r" + m.group(1), {"": 64, "d": 32, "w": 16, "b": 8}[m.group(2)]
    m = re.fullmatch(r"([re]?)(ax|bx|cx|dx|si|di|bp|sp)", name)
    if m:
        return GPR_BASE[m.group(2)], {"r": 64, "e": 32, "": 16}[m.group(1)]
    m = re.fullmatch(r"([abcd])([lh])", name)
    if m:
        return "r%sx" % m.group(1), 8
    m = re.fullmatch(r"(sil|dil|bpl|spl)", name)
    if m:
        return "r" + m.group(1)[:2], 8
    m = re.fullmatch(r"[xyz]mm(\d+)", name)
    if m:
        return "xmm" + m.group(1), 128
    return None, 0


def load_directory(pe):
    """Return (primary {begin: end}, pieces {parent begin: [(b, e), ...]}, chained_begins, exports)."""
    d = pe.data
    rva, size = pe.dirs[3]
    off = pe.rva_to_off(rva)
    entries = []
    for i in range(size // 12):
        b, e, unw = struct.unpack_from("<III", d, off + 12 * i)
        entries.append((b, e, unw))
    primary, pieces, chained_begins = {}, collections.defaultdict(list), set()
    for b, e, unw in entries:
        uoff = pe.rva_to_off(unw & ~1)
        ver_flags, _, ncodes, _ = struct.unpack_from("<BBBB", d, uoff)
        if (ver_flags >> 3) & UNW_FLAG_CHAININFO:
            chained_begins.add(b)
            parent_off = uoff + 4 + 2 * ((ncodes + 1) & ~1)
            (pb, _, _) = struct.unpack_from("<III", d, parent_off)
            pieces[pb].append((b, e))
        else:
            primary[b] = e
    for b, e in primary.items():
        pieces[b].insert(0, (b, e))
    erva, _ = pe.dirs[0]
    eo = pe.rva_to_off(erva)
    (_, _, _, _, _, obase, nf, nn, far, nar, oar) = struct.unpack_from("<IIHHIIIIIII", d, eo)
    fa, na, oa = pe.rva_to_off(far), pe.rva_to_off(nar), pe.rva_to_off(oar)
    name_of = {}
    for i in range(nn):
        (nrva,) = struct.unpack_from("<I", d, na + 4 * i)
        (idx,) = struct.unpack_from("<H", d, oa + 2 * i)
        name_of[idx] = pe.cstr(nrva)
    exports = []
    for idx in range(nf):
        (frva,) = struct.unpack_from("<I", d, fa + 4 * idx)
        if frva:
            exports.append({"name": name_of.get(idx), "rva": frva, "ordinal": obase + idx})
    return primary, dict(pieces), chained_begins, exports


def is_zeroing(ins):
    if ins.mnemonic not in ("xor", "pxor", "xorps", "xorpd", "vxorps", "vxorpd", "vpxor", "sub"):
        return False
    ops = ins.operands
    return len(ops) >= 2 and ops[0].type == X.X86_OP_REG and ops[1].type == X.X86_OP_REG \
        and ops[0].reg == ops[1].reg


def leaf_extent(pe, md, start, bound):
    off = pe.rva_to_off(start)
    for ins in md.disasm(pe.data[off:off + (bound - start)], start):
        if ins.mnemonic in ("ret", "jmp", "ud2") or ins.id == X.X86_INS_RET:
            nxt = ins.address + ins.size
            if nxt >= bound or pe.data[pe.rva_to_off(nxt)] == 0xCC:
                return nxt
    return bound


def decode_pieces(pe, md, pieces):
    """Instructions of every piece, in address order, with contiguity-based fallthrough."""
    insns = []
    for b, e in sorted(pieces):
        off = pe.rva_to_off(b)
        insns += list(md.disasm(pe.data[off:off + (e - b)], b))
    return insns


def origin(insns, idx_before, fam, depth=0):
    """Where the value of register family `fam` comes from, traced back linearly.

    Returns ('arg', n) for an entry argument register, ('stack',), ('global',) for a RIP-relative
    load or lea, ('load', base) for a load through another register, ('copy', ...) chains, or
    ('computed',) / ('unknown',)."""
    if depth > 6:
        return ("unknown",)
    j = idx_before - 1
    while j >= 0:
        ins = insns[j]
        rd, wr = ins.regs_access()
        writes = {reg_info(ins.reg_name(r))[0] for r in wr}
        if ins.id == X.X86_INS_CALL and fam in CALL_CLOBBERED:
            # regs_access() omits the implicit writes of a call: rax holds the result, the rest are clobbered.
            return ("call-result",) if fam == "rax" else ("call-clobbered",)
        if fam in writes:
            ops = ins.operands
            if ins.mnemonic in ("mov", "movsxd", "movzx", "lea") and len(ops) == 2 and ops[0].type == X.X86_OP_REG:
                src = ops[1]
                if src.type == X.X86_OP_REG:
                    sfam = reg_info(ins.reg_name(src.reg))[0]
                    if sfam in ARG_BIT:
                        return ("arg", ARG_BIT[sfam] + 1)
                    if sfam in ("rsp", "rbp"):
                        return ("stack",)
                    return origin(insns, j, sfam, depth + 1)
                if src.type == X.X86_OP_MEM:
                    if src.mem.base == X.X86_REG_RIP:
                        return ("global",) if ins.mnemonic != "lea" else ("rip-address",)
                    if ins.mnemonic == "lea":
                        return ("computed",)
                    bfam = reg_info(ins.reg_name(src.mem.base))[0] if src.mem.base else None
                    if bfam in ("rsp", "rbp"):
                        return ("stack-load",)
                    return ("load",) + (origin(insns, j, bfam, depth + 1) if bfam else ("none",))
            return ("computed", ins.mnemonic)
        j -= 1
    fam_arg = ARG_BIT.get(fam)
    if fam_arg is not None:
        return ("arg", fam_arg + 1)
    return ("entry", fam)


def build_flow(insns):
    addr_idx = {ins.address: i for i, ins in enumerate(insns)}
    n = len(insns)
    info, succs = [], []
    loops = indirect_jumps = 0
    for i, ins in enumerate(insns):
        zero = is_zeroing(ins)
        reads = kills = 0
        rd, wr = ins.regs_access()
        if not zero:
            for r in rd:
                fam, _ = reg_info(ins.reg_name(r))
                if fam in ARG_BIT:
                    reads |= 1 << ARG_BIT[fam]
        for r in wr:
            fam, w = reg_info(ins.reg_name(r))
            if fam not in ARG_BIT:
                continue
            if fam.startswith("xmm"):
                partial = ins.mnemonic in ("movss", "movsd", "vmovss", "vmovsd") and len(ins.operands) == 2 \
                    and all(o.type == X.X86_OP_REG for o in ins.operands)
                if not partial:
                    kills |= 1 << ARG_BIT[fam]
            elif w >= 32 or zero:
                kills |= 1 << ARG_BIT[fam]
        if ins.id == X.X86_INS_CALL:
            kills |= ALL_ARG_MASK
        info.append((reads, kills))
        s = []
        if GRP_RET in ins.groups or GRP_INT in ins.groups or ins.mnemonic in ("int3", "ud2"):
            pass
        elif ins.mnemonic == "jmp":
            op = ins.operands[0]
            if op.type == X.X86_OP_IMM:
                if op.imm in addr_idx:
                    s.append(addr_idx[op.imm])
                    loops += op.imm <= ins.address
            else:
                indirect_jumps += 1
        elif GRP_JUMP in ins.groups:
            op = ins.operands[0]
            if op.type == X.X86_OP_IMM and op.imm in addr_idx:
                s.append(addr_idx[op.imm])
                loops += op.imm <= ins.address
            if i + 1 < n:
                s.append(i + 1)
        elif i + 1 < n and insns[i + 1].address == ins.address + ins.size:
            s.append(i + 1)
        succs.append(s)
    return info, succs, indirect_jumps, loops


def arg_inputs(info, succs):
    seen = set()
    stack = [(0, 0)]
    used = 0
    while stack:
        i, mask = stack.pop()
        if (i, mask) in seen:
            continue
        seen.add((i, mask))
        reads, kills = info[i]
        used |= reads & ~mask
        nmask = mask | kills
        for j in succs[i]:
            stack.append((j, nmask))
    return used


def positions(mask):
    ints = [k + 1 for k, f in enumerate(ARG_INT) if mask & (1 << ARG_BIT[f])]
    flts = [k + 1 for k, f in enumerate(ARG_FLT) if mask & (1 << ARG_BIT[f])]
    return ints, flts


def analyse(pe, md, start, pieces, name, leaf):
    insns = decode_pieces(pe, md, pieces)
    piece_bytes = sum(e - b for b, e in pieces)
    covered = sum(i.size for i in insns)
    span = set()
    for b, e in pieces:
        span.update(range(b, e))
    r = {"name": name, "start": start, "pieces": len(pieces), "leaf": leaf, "insns": len(insns),
         "bytes": piece_bytes, "decode_ok": covered == piece_bytes, "rets": 0, "direct_calls": 0,
         "indirect_calls": 0, "call_targets": [], "call_origins": collections.Counter(),
         "tail_jumps": 0, "jcc": 0, "mem_reads": 0, "mem_writes": 0,
         "write_bases": collections.Counter(), "sse_arith": 0, "sse_moves": 0, "vector_ops": 0,
         "frame": 0, "rep_ops": 0, "rip_refs": set(), "indirect_jmp": 0, "loops": 0, "jmp_slots": [],
         "arg1_disps": []}
    for idx, ins in enumerate(insns):
        m = ins.mnemonic
        ops = ins.operands
        if GRP_RET in ins.groups:
            r["rets"] += 1
        if m == "call":
            if ops and ops[0].type == X.X86_OP_IMM:
                r["direct_calls"] += 1
                r["call_targets"].append(ops[0].imm)
            else:
                r["indirect_calls"] += 1
                if ops and ops[0].type == X.X86_OP_MEM and ops[0].mem.base not in (0, X.X86_REG_RIP):
                    bfam = reg_info(ins.reg_name(ops[0].mem.base))[0]
                    og = origin(insns, idx, bfam)
                    r["call_origins"][og[0] + (str(og[1]) if og[0] == "arg" else "")] += 1
                    if og[0] == "arg" and og[1] == 1:
                        r["arg1_disps"].append(ops[0].mem.disp)
        elif m == "jmp":
            if ops[0].type == X.X86_OP_IMM:
                if ops[0].imm not in span:
                    r["tail_jumps"] += 1
            else:
                r["indirect_jmp"] += 1
                if ops[0].type == X.X86_OP_MEM:
                    r["jmp_slots"].append(ops[0].mem.disp)
        elif GRP_JUMP in ins.groups:
            r["jcc"] += 1
        if m.startswith("rep"):
            r["rep_ops"] += 1
        if m == "sub" and len(ops) == 2 and ops[0].type == X.X86_OP_REG and ops[0].reg == X.X86_REG_RSP \
                and ops[1].type == X.X86_OP_IMM:
            r["frame"] = max(r["frame"], ops[1].imm)
        vec = any(o.type == X.X86_OP_REG and (reg_info(ins.reg_name(o.reg))[0] or "").startswith("xmm") for o in ops)
        if vec:
            r["vector_ops"] += 1
            if FP_ARITH.match(m) or FP_CMP_CVT.match(m):
                r["sse_arith"] += 1
            elif VEC_MOVE.match(m):
                r["sse_moves"] += 1
        for o in ops:
            if o.type != X.X86_OP_MEM or m in ("lea", "nop"):
                continue
            if o.mem.base == X.X86_REG_RIP:
                r["rip_refs"].add(ins.address + ins.size + o.mem.disp)
            if o.access & AC_READ:
                r["mem_reads"] += 1
            if o.access & AC_WRITE:
                r["mem_writes"] += 1
                if o.mem.base == X.X86_REG_RIP:
                    b = "rip"
                elif o.mem.base == X.X86_REG_INVALID:
                    b = "absolute"
                else:
                    fam = reg_info(ins.reg_name(o.mem.base))[0]
                    if fam in ("rsp", "rbp"):
                        b = "stack"
                    elif fam in ARG_BIT:
                        b = fam
                    else:
                        og = origin(insns, idx, fam)
                        b = "traced:" + (("arg%d" % og[1]) if og[0] == "arg" else og[0])
                r["write_bases"][b] += 1
    info, succs, ijmp, loops = build_flow(insns)
    r["indirect_jmp"] = max(r["indirect_jmp"], ijmp)
    r["loops"] = loops
    r["arg_mask"] = arg_inputs(info, succs)
    r["int_pos"], r["flt_pos"] = positions(r["arg_mask"])
    return r


def pct(n, d):
    return "%d (%.1f%%)" % (n, 100.0 * n / d) if d else "0"


def export_report(pe, md, primary, pieces, exports, out):
    text = next(s for s in pe.sections if s["name"] == ".text")
    text_end = text["va"] + text["vsize"]
    export_rvas = {e["rva"] for e in exports}
    all_starts = sorted(set(primary) | set(pieces) | export_rvas)
    results = []
    for e in exports:
        s = e["rva"]
        if s in primary:
            results.append(analyse(pe, md, s, pieces[s], e["name"], False))
        else:
            k = bisect.bisect_right(all_starts, s)
            bound = all_starts[k] if k < len(all_starts) else text_end
            end = leaf_extent(pe, md, s, bound)
            results.append(analyse(pe, md, s, [(s, end)], e["name"], True))
    out.extend(results)
    N = len(results)
    multi = sum(1 for r in results if r["pieces"] > 1)
    print("Exports analysed: %d; with an unwind function: %d (of which %d have cold or split pieces); leaf exports: %d"
          % (N, N - sum(r["leaf"] for r in results), multi, sum(r["leaf"] for r in results)))
    print("Pieces per unwind export: total %d chained continuation pieces attached to exports" % sum(r["pieces"] - 1 for r in results if not r["leaf"]))
    print("Extents decoded with no gap: %d of %d" % (sum(r["decode_ok"] for r in results), N))
    sizes = sorted(r["insns"] for r in results)
    print("Instructions per export: min %d, p25 %d, median %d, p75 %d, p90 %d, max %d, mean %.1f"
          % (sizes[0], sizes[N // 4], sizes[N // 2], sizes[3 * N // 4], sizes[9 * N // 10], sizes[-1], sum(sizes) / N))
    edges = [10, 30, 100, 300]
    buckets = collections.Counter(next((("<=%d" % e) for e in edges if v <= e), ">300") for v in sizes)
    print("Size buckets: " + ", ".join("%s:%d" % (k, buckets[k]) for k in ["<=10", "<=30", "<=100", "<=300", ">300"]))
    print("Total instructions in exports: %d" % sum(sizes))
    print("Byte size of exports (bytes incl. pieces): median %d, max %d" % (
        sorted(r["bytes"] for r in results)[N // 2], max(r["bytes"] for r in results)))

    calls = sum(1 for r in results if r["direct_calls"] + r["indirect_calls"])
    print("Exports with no call instruction at all: %s" % pct(N - calls, N))
    print("Exports with a direct call: %s; with an indirect call: %s"
          % (pct(sum(r["direct_calls"] > 0 for r in results), N), pct(sum(r["indirect_calls"] > 0 for r in results), N)))
    print("Total call instructions: direct %d, indirect %d" % (
        sum(r["direct_calls"] for r in results), sum(r["indirect_calls"] for r in results)))
    print("Exports with a memory write: %s; with a memory read: %s"
          % (pct(sum(r["mem_writes"] > 0 for r in results), N), pct(sum(r["mem_reads"] > 0 for r in results), N)))
    print("Exports with a backward branch (loop): %s; with an indirect jump: %s; with a tail jump out: %s"
          % (pct(sum(r["loops"] > 0 for r in results), N), pct(sum(r["indirect_jmp"] > 0 for r in results), N),
             pct(sum(r["tail_jumps"] > 0 for r in results), N)))
    print("Indirect jumps through memory (tail-calls through a table): %d; through a register (switch tables): %d"
          % (sum(len(r["jmp_slots"]) for r in results),
             sum(r["indirect_jmp"] - len(r["jmp_slots"]) for r in results)))
    print("Exports with a stack frame (sub rsp, imm): %s" % pct(sum(r["frame"] > 0 for r in results), N))
    fr = sorted(r["frame"] for r in results if r["frame"])
    if fr:
        print("  frame sizes: min %d, median %d, max %d bytes" % (fr[0], fr[len(fr) // 2], fr[-1]))
    print("Rets per export: exactly one: %s; more than one: %s"
          % (pct(sum(r["rets"] == 1 for r in results), N), pct(sum(r["rets"] > 1 for r in results), N)))
    print("rep-prefixed string instructions in exports: %d" % sum(r["rep_ops"] for r in results))
    print("Exports touching any xmm operand: %s" % pct(sum(r["vector_ops"] > 0 for r in results), N))
    print("Exports with a float arithmetic, compare or convert op: %s" % pct(sum(r["sse_arith"] > 0 for r in results), N))
    print("Exports with 10 or more such ops: %s" % pct(sum(r["sse_arith"] >= 10 for r in results), N))
    fa = sum(r["sse_arith"] for r in results)
    print("Float arithmetic/compare/convert ops: %d of %d instructions (%.2f%%); xmm moves: %d"
          % (fa, sum(sizes), 100.0 * fa / sum(sizes), sum(r["sse_moves"] for r in results)))

    sig = collections.Counter("int %s / float %s" % (r["int_pos"] or "-", r["flt_pos"] or "-") for r in results)
    print("Argument registers read before written, per export (Microsoft x64 positions):")
    for k, v in sig.most_common(12):
        print("  %-30s %d" % (k, v))
    pos_int = collections.Counter(p for r in results for p in r["int_pos"])
    pos_flt = collections.Counter(p for r in results for p in r["flt_pos"])
    print("  integer positions read, exports per position (1=rcx 2=rdx 3=r8 4=r9): %s" % dict(sorted(pos_int.items())))
    print("  float positions read: %s" % (dict(sorted(pos_flt.items())) or "none"))
    print("Exports reading no argument register: %s" % pct(sum(r["arg_mask"] == 0 for r in results), N))
    print("Exports reading a float argument: %s" % pct(sum(bool(r["flt_pos"]) for r in results), N))

    origins = collections.Counter()
    for r in results:
        origins.update(r["call_origins"])
    print("Indirect call sites by where the called-through pointer comes from: %s" % dict(origins.most_common()))
    disps = collections.Counter(d for r in results for d in r["arg1_disps"])
    print("Calls through a pointer traced to rcx: %d sites, %d distinct offsets, offsets range 0x%x..0x%x; most used: %s"
          % (sum(disps.values()), len(disps), min(disps) if disps else 0, max(disps) if disps else 0,
             ", ".join("0x%x x%d" % kv for kv in disps.most_common(6))))
    wb = collections.Counter()
    for r in results:
        wb.update(r["write_bases"])
    print("Memory writes by base (all %d writes): %s" % (sum(wb.values()), ", ".join("%s:%d" % kv for kv in wb.most_common())))

    tgt_kind = collections.Counter()
    dtargets = set()
    for r in results:
        for t in r["call_targets"]:
            dtargets.add(t)
            if t in export_rvas:
                tgt_kind["an export"] += 1
            elif t in primary or t in pieces:
                tgt_kind["an internal unwind function"] += 1
            else:
                tgt_kind["other address"] += 1
    print("Direct call sites in exports, by target kind: %s" % (dict(tgt_kind) or "none"))
    rip_t = collections.Counter()
    for r in results:
        for t in r["rip_refs"]:
            sec = pe.section_of(t)
            rip_t[sec["name"] if sec else "outside image"] += 1
    print("RIP-relative data references from exports, by target section: %s" % dict(rip_t))
    print("Exports with no RIP-relative reference: %s" % pct(sum(not r["rip_refs"] for r in results), N))

    by_name = sorted(results, key=lambda r: r["name"])
    step = max(1, N // 40)
    sample = by_name[::step][:40]
    print("Sample of %d exports (every %dth by name): insns, calls, writes, float ops, rets, frame, args" % (len(sample), step))
    for r in sample:
        print("  %-38s %5d %3d %3d %3d %2d %5d int%s flt%s" % (
            r["name"][:38], r["insns"], r["direct_calls"] + r["indirect_calls"], r["mem_writes"],
            r["sse_arith"], r["rets"], r["frame"], r["int_pos"], r["flt_pos"]))
    return results


def pair_report(results):
    by_name = {r["name"]: r for r in results}
    pairs = [(n, n + "__hidden") for n in by_name if n + "__hidden" in by_name]
    print("Pairs X / X__hidden: %d" % len(pairs))
    print("  both members have at least one indirect call: %d; both use the same set of call-through slots: %d"
          % (sum(by_name[a]["indirect_calls"] > 0 and by_name[b]["indirect_calls"] > 0 for a, b in pairs),
             sum(sorted(by_name[a]["jmp_slots"]) == sorted(by_name[b]["jmp_slots"]) for a, b in pairs)))
    print("  same argument registers read: %d; share at least one RIP-relative data address: %d"
          % (sum(by_name[a]["arg_mask"] == by_name[b]["arg_mask"] for a, b in pairs),
             sum(bool(by_name[a]["rip_refs"] & by_name[b]["rip_refs"]) for a, b in pairs)))
    diffs = collections.Counter(by_name[b]["insns"] - by_name[a]["insns"] > 0 for a, b in pairs)
    print("  hidden variant has more instructions than plain: %d; fewer: %d"
          % (diffs[True], len(pairs) - diffs[True]))
    print("  plain members that are unwind functions: %d of %d" % (sum(not by_name[a]["leaf"] for a, b in pairs), len(pairs)))
    print("  hidden members that are unwind functions: %d of %d" % (sum(not by_name[b]["leaf"] for a, b in pairs), len(pairs)))
    singles = [n for n in by_name if not n.endswith("__hidden") and n + "__hidden" not in by_name]
    print("  exports with no hidden variant: %d; hidden variants with no plain name: %d"
          % (len(singles), sum(1 for n in by_name if n.endswith("__hidden") and n[:-8] not in by_name)))


def position_independence(pe, md, primary, pieces, results):
    text = next(s for s in pe.sections if s["name"] == ".text")
    image_lo, image_hi = pe.image_base, pe.image_base + pe.size_of_image
    extents = set()
    for b, lst in pieces.items():
        for pb, pe_ in lst:
            extents.add((pb, pe_))
    extents |= {(r["start"], r["start"] + r["bytes"]) for r in results if r["leaf"]}
    rip_ops = collections.Counter()
    abs_disp = abs_index = imm_in_image = 0
    imm_hits = []
    slot_calls = collections.Counter()
    indirect = collections.Counter()
    branch_outside_text = 0
    total = 0
    direct_calls_all = 0
    for s, e in sorted(extents):
        off = pe.rva_to_off(s)
        for ins in md.disasm(pe.data[off:off + (e - s)], s):
            total += 1
            m = ins.mnemonic
            if m == "call" and ins.operands and ins.operands[0].type == X.X86_OP_IMM:
                direct_calls_all += 1
            for o in ins.operands:
                if o.type == X.X86_OP_MEM:
                    if o.mem.base == X.X86_REG_RIP:
                        rip_ops["rip-relative"] += 1
                        if m in ("call", "jmp"):
                            slot_calls[ins.address + ins.size + o.mem.disp] += 1
                    elif o.mem.base == X.X86_REG_INVALID and o.mem.index == X.X86_REG_INVALID:
                        abs_disp += 1
                    elif o.mem.base == X.X86_REG_INVALID and m != "lea":
                        abs_index += 1
                elif o.type == X.X86_OP_IMM and m not in ("call", "jmp") and GRP_JUMP not in ins.groups:
                    v = o.imm & 0xFFFFFFFFFFFFFFFF
                    if image_lo <= v < image_hi:
                        imm_in_image += 1
                        imm_hits.append((hex(ins.address), m))
            if m in ("call", "jmp") or GRP_JUMP in ins.groups:
                op = ins.operands[0]
                if op.type == X.X86_OP_IMM:
                    sec = pe.section_of(op.imm)
                    if not (sec and pe.is_exec(sec)):
                        branch_outside_text += 1
                elif m == "call":
                    indirect["call through register" if op.type == X.X86_OP_REG else "call through memory"] += 1
                else:
                    indirect["jmp through register" if op.type == X.X86_OP_REG else "jmp through memory"] += 1
    print()
    print("=== Question 3: position independence and external calls ===")
    print("Instructions decoded across all unwind pieces and leaf exports: %d" % total)
    cov = sum(e - b for lst in pieces.values() for b, e in lst)
    print(".text bytes covered by unwind entries (primary and chained): %d of %d (%.1f%%)"
          % (cov, text["vsize"], 100.0 * cov / text["vsize"]))
    print("Direct call instructions with a target address (rel32): %d" % direct_calls_all)
    print("RIP-relative memory operands: %d" % rip_ops["rip-relative"])
    print("Absolute disp32 operands (no base, no index): %d; absolute with index and no base (not lea): %d"
          % (abs_disp, abs_index))
    print("Immediates inside the preferred image range %s-%s: %d %s"
          % (hex(image_lo), hex(image_hi), imm_in_image, imm_hits[:4]))
    print("Direct call and jump targets outside any executable section: %d" % branch_outside_text)
    print("Indirect control transfers: %s" % dict(indirect))
    print("Distinct [rip+X] slots used as call or jmp targets: %d" % len(slot_calls))
    for t in sorted(slot_calls)[:6]:
        sec = pe.section_of(t)
        print("  slot RVA 0x%x in %s, file value %s, used by %d sites"
              % (t, sec["name"] if sec else "outside image",
                 "zero-filled" if not (sec and sec["rsize"]) else "0x%x" % struct.unpack_from("<Q", pe.data, pe.rva_to_off(t))[0],
                 slot_calls[t]))
    abs_ptrs = []
    for name in (".rdata", ".data", ".pdata"):
        sec = next((s for s in pe.sections if s["name"] == name), None)
        if sec is None or sec["rsize"] == 0:
            continue
        for i in range(0, sec["rsize"] - 7, 8):
            (v,) = struct.unpack_from("<Q", pe.data, sec["rptr"] + i)
            if image_lo <= v < image_hi:
                abs_ptrs.append((name, sec["va"] + i, v))
    print("Qwords in .rdata, .data, .pdata that point into the image's preferred range: %d" % len(abs_ptrs))
    for name, rva, v in abs_ptrs[:4]:
        print("  %s at RVA 0x%x holds 0x%x" % (name, rva, v))
    pad = pe.data[pe.rva_to_off(text["va"]):pe.rva_to_off(text["va"]) + text["rsize"]].count(b"\xcc")
    print("0xCC bytes in .text: %d" % pad)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path")
    ap.add_argument("--dump", nargs="*", default=[], help="export names to print as disassembly")
    args = ap.parse_args()
    pe = PE(args.path)
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    primary, pieces, chained, exports = load_directory(pe)
    results = []
    export_report(pe, md, primary, pieces, exports, results)
    pair_report(results)
    for want in args.dump:
        for r in results:
            if r["name"] == want:
                print("--- %s, %d instructions, pieces %d" % (want, r["insns"], r["pieces"]))
                for b, e in sorted(pieces.get(r["start"], [(r["start"], r["start"] + r["bytes"])])):
                    off = pe.rva_to_off(b)
                    for ins in md.disasm(pe.data[off:off + (e - b)], b):
                        print("  %06x  %-8s %s" % (ins.address, ins.mnemonic, ins.op_str))
    position_independence(pe, md, primary, pieces, results)


if __name__ == "__main__":
    main()
