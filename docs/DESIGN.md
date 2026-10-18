# Lifter and harness design (v0)

This is the contract between the lifter, the test harness and (later) the runtime. Change it
deliberately, in its own commit, and re-run the whole test corpus when you do.

Terms: *guest* = the original x86-64 Windows code and its view of the world. *host* = the
arm64 macOS process. *module* = one game DLL. *RVA* = offset from a module's image base.

The translators are split into instruction families (section 10).

## 1. What the lifter does

Input: one PE32+ DLL. Output, under `lifted/<module>/` (gitignored):

- `<module>_NNNN.c`: generated C, about 200 guest functions per file.
- `<module>.h`: prototypes.
- `<module>_table.c`: `<module>_table`, the sorted table of `{rva, host function}` for
  indirect dispatch, with one row for every function the lifter found. A lifted function's
  row is its C function. A skipped function's row is a stub, in the same file, that stops
  the run with the reason (`rt_trap_skipped` of `harness/runtime.h`: the function's address,
  that the lifter skipped it, and the first reason of its record with the address of the
  instruction). A direct call to a skipped function names the same stub, so both ways of
  reaching it give the same report. `<module>_table_count` is the number of rows;
  `<module>_skips` lists the skipped functions (`{rva, rva of the instruction, reason}`,
  sorted by rva) and `<module>_skips_count` says how many of the rows are stubs, so the
  number of lifted functions is the difference: a reader must not take the row count for
  it. The isolated build (`-DHARNESS_ISOLATED`) has no stubs: there the table and its count
  are the lifted functions alone, and the skip list is not defined.
- `<module>.report.json`: one record per guest function, a summary, and a *stamp* (below).
  The summary's `guest_patches` lists the places where the C is deliberately not a
  translation ("Guest patches" below).
- `<module>.cfg.json`: for each lifted function, the RVAs of its instructions and of its
  conditional branches, and of its *stops* if it has any (section 3). The function test
  measures coverage against it and ends a case at a stop.
- `<module>.shapes.json`: a sample of translated instructions per operand shape, from which
  the instruction test draws.

A function's status in the report is one of:

- `lifted`.
- `lifted-without-handlers`: lifted, but some bytes inside its unwind ranges are neither
  decoded instructions, recovered jump tables nor padding (`int3`, `nop`). That is normally
  exception-handler code, which is reached only through the scope tables and is not lifted
  yet. The record lists the runs (`undecoded_runs`) and their size (`undecoded_bytes`); the
  lift summary prints the count. These functions are compiled and tested like the others.
- `skipped`, with the reason (an unresolved indirect jump, a refused transfer of control, a
  relocation the lifter cannot place, a decode failure). The first reason is what the
  function's stub reports at run time.

A lifted function may hold *stops*: instructions the decoder decodes and the lifter has no
translation for (`untranslated` in its record, with the count `untranslated_insns`; the lift
summary has `lifted_with_stops`, `stops` and `stops_by_instruction`). The lifted code halts
the run at such an instruction with a report. Section 3 says exactly which refused
instructions are stops; every other refusal still makes the lifter skip the function whole.

Never emit a partial or approximate translation: a missing function is visible, a wrong one is
not. A stop is neither: everything before it is translated exactly, the instruction itself does
not run, and nothing runs after it. If the lifter catches itself breaking one of its own
invariants it raises `LifterBug` and the lift stops; that is not a reason to skip a function.

### Guest patches: where the lifted code differs from the game on purpose

One thing the lifter writes is not a translation. A *guest patch* changes what the game does
at one instruction, because it was decided that the game's own behaviour there is a defect
this port does not keep. `lifter/guest_patches.py` is the mechanism and says the rules;
`lifter/guest_patches.toml` is the table, the only place a patch can come from. In short:

- A row names its module, the SHA-256 of the DLL file it was reviewed against, the rva and
  the exact bytes of the instruction, and through its `[fix]` what the patch does, why, and
  where that was found and decided. The lifter applies a row only when module, hash and bytes
  all match. A row listed for the module being lifted that does not match (another file,
  other bytes, an rva that is no instruction of a lifted function, a shape the kind does not
  allow) stops the lift: `GuestPatchError`, exit status 4, nothing written.
- There is one kind, `reuse-load`: a 64-bit load takes the value that an earlier load put in
  its register, instead of reading memory again. The lifter checks that both are plain loads
  in one lifted function, that the code between them is a straight line nothing branches
  into, that it holds nothing but other plain loads (so nothing there writes memory), and
  that none of them writes the first load's register. That the two loads read the same
  location is the row's claim, made by a person; the lifter does not check it.
- In the lifted C the line after the instruction's comment (which still shows the game's
  instruction) begins with `GUEST PATCH`, the name of the fix and the table. The report lists
  each patch in the record of its function (`guest_patches`) and all of them in
  `summary.guest_patches`; the lift's printed summary has a line for them.
- `lifter.lift --no-patches` (for the make targets `GUEST_PATCHES=off`, `runtime/rt.mk`)
  lifts without them, by default into `build/lifted-unpatched/<module>`, so that the game's
  own behaviour can always be measured. Its C is what the lifter writes with an empty table.
- The table is part of the stamp (below) and of the key of the lift cache.

The rows of today: the push routine of the game's lock-free queue, at its 11 copies in 8
DLLs.

The stamp is a SHA-256 over the lifter's source files (the core and every family module under
`lifter/insns/`), the PE reader, the table of guest patches and the input image; the report
also holds the digest of every generated file. The tests refuse to run on output whose
stamp is not current or whose files are not the ones the report lists.

The lift also runs a relocation census and lint (`lifter/relocs.py`) and reports both in the
summary. A relocated field is an 8-byte slot to which the Windows loader adds the load address
(a DIR64 entry). The oracle and the candidate share one list of them, so a field missing from
the list, or lying where the lifter does not look, would be wrong on both sides and no test
would show it. The census puts every relocated field into a kind by where it lies: a data slot
(the loader's business; lifted code reads it at run time), the 64-bit immediate or the 64-bit
absolute address of a decoded instruction (written `BASE + rva`), part of a decoded instruction
(the function is skipped), bytes of a code section that nothing decodes. The lint looks for the
opposite: 8-byte values, at any byte offset, that lie inside the image at its preferred base and
are not relocated fields, and constants of that range in the generated C. Most such values are
chance, and each is given a kind that says why: parts of instructions (x86-64 has no 8-byte
address field other than the two above), a value at an address that is not a multiple of 8 in
a module whose relocated fields all are, a value that would point into the PE headers. What
fits no such kind is of an *unknown* kind: a field that overlaps another, lies in the headers
or holds a value outside the image; a whole 64-bit instruction field or an 8-aligned value
with no relocation; a constant in the C; a relocation directory that does not parse to its
end. Anything of an unknown kind makes `lifter.lift` exit with status 3 (the files are still
written): somebody has to look before the module is trusted. `make reloc-lint` runs both over
the boot-path modules without writing anything. The kinds found in the game's modules, with
counts and the evidence for each, are given in an earlier analysis.

The lifter is Python 3.12 using Capstone for decoding. No other decoder.

## 2. Finding functions

1. Every `RUNTIME_FUNCTION` in `.pdata` whose unwind info is not chained starts a function.
2. A chained entry (`UNW_FLAG_CHAININFO`) is a *fragment* of the function its chain leads to.
   Follow chains to the root. A guest function is its entry range plus all its fragments.
3. Leaf functions have no unwind entry. Add an entry for the module's entry point, every
   export that is code, every direct `call` target, every tail-jump target and every
   relocated pointer into an executable section that is not inside a known function. Their
   extent is found by decoding (section 3).

## 3. Control flow inside a function

Recursive descent from the entry point; never linear sweep (code ranges contain data).

- A branch target inside the function's ranges becomes a label.
- `jmp` to an address outside the function's ranges is a tail call. `jcc` to outside is a
  conditional tail call.
- `call` continues at the next instruction.
- `ret` ends a path. `int3`, `ud2`, `hlt` and `int 0x29` (`__fastfail`) end a path with a
  trap. Other `int n`: skip the function.
- Running off the end of the function's ranges is a jump to the next address: a trap at that
  address if the byte there is `int3`, otherwise a tail call to it.
- `jmp [mem]` is an indirect tail call when the operand has no index register (an import
  slot, a global pointer, a vtable slot). With an index register: skip the function.
- `jmp reg` is an indirect tail call in exactly two cases, judged by the instruction that
  last wrote the register in the same straight-line run:
  - a 64-bit `mov reg, [mem]` with no index register;
  - a `call`, when the register is `rax` (a thunk that jumps to the address a helper
    returned). A `call` counts as writing every volatile register.

  Otherwise it must be a jump table that section 6 recovers, or the function is skipped.
- Far `call`/`jmp` (`FF /3`, `FF /5`) and near branches with a `0x66` prefix (Intel ignores
  the prefix, AMD truncates the target to 16 bits) are unsupported.

### An instruction without a translation: the stop

Earlier, one instruction the lifter could not translate made it skip the whole
function, and with it every path of the function that never executes the instruction. That
cost the routine `engine.dll` calls from a static initializer to fill its processor-capability
words (one `xgetbv`, behind a test of the CPUID bit that our CPUID reports clear), and three
functions that test a capability bit inside themselves and have a plain path beside the
extension's. Now such a function is lifted, and the instruction becomes a *stop*: the lifted
code calls `UNTRANSLATED(rva, what, needs)` there (section 6), which halts the run with a
report. The rule is in `lifter/stops.py` (`judge`); a refused instruction is a stop only if
all three hold:

1. The refusal is of kind `unsupported`. A relocation the lifter cannot place (`reloc`) means
   the bytes are not what the loaded image holds, and an unresolved indirect jump
   (`indirect-jump`) leaves the function's shape unknown: both stay skips.
2. Recursive descent took the instruction as one that falls through: it gave it no
   control-flow verdict and continued at the next address. A `call`, `jmp`, conditional
   branch, `ret` or trap that the lifter refuses (a far call or jump, a near branch with a
   `0x66` prefix) is never a stop.
3. The decoder agrees that it transfers no control and is something user-mode code executes:
   none of its groups is jump, call, ret, int, iret, branch-relative, privileged, VM, RTM or
   SGX, and its name is not on the list `NOT_STRAIGHT` of what the decoder leaves without such
   a group (`ud0`, `ud1`, `enclu`, `getsec`, `rdmsr`, `in`, `out` and their like). `int n`
   other than 0x29, `syscall`, `sysenter`, `xbegin`: skip the function, as before.

This is the rule to never guess control flow, held for stops. The length of a
stop is the decoder's, as for every translated instruction; the one case met where the
decoder's length is doubtful (`ud0`/`ud1`: two bytes in Capstone 5.0.7, with a ModRM byte in
Intel's manual) is on the list. The shape of a function never depends on what a stop would
have computed, because the lifter never shapes a function by a value: only by operand forms
and by the decoder's list of the registers each instruction writes (the jump analyses of
section 3 and 6). Those lists are read for a stop exactly as for a translated instruction, so
an instruction that writes a register the following code reads needs no special case: the
translation of the rest reads `ctx` and is well-formed whatever the stop would have left
there, and since a path through a stop ends at it, anything the analysis concludes about that
path is never used. Where the stop is the last writer of a jump's register the jump stays
unresolved and the function stays skipped. A function skipped for another reason lists its
would-be stops in `untranslated` and not among its `reasons`.

**What a stop means** is decided when it is hit, not by the lifter. For each stop the lifter
names the CPUID feature bits a processor must report to execute the instruction
(`stops.features`: by the decoder's group, by name for `xgetbv`, `movbe`, `popcnt`, `crc32`,
`rdrand`, `rdseed`; any other VEX-encoded vector instruction waits for AVX at least) and
passes them as text, `NAME=LEAF.REG.BIT` items. The runtime looks the bits up in its own
CPUID answers (`rt_machine_cpuid_bit`, the table `rt_cpuid` reads):

- a needed feature is reported absent (`xgetbv` and OSXSAVE; AVX, AVX2, AES-NI, PCLMULQDQ,
  SHA, BMI1, BMI2, MOVBE, XOP in `engine.dll`): a processor answering CPUID as the runtime
  does raises an invalid-opcode fault (#UD) at that instruction. The stop stands where that
  fault would be, and the report says so: the guest took a path that its own test of the
  feature should have closed.
- every needed feature is reported present (MMX), or the lifter knows of none (x87, `lock
  neg`, a segment the lifter does not handle): a real processor executes the instruction. The
  stop is a gap of the lifter, and the report says that instead. `tzcnt` and `lzcnt` are
  deliberately in this class: a processor without BMI1 or LZCNT runs the same bytes as `bsf`
  and `bsr`, so there is no fault for a stop to stand for.

The harness has no CPUID answers to agree or disagree with: in the function test `cpuid` is a
stub event answered with words derived from the seed on both sides (section 8), and
`tests/linked_main.c` ends the process at a `cpuid`.

### Which bytes reach which translator

A translator is found by the instruction's name, and names are treacherous. The decoder's id
name is wrong for the SSE compares with a predicate (Capstone 5 gives `cmpleps` the id of
`cmpsb`, `cmpeqps` that of `cmppd`, `cmpordps` that of `cmpxchg16b`), and the Intel manual
itself uses MOVSD and CMPSD for a string instruction and for an SSE2 instruction.

*The lifter's name* for an instruction (`mnemonic_of` in `lifter/translate.py`) is the
mnemonic the decoder prints, without prefixes, with one exception: the string instructions on
doublewords are `movsl`, `cmpsl`, `stosl`, `lodsl`, `scasl`, chosen by the opcode byte, so that
`movsd` and `cmpsd` always mean the SSE2 instructions. Translators are registered under this
name, and it is the name in operand shapes, in the instruction index and in the reasons of
skipped functions. A name belongs to exactly one family (section 10).

Three more rules apply before any translator runs, each sufficient on its own for the clashes
known today; all of them are in the core (`Translator._check_encoding`) and apply to every
family:

1. Every translator is registered with the encodings it was written for (`OPCODES`): the
   opcode bytes, `/reg` values and, for SSE, the mandatory prefix. The encoding is read from
   the raw bytes and must agree with the decoder on REX and opcode. Anything else is
   unsupported, whatever it is called (far `call`/`jmp`, the MMX forms of `movq` and `pxor`,
   `tzcnt` under the name `bsf`).
2. An instruction with a register operand that is not a general register reaches a
   translator only if that translator is registered as handling vector operands (`VECTOR_OK`).
3. The string translator checks the opcode byte itself.

`LOCK` and `REP` are taken from the prefix bytes, not from the decoder, which reports at most
one of F0/F2/F3. If the bytes and the decoder disagree (a `LOCK` the decoder does not report,
`LOCK` together with F2/F3, both F2 and F3, an F2/F3 the decoder does not report other than
a mandatory SSE prefix) the instruction is unsupported.

## 4. Guest state

```c
typedef union { uint8_t b[16]; uint16_t w[8]; uint32_t d[4]; uint64_t q[2];     /* unsigned lanes */
                int8_t sb[16]; int16_t sw[8]; int32_t i[4]; int64_t l[2];       /* signed lanes */
                float f[4]; double pd[2]; } Xmm;

typedef struct Ctx {
    uint64_t r[16];            /* rax rcx rdx rbx rsp rbp rsi rdi r8..r15 (encoding order) */
    uint8_t  cf, pf, af, zf, sf, of, df;   /* each 0 or 1 */
    uint8_t  pad;
    uint32_t mxcsr;
    uint16_t fpcw;             /* x87 control word */
    Xmm      xmm[16];
    uint64_t gs_base;          /* guest TEB */
    void    *host;             /* runtime or harness private data */
} Ctx;
```

- Writing a 32-bit register zero-extends to 64. Writing 8 or 16 bits leaves the rest alone.
  `ah ch dh bh` are bits 8-15 of `r[0..3]`.
- v0 computes every flag an instruction defines, eagerly, into the fields above. Flags an
  instruction leaves undefined may hold anything. Liveness-based flag elimination is a later
  optimisation and must not change this contract for flags that are read.
- `mxcsr` is the guest's MXCSR; it starts at `0x1F80`. Its rounding mode, DAZ and FTZ are in
  force for every float instruction, because lifted code runs in the *guest float mode*
  (`harness/fpmode.h`: FPCR.AH, and RMode, FIZ, FZ mirroring RC, DAZ, FTZ) and `ldmxcsr`
  re-enters that mode with the value it loaded. `ldmxcsr` of a value with a reserved bit
  (16-31) set traps (#GP).
- `fpcw` is all the x87 state there is: the control word, `0x027F` at the start of a thread
  (Windows x64). `fnstcw` stores it, `fldcw` loads all 16 bits, `fnclex` does nothing (there is
  no status word), and nothing else reads it: it has no effect on SSE arithmetic. Every other
  x87 instruction is untranslated (`docs/findings/15-x87.md`).
- **RFLAGS is the seven flags above and nothing else.** `pushfq` stores them with bit 1 and
  IF (bit 9) set, as user-mode code sees them in a Windows process, and zeros elsewhere.
  `popfq` loads the seven; a value with TF, NT, AC or ID set, which lifted code could not
  honour, traps before anything changes; IF, IOPL and the reserved bits are ignored.
- **The six MXCSR status flags (IE DE ZE OE UE PE) are not modelled.** Float instructions do
  not set them; `ldmxcsr` stores them and `stmxcsr` returns them as they were last loaded. A
  guest that reads back flags raised by its own arithmetic gets zeros. Unmasked float
  exceptions are not modelled either: a value that unmasks one is stored, and nothing ever
  raises it. The harness compares MXCSR with exactly these six bits masked out (`compare` in
  `harness/difftest.py`), by name, not by leaving the register out.
- One lifted function per guest function: `void <module>_<rva hex>(Ctx *ctx)`. v0 reads and
  writes `ctx->` directly; keeping registers in locals comes after Gate 1.

## 5. Memory and addresses

- Guest address equals host address. Loads and stores go through `LD8/16/32/64(addr)` and
  `ST8/16/32/64(addr, v)`, always-inline functions built on `__builtin_memcpy` (unaligned and
  aliasing-safe).
- A module can be loaded anywhere. Every RIP-relative reference and every absolute address
  becomes `BASE + rva`, where `BASE` is the per-module global `g_base_<module>` set by
  whoever loads the image. The lifted C never contains the preferred image base.
- The guest stack is ordinary memory with `rsp` in `ctx->r[4]`. `push`, `pop`, `call` and
  `ret` move it exactly as x86 does, including the 8-byte return address, whose value is the
  guest address `BASE + rva_of_next_instruction`.
- `gs:[x]` is `LDn(ctx->gs_base + x)`. `fs:` has no translation: an instruction that uses it is a stop (section 3; not used by 64-bit Windows code).

## 6. Calls, jumps out, jump tables

All ways of leaving a function go through macros, so the same generated C serves the
isolated test harness and the real linked program:

| Guest | Emitted |
|---|---|
| `call` known function at rva R | `CALL_DIRECT(<module>_<R>, R, ret_rva)` |
| `call [rip+slot]`, `call reg`, `call [reg+disp]` | `CALL_INDIRECT(target_expr, ret_rva)` |
| `jmp` known function (tail call) | `TAIL_DIRECT(<module>_<R>, R)` |
| `jmp` through slot or register (tail call) | `TAIL_INDIRECT(target_expr)` |
| `ret` / `ret n` | `RET(n)` |
| trap | `TRAP(rva)` |
| an instruction without a translation (section 3) | `UNTRANSLATED(rva, what, needs)` |

`ret_rva` is the RVA after the call; the macro pushes `BASE + ret_rva`. In the linked
program `CALL_INDIRECT` and `TAIL_INDIRECT` look the target up in the dispatch table
(`rt_dispatch_fn`: guest address to host function, never NULL) and call the result; a miss is
logged and trapped. Both tail macros are `musttail` calls, so a guest loop made of tail calls,
direct or through a register or a slot, does not grow the host stack.

`RET` returns to the host caller whatever the guest return address is. So in the linked
program each `CALL_*` checks, after the callee comes back, that `rsp` has the value it had
before the return address was pushed and that the address is still in its slot; if not,
`rt_rsp_mismatch` reports the call site and stops. `longjmp`, unwinding, stack switching and
a `ret` used as a jump therefore stop at the first frame they skip instead of desynchronising
the two stacks without notice; making them work is `docs/RUNTIME.md` section 9.3.
`-DTF_NO_RSP_CHECK` builds the macros without the check.

Jump tables hold 32-bit entries and are reached by

```
mov    eX, dword ptr [rBase + rIdx*4 + disp]      (or movsxd rX, ...)
add    rX, rBase
jmp    rX
```

- Microsoft: `rBase` holds the image base and `disp` is the table's RVA, so entries are RVAs.
  Some use a byte-sized index table first (`movzx idx, byte ptr [rBase + i + d]`).
- Clang: `rBase` holds the table's own address (`lea rBase, [rip + table]`), `disp` is 0 and
  entries are relative to the table.

Both are one computation, target = value of `rBase` + entry. Recover the table by pattern,
read its entries from the image, and emit a C `switch` over the *computed target address*
with one `goto` per distinct entry and a `TRAP` default. The table is only read to learn the
set of targets: read too long is harmless, read too short shows as a trap.

The number of entries comes from a bound on the index register itself: `cmp idx, N` followed
by `ja`/`jae` (the index followed back through register copies), an `and idx, N` that is the
last write to the index, or the largest byte of a byte-index table whose own index is bounded
that way. A conditional branch on anything else is not a bound. Without a bound, a function
that has unwind info reads entries while they are addresses inside it; a leaf function is
not recovered. Once the whole function is decoded each table is checked again: the bounds
check, the load and the add must form a run that is entered only at its start, and every
`lea rBase, [rip + x]` in the function must agree on the base. Each recovered table is
recorded in the report with its layout and the kind of bound.

## 7. Atomics and other special instructions

- `lock`-prefixed instructions and `xchg` with memory go through the `x_atomic_*` helpers in
  `harness/runtime.h`, which use the `__atomic_*` builtins with `__ATOMIC_SEQ_CST`. There are
  2,099 lock-prefixed sites in the whole game.
- x86 allows a locked operand at any address. The host's atomic instructions accept any
  address as long as the operand lies inside one 16-byte block (FEAT_LSE2, present on every
  Apple Silicon CPU) and raise SIGBUS when it straddles a 16-byte boundary. For a straddling
  operand the helpers take one process-wide lock (`tf_straddle_lock`, defined by whoever
  links the lifted code) and do a plain read-modify-write. **This does not exclude a
  concurrent aligned atomic operation on the same bytes**: such a pair is atomic with respect
  to each other on x86 and is not here. Compilers do not generate straddling locked
  operands; the slow path exists so that code which does it without racing still runs.
- The lifter asserts that every instruction with an F0 prefix byte, and every `xchg` with a
  memory operand, emits an `x_atomic_*` operation (`LifterBug` otherwise). A single-threaded
  test cannot see a missing lock.
- `cmpxchg16b` with an operand that is not 16-byte aligned traps: the SDM lists `#GP(0)` "If
  memory operand for CMPXCHG16B is not aligned on a 16-byte boundary", with and without
  `LOCK`, and the oracle implements it.
- `lock neg` is unsupported: the oracle computes two of its flags wrongly and the other four
  cannot tell a right translation from a wrong one. No game module contains it.
- `cpuid` calls `rt_cpuid(ctx)`; the shim reports SSE2-era features only (no AVX, AES, SHA,
  BMI), and SSE3 with them: the sound decoder's routines for "no SSE3" give
  wrong sound (`runtime/core/machine.c`, the top). Code behind those feature checks (only `engine.dll` has any) is lifted with stops at
  the instructions of the extension (section 3).
  `rdtsc` calls `rt_rdtsc(ctx)`. In the harness both are stub events (section 8), not a
  feature shim. `syscall`, `in`, `out`: skip the function.
- x87 (353 instructions in the game): no translation in v0 beyond the control word; each is
  a stop.

## 8. The test harness

Two levels, one mechanism. The oracle is Unicorn running the original bytes. The candidate
is the lifted C compiled for arm64 and run natively in a child process.

**Isolated function test.** For module M, function F, seed S:

1. Build the initial state from S. The image is loaded at a fixed harness address with
   relocations applied to that address; each import slot is filled with a distinct fake
   address. A 1 MiB scratch arena at another fixed address and a stack region with a
   sentinel return address are filled according to the seed's family (below). A case may
   also carry *pokes*: byte strings written into the image, arena or stack before the run on
   both sides; they are part of the initial state. In the function test the rest of the
   address space is part of it too: *demand pages* (below).
2. Oracle: run F in Unicorn. Whenever execution leaves F's ranges, record an event: `(kind,
   target, rsp, rcx, rdx, r8, r9, rax, r10, r11, the 8 bytes at rsp, xmm0-xmm5)`, that is,
   everything a callee could read, since the stub overwrites all of it. Then apply the *stub
   effect* (rax and the volatile registers set to values derived from S and the call index;
   memory untouched) and return to the caller. `cpuid` and `rdtsc` are events of their own.
   Only a `ret` that lands on the sentinel ends the run; a `jmp` or `call` that lands there
   is a (tail) call to that address. Record written pages, executed instructions and the
   direction of every conditional branch.
3. Candidate: a C driver maps the same regions at the same addresses, and the demand pages
   the oracle mapped, runs the lifted F with the harness versions of the macros in section 6
   (same record, same stub effect), and dumps registers, flags, xmm, the call record and
   every 4 KiB page of the image, arena, stack and demand pages that differs from its initial
   contents.
4. Compare all 16 general registers, all xmm registers, `df`, the call record, and every
   changed page on either side. Flags other than `df` are not compared here.

*Stops* (section 3) are results of a third kind. The oracle is told where a function's stops
are (`cfg.json`: that is the lift's own statement, nothing an emulator could know) and ends
the case when it arrives at one, before executing it; the candidate must have stopped at the
same instruction, and the two states are compared like any other result. A disagreement fails
the function. An agreement says nothing about the instruction or what follows it, so such a
seed is counted apart (`stopped`) and is not one of the three conclusive seeds a function
needs to pass: a function passes on the paths that avoid its stops, and one with no such path
is inconclusive.

*Defined guest faults* are results, not failures to run: `#DE` from `div`/`idiv`, `int3`,
`int 0x29`, `ud2`, `hlt`, `#GP` from a misaligned `cmpxchg16b`, and running off the end of
the function into `int3`. The oracle reports the RVA, the candidate must report a trap at
the same RVA, and the state at the trap is compared as in step 4. Any other CPU exception,
a memory fault (in the function test: at an address where no demand page can stand), the
limits (below) and the demand page limit make the case *inconclusive*; its record keeps the
reason, the RVA of the instruction the oracle stopped at and, for a memory fault, the address.

*Limits.* The call record holds 8,192 events and the oracle runs 200,000 instructions. In the
instruction test meeting either makes the case inconclusive. In the function test (proposal 4) a case that meets one *ends at an event*: the event that no longer fits in the
record, or the first event after instruction 200,000 if one comes within 20,000 more. An
event is a point both sides can name: the oracle stops there before the stub effect and tells
the candidate the event's index, the candidate stops when its event of that index arrives,
and the two are compared as in step 4 (all registers as the callee would find them, memory,
the events before it) together with the kind and target of the event itself. A candidate that
returns, traps or runs on without reaching that event fails. A loop with a call in it, which
the stub's return values never end, is judged on its first 8,192 calls this way; a loop
without an event is still inconclusive at instruction 220,000 ("instruction limit").
`--no-cut` gives the old rule.

*The stack probe.* A function with more than a page of locals begins `mov eax, size`, `call`
the compiler's stack probe, `sub rsp, rax`. The probe is no ordinary callee: it returns with
every register as it was, and its caller relies on that for rax. Under the stub effect rax
comes back as a "return value", rsp goes somewhere else, and the function never runs (27
functions in tier0, 17 in vstdlib). The probe is recognised by what its callers do, never by
name or address: a direct call immediately followed by `sub rsp, rax` is a call to it
(`probe_targets`; the report lists the targets found). In the function test a case that
called it and came out inconclusive is run once more with the probe's contract: a call to it
is recorded like any other event, and then only returns, on both sides. A case that was
conclusive under the ordinary stub effect is not touched.

The oracle restores a pristine CPU context before every run (Unicorn otherwise carries
QEMU's exception-in-progress state from one run into the next).

*Demand pages* (function test only; proposal 3). Code reaches memory outside the
three regions all the time: a variable imported from another DLL (the import slot holds a
fake address), a table indexed by half a pointer, the sum of two pointers. Such an access used
to end the case. Now every 4 KiB page in the *window* `0x10000000000` (1 TiB) to
`0x7FFFFE000000` has contents that are a function of the case's seed and the page's address
(`demand_page` in `harness/difftest.py`, `demand_fill` in `harness/driver.c`): 512 pointers
into the arena, in every seed family, so that a pointer loaded from such a page can be
followed and what it leads to is the family's own memory. The oracle maps a page when the
function first touches it, readable and writable, never executable (a call or jump to it
is still a stub event). The candidate gets the list of the pages the oracle mapped and maps
them before it runs. Afterwards they are compared like any other page. The pages go away
before the next case.

- The window is what a native macOS arm64 process can map without meeting its own mappings
  (measured, macOS 27): nothing below 4 GiB can be mapped; the executable, heap and shared
  library cache lie below 1 TiB; the address space ends at `0x7FFFFE000000`. An access
  outside the window is a fault and the case is inconclusive, as before: *near null* (below
  64 KiB), *32-bit value used as an address*, *below 1 TiB*, *above the 47-bit address space*.
- The host's pages are 16 KiB, so the candidate maps the host page around each demand page;
  the up to three neighbours in it get their own demand contents, which is what the oracle
  would have mapped had the function gone there, and are compared too. A page the candidate
  touches outside those host pages is a signal, and the case fails.
- If the kernel will not put a page where the oracle had one, the case is inconclusive
  ("could not map a demand page"), never judged.
- At most 256 pages per case; a case that needs more is inconclusive ("demand page limit").
- **The emulator cuts addresses to 52 bits** (QEMU's `PG_ADDRESS_MASK`; Unicorn runs x86-64
  without paging): a load from `0xf2903683000dc068` is a load from `0x3683000dc068` and
  succeeds if memory is there, where a CPU raises #GP and lifted code gets a signal. The
  address a hook is handed is cut the same way. So the pager works out the operand addresses
  of the instruction itself, with all 64 bits (`Oracle._operands`: explicit memory operands,
  and the stack for push, pop, call, ret, leave), maps a page only for an operand that lies
  in the window, reports the uncut address of one that does not, and maps nothing for an
  access no operand accounts for. What this does not catch: a non-canonical address whose
  low 52 bits fall on a page that is *already* mapped. The emulator reads it, the candidate
  gets a signal, and the case fails; that is a harness artefact to recognise (the candidate's
  signal address is in the failure text), not a lifter fault. It needs an address with random
  top bits to hit one of a few hundred mapped pages, and has not been seen.
- `--no-paging` gives the test as it was.

*In pieces* (function test; `tools/functest`). A module of
tens of thousands of functions is not tested in one process. `function_test_prepare` writes
what depends only on the module and its lift into one directory, once: the image, its `zfill`
variants, the memory files and the driver (all the module's lifted C). `function_test(...,
prepared=True, work_dir=..., batch=n)` then tests any subset of the functions from that
directory, which it only reads (a file that is not what this session would have written is an
error), with its case and result files in `work_dir`, and in rounds of `n` functions: the
oracle and the candidate run a round's cases, the verdicts and the coverage are kept, the
results (up to 8,192 call events per case and side) are dropped. The records are those of the
whole module in one round (self-test "function test in pieces").

*The image in the driver.* The image and the copy it is compared with are two private mappings
of the image file, the live one read-only. The first write to a 16 KiB host page of it faults;
the signal handler makes that page writable, notes it and returns, and after the case only the
noted pages are compared with the pristine mapping. A page never noted was never written, so
the changed pages reported are exactly those a comparison of the whole image finds; the
arena and the stack are compared whole. (engine's image is 334 MB, almost all zero-initialised
data: copied per image variant and compared per case, it was most of the candidate's time.)
The oracle's processes map the image files too, and a round's cases run image by image,
because changing the image is writing all of it into the emulator.

*Seeds.* Twelve base seeds per function: memory in which every aligned 8 bytes is a pointer
back into the arena, argument and scratch registers holding arena pointers or small integers.
Then three seeds of each *family* (30 seeds per function in all), each changing one thing:

| Family | What differs |
|---|---|
| `args-ptr` | rcx, rdx, r8, r9 all hold arena pointers |
| `globals` | as `args-ptr`, and every zero qword in a writable image section is an arena pointer |
| `mixed` | memory is half pointers, half small integers |
| `ints` | memory is small integers only (0, 1, -1, small counts, sign boundaries); pointer arguments |
| `nulls` | memory is pointers, one aligned qword in eight null: a list or tree walked until a null pointer ends (in pointer-only memory it never does) |
| `regs-ptr` | as `globals`, and every register but rsp holds an arena pointer: a function that takes an argument in a register the calling convention does not use for one |

A function is *passing* when at least 3 seeds are conclusive and all conclusive seeds agree.
Passing says nothing about paths no conclusive seed took, so every function's record also
states how many of its instructions were executed by a conclusive seed and how many of its
conditional branches were seen both ways, one way and never; the module totals are printed.

**Instruction test.** The same machinery on one real instruction taken from the game, ending
at the next address (`INSN_END`), with random registers and flags going in. All six status
flags are compared afterwards except those the SDM leaves undefined for that instruction and
input. In every second state each explicit memory operand is preset to a boundary value: 0,
1, -1, the most negative and most positive values, values equal or complementary to the
other operand, both operands zero. For an atomic instruction, half the states have a
naturally aligned operand, a quarter one that straddles a 16-byte boundary, a quarter an
unconstrained one. For a legacy SSE instruction with a 16-byte memory operand that x86
requires to be aligned, four states of five have it aligned and the fifth has it at offset 8,
4, 1, 12, 15 or 2 (in turn), where the expected result is the #GP (below); a RIP-relative or
absolute operand stays where it is. Each instruction's record counts both kinds (`m128_operand`).

*What a state contains.* 24 states per instruction, numbered from 0. Every state: sixteen
general registers from `rand_val` (boundary values, random 64, 32 and 8 bits), except that a
register used in an address holds an arena pointer or a small index; seven flags; 256 random
bytes for the sixteen xmm registers; one of four random memories. On top of that:

| States | What is chosen |
|---|---|
| odd (1, 3, ...) | memory operands preset to the boundary values above (`operand_pokes`); for a 16-byte operand the low quadword, the high one a pick from the same list. Vector registers stay random |
| even (0, 2, ...) | the vector operands the instruction reads (xmm registers, 16-byte memory operands) hold values chosen from the mnemonic's class, `harness/vecstate.py`; everything else is as before, and an instruction without a vector operand has purely random even states |

The classes of `vecstate.py` (`CLASS_TABLE`, matched against the whole mnemonic) and what the
twelve even states hold, in turn (*slot* = position among the even states, not counting those
spent on the #GP of a misaligned operand, so a memory form meets every entry with its result
compared):

| Class | Mnemonics | Even states, slot by slot |
|---|---|---|
| count | `psll psrl psra` x `w d q` with the count in an xmm register or in memory | the count's low quadword: 0, 1, width-1, width, width+1, 2^32, 2^32+1, two random counts in 2..width-2, then again from 0; its high quadword and the data random |
| equal | `pcmpeq pcmpgt` x `b w d q` | *equal*, *edge-equal*, *equal*, *edges* |
| pack | `packsswb packuswb packssdw packusdw` | *edges*, *edges*, *mixed*, *edge-equal*, the pool holding the narrow type's limits too |
| limits | `padds paddus psubs psubus` x `b w`, `pavg`, `pmin pmax`, `pmul* pmadd* psadbw` | *edges*, *edge-equal*, *edges*, *mixed* |
| general | every other mnemonic with a vector operand, and the immediate form of a shift | *equal*, *edges*, *mixed*, at lane width 8, 16, 32, 64 in turn (twelve combinations) |

*equal*: the destination random; of the source lanes a third are copies of the destination
lane, a third the copy plus or minus one, a third left random. *edges*: both operands from the
pool, the pairs walked in order (the destination runs through the pool, the source through the
pool at a distance that grows by one each time round), lanes shuffled. *edge-equal*: the destination from
the pool, each source lane its copy or the copy plus or minus one. *mixed*: every second
destination lane and the other source lanes from the pool, the rest random. The pool of a
lane of n bits is -2, -1, 0, 1 and the largest and smallest signed values with one neighbour
each (`7ffe 7fff 8000 8001` for a word); for a pack and for the general class also the same
around the narrower type's ends (for words: 126..129, 254..257, -130..-127). The structured
values come from a random stream seeded with the state's 256 random xmm bytes, so the same
seed gives the same state and the registers, flags and memory of every state are what they
were before the classes existed. A float instruction (below) has operands of its own and is
not touched. `vecstate.ENABLED = False` gives the old states; the self-tests use it to show
three wrong translations that the old states accept and these refuse.

**Oracle bugs.** A known oracle bug is handled by correcting the expected value where the
right value is certain (32-bit `cmpxchg`: `oracle_corrections`) or by making exactly the
affected cases inconclusive (`shld`/`shrd` with a RIP-relative operand and an immediate
count; `ret imm16` with an immediate of 0x8000 or more, which the emulator takes as a negative
count of bytes to release: `ORACLE_BUG_RET_IMM` in `harness/difftest.py`, not measured on a
CPU, one instance in the 20 modules and that one data). Never by comparing less: there is no flag mask for an oracle bug.

**Oracle compensations.** Each implemented once, each counted in both tests' summaries
(`cases_changed_by_compensation`: the cases in which it changed the emulator's result), each
pinned by a self-test that also shows the emulator still needs it. The rules are in
`harness/oraclefix.py`, `harness/floatcase.py` and `harness/statefix.py`:

| Compensation | Where | Applies to | Justification |
|---|---|---|---|
| NaN operand choice | `floatcase.py` (`NAN_FIRST_OPERAND`, `nan_wants`), applied by the emulator hooks `_fp_capture`/`_fp_apply` in `difftest.py`, so in both tests | `add sub mul div` x `ss sd ps pd`, only when their family declared them float, only the lanes in which **both** operands are NaNs | SSE returns the first source operand, quieted (SDM vol. 1 table 4-7); Unicorn applies the x87 rule (quiet over signalling, else larger significand). Measured on a Core i5-7600K: 5,895 of 5,895 cases. `addss(7f800001, 7fe27b08)` is `7fc00001`, Unicorn says `7fe27b08` |
| Misaligned 16-byte operand | `oraclefix.py` (`aligned_operand`), applied in `Oracle._code`, so in both tests; `Hardware.expect` maps the CPU's #GP to the same defined trap | every legacy SSE encoding (opcode maps 0F, 0F38, 0F3A; no VEX) with a memory operand of exactly 16 bytes, except `movups movupd movdqu lddqu pcmpestri pcmpestrm pcmpistri pcmpistrm`; only when the address is not a multiple of 16 | SDM vol. 2 section 2.4, exception classes 1, 2 and 4: "#GP(0) ... memory operand is not 16-byte aligned" for legacy SSE, with those exceptions. Unicorn checks no alignment. Measured on a Core i5-7600K: 10 mnemonics at offsets 1, 4, 8 fault, before the page fault of an unmapped operand and with nothing written; `movups`, `movdqu` and 4- and 8-byte operands do not (2026-10-09), and 1,056 of 1,056 earlier cross-check cases. The oracle reports a defined trap and lifted code traps (`Translator.xalign`, reached through `xval` and `xstore`): the game's instances are aligned in a correct run, so the trap is a loud stop for a wrong address, not a path to make fast |
| Min and max under DAZ | `oraclefix.py` (`MINMAX`, `minmax_wants`), through the same hooks as the NaN choice | `min max` x `ss sd ps pd`, only when declared float, only with MXCSR.DAZ set, only the lanes in which an operand is a denormal | SDM MINSS/MAXSS selection with a denormal read as a zero of its sign; the CPU returns the selected operand flushed, Unicorn its original bits. Measured: the rule equals the CPU in 12,400 of 12,400 lanes (addendum to the float-mode note); 822 of 822 earlier. `minss(00000001, +inf)` under DAZ is `00000000`, Unicorn says `00000001` |
| FTZ | `oraclefix.py` (`FTZ_ARITH`, `ftz_wants`, `_tiny`), same hooks | `add sub mul div` x `ss sd ps pd`, only when declared float, only with MXCSR.FTZ set, only the lanes with two finite operands (after DAZ) whose exact result is tiny after rounding | SDM vol. 1 10.2.3.3 and 4.9.1.5: a tiny result (rounded with unbounded exponent, still below 2^emin in magnitude) becomes a zero with the sign of the true result; a result that rounds up to 2^emin is kept. Unicorn ignores FTZ. The rule is `experiments/afp`'s, 5,790 of 5,790 against the CPU; with it the emulator equals the CPU on all 24,192 float vector cases (520 differed before). `addss(00000001, 00000001)` under FTZ is `00000000`, Unicorn says `00000002` |
| `ldmxcsr` reserved bits | the `ldmxcsr` branch of `Oracle._code` | `ldmxcsr m32` whose operand has a bit of 16-31 set | #GP(0) "for an attempt to set reserved bits in MXCSR" (SDM, LDMXCSR); measured on the same CPU (`0x11f80` and `0x80001f80` fault, `0xffff` loads). Unicorn loads anything. The oracle reports a defined trap, as for `cmpxchg16b` alignment |
| `pcmpistri` equal ordered | `statefix.py` (`pcmpistri_model`, `pcmpistri_wants`), applied by `Oracle._code`/`_post_apply` after the instruction, so in both tests | `pcmpistri` with imm8[3:2] = 11; only ECX, CF and OF | SDM vol. 2 section 4.1: IntRes1[j] is the AND over i = 0 .. n-1-j, so a match that runs off the end of the 16 bytes is reported (it is what a `strstr` loop continues from). QEMU 5.0 tests only positions where the whole of xmm1's string fits. Measured on the same CPU (cache family `fo-pcmpistri`, control bytes 0x0c 0x0d 0x15 0x40 0x41, 600 cases each): the model equals the CPU in 3,000 of 3,000; the emulator differs in 36 (0x0c) and 45 (0x0d), in ECX and CF together, and in none of the other three |
| RFLAGS in user mode | `statefix.py` (`START_RFLAGS`, `POPF_TRAP`, `popfq_patch`): the start state of every oracle run, and the `popfq` branch of `Oracle._code` | every run starts with IF = 1; after a `popfq` the emulator's IF is set and IOPL cleared again; a `popfq` of a value with TF, NT, AC or ID is a defined trap | A Windows process runs at CPL 3 with IOPL 0: IF reads as 1 and POPF changes neither (SDM, POPF); the real CPU's `pushfq` stores IF = 1 (`tools/hworacle/README.md`). Unicorn starts with IF = 0 and runs as ring 0, so its `popfq` loads IF and IOPL. The trap is the lifted code's own rule (section 4), not the CPU's: the four bits have no place in `Ctx` |
| 8-byte operand read as 16 | `oraclefix.py` (`M64_READ_AS_16`, `overread_page`), applied in `Oracle._bad_access`, so in both tests | `cvtdq2pd xmm, m64` and `cvtps2pd xmm, m64`, only when the operand's own 8 bytes are mapped and the 8 after them reach an unmapped page | SDM, CVTDQ2PD and CVTPS2PD: the memory operand is `m64`. Unicorn loads 16 bytes and reports an invalid read; lifted code reads 8 and completes. The bytes beyond the operand never reach the result, so the emulator is let read zeros: the page is mapped for that one instruction and unmapped before the next. Such a case was inconclusive before and is judged now. Not measured on the CPU with the next page unmapped; measured there: this operand of `cvtps2pd` takes no alignment fault, which a 16-byte one would |
| x87 control word at start | `statefix.py` (`FPCW_DEFAULT`), written in `Oracle.run` and by the driver from the case | every case | A Windows x64 thread starts with `0x027F`; Unicorn reports 0. Both sides start with the case's value (an `fnstcw` case: any 16 bits in three states of four) and the field is compared afterwards. Not measured on the CPU: the hardware runner refuses x87 |

| `bsf`/`bsr r32` with a zero source | `difftest.py` (`BSCAN_RULE`), applied by `Oracle._code`/`_post_apply` after the instruction, so in both tests | `bsf` and `bsr` with a 32-bit register destination, only when ZF says the source was zero; the register gets back the 64 bits it had | The SDM calls the destination undefined. Measured on the Core i5-7600K: all 64 bits unchanged, no zero-extension, which is also what lifted code does (it does not write the destination). Unicorn writes the old low half back as a 32-bit result and clears the upper half. The instruction test does not compare the destination in that case; the function test runs on with it: engine+0xb4bb0 indexes a table with the register, and failed until this was corrected. One CPU: another may differ |

Every result prints how many cases each compensation changed. Known and **not** compensated:
`fldcw` keeps all 16 bits on both sides, where a real CPU is said to hold bit 6 at 1 and bits
7 and 13-15 at 0 (not measured: the runner refuses x87).
FTZ outside the sixteen arithmetic mnemonics (`cvtsd2ss`, `cvtpd2ps`, `haddps`, ...), which is
why a float case under a non-default MXCSR is still never judged by the emulator (below); and
`rcpss rcpps rsqrtss rsqrtps`, where Unicorn computes the exact quotient and the CPU returns a
table value (the translation is the host's `frecpe`/`frsqrte`, bit-identical to this Intel CPU
over all 2^32 inputs). For those
four the emulator is not corrected but **excluded** (`oraclefix.py`, `HARDWARE_ONLY`): it stops
before executing one, so a function-test case that reaches one is inconclusive, and in the
instruction test the expected side is the real CPU or nothing, under the default MXCSR too.

**Float instructions.** A mnemonic a family declares float (`fp="f32 scalar"` and so on,
section 10) is tested differently in the instruction test; the machinery is
`harness/floatcase.py`.

- *Float mode.* `driver.c` enters the guest mode (`tf_fp_enter_guest(ctx->mxcsr)`) immediately
  before the lifted function and the host mode immediately after, and every function lifted
  code can call (`h_stub`, `h_ret`, `h_trap`, `rt_cpuid`, `rt_rdtsc`) enters the host mode
  first and the guest mode again before it returns; the signal handler enters the host mode.
  Both directions are checked at run time: host code that finds FPCR not at the host value,
  or a lifted function that returns in another mode than that of its final MXCSR, makes the
  case fail with status "float mode violated". The linked build must do the same at every
  crossing (shim thunks, callbacks into the guest, new threads: FPCR is per thread and starts
  at the host value).
- *Build flags for lifted C*, in the harness (`LIFTED_CFLAGS`) and required of the linked
  build: `-fno-strict-aliasing -fwrapv -ffp-contract=off -fno-vectorize -fno-slp-vectorize`.
  Contraction would fuse a multiply and an add into one `fmadd`. The vectorisers build the
  mask `0x7fffffffffffffff` as `fneg(all-ones)`, and in the guest mode `fneg`/`fabs` leave a
  NaN bit pattern unchanged, so the mask would be all-ones. After compiling, the harness
  disassembles every object of lifted code and refuses to run if it contains any `fneg` or
  `fabs` (`mask_hazards`): lifted code never needs them, x86 changes signs with bitwise
  instructions. The linked build needs the same scan.
- *Operand order.* Commutative float operations go through `tf_fadd32/64`, `tf_fmul32/64`
  (`runtime.h`): the host instruction written out, because the compiler swaps the operands of
  a C `+`, which changes which of two NaNs is returned (measured in this harness: plain C
  `+` fails exactly like deliberately swapped operands).
- *Inputs.* Per lane at the lane width: pairs from the boundary sets of
  `experiments/afp/afp_inputs.py` (zeros, infinities, quiet and signalling NaNs, denormals,
  smallest and largest normals, ties, near-integers, the neighbours of 2^31, 2^32, 2^63,
  2^64), two NaNs with random payloads, boundary against random bits, random bits, close
  exponents, denormals, cancellations; eight categories that rotate through the states and
  through the lanes. Lanes a scalar instruction must preserve hold random bits and are
  compared. A memory operand gets its value by a poke.
- *MXCSR variants.* Each instruction runs its `states` operand sets under seven MXCSR values:
  default, round down, round up, round toward zero, DAZ, FTZ, DAZ+FTZ.
- *Expected side: the real CPU.* For these cases the expected result is the hardware's
  (`tools/hworacle`): cached answers first, the x86 machine for the misses (`HWORACLE=offline`
  never asks it). The emulator still runs; cases where it differs from the CPU are counted.
  A case without a hardware answer is judged by the corrected emulator if its MXCSR is the
  default ("emulator only" in the summary) and is not judged otherwise ("no hardware
  answer"), and `make family-check` fails for a float mnemonic that has any of the latter.
  The hardware case maps the arena, the stack and only the 4 KiB pages of the image that the
  instruction and a RIP-relative operand lie in.
- The function test runs under the default MXCSR, against the emulator with the NaN
  correction, on its usual seeds.

Rules for anyone, human or agent, changing lifter code:

- Do not edit `harness/` or the test corpus to make a case pass. If the harness is wrong,
  say so and stop.
- Run the full corpus before and after. Nothing that passed may fail afterwards.
- Re-run the lifter before testing: the tests refuse stale output (section 1).
- A change to one instruction family is judged by `make family-check FAMILY=<name>` (section 10).
- Every harness fix needs a permanent self-test that fails without it: a deliberately broken
  translation in `tests/run_selftests.py` that the harness must catch.

## 9. Fixed harness addresses (macOS arm64)

The preferred image base of these DLLs, `0x180000000`, is where macOS maps its shared
library cache, so the harness never uses it. Regions are 16 KB aligned.

| Region | Address |
|---|---|
| Module image | `0x0000200000000000` |
| Scratch arena | `0x0000210000000000` |
| Guest stack | `0x0000220000000000` |
| Fake import targets | `0x0000230000000000` + 16 per slot (never executable; data there is a demand page) |
| Sentinel return address | `0x0000240000000000` (never executable) |
| Demand page window (function test) | `0x0000010000000000` to `0x00007FFFFE000000`, outside the three regions above |

If macOS refuses any of these, pick others and update this table in the same commit.

## 10. Adding an instruction family

The translators are grouped into *families*, and a family is exactly three files that nobody
else touches:

| File | Holds |
|---|---|
| `lifter/insns/<family>.py` | the translators and their registration |
| `harness/insns/<family>.h` | the C helpers only this family's translators call (optional) |
| `tests/vectors/<family>.py` | the family's hand-written single-instruction vectors |

The families today: `data_mov`, `int_arith`, `shift_bit`, `control`, `atomics`, `strings`,
`system`, `sse_mov`, `sse_scalar_arith` (the pattern for float families), `mxcsr`. A float
mnemonic is registered with `fp="f32 scalar"`, `"f32 packed"`, `"f64 scalar"` or
`"f64 packed"`: the lane type and scalar or packed, nothing else; section 8, "Float
instructions", is what the harness derives from it, and `make family-check` then also requires
every tested instance to have run under every MXCSR variant with a hardware answer. `lifter/insns/README.md` is the one page a contributor needs.

**Registration.** A family module creates `F = Family(__name__)` (or
`Family(__name__, header="<family>.h")`) and registers each translator with
`@F.insn(mnemonic, enc(...), vector=...)` or `F.register(...)`: the lifter's name of the
instruction (section 3), the encodings the translator accepts, and whether it handles vector
operands. That fills the three tables the core consults for every instruction (`HANDLERS`,
`OPCODES`, `VECTOR_OK`). Nothing is listed centrally:

- `lifter/translate.py` imports every module in `lifter/insns/` (by directory listing) when it
  is itself imported. A mnemonic registered by two families is an error at import time that
  names both; so is a family whose header does not exist, or a header without a family.
- Every generated C file starts with `#include "runtime.h"` and then one include per family
  header, in name order. The lifter writes these lines from the families it found
  (`translate.c_includes`); the instruction test's generated C goes through the same code.
- `tests/run_selftests.py` runs the vectors of every `tests/vectors/<family>.py` it finds,
  each family in an image of its own, and requires every registered mnemonic to have one.

**What stays in the core** (`lifter/translate.py`), and so applies to every family without its
cooperation: prefix parsing, `Encoding`, the name-clash guard and the prefix-agreement rules
(`_check_encoding`), the refusal of unregistered names, the atomicity assertion, relocation
handling and the operand helpers. `harness/runtime.h` keeps what several families share:
`Ctx`, memory and register access, condition codes, the atomics, the call and return macros,
and the flag helpers `x_szp`, `x_add`, `x_sub`.

**Rules for a family.** A missing instruction must stay visible (`Unsupported`); a wrong one
is the worst outcome. Translate only encodings you registered. When the oracle and the Intel
manual disagree, stop and report the case instead of matching the oracle. Never edit a shared
file; if a helper is needed in the core, say so in the report.

**The check.** `make family-check FAMILY=<name>` is the definition of done for a family; exit
status 0 means done. It builds a staging tree from the *reference commit* (the merge base of
HEAD and `BASE`, default `wip/lifter-v0`; HEAD itself on the branch that introduced the
layout), takes only the family's three files from the worktree, and runs everything there, by
the reference commit's copy of `tools/family_check.py`. So the core, the harness, the tests and
the tools that judge a family are never the family author's copies. It checks:

1. Only the three files changed relative to the reference commit (untracked files count).
2. `make test` passes.
3. The instruction test on real instances: for every game and corpus DLL in the instruction
   index and every operand shape of the family's mnemonics that occurs in reached code, the
   first 2 sample instructions (1 from a DLL whose image is larger than 128 MB, which is
   `engine.dll`), 24 states each. `d3dcompiler_46.dll` is indexed but not sampled. No failure;
   every mnemonic has a passing case or does not occur in any sampled DLL (printed); at most
   10% of a mnemonic's tested shapes may be without a conclusive case (the existing families
   have none). Samples the family refuses are listed, not counted: they stay unsupported.
4. Every registered mnemonic has a hand-written vector in the family's file.
5. Regression: `tier0` and one corpus DLL are lifted and function-tested. No failing function,
   and no function that lifted with the reference commit fails to lift.
6. One last line for scripts: `FAMILY-CHECK family=... result=PASS|FAIL mnemonics=...
   game_instances=... cases_passed=... cases_failed=... cases_inconclusive=... seconds=...`.

Passing results are cached under `harness/out/family-check/_cache/`, keyed by the reference
commit, the three files' contents, the DLL and the sample; `FRESH=1` ignores the cache.

**The instruction index.** `tools/census/insn_index.py` decodes every unwind-table range of
every DLL under `game/` and in `tests/corpus/build/` and records, per lifter name and operand
shape, the count and up to 8 sample addresses per DLL, and whether the lifter's recursive
descent reaches the instance (the linear sweep also decodes data that lies inside code
ranges). It is cached in `inventory/insn_index/`, keyed by each DLL's size and SHA-256.
`--worklist` prints every mnemonic that has no translator, with its count and the number of
functions it blocks; `--mnemonic <name>` prints where an instruction occurs.
