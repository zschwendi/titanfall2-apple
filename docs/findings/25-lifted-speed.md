# 25. Lifted code against the same C built natively: is it right, and how fast is it

## Summary

Lifted code runs **3.4 times slower than native** on the test corpus today: geometric mean 3.36 (best
round) or 3.44 (median round) over 194 functions built from the same source both ways; range 0.92x to 26x.
By kind: switch 2.1x, arithmetic 2.8x, atomics 3.1x, memory loops 3.5x, call-heavy 4.1x, indirect calls 7.8x.
Main cause: registers and flags live in a `Ctx` that the compiler must assume guest memory can alias, so
every guest load or store forces six flags and the changed registers out and back. Declaring the `Ctx`
unaliased gives 2.9x, flags dead at `ret`/`call` 2.5x, a direct-mapped dispatch table 2.4x (all measured).
**2x is reachable for loop code** (by hand, registers in locals: 1.2x and 1.1x), **not yet for code that
is mostly calls** (`fib` 2.8x by hand; call-heavy 2.5x and indirect calls 4.0x after the three changes).
Correctness: 283 functions compared over 5.66 million cases, 280 identical; the 3 mismatches are one lifter
bug (a function reached only through `lea` is never lifted). x86-ordering mode: no cost on average, measured.

## Method

**Two builds of the same functions.** For each corpus source `tests/corpus/<file>.c`:

- *native*: compiled for arm64 macOS with the corpus's own flags minus the Windows target
  (`-O2 -ffreestanding -fno-stack-protector -fno-exceptions -fno-math-errno`), together with
  `mem_str.c`, into `bench/build/native/<file>.dylib`. A force-included header
  (`bench/native/prefix.h`) stands in for `corpus.h`: `EXPORT` and `NOINLINE` become
  attributes, `usize` becomes `unsigned long`, the Microsoft `va_list` builtins map to the
  native ones. The corpus is not edited.
- *lifted*: `tests/corpus/build/one_<file>_O2.dll` lifted with
  `python -m lifter.lift --module one_<file> --dll ...`, the generated C compiled at `-O2`
  with `-fno-strict-aliasing -fwrapv` (what `harness/difftest.py` uses) and the really-linked
  macro set of `harness/runtime.h`. The driver maps the DLL image at a fixed address, applies
  its base relocations and sets `g_base_one_<file>`.

Two files need a different native side:

- `intrare.c` is x86 inline assembly from top to bottom and cannot be built for arm64. Its
  native side is `bench/native/intrare_ref.c`, a C reference written from the instruction
  definitions. Its comparisons are a correctness oracle; its timings are reported as kind
  `probe` and **left out of the headline**, because they are not the same source twice.
- `abi.c` includes `<emmintrin.h>`; `bench/native/emmintrin.h` supplies the nine intrinsics
  it uses, so that the file compiles. The functions that use them are float code and are
  not compared.

**The driver** (`bench/driver.c`) calls each function described in `bench/functions/*.tbl`
through both builds. Natively it is a C call with the function's real prototype (generated
by `bench/gen.py`). Lifted, the arguments are placed as a Windows x64 caller places them:
rcx, rdx, r8, r9, then 8-byte stack slots above 32 bytes of home space; structs of 1, 2, 4 or
8 bytes in the slot, other structs and 128-bit integers by address of a caller copy; a hidden
first argument for a struct result that does not fit in rax. The guest stack is a real 1 MiB
mapping with a sentinel return address. Results come back in rax, through the hidden pointer,
or in xmm0 for a 128-bit integer. Indirect calls go through `rt_dispatch`, which looks the
target up in the module's table by binary search.

**Inputs** come from a generator seeded by a fixed seed and the function's name: random 64-bit
values mixed with small values, boundary values and powers of two, reduced to the argument's
type; one time in six an integer repeats an earlier integer argument or lands next to it;
buffers of random, low-entropy or structured bytes as the table row asks; structs sometimes
all zero or all ones. On the lifted side the six status flags and every register that is not
an argument are random at entry. Both sides get the same values and the same buffers at the
same addresses, one after the other.

**Compared** for every case: the result (the bits its type defines), every byte of every
buffer and of the guard bytes around them. On the lifted side also: rsp is entry + 8 on
return, rbx, rbp, rsi, rdi, r12-r15 and xmm6-xmm15 are unchanged, the direction flag is clear,
and a struct result's address is back in rax. A lifted call that traps, calls an address that
is no lifted function, faults, or does not return is a mismatch with that reason. Each
function is compared in a child process, so that a translation which damages memory costs
one function and not the run.

**Timing.** 32 prepared inputs per function; one round is as many passes over them as fit in
1 ms; at least 30 rounds per side, alternating which side goes first, and more (up to 120)
until the best round of each side is reproduced within 2% by two others. The native call is a
call through a pointer with arguments loaded from the prepared case. The lifted call is five
stores into the `Ctx` (four argument registers and rsp), a call through a pointer and a load
of rax. Each round checks the CPU number before and after and is repeated if it touched an
efficiency core.

Machine: Apple M3 Pro (5 performance and 6 efficiency cores), macOS 27.0.1, Apple clang
21.0.0. `bench/build/driver --cores` shows which CPU numbers are which: 0 to 5 take 1.13 ns
per step of a multiply-add chain, 6 to 10 take 0.84 ns.

Run it: `make bench-smoke` (3,000 cases per function and a short timing pass, about ten
seconds), `make bench` (20,000 cases, the timing above), `make bench-gap` (the variants
below), `make bench-mutants`, `make bench-tso`. `bench/README.md` has the details and how to
add a function. The raw results quoted here are in `bench/results/2026-10-09/`.

## Correctness

| Corpus file | Described | Lifted and compared | Identical | Mismatch | Not lifted |
|---|---:|---:|---:|---:|---:|
| `abi.c` | 61 | 51 | 51 | 0 | 10 |
| `atomics.c` | 13 | 13 | 13 | 0 | 0 |
| `calls.c` | 11 | 10 | 10 | 0 | 1 |
| `cpp2.cpp` | 69 | 61 | 60 | 1 | 8 |
| `int_arith.c` | 24 | 24 | 24 | 0 | 0 |
| `intrare.c` (against the C reference) | 110 | 86 | 86 | 0 | 24 |
| `mem_str.c` | 15 | 13 | 11 | 2 | 2 |
| `structs.c` | 14 | 14 | 14 | 0 | 0 |
| `switches.c` | 7 | 7 | 7 | 0 | 0 |
| `virt.cpp` | 4 | 4 | 4 | 0 | 0 |
| **Total** | **328** | **283** | **280** | **3** | **45** |

20,000 cases per function in `make bench` (5.66 million compared cases), 3,000 in
`make bench-smoke`. No case needed code the lifter skipped, no calling-convention invariant
was violated, and no native call faulted.

Not described at all: functions with float or double arguments or results (the table has no
float kind and the lifter does not translate float code yet), `ab_fp_checked` (it takes the
address of a function that is static in the corpus) and `ir_enter` (it returns a stack
address).

### Every mismatch

All three have the same cause and are listed in `bench/known_mismatches.tbl` as `lifter-bug`.

*Fixed: discovery now takes
the target of a RIP-relative `lea` as a root, the three functions compare identical and the
rows have left the table. The text below describes the state the measurements were taken in.*

**L1. A function whose address is taken only by `lea` is never lifted** (lifter bug, in
function discovery).

| Function | What happens | Cases |
|---|---|---|
| `mem_str.c` `sort_ints` | the indirect call to the comparison function finds no lifted function | 2,264 of 3,000 |
| `mem_str.c` `sort_shorts_desc` | the same | 2,234 of 3,000 |
| `cpp2.cpp` `cx_pmf_derived` | the indirect call through a pointer to a virtual member function finds no lifted function | 1,528 of 3,000 |

Evidence, `sort_ints` (`one_mem_str_O2.dll`):

```
1dc0: and  edx, 0xf
1dc3: lea  r9, [rip + 0xf6]      ; r9 = 0x1ec0 = cmp_i32
1dca: mov  r8d, 4
1dd0: jmp  0x1de0                ; sort_generic, which does `call r9`
```

`cmp_i32` at 0x1ec0 is a leaf: it has no unwind entry, is not exported, is not the target of
a direct call or jump, and no relocated pointer refers to it (a RIP-relative `lea` needs no
relocation). Those are the four sources `docs/DESIGN.md` section 2 lists, so the lifter's
report for the module has no function at 0x1ec0, and `rt_dispatch` is asked for an address
that is not in the table. The driver prints
`lifted code called guest address 0x300600001ec0, which is no lifted function`. The cases that
pass are the ones with fewer than two elements, where the comparison is never called.

`cx_pmf_derived` is the same through a different door. `&AccV::sub` is a pointer to a virtual
member function; the Microsoft ABI represents it as the address of a three-instruction thunk
(`mov rax,[rcx]; mov rax,[rax]; jmp rax` at 0x12d0), and the function selects between two such
addresses with `lea rax, [rip + 0x32]; lea r9, [rip + 0x3b]; cmove r9, rax; ...; call r9`.
0x12c0 is lifted only because a table in `.rdata` also points at it; 0x12d0 is not.

In the real program this is a logged trap, not a silent wrong result. It matters for the game
because Microsoft's compiler passes callbacks the same way (`lea rdx, comparator`), and small
leaf functions, the ones without unwind entries, are exactly comparators and member-pointer
thunks. A fix on the lifter's side: treat the target of every `lea reg, [rip + x]` that lands
in an executable section as a candidate function entry, under the same checks as a relocated
pointer.

**Corpus undefined behaviour: none found. Driver bugs left: none known.** The two compilers
were free to differ on signed overflow and oversized shifts in the corpus (`dense`, `clamp`,
`list_build` and others); on these inputs they do not.

One thing found on the way that is not a mismatch: `abi.c` says 128-bit integers are
"returned through a hidden pointer". This compiler returns them in xmm0
(`movq xmm0, rax; movq xmm1, rdx; punpcklqdq xmm0, xmm1`), which is why `ab_u128_add` and its
neighbours are skipped today for `punpcklqdq`. The driver reads 128-bit results from xmm0.

### Functions the lifter skips

45 described functions are not lifted and so not compared. They join the comparison without
any change here when the lifter learns them.

| Reason | Functions |
|---|---|
| Vector instruction (`pshufd`, `paddd`, `punpcklqdq`, `psadbw`, ...) | 15: `crc32`, `utoa10`, `ab_s16_add`, six `ab_u128_*`, five `cx_tpl_sum_*`/`cx_tpl_dot_*`, `cx_v4_add` |
| Indirect jump through a table of code pointers (`jmp [r9 + rax*8]`) | 4: `dispatch`, `ab_fp_table`, `ab_goto_tab`, `ab_goto_loop` |
| Jump table whose set-up can be reached another way | 1: `cx_iface_arg` |
| Instruction not translated: `rcl`/`rcr` 6, `movbe` 4, `stmxcsr`/`ldmxcsr` 3, `loop`/`loope`/`loopne` 3, `xlatb` 2, `andn` 2, `pushfq`/`popfq` 2, `lahf` 1, `sahf` 1 | 24, all in `intrare.c` |

`memcpy`, `memset` and `memmove` in `mem_str.c` are skipped too (vectorised), but no described
function reaches them: the compiler inlined the copies it needed.

### Would the comparison notice a wrong translation?

`make bench-mutants` breaks the lifted C on purpose, one way at a time and in every function
at once (every 32-bit subtract computed as an add, every `jl` taken as `jle`, every byte
store dropped, and so on), rebuilds, and counts how many of the functions whose text changed
are flagged. Only functions that pass unmodified are counted.

| Wrong translation | Functions changed | Flagged | Not flagged |
|---|---:|---:|---|
| 32-bit `sub`/`cmp` computed as `add` | 55 | 51 | `atomic_cas`, `spin_try`, `ir_bsf_mem`, `ir_lock_sub` |
| 64-bit `add` computed as `sub` | 74 | 73 | `atomic_add64` |
| "less" taken as "less or equal" | 4 | 4 | |
| "below" taken as "below or equal" | 25 | 20 | `big_frame`, `copy_words`, `overlap_move`, `list_build`, `interp` |
| "equal" taken as "not equal" | 57 | 57 | |
| "sign" taken as "not sign" | 1 | 1 | |
| byte stores dropped | 9 | 9 | |
| 32-bit loads read 16 bits | 53 | 47 | `ab_rec_stack`, `ab_sw_chain`, `atomic_or`, `atomic_and`, `big_frame`, `member_ptr` |
| 32-bit `shl` computed as `shr` | 14 | 13 | `cx_pdm_swap` |
| low bit of every 32-bit `imul` result flipped | 34 | 33 | `big_frame` |
| `inc` adds 2 | 13 | 13 | |
| **eleven mutations** | **339** | **321** | 18 |
| a 32-bit register write keeps the old upper half | 227 | 86 | 141 |

Each of the 18 was looked at:

- 15 cannot be seen from outside the function, so they are not gaps in the inputs. The
  changed arithmetic only feeds flags that nothing reads (`lock cmpxchg`, `lock xadd`,
  `lock sub`), or compares with zero where add and subtract agree; the changed branch chooses
  between two code paths that copy the same bytes (`copy_words`, `overlap_move`) or guards a
  limit the function cannot reach (the 256-step limit of `interp`, the 16-node limit of the
  list walk inlined into `list_build`); the 16-bit load reads a value that is below 65536 by
  construction, or feeds a compare-and-swap loop that retries with the right value; the
  swapped pair in `cx_pdm_swap` is swapped either way round.
- 3 are `big_frame`, and that is a weakness of the corpus function: it returns the XOR of
  `buf[i] + buf[511 - i]` for i = 0, 7, ..., 511, every such sum occurs twice, so **the
  result is always 0** whatever the arithmetic does. This comparison sees only its result;
  the emulator-based function test also compares the stack pages and is not blind to it.

The last row is a different kind: the upper half of a register after a 32-bit write is not
observable through a 32-bit result, so most functions cannot show it; the 86 that do are the
ones that use the register as a 64-bit value afterwards. Those 141 were not looked at one
by one.

The first run of this check found real gaps, since closed: status flags were zero at entry
(a translation that read a stale flag passed), two independent random integers almost never
met (so "less" against "less or equal" passed in `dense`), `str_len` never saw a string
longer than its 4096-byte limit, and a wrong translation that ran off into the driver's
memory killed the whole run instead of failing one function.

## Speed

`make bench` with the machine under load from other sessions; all 280 timings on performance
cores (`bench/results/2026-10-09/O2-full.json`). Ratio = lifted ns per call / native ns per
call.

| Kind | Functions | Geometric mean (best round) | (median round) | Range |
|---|---:|---:|---:|---|
| tight arithmetic | 79 | 2.75 | 2.91 | 1.01 to 6.43 |
| memory loops | 38 | 3.48 | 3.53 | 1.18 to 7.56 |
| call-heavy | 35 | 4.07 | 4.11 | 0.92 to 13.98 |
| switch dispatch | 12 | 2.11 | 2.11 | 1.59 to 4.20 |
| indirect calls (virtual, function pointers) | 17 | 7.82 | 7.83 | 4.23 to 26.00 |
| atomics | 13 | 3.13 | 2.92 | 2.23 to 5.65 |
| **all of the above** | **194** | **3.36** | **3.44** | 0.92 to 26.00 |
| `intrare.c` probes (native side is the C reference) | 86 | 3.01 | 3.12 | 1.42 to 15.77 |

Of the 194: 36 are within 2x, 85 within 3x, 46 above 5x. Quartiles 2.37, 3.23, 4.95. Summed
over all 194 functions (so that long functions weigh more), lifted takes 3.34 times the native
time.

Spread: the median round is 3.8% above the best round for the native side (90th percentile
12.3%) and 5.4% for the lifted side (90th percentile 24.6%). 19 functions did not have their
best round reproduced within 2% in 120 rounds. The same run on a quiet machine earlier the
same morning gave 3.36 (best) and 3.45 (median); eight other runs of the `-O2` build over
two days, shorter ones included, gave 3.31 to 3.39.

Two things to keep in mind when reading single rows of the table at the end:

- **Small functions measure the calling convention.** A function that returns at once costs
  0.87 ns natively and 1.23 ns lifted in this harness (`bits_set`: 1.42x). 100 of the 194
  functions take under 1 ns natively. Their geometric mean is 3.3x and that of the 20 that
  take over 5 ns is 3.5x, so the headline is not an artefact of tiny functions.
- **The two sides are different programs, not only different encodings.** The x86 side is
  what clang makes for baseline x86-64 and the Microsoft ABI; the native side is what clang
  makes for arm64 and the Itanium ABI. `popcount32` is one `cnt` instruction natively and a
  loop on x86 (6.4x). `cx_pmf_virtual` folds to four instructions natively and keeps two
  calls through a member-pointer thunk on x86 (26x). It cuts the other way too: `big_frame`
  runs at 0.39x under `restrict` (below), because the x86 build broke the loop's dependency
  chain into four independent multiplies and the arm64 build did not. For the game only the
  first direction exists: there is no native build to lose to.

## Where the gap comes from

### What the generated arm64 looks like

**`cx_pmf_virtual` (26x, the worst).** Native: 4 instructions (`sub`, `lsl`, `add`, `ret`).
x86: 22 instructions with two calls to a thunk that tail-jumps through a vtable. Lifted arm64
for the outer function alone: 151 instructions.

- Each `push` is four instructions (load rsp from the `Ctx`, subtract, store rsp, store the
  value), and rsp is loaded again for the next one, because the store to the guest stack
  might have changed the `Ctx`.
- `sub rsp, 0x30`, `inc esi` and `add rsp, 0x30` each compute and store all six flags: about
  20 instructions each, five of them for the parity flag alone. Nothing in the function
  reads a flag.
- Each call materialises the guest return address (load the module base, add, store), and
  the thunk's `jmp rax` goes through `rt_dispatch`: find the module, binary search, call.

**`ab_rec_stack` (14x, the worst call-heavy function).** Native: 38 instructions. x86: 47.
Lifted: 202, of which 110 are loads and stores (30 `ldr`, 29 `str`, 23 `strb`, 9 `stur`,
9 `stp`, 10 `ldp`). The 23 byte stores are flags; one flag (ZF, for one `je`) is ever read.
The twelve pushes and pops are four instructions each. Declaring the `Ctx` pointer `restrict`
does not change this function at all (still 202 instructions): the pointer is passed to the
recursive call, so the compiler must still assume the callee and the guest stack can reach it.

In a function with no guest memory access and no call the compiler already does the right
thing by itself. `popcount32`'s lifted loop is five instructions with every register in a
host register and no flag stored inside the loop; the flags are written once before the loop
and once after it.

### Variants of the lifted build

`make bench-gap` builds copies of the lifter's C with one thing changed, runs the full
comparison on each (every variant gives the same 280 identical, 3 mismatching), and times it.
Geometric mean of lifted/native per kind (`bench/results/2026-10-09/gap/`, 194 functions,
performance cores, best rounds; the last column is by median rounds):

| Variant | arith | mem | call | switch | indirect | atomic | all | all (median) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| the lifter's C, `-O2` | 2.74 | 3.43 | 4.00 | 2.08 | 7.63 | 3.15 | **3.33** | 3.41 |
| `-O1` | 3.11 | 4.20 | 4.99 | 2.50 | 8.30 | 3.11 | 3.86 | 3.94 |
| `-O3` | 2.76 | 3.46 | 4.14 | 2.08 | 7.73 | 3.08 | 3.36 | 3.46 |
| `-O2`, one translation unit per function | 2.76 | 3.48 | 4.41 | 2.10 | 7.75 | 3.12 | 3.41 | 3.48 |
| `-O2`, `Ctx *restrict ctx` | 2.65 | 2.29 | 3.01 | 2.06 | 7.14 | 3.25 | **2.87** | 2.95 |
| the same, and flags dead at `ret` and `call` | 2.28 | 2.00 | 2.52 | 1.67 | 6.47 | 3.04 | **2.48** | 2.55 |
| the same at `-O3` | 2.27 | 2.00 | 2.52 | 1.64 | 6.42 | 3.03 | 2.47 | 2.56 |
| `-O2`, direct-mapped dispatch table | 2.76 | 3.46 | 4.12 | 2.07 | 4.84 | 3.12 | 3.23 | 3.33 |
| `restrict`, dead flags and direct-mapped dispatch | 2.28 | 2.02 | 2.51 | 1.64 | 4.03 | 2.95 | **2.37** | 2.48 |

The same variants measured the evening before, with an earlier driver and input generator
(`bench/results/2026-10-08-gap/`), agree within 0.06 in the last column but one.

### Registers and flags in memory the compiler cannot reason about: 3.33 to 2.87

The lifted C reads and writes `ctx->r[i]` and `ctx->zf` directly, and guest memory through
pointers made from integers. The compiler has to assume those pointers can point at the
`Ctx`. So before every guest load, everything pending for the `Ctx` (changed registers, all
six flags) must be stored, and after every guest store every register must be loaded again.
`restrict` on the `Ctx` parameter says they cannot overlap, which is true by construction
(the guest never learns the address of its register file). That one keyword takes memory
loops from 3.43 to 2.29 and the whole corpus from 3.33 to 2.87. It does nothing for functions
that pass the `Ctx` to a call, as `ab_rec_stack` shows; keeping registers in locals (below)
does not have that limit.

**Correction: on the game `restrict` alone did almost nothing, and the reason is a
limit of the compiler.** The corpus functions are small. A function of the game has hundreds or
thousands of uses of `ctx`, and LLVM's capture tracking gives up after 100 uses of a pointer
(`-capture-tracking-max-uses-to-explore`, default 100) and answers "may escape"; alias analysis then
treats every pointer made from an integer as possibly the `Ctx`, `restrict` or not. And where the
limit is not reached, the first call that passes `ctx` on is an escape, as said above. Both are lifted
without touching the lifter: the calls of the linked build go through a function type whose parameter
is `noescape` and the rt_ functions lifted code calls declare theirs so (`harness/runtime.h`, "THE
REGISTER FILE DOES NOT ESCAPE"), and `RT_LIFT_CFLAGS` raises that limit and those of dead-store
elimination and memory dependence (`runtime/rt.mk`). On the hottest translation unit of the game
(`client_0063.c`, 200 functions):

| Build of `client_0063.c` | arm64 instructions | of `client_2e8e80`: instructions, loads, stores, flag bytes stored |
|---|---:|---|
| stage 1 (`-O2`, `restrict`, flags dead at calls) | 87,347 | 6,429, 1,754, 1,273, 371 |
| + capture limit raised | 84,871 | 6,411, 1,746, 1,268, 371 |
| + `noescape` on callees (limit not raised) | 86,027 | 6,429, 1,754, 1,273, 371 |
| + both | 63,574 | 4,832, 920, 892, 289 |
| + dead-store and memory-dependence limits | **60,604** | **4,579, 699, 885, 206** |

The game binary went from 198 MB to 134 MB, and the heaviest map at the lowest settings and 960 by 540
(the processor is the limit there) from 104 to 120 frames a second. The flag bytes still stored are
live on the paths to a trap (`rt_trap(ctx)` may read them); zeroing the flags before each trap removes
few of them and adds more code than it saves. Registers in locals (below) would still gain what is left
around calls.

### Flag computation nobody reads

With `restrict` the compiler removes a flag write when a later one overwrites it and nothing
in between can read it. What remains is the last flag-setting instruction before each `ret`
and `call`, whose flags are dead by the calling convention but which the lifter's contract
still stores. Overwriting the six flags with a constant just before every `ret`, `call` and
tail call lets the compiler delete those too: 2.87 to 2.48.

By hand, in copies of three functions (`bench/hand/deadflags/three.c`: every flag write with
no reader removed, nothing else touched; verified by the same comparison):

| Function | x86 instructions that set flags / that have a reader | As lifted | Dead flags removed by hand | `restrict` | Both |
|---|---|---:|---:|---:|---:|
| `str_cmp` (memory loop) | 8 / 4 | 7.52 | 2.80 | 1.31 | 1.29 |
| `list_sum` (pointer chase) | 6 / 3 | 6.65 | 2.24 | 1.57 | 1.49 |
| `fib` (recursion) | 11 / 2 | 4.7 to 5.9 | 5.13 | 3.50 | 3.22 |

For the two loops, flags nobody reads are most of the gap: 7.5x becomes 2.8x and 6.7x becomes
2.2x from removing them alone. For `fib` removing them changes nothing that can be measured;
its time is in the calls. (`fib`'s lifted time is 4.7 times native in this build and 5.9
times in the full run's build of the same C; see "What is uncertain".)

### Registers living in memory

`bench/hand/locals/three.c` has the same three functions the way a lifter that keeps
registers in locals would emit them, by one fixed rule: load every register the function
touches at entry, store the ones it writes before each return, store and reload around a
call, compute a flag only at the branch that reads it.

| Function | As lifted | `restrict` and flags dead at `ret`/`call` | Registers in locals |
|---|---:|---:|---:|
| `str_cmp` | 7.52 | 1.30 | **1.24** |
| `list_sum` | 6.65 | 1.35 | **1.08** |
| `fib` | 4.7 to 5.9 | 3.46 | **2.84** |

So for loop code the whole gap is `Ctx` traffic and dead flags, and it closes. For call-heavy
code about half of it stays.

### Call overhead

- **Direct calls.** A lifted call stores the guest return address, calls the host function,
  and the callee's `ret` adjusts rsp in memory; around it the guest's own pushes and pops of
  callee-saved registers go through the `Ctx`. `fib` with registers in locals is still 2.8x.
- **Inlining between lifted functions** is worth little: one translation unit per function
  moves call-heavy code from 4.00 to 4.41 and nothing else.
- **Indirect calls.** `rt_dispatch` here finds the module and then binary-searches its
  table (about 100 entries per module). Replacing the search with an array indexed by RVA
  takes indirect-call functions from 7.63 to 4.84, and from 6.47 to 4.03 on top of `restrict`
  and dead flags. The game's tables have tens of thousands of entries per module, so a binary
  search there costs more than it does here.

### Optimisation level

`-O1` is 16% slower than `-O2` (3.86 against 3.33). `-O3` is no faster than `-O2` (3.36
against 3.33, 2.47 against 2.48 once `restrict` and dead flags are in).

### Is 2x reachable, and what would it take

For code that spends its time in loops, yes, and with changes that are already on the
project's list:

1. Registers in locals (or, as a stopgap that costs one keyword, `restrict` on the `Ctx`
   parameter). Measured: 3.33 to 2.87 for the corpus, memory loops to 2.3.
2. Flag liveness inside a function, with flags dead at `ret` and `call`. Measured with 1:
   2.48 for the corpus, memory loops 2.0, switches 1.7, arithmetic 2.3. On the three
   hand-converted functions: 1.2, 1.1 and 2.8.
3. A dispatch table indexed by RVA instead of searched. Measured with 1 and 2: 2.37 for the
   corpus, indirect-call code 4.0.

After all three, arithmetic is at 2.3, memory loops at 2.0, switches at 1.6, call-heavy code
at 2.5, atomics at 3.0 and indirect-call code at 4.0. Size matters more than kind by then.
Functions that take at least 1 ns natively: memory loops 1.7 (19 functions), switches 1.6
(11), arithmetic 2.05 (17; `collatz` 1.01, `isqrt` 1.07, `gcd` 1.49), call-heavy 2.5 (30),
indirect calls 3.2 (14). Functions under 1 ns natively stay at 2.3 to 3.2 whatever their
kind (62 of the 79 arithmetic ones, 10 of the 13 atomics): there the work is the call itself,
five stores, a call and a load against a native call that passes everything in registers.

So what is left after the three changes is the calling convention. Getting call-dense code
under 2x needs a cheaper call sequence than "everything through the `Ctx`": the guest stack
pointer and the argument registers in host registers across a lifted call, and no store of
a return address that nothing reads. That is not measured here.

What the game's mix of these kinds is, nobody knows yet; the corpus weights them by how many
test functions there are, not by where a frame's time goes.

## x86 memory-ordering mode

Measured, with the existing provisioning and nothing else. `make bench-tso` (`bench/tso.sh`)
copies `experiments/tso/app` unchanged to `bench/build/tso/` and builds it with `xcodebuild`
and two build settings on the command line: the probe's own `main` is renamed by the
preprocessor, and the benchmark driver's objects are added to the link. The signed executable
is then the benchmark driver, with the same bundle identifier, entitlements file, team and
automatic signing as the probe. The build succeeded without `-allowProvisioningUpdates` or
`-allowProvisioningDeviceRegistration`, nothing in the developer account or in
`experiments/` was touched, and Xcode's window was not opened. The app carries
`com.apple.developer.cross-architecture-support`, and `thread_set_x86_64_compat(1)` returned 0
on the benchmark's thread.

Four runs, off, on, off, on, 280 functions each, all timings on performance cores
(`bench/results/2026-10-09/tso/`):

| | Run pair 1 (on / off) | Run pair 2 (on / off) |
|---|---:|---:|
| native time, 194 functions | 0.974 | 0.940 |
| lifted time, 194 functions | 0.983 | 0.946 |
| lifted/native ratio | 1.009 | 1.006 |

The machine got about 5% faster during the last run (both sides, so it is the processor's
frequency, not the mode), which is more than the effect being measured. The ratio of lifted
to native cancels that: **3.39 with the mode off, 3.40 with it on. On single-threaded code
the mode costs about 1% on average, which is inside the noise of this measurement.**

| Kind | Lifted / native, off | on |
|---|---:|---:|
| arithmetic | 2.78 | 2.80 |
| memory loops | 3.45 | 3.47 |
| call-heavy | 4.14 | 4.07 |
| switch | 2.13 | 2.09 |
| indirect calls | 7.87 | 7.83 |
| atomics | 3.16 | 3.37 |
| `intrare.c` probes | 3.02 | 3.16 |

It is not free everywhere. Nine of the 280 functions are slower with the mode on in both
pairs, by a factor of 1.2 to 3.1 on the lifted side, while their native side does not move:

| Function | Lifted ns, off | on | Factor |
|---|---:|---:|---:|
| `ir_imul16_imm` | 1.80 | 5.3 to 5.6 | 3.0 |
| `atomic_flag_set` | 2.1 | 6.0 | 2.9 |
| `ir_bsr16` | 2.2 | 5.8 to 6.3 | 2.8 |
| `ir_xadd8` | 3.3 | 6.5 to 7.1 | 2.1 |
| `ir_lodsb` | 3.2 | 6.5 to 6.9 | 2.1 |
| `ir_cmpxchg16` | 5.0 | 8.5 to 9.5 | 1.8 |
| `cx_v2_compound` | 7.9 | 12.0 to 13.0 | 1.6 |
| `cx_pdm_read` | 5.9 | 7.7 to 8.0 | 1.3 |
| `ab_fp_all` | 18.8 | 23.2 to 24.4 | 1.3 |

The first six write 8 or 16 bits of a register. In the lifted code that is a narrow store
into the register's 8-byte slot in the `Ctx` (`strh w9, [x0]` in `ir_imul16_imm`), and the
next read of the whole register is a 64-bit load of the same slot, which the processor
cannot satisfy from the pending store. With x86 ordering on, that sequence costs about 4 ns
more. In these six the wide read is the harness reading rax; in real code it is the next
instruction that uses the full register. The other three were not examined. Registers in
locals remove the pattern, because a partial register write is then arithmetic on a host
register and no store at all.

What this does not measure: more than one thread. The litmus loop in `experiments/tso`, two
threads fighting over two cache lines, lost a factor of 2.4 to 3.5 with the mode on; this
benchmark is one thread on private data, which is the other end of the range.

## What is uncertain

- **The headline is a statement about this corpus**: 194 small functions compiled by clang.
  The game is Microsoft-compiled, larger, and float-heavy; float code is not lifted yet and
  is not in these numbers at all.
- **Absolute times depend on what else the machine is doing; ratios much less.** An empty
  native call takes 0.78 ns when the machine is idle and 0.88 ns under load (the performance
  cores clock down when several are busy). The headline was 3.36 on the idle machine and
  3.31 to 3.39 under load.
- **Single rows can be off by a quarter.** `fib` lifted is 4.7 times native in one build of
  the same lifted C and 5.9 times in another, minutes apart, and was 4.6 in the run on the
  idle machine; its native side does not move. Not explained; where the code and the `Ctx`
  land in memory is the suspicion. Across the runs the per-kind means moved by up to 0.15,
  and the mean of the 17 indirect-call functions by 0.45 (7.6 to 8.1).
- **Small lifted functions have two speeds.** For 44 of the 194 the lifted side's median
  round is more than 15% above its best round (`clamp`: best 2.6 ns, median 4.5 ns), with the
  fast rounds coming rarely and in runs; the native side does this far less. The suspicion,
  not established, is the processor's store-to-load forwarding across the call: the harness
  stores four argument registers and rsp into the `Ctx` and the lifted function reads them
  back at once. Moving the `Ctx` to other page offsets relative to the guest stack
  (`--ctx-offset`) did not change it. Both the best-round and the median-round means are
  given for that reason; they differ by 2% overall.
- **Performance cores are not always available.** macOS confines a process tree running at
  background priority to the efficiency cores, and that happened to this session for a while
  (everything under the desktop application at priority 4, presumably App Nap). The driver
  marks such timings `E` and says so; none of the numbers in this document are from such a
  run. Rounds that touched an efficiency core are discarded when a function has at least
  three that did not.
- **The `intrare.c` reference is hand-written.** It agrees with the lifted code on all 86
  functions that lift, and was checked against hand-computed values, but it is a second
  implementation, not the same source, and its timings say less than the others.
- **Inputs are random, not directed.** The mutation check is the evidence that they reach
  what matters, and it covers twelve kinds of mistake. A wrong translation of a path that
  random inputs do not take would pass.
- **The memory-ordering result is single-threaded** and its cause for the slow functions was
  read off the code of one of them, not measured separately.
- **Functions with float arguments or results are not compared.** 30 exported functions of
  `abi.c` and `cpp2.cpp` have no row; the table format would need a float kind.

## Every function

`bench/results/2026-10-09/O2-full.json`, from `make bench`. Ratio is best round over best
round; spread is median round over best round, native / lifted.

<!-- table:begin -->
| File | Function | Kind | Native ns | Lifted ns | Ratio | Ratio of medians | Spread n / l |
|---|---|---|---:|---:|---:|---:|---:|
| abi | `ab_args6` | call | 1.53 | 7.21 | 4.70 | 4.17 | 25% / 11% |
| abi | `ab_args6_pass` | call | 1.53 | 7.80 | 5.09 | 5.50 | 12% / 21% |
| abi | `ab_args8` | call | 1.54 | 8.18 | 5.33 | 4.31 | 30% / 5% |
| abi | `ab_args_narrow` | arith | 0.88 | 4.26 | 4.83 | 4.89 | 3% / 5% |
| abi | `ab_s1_arg` | arith | 0.88 | 2.27 | 2.57 | 2.77 | 11% / 20% |
| abi | `ab_s1_ret` | call | 1.24 | 2.41 | 1.94 | 2.02 | 4% / 8% |
| abi | `ab_s2_arg` | arith | 0.95 | 2.25 | 2.36 | 2.20 | 15% / 8% |
| abi | `ab_s2_ret` | arith | 0.88 | 1.27 | 1.43 | 1.51 | 4% / 10% |
| abi | `ab_s3_arg` | arith | 0.93 | 3.85 | 4.14 | 4.16 | 7% / 8% |
| abi | `ab_s3_ret` | arith | 0.88 | 2.72 | 3.08 | 3.22 | 6% / 11% |
| abi | `ab_s4_arg` | arith | 0.88 | 3.43 | 3.89 | 4.15 | 3% / 10% |
| abi | `ab_s4_ret` | arith | 0.88 | 1.27 | 1.43 | 1.46 | 7% / 9% |
| abi | `ab_s12_arg` | arith | 0.97 | 5.48 | 5.67 | 5.59 | 6% / 4% |
| abi | `ab_s12_ret` | arith | 0.88 | 2.83 | 3.21 | 3.36 | 4% / 9% |
| abi | `ab_s12_swap` | arith | 0.88 | 4.21 | 4.77 | 5.04 | 6% / 12% |
| abi | `ab_s16_arg` | arith | 0.88 | 4.53 | 5.13 | 5.17 | 5% / 6% |
| abi | `ab_s16_ret` | arith | 0.88 | 2.07 | 2.35 | 2.61 | 7% / 19% |
| abi | `ab_s16_add` | arith | | | not-lifted | | |
| abi | `ab_smix` | call | 2.02 | 11.35 | 5.62 | 5.37 | 7% / 2% |
| abi | `ab_va_bits` | call | 2.77 | 8.18 | 2.96 | 2.66 | 18% / 6% |
| abi | `ab_va_fwd` | call | 2.46 | 9.49 | 3.85 | 3.72 | 12% / 8% |
| abi | `ab_va_many` | call | 0.90 | 5.85 | 6.52 | 6.28 | 13% / 9% |
| abi | `ab_va_bits_caller` | call | 2.37 | 11.77 | 4.97 | 5.12 | 4% / 7% |
| abi | `ab_va_fwd_caller` | call | 2.39 | 16.14 | 6.75 | 6.76 | 4% / 4% |
| abi | `ab_va_many_caller` | call | 1.53 | 9.70 | 6.33 | 6.47 | 6% / 8% |
| abi | `ab_fp_table` | virtual | | | not-lifted | | |
| abi | `ab_fp_all` | call | 4.04 | 17.23 | 4.27 | 4.57 | 7% / 15% |
| abi | `ab_fp_sret` | virtual | 2.10 | 13.44 | 6.40 | 6.49 | 8% / 10% |
| abi | `ab_rec_tree` | call | 60.68 | 726.77 | 11.98 | 12.19 | 12% / 14% |
| abi | `ab_rec_stack` | call | 7.79 | 109.00 | 13.98 | 13.51 | 7% / 4% |
| abi | `ab_ping` | call | 3.91 | 3.61 | 0.92 | 0.99 | 8% / 16% |
| abi | `ab_rec_struct` | call | 3.44 | 17.81 | 5.18 | 5.85 | 10% / 24% |
| abi | `ab_tail6` | call | 1.26 | 7.03 | 5.57 | 5.43 | 7% / 5% |
| abi | `ab_tail_sret` | call | 1.24 | 4.74 | 3.82 | 4.01 | 5% / 10% |
| abi | `ab_tail_branch` | call | 1.75 | 2.32 | 1.33 | 1.33 | 14% / 14% |
| abi | `ab_tail_struct` | call | 1.25 | 5.60 | 4.50 | 4.07 | 13% / 2% |
| abi | `ab_sw64_dense` | switch | 1.97 | 3.14 | 1.59 | 1.63 | 3% / 5% |
| abi | `ab_sw_bittest` | switch | 1.86 | 3.30 | 1.77 | 1.92 | 6% / 14% |
| abi | `ab_sw64_sparse` | switch | 2.51 | 4.34 | 1.73 | 1.68 | 10% / 7% |
| abi | `ab_sw_range` | switch | 0.88 | 3.51 | 3.97 | 4.09 | 5% / 9% |
| abi | `ab_sw_chain` | switch | 2.16 | 4.16 | 1.93 | 2.26 | 10% / 29% |
| abi | `ab_goto_tab` | switch | | | not-lifted | | |
| abi | `ab_goto_loop` | switch | | | not-lifted | | |
| abi | `ab_u128_add` | arith | | | not-lifted | | |
| abi | `ab_u128_mul` | arith | | | not-lifted | | |
| abi | `ab_u128_shl` | arith | | | not-lifted | | |
| abi | `ab_u128_shr` | arith | | | not-lifted | | |
| abi | `ab_u128_cmp` | arith | | | not-lifted | | |
| abi | `ab_u128_sub` | arith | | | not-lifted | | |
| abi | `ab_u128_addc` | arith | 0.88 | 5.17 | 5.86 | 6.89 | 10% / 29% |
| abi | `ab_u128_accum` | arith | 2.56 | 8.51 | 3.33 | 3.28 | 19% / 17% |
| abi | `ab_press_int` | call | 4.38 | 37.40 | 8.53 | 7.10 | 39% / 16% |
| abi | `ab_copy32` | mem | 0.88 | 2.06 | 2.33 | 2.55 | 5% / 15% |
| abi | `ab_s16_copy` | mem | 0.88 | 1.46 | 1.66 | 1.58 | 7% / 2% |
| abi | `ab_sel_ne` | arith | 0.88 | 2.35 | 2.65 | 2.21 | 39% / 16% |
| abi | `ab_sel_ne_mem` | arith | 1.29 | 2.68 | 2.08 | 2.26 | 16% / 26% |
| abi | `ab_eq_reg` | arith | 0.88 | 2.47 | 2.79 | 3.36 | 8% / 30% |
| abi | `ab_eq_mem` | arith | 0.88 | 2.53 | 2.87 | 3.25 | 4% / 18% |
| abi | `ab_flags_pack` | arith | 0.91 | 2.47 | 2.71 | 2.85 | 13% / 18% |
| abi | `ab_lea_mix` | arith | 0.88 | 2.99 | 3.39 | 3.00 | 23% / 9% |
| abi | `ab_lea_idx` | mem | 0.88 | 4.54 | 5.14 | 6.06 | 10% / 30% |
| atomics | `atomic_inc` | atomic | 0.88 | 2.80 | 3.17 | 3.08 | 12% / 8% |
| atomics | `atomic_dec` | atomic | 0.88 | 2.80 | 3.18 | 3.20 | 9% / 10% |
| atomics | `atomic_add64` | atomic | 0.88 | 2.83 | 3.21 | 3.17 | 3% / 2% |
| atomics | `atomic_or` | atomic | 0.88 | 3.98 | 4.51 | 4.83 | 5% / 12% |
| atomics | `atomic_and` | atomic | 0.88 | 4.99 | 5.65 | 5.59 | 8% / 7% |
| atomics | `atomic_swap` | atomic | 0.88 | 1.97 | 2.23 | 2.94 | 4% / 37% |
| atomics | `atomic_swap_ptr` | atomic | 0.88 | 1.97 | 2.23 | 2.59 | 9% / 27% |
| atomics | `atomic_cas` | atomic | 1.68 | 3.99 | 2.38 | 1.81 | 66% / 26% |
| atomics | `atomic_cas64` | atomic | 1.63 | 4.60 | 2.82 | 1.73 | 112% / 30% |
| atomics | `atomic_flag_set` | atomic | 0.88 | 1.97 | 2.23 | 1.92 | 22% / 5% |
| atomics | `spin_try` | atomic | 1.87 | 4.96 | 2.65 | 1.81 | 82% / 25% |
| atomics | `refcount_release` | atomic | 0.88 | 2.95 | 3.34 | 3.32 | 5% / 4% |
| atomics | `atomic_add16` | atomic | 0.88 | 4.47 | 5.06 | 4.97 | 8% / 6% |
| calls | `fib` | call | 220.23 | 1301.70 | 5.91 | 5.71 | 4% / 0% |
| calls | `ackermann` | call | 2.75 | 6.26 | 2.28 | 2.19 | 5% / 1% |
| calls | `call_many` | call | 2.51 | 17.19 | 6.86 | 7.99 | 3% / 20% |
| calls | `dispatch` | virtual | | | not-lifted | | |
| calls | `fold` | virtual | 11.52 | 56.84 | 4.93 | 5.09 | 9% / 13% |
| calls | `tail_caller` | call | 1.66 | 2.30 | 1.39 | 1.50 | 7% / 16% |
| calls | `sum_va` | call | 2.26 | 8.69 | 3.85 | 4.33 | 7% / 21% |
| calls | `call_va` | call | 2.98 | 15.90 | 5.33 | 5.36 | 8% / 9% |
| calls | `big_frame` | mem | 429.43 | 530.73 | 1.24 | 1.23 | 4% / 4% |
| calls | `mutual_even` | call | 2.18 | 4.23 | 1.94 | 2.27 | 8% / 26% |
| calls | `callback_walk` | virtual | 4.44 | 30.00 | 6.75 | 5.93 | 24% / 9% |
| cpp2 | `cx_mi_first_base` | arith | 0.88 | 2.61 | 2.96 | 3.31 | 3% / 16% |
| cpp2 | `cx_mi_second_base` | arith | 1.16 | 1.70 | 1.46 | 1.47 | 0% / 1% |
| cpp2 | `cx_mi_pick` | virtual | 4.16 | 29.12 | 7.01 | 6.86 | 5% / 3% |
| cpp2 | `cx_mi_downcast` | arith | 0.88 | 1.27 | 1.44 | 1.41 | 5% / 3% |
| cpp2 | `cx_mi_crosscast` | arith | 0.88 | 2.25 | 2.55 | 2.76 | 3% / 12% |
| cpp2 | `cx_mi_store` | mem | 0.88 | 2.53 | 2.86 | 2.97 | 6% / 10% |
| cpp2 | `cx_vb_fields` | arith | 0.88 | 2.22 | 2.52 | 2.70 | 2% / 10% |
| cpp2 | `cx_vb_call` | arith | 0.88 | 2.46 | 2.78 | 2.67 | 18% / 13% |
| cpp2 | `cx_vb_pick` | arith | 0.88 | 2.46 | 2.79 | 2.55 | 11% / 1% |
| cpp2 | `cx_vb_write` | arith | 0.88 | 2.78 | 3.15 | 3.22 | 7% / 9% |
| cpp2 | `cx_vb_store` | mem | 0.88 | 2.72 | 3.09 | 3.21 | 3% / 7% |
| cpp2 | `cx_pmf_pick` | virtual | 2.77 | 11.72 | 4.23 | 4.21 | 9% / 9% |
| cpp2 | `cx_pmf_table` | virtual | 2.59 | 12.48 | 4.81 | 4.81 | 7% / 7% |
| cpp2 | `cx_pmf_virtual` | virtual | 0.88 | 22.95 | 26.00 | 30.43 | 4% / 21% |
| cpp2 | `cx_pmf_store` | virtual | 2.54 | 12.74 | 5.03 | 4.91 | 9% / 7% |
| cpp2 | `cx_pmf_derived` | virtual | | | MISMATCH (lifter-bug) | | |
| cpp2 | `cx_pmf_mi_base` | virtual | 0.88 | 11.76 | 13.31 | 13.35 | 12% / 12% |
| cpp2 | `cx_pmf_mi_pick` | virtual | 2.52 | 17.52 | 6.94 | 6.25 | 28% / 16% |
| cpp2 | `cx_pmf_compare` | arith | 0.88 | 2.30 | 2.60 | 2.57 | 22% / 21% |
| cpp2 | `cx_pdm_read` | arith | 0.88 | 5.31 | 6.01 | 5.83 | 7% / 4% |
| cpp2 | `cx_pdm_write` | mem | 0.88 | 4.41 | 4.99 | 5.08 | 7% / 9% |
| cpp2 | `cx_pdm_swap` | mem | 0.90 | 5.50 | 6.08 | 5.58 | 24% / 13% |
| cpp2 | `cx_pdm_table` | arith | 1.16 | 5.95 | 5.12 | 5.36 | 3% / 8% |
| cpp2 | `cx_pdm_base` | arith | 0.88 | 1.26 | 1.42 | 1.52 | 5% / 13% |
| cpp2 | `cx_iface_pick` | virtual | 5.82 | 44.30 | 7.61 | 7.89 | 16% / 20% |
| cpp2 | `cx_iface_all` | arith | 1.40 | 3.63 | 2.59 | 4.58 | 7% / 90% |
| cpp2 | `cx_iface_ref` | virtual | 4.03 | 27.93 | 6.93 | 7.12 | 4% / 7% |
| cpp2 | `cx_iface_arg` | virtual | | | not-lifted | | |
| cpp2 | `cx_iface_store` | mem | 0.88 | 2.97 | 3.36 | 3.52 | 1% / 6% |
| cpp2 | `cx_tpl_sum_u8` | mem | | | not-lifted | | |
| cpp2 | `cx_tpl_sum_i16` | mem | | | not-lifted | | |
| cpp2 | `cx_tpl_sum_u32` | mem | | | not-lifted | | |
| cpp2 | `cx_tpl_sum_u64` | mem | | | not-lifted | | |
| cpp2 | `cx_tpl_dot_u32` | mem | | | not-lifted | | |
| cpp2 | `cx_tpl_dot_u8` | mem | | | not-lifted | | |
| cpp2 | `cx_tpl_clamp_i32` | arith | 0.87 | 2.48 | 2.84 | 4.48 | 3% / 63% |
| cpp2 | `cx_tpl_clamp_i64` | arith | 0.87 | 2.46 | 2.83 | 3.99 | 1% / 42% |
| cpp2 | `cx_tpl_fold_u32` | arith | 1.31 | 4.12 | 3.14 | 3.66 | 3% / 20% |
| cpp2 | `cx_tpl_fold_u64` | arith | 1.14 | 2.62 | 2.29 | 2.24 | 4% / 2% |
| cpp2 | `cx_tpl_apply_u32` | call | 1.78 | 2.35 | 1.33 | 1.40 | 2% / 8% |
| cpp2 | `cx_lam_capture_tpl` | arith | 0.87 | 2.38 | 2.73 | 2.85 | 3% / 8% |
| cpp2 | `cx_lam_capture_ref` | arith | 0.87 | 2.56 | 2.94 | 3.66 | 4% / 29% |
| cpp2 | `cx_lam_fnptr` | arith | 1.22 | 2.19 | 1.79 | 1.88 | 4% / 10% |
| cpp2 | `cx_lam_ctx` | call | 1.52 | 5.48 | 3.61 | 3.92 | 2% / 11% |
| cpp2 | `cx_lam_out` | mem | 0.88 | 3.62 | 4.10 | 4.18 | 3% / 5% |
| cpp2 | `cx_lam_nested` | arith | 0.88 | 1.81 | 2.06 | 2.06 | 3% / 3% |
| cpp2 | `cx_lam_mutable` | arith | 0.87 | 1.24 | 1.43 | 1.46 | 3% / 5% |
| cpp2 | `cx_v2_arith` | arith | 1.01 | 2.93 | 2.89 | 2.99 | 1% / 5% |
| cpp2 | `cx_v2_dot` | arith | 0.90 | 2.95 | 3.26 | 3.22 | 7% / 5% |
| cpp2 | `cx_v2_scalar_left` | arith | 0.87 | 2.57 | 2.95 | 3.03 | 6% / 8% |
| cpp2 | `cx_v2_compound` | arith | 1.15 | 6.85 | 5.97 | 6.20 | 3% / 7% |
| cpp2 | `cx_v2_store` | mem | 0.87 | 2.81 | 3.23 | 3.24 | 2% / 3% |
| cpp2 | `cx_v2_array_sum` | mem | 2.01 | 7.36 | 3.65 | 3.56 | 5% / 2% |
| cpp2 | `cx_v4_add` | mem | | | not-lifted | | |
| cpp2 | `cx_v4_bias` | mem | 1.15 | 5.89 | 5.14 | 5.17 | 2% / 3% |
| cpp2 | `cx_v4_bias_xor` | mem | 0.87 | 3.23 | 3.71 | 3.74 | 1% / 2% |
| cpp2 | `cx_place_acc` | mem | 1.35 | 3.72 | 2.75 | 2.70 | 3% / 1% |
| cpp2 | `cx_place_pair` | mem | 0.87 | 3.39 | 3.89 | 4.01 | 2% / 5% |
| cpp2 | `cx_place_mi` | mem | 0.87 | 3.54 | 4.05 | 4.28 | 6% / 12% |
| cpp2 | `cx_place_vb` | mem | 0.87 | 4.31 | 4.95 | 4.98 | 4% / 5% |
| cpp2 | `cx_place_units` | virtual | 3.92 | 24.48 | 6.25 | 6.27 | 5% / 5% |
| cpp2 | `cx_place_loop` | mem | 2.23 | 4.84 | 2.17 | 2.15 | 5% / 4% |
| cpp2 | `cx_cov_next` | arith | 0.87 | 2.24 | 2.57 | 2.69 | 4% / 9% |
| cpp2 | `cx_cov_direct` | arith | 0.87 | 2.26 | 2.59 | 2.71 | 3% / 8% |
| cpp2 | `cx_ctor_virtual` | arith | 0.88 | 1.24 | 1.41 | 1.43 | 2% / 4% |
| cpp2 | `cx_copy_ctor` | call | 2.06 | 17.36 | 8.45 | 8.41 | 4% / 4% |
| cpp2 | `cx_conv_op` | arith | 0.87 | 2.09 | 2.40 | 2.42 | 5% / 6% |
| cpp2 | `cx_builder` | arith | 0.87 | 3.17 | 3.64 | 4.07 | 3% / 15% |
| cpp2 | `cx_static_member` | arith | 0.87 | 2.38 | 2.73 | 2.70 | 3% / 2% |
| int_arith | `mix32` | arith | 0.87 | 2.49 | 2.85 | 2.89 | 3% / 4% |
| int_arith | `mix64` | arith | 0.87 | 2.98 | 3.43 | 4.56 | 3% / 37% |
| int_arith | `sdivmod` | arith | 1.23 | 5.06 | 4.10 | 4.14 | 4% / 4% |
| int_arith | `udivmod64` | arith | 1.23 | 2.02 | 1.64 | 1.64 | 4% / 4% |
| int_arith | `sdiv64` | arith | 1.22 | 6.21 | 5.07 | 5.05 | 4% / 3% |
| int_arith | `mulhi` | arith | 0.87 | 2.79 | 3.20 | 3.47 | 3% / 11% |
| int_arith | `smulhi` | arith | 0.87 | 2.81 | 3.23 | 3.51 | 4% / 13% |
| int_arith | `narrow8` | arith | 0.87 | 2.60 | 2.98 | 2.98 | 3% / 3% |
| int_arith | `narrow16` | arith | 0.87 | 2.31 | 2.65 | 2.67 | 3% / 4% |
| int_arith | `widen` | arith | 0.87 | 3.56 | 4.09 | 4.13 | 3% / 4% |
| int_arith | `shifts` | arith | 0.87 | 2.80 | 3.22 | 3.51 | 4% / 14% |
| int_arith | `shifts64` | arith | 0.87 | 2.88 | 3.31 | 3.35 | 4% / 6% |
| int_arith | `popcount32` | arith | 0.88 | 5.64 | 6.43 | 6.45 | 3% / 3% |
| int_arith | `clz32` | arith | 3.71 | 4.37 | 1.18 | 1.22 | 24% / 28% |
| int_arith | `bswap32` | arith | 0.88 | 1.26 | 1.43 | 1.47 | 5% / 8% |
| int_arith | `clamp` | arith | 0.88 | 2.55 | 2.89 | 3.74 | 34% / 74% |
| int_arith | `sign` | arith | 0.89 | 1.95 | 2.18 | 2.25 | 15% / 19% |
| int_arith | `select` | arith | 0.91 | 2.88 | 3.18 | 3.61 | 12% / 27% |
| int_arith | `carry_chain` | arith | 0.87 | 3.25 | 3.73 | 4.25 | 12% / 28% |
| int_arith | `borrow_chain` | arith | 0.87 | 3.14 | 3.60 | 4.40 | 9% / 33% |
| int_arith | `ptr_diff` | arith | 0.87 | 2.86 | 3.29 | 3.25 | 7% / 5% |
| int_arith | `collatz` | arith | 138.56 | 140.49 | 1.01 | 1.02 | 6% / 7% |
| int_arith | `gcd` | arith | 8.18 | 13.71 | 1.68 | 1.63 | 9% / 6% |
| int_arith | `isqrt` | arith | 9.16 | 11.72 | 1.28 | 1.28 | 9% / 9% |
| intrare | `ir_rcl1` | probe | | | not-lifted | | |
| intrare | `ir_rcl8_imm` | probe | | | not-lifted | | |
| intrare | `ir_rcl32_cl` | probe | | | not-lifted | | |
| intrare | `ir_rcr16_1` | probe | | | not-lifted | | |
| intrare | `ir_rcr64_cl` | probe | | | not-lifted | | |
| intrare | `ir_rcr32_mem` | probe | | | not-lifted | | |
| intrare | `ir_lahf` | probe | | | not-lifted | | |
| intrare | `ir_sahf` | probe | | | not-lifted | | |
| intrare | `ir_pushfq_popfq` | probe | | | not-lifted | | |
| intrare | `ir_popfq_reload` | probe | | | not-lifted | | |
| intrare | `ir_xlatb` | probe | | | not-lifted | | |
| intrare | `ir_xlatb_mem` | probe | | | not-lifted | | |
| intrare | `ir_movbe32` | probe | | | not-lifted | | |
| intrare | `ir_movbe32_store` | probe | | | not-lifted | | |
| intrare | `ir_movbe64` | probe | | | not-lifted | | |
| intrare | `ir_movbe16` | probe | | | not-lifted | | |
| intrare | `ir_andn32` | probe | | | not-lifted | | |
| intrare | `ir_andn64_mem` | probe | | | not-lifted | | |
| intrare | `ir_shld32_imm` | probe | 1.18 | 2.04 | 1.73 | 1.70 | 8% / 6% |
| intrare | `ir_shld64_cl` | probe | 0.87 | 2.90 | 3.32 | 3.31 | 9% / 9% |
| intrare | `ir_shrd16_imm` | probe | 0.89 | 2.14 | 2.41 | 2.43 | 7% / 8% |
| intrare | `ir_shrd32_cl` | probe | 0.88 | 2.89 | 3.30 | 3.30 | 4% / 4% |
| intrare | `ir_shld32_mem` | probe | 0.88 | 3.02 | 3.42 | 3.55 | 2% / 6% |
| intrare | `ir_shrd64_mem` | probe | 0.87 | 2.20 | 2.52 | 2.53 | 1% / 1% |
| intrare | `ir_bsf32` | probe | 0.87 | 1.73 | 1.99 | 2.00 | 1% / 1% |
| intrare | `ir_bsr64` | probe | 0.87 | 1.72 | 1.98 | 1.99 | 0% / 1% |
| intrare | `ir_bsf_mem` | probe | 1.24 | 2.02 | 1.63 | 1.63 | 1% / 1% |
| intrare | `ir_bsr16` | probe | 0.87 | 1.90 | 2.18 | 2.21 | 0% / 1% |
| intrare | `ir_bt32_reg` | probe | 0.87 | 1.43 | 1.64 | 1.62 | 1% / 0% |
| intrare | `ir_bt_mem_far` | probe | 0.87 | 2.35 | 2.70 | 5.09 | 0% / 90% |
| intrare | `ir_bts_reg` | probe | 0.87 | 2.01 | 2.30 | 2.32 | 1% / 2% |
| intrare | `ir_btr64_reg` | probe | 0.87 | 2.18 | 2.50 | 2.88 | 1% / 16% |
| intrare | `ir_btc_imm` | probe | 0.90 | 1.32 | 1.47 | 1.49 | 0% / 1% |
| intrare | `ir_bts_mem` | probe | 0.87 | 2.59 | 2.98 | 4.62 | 0% / 55% |
| intrare | `ir_btr_mem` | probe | 0.87 | 2.76 | 3.17 | 5.12 | 0% / 62% |
| intrare | `ir_btc_mem` | probe | 0.90 | 2.70 | 3.01 | 4.37 | 0% / 45% |
| intrare | `ir_bts_mem_imm` | probe | 0.90 | 1.56 | 1.74 | 1.74 | 0% / 0% |
| intrare | `ir_lock_add` | probe | 0.88 | 2.49 | 2.83 | 2.77 | 2% / 0% |
| intrare | `ir_lock_sub` | probe | 0.87 | 2.49 | 2.86 | 2.77 | 3% / 0% |
| intrare | `ir_lock_and` | probe | 0.87 | 2.49 | 2.86 | 2.86 | 0% / 0% |
| intrare | `ir_lock_or` | probe | 0.87 | 2.49 | 2.86 | 2.77 | 3% / 0% |
| intrare | `ir_lock_xor` | probe | 0.87 | 2.49 | 2.86 | 2.86 | 0% / 0% |
| intrare | `ir_lock_inc` | probe | 0.87 | 2.35 | 2.70 | 2.70 | 0% / 0% |
| intrare | `ir_lock_dec` | probe | 0.88 | 2.35 | 2.67 | 2.63 | 2% / 1% |
| intrare | `ir_lock_not` | probe | 0.87 | 1.66 | 1.91 | 1.91 | 0% / 0% |
| intrare | `ir_lock_addb` | probe | 0.87 | 2.49 | 2.86 | 2.86 | 0% / 0% |
| intrare | `ir_lock_bts` | probe | 0.87 | 3.75 | 4.31 | 4.19 | 3% / 0% |
| intrare | `ir_lock_btr` | probe | 0.87 | 2.85 | 3.28 | 4.25 | 1% / 32% |
| intrare | `ir_lock_btc` | probe | 0.87 | 3.72 | 4.27 | 4.31 | 0% / 1% |
| intrare | `ir_xadd32` | probe | 0.87 | 3.43 | 3.94 | 4.14 | 0% / 5% |
| intrare | `ir_xadd8` | probe | 0.87 | 2.87 | 3.30 | 3.32 | 0% / 1% |
| intrare | `ir_cmpxchg64` | probe | 1.63 | 3.86 | 2.37 | 2.42 | 0% / 2% |
| intrare | `ir_cmpxchg32_flag` | probe | 1.63 | 3.91 | 2.40 | 2.40 | 0% / 0% |
| intrare | `ir_cmpxchg16` | probe | 1.66 | 3.94 | 2.38 | 2.38 | 0% / 0% |
| intrare | `ir_mul32_wide` | probe | 0.87 | 2.13 | 2.44 | 2.44 | 0% / 0% |
| intrare | `ir_imul64_1op` | probe | 0.90 | 2.90 | 3.24 | 3.46 | 0% / 7% |
| intrare | `ir_div32` | probe | 1.11 | 2.69 | 2.42 | 2.45 | 3% / 4% |
| intrare | `ir_idiv32` | probe | 0.87 | 10.18 | 11.68 | 11.83 | 3% / 4% |
| intrare | `ir_div16` | probe | 1.20 | 8.58 | 7.15 | 7.24 | 3% / 4% |
| intrare | `ir_idiv8` | probe | 0.87 | 2.86 | 3.29 | 3.30 | 3% / 4% |
| intrare | `ir_imul3` | probe | 0.87 | 1.58 | 1.81 | 1.81 | 3% / 3% |
| intrare | `ir_imul_mem` | probe | 0.87 | 1.75 | 2.01 | 2.04 | 2% / 4% |
| intrare | `ir_imul16_imm` | probe | 0.87 | 1.56 | 1.79 | 1.81 | 1% / 2% |
| intrare | `ir_imul64_mem` | probe | 0.87 | 3.45 | 3.96 | 4.86 | 2% / 26% |
| intrare | `ir_cbw` | probe | 0.87 | 1.25 | 1.43 | 1.45 | 2% / 3% |
| intrare | `ir_cwde` | probe | 0.87 | 1.25 | 1.44 | 1.48 | 4% / 7% |
| intrare | `ir_cdqe` | probe | 0.87 | 1.23 | 1.42 | 1.43 | 3% / 4% |
| intrare | `ir_cwd` | probe | 0.87 | 2.23 | 2.56 | 2.97 | 3% / 20% |
| intrare | `ir_cdq` | probe | 0.87 | 1.32 | 1.51 | 1.51 | 2% / 2% |
| intrare | `ir_cqo` | probe | 0.87 | 1.29 | 1.49 | 1.49 | 2% / 3% |
| intrare | `ir_loop` | probe | | | not-lifted | | |
| intrare | `ir_loope` | probe | | | not-lifted | | |
| intrare | `ir_loopne` | probe | | | not-lifted | | |
| intrare | `ir_jrcxz` | probe | 0.87 | 1.74 | 1.99 | 2.00 | 2% / 3% |
| intrare | `ir_leave_frame` | probe | 0.87 | 5.49 | 6.30 | 6.94 | 4% / 14% |
| intrare | `ir_movsb` | probe | 3.43 | 54.06 | 15.77 | 15.24 | 6% / 2% |
| intrare | `ir_movsw` | probe | 3.29 | 21.26 | 6.46 | 6.33 | 9% / 7% |
| intrare | `ir_movsd` | probe | 3.28 | 21.16 | 6.44 | 6.34 | 10% / 8% |
| intrare | `ir_movsq` | probe | 3.70 | 12.23 | 3.31 | 3.30 | 4% / 4% |
| intrare | `ir_movsb_back` | probe | 3.45 | 24.26 | 7.02 | 6.94 | 4% / 3% |
| intrare | `ir_movsq_once` | probe | 0.87 | 3.03 | 3.47 | 3.70 | 2% / 8% |
| intrare | `ir_stosb` | probe | 3.58 | 31.10 | 8.69 | 8.46 | 6% / 3% |
| intrare | `ir_stosw` | probe | 3.29 | 15.73 | 4.78 | 4.64 | 4% / 1% |
| intrare | `ir_stosd` | probe | 3.18 | 19.86 | 6.24 | 6.09 | 5% / 3% |
| intrare | `ir_stosq` | probe | 2.76 | 13.15 | 4.77 | 4.72 | 3% / 2% |
| intrare | `ir_stosb_back` | probe | 3.35 | 14.90 | 4.45 | 4.33 | 4% / 2% |
| intrare | `ir_cmpsb` | probe | 7.81 | 28.87 | 3.70 | 3.70 | 2% / 2% |
| intrare | `ir_cmpsb_ne` | probe | 2.44 | 7.60 | 3.12 | 3.05 | 7% / 4% |
| intrare | `ir_scasb` | probe | 6.40 | 21.52 | 3.36 | 3.36 | 3% / 3% |
| intrare | `ir_scasw` | probe | 1.88 | 5.32 | 2.82 | 2.80 | 5% / 4% |
| intrare | `ir_scasb_once` | probe | 0.87 | 4.06 | 4.66 | 5.21 | 3% / 15% |
| intrare | `ir_lodsb` | probe | 0.87 | 2.80 | 3.22 | 3.23 | 2% / 2% |
| intrare | `ir_lodsb_loop` | probe | 3.78 | 14.16 | 3.74 | 3.66 | 5% / 2% |
| intrare | `ir_stmxcsr` | probe | | | not-lifted | | |
| intrare | `ir_mxcsr_round_restore` | probe | | | not-lifted | | |
| intrare | `ir_mxcsr_default` | probe | | | not-lifted | | |
| intrare | `ir_adc32_reg` | probe | 0.87 | 2.36 | 2.70 | 2.70 | 2% / 2% |
| intrare | `ir_adc64_imm` | probe | 0.87 | 2.33 | 2.67 | 3.14 | 2% / 19% |
| intrare | `ir_adc64_mem` | probe | 0.87 | 2.55 | 2.93 | 2.92 | 2% / 2% |
| intrare | `ir_adc8` | probe | 0.87 | 2.50 | 2.87 | 2.86 | 3% / 2% |
| intrare | `ir_sbb32_reg` | probe | 0.87 | 2.33 | 2.67 | 2.67 | 2% / 2% |
| intrare | `ir_sbb16_imm` | probe | 0.87 | 2.35 | 2.70 | 2.73 | 2% / 3% |
| intrare | `ir_sbb32_mem` | probe | 0.87 | 2.44 | 2.80 | 2.80 | 2% / 2% |
| intrare | `ir_sbb64_reg` | probe | 0.87 | 2.33 | 2.67 | 2.71 | 3% / 4% |
| intrare | `ir_mul8` | probe | 0.87 | 1.51 | 1.74 | 1.76 | 3% / 4% |
| intrare | `ir_mul64_wide` | probe | 0.87 | 2.95 | 3.39 | 3.44 | 3% / 4% |
| intrare | `ir_div64` | probe | 3.02 | 13.90 | 4.61 | 4.62 | 3% / 3% |
| intrare | `ir_idiv64` | probe | 0.87 | 10.31 | 11.84 | 11.85 | 2% / 2% |
| intrare | `ir_cmpxchg16b_direct` | probe | 2.07 | 4.38 | 2.12 | 2.12 | 2% / 3% |
| intrare | `ir_cmpxchg16b_copy` | probe | 6.34 | 9.67 | 1.53 | 1.51 | 2% / 1% |
| mem_str | `str_len` | mem | 529.86 | 2354.47 | 4.44 | 4.47 | 1% / 2% |
| mem_str | `str_cmp` | mem | 6.25 | 47.25 | 7.56 | 7.56 | 2% / 2% |
| mem_str | `mem_cmp` | mem | 7.39 | 27.74 | 3.75 | 3.74 | 5% / 4% |
| mem_str | `copy_words` | mem | 3.30 | 13.08 | 3.97 | 3.80 | 8% / 3% |
| mem_str | `fill_pattern` | mem | 99.62 | 170.22 | 1.71 | 1.72 | 3% / 3% |
| mem_str | `overlap_move` | mem | 4.87 | 19.32 | 3.97 | 3.87 | 6% / 3% |
| mem_str | `fnv1a` | mem | 115.40 | 168.53 | 1.46 | 1.46 | 3% / 3% |
| mem_str | `crc32` | mem | | | not-lifted | | |
| mem_str | `adler` | mem | 245.00 | 288.05 | 1.18 | 1.17 | 3% / 3% |
| mem_str | `insertion_sort` | mem | 29.01 | 154.62 | 5.33 | 5.47 | 4% / 6% |
| mem_str | `sort_ints` | virtual | | | MISMATCH (lifter-bug) | | |
| mem_str | `sort_shorts_desc` | virtual | | | MISMATCH (lifter-bug) | | |
| mem_str | `bsearch_i32` | mem | 2.47 | 9.03 | 3.66 | 3.56 | 5% / 2% |
| mem_str | `utoa10` | mem | | | not-lifted | | |
| mem_str | `hex_parse` | mem | 6.79 | 11.37 | 1.67 | 1.60 | 10% / 5% |
| structs | `pt_add` | call | 0.87 | 2.47 | 2.84 | 3.18 | 5% / 17% |
| structs | `big_make` | call | 0.87 | 3.12 | 3.58 | 3.68 | 2% / 5% |
| structs | `big_sum` | call | 0.88 | 3.96 | 4.49 | 4.48 | 3% / 3% |
| structs | `big_chain` | call | 0.87 | 2.51 | 2.88 | 3.01 | 2% / 6% |
| structs | `rec_pack` | mem | 0.87 | 4.19 | 4.81 | 4.84 | 4% / 4% |
| structs | `rec_fill` | mem | 0.87 | 3.74 | 4.30 | 5.56 | 3% / 33% |
| structs | `bits_get` | arith | 0.87 | 2.06 | 2.37 | 2.36 | 3% / 2% |
| structs | `bits_set` | arith | 0.87 | 1.23 | 1.42 | 1.44 | 3% / 5% |
| structs | `list_sum` | mem | 3.85 | 25.71 | 6.68 | 6.66 | 3% / 2% |
| structs | `list_build` | mem | 4.70 | 21.63 | 4.60 | 4.62 | 2% / 3% |
| structs | `matrix_mul` | mem | 5.03 | 31.33 | 6.23 | 6.24 | 2% / 2% |
| structs | `table_lookup` | mem | 0.87 | 3.79 | 4.35 | 4.34 | 3% / 2% |
| structs | `counter_bump` | mem | 0.88 | 2.27 | 2.60 | 2.64 | 2% / 4% |
| structs | `name_char` | mem | 0.87 | 3.94 | 4.52 | 4.81 | 3% / 10% |
| switches | `dense` | switch | 2.07 | 3.84 | 1.86 | 1.67 | 19% / 7% |
| switches | `dense_offset` | switch | 1.28 | 2.25 | 1.76 | 1.65 | 10% / 3% |
| switches | `sparse` | switch | 1.95 | 3.56 | 1.83 | 1.78 | 8% / 5% |
| switches | `fallthrough` | switch | 2.07 | 4.06 | 1.97 | 1.87 | 9% / 4% |
| switches | `nested` | switch | 1.53 | 3.84 | 2.51 | 2.41 | 6% / 2% |
| switches | `byte_table` | switch | 1.70 | 2.91 | 1.71 | 1.71 | 4% / 3% |
| switches | `interp` | switch | 14.13 | 59.33 | 4.20 | 4.16 | 3% / 3% |
| virt | `shapes` | virtual | 11.44 | 94.83 | 8.29 | 8.25 | 3% / 2% |
| virt | `pick` | virtual | 3.14 | 25.27 | 8.05 | 8.15 | 2% / 3% |
| virt | `multi` | virtual | 3.44 | 29.40 | 8.54 | 8.61 | 2% / 3% |
| virt | `member_ptr` | virtual | 0.87 | 19.27 | 22.13 | 22.49 | 2% / 4% |
<!-- table:end -->
