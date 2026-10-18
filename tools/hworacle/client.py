#!/usr/bin/env python3
"""A real x86-64 CPU as a test oracle: the client (tools/hworacle/README.md).

    from client import Oracle, Case, Region, PROT_R, PROT_W, PROT_X
    ora = Oracle(family="my-tests")
    res = ora.run([Case(regions, rip, gpr=[...], ...)])        # one Result per case, same order

`run` answers from the cache under inventory/hworacle_cache/ where it can and sends the rest, in
batches of a few seconds, to tools/hworacle/runner.c on the x86 machine: one ssh process per
batch, cases on its stdin, results on its stdout.  With offline=True it never starts ssh and
reports the misses as "no hardware answer".

    .venv/bin/python tools/hworacle/client.py status     # is the machine there, what CPU, is the runner built
    .venv/bin/python tools/hworacle/client.py build      # ship runner.c and build it now
    .venv/bin/python tools/hworacle/client.py ps         # anything of ours still running there? (exit 1 if so)
    .venv/bin/python tools/hworacle/client.py remote-ls  # what we keep in ~/tf2-hw-oracle
    .venv/bin/python tools/hworacle/client.py purge      # delete ~/tf2-hw-oracle/{bin,src,blobs} contents

The rules for the x86 machine are in the README; the ones this file enforces: everything lives
in ~/tf2-hw-oracle, the compiler and the runner run under nice and a remote time limit (the few
housekeeping commands - list, store a memory image, ps - are single short commands under the
local ssh timeout), nothing is left running, and no path outside that directory is ever named in
a remote command.
"""

import argparse
import hashlib
import importlib.util
import os
import re
import shlex
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
RUNNER_C = os.path.join(HERE, "runner.c")

def ref_setting(key, env):
    """The reference PC's setting: $env, then $TF_REF_<KEY>, then the line KEY=... of .ref-host at the
    checkout's root (git-ignored: each owner names their own machine there).  "" if none."""
    for name in (env, "TF_REF_" + key.upper()):
        if name and os.environ.get(name):
            return os.environ[name]
    try:
        with open(os.path.join(REPO, ".ref-host"), encoding="utf-8") as f:
            for line in f:
                k, _, v = line.strip().partition("=")
                if k.strip() == key and v.strip():
                    return v.strip()
    except OSError:
        pass
    return ""


HOST = ref_setting("host", "HWORACLE_HOST")
SSH = os.environ.get("HWORACLE_SSH", "ssh")
REMOTE_DIR = "tf2-hw-oracle"           # under the remote home directory; nothing of ours lives anywhere else
GCC_FLAGS = "-O2 -static-pie -fno-stack-protector -Wall -Wextra -Werror"    # a warning is a failed build: nothing is installed

PROT_R, PROT_W, PROT_X = 1, 2, 4
PAGE = 4096
M64 = (1 << 64) - 1
RFLAGS_VISIBLE = 0xCD5                 # CF PF AF ZF SF DF OF: the flags a case sets and a result reports
INLINE_BLOB_MAX = 1 << 16              # larger memory images are stored once on the x86 machine, by content hash

# ---- outcomes (runner.c: enum OUT_*) and refusal reasons (enum HWO_R_*) ---------------------------

DONE, FAULT, REFUSED, TIMEOUT, KILLED, BAD_CASE, RUNNER_ERROR = range(7)
NO_ANSWER = -1                         # client side only: offline mode and the case is not in the cache
OUTCOMES = {DONE: "done", FAULT: "fault", REFUSED: "refused", TIMEOUT: "timeout", KILLED: "killed by the sandbox",
            BAD_CASE: "bad case", RUNNER_ERROR: "runner error", NO_ANSWER: "no hardware answer"}
DONE_AT_STOP, DONE_UNEXPECTED_RIP, DETAIL_MEM_TRUNCATED = 1, 2, 0x100


def cacheable(outcome, detail):
    """Is this answer a fact about the case?  Done, fault and refusal by inspection are.  A
    timeout, a kill, a runner error, a case the runner could not set up, and the refusal of an
    unchecked case (which depends on how the runner was started) describe one run."""
    return outcome in (DONE, FAULT) or (outcome == REFUSED and detail != 34)

REASONS = {16: "not on the allow-list", 17: "system call", 18: "software interrupt", 19: "hlt", 20: "port I/O",
           21: "privileged", 22: "machine-dependent answer (rdtsc, cpuid, rdrand, ...)",
           23: "segment register or fs:", 24: "x87 or MMX state", 25: "VEX/EVEX/XOP (YMM state)", 26: "far transfer",
           27: "not valid in 64-bit mode", 28: "instruction bytes end early", 29: "prefix combination",
           30: "state save/restore", 31: "control transfer without a stop address",
           32: "start address not executable", 33: "program failed the static check",
           34: "unchecked case without --allow-unchecked"}
BAD_CASES = {1: "region set cannot be mapped", 2: "mxcsr has reserved bits", 3: "gs base not canonical",
             4: "poke outside a writable region", 5: "stop address of a free-running case is executable or in use",
             6: "malformed record", 7: "sandbox could not be entered", 8: "limits"}
SIGNALS = {4: "SIGILL", 5: "SIGTRAP", 7: "SIGBUS", 8: "SIGFPE", 9: "SIGKILL", 11: "SIGSEGV", 24: "SIGXCPU"}
TRAPS = {0: "#DE", 1: "#DB", 3: "#BP", 4: "#OF", 5: "#BR", 6: "#UD", 7: "#NM", 12: "#SS", 13: "#GP", 14: "#PF",
         16: "#MF", 17: "#AC", 19: "#XM"}

with open(RUNNER_C) as _f:
    _SRC = _f.read()
FORMAT_VERSION = int(re.search(r"#define HWO_FORMAT_VERSION (\d+)", _SRC).group(1))
ANSWERS_REV = int(re.search(r"#define HWO_ANSWERS_REV (\d+)", _SRC).group(1))
RUNNER_HASH = hashlib.sha256(_SRC.encode() + b"\0" + GCC_FLAGS.encode()).hexdigest()[:16]

MAGIC_BATCH, MAGIC_CASES, MAGIC_END_IN = 0x424F5748, 0x434F5748, 0x5A4F5748
MAGIC_RESULT, MAGIC_END_OUT = 0x524F5748, 0x454F5748
BATCH_HDR = struct.Struct("<12I")
BLOB_HDR = struct.Struct("<32sQII")
REGION_REC = struct.Struct("<QQII")
CASE_HDR = struct.Struct("<8I3Q16Q256s")
RESULT_HDR = struct.Struct("<8IQ48s64s32s128s")
RES_HDR = struct.Struct("<4I2i2I3Q2I2HI")
assert (BATCH_HDR.size, BLOB_HDR.size, REGION_REC.size, CASE_HDR.size, RESULT_HDR.size, RES_HDR.size) == \
    (48, 48, 24, 440, 312, 72)


class OracleUnavailable(Exception):
    """The x86 machine could not be reached, or went away in the middle of a batch.  Nothing was
    stored for the batch that was in flight."""


class OracleError(Exception):
    """The runner answered, and what it said means the batch could not be run (its self-test
    failed, the build failed, the input was malformed)."""


def _load_sibling(name, filename):
    """Import a file that sits next to this one under a name that cannot clash."""
    if name in sys.modules:
        return sys.modules[name]
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, filename))
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod


# ---- cases ------------------------------------------------------------------------------------------

class Blob:
    """Bytes of a memory region, hashed once however many cases share them."""
    __slots__ = ("data", "sha")

    def __init__(self, data):
        self.data = bytes(data)
        self.sha = hashlib.sha256(self.data).digest()


class Region:
    """Memory to map for a case: `data` (bytes, a Blob, or None for zeroes) at `addr`, zero-padded
    to `size` (default: the data rounded up to 4 KiB), with protection PROT_R | PROT_W | PROT_X."""
    __slots__ = ("addr", "size", "prot", "blob")

    def __init__(self, addr, data, prot, size=None):
        self.blob = data if isinstance(data, Blob) or data is None else Blob(data)
        n = len(self.blob.data) if self.blob else 0
        self.size = (max(n, 1) + PAGE - 1) // PAGE * PAGE if size is None else size
        self.addr, self.prot = addr, prot
        if addr % PAGE or self.size % PAGE or n > self.size or not 0 <= prot <= 7:
            raise ValueError("region %#x: address and size must be multiples of 4 KiB, data must fit" % addr)

    def key(self):
        return struct.pack("<QQI", self.addr, self.size, self.prot) + (self.blob.sha if self.blob else bytes(32))


class Case:
    """One question for the CPU.

    regions   tuple of Region; share the same tuple between cases that use the same memory
    rip       where to start
    gpr       16 integers: rax rcx rdx rbx rsp rbp rsi rdi r8..r15
    rflags    only CF PF AF ZF SF DF OF are used
    xmm       256 bytes: xmm0..xmm15
    mxcsr     default 0x1F80
    gs_base   base address for gs:-relative operands
    max_insns 1: one instruction.  N > 1: N single steps.  0: a free-running program (see README)
    stops     addresses where the case is expected to stop; required for control transfers
    pokes     [(address, bytes)] written over the regions' contents before the run (writable regions only)
    code_len  free-running only: the length of the program starting at rip
    unchecked skip the runner's byte inspection; honoured only by Oracle.run(..., allow_unchecked=True)
    tag       anything; carried along, not part of the question"""
    __slots__ = ("regions", "rip", "gpr", "rflags", "xmm", "mxcsr", "gs_base", "max_insns", "stops", "pokes",
                 "code_len", "unchecked", "tag")

    def __init__(self, regions, rip, gpr=None, rflags=2, xmm=None, mxcsr=0x1F80, gs_base=0, max_insns=1,
                 stops=(), pokes=(), code_len=0, unchecked=False, tag=None):
        self.regions = regions if isinstance(regions, tuple) else tuple(regions)
        self.rip = rip
        self.gpr = list(gpr) if gpr is not None else [0] * 16
        self.rflags = rflags
        self.xmm = bytes(xmm) if xmm is not None else bytes(256)
        self.mxcsr, self.gs_base, self.max_insns = mxcsr, gs_base, max_insns
        self.stops = tuple(stops)
        self.pokes = tuple((a, bytes(d)) for a, d in pokes)
        self.code_len, self.unchecked, self.tag = code_len, bool(unchecked), tag
        if len(self.gpr) != 16 or len(self.xmm) != 256:
            raise ValueError("a case has 16 general registers and 256 bytes of xmm")
        # the runner's limits: one case over them would make it reject the whole batch
        if len(self.regions) > 64 or len(self.stops) > 64 or len(self.pokes) > 256 \
                or sum(len(d) for _, d in self.pokes) > (1 << 20):
            raise ValueError("a case has at most 64 regions, 64 stop addresses, 256 pokes and 1 MiB of poked bytes")

    def record(self, set_index):
        """The case as it goes on the wire (struct case_hdr, stops, pokes)."""
        tail = struct.pack("<%dQ" % len(self.stops), *self.stops)
        for a, d in self.pokes:
            tail += struct.pack("<QII", a, len(d), 0) + d + bytes(-len(d) % 8)
        head = CASE_HDR.pack(CASE_HDR.size - 4 + len(tail), 1 if self.unchecked else 0, set_index, self.max_insns,
                             len(self.stops), len(self.pokes), self.code_len, self.mxcsr, self.rip & M64,
                             self.rflags & M64, self.gs_base & M64, *[r & M64 for r in self.gpr], self.xmm)
        return head + tail

    def key(self, memo=None):
        """32 bytes naming the complete question: every region with a hash of its contents, the
        whole register state, pokes, stops and limits, the format version and the answers
        revision of the runner.  Equal keys mean the CPU is being asked the same thing.  Computed
        from the case as it is now, every time: a case changed after an earlier call gets a new key."""
        h = hashlib.sha256(b"hworacle case\0" + struct.pack("<III", FORMAT_VERSION, ANSWERS_REV, len(self.regions)))
        h.update(regionset_key(self.regions, memo))
        h.update(self.record(0))
        return h.digest()


def regionset_key(regions, memo=None):
    """Hash of a tuple of regions.  `memo` (a dict owned by the caller, alive only for one call)
    saves recomputing it for the many cases that share one tuple."""
    if memo is None:
        return hashlib.sha256(b"".join(r.key() for r in regions)).digest()
    k = memo.get(id(regions))
    if k is None or k[0] is not regions:
        k = memo[id(regions)] = (regions, hashlib.sha256(b"".join(r.key() for r in regions)).digest())
    return k[1]


class Result:
    """What the CPU did.  `gpr`, `xmm` hold the complete state afterwards (for outcomes that have
    one: see has_state); `mem` is [(address, bytes)], the runs of bytes that differ from the
    case's initial memory, lowest address first."""
    __slots__ = ("outcome", "detail", "signo", "sig_code", "sig_addr", "trapno", "err", "rip", "rflags", "mxcsr",
                 "insns", "gpr", "xmm", "mem", "raw", "cached", "cpu")

    @property
    def has_state(self):
        return self.outcome in (DONE, FAULT) or (self.outcome == REFUSED and self.insns > 0)

    @property
    def at_stop(self):
        return self.outcome == DONE and bool(self.detail & DONE_AT_STOP)

    @property
    def unexpected_rip(self):
        return self.outcome == DONE and bool(self.detail & DONE_UNEXPECTED_RIP)

    @property
    def mem_truncated(self):
        return bool(self.detail & DETAIL_MEM_TRUNCATED)

    def describe(self):
        o = self.outcome
        if o == FAULT:
            return "fault: %s si_code %d (%s, error code %#x) address %#x at rip %#x" % (
                SIGNALS.get(self.signo, "signal %d" % self.signo), self.sig_code,
                TRAPS.get(self.trapno, "trap %d" % self.trapno), self.err, self.sig_addr, self.rip)
        if o == REFUSED:
            return "refused: %s" % REASONS.get(self.detail, "reason %d" % self.detail)
        if o == KILLED:
            return "killed by the sandbox (%s)" % SIGNALS.get(self.detail, "signal %d" % self.detail)
        if o == TIMEOUT:
            return "timeout after %d ms" % self.detail
        if o == BAD_CASE:
            return "bad case: %s" % BAD_CASES.get(self.detail, "code %d" % self.detail)
        if o == DONE:
            return "done at rip %#x after %d instruction%s%s" % (
                self.rip, self.insns, "" if self.insns == 1 else "s",
                " (not where expected)" if self.unexpected_rip else "")
        return OUTCOMES.get(o, "outcome %d" % o) + (" (%#x)" % self.detail if self.detail else "")


def decode_result(raw, case):
    """A result record (struct res_hdr and what follows) into a Result, filling in from `case`
    the registers the record says did not change.  A record that does not hold what its header
    promises is an OracleError."""
    try:
        return _decode_result(raw, case)
    except struct.error as e:
        raise OracleError("malformed result record (%d bytes): %s" % (len(raw), e)) from None


def _decode_result(raw, case):
    f = RES_HDR.unpack_from(raw, 0)
    r = Result()
    (_, _, r.outcome, r.detail, r.signo, r.sig_code, r.trapno, r.err, r.sig_addr, r.rip, r.rflags, r.mxcsr, r.insns,
     gmask, xmask, n_mem) = f
    r.raw, r.cached, r.cpu = bytes(raw), False, None
    pos = RES_HDR.size
    r.gpr = [x & M64 for x in case.gpr]
    for i in range(16):
        if gmask >> i & 1:
            r.gpr[i] = struct.unpack_from("<Q", raw, pos)[0]
            pos += 8
    xmm = bytearray(case.xmm)
    for i in range(16):
        if xmask >> i & 1:
            xmm[16 * i:16 * i + 16] = raw[pos:pos + 16]
            pos += 16
    r.xmm = bytes(xmm)
    r.mem = []
    for _ in range(n_mem):
        a, n, _z = struct.unpack_from("<QII", raw, pos)
        if pos + 16 + n > len(raw):
            raise OracleError("result record: a memory run of %d bytes does not fit" % n)
        r.mem.append((a, bytes(raw[pos + 16:pos + 16 + n])))
        pos += 16 + (n + 7) // 8 * 8
    if pos != len(raw):
        raise OracleError("result record has %d bytes, its contents account for %d" % (len(raw), pos))
    r.mem.sort()
    if not r.has_state:
        r.rflags, r.mxcsr = case.rflags & RFLAGS_VISIBLE, case.mxcsr
    return r


def initial_mem(case, addr, n):
    """The n bytes at addr before the run (regions with pokes applied), or None if any of them
    is unmapped.  The range may run across regions that touch."""
    buf = bytearray(n)
    covered = 0
    for r in sorted(case.regions, key=lambda r: r.addr):
        lo, hi = max(r.addr, addr), min(r.addr + r.size, addr + n)
        if lo >= hi:
            continue
        if lo != addr + covered:
            return None                                # a gap before this region
        if r.blob:
            data = r.blob.data[lo - r.addr:hi - r.addr]
            buf[lo - addr:lo - addr + len(data)] = data
        covered = hi - addr
    if covered != n:
        return None
    for a, d in case.pokes:
        lo, hi = max(a, addr), min(a + len(d), addr + n)
        if lo < hi:
            buf[lo - addr:hi - addr] = d[lo - a:hi - a]
    return bytes(buf)


def final_mem(case, result, addr, n):
    """The n bytes at addr after the run: the initial contents with the result's changes applied."""
    buf = initial_mem(case, addr, n)
    if buf is None:
        return None
    buf = bytearray(buf)
    for a, d in result.mem:
        lo, hi = max(a, addr), min(a + len(d), addr + n)
        if lo < hi:
            buf[lo - addr:hi - addr] = d[lo - a:hi - a]
    return bytes(buf)


def encode_result(index, case, outcome, detail=0, gpr=None, xmm=None, rip=None, rflags=None, mxcsr=None, insns=0,
                  mem=(), signo=0, sig_code=0, sig_addr=0, trapno=0, err=0):
    """A result record as the runner would write it (for tests and for stand-ins of the runner)."""
    gpr = [x & M64 for x in (gpr if gpr is not None else case.gpr)]
    xmm = bytes(xmm if xmm is not None else case.xmm)
    gmask = sum(1 << i for i in range(16) if gpr[i] != case.gpr[i] & M64)
    xmask = sum(1 << i for i in range(16) if xmm[16 * i:16 * i + 16] != case.xmm[16 * i:16 * i + 16])
    body = b"".join(struct.pack("<Q", gpr[i]) for i in range(16) if gmask >> i & 1)
    body += b"".join(xmm[16 * i:16 * i + 16] for i in range(16) if xmask >> i & 1)
    for a, d in sorted(mem):
        body += struct.pack("<QII", a, len(d), 0) + bytes(d) + bytes(-len(d) % 8)
    head = RES_HDR.pack(RES_HDR.size - 4 + len(body), index, outcome, detail, signo, sig_code, trapno, err, sig_addr,
                        (case.rip if rip is None else rip) & M64,
                        (case.rflags if rflags is None else rflags) & RFLAGS_VISIBLE,
                        case.mxcsr if mxcsr is None else mxcsr, insns, gmask, xmask, len(mem))
    return head + body


def unpack_batch(data):
    """The inverse of pack_batch, for tests and stand-ins of the runner: (header dict, [Case]).
    Blobs stored on the x86 machine come back as Blob objects with empty data and the right hash."""
    (magic, version, flags, time_limit_ms, group_size, batch_limit_s, n_blobs, n_sets, n_cases, _, _, _) = \
        BATCH_HDR.unpack_from(data, 0)
    if magic != MAGIC_BATCH:
        raise ValueError("not a batch")
    pos, blobs, stored = BATCH_HDR.size, [], []
    for _ in range(n_blobs):
        sha, n, where, _z = BLOB_HDR.unpack_from(data, pos)
        pos += BLOB_HDR.size
        b = Blob(b"")
        if where == 0:
            b = Blob(data[pos:pos + n])
            pos += (n + 7) // 8 * 8
            if b.sha != sha:
                raise ValueError("inline blob does not match its hash")
        else:
            b.sha = sha
            stored.append(sha)
        blobs.append(b)
    sets = []
    for _ in range(n_sets):
        n, _z = struct.unpack_from("<II", data, pos)
        pos += 8
        regs = []
        for _ in range(n):
            addr, size, prot, bi = REGION_REC.unpack_from(data, pos)
            pos += REGION_REC.size
            regs.append(Region(addr, None if bi == 0xFFFFFFFF else blobs[bi], prot, size))
        sets.append(tuple(regs))
    if struct.unpack_from("<I", data, pos)[0] != MAGIC_CASES:
        raise ValueError("case marker missing")
    pos += 8
    cases = []
    for _ in range(n_cases):
        f = CASE_HDR.unpack_from(data, pos)
        end = pos + 4 + f[0]
        q = pos + CASE_HDR.size
        stops = struct.unpack_from("<%dQ" % f[4], data, q)
        q += 8 * f[4]
        pokes = []
        for _ in range(f[5]):
            a, n, _z = struct.unpack_from("<QII", data, q)
            pokes.append((a, data[q + 16:q + 16 + n]))
            q += 16 + (n + 7) // 8 * 8
        if q != end:
            raise ValueError("case record length does not match its contents")
        cases.append(Case(sets[f[2]], f[8], gpr=f[11:27], rflags=f[9], xmm=f[27], mxcsr=f[7], gs_base=f[10],
                          max_insns=f[3], stops=stops, pokes=pokes, code_len=f[6], unchecked=bool(f[1] & 1)))
        pos = end
    if struct.unpack_from("<I", data, pos)[0] != MAGIC_END_IN or pos + 8 != len(data):
        raise ValueError("end marker missing")
    return {"version": version, "flags": flags, "time_limit_ms": time_limit_ms, "group_size": group_size,
            "batch_limit_s": batch_limit_s, "stored_blobs": stored}, cases


def no_answer(case):
    r = Result()
    r.outcome, r.detail, r.signo, r.sig_code, r.sig_addr, r.trapno, r.err = NO_ANSWER, 0, 0, 0, 0, 0, 0
    r.rip, r.rflags, r.mxcsr, r.insns = case.rip, case.rflags & RFLAGS_VISIBLE, case.mxcsr, 0
    r.gpr, r.xmm, r.mem, r.raw, r.cached, r.cpu = [x & M64 for x in case.gpr], case.xmm, [], b"", False, None
    return r


# ---- the wire ----------------------------------------------------------------------------------------

def pack_batch(cases, time_limit_ms=2000, group_size=64, batch_limit_s=120, remote_blobs=True):
    """Bytes for the runner's stdin, and the blobs it must find in its blob directory.  Cases
    are sent in the order given; results come back in the same order."""
    blobs, blob_index, sets, set_index, recs, memo = [], {}, [], {}, [], {}
    for c in cases:
        sk = regionset_key(c.regions, memo)
        si = set_index.get(sk)
        if si is None:
            si = set_index[sk] = len(sets)
            entry = []
            for r in c.regions:
                bi = 0xFFFFFFFF
                if r.blob is not None:
                    bi = blob_index.get(r.blob.sha)
                    if bi is None:
                        bi = blob_index[r.blob.sha] = len(blobs)
                        blobs.append(r.blob)
                entry.append(REGION_REC.pack(r.addr, r.size, r.prot, bi))
            sets.append(struct.pack("<II", len(entry), 0) + b"".join(entry))
        recs.append(c.record(si))
    out = [BATCH_HDR.pack(MAGIC_BATCH, FORMAT_VERSION, 0, time_limit_ms, group_size, batch_limit_s, len(blobs),
                          len(sets), len(cases), 0, 0, 0)]
    stored = []
    for b in blobs:
        far = remote_blobs and len(b.data) > INLINE_BLOB_MAX
        out.append(BLOB_HDR.pack(b.sha, len(b.data), 1 if far else 0, 0))
        if far:
            stored.append(b)
        else:
            out.append(b.data + bytes(-len(b.data) % 8))
    out += sets
    out.append(struct.pack("<II", MAGIC_CASES, 0))
    out += recs
    out.append(struct.pack("<II", MAGIC_END_IN, 0))
    return b"".join(out), stored


def parse_header(data):
    if len(data) < RESULT_HDR.size:
        raise OracleUnavailable("the runner's output ends inside its header (%d bytes)" % len(data))
    (magic, version, status, n, sig, ucode, flags, rev, xcr0, brand, kernel, build, message) = \
        RESULT_HDR.unpack_from(data, 0)
    if magic != MAGIC_RESULT:
        raise OracleError("the runner's output does not start with a result header")
    s = lambda b: b.split(b"\0")[0].decode("ascii", "replace").strip()          # noqa: E731
    return {"version": version, "status": status, "n": n, "cpu_signature": "%#x" % sig, "microcode": "%#x" % ucode,
            "aslr_off": bool(flags & 1), "fsgsbase": bool(flags & 2), "answers_rev": rev, "xcr0": "%#x" % xcr0,
            "cpu": s(brand), "kernel": s(kernel), "runner_build": s(build), "message": s(message)}


def parse_results(data, n_cases):
    """(header dict, [raw record bytes]) from the runner's stdout, in case order.  A stream that
    stops early is OracleUnavailable: the machine went away mid-batch."""
    hdr = parse_header(data)
    if hdr["status"] != 0:
        return hdr, []
    if hdr["version"] != FORMAT_VERSION or hdr["answers_rev"] != ANSWERS_REV:
        raise OracleError("the remote runner speaks format %d rev %d, this client %d rev %d" % (
            hdr["version"], hdr["answers_rev"], FORMAT_VERSION, ANSWERS_REV))
    pos, recs = RESULT_HDR.size, []
    for i in range(n_cases):
        if pos + 8 > len(data):
            raise OracleUnavailable("the runner's output ends after %d of %d results" % (i, n_cases))
        n, idx = struct.unpack_from("<II", data, pos)
        if idx != i or n < RES_HDR.size - 4 or pos + 4 + n > len(data):
            raise OracleUnavailable("the runner's output is cut or out of order at result %d of %d" % (i, n_cases))
        # the position in the batch is not part of the answer: stored records carry index 0
        recs.append(data[pos:pos + 4] + bytes(4) + data[pos + 8:pos + 4 + n])
        pos += 4 + n
    if len(data) != pos + 8 or struct.unpack_from("<II", data, pos) != (MAGIC_END_OUT, n_cases):
        raise OracleUnavailable("the runner's output has no end marker: the batch did not finish")
    return hdr, recs


# ---- the oracle ---------------------------------------------------------------------------------------

class Oracle:
    def __init__(self, family="misc", host=None, cache_root=None, offline=False, use_cache=True, time_limit_ms=2000,
                 group_size=64, batch_seconds=4.0, verbose=False):
        self.host = host or HOST
        if not self.host and not offline:
            raise OracleUnavailable("hworacle: no x86 machine named: host=user@host in .ref-host, or HWORACLE_HOST / TF_REF_HOST")
        self.offline, self.use_cache, self.verbose = offline, use_cache, verbose
        self.time_limit_ms, self.group_size, self.batch_seconds = time_limit_ms, group_size, batch_seconds
        self.cache = _load_sibling("hworacle_cache", "cache.py").Cache(cache_root, family) if use_cache else None
        self.ssh_calls = 0             # ssh processes started by this object
        self.sent = self.from_cache = 0
        self.remote_seconds = 0.0
        self.header = None             # the last result header: CPU, kernel, runner build
        self._blobs_there = None       # hex digests known to be in the remote blob directory

    # -- ssh --

    def _ssh(self, script, stdin=b"", timeout=60, ok=(0,)):
        """Run a shell script on the x86 machine: one ssh process, stdin in, stdout out.  Every
        script starts in the remote home directory and may only name paths under REMOTE_DIR."""
        if self.offline:
            raise OracleUnavailable("offline mode: not starting ssh")
        cmd = [SSH, "-o", "BatchMode=yes", "-o", "ConnectTimeout=10", "-o", "ServerAliveInterval=5",
               "-o", "ServerAliveCountMax=3", "-C", self.host, script]
        self.ssh_calls += 1
        t0 = time.time()
        try:
            p = subprocess.run(cmd, input=stdin, capture_output=True, timeout=timeout)
        except subprocess.TimeoutExpired:
            raise OracleUnavailable("no answer from %s within %d s" % (self.host, timeout)) from None
        except OSError as e:
            raise OracleUnavailable("cannot start %s: %s" % (SSH, e)) from None
        self.remote_seconds += time.time() - t0
        if p.returncode == 255:
            raise OracleUnavailable("ssh to %s failed: %s" % (self.host, p.stderr.decode("utf-8", "replace").strip()[-300:]))
        if p.returncode not in ok:
            raise OracleError("remote command exited %d: %s" % (p.returncode, p.stderr.decode("utf-8", "replace").strip()[-600:]))
        return p

    def build(self):
        """Ship runner.c and build it as ~/tf2-hw-oracle/bin/runner-<hash of the source and flags>."""
        h, d = RUNNER_HASH, REMOTE_DIR
        script = ("set -e; umask 077; mkdir -p {d}/bin {d}/src {d}/blobs; cd {d}; cat > src/.tmp-{h}.$$; "
                  "mv src/.tmp-{h}.$$ src/runner-{h}.c; "
                  "nice -n 10 timeout -k 2 120 gcc {flags} -DHWO_BUILD_ID='\"{h}\"' -o bin/.tmp-{h}.$$ src/runner-{h}.c; "
                  "mv bin/.tmp-{h}.$$ bin/runner-{h}; "
                  "ls -t bin/runner-* | tail -n +5 | xargs -r rm -f; ls -t src/runner-*.c | tail -n +5 | xargs -r rm -f"
                  ).format(d=d, h=h, flags=GCC_FLAGS)
        p = self._ssh(script, _SRC.encode(), timeout=180)
        if p.stderr.strip():
            raise OracleError("gcc on the x86 machine printed diagnostics for runner.c:\n" + p.stderr.decode("utf-8", "replace"))
        if self.verbose:
            print("hworacle: built runner-%s on %s" % (h, self.host), file=sys.stderr)

    def _runner(self, args, stdin, limit_s):
        """Run the remote runner; build it first if this version of the source is not there.
        Returns the raw stdout."""
        script = ("cd {d} 2>/dev/null && test -x bin/runner-{h} || exit 97; "
                  "exec nice -n 10 timeout -k 2 {t} bin/runner-{h} {a}").format(
                      d=REMOTE_DIR, h=RUNNER_HASH, t=int(limit_s) + 5, a=args)
        ok = (0, 4, 5, 97)
        p = self._ssh(script, stdin, timeout=limit_s + 40, ok=ok)
        if p.returncode == 97:
            self.build()
            p = self._ssh(script, stdin, timeout=limit_s + 40, ok=ok)
            if p.returncode == 97:
                raise OracleError("the runner is still missing on %s after building it" % self.host)
        return p.stdout

    def _ensure_blobs(self, blobs):
        if not blobs:
            return
        if self._blobs_there is None:
            p = self._ssh("mkdir -p {d}/blobs && ls {d}/blobs".format(d=REMOTE_DIR), timeout=40)
            self._blobs_there = set(p.stdout.decode().split())
        for b in blobs:
            hx = b.sha.hex()
            if hx in self._blobs_there:
                continue
            script = ("set -e; umask 077; cd {d}/blobs; cat > .tmp-{h}.$$; "
                      "test \"$(sha256sum < .tmp-{h}.$$ | cut -c1-64)\" = {h} || {{ rm -f .tmp-{h}.$$; exit 9; }}; "
                      "mv .tmp-{h}.$$ {h}").format(d=REMOTE_DIR, h=hx)
            self._ssh(script, b.data, timeout=60 + len(b.data) // 100000)
            self._blobs_there.add(hx)
            if self.verbose:
                print("hworacle: stored a %d-byte memory image on %s" % (len(b.data), self.host), file=sys.stderr)

    def _run_batch(self, cases, allow_unchecked, group_size):
        limit = 60 + int(len(cases) * self.time_limit_ms / 1000 * 0.02)
        data, stored = pack_batch(cases, self.time_limit_ms, group_size, min(limit, 600))
        args = "--blobs blobs" + (" --allow-unchecked" if allow_unchecked else "")
        for attempt in (0, 1):
            self._ensure_blobs(stored)
            out = self._runner(args, data, min(limit, 600))
            hdr, recs = parse_results(out, len(cases))
            self.header = hdr
            if hdr["status"] == 2:                           # a stored memory image is missing or damaged there
                if attempt:
                    raise OracleError("memory images keep disappearing from %s" % self.host)
                gone = {out[RESULT_HDR.size + 32 * i:RESULT_HDR.size + 32 * i + 32].hex() for i in range(hdr["n"])}
                self._blobs_there = (self._blobs_there or set()) - gone
                continue
            if hdr["status"] != 0:
                raise OracleError("the runner refused the batch: %s" % hdr["message"])
            if hdr["runner_build"] != RUNNER_HASH:
                raise OracleError("the remote runner is build %s, expected %s" % (hdr["runner_build"], RUNNER_HASH))
            return hdr, recs

    # -- the public call --

    def run(self, cases, offline=None, allow_unchecked=False, isolate=False, progress=None, refresh=False):
        """One Result per case, in order.  Cached answers are used first; the rest go to the
        machine unless offline (offline=True here, or on the Oracle, which a call cannot undo).
        allow_unchecked: honour Case.unchecked (sandbox tests only).
        isolate: one forked child per case instead of one per group of cases.  refresh: ask the
        machine even for cached cases; storing an answer that differs from the cached one raises
        the cache's CacheConflict (the hardware gave two answers to one question)."""
        offline = self.offline or bool(offline)        # an Oracle made offline never opens ssh, whatever a call asks
        cases = list(cases)
        results = [None] * len(cases)
        memo = {}
        keys = [c.key(memo) for c in cases]
        hits = self.cache.get_many(set(keys)) if self.cache is not None and not (refresh and not offline) else {}
        todo, first = [], {}
        for i, (c, k) in enumerate(zip(cases, keys)):
            raw = hits.get(k)
            if raw is not None:
                results[i] = decode_result(raw, c)
                results[i].cached = True
                meta = self.cache.meta_of(k) or {}
                results[i].cpu = meta.get("cpu")
                self.from_cache += 1
            elif offline:
                results[i] = no_answer(c)
            elif k in first:
                pass                                   # the same question twice in one call: ask once
            else:
                first[k] = i
                todo.append(i)
        if todo:
            for chunk in self._batches([cases[i] for i in todo], todo):
                idx = [i for i, _ in chunk]
                hdr, recs = self._run_batch([c for _, c in chunk], allow_unchecked, 1 if isolate else self.group_size)
                self.sent += len(idx)
                keep = {}
                for i, raw in zip(idx, recs):
                    results[i] = decode_result(raw, cases[i])
                    results[i].cpu = hdr["cpu"]
                    if cacheable(results[i].outcome, results[i].detail):
                        keep[keys[i]] = raw
                if keep and self.cache is not None:
                    meta = {k: hdr[k] for k in ("cpu", "cpu_signature", "microcode", "kernel", "runner_build")}
                    meta.update(format=FORMAT_VERSION, answers_rev=ANSWERS_REV, host=self.host,
                                written=time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()))
                    self.cache.put_many(keep, meta)
                if progress:
                    progress(len(idx))
            for i, k in enumerate(keys):               # duplicates of a question asked in this call
                if results[i] is None:
                    j = first[k]
                    results[i] = decode_result(results[j].raw, cases[i])
                    results[i].cpu = results[j].cpu
        return results

    def _batches(self, cases, indices):
        """Split into batches of a few seconds of remote work.  Cases that share memory stay
        together (one fork serves a group of them); the cost of a case is dominated by comparing
        its writable memory afterwards."""
        memo = {}
        order = sorted(range(len(cases)), key=lambda j: (regionset_key(cases[j].regions, memo), cases[j].gs_base & M64, j))
        batch, cost, size = [], 0.0, 0
        for j in order:
            c = cases[j]
            writable = sum(r.size for r in c.regions if r.prot & PROT_W)
            est = 30e-6 + writable * 0.25e-9 + (0.05 if c.max_insns != 1 else 0)
            rec = CASE_HDR.size + 8 * len(c.stops) + sum(16 + len(d) + 8 for _, d in c.pokes)
            if batch and (cost + est > self.batch_seconds or size + rec > (6 << 20)):
                yield batch
                batch, cost, size = [], 0.0, 0
            batch.append((indices[j], c))
            cost += est
            size += rec
        if batch:
            yield batch

    # -- housekeeping --

    def status(self):
        """Header dict from the remote runner's self-test (builds the runner if needed)."""
        return parse_header(self._runner("--selftest", b"", 30))

    def remote_processes(self):
        """Processes on the x86 machine that belong to this tool, as `ps` lines."""
        script = ("me=$(id -u); for p in /proc/[0-9]*; do [ \"$(stat -c %u $p 2>/dev/null)\" = \"$me\" ] || continue; "
                  "e=$(readlink $p/exe 2>/dev/null); c=$(readlink $p/cwd 2>/dev/null); "
                  "case \"$e $c\" in *{d}*) echo \"${{p#/proc/}} exe=$e cwd=$c\";; esac; done").format(d=REMOTE_DIR)
        p = self._ssh(script, timeout=40)
        return [ln for ln in p.stdout.decode().splitlines() if ln.strip()]

    def remote_listing(self):
        p = self._ssh("cd {d} 2>/dev/null && du -sh . && find . -maxdepth 2 -type f -printf '%10s  %p\\n' | sort -k2 || "
                      "echo '(no {d} directory)'".format(d=REMOTE_DIR), timeout=40)
        return p.stdout.decode()

    def purge_remote(self):
        self._ssh("cd {d} 2>/dev/null && rm -f bin/runner-* bin/.tmp-* src/runner-*.c src/.tmp-* blobs/* blobs/.tmp-* || true"
                  .format(d=REMOTE_DIR), timeout=40)
        self._blobs_there = None


# ---- small helpers for hand-written cases --------------------------------------------------------------

CODE_ADDR = 0x0000200000000000         # the harness's image base (docs/DESIGN.md section 9)
DATA_ADDR = 0x0000210000000000         # its arena
STACK_ADDR = 0x0000220000000000


def code_region(code, addr=CODE_ADDR, writable=False):
    return Region(addr, bytes(code), PROT_R | PROT_X | (PROT_W if writable else 0))


def xmm_bytes(values):
    """256 bytes from {register index: bytes or int (little-endian 128-bit)}."""
    out = bytearray(256)
    for i, v in values.items():
        out[16 * i:16 * i + 16] = v.to_bytes(16, "little") if isinstance(v, int) else bytes(v).ljust(16, b"\0")
    return bytes(out)


def gpr_list(**named):
    """16 registers from keywords: gpr_list(rax=1, rsp=0x220000010000)."""
    names = "rax rcx rdx rbx rsp rbp rsi rdi r8 r9 r10 r11 r12 r13 r14 r15".split()
    out = [0] * 16
    for k, v in named.items():
        out[names.index(k)] = v & M64
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=["status", "build", "ps", "remote-ls", "purge"])
    ap.add_argument("--host", default=None)
    args = ap.parse_args()
    ora = Oracle(host=args.host, use_cache=False, verbose=True)
    try:
        if args.command == "status":
            h = ora.status()
            print("%s: %s" % (ora.host, h["message"] or "status %d" % h["status"]))
            for k in ("cpu", "cpu_signature", "microcode", "kernel", "xcr0", "aslr_off", "fsgsbase", "runner_build"):
                print("  %-14s %s" % (k, h[k]))
            print("  %-14s format %d, answers revision %d, runner source %s" % ("this client", FORMAT_VERSION, ANSWERS_REV, RUNNER_HASH))
            return 0 if h["status"] == 0 else 1
        if args.command == "build":
            ora.build()
            return 0
        if args.command == "ps":
            procs = ora.remote_processes()
            print("\n".join(procs) if procs else "nothing of ours is running on %s" % ora.host)
            return 1 if procs else 0
        if args.command == "remote-ls":
            print(ora.remote_listing(), end="")
            return 0
        ora.purge_remote()
        print("removed the runner builds, sources and memory images under ~/%s on %s" % (REMOTE_DIR, ora.host))
        return 0
    except OracleUnavailable as e:
        print("x86 machine unavailable: %s" % e, file=sys.stderr)
        return 3
    except OracleError as e:
        print("oracle error: %s" % e, file=sys.stderr)
        return 4


if __name__ == "__main__":
    sys.exit(main())
