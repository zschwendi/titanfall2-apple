"""System and special instructions: the traps (int3, ud2, hlt, int 0x29), cpuid, rdtsc, the flag
operations clc/stc/cmc/cld/std, and the instructions that do nothing to guest state (nop,
pause, the prefetches, lfence, sfence) or only order memory accesses (mfence).

cpuid and rdtsc call into the runtime (rt_cpuid, rt_rdtsc); in the test harness both are
stub events (docs/DESIGN.md sections 7 and 8).
"""

from ..translate import SEQ, Family, Unsupported, enc

F = Family(__name__)


@F.insn("int3", enc(0xCC))
@F.insn("ud2", enc(0x0F0B))
@F.insn("hlt", enc(0xF4))
def t_trap(tr, flow):
    return ["TRAP(0x%x);" % tr.ins.address]


@F.insn("int", enc(0xCD))
def t_int(tr, flow):
    # int 0x29 is Windows' __fastfail: the process ends there.  Other vectors: skip.
    if len(tr.ops) == 1 and tr.ops[0].imm == 0x29:
        return ["TRAP(0x%x);" % tr.ins.address]
    raise Unsupported("int 0x%x" % (tr.ops[0].imm & 0xFF if tr.ops else 0))


@F.insn("nop", enc(0x90, 0x0F0D, *range(0x0F18, 0x0F20)))
@F.insn("pause", enc(0x90, mand=0xF3))
@F.insn("prefetcht0", enc(0x0F18))
@F.insn("prefetcht1", enc(0x0F18))
@F.insn("prefetcht2", enc(0x0F18))
@F.insn("prefetchnta", enc(0x0F18))
@F.insn("prefetch", enc(0x0F0D))
@F.insn("prefetchw", enc(0x0F0D))
@F.insn("lfence", enc(0x0FAE, reg=5, mand=0))       # F3 0F AE /5 is incssp
@F.insn("sfence", enc(0x0FAE, reg=7, mand=0))
def t_nop(tr, flow):
    return []


@F.insn("mfence", enc(0x0FAE, reg=6, mand=0))
def t_mfence(tr, flow):
    return ["__atomic_thread_fence(%s);" % SEQ]


@F.insn("clc", enc(0xF8))
@F.insn("stc", enc(0xF9))
@F.insn("cmc", enc(0xF5))
@F.insn("cld", enc(0xFC))
@F.insn("std", enc(0xFD))
def t_flagop(tr, flow):
    return {"clc": ["ctx->cf = 0;"], "stc": ["ctx->cf = 1;"], "cmc": ["ctx->cf ^= 1;"],
            "cld": ["ctx->df = 0;"], "std": ["ctx->df = 1;"]}[tr.name]


@F.insn("cpuid", enc(0x0FA2))
def t_cpuid(tr, flow):
    return ["rt_cpuid(ctx);"]


@F.insn("rdtsc", enc(0x0F31))
def t_rdtsc(tr, flow):
    return ["rt_rdtsc(ctx);"]
