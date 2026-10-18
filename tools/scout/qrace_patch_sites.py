#!/usr/bin/env python3
"""Are the sites of the guest patch `queue-push-one-read` all the same routine with the same two reads?

    .venv/bin/python tools/scout/qrace_patch_sites.py [--fix queue-push-one-read]

Reads the rows of lifter/guest_patches.toml for that fix and, for each, the function of the
game DLL that holds the patched instruction (static; nothing is executed).  Two checks:

  1. the whole function, instruction by instruction, against the first row's (tier0's):
     the same bytes everywhere except in the 4-byte displacement of a rip-relative operand
     or of a direct call or jump, and there the same target: the same function of the same
     DLL for an import slot, the same offset for a branch inside the function (any other
     target only counts as "outside").  Registers and stack offsets are compared, not
     abstracted: a copy that used other registers would be reported as different.
  2. at each site by itself, what the patch rests on.  In the function's own order:
       w  an instruction before the first lock call stores a register through the routine's
          second argument register (the unlocked write of the end mark into the new node)
       s  the instruction before the lock call stores a register P to a stack slot [rsp+d]
       c  the lock call is a call through the import slot of EnterCriticalSection
       1  the row's `reuse` is the next instruction: a load `mov A, [P]`
       r  then `mov T, [rsp+d]`: the same slot reloaded
       2  the row's `at` is the next instruction: a load `mov B, [T]`
       k  later, before the release: `cmp B, E`, a jump if not equal over one instruction,
          and that instruction stores to the same link
       v  after the release: `cmp A, E` with the same E: the first value decides the result
     So both loads read [P] (the call in between is an import; it gets neither P's register,
     which is callee-saved, nor the routine's own stack slot), and one value is meant.

Prints one line per site: module+rva, sizes, counts and the letters that hold; exit status 0
only if every site is the same as the first and has every letter.  No listing is printed.
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [HERE, os.path.abspath(os.path.join(HERE, "..", ".."))]

import capstone  # noqa: E402
from capstone import x86_const as X  # noqa: E402

from threads_lib import Module  # noqa: E402
from lifter import guest_patches  # noqa: E402

CALLEE_SAVED = {X.X86_REG_RBX, X.X86_REG_RBP, X.X86_REG_RSI, X.X86_REG_RDI, X.X86_REG_R12, X.X86_REG_R13,
                X.X86_REG_R14, X.X86_REG_R15}


def dll_name(module):
    return module + ".dll"


def decode(m, md, begin, end):
    out = []
    pc = begin
    while pc < end:
        ins = next(md.disasm(m.bytes_at(pc, 16), pc, 1), None)
        if ins is None:
            break
        out.append(ins)
        pc += ins.size
    return out


def norm(ins, slots, begin, end):
    """The instruction's bytes with a rip-relative or direct-branch displacement replaced by what it points at."""
    raw = bytes(ins.bytes)
    for op in ins.operands:
        if op.type == X.X86_OP_MEM and op.mem.base == X.X86_REG_RIP and ins.disp_size == 4:
            tgt = ins.address + ins.size + op.mem.disp
            what = "import %s!%s" % slots[tgt] if tgt in slots else "rip"
            return raw[:ins.disp_offset].hex() + "<" + what + ">" + raw[ins.disp_offset + 4:].hex()
    if raw[0] in (0xE8, 0xE9) and ins.size == 5:
        tgt = ins.operands[0].imm
        return raw[:1].hex() + ("<function+0x%x>" % (tgt - begin) if begin <= tgt < end else "<outside the function>")
    return raw.hex()


def mem(op):
    return op.type == X.X86_OP_MEM and op.mem.index == 0 and op.mem.segment == 0


def is_mov(ins):
    return ins.mnemonic == "mov" and len(ins.operands) == 2 and all(o.size == 8 for o in ins.operands)


def load(ins):
    """(destination, base, displacement) of `mov r64, [base + disp]`, else None."""
    if is_mov(ins) and ins.operands[0].type == X.X86_OP_REG and mem(ins.operands[1]):
        return ins.operands[0].reg, ins.operands[1].mem.base, ins.operands[1].mem.disp
    return None


def store(ins):
    """(base, displacement, source) of `mov [base + disp], r64`, else None."""
    if is_mov(ins) and mem(ins.operands[0]) and ins.operands[1].type == X.X86_OP_REG:
        return ins.operands[0].mem.base, ins.operands[0].mem.disp, ins.operands[1].reg
    return None


def import_call(ins, slots, name):
    if ins.mnemonic != "call" or ins.operands[0].type != X.X86_OP_MEM or ins.operands[0].mem.base != X.X86_REG_RIP:
        return False
    return slots.get(ins.address + ins.size + ins.operands[0].mem.disp, ("", ""))[1] == name


def facts(insns, slots, p):
    """The letters of the docstring that hold at this site."""
    at = {ins.address: i for i, ins in enumerate(insns)}
    if p.at not in at or p.reuse not in at:
        return ""
    i1, i2 = at[p.reuse], at[p.at]
    got = ""
    first_lock = next((i for i, ins in enumerate(insns) if import_call(ins, slots, "EnterCriticalSection")), None)
    if first_lock is None or i1 != first_lock + 1 or i2 != i1 + 2 or first_lock < 1:
        return got
    if any(store(ins) and store(ins)[0] == X.X86_REG_RDX and store(ins)[1] == 0 for ins in insns[:first_lock]):
        got += "w"
    st, l1, rl, l2 = store(insns[first_lock - 1]), load(insns[i1]), load(insns[i1 + 1]), load(insns[i2])
    if st and st[0] == X.X86_REG_RSP and st[2] in CALLEE_SAVED:
        got += "s"
    got += "c"
    if st and l1 and l1[1] == st[2] and l1[2] == 0:
        got += "1"
    if st and rl and rl[1] == X.X86_REG_RSP and rl[2] == st[1] and l1 and rl[0] != l1[0]:
        got += "r"
    if rl and l2 and l2[1] == rl[0] and l2[2] == 0 and l1 and l2[0] != l1[0]:
        got += "2"
    release = next((i for i in range(i2, len(insns)) if import_call(insns[i], slots, "LeaveCriticalSection")), None)
    if not (l1 and l2 and release):
        return got
    end_mark = None
    for i in range(i2 + 1, release - 2):
        c, j, s = insns[i], insns[i + 1], store(insns[i + 2])
        if (c.mnemonic == "cmp" and c.operands[0].type == X.X86_OP_REG and c.operands[0].reg == l2[0]
                and c.operands[1].type == X.X86_OP_REG and j.mnemonic == "jne"
                and j.operands[0].imm == insns[i + 3].address and s and s[1] == 0):
            # the store goes through a register that holds the tail node again: P itself, or P's slot reloaded
            via = next((load(x) for x in insns[i2 + 1:i + 2] if load(x) and load(x)[0] == s[0]), None)
            if s[0] == st[2] and (via is None or (via[1] == X.X86_REG_RSP and via[2] == st[1])):
                end_mark = c.operands[1].reg
                got += "k"
            break
    for c in insns[release + 1:release + 4]:
        if (end_mark is not None and c.mnemonic == "cmp" and c.operands[0].type == X.X86_OP_REG and c.operands[0].reg == l1[0]
                and c.operands[1].type == X.X86_OP_REG and c.operands[1].reg == end_mark):
            got += "v"
    return got


def main():
    fix = sys.argv[sys.argv.index("--fix") + 1] if "--fix" in sys.argv else "queue-push-one-read"
    rows = [p for p in guest_patches.load_table() if p.fix == fix]
    if not rows:
        sys.exit("no rows for the fix %r in %s" % (fix, guest_patches.TABLE))
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    ref, bad = None, 0
    want = "wsc1r2kv"
    for p in rows:
        m = Module(dll_name(p.module))
        slots = m.import_slots()
        entry = m.entry_of(p.at)
        if entry is None or entry[2] != entry[0]:
            print("%s+0x%x: in no function of the unwind table, or in a fragment of one" % (p.module, p.at))
            bad += 1
            continue
        begin, end = entry[0], entry[1]
        insns = decode(m, md, begin, end)
        shape = [norm(i, slots, begin, end) for i in insns]
        moved = sum(1 for s in shape if "<import " in s or "<rip>" in s or "<outside" in s)
        got = facts(insns, slots, p)
        if ref is None:
            ref, same = (p, shape), "the reference"
        elif shape == ref[1]:
            same = "the same as %s+0x%x, instruction for instruction" % (ref[0].module, ref[0].reuse - 0x3f)
        else:
            k = next((i for i, (a, b) in enumerate(zip(shape, ref[1])) if a != b), min(len(shape), len(ref[1])))
            same = "DIFFERENT from the reference from instruction %d on (function+0x%x)" % (
                k, (insns[k].address - begin) if k < len(insns) else len(shape))
            bad += 1
        if got != want:
            bad += 1
        print("%-20s +0x%-7x function +0x%-7x %3d bytes, %2d instructions, %2d fields that point outside the function: %s; "
              "first read at function+0x%x, second at +0x%x; holds: %s%s" % (
                  p.module, p.at, begin, end - begin, len(insns), moved, same, p.reuse - begin, p.at - begin,
                  got or "-", "" if got == want else "  (MISSING: %s)" % "".join(c for c in want if c not in got)))
    print("%d sites; %s" % (len(rows), "all the same routine with the same two reads" if not bad else "%d PROBLEMS" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
