#!/usr/bin/env python3
"""Shared code of the queue-race survey (finding 36): where the game does a compare-and-swap
by hand under a lock and reads the swapped location twice.

Read-only over the game DLLs; uses cpu_core (decoder, function extents from the unwind
tables) and threads_lib (import slots, exports, reference scans). Nothing here prints or
stores instruction listings: results are addresses, mnemonics of single instructions,
offsets and counts.

  survey_modules()      the DLLs in scope (OriginSDK.dll and d3dcompiler_46.dll are never opened)
  Ctx(name)             one module: lock import slots, lock wrappers, functions with a lock call
  Ctx.scan_function(f)  symbolic walk of every critical region of function f; returns hits

The walk. For each call that takes a lock, the code is followed path by path from the
start of the straight run of instructions that ends in that call, to a few instructions
past the matching release. Along a path every register and stack slot carries a value name
(root, offset): `mov` copies names, `lea`/`add imm` shift the offset, a load from non-stack
memory makes a new name and is recorded as an event. Two memory operands are "the same
location" when base name, offset, index name, scale and size agree; a pointer loaded twice
from one place with no store or call in between counts as one base. That shows equality
only along the path walked; it never shows two locations to be different.
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import cpu_core as C  # noqa: E402
from threads_lib import Module, GAME  # noqa: E402,F401

EXCLUDED = ("originsdk.dll", "d3dcompiler_46.dll")

ENTER = {"EnterCriticalSection": "enter", "TryEnterCriticalSection": "tryenter",
         "AcquireSRWLockExclusive": "enter", "AcquireSRWLockShared": "enter",
         "TryAcquireSRWLockExclusive": "tryenter", "TryAcquireSRWLockShared": "tryenter"}
LEAVE = {"LeaveCriticalSection": "leave", "ReleaseSRWLockExclusive": "leave",
         "ReleaseSRWLockShared": "leave"}
LOCK_NAMES = dict(ENTER)
LOCK_NAMES.update(LEAVE)

VOLATILE = ("a", "c", "d", "r8", "r9", "r10", "r11")
FLAG_KEEP = ("mov", "lea", "push", "pop", "nop", "not", "xchg", "cmov", "set", "bswap", "prefetch",
             "cdq", "cqo", "cdqe", "cwde", "j", "call", "ret", "lfence", "mfence", "sfence", "pause")

MAX_BACK = 48        # instructions walked back from the lock call to find where a path starts
MAX_REGION = 200     # instructions on one path inside one region
TAIL = 28            # instructions followed after the release (to see what the first read is used for)
MAX_STEPS = 5000     # instructions walked per lock call, all paths together


def survey_modules():
    """File names in scope: game/bin/x64_retail/*.dll without the two excluded, plus server.dll."""
    d = os.path.join(GAME, "bin", "x64_retail")
    names = sorted(f for f in os.listdir(d) if f.lower().endswith(".dll") and f.lower() not in EXCLUDED)
    return names + ["server.dll"]


def short(name):
    return name[:-4] if name.lower().endswith(".dll") else name


def root_functions(pe):
    """(func_of, groups) with split fragments joined to the function they were cut from through
    every level of chained unwind info (threads_lib and cpu_core follow one level only).
    func_of(rva) -> start of the whole function or None; groups: start -> its ranges."""
    entries, _ = C.function_table(pe)
    par = {b: p for b, e, p in entries}

    def root(b):
        n = 0
        while par.get(b, b) != b and n < 16:
            b, n = par[b], n + 1
        return b
    groups = {}
    for b, e, p in entries:
        groups.setdefault(root(b), []).append((b, e))
    for g in groups.values():
        g.sort()
    index = C.RangeIndex(entries)

    def func_of(rva):
        r = index.find(rva)
        return root(r[0]) if r else None
    return func_of, groups


class Ctx:
    def __init__(self, name):
        if name.lower() in EXCLUDED:
            raise SystemExit("%s is out of scope" % name)
        self.name = name
        self.m = Module(name)
        self.pe = self.m.pe
        self.dis = C.Dis(self.pe)
        self.func_of, self.groups = root_functions(self.pe)
        self.slots = {}          # slot rva -> kind
        self.slot_names = {}
        for slot, (dll, n) in self.m.import_slots().items():
            if n in LOCK_NAMES:
                self.slots[slot] = LOCK_NAMES[n]
                self.slot_names[slot] = n
        self.wrappers = {}       # function rva -> kind (a small function that only forwards to a lock import)
        self._find_wrappers()
        self._fn_cache = {}

    # ---- which functions take a lock --------------------------------------

    def _find_wrappers(self):
        cand = {}
        self.ref_kinds = {}      # function -> {(how the slot is used, kind of lock import)}
        for site, kind, slot in self.m.iat_refs(self.slots):
            f = self.func_of(site)
            if f is not None:
                cand.setdefault(f, set()).add(self.slots[slot])
                self.ref_kinds.setdefault(f, set()).add((kind, self.slots[slot]))
        self.lock_fns = set(cand)
        for f, kinds in cand.items():
            if len(kinds) != 1:
                continue
            rng = self.groups.get(f)
            if not rng or len(rng) != 1 or rng[0][1] - rng[0][0] > 48:
                continue
            ins = self.dis.linear(rng[0][0], rng[0][1])
            calls = [i for i in ins if i.mn in ("call", "jmp")]
            if len(ins) <= 10 and len(calls) == 1 and not any(i.is_jcc for i in ins):
                self.wrappers[f] = next(iter(kinds))
        if self.wrappers:
            for site, kind, tgt in self.m.rel_refs(self.wrappers):
                f = self.func_of(site)
                if f is not None and f not in self.wrappers:
                    self.lock_fns.add(f)

    def lock_functions(self):
        return sorted(f for f in self.lock_fns if f not in self.wrappers)

    # ---- one function -------------------------------------------------------

    def function(self, f):
        """(insns, succ, pred, reg_kind) of function f; reg_kind maps a register family that only
        ever holds one lock import's address in this function to that import's kind."""
        if f in self._fn_cache:
            return self._fn_cache[f]
        ranges = self.groups.get(f) or []
        insns, succ, unresolved = C.descend(self.dis, ranges, [f])
        pred = C.preds(succ)
        writers = {}
        for a, i in insns.items():
            if i.mn == "pop" or i.is_call:
                continue
            if i.mn == "mov" and len(i.ops) == 2 and i.ops[0][0] == "r" and i.ops[1][0] == "m" \
                    and i.ops[1][1] == "sp":
                continue  # restore of a saved register
            for r in i.wr:
                kind = None
                if i.mn == "mov" and len(i.ops) == 2 and i.ops[1][0] == "m":
                    t = C.rip_target(i, i.ops[1])
                    kind = self.slots.get(t)
                writers.setdefault(r, set()).add(kind)
        reg_kind = {r: next(iter(k)) for r, k in writers.items() if len(k) == 1 and None not in k}
        res = (insns, succ, pred, reg_kind)
        self._fn_cache = {f: res}
        return res

    def call_kind(self, ins, reg_kind, st=None):
        """'enter' | 'tryenter' | 'leave' | 'other' for a call (or a tail jump)."""
        if not ins.ops:
            return "other"
        o = ins.ops[0]
        if o[0] == "m":
            t = C.rip_target(ins, o)
            return self.slots.get(t, "other")
        if o[0] == "i":
            return self.wrappers.get(ins.target, "other")
        if o[0] == "r":
            if st is not None:
                v = st.regs.get(o[1])
                if v is not None and isinstance(v[0], tuple) and v[0][0] == "imp":
                    return v[0][1]
            return reg_kind.get(o[1], "other")
        return "other"

    def enter_sites(self, f):
        insns, succ, pred, reg_kind = self.function(f)
        return sorted(a for a, i in insns.items()
                      if i.is_call and self.call_kind(i, reg_kind) in ("enter", "tryenter"))

    def scan_function(self, f):
        """Hits and statistics of every critical region of f."""
        insns, succ, pred, reg_kind = self.function(f)
        hits, singles, trunc = {}, {}, 0
        sites = self.enter_sites(f)
        for e in sites:
            w = Walk(self, f, e, insns, succ, pred, reg_kind)
            w.run()
            trunc += 1 if w.truncated else 0
            for k, h in w.hits.items():
                if k in hits:
                    hits[k]["fate"] = sorted(set(map(tuple, hits[k]["fate"])) | set(map(tuple, h["fate"])))
                else:
                    hits[k] = h
            singles.update(w.singles)
        for h in hits.values():
            classify(h)
        return {"fn": f, "enters": len(sites), "truncated": trunc,
                "hits": sorted(hits.values(), key=lambda h: h["store"]),
                "single_read_cas": sorted(singles)}


# ---- path state ---------------------------------------------------------------

class State:
    __slots__ = ("regs", "stk", "flags", "n", "memo", "canon")

    def copy(self):
        s = State()
        s.regs = dict(self.regs)
        s.stk = dict(self.stk)
        s.flags = self.flags
        s.n = self.n
        s.memo = dict(self.memo)
        s.canon = dict(self.canon)
        return s


class Walk:
    """All paths from the straight run before one lock call to a little past its release."""

    def __init__(self, ctx, f, enter, insns, succ, pred, reg_kind):
        self.ctx, self.f, self.enter = ctx, f, enter
        self.insns, self.succ, self.pred, self.reg_kind = insns, succ, pred, reg_kind
        self.fresh = 0
        self.steps = 0
        self.truncated = False
        self.hits = {}
        self.singles = {}

    def new(self):
        self.fresh += 1
        return (self.fresh, 0)

    def leader(self):
        a, n = self.enter, 0
        while n < MAX_BACK:
            ps = self.pred.get(a, [])
            if len(ps) != 1:
                break
            p = ps[0]
            pi = self.insns[p]
            if p + pi.size != a:
                break
            if pi.is_call and self.ctx.call_kind(pi, self.reg_kind) != "other":
                break
            a, n = p, n + 1
        return a

    def reg(self, st, fam):
        v = st.regs.get(fam)
        if v is None:
            v = st.regs[fam] = self.new()
        return v

    def key(self, st, ins, op):
        """Name of the location a memory operand addresses: (base root, offset, index, scale)."""
        if op[6] == "rip":
            return (("g", ins.rva + ins.size + op[4]), 0, None, 0)
        if op[5] in ("gs", "fs"):
            return (("seg", op[5]), op[4], None, 0)
        if op[1]:
            b = self.reg(st, op[1])
            root, off = st.canon.get(b[0], b[0]), b[1] + op[4]
        else:
            root, off = ("abs",), op[4]
        idx = self.reg(st, op[2]) if op[2] else None
        return (root, off, idx, op[3] if op[2] else 0)

    @staticmethod
    def on_stack(key):
        return key[0] == "SP" and key[2] is None

    def load(self, st, ins, op, ev):
        k = self.key(st, ins, op)
        if isinstance(k[0], tuple) and k[0][0] == "g" and k[0][1] in self.ctx.slots:
            return (("imp", self.ctx.slots[k[0][1]]), 0)
        if self.on_stack(k):
            if op[7] == 8:
                v = st.stk.get(k[1])
                if v is None:
                    v = st.stk[k[1]] = self.new()
                return v
            return self.new()
        v = self.new()
        ev.append(("load", ins.rva, k, op[7], v, ins.mn))
        # A pointer read again from the same place with nothing written in between is taken to be
        # the same pointer when it is used as an address (the two values keep different names).
        if op[7] == 8:
            old = st.memo.get(k)
            if old is None:
                st.memo[k] = v
            else:
                st.canon[v[0]] = st.canon.get(old[0], old[0])
        return v

    def step(self, st, ins, ev):
        """Apply one non-branch instruction to the state, appending events."""
        mn, ops = ins.mn, ins.ops
        if mn == "mov" and len(ops) == 2:
            d, s = ops
            if s[0] == "r":
                v = self.reg(st, s[1]) if s[3] >= 4 else self.new()
            elif s[0] == "i":
                v = (("k", C.in_mask(s[1], 8)), 0)
            elif s[0] == "m":
                v = self.load(st, ins, s, ev)
            else:
                v = self.new()
            if d[0] == "r":
                st.regs[d[1]] = v if d[3] >= 4 else self.new()
            elif d[0] == "m":
                k = self.key(st, ins, d)
                if self.on_stack(k):
                    if d[7] == 8:
                        st.stk[k[1]] = v
                    else:
                        st.stk.pop(k[1], None)
                else:
                    ev.append(("store", ins.rva, k, d[7], v))
                    st.memo.clear()
            return
        if mn == "lea" and len(ops) == 2 and ops[0][0] == "r" and ops[1][0] == "m":
            k = self.key(st, ins, ops[1])
            if k[2] is None and not (isinstance(k[0], tuple) and k[0][0] in ("seg", "abs")):
                st.regs[ops[0][1]] = (k[0], k[1]) if ops[0][3] == 8 else self.new()
            else:
                st.regs[ops[0][1]] = self.new()
            return
        if mn in ("add", "sub") and len(ops) == 2 and ops[0][0] == "r" and ops[1][0] == "i" and ops[0][3] == 8:
            v = self.reg(st, ops[0][1])
            d = ops[1][1] if mn == "add" else -ops[1][1]
            st.regs[ops[0][1]] = (v[0], v[1] + d)
            st.flags = None
            return
        if mn == "push":
            sp = self.reg(st, "sp")
            st.regs["sp"] = (sp[0], sp[1] - 8)
            if ops and ops[0][0] == "r" and sp[0] == "SP":
                st.stk[sp[1] - 8] = self.reg(st, ops[0][1])
            return
        if mn == "pop":
            sp = self.reg(st, "sp")
            if ops and ops[0][0] == "r":
                v = st.stk.get(sp[1]) if sp[0] == "SP" else None
                st.regs[ops[0][1]] = v if v is not None else self.new()
            st.regs["sp"] = (sp[0], sp[1] + 8)
            return
        if mn in ("cmp", "test") and len(ops) == 2:
            desc = []
            for o in ops:
                if o[0] == "r":
                    desc.append(self.reg(st, o[1]) if o[3] >= 4 else self.new())
                elif o[0] == "i":
                    desc.append((("k", C.in_mask(o[1], 8)), 0))
                elif o[0] == "m":
                    desc.append(self.load(st, ins, o, ev))
                else:
                    desc.append(self.new())
            if mn == "test":
                desc = [desc[0], (("k", 0), 0)] if desc[0] == desc[1] else [self.new(), self.new()]
            st.flags = (ins.rva, desc[0], desc[1])
            ev.append(("cmp", ins.rva, desc[0], desc[1]))
            return
        if mn == "xor" and len(ops) == 2 and ops[0][0] == "r" and ops[1][0] == "r" and ops[0][2] == ops[1][2]:
            st.regs[ops[0][1]] = (("k", 0), 0)
            st.flags = None
            return
        # everything else: memory operands become events, written registers get new names
        for n, o in enumerate(ops):
            if o[0] != "m" or mn in ("nop", "lea") or mn.startswith("prefetch"):
                continue
            k = self.key(st, ins, o)
            if self.on_stack(k):
                if n == 0 and mn not in C.NO_DEST_WRITE:
                    st.stk.pop(k[1], None)
                continue
            if n == 0 and mn not in C.NO_DEST_WRITE and not mn.startswith(("cmp", "test", "ucomi", "comi")):
                ev.append(("rmw", ins.rva, k, o[7], mn))
                st.memo.clear()
            else:
                ev.append(("load", ins.rva, k, o[7], None, mn))
        for r in ins.wr:
            if r != "sp":
                st.regs[r] = self.new()
        if not mn.startswith(FLAG_KEEP):
            st.flags = None

    def run(self):
        start = self.leader()
        st = State()
        st.regs = {"sp": ("SP", 0)}
        st.stk = {}
        st.flags = None
        st.n = 0
        st.memo = {}
        st.canon = {}
        # work items: (rva, state, events, depth, in-region count, tail count, on-path set)
        work = [(start, st, [], 0, 0, 0, frozenset())]
        while work:
            a, st, ev, depth, nreg, ntail, seen = work.pop()
            ev = list(ev)
            seen = set(seen)
            opened = any(e[0] == "call" and e[1] == self.enter for e in ev)
            while True:
                ins = self.insns.get(a)
                if ins is None or a in seen:
                    break
                seen.add(a)
                self.steps += 1
                if self.steps > MAX_STEPS:
                    self.truncated = True
                    work = []
                    break
                if opened and depth == 0:
                    ntail += 1
                    if ntail > TAIL:
                        break
                elif opened:
                    nreg += 1
                    if nreg > MAX_REGION:
                        self.truncated = True
                        break
                if ins.is_ret or ins.terminal:
                    ev.append(("ret", a, st.regs.get("a")))
                    break
                if ins.is_jmp:
                    if ins.target is None:  # a tail jump through an import or a register, or a switch
                        kind = self.ctx.call_kind(ins, self.reg_kind, st)
                    else:
                        if ins.target in self.insns:
                            a = ins.target
                            continue
                        kind = self.ctx.wrappers.get(ins.target, "other")
                    if kind == "leave":
                        ev.append(("call", a, "leave", None))
                    elif ins.target is None and opened and depth > 0 and self.succ.get(a):
                        self.truncated = True  # a switch inside a region is not followed
                    ev.append(("ret", a, None if kind == "leave" else st.regs.get("a")))
                    break
                if ins.is_call:
                    kind = self.ctx.call_kind(ins, self.reg_kind, st)
                    if kind in ("enter", "tryenter"):
                        if a != self.enter and depth == 0:
                            break  # another region: it has its own walk
                        depth += 1
                        opened = opened or a == self.enter
                    elif kind == "leave":
                        depth = max(0, depth - 1)
                    ev.append(("call", a, kind, st.regs.get("c")))
                    for r in VOLATILE:
                        st.regs[r] = self.new()
                    st.flags = None
                    st.memo.clear()
                    a += ins.size
                    continue
                if ins.is_jcc and ins.target is not None:
                    nxt = a + ins.size
                    fl = st.flags
                    if ins.target in self.insns:
                        ev2 = ev + [("br", a, fl, nxt, ins.mn)]
                        work.append((ins.target, st.copy(), ev2, depth, nreg, ntail, frozenset(seen)))
                    ev.append(("br", a, fl, ins.target, ins.mn))
                    a = nxt
                    continue
                self.step(st, ins, ev)
                a += ins.size
            if any(e[0] == "call" and e[1] == self.enter for e in ev):
                self.analyse(ev)

    # ---- the shape ----------------------------------------------------------

    def straight(self, a, n=10):
        out = set()
        while n and a in self.insns:
            i = self.insns[a]
            out.add(a)
            if i.is_jcc or i.is_jmp or i.is_ret or i.is_call:
                break
            a += i.size
            n -= 1
        return out

    def analyse(self, ev):
        depth, start = 0, None
        region = [None] * len(ev)
        for i, e in enumerate(ev):
            if e[0] == "call":
                if e[2] in ("enter", "tryenter"):
                    depth += 1
                    if depth == 1:
                        start = i
                elif e[2] == "leave":
                    depth = max(0, depth - 1)
            region[i] = start if depth > 0 else None
        for si, s in enumerate(ev):
            if s[0] != "store" or region[si] is None:
                continue
            skey, ssize = s[2], s[3]
            j = si - 1
            while j >= 0 and ev[j][0] in ("store", "load", "cmp"):
                j -= 1
            if j < 0 or ev[j][0] != "br" or ev[j][2] is None:
                continue
            br = ev[j]
            if s[1] in self.straight(br[3]):
                continue  # reached on both sides of the branch: not conditional on it
            fl = br[2]
            lock_ev = ev[region[si]]
            # which earlier load fed the compare?
            l2 = None
            for i in range(j - 1, region[si], -1):
                e = ev[i]
                if e[0] == "load" and e[2] == skey and e[3] == ssize and e[4] is not None \
                        and e[4] in (fl[1], fl[2]):
                    l2 = i
                    break
            if l2 is None:
                continue
            comperand = fl[2] if ev[l2][4] == fl[1] else fl[1]
            # another load of the same location in the same region, nothing written to it in between
            others = []
            for i in range(region[si] + 1, si):
                e = ev[i]
                if i != l2 and e[0] == "load" and e[2] == skey and e[3] == ssize:
                    lo, hi = min(i, l2), max(i, l2)
                    if any(x[0] == "call" or (x[0] in ("store", "rmw") and x[2] == skey) for x in ev[lo:hi]):
                        continue
                    others.append(i)
            if not others:
                self.singles[s[1]] = True
                continue
            for l1 in others:
                e1 = ev[l1]
                fate = []
                if e1[4] is None:
                    fate.append(("arith", e1[1], False))
                else:
                    for i in range(l1 + 1, len(ev)):
                        x = ev[i]
                        if x[0] == "cmp" and x[1] != fl[0] and e1[4] in (x[2], x[3]):
                            other = x[3] if x[2] == e1[4] else x[2]
                            fate.append(("cmp", x[1], other == comperand))
                        elif x[0] == "ret" and x[2] == e1[4]:
                            fate.append(("ret", x[1], False))
                        elif x[0] == "store" and x[4] == e1[4]:
                            fate.append(("stored", x[1], False))
                lo, hi = min(l1, l2), max(l1, l2)
                k = (s[1], e1[1], ev[l2][1])
                h = {"fn": self.f, "enter": lock_ev[1], "read1": e1[1], "read2": ev[l2][1],
                     "cmp": fl[0], "jcc": br[1], "store": s[1], "size": ssize,
                     "first_is_other": l1 < l2,
                     "branches_between": sum(1 for x in ev[lo:hi] if x[0] == "br"),
                     "branches_to_jcc": sum(1 for x in ev[lo:j] if x[0] == "br"),
                     "fate": fate, "addr": describe(skey, lock_ev[3]),
                     "comperand": describe_val(comperand, lock_ev[3])}
                if k in self.hits:
                    self.hits[k]["fate"] = sorted(set(self.hits[k]["fate"]) | set(fate))
                else:
                    self.hits[k] = h


def describe(key, lockval):
    """Where a location is, in words: relative to the lock, through a loaded pointer, a global."""
    root = key[0]
    if isinstance(root, tuple) and root[0] == "g":
        return "global 0x%x" % root[1]
    idx = " indexed" if key[2] is not None else ""
    if lockval is not None and root == lockval[0]:
        return "lock%+#x%s" % (key[1] - lockval[1], idx)
    return "ptr%+#x%s" % (key[1], idx)


def describe_val(v, lockval):
    if v is None:
        return "?"
    if isinstance(v[0], tuple) and v[0][0] == "k":
        return "const 0x%x" % v[0][1]
    if lockval is not None and v[0] == lockval[0]:
        return "lock%+#x" % (v[1] - lockval[1])
    return "value"


def classify(h):
    """A: the second read alone decides the store and the first read's value is compared with
    the same comperand elsewhere, or returned (compare-and-swap that reports the old value).
    S: two reads and a conditional store, but not that use of the first read."""
    fate = h["fate"]
    reports = any(f[0] == "ret" or (f[0] == "cmp" and f[2]) for f in fate)
    if h["first_is_other"] and h["branches_to_jcc"] == 0 and reports:
        h["class"] = "A"
    else:
        h["class"] = "S"
    return h
