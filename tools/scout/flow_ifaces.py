#!/usr/bin/env python3
"""Interfaces a game DLL registers with its `CreateInterface` export (finding 37).

    .venv/bin/python tools/scout/flow_ifaces.py MODULE [NAME_REGEX] [--slots A-B]

Reads the list head from the code of the `CreateInterface` export, finds the static constructors
that push a node on that list (directly, or through a helper they call), and for each
registration prints: the interface name, the creator function, the object it returns, the
object's class (RTTI of the vtable its constructor stores) and the table of dependencies its
slot 6 returns (pairs of DLL name and interface name), when slot 6 has the shape
`lea rax, [table]; ret`.  With --slots, also the functions in those vtable slots.

How the names of the slots are known: the launcher calls slot 1 with its factory, slot 4 in the
initialisation stage, slot 6 in the "LOADING DEPENDENCIES" stage (launcher+0x12f60, +0x13eb0,
+0x14940).  Prints addresses, names and strings.
OriginSDK.dll is refused.
"""

import re
import sys

from flow_lib import get
from capstone import x86_const as X


def rip_target(ins):
    for op in ins.operands:
        if op.type == X.X86_OP_MEM and op.mem.base == X.X86_REG_RIP:
            return ins.address + ins.size + op.mem.disp
    return None


def list_head(M):
    ci = M.export_by_name.get("CreateInterface")
    if ci is None:
        return None
    todo, seen = [ci], set()
    while todo:
        f = todo.pop(0)
        if f in seen or len(seen) > 4:
            continue
        seen.add(f)
        for ins in M.insns(M.func_of(f) if M.func_of(f) is not None else f):
            if ins.mnemonic == "mov" and rip_target(ins) is not None and ins.operands[0].type == X.X86_OP_REG:
                t = rip_target(ins)
                if t not in M.slots:
                    return t
            if ins.mnemonic in ("call", "jmp"):
                ct = M.call_target(ins)
                if ct and ct[0] == "direct" and M.func_of(ct[1]) != M.func_of(ins.address):
                    todo.append(ct[1])
    return None


def registrations(M, head):
    writers = set()
    for site, mn, fm in M.data_refs(head):
        if mn == "mov" and fm:
            writers.add(M.start_of(site))
    cands = set(writers)
    for w in writers:
        for site, mn, f in M.callers(w):
            if f is not None:
                cands.add(f)
    out = []
    for f in sorted(cands):
        names, codes = [], []
        for ins in M.insns(f):
            if ins.mnemonic == "lea":
                t = rip_target(ins)
                if t is None:
                    continue
                s = M.cstr(t)
                if M.is_code(t):
                    codes.append(t)
                elif s and len(s) >= 3 and re.match(r"^[A-Za-z_][A-Za-z0-9_ .]*$", s):
                    names.append(s)
        if len(names) == 1 and len(codes) == 1:
            out.append((names[0], codes[0], f))
        elif names and codes and len(names) == len(codes):
            out.extend((n, c, f) for n, c in zip(names, codes))
    return out


def creator_object(M, fn):
    ins = M.insns(M.func_of(fn) if M.func_of(fn) is not None else fn)
    if len(ins) <= 3 and ins and ins[0].mnemonic == "lea" and rip_target(ins[0]) is not None:
        return rip_target(ins[0])
    return None


def object_vtables(M, obj, creator=None):
    """Candidate vtables of the object at obj: (site, vtable) for every vtable address that a
    rip-relative move stores at obj, and every RTTI vtable address loaded in a function that is
    called right after obj's address is taken (a constructor called with the object)."""
    out = []
    for site, mn, fm in M.data_refs(obj):
        f = M.start_of(site)
        if mn == "mov" and fm:
            prev = None
            for ins in M.insns(f):
                if ins.address == site:
                    break
                if ins.mnemonic == "lea" and rip_target(ins) is not None and M.vtable_class(rip_target(ins)):
                    prev = rip_target(ins)
            if prev is not None:
                out.append((site, prev))
        elif mn == "lea" and f != creator:
            after = False
            for ins in M.insns(f):
                if ins.address == site:
                    after = True
                elif after and ins.mnemonic in ("call", "jmp"):
                    ct = M.call_target(ins)
                    if ct and ct[0] == "direct":
                        g = M.func_of(ct[1]) if M.func_of(ct[1]) is not None else ct[1]
                        for j in M.insns(g):
                            if j.mnemonic == "lea" and rip_target(j) is not None and M.vtable_class(rip_target(j)):
                                out.append((j.address, rip_target(j)))
                    break
    return out


def best_vtable(M, vts):
    """The most derived candidate: most slots; a class that is not an `I...` interface before
    one that is; on a tie the candidate found last (constructors store the base class first)."""
    best, bk = None, None
    for i, (s, v) in enumerate(vts):
        c = M.vtable_class(v) or ""
        k = (len(M.vtable_slots(v)), not c.startswith("I"), i)
        if bk is None or k > bk:
            best, bk = v, k
    return best


def dependencies(M, vt):
    f = M.m.u64(vt + 8 * 6)
    if f is None:
        return None, None
    f -= M.m.base
    ins = M.insns(M.func_of(f) if M.func_of(f) is not None else f)
    if len(ins) <= 3 and ins and ins[0].mnemonic == "lea" and rip_target(ins[0]) is not None:
        t = rip_target(ins[0])
        pairs = []
        for i in range(64):
            a, b = M.m.u64(t + 16 * i), M.m.u64(t + 16 * i + 8)
            if not a or not b:
                break
            pairs.append((M.cstr(a - M.m.base) or "?", M.cstr(b - M.m.base) or "?"))
        return f, pairs
    if len(ins) <= 3 and ins and ins[0].mnemonic in ("xor", "mov"):
        return f, []
    return f, None


def main():
    a = sys.argv[1:]
    slots = None
    if "--slots" in a:
        lo, hi = a[a.index("--slots") + 1].split("-")
        slots = (int(lo), int(hi))
        del a[a.index("--slots"):a.index("--slots") + 2]
    M = get(a[0])
    rx = re.compile(a[1]) if len(a) > 1 else None
    head = list_head(M)
    if head is None:
        print("%s: no CreateInterface export or list head not found" % M.short)
        return
    regs = registrations(M, head)
    print("%s: list head %s+0x%x, %d registration(s)" % (M.short, M.short, head, len(regs)))
    for name, fn, ctor in sorted(regs):
        if rx and not rx.search(name):
            continue
        obj = creator_object(M, fn)
        line = "  %-34s creator +0x%x" % (name, fn)
        if obj is None:
            print(line + "  (creator is not `lea; ret`)")
            continue
        vts = object_vtables(M, obj, fn)
        vt = best_vtable(M, vts)
        if vt is None:
            print(line + "  object +0x%x  (no vtable store found)" % obj)
            continue
        print(line + "  object +0x%x  %s @+0x%x (%d slots)" % (obj, M.vtable_class(vt), vt, len(M.vtable_slots(vt))))
        if True:
            f, deps = dependencies(M, vt)
            if deps is None:
                print("      slot 6 +0x%x: not a plain table" % f if f is not None else "      no slot 6")
            elif deps:
                print("      dependencies (slot 6 +0x%x): %s" % (f, "; ".join("%s %s" % d for d in deps)))
            if slots:
                for i in range(slots[0], slots[1] + 1):
                    v = M.m.u64(vt + 8 * i)
                    print("      slot %2d  +0x%x  %d bytes" % (i, v - M.m.base, M.size(v - M.m.base)))


if __name__ == "__main__":
    main()
