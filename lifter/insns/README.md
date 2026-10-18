# Instruction families: one page for whoever adds one

A family is a cohesive group of instructions (`pshufd`, `pshuflw`, `pshufhw`; not "SSE2") and
exactly three files, named after it. Nobody else edits them, and you edit nothing else.

| File | Holds |
|---|---|
| `lifter/insns/<family>.py` | the translators, and everything that registers them |
| `harness/insns/<family>.h` | the C helpers only these translators call (leave it out if there are none) |
| `tests/vectors/<family>.py` | hand-written test vectors: at least one per mnemonic |

There is no list to add yourself to. The three directories are listed at import time; the
generated C includes your header because your family names it.

## The translator file

```python
"""SSE2 shuffles: pshufd."""

from ..translate import Family, Unsupported, enc

F = Family(__name__, header="sse_shuffle.h")       # Family(__name__) if you have no C helpers


@F.insn("pshufd", enc(0x0F70, mand=0x66), vector=True)
def t_pshufd(tr, flow):
    tr.need_ops(3)
    d = tr.xr(0)                                   # number of the xmm register, or None
    if d is None:
        raise Unsupported("pshufd destination")
    return ["Xmm s = %s;" % tr.xval(1),            # read every source before writing anything
            "ctx->xmm[%d] = x_pshufd(s, %d);" % (d, tr.ops[2].imm & 0xFF)]
```

- **The name** is the mnemonic the decoder prints, without prefixes (`translate.mnemonic_of`).
  The SSE compares are therefore `cmpltps`, `cmpeqss`, ... (the predicate is in the name; with
  an immediate above 7 the decoder prints `cmpps`). `movsd` and `cmpsd` are the SSE2
  instructions; the string instructions on doublewords are `movsl`, `cmpsl`, `stosl`, ...
  A name belongs to one family: registering one that another family has is an import error.
- **`enc(...)`** lists the encodings you translate: opcode keys `0xNN`, `0x0FNN`, `0x0F38NN`,
  `0x0F3ANN`, optionally `reg=` (the /digit of ModRM) and `mand=` (the mandatory prefix `0x66`,
  `0xF2`, `0xF3`, or `0` for "none of them"). Join alternatives with `|`. The core lets an
  instruction reach your translator only if its bytes match, so `pshufd` above never sees the
  MMX `pshufw` (no `66`) or a VEX form.
- **`vector=True`** says you handle operands that are not general registers. Without it such
  an instruction is refused before your code runs.
- One function may serve several mnemonics: stack `@F.insn` lines, or call
  `F.register(name, function, enc(...), vector=...)` in a loop.
- The translator gets `tr` (the `Translator` in `lifter/translate.py`) and returns a list of C
  statements; they are wrapped in one `{ }` block, so short temporary names are fine. Useful:
  `tr.ins`, `tr.ops`, `tr.name`, `tr.enc.key/.reg/.mandatory`, `tr.need_ops(n)`, `tr.bits(i)`,
  `tr.is_reg(i)`, `tr.is_mem(i)`, `tr.val(i, bits)` and `tr.wr(i, expr)` for general registers,
  immediates and memory, `tr.ea(i)` (a temporary holding the operand's address),
  `tr.xr(i)` and `tr.xval(i)` for xmm operands; `T(bits)`, `lit(v, bits)`.
- C side: `Ctx`, `LD8..LD64`, `ST8..ST64`, `LDX`, `STX`, `R8..R64`, `W8..W64`, the flags
  `ctx->cf pf af zf sf of df`, `ctx->mxcsr`, `ctx->fpcw` (the x87 control word: all the x87 state there is), and the `Xmm` union with lanes `b w d q` (unsigned), `sb sw i l`
  (signed), `f` (float), `pd` (double). Your header starts with `#include "../runtime.h"`, has
  the include guard `TF_INSNS_<FAMILY>_H`, and defines `TF_INLINE` functions.
- **Every header is included in every generated C file**, so all families share one C
  namespace. Whatever your header defines is named after one of your mnemonics or after the
  family: `x_pshufd`, `x_pshufd_lane`, `x_sse_shuffle_pick`; macros `TF_SSE_SHUFFLE_...`.
  `make family-check` refuses any other new name.

## Vectors

```python
VECTORS = [
    (0x1000, "660f70c11b", "pshufd xmm0, xmm1, 0x1b"),
    (0x1010, "660f700b4e", "pshufd xmm1, xmmword ptr [rbx], 0x4e"),
]
REFUSED = [(0x1020, "0f70c11b", "pshufw mm0, mm1, 0x1b", "pshufw")]      # optional: must be refused, and why
```

One instruction per 16-byte slot from `0x1000` to `0x4ff0`, in an image of your own; the text
is exactly what the decoder prints (`make test` checks it). Details: `tests/vectors/__init__.py`.
Do not assemble by hand: `tools/insn_vec.py --enc "66 0F 70 /r ib"` prints these lines for every
operand form of an encoding, and `tools/insn_vec.py <hex>` says what bytes of yours decode to.

## Rules

1. **A missing instruction must stay visible; a wrong one is the worst outcome.** Whatever you
   do not translate exactly, refuse: `raise Unsupported("what")`. The function is then skipped
   and reported. Never approximate, never guess an operand form you have not tested.
2. **Translate only encodings you registered**, and register only encodings you tested.
3. **When the oracle and the Intel manual disagree, stop and report the case.** Do not make the
   translation match the oracle, and do not mask the difference. The manual is the reference;
   an oracle bug is handled in the harness by someone else.
   One difference is settled and handled for you: **a 16-byte memory operand must be 16-byte
   aligned.** A real CPU raises #GP when the 16-byte memory operand of an SSE instruction is
   not 16-byte aligned (every one except `movups`, `movupd`, `movdqu`, `lddqu` and the four
   `pcmp?str?`); the emulator alone would not. Lifted code does what the CPU does: it traps at
   that instruction before anything is read or written. You get this by reading such an
   operand with `tr.xval(i)` and storing to one with `tr.xstore(i, expr)`; **never write
   `LDX(tr.ea(i))` or `STX(tr.ea(i), ...)` yourself.** (`tr.xval` refuses a memory operand that
   is not 16 bytes: read an `m64` or `m32` with `LD64`, `LD32`, `LDF64`, `LDF32` at `tr.ea(i)`.)
   The instruction test puts a register-relative operand at an aligned address in four states
   of five and at a misaligned one in the fifth, where the trap is the expected result; a
   RIP-relative operand stays where the encoding puts it, so give your vectors one at an
   aligned address (the data at `0x8040`). `make family-check` requires, for each mnemonic that
   has such an operand, a passing aligned case and a passing misaligned one, and prints the counts.
4. **Never edit a shared file**: `lifter/translate.py` and the rest of `lifter/`, `harness/`
   outside your header, `tests/` outside your vectors, `tools/`, `Makefile`, other families.
   `make family-check` fails if you do, and tests your family against the unedited copies.
5. **If you need a helper in the core** (a new operand helper, something in `runtime.h`, a
   harness change), say so in your report and stop there; do not add it yourself.
6. Anything whose result depends on rounding, NaN rules or MXCSR is a *float mnemonic* and
   must be declared as one: see "Float mnemonics" below.

## Float mnemonics

```python
@F.insn("addss", enc(0x0F58, mand=0xF3), vector=True, fp="f32 scalar")
@F.insn("addsd", enc(0x0F58, mand=0xF2), vector=True, fp="f64 scalar")
def t_add_scalar(tr, flow):
    ...
    return ["float s = %s;" % tr.fval(1, 32),                       # xmm lane 0 or an m32, as a float
            "ctx->xmm[%d].f[0] = tf_fadd32(ctx->xmm[%d].f[0], s);" % (d, d)]
```

`fp=` is the whole declaration: `"f32 scalar"`, `"f32 packed"`, `"f64 scalar"` or
`"f64 packed"`, the lane type and whether the instruction computes lane 0 or all lanes. From
it the harness gives your instruction float-aware operands in those lanes (boundary values,
NaN pairs, denormals, ties, random bits; random bits in the lanes you must preserve), runs
every case under seven MXCSR settings (default, the three other rounding modes, DAZ, FTZ,
DAZ+FTZ) and takes the expected result from a real Intel CPU, not from the emulator
(`docs/DESIGN.md` section 8). `lifter/insns/sse_scalar_arith.py` is the pattern for a scalar
instruction, `lifter/insns/sse_addsub_p.py` for a packed one.

Lifted code runs in the guest float mode (`harness/fpmode.h`), in which plain C on the `Xmm`
lanes reproduces SSE bit for bit **for the formulations measured exact**. They are listed, per
instruction, in the results table of `docs/findings/11-float-mode.md` (column "Exact C");
use those and nothing else. The traps that apply:

- **Operand order.** A commutative operation (add, mul) goes through `tf_fadd32/64`,
  `tf_fmul32/64` of `harness/runtime.h`, first argument = the x86 destination. With two NaNs
  x86 returns the first operand, and the compiler swaps the operands of a C `+` or `*`.
  Subtraction and division are exact as plain C.
- **Float to integer** (`cvtt*2si`, `cvtt*2dq`): never `(int)v`. Range-check first, and return
  the integer indefinite (`INT32_MIN`, `INT64_MIN`) for NaN and out of range; the exact bounds
  are in the finding's table.
- **Round before the range test** for the non-truncating conversions (`cvtss2si`,
  `cvtsd2si`, `cvtps2dq`): `t = rintf(v)` (it follows the rounding mode), then the range test on
  `t`. `cvtsd2si(2147483647.5)` overflows under round-to-nearest.
- **min/max are the host's `fmin`/`fmax`, written out**: `tf_fmin32/64`, `tf_fmax32/64` of
  `harness/runtime.h`, first argument = the x86 destination, second = the source. x86 returns
  the second operand for a NaN and for two zeros, and under DAZ a zero where the operand it
  selects is a denormal; the host instruction does all of that in the guest mode (12,400 of
  12,400 lanes against the real CPU). **Not `a < b ? a : b`** (it returns the denormal under
  DAZ: 2,064 of 6,288 DAZ lanes wrong) and **never libm `fmin`/`fmax`** (they return the
  non-NaN). `lifter/insns/sse_minmax_s.py` is the pattern; packed: the same per lane.
- **Compares:** write `!(a < b)`, never `a >= b` (they differ on NaN); the eight predicates are
  in the table.
- **No `fneg`/`fabs`**, by you or by the compiler: change signs with integer operations on the
  `d`/`q` lanes, as x86 does. The harness refuses compiled lifted code that contains either.
- **Nothing the compiler can fold at build time** may carry a NaN sign: compute at run time.
- **`rcp*`/`rsqrt*` are table approximations on a real CPU**; `1.0f / x` is not the translation.
  The translation is one host `frecpe` / `frsqrte` instruction per lane in the guest mode:
  `tf_frecpe32`, `tf_frsqrte32` of `harness/runtime.h`. That is the **Intel** table bit for bit
  (all 2^32 inputs under all 16 MXCSR settings, `docs/handoff/hw-oracle.md`; 97,236 cases through
  the translators, `harness/recip_sweep.py`); an AMD CPU returns other bits. Family `sse_recip`
  has all four (`rcpss rcpps rsqrtss rsqrtps`). The emulator divides exactly, so for these it is
  never the expected side: only the real CPU judges them, a case it has not answered is
  inconclusive under every MXCSR setting, and a function-test case that reaches one is
  inconclusive (`harness/oraclefix.py`, `HARDWARE_ONLY`).

A packed instruction is the scalar formulation lane by lane, on a copy of the source taken
before anything is written (`Xmm s = tr.xval(1);`, then `ctx->xmm[d].f[j]` and `s.f[j]` for
`j` = 0..3, or `.pd[j]` for 0..1). The source may be 16 bytes of memory, which must be aligned:
`tr.xval` emits the fault (rule 3), and the misaligned states of the test expect it.
`make family-check` fails
for a float mnemonic if an instance did not run under every MXCSR variant or if a case under a
non-default MXCSR has no hardware answer (the x86 machine was unreachable and the case was
never asked before); it prints how many cases the real CPU judged.

## A small task (`swarm/briefs/insn-<family>.md`)

Most families are handed out as tasks of one to four mnemonics that are translated the same
way (`tools/swarm/make_insn_tasks.py --list`). The task's brief names the family, gives the
encodings and what the instructions do, and is all you need besides this page. Such a family
registers **exactly** the task's mnemonics: every other mnemonic is another task's, and the
same mnemonic in two families stops the lifter from importing.
`.venv/bin/python tools/swarm/make_insn_tasks.py --verify <family>` checks it.

## Working

```
.venv/bin/python tools/census/insn_index.py --mnemonic pshufd     # real instances: shapes, counts, addresses
.venv/bin/python tools/census/insn_index.py --worklist            # everything still untranslated
make test                                                         # self-tests, with your vectors
make family-check FAMILY=sse_shuffle                              # the definition of done
```

`make family-check` is the one command that decides whether the family is done (exit status 0).
It checks that only your three files changed and how your header names things, runs
`make test`, runs the instruction test on samples of every operand shape of your mnemonics from
every game and corpus DLL, checks that every mnemonic has a vector and a passing real instance,
lifts and function-tests `tier0` and a corpus DLL, and ends with a line starting `FAMILY-CHECK`.
Put that line in your report, together with every sample your family refuses and every case you
could not make conclusive.
