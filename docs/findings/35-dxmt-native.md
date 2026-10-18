# 35: what DXMT's native (no Wine) build gives us for Direct3D 11, and what we must add

Subject: DXMT, `https://github.com/3Shain/dxmt`, commit
**`e94c312f5c054263acf261cfa109edf13e757587`** (2026-10-06, "fix(d3d11): avoid deadlock on reentrant
`ResizeTarget()`"), shallow clone in `third_party/dxmt`, plus its submodule `include/native/directx`
(`misyltoad/mingw-directx-headers` at `9df86f2341616ef1888ae59919feaa6d4fad693d`, fetched with
`git submodule update --init --depth 1 include/native/directx`; the `external/nvapi` submodule was not
fetched, it is only used by `-Denable_nvapi`). The upstream pull request #133 was read by fetching its
head ref (`git fetch --depth 1 origin pull/133/head`, head `eb567a3c515a16f9b09626f3b51e0ecfcf1836ec`);
nothing was posted anywhere and the project was not contacted. All paths below are
`third_party/dxmt/<path>:<line>`.

Tags, as in the other findings: **[V]** verified, with how: *read* (source or build file read),
*ran* (program executed on this Mac), *compiled* (Apple clang front end, no linking), **[N]** inferred
from what is named, **[?]** not determined.

Scripts (all under `tools/scout/`, system `clang` only, they write nothing outside a temp directory):
`dxmt_abi_scan.py` (type sizes, struct layouts, method signatures, both ABIs),
`dxmt_syntax_check.py` (front-end compile of the non-LLVM sources), `dxmt_metal_check.m` (GPU facts and
non-public Apple interfaces on this macOS), `dxmt_probe.cpp` (the Q9 program: written and compile-checked,
**not run**).

## Summary

1. "Native build" = an ordinary meson non-cross build (`-Ddxmt_native` is deprecated, `meson.build:10`): `dxgi.dylib`, `d3d11.dylib` (with D3D10), `d3d10core.dylib`, `winemetal.dylib`, plus an `airconv` tool; LGPL-2.1+; upstream CI builds it.
2. It was NOT built here: meson, ninja, LLVM 15 (exact major; Homebrew has 23.1.3, which fails to compile airconv) and the Metal Toolchain (Xcode says "uninstalled") are missing; the 83 non-LLVM sources do parse with Xcode 27.
3. ABI: COM interfaces are plain Itanium C++ abstract classes from mingw-w64 headers, `STDMETHODCALLTYPE` empty, so arm64 AAPCS64 vtables; base types match Windows x64 except `WCHAR`/`wchar_t` (4 bytes, not 2).
4. Only 6 of 491 compared structs differ from Windows (`DXGI_ADAPTER_DESC`/`1`/`2`/`3`, `DXGI_OUTPUT_DESC`/`1`, all `WCHAR` arrays); 8 of 734 methods have float or struct-by-value arguments (`ClearDepthStencilView`, `Get/SetResourceMinLOD`, `EnumAdapterByLuid`).
5. Entry points are plain C exports (`D3D11CreateDevice`, `...AndSwapChain`, `D3D11CoreCreateDevice`, `CreateDXGIFactory`/`1`/`2`); a device needs no window; there is no native sample (`tests/dx11` are Win32 programs).
6. Window: native mode has no working swap chain: the constructor needs Wine's winemac and calls `abort()` (`d3d11_swapchain.cpp:135-140`); window and monitor code is headless stubs (1024x1024, no outputs). Smallest fix: let `HWND` be our `CAMetalLayer*`, three files.
7. Shaders: DXBC to AIR in process by statically linked LLVM 15 ("airconv"), one compile per pipeline variant on a worker pool, cached in sqlite; no Apple toolchain at run time; the `airconv` tool cannot prebuild those variants. DXMT has neither `D3DReflect` nor `D3DGetBlobPart`.
8. Coverage is wide (FL 11_1 on Apple7+, Device..5, Context..4, SwapChain..4, Factory..6, compute, tessellation and geometry shaders via mesh shaders, MSAA resolve, BC, deferred contexts); stubbed: predication, DrawAuto, class linkage, counters, shared resources; 19 `UNIMPLEMENTED` sites `abort()` (tiled resources, `SwapDeviceContextState`, staging<->BC copies).
9. Threads: std::thread and pthread only; per device an encode, a finish and (native: instantly ending) event thread plus 2 to 2xcores shader workers; two layer calls `dispatch_sync` to the main thread; Win32 stubs return failure and log a warning (`ReleaseSemaphore` on every Present).
10. Needs macOS 14+ in practice (Metal 3.1/3.2, mesh shaders, MetalFX linked), no entitlements, non-public Apple selectors/classes used by default (present on this macOS 27.0.1 except `MTLHUDService`); to patch: `getExePath()` returns "" (cache dir, config, log names), two string encodings for adapter names.

## 1. What the native build is (Q1)

- **The option.** There is no native-specific target any more. `meson.build:10` sets
  `dxmt_crossbuild = meson.is_cross_build()`; a build without `--cross-file` is the native build.
  `meson.options:4` keeps `dxmt_native` as `deprecated : true` (commit `20bd2a4`, 2026-03-10, "Since it
  effectively means not a cross build"). The `-DDXMT_NATIVE` define is only a C preprocessor macro
  that `src/nativemetal/meson.build:20` passes to the three Objective-C/C files of `winemetal`. [V: read]
- **What changes when not cross-building** [V: read]: native headers `include/native/windows` and
  `include/native/directx` are used instead of mingw's (`meson.build:24-31`); no `windres`
  (`:110-112`); `winemetal` is built from `src/nativemetal` (one dylib holding the former Windows-side thunks and
  the Mach-O side) instead of `src/winemetal` (a PE dll plus a `.so`) (`src/meson.build:4-8`); the
  window system layer is `wsi_*_headless.cpp` and `wsi_platform_darwin.cpp` instead of the `win32` ones
  (`src/util/meson.build:27-39`); `windows_native_install_dir` is empty (`meson.build:187-189`).
- **Files produced** [N: from the `shared_library()`/`executable()` calls with `name_prefix : ''`; names and
  the install-name rule are meson defaults, not seen on disk]:

  | File | Defined at | Contents |
  |---|---|---|
  | `winemetal.dylib` | `src/nativemetal/meson.build:16-23` | Metal wrappers (`winemetal_thunks.c`, `unix/winemetal_unix.c`, `unix/cache.c`) **and the whole of airconv with LLVM 15 linked in statically** (`airconv_dep_darwin`, `src/airconv/darwin/meson.build:25-37`) |
  | `dxgi.dylib` | `src/dxgi/meson.build:14` | DXGI factory, adapter, output; depends on winemetal |
  | `d3d11.dylib` | `src/d3d11/meson.build:44` | D3D11 and the D3D10 front end (`d3d10_src`, `:32-39`); depends on dxgi and winemetal |
  | `d3d10core.dylib` | `src/d3d10/meson.build:10` | thin D3D10 core entry; depends on d3d11 and dxgi |
  | `airconv` (executable) | `src/airconv/darwin/meson.build:39-45` | command-line DXBC to AIR/metallib converter |

  Architecture is the host's (arm64 here); `-arch` flags are only added for cross builds
  (`meson.build:199-203`). `d3d12`, `nvapi64`, `nvngx` are separate options (`meson.options:9-11`), off by default and
  not part of upstream's native CI job (`.github/workflows/ci.yml:611-640`). [V: read]
- **Install names / dependencies** [N: from the build files, `otool -L` impossible without a build].
  `winemetal.dylib` links weakly CoreFoundation, Metal, Foundation, CoreGraphics, QuartzCore, ColorSync,
  Cocoa, **strongly MetalFX**, plus `-lsqlite3` (`src/nativemetal/meson.build:8-14`), `-lm -lz -lcurses -lxml2`
  and, for a Homebrew LLVM path, `libzstd.a` and `libunwind.a` (`src/airconv/darwin/meson.build:12-23`). The
  others link `winemetal.dylib` and `dxgi.dylib` by meson's default install name `@rpath/<file>.dylib`;
  `winemetal` sets `build_rpath : '@loader_path/:@loader_path/../../'` (`nativemetal/meson.build:22`).
- **Left out compared with the Wine build** [V: read]: PE `.dll` files and `winebuild --builtin`
  post-processing; 32-bit/WoW64 argument conversion (`#ifndef DXMT_NATIVE` at `winemetal_unix.c:1874-2196`,
  `:3315-end`); Win32 events for `MTLSharedEvent` (`:2527-2600`, native stubs `:2602-2622`); Wine's window system
  (`macdrv_*` lookups, Q4); the Win32 window code (`wsi_window_win32.cpp`, `wsi_monitor_win32.cpp`); the
  Metal shader-cache directory call in `DllMain` (`dxgi.cpp:53-74`, `#ifdef _WIN32`); NVIDIA vendor extension
  registry setup (`dxgi.cpp:16-51`); everything D3DKMT (shared resources, keyed mutex, shared fences) is a
  stub returning -1 (`src/util/util_d3dkmt.h:517-655`).
- **Upstream support level.** The native targets were added 2025-07-10 (`94e46c3`, `39a704d`); upstream
  CI has one native job, `build-clang-release-arm64-macos`, on `macos-15`, Xcode 16.1, meson 1.10.0 and a
  self-built LLVM 15.0.7 (`.github/workflows/ci.yml:21`, `:249-298`, `:611-640`). That job only builds; the
  tests are Windows programs. DXMT's README says it is "using Wine" and `AGENTS.md`/`CONTRIBUTING.md` refuse
  AI-written contributions (matches the project's private-fork rule). [V: read]
- **Licences, as stated in the repository** [V: read]:
  - DXMT's own code: LGPL 2.1 or later (`LICENSE:1-12`, `COPYING.LIB`). Releases up to v0.80 were MIT
    (`LICENSE.OLD`).
  - Files derived from DXVK: zlib/libpng (`src/util/dxvk.LICENSE`; marked in file headers, for example
    `src/util/thread.hpp:1-9`).
  - `libs/DXBCParser`: MIT, Microsoft (`libs/DXBCParser/ShaderBinary.h:1-2`).
  - `include/native/directx` (WIDL-generated mingw-w64 headers): LGPL 2.1, with the note that using the
    headers does not make the user's code LGPL (`include/native/directx/README.md`).
  - `src/util/util_md5.cpp`: MIT, Philip Rebohle; `src/util/sha1/sha1.c`: public domain;
    `src/airconv/sha256.hpp`: Brad Conte's public-domain-style code; `include/tl/generator.hpp`: CC0.
  - LLVM 15, statically linked into `winemetal.dylib`: Apache-2.0 with LLVM exceptions [N: LLVM's own
    licence, not stated in the DXMT clone]. Apple frameworks and system sqlite are system libraries.
  - Consequence [N]: the dylibs are LGPL; our changes to DXMT files fall under it. We neither distribute nor
    link DXMT statically into our binary, so this is only a record.

## 2. The calling convention problem (Q2)

- **Interfaces are plain C++ virtual classes.** `include/native/windows/unknwn.h:10-23` declares
  `IUnknown` with `virtual HRESULT QueryInterface(...) = 0; virtual ULONG AddRef() = 0; virtual ULONG Release() = 0;`
  and a non-virtual template `QueryInterface<Q>`. `windows_base.h:209-210` defines `STDMETHODCALLTYPE` and
  `__stdcall` as empty, `:223-224` `interface`/`MIDL_INTERFACE(x)` as `struct`. The D3D11/DXGI interfaces
  come from the mingw-w64 headers (`virtual HRESULT STDMETHODCALLTYPE Name(...) = 0;`). So on arm64 macOS
  they are Itanium-ABI classes: vtable pointer at offset 0, slots in declaration order, `this` in `x0`, arguments by
  AAPCS64 (Apple variant), not the Windows x64 convention. [V: read, and compiled: every header parses
  with Apple clang]
- **vtable order equals the Windows order.** Across all 123 D3D11/DXGI/D3D10Device interfaces (734
  own virtual methods) there is no overloaded virtual method and no virtual destructor, so MSVC's
  reverse-order rule for overloads and Itanium's destructor slots do not shift any slot. [V: compiled;
  `dxmt_abi_scan.py methods` prints "overloaded virtual methods inside one interface: 0; interfaces with a virtual
  destructor: 0" over the 123 interfaces it examined, from the clang AST of the headers]. DXMT's implementation classes may add a virtual
  destructor (`com_object.hpp`, `NoWrapper`), which lands after the interface slots. [V: read]
- **Types** (`include/native/windows/windows_base.h`; sizes/alignments from `dxmt_abi_scan.py types`) [V: compiled]:

  | Type | Definition | macOS arm64 | Windows x64 |
  |---|---|---|---|
  | `HRESULT` | `int32_t` (`:28`) | 4/4 | 4/4 |
  | `LONG`, `ULONG`, `BOOL`, `DWORD` | `int32_t`, `uint32_t`, `INT`, `uint32_t` (`:24,25,37,104`) | 4/4 | 4/4 |
  | `LONGLONG`, `LARGE_INTEGER` | `int64_t` and a union of two `{DWORD LowPart; LONG HighPart}` structs and `QuadPart` (`:57,163-175`) | 8/8 | 8/8 |
  | `GUID`/`IID` | `uint32_t,uint16_t,uint16_t,uint8_t[8]` (`:70-81`) | 16/4 | 16/4 |
  | `HANDLE`, `HWND`, `HMONITOR` | `void*` (`:108-113`) | 8/8 | 8/8 |
  | `SIZE_T`, `LONG_PTR`, `UINT_PTR` | `size_t`, `intptr_t`, `uintptr_t` | 8/8 | 8/8 |
  | `WCHAR` | `wchar_t` (`:30`) | **4/4** | **2/2** |
  | C `long` | not used in any D3D11/DXGI signature or struct (scan found none) | 8/8 | 4/4 |
  | `REFIID`/`REFGUID` | `const IID&` in C++ (`:83-85`), a pointer in the ABI | 8 | 8 |
- **Structs whose layout differs from Windows** (all D3D11/DXGI/d3dcommon/d3d11shader/d3dcompiler records,
  491 compared, 467 of them Direct3D/DXGI named; clang `-fdump-record-layouts-complete` for
  `arm64-apple-macos14.0` against `x86_64-pc-windows-msvc`) [V: compiled]. Exactly six, all because of a
  `WCHAR` array:

  | Struct (method) | `sizeof` macOS / Windows | First field that moves |
  |---|---|---|
  | `DXGI_ADAPTER_DESC` (`IDXGIAdapter::GetDesc`) | 560 / 304 | `VendorId` 512 vs 256 |
  | `DXGI_ADAPTER_DESC1` (`IDXGIAdapter1::GetDesc1`) | 568 / 312 | `VendorId` 512 vs 256 |
  | `DXGI_ADAPTER_DESC2` (`IDXGIAdapter2::GetDesc2`) | 576 / 320 | `VendorId` 512 vs 256 |
  | `DXGI_ADAPTER_DESC3` (`IDXGIAdapter4::GetDesc3`) | 576 / 320 | `VendorId` 512 vs 256 |
  | `DXGI_OUTPUT_DESC` (`IDXGIOutput::GetDesc`) | 160 / 96 | `DesktopCoordinates` 128 vs 64 |
  | `DXGI_OUTPUT_DESC1` (`IDXGIOutput6::GetDesc1`) | 216 / 152 | `DesktopCoordinates` 128 vs 64 |

  Everything else (including every `D3D11_*` desc, `DXGI_SWAP_CHAIN_DESC`, `DXGI_MODE_DESC`, `D3D11_SHADER_DESC`
  and friends) has identical size, alignment and field offsets. The proxy for these six must copy field by
  field and re-pack the string (`Description[128]`, `DeviceName[32]`).
- **The string content is not even consistent inside DXMT** [V: read]. `GetDesc3` writes the adapter name with
  `NSString getCString:...encoding:WMTUTF16StringEncoding` straight into the buffer the native header types as
  `wchar_t[128]` (`src/dxgi/dxgi_adapter.cpp:156`): that is 2-byte UTF-16 units packed from the start (the first
  256 bytes are already the Windows `WCHAR[128]`; whether a byte-order mark is written is **[?]**). The
  `dxgi.customDeviceDesc` branch (`:150-154`) uses `str::transcodeString`, which picks the encoding from
  `sizeof(wchar_t)` (`util_string.hpp:22-28`) and so writes 4-byte units. One of the two should be patched in
  our fork before the thunk relies on either.
- **Methods whose arguments need type-specific thunk code** (clang AST of the headers, Windows target)
  [V: compiled]. Floating-point or struct-by-value (Windows x64 assigns register by argument position,
  AAPCS64 by class): `ID3D11DeviceContext::ClearDepthStencilView` (arg 3 `FLOAT Depth`),
  `ID3D11DeviceContext::SetResourceMinLOD` (arg 2 `FLOAT`), `ID3D11DeviceContext::GetResourceMinLOD`
  (returns `FLOAT`), `IDXGIFactory4::EnumAdapterByLuid` (arg 1 `LUID`, 8 bytes, by value: an integer register
  in both ABIs but the thunk must know), `ID3D10Device::ClearDepthStencilView`, and three `ID3D11VideoContext`
  methods the game will not call. Every other of the 734 methods takes only integers, enums, `BOOL`s and
  pointers. Wide-string pointers (`const WCHAR*`): `ID3DUserDefinedAnnotation::BeginEvent`/`SetMarker`,
  `ID3D11DeviceContext2::BeginEventInt`/`SetMarkerInt` (DXMT ignores them: `d3d11_annotation.hpp:19-23`,
  `d3d11_context_impl.cpp:3246-3256`), `ID3D11Device1::OpenSharedResourceByName`, `ID3D11Fence::CreateSharedHandle`,
  `IDXGIResource1::CreateSharedHandle` (all behind unimplemented shared-resource code). So no string
  conversion is needed unless shared resources are implemented.
- **Calls from DXMT into guest objects** [V: read]: `ID3D11DeviceChild::SetPrivateDataInterface` and the DXGI
  equivalent call `AddRef`/`Release` on an application `IUnknown` (`src/util/com/com_private_data.cpp:30,80`), so
  RUNTIME.md 3.7's "other direction" object is needed for that one case. `HWND` crosses at
  `IDXGIFactory::CreateSwapChain`/`MakeWindowAssociation`/`GetWindowAssociation`,
  `IDXGIFactory2::CreateSwapChainForHwnd`, `IDXGISwapChain1::GetHwnd`, `DXGI_SWAP_CHAIN_DESC::OutputWindow`
  (`d3d11_swapchain.cpp:471`), `GetHwnd` (`:689-696`) and `D3D11CreateDeviceAndSwapChain`; `HMONITOR` in `DXGI_OUTPUT_DESC::Monitor`.
- **Return values** are `HRESULT`/`ULONG`/`BOOL`/`UINT` in `w0` (upper half undefined on arm64: the thunk must
  zero or sign extend); no method in the scanned set returns a struct.

## 3. Entry points (Q3)

- **Exports** [V: read]. `d3d11`: `D3D11CoreCreateDevice`, `D3D11CreateDevice`, `D3D11CreateDeviceAndSwapChain`,
  `D3D11On12CreateDevice` (returns `E_NOTIMPL`) (`d3d11.cpp:32,113,207,218`, `d3d11.def`); `dxgi`:
  `CreateDXGIFactory`, `CreateDXGIFactory1`, `CreateDXGIFactory2`, `DXGIDeclareAdapterRemovalSupport` (a stub),
  `DXGIGetDebugInterface1` (returns `E_NOINTERFACE`) (`dxgi_factory.cpp:327,344,348`, `dxgi.cpp:78,91`);
  `d3d10core`: `D3D10CoreCreateDevice`, `D3D10CoreRegisterLayers`. They are `extern "C"` functions with the
  C names, ordinary AAPCS64 C calls. The `.def` files only matter to Windows linkers; on macOS no
  `-fvisibility=hidden` is set anywhere, so every symbol (including all C++ ones) is exported [N: `nm -gU` after a
  build will show it].
- **Creating a device today** [V: read]. `D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, ...)` calls
  `D3D11CreateDeviceAndSwapChain` (`d3d11.cpp:207-216`), which with a null adapter calls `CreateDXGIFactory1`, takes
  `EnumAdapters(0)` (all Metal devices, `MTLCopyAllDevices`; with several, the non-unified-memory one first:
  `dxgi_factory.cpp:174-205`) and calls `D3D11CoreCreateDevice` (`d3d11.cpp:32-111`). That requires the adapter to
  answer `IMTLDXGIAdapter` (DXMT's own interface), so a device can only be made with a DXMT adapter. Nothing on
  this path touches a window, a monitor or Wine. `DriverType` is ignored with a warning when the adapter is null
  (`:138-139`); `Flags` and `SDKVersion` are passed on or ignored. A swap chain is created only if the caller
  passes `ppSwapChain` (`:179-187`).
- **Feature level.** Maximum 11_1 when the GPU supports family Apple7, else 11_0; `d3d11.maxFeatureLevel` in the
  DXMT config can name 9_1 to 12_1; default list when the caller gives none is 11_0, 10_1, 10_0, 9_3, 9_2, 9_1
  (`d3d11.cpp:48-79`). This M3 Pro reports Apple7, 8 and 9 (`dxmt_metal_check.m`, run), so 11_1 [V: ran for the
  GPU facts, read for the mapping].
- **Samples/tests** [V: read]. None for native. `tests/dx11/*.cpp` are Win32 programs (`WinMain`,
  `RegisterClassExW`, `CreateWindowExW`, `D3DCompileFromFile` from `d3dcompiler_47`, e.g. `dx11_triangle.cpp:13-98`),
  built only for cross builds (`tests/dx11/meson.build`, `-Denable_tests`). `tools/scout/dxmt_probe.cpp` is the
  headless device probe described in Q9.

## 4. The window (Q4)

- **What `HWND` is in native mode** [V: read]. A `void*` (`windows_base.h:108-113`) that nobody interprets: the
  headless window layer returns constants. `wsi_window_headless.cpp`: `getWindowSize` writes 1024 x 1024 (`:19-25`);
  `isWindow` true (`:54`); `isForeground` true, `isMinimized` false (`:60,62`); `getWindowMonitor` returns null
  (`:50-52`); `resizeWindow`, `updateFullscreenWindow` do nothing; `setWindowMode`, `enterFullscreenMode`,
  `leaveFullscreenMode`, `restoreDisplayMode` return false (`:31-48`). `wsi_monitor_headless.cpp`:
  `getDefaultMonitor`/`enumMonitors` return null, every other function false (`:15-44`).
  Consequences: `IDXGIAdapter::EnumOutputs` always returns `DXGI_ERROR_NOT_FOUND` (`dxgi_adapter.cpp:195-203`), so
  no output, no display-mode list (the game cannot enumerate resolutions); any width/height of 0 becomes 1024 (also in
  `CreateSwapChainForHwnd`, `dxgi_factory.cpp:121`, and `ResizeBuffers(0,0,...)`, `d3d11_swapchain.cpp:160,485`);
  `SetFullscreenState(TRUE)` fails with `DXGI_ERROR_NOT_CURRENTLY_AVAILABLE` (`:337-339`); the refresh rate stays
  unknown, so `init_refresh_rate_` stays `DBL_MAX` (`:1082`) and the `SyncInterval` pacing value
  `vsync_duration` (`:769`) comes out as about 0.
- **Who creates the `CAMetalLayer`: in native mode, nobody, and the swap chain then aborts.** The swap-chain
  constructor calls `WMT::CreateMetalViewFromHWND((intptr_t)hWnd, device, layer)` (`d3d11_swapchain.cpp:135`),
  which goes to `_CreateMetalViewFromHWND` (`winemetal_unix.c:1684-1719`). That function does
  `dlsym(RTLD_DEFAULT, "macdrv_functions")` or `get_win_data`, `macdrv_view_create_metal_view`,
  `macdrv_view_get_metal_layer` (Wine's winemac driver) and returns nothing if they are absent. The constructor
  then logs "it seems like your Wine has no exported symbols needed by DXMT" and calls `abort()`
  (`d3d11_swapchain.cpp:137-140`). The function is in the native build too (it is not under `#ifndef DXMT_NATIVE`).
  Under Wine the layer belongs to a `WineMetalView` that winemac creates inside the HWND's client view [N: from the
  field name `client_cocoa_view` and my memory of winemac, whose source is not in the clone; thread unknown **[?]**].
  An earlier plan's "mainline only draws off-screen in native mode" is right in the sense that rendering and read-back work
  but presenting does not.
- **What DXMT does to the layer** [V: read]. Nothing needs to be pre-configured on it. `Presenter` reads its props, then
  sets device, `opaque = true`, `displaySyncEnabled = false`, `framebufferOnly = false`, `contentsScale *=
  scale_factor`, `drawableSize` = swap-chain size x scale, `pixelFormat`, colour space and EDR flag
  (`dxmt_presenter.cpp:11-35,104-151`; `winemetal_unix.c:1624-1636,2268-2284`). The first `setProps` happens in the
  first `Present` (`synchronizeLayerProperties`, `dxmt_presenter.cpp:122-126`), later ones on
  `ResizeBuffers`/colour-space changes. The back buffer is a separate texture; each Present encodes a blit/scale pass into
  `layer.nextDrawable` (`dxmt_presenter.cpp:153-186`) on the **encode thread** (`dxmt_command_queue.cpp:114-165`, run from `:168-185`;
  `dxmt_context.cpp:1087-1105`). A drawable size different from the layer's bounds is scaled by CoreAnimation [N].
- **Threads for the layer.** `_MetalLayer_setProps` and `_MetalLayer_setColorSpace` run their body through
  `execute_on_main`, which is `dispatch_sync(dispatch_get_main_queue(), ...)` unless already on the main thread
  (`winemetal_unix.c:20-26,1624-1636,2268-2284`). So the thread that calls `Present` or `ResizeBuffers` blocks until
  the main thread services its dispatch queue [V: read]. Whether SDL3's event pump does that is **[?]**. SDL3
  states that `SDL_Metal_CreateView`, `SDL_Metal_DestroyView` and `SDL_Metal_GetLayer` "should only be called on
  the main thread" (`/opt/homebrew/include/SDL3/SDL_metal.h`, SDL 3.4.14), so the view must be created by our
  Win32 shim on the main thread and DXMT must be handed the layer, not create it from the render thread.
- **Resize, fullscreen, vsync, present** [V: read]. Resize: the application calls `ResizeBuffers`; DXMT recreates the back
  buffer and updates the layer (`d3d11_swapchain.cpp:481-545,582-597`); a window resize alone does nothing.
  `ResizeTarget` calls `wsi::resizeWindow` / `updateFullscreenWindow`. Fullscreen: `EnterFullscreenMode` picks a mode with
  `FindClosestMatchingMode1`, calls `wsi::setWindowMode` then `wsi::enterFullscreenMode` (`:291-344`); there is no
  exclusive fullscreen on macOS, so it would have to be borderless through SDL3. Vsync: DXMT never enables
  `displaySyncEnabled`; it uses `presentDrawableAfterMinimumDuration(drawable, SyncInterval / refresh)`
  (`dxmt_context.cpp:1101-1104`) and a frame-latency wait of `max_latency` frames (`PresentBoundary`,
  `dxmt_command_queue.hpp:261-273`, default `kSwapchainLatency = 1`, `d3d11_swapchain.cpp:34`). `GetBuffer` serves only
  index 0 (`:259`); `BufferCount` is ignored; `GetFrameLatencyWaitableObject` returns null in native mode because
  `DuplicateHandle` is a stub (`:985-990`, `util_win32_compat.h:90-100`).
- **Everything that assumes Wine** [V: read]: the `macdrv_*`/`get_win_data` lookups (`winemetal_unix.c:1655-1737`);
  `D3DKMTEscape(D3DKMT_ESCAPE_SET_PRESENT_RECT_WINE)` in `wsi_window_win32.cpp:133,178,244` (cross builds only);
  `wine_get_unix_file_name` and `__wine_dbg_output` lookups (`util_env.cpp:140`, `log.cpp:61-65`, `_WIN32` only);
  `GetModuleHandle("dxgitrace.dll")` (`d3d11_device.cpp:64`, a stub in native); `kWineDXGIAdapterUUID`
  (`dxgi_adapter.cpp:14,62`, harmless); the winemac cache path call in `DllMain` (Windows builds only).
- **Smallest change to present into a `CAMetalLayer` we own** [V: read for the code that would change; the change
  itself is untested]. Let the `HWND` DXMT receives be our `CAMetalLayer*` (our `CreateSwapChain` proxy maps the guest
  `HWND` to it; the guest sees its own `HWND` back through `GetHwnd`/`GetDesc` only if we map again, because DXMT
  stores and returns what it was given, `d3d11_swapchain.cpp:471,689-696`).
  1. `src/winemetal/unix/winemetal_unix.c`, `_CreateMetalViewFromHWND` (`:1684-1719`) and `_ReleaseMetalView`
     (`:1722-1737`): under `#ifdef DXMT_NATIVE` return `ret_layer = ret_view =` the retained layer given in
     `params->hwnd`, and release it in `_ReleaseMetalView`. With that the constructor at `d3d11_swapchain.cpp:135-140`
     runs unchanged.
  2. `src/util/wsi_window_headless.cpp`: `getWindowSize` (`:19-25`) must return the layer's pixel size (bounds x
     `contentsScale`, or a callback our runtime registers); make `setWindowMode`/`enterFullscreenMode`/
     `leaveFullscreenMode`/`restoreDisplayMode` (`:31-48`) succeed as no-ops, or intercept `SetFullscreenState` in our proxy.
  3. `src/util/wsi_monitor_headless.cpp` (`:15-44`): return a real monitor from `enumMonitors(0)`/`getDefaultMonitor`
     and answer `getDesktopCoordinates`, `getDisplayName`, `getDisplayMode`, `getCurrentDisplayMode` (refresh rate
     for pacing) from CoreGraphics or SDL3. Needed for `EnumOutputs` / `GetDisplayModeList` (`dxgi_output.cpp:198-203,
     382-389,456-457`), which a game's video settings use.
  4. Not DXMT code: our Win32 shim creates the SDL3 window with `SDL_WINDOW_METAL`, the view and layer on the main
     thread, and keeps the main thread pumping events so `execute_on_main` can complete.
  Upstream pull request #133 (head commits 2026-03-05, "feat(wsi): add sdl2/sdl3 wsi backend") takes the other
  route: `HWND` = `SDL_Window*`, SDL functions found with `dlsym`, `SDL_Metal_CreateView` called from the swap-chain
  constructor (`git show eb567a3:src/util/wsi_window_sdl3.cpp`, `687da98 -- src/d3d11/d3d11_swapchain.cpp`), which is
  the render thread, against SDL's main-thread rule. It also predates current `main` (`restoreDisplayMode()` takes no
  argument, no `isForeground`/`isMinimized`), so it would not apply without edits. Useful as a reference for the SDL3
  monitor/mode functions only. [V: read; whether the PR is still open **[?]**]

## 5. Shaders (Q5)

- **Path** [V: read]. `CreateVertexShader` etc. keep the DXBC and call `SM50Initialize` (parse, reflection). At first
  use a *variant* is compiled: `SM50Compile` (or the tessellation/geometry variants) produces AIR wrapped as a
  metallib (`metallib_writer.cpp`), `MTLDevice newLibraryWithData`, `newFunction`
  (`d3d11_shader.cpp:42-100`, `src/airconv/airconv_public.h:314-352`). Variants depend on pipeline state: input layout
  for vertex fetch, render-target formats and dual-source blending for pixel shaders, GS pass-through, stream-output layout
  (`d3d11_shader.cpp:117-355`; consumed at `dxbc_converter.cpp:335-372,595-605`). Compilation runs on a worker
  pool (`task_scheduler`, `dxmt_tasks.hpp:48-56`, instantiated at `d3d11_pipeline_cache.cpp:193`). A draw does not wait on the
  game thread: when the encode thread reaches it, `GetPipeline` blocks on `ready_.wait(false)` until the pipeline (shader variants plus
  `newRenderPipelineState`) has been compiled (`d3d11_pipeline.cpp:49-52,153-156`, call sites `d3d11_context_impl.cpp:4737,4785,4841,5001`,
  marked "may block"). First use of a shader or state combination is therefore a stall that reaches the game thread once the
  32-chunk command queue (`kCommandChunkCount`, `dxmt_command_queue.hpp:91`) fills.
- **Which compiler, and what it needs at run time.** The compiler is airconv, LLVM 15 linked statically into
  `winemetal.dylib` (Q1), called in process. Apple's Metal Toolchain (`xcrun metal`, `metallib`) is a **build-time**
  requirement only: it compiles the three `src/airconv/shaders/*.metal` (`-std=metal3.1 --target=air64-apple-macos14.0`,
  `src/airconv/meson.build:65-77`) and `src/dxmt/dxmt_command.metal` (`src/dxmt/meson.build:28-41`) and `xxd` embeds
  them (`meson.build:115-132`); at run time the embedded metallib is loaded with `newLibrary(dxmt_command, dxmt_command_len)`
  (`dxmt_command.cpp:16`). Nothing launches a subprocess (`grep` for `popen|system|posix_spawn|xcrun` in the
  run-time sources: none). The GPU driver still compiles AIR to machine code when a pipeline is created, as for any
  Metal app. The Metal Toolchain being missing (`xcodebuild -showComponent metalToolchain`: "uninstalled",
  `xcrun -sdk macosx metal`: "missing Metal Toolchain") therefore blocks the build, not the use. [V: read, ran for
  the toolchain state]
- **LLVM version is binding.** airconv sets typed pointers (`context.setOpaquePointers(false)`, "I suspect Metal uses
  LLVM 14", `dxbc_converter.cpp:1362,1423,1487,1552`; `airconv_cli.cpp:134`) and uses `llvm::Optional` (64 uses) and
  `StringRef::endswith`. Compiling `airconv_cli.cpp` and `dxbc_converter.cpp` against Homebrew's LLVM 23.1.3 gives
  13 and 16 errors (first one: no template `llvm::Optional`) [V: compiled].
- **On-disk cache** [V: read]. A sqlite database per Metal version at
  `$(getconf DARWIN_USER_CACHE_DIR)/dxmt/<executable name>/shaders_<310|320>.db`, or `DXMT_SHADER_CACHE_PATH`
  (absolute), off with `DXMT_SHADER_CACHE=0` (`dxmt_shader_cache.cpp:21-34`, `winemetal/unix/cache.c:23-42,46,104`); key is shader SHA-1 plus variant digest (`d3d11_shader.cpp:117-130`); table name includes `AIRCONV_VERSION`
  (26 now, `airconv_public.h:8`, `dxmt_shader_cache.hpp:8`). **In native mode the executable name is empty**: `getExePath()`
  ends in `return std::string(""); //TODO` for non-Windows, non-Linux (`util_env.cpp:94`), so the cache directory is
  `.../dxmt//`, per-executable sections of `dxmt.conf` never match (`config.cpp:124`) and logs are named `_d3d11.log`.
- **Ahead-of-time conversion.** The `airconv` tool exists in the native build (`airconv_cli.cpp`): `airconv in.dxbc -o out.air`
  (`-A` for metallib, `-S` for text, `--O0/1/2`, options for hull/domain/vertex/geometry pairs). It passes only
  `SM50_SHADER_COMMON_DATA` with Metal 3.2 (`airconv_cli.cpp:218-222`), names the function `shader_main`, and gives
  no input layout or render-target formats. So it yields one generic conversion per shader, not the variants and function
  names DXMT asks for at run time, and it cannot fill DXMT's cache. Two things remain possible: (a) use it as a
  **conversion sweep** over every DXBC the game ships to find shaders airconv cannot translate before any rendering
  work (errors come from `UnsupportedFeature`); (b) fill the cache by running the game (or a driver that links
  `winemetal.dylib` and calls `SM50Compile` with the real arguments). An earlier plan's "convert the game's compiled DXBC ahead
  of time with airconv" needs that correction. [V: read]
- **`D3DReflect` and `D3DGetBlobPart`: DXMT provides neither** [V: read: no definition anywhere in `src`/`libs`; the
  only `d3dcompiler` uses are the Windows tests and the Windows-hosted disassembly option of the airconv tool,
  `airconv_cli.cpp:175-203`]. What is reusable: `libs/DXBCParser/BlobContainer.h:77-125` (MIT) parses the DXBC
  container (`ReadDXBC`, `GetBlob`, `GetBlobFourCC`, `FindNextMatchingBlob`) and knows the four-character codes for
  `ISGN/OSGN/PCSG/SHDR/SHEX/IFCE/RTS0` (not `RDEF` or `STAT`). DXMT has no resource-definition (`RDEF`) parser and no
  `ID3D11ShaderReflection`; its `MTL_SHADER_REFLECTION` describes Metal bindings only. We would write: the
  container walk for `D3DGetBlobPart`, and an `RDEF`/`ISGN`/`OSGN`/`STAT` reader behind guest-ABI native objects for
  `ID3D11ShaderReflection` (22 own methods; plus `ID3D11ShaderReflectionConstantBuffer` 3, `...Type` 11, `...Variable` 4).

## 6. Feature coverage (Q6)

- **Interfaces** (`QueryInterface` lists) [V: read]. Device: `ID3D11Device`..`Device5`, `ID3D10Device`/`1`, `IDXGIDevice`..`3`,
  `ID3D11Multithread`; `ID3D11Debug`, `ID3D11InfoQueue`, RenderDoc/PIX/GPA GUIDs return `E_NOINTERFACE`
  (`d3d11_device.cpp:1169-1210`). Context: `ID3D11DeviceContext`..`4`, `ID3DUserDefinedAnnotation` (all no-ops)
  (`d3d11_context_impl.cpp:640-657`). Swap chain: `IDXGISwapChain`..`4` (`d3d11_swapchain.cpp:228`). Factory:
  `IDXGIFactory`..`6`; adapter `IDXGIAdapter`..`4`; output `IDXGIOutput`..`6` (`dxgi_factory.cpp:26-33`,
  `dxgi_adapter.cpp:51-60`, `dxgi_output.cpp:171-174`). Not offered: `IDXGIFactory7`, `IDXGIDevice4`, video interfaces.
- **Present and real** [V: read]: all `Draw*`/`DrawIndexed*`, indirect draws and `DispatchIndirect`, `Dispatch`, compute
  shaders and UAVs (with a UAV-counter argument, `airconv_public.h:43`), `CopyStructureCount`, `CopySubresourceRegion1`,
  `UpdateSubresource1`, `ClearView`, `ResolveSubresource`, `GenerateMips`, deferred contexts with `FinishCommandList`, and `ExecuteCommandList` on the
  immediate context (`d3d11_context_def.cpp:398`, `d3d11_context_imm.cpp:508`; `ExecuteCommandList` on a deferred context aborts, see below), BC1-7 (the GPU supports
  `supportsBCTextureCompression`, ran), `D24_UNORM_S8_UINT` emulated on `Depth32Float_Stencil8`
  (`dxmt_format.cpp:718-723`, flag `MTL_DXGI_FORMAT_EMULATED_D24`), MSAA, dynamic buffers/textures with `WRITE_DISCARD`/`WRITE_NO_OVERWRITE`, staging `Map`
  for read/write with `DO_NOT_WAIT` (`d3d11_context_imm.cpp:138-290`), queries: event, occlusion (and predicate),
  timestamp, timestamp-disjoint (`d3d11_device.cpp:955-981`), pipeline statistics (a dummy that returns zeros).
- **Tessellation and geometry shaders** are emulated with Metal mesh shaders: `d3d11_pipeline_ts.cpp`, `_gs.cpp`;
  limited on GPUs below Apple9 to a max tess factor of 8 unless the app asks for less (`d3d11_pipeline_ts.cpp:55-60`),
  hull/domain output primitives point/isoline unsupported and geometry+tessellation together unsupported
  (`docs/COMPATIBILITY_FLAG.md`, flags set at `d3d11_context_impl.cpp:4698-4715`). Stream output is emulated by running
  the vertex shader with vertex pulling and writing the buffers (`d3d11_device.cpp:279-295`), only when
  `RasterizedStream == D3D11_SO_NO_RASTERIZED_STREAM` and `NumEntries > 0`; otherwise "not supported, expect
  problem" is logged and the geometry shader is built without stream output. Appending and multiple streams are flagged
  unsupported.
- **Stubbed or missing** [V: read]:

  | Item | Behaviour | Where |
  |---|---|---|
  | `SetPredication` | state stored, predication not applied (flagged "P") | `d3d11_context_impl.cpp:1892-1900` |
  | `DrawAuto` | does nothing (flagged "A") | `:1832-1836` |
  | Class linkage / dynamic linking | `CreateClassInstance`, `GetClassInstance` `E_NOTIMPL`; linkage argument warned and ignored | `d3d11_class_linkage.cpp:36-51`, `d3d11_device.cpp:243-342` |
  | `CreateCounter`, `CheckCounter` | `E_NOTIMPL` | `d3d11_device.cpp:437-444,570-581` |
  | Query types stream-output statistics and overflow predicate | `E_NOTIMPL` in `CreateQuery1` | `:975-979` |
  | `DiscardResource`, `DiscardView` | no-op (logged once) | `d3d11_context_impl.cpp:1268-1290` |
  | `OutputMergerLogicOp` | supported only through a non-public selector (Q8) | `d3d11_state_object.cpp:347`, `d3d11_inspection.cpp:56-60` |
  | Shared resources, keyed mutex, shared handles, `OpenSharedResource*` | go through D3DKMT stubs that return -1; `SharedResourceTier = 0` | `util_d3dkmt.h:517-655`, `d3d11_inspection.cpp:64` |
  | `ID3D11Fence` with a Win32 event | `_MTLSharedEvent_setWin32EventAtValue` is a no-op in native | `winemetal_unix.c:2602-2607` |
  | Tiled resources | `UNIMPLEMENTED` = log and `abort()` (see below); `TiledResourcesTier` not supported | `d3d11_context_impl.cpp:3207-3238`, `d3d11_device.cpp:803` |
  | `SetExceptionMode`, `SetHardwareProtectionState` | `E_NOTIMPL` / stub | `d3d11_device.cpp:705`, `d3d11_context_impl.cpp:3264` |
  | Doubles in shaders | `DoublePrecisionFloatShaderOps = FALSE` | `d3d11_inspection.cpp:19` |
  | Swap chain: `SetRotation`, `SetSourceSize`, `SetBackgroundColor`, matrix transform, `GetRestrictToOutput`, `GetCoreWindow` | `E_NOTIMPL` | `d3d11_swapchain.cpp:701,908,915,932,947,998,1004` |
  | `IDXGIOutput`: `GetDisplaySurfaceData`, `SetDisplaySurface`, `GetFrameStatistics`; factory: window/stereo/occlusion registration, `CreateSwapChainForCoreWindow/Composition` | `E_NOTIMPL` | `dxgi_output.cpp:337-358,534`, `dxgi_factory.cpp:142-252` |
  | `IDXGIDevice3::Trim`, `OfferResources`, `ReclaimResources` | no-ops | `d3d11_device.cpp:1273-1290` |
- **Paths that abort the process.** The macro `UNIMPLEMENTED(text)` logs the text and calls `abort()`
  (`d3d11_private.h:14-17`). 19 sites use it [V: read]: `ExecuteCommandList` on a deferred context
  (`d3d11_context_def.cpp:393`); `SwapDeviceContextState` (`d3d11_context_impl.cpp:1910`); all tiled-resource calls
  (`:3207,3215,3222,3229,3233,3238`, `d3d11_device.cpp:803`); `UpdateSubresource` into a **staging** buffer or texture
  (`:1242,4255`); copies between a **block-compressed texture and a staging resource**, in either direction
  (`:4107,4111,4158,4162`); `WriteToSubresource`/`ReadFromSubresource` (map-on-default, `d3d11_device.cpp:1000,1008`);
  `IDXGIDevice::CreateSurface` (`:1233`); and `d3d11_context_imm.cpp:282`, which sits behind an earlier `E_INVALIDARG`
  return. For a 2016 renderer the ones to watch are staging/BC copies and `UpdateSubresource` on staging resources.
- **Whether any of this matters to Titanfall 2 is [?].** `materialsystem_dx11.dll` imports only `D3D11CreateDevice`,
  `D3DReflect` and `D3DGetBlobPart`; everything else is reached through vtables, so only a trace of the
  vtable calls the renderer makes can say which gaps (predication, stream output, class linkage, query types) are hit.

## 7. Threads (Q7)

- **Primitives** [V: read]. For non-Windows builds `dxmt::thread`, `mutex`, `shared_mutex`, `recursive_mutex` and
  `condition_variable` are the standard-library types (`util/thread.hpp:325-361`); `get_id()` is a thread-local counter
  (`thread.cpp:30-47`); thread names via `pthread_setname_np` (`util_env.cpp:117`); C++20 `std::atomic::wait`
  in the command queue (`dxmt_command_queue.cpp:108,174`). No Wine event, critical section, SRW lock or TLS call remains
  (`util/threadpool.hpp`, which uses the Win32 thread-pool API, is included by nothing). The Win32 functions that
  are still *called* are stubs in `util_win32_compat.h:11-103` and `util_d3dkmt.h:517-655` that log a warning and return
  failure: `GetCurrentProcess`/`GetCurrentThread` (null), `SetThreadPriority` (FALSE, called at every worker start:
  `dxmt_command_queue.cpp:171,190`, `dxmt_tasks.hpp:75`), `CreateSemaphore` (null, once per swap chain), `ReleaseSemaphore`
  (FALSE, **once per Present**, `d3d11_swapchain.cpp:797,807`), `DuplicateHandle`, `CloseHandle`,
  `GetWindowThreadProcessId` (pid 0, which is what makes the same-process check at `:1108-1109` pass), D3DKMT.
  `Logger::warn` is printed at the default level, so expect a warning line per frame; `DXMT_LOG_LEVEL=error` silences
  them. By default a log file `_d3d11.log`/`_dxgi.log` is also created in the current directory (`log.cpp:96-111`)
  unless `DXMT_LOG_PATH=none`.
- **Threads DXMT creates** [V: read]:
  1. Per device (`CommandQueue`, `dxmt_command_queue.cpp:36-43`): `dxmt-encode-thread` (encodes command chunks and
     calls `nextDrawable`, `:168-185`), `dxmt-finish-thread` (waits for command buffers, frees staging memory,
     `:187-224`) and an event-listener thread that returns immediately in native (`winemetal_unix.c:2610-2612`).
  2. Pipeline/shader compile workers: 2 at start, growing to `2 x hardware_concurrency` (`dxmt_tasks.hpp:48-56,138-141`).
  3. Metal's own threads (completion handlers, driver compilation; `setShouldMaximizeConcurrentCompilation(true)`,
     `dxmt_device.cpp:49-53`).
- **Calls that must be on a particular thread.** None for device creation or drawing. The caller must serialise its own use
  of the immediate context: `d3d11_device_mutex` does nothing until the application calls
  `ID3D11Multithread::SetMultithreadProtected(TRUE)` (`d3d11_multithread.cpp:7-47`, default `protected_ = false`,
  `.hpp:16`), as on Windows. The one thread constraint is the main-thread hop in Q4 (`execute_on_main`) [V: read].
- **Language runtime.** The front end uses C++ exceptions and RTTI internally (`catch (const MTLD3DError &)`,
  `d3d11.cpp:107`); airconv is built `-fno-exceptions -fno-rtti` (`src/airconv/meson.build:40-50`). `d3d11.cpp:243-255`
  defines `_massert` and `__cxa_pure_virtual` (non-fatal in release). The recompiled game's own runtime must not unwind
  through DXMT frames; DXMT does not let exceptions escape the exported functions that were read (`CreateDXGIFactory2`,
  `D3D11CoreCreateDevice` catch `MTLD3DError`).

## 8. macOS and Metal requirements (Q8)

- **Versions** [V: read; the OS floor is derived, not enforced by a check]. Documented prerequisite "macOS Sonoma and
  later" and "Xcode 16+, with Metal toolchain" (`docs/DEVELOPMENT.md:5-14`). Run-time: Metal 3.2 when the OS major is
  15 or later, else 3.1 (`dxmt_device.cpp:39-45`, `winemetal.h:1972-1974`); shaders are built for `air64-apple-macos14.0`
  and the metallib header says 14.4 (`metallib_writer.cpp:398-399`); mesh shaders (tessellation, geometry) need Apple7 or
  newer (`d3d11_pipeline_ts.cpp:136`); feature level 11_1 needs Apple7; GPUs below Apple7 get
  "Experimental non-Apple GPU support" (`dxmt_device.cpp:45-47`). `@available(macOS 15, *)` guards cover binary-archive
  options and `requiresSynchronousInitialization` (`winemetal_unix.c:636,650,1437,2924,2938`). MetalFX is linked
  strongly (`nativemetal/meson.build:10`), so the dylib will not load without it (macOS 13+). This Mac: macOS 27.0.1,
  Apple M3 Pro (Apple7, 8 and 9 true, unified memory, BC compression, working set 13,639 MiB) [V: ran,
  `dxmt_metal_check.m`].
- **Non-public Apple interfaces, enabled by default** (`DXMT_NO_PRIVATE_API` is not defined by any build file) [V: read; presence
  on this macOS: ran]:

  | Interface | Used for | Present on macOS 27.0.1 here |
  |---|---|---|
  | `-[MTLRenderPipelineDescriptor setLogicOperationEnabled:]` / `setLogicOperation:`, same on `MTLMeshRenderPipelineDescriptor` (`winemetal_unix.c:482-516,548-549,615-616`) | `OutputMergerLogicOp` | yes, on the instance class `MTLRenderPipelineDescriptorInternal` and on the mesh descriptor |
  | classes `_CADeveloperHUDProperties` (before macOS 27) and `MTLHUDService` (27 and later) (`:1533-1590`) | version label on the Metal HUD, set in every swap-chain constructor | `_CADeveloperHUDProperties` yes; **`MTLHUDService` not found** (messages to a nil class are no-ops; commit `654f547` of 2026-09-15 is a fix for macOS 27 HUD metrics) |
  | C functions `MTLSetShaderCachePath` / `MTLGetShaderCachePath` (`cache.c:224-236`) | Metal pipeline-cache directory; called only from the Windows `DllMain`, so unused in native | yes (also exported by the SDK's `Metal.tbd`) |
  | `bootstrap_register2`, declared by hand (`winemetal_unix.c:2737`) | shared-resource keyed mutex; unused here | yes |
  | `macdrv_functions` and friends | Wine only | n/a |

  The selector use is guarded only by the header declaration, so a future macOS that drops the logic-operation selectors
  would raise "unrecognized selector" on the first pipeline created. No build file defines `DXMT_NO_PRIVATE_API`; adding
  `-DDXMT_NO_PRIVATE_API` to the compiler flags removes these uses [N].
- **Entitlements / signing.** None referenced anywhere in the repository (`grep` for entitlement, codesign: no hits). No
  hardened-runtime exceptions are needed by the code read. Shared-event Mach ports use `bootstrap_register2`, which
  is irrelevant without shared resources. [V: read]

## 9. Build attempt and probe (Q9)

- **Build: not attempted, because it cannot start.** Pre-flight against this Mac [V: ran]:

  | Needed (source) | On this Mac |
  |---|---|
  | meson >= 1.3.0 (`meson.build:1`; CI pins 1.10.0) | **absent** (`which meson`, no `mesonbuild` module in any Python) |
  | ninja (meson's default backend) | **absent** |
  | LLVM 15, exact major, headers and static libs, at `-Dnative_llvm_path` (`meson.options:1`, `DEVELOPMENT.md:8`) | **absent**; Homebrew `llvm` is 23.1.3 (`/opt/homebrew/opt/llvm`), which fails to compile airconv (Q5) |
  | Metal Toolchain (`xcrun -sdk macosx metal`, `metallib`; `meson.build:117-125`) | **absent**: `xcodebuild -showComponent metalToolchain` says "Status: uninstalled"; `xcrun --find metallib` fails |
  | `xxd`, `touch`, git (`meson.build:111,127`) | present |
  | Xcode 16+ | Xcode 27.0 (27A266a), Apple clang 21.0.0, macOS 27 SDK |
  | `include/native/directx` submodule (`meson.build:99-101`) | fetched into the clone |
  | CMake >= 3.27, only to build LLVM from source | absent |

  No script from the clone was run. The only scripts it contains that fetch anything are the CI workflow
  (`.github/workflows/ci.yml`: downloads Wine, llvm-mingw, `git clone` of llvm-project, `pip3 install meson`) and the LLVM
  recipe in `docs/DEVELOPMENT.md:21-46` (`git clone --depth 1 --branch llvmorg-15.0.7`); neither was executed. `meson`
  would also run `git describe` for `version.h` (`meson.build:142-146`).
- **What was done instead** [V: ran/compiled]:
  - `dxmt_syntax_check.py`: `clang -fsyntax-only` with the native include paths and defines on the 83 sources of the native
    configuration that do not include LLVM headers (d3d11, d3d10, dxgi, dxmt, the non-Windows util files, `libs/DXBCParser`,
    and the three Objective-C files of winemetal with `-DDXMT_NATIVE=1`): **83 of 83 pass** with Apple clang 21 and the
    macOS 27 SDK (two generated headers stubbed). The airconv sources were not checked against LLVM 15 (not available).
  - The headers, layouts and method scan of Q2, the Metal/OS capability check of Q8 (`dxmt_metal_check.m`).
  - `dxmt_probe.cpp` compiles to an arm64 Mach-O and its error path runs ("dlopen dxgi.dylib ... no such file"); it was
    **not run against DXMT**. No device was created and no adapter name or feature level was observed. What it is meant to
    print is listed at its top. The expected feature level is 11_1 from the source and the GPU facts, not from a run [N].
- **Sample that draws:** none exists for native; nothing was run.

## 10. What we would have to write or change, in the order I would do it

1. **Get approval for the tools** (next section). Build the unmodified native libraries, run `dxmt_probe` against them, record
   `nm -gU`, `otool -L`, `otool -D` for the four dylibs and the probe output. If device creation fails, stop here.
2. **Conversion sweep with the `airconv` tool** over every DXBC blob the game ships (needs the shader extraction from
   a separate step). It answers early whether DXMT can translate the game's shaders at all. No window work before this.
3. **Fix the native-mode shims in our private fork** (each a few lines): implement `getExePath()` for macOS (`util_env.cpp:94`; it
   feeds the shader-cache directory, the config sections and the log names); make the semaphore stubs real or silent
   (`util_win32_compat.h:70-88`); decide one string encoding for `DXGI_ADAPTER_DESC::Description` (`dxgi_adapter.cpp:150-156`);
   default `DXMT_LOG_PATH=none`. Lower priority: `wsi_platform_darwin.cpp:7` calls `aligned_alloc(size, alignment)` but the C11
   signature is `aligned_alloc(alignment, size)` (SDK `c++/v1/stdlib.h:72`). Harmless today: the only arm64 caller,
   `dxmt_context.cpp:58` (`aligned_malloc(65536, 4096)`), becomes `aligned_alloc(65536, 4096)`, which fails with `EINVAL` on this macOS
   (ran: a 20-line C program, returns NULL), and DXMT then lets Metal allocate the buffer (`winemetal_unix.c:161-169`); the other callers
   (`dxmt_buffer.cpp:45`, `dxmt_texture.cpp:197`, `dxmt_occlusion_query.hpp:170`) are reached only in 32-bit builds (`CpuPlaced` and
   those allocations sit under `#ifdef __i386__`: `d3d11_buffer.cpp:101-103`, `d3d11_texture_dynamic.cpp:121-123`,
   `dxmt_staging.cpp:80-82`). `-DDXMT_PAGE_SIZE=4096` (`meson.build:140`) is also hard-coded while this Mac's page size is 16384
   (`sysctl hw.pagesize`); it only matters if the placed-buffer path were ever enabled on arm64. [V: read, ran]
4. **Window bridge** (Q4 items 1 to 3, about three files), then a native draw test (a copy of `tests/dx11/dx11_triangle.cpp`
   with the Win32 parts replaced by SDL3, compiled against the dylibs) to prove clear, draw, resize, present and read-back
   before any recompiled code is involved. Verify the main-thread requirement with the real SDL3 event pump.
5. **COM proxy generator inputs** (RUNTIME.md 3.7): generate from the 123 interface headers (734 own methods); special cases:
   the 8 methods with `FLOAT`/`LUID` arguments (4 matter), the 6 `WCHAR`-struct conversions, `HWND` to layer mapping
   at the places listed in Q2, the host-to-guest `AddRef`/`Release` for `SetPrivateDataInterface`, one proxy per (native pointer,
   interface) because DXMT hands out different sub-objects for different IIDs (`d3d11_device.cpp:1177-1182` returns
   `&d3d11_device_` for `ID3D11Device`, `this` for `IDXGIDevice`).
6. **`D3DReflect` and `D3DGetBlobPart`** as guest-ABI native objects (Q5), reusing `libs/DXBCParser` for the container.
7. **Shader cache plan**: warm the sqlite cache by running the game, or write a driver around `SM50Compile`; set
   `DXMT_SHADER_CACHE_PATH` explicitly.
8. **Coverage gaps from the vtable trace** of the real renderer (Q6 table): implement only what the trace hits.

## Tool installation needed (for approval)

- `meson` >= 1.3.0: Homebrew `meson` **1.12.1** (depends on `ninja` and `python@3.14`, installed); upstream CI uses
  `pip3 install meson==1.10.0`.
- `ninja`: Homebrew `ninja` **1.13.2** (bottle `arm64_golden_gate` exists).
- **LLVM 15.0.7** with static libs: Homebrew `llvm@15` **15.0.7** (keg-only; depends on `zstd`, installed with `libzstd.a`;
  use `-Dnative_llvm_path=/opt/homebrew/opt/llvm@15`, a path DXMT special-cases, `src/airconv/darwin/meson.build:17-23`). Only an
  `arm64_sonoma` bottle is listed; whether Homebrew pours it on macOS 27 or builds from source is **[?]**. The alternative is the
  CI recipe (`ci.yml:249-298`: tag `llvmorg-15.0.7`, `-DLLVM_TARGETS_TO_BUILD=""`, assertions on, no zstd, arm64, plus
  `-D_LIBCPP_KEEP_TRANSITIVE_INCLUDES_LLVM23`) which needs `cmake` **4.4.4** (>= 3.27) and tens of GB of disk. This Mac has
  34 GiB free of 460 (93% used), so prefer the bottle.
- **Metal Toolchain**: `xcodebuild -downloadComponent MetalToolchain` (a download by Xcode, size not known).
- Already present: Xcode 27.0, Apple clang 21.0.0, macOS 27 SDK, git 2.55.0, `xxd`, `pkg-config`, `zstd` with static lib, SDL3 3.4.14.
- Not needed: Wine, mingw/llvm-mingw, the `external/nvapi` submodule.

## What I could not determine

- Whether the unmodified native build compiles, links and runs: no build. So `nm`/`otool` facts (exported symbols, install
  names, rpaths, dependencies), the adapter name format and byte-order mark, and the reported feature level on this Mac are all
  unobserved.
- Whether LLVM 15's headers compile with Xcode 27's libc++ (upstream added `_LIBCPP_KEEP_TRANSITIVE_INCLUDES_LLVM23` to its LLVM
  builds on 2026-09-28; its DXMT job uses Xcode 16.1), and whether Homebrew's `llvm@15` installs on macOS 27.
- Whether upstream's last native CI run is green (not looked up).
- Which thread winemac creates its Metal view on (Wine source not in the clone), whether SDL3's event pump services the
  main dispatch queue for `execute_on_main`, and how SDL's own `drawableSize` updates interact with DXMT's layer settings.
- Whether `MTLHUDService` is merely not loaded yet on macOS 27 or gone.
- Which of the stubbed or missing D3D11 features the game's renderer calls, whether airconv translates every shader the game
  ships, and performance: all need the game's vtable-call trace and the shader set.
- Upstream PR #133: current state not checked; only its head commits were read.
