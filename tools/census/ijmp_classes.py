#!/usr/bin/env python3
"""Group the functions a census skipped for an indirect jump by the shape of the jump.

    .venv/bin/python tools/census/ijmp_classes.py CENSUS_DIR [--module M] [--show N] [--list]
    .venv/bin/python tools/census/ijmp_classes.py CENSUS_DIR --module M --dump RVA

CENSUS_DIR is the output directory of tools/census/lift_census.sh.  For every function whose
report record is skipped with a reason of kind 'indirect-jump', the function is decoded again
with the lifter's own discovery (lifter.discover.Program.build_cfg) and each unresolved jump is
described by a backward slice: starting from the registers of the jump's operand, the
instructions of the straight-line run before it that write a register the jump depends on, with
registers renamed in order of appearance (J is the jump's register) and constants dropped.
Jumps with the same slice are one group.

A report made by an older lifter can be read with the current one: a jump the current discovery
settles is grouped as "now a tail call" or "now a switch".

Prints, per group: the count, the count per module, one example (module+rva of the jump) and
the slice.  With --list, one line per function instead: module, function rva, jump rva, the
other reason kinds of the record, and the group number.  --show N prints the N instructions
before the example jump of every group as addresses and mnemonics, for reading at the terminal;
--dump prints one function the same way, with its labels and what discovery made of each
transfer.  Nothing is written to disk.
"""

import argparse
import glob
import json
import os
import sys
from collections import Counter, defaultdict

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
sys.path.insert(0, REPO)

from capstone import x86_const as X                 # noqa: E402

from lifter.discover import Func, Program           # noqa: E402
from lifter.image import Image, find_module         # noqa: E402
from lifter.translate import GPR                    # noqa: E402

SLICE_DEPTH = 40


def skipped_jumps(census):
    """{module: [(function rva, [jump rva], other reason kinds, report record)]}"""
    out = {}
    for path in sorted(glob.glob(os.path.join(census, "*", "*.report.json"))):
        rep = json.load(open(path))
        rows = []
        for r in rep["functions"]:
            if r["status"] != "skipped":
                continue
            jumps = sorted({int(x["rva"], 16) for x in r["reasons"] if x["kind"] == "indirect-jump"})
            if jumps:
                others = sorted({x["kind"] for x in r["reasons"]} - {"indirect-jump"})
                rows.append((int(r["rva"], 16), jumps, others, r))
        if rows:
            out[rep["module"]] = (rep, rows)
    return out


def program_for(module, rep):
    """The lifter's Program with the functions the census run knew, so that 'inside this
    function' means what it meant there, without repeating the whole discovery."""
    image = Image.from_pe(find_module(module), module)
    prog = Program(image)
    for r in rep["functions"]:
        rva = int(r["rva"], 16)
        if rva not in prog.funcs:
            prog.add_entry(rva, r["origin"].replace("-into", ""))
    return prog


class Names:
    def __init__(self):
        self.of = {}

    def reg(self, r):
        if r == X.X86_REG_RIP:
            return "rip"
        if r == X.X86_REG_RSP:
            return "rsp"
        if r not in GPR:
            return "?"
        bits, idx, high = GPR[r]
        if idx not in self.of:
            self.of[idx] = "J" if not self.of else "ABCDEFGHIKLMNOPQ"[len(self.of) - 1]
        return "%s%d%s" % (self.of[idx], bits, "h" if high else "")


def operand(names, op):
    if op.type == X.X86_OP_REG:
        return names.reg(op.reg)
    if op.type == X.X86_OP_IMM:
        return "imm"
    if op.type == X.X86_OP_MEM:
        m = op.mem
        parts = []
        if m.base:
            parts.append(names.reg(m.base))
        if m.index:
            parts.append("%s*%d" % (names.reg(m.index), m.scale))
        if m.disp:
            parts.append("d")
        return "m%d[%s]" % (op.size * 8, "+".join(parts))
    return "?"


def reads_of(ins):
    try:
        read, written = ins.regs_access()
    except Exception:
        return set(), set()
    return {GPR[r][1] for r in read if r in GPR}, {GPR[r][1] for r in written if r in GPR}


def slice_of(prog, cfg, jmp):
    """([text], [instructions]) of the backward slice of the jump at `jmp`."""
    names = Names()
    ins = cfg.insns[jmp]
    op = ins.operands[0]
    want = set()
    if op.type == X.X86_OP_REG and op.reg in GPR:
        want.add(GPR[op.reg][1])
    elif op.type == X.X86_OP_MEM:
        for r in (op.mem.base, op.mem.index):
            if r in GPR:
                want.add(GPR[r][1])
    lines = ["jmp " + operand(names, op)]
    picked = [ins]
    a = cfg.pred.get(jmp)
    for _ in range(SLICE_DEPTH):
        if a is None:
            lines.append("(run starts)")
            break
        p = cfg.insns[a]
        name = p.insn_name()
        read, written = reads_of(p)
        if name == "call":
            written = written | {0, 1, 2, 8, 9, 10, 11}
        take = False
        if name in ("ja", "jae", "jb", "jbe", "jg", "jge", "jl", "jle", "je", "jne"):
            c = cfg.insns.get(cfg.pred.get(a))
            if c is not None and c.insn_name() in ("cmp", "test", "sub", "and"):
                cr, _ = reads_of(c)
                if cr & want:
                    lines.append("%s ; %s" % (" ".join([c.insn_name()] + [operand(names, o) for o in c.operands]), name))
                    picked.append(c)
        elif written & want:
            take = True
        if take:
            lines.append(" ".join([name] + [operand(names, o) for o in p.operands]))
            picked.append(p)
            if name == "call":
                want -= written
            else:
                # the written register is now explained by what this instruction reads
                pure = name in ("mov", "movzx", "movsx", "movsxd", "lea")
                if pure:
                    want -= written
                want |= {r for r in read if r != 4}
            rip = any(o.type == X.X86_OP_MEM and o.mem.base == X.X86_REG_RIP for o in p.operands)
            if rip and name == "lea":
                t = p.address + p.size + [o for o in p.operands if o.type == X.X86_OP_MEM][0].mem.disp
                lines[-1] += "  -> %s" % ("image base" if t == 0 else "exec" if prog.image.is_exec(t) else "data")
        if not want:
            break
        a = cfg.pred.get(a)
    else:
        lines.append("(depth)")
    return lines, picked


def dump(census, module, rva):
    rep = json.load(open(os.path.join(census, module, module + ".report.json")))
    prog = program_for(module, rep)
    fn = prog.funcs.get(rva) or Func(rva, None, "asked")
    cfg = prog.build_cfg(fn)
    print("%s+0x%x origin %s, %s, %d instructions" % (
        module, rva, fn.origin, "ranges %s" % ["0x%x-0x%x" % r for r in fn.ranges] if fn.ranges else "leaf", len(cfg.insns)))
    last = None
    for a in sorted(cfg.insns):
        ins = cfg.insns[a]
        if last is not None and last != a:
            print("        ...")
        flow = cfg.flow.get(a)
        note = ""
        if flow and flow[0] in ("ijmp_tail", "ijmp_unresolved", "ijmp_table", "tail", "trap"):
            note = "   ; %s%s" % (flow[0], " %d targets" % len(flow[1]) if flow[0] == "ijmp_table" else "")
        print("%s 0x%x  %s %s%s" % ("L" if a in cfg.labels else " ", a, ins.mnemonic, ins.op_str, note))
        last = a + ins.size
    for t in cfg.tables:
        print("table at 0x%x: %d entries, %s, bound %s, jmp 0x%x" % (t["table"], t["entries"], t["layout"], t["bound"], t["jmp"]))
    for k, a, text in cfg.problems:
        print("problem %s at 0x%x: %s" % (k, a, text))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("census")
    ap.add_argument("--module")
    ap.add_argument("--show", type=int, default=0)
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--only", action="store_true", help="count only functions whose sole reason kind is indirect-jump")
    ap.add_argument("--dump", metavar="RVA", help="with --module: print the decoded function at RVA (addresses, "
                    "mnemonics, labels and what discovery made of each transfer) and stop")
    args = ap.parse_args()
    if args.dump:
        return dump(args.census, args.module, int(args.dump, 16))

    groups = defaultdict(list)          # slice text -> [(module, fn, jmp, others)]
    examples = {}
    totals, settled = Counter(), Counter()
    for module, (rep, rows) in skipped_jumps(args.census).items():
        if args.module and module != args.module:
            continue
        prog = program_for(module, rep)
        for fn_rva, jumps, others, rec in rows:
            if args.only and others:
                continue
            totals[module] += 1
            fn = prog.funcs[fn_rva]
            cfg = prog.build_cfg(fn)
            for j in jumps:
                if j not in cfg.insns:
                    key = "(jump not decoded on a second look)"
                    groups[key].append((module, fn_rva, j, others))
                    continue
                lines, picked = slice_of(prog, cfg, j)
                flow = cfg.flow.get(j)
                refused = any(k == "indirect-jump" and a == j for k, a, _ in cfg.problems)
                if flow and flow[0] in ("ijmp_tail", "ijmp_table") and not refused:
                    # the report is from an older lifter: the current one settles this jump
                    why = "now %s%s" % ("a tail call" if flow[0] == "ijmp_tail" else "a switch",
                                        ", judged over the whole function" if j in getattr(cfg, "late", ()) else "")
                    settled[module] += 1
                else:
                    why = "table-refused-later" if flow and flow[0] == "ijmp_table" else "unresolved"
                key = "[%s] %s" % (why, " <- ".join(lines))
                groups[key].append((module, fn_rva, j, others))
                if key not in examples:
                    ctx = []
                    a = j
                    for _ in range(args.show + 1):
                        if a is None:
                            break
                        ctx.append(cfg.insns[a])
                        a = cfg.pred.get(a)
                    examples[key] = (module, ctx[::-1], fn.origin, fn.ranges is not None)
    print("functions skipped for an indirect jump: %d  (%s)" % (
        sum(totals.values()), ", ".join("%s %d" % kv for kv in sorted(totals.items()))))
    print("jumps of those that the current lifter settles: %d  (%s)" % (
        sum(settled.values()), ", ".join("%s %d" % kv for kv in sorted(settled.items()))))
    order = sorted(groups, key=lambda k: (-len(groups[k]), k))
    for n, key in enumerate(order):
        rows = groups[key]
        per = Counter(m for m, _, _, _ in rows)
        m, f, j, _ = rows[0]
        print("\n#%d  %d jumps in %d functions  (%s)  e.g. %s+0x%x in function 0x%x" % (
            n, len(rows), len({(m_, f_) for m_, f_, _, _ in rows}),
            ", ".join("%s %d" % kv for kv in sorted(per.items())), m, j, f))
        print("    " + key)
        if args.show and key in examples:
            em, ctx, origin, unwound = examples[key]
            print("    (origin %s, %s)" % (origin, "unwind-described" if unwound else "leaf"))
            for ins in ctx:
                print("      %s+0x%x  %s %s" % (em, ins.address, ins.mnemonic, ins.op_str))
    if args.list:
        print()
        for n, key in enumerate(order):
            for m, f, j, others in groups[key]:
                print("%-20s fn 0x%-8x jmp 0x%-8x #%-3d %s" % (m, f, j, n, ",".join(others)))


if __name__ == "__main__":
    main()
