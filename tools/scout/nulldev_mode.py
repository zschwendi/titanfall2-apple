#!/usr/bin/env python3
"""Every test of one mode word, with what each side of the test does (design-null-device).

    .venv/bin/python tools/scout/nulldev_mode.py MODULE 0xWORD [--value N] [--len N] [--only 0xSITE,...]

Written for the material system's mode word (materialsystem_dx11+0x21e0e0: 1 normally, 2 with
`-noshaderapi`; finding 37), usable for any global that code compares with a constant.

For every instruction that refers to the word it prints one block:

  site, the function that holds it, and every RTTI vtable slot that points at that function
  (so a reader can tell which interface method the test sits in);
  the constant compared with and the branch that follows;
  a summary of the path taken when the word equals --value (default 2) and of the other
  path: the first --len (default 14) instructions in execution order, unconditional jumps
  followed, shown as calls (import, direct target, virtual slot), stores of constants to the
  return register, and the return.

"Summary" means mnemonic-level: it prints addresses, call targets, import names and the
constants returned, not a listing.  Terminal use; nothing is stored.  OriginSDK.dll is refused
(flow_lib).
"""

import sys

from flow_lib import get, fmt_imp
from capstone import x86_const as X

RET_REGS = {"al", "ax", "eax", "rax"}


def slot_index(M):
    """{function rva: ["Class::slotN", ...]} over every RTTI vtable of the module."""
    out = {}
    for vt, cls in sorted(M.vtables().items()):
        for i, f in enumerate(M.vtable_slots(vt)):
            out.setdefault(M._follow_jmp(f), []).append("%s::slot%d" % (cls, i))
    return out


def insn_at(M, func, addr):
    """Instruction list of func and the index of the one at addr (None when not found)."""
    L = M.insns(func)
    for i, ins in enumerate(L):
        if ins.address == addr:
            return L, i
    return L, None


def describe(M, ins):
    """One short token for an instruction that matters to a reader, else None."""
    mn = ins.mnemonic
    if mn in ("call", "jmp"):
        ct = M.call_target(ins)
        if not ct:
            return None
        k, v = ct
        if k == "import":
            return "%s %s" % (mn, fmt_imp(v))
        if k == "direct":
            if mn == "jmp" and M.func_of(v) == M.func_of(ins.address) and M.func_of(v) is not None:
                return None
            return "%s f%x" % (mn, v)
        if k == "vslot":
            return "%s [%s+0x%x]" % (mn, v[0], v[1]) if isinstance(v[1], int) else "%s [%s]" % (mn, v[0])
        if k == "gptr":
            return "%s [g+0x%x]" % (mn, v)
        return "%s %s" % (mn, v)
    if mn.startswith("ret"):
        return "ret"
    if mn in ("xor", "mov", "or", "sete", "setne", "movzx") and ins.operands:
        op0 = ins.operands[0]
        if op0.type == X.X86_OP_REG and ins.reg_name(op0.reg) in RET_REGS:
            if mn == "xor" and ins.operands[1].type == X.X86_OP_REG and ins.operands[1].reg == op0.reg:
                return "ret-reg=0"
            if mn == "mov" and ins.operands[1].type == X.X86_OP_IMM:
                return "ret-reg=%d" % ins.operands[1].imm
            if mn == "mov":
                return "ret-reg<-%s" % ins.op_str.split(",", 1)[1].strip()
            return "ret-reg:%s" % mn
    if mn.startswith("j"):
        return "%s->%x" % (mn, ins.operands[0].imm) if ins.operands[0].type == X.X86_OP_IMM else mn
    if mn == "cmp" or mn == "test":
        return None
    return None


def path(M, func, start, limit):
    """Tokens of the straight-line path from `start`: unconditional jumps inside the function
    are followed, conditional ones are shown and fallen through."""
    L = M.insns(func)
    by = {ins.address: i for i, ins in enumerate(L)}
    out, n, a, seen = [], 0, start, set()
    while a in by and n < limit and a not in seen:
        seen.add(a)
        ins = L[by[a]]
        n += 1
        tok = describe(M, ins)
        if ins.mnemonic == "jmp" and ins.operands[0].type == X.X86_OP_IMM and ins.operands[0].imm in by:
            a = ins.operands[0].imm
            continue
        if tok:
            out.append(tok)
        if ins.mnemonic.startswith("ret") or ins.mnemonic == "jmp" or ins.mnemonic == "int3":
            break
        a = ins.address + ins.size
    else:
        if n >= limit:
            out.append("...")
        elif a not in by:
            out.append("(leaves at %x)" % a)
    return " ; ".join(out) if out else "(nothing of note in %d insns)" % n


def main():
    a = sys.argv[1:]
    val = int(a[a.index("--value") + 1], 0) if "--value" in a else 2
    ln = int(a[a.index("--len") + 1], 0) if "--len" in a else 14
    only = set(int(x, 16) for x in a[a.index("--only") + 1].split(",")) if "--only" in a else None
    M = get(a[0])
    word = int(a[1], 16)
    slots = slot_index(M)
    refs = sorted(M.data_refs(word))
    print("%s+0x%x: %d reference(s); paths for word == %d" % (M.short, word, len(refs), val))
    for site, mn, first in refs:
        if only and site not in only:
            continue
        func = M.start_of(site)
        L, i = insn_at(M, func, site)
        where = ", ".join(slots.get(func, [])[:4]) or "(in no RTTI vtable)"
        print("\n%x  in f%x (%d bytes)  %s" % (site, func, M.size(func) or 0, where))
        if i is None:
            print("    (instruction not found in the function's decode)")
            continue
        ins = L[i]
        if mn != "cmp":
            print("    %s %s" % (ins.mnemonic, "(write)" if first else "(read)"))
            continue
        imm = [op.imm for op in ins.operands if op.type == X.X86_OP_IMM]
        k = imm[0] if imm else None
        nxt = L[i + 1] if i + 1 < len(L) else None
        if nxt is None or not nxt.mnemonic.startswith("j") or nxt.operands[0].type != X.X86_OP_IMM:
            print("    cmp %s then %s (not a plain branch)" % (k, nxt.mnemonic if nxt else "?"))
            # setcc / cmov users: show the straight path
            print("    after: " + path(M, func, ins.address + ins.size, ln))
            continue
        tgt, fall = nxt.operands[0].imm, nxt.address + nxt.size
        j = nxt.mnemonic
        # which way does word == val go?
        taken = None
        if k is not None:
            if j in ("je", "jz"):
                taken = (val == k)
            elif j in ("jne", "jnz"):
                taken = (val != k)
            elif j in ("jl",):
                taken = (val < k)
            elif j in ("jge",):
                taken = (val >= k)
            elif j in ("jle",):
                taken = (val <= k)
            elif j in ("jg",):
                taken = (val > k)
            elif j in ("jb",):
                taken = (val < k)
            elif j in ("jae",):
                taken = (val >= k)
            elif j in ("jbe",):
                taken = (val <= k)
            elif j in ("ja",):
                taken = (val > k)
        print("    cmp word, %s ; %s -> %x" % (k, j, tgt))
        if taken is None:
            print("    branch: " + path(M, func, tgt, ln))
            print("    fall:   " + path(M, func, fall, ln))
        else:
            a_val, a_oth = (tgt, fall) if taken else (fall, tgt)
            print("    ==%d (%s): %s" % (val, "branch" if taken else "fall", path(M, func, a_val, ln)))
            print("    other:      %s" % path(M, func, a_oth, ln))


if __name__ == "__main__":
    main()
