# 16. What the game asks the CPU about itself

Scope: `game/bin/x64_retail/*.dll` and `game/server.dll`, 25 x64 images: the census's 24, plus
`d3dcompiler_46.dll`. d3dcompiler has no cpuid or MXCSR sites and 3 rdtsc sites in unwind-less code.
A stray `game/r2/paks/Win64/ui(11).dll` is outside that scope and was not analysed.

Method. All scripts are in `tools/scout/` and read the binaries only. Shared decoder: `cpu_core.py`.
`cpu_sites.py` runs the census sweep over every `.pdata` range and reproduces the census counts
(cpuid 123, rdtsc 187, stmxcsr 43, ldmxcsr 22, pause 42). `cpu_gap_check.py` covers code outside
`.pdata`. Question scripts: `cpu_q1_cpuid.py`, `cpu_q2_isa.py`, `cpu_q3_q4.py`, `cpu_q5_rare.py`.
`cpu_show.py` prints a function for inspection. Their JSON output goes to the scratchpad, not the
repo. The leaf and bit analysis is a backward constant search plus a forward taint walk over
registers and stack or global slots. The gating test is a heuristic (a flag read within 8
instructions before a branch, then a dominance check). Treat "not gated" as "no gate found".

## Headlines

1. **cpuid.** 131 sites in total: 123 in `.pdata` code and 8 in unwind-less engine.dll code. Leaves
   requested: 0, 1, 7, 2, 4, 0x80000000, 0x80000001, 0x80000008, plus two enumeration loops. No brand
   string (0x80000002-4), no 0x15, 0x16, 0xB, 0x1F or 0xD. Bits tested in the same function include
   the leaf 1 SSE/SSE2 bits, the AVX, AES, FMA, OSXSAVE and SSE4 bits, and leaf 7 AVX2 (bit 5) and ERMS.
2. **AVX/AES/SHA/BMI2/PCLMUL.** All in engine.dll. They are OpenSSL CRYPTOGAMS x86_64 code.
   51 code units hold them: 43 `.pdata` functions and 8 unwind-less gaps. Only 10 are reachable in
   the static call graph, through relocated function-pointer tables. No cpuid-dependent branch was
   found on those paths. **The claim "none reachable when cpuid reports no AVX" is not confirmed,
   and the static evidence contradicts it for those 10 units.**
3. **rdtsc.** 212 sites (187 in `.pdata`, 25 in gaps) in 103 `.pdata` functions plus gap routines.
   The result is 3 timing helpers that return the TSC, 30 elapsed-time functions, and 70 functions
   that only store a timestamp. No dedicated seed function. No `rdtscp`.
4. **MXCSR.** 43 stmxcsr, 22 ldmxcsr. Only bink2w64 `f2e920` changes the mode: it sets FZ and DAZ,
   forces round-to-nearest, and restores the saved image before returning. Every other ldmxcsr
   restores a value that was saved earlier. No constant MXCSR is ever loaded.
5. **Rare instructions.** The `.pdata` sweep decodes no xgetbv, rdrand or rdseed. Raw bytes exist:
   three xgetbv, two rdrand and four rdseed. Most are in OpenSSL's unwind-less CPU-detection
   routine. `in`, `out`, `hlt`, `int1` and `syscall` are data mis-decoded by the sweep. The one real
   rare instruction is `int 0x29` (84 sites, MSVC `__fastfail`).

## 1. cpuid (Question 1)

Full site list: `q1_cpuid.json` in the scratchpad. Counts below are over 123 `.pdata` sites, with the
8 gap sites added at the end.

| Purpose | Sites | Leaf (sub-leaf) requested | Bits tested in the same function |
|---|---|---|---|
| Feature detection: a 4-query block (leaves 0, 1, 7, 1) in 15 images (`engine` 51f8c0, `client` 80813c, `tier0` 2cdb4, and others; the named bits match where checked), plus single queries of 0x80000001 and 0x80000008 in `tier0` | 73 | 1, 7 (0), 0x80000001, 0x80000008 | Leaf 1 ECX: AESNI, AVX, CMPXCHG16B, F16C, FMA, MOVBE, OSXSAVE, PCLMULQDQ, POPCNT, RDRAND, SSE3, SSE4.1, SSE4.2, SSSE3, XSAVE. Leaf 1 EDX: MMX, SSE, SSE2, HTT. Leaf 7 EBX: AVX2 (bit 5), ERMS (bit 9). |
| Max-leaf and vendor checks | 42 | 0, 0x80000000 | Max-leaf compares. `amd_ags_x64` 5d20 also compares family and model. |
| Topology and cache | 2 | 2, 4 (0) | Stored, not tested in the function. |
| Leaf-enumeration loops (`tier0` 18f70, 199f0) | 2 | 1..max and 0x80000001..max | Stores every leaf. |
| Leaf from caller, or composite | 4 | unresolved | `tier0` ca60 (leaf in ecx) and cdc0 (leaf in edx) have no direct caller in the image. d490 and df60 compute the leaf from `ebx | edi`. |
| Gap: OpenSSL-style routine, engine 55c39d (8 sites) | 8 | 0 (sub param), 0x80000000, 0x80000001, 0x80000008, 1 (twice), 4 (0), 7 (0) | Leaf 1 EDX bit 28 (HTT) cleared when not hyperthreaded. `bt ecx, 27` (OSXSAVE) tested before the xgetbv in the same routine. |

Points for the port:

- No leaf 0x15 or 0x16 is queried, and no brand string. Timing calibration uses rdtsc deltas only,
  for example in `tier0` `GetCPUInformation` (ca90) and at 19260.
- `tier0` `GetCPUInformation` writes its results to a stack buffer that is consumed outside the
  function. Bits used through that structure are not named here.
- Vendor: `amd_ags_x64` 5d20 has separate Intel and AMD branches. The answer to leaf 0 decides which
  one runs. This is not resolved.

## 2. AVX, AVX2, AES, SHA, BMI2, PCLMUL in engine.dll (Question 2)

**Library: OpenSSL (CRYPTOGAMS x86_64 perlasm).** Evidence: `engine.dll` carries the module banners of OpenSSL's CRYPTOGAMS x86_64 assembly (AES-NI, AES-NI GCM,
GHASH, SHA-1/256/512, Montgomery multiplication, GF(2^m) multiplication, RC4, the AES-NI CBC + SHA-256
stitch), and a bit-sliced SSSE3 AES by
Mike Hamburg, and the name `OPENSSL_ia32cap`. The SHA-256 round constant occurs 15 times and the SHA-1 initial value 7 times.
The ISA mix (AES-NI and PCLMUL for GHASH, SHA, AVX2 and BMI2 for RSAZ) fits that library. XOP
instructions (`vprotd`, `vprotq`, 56 sites, AMD-only encodings that capstone does not group) are
consistent with OpenSSL's XOP code paths. This is consistent with the library, not proof of it.

Counts (`cpu_sites.py`): AVX 6,633, AVX2 4,193, AES 1,103, SHA 512, BMI2 800, PCLMUL 164, BMI1 120.
The sum over groups is 13,405. The unique instructions are 12,937.
All of them are in engine.dll.

**Coverage.** `.pdata` covers only part of engine.dll's `.text`. Outside it, 777,367 bytes sit in
21,045 gaps. Most of those are int3 padding. Eight gaps hold 733 ISA instructions (AVX 354, AES 300,
PCLMUL 32, AVX2 47). The census could not see these. The analysis covers them as units.

**Reachability.** 51 units hold ISA code. The static graph has 11,030 direct-call edges into units
and about 25,000 indirect call sites that are not resolved.

- 41 units have no path from an export, the entry point, or an address-taken unit. Their direct
  callers are only other ISA units.
- 10 units have a path. Each chain starts at one of 7 units whose begin address is stored as a
  relocated pointer in data: slots 0x63ef78, 0x63efe0, 0x63fed8, 0x63fee0, 0x640118 (`.rdata`),
  0x646148 (`.rdata`) and 0x80d8b0 (`.data`). Those dispatchers call the ISA units directly, and
  none of those call sites is dominated by a flag test by the gating heuristic.
- The 10 reachable units: `g563fcd`, `g5699dd`, `f56d330`, `f56d3a0`, `g56d845`, `g56e46a`, `f56f650`,
  `f570060`, `g596f4a`, `g599940`.

**Flag words.** The cpuid routine in engine 51f8c0 writes the flag words 0x839540 and 0x839544 (in
`.data`) and 0x14c84454 (in zero-initialised `.data`). Eight units read them. One of those readers,
f404740, writes a four-dword selector block at 0x14c6f120. The only unit that reads that block,
f404b40, does not reference the ISA dispatch tables. The OpenSSL capability vector is
`OPENSSL_ia32cap_P`. The two adjacent dwords at 0x839540 and 0x839544 match its layout, but I have
not verified the symbol.

**Verdict on "if cpuid reports none of those features, none of that code is reachable":**
not confirmed. For 41 units the static graph agrees. For 10 units the static graph shows a route
from relocated dispatch tables to the ISA code, and no cpuid-dependent branch on that route. The
claim therefore does not hold for those 10 units unless the tables are never used at run time, or
the selection goes through a value this analysis cannot follow.

## 3. rdtsc (Question 3)

187 `.pdata` sites in 103 functions, plus 25 gap sites. By function (the rule is the sink of the
TSC value after the `rdtsc`):

- 3 timing helpers that return the TSC (client `feec0`, tier0 `c6c0` and `191c0`). The gap routines
  add 22 more, most in other images.
- 30 elapsed-time functions, where the TSC is subtracted from another value. Examples: tier0
  `GetCPUInformation` (ca90), with two rdtsc deltas, which is probably clock-speed calibration (not
  verified); `server` 385e20 (30 sites); `mileswin64` 13280 and 14ee0.
- 70 store-only functions: a timestamp is stored and nothing else happens. Most are same-size
  routines (232 to 238 bytes, or 314 bytes), one or more per image. They look like one timer library.
- Four sites mix the TSC with xor, imul or rotate and have no delta or return. No function is a
  dedicated random-seed routine.
- No `rdtscp` in the game code.

## 4. MXCSR (Question 4)

- 43 stmxcsr and 22 ldmxcsr across 47 functions. Only bink2w64 `f2e920` changes the MXCSR:
  `and 0xffff9fff` clears the rounding bits (round-to-nearest), `bts 15` sets FZ, and `or 0x40`
  sets DAZ. It stores the image to a stack slot and restores it at the end of the function. The
  change is scoped to that function.
- 40 other stmxcsr sites are plain saves. 14 ldmxcsr restore a stack slot saved in the same
  function (scoped). One restores a stack slot with no matching save in its function.
- 7 stmxcsr write to `[reg+88]` of a per-context structure (client, engine, server, vgui2,
  vguimatsurface, vphysics, vstdlib). Seven ldmxcsr restore from a register-held pointer, in other
  functions of the same image. These are save and restore pairs across calls, not global changes.
- No ldmxcsr loads a constant. The initial MXCSR is not set by any of these DLLs, so it comes from
  the OS. I assume the Windows default 0x1F80 (all exceptions masked, round-to-nearest, FZ and DAZ
  clear). This is not confirmed from the binaries.

## 5. Rare instructions (Question 5)

Raw byte search over executable sections versus the `.pdata` decode and the reachability check
from primary entries (`cpu_q5_rare.py`).

| Mnemonic | Sweep decodes | Raw encodings | Verdict |
|---|---|---|---|
| xgetbv | 0 | 3: engine 55c5a1 (no `.pdata`), materialsystem ce024 (`.pdata`, not reached), d3dcompiler 36522c (no `.pdata`) | engine 55c5a1 is in the OpenSSL-style gap routine, after a `bt ecx, 27` (OSXSAVE) test. Keep OSXSAVE clear and that path is skipped. The other two were not checked further. |
| rdrand | 0 | 2: engine 35687d (`.pdata`, not reached), 55c686 (gap, same routine) | 55c686 is in the same gap routine. The RDRAND gate was not checked. |
| rdseed | 0 | 4: client 394f30 and 394f74 (`.pdata`, not reached), vgui2 (1 hit, location not checked), engine 55c6a6 (gap) | 55c6a6 is in the same gap routine. The rest were not reached from primary entries. |
| syscall | 4 | 104 (mostly inside `.pdata`) | None strictly reached. Data. |
| in | 225 | in engine, server, client, d3dcompiler, and others | 2 strictly reached, both in engine f3ae4d0, which has an unresolved jump and data-like neighbours. Suspect. 19 reached only through fragments. The rest are data. |
| out | 188 | | 4 strictly reached: 3 in engine f3ae4d0, 1 in materialsystem f144420. Suspect, as for `in`. 25 fragment-only. The rest are data. |
| hlt | 57 | | 1 strictly reached (client 603210), suspect. The rest are data. |
| int1 (0xF1) | 74 | | None strictly reached. Data. |
| int | 154 | | 84 are `int 0x29` (MSVC `__fastfail`), all strictly reached, real code. The other 70 have random immediates and are not strictly reached. Data. |

The neighbouring bytes of the strictly reached `in` and `out` sites (engine f3ae4d0) decode as
`cmp al,[rax]` followed by `in eax,0x3a`, which looks like data. The fragment-only sites in client
72e1d4 decode as `cwde`, `fbstp`, `in al,dx`. Both are most likely mis-decoded. Confirm by hand before
relying on it.

## 6. Bottom line for the shim

**cpuid leaves and bits**

- Leaf 0: max basic leaf at least 7. Vendor string: choose one; amd_ags 5d20 branches on it.
- Leaf 1 EDX: MMX (23), SSE (25) and SSE2 (26) should be 1, as the SSE2 baseline requires. HTT (28)
  is tested in several images; answer consistently.
- Leaf 1 ECX: AVX (28), F16C (29), FMA (12), AESNI (25), PCLMULQDQ (1), RDRAND (30), XSAVE (26),
  OSXSAVE (27), MOVBE (22), POPCNT (23) and CMPXCHG16B (13) should be 0. SSE3 (0), SSSE3 (9), SSE4.1
  (19) and SSE4.2 (20) are tested. Whether the code has SSE2-only fallbacks for them is not
  determined. This is the main design decision.
  **Decided for SSE3: 1.** `binkawin64+0x359e` picks its transform routines by this
  bit, and the set for "no SSE3" gave scrambled sound in the run (`runtime/core/machine.c`, the
  top). SSSE3, SSE4.1 and SSE4.2 stay 0.
- Leaf 7 EBX: AVX2 (5) must be 0. ERMS (9) is tested, and 0 is safe. BMI1 (3), BMI2 (8), RDSEED (18),
  SHA (29) should be 0. Leaf 7 ECX and EDX are not tested.
- Leaf 0x80000000: at least 0x80000008 if the code checks the maximum before querying 0x80000001
  and 0x80000008. The check order was not verified.
- Leaf 0x80000001: no in-function bit tested. Set LZCNT, XOP, FMA4 and PREFETCHW to 0.
- Leaf 4 and leaf 2: cache results only, not tested. Enumeration loops need a finite maximum.
- Leaves 0x15, 0x16, 0xB, 0x1F and 0xD are never queried. Brand string never queried.

**MXCSR behaviour**

- Assume the initial value is 0x1F80 (OS default) and the shim must keep it.
- bink2w64 `f2e920` sets FZ and DAZ, forces round-to-nearest, and restores the saved image on
  exit. The emulated MXCSR must be a per-thread 32-bit value that stmxcsr and ldmxcsr read and
  write exactly, including the flag bits (0 to 5) and the mask bits (7 to 12). Some code
  round-trips it through memory.
- On AArch64, FPCR.FZ flushes denormal inputs and results, which is close to FTZ and DAZ together.
  That is a design note, not a verified equivalence.

**rdtsc.** The timing and profiler code needs a monotonic 64-bit counter read by rdtsc, with
edx:eax as the result. The 22 timing-helper gap routines follow the same pattern.

## What I could not determine

- Which of the 10 reachable ISA units run in practice. That needs a dynamic trace or resolving the
  indirect calls.
- Whether 0x839540 and 0x839544 are `OPENSSL_ia32cap_P`. No symbol was found, and the match rests on
  layout and the CPU-detection routine.
- Leaf values for the two wrappers (ca60, cdc0) and the composites (d490, df60). No direct caller
  was found in the image, so they may be dead or called indirectly.
- Which bits other images read from the `CPUInformation` structure filled by `GetCPUInformation`.
- The initial MXCSR, which comes from the OS.
- Whether the 7 `in`, `out` and `hlt` sites in functions with unresolved jumps are really data.
- The sink of the TSC in gap routine engine 55c420 (OPENSSL_rdtsc style). The linear chain did not
  reach its `ret`.

## Reproduce

```
.venv/bin/python tools/scout/cpu_sites.py game --out <scratchpad>/sites.json
.venv/bin/python tools/scout/cpu_q1_cpuid.py <scratchpad>/sites.json game
.venv/bin/python tools/scout/cpu_q2_isa.py <scratchpad>/sites.json game
.venv/bin/python tools/scout/cpu_q3_q4.py <scratchpad>/sites.json game
.venv/bin/python tools/scout/cpu_q5_rare.py <scratchpad>/sites.json game
.venv/bin/python tools/scout/cpu_gap_check.py game
```

`cpu_show.py <image> <function-rva> --around <rva> --window N` prints a function for checking.
