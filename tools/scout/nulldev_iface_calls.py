#!/usr/bin/env python3
"""Which methods of the material system the other DLLs call, by vtable slot (design-null-device).

    .venv/bin/python tools/scout/nulldev_iface_calls.py MODULE [MODULE ...] [--iface NAME] [--sites SLOT]
                                                        [--ret SLOT] [--reach]
    .venv/bin/python tools/scout/nulldev_iface_calls.py MODULE --walk 0xROOT[,0xROOT...] --sites SLOT [--depth N]

Every game DLL carries a table of (interface name, address of a global) pairs that its connect
fills from the launcher's factory (finding 37, 2.4).  This script finds the globals paired with
NAME (default `VMaterialSystem083`), then every instruction that loads one of them, and follows
the loaded pointer in straight-line order: `mov V, [O]` reads the vtable, `call [V+disp]` or
`jmp [V+disp]` is a call of slot disp/8.  Registers are tracked through register-to-register
moves; after any call the registers a callee may change are forgotten, so an object kept in a
saved register is followed across calls.  The walk stops at a return, an unconditional jump or
60 instructions.

Output: per module the globals, the number of loads, the loads that led to no call the walk
could see (the pointer was stored, passed on or tested), and the slots called with their site
counts; then the union over the modules given.

  --sites SLOT   also list the call sites of that slot (address and enclosing function)
  --ret SLOT     treat the pointer returned by that slot as a second object and count the slots
                 called on it in the same way (for the method that hands out the render context)
  --reach        mark each slot with what nulldev_reach.py finds for the material system's
                 function in that slot with the mode word at 2 (DEVICE = a use of a zero object
                 is reachable by direct calls)

  --walk ROOTS   (one module, with --sites) instead of the whole module, say whether a call
                 site of that slot is reachable from each root by direct calls and tail jumps
                 (depth N, default 8); calls through vtables and pointers are not followed

Limits: only calls made on a pointer freshly loaded from the global are seen.  A pointer kept in
an object field, passed as an argument or reloaded from the stack is not followed, so the counts
are a lower bound on what a module calls.  Prints addresses, slot numbers and counts.
OriginSDK.dll is refused (flow_lib).
"""

import struct
import sys

from flow_lib import get
import capstone
from capstone import x86_const as X

VOLATILE = {X.X86_REG_RAX, X.X86_REG_RCX, X.X86_REG_RDX, X.X86_REG_R8, X.X86_REG_R9, X.X86_REG_R10, X.X86_REG_R11}
WINDOW = 60


def iface_globals(M, name):
    """Globals paired with the interface name in the module's connect tables."""
    hits = set(r for r, s in M.strs.items() if s == name)
    out = []
    for sec in M.m.data_sections():
        buf = M.m.data[sec["rptr"]:sec["rptr"] + sec["rsize"]]
        n = len(buf) // 8
        vals = struct.unpack_from("<%dQ" % n, buf, 0)
        for i, v in enumerate(vals[:-1]):
            if v - M.m.base in hits:
                g = vals[i + 1] - M.m.base
                s = M.m.pe.section_of(g) if 0 < g < 0x40000000 else None
                if s and not M.m.pe.is_exec(s):
                    out.append(g)
    return sorted(set(out))


def follow(M, md, site, ret_slot):
    """Slots called on the object loaded at `site`: ([(call site, slot)], [(call site, slot)] on
    the object returned by ret_slot)."""
    buf = M.m.bytes_at(site, 16 * WINDOW)
    obj, vt, obj2, vt2 = set(), set(), set(), set()
    calls, calls2 = [], []
    first = True
    n = 0
    for ins in md.disasm(buf, site):
        n += 1
        if n > WINDOW:
            break
        ops = ins.operands
        mn = ins.mnemonic
        if first:
            first = False
            if mn != "mov" or len(ops) != 2 or ops[0].type != X.X86_OP_REG:
                return None
            obj.add(ops[0].reg)
            continue
        if mn in ("call", "jmp"):
            op = ops[0]
            got_ret = False
            if op.type == X.X86_OP_MEM and op.mem.base and not op.mem.index and op.mem.base != X.X86_REG_RIP:
                if op.mem.base in vt:
                    calls.append((ins.address, op.mem.disp // 8))
                    got_ret = (op.mem.disp // 8 == ret_slot)
                elif op.mem.base in vt2:
                    calls2.append((ins.address, op.mem.disp // 8))
            if mn == "jmp":
                break
            for s in (obj, vt, obj2, vt2):
                s -= VOLATILE
            if got_ret:
                obj2.add(X.X86_REG_RAX)
            continue
        if mn.startswith("ret") or mn == "int3":
            break
        if mn == "mov" and len(ops) == 2 and ops[0].type == X.X86_OP_REG:
            d = ops[0].reg
            for s in (obj, vt, obj2, vt2):
                s.discard(d)
            if ops[1].type == X.X86_OP_REG:
                for s in (obj, vt, obj2, vt2):
                    if ops[1].reg in s:
                        s.add(d)
            elif ops[1].type == X.X86_OP_MEM and not ops[1].mem.index and ops[1].mem.disp == 0:
                if ops[1].mem.base in obj:
                    vt.add(d)
                elif ops[1].mem.base in obj2:
                    vt2.add(d)
            continue
        if ops and ops[0].type == X.X86_OP_REG and mn not in ("cmp", "test", "push"):
            for s in (obj, vt, obj2, vt2):
                s.discard(ops[0].reg)
        if not (obj or vt or obj2 or vt2):
            break
    return calls, calls2


def walk(M, roots, name, slot, depth):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    sites = {}
    for g in iface_globals(M, name):
        for site, mn, fm in M.data_refs(g):
            if mn == "mov" and not fm:
                r = follow(M, md, site, -1)
                for cs, s in (r[0] if r else []):
                    if s == slot:
                        sites.setdefault(M.start_of(cs), []).append(cs)
    print("%s: %d function(s) hold a call of slot %d of %s" % (M.short, len(sites), slot, name))
    for root in roots:
        seen, todo, hits, blind = {root: None}, [(root, 0)], [], 0
        while todo:
            f, d = todo.pop(0)
            if f in sites:
                chain, x = [], f
                while x is not None:
                    chain.append("f%x" % x)
                    x = seen[x]
                hits.append("%x via %s" % (sites[f][0], " > ".join(chain[::-1])))
            for ins in M.insns(f):
                if ins.mnemonic not in ("call", "jmp"):
                    continue
                ct = M.call_target(ins)
                if not ct:
                    continue
                if ct[0] == "direct":
                    if d < depth and ct[1] not in seen and (ins.mnemonic == "call" or M.func_of(ct[1]) != M.func_of(f)):
                        seen[ct[1]] = f
                        todo.append((ct[1], d + 1))
                elif ct[0] != "import":
                    blind += 1
        print("  root f%x: %d function(s) walked, %d indirect call(s) not followed, %d site(s) reachable" % (
            root, len(seen), blind, len(hits)))
        for h in hits[:4]:
            print("      " + h)


def main():
    a = sys.argv[1:]

    def opt(name, default=None):
        return a[a.index(name) + 1] if name in a else default
    name = opt("--iface", "VMaterialSystem083")
    sites_of = int(opt("--sites", "-1"))
    ret_slot = int(opt("--ret", "-1"))
    skip = set()
    for o in ("--iface", "--sites", "--ret"):
        if o in a:
            skip.add(a.index(o))
            skip.add(a.index(o) + 1)
    mods = [x for i, x in enumerate(a) if i not in skip and not x.startswith("--")]
    for o in ("--walk", "--depth"):
        if o in a:
            mods = [m for m in mods if m not in (opt(o),)]
    if "--walk" in a:
        walk(get(mods[0]), [int(x, 16) for x in opt("--walk").split(",")], name, sites_of, int(opt("--depth", "8")))
        return
    union, union2 = {}, {}
    for mod in mods:
        M = get(mod)
        md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        md.detail = True
        gl = iface_globals(M, name)
        loads = blind = 0
        slots, slots2 = {}, {}
        listed = []
        for g in gl:
            for site, mn, fm in M.data_refs(g):
                if mn != "mov" or fm:
                    continue
                r = follow(M, md, site, ret_slot)
                if r is None:
                    continue
                loads += 1
                calls, calls2 = r
                if not calls:
                    blind += 1
                for cs, s in calls:
                    slots.setdefault(s, set()).add(cs)
                    if s == sites_of:
                        listed.append(cs)
                for cs, s in calls2:
                    slots2.setdefault(s, set()).add(cs)
        print("%s: globals %s; %d load(s), %d with no call seen; %d distinct slot(s), %d call site(s)" % (
            M.short, ", ".join("+0x%x" % g for g in gl) or "(none found)", loads, blind,
            len(slots), sum(len(v) for v in slots.values())))
        if slots2:
            print("    on the object returned by slot %d: %d distinct slot(s), %d call site(s)" % (
                ret_slot, len(slots2), sum(len(v) for v in slots2.values())))
        for cs in sorted(set(listed)):
            f = M.start_of(cs)
            print("    slot %d called at %s+0x%x in f%x" % (sites_of, M.short, cs, f))
        for s, v in slots.items():
            union.setdefault(s, {})[M.short] = len(v)
        for s, v in slots2.items():
            union2.setdefault(s, {})[M.short] = len(v)

    reach = None
    if "--reach" in a and name == "VMaterialSystem083":
        import nulldev_reach as NR
        MS = get(NR.MODULE)
        R = NR.Reach(MS, 2, 0)
        tabs = {c: vt for vt, c in MS.vtables().items()}
        reach = (MS, R, MS.vtable_slots(tabs["CMaterialSystem"]), MS.vtable_slots(tabs["CMatRenderContext"]))

    def show(title, u, which):
        print("\n%s: %d distinct slot(s) over %s" % (title, len(u), ", ".join(mods)))
        ndev = 0
        for s in sorted(u):
            mark = ""
            if reach:
                MS, R, t1, t2 = reach
                tab = t1 if which == 1 else t2
                if 0 <= s < len(tab):
                    f = MS._follow_jmp(tab[s])
                    d, nf, virt = R.search(f, 8, False)
                    mark = "  f%x %s" % (f, "DEVICE" if d else "clean (%d insn)" % len(MS.insns(f)))
                    ndev += bool(d)
                else:
                    mark = "  (no such slot)"
            print("   slot %3d  %s%s" % (s, " ".join("%s:%d" % kv for kv in sorted(u[s].items())), mark))
        if reach:
            print("   of these, %d reach a zero object in mode 2 by direct calls" % ndev)

    show("Union, %s" % name, union, 1)
    if union2:
        show("Union, object returned by slot %d" % ret_slot, union2, 2)


if __name__ == "__main__":
    main()
