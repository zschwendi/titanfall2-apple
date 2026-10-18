# 38. Can the boot path reach what the lifter cannot translate?

2026-10-10. Reading only: no lifter, runtime or shim change, no game code
run. **One of the 114 skipped functions is called while `engine.dll` loads: its static
initializer 1,433 of 1,624 (engine+0x2dce70) calls the CPU-detection routine engine+0x55c430,
which the lifter skips for one `xgetbv` that our CPUID answers would never execute. Milestone 1
stops there unless that is dealt with.** Of the other 113: 97 are unreachable (79 crypto
variants behind capability tests, 18 MMX functions of Bink), 4 have no caller that can run, 3
are off the boot path (demo playback, save game), 1 is reached when `client.dll` starts its
sound system, with or without `-nosound`, and 8 are reachable under a condition that static
reading could not settle, three of them whatever CPUID says. `setjmp` is called on every frame
and is translated; no healthy start needs `longjmp`, with one case left open (the glyph
rasterizer).

| # | Group | Count | Verdict | Condition, and how sure |
|---|---|---|---|---|
| 1a | `engine`: the routine that fills the capability words (engine+0x55c430), skipped for `xgetbv` | 1 | **Reachable, at load of `engine.dll`** | Always: called from static initializer 1,433 of 1,624. Not called only if the environment variable `OPENSSL_ia32cap` is set to a plain number. *Read from the code.* |
| 1b | `engine`: crypto variants (AES-NI, CLMUL, SHA, AVX, AVX2) entered only behind a test of the capability words | 79 | **Unreachable with our CPUID** | Every entry from translated code is behind a test of a bit that is 0 under our CPUID answers, provided the words hold what 1a would compute (word 0 = 0x478bf7fd, words 1 and 2 = 0). *Read from the code.* |
| 1c | `engine`: functions that test the capability bit *inside* themselves, so the whole function is skipped: two table lookups of the P-256 curve (+0x59a5e0, +0x59a740), one binary-field multiply (+0x59e5d0) | 3 | **Reachable whatever CPUID says; whether a start gets there: not established** | The first P-256 scalar multiplication (a key exchange or signature check on that curve); binary-field curve arithmetic. Path *read from the code*; the trigger is not. |
| 2 | `setjmp` / `longjmp`: a private copy of each in 7 modules; no import | 14 routines, all translated | `setjmp`: reached, not a stop. `longjmp`: **not needed by a healthy start, one case not established** | `setjmp` runs twice per frame in `engine`, per script compiled in `client`/`server`, per font opened in `vgui2`/`vguimatsurface`. `longjmp` is error recovery everywhere except the glyph rasterizer of `vgui2` and `vguimatsurface`, where it is normal operation when a glyph overflows a fixed cell pool. |
| 3 | `bink2w64`: MMX functions (three sections of software colour converters) | 18 | **Unreachable** | Reached only from the exports `BinkCopyToBuffer` and `BinkCopyToBufferRect`; none of the 20 modules imports either or holds either name. Loading the DLL and opening a video do not touch them. *Read from the code.* |
| 4a | indirect jump: `mileswin64`+0xa720 (speaker layout by channel count, in driver creation) | 1 | **Reachable, when `client.dll` starts its sound system** | `-nosound` does not avoid it: it selects a null output and the driver is still created. *Read from the code* up to the client's interface; that the engine calls that slot during start-up is *inferred*. |
| 4b | indirect jump, reachable under a condition: `engine`+0x2526d0 (state change of an HTTP transfer), `engine`+0xf130 (per-frame tick of a back-end session object), `materialsystem_dx11`+0x148370 (in loading a `.vtf` texture), `client`+0x25bb90 (create a material proxy by name), `mileswin64`+0x14ab0 (in the mixer) | 5 | **Not established** | First HTTP transfer; a session object active at a frame; first `.vtf` texture; first material with a proxy; first mixed voice of one kind. None is called from a static initializer. |
| 4c | indirect jump, off the boot path: `engine`+0x5c0b0, +0x5c100 (demo actions), `vphysics`+0x11c5b0 (per-type save of a physics object) | 3 | **Not reachable by a start** | Demo playback with an actions file or the demo editor; writing a save game. *Inferred* from names in the binary (demo) and from shape (save). |
| 4d | indirect jump, no caller that can run: `client`+0x282e70, `client`+0x306540 (no reference of any kind), `engine`+0xfdeb0, +0xfe050 (only reader of their tables is itself unreferenced) | 4 | **Unreachable** | *Read from the code*: no direct branch, no address taken, no pointer in data. |
| 5 | anything else in the reports of the 20 modules | 0 skipped | nothing beyond 1, 3 and 4 | 164,093 functions, 114 skipped. Hidden inside "1": group 1a is not an AVX or AES function. Not skips, but worth knowing: 247 functions translated without some bytes of their unwind range (section 5). |

## What a run should log to settle each "not established"

Every one of these is a stop that names itself (`FATAL call to <module>+<rva>, a function the
lifter skipped ... called from ...`), so the first run that gets there settles it. To know
before the stop:

| Open case | Log |
|---|---|
| 1c, P-256 | the first call of engine+0x33e230 (the curve's point multiply; it calls +0x59a740 and, through +0x33f5c0, +0x59a5e0), with the guest stack |
| 1c, binary field | the first call of engine+0x378e80 |
| 2, rasterizer | calls of the restore copies vguimatsurface+0x1c20f0 and vgui2+0xcfe40 with the return address: +0x1b7c2d / +0xce92d is the rasterizer (pool full), +0x17e1f5 / +0x94ef5 is a font table rejected. Today the run would stop one step later, at the unimplemented import `RtlUnwindEx`. |
| 4b, HTTP | the first call of engine+0x250c00 (adds a transfer; it calls +0x2526d0 at once) and the URL handed to engine+0x170df0 |
| 4b, session tick | at the first frame: the byte at engine+0x8ad705 and the first byte of each object in the array at engine+0x13002520 (the loop in engine+0xe560 calls +0xf130 for each object whose byte is set) |
| 4b, texture | the first call of materialsystem_dx11+0x7ae30 and of `CTexture` slot 43 (+0x7b600), with the file name |
| 4b, proxy | the first call through slot 0 of `CMaterialProxyDict` (client+0x25bb90), with the proxy name |
| 4b, mixer | the first call of mileswin64+0x14ee0 that takes the branch at +0x15409 |
| 4a | nothing to settle; expect the stop when `client.dll` is initialised |

## Method

All scripts are in `tools/scout/` and read the DLLs and the lifter's reports only
(`OriginSDK.dll` and `d3dcompiler_46.dll` are refused by name in `reach_lib.py`).

- The list of skipped functions is the lifter's own, from a census made with the lifter of
  this branch (`tools/census/lift_census.sh`, reports only). The stored reports could
  not serve: 18 of its 20 reports predate the current lifter (its `engine` report has 27,583 functions;
  the lifter now finds 29,262).
- A reference to a function is a direct `call`/`jmp`/`jcc`, a `lea` of its address, or an
  8-byte pointer in a data section (`reach_lib.Mod.refs`). Direct references come from one
  linear decode per module (`xref_lib`), and are kept only if the lifter itself decoded an
  instruction at that address in a translated function (`<module>.cfg.json`); that removes
  constant tables read as code. For "no reference at all" the raw bytes were searched as well
  (rel32 at any offset, 8-byte pointers, aligned 4-byte RVAs).
- "Unreachable" means: no such reference from code that can run. An indirect call can only go
  where an address exists, so a function with no `lea` and no pointer in data cannot be its
  target. It does not cover an address computed arithmetically; none was seen.
- Names of libraries below (OpenSSL, libcurl, qhull, FreeType, Squirrel) come from strings in
  the DLLs where one exists and otherwise from the shape of the code against the public
  sources; they are guesses at meaning and are marked. No verdict rests on a name.
- Our CPUID answers are those of `runtime/core/machine.c` (`cpuid_answer`): vendor
  `GenuineIntel`, highest leaf 7, leaf 1 ECX = 0, leaf 1 EDX = 0x078bf3fd (MMX, FXSR, SSE, SSE2
  set; HTT clear), leaf 7 all 0.

## 1. The 83 functions of `engine.dll`

`reach_isa.py --units`: the 83 fall into 63 code units (a unit is one stretch of skipped code;
the lifter cuts some stretches into two functions, and 36 unwind ranges start 13 bytes after
the real entry, behind three register saves; two more start 8 bytes after it). 80 are skipped only for instructions of an
extension, 2 also contain `movbe`, and one, the detection routine, is skipped for `xgetbv`
alone.

### 1a. The detection routine, engine+0x55c430: reached at load

- engine+0x55c430 (0x1a1 bytes, no unwind entry, found by the lifter through its callers)
  runs `cpuid` for leaves 0, 1, 4, 7 (and three extended leaves on the non-Intel path), edits
  the leaf-1 words and returns them; it writes leaf 7 EBX to the third word itself. At
  +0x55c598 it tests bit 27 of the edited ECX word (OSXSAVE) and skips to +0x55c5ac when it is
  clear; the `xgetbv` at +0x55c5a1 runs only when it is set. Under our CPUID it is clear, so
  the original would not execute the instruction the function is skipped for.
- Its only callers are three sites in engine+0x2dce70. That function looks up the environment
  variable whose name is the string `OPENSSL_ia32cap` (engine+0x51d1b8, taken to be the C
  runtime's `getenv`: *inferred*). Variable absent: `call` at +0x2dcf93, unconditional.
  Variable starting with `~`: `call` at +0x2dcf12. Variable starting with `:`: `call` at
  +0x2dcf30. Any other value: no call; the words come from the variable. It then stores the
  low half of the result with bit 10 set to word 0 and the high half to word 1. A guard
  variable at the top makes it run once.
- engine+0x2dce70's address is at engine+0x5c1c98, entry 1,433 (from 0) of the 1,624 in the
  table [engine+0x5befc8, +0x5c2290). Both bounds of that table are loaded in engine+0x527f64,
  three direct calls from the DLL's entry point (+0x525f44): it is the table of static
  initializers the C start-up walks at load (`reach_ctors.py engine 0x2dce70 --walk`). It is
  also called from engine+0x2cbc80, which is moot.
- **So a load of `engine.dll` stops at initializer 1,433.** No static initializer of any of the
  20 modules reaches another skipped function within two direct calls (`reach_ctors.py
  <module> --walk`, run for the six modules that have skips).
- The capability words are at engine+0x14c85850 (four dwords, zero-initialised data): read
  from the argument of the three calls. Finding 16 guessed engine+0x839540/+0x839544; those
  are written by engine+0x51f8c0, a different routine. **This closes that open item.**
- What the words would be under our CPUID, tracing the routine by hand: Intel path; leaf 4
  gives 0 cores; leaf 7 EBX = 0; EDX of leaf 1 loses bits 20 and 30, gains bit 30 (Intel),
  family is 6 so bit 20 stays clear, HTT is clear so the thread-count edits are skipped;
  ECX = 0. Result: **word 0 = 0x478bf7fd** (with bit 10 from the caller), **word 1 = 0,
  word 2 = 0**. Two ways lead there and choosing is not this study's: the lifter learns
  `xgetbv` (the function then lifts; the instruction itself is never executed with OSXSAVE
  clear), or the run's environment carries the variable.

### 1b. 79 functions entered only behind a test: unreachable with our CPUID

`reach_isa.py --edges` lists every reference into a unit from translated code; `--guards` and
`--slice=LO:HI` show the tests. All entries, by what the unit is (my words; library names are
a guess from the strings finding 16 quotes):

| Units (lifter functions) | What | Entered from | Guard, read from the code | Under our words |
|---|---|---|---|---|
| 1, 2, 3 (6) | SHA-256 block, three variants | engine+0x55c70d, `jcc` at +0x55c72f, +0x55c743, +0x55c761 | word 2 bit 29; word 2 & 0x128 == 0x128; (word 0 bit 30 with word 1 & 0x10000200) == 0x50000200 | all false |
| 37, 38, 39 (6) | SHA-1 block, three variants | engine+0x573b0d | first word 1 bit 9 (`je` to the plain code), then word 2 bit 29; word 2 & 0x128; word 0 bit 30 with word 1 bit 28 | leaves at the first test |
| 53, 54, 55 (6) | SHA-512 block, three variants | engine+0x58f80d | word 1 bit 11; word 2 & 0x128 == 0x128; as SHA-256's third | all false |
| 40, 41, 42 and 50, 51, 52 (12) | SHA-1 and SHA-256 over several buffers at once | engine+0x57890d, +0x584f0d | word 2 bit 29 (`bt` 61 of words 1:2); word 1 bit 28. Units 42 and 52 are entered only from 41 and 51. | false; and the two entry functions are themselves called only from 43's callers |
| 4 to 24 (23) | AES-NI: single block, the block modes, the two key schedules | ten functions that test nothing: engine+0x2f8c80, +0x2f91f0, +0x2f93e0, +0x2fa300, +0x2fa4d0, +0x2fa510, +0x2facc0, +0x2faee0, +0x2fc840, +0x2fcad0 | those ten, and +0x2fb8b0 and +0x2fd590, are referenced only as pointers in 33 method tables. Each table's address is loaded by exactly one function and by nothing else (checked for all): 31 of the shape `test word 1, 0x2000000 ; lea rcx, table ; lea rax, plain table ; cmovne rax, rcx`, 2 that return no table when the bit is clear | bit 25 of word 1 is 0: the plain tables are chosen |
| 25 to 28 (4) | AES counter mode with the authentication hash, stitched | engine+0x2f8e40 and its tail +0x2fa9e0: `call` at +0x2f8f7c, +0x2f904e, +0x2faaf4, +0x2fabd7 | +0x2f8e40 is in three AES-NI tables **and in their three plain alternatives**, so it runs without AES-NI. Each of the four calls is behind two pointer comparisons: the context's counter function must be unit 19's entry (+0x56ae40) and its hash function unit 33's entry (+0x570240). The first address is stored only by +0x2f8c80 and +0x2fa300 (AES-NI tables only), the second only by +0x347b20 behind the test of the next row | both comparisons fail |
| 29 to 33 (5) | the authentication hash by carry-less multiply | engine+0x347b20: `call` +0x347baf, +0x347bcb; `lea` +0x347bb4, +0x347bc2, +0x347bd0, +0x347bde (function pointers kept in the context; the entries of units 31 and 33 are 8 bytes before their unwind ranges) | `test word 0, 0x1000000 ; je` and word 1 bit 1 `; je`, both to +0x347be7 | word 1 bit 1 (PCLMULQDQ) is 0 |
| 34, 35, 36 (3) | AES-CBC with SHA-1, stitched | engine+0x570e00, which ends in an unconditional `jmp` into unit 34 | its only caller is engine+0x2faee0, behind a method table | false |
| 47, 48, 49 (3) | AES-CBC with SHA-256, stitched | engine+0x582000; with rcx = 0 it returns before any variant | callers: engine+0x2fcad0 (behind a table) and the two table loaders +0x2fdc10, +0x2fdc70, which call it with rcx = 0 and only after the bit-25 test | false |
| 43 to 46 (6) | AES-CBC over several buffers at once | engine+0x2fc010, +0x2fddd0 | called from +0x2fb8b0 and +0x2fd590 (behind tables), which also test word 2 bit 5 | false |
| 56 to 60 (5) | 1024-bit modular exponentiation in AVX2 | engine+0x36eed0 (85 call sites), no test | its only caller engine+0x335dc0 calls engine+0x597780 first and skips on 0; +0x597780 returns bit 5 of word 2 | false |

Units with no reference from translated code at all are counted here too (they are helpers
entered from other units, or variants nothing selects). One referrer in the raw list is not
code: engine+0x5732c0 is a constant table that the lifter took for a function because its
address is taken, and four more "functions" (+0x573245 to +0x573251) hang off its mis-decoded
jumps. Nothing calls them.

**Finding 16's ten open code units**, by its labels:

| Unit | Is | Now |
|---|---|---|
| `f56d330`, `f56d3a0` | the two AES-NI key schedules (units 23, 24) | closed: unreachable, bit 25 of word 1 |
| `g5699dd` | the unwind-less AES-NI routines (units 4 to 15) | closed: unreachable, same guard |
| `g56d845`, `g56e46a` | the two stitched counter-mode inner loops (units 25, 27) | closed: unreachable, same guard |
| `f56f650`, `f570060` | set-up of the authentication hash (units 29, 32) | closed: unreachable, word 1 bit 1 |
| `g596f4a` | two AVX2 helpers of the modular exponentiation (units 58, 59) | closed: unreachable, word 2 bit 5 |
| `g563fcd` | 7 KB of constant tables; its one "AVX instruction" is data read as code | closed: not code |
| `g599940` | the P-256 lookups (unit 61) | **reachable whatever CPUID says** (1c); whether a start gets there stays open |

The "relocated dispatch tables with no cpuid-dependent branch" that finding 16 saw are the
method tables: the branch is in the function that picks the table, one level above.

### 1c. Three functions whose guard is inside the skipped function

- engine+0x59a5e0 and +0x59a740 start with `mov eax, word 2 ; test eax, 0x20 ; jne` to an AVX2
  body (+0x59a880, +0x59a9e0) and otherwise fall into a plain SSE2 body. The lifter's function
  holds both bodies and is skipped for the AVX2 one. Under our CPUID the original runs the SSE2
  body; the lifted program stops at the call. Callers: engine+0x33e230 (two calls of +0x59a740)
  and engine+0x33f5c0 (three of +0x59a5e0, itself called from +0x33e230); neither tests
  anything. engine+0x33e230 is slot +0xe8 of the method table at engine+0x646060, returned by
  engine+0x33ec90, whose address sits in read-only data at +0x63d8e0. *Inferred from shape:*
  the table is the P-256 curve's, the function its scalar multiply.
- engine+0x59e5d0 starts with `bt words 0:1, 33 ; jae` +0x59e640 (a separate, translated
  function) and otherwise uses `pclmulqdq`. Same effect. Caller: engine+0x378e80, reached from
  four functions of binary-field arithmetic.
- What triggers either was not traced to the engine's own code: the path up from the curve
  table goes through data.

## 2. `setjmp` and `longjmp`

`reach_setjmp.py --callers`. No module imports either by name. Seven modules carry a private
copy of the C runtime's pair, found by what the routines must do (store or load MXCSR and the
x87 control word at offsets 0x58 and 0x5c of the buffer). All 14 are translated (the lifter's
report: `lifted`, one tail jump through a register each). Every caller passes a frame address
as second argument (`mov rdx, rsp` or `lea rdx, [rbp - 0x20]`), and each restore copy, when the
buffer's first field is not zero, calls the import `RtlUnwindEx` (engine: through the thunk at
+0x55b3e8), which has no shim. So **a `setjmp` is not a stop; a `longjmp` taken is one today**,
at `RtlUnwindEx`.

| Module | save / restore | `setjmp` called from | `longjmp` called from | For | On a healthy start |
|---|---|---|---|---|---|
| `engine` | +0x5238b0 / +0x5237b0 | +0x1591d0 (the frame function: strings `RunFrameServer`, `frame_end`) and +0x16db00, three direct calls above it | +0x155ae0 (string `Host_EndGame: %s`, 11 callers) and +0x155c30 (`Host_Error: %s`, 32 callers) | abandon the current frame on an error or a disconnect | `setjmp` twice per frame, from the first frame of the main loop; `longjmp` not (*inferred*: errors and disconnects) |
| `client`, `server` (same code, shifted) | client +0x857340 / +0x80ad70 | +0x4b1b0, +0x4ba00 (script compiler entries: strings such as `end of statement expected`, `Unmatched #IF in this file`), +0x4b920 (callers mention `typecompiler`), +0x87cc0 (regular-expression compiler: `unexpected character`) | +0x4ac00 (the compiler's error exit), seven functions +0x86980 to +0x86fa0 and +0x87cc0 (regular-expression errors: `expected paren`, `invalid range` ...) | compile error in a script or a pattern | `setjmp` at every script compiled (client start-up compiles scripts: *inferred*); `longjmp` only on bad input |
| `vguimatsurface`, `vgui2` (same library in both) | vguimatsurface +0x1c2210 / +0x1c20f0; vgui2 +0xcff60 / +0xcfe40 | (a) +0x18c580 / +0xa3280, before an indirect call through slot +0x58; (b) +0x1b7b30 / +0xce830 | (a) +0x17e1e0 / +0x94ee0: stores an error code and jumps, 9 callers; (b) +0x1b7ba0 / +0xce8a0: jumps when a count reaches a limit (`cmp rdx, [r10+0x38] ; jge`) | (a) validating a table of a font when it is opened; (b) the anti-aliased glyph rasterizer running out of its fixed pool of cells, after which its caller renders a smaller band. *Guess: FreeType; no string names it.* | (a) `setjmp` per font table, `longjmp` only on a bad font. **(b) `longjmp` is normal operation** for a glyph that needs more cells than the pool has: not established whether a start draws one |
| `vphysics` | +0x150e90 / +0x1509a0 | +0x5bd80 (`qh_new_qhull: build new Qhull ...`), +0x2e810 (`qhull precision error: %d attempts ...`) | +0x5b9a0 (error exit, 78 callers), +0x30190 (`qhull restart because of %s`), +0x2ef90 | convex hull construction: error exit, and restart with perturbed input on a precision problem | not on the boot path (*inferred*: hulls are built from point sets at run time) |
| `vstdlib` | +0x303c0 / +0x30260 | +0x6f30, +0x7f40 (`Coroutine_YieldToMain`) | +0x64e0, +0x6f30, +0x7f40 | the coroutine exports | never: none of the 20 modules imports a `Coroutine_*` function (re-checked) |

Distance between a `setjmp` and its `longjmp`, shortest chain of direct calls (the real jump
is usually deeper), and whether it leaves the module:

| Case | Shortest | Crosses modules |
|---|---|---|
| rasterizer (b) | 2 frames (+0x1b7b30 → +0x1b7db0 → +0x1b7ba0); 4 through the outline walker +0x17a180 and its callback table at +0x20fff0 | no |
| font table (a) | through one indirect call, then 1 | no |
| script compiler | 2 (+0x4b1b0 → +0x4ac50 → +0x4ac00); the parser recurses, so tens in practice | no |
| `engine` frame → error | 3 at least; the 32 callers of the error function sit under network and entity parsing | the buffer and both ends are in `engine`; frames of `client` or `server` can lie between when they call back into the engine (*inferred*, not traced) |
| hull construction | 2 at least | no |

**Does the boot path need a `longjmp`?** Not for a start in which nothing fails, with the
rasterizer as the one case that is not an error path. The smallest case is that one: C code,
one module, the jump lands in the function that called `setjmp` two to five frames up, no
frame between has a handler or anything to destroy, and the only thing between that is not a
direct call is one callback. What it would have to do is return a second time from the
`setjmp` call at vguimatsurface+0x1b7b4c (vgui2+0xce84c) with the callee-saved registers and
`rsp` of that moment. Nothing is designed here.

## 3. The 18 MMX functions of `bink2w64.dll`

- They are all the code of three sections named `BINK16` (4 functions), `BINK32` (6) and
  `BINKP8` (8), outside the unwind table: converters from decoded planes to packed pixels.
- Their addresses are taken (`lea`) only in six functions, bink2w64+0x3d6e0, +0x3d920,
  +0x3da40, +0x3dc10, +0x3dd00, +0x3ddb0. Each tests bit 0 of the feature word at
  bink2w64+0x641f0 first. That word is filled by +0x13380 from CPUID leaf 1: bit 0 from EDX
  bit 23 (MMX), 4 from bit 25, 8 from bit 26, 0x10 from ECX bit 0, 0x20 from ECX bit 9. **Our
  CPUID reports MMX, so the guard would let them in**; it is not what keeps them out.
- The six are pointers in the table at bink2w64+0x5eef0. Its only reader is +0x34ec0 (it walks
  the table and calls each entry until one accepts), whose only caller is +0xa0d0, the export
  `BinkCopyToBufferRect`, called also from the export `BinkCopyToBuffer` (+0xa080).
- Of the 20 modules only `engine` imports from `bink2w64`: 22 functions (`BinkOpen`,
  `BinkDoFrame`, `BinkRegisterFrameBuffers`, `BinkGetFrameBuffersInfo` ...), not these two, and
  no other module contains the string `BinkCopyToBuffer`. The engine takes the decoded planes
  itself.
- No entry of bink2w64's start-up tables reaches them. **Unreachable**, *read from the code*.

## 4. The 13 functions skipped for an indirect jump

`reach_up.py --ijmp CENSUS --depth 5 --strings`, then each call site read.

| Function | Class in the hand-off | What it is | Who calls it | A start reaches it? |
|---|---|---|---|---|
| mileswin64+0xa720 | G | writes a speaker layout, switch on the second argument | +0xad30 at +0xaf30 after `cmp edx, 8 ; ja` (the bound is in the caller), on the success path of driver creation; +0xad30 ← +0xb160 ← export `MilesDriverCreate` | **Yes, when `client.dll` starts sound.** client+0x57cee0 calls `MilesDriverCreate` at +0x57d0eb on every path; with `-nosound` it only takes `MilesOutputGetNull` as the output first (+0x57d01b). Up: +0x57c410 (`MilesStartup`) ← ... ← slot 3 of `CHLClient`, and the command `miles_reboot`. Only `client` imports the driver functions. |
| mileswin64+0x14ab0 | G | reads samples, switch on the third argument | +0x14ee0 at +0x15426, when a byte is 1 and `r8 <= 8`; up to export `MilesServiceDrivers` | in the mixer, once something of that kind plays: *not established* |
| engine+0x2526d0 | I | sets the state of a transfer and tail-calls the state's entry function from a read-only table (+0x61c030) | 39 sites in four functions of a transfer state machine (strings `Connection timed out after %ld milliseconds`, `easy handle already used in multi handle`); above them engine+0x170df0 (`Respawn HTTPS/1.0`), pumped from a function with `chatserver:Net_FrameLoop`. *Guess: libcurl's multi interface.* | at the first HTTP transfer; the adding of a transfer calls it at once. Whether a start makes a request before or in the first frames: *not established* |
| engine+0xf130 | I | per-object tick: after a delay, tail-calls the handler of the object's state from a table in writable data (+0x75f990, empty in the file) | engine+0xe560 at +0xea5f, in a loop over the objects at +0x13002520, for each whose first byte is set, if the byte at +0x8ad705 is set; +0xe560 (strings `securityToken`, `3pToken`, `language`) ← +0x15a090 (`test bl, bl ; je` around the call) ← the frame function +0x1591d0. Its two other callers, +0xfb50 and +0xfbc0, have no reference of any kind. | not before the main loop (*read from the code*); in a frame when a session object is active: *not established* |
| materialsystem_dx11+0x148370 | H | maps two small enumerations to a constant, switch on the second | +0x7ae30 at +0x7b000 (after a `.vtf` file opened) and `CTexture` slot 43 (+0x7b600) at +0x7b6d0; +0x7ae30 ← +0x7bed0 ← `CTexture` slot 17 | at the first texture loaded from a `.vtf` file, so after a device exists: *not established* for a start |
| client+0x25bb90 | I | looks a name up and tail-calls the factory found | slot 0 of `CMaterialProxyDict`: an indirect call from the material system | at the first material that names a proxy: *not established* |
| engine+0x5c0b0, +0x5c100 | I | call a factory by type from a table of 32-byte records in writable data (+0x7645f0); string `CBaseDemoAction::AddFactory: Bogus factory type` | `CDemoActionManager` slot 15 (strings `.vdm`, `factory`) and `CDemoEditorPanel` slot 98 | no: demo playback with an actions file, or the demo editor (*inferred* from the names) |
| vphysics+0x11c5b0 | I | calls one virtual, then tail-calls a handler by the type field of its argument (10 types, table +0x1cf080) | slot 21 of a 51-slot virtual table at vphysics+0x198e10 (no RTTI), installed by +0xee590 and +0xef7b0 | no (*inferred* from shape: the per-type writer of a physics object into a save) |
| engine+0xfdeb0, +0xfe050 | G | write runs of 16-bit indices, switch on a count | only from the tables at engine+0x5f4650 and +0x5f4560, whose one reader, the 3-instruction dispatcher engine+0xfc470, has no reference of any kind and is not even in the lifter's list | **unreachable** |
| client+0x282e70, +0x306540 | I | a lookup-and-call; a call of a member through offsets | none: no direct branch, no `lea`, no pointer, no RVA outside the unwind table | **unreachable** |

## 5. Anything else the tables hide

`reach_skips.py CENSUS` over the 20 modules: 164,093 functions, 163,979 translated, 114 skipped;
14 modules and `server` have none.

| Module | Skipped | By reason |
|---|---|---|
| `engine` | 89 | 80 extension only; 2 extension and `movbe`; 1 `xgetbv`; 6 indirect jump |
| `bink2w64` | 18 | MMX forms |
| `client` | 3 | indirect jump |
| `mileswin64` | 2 | indirect jump |
| `vphysics`, `materialsystem_dx11` | 1 each | indirect jump |

Nothing exists beyond groups 1, 3 and 4. What the three counts do not say:

1. **"83 for AVX and AES" contains the one function that matters most** (1a), and it is
   skipped for neither.
2. **A skip can hide a path that needs no extension** (1c): when the test and both bodies are
   in one function, the plain body goes with the other.
3. **247 functions are `lifted-without-handlers`** (22,505 bytes of their unwind ranges were
   never decoded): by size, 160 have at most 8 such bytes, 65 have 9 to 64, 22 more. These are
   bytes only an exception handler or a table can reach (finding 28's subject), and they are
   not stops until an exception wants them. Four of the 22 are not real: the mis-decoded table
   of 1b accounts for 17,916 of `engine`'s 18,776 bytes.
4. **Code the lifter never found** has no row at all; a call would be a dispatch miss, not a
   skip stop. Two were met, both with no reference of any kind: engine+0xfc470 and
   engine+0x2dcfc0 (a second function that writes capability word 1).
5. The stops *inside* translated functions (a `switch` default, `int3`, `ud2`, an alignment or
   divide fault) are the original's own traps or loud by construction, and are not counted by
   any table.

## What was not established

- Whether a start performs a P-256 multiplication, an HTTP transfer, a session tick, a `.vtf`
  load, a material-proxy creation or a pool-overflowing glyph before or in the first frames.
  The logging table above says what settles each.
- That slot 3 of `CHLClient` runs during the engine's start-up (4a rests on it).
- The conditions on the way from a frame down to `Host_Error` or `Host_EndGame`.
- Group 3's and 4d's "no importer" covers the 20 boot-path modules (and three more DLLs of the
  install), not the launcher executable or `OriginSDK.dll`, which are never read.

## Reproduce

    C=<scratch>/census ; export SCOUT_CACHE=<scratch>/xref
    nice tools/census/lift_census.sh $C                       # reports only; about 8 minutes
    .venv/bin/python tools/scout/reach_skips.py $C --list
    .venv/bin/python tools/scout/reach_isa.py $C --units --edges --guards --capreaders
    .venv/bin/python tools/scout/reach_isa.py $C --slice=55c70d:55c770
    TF2_CENSUS_DIR=$C .venv/bin/python tools/scout/reach_ctors.py engine 0x2dce70 --walk
    .venv/bin/python tools/scout/reach_setjmp.py --callers
    .venv/bin/python tools/scout/reach_up.py --ijmp $C --depth 5 --strings
    .venv/bin/python tools/scout/reach_up.py engine 0x170df0 --depth 4 --strings

Measured in this study: every count above comes from one of these commands; the census was
made with the lifter of commit `a7c64d7` (last change under `lifter/`: `46fa5b7`).
