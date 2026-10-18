# Finding 28: how the game uses exceptions

Static analysis only: nothing was run, and no game bytes are
copied here (addresses, sizes and counts only). Script: `tools/scout/exceptions.py`.

Scope: the 24 DLLs the plan counts (23 in `bin/x64_retail` without `d3dcompiler_46.dll`, plus
`server.dll`). Of these, 16 are first-party (Respawn engine and game modules, VS2012, static
CRT) and 8 are middleware. `d3dcompiler_46.dll` (Microsoft) and `ui(11).dll` (pak DLL, no
handlers) are listed as extra rows and are not in any total.

## Answer

1. **What the handlers are.** 5,058 of 128,946 non-chained unwind entries (3.9%) in the 24 DLLs
   name a handler, using 96 distinct handler routines. By disassembly: 2,061 `__GSHandlerCheck`
   (stack-cookie check at unwind time, nothing else), 563 `__GSHandlerCheck_EH` and 1,101
   `__CxxFrameHandler3` (C++ EH), 9 `__GSHandlerCheck_SEH` and 1,243 `__C_specific_handler`
   (SEH), and 81 hand-written assembly handlers in `engine.dll` (36 routines, OpenSSL-style).
   Grouped as C++ / SEH / other these are 1,664 / 1,252 / 2,142, which is exactly what the
   earlier data-shape heuristic gave. Handlers only run while an exception is being dispatched
   or unwound, so none of them runs in an exception-free session.
2. **Real try/catch.** Of 1,566 C++ EH function bodies, 90 have a non-empty try-block map
   (client 24, server 24, engine 24, materialsystem 13, tier0 1, GFSDK_SSAO 4); the other 1,476
   only run destructors while unwinding (OriginSDK 1,239, materialsystem 190, engine 43, tier0 4).
   **All 98 catch handlers are `catch(...)`; there is no typed catch in the 24 DLLs.** 83 clean up
   and rethrow (`throw;`); 15 do not rethrow.
3. **Throws.** `_CxxThrowException` is linked into 7 first-party DLLs and has 208 call sites there:
   83 are the `throw;` inside the rethrowing catch blocks, 125 are new throws. Almost all of those
   are MSVC STL/CRT error paths (`bad_alloc`, `length_error`, `out_of_range`, RTTI casts, locale
   facets). Game-side: `tier0.dll` throws an `int` from `CThread::Stop`, and
   `filesystem_stdio.dll` has one custom `RaiseException(0x100)` in an error reporter. There are 25
   `RaiseException` call sites outside the throw routine in the 16 first-party DLLs: 16 FP-exception
   raisers (one per DLL), 5 in the CRT routine that runs catch blocks (it re-raises on a rethrow),
   3 delay-load failures in `server.dll` and that one `0x100`. Non-CRT `__except` functions: 1 of 133 (OpenSSL's entropy poll in
   `engine.dll`).
4. **Boilerplate.** 1,842 of the 3,180 handler-bearing functions in the 16 first-party DLLs
   (58%) are byte-identical in at least 12 of the 16, which is the static CRT: about 55 SEH
   functions (5 with an `__except`, the rest `__try/__finally` wrappers) plus about 60 cookie-check
   functions per DLL, and **no C++ EH**. All 323 C++ EH function bodies are in code that is
   present in only 1-4 DLLs.
5. **Port.** Aborting on any throw changes behaviour in at most 12 first-party functions: 10 with
   a `catch(...)` that does not rethrow, 1 non-CRT `__except`, and the `CThread::Stop` thrower.
   None of the 12 is known to run in single-player. The one designed use of an exception as
   control flow is `CThread::Stop` (throws the exit code) and `CThread::ThreadProc` (catches it);
   it fires only when a thread stops itself and a flag is set. Nothing in the binaries shows an
   exception thrown on an ordinary path. Whether the campaign ever trips one needs a runtime trace,
   which this survey could not do. Plan A (abort with a log) is supported; Plan B is not justified by
   the static evidence.

## Method

All of this is read from the binaries with `tools/scout/exceptions.py` (Capstone, the
`tools/inventory/pe_inventory.py` PE parser).

1. **Unwind tables.** Every `RUNTIME_FUNCTION` is read; unwind info is decoded for flags, handler
   RVA, handler data and chain parent. Chained entries (72,048) carry no handler of their own and
   are attributed to their root. "Function" below means a non-chained entry.
2. **Handler routines.** Each distinct handler RVA is identified from the routine itself: if it is
   a `jmp [iat]` thunk, by the import name; otherwise by disassembling it and testing structural
   features: immediates (`0xE06D7363`, `0x19930520-2`, mask `0x1FFFFFFF`), imports called
   (`RtlUnwindEx`, `RtlVirtualUnwind`), a call chain that reaches the security-cookie check
   (`rol 16` plus a compare against a global, or the load-config cookie), the `[r9+0x38]`
   HandlerData load, and for hand-written handlers the `CONTEXT` copy (`rep movsq`, 0x9a qwords)
   and `pushfq`. The language-specific data is then parsed with the layout that handler implies,
   which independently confirms the identification: 0 parse failures, every FuncInfo magic valid.
3. **FuncInfo.** x64 `FuncInfo` (40 bytes), unwind map, try-block map and handler arrays are parsed.
   Catch funclets are located through `HandlerType.dispOfHandler`; each is a separate unwind
   entry that shares its parent's FuncInfo (checked for all 114 funclets in the 25 DLLs that have any, `d3dcompiler_46.dll` included; 0
   mismatches).
4. **SEH.** The C scope table (begin, end, handler, jump target) is parsed. Jump target 0 is
   `__finally`; otherwise `__except`, with handler 1 meaning `EXCEPTION_EXECUTE_HANDLER` and any
   other value a filter funclet, whose constants and calls are scanned.
5. **Throw sites.** The static `_CxxThrowException` is found by structure: a small function that
   calls `RaiseException` and loads a data block whose first dword is `0xE06D7363` (in dynamic-CRT
   DLLs it is the import). Every raw `call/jmp rel32` and `call/jmp [iat]` byte pattern aimed at
   it (or at a `jmp [iat]` stub) is verified to land on an instruction boundary by disassembling the
   containing function. The thrown type comes from the `lea rdx` ThrowInfo before the call,
   followed through the catchable-type array to the RTTI type name; `xor ecx,ecx / xor edx,edx`
   marks a rethrow. `RaiseException` sites outside the throw routine are listed with the constant
   in `ecx` where there is one.
6. **Boilerplate vs game code.** Each handler-bearing function (and each function containing a
   throw or raise) is hashed with call targets and rip-relative displacements masked. Tiers by
   how many of the 16 first-party DLLs contain the same hash: A = 12 or more (the CRT), B = 5-11,
   C = 2-4, D = this DLL only. Tier alone does not say "CRT": every item outside A that matters
   was then identified by hand (table below).

The old heuristic (`tools/scout/teb_and_handlers.py`) reads the data after the handler. It is
now confirmed: its three totals equal the disassembly-based ones to the unit.

## 1. Handler routines

| Module | Linker | Unwind entries | Primary | Chained | With handler | Distinct handlers | GSHandlerCheck | GSHandlerCheck_EH | GSHandlerCheck_SEH | C_specific_handler | CxxFrameHandler3 | asm handler | other |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| client.dll | 11.0 | 46493 | 30028 | 16465 | 207 | 3 | 84 | 0 | 0 | 74 | 49 | 0 | 0 |
| server.dll | 11.0 | 59913 | 32663 | 27250 | 201 | 3 | 80 | 0 | 0 | 72 | 49 | 0 | 0 |
| engine.dll | 11.0 | 34812 | 22482 | 12330 | 710 | 40 | 432 | 0 | 4 | 100 | 93 | 81 | 0 |
| materialsystem_dx11.dll | 11.0 | 8451 | 6530 | 1921 | 434 | 4 | 135 | 74 | 0 | 80 | 145 | 0 | 0 |
| vguimatsurface.dll | 11.0 | 11484 | 7295 | 4189 | 197 | 2 | 134 | 0 | 0 | 63 | 0 | 0 | 0 |
| vphysics.dll | 11.0 | 7987 | 5353 | 2634 | 138 | 2 | 71 | 0 | 0 | 67 | 0 | 0 | 0 |
| vgui2.dll | 11.0 | 4942 | 3257 | 1685 | 211 | 3 | 139 | 0 | 2 | 70 | 0 | 0 | 0 |
| filesystem_stdio.dll | 11.0 | 3920 | 2891 | 1029 | 171 | 3 | 88 | 0 | 3 | 80 | 0 | 0 | 0 |
| datacache.dll | 11.0 | 3122 | 2311 | 811 | 124 | 2 | 68 | 0 | 0 | 56 | 0 | 0 | 0 |
| launcher.dll | 11.0 | 2190 | 1739 | 451 | 142 | 2 | 77 | 0 | 0 | 65 | 0 | 0 | 0 |
| studiorender.dll | 11.0 | 1759 | 1442 | 317 | 124 | 2 | 68 | 0 | 0 | 56 | 0 | 0 | 0 |
| localize.dll | 11.0 | 1893 | 1533 | 360 | 129 | 2 | 73 | 0 | 0 | 56 | 0 | 0 | 0 |
| vstdlib.dll | 11.0 | 2101 | 1700 | 401 | 123 | 2 | 67 | 0 | 0 | 56 | 0 | 0 | 0 |
| tier0.dll | 11.0 | 1801 | 1535 | 266 | 144 | 3 | 60 | 0 | 0 | 77 | 7 | 0 | 0 |
| inputsystem.dll | 11.0 | 1601 | 1340 | 261 | 123 | 2 | 67 | 0 | 0 | 56 | 0 | 0 | 0 |
| rtech_game.dll | 11.0 | 844 | 733 | 111 | 96 | 2 | 49 | 0 | 0 | 47 | 0 | 0 | 0 |
| OriginSDK.dll | 11.0 | 3406 | 2898 | 508 | 1302 | 4 | 57 | 489 | 0 | 6 | 750 | 0 | 0 |
| mileswin64.dll | 10.0 | 1446 | 885 | 561 | 122 | 2 | 91 | 0 | 0 | 31 | 0 | 0 | 0 |
| GFSDK_SSAO.win64.dll | 10.0 | 1096 | 1066 | 30 | 122 | 3 | 62 | 0 | 0 | 52 | 8 | 0 | 0 |
| bink2w64.dll | 9.0 | 811 | 477 | 334 | 65 | 2 | 49 | 0 | 0 | 16 | 0 | 0 | 0 |
| auditionwin64.dll | 10.0 | 254 | 215 | 39 | 48 | 2 | 34 | 0 | 0 | 14 | 0 | 0 | 0 |
| amd_ags_x64.dll | 12.0 | 275 | 247 | 28 | 50 | 2 | 27 | 0 | 0 | 23 | 0 | 0 | 0 |
| binkawin64.dll | 10.0 | 227 | 176 | 51 | 38 | 2 | 25 | 0 | 0 | 13 | 0 | 0 | 0 |
| GFSDK_TXAA.win64.dll | 10.0 | 166 | 150 | 16 | 37 | 2 | 24 | 0 | 0 | 13 | 0 | 0 | 0 |
| d3dcompiler_46.dll | 10.10 | 9440 | 4683 | 4757 | 613 | 4 | 561 | 2 | 0 | 11 | 39 | 0 | 0 |
| ui(11).dll | 11.0 | 1438 | 665 | 773 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| **total (24 DLLs)** |  | 200994 | 128946 | 72048 | 5058 | 96 | 2061 | 563 | 9 | 1243 | 1101 | 81 | 0 |

Distinct handler routines: client 3, server 3, engine 40 (the three CRT ones, one
`__GSHandlerCheck_SEH`, and 36 hand-written), materialsystem 4, OriginSDK 4 (two are
`jmp` thunks to `msvcr110.dll` imports: `__CxxFrameHandler3` and `__C_specific_handler`), the
rest 2 or 3. The full list is in the appendix.

| Routine | Distinct routines (24 DLLs) | Functions using it | DLLs | Sizes (bytes; 6 = import thunk) | How it was recognised | Examples |
|---|---|---|---|---|---|---|
| __GSHandlerCheck | 24 | 2061 | 24 | 29 | GS check only (reaches the security cookie), returns ContinueSearch | client@0x814d60 (29 B); engine@0x5260cc (29 B); materialsystem_dx11@0x17e704 (29 B) |
| __GSHandlerCheck_EH | 2 | 563 | 2 | 143 | GS check (reaches the security cookie), then chains to __CxxFrameHandler3 | materialsystem_dx11@0x1841b0 (143 B); OriginSDK@0xa87ec (143 B) |
| __GSHandlerCheck_SEH | 3 | 9 | 3 | 149 | GS check (reaches the security cookie), then chains to __C_specific_handler | engine@0x528f6c (149 B) |
| __C_specific_handler | 24 | 1243 | 24 | 478, 509, 6, 417 | walks a scope table, tests code 0xE06D7363, calls RtlUnwindEx | client@0x80b92c (478 B); engine@0x52e270 (478 B); materialsystem_dx11@0x187c90 (478 B) |
| __CxxFrameHandler3 | 7 | 1101 | 7 | 135, 6 | loads FuncInfo from HandlerData, calls __InternalCxxFrameHandler | client@0x807080 (135 B); engine@0x51cd18 (135 B); materialsystem_dx11@0x179a9c (135 B) |
| asm_se_handler | 36 | 81 | 1 | 285, 244, 768, 219 | hand-written leaf handler: reads CONTEXT.Rax/Rip, pushfq, no compiler prologue (copies a CONTEXT and calls RtlVirtualUnwind, possibly in a shared tail) | engine@0x566b00 (219 B); engine@0x59e120 (311 B); engine@0x5612e0 (333 B) |

What each one is:

- `__GSHandlerCheck`: 29 bytes. Loads `HandlerData` from `[r9+0x38]`, calls a 99-byte common
  check that reaches the cookie comparison, returns 1 (continue search). It never handles
  anything. 2,061 functions.
- `__GSHandlerCheck_EH` / `_SEH`: 143/149 bytes. The same cookie check, then a call into
  `__CxxFrameHandler3` / `__C_specific_handler`. They exist because a /GS function also has C++
  EH or SEH. 563 and 9 functions.
- `__C_specific_handler`: 478 bytes in the VS2012 static CRT (417-509 in the older toolchains;
  an import thunk in OriginSDK). Walks the scope table, tests code `0xE06D7363`, calls
  `RtlUnwindEx`.
- `__CxxFrameHandler3`: 135 bytes. Stores the image base in the thread block, loads the FuncInfo
  RVA from `HandlerData`, calls `__InternalCxxFrameHandler` (548 bytes in `client.dll`, recognised
  by the FuncInfo magics and the `0x1FFFFFFF` mask).
- Hand-written handlers (`engine.dll` only): 36 routines, 81 functions, no unwind entry of their
  own. They read `CONTEXT.Rax/Rip`, `pushfq`, copy the `CONTEXT` and call `RtlVirtualUnwind`. This is
  the style the OpenSSL assembly generator emits for its AES/SHA/bignum routines; `engine.dll`
  contains OpenSSL (58 "OpenSSL", 63 "EVP_", 44 "ssl3_" strings). Inferred, not named.

## 2. C++ EH: try/catch versus unwind-only

| Module | C++ EH unwind entries | of which catch funclets | function bodies | with try/catch | unwind only (destructor actions) | states, no actions | no states | try blocks | catch handlers | catch(...) | typed catch | funclets that rethrow | funclets that do not |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| client.dll | 49 | 25 | 24 | 24 | 0 | 0 | 0 | 25 | 25 | 25 | 0 | 25 | 0 |
| server.dll | 49 | 25 | 24 | 24 | 0 | 0 | 0 | 25 | 25 | 25 | 0 | 25 | 0 |
| engine.dll | 93 | 26 | 67 | 24 | 43 | 0 | 0 | 26 | 26 | 26 | 0 | 20 | 6 |
| materialsystem_dx11.dll | 219 | 16 | 203 | 13 | 190 | 0 | 0 | 16 | 16 | 16 | 0 | 12 | 4 |
| tier0.dll | 7 | 2 | 5 | 1 | 4 | 0 | 0 | 2 | 2 | 2 | 0 | 1 | 1 |
| OriginSDK.dll | 1239 | 0 | 1239 | 0 | 1239 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| GFSDK_SSAO.win64.dll | 8 | 4 | 4 | 4 | 0 | 0 | 0 | 4 | 4 | 4 | 0 | 0 | 4 |
| d3dcompiler_46.dll | 41 | 16 | 25 | 16 | 9 | 0 | 0 | 16 | 16 | 0 | 16 | 0 | 16 |
| **total (24 DLLs)** | 1664 | 98 | 1566 | 90 | 1476 | 0 | 0 | 98 | 98 | 98 | 0 | 83 | 15 |

"C++ EH unwind entries" is the old 1,664 figure (for the 24 DLLs; the extra `d3dcompiler_46.dll`
row is not in that sum). It is the sum of function bodies and catch funclets. In `client.dll`
and `server.dll` the 24 bodies are all try/catch and there is no destructor action anywhere, so the
rest of those two DLLs carries no C++ unwind data at all (inferred: their game code was built
without C++ exceptions, and only STL template code was built with them).

| Module | catch funclets | cleanup funclets (destructor actions) | SEH filter / __finally funclets | total distinct funclets |
|---|---|---|---|---|
| client.dll | 25 | 0 | 80 | 105 |
| server.dll | 25 | 0 | 78 | 103 |
| engine.dll | 26 | 108 | 110 | 244 |
| materialsystem_dx11.dll | 16 | 348 | 86 | 450 |
| vguimatsurface.dll | 0 | 0 | 69 | 69 |
| vphysics.dll | 0 | 0 | 73 | 73 |
| vgui2.dll | 0 | 0 | 78 | 78 |
| filesystem_stdio.dll | 0 | 0 | 90 | 90 |
| datacache.dll | 0 | 0 | 62 | 62 |
| launcher.dll | 0 | 0 | 71 | 71 |
| studiorender.dll | 0 | 0 | 62 | 62 |
| localize.dll | 0 | 0 | 62 | 62 |
| vstdlib.dll | 0 | 0 | 62 | 62 |
| tier0.dll | 2 | 4 | 83 | 89 |
| inputsystem.dll | 0 | 0 | 62 | 62 |
| rtech_game.dll | 0 | 0 | 53 | 53 |
| OriginSDK.dll | 0 | 1526 | 5 | 1531 |
| mileswin64.dll | 0 | 0 | 25 | 25 |
| GFSDK_SSAO.win64.dll | 4 | 0 | 58 | 62 |
| bink2w64.dll | 0 | 0 | 12 | 12 |
| auditionwin64.dll | 0 | 0 | 12 | 12 |
| amd_ags_x64.dll | 0 | 0 | 20 | 20 |
| binkawin64.dll | 0 | 0 | 11 | 11 |
| GFSDK_TXAA.win64.dll | 0 | 0 | 11 | 11 |
| d3dcompiler_46.dll | 16 | 75 | 17 | 108 |
| **total (24 DLLs)** | 98 | 1986 | 1335 | 3419 |

The funclet counts are what Plan B would have to lift as separate functions: 98 catch funclets,
1,986 cleanup funclets (1,526 of them in OriginSDK, 460 in engine, materialsystem and tier0),
1,335 SEH funclets (filters and `__finally` bodies, mostly the CRT's).

## 3. SEH

| Module | SEH functions | scope entries | __except scopes | __finally scopes | functions with __except | filter = catch-all | filter = funclet | filter tests 0xC0000005 | __except fns: tier A | B | C | D |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| client.dll | 74 | 89 | 14 | 75 | 14 | 4 | 10 | 5 | 5 | 9 | 0 | 0 |
| server.dll | 72 | 87 | 14 | 73 | 14 | 4 | 10 | 5 | 5 | 9 | 0 | 0 |
| engine.dll | 104 | 122 | 16 | 106 | 16 | 5 | 11 | 5 | 5 | 9 | 1 | 1 |
| materialsystem_dx11.dll | 80 | 96 | 14 | 82 | 14 | 4 | 10 | 5 | 5 | 9 | 0 | 0 |
| vguimatsurface.dll | 63 | 76 | 8 | 68 | 8 | 2 | 6 | 5 | 5 | 3 | 0 | 0 |
| vphysics.dll | 67 | 81 | 5 | 76 | 5 | 2 | 3 | 2 | 5 | 0 | 0 | 0 |
| vgui2.dll | 72 | 86 | 5 | 81 | 5 | 2 | 3 | 2 | 5 | 0 | 0 | 0 |
| filesystem_stdio.dll | 83 | 98 | 9 | 89 | 9 | 2 | 7 | 5 | 5 | 3 | 1 | 0 |
| datacache.dll | 56 | 67 | 5 | 62 | 5 | 2 | 3 | 2 | 5 | 0 | 0 | 0 |
| launcher.dll | 65 | 78 | 5 | 73 | 5 | 2 | 3 | 2 | 5 | 0 | 0 | 0 |
| studiorender.dll | 56 | 67 | 5 | 62 | 5 | 2 | 3 | 2 | 5 | 0 | 0 | 0 |
| localize.dll | 56 | 67 | 5 | 62 | 5 | 2 | 3 | 2 | 5 | 0 | 0 | 0 |
| vstdlib.dll | 56 | 67 | 5 | 62 | 5 | 2 | 3 | 2 | 5 | 0 | 0 | 0 |
| tier0.dll | 77 | 93 | 13 | 80 | 13 | 4 | 9 | 3 | 5 | 6 | 0 | 2 |
| inputsystem.dll | 56 | 67 | 5 | 62 | 5 | 2 | 3 | 2 | 5 | 0 | 0 | 0 |
| rtech_game.dll | 47 | 57 | 5 | 52 | 5 | 2 | 3 | 2 | 5 | 0 | 0 | 0 |
| OriginSDK.dll | 6 | 7 | 5 | 2 | 5 | 0 | 5 | 1 | - | - | - | - |
| mileswin64.dll | 31 | 36 | 4 | 32 | 4 | 1 | 3 | 2 | - | - | - | - |
| GFSDK_SSAO.win64.dll | 52 | 63 | 15 | 48 | 15 | 4 | 11 | 2 | - | - | - | - |
| bink2w64.dll | 16 | 18 | 7 | 11 | 7 | 2 | 5 | 3 | - | - | - | - |
| auditionwin64.dll | 14 | 16 | 4 | 12 | 4 | 1 | 3 | 2 | - | - | - | - |
| amd_ags_x64.dll | 23 | 27 | 3 | 24 | 3 | 1 | 2 | 1 | - | - | - | - |
| binkawin64.dll | 13 | 15 | 3 | 12 | 3 | 1 | 2 | 1 | - | - | - | - |
| GFSDK_TXAA.win64.dll | 13 | 15 | 3 | 12 | 3 | 1 | 2 | 1 | - | - | - | - |
| d3dcompiler_46.dll | 11 | 19 | 15 | 4 | 8 | 2 | 13 | 1 | - | - | - | - |
| **total (24 DLLs)** | 1252 | 1495 | 177 | 1318 | 177 | 54 | 123 | 64 | 80 | 48 | 2 | 3 |

SEH functions: 1,252 in the 24 DLLs. Their 177 `__except` scopes sit in 177 functions; the
other 1,318 scopes are `__finally`. In the 16 first-party DLLs there are 133 functions with
an `__except`, 80 of them tier A.

## 4. Throw sites and raise sites

| Module | _CxxThrowException | throw call sites | containing functions | of which rethrow (`throw;`) | new throws | types thrown (top) | RaiseException sites outside it | constant codes |
|---|---|---|---|---|---|---|---|---|
| client.dll | static | 40 | 37 | 25 | 15 | std::__non_rtti_object x4, std::bad_alloc x1, std::bad_function_call x1, std::invalid_argument x1 | 2 |  |
| server.dll | static | 40 | 37 | 25 | 15 | std::__non_rtti_object x4, std::bad_alloc x1, std::bad_function_call x1, std::invalid_argument x1 | 5 | 0xc06d0057, 0xc06d007e, 0xc06d007f |
| engine.dll | static | 35 | 32 | 20 | 15 | std::__non_rtti_object x4, std::bad_alloc x1, std::bad_function_call x1, std::invalid_argument x1 | 2 |  |
| materialsystem_dx11.dll | static | 69 | 65 | 12 | 57 | std::bad_cast x40, std::__non_rtti_object x4, std::runtime_error x2, std::range_error x2 | 2 |  |
| vguimatsurface.dll | static | 6 | 3 | 0 | 6 | std::__non_rtti_object x4, std::bad_cast x1, std::bad_typeid x1 | 1 |  |
| vphysics.dll | - | 0 | 0 | 0 | 0 |  | 1 |  |
| vgui2.dll | - | 0 | 0 | 0 | 0 |  | 1 |  |
| filesystem_stdio.dll | static | 6 | 3 | 0 | 6 | std::__non_rtti_object x4, std::bad_cast x1, std::bad_typeid x1 | 2 | 0x100 |
| datacache.dll | - | 0 | 0 | 0 | 0 |  | 1 |  |
| launcher.dll | - | 0 | 0 | 0 | 0 |  | 1 |  |
| studiorender.dll | - | 0 | 0 | 0 | 0 |  | 1 |  |
| localize.dll | - | 0 | 0 | 0 | 0 |  | 1 |  |
| vstdlib.dll | - | 0 | 0 | 0 | 0 |  | 1 |  |
| tier0.dll | static | 12 | 12 | 1 | 11 | std::bad_alloc x2, .H x1, std::bad_function_call x1, std::invalid_argument x1 | 2 |  |
| inputsystem.dll | - | 0 | 0 | 0 | 0 |  | 1 |  |
| rtech_game.dll | - | 0 | 0 | 0 | 0 |  | 1 |  |
| OriginSDK.dll | import | 140 | 110 | 0 | 140 | ?_Xlength_error@std@@YAXPEBD@Z x124, ?_Xout_of_range@std@@YAXPEBD@Z x15, ?_Xbad_alloc@std@@YAXXZ x1 | 3 | 0x406d1388 |
| mileswin64.dll | static | 1 | 1 | 0 | 1 | std::bad_alloc x1 | 3 | 0x406d1388 |
| GFSDK_SSAO.win64.dll | static | 52 | 17 | 0 | 52 | std::runtime_error x50, std::bad_alloc x1, std::bad_exception x1 | 2 |  |
| bink2w64.dll | - | 0 | 0 | 0 | 0 |  | 5 | 0x406d1388, 0xc06d0057, 0xc06d007e, 0xc06d007f |
| auditionwin64.dll | - | 0 | 0 | 0 | 0 |  | 2 | 0x406d1388 |
| amd_ags_x64.dll | static | 1 | 1 | 0 | 1 | std::bad_alloc x1 | 0 |  |
| binkawin64.dll | - | 0 | 0 | 0 | 0 |  | 0 |  |
| GFSDK_TXAA.win64.dll | - | 0 | 0 | 0 | 0 |  | 0 |  |
| d3dcompiler_46.dll | import | 488 | 72 | 0 | 488 | .J x488 | 0 |  |
| ui(11).dll | - | 0 | 0 | 0 | 0 |  | 0 |  |

| Import | call sites, 16 first-party DLLs | DLLs | call sites, 8 third-party DLLs |
|---|---|---|---|
| ?_Xbad_alloc@std@@YAXXZ | 0 | 0 | 1 |
| ?_Xlength_error@std@@YAXPEBD@Z | 0 | 0 | 124 |
| ?_Xout_of_range@std@@YAXPEBD@Z | 0 | 0 | 15 |
| IsDebuggerPresent | 50 | 16 | 20 |
| RaiseException | 25 | 16 | 15 |
| RtlCaptureContext | 43 | 16 | 17 |
| RtlLookupFunctionEntry | 37 | 16 | 16 |
| RtlPcToFileHeader | 23 | 7 | 1 |
| RtlUnwindEx | 44 | 16 | 15 |
| RtlVirtualUnwind | 32 | 16 | 15 |
| RtlVirtualUnwind (asm, outside unwind table) | 24 | 1 | 0 |
| SetUnhandledExceptionFilter | 16 | 16 | 22 |
| TerminateProcess | 5 | 3 | 8 |
| UnhandledExceptionFilter | 0 | 0 | 13 |
| _CxxThrowException | 208 | 7 | 54 |
| __CxxFrameHandler3 | 0 | 0 | 1 |
| __crtUnhandledException | 0 | 0 | 1 |

The `RaiseException` sites in the 16 first-party DLLs, by function: 16 in one 771-byte routine
present in every DLL, which raises the floating-point exception codes `0xC000008E-0xC0000093`
(the CRT's FP-fault raiser; FP exceptions are masked by default, so it does not fire); 5 in the
515-byte routine that runs catch blocks (client, server, engine, materialsystem, tier0), which
re-raises the original exception record on a rethrow; 3 delay-load failures in `server.dll`; and
the `0x100` in `filesystem_stdio.dll`.

Call sites are direct `call/jmp rel32` and `call [iat]`, plus calls to a `jmp [iat]` stub.
Calls through registers are not resolved (the throw routine and the Win32 imports are never
called that way in these binaries). Raw byte matches rejected as non-instructions: 0-3 per DLL.

## 5. CRT boilerplate versus game code

| Module | Cookie-check only (GSHandlerCheck) | SEH functions | tier A/B/C/D | C++ EH function bodies (no catch funclets) | tier A/B/C/D | hand-written asm handlers |
|---|---|---|---|---|---|---|
| client.dll | 84 | 74 | 56/16/0/2 | 24 | 0/0/24/0 | 0 |
| server.dll | 80 | 72 | 55/16/0/1 | 24 | 0/0/24/0 | 0 |
| engine.dll | 432 | 104 | 55/20/17/12 | 67 | 0/0/3/64 | 81 |
| materialsystem_dx11.dll | 135 | 80 | 55/20/3/2 | 203 | 0/0/1/202 | 0 |
| vguimatsurface.dll | 134 | 63 | 55/7/0/1 | 0 | 0/0/0/0 | 0 |
| vphysics.dll | 71 | 67 | 55/8/1/3 | 0 | 0/0/0/0 | 0 |
| vgui2.dll | 139 | 72 | 55/8/7/2 | 0 | 0/0/0/0 | 0 |
| filesystem_stdio.dll | 88 | 83 | 55/12/12/4 | 0 | 0/0/0/0 | 0 |
| datacache.dll | 68 | 56 | 55/0/0/1 | 0 | 0/0/0/0 | 0 |
| launcher.dll | 77 | 65 | 55/4/5/1 | 0 | 0/0/0/0 | 0 |
| studiorender.dll | 68 | 56 | 55/0/0/1 | 0 | 0/0/0/0 | 0 |
| localize.dll | 73 | 56 | 55/0/0/1 | 0 | 0/0/0/0 | 0 |
| vstdlib.dll | 67 | 56 | 55/0/0/1 | 0 | 0/0/0/0 | 0 |
| tier0.dll | 60 | 77 | 53/15/4/5 | 5 | 0/0/1/4 | 0 |
| inputsystem.dll | 67 | 56 | 55/0/0/1 | 0 | 0/0/0/0 | 0 |
| rtech_game.dll | 49 | 47 | 46/0/0/1 | 0 | 0/0/0/0 | 0 |
| **total** | 1692 | 1084 |  | 323 |  | 81 |

Reading it: every DLL carries the same 117-119 handler-bearing CRT functions in tier A (`tier0`
104 and `rtech_game` 90, probably because less of the CRT is linked): 55 SEH functions and the
rest cookie checks. No tier A or B function uses C++ EH. All 323 first-party C++ EH function
bodies are in tiers C and D (client 24 and server 24 byte-identical, 5 more shared by two or
three DLLs, 270 in one DLL only). Of the 1,084 first-party SEH functions, 870 are tier A and 126
tier B (C++ runtime and stdio/locale pieces that only some DLLs link); 49 are tier C and 39 tier D
(mostly `__finally` wrappers, plus the items in the table below).

The same tiers applied to the items that carry exception semantics. "Swallows" means at least one
catch handler of the function does not rethrow:

| Module | functions with try/catch | by tier A/B/C/D | of which a catch swallows | functions with __except | by tier A/B/C/D | new-throw sites | by tier A/B/C/D | RaiseException sites |
|---|---|---|---|---|---|---|---|---|
| client.dll | 24 | 0/0/24/0 | 0 | 14 | 5/9/0/0 | 15 | 0/15/0/0 | 2 |
| server.dll | 24 | 0/0/24/0 | 0 | 14 | 5/9/0/0 | 15 | 0/15/0/0 | 5 |
| engine.dll | 24 | 0/0/1/23 | 5 | 16 | 5/9/1/1 | 15 | 0/15/0/0 | 2 |
| materialsystem_dx11.dll | 13 | 0/0/1/12 | 4 | 14 | 5/9/0/0 | 57 | 0/15/0/42 | 2 |
| vguimatsurface.dll | 0 | 0/0/0/0 | 0 | 8 | 5/3/0/0 | 6 | 0/6/0/0 | 1 |
| vphysics.dll | 0 | 0/0/0/0 | 0 | 5 | 5/0/0/0 | 0 | 0/0/0/0 | 1 |
| vgui2.dll | 0 | 0/0/0/0 | 0 | 5 | 5/0/0/0 | 0 | 0/0/0/0 | 1 |
| filesystem_stdio.dll | 0 | 0/0/0/0 | 0 | 9 | 5/3/1/0 | 6 | 0/6/0/0 | 2 |
| datacache.dll | 0 | 0/0/0/0 | 0 | 5 | 5/0/0/0 | 0 | 0/0/0/0 | 1 |
| launcher.dll | 0 | 0/0/0/0 | 0 | 5 | 5/0/0/0 | 0 | 0/0/0/0 | 1 |
| studiorender.dll | 0 | 0/0/0/0 | 0 | 5 | 5/0/0/0 | 0 | 0/0/0/0 | 1 |
| localize.dll | 0 | 0/0/0/0 | 0 | 5 | 5/0/0/0 | 0 | 0/0/0/0 | 1 |
| vstdlib.dll | 0 | 0/0/0/0 | 0 | 5 | 5/0/0/0 | 0 | 0/0/0/0 | 1 |
| tier0.dll | 1 | 0/0/0/1 | 1 | 13 | 5/6/0/2 | 11 | 0/9/0/2 | 2 |
| inputsystem.dll | 0 | 0/0/0/0 | 0 | 5 | 5/0/0/0 | 0 | 0/0/0/0 | 1 |
| rtech_game.dll | 0 | 0/0/0/0 | 0 | 5 | 5/0/0/0 | 0 | 0/0/0/0 | 1 |

Everything with exception semantics outside tiers A and B, identified by hand:

| Item | Where | What it is | Basis | Status |
|---|---|---|---|---|
| `CThread::Stop` | tier0 `0x1b730` (export `?Stop@CThread@@QEAAXH@Z`) | Throws an `int` (the exit code) when called on the thread's own object and bit 0 of the flags dword at `+0x80` is set; otherwise cleans up | ThrowInfo resolves to type `int`; flag test and self-thread test read in the code | type and location verified, behaviour read from code |
| `CThread::ThreadProc` | tier0 `0x1b890` (export) | The matching catch: two `catch(...)`, one rethrows, the other (`0x4ec18`) is an empty body | FuncInfo, funclet bodies | verified |
| `std::bad_alloc` thrower | tier0 `0x263c8` | 105 bytes, 25 direct callers | ThrowInfo type, call sites | verified; purpose (allocation failure) inferred |
| `CThread::Stop` caller | engine `0x157c70` | The only call to `Stop` from another DLL (via import; tier0's own `~CThread` also calls it), passes exit code 0 after setting a byte in the object | import xref | verified; probably a shutdown path on another thread (inferred), where `Stop` does not throw |
| entropy poll | engine `0x3597c0` | `__try {...} __except(1)` around Windows entropy gathering (loads `NetStatisticsGet`, `CryptGenRandom`, Toolhelp heap walk by name) | strings and imports in the function | inferred: OpenSSL `RAND_poll` |
| hand-written handlers | engine, 36 routines, 81 functions | Unwind support for assembly routines | structure (see above) | inferred: OpenSSL |
| diagnostics guards | engine `0x1c4620`, `0x1c78f0`, `0x1deac0` | `catch(...)` that swallows. Strings: a memory-status report, convar names, `-nominidumps`, `Plat_IsInDebugSession` | strings and callee names | inferred: crash/diagnostic reporting |
| nothrow-new wrapper | engine `0x525dc4` | 31 bytes, swallowing `catch(...)`, in the CRT address range | size and position | inferred: `operator new(nothrow)` |
| allocate-retry | engine `0x30560`, materialsystem `0x12530` (identical), materialsystem `0x1550a0`, `0x1551b8`, `0x17680` | one `catch(...)` that swallows and retries the allocation, one that rethrows | funclet bodies, `_Xbad_alloc` call | inferred: string/vector growth |
| error reporter | filesystem_stdio `0x7e5f0` | formats a message, then `RaiseException(0x100, 0, 0, NULL)` if one global flag is set, else an exit path; at least 9 direct callers | disassembly | structure verified, purpose inferred |
| delay-load helper | server `0x82f468` | raises `0xC06D0057/7E/7F` (Visual C++ delay-load failures) only when a delay-loaded DLL or function is missing | codes verified | meaning inferred |
| `<locale>` throwers | materialsystem: 40 functions of 304 bytes throwing `std::bad_cast`; `0xfe80` (`runtime_error`, "bad locale name"); `0x14110` (`range_error`, "string too long", "bad conversion") | iostream/locale/string-conversion template code | strings, ThrowInfo types | inferred: STL |
| STL try/catch | client and server, 24 bodies each, byte-identical in the two | node/array allocation with `catch(...) { free; throw; }` | 2 read by hand, 10 of 24 call `std::_Xbad_alloc` | inferred: STL |
| CRT `__except` outside tier A/B | tier0 `0x27bac` (heap walk, AV filter), tier0 `0x28160` (array-unwind helper), engine `0x526eb4` and filesystem `0x8dad8` (thread start wrapper) | CRT pieces linked into only 1-2 DLLs | structure | inferred: `_heapwalk`, `__ArrayUnwind`, `_threadstartex` |

Middleware, for completeness (not in the first-party totals):

- Seven `RaiseException(0x406D1388)` (thread naming) in OriginSDK (3), Miles (2), Bink2 (1),
  Audition (1). The 227-byte variant (Miles, Bink2, Audition) resolves `IsDebuggerPresent` at run time and
  returns without raising when no debugger is attached (read for Bink2; same size in the other
  two). OriginSDK's contain a direct `IsDebuggerPresent` call and sit in an `__except` scope.
- Bink2 `0x43e40` has its own delay-load helper (3 raises, same codes as `server.dll`).
- GFSDK_SSAO: 52 throw sites (50 `runtime_error`) and 4 `catch(...)` that do not rethrow. NVIDIA
  code using exceptions for error returns. The plan stubs the GFSDK DLLs, so this does not matter.
- OriginSDK: 1,239 unwind-only C++ functions, no catch, 140 throws of `length_error`/`out_of_range`/
  `bad_alloc` through `msvcp110.dll` imports. An exception escaping it has no handler inside it.
- `d3dcompiler_46.dll` (extra): 488 `throw` sites of a `long` (an HRESULT) in 72 functions and 16
  typed `catch(long)` that do not rethrow. It is the only module here that uses exceptions as
  ordinary error flow. If it is lifted rather than replaced, it needs real exception support.

## 6. Bottom line for the port

- **If every throw aborted,** the handler tables (5,058 entries, 96 routines) are inert: nothing in
  them runs without an exception. Behaviour changes only where a path can *survive* an exception:
  10 first-party functions with a swallowing `catch(...)` (engine 5, materialsystem 4, tier0 1),
  1 non-CRT `__except` (engine, OpenSSL), plus the one thrower, `CThread::Stop`: 12 functions.
  There is no typed catch, so no code distinguishes exception types.
- **Signs of throws on ordinary paths:** none found. The throw sites are error paths (allocation
  failure, container length, bad cast, fatal-error reporting, missing delay-loaded DLL) and a
  catch-and-clean-up idiom. The single designed use is the thread stop protocol in `tier0.dll`.
- **Open question that needs a trace:** whether any `CThread` subclass sets flag bit 0 (the
  constructor only initialises the field; subclasses live in other modules) and stops itself. If
  one does, aborting breaks that thread's exit. It would be cheap to handle that one case
  specifically (a lifted `throw`/`catch` pair around `ThreadProc`) rather than building general
  unwinding. Everything else can abort with a log.
- **If Plan B is ever needed,** the work is bounded: 98 catch funclets, 1,986 cleanup funclets,
  1,335 SEH funclets in the 24 DLLs, no type matching to implement (every catch is `catch(...)`),
  and every throw goes through `_CxxThrowException` or `RaiseException`.
- The engine's 81 assembly handlers sit beside the OpenSSL routines they cover. They are only
  called with an exception in flight inside those routines.

## Verified, inferred, not determined

Verified from the binaries: identity of all 96 handler routines (and agreement with the earlier
data-shape counts); FuncInfo and scope-table parses (0 inconsistencies; every catch funclet shares its parent's FuncInfo; 83
rethrow sites equal the 83 rethrowing funclets); the throw routine and every call site, thrown
types, rethrow versus new throw; `RaiseException` constants; the exact counts above.

Inferred: that tier A is "the CRT" (identical bytes in 12 or more DLLs, which is what static
linking gives; no symbols exist to confirm); the hand-identification of the table's items (strings
and structure, not names); OpenSSL for the assembly handlers and the entropy poll; that the
client/server try/catch bodies are STL; "rethrow" for a funclet means a `_CxxThrowException(0,0)`
inside its own unwind range (funclets that tail-jump elsewhere would read as non-rethrowing; 4 of the
15 were read by hand and really are swallowing).

Not determined:

- Whether anything here executes in the campaign. That needs a runtime trace (Proton, or the
  harness once it boots). Candidates worth tracing: `CThread::Stop` (`tier0.dll` `0x1b730`),
  `RaiseException` in `filesystem_stdio.dll` `0x7e5f0`, and the delay-load helper in `server.dll`.
- Hardware traps outside SEH (`int 29h`, `ud2`, `int3` markers): the census counts 135 `int n` and 1
  `ud2`, not classified here.
- Faults handled by `__except` bodies (access violations as control flow): only the `__except`
  scopes are visible; every filter that tests `0xC0000005` is a CRT RTTI helper.
- Indirect calls to a throw routine or to `CThread::Stop` through vtables or registers.
- Who sets the stop-protocol flag on `CThread` subclasses in other modules.
- The tier of a function that is identical in two DLLs but is game code (client/server) rather than
  CRT: judged by hand only for items that have exception semantics.

## Reproduce

    .venv/bin/python tools/scout/exceptions.py analyze game --cache <dir> -j 4     # about 3 s
    .venv/bin/python tools/scout/exceptions.py report --cache <dir> --split <dir2>  # one file per table
    .venv/bin/python tools/scout/exceptions.py report --cache <dir> --max-items 60  # also the item lists

## Appendix: every distinct handler routine

| Module | Handler RVA | Bytes | Has unwind entry | Identified as | Functions using it |
|---|---|---|---|---|---|
| client.dll | 0x814d60 | 29 | yes | __GSHandlerCheck | 84 |
| client.dll | 0x80b92c | 478 | yes | __C_specific_handler | 74 |
| client.dll | 0x807080 | 135 | yes | __CxxFrameHandler3 | 49 |
| server.dll | 0x7ef318 | 29 | yes | __GSHandlerCheck | 80 |
| server.dll | 0x7e71a4 | 478 | yes | __C_specific_handler | 72 |
| server.dll | 0x7e6908 | 135 | yes | __CxxFrameHandler3 | 49 |
| engine.dll | 0x5260cc | 29 | yes | __GSHandlerCheck | 432 |
| engine.dll | 0x52e270 | 478 | yes | __C_specific_handler | 100 |
| engine.dll | 0x51cd18 | 135 | yes | __CxxFrameHandler3 | 93 |
| engine.dll | 0x566b00 | 219 | no | asm_se_handler | 5 |
| engine.dll | 0x59e120 | 311 | no | asm_se_handler | 5 |
| engine.dll | 0x528f6c | 149 | yes | __GSHandlerCheck_SEH | 4 |
| engine.dll | 0x5612e0 | 333 | no | asm_se_handler | 4 |
| engine.dll | 0x569c00 | 274 | no | asm_se_handler | 4 |
| engine.dll | 0x581e30 | 285 | no | asm_se_handler | 4 |
| engine.dll | 0x5949b0 | 339 | no | asm_se_handler | 4 |
| engine.dll | 0x5999c0 | 305 | no | asm_se_handler | 4 |
| engine.dll | 0x56da40 | 462 | no | asm_se_handler | 3 |
| engine.dll | 0x56dab0 | 350 | no | asm_se_handler | 3 |
| engine.dll | 0x573940 | 336 | no | asm_se_handler | 3 |
| engine.dll | 0x5786f0 | 277 | no | asm_se_handler | 3 |
| engine.dll | 0x580300 | 244 | no | asm_se_handler | 3 |
| engine.dll | 0x584d20 | 340 | no | asm_se_handler | 3 |
| engine.dll | 0x58d070 | 244 | no | asm_se_handler | 3 |
| engine.dll | 0x58f570 | 520 | no | asm_se_handler | 3 |
| engine.dll | 0x565900 | 665 | no | asm_se_handler | 2 |
| engine.dll | 0x5659b0 | 489 | no | asm_se_handler | 2 |
| engine.dll | 0x56ebc0 | 285 | no | asm_se_handler | 2 |
| engine.dll | 0x570cc0 | 229 | no | asm_se_handler | 2 |
| engine.dll | 0x5956a0 | 371 | no | asm_se_handler | 2 |
| engine.dll | 0x597880 | 288 | no | asm_se_handler | 2 |
| engine.dll | 0x561430 | 768 | no | asm_se_handler | 1 |
| engine.dll | 0x561e20 | 242 | no | asm_se_handler | 1 |
| engine.dll | 0x562840 | 269 | no | asm_se_handler | 1 |
| engine.dll | 0x5628c0 | 141 | no | asm_se_handler | 1 |
| engine.dll | 0x565a60 | 313 | no | asm_se_handler | 1 |
| engine.dll | 0x56db20 | 238 | no | asm_se_handler | 1 |
| engine.dll | 0x578600 | 517 | no | asm_se_handler | 1 |
| engine.dll | 0x578690 | 373 | no | asm_se_handler | 1 |
| engine.dll | 0x580400 | 768 | no | asm_se_handler | 1 |
| engine.dll | 0x58d170 | 586 | no | asm_se_handler | 1 |
| engine.dll | 0x58f620 | 344 | no | asm_se_handler | 1 |
| engine.dll | 0x595700 | 275 | no | asm_se_handler | 1 |
| engine.dll | 0x59cd50 | 253 | no | asm_se_handler | 1 |
| engine.dll | 0x59e790 | 232 | no | asm_se_handler | 1 |
| engine.dll | 0x5a0250 | 260 | no | asm_se_handler | 1 |
| materialsystem_dx11.dll | 0x179a9c | 135 | yes | __CxxFrameHandler3 | 145 |
| materialsystem_dx11.dll | 0x17e704 | 29 | yes | __GSHandlerCheck | 135 |
| materialsystem_dx11.dll | 0x187c90 | 478 | yes | __C_specific_handler | 80 |
| materialsystem_dx11.dll | 0x1841b0 | 143 | yes | __GSHandlerCheck_EH | 74 |
| vguimatsurface.dll | 0x117b74 | 29 | yes | __GSHandlerCheck | 134 |
| vguimatsurface.dll | 0x110ea8 | 478 | yes | __C_specific_handler | 63 |
| vphysics.dll | 0x160cc4 | 29 | yes | __GSHandlerCheck | 71 |
| vphysics.dll | 0x159e94 | 478 | yes | __C_specific_handler | 67 |
| vgui2.dll | 0x6d328 | 29 | yes | __GSHandlerCheck | 139 |
| vgui2.dll | 0x62e4c | 478 | yes | __C_specific_handler | 70 |
| vgui2.dll | 0x8bf08 | 149 | yes | __GSHandlerCheck_SEH | 2 |
| filesystem_stdio.dll | 0x964b4 | 29 | yes | __GSHandlerCheck | 88 |
| filesystem_stdio.dll | 0x8f04c | 478 | yes | __C_specific_handler | 80 |
| filesystem_stdio.dll | 0xb7a30 | 149 | yes | __GSHandlerCheck_SEH | 3 |
| datacache.dll | 0xa4284 | 29 | yes | __GSHandlerCheck | 68 |
| datacache.dll | 0x9c9f0 | 478 | yes | __C_specific_handler | 56 |
| launcher.dll | 0x40e4c | 29 | yes | __GSHandlerCheck | 77 |
| launcher.dll | 0x3b158 | 478 | yes | __C_specific_handler | 65 |
| studiorender.dll | 0x49860 | 29 | yes | __GSHandlerCheck | 68 |
| studiorender.dll | 0x3d8d8 | 478 | yes | __C_specific_handler | 56 |
| localize.dll | 0x3b560 | 29 | yes | __GSHandlerCheck | 73 |
| localize.dll | 0x32c9c | 478 | yes | __C_specific_handler | 56 |
| vstdlib.dll | 0x3d5f8 | 29 | yes | __GSHandlerCheck | 67 |
| vstdlib.dll | 0x3491c | 478 | yes | __C_specific_handler | 56 |
| tier0.dll | 0x2b91c | 478 | yes | __C_specific_handler | 77 |
| tier0.dll | 0x346ec | 29 | yes | __GSHandlerCheck | 60 |
| tier0.dll | 0x28958 | 135 | yes | __CxxFrameHandler3 | 7 |
| inputsystem.dll | 0x32c28 | 29 | yes | __GSHandlerCheck | 67 |
| inputsystem.dll | 0x275ec | 478 | yes | __C_specific_handler | 56 |
| rtech_game.dll | 0x141c8 | 29 | yes | __GSHandlerCheck | 49 |
| rtech_game.dll | 0x16a30 | 478 | yes | __C_specific_handler | 47 |
| OriginSDK.dll | 0xa88b0 | 6 | no (import thunk) | __CxxFrameHandler3 | 750 |
| OriginSDK.dll | 0xa87ec | 143 | yes | __GSHandlerCheck_EH | 489 |
| OriginSDK.dll | 0xa8768 | 29 | yes | __GSHandlerCheck | 57 |
| OriginSDK.dll | 0xa8d46 | 6 | no (import thunk) | __C_specific_handler | 6 |
| mileswin64.dll | 0x428b4 | 29 | yes | __GSHandlerCheck | 91 |
| mileswin64.dll | 0x43660 | 509 | yes | __C_specific_handler | 31 |
| GFSDK_SSAO.win64.dll | 0x2eaf0 | 29 | yes | __GSHandlerCheck | 62 |
| GFSDK_SSAO.win64.dll | 0x35878 | 509 | yes | __C_specific_handler | 52 |
| GFSDK_SSAO.win64.dll | 0x2f628 | 135 | yes | __CxxFrameHandler3 | 8 |
| bink2w64.dll | 0x42584 | 29 | yes | __GSHandlerCheck | 49 |
| bink2w64.dll | 0x3fd1c | 417 | yes | __C_specific_handler | 16 |
| auditionwin64.dll | 0xb6a0 | 29 | yes | __GSHandlerCheck | 34 |
| auditionwin64.dll | 0xa64c | 509 | yes | __C_specific_handler | 14 |
| amd_ags_x64.dll | 0x4d64 | 29 | yes | __GSHandlerCheck | 27 |
| amd_ags_x64.dll | 0x9ce0 | 481 | yes | __C_specific_handler | 23 |
| binkawin64.dll | 0xb180 | 29 | yes | __GSHandlerCheck | 25 |
| binkawin64.dll | 0x7630 | 509 | yes | __C_specific_handler | 13 |
| GFSDK_TXAA.win64.dll | 0x44cc | 29 | yes | __GSHandlerCheck | 24 |
| GFSDK_TXAA.win64.dll | 0x5884 | 509 | yes | __C_specific_handler | 13 |
| d3dcompiler_46.dll | 0x34f898 | 29 | yes | __GSHandlerCheck | 561 |
| d3dcompiler_46.dll | 0x34f9a2 | 6 | no (import thunk) | __CxxFrameHandler3 | 39 |
| d3dcompiler_46.dll | 0x2b8580 | 6 | no (import thunk) | __C_specific_handler | 11 |
| d3dcompiler_46.dll | 0x34f9b0 | 143 | yes | __GSHandlerCheck_EH | 2 |
