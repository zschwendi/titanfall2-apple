# 17. TLS and gs: what the game reads through the thread block

Survey: `tools/scout/tls_gs_scout.py` (run `.venv/bin/python tools/scout/tls_gs_scout.py game`, about 8 minutes,
four workers). Addresses are RVAs. Scope: `server.dll` plus the 23 DLLs in `game/bin/x64_retail` other than
`d3dcompiler_46.dll` (24 modules). `game/r2/paks/Win64/ui(11).dll` is outside that set; the old scan found no gs
operands in it.

## Bottom line

- The 277 gs operands in the unwind ranges split into 244 that are reached code and 33 that are not. The 33 are
  32 register-based operands and one `gs:[0x58]`. None of the 32 register-based operands is code: they are data
  that linear decoding ran into. The brief's "17 computed" is the in-sequence part of those 32.
- Reached code has 270 gs operands, all with fixed offsets: 0x58 (240), 0x10 (24), 0x30 (5), 0x08 (1). No
  register-based gs access is reached, so the lifter needs no general gs-plus-register support.
- gs:[0x58] follows the MSVC static-TLS pattern in 233 of 240 reached accesses. The other 7 (materialsystem_dx11 2,
  vphysics 5) use an index that is not a `_tls_index` load in their neighbourhood.
- 17 modules have a TLS directory, all with zero callbacks and zero fill. The per-thread static TLS total is
  66,153 bytes.
- gs:[0x10] is a stack-limit probe, one helper per module, identical in all 24.
- All 24 modules import TLS/FLS APIs. Calls are reached in 8 modules (101 call sites); the other 16 import them
  and never reach a call.

## Reconciling the 277

| Bucket | Operands | Notes |
|---|---|---|
| In sequence (decoded from function start up to the first undecodable byte) | 262 | 217 gs:[0x58], 24 gs:[0x10], 4 gs:[0x30], 17 register-based |
| After a decode failure (resync) | 15 | all register-based; all in data |
| **Total in unwind ranges** | **277** | matches the brief |
| Reached (code), fixed offset | 244 | 216 gs:[0x58], 24 gs:[0x10], 4 gs:[0x30] |
| Not reached, register-based | 32 | 17 in sequence + 15 after failure; see section 1 |
| Not reached, fixed offset | 1 | gs:[0x58] in materialsystem_dx11 at RVA 0x95a9f (section 2) |

Code with no unwind entry (outside the 277): 26 reached gs operands (24 gs:[0x58], 1 gs:[0x30] in OriginSDK, 1 gs:[0x08]
in tier0). The raw byte scan finds 343 gs:[0x58] absolute sites in total: 240 reached, 103 not reached (102 in code
with no unwind entry that no entry point reaches, 1 the materialsystem site above). 24 gs:[0x10] and 5 gs:[0x30]
sites in the byte scan are all reached.

Why 277 and not 262: the resync sweep (decode, skip one byte at an undecodable byte, continue) gives 277, and the
linear sweep gives 262. The extra 15 come from decoding data after a failure. Reachability separates the two: the
15 are not reached and not in any call or jump path.

## 1. Register-based gs operands

All 32 are data, not code. Each one's base register has no defining instruction in the decoded neighbourhood or
is set by a byte pattern that is data (for example `adc eax, 0x31000265` and repeated `add al, byte ptr gs:[rax]`
at 8-byte spacing, which is a table of 32-bit offsets). Verdicts:

- **Unrooted** (5): the containing unwind range starts in data, is not called, not exported and not pointed to
  by a relocation, so no entry point reaches it. Client 0x26c38 (1), client 0x651d58 (1), localize 0x266c8 (3).
- **Unproven** (3): the function contains a register-form indirect jump the survey could not resolve, so reachability
  is not proven. The bytes around them are data. Materialsystem 0x1264b0 (2), vstdlib 0x2f5f0 (1).
- **Not reached** (24): reachable from no entry point.

| Module | Range (RVA) | Instruction (RVA) | Instruction | Sequence | Verdict |
|---|---|---|---|---|---|
| server | 0x114640 | 0x1146b4 | adc dword ptr gs:[rax], r8d | in | not reached |
| server | 0x114640 | 0x1146bc | adc dword ptr gs:[rax], r8d | in | not reached |
| server | 0x652ae0 | 0x652c6e | add byte ptr gs:[rcx + 0x2b], bh | in | not reached |
| client | 0x4e650 | 0x4e844 | fiadd word ptr gs:[rax + rax] | in | not reached |
| client | 0x651d58 | 0x651d6a | add byte ptr gs:[rdx + 0x1c], bh | in | unrooted |
| engine | 0x286ab8 | 0x286ad1 | sub byte ptr gs:[rax], al | in | not reached |
| engine | 0x286ab8 | 0x286add | sub byte ptr gs:[rax], al | in | not reached |
| localize | 0x266c8 | 0x266d1 | add al, byte ptr gs:[rax] | in | unrooted |
| localize | 0x266c8 | 0x266d4 | add al, byte ptr gs:[rax] | in | unrooted |
| localize | 0x266c8 | 0x266dd | add al, byte ptr gs:[rax] | in | unrooted |
| materialsystem_dx11 | 0x1264b0 | 0x12670d | adc al, byte ptr gs:[rax] | in | unproven |
| materialsystem_dx11 | 0x1264b0 | 0x12671d | adc al, byte ptr gs:[rax] | in | unproven |
| mileswin64 | 0x22690 | 0x23e6c | sub byte ptr gs:[rdx], al | in | not reached |
| mileswin64 | 0x22690 | 0x23e80 | sub byte ptr gs:[rdx], al | in | not reached |
| mileswin64 | 0x22690 | 0x23e8c | sub byte ptr gs:[rdx], al | in | not reached |
| mileswin64 | 0x22690 | 0x23e98 | sub byte ptr gs:[rdx], al | in | not reached |
| mileswin64 | 0x22690 | 0x23ea4 | sub byte ptr gs:[rdx], al | in | not reached |
| server | 0x26be8 | 0x26c49 | add al, byte ptr gs:[rax] | after | not reached |
| server | 0x26be8 | 0x26c51 | add al, byte ptr gs:[rax] | after | not reached |
| server | 0x26be8 | 0x26c59 | add al, byte ptr gs:[rax] | after | not reached |
| server | 0x26be8 | 0x26c69 | add al, byte ptr gs:[rax] | after | not reached |
| server | 0x26be8 | 0x26c71 | add al, byte ptr gs:[rax] | after | not reached |
| server | 0x77645c | 0x7764a0 | movsxd esi, dword ptr gs:[rdi] | after | not reached |
| client | 0x26c38 | 0x26c99 | add al, byte ptr gs:[rax] | after | not reached |
| client | 0x26c38 | 0x26c9d | add al, byte ptr gs:[rax] | after | not reached |
| client | 0x26c38 | 0x26ca1 | add al, byte ptr gs:[rax] | after | not reached |
| client | 0x26c38 | 0x26ca4 | add al, byte ptr gs:[rax] | after | not reached |
| client | 0x26c38 | 0x26cbd | add al, byte ptr gs:[rax] | after | unrooted |
| client | 0x337d0 | 0x33864 | sbb byte ptr gs:[rbx], al | after | not reached |
| client | 0x108530 | 0x10869c | test dword ptr gs:[rax], edx | after | not reached |
| engine | 0x286ab8 | 0x286b05 | sub byte ptr gs:[rax], al | after | not reached |
| vstdlib | 0x2f5f0 | 0x2f6df | test dword ptr gs:[rdx], 0x2f78100 | after | unproven |

## 2. gs:[0x58]: the MSVC static-TLS pattern

The reached accesses (240) have this shape. Register R takes the TLS array pointer from `gs:[0x58]`. A register Ri
takes `_tls_index` from `[rip+X]`, and B takes the module's block pointer from `[R + Ri*8]`. Later accesses use
`[B + const]`, or a register holding a constant plus B. The survey checks that X equals the module's `_tls_index` RVA
from the TLS directory: true for all 233 standard accesses. Offsets are relative to the module's static TLS block.

| Module | 0x58 reached | Standard | Distinct offsets | Largest | Notes |
|---|---|---|---|---|---|
| server | 11 | 11 | 4 | 0x1024 | |
| client | 11 | 11 | 4 | 0x1024 | |
| engine | 11 | 11 | 4 | 0x1024 | |
| vguimatsurface | 9 | 9 | 4 | 0x1024 | |
| tier0 | 31 | 31 | 4 | 0x24 | |
| vphysics | 21 | 16 | 5 | 0x24 | 5 non-standard |
| materialsystem_dx11 | 91 | 89 | 6 | 0x1030 | 2 non-standard; 1 unreached access (0x95a9f) |
| datacache | 6 | 6 | 2 | 0x14 | |
| filesystem_stdio | 6 | 6 | 2 | 0x14 | |
| inputsystem | 6 | 6 | 2 | 0x14 | |
| launcher | 6 | 6 | 2 | 0x14 | |
| localize | 6 | 6 | 2 | 0x14 | |
| rtech_game | 6 | 6 | 2 | 0x14 | |
| studiorender | 6 | 6 | 2 | 0x14 | |
| vgui2 | 6 | 6 | 2 | 0x14 | |
| vstdlib | 6 | 6 | 2 | 0x14 | |
| OriginSDK | 1 | 1 | 2 | 0x10 | |
| **Total** | **240** | **233** | | | |

No standard access reaches an offset at or beyond its module's template size. The non-standard accesses are at
materialsystem_dx11 0x4c17f and 0x594e2 (index register not loaded from `_tls_index` nearby), and vphysics 0x696eb
and 0x955ac (no block load within 8 instructions) and 0x94e40, 0x9af6d and 0xada10 (index not loaded from
`_tls_index` nearby). They are excluded from the offset counts.

The unreached gs:[0x58] at materialsystem_dx11 0x95a9f is a second function inside the unwind range of a thunk
(entry 0x95a75 is `call`, then `jmp qword ptr [rip+...]`). No entry point reaches it.

## 3. TLS directory (IMAGE_TLS_DIRECTORY64)

| Module | Template bytes | Zero fill | Callbacks | _tls_index RVA | Nonzero template bytes |
|---|---|---|---|---|---|
| server | 4136 | 0 | 0 | 0x175a494 | 0 |
| OriginSDK | 24 | 0 | 0 | 0x108208 | 0 |
| client | 4136 | 0 | 0 | 0x2fd2fd4 | 0 |
| datacache | 4128 | 0 | 0 | 0x130308 | 0 |
| engine | 4136 | 0 | 0 | 0x14c84458 | 0 |
| filesystem_stdio | 4128 | 0 | 0 | 0xeb030 | 0 |
| inputsystem | 4128 | 0 | 0 | 0x6d124 | 0 |
| launcher | 4128 | 0 | 0 | 0x82368 | 0 |
| localize | 4128 | 0 | 0 | 0x72dfc | 0 |
| materialsystem_dx11 | 4145 | 0 | 0 | 0x1de5a74 | 1 (template offset 0x1028, value 0x1C) |
| rtech_game | 4128 | 0 | 0 | 0x3d9768 | 0 |
| studiorender | 4128 | 0 | 0 | 0x20d90c | 0 |
| tier0 | 4144 | 0 | 0 | 0x19aaf0 | 1 (template offset 0x10, value 0x1C) |
| vgui2 | 4128 | 0 | 0 | 0x12cb04 | 0 |
| vguimatsurface | 4136 | 0 | 0 | 0x28546c | 0 |
| vphysics | 4144 | 0 | 0 | 0x1f14bc | 0 |
| vstdlib | 4128 | 0 | 0 | 0x79ea4 | 0 |

No TLS directory in: GFSDK_SSAO.win64, GFSDK_TXAA.win64, amd_ags_x64, auditionwin64, bink2w64, binkawin64, mileswin64.
Every `_tls_index` RVA lies in `.data` beyond the file's raw data, so the loader writes it. Template sizes total
66,153 bytes across the 17 modules.

## 4. gs:[0x10]: stack probe

Each module has one helper of 0x4e bytes that reads `gs:[0x10]` once and compares it with a value derived from `rsp`
and the requested size in `rax`, then branches. The shape is that of an MSVC-style stack probe (the size arrives in
`rax`); the failure path was not examined. Reached direct call sites per module: server 226, GFSDK_SSAO 5, GFSDK_TXAA 3, OriginSDK 80,
amd_ags_x64 6, auditionwin64 10, bink2w64 23, binkawin64 5, client 286, datacache 29, engine 4287, filesystem_stdio 31,
inputsystem 14, launcher 26, localize 22, materialsystem_dx11 99, mileswin64 34, rtech_game 8, studiorender 35,
tier0 36, vgui2 29, vguimatsurface 114, vphysics 70, vstdlib 24.

## 5. gs:[0x30] and gs:[0x08]

gs:[0x30] (TEB self pointer): 5 sites, all in OriginSDK.dll. Four are in unwind ranges; one is in code without an
unwind entry. Two sites are followed by a read of `[rax + 8]` (the StackBase field), so the code reads the TEB and then
its stack base. gs:[0x08] (StackBase): one site, in tier0.dll code without an unwind entry, not examined further.

## 6. Dynamic TLS/FLS call sites (reached)

Counted as calls through the IAT slot, plus calls to the import thunk that jumps through it. "Imports" are named
TLS/FLS imports in the import table (kernel32.dll for all).

| Module | Imports | Reached call sites | Detail |
|---|---|---|---|
| GFSDK_SSAO.win64 | Fls x4 | 8 | FlsAlloc 1, FlsFree 1, FlsGetValue 2, FlsSetValue 4 |
| GFSDK_TXAA.win64 | Fls x4 | 8 | as above |
| OriginSDK | Tls x4 | 21 | TlsAlloc 10, TlsFree 3, TlsGetValue 2, TlsSetValue 6 |
| auditionwin64 | Fls x4, Tls x3 | 13 | FlsAlloc 1, FlsFree 1, FlsGetValue 2, FlsSetValue 4, TlsAlloc 1, TlsGetValue 1, TlsSetValue 3 |
| bink2w64 | Fls x4 | 8 | as GFSDK |
| binkawin64 | Fls x4 | 8 | as GFSDK |
| mileswin64 | Fls x4, Tls x3 | 13 | as auditionwin64 |
| tier0 | Tls x4 | 22 | TlsAlloc 4, TlsFree 4, TlsGetValue 5, TlsSetValue 9 |
| server, amd_ags_x64, client, datacache, engine, filesystem_stdio, inputsystem, launcher, localize, materialsystem_dx11, rtech_game, studiorender, vgui2, vguimatsurface, vphysics, vstdlib | Tls x4 | 0 | imported, no reached call site |
| **Total** | | **101** | |

The 16 modules with no reached call sites still have IAT slots for these imports; the calls behind them are not in any
path from an entry point in that module.

## 7. Proposal for the port's thread block

What guest code reads, from reached code only:

- gs:[0x08] StackBase: 1 site. gs:[0x10] StackLimit: 24 sites (the probe). gs:[0x30] self pointer: 5 sites
  (OriginSDK). gs:[0x58] TLS array pointer: 240 sites (17 modules). Nothing else. No register-based gs access is reached.

Fields the port's block must provide:

- Offsets 0x08, 0x10, 0x30, 0x58 at the usual TEB layout, so that the lifted `gs:[X]` becomes a load from one per-thread
  base. The block is host-side and must be reachable from the thread's register or thread-local base.
- StackLimit (0x10): either the real lower bound of the thread's stack, or 0, which makes the probe always pass. The
  probe is called 226 to 4287 times per module, so a no-op helper is also reasonable.
- The TLS array at 0x58: at least 17 pointers (one per TLS module), indexed by `_tls_index * 8`. Allocate 64 for headroom.

Static TLS block per module: template bytes plus zero fill (above). Total 66,153 bytes per thread. The largest offset used
is 0x1030 (materialsystem_dx11), inside its 4145-byte block. Copy the template: materialsystem_dx11 and tier0 each have
one nonzero byte. No module has TLS callbacks.

On a new thread:

1. Allocate the TLS array (64 entries). For each TLS module with a known index, allocate a zeroed block of template
   size, copy the template bytes, and store the pointer at `array[index]`.
2. Set self (0x30) to the block, and set StackBase and StackLimit.
3. Run no TLS callbacks (none exist).

When a DLL is loaded:

1. Assign it the next free index and write that value into its `_tls_index` slot (RVA in section 3) before any of its code runs.
2. Allocate its block for every live thread (or lazily on first access in each thread).
3. Run its entry point (DllMain). The survey treats it as a root; it adds no reached gs operands or TLS calls.

When a DLL is unloaded: free its block in every thread and clear its array entry.

TLS/FLS APIs: 8 modules call TlsAlloc, TlsFree, TlsGetValue, TlsSetValue, FlsAlloc, FlsFree, FlsGetValue or FlsSetValue
(101 sites). The port implements them as host functions over a per-thread slot table. None of the modules reads the
TEB slot area (0x1480 onward) directly.

## Limits of this survey

- Reachability is a lower bound. 1011 register-form indirect jumps are unresolved. In the cases examined, the switch
  table base was set outside the unwind range, so the walk never saw it. Register-indirect calls through loaded function pointers are followed only where a
  relocation or export names the target.
- 102 gs:[0x58] byte-level sites sit in code with no unwind entry that no entry point reaches. They are probably dead
  accessor functions. Their offsets are not counted.
- 3 register-based operands are unproven (section 1). The materialsystem gs:[0x58] at 0x95a9f is not reached.
- The standard-pattern check trusts the `_tls_index` load it finds within a short window. The 7 non-standard accesses
  are listed, not explained.
