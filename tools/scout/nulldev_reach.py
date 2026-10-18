#!/usr/bin/env python3
"""Which material-system entry points still reach the Direct3D objects in mode 2 (design-null-device).

    .venv/bin/python tools/scout/nulldev_reach.py --surface CLASS_REGEX [options]   # per vtable slot
    .venv/bin/python tools/scout/nulldev_reach.py --root 0xFUNC[,0xFUNC...] [options] # one function, with chains
    .venv/bin/python tools/scout/nulldev_reach.py --census [options]                  # every user of the globals

    options: --mode N      value of the mode word to assume (default 2; 1 = the normal start)
             --active 0|1  what the "device is up" predicate returns (default: not assumed)
             --depth N     how many direct-call levels to follow (default 8)
             --ptr         also follow function addresses taken with `lea` (queued render-thread
                           calls and callbacks), not only direct calls
             --list        with --surface: one line per slot instead of the totals only

The question: `-noshaderapi` sets the material system's mode word (materialsystem_dx11+0x21e0e0,
finding 37) to 2, and in that mode the device function never runs, so the globals that hold the
Direct3D and DXGI objects (finding 31, section 1) stay zero.  Code that loads one of them and
uses it as a pointer then faults at address 0.  This script walks each function the way the
code would run with the mode word fixed, and reports where such a use is still reachable.

How a function is walked.  Instructions are followed from the entry.  A conditional branch is
decided, instead of followed both ways, only when it tests one of two things:
  - `cmp dword [mode word], K` (the branch may be separated from the compare by instructions
    that leave the flags alone);
  - the byte result of a known predicate just called: materialsystem_dx11+0x60f50 and +0x494d0
    ("mode is 2 or more"), +0x60f90 ("mode is 1"), and, only with --active, +0x16f70 ("the
    device is up": the byte +0x14edd9a, whose one writer is the swap-chain function +0x16af0).
What is known is forgotten at every jump target, so a join never inherits one path's facts.
A function with a jump through a register or a table is walked whole (nothing pruned).

What is reported as a use.  A reachable instruction that loads one of the object globals into a
register, classed by what happens to that register next in straight-line order:
  deref   it is used as the base of a memory operand before any test: a fault when it is zero
  tested  it is compared with zero first (the code expects that it may be missing)
  passed  neither within 8 instructions (handed to a callee or stored)
Only `deref` counts against a slot.

What the walk cannot see: calls through a vtable, a register or a stored pointer (their number
on the walked paths is printed).  So "clean" means "no direct path", not "safe"; a slot that
calls another slot of the same class virtually inherits nothing from it here, but that other
slot is reported on its own line.

Prints addresses, counts, class names from RTTI and the names of the globals.  Terminal use.
OriginSDK.dll is refused (flow_lib).
"""

import re
import sys
from collections import deque

from flow_lib import get
from capstone import x86_const as X

MODULE = "materialsystem_dx11"
MODE_WORD = 0x21e0e0
# the object globals of finding 31, section 1
DEVS = {
    0x14e8dd0: "device", 0x14e8dd8: "context", 0x14e8dc8: "dxgi-device", 0x14e8dc0: "dxgi-factory",
    0x14ee258: "swapchain",
    0x14ee260: "backbuffer", 0x14ee288: "backbuffer2",
    0x14e8e38: "rtv", 0x14e8e28: "rtv2", 0x14e8e08: "dsv",
    0x14e8e30: "srv", 0x14e8e20: "uav", 0x14e8e00: "depth-res",
}
ACTIVE_PRED = 0x16f70


def mode_preds(mode):
    return {0x60f50: int(mode >= 2), 0x494d0: int(mode >= 2), 0x60f90: int(mode == 1)}


RAX_FAMILY = {X.X86_REG_RAX, X.X86_REG_EAX, X.X86_REG_AX, X.X86_REG_AL, X.X86_REG_AH}
JCC = {
    "je": lambda a, b: a == b, "jne": lambda a, b: a != b,
    "jl": lambda a, b: a < b, "jge": lambda a, b: a >= b,
    "jle": lambda a, b: a <= b, "jg": lambda a, b: a > b,
    "jb": lambda a, b: a < b, "jae": lambda a, b: a >= b,
    "jbe": lambda a, b: a <= b, "ja": lambda a, b: a > b,
}


def rip_target(ins):
    for i, op in enumerate(ins.operands):
        if op.type == X.X86_OP_MEM and op.mem.base == X.X86_REG_RIP:
            return i, ins.address + ins.size + op.mem.disp
    return None, None


class Reach:
    def __init__(self, M, mode, active):
        self.M, self.mode = M, mode
        self.preds = dict(mode_preds(mode))
        if active is not None:
            self.preds[ACTIVE_PRED] = active
        self.memo = {}

    def analyse(self, func):
        if func in self.memo:
            return self.memo[func]
        M = self.M
        L = M.insns(func)
        by = {ins.address: i for i, ins in enumerate(L)}
        targets = set()
        whole = False
        for ins in L:
            if ins.mnemonic[0] == "j" and ins.operands:
                op = ins.operands[0]
                if op.type == X.X86_OP_IMM:
                    if op.imm in by:
                        targets.add(op.imm)
                elif ins.mnemonic == "jmp" and not (op.type == X.X86_OP_MEM and op.mem.base == X.X86_REG_RIP):
                    if not (op.type == X.X86_OP_MEM and op.mem.base and not op.mem.index):
                        whole = True        # jmp reg / jmp [table + reg*8]: a switch
        res = {"calls": [], "ptrs": [], "uses": [], "virt": 0, "decided": 0, "whole": whole}
        reach = set()
        if whole:
            reach = set(range(len(L)))
        else:
            todo = [func] if func in by else []
            while todo:
                a = todo.pop()
                flags, al = None, None
                while a in by and by[a] not in reach:
                    i = by[a]
                    ins = L[i]
                    if a in targets:
                        flags, al = None, None
                    reach.add(i)
                    mn = ins.mnemonic
                    if mn[0] == "j":
                        op = ins.operands[0]
                        if mn == "jmp":
                            if op.type == X.X86_OP_IMM and op.imm in by:
                                todo.append(op.imm)
                            break
                        if op.type == X.X86_OP_IMM:
                            if flags is not None and mn in JCC:
                                res["decided"] += 1
                                if JCC[mn](flags[0], flags[1]):
                                    if op.imm in by:
                                        todo.append(op.imm)
                                    break
                                a = ins.address + ins.size
                                continue
                            if op.imm in by:
                                todo.append(op.imm)
                        a = ins.address + ins.size
                        continue
                    if mn.startswith("ret") or mn == "int3":
                        break
                    if mn == "call":
                        ct = M.call_target(ins)
                        flags = None
                        al = self.preds.get(ct[1]) if ct and ct[0] == "direct" else None
                        a = ins.address + ins.size
                        continue
                    if mn == "cmp":
                        k, t = rip_target(ins)
                        imm = [o.imm for o in ins.operands if o.type == X.X86_OP_IMM]
                        if k == 0 and t == MODE_WORD and imm:
                            flags = (self.mode, imm[0])
                        elif al is not None and ins.operands[0].type == X.X86_OP_REG and \
                                ins.operands[0].reg in RAX_FAMILY and imm:
                            flags = (al, imm[0])
                        else:
                            flags = None
                    elif mn == "test":
                        o0, o1 = ins.operands
                        if al is not None and o0.type == X.X86_OP_REG and o1.type == X.X86_OP_REG and \
                                o0.reg == o1.reg and o0.reg in RAX_FAMILY:
                            flags = (al, 0)
                        else:
                            flags = None
                    elif flags is not None or al is not None:
                        try:
                            _, wr = ins.regs_access()
                        except Exception:
                            wr = None
                        if wr is None:
                            flags, al = None, None
                        else:
                            if X.X86_REG_EFLAGS in wr:
                                flags = None
                            if RAX_FAMILY & set(wr):
                                al = None
                    a = ins.address + ins.size
        # collect from the reachable instructions
        order = sorted(reach)
        for n, i in enumerate(order):
            ins = L[i]
            mn = ins.mnemonic
            if mn in ("call", "jmp"):
                ct = M.call_target(ins)
                if ct:
                    if ct[0] == "direct":
                        if mn == "call" or ct[1] not in by:
                            res["calls"].append((ins.address, ct[1]))
                    elif ct[0] in ("vslot", "reg", "gptr"):
                        if mn == "call" or ct[0] != "reg":
                            res["virt"] += 1
                continue
            k, t = rip_target(ins)
            if t is None:
                continue
            if mn == "lea" and M.is_code(t) and t not in by:
                res["ptrs"].append((ins.address, t))
                continue
            if t in DEVS:
                kind = "other"
                if mn == "mov" and k == 1 and ins.operands[0].type == X.X86_OP_REG:
                    reg = ins.operands[0].reg
                    kind = "passed"
                    for j in order[n + 1:n + 9]:
                        nx = L[j]
                        if nx.mnemonic in ("test", "cmp") and nx.operands[0].type == X.X86_OP_REG \
                                and nx.operands[0].reg == reg:
                            kind = "tested"
                            break
                        if any(o.type == X.X86_OP_MEM and o.mem.base == reg for o in nx.operands):
                            kind = "deref"
                            break
                        if nx.mnemonic in ("call", "ret") or nx.mnemonic[0] == "j":
                            break
                        if nx.operands and nx.operands[0].type == X.X86_OP_REG and nx.operands[0].reg == reg \
                                and nx.mnemonic in ("mov", "lea", "xor"):
                            break
                elif mn == "cmp":
                    kind = "tested"
                elif mn == "mov" and k == 0:
                    kind = "store"
                elif mn == "lea":
                    kind = "addr"
                res["uses"].append((ins.address, DEVS[t], kind))
        self.memo[func] = res
        return res

    def search(self, root, depth, ptr):
        """Breadth-first over the pruned call graph.  Returns (derefs, n_funcs, n_virt) where
        derefs is [(site, global, chain of functions, via_ptr)] in order of distance."""
        seen = {root: (None, False)}
        q = deque([(root, 0)])
        out, virt = [], 0
        while q:
            f, d = q.popleft()
            r = self.analyse(f)
            virt += r["virt"]
            for site, g, kind in r["uses"]:
                if kind == "deref":
                    chain, x, viap = [], f, False
                    while x is not None:
                        chain.append(x)
                        x, p = seen[x]
                        viap = viap or p
                    out.append((site, g, chain[::-1], viap))
            if d >= depth:
                continue
            edges = [(t, False) for _, t in r["calls"]]
            if ptr:
                edges += [(t, True) for _, t in r["ptrs"]]
            for t, p in edges:
                if t not in seen:
                    seen[t] = (f, p)
                    q.append((t, d + 1))
        return out, len(seen), virt


def fmt_chain(chain):
    return " > ".join("f%x" % c for c in chain)


def main():
    a = sys.argv[1:]

    def opt(name, default=None):
        return a[a.index(name) + 1] if name in a else default
    mode = int(opt("--mode", "2"))
    active = opt("--active")
    active = int(active) if active is not None else None
    depth = int(opt("--depth", "8"))
    ptr = "--ptr" in a
    M = get(MODULE)
    R = Reach(M, mode, active)
    print("%s, mode word = %d, device-up predicate %s, depth %d, %s" % (
        MODULE, mode, "not assumed" if active is None else "= %d" % active, depth,
        "direct calls and taken addresses" if ptr else "direct calls only"))

    if "--root" in a:
        for tok in opt("--root").split(","):
            root = int(tok, 16)
            derefs, nf, virt = R.search(root, depth, ptr)
            r0 = R.analyse(root)
            print("\nf%x: %d function(s) walked, %d indirect call(s) not followed, %d branch(es) decided in the root"
                  % (root, nf, virt, r0["decided"]))
            if not derefs:
                print("    no use of a zero object reachable")
            seen_site = set()
            for site, g, chain, viap in derefs:
                if site in seen_site:
                    continue
                seen_site.add(site)
                print("    %x  %-12s %s%s" % (site, g, fmt_chain(chain), "  (through a taken address)" if viap else ""))
        return

    if "--census" in a:
        users = {}
        for addr, size, mn, tgt, fm in M.xr["rip"]:
            if tgt in DEVS:
                f = M.start_of(addr)
                users.setdefault(f, 0)
                users[f] += 1
        guarded = unguarded = 0
        n_deref = 0
        for f in sorted(users):
            r = R.analyse(f)
            d = [u for u in r["uses"] if u[2] == "deref"]
            if d:
                unguarded += 1
                n_deref += len(d)
            else:
                guarded += 1
        print("functions that refer to an object global: %d" % len(users))
        print("  with a zero-object use still reachable from their own entry: %d (%d sites)" % (unguarded, n_deref))
        print("  with none (every use sits behind a test of the mode, of the predicate, or of the pointer): %d" % guarded)
        return

    rx = re.compile(opt("--surface", "."))
    tot = {}
    for vt, cls in sorted(M.vtables().items()):
        if not rx.search(cls):
            continue
        slots = M.vtable_slots(vt)
        c = {"slots": len(slots), "dev": 0, "clean": 0, "small": 0, "guard": 0}
        lines = []
        for i, f in enumerate(slots):
            f = M._follow_jmp(f)
            derefs, nf, virt = R.search(f, depth, ptr)
            r0 = R.analyse(f)
            n_ins = len(M.insns(f))
            if derefs:
                c["dev"] += 1
                site, g, chain, viap = derefs[0]
                lines.append("   %3d  f%-7x DEVICE  %x %s  %s%s" % (
                    i, f, site, g, fmt_chain(chain), " (taken address)" if viap else ""))
            else:
                c["clean"] += 1
                if n_ins <= 4:
                    c["small"] += 1
                if r0["decided"]:
                    c["guard"] += 1
                lines.append("   %3d  f%-7x clean   %d insn, %d function(s) walked, %d indirect not followed%s" % (
                    i, f, n_ins, nf, virt, ", branch decided by the mode" if r0["decided"] else ""))
        print("\n%s (vtable +0x%x): %d slots; %d reach a zero object, %d do not "
              "(of those %d are 4 instructions or fewer, %d have a branch the mode decides)"
              % (cls, vt, c["slots"], c["dev"], c["clean"], c["small"], c["guard"]))
        if "--list" in a:
            print("\n".join(lines))
        tot[cls] = c


if __name__ == "__main__":
    main()
