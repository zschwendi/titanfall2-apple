"""The core of the instruction translator: one Capstone instruction -> a list of C statements.

The translators themselves live in families, one module per family under lifter/insns/
(see lifter/insns/README.md and docs/DESIGN.md, "Adding an instruction family").  This file
holds what every family shares and what no family may switch off:

  * the exceptions, prefix parsing, register tables, `Encoding` and `shape`;
  * the lifter's name for an instruction (`mnemonic_of`);
  * the registry the families fill (`Family`, `enc`; the tables HANDLERS, OPCODES, VECTOR_OK);
  * the `Translator`: per-instruction state, operand helpers, and the checks that run for
    every instruction of every family before and after its translator (`translate`,
    `_check_encoding`): the name-clash guard, prefix agreement, the atomicity assertion.

Rules (docs/DESIGN.md):
  * Never approximate.  Anything not handled exactly raises Unsupported and the whole
    function is skipped.
  * All source values are read into temporaries before anything is written.
  * Every flag an instruction defines is computed eagerly (harness/runtime.h and
    harness/insns/<family>.h helpers).

The statements for one instruction are wrapped in their own { } block by the emitter, so
temporaries may reuse short names.
"""

import bisect
import os

from capstone import x86_const as X

M64 = (1 << 64) - 1
HARNESS_INSNS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "harness", "insns")


class Unsupported(Exception):
    """kind: 'unsupported' (mnemonic or operand form), 'indirect-jump', 'reloc'."""

    def __init__(self, what, kind="unsupported"):
        Exception.__init__(self, what)
        self.what = what
        self.kind = kind


class LifterBug(AssertionError):
    """The lifter broke one of its own invariants.  Deliberately not an Unsupported: nothing
    catches it, so the lift stops instead of writing C that is known to be wrong."""


LEGACY_PREFIXES = (0xF0, 0xF2, 0xF3, 0x2E, 0x36, 0x3E, 0x26, 0x64, 0x65, 0x66, 0x67)
STRING_OPCODES = (0xA4, 0xA5, 0xA6, 0xA7, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF)


def legacy_prefixes(raw):
    """The legacy prefix bytes at the start of an instruction's encoding, in order."""
    out = []
    for b in raw:
        if b not in LEGACY_PREFIXES:
            break
        out.append(b)
    return out


def _ids(names):
    return [getattr(X, "X86_REG_" + n) for n in names.split()]


GPR = {}   # capstone reg id -> (bits, index, is_high_byte)
for _i, _r in enumerate(_ids("RAX RCX RDX RBX RSP RBP RSI RDI R8 R9 R10 R11 R12 R13 R14 R15")):
    GPR[_r] = (64, _i, False)
for _i, _r in enumerate(_ids("EAX ECX EDX EBX ESP EBP ESI EDI R8D R9D R10D R11D R12D R13D R14D R15D")):
    GPR[_r] = (32, _i, False)
for _i, _r in enumerate(_ids("AX CX DX BX SP BP SI DI R8W R9W R10W R11W R12W R13W R14W R15W")):
    GPR[_r] = (16, _i, False)
for _i, _r in enumerate(_ids("AL CL DL BL SPL BPL SIL DIL R8B R9B R10B R11B R12B R13B R14B R15B")):
    GPR[_r] = (8, _i, False)
for _i, _r in enumerate(_ids("AH CH DH BH")):
    GPR[_r] = (8, _i, True)
XMM = {getattr(X, "X86_REG_XMM%d" % i): i for i in range(16)}

CC = {"o": "CC_O", "no": "CC_NO", "b": "CC_B", "ae": "CC_AE", "e": "CC_E", "ne": "CC_NE",
      "be": "CC_BE", "a": "CC_A", "s": "CC_S", "ns": "CC_NS", "p": "CC_P", "np": "CC_NP",
      "l": "CC_L", "ge": "CC_GE", "le": "CC_LE", "g": "CC_G"}

SEQ = "__ATOMIC_SEQ_CST"
ATOMIC = "x_atomic_"        # prefix of the runtime.h helpers every locked operation goes through


def reg_rd(reg):
    bits, i, hi = GPR[reg]
    if bits == 8:
        return "R8H(%d)" % i if hi else "R8(%d)" % i
    return "R%d(%d)" % (bits, i)


def reg_wr(reg, v):
    bits, i, hi = GPR[reg]
    if bits == 8:
        return ("W8H(%d, %s);" if hi else "W8(%d, %s);") % (i, v)
    return "W%d(%d, %s);" % (bits, i, v)


def lit(v, bits):
    v &= (1 << bits) - 1
    if bits == 64:
        return "UINT64_C(0x%x)" % v
    if bits == 32:
        return "UINT32_C(0x%x)" % v
    return "(uint%d_t)0x%x" % (bits, v)


def T(bits):
    return "uint%d_t" % bits


_STRING_KIND = {0xA4: "movs", 0xA5: "movs", 0xA6: "cmps", 0xA7: "cmps", 0xAA: "stos", 0xAB: "stos",
                0xAC: "lods", 0xAD: "lods", 0xAE: "scas", 0xAF: "scas"}


def mnemonic_of(ins):
    """The lifter's name for an instruction: the name its translator is registered under, the
    name in operand shapes, in the instruction index and in 'unsupported' reports.

    It is the mnemonic the decoder prints, without prefixes (lock, rep, repne, bnd, ...).  Not
    the decoder's id name (`ins.insn_name()`): Capstone 5 gives the SSE compares with a
    predicate the ids of unrelated instructions - `cmpleps` is 'cmpsb', `cmpeqps` is 'cmppd',
    `cmpordps` is 'cmpxchg16b' - while the printed mnemonic is right.

    One exception.  The Intel manual itself uses two mnemonics twice: MOVSD and CMPSD are
    string instructions on doublewords (one-byte opcodes A5 and A7) and SSE2 instructions on
    scalar doubles (F2 0F 10/11, F2 0F C2).  A name has to mean one instruction here, because
    a name belongs to exactly one family.  `movsd` and `cmpsd` are the SSE2 instructions; the
    doubleword string instructions are named with an `l`, as in AT&T syntax: movsl, cmpsl,
    stosl, lodsl, scasl.  Which of the two an instruction is comes from its opcode byte."""
    name = ins.mnemonic.split()[-1] if ins.mnemonic else ins.insn_name()
    kind = _STRING_KIND.get(ins.opcode[0])
    if kind is not None and name == kind + "d":
        return kind + "l"
    return name


def mnemonic_of_text(text):
    """The lifter's mnemonic of a line of assembly as the decoder prints it ('lock add ...' is add).
    For reports that only have the text; string instructions on doublewords are not told apart."""
    words = text.split()
    while words and words[0] in ("lock", "rep", "repe", "repne", "repz", "repnz", "bnd", "notrack"):
        words.pop(0)
    return words[0] if words else ""


def shape(ins):
    """Operand-shape signature used to sample instructions for the instruction test."""
    parts = []
    for op in ins.operands:
        if op.type == X.X86_OP_REG:
            if op.reg in GPR:
                bits, _, hi = GPR[op.reg]
                parts.append("r8h" if hi else "r%d" % bits)
            elif op.reg in XMM:
                parts.append("x")
            else:
                parts.append("reg?")
        elif op.type == X.X86_OP_IMM:
            parts.append("i")
        elif op.type == X.X86_OP_MEM:
            s = "m%d" % (op.size * 8)
            if op.mem.base == X.X86_REG_RIP:
                s += "rip"
            if op.mem.segment == X.X86_REG_GS:
                s += "gs"
            parts.append(s)
    raw = legacy_prefixes(bytes(ins.bytes))
    name = mnemonic_of(ins)
    pre = "lock " if 0xF0 in raw else ""
    reps = [b for b in raw if b in (0xF2, 0xF3)]
    if reps and ins.opcode[0] in STRING_OPCODES:
        pre = "rep " if reps[-1] == 0xF3 else "repne "
    return (pre + name + " " + ",".join(parts)).strip()


class Encoding:
    """What the bytes of one instruction say, read without the decoder: legacy prefixes, REX,
    the opcode and the /reg field of ModRM.

    key: the opcode as one number - 0xNN for the one-byte map, 0x0FNN for the two-byte map,
         0x0F38NN / 0x0F3ANN for the three-byte maps.
    mandatory: the prefix that selects an SSE variant - the last F2/F3 if there is one,
         else 0x66 if present, else 0."""

    def __init__(self, ins):
        raw = bytes(ins.bytes)
        self.prefixes = legacy_prefixes(raw)
        i = len(self.prefixes)
        self.rex = raw[i] if i < len(raw) and 0x40 <= raw[i] <= 0x4F else 0
        i += 1 if self.rex else 0
        cap = list(ins.opcode)
        if i >= len(raw) or raw[i] != cap[0] or self.rex != ins.rex:
            # VEX/EVEX/XOP, a REX prefix that is not last, or anything else read differently
            # here and by the decoder.
            raise Unsupported("%s: encoding the lifter does not parse (%s)" % (ins.mnemonic, raw.hex()))
        if raw[i] != 0x0F:
            self.key = raw[i]
        elif i + 1 < len(raw) and raw[i + 1] in (0x38, 0x3A):
            self.key = 0x0F0000 | raw[i + 1] << 8 | (raw[i + 2] if i + 2 < len(raw) else 0)
        else:
            self.key = 0x0F00 | (raw[i + 1] if i + 1 < len(raw) else 0)
            if cap[1] != self.key & 0xFF:
                raise Unsupported("%s: encoding the lifter does not parse (%s)" % (ins.mnemonic, raw.hex()))
        self.reg = (ins.modrm >> 3) & 7
        reps = [b for b in self.prefixes if b in (0xF2, 0xF3)]
        self.mandatory = reps[-1] if reps else 0x66 if 0x66 in self.prefixes else 0

    def key_text(self):
        return "%x" % self.key


# ---- the registry the families fill ---------------------------------------------------------------
#
# Three tables, keyed by the lifter's mnemonic (mnemonic_of).  They are filled when the family
# modules under lifter/insns/ are imported (at the bottom of this file) and read by
# Translator.translate() and Translator._check_encoding() for every instruction.

HANDLERS = {}       # mnemonic -> translator: f(tr, flow) -> list of C statements
OPCODES = {}        # mnemonic -> the encodings its translator was written for: {(key, /reg, mandatory prefix)}
VECTOR_OK = set()   # mnemonics whose translator handles xmm (and other non-general) register operands
FAMILY_OF = {}      # mnemonic -> name of the family that registered it
FAMILIES = {}       # family name -> Family
FLOAT = {}          # mnemonic -> (lane type "f32"|"f64", packed?) for the mnemonics declared float (fp=...)

# A legacy SSE instruction raises #GP when its 16-byte memory operand is not 16-byte aligned,
# except these (Intel SDM vol. 2, section 2.4, exception class type 4, and the instructions'
# own pages).  Translator.xval and Translator.xstore emit the fault for every other mnemonic;
# the harness has its own copy of the rule (harness/oraclefix.py) and the real CPU's answers.
UNALIGNED_M128 = frozenset(("movups", "movupd", "movdqu", "lddqu", "pcmpestri", "pcmpestrm", "pcmpistri", "pcmpistrm"))

# Capstone 5.0.7 reports the memory operand of these as 16 bytes ("xmmword ptr") where the manual
# says m64: the instruction reads 8 bytes and has no alignment rule.  Of some ninety legacy SSE
# encodings with a memory operand compared with the manual (2026-10-09) comisd is the only one;
# ucomisd, comiss and ucomiss are reported right.  Translator.mem_size is the corrected size.
DECODER_M64 = frozenset(("comisd",))

# What a family may write as fp=...: the lane type and whether the instruction is scalar or packed,
# nothing else.  The harness derives the rest from it (harness/floatcase.py): float-aware operand
# values per lane, the MXCSR variants, the real CPU as the expected side.
FP_KINDS = {"f32 scalar": ("f32", False), "f32 packed": ("f32", True),
            "f64 scalar": ("f64", False), "f64 packed": ("f64", True)}


class RegistryError(Exception):
    """A family module registers something it may not.  Raised while the families are being
    imported, so nothing can be lifted or tested until it is fixed."""


def enc(*keys, reg=None, mand=None):
    """The encodings a translator accepts: opcode keys (see Encoding.key: 0xNN, 0x0FNN,
    0x0F38NN, 0x0F3ANN), optionally only with one /reg value of ModRM and one mandatory
    prefix (0x66, 0xF2, 0xF3, or 0 for 'none of them').  Combine several with `|`.

    The decoder names instructions, and unrelated instructions can share a name: far
    'call'/'jmp' through memory are plain 'call'/'jmp', 'movq' and 'pxor' are also MMX
    instructions, 0F BC is 'bsf' without F3 and 'tzcnt' with it.  A translator is therefore
    reached only when the opcode bytes are ones it registered; everything else is
    Unsupported, whatever it is called."""
    return {(k, reg, mand) for k in keys}


class Family:
    """One instruction family: the translators in lifter/insns/<name>.py, their C helpers in
    harness/insns/<name>.h (if it needs any) and their test vectors in tests/vectors/<name>.py.

        F = Family(__name__)                          # no C helpers
        F = Family(__name__, header="sse_shuffle.h")  # harness/insns/sse_shuffle.h

        @F.insn("pshufd", enc(0x0F70, mand=0x66), vector=True)
        def t_pshufd(tr, flow):
            ...
            return ["C statement;", ...]

    `vector=True` says the translator handles operands that are not general registers; without
    it such an instruction never reaches the translator.  One function can serve several
    mnemonics: stack the decorators, or call F.register() in a loop."""

    def __init__(self, module, header=None):
        name = module.rsplit(".", 1)[-1]
        if name in FAMILIES:
            raise RegistryError("family %r is defined twice" % name)
        path = os.path.join(HARNESS_INSNS, name + ".h")
        if header is not None and header != name + ".h":
            raise RegistryError("family %r: its C header must be harness/insns/%s.h, not %r" % (name, name, header))
        if header is not None and not os.path.exists(path):
            raise RegistryError("family %r names the C header %s, which does not exist" % (name, header))
        if header is None and os.path.exists(path):
            raise RegistryError("harness/insns/%s.h exists, but family %r does not name it: write "
                                "Family(__name__, header=\"%s.h\")" % (name, name, name))
        self.name = name
        self.header = header
        self.mnemonics = []
        FAMILIES[name] = self

    def register(self, mnemonic, handler, encodings, vector=False, fp=None):
        """Register `handler` as the translator of `mnemonic` for `encodings` (from enc()).
        `fp` declares a float instruction, one whose result depends on rounding, NaN rules or
        MXCSR: "f32 scalar", "f32 packed", "f64 scalar" or "f64 packed"."""
        if mnemonic in FAMILY_OF:
            other = FAMILY_OF[mnemonic]
            if other == self.name:
                raise RegistryError("family %r registers the mnemonic %r twice" % (self.name, mnemonic))
            raise RegistryError("the mnemonic %r is registered by two families: %r and %r (lifter/insns/%s.py "
                                "and lifter/insns/%s.py)" % (mnemonic, other, self.name, other, self.name))
        if not callable(handler):
            raise RegistryError("family %r, %r: the translator is not callable" % (self.name, mnemonic))
        encodings = set(encodings)
        if not encodings or not all(isinstance(e, tuple) and len(e) == 3 for e in encodings):
            raise RegistryError("family %r, %r: give the accepted encodings with enc(...)" % (self.name, mnemonic))
        if fp is not None:
            if fp not in FP_KINDS:
                raise RegistryError("family %r, %r: fp=%r is not one of %s" % (self.name, mnemonic, fp, ", ".join(
                    repr(k) for k in sorted(FP_KINDS))))
            if not vector:
                raise RegistryError("family %r, %r: a float mnemonic takes xmm operands, so it needs vector=True"
                                    % (self.name, mnemonic))
            FLOAT[mnemonic] = FP_KINDS[fp]
        HANDLERS[mnemonic] = handler
        OPCODES[mnemonic] = encodings
        if vector:
            VECTOR_OK.add(mnemonic)
        FAMILY_OF[mnemonic] = self.name
        self.mnemonics.append(mnemonic)

    def insn(self, mnemonic, encodings, vector=False, fp=None):
        """Decorator form of register()."""
        def deco(handler):
            self.register(mnemonic, handler, encodings, vector, fp)
            return handler
        return deco


def c_includes():
    """The #include lines every generated C file starts with: runtime.h, then the header of
    every family that has one, in name order.  Nobody maintains this list; it is derived from
    the families that were found."""
    return "".join(["#include \"runtime.h\"\n"] + ["#include \"insns/%s\"\n" % FAMILIES[n].header
                                                    for n in sorted(FAMILIES) if FAMILIES[n].header])


NEAR_BRANCHES = frozenset(["call", "jmp", "ret", "jrcxz", "jecxz"] + ["j" + c for c in CC])


class Translator:
    def __init__(self, image, modname):
        self.image = image
        self.mod = modname
        self.reloc = image.reloc_sites

    # ---- per-instruction state ----------------------------------------------

    def translate(self, ins, flow=None):
        """Return the C statements for `ins`.  `flow` is the CFG's verdict for control
        transfers (see discover.build_cfg)."""
        self.ins = ins
        self.ops = ins.operands
        self.pre = []
        self.eavars = {}
        self.name = name = mnemonic_of(ins)
        # LOCK and REP come from the prefix bytes, not from the decoder (review finding 6):
        # Capstone reports at most one of F0/F2/F3 and drops the other ('f0 f3 ff 03' comes
        # back as a plain inc), and reports no prefix at all for 'f2 a5'.
        raw = legacy_prefixes(bytes(ins.bytes))
        reps = [b for b in raw if b in (0xF2, 0xF3)]
        self.lock = 0xF0 in raw
        self.lock_ok = False
        self.rep = reps[-1] if reps else 0
        self.reloc_field = self._reloc_field()
        # Decided from the bytes, before any handler runs and independently of what the
        # decoder or a handler thinks (review finding 12): this instruction is atomic on x86.
        must_be_atomic = self.lock or (
            name == "xchg" and any(op.type == X.X86_OP_MEM for op in self.ops))
        h = HANDLERS.get(name)              # the translator some family registered under this name
        if h is None:
            raise Unsupported(("lock " if self.lock else "") + name)
        self.enc = self._check_encoding()
        body = h(self, flow)
        if self.lock and not self.lock_ok:
            raise Unsupported("lock " + name)
        # Atomicity cannot be seen by a single-threaded test: a locked instruction translated
        # as a plain read-modify-write passes every case.  So it is checked here instead.
        if must_be_atomic and not any(ATOMIC in line for line in body):
            raise LifterBug("0x%x: %s %s is atomic on x86 but its translation contains no %s* operation"
                            % (ins.address, ins.mnemonic, ins.op_str, ATOMIC))
        return self.pre + body

    def _check_encoding(self):
        """The name-clash guard: only let an instruction reach the translator registered for
        its name if its bytes are an encoding that translator handles (OPCODES), and only
        let an instruction with a vector or other non-general register operand reach a
        translator that handles such operands (VECTOR_OK).  Applies to every family."""
        ins, name = self.ins, self.name
        enc = Encoding(ins)
        ok = OPCODES.get(name, ())
        hits = [m for k, r, m in ok if k == enc.key and r in (None, enc.reg) and m in (None, enc.mandatory)]
        if not hits:
            raise Unsupported("%s (opcode %s /%d is not a form the lifter translates)" % (name, enc.key_text(), enc.reg))
        # The prefix bytes and the decoder must tell the same story, or the instruction is
        # not translated (review finding 6).
        p0 = ins.prefix[0]
        reps = [b for b in enc.prefixes if b in (0xF2, 0xF3)]
        if self.lock != (p0 == 0xF0):
            raise Unsupported("%s: LOCK prefix that the decoder does not report" % name)
        if self.lock and reps:
            raise Unsupported("%s: LOCK together with an F2/F3 prefix" % name)
        if len(set(reps)) > 1:
            raise Unsupported("%s: both F2 and F3 prefixes" % name)
        # An F2/F3 the decoder does not report is fine only where it is the mandatory prefix
        # that the opcode table demanded (SSE scalar forms, pause).
        if self.rep != (p0 if p0 in (0xF2, 0xF3) else 0) and self.rep not in hits:
            raise Unsupported("%s: F2/F3 prefix that the decoder does not report" % name)
        # A near branch with an operand-size prefix: Intel CPUs ignore the prefix, AMD CPUs
        # truncate the target to 16 bits (and the oracle follows AMD).  Not one behaviour, so
        # not translated (review finding 13).
        if 0x66 in enc.prefixes and name in NEAR_BRANCHES:
            raise Unsupported("%s with a 0x66 prefix" % name)
        if name not in VECTOR_OK and any(op.type == X.X86_OP_REG and op.reg not in GPR for op in self.ops):
            raise Unsupported("%s with a vector or other non-general register" % name)
        return enc

    def _reloc_field(self):
        """'imm', 'disp' or None: which 8-byte field of this instruction is a relocated
        absolute address."""
        ins = self.ins
        k = bisect.bisect_left(self.reloc, ins.address)
        # An 8-byte relocation slot that begins before the instruction and reaches into it: some
        # of the instruction's bytes change when the image is loaded, so they must not be
        # translated as they stand.  In code this cannot happen (a relocated field is the last
        # field of its instruction); it means a pointer table was decoded as code.
        if k > 0 and self.reloc[k - 1] + 8 > ins.address:
            raise Unsupported("relocation that begins before %s and covers part of it" % self.name, "reloc")
        if k >= len(self.reloc) or self.reloc[k] >= ins.address + ins.size:
            return None
        off = self.reloc[k] - ins.address
        if k + 1 < len(self.reloc) and self.reloc[k + 1] < ins.address + ins.size:
            raise Unsupported("two relocations in one instruction", "reloc")
        if ins.imm_size == 8 and off == ins.imm_offset:
            return "imm"
        if ins.disp_size == 8 and off == ins.disp_offset:
            return "disp"
        raise Unsupported("relocation at an unexpected offset inside %s" % self.name, "reloc")

    def _abs(self, v):
        """A relocated absolute address as BASE + rva."""
        return "(BASE + UINT64_C(0x%x))" % ((v - self.image.image_base) & M64)

    # ---- operands -------------------------------------------------------------

    def bits(self, i):
        return self.ops[i].size * 8

    def mem_size(self, i):
        """Bytes the instruction reads or writes at memory operand i: the decoder's size, but
        for the mnemonics of DECODER_M64."""
        return 8 if self.name in DECODER_M64 else self.ops[i].size

    def ea_expr(self, i, noseg=False):
        ins, m = self.ins, self.ops[i].mem
        if m.segment == X.X86_REG_FS and not noseg:
            raise Unsupported("fs: segment override")
        asz = ins.addr_size
        if asz not in (4, 8):
            raise Unsupported("address size %d" % asz)
        if m.base == X.X86_REG_RIP:
            if asz != 8:
                raise Unsupported("eip-relative addressing")
            e = "(BASE + UINT64_C(0x%x))" % ((ins.address + ins.size + m.disp) & M64)
        else:
            terms = []
            for reg, scale in ((m.base, 1), (m.index, m.scale)):
                if reg == 0:
                    continue
                if reg not in GPR or GPR[reg][0] != asz * 8:
                    raise Unsupported("addressing register in %s" % self.name)
                terms.append(reg_rd(reg) if scale == 1 else "%s * %d" % (reg_rd(reg), scale))
            if self.reloc_field == "disp":
                terms.append(self._abs(m.disp & M64))
            elif m.disp != 0 or not terms:
                terms.append(lit(m.disp, asz * 8))
            e = "(%s)" % " + ".join(terms)
            if asz == 4:
                e = "(uint64_t)(uint32_t)%s" % e
        if m.segment == X.X86_REG_GS and not noseg:
            e = "(ctx->gs_base + %s)" % e
        return e

    def ea(self, i):
        """Name of a temporary holding operand i's effective address, computed once from
        the register state before the instruction."""
        if i not in self.eavars:
            v = "m%d" % i
            self.pre.append("uint64_t %s = %s;" % (v, self.ea_expr(i)))
            self.eavars[i] = v
        return self.eavars[i]

    def stack_op(self, i):
        """True if operand i is a memory operand [rsp] with no index and no segment override:
        an access to the running thread's own stack (see harness/runtime.h, STS64 and LDS64)."""
        op = self.ops[i]
        return (op.type == X.X86_OP_MEM and op.mem.base == X.X86_REG_RSP
                and op.mem.index == 0 and op.mem.segment == 0)

    def mac(self, i, name):
        """Macro name for a guest access to memory operand i: `name` (LD64, STX, LDF32, ...), or
        its stack variant (LDS64, STSX, LDSF32, ...) when operand i is a stack operand."""
        if self.stack_op(i):
            return name[:2] + "S" + name[2:]
        return name

    def val(self, i, bits):
        """C expression of type uint<bits>_t for operand i."""
        op = self.ops[i]
        if op.type == X.X86_OP_REG:
            if op.reg not in GPR:
                raise Unsupported("%s with a non-general register" % self.name)
            if GPR[op.reg][0] != bits:
                raise Unsupported("%s operand size mix" % self.name)
            return reg_rd(op.reg)
        if op.type == X.X86_OP_IMM:
            if self.reloc_field == "imm":
                if bits != 64:
                    raise Unsupported("relocated immediate that is not 64-bit", "reloc")
                return self._abs(op.imm & M64)
            return lit(op.imm, bits)
        if op.type == X.X86_OP_MEM:
            if op.size * 8 != bits:
                raise Unsupported("%s memory operand size mix" % self.name)
            return "%s(%s)" % (self.mac(i, "LD%d" % bits), self.ea(i))
        raise Unsupported("%s operand type" % self.name)

    def wr(self, i, expr, bits=None):
        op = self.ops[i]
        bits = bits or op.size * 8
        if op.type == X.X86_OP_REG:
            if op.reg not in GPR:
                raise Unsupported("%s with a non-general register" % self.name)
            if GPR[op.reg][0] != bits:
                raise Unsupported("%s operand size mix" % self.name)
            return reg_wr(op.reg, expr)
        if op.type == X.X86_OP_MEM:
            if op.size * 8 != bits:
                raise Unsupported("%s memory operand size mix" % self.name)
            return "%s(%s, %s);" % (self.mac(i, "ST%d" % bits), self.ea(i), expr)
        raise Unsupported("%s destination type" % self.name)

    def is_mem(self, i):
        return self.ops[i].type == X.X86_OP_MEM

    def is_reg(self, i):
        return self.ops[i].type == X.X86_OP_REG

    def need_ops(self, *counts):
        if len(self.ops) not in counts:
            raise Unsupported("%s with %d operands" % (self.name, len(self.ops)))

    def fn_name(self, rva):
        return "%s_%x" % (self.mod, rva)

    # ---- vector operands ---------------------------------------------------------

    def xr(self, i):
        """Number of the xmm register that operand i is, or None."""
        op = self.ops[i]
        if op.type == X.X86_OP_REG:
            if op.reg not in XMM:
                return None
            return XMM[op.reg]
        return None

    def xalign(self, i):
        """The alignment fault of operand i, if it is a 16-byte memory operand that x86 requires
        to be 16-byte aligned (every legacy SSE instruction but UNALIGNED_M128): #GP before
        anything is read or written, so a trap at this instruction ahead of the translator's
        own statements.  Nothing for a register or a smaller operand."""
        if self.is_mem(i) and self.mem_size(i) == 16 and self.name not in UNALIGNED_M128:
            chk = "if (%s & 15) TRAP(0x%x);" % (self.ea(i), self.ins.address)
            if chk not in self.pre:
                self.pre.append(chk)

    def xval(self, i):
        """C expression of type Xmm for operand i: an xmm register or 16 bytes of memory (with
        the alignment fault of xalign)."""
        k = self.xr(i)
        if k is not None:
            return "ctx->xmm[%d]" % k
        if self.is_mem(i):
            if self.mem_size(i) != 16:               # LDX reads 16 bytes: beyond the operand
                raise Unsupported("%s: a %d-byte memory operand read as 16 bytes" % (self.name, self.mem_size(i)))
            self.xalign(i)
            return "%s(%s)" % (self.mac(i, "LDX"), self.ea(i))
        raise Unsupported(self.name + " operand")

    def xstore(self, i, expr):
        """C statement storing the Xmm expression into the 16-byte memory operand i (with the
        alignment fault of xalign)."""
        if not self.is_mem(i) or self.ops[i].size != 16:
            raise Unsupported(self.name + " destination")
        self.xalign(i)
        return "%s(%s, %s);" % (self.mac(i, "STX"), self.ea(i), expr)

    def fval(self, i, bits):
        """C expression of type float (bits = 32) or double (64) for a scalar float operand:
        lane 0 of an xmm register, or a memory operand of exactly that size."""
        k = self.xr(i)
        if k is not None:
            return "ctx->xmm[%d].%s[0]" % (k, "f" if bits == 32 else "pd")
        if self.is_mem(i):
            if self.mem_size(i) * 8 != bits:
                raise Unsupported("%s memory operand size mix" % self.name)
            return "%s(%s)" % (self.mac(i, "LDF%d" % bits), self.ea(i))
        raise Unsupported(self.name + " operand")


# ---- the families ------------------------------------------------------------------------------------
# Imported last: every module under lifter/insns/ imports this one and fills the tables above.

from . import insns as _insns            # noqa: E402

_insns.discover()
assert set(HANDLERS) == set(OPCODES) == set(FAMILY_OF), sorted(set(HANDLERS) ^ set(OPCODES))
