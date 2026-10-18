#!/usr/bin/env python3
"""Differential test harness (docs/DESIGN.md section 8).

Oracle: Unicorn running the original bytes.  Candidate: the lifted C, compiled natively and
run by harness/driver.c in a child process per case.

    .venv/bin/python harness/difftest.py func --module tier0     # isolated function test
    .venv/bin/python harness/difftest.py insn --module tier0     # instruction test

Both modes are the same mechanism: build an initial state, run both sides with every way
out of the function stubbed identically, compare registers, xmm, the call record and
memory.  The instruction test runs one real instruction with random flags going in and
also compares the status flags the instruction defines.

Nothing in here may special-case a function or weaken a comparison to make a case pass.
The only masks are flags and results the Intel SDM leaves undefined; each is cited where
it is applied (undefined_after).  Known oracle bugs are handled by correcting the expected
value (oracle_corrections) or by declaring exactly the affected cases inconclusive, never by
comparing less.
"""

import argparse
import functools
import glob
import itertools
import json
import mmap
import os
import random
import struct
import subprocess
import sys
import time
from collections import Counter, defaultdict
from concurrent.futures import ThreadPoolExecutor
from multiprocessing import Pool

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, ".."))
if REPO not in sys.path:
    sys.path.insert(0, REPO)

import capstone                                   # noqa: E402
from capstone import x86_const as X               # noqa: E402
import unicorn                                    # noqa: E402
from unicorn import x86_const as U                # noqa: E402

from lifter.image import Image, find_module       # noqa: E402
from lifter import lift as lifter_lift            # noqa: E402
from lifter.discover import Program               # noqa: E402
from lifter.translate import GPR, XMM             # noqa: E402
import floatcase as FC                            # noqa: E402
import oraclefix as OF                            # noqa: E402
import statefix as SF                             # noqa: E402
import vecstate as VS                             # noqa: E402

# ---- fixed harness addresses (DESIGN.md section 9) -----------------------------------------

IMAGE_BASE = 0x0000200000000000
ARENA_BASE = 0x0000210000000000
STACK_BASE = 0x0000220000000000
FAKE_IMPORT_BASE = 0x0000230000000000
SENTINEL = 0x0000240000000000
ARENA_SIZE = 1 << 20
STACK_SIZE = 0x40000
RSP0 = STACK_BASE + 0x20000 - 8          # rsp = 8 (mod 16) at entry, as after a call
GS_BASE = ARENA_BASE + 0x80000
PAGE = 4096
INSN_LIMIT = 200000
INSN_GRACE = 20000          # function test: instructions a case over the limit may still run to reach an event
MAX_CALLS = 8192
M64 = (1 << 64) - 1

K_CALL_DIRECT, K_CALL_INDIRECT, K_TAIL_DIRECT, K_TAIL_INDIRECT, K_CPUID, K_RDTSC = 1, 2, 3, 4, 5, 6
KIND_NAMES = {1: "call", 2: "call-indirect", 3: "tail", 4: "tail-indirect", 5: "cpuid", 6: "rdtsc"}
ST_NAMES = {0: "ok", 1: "trap", 2: "signal", 3: "timeout", 4: "no such function", 5: "call-record overflow",
            6: "returned to a non-sentinel address", 7: "child lost",
            8: "float mode violated", 9: "could not map a demand page", 10: "stopped at its event limit"}
ST_NOMAP, ST_CUT = 9, 10
NO_CUT = 0xFFFFFFFF
FPMODE_WHAT = {1: "lifted code was not in the guest float mode", 2: "host code ran in the guest float mode"}

REG_NAMES = "rax rcx rdx rbx rsp rbp rsi rdi r8 r9 r10 r11 r12 r13 r14 r15".split()
UC_GPR = [U.UC_X86_REG_RAX, U.UC_X86_REG_RCX, U.UC_X86_REG_RDX, U.UC_X86_REG_RBX, U.UC_X86_REG_RSP,
          U.UC_X86_REG_RBP, U.UC_X86_REG_RSI, U.UC_X86_REG_RDI] + [getattr(U, "UC_X86_REG_R%d" % i) for i in range(8, 16)]
UC_XMM = [getattr(U, "UC_X86_REG_XMM%d" % i) for i in range(16)]
FLAG_BITS = {"cf": 0, "pf": 2, "af": 4, "zf": 6, "sf": 7, "df": 10, "of": 11}
STATUS_FLAGS = ("cf", "pf", "af", "zf", "sf", "of")
FLAG_MASK = sum(1 << b for b in FLAG_BITS.values())


def round_up(n, a):
    return (n + a - 1) // a * a


# ---- the stub effect: must match harness/driver.c exactly -----------------------------------

def mix64(x):
    x ^= x >> 30
    x = (x * 0xBF58476D1CE4E5B9) & M64
    x ^= x >> 27
    x = (x * 0x94D049BB133111EB) & M64
    x ^= x >> 31
    return x


def hv(seed, idx, k):
    return mix64((seed + 0x9E3779B97F4A7C15 * (idx + 1) + 0xD1B54A32D192ED03 * (k + 1)) & M64)


def stub_rax(seed, v):
    """The stub's return value.  Three seeds in four always return an arena pointer, so that
    callers which dereference the result can run to the end; the fourth mixes in 0, 1 and
    small numbers to reach the other branches."""
    sel, w = v & 7, v >> 8
    if (seed & 3) != 0 or sel <= 4:
        return ARENA_BASE + 0x40000 + (w % 0x8000) * 16
    if sel == 5:
        return 0
    if sel == 6:
        return 1
    return w & 0xFF


# ---- demand pages: must match harness/driver.c exactly ---------------------------------------
#
# Issue 44, proposal 3 (DESIGN.md section 8, "Demand pages").  In the function test the memory
# outside the image, arena and stack is not absent: every 4 KiB page in the window below has
# contents that are a function of the case's seed and the page's address, and the oracle maps a
# page the first time the function touches it.  The candidate is given the same pages before
# it runs.  The window is what a native macOS arm64 process can map without meeting its own
# mappings (measured 2026-10-10, macOS 27): nothing below 4 GiB can be mapped at all, the
# executable, the heap and the shared library cache lie below 1 TiB, and the address space
# ends at 0x7FFFFE000000.  Both bounds are multiples of the host's 16 KiB page.

DEMAND_LO = 0x0000010000000000
DEMAND_HI = 0x00007FFFFE000000
DEMAND_MAX = 256                         # pages one case may map; beyond it the case is inconclusive
DEMAND_LIMIT = "demand page limit"
HOST_PAGE = 0x4000


def demand_page(seed, addr):
    """Initial contents of the 4 KiB demand page at `addr`: every aligned 8 bytes is a pointer
    into the arena, like the arena of the base seeds, so that code which loads a pointer from a
    page it reached by arithmetic (an imported variable, a table past the image, an index that
    is half a pointer) can follow it."""
    k = mix64(seed ^ mix64((addr >> 12) + 0x44454D414E44))
    g, m, out = 0x9E3779B97F4A7C15, M64, []
    for i in range(1, 513):
        x = (k + g * i) & m
        x ^= x >> 30
        x = (x * 0xBF58476D1CE4E5B9) & m
        x ^= x >> 27
        x = (x * 0x94D049BB133111EB) & m
        x ^= x >> 31
        out.append(ARENA_BASE + 0x40000 + (x % 0x8000) * 16)
    return struct.pack("<512Q", *out)


def demand_ok(addr):
    """Whether the 4 KiB page holding `addr` may be mapped on demand."""
    return DEMAND_LO <= addr < DEMAND_HI


# Unicorn 2.1.4 looks memory up by the low 52 bits of an address (QEMU's PG_ADDRESS_MASK, applied
# with paging off) and hands its hooks the address cut the same way: a load from
# 0xf2903683000dc068 is a load from 0x3683000dc068, and succeeds if a page is there.  A real CPU
# raises #GP for an address that is not canonical, and lifted code gets a signal.  So the pager
# never trusts the address it is handed: it works out the instruction's own operand addresses
# with all 64 bits (Oracle._operands) and maps a page only for an operand that really lies there.
UC_ADDR_MASK = (1 << 52) - 1
STACK_INSNS = frozenset(["push", "pop", "call", "ret", "retf", "pushfq", "popfq", "pushf", "popf", "enter", "leave"])


# ---- the stack probe ------------------------------------------------------------------------
#
# A function with more than a page of locals starts with 'mov eax, size; call probe; sub rsp, rax'.
# The compiler's stack probe (__chkstk) is not an ordinary callee: it returns with every register
# as it was, rax included, and the caller relies on it.  The stub effect puts a "return value" in
# rax, which sends rsp somewhere else, so such a function never ran.  The probe is recognised by
# what its callers do, never by name or address: a direct call that is immediately followed by
# 'sub rsp, rax' is a call to it.  A case that called it and then came out inconclusive is run
# once more with the probe's contract (Case.probe): a call to it is recorded like any other
# event and then only returns.  A case that was conclusive without that stays as it was.

def probe_targets(image, where):
    """({rva of a stack probe}, {name of a function that calls one}), from the instructions of
    the lifted functions (`where`: the lifter's <module>.cfg.json)."""
    d, targets, callers = image.data, set(), set()
    for name, w in where.items():
        for a in w["insns"]:
            if d[a] == 0xE8 and bytes(d[a + 5:a + 8]) == b"\x48\x2b\xe0":       # call rel32 ; sub rsp, rax
                targets.add((a + 5 + struct.unpack_from("<i", d, a + 1)[0]) & 0xFFFFFFFF)
                callers.add(name)
    return targets, callers


# ---- initial states --------------------------------------------------------------------------

def arena_ptr(rng):
    return ARENA_BASE + 0x40000 + rng.randrange(0x8000) * 16


def small_int(rng, bits):
    """An integer of the kind code branches on: 0, 1, -1, a small count, or a sign boundary."""
    r = rng.random()
    mask = (1 << bits) - 1
    if r < 0.30:
        return 0
    if r < 0.45:
        return 1
    if r < 0.55:
        return mask                                  # -1
    if r < 0.80:
        return rng.randrange(2, 256)
    if r < 0.90:
        return rng.choice([0x7F, 0x80, 0xFF, 0x7FFF, 0x8000, 0xFFFF, 0x7FFFFFFF, 0x80000000, mask - 1,
                           1 << (bits - 1), (1 << (bits - 1)) - 1]) & mask
    return rng.getrandbits(bits)


@functools.lru_cache(maxsize=64)
def make_mem(kind, seed):
    """Arena followed by stack.

    'ptr':   every aligned 8 bytes of the arena is a pointer back into the arena (DESIGN.md
             section 8), so pointer chains can be followed - but every aligned dword is half
             a pointer, so code that tests an integer it loaded always goes the same way.
    'mixed': half the aligned qwords are such pointers, half are small integers.
    'ints':  every aligned dword is a small integer (0, 1, -1, a small count, a sign
             boundary); nothing loaded from this memory is a usable pointer.
    'nulls': as 'ptr', but one aligned qword in eight is zero: a list or a tree that is
             walked until a null pointer ends, where in 'ptr' memory it never does.
    'rand':  random bytes, for instruction tests."""
    rng = random.Random("mem:%s:%d" % (kind, seed))
    if kind == "ptr":
        n = ARENA_SIZE // 8
        arena = struct.pack("<%dQ" % n, *[arena_ptr(rng) for _ in range(n)])
        n = STACK_SIZE // 8
        stack = bytearray(struct.pack("<%dQ" % n, *[arena_ptr(rng) if rng.random() < 0.75 else rng.randrange(256)
                                                    for _ in range(n)]))
    elif kind == "mixed":
        n = (ARENA_SIZE + STACK_SIZE) // 8
        both = struct.pack("<%dQ" % n, *[arena_ptr(rng) if rng.random() < 0.5 else small_int(rng, 64)
                                         for _ in range(n)])
        arena, stack = both[:ARENA_SIZE], bytearray(both[ARENA_SIZE:])
    elif kind == "nulls":
        n = (ARENA_SIZE + STACK_SIZE) // 8
        both = struct.pack("<%dQ" % n, *[0 if rng.random() < 0.125 else arena_ptr(rng) for _ in range(n)])
        arena, stack = both[:ARENA_SIZE], bytearray(both[ARENA_SIZE:])
    elif kind == "ints":
        n = (ARENA_SIZE + STACK_SIZE) // 4
        both = struct.pack("<%dI" % n, *[small_int(rng, 32) for _ in range(n)])
        arena, stack = both[:ARENA_SIZE], bytearray(both[ARENA_SIZE:])
    else:
        arena = rng.randbytes(ARENA_SIZE)
        stack = bytearray(rng.randbytes(STACK_SIZE))
    struct.pack_into("<Q", stack, RSP0 - STACK_BASE, SENTINEL)
    return bytes(arena) + bytes(stack)


class Case:
    """One run.  `pokes` is a list of (address, bytes) written into the image, arena or stack
    before the run, on both sides; they are part of the initial state.  `paging`: the memory
    outside the three regions is there on demand (demand_page); `demand` is the list of the
    pages the oracle mapped, filled in after it ran, which the candidate maps before it runs.
    `cut`: a case that runs into the call record's or the instruction limit ends at its next
    event and is compared there (Oracle._run); `cut_at` is that event's index, from the oracle.
    `probe`: a call to a stack probe returns with every register as it was (probe_targets).
    `stops`: the RVAs of the function's instructions that the lifter does not translate and
    stops at (the lift's cfg.json); the oracle ends the case at one, before it executes.
    `table_stops`: [(rva of a jump through a table that nothing bounds, number of the jump's
    register, the RVAs of its cases)], also from cfg.json; the oracle ends the case at such a
    jump, before it executes, when the register holds no case's address (TABLE_MISS_WHAT)."""
    __slots__ = ("id", "key", "func_rva", "ranges", "end", "mem_id", "regs", "eflags", "xmm", "gs_base",
                 "seed", "insn_rva", "pokes", "label", "mxcsr", "fpcw", "paging", "demand", "cut", "cut_at", "probe",
                 "stops", "table_stops")

    def __init__(self, **kw):
        self.mxcsr = FC.MXCSR_DEFAULT
        self.fpcw = SF.FPCW_DEFAULT
        self.end = None
        self.insn_rva = None
        self.gs_base = GS_BASE
        self.pokes = ()
        self.label = ""
        self.paging = False
        self.demand = ()
        self.cut = False
        self.cut_at = NO_CUT
        self.probe = False
        self.stops = ()
        self.table_stops = ()
        for k, v in kw.items():
            setattr(self, k, v)


def func_state(rng, args="std"):
    """Registers for an isolated function test: argument and scratch registers hold arena
    pointers or small integers; callee-saved registers hold random values (they must come
    back unchanged).  args="ptr": the four argument registers all hold arena pointers.
    args="all": every register but rsp does, for a function that takes an argument in a
    register the calling convention does not use for one."""
    regs = []
    for i in range(16):
        if i == 4:
            regs.append(RSP0)
        elif args == "all":
            regs.append(arena_ptr(rng))
        elif i in (3, 5, 6, 7, 12, 13, 14, 15):
            regs.append(rng.getrandbits(64))
        elif args == "ptr" and i in (1, 2, 8, 9):
            regs.append(arena_ptr(rng))
        elif rng.random() < 0.7:
            regs.append(arena_ptr(rng))
        else:
            regs.append(rng.choice([0, 1, 2, 3, 4, 8, 16, rng.randrange(256)]))
    eflags = 2 | (rng.getrandbits(12) & FLAG_MASK & ~(1 << FLAG_BITS["df"]))
    return regs, eflags, rng.randbytes(256)


AIM_NAMES = {n: (i, 64) for i, n in enumerate(
    "rax rcx rdx rbx rsp rbp rsi rdi r8 r9 r10 r11 r12 r13 r14 r15".split())}
AIM_NAMES.update({n: (i, 32) for i, n in enumerate(
    "eax ecx edx ebx esp ebp esi edi r8d r9d r10d r11d r12d r13d r14d r15d".split())})


def parse_aim(specs):
    """The settings that `--reg` asks for (tools/functest, `one`).  Each spec sets a register,
    'REG=V' or 'REG=V,V,...' with REG a 64-bit general register or its 32-bit name, or the
    memory a register points at, '[REG]:BITS=V,...' or '[REG+OFF]:BITS=V,...' with BITS 8, 16,
    32 or 64; V and OFF are integers in any base Python reads (a negative V is taken modulo
    the width).  Returns one setting per combination of the values, in the order given, as
    (label, [item]) with item (register number, bits, value) or ("mem", register number,
    offset, bits, value): [("rcx=0x7 [rdx+0x40]:32=0x0", [(1, 64, 7), ("mem", 2, 0x40, 32, 0)]),
    ...].  ValueError says what is wrong: an unknown register, rsp set, a register or a place
    named twice, a value that is no integer or does not fit."""
    axes, seen = [], set()
    for spec in specs:
        place, eq, values = spec.partition("=")
        place = place.strip().lower().replace(" ", "")
        mem = None
        if place.startswith("["):
            inner, _, bits = place[1:].partition("]:")
            name, sign, off = inner.partition("+") if "+" in inner else inner.partition("-")
            try:
                mem = (int(sign + off, 0) if sign else 0, int(bits))
            except ValueError:
                raise ValueError("--reg %s: expected [REG+OFFSET]:BITS=VALUE[,VALUE...]" % spec) from None
            if name not in AIM_NAMES or AIM_NAMES[name][1] != 64 or mem[1] not in (8, 16, 32, 64):
                raise ValueError("--reg %s: expected [REG+OFFSET]:BITS with a 64-bit register and 8, 16, 32 or 64 bits" % spec)
            num, bits, key = AIM_NAMES[name][0], mem[1], (AIM_NAMES[name][0], mem[0])
        else:
            if not eq or place not in AIM_NAMES:
                raise ValueError("--reg %s: expected REG=VALUE[,VALUE...] with a 64- or 32-bit general register" % spec)
            (num, bits), key = AIM_NAMES[place], AIM_NAMES[place][0]
            if num == 4:
                raise ValueError("--reg %s: the stack pointer is the harness's" % spec)
        if not eq or key in seen:
            raise ValueError("--reg %s: %s" % (spec, "no value" if not eq else "set twice"))
        seen.add(key)
        axis = []
        for v in values.split(","):
            try:
                x = int(v.strip(), 0)
            except ValueError:
                raise ValueError("--reg %s: %r is not an integer" % (spec, v)) from None
            if not (-(1 << (bits - 1)) <= x < (1 << bits)):
                raise ValueError("--reg %s: %s does not fit in %d bits" % (spec, v.strip(), bits))
            x &= (1 << bits) - 1
            axis.append(("%s=0x%x" % (place, x), (num, bits, x) if mem is None else ("mem", num, mem[0], bits, x)))
        axes.append(axis)
    return [(" ".join(text for text, _ in combo), [item for _, item in combo]) for combo in itertools.product(*axes)]


def aim_regs(regs, setting):
    """`regs` with the registers of one setting replaced.  A 32-bit name replaces the low half
    and keeps the seed's upper half, which a function that reads 32 bits must ignore."""
    regs = list(regs)
    for item in setting:
        if item[0] != "mem":
            num, bits, value = item
            regs[num] = value if bits == 64 else (regs[num] & ~0xFFFFFFFF) | value
    return regs


def aim_pokes(regs, setting):
    """The memory writes of one setting, as Case.pokes, for registers `regs` (aimed already).
    A place that is not inside the arena or the stack, because the seed's register is no
    pointer there, is left alone: that case runs with the seed's memory."""
    pokes = []
    for item in setting:
        if item[0] == "mem":
            _, num, off, bits, value = item
            addr, n = (regs[num] + off) & M64, bits // 8
            if (ARENA_BASE <= addr and addr + n <= ARENA_BASE + ARENA_SIZE) or (STACK_BASE <= addr and addr + n <= STACK_BASE + STACK_SIZE):
                pokes.append((addr, value.to_bytes(n, "little")))
    return pokes


INTERESTING = [0, 1, 2, 3, 7, 8, 0xF, 0x10, 0x1F, 0x20, 0x3F, 0x40, 0x7F, 0x80, 0xFF, 0x100, 0x7FFF, 0x8000,
               0xFFFF, 0x10000, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, 0x100000000, 0x7FFFFFFFFFFFFFFF,
               0x8000000000000000, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFE, 0xFFFFFFFF80000000,
               0xFFFFFFFFFFFFFF80, 0xFFFFFFFFFFFF8000, 0x5555555555555555, 0xAAAAAAAAAAAAAAAA]
STRING_OPS = {k + s for k in ("movs", "stos", "lods", "scas", "cmps") for s in "bwdq"}


def rand_val(rng):
    r = rng.random()
    if r < 0.45:
        return rng.choice(INTERESTING)
    if r < 0.75:
        return rng.getrandbits(64)
    if r < 0.9:
        return rng.getrandbits(32)
    return rng.getrandbits(8)


def ea_of(ins, op, regs, gs_base=GS_BASE, rip_base=IMAGE_BASE):
    """Effective address of a memory operand for the given register values.  `rip_base` is
    what to add to ins.address: IMAGE_BASE for an instruction decoded at its RVA, 0 for one
    decoded at its run-time address."""
    m = op.mem
    if m.base == X.X86_REG_RIP:
        a = rip_base + ins.address + ins.size + m.disp
    else:
        a = m.disp
        for reg, scale in ((m.base, 1), (m.index, m.scale)):
            if reg in GPR:
                bits, i, _ = GPR[reg]
                a += (regs[i] & ((1 << bits) - 1)) * scale
        if ins.addr_size == 4:
            a &= 0xFFFFFFFF
    if m.segment == X.X86_REG_GS:
        a += gs_base
    return a & M64


def atomic_operand(ins):
    """(memory operand, size in bytes) if the instruction updates memory atomically: any
    LOCK-prefixed instruction, xchg with memory, and cmpxchg16b (whose operand has the same
    alignment rule with and without LOCK).  Otherwise None."""
    name = ins.insn_name()
    mem = [op for op in ins.operands if op.type == X.X86_OP_MEM]
    if not mem:
        return None
    if name == "cmpxchg16b":
        return mem[0], 16
    if 0xF0 in _legacy_prefixes(bytes(ins.bytes)) or name == "xchg":
        return mem[0], mem[0].size
    return None


def straddles(addr, size):
    return (addr & 15) + size > 16


def _steer(ins, op, regs, want, mod):
    """Change one register in op's address so that the address is `want` modulo `mod`.
    Nothing can be done for a RIP-relative or absolute operand."""
    delta = (want - ea_of(ins, op, regs)) % mod
    m = op.mem
    if delta == 0 or m.base == X.X86_REG_RIP:
        return
    coef = {}
    for reg, scale in ((m.base, 1), (m.index, m.scale)):
        if reg in GPR:
            coef[GPR[reg][1]] = coef.get(GPR[reg][1], 0) + scale
    for i in sorted(coef, key=lambda i: i == 4):         # the stack pointer only if nothing else will do
        for d in range(1, mod):
            if coef[i] * d % mod == delta:
                regs[i] = (regs[i] + d) & M64
                return


def place_atomic_operand(ins, regs, k):
    """Where the operand of an atomic instruction lies relative to a 16-byte boundary.

    x86 allows a locked operand at any address.  The host's atomic instructions fault when
    the operand straddles a 16-byte boundary, and the lifted code then takes the slow path
    in runtime.h (x_atomic_*), so straddling operands must be generated and tested, not
    avoided.  Over the states of one instruction: half are naturally aligned (what compilers
    emit), a quarter straddle a 16-byte boundary, a quarter lie wherever the random
    registers put them.  rsp-based and scaled-index operands are steered like any other;
    RIP-relative ones cannot be, so tests/vectors.py has encodings whose target straddles.
    For cmpxchg16b the non-aligned states are the #GP cases (see Oracle._run)."""
    op, size = atomic_operand(ins)
    cls = 0 if k is None else ((k >> 1) + k // 24) & 3
    if cls in (0, 2):
        _steer(ins, op, regs, 0, size)
    elif cls == 1 and size > 1:
        _steer(ins, op, regs, 17 - size + k % (size - 1), 16)


def place_aligned_operand(ins, regs, k):
    """Where the 16-byte memory operand of an instruction that requires alignment lies in state
    k (oraclefix.py): aligned in four states of five, at a misaligned offset in the fifth.  A
    register of its address is moved by up to 15 bytes; an operand that cannot be steered
    (RIP-relative, absolute) is left alone.  Returns None if the instruction has no such
    operand, else whether the operand ends up misaligned."""
    op = OF.aligned_operand(ins)
    if op is None:
        return None
    if k is not None:
        _steer(ins, op, regs, OF.wanted_offset(k), 16)
    return ea_of(ins, op, regs) & 15 != 0


def sized_boundaries(bits):
    """The values of a `bits`-bit operand at which results and flags change: 0, 1, -1, the
    most negative and most positive signed values, then their neighbours."""
    m, sgn = (1 << bits) - 1, 1 << (bits - 1)
    return [0, 1, m, sgn, sgn - 1], [m - 1, 2, sgn + 1]


def _other_operand(ins, mem_op, regs):
    """The operand that the memory operand `mem_op` is combined with, among the first two:
    (value, register index or None, bit mask of the register part) or None."""
    used = {GPR[r][1] for op in ins.operands if op.type == X.X86_OP_MEM
            for r in (op.mem.base, op.mem.index) if r in GPR}
    for op in ins.operands[:2]:
        if op.type == X.X86_OP_IMM:
            return op.imm, None, 0
        if op.type == X.X86_OP_REG and op.reg in GPR:
            bits, i, hi = GPR[op.reg]
            part = ((1 << bits) - 1) << (8 if hi else 0)
            return (regs[i] & part) >> (8 if hi else 0), (None if i in used or i == 4 else i), part
    return None


def operand_pokes(ins, regs, j, rng2, peek, cf):
    """Review finding 2: boundary values in the memory operands of an instruction.

    Random memory almost never holds 0, 1, -1 or a sign boundary, so flags and results that
    only change there (the ZF of 'lock dec' on a reference count of 1) were never checked.
    In the j-th seeded state of an instruction each explicit memory operand is preset to the
    j-th entry of this list:

      0, 1, -1, most negative, most positive                        (operand-size boundaries)
      v, -v, ~v, v-1, v+1     where v is the other operand's value   (equal, cancelling, ...;
                              v includes the incoming carry for adc and sbb)
      0 with the other operand, if it is a register, also set to 0   (both zero)
      -2, 2, most negative + 1

    and after that to a random pick from the list and INTERESTING.  Returns [(address,
    bytes)]; the same pokes go to the oracle and the candidate.  `regs` is modified in place
    only for the 'both zero' entry."""
    name = ins.insn_name()
    if name == "lea" or name.startswith(("nop", "prefetch")):
        return []
    pokes = []
    lo, hi = IMAGE_BASE + ins.address, IMAGE_BASE + ins.address + ins.size
    for op in ins.operands:
        if op.type != X.X86_OP_MEM or op.size not in (1, 2, 4, 8, 16):
            continue
        bits = min(op.size, 8) * 8
        mask = (1 << bits) - 1
        first, last = sized_boundaries(bits)
        seq = list(first)
        other = _other_operand(ins, op, regs)
        both_zero = None
        if other is not None:
            v = (other[0] + (cf if name in ("adc", "sbb") else 0)) & mask
            seq += [v, -v & mask, ~v & mask, (v - 1) & mask, (v + 1) & mask]
            if other[1] is not None:
                both_zero = len(seq)
                seq.append(0)
        seq += last
        pool = seq + [x & mask for x in INTERESTING]
        vals = [seq[j] if j < len(seq) else rng2.choice(pool) for _ in range(op.size // (bits // 8))]
        if op.size == 16 and j < len(seq):
            vals[1] = rng2.choice(pool)
        data = b"".join(x.to_bytes(bits // 8, "little") for x in vals)
        a = ea_of(ins, op, regs)
        if peek(a, len(data)) is None or (a < hi and lo < a + len(data)):
            continue                 # not mapped, or it would overwrite the instruction under test
        if j == both_zero:
            regs[other[1]] &= ~other[2] & M64
        pokes.append((a, data))
    return pokes


def _incoming_cf(rng, draws_first):
    """The carry flag insn_state is going to draw, without disturbing the stream."""
    state = rng.getstate()
    for _ in range(draws_first):
        rng.random()
    cf = rng.getrandbits(12) & 1
    rng.setstate(state)
    return cf


def string_pokes(ins, regs, eflags, rng2, peek):
    """The same idea for compare and scan string instructions: with random memory two
    elements are almost never equal, so 'repe cmps' stops at once and 'repne scas' never
    finds anything.  Make a run of elements equal."""
    name = ins.insn_name()
    kind, sz = name[:4], {"b": 1, "w": 2, "d": 4, "q": 8}[name[4]]
    if kind not in ("cmps", "scas"):
        return []
    rep = _legacy_prefixes(bytes(ins.bytes))
    count = (regs[1] & 0xFF) if (0xF2 in rep or 0xF3 in rep) else 1
    step = -sz if (eflags >> FLAG_BITS["df"]) & 1 else sz
    n = rng2.randrange(0, min(count, 6) + 2)             # elements made equal
    first = rng2.randrange(0, 3) if 0xF2 in rep else 0   # repne: the match is not the first element
    pokes = []
    for i in range(first, first + n):
        dst = (regs[7] + i * step) & M64
        if kind == "cmps":
            src = (regs[6] + i * step) & M64
            v = _overlay(peek, pokes)(src, sz)
        else:
            v = regs[0] & ((1 << (8 * sz)) - 1)
        if v is None or peek(dst, sz) is None:
            break
        pokes.append((dst, v.to_bytes(sz, "little")))
    return pokes


def _overlay(peek, pokes):
    """peek() that sees `pokes` on top of the initial memory."""
    def f(addr, n):
        for a, d in reversed(pokes):
            if a <= addr and addr + n <= a + len(d):
                return int.from_bytes(d[addr - a:addr - a + n], "little")
        return peek(addr, n) if peek else None
    return f


def _legacy_prefixes(raw):
    """The legacy prefix bytes at the start of an instruction's encoding."""
    out = []
    for b in raw:
        if b not in (0xF0, 0xF2, 0xF3, 0x2E, 0x36, 0x3E, 0x26, 0x64, 0x65, 0x66, 0x67):
            break
        out.append(b)
    return out


def insn_state(ins, rng, peek=None, k=None, rng2=None):
    """Registers for an instruction test: adversarial values everywhere, except that
    registers the instruction uses to form an address hold arena pointers (base) or small
    numbers (index), so that the case can run at all.  `peek(addr, n)` reads initial
    memory; it is used to make cmpxchg succeed in about half of the cases.

    `k` is the index of this state among the instruction's states.  Odd states also get
    memory pokes (operand_pokes, string_pokes), drawn from `rng2`, so the even states are
    exactly what they were without pokes, except for the vector operands: in the even states
    the xmm registers and 16-byte memory operands the instruction reads hold values chosen
    from the instruction (vecstate.py: shift counts around the lane width, equal lanes, the
    ends of the lanes' ranges), without a draw from `rng`.  Returns (regs, eflags, xmm, pokes)."""
    seeded = k is not None and k % 2 == 1 and peek is not None and rng2 is not None
    pokes = []
    regs = [rand_val(rng) for _ in range(16)]
    regs[4] = RSP0
    name = ins.insn_name()
    is_string = name in STRING_OPS and ins.opcode[0] in range(0xA4, 0xB0)
    for op in ins.operands:
        if op.type != X.X86_OP_MEM:
            continue
        m = op.mem
        if m.index in GPR and GPR[m.index][1] != 4:
            regs[GPR[m.index][1]] = rng.randrange(-8, 64) & M64
        if m.base in GPR and GPR[m.base][1] != 4:
            # gs:[reg + disp] is relative to the TEB: keep the register small
            regs[GPR[m.base][1]] = rng.randrange(0, 0x400) * 8 if m.segment == X.X86_REG_GS else arena_ptr(rng)
    if is_string:
        regs[6], regs[7] = arena_ptr(rng), arena_ptr(rng)
        if {0xF2, 0xF3} & set(_legacy_prefixes(bytes(ins.bytes))):
            regs[1] = rng.randrange(0, 48)
    if name == "leave":
        regs[5] = arena_ptr(rng)
    if name in ("bt", "bts", "btr", "btc") and ins.operands[0].type == X.X86_OP_MEM \
            and ins.operands[1].type == X.X86_OP_REG and ins.operands[1].reg in GPR:
        regs[GPR[ins.operands[1].reg][1]] = rng.randrange(-512, 2048) & M64
    if name in ("div", "idiv") and rng.random() < 0.8:
        # keep most dividends small enough that the quotient fits (otherwise #DE: inconclusive)
        bits = ins.operands[0].size * 8
        if bits == 8:
            regs[0] = rng.randrange(0x8000) if name == "div" else rng.randrange(-0x4000, 0x4000) & M64
        else:
            lo = regs[0] & ((1 << bits) - 1)
            neg = name == "idiv" and (lo >> (bits - 1)) & 1
            regs[2] = ((1 << 64) - 1) if neg else 0
            if rng.random() < 0.3:
                regs[2] = rng.randrange(4)
    mem_ops = [op for op in ins.operands if op.type == X.X86_OP_MEM]
    if atomic_operand(ins):
        place_atomic_operand(ins, regs, k)
    else:
        place_aligned_operand(ins, regs, k)
    if seeded and not is_string:
        cf = _incoming_cf(rng, 1 if name in ("cmpxchg", "cmpxchg16b") else 0)
        pokes = operand_pokes(ins, regs, k // 2, rng2, peek, cf)
        peek = _overlay(peek, pokes)
    if name in ("cmpxchg", "cmpxchg16b") and rng.random() < 0.5:
        used = {GPR[r][1] for op in mem_ops for r in (op.mem.base, op.mem.index) if r in GPR}
        d = ins.operands[0]
        if name == "cmpxchg16b":
            if peek and not ({0, 2} & used):
                a = ea_of(ins, d, regs)
                regs[0], regs[2] = peek(a, 8), peek(a + 8, 8)
        elif 0 not in used:
            mask = (1 << (d.size * 8)) - 1
            v = None
            if d.type == X.X86_OP_MEM:
                v = peek(ea_of(ins, d, regs), d.size) if peek else None
            elif d.reg in GPR and not GPR[d.reg][2]:
                v = regs[GPR[d.reg][1]] & mask
            if v is not None:
                regs[0] = (regs[0] & ~mask & M64) | v        # upper accumulator bits stay random
    eflags = 2 | (rng.getrandbits(12) & FLAG_MASK)
    if not (is_string or name in ("cld", "std", "pushfq")):
        eflags &= ~(1 << FLAG_BITS["df"])
    if seeded and is_string:
        pokes = string_pokes(ins, regs, eflags, rng2, peek)
    xmm = rng.randbytes(256)
    xmm, vec = VS.structure(ins, regs, xmm, k, peek, ea_of)
    at = IMAGE_BASE + ins.address                    # as operand_pokes: never over the instruction under test
    pokes = pokes + [(a, d) for a, d in vec if not (a < at + ins.size and at < a + len(d))]
    if name == "ldmxcsr":
        # Three states in four load a value a program can load (any low 16 bits); the fourth
        # sets reserved bits (16-31), which is #GP.  Random memory would be #GP nearly always.
        v = rng.getrandbits(16) | (rng.getrandbits(16) << 16 if rng.random() < 0.25 else 0)
        pokes = [(ea_of(ins, ins.operands[0], regs), v.to_bytes(4, "little"))]
    if name == "popfq":
        pokes = [(regs[4], SF.popfq_value(rng))]
    if name == "pcmpistri":
        xmm, pokes = SF.pcmpistri_state(ins, regs, xmm, rng, ea_of)
    return regs, eflags, xmm, pokes


# ---- oracle ------------------------------------------------------------------------------------

FETCH_ERRORS = (unicorn.UC_ERR_FETCH_UNMAPPED, unicorn.UC_ERR_FETCH_PROT)


class _Run:
    __slots__ = ("ranges", "n", "stop", "event", "pending", "prev", "intno", "cov", "br", "jcc", "fp", "nan_fixed", "trap", "fixes", "post",
                 "stops", "table_stops")


# A third oracle bug (review finding 8).  Unicorn 2.1.4 computes the address of
# 'shld/shrd r/m, r, imm8' with a RIP-relative operand one byte too low.  The review traced
# it to QEMU v5.0.0, target/i386/translate.c: the shld/shrd-by-immediate path (do_shiftd)
# never sets s->rip_offset, so the immediate byte that follows the displacement is not
# counted (https://raw.githubusercontent.com/qemu/qemu/v5.0.0/target/i386/translate.c; not
# re-read for this change).  Measured here on Unicorn 2.1.4: '48 0f ac 05 <disp> 04' changes
# the bytes at operand-1 .. operand+6 and '0f ac 05 <disp> 04' those at operand-1 ..
# operand+2, while the ', cl' forms change exactly the operand.  The lifted code follows the
# SDM.  A case that executes such an instruction is inconclusive: the oracle's memory is
# wrong from there on.
ORACLE_BUG_SHIFTD = ("oracle bug: shld/shrd with a RIP-relative operand and an immediate count "
                     "(QEMU 5.0 addresses the operand one byte low)")

JCC_NAMES = frozenset(["j" + c for c in ("o", "no", "b", "ae", "e", "ne", "be", "a", "s", "ns", "p", "np", "l",
                                         "ge", "le", "g", "rcxz", "ecxz")] + ["loop", "loope", "loopne"])
BR_TAKEN, BR_NOT_TAKEN = 1, 2


# Instructions whose fault is part of the guest's defined behaviour (review finding 4).  The
# lifter translates each to TRAP(rva); the oracle reports the same RVA and the two states are
# compared like any other result.  Every other CPU exception stays inconclusive.
TRAP_KINDS = {"int3": "int3", "ud2": "ud2", "hlt": "hlt", "int29": "int 0x29 (__fastfail)", "div": "#DE"}

# A stop (lifter/stops.py): an instruction inside a lifted function that the lifter decodes and
# does not translate.  The lifted code halts there (UNTRANSLATED of runtime.h, a trap at that
# RVA in the isolated build).  Where the stops are is the lift's own statement (cfg.json),
# not something the oracle could know; what the oracle contributes is the state on arrival.
# So the oracle ends the case when it arrives at one, before executing it, the candidate must
# stop at the same instruction, and the two states are compared like any other result.  Such
# a case says nothing about the instruction or what follows it: it is counted apart
# ("stopped") and never makes a function pass.
# `ret imm16` with an immediate of 0x8000 or more.  The SDM's operand is "the number of stack
# bytes to be released after the return address is popped" (RET, description; the operation is
# RSP := RSP + SRC): a count, 0 to 65,535.  Unicorn 2.1.4 (QEMU's translate.c reads the word
# signed) moves rsp DOWN for such a count: ret 0x8401 leaves rsp at start + 8 - 0x7bff.  The
# lifted code releases 0x8401 bytes.  No compiler emits such a return; the one instance in the
# 20 modules is data in engine's code section that a `lea` points at (engine+0x570a80, bytes
# 00 00 c2 01 84), found by the function test on 2026-10-10.  Not measured on a CPU, so the
# emulator is not corrected: exactly these cases are inconclusive.
ORACLE_BUG_RET_IMM = ("oracle bug: ret imm16 with an immediate of 0x8000 or more "
                      "(the emulator takes the count of bytes to release as negative)")
RET_IMM_RULE = True                 # the self-tests switch it off to show that the emulator still needs it

# `bsf`/`bsr` with a 32-bit destination and a zero source.  The SDM calls the destination
# undefined.  Measured on the Core i5-7600K (docs/handoff/hw-oracle.md): all 64 bits of the
# register stay as they were, there is no zero-extension; the lifted code does the same (it does
# not write the destination).  Unicorn 2.1.4 writes the old low half back as a 32-bit result and
# so clears the upper half.  In the instruction test the destination is not compared in that
# case (undefined_after); in the function test what follows uses it (engine+0xb4bb0 indexes a
# table with it: the emulator read a mapped page, the lifted code, like the CPU, an address no
# CPU accepts, and the function failed, 2026-10-10).  So the emulator is corrected: after such
# an instruction, if ZF says the source was zero, the register gets its old 64 bits back.
BSCAN_RULE = True                   # the self-tests switch it off to show that the emulator still needs it

UNTRANSLATED_WHAT = "an instruction the lifter does not translate, where the lifted code stops"
# The same for a jump through a table that nothing bounds (lifter/discover.py, 'unbounded'): the
# lifted switch has the cases the lifter read and stops for any other address (lifter/insns/
# control.py, t_jmp).  Which jump and which cases is the lift's statement (cfg.json, "table_stops"); the
# oracle reads the jump's register on arrival and ends the case there, before the jump, when the
# register holds no case's address.  A case that goes through the table to a case runs on.
TABLE_MISS_WHAT = "a jump through an unbounded table to an address the lifter read no entry for, where the lifted code stops"
STOP_WHATS = (UNTRANSLATED_WHAT, TABLE_MISS_WHAT)


class Oracle:
    def __init__(self, image_bytes, probes=()):
        # relocated, padded to 16 KiB; code is decoded from it.  bytes, or a read-only mapping of
        # the image file (map_file): a large module's image is hundreds of megabytes
        self.image = image_bytes if isinstance(image_bytes, mmap.mmap) else bytes(image_bytes)
        self.probes = frozenset(probes)                  # addresses of the stack probe (probe_targets)
        self.probed = 0                                  # calls to it in the running case
        self.cur_image = self.image                      # what is mapped now (a variant: same code, other data)
        uc = self.uc = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_64)
        uc.mem_map(IMAGE_BASE, len(self.image), unicorn.UC_PROT_ALL)
        uc.mem_map(ARENA_BASE, ARENA_SIZE, unicorn.UC_PROT_READ | unicorn.UC_PROT_WRITE)
        uc.mem_map(STACK_BASE, STACK_SIZE, unicorn.UC_PROT_READ | unicorn.UC_PROT_WRITE)
        uc.mem_write(IMAGE_BASE, self.image[:])
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        self.md.detail = True
        self.icache = {}
        self.mem = None
        self.written = set()
        self.st = None
        uc.hook_add(unicorn.UC_HOOK_CODE, self._code)
        uc.hook_add(unicorn.UC_HOOK_MEM_WRITE, self._write)
        uc.hook_add(unicorn.UC_HOOK_MEM_READ_UNMAPPED | unicorn.UC_HOOK_MEM_WRITE_UNMAPPED, self._bad_access)
        uc.hook_add(unicorn.UC_HOOK_INTR, self._intr)
        self.fault_addr = None
        self.lent = None                # a page mapped for one instruction (oraclefix.py, M64_READ_AS_16)
        self.paging = None              # the seed of the running case's demand pages, or None: no paging
        self.demand = {}                # page number -> initial contents, of the pages mapped on demand
        self.dcache = {}                # address -> decoded instruction, for _operands
        self.cutting = False            # the running case ends at an event when it meets a limit (Case.cut)
        # The CPU as created.  It is restored before every run: Unicorn keeps QEMU's
        # "exception in progress" state (env->old_exception) across emu_start calls, so
        # without this the second #DE or #GP in a worker is reported as a double fault and
        # every later exception as a silent stop (a triple fault).  Measured with
        # harness/out/fixes/p5_ctx.py on Unicorn 2.1.4.
        self.cpu0 = uc.context_save()

    def load_mem(self, data, image=None):
        """Arena and stack contents, and optionally a variant of the image.  Variants differ
        from the image the oracle was built with only in non-executable data."""
        self.mem = data
        self.uc.mem_write(ARENA_BASE, data[:ARENA_SIZE])
        self.uc.mem_write(STACK_BASE, data[ARENA_SIZE:])
        if image is not None and image is not self.cur_image:
            self.uc.mem_write(IMAGE_BASE, image[:])
            self.cur_image = image

    def _info(self, addr):
        """(kind, operand_is_immediate, size) of the instruction at addr, decoded
        independently of the lifter's control-flow analysis."""
        off = addr - IMAGE_BASE
        ins = next(self.md.disasm(self.image[off:off + 15], addr, 1), None)
        if ins is None:
            info = ("other", False, 1)
        else:
            n = ins.insn_name()
            imm = bool(ins.operands) and ins.operands[0].type == X.X86_OP_IMM
            if n == "call":
                info = ("call", imm, ins.size)
            elif n == "jmp":
                info = ("jmp", imm, ins.size)
            elif RET_IMM_RULE and n == "ret" and imm and ins.operands[0].imm & 0xFFFF >= 0x8000:
                info = ("oracle-bug", False, ins.size, ORACLE_BUG_RET_IMM)
            elif n.startswith("ret") or n.startswith("iret"):
                info = ("ret", False, ins.size)
            elif n in ("cpuid", "rdtsc"):
                info = (n, False, ins.size)
            elif n in ("int3", "ud2", "hlt"):
                info = (n, False, ins.size)
            elif n == "int" and imm and ins.operands[0].imm == 0x29:
                info = ("int29", False, ins.size)
            elif n in ("div", "idiv"):
                info = ("div", False, ins.size)
            elif n == "cmpxchg16b":
                info = ("cx16", False, ins.size, ins)
            elif n == "ldmxcsr" and ins.operands[0].type == X.X86_OP_MEM:
                info = ("ldmxcsr", False, ins.size, ins)
            elif (n in ("shld", "shrd") and len(ins.operands) == 3 and ins.operands[0].type == X.X86_OP_MEM
                  and ins.operands[0].mem.base == X.X86_REG_RIP and ins.operands[2].type == X.X86_OP_IMM):
                info = ("oracle-bug", False, ins.size, ORACLE_BUG_SHIFTD)
            elif n in JCC_NAMES and imm:
                info = ("jcc", True, ins.size, ins.operands[0].imm)
            elif (BSCAN_RULE and n in ("bsf", "bsr") and ins.operands[0].type == X.X86_OP_REG
                  and GPR.get(ins.operands[0].reg, (0,))[0] == 32):
                info = ("bscan", False, ins.size, UC_GPR[GPR[ins.operands[0].reg][1]])
            elif n == "popfq":
                info = ("popfq", False, ins.size)
            elif SF.pcmpistri_plan(ins) is not None:
                info = ("pcmpistri", False, ins.size, ins, SF.pcmpistri_plan(ins))
            else:
                plan = OF.plan(ins)
                info = ("other", False, ins.size) if plan is None else ("other", False, ins.size, ins, plan)
        self.icache[addr] = info
        return info

    def _inside(self, addr):
        for b, e in self.st.ranges:
            if b <= addr < e:
                return True
        return False

    def _ea(self, uc, ins, op):
        """Effective address of a memory operand of the instruction about to run."""
        regs = [0] * 16
        for reg in (op.mem.base, op.mem.index):
            if reg in GPR:
                regs[GPR[reg][1]] = uc.reg_read(UC_GPR[GPR[reg][1]])
        return ea_of(ins, op, regs, uc.reg_read(U.UC_X86_REG_GS_BASE) if op.mem.segment == X.X86_REG_GS else 0, rip_base=0)

    def _fp_capture(self, uc, st, ins, value):
        """Before a float instruction with value corrections runs (oraclefix.py): note the lanes
        in which the CPU's result is known to differ from what the emulator computes."""
        dst, src, w, nl = value[:4]
        a = uc.reg_read(UC_XMM[dst]).to_bytes(16, "little")
        if src[0] == "x":
            b = uc.reg_read(UC_XMM[src[1]]).to_bytes(16, "little")
        else:
            try:
                b = bytes(uc.mem_read(self._ea(uc, ins, src[1]), nl * w // 8))
            except unicorn.UcError:
                return                                   # the instruction is about to fault
        wants = OF.wants(value, a, b, uc.reg_read(U.UC_X86_REG_MXCSR))
        if wants:
            st.fp = (dst, w, wants)

    def _fp_apply(self, uc, st):
        """After that instruction completed (the next one is starting): put the CPU's result into
        the lanes noted, and count the lanes this changed, per correction."""
        dst, w, wants = st.fp
        st.fp = None
        v = bytearray(uc.reg_read(UC_XMM[dst]).to_bytes(16, "little"))
        wb = w // 8
        for j, want, key in wants:
            new = want.to_bytes(wb, "little")
            if v[j * wb:(j + 1) * wb] != new:
                v[j * wb:(j + 1) * wb] = new
                st.fixes[key] = st.fixes.get(key, 0) + 1
                st.nan_fixed += key == "nan"
        uc.reg_write(UC_XMM[dst], int.from_bytes(v, "little"))

    def _post_apply(self, uc, st):
        """After an instruction with a register correction (statefix.py) completed: apply it, and
        count the case under the correction's key if it changed anything."""
        key, patches = st.post
        st.post = None
        if key == "bscan":              # (register, its 64 bits before the instruction): BSCAN_RULE
            reg, old = patches
            if uc.reg_read(U.UC_X86_REG_EFLAGS) & (1 << FLAG_BITS["zf"]) and uc.reg_read(reg) != old:
                uc.reg_write(reg, old)
                st.fixes[key] = 1
            return
        for reg, keep, value in patches:
            r = U.UC_X86_REG_RCX if reg == "rcx" else U.UC_X86_REG_EFLAGS
            old = uc.reg_read(r)
            new = (old & keep & M64) | value
            if new != old:
                uc.reg_write(r, new)
                st.fixes[key] = 1

    def _take_back(self):
        """The page lent to one instruction is unmapped again before anything else can read it."""
        self.uc.mem_unmap(self.lent, 0x1000)
        self.lent = None

    def _code(self, uc, addr, size, _):
        st = self.st
        if self.lent is not None:
            self._take_back()
        if st.fp is not None:
            self._fp_apply(uc, st)
        if st.post is not None:
            self._post_apply(uc, st)
        st.n += 1
        if st.n > INSN_LIMIT and (st.n > INSN_LIMIT + INSN_GRACE or not self.cutting):
            st.stop = "instruction limit"
            uc.emu_stop()
            return
        if st.jcc is not None:               # the previous instruction was a conditional branch
            self._branch_outcome(st, addr)
        if st.pending is not None:           # the previous instruction was a call: its target
            st.event = "call"
            uc.emu_stop()
            return
        for b, e in st.ranges:
            if b <= addr < e:
                break
        else:
            st.event = "leave"
            uc.emu_stop()
            return
        info = self.icache.get(addr) or self._info(addr)
        st.prev = addr
        if addr in st.stops:                 # the lifted code stops here: so does the case, before the instruction
            st.trap = ("trap", addr - IMAGE_BASE, UNTRANSLATED_WHAT)
            uc.emu_stop()
            return
        if addr in st.table_stops:
            reg, targets = st.table_stops[addr]
            if uc.reg_read(reg) not in targets:
                st.trap = ("trap", addr - IMAGE_BASE, TABLE_MISS_WHAT)
                uc.emu_stop()
                return
        st.cov.add(addr)
        k = info[0]
        if k == "call":
            st.pending = (addr, uc.reg_read(U.UC_X86_REG_RSP))
        elif k == "jcc":
            st.jcc = (info[3], addr + info[2])
        elif k == "cpuid" or k == "rdtsc":
            st.event = k
            uc.emu_stop()
        elif k == "bscan":                   # bsf/bsr r32: a zero source leaves all 64 bits (BSCAN_RULE)
            st.post = ("bscan", (info[3], uc.reg_read(info[3])))
        elif k == "oracle-bug":              # the oracle is known to execute this one wrongly
            st.stop = info[3]
            uc.emu_stop()
        elif k == "ldmxcsr":
            # Intel SDM, LDMXCSR, #GP(0): "For an attempt to set reserved bits in MXCSR."  Bits
            # 16-31 are reserved (MXCSR_MASK is 0xFFFF); a Core i5-7600K raises #GP for 0x11f80
            # and 0x80001f80 and loads 0xffff (tools/hworacle, 2026-10-09).  Unicorn 2.1.4 loads
            # any value silently, so the fault is decided here: a defined trap, like the others.
            ins = info[3]
            ea = ea_of(ins, ins.operands[0], [uc.reg_read(r) for r in UC_GPR], uc.reg_read(U.UC_X86_REG_GS_BASE), rip_base=0)
            try:
                v = int.from_bytes(uc.mem_read(ea, 4), "little")
            except unicorn.UcError:
                v = 0                                    # unmapped: the instruction itself faults
            if v & 0xFFFF0000:
                st.trap = ("trap", addr - IMAGE_BASE, "#GP (ldmxcsr sets reserved bits of MXCSR)")
                uc.emu_stop()
        elif k == "popfq":
            # statefix.py: a value with TF, NT, AC or ID is a trap in lifted code, so a defined
            # trap here; otherwise the emulator's ring-0 popfq runs and IF and IOPL are put back.
            try:
                v = int.from_bytes(uc.mem_read(uc.reg_read(U.UC_X86_REG_RSP), 8), "little")
            except unicorn.UcError:
                return                                   # unmapped: the instruction itself faults
            if v & SF.POPF_TRAP:
                st.trap = ("trap", addr - IMAGE_BASE, SF.POPF_TRAP_WHAT)
                uc.emu_stop()
            else:
                st.post = ("popfq", [("eflags",) + SF.popfq_patch()])
        elif k == "pcmpistri":
            # statefix.py: the emulator's equal-ordered aggregation misses a match that runs off
            # the end; ECX, CF and OF come from the SDM's definition.
            ins, (xa, src, ctl) = info[3], info[4]
            try:
                b = uc.reg_read(UC_XMM[XMM[src.reg]]).to_bytes(16, "little") if src.type == X.X86_OP_REG \
                    else bytes(uc.mem_read(self._ea(uc, ins, src), 16))
            except unicorn.UcError:
                return                                   # the instruction is about to fault
            st.post = ("pcmpistri", SF.pcmpistri_wants(uc.reg_read(UC_XMM[xa]).to_bytes(16, "little"), b, ctl))
        elif len(info) > 3 and k == "other":  # an instruction with an oracle compensation (oraclefix.py)
            ins, plan = info[3], info[4]
            if plan.align is not None and self._ea(uc, ins, plan.align) & 15:
                # #GP before anything is read or written; Unicorn would execute the instruction
                st.trap = ("trap", addr - IMAGE_BASE, OF.ALIGN_WHAT)
                st.fixes["align"] = 1
                uc.emu_stop()
            elif plan.unjudged is not None:  # rcp/rsqrt: the emulator's result is no reference (oraclefix.py)
                st.stop = plan.unjudged
                uc.emu_stop()
            elif plan.value is not None:
                self._fp_capture(uc, st, ins, plan.value)

    def _branch_outcome(self, st, addr):
        """Coverage (review finding 1): which way the conditional branch at st.prev went,
        given the address execution reached next."""
        target, fall = st.jcc
        st.jcc = None
        if target != fall:
            if addr == target:
                st.br[st.prev] = st.br.get(st.prev, 0) | BR_TAKEN
            elif addr == fall:
                st.br[st.prev] = st.br.get(st.prev, 0) | BR_NOT_TAKEN

    def _write(self, uc, access, addr, size, value, _):
        self.written.add(addr >> 12)
        self.written.add((addr + size - 1) >> 12)

    def _bad_access(self, uc, access, addr, size, value, _):
        self.fault_addr = addr          # diagnostics only: where the oracle faulted
        st = self.st
        info = self.icache.get(st.prev, ()) if st is not None else ()
        if access == unicorn.UC_MEM_READ_UNMAPPED and len(info) > 4 and info[0] == "other" and self.lent is None \
                and info[4].overread is not None:
            # oraclefix.py: the emulator loads 16 bytes for an 8-byte operand.  If the operand's own
            # 8 bytes are mapped, the rest reads as zeros (it never reaches the result) and the
            # instruction runs; the page goes away again before the next instruction (_code, run).
            ea = self._ea(uc, info[3], info[4].overread)
            page = OF.overread_page(ea, addr)
            try:
                if page is not None:
                    uc.mem_read(ea, 8)
                    uc.mem_map(page, 0x1000, unicorn.UC_PROT_READ)
                    self.lent = page
                    st.fixes["m64"] = 1
                    return True
            except unicorn.UcError:
                pass
        if self.paging is not None and st is not None:
            return self._map_on_demand(uc, addr, size)
        return False

    def _operands(self, uc):
        """(address, size) of every memory access the running instruction can make, with all 64
        bits of the address: its explicit memory operands (string instructions have theirs
        explicit too) and, for a stack instruction, the 32 bytes around rsp, and rbp for leave."""
        addr = self.st.prev
        ins = self.dcache.get(addr)
        if ins is None:
            off = addr - IMAGE_BASE
            ins = self.dcache[addr] = next(self.md.disasm(self.image[off:off + 15], addr, 1), None)
        if ins is None:
            return []
        out = [(self._ea(uc, ins, op) & M64, max(op.size, 1)) for op in ins.operands if op.type == X.X86_OP_MEM]
        name = ins.insn_name()
        if name in STACK_INSNS:
            out.append(((uc.reg_read(U.UC_X86_REG_RSP) - 16) & M64, 32))
        if name == "leave":
            out.append((uc.reg_read(U.UC_X86_REG_RBP), 8))
        return out

    def _map_on_demand(self, uc, addr, size):
        """Demand paging (demand_page): an access met the unmapped page of `addr`, which is the
        address as Unicorn cuts it (UC_ADDR_MASK).  If an operand of the instruction accounts
        for that page and lies, with all its 64 bits, where the candidate can map too, map the
        page with its contents and let the instruction go on.  Otherwise the fault stands: for
        an operand elsewhere with its real address on record, and for an access no operand
        accounts for (the emulator reading past an operand) as it is."""
        q = addr >> 12
        found = False
        for a, n in self._operands(uc):
            b = (a + n - 1) & M64
            if q not in ((a & UC_ADDR_MASK) >> 12, (b & UC_ADDR_MASK) >> 12):
                continue
            if not (demand_ok(a) and demand_ok(b)):
                self.fault_addr = a
                return False
            found = True
        if not found or q in self.demand or self._pristine(q) is not None:
            return False
        if len(self.demand) >= DEMAND_MAX:
            self.st.stop = DEMAND_LIMIT
            return False
        data = demand_page(self.paging, q << 12)
        uc.mem_map(q << 12, PAGE, unicorn.UC_PROT_READ | unicorn.UC_PROT_WRITE)
        uc.mem_write(q << 12, data)
        self.demand[q] = data
        return True

    def _intr(self, uc, intno, _):
        self.st.intno = intno           # a CPU exception or software interrupt: classified in _run
        uc.emu_stop()

    def _prev_kind(self, st):
        return self.icache[st.prev][0] if st.prev is not None else None

    def _pristine(self, page):
        a = page << 12
        if IMAGE_BASE <= a < IMAGE_BASE + len(self.image):
            return self.cur_image[a - IMAGE_BASE:a - IMAGE_BASE + PAGE]
        if ARENA_BASE <= a < ARENA_BASE + ARENA_SIZE:
            return self.mem[a - ARENA_BASE:a - ARENA_BASE + PAGE]
        if STACK_BASE <= a < STACK_BASE + STACK_SIZE:
            o = ARENA_SIZE + a - STACK_BASE
            return self.mem[o:o + PAGE]
        return None

    def run(self, case):
        """Returns {'status': 'ok'|'inconclusive', ...}."""
        uc = self.uc
        st = self.st = _Run()
        st.ranges = [(IMAGE_BASE + b, IMAGE_BASE + e) for b, e in case.ranges]
        st.n, st.stop, st.event, st.pending, st.prev, st.intno = 0, None, None, None, None, None
        st.cov, st.br, st.jcc = set(), {}, None
        st.fp, st.nan_fixed, st.trap, st.fixes, st.post = None, 0, None, {}, None
        st.stops = frozenset(IMAGE_BASE + a for a in case.stops)
        st.table_stops = {IMAGE_BASE + j: (UC_GPR[reg], frozenset(IMAGE_BASE + t for t in targets))
                          for j, reg, targets in case.table_stops}
        self.written = set()
        self.fault_addr = None
        self.paging = case.seed if case.paging else None
        self.demand = {}
        self.cutting = bool(case.cut)
        self.probed = 0
        uc.context_restore(self.cpu0)
        for i in range(16):
            uc.reg_write(UC_GPR[i], case.regs[i])
            uc.reg_write(UC_XMM[i], int.from_bytes(case.xmm[16 * i:16 * i + 16], "little"))
        uc.reg_write(U.UC_X86_REG_EFLAGS, case.eflags | SF.START_RFLAGS)      # IF = 1: user mode (statefix.py)
        uc.reg_write(U.UC_X86_REG_GS_BASE, case.gs_base)
        uc.reg_write(U.UC_X86_REG_MXCSR, case.mxcsr)
        uc.reg_write(U.UC_X86_REG_FPCW, case.fpcw)
        end = None if case.end is None else IMAGE_BASE + case.end
        # Pokes are part of the initial state: pages are reported relative to the poked contents.
        initial = {}
        for addr, data in case.pokes:
            if any(self._pristine(q) is None for q in (addr >> 12, (addr + len(data) - 1) >> 12)):
                raise ValueError("poke outside the image, arena and stack: %#x" % addr)
            uc.mem_write(addr, bytes(data))
            for q in range(addr >> 12, ((addr + len(data) - 1) >> 12) + 1):
                initial[q] = None
        for q in initial:
            initial[q] = bytes(uc.mem_read(q << 12, PAGE))
        try:
            out = self._run(case, st, end)
        finally:
            pages = {}
            for p in self.written | set(initial):
                pr = self._pristine(p)
                if pr is None:
                    continue
                now = bytes(uc.mem_read(p << 12, PAGE))
                if p in self.written and now != initial.get(p, pr):
                    pages[p << 12] = now
                if now != pr:
                    uc.mem_write(p << 12, pr)
            # Demand pages are compared like any other page, against the contents they were
            # mapped with, and go away again: the next case starts without them.
            demand = sorted(q << 12 for q in self.demand)
            for q, pr in self.demand.items():
                now = bytes(uc.mem_read(q << 12, PAGE))
                if now != pr:
                    pages[q << 12] = now
                uc.mem_unmap(q << 12, PAGE)
            self.demand = {}
            self.paging = None
        if isinstance(out, str):
            # Issue 44: where it stopped.  `rva` is the last instruction the oracle started
            # inside the function; `address` is the unmapped address for a memory fault.
            res = {"status": "inconclusive", "reason": out, "probed": self.probed,
                   "rva": None if st.prev is None else st.prev - IMAGE_BASE, "address": None}
            if "UNMAPPED" in out:
                rip = uc.reg_read(U.UC_X86_REG_RIP)
                res["address"] = rip if "FETCH" in out else self.fault_addr
            return res
        regs = [uc.reg_read(UC_GPR[i]) for i in range(16)]
        xmm = b"".join(uc.reg_read(UC_XMM[i]).to_bytes(16, "little") for i in range(16))
        res = {"status": "ok", "regs": regs, "eflags": uc.reg_read(U.UC_X86_REG_EFLAGS) & FLAG_MASK,
               "xmm": xmm, "calls": self.calls, "pages": pages, "mxcsr": uc.reg_read(U.UC_X86_REG_MXCSR),
               "fpcw": uc.reg_read(U.UC_X86_REG_FPCW), "demand": demand,
               "nan_fixed": st.nan_fixed, "fixes": dict(st.fixes, nan=st.nan_fixed)}
        if out is not None and out[0] == "cut":
            res.update(status="cut", cut=(len(self.calls), out[1], out[2]))
        elif out is not None:            # ("trap", rva, what): a defined guest fault
            res.update(status="trap", trap_rva=out[1], trap_what=out[2])
        if end is None:                  # function test: coverage, as RVAs
            res["cov"] = frozenset(a - IMAGE_BASE for a in st.cov)
            res["br"] = {a - IMAGE_BASE: m for a, m in st.br.items()}
        return res

    def _run(self, case, st, end):
        """None: returned to the sentinel (or reached the synthetic end).  ("trap", rva, what):
        stopped at a defined guest fault.  ("cut", kind, target): a case with `cut` met the call
        record's or the instruction limit and stopped at the event that came next, before its
        stub effect; the candidate stops at the event of the same index.  A string:
        inconclusive, with the reason."""
        uc = self.uc
        pc = IMAGE_BASE + case.func_rva
        calls = self.calls = []
        rd = uc.reg_read
        while True:
            st.event = None
            err = None
            try:
                uc.emu_start(pc, SENTINEL)
            except unicorn.UcError as e:
                err = e
            if self.lent is not None:
                self._take_back()
            rip = rd(U.UC_X86_REG_RIP)
            if st.stop:
                return st.stop
            if st.trap:
                return st.trap
            if st.jcc is not None:           # a branch taken to an address that could not be fetched
                self._branch_outcome(st, rip)
            ev = st.event
            pk = self._prev_kind(st)
            if st.intno is not None:
                # #DE from div/idiv, int3 and int 0x29 are defined guest behaviour: the lifted
                # code must trap at the same instruction.  Anything else is inconclusive.
                if (st.intno, pk) in ((0, "div"), (3, "int3"), (0x29, "int29")):
                    return ("trap", st.prev - IMAGE_BASE, TRAP_KINDS[pk])
                if (st.intno, pk) == (13, "cx16"):
                    # Intel SDM, CMPXCHG8B/CMPXCHG16B, 64-Bit Mode Exceptions, #GP(0): "If memory
                    # operand for CMPXCHG16B is not aligned on a 16-byte boundary."  The oracle
                    # implements that (with and without LOCK), and so does the lifted code.
                    ins = self.icache[st.prev][3]
                    ea = ea_of(ins, ins.operands[0], [rd(r) for r in UC_GPR], rd(U.UC_X86_REG_GS_BASE), rip_base=0)
                    if ea & 15:
                        return ("trap", st.prev - IMAGE_BASE, "#GP (cmpxchg16b operand not 16-byte aligned)")
                return "fault: CPU exception %d at image+0x%x" % (st.intno, (st.prev or rip) - IMAGE_BASE)
            at_sentinel = ev is None and rip == SENTINEL and (err is None or err.errno in FETCH_ERRORS)
            if at_sentinel and pk == "ret":
                return None                              # returned to the sentinel
            if at_sentinel:
                # Reached by a jmp or a call, not a ret: a (tail) call whose target happens to
                # be the sentinel address, e.g. 'jmp [rsp]'.  The lifted code records it as
                # one, so the oracle must too (review finding 13).
                ev = "call" if st.pending is not None else "leave"
            if ev is None:
                if err is None:
                    if pk == "hlt" and rip == st.prev + self.icache[st.prev][2]:
                        return ("trap", st.prev - IMAGE_BASE, TRAP_KINDS[pk])
                    return "emulation stopped at image+0x%x" % (rip - IMAGE_BASE)
                if err.errno == unicorn.UC_ERR_INSN_INVALID and pk == "ud2" and rip == st.prev:
                    return ("trap", st.prev - IMAGE_BASE, TRAP_KINDS[pk])
                if err.errno not in FETCH_ERRORS:
                    where = ""
                    if err.errno in (unicorn.UC_ERR_READ_UNMAPPED, unicorn.UC_ERR_WRITE_UNMAPPED) \
                            and self.fault_addr is not None:
                        where = " " + _where(self.fault_addr)
                    return "fault: %s%s" % (err, where)
                if st.pending is not None and rd(U.UC_X86_REG_RSP) == st.pending[1] - 8:
                    ev = "call"
                elif not self._inside(rip):
                    ev = "leave"
                else:
                    return "fault: %s" % err
            rsp = rd(U.UC_X86_REG_RSP)
            idx = len(calls)
            over = idx >= MAX_CALLS or st.n > INSN_LIMIT
            if idx >= MAX_CALLS and not self.cutting:
                return "call record overflow"
            # Everything a callee could read (review finding 3): the stub effect overwrites
            # rax, rcx, rdx, r8-r11 and xmm0-xmm5, so whatever is not recorded here is unchecked.
            snap = (rsp, rd(U.UC_X86_REG_RCX), rd(U.UC_X86_REG_RDX), rd(U.UC_X86_REG_R8), rd(U.UC_X86_REG_R9),
                    rd(U.UC_X86_REG_RAX), rd(U.UC_X86_REG_R10), rd(U.UC_X86_REG_R11))
            xsnap = b"".join(rd(UC_XMM[i]).to_bytes(16, "little") for i in range(6))
            if ev in ("cpuid", "rdtsc"):
                if over:
                    return ("cut", K_CPUID if ev == "cpuid" else K_RDTSC, 0)
                calls.append((K_CPUID if ev == "cpuid" else K_RDTSC, 0) + snap + (0, xsnap))
                s = case.seed
                if ev == "cpuid":
                    for k, r in enumerate((U.UC_X86_REG_RAX, U.UC_X86_REG_RBX, U.UC_X86_REG_RCX, U.UC_X86_REG_RDX)):
                        uc.reg_write(r, hv(s, idx, k) & 0xFFFFFFFF)
                else:
                    uc.reg_write(U.UC_X86_REG_RAX, hv(s, idx, 0) & 0xFFFFFFFF)
                    uc.reg_write(U.UC_X86_REG_RDX, hv(s, idx, 1) & 0xFFFFFFFF)
                pc = rip + self.icache[rip][2]
                if end is not None and pc == end:
                    return None
                continue
            if ev == "call":
                kind = K_CALL_DIRECT if self.icache[st.pending[0]][1] else K_CALL_INDIRECT
                st.pending = None
            else:
                if end is not None and rip == end:
                    return None                          # instruction test: the synthetic end
                if st.prev is None:
                    return "entry is outside the function's ranges"
                p = self.icache[st.prev]
                if p[0] == "ret":
                    return "ret to a non-sentinel address"
                if p[0] == "jmp":
                    kind = K_TAIL_DIRECT if p[1] else K_TAIL_INDIRECT
                elif rip == st.prev + p[2] and self.image[rip - IMAGE_BASE:rip - IMAGE_BASE + 1] == b"\xcc":
                    # Execution runs off the end of the function into int3 padding: the CPU
                    # would execute that int3.  (The lifter emits TRAP(rva) for the same case.)
                    return ("trap", rip - IMAGE_BASE, "int3 after the function")
                else:
                    kind = K_TAIL_DIRECT                 # jcc taken, or fallthrough out of the ranges
            try:
                ra = struct.unpack("<Q", uc.mem_read(rsp, 8))[0]
            except unicorn.UcError:
                return "fault: stack unreadable at a call"
            if over:
                return ("cut", kind, rip)
            calls.append((kind, rip) + snap + (ra, xsnap))
            probe = rip in self.probes and kind in (K_CALL_DIRECT, K_CALL_INDIRECT)
            self.probed += probe
            if not (probe and case.probe):               # the stack probe's contract: it only returns
                s = case.seed
                uc.reg_write(U.UC_X86_REG_RAX, stub_rax(s, hv(s, idx, 0)))
                for k, r in enumerate((U.UC_X86_REG_RCX, U.UC_X86_REG_RDX, U.UC_X86_REG_R8, U.UC_X86_REG_R9,
                                       U.UC_X86_REG_R10, U.UC_X86_REG_R11)):
                    uc.reg_write(r, hv(s, idx, k + 1))
                for i in range(6):
                    uc.reg_write(UC_XMM[i], hv(s, idx, 16 + 2 * i) | (hv(s, idx, 17 + 2 * i) << 64))
            uc.reg_write(U.UC_X86_REG_RSP, (rsp + 8) & M64)
            if kind in (K_TAIL_DIRECT, K_TAIL_INDIRECT):
                if ra != SENTINEL:
                    return "tail call with a non-sentinel return address"
                return None
            pc = ra
            if end is not None and pc == end:
                return None
            if pc == SENTINEL:
                return None


def _where(addr):
    """Coarse description of a faulting address, for the inconclusive-reason histogram.  The
    first three after "near null" are addresses no demand page can stand at (demand_ok): with
    paging on, a memory fault of the oracle is always one of those four."""
    if addr < 0x10000 or addr >= M64 - 0x10000:
        return "[near null]"
    if addr < (1 << 32):
        return "[32-bit value used as an address]"
    if addr < DEMAND_LO:
        return "[below 1 TiB]"
    if addr >= DEMAND_HI:
        return "[above the 47-bit address space]"
    if ARENA_BASE - (1 << 32) <= addr < ARENA_BASE + ARENA_SIZE + (1 << 32):
        return "[off the arena]"
    if STACK_BASE - (1 << 32) <= addr < STACK_BASE + STACK_SIZE + (1 << 32):
        return "[off the stack]"
    if IMAGE_BASE - (1 << 32) <= addr < IMAGE_BASE + (1 << 32):
        return "[off the image]"
    return "[elsewhere]"


# ---- undefined results (the only masks) --------------------------------------------------------

_UNDEF = (("cf", X.X86_EFLAGS_UNDEFINED_CF), ("pf", X.X86_EFLAGS_UNDEFINED_PF), ("af", X.X86_EFLAGS_UNDEFINED_AF),
          ("zf", X.X86_EFLAGS_UNDEFINED_ZF), ("sf", X.X86_EFLAGS_UNDEFINED_SF), ("of", X.X86_EFLAGS_UNDEFINED_OF))


def undefined_after(ins, regs, oracle_eflags):
    """(flags, register indices, drop_reason) that the instruction leaves undefined for this
    input.  Starts from what Capstone reports and refines it where the SDM is more precise."""
    name = ins.insn_name()
    und = {f for f, bit in _UNDEF if ins.eflags & bit}
    und_regs = set()
    drop = None
    ops = ins.operands
    if name in ("shl", "sal", "shr", "sar", "rol", "ror", "shld", "shrd"):
        bits = ops[0].size * 8
        cop = ops[-1] if len(ops) >= 2 else None
        if cop is None:
            cnt = 1
        elif cop.type == X.X86_OP_IMM:
            cnt = cop.imm & 0xFF
        else:
            cnt = regs[1] & 0xFF                         # cl
        n = cnt & (63 if bits == 64 else 31)
        if n == 0:
            # SDM SAL/SAR/SHL/SHR, ROL/ROR, SHLD/SHRD: "If the count is 0, the flags are not
            # affected."  Nothing is undefined; every flag must be unchanged.
            und = set()
        elif name in ("rol", "ror"):
            # SDM RCL/RCR/ROL/ROR: "The OF flag is defined only for the 1-bit rotates; it is
            # undefined in all other cases."  SF ZF AF PF are not affected.
            und = set() if n == 1 else {"of"}
        else:
            # SDM: "The OF flag is affected only for 1-bit shifts; otherwise, it is
            # undefined."  "For a non-zero count, the AF flag is undefined."
            und = {"af"} if n == 1 else {"af", "of"}
            if name in ("shl", "sal", "shr") and n >= bits:
                # SDM: CF "is undefined for SHL and SHR instructions where the count is
                # greater than or equal to the size (in bits) of the destination operand."
                und.add("cf")
            if name in ("shld", "shrd") and n > bits:
                # SDM SHLD/SHRD: "If the count is greater than the operand size, the result
                # is undefined" (and so are the flags).
                drop = "double shift count above the operand size: result undefined (SDM)"
    elif name in ("bsf", "bsr"):
        # SDM BSF/BSR: "If the content of the source operand is 0, the content of the
        # destination operand is undefined."  ZF = 1 exactly in that case.
        if (oracle_eflags >> FLAG_BITS["zf"]) & 1 and ops[0].type == X.X86_OP_REG and ops[0].reg in GPR:
            und_regs.add(GPR[ops[0].reg][1])
    return und, und_regs, drop


def oracle_corrections(ins, case, ora):
    """Known deviations of the oracle (Unicorn 2.1.4, built on QEMU 5.0) from the SDM.
    Returns the corrected expected registers.  Applied in the instruction test only.

    CMPXCHG r/m32, r32 (SDM "Operation": IF accumulator = DEST THEN ZF <- 1; DEST <- SRC;
    ELSE ZF <- 0; accumulator <- DEST): only the register that is actually written is
    zero-extended.  QEMU 5.0 always wrote both EAX and a register destination, clearing
    their upper halves.  Fixed upstream by commit d1bb978ba1 ("target/i386: fix cmpxchg with
    32-bit register destination", QEMU issue 508), first released in QEMU 7.2.0 - its
    regression test tests/tcg/x86_64/cmpxchg.c exists at v7.2.0 and not at v7.1.0 (checked by
    the review of 2026-10-08).  The lifted code follows the SDM.

    Not applied in the function test: a function that keeps using the upper half of rax
    after a successful 32-bit cmpxchg would be reported failing there (a false failure).
    """
    regs = list(ora["regs"])
    ops = ins.operands
    if ins.insn_name() == "cmpxchg" and ops[0].size == 4:
        zf = (ora["eflags"] >> FLAG_BITS["zf"]) & 1
        dreg = GPR[ops[0].reg][1] if ops[0].type == X.X86_OP_REG and ops[0].reg in GPR else None
        if zf and dreg != 0:
            regs[0] = case.regs[0]                  # equal: the accumulator is not written
        if not zf and dreg not in (None, 0):
            regs[dreg] = case.regs[dreg]            # not equal: a register destination is not written
    return regs


# ---- comparison ----------------------------------------------------------------------------------

def compare(case, ora, cand, mode, md, image):
    """List of mismatch descriptions (empty = agree), or ('drop', reason)."""
    out = []
    how = "%s%s" % (ST_NAMES.get(cand["status"], "?"), " (signal %d%s)" % (
                        cand["aux"], " at address %#x" % cand["final_ra"] if cand["aux"] in (10, 11) and cand["final_ra"] != M64 else "")
                    if cand["status"] == 2 else
                    " (at rva 0x%x)" % cand["aux"] if cand["status"] == 1 else
                    " (event %d, %s to %#x)" % (len(cand["calls"]), KIND_NAMES.get(cand["aux"], cand["aux"]), cand["final_ra"])
                    if cand["status"] == ST_CUT else
                    " (%s)" % FPMODE_WHAT.get(cand["aux"], "?") if cand["status"] == 8 else "")
    if cand["status"] == ST_NOMAP:
        # The oracle mapped a demand page where this driver process already has something of its
        # own.  The two sides did not start from the same state, so there is nothing to compare.
        return ("drop", "the candidate could not map a demand page (host page %#x)" % (cand["final_ra"]))
    if ora["status"] == "cut":
        # The case met a limit and ended at an event (Oracle._run): the candidate must have
        # stopped at the event of the same index, the same kind and the same target, and the
        # state there is compared like any other result.
        k, kind, target = ora["cut"]
        if (cand["status"], cand["aux"], cand["final_ra"], len(cand["calls"])) != (ST_CUT, kind, target, k):
            return ["oracle: stopped at event %d (%s to %#x), where the case's limit ends it; candidate: %s" % (
                k, KIND_NAMES.get(kind, kind), target, how)]
    elif ora["status"] == "trap":
        # A defined guest fault (review finding 4): the candidate must trap at the same
        # instruction, and the state at the trap is compared like any other result.
        if cand["status"] != 1 or cand["aux"] != ora["trap_rva"]:
            return ["oracle: %s at rva 0x%x; candidate: %s" % (ora["trap_what"], ora["trap_rva"], how)]
    elif cand["status"] != 0:
        return ["candidate: " + how]
    und, und_regs = set(), set()
    flags = ("df",)
    if mode == "insn":
        off = case.insn_rva
        ins = next(md.disasm(bytes(image[off:off + 15]), off, 1))
        und, und_regs, drop = undefined_after(ins, case.regs, ora["eflags"])
        if drop:
            return ("drop", drop)
        flags = STATUS_FLAGS + ("df",)
        expect = oracle_corrections(ins, case, ora)
    else:
        expect = ora["regs"]
    for i in range(16):
        if i not in und_regs and expect[i] != cand["regs"][i]:
            out.append("%s: oracle %#x candidate %#x" % (REG_NAMES[i], expect[i], cand["regs"][i]))
    for f in flags:
        if f in und:
            continue
        a, b = (ora["eflags"] >> FLAG_BITS[f]) & 1, (cand["eflags"] >> FLAG_BITS[f]) & 1
        if a != b:
            out.append("%s: oracle %d candidate %d" % (f, a, b))
    for i in range(16):
        a, b = ora["xmm"][16 * i:16 * i + 16], cand["xmm"][16 * i:16 * i + 16]
        if a != b:
            out.append("xmm%d: oracle %s candidate %s" % (i, a.hex(), b.hex()))
    # MXCSR without its six status flags: lifted code does not model them (DESIGN.md section 4),
    # so they are masked here, by name, and everything else in the register is compared.
    a, b = ora["mxcsr"] & ~FC.MXCSR_STATUS, cand["mxcsr"] & ~FC.MXCSR_STATUS
    if a != b:
        out.append("mxcsr (status flags masked): oracle %#x candidate %#x" % (a, b))
    # The x87 control word.  A hardware answer has none (the runner's cases do not hold x87 state).
    if "fpcw" in ora and ora["fpcw"] != cand["fpcw"]:
        out.append("x87 control word: oracle %#x candidate %#x" % (ora["fpcw"], cand["fpcw"]))
    ca, cb = ora["calls"], cand["calls"]
    if len(ca) != len(cb):
        out.append("call record: oracle has %d events, candidate %d" % (len(ca), len(cb)))
    for k, (x, y) in enumerate(zip(ca, cb)):
        if tuple(x) != tuple(y):
            out.append("call record[%d] differs in %s: oracle %s candidate %s" % (
                k, ",".join(_call_diff(x, y)), _fmt_call(x), _fmt_call(y)))
            break
    pa, pb = ora["pages"], cand["pages"]
    for addr in sorted(set(pa) | set(pb)):
        a, b = pa.get(addr), pb.get(addr)
        if a is None or b is None:
            out.append("memory page %#x changed only on the %s side" % (addr, "candidate" if a is None else "oracle"))
        elif a != b:
            k = next(i for i in range(PAGE) if a[i] != b[i])
            out.append("memory at %#x: oracle %s candidate %s" % (addr + k, a[k:k + 8].hex(), b[k:k + 8].hex()))
    return out


CALL_FIELDS = ("kind", "target", "rsp", "rcx", "rdx", "r8", "r9", "rax", "r10", "r11", "[rsp]", "xmm0-5")


def _fmt_call(c):
    return "(%s %s xmm0-5=%s)" % (KIND_NAMES.get(c[0], c[0]),
                                  " ".join("%s=%#x" % (n, v) for n, v in zip(CALL_FIELDS[1:11], c[1:11])),
                                  bytes(c[11]).hex())


def _call_diff(x, y):
    """Names of the call-record fields that differ."""
    out = [n for n, a, b in zip(CALL_FIELDS[:11], x, y) if a != b]
    xa, xb = bytes(x[11]), bytes(y[11])
    out += ["xmm%d" % i for i in range(6) if xa[16 * i:16 * i + 16] != xb[16 * i:16 * i + 16]]
    return out


# ---- oracle workers -------------------------------------------------------------------------------

_W = {}


def map_file(path):
    """A file's contents without holding them in this process: a read-only mapping, with the
    length and the slices of the bytes."""
    with open(path, "rb") as f:
        return mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)


def _worker_init(image_paths, mem_paths, mem_images, probes=()):
    _W.pop("session", None)
    _W["images"] = {}
    _W["image_paths"] = image_paths
    _W["images"][0] = map_file(image_paths[0])
    _W["oracle"] = Oracle(_W["images"][0], probes)
    _W["mem_paths"] = mem_paths
    _W["mem_images"] = mem_images
    _W["cur"] = None


def _worker_run(cases):
    o = _W["oracle"]
    out = []
    for c in cases:
        if c.mem_id != _W["cur"]:
            k = _W["mem_images"][c.mem_id]
            if k not in _W["images"]:
                _W["images"][k] = map_file(_W["image_paths"][k])
            with open(_W["mem_paths"][c.mem_id], "rb") as f:
                o.load_mem(f.read(), _W["images"][k])
            _W["cur"] = c.mem_id
        try:
            out.append((c.id, o.run(c)))
        except Exception as e:                           # an oracle bug must not look like a pass
            out.append((c.id, {"status": "inconclusive", "reason": "oracle error: %r" % (e,)}))
    return out


# ---- session: one image, one lifted directory ------------------------------------------------------

# How lifted C is compiled, here and in the linked build (DESIGN.md section 8, "Float instructions").
#   -ffp-contract=off                       a*b+c must stay mulss then addss, never one fused fmadd
#   -fno-vectorize -fno-slp-vectorize       the vectorisers build the mask 0x7fff...f as fneg(all-ones),
#                                           and fneg leaves a NaN bit pattern alone in the guest float mode
LIFTED_CFLAGS = ["-fno-strict-aliasing", "-fwrapv", "-ffp-contract=off", "-fno-vectorize", "-fno-slp-vectorize"]


def mask_hazards(obj):
    """The fneg and fabs instructions in a compiled object of lifted code, as text.  There must
    be none: in the guest float mode (FPCR.AH) they return a NaN bit pattern unchanged, lifted
    code never needs them (x86 changes signs with bitwise instructions), and the one way they
    got in so far is the compiler building a sign mask with them.  This is the check that
    notices if LIFTED_CFLAGS loses a flag."""
    r = subprocess.run(["objdump", "-d", "--no-show-raw-insn", obj], capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError("objdump failed on %s: %s" % (obj, r.stderr.strip()[:200]))
    return ["%s: %s" % (os.path.basename(obj), " ".join(ln.split()))
            for ln in r.stdout.split("\n") if "\tfneg" in ln or "\tfabs" in ln]



def default_jobs():
    """Worker processes when --jobs is not given: all but two cores on a quiet machine, a third of
    them when the machine is already busy (1-minute load above the core count).  Several family
    checks run side by side in a wave; at nine processes each they pushed the load average past
    170 on eleven cores (2026-10-09) and the 10 s limit per case came within reach."""
    n = os.cpu_count() or 4
    try:
        busy = os.getloadavg()[0] > n
    except OSError:
        busy = False
    return max(2, n // 3) if busy else max(2, n - 2)

class Session:
    def __init__(self, image, lifted_dir, out_dir, opt="-O1", jobs=None, work_dir=None, prepared=False):
        """`out_dir` holds what depends only on the module: the image, its variants, the memory
        files and the driver.  `work_dir` (default: out_dir) holds what one run writes: the case
        and result files.  `prepared`: out_dir was filled by an earlier session of the same
        module and lift (tools/functest: one module is tested in many processes, several at a
        time) and is only read here; a file that is missing or is not what this session would
        have written is an error, never rewritten."""
        self.image = image
        self.mod = image.name
        self.lifted_dir = lifted_dir
        self.out_dir = out_dir
        self.work_dir = work_dir or out_dir
        self.prepared = prepared
        self.opt = opt
        self.jobs = jobs or default_jobs()
        os.makedirs(out_dir, exist_ok=True)
        os.makedirs(self.work_dir, exist_ok=True)
        data = image.relocated(IMAGE_BASE, FAKE_IMPORT_BASE)
        data = bytes(data) + bytes(round_up(len(data), 0x4000) - len(data))
        self.image_bytes = data
        self.image_path = os.path.join(out_dir, "image.bin")
        self._put(self.image_path, data)
        if prepared:
            self.image_bytes = map_file(self.image_path)     # the same bytes (_put), not held by this process
        self.image_paths = [self.image_path]     # [0] is the image; the rest are data variants
        self.mem_paths = []
        self.mem_kinds = []
        self.mem_images = []                     # which image each memory file goes with
        self.ora = {}
        self.probes = ()                         # addresses of the stack probe (probe_targets), for both sides
        self.keep_oracle = False                 # run_oracle: one in-process oracle for all calls of this session
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        self.md.detail = True

    def _put(self, path, data):
        """Write a file of out_dir; in a prepared directory, check instead that it is there
        with these contents."""
        if not self.prepared:
            with open(path, "wb") as f:
                f.write(data)
            return
        try:
            with open(path, "rb") as f:
                same = f.read() == data
        except OSError:
            same = False
        if not same:
            raise RuntimeError("%s is missing from the prepared directory or is not this session's" % path)

    def add_mem(self, kind, seed, image_id=0):
        path = os.path.join(self.out_dir, "mem_%s_%d.bin" % (kind, seed))
        if path not in self.mem_paths:
            self._put(path, make_mem(kind, seed))
        self.mem_paths.append(path)
        self.mem_kinds.append((kind, seed))
        self.mem_images.append(image_id)
        return len(self.mem_paths) - 1

    def add_image_variant(self, name, data):
        """A copy of the image with different *data* (same size, same executable bytes).  `data`
        may be a function that makes it: in a prepared directory the variant is the file the
        preparing session wrote (same size and same code are checked, the data is not made
        again: for a large module that is seconds per variant)."""
        path = os.path.join(self.out_dir, "image_%s.bin" % name)
        if self.prepared:
            try:
                data = map_file(path)
            except (OSError, ValueError):
                raise RuntimeError("%s is missing from the prepared directory" % path)
        elif callable(data):
            data = data()
        assert len(data) == len(self.image_bytes)
        for a, b in self.image.exec_ranges:
            assert data[a:b] == self.image_bytes[a:b], "an image variant must not change code"
        if not self.prepared:
            with open(path, "wb") as f:
                f.write(data)
        self.image_paths.append(path)
        return len(self.image_paths) - 1

    def peeker(self, mem_id):
        """peek(addr, n) over the initial memory of one mem image (None if unmapped)."""
        mem = make_mem(*self.mem_kinds[mem_id])
        img = self.image_bytes

        def peek(addr, n):
            for base, buf in ((ARENA_BASE, mem[:ARENA_SIZE]), (STACK_BASE, mem[ARENA_SIZE:]), (IMAGE_BASE, img)):
                if base <= addr and addr + n <= base + len(buf):
                    return int.from_bytes(buf[addr - base:addr - base + n], "little")
            return None
        return peek

    def build_driver(self):
        if self.prepared:
            self.driver = os.path.join(self.out_dir, "driver")
            if not os.access(self.driver, os.X_OK):
                raise RuntimeError("%s is missing from the prepared directory" % self.driver)
            return
        obj_dir = os.path.join(self.out_dir, "obj")
        os.makedirs(obj_dir, exist_ok=True)
        for f in glob.glob(os.path.join(obj_dir, "*.o")):
            os.remove(f)
        srcs = sorted(glob.glob(os.path.join(self.lifted_dir, self.mod + "_*.c"))) + [os.path.join(HERE, "driver.c")]
        flags = ["clang"] + self.opt.split() + LIFTED_CFLAGS + ["-g0", "-DHARNESS_ISOLATED",
                 "-DMODULE=" + self.mod, "-I" + HERE, "-I" + self.lifted_dir, "-Wall", "-Wno-unused-variable",
                 "-Wno-unused-but-set-variable", "-Wno-unused-label"]

        def cc(src):
            obj = os.path.join(obj_dir, os.path.basename(src)[:-2] + ".o")
            r = subprocess.run(flags + ["-c", src, "-o", obj], capture_output=True, text=True)
            return obj, r

        with ThreadPoolExecutor(self.jobs) as ex:
            res = list(ex.map(cc, srcs))
        for obj, r in res:
            if r.returncode != 0 or r.stderr.strip():
                sys.stderr.write(r.stderr)
            if r.returncode != 0:
                raise RuntimeError("compiling lifted C failed: %s" % obj)
        with ThreadPoolExecutor(self.jobs) as ex:
            hazards = [h for hs in ex.map(mask_hazards, [o for o, _ in res if not o.endswith("driver.o")]) for h in hs]
        if hazards:
            raise RuntimeError("lifted code contains fneg/fabs, which the guest float mode changes on NaN bit "
                               "patterns (DESIGN.md section 8): %s" % "; ".join(hazards[:4]))
        self.driver = os.path.join(self.out_dir, "driver")
        r = subprocess.run(["clang"] + [f for f in self.opt.split() if f.startswith("-fsanitize")]
                           + ["-o", self.driver] + [o for o, _ in res], capture_output=True, text=True)
        if r.returncode != 0:
            sys.stderr.write(r.stderr)
            raise RuntimeError("linking the driver failed")

    # -- oracle --

    def run_oracle(self, cases):
        chunks = defaultdict(list)
        for c in cases:
            chunks[c.mem_id].append(c)
        tasks = []
        # by image, then memory: changing the image is writing all of it into the emulator
        for mem_id in sorted(chunks, key=lambda m: (self.mem_images[m], m)):
            cs = chunks[mem_id]
            step = max(1, min(400, len(cs) // (self.jobs * 2) + 1))
            tasks += [cs[i:i + step] for i in range(0, len(cs), step)]
        out = {}
        if self.jobs == 1 or len(cases) < 64:
            # A new oracle for every call, unless the session keeps its own (function_test with
            # `batch` calls this once per round, and the files of a session never change).  The
            # self-tests rely on the new one: they switch a compensation off between two calls,
            # and an oracle remembers what it decoded.
            mine = (self, len(self.image_paths), len(self.mem_paths), tuple(self.probes))
            if not self.keep_oracle or _W.get("session") != mine:
                _worker_init(self.image_paths, self.mem_paths, self.mem_images, self.probes)
                _W["session"] = mine
            for t in tasks:
                out.update(_worker_run(t))
        else:
            with Pool(self.jobs, _worker_init, (self.image_paths, self.mem_paths, self.mem_images, self.probes)) as pool:
                for res in pool.imap_unordered(_worker_run, tasks):
                    out.update(res)
        return out

    # -- candidate --

    def run_candidate(self, cases):
        order = lambda c: (self.mem_images[c.mem_id], c.mem_id, c.id)       # noqa: E731  (as in run_oracle)
        cases = sorted(cases, key=order)
        n = max(1, min(self.jobs, len(cases) // 200 + 1))
        parts = [cases[i::n] for i in range(n)]
        parts = [sorted(p, key=order) for p in parts if p]

        def run(k):
            cpath = os.path.join(self.work_dir, "cases_%d.bin" % k)
            rpath = os.path.join(self.work_dir, "results_%d.bin" % k)
            def path256(p):
                b = os.path.abspath(p).encode()
                if len(b) > 255:
                    raise RuntimeError("path too long: %s" % p)
                return b + bytes(256 - len(b))

            with open(cpath, "wb") as f:
                f.write(struct.pack("<IIII8Q", 0x33434654, len(self.image_paths), len(self.mem_paths), len(parts[k]),
                                    IMAGE_BASE, len(self.image_bytes), ARENA_BASE, ARENA_SIZE, STACK_BASE, STACK_SIZE,
                                    SENTINEL, len(self.probes)))
                for p in self.image_paths:
                    f.write(path256(p))
                for p, img in zip(self.mem_paths, self.mem_images):
                    f.write(path256(p) + struct.pack("<II", img, 0))
                f.write(struct.pack("<%dQ" % len(self.probes), *self.probes))
                for c in parts[k]:
                    blob = b"".join(struct.pack("<QQ", a, len(d)) + bytes(d) + bytes(-len(d) % 8) for a, d in c.pokes)
                    f.write(struct.pack("<IIIIQ16QQIIIIII", c.id, c.mem_id, c.func_rva, c.eflags, c.seed, *c.regs,
                                        c.gs_base, c.mxcsr, len(blob), c.fpcw, len(c.demand), c.cut_at, int(c.probe)))
                    f.write(c.xmm)
                    f.write(blob)
                    f.write(struct.pack("<%dQ" % len(c.demand), *c.demand))
            r = subprocess.run([self.driver, cpath, rpath], capture_output=True, text=True)
            if r.returncode != 0:
                raise RuntimeError("driver failed: %s" % r.stderr)
            return parse_results(rpath)

        out = {}
        with ThreadPoolExecutor(n) as ex:
            for res in ex.map(run, range(len(parts))):
                out.update(res)
        return out

    # -- both --

    def run(self, cases, mode, adjust=None, again=None):
        """Returns {case.id: ('pass'|'stopped'|'fail'|'inconclusive', detail)}.  'stopped': the
        case arrived at an instruction the lifter does not translate (Case.stops), or at a jump
        through an unbounded table with an address that is no case (Case.table_stops), and both
        sides stopped there in the same state; detail is the RVA.  `adjust(ora)` may replace
        expected results before the candidate runs (the real CPU for float instructions).
        `again(case, result)` may change a case the oracle found inconclusive and return True to
        have the oracle run it once more; the second result stands."""
        t0 = time.time()
        ora = self.ora = self.run_oracle(cases)
        self.rerun = [c for c in cases if again and ora[c.id]["status"] == "inconclusive" and again(c, ora[c.id])]
        if self.rerun:
            ora.update(self.run_oracle(self.rerun))
        self.nan_fixed = sum(1 for o in ora.values() if o.get("nan_fixed"))     # cases the NaN correction changed
        # cases each compensation changed in the emulator's result, by its name in OF.COMPENSATIONS
        self.fixes = Counter(n for o in ora.values() for n, v in o.get("fixes", {}).items() if v)
        if adjust:
            adjust(ora)
        t1 = time.time()
        live = [c for c in cases if ora[c.id]["status"] in ("ok", "trap", "cut")]
        for c in live:
            c.demand = ora[c.id].get("demand", ())       # the candidate starts with the pages the oracle mapped
            c.cut_at = ora[c.id]["cut"][0] if ora[c.id]["status"] == "cut" else NO_CUT
        self.oracle_cuts = sum(1 for c in live if ora[c.id]["status"] == "cut")
        self.demand_cases = sum(1 for c in live if c.demand)
        self.demand_pages = sum(len(c.demand) for c in live)
        cand = self.run_candidate(live) if live else {}
        self.seconds = (round(t1 - t0, 1), round(time.time() - t1, 1))       # oracle, candidate
        at_stop = {c.id for c in live if ora[c.id]["status"] == "trap" and ora[c.id]["trap_what"] in STOP_WHATS}
        self.oracle_stops = len(at_stop)
        self.oracle_traps = sum(1 for c in live if ora[c.id]["status"] == "trap") - len(at_stop)
        verdicts = {}
        for c in cases:
            o = ora[c.id]
            if o["status"] == "inconclusive":
                verdicts[c.id] = ("inconclusive", o["reason"], {"rva": o.get("rva"), "address": o.get("address")})
                continue
            if c.id not in cand:
                verdicts[c.id] = ("fail", ["candidate produced no result"])
                continue
            diff = compare(c, o, cand[c.id], mode, self.md, self.image.data)
            if isinstance(diff, tuple):
                verdicts[c.id] = ("inconclusive", diff[1], {"rva": None, "address": None})
            elif diff:
                verdicts[c.id] = ("fail", diff)
            elif c.id in at_stop:
                verdicts[c.id] = ("stopped", o["trap_rva"])
            else:
                verdicts[c.id] = ("pass", None)
        return verdicts


def parse_results(path):
    out = {}
    with open(path, "rb") as f:
        data = f.read()
    pos = 0
    while pos < len(data):
        (ln,) = struct.unpack_from("<I", data, pos)
        pos += 4
        rec = data[pos:pos + ln]
        pos += ln
        h = struct.unpack_from("<IIII16Q256sQIIII", rec, 0)          # ResHdr in driver.c
        case_id, status, aux, eflags = h[0:4]
        regs = list(h[4:20])
        xmm, final_ra, ncalls, npages, mxcsr, fpcw = h[20], h[21], h[22], h[23], h[24], h[25]
        p = 424
        calls = []
        for _ in range(ncalls):
            k = struct.unpack_from("<II10Q96s", rec, p)      # CallRec in driver.c
            calls.append((k[0],) + k[2:])
            p += 184
        pages = {}
        for _ in range(npages):
            (addr,) = struct.unpack_from("<Q", rec, p)
            pages[addr] = rec[p + 8:p + 8 + PAGE]
            p += 8 + PAGE
        out[case_id] = {"status": status, "aux": aux, "eflags": eflags, "regs": regs, "xmm": xmm,
                        "final_ra": final_ra, "calls": calls, "pages": pages, "mxcsr": mxcsr, "fpcw": fpcw}
    return out


# ---- the two tests ---------------------------------------------------------------------------------

class StaleLift(RuntimeError):
    """The lifted output on disk does not come from the current lifter and image."""


def check_fresh(image, lifted_dir, report, files=True):
    """Review finding 5: never test lifted C that is older than the lifter.  The report must
    carry the stamp of the current lifter sources and input image, and every generated file
    must still be the one that lift wrote."""
    hint = "re-run the lifter (make lift MODULE=%s) before testing" % image.name
    if report.get("stamp") != lifter_lift.lifter_stamp(image):
        raise StaleLift("%s was lifted by a different version of the lifter, or from a different image: %s"
                        % (lifted_dir, hint))
    if not files:
        return
    on_disk = {os.path.basename(p) for p in glob.glob(os.path.join(lifted_dir, image.name + "_*.c"))}
    if not on_disk <= set(report.get("generated", {})):
        raise StaleLift("%s holds C files its report does not list: %s" % (lifted_dir, hint))
    for name, digest in report["generated"].items():
        path = os.path.join(lifted_dir, name)
        if not os.path.exists(path) or lifter_lift.file_digest(path) != digest:
            raise StaleLift("%s is not the file the report in %s describes: %s" % (name, lifted_dir, hint))


# Seed families (issue 44).  The base seeds fault on the same paths every time, so more of
# them do not help; each family changes one thing about the initial state instead.
#   label, memory kind (make_mem), argument registers (func_state), image variant
FAMILIES = (
    ("args-ptr", "ptr", "ptr", None),        # rcx, rdx, r8, r9 all hold arena pointers
    ("globals", "ptr", "ptr", "zfill"),      # ... and no global in the image is a null pointer
    ("mixed", "mixed", "std", None),         # memory: half pointers, half small integers
    ("ints", "ints", "ptr", None),           # memory: small integers only (0, 1, -1, counts, sign boundaries)
    ("nulls", "nulls", "std", None),         # memory: pointers, one in eight null, so that walks end
    ("regs-ptr", "ptr", "all", "zfill"),     # as globals, and every register but rsp holds an arena pointer
)


def zero_filled_image(image, data, seed):
    """Issue 44: every aligned zero qword in a writable, non-executable section of the
    (relocated) image replaced by an arena pointer, so that code which loads a global pointer
    and dereferences it can run."""
    out = bytearray(data)
    rng = random.Random("zfill:%d" % seed)
    zero = bytes(8)
    for a, b in image.writable_ranges:
        for off in range((a + 7) & ~7, min(b, len(out)) - 7, 8):
            if out[off:off + 8] == zero:
                struct.pack_into("<Q", out, off, arena_ptr(rng))
    return bytes(out)


def _coverage_totals(recs):
    t = {"functions": len(recs), "functions_fully_executed": sum(1 for r in recs if r["insns_executed"] == r["insns"])}
    for k in ("insns", "insns_executed", "cond_branches", "cond_branches_both_ways", "cond_branches_one_way",
              "cond_branches_never"):
        t[k] = sum(r[k] for r in recs)
    return t


def _coverage_line(what, t):
    return ("  coverage, %s: %d of %d instructions executed in a conclusive seed (%.1f%%), %d of %d functions "
            "completely; conditional branches: %d both ways, %d one way, %d never (of %d)" % (
                what, t["insns_executed"], t["insns"], 100.0 * t["insns_executed"] / max(1, t["insns"]),
                t["functions_fully_executed"], t["functions"], t["cond_branches_both_ways"],
                t["cond_branches_one_way"], t["cond_branches_never"], t["cond_branches"]))


def _function_plan(s, image, seeds, family_seeds):
    """The seeds of the function test, and their memory files and image variants in session `s`:
    [(label, memory id, argument-register family, key of the per-function random stream)]."""
    plan = [("base:%d" % k, s.add_mem("ptr", k), "std", "%d" % k) for k in range(seeds)]
    variants = {}
    for k in range(family_seeds):
        for label, kind, args, variant in FAMILIES:
            img = 0
            if variant == "zfill":
                if k not in variants:
                    variants[k] = s.add_image_variant("zfill_%d" % k,
                                                      functools.partial(zero_filled_image, image, s.image_bytes, k))
                img = variants[k]
            plan.append(("%s:%d" % (label, k), s.add_mem(kind, k, img), args, "%s:%d" % (label, k)))
    return plan


def function_test_prepare(image, lifted_dir, out_dir, seeds=6, opt="-O1", jobs=None, family_seeds=3):
    """Everything of a function test that depends only on the module and its lift, written to
    out_dir once: the image and its variants, the memory files, the driver (all the module's
    lifted C, compiled and scanned as in any function test).  function_test(..., prepared=True)
    then runs any subset of the functions from it without writing there, so that a large module
    can be tested in pieces and in several processes (tools/functest).  Returns the lift's
    report."""
    with open(os.path.join(lifted_dir, image.name + ".report.json")) as f:
        report = json.load(f)
    check_fresh(image, lifted_dir, report)
    s = Session(image, lifted_dir, out_dir, opt, jobs)
    s.build_driver()
    _function_plan(s, image, seeds, family_seeds)
    return report


def function_test(image, lifted_dir, out_dir, seeds=6, only=None, opt="-O1", jobs=None, quiet=False,
                  family_seeds=3, paging=True, cut=True, work_dir=None, prepared=False, batch=None,
                  watch=(), aim=None):
    """Isolated function test over every lifted function in the module's report: `seeds`
    base seeds plus `family_seeds` of each family in FAMILIES per function.  Three per family
    is the least that lets a function which only runs under one family reach the passing
    rule (3 conclusive seeds); measured on tier0, 1, 2 and 3 per family add 16, 30 and 78
    passing functions to the 1,635 that pass on 12 base seeds.  `paging`: memory outside the
    image, arena and stack is mapped on demand (demand_page); without it such an access makes
    the case inconclusive, as it did before 2026-10-10.  `cut`: a case that fills the call
    record, or passes the instruction limit and then reaches an event, ends at that event and
    is compared there; without it such a case is inconclusive, as it was before that day.
    `work_dir`, `prepared`: see Session; with `prepared` the driver in out_dir is used as it
    is, so the caller answers for it being built from lifted_dir (tools/functest keeps the
    digest of the lift's report beside it), and only the stamp of the lift is checked here.
    `batch`: run the oracle and the candidate on that many functions at a time and keep of each
    round only the verdicts and the coverage.  The verdicts are the same; what changes is the
    memory: a case that ends at the call record's limit holds 8,192 events on each side, about
    10 MB, and all cases of a round are held at once (210 functions of rtech_game in one round:
    2.0 GB).  `watch`: RVAs of instructions; a function that holds one gets `watched` in its
    record: for each, in how many of its conclusive seeds that instruction was executed.
    `aim`: settings from parse_aim.  Every seed is then run once per setting, with the
    setting's registers put over the seed's (aim_regs), and each function's record gets
    `aimed`: per setting, how its cases ended.  The harness's own seeds put arena pointers and
    a few small integers in the argument registers, so a path that needs one particular
    argument (the seventh entry of a jump table) is otherwise taken by no seed."""
    with open(os.path.join(lifted_dir, image.name + ".report.json")) as f:
        report = json.load(f)
    check_fresh(image, lifted_dir, report, files=not prepared)
    with open(os.path.join(lifted_dir, image.name + ".cfg.json")) as f:
        where = json.load(f)
    funcs = [r for r in report["functions"] if lifter_lift.is_lifted(r)]
    if only is not None:
        funcs = [r for r in funcs if int(r["rva"], 16) in only]
    s = Session(image, lifted_dir, out_dir, opt, jobs, work_dir=work_dir, prepared=prepared)
    probes, probe_callers = probe_targets(image, where)
    s.probes = sorted(IMAGE_BASE + t for t in probes)
    t0 = time.time()
    s.build_driver()
    t_build = time.time() - t0

    def with_probe_contract(c, o):
        """A case that called the stack probe and came out inconclusive runs again with the
        probe's contract."""
        if o.get("probed") and not c.probe:
            c.probe = True
            return True
        return False

    plan = _function_plan(s, image, seeds, family_seeds)
    cases = []
    for r in funcs:
        rva = int(r["rva"], 16)
        for label, mem_id, args, key in plan:
            rng = random.Random("func:%s:%x:%s" % (image.name, rva, key))
            regs, eflags, xmm = func_state(rng, args)
            seed = rng.getrandbits(64)
            for aimed, setting in aim or [(None, ())]:
                aimed_regs = aim_regs(regs, setting)
                cases.append(Case(id=len(cases), key=r["name"], func_rva=rva, ranges=r["ranges"], mem_id=mem_id,
                                  regs=aimed_regs, pokes=aim_pokes(aimed_regs, setting), eflags=eflags, xmm=xmm, seed=seed,
                                  label=label if aimed is None else "%s:%s" % (label, aimed),
                                  paging=paging, cut=cut, stops=where[r["name"]].get("stops", ()),
                                  table_stops=where[r["name"]].get("table_stops", ())))
    t0 = time.time()
    per_round = len(cases) if not batch else batch * len(plan) * len(aim or [0])
    s.keep_oracle = bool(batch)
    verdicts, covered, rerun, tot, fixes, seconds = {}, {}, [], Counter(), Counter(), [0.0, 0.0]
    for at in range(0, len(cases), max(1, per_round)):
        part = cases[at:at + per_round]
        verdicts.update(s.run(part, "func", again=with_probe_contract))
        for c in part:
            if verdicts[c.id][0] != "inconclusive":
                covered[c.id] = (s.ora[c.id]["cov"], s.ora[c.id]["br"])
        rerun += [c.id for c in s.rerun]
        for k in ("oracle_traps", "oracle_stops", "oracle_cuts", "nan_fixed", "demand_cases", "demand_pages"):
            tot[k] += getattr(s, k)
        fixes.update(s.fixes)
        seconds = [seconds[0] + s.seconds[0], seconds[1] + s.seconds[1]]
        s.ora = {}
    s.oracle_traps, s.oracle_stops, s.oracle_cuts = tot["oracle_traps"], tot["oracle_stops"], tot["oracle_cuts"]
    s.nan_fixed, s.demand_cases, s.demand_pages = tot["nan_fixed"], tot["demand_cases"], tot["demand_pages"]
    s.fixes, s.seconds = fixes, (round(seconds[0], 1), round(seconds[1], 1))
    t_run = time.time() - t0
    probe_runs = len(rerun)
    probe_conclusive = sum(1 for i in rerun if verdicts[i][0] != "inconclusive")

    per = defaultdict(list)
    for c in cases:
        per[c.key].append((c, verdicts[c.id]))
    table_jumps = {r["name"]: {t[0] for t in where[r["name"]].get("table_stops", ())} for r in funcs}
    results = []
    why = Counter()
    by_family = defaultdict(lambda: [0, 0])
    base_alone = 0
    for r in funcs:
        vs = per[r["name"]]
        n_pass = sum(1 for _, v in vs if v[0] == "pass")
        fails = [(c, v) for c, v in vs if v[0] == "fail"]
        inc = [(c, v) for c, v in vs if v[0] == "inconclusive"]
        for c, v in vs:
            e = by_family[c.label.split(":")[0]]
            e[0] += 1
            e[1] += v[0] != "inconclusive"
        # DESIGN.md: passing = at least 3 conclusive seeds and all conclusive seeds agree.  A
        # seed that ended at a stop (an instruction the lifter does not translate) agrees up to
        # there and fails the function if it does not, but it is not one of the 3.
        stopped = [(c, v) for c, v in vs if v[0] == "stopped"]
        if fails:
            status = "failing"
        elif n_pass >= 3:
            status = "passing"
        else:
            status = "inconclusive"
            for reason in set(_reason_class(v[1]) for _, v in inc):
                why[reason] += 1
            for what in sorted(set(TABLE_MISS_WHAT if v[1] in table_jumps.get(r["name"], ()) else UNTRANSLATED_WHAT
                                   for _, v in stopped)):
                why[what] += 1
        if not fails and sum(1 for c, v in vs if v[0] == "pass" and c.label.startswith("base:")) >= 3:
            base_alone += 1
        rec = {"name": r["name"], "rva": r["rva"], "status": status, "seeds": len(vs), "conclusive": n_pass + len(fails),
               "agree": n_pass, "inconclusive_reasons": sorted(set(v[1] for _, v in inc))[:4]}
        if where[r["name"]].get("stops") or where[r["name"]].get("table_stops"):
            # The function has stops: how many, how many seeds ended at one (in agreement), and where.
            # A jump through an unbounded table is counted apart: it is a stop for some indexes only.
            if where[r["name"]].get("stops"):
                rec["stops"] = len(where[r["name"]]["stops"])
            if where[r["name"]].get("table_stops"):
                rec["table_stops"] = len(where[r["name"]]["table_stops"])
            rec["stopped"] = len(stopped)
            rec["stopped_at"] = ["0x%x" % a for a in sorted(set(v[1] for _, v in stopped))[:8]]
        if aim:
            # Per setting of the aimed registers: cases, how many were conclusive and passed, how
            # many ended at a stop in agreement, how many failed.
            rec["aimed"] = {}
            for label, _ in aim:
                mine = [v for c, v in vs if c.label.endswith(":" + label)]
                rec["aimed"][label] = {"cases": len(mine), "pass": sum(1 for v in mine if v[0] == "pass"),
                                       "stopped": sum(1 for v in mine if v[0] == "stopped"),
                                       "fail": sum(1 for v in mine if v[0] == "fail"),
                                       "inconclusive": sum(1 for v in mine if v[0] == "inconclusive")}
        # Coverage (review finding 1): what the conclusive seeds actually executed.  A wrong
        # translation on a path no conclusive seed takes cannot be seen, so "passing" is
        # reported together with how much of the function it stands for.
        executed, br = set(), {}
        for c, v in vs:
            if v[0] != "inconclusive":
                cov, taken = covered[c.id]
                executed |= cov
                for a, m in taken.items():
                    br[a] = br.get(a, 0) | m
        w = where[r["name"]]
        insns = set(w["insns"])
        rec["insns"] = len(insns)
        rec["insns_executed"] = len(executed & insns)
        rec["cond_branches"] = len(w["jcc"])
        rec["cond_branches_both_ways"] = sum(1 for a in w["jcc"] if br.get(a, 0) == BR_TAKEN | BR_NOT_TAKEN)
        rec["cond_branches_one_way"] = sum(1 for a in w["jcc"] if br.get(a, 0) in (BR_TAKEN, BR_NOT_TAKEN))
        rec["cond_branches_never"] = rec["cond_branches"] - rec["cond_branches_both_ways"] - rec["cond_branches_one_way"]
        if insns & set(watch):
            rec["watched"] = {"0x%x" % a: sum(1 for c, v in vs if v[0] != "inconclusive" and a in covered[c.id][0])
                              for a in sorted(insns & set(watch))}
        if executed - insns:
            rec["executed_but_not_lifted"] = ["0x%x" % a for a in sorted(executed - insns)[:8]]
        if inc:
            rec["inconclusive_cases"] = [
                {"seed": c.label, "reason": v[1],
                 "rva": None if v[2]["rva"] is None else "0x%x" % v[2]["rva"],
                 "address": None if v[2]["address"] is None else "0x%x" % v[2]["address"]} for c, v in inc]
        if fails:
            rec["failures"] = [{"seed": c.label, "mismatches": v[1][:6]} for c, v in fails[:3]]
        results.append(rec)
    summary = {"module": image.name, "functions": len(funcs), "seeds": seeds, "family_seeds": family_seeds,
               "seeds_per_function": len(plan),
               "passing": sum(1 for r in results if r["status"] == "passing"),
               "failing": sum(1 for r in results if r["status"] == "failing"),
               "inconclusive": sum(1 for r in results if r["status"] == "inconclusive"),
               "passing_on_base_seeds_alone": base_alone,
               "cases": len(cases), "cases_conclusive": sum(1 for v in verdicts.values() if v[0] != "inconclusive"),
               "cases_ending_in_a_defined_trap": s.oracle_traps,
               "functions_with_stops": sum(1 for r in results if r.get("stops")),
               "functions_with_stops_passing": sum(1 for r in results if r.get("stops") and r["status"] == "passing"),
               "cases_ending_at_a_stop": s.oracle_stops,
               "cases_ending_at_an_event_limit": s.oracle_cuts,
               "stack_probes": ["0x%x" % t for t in sorted(probes)], "functions_calling_a_stack_probe": len(probe_callers),
               "cases_run_again_with_the_probe_contract": probe_runs,
               "cases_conclusive_with_the_probe_contract": probe_conclusive,
               "cases_changed_by_the_nan_correction": s.nan_fixed,
               "cases_changed_by_compensation": OF.counts(s.fixes),
               "demand_paging": paging, "cases_with_demand_pages": s.demand_cases, "demand_pages": s.demand_pages,
               "cases_by_family": {k: {"cases": v[0], "conclusive": v[1]} for k, v in by_family.items()},
               "coverage_all_lifted": _coverage_totals(results),
               "coverage_passing": _coverage_totals([r for r in results if r["status"] == "passing"]),
               "functions_executing_unlifted_code": sum(1 for r in results if "executed_but_not_lifted" in r),
               "inconclusive_functions_by_reason": dict(why.most_common()),
               "build_seconds": round(t_build, 1), "run_seconds": round(t_run, 1),
               "oracle_seconds": s.seconds[0], "candidate_seconds": s.seconds[1]}
    with open(os.path.join(work_dir or out_dir, "difftest_func.json"), "w") as f:
        json.dump({"summary": summary, "functions": results}, f, indent=1)
    if not quiet:
        print("function test, %s: %d lifted functions x %d seeds (%d base + %d of each of %d families)" % (
            image.name, len(funcs), len(plan), seeds, family_seeds, len(FAMILIES)))
        print("  passing %d, failing %d, inconclusive %d   (%d of %d cases conclusive, %d of them ending in a "
              "defined trap; build %.0fs, oracle %.0fs, candidate %.0fs)" % (
                  summary["passing"], summary["failing"], summary["inconclusive"], summary["cases_conclusive"],
                  len(cases), s.oracle_traps, t_build, s.seconds[0], s.seconds[1]))
        print("  oracle compensations, cases changed: %s" % OF.counts_line(s.fixes))
        if summary["functions_with_stops"]:
            print("  stops: %d lifted functions hold instructions the lifter does not translate; %d of them pass on the "
                  "paths that avoid those; %d cases ended at one, the state on arrival compared" % (
                      summary["functions_with_stops"], summary["functions_with_stops_passing"], s.oracle_stops))
        print("  demand paging %s: %d conclusive cases ran on %d demand pages;  %d cases ended at an event, at a limit%s" % (
            "on" if paging else "off", s.demand_cases, s.demand_pages, s.oracle_cuts, "" if cut else " (off)"))
        print("  stack probe: %d functions call it (%s); %d cases that did and were inconclusive ran again with "
              "its contract, %d of them conclusive" % (len(probe_callers), ", ".join("+0x%x" % t for t in sorted(probes)) or "none",
                                                      probe_runs, probe_conclusive))
        print("  passing on the %d base seeds alone: %d;  conclusive cases by family: %s" % (
            seeds, base_alone, ", ".join("%s %d/%d" % (k, v[1], v[0]) for k, v in by_family.items())))
        print(_coverage_line("all %d lifted functions" % len(results), summary["coverage_all_lifted"]))
        print(_coverage_line("the %d passing functions" % summary["passing"], summary["coverage_passing"]))
        if summary["functions_executing_unlifted_code"]:
            print("  WARNING: %d functions executed instructions that are not in their lifted body"
                  % summary["functions_executing_unlifted_code"])
        print("  inconclusive functions by oracle reason: %s" % ", ".join("%s %d" % kv for kv in why.most_common(8)))
        for r in results:
            if r["status"] == "failing":
                print("  FAIL %s (%d/%d seeds agree): %s" % (r["name"], r["agree"], r["conclusive"],
                                                             r["failures"][0]["mismatches"][0]))
    return summary, results


def _reason_class(reason):
    if reason.startswith("fault: "):
        r = reason[7:]
        tail = (" " + r[r.index("["):]) if "[" in r else ""
        r = r.split(" [")[0]
        r = r.split(" at image+")[0]
        return "fault: " + (r.split("(")[-1].rstrip(")") if "(" in r else r).replace("UC_ERR_", "") + tail
    if reason.startswith("emulation stopped"):
        return "emulation stopped"
    return reason.split(" at image+")[0]


M128_KEYS = ("aligned_cases", "aligned_pass", "misaligned_cases", "misaligned_pass")


def instruction_test(image, lifted_dir, out_dir, per_shape=3, states=24, opt="-O1", jobs=None, quiet=False,
                     rvas=None):
    """Instruction test.  `rvas`: explicit list; default: a sample per operand shape taken
    from <module>.shapes.json written by the lifter."""
    insn_dir = lifted_dir.rstrip("/") + "_insn"
    if rvas is None:
        with open(os.path.join(lifted_dir, image.name + ".report.json")) as f:
            check_fresh(image, lifted_dir, json.load(f), files=False)     # the sample comes from that lift
        with open(os.path.join(lifted_dir, image.name + ".shapes.json")) as f:
            shapes = json.load(f)
        rng = random.Random(0x1257)
        rvas = []
        for sh in sorted(shapes):
            pool = shapes[sh]["rvas"]
            rvas += rng.sample(pool, min(per_shape, len(pool)))
    prog = Program(image)
    recs = lifter_lift.lift_instructions(image, rvas, insn_dir, prog)
    s = Session(image, insn_dir, out_dir, opt, jobs)
    s.build_driver()
    mems = [s.add_mem("rand", k) for k in range(4)]
    peeks = [s.peeker(m) for m in mems]
    cases = []
    skipped = []
    straddling = set()          # cases whose atomic operand crosses a 16-byte boundary
    fins, fcases, how = {}, [], {}                  # float instructions, their cases, where each expected result came from
    m128 = {}                   # case -> is it misaligned, for cases with a 16-byte operand that must be aligned
    for rva in sorted(recs):
        r = recs[rva]
        if r["status"] != "lifted":
            skipped.append(rva)
            continue
        ins = prog.decode(rva)
        atom = atomic_operand(ins)
        fdecl = FC.decl(ins)
        if fdecl is not None:
            # A float instruction: float-aware operands (the same `states` operand sets under
            # every MXCSR variant), and the real CPU as the expected side.
            fins[rva] = ins
            for vname, mx in FC.VARIANTS:
                for k in range(states):
                    rng = random.Random("insn-fp:%s:%x:%d" % (image.name, rva, k))
                    regs, eflags, xmm, _ = insn_state(ins, rng)
                    mis = place_aligned_operand(ins, regs, k)
                    if mis is not None:
                        m128[len(cases)] = mis
                    xmm, pokes = FC.float_operands(ins, fdecl, regs, xmm, rng, k, ea_of, peeks[k % len(mems)])
                    cases.append(Case(id=len(cases), key=rva, func_rva=rva, ranges=[[rva, rva + ins.size]],
                                      end=rva + ins.size, insn_rva=rva, mem_id=mems[k % len(mems)], regs=regs,
                                      eflags=eflags, xmm=xmm, seed=rng.getrandbits(64), pokes=pokes, mxcsr=mx,
                                      label=vname))
                    fcases.append(cases[-1])
            continue
        for k in range(states):
            rng = random.Random("insn:%s:%x:%d" % (image.name, rva, k))
            rng2 = random.Random("insn-mem:%s:%x:%d" % (image.name, rva, k))
            regs, eflags, xmm, pokes = insn_state(ins, rng, peeks[k % len(mems)], k, rng2)
            if atom and straddles(ea_of(ins, atom[0], regs), atom[1]):
                straddling.add(len(cases))
            mis = None if atom else place_aligned_operand(ins, regs, None)
            if mis is not None:
                m128[len(cases)] = mis
            cases.append(Case(id=len(cases), key=rva, func_rva=rva, ranges=[[rva, rva + ins.size]],
                              end=rva + ins.size, insn_rva=rva, mem_id=mems[k % len(mems)], regs=regs,
                              eflags=eflags, xmm=xmm, seed=rng.getrandbits(64), pokes=pokes,
                              fpcw=SF.fpcw_start(ins, rng)))
    hw = FC.Hardware(sys.modules[__name__], s) if fcases else None
    verdicts = s.run(cases, "insn", (lambda ora: how.update(hw.expect(fcases, fins, ora))) if fcases else None)

    # pass, fail, inconclusive, first failure, reasons, [straddling cases, of which pass]
    by_insn = defaultdict(lambda: [0, 0, 0, None, set(), [0, 0], [0, 0, 0, 0]])
    fstat = defaultdict(lambda: {"cases": 0, "variants": Counter(), "hardware": 0, "emulator_only": 0,
                                 "no_hardware_answer": 0, "hardware_fault": 0, "emulator_differs_from_hardware": 0,
                                 "nan_correction_changed": 0, "emulator_differs_by_variant": Counter()})
    for c in fcases:
        f = fstat[c.key]
        src, differs = how[c.id]
        f["cases"] += 1
        f["variants"][c.label] += 1
        f[src.replace(" ", "_")] += 1
        f["emulator_differs_from_hardware"] += differs
        f["emulator_differs_by_variant"][c.label] += differs
        f["nan_correction_changed"] += 1 if s.ora[c.id].get("nan_fixed") else 0
    for c in cases:
        v = verdicts[c.id]
        e = by_insn[c.key]
        e[{"pass": 0, "fail": 1, "inconclusive": 2}[v[0]]] += 1
        if v[0] == "inconclusive":
            e[4].add(_reason_class(v[1]))
        if c.id in straddling:
            e[5][0] += 1
            e[5][1] += v[0] == "pass"
        if c.id in m128:                 # aligned cases, of which pass; misaligned cases (#GP), of which pass
            e[6][2 * m128[c.id]] += 1
            e[6][2 * m128[c.id] + 1] += v[0] == "pass"
        if v[0] == "fail" and e[3] is None:
            e[3] = {"regs": ["%#x" % x for x in c.regs], "eflags": "%#x" % c.eflags, "mxcsr": "%#x" % c.mxcsr,
                    "xmm": c.xmm.hex() if c.key in fins else None,
                    "pokes": [["%#x" % a, bytes(d).hex()] for a, d in c.pokes], "mismatches": v[1][:6]}
    by_mn = defaultdict(lambda: {"instructions": 0, "pass": 0, "fail": 0, "inconclusive": 0, "failing_instructions": 0,
                                 "untested_instructions": 0})
    insns = []
    for rva in sorted(by_insn):
        p, f_, i, first, reasons, strad, al = by_insn[rva]
        r = recs[rva]
        mn = r["shape"].split(" ")[0] if not r["shape"].startswith(("lock ", "rep ", "repne ")) \
            else " ".join(r["shape"].split(" ")[:2])
        m = by_mn[mn]
        m["instructions"] += 1
        m["pass"] += p
        m["fail"] += f_
        m["inconclusive"] += i
        m["failing_instructions"] += 1 if f_ else 0
        m["untested_instructions"] += 1 if (p + f_) == 0 else 0
        rec = {"rva": "0x%x" % rva, "text": r["text"], "shape": r["shape"], "pass": p, "fail": f_, "inconclusive": i}
        if first:
            rec["first_failure"] = first
        if reasons:
            rec["inconclusive_reasons"] = sorted(reasons)
        if strad[0]:
            rec["atomic_straddling_cases"], rec["atomic_straddling_pass"] = strad
        if al[0] + al[2]:
            rec["m128_operand"] = dict(zip(M128_KEYS, al))
        if rva in fstat:
            rec["float"] = dict(fstat[rva], variants=dict(fstat[rva]["variants"]),
                                emulator_differs_by_variant={k: v for k, v in fstat[rva]["emulator_differs_by_variant"].items() if v})
        insns.append(rec)
    summary = {"module": image.name, "instructions": len(by_insn), "states_each": states,
               "cases": len(cases), "pass": sum(m["pass"] for m in by_mn.values()),
               "fail": sum(m["fail"] for m in by_mn.values()),
               "inconclusive": sum(m["inconclusive"] for m in by_mn.values()),
               "instructions_failing": sum(m["failing_instructions"] for m in by_mn.values()),
               "instructions_never_conclusive": sum(m["untested_instructions"] for m in by_mn.values()),
               "cases_ending_in_a_defined_trap": s.oracle_traps,
               "atomic_straddling_cases": sum(e[5][0] for e in by_insn.values()),
               "atomic_straddling_pass": sum(e[5][1] for e in by_insn.values()),
               "not_lifted": len(skipped),
               "cases_changed_by_the_nan_correction": s.nan_fixed,
               "cases_changed_by_compensation": OF.counts(s.fixes),
               "m128_operand": {k: sum(e[6][j] for e in by_insn.values()) for j, k in enumerate(M128_KEYS)}}
    if fcases:
        # Float instructions: how many cases were judged by the real CPU, by the emulator alone
        # (default MXCSR, no hardware answer) or not at all (another MXCSR, no hardware answer).
        summary["float"] = {k: sum(f[k] for f in fstat.values()) for k in (
            "cases", "hardware", "emulator_only", "no_hardware_answer", "hardware_fault",
            "emulator_differs_from_hardware", "nan_correction_changed")}
        summary["float"].update(instructions=len(fstat), hardware_asked_now=hw.sent, hardware_from_cache=hw.from_cache,
                                hardware_unreachable=hw.unreachable)
    with open(os.path.join(out_dir, "difftest_insn.json"), "w") as f:
        json.dump({"summary": summary, "by_mnemonic": dict(sorted(by_mn.items())), "instructions": insns}, f, indent=1)
    if not quiet:
        print("instruction test, %s: %d instructions x %d states = %d cases" % (
            image.name, len(by_insn), states, len(cases)))
        print("  cases: pass %d, fail %d, inconclusive %d; instructions failing %d, never conclusive %d" % (
            summary["pass"], summary["fail"], summary["inconclusive"], summary["instructions_failing"],
            summary["instructions_never_conclusive"]))
        print("  %d cases end in a defined trap; %d have an atomic operand that straddles a 16-byte boundary "
              "(%d pass)" % (s.oracle_traps, summary["atomic_straddling_cases"], summary["atomic_straddling_pass"]))
        print("  16-byte operands that must be aligned: %(aligned_cases)d aligned cases (%(aligned_pass)d pass), "
              "%(misaligned_cases)d misaligned, expecting #GP (%(misaligned_pass)d pass)" % summary["m128_operand"])
        print("  oracle compensations, cases changed: %s" % OF.counts_line(s.fixes))
        if fcases:
            fl = summary["float"]
            print("  float: %d instructions under %d MXCSR variants = %d cases; expected side: the real CPU %d (%d from "
                  "the cache, %d asked now), emulator only %d, no hardware answer %d, hardware fault %d" % (
                      fl["instructions"], len(FC.VARIANTS), fl["cases"], fl["hardware"], hw.from_cache, hw.sent,
                      fl["emulator_only"], fl["no_hardware_answer"], fl["hardware_fault"]))
            byv = Counter()
            for f in fstat.values():
                byv.update(f["emulator_differs_by_variant"])
            print("  float: the emulator differs from the CPU in %d of those cases (%s); the NaN correction changed %d" % (
                fl["emulator_differs_from_hardware"], ", ".join("%s %d" % (k, byv[k]) for k in FC.VARIANT_NAMES if byv[k])
                or "none", fl["nan_correction_changed"]))
            if hw.unreachable:
                print("  float: the x86 machine did not answer (%s)" % hw.unreachable)
        print("  %-16s %6s %7s %6s %7s" % ("mnemonic", "insns", "pass", "fail", "inconcl"))
        for mn in sorted(by_mn):
            m = by_mn[mn]
            print("  %-16s %6d %7d %6d %7d%s" % (mn, m["instructions"], m["pass"], m["fail"], m["inconclusive"],
                                                 "   <-- FAIL" if m["fail"] else
                                                 "   (never conclusive: %d)" % m["untested_instructions"]
                                                 if m["untested_instructions"] else ""))
        for rec in insns:
            if rec["fail"]:
                print("  FAIL %s  %s: %s" % (rec["rva"], rec["text"], rec["first_failure"]["mismatches"][0]))
    return summary, by_mn, insns


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mode", choices=["func", "insn"])
    ap.add_argument("--module", required=True)
    ap.add_argument("--dll")
    ap.add_argument("--seeds", type=int, default=12, help="func: base seeds per function")
    ap.add_argument("--family-seeds", type=int, default=3,
                    help="func: seeds of each seed family per function (see FAMILIES; 0 = base seeds only)")
    ap.add_argument("--only", help="func: comma-separated function RVAs (hex)")
    ap.add_argument("--no-cut", action="store_true",
                    help="func: a case that meets the call record's or the instruction limit is inconclusive "
                         "instead of ending at its next event")
    ap.add_argument("--no-paging", action="store_true",
                    help="func: an access outside the image, arena and stack makes the case inconclusive "
                         "instead of mapping a demand page")
    ap.add_argument("--per-shape", type=int, default=3, help="insn: instructions sampled per operand shape")
    ap.add_argument("--states", type=int, default=24, help="insn: random states per instruction")
    ap.add_argument("--opt", default="-O1", help="optimisation level for the lifted C")
    ap.add_argument("--jobs", type=int)
    args = ap.parse_args()
    image = Image.from_pe(args.dll or find_module(args.module), args.module)
    lifted = os.path.join(REPO, "lifted", image.name)
    out = os.path.join(HERE, "out", image.name + ("_insn" if args.mode == "insn" else ""))
    if not os.path.exists(os.path.join(lifted, image.name + ".report.json")):
        sys.exit("no lifted output for %s; run: make lift MODULE=%s" % (image.name, image.name))
    try:
        if args.mode == "func":
            only = {int(x, 16) for x in args.only.split(",")} if args.only else None
            summary, _ = function_test(image, lifted, out, args.seeds, only, args.opt, args.jobs,
                                       family_seeds=args.family_seeds, paging=not args.no_paging,
                                       cut=not args.no_cut)
            return 1 if summary["failing"] else 0
        summary, _, _ = instruction_test(image, lifted, out, args.per_shape, args.states, args.opt, args.jobs)
        return 1 if summary["fail"] else 0
    except StaleLift as e:
        print("refusing to test stale output: %s" % e, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
