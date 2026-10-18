#!/usr/bin/env python3
"""Per-module analysis context for the EA-SDK and anti-tamper scouts.

Wraps xref_lib: loads the cached cross-references of one game DLL and adds RTTI vtable labels,
per-function string references, forward/reverse call edges and a caller-climbing helper.
Prints addresses, names and counts only; never bytes of the game.
"""

import collections
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xref_lib  # noqa: E402

GAME = os.environ.get("TF2_GAME_DIR", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "game"))


def module_path(name):
    name = name[:-4] if name.lower().endswith(".dll") else name
    if name.lower() == "server":
        return os.path.join(GAME, "server.dll")
    return os.path.join(GAME, "bin", "x64_retail", name + ".dll")


def all_module_names():
    d = os.path.join(GAME, "bin", "x64_retail")
    return ["server"] + [f[:-4] for f in sorted(os.listdir(d)) if f.lower().endswith(".dll")]


class Ctx:
    def __init__(self, name):
        self.name = name
        self.mod, self.t = xref_lib.load_cached(module_path(name))
        self.g = xref_lib.Graph(self.mod, self.t)
        self.vts = xref_lib.vtables(self.mod)
        self.vs = xref_lib.vtable_slots(self.mod, self.vts)
        self.exports = {r: n for n, r in xref_lib.export_rvas(self.mod.pe).items()}
        self.fstr = collections.defaultdict(list)
        m = self.mod
        for addr, size, mn, tgt, fm in self.t["rip"]:
            if mn == "lea" and m.section_name(tgt) in (".rdata", ".data", "_RDATA"):
                s = m.any_string(tgt)
                if s:
                    f = m.unit_of(addr)
                    if f is not None and s not in self.fstr[f]:
                        self.fstr[f].append(s)

    # ---- naming ---------------------------------------------------------
    def va(self, rva):
        return self.mod.base + rva

    def fsize(self, r):
        m = self.mod
        return sum(e - b for b, e, u in m.entries if m.parent.get(b, b) == r)

    def label(self, r):
        out = []
        v = self.vs.get(r)
        if v:
            out.append("vt:" + ";".join("%s#%d" % (xref_lib.demangle_class(c), s) for c, s in v[:3]))
        if r in self.exports:
            out.append("export:" + self.exports[r])
        return " ".join(out)

    def callers(self, r):
        return self.g.callers.get(r, [])

    # ---- thunks ---------------------------------------------------------
    def import_thunks(self, dll=None, names=None):
        """{thunk_rva: (dll, name)} for every `jmp [IAT slot]` stub, optionally filtered."""
        slots = self.mod.iat_slots()
        out = {}
        for addr, size, mn, tgt, fm in self.t["rip"]:
            if mn == "jmp" and tgt in slots:
                d, n, k = slots[tgt]
                if (dll is None or d == dll) and (names is None or n in names):
                    out[addr] = (d, n)
        return out

    def import_call_sites(self, dll):
        """{name: [(site_rva, kind)]} for calls/tail-jumps to imports of `dll`, via the IAT slot
        directly (`call [slot]`) or through a `jmp [slot]` thunk."""
        slots = self.mod.iat_slots()
        thunks = self.import_thunks(dll)
        out = collections.defaultdict(list)
        for addr, size, mn, tgt, fm in self.t["rip"]:
            if tgt in slots and slots[tgt][0] == dll and mn in ("call", "jmp") and addr not in thunks:
                out[slots[tgt][1]].append((addr, "call [slot]" if mn == "call" else "jmp [slot]"))
        for ta, (d, n) in thunks.items():
            for site, mn, cf in self.callers(ta):
                out[n].append((site, "call thunk" if mn == "call" else "tail-jmp thunk"))
        return out

    # ---- graph walks ----------------------------------------------------
    def build_rev(self):
        """callee unit -> {caller unit: 'call'|'addr'}  (direct calls/tail jumps, and code addresses taken with lea)"""
        m = self.mod
        rev = collections.defaultdict(dict)
        for cf, callees in self.g.callees.items():
            for a in callees:
                u = m.unit_of(a)
                if u is not None and u != cf:
                    rev[u].setdefault(cf, "call")
        for tgt, lst in self.g.taken.items():
            u = m.unit_of(tgt)
            for site, cf in lst:
                if cf is not None and u is not None and u != cf:
                    rev[u].setdefault(cf, "addr")
        return rev

    def climb(self, rev, start, maxdepth=14):
        """BFS up the caller edges. Stops at labelled units (vtable slot / export) and caller-less units.
        Returns (seen, roots); seen[x] = (child, depth, edge_kind)."""
        seen = {start: (None, 0, None)}
        dq = collections.deque([start])
        roots = []
        while dq:
            x = dq.popleft()
            d = seen[x][1]
            ups = rev.get(x, {})
            lab = self.label(x)
            if x != start and (lab or not ups):
                roots.append(x)
                if lab:
                    continue
            if d >= maxdepth:
                continue
            for y, kind in ups.items():
                if y not in seen:
                    seen[y] = (x, d + 1, kind)
                    dq.append(y)
        return seen, roots

    @staticmethod
    def chain(seen, n):
        out = []
        while n is not None:
            out.append(n)
            n = seen[n][0]
        return out
