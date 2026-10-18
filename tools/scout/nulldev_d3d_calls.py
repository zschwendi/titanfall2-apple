#!/usr/bin/env python3
"""Direct3D and DXGI methods called on the five global objects, whole module or from start-up
roots, and how many call sites look at the result (design-null-device).

    .venv/bin/python tools/scout/nulldev_d3d_calls.py                      # every site in the module
    .venv/bin/python tools/scout/nulldev_d3d_calls.py --roots 0xA,0xB [--depth N] [--no-ptr]

The material system keeps its device, immediate context, DXGI device, DXGI factory and swap
chain in five globals (finding 31, section 1).  This script finds every load of one of them and
follows the loaded pointer to the vtable call made on it (the same straight-line tracking as
nulldev_iface_calls.py), names the method from the public MinGW-w64 headers in third_party/dxmt
(tools/scout/d3d11_vtables.py: nothing from the game), and looks at the four instructions after
the call for a test of eax (`test eax, eax` or `cmp eax, K`): a site that has one branches on
the HRESULT, a site that has none ignores it.

With --roots only the functions reachable from the roots are counted: direct calls, and (unless
--no-ptr) function addresses taken with `lea`, which is how work is handed to the render thread;
depth N (default 8); the walk is nulldev_reach.py's with the mode word at 1 and nothing assumed
about the device.  It includes branches a normal start does not take and cannot follow calls
through vtables, so it is an upper bound on what those roots call directly and says nothing
about what they reach through an interface.

Objects other than the five (adapters, outputs, textures, views, shaders, queries) are held in
locals and tables and are not seen here; finding 31's table has them.

Prints interface and method names, addresses and counts.  OriginSDK.dll is refused (flow_lib).
"""

import sys
from collections import deque

from flow_lib import get
import capstone
from capstone import x86_const as X
import d3d11_vtables
import nulldev_reach as NR
from nulldev_iface_calls import follow

OBJ = {0x14e8dd0: "ID3D11Device", 0x14e8dd8: "ID3D11DeviceContext", 0x14e8dc8: "IDXGIDevice1",
       0x14e8dc0: "IDXGIFactory1", 0x14ee258: "IDXGISwapChain"}


def closure(R, roots, depth, ptr):
    seen = set(roots)
    q = deque((r, 0) for r in roots)
    while q:
        f, d = q.popleft()
        if d >= depth:
            continue
        r = R.analyse(f)
        for _, t in r["calls"] + (r["ptrs"] if ptr else []):
            if t not in seen:
                seen.add(t)
                q.append((t, d + 1))
    return seen


def result_tested(M, md, site):
    """True when eax is tested within four instructions after the call at `site`."""
    buf = M.m.bytes_at(site, 64)
    n = 0
    for ins in md.disasm(buf, site):
        n += 1
        if n == 1:
            continue
        if n > 5:
            break
        if ins.mnemonic in ("test", "cmp") and ins.operands and ins.operands[0].type == X.X86_OP_REG \
                and ins.operands[0].reg in (X.X86_REG_EAX, X.X86_REG_RAX):
            return True
        if ins.mnemonic in ("call", "ret") or ins.mnemonic == "jmp":
            break
        if ins.mnemonic == "mov" and ins.operands[0].type == X.X86_OP_REG and \
                ins.operands[0].reg in (X.X86_REG_EAX, X.X86_REG_RAX):
            break
    return False


def main():
    a = sys.argv[1:]

    def opt(name, default=None):
        return a[a.index(name) + 1] if name in a else default
    M = get(NR.MODULE)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    ifaces = d3d11_vtables.load()
    only = None
    if "--roots" in a:
        roots = [int(x, 16) for x in opt("--roots").split(",")]
        R = NR.Reach(M, 1, None)
        only = closure(R, roots, int(opt("--depth", "8")), "--no-ptr" not in a)
        print("roots %s: %d function(s) reachable" % (", ".join("f%x" % r for r in roots), len(only)))
    tally = {}
    loads = blind = 0
    for g, iname in OBJ.items():
        meths = ifaces[iname].methods
        for site, mn, fm in M.data_refs(g):
            if mn != "mov" or fm:
                continue
            if only is not None and M.start_of(site) not in only:
                continue
            r = follow(M, md, site, -1)
            if r is None:
                continue
            loads += 1
            if not r[0]:
                blind += 1
            for cs, slot in r[0]:
                name = meths[slot].name if 0 <= slot < len(meths) else "slot%d" % slot
                t = tally.setdefault((iname, name), [set(), set()])
                t[0].add(cs)
                if result_tested(M, md, cs):
                    t[1].add(cs)
    print("%d load(s) of the five globals, %d with no call seen" % (loads, blind))
    by = {}
    for (iname, name), (s, c) in tally.items():
        by.setdefault(iname, []).append((name, len(s), len(c)))
    ns = nc = nm = 0
    for iname in sorted(by):
        rows = sorted(by[iname], key=lambda r: -r[1])
        print("\n%s: %d method(s), %d site(s)" % (iname, len(rows), sum(r[1] for r in rows)))
        print("   " + ", ".join("%s %d%s" % (n, s, ("/%d tested" % c) if c else "") for n, s, c in rows))
        nm += len(rows)
        ns += sum(r[1] for r in rows)
        nc += sum(r[2] for r in rows)
    print("\ntotal: %d method(s), %d call site(s), %d of them test the result" % (nm, ns, nc))


if __name__ == "__main__":
    main()
