# hworacle: a real x86-64 CPU as a test oracle

The differential harness checks lifted code against Unicorn, and Unicorn (QEMU 5.0) is wrong in
places. This tool asks a real Intel CPU instead: you give it single-instruction cases (memory,
registers, flags, XMM, MXCSR, `gs` base, start address), it returns what the hardware did
(state afterwards, changed memory bytes, or the fault). Answers are cached, so a family of cases
that has been asked once can be checked with the x86 machine switched off.

| File | What it is |
|---|---|
| `runner.c` | Runs on the x86 machine. One batch on stdin, one result per case on stdout, then it exits. |
| `client.py` | `Oracle.run(cases) -> results`; ships and builds the runner, batches, caches. Also a small CLI. |
| `cache.py` | The answer store under `inventory/hworacle_cache/` (shared by all worktrees, not in git). |
| `crosscheck.py` | Unicorn against the hardware over the harness's own instruction cases and the SSE cases of `experiments/afp`. |
| `sweep.py`, `sweep_m3.c` | `rcpss`/`rsqrtss` on Intel against `frecpe`/`frsqrte` on the M3, every input. |
| `tests/` | `make hworacle-test` (no other machine needed) and `make hworacle-live` (needs it). |

    make hworacle-test         byte inspection, wire format, cache, client against a stand-in for ssh, offline mode
    make hworacle-live         known answers on the real CPU, sandbox, time limit, cache proof
    make hworacle-status       is the machine there, which CPU, is the runner built
    make hworacle-crosscheck   emulator against hardware (answers already cached are not asked again)
    make hworacle-sweep        the rcp/rsqrt sweep

## Using it from a test

```python
import sys; sys.path.insert(0, "tools/hworacle")
import client as C

regions = (C.Region(0x200000000000, bytes.fromhex("4801d8"), C.PROT_R | C.PROT_X),   # add rax, rbx
           C.Region(0x210000000000, None, C.PROT_R | C.PROT_W, size=0x2000))          # two zeroed pages
case = C.Case(regions, rip=0x200000000000, gpr=C.gpr_list(rax=1, rbx=-1), rflags=0)
ora = C.Oracle(family="my-tests")            # offline=True: never open ssh
r = ora.run([case])[0]
assert r.outcome == C.DONE and r.gpr[0] == 0 and r.rflags == 0x55        # CF PF AF ZF
```

- A `Case` is the complete question: `regions` (share one tuple between the cases that use the
  same memory), `rip`, `gpr` (16 values in encoding order: rax rcx rdx rbx rsp rbp rsi rdi r8..r15),
  `rflags` (only CF PF AF ZF SF DF OF are used), `xmm` (256 bytes), `mxcsr`, `gs_base`, `pokes`
  (`[(address, bytes)]` written over the regions first; writable regions only), `stops`,
  `max_insns`.
- A `Result` has `outcome` (`DONE`, `FAULT`, `REFUSED`, `TIMEOUT`, `KILLED`, `BAD_CASE`,
  `RUNNER_ERROR`, or `NO_ANSWER` in offline mode), the full `gpr`, `xmm`, `rflags`, `mxcsr`, `rip`
  afterwards, `mem` (runs of bytes that differ from the initial memory), and for a fault `signo`,
  `sig_code`, `sig_addr`, `trapno` (the CPU exception number) and `err` (page fault: bit 1 = write,
  bit 4 = instruction fetch; other bits are masked because they depend on the kernel's paging).
  `r.describe()` says it in words; `C.final_mem(case, r, addr, n)` gives memory contents after the run.
- Addresses are the harness's own (image at `0x2000_0000_0000`, arena `0x2100_...`, stack
  `0x2200_...`); they are mapped at exactly those addresses on Linux, so RIP-relative and absolute
  operands mean the same on both sides.
- Control transfers (`jcc`, `jmp`, `call`, `ret`, `loop`) need `stops`: the addresses where the
  case may end. The instruction at a stop address is never executed. A result that ends elsewhere
  has `unexpected_rip` set.
- `max_insns=N` (N > 1) single-steps N instructions, each inspected before it runs. `max_insns=0`
  is a free-running program of `code_len` bytes (see "What is refused").
- `Oracle.run(..., refresh=True)` asks the machine again even for cached cases; if the new answer
  differs from the stored one the cache raises `CacheConflict` (that would be non-determinism).
- `Oracle(offline=True)` never opens ssh, whatever a later call asks; `run(..., offline=True)`
  makes one call offline.
- Limits per case: 64 regions, 64 stop addresses, 256 pokes, 1 MiB of poked bytes. A poke, and
  the bytes of the instruction at `rip`, must each lie inside one region.

### What a case does not contain (format 1)

x87 and MMX state, the upper halves of YMM, `fs` base, debug registers. They are set to a fixed
state before every case (x87 as after `fninit`, upper YMM zero) and are not reported; instructions
that need them are refused. The trap flag is the runner's: `pushfq` is reported with TF clear and
IF set, exactly what user-mode code sees on a real machine. MXCSR is part of the case and of the
result, including its status flags.

## The cache

`inventory/hworacle_cache/<family>/seg-*.hwc`. The key of a case is a SHA-256 over every region
(address, size, protection, hash of contents), the whole register state, pokes, stops, limits,
the format version and `HWO_ANSWERS_REV` (bump that constant in `runner.c` whenever a runner change
could alter an answer; old answers then stop matching). The key is computed from the case as
it is at each call, and `rflags` goes into it whole, so two cases that differ only in flag bits
the runner ignores are stored twice. The value is the runner's result record;
each segment file records the CPU model, microcode, kernel and runner build that produced it.
Segments are written to a temporary name and renamed, never modified; two worktrees can write at
once. Only facts about the case are stored (done, fault, refusal by inspection); timeouts, kills
and runner errors describe one run and are not.

    .venv/bin/python tools/hworacle/cache.py stats
    .venv/bin/python tools/hworacle/cache.py compact FAMILY

`family` is only a directory: lookups read the family you name, so use one family per body of
cases (`known`, `xc-tier0`, `xc-float`, `sweep`, ...).

## The x86 machine, and the rules for it

`ssh -o BatchMode=yes <host>`, where the host is `host=user@host` in `.ref-host` at the checkout's root
(git-ignored; or `TF_REF_HOST`, or `HWORACLE_HOST`). It is the owner's
home machine, not a test server:

- Everything of ours lives in `~/tf2-hw-oracle/` (`bin/runner-<hash>`, `src/runner-<hash>.c`,
  `blobs/<sha256>`: memory images, sent once and reused). Nothing is written anywhere else. Never
  read, write or list `/mnt/2tb_hdd` or any other mount.
- No sudo, nothing system-wide, no daemons, no cron, no listening sockets. The Mac starts one ssh
  process per batch; the remote command is `nice -n 10 timeout ... bin/runner-<hash>`; it reads
  stdin, writes stdout and exits.
- The house loses power without warning: batches are a few seconds long, nothing on that machine
  matters (the memory images are verified by SHA-256 every time they are loaded and re-sent if
  missing or damaged), and a batch that does not come back complete raises `OracleUnavailable`
  with nothing stored for it.
- Before you finish: `.venv/bin/python tools/hworacle/client.py ps` must say nothing is running.
- `client.py remote-ls` lists what is there (the memory images include relocated images of game
  DLLs; the game is installed on that machine too). `client.py purge` deletes it all.

### Rebuilding the remote side

Nothing to do by hand. The runner binary is named after the SHA-256 of `runner.c` plus the compiler
flags; every batch command first checks that this exact binary exists (`exit 97` if not), and the
client then sends the source and builds it with the machine's gcc
(`gcc -O2 -static-pie -fno-stack-protector -Wall -Wextra -Werror`: a warning is a failed build and
nothing is installed).
`client.py build` forces it; `client.py status` runs the runner's self-test and prints the CPU.

## How the runner executes a case

The parent maps the case's regions at their addresses (`MAP_FIXED_NOREPLACE`: a region that would
land on the runner's own memory is a `BAD_CASE`), forks, and the child enters seccomp strict mode
and executes `int3`. The SIGTRAP handler saves the runner's whole register state from the signal
frame and overwrites the frame with the guest's, with the trap flag set; the kernel's sigreturn
loads all of it at once. The guest executes one instruction; the single-step trap, or the fault,
returns to the handler, which copies the guest state out and puts the runner's back. Nothing of
the runner's registers or stack is visible to the guest, and the runner loses nothing.

- Determinism: the runner re-executes itself with address-space randomisation off, x87 and upper
  YMM start in their initial state, every byte of mapped memory is given by the case, and the
  page-fault error code is reduced to its architectural bits. The live test asks every known
  answer twice, grouped and one child per case, and requires identical bytes.
- Memory changes are found by comparing every writable region with a pristine copy afterwards;
  the copy is also what restores memory before the next case of the same child.
- At start-up the runner runs a self-test of the state swap (a `nop` with every register, flag,
  XMM byte and MXCSR bit patterned must change nothing; add, paddq, a `gs` load, a store, a
  refused `syscall`, upper YMM zero, x87 fresh). If it fails, no batch runs. A kernel or CPU on
  which the signal-frame layout differs is caught there, not in cached answers.
- `REP` string instructions take one single-step trap per iteration; the runner continues until
  the instruction is finished. Free-running programs run without the trap flag.

## The sandbox

1. **Byte inspection** (`hwo_decode`, an allow-list: anything not listed is refused).
2. **seccomp strict mode** in the child, entered before any guest instruction: the only system
   calls left are `read`, `write`, `_exit`, `sigreturn`; anything else is SIGKILL. The child holds
   one file descriptor, the pipe to its parent. If seccomp cannot be entered, nothing runs.
3. **Time limit**: the parent waits for each result with a deadline (2 s by default) and kills a
   late child; the result is `TIMEOUT`. The parent itself dies at a whole-batch deadline
   (`alarm`), the remote command runs under `timeout`, and a child whose parent dies is killed by
   the kernel (`PR_SET_PDEATHSIG`). The parent never executes a guest instruction.

A killed or late child costs that one case; a new child takes the rest of the group.

### What is refused, and why

| Reason | Instructions | Why |
|---|---|---|
| 17 system call | `syscall`, `sysenter`, `sysret`, `sysexit` | they leave the sandbox's first line of defence |
| 18 software interrupt | `int n`, `int1` | same (`int 0x80` is a system call); `int3` is allowed: it only traps |
| 19 | `hlt` | |
| 20 port I/O | `in`, `out`, `ins`, `outs` | |
| 21 privileged | `mov cr/dr`, `lgdt`, `rdmsr`, `cli`, `sti`, `xgetbv`, `rdtscp`, `lar`, `lsl`, ... | privileged, or they read system tables |
| 22 machine-dependent | `rdtsc`, `rdpmc`, `cpuid`, `rdrand`, `rdseed` | the answer is the machine's, not the instruction's |
| 23 segment | `mov sreg`, push/pop `fs`/`gs`, `lfs`.., `rd/wrfsbase`, any `fs:` override | the `fs` base belongs to the runner; `gs:` is fine |
| 24 x87 or MMX | `D8`..`DF`, `fwait`, MMX forms, `emms` | that state is not in a case (format 1) |
| 25 VEX/EVEX/XOP | all | upper YMM is not in a case (format 1) |
| 26 far transfer | far `call`/`jmp`/`ret`, `iret` | |
| 27 | not an instruction in 64-bit mode | |
| 28 | the bytes end before the instruction does, or it is longer than 15 bytes | |
| 29 prefix combination | `66` on a near branch (Intel and AMD disagree on its length), `66` with `F2`/`F3` on an SSE opcode, `F2`/`F3` on `cmov`, `setcc`, ... | the decoder does not claim to know what they mean |
| 30 state save/restore | `fxsave`, `fxrstor`, `xsave`, `xrstor`, `xsaveopt` | they dump or load state that is not in a case |
| 31 | a control transfer in a case with no `stops` | the case must say where it ends |
| 32 | `rip` is not inside an executable region of the case | otherwise it would run the runner's own memory |
| 33 | a free-running program that fails the static check | see below |
| 34 | an `unchecked` case when the caller did not allow it | |
| 16 not on the allow-list | everything else | nobody needed it yet; add it to `hwo_map*` with a test |

A free-running program (`max_insns=0`) runs without single-stepping, so all of it is checked
beforehand: its code region must not be writable, the `code_len` bytes are decoded front to back,
every instruction must be allowed, every branch must be direct and land on an instruction of the
program or on a stop address, and there is no `call`, `ret` or indirect jump. Stop addresses must
fault on arrival (unmapped, or in a non-executable region of the case).

`Case(unchecked=True)` with `Oracle.run(..., allow_unchecked=True)` skips the inspection. It
exists for one purpose, `make hworacle-live` proving that seccomp kills `syscall`; the cases used
there (`getpid`, `getppid`) would be harmless even if the sandbox failed.

`tests/test_client.py` runs the client against `tests/fake_ssh.py`, a stand-in for ssh and the
remote side with failure injection (unreachable, cut streams, lost memory images, failed builds),
and checks every remote command text against the rules above. `tests/test_refusal.py` checks the
decoder against Capstone on 690,880 encodings (220,411 allowed:
same length, class, `rep`, branch displacement), hand-written must-refuse and must-allow tables,
every must-refuse encoding behind every legal prefix, and every instruction of the game and corpus
DLLs when present.

## The wire format (version 1, little-endian; structs in `runner.c`, `struct.Struct`s in `client.py`)

Batch (stdin):

    batch_hdr   48 bytes  "HWOB", version, flags, time_limit_ms, group_size, batch_limit_s,
                          n_blobs, n_sets, n_cases, 3 reserved words
    n_blobs  x  blob_hdr 48 bytes: sha256[32], len u64, where u32 (0 inline: len bytes follow,
                          padded to 8; 1: the file blobs/<sha256 hex>), reserved u32
    n_sets   x  u32 n_regions, u32 0, then n_regions x region 24 bytes: addr u64, size u64,
                          prot u32 (1 r, 2 w, 4 x), blob index u32 (0xFFFFFFFF: zeroes)
    "HWOC", u32 0
    n_cases  x  case_hdr 440 bytes: rec_len u32 (bytes after this field), flags u32 (1: unchecked),
                          set u32, max_insns u32, n_stops u32, n_pokes u32, code_len u32, mxcsr u32,
                          rip u64, rflags u64, gs_base u64, gpr[16] u64, xmm[16][16]
                then n_stops x u64, then n_pokes x {addr u64, len u32, u32 0, bytes padded to 8}
    "HWOZ", u32 0

Results (stdout):

    result_hdr  312 bytes "HWOR", version, status (0 ok, 1 bad input, 2 memory images missing,
                          3 self-test failed), n_results, cpuid(1).eax, microcode, flags
                          (1: randomisation off, 2: FSGSBASE), answers_rev, xcr0 u64,
                          brand[48], kernel[64], build[32], message[128]
    n_results x res_hdr 72 bytes: rec_len u32, index u32, outcome u32, detail u32, signo i32,
                          si_code i32, trapno u32, err u32, si_addr u64, rip u64, rflags u64,
                          mxcsr u32, insns u32, gpr_mask u16, xmm_mask u16, n_mem u32
                then 8 bytes per set bit of gpr_mask, 16 per set bit of xmm_mask (registers whose
                value differs from the case's, in index order),
                then n_mem x {addr u64, len u32, u32 0, bytes padded to 8}
    "HWOE", n_results

`outcome`: 0 done (`detail` bit 0: at a stop address, bit 1: not where expected), 1 fault,
2 refused (`detail` = reason), 3 timeout, 4 killed (`detail` = signal), 5 bad case, 6 runner
error. `detail` bit 8: more memory changed than fits a record (256 KiB), the runs given are the
lowest. With status 2 the header is followed by `n_results` 32-byte digests of the missing images.
Cases are grouped per forked child by region set and `gs` base (`group_size`, 1 = a child per case).
The format has room for more: `flags` in the batch header and in each case, `max_insns`, and a
version field checked on both sides.

## Throughput (2026-10-08, Tailscale relayed at about 2.6 MB/s each way)

| Kind of case | End to end |
|---|---|
| harness instruction cases (1.3 MiB of writable memory each, random registers) | 1,300 to 3,200 cases/s (the low end includes sending the memory images the first time) |
| register-only cases (the SSE families) | about 19,000 cases/s |
| free-running program | about 1 ns per loop iteration, plus 0.35 s per ssh process |

The link is the limit for random-state cases (440 bytes each, incompressible); the remote side
spends most of its time comparing writable memory.
