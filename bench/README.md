# bench: lifted code against the same C built natively

For the test corpus the original C source exists, so every function can be built two ways:

- **native**: the corpus source compiled for arm64 macOS at `-O2`;
- **lifted**: the corpus source compiled to an x86-64 Windows DLL (`tests/corpus/build.sh`),
  lifted to C by `lifter/lift.py`, and that C compiled for arm64 as a really-linked build
  (the macro set of `harness/runtime.h` without `-DHARNESS_ISOLATED`).

The driver calls each described function through both with the same inputs, checks that the
results and every buffer are identical, and then times both. The comparison does not involve
the emulator: a mismatch is a lifter bug, undefined behaviour in the corpus function, or a
bug in the description or the driver. Findings: `docs/findings/25-lifted-speed.md`.

## Running it

    make bench-smoke     # build both sides, compare every described function, short timing pass
    make bench           # more inputs, longer timing; writes bench/build/results.json
    make bench-gap       # the variant builds behind the gap analysis (several minutes)
    make bench-mutants   # break the lifted C on purpose and see whether the comparison notices
    make bench-tso       # the timing inside the entitled probe app, x86 memory ordering off and on
    make bench-clean

`make bench-smoke` exits non-zero if any function mismatches and is not listed in
`known_mismatches.tbl`, or if a table row names a function that does not exist. It needs
Apple clang, `lld-link` (for the corpus DLLs) and `.venv/bin/python`. Everything it writes
goes under `bench/build/` (gitignored), plus `tests/corpus/build/` for the DLLs.

The driver can be run by hand once built:

    bench/build/driver --filter mem_str/ --cases 100000          # compare only
    bench/build/driver --filter calls/fib --rounds 30 --verbose  # compare, then time
    bench/build/driver --kind virtual --rounds 20 --json out.json
    bench/build/driver --filter abi/ab_s3_ret --cases 5 --show 5 # print arguments and both results
    bench/build/driver --list                                    # every row, its RVA, lifted or not
    bench/build/driver --cores                                   # which CPU numbers are performance cores

    .venv/bin/python bench/report.py summary bench/build/results.json
    .venv/bin/python bench/report.py functions bench/build/results.json    # markdown table
    .venv/bin/python bench/report.py variants bench/build/v/*/results.json
    .venv/bin/python bench/report.py compare bench/build/v/O2/results.json bench/build/v/restrict/results.json

## What is where

| Path | What |
|---|---|
| `functions/<file>.tbl` | the table: one row per benchmarked function of `tests/corpus/<file>.c` |
| `gen.py` | turns the tables into C: argument descriptors and a typed native call per function |
| `driver.c`, `bench.h` | loads the DLL images and the native dylibs, generates inputs, compares, times |
| `bench.mk` | make rules (included from the root `Makefile`) |
| `native/prefix.h` | stands in for `tests/corpus/corpus.h` in the native build (declspecs, `usize`, `va_list`) |
| `native/emmintrin.h` | the nine SSE intrinsics `abi.c` uses, so that the file compiles for arm64 |
| `native/intrare_ref.c` | C reference for `intrare.c`, which is x86 inline assembly and has no native build |
| `known_mismatches.tbl` | mismatches that have been explained; reported, not fatal |
| `variants.py` | variant builds of the lifted side (optimisation level, `restrict`, dead flags, ...) and the mutation check |
| `hand/` | hand-edited lifted functions used by two of the variants |
| `tso.sh` | builds the driver into a copy of the entitled probe app and times it with x86 memory ordering off and on |
| `report.py` | tables from the driver's JSON |
| `results/` | the raw results the findings quote |

## How a comparison works

1. `gen.py` writes, for every row, a C function that calls the native build with the
   function's real prototype (so the compiler applies the arm64 convention), and a descriptor
   of each argument.
2. The driver maps `tests/corpus/build/one_<file>_O2.dll` at a fixed address, applies its
   base relocations and sets `g_base_one_<file>`, so that the lifted code's `BASE + rva`
   references (constants, globals, jump tables, vtables) work. The native side is
   `bench/build/native/<file>.dylib`: the same source plus `mem_str.c`, exactly what the DLL
   holds.
3. For each case it draws argument values from a generator seeded by `--seed` and the
   function's name, and fills the buffers. Then:
   - native: the generated call; the buffers are saved and restored to their initial bytes;
   - lifted: arguments go into a `Ctx` the way a Windows x64 caller passes them (rcx, rdx,
     r8, r9, then stack slots above 32 bytes of home space; structs of 1, 2, 4 or 8 bytes in
     the slot, other structs and 128-bit integers by address of a copy; a hidden first
     argument for a struct result that does not fit in rax), on a real 1 MiB guest stack with
     a sentinel return address; the lifted function runs; the result is read from rax (or
     the hidden buffer, or xmm0 for a 128-bit integer).
   Both sides see the same buffers at the same addresses, so pointers that a function
   stores or returns compare equal.
4. Compared: the result (only the bits the result type defines), every byte of every buffer
   and of the guard bytes between them. On the lifted side also: rsp is entry + 8 on return,
   the nonvolatile registers (rbx, rbp, rsi, rdi, r12-r15, xmm6-xmm15) are unchanged, the
   direction flag is clear, and a struct result's address is back in rax.
5. A lifted call that traps (`rt_trap`), calls an address that is no lifted function, faults,
   or does not return within half a second of CPU time is a mismatch with that reason. The
   exception: reaching a function the lifter reported as skipped is "needs unlifted code",
   and that case is left out.
6. Each function is compared in a child process (`--no-fork` turns that off). A translation
   that damages memory it does not own then costs that one function, reported as a mismatch,
   and every function sees the modules' globals as they were at load.

The inputs are random but not naive: integers mix random 64-bit values with small values,
boundary values and powers of two; one time in six an integer repeats an earlier integer
argument or lands next to it; structs are sometimes all zero or all ones; the six status
flags and every register that is not an argument hold random values at entry.

Bits of an argument register above the argument's width are undefined in the convention; half
the cases fill bits 32-63 with junk. Below bit 32 a narrow argument is extended the way
clang's own callers extend it (clang's callees may rely on that).

## Timing

Per function, 32 prepared inputs; one round is as many passes over them as fit in the target
time. Rounds alternate which side goes first. The reported time is the best round, and rounds
continue (up to `--max-rounds`) until the best time of each side has been reproduced within 2%
by two other rounds. "spread" is how far the median round is from the best.

The native call is a call through a pointer with the arguments loaded from the prepared
case; the lifted call is five stores into the `Ctx` (four argument registers and rsp), the
stack arguments if there are any, a call through a pointer, and a load of rax. Neither loop
can inline the function. A function that returns at once costs about 0.9 ns natively and
about 1.3 ns lifted in this harness, so the ratio of a very small function is mostly the
price of the calling convention.

Rows marked `@restore` have their buffers restored before every call (a sort would otherwise
see sorted input after the first pass); the cost of the restore is measured separately and
subtracted from both sides.

Two things about the processor show in the numbers. The performance cores run faster when
the rest of the machine is idle (0.78 ns for an empty native call against 0.88 ns under
load), so absolute times from different runs are not comparable; ratios within a run are.
And some small lifted functions have a rare fast mode, so the driver prints the mean of the
median rounds beside the mean of the best rounds.

macOS has no thread affinity on Apple Silicon. The driver asks for user-interactive QoS and
checks the CPU number before and after every round; a round that touched an efficiency core
is repeated. If no performance core can be had at all (macOS confines a process tree running
at background priority to the efficiency cores, which happens to everything under an
application in App Nap), the rows are marked `E` and a note is printed: those timings are
real but are not performance-core timings.

## Adding a function

Add a row to `functions/<file>.tbl` (the format is documented at the top of `gen.py`):

    file  name  kind  result  [argument ...]  [@option ...]

    mem_str  fnv1a    mem   u32   buf=256  u32
    structs  pt_add   call  s8a4  s8a4  s8a4
    calls    sum_va   call  i64   u32  ...  i64  i64  i64  i64  i64  i64  i64

- `kind` only groups the timings: `arith`, `mem`, `call`, `switch`, `virtual` (any indirect
  call), `atomic`, `probe` (single-instruction probes; left out of the headline number).
- A pointer argument is `buf=N`. N must cover every byte the function can touch through it
  for any value of the other arguments. Use `fill=` when random bytes would not exercise
  the function (`str`, `hex`, `i32sorted`, `list16`, `small`, `small32`, `small64`, `zero`),
  `copyof=I:mut` for the second operand of a comparison, `off=K` when the function walks
  downward from the pointer, `nocmp` when the buffer receives something whose layout
  differs between the two ABIs (an object with a vtable pointer).
- An integer argument takes `lt=N`, `mask=M`, `eq=V` or `pick=a|b|c` when the default
  distribution (random 64 bits, small values, boundary values) would rarely reach an
  interesting path, such as the keys of a sparse switch.
- A struct passed or returned by value is `sNaM` (N bytes, alignment M); it must not be a
  struct of only floats. A function with float or double arguments cannot be described yet.
- If a result register is only partly defined by the function, declare the narrower type.

Then `make bench-smoke`. A row for a function the lifter skips is fine: it is reported as
not-lifted and joins the comparison when it lifts. For a new corpus file, add its name to
`BENCH_FILES` in `bench.mk` and create `functions/<file>.tbl`.

If the new row mismatches: look at the three example cases the driver prints, decide which of
the three it is (lifter, corpus, description), and only when it is explained add it to
`known_mismatches.tbl` with the class and write it up in the findings.
