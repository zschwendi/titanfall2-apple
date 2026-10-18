"""Cases whose answer is known beforehand, written by hand.  `make hworacle-live` asks the real
CPU and requires exactly these answers; `make hworacle-test` checks the same list against the
cache when the answers are there (so it runs with the x86 machine switched off).

Every instruction lives in one code page at the harness's image base; data, a read-only page,
an inaccessible page and a stack sit where the harness puts its arena and stack.  Nothing here
comes from a game file.
"""

import os
import struct
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")))
import client as C                                    # noqa: E402

CODE = C.CODE_ADDR
DATA = C.DATA_ADDR                                    # two writable pages, patterned
RO = DATA + 0x10000                                   # one read-only page
NONE = DATA + 0x20000                                 # one page with no access
UNMAPPED = DATA + 0x800000000
STACK = C.STACK_ADDR                                  # four writable pages of zeroes
RSP = STACK + 0x2000
GS = DATA + 0x800
CONST_OFF = 0x800                                     # eight known bytes inside the code page
CONST = 0x1122334455667788

CF, PF, AF, ZF, SF, DF, OF = 1, 4, 0x10, 0x40, 0x80, 0x400, 0x800
ALL_FLAGS = CF | PF | AF | ZF | SF | DF | OF

DATA_BYTES = bytes((i * 7 + 3) & 0xFF for i in range(0x2000))
_code = bytearray(0x1000)
_code[CONST_OFF:CONST_OFF + 8] = struct.pack("<Q", CONST)
_slots = {}


def at(hexbytes):
    """Address of this instruction in the code page (each distinct encoding gets a 16-byte slot)."""
    if hexbytes not in _slots:
        off = 16 * len(_slots)
        assert off + 16 <= CONST_OFF, "code page full"
        raw = bytes.fromhex(hexbytes)
        _code[off:off + len(raw)] = raw
        _slots[hexbytes] = off
    return CODE + _slots[hexbytes]


def rip_rel(prefix_hex, target_off):
    """An instruction ending in a disp32 that addresses CODE + target_off."""
    off = 16 * len(_slots)
    n = len(bytes.fromhex(prefix_hex)) + 4
    return prefix_hex + struct.pack("<i", target_off - (off + n)).hex()


def data(off, n):
    return DATA_BYTES[off:off + n]


def q(off):
    return struct.unpack_from("<Q", DATA_BYTES, off)[0]


def f32pair(lo, pattern=0xA5A5A5A5_5A5A5A5A_C3C3C3C3):
    """xmm value: a float in lane 0 and a pattern above it (which a scalar operation must keep)."""
    return (lo | pattern << 32).to_bytes(16, "little")


ENTRIES = []          # (name, kind, make_case, expectation); built lazily because regions need the finished code page


def entry(name, hexbytes, kind="plain", **kw):
    addr = at(hexbytes)
    exp = {k[2:]: kw.pop(k) for k in list(kw) if k.startswith("e_")}
    ENTRIES.append((name, kind, addr, len(bytes.fromhex(hexbytes)), kw, exp))


def R(**named):
    named.setdefault("rsp", RSP)
    return C.gpr_list(**named)


# ---- integer arithmetic and flags ------------------------------------------------------------------
entry("add rax, rbx: signed overflow", "4801d8", gpr=R(rax=0x7FFFFFFFFFFFFFFF, rbx=1),
      e_gpr={0: 0x8000000000000000}, e_rflags=OF | SF | AF | PF)
entry("add rax, rbx: 1 + -1", "4801d8", gpr=R(rax=1, rbx=-1), rflags=SF | OF | DF,
      e_gpr={0: 0}, e_rflags=CF | PF | AF | ZF | DF)
entry("add eax, ebx: writes 32 bits, clears the upper half", "01d8", gpr=R(rax=-1, rbx=1),
      e_gpr={0: 0}, e_rflags=CF | PF | AF | ZF)
entry("add al, bl: writes 8 bits only", "00d8", gpr=R(rax=0x1234567F, rbx=1),
      e_gpr={0: 0x12345680}, e_rflags=OF | SF | AF)
entry("adc rax, rbx: carry in, every other flag recomputed, DF kept", "4811d8", gpr=R(rax=5, rbx=10), rflags=ALL_FLAGS,
      e_gpr={0: 16}, e_rflags=AF | DF)
entry("sub rax, rbx: 0 - 1", "4829d8", gpr=R(rax=0, rbx=1), e_gpr={0: C.M64}, e_rflags=CF | PF | AF | SF)
entry("inc rax: wraps, CF untouched (clear)", "48ffc0", gpr=R(rax=-1), e_gpr={0: 0}, e_rflags=PF | AF | ZF)
entry("inc rax: wraps, CF untouched (set)", "48ffc0", gpr=R(rax=-1), rflags=CF, e_gpr={0: 0}, e_rflags=CF | PF | AF | ZF)
entry("div rcx: 100 / 7", "48f7f1", gpr=R(rax=100, rdx=0, rcx=7), e_gpr={0: 14, 2: 2}, e_rflags=None)
entry("cmpxchg ecx, edx: equal, so rax is not written (upper half survives)", "0fb1d1",
      gpr=R(rax=0xAAAAAAAA00000005, rcx=0xBBBBBBBB00000005, rdx=0xCCCCCCCC00000009),
      e_gpr={1: 9}, e_rflags=ZF | PF)
entry("cmpxchg ecx, edx: not equal, so only eax is written", "0fb1d1",
      gpr=R(rax=0xAAAAAAAA00000005, rcx=0xBBBBBBBB00000006, rdx=0xCCCCCCCC00000009),
      e_gpr={0: 6}, e_rflags=CF | PF | AF | SF)
entry("lock neg dword [rbx]", "f0f71b", gpr=R(rbx=DATA + 0x100), pokes=[(DATA + 0x100, struct.pack("<I", 1))],
      e_mem={DATA + 0x100: struct.pack("<I", 0xFFFFFFFF)}, e_rflags=CF | PF | AF | SF)

# ---- memory operands ---------------------------------------------------------------------------------
entry("gs-relative load", "65488b042530000000", gpr=R(), gs_base=GS, e_gpr={0: q(0x830)})
entry("gs-relative load with a register", "65488b04c8", gpr=R(rax=0x10, rcx=2), gs_base=GS, e_gpr={0: q(0x800 + 0x10 + 16)})
entry("rip-relative load", rip_rel("488b05", CONST_OFF), gpr=R(), e_gpr={0: CONST})
entry("load", "488b03", gpr=R(rbx=DATA + 0x123), e_gpr={0: q(0x123)})
entry("store across a page boundary", "488903", gpr=R(rax=0x0102030405060708, rbx=DATA + 0xFFC),
      e_mem={DATA + 0xFFC: struct.pack("<Q", 0x0102030405060708)})
entry("push rax", "50", gpr=R(rax=0xDEADBEEFCAFEF00D), e_gpr={4: RSP - 8},
      e_mem={RSP - 8: struct.pack("<Q", 0xDEADBEEFCAFEF00D)})
entry("pushfq: IF and bit 1 set, the runner's trap flag not visible", "9c", gpr=R(), rflags=CF | PF | AF | ZF | SF | OF,
      e_gpr={4: RSP - 8}, e_mem={RSP - 8: struct.pack("<Q", 0x8D5 | 0x202)}, e_rflags=CF | PF | AF | ZF | SF | OF)
entry("popfq", "9d", gpr=R(), pokes=[(RSP, struct.pack("<Q", 0x202 | CF | ZF | DF))], e_gpr={4: RSP + 8},
      e_rflags=CF | ZF | DF)
entry("stmxcsr [rbx]", "0fae1b", gpr=R(rbx=DATA + 0x40), mxcsr=0x9FC0, e_mem={DATA + 0x40: struct.pack("<I", 0x9FC0)},
      e_mxcsr=0x9FC0)
entry("ldmxcsr [rbx]", "0fae13", gpr=R(rbx=DATA + 0x40), pokes=[(DATA + 0x40, struct.pack("<I", 0x7FA0))], e_mxcsr=0x7FA0)
entry("rep movsb, 5 bytes", "f3a4", gpr=R(rcx=5, rsi=DATA + 0x200, rdi=DATA + 0x300),
      e_gpr={1: 0, 6: DATA + 0x205, 7: DATA + 0x305}, e_mem={DATA + 0x300: data(0x200, 5)})
entry("rep movsb, rcx = 0", "f3a4", gpr=R(rcx=0, rsi=DATA + 0x200, rdi=DATA + 0x300), e_gpr={})
entry("rep movsb backwards (DF set)", "f3a4", gpr=R(rcx=3, rsi=DATA + 0x202, rdi=DATA + 0x302), rflags=DF,
      e_gpr={1: 0, 6: DATA + 0x1FF, 7: DATA + 0x2FF}, e_mem={DATA + 0x300: data(0x200, 3)}, e_rflags=DF)
entry("repne scasb finds a byte", "f2ae", gpr=R(rax=data(0x403, 1)[0], rcx=100, rdi=DATA + 0x400),
      e_gpr={1: 96, 7: DATA + 0x404}, e_rflags=ZF | PF)

# ---- faults -----------------------------------------------------------------------------------------
entry("read from an unmapped address", "488b03", gpr=R(rbx=UNMAPPED + 0x18),
      e_fault=(11, 1, UNMAPPED + 0x18, 14, 0))
entry("write to an unmapped address", "488903", gpr=R(rbx=UNMAPPED + 0x18), e_fault=(11, 1, UNMAPPED + 0x18, 14, 2))
entry("read from a page with no access", "488b03", gpr=R(rbx=NONE + 8), e_fault=(11, 2, NONE + 8, 14, 0))
entry("write to a read-only page", "488903", gpr=R(rbx=RO + 8), e_fault=(11, 2, RO + 8, 14, 2))
entry("read running off the end of the data pages", "488b03", gpr=R(rbx=DATA + 0x1FFC),
      e_fault=(11, 1, DATA + 0x2000, 14, 0))
entry("read from a non-canonical address: #GP", "488b03", gpr=R(rbx=0x0001000000000000), e_fault=(11, 0x80, 0, 13, 0))
entry("divide by zero", "48f7f1", gpr=R(rax=1, rdx=0, rcx=0), e_fault=(8, 1, at("48f7f1"), 0, 0))
entry("idiv overflow: INT_MIN / -1", "f7f9", gpr=R(rax=0x80000000, rdx=0xFFFFFFFF, rcx=0xFFFFFFFF),
      e_fault=(8, 1, at("f7f9"), 0, 0))
entry("ud2", "0f0b", gpr=R(), e_fault=(4, 2, at("0f0b"), 6, 0))
entry("int3: a trap, rip is past it", "cc", gpr=R(), e_fault=(5, 0x80, 0, 3, 0), e_rip=at("cc") + 1)
entry("movaps from a misaligned address: #GP", "0f2803", gpr=R(rbx=DATA + 0x108), e_fault=(11, 0x80, 0, 13, 0))
entry("divss by zero with the divide-by-zero exception unmasked", "f30f5ec1", gpr=R(), mxcsr=0x1D80,
      xmm=C.xmm_bytes({0: f32pair(0x3F800000), 1: f32pair(0)}), e_fault=(8, 3, at("f30f5ec1"), 19, 0), e_mxcsr=0x1D84)
entry("rep stosb running off the end of the data pages", "f3aa", gpr=R(rax=0x00, rcx=0x100, rdi=DATA + 0x1FF0),
      e_fault=(11, 1, DATA + 0x2000, 14, 2), e_gpr={1: 0xF0, 7: DATA + 0x2000}, e_mem={DATA + 0x1FF0: bytes(16)},
      e_insns=0)

# ---- control transfers (each lists where it may stop) -----------------------------------------------------
entry("call rel32", "e8fb0f0000", gpr=R(), stops=[at("e8fb0f0000") + 0x1000], e_rip=at("e8fb0f0000") + 0x1000,
      e_gpr={4: RSP - 8}, e_mem={RSP - 8: struct.pack("<Q", at("e8fb0f0000") + 5)}, e_at_stop=True)
entry("ret", "c3", gpr=R(), pokes=[(RSP, struct.pack("<Q", 0x240000000000))], stops=[0x240000000000],
      e_rip=0x240000000000, e_gpr={4: RSP + 8}, e_at_stop=True)
entry("jmp rax", "ffe0", gpr=R(rax=0x230000000010), stops=[0x230000000010], e_rip=0x230000000010, e_at_stop=True)
entry("jne taken", "7510", gpr=R(), rflags=0, stops=[at("7510") + 2, at("7510") + 0x12], e_rip=at("7510") + 0x12,
      e_at_stop=True, e_rflags=0)
entry("jne not taken", "7510", gpr=R(), rflags=ZF, stops=[at("7510") + 2, at("7510") + 0x12], e_rip=at("7510") + 2,
      e_at_stop=True, e_rflags=ZF)
entry("jmp rax to somewhere the case did not list", "ffe0", gpr=R(rax=0x230000000020), stops=[0x230000000010],
      e_rip=0x230000000020, e_unexpected=True)

# ---- SSE: the probe results of 2026-10-08 (i5-7600K), brief's last section --------------------------------
MX, MX_FTZ, MX_DAZ = 0x1F80, 0x1F80 | 0x8000, 0x1F80 | 0x40


def sse(name, hexbytes, a, b, want, mxcsr=MX, **kw):
    entry(name, hexbytes, gpr=R(), mxcsr=mxcsr, xmm=C.xmm_bytes({0: f32pair(a), 1: f32pair(b, 0x0F0F0F0F_F0F0F0F0_3C3C3C3C)}),
          e_xmm={0: f32pair(want)}, **kw)


sse("rcpss(1.0)", "f30f53c1", 0, 0x3F800000, 0x3F7FF000, e_mxcsr=MX)
sse("rcpss(3.0)", "f30f53c1", 0, 0x40400000, 0x3EAAA000, e_mxcsr=MX)
sse("rsqrtss(1.0)", "f30f52c1", 0, 0x3F800000, 0x3F7FF000, e_mxcsr=MX)
sse("rsqrtss(2.0)", "f30f52c1", 0, 0x40000000, 0x3F34F800, e_mxcsr=MX)
sse("mulss 2^-126 * 0.5, FTZ=1", "f30f59c1", 0x00800000, 0x3F000000, 0x00000000, MX_FTZ)
sse("mulss 1b6a9cc0 * 248bab31, FTZ=1", "f30f59c1", 0x1B6A9CC0, 0x248BAB31, 0x00800000, MX_FTZ)
sse("mulss a1d199b3 * 1e1c55e9, FTZ=1", "f30f59c1", 0xA1D199B3, 0x1E1C55E9, 0x80800000, MX_FTZ)
sse("addss denormal + denormal, FTZ=1", "f30f58c1", 0x00000001, 0x00000001, 0x00000000, MX_FTZ)
sse("minss(1e-45, +inf), DAZ=1", "f30f5dc1", 0x00000001, 0x7F800000, 0x00000000, MX_DAZ)
sse("minss(+inf, 1e-45), DAZ=1", "f30f5dc1", 0x7F800000, 0x00000001, 0x00000000, MX_DAZ)
sse("maxss(1e-45, -inf), DAZ=1", "f30f5fc1", 0x00000001, 0xFF800000, 0x00000000, MX_DAZ)
sse("addss(sNaN 7f800001, qNaN 7fe27b08)", "f30f58c1", 0x7F800001, 0x7FE27B08, 0x7FC00001, e_mxcsr=MX | 1)
sse("addss(sNaN 7f800002, sNaN 7f800004)", "f30f58c1", 0x7F800002, 0x7F800004, 0x7FC00002, e_mxcsr=MX | 1)
sse("mulss(qNaN 7fc00009, qNaN 7fc00005)", "f30f59c1", 0x7FC00009, 0x7FC00005, 0x7FC00009, e_mxcsr=MX)
sse("addss(1.0, qNaN 7fc00005)", "f30f58c1", 0x3F800000, 0x7FC00005, 0x7FC00005, e_mxcsr=MX)
sse("minss(sNaN 7f800001, 1.0)", "f30f5dc1", 0x7F800001, 0x3F800000, 0x3F800000, e_mxcsr=MX | 1)
sse("minss(1.0, sNaN 7f800001)", "f30f5dc1", 0x3F800000, 0x7F800001, 0x7F800001, e_mxcsr=MX | 1)
sse("sqrtss(-1.0)", "f30f51c1", 0, 0xBF800000, 0xFFC00000, e_mxcsr=MX | 1)
entry("paddq xmm8, xmm15", "66450fd4c7", gpr=R(), xmm=C.xmm_bytes({8: 0xFFFFFFFFFFFFFFFF, 15: 1 | 5 << 64}),
      e_xmm={8: (5 << 64).to_bytes(16, "little")})

# ---- several steps ---------------------------------------------------------------------------------------
entry("three steps: inc rax; inc rax; inc rax", "48ffc048ffc048ffc0", gpr=R(rax=10), max_insns=3,
      e_gpr={0: 13}, e_insns=3, e_rip=at("48ffc048ffc048ffc0") + 9, e_rflags=0)
entry("two steps, the second refused: inc rax; syscall", "48ffc00f05", gpr=R(rax=10), max_insns=2,
      e_refused=17, e_insns=1, e_gpr={0: 11}, e_rip=at("48ffc00f05") + 3, e_rflags=0)
entry("three steps reaching a stop address after two", "48ffc048ffc048ffc1", gpr=R(rax=1), max_insns=3,
      stops=[at("48ffc048ffc048ffc1") + 6], e_gpr={0: 3}, e_insns=2, e_rip=at("48ffc048ffc048ffc1") + 6, e_at_stop=True,
      e_rflags=PF)

# ---- refused by inspection -------------------------------------------------------------------------------
for _name, _hex, _why in (("syscall", "0f05", 17), ("sysenter", "0f34", 17), ("int 0x80", "cd80", 18),
                          ("int 0x29", "cd29", 18), ("hlt", "f4", 19), ("in al, dx", "ec", 20), ("out dx, al", "ee", 20),
                          ("mov rax, cr0", "0f20c0", 21), ("cli", "fa", 21), ("rdtsc", "0f31", 22), ("cpuid", "0fa2", 22),
                          ("rdrand eax", "0fc7f0", 22), ("rdseed eax", "0fc7f8", 22), ("rdtscp", "0f01f9", 21),
                          ("mov ds, ax", "8ed8", 23), ("mov rax, fs:[0]", "64488b042500000000", 23),
                          ("wrgsbase rax", "f3480faed0", 23), ("fld st0", "d9c0", 24), ("emms", "0f77", 24),
                          ("vzeroupper", "c5f877", 25), ("retf", "cb", 26), ("iretq", "48cf", 26),
                          ("fxsave [rbx]", "0fae03", 30), ("jmp rax without a stop address", "ffe0", 31),
                          ("ret without a stop address", "c3", 31)):
    entry("refused: " + _name, _hex, gpr=R(rbx=DATA), e_refused=_why)

# ---- the sandbox (Oracle.run(..., allow_unchecked=True)) ------------------------------------------------------
entry("unchecked syscall (getpid) is killed by seccomp", "0f05", kind="sandbox", gpr=R(rax=39), unchecked=True, e_killed=9)
entry("unchecked syscall (getppid) is killed by seccomp", "0f05", kind="sandbox", gpr=R(rax=110), unchecked=True, e_killed=9)
entry("unchecked int 0x80 (32-bit getpid) is killed by seccomp", "cd80", kind="sandbox", gpr=R(rax=20), unchecked=True,
      e_killed=9)
entry("a checked case right after a killed one", "4801d8", kind="sandbox", gpr=R(rax=2, rbx=3), e_gpr={0: 5}, e_rflags=PF)
entry("an unchecked case is refused unless the caller allows it", "0f05", kind="unchecked-refused", gpr=R(rax=39),
      unchecked=True, e_refused=34)

# ---- time limit ------------------------------------------------------------------------------------------------
entry("jmp to itself, 4 billion single steps: time limit", "ebfe", kind="timeout", gpr=R(), max_insns=0xFFFFFFFF,
      stops=[CODE + 0x1000], e_timeout=True)
entry("a case right after a timed-out one", "4801d8", kind="timeout", gpr=R(rax=20, rbx=22), e_gpr={0: 42}, e_rflags=0)


def regions():
    return (C.Region(CODE, bytes(_code), C.PROT_R | C.PROT_X),
            C.Region(DATA, DATA_BYTES, C.PROT_R | C.PROT_W),
            C.Region(RO, DATA_BYTES[:0x1000], C.PROT_R),
            C.Region(NONE, None, 0, size=0x1000),
            C.Region(STACK, None, C.PROT_R | C.PROT_W, size=0x4000))


# Free-running programs: code at the end of its own page, so that the next page (unmapped) is the stop.
def program(code_hex):
    code = bytes.fromhex(code_hex)
    page = bytearray(0x1000)
    page[0x1000 - len(code):] = code
    start = CODE + 0x100000 + 0x1000 - len(code)
    regs = (C.Region(CODE + 0x100000, bytes(page), C.PROT_R | C.PROT_X),
            C.Region(DATA, DATA_BYTES, C.PROT_R | C.PROT_W))
    return regs, start, len(code), CODE + 0x101000


def build():
    """[(name, kind, Case, expectation)]; kinds: plain, sandbox, unchecked-refused, timeout, program."""
    regs = regions()
    out = []
    for name, kind, addr, _n, kw, exp in ENTRIES:
        out.append((name, kind, C.Case(regs, addr, **kw), exp))
    # sum of 1..1000 in a loop, falling off the end of the page into the stop address
    pr, start, n, stop = program("b9e803000031c001c8ffc975fa")
    out.append(("free-running loop: sum of 1..1000", "program",
                C.Case(pr, start, gpr=R(), max_insns=0, code_len=n, stops=[stop]),
                {"gpr": {0: 500500, 1: 0}, "rip": stop, "at_stop": True, "rflags": ZF | PF, "insns": 0}))
    # a program that stores into data and leaves through a jmp to the stop
    pr, start, n, stop = program("48c7c378563412" + "48891f" + "eb00")
    out.append(("free-running program: store, then jmp to the stop", "program",
                C.Case(pr, start, gpr=R(rdi=DATA + 0x80), max_insns=0, code_len=n, stops=[stop]),
                {"gpr": {3: 0x12345678}, "rip": stop, "at_stop": True, "mem": {DATA + 0x80: struct.pack("<Q", 0x12345678)}}))
    pr, start, n, stop = program("31c00f05")
    out.append(("free-running program containing syscall is refused whole", "program",
                C.Case(pr, start, gpr=R(), max_insns=0, code_len=n, stops=[stop]), {"refused": 17}))
    pr, start, n, stop = program("ffe0")
    out.append(("free-running program with an indirect jump is refused", "program",
                C.Case(pr, start, gpr=R(rax=stop), max_insns=0, code_len=n, stops=[stop]), {"refused": 33}))
    pr, start, n, stop = program("ebfe")
    out.append(("free-running jmp to itself: time limit", "timeout",
                C.Case(pr, start, gpr=R(), max_insns=0, code_len=n, stops=[stop]), {"timeout": True}))
    return out


def verify(case, exp, r):
    """Problems with result r for a case, as a list of strings (empty = as expected)."""
    bad = []
    if r.outcome == C.NO_ANSWER:
        return ["no hardware answer"]
    want = C.DONE
    if "fault" in exp:
        want = C.FAULT
    elif "refused" in exp:
        want = C.REFUSED
    elif "killed" in exp:
        want = C.KILLED
    elif "timeout" in exp:
        want = C.TIMEOUT
    if r.outcome != want:
        return ["expected %s, got: %s" % (C.OUTCOMES[want], r.describe())]
    if want == C.REFUSED and r.detail != exp["refused"]:
        bad.append("refused for reason %d (%s), expected %d" % (r.detail, C.REASONS.get(r.detail), exp["refused"]))
    if want == C.KILLED and r.detail != exp["killed"]:
        bad.append("killed by signal %d, expected %d" % (r.detail, exp["killed"]))
    if want == C.FAULT:
        got = (r.signo, r.sig_code, r.sig_addr, r.trapno, r.err)
        signo, code, addr, trapno, err = exp["fault"]
        if got[:4] != (signo, code, addr, trapno) or (trapno == 14 and got[4] != err):
            bad.append("fault (signal, si_code, address, trap, error code) is %s, expected %s" % (
                tuple(hex(x) for x in got), tuple(hex(x) for x in exp["fault"])))
    if not r.has_state:
        return bad
    if want in (C.DONE, C.FAULT, C.REFUSED):
        fault_rip = case.rip if want == C.FAULT else None
        if "rip" in exp:
            rip = exp["rip"]
        elif fault_rip is not None:
            rip = fault_rip
        elif case.max_insns == 1 and not case.stops:
            rip = None                                  # checked through unexpected_rip below
        else:
            rip = None
        if rip is not None and r.rip != rip:
            bad.append("rip %#x, expected %#x" % (r.rip, rip))
        if want == C.DONE:
            if r.unexpected_rip != bool(exp.get("unexpected")):
                bad.append("rip %#x is %swhere the runner expected the case to stop" % (r.rip, "not " if r.unexpected_rip else ""))
            if "at_stop" in exp and r.at_stop != exp["at_stop"]:
                bad.append("at_stop is %s" % r.at_stop)
        insns = exp.get("insns", 1 if want == C.DONE and case.max_insns == 1 else 0 if want == C.FAULT else None)
        if insns is not None and r.insns != insns:
            bad.append("%d instructions completed, expected %d" % (r.insns, insns))
        g = exp.get("gpr", {})
        for i in range(16):
            w = g.get(i, case.gpr[i]) & C.M64
            if r.gpr[i] != w:
                bad.append("%s = %#x, expected %#x" % ("rax rcx rdx rbx rsp rbp rsi rdi r8 r9 r10 r11 r12 r13 r14 r15".split()[i], r.gpr[i], w))
        x = exp.get("xmm", {})
        for i in range(16):
            w = x.get(i, case.xmm[16 * i:16 * i + 16])
            if r.xmm[16 * i:16 * i + 16] != w:
                bad.append("xmm%d = %s, expected %s" % (i, r.xmm[16 * i:16 * i + 16].hex(), bytes(w).hex()))
        if "rflags" in exp:
            if exp["rflags"] is not None and r.rflags != exp["rflags"]:
                bad.append("rflags %#x, expected %#x" % (r.rflags, exp["rflags"]))
        elif want == C.FAULT and r.rflags != case.rflags & C.RFLAGS_VISIBLE:
            bad.append("rflags changed by a faulting instruction: %#x" % r.rflags)
        if "mxcsr" in exp and r.mxcsr != exp["mxcsr"]:
            bad.append("mxcsr %#x, expected %#x" % (r.mxcsr, exp["mxcsr"]))
        m = exp.get("mem", {})
        for a, w in m.items():
            got = C.final_mem(case, r, a, len(w))
            if got != w:
                bad.append("memory at %#x is %s, expected %s" % (a, got.hex() if got else got, w.hex()))
        for a, d in r.mem:                              # nothing may change outside what was expected
            for k in range(len(d)):
                if not any(b <= a + k < b + len(w) for b, w in m.items()):
                    bad.append("memory at %#x changed unexpectedly (to %#04x)" % (a + k, d[k]))
                    break
    return bad
