#!/usr/bin/env python3
"""Pointer-flow analysis that types COM objects in one x64 module (issue #31).

The question this answers: for each `call [reg + disp]` that goes through a COM vtable, which
interface does the object belong to?  The answer comes from following pointers from where they
are created (D3D11CreateDevice, and every `Create*`, `Get*`, `QueryInterface` and `GetParent`
call whose out-parameter type is known from the SDK headers, see d3d11_vtables.py) to the call.

Model (an Andersen-style points-to analysis with a flow-sensitive pass inside each function):

  value    a small set of atoms.  Atoms:
             ('O', T)            a pointer to a COM object of interface T
             ('V', T)            the vtable pointer of such an object (result of `mov r, [obj]`)
             ('M', T, off)       a method pointer loaded from such a vtable
             ('B', base, delta)  a pointer into memory region `base` at byte offset `delta`
             ('R', rva)          a pointer into .rdata or .text (vtables, IIDs, strings)
             ('F', rva)          a code address
             ('I', value)        a small constant (only inside one function; used to read call arguments)
           base is ('G',) for module globals (delta = absolute RVA), ('S', fn) for the stack frame
           of fn (delta = offset from the entry value of rsp), ('N', site) for the result of the call
           at `site`, ('C', class) for a C++ class found through RTTI, ('E', base, delta) for "an
           element of the array at that address".  Bases can be merged ("aliased") when a vtable
           address is stored at offset 0 of an object: all objects with that vtable are one class.
  cells    memory of a region: (base, offset) -> set of atoms ever stored there.  Global and heap
           cells are flow-insensitive and shared by every function.  The function's own stack
           frame is flow-sensitive (strong updates).
  summary  param[(fn, i)] (atoms passed by callers), ret[fn] (atoms returned), kept per function
           and iterated to a fixed point over the whole call graph.

Two settings.  `strict=True` does not carry values across calls (no parameter or return flow, no
RTTI `this` seeding, no class merging): a type found that way depends only on the code of the
function that makes the call and on global cells.  `strict=False` is the full analysis.  Sites
typed in both settings are reported as certain; sites typed only in the full run as likely.

Limits (the report repeats these): calls through function pointers and virtual calls whose target
class is unknown do not carry arguments; objects handed over through queued commands or other
untyped memory are lost; indirect jumps that are not the MSVC switch idiom end a path.
"""

import collections
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import d3d11_decode as D  # noqa: E402
import d3d11_vtables as V  # noqa: E402
import xref_lib  # noqa: E402

RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI = range(8)
ARG_REGS = D.ARG_REGS
VOLATILE = D.VOLATILE
CAP = 14           # maximum atoms kept in one set
STACK_ARGS = 8     # arguments 0..7 are tracked (4 in registers, 4 on the stack)
GLOBAL = ("G",)

_OUT_RE = re.compile(r"^(?:const )?(\w+) \*\*$")


def _norm(atoms):
    if len(atoms) <= CAP:
        return frozenset(atoms)
    keep = sorted(atoms, key=lambda a: (a[0] != "O", a[0] != "V", repr(a)))[:CAP]
    return frozenset(keep)


_TYPED = ("O", "V", "M", "X")
POLY_LIMIT = 5   # a set that would hold more distinct interfaces than this is a polymorphic hub
POLY_COLLAPSES = [0]


def _merge(old, atoms):
    """old | atoms, capped at CAP atoms without ever dropping a typed atom for an untyped one,
    so repeated merging reaches a fixed point.  A set that would hold more than POLY_LIMIT
    different interface types collapses to the single atom ('X',): a function or cell that
    sees many unrelated COM types cannot be typed."""
    if atoms <= old:
        return old
    u = set(old | atoms)
    if ("X",) in u:
        u = {a for a in u if a[0] != "O"}
    elif sum(1 for a in u if a[0] == "O") > POLY_LIMIT:
        u = {a for a in u if a[0] != "O"}
        u.add(("X",))
        POLY_COLLAPSES[0] += 1
    if len(u) <= CAP:
        return frozenset(u)
    keep = set(u) & set(old)
    for a in sorted(u - keep, key=repr):
        if len(keep) < CAP:
            keep.add(a)
        elif a[0] in _TYPED:
            victims = sorted((x for x in keep if x[0] not in _TYPED), key=repr)
            if victims:
                keep.remove(victims[-1])
                keep.add(a)
    return frozenset(keep)


def _flowing(atoms):
    """Atoms that may cross a call boundary: object pointers, memory pointers and code addresses.
    Constants, vtable pointers and method pointers stay inside the function that loaded them."""
    return frozenset(a for a in atoms if a[0] in ("O", "B", "R", "F", "X"))


class Imports:
    """Import call targets: IAT slot -> (dll, name), and `jmp [slot]` thunks."""

    def __init__(self, mod, dec):
        self.slots = mod.iat_slots()
        self.thunks = {}
        for begin in dec.gap_funcs:
            f = dec.func(begin)
            for a, sz, mn, ops, k in f.insns:
                if mn == "jmp" and ops and ops[0][0] == "m" and ops[0][1] == -2 and ops[0][4] in self.slots:
                    self.thunks[a] = self.slots[ops[0][4]]
            dec.drop(begin)


class Prog:
    """A module prepared for analysis."""

    def __init__(self, path, quiet=False):
        t0 = time.time()
        self.mod = D.load_module(path)
        self.name = self.mod.name
        _, tables = xref_lib.load_cached(path)
        hints = set(t for a, mn, t in tables["calls"] if mn == "call")
        hints.update(tgt for a, size, mn, tgt, fm in tables["rip"] if mn == "lea" and self.mod.in_exec(tgt))
        self.vts = xref_lib.vtables(self.mod)
        self.vslots = xref_lib.vtable_slots(self.mod, self.vts)
        hints.update(self.vslots)
        hints.update(xref_lib.export_rvas(self.mod.pe).values())
        # code addresses stored in data (callback tables, vtables without RTTI)
        pe = self.mod.pe
        import struct as _st
        for sec in pe.sections:
            if pe.is_exec(sec) or sec["rsize"] == 0:
                continue
            raw = pe.data[sec["rptr"]:sec["rptr"] + min(sec["rsize"], sec["vsize"])]
            n = len(raw) // 8
            for v in _st.unpack_from("<%dQ" % n, raw, 0):
                r = v - self.mod.base
                if 0 < r < pe.size_of_image and r % 16 == 0 and self.mod.in_exec(r):
                    hints.add(r)
        # 16-byte aligned targets of jumps that come from a different unit (tail calls)
        for a, mn, t in tables["calls"]:
            if mn == "jmp" and t % 16 == 0 and self.mod.unit_of(a) != self.mod.unit_of(t):
                hints.add(t)
        self.dec = D.Decoder(self.mod, hints)
        self.imports = Imports(self.mod, self.dec)
        self.vt_class = {a: xref_lib.demangle_class(n) for a, n in self.vts.items()}
        self.ifaces = V.load()
        self.iid_names = V.iid_table(self.ifaces)
        self.outinfo = self._build_outinfo()
        self.data_secs = [s for s in self.mod.pe.sections if not self.mod.pe.is_exec(s) and (s["name"] in (".data",))]
        self.chkstk = self._find_chkstk()
        self.base = self.mod.base
        if not quiet:
            print("[prog] %s: %d functions, %d vtables, %d imports (%.1fs)" % (
                self.name, len(self.dec.begins), len(self.vts), len(self.imports.slots), time.time() - t0), file=sys.stderr)

    # ---- method effects from the header prototypes -------------------------------------------
    def _build_outinfo(self):
        """{(iface, slot): [(arg_index, kind, value)]}; kind 'type' (value = interface) or 'iid' (value = riid arg index)."""
        out = {}
        for n, it in self.ifaces.items():
            for m in it.methods:
                eff = []
                for p in m.params[1:]:
                    mm = _OUT_RE.match(p.type)
                    if mm and mm.group(1) in self.ifaces and mm.group(1) != "IUnknown":
                        eff.append((p.index, "type", mm.group(1)))
                    elif p.type.replace(" ", "") == "void**" and p.index >= 2 and m.params[p.index - 1].type.replace(" ", "") in ("REFIID", "REFIID"):
                        eff.append((p.index, "iid", p.index - 1))
                if eff:
                    out[(n, m.slot)] = eff
        return out

    def _find_chkstk(self):
        """Stack probe helpers (__chkstk): small functions that read the stack limit from the TEB
        (gs:[0x10]) and return with rax unchanged.  `mov eax, N; call chkstk; sub rsp, rax` is how
        MSVC allocates a frame of N bytes."""
        out = set()
        for b in self.dec.begins:
            f = self.dec.func(b)
            if len(f.insns) > 40 or len(f.insns) < 8:
                continue
            for a, sz, mn, ops, k in f.insns:
                if mn == "mov" and len(ops) == 2 and ops[1][0] == "m" and ops[1][6] and ops[1][4] == 0x10:
                    out.add(b)
                    break
        return out

    def is_data(self, rva):
        for s in self.data_secs:
            if s["va"] <= rva < s["va"] + s["vsize"]:
                return True
        return False

    def read_iid(self, rva):
        b = self.mod.read(rva, 16)
        if not b or len(b) < 16:
            return None
        return V.bytes_guid(b)


# import effects: name -> list of (arg index, ('type', T) | ('iid', riid arg index))
IMPORT_OUTS = {
    "D3D11CreateDevice": [(7, ("type", "ID3D11Device")), (9, ("type", "ID3D11DeviceContext"))],
    "D3DReflect": [(3, ("iid", 2))],
    "D3DGetBlobPart": [(4, ("type", "ID3D10Blob"))],
    "agsDriverExtensionsDX11_CreateTexture2D": [(3, ("type", "ID3D11Texture2D"))],
}


class State:
    __slots__ = ("regs", "stack", "rsp")

    def __init__(self):
        self.regs = [None] * 16
        self.stack = {}
        self.rsp = 0

    def copy(self):
        s = State()
        s.regs = list(self.regs)
        s.stack = dict(self.stack)
        s.rsp = self.rsp
        return s

    def key(self):
        return (tuple(self.regs), tuple(sorted(self.stack.items())), self.rsp)


def join_into(a, b):
    """Join state b into a (in place).  Returns True if a changed."""
    changed = False
    ar, br = a.regs, b.regs
    for i in range(16):
        x, y = ar[i], br[i]
        if y and y is not x:
            if not x:
                ar[i] = y
                changed = True
            elif not y <= x:
                nv = _merge(x, y)
                if nv != x:
                    ar[i] = nv
                    changed = True
    for k, v in b.stack.items():
        x = a.stack.get(k)
        if x is None:
            a.stack[k] = v
            changed = True
        elif not v <= x:
            nv = _merge(x, v)
            if nv != x:
                a.stack[k] = nv
                changed = True
    if a.rsp != b.rsp and a.rsp is not None:
        a.rsp = None
        changed = True
    return changed


class Solver:
    def __init__(self, prog, strict=False, seeds=None, quiet=False):
        self.p = prog
        self.strict = strict
        self.quiet = quiet
        self.seeds = seeds or {}               # fn -> {param index: set of atoms}
        self.cells = collections.defaultdict(dict)   # canon base -> {off: frozenset}
        self.alias = {}
        self.param = collections.defaultdict(frozenset)
        self.ret = collections.defaultdict(frozenset)
        self.readers = collections.defaultdict(set)  # (base, off) -> fns
        self.base_users = collections.defaultdict(set)
        self.callers = collections.defaultdict(set)
        self.dirty = set()
        self.changed = False
        self.store_sites = collections.defaultdict(lambda: collections.defaultdict(set))  # (base,off) -> T -> sites
        self.cell_untyped = collections.defaultdict(int)
        self.records = None
        self.overflow = 0
        self.unions = []
        # RTTI: function -> class
        self.this_class = {}
        if not strict:
            for fn, lst in prog.vslots.items():
                self.this_class[fn] = xref_lib.demangle_class(lst[0][0])
        self.begin_set = set(prog.dec.begins)
        self.iid_log = {}
        self.stats = collections.Counter()

    # ---- alias handling ---------------------------------------------------------------------
    def canon(self, b):
        al = self.alias
        while b in al:
            b = al[b]
        return b

    def union(self, b1, b2):
        b1, b2 = self.canon(b1), self.canon(b2)
        if b1 == b2:
            return
        # keep class bases as the representative
        if b2[0] == "C" and b1[0] != "C":
            b1, b2 = b2, b1
        self.alias[b2] = b1
        c2 = self.cells.pop(b2, None)
        if c2:
            c1 = self.cells[b1]
            for off, v in c2.items():
                old = c1.get(off)
                c1[off] = _merge(old, v) if old else v
        for fn in self.base_users.pop(b2, ()):
            self.dirty.add(fn)
            self.base_users[b1].add(fn)
        for fn in self.base_users.get(b1, ()):
            self.dirty.add(fn)
        self.changed = True
        self.unions.append((b1, b2))

    # ---- cell access ------------------------------------------------------------------------
    def cell_get(self, fn, base, off):
        base = self.canon(base)
        self.readers[(base, off)].add(fn)
        self.base_users[base].add(fn)
        return self.cells[base].get(off)

    def cell_add(self, fn, base, off, atoms, site=None):
        base = self.canon(base)
        self.base_users[base].add(fn)
        c = self.cells[base]
        old = c.get(off)
        if old is None:
            new = _norm(atoms)
        else:
            new = _merge(old, atoms)
            if new != old and len(old | atoms) > CAP:
                self.overflow += 1
        if new != old:
            c[off] = new
            self.changed = True
            for r in self.readers.get((base, off), ()):
                self.dirty.add(r)
            if base[0] == "S":
                self.dirty.add(base[1])
        if site is not None:
            for a in atoms:
                if a[0] == "O":
                    self.store_sites[(base, off)][a[1]].add(site)

    def add_param(self, f, i, atoms):
        atoms = _flowing(atoms)
        if not atoms:
            return
        key = (f, i)
        old = self.param[key]
        if atoms <= old:
            return
        new = _merge(old, atoms)
        if new != old:
            self.param[key] = new
            self.dirty.add(f)
            self.changed = True

    def add_ret(self, fn, atoms):
        atoms = frozenset(a for a in atoms if a[0] in ("O", "B", "R", "F", "X") and not (a[0] == "B" and a[1][0] == "S"))
        if not atoms:
            return
        old = self.ret[fn]
        if atoms <= old:
            return
        new = _merge(old, atoms)
        if new != old:
            self.ret[fn] = new
            for c in self.callers.get(fn, ()):
                self.dirty.add(c)
            self.changed = True

    # ---- the per-function analysis ------------------------------------------------------------
    def analyze(self, fn):
        p = self.p
        f = p.dec.func(fn)
        if not f.insns:
            return
        ent = State()
        strict = self.strict
        if not strict:
            for i in range(4):
                a = self.param.get((fn, i))
                if a:
                    ent.regs[ARG_REGS[i]] = a
            for i in range(4, STACK_ARGS):
                a = self.param.get((fn, i))
                if a:
                    ent.stack[0x28 + 8 * (i - 4)] = a
            cls = self.this_class.get(fn)
            if cls:
                cur = ent.regs[RCX] or frozenset()
                ent.regs[RCX] = _norm(cur | {("B", ("C", cls), 0)})
                self.add_param(fn, 0, {("B", ("C", cls), 0)})
        # seeds (used for modules whose entry points get their arguments from outside)
        sd = self.seeds.get(fn)
        if sd:
            for i, atoms in sd.items():
                if i < 4:
                    ent.regs[ARG_REGS[i]] = _norm((ent.regs[ARG_REGS[i]] or frozenset()) | atoms)
                else:
                    ent.stack[0x28 + 8 * (i - 4)] = frozenset(atoms)
        nb = len(f.blocks)
        instate = [None] * nb
        instate[0] = ent
        visits = [0] * nb
        work = collections.deque([0])
        inwork = {0}
        rec = self.records
        local_sites = {} if rec is not None else None
        while work:
            bi = work.popleft()
            inwork.discard(bi)
            visits[bi] += 1
            if visits[bi] > 10:
                continue
            st = instate[bi].copy()
            s, e = f.blocks[bi]
            self.run_block(fn, f, s, e, st, local_sites if visits[bi] >= 1 else None)
            for sb in f.succ[bi]:
                if instate[sb] is None:
                    instate[sb] = st.copy()
                    if sb not in inwork:
                        work.append(sb)
                        inwork.add(sb)
                elif join_into(instate[sb], st):
                    if sb not in inwork:
                        work.append(sb)
                        inwork.add(sb)
        # unreached blocks (indirect jumps we could not resolve): analyse with an empty state
        if rec is not None:
            for bi in range(nb):
                if instate[bi] is None:
                    st = State()
                    s, e = f.blocks[bi]
                    st.rsp = None
                    self.run_block(fn, f, s, e, st, local_sites)
                    self.stats["unreached_blocks"] += 1
            rec[fn] = list(local_sites.values())

    # instruction semantics ----------------------------------------------------------------------
    @staticmethod
    def fix_op(op, regs):
        """[base + index*1 + disp] where only the index register holds a pointer: swap them, so
        `shl rdx, 5; mov rdx, [rdx + r11]` with r11 = &table is read as an element of the table."""
        if op[2] >= 0 and op[3] == 1 and op[1] >= 0 and op[1] != RSP:
            b, i = regs[op[1]], regs[op[2]]
            if (not b or not any(a[0] in ("B", "R", "O") for a in b)) and i and any(a[0] in ("B", "R", "O") for a in i):
                return ("m", op[2], op[1], 1, op[4], op[5], op[6])
        return op

    def addr_atoms(self, fn, op, regs, rsp):
        """Atoms for the effective address of a memory operand (no dereference)."""
        op = self.fix_op(op, regs)
        _, base, index, scale, disp, size, seg = op
        p = self.p
        if seg:
            return None
        if base == -2:
            a = disp
            if a == 0:
                return frozenset({("B", GLOBAL, 0)})   # the image base, used to index global arrays
            if a < 0x1000:
                return None
            return frozenset({("B", GLOBAL, a)}) if p.is_data(a) else frozenset({("R", a)})
        if base < 0:
            return None
        if base == RSP:
            if rsp is None:
                return None
            if index >= 0:
                return frozenset({("B", ("E", ("S", fn), rsp), disp)})
            return frozenset({("B", ("S", fn), rsp + disp)})
        atoms = regs[base]
        if not atoms:
            return None
        out = set()
        for a in atoms:
            k = a[0]
            if k == "B":
                if index >= 0:
                    out.add(("B", ("E", a[1], a[2]), disp))
                else:
                    out.add(("B", a[1], a[2] + disp))
            elif k == "R":
                if index < 0:
                    out.add(("R", a[1] + disp))
            elif k == "O" and index < 0 and disp == 0:
                out.add(a)
        return frozenset(out) if out else None

    def load(self, fn, op, regs, stack, rsp):
        """Atoms of an 8-byte load from a memory operand."""
        if op[5] != 8 and op[5] != 0:
            return None
        op = self.fix_op(op, regs)
        _, base, index, scale, disp, size, seg = op
        if seg or index >= 0 and base == -2:
            return None
        p = self.p
        if base == -2:
            if disp < 0x1000:
                return None
            if p.is_data(disp):
                return self.cell_get(fn, GLOBAL, disp)
            return None
        if base < 0:
            return None
        if base == RSP:
            if rsp is None or index >= 0:
                return None
            return stack.get(rsp + disp)
        atoms = regs[base]
        if not atoms:
            return None
        out = set()
        for a in atoms:
            k = a[0]
            if k == "O":
                if disp == 0 and index < 0:
                    out.add(("V", a[1]))
            elif k == "X":
                if disp == 0 and index < 0:
                    out.add(("V", "*"))
            elif k == "V":
                if index < 0 and disp % 8 == 0:
                    out.add(("M", a[1], disp))
            elif k == "B":
                b = a[1]
                if index >= 0:
                    b, off = ("E", a[1], a[2]), disp
                else:
                    off = a[2] + disp
                if b[0] == "S" and b[1] == fn:
                    v = stack.get(off)
                else:
                    v = self.cell_get(fn, b, off)
                if v:
                    out.update(v)
            elif k == "R" and index < 0:
                # a load from .rdata: a vtable slot holds a code pointer
                tgt = p.mod.u64(a[1] + disp)
                if tgt is not None and p.mod.in_exec(tgt - p.base):
                    out.add(("F", tgt - p.base))
        return _norm(out) if out else None

    def store(self, fn, op, val, regs, stack, rsp, site):
        """Store atoms (may be None) to a memory operand."""
        op = self.fix_op(op, regs)
        _, base, index, scale, disp, size, seg = op
        if seg:
            return
        p = self.p
        keep = frozenset(a for a in val if a[0] in ("O", "B", "R", "F", "X")) if (val and size == 8) else None
        if keep is not None and not keep:
            keep = None
        if base == -2:
            if disp >= 0x1000 and p.is_data(disp) and keep:
                self.cell_add(fn, GLOBAL, disp, keep, site)
            elif disp >= 0x1000 and p.is_data(disp) and size == 8 and val is None:
                self.cell_untyped[(GLOBAL, disp)] += 0
            return
        if base < 0:
            return
        consts = frozenset(a for a in val if a[0] == "I") if (val and size in (4, 8)) else None
        if base == RSP:
            if rsp is None or index >= 0:
                return
            off = rsp + disp
            self._stack_write(stack, off, size, keep or consts)
            return
        atoms = regs[base]
        if not atoms:
            return
        single = len(atoms) == 1
        for a in atoms:
            if a[0] != "B":
                continue
            b = a[1]
            if index >= 0:
                b, off = ("E", a[1], a[2]), disp
            else:
                off = a[2] + disp
            if b[0] == "S" and b[1] == fn:
                self._stack_write(stack, off, size, keep or consts, strong=single)
            elif keep:
                self.cell_add(fn, b, off, keep, site)
                if off == 0 and not self.strict:
                    self._vtable_store(b, keep)

    def _stack_write(self, stack, off, size, keep, strong=True):
        if strong:
            for o in range(off - 7, off + max(size, 1)):
                if o in stack:
                    del stack[o]
            if keep and size in (4, 8):
                stack[off] = keep
        elif keep and size in (4, 8):
            old = stack.get(off)
            stack[off] = _norm(old | keep) if old else keep

    def _vtable_store(self, base, atoms):
        for a in sorted(atoms, key=repr):
            if a[0] == "R":
                cls = self.p.vt_class.get(a[1])
                if cls:
                    b = self.canon(base)
                    if b[0] in ("N", "E", "C"):
                        self.union(b, ("C", cls))
                    return

    def set_reg(self, regs, i, v):
        regs[i] = v if v else None

    def run_block(self, fn, f, s, e, st, sites):
        regs = st.regs
        stack = st.stack
        p = self.p
        insns = f.insns
        for k in range(s, e):
            a, sz, mn, ops, kills = insns[k]
            n = len(ops)
            if mn == "mov" or mn == "movabs":
                d, sr = ops[0], ops[1]
                if d[0] == "r":
                    if d[2] == 64:
                        if sr[0] == "r" and sr[2] == 64:
                            if sr[1] == RSP:
                                regs[d[1]] = frozenset({("B", ("S", fn), st.rsp)}) if st.rsp is not None else None
                            elif d[1] == RSP:
                                # mov rsp, rbp: restore the stack pointer from a saved frame address
                                x = regs[sr[1]]
                                if x and len(x) == 1 and next(iter(x))[0] == "B" and next(iter(x))[1] == ("S", fn):
                                    st.rsp = next(iter(x))[2]
                                else:
                                    st.rsp = None
                            else:
                                regs[d[1]] = regs[sr[1]]
                        elif sr[0] == "m":
                            regs[d[1]] = self.load(fn, sr, regs, stack, st.rsp)
                        elif sr[0] == "i":
                            regs[d[1]] = frozenset({("I", sr[1])})
                        else:
                            regs[d[1]] = None
                    else:
                        if sr[0] == "i" and d[2] == 32:
                            regs[d[1]] = frozenset({("I", sr[1] & 0xFFFFFFFF)})
                        else:
                            regs[d[1]] = None
                        if d[1] == RSP:
                            st.rsp = None
                elif d[0] == "m":
                    if sr[0] == "r" and sr[2] in (32, 64):
                        self.store(fn, d, regs[sr[1]], regs, stack, st.rsp, a)
                    elif sr[0] == "i":
                        self.store(fn, d, frozenset({("I", sr[1])}) if d[5] <= 8 else None, regs, stack, st.rsp, a)
                    else:
                        self.store(fn, d, None, regs, stack, st.rsp, a)
                if d[0] == "r" and d[1] == RSP and d[2] == 64 and not (sr[0] == "r" and sr[2] == 64):
                    st.rsp = None   # a load or constant into rsp
                continue
            if mn == "lea":
                d, sr = ops[0], ops[1]
                if d[0] == "r" and d[2] == 64 and sr[0] == "m":
                    regs[d[1]] = self.addr_atoms(fn, sr, regs, st.rsp)
                    if d[1] == RSP:
                        st.rsp = None
                elif d[0] == "r":
                    if d[2] == 32 and sr[0] == "m" and sr[1] >= 0 and sr[2] < 0 and sr[1] != RSP:
                        c = regs[sr[1]]
                        regs[d[1]] = None
                        if c and len(c) == 1:
                            x = next(iter(c))
                            if x[0] == "I":
                                regs[d[1]] = frozenset({("I", (x[1] + sr[4]) & 0xFFFFFFFF)})
                    else:
                        regs[d[1]] = None
                continue
            if mn == "call":
                self.do_call(fn, a, sz, ops, regs, stack, st, sites)
                continue
            if mn == "jmp":
                if ops and ops[0][0] == "i":
                    t = ops[0][1]
                    if not f.contains(t):
                        self.do_call(fn, a, sz, ops, regs, stack, st, sites, tail=True)
                elif ops and ops[0][0] == "m":
                    self.do_call(fn, a, sz, ops, regs, stack, st, sites, tail=True)
                elif ops and ops[0][0] == "r" and a not in f.tables:
                    self.do_call(fn, a, sz, ops, regs, stack, st, sites, tail=True)
                continue
            if mn == "ret":
                x = regs[RAX]
                if x and not self.strict:
                    self.add_ret(fn, x)
                continue
            if mn in ("add", "sub"):
                d, sr = ops[0], ops[1]
                if d[0] == "r" and d[2] == 64:
                    if d[1] == RSP:
                        if sr[0] == "i" and st.rsp is not None:
                            st.rsp += -sr[1] if mn == "sub" else sr[1]
                        elif sr[0] == "r" and st.rsp is not None and regs[sr[1]] and len(regs[sr[1]]) == 1 \
                                and next(iter(regs[sr[1]]))[0] == "I":
                            v = next(iter(regs[sr[1]]))[1]
                            st.rsp += -v if mn == "sub" else v
                        else:
                            st.rsp = None
                        continue
                    if sr[0] == "i":
                        x = regs[d[1]]
                        if x:
                            delta = sr[1] if mn == "add" else -sr[1]
                            out = set()
                            for at in x:
                                if at[0] == "B":
                                    out.add(("B", at[1], at[2] + delta))
                                elif at[0] == "R":
                                    out.add(("R", at[1] + delta))
                                elif at[0] == "I":
                                    out.add(("I", at[1] + delta))
                            regs[d[1]] = frozenset(out) if out else None
                    elif sr[0] == "r" and mn == "add":
                        # pointer plus unknown integer: an element of the array
                        x = regs[d[1]]
                        y = regs[sr[1]]
                        src = x if x and any(at[0] == "B" for at in x) else (y if y and any(at[0] == "B" for at in y) else None)
                        if src:
                            regs[d[1]] = frozenset(("B", ("E", at[1], at[2]), 0) for at in src if at[0] == "B")
                        else:
                            regs[d[1]] = None
                    else:
                        regs[d[1]] = None
                elif d[0] == "r":
                    regs[d[1]] = None
                elif d[0] == "m":
                    self.store(fn, d, None, regs, stack, st.rsp, a)
                continue
            if mn == "push":
                if st.rsp is not None:
                    st.rsp -= 8
                    o = ops[0]
                    v = None
                    if o[0] == "r" and o[2] == 64:
                        v = regs[o[1]]
                    if v:
                        stack[st.rsp] = v
                    else:
                        stack.pop(st.rsp, None)
                continue
            if mn == "pop":
                if st.rsp is not None:
                    o = ops[0]
                    if o[0] == "r" and o[2] == 64:
                        regs[o[1]] = stack.get(st.rsp)
                        if o[1] == RSP:
                            st.rsp = None
                            continue
                    st.rsp += 8
                else:
                    o = ops[0]
                    if o[0] == "r":
                        regs[o[1]] = None
                continue
            if mn == "xor":
                d, sr = ops[0], ops[1]
                if d[0] == "r":
                    if sr[0] == "r" and sr[1] == d[1] and sr[2] == d[2]:
                        regs[d[1]] = frozenset({("I", 0)})
                    else:
                        regs[d[1]] = None
                elif d[0] == "m":
                    self.store(fn, d, None, regs, stack, st.rsp, a)
                continue
            if mn.startswith("cmov"):
                d, sr = ops[0], ops[1]
                if d[0] == "r" and d[2] == 64:
                    y = regs[sr[1]] if sr[0] == "r" else (self.load(fn, sr, regs, stack, st.rsp) if sr[0] == "m" else None)
                    x = regs[d[1]]
                    if y:
                        regs[d[1]] = _norm((x or frozenset()) | y)
                elif d[0] == "r":
                    regs[d[1]] = None
                continue
            if mn == "xchg":
                if n == 2 and ops[0][0] == "r" and ops[1][0] == "r" and ops[0][2] == 64 and ops[1][2] == 64:
                    regs[ops[0][1]], regs[ops[1][1]] = regs[ops[1][1]], regs[ops[0][1]]
                else:
                    for kx in kills:
                        regs[kx] = None
                continue
            if mn == "leave":
                x = regs[RBP]
                if x and len(x) == 1:
                    at = next(iter(x))
                    if at[0] == "B" and at[1] == ("S", fn):
                        st.rsp = at[2] + 8
                        regs[RBP] = stack.get(at[2])
                        continue
                st.rsp = None
                regs[RBP] = None
                continue
            if mn in ("cmp", "test", "nop", "int3", "bt", "ud2", "pause", "fnop", "lfence", "mfence", "sfence", "prefetcht0"):
                continue
            # generic instruction: everything it writes is unknown afterwards
            for kx in kills:
                regs[kx] = None
                if kx == RSP:
                    st.rsp = None
            if n and ops[0][0] == "m" and mn not in ("jmp",):
                m = ops[0]
                # a memory destination: drop an own-frame slot it overlaps
                if m[1] == RSP and not m[6] and st.rsp is not None and m[2] < 0:
                    self._stack_write(stack, st.rsp + m[4], m[5] or 8, None)
            if mn in ("and",) and n == 2 and ops[0][0] == "r" and ops[0][1] == RSP:
                st.rsp = None

    # ---- calls ------------------------------------------------------------------------------
    def arg_atoms(self, regs, stack, st, i):
        if i < 4:
            return regs[ARG_REGS[i]]
        if st.rsp is None:
            return None
        return stack.get(st.rsp + 0x20 + 8 * (i - 4))

    def do_call(self, fn, site, size, ops, regs, stack, st, sites, tail=False):
        p = self.p
        tgt = None            # internal target function (rva)
        imp = None            # (dll, name)
        com = None            # (T, slot) candidates
        disp = None
        o = ops[0] if ops else None
        unresolved_vcall = False
        if o is None:
            return
        if o[0] == "i":
            t = o[1]
            if t in p.imports.thunks:
                imp = p.imports.thunks[t]
            else:
                tgt = t if t in self.begin_set else p.dec.func_of(t)
        elif o[0] == "m":
            if o[1] == -2:
                if o[4] in p.imports.slots:
                    imp = p.imports.slots[o[4]][:2]
                else:
                    pass
            elif o[1] >= 0 and o[2] < 0 and not o[6]:
                disp = o[4]
                va = regs[o[1]]
                if va:
                    cands = []
                    for at in sorted(va, key=repr):
                        if at[0] == "V" and disp % 8 == 0:
                            cands.append((at[1], disp // 8))
                        elif at[0] == "R" and not self.strict:
                            slot_t = p.mod.u64(at[1] + disp)
                            if slot_t is not None and p.mod.in_exec(slot_t - p.base):
                                tgt = slot_t - p.base
                    if cands:
                        com = cands
                else:
                    unresolved_vcall = True
        elif o[0] == "r":
            va = regs[o[1]]
            if va:
                cands = []
                for at in sorted(va, key=repr):
                    if at[0] == "M":
                        cands.append((at[1], at[2] // 8))
                    elif at[0] == "F" and not self.strict:
                        tgt = at[1]
                if cands:
                    com = cands
        # ---- effects
        result = None
        if tgt is not None and tgt in p.chkstk:
            regs[10] = None
            regs[11] = None
            return
        if com:
            labels = []
            for T, slot in com:
                if T == "*":
                    labels.append(("*", "?", slot))
                    continue
                it = p.ifaces.get(T)
                if it is None or slot >= len(it.methods):
                    continue
                labels.append((T, it.methods[slot].name, slot))
                for ai, kind, val in p.outinfo.get((T, slot), ()):
                    self.apply_out(fn, site, ai, kind, val, regs, stack, st)
            if sites is not None:
                self.put_site(sites, self.make_site(fn, site, "com", labels, disp, regs, stack, st, ops, com))
            self.stats["com_calls"] += 1
        elif imp:
            name = imp[1]
            for ai, (kind, val) in IMPORT_OUTS.get(name, ()):
                self.apply_out(fn, site, ai, kind, val, regs, stack, st)
            if sites is not None and (imp[0] in ("d3d11.dll", "d3dcompiler_46.dll", "amd_ags_x64.dll", "d3d9.dll", "dxgi.dll")):
                self.put_site(sites, self.make_site(fn, site, "import", [imp], None, regs, stack, st, ops, None))
        elif tgt is not None and not self.strict:
            self.stats["direct_calls"] += 1
            self.callers[tgt].add(fn)
            for i in range(STACK_ARGS):
                at = self.arg_atoms(regs, stack, st, i)
                if at:
                    self.add_param(tgt, i, at)
            r = self.ret.get(tgt)
            if r:
                result = r
            if tgt not in (fn,):
                pass
        elif unresolved_vcall and sites is not None:
            self.put_site(sites, self.make_site(fn, site, "unresolved", [], disp, regs, stack, st, ops, None))
        elif sites is not None and o[0] == "m" and o[1] >= 0 and o[2] < 0 and disp is not None:
            self.put_site(sites, self.make_site(fn, site, "unresolved", [], disp, regs, stack, st, ops, None))
        # stack objects written through pointers handed to a callee
        if tgt is not None and not self.strict:
            self.pull_frame(fn, stack)
        # result and clobbers
        for r_ in VOLATILE:
            regs[r_] = None
        if result:
            regs[RAX] = result
        if not self.strict and imp is None and com is None:
            fresh = frozenset({("B", ("N", site), 0)})
            regs[RAX] = _norm((regs[RAX] or frozenset()) | fresh)
        elif not self.strict and (imp is not None):
            regs[RAX] = frozenset({("B", ("N", site), 0)})
        if tail:
            x = regs[RAX]
            if x and not self.strict:
                self.add_ret(fn, x)

    def pull_frame(self, fn, stack):
        cs = self.cells.get(("S", fn))
        if cs:
            self.base_users[("S", fn)].add(fn)
            for off, v in cs.items():
                old = stack.get(off)
                stack[off] = _norm(old | v) if old else v

    def apply_out(self, fn, site, ai, kind, val, regs, stack, st):
        p = self.p
        dest = self.arg_atoms(regs, stack, st, ai)
        if not dest:
            return
        if kind == "type":
            T = val
        else:
            T = None
            for at in sorted(self.arg_atoms(regs, stack, st, val) or (), key=repr):
                if at[0] == "R":
                    g = p.read_iid(at[1])
                    if g:
                        T = p.iid_names.get(g)
                        self.iid_log[site] = [g, T]
            if T is None:
                return
        obj = frozenset({("O", T)})
        single = len(dest) == 1
        for at in dest:
            if at[0] != "B":
                continue
            b = at[1]
            if b[0] == "S" and b[1] == fn:
                if single:
                    stack[at[2]] = obj
                else:
                    old = stack.get(at[2])
                    stack[at[2]] = _norm(old | obj) if old else obj
            else:
                self.cell_add(fn, b, at[2], obj, site)

    @staticmethod
    def put_site(sites, rec):
        sites[rec["addr"]] = rec

    def make_site(self, fn, site, kind, labels, disp, regs, stack, st, ops, com):
        args = []
        for i in range(4):
            at = regs[ARG_REGS[i]]
            args.append(sorted(self.render(a) for a in at) if at else [])
        rec = {"addr": site, "fn": fn, "kind": kind, "labels": labels, "disp": disp, "args": args}
        if st.rsp is None:
            rec["rsp_unknown"] = True
        sargs = {}
        for i in range(4, 10):
            at = self.arg_atoms(regs, stack, st, i)
            if at:
                sargs[i] = sorted(self.render(a) for a in at)
        if sargs:
            rec["sargs"] = sargs
        # constants stored in the stack object that an argument points at (descriptor structs)
        pointee = {}
        for i in range(4):
            at = regs[ARG_REGS[i]]
            if at and len(at) == 1:
                x = next(iter(at))
                if x[0] == "B" and x[1] == ("S", fn):
                    vals = {}
                    for off in range(x[2], x[2] + 0x40):
                        v = stack.get(off)
                        if v:
                            c = sorted(a[1] for a in v if a[0] == "I")
                            if c:
                                vals[off - x[2]] = c
                    if vals:
                        pointee[i] = vals
        if pointee:
            rec["pointee"] = pointee
        iid = self.iid_log.get(site)
        if iid:
            rec["iid"] = iid
        return rec

    @staticmethod
    def render(a):
        if a[0] == "O":
            return "O:" + a[1]
        if a[0] == "I":
            return "I:%d" % a[1]
        if a[0] == "B":
            return "B:" + repr(a[1]) + "+%d" % a[2]
        return a[0] + ":" + ",".join(str(x) for x in a[1:])

    # ---- driver -----------------------------------------------------------------------------
    def run(self, max_rounds=60):
        order = list(self.p.dec.begins)
        self.dirty = set(order)
        rnd = 0
        t0 = time.time()
        while self.dirty and rnd < max_rounds:
            rnd += 1
            todo = sorted(self.dirty)
            self.dirty = set()
            for fn in todo:
                self.analyze(fn)
            if not self.quiet:
                print("[%s] round %d: %d functions, %d dirty after, %d unions (%.0fs)" % (
                    "strict" if self.strict else "full", rnd, len(todo), len(self.dirty), len(self.unions), time.time() - t0),
                    file=sys.stderr)
        return rnd

    def record_pass(self):
        self.records = {}
        for fn in self.p.dec.begins:
            self.analyze(fn)
        recs = self.records
        self.records = None
        return recs
