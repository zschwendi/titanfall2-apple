# 35: DXMT's native build compiles unmodified, creates a device and draws off screen

Built and run 2026-10-08 on this Mac (Apple M3 Pro, macOS 27.0.1, Xcode 27.0 with Apple clang 21.0.0, meson 1.12.1,
ninja 1.13.2, Homebrew `llvm@15` 15.0.7, Metal Toolchain "Apple metal version 32023.921"). Subject: DXMT commit
`e94c312f5c054263acf261cfa109edf13e757587`, the clone in `third_party/dxmt`, build directory
`third_party/dxmt/build-native`. Follows `docs/findings/35-dxmt-native.md` (the earlier survey, which could not build).
Tags: **[V: ran]** executed on this Mac and the output is quoted, **[V: read]** source read, **[N]** inferred, **[?]** not
determined. Paths in `third_party/dxmt/` are written relative to it.

## Summary

1. Built unmodified: yes. `meson setup` + `meson compile` with `-Dnative_llvm_path=/opt/homebrew/opt/llvm@15`: 154 steps, 0 errors, 55 s (108 s from an empty directory through `build.sh`); the earlier survey's worry about LLVM 15 headers against Xcode 27's libc++ did not occur. No patch, no `tf2` branch, nothing fetched.
2. Output: arm64 dylibs `winemetal` 27.4 MB (LLVM 15 linked in, 16,197 exported symbols), `dxgi` 330 KB, `d3d11` 3.0 MB, `d3d10core` 39 KB, and the `airconv` tool; all `minos 27.0`; install names `@rpath/<name>.dylib`; build-tree rpaths, so they load from the build tree and a copy in one directory needs `DYLD_LIBRARY_PATH`.
3. Device: `D3D11CreateDevice` returns S_OK; adapter "Apple M3 Pro" (vendor 0x106b, 6.66 GiB "dedicated"), feature level 11_1 when the caller's list names it, **11_0 when the list is null** (the earlier survey expected 11_1), no outputs (`EnumOutputs` = DXGI_ERROR_NOT_FOUND), no window needed.
4. Answers: BC1 to BC7 sampleable (no RTV/UAV), D24_UNORM_S8_UINT yes, MSAA 1/2/4 only (Metal itself says no 8x here), compute shaders yes, doubles no, tiled resources 0, UAV typed loads yes; a hand-assembled `cs_5_0` wrote 0x1234abcd into a raw UAV buffer and the CPU read it back.
5. Triangle: rendered and read back. 256x256 RGBA8 target, 3-vertex buffer, VS and PS given as DXBC bytes, clear colour (64,128,191), triangle (255,204,0): exactly 8192 pixels triangle-coloured (the geometric area), 0 stray pixels, 16 inside/outside probes pass; second draw with red/green/blue vertices interpolates correctly (centroid 84,85,86).
6. The shader bytes are **hand-assembled DXBC, not compiler output** (no HLSL compiler here; the script that writes them is `tools/dxmt/make_dxbc.py`); compiler-produced blobs exist inside CrossOver's Wine and wine-mono but none was used (section 4).
7. Timing, first draw to readable pixels: 102 ms for a pixel shader Metal has never compiled, 14.6 ms when Metal's own cache knows it, 13.1 ms when DXMT's sqlite cache also does, 15.8 ms with DXMT's cache off; a draw on an already built pipeline 2.3 ms. DXMT's own DXBC-to-AIR step is about 1 to 2 ms for these tiny shaders; the Metal compile dominates.
8. Native mode, observed: the log at device creation shows four kinds of stubbed Win32 call (D3DKMT adapter open, `GetCurrentThread`, `SetThreadPriority` (these two four times), `GetModuleHandle`) and no missing Apple API; the empty executable name sends the shader cache to `.../C/dxmt/shaders_320.db` and the log to `./_dxgi.log`; `DXMT_CONFIG` works. A swap chain with any non-null `HWND` aborts the process (SIGABRT) exactly as the earlier survey read.
9. The earlier survey's window plan confirmed (three files, same functions); two corrections: default feature level (above), and the swap-chain constructor also calls three more Win32 stubs before it aborts (`GetWindowThreadProcessId`, `GetCurrentProcess`, `GetProcessId`).
10. `sh tools/dxmt/build.sh`, `sh tools/dxmt/probe.sh`, `make test` all exit 0 on the committed worktree; not determined: realistic-shader compile cost, anything that needs RDEF/constant buffers/textures, debug-build (assert) behaviour, the window path (a later study).

## 1. Commands, exactly

Everything below ran in `third_party/dxmt` unless a path says otherwise. Nothing was installed or downloaded. DXMT declares
no meson wraps (`find . -name '*.wrap'` finds none, there is no `subprojects/`) and its two submodules are `external/nvapi`
(only for `-Denable_nvapi`, not fetched) and `include/native/directx` (already fetched by the earlier survey, see section 7).

    # configure (tools/dxmt/build.sh runs this when build-native/build.ninja is missing)
    meson setup build-native -Dnative_llvm_path=/opt/homebrew/opt/llvm@15 --buildtype release
    # build
    meson compile -C build-native            # 154 steps, "real 54.8 s" (176 s user), exit 0

`--buildtype release` is what upstream's CI native job uses (`.github/workflows/ci.yml:611-640`: `meson setup
-Dnative_llvm_path=... build --buildtype release --prefix ... --strip`, then `meson install`). Nothing here runs `meson install`: a dry run (`meson install --dry-run
--destdir /tmp/x`) shows it would copy only `dxgi.dylib`, `d3d11.dylib` and `d3d10core.dylib`, into `<prefix>` itself (`/opt/homebrew` by default), and not
`winemetal.dylib`. [V: ran]

From an empty directory, through the script (a second, independent build in a scratch directory):

    DXMT_BUILD_DIR=<scratch>/bn2 sh tools/dxmt/build.sh      # exit 0, 1 min 48 s wall; d3d11.dylib is byte-identical to the first build
    sh tools/dxmt/build.sh                                   # incremental, 2.7 s

Warnings, from the build log (197 lines, none an error): 129 `-Wreturn-type` ("non-void function does not return a value
in all control paths"), all in airconv headers and sources (`air_signature.hpp`, `air_type.hpp`, `dxbc_converter.hpp`,
`dxbc_instructions.cpp`) plus one in `dxgi_adapter.cpp:229`. They are `assert(0 && "...")` default branches that compile to
nothing in a release build (`b_ndebug=if-release`): an unexpected value reaching one is undefined behaviour rather than an
error. A debug build would assert; not tried [?]. Other warnings: 12 `-Wsometimes-uninitialized`, 3 `-Wmismatched-tags`
(`ID3D11Buffer`), 3 deprecation notices for `MTLStorageModeManaged`/`didModifyRange:` ("no effect on Apple Silicon", macOS 27),
2 linker notes that `libunwind.1.dylib` is linked directly, 3 "search path '-Wl,-dead_strip_dylibs' not found".

## 2. What came out [V: ran: `file`, `lipo -archs`, `otool -D/-L/-l`, `nm -gU`]

All in `build-native/src/`. Architecture arm64 for every file, `minos 27.0`, `sdk 27.0` (the host SDK's default: these
dylibs load on macOS 27 and later only unless `MACOSX_DEPLOYMENT_TARGET` is set at configure time **[?: not tried]**), ad-hoc
linker-signed, not stripped.

| File | Bytes | Install name | Direct dependencies (`otool -L`) | rpaths |
|---|---|---|---|---|
| `nativemetal/winemetal.dylib` | 27,398,512 | `@rpath/winemetal.dylib` | system: Metal (weak), MetalFX (strong), Foundation, CoreFoundation, CoreGraphics, ColorSync, AppKit (weak), libsqlite3, libz, libncurses.5.4, libc++, libobjc, libSystem; LLVM 15, zstd and libunwind are static inside | `@loader_path/`, `@loader_path/../../` |
| `dxgi/dxgi.dylib` | 330,232 | `@rpath/dxgi.dylib` | `@rpath/winemetal.dylib`, libc++, libSystem | `@loader_path/../nativemetal` |
| `d3d11/d3d11.dylib` | 3,026,696 | `@rpath/d3d11.dylib` | `@rpath/dxgi.dylib`, `@rpath/winemetal.dylib`, libc++, libSystem | `@loader_path/../dxgi`, `@loader_path/../nativemetal` |
| `d3d10/d3d10core.dylib` | 38,720 | `@rpath/d3d10core.dylib` | `@rpath/d3d11.dylib`, `@rpath/dxgi.dylib`, libc++, libSystem | `../d3d11`, `../dxgi`, `../nativemetal` (all `@loader_path`) |
| `airconv/darwin/airconv` | 27,292,632 | (executable) | libz, libncurses.5.4, libc++, libSystem | none |

(QuartzCore, listed in the build file as a weak framework, is not in the load commands; DXMT reaches `CAMetalLayer` through
the Objective-C runtime.) Exported symbols relevant to us (`nm -gU`):

    dxgi.dylib       _CreateDXGIFactory  _CreateDXGIFactory1  _CreateDXGIFactory2  _DXGIDeclareAdapterRemovalSupport  _DXGIGetDebugInterface1
    d3d11.dylib      _D3D11CoreCreateDevice  _D3D11CreateDevice  _D3D11CreateDeviceAndSwapChain  _D3D11On12CreateDevice
    d3d10core.dylib  _D3D10CoreCreateDevice  _D3D10CoreRegisterLayers
    winemetal.dylib  _SM50Initialize _SM50Compile _SM50CompileTessellationPipelineHull/Domain _SM50CompileGeometryPipelineVertex/Geometry
                     _SM50GetCompiledBitcode _SM50GetArgumentsInfo _SM50Destroy ... (12 `_SM50*`), _CreateMetalViewFromHWND (the Wine hook, section 6)

There is no symbol hiding: `dxgi` exports 120 symbols, `d3d11` 655 (575 of them C++ `_ZN...`), `d3d10core` 3, and
`winemetal.dylib` **16,197, among them 10,436 `llvm::` symbols and zstd's `FSE_*`** because LLVM is linked in whole. Any
process that loads it `RTLD_GLOBAL` makes those visible to `dlsym(RTLD_DEFAULT, ...)`; with two-level namespaces a second LLVM in
the same process is not a link conflict, but it is something to keep in mind for the recompiled game's own libraries [N].
`dxmt::Logger::s_instance` is exported too and shared, which is why dxgi and d3d11 write one log file.

Deployment [V: ran]: the three dylibs open fine by absolute path from the build tree (`dlopen` of `dxgi.dylib` and
`d3d11.dylib`, no `DYLD_*`). Copied together into one directory, `dlopen` fails ("Library not loaded: @rpath/winemetal.dylib",
tried `.../../nativemetal/winemetal.dylib`) and works with `DYLD_LIBRARY_PATH=<that directory>`. For the runtime this means one
of: load from the build tree, set an rpath in the host executable, or `install_name_tool -add_rpath @loader_path` on copies
(which invalidates the linker signature: re-sign) [N for the last].

## 3. The device [V: ran: `tools/dxmt/device_probe`, output kept by `probe.sh` in `build-native/tf2-probe/device_probe.out`]

    D3D11CreateDevice(default feature level list)    hr=0x00000000 feature_level=11_0 (0xb000)
    D3D11CreateDevice(list 11_1..10_0, BGRA_SUPPORT) hr=0x00000000 feature_level=11_1 (0xb100)
    device->GetFeatureLevel() = 11_1; GetCreationFlags() = 0x20; GetDeviceRemovedReason() = 0x00000000; context type 0 (immediate)
    adapter: "Apple M3 Pro"   VendorId=0x106b DeviceId=0x0000 SubSysId=0x0 Revision=0
      DedicatedVideoMemory=7151124480 DedicatedSystemMemory=0 SharedSystemMemory=0   IDXGIAdapter1::GetDesc1 Flags=0x0
      sizeof(DXGI_ADAPTER_DESC) in this build = 560 (Windows x64: 304)
      EnumOutputs(0) hr=0x887a0002 (DXGI_ERROR_NOT_FOUND)     EnumAdapters1 count = 1

- **Feature level.** A null list gives 11_0 because DXMT's default list starts at 11_0 (`d3d11.cpp:48-79`); the log says
  "Maximum supported feature level: 11_1 / Using feature level 11_0". The game will pass its own list. `DXMT_CONFIG="d3d11.maxFeatureLevel=11_0"` in the environment
  caps the device at 11_0 (ran: the log then says "Maximum supported ... 11_0"); `=9_1` fails creation with "Minimum required
  feature level 10_0 not supported" (used as the negative test of `probe.sh`).
- **Adapter name encoding, confirmed.** The first bytes of `Description` are `41 00 70 00 70 00 6c 00`: 2-byte UTF-16 units packed from
  the start, in a field the native header types as 4-byte `wchar_t[128]` (read as `wchar_t`, `wcslen` says 6). No byte-order
  mark. The survey's finding stands: a thunk must repack it.
- **CheckFeatureSupport** (all `hr=0`): THREADING concurrent creates yes, command lists yes; DOUBLES no; D3D10_X compute yes;
  D3D11_OPTIONS: OutputMergerLogicOp yes (the non-public selector exists here, no "unrecognized selector"), ClearView yes,
  CopyWithOverlap yes, ConstantBufferPartialUpdate/Offsetting yes, MapNoOverwrite on dynamic constant buffers and buffer SRVs yes,
  SAD4 yes, ExtendedDoubles no, ExtendedResourceSharing yes (although shared resources are stubs); OPTIONS1: TiledResourcesTier 0,
  MinMaxFiltering no, MapOnDefaultBuffers yes; OPTIONS2: ROVsSupported yes, TypedUAVLoadAdditionalFormats yes,
  ConservativeRasterizationTier 0, MapOnDefaultTextures yes, StandardSwizzle yes, UnifiedMemoryArchitecture **no**; OPTIONS3:
  VPAndRTArrayIndexFromAnyShaderFeedingRasterizer yes; ARCHITECTURE_INFO TileBasedDeferredRenderer yes; min precision
  `0x2` for pixel and other stages. (A feature DXMT answers "yes" to is not thereby implemented: the earlier survey listed stubs.)
- **CheckFormatSupport.** BC1, BC2, BC3, BC4 (unorm, snorm), BC5, BC6H (UF16, SF16), BC7, each with and without sRGB where it
  exists: `0x00d213f0` = TEX1D, TEX2D, TEX3D, CUBE, LOAD, SAMPLE, MIP, CPU_LOCK, MS_LOAD, GATHER (no RTV, no UAV, no autogen), all
  `hr=0`. `D24_UNORM_S8_UINT` `0x04f717f0`: TEX1D..CUBE, LOAD, SAMPLE, SAMPLE_CMP, MIP, DSV, CPU_LOCK, MS_RESOLVE, MS_RTV, MS_LOAD,
  GATHER (emulated on a 32-bit depth plus stencil format). `R8G8B8A8_UNORM` `0x02fef3f3` (RTV, BLEND, MS_RESOLVE, MS_RTV, UAV_TYPED,
  DISPLAY, ...), `B8G8R8A8_UNORM` and `R10G10B10A2_UNORM` the same, `R11G11B10_FLOAT`, `R16G16B16A16_FLOAT`, `R32G32B32A32_FLOAT`,
  `R32_FLOAT`, `R8_UNORM`, `R16_UNORM` all render-target, blendable, typed-UAV. 32 formats asked; the full list is in the probe output.
- **MSAA** (`CheckMultisampleQualityLevels`, quality levels for 1/2/4/8/16 samples) for R8G8B8A8, B8G8R8A8, R16G16B16A16_FLOAT, D32_FLOAT,
  D24S8: `1 1 1 0 0`. DXMT answers from `MTLDevice supportsTextureSampleCount:` (`d3d11_device.cpp:827`) and a direct query
  of the Metal device (5 lines of Objective-C, ran) agrees: "Apple M3 Pro: x1=1 x2=1 x4=1 x8=0 x16=0". So 8x MSAA does not
  exist on this GPU through Metal. A game that asks for 8x gets 0 quality levels (and fails or falls back).
- **Compute** [V: ran]. `CreateComputeShader` of a hand-assembled `cs_5_0` with an empty body: S_OK; `Dispatch` and `Flush`
  run. A second one that does `store_raw u0.x, l(0), l(0x1234abcd)` into a 16-byte raw UAV buffer pre-filled with 0xdddddddd:
  after `Dispatch(1,1,1)`, `CopyResource` to a staging buffer and `Map`, element 0 is `0x1234abcd` and element 1 is still
  `0xdddddddd`.

## 4. The off-screen triangle [V: ran: `tools/dxmt/offscreen_triangle`]

What the program does: `D3D11CreateDevice` (list 11_1, 11_0, so 11_1); a 256x256 `R8G8B8A8_UNORM` render-target texture and view; a
staging texture with CPU read access; a default-usage vertex buffer (3 vertices of `float4 position, float4 colour`);
`CreateVertexShader`, `CreatePixelShader`, `CreateInputLayout` (`POSITION`, `COLOR`) from byte arrays; viewport 256x256; the default
rasterizer state (back-face culling, clockwise is front); `ClearRenderTargetView`; `Draw(3, 0)`; `CopyResource` into the staging
texture; `Map(D3D11_MAP_READ)`; checks on the mapped pixels. Nothing is compiled at build time: DXMT turns the DXBC into AIR
itself, and Metal builds the pipeline.

Run A: clear (0.25, 0.5, 0.75, 1.0) = (64, 128, 191, 255); triangle (-0.5,-0.5), (0,0.5), (0.5,-0.5), solid (1.0, 0.8, 0.0, 1.0) =
(255, 204, 0, 255). Checked with a tolerance of 1 per channel:

- 6 points inside the triangle (e.g. (128,150), (128,80), (80,185)): triangle colour; 10 points outside (the four corners,
  above the apex, below the base, just outside both slanted edges): clear colour;
- over all 65,536 pixels: **8,192 triangle-coloured** (the triangle's area is 128 x 128 / 2 = 8,192, and the test allows 1.5%),
  57,344 clear-coloured, **0 of any other colour** (so no blended or half-covered edge pixels, as for vertices on pixel corners);
- coarse picture printed by the program (every 8th row, every 4th column), the triangle is the right way up and the right size.

Run B (same shaders, vertex colours red at bottom left, green at top, blue at bottom right): centroid pixel (84, 85, 86) against the
expected 85 each; near the three corners (233,11,11), (7,238,9), (9,11,235); the three channels sum to 255 +- 6 at an off-centre
point; clear colour outside. Converting the readback to PNG and looking at it shows the expected smooth gradient triangle on a steel-blue
background. Both runs pass, in both of `probe.sh`'s executions (empty shader cache, then the filled one).

**The shader bytes.** There is no HLSL compiler on this Mac, so `tools/dxmt/make_dxbc.py` writes the programs token by token and
wraps them in a DXBC container (chunks `ISGN`, `OSGN`, `SHEX`; the container checksum is computed). Token values come from DXMT's copy of
Microsoft's `d3d12tokenizedprogramformat.hpp` (opcodes printed by a short C++ program: `mov` 54, `ret` 62, `dcl_input` 95, `dcl_output` 101,
`dcl_output_siv` 103, `dcl_thread_group` 155, `dcl_uav_raw` 157, `store_raw` 166, ...). The programs, in HLSL terms:

    vs_5_0   float4 main(float4 pos : POSITION, float4 col : COLOR, out float4 ocol : COLOR) : SV_Position { ocol = col; return pos; }
    ps_5_0   float4 main(float4 pos : SV_Position, float4 col : COLOR) : SV_Target { return col; }
    cs_5_0   [numthreads(1,1,1)] void main() { u0.Store(0, 0x1234abcd); }   (RWByteAddressBuffer u0), and one with an empty body
    ps_5_0   the same pixel shader plus `float4(0,0,0,SALT)` on the colour, SALT patched at run time (timing only, section 8)

What this does and does not show. DXMT's `SM50Initialize` accepted these containers as it would compiler output and the
results are right, so the encoding of declarations, the `mov` family, `ret`, `store_raw` and immediate operands is correct.
Not shown: anything that depends on an `RDEF` chunk (DXMT does not parse it: no reference to it in `src/airconv` or
`src/d3d11`, [V: read]), constant buffers, textures and samplers, `dcl_input_ps_siv`, control flow, and everything else a real
renderer's shaders use. The checksum code reproduces the stored checksum of two fxc-produced containers (604 and 596 bytes, read
only for this comparison; nothing was copied), but only for tails shorter than 56 bytes.

Where compiler-produced DXBC exists, and why it was not used. Looked for with a small script that finds `DXBC` containers in a
file (`FourCC`, version 1, plausible size; scratch, not committed): DXMT's own `tests/dx11` has none (HLSL compiled at run time by
`D3DCompileFromFile`, Windows only). In CrossOver (its shared-support folder):
`share/wine/mono/wine-mono-10.4.1/lib/x86_64/FNA3D.dll` (a `vs_4_0` of 604 bytes at 0x65ff0, a `ps_4_0` of 596 bytes at 0x66250),
`lib/wine/x86_64-windows/mfmediaengine.dll` (`ps_4_0` 760 bytes at 0xd960, `vs_4_0` 216 bytes at 0xdc60), `.../wine-mono-10.4.1/lib/x86_64/SDL3.dll`
(25 containers, four of them SM4/5), and, counted but not examined, `xul.dll` (27), `libEGL.dll` and `libGLESv2.dll` (55 each) in
the Gecko package. The attempt to list the blobs of `libGLESv2.dll` was refused by the tool-permission layer ("Untrusted Code
Integration") and was not retried or worked around; I did not take bytes out of any of these files for the test programs, and
wrote the bytecode myself instead, which also made every field known. If compiler output is wanted in the test, the
FNA3D or mfmediaengine pairs are the smallest candidates and would need their input layouts read out of the `ISGN` chunks first.

## 5. Patches

None. `patches/dxmt/README.md` says so. The clone is on `main` at `e94c312`, `git status` there is clean, the branch `tf2` was
not created (`tools/dxmt/build.sh` creates it from `e94c312` and applies `patches/dxmt/*.patch` when the first one exists).
The three defects the earlier survey listed (empty `getExePath()`, two string encodings for the adapter name, `aligned_alloc` argument
order) are not needed for this study and were left alone; the first two were observed (section 6), the third not tested.

## 6. Look ahead

**Window path: the earlier survey's plan holds.** Run: a swap chain with `OutputWindow` = 0x1000 (`tools/dxmt/window_probe`, forked children):

    info:  Using feature level D3D_FEATURE_LEVEL_11_0
    warn:  GetModuleHandle not implemented.
    warn:  GetWindowThreadProcessId not implemented.
    warn:  GetCurrentProcess not implemented.
    warn:  GetProcessId not implemented.
    err:   Failed to create metal view, it seems like your Wine has no exported symbols needed by DXMT.
    OutputWindow=0x1000: child killed by signal 6 (Abort trap: 6)
    OutputWindow=0x0: err: D3D11CreateDevice: Failed to create swap chain; hr=0x887a0001 (DXGI_ERROR_INVALID_CALL), exit 1, no abort

That is `d3d11_swapchain.cpp:135-140` calling `WMT::CreateMetalViewFromHWND` -> `_CreateMetalViewFromHWND`
(`src/winemetal/unix/winemetal_unix.c:1684-1719`), which finds no `macdrv_*` symbols and returns a null view. Re-read after the
build: the files and functions are as the earlier survey named them: `winemetal_unix.c` (`_CreateMetalViewFromHWND`, `_ReleaseMetalView`),
`src/util/wsi_window_headless.cpp`, `src/util/wsi_monitor_headless.cpp`; the native build already exports `_CreateMetalViewFromHWND`
(a C function in `winemetal.dylib`), so a patch to the first of them needs only `winemetal.dylib` rebuilt [N: both files are in its source list]. One addition to the earlier survey's
list: the constructor reaches the abort only after three more Win32 stubs have logged (`GetWindowThreadProcessId`, `GetCurrentProcess`,
`GetProcessId`, the same-process check, `d3d11_swapchain.cpp:1108`), so a null `HWND` is the only input that fails softly.

**What the log says at device creation** [V: ran, `DXMT_LOG_LEVEL=trace`; per `D3D11CreateDevice` call]:

| Line | Count | Meaning |
|---|---|---|
| `warn: D3DKMTOpenAdapterFromLuid not implemented.` + `warn: Failed to open D3DKMT adapter` | 1 per DXGI factory creation (`D3D11CreateDevice` makes one itself) | the D3DKMT stubs (`util_d3dkmt.h`): shared resources, keyed mutex, LUID adapter open; harmless for device creation |
| `warn: GetCurrentThread not implemented.` + `warn: SetThreadPriority not implemented.` | 4 per device | each DXMT worker thread start tries to raise its priority: encode, finish, and the first two shader workers [N: four starts] |
| `warn: GetModuleHandle not implemented.` | 1 per device | the `dxgitrace.dll` probe (`d3d11_device.cpp:64`) |
| `info: Maximum supported feature level ...` / `Using feature level ...` | 1 each | |

Nothing about a missing Apple interface, no "unrecognized selector" (the private logic-op selectors are present), no Metal error. With a swap chain
there are the three extra stubs above and, per the earlier survey, `ReleaseSemaphore` on every `Present` (not run). `DXMT_LOG_LEVEL=error` silences all the
warnings.

**The empty executable name** (`getExePath()` returns `""` off Windows and Linux, `util_env.cpp:94`) [V: ran]: (1) the shader cache is
`$(getconf DARWIN_USER_CACHE_DIR)dxmt/shaders_320.db` (plus `-lock`, `-shm`, `-wal`), i.e. `dxmt//` without a per-program directory,
shared by every native program (the key is a hash of shader and variant, so sharing is harmless), unless `DXMT_SHADER_CACHE_PATH`
(absolute directory) or `DXMT_SHADER_CACHE=0` is set; table name `cache_26`; (2) the log file is `./_dxgi.log` in the current
directory, one file for dxgi and d3d11, unless `DXMT_LOG_PATH` is a directory or `none`; (3) `[exe name]` sections in `dxmt.conf` can only match an
empty name, so per-game settings must go in the unsectioned part of `$DXMT_CONFIG_FILE` or `./dxmt.conf`, or in the `DXMT_CONFIG`
environment string (ran: `DXMT_CONFIG="d3d11.maxFeatureLevel=11_0"` takes effect). My first runs, before I set `DXMT_SHADER_CACHE_PATH`, left a
28 KB `dxmt/shaders_320.db*` in the user cache directory next to the existing `dxmt/cs2.exe/` (not touched); `probe.sh` and `timing.sh` use
private directories.

The shader cache works in native mode [V: ran]: after `probe.sh`, `shaders_320.db` has table `cache_26` with 4 rows (2 compute shaders, the
vertex and the pixel shader; 12,248 bytes of values). The Metal driver's own pipeline cache is a separate thing, in
`$(getconf DARWIN_USER_CACHE_DIR)com.apple.metal/<gpu id>/` (shared by all processes of this user).

**`airconv` as a tool** [V: ran]: `airconv X.dxbc -S -o X.ll` converts the hand-assembled blobs in 10 ms wall each (process start included). The pixel shader's AIR is
`define <{ <4 x float> }> @shader_main(<4 x float> %0)` with input `user(reg1_0)` (named by register, not by semantic), output `air.render_target 0`, interpolation `center perspective`:
consistent with the earlier survey's reading that the tool gives one generic conversion per shader, with a function name and input names that differ from
what DXMT itself requests at run time.

## 7. What was fetched

Nothing, in this study. For the record, what is in `third_party/dxmt` from the earlier survey's earlier fetch: the clone of `https://github.com/3Shain/dxmt` at
`e94c312f5c054263acf261cfa109edf13e757587`; the submodule `include/native/directx` = `https://github.com/misyltoad/mingw-directx-headers` at
`9df86f2341616ef1888ae59919feaa6d4fad693d` (`git submodule status`: present, "heads/main"); the submodule `external/nvapi`
(`https://github.com/NVIDIA/nvapi`, `d08488fcc82eef313b0464db37d2955709691e94`) is **not** fetched (only `-Denable_nvapi` uses it). No meson wrap, no pip,
no Homebrew install; `llvm@15`, meson and ninja were already installed. `build.sh` never fetches: if the headers submodule is missing it stops and prints
the one command a user would run.

## 8. Timings [V: ran: `sh tools/dxmt/timing.sh 5`, median of 5 rounds, ms]

"First draw" = from the first `UpdateSubresource`/`Clear`/`Draw` to the staging pixels being mapped (so it includes the copy and the CPU wait), in a new
process, for the one-triangle pipeline. Each round uses a new `--salt` (a pixel shader `col + float4(0,0,0,N*1e-9)`, alpha clamps, pixels unchanged) so Metal's
own cache has not seen it. The shaders are 10 instructions; a real game's will take longer.

| State | first draw to readback | all five |
|---|---|---|
| cold: shader new to Metal, empty DXMT cache | **102.2** | 144.8 119.7 100.3 102.2 102.1 |
| Metal's cache knows the shader, empty DXMT cache (DXMT converts again) | 14.6 | 14.6 14.6 14.8 15.0 14.2 |
| warm: DXMT cache filled by the cold run | 13.1 | 16.5 12.8 14.2 13.1 12.7 |
| `DXMT_SHADER_CACHE=0` | 15.8 | 15.8 18.4 16.5 13.5 12.8 |
| second draw on the same pipeline, same process | 2.3 to 2.5 (every run) | |

Reading: the Metal driver's compile of a pipeline it has not seen is about 90 ms here; DXMT's own conversion costs about 1 to 2 ms (the difference between rows 3
and 2/4, near the noise); DXMT's sqlite cache therefore saves almost nothing for tiny shaders, and what matters for first-use stalls is Metal's cache (which persists
between runs). The very first draw ever made in this session, before either cache had anything (scratch run of the same program), took 95 ms. Whole-process wall time (5 runs
each, warm caches): `offscreen_triangle` 0.41 to 0.61 s (`dlopen` of the 27 MB `winemetal.dylib`, device, pipeline, two draws, exit), `device_probe` 0.11 to 0.41 s; the first
probe run ever took 1.2 s. The split between `dlopen`, `MTLCopyAllDevices` and device creation was not measured **[?]**.

## 9. The scripts, and how to repeat

All under `tools/dxmt/`; each takes no arguments (except `timing.sh [rounds]`), uses only system tools plus the installed meson, ninja, LLVM 15 and Metal Toolchain.

- `build.sh`: checks tools, LLVM 15, Metal Toolchain, the headers submodule and that the clone descends from `e94c312`; applies `patches/dxmt/*.patch` on `tf2` if any; `meson setup` if not
  configured; `meson compile`. Exit non-zero on any failure. `DXMT_LLVM15` and `DXMT_BUILD_DIR` override the LLVM prefix and build directory.
- `probe.sh`: runs `build.sh`, checks `dxbc_blobs.h` is what `make_dxbc.py` writes, compiles `device_probe.cpp` and `offscreen_triangle.cpp` with system `clang++` into
  `build-native/tf2-probe/`, runs the device probe (including the compute check) and the triangle twice (empty cache, then filled), prints a summary of DXMT's stderr. Exit 0 only
  if the device was created, the compute check passed and both triangle runs passed. Tested: exit 1 with `DXMT_CONFIG="d3d11.maxFeatureLevel=9_1"` (device creation fails), exit 0
  normally, from another working directory too.
- `timing.sh`: section 8. `window_probe.cpp`: section 6 (built by hand, expected to crash a child; not part of `probe.sh`):
  `clang++ -std=c++20 -w -I third_party/dxmt/include/native/windows -I third_party/dxmt/include/native/directx -I tools/dxmt tools/dxmt/window_probe.cpp -o <out>; <out> third_party/dxmt/build-native`.
- `make_dxbc.py` (`--check`, `--dump <dir>`), `dxbc_blobs.h` (generated, committed), `dxmt_load.h` (the `dlopen` loader both programs share), `device_probe.cpp`, `offscreen_triangle.cpp`.
- `make test` passes on the worktree (37 s, "PASSED: 0 checks failed"); nothing under `tests/` or the `Makefile` was changed.

## What I could not determine

- **Anything the hand-assembled shaders do not exercise**: constant buffers and `RDEF`, textures and samplers, control flow, geometry/tessellation, the many instructions a real renderer uses. Whether
  airconv translates the game's shaders is for the conversion sweep (step 2 of the earlier survey), not answered here. A compiler-produced pair would be a better test input (section 4).
- **Compile cost of realistic shaders**, and what first-use stalls will look like in a game: only the 10-instruction case was timed.
- **Behaviour of the debug build** (`--buildtype debug` enables the 129 `assert(0)` default branches); not built.
- **Whether deployment target 27.0 matters**: the dylibs load on macOS 27 only as built; whether `MACOSX_DEPLOYMENT_TARGET=14.0` at configure time builds (the code uses `@available` guards) was not tried.
- **Window-path behaviour on the main thread** (`execute_on_main`, SDL3's event pump), the per-`Present` `ReleaseSemaphore` warning, `GetFrameLatencyWaitableObject` returning null, `MTLHUDService` missing: all need a swap chain, which is a later study.
- **Why DXMT answers `UnifiedMemoryArchitecture = no`** and `DedicatedVideoMemory = 6.66 GiB` (about half the working set the earlier survey measured, 13,639 MiB) on a unified-memory GPU; read as DXMT's choice, not checked in source.
- **Stability over many runs or under load**: several dozen process runs, no crash or hang; no stress test; threads were not inspected. The test machine has had unexpected power cuts, so nothing here ran longer than a minute.
- **Left outside the repository**: `third_party/dxmt/build-native` (76 MB, includes `tf2-probe/`), a 28 KB default-location shader cache `.../C/dxmt/shaders_320.db*`, small additions to Metal's own cache, and scratch files under the session scratchpad (including a second 76 MB build, `bn2`).
