#!/usr/bin/env python3
"""Write the briefs and unit files for the test-corpus units (issue 6).

One unit = one new source file under tests/corpus/ that makes the compiler emit a given set
of instructions.  The required mnemonics live in the unit's check command, in swarm/units/,
which workers cannot edit.  Re-running this overwrites briefs and leaves existing unit files
(and so their state) alone.

    .venv/bin/python tools/swarm/make_corpus_units.py
"""

import json
import os

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

SS = "eq lt le unord neq nlt nle ord".split()

UNITS = [
    dict(
        id="corpus-fscalar", file="fscalar.c", prefix="fs_", model="haiku",
        what="scalar float and double arithmetic, compares and conversions",
        require=("addss subss mulss divss sqrtss minss maxss addsd subsd mulsd divsd sqrtsd minsd maxsd "
                 "comiss ucomiss comisd ucomisd rcpss rsqrtss "
                 "cvtsi2ss cvtsi2sd cvttss2si cvttsd2si cvtss2si cvtsd2si cvtss2sd cvtsd2ss "
                 + " ".join("cmp%sss cmp%ssd" % (p, p) for p in SS)).split(),
        hints=("The compiler emits `ucomiss`, never `comiss`, for a C comparison: the `comiss` forms need the "
               "`_mm_comi*_ss` intrinsics. The eight compare predicates need `_mm_cmp*_ss` / `_mm_cmp*_sd`. "
               "`cvtss2si` (rounding, not truncating) needs `_mm_cvtss_si32` / `_mm_cvtss_si64`. Cover the 64-bit "
               "integer forms of every conversion as well as the 32-bit ones, and memory-operand forms (load the "
               "operand through a pointer argument, or from a constant the compiler puts in read-only data)."),
    ),
    dict(
        id="corpus-fpacked", file="fpacked.c", prefix="fp_", model="haiku",
        what="packed float and double arithmetic, compares, bitwise operations and conversions",
        require=("addps subps mulps divps sqrtps minps maxps rcpps rsqrtps "
                 "addpd subpd mulpd divpd sqrtpd minpd maxpd "
                 "andps andnps orps xorps andpd andnpd orpd xorpd "
                 "cvtdq2ps cvtps2dq cvttps2dq cvtdq2pd cvtpd2dq cvttpd2dq cvtps2pd cvtpd2ps "
                 "movmskps movmskpd addsubps haddps "
                 + " ".join("cmp%sps cmp%spd" % (p, p) for p in SS)).split(),
        hints=("Use the intrinsics in `<xmmintrin.h>`, `<emmintrin.h>` and `<pmmintrin.h>`. `addsubps` and `haddps` "
               "are SSE3: put `__attribute__((target(\"sse3\")))` on the functions that use them. Take vectors "
               "through pointer arguments (`_mm_loadu_ps`) and return them through pointer arguments."),
    ),
    dict(
        id="corpus-vshuffle", file="vshuffle.c", prefix="vs_", model="haiku",
        what="vector shuffles, unpacks, partial moves, inserts and extracts, non-temporal stores",
        require=("shufps shufpd unpcklps unpckhps unpcklpd unpckhpd movlhps movhlps movhps movlps movhpd movlpd "
                 "movddup movsldup movshdup pshufd pshuflw pshufhw pshufb palignr pslldq psrldq "
                 "punpcklbw punpckhbw punpcklwd punpckhwd punpckldq punpckhdq punpcklqdq punpckhqdq "
                 "pinsrw pextrw pinsrb pinsrd pextrd movnti movntps movntdq lddqu pmovmskb").split(),
        hints=("Several of these are later than SSE2: `movddup`, `movsldup`, `movshdup`, `lddqu` are SSE3 "
               "(`target(\"sse3\")`), `pshufb` and `palignr` are SSSE3 (`target(\"ssse3\")`), `pinsrb`, `pinsrd`, "
               "`pextrd` are SSE4.1 (`target(\"sse4.1\")`). Use many different immediate values for the shuffles; "
               "the immediate is where translators go wrong."),
    ),
    dict(
        id="corpus-vint", file="vint.c", prefix="vi_", model="haiku",
        what="integer vector arithmetic, compares, shifts and packs",
        require=("paddb paddw paddd paddq psubb psubw psubd psubq paddsb paddsw paddusb paddusw psubsb psubsw "
                 "psubusb psubusw pmullw pmulhw pmulhuw pmuludq pmulld pmaddwd pmaddubsw pavgb pavgw "
                 "pminsw pmaxsw pminub pmaxub pminsd psadbw "
                 "pcmpeqb pcmpeqw pcmpeqd pcmpgtb pcmpgtw pcmpgtd "
                 "psllw pslld psllq psrlw psrld psrlq psraw psrad packsswb packssdw packuswb "
                 "pand pandn por pxor").split(),
        hints=("`pmulld` and `pminsd` are SSE4.1 (`target(\"sse4.1\")`), `pmaddubsw` is SSSE3. Every shift has two "
               "forms, a count in an immediate and a count in an xmm register: cover both, with counts at and "
               "above the lane width (the result is defined: zero, or all sign bits for the arithmetic shifts)."),
    ),
    dict(
        id="corpus-intrare", file="intrare.c", prefix="ir_", model="haiku",
        what="integer instructions a compiler rarely emits on its own",
        require=("rcl rcr lahf sahf pushfq popfq xlatb movbe andn shld shrd bsf bsr bt bts btr btc "
                 "xadd cmpxchg cmpxchg16b adc sbb mul div idiv imul cbw cwde cdqe cwd cdq cqo "
                 "jrcxz loop loope loopne enter leave stmxcsr ldmxcsr "
                 "movsb movsw movsd movsq stosb stosw stosd stosq cmpsb scasb scasw lodsb").split(),
        hints=("Most of these need GNU inline assembly (`__asm__`), which clang accepts for this target. Write "
               "each as a small function whose assembly block does one thing and whose inputs and outputs are "
               "ordinary C values, with every clobbered register declared. Cover: the string instructions with "
               "and without `rep` / `repe` / `repne` and with the direction flag both ways (restore it with "
               "`cld` before returning); `bt`-family with register and memory bit strings, including bit offsets "
               "outside the first word; the `lock`-prefixed forms of `add`, `sub`, `and`, `or`, `xor`, `inc`, "
               "`dec`, `not`, `bts`, `btr`, `btc`, `xadd`, `cmpxchg`, `cmpxchg16b` on memory; `rcl`/`rcr` by 1, "
               "by an immediate and by `cl`; `shld`/`shrd` with immediate and `cl` counts. `movbe` needs "
               "`target(\"movbe\")` and `andn` needs `target(\"bmi\")` if written as intrinsics. `ldmxcsr` must "
               "be followed, before the function returns, by an `ldmxcsr` that restores the value it replaced. "
               "`stmxcsr`/`ldmxcsr` only with the default value 0x1F80 or with rounding-mode bits changed."),
    ),
    dict(
        id="corpus-abi", file="abi.c", prefix="ab_", model="haiku",
        what="Windows x64 calling-convention shapes and control flow",
        require="call jmp ret push pop lea movaps movups cmovne sete".split(),
        hints=("This unit is about shapes, not mnemonics. Cover: functions with more than four arguments, mixing "
               "integer, float and double (arguments five and up are on the stack; a float in position 2 is in "
               "xmm1 whatever the other arguments are); structs passed by value of 1, 2, 3, 4, 8, 12, 16 and 24 "
               "bytes (sizes 1, 2, 4, 8 go in a register, the others by hidden pointer); structs returned by "
               "value of the same sizes; variadic functions using `__builtin_ms_va_list`; calls through function "
               "pointers and through tables of them; direct and mutual recursion with a depth bounded by a "
               "masked argument; tail calls; dense switches, sparse switches and switches on 64-bit values (the "
               "compiler picks jump tables, bit tests or compare chains: aim for all three); computed `goto` "
               "through a label table; `__int128` arithmetic (passed by pointer on this ABI); callee-saved "
               "register pressure (many live values across a call). Mark the helpers that must stay real calls "
               "`NOINLINE`. Do not write a function whose frame is 4 KB or larger: that calls `__chkstk`, which "
               "this freestanding build does not have."),
    ),
    dict(
        id="corpus-cpp", file="cpp2.cpp", prefix="cx_", model="haiku",
        what="C++ object-model shapes as the Microsoft ABI lays them out",
        require="call jmp lea mov ret".split(),
        hints=("Cover: multiple inheritance with virtual functions in both bases (calls through the second base "
               "go through adjustor thunks); virtual inheritance (vbtable lookups); pointers to member functions, "
               "called through; pointers to data members; abstract interfaces implemented by several classes, "
               "called through a base pointer chosen by an integer argument; templates instantiated at several "
               "types; lambdas with captures passed to a template and to a function pointer; operator "
               "overloads on a small vector type; placement construction into a caller-supplied buffer. The "
               "build has no C++ runtime: no exceptions, no RTTI (`dynamic_cast`, `typeid`), no `new`/`delete`, "
               "no static objects with constructors, no function-local statics with dynamic initialisers. "
               "Export plain `extern \"C\"` functions (wrap them in `extern \"C\" { }`) that build the objects "
               "on the stack or in a caller-supplied buffer and return integers."),
    ),
]

BRIEF = """# Unit {id}: test programs for {what}

This project turns x86-64 Windows game code into C for arm64 macOS, one instruction at a
time, and checks every translated instruction against an emulator and a real CPU. The checks
need real compiler output to work on. `tests/corpus/` holds small C programs compiled as
Windows x64 DLLs for that purpose. It is thin: many instructions the game uses heavily do not
appear in it at all. Your job is to write one new source file that makes the compiler emit
the instructions listed below, in varied forms.

## Where you work

- Your worktree is the directory you were given (`.wt/{id}`), branch `swarm/{id}`. Work only
  inside it.
- **You create exactly one file: `tests/corpus/{file}`.** Do not edit any other file: not the
  build script, not `corpus.h`, not other corpus sources, nothing under `lifter/`,
  `harness/`, `swarm/`, `tools/`. If you think another file needs changing, say so in your
  report instead.
- Commit your file in the worktree (message ending with the line
  `Co-Authored-By: Claude Haiku 5.5 <noreply@anthropic.com>`). Do not push, merge or switch
  branches.
- Write original code. Do not copy code from anywhere, and never read or copy anything from
  `game/`.
- Python is `.venv/bin/python`. Never run a Python script that uses `multiprocessing` from
  stdin (`python - <<EOF`).

## How the corpus is built

`sh tests/corpus/build.sh` compiles every `.c` and `.cpp` in `tests/corpus/` with Apple clang
for the target `x86_64-pc-windows-msvc`, freestanding (no C library, no system headers except
the compiler's own, such as `<emmintrin.h>`), SSE2 baseline, no AVX, at `-O1`, `-O2` and
`-Os`, and links with `lld-link`. It produces `tests/corpus/build/corpus_<opt>.dll` (everything)
and `tests/corpus/build/one_{stem}_<opt>.dll` (your file alone, plus `mem_str.c` for `memcpy`
and friends). Read `tests/corpus/build.sh`, `tests/corpus/corpus.h` and two existing sources
(`floats.c`, `atomics.c`) before you write anything.

## What to write

Small exported functions, as many as it takes (expect 40 to 120), each `EXPORT`, each named
with the prefix `{prefix}` so nothing clashes with other files in the same DLL.

Instructions that must appear, each **at least 3 times in total** across the three builds of
your file:

    {require_wrapped}

{hints}

Rules for every function, because of how they will be tested (called thousands of times with
random register contents, in an emulator and as translated C, and compared):

1. **It must finish quickly whatever its arguments are.** Any bit pattern can arrive in any
   argument. Bound every loop by masking the count (`n &= 31`). No recursion without a masked
   depth.
2. **It should not fault for most inputs.** Guard divisors (`d | 1`). Pointer arguments are
   sometimes valid pointers to a few hundred bytes of readable and writable memory and
   sometimes garbage: that is fine, but only use the first four arguments as pointers, read
   and write at most the first 256 bytes behind them, and never keep a pointer in a global.
3. **Its result must depend on what the instruction computed.** Return it, or store it
   through a pointer argument. A function whose interesting value is dead will be optimised
   away or will test nothing.
4. **Deterministic.** No globals that change between calls, no time, no addresses in results.
5. **Vary the operand forms.** For each instruction: register to register, memory operand
   (through a pointer argument, and from a constant in read-only data), and each immediate or
   count form it has. Different forms are different code in the translator.
6. Keep each function small and about one thing, with a one-line comment only where the
   intent is not obvious. No file-level essay.

Use the compiler's intrinsics where they exist. Where the compiler will not emit an
instruction from C or intrinsics, use GNU inline assembly with all clobbers declared. Check
what you actually got: compile, then count.

    sh tests/corpus/build.sh
    .venv/bin/python tools/census/require_mnemonics.py tests/corpus/build/one_{stem}_*.dll

The second command lists every instruction in your DLLs with its count. If a required one is
missing, the compiler chose something else (for example it folded your constant, or picked a
different but equivalent instruction): change the C until it appears.

## Done

All of these exit 0 in your worktree, with your file committed and `git status` clean:

{check_block}

The last two lift your `-O2` DLL and run the functions that the lifter can already translate
through the differential test: none may fail. Functions the lifter cannot translate yet are
reported as unsupported; that is expected and is the reason this corpus is being written. If
a function of yours *fails* the differential test, do not delete it to make the check pass:
that is a finding. Keep it, and report its name and the failure message. (If the failure
blocks the check, say so in the report; the orchestrator decides.)

## Report

Reply with: the number of functions; the count for each required instruction; any required
instruction you could not get the compiler to emit and what you tried; any function that
failed or behaved oddly in the differential test; anything you think is wrong in the build
script or the existing corpus.
"""


def checks(u):
    stem = u["file"].rsplit(".", 1)[0]
    dlls = " ".join("tests/corpus/build/one_%s_%s.dll" % (stem, o) for o in ("O1", "O2", "Os"))
    mod = "one_%s_O2" % stem
    return [
        "test \"$(git diff --name-only $(git merge-base HEAD wip/lifter-v0) HEAD)\" = tests/corpus/%s" % u["file"],
        "sh tests/corpus/build.sh",
        ".venv/bin/python tools/census/require_mnemonics.py %s --require %s --min 3" % (dlls, ",".join(u["require"])),
        "make test",
        ".venv/bin/python -m lifter.lift --module %s --dll tests/corpus/build/%s.dll" % (mod, mod),
        ".venv/bin/python harness/difftest.py func --module %s --dll tests/corpus/build/%s.dll" % (mod, mod),
    ]


def wrap(words, width=78, indent="    "):
    lines, cur = [], ""
    for w in words:
        if len(cur) + len(w) + 1 > width:
            lines.append(cur)
            cur = ""
        cur = (cur + " " + w).strip()
    lines.append(cur)
    return ("\n" + indent).join(lines)


def main():
    for i, u in enumerate(UNITS):
        stem = u["file"].rsplit(".", 1)[0]
        cmds = checks(u)
        brief = BRIEF.format(id=u["id"], what=u["what"], file=u["file"], stem=stem, prefix=u["prefix"],
                             require_wrapped=wrap(u["require"]), hints=u["hints"],
                             check_block="\n".join("    " + c for c in cmds))
        with open(os.path.join(ROOT, "swarm", "briefs", u["id"] + ".md"), "w") as f:
            f.write(brief)
        path = os.path.join(ROOT, "swarm", "units", u["id"] + ".json")
        if os.path.exists(path):
            continue
        unit = {"id": u["id"], "title": "Corpus: " + u["what"], "kind": "corpus", "issue": 6, "model": u["model"],
                "priority": 40 + i, "needs": [], "unshare": ["tests/corpus/build"], "check": cmds, "state": "todo"}
        with open(path, "w") as f:
            json.dump(unit, f, indent=2)
            f.write("\n")
        print("wrote", u["id"])


if __name__ == "__main__":
    main()
