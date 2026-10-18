#!/usr/bin/env python3
"""Can the game reach the OpenGL and Direct3D 9 imports of the NVIDIA middleware? (issue #31, Q6)

    .venv/bin/python tools/scout/d3d11_reach.py [MODULE ...]

For GFSDK_SSAO.win64 and GFSDK_TXAA.win64: finds every call site of an `opengl32.dll` or
`d3d9.dll` import, then does a forward walk of the direct-call graph (plus code addresses taken
with lea and the slots of RTTI vtables) from the entry points that belong to each rendering
back end, and reports which imports each back end can reach.  Incremental-link thunks
(`jmp rel32` stubs) are followed.  Prints addresses, names and counts only.

Limits: calls through function pointers loaded from memory are not followed, except vtable
slots of classes known through RTTI; a vtable whose class has no RTTI name is not a root.
"""

import collections
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import scout_ctx  # noqa: E402
import xref_lib  # noqa: E402


class Walker:
    def __init__(self, name):
        self.c = scout_ctx.Ctx(name)
        m = self.c.mod
        self.m = m
        self.thunk = {}   # address of a lone `jmp rel32` -> its target
        for a, mn, tgt in self.c.t["calls"]:
            if mn == "jmp":
                b = m.read(a, 5)
                if b and b[0] == 0xE9 and (m.func_of(a) is None):
                    self.thunk[a] = tgt
        self.edges = collections.defaultdict(set)   # unit -> units
        g = self.c.g
        for cf, callees in g.callees.items():
            for t in callees:
                self.edges[cf].add(self.real(t))
        for tgt, lst in g.taken.items():
            for site, cf in lst:
                if cf is not None:
                    self.edges[cf].add(self.real(tgt))

    def real(self, a):
        seen = 0
        while a in self.thunk and seen < 8:
            a = self.thunk[a]
            seen += 1
        return self.m.unit_of(a) or a

    def class_slots(self, cls_substr):
        out = set()
        for vt, nm in self.c.vts.items():
            if cls_substr == xref_lib.demangle_class(nm):
                slot = 0
                while True:
                    v = self.m.u64(vt + 8 * slot)
                    if v is None:
                        break
                    r = v - self.m.base
                    if not self.m.in_exec(r):
                        break
                    out.add(self.real(r))
                    slot += 1
        return out

    def reach(self, roots):
        seen = set(roots)
        todo = list(roots)
        while todo:
            x = todo.pop()
            for y in self.edges.get(x, ()):
                if y not in seen:
                    seen.add(y)
                    todo.append(y)
        return seen

    def import_units(self, dll):
        sites = self.c.import_call_sites(dll)
        out = collections.defaultdict(set)
        for fname, lst in sites.items():
            for site, kind in lst:
                u = self.m.unit_of(site)
                if u is not None:
                    out[fname].add(u)
        return out


def ssao():
    w = Walker("GFSDK_SSAO.win64")
    ex = xref_lib.export_rvas(w.m.pe)
    print("== GFSDK_SSAO.win64.dll")
    backends = {
        "D3D11": ({w.real(ex["GFSDK_SSAO_CreateContext_D3D11"])},
                  ["GFSDK_SSAO_Context_D3D11", "GFSDK::SSAO::D3D11::Renderer"]),
        "GL": ({w.real(ex["GFSDK_SSAO_CreateContext_GL"])},
               ["GFSDK_SSAO_Context_GL", "GFSDK::SSAO::GL::Renderer"]),
    }
    reached = {}
    for k, (roots, classes) in backends.items():
        r = set(roots)
        for cl in classes:
            r |= w.class_slots(cl)
        reached[k] = w.reach(r)
        print("  back end %-5s: %d roots (export + vtable slots of %s), reaches %d functions" % (
            k, len(r), ", ".join(classes), len(reached[k])))
    for dll in ("opengl32.dll", "d3d9.dll"):
        iu = w.import_units(dll)
        n_sites = sum(len(v) for v in iu.values())
        print("  %s: %d imports used, call sites in %d functions" % (dll, len(iu), len({u for v in iu.values() for u in v})))
        for k in backends:
            hit = {f for f, us in iu.items() if us & reached[k]}
            print("     reachable from %-5s back end: %d of %d imports%s" % (
                k, len(hit), len(iu), (": " + ", ".join(sorted(hit))) if hit and len(hit) <= 8 else ""))
    # class of each function that contains GL calls, by which back end reaches it
    gl_units = {u for v in w.import_units("opengl32.dll").values() for u in v}
    print("  functions with OpenGL calls: %d; reached from D3D11 back end: %d; from GL back end: %d; from neither: %d" % (
        len(gl_units), len(gl_units & reached["D3D11"]), len(gl_units & reached["GL"]),
        len(gl_units - reached["D3D11"] - reached["GL"])))
    d9_units = {u for v in w.import_units("d3d9.dll").values() for u in v}
    for u in sorted(d9_units):
        print("  d3d9 call site function 0x%x: D3D11=%s GL=%s" % (u, u in reached["D3D11"], u in reached["GL"]))


def txaa():
    w = Walker("GFSDK_TXAA.win64")
    ex = xref_lib.export_rvas(w.m.pe)
    print("== GFSDK_TXAA.win64.dll")
    roots = {w.real(v) for k, v in ex.items() if k.endswith("GL")}
    r = w.reach(roots)
    gl_units = {u for v in w.import_units("opengl32.dll").values() for u in v}
    print("  GL exports reach %d functions; functions with OpenGL calls: %d, reached from GL exports: %d" % (
        len(r), len(gl_units), len(gl_units & r)))
    dx = {w.real(v) for k, v in ex.items() if k.endswith("DX")}
    rd = w.reach(dx)
    print("  DX exports reach %d functions; of those with OpenGL calls: %d" % (len(rd), len(gl_units & rd)))


def main():
    ssao()
    txaa()


if __name__ == "__main__":
    main()
