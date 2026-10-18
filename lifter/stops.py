"""A stop inside a lifted function: an instruction the decoder decodes and the lifter has no
translation for (docs/DESIGN.md section 3, "An instruction without a translation").

One such instruction used to make the lifter skip the whole function it is in, and with it
every path of the function that never executes the instruction: a routine that tests a CPU
feature and uses the extension only when the feature is there was lost with its plain path
(docs/findings/38-unlifted-reach.md).  Now the function is lifted and the instruction
becomes a stop: the lifted code halts the run there with a report (harness/runtime.h,
UNTRANSLATED).  Nothing is approximated.  Everything before the stop runs exactly, the
instruction itself does not run, and after a stop nothing runs.

When a refused instruction is a stop, and when the function is still skipped (`judge`):

  1. the refusal is of kind 'unsupported'.  A relocation the lifter cannot place ('reloc')
     says the bytes are not what the loaded image holds, and an unresolved indirect jump
     ('indirect-jump') leaves the function's shape unknown: both stay skips;
  2. recursive descent took the instruction as one that falls through (discover._descend):
     it gave it no control-flow verdict and went on at the next address.  A call, a jump, a
     conditional branch, a return or a trap that the lifter refuses is not a stop;
  3. the decoder agrees that it is no transfer of control and is something user-mode code
     executes: none of its groups is jump, call, ret, int, iret, branch-relative, privileged,
     VM, RTM or SGX, and its name is not one of NOT_STRAIGHT, the instructions the decoder
     leaves without such a group although they end a path, leave the straight line, or are
     what bytes that are not code decode to.

The length of the instruction is the decoder's, as for every instruction the lifter does
translate.  The shape of the function does not depend on what the instruction would have
computed: the lifter never shapes a function by a value, only by operand forms and by the
decoder's lists of the registers each instruction writes (the jump analyses of discover.py).
Those lists are read for a stop as for any instruction, and whatever they say about it can
only matter on a path through it, which ends at it.  A jump whose register the stop is the
last to write stays unresolved and the function stays skipped, as before.

What a stop means is the other half (`needs`): for each stop the lifter names the CPUID
feature bits that a processor must report in order to execute the instruction, and the
runtime decides at the stop, from its own CPUID answers, whether a processor answering like
it would have raised an invalid-opcode fault there or would have executed the instruction
(runtime/core/trap.c, rt_trap_untranslated).  The lifter holds no copy of those answers.
"""

from capstone import x86_const as X

from .discover import JCC, NOT_USER_CODE, TRAPS
from .translate import GPR, NEAR_BRANCHES, legacy_prefixes, mnemonic_of

# ---- when ---------------------------------------------------------------------------------------

# Decoder groups of instructions that are not a straight-line instruction of user-mode code.
NOT_STRAIGHT_GROUPS = frozenset((X.X86_GRP_JUMP, X.X86_GRP_CALL, X.X86_GRP_RET, X.X86_GRP_INT, X.X86_GRP_IRET,
                                 X.X86_GRP_BRANCH_RELATIVE, X.X86_GRP_PRIVILEGE, X.X86_GRP_VM, X.X86_GRP_RTM,
                                 X.X86_GRP_SGX))

# The same by name, for what Capstone 5.0.7 leaves without one of those groups (checked on the
# encodings in tests/vectors, STOP_REFUSED) and as a second fence around the ones it has:
#   ud0 ud1          always raise #UD: the path ends there, the bytes after are not reached
#   enclu encls enclv getsec       enter or leave an enclave or a measured environment
#   xbegin xabort xend             a transaction: xbegin has a second way out, xabort takes it
#   syscall sysenter sysexit sysret, int, int1, into, the far and the near branches, loops
#   in out ins outs hlt cli sti iret retf rdmsr wrmsr (discover.NOT_USER_CODE) and friends:
#                    privileged or I/O; in a user-mode module they are data read as code
NOT_STRAIGHT = frozenset(NEAR_BRANCHES) | JCC | frozenset(TRAPS) | NOT_USER_CODE | frozenset((
    "loop", "loope", "loopne", "ud0", "ud1", "ud2b", "enclu", "encls", "enclv", "getsec", "xbegin", "xabort", "xend",
    "syscall", "sysenter", "sysexit", "sysret", "sysretq", "sysexitq", "int", "int1", "into", "icebp", "rsm",
    "clts", "invd", "wbinvd", "lgdt", "lidt", "lldt", "ltr", "lmsw", "swapgs", "vmfunc", "uiret", "rdpmc", "xsetbv"))


def judge(ins, refusal, flow, fall):
    """The stop that the refused instruction `ins` becomes, as (insn, what, needs), or None if
    the function it is in must be skipped.  `refusal` is the Unsupported the translator raised,
    `flow` and `fall` what recursive descent recorded for the instruction (CFG.flow, CFG.fall).

    insn:  the lifter's name for the instruction (translate.mnemonic_of);
    what:  the lifter's words for why it has no translation, as in the reasons of a skipped
           function, always with that name in them;
    needs: the CPUID feature bits that gate it, for the report (`needs` below)."""
    if refusal.kind != "unsupported" or flow is not None or fall is None:
        return None
    if NOT_STRAIGHT_GROUPS.intersection(ins.groups):
        return None
    name = mnemonic_of(ins)
    if name in NOT_STRAIGHT or ins.insn_name() in NOT_STRAIGHT:
        return None
    what = refusal.what
    if name not in what.replace(":", " ").replace("(", " ").split():
        what = "%s (%s)" % (name, what)             # 'fs: segment override' does not say of what
    return name, what, needs(ins)


# ---- what it means ------------------------------------------------------------------------------

# A CPUID feature bit by name: (leaf, register, bit), subleaf 0.  From the Intel SDM vol. 2A,
# CPUID, tables 3-8 and 3-10 (leaf 7 and leaf 1), and for leaf 0x80000001 from AMD's CPUID
# specification; which of them were read again on 2026-10-10 in the copy of the SDM at
# felixcloutier.com/x86, and which are from memory, is in docs/handoff/lifter-trap-insn.md.
CPUID_BITS = {
    "MMX": (1, "edx", 23), "SSE": (1, "edx", 25), "SSE2": (1, "edx", 26),
    "SSE3": (1, "ecx", 0), "PCLMULQDQ": (1, "ecx", 1), "SSSE3": (1, "ecx", 9), "FMA": (1, "ecx", 12),
    "SSE4.1": (1, "ecx", 19), "SSE4.2": (1, "ecx", 20), "MOVBE": (1, "ecx", 22), "POPCNT": (1, "ecx", 23),
    "AES": (1, "ecx", 25), "OSXSAVE": (1, "ecx", 27), "AVX": (1, "ecx", 28), "F16C": (1, "ecx", 29),
    "RDRAND": (1, "ecx", 30),
    "BMI1": (7, "ebx", 3), "AVX2": (7, "ebx", 5), "BMI2": (7, "ebx", 8), "AVX512F": (7, "ebx", 16),
    "RDSEED": (7, "ebx", 18), "ADX": (7, "ebx", 19), "SHA": (7, "ebx", 29),
    "LAHF-SAHF": (0x80000001, "ecx", 0),
    "SSE4A": (0x80000001, "ecx", 6), "XOP": (0x80000001, "ecx", 11), "FMA4": (0x80000001, "ecx", 16),
    "TBM": (0x80000001, "ecx", 21), "3DNow": (0x80000001, "edx", 31),
}

# Which features an instruction waits for.  First by name, for what the decoder puts in no
# group or in the wrong one; then by the decoder's groups.  An instruction that matches
# nothing here gets no feature, and the report then calls its stop the lifter's gap: a claim
# that a real processor would fault is made only for what is listed.
#
#   xgetbv   #UD if CPUID.01H:ECX.XSAVE = 0 or CR4.OSXSAVE = 0 (SDM, XGETBV); CPUID.01H:ECX
#            bit 27 is CR4.OSXSAVE as the OS set it (SDM, CPUID, table 3-10)
#   movbe    #UD if CPUID.01H:ECX.MOVBE = 0 (SDM, MOVBE)
#   lahf, sahf   in 64-bit mode #UD if CPUID.80000001H:ECX.LAHF-SAHF = 0 (SDM, LAHF)
#   tzcnt, lzcnt   no feature on purpose: a processor without BMI1 / LZCNT executes the same
#            bytes as bsf / bsr (the F3 prefix is ignored), so there is no fault to stand for
BY_NAME = {"xgetbv": ("OSXSAVE",), "movbe": ("MOVBE",), "popcnt": ("POPCNT",), "crc32": ("SSE4.2",),
           "rdrand": ("RDRAND",), "rdseed": ("RDSEED",), "lahf": ("LAHF-SAHF",), "sahf": ("LAHF-SAHF",),
           "tzcnt": (), "lzcnt": ()}
BY_GROUP = ((X.X86_GRP_AVX512, "AVX512F"), (X.X86_GRP_AVX2, "AVX2"), (X.X86_GRP_AVX, "AVX"), (X.X86_GRP_FMA, "FMA"),
            (X.X86_GRP_F16C, "F16C"), (X.X86_GRP_AES, "AES"), (X.X86_GRP_PCLMUL, "PCLMULQDQ"), (X.X86_GRP_SHA, "SHA"),
            (X.X86_GRP_BMI, "BMI1"), (X.X86_GRP_BMI2, "BMI2"), (X.X86_GRP_ADX, "ADX"), (X.X86_GRP_XOP, "XOP"),
            (X.X86_GRP_FMA4, "FMA4"), (X.X86_GRP_TBM, "TBM"), (X.X86_GRP_SSE4A, "SSE4A"), (X.X86_GRP_3DNOW, "3DNow"),
            (X.X86_GRP_SSE42, "SSE4.2"), (X.X86_GRP_SSE41, "SSE4.1"), (X.X86_GRP_SSSE3, "SSSE3"),
            (X.X86_GRP_SSE3, "SSE3"), (X.X86_GRP_SSE2, "SSE2"), (X.X86_GRP_SSE1, "SSE"), (X.X86_GRP_MMX, "MMX"))


def features(ins):
    """Names (keys of CPUID_BITS) of the features the instruction waits for; () if none is known."""
    name = mnemonic_of(ins)
    if name in BY_NAME:
        return BY_NAME[name]
    groups = set(ins.groups)
    found = tuple(f for g, f in BY_GROUP if g in groups)
    if not found and _vex_vector(ins):
        # The decoder gives some VEX-encoded vector instructions no group at all (vpalignr in
        # Capstone 5.0.7).  Whatever extension such an instruction belongs to (AVX, AVX2, FMA,
        # F16C), a processor without AVX has none of them: AVX at least.
        return ("AVX",)
    return found


def _vex_vector(ins):
    """True for an instruction with a VEX prefix (C4 or C5 after the legacy prefixes: in 64-bit
    mode those bytes are nothing else) and a register operand that is not a general register."""
    raw = bytes(ins.bytes)
    i = len(legacy_prefixes(raw))
    return (i < len(raw) and raw[i] in (0xC4, 0xC5)
            and any(op.type == X.X86_OP_REG and op.reg not in GPR for op in ins.operands))


def needs(ins):
    """The same as the text the lifted code hands to the report: 'NAME=LEAF.REG.BIT' items
    separated by spaces (LEAF in hex), or '' (harness/runtime.h, UNTRANSLATED)."""
    return " ".join("%s=%x.%s.%d" % ((f,) + CPUID_BITS[f]) for f in features(ins))


def c_statement(rva, what, need):
    return "UNTRANSLATED(0x%x, %s, %s);" % (rva, _c_string(what), _c_string(need))


def _c_string(text):
    return '"%s"' % "".join(c if 32 <= ord(c) < 127 and c not in '"\\?' else "\\%03o" % (ord(c) & 0xFF) for c in text)
