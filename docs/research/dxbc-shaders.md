# Direct3D 11 shader bytecode (DXBC) and what converting it ahead of time needs

Read alongside the project's notes on how the game uses Direct3D 11 (precompiled SM4.0/5.0 VS, PS, GS and CS from
`.vcs` files; `D3DReflect` only for a vertex shader's input signature; `D3DGetBlobPart` only for part 0) and on
DXMT measured on a test Mac (M3 Pro).

Everything here comes from public documentation and public source. No game file was used, nothing from the
cloned trees was built or run, no code is copied (field, enum and function names are quoted as names). Where an
implementation is described, it is in my own words.

**Counts** (citations per part, counted mechanically in this file: documented = Microsoft/Apple documentation or Microsoft's published format code; source = file:line citations of an open implementation, Wine's tests included; not settled = places marked so; bare line numbers inside table cells, frequent in parts 1 and 3, are not counted, so the middle column is a lower bound):

| Part | Documented | Source of an open implementation | Not settled |
|---|---|---|---|
| 1 Container | 26 | 148 | 12 |
| 2 Instruction stream | 234 | 158 | 25 |
| 3 Reflection | 40 | 51 | 8 |
| 4 Routes to Metal | 63 | 260 | 16 |
| 5 Ahead of time | 39 | 128 | 8 |
| 6 Test plan (cross-references only) | 0 | 0 | 2 |

## How to read this document

**Labels.** Every statement carries a source and one of three verdicts, written as a tag:

| Tag in the text | Verdict | What it means |
|---|---|---|
| `[doc]`, `documented`, `SPEC §n`, `DOC:name`, `FST p.n`, `MSL §n` | **documented** | Microsoft's or Apple's documentation (URL given, or one of the abbreviations below). |
| `[ms-src]`, `HDR:n` | **documented** | Microsoft's own published, MIT-licensed code that defines the format: `d3d12tokenizedprogramformat.hpp`, `BlobContainer.h`, `DXBCUtils.*` (vendored in DXMT's `libs/DXBCParser`), and `DxilContainer.h` (DXC). |
| `[src]`, `impl`, `source` | **source of an open implementation** | A file:line (and function) in one of the trees below. |
| `[test]` | **source of an open implementation**, and evidence of Windows' behaviour | Wine's conformance tests. They assert what Windows' `d3dcompiler` returns, and their embedded blobs were produced by Microsoft's `fxc.exe`. A `todo_wine` marks a case where Wine still differs from Windows. |
| `[write-up]` | **published format write-up** | Third-party write-up or reader (SlimShader, DarkStarSword's `dx11shaderanalyse.py`, press release notes). Never the only source of a verdict. |
| **NOT SETTLED** / *not settled* | **not settled** | Sources disagree or none was found. The entry says what was looked at and what would settle it. |

**Abbreviations.**
- `SPEC §n`: *Direct3D 11.3 Functional Specification*,
  https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm (section numbers are the page's anchors).
- `DOC:name`: https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/name (the shader-model assembly reference pages).
- Other Microsoft pages are cited by their full learn.microsoft.com URL.
- `MSC`: Apple's Metal shader converter (IRConverter). `AOT`: ahead of time. `PSO`: Metal pipeline state object.
- `HDR:n`: line n of `dxmt/libs/DXBCParser/d3d12tokenizedprogramformat.hpp` (Microsoft, MIT).
- `FST p.n`: Apple, *Metal Feature Set Tables*, May 21 2026, https://developer.apple.com/metal/Metal-Feature-Set-Tables.pdf.
- `MSL §n, p.n`: Apple, *Metal Shading Language Specification* 4.1 (2026-06-04),
  https://developer.apple.com/metal/Metal-Shading-Language-Specification.pdf.
- In part 2, `VK` = `wine/libs/vkd3d/libs/vkd3d-shader/`, `DXVK` = `dxvk-2.7.1/src/dxbc/`, `DS` = `dxbc-spirv/`,
  `MT` = `dxmt/src/airconv/`, `HC` = `hlslcc/src/`.
- Inside parts 2, 4 and 5 a bare `§n` points to that part's own sections; `SPEC §n` always points to the
  functional specification (where a list continues, e.g. "SPEC §3.1.3.2, §22.10.21", the 22.x numbers are the
  specification's).

**How the Microsoft and Apple pages were read.** learn.microsoft.com and microsoft.github.io were unreachable
from the research environment (the proxy refused them). The same pages were read from the repositories that
publish them: `MicrosoftDocs/sdk-api` and `MicrosoftDocs/win32` (Markdown sources of the learn.microsoft.com
pages) and `microsoft/DirectX-Specs` (the source of the functional specification). The URLs given are the
published pages; the published text could differ slightly from those sources (not checked). Apple's API pages
render by script; they were read through their JSON/Markdown endpoints, the two PDFs above and WWDC transcripts.
No page contained text addressed to the reader or to an AI.

**Sources (paths in the text are relative to these roots).**

| Root | Repository | Commit (date) | Licence |
|---|---|---|---|
| `wine/` | https://github.com/wine-mirror/wine (sparse: `dlls/d3dcompiler_43`, `dlls/d3dcompiler_47`, `include`, `libs/vkd3d`) | a15ee2cc (2026-10-09) | LGPL 2.1+ |
| `wine/libs/vkd3d/` | vkd3d **2.1** as bundled in Wine (`config.h:3`); upstream gitlab.winehq.org was unreachable | same | LGPL 2.1+ |
| `dxvk/` | https://github.com/doitsujin/dxvk (3.1.1 development) | 71ec7b5b (2026-10-10) | zlib/libpng |
| `dxvk-2.7.1/` | same, tag v2.7.1, the last release with `src/dxbc` (sparse: `src/dxbc`, `src/d3d11`) | c3dd74be (2025-08-30) | zlib/libpng |
| `dxbc-spirv/` | https://github.com/doitsujin/dxbc-spirv (DXVK 3's DXBC front end; DXVK pins ecb8308f) | abfc9ad6 (2026-10-09) | MIT |
| `dxmt/` | https://github.com/3Shain/dxmt (same commit as the DXMT measurement) | e94c312f (2026-10-06) | LGPL 2.1+; `libs/DXBCParser` MIT (Microsoft) |
| `spirv-cross/` | https://github.com/KhronosGroup/SPIRV-Cross | aa217aeb (2026-09-22) | Apache-2.0 (MSL back end Apache-2.0 OR MIT) |
| `moltenvk/` | https://github.com/KhronosGroup/MoltenVK | 67b26826 (2026-10-08) | Apache-2.0 |
| `hlslcc/` | https://github.com/Unity-Technologies/HLSLcc | 3ea1fcd6 (2020-09-23, last commit) | MIT (+ BSD bstring) |
| `dxc/` | https://github.com/microsoft/DirectXShaderCompiler (sparse) | 6e0fab36 (2026-10-09) | University of Illinois/NCSA |

Mesa (gitlab.freedesktop.org and its GitHub mirror) was not reachable; its D3D10 user-mode driver's DXBC
parser was therefore not read. DXVK's `dxbc` directory no longer exists on DXVK's main
branch: DXVK 3 moved its DXBC front end into `dxbc-spirv`. Both the old directory (2.7.1) and the new front
end were read.

**Corrections to an earlier DXMT measurement found on the way** (details in the parts named):
- The DXMT measurement says `ID3D11ShaderReflection` has "22 own methods". It has 22 vtable entries: 3 of `IUnknown` and 19 of
  its own (part 3, 3.3).
- The DXMT measurement counts four `setOpaquePointers(false)` sites in airconv; there are five (part 4, §3.1, §7).
- The DXMT measurement says the `airconv` command-line tool's output cannot serve the game. Its generic vertex-shader output is an
  ordinary `[[stage_in]]` vertex function, usable by a runtime of our own that sets a matching
  `MTLVertexDescriptor` (part 4, §3.2, §7).
- The DXMT measurement relies on airconv's `UnsupportedFeature` errors for a conversion sweep. Several gaps (double-precision
  opcodes, GS streams other than 0, interfaces, some system values) are `assert`s that release builds compile
  out, so they become silent miscompiles; a sweep must scan the bytecode for them as well (part 4, §3.6, §7).

## Summary for the renderer

1. **Container (part 1).** A 32-byte header (`DXBC`, a 16-byte checksum, version 1, total size, chunk count) and a
   table of chunk offsets. The checksum is MD5 over bytes 20 to the end, with a different last padding block
   (part 1, 1.2): we can verify it and must recompute it for any container we write (`D3DGetBlobPart`,
   `D3DStripShader`). The game's SM4.0/5.0 shaders carry RDEF, ISGN, OSGN (OSG5 for SM5 GS), SHDR/SHEX and STAT;
   SFI0 and IFCE when the features are used; PCSG for HS/DS only.
2. **Instruction stream (part 2).** Microsoft publishes the token format as an MIT header
   (`d3d12tokenizedprogramformat.hpp`), and the functional specification pins down the numerics. The five open
   converters disagree on exactly the cases the specification defines and Metal does not: shift counts masked to
   5 bits, `ftoi`/`ftou` saturation and NaN, `udiv` by zero, min/max with NaN, fused `mad`, fast-math, and
   out-of-bounds `x#` access (part 2, §4). Metal's own rules for these were not settled here.
3. **Reflection (part 3).** Every value `D3DReflect` returns comes from RDEF (bindings, cbuffers, variables,
   types, creator, flags), the signature chunks, STAT (all counts, and the GS/HS/DS values) and one declaration
   (`dcl_thread_group`). It can be answered from the bytecode alone. Wine/vkd3d implements it and Wine's tests
   record Windows' answers; four methods are stubs there (`GetMinFeatureLevel`, `GetRequiresFlags`,
   `GetNumInterfaceSlots`, the class-type queries). The game needs only `GetInputParameterDesc` and part 0 of
   `D3DGetBlobPart` [proj: 31 §7].
4. **Routes (part 4)**, ranked for this game:
   1. **DXBC → AIR with DXMT's airconv**, driven by our own offline tool. It is the only open converter that
      handles VS, PS, CS *and* GS (as VS+GS object/mesh pairs, Apple7+) from DXBC directly, and it ships in
      CrossOver. Its gaps (doubles, interfaces, GS streams > 0 silently dropped; cull distance not applied) are
      detectable by scanning the bytecode first.
   2. **DXBC → DXIL with Microsoft's `dxilconv` (Windows, offline) → Apple's Metal shader converter.** It has the
      best front-end fidelity and Apple maintains the back end, but whether Apple's converter accepts dxilconv's
      SM6.0 output, and Apple's licence terms, are not settled. Worth a spike as a cross-check, not as the main
      path.
   3. **DXBC → SPIR-V (vkd3d-shader or dxbc-spirv) → SPIRV-Cross → MSL.** It is mature and permissively
      licensed for VS/PS/CS, but has no geometry-shader path. It is the fallback for single shaders, and readable
      MSL makes it a debugging oracle.

   Last come the source-level back ends (vkd3d's experimental MSL target, Unity's HLSLcc frozen in 2020) and
   DXBC→HLSL decompilers. They are not usable for about 11,000 shaders.
5. **Ahead of time (part 5).** Everything per shader can be converted before the game runs: instructions,
   bindings (D3D11's fixed slot model), texture kinds, interpolation, CS completely. What waits for the first draw
   of a combination:
   - the VS against its input layout (DXMT fetches vertices in the shader);
   - the PS against its render-target types, UNORM8 targets, dual-source blending, sample mask and the presence of
     a depth target;
   - each GS against the VS that feeds it;
   - the Metal pipeline object itself (formats, blend, sample count).

   Function constants and Metal 4's unspecialised pipelines shrink that step but do not remove the back-end
   compile. The real list of combinations exists only after the game has been played once with logging. That log
   is also the input Apple's offline pipeline tools (`metal-tt`) need.
6. **Tests (part 6).** Fifteen shaders from a pass-through VS to the features to refuse. Each is checked against
   recorded `fxc` output, `D3DReflect` dumps and WARP renders made once on Windows.

## Part 1. The container

Short file names in parts 1 and 3: `dxbc.c`, `tpf.c`, `checksum.c`, `hlsl_codegen.c`, `vkd3d_shader_private.h` are in `wine/libs/vkd3d/libs/vkd3d-shader/`; `reflection.c` and `vkd3d_utils_main.c` without a `tests/` prefix are in `wine/libs/vkd3d/libs/vkd3d-utils/`; `tests/reflection.c`, `tests/blob.c`, `hlsl_d3d11.c` and every `[test]` line number are in `wine/dlls/d3dcompiler_43/tests/`; Wine's own `blob.c` (`D3DGetBlobPart`, `D3DStripShader`) is `wine/dlls/d3dcompiler_43/blob.c`; `BlobContainer.*`, `DXBCUtils.*`, `ShaderBinary.*` are in `dxmt/libs/DXBCParser/`; `DxilContainer.h` is `dxc/include/dxc/DxilContainer/DxilContainer.h`; `dxbc_chunk_isgn.cpp`, `dxbc_header.cpp`, `dxbc_module.cpp` are in `dxvk-2.7.1/src/dxbc/`; `dxbc_container.cpp`, `dxbc_signature.*`, `dxbc_interface.*` are in `dxbc-spirv/dxbc/`; `reflect.cpp`, `decode.cpp` are in `hlslcc/src/`. In a table row whose column says "vkd3d", a bare line number is in vkd3d's `reflection.c`; in a "test" or "Windows" column it is in `tests/reflection.c`.

### 1.1 Container header (32 bytes + 4×chunkCount)

All values are little-endian.

| Offset | Size | Field | Meaning / notes |
|---|---|---|---|
| 0x00 | 4 | magic | ASCII `DXBC` (0x43425844 read as a little-endian u32). |
| 0x04 | 16 | checksum | The modified MD5 described in 1.2. Microsoft calls it `Hash`/`Digest[16]`. |
| 0x14 | 4 | version | Microsoft defines this as two u16 values: Major=1 at 0x14 and Minor=0 at 0x16, so the dword reads as 0x00000001. |
| 0x18 | 4 | total size | Size of the whole container in bytes, counted from the start of the header. |
| 0x1C | 4 | chunk count | Microsoft calls it BlobCount (DXC: PartCount). |
| 0x20 | 4×N | chunk offsets | One u32 per chunk: the byte offset from the container start to that chunk's 8-byte header. |

Each chunk (Microsoft calls it a "blob", DXC a "part") is an 8-byte header followed by data:

| Offset in chunk | Size | Field |
|---|---|---|
| 0 | 4 | FourCC tag |
| 4 | 4 | data size in bytes. This excludes the 8-byte header. |
| 8 | size | data |

Unless a chunk section below says otherwise, every offset *inside* a chunk (string offsets, sub-table offsets) is relative to the
start of the chunk **data**, which is the chunk start + 8.

Sources:
- [ms-src] dxmt/libs/DXBCParser/BlobContainer.h:7-10 gives DXBC_MAJOR_VERSION 1, DXBC_MINOR_VERSION 0,
  `DXBC_1_0_MAX_SIZE_IN_BYTES` 0x02000000 and `DXBC_MAX_SIZE_IN_BYTES` 0x80000000.
  Lines 39-70 give the `DXBCHeader` struct (FourCC, 16-byte hash, version {u16 Major, u16 Minor}, ContainerSizeInBytes, BlobCount,
  then a u32 index array) and `DXBCBlobHeader` {FourCC, BlobSize "byte count for BlobData"}.
  Line 62: `DXBCHashStartOffset = offsetof(DXBCHeader, Version)`, which is byte 20.
- [ms-src] dxc/include/dxc/DxilContainer/DxilContainer.h:28-30 and 53-70 give the same layout
  (DxilContainerHeader: HeaderFourCC, Hash, Version{u16,u16}, ContainerSizeInBytes "From start of this header", PartCount).
- [src] wine/libs/vkd3d/libs/vkd3d-shader/dxbc.c:172-289 `parse_dxbc`. It reads the magic, then the 4 checksum dwords
  (verified unless VKD3D_SHADER_PARSE_DXBC_IGNORE_CHECKSUM is set). It rejects any version other than 0x00000001 (dxbc.c:223-230).
  It reads the total size **but never checks it against the buffer size**. It reads the chunk count and the offsets, and checks
  that each offset and chunk size fit inside the buffer it was given.
  The minimum header size is 8 dwords (vkd3d_shader_private.h:2287 `VKD3D_DXBC_HEADER_SIZE`).
- [src] vkd3d writer: dxbc.c:53-97 `vkd3d_shader_serialize_dxbc`. It writes the magic, a zero checksum, version 1, a size
  placeholder, the count and an offset table. It then aligns each chunk to 4 bytes (`bytecode_align`). Padding bytes are 0xAB
  (vkd3d_shader_main.c:504-517). It patches the size, then computes the checksum and writes it into bytes 4..19.
- [src] dxbc-spirv/dxbc/dxbc_container.cpp:168-228 `Container::parseHeader`. It reads the same fields and rejects a declared
  fileSize larger than the buffer. It does not check the version.
- [src] dxvk-2.7.1/src/dxbc/dxbc_header.cpp:5-23. DXVK skips the checksum, the "constant 1" and the "bytecode length", then reads
  the count and the offsets. It verifies none of them.
- [src] hlslcc/src/decode.cpp:31-44 (struct with `unk[4]`, `one`, `totalSize`, `chunkCount`) and decode.cpp:1513-1610 `DecodeDXBC`.
  hlslcc does no validation.
- [write-up] DarkStarSword's dx11shaderanalyse.py (https://github.com/DarkStarSword/3d-fixes/blob/master/dx11shaderanalyse.py,
  `parse_dxbc_header`) unpacks the header as `<4s16s3I`: magic, hash, "unknown1" (the version), size, chunk count.

Validation differences between implementations:
- Microsoft's `CDXBCParser::ReadDXBC` (dxmt/libs/DXBCParser/BlobContainer.cpp:21-93) has the strictest checks:
  - the declared size must **equal** the buffer size passed in;
  - the major and minor version must be exactly 1 and 0;
  - each chunk must start **immediately after** the previous one (or after the index for the first chunk). There may be no gaps
    and no reordering (lines 58-87);
  - no chunk may run past the end of the container.

  That parser therefore does not accept padding between chunks. vkd3d accepts any offsets (dxbc.c:244-279).
  Whether the runtime's real validator is exactly this code is **NOT SETTLED**. The code is the open-sourced Microsoft parser
  used by the D3D12 translation layer, as vendored in DXMT.
- Wine tests show what Windows does with truncated input. D3DGetBlobPart with size 0 or 7 dwords returns D3DERR_INVALIDCALL.
  With exactly 8 dwords (a header that declares chunks it does not contain), it returns E_FAIL on d3dcompiler <46 and
  D3DERR_INVALIDCALL on ≥46 (wine/dlls/d3dcompiler_43/tests/blob.c:149-162, `test_get_blob_part`). The ≥46 case is `todo_wine`.
- Real fxc output can have a total size that is not a multiple of 4. `test_blob_part2` declares 0x139B = 5019 bytes, because its
  SDBG chunk is 0xFD7 bytes long (wine/dlls/d3dcompiler_43/tests/blob.c:463 and the SDBG header at dword 239-240 of the array).
  Chunks may also be odd-sized.

Observed fxc chunk orders:
- vs_4_0_level_9_0: XNAS, XNAP, Aon9, SHDR, STAT, RDEF, ISGN, OSGN (offsets at blob.c:90-91).
- hs_5_0 /Zi: RDEF, ISGN, OSGN, PCSG, SHEX, STAT, SDBG (blob.c:463-464).
- vs_4_0 plain: RDEF, ISGN, OSGN, SHDR, STAT (tests/reflection.c:65-66).
- ps_5_0 using interfaces: RDEF, ISGN, OSGN, IFCE, SHEX, STAT (tests/reflection.c:1548-1549).

vkd3d writes ISGN, OSGN, [PCSG], RDEF, SHDR, [SFI0], STAT (tpf.c:4588-4595). Readers search chunks by tag, so the order does not
matter to any implementation examined.

### 1.2 Checksum algorithm

What is hashed: bytes 20 through total_size-1. That is everything after the 16-byte checksum, starting at the version dword and
including the rest of the header, the offset table and all chunks.

How it differs from RFC 1321 MD5. The compression function, initial state (0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476), round
constants and little-endian word loading are all standard MD5. Only the final padding block differs. Let L be the byte count of
the hashed data, B = 8·L its bit count, and r = L mod 64:
- Standard MD5 appends 0x80, zero-fills to 56 mod 64, then appends the 64-bit bit count at the end.
- DXBC variant, case **r < 56**: the last block is laid out as:
  - bytes 0..3: B as a u32;
  - next: the r leftover data bytes;
  - next: 0x80, then zeros up to byte 59;
  - bytes 60..63: the u32 (B >> 2) | 1, which equals 2·L + 1.
- DXBC variant, case **r ≥ 56**: the leftover bytes, then 0x80, then zeros to the end of the 64-byte block, are hashed as one
  normal block. Then one more block is hashed: B (u32) in bytes 0..3, zeros, and (B >> 2) | 1 in bytes 60..63.
- The final MD5 state (A, B, C, D) is written as the 16 checksum bytes, as four u32 little-endian words.
- Only the low 32 bits of the bit count are used. The high 32 bits of the standard 64-bit length are never emitted.

Sources:
- [src] wine/libs/vkd3d/libs/vkd3d-shader/checksum.c:36-39 (comment: the variant "only changes the way the message is padded in
  the final step") and md5_final checksum.c:238-303. The VKD3D_MD5_DXBC branches are at lines 267-274 and 282-291.
  dxbc.c:23-34 `compute_dxbc_checksum` skips the first 20 bytes (`DXBC_CHECKSUM_SKIP_BYTE_COUNT 20`).
- [src] dxbc-spirv/dxbc/dxbc_container.cpp:11-72 `hashDxbcBinary`. It is an independent formulation of the same thing: it skips
  `offsetof(FileHeader, version)`, defines aNum = size·8 and bNum = (aNum >> 2) | 1, and uses the same two cases split at 56.
- [ms-src] BlobContainer.h:62 says the hash covers the container "from this offset to the end", and the offset is that of
  `Version`, i.e. 20.
- [write-up] DarkStarSword dx11shaderanalyse.py `shader_hash()`. Its comments describe the same change: a 32-bit bit count
  inserted at the start of the final block, and `(orig_len_bytes << 1) | 1` at the end. It also notes the RSA reference
  algorithm is otherwise unchanged. (URL above.)

Implementation note (divergence): dxbc-spirv's container *writer* `buildContainer` (dxbc_container.cpp:313-323) fills the header
hash with `util::md5::Hasher::compute` over the whole file. That looks like plain MD5 over all bytes, not the DXBC variant over
bytes 20..end. Its `validateHash` (line 156-159), by contrast, uses the DXBC variant. So containers that dxbc-spirv writes would
probably fail a DXBC checksum check. **NOT SETTLED**: I did not look inside util_md5 to rule out a wrapper.

What happens when the checksum is wrong:
- vkd3d `parse_dxbc` rejects the blob (VKD3D_SHADER_ERROR_DXBC_INVALID_CHECKSUM, dxbc.c:207-221). Wine's D3DReflect, D3DGetBlobPart
  and D3DStripShader all go through that check.
- DXVK 2.7.1 does not verify it (dxbc_header.cpp:13).

### 1.3 Chunk FourCC inventory

| Tag | Content | Readers among the trees read |
|---|---|---|
| RDEF | resource definitions: cbuffers, variables, types, bindings, creator string, compile flags | vkd3d-utils reflection.c parse_rdef; hlslcc reflect.cpp ReadResources |
| ISGN / OSGN / PCSG | input / output / patch-constant signature, 24-byte elements | all |
| OSG5 | output signature with stream index, 28-byte elements; GS in SM5 | all |
| ISG1 / OSG1 / PSG1 | signatures with stream index and min-precision, 32-byte elements | vkd3d, DXVK, dxbc-spirv, hlslcc, MS DXBCUtils |
| SHDR | SM4 token stream ("generic shader") | all |
| SHEX | the same token stream; Microsoft: "same as SHDR, but this will fail on D3D10.x runtimes and not on D3D11+" | all |
| STAT | statistics; 29 or 37 u32 values | vkd3d-utils parse_stat; SlimShader |
| SFI0 | shader feature info, one u64 of flag bits | vkd3d (only the ROV bit); MS DXBCUtils.h defines the bits |
| IFCE | class/interface data for dynamic linkage | dxbc-spirv dxbc_interface.cpp; hlslcc ReadInterfaces; SlimShader |
| Aon9 | embedded D3D9 ("level 9") shader for feature levels 9_x | only passed through (D3DGetBlobPart LEGACY_SHADER) |
| XNAP / XNAS | XNA prepass / XNA shader, also D3D9-token payloads | only passed through |
| SDBG | old-style debug info (fxc /Zi) | only passed through (D3DGetBlobPart DEBUG_INFO, D3DStripShader DEBUG_INFO) |
| SPDB | PDB-format debug info (newer fxc /Zi) | none parse it. D3D_BLOB_PDB is the API part. **NOT SETTLED** which tag it maps to on Windows; Wine does not implement it |
| PRIV | private data appended by the application | none; D3D_BLOB_PRIVATE_DATA / D3DCOMPILER_STRIP_PRIVATE_DATA (DXC enum: DFCC_PrivateData = 'PRIV') |
| RTS0 | serialized D3D12 root signature | vkd3d dxbc.c shader_parse_root_signature; MS DXBCGetRootSignature. Not relevant to D3D11 |
| ILDB / ILDN / DXIL / HASH / PSV0 / RDAT / SRCI / VERS | SM6 / DXIL container parts | DXC enum. DXIL only, never in SM4/5 D3D11 shaders |
| TEXT, CLIT, CLI4, CTAB, FX10, FXLC | effects / D3D9 / other | vkd3d tag list only |

Tag definitions:
- [src] wine/libs/vkd3d/include/private/vkd3d_common.h:74-101. This file also defines `TAG_RD11_REVERSE 0x25441313`.
- [ms-src] dxmt/libs/DXBCParser/BlobContainer.h:20-36 (SHDR, SHEX with the D3D10.x comment, ISGN, ISG1 "InputSignature11_1",
  PCSG, PSG1 "PatchConstantSignature11_1", OSGN, OSG5 "OutputSignature5", OSG1 "OutputSignature11_1", IFCE "InterfaceData",
  SFI0 "ShaderFeatureInfo", RTS0, DXIL).
- [ms-src] dxc/include/dxc/DxilContainer/DxilContainer.h:81-100 lists RDEF, ISG1, OSG1, PSG1, STAT, ILDB "ShaderDebugInfoDXIL",
  ILDN "ShaderDebugName", SFI0, PRIV, RTS0, DXIL, PSV0, RDAT, HASH, SRCI, PDBI, VERS.
  DXC therefore uses only the "1" signature variants; DXIL containers never use ISGN/OSGN.

**Are ISG1/OSG1/PSG1 an SM5.1 thing?** No. Microsoft names them "11_1" (BlobContainer.h:26, 28, 31): they arrived with D3D11.1
minimum-precision support. vkd3d's own writer emits them for any SM4/5 shader whose global flags enable minimum precision, and it
then also sets the SFI0 MINIMUM_PRECISION bit (tpf.c:3274, 3338-3346, 4492-4493). They are relevant to D3D11.1+ shaders that use
min16float and similar types. Treating them as shader-model-5.1 variants is inaccurate.

### 1.4 Signature chunks: ISGN, OSGN, PCSG (24 bytes), OSG5 (28), ISG1/OSG1/PSG1 (32)

Chunk data header (8 bytes):

| Off | Size | Field |
|---|---|---|
| 0 | 4 | element count |
| 4 | 4 | offset from the chunk-data start to the element array. Always 8 in practice |

Microsoft names these two fields `Parameters` and `ParameterInfo`, "offset to D3D10_INTERNALSHADER_PARAMETER[]"
(DXBCUtils.cpp:281-285). DXC calls them ParamCount and ParamOffset (DxilContainer.h:124-127).
Readers treat the second field differently:
- vkd3d treats it as a header size. It requires a multiple of 4 that is ≥ 8, and skips extra dwords (dxbc.c:383-395).
- Microsoft's parser jumps to that offset (DXBCUtils.cpp:618, 728).
- DXVK, dxbc-spirv and hlslcc skip the field and assume 8 (dxbc_chunk_isgn.cpp:7; dxbc_signature.cpp:244-245; reflect.cpp:57).
- vkd3d's writer emits a literal 8 with the comment "unknown" (tpf.c:3287). So does dxbc-spirv (dxbc_signature.cpp:303).

Element layouts. The stream index is a prefix and min-precision a suffix around the same 24-byte core:

| ISGN/OSGN/PCSG off | OSG5 off | ISG1/OSG1/PSG1 off | Size | Field |
|---|---|---|---|---|
| – | 0 | 0 | 4 | Stream (GS stream index; "parameters must appear in non-decreasing stream order") |
| 0 | 4 | 4 | 4 | SemanticName: offset of a NUL-terminated string from the chunk-data start |
| 4 | 8 | 8 | 4 | SemanticIndex |
| 8 | 12 | 12 | 4 | SystemValue. Uses D3D_NAME numbering; see below |
| 12 | 16 | 16 | 4 | ComponentType: D3D_REGISTER_COMPONENT_TYPE 0 = unknown, 1 = uint32, 2 = sint32, 3 = float32 (DXC also has 4-9 for 16/64-bit types in DXIL) |
| 16 | 20 | 20 | 4 | Register. 0xFFFFFFFF for non-register outputs such as oDepth or oMask |
| 20 | 24 | 24 | 1 | Mask: components the element occupies (bit0 = x … bit3 = w) |
| 21 | 25 | 25 | 1 | For inputs: AlwaysReads mask. For outputs: NeverWrites mask |
| 22 | 26 | 26 | 2 | padding (Pad) |
| – | – | 28 | 4 | MinPrecision (D3D_MIN_PRECISION: 0 default, 1 f16, 2 f2.8 (min10float), 3 reserved, 4 s16, 5 u16, 0xF0 any16, 0xF1 any10) |

Total element size: 24, 28 or 32 bytes. The semantic name strings follow the element array, padded with 0xAB.

Sources:
- [ms-src] DXBCUtils.cpp:287-403 has the three internal structs (D3D10_INTERNALSHADER_PARAMETER, D3D11_..._11_1,
  D3D11_..._FOR_GS) with the field comments quoted above.
- [ms-src] DxilContainer.h:117-220: `DxilProgramSignatureElement`, static_assert size 0x20.
- [src] vkd3d `vsir_signature_parse` dxbc.c:365-480. Stream exists for OSG5/ISG1/OSG1/PSG1 and min-precision for the "1" tags
  (lines 409-410). vkd3d rejects component types > 3 (line 434). It reads the mask as `mask & 0xff` and the second mask as
  `(mask >> 8) & 0xff` (lines 443-445).
- [src] DXVK dxbc_chunk_isgn.cpp:15-34 (the stream/precision rules cite DarkStarSword's script). It extracts bits 0..3 and
  8..11 and maps component type 0 to uint32.
- [src] dxbc-spirv dxbc_signature.cpp:11-23 and 102-146.
- [src] hlslcc reflect.cpp:49-193 masks with 0x7F and 0x7F00. Its input reader takes stream and minprec together for ISG1.
- [write-up] dx11shaderanalyse.py `decode_sgn`: sizes 24 / 28 (OSG5) / 32 (ISG1, OSG1, PSG1), struct `<5I2BH` / `<6I2BH` / `<6I2BHI`.

The **second mask byte** has opposite meanings for inputs and outputs:
- Microsoft: an output's byte holds the components the shader **never writes**; an input's byte holds the components it
  **always reads** (DXBCUtils.cpp:296-322).
- vkd3d calls DXBC's output storage "inverted". When parsing it converts the byte to a real used mask, `mask & ~byte`, for the
  output signature and (HS only) the patch-constant signature (tpf.c:2818-2826, 2913-2917). The writer inverts again
  (tpf.c:3304-3305). The output side counts as output when the tag is OSGN, or when the tag is PCSG and the shader is a hull
  shader (tpf.c:3281).
- Reflection returns the raw byte. vkd3d recomputes it as `0xf ^ used` for outputs (vkd3d-utils/reflection.c:671), and
  GetPatchConstantParameterDesc treats PCSG as output only for hull shaders (reflection.c:704).
- [test] tests/reflection.c:393-406 (vs_4_1): output FOG has Mask 0x1 and ReadWriteMask 0xE; SV_position has 0xF and 0x0.
  These equal the raw bytes in the blob, e.g. 0x00000e01 at line 335.
- [doc] ReadWriteMask: "Mask which indicates whether a given component is never written (if the signature is an output
  signature) or always read (if the signature is an input signature)."
  https://learn.microsoft.com/en-us/windows/win32/api/d3d11shader/ns-d3d11shader-d3d11_signature_parameter_desc

**SystemValue numbering.** Signatures store the D3D_NAME (API) values:
- 0 undefined, 1 position, 2 clip distance, 3 cull distance, 4 RT array index, 5 viewport array index, 6 vertex id,
  7 primitive id, 8 instance id, 9 is front face, 10 sample index;
- 11 final quad edge tessfactor, 12 quad inside, 13 tri edge, 14 tri inside, 15 line detail, 16 line density;
- 23 barycentrics, 24 shading rate, 25 cull primitive;
- 64 target, 65 depth, 66 coverage, 67 depth ≥, 68 depth ≤, 69 stencil ref, 70 inner coverage.

The token stream's `dcl_*_siv` name tokens use a *different* enum, D3D10_SB_NAME, where 11..22 are per-component tess factors
(quad U/V edges etc.) and 23-25 match. Microsoft's parser converts signature D3D10_NAME + SemanticIndex into D3D10_SB_NAME
(DXBCUtils.cpp:405-499).
- [ms-src] d3d12tokenizedprogramformat.hpp:2145-2175 (D3D10_SB_NAME).
- [src] dxbc-spirv dxbc_signature.h:22-49; vkd3d vkd3d_shader.h:2623-2632 (SV_TESS_FACTOR_QUADEDGE = 0x0b, SV_TARGET = 0x40).
- [ms-src] DxilContainer.h comment "Corresponds to D3D_NAME and D3D10_SB_NAME" (the values in that enum are the D3D_NAME ones).
- [src] wine/include/d3dcommon.idl:634-690 (D3D_NAME).

**SM4/5 pixel-shader outputs store SystemValue 0 for SV_Target / SV_Depth / SV_Coverage.** Every observed fxc blob does this:
- tests/reflection.c:962-970: SV_Target3 has sysval 0 and register 3.
- reflection.c:979-987: SV_DepthLessEqual has sysval 0 and register 0xFFFFFFFF.
- reflection.c:1045-1052: SV_COVERAGE has sysval 0, component type 1.

The API still reports D3D_NAME_TARGET/DEPTH/COVERAGE/DEPTH_LESS_EQUAL/DEPTH_GREATER_EQUAL, so reflection derives them from the
semantic name, case-insensitively ("sV_DePtH" maps to DEPTH, test at reflection.c:1069-1070). The test comment says the stripped
blobs exist to show "correct reporting of pixel shader output sysvals should not depend on the RDEF chunk" (reflection.c:952-954).
- vkd3d does this in `map_fragment_output_sysval` (dxbc.c:347-363), applied to OSGN/OSG1/OSG5 (dxbc.c:446-454). Its writer maps
  ≥ TARGET back to 0 (tpf.c:3307-3309).
- D3D10ReflectShader (Wine "version 0") reports D3D_NAME_UNDEFINED for these, per the tests at reflection.c:917-921 and
  1107-1112, the latter `todo_wine`. vkd3d does that for version < 35 (reflection.c:665-666).

Ordering rules:
- Microsoft's parser rejects signatures whose Register values decrease (DXBCUtils.cpp:635-643). Elements with register
  0xFFFFFFFF therefore sort last; fxc does place them last (test blob at reflection.c:707-710).
- vkd3d's writer sorts elements before emitting (tpf.c:3289-3296).

Chunk preference when several signature variants are present:
- dxbc-spirv: ISG1 > ISGN; OSG1 > OSG5 > OSGN; PSG1 > PCSG; SHEX > SHDR (dxbc_container.cpp:108-148).
- Microsoft: input ISG1 > ISGN; output OSG1 > OSGN > OSG5 (DXBCUtils.cpp:25-112); patch PSG1 > PCSG (150-186).
- vkd3d: whichever comes first, with a FIXME on duplicates (dxbc.c:532-578).
- DXVK: whichever comes last (dxbc_module.cpp:26-33).

### 1.5 SHDR / SHEX (token stream; only the container-relevant header here)

- dword 0, the version token:
  - bits 0-3: minor version;
  - bits 4-7: major version;
  - bits 16-31: program type: 0 PS, 1 VS, 2 GS, 3 HS, 4 DS, 5 CS (12 = library, 14 = amplification per DXC/D3D12).

  Examples: 0x00010041 = vs_4_1, 0x00000041 = ps_4_1, 0x00030050 = hs_5_0, 0x00050040 = cs_4_0.
- dword 1: total length in **dwords**, including these two.

Sources:
- [ms-src] d3d12tokenizedprogramformat.hpp:54-117.
- [src] vkd3d writer tpf.c:4423-4481 `tpf_write_shdr` (major<<4 | minor, type in the high half, length patched afterwards).
- [src] hlslcc decode.cpp:1621-1622.

**Tag choice.** fxc emits SHEX for 5.x targets. Observed:
- ps_5_0 constant buffer test: SHEX (reflection.c:1578).
- hs_5_0: SHEX (blob.c test_blob_part2, dword 120).
- cs_4_0: **SHEX** as well, version 0x00050040 (reflection.c:1232).
- ps_4_x / vs_4_x: SHDR.

vkd3d always writes SHDR (tpf.c:4479). Every reader accepts both tags.

**GetDesc().Version is this token copied verbatim.** vkd3d copies dword 0 of SHDR/SHEX (vkd3d-utils/reflection.c:1793-1803).
Tests assert:
- 0x10041 for vs_4_1 (reflection.c:450);
- 0x41 for ps_4_1 (reflection.c:804);
- 0x50 for ps_5_0, or 0x40 for the ps_4_0 variant used under D3D10ReflectShader (reflection.c:1758-1759).

### 1.6 RDEF

#### 1.6.1 RDEF header (28 bytes), plus RD11 for targets ≥ 5.0 (32 more bytes)

| Off | Size | Field | Notes |
|---|---|---|---|
| 0x00 | 4 | cbuffer count | |
| 0x04 | 4 | cbuffer array offset | |
| 0x08 | 4 | binding count | |
| 0x0C | 4 | binding array offset | |
| 0x10 | 1 | target minor | e.g. 0x01 in 0xFFFE0401 |
| 0x11 | 1 | target major | |
| 0x12 | 2 | target type | 0xFFFF PS, 0xFFFE VS, 0x4753 'GS', 0x4853 'HS', 0x4453 'DS', 0x4353 'CS' |
| 0x14 | 4 | compile flags | the D3DCOMPILE_* flags used. Becomes D3D11_SHADER_DESC.Flags |
| 0x18 | 4 | creator string offset | e.g. "Microsoft (R) HLSL Shader Compiler 9.29.952.3111" or "... 10.1" |

RD11 sub-header, present iff target major ≥ 5, at offset 0x1C:

| Off | Size | Value seen in fxc output | Meaning |
|---|---|---|---|
| 0x1C | 4 | 'RD11' = 0x31314452 (5.0), or 0x25441313 (5.1/6.0) | magic |
| 0x20 | 4 | 0x3C (60) | total RDEF header size, including RD11 |
| 0x24 | 4 | 0x18 (24) | cbuffer descriptor size |
| 0x28 | 4 | 0x20 (32) for 5.0; 0x28 (40) for 5.1 | binding descriptor size |
| 0x2C | 4 | 0x28 (40) | variable descriptor size |
| 0x30 | 4 | 0x24 (36) | type descriptor size |
| 0x34 | 4 | 0x0C (12) | member (field) descriptor size |
| 0x38 | 4 | 0, or 1 in the interface test | vkd3d's reader calls it "interface_slot_count"; vkd3d's writer emits 0, "unknown; possibly a null terminator" |

What the test blobs show:
- vs_4_0: header 0x00000000 ×3, bind offset 0x1C, target 0xFFFE0400, flags 0x100, creator offset 0x1C (tests/reflection.c:66-67).
- hs_5_0 /Zi: target 0x48530500, flags 0x101, RD11 sizes 0x3C, 0x18, 0x20, 0x28, 0x24, 0x0C, 0 (blob.c:465-466).
- cs_5_1: target 0x43530501, flags 0x500, magic 0x25441313, binding size 0x28 (reflection.c:1253-1254).
- ps_5_1: same pattern (reflection.c:2202-2203).
- ps_5_0 using an interface: RD11 last dword = 1 (reflection.c:1550-1551). That shader has exactly one interface slot, which
  supports the "interface slot count" reading. **NOT SETTLED**: one data point only, and no implementation uses the value.

Implementations:
- [src] vkd3d reader: structs `rdef_header` / `rdef_rd11` at vkd3d-utils/reflection.c:1183-1206. `parse_rdef` (1503-1669)
  checks every RD11 size, accepts either magic (comment at 1530-1532: RD11 for 5.0, reversed for 5.1/6.0, "not clear why the
  magic also changed"), and accepts binding size 32 or 40.
  For 4.x it uses built-in sizes: variable = 24 bytes (the first 6 fields), binding = 32, type = 16 (no trailing fields).
- [src] vkd3d writer: hlsl_codegen.c:15211-15290 `sm4_generate_rdef` (target types at 15225-15233, RD11 at 15266-15278).
  It writes **compile flags 0** ("FIXME: compilation flags").
- [src] hlslcc ReadResources (reflect.cpp:389-465) skips the target word and flags. It decides 5.0 versus 4.x layout from the
  **SHDR/SHEX** version, not from the RDEF target.

vkd3d rejects some combinations by compiler version (reflection.c:1516-1523):
- with "version" < 40 (D3D10 / d3dcompiler ≤ 39): RDEF ≥ 5.0, or a CS target;
- with "version" < 47: RDEF ≥ 5.1, unless the target is CS.

[test] matching this:
- test_reflection_cs (reflection.c:1214-1290): D3D10ReflectShader returns E_INVALIDARG for cs_4_0 and cs_5_1.
  D3DReflect returns S_OK for both on every d3dcompiler version.
- test_reflection_bound_resources (reflection.c:1305-1314): ps_5_1 gives E_INVALIDARG when D3D_COMPILER_VERSION < 47
  (`expected_hr`), and some older d3dcompiler_47 builds are also accepted as broken E_INVALIDARG.

#### 1.6.2 Binding descriptor (32 bytes; 40 when RD11 binding size = 40, i.e. 5.1)

| Off | Field | Maps to D3D11_SHADER_INPUT_BIND_DESC |
|---|---|---|
| 0 | name offset | Name |
| 4 | input type (D3D_SHADER_INPUT_TYPE: 0 cbuffer, 1 tbuffer, 2 texture, 3 sampler, 4 uav rwtyped, 5 structured, 6 uav rwstructured, 7 byteaddress, 8 uav rwbyteaddress, 9 append, 10 consume, 11 rwstructured with counter) | Type |
| 8 | return type (D3D_RESOURCE_RETURN_TYPE 1 unorm … 5 float, 6 mixed, 7 double, 8 continued; 0 for non-textures) | ReturnType |
| 12 | dimension (D3D_SRV_DIMENSION 0..11) | Dimension |
| 16 | sample count | NumSamples |
| 20 | bind point | BindPoint |
| 24 | bind count | BindCount |
| 28 | flags (D3D_SHADER_INPUT_FLAGS) | uFlags |
| 32 | space (5.1 only) | D3D12 Space |
| 36 | range ID (5.1 only) | D3D12 uID |

Sources:
- [src] reflection.c:1253-1265 (struct) and 1615-1654.
  When the binding size is 32, vkd3d sets Space = 0 and uID = BindPoint (1649-1653).
  The 4.x binding test confirms the uID rule: tex2 has uID 2 and c2 has uID 1, equal to their bind points
  (reflection.c:1209-1211).
- [src] hlslcc reflect.cpp:195-238. Comment: "fxc generates 2^32 - 1 for non MS images". hlslcc reads space and range id only
  when the version is ≥ 5.1.
- [src] vkd3d writer hlsl_codegen.c:15282-15330.
  - It puts the texture component count minus 1 into flags bits 2-3 (`VKD3D_SM4_SIF_TEXTURE_COMPONENTS_SHIFT`).
  - For structured buffers it writes the **element stride into the sample-count field**, and ~0 otherwise (the latter marked
    "FIXME: multisample count").
  - For cbuffers and samplers it writes 0, 0, 0 in the return type, dimension and sample count fields.
- [test] ps_4_1 blob (reflection.c:689-694):
  - sampler "samc" has flags 2 (D3D_SIF_COMPARISON_SAMPLER);
  - tex1 has type 2, return 5, dimension 4, samples 0xFFFFFFFF, flags 0x0C (4 components).
- [test] D3D11 bindings from fxc ps_4_0 (reflection.c:1205-1212): tex1 has BindCount 2 for the `tex1[2]` array.
- [test] hlsl_d3d11.c:773-779: `register(b5)` gives D3D_SIF_USERPACKED. The ps_5_1 cbuffers with an explicit register/space
  are also USERPACKED (reflection.c:2247-2248).
- [doc] NumSamples is -1 (0xFFFFFFFF) when a texture is not multisampled.
  https://learn.microsoft.com/en-us/windows/win32/api/d3d11shader/ns-d3d11shader-d3d11_shader_input_bind_desc
  The doc does not mention the structured-buffer stride reuse. That comes only from vkd3d's writer: **NOT SETTLED** against
  Windows output.
- [doc] D3D_SHADER_INPUT_FLAGS: USERPACKED 0x1, COMPARISON_SAMPLER 0x2, TEXTURE_COMPONENT_0 0x4, TEXTURE_COMPONENT_1 0x8,
  TEXTURE_COMPONENTS 0xC ("2-bit value for encoding texture components"), UNUSED 0x10.
  https://learn.microsoft.com/en-us/windows/win32/api/d3dcommon/ne-d3dcommon-d3d_shader_input_flags

#### 1.6.3 Constant-buffer descriptor (24 bytes, all versions)

| Off | Field | Maps to D3D11_SHADER_BUFFER_DESC |
|---|---|---|
| 0 | name offset | Name |
| 4 | variable count | Variables |
| 8 | variable array offset | – |
| 12 | size in bytes | Size |
| 16 | flags (D3D_SHADER_CBUFFER_FLAGS: USERPACKED = 1) | uFlags |
| 20 | type (D3D_CBUFFER_TYPE: 0 CBUFFER, 1 TBUFFER, 2 INTERFACE_POINTERS, 3 RESOURCE_BIND_INFO) | Type |

Sources: [src] reflection.c:1208-1216 and 1446-1496; hlslcc reflect.cpp:295-387 (size, then flags and type are skipped); vkd3d
writer hlsl_codegen.c:15334-15376. vkd3d emits a RESOURCE_BIND_INFO buffer with one variable for each structured-buffer
resource, named after the resource.

[test] ps_5_0 with an interface (reflection.c:1595-1599):
- "$ThisPointer" is INTERFACE_POINTERS with 1 variable and size 16;
- "c1" is CBUFFER with 4 variables and size 64.

[test] hlsl_d3d11.c:765-771:
- "$Globals" holds global uniforms and "$Params" holds `uniform` entry-point parameters;
- cbuffer sizes are rounded to 16 bytes (b1 = 272).

#### 1.6.4 Variable descriptor (24 bytes for 4.x, 40 bytes for 5.x)

| Off | Field | Maps to D3D11_SHADER_VARIABLE_DESC |
|---|---|---|
| 0 | name offset | Name |
| 4 | start offset in the cbuffer | StartOffset |
| 8 | size in bytes | Size |
| 12 | flags (D3D_SHADER_VARIABLE_FLAGS: 1 USERPACKED, 2 USED, 4 INTERFACE_POINTER, 8 INTERFACE_PARAMETER) | uFlags |
| 16 | type descriptor offset | (the type) |
| 20 | default value offset, 0 = none; the data is `Size` bytes | DefaultValue |
| 24 | start texture (5.x only) | StartTexture |
| 28 | texture count | TextureSize |
| 32 | start sampler | StartSampler |
| 36 | sampler count | SamplerSize |

Sources:
- [src] reflection.c:1218-1230 and 1393-1444. For 4.x vkd3d sets StartTexture and StartSampler to ~0u and the counts to 0
  (1482-1484). The tests require exactly that: StartTexture 0xFFFFFFFF, TextureSize 0, StartSampler 0xFFFFFFFF, SamplerSize 0
  for both the ps_4_0 and ps_5_0 blobs (reflection.c:1997-2023).
- [src] hlslcc reflect.cpp:310-373 reads a default value as Size/4 dwords and skips the four 5.x fields.
- [test] ps_5_0 variable "g_Test" (reflection.c:1555 raw; 1607 expected) has flags 6 = USED | INTERFACE_POINTER and size 1.
- [doc] The doc wording for StartTexture etc. says "offset from the start of the variable to the beginning of the texture" and
  TextureSize is "the size of the texture, in bytes".
  https://learn.microsoft.com/en-us/windows/win32/api/d3d11shader/ns-d3d11shader-d3d11_shader_variable_desc
  The observed values (~0 and 0 for ordinary numeric variables) fit a "first slot / count" reading better than a byte-offset
  reading: **NOT SETTLED**.
- [test] hlsl_d3d11.c:735-756: USED is set only on variables the shader reads (m, n, o, u). Unused cbuffer members have flags 0.
- [doc] "As of the June 2010 update, DefaultValue emits default values for reflection" (same variable_desc URL).

#### 1.6.5 Type descriptor (16 bytes for 4.x, 36 bytes for 5.x)

| Off | Size | Field | Maps to D3D11_SHADER_TYPE_DESC |
|---|---|---|---|
| 0 | 2 | class (D3D_SHADER_VARIABLE_CLASS: 0 scalar, 1 vector, 2 matrix_rows, 3 matrix_columns, 4 object, 5 struct, 6 interface_class, 7 interface_pointer) | Class |
| 2 | 2 | base type (D3D_SHADER_VARIABLE_TYPE: 0 void, 1 bool, 2 int, 3 float, 19 uint, 37 interface_pointer, 39 double, 52-57 min* types …) | Type |
| 4 | 2 | rows | Rows |
| 6 | 2 | columns | Columns |
| 8 | 2 | element count (0 = not an array) | Elements |
| 10 | 2 | member count | Members |
| 12 | 4 | member array offset; each member is 12 bytes {name offset, type offset, byte offset} | – |
| 16 | 4 | 5.x: unknown[0] | – |
| 20 | 4 | 5.x: unknown[1] | – |
| 24 | 4 | 5.x: unknown[2] | – |
| 28 | 4 | 5.x: unknown[3] | – |
| 32 | 4 | 5.x: type name offset | Name |

Offset (D3D11_SHADER_TYPE_DESC.Offset) comes from the *member record* (byte offset), and is 0 for a top-level variable's type.

Sources:
- [src] reflection.c:1232-1251 and 1330-1391. Member records: struct `rdef_field` at 1246-1251.
- [src] hlslcc reflect.cpp:240-293.
- [src] vkd3d writer hlsl_codegen.c:15095-15191:
  - type names are written only for profile ≥ 5;
  - a struct is written as class STRUCT, type VOID, rows = 1, columns = component count;
  - the four extra dwords are written as 0 with the comment "FIXME: unknown".

The four 5.x "unknown" dwords: **NOT SETTLED**.
- SlimShader reads them as parentTypeOffset ("Guessing"), unknown, unknown, unknown-offset, then a parent-name offset
  (https://github.com/tgjones/slimshader/blob/master/src/SlimShader/Chunks/Rdef/ShaderType.cs, lines 76-113).
- In the ps_5_0 interface test blob (reflection.c:1557-1558):
  - the interface-pointer type for `iTest g_Test` is {class 7, type 37, rows 1, cols 4, elements 0, members 0, member offset 0,
    extra[0] = 0xD8, extra[1..3] = 0, name = "iTest"};
  - 0xD8 is the offset of a second type record {class 6 INTERFACE_CLASS, type 0, rows 1, …, name "iTest"}.

  This fits the ID3D11ShaderReflectionType methods GetSubType / GetBaseClass / GetNumInterfaces / GetInterfaceByIndex, i.e.
  extra = {subtype offset, base-class offset, interface count, interface-array offset}. That mapping is my inference, not
  documented, and no implementation examined uses it.

Type naming [test] hlsl_d3d11.c:722-756 (vs_5_0, Windows d3dcompiler):
- an anonymous struct is named "<unnamed>";
- a typedef keeps its alias ("uint_t");
- matrices are named "float3x1", with class MATRIX_COLUMNS or MATRIX_ROWS depending on packing;
- an array of floats has Elements 2;
- struct members get Offset 0/16/20.

[test] reflection.c:1712-1719:
- the ps_5_0 struct `s` reports Class STRUCT, Type VOID, Rows 1, Columns 2, Members 2, Name "s";
- under D3D10 / 4.x the Name is not checked, because 4.x RDEF stores no names.

IsEqual identity: vkd3d identifies a type by its byte offset in RDEF (reflection.c:1346, 201). The tests require that:
- two float members in a struct compare S_OK;
- a struct-member float compared with a top-level float gives S_FALSE (reflection.c:2101-2118).

### 1.7 STAT chunk (statistics)

Layout as u32 words. The word indices are shared by all readers that name them:

| Word | Field (vkd3d name) | D3D11 API destination |
|---|---|---|
| 0 | instruction_count | InstructionCount |
| 1 | temp_count | TempRegisterCount |
| 2 | def_count | DefCount |
| 3 | dcl_count | DclCount |
| 4 | float_count | FloatInstructionCount |
| 5 | int_count | IntInstructionCount |
| 6 | uint_count | UintInstructionCount |
| 7 | static_flow_control_count | StaticFlowControlCount |
| 8 | dynamic_flow_control_count | DynamicFlowControlCount |
| 9 | macro_instruction_count | MacroInstructionCount (SlimShader: "Guessed") |
| 10 | temp_array_count | TempArrayCount |
| 11 | array_instruction_count | ArrayInstructionCount |
| 12 | cut_count | CutInstructionCount |
| 13 | emit_count | EmitInstructionCount |
| 14 | sample_count | TextureNormalInstructions |
| 15 | load_count | TextureLoadInstructions |
| 16 | sample_compare_count | TextureCompInstructions |
| 17 | sample_bias_count | TextureBiasInstructions |
| 18 | sample_grad_count | TextureGradientInstructions |
| 19 | mov_count | GetMovInstructionCount() |
| 20 | movc_count | GetMovcInstructionCount() |
| 21 | type_conversion_count | GetConversionInstructionCount() |
| 22 | bitwise_count | GetBitwiseInstructionCount(). SlimShader: "unknown0", asserts 0 |
| 23 | input_primitive (D3D_PRIMITIVE) | InputPrimitive, GetGSInputPrimitive() |
| 24 | gs_output_topology (D3D_PRIMITIVE_TOPOLOGY) | GSOutputTopology |
| 25 | gs_max_output_vertex_count | GSMaxOutputVertexCount |
| 26 | gather_count | not exposed. SlimShader: unknown1 (0 or 1) |
| 27 | lod_count | not exposed. SlimShader: unknown2 (0 or 2) |
| 28 | sample_frequency | IsSampleFrequencyShader() |
| — | end of the SM4.x chunk: 29 words = 116 bytes (0x74) | |
| 29 | gs_instance_count | cGSInstanceCount. SlimShader: "Unknown", asserts 0 |
| 30 | control_point_count | cControlPoints |
| 31 | hs_output_primitive (D3D_TESSELLATOR_OUTPUT_PRIMITIVE) | HSOutputPrimitive |
| 32 | hs_partitioning (D3D_TESSELLATOR_PARTITIONING) | HSPartitioning |
| 33 | tessellator_domain (D3D_TESSELLATOR_DOMAIN) | TessellatorDomain |
| 34 | barrier_count | cBarrierInstructions |
| 35 | atomic_count | cInterlockedInstructions |
| 36 | store_count | cTextureStoreInstructions |
| — | end of the SM5.x chunk: 37 words = 148 bytes (0x94) | |

Sources:
- [src] vkd3d-utils/reflection.c:1267-1306 (struct stat) and 1671-1726 (`parse_stat`). vkd3d accepts sizes 148, 116
  (= offsetof gs_instance_count) and 112 (= offsetof sample_frequency, 28 words) and fails others with E_FAIL.
  - No test blob has a 28-word STAT, so the source of that size is **NOT SETTLED**.
  - Every fxc blob in the tests is 0x74 for 4.0/4.1 (vs_4_0, vs_4_1, ps_4_1, ps_4_0, cs_4_0, vs_4_0_level_9_0) and 0x94 for
    5.0/5.1 (ps_5_0, hs_5_0, cs_5_1, ps_5_1).
- [src] vkd3d writer tpf.c:4506-4554. It writes 29 words, plus 8 more when the version is ≥ 5.0.
  It writes 0 for def, dcl, static/dynamic flow control, macro, temp array, array instruction and sample frequency.
- [write-up] SlimShader StatisticsChunk.cs
  (https://github.com/tgjones/slimshader/blob/master/src/SlimShader/Chunks/Stat/StatisticsChunk.cs, Parse).
  "DX10 stat size" is 29 words and "DX11 stat size" is 37. It places words 0-21 and 23-25, 28, 30-36 identically, and treats
  22, 26, 27 and 29 as unknowns.
- Confirmation that the word positions are right, from [test] data versus asserted API values:
  - vs_4_1 STAT (reflection.c:367-371) against the asserts at 457-507: 21 instructions, 1 temp, dcl 18, int 1, uint 1,
    static flow 1, temp arrays 5, array instructions 6, mov 10, conversion 2, bitwise 0.
  - ps_4_1 STAT (754-758) against 811-854: 50 instructions, 3 temps, dcl 5, float 17, static 6, dynamic 5, sample 1, cmp 3,
    bias 4, grad 5, mov 24.
  - hs_5_0 STAT (blob.c test_blob_part2, array dwords 202-238):
    - w23 = 15 (D3D_PRIMITIVE_8_CONTROL_POINT_PATCH, for InputPatch<VSO,8>);
    - w30 = 8 (outputcontrolpoints(8));
    - w31 = 3 (TRIANGLE_CW);
    - w32 = 1 (INTEGER);
    - w33 = 3 (QUAD).
- [doc] D3D11_SHADER_DESC field meanings (e.g. DclCount "Number of declarations (input + output)"; InputPrimitive is "the input
  primitive for a geometry shader or hull shader"; cBarrier/cInterlocked/cTextureStore are "in a compute shader").
  https://learn.microsoft.com/en-us/windows/win32/api/d3d11shader/ns-d3d11shader-d3d11_shader_desc

### 1.8 SFI0 (shader feature info)

Chunk data is a single u64 `FeatureFlags`, 8 bytes. It is only present when at least one bit is set; the vkd3d writer omits the
chunk when the flags are 0 (tpf.c:4500-4503).

| Bit | Microsoft name (DXBCUtils.h) | vkd3d name (vkd3d_shader_private.h) |
|---|---|---|
| 0x1 | SHADER_FEATURE_DOUBLES | REQUIRES_DOUBLES |
| 0x2 | SHADER_FEATURE_COMPUTE_SHADERS_PLUS_RAW_AND_STRUCTURED_BUFFERS_VIA_SHADER_4_X | REQUIRES_EARLY_DEPTH_STENCIL |
| 0x4 | UAVS_AT_EVERY_STAGE | same |
| 0x8 | 64_UAVS | same |
| 0x10 | MINIMUM_PRECISION | same |
| 0x20 | 11_1_DOUBLE_EXTENSIONS | same |
| 0x40 | 11_1_SHADER_EXTENSIONS | same |
| 0x80 | LEVEL_9_COMPARISON_FILTERING | same |
| 0x100 | TILED_RESOURCES | same |
| 0x200 | STENCIL_REF | same |
| 0x400 | INNER_COVERAGE | same |
| 0x800 | TYPED_UAV_LOAD_ADDITIONAL_FORMATS | same |
| 0x1000 | ROVS | same |
| 0x2000 | VIEWPORT_AND_RT_ARRAY_INDEX_FROM_ANY_SHADER_FEEDING_RASTERIZER | same |

Microsoft also reserves 0x7FFFFF0000000000 as "optional" bits. If any bit outside that range is unknown, the runtime fails shader
creation.

Sources:
- [ms-src] dxmt/libs/DXBCParser/DXBCUtils.h:181-213.
- [src] wine/libs/vkd3d/libs/vkd3d-shader/vkd3d_shader_private.h:2292-2305.
- [src] vkd3d reads only the ROV bit (dxbc.c:511-530 `shdr_parse_features`). It writes ROVS, MINIMUM_PRECISION and STENCIL_REF
  (tpf.c:4483-4504).
- [src] hlslcc ignores SFI0 (decode.cpp:1601-1604).

**Disagreement on bit 0x2 (NOT SETTLED).**
- Microsoft's blob-format header calls it "compute shaders plus raw and structured buffers via shader 4.x".
- vkd3d calls it EARLY_DEPTH_STENCIL, which matches the *API* constant D3D_SHADER_REQUIRES_EARLY_DEPTH_STENCIL = 0x2 returned by
  GetRequiresFlags:
  - wine/include/d3d11shader.h:29-37;
  - [doc] https://learn.microsoft.com/en-us/windows/win32/api/d3d11shader/nf-d3d11shader-id3d11shaderreflection-getrequiresflags
    (lists DOUBLES 0x1, EARLY_DEPTH_STENCIL 0x2, UAVS_AT_EVERY_STAGE 0x4, 64_UAVS 0x8, MINIMUM_PRECISION 0x10,
    11_1_DOUBLE_EXTENSIONS 0x20, 11_1_SHADER_EXTENSIONS 0x40, LEVEL_9_COMPARISON_FILTERING 0x80).

So GetRequiresFlags is probably *not* a raw copy of SFI0 for bit 1. Nothing tested settles this.

### 1.9 IFCE (interfaces / class linkage; SM5 dynamic linking)

The layout below comes from open readers that both credit or reproduce SlimShader. It is checked against the one real fxc blob
in the Wine tests (ps_5_0, reflection.c:1576-1578). IFCE data size there is 0x50. Offsets are relative to the chunk data.

Header:

| Off | Field | hlslcc name | dxbc-spirv name | Test value |
|---|---|---|---|---|
| 0x00 | class instance count | ClassInstanceCount | classInstanceCount | 0 |
| 0x04 | class type count | ClassTypeCount | classTypeCount | 1 |
| 0x08 | interface slot record count | InterfaceSlotRecordCount | ifaceSlotInfoCount | 1 |
| 0x0C | total interface slot count | (skipped) | ifaceSlotCount | 1 |
| 0x10 | class instance array offset | ClassInstanceOffset | "unknown" | 0x40 |
| 0x14 | class type array offset | ClassTypeOffset | classTypeOffset | 0x34 |
| 0x18 | interface slot record array offset | InterfaceSlotOffset | ifaceSlotOffset | 0x24 |
| 0x1C | ? | — | — | 0 |
| 0x20 | ? | — | — | 0x4E47534F, ASCII "OSGN" |

The dwords at 0x1C and 0x20 are unexplained (**NOT SETTLED**); possibly padding or garbage.

Class type record, 12 bytes:

| Off | Field |
|---|---|
| +0 | u32 name offset |
| +4 | u16 ID |
| +6 | u16 constant-buffer stride |
| +8 | u16 texture count |
| +10 | u16 sampler count |

The test record: name "cTest", ID 0, CB stride 1, textures 0, samplers 0.

Class instance record: **NOT SETTLED**.
- dxbc-spirv reads u32 name, u16 typeId, u16 unknown, i16 cbvIndex, u16 cbvOffset, i16 srvIndex, i16 samplerIndex
  (16 bytes; dxbc_interface.cpp:131-158, dxbc_interface.h:50-62).
- hlslcc reads two u16 values as a name offset with the halves swapped, then u16 ID, ConstBuf, ConstBufOffset, Texture and
  Sampler (14 bytes; reflect.cpp:483-497).
- dxbc-spirv places instances right after the class types (dxbc_interface.cpp:58-70). hlslcc uses header 0x10 instead. In the
  test blob both give 0x40.

Interface slot record, 16 bytes:

| Off | Field |
|---|---|
| +0 | slot span (number of slots) |
| +4 | entry count |
| +8 | offset of a u16 type-ID array |
| +12 | offset of a u32 table-ID array |

The test record: span 1, count 1, typeIDs at 0x40 (value 0x0000, padded 0xABAB), tableIDs at 0x44 (0).

Sources:
- [src] dxbc-spirv/dxbc/dxbc_interface.cpp:10-92, with the comment "layout … inferred from SlimShader code by Tim Jones" and
  the URL https://github.com/tgjones/slimshader/blob/master/src/SlimShader/Chunks/Ifce/InterfacesChunk.cs.
- [src] hlslcc/src/reflect.cpp:467-562.
- [write-up] SlimShader InterfacesChunk.cs, Parse lines 23-61. It reads 0x10 as "Think this is offset to start of interface slot
  info, but we don't need it" and flags its own ID assignment "Really??".

### 1.10 Aon9, XNAP, XNAS (embedded D3D9 shaders), SDBG/SPDB, PRIV, RTS0

**Aon9, XNAP, XNAS.** fxc emits these for `*_4_0_level_9_x` targets.
- [test] test_blob_part (vs_4_0_level_9_0, blob.c:89-112) has XNAS (data 0x44), XNAP (0x44) and Aon9 (0x5C).
- Each starts with a dword equal to its own data size, then 0xFFFE0200 (a vs_2_0 version token), then small offset/size dwords,
  then a table of 0x0024xxxx entries, then a complete D3D9 token stream beginning 0xFFFE0200 and ending 0x0000FFFF.
- The meaning of the header fields is **NOT SETTLED**; no open reader among the trees read parses them. vkd3d skips Aon9 when compiling
  (dxbc.c:599-601).
- D3DGetBlobPart returns the raw chunk *data* for these, not a DXBC container (see 3.9).

**SDBG.** Debug info from fxc /Zi in the d3dcompiler_43 era. [test] the hs_5_0 /Zi blob has a 4055-byte SDBG
(blob.c:705-713). There is no open parser. D3D_BLOB_DEBUG_INFO maps to it in Wine (blob.c:191-193).

**SPDB, PRIV, RTS0.**
- RTS0 is a D3D12 root signature, not used by D3D11. vkd3d parses it (dxbc.c:987-1070):
  - u32 version (1 = 1.0, 2 = 1.1; vkd3d_shader.h:2197-2198);
  - u32 parameter count;
  - u32 parameter offset;
  - u32 static sampler count;
  - u32 static sampler offset;
  - u32 flags.
- SPDB and PRIV: no layout in any tree read. D3D_BLOB_PDB, D3D_BLOB_PRIVATE_DATA, D3D_BLOB_ROOT_SIGNATURE and D3D_BLOB_DEBUG_NAME are
  documented parts (see 3.9), but Wine maps none of them.
- ILDB / ILDN / DXIL are DXIL-only parts (DxilContainer.h:89-94); they never appear in SM4/5 D3D11 containers.

## Part 2. The instruction stream (SHDR/SHEX)

### 1. Token format

#### 1.1 Program header: version token and length token

- **Version token (VerTok)**:
  - bits [31:16] hold `D3D10_SB_TOKENIZED_PROGRAM_TYPE`: PS=0, VS=1, GS=2, HS=3, DS=4, CS=5. Mesh=13 and Amplification=14 are D3D12-only, and 0xFFF0 is reserved/lib (documented: HDR:60-81; impl: VK tpf.c:31-37).
  - The decoder macros take the **minor version from bits [3:0] (mask 0x0f) and the major version from bits [7:4] (mask 0xf0)** (HDR:86-93).
  - The header's own prose says the opposite: minor in [7:0] and major in [15:8] (HDR:52-54). The macros win in practice:
    - VK reads major as `(v>>4)&0xf` and minor as `v&0xf` (tpf.c:152-153).
    - DS reads type from bit 16 (16 bits), major from bit 4 (4 bits) and minor from bit 0 (4 bits) (dxbc/dxbc_parser.h:60-67).
  - Derived example: vs_5_0 is therefore 0x00010050.
  - SPEC §22.2.1 instead lists "vs_5_0 == 0xFFFE0500, gs_5_0 == 0xFFFD0500, ps_5_0 == 0xFFFF0500". That is a D3D9-style encoding and does not match HDR or any implementation. It is, however, exactly the encoding of RDEF's target word (part 1, 1.6.1: 0xFFFE = VS, 0xFFFF = PS, 0x4753 = 'GS', then major and minor bytes), so the spec text most likely describes that word rather than the SHDR/SHEX version token (my reading; not settled from the spec itself).
- **Length token (LenTok)**: always follows VerTok. It is the DWORD count of the whole program, including VerTok and LenTok, so the minimum is 2 (documented: HDR:100-117).

#### 1.2 Opcode token (OpcodeToken0)

- **[10:0]**: `D3D10_SB_OPCODE_TYPE` (mask 0x7ff).
- **[23:11]**: opcode-specific controls.
- **[30:24]**: instruction length in DWORDs, including the opcode token and any extended opcode tokens, range 1–127.
- **[31]**: "extended" bit; the next DWORD is an extended opcode token.
- Source: documented, HDR:119-133 and 418-436. Impl HC internal_includes/tokens.h:33 and 332 use the same 0x7f000000 and 0x7ff masks. DXVK dxbc_decoder.cpp:46 and :71 extract bits 0-10 and 24-30. DS dxbc_parser.h:315 and :345 extract 11 bits at 0 and 7 bits at 24.
- **Exception, CUSTOMDATA (opcode 0x35)**:
  - bits [31:11] hold the custom-data class, and DWORD 1 holds the block length including DWORDs 0 and 1 (minimum 2) (documented: HDR:653-689).
  - The DXVK, DS and MT parsers all read DWORD 1 for customdata (impl: DXVK dxbc_decoder.cpp:63-96; DS dxbc_parser.h:345; dxmt/libs/DXBCParser/ShaderBinary.cpp:607-625).
- **Exception, DCL_FUNCTION_TABLE and DCL_INTERFACE**: when bit 31 is set on these, the next DWORD is the *actual* length rather than an extended opcode, because the 7-bit field can overflow (documented: HDR:2238-2242 and 2279-2282; impl ShaderBinary.cpp:551-560).
- **vkd3d's reader is narrower**:
  - It masks the opcode with 0xff (tpf.c:107) and the length with 5 bits, 0x1f<<24 (tpf.c:60).
  - Length 0 means "length is in the next DWORD" (tpf.c:2626-2635). That generic rule covers customdata.
  - It is a vkd3d simplification. It is safe only because no SM5 opcode exceeds 0xea and fxc rarely emits instructions longer than 31 DWORDs (not settled for long instructions; observed only in the source).

**Opcode-specific control fields in [23:11]** (documented, HDR):

| Field | Bits / mask | Values | HDR lines |
|---|---|---|---|
| `_sat` | bit 13 (0x2000) | flag | 438-445 |
| test boolean for if, breakc, continuec, retc, callc, discard | bit 18 (0x40000) | 0 = `_z`, 1 = `_nz` | 447-462 |
| precise mask | bits [22:19] (0x00780000) | one bit per x,y,z,w; for multi-destination instructions it applies to that component of every destination | 464-478 |
| resinfo return type | bits [12:11] (0x1800) | FLOAT=0, RCPFLOAT=1, UINT=2 | 480-496 |
| sampleinfo `_uint` (`D3D10_SB_INSTRUCTION_RETURN_TYPE`) | same 0x1800 mask | FLOAT=0, UINT=1 | 2215-2229 |
| sync flags | — | THREADS_IN_GROUP (`_t`) 0x800, THREAD_GROUP_SHARED_MEMORY (`_g`) 0x1000, UAV_MEMORY_GROUP (`_ugroup`) 0x2000, UAV_MEMORY_GLOBAL (`_uglobal`) 0x4000; mask 0x7800 | 498-509 |

- vkd3d keeps the low 3 control bits at 11 as "flags" and treats bit 13 (0x4 in that field) as saturate (tpf.c:62-63, 158, 2695-2702). It reads the precise mask with the same layout (tpf.c:89-90).

#### 1.3 Extended opcode tokens (OpcodeToken1..n)

- [5:0] holds `D3D10_SB_EXTENDED_OPCODE_TYPE`: EMPTY=0, SAMPLE_CONTROLS=1, RESOURCE_DIM=2, RESOURCE_RETURN_TYPE=3.
- [31] chains another extended token. At most 3 are used together (`D3D11_SB_MAX_SIMULTANEOUS_EXTENDED_OPCODES`) (documented: HDR:521-573).
- **SAMPLE_CONTROLS**: the `_aoffimmi` offsets are three 4-bit two's-complement values: U at [12:9], V at [16:13], W at [20:17] (range −8..7) (documented: HDR:536-540 and 587-610; SPEC §22.4.6 "_aoffimmi … 4 bit 2's complement numbers, having integer range [-8,7]").
  - HDR's comment line "[30:14] Ignored" overlaps the V and W fields and is a typo; the macros use shift 9+4*coord (HDR:594).
  - impl: VK tpf.c:52-57 and 2523-2548, sign-extended through 4-bit bitfields. DXVK dxbc_decoder.cpp:127-129.
- **RESOURCE_DIM**: [10:6] holds `D3D10_SB_RESOURCE_DIMENSION`, and [22:11] holds the structure stride when the dimension is STRUCTURED_BUFFER (documented: HDR:542-546 and 612-636; impl VK tpf.c:46-50 and 2573-2593).
- **RESOURCE_RETURN_TYPE**: four 4-bit `D3D10_SB_RESOURCE_RETURN_TYPE` values for x, y, z, w at bits [9:6], [13:10], [17:14], [21:18] (documented: HDR:548-554 and 638-651; impl VK tpf.c:43-44 and 2551-2571).
- **Values**:
  - `D3D10_SB_RESOURCE_DIMENSION`: UNKNOWN 0, BUFFER 1, TEXTURE1D 2, TEXTURE2D 3, TEXTURE2DMS 4, TEXTURE3D 5, TEXTURECUBE 6, TEXTURE1DARRAY 7, TEXTURE2DARRAY 8, TEXTURE2DMSARRAY 9, TEXTURECUBEARRAY 10, RAW_BUFFER 11, STRUCTURED_BUFFER 12 (HDR:2177-2192).
  - `D3D10_SB_RESOURCE_RETURN_TYPE`: UNORM 1, SNORM 2, SINT 3, UINT 4, FLOAT 5, MIXED 6, DOUBLE 7, CONTINUED 8, UNUSED 9 (HDR:2194-2205).

#### 1.4 Operand token (OperandToken0)

| Bits | Field | Values / notes |
|---|---|---|
| [1:0] | `D3D10_SB_OPERAND_NUM_COMPONENTS` | 0 = 0-component, 1 = 1-component, 2 = 4-component, 3 = N-component (unused) |
| [3:2] | selection mode (only when 4-component) | MASK=0, SWIZZLE=1, SELECT_1=2 |
| [7:4] | MASK mode: write mask | x=0x10, y=0x20, z=0x40, w=0x80 |
| [11:4] | SWIZZLE mode: four 2-bit source selectors | x in [5:4], y in [7:6], z in [9:8], w in [11:10] |
| [5:4] | SELECT_1 mode: a single component | 0..3 |
| [19:12] | `D3D10_SB_OPERAND_TYPE` | 8 bits, see list below |
| [21:20] | index dimension | 0D, 1D, 2D, 3D |
| [24:22], [27:25], [30:28] | `D3D10_SB_OPERAND_INDEX_REPRESENTATION` for index 0, 1, 2 | IMMEDIATE32=0 (one extra DWORD); IMMEDIATE64=1 (two DWORDs, HI:LO); RELATIVE=2 (one nested operand); IMMEDIATE32_PLUS_RELATIVE=3 (DWORD, then operand); IMMEDIATE64_PLUS_RELATIVE=4 |
| [31] | extended operand | an OperandToken1 follows |

- Source: documented, HDR:691-1020.
- Header quirk: the representation fields are documented as 3 bits wide and the enum reaches 4, but `D3D10_SB_OPERAND_INDEX_REPRESENTATION_MASK` is `0x3<<shift`, which is 2 bits (HDR:998-999).
  - DXVK reads 3 bits (dxbc_decoder.cpp:278), and so does DS (dxbc_parser.h:822).
  - VK reads 2 bits per index and interprets only bit 0x2 (relative) and bit 0x1 (immediate offset present) (tpf.c:122-129, 155-156, 2013-2039). The IMMEDIATE64 index forms are therefore not handled by vkd3d (impl). fxc emitting 64-bit indices: not settled, and none of the implementations seem to expect it.
- **Operand type** values (HDR:905-958):
  - TEMP r# 0, INPUT v# 1, OUTPUT o# 2, INDEXABLE_TEMP x# 3, IMMEDIATE32 4, IMMEDIATE64 5, SAMPLER s# 6, RESOURCE t# 7, CONSTANT_BUFFER cb# 8, IMMEDIATE_CONSTANT_BUFFER icb 9, LABEL l# 10, INPUT_PRIMITIVEID vPrim 11, OUTPUT_DEPTH oDepth 12, NULL 13.
  - Added in 10.1: RASTERIZER 14 and OUTPUT_COVERAGE_MASK oMask 15.
  - Added in 11: STREAM m# 16, FUNCTION_BODY fb# 17, FUNCTION_TABLE ft# 18, INTERFACE fp# 19, FUNCTION_INPUT 20, FUNCTION_OUTPUT 21, OUTPUT_CONTROL_POINT_ID 22, INPUT_FORK_INSTANCE_ID 23, INPUT_JOIN_INSTANCE_ID 24, INPUT_CONTROL_POINT vicp 25, OUTPUT_CONTROL_POINT vocp 26, INPUT_PATCH_CONSTANT vpc 27, INPUT_DOMAIN_POINT vDomain 28, THIS_POINTER 29, UNORDERED_ACCESS_VIEW u# 30, THREAD_GROUP_SHARED_MEMORY g# 31, INPUT_THREAD_ID 32, INPUT_THREAD_GROUP_ID 33, INPUT_THREAD_ID_IN_GROUP 34, INPUT_COVERAGE_MASK 35, INPUT_THREAD_ID_IN_GROUP_FLATTENED 36, INPUT_GS_INSTANCE_ID 37, OUTPUT_DEPTH_GREATER_EQUAL 38, OUTPUT_DEPTH_LESS_EQUAL 39, CYCLE_COUNTER 40, OUTPUT_STENCIL_REF 41, INNER_COVERAGE 42.
  - VK's register table omits 20, 21, 29, 40 and 42 (tpf.c:398-438) (impl).
- **Index dimension** means the dimensionality of the register file, not of the resource:
  - temps are 1D (`r#`), constant buffers 2D (`cb#[elem]`), GS inputs 2D (`v[vertex][attr]`), the ICB 1D (`icb[idx]`) (documented: HDR:969-975 and 1655-1666; SPEC §7.5).
  - From SM5.1, resources, samplers and CBs are 3D at declaration (`id, lbound, ubound`) and 2D/3D in use (HDR:1233-1247).
  - VK rewrites SM5.0 descriptor operands into the SM5.1 layout (impl: tpf.c:2224-2234).
- **Immediate operands** (type 4/5):
  - With 1 component: one DWORD (IMM32) or two (IMM64).
  - With 4 components: four DWORDs. For IMM64 that is two doubles (impl: VK tpf.c:2186-2216; documented HDR:911-918).
  - At most one source per instruction may be an immediate or a constant-buffer reference. Immediates used inside indices do not count toward that limit (documented: SPEC §7.4).
- **Relative addressing**:
  - The relative index is a complete nested operand. It follows the optional immediate DWORD(s) of that index (documented: HDR:43-45 and 986-997).
  - The index register must be a scalar select. Its value is treated as an unsigned 32-bit integer (documented: SPEC §7.8.6).
  - Only one level of nesting is allowed: `x0[x1[r0.x+1].x+1]` is illegal (documented: SPEC §7.3).

#### 1.5 Extended operand token (OperandToken1)

- [5:0] holds `D3D10_SB_EXTENDED_OPERAND_TYPE`: EMPTY=0, MODIFIER=1.
- When the type is MODIFIER:
  - [13:6]: `D3D10_SB_OPERAND_MODIFIER`, with NONE 0, NEG 1, ABS 2, ABSNEG 3 (−|x|).
  - [16:14]: `D3D11_SB_OPERAND_MIN_PRECISION`, with DEFAULT 0, FLOAT_16 1, FLOAT_2_8 (10-bit) 2, SINT_16 4, UINT_16 5.
  - [17]: `D3D12_SB_OPERAND_NON_UNIFORM`.
- [31]: chains a further extended operand token (none are defined).
- Source: documented, HDR:1022-1148. Impl: VK tpf.c:109-120 and 2079-2145 (it skips second-order extensions with a FIXME). DS dxbc_parser.h:686-701 reads neg as bit 6, abs as bit 7, min precision as 3 bits at 14, and non-uniform as bit 17.
- Semantics:
  - `abs` and `-` on a source assume float data. `movc` and `mov` without modifiers move bits unchanged (documented: SPEC §22.9.2).
  - On integer instructions, negate means two's complement (SPEC §22.12.6, imul: "negate modifier … takes 2's complement").

#### 1.6 Name token (system values in dcl_*_siv/sgv)

- [15:0] holds `D3D10_SB_NAME`:
  - UNDEFINED 0, POSITION 1, CLIP_DISTANCE 2, CULL_DISTANCE 3, RENDER_TARGET_ARRAY_INDEX 4, VIEWPORT_ARRAY_INDEX 5, VERTEX_ID 6, PRIMITIVE_ID 7, INSTANCE_ID 8, IS_FRONT_FACE 9, SAMPLE_INDEX 10.
  - Tessellation factors 11–22: FINAL_QUAD_{U,V}_EQ_{0,1}_EDGE, QUAD_{U,V}_INSIDE, TRI_{U,V,W}_EQ_0_EDGE, TRI_INSIDE, LINE_DETAIL, LINE_DENSITY.
  - D3D12 additions: BARYCENTRICS 23, SHADINGRATE 24, CULLPRIMITIVE 25.
- Source: documented, HDR:1150-1163 and 2145-2175.

---

### 2. Declarations

Every declaration starts with a standard opcode token. Opcode numbers are listed in §3.0.

| Declaration (opcode) | Encoding (documented in HDR unless noted) |
|---|---|
| **dcl_globalFlags** (0x6a) | Flags live in OpcodeToken0; no operands. REFACTORING_ALLOWED 1<<11; ENABLE_DOUBLE_PRECISION_FLOAT_OPS 1<<12; FORCE_EARLY_DEPTH_STENCIL 1<<13; ENABLE_RAW_AND_STRUCTURED_BUFFERS 1<<14 (CS 4.x only); SKIP_OPTIMIZATION 1<<15; ENABLE_MINIMUM_PRECISION 1<<16; ENABLE_DOUBLE_EXTENSIONS (11.1) 1<<17; ENABLE_SHADER_EXTENSIONS (11.1) 1<<18; ALL_RESOURCES_BOUND (12) 1<<19; mask 0x00fff800 (HDR:1174-1213). Semantics: SPEC §22.3.2. The DOC page dcl-globalflags says "there is currently one flag defined", which is stale. VK reads a 9-bit field and maps it to VKD3DSGF_* (tpf.c:86-87, 1191-1200; vkd3d_shader_private.h:888-920). DXVK reads only bits 11-14 (dxbc_decoder.h:288). |
| **dcl_constantBuffer** (0x59) | Bit 11: access pattern, IMMEDIATE_INDEXED=0 or DYNAMIC_INDEXED=1. One operand `cb#[size]` (2D: slot, size in float4 elements; size 0 means unknown) (HDR:1642-1704). Declaration order is the priority order (SPEC §7.5). |
| **dcl_immediateConstantBuffer** | Not a real opcode. It is CUSTOMDATA (0x35) with class DCL_IMMEDIATE_CONSTANT_BUFFER=3; DWORD 1 is the length (2 + 4·n), followed by n float4 tuples (HDR:1706-1720). Up to 4096 tuples, one per shader, accessed like a dynamically indexed CB (SPEC §22.3.4, §7.5.1; DOC:dcl-immediateconstantbuffer). Other customdata classes: COMMENT 0, DEBUGINFO 1, OPAQUE 2, SHADER_MESSAGE 4 (layout at HDR:1722-1755: message id 0x00200102/0x00200103, format ANSI_TEXT or PRINTF, operand count, string), SHADER_CLIP_PLANE_CONSTANT_MAPPINGS_FOR_DX9 5 (HDR:672-680, 1757-1777). VK keeps only the ICB and turns the other classes into NOP (tpf.c:814-852). DXVK warns on other classes (dxbc_compiler.cpp:1600-1606). |
| **dcl_sampler** (0x5a) | [14:11]: mode, DEFAULT 0, COMPARISON 1, MONO 2; one operand s# (HDR:1335-1382). |
| **dcl_resource** (0x58) | [15:11]: resource dimension; [22:16]: sample count (1..127; only for 2DMS and 2DMSARRAY). Operands: t#, then a ResourceReturnTypeToken (4 bits per component at 0/4/8/12). SM5.1 adds a space DWORD (HDR:1215-1333). VK reads only 4 bits of dimension and 4 bits of sample count (tpf.c:65-69). |
| **dcl_resource_raw** (0xa1) | Operand t# (SM5.1: plus space) (HDR:2554-2585). |
| **dcl_resource_structured** (0xa2) | Operand t#, then the byte stride (HDR:2587-2621). |
| **dcl_uav_typed** (0x9c) | [15:11]: dimension; [16]: GLOBALLY_COHERENT (`_glc`, 0x10000); [17]: RASTERIZER_ORDERED (0x20000). Operands: u#, then a return-type token (HDR:2354-2411). Only buffer, 1D, 1DArray, 2D, 2DArray and 3D dimensions; types UNORM, SNORM, UINT, SINT, FLOAT (DOC:dcl-uav-typed--sm5---asm-). |
| **dcl_uav_raw** (0x9d) | Bits 16 and 17 as for typed; operand u# (HDR:2413-2447). |
| **dcl_uav_structured** (0x9e) | Bits 16 and 17, plus [23] UAV_HAS_ORDER_PRESERVING_COUNTER (`_opc`, 0x800000). Operands: u#, then the byte stride (HDR:2449-2512). Without `_opc`, a shader that uses imm_atomic_alloc/consume needs an APPEND UAV; with it, a COUNTER UAV (DOC:dcl-uav-structured--sm5---asm-). The stride must be a multiple of 4. VK reads a 9-bit field at 15 (tpf.c:98-99) and maps GLOBALLY_COHERENT 0x2, ROV 0x4, ORDER_PRESERVING_COUNTER 0x100 (vkd3d_shader_private.h:933-935), i.e. bits 16, 17 and 23. DXVK reads only bits 16-17 (dxbc_decoder.h:336). |
| **dcl_tgsm_raw** (0x9f) | Operands: g#, then a byte count that is a multiple of 4 (HDR:2514-2531). |
| **dcl_tgsm_structured** (0xa0) | Operands: g#, then stride, then struct count (HDR:2533-2552). |
| **dcl_input** (0x5f), **_sgv** (0x60), **_siv** (0x61) | Operand v# with a write mask; sgv/siv add a NameToken. For GS inputs the declaration is `v[vertices][attr]` (HDR:1384-1447). |
| **dcl_input_ps** (0x62), **dcl_input_ps_siv** (0x64) | [14:11]: interpolation mode: UNDEFINED 0, CONSTANT 1, LINEAR 2, LINEAR_CENTROID 3, LINEAR_NOPERSPECTIVE 4, LINEAR_NOPERSPECTIVE_CENTROID 5, LINEAR_SAMPLE 6, LINEAR_NOPERSPECTIVE_SAMPLE 7 (HDR:1449-1475, 2060-2070). **dcl_input_ps_sgv** (0x63) carries no mode (HDR:1497-1514). |
| **dcl_output** (0x65), **_sgv** (0x66), **_siv** (0x67) | o#, or oDepth / oDepthGE / oDepthLE / oMask; plus a NameToken for sgv/siv (HDR:1516-1577). |
| **dcl_temps** (0x68) | One DWORD: the count n declares r0..r(n−1) (HDR:1605-1621). r# plus x# must total at most 4096 (SPEC §7.3; DOC:dcl-temps). |
| **dcl_indexableTemp** (0x69) | Three DWORDs: register index, element count, component count 1..4 (HDR:1623-1640; DOC:dcl-indexabletemp). |
| **dcl_indexRange** (0x5b) | An operand v# or o# with a mask, then a DWORD count (HDR:1580-1603). Indexing outside the span is undefined (SPEC §22.3.30, §7.7). |
| **dcl_inputPrimitive** (0x5d) | [16:11]: `D3D10_SB_PRIMITIVE`: POINT 1, LINE 2, TRIANGLE 3, LINE_ADJ 6, TRIANGLE_ADJ 7, 1..32_CONTROL_POINT_PATCH = 8..39 (HDR:1779-1805, 2089-2130). |
| **dcl_outputTopology** (0x5c) | [17:11]: `D3D10_SB_PRIMITIVE_TOPOLOGY`: POINTLIST 1, LINELIST 2, LINESTRIP 3, TRIANGLELIST 4, TRIANGLESTRIP 5, with *_ADJ 10–13 (HDR:1807-1833, 2072-2087). GS output uses only 1, 3 and 5 (VK tpf.c:463-468). |
| **dcl_maxOutputVertexCount** (0x5e) | One DWORD (HDR:1835-1851). |
| **dcl_gsinstances** (0xce) | One DWORD instance count (HDR:1853-1868; SPEC §22.3.7). |
| **dcl_stream** (0x8f) | One operand m# (0..3). Must come before the dcl_output/outputTopology of that stream; with no streams, stream 0 is implied (DOC:dcl-stream--sm5---asm-; SPEC §22.3.9). |
| **dcl_thread_group** (0x9b) | Three DWORDs x, y, z (HDR:2336-2352). |
| **dcl_input_control_point_count** (0x93), **dcl_output_control_point_count** (0x94) | Count in [16:11] (HDR:1870-1916). VK reads 8 bits (tpf.c:92-93). |
| **dcl_tessellator_domain** (0x95) | [12:11]: ISOLINE 1, TRI 2, QUAD 3 (HDR:1918-1951). |
| **dcl_tessellator_partitioning** (0x96) | [13:11]: INTEGER 1, POW2 2, FRACTIONAL_ODD 3, FRACTIONAL_EVEN 4 (HDR:1953-1987). |
| **dcl_tessellator_output_primitive** (0x97) | [13:11]: POINT 1, LINE 2, TRIANGLE_CW 3, TRIANGLE_CCW 4 (HDR:1989-2023). |
| **dcl_hs_max_tessfactor** (0x98) | One float32 DWORD (HDR:2026-2041). |
| **dcl_hs_fork_phase_instance_count** (0x99), **dcl_hs_join_phase_instance_count** (0x9a) | One UINT each (HDR:2043-2058). |
| HS phase markers | **hs_decls** (0x71), **hs_control_point_phase** (0x72), **hs_fork_phase** (0x73), **hs_join_phase** (0x74): no operands; each starts a sub-program (HDR:264-267; SPEC §22.3.14, §22.3.21, §22.3.23, §22.3.26). Phase-local inputs are vOutputControlPointID, vForkInstanceID and vJoinInstanceID (operand types 22-24). |
| **dcl_function_body** (0x90) | One DWORD: the fb# id (HDR:2231-2247). |
| **dcl_function_table** (0x91) | DWORDs: ft# id, then table length, then that many fb# ids. If bit 31 is set, the true length follows the opcode (HDR:2249-2268). |
| **dcl_interface** (0x92) | [11]: dynamically indexed. DWORDs: fp# id, then the expected table length, then a token with [15:0] the number of tables and [31:16] the array length, then the ft# ids (HDR:2270-2315). At most 253 object sites (this[] slots) and 4096 call sites (DOC:dcl-interface--sm5---asm-; SPEC §22.7.20). |
| Thread-ID inputs | `dcl_input vThreadID` and friends use plain dcl_input with operand types 32-36 (SPEC §22.3.41). |
| Depth outputs | `dcl_output oDepth`, oDepthGE/LE (conservative depth), oMask (SPEC §22.3.37-39). |

- Implementations that ignore parts of these declarations:
  - VK ignores the function-table body lists and the interface table lists, logging FIXMEs (tpf.c:1215-1230).
  - MT treats dcl_stream, dcl_interface, dcl_function_table, dcl_function_body and hs_decls as "ignore atm" (dxbc_converter_cfg.cpp:589-595).
  - DS implements interfaces (dxbc_converter.cpp:465-475, `emitFcall`; dxbc_registers.cpp:143-155).
  - DXVK: grep found no InterfaceCall or FunctionBody handling in dxbc_compiler.cpp. `emitDclInterfaceReg` (line 417) handles I/O registers, not interfaces. So no fcall support was found (not settled beyond the grep).

---

### 3. Opcodes: families and what each needs from a target

#### 3.0 Opcode numbers

These come from Microsoft's enum. It has no explicit values, so I numbered it sequentially (HDR:137-416). The result matches vkd3d's explicit values (VK tpf.c:164-389).

| Range | Opcodes |
|---|---|
| 0x00–0x1f | add, and, break, breakc, **call 0x04**, **callc 0x05**, case, continue, continuec, cut 0x09, default, deriv_rtx 0x0b, deriv_rty 0x0c, discard 0x0d, div 0x0e, dp2–dp4 0x0f–0x11, else 0x12, emit 0x13, **emitThenCut 0x14**, endif, endloop, endswitch 0x17, eq 0x18, exp 0x19, frc 0x1a, ftoi 0x1b, ftou 0x1c, ge 0x1d, iadd 0x1e, if 0x1f |
| 0x20–0x3f | ieq, ige, ilt, imad 0x23, imax, imin, imul 0x26, ine, ineg 0x28, ishl 0x29, ishr 0x2a, itof 0x2b, label 0x2c, ld 0x2d, ld2dms (LD_MS) 0x2e, log 0x2f, loop 0x30, lt 0x31, mad 0x32, min 0x33, max 0x34, customdata 0x35, mov 0x36, movc 0x37, mul 0x38, ne 0x39, nop 0x3a, not 0x3b, or 0x3c, resinfo 0x3d, ret 0x3e, retc 0x3f |
| 0x40–0x57 | round_ne/ni/pi/z 0x40–0x43, rsq 0x44, sample 0x45, sample_c 0x46, sample_c_lz 0x47, sample_l 0x48, sample_d 0x49, sample_b 0x4a, sqrt 0x4b, switch 0x4c, sincos 0x4d, udiv 0x4e, ult 0x4f, uge 0x50, umul 0x51, **umad 0x52**, umax 0x53, umin 0x54, ushr 0x55, utof 0x56, xor 0x57 |
| 0x58–0x6a | the D3D10 declarations listed in §2 |
| 0x6b | reserved |
| 0x6c–0x6f (10.1) | lod 0x6c, gather4 0x6d, samplepos 0x6e, sampleinfo 0x6f |
| 0x70 | reserved |
| 0x71–0x8e (11.0) | hs_decls 0x71, hs_control_point_phase 0x72, hs_fork_phase 0x73, hs_join_phase 0x74, emit_stream 0x75, cut_stream 0x76, **emitThenCut_stream 0x77**, fcall (INTERFACE_CALL) 0x78, bufinfo 0x79, deriv_rtx_coarse 0x7a, deriv_rtx_fine 0x7b, deriv_rty_coarse 0x7c, deriv_rty_fine 0x7d, gather4_c 0x7e, gather4_po 0x7f, gather4_po_c 0x80, rcp 0x81, f32tof16 0x82, f16tof32 0x83, **uaddc 0x84**, **usubb 0x85**, countbits 0x86, firstbit_hi 0x87, firstbit_lo 0x88, firstbit_shi 0x89, ubfe 0x8a, ibfe 0x8b, bfi 0x8c, bfrev 0x8d, swapc 0x8e |
| 0x8f–0xa2 | the D3D11 declarations listed in §2 |
| 0xa3–0xbe | ld_uav_typed 0xa3, store_uav_typed 0xa4, ld_raw 0xa5, store_raw 0xa6, ld_structured 0xa7, store_structured 0xa8, atomic_and/or/xor/cmp_store/iadd/imax/imin/umax/umin 0xa9–0xb1, imm_atomic_alloc 0xb2, imm_atomic_consume 0xb3, imm_atomic_iadd/and/or/xor/exch/cmp_exch/imax/imin/umax/umin 0xb4–0xbd, sync 0xbe |
| 0xbf–0xca | dadd, dmax, dmin, dmul, deq, dge, dlt, dne, dmov, dmovc, dtof, ftod |
| 0xcb–0xd0 | **eval_snapped 0xcb**, eval_sample_index 0xcc, eval_centroid 0xcd, dcl_gsinstances 0xce, **abort 0xcf**, **debug_break 0xd0** |
| 0xd1 | reserved; end of 11.0 |
| 0xd2–0xd9 (11.1) | ddiv, dfma, drcp, msad 0xd5, dtoi, dtou, itod, utod |
| 0xda | reserved |
| 0xdb–0xea (WDDM 1.3, tiled-resource `_s` feedback forms) | gather4_s, gather4_c_s, gather4_po_s, gather4_po_c_s, ld_s, ld2dms_s, ld_uav_typed_s, ld_raw_s, ld_structured_s, sample_l_s, sample_c_lz_s, sample_cl_s 0xe6, sample_b_cl_s, sample_d_cl_s, sample_c_cl_s 0xe9, check_access_fully_mapped 0xea |

- The "sample_*_cl_s" question: these are not plain SM5.0. They are the D3D11.2 tiled-resources feedback opcodes (HDR:391-408). SPEC §22.4.15 writes them as `sample[_aoffimmi][_cl][_s]` and points to Tiled Resources §5.9.4.5. They are absent from DOC:shader-model-5-assembly--directx-hlsl- (that index lists no `_s` or `_cl` forms).
- Opcodes in **bold** have **no entry in vkd3d's TPF table**: call, callc, emitThenCut, umad, emitThenCut_stream, uaddc, usubb, eval_snapped, abort, debug_break. Grep of tpf.c, spirv.c and msl.c finds no handling, so the parser turns them into VSIR_OP_INVALID (tpf.c:2643-2648) (impl). DS handles eval_snapped and the emitThenCut variants (dxbc_converter.cpp:300, 455-456). MT and DXVK handle uaddc and usubb (see below).

#### 3.1 Float arithmetic

- **Opcodes**: add, mul, div, mad, dp2/3/4, min, max, frc, round_ne/ni/pi/z, exp (2^x), log (log2), rsq, rcp, sqrt, sincos (two destinations, either may be null). Modifiers: `_sat` on the result; neg/abs on sources (documented: SPEC §22.10, §22.19.1).
- **Target needs**:
  - IEEE fp32 with RNE add/sub/mul.
  - Denormals flushed to sign-preserved zero except on pure moves (§4.6).
  - A NaN-avoiding min/max (§4.1).
  - `_sat` must clamp NaN to 0: "sat(NaN) returns 0, by the rules for min and max" (SPEC §22.19.1).
  - Control over FMA contraction (§4.10).
  - sincos may be split into separate sin and cos (impl: MT dxbc_converter_base.cpp:1164-1181; VK ir.c:2501 `vsir_program_lower_sm4_sincos`).
- **How each implementation emits them**:
  - MT (AIR): `air.log2`, `exp2`, `rsqrt`, `sqrt`, `fract`, `rint`, `floor`, `ceil`, `trunc`; rcp as `1.0/x` (FDiv); min/max as `air.fmin`/`fmax`; mad as `air.fma` (dxbc_converter_base.cpp:913-989, 1153-1161).
  - vkd3d MSL: `exp2`, `fract`, `fma`, `max`, `min`, `rint`, `trunc`, `rsqrt`, `sqrt`; `/` for div (msl.c:1531-1675).
  - HLSLcc Metal: `fma` for mad, `min`/`max`, `rsqrt`, and rcp as `T(1.0)/x` (toMetalInstruction.cpp:2046-2054, 2534-2542, 2634-2672, 3688-3718).

#### 3.2 Integer and bit operations

- **Opcodes**:
  - iadd, imad, umad, ineg, imin/imax/umin/umax, and/or/xor/not, ishl/ishr/ushr.
  - imul/umul: two destinations, destHI then destLO, giving the full 64-bit product; either may be null.
  - udiv: destQUOT then destREM. **There is no signed divide in SM4/5**; the SM5 index has no idiv (DOC:shader-model-5-assembly--directx-hlsl- lists only udiv).
  - uaddc/usubb: the second destination is the carry/borrow, 1 or 0.
  - countbits, firstbit_hi/lo/shi, ubfe/ibfe/bfi, bfrev, msad (11.1).
  - Source: documented, SPEC §22.11, §22.12.
- **Bitfield rules**:
  - ubfe/ibfe/bfi use only the low 5 bits of width and offset. width==0 gives 0. When width+offset ≥ 32, the result is a plain shift by offset (SPEC §22.11.2, §22.11.6, §22.11.11).
  - Target needs: an explicit `&31` on width and offset, and a clamp of width to 32−offset before calling a native bitfield extract.
  - impl: DS clamps with `UMin(count, 32-offset)` after `&0x1f` (dxbc_converter.cpp:1640-1682, 3429-3435). VK spirv.c:7864-7923 does the same per component. MT open-codes the spec's shift formula (dxbc_converter_base.cpp:1303-1347).
- **firstbit**:
  - Every variant returns 0xffffffff when no bit matches. hi and shi count **from the MSB** (SPEC §22.11.5: "firstbit_hi on 0x10000000 returns 3").
  - **The DOC page disagrees**: DOC:firstbit--sm5---asm- says "firstbit_hi on 0x10000000 returns 28".
  - Every implementation follows the spec, giving `31 − findMSB`:
    - DXVK dxbc_compiler.cpp:2794-2808: "The 'Hi' variants are counted from the MSB in DXBC rather than the LSB".
    - DS dxbc_converter.cpp:1742-1772.
    - MT uses count-leading-zeros (dxbc_converter_base.cpp:1011-1027).
    - VK msl.c:711-729 uses `clz`/`ctz` with a 0xffffffff select.
  - Treat the DOC example as wrong.
- **msad**: a masked sum of absolute differences per byte; reference bytes of 0 are skipped. Overflow is "recommended" to saturate, but wrapping is allowed, so it is undefined for applications (SPEC §22.12.15). MT ships a Metal helper, `airconv/shaders/air_msad.metal` (impl; contents not reviewed).
- **Two-destination ops in Metal**:
  - MT: umul/imul high as `air.mul_hi`, low as a plain mul; uaddc/usubb via the LLVM `uadd/usub.with.overflow` intrinsics (dxbc_converter_base.cpp:1230-1300).
  - DS lowers swapc and the two-destination ops into separate IR ops (dxbc_converter.cpp:967, 1399, 1503).
- **f16tof32 / f32tof16**:
  - The low 16 bits carry the value; f32tof16 zeroes the upper 16 bits. Both follow the D3D conversion rules (SPEC §22.13.1-2): float→float16 rounds toward zero, and values above the range become max-representable, not INF (SPEC §3.2.2).
  - DS's IR documents round-to-zero for `ConvertF32toPackedF16` (ir/ir.md:153-160).
  - Whether Metal's native half conversion rounds to nearest even: not settled (Metal spec not consulted).

#### 3.3 Conversions and comparisons

- **Conversions**:
  - ftoi/ftou: round toward zero, saturating, NaN→0 (§4.3).
  - itof/utof: RNE "strongly recommended but not required" (SPEC §22.13.5-6).
  - Double conversions are dtof/ftod/dtoi/dtou/itod/utod (SPEC §22.17.1-6).
- **Comparisons**:
  - eq/ne/lt/ge (float); ieq/ine/ilt/ige; ult/uge. Each writes 0xFFFFFFFF or 0 per component.
  - Float eq/lt/ge are ordered: false if either operand is NaN. ne is unordered: true for NaN. ±0 compare equal (SPEC §3.1.3.1).
  - impl: MT uses FCMP_OEQ/UNE/OGE/OLT and sign-extends to 32 bits (dxbc_converter_base.cpp:1124-1150). VK's opcode table names them EQO, NEU, GEO, LTO (tpf.c:1472, 1477, 1498, 1507). vkd3d MSL emits `select(0u, 0xffffffffu, a op b)`, relying on Metal's comparison operators being ordered (msl.c:756-778); NE uses `!=` (msl.c:1613-1614), which is unordered in C-like languages (not settled for Metal).
- **movc**: "any bit set" on the condition chooses src1, else src2, per component. With no modifiers the bits are copied unchanged, so denormals and NaN payloads are preserved (SPEC §22.9.2; impl MT dxbc_converter_base.cpp:865-874 uses an integer `!= 0` select). The spec defines **swapc** through two movc instructions, with destinations written only at the end (SPEC §22.9.3; DOC:swapc--sm5---asm-).

#### 3.4 Control flow

- **Opcodes**: if_z/if_nz, else, endif, loop/endloop, break, breakc_z/nz, continue, continuec, switch/case/default/endswitch, call l#, callc_z/nz, ret, retc_z/nz, label l#.
- **Rules**:
  - switch compares bitwise as integers. A case that falls through to another case may not contain code (SPEC §22.7.18).
  - Nesting of if/loop/switch is limited to 64 per subroutine; deeper is undefined (SPEC §7.12, §22.7.4).
  - The call depth limit is 32. A call issued at depth 32 is **skipped** (SPEC §22.7.10 restriction 3).
  - There is no automatic parameter stack; use x# arrays (SPEC §22.7.10).
- **Target needs**: structured control flow for Metal. The implementations build a CFG and structurise:
  - VK lowers switch to a selection ladder (ir.c:6895).
  - DS uses scoped CF (dxbc/dxbc_control_flow.h).
  - MT builds basic blocks (dxbc_converter_cfg.cpp).
  - Subroutines (`call`) need inlining or real functions. The skip-at-depth-32 rule is not implemented by any open implementation (not settled; no handling found).
  - DXVK emits a UAV barrier around call, callc and retc (dxbc_compiler.cpp:4835-4860).
- **fcall** (0x78) means "call function body fp#[arrayIndex][callSite]":
  - fp#+arrayIndex selects a function table bound at the API.
  - A dynamic arrayIndex that diverges between lockstep invocations is undefined, and so is an out-of-bounds index ("including … Device Lost") (SPEC §22.7.19; DOC:fcall--sm5---asm-).
- **this[]**: up to 253 read-only uint4 slots holding (cb index, base element, base texture, base sampler). Out-of-bounds reads give undefined data (SPEC §22.7.20).
- A Metal target needs either a switch over a runtime table index, or specialisation per binding. DS implements fcall (dxbc_converter.cpp:474); MT and VK do not (see §2).

#### 3.5 discard and derivatives

- **discard_z/_nz** "flags the current pixel as terminated, while continuing execution" so that other pixels can still compute derivatives. All outputs are discarded (SPEC §22.5.1; DOC:discard--sm4---asm-). Further rules are in §4.9.
- **Derivatives**:
  - deriv_rtx/rty: SM4 forms 0x0b/0x0c.
  - deriv_rt{x,y}_coarse: computed once per 2x2 quad; whether the current pixel takes part is implementation-defined.
  - deriv_rt{x,y}_fine: per-pixel deltas within the row or column (SPEC §22.5.2-5; DOC:deriv-rtx-coarse--sm5---asm-).
  - Pixel shader only. Results that depend on UAV reads must not feed derivatives (SPEC §16.13).
- **Implementations**:
  - SPIR-V back ends use DPdxCoarse/Fine (VK spirv.c:8369-8395).
  - All three Metal back ends emit plain `dfdx`/`dfdy` for both coarse and fine: VK msl.c:1505-1518; HC toMetalInstruction.cpp:3791-3799; MT `CreateDerivative` with only an x/y flag (dxbc_converter_base.cpp:1084-1090).
  - Whether Metal offers a coarse/fine distinction: not settled (Metal spec not consulted).
- **lod** (0x6c) returns (ClampedLOD, UnclampedLOD, 0, 0). It is undefined with MIP_POINT samplers, returns 0 when nothing is bound, and is PS only (SPEC §22.5.6).

#### 3.6 Texture access

- **sample family** (PS only for the implicit-LOD forms sample, sample_b and sample_c):
  - sample, sample_b (bias), sample_l (explicit LOD), sample_d (gradients), sample_c (compare), sample_c_lz (compare at LOD 0).
  - `_aoffimmi(u,v,w)` gives immediate offsets in −8..7. Every t# reference carries the view dimension (SPEC §7.11.6, §22.4.15-20).
  - Address modes come from the sampler; out-of-range coordinates wrap, mirror or clamp per sampler state (SPEC §22.4.15).
- **ld** (0x2d):
  - Integer texel coordinates with mip in .w (array slice in .y for 1DArray, .z for 2DArray). No filtering.
  - Out of bounds (texel, mip or slice) returns 0 for components present in the format and the format default for missing ones; an unbound t# returns 0.
  - float32 data passes through bit-exact, so denormals survive.
  - `_aoffimmi` that drives the coordinate below 0 wraps to a large unsigned value and so goes out of bounds (SPEC §22.4.6).
- **ld2dms** (0x2e) adds a sample index. Its out-of-bounds behaviour for a bad sample index is "undefined, but cannot return data outside the address space" (SPEC §22.4.7, near spec.txt:15901-15913).
- **gather4** (0x6d): 2D/Cube textures. **gather4_c**: compare. **gather4_po/_po_c**: programmable offset. For the _po forms only the 6 low bits of each offset count, giving −32..31; they are 2D only, only the address modes are honoured, and only the most detailed mip is used (DOC:gather4-po--sm5---asm-; SPEC §22.4.2-5).
- **resinfo[_uint|_rcpFloat]** (0x3d) returns (w, h, d-or-array-size, mipcount):
  - 1D sets y and z to 0; 2D sets z to 0. A cube array's z is **undefined** (spec "oversight").
  - An out-of-range mip returns 0 for xyz but keeps .w. `_rcpFloat` turns 0 into INF.
  - The dimensions of a mip "clamped off" by a resource mip clamp are undefined (SPEC §22.4.14; DOC:resinfo--sm4---asm-).
  - DXVK fixes the out-of-range mip by selecting 0 (dxbc_compiler.cpp:3551-3556).
- **bufinfo** (0x79) returns element count for typed views, **byte count** for raw views, and struct count for structured views (SPEC §22.4.1; DOC:bufinfo---sm5---asm-).
- **sampleinfo[_uint]** (0x6f): sample count of a t# or of the rasterizer; 0 if unbound.
- **samplepos** (0x6e): position of the sample relative to the pixel centre; an out-of-bounds index gives a 0 vector (SPEC §22.4.21-22).
- **eval_sample_index** (0xcc; out-of-bounds index undefined), **eval_centroid** (0xcd), **eval_snapped** (0xcb): pull-model interpolation.
  - eval_snapped uses the low 4 bits of U and V as signed fixed point in 1/16 pixel (−8/16..7/16).
  - The attribute must be linear or linear_noperspective, or constant (no effect). The attribute's centroid/sample qualifier is ignored (SPEC §22.4.23-25, §16.5.2).
- **Metal needs**:
  - Texture plus sampler objects, each declaring dimension and return type.
  - Explicit-LOD, bias, gradient and compare sampling; gather with offset.
  - Read with LOD, and MS read with a sample index.
  - Size and mip-count queries; a sample-position query (MT ships `shaders/air_samplepos.metal`); interpolation at sample, centroid and offset.
- **What the Metal back ends cover**:
  - MT has handlers for every texture op (dxbc_converter_base.cpp:1350 onward).
  - vkd3d MSL handles sample, sample_b, sample_c, sample_c_lz, sample_d, sample_l, gather4, gather4_c, gather4_po and ld/ld2dms. Other ops fall to `msl_unhandled` (msl.c:1550-1560, 1625-1627, 1690-1693).

#### 3.7 Buffer, UAV and TGSM memory

- **Opcodes**:
  - ld_raw / store_raw: byte offset, 4-byte aligned, 1-4 DWORDs.
  - ld_structured / store_structured: struct index plus byte offset.
  - ld_uav_typed / store_uav_typed: store_uav_typed writes all of .xyzw (SPEC §22.4.8-13; DOC:ld-raw--sm5---asm-, DOC:ld-structured--sm5---asm-).
- **Alignment**: a misaligned raw/structured address on a write makes the whole UAV, or all TGSM, undefined. On a read, the result is undefined (SPEC §7.13).
- **Atomics** (no return value): atomic_and/or/xor/cmp_store/iadd/imax/imin/umax/umin on u# (raw, structured, or typed R32_UINT/SINT) or g#.
- **imm_atomic_*** forms return the old value. imm_atomic_alloc returns the old counter; imm_atomic_consume returns the **new** (decremented) counter. Counters wrap. A shader may not mix alloc and consume on one UAV (SPEC §22.17.8-28; DOC:imm-atomic-alloc--sm5---asm-).
- **Atomicity is at 32-bit granularity**:
  - Atomics do not imply fences.
  - TGSM atomics are **not** atomic with respect to plain TGSM stores. UAV atomics are atomic with respect to both loads and stores (SPEC §7.14.2).
- **Coherence**:
  - A UAV without `_glc` is only group-coherent in CS and locally coherent in PS.
  - Cross-group communication by plain loads needs `_glc` (SPEC §7.14.4).
- **sync** (0xbe):
  - `_uglobal`: UAV fence at device scope, or group scope for non-`_glc` UAVs in CS.
  - `_ugroup`: UAV fence at group scope. fxc never emits it.
  - `_g`: TGSM fence. `_t`: execution barrier, which must be in uniform control flow.
  - Graphics stages allow only sync_uglobal.
  - discard implies a sync_uglobal fence (SPEC §22.17.7; DOC:sync--sm5---asm-).
- **How implementations map sync**:
  - VK → OpControlBarrier/OpMemoryBarrier with Workgroup or Device scope (spirv.c:9820-9868).
  - MT → `air` fence plus barrier with Threadgroup, Device and Texture memory flags (dxbc_converter_base.hpp:244-260).
  - vkd3d MSL → `msl_barrier` (msl.c:1683-1685).
- **Metal needs**: device buffers with byte-length metadata for bounds handling (see §4.4), 32-bit atomics on device and threadgroup memory, and a hidden counter buffer for append/consume. Rasterizer-ordered UAVs (bit 17) also need raster-order groups; DS has `RovScopedLock*` (ir/ir.md:505-512). Metal ROV support: not settled.

#### 3.8 Geometry-shader streams and tessellation

- **GS stream ops**:
  - emit, cut, emitThenCut (SM4).
  - emit_stream m#, cut_stream m#, emitThenCut_stream m#.
  - After an emit, **all** output registers of all streams become uninitialised.
  - streamIndex is an immediate 0..3 (DOC:emit-stream--sm5---asm-; SPEC §22.8).
- **HS**: phases (§2). The control-point phase runs once per output control point; fork and join phases run once per instance count. Hull/domain-shader pairing rules are in SPEC §11 and §22.3.14-28.
- **Metal**: Metal has no geometry shader or D3D-style hull shader. MT emulates them in dxbc_converter_gs.cpp and dxbc_converter_ts.cpp (sizes 892 and 1056 lines) and ships `shaders/air_tessellation.metal` (impl). The emulation strategy itself was not reviewed (not settled).

#### 3.9 Doubles (optional, 5_0 with ENABLE_DOUBLE_PRECISION_FLOAT_OPS)

- **Opcodes**: dadd, dmul, dmin, dmax, deq, dge, dlt, dne, dmov, dmovc, dtof, ftod. 11.1 adds ddiv, dfma, drcp, dtoi, dtou, itod and utod.
- **Rules**:
  - IEEE 754R, except drcp, which allows 1 ULP.
  - **Double denormals are preserved**.
  - A pair (lo in x, hi in y) occupies xy or zw. Allowed swizzles are .xyzw, .xyxy, .zwxy and .zwzw; masks are .xy, .zw and .xyzw (SPEC §3.1.4).
- VK sets the f64 denormal mode to PRESERVE when the flag is present (tpf.c:1197-1199). DS uses ePreserve for f64 (dxbc_converter.cpp:558-571).
- Metal fp64 shader support: not settled (Metal spec not consulted).

#### 3.10 Misc

- **nop**: does nothing (SPEC §22.10.13; DOC:nop--sm4---asm-).
- **customdata**: see §2.
- **abort** and **debug_break**: DS ignores them (dxbc_converter.cpp:477-479).
- **check_access_fully_mapped** consumes the `_s` status value (SPEC §22.4.26).

---

### 4. Undefined or implementation-defined behaviour a faithful converter must pin down

Each entry gives the Microsoft rule, then what each open implementation does.

#### 4.1 min/max and NaN

- **Spec**:
  - `min(x,NaN)==min(NaN,x)==x`, and likewise for max. NaN on both sides returns any NaN.
  - Denormals are flushed before the comparison, but the result "may or may not be" flushed.
  - min(−0,+0) returning −0 is recommended but not enforced (SPEC §3.1.3.1-2, §22.10.10-11; DOC:min--sm4---asm-, DOC:max--sm4---asm-).
  - `max` is defined with `>=` so that min and max of the same pair return different operands (SPEC §22.10.10).
- **Implementations**:
  - VK SPIR-V: GLSLstd450 NMin/NMax, which return the non-NaN operand (spirv.c:7466-7467). vkd3d MSL: plain `min`/`max` (msl.c:1601-1608).
  - DXVK old: OpNMin/NMax (dxbc_compiler.cpp:1675-1685).
  - DS: `FMin`/`FMax` IR mapped to GLSLstd450 NMin/NMax (spirv/spirv_builder.cpp:3725-3728).
  - MT: `air.fmin`/`air.fmax` (dxbc_converter_base.cpp:978-985). HC Metal: `min`/`max` (toMetalInstruction.cpp:2634-2672).
  - Whether Metal's `min`/`fmin` drop NaN: not settled (Metal spec not consulted). An implementer must check it, or emit an explicit isnan select.
- **_sat and NaN**: `_sat` must turn NaN into 0 (SPEC §22.19.1). DS notes "Clamping will flush any NaN values to zero as required by D3D" (dxbc_converter.cpp:1272-1274). For MT and vkd3d MSL (the latter emits `saturate`, msl.c:1668-1670), Metal `saturate(NaN)` behaviour is not settled.

#### 4.2 Shift amounts

- **Spec**: ishl/ishr/ushr use the **low 5 bits** of the shift amount. In SM5 the count is per component; in SM4 it is a scalar `.select_component` (SPEC §22.11.7-8, §22.11.12; DOC:ishl--sm5---asm-, DOC:ishl--sm4---asm-, DOC:ushr--sm5---asm-, DOC:ishr--sm5---asm-). The bitfield ops also mask width and offset to 5 bits (§3.2).
- **Implementations**:
  - VK SPIR-V ANDs the count with 0x1f. Its comment says fxc emits immediates larger than 5 bits (spirv.c:7385-7398).
  - **vkd3d MSL does not mask**: it emits raw `<<`/`>>` (msl.c:1586-1591).
  - **DXVK old masks only non-immediate counts** (dxbc_compiler.cpp:2264-2265: "if (ins.src[1].type != DxbcOperandType::Imm32)"), so an oversized immediate count from fxc goes through unmasked.
  - DS masks every count via `loadSrcBitCount`, `BitCountMask = 0x1f` (dxbc_converter.cpp:1558-1588, 3429-3435; dxbc_converter.h:32). Its constant folder also masks with `& 31u` (ir/passes/ir_pass_arithmetic.cpp:4414-4463).
  - MT masks with `& 0x1f` (dxbc_converter_base.cpp:1057-1065).
  - **HC does not mask** (toMetalInstruction.cpp:3161-3200).

#### 4.3 Float→int conversion: ftoi/ftou, NaN and out-of-range values

- **Spec**:
  - Round toward zero.
  - ftoi clamps its input to [−2147483648.999, 2147483647.999]; ftou to [0, 4294967295.999]. NaN gives 0. neg/abs are applied first (SPEC §22.13.3-4; DOC:ftoi--sm4---asm-, DOC:ftou--sm4---asm-).
  - All other float→integer conversions use RNE (SPEC §3.2.2).
- **Implementations**:
  - VK SPIR-V: max with INT_MIN, select INT_MAX when ≥ 2^31, select 0 when NaN. ftou: max(x,0), which flushes NaN through NMax, then select UINT_MAX when ≥ 2^32 (spirv.c:7724-7839).
  - **vkd3d MSL: plain `int(x)`/`uint(x)` casts with no clamp and no NaN handling** (msl.c:780-799, 1544-1548).
  - **DXVK old: plain OpConvertFtoS/FtoU** (dxbc_compiler.cpp:1824-1832).
  - DS: a saturating helper function with explicit NaN→0 for the signed case. Unsigned relies on "Max will implicitly flush nan to 0" (ir/passes/ir_pass_arithmetic.cpp:~700-770).
  - MT: `air.convert.s.i32.f32` / `.u.i32.f32` with no explicit clamp (dxbc_converter_base.cpp:1106-1114; nt/air_builder.cpp:1322-1360). Whether Metal's AIR convert saturates: not settled.
  - HC: plain cast (toMetalInstruction.cpp:1925-1945).
- **itof/utof**: RNE is recommended, not required (SPEC §22.13.5-6).

#### 4.4 Integer division by zero, and signed division

- **Spec**: udiv by 0 returns 0xFFFFFFFF in **both** quotient and remainder (SPEC §22.12.9; DOC:udiv--sm4---asm-). SM4/5 has no signed integer divide (DOC:shader-model-5-assembly--directx-hlsl-); HLSL `int / int` compiles to other sequences (not settled: fxc's exact expansion not examined).
- **Implementations**:
  - VK lowers UDIV to UDIV_SIMPLE/UREM plus a MOVC on the divisor, with UINT_MAX when it is zero. This happens in common VSIR, so SPIR-V and MSL both get it (ir.c:2303-2420, especially 2376-2379; dispatch at ir.c:4267).
  - DXVK old: OpUDiv/UMod, then `select(b != 0, r, ~0u)` (dxbc_compiler.cpp:2145-2190).
  - DS: `lowerUDiv` folds a constant zero divisor to −1, otherwise calls a helper function (ir/passes/ir_pass_arithmetic.cpp:598-623).
  - MT: `select(b==0, all-ones, udiv/urem)` (dxbc_converter_base.cpp:1256-1271).
  - **HC: plain `/` and `%` with no zero check** (toMetalInstruction.cpp:2283-2305). In Metal, integer division by zero is undefined (not settled; Metal spec not consulted).

#### 4.5 Out-of-bounds access

**Microsoft's rules (documented, by resource kind):**

- **Constant buffers**:
  - An out-of-bounds read returns 0 in every component, measured against the size of the *bound* buffer.
  - Between the declared size and the bound size, any data may come back.
  - An unbound slot reads 0.
  - CB slot indexing out of [0..13] behaves as unbound (SPEC §7.5, §7.11.2).
- **Immediate constant buffer**: "accessed … exactly the same way as Constant Buffers" (SPEC §7.5.1). By implication an out-of-bounds read returns 0 (inferred; the spec does not restate it).
- **Indexable temps x#**: out-of-bounds access is undefined, except that data outside the GPU process context is never visible (SPEC §7.3). Reading an unwritten temp is undefined (SPEC §7.3).
- **Dynamically indexed inputs and outputs** outside the declared range are undefined (SPEC §7.7, §22.3.30).
- **this[]** out of bounds: undefined (SPEC §22.7.20).
- **fcall** out of bounds: undefined, possibly device lost (SPEC §22.7.19).
- **Textures**:
  - ld out of bounds (coordinate, mip or slice) returns 0 plus format defaults. An unbound t# reads 0.
  - sample applies the sampler address modes.
  - Out-of-bounds resource *slot* indexing behaves as unbound.
  - Sampler slot indexing out of bounds is undefined (SPEC §22.4.6, §7.11.2).
- **Typed UAV**: an out-of-bounds load behaves like ld; an out-of-bounds store writes nothing (SPEC §22.4.8-9).
- **Raw buffers**:
  - Per DWORD, an out-of-bounds read returns 0 on t#/u#. On g# (bounds of that g#) the result is undefined.
  - An out-of-bounds store to u# drops only the out-of-bounds part. An out-of-bounds store to g# makes **all TGSM undefined** (SPEC §22.4.10-11).
- **Structured buffers**:
  - Like raw, except that when the *byte offset* causes the out-of-bounds access, reads return undefined for all components and writes make the **whole UAV undefined** (SPEC §22.4.12-13).
- **Atomics**:
  - An out-of-bounds address on u# writes nothing, except that a structured offset fault makes the whole UAV undefined.
  - On g#, an out-of-bounds address makes all TGSM undefined.
  - imm_* forms return undefined values (SPEC §22.17.8, §22.17.19).
- **Device memory writes, general rule**: SPEC §18.6.8.3 "Device Memory Writes" (Compute Shader chapter, spec.txt:11638) says that "out-of-bounds array indices cause the write to be ignored, though out of bounds offsets within individual structs cause the entire contents of the resource to be undefined".

**What the implementations do:**

- **DXVK old**:
  - Dynamically indexed CBs are declared with 4096 elements, relying on API robustness (dxbc_compiler.cpp:820-823).
  - The ICB is padded with one zero vector and its index clamped with UMin, so out-of-bounds reads give 0 (dxbc_compiler.cpp:1541-1543, 5496-5500). In UBO mode it relies on 4096-element robustness (1582-1583).
  - x# and index-range inputs: an out-of-bounds read returns zero through a phi, and an out-of-bounds write is skipped (dxbc_compiler.cpp:5972-6040, 6188-6201).
  - Raw/structured addressing is a flat DWORD index, `struct*stride/4 + offset/4` (dxbc_compiler.cpp:5843-5870), relying on buffer robustness (not settled; robustness setup not reviewed).
- **DS**:
  - An optional scratch bound-check pass, `enableBoundChecking` (ir/passes/ir_pass_scratch.cpp:84-110, 479-486).
  - `boundCheckShaderIo = true` by default (dxbc/dxbc_converter.h:44).
  - The ICB is either a constant array or lowered to a CBV (dxbc_converter.cpp:661-691, 3263-3295).
- **VK**: no shader-side bounds checks were found in spirv.c or msl.c by grep. The ICB is a SPIR-V constant array (spirv.c:4619-4675). Out-of-bounds behaviour is therefore left to the API's robustness settings (not settled).
- **MT**:
  - Raw/structured buffer access selects a null pointer when the DWORD index is ≥ byteLength/4 (`CreateGEPInt32WithBoundCheck`, dxbc_converter_base.cpp:2905-2927). What a null-pointer access does on Metal: not settled.
  - Typed buffer (texture_buffer) ld and ld_uav_typed replace an out-of-bounds address with all-ones (dxbc_converter_base.cpp:1360-1366, 1415-1416, 1435-1440, 1471-1472, 2435-2465).
  - x# load and store use unchecked GEPs, and the store uses an *inbounds* GEP (dxbc_converter_base.cpp:277-313, 700-720). So an out-of-bounds x# access has no protection (impl).
- **HC**: bounds handling not examined (not settled).

#### 4.6 Denormals

- **Spec**:
  - fp32 denormals **must** be flushed to sign-preserved zero on input and output of any float maths.
  - Exception: pure data movement (ld, mov, movc, swapc, but *not* min/max) must preserve the bits. A multiply by 1.0 is still maths and flushes.
  - min/max flush for the comparison, but the result may or may not be flushed.
  - State values such as viewport depth or border colour may or may not be flushed.
  - fp16 and fp10/11 **preserve** denormals; fp64 **preserves** them (SPEC §3.1.3.2, §3.1.4-6).
  - Conversions: a denorm in a higher-range format becomes the lower format's denorm if it exists, else 0. fp32 counts as having no denorms (SPEC §3.2.2).
- **Implementations**:
  - VK: TPF input sets the f32 mode to FLUSH_TO_ZERO (tpf.c:2807) and f64 to PRESERVE when doubles are enabled (tpf.c:1197-1199). It is emitted via SPV_KHR_float_controls (spirv.c:6104-6129).
  - **vkd3d MSL refuses** any denormal mode other than ANY ("The target environment does not support float controls", msl.c:2305-2312). For API ≤ 1.19 the options default to ANY (ir.c:905-920). How that interacts with TPF's FTZ default is not settled.
  - DS: `SetFpMode` f32 RNE + eFlush; f16 and f64 ePreserve (dxbc_converter.cpp:557-571).
  - DXVK old: no denorm execution mode found by grep (not settled).
  - MT: no denorm control found; it sets fast-math flags (§4.10). Not settled what Metal does by default.

#### 4.7 Precision of the transcendental and reciprocal ops

| Operation | Precision rule | Source |
|---|---|---|
| add, sub, mul | 0.5 ULP (RNE) | SPEC §3.1.3.1 |
| **sqrt** | 1 ULP | SPEC §3.1.3.2, §22.10.21; DOC:sqrt--sm4---asm- |
| **div** | either a/b, or a*(1/b) with the reciprocal at 1 ULP and the multiply at 0.5 ULP; a direct a/b must be at least as accurate. Consequence: a denominator above 8.5070592e+37 can flush 1/b to 0, so INF/large may be NaN on some hardware | SPEC §3.1.3.2, §22.10.2; DOC:div--sm4---asm- |
| **rcp** (sm5 instruction) | max relative error 2^-21 ("reduced precision reciprocal, independent of the strict requirements for divide") | SPEC §22.10.18; DOC:rcp--sm5---asm- |
| **rsq** | relative error 2^-21 | SPEC §22.10.19; DOC:rsq--sm4---asm- |
| **exp** | relative error 2^-21 | SPEC §22.10.6; DOC:exp--sm4---asm- |
| **log** | absolute error 2^-21 on [0.5,2], relative 2^-21 elsewhere | SPEC §22.10.8; DOC:log--sm4---asm- |
| **sincos** | absolute error 0.0008 on [−100π, 100π] | SPEC §22.10.20; DOC:sincos--sm4---asm- |
| drcp | 1 ULP | SPEC §3.1.4 |
| other double ops | IEEE | SPEC §3.1.4 |
| float→integer conversion | 0.6 ULP (constant D3D11_FLOAT32_TO_INTEGER_TOLERANCE_IN_ULP) | SPEC constants table, spec.txt:21916 |
| fused fp16 | 0.6 ULP | spec.txt:21914 |

- The spec text in §3.1.3.2 names the rsq instruction as "rsw(22.10.19)", a typo.
- "2-21" in the converted text is 2^-21; the superscript was lost in conversion.
- Special-value rules (SPEC §3.1.3.1): log(±0) = −INF; log(negative) = NaN; sqrt/rsq of a negative = NaN; sqrt(−0) = −0; rsq(−0) = −INF; x/0 = ±INF; 0/0 = NaN.
- **Implementations**:
  - rcp as an exact division: VK spirv.c:7682-7701, DXVK 1693-1699, DS spirv_builder.cpp:3809-3840, HC 3688-3718.
  - MT also uses FDiv, but under fast-math flags that allow reciprocal approximation in PS and CS (dxbc_converter_base.cpp:927-929, 2942-2948).
  - rsq maps to InverseSqrt or `rsqrt` everywhere.
  - Whether Metal's `rsqrt`, `exp2`, `log2` and `sin`/`cos` meet the D3D bounds (in fast or precise math): not settled.

#### 4.8 mad: fused or not

- **Spec**: fused operations must be no less accurate than the worst serial ordering of the unfused steps, may keep extra intermediate precision, and must follow the NaN rules of some unfused ordering (SPEC §3.1.3.2). So both mul+add and true FMA satisfy `mad`. Under `precise`, the hardware "must execute a mad (or exact equivalent), and cannot split it", and a precise mul+add "cannot be merged" (SPEC §22.19.2).
- **Implementations**:
  - VK: `mad` → GLSLstd450Fma, and a precise mad is split into mul + add with NoContraction (spirv.c:7465; ir.c:2124-2161, 4197-4198). This is the **opposite** of the spec's wording for precise mad.
  - vkd3d MSL: always `fma` (msl.c:1598-1599), after the same precise split.
  - DXVK old: a non-precise mad becomes mul+add; a precise mad becomes FFma (dxbc_compiler.cpp:1662-1673: "FXC only emits precise mad if the shader explicitly uses the HLSL mad()/fma() intrinsics").
  - DS: a non-precise FMad is split and later re-fused for invariance. A precise one stays fused ("Elite Dangerous relies on this") (ir/passes/ir_pass_arithmetic.cpp:353-399).
  - MT: `air.fma`, unless the per-shader flag `SM50_SHADER_FLAG_DEFUSE_FMA` is set (nt/air_builder.cpp:1177-1188; dxbc_converter.cpp:408).
  - HC: `fma` (toMetalInstruction.cpp:2046-2054).

#### 4.9 discard semantics and helper invocations

- **Spec**:
  - A discarded pixel keeps executing, so derivatives still work. UAV accesses made before the discard complete.
  - After the discard, UAV writes and atomics have no effect and return undefined values.
  - Helper pixels: writes are ignored and immediate atomics return undefined values (SPEC §16.13).
  - The SPEC contradicts itself: §16.12.2 says helper immediate atomics "return 0".
  - A discard is an implicit memory fence that prevents reordering across it (SPEC §16.13, §22.17.7).
- **Target needs**: "demote" semantics, not "terminate".
- **Implementations**:
  - VK SPIR-V: OpDemoteToHelperInvocation if available, else OpKill (spirv.c:8181-8235).
  - vkd3d MSL: `discard_fragment()`, assuming Metal ≥ 2.3, where its comment says discard_fragment behaves like demote (msl.c:828-839). That claim is not checked against the Metal spec here.
  - DXVK old: demote, and it marks the top level non-uniform (dxbc_compiler.cpp:4690-4710). DS: demote (dxbc_converter.cpp:3021-3039; spirv_builder.cpp:2729-2732).
  - MT: `air.CreateDiscard()` in a conditional block (nt/dxbc_converter_base.hpp:239-241; dxbc_converter_cfg.cpp:244-249). HC: `discard_fragment()` (toMetalInstruction.cpp:3241-3263).
  - Whether any implementation enforces "no UAV writes after discard": not settled; no explicit masking found.

#### 4.10 Precise and refactoring

- **Spec**: without REFACTORING_ALLOWED, operations must run in the order written. The precise modifier is only legal when the flag is present and works per component (SPEC §22.3.2, §22.19.2).
- **Implementations**:
  - DXVK old treats everything as precise (`m_precise = true`, dxbc_compiler.h:570) unless the flag is set (dxbc_compiler.cpp:357-361). Precise results get NoContraction (1841-1842).
  - DS defaults to `ePrecise`; RefactoringAllowed removes it and adds `eNoSz` (dxbc/dxbc_converter.h:110; dxbc_converter.cpp:697-703).
  - VK **ignores** REFACTORING_ALLOWED (spirv.c:6133-6141). Only the per-instruction precise bits become NoContraction (spirv.c:7402-7403).
  - **MT ignores the global flag**. It turns on fast-math (contract, reassoc, reciprocal in PS/CS; approx-func and no-signed-zeros in all stages) unless the instruction's precise mask is set (dxbc_converter_base.cpp:2929-2950). Its comment: "fp contraction&refactoring is simply disabled for pre-raster stage to reduce the chance of depth prepass z-fighting".
  - **This MT behaviour departs from the spec's default strict ordering.**

#### 4.11 Other loose ends

- **Temporaries**: reading uninitialised r#/x# is undefined (SPEC §7.3). A GS emit makes all outputs undefined (DOC:emit-stream--sm5---asm-). Failing to write a declared oDepth or oMask is undefined (SPEC §16.9.2, spec.txt:10781).
- **Uniformity**: resource and sampler indexing must be uniform; violations are undefined (SPEC §7.11.1). The non-uniform extended operand bit (bit 17, D3D12 only) marks the exceptions.
- **_t sync** is illegal in divergent control flow (SPEC §22.17.7).
- **Unaligned raw access**: the whole UAV or all TGSM becomes undefined (SPEC §7.13).

## Part 3. Reflection: D3DReflect, ID3D11ShaderReflection, D3DGetBlobPart, D3DStripShader

### 3.1 D3DReflect entry point and versioned IIDs

[doc] D3DReflect(pSrcData, SrcDataSize, pInterface IID, ppReflector) "Gets a pointer to a reflection interface"; it returns
Direct3D 11 return codes. https://learn.microsoft.com/en-us/windows/win32/api/d3dcompiler/nf-d3dcompiler-d3dreflect

The IID of ID3D11ShaderReflection **changed between d3dcompiler versions** (wine/include/d3d11shader.h:211-221):

| IID | Value | d3dcompiler |
|---|---|---|
| v40 | 17f27486-a342-4d10-8842-ab0874e7f670 | ≤ 42 |
| v43 | 0a233719-3960-4578-9d7c-203b8b1d9cc1 | 43 |
| v46 | 8d536ca1-0cca-4956-a837-786963755584 | 46/47 |

d3dcompiler_47 also accepts IID_ID3D12ShaderReflection. vkd3d's acceptance table is `reflection_iid_supported`
(vkd3d-utils/reflection.c:425-440):

| "version" | Accepts |
|---|---|
| < 33 | ID3D10ShaderReflection only |
| < 40 | ID3D10ShaderReflection or ID3D10ShaderReflection1 |
| < 43 | v40 |
| < 46 | v43 |
| 46 | v46 |
| 47 | v46 or ID3D12ShaderReflection |

[test] test_reflection_references (tests/reflection.c:83-164), on Windows:
- On an ID3D11 object, QI for ID3D10ShaderReflection and for ID3D10ShaderReflection1 gives E_NOINTERFACE.
- Requesting ID3D10ShaderReflection(1) from D3DReflect gives E_INVALIDARG on _47 and E_NOINTERFACE on older versions
  (Windows 8's _47 also gives E_NOINTERFACE, marked broken).
- NULL data gives D3DERR_INVALIDCALL on ≥43, E_INVALIDARG on 40-42, E_FAIL for D3D10.
- Size 31 (shorter than the header) gives D3DERR_INVALIDCALL on ≥43.
- Size 32 or size-1 (header present, checksum wrong) gives D3DERR_INVALIDCALL on ≥46, E_INVALIDARG on 42, E_FAIL otherwise.
- vkd3d mirrors these results in `d3d12_reflection_init` (reflection.c:1752-1759).

[test] test_reflection_interfaces (168-226), on _47:
- Asking D3DReflect for IID_ID3D12ShaderReflection succeeds. Older _46/_47 builds are broken (E_INVALIDARG / E_NOINTERFACE).
- QI between ID3D11ShaderReflection and ID3D12ShaderReflection returns the **same pointer**, and IUnknown QI returns the same
  pointer too.
- [test] test_reflection_bound_resources (1316-1330, 1400-1407): a D3D12 object obtained by QI from the D3D11 one is the same
  object. GetResourceBindingDesc through it writes only the D3D11-sized struct, so uID keeps 0xdeadbeef; Space is valid only
  where it overlaps D3D11 padding on x64.
- vkd3d implements this by choosing `bind_desc_size` from the IID used at creation (reflection.c:1772-1775).

[test] test_d3d10_interfaces (228-251): a D3D10ReflectShader object QIs to ID3D10ShaderReflection only.

### 3.2 How Wine/vkd3d builds the reflection object, and what happens when chunks are missing

`d3d12_reflection_init` (vkd3d-utils/reflection.c:1728-1837) works in this order:
1. Parse the container with `vkd3d_shader_parse_dxbc`. This includes checksum verification, so a bad checksum fails here.
2. Run `vkd3d_shader_scan` with signature-info and thread-group-size chains. The scan needs a code chunk (SHDR/SHEX):
   `shader_extract_from_dxbc` fails with INVALID_ARGUMENT when there is no byte code (dxbc.c:618-634). A container without
   SHDR/SHEX therefore cannot be reflected in Wine. Windows behaviour for that case is **NOT SETTLED**; no test covers it.
3. Walk the chunks:
   - RDEF → parse_rdef (first one wins; duplicates FIXME);
   - SHDR/SHEX → Version = first dword (the last code chunk wins);
   - STAT → parse_stat (first one wins).
4. Set InputParameters, OutputParameters and PatchConstantParameters from the element counts of the scanned signatures
   (1818-1820). Those come from the ISGN/ISG1, OSGN/OSG5/OSG1 and PCSG/PSG1 chunks (dxbc.c:532-578). DXIL is skipped.
5. Check the IID.

**Missing RDEF** (the stripped-reflection case):
- ConstantBuffers = 0, BoundResources = 0, Flags = 0, Creator = NULL (the desc is zero-initialised by calloc).
- Every GetConstantBufferBy* call returns the null buffer.
- [test] test_reflection_desc_ps_output (reflection.c:952-1130) reflects six blobs that have only ISGN, OSGN and SHEX/SHDR (no
  RDEF, no STAT). On d3dcompiler ≥43 Windows returns S_OK and correct output parameters. On <43 Windows returns E_INVALIDARG;
  this is `todo_wine` because Wine succeeds.

**Missing STAT**: every STAT-derived field and Get*InstructionCount returns 0, and IsSampleFrequencyShader returns FALSE.

**A STAT of unexpected size** makes the whole reflection fail with E_FAIL (reflection.c:1677-1683).

### 3.3 ID3D11ShaderReflection methods (22 vtable entries: 3 from IUnknown + 19 of its own)

The DXMT measurement counts "22 own methods". The header actually has 22 vtable entries, of which 19 are specific to this interface.
The vtable order follows wine/include/d3d11shader.h:223-250. IUnknown's three methods come first, then these 19:
1. GetDesc
2. GetConstantBufferByIndex
3. GetConstantBufferByName
4. GetResourceBindingDesc
5. GetInputParameterDesc
6. GetOutputParameterDesc
7. GetPatchConstantParameterDesc
8. GetVariableByName
9. GetResourceBindingDescByName
10. GetMovInstructionCount
11. GetMovcInstructionCount
12. GetConversionInstructionCount
13. GetBitwiseInstructionCount
14. GetGSInputPrimitive
15. IsSampleFrequencyShader
16. GetNumInterfaceSlots
17. GetMinFeatureLevel
18. GetThreadGroupSize
19. GetRequiresFlags

With QueryInterface, AddRef and Release that makes 22 entries, which matches the vkd3d vtable at reflection.c:880-906.

[doc] Per-method pages: https://learn.microsoft.com/en-us/windows/win32/api/d3d11shader/nn-d3d11shader-id3d11shaderreflection
Each method page says "This method's interface is hosted in the out-of-box DLL D3DCompiler_xx.dll".

| Method | Data source (chunk.field) | vkd3d behaviour | Windows behaviour per tests |
|---|---|---|---|
| GetDesc(D3D11_SHADER_DESC*) | see 3.4 | NULL → E_FAIL (reflection.c:563-578). Copies the D3D10-sized or D3D11/12-sized desc depending on version (1767-1770) | NULL → E_FAIL (reflection.c:444-445). D3D11 and D3D12 descs identical (489-494) |
| GetConstantBufferByIndex(i) | RDEF cbuffer[i] | out of range → a static null buffer object, never NULL (580-594) | 0xffffffff returns a non-NULL dummy, the same pointer for D3D11 and D3D12 (1804-1811) |
| GetConstantBufferByName(name) | RDEF cbuffer name, exact strcmp | NULL or unknown name → null buffer (596-624) | unknown name or NULL → the dummy (1848-1852) |
| GetResourceBindingDesc(i, desc) | RDEF binding[i] | NULL desc or i out of range → E_INVALIDARG (626-648) | same (1333-1337) |
| GetInputParameterDesc(i, desc) | ISGN/ISG1 element i | NULL or out of range → E_INVALIDARG (650-688) | same (858-862) |
| GetOutputParameterDesc(i, desc) | OSGN/OSG5/OSG1 element i | same (690-698) | same (864-868) |
| GetPatchConstantParameterDesc(i, desc) | PCSG/PSG1 element i; treated as output for HS (rw mask inverted) | same (700-710) | PS with no PCSG → E_INVALIDARG (870-873) |
| GetVariableByName(name) | searches every RDEF cbuffer's variables | NULL or unknown → null variable (712-745) | NULL or "invalid" → dummy; "a" → the same pointer as via the buffer (1854-1863) |
| GetResourceBindingDescByName(name, desc) | RDEF bindings by name | NULL name, NULL desc or not found → E_INVALIDARG (747-782) | same (1339-1351) |
| GetMovInstructionCount | STAT w19 | 784-791 | vs_4_1 → 10, ps_4_1 → 24 (503-504, 850-851) |
| GetMovcInstructionCount | STAT w20 | 793-800 | 0, 0 |
| GetConversionInstructionCount | STAT w21 | 802-809 | vs_4_1 → 2 (500-501) |
| GetBitwiseInstructionCount | STAT w22 | 811-818 | 0 for vs_4_1 (y << x & 0xf, yet 0) and ps_4_1 |
| GetGSInputPrimitive | STAT w23 (same as desc.InputPrimitive) | 820-827 | not directly tested |
| IsSampleFrequencyShader | STAT w28 | 829-836 | not tested |
| GetNumInterfaceSlots | presumably IFCE.ifaceSlotCount or RD11's last dword (NOT SETTLED) | **stub, returns 0** (838-843) | not tested |
| GetMinFeatureLevel(D3D_FEATURE_LEVEL*) | unknown; a likely input is SFI0 plus the target (NOT SETTLED) | **stub, E_NOTIMPL** (845-851) | not tested |
| GetThreadGroupSize(x, y, z) | the `dcl_thread_group` instruction in SHEX/SHDR (found by the vkd3d scan) | any NULL pointer → returns E_INVALIDARG through a UINT return; otherwise writes x, y, z and returns x·y·z (853-871) | cs_5_1 [numthreads(16,8,4)] → 16, 8, 4, 512 on ≥43 (1280-1287); PS → 0, 0, 0, 0 (937-946) |
| GetRequiresFlags | presumably SFI0 (bit 0x2 disagreement, see 1.8) | **stub, returns 0** (873-878) | not tested |

The interface also has QueryInterface, AddRef and Release (447-559).

[doc] GetThreadGroupSize returns the product of the three sizes; the documented maxima are x ≤ 1024, y ≤ 1024, z ≤ 64.
https://learn.microsoft.com/en-us/windows/win32/api/d3d11shader/nf-d3d11shader-id3d11shaderreflection-getthreadgroupsize

[doc] GetMinFeatureLevel takes a D3D_FEATURE_LEVEL* and returns "Direct3D 11 Return Codes".
https://learn.microsoft.com/en-us/windows/win32/api/d3d11shader/nf-d3d11shader-id3d11shaderreflection-getminfeaturelevel

[doc] GetGSInputPrimitive's return is described as a D3D_PRIMITIVE_TOPOLOGY in the doc, but it is declared D3D_PRIMITIVE in the
headers (d3d11shader.h:244). Doc and header disagree; the header is authoritative.
https://learn.microsoft.com/en-us/windows/win32/api/d3d11shader/nf-d3d11shader-id3d11shaderreflection-getgsinputprimitive

GetDesc for geometry, hull, domain and compute shaders. vkd3d takes every GS/HS/DS field (InputPrimitive, GSOutputTopology,
GSMaxOutputVertexCount, cGSInstanceCount, cControlPoints, HSOutputPrimitive, HSPartitioning, TessellatorDomain) **only from
STAT**, never from the SHEX declarations. The thread-group size is the only value that comes from declarations.
- A STAT-less hull shader would therefore report zeros in Wine, even though `dcl_tessellator_domain` and the other
  declarations exist in SHEX. Windows behaviour for that case is **NOT SETTLED**.
- The hs_5_0 test blob shows that fxc's STAT carries the values that the SHEX declarations also encode: domain quad,
  partitioning integer, output triangle_cw, 8 control points.

### 3.4 D3D11_SHADER_DESC: every field and where it comes from (vkd3d), with test evidence

The field list is from wine/include/d3d11shader.h:50-90 and [doc] ns-d3d11shader-d3d11_shader_desc (URL in 1.7).

| Field | Source | Test assertions (Windows) |
|---|---|---|
| Version | SHDR/SHEX dword 0 (reflection.c:1802) | 0x10041 vs_4_1, 0x41 ps_4_1, 0x50 ps_5_0 |
| Creator | RDEF creator string (1657-1660). NULL without RDEF | equals "Microsoft (R) HLSL Shader Compiler 9.29.952.3111" (the d3dcompiler_43-era fxc) for the test blobs (reflection.c:49-53, 451) |
| Flags | RDEF header +0x14 compile flags (1657) | 0x100 (D3DCOMPILE_NO_PRESHADER) for default fxc builds (452, 806, 1761). The hs /Zi blob's RDEF has 0x101 (DEBUG \| NO_PRESHADER). vkd3d's compiler writes 0 |
| ConstantBuffers | RDEF cbuffer count (1580) | 0 for vs_4_1; 2 for ps_5_0 / ps_4_0; 4 for vs_5_0 in hlsl_d3d11 |
| BoundResources | RDEF binding count (1605) | 4 for ps_4_1. ps_5_0 → 1, but its ps_4_0 counterpart under D3D10 → 2 (1763-1764) |
| InputParameters | element count of the input signature chunk (1818) | 15 vs_4_1, 8 ps_4_1; a cs_4_0 with only an sv_dispatchthreadid input → 0 (hlsl_d3d11.c:1120) |
| OutputParameters | output signature element count (1819) | 11, 4 |
| InstructionCount … TextureGradientInstructions, Float/Int/Uint, Static/Dynamic flow, Macro, Array, Cut, Emit | STAT words 0-18 per the table in 1.7 (1689-1707) | see 1.7 |
| GSOutputTopology | STAT w24 | 0 in non-GS tests |
| GSMaxOutputVertexCount | STAT w25 | 0 |
| InputPrimitive | STAT w23 | 0 for VS/PS. HS blob raw = 15 |
| PatchConstantParameters | PCSG/PSG1 element count (1820) | 0 |
| cGSInstanceCount | STAT w29 (5.x only) | 0 |
| cControlPoints | STAT w30 | 0 (HS raw 8) |
| HSOutputPrimitive | STAT w31 | 0 |
| HSPartitioning | STAT w32 | 0 |
| TessellatorDomain | STAT w33 | 0 |
| cBarrierInstructions | STAT w34 | 0 |
| cInterlockedInstructions | STAT w35 | 0 |
| cTextureStoreInstructions | STAT w36 | 0 |

Evidence that d3dcompiler versions compile differently: the ps_5_0 blob (for D3DReflect) and the ps_4_0 blob (for
D3D10ReflectShader) differ in InstructionCount (8 vs 7), TempRegisterCount (2 vs 1) and UintInstructionCount (1 vs 0)
(reflection.c:1767-1783). These are differences between the two blobs, not between reflection implementations.

D3D12_SHADER_DESC extends the D3D11 struct, and the tests require the two to compare equal (memcmp) (reflection.c:489-494).

### 3.5 D3D11_SIGNATURE_PARAMETER_DESC

Fields (d3d11shader.h:138-151):

| Field | Source |
|---|---|
| SemanticName | element name string |
| SemanticIndex | element +4 (+8 with a stream prefix) |
| Register | element register |
| SystemValueType | element sysval, adjusted for PS outputs (see 1.4) |
| ComponentType | element component type |
| Mask | low mask byte |
| ReadWriteMask | second mask byte, raw (see 1.4) |
| Stream | stream prefix (OSG5/…1); 0 otherwise |
| MinPrecision | the "1" variants' suffix; 0 otherwise. The field exists only when D3D_COMPILER_VERSION ≥ 46 |

[src] get_signature_parameter (reflection.c:650-678) fills Stream only for version ≥ 40 and MinPrecision only for ≥ 46.

[test] test_reflection_desc_vs (reflection.c:525-534): on _46/_47 the MinPrecision field is written as 0; on earlier versions it
is left untouched (~0u stays). This proves d3dcompiler_43 uses the shorter struct.

[test] Full expected tables (Windows):
- vs_4_1 inputs and outputs: reflection.c:374-406.
- ps_4_1: 761-779.
- Notable values:
  - VS input SV_InstanceID → D3D_NAME_INSTANCE_ID, but VS *output* SV_InstanceID → UNDEFINED (raw sysval 0);
  - PS input SV_InstanceID → UNDEFINED;
  - PS input "VFACE" → UNDEFINED;
  - SV_Coverage and SV_Depth outputs have Register 0xFFFFFFFF;
  - SV_Target with index 1 has register 1;
  - several elements share register 2 with different masks (packing).

[test] hlsl_d3d11.c test_semantic_reflection (947-1165) (compiled at test time on Windows):
- lower-case "sv_position" as a **VS input** is reported as D3D_NAME_UNDEFINED, and as an output as POSITION;
- "banana2" splits into the name "banana" with index 2;
- with D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY, a VS output "position" is renamed "SV_Position" with POSITION;
- ps "sv_target2" is reported as name "sv_target", index 2, register 2, TARGET;
- "sv_depth" has register ~0u and mask 1 / 0xE.

### 3.6 ID3D11ShaderReflectionConstantBuffer (3 methods), ID3D11ShaderReflectionVariable (4), ID3D11ShaderReflectionType (11)

The header declares them **without** IUnknown, i.e. not COM, no refcount (wine/include/d3d11shader.h:171-208).

[doc] The pages say "inherits from the IUnknown interface", but also "This isn't a COM interface, so you don't need to worry
about reference counts or releasing the interface". The header has no IUnknown methods.
https://learn.microsoft.com/en-us/windows/win32/api/d3d11shader/nn-d3d11shader-id3d11shaderreflectionconstantbuffer
(likewise …variable and …type).

**ConstantBuffer** (vkd3d reflection.c:347-421):

| Method | Behaviour |
|---|---|
| GetDesc(D3D11_SHADER_BUFFER_DESC*) | null buffer or NULL desc → E_FAIL. [test] 1887-1894 |
| GetVariableByIndex(i) | out of range → the null variable |
| GetVariableByName(name) | NULL or unknown → the null variable. [test] 1866-1879 |

**Variable** (274-338):

| Method | Behaviour |
|---|---|
| GetDesc(D3D11_SHADER_VARIABLE_DESC*) | null variable or NULL desc → E_FAIL. [test] 1897-1904. Copies a D3D10-sized desc for version < 41 (1431-1434) |
| GetType() | null variable → the null type |
| GetBuffer() | null variable → the null buffer |
| GetInterfaceSlot(UINT arrayIndex) | **stub, returns 0** (324-330). [doc] it returns the slot index for an interface-pointer variable; https://learn.microsoft.com/en-us/windows/win32/api/d3d11shader/nf-d3d11shader-id3d11shaderreflectionvariable-getinterfaceslot |

**Type** (95-265):

| Method | Behaviour |
|---|---|
| GetDesc(D3D11_SHADER_TYPE_DESC*) | null type or NULL → E_FAIL. D3D10-sized for version < 40 (1357-1360) |
| GetMemberTypeByIndex(i) | out of range → the null type. [test] 1922-1926 |
| GetMemberTypeByName(name) | NULL or unknown → the null type. [test] 1928-1938 |
| GetMemberTypeName(i) | the null type returns the string "$Invalid"; a valid type with index out of range returns NULL. [test] 1916-1920 |
| IsEqual(other) | the null type as `this` → E_FAIL. Otherwise S_OK/S_FALSE by RDEF offset identity. [test] 1940-1952 |
| GetSubType | **stub, NULL** |
| GetBaseClass | **stub, NULL** |
| GetNumInterfaces | **stub, 0** |
| GetInterfaceByIndex | **stub, NULL** |
| IsOfType | **stub, E_NOTIMPL** |
| ImplementsInterface | **stub, E_NOTIMPL** |

Dummy objects (Windows behaviour per the tests):
- out-of-range lookups never return NULL;
- they return per-kind singleton dummies whose GetDesc fails with E_FAIL;
- the dummy CB from a D3D12 reflection is the same pointer as the D3D11 one (1804-1817);
- vkd3d uses static `null_buffer`, `null_variable` and `null_type` objects (reflection.c:86-88, 267, 340, 423).

Valid D3D12 versus D3D11 buffer objects from *separate* D3DReflect calls are different pointers (1827-1829).

### 3.7 D3D11_SHADER_INPUT_BIND_DESC, D3D11_SHADER_BUFFER_DESC, D3D11_SHADER_VARIABLE_DESC, D3D11_SHADER_TYPE_DESC

Field ↔ RDEF mappings are given in 1.6.2-1.6.5. Field lists: wine/include/d3d11shader.h:92-136.
- D3D12_SHADER_INPUT_BIND_DESC adds Space and uID.
- [doc] D3D11_SHADER_TYPE_DESC: Rows/Columns "Number of rows in a matrix. Otherwise a numeric type returns 1, any other type
  returns 0"; Elements is the array count or 0; Members is the struct member count or 0; Offset is "between the start of the
  parent structure and this variable"; Name may be NULL and is used for dynamic linkage interface types.
  https://learn.microsoft.com/en-us/windows/win32/api/d3d11shader/ns-d3d11shader-d3d11_shader_type_desc
- [doc] D3D11_SHADER_BUFFER_DESC: Name, Type (D3D_CBUFFER_TYPE), Variables ("number of unique variables"), Size (bytes),
  uFlags (D3D_SHADER_CBUFFER_FLAGS).
  https://learn.microsoft.com/en-us/windows/win32/api/d3d11shader/ns-d3d11shader-d3d11_shader_buffer_desc
- [doc] enums:
  - D3D_CBUFFER_TYPE 0..3: https://learn.microsoft.com/en-us/windows/win32/api/d3dcommon/ne-d3dcommon-d3d_cbuffer_type
  - D3D_SHADER_VARIABLE_FLAGS 1/2/4/8: https://learn.microsoft.com/en-us/windows/win32/api/d3dcommon/ne-d3dcommon-d3d_shader_variable_flags
  - D3D_SHADER_CBUFFER_FLAGS USERPACKED = 1: https://learn.microsoft.com/en-us/windows/win32/api/d3dcommon/ne-d3dcommon-d3d_shader_cbuffer_flags
  - D3D_SHADER_INPUT_TYPE 0..11: https://learn.microsoft.com/en-us/windows/win32/api/d3dcommon/ne-d3dcommon-d3d_shader_input_type
    Values 12 (RTACCELERATIONSTRUCTURE) and 13 (UAV_FEEDBACKTEXTURE) appear in wine/include/d3dcommon.idl:692-720 but not
    on that doc page.
  - D3D_MIN_PRECISION: https://learn.microsoft.com/en-us/windows/win32/api/d3dcommon/ne-d3dcommon-d3d_min_precision
  - D3D_NAME: https://learn.microsoft.com/en-us/windows/win32/api/d3dcommon/ne-d3dcommon-d3d_name
  - D3D_REGISTER_COMPONENT_TYPE: https://learn.microsoft.com/en-us/windows/win32/api/d3dcommon/ne-d3dcommon-d3d_register_component_type
- Numeric values used above come from wine/include/d3dcommon.idl:
  - SVC: lines 132-151;
  - SVT: 166-279 (VOID = 0 … UINT = 19 … INTERFACE_POINTER = 37, DOUBLE = 39, MIN8FLOAT = 52 … MIN16UINT = 57);
  - SIF: 281-295;
  - D3D_PRIMITIVE: 297-381 (UNDEFINED 0, POINT 1, LINE 2, TRIANGLE 3, LINE_ADJ 6, TRIANGLE_ADJ 7, N_CONTROL_POINT_PATCH = 7+N);
  - tessellator enums: 481-519 (domain: 0 undefined, 1 isoline, 2 tri, 3 quad; partitioning: 1 integer, 2 pow2, 3 fractional
    odd, 4 fractional even; output: 1 point, 2 line, 3 triangle_cw, 4 triangle_ccw);
  - CBUFFER_TYPE: 533-545;
  - SRV_DIMENSION: 547-594;
  - REGISTER_COMPONENT_TYPE: 596-606;
  - RESOURCE_RETURN_TYPE: 608-632;
  - D3D_NAME: 634-690.

### 3.8 d3dcompiler version differences (43 vs 47 and others) visible in reflection

| Topic | Versions | Source |
|---|---|---|
| ID3D11ShaderReflection IID | v40 / v43 / v46; _47 also accepts ID3D12ShaderReflection | 3.1 |
| SIGNATURE_PARAMETER_DESC.MinPrecision | exists and is written only on ≥ 46 | test 525-534 |
| Error codes for bad or NULL input | the version table in 3.1 | test_reflection_references |
| D3DGetBlobPart / D3DStripShader with only a header (8 dwords) | E_FAIL on < 46, D3DERR_INVALIDCALL on ≥ 46 (`todo_wine`) | blob.c:124-128, 157-162, 377-381 |
| Reflecting stripped shaders (no RDEF/STAT) | fails on < 43 (E_INVALIDARG), works on ≥ 43 | reflection.c:1087-1093 |
| SM5.1 non-compute shaders | need _47 (and not every _47 build) | reflection.c:1307-1311; vkd3d reflection.c:1516-1523 |
| GetThreadGroupSize | asserted only on ≥ 43 | reflection.c:937-946, 1281-1287 |
| PS-output SystemValueType | TARGET/DEPTH/… with any d3dcompiler; UNDEFINED with D3D10ReflectShader | reflection.c:917-921, 1107-1113 |
| GetResourceBindingDesc uFlags under D3D10 | 0 | reflection.c:1365-1371, with todo_wine where the expected flags are non-zero |

### 3.9 D3DGetBlobPart and D3D_BLOB_PART

[doc] D3DGetBlobPart(pSrcData, SrcDataSize, Part, Flags, ppPart): "Retrieves a specific part from a compilation result".
Flags: "Currently, no flags are defined." https://learn.microsoft.com/en-us/windows/win32/api/d3dcompiler/nf-d3dcompiler-d3dgetblobpart

[doc] D3D_BLOB_PART values: https://learn.microsoft.com/en-us/windows/win32/api/d3dcompiler/ne-d3dcompiler-d3d_blob_part
(numeric values also in wine/include/d3dcompiler.h:111-130). The chunk mapping below is from Wine (blob.c:165-215) and vkd3d-utils
(vkd3d_utils_main.c:830-889):

| Value | Name | Doc note | Wine chunk mapping | Output form |
|---|---|---|---|---|
| 0 | INPUT_SIGNATURE_BLOB | | ISGN (exactly 1 required) | a new DXBC container holding that chunk |
| 1 | OUTPUT_SIGNATURE_BLOB | | OSGN or OSG5 (exactly 1) | container |
| 2 | INPUT_AND_OUTPUT_SIGNATURE_BLOB | | ISGN + OSGN/OSG5 (exactly 2) | container |
| 3 | PATCH_CONSTANT_SIGNATURE_BLOB | | PCSG (exactly 1) | container |
| 4 | ALL_SIGNATURE_BLOB | | ISGN + OSGN/OSG5 + PCSG (exactly 3) | container |
| 5 | DEBUG_INFO | | SDBG | **raw chunk data**, no DXBC header |
| 6 | LEGACY_SHADER | | Aon9 | raw data |
| 7 | XNA_PREPASS_SHADER | | XNAP | raw data |
| 8 | XNA_SHADER | | XNAS | raw data |
| 9 | PDB | d3dcompiler_44+ | **not mapped** (FIXME → E_FAIL). Chunk presumably SPDB (NOT SETTLED) | |
| 10 | PRIVATE_DATA | 44+ | not mapped. Presumably PRIV | |
| 11 | ROOT_SIGNATURE | 47+ | not mapped. Presumably RTS0 | |
| 12 | DEBUG_NAME | 47 (Win10 Fall Creators Update)+; an auto-generated name matching the PDB file if none was given | not mapped. DXIL uses ILDN; the DXBC tag is NOT SETTLED | |
| 0x8000 | TEST_ALTERNATE_SHADER | "only produced by special compiler versions" | not mapped → E_FAIL | |
| 0x8001 | TEST_COMPILE_DETAILS | same | not mapped | |
| 0x8002 | TEST_COMPILE_PERF | same | not mapped | |
| 0x8003 | TEST_COMPILE_REPORT | same; 44+ | not mapped | |

Wine argument checks (blob.c:227-238), in order:
- data NULL, size < 32, flags ≠ 0, or blob NULL → D3DERR_INVALIDCALL;
- part > TEST_COMPILE_REPORT, or DEBUG_NAME < part < TEST_ALTERNATE_SHADER → D3DERR_INVALIDCALL;
- container parse failure (including a bad checksum) → **E_FAIL**;
- wrong number of matching chunks → E_FAIL.

ISG1, OSG1 and PSG1 are **not** recognised by Wine's mapping. A min-precision shader's signatures would therefore return E_FAIL
in Wine. Windows behaviour for that case is NOT SETTLED (untested).

[test] Windows behaviour, test_get_blob_part (blob.c:115-434), vs_4_0_level_9_0 blob with XNAS, XNAP, Aon9, SHDR, STAT, RDEF,
ISGN, OSGN:
- Invalid calls: NULL data, size 0, size 7 dwords, NULL output, flags = 1, or part 0xffffffff → D3DERR_INVALIDCALL
  (135-176). Size 8 dwords → E_FAIL (<46) or D3DERR_INVALIDCALL (≥46).
- INPUT_SIGNATURE → S_OK, a 124-byte container, with ISGN at dword 9 (32-byte header + 1 offset). Querying that result again:
  only INPUT_SIGNATURE succeeds; every other part in the test list (0-8 and 0x8000-0x8002) gives E_FAIL (181-210).
- OUTPUT → 88 bytes, OSGN at dword 9. Querying the result: only OUTPUT succeeds (212-241).
- INPUT_AND_OUTPUT → 180 bytes, ISGN at dword 10, OSGN at dword 32. Querying the result: INPUT, OUTPUT and INPUT_AND_OUTPUT
  succeed; others E_FAIL (243-275).
- PATCH_CONSTANT, ALL_SIGNATURE and DEBUG_INFO → E_FAIL for this VS (277-287). ALL needs all three signatures.
- LEGACY_SHADER → S_OK, 92 bytes, *not* starting with DXBC. Feeding that back: every part → E_FAIL (<46) / D3DERR_INVALIDCALL
  (≥46), `todo_wine` on ≥46 (289-310).
- XNA_PREPASS_SHADER → 68 bytes raw; XNA_SHADER → 68 bytes raw; same re-query behaviour (312-356).

[test] test_get_blob_part2 (blob.c:622-780), hs_5_0 /Zi blob with RDEF, ISGN, OSGN, PCSG, SHEX, STAT, SDBG:
- PATCH_CONSTANT → 232-byte container with PCSG at dword 9. Querying the result: only PATCH_CONSTANT succeeds.
- ALL_SIGNATURE → 344 bytes with ISGN (dword 11), OSGN (24), PCSG (37). Querying the result: ALL, PATCH_CONSTANT, INPUT_AND_OUTPUT,
  INPUT and OUTPUT all succeed.
- DEBUG_INFO → S_OK, 4055 bytes, not DXBC.
- LEGACY, XNAP and XNAS → E_FAIL.

Chunk order in the output: the extracted container keeps the source order of the matching chunks (Wine iterates in source order,
blob.c:253-259). This matches the tests: ISGN before OSGN, PCSG last.

[doc] D3DGetInputSignatureBlob, D3DGetOutputSignatureBlob and D3DGetInputAndOutputSignatureBlob "may be altered or unavailable
for releases after Windows 8.1. Instead use D3DGetBlobPart with the D3D_BLOB_INPUT_SIGNATURE_BLOB (…OUTPUT…,
…INPUT_AND_OUTPUT…) value".
- https://learn.microsoft.com/en-us/windows/win32/api/d3dcompiler/nf-d3dcompiler-d3dgetinputsignatureblob
- https://learn.microsoft.com/en-us/windows/win32/api/d3dcompiler/nf-d3dcompiler-d3dgetinputandoutputsignatureblob

D3DGetDebugInfo "Gets shader debug information":
https://learn.microsoft.com/en-us/windows/win32/api/d3dcompiler/nf-d3dcompiler-d3dgetdebuginfo

[src] Wine implements all four as D3DGetBlobPart with the matching part and flags 0 (blob.c:421-447). vkd3d-utils does the same
(vkd3d_utils_main.c:1007-1033).

### 3.10 D3DStripShader and the strip flags

[doc] D3DStripShader(pShaderBytecode, BytecodeLength, uStripFlags, ppStrippedBlob) "Removes unwanted blobs from a compilation
result". https://learn.microsoft.com/en-us/windows/win32/api/d3dcompiler/nf-d3dcompiler-d3dstripshader

[doc] D3DCOMPILER_STRIP_FLAGS (https://learn.microsoft.com/en-us/windows/win32/api/d3dcompiler/ne-d3dcompiler-d3dcompiler_strip_flags):

| Flag | Value | Doc meaning | Wine removes | NOT SETTLED |
|---|---|---|---|---|
| REFLECTION_DATA | 0x1 | "Remove reflection data" | RDEF and STAT (blob.c:331-334) | whether Windows also removes IFCE or SFI0 |
| DEBUG_INFO | 0x2 | "Remove debug information" | SDBG (336-338) | whether Windows also removes SPDB |
| TEST_BLOBS | 0x4 | "Remove test blob data" | FIXME, ignored | |
| PRIVATE_DATA | 0x8 | (44+) "Remove private data" | ignored | presumably PRIV |
| ROOT_SIGNATURE | 0x10 | (47+) "Remove the root signature" | ignored | presumably RTS0 |

The output is always a newly serialised container: the remaining chunks in their original order, with a recomputed checksum
(Wine blob.c:349-411).

Wine argument checks:
- blob NULL → **E_FAIL** (checked first);
- data NULL or size < 32 → D3DERR_INVALIDCALL;
- parse failure → E_FAIL.

[test] test_get_blob_part, the strip part (blob.c:358-433):
- flags 0xffffffff → S_OK; flags 0 → S_OK.
- NULL data → D3DERR_INVALIDCALL; 7 dwords → D3DERR_INVALIDCALL; 8 dwords → E_FAIL (<46) / D3DERR_INVALIDCALL (≥46).
- NULL output pointer → E_FAIL, even when the data is also NULL or the size is 0. So the output pointer is checked first on
  Windows too.
- DEBUG_INFO on the level9 VS (which has no SDBG) → 736 bytes, the same size as the input. Chunk positions:
  XNAS@16, XNAP@35, Aon9@54, SHDR@79, STAT@96, RDEF@127, ISGN@149, OSGN@171 (dword indices).
- REFLECTION_DATA → 516 bytes, with no STAT and no RDEF: XNAS@14, XNAP@33, Aon9@52, SHDR@77, ISGN@94, OSGN@116. The arithmetic
  checks: 736 − (0x74+8) − (0x50+8) − 2·4 = 516.

[test] test_get_blob_part2 (blob.c:739-778), hs_5_0 /Zi:
- DEBUG_INFO → 952 bytes, chunks RDEF, ISGN, OSGN, PCSG, SHEX, STAT (SDBG gone). D3DGetBlobPart(DEBUG_INFO) on that result →
  E_FAIL.
- REFLECTION_DATA → 4735 bytes, chunks ISGN, OSGN, PCSG, SHEX, SDBG.

vkd3d-utils' copies of D3DGetBlobPart and D3DStripShader differ from Wine's:
- size == 0, rather than size < 32, triggers INVALIDCALL;
- a parse failure gives D3DERR_INVALIDCALL, where Wine gives E_FAIL. That is closer to the ≥46 Windows results;
- D3DGetBlobPart's valid range stops at XNA_SHADER and TEST_COMPILE_PERF, so PDB, PRIVATE_DATA, ROOT_SIGNATURE, DEBUG_NAME and
  TEST_COMPILE_REPORT give INVALIDCALL;
- D3DStripShader FIXMEs about any flag other than REFLECTION_DATA or DEBUG_INFO.

Sources: vkd3d_utils_main.c:902-920 and 1073-1099.

## Part 4. Conversion routes to Metal

### 1. The Metal side: what D3D11 features map to, from Apple's documents

| D3D11 feature | Metal fact | Source |
|---|---|---|
| Geometry shaders | Metal has no geometry stage. The only programmable pre-raster stages are vertex, post-tessellation vertex, object and mesh. Emulation must use mesh shaders (Apple7+) or compute plus indirect draw. | documented: FST p.4 "Mesh shading ... Apple7"; MSL spec has no geometry function type (searched `msl.txt` for "geometry" function qualifiers: only ray-tracing "geometry" hits) |
| Tessellation | Fixed-function tessellator fed by tessellation factors written to a buffer (by a compute kernel in practice), then a post-tessellation vertex function. Tessellation is available from Apple3 and only in the **Metal 3** programming model column, not Metal 4. | documented: FST p.3 "Tessellation ... Metal 3 ... Apple3"; "Indirect tessellation arguments ... Apple5" |
| Max tessellation factor | "Not available" on Apple2, 16 on Apple3/Apple4, **64 on Apple5 and later** (all Apple-silicon Macs are Apple7+). D3D11 max is 64. | documented: FST p.7 row "Maximum tessellation factor" |
| Partitioning modes | Metal's `MTLTessellationPartitionMode` has pow2, integer, fractionalOdd, fractionalEven (the four D3D11 modes). Isoline domain and point/line output topologies have no Metal counterpart. | source (as evidence of the Metal enum values): `hlslcc/src/toMetal.cpp:798-812` maps all four D3D modes to Metal constants 0-3; `:826-831` reports point/line output "not supported"; `spirv-cross/spirv_msl.cpp:14692-14693,14706-14707` "Metal does not support isoline tessellation". Apple API page itself not fetched: **not settled** from Apple text |
| Stream output | No transform feedback in Metal. Must be emulated (vertex function writing to a device buffer, or compute). | documented by absence: FST pp.3-6 lists no such feature; MSL spec has no stream-output attribute (searched) |
| Doubles (fp64) | "Metal does not support the double, long long, unsigned long long, and long double data types." | documented: MSL §2.1 Scalar Data Types, p.24-25 |
| 64-bit integer | "64-bit integer math" Apple3+. 64-bit atomics Apple9+ (min/max on Apple8 macOS). Not needed by SM5. | documented: FST p.4, p.5, footnote 7 p.6 |
| Integer division by zero | "does not cause an exception but results in an unspecified value". D3D defines udiv by 0 as 0xFFFFFFFF, so a converter must insert a select. | documented: MSL §3.1, p.111 |
| min16float / min-precision | D3D allows executing min-precision at 32 bits; Metal has `half`. Converters may keep 32-bit. | dxbc-spirv README:13-15 (lowered to 16 or 32 bit by option); airconv ignores min precision (`dxmt/src/airconv/dxbc_signature.cpp:88,658,1082` comments "not used") |
| Dual-source blending | `[[color(m), index(i)]]` fragment output, Metal 1.2+; "Dual-source blending ... Apple2". | documented: MSL §5.2.3.5 table, p.150; FST p.3 |
| SV_Depth / SV_DepthGreaterEqual / LessEqual | `[[depth(any|greater|less)]]`. | documented: MSL §5.2.3.5, p.150-151 |
| SV_Coverage output / input | `[[sample_mask]]` (output and input). | documented: MSL §5.2.3.5 p.150 (output); §5.2.3.4 p.147-148 (input, may be declared once) |
| SV_StencilRef | `[[stencil]]` output, Metal 2.1+; incompatible with `[[early_fragment_tests]]`; "Stencil feedback ... Apple5". | documented: MSL §5.2.3.5 p.150-151; FST p.4 |
| SV_SampleIndex / sample-rate shading | `[[sample_id]]` input or sample-qualified inputs make the fragment function run per sample. | documented: MSL §5.2.3.4 p.147; §5.5 p.175 |
| SV_ClipDistance | `[[clip_distance]]` float or float[n] vertex output. | documented: MSL §5.2.3.3 table 5.4, p.142 |
| SV_CullDistance | No cull-distance attribute in MSL (searched "cull": only mesh `primitive_culled` and ray-tracing cull modes). Must be emulated (clip distance trick, or mesh `primitive_culled`). | documented by absence: MSL §5.2.3.3; mesh `[[primitive_culled]]` MSL p.76 area (`msl.txt:3430`) |
| SV_RenderTargetArrayIndex / SV_ViewportArrayIndex from VS | `[[render_target_array_index]]` (macOS Metal 1.1+) and `[[viewport_array_index]]` (macOS Metal 2+) are vertex-function outputs. "Layered rendering" Apple5, "Multiple viewports" Apple5, max 16 viewports Apple4+. | documented: MSL §5.2.3.3 p.143-144; FST p.3, p.4, p.7 |
| UAVs in pixel shaders, ordering | Read/write buffers in functions Apple3, read/write textures Apple4; raster order groups Apple4 (up to 8). SM5.0 has no ROVs, so plain device memory suffices. | documented: FST p.4, p.7; MSL §5.2.1.2 p.136 |
| Typed UAV loads | Texture `read_write` access: R32Uint/R32Sint/R32Float "All" (all tiers); other formats need "Tier 2" (`MTLDevice.readWriteTextureSupport`). D3D11 FL11_0 only guarantees R32 typed UAV loads, so tier 1 already covers the guaranteed set. | documented: FST p.16 table + footnote 1 |
| Viewport / NDC | NDC lower-left (-1,-1), upper (1,1), so +y up; visible z 0..1; viewport coordinates origin top-left, y down. Same as D3D10+: no y-flip needed in the shader and no depth remap. | documented: MSL §1.7 Metal Coordinate Systems, p.22-23 |
| Half-pixel offset | D3D9 had it; D3D10+ and Metal both put pixel centres at .5. **not settled** from Microsoft text (learn.microsoft.com was unreachable from the research environment); MSL §5.5 p.175 refers to "pixel center" interpolation. |
| Winding | API state `MTLWinding` (clockwise / counterClockwise) mapped from `FrontCounterClockwise`. | source: `dxmt/src/d3d11/d3d11_state_object.cpp:459`; MoltenVK `MVKPipeline.mm:830` |
| Depth clip | D3D11 `DepthClipEnable=FALSE` -> `MTLDepthClipModeClamp`. DXMT's comment points to `https://github.com/gpuweb/gpuweb/issues/2100` for the semantic difference (clamp vs no-clip). | source: `dxmt/src/d3d11/d3d11_state_object.cpp:452-453`; MoltenVK `Commands/MVKCommandEncoderState.mm:1207` |
| D24_UNORM_S8_UINT | Not present for Apple GPU families: the depth/stencil table lists Depth16Unorm, Depth32Float, Stencil8, Depth32Float_Stencil8, X32_Stencil8 only. Emulate with Depth32Float_Stencil8 (precision differs; depth bias units differ). | documented: FST p.15 depth/stencil table; source: `dxmt/src/dxmt/dxmt_format.cpp:706-735` |
| B5G6R5 | B5G6R5Unorm listed with Color/MSAA/Resolve/Blend from Apple2. | documented: FST p.12 |
| R11G11B10 | RG11B10Float "All" capabilities on all families, including write; texture-buffer read/write. | documented: FST p.13 (`fst.txt:669`), FST p.16 |
| 3-component 32-bit (R32G32B32_*) | No RGB32 pixel format in any table (only vertex attribute formats like Float3). DXMT maps `R32G32B32_FLOAT` to `R32Float` with 12 bytes per texel (buffer use) and `AttributeFormatFloat3`. | documented by absence: FST pp.11-15; source: `dxmt/src/dxmt/dxmt_format.cpp:488-492` |
| BC1-7 | "BC pixel formats ... Apple9" for all GPUs; some Apple7/8 support it (check `MTLDevice.supportsBCTextureCompression`). M1/M2 Macs (Apple7/8): the DXMT measurement ran the check on an M3 Pro only. Whether every Apple7/8 Mac reports true: **not settled** (FST footnote 2 speaks only of iPadOS). | documented: FST p.3 + footnote 2 p.6 |
| Argument limits | 31 buffer, 128 texture (Apple6+), 16 sampler entries per function; D3D11 has 14 CB + 128 SRV + 8/64 UAV + 16 samplers per stage, so a converter must use argument buffers (Tier 2 from Apple6). | documented: FST p.7, p.3 |

---

### 2. Route 1: DXBC -> SPIR-V -> SPIRV-Cross -> MSL source -> Apple `metal` compiler

#### 2.1 DXBC -> SPIR-V front ends

**vkd3d-shader (Wine's copy, vkd3d 2.1)**
- Licence: LGPL 2.1 or later (`wine/libs/vkd3d/COPYING:1-16`).
- API: `vkd3d_shader_compile()` with source `VKD3D_SHADER_SOURCE_DXBC_TPF` and target `VKD3D_SHADER_TARGET_SPIRV_BINARY`;
  the header lists the supported transformations at `wine/libs/vkd3d/include/vkd3d_shader.h:3025-3049` (DXBC_TPF ->
  SPIRV_BINARY, SPIRV_TEXT, D3D_ASM; MSL and GLSL are *not* in that list).
- Options: SPIR-V environment `OPENGL_4_5`, `VULKAN_1_0` (default), `VULKAN_1_1` (`vkd3d_shader.h:1722-1726`); opt-in
  SPIR-V extensions demote-to-helper, descriptor indexing, stencil export, viewport-index-layer, fragment-shader
  interlock, float controls (`:1733-1744`); compile options include typed-UAV read format unknown (`:174`), fragment
  coordinate origin upper/lower left (`:268-271`, `:374`), feature flags INT64/FLOAT64/WAVE_OPS (`:282-299`, `:384`),
  `WRITE_TESS_GEOM_POINT_SIZE` (`:347`), denormal modes (`:421-435`).
- What the SPIR-V needs (capabilities emitted, `wine/libs/vkd3d/libs/vkd3d-shader/spirv.c`): `Geometry` (`:1090`),
  `Tessellation` (`:1087`), `TransformFeedback` + `ExecutionModeXfb` for stream output (`:6083-6084`, Xfb decorations
  `:5463-5464`), `Float64` (`:6164`), `SampleRateShading` (`:5053`), `ClipDistance`/`CullDistance` (`:5056-5059`),
  `MultiViewport` (`:5033`), `ShaderViewportIndexLayerEXT` (`:2592`), `StencilExportEXT` (`:2590`),
  `DemoteToHelperInvocationEXT` (`:2576`), fragment-shader interlock (`:2587-2588`), `StorageImageReadWithoutFormat`
  (`:6643`). Execution models for hull (`:6061`) and geometry (`:6067`).
- Integer semantics: the IR lowers `udiv` to a guarded divide because "division by zero is well-defined" in TPF and
  returns UINT_MAX (`wine/libs/vkd3d/libs/vkd3d-shader/ir.c:2303` `vsir_program_lower_udiv`, comment `:2377`).
- Maturity: used by Wine's wined3d Vulkan renderer and vkd3d (D3D12). Released versions: vkd3d 2.0 in May 2026
  (write-up: `https://phoronix.com/news/VKD3D-2.0-Released`); Wine bundles 2.1 (`config.h:3`). No ANNOUNCE/NEWS file in
  Wine's copy (`ls wine/libs/vkd3d`: AUTHORS, COPYING, Makefile.in, config.h, include, libs).

**DXVK's front end**
- DXVK 2.7.1 (last release with `src/dxbc`): licence zlib/libpng (`dxvk-2.7.1/LICENSE:1-6`).
  - Options (`dxvk-2.7.1/src/dxbc/dxbc_options.h`): depth-clip workaround (clamp oDepth when depth clip unsupported),
    `supportsTypedUavLoadR32`, raw access chains, invariant position, volatile TGSM, compute UAV barriers, disable MSAA,
    force sample-rate shading, sample interlock, point-size export, sincos emulation, 16-bit push data, float-control
    flags, min SSBO alignment.
  - Capabilities emitted (`dxvk-2.7.1/src/dxbc/dxbc_compiler.cpp`): `Geometry` (`:6542`), `GeometryStreams` and
    `TransformFeedback` (`:7091-7092`), `Tessellation` (`:7036`), `Float64` (`:7753`), `Int64` (`:8389`), `StencilExportEXT`
    + `SPV_EXT_shader_stencil_export` (`:563-564`), `DemoteToHelperInvocation` (`:4709`), `SampleRateShading` (`:722`),
    `ClipDistance`/`CullDistance` (`:7004-7005`), `ShaderLayer`/`ShaderViewportIndex` (`:6646`, `:6669`),
    `MultiViewport` (`:6598`), interlock (`:7253-7260`), `FragmentFullyCoveredEXT` (`:646-647`), `VulkanMemoryModel`
    (`:35`), `PhysicalStorageBufferAddresses` (`:37`), `StorageImageReadWithoutFormat` (`:898`), `SparseResidency` (`:3514`).
  - udiv by zero yields 0xFFFFFFFF via `OpSelect` (`dxbc_compiler.cpp:2145-2165`).
  - Required Vulkan features for D3D11 (profile `dxvk-2.7.1/VP_DXVK_requirements.json`): Vulkan 1.3 features incl.
    `shaderDemoteToHelperInvocation`, `dynamicRendering`, `robustImageAccess` (`:42-62`); block `d3d11_baseline`
    (`:220-300`) requires `geometryShader`, `depthClamp`, `depthBiasClamp`, `dualSrcBlend`, `multiViewport`,
    `sampleRateShading`, `shaderClipDistance`, `shaderCullDistance`, `textureCompressionBC`, `occlusionQueryPrecise`,
    `independentBlend`, `nullDescriptor`/`robustBufferAccess2`, `transformFeedback` (`VK_EXT_transform_feedback`) and
    `depthClipEnable`. Feature level 11_0 additionally needs `drawIndirectFirstInstance`, `fragmentStoresAndAtomics`,
    `multiDrawIndirect`, `tessellationShader` (`dxvk-2.7.1/src/d3d11/d3d11_features.cpp:375-381`); 11_1 needs logic op and
    `vertexPipelineStoresAndAtomics` (`:384-386`).
- DXVK 3.x (HEAD 3.1.1) no longer has `src/dxbc` (`ls dxvk/src/dxbc`: absent); `dxvk/src/d3d11/d3d11_shader.cpp:34,50`
  drives `dxbc_spv::dxbc::Converter`.
- **dxbc-spirv**: MIT, Philip Rebohle 2025 (`dxbc-spirv/LICENSE:1-3`). "An SSA-based compiler for Direct3D Shader Models
  5.1 and older" with a custom IR "which can be trivially lowered to SPIR-V or translated to other, similar IRs"
  (`dxbc-spirv/README.md:1-7`); min-precision lowered to 16 or 32 bit by option (`:13-15`); SM5.0 class linkage supported
  with a specific data layout (`:19-20`); SPIR-V output targets SPIR-V 1.6 + Vulkan memory model, "backwards compatibility
  ... not planned" (`:23-26`); the standalone compiler's SPIR-V "is likely not useful as-is" because resources and I/O
  lowering are meant to be done by the embedding application (`:57-60`). Entry points
  `compileShaderToIr` / `compileShaderToLegalizedIr` (`dxbc-spirv/dxbc/dxbc_api.h:12-19`); lowering options
  `ir::CompileOptions` (`dxbc-spirv/ir/ir_legalize.h:27-61`); it can synthesise a pass-through GS
  (`dxbc_converter.h:70`). Relevance: its IR is the cleanest open DXBC front end for a *custom* back end (e.g. direct MSL
  or a GS-to-mesh lowering), but no Metal back end exists.

#### 2.2 SPIRV-Cross MSL back end

- Licence: Apache-2.0 (`spirv-cross/LICENSE`), `spirv_msl.cpp` header "SPDX-License-Identifier: Apache-2.0 OR MIT"
  (`spirv_msl.cpp:3`).
- Stage support (`spirv_msl.cpp:14679-14722`, entry-point qualifier): Vertex -> `vertex` (or `kernel` when
  `vertex_for_tessellation`), TessellationControl -> `kernel` ("tess control as compute"), TessellationEvaluation ->
  `[[patch(triangle|quad, N)]] vertex` (post-tessellation vertex function), Fragment, GLCompute, MeshEXT ->
  `[[mesh]]`, TaskEXT -> `[[object]]`. **Geometry falls to `default: entry_type = "unknown"`** - there is no geometry
  shader support. Isolines throw (`:14692-14693`, `:14706-14707`).
- Relevant options (`spirv_msl.hpp:300-555`): `msl_version` default 1.2 (`:306`), platform macOS/iOS (`:303-305`),
  `texel_buffer_texture_width` 4096 / `texture_buffer_native` (MSL 2.1) (`:307`, `:393`), swizzle buffer index (`:310`,
  `swizzle_texture_samples` `:338`), buffer-size buffer (`:315`, for `OpArrayLength`/robustness), depth-clip state buffer
  and `emulate_depth_clip_enable` (`:323`, `:346`; used `spirv_msl.cpp:13859,13890,15150-15164`),
  `emulate_reversed_depth_viewport` (`:345`), `capture_output_to_buffer` (`:337`, vertex output to buffer, used for
  tessellation), `multiview` (`:340-342`), argument buffers + tier (`:350-365`), `multi_patch_workgroup` (`:425`),
  `raw_buffer_tese_input` (`:430`), `vertex_for_tessellation` (`:435`), `force_sample_rate_shading` (`:483`),
  `enable_clip_distance_user_varying` (`:420`). Dual-source: `[[index(n)]]` emitted from `DecorationIndex`
  (`spirv_msl.cpp:14479`). Raster order groups emitted (`:14161`, `:15616-15683`). Image atomics on unknown or cube
  dimension restricted (`:11756-11764`); 64-bit atomics refused (`:11656`); stencil export needs MSL 2.1 (`:14454`).
- Cull distance: emitted as `[[user(cullN)]]` varyings, i.e. not enforced (`spirv_msl.cpp:14232-14236`, `:14367-14368`).
- Doubles: SPIR type `Double` prints `double` with the comment "Currently unsupported" (`spirv_msl.cpp:17517-17518`);
  "double types are not supported in buffers in MSL" throws (`:19218-19219`). Since MSL has no `double` (MSL §2.1), any
  DXBC shader using double ops fails here.
- Transform feedback: no `Xfb` handling in `spirv_msl.cpp` (grep: none).

#### 2.3 MoltenVK (only relevant if a Vulkan layer is kept, e.g. DXVK on MoltenVK)

- Licence: Apache-2.0 (`moltenvk/LICENSE`).
- Features (`moltenvk/MoltenVK/MoltenVK/GPUObjects/MVKDevice.mm:2794-2864`): all start cleared; set true:
  robustBufferAccess, independentBlend, sampleRateShading, depthBiasClamp, fillModeNonSolid, largePoints,
  shaderClipDistance, vertexPipeline/fragmentStoresAndAtomics, storage-image read/write without format, dualSrcBlend,
  depthClamp, multiDrawIndirect; `textureCompressionBC` from the GPU; tessellationShader + shaderInt64 on Apple3+/Mac1;
  multiViewport Apple5+/Mac1; `logicOp` and `wideLines` only with `useMetalPrivateAPI` (`:2802`, `:2806`).
  **Never set: `geometryShader`, `shaderCullDistance`, `shaderFloat64`, `shaderStorageImageMultisample`.**
  `depthClipEnable` feature true (`:687`); `transformFeedbackPreservesProvokingVertex` false (`:784`).
- Extensions: `VK_EXT_depth_clip_enable` (`Layers/MVKExtensions.def:139`), demote-to-helper (`:180`), stencil export
  (`:181`), viewport-index-layer (`:184`), fragment-shader interlock (`:146`); **no `VK_EXT_transform_feedback`** (grep of
  `MVKExtensions.def`: no hit).
- Depth clip -> `setDepthClipMode:` clip/clamp (`Commands/MVKCommandEncoderState.mm:1207`); viewports passed through
  unchanged (`Vulkan/mvk_datatypes.mm:644-653`), so a negative-height Vulkan viewport becomes a negative-height Metal
  viewport.
- Documented limitations: pipeline-statistics queries unsupported, `VkAllocationCallbacks` ignored, etc.
  (`moltenvk/Docs/MoltenVK_Runtime_UserGuide.md:685-700`).
- Consequence: DXVK's `d3d11_baseline` requirements (geometryShader, shaderCullDistance, transformFeedback) are not met
  by MoltenVK, so DXVK-on-MoltenVK cannot be a full D3D11 path without patches. **not settled**: whether DXVK refuses the
  device or only degrades (DXVK's adapter code is not in the sparse 2.7.1 clone; HEAD's was not traced).

#### 2.4 Route 1 summary for this game

- VS/PS/CS: workable. vkd3d-shader or dxbc-spirv -> SPIR-V; SPIRV-Cross with MSL 2.x/3.x, argument buffers, macOS; then
  `xcrun metal` offline (needs the Metal Toolchain, the DXMT measurement §9) or `newLibraryWithSource` at run time.
- GS: **no path** through SPIRV-Cross (no geometry execution model). Would need a custom GS -> mesh or compute rewrite
  before or instead of SPIRV-Cross.
- Doubles: break (front ends emit Float64; MSL has none).
- Cull distance: varying only (not enforced).
- Stream output: no XFB in SPIRV-Cross/MoltenVK; `capture_output_to_buffer` could be a building block.
- Glue we would own: binding model (descriptor set/binding -> Metal argument buffer indices via
  `MSLResourceBinding`), vertex attributes (`[[attribute(n)]]` from input locations), buffer-size buffer for
  structured/raw buffer bounds, swizzle buffer if needed.
- Licences acceptable for a closed game: vkd3d LGPL (use as an offline tool; output not covered), dxbc-spirv MIT,
  SPIRV-Cross Apache/MIT.

---

### 3. Route 2: DXBC -> AIR directly (DXMT "airconv")

#### 3.1 What it is

- Repository `https://github.com/3Shain/dxmt`; licence LGPL 2.1+ ("Copyright (c) 2023-2026 Feifan He for CodeWeavers",
  `dxmt/LICENSE:1-12`); DXBC parser `libs/DXBCParser` is Microsoft MIT (`dxmt/libs/DXBCParser/ShaderBinary.h:1-2`).
  Releases up to v0.80 were MIT (the DXMT measurement §1, `LICENSE.OLD`).
- Shipping: bundled in CrossOver 25 (March 2025) and CrossOver 26 (DXMT v0.72, February 2026) (write-up:
  `https://en.wikipedia.org/wiki/CrossOver_(software)`; `https://itsfoss.com/news/crossover-25-release/`). The same author
  wrote vkd3d's MSL back end ("Copyright 2024 Feifan He for CodeWeavers", `wine/.../msl.c` header).
- airconv = C++ on LLVM 15 (typed pointers: `context.setOpaquePointers(false)` at `dxmt/src/airconv/dxbc_converter.cpp:1362,
  1423,1487,1552,1615` and `airconv_cli.cpp:134`; DXMT measurement correction: five sites, not four). It writes AIR bitcode and wraps
  it as a metallib itself (`metallib_writer.cpp`; header claims platform macOS, OS 14.4, metallib version 2.7:
  `metallib_writer.cpp:393-399`). No Apple toolchain at conversion time except for three helper `.metal` files compiled
  at build time (`dxmt/src/airconv/shaders/air_msad.metal`, `air_samplepos.metal`, `air_tessellation.metal`; the DXMT measurement §5).
  15,755 lines in `src/airconv/*.cpp|*.h*` (wc).

#### 3.2 API (`dxmt/src/airconv/airconv_public.h`, `AIRCONV_VERSION 26` at `:8`)

- `SM50Initialize(bytecode, size, &shader, &reflection, &error)` (`:314`), `SM50Compile(shader, args, FunctionName,
  &bitcode, &error)` (`:319`), `SM50CompileTessellationPipelineHull(vs, hs, ...)` / `...Domain(hs, ds, ...)` (`:330-339`),
  `SM50CompileGeometryPipelineVertex(vs, gs, ...)` / `...Geometry(vs, gs, ...)` (`:341-350`), `SM50GetArgumentsInfo`.
- Variant arguments: a linked list of `SM50_SHADER_COMPILATION_ARGUMENT_DATA` (`:197-213`):
  `SM50_SHADER_COMMON` (Metal version 310/320, flags sample-NaN-to-zero, defuse-FMA: `:231-247`),
  `SM50_SHADER_PSO_PIXEL_SHADER` (sample mask, dual-source blending, disable depth output, unorm output mask, pixel
  formats: `:249-258`), `SM50_SHADER_IA_INPUT_LAYOUT` (index format, slot mask, elements with MTLAttributeFormat and
  step rate: `:260-283`), `SM50_SHADER_GS_PASS_THROUGH` (RT array index / viewport index pass-through, rasterization
  disabled: `:285-293`), `SM50_SHADER_PSO_GEOMETRY_SHADER` (strip topology), `SM50_SHADER_PSO_TESSELLATOR` (max potential
  factor: `:301-305`), `SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT` (`:222-229`), root signatures (D3D12 use).
- Reflection struct `MTL_SHADER_REFLECTION` (binding masks, thread-group size, tessellator info, GS info, PS valid render
  targets, coverage output: `:102-126`; tessellator/GS sub-structs `:65-100`). Binding: constant buffers and an argument table; argument index formula
  CB slot, sampler slot+32, SRV slot*3+128, UAV slot*3+512 (`airconv_public.h` `GetArgumentIndex`, `:176-188`).
- Without an IA-layout argument, a VS gets ordinary `[[stage_in]]` attributes keyed by input register
  (`dxbc_signature.cpp:137-151`, `InputVertexStageIn`), so a plain `MTLVertexDescriptor` pipeline works. With it, the VS
  pulls vertices from buffers itself (`pull_vertex_input`, `dxbc_converter_basicblock.cpp:385-410`, instance step rate by
  division).
- How DXMT picks variants at run time (`dxmt/src/d3d11/d3d11_shader.cpp`): VS digest = global flags + GS pass-through +
  rasterization-disabled + input-layout SHA-1 (`:121-130`); PS digest = global flags + sample mask + unorm mask +
  dual-source + disable-depth-output (`:166-176`; pixel formats are zeroed `:186`); CS default (`:205-210`); VS+HS and
  HS+DS pairs (`:228-290`). Function names `vs_<sha8>_<digest>` etc. Cache: per-variant metallib bytes in sqlite keyed by
  variant digest (`d3d11_shader.cpp:52,90`; `d3d11_pipeline_cache.cpp:147-158`; path rules in
  `dxmt/src/dxmt/dxmt_shader_cache.cpp:21-34`). The DXMT measurement's statements on the cache and the CLI hold.
- CLI `airconv` (`airconv_cli.cpp`): options `-o`, `-S`, `-A` (metallib), `--O0/1/2`, `--hull-before-domain`,
  `--vertex-before-hull`, `--hull-after-vertex`, `--vertex-before-geometry`, `--geometry-after-vertex`, `--disas-dxbc`
  (`:27-66`); always Metal 3.2 (`:219`) and entry name `shader_main` (`:246-366`). So a single shader converts to a generic
  variant; GS needs its VS partner. DXMT measurement correction/addition: the generic VS output is a normal stage-in vertex function,
  usable by a runtime of our own that sets a matching `MTLVertexDescriptor`; and a small driver calling `SM50Compile`
  with real variant arguments can produce exactly what DXMT would.

#### 3.3 Geometry shaders

- GS is not converted alone ("Geometry shader cannot be independently converted.", `dxbc_converter.cpp:888-891`).
  The VS becomes an **object function** that pulls vertices (index buffer, draw arguments) and fills a 16,256-byte
  payload per warp (`dxbc_converter_gs.cpp:520-584`, payload store `:867-873`, `CreateSetMeshProperties`); the GS
  becomes a **mesh function** (`:99-162`, mesh output topology point/line/triangle with `max_vertex_out` vertices).
  `EmitThenCut`/`emit`/`cut` are lowered to mesh vertex/index writes (`:219-335`), clip distances written per mesh vertex
  (`:207-216`), GS instancing via the grid y index (`:191`). Vertices per warp tables by primitive type, adjacency and
  patch inputs (`:19-97`).
- Errors: unsupported output topology (`:157`), unhandled strip input primitive (`:470`, `:846`).
- Multiple streams: `emit_stream`/`cut_stream` with stream != 0 only `assert` (`dxbc_converter_cfg.cpp:639-650`);
  release builds have asserts off (`dxmt/meson.build:5` `b_ndebug=if-release`), so stream >0 is silently treated as 0.
  Flags "SA" (SO appending) and "MS" (multiple SO streams) in `dxmt/docs/COMPATIBILITY_FLAG.md`.
- Pipeline requirement: mesh shaders, Apple7+ (FST p.4; the DXMT measurement §8 for DXMT's check). Geometry + tessellation together is
  unsupported (flag "GT", `COMPATIBILITY_FLAG.md`).

#### 3.4 Tessellation (for completeness)

- VS+HS -> object function (`convert_dxbc_vertex_hull_shader`, `dxbc_converter_ts.cpp:178`, payload `:295-299`);
  tessellator + DS -> mesh function (`convert_dxbc_tesselator_domain_shader`, `:732`, `:822-842`). DXMT does **not** use
  Metal's fixed-function tessellator: it ships its own fixed-point tessellator in MSL (826 lines,
  `dxmt/src/airconv/shaders/air_tessellation.metal`, partitioning enum integer/pow2/fractional_odd/fractional_even `:5-10`,
  factor regularisation ranges per mode `:102-125`). All four D3D11 partitioning modes are handled
  (`dxbc_converter_ts.cpp:36-53`, clamps 1..64 / 2..64 `:77-104`).
- Limits: max potential factor clamped to 8 on GPUs below Apple9 unless config `d3d11.maxTessFactor` (4..64)
  (`dxmt/src/d3d11/d3d11_pipeline_ts.cpp:55-70`, with a warning); point/isoline output unsupported (flag "TO");
  HS control-point pass-through requires VS output signature to match (`dxbc_converter_ts.cpp:558-563`).

#### 3.5 Stream output

- Only "vertex shader + SO declaration" is emulated: `CreateGeometryShaderWithStreamOutput` with a DXBC VS writes via
  `SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT` (`dxmt/src/d3d11/d3d11_device.cpp:275-292`); otherwise it logs
  "not supported, expect problem" and builds a GS without SO (`:288-290`). the DXMT measurement §6 matches. `DrawAuto` stubbed.

#### 3.6 Coverage of SM5 instructions and system values

- 133 `case microsoft::` opcode entries in `dxmt/src/airconv/dxbc_instructions.cpp` (grep count), including
  `gather4_po(_c)` (`:753-790`), `sample_info` (`:793`), `bufinfo` (`:842`), typed UAV load/store (`:936`, `:954`),
  raw/structured (`:972`, `:1019`), `msad` (`:1247`), `bfrev`/`countbits` (`:1362-1369`), `uaddc`/`usubb`
  (`:1542-1552`), `f16tof32`/`f32tof16` (`:1562-1569`), `lod` (`:1642`), `sync` (`:1653`), append/consume
  `imm_atomic_alloc`/`consume` (`:1845-1853`), `eval_centroid`/`eval_sample_index`/`eval_snapped` (`:2063-2086`).
- **Doubles: no case for any D3D11 double opcode** (grep for `DADD|DMUL|...|DTOF|FTOD` in `dxbc_instructions.cpp`: none).
  The default branch prints "unhandled dxbc instruction" and `assert`s, then returns a no-op (`:2098-2101`); with release
  asserts off the shader silently loses those instructions. DXMT reports `DoublePrecisionFloatShaderOps = FALSE` (the DXMT measurement §6,
  `d3d11_inspection.cpp:19`), so a well-behaved game should not create such shaders, but precompiled shaders are not
  checked.
- Class linkage / interfaces: `assert(0 && "interface resource is not supported yet")` (`dxbc_instructions.cpp:435`).
- udiv by zero -> all ones via select (`dxmt/src/airconv/nt/dxbc_converter_base.cpp:1256-1271`), matching D3D.
- SV values: clip distance -> Metal clip distance; **cull distance -> a `SV_CullDistanceN` user varying only**
  (`dxbc_signature.cpp:188-200`, `:382-384`); the collected `cull_distance_scalars` (`dxbc_converter.hpp:443`) are
  never consumed (grep across `dxmt/src`), so cull distances are not enforced. RT array index / viewport index outputs
  (`:218-235`, `:365-372`); sample index input (`:414`); `oDepthGE/LE` (`:496-516`); stencil ref (`:537`); coverage output
  (`:558`); inner coverage input asserts (`:337`).
- Unsupported-feature errors (`llvm::make_error<UnsupportedFeature>` / `throwUnsupported`): HS/DS/GS alone
  (`dxbc_converter.cpp:886-898`), undefined basic block and un-lowered call terminators
  (`dxbc_converter_basicblock.cpp:527`, `:613`), non-32-bit integer inputs (`:228`), unknown interpolant input (`:246`),
  GS topology/strip (`dxbc_converter_gs.cpp:157,470,846`), HS pass-through signature mismatch
  (`dxbc_converter_ts.cpp:561`). Many other gaps are `assert(0)` only (e.g. `dxbc_signature.cpp` unhandled system values
  at `:114,158,239,269,...`), i.e. silent in release.
- Min precision ignored (32-bit), legal for D3D.

#### 3.7 Fixed-function state DXMT handles beside the shader

- Depth clip -> clamp mode, winding, cull (`d3d11_state_object.cpp:440-460`); D24S8 emulated on Depth32Float_Stencil8
  (`dxmt_format.cpp:706-735`); B5G6R5 native (`:957-958`); R11G11B10 native (`:599-603`); R32G32B32_FLOAT as R32Float
  12-byte texel (`:488-492`); logic op through a non-public selector (the DXMT measurement §8).

#### 3.8 Route 2 summary for this game

- Strengths: direct DXBC, the D3D-specific semantics are already encoded (udiv, sample positions, msad, eval_*,
  dual-source variants, sample mask), VS/PS/CS convert standalone; GS without SO works on Apple7+ as VS+GS pairs; in use
  by CrossOver.
- Gaps: doubles silently dropped; cull distance not enforced; interfaces unsupported; LLVM 15 pinned (the DXMT measurement §5);
  AIR/metallib written by hand (bitcode compatibility with future Metal compilers is DXMT's risk; DXMT targets
  `air64-apple-macos14.0`).
- AOT plan: a converter tool linking airconv, calling `SM50Initialize` then `SM50Compile` with our own variant arguments
  (Metal 3.1 or 3.2, no IA layout, PS defaults) for each VS/PS/CS, and `SM50CompileGeometryPipeline*` for each VS+GS pair
  the game binds (pairs must be known: either from a capture or by converting every GS against every VS that feeds it).
  Output: metallibs plus `MTL_SHADER_REFLECTION` binding masks to serialize next to them. LGPL applies to the tool, not
  to its output [inferred, not legal advice].

---

### 4. Route 3: DXBC -> source (MSL/GLSL/HLSL) -> a compiler

#### 4.1 vkd3d-shader's MSL back end (`wine/libs/vkd3d/libs/vkd3d-shader/msl.c`, 2,461 lines)

- Status: experimental. Only offered when built with `VKD3D_SHADER_UNSUPPORTED_MSL`
  (`vkd3d_shader_main.c:2245-2247`, also for HLSL and DXIL sources `:2263-2265`, `:2284-2286`); the public list of
  supported transformations omits MSL (`vkd3d_shader.h:3025-3049`); target added "\since 1.14" (`:1648-1650`). Release notes
  quoted in the press: "only able to compile the most basic shaders when targetting MSL" (1.14) and still "experimental"
  in 2.0 (write-ups: `https://phoronix.com/news/VKD3D-2.0-Released`, `https://linuxiac.com/wine-vkd3d-2-0-brings-direct3d-12-to-vulkan-updates/`,
  `https://www.gamingonlinux.com/2026/05/wine-11-10-is-out-with-vkd3d-2-0-vbscript-compatibility-improvements-and-more/page=1/`).
- Stages: vertex, fragment, kernel only; anything else "Unhandled shader type" (`msl.c:2162-2180`, inputs `:1808-1819`).
  No GS, HS, DS.
- Instructions handled (`msl_handle_instruction`, `msl.c:1442-1694`): arithmetic/logic, comparisons, control flow,
  derivatives, `countbits`/`firstbit`, `frc`, conversions, sample/sample_b/c/c_lz/grad/lod, gather4/_c/_po, `ld`,
  `ld2dms`, `store_uav_typed`, `sync`, `discard`, `udiv` (simple), `idiv`, `irem`/`urem`. Everything else goes to
  `msl_unhandled` which emits a comment and a compiler error (`:662-670`). **Missing** include: raw/structured loads and
  stores, all atomics and append/consume, `resinfo`, `bufinfo`, `sample_info`, bit-field ops (`ubfe`/`ibfe`/`bfi`/`bfrev`),
  `f16tof32`/`f32tof16`, `eval_*`, `emit`/`cut`, doubles, `msad`, `uaddc`/`usubb`, `imul`/`umul` wide forms, `dcl_` for
  UAV counters. Descriptor indexing unsupported (`:944-945`, `:1069-1070`, `:1251-1252`); several resource types
  unsupported for fetch/sample/store (`:972-973`, `:1094-1104`, `:1275`).
- Licence: LGPL 2.1+.

#### 4.2 vkd3d-shader's GLSL back end (`glsl.c`)

- Same gating (`VKD3D_SHADER_UNSUPPORTED_GLSL`, `vkd3d_shader_main.c:2242-2244`); vertex/pixel/compute only
  (`glsl.c:2896-2904`); descriptor indexing unsupported (`:647` etc.). Not useful for Metal except via GLSL -> SPIR-V ->
  SPIRV-Cross, which is longer than Route 1.

#### 4.3 Unity HLSLcc (`hlslcc`)

- MIT (HLSLcc) + BSD (bstring) (`hlslcc/README.md` "License"; `license.txt`). Last commit 2020-09-23 (`git log`).
  "This library is used to generate all shaders in Unity for OpenGL, OpenGL ES 3.0+, Metal and Vulkan" (`README.md`),
  with "Lots of Unity-specific tweaks" (same file). Entry point `TranslateHLSLFromMem()`.
- Metal stages: VS (`vertex Mtl_VertexOut xlatMtlMain`, or a helper when tessellating), PS, CS, HS as
  `kernel patchKernel`, DS as `[[patch(...)]] vertex` (`hlslcc/src/toMetal.cpp:446-475`); default -> "Not supported"
  `ASSERT(0)` (`:476-479`). **No geometry shaders**: `cut`/`emit` opcodes are `ASSERT(0); // Not on metal`
  (`toMetalInstruction.cpp:2943-2950`).
- Tessellation uses Metal's fixed-function tessellator (factor buffer `MTLQuad/TriangleTessellationFactorsHalf`,
  `toMetal.cpp:359`, `:592`), all four partition modes mapped (`:798-812`), point/line output and isoline domain
  reported unsupported (`:826-831`; `toMetalDeclaration.cpp:1612`).
- Doubles: "doubles not supported on Metal" `ASSERT(0)` (`toMetalOperand.cpp:492`), although double opcodes have cases
  (`toMetalInstruction.cpp:2073-2091`).
- Bindings are reported through Unity's reflection callbacks; output is MSL source that needs Apple's compiler. Maturity
  for Metal: proven in Unity up to 2020; unmaintained publicly since.

#### 4.4 DXBC -> HLSL decompilers

- 3Dmigoto contains `HLSLDecompiler` and `BinaryDecompiler` directories; licence files `LICENSE.GPL.txt` and
  `COPYING.txt` suggest GPL; version **not settled** (write-up/listing: `https://github.com/bo3b/3Dmigoto`). Forks
  (`zxxyye/HLSLDecompiler`) are used by modding tools (write-up: `https://www.gta5-mods.com/tools/shaderv-the-ingame-shader-editing-tool`).
  CMD_Decompiler: no repository or licence found (search). Decompiled HLSL would then go through DXC -> DXIL -> Metal
  shader converter, or through HLSL -> SPIR-V. Accuracy of decompilers on arbitrary SM5 code: **not settled** (not
  tested); they are built for hand-edited mods, not bulk recompilation.

---

### 5. Route 4: Apple's Metal shader converter (IRConverter)

#### 5.1 What Apple documents

- "converts shader intermediate representations in LLVM IR bytecode into bytecode suitable to be loaded into Metal"; the
  CLI "takes a DXIL file as input and produces a metallib"; library entry `IRObjectCreateFromDXIL` (documented:
  `https://developer.apple.com/metal/shader-converter/`). Shader models listed SM6.0-6.6. Requirements: tool needs macOS 13
  + Xcode 15 (Windows 10 + VS2019 for the Windows build); produced libraries need Argument Buffers Tier 2 and macOS 14 /
  iOS 17 (same page). Versions 1, 1.1, 2, 3, 3.1, 4.0 (4.0 beta at fetch time) with highlights (same page). DXBC is not
  mentioned anywhere on the page.
- Geometry and tessellation: WWDC23 session 10124 says it converts "the traditional graphics pipeline including
  tessellation and geometry shaders" by "mapping them to Mesh Shaders", including the tessellator, and that its runtime
  "emulates draw calls by dispatching mesh shading work" (documented: `https://developer.apple.com/videos/play/wwdc2023/10124/`).
  Details (limits, stream output, max factor): **not settled** (the user manual PDF is behind the developer download page
  `https://developer.apple.com/download/all/?q=metal%20shader%20converter`).
- Licence / redistribution: not on the page; terms ship with the installer; an Apple forum question about bundling
  `libmetalirconverter` has no visible answer (write-up: `https://developer.apple.com/forums/thread/829379`). **not
  settled.** For AOT use only the CLI on the build machine and the runtime header would be needed in the game.
- Apple's own D3D translation (D3DMetal in Game Porting Toolkit / CrossOver) is closed and not a redistributable route
  (CrossOver bundles it under Apple's terms: write-up `https://en.wikipedia.org/wiki/CrossOver_(software)`).

#### 5.2 Getting from DXBC to DXIL

- **Microsoft publishes a DXBC -> DXIL converter in the DXC repository**: `projects/dxilconv` with the library
  `lib/DxbcConverter/DxbcConverter.cpp` (8,316 lines), COM interface `IDxbcConverter::Convert(pDxbc, size, options,
  &pDxil, &size, &diag)` and `ConvertInDriver` "for driver consumption only" (`dxc` `HEAD:projects/dxilconv/include/DxbcConverter.h`,
  CLSID `4900391E-B752-4EDD-A885-6FB76E25ADDB`), tools `tools/dxbc2dxil/dxbc2dxil.cpp` and `tools/dxilconv/dxilconv.cpp`
  (+ `.def`), 127 DXBC test inputs under `test/dxbc2dxil/` (`git ls-tree` of `dxc` HEAD). Licence: University of Illinois
  Open Source License (NCSA), Microsoft copyright (header of `DxbcConverter.h`). The DXC release package also ships
  `dxilconv.dll` (write-up: `https://www.gitlink.org.cn/mirrors/DirectXShaderCompiler`); `docs/DXIL.rst` names
  `dxbc2dxil` in its component diagram (write-up: `https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/DXIL.rst`).
- Coverage evidence: handles double ops (`DxbcConverter.cpp:2844` `DADD`), interface calls (`:4517`), GS `emit`/
  `emit_stream`/`emitthencut(_stream)` (`:5019-5070`). Output targets shader model 6.0 (`ShaderModel::Get(kind, 6, 0)`,
  `:1270`) with DXIL version 1.0 (`:8295`) and a PSV0 part (`:123-253`); original I/O signatures are appended (`:258`).
- Build: **Windows only** - `projects/CMakeLists.txt` adds `dxilconv` only `if(WIN32)` and `HLSL_BUILD_DXILCONV`;
  `projects/dxilconv/CMakeLists.txt` adds lib/tools only `if(WIN32)`. It is plain C++ on DXC's LLVM 3.7 fork with
  Windows types; a non-Windows port is **not settled**. Since this is an offline step, running it on a Windows machine
  (or under Wine) is enough.
- Container: DXIL uses the same container as DXBC: header four-cc `DXBC` "for back-compat with tools that look for DXBC
  containers", then part offsets; part codes `RDEF`, `ISG1`, `OSG1`, `PSG1`, `STAT`, `ILDB`, `SFI0`, `RTS0`, `DXIL`, `PSV0`,
  `RDAT`, `HASH` (`dxc/include/dxc/DxilContainer/DxilContainer.h:59-97`). DXBC's `ISGN/OSGN/SHDR/SHEX` parts therefore
  sit in an identical outer structure, which is what makes "same container, different program part" conversion simple.
- Validation/signing: whether dxilconv output is validated/hashed and whether Metal shader converter requires that:
  **not settled**.
- Recompiling from HLSL source is not available for this game.

#### 5.3 Route 4 summary for this game

- Pros: Microsoft's own semantic translation of DXBC (covers doubles at the DXIL level, GS, interfaces) and Apple's own
  converter with GS/tessellation emulation and maintained by Apple; output is metallib.
- Cons: two closed or platform-bound tools in the chain (dxilconv builds on Windows only; Metal shader converter is
  proprietary with unknown redistribution terms but needed only offline); whether MSC accepts dxilconv's SM6.0 output,
  and how it lowers doubles (Metal has none): **not settled**; MSC's binding model is a D3D12-style root signature /
  top-level argument buffer, so a D3D11 runtime on top must build root-signature-like layouts for D3D11 slots
  (inferred from the DXIL input; not read in Apple's manual); requires Argument Buffers Tier 2 and macOS 14 (documented).

---

### 6. Feature-by-feature: how each route handles the hard D3D11 parts

| Item | R1 vkd3d/dxbc-spirv + SPIRV-Cross | R2 DXMT airconv | R3 vkd3d-MSL / HLSLcc | R4 dxilconv + MSC |
|---|---|---|---|---|
| VS/PS/CS | yes | yes | vkd3d-MSL: basic ops only; HLSLcc: yes (2020 level) | yes (MSC SM6.0+) |
| GS | no (SPIRV-Cross has no geometry model, §2.2) | yes, VS+GS -> object+mesh, Apple7+ (§3.3) | no (vkd3d-MSL stages; HLSLcc `ASSERT` on emit) | yes per WWDC23 (mesh), details not settled |
| HS/DS | yes, tess control as compute + post-tess vertex; isoline no | yes, own tessellator in mesh, all partition modes, factor 8 below Apple9 | HLSLcc yes (fixed-function tessellator), vkd3d no | yes per WWDC23 (mesh, incl. tessellator) |
| Stream output | no XFB in SPIRV-Cross; `capture_output_to_buffer` building block | VS+SO only; GS SO not | no | not settled |
| Doubles | break (Float64 -> `double`) | silently dropped (release) | HLSLcc assert | dxilconv converts; MSC behaviour not settled |
| Cull distance | user varying only | user varying only | n/a | not settled |
| udiv by 0 | front ends select ~0 (vkd3d `ir.c:2377`, DXVK `:2145`) | select ~0 (`dxbc_converter_base.cpp:1256`) | vkd3d-MSL `udiv_simple` after IR lowering; HLSLcc not checked | dxilconv: not checked |
| Dual-source | `[[index]]` | PS variant flag | HLSLcc not checked | not settled |
| D24S8, RGB32 etc. | runtime concern, not shader | DXMT format table (§3.7) | runtime | runtime |
| Licence | LGPL / MIT + Apache/MIT | LGPL 2.1+ (+MIT parser) | LGPL / MIT | NCSA + Apple proprietary (terms not settled) |
| Needs at build | Apple `metal` compiler for MSL (or runtime source compile) | LLVM 15 + Metal toolchain to build airconv once | Apple `metal` compiler | Windows for dxilconv; macOS 13+/Xcode 15 for MSC |
| Run-time | metallib or MSL source | metallib (AIR) | MSL source -> metallib | metallib + MSC runtime header |

---

### 7. Re-check results of the DXMT measurement

- Confirmed: `AIRCONV_VERSION 26`; `SM50*` API at `airconv_public.h:314-352`; CLI fixed to Metal 3.2 and `shader_main`;
  variant inputs; sqlite cache; tess factor clamp below Apple9; `COMPATIBILITY_FLAG.md` flags; SO emulation only for VS;
  `DoublePrecisionFloatShaderOps = FALSE`; D24 emulation on Depth32Float_Stencil8.
- Corrections/additions: five `setOpaquePointers(false)` sites (`dxbc_converter.cpp:1615` too); without an IA layout the
  VS is a normal `[[stage_in]]` function (`dxbc_signature.cpp:146-151`), so the CLI's generic output is usable by a
  custom runtime; release builds turn several "unsupported" asserts (doubles, multiple GS streams, interfaces) into silent
  miscompiles rather than errors, so a conversion sweep must also scan the DXBC for these opcodes, not only rely on
  `UnsupportedFeature`.

---

### 8. Ranking for this project

Project facts: ~6,500 VS + ~4,400 PS (plus CS) precompiled SM4/5, GS without stream output, no HS/DS created,
`D3DReflect` needed only for input signatures, AOT on arm64 macOS.

1. **Route 2, DXMT airconv driven by our own offline tool.** It is the only open converter that takes DXBC straight to
   Metal with VS, PS, CS *and* GS (as object+mesh) and that already encodes D3D11 semantics; it is in production via
   CrossOver. The game uses none of its weak areas except possibly doubles and cull distance, both detectable by a scan.
   Its reflection (`MTL_SHADER_REFLECTION`) and DXBCParser (MIT) also cover what a D3DReflect input-signature shim needs.
   Costs: LLVM 15 toolchain once; VS+GS pairs must be enumerated; GS needs Apple7+ (all Apple-silicon Macs); LGPL tool.
   Fits either "use DXMT as the D3D11 runtime" (the DXMT measurement) or "our own Metal runtime" (stage-in VS, documented binding indices).
2. **Route 4, dxilconv (Windows, offline) -> Metal shader converter.** Strongest semantic fidelity at the front (Microsoft's
   own DXBC reader) and Apple-maintained back end with GS emulation, but two unknowns that could each block it (MSC
   accepting dxilconv output; MSC terms), plus a D3D12-style binding model to build. Worth a one-day spike on a sample of
   shaders as a cross-check for Route 2 results, not as the primary path.
3. **Route 1, vkd3d-shader or dxbc-spirv -> SPIR-V -> SPIRV-Cross -> MSL.** Mature, permissively licensed pieces and
   readable MSL output (good for debugging and as an oracle), but no GS path through SPIRV-Cross and doubles fail; the
   GS shaders would need a separate solution. A reasonable fallback for VS/PS/CS if airconv fails on specific shaders.
4. **Route 3, source-level back ends.** vkd3d's MSL target is experimental and lacks raw/structured buffers, atomics,
   resinfo etc. (§4.1 of this part); HLSLcc has no GS, is Unity-tuned and unmaintained since 2020; decompilers are mod tools of
   unclear licence. Not suitable for ~11,000 shaders.

The scan that decides how much the gaps matter: `dcl_globalFlags` with ENABLE_DOUBLE_PRECISION_FLOAT_OPS or double opcodes, SFI0 bits, STAT, `dcl_output_siv ... cull_distance`, `dcl_stream`, `fcall` and `dcl_interface` in every shader the game ships.

## Part 5. Doing it ahead of time

### 0. Metal: what a pipeline state object (PSO) bakes in, and what the encoder sets

- **Baked into `MTLRenderPipelineState` [doc].** The `MTLRenderPipelineDescriptor` topics list these
  properties (https://developer.apple.com/documentation/metal/mtlrenderpipelinedescriptor):
  - `vertexFunction`, `fragmentFunction`, `vertexDescriptor`;
  - `colorAttachments[]`, `depthAttachmentPixelFormat`, `stencilAttachmentPixelFormat`;
  - `isAlphaToCoverageEnabled`, `isAlphaToOneEnabled`, `isRasterizationEnabled`, `inputPrimitiveTopology`,
    `rasterSampleCount`;
  - the tessellation state, `maxVertexAmplificationCount`, `binaryArchives`, linked functions, and
    buffer mutability.
- **Blending is part of each color attachment of the PSO [doc].** Each `colorAttachments[i]` carries
  `pixelFormat`, `writeMask`, `isBlendingEnabled`, the RGB/alpha blend operations and the four blend factors
  (https://developer.apple.com/documentation/metal/mtlrenderpipelinecolorattachmentdescriptor).
  So a D3D11 blend state, including `RenderTargetWriteMask`, is PSO state on Metal.
- **Not baked: set on `MTLRenderCommandEncoder` [doc]**
  (https://developer.apple.com/documentation/metal/render-pass-configuration):
  - `setTriangleFillMode`, `setFrontFacing`, `setCullMode`;
  - `setDepthStencilState` (a separate object), `setDepthBias(slopeScale:clamp:)`, `setDepthClipMode`;
  - `setStencilReferenceValue(s)`, `setBlendColor`;
  - `setViewport(s)`, `setScissorRect(s)`, visibility mode;
  - resources (buffers and textures per stage).
  So the whole D3D11 rasterizer state and depth-stencil state, plus the blend factor and stencil reference,
  never enter a Metal PSO. DXMT encodes them this way: `src/d3d11/d3d11_state_object.cpp:436-460`
  (`SetupRasterizerState` fills fill mode, cull, depth clip, depth bias, winding) [src].
- **No fixed sample mask in a Metal PSO [src].** SPIRV-Cross says Metal has no fixed sample mask in the
  pipeline, so it ANDs one into the shader's coverage output
  (`spirv-cross/spirv_msl.hpp:327-330`, `additional_fixed_sample_mask`). DXMT does the same:
  `dxmt/src/airconv/dxbc_converter.cpp:377-386` adds a coverage-mask output when `sample_mask != 0xffffffff`.
  D3D11's `OMSetBlendState` SampleMask therefore becomes **shader** state.
- **Vertex stride [doc + src].**
  - Metal: `MTLVertexBufferLayoutDescriptor.stride` is part of the vertex descriptor, so it is part of the PSO
    (https://developer.apple.com/documentation/metal/mtlvertexbufferlayoutdescriptor/stride).
  - Metal 3 adds `MTLBufferLayoutStrideDynamic` with `setVertexBuffer(_:offset:attributeStride:index:)`
    (https://developer.apple.com/documentation/metal/mtlbufferlayoutstridedynamic). Apple's pages give
    only the signature.
  - MoltenVK uses the dynamic form when the stride is not static: `moltenvk/MoltenVK/MoltenVK/GPUObjects/MVKPipeline.mm:1724`.
    It rejects static strides that are not aligned to `vertexStrideAlignment` (`:1707-1708`).
  - In D3D11 the strides come from `IASetVertexBuffers` (pStrides), not from the input layout
    (https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-iasetvertexbuffers) [doc].
- **Metal 4 [doc].**
  - `MTL4RenderPipelineDescriptor` has `colorAttachments`, `alphaToCoverageState`, `alphaToOneState`,
    `colorAttachmentMappingState`, `inputPrimitiveTopology`, `isRasterizationEnabled`, `rasterSampleCount`,
    `vertexDescriptor` and function descriptors.
  - It lists **no depth/stencil pixel format** and no binary-archive or buffer-mutability fields
    (https://developer.apple.com/documentation/metal/mtl4renderpipelinedescriptor).
  - Color attachments have `pixelFormat`, `writeMask` and `blendingState` (`MTL4BlendState`: disabled,
    enabled, unspecialized) (https://developer.apple.com/documentation/metal/mtl4blendstate).
  - `MTLColorWriteMask` has an `unspecialized` value
    (https://developer.apple.com/documentation/metal/mtlcolorwritemask).

### 1. What each stage needs beyond its bytecode

#### 1.1 Vertex shader

- **Input layout.** It fixes, per element:
  - DXGI format, input slot and `AlignedByteOffset` (or APPEND_ALIGNED);
  - per-vertex or per-instance class and `InstanceDataStepRate`;
  - the semantic that matches a VS input-signature register.

  `CreateInputLayout` validates the elements against the input signature of the bytecode it is given. The
  layout "can be reused with any other shader that has an identical input signature (semantics included)"
  (https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11device-createinputlayout, Remarks)
  [doc]. `D3D11_INPUT_ELEMENT_DESC` has no stride
  (https://learn.microsoft.com/windows/win32/api/d3d11/ns-d3d11-d3d11_input_element_desc) [doc]. The game
  builds layouts at one call site, from a VS input-signature blob it gets with `D3DGetBlobPart`
  [proj: 31-d3d11-usage.md:20,157].
- **DXMT: vertex fetch in the shader, so the VS function depends on the layout [src].**
  - DXMT never builds an `MTLVertexDescriptor`. The VS variant takes `SM50_SHADER_IA_INPUT_LAYOUT_DATA`
    (`airconv_public.h:260-283`: per element register, slot, offset, Metal attribute format, step
    function and rate, plus a slot mask).
  - The converter emits a prologue that loads each attribute from a vertex-buffer table
    (`dxbc_signature.cpp:137-145` calls `pull_vertex_input`, `dxbc_converter_basicblock.cpp:375-437`). The
    **stride is read at run time** from the table entry (`:430-434`).
  - So the variant digest covers layout content but not strides: the VS digest is global flags +
    `gs_passthrough` + `rasterization_disabled` + the layout's SHA-1 (`d3d11/d3d11_shader.cpp:121-130`).
    The layout SHA-1 is computed in `d3d11/d3d11_pipeline_cache.cpp:166-173`.
  - Layout registers are resolved against the VS signature when the layout is created
    (`d3d11/d3d11_input_layout.cpp:55-90`).
- **DXVK: Vulkan vertex-input state, so the shader is independent of the layout [src].**
  - The layout goes into the vertex-input part of the pipeline: `DxvkGraphicsPipelineVertexInputState`,
    and a separate vertex-input pipeline library, `dxvk/src/dxvk/dxvk_graphics.h:45-95`.
  - The shader is patched only when the VS reads a location the layout does not provide
    (`dxvk_graphics.cpp:910-920`, `getLinkage`; `canCreateBasePipeline` returns false at `:1260-1268`).
- **MoltenVK [src].**
  - Builds `MTLVertexDescriptor` from Vulkan vertex input (`MVKPipeline.mm:1197`, attributes at `:1750-1810`).
  - Fixes up offsets larger than the stride by inventing extra bindings (`:1780-1795`).
  - Truncates attribute vectors wider than the stride (`:1796-1803`).
  - For tessellation it fetches vertices in a compute stage through `MTLStageInputOutputDescriptor`
    (`:1241-1245`) and needs one variant per index type (none / 16 / 32-bit, `:1530`). SPIRV-Cross explains
    why: compute must know the index type at pipeline creation (`spirv_msl.hpp:472-478`).
- **Formats Metal's vertex descriptor cannot express [doc + inference].**
  - `MTLVertexFormat` covers:
    - 8/16/32-bit int and float vectors, normalized 8/16;
    - `int1010102Normalized` and `uint1010102Normalized`;
    - `uchar4Normalized_bgra`, `floatRG11B10` and `floatRGB9E5`
    (https://developer.apple.com/documentation/metal/mtlvertexformat).
  - It has **no unnormalized 10:10:10:2 integer** (D3D `R10G10B10A2_UINT`). That is an inference from the
    list.
  - With fetch in the shader (DXMT), any format is just code.
  - Not checked: which DXGI formats the game's layouts actually use.
- **Which stage follows the VS.**
  - With a GS, DXMT compiles a different VS ("vsgs", run as the mesh pipeline's object function). Its digest
    includes the GS SHA-1, the index-buffer format, strip topology and the layout
    (`d3d11_shader.cpp:365-375`; built in `d3d11_pipeline_gs.cpp:47-51,99-107`) [src].
  - Without a GS, the VS variant still carries `gs_passthrough` (render-target-array / viewport-index output
    registers) and "rasterization disabled" (`airconv_public.h:78-83,285-293`;
    `dxbc_converter.cpp:597-601,710-720`) [src].
  - Stream output gives a separate "vsso" variant (`d3d11_shader.cpp:315-323`). This game never calls
    stream output [proj: 31-d3d11-usage.md:19], so that variant does not occur.

#### 1.2 Pixel shader

- **Render-target formats: output type and channel count [src].**
  - A Metal fragment output's type has to suit the attachment format (float vs int vs uint).
  - DXMT's PS data has a `pixel_formats[8]` slot (`airconv_public.h:249-258`). The converter would take the
    output type from it (`dxbc_signature.cpp:605-608`), falling back to the signature type when the format is
    unknown.
  - The D3D11 front end, however, **zeroes `pixel_formats`** (`d3d11_shader.cpp:186`), so today the output
    type comes from the DXBC signature only.
  - What DXMT does key on is `unorm_output_reg_mask`: which RTs are 8-bit UNORM, per
    `IsUnorm8RenderTargetFormat` (`d3d11_pipeline.cpp:22-26`). For those RTs the converter subtracts a small
    constant before output to match D3D rounding (`dxbc_converter_basicblock.cpp:312-321`, `dxbc_signature.cpp:625-628`).
  - MoltenVK passes `enable_frag_output_mask` for attachments in use. With alpha-to-coverage it keeps output 0
    even with no attachment (`MVKPipeline.mm:2087-2103`).
  - SPIRV-Cross has `pad_fragment_output_components` and `set_fragment_output_components`, because
    "Fragment output in MSL must have at least as many components as the render pass"
    (`spirv_msl.hpp:373-375,788-790`).
  - DXVK patches PS outputs with per-RT component swizzles (`rtSwizzles`) taken from the attachment
    (`dxvk_graphics.cpp:905-909`). vkd3d does the same through `output_swizzles`, from the RT format
    (`wine/libs/vkd3d/libs/vkd3d/state.c:3328-3333`; field at `include/vkd3d_shader.h:1766`).
- **Depth format.**
  - DXMT keys `disable_depth_output` on "no depth-stencil format bound" (`d3d11_pipeline.cpp:38-41`).
  - MoltenVK sets `enable_frag_depth_builtin` / `enable_frag_stencil_ref_builtin` from the attachment formats
    (`MVKPipeline.mm:2109-2110`) [src].
- **Sample count, sample mask, alpha-to-coverage.**
  - `rasterSampleCount` and alpha-to-coverage are PSO fields [doc, section 0].
  - The sample mask goes into the shader (DXMT `sample_mask`, `d3d11_shader.cpp:171`; SPIRV-Cross
    `additional_fixed_sample_mask`) [src].
  - DXVK needs the sample count inside the shader only for `GetRenderTargetSampleCount`. It is a
    specialization constant set from the rasterizer state at draw time (`dxvk/src/d3d11/d3d11_shader.cpp:129-131`,
    `d3d11_context.cpp:3460-3474`) [src].
  - vkd3d's equivalent is the parameter `VKD3D_SHADER_PARAMETER_NAME_RASTERIZER_SAMPLE_COUNT`
    (`include/vkd3d_shader.h:716-729`) [src].
- **Dual-source blending.** It is a property of the blend state, but it changes the PS outputs.
  - DXMT: `dual_source_blending` (`d3d11_shader.cpp:173`, `dxbc_signature.cpp:609-617`).
  - DXVK: `fsDualSrcBlend` (`dxvk_graphics.cpp:902`). Dual source forces DXVK off the fast-link path
    (`:1306-1308`).
  - vkd3d: `vkd3d_shader_spirv_target_info.dual_source_blending` (`vkd3d_shader.h:1765`; set from the blend
    desc at `state.c:3424`) [src].
- **Blend state and write masks.** These are PSO state in Metal [doc]. DXMT applies them at PSO creation
  (`d3d11_state_object.cpp:317-355`; alpha-to-coverage at `:355`), with no shader change [src].
- **Sample-rate shading and flat shading.**
  - In DXBC SM4/5, interpolation modes (including `sample`) are declared in the bytecode, so they are known
    per shader. vkd3d says its flat-interpolation parameter is "ignored if the shader model is 4 or greater"
    (`vkd3d_shader.h:766-788`) [src].
  - DXVK still has `fsFlatShading` and `HasSampleRateShading` paths (`dxvk_graphics.cpp:903`, `:1310-1335`).
    These serve D3D9; not checked further.
- **Depth output.** `oDepth` and the conservative variants are declared in DXBC. DXMT's only per-pipeline
  depth input is "drop depth output when no DSV" (above).

#### 1.3 Geometry shader

- Metal has no GS stage. DXMT emulates a VS+GS pair as a **mesh render pipeline**: the VS becomes the object
  function, the GS the mesh function (`d3d11_pipeline_gs.cpp:99-107,134`) [src].
- The GS variant depends on the VS that feeds it (its SHA-1) and on whether the topology is a strip
  (`d3d11_shader.cpp:415-422`; struct at `d3d11_shader.hpp:157-165`) [src]. The VS-side variant also needs
  the input layout and the index-buffer format (`d3d11_shader.hpp:143-155`) [src].
- So a GS cannot be finished per shader. It is finished per **(VS, GS, layout, index format, strip/list)**.
  The input primitive type itself is declared in the GS bytecode.
- DXVK patches the GS input topology when the IA topology disagrees (`dxvk_graphics.cpp:928-933`,
  `:1271-1279`) [src].
- The game has GS without stream output [proj: 31-d3d11-usage.md:18-19]. How many GS it has, and how many VS
  feed each one: not settled.

#### 1.4 Compute shader

- The thread-group size is in the bytecode (DXMT reflection `ThreadgroupSize[3]`, `airconv_public.h:108`).
- DXMT's CS variant hashes only the global shader flags (`d3d11_shader.cpp:205-210`). The compute PSO adds only
  "threadgroup size is a multiple of 32" (`d3d11_pipeline.cpp:143-151,168-175`) [src].
- So a CS can be converted fully, and even made into a PSO, ahead of time.
- DXVK compiles a complete compute pipeline when the shader is created: a "shader pipeline library ... for
  either a complete compute pipeline, a pre-rasterization pipeline library consisting of a single vertex
  shader, or a fragment shader pipeline library", where "all state unknown at shader compile time will be
  made dynamic" (`dxvk/src/dxvk/dxvk_shader.h:566-574`) [src].

#### 1.5 Resource binding layout

- **D3D11 slot counts per stage [src: wine/include/d3d11.idl].**
  - 14 constant buffers (`:47`), 128 SRVs (`:66`), 16 samplers (`:71`);
  - 8 PS/CS UAVs (`:195`), or 64 with D3D11.1 (`:285`);
  - 32 IA vertex-buffer slots (`:168`), 8 render targets (`:172`).
- **This is a fixed slot model, so the binding layout can be fixed at conversion time.**
  - DXMT gives each shader a constant-buffer table and an argument table at fixed Metal buffer indices
    (`SM50_BINDING_INDEX_*`, `airconv_public.h:23-37`).
  - Its reflection exports slot masks and the size of the argument table (`airconv_public.h:102-126`).
  - Each argument records its D3D slot and flags (buffer, texture, min-LOD clamp, texture array, UAV counter,
    and so on; `:39-63`) [src].
  - Nothing in a DXMT variant digest depends on bindings.
- **Texture-type questions.**
  - What the bytecode fixes: resource dimension (2D, 2D array, cube, ...), return type, and whether the
    shader samples with comparison. DXMT marks an SRV `compared` when it sees `sample_c`-type instructions
    (`dxbc_instructions.cpp:702,750,790`) and then declares a Metal depth texture type
    (`dxbc_converter.cpp:1100`) [src].
  - A view whose array-ness differs from the declaration is fixed **at bind time**, not by recompiling:
    `checkViewUseArray` (`dxmt/dxmt_context.cpp:356-357,396-397`; `dxmt_texture.cpp:263`) [src].
  - Component swizzles of views: Metal has native texture-view swizzle on current GPUs. MoltenVK falls back
    to shader-side swizzle only without it (`MVKPipeline.mm:2034`, `swizzle_texture_samples`) [src].
- **MoltenVK / SPIRV-Cross binding inputs fixed per pipeline layout:**
  - `add_msl_resource_binding`, argument-buffer tier, discrete descriptor sets;
  - the implicit buffer indices for swizzle, buffer size and dynamic offsets
    (`spirv_msl.hpp:310,315,350,699-718`; `MVKPipeline.mm:1422-1424,2038-2061`) [src].

  For a D3D11 port these are a constant of the converter, not of the draw.

#### 1.6 Per-pipeline inputs that each layer takes (summary table)

| Layer | Per-shader (fixed once) | Per-pipeline inputs that change the generated shader | Per-pipeline inputs that only change the PSO |
|---|---|---|---|
| DXMT [src] | Metal version, flags (`SM50_SHADER_COMMON_DATA`, `airconv_public.h:237-247`) | VS: input layout, GS pass-through, rasterization off. PS: sample mask, dual source, depth output off, UNORM8 RT mask. GS: the VS that feeds it, strip. (`d3d11_shader.cpp:121-422`) | RT formats, DS format, blend state, topology class, sample count, rasterization (`d3d11_pipeline.hpp:10-30`, `d3d11_pipeline.cpp:67-99`) |
| DXVK [src] | DXBC to IR once; options in `DxvkShaderOptions`/flags (`dxvk_shader.h:20-146`; 2.7.1: `DxbcModuleInfo` = options + tess + xfb, `dxvk-2.7.1/src/dxbc/dxbc_modinfo.h:53-57`) | `DxvkShaderLinkage`: dual-source, flat shading, sample locations, input topology, previous-stage outputs, RT swizzles (`dxvk_shader.h:203-219`); sample count as spec constant | vertex-input state, fragment-output state (formats, blend, MSAA), pre-raster, dynamic and spec state (`dxvk_graphics.h:426-470`) |
| MoltenVK + SPIRV-Cross [src] | none: MSL is generated per pipeline config | output mask, sample mask, sample-rate, point size, frag depth/stencil, disable rasterization, index type, swizzle, argument buffers, previous-stage outputs (`MVKPipeline.mm:1469-1668,2031-2110`) | vertex descriptor, color formats/blend/write mask, depth/stencil formats, sample count, A2C (`MVKPipeline.mm:1921-2001`) |
| vkd3d-shader [src] | DXBC parse | `spirv_target_info`: parameters (sample count, alpha test, flat interpolation, clip planes, point size, fog), `dual_source_blending`, `output_swizzles` (`vkd3d_shader.h:716-992,1750-1768`); `interface_info` bindings (`:1437-1463`) | (D3D12 PSO, out of scope) |

### 2. What can be fixed before the game runs vs. at first use

#### 2.1 Per shader, ahead of time (no game state needed)

- Parsing, the control flow and translation of all instructions.
- Resource declarations and the binding layout (section 1.5).
- Texture kinds, including depth-compare from `sample_c`.
- Interpolation modes, thread-group size, and the input/output signatures.
- CS: the whole pipeline (section 1.4).
- A full conversion sweep over every DXBC the game ships, to find untranslatable shaders early. The project
  already noted that DXMT's `airconv` CLI does exactly this generic conversion. It passes only common data,
  with no layout or RT formats, so it cannot fill DXMT's variant cache [proj: 35-dxmt-native.md:309-316].

#### 2.2 Per pipeline (known only when a draw first combines state)

- VS + input layout, if fetch is in the shader (DXMT). With a vertex descriptor the layout and static
  strides move into the PSO instead.
- PS × {RT formats (integer/float type, UNORM8 rounding, component count), dual-source blending, sample
  mask, presence of a depth attachment}.
- GS × feeding VS × layout × index format × strip/list.
- The PSO itself:
  - RT pixel formats and depth/stencil formats;
  - blend state and write masks;
  - sample count and alpha-to-coverage;
  - topology class and rasterization enable.

  These are all [doc] (section 0).

#### 2.3 Two-level approach

- **Level 1: once per shader, ahead of time.** Convert DXBC to AIR in a metallib, with every pipeline
  dependency expressed as a function constant or a generic path:
  - the input-layout description read from a buffer;
  - output type and rounding per RT index as constants;
  - sample mask and dual-source as constants.

  Apple describes specialization as front-end compiled at build time and "the back-end compiler to compile
  the specialized function at runtime, when the pipeline is created"
  (https://developer.apple.com/documentation/metal/using-function-specialization-to-build-pipeline-variants)
  [doc]. A function-constant value set is applied when the specialized `MTLFunction` is created. Later
  changes to the values have no effect on that function
  (https://developer.apple.com/documentation/metal/mtlfunctionconstantvalues) [doc]. MoltenVK maps Vulkan
  specialization constants to `MTLFunctionConstantValues` this way (`MVKShaderModule.mm:125-140`) [src].
- **Level 2: per combination, at first use (or replayed from a recorded list).**
  - Specialize the functions.
  - Call `newRenderPipelineState`; DXMT does this in `d3d11_pipeline.cpp:67-99`.
  - Optionally look up or record a binary archive.
- **Costs.**
  - Function constants do not remove the back-end compile. They only move the front end out of the run
    [doc above].
  - Per-PSO compile time on Apple GPUs: **not settled**. Apple only says drivers "may need time to evaluate
    and build each pipeline state" and recommends creating PSOs "at a noncritical time"
    (https://developer.apple.com/documentation/metal/mtlrenderpipelinestate) [doc].
  - Apple's WWDC22 example spends "roughly 2/3 of its time compiling GPU binaries behind a loading screen"
    (https://developer.apple.com/videos/play/wwdc2022/10102/) [write-up]. No per-PSO milliseconds were found.
- **Binary archives (Metal 3) [doc/pub].**
  - `MTLBinaryArchive` is "a container for pipeline state descriptors and their associated compiled shader
    code". It has `addRenderPipelineFunctions(descriptor:)` and `serialize(to:)`
    (https://developer.apple.com/documentation/metal/mtlbinaryarchive).
  - Pipelines look archives up through the descriptor's `binaryArchives`. `failOnBinaryArchiveMiss` makes a
    miss an error (https://developer.apple.com/documentation/metal/mtlpipelineoption).
  - Offline: harvest at run time, turn the archive into an `.mtlp-json` pipelines script with `metal-source`,
    then build GPU binaries with `metal-tt shaders.metallib descriptors.mtlp-json -o archive.metallib`. Apple
    says "Metal gracefully upgrades your binary archives during OS updates"
    (WWDC22 10102, https://developer.apple.com/videos/play/wwdc2022/10102/) [write-up].
  - So the archive key is the full pipeline descriptor. Producing archives offline needs the list of pipeline
    descriptors, which only exists after the game has run (harvesting) or after it has been reconstructed.
- **Metal 4 flexible ("unspecialized") pipelines [doc/pub].**
  - Create the pipeline once with color `pixelFormat`, `writeMask` and `blendingState` left unspecialized.
    Later, `newRenderPipelineStateBySpecializationWithDescriptor:pipeline:` fills only the unspecialized
    fields. A specialized PSO cannot be specialized again
    (https://developer.apple.com/documentation/metal/mtl4compiler/newrenderpipelinestatebyspecializationwithdescriptor:pipeline:error:;
    macOS 26+).
  - The unspecialized pipeline holds "a vertex binary, the fragment binary body, and the default fragment
    output part". The new fragment output "can be generated very quickly".
  - There is "a small GPU performance overhead" (unused channels are no longer optimized away, plus a jump to
    the output part), and it "can be large in some fragment shaders". Apple advises compiling the important
    pipelines fully in the background (WWDC25 254, https://developer.apple.com/videos/play/wwdc2025/254/)
    [write-up].
  - Harvesting in Metal 4 writes a JSON pipeline script (suffix `mtl4-json`) through
    `MTL4PipelineDataSetSerializer`, then `metal-tt` builds an archive. Archive misses ("no matching pipeline,
    incompatible OS, or incompatible GPU architecture") must be handled by the app (same session) [write-up].
  - Colour-attachment mapping lets one PSO serve encoders with different attachment layouts
    (https://developer.apple.com/documentation/metal/using-the-metal-4-compilation-api.md) [doc].
  - Fit for D3D11: blend state and write masks and RT formats (the biggest PSO multipliers) can be deferred.
    Still in the PSO: vertex descriptor, sample count, A2C state, topology, rasterization.
  - Depth/stencil formats: `MTL4RenderPipelineDescriptor` lists none (section 0). What this means for the
    pipeline is **not settled**; the Apple forum thread
    https://developer.apple.com/forums/thread/793508 asks the same question.
  - Also not settled: whether an unspecialized output can change the fragment output **type** (int vs float)
    or only the conversion and packing.
- **Not applicable: Apple's Metal shader converter** accepts DXIL SM6.0-6.6 only
  (https://developer.apple.com/metal/shader-converter/) [doc]. It does not accept DXBC SM4/5.

#### 2.4 How the open layers cache

- **DXMT [src].**
  - Variant compile: `get_shader` creates a variant on first request and submits it to the worker pool
    (`d3d11_pipeline_cache.cpp:115-125`).
  - Pipelines: one per distinct `MTL_GRAPHICS_PIPELINE_DESC`, also submitted to the pool (`:380-395`). The
    pool has up to 2 × hardware threads (`dxmt/dxmt_tasks.hpp:48-49`).
  - The draw blocks in `GetPipeline` until the pipeline is ready (`d3d11_pipeline.cpp:49-52`; call sites
    "may block", `d3d11_context_impl.cpp:4737,4785,4841,5001`).
  - On disk: sqlite, keyed by (shader SHA-1, variant digest) (`d3d11_pipeline_cache.cpp:147-157`). The table
    name is `cache_<version>` (`winemetal/unix/cache.c:51,131-135`), with version = `AIRCONV_VERSION` 26
    (`airconv_public.h:8`, `dxmt_shader_cache.hpp:8`). One db per Metal version (`dxmt_shader_cache.cpp:21-31`).
  - **Only the AIR variants are cached, not the PSOs**: on a hit DXMT still calls `newLibrary` and
    `newRenderPipelineState` (`d3d11_shader.cpp:48-110`).
  - `winemetal` has binary-archive fields for pipeline creation (`winemetal_unix.c:385-394,567-572`), but
    the d3d11 path initializes them empty (`winemetal/Metal.hpp:859-876`). So DXMT relies on the OS's own
    Metal shader cache for GPU binaries (an inference; Apple does not document that cache).
  - Native-build caveat: the executable name is empty, so the cache path degenerates
    [proj: 35-dxmt-native.md:306-308].
- **DXVK [src/pub].**
  - The state cache (`*.dxvk-cache`) was **removed in DXVK 2.7**. It was "largely unused since the
    introduction of VK_EXT_graphics_pipeline_library in DXVK 2.0"
    (https://github.com/doitsujin/dxvk/releases/tag/v2.7). The master tree has no state-cache source.
  - With graphics pipeline libraries, Vulkan shaders "will be compiled at the time the game loads its D3D
    shaders, rather than at draw time" (`dxvk/README.md:99-100`). `registerShader` queues a per-shader
    pipeline library at create time (`d3d11_shader.cpp:231`; `dxvk_pipemanager.cpp:302-309`).
  - At draw time a base pipeline links the vertex-input and fragment-output libraries
    (`dxvk_graphics.h:402-418`). Optimized "fast" pipelines compile in the background (`dxvk.conf:455-467`).
  - Base linking is refused, forcing a full compile, for: missing VS inputs, a GS topology mismatch,
    dual-source blending, flat shading, some MSAA cases, non-fill polygon mode, and depth clip off without
    EDS3 (`dxvk_graphics.cpp:1240-1340`).
  - Since DXVK 3.0 there is an on-disk cache of DXVK's own **IR** (`*.dxvk.lut` + `*.dxvk.bin`, discarded on
    version change; `dxvk_shader_cache.cpp:189-214,612-665`; env `DXVK_SHADER_CACHE(_PATH)`,
    `README.md:96-98`; release 3.0 notes, https://github.com/doitsujin/dxvk/releases) [src/pub].
  - Driver-level pipeline binaries are left to the Vulkan driver cache (not checked in source).
- **MoltenVK [src].**
  - Shader conversion is cached per (shader module, full conversion config): `MVKShaderLibraryCache::findShaderLibrary`
    matches the config (`MVKShaderModule.mm:344-348`).
  - `VkPipelineCache` serialization writes, per entry, the module key, the conversion config, the result
    info and the **compressed MSL source** (`MVKPipeline.mm:2622-2660`, needs `MVK_USE_CEREAL`). The MSL is
    compiled again with `newLibraryWithSource` on load (`MVKShaderModule.mm:582`).
  - No `MTLBinaryArchive` use anywhere in `MoltenVK/` (grep for `BinaryArchive`: no hits).

### 3. How many combinations to expect

- **In principle.**
  - Pairs: VS × PS = 6,500 × 4,400 ≈ 28.6 M before any state.
  - Each pair is then multiplied by input layouts × RT-format sets × DS formats × blend states × sample
    counts × topology classes × the PS variant bits (sample mask, dual source, depth-output-off, UNORM8 mask).
  - D3D11 caps unique blend, rasterizer, depth-stencil and sampler states at 4,096 per device each
    (`D3D11_REQ_*_OBJECT_COUNT_PER_DEVICE`, `wine/include/d3d11.idl:220-238`). Creating a duplicate returns
    the existing object
    (https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11device-createblendstate and the
    rasterizer, depth-stencil and sampler pages, Remarks) [doc/src].
  - Only blend state is a Metal PSO multiplier. Rasterizer, depth-stencil and sampler state are encoder or
    argument state (section 0).
- **What bounds it in practice.**
  - Pairs that are actually drawn, not the product.
  - In DXMT, VS variants ≈ Σ over VS of (number of layouts used with it). An input layout is tied to an
    identical input signature (Microsoft remark above).
  - The game creates layouts from one call site [proj: 31-d3d11-usage.md:157]. The number of distinct layouts
    is **not settled**.
  - PS variants ≈ PS × (distinct RT-UNORM8 masks × with/without DSV × sample mask × dual source in use).
    DXMT hashes `disable_depth_output` and `unorm_output_reg_mask` even for a PS that writes no depth or that
    would produce identical code (`d3d11_shader.cpp:169-175`). So it can create duplicate variants: one PS
    drawn both with and without a DSV gives two variants (an inference from the digest code).
  - The game creates blend, depth-stencil, rasterizer and sampler states from 1, 1, 2 and 3 call sites
    [proj: 31-d3d11-usage.md:162-165]. How many distinct objects that yields is not settled.
- **Public numbers.**
  - One community DXVK state-cache file for Apex Legends lists "3604 entries"
    (https://github.com/alvesvaren/apex-legends-cache, README) [write-up]. Apex Legends uses a D3D11 engine
    descended from Titanfall's. A state-cache entry recorded one pipeline state vector, but the format could
    not be checked here (the state-cache code is gone from the cloned DXVK).
  - No DXMT issue or write-up with pipeline or variant counts was found.
  - Rough order, **not settled**: thousands of PSOs per long session. Their lower bound is the number of
    distinct (VS, PS) pairs drawn, which is at least ≈ 4,400 if every PS is used. Each needs one back-end
    compile at first use unless it is replayed from a harvested list or binary archive.
- **Arithmetic for planning (DXMT-style key).** Let L = layouts per VS (typically 1 to 2) and P = PS variant
  multiplicity (typically 1 to 2).
  - VS functions: 6,500 × L (6.5k to 13k).
  - PS functions: 4,400 × P (4.4k to 8.8k).
  - Each needs a back-end compile when specialized.
  - PSOs: (drawn pairs) × (RT/DS/blend/sample combos per pair). Not settled without a harvest run.
  - Recommended measurement: log every (VS SHA-1, PS SHA-1, layout hash, RT formats, DS format, blend desc,
    samples, topology) at first use during play. This is exactly the list Metal's pipeline scripts need.

## Part 6. A test plan in words: fifteen shaders in rising difficulty

### 6.0 The oracle and the four checks

**The oracle is the Windows compiler and runtime, recorded once.** On a Windows machine (or a Windows CI
runner), a builder compiles each shader below with Microsoft's `fxc.exe` (the `d3dcompiler_47` of the current
Windows SDK; the game itself shipped `d3dcompiler_46` [proj: 31 §7]) and stores, per shader:

1. the bytecode (`/Fo`) and the assembly listing (`/Fc`), which is the instruction-level truth for part 2;
2. a JSON dump of everything `D3DReflect` returns (every field of part 3's tables, every method, including the
   out-of-range and wrong-name calls) and of `D3DGetBlobPart` for parts 0-12 and `D3DStripShader` with each
   flag, with sizes and HRESULTs;
3. for the shaders that compute something, the output of a run on Microsoft's WARP software rasteriser
   (`D3D_DRIVER_TYPE_WARP`) with fixed inputs: buffers read back as raw bytes, render targets read back per
   texel. WARP is Microsoft's reference-quality implementation of the functional specification; a hardware
   driver's result is a second opinion, not the oracle.

These files are small, contain no game data, and are committed as test data. Wine's conformance tests already
hold free oracles of this kind: blobs compiled by `fxc.exe` together with the values Windows returned for them
(`wine/dlls/d3dcompiler_43/tests/reflection.c`: vs_4_0, vs_4_1, ps_4_1, ps_4_0 without RDEF, cs_4_0, cs_5_1,
ps_5_0 with an interface, ps_5_1; `tests/blob.c`: vs_4_0_level_9_0, hs_5_0 /Zi). Their *asserted values* may be
used as expected results; their code is not copied [test].

**The four checks applied to each shader.**
- **C (container):** our reader finds every chunk; our checksum (part 1, 1.2) reproduces the 16 bytes `fxc`
  wrote; our `D3DGetBlobPart`/`D3DStripShader` produce byte-identical containers to Windows' (part 3, 3.9-3.10).
- **R (reflection):** our `D3DReflect` returns field-for-field what the JSON says, including HRESULTs and the
  dummy-object behaviour (part 3, 3.6).
- **T (translation):** the converter accepts the shader, or refuses it with a named reason; a disassembly of its
  output contains no "unhandled" placeholder (airconv release builds turn some gaps into silent no-ops: part 4,
  §3.6, so this check reads the output, not only the return code).
- **E (execution):** the converted shader, run on Metal with the same inputs, gives WARP's bytes. Exact equality
  is required for integer, bit, move, load and store results; for floating point the tolerance is the one the
  specification allows for that operation (part 2, §4.7), never a looser one.

Each shader below names what it isolates, the checks that apply, and the edge values it must include.

### 6.1 The fifteen shaders

1. **Pass-through vertex shader, vs_4_0.** One `float4 POSITION` input copied to `SV_Position`. Isolates: the
   container header and checksum, ISGN/OSGN with 24-byte elements, a 29-word STAT, RDEF of a 4.x target with no
   bindings, `Version` 0x00010040. Checks C, R (InputParameters 1, OutputParameters 1, InstructionCount 2, Creator
   string, Flags), T, E (one triangle, positions read back from a stream of vertices placed with the vertex
   descriptor at three input formats: R32G32B32A32_FLOAT, R16G16B16A16_FLOAT, R8G8B8A8_UNORM). Also the first
   `D3DGetBlobPart(part 0)` test, which is the one call the game makes [proj: 31 §7].

2. **Constant-colour pixel shader, ps_4_0 and ps_5_0 of the same source.** Writes one cbuffer `float4` to
   `SV_Target0`. Isolates: RDEF cbuffer and variable records in both the 4.x layout and the 5.0 layout with the
   RD11 header (part 1, 1.6), PS outputs stored with SystemValue 0 yet reported as `D3D_NAME_TARGET` (1.4), SHDR
   versus SHEX. Checks C, R (ConstantBuffers 1, the variable's StartOffset/Size/uFlags USED, StartTexture 0xFFFFFFFF),
   T, E with three render-target formats (R8G8B8A8_UNORM, R16G16B16A16_FLOAT, R32G32B32A32_UINT with `asuint`
   output): the UNORM case must round as D3D does (DXMT subtracts a small constant for this: part 5, 1.2).

3. **Transform vertex shader with packing, vs_5_0.** A cbuffer with a `row_major float4x4`, a `column_major
   float3x3`, a `float3` followed by a `float` that packs into its .w, an array of `float2` (16-byte stride), a
   nested struct, and a `packoffset`. Isolates: variable/type records (class MATRIX_ROWS/COLUMNS, Elements,
   Members, member offsets, type names, `IsEqual` by identity: part 3, 3.6), `dp4` and `mad`. Checks C, R (every
   type and member, `GetMemberTypeName` out of range, `GetVariableByName` with an unknown name returning a dummy
   whose `GetDesc` fails), T, E (fixed matrices, positions compared within 0.5 ULP per multiply and the mad rule of
   part 2, §4.8).

4. **Textured pixel shader, ps_5_0.** `Texture2D` + `SamplerState` with `Sample`, `SampleLevel`, `SampleBias`,
   `SampleGrad`, a `Texture2DArray` with `Load` and `_aoffimmi` offsets (−8 and 7), and a `Texture2D<float>` with a
   `SamplerComparisonState` (`sample_c`, `sample_c_lz`). Isolates: binding records (types, return types,
   dimensions, NumSamples 0xFFFFFFFF, D3D_SIF_COMPARISON_SAMPLER, texture-component flags), the depth-texture type
   chosen from `sample_c` use (part 5, 1.5), `ld` out of bounds returning 0 (part 2, §4.5). Checks C, R, T, E
   (a 4x4 mipmapped texture with known texels; out-of-bounds `Load` at mip 5 and slice 9 must give 0).

5. **Discard and derivatives, ps_5_0.** `clip()` on a computed value, then `ddx`/`ddy`, `ddx_coarse`/`ddx_fine`
   and a `Sample` (implicit derivatives) used by the surviving pixels; `SV_IsFrontFace` input. Isolates: discard as
   *demote* (the discarded pixel keeps running so its neighbours' derivatives stay valid: part 2, §4.9), coarse
   versus fine (all three Metal back ends emit the same `dfdx`: part 2, §3.5, a known gap), front-face winding.
   Checks T, E (a quad whose left half discards; the right half's derivative values and sampled mip must equal
   WARP's; fine derivatives must differ per pixel where WARP's do).

6. **Integer and bit operations, cs_5_0.** One thread per test vector, results to a `RWByteAddressBuffer`:
   `<<`, `>>` (signed and unsigned) with counts 0, 31, 32, 33 and 0xFFFFFFFF, both immediate and from a buffer;
   `udiv`/`urem` by 0; `imul`/`umul` high and low halves; `ibfe`/`ubfe`/`bfi` with width 0, offset 31, width+offset
   above 32; `bfrev`, `countbits`, `firstbithigh`/`firstbitlow` of 0, 1, 0x10000000 and 0x80000000 (signed);
   `uaddc`/`usubb` through `AddUint64`-style code; `msad4`. Isolates: the shift masking that vkd3d-MSL, HLSLcc and
   old DXVK (immediate counts) do not do (part 2, §4.2), udiv by zero = 0xFFFFFFFF (§4.4), firstbit_hi counted
   from the MSB (the documentation page's example is wrong: §3.2). Checks R (`GetThreadGroupSize`,
   `cInterlockedInstructions` 0, GetBitwiseInstructionCount), T, E with **exact** equality.

7. **Floating-point edge values, cs_5_0.** `min`/`max` with one and two NaN operands and with −0/+0; `saturate`
   of NaN; `ftoi`/`ftou` of NaN, ±INF, 2^31, −2^31−1, 4294967296.0 and −0.5; `rcp(0)`, `rsqrt(−1)`, `rsqrt(−0)`,
   `sqrt(−0)`, `log(0)`, `log(−1)`; a denormal input through `mov`, `movc`, `asfloat` loads (bits kept) and through
   `mul` by 1.0 (flushed); `f32tof16` of 65520.0 (D3D rounds to the largest finite half, not INF) and of a half
   denormal; `exp`, `log`, `sincos` and `rcp` on a sweep of 256 values. Isolates: every rule of part 2, §4.1, §4.3,
   §4.6, §4.7. Checks T, E: exact for min/max/sat/conversions/moves, the ULP and 2^-21 bounds for the rest. This is
   the shader most likely to fail on every route; Metal's own NaN, saturate and conversion rules are not settled in
   this document (see "What was not reached").

8. **Out-of-bounds access, cs_5_0.** Reads and writes past the end of a typed `Buffer`, a `ByteAddressBuffer`, a
   `StructuredBuffer` (index past the end, and a byte offset past the struct), a cbuffer indexed dynamically past
   its declared and its bound size, an immediate constant buffer (`static const` array indexed dynamically), and an
   indexable temp (`x#`) indexed past its length. Isolates: part 2, §4.5, rule by rule: reads that must give 0,
   writes that must be dropped, and the cases the specification leaves undefined (structured byte-offset faults,
   `x#`), which the test records but does not compare. DXMT's null-pointer select for raw/structured access and its
   unchecked `x#` access (part 2, §4.5) are what this shader probes. Checks T, E (exact on the defined cases).

9. **Control flow and dynamic indexing, ps_5_0.** A `[loop]` with `break` and `continue`, a `switch` with a
   default and a case shared by two labels, a nested `if` eight deep, an early `return`, an immediate constant
   buffer indexed by a pixel's coordinate, and an output selected by dynamic index into an indexable temp.
   Isolates: structuring of the token stream's control flow (part 2, §3.4), `icb` declaration through `customdata`
   (§2), `dcl_indexableTemp`. Checks R (StaticFlowControlCount, DynamicFlowControlCount, TempArrayCount,
   ArrayInstructionCount against the JSON), T, E (a 16x16 target, exact).

10. **Resource queries and gathers, ps_5_0.** `GetDimensions` on a `Texture2DArray` with mips (float, uint and
    `rcpFloat` forms, at a valid and an invalid mip), on a `TextureCube` and a typed `Buffer` (element count) and a
    `ByteAddressBuffer` (byte count: `bufinfo`); `Texture2DMS.Load` with a sample index and `GetSamplePosition`
    (`samplepos`), `GetRenderTargetSampleCount` (`sampleinfo`), `CalculateLevelOfDetail` (`lod`); `Gather`,
    `GatherCmp`, `Gather` with a programmable offset (`gather4_po`, offsets −32 and 31). Isolates: part 2, §3.6.
    Checks R, T, E (exact for queries; gather results against WARP's texel choice).

11. **Unordered access and atomics, cs_5_0.** `groupshared` array with `GroupMemoryBarrierWithGroupSync`,
    `InterlockedAdd`/`InterlockedMax`/`InterlockedCompareExchange` on groupshared memory and on a
    `RWByteAddressBuffer`, `RWStructuredBuffer` with `IncrementCounter`, an `AppendStructuredBuffer` and a
    `ConsumeStructuredBuffer`, a `RWTexture2D<float>` typed load and store (R32_FLOAT only, the format every
    feature-level-11 device must load), `[numthreads(8,8,1)]` dispatched as 4x4 groups. Isolates: part 2, §3.7 (sync
    flags, counters, `imm_atomic_consume` returning the new value), binding records of type 4-11 (part 1, 1.6.2).
    Checks R (cBarrierInstructions, cInterlockedInstructions, cTextureStoreInstructions, binding types), T, E
    (final buffer contents exact; append order excluded from the comparison, the multiset compared instead).

12. **Pixel shader with the rare outputs and inputs, ps_5_0.** Three render targets of different types (UNORM,
    FLOAT, SINT), `SV_Depth` and in a second variant `SV_DepthGreaterEqual`, `SV_Coverage` output,
    `SV_SampleIndex` input (sample-rate shading), inputs with `nointerpolation`, `centroid`, `sample` and
    `noperspective`, and `EvaluateAttributeAtSample`/`AtCentroid`/`Snapped`. Isolates: signature elements with
    register 0xFFFFFFFF, mixed component types, interpolation modes in `dcl_input_ps` (part 2, §2), Metal's
    `[[depth(greater)]]`, `[[sample_mask]]`, `[[sample_id]]` (part 4, §1), the per-pipeline inputs "depth output off"
    and output types (part 5, 1.2). Checks C, R (ReadWriteMask raw bytes, SystemValueType derived from names,
    `IsSampleFrequencyShader` TRUE), T, E at 4x MSAA.

13. **Dual-source blending and sample mask, ps_5_0.** Outputs `SV_Target0` and `SV_Target1` drawn with a blend
    state using `D3D11_BLEND_SRC1_COLOR`, then the same shader with ordinary blending, then with a sample mask
    0x5 and alpha-to-coverage. Isolates: the pipeline-dependent variants of a pixel shader (part 5, 1.2, 1.6):
    the same bytecode must convert to `[[color(0), index(1)]]` in one pipeline and to two colour attachments in the
    other, and the sample mask must be folded into the shader because Metal has no pipeline sample mask (part 5, 0).
    Checks T for each variant, E (blended results exact for UNORM targets).

14. **Geometry shaders, gs_4_0 and gs_5_0.** (a) Point-to-quad expansion: `[maxvertexcount(4)]`,
    `TriangleStream`, `point` input, writing `SV_RenderTargetArrayIndex`. (b) The same with `[instance(2)]`
    (GS instancing, `SV_GSInstanceID`). (c) A triangle-adjacency input passing through with `RestartStrip`.
    (d) A two-stream GS (`emit_stream`/`cut_stream` on stream 1, OSG5 stream indices) with no stream output bound.
    Isolates: STAT words 23-25 and 29 (InputPrimitive, GSOutputTopology, GSMaxOutputVertexCount,
    cGSInstanceCount) versus the declarations (part 3, 3.3, which notes vkd3d reads only STAT), `GetGSInputPrimitive`,
    and the route's GS emulation (part 4, §3.3: DXMT converts only VS+GS pairs, as object+mesh functions, Apple7+;
    stream > 0 is an `assert` there). Checks C, R, T (each paired with the vertex shader of test 1), E (rasterised
    output against WARP's; for (d), stream 0's output only, and T must report stream 1 rather than drop it).

15. **What the game is not known to use, and must be refused cleanly.** (a) A hull/domain pair: quad domain,
    `fractional_odd` partitioning, `triangle_cw` output, 4 control points, max tessellation factor 64.
    (b) A `ps_5_0` using an interface and two classes (`fcall`, IFCE chunk, `this` pointer cbuffer).
    (c) A `ps_5_0` with `double` arithmetic (`dadd`, `dmul`, `dtof`: SFI0 bit 0x1 and the
    ENABLE_DOUBLE_PRECISION_FLOAT_OPS global flag). Isolates: STAT words 30-33 and the PCSG chunk, IFCE and
    `GetNumInterfaceSlots`, SFI0 and `GetRequiresFlags`, `D3DGetBlobPart` parts 3 and 4 (part 3, 3.9).
    Checks C, R (these are where Wine stubs and the part 3 NOT SETTLED rows sit: `GetMinFeatureLevel`,
    `GetRequiresFlags`, `GetNumInterfaceSlots`, the four extra RDEF type words; the Windows JSON settles them),
    T: the expected result for (b) and (c) on routes 1-3 is a **named refusal**, never a silent output (airconv
    release builds drop double opcodes and interfaces without an error: part 4, §3.6); (a) converts on DXMT and
    route 1, and is executed only if a later phase needs tessellation.

### 6.2 Order of use

Tests 1-4 and 6 are the gate for the first conversion run over the game's shaders: they cover every chunk the
game's shaders certainly carry and the instructions whose semantics differ most between converters. Tests 7, 8
and 13 decide which route's arithmetic and pipeline model are acceptable. Test 14 must pass before any of the
game's geometry shaders is used. Test 15 is run once per route to prove that refusals are loud. A sweep over the
game's own shaders (part 4, §8: double-precision flags and opcodes, SFI0, `dcl_stream`, `fcall`, cull
distances) says which of tests 12, 14 and 15 are needed at all.

## What was not reached, and what stayed unsettled

**Not reached.**
- **Hosts:**
  - learn.microsoft.com and microsoft.github.io: read through their published GitHub sources instead (see "How to
    read").
  - gitlab.winehq.org (upstream vkd3d): the copy bundled in Wine was read instead.
  - Mesa: unreachable, so its D3D10 user-mode driver's DXBC reader was not read.
  - llvm.org (the LLVM DXContainer description).
- **Behind logins or not public:**
  - Apple's Metal shader converter manual and licence terms.
  - Apple API reference prose for `MTLTessellationPartitionMode` and `depthClipMode` (only the PDFs and code that
    encodes the enums were read).
- **Repositories and code not reviewed:**
  - 3Dmigoto's decompiler licence and quality; CMD_Decompiler (no repository found).
  - KosmicKrisp and other newer Vulkan-on-Metal layers.
  - DXMT's GS and tessellation emulation in detail (§3.3 of part 4 describes its structure only).
  - DXVK's API-side robustness setup.
  - HLSLcc's bounds handling.
  - The full arithmetic passes of dxbc-spirv.
  - DXVK 3's adapter behaviour on MoltenVK without geometry shaders.
  - Whether `dxilconv` still builds, and whether it can be built outside Windows.
- **Metal language rules:**
  - The MSL specification was read for the feature mapping (part 4, §1).
  - It was not read for numerics. So these remain open: Metal's `min`/`fmin` with NaN, `saturate(NaN)`,
    float-to-integer saturation, coarse versus fine derivatives, `discard_fragment` as demote, denormals, and the
    precision of `rsqrt`/`exp2`/`log2`/`sin`/`cos`. In part 2 these are described by what the converters
    *emit*, not by what Metal guarantees.
- **fxc's own output patterns:** whether it emits 64-bit indices, instructions longer than 31 dwords, shift
  immediates of 32 or more (vkd3d's comment says it does), or signed-division sequences.
- **Numbers:**
  - No measured Metal pipeline compile times.
  - No pipeline or variant counts for this game or any Titanfall build in DXVK or DXMT.
  - Only one public DXVK state-cache figure (3,604 entries for Apex Legends), whose entry format could not be
    checked.

**Unsettled, with what would settle it** (each is marked where it occurs).
- **Part 1:**
  - The four extra SM5 RDEF type words and RD11's last word (a Windows `D3DReflect` dump of a shader with
    interfaces and nested structs: test 15).
  - The IFCE class-instance record and header words 0x1C/0x20 (an `fxc` blob with class instances).
  - The Aon9/XNAP/XNAS headers; the SPDB and PRIV layouts.
  - The DXBC tags behind `D3D_BLOB_PDB`, `PRIVATE_DATA`, `ROOT_SIGNATURE` and `DEBUG_NAME`.
  - SFI0 bit 0x2: Microsoft's blob header and the API constant disagree.
  - The origin of vkd3d's accepted 28-word STAT.
  - Settled during assembly: dxbc-spirv's container *writer* uses a plain MD5 (`dxbc-spirv/util/util_md5.cpp`
    pads with 0x80 the standard way; `dxbc_container.cpp:313-323` applies it to the whole file), while its
    *validator* uses the DXBC variant. Containers written by dxbc-spirv would therefore fail a DXBC checksum
    check. That does not matter to us; it is noted so nobody copies that path.
- **Part 2** (all settled by test 7 and 8 runs on WARP and on Metal):
  - Metal's numerics, listed above.
  - Whether vkd3d-MSL's `!=` is unordered.
  - Metal's support for rasterizer-ordered access.
  - What a null-pointer load does on Metal (DXMT's out-of-bounds path).
- **Part 3:**
  - How Windows computes `GetMinFeatureLevel`, `GetRequiresFlags` and `GetNumInterfaceSlots`.
  - Whether Windows takes GS/HS/DS descriptor values from STAT only.
  - Windows' `D3DStripShader` treatment of SPDB, IFCE, SFI0, PRIV and RTS0.
  - `D3DGetBlobPart` on ISG1/OSG1/PSG1 shaders.
  - Reflection of a container with no SHDR/SHEX.

  All are settled by the Windows dumps of part 6. None of them affects the two calls the game makes.
- **Part 4:**
  - Whether Apple's Metal shader converter accepts `dxilconv` output, how it lowers doubles, its geometry and
    tessellation limits, and its redistribution terms (a one-day spike on a Mac with the converter installed).
  - Whether every Apple7/Apple8 Mac reports BC texture support.
  - Whether DXVK refuses or degrades on MoltenVK.
- **Part 5:**
  - The number of distinct input layouts, blend states and (VS, PS) pairs the game actually draws, which only a
    logged play session settles.
  - Whether a Metal 4 unspecialised pipeline can change a fragment output's type, and how it treats depth and
    stencil formats.
  - Per-pipeline compile cost on a test Mac.
- **The game's own shaders:** whether they use doubles, cull distances, GS streams other than 0, or interfaces.
  A bytecode scan once the `.vcs` container is read (an earlier analysis left that format unread) settles this, and
  decides how much of route 1's and route 2's gaps matter.
