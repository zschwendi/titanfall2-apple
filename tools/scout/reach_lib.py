#!/usr/bin/env python3
"""Shared helpers of the unlifted-reach scout (finding 38): who can reach a function the
lifter skipped.

Read-only over the game DLLs.  Built on xref_lib (one cached linear decode per module: direct
branches, rip-relative operands, indirect calls), threads_lib (PE, imports, exports, pointers
in data) and cpu_core (single-instruction decoder).  Prints and returns addresses, mnemonics
of single instructions, names of imports and exports, and counts; never bytes or listings of
the game.  OriginSDK.dll and d3dcompiler_46.dll are never opened.

  BOOT                 the 20 boot-path modules (round 3 plan, milestone 1)
  load_report(m, dir)  the lifter's report of module m from a census directory
                       (tools/census/lift_census.sh OUT; the directory is TF2_CENSUS_DIR)
  skipped(report)      [(rva, first reason kind, first reason text, record)]
  Mod(name)            one module
  Mod.unit_of(rva)     enclosing function: the unwind table's, else the nearest start the
                       lifter's report knows, else the start of the int3-delimited run
  Mod.refs(targets)    {target: [(site, kind, enclosing function)]}; kind is call, jmp, jcc
                       (decoded), lea (address taken in code), mem (another rip-relative
                       operand that addresses it: a read or write of a variable), data (an
                       8-byte pointer in a data section; the third field is the section name)
  Mod.climb(starts)    breadth-first walk up the reference graph, to roots
  Mod.rip_refs(lo, hi) every instruction with a rip-relative operand landing in [lo, hi)
  load_cfg(m, dir), Mod.set_cfg, Mod.lifted_owner(site)
                       the instructions the lifter itself decoded, to tell a reference in
                       translated code from one the linear decode read out of data

The cache of the linear decode goes to SCOUT_CACHE (default: a directory in the system's
temporary directory); it holds addresses, mnemonics and operand strings of branch
instructions, and is never part of the repository.
"""

import bisect
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import cpu_core as C  # noqa: E402
import threads_lib as T  # noqa: E402
import xref_lib as X  # noqa: E402

BOOT = ["tier0", "vstdlib", "launcher", "rtech_game", "filesystem_stdio", "engine", "bink2w64",
        "inputsystem", "vphysics", "amd_ags_x64", "materialsystem_dx11", "datacache",
        "studiorender", "vguimatsurface", "vgui2", "localize", "mileswin64", "binkawin64",
        "client", "server"]
NEVER = {"originsdk", "d3dcompiler_46"}


def census_dir(arg=None):
    d = arg or os.environ.get("TF2_CENSUS_DIR")
    if not d:
        raise SystemExit("give the census directory (tools/census/lift_census.sh OUT) as an "
                         "argument or in TF2_CENSUS_DIR")
    return d


def load_report(module, cdir=None):
    if module.lower() in NEVER:
        raise SystemExit("%s is never read" % module)
    with open(os.path.join(census_dir(cdir), module, module + ".report.json")) as f:
        return json.load(f)


def load_cfg(module, cdir=None):
    """{function rva: sorted [instruction rva]} of the functions the lifter translated: the
    instructions its own decoding reached (<module>.cfg.json, written beside the report)."""
    with open(os.path.join(census_dir(cdir), module, module + ".cfg.json")) as f:
        raw = json.load(f)
    out = {}
    for name, v in raw.items():
        out[int(name.rsplit("_", 1)[1], 16)] = v["insns"]
    return out


def skipped(report):
    out = []
    for f in report["functions"]:
        if f.get("status") != "skipped":
            continue
        rs = f.get("reasons") or [{}]
        out.append((int(f["rva"], 16), rs[0].get("kind", "?"), rs[0].get("what", "?"), f))
    return out


class Mod(T.Module):
    def __init__(self, name, report=None):
        name = name[:-4] if name.lower().endswith(".dll") else name
        if name.lower() in NEVER:
            raise SystemExit("%s is never read" % name)
        super().__init__(name + ".dll")
        self.short = name
        self.dis = C.Dis(self.pe)
        self._load_funcs()
        self._exp = None
        self._x = None
        self._by_target = None
        self._rip_sorted = None
        self._starts = []
        self._owner = None
        if report is not None:
            self.add_starts(int(f["rva"], 16) for f in report["functions"])

    def set_cfg(self, cfg):
        """Give the module the lifter's decoded instructions (load_cfg).  After this,
        lifted_owner(site) says which translated function holds the instruction at `site`,
        or None when the lifter decoded no instruction there (data read as code by the
        linear decode, code of a skipped function, or code the lifter never found)."""
        self._owner = {}
        for f, insns in cfg.items():
            for a in insns:
                self._owner.setdefault(a, f)

    def lifted_owner(self, site):
        if self._owner is None:
            raise SystemExit("set_cfg first")
        return self._owner.get(site)

    # ---- names and extents ---------------------------------------------------

    def export_names(self):
        if self._exp is None:
            self._exp = {}
            for n, _o, r in self.exports():
                self._exp.setdefault(r, n)
        return self._exp

    def add_starts(self, rvas):
        """Function starts known from elsewhere (the lifter's report), for code outside .pdata."""
        self._starts = sorted(set(self._starts) | set(rvas))

    def func_of(self, rva):
        """Start of the unwind-described function that holds rva.  A fragment chained to a
        fragment is followed to the first one (threads_lib stops after one step)."""
        f = super().func_of(rva)
        for _ in range(8):
            if f is None:
                return None
            e = self.entry_of(f)
            if e is None or e[2] == f or e[0] != f:
                return f
            f = e[2]
        return f

    def unit_of(self, rva):
        f = self.func_of(rva)
        if f is not None:
            return f
        i = bisect.bisect_right(self._starts, rva) - 1
        if i >= 0:
            s = self._starts[i]
            j = bisect.bisect_right(self._fbegins, rva) - 1
            if (j < 0 or self._funcs[j][1] <= s) and rva - s < 0x4000:
                return s
        # the start of the int3-delimited run
        off = self.pe.rva_to_off(rva)
        if off is None:
            return None
        i = off
        lim = max(off - 0x400, 0)
        while i > lim and self.data[i - 1] != 0xCC:
            i -= 1
        return rva - (off - i)

    def label(self, f):
        if f is None:
            return "?"
        n = self.export_names().get(f)
        return "%s+0x%x%s" % (self.short, f, (" (export %s)" % n[:48]) if n else "")

    # ---- the cached linear decode -------------------------------------------

    def x(self):
        if self._x is None:
            _m, self._x = X.load_cached(self.path)
        return self._x

    def _index(self):
        if self._by_target is None:
            idx = {}
            for addr, mn, tgt in self.x()["calls"]:
                kind = mn if mn in ("call", "jmp") else "jcc"
                idx.setdefault(tgt, []).append((addr, kind))
            for addr, _size, mn, tgt, _fm in self.x()["rip"]:
                if mn in ("call", "jmp"):
                    continue          # through a slot: that is a read of the slot, kept as mem
                idx.setdefault(tgt, []).append((addr, "lea" if mn == "lea" else "mem"))
            self._by_target = idx
        return self._by_target

    def rip_refs(self, lo, hi):
        """[(site, mnemonic, target, first operand is the memory operand)] with lo <= target < hi."""
        if self._rip_sorted is None:
            self._rip_sorted = sorted(self.x()["rip"], key=lambda r: r[3])
            self._rip_keys = [r[3] for r in self._rip_sorted]
        i = bisect.bisect_left(self._rip_keys, lo)
        out = []
        while i < len(self._rip_sorted) and self._rip_keys[i] < hi:
            a, _s, mn, t, fm = self._rip_sorted[i]
            out.append((a, mn, t, fm))
            i += 1
        return sorted(out)

    def indirect(self, lo, hi):
        """Indirect calls and jumps at sites in [lo, hi): [(site, mnemonic, operand text)]."""
        return [(a, mn, ops) for a, mn, ops in self.x()["ind"] if lo <= a < hi]

    # ---- references -----------------------------------------------------------

    def refs(self, targets):
        targets = set(targets)
        idx = self._index()
        out = {t: [] for t in targets}
        for t in targets:
            for site, kind in idx.get(t, ()):
                out[t].append((site, kind, self.unit_of(site)))
        for t, lst in self.abs_refs(targets).items():
            for site in lst:
                out[t].append((site, "data", self.sec_name(site)))
        for t in out:
            out[t].sort(key=lambda r: r[0])
        return out

    def climb(self, starts, depth=6, stop=lambda f: False):
        """Walk up from the functions in `starts`.  Returns (edges, roots).  edges is
        {function: [(site, kind, referrer)]}.  roots maps a function to the reasons the walk
        could end there: 'export NAME'; 'data' (its address is stored in a data section, so a
        call through a pointer can reach it); 'lea' (its address is taken in code); 'none' (no
        static reference at all); 'depth' (the walk was cut)."""
        edges = {}
        roots = {}
        level = set(starts)
        seen = set()
        for d in range(depth + 1):
            level = {f for f in level if f not in seen and f is not None}
            if not level:
                break
            seen |= level
            r = self.refs(level)
            nxt = set()
            for f in level:
                edges[f] = r[f]
                why = []
                n = self.export_names().get(f)
                if n:
                    why.append("export " + n)
                kinds = {k for _s, k, _u in r[f]}
                for k in ("data", "lea"):
                    if k in kinds:
                        why.append(k)
                if not r[f]:
                    why.append("none")
                if why:
                    roots[f] = why
                if stop(f):
                    continue
                for _site, kind, u in r[f]:
                    if kind in ("call", "jmp", "jcc", "lea") and u is not None and u != f:
                        nxt.add(u)
            if d == depth:
                for f in nxt - seen:
                    roots.setdefault(f, []).append("depth")
            level = nxt
        return edges, roots


def hx(x):
    return "0x%x" % x


def fmt_refs(m, lst, limit=12):
    out = []
    for site, kind, u in lst[:limit]:
        out.append("%s@0x%x in %s" % (kind, site, ("0x%x" % u) if isinstance(u, int) else u))
    if len(lst) > limit:
        out.append("... %d more" % (len(lst) - limit))
    return ", ".join(out)
