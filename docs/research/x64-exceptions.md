# How 64-bit Windows exceptions work: a reference for the runtime and the lifter

Written from public documentation and public source only. No game file was used; numbers about
the game come from this repository's own analysis and are stated as such. No code is copied: where an open implementation is cited,
what it does is said in words, with the file and the function.

The context is the question of structured and C++ exceptions. A static analysis and one Proton run
show that a normal start raises no exception from game code, and RUNTIME.md 9.3 chose
"abort with a log" (plan A). This file is what a designer needs the day plan B is built: what the
tables in the DLLs say, what Windows does with them, what a translation that keeps the guest
stack and registers in memory must keep true, and ten programs to test it with.

Contents by count: part 1, 27 numbered statements; part 2, 24; part 3, 10; part 4, 30; part 5,
16 requirements (X1–X16) and 4 implementations (3 read, 1 not reached); part 6, 10 test programs;
10 unsettled points listed at the end.

## How to read the labels

Every statement ends with a label and a source key:

- **[D]** documented by Microsoft (or, for a compiler fact, by the compiler's public documentation);
- **[O]** read in the source of an open implementation (Wine, LLVM, FEX, box64, XenonRecomp).
  **[O-test]** is a Wine conformance test: the test asserts it of Windows, so it is evidence of
  what Windows does, not only of what Wine does;
- **[N]** not settled: what was looked at and what would settle it is said;

Part 5's requirements are reasoning from the sourced statements; each names the statements it
rests on.

### Sources (keys used below)

| Key | What | Where |
|---|---|---|
| MS-X64EH | "x64 exception handling" (page dated 2019-10-14) | learn.microsoft.com/cpp/build/exception-handling-x64 (source: MicrosoftDocs/cpp-docs `docs/build/exception-handling-x64.md`) |
| MS-PROLOG | "x64 prolog and epilog" | learn.microsoft.com/cpp/build/prolog-and-epilog |
| MS-V3 | "Unwind Information V3" preview specification | learn.microsoft.com/cpp/build/x64-unwind-information-v3 |
| MS-PE | "PE Format", data directory and "The .pdata Section" | learn.microsoft.com/windows/win32/debug/pe-format |
| MS-DISPATCH | "Exception Dispatching" | learn.microsoft.com/windows/win32/debug/exception-dispatching |
| MS-VEH | "Vectored Exception Handling", `AddVectoredExceptionHandler` | learn.microsoft.com/windows/win32/debug/vectored-exception-handling, …/api/errhandlingapi/nf-errhandlingapi-addvectoredexceptionhandler |
| MS-UEF | `SetUnhandledExceptionFilter`, `UnhandledExceptionFilter` | learn.microsoft.com/windows/win32/api/errhandlingapi/… |
| MS-RAISE | `RaiseException` | learn.microsoft.com/windows/win32/api/errhandlingapi/nf-errhandlingapi-raiseexception |
| MS-RTL | `RtlUnwindEx`, `RtlVirtualUnwind`, `RtlLookupFunctionEntry`, `EXCEPTION_RECORD` | learn.microsoft.com/windows/win32/api/winnt/… |
| MS-TIMING | "Timing of exception handling: A summary" | learn.microsoft.com/cpp/cpp/timing-of-exception-handling-a-summary |
| MS-TRY | `try-except` and `try-finally` statements | learn.microsoft.com/cpp/cpp/try-except-statement, …/try-finally-statement |
| MS-EH | `/EH` (exception handling model) | learn.microsoft.com/cpp/build/reference/eh-exception-handling-model |
| MS-MIX | "Mixing C (structured) and C++ exceptions" | learn.microsoft.com/cpp/cpp/mixing-c-structured-and-cpp-exceptions |
| MS-SPEC | "Exception specifications (throw, noexcept)" | learn.microsoft.com/cpp/cpp/exception-specifications-throw-cpp |
| MS-SET | `_set_se_translator` | learn.microsoft.com/cpp/c-runtime-library/reference/set-se-translator |
| MS-LONGJMP | `longjmp` | learn.microsoft.com/cpp/c-runtime-library/reference/longjmp |
| MS-FH4 | "Making C++ Exception Handling Smaller On x64", C++ team blog, 2019-03-07, with its later updates | devblogs.microsoft.com/cppblog/making-cpp-exception-handling-smaller-x64 (read through a search summary; the page itself was not reachable from here) |
| W-UNW | Wine `dlls/ntdll/unwind.c`, x86_64 part: `RtlVirtualUnwind2`, `is_inside_epilog`, `interpret_epilog`, `find_function_info`, `__C_specific_handler`, `RtlLookupFunctionEntry`, `RtlUnwind` | github.com/wine-mirror/wine, master as fetched 2026-10-11 |
| W-SIG | Wine `dlls/ntdll/signal_x86_64.c`: `virtual_unwind`, `call_seh_handlers`, `nested_exception_handler`, `unwind_exception_handler`, `RtlUnwindEx`, `RtlRestoreContext`, `call_consolidate_callback`, `RtlRaiseException`, `RtlCaptureContext`, `NTDLL__setjmpex`, `NTDLL_longjmp`, `RtlUserThreadStart` | same |
| W-EXC | Wine `dlls/ntdll/exception.c`: `dispatch_exception`, `call_vectored_handlers`, `call_unhandled_exception_handler` | same |
| W-CXX | Wine `dlls/msvcrt/except.c` (`cxx_frame_handler`, `find_catch_block`, `call_catch_block`, `cxx_local_unwind`, `ip_to_state`, `find_catch_handler`), `except_x86_64.c` (`call_exc_handler`, `longjmp`), `cppexcept.h`, `cxx.h`, `cpp.c` (`_CxxThrowException`), `exception_ptr.c`, `handler4.c` | same |
| W-H | Wine `include/winnt.h`, `include/excpt.h`, `include/ntstatus.h`, `include/msvcrt/setjmp.h` (transcriptions of the SDK's public declarations) | same |
| W-T-EXC | Wine `dlls/ntdll/tests/exception.c`, x86_64 section: `test___C_specific_handler`, `test_nested_exception` (handlers `nested_exception_handler`, `nested_exception_handler2`), `test_collided_unwind` (`collided_exception_handler`), `test_restore_context` (`test_consolidate_dummy`) | same |
| W-T-UNW | Wine `dlls/ntdll/tests/unwind.c`: `test_virtual_unwind_x86` (the x86_64 cases) | same |
| W-EC | Wine `dlls/ntdll/signal_arm64ec.c`: `prepare_exception_arm64ec`, the `ResetToConsistentState` hook | same |
| L-WEH | LLVM `llvm/lib/CodeGen/AsmPrinter/WinException.cpp`: `emitCSpecificHandlerTable`, `emitCXXFrameHandler3Table`, `computeIP2StateTable`, the CLR clause comment | github.com/llvm/llvm-project, main as fetched 2026-10-11 |
| L-MC | LLVM `llvm/lib/MC/MCWin64EH.cpp` (version 2 epilog codes), `llvm/include/llvm/Support/Win64EH.h` | same |
| L-NOP | LLVM `llvm/lib/Target/X86/X86MCInstLower.cpp`, `maybeEmitNopAfterCallForWindowsEH` and its comment | same |
| L-FL | LLVM `llvm/lib/Target/X86/X86FrameLowering.cpp`: funclet prologue (establisher in RDX), `adjustFrameForMsvcCxxEh` (UnwindHelp) | same |
| FEX | FEX `Source/Windows/ARM64EC/Module.cpp`: `ResetToConsistentStateImpl`, `ResetToConsistentState` | github.com/FEX-Emu/FEX, main as fetched 2026-10-11 |
| BOX64 | box64 `src/libtools/signals.c`: the SIGSEGV path (`FindDynablockFromNativeAddress`, `getX64Address`, `copyUCTXreg2Emu`, the resume by `siglongjmp`) | github.com/ptitSeb/box64, main as fetched 2026-10-11 |
| XENON | XenonRecomp `README.md`, sections "Exceptions" and "setjmp" | github.com/hedge-dev/XenonRecomp, main as fetched 2026-10-11 |

Pages that contained instructions addressed to a reader or an AI: none met.

---

## Part 1. The data in the file

### 1.1 Where the table is

1.1.1 The exception directory is entry 3 of the PE32+ optional header's data directories
("Exception Table", at optional-header offset 120 in PE32 and 136 in PE32+); it points to the
`.pdata` section, an array of function table entries sorted by function address. **[D]** MS-PE.

1.1.2 On x64 each entry is a `RUNTIME_FUNCTION` of three 32-bit fields, all image-relative
(RVAs): `BeginAddress`, `EndAddress` (exclusive), `UnwindData` (RVA of an `UNWIND_INFO`). 12
bytes, `DWORD`-aligned. **[D]** MS-X64EH, MS-PE.

1.1.3 A function needs an entry if it allocates stack or calls another function; a *leaf*
function (neither) has none, and the unwinder treats an address with no entry as a leaf whose
return address is at `[RSP]`. **[D]** MS-X64EH ("Unwind procedure", step 2).

1.1.4 Code generated at run time is registered with `RtlAddFunctionTable` or
`RtlInstallFunctionTableCallback`. **[D]** MS-X64EH. Wine keeps such tables in a separate list
searched after the loaded images' directories. **[O]** W-UNW (`RtlLookupFunctionEntry`,
`lookup_dynamic_function_table`).

1.1.5 `RtlLookupFunctionEntry(ControlPc, &ImageBase, HistoryTable)` returns the entry whose
range contains `ControlPc`, or NULL, and stores the module's base in `*ImageBase`. **[D]**
MS-RTL. Wine searches the sorted array by binary search and, when found, follows an entry
whose `UnwindData` has bit 0 set as a pointer (RVA, bit cleared) to another `RUNTIME_FUNCTION`
(an "indirect" entry), and stores 0 in `*ImageBase` when nothing is found. **[O]** W-UNW
(`find_function_info`, `RtlLookupFunctionEntry`). The indirect form is not in Microsoft's
x64 documentation. **[N]** (the project's read-only unwinder refuses it; a game DLL's table would show whether any exists).

1.1.6 The unwind history table (`UNWIND_HISTORY_TABLE`: count, four hint bytes, a low and high
address, 12 cached `{ImageBase, FunctionEntry}` pairs) is a lookup cache the caller provides.
**[O]** W-H (`winnt.h`). Microsoft documents the parameter without the layout ("see
Winternl.h"). **[D]** MS-RTL. Ignoring it changes no result. **[N]** (no source says what a
caller may rely on; the project's unwinder already ignores it).

### 1.2 `UNWIND_INFO`

1.2.1 Layout: byte 0 = `Version` (bits 0–2) and `Flags` (bits 3–7); byte 1 = `SizeOfProlog`;
byte 2 = `CountOfCodes` (number of 16-bit slots); byte 3 = `FrameRegister` (bits 0–3) and
`FrameOffset` (bits 4–7, scaled by 16); then the unwind code array; then, at the slot index
rounded up to even (`(CountOfCodes + 1) & ~1`), either the handler part or the chained entry.
4-byte aligned. **[D]** MS-X64EH (tables and "Unwind data definitions in C").

1.2.2 Flags: `UNW_FLAG_EHANDLER` 1 (an exception handler to call in the search pass),
`UNW_FLAG_UHANDLER` 2 (a termination handler to call when unwinding), `UNW_FLAG_CHAININFO` 4
(this is not the primary info; a `RUNTIME_FUNCTION` follows instead of a handler). With
`CHAININFO`, `EHANDLER` and `UHANDLER` must be clear, and the frame register and fixed
allocation must equal the primary's. `UNW_FLAG_NHANDLER` is 0. **[D]** MS-X64EH, MS-RTL
(`RtlVirtualUnwind`). Both `EHANDLER` and `UHANDLER` may be set together, naming one routine
for both passes. **[O]** W-T-EXC (`test_collided_unwind` builds such an entry), W-SIG (the
wrapper that calls unwind handlers is declared `@except, @unwind`).

1.2.3 Handler part: a 32-bit RVA of the language-specific handler, followed immediately by the
handler's data, whose format belongs to the handler. **[D]** MS-X64EH. The `HandlerData`
pointer a handler receives is the address just after that RVA. **[O]** W-UNW (`*data =
&handler_data->handler + 1`). For `__CxxFrameHandler3` that data is a 32-bit RVA of the
`FuncInfo`; for `__C_specific_handler` it is the scope table itself. **[O]** W-CXX
(`__CxxFrameHandler` reads `*(UINT *)dispatch->HandlerData`), W-UNW (`__C_specific_handler`
casts `HandlerData` to the scope table). The game's handlers read it at `[r9+0x38]`.

1.2.4 Chained part: three 32-bit fields, a copy of the parent `RUNTIME_FUNCTION`. Chains may
nest (a chained info whose parent is chained again) until an info without `CHAININFO`, the
primary, which holds the real entry point. Uses: non-contiguous code, and saving nonvolatile
registers late ("shrink-wrapping"), where the chained info has a nonzero prolog of
`UWOP_SAVE_NONVOL` codes only. **[D]** MS-X64EH. (The page's prose says "three `UWORD`s";
its own table and C definition say three `ULONG`s, which is what Wine reads. **[D]** MS-X64EH,
**[O]** W-UNW.)

1.2.5 When unwinding through a chained entry, the codes of the chained info are undone first,
then the parent's whole code array, and so on to the primary. **[D]** MS-X64EH ("Unwind
procedure", last paragraph). Wine does exactly this and reports a handler only from the
primary. **[O]** W-UNW (`RtlVirtualUnwind2`). The game has 72,048 chained entries of 200,994.

1.2.6 The frame register, if nonzero, is set to `RSP + 16 × FrameOffset` by the prolog
(offsets 0 to 240). **[D]** MS-X64EH.

1.2.7 The *establisher frame* given to a handler is "the base of the fixed stack allocation":
with a frame register it is `FrameRegister − 16 × FrameOffset`, otherwise the value of `RSP`
after the prolog. **[D]** MS-X64EH (field descriptions), **[O]** W-UNW (`RtlVirtualUnwind2`:
the frame is computed from the frame register when there is one, from `Rsp` otherwise, and is
the value `RSP` had in the body).

### 1.3 Unwind codes

1.3.1 Slot layout (16 bits): byte 0 = `CodeOffset` (offset from the function start of the end
of the prolog instruction, i.e. of the next instruction); byte 1 = `UnwindOp` (bits 0–3) and
`OpInfo` (bits 4–7). Extra slots of a code hold a 16-bit or 32-bit operand. The array is in
descending order of `CodeOffset` (reverse prolog order). Register numbers: 0 RAX, 1 RCX, 2 RDX,
3 RBX, 4 RSP, 5 RBP, 6 RSI, 7 RDI, 8–15 R8–R15. **[D]** MS-X64EH.

1.3.2 The codes, with slot count and meaning. **[D]** MS-X64EH unless marked.

| Op | Name | Slots | Effect described (undo when unwinding) |
|---|---|---|---|
| 0 | `UWOP_PUSH_NONVOL` | 1 | `push reg` (`OpInfo` = register); `RSP −= 8`. Must come first in the prolog, i.e. last in the array |
| 1 | `UWOP_ALLOC_LARGE` | 2 or 3 | `OpInfo` 0: next slot = size / 8 (up to 512K − 8); `OpInfo` 1: next two slots = size, unscaled (up to 4G − 8) |
| 2 | `UWOP_ALLOC_SMALL` | 1 | size = `OpInfo × 8 + 8` (8 to 128) |
| 3 | `UWOP_SET_FPREG` | 1 | frame register = `RSP + 16 × FrameOffset`; `OpInfo` reserved |
| 4 | `UWOP_SAVE_NONVOL` | 2 | `mov [base + off], reg`; next slot = offset / 8 |
| 5 | `UWOP_SAVE_NONVOL_FAR` | 3 | same, next two slots = offset, unscaled |
| 6 | `UWOP_EPILOG` (version 2 only) | 1 per entry | epilog location, see 1.4. Not in Microsoft's documentation; **[O]** L-MC, W-UNW |
| 7 | `UWOP_SPARE_CODE` | — | name only in LLVM (`UOP_SpareCode`); no meaning found. **[O]** L-MC; **[N]** |
| 8 | `UWOP_SAVE_XMM128` | 2 | `movaps [base + off], xmm` (`OpInfo` = xmm number); next slot = offset / 16 |
| 9 | `UWOP_SAVE_XMM128_FAR` | 3 | same, next two slots unscaled |
| 10 | `UWOP_PUSH_MACHFRAME` | 1 | a hardware frame: `OpInfo` 0 = `RIP, CS, EFLAGS, old RSP, SS` at `RSP+0..32`; `OpInfo` 1 = an error code first, so each is 8 higher. Decrements `RSP` by 40 or 48 |

1.3.3 The base of a save offset is `RSP` when there is no frame register, otherwise the frame
register minus `16 × FrameOffset`; a code taking an offset must come after the frame register
is set. **[D]** MS-X64EH.

1.3.4 Allocation must use the shortest form (small for 8–128, large/0 for 136 to 512K−8,
large/1 above). **[D]** MS-X64EH. A fixed allocation of a page or more is preceded by a call
to `__chkstk` with the size in `RAX`; `__chkstk` changes only R10, R11 and flags. **[D]**
MS-PROLOG. (The function test's stack-probe rule rests on this; see DESIGN.md section 8.)

1.3.5 Inside a prolog (`ControlPc − BeginAddress < SizeOfProlog`), only the codes whose
`CodeOffset` is at most that distance are undone; past the prolog, all are. **[D]** MS-X64EH
("Case b"). (The page says "less than or equal to the prolog size"; Wine and the test treat
the offset equal to `SizeOfProlog` as the body: the handler is reported from that offset on.
**[O]** W-UNW, **[O-test]** W-T-UNW case `results_0`, offset `0x1d` = prolog size: handler
reported.) **[N]** for the boundary as worded by Microsoft.

1.3.6 `UWOP_PUSH_MACHFRAME` sets `RIP` and `RSP` from the frame and no return address is
popped afterwards. Wine accepts it only as the last code of a non-chained info. **[O]** W-UNW.

1.3.7 Versions: version 1 is documented as current. **[D]** MS-X64EH. Version 2 adds
`UWOP_EPILOG` entries (1.4.3). **[O]** L-MC, W-UNW. Version 3 (Intel APX) is a different
payload format, documented as a preview. **[D]** MS-V3. Which MSVC release first emitted
version 2 was not found. **[N]**. The game's DLLs are VS2010–2013 (linker 9.0–12.0).
Whether any game entry is version 2 is answered by the unwinder's "every row understood" check on a
game DLL.

### 1.4 Epilogs: how an interrupted one is recognised

1.4.1 Epilogs carry no unwind data in version 1; the unwinder decides that `RIP` is in an
epilog by examining the code bytes from `RIP` on: if they match the tail of a legal epilog, the
rest of the epilog is simulated on the context and the caller's state results. No handler of
that function is called. **[D]** MS-X64EH ("Case a").

1.4.2 The legal epilog: an optional `add rsp, imm` or, with a frame register,
`lea rsp, [framereg + disp]`; then zero or more 8-byte `pop`s; then `ret` or a `jmp` (only the
`jmp` forms with ModRM `mod` = 00, i.e. an indirect jump through memory without displacement,
are allowed besides direct jumps); nothing else, nothing scheduled in between; without a frame
pointer `lea rsp, [rsp+n]` is not allowed. **[D]** MS-PROLOG.

1.4.3 What Wine accepts as "in an epilog" (and so treats as what Windows does), in words. **[O]**
W-UNW (`is_inside_epilog`, `interpret_epilog`):
- first instruction optionally `REX.W add rsp, imm32/imm8` (exactly `48 81 C4` / `48 83 C4`) or
  `REX.W lea rsp, [reg + disp8/disp32]` with no SIB byte and REX.R/X clear;
- then any number of `pop r64` (with an optional REX prefix selecting R8–R15);
- then one of: `ret`, `ret imm16`, `rep ret` (`F3 C3`), `jmp rel32` / `jmp rel8` whose target
  is **not** inside the same function (a tail call; "same function" means the same primary
  entry after following chains), `jmp [rip+disp32]` (`FF 25`), or with a REX prefix of exactly
  `REX.W`, `jmp` through a ModRM memory operand;
- the epilog check is skipped for a chained entry, and for an entry with zero unwind codes
  (Wine notes that Windows 10 1809 and later no longer treat such functions specially);
- simulating it: `add`/`lea` set `RSP`, each `pop` loads the register from `[RSP]` and adds 8,
  the final instruction pops the return address into `RIP` (`ret imm16` also adds `imm16`).

1.4.4 In version 2, the first `UWOP_EPILOG` slot gives the epilog size in its `CodeOffset` and
in `OpInfo` bit 0 says whether the last epilog ends exactly at the function's end; further
`UWOP_EPILOG` slots give each epilog's start as a distance back from the function's end (low 8
bits in `CodeOffset`, high 4 bits in `OpInfo`); all epilogs of a function have the same size; a
slot of all-zero `UWOP_EPILOG` pads. **[O]** L-MC (the emitter). Wine ignores these slots and
recognises epilogs from the code bytes for version 2 too. **[O]** W-UNW. What Windows does with
them is not documented. **[N]** (a Windows test with a version 2 entry whose code bytes are not
a legal epilog would settle it).

1.4.5 The establisher frame reported for an address inside an epilog differs across Windows
versions (before Windows 11 the frame-register value was reported even after the register was
popped). **[O-test]** W-T-UNW (`broken_results_0` against `results_0`). No handler is called
there, so this matters only to a caller of `RtlVirtualUnwind` that reads the frame. **[O]** W-UNW.

1.4.6 The handler (and its data) is returned by `RtlVirtualUnwind` only when the
`HandlerType` asked for is set in the primary's flags **and** `ControlPc` is past the prolog and
not in an epilog. **[D]** MS-X64EH (cases a–c), **[O]** W-UNW, **[O-test]** W-T-UNW (handler
flag false at offsets 0x00–0x16 and from 0x2c on, true at 0x1d–0x25).

1.4.7 `RtlVirtualUnwind(HandlerType, ImageBase, ControlPc, FunctionEntry, Context,
&HandlerData, &EstablisherFrame, ContextPointers)` updates `Context` to the caller's state,
returns the handler or NULL, and records in `ContextPointers` where each restored register was
read from. **[D]** MS-RTL (`HandlerType` values 0/1/2/4), **[O]** W-UNW. Wine returns NULL and
sets `Rip` to 0 when the data is not understood. **[O]** W-UNW.

---

## Part 2. Dispatch, step by step

### 2.1 Getting to the dispatcher

2.1.1 The system saves the thread's machine state in a `CONTEXT` (the *context record*) and a
description of the exception in an `EXCEPTION_RECORD`, then searches: debugger first chance;
then frame-based handlers from the current frame outwards; then debugger second chance; then
default handling (for most exceptions, the process ends). **[D]** MS-DISPATCH.

2.1.2 `EXCEPTION_RECORD` (x64): `ExceptionCode` (+0, 32-bit), `ExceptionFlags` (+4),
`ExceptionRecord` (+8, a chained record), `ExceptionAddress` (+0x10), `NumberParameters`
(+0x18), `ExceptionInformation[15]` (+0x20); 0x98 bytes. **[D]** MS-RTL (fields; offsets are the
fields' natural 64-bit alignment), **[O]** W-H (`EXCEPTION_MAXIMUM_PARAMETERS` 15). This
repository's crash contract already uses these offsets. 

2.1.3 Flag bits: `EXCEPTION_NONCONTINUABLE` 0x01, `EXCEPTION_UNWINDING` 0x02,
`EXCEPTION_EXIT_UNWIND` 0x04, `EXCEPTION_STACK_INVALID` 0x08, `EXCEPTION_NESTED_CALL` 0x10,
`EXCEPTION_TARGET_UNWIND` 0x20, `EXCEPTION_COLLIDED_UNWIND` 0x40,
`EXCEPTION_SOFTWARE_ORIGINATE` 0x80. **[O]** W-H. Only the first is documented with
`RaiseException`. **[D]** MS-RAISE. Windows sets 0x80 on records from `RaiseException` in newer
versions; the tests accept 0 or 0x80. **[O-test]** W-T-EXC (`nested_exception_handler`,
`collided_exception_handler`).

2.1.4 **From `RaiseException(code, flags, n, args)`**: a record is built with the code, the
flags (0 or `EXCEPTION_NONCONTINUABLE`), at most 15 arguments (`n` must not exceed
`EXCEPTION_MAXIMUM_PARAMETERS`; `args` NULL means none). **[D]** MS-RAISE. Wine's
`RtlRaiseException` captures the caller's context, sets `Rip` and `ExceptionAddress` to its
own return address and `Rsp` to the caller's stack pointer, then dispatches in the same thread
(through the kernel only when a debugger is attached). **[O]** W-SIG (`RtlRaiseException`).

2.1.5 **From a hardware fault**: the kernel builds the record and context and resumes the
thread in `KiUserExceptionDispatcher` with both on the user stack; its unwind data is a machine
frame plus the saved nonvolatile registers, so a walk from inside the dispatcher reaches the
faulting frame. **[O]** W-SIG (`KiUserExceptionDispatcher` and its `.seh_pushframe`,
`.seh_savereg` lines). For a fault, `ExceptionAddress` and `Rip` are the faulting instruction.
**[D]** MS-DISPATCH ("the address at which the exception occurred"); **[O-test]** W-T-EXC
(`run_exception_test_flags`' handler checks `Rip` against the faulting instruction's offset and
`ExceptionAddress` against `Rip`, allowing `Rip + 1` for a breakpoint).

### 2.2 The search pass (`RtlDispatchException`)

2.2.1 **Vectored handlers first.** Vectored exception handlers are called in registration order
(`AddVectoredExceptionHandler(First, …)`: nonzero First puts it at the head), after the
debugger's first chance and before the frame search. **[D]** MS-VEH. A handler returning
`EXCEPTION_CONTINUE_EXECUTION` (−1) stops the list and the thread resumes with the (possibly
edited) context; otherwise the next one is called. **[O]** W-EXC (`call_vectored_handlers`,
`dispatch_exception`).

2.2.2 **The frame walk.** Starting from a copy of the context, repeatedly:
1. `RtlLookupFunctionEntry(context.Rip)`;
2. `RtlVirtualUnwind(UNW_FLAG_EHANDLER, …)` on the copy, which moves it to the caller and gives
   the handler, its data and the establisher frame (1.4.6);
3. if the establisher frame is outside the thread's stack limits, set
   `EXCEPTION_STACK_INVALID` and stop;
4. if there is a handler, call it (2.3) with the **original** context, not the walked copy;
5. stop when the copy's `Rsp` reaches the stack base (or `Rip` is 0).

**[D]** MS-X64EH ("Unwind procedure" steps 1–5), **[O]** W-SIG (`call_seh_handlers`). The stack
limits come from the thread block (`NT_TIB.StackBase`/`StackLimit`). **[O]** W-SIG (the check's
error message prints `Tib.StackLimit` and `Tib.StackBase`; `is_valid_frame` itself is defined in a
header not read here).

2.2.3 What the handler's return value does. **[O]** W-SIG (`call_seh_handlers`); values **[O]** W-H
(`excpt.h`):

| Return | Value | Effect |
|---|---|---|
| `ExceptionContinueExecution` | 0 | if the record is noncontinuable, raise `STATUS_NONCONTINUABLE_EXCEPTION` (0xC0000025); otherwise resume the thread with the original context (which the handler may have edited) |
| `ExceptionContinueSearch` | 1 | go to the next frame |
| `ExceptionNestedException` | 2 | see 2.5 |
| `ExceptionCollidedUnwind` | 3 | see 2.6 |
| anything else | | `STATUS_INVALID_DISPOSITION` (0xC0000026) |

2.2.4 Nothing handled it: Wine raises it again to the kernel as second chance, which ends the
process; but in practice the outermost frame of every thread (`RtlUserThreadStart`, the
process's and threads' start routine) carries a handler that calls the unhandled-exception
filter. **[O]** W-SIG (`RtlUserThreadStart` with `.seh_handler call_unhandled_exception_handler`),
W-EXC (`call_unhandled_exception_handler`).

### 2.3 What a language handler is called with

2.3.1 Prototype: `EXCEPTION_DISPOSITION handler(EXCEPTION_RECORD *ExceptionRecord, ULONG64
EstablisherFrame, CONTEXT *ContextRecord, DISPATCHER_CONTEXT *DispatcherContext)`, in RCX,
RDX, R8, R9. **[D]** MS-X64EH.

2.3.2 `ContextRecord` is the context at the time of the exception in the search pass, and the
current "unwind" context in the unwind pass. **[D]** MS-X64EH. In the search pass every handler
gets the same original context; in the unwind pass, the context of the frame being unwound
(the walk's state before stepping out of that frame). **[O]** W-SIG (`call_seh_handlers` passes
`orig_context`; `RtlUnwindEx` passes `dispatch.ContextRecord`, which it keeps as the frame's
context).

2.3.3 `DISPATCHER_CONTEXT` (x64; offsets from the field sizes):

| Offset | Field | Meaning |
|---|---|---|
| 0x00 | `ControlPc` | `RIP` within this function: the exception address for the faulting frame, or the address at which control left this function (a return address) for the others |
| 0x08 | `ImageBase` | load address of the module; RVAs in the entry and the handler data are relative to it |
| 0x10 | `FunctionEntry` | the `RUNTIME_FUNCTION` for this function (as found, possibly a chained one) |
| 0x18 | `EstablisherFrame` | base of the fixed stack allocation (1.2.7) |
| 0x20 | `TargetIp` | in the unwind pass, the continuation address of the unwind; ignored when no target frame |
| 0x28 | `ContextRecord` | the dispatcher's own context record |
| 0x30 | `LanguageHandler` | the handler being called |
| 0x38 | `HandlerData` | the handler's data (1.2.3) |
| 0x40 | `HistoryTable` | the lookup cache (1.1.6) |
| 0x48 | `ScopeIndex` | index of the next scope-table entry to consider (part 3) |
| 0x4C | `Fill0` | padding; 0x50 bytes in all |

First eight fields and their meanings **[D]** MS-X64EH; the last three **[O]** W-H (`winnt.h`). That
`ControlPc` of a caller frame is the return address itself, not one less: **[D]** MS-X64EH ("the
address at which control left the establishing function"), **[O]** L-NOP (the compiler rule in 4.6.4
exists because of it), W-SIG (`virtual_unwind` sets `ControlPc = context->Rip` after the step).

### 2.4 The unwind pass (`RtlUnwindEx`)

2.4.1 `RtlUnwindEx(TargetFrame, TargetIp, ExceptionRecord, ReturnValue, ContextRecord,
HistoryTable)`: unwind the frames to `TargetFrame`, put `ReturnValue` in the integer return
register, and continue at `TargetIp`; `TargetFrame` NULL is an exit unwind (and `TargetIp` is
then ignored). **[D]** MS-RTL. `RtlUnwind(frame, ip, rec, value)` is the same with a context
buffer of its own and no history table. **[O]** W-UNW (`RtlUnwind`).

2.4.2 Step by step. **[O]** W-SIG (`RtlUnwindEx`):
1. capture the caller's context;
2. without a record, make one with code `STATUS_UNWIND` (0xC0000027) and no parameters; set
   `EXCEPTION_UNWINDING`, and `EXCEPTION_EXIT_UNWIND` if there is no target frame;
3. walk as in the search pass but asking for `UNW_FLAG_UHANDLER`;
4. for a frame with a handler: if its establisher frame is above the target, raise
   `STATUS_INVALID_UNWIND_TARGET` (0xC0000029); if it equals the target, add
   `EXCEPTION_TARGET_UNWIND` to the flags; call the handler (with `TargetIp` in the dispatcher
   context); `ExceptionContinueSearch` → go on (and clear `EXCEPTION_COLLIDED_UNWIND`);
   `ExceptionCollidedUnwind` → 2.6; anything else → `STATUS_INVALID_DISPOSITION`;
5. stop after the target frame; set `Rax = ReturnValue` and, unless the code is
   `STATUS_UNWIND_CONSOLIDATE`, `Rip = TargetIp`; then `RtlRestoreContext` (2.4.3).

The flag values seen by the handlers in a real Windows run match this: the first frame of an
unwind sees `STATUS_UNWIND` with exactly `EXCEPTION_UNWINDING`, the target frame sees
`EXCEPTION_UNWINDING | EXCEPTION_TARGET_UNWIND`. **[O-test]** W-T-EXC (`collided_exception_handler`
cases 1 and 5).

2.4.3 `RtlRestoreContext(context, record)`: loads the context and continues there. Two record
codes change it first: `STATUS_LONGJUMP` (0x80000026) with the `_JUMP_BUFFER` in
`ExceptionInformation[0]` takes the nonvolatile registers, `Rsp`, `Rip`, MXCSR, x87 control word
and XMM6–15 from the buffer; `STATUS_UNWIND_CONSOLIDATE` (0x80000029) calls the function in
`ExceptionInformation[0]` with the record, from a frame whose unwind data leads to the target
context (so that an exception or unwind started inside the callback skips the frames already
unwound), and continues at the address the callback returns. **[O]** W-SIG (`RtlRestoreContext`,
`call_consolidate_callback`), **[O-test]** W-T-EXC (`test_restore_context`: both codes on Windows;
`test_consolidate_dummy`: `ExceptionInformation[10]` is −1 on x64 and the callback's return
value becomes `Rip`).

2.4.4 Consequence that matters for a translator: when a C++ `catch` runs, it runs as that
consolidate callback, **on the stack below the frames being unwound**: their memory (including
the thrown object, which lives in the thrower's frame) is still intact while the catch body
runs, and the real stack pointer is moved to the target frame only when the callback returns.
**[O]** W-SIG (`call_consolidate_callback` builds its frame on the current stack), W-CXX
(`find_catch_block` passes `call_catch_block` as the callback). Microsoft's own catch-calling
routine is not publicly documented. **[N]** (behaviour consistent with Wine is asserted by W-T-EXC
`test_restore_context`; the object's lifetime is not tested there).

### 2.5 Nested exceptions (an exception raised while a handler runs in the search pass)

2.5.1 Each handler call is made from a dispatcher frame that has its own exception handler.
When a new exception's search reaches that frame, its handler returns
`ExceptionNestedException` and reports, through the dispatcher context, the establisher frame
of the frame whose handler was running. The new search then sets `EXCEPTION_NESTED_CALL` on its
record, continues the walk (through the original faulting frames, which are still on the
stack), and clears the flag after it has called the handler of that reported frame. In the
unwind pass the wrapper does nothing (continue search). **[O]** W-SIG (`call_seh_handler`,
`nested_exception_handler`, the `nested_frame` logic of `call_seh_handlers`).

2.5.2 On Windows: a handler raising a second exception sees it come back to its own frame with
`EXCEPTION_NESTED_CALL` set, and the next outer frame sees it with the flag clear; a handler that
itself returns `ExceptionNestedException` with a changed `EstablisherFrame` moves the point where
the flag is cleared. **[O-test]** W-T-EXC (`nested_exception_handler`, `nested_exception_handler2`).

### 2.6 Collided unwinds (an unwind started while a termination handler runs)

2.6.1 Each unwind-handler call is made from a wrapper frame whose handler, when a later unwind
reaches it, copies the *older* unwind's dispatcher context (pc, base, entry, frame, handler,
data, history, `ScopeIndex`, and the context) into the new one and returns
`ExceptionCollidedUnwind`. The new unwind then steps over the frame being processed by the old
one, sets `EXCEPTION_COLLIDED_UNWIND` for the call of that frame's handler, and so resumes where
the old one stood instead of unwinding the old one's frames again. The same disposition in the
search pass makes the dispatcher take over the old dispatcher context likewise. **[O]** W-SIG
(`unwind_exception_handler`, the `ExceptionCollidedUnwind` cases in `RtlUnwindEx` and
`call_seh_handlers`).

2.6.2 On Windows: an exception raised inside a termination handler, handled by unwinding to an
outer frame, produces exactly one call with `EXCEPTION_UNWINDING | EXCEPTION_COLLIDED_UNWIND` to
the frame whose handler collided, then `EXCEPTION_UNWINDING | EXCEPTION_TARGET_UNWIND` at the
target; six handler calls in all in that test. **[O-test]** W-T-EXC (`test_collided_unwind`).

### 2.7 The unhandled-exception filter

2.7.1 `SetUnhandledExceptionFilter(f)` replaces the process-wide top-level filter and returns
the previous one (NULL means none). The filter is called by `UnhandledExceptionFilter` when the
process is not being debugged, in the faulting thread; it returns `EXCEPTION_EXECUTE_HANDLER`
(1: end the process), `EXCEPTION_CONTINUE_EXECUTION` (−1: resume at the context, which it may
have edited), or `EXCEPTION_CONTINUE_SEARCH` (0: default handling: error mode, error box).
**[D]** MS-UEF.

2.7.2 The thread-start handler maps those to the dispositions and terminates the process with
the exception code as exit code on `EXCEPTION_EXECUTE_HANDLER`. **[O]** W-EXC
(`call_unhandled_exception_handler`). Every first-party game DLL calls
`SetUnhandledExceptionFilter`; the static CRT also calls `UnhandledExceptionFilter`.

### 2.8 `longjmp` on x64

2.8.1 In Microsoft C++ code `longjmp` unwinds like an exception (destructors run) and may be
used where an exception could be raised; it must be called before the function that called
`setjmp` returns. **[D]** MS-LONGJMP. A termination handler runs when `longjmp` leaves its
`__try` block, for the `setjmpex.h` form; the `setjmp.h` form is described as bypassing
termination handling. **[D]** MS-TIMING (note). Which of the two the x64 compiler's plain
`setjmp` gets in C code is not stated. **[N]** (test 8 settles it on Windows).

2.8.2 Mechanism. `setjmp` (x64 `_setjmpex`) stores in the `_JUMP_BUFFER` the `Frame` (the
compiler passes it a frame value as a second argument, in RDX), RBX, `RSP` after return, RBP,
RSI, RDI, R12–R15, the return address as `Rip`, MXCSR, the x87 control word and XMM6–15 (layout
from offset 0: `Frame, Rbx, Rsp, Rbp, Rsi, Rdi, R12, R13, R14, R15, Rip` 8 bytes each, then
`MxCsr` 4, `FpCsr` 2, spare 2, then ten 16-byte XMM values at 0x60). `longjmp(buf, v)` turns 0
into 1; if `Frame` is 0 it just restores the registers and jumps; otherwise it calls
`RtlUnwind(Frame, Rip, record{STATUS_LONGJUMP, 1 parameter = buf}, v)`, so every termination
handler and C++ cleanup between runs, and `RtlRestoreContext` takes the registers from the
buffer (2.4.3). **[O]** W-SIG (`NTDLL__setjmpex`, `NTDLL_longjmp`), W-CXX (`except_x86_64.c`
`longjmp`), W-H (`setjmp.h`). The C++ frame handler lets `STATUS_LONGJUMP` through even in
functions compiled for C++ exceptions only, so destructors run. **[O]** W-CXX (`cxx_frame_handler`).

---

## Part 3. Structured handlers (`__C_specific_handler`)

3.1 The handler data is a scope table: a 32-bit `Count`, then `Count` entries of four 32-bit
RVAs: `BeginAddress`, `EndAddress` (exclusive), `HandlerAddress`, `JumpTarget`. **[O]** W-H
(`SCOPE_TABLE_AMD64`), L-WEH (`emitCSpecificHandlerTable` comment: "LabelStart … Inclusive,
LabelEnd … Exclusive, FilterOrFinally … One means catch-all, LabelLPad … Zero means
__finally"). Microsoft does not document the table. **[N]** for the formal source; the game's
1,495 scope entries parse with this layout with no inconsistency.

3.2 Kinds of entry. **[O]** W-UNW (`__C_specific_handler`), L-WEH:
- `JumpTarget == 0`: a `__finally`; `HandlerAddress` is the termination funclet;
- `JumpTarget != 0`: an `__except` whose body starts at `JumpTarget`; `HandlerAddress == 1`
  means the filter is the constant `EXCEPTION_EXECUTE_HANDLER`; otherwise it is the RVA of a
  filter funclet.

3.3 Filter values: `EXCEPTION_EXECUTE_HANDLER` 1, `EXCEPTION_CONTINUE_SEARCH` 0,
`EXCEPTION_CONTINUE_EXECUTION` −1. **[D]** MS-TRY.

3.4 Search pass (flags without `EXCEPTION_UNWINDING`/`EXIT_UNWIND`). **[O]** W-UNW:
for each entry from `DispatcherContext->ScopeIndex` on, in table order (innermost first), that
is an `__except` whose range contains `ControlPc`: evaluate the filter (constant 1, or call the
filter funclet with `EXCEPTION_POINTERS* {record, context}` in RCX and the establisher frame in
RDX). Continue search → next entry. Continue execution → return `ExceptionContinueExecution`.
Execute handler → `RtlUnwindEx(EstablisherFrame, ImageBase + JumpTarget, record,
ReturnValue = ExceptionCode, context, history)`, which does not return. No entry → return
`ExceptionContinueSearch`.

3.5 Unwind pass. **[O]** W-UNW: for each entry from `ScopeIndex` on that is a `__finally` whose
range contains `ControlPc`: if this is the target frame (`EXCEPTION_TARGET_UNWIND`) and
`TargetIp` lies in the entry's range, stop; otherwise set `ScopeIndex = i + 1` **before** calling
the termination funclet with `AbnormalTermination = TRUE` in RCX and the establisher frame in
RDX. Return `ExceptionContinueSearch`. On Windows, a second call with the `ScopeIndex` left by
the first does not run the same `__finally` again. **[O-test]** W-T-EXC (`test___C_specific_handler`:
termination handler called once, `ScopeIndex` 1, and still once after the second call).

3.6 Order: all filters run (search pass) before any termination handler (unwind pass); the
termination handlers run innermost first; then the `__except` body; then control goes on after
it. A filter returning −1 resumes at the raise point and no termination handler runs. **[D]**
MS-TIMING.

3.7 What a funclet is here: the filter and the `__finally` body are separate entry points with
their own code, reached only through the table, called with the parent's establisher frame
so that they can address the parent's locals. **[O]** L-WEH ("the filter function is actually a
cleanup handler with the same prototype"; the parent-frame-offset symbol it emits for
`llvm.eh.recoverfp`), W-UNW (the two call shapes above). A `__finally` reached normally (no
exception) is executed inline by the compiled code, not through the table. **[N]** (MSVC's
inline-copy-versus-call choice is not documented; the earlier analysis counts 1,335 SEH funclets, so some
are separate).

3.8 The `__except` body runs in the parent function's frame (the unwind ends there, `RSP` and
the nonvolatile registers as the parent's unwind data restore them), with the exception code in
`EAX`. **[O]** W-UNW (`ReturnValue = ExceptionCode`), W-SIG (`RtlUnwindEx` sets `Rax`).

3.9 Microsoft's own `__C_specific_handler` also tests the code `0xE06D7363` (a C++ exception) and
calls `RtlUnwindEx`. The static copy's disassembly shows this. Why it tests the C++ code
(plausibly to destroy a C++ exception object that an `__except` caught) is not settled from
public sources; Wine's version does not. **[N]**

3.10 Variants seen in the game: `__GSHandlerCheck_SEH` checks the stack cookie and then defers to
`__C_specific_handler`; `__GSHandlerCheck` only checks the cookie and continues the search.
Neither is publicly documented. **[N]**

---

## Part 4. C++ exceptions (`__CxxFrameHandler3`)

### 4.1 Which handler: 3, not 4

4.1.1 `__CxxFrameHandler4` ("FH4") is a compressed format introduced with Visual Studio 2019:
off by default behind `/d2FH4` in 2019 Preview 3, on by default from 16.3. **[D]** MS-FH4.
A compiler of 2012 to 2016 therefore could not emit it. (derived from MS-FH4) The game's
C++ handlers are all `__CxxFrameHandler3` (static, or imported from `msvcr110.dll` in OriginSDK),
and every `FuncInfo` magic is one of 0x19930520–0x19930522.

4.1.2 FH4 for completeness: a header byte of flags (catch funclet, separated, BBT, has unwind
map, has try map, EHs, noexcept), variable-length integers (low bits of the first byte give the
length), typed unwind entries (no handler, destroy object, destroy through pointer, call a
funclet), and catch entries that can carry continuation addresses. **[O]** W-CXX (`handler4.c`).

### 4.2 Throwing: `_CxxThrowException` and the record

4.2.1 `throw x;` builds the object, then calls `_CxxThrowException(object, ThrowInfo)`, which
raises code **0xE06D7363** with flags `EXCEPTION_NONCONTINUABLE` and **four** parameters:
`[0]` = 0x19930520 (the magic), `[1]` = the object's address, `[2]` = the `ThrowInfo`'s address,
`[3]` = the image base of the module holding the `ThrowInfo` (on x64 every pointer inside it is
an RVA from that base). **[O]** W-CXX (`cpp.c` `_CxxThrowException`, `cppexcept.h`
`CXX_EXCEPTION_PARAMS` 4 and the RVA forms). The object is a temporary in the throwing
function's frame, which stays in place until the catch is done (derived from 2.4.4).

4.2.2 A rethrow (`throw;`) is `_CxxThrowException(NULL, NULL)`: parameters 1 and 2 are zero.
**[O]** W-CXX (`cxx_frame_handler` "rethrow detected"); 83 of the game's 208 throw sites are
rethrows of this form.

4.2.3 `ThrowInfo` (x64, 16 bytes): `attributes` (32-bit: 1 const, 2 volatile, and in later
runtimes 16 WinRT), `pmfnUnwind` (RVA of the object's destructor, 0 if none), `pForwardCompat`
(RVA), `pCatchableTypeArray` (RVA). **[O]** W-CXX (`cxx.h` `cxx_exception_type` in its RVA form;
`cppexcept.h` `TYPE_FLAG_*`).

4.2.4 `CatchableTypeArray`: a 32-bit count, then that many RVAs of `CatchableType`, the thrown
type first and then each base class it can be caught as. **[O]** W-CXX (`cxx_type_info_table`;
`exception_ptr_from_record` takes entry 0 as the object's own type).

4.2.5 `CatchableType` (28 bytes): `properties` (1 simple type, i.e. scalar or pointer, copied
bitwise; 4 has a virtual base; 8 WinRT), `pType` (RVA of the type descriptor), the
this-displacement (`mdisp`, `pdisp`, `vdisp`: offset of the base in the object, and when
`pdisp >= 0` the virtual-base lookup), `sizeOrOffset`, `copyFunction` (RVA of the copy
constructor, 0 for bitwise). **[O]** W-CXX (`cxx.h` `cxx_type_info`, `this_ptr_offsets`;
`cppexcept.h` `get_this_pointer`). A value 2 ("by reference only") is used by MSVC headers.
**[N]** (not defined in Wine; not used by its matcher).

4.2.6 Type descriptor (`type_info`): a vtable pointer (8 bytes), a spare/name pointer (8), then
the decorated name, NUL-terminated (`.?AV…@@` for classes, `.H` for `int`). **[O]** W-CXX
(`cxx.h` `type_info`). The game throws `.H` once (`CThread::Stop`) and STL types otherwise.

### 4.3 `FuncInfo` and its tables (x64)

4.3.1 `FuncInfo`, 40 bytes. **[O]** W-CXX (`cppexcept.h` `cxx_function_descr`, RVA form), L-WEH
(`emitCXXFrameHandler3Table` comment):

| Off | Field | Meaning |
|---|---|---|
| 0 | `magic` (29 bits) + BBT flags (3) | 0x19930520; 0x19930521 adds the ES type list; 0x19930522 adds `EHFlags` |
| 4 | `maxState` | number of unwind map entries |
| 8 | `pUnwindMap` | RVA |
| 12 | `nTryBlocks` | |
| 16 | `pTryBlockMap` | RVA |
| 20 | `nIPMapEntries` | |
| 24 | `pIPtoStateMap` | RVA |
| 28 | `dispUnwindHelp` | offset from the establisher frame of an 8-byte slot the runtime uses |
| 32 | `pESTypeList` | RVA, magic ≥ 0x19930521 |
| 36 | `EHFlags` | magic ≥ 0x19930522: bit 0 = compiled `/EHs` (only C++ exceptions handled), bit 2 = `noexcept` |

The game's `FuncInfo`s all parse with this 40-byte layout.

4.3.2 Unwind map entry (8 bytes): `toState` (the state this one leads back to, −1 = none),
`action` (RVA of a cleanup funclet, or 0). States form a tree: undoing state *s* runs its
action and moves to `toState`. **[O]** W-CXX (`unwind_info`, `cxx_local_unwind`), L-WEH.

4.3.3 Try block map entry (20 bytes): `tryLow`, `tryHigh` (the states covered by the try body),
`catchHigh` (the highest state used inside its catch funclets), `nCatches`, `pHandlerArray`
(RVA). **[O]** W-CXX (`tryblock_info`), L-WEH.

4.3.4 Handler type (catch clause), 20 bytes on x64: `adjectives` (1 const, 2 volatile, 8
reference), `pType` (RVA of the caught type's descriptor; 0 = `catch(...)`), `dispCatchObj`
(offset from the establisher frame where the caught object or reference is put; 0 = none),
`addressOfHandler` (RVA of the catch funclet), `dispFrame` (offset in the catch funclet's own
frame where the parent's establisher frame is stored). **[O]** W-CXX (`catchblock_info` with
`frame` on `_WIN64`), L-WEH ("ParentFrameOffset // x64 and AArch64 only").

4.3.5 IP-to-state map entry (8 bytes): `ip` (RVA), `state`. The state of an address is the
state of the last entry whose `ip` is at or below it; −1 before the first. **[O]** W-CXX
(`ip_to_state`), L-WEH (`computeIP2StateTable`: an entry at each funclet start and at each state
change). Entries must lie on instruction boundaries. **[O]** L-NOP comment.

4.3.6 `UnwindHelp`: the function's prolog stores −2 in its slot; the runtime keeps there the
state already unwound to (−2 = "derive it from the IP") and, in the second 32-bit half, the
highest state seen at a catch. **[O]** L-FL (`adjustFrameForMsvcCxxEh` stores −2 on entry),
W-CXX (`cxx_local_unwind`, `find_catch_block`, `call_catch_block` resets both halves after a
catch).

### 4.4 The handler's two passes

4.4.1 State of the frame = `ip_to_state(ControlPc)` (4.3.5); for a caller frame that is the state
at its return address. **[O]** W-CXX (`get_exception_pc` returns `ControlPc`).

4.4.2 If `EHFlags` bit 0 is set and the code is not 0xE06D7363, not a consolidate record of its own
and not `STATUS_LONGJUMP`, the handler does nothing (a `/EHs` function ignores hardware faults
and other SEH exceptions). **[O]** W-CXX (`cxx_frame_handler`), **[D]** MS-EH ("If you use /EHs …
your catch(...) clauses don't catch asynchronous structured exceptions … objects in scope … aren't
destroyed").

4.4.3 Unwind pass: run the unwind map from the current state down to the target state (the try
block's level for the frame that holds the catch, −1 for frames being left entirely), calling
each action funclet with the establisher frame. **[O]** W-CXX (`cxx_local_unwind`).

4.4.4 Search pass, for a C++ exception: for each try block whose `[tryLow, tryHigh]` contains the
state (and, when the exception comes from inside a catch funclet of this function, only try
blocks nested correctly with that catch), for each catch clause in order: `catch(...)` matches
anything; otherwise walk the catchable type array and compare the type descriptor (same address
or same decorated name), and reject when the thrown type is const or volatile and the clause is
not. On a match: copy the object into `establisher + dispCatchObj` (reference: the adjusted
address; simple type: the bytes, a pointer adjusted; class: the copy constructor with the
adjusted source, or the bytes), then `RtlUnwindEx(frame, ControlPc, consolidate record,
…)` whose callback runs the catch (4.5). Non-C++ exceptions only match `catch(...)` (and only in a
function not compiled `/EHs`). No match → continue search. **[O]** W-CXX (`find_catch_block`,
`find_catch_handler`, `cppexcept.h` `find_caught_type`, `copy_exception`). The game has no typed
catch: all 98 clauses are `catch(...)`.

4.4.5 The consolidate record built for a catch (Wine's; Microsoft's is not documented):
`STATUS_UNWIND_CONSOLIDATE`, noncontinuable, 11 parameters: the callback, the parent's frame, the
`FuncInfo`, the try block's `tryLow`, a translated SEH record if any, the catch funclet's address,
the original record, the original context, and −1 in `[10]`. **[O]** W-CXX (`find_catch_block`);
that `[10]` is −1 on x64 Windows **[O-test]** W-T-EXC (`test_consolidate_dummy`). Third-party code
reads `[6]` as the original record. **[O]** W-CXX comment ("tyFlow expects …").

### 4.5 A catch block is a separate function (a funclet)

4.5.1 Each catch clause is compiled as a separate function with its own `RUNTIME_FUNCTION` and
unwind info, pointing to the same `FuncInfo` as its parent. (all 114 funclets checked),
**[O]** L-WEH ("emit a reference to the LSDA for the parent function" for funclets).

4.5.2 Call: RDX = the parent's establisher frame (Wine passes the funclet's own address in RCX), and
the funclet's prologue stores RDX in its home slot at `[RSP+16]` ("the runtime cares about this")
and sets its frame register from it, so it addresses the parent's locals at their original
offsets. **[O]** L-FL (funclet prologue: establisher in RDX, spilled to 16(%rsp), frame pointer
derived from it), W-CXX (`call_exc_handler`: handler address in RCX, frame in RDX, 0x100 in R8D).

4.5.3 Return: the catch funclet returns in RAX the address at which the parent continues (the
code after the try statement); that becomes `Rip` of the restored context. **[O]** W-CXX
(`call_catch_block` returns the funclet's result), W-SIG (`RtlRestoreContext` sets `Rip` from the
consolidate callback).

4.5.4 After the catch returns normally the exception object is destroyed (its `pmfnUnwind`) unless
the catch rethrew. **[O]** W-CXX (`__CxxRegisterExceptionObject`/`__CxxUnregisterExceptionObject`
around the call). In the game's static CRT this calling routine is guest code (515 bytes, with
a `RaiseException` for the rethrow). 

4.5.5 Cleanup (unwind map) actions in MSVC x64 code are also funclets called with the parent's
frame; the game has 1,986 of them (1,526 in OriginSDK). Called through the
`call_exc_handler` shape. **[O]** W-CXX (`call_unwind_handler`).

### 4.6 Rethrow, nesting, specifications, `exception_ptr`, SEH in the same frame

4.6.1 Rethrow: the frame handler that sees parameters 1 and 2 zero replaces the record by the
exception currently being handled (kept per thread by the C++ runtime). The routine that runs the
catch wraps the funclet in a filter that recognises the rethrow and raises the original record
again. **[O]** W-CXX (`cxx_frame_handler`, `call_catch_block`, `cxx_rethrow_filter`).

4.6.2 A throw inside a catch funclet: the funclet's frame is searched first (its `ControlPc` is in
the funclet, its state inside `(tryHigh, catchHigh]` of the try block that owns it); the handler
recognises "this frame is a catch funclet of try block *t*", reads the parent's real establisher
frame from `funclet frame + dispFrame`, and continues with that frame and only the try blocks
outside *t*. It finds which funclet it is in by looking up `ControlPc`'s function entry and
comparing its `BeginAddress` with the clauses' `addressOfHandler`. **[O]** W-CXX
(`cxx_frame_handler` "nested exception detected", `RtlLookupFunctionEntry` call).

4.6.3 Exception specifications: `throw()`, `noexcept`, `noexcept(true)` mean "does not throw" and
an exception leaving such a function calls `std::terminate` (in `/std:c++14`; behaviour of
`throw()` "could lead to undefined behaviour" there); `throw(type)` is accepted and treated as
`noexcept(false)`. **[D]** MS-SPEC. At run time, `EHFlags` bit 2 makes the handler call `terminate`
when an exception would leave the function; the ES type list is not consulted. **[O]** W-CXX
(`check_noexcept`; `expect_list` is only printed).

4.6.4 The return-address rule. The state lookup for a caller frame uses its return address, so a
`call` that is the last instruction of a state region would be attributed to the next region; the
Windows x64 ABI therefore requires the compiler to put an instruction (a `nop`) after such a call
so that the return address stays in the call's region. **[O]** L-NOP (the comment states it as an
ABI requirement). Microsoft's own statement of the rule was not found. **[N]**

4.6.5 `std::exception_ptr`: Wine's runtime copies the record and the object (through the type's
copy) to the heap with a reference count; rethrowing raises the copied record again. Wine notes
its layout is not binary compatible with Microsoft's. **[O]** W-CXX (`exception_ptr.c`). Microsoft's
layout is not documented. **[N]**. The game's 16 first-party DLLs import nothing for it.
(no such import listed).

4.6.6 SEH and C++ in one frame: "C++ exceptions and structured exceptions can't be mixed within the
same function". **[D]** MS-MIX. Under `/EHa`, `catch(...)` also catches structured exceptions and
destructors run for them. **[D]** MS-EH. A translator set with `_set_se_translator` is called, for a
non-C++ exception, from the C++ frame handler of a frame being searched, and may throw a C++
exception which is then matched in its place. **[D]** MS-SET, **[O]** W-CXX (`se_translation_filter`).
Whether the game's 90 try/catch functions are `/EHs` (bit 0 of `EHFlags`) is readable from their
`FuncInfo` and was not reported. **[N]**.

---

## Part 5. What a static translator must preserve

### 5.1 Which properties of the original code the tables describe

Everything in parts 1, 3 and 4 is expressed in the original program's terms:

- **instruction addresses**: function ranges (1.1.2), prolog offsets of each save (1.3.1), the
  prolog size (1.3.5), epilog byte patterns (1.4), scope ranges (3.1), IP-to-state entries (4.3.5),
  continuation addresses (`JumpTarget`, 3.2; the catch funclet's return value, 4.5.3; `setjmp`'s
  `Rip`, 2.8.2);
- **stack layout**: the establisher frame (1.2.7), every save offset (1.3.3), the frame register
  and its offset (1.2.6), `dispUnwindHelp`, `dispCatchObj`, `dispFrame` (4.3), the thrown object in
  the thrower's frame (4.2.1);
- **register state**: which nonvolatile registers the prolog saved and where (1.3), RDX as the
  funclet's frame (4.5.2), RAX as the continuation (4.5.3) and as the `__except` code (3.8);
- **runtime structures in guest memory** read by guest code: `EXCEPTION_RECORD` (2.1.2),
  `CONTEXT`, `DISPATCHER_CONTEXT` at fixed offsets (2.3.3), the scope index (3.5), `UnwindHelp`
  (4.3.6), the C++ runtime's per-thread current exception (4.6.1).

DESIGN.md already keeps the guest stack and registers in memory the runtime owns, pushes the
original return address `BASE + rva` at every call (sections 4–6), and the project's unwinder already reads unwind
data from guest memory.

### 5.2 Requirements, each with its reason

| # | Requirement | Reason (statements) |
|---|---|---|
| X1 | Every guest return address on the guest stack stays the original `BASE + rva` of the instruction after the call, including calls the lifter turns into direct host calls. | `ControlPc` of a caller frame is that value and nothing else; lookups of function entries, scope ranges and IP-to-state states use it as is (2.3.3, 3.4, 4.4.1); compilers placed a `nop` after state-ending calls precisely so that this exact value falls in the right region (4.6.4). Already true by DESIGN 5–6. |
| X2 | The guest stack's frames stay exactly as each prolog laid them out: every `push`, `sub rsp`, save to `[rsp+n]`, frame register setup, and `__chkstk` adjustment executed literally, in that order, into guest memory. No later optimisation may elide a save or move the frame. | The unwinder restores the caller's registers from those slots and computes the establisher frame from `RSP` or the frame register (1.2.7, 1.3.3); funclets address the parent's locals at fixed offsets from that frame (4.5.2); `dispCatchObj`, `dispUnwindHelp` are such offsets (4.3). |
| X3 | At every call site that can raise (any call: the callee may throw), all guest state that the original code held in memory is in guest memory, and every nonvolatile register value the original code had is in `Ctx` or in the slot its prolog saved it to. When registers move to host locals (DESIGN 4, "after Gate 1"), they must be written back before each call and re-read after it. | Unwinding restores nonvolatile registers from the stack slots and from the context (1.3, 2.4.2); a catch or `__except` resumes the parent with those values (3.8, 4.5.3); funclets read and write the parent's locals in guest memory (4.5.2). |
| X4 | A guest "instruction pointer" is known wherever an exception can start: for a raise through an import (`RaiseException`, `_CxxThrowException`, `RtlRaiseException`), the return address of that call (available on the guest stack); for a hardware fault in lifted code that a handler might catch (`/EHa` code, an `__except` around a fault), the guest address of the faulting instruction, via a map from host pc to guest pc or by storing the guest pc before each instruction that can fault. | The faulting frame's `ControlPc` is the faulting instruction (2.1.5) and scope ranges and states are looked up with it (3.4, 4.4.1); without it the wrong `__except` or no handler would be chosen. Precise state at the fault is also needed (X3). The game has 1 non-CRT `__except` (OpenSSL's entropy poll) and CRT filters testing 0xC0000005. |
| X5 | Faults inside prologs and epilogs either cannot reach guest handlers, or carry the exact guest pc; and the image's code bytes stay mapped as on disk. | The unwinder treats prolog and epilog addresses differently and recognises epilogs by reading the code bytes (1.3.5, 1.4); the unwinder decodes them from guest memory. |
| X6 | Funclets are lifted and callable as guest functions at their original addresses: catch funclets, cleanup actions, SEH filters, `__finally` bodies, and the CRT's handler routines themselves (`__CxxFrameHandler3`, `__C_specific_handler`, `__GSHandlerCheck*`, and the routine that calls catch blocks), with the guest calling convention (RCX, RDX, R8, R9 in `Ctx`, return in `RAX`). | They are reached only through tables (1.2.3, 3.2, 4.3), called by the dispatcher with RDX = parent frame (3.4, 3.5, 4.5.2); in 23 of the 24 DLLs `__C_specific_handler` is static CRT code (guest code), and so is
`__CxxFrameHandler3` in 6 of the 7 that have one; such code is today "lifted-without-handlers". OriginSDK imports its handlers from `msvcr110.dll`, so that DLL's three entries must also exist as guest-callable code. |
| X7 | Lifted code can be **entered at a continuation address inside a function**, with `Ctx` (registers, `RSP`) restored: `__except` bodies (`JumpTarget`), the code after a `try` (the catch funclet's return value), `setjmp`'s return point, and a context edited by a filter or vectored handler that returns "continue execution". | `RtlRestoreContext` sets `Rip`/`Rsp` to such addresses (2.4.2, 2.4.3, 2.2.3); they are mid-function labels, not function entries. |
| X8 | When the guest stack is unwound to a frame, the host frames of the lifted functions above that frame are discarded too, and nothing on the host side of them runs afterwards (no `CALL_*` epilogue, no rsp check). | `RET` returns to the host caller (DESIGN 6); after a guest unwind the guest stack no longer matches the host stack and DESIGN 6's rsp check would stop the run at the first skipped frame. |
| X9 | A `CONTEXT` is built from guest state, in guest memory, field-exact (`Rip`, `Rsp`, all sixteen integer registers, `EFlags`, `MxCsr`, `SegCs`/`SegSs`, XMM0–15, `ContextFlags`), and edits made to it by guest handlers are honoured on resume. | Handlers, filters and vectored handlers receive it and may change it (2.2.1, 2.3.2, 2.7.1); the unwinder and the C++ runtime copy it (Wine's consolidate path reads XMM6–15 at 0x200). The project's `rt_unwind_capture` already defines most fields. |
| X10 | `EXCEPTION_RECORD`, `DISPATCHER_CONTEXT` (0x50 bytes, 2.3.3, including `HistoryTable` and `ScopeIndex`) and `EXCEPTION_POINTERS` live in guest memory at their exact layouts, and `FunctionEntry` points into the module's real `.pdata`. | Guest handler code reads them at fixed offsets (`[r9+0x38]`) and writes `ScopeIndex` (3.5); the C++ handler looks up `ControlPc` again and compares `BeginAddress` (4.6.2). |
| X11 | While a catch or `__except` decision is being made and while a catch funclet runs, the guest stack below the target frame stays untouched: the dispatcher, the handlers and the funclet run on guest stack space **below** the faulting/throwing frames. | The thrown object and the frames being unwound are still read during the catch (2.4.4, 4.4.4 copy, 4.5); an exception inside the catch must still find the parent through `dispFrame` (4.6.2). |
| X12 | The dispatcher's own calls of handlers and of unwind handlers appear to a guest walk as frames that answer `ExceptionNestedException` and `ExceptionCollidedUnwind`; this includes the frames of a shim through which a guest callback was called. | Nested and collided cases depend on those wrapper frames (2.5, 2.6); The project's unwinder treats the shim boundary as one opaque frame today, and this is named there as missing for dispatch. |
| X13 | The thread block's stack limits (`NT_TIB.StackBase`, `StackLimit`) describe the guest stack. | The dispatcher stops or marks `EXCEPTION_STACK_INVALID` by them (2.2.2). |
| X14 | Per-thread C++ runtime state (current exception record, "processing throw" count) works: the static CRT keeps it in its thread data (TLS/FLS of the guest). | Rethrow and `exception_ptr` read it (4.6.1, 4.6.5). |
| X15 | Vectored handlers and the top-level filter are kept in the runtime and called in the documented order and with documented effects. | 2.2.1, 2.7. Plan A already stores the filter (RUNTIME.md 9.3). |
| X16 | `longjmp` with a nonzero `Frame` goes through the same unwind (X7, X8) with `STATUS_LONGJUMP`; with `Frame` 0 it is a plain context restore. | 2.8.2. |

### 5.3 How open implementations of comparable translators have done it

**A. Wine on ARM64EC with FEX as the x86-64 emulator.** Wine runs the real x64 dispatch algorithm
(part 2) over x64 `CONTEXT`s and the guest's own x64 `.pdata`, and the guest's handlers run as
emulated guest code. When a fault happens in emulated code, Wine converts the native ARM64 context
to an x64 one and calls the emulator's `ResetToConsistentState` hook first. FEX there checks
whether the faulting native pc is in its JIT code; it handles internally what is its own business
(self-modifying code, its guard pages, misaligned atomics), and otherwise, for a fault in JIT code,
reconstructs the x86 thread state at the faulting guest instruction and "rethrows" the exception
onto the guest stack, so that the x64 dispatcher sees a guest fault at a guest `RIP`. **[O]** W-EC
(`prepare_exception_arm64ec`), FEX (`ResetToConsistentStateImpl`, `ResetToConsistentState`).
*Cost:* the JIT must be able to reconstruct the exact guest state at any instruction that can
fault (metadata per block or per instruction); all handler code runs emulated; the dispatcher
and unwinder are native and shared with the rest of Windows. *Relevance:* closest in spirit to
our layout (guest state in memory, real x64 tables, a native runtime implementing ntdll); the
difference is that FEX resumes by re-entering its dispatcher loop, while lifted C has nested
host frames (X8).

**B. box64 (Linux host, x86-64 guest, dynamic recompiler).** On a host signal, box64 finds the
translated block containing the native pc, asks the block's mapping for the x86-64 address of the
instruction that was executing, copies the native registers into its emulated CPU state, adjusts
it to that instruction, and delivers the signal to the guest's handler with a guest context. If
the guest handler changed the context, box64 abandons the native frames with `siglongjmp` back
into its emulator loop and resumes there, at the new guest `RIP`. **[O]** BOX64 (the SIGSEGV path:
`FindDynablockFromNativeAddress`, `getX64Address`, `copyUCTXreg2Emu`, `adjustregs`, the "doing
siglongjmp to resume emu" branch). *Cost:* a per-block table from native to guest addresses;
resume needs a central loop to jump back to. For us the analogue of the central loop would be a
trampoline at the base of each guest call (`rt_guest_call`).

**C. XenonRecomp (static recompiler, Xbox 360 PowerPC to C++).** Exceptions are not supported
("challenging due to the use of the link register and the fact that exception handlers can jump
to arbitrary code locations"); `setjmp`/`longjmp` are redirected to native implementations that
save and restore the guest CPU state; exception-handling data between functions is listed so the
recompiler skips it as non-code. **[O]** XENON (README "Exceptions", "setjmp", `invalid_instructions`).
*Cost:* none at run time; any game that throws on a path it takes breaks. This is plan A's
position, and it worked for a shipped port (Unleashed Recompiled, named in the README). **[O]**

**D. McSema (static lifter, Linux ELF, Itanium ABI).** Described as lifting programs with C++
exception control flow by recovering the exception tables and re-emitting them for LLVM. **[N]**:
the Trail of Bits post (2019-01-21) and its mirrors were not reachable from here; only a search
summary was read. It is a different ABI (two-phase like Windows, but DWARF tables and a personality
function), so it would at most show how a lifter maps landing pads to resumable entry points.

### 5.4 What this means for the choice (notes, not sourced; for the designers)

- X7 and X8 together are the core: a guest unwind must end in a host-side transfer to the lifted
  function that owns the target frame, entering it at a label. Three shapes are known in general:
  a `setjmp`-style anchor at guest-call boundaries (A/B's "back to the loop"), an "unwinding" flag
  tested after every `CALL_*` with an early return (no host-unwinder dependency; one test per call
  site), or the host's own unwinder (`-fexceptions` C with a host exception thrown from the
  runtime's `RtlRestoreContext`). Each needs a lifted function to have resumable entry labels for
  its continuation addresses (`JumpTarget`s, catch continuations, `setjmp` return points).
- Most continuation addresses are in the tables (`JumpTarget` in scope tables). The code after a
  `try` is not: it is the value a catch funclet returns in RAX, computed by guest code. Lifting the
  funclet shows it (typically a `lea rax, [rip+X]` before the `ret`), so the lifter could collect
  those labels as resumable entries. Not checked on the game. **[N]**
- Plan A remains consistent with all of this: nothing above is needed until a handler must run.
  The cheapest step up is X6 alone (lift the funclets) plus X7/X8 for the one `CThread::Stop`
  pattern the earlier analysis names.

---

## Part 6. Tests a builder could type

Ten small programs, in rising difficulty. Build each for x64 Windows with MSVC or clang-cl
(`/EHsc` unless stated; clang-cl emits `__CxxFrameHandler3` tables, the game's format **[O]** L-WEH),
run on Windows to confirm the expected output, then lift and run under the runtime. Each prints
lines with `printf` and ends with `done`. "Expected" is what Windows prints, from the statements
cited.

1. **A filter that declines.** `main` has `__try { f(); } __except (outer_filter()) { puts("outer
   except"); }`; `f` has `__try { RaiseException(0xE0000001, 0, 0, NULL); } __except
   (inner_filter()) { puts("inner except"); }`; `inner_filter` prints `inner filter` and returns
   `EXCEPTION_CONTINUE_SEARCH`; `outer_filter` prints `outer filter`, and the `__except` body also
   prints `GetExceptionCode()` in hex. Expected: `inner filter`, `outer filter`, `outer except`,
   `code e0000001`, `done`. (3.4, 3.6)

2. **A `__finally` that runs on unwind, after the filters.** `g` raises `0xE0000002`; `f` has
   `__try { g(); } __finally { printf("finally f abnormal=%d\n", AbnormalTermination()); }`; `main`
   has `__try { f(); } __except (puts("filter main"), EXCEPTION_EXECUTE_HANDLER) { puts("except
   main"); }`. Expected: `filter main`, `finally f abnormal=1`, `except main`, `done`. A second call
   of `f` with `g` returning normally prints `finally f abnormal=0`. (3.5, 3.6)

3. **Continue execution, and a noncontinuable raise.** (a) A filter that prints `filter`, increments
   a global and returns `EXCEPTION_CONTINUE_EXECUTION` around `RaiseException(0xE0000003, 0, 0,
   NULL); puts("resumed");`. Expected: `filter`, `resumed`. (b) The same with
   `EXCEPTION_NONCONTINUABLE` inside an outer `__try/__except` that prints the code. Expected:
   `filter`, then `outer c0000025`, then `done`. (2.2.3, 3.3) Variant (c): a vectored handler that
   returns `EXCEPTION_CONTINUE_EXECUTION` for `0xE0000004` after printing `vectored`, with no
   `__try` at all: expected `vectored`, `resumed`. (2.2.1)

4. **A throw caught two frames up, with a destructor in between.** `struct D { ~D() {
   puts("~D"); } };` `void b() { throw 7; }` `void a() { D d; b(); puts("not reached"); }`
   `main: try { a(); } catch (int e) { printf("caught %d\n", e); }`. Expected: `~D`, `caught 7`,
   `done`: the destructor runs before the catch body (two passes). (4.4.3, 4.4.4)

5. **Matching by base class, by reference, and the object's single destruction.** `struct B {
   virtual const char* n() const { return "B"; } virtual ~B() {} }; struct E : B { int id; E(int
   i):id(i){} E(const E& o):id(o.id){ puts("copy E"); } const char* n() const override { return
   "E"; } ~E() { printf("~E %d\n", id); } };` `main: try { throw E(5); } catch (int) { puts("int");
   } catch (const B& b) { printf("caught %s\n", b.n()); }`. Expected: `caught E`, `~E 5`, `done`;
   no `copy E` line is required (the compiler may elide or not; count the `~E` lines against the
   `copy E` lines: one more `~E` than `copy E`). (4.2.4, 4.4.4, 4.5.4)

6. **A rethrow.** `struct O { int id; ~O() { printf("~O %d\n", id); } };` inner function: `try {
   throw O{42}; } catch (...) { puts("inner"); throw; }`; `main`: `try { inner(); } catch (O& o) {
   printf("outer %d\n", o.id); }`. Expected: `inner`, `outer 42`, `~O 42`, `done` (exactly one
   `~O 42`). (4.2.2, 4.6.1)

7. **A throw from inside a catch block, caught in the same function.** `struct E1 { ~E1() {
   puts("~E1"); } }; struct E2 { ~E2() { puts("~E2"); } };` `main: try { try { throw E1(); } catch
   (E1&) { puts("in catch E1"); throw E2(); } } catch (E2&) { puts("caught E2"); }`. Expected:
   `in catch E1`, `~E1`, `caught E2`, `~E2`, `done`. The order `~E1` before `caught E2` follows from
   Wine (the first object is released while the catch is left by unwinding) **[O]** W-CXX
   (`cxx_catch_cleanup` as a `__finally` around the funclet); confirm on Windows. (4.6.2)

8. **`longjmp` across a `__finally` and a destructor.** C file (`#include <setjmpex.h>`): `f` has
   `__try { g(); } __finally { puts("finally"); }`; `g` calls `longjmp(env, 3)`; `main` does `if
   ((v = setjmp(env)) == 0) f(); else printf("back %d\n", v);`. Expected: `finally`, `back 3`,
   `done`. C++ variant (`/EHsc`, plain `<setjmp.h>`): `g` has a local with a destructor printing
   `~L`; expected `~L`, `back 3`. Run the C file again with `<setjmp.h>` instead of `<setjmpex.h>`
   and record whether `finally` prints: that settles 2.8.1. (2.8)

9. **A throw through a callback from a system or runtime function.** The comparator passed to
   `qsort` throws `std::runtime_error("cmp")` on its third call; `main`: `try { qsort(a, 10,
   sizeof(int), cmp); } catch (const std::exception& e) { printf("caught %s\n", e.what()); }`.
   Expected: `caught cmp`, `done`. Then the same through an import that our runtime implements
   natively as a shim, e.g. `EnumWindows` or a timer-queue callback, raising
   `RaiseException(0xE0000009, …)` caught by an `__except` around the API call in `main`;
   expected: `except e0000009`. This is the case of X12: on Windows the exception crosses frames of
   system code that has unwind data; in our runtime it crosses host frames. (Windows' handling of
   exceptions in window procedures and other kernel callbacks on x64 is a separate known
   behaviour; it was not researched here and the test should avoid window procedures.) **[N]**

10. **An exception in an epilog, and the call-at-end-of-try rule.** (a) A hand-written x64 assembly
    function with `.seh_proc`/`.seh_handler`: prolog `push rbx; sub rsp, 0x20`, a body, epilog `add
    rsp, 0x20; pop rbx; ret`, with an exception handler. For each instruction address from the
    function start to the `ret`, call `RtlLookupFunctionEntry` and `RtlVirtualUnwind(UNW_FLAG_EHANDLER,
    …)` on a context whose `Rsp` points at a prepared stack image (saved RBX = 0x1111, return address
    = 0x2222); print, per offset, `handler=yes|no rip=… rsp=+N rbx=…`. Expected: `handler=no` in the
    prolog and at every epilog instruction, `handler=yes` in the body; `rip=0x2222` everywhere; `rbx`
    restored only where the `push` has happened and the `pop` has not; `rsp` the address just past the
    return address. (1.3.5, 1.4, the shape of W-T-UNW) (b) C++: `try { may_throw(); } catch (...) {
    puts("caught"); }` where `may_throw` is the last call before the try's end and is followed directly
    by code in another state; check in the listing that the compiler placed a `nop` after the call, and
    expect `caught`. (4.6.4)

---

## What I did not reach, and what stayed unsettled

Not reached:

- **ReactOS** (`sdk/lib/rtl/amd64/unwind.c`, `except.c`): not read; Wine served as the open
  implementation throughout. A second implementation would help for 1.1.5 (indirect entries), 1.4.4
  (version 2) and 2.5–2.6.
- **Microsoft's own `RtlDispatchException`/`RtlUnwindEx` internals** beyond what Wine's tests assert:
  CET shadow-stack checks on the unwind target (newer Windows), the history table's use, stack-limit
  details. None was found documented.
- **McSema's method** (5.3 D): the post was not reachable.
- **box64 on Windows** (its WoW64/ARM64EC mode) and **FEX's state-reconstruction metadata**: only the
  entry points were read, not how the native-to-guest address tables are built.
- **`llvm-readobj --unwind` output format**: not used; the LLVM emitter (L-MC, L-WEH) and Wine's
  dumper were enough for the layouts.

Not settled (each also marked [N] where it occurs):

1. Version 2 unwind info: when MSVC began emitting it, and what Windows does with `UWOP_EPILOG`
   entries (1.3.7, 1.4.4). Answered for the game by running the unwinder's row check on a game DLL.
2. Indirect `RUNTIME_FUNCTION` entries (bit 0 of `UnwindData`): Wine follows them, Microsoft does not
   document them (1.1.5).
3. The prolog-size boundary wording (1.3.5): Microsoft's text says "≤", Wine and a Windows test say
   the address equal to the size is the body.
4. The return-address `nop` rule (4.6.4): stated by LLVM as an ABI requirement; Microsoft's own text
   not found.
5. `CatchableType` property 2 and the meaning of `__C_specific_handler`'s test of 0xE06D7363 (4.2.5,
   3.9).
6. Microsoft's catch-calling routine and consolidate record (4.4.5) and `exception_ptr` layout
   (4.6.5): only Wine's are public.
7. Whether plain `setjmp.h` in C unwinds on x64 (2.8.1): test 8 settles it.
8. The order of `~E1` and `caught E2` in test 7: from Wine, to be confirmed on Windows.
9. Whether the game's try/catch functions are `/EHs` or `/EHa` (bit 0 of `EHFlags`), and so whether
   their `catch(...)` would catch an access violation (4.6.6): readable from the game's `FuncInfo`s; not reported in the earlier analysis.
10. How a guest exception should cross a native shim that called a guest callback (X12, test 9): a
    design decision for this runtime; Windows' own behaviour for kernel callbacks was not researched.
