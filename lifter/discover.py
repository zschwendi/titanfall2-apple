"""Function discovery and per-function control flow (docs/DESIGN.md sections 2 and 3).

Discovery roots: unwind-table functions (chained fragments merged into their root), exports,
the entry point, relocated pointers into executable sections that are outside every known
function, and then the closure over direct call targets, tail-jump targets and the addresses
that code takes with a RIP-relative lea (callbacks and thunks: issue 51).

Control flow is recursive descent from the entry.  For a function described by the unwind
table, "inside" means inside its ranges.  For a leaf function (no unwind entry) "inside"
means: executable, not another function's entry, and not inside any unwind-described range.
"""

import bisect
import struct

import capstone
from capstone import x86_const as X

from .translate import GPR, CC, NEAR_BRANCHES, legacy_prefixes

JCC = {"j" + c for c in CC} | {"jrcxz", "jecxz"}
TRAPS = ("int3", "ud2", "hlt")
VOLATILE = (0, 1, 2, 8, 9, 10, 11)      # rax rcx rdx r8-r11: not preserved by a callee (Microsoft x64)
# Instructions that compiled user-mode code does not contain and that bytes which are not code
# often decode to: seeing one in the body of a candidate that only a lea vouches for means
# the lea took the address of data.
ENTRY = -1      # among the writers of a register that reach a point: the function was entered with it
CALLEE_SAVED = (3, 5, 6, 7, 12, 13, 14, 15)     # rbx rbp rsi rdi r12-r15: a callee hands them back (Microsoft x64)
SAVED_VECTORS = frozenset(getattr(X, "X86_REG_%s%d" % (k, n)) for k in ("XMM", "YMM", "ZMM") for n in range(6, 16))
STACK_PATH_CAP = 16384      # instructions on the paths to a jump that _stack_at_entry follows at most
OPEN_TABLE_CAP = 256        # entries read at most from a jump table that nothing bounds (_jump_table, 'unbounded')
# REGISTER WRITES THE DECODER DOES NOT REPORT.  _writes asks capstone (regs_access) which
# registers an instruction writes, and capstone 5.0.7 leaves some out (finding 7 of
# docs/reviews/night-2026-10-11-code-review.md; measured for every instruction named in the
# brief of unit fix-review-night: docs/handoff/fix-review-night.md has the table).  What an
# instruction really writes is the Intel manual's description, from memory; the self-test
# (tests/run_selftests.py, test_register_writes) decodes a sample of each row and holds both
# that the decoder still leaves the write out and that _writes reports it.
UNREPORTED_WRITES = {
    "cmpxchg": (0,),            # rax (al, ax, eax): loaded from the destination when the compare fails
    "xlatb": (0,),              # al
    "enter": (4, 5),            # rsp, rbp
}
# Instructions after which this file knows no register's value: the kernel or a monitor ran.
UNKNOWN_WRITES = {"syscall", "sysenter", "sysexit", "sysret", "int", "int1", "into", "vmcall", "vmmcall", "vmfunc"}
# The instructions the rule 'cmp-reg' lets stand between the constant and the compare
# (_constant_at_compare): the ones whose register writes are their written operands and
# nothing else, and for which the self-test checks the decoder's report, sample by sample,
# against a list written by hand.  The rule's claim is "the last write to the bound register
# is the mov of a constant"; an instruction that is not here may write a register without
# the decoder saying so (capstone reports no write at all for 'rdsspq rax', measured), so
# the rule refuses it instead of believing the report.
SURE_WRITES = ({"mov", "movzx", "movsx", "movsxd", "lea", "add", "sub", "and", "or", "xor", "cmp", "test", "inc", "dec",
                "neg", "not", "shl", "shr", "sar", "nop"}
               | {"cmov" + c for c in CC} | {"set" + c for c in CC} | {"j" + c for c in CC})
NOT_USER_CODE = {"in", "out", "insb", "insw", "insd", "outsb", "outsw", "outsd", "hlt", "cli", "sti",
                 "iret", "iretd", "iretq", "retf", "rdmsr", "wrmsr", "lcall", "ljmp"}


class Func:
    __slots__ = ("entry", "ranges", "origin")

    def __init__(self, entry, ranges, origin):
        self.entry = entry
        self.ranges = ranges      # [(begin, end)] from the unwind table, or None for a leaf
        self.origin = origin


class CFG:
    def __init__(self):
        self.insns = {}       # rva -> CsInsn
        self.flow = {}        # rva -> verdict for control transfers (tuple) or None
        self.fall = {}        # rva -> ("next"|"tail"|"trap"|"end", rva) for fallthrough
        self.pred = {}        # rva -> straight-line predecessor in the same decode run
        self.labels = set()
        self.calls = set()
        self.tails = set()
        self.problems = []    # [(kind, rva, text)]
        self.tables = []      # recovered jump tables
        self.leas = {}        # executable rva taken by 'lea r64, [rip + x]' -> the first lea that takes it
        self.pending = set()  # 'jmp reg' that the run ending in it does not explain
        self.late = {}        # the ones of those that the whole function explains -> (verdict, tail targets)
        self.callee_writes = {}   # direct call target -> the general registers that call can write, or None
        self.table_tails = {}     # jump through a table of function pointers -> what shows it is a tail call
                                  # (_tail_through_table)

    def extents(self):
        """Merged [begin, end) ranges of the decoded instructions."""
        out = []
        for a in sorted(self.insns):
            e = a + self.insns[a].size
            if out and out[-1][1] >= a:
                out[-1][1] = max(out[-1][1], e)
            else:
                out.append([a, e])
        return out


class Program:
    def __init__(self, image):
        self.image = image
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        self.md.detail = True
        pf, self.n_fragments = image.pdata_functions()
        self.n_pdata_roots = len(pf)
        self.n_pdata_entries = len(image.pdata)
        iv = sorted((b, e, root) for root, rs in pf.items() for (b, e) in rs)
        self._iv = iv
        self._iv_starts = [b for b, _, _ in iv]
        self.funcs = {root: Func(root, rs, "pdata") for root, rs in pf.items()}
        self.rejected = []    # (rva, origin) roots that were not usable
        # Addresses in executable sections taken by a RIP-relative lea (discover): how many,
        # how many were functions already, how many lie inside an unwind-described function
        # (the address of a label, never an entry), how many became functions of origin
        # 'lea', and the ones that do not decode as a function start: [(rva, lea rva, why)].
        self.lea = {"targets": 0, "known": 0, "inside": 0, "roots": 0, "not_functions": 0}
        self.lea_rejected = []
        # Leaf entry -> addresses that are labels of that function although a relocated
        # pointer made each a function of its own: the slots of a table of absolute code
        # addresses that the function jumps through (_abs_jump_table).
        self.absorbed = {}
        self._reloc_sites = None

    # ---- lookup ---------------------------------------------------------------

    def container(self, rva):
        """Root entry of the unwind-described function whose ranges contain rva, or None."""
        k = bisect.bisect_right(self._iv_starts, rva) - 1
        if k >= 0:
            b, e, root = self._iv[k]
            if b <= rva < e:
                return root
        return None

    def in_function(self, fn, rva):
        if fn.ranges is not None:
            for b, e in fn.ranges:
                if b <= rva < e:
                    return True
            return False
        if rva == fn.entry or rva in self.absorbed.get(fn.entry, ()):
            return True
        if rva in self.funcs or self.container(rva) is not None:
            return False
        return self.image.is_exec(rva)

    def add_entry(self, rva, origin):
        if rva in self.funcs:
            return False
        if not (0 <= rva < self.image.size) or not self.image.is_exec(rva):
            self.rejected.append((rva, origin))
            return False
        root = self.container(rva)
        if root is not None:
            # an entry into the middle of an unwind-described function: same body, other entry
            self.funcs[rva] = Func(rva, self.funcs[root].ranges, origin + "-into")
        else:
            self.funcs[rva] = Func(rva, None, origin)
        return True

    def decode(self, rva):
        if not self.image.is_exec(rva):
            return None
        return next(self.md.disasm(bytes(self.image.data[rva:rva + 15]), rva, 1), None)

    # ---- discovery --------------------------------------------------------------

    def discover(self):
        im = self.image
        work = list(self.funcs)
        for rva in im.extra_entries:
            if self.add_entry(rva, "given"):
                work.append(rva)
        if im.entry_rva and self.add_entry(im.entry_rva, "entry"):
            work.append(im.entry_rva)
        for rva in im.exports:
            if self.add_entry(rva, "export"):
                work.append(rva)
        for rva in sorted(im.code_pointer_targets()):
            if self.container(rva) is None and self.add_entry(rva, "reloc"):
                work.append(rva)
        taken, judged = {}, set()
        while work:
            while work:
                fn = self.funcs[work.pop()]
                cfg = self.build_cfg(fn)
                for t in sorted(cfg.calls):
                    if self.add_entry(t, "call"):
                        work.append(t)
                for t in sorted(cfg.tails):
                    if self.add_entry(t, "tail"):
                        work.append(t)
                for t, a in cfg.leas.items():
                    taken.setdefault(t, a)
            # Addresses that code takes with a RIP-relative lea, once everything reachable
            # another way is known (so an address that is also called keeps that origin).  A
            # leaf comparator handed to a sort, or the thunk that stands for a pointer to a
            # virtual member function, has no unwind entry, no export, no direct caller and no
            # relocation: the lea is the only thing that names it (issue 51).
            for t in sorted(set(taken) - judged):
                judged.add(t)
                self.lea["targets"] += 1
                if t in self.funcs:
                    self.lea["known"] += 1
                elif self.container(t) is not None:
                    self.lea["inside"] += 1          # same rule as a relocated pointer
                else:
                    why = self._not_a_function_start(t)
                    if why is None and self.add_entry(t, "lea"):
                        self.lea["roots"] += 1
                        work.append(t)
                    else:
                        self.lea["not_functions"] += 1
                        self.lea_rejected.append((t, taken[t], why))

    def _not_a_function_start(self, rva):
        """Why the code at rva, which only a lea vouches for, cannot be the start of a
        function, or None if it can.  A lea may as well take the address of data that sits
        in an executable section, so the candidate is decoded the way a leaf function would
        be, from rva to every end it reaches, and refused if it starts in padding, if any
        byte on the way does not decode, or if it contains an instruction that user-mode
        code does not."""
        first = self.decode(rva)
        if first is None:
            return "does not decode"
        if first.insn_name() in TRAPS:
            return "starts with '%s' (padding)" % first.mnemonic
        cfg = self.build_cfg(Func(rva, None, "lea"))
        for kind, a, text in cfg.problems:
            if kind == "decode":
                return "%s at 0x%x" % (text, a)
        for t in sorted(cfg.labels):
            if t not in cfg.insns:
                return "branch target 0x%x not decoded" % t
        for a in sorted(cfg.insns):
            ins = cfg.insns[a]
            if ins.insn_name() in NOT_USER_CODE or ins.group(capstone.CS_GRP_PRIVILEGE):
                return "'%s' at 0x%x" % (ins.mnemonic, a)
        return None

    # ---- control flow -------------------------------------------------------------

    def build_cfg(self, fn, single=False):
        """Recursive descent.  With single=True the 'function' is the one instruction at
        fn.entry followed by a synthetic end (instruction test)."""
        while True:
            # Recovering a table of absolute addresses in a leaf can turn addresses that were
            # taken for other functions into labels of this one (self.absorbed); what was
            # decoded before that treated them as the function's edge, so start again.
            n = len(self.absorbed.get(fn.entry, ()))
            cfg = self._build_cfg(fn, single)
            if len(self.absorbed.get(fn.entry, ())) == n:
                return cfg

    def _build_cfg(self, fn, single):
        cfg = CFG()
        work = [fn.entry]
        while True:
            self._descend(fn, cfg, work, single)
            # A 'jmp reg' whose register is not set in the run that ends in it is judged with
            # everything decoded so far; a verdict with targets inside the function gives more
            # to decode, and that can show another way into a jump judged before, so all of
            # them are judged again at the end.
            if single or not self._judge_pending(fn, cfg, work):
                break
        # Decoding is over.  A jump that is still unexplained and goes through a pointer loaded
        # from a table that cannot be a compiler's switch is a tail call where the stack is shown
        # to be at its entry level (_tail_through_table).  The verdict adds nothing to decode and
        # no edge, so it is judged once, on the function as it is emitted.
        into = None
        for a in sorted(cfg.flow):
            if cfg.flow[a] == ("ijmp_unresolved",) and not single:
                into = into or self._edges_in(cfg)
                proof = self._tail_through_table(fn, cfg, into, cfg.insns[a])
                if proof is not None:
                    cfg.flow[a] = ("ijmp_tail",)
                    cfg.pending.discard(a)
                    cfg.table_tails[a] = proof
        into = None
        for a in sorted(cfg.late):
            into = into or self._edges_in(cfg)
            if self._late_jump(fn, cfg, into, cfg.insns[a]) != cfg.late[a]:
                cfg.problems.append(("indirect-jump", a, "jump through a register that can be set another way"))
        for t in cfg.tables:
            if not self._table_still_holds(fn, cfg, t):
                cfg.problems.append(("indirect-jump", t["jmp"], "jump table whose set-up can be reached another way"))
            elif t["bound"] in ("cmp-reg", "unbounded"):
                why = self._reg_bound_table_refused(cfg, t)
                if why is not None:
                    cfg.problems.append(("indirect-jump", t["jmp"], why))
        return cfg

    def _descend(self, fn, cfg, work, single):
        im = self.image
        while work:
            a = work.pop()
            prev = None
            while a not in cfg.insns:
                ins = self.decode(a)
                if ins is None:
                    cfg.problems.append(("decode", a, "cannot decode"))
                    break
                cfg.insns[a] = ins
                if prev is not None:
                    cfg.pred[a] = prev
                nxt = a + ins.size
                name = ins.insn_name()
                ops = ins.operands
                flow = None
                stop = False
                if name == "lea" and not single:
                    t = self._rip_lea(ins)
                    if t is not None and 0 <= t < im.size and im.is_exec(t):
                        cfg.leas.setdefault(t, a)
                if name in NEAR_BRANCHES and 0x66 in legacy_prefixes(bytes(ins.bytes)):
                    # Not translated (the translator reports it), and where it goes depends
                    # on the CPU vendor: do not follow it.
                    stop = True
                elif name in ("ret", "retf", "iretq", "iret", "iretd"):
                    flow = ("ret",)
                    stop = True
                elif name in TRAPS or (name == "int" and ops and ops[0].imm == 0x29):
                    flow = ("trap",)         # int 0x29 is __fastfail: it never returns
                    stop = True
                elif name == "jmp":
                    stop = True
                    if ops and ops[0].type == X.X86_OP_IMM:
                        t = ops[0].imm
                        if single:
                            flow = ("end",) if t == nxt else ("tail", t)
                            if t != nxt:
                                cfg.tails.add(t)
                        elif self.in_function(fn, t):
                            cfg.labels.add(t)
                            work.append(t)
                            flow = ("jmp", t)
                        else:
                            cfg.tails.add(t)
                            flow = ("tail", t)
                    else:
                        flow = self._indirect_jmp(fn, cfg, work, ins, single)
                        if (flow == ("ijmp_unresolved",) and ops[0].type == X.X86_OP_REG
                                and GPR.get(ops[0].reg, (0,))[0] == 64):
                            cfg.pending.add(a)
                elif name in JCC or name in ("loop", "loope", "loopne"):
                    t = ops[0].imm
                    if single:
                        if t == nxt:
                            flow = ("jcc_end",)
                        else:
                            flow = ("jcc", t, False)
                            cfg.tails.add(t)
                    elif self.in_function(fn, t):
                        cfg.labels.add(t)
                        work.append(t)
                        flow = ("jcc", t, True)
                    else:
                        cfg.tails.add(t)
                        flow = ("jcc", t, False)
                elif name == "call":
                    if ops and ops[0].type == X.X86_OP_IMM:
                        cfg.calls.add(ops[0].imm)
                        flow = ("call", ops[0].imm)
                    else:
                        flow = ("icall",)
                cfg.flow[a] = flow
                if stop:
                    break
                if single:
                    cfg.fall[a] = ("end", nxt)
                    break
                if self.in_function(fn, nxt):
                    cfg.fall[a] = ("next", nxt)
                    prev = a
                    a = nxt
                    continue
                # Falling out of the function's ranges is exactly a jump to the next address:
                # a trap if that byte is int3 padding, otherwise a tail call.
                if nxt < im.size and im.data[nxt] == 0xCC:
                    cfg.fall[a] = ("trap", nxt)
                else:
                    cfg.fall[a] = ("tail", nxt)
                    cfg.tails.add(nxt)
                break

    @staticmethod
    def _writes(p, idx):
        """True/False: instruction p writes general register number idx.  None: unknown.
        A call writes every volatile register (its callee may).  The decoder's report is
        completed from UNREPORTED_WRITES; after an instruction of UNKNOWN_WRITES nothing is
        known."""
        name = p.insn_name()
        if name == "call" and idx in VOLATILE:
            return True
        if name in UNKNOWN_WRITES:
            return None
        if idx in UNREPORTED_WRITES.get(name, ()):
            return True
        try:
            _, written = p.regs_access()
        except capstone.CsError:
            return None
        return any(w in GPR and GPR[w][1] == idx for w in written)

    def _last_writer(self, cfg, addr, reg, sure=False):
        """The instruction in the straight-line run ending at `addr` that last wrote `reg`.
        With `sure`, None as well if the run from that instruction on has an instruction that
        is not in SURE_WRITES: one whose writes this file does not know for certain."""
        idx = GPR[reg][1]
        a = cfg.pred.get(addr)
        for _ in range(24):
            if a is None:
                return None
            p = cfg.insns[a]
            if sure and p.insn_name() not in SURE_WRITES:
                return None
            w = self._writes(p, idx)
            if w is None:
                return None
            if w:
                return p
            a = cfg.pred.get(a)
        return None

    @staticmethod
    def _reg_from_image_slot(p, reg):
        """True if p is '<op> reg, qword ptr [rip + d]': a 64-bit register and one 8-byte slot
        of the image, addressed with no base or index register."""
        ops = p.operands
        return (len(ops) == 2 and ops[0].type == X.X86_OP_REG and ops[0].reg == reg
                and ops[1].type == X.X86_OP_MEM and ops[1].size == 8
                and ops[1].mem.base == X.X86_REG_RIP and ops[1].mem.index == 0 and ops[1].mem.segment == 0)

    def _indirect_jmp(self, fn, cfg, work, ins, single):
        op = ins.operands[0]
        if single:
            return ("ijmp_tail",)
        if op.type == X.X86_OP_MEM:
            if op.mem.index == 0:
                return ("ijmp_tail",)        # import slot, global pointer or vtable slot
            return self._abs_table_flow(fn, cfg, work, ins, ins)
        if op.type != X.X86_OP_REG or op.reg not in GPR or GPR[op.reg][0] != 64:
            return ("ijmp_unresolved",)
        # 'jmp reg' is a tail call in exactly three cases, judged by what last wrote the register
        # in the same straight-line run:
        #   * a 64-bit 'mov reg, [mem]' with no index register: a pointer loaded from a slot
        #     (import, global, vtable).  An indexed load is a table of code addresses, and
        #     treating that as a tail call silently drops the cases (review finding 10): it
        #     is lifted only if _abs_jump_table can read the table;
        #   * a call, when the register is rax: a thunk that jumps to the address a helper
        #     returned (delay-load style);
        #   * a 64-bit 'xor reg, [rip + d]' whose register was last written by a 64-bit
        #     'mov reg, [rip + d]': a pointer loaded from one slot of the image and decoded
        #     with a key from another (the Visual C++ runtime keeps the functions it looked
        #     up with GetProcAddress this way).  Neither operand has a base or an index
        #     register, so no value the function computes selects the target: it is not a
        #     table.  Any other change to a loaded pointer ('xor rax, rcx') is not this case.
        # Otherwise it must be a jump table that _jump_table recovers, or the function is
        # skipped.
        w = self._last_writer(cfg, ins.address, op.reg)
        if w is None:
            return ("ijmp_unresolved",)
        wops = w.operands
        if w.insn_name() == "call":
            return ("ijmp_tail",) if GPR[op.reg][1] == 0 else ("ijmp_unresolved",)
        if (w.insn_name() == "mov" and len(wops) == 2 and wops[0].type == X.X86_OP_REG and wops[0].reg == op.reg
                and wops[1].type == X.X86_OP_MEM and wops[1].size == 8):
            return ("ijmp_tail",) if wops[1].mem.index == 0 else self._abs_table_flow(fn, cfg, work, ins, w)
        if w.insn_name() == "xor" and self._reg_from_image_slot(w, op.reg):
            w0 = self._last_writer(cfg, w.address, op.reg)
            if w0 is not None and w0.insn_name() == "mov" and self._reg_from_image_slot(w0, op.reg):
                return ("ijmp_tail",)
            return ("ijmp_unresolved",)
        table = self._jump_table(fn, cfg, ins, w)
        if table is None:
            return ("ijmp_unresolved",)
        for t in table["targets"]:
            cfg.labels.add(t)
            work.append(t)
        cfg.tables.append(table)
        if table["bound"] == "unbounded":
            # the third item tells the translator to stop with a report, not with a plain trap,
            # where the address computed is none of the cases (insns/control.py, t_jmp)
            return ("ijmp_table", table["targets"], ("unbounded", fn.entry, table["table"], table["entries"]))
        return ("ijmp_table", table["targets"])

    def _jump_table(self, fn, cfg, jmp, add):
        """Jump tables of 32-bit entries (DESIGN.md section 6):

            mov    eX, dword ptr [rBase + rIdx*4 + disp]      (or movsxd rX, ...)
            add    rX, rBase
            jmp    rX

        Microsoft: rBase holds the image base ('lea rBase, [rip -> rva 0]', usually hoisted)
        and disp is the table's RVA, so entries are RVAs.  Clang: rBase holds the table's own
        address ('lea rBase, [rip + table]'), disp is 0, and entries are relative to the
        table.  Both are the same computation, target = value of rBase + entry, with the
        table at value of rBase + disp.

        Two variants of the Microsoft shape are read the same way.  The load may overwrite
        its own base and the add take the image base from a second register, set by a lea
        of its own in the same run ('lea rB, [rip -> 0] ; mov eB, [rB + rIdx*4 + disp] ;
        lea rA, [rip -> 0] ; add rB, rA ; jmp rB'): the table is at the load's base + disp,
        the targets are the add's base + entry.  And the add may have its operands the other
        way round ('add rBase, rX ; jmp rBase').

        The lifted code still computes the target the way the original does; the table is
        only read here to learn the set of possible targets, and the emitted switch traps on
        anything else.  So a table that is read too long is harmless and one read too short
        shows up as a trap, never as wrong behaviour.  build_cfg checks every recovered table
        again once the whole function is decoded (_table_still_holds).
        """
        aops = add.operands
        if (add.insn_name() != "add" or len(aops) != 2 or aops[0].type != X.X86_OP_REG
                or aops[1].type != X.X86_OP_REG or aops[0].reg != jmp.operands[0].reg
                or aops[1].reg not in GPR or GPR[aops[1].reg][0] != 64 or GPR[aops[1].reg][1] == GPR[aops[0].reg][1]):
            return None
        # One operand of the add is the entry, last written by the load from the table; the
        # other is the address the entries are relative to.  'add rX, rBase' is the usual
        # order; hand-written code also has 'add rBase, rX ; jmp rBase'.
        for entry_reg, addend in ((aops[0].reg, aops[1].reg), (aops[1].reg, aops[0].reg)):
            load = self._last_writer(cfg, add.address, entry_reg)
            lops = None if load is None else load.operands
            if (load is not None and load.insn_name() in ("mov", "movsxd") and len(lops) == 2
                    and lops[0].type == X.X86_OP_REG and lops[1].type == X.X86_OP_MEM and lops[1].size == 4):
                break
        else:
            return None
        m = lops[1].mem
        base_reg = m.base
        if (base_reg not in GPR or GPR[base_reg][0] != 64 or m.index not in GPR or GPR[m.index][0] != 64
                or m.scale != 4 or m.segment != 0):
            return None
        signed = load.insn_name() == "movsxd"
        bidx = GPR[base_reg][1]
        evidence = {add.address, load.address}

        base_value, lea = self._table_base(cfg, load, base_reg, m.disp)
        if base_value is None:
            return None
        # What the add adds.  Normally the register the load used as its base, untouched in
        # between.  Where the load overwrote its own base ('mov ecx, [rcx + rax*4 + d]'), or
        # the add uses another register, that register must be set by a RIP-relative lea in
        # the same run: then its value is known at the add whatever came before.
        w = self._last_writer(cfg, add.address, addend)
        if GPR[addend][1] == bidx and (w is None or w.address < load.address):
            add_value = base_value
        else:
            add_value = None if w is None else self._rip_lea(w, GPR[addend][1])
            if add_value is None:
                return None
            evidence.add(w.address)
        table_rva = base_value + m.disp
        if not (0 < table_rva < self.image.size - 4):
            return None

        bound, kind, used, byte_table = self._index_bound(cfg, load, m.index, base_reg, base_value)
        reach = None
        if bound is None and fn.ranges is None:
            # A leaf has no known extent: without a bound the table has no end.  This is the
            # one place where a bound held in a register is looked for ('cmp-reg'): only
            # where the function would otherwise be skipped, so that no table read before
            # is read differently now.
            bound, kind, used, byte_table = self._index_bound(cfg, load, m.index, base_reg, base_value, reg_bound=True)
            if kind != "cmp-reg":
                # No bound of any form: the table is read exactly or the run stops loudly
                # ('unbounded', _open_table_reach).  Asked here only, so it too can turn a
                # skipped function into a lifted one and nothing else.
                reach = self._open_table_reach(fn, cfg, load, lea, table_rva)
                if reach is None:
                    return None
                bound, kind, used, byte_table = None, "unbounded", set(), None
        evidence |= used
        # Read entries while they are addresses inside the function.
        targets, n = [], 0
        limit = OPEN_TABLE_CAP if kind == "unbounded" else 1024 if bound is None else bound
        while n < limit and table_rva + 4 * n + 4 <= self.image.size:
            if kind == "unbounded" and not self._file_bytes_stay(table_rva + 4 * n, 4):
                break
            e = self.image.u32(table_rva + 4 * n)
            if signed and e & 0x80000000:
                e -= 1 << 32
            t = add_value + e
            if not (0 <= t < self.image.size) or not self.in_function(fn, t):
                break
            if reach is not None and not (fn.entry <= t < reach):
                break
            if t not in targets:
                targets.append(t)
            n += 1
        if n == 0:
            return None
        if kind == "cmp-reg" and (lea is None or n != bound or not self._reg_bound_table(fn, table_rva, n, targets)):
            return None
        return {"jmp": jmp.address, "table": table_rva, "entries": n, "width": 4, "targets": sorted(targets),
                "layout": "image-relative" if add_value == 0 else "table-relative", "bound": kind,
                "byte_table": byte_table, "evidence": evidence, "lea": lea, "base": (bidx, m.disp, base_value),
                "load": load.address, "index": GPR[m.index][1], "limit": bound}

    def _file_bytes_stay(self, rva, n):
        """True if the n bytes at rva hold at run time what the file holds: they lie in no
        writable section and no relocation site overlaps them."""
        im = self.image
        if any(a < rva + n and rva < b for a, b in im.writable_ranges):
            return False
        if self._reloc_sites is None:
            self._reloc_sites = set(im.reloc_sites)
        return not any(a in self._reloc_sites for a in range(rva - 7, rva + n))

    def _open_table_reach(self, fn, cfg, load, lea, table_rva):
        """A jump table of 32-bit entries in a leaf, with no bounds check of any form on its
        index ('unbounded'; unit lifter-leaf-tables: mileswin64+0xa720 and +0x14ab0, whose
        callers hold the bound, engine+0xfdeb0 and +0xfe050).  Where the index comes from
        does not matter (a 32-bit value extended, a register used whole that the function
        never writes, the result of arithmetic that the load then overwrites): the rule
        holds for whatever index arrives.  The original jumps to base + table[index] for whatever index arrives.
        The lifted switch gets one case for every entry that could be read, and for every
        other address the jump computes it stops the run with a report (insns/control.py,
        t_jmp: the function, the index, the jump).  So it does what the machine code
        does wherever it does anything; at worst it stops where the original would have
        jumped to something that was not read.

        Returns the address the targets must stay below (they must also be at or after the
        entry), or None if the form is not one this rule reads:

          * the base is set by a RIP-relative lea in the run that ends in the jump, and the
            run from that lea to the jump is entered only at its start (_table_still_holds);
          * no conditional branch lies in that run, as far back as it goes: "no bounds check"
            means that nothing before the jump could be one.  A check that is there and
            cannot be read (a compare with a register that holds no provable constant) is
            another matter, and such a function stays skipped.

        What _jump_table then reads, entry by entry, stopping at the first that fails:

          * the entry's four bytes hold at run time what the file holds (_file_bytes_stay);
          * its target is inside the function as a leaf has it (in_function), at or after
            the entry, and below the first thing known to be another function after the
            entry (an entry, or the start of an unwind-described range);
          * and below the table itself when the table lies behind the entry: a function's
            code does not go on behind its own table, so an "entry" that points there is
            past the table's end;
          * at most OPEN_TABLE_CAP entries.

        No entry at all: no table, the function stays skipped.  With the function decoded,
        _reg_bound_table_refused looks at the targets again."""
        if lea is None:
            return None
        a = cfg.pred.get(load.address)
        while a is not None:
            if cfg.insns[a].insn_name() in JCC or cfg.insns[a].insn_name() in ("loop", "loope", "loopne"):
                return None
            a = cfg.pred.get(a)
        reach = self.image.size
        k = bisect.bisect_right(self._iv_starts, fn.entry)
        if k < len(self._iv_starts):
            reach = min(reach, self._iv_starts[k])
        reach = min([reach] + [e for e in self.funcs if e > fn.entry])
        if table_rva > fn.entry:
            reach = min(reach, table_rva)
        return reach

    def _table_base(self, cfg, load, base_reg, disp):
        """What the base register of a table load holds: (rva, the address of the lea that
        sets it in the same straight-line run or None), or (None, None).

        If the run that ends in the load sets the register, that must be a RIP-relative
        lea.  If nothing in the run does (the lea is hoisted out of a loop or to the top of
        the function), every 'lea rBase, [rip + x]' in the function must agree, except that
        for the Microsoft shape one that yields the image base is enough.  Either way the
        answer is checked again when the whole function is decoded (_table_still_holds)."""
        bidx = GPR[base_reg][1]
        w = self._last_writer(cfg, load.address, base_reg)
        if w is None:
            return self._scan_base(cfg, bidx, disp), None
        value = self._rip_lea(w, bidx)
        return (None, None) if value is None else (value, w.address)

    def _abs_table_flow(self, fn, cfg, work, jmp, load):
        table = self._abs_jump_table(fn, cfg, jmp, load)
        if table is None:
            return ("ijmp_unresolved",)
        cfg.tables.append(table)
        if table["layout"] == "absolute, functions":
            cfg.tails.update(table["targets"])
            return ("ijmp_tail",)
        for t in table["targets"]:
            cfg.labels.add(t)
            work.append(t)
        return ("ijmp_table", table["targets"])

    def _abs_jump_table(self, fn, cfg, jmp, load):
        """Tables of 64-bit absolute code addresses: what clang emits for a computed goto,
        for a switch it does not make table-relative, and for a call in tail position
        through a constant array of functions (issue 13):

            lea    rBase, [rip + table]                         (may be hoisted)
            jmp    qword ptr [rBase + rIdx*8 + disp]
        or  mov    rX, qword ptr [rBase + rIdx*8 + disp]
            jmp    rX

        `load` is the instruction with the memory operand.  The table is used only if all
        of this holds; otherwise the jump stays unresolved and the function is not lifted:

          * rBase holds a known address (_table_base);
          * the index is bounded by a check on the index register (_index_bound): without
            a bound there is no telling where the table ends;
          * the table is in the image and outside every writable section (what a writable
            array holds at run time is not what the file holds);
          * every slot the index can select is a relocation site holding the address of
            code - an absolute address in a relocatable image always has one;
          * the targets are all inside this function (a switch or computed goto: they become
            labels) or all entries of other functions (a tail call through the table; the
            jump is then translated like any other indirect tail call).

        In a leaf function "inside" needs care: relocated pointers into code outside every
        unwind-described function are discovery roots, so the slots of the table made each
        label a function before this one was decoded.  A target that only a relocation
        vouches for is taken as a label of this function when no function known another way
        and no unwind-described range begins between this function's entry and it;
        self.absorbed then keeps it inside this function.  It also stays a function of its
        own: nothing shows that the table is the only user of its address, and lifting the
        same bytes twice does not change behaviour."""
        m = None
        for op in load.operands:
            if op.type == X.X86_OP_MEM:
                m = op.mem
                if op.size != 8:
                    return None
        if (m is None or m.base not in GPR or GPR[m.base][0] != 64 or m.index not in GPR
                or GPR[m.index][0] != 64 or m.scale != 8 or m.segment != 0):
            return None
        evidence = {load.address}
        base_value, lea = self._table_base(cfg, load, m.base, m.disp)
        if base_value is None:
            return None
        table_rva = base_value + m.disp
        bound, kind, used, byte_table = self._index_bound(cfg, load, m.index, m.base, base_value)
        if bound is None:
            return None
        evidence |= used
        im = self.image
        if not (0 < table_rva and table_rva + 8 * bound <= im.size):
            return None
        if any(a < table_rva + 8 * bound and table_rva < b for a, b in im.writable_ranges):
            return None
        if self._reloc_sites is None:
            self._reloc_sites = set(im.reloc_sites)
        targets = []
        for n in range(bound):
            if table_rva + 8 * n not in self._reloc_sites:
                return None
            t = im.u64(table_rva + 8 * n) - im.image_base
            if not (0 <= t < im.size and im.is_exec(t)):
                return None
            if t not in targets:
                targets.append(t)
        labels = [t for t in targets if self._abs_target_is_label(fn, t)]
        if len(labels) not in (0, len(targets)):
            return None                 # some inside, some not: not a shape this was written for
        if not labels and not all(t in self.funcs for t in targets):
            return None                 # into the middle of something that is not this function
        if labels and fn.ranges is None:
            self.absorbed.setdefault(fn.entry, set()).update(t for t in labels if t != fn.entry)
        return {"jmp": jmp.address, "table": table_rva, "entries": bound, "width": 8, "targets": sorted(targets),
                "layout": "absolute" if labels else "absolute, functions", "bound": kind,
                "byte_table": byte_table, "evidence": evidence, "lea": lea,
                "base": (GPR[m.base][1], m.disp, base_value),
                "load": load.address, "index": GPR[m.index][1], "limit": bound}

    def _abs_target_is_label(self, fn, t):
        if fn.ranges is not None or t == fn.entry or t in self.absorbed.get(fn.entry, ()):
            return self.in_function(fn, t)
        f = self.funcs.get(t)
        if f is None or f.origin != "reloc" or f.ranges is not None or t < fn.entry:
            return False
        return not any(fn.entry < e <= t and (g.origin != "reloc" or g.ranges is not None)
                       for e, g in self.funcs.items()) and self.container(t) is None

    @staticmethod
    def _rip_lea(p, bidx=None):
        """The RVA that 'lea r64, [rip + x]' puts in general register bidx (any 64-bit general
        register when bidx is None), or None."""
        ops = p.operands
        if (p.insn_name() == "lea" and len(ops) == 2 and ops[0].type == X.X86_OP_REG and ops[0].reg in GPR
                and GPR[ops[0].reg][0] == 64 and bidx in (None, GPR[ops[0].reg][1]) and ops[1].type == X.X86_OP_MEM
                and ops[1].mem.base == X.X86_REG_RIP and ops[1].mem.index == 0):
            return p.address + p.size + ops[1].mem.disp
        return None

    def _scan_base(self, cfg, bidx, disp):
        values = {v for v in (self._rip_lea(p, bidx) for p in cfg.insns.values()) if v is not None}
        if disp != 0 and 0 in values:
            return 0
        return values.pop() if len(values) == 1 else None

    def _index_bound(self, cfg, load, index_reg, base_reg=None, base_value=None, reg_bound=False):
        """How many table entries the index can select: (count, 'cmp' | 'and' | 'byte-table' |
        'cmp-reg' | None, the addresses of the instructions that say so, the (rva, length) of a
        byte-index table or None).

        The bound must be established on the index register itself (review suspicion: the
        first ja/jae before the load was taken, whatever it compared, so an unrelated
        'cmp x, 3; ja' cut the table short and valid cases trapped):
          cmp idx, N ; ja/jae elsewhere      the index, or a register it was copied from
          and idx, N                         as the last write to the index
          movzx idx, byte ptr [rBase + i + d]   Microsoft's two-level form: a byte table maps a
                                             bounded first index i to the entry number, so
                                             the count is the largest byte in it plus one
        A conditional branch on something else is skipped; any other write to the index ends
        the search without a bound.

        With reg_bound (tables of 32-bit entries only) one more form, and nothing wider:
          mov rB, N ... cmp idx, rB ; ja/jae elsewhere
        the bound is in a register that holds a constant (_constant_at_compare).  _jump_table
        asks for it only in a leaf for which the forms above gave no bound.  Because the
        count is then the only thing that ends the table, the index is followed more strictly
        here: `need` is the number of low bits of the followed register that the 64-bit index
        is made of (64 at the load; 32 behind 'movsxd r64, r32', 'mov r32, r32' or cdqe; 8 or
        16 behind a movzx), and the compare must be on a 32- or 64-bit register at least that
        wide.  A copy that writes only part of the index register cannot be followed.  The
        caller holds a table found this way to more (_jump_table, _reg_bound_table,
        _reg_bound_table_refused)."""
        idx = GPR[index_reg][1]
        need = GPR[index_reg][0]
        a = cfg.pred.get(load.address)
        for _ in range(24):
            if a is None:
                break
            p = cfg.insns[a]
            name, ops = p.insn_name(), p.operands
            if name in ("ja", "jae"):
                c = cfg.insns.get(cfg.pred.get(a))
                if (c is not None and c.insn_name() == "cmp" and len(c.operands) == 2
                        and c.operands[0].type == X.X86_OP_REG and c.operands[0].reg in GPR
                        and GPR[c.operands[0].reg][1] == idx and not GPR[c.operands[0].reg][2]
                        and c.operands[1].type == X.X86_OP_IMM and 0 <= c.operands[1].imm < 4096):
                    return c.operands[1].imm + (1 if name == "ja" else 0), "cmp", {a, c.address}, None
                if reg_bound and c is not None:
                    n, mov = self._constant_at_compare(cfg, c, idx, need)
                    if n is not None:
                        return n + (1 if name == "ja" else 0), "cmp-reg", {a, c.address, mov}, None
            else:
                w = self._writes(p, idx)
                if w is None:
                    break
                if w:
                    if (name == "and" and len(ops) == 2 and ops[0].type == X.X86_OP_REG and ops[0].reg in GPR
                            and GPR[ops[0].reg][1] == idx and not GPR[ops[0].reg][2]
                            and ops[1].type == X.X86_OP_IMM and 0 <= ops[1].imm < 4096):
                        return ops[1].imm + 1, "and", {a}, None
                    if (name == "movzx" and len(ops) == 2 and ops[1].type == X.X86_OP_MEM and ops[1].size == 1
                            and base_reg is not None and ops[1].mem.base == base_reg and ops[1].mem.index in GPR
                            and ops[1].mem.scale == 1 and ops[1].mem.segment == 0):
                        n1, _, used, _ = self._index_bound(cfg, p, ops[1].mem.index)
                        btab = base_value + ops[1].mem.disp
                        if n1 is None or not (0 < btab and btab + n1 <= self.image.size):
                            break
                        return max(self.image.data[btab:btab + n1]) + 1, "byte-table", used | {a}, (btab, n1)
                    if (name in ("mov", "movsxd", "movzx") and len(ops) == 2 and ops[1].type == X.X86_OP_REG
                            and ops[1].reg in GPR and not GPR[ops[1].reg][2]):
                        idx = GPR[ops[1].reg][1]             # a copy: follow the source
                        full = ops[0].type == X.X86_OP_REG and GPR.get(ops[0].reg, (0,))[0] >= 32
                        # 65: more bits than any compare has, and it stays (a later full copy
                        # must not hide that part of the index came from somewhere else)
                        need = min(need, GPR[ops[1].reg][0]) if full and need <= 64 else 65
                    elif name != "cdqe":                     # cdqe widens eax in place
                        break
                    elif need <= 64:
                        need = min(need, 32)
            a = cfg.pred.get(a)
        return None, None, set(), None

    def _constant_at_compare(self, cfg, c, idx, need):
        """For 'cmp rI, rB' at c, where rI is general register number idx, of which the index
        uses the low `need` bits: (N, the address of the instruction that sets rB) if rB is
        known to hold the constant N whenever the compare executes, else (None, None).

        Established in the narrowest way: the last instruction to write any part of rB in
        the straight-line run that ends at the compare is 'mov r32, N' or 'mov r64, N' (a
        write of the whole register) with 0 <= N < 4096.  The caller puts that mov among the
        table's evidence, so _table_still_holds refuses the table if anything can enter the
        run between the mov and the jump; with that, every path to the compare has executed
        the mov and nothing that writes rB after it.  Refused: a register set any other way
        (a copy, a load, arithmetic, 'xor r, r', a write of 8 or 16 bits), a call in between
        when rB is volatile, a compare of 8 or 16 bits or narrower than the index, a compare
        of the index with itself, the operands the other way round, and a constant set
        before a label (hoisted): a second path could bring another value.  Refused as well:
        any instruction between the mov and the compare that is not in SURE_WRITES, whatever
        the decoder says it writes ('cmpxchg' writes eax and the decoder does not report it:
        finding 7 of docs/reviews/night-2026-10-11-code-review.md), so a call, too, whichever
        register holds the bound."""
        ops = c.operands
        if (c.insn_name() != "cmp" or len(ops) != 2 or ops[0].type != X.X86_OP_REG or ops[1].type != X.X86_OP_REG
                or ops[0].reg not in GPR or ops[1].reg not in GPR):
            return None, None
        (width, i, _), (bwidth, b, _) = GPR[ops[0].reg], GPR[ops[1].reg]
        if i != idx or b == idx or width not in (32, 64) or bwidth != width or need > width:
            return None, None
        w = self._last_writer(cfg, c.address, ops[1].reg, sure=True)
        if w is None or w.insn_name() != "mov" or len(w.operands) != 2:
            return None, None
        dst, src = w.operands
        if (dst.type != X.X86_OP_REG or GPR.get(dst.reg, (0, -1))[1] != b or GPR[dst.reg][0] not in (32, 64)
                or src.type != X.X86_OP_IMM or not (0 <= src.imm < 4096)):
            return None, None
        return src.imm, w.address

    def _reg_bound_table(self, fn, table_rva, n, targets):
        """What a table whose bound is in a register ('cmp-reg') must satisfy besides what
        _jump_table checked (a base set by a lea in the same run, and every one of the n
        entries the index can select inside the function), as far as it can be told before
        the function is decoded:

          * the table's bytes are in no writable section and none of them is a relocation
            site: at run time they hold what the file holds;
          * in a leaf, which has no unwind range, 'inside the function' is made narrower
            than in_function: every target is at or after the entry, and nothing known to
            be another function (an entry, or the start of an unwind-described range) lies
            after the entry up to the last target."""
        im = self.image
        end = table_rva + 4 * n
        if end > im.size or any(a < end and table_rva < b for a, b in im.writable_ranges):
            return False
        if self._reloc_sites is None:
            self._reloc_sites = set(im.reloc_sites)
        if any(a in self._reloc_sites for a in range(table_rva - 7, end)):
            return False
        if fn.ranges is None:
            last = max(targets)
            if min(targets) < fn.entry:
                return False
            k = bisect.bisect_right(self._iv_starts, fn.entry)
            if k < len(self._iv_starts) and self._iv_starts[k] <= last:
                return False
            if any(fn.entry < e <= last for e in self.funcs):
                return False
        return True

    def _reg_bound_table_refused(self, cfg, t):
        """With the whole function decoded, why the 'cmp-reg' or 'unbounded' table t cannot be
        kept, or None: every target must be an instruction discovery decoded, none may lie in the
        middle of a decoded instruction, and no decoded instruction may overlap the table's
        own bytes (they are data: nothing may fall or jump into them)."""
        lo, hi = t["table"], t["table"] + t["width"] * t["entries"]
        targets = set(t["targets"])
        what = "jump table bounded by a register" if t["bound"] == "cmp-reg" else "jump table without a bound"
        if not all(a in cfg.insns for a in targets):
            return what + ": a target is not decoded"
        for a, ins in cfg.insns.items():
            if a < hi and lo < a + ins.size:
                return what + ": the table's bytes are decoded as code"
            if any(a < x < a + ins.size for x in targets):
                return what + ": a target inside an instruction"
        return None

    # ---- over the whole function ---------------------------------------------------
    #
    # What follows reasons about every path through the function instead of one straight-line
    # run.  That is sound for the code the lifter emits: the lifted function has exactly the
    # instructions and edges of the CFG (code that was not decoded is not emitted, and a jump
    # to an address without a case stops the run), and it is entered only at fn.entry.

    @staticmethod
    def _edges_in(cfg):
        """{rva: [(rva of a predecessor, True for a branch to its target / False for falling
        through)]} over what is decoded."""
        into = {}
        for a, f in cfg.fall.items():
            if f[0] == "next":
                into.setdefault(f[1], []).append((a, False))
        for a, f in cfg.flow.items():
            if not f:
                continue
            if f[0] == "jmp" or (f[0] == "jcc" and len(f) > 2 and f[2]):
                into.setdefault(f[1], []).append((a, True))
            elif f[0] == "ijmp_table":
                for t in f[1]:
                    into.setdefault(t, []).append((a, True))
        return into

    def _reaching_writers(self, fn, cfg, into, addr, idx, closed_calls=False):
        """The instructions that can be the last to have written general register number idx
        when control arrives at addr, on any path through the function: a set of addresses,
        with ENTRY in it if a path from the function's entry writes nothing to the register.
        None if it cannot be told.  A call counts as a write to every volatile register;
        with closed_calls, not when the callee shows that it leaves the register alone
        (_callee_writes)."""
        out, seen, todo = set(), set(), [addr]
        while todo:
            a = todo.pop()
            if a in seen:
                continue
            seen.add(a)
            if len(seen) > 4096:
                return None
            edges = into.get(a, ())
            if a == fn.entry:
                out.add(ENTRY)
            elif not edges:
                return None             # decoded, and nothing is known to lead here
            for p, _ in edges:
                ins = cfg.insns[p]
                w = self._writes(ins, idx)
                if w is None:
                    return None
                if w and closed_calls and ins.insn_name() == "call" and ins.operands[0].type == X.X86_OP_IMM:
                    written = self._callee_writes(cfg, ins.operands[0].imm)
                    w = written is None or idx in written
                if w:
                    out.add(p)
                else:
                    todo.append(p)
        return out

    def _callee_writes(self, cfg, target):
        """The numbers of the general registers that a direct call to `target` can write, read
        off the callee, or None if that cannot be done (the caller then assumes every
        volatile register, as _writes does).

        It can be done for a function that is decoded completely and is closed: every path
        from its entry ends in a ret, with no call, no tail call, no indirect jump and no
        fall out of it on the way.  Such a callee executes its own instructions and nothing
        else, so a register none of them writes has, after the call, the value it had before.
        (A compiler that sees both functions relies on exactly this: it keeps a jump-table
        base in r10 across a call to a small leaf.)"""
        if target not in cfg.callee_writes:
            written = None
            fn = self.funcs.get(target)
            c = None if fn is None else self.build_cfg(fn)
            if (c is not None and not (c.problems or c.pending or c.tails or c.calls)
                    and all(t in c.insns for t in c.labels)
                    and all(f is None or f[0] in ("jmp", "jcc", "ret") for f in c.flow.values())
                    and all(f[0] == "next" for f in c.fall.values())):
                written = set()
                for ins in c.insns.values():
                    try:
                        written |= {GPR[r][1] for r in ins.regs_access()[1] if r in GPR}
                    except capstone.CsError:
                        written = None
                        break
            cfg.callee_writes[target] = written
        return cfg.callee_writes[target]

    def _entered_at_start(self, fn):
        """True if fn.entry is where its function begins: not a second entry into the middle
        of an unwind-described function."""
        return self.container(fn.entry) in (None, fn.entry) and not fn.origin.endswith("-into")

    def _origins(self, fn, cfg, into, addr, idx, depth=4):
        """Where the 64-bit value of general register number idx can come from when control
        arrives at addr, as a set of
            ("ptr",)         a pointer the function loaded whole: the forms that make 'jmp reg'
                             a tail call in _indirect_jmp (an unindexed 64-bit load, such a
                             load decoded with a key from the image, rax after a call)
            ("entry",)       the value a volatile register had when the function was entered
                             at its start: the caller's (an argument, or what a thunk was
                             handed in rax, r10 or r11)
            ("const", rva)   'lea reg, [rip + x]' of an address in an executable section
        following 64-bit register copies and 'cmovcc reg, reg' (either value).  None if any
        writer that reaches addr is something else: arithmetic, an indexed load, a narrower
        write."""
        ws = self._reaching_writers(fn, cfg, into, addr, idx)
        if not ws or depth == 0:
            return None
        out = set()
        for w in ws:
            if w == ENTRY:
                if idx not in VOLATILE or not self._entered_at_start(fn):
                    return None
                out.add(("entry",))
                continue
            p = cfg.insns[w]
            name, ops = p.insn_name(), p.operands
            more = None
            if name == "call":
                more = {("ptr",)} if idx == 0 else None
            elif len(ops) == 2 and ops[0].type == X.X86_OP_REG and GPR.get(ops[0].reg) == (64, idx, False):
                src = ops[1]
                src_reg = GPR[src.reg][1] if src.type == X.X86_OP_REG and GPR.get(src.reg, (0,))[0] == 64 else None
                if name == "mov" and src.type == X.X86_OP_MEM and src.size == 8:
                    more = {("ptr",)} if src.mem.index == 0 else None
                elif name == "mov" and src_reg is not None:
                    more = self._origins(fn, cfg, into, w, src_reg, depth - 1)
                elif name.startswith("cmov") and src_reg is not None:
                    kept = self._origins(fn, cfg, into, w, idx, depth - 1)
                    moved = self._origins(fn, cfg, into, w, src_reg, depth - 1)
                    more = None if kept is None or moved is None else kept | moved
                elif name == "lea":
                    t = self._rip_lea(p, idx)
                    if t is not None and 0 <= t < self.image.size and self.image.is_exec(t):
                        more = {("const", t)}
                elif name == "xor" and self._reg_from_image_slot(p, ops[0].reg):
                    w0 = self._reaching_writers(fn, cfg, into, w, idx)
                    if (w0 is not None and len(w0) == 1 and ENTRY not in w0
                            and cfg.insns[min(w0)].insn_name() == "mov"
                            and self._reg_from_image_slot(cfg.insns[min(w0)], ops[0].reg)):
                        more = {("ptr",)}
            if more is None:
                return None
            out |= more
        return out

    def _late_jump(self, fn, cfg, into, jmp):
        """'jmp reg' judged by every definition of the register that reaches it (_origins):
        (verdict, addresses to add to the function's tail-call targets), or None.

        If every origin is a whole pointer or the caller's value, nothing this function
        computes from an index selects the target, so it is not one of its jump tables: a tail
        call.  An origin that is a known code address is treated as a direct jump to that
        address would be: inside the function it is a label, outside it a tail call to a
        function.  All the known addresses must fall the same way, and labels cannot be mixed
        with pointers (the translation is either a tail call or a switch)."""
        origins = self._origins(fn, cfg, into, jmp.address, GPR[jmp.operands[0].reg][1])
        if not origins:
            return None
        consts = {o[1] for o in origins if o[0] == "const"}
        inside = {t for t in consts if self.in_function(fn, t)}
        if not inside:
            return ("ijmp_tail",), frozenset(consts)
        if inside != consts or len(origins) != len(consts):
            return None
        return ("ijmp_table", sorted(consts)), frozenset()

    def _judge_pending(self, fn, cfg, work):
        """Judge the pending jumps with what is decoded now.  True if any got a verdict."""
        if not cfg.pending:
            return False
        into = self._edges_in(cfg)
        done = False
        for a in sorted(cfg.pending):
            verdict = self._late_jump(fn, cfg, into, cfg.insns[a])
            if verdict is None:
                continue
            cfg.pending.discard(a)
            cfg.late[a] = verdict
            cfg.flow[a], tails = verdict
            cfg.tails.update(tails)
            if cfg.flow[a][0] == "ijmp_table":
                for t in cfg.flow[a][1]:
                    cfg.labels.add(t)
                    work.append(t)
            done = True
        return done

    def _table_still_holds(self, fn, cfg, t):
        """With the whole function decoded: the bounds check, the load and the add must form
        a run that is entered only at its start (a label in the middle means another path
        reaches the jump with another index or bound).  For the base register one of two
        things must hold.  Either the lea that sets it belongs to that run too, with no
        label between it and the jump: then nothing else can have set the base.  Or the
        register has one possible value in the whole function: a lea is normally hoisted out
        of a loop, so it lies before a label, which is why that is checked by scanning.

        A switch at the head of a loop has a label between its bounds check and its load:
        the cases jump back to the load after a check of their own.  That table holds if the
        same three things are true on every path instead of in one run
        (_table_holds_on_every_path)."""
        def entered_at_start(need):
            left = set(need)
            a = t["jmp"]
            while left and a is not None:
                left.discard(a)
                if left and a in cfg.labels:
                    return False
                a = cfg.pred.get(a)
            return not left
        if t["bound"] in ("cmp-reg", "unbounded"):
            # A bound in a register: the constant's mov, the compare, the lea of the base,
            # the load and the add must all be in one run entered only at its start.  No
            # reasoning over paths and no scan for a hoisted lea.  The same for a table
            # without a bound (the lea, the load and the add).
            return t["lea"] is not None and entered_at_start(t["evidence"] | {t["lea"]})
        if not entered_at_start(t["evidence"]):
            return (entered_at_start({a for a in t["evidence"] if a >= t["load"]})
                    and self._table_holds_on_every_path(fn, cfg, t))
        if t["lea"] is not None and entered_at_start(t["evidence"] | {t["lea"]}):
            return True
        bidx, disp, value = t["base"]
        return self._scan_base(cfg, bidx, disp) == value

    def _table_holds_on_every_path(self, fn, cfg, t):
        """The run from the table load to the jump is entered only at the load (the caller
        checked that).  What is left to show, on every path that arrives at the load:

          * the index is below the bound the table was read with: each path passes a check
            of the index register (_bound_on_every_path) and none allows more entries;
          * the base register holds the table's base: every write to it that reaches the
            load is the same RIP-relative lea value, and no path comes from the function's
            entry without one.  (A call on the way writes the base if it is a volatile
            register, unless the callee is closed and does not: _callee_writes.)"""
        if t["bound"] not in ("cmp", "and"):
            return False                # a byte table has two indexes: not followed here
        into = self._edges_in(cfg)
        bound = self._bound_on_every_path(fn, cfg, into, t["load"], t["index"])
        if bound is None or bound > t["limit"]:
            return False
        bidx, _, value = t["base"]
        ws = self._reaching_writers(fn, cfg, into, t["load"], bidx, closed_calls=True)
        return bool(ws) and all(w != ENTRY and self._rip_lea(cfg.insns[w], bidx) == value for w in ws)

    def _bound_on_every_path(self, fn, cfg, into, load, idx):
        """The largest number of entries the index (general register number idx at the
        instruction at `load`) can select over all paths to it, or None if some path does
        not bound it.  A path is followed backwards until it passes one of

            cmp idx, N ; ja/jae elsewhere     arriving by falling through the branch
            cmp idx, N ; jb/jbe here          arriving by taking the branch
            and idx, N
            mov idx, N  or  xor idx, idx      a constant index: N + 1 entries

        with the compare the only way to the branch, on a 32- or 64-bit register.  Copies of
        the index are followed to their source as in _index_bound; any other write to it, and
        a path that starts at the function's entry, gives no bound.  A path that comes back
        to where it has been with the index in the same register adds nothing: the index has
        not changed on it, so it is bounded by the paths that enter the cycle."""
        best, seen, todo = 0, set(), [(load, idx)]
        while todo:
            a, idx = todo.pop()
            if (a, idx) in seen:
                continue
            seen.add((a, idx))
            edges = into.get(a, ())
            if a == fn.entry or not edges or len(seen) > 4096:
                return None
            for p, taken in edges:
                ins = cfg.insns[p]
                name, ops = ins.insn_name(), ins.operands
                if name in JCC:
                    n = None
                    how = into.get(p, ())
                    c = cfg.insns[how[0][0]] if len(how) == 1 and not how[0][1] else None
                    if (c is not None and c.insn_name() == "cmp" and len(c.operands) == 2
                            and c.operands[0].type == X.X86_OP_REG and c.operands[0].reg in GPR
                            and GPR[c.operands[0].reg][1] == idx and GPR[c.operands[0].reg][0] >= 32
                            and c.operands[1].type == X.X86_OP_IMM and 0 <= c.operands[1].imm < 4096):
                        n = {("ja", False): 1, ("jae", False): 0, ("jbe", True): 1, ("jb", True): 0}.get((name, taken))
                    if n is None:
                        todo.append((p, idx))
                    else:
                        best = max(best, c.operands[1].imm + n)
                    continue
                w = self._writes(ins, idx)
                if w is None:
                    return None
                if not w or name == "cdqe":
                    todo.append((p, idx))
                elif (name == "and" and len(ops) == 2 and ops[0].type == X.X86_OP_REG and ops[0].reg in GPR
                        and GPR[ops[0].reg][1] == idx and GPR[ops[0].reg][0] >= 32
                        and ops[1].type == X.X86_OP_IMM and 0 <= ops[1].imm < 4096):
                    best = max(best, ops[1].imm + 1)
                elif (name in ("mov", "movsxd", "movzx") and len(ops) == 2 and ops[1].type == X.X86_OP_REG
                        and ops[1].reg in GPR and not GPR[ops[1].reg][2]):
                    todo.append((p, GPR[ops[1].reg][1]))
                elif (len(ops) == 2 and ops[0].type == X.X86_OP_REG and GPR.get(ops[0].reg, (0,))[0] >= 32
                        and name == "mov" and ops[1].type == X.X86_OP_IMM and 0 <= ops[1].imm < 4096):
                    best = max(best, ops[1].imm + 1)
                elif (len(ops) == 2 and ops[0].type == X.X86_OP_REG and GPR.get(ops[0].reg, (0,))[0] >= 32
                        and name == "xor" and ops[1].type == X.X86_OP_REG and ops[1].reg == ops[0].reg):
                    best = max(best, 1)
                else:
                    return None
        return best or None

    # ---- a jump through a table of function pointers ---------------------------------
    #
    # Unit lifter-tail-tables, the planner's rule (swarm/briefs/lifter-tail-tables.md).  A jump
    # through a pointer that the function loads at run time from a table which cannot be one of
    # its own switch tables is an indirect tail call: the lifted function hands the loaded
    # address to the runtime's dispatcher for indirect calls, which stops on an address that is
    # no lifted function, and returns what the callee returns.  That is exact wherever it does
    # anything, because the address is the one the machine code would jump to, PROVIDED the
    # callee's return goes where the machine's would: in lifted code a tail call always returns
    # to this function's caller, and on the machine the callee's ret pops whatever is on top of
    # the stack.  So the reading is taken only where the stack pointer at the jump is shown to
    # be the entry's on every path (_stack_at_entry), and the function stays skipped otherwise.

    def _tail_through_table(self, fn, cfg, into, jmp):
        """The proof that the unexplained indirect jump `jmp` is a tail call through a table of
        function pointers, as a dict for the report, or None (the function stays skipped).

        The jump's target is a 64-bit value loaded with an index register and never changed:
        'jmp qword ptr [rB + rI*s + d]', or 'jmp rJ' where the only write to rJ that reaches the
        jump on any path is 'mov rJ, qword ptr [rB + rI*s + d]' (a pointer that is changed after
        the load, or that a second path sets another way, is not this).  Neither address
        register is rsp.  And one of these shows that the table is no switch table of this
        function, whose address a compiler always takes as a link-time constant:

          'run-time pointer'   the index is scaled (s > 1) and rB holds, on every path to the
                               load, a pointer the function got at run time (_run_time_pointer):
                               the table is wherever that pointer says, which nobody knows at
                               lift time (client+0x25bb90: the table hangs off the object).
          'run-time pointer, unscaled'
                               no scale, and neither register is an address the file fixes:
                               one holds a pointer the function got at run time and the other
                               a value it got at run time (_run_time_value), on every path.
                               The pointer loaded is a member of somebody's object at an
                               offset that is itself read from memory (client+0x306540).
          'writable table'     rB is a RIP-relative lea of one address on every path (with
                               no scale, either register is, and the other is not: the index
                               was shifted beforehand, engine+0x5c0b0 and +0x5c100; then the
                               table must be writable, a read-only one is refused), and the
                               table lies in a writable section, with
                               no label of this function among what the file holds there
                               (_writable_table).  It cannot be read, and a compiler puts no
                               switch table where the program can write (engine+0xf130).
          'read-only table of functions'
                               the index is scaled by 8, rB is a RIP-relative lea on every
                               path, the table is in no writable section, and what the file
                               holds there is function pointers (_table_of_functions).  This
                               table could be read; it is read as evidence only, because the
                               index has no bound (engine+0x2526d0).

        Then the stack: _stack_at_entry.  The function must be entered at its start (a second
        entry into an unwind-described function starts inside somebody's frame)."""
        if not self._entered_at_start(fn):
            return None
        op = jmp.operands[0]
        if op.type == X.X86_OP_MEM:
            load, mem = jmp, op
        else:
            if op.type != X.X86_OP_REG or GPR.get(op.reg, (0,))[0] != 64:
                return None
            ws = self._reaching_writers(fn, cfg, into, jmp.address, GPR[op.reg][1])
            if not ws or len(ws) != 1 or ENTRY in ws:
                return None
            load = cfg.insns[min(ws)]
            lops = load.operands
            if (load.insn_name() != "mov" or len(lops) != 2 or lops[0].type != X.X86_OP_REG or lops[0].reg != op.reg
                    or lops[1].type != X.X86_OP_MEM):
                return None
            mem = lops[1]
        m = mem.mem
        if (mem.size != 8 or m.segment != 0 or m.index == 0 or GPR.get(m.base, (0,))[0] != 64
                or GPR.get(m.index, (0,))[0] != 64):
            return None
        bidx, iidx = GPR[m.base][1], GPR[m.index][1]
        if 4 in (bidx, iidx):
            return None
        form, more = None, {}
        base = self._lea_constant(fn, cfg, into, load.address, bidx)
        if m.scale == 1:
            # No scale: the address is a plain sum, and either register may be the one that
            # holds the table (the index was shifted beforehand: engine+0x5c0b0).  Both at
            # fixed addresses is nothing this rule knows.
            other = self._lea_constant(fn, cfg, into, load.address, iidx)
            if base is not None and other is not None:
                return None
            base = other if base is None else base
            if base is not None and self._writable_table(fn, base + m.disp):
                form, more = "writable table", {"table": "0x%x" % (base + m.disp)}
            elif base is None and any(
                    self._run_time_pointer(fn, cfg, into, load.address, p) and self._run_time_value(fn, cfg, into, load.address, v)
                    for p, v in ((bidx, iidx), (iidx, bidx))):
                form = "run-time pointer, unscaled"
        elif self._run_time_pointer(fn, cfg, into, load.address, bidx):
            form = "run-time pointer"
        elif m.scale == 8 and base is not None and not self._touches_writable(base + m.disp, 8):
            read = self._table_of_functions(fn, base + m.disp)
            if read is not None:
                form = "read-only table of functions"
                more = {"table": "0x%x" % (base + m.disp), "slots": read[0], "functions": read[1]}
        elif m.scale > 1 and base is not None and self._writable_table(fn, base + m.disp):
            form, more = "writable table", {"table": "0x%x" % (base + m.disp)}
        if form is None or not self._stack_at_entry(fn, cfg, into, jmp.address):
            return None
        return dict({"jmp": jmp.address, "load": load.address, "form": form}, **more)

    def _lea_constant(self, fn, cfg, into, addr, idx):
        """The RVA that general register number idx holds whenever control arrives at addr,
        if every write to it that reaches addr on any path is a RIP-relative lea of that one
        address; else None."""
        ws = self._reaching_writers(fn, cfg, into, addr, idx)
        if not ws or ENTRY in ws:
            return None
        values = {self._rip_lea(cfg.insns[w], idx) for w in ws}
        return values.pop() if len(values) == 1 and None not in values else None

    def _touches_writable(self, rva, n):
        return any(a < rva + n and rva < b for a, b in self.image.writable_ranges)

    def _own_label(self, fn, t):
        """True if the code address t, found in a table that fn jumps through, is a label of fn
        and not a function of its own: inside its ranges if it has unwind ranges; for a leaf,
        what _abs_target_is_label says (only a relocation vouches for it, and nothing known to
        be another function lies between the entry and it).  The entry itself is a function."""
        return t != fn.entry and self._abs_target_is_label(fn, t)

    def _writable_table(self, fn, table_rva):
        """True if the 8 bytes at table_rva lie in a writable section and nothing the file holds
        there says the table is a switch table of fn after all.  What a writable table holds
        at run time is not what the file holds, so it cannot be read, and a compiler puts no
        switch table where the program can write.  The one thing the file can still show is
        the opposite: a relocated pointer to a label of this function (_own_label) in the
        table's first OPEN_TABLE_CAP slots' worth of bytes, at any offset (the records may be
        wider than a pointer), up to the end of the section.  Then it is refused."""
        im = self.image
        ends = [b for a, b in im.writable_ranges if a <= table_rva and table_rva + 8 <= b]
        if not ends:
            return False
        if self._reloc_sites is None:
            self._reloc_sites = set(im.reloc_sites)
        for a in range(table_rva, min(ends[0], table_rva + 8 * OPEN_TABLE_CAP) - 7):
            if a in self._reloc_sites:
                t = im.u64(a) - im.image_base
                if 0 <= t < im.size and im.is_exec(t) and self._own_label(fn, t):
                    return False
        return True

    def _table_of_functions(self, fn, table_rva):
        """The evidence that a read-only table of 64-bit slots, which the function jumps through
        with no bound on the index, is a table of function pointers (engine+0x2526d0, where
        the table is sparse): (slots read, how many hold a function), or None.

        Slots are read from the first while each holds at run time what the file holds and is
        either zero, with no relocation, or a relocation site holding the entry of a function
        discovery knows; at most OPEN_TABLE_CAP.  None if no slot read holds a function, or if
        one holds a label of this function (_own_label): then the table is, or may be, a
        switch table of this function, which must be read with a bound or not at all.

        Nothing is made of what was read: no cases, no targets.  The lifted jump hands
        whatever the slot holds at run time to the dispatcher, for any index, as the machine
        code jumps to it; the reading only tells this table from a switch table."""
        im = self.image
        if self._reloc_sites is None:
            self._reloc_sites = set(im.reloc_sites)
        n = functions = 0
        while n < OPEN_TABLE_CAP:
            a = table_rva + 8 * n
            if not (0 < a and a + 8 <= im.size) or self._touches_writable(a, 8):
                break
            if a in self._reloc_sites:
                t = im.u64(a) - im.image_base
                if t not in self.funcs:
                    break
                if self._own_label(fn, t):
                    return None
                functions += 1
            elif im.u64(a) != 0 or any(x in self._reloc_sites for x in range(a - 7, a + 8)):
                break
            n += 1
        return (n, functions) if functions else None

    def _in_writable(self, rva, n):
        """True if the n bytes at rva lie in one writable section."""
        return any(a <= rva and rva + n <= b for a, b in self.image.writable_ranges)

    def _run_time_pointer(self, fn, cfg, into, addr, idx, depth=4):
        """True if general register number idx holds, whenever control arrives at addr, a
        64-bit value that the function got at run time and did not compute: every write to
        it that reaches addr on any path is

            mov r64, qword ptr [mem]     a pointer loaded whole from memory; if the memory is
                                         a slot of the image addressed through rip, the slot
                                         must be writable (what a read-only slot holds is in
                                         the file: such a pointer can be read at lift time and
                                         says nothing here)
            mov r64, r64                 a copy of a register for which the same holds there
            (none)                       the value the function was entered with, in a
                                         volatile register: the caller's

        Anything else (a lea, arithmetic, a narrower write, a call) is not."""
        ws = self._reaching_writers(fn, cfg, into, addr, idx)
        if not ws or depth == 0:
            return False
        for w in ws:
            if w == ENTRY:
                if idx not in VOLATILE:
                    return False
                continue
            p = cfg.insns[w]
            ops = p.operands
            if (p.insn_name() != "mov" or len(ops) != 2 or ops[0].type != X.X86_OP_REG
                    or GPR.get(ops[0].reg) != (64, idx, False)):
                return False
            src = ops[1]
            if src.type == X.X86_OP_MEM and src.size == 8 and src.mem.segment == 0:
                if src.mem.base == X.X86_REG_RIP and not (
                        src.mem.index == 0 and self._in_writable(p.address + p.size + src.mem.disp, 8)):
                    return False
            elif src.type == X.X86_OP_REG and GPR.get(src.reg, (0,))[0] == 64 and not GPR[src.reg][2]:
                if not self._run_time_pointer(fn, cfg, into, w, GPR[src.reg][1], depth - 1):
                    return False
            else:
                return False
        return True

    def _run_time_value(self, fn, cfg, into, addr, idx, depth=4):
        """True if general register number idx holds, whenever control arrives at addr, a
        value that the function got at run time and did not compute.  As _run_time_pointer,
        but the value need not be a 64-bit pointer: every write to the register that
        reaches addr on any path writes all of it (a 32- or 64-bit destination) and is a
        mov, movzx, movsx or movsxd from memory (not from a read-only slot of the image
        addressed through rip), or from a register for which the same holds there; or there
        is none and the register is volatile (the caller's value)."""
        ws = self._reaching_writers(fn, cfg, into, addr, idx)
        if not ws or depth == 0:
            return False
        for w in ws:
            if w == ENTRY:
                if idx not in VOLATILE:
                    return False
                continue
            p = cfg.insns[w]
            ops = p.operands
            if (p.insn_name() not in ("mov", "movzx", "movsx", "movsxd") or len(ops) != 2
                    or ops[0].type != X.X86_OP_REG or ops[0].reg not in GPR or GPR[ops[0].reg][1] != idx
                    or GPR[ops[0].reg][0] < 32):
                return False
            src = ops[1]
            if src.type == X.X86_OP_MEM and src.mem.segment == 0:
                if src.mem.base == X.X86_REG_RIP and not (
                        src.mem.index == 0 and self._in_writable(p.address + p.size + src.mem.disp, src.size)):
                    return False
            elif src.type == X.X86_OP_REG and src.reg in GPR and not GPR[src.reg][2]:
                if not self._run_time_value(fn, cfg, into, w, GPR[src.reg][1], depth - 1):
                    return False
            else:
                return False
        return True

    def _stack_at_entry(self, fn, cfg, into, jmp):
        """True if, on every path from the function's entry to the instruction at `jmp`, the
        stack pointer there is what it was at entry (the caller's return address on top) and
        every callee-saved general register holds what it held at entry.

        Followed forward from the entry over the instructions from which the jump can be
        reached (every path to it lies among them), one state per instruction: the stack
        pointer's offset from its entry value, the callee-saved registers that have been
        written, and the stack slots known to hold a callee-saved register's entry value.
        Where two paths meet, their offsets must be equal; a register is written if it is on
        either path, and a slot is known only if it is on both.  _stack_step moves the state
        across one instruction and refuses what it cannot follow.  Nothing here is assumed
        about what the function is for: a frame the compiler set up and took down again
        passes, and so does a function that never touches the stack."""
        back, todo = set(), [jmp]
        while todo:
            a = todo.pop()
            if a in back:
                continue
            back.add(a)
            edges = into.get(a, ())
            if (a != fn.entry and not edges) or len(back) > STACK_PATH_CAP:
                return False                # decoded, and nothing is known to lead here
            todo.extend(p for p, _ in edges)
        if fn.entry not in back:
            return False
        succ = {}
        for a in back:
            for p, _ in into.get(a, ()):
                succ.setdefault(p, []).append(a)
        state = {fn.entry: (0, frozenset(), frozenset())}
        todo = [fn.entry]
        while todo:
            a = todo.pop()
            if a == jmp:
                continue
            out = self._stack_step(cfg.insns[a], *state[a])
            if out is None:
                return False
            for s in succ.get(a, ()):
                old = state.get(s)
                if old is None:
                    new = out
                elif old[0] != out[0]:
                    return False            # two paths, two stack levels
                else:
                    new = (old[0], old[1] | out[1], old[2] & out[2])
                if new != old:
                    state[s] = new
                    todo.append(s)
        at = state.get(jmp)
        return at is not None and at[0] == 0 and not at[1]

    @staticmethod
    def _stack_step(ins, off, changed, slots):
        """The state of _stack_at_entry after `ins`, from the state before it: (the stack
        pointer's offset from its entry value, the callee-saved registers written and not
        restored, the slots (offset from the entry stack pointer, register) that hold that
        register's entry value), or None if the instruction cannot be followed.

            push x (64-bit)          8 down; pushing a callee-saved register that still has its
                                     entry value makes the slot hold it
            pop r64                  8 up; a callee-saved register is restored if the slot
                                     popped holds its entry value, otherwise it counts as written
            add/sub rsp, imm
            lea rsp, [rsp + d]
            call                     balanced: the callee pops its return address, and every call
                                     site of the lifted code checks at run time that it came back
                                     with the stack pointer where it was.  The 32 bytes at rsp are
                                     the callee's to write (its home area).
            a write to [rsp + d]     must not touch the return address (offsets 0 to 7 from
                                     the entry stack pointer); it ends what was known of the
                                     slots it overlaps
            mov [rsp + d], r64       ... and if r64 is a callee-saved register that still has
                                     its entry value, the slot holds it from here on (a save
                                     in the frame or in the home area above the return address)
            mov r64, [rsp + d]       restores a callee-saved register if that slot holds its
                                     entry value
            a write to a callee-saved register by anything else: written

        Refused: any other write to rsp or part of it (leave, enter, mov, and, xchg, pushfq, a
        16-bit push or pop, pop into memory or into rsp), a write through rsp with an index or
        with rsp as the index, an instruction whose registers capstone cannot tell, and any
        write to xmm6-xmm15 (callee-saved; no function of the class has one, so restoring them
        is not followed).  Slots below the stack pointer are forgotten: they are nobody's."""
        name, ops = ins.insn_name(), ins.operands
        try:
            _, written = ins.regs_access()
        except capstone.CsError:
            return None
        if any(r in SAVED_VECTORS for r in written):
            return None
        widx = {GPR[r][1] for r in written if r in GPR}
        if name == "call":
            return off, changed, frozenset(s for s in slots if s[0] >= off + 0x20)
        if name == "push":
            if len(ops) != 1 or ops[0].size != 8:
                return None
            off -= 8
            if ops[0].type == X.X86_OP_REG and GPR.get(ops[0].reg, (0, -1))[1] in CALLEE_SAVED:
                idx = GPR[ops[0].reg][1]
                if idx not in changed:
                    slots = slots | {(off, idx)}
            return off, changed, slots
        if name == "pop":
            if len(ops) != 1 or ops[0].type != X.X86_OP_REG or GPR.get(ops[0].reg, (0,))[0] != 64:
                return None
            idx = GPR[ops[0].reg][1]
            if idx == 4:
                return None
            if idx in CALLEE_SAVED:
                changed = changed - {idx} if (off, idx) in slots else changed | {idx}
            off += 8
            return off, changed, frozenset(s for s in slots if s[0] >= off)
        if ops and ops[0].type == X.X86_OP_REG and ops[0].reg == X.X86_REG_RSP:
            if name in ("add", "sub") and len(ops) == 2 and ops[1].type == X.X86_OP_IMM:
                off += ops[1].imm if name == "add" else -ops[1].imm
            elif (name == "lea" and len(ops) == 2 and ops[1].mem.base == X.X86_REG_RSP and ops[1].mem.index == 0
                    and ops[1].mem.segment == 0):
                off += ops[1].mem.disp
            else:
                return None
            return off, changed, frozenset(s for s in slots if s[0] >= off)
        if 4 in widx:
            return None
        for o in ops:
            if o.type != X.X86_OP_MEM or name == "lea":
                continue
            through = [GPR[r][1] for r in (o.mem.base, o.mem.index) if r in GPR]
            if 4 not in through or o.access == capstone.CS_AC_READ:
                continue
            if o.mem.base != X.X86_REG_RSP or o.mem.index != 0 or o.mem.segment != 0:
                return None
            lo, hi = off + o.mem.disp, off + o.mem.disp + o.size
            if lo < 8 and hi > 0:
                return None                 # the return address
            slots = frozenset(s for s in slots if not (s[0] < hi and lo < s[0] + 8))
        at_rsp = [o.type == X.X86_OP_MEM and o.size == 8 and o.mem.base == X.X86_REG_RSP and o.mem.index == 0
                  and o.mem.segment == 0 for o in ops]
        whole = [o.type == X.X86_OP_REG and GPR.get(o.reg, (0,))[0] == 64 for o in ops]
        if name == "mov" and len(ops) == 2 and at_rsp[0] and whole[1]:
            idx = GPR[ops[1].reg][1]            # a save in the frame or in the home area
            if idx in CALLEE_SAVED and idx not in changed:
                slots = slots | {(off + ops[0].mem.disp, idx)}
        saved = widx & set(CALLEE_SAVED)
        if saved:
            if (name == "mov" and len(ops) == 2 and whole[0] and at_rsp[1]
                    and (off + ops[1].mem.disp, GPR[ops[0].reg][1]) in slots):
                changed = changed - saved       # loaded back from the slot that holds its entry value
            else:
                changed = changed | saved
        return off, changed, slots
