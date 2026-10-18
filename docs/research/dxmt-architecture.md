# DXMT's architecture, read as a plan for a native (Wine-free) build

Research note on real rendering through DXMT. Public
sources only; no game file was involved, nothing was built or run.

**Subject.** DXMT, `https://github.com/3Shain/dxmt`, full (not shallow) clone at commit
**`e94c312f5c054263acf261cfa109edf13e757587`** (2026-10-06, "fix(d3d11): avoid deadlock on reentrant
`ResizeTarget()`"), which is also the commit the project built and patched. Submodule `include/native/directx` = `misyltoad/mingw-directx-headers` at
`9df86f2341616ef1888ae59919feaa6d4fad693d`; `external/nvapi` not fetched. Web pages read: the issue
tracker (#56, #204 and the open/closed lists; #21, #26, #28, #35, #151, #152, #230 and others as
listed), the releases page and the wiki page "Device System Runtime Specifications". None of them
contained instructions addressed to a reader or an AI.

**How statements are marked.** Every statement names what it rests on and how:

- **source** — read in the source at `e94c312`; paths are relative to the DXMT repository root,
  `path:line` or `path:first-last`;
- **documented** — a documentation file in that repository (`docs/*.md`, `README.md`, `CONTRIBUTING.md`)
  or a public web page, with its URL;
- **inference** (also written "my inference") — my reasoning from what is cited next to it.

Where a statement rests on this project's own earlier work, it says so. Descriptions are in my own words; no code is copied.

**How it was done.** I read the build files, the Wine boundary, the window and presentation code and the
native-build differences myself; three reading passes over the object model, the shader translator and
coverage/risk were delegated to sub-agents working on the same clone and their key claims were
spot-checked against the source before use (the release-mode drop of unknown opcodes, the pipeline hash,
the chunk back-pressure, the frame-latency default, the map-on-default contradiction, the adapter memory,
issues #56 and #204).

## Summary

1. DXMT is five products of one meson tree: `d3d11` (with the D3D10 front end), `dxgi`, `d3d10core`, a static
   backend `dxmt`, and `winemetal` (the only code that talks to Metal, plus the LLVM-15-based shader
   translator `airconv`). In the Wine build the first four are PE DLLs and `winemetal` is split into a PE
   thunk DLL and a macOS `.so` joined by **Wine's unix-call interface: 145 numbered functions** taking one
   parameter struct. The native build (any non-cross build) keeps the same table and calls it directly.
2. A Direct3D call becomes a **closure appended to one of 32 command chunks**; a per-device encode thread
   turns chunks into Metal command buffers and encoders, a finish thread retires them. The caller blocks only
   when 31 chunks are in flight, in staging `Map`, in `Present`'s frame-latency wait (default 3 frames), and
   when it waits for the GPU. The device lock is off unless the application turns it on.
3. Shaders: DXBC → AIR (LLVM 15, typed pointers) → a metallib DXMT writes itself → `newLibraryWithData:`,
   compiled **per pipeline variant** on a worker pool, cached in SQLite, waited for on the encode thread.
   Geometry shaders and tessellation run as mesh pipelines. No `D3DReflect`/`D3DGetBlobPart`. Unknown
   opcodes are silently dropped in release builds.
4. Window: the swap chain needs a `CAMetalLayer` that the Wine build gets from Wine's Mac driver by symbol
   lookup; natively that lookup fails and the constructor **aborts**; the window/monitor layer is constants.
   Vsync is `presentDrawable:afterMinimumDuration:`, not the layer's display sync. Layer setters
   `dispatch_sync` to the main thread.
5. Native and Wine-free: DXMT already is, as a library for arm64 macOS callers; the work is (a) proxies that
   convert **Windows x64 COM calls to AAPCS64 C++ virtual calls** with `WCHAR`/`HWND` translation and
   per-interface identity, and (b) about five DXMT files (the two window functions in `winemetal_unix.c`,
   the two headless `wsi` files, the semaphore stubs, `getExePath`). The game uses no deferred contexts,
   tessellation, stream output or predication, so none of those paths needs testing.
6. Risk: 19 `UNIMPLEMENTED` sites that `abort()` (none known to be on this game's path; staging
   `UpdateSubresource` and BC↔non-BC staging copies are the ones to log); open issue #204 (per-draw
   `UpdateSubresource` into a default constant buffer is quadratic); first-use pipeline stalls; a
   one-maintainer project, very active (≈ 50–90 commits a month since July), last release v0.80
   (2026-04-23), LGPL since, no AI-written contributions accepted.

## 1. The layout

### 1.1 Components and what each is built into

| Component (directory) | What it is | Built into (native build) | Built into (Wine build) | Mark |
|---|---|---|---|---|
| `src/d3d11` (26 sources) and `src/d3d10/d3d10_*.cpp` (6) | the Direct3D 11 front end (device, contexts, resources, views, states, shaders, pipelines, queries, swap chain) and the Direct3D 10 front end layered on it | `d3d11.dylib` | `d3d11.dll` (PE) | source: `src/d3d11/meson.build:3-59` |
| `src/d3d10/d3d10core.cpp` | thin `D3D10CoreCreateDevice` entry | `d3d10core.dylib` | `d3d10core.dll` | source: `src/d3d10/meson.build:3-19` |
| `src/dxgi` (5 sources) | factory, adapter, output, options, the `CreateDXGIFactory*` exports | `dxgi.dylib` | `dxgi.dll` | source: `src/dxgi/meson.build:3-23` |
| `src/dxmt` (24 sources, one `.metal`) | the backend: Metal device wrapper, command queue and encode/finish threads, encoding context, buffers/textures/allocations, staging, presenter, occlusion and timestamp counters, fences, shader cache, MetalFX scaler, internal Metal shaders | static library `libdxmt`, linked into `dxgi`, `d3d11` (and `d3d12`) | the same, inside the PE DLLs | source: `src/dxmt/meson.build:1-46` |
| `src/util` | logging, config file, COM helpers (`com_private_data`), threads, strings, hashing (SHA-1, MD5, bloom), window-system layer (`wsi_*`), Win32 compatibility stubs | static library `libutil` | the same | source: `src/util/meson.build:1-48` |
| `src/airconv` | DXBC to AIR (Apple's LLVM-based shader IR) translator and metallib writer | static library `libairconv` (built against LLVM 15) linked into `winemetal.dylib`; also the `airconv` command-line tool | linked into the **unix** side `winemetal.so` (built natively for macOS) | source: `src/airconv/meson.build:1-111`, `src/airconv/darwin/meson.build:25-45`, `src/winemetal/unix/meson.build:27-38` |
| `libs/DXBCParser` | Microsoft's DXBC container and token parser (MIT) | static `DXBCParserNative` | static `DXBCParser` (PE side) and `DXBCParserNative` (unix side) | source: `libs/DXBCParser/meson.build:1-39`, `ShaderBinary.h:1-2` |
| `src/winemetal/winemetal_thunks.c` | the C API the front end calls for every Metal operation (135 exported functions: `NSObject_retain`, `MTLDevice_newBuffer`, `MetalLayer_nextDrawable`, ...); each packs its arguments into a struct and makes a "unix call" by number | compiled into `winemetal.dylib` | compiled into `winemetal.dll` (PE) | source: `winemetal_thunks.c:1-1333` (135 `WINEMETAL_API` definitions, counted) |
| `src/winemetal/airconv_thunks.c` | the same for the 12 `SM50*` compiler entry points | **not built** (native code calls the compiler directly) | `winemetal.dll` | source: `src/winemetal/meson.build:1-5`; `src/nativemetal/meson.build:1-5` |
| `src/winemetal/unix/winemetal_unix.c`, `cache.c` | the Objective-C side: one function per unix call that sends the actual Metal / QuartzCore / AppKit / CoreGraphics / ColorSync messages; the sqlite shader cache | `winemetal.dylib` (with `-DDXMT_NATIVE=1`) | `winemetal.so` (Mach-O, loaded by Wine) | source: `src/nativemetal/meson.build:1-23`, `src/winemetal/unix/meson.build:1-38` |
| `src/winemetal/Metal.hpp` | C++ wrapper classes (`WMT::Device`, `WMT::CommandBuffer`, ...) over the C API, used by `src/dxmt` and the front end | header only | header only | source: `src/winemetal/Metal.hpp` (1231 lines) |
| `src/d3d12`, `src/nvapi`, `src/nvngx` | optional, off by default | only with `-Denable_d3d12/nvapi/nvngx` | idem | source: `meson.options:9-11`, `src/meson.build:16-26` |
| `tests/dx11` | Win32 test programs | only with `-Denable_tests` (cross builds) | idem | source: `meson.build:208-210`; the DXMT measurement |

### 1.2 Build system and options

- Meson ≥ 1.3, C++20, `b_ndebug=if-release` (asserts vanish in release builds) — source: `meson.build:1-6`.
- The build is "native" exactly when it is **not** a cross build: `dxmt_crossbuild = meson.is_cross_build()`; the old `dxmt_native` option is deprecated — source: `meson.build:10`, `meson.options:4`.
- Options: `native_llvm_path` (LLVM 15 prefix, default `/usr/local/opt/llvm@15`), `build_airconv_for_windows`, `dxmt_debug` (adds `-DDXMT_DEBUG=1`), `wine_build_path` / `wine_install_path` (one is required for a cross build), `wine_builtin_dll`, `enable_tests`, `enable_nvapi`, `enable_nvngx`, `enable_d3d12` — source: `meson.options:1-11`, `meson.build:134-138`, `src/winemetal/meson.build:11-35`.
- Native builds add `include/native/windows` (DXMT's own minimal Windows type header) and `include/native/directx` (mingw-w64 DirectX headers) to the include path instead of mingw's — source: `meson.build:24-31`.
- Build-time tools: `xcrun metal` / `metallib` compile the internal `.metal` files (presenter, clears, tessellation helpers), `xxd` embeds them as C arrays, `git describe` stamps `version.h` — source: `meson.build:115-146`, `src/dxmt/meson.build:28-36`, `src/airconv/meson.build:65-77`. None of these runs at run time (inference, consistent with the DXMT measurement).
- `-DDXMT_PAGE_SIZE=4096` is hard-coded — source: `meson.build:140`.
- A cross build links every PE `-static` and post-processes each DLL with `winebuild --builtin` — source: `meson.build:79-87`, e.g. `src/d3d11/meson.build:66-74`.

### 1.3 Third-party dependencies and licences

| Dependency | Where | Licence | Mark |
|---|---|---|---|
| DXMT itself | whole tree | LGPL 2.1 or later (releases up to v0.80 were MIT) | source: `LICENSE:1-12`, `LICENSE.OLD:1-5` |
| Code derived from DXVK (35 files mention DXVK, e.g. `util/thread.hpp`, `wsi_*`) | `src/util`, parts of `src/d3d11` | zlib/libpng | source: `src/util/dxvk.LICENSE`, `src/util/thread.hpp:1-9` |
| DXBCParser | `libs/DXBCParser` | MIT, Microsoft | source: `libs/DXBCParser/ShaderBinary.h:1-2` |
| mingw-w64 DirectX headers (submodule `include/native/directx`, `misyltoad/mingw-directx-headers` at `9df86f2`) | native builds | LGPL 2.1+, with the note that using the headers does not bring the user's code under it | source: `include/native/directx/README.md` (submodule at `9df86f23`), `.gitmodules` |
| LLVM 15.0.x (static: Support, Core, BitWriter, Passes, ...) | `airconv`, inside `winemetal` | Apache-2.0 with LLVM exceptions | list: source `src/airconv/meson.build:24-38`; licence: my inference (LLVM's own licence, not in this repository) |
| MD5 code | `src/util/util_md5.cpp` | MIT, Philip Rebohle | source: `util_md5.cpp:1-5` |
| SHA-1 | `src/util/sha1/sha1.c` | public domain (Steve Reid, via OpenBSD) | source: `sha1.c:1-5` |
| SHA-256 | `src/airconv/sha256.hpp` | Brad Conte's code with a disclaimer, no licence named | source: `sha256.hpp:1-5` |
| `tl::generator` | `include/tl/generator.hpp` | CC0 | source: header lines 1-5 |
| NVIDIA nvapi headers (submodule `external/nvapi`) | only `-Denable_nvapi` | NVIDIA's (not read) | source: `.gitmodules`, `src/nvapi/meson.build:11` |
| System: Metal, MetalFX (strong link), Foundation, CoreFoundation, CoreGraphics, QuartzCore, ColorSync, Cocoa (weak), `libsqlite3`, `libz`, `libcurses`, `libxml2`; zstd and libunwind static from a Homebrew LLVM | `winemetal` | Apple system libraries | source: `src/nativemetal/meson.build:8-14`, `src/airconv/darwin/meson.build:12-23` |

### 1.4 Windows side and macOS side, and what joins them

- **In the Wine build** the front end (`d3d11.dll`, `dxgi.dll`, `d3d10core.dll`) and `winemetal.dll` are PE DLLs compiled with a mingw cross compiler against mingw's headers; the Metal work happens in `winemetal.so`, a Mach-O built natively for macOS that links Wine's `winemac.so` and `ntdll.so` directly — source: `src/winemetal/meson.build:37-58`, `src/winemetal/unix/meson.build:6-38`.
- **The joint is Wine's unix-call interface.** `winemetal.dll`'s `DllMain` calls `__wine_init_unix_call` — source: `src/winemetal/main.c:5-11`. Each thunk calls `WINE_UNIX_CALL(code, &params)`, which in a PE module is `__wine_unix_call_dispatcher(__wine_unixlib_handle, code, args)` — source: `src/winemetal/wineunixlib.h:304-314`, `winemetal_thunks.c:12-20`. The unix side exports the table `__wine_unix_call_funcs[]`, indexed by `code` — source: `winemetal_unix.c:3166-3313`. A second table, `__wine_unix_call_wow64_funcs[]`, converts 32-bit parameter structs for WoW64 and exists only in the Wine build — source: `winemetal_unix.c:1874-2196` (32-bit converters), `:3315-end`.
- **Every call that crosses**: the table has **146 slots, 145 functions and one empty slot (index 83)**, grouped as: `MTLDevice_*` 36 (object creation: buffers, textures, samplers, depth-stencil states, libraries, pipelines including mesh and tile pipelines, heaps, residency sets, indirect command buffers, shared events/textures, binary archives, capability queries); `MTLCommandBuffer_*` 16 (encoder creation, commit, wait, status, present, MetalFX encode, error/logs, event signal/wait); `SM50*` 12 (the shader compiler, section 3); `MTLTexture_*` 9; `MTLSharedEvent_*` 5; `MetalLayer_*` 5 (`nextDrawable`, `setProps`, `getProps`, `setColorSpace`, `getEDRValue`); `NSString_*` 4; `DeveloperHUDProperties_*` 4; `MTLResidencySet_*` 4; `NSObject_*` 3 (retain, release, description); `MTLBuffer_*` 3; `MTLCaptureManager_*` 3; `SharedEventListener_*` 3; `CacheReader/Writer_*` 4; `MTLCounterSampleBuffer_*` 2; `MTLHeap_*` 2; `NSArray_*` 2; `MTLCommandQueue_*` 2; `MTLLibrary_*` 2; `MTLCommandEncoder_*` 2; one each for the three `encodeCommands` calls (blit, compute, render: whole encoded command streams cross in one call), `MTLCopyAllDevices`, `NSAutoreleasePool_alloc_init`, `MetalDrawable_texture`, `MTLLogContainer_enumerate`, `MTLBinaryArchive_serialize`, `DispatchData_alloc_init`, `CGColorSpace_checkColorSpaceSupported`, the display queries `WMTGetPrimaryDisplayId`, `WMTGetSecondaryDisplayId`, `WMTGetDisplayDescription`, `WMTQueryDisplaySetting`, `WMTUpdateDisplaySetting`, `WMTQueryDisplaySettingForLayer`, `WMTGetOSVersion`, `WMTSetMetalShaderCachePath`, `WMTBootstrapRegister`, `WMTBootstrapLookUp`, and the two window calls `CreateMetalViewFromHWND` and `ReleaseMetalView` — source: `winemetal_unix.c:3166-3313` (counted by script over the table).
- **Objects cross as 64-bit handles** (`obj_handle_t` = `uint64_t`, an Objective-C pointer value), never as PE pointers to Objective-C objects — source: `winemetal.h:28`.
- **In the native build the same thunk file is compiled, but `WINE_UNIX_CALL(code, args)` becomes a direct indexed call through the same table**, `__wine_unix_call_funcs[code](args)`, in the same process and thread — source: `src/nativemetal/wineunixlib.h:1-13`, `src/nativemetal/meson.build:1-5`. So the boundary survives as a C function table but costs one indirect call; there is no thread or ABI switch (inference from the macro).
- **Shader compiler, native**: `airconv_thunks.c` is not compiled; `d3d11` calls the `SM50*` functions exported by `winemetal.dylib`, where `libairconv` is linked — source: `src/nativemetal/meson.build:1-23`, `src/airconv/airconv_public.h:159-167` (`AIRCONV_API` is `__attribute__((sysv_abi))` off Windows; on arm64 that attribute has no effect — my inference).
- **Windows-only code compiled out of the native build**: `DllMain` of `d3d11` and `dxgi` (the latter writes NVIDIA vendor-extension registry keys and sets Metal's shader-cache path) — source: `src/d3d11/d3d11.cpp:231-239`, `src/dxgi/dxgi.cpp:13-74`; `GetModuleFileNameW`, `SetThreadDescription`, `CreateDirectoryW`, `wine_get_unix_file_name` — source: `src/util/util_env.cpp:68-156`; `__wine_dbg_output` — source: `src/util/log/log.cpp:60-66`; the Win32 window system (`wsi_window_win32.cpp`, `wsi_monitor_win32.cpp`, `wsi_platform_win32.cpp`) replaced by `*_headless.cpp` and `wsi_platform_darwin.cpp` — source: `src/util/meson.build:27-39`; D3DKMT functions (shared resources) — source: `src/util/util_d3dkmt.h:517-655`.


## 2. The object model

### 2.1 Device

- Two layers. The backend `dxmt::Device` is a small interface (Metal device, one `CommandQueue`, Metal version, maximum object threadgroups); its one implementation holds the `MTLDevice` and the queue — source: `src/dxmt/dxmt_device.hpp:7-15`, `dxmt_device.cpp:50-85`. The front end's COM object is `MTLD3D11DXGIDevice`, which owns the adapter, the backend device, and **by value** the `MTLD3D11DeviceImpl` — source: `src/d3d11/d3d11_device.cpp:1133-1143,1307-1312`.
- `QueryInterface` on that container returns **different sub-objects**: itself for `IDXGIDevice`..`3` and DXMT's `IMTLDXGIDevice`; the embedded device for `ID3D11Device`..`5` and DXMT's private extension interfaces; a D3D10 sub-object for `ID3D10Device`; an embedded `D3D11Multithread` for `ID3D11Multithread`; the sub-objects forward `AddRef`/`Release` to the container — source: `d3d11_device.cpp:68-75,1163-1213`. (Consequence for a proxy: one proxy per native pointer and interface — inference.)
- `MTLD3D11DeviceImpl` owns the state-object caches (sampler, rasterizer, depth-stencil), the command-list pool, the pipeline cache, the immediate context, the D3D10 device and the multithread object — source: `d3d11_device.cpp:59-66,1113-1124`.
- Creation: `D3D11CoreCreateDevice` requires a DXMT adapter (`IMTLDXGIAdapter`), then builds the backend device on the adapter's `MTLDevice` and the front end around it — source: `src/d3d11/d3d11.cpp:32-111`.
- **Metal objects owned per device** (through its `CommandQueue`): one `MTLCommandQueue` (32 command buffers), one `MTLSharedEvent` for completion, ring allocators for staging (managed, write-combined), scratch (private), argument buffers (shared, write-combined) and CPU command memory, the internal command library, the one `ArgumentEncodingContext`, a `ResourceInitializer` (with its **own second** `MTLCommandQueue` and shared event for initial uploads, which every chunk's command buffer waits on), and a counter pool — source: `src/dxmt/dxmt_command_queue.cpp:36-63,157-159`, `dxmt_command_queue.hpp:91,170-203`, `src/dxmt/dxmt_resource_initializer.cpp:68-69`.

### 2.2 Immediate context and the queue to the encode thread

- One template class for both context kinds, specialised by an internal state; the immediate context's state is the device mutex, the `CommandQueue` and a "dirty" flag — source: `src/d3d11/d3d11_context_impl.cpp:626,5137-5164`, `d3d11_context_imm.cpp:11-15`; binding state: `d3d11_context_state.hpp:16-186`.
- **A Direct3D call records a C++ closure** taking the encoding context, constructed in place in CPU memory from the queue's ring allocator and appended to the current chunk's singly linked list — source: `d3d11_context_imm.cpp:24-39`, `dxmt_command_queue.hpp:102-106`, `src/dxmt/dxmt_command_list.hpp:98-122`. The calling thread also tracks which kind of pass is open (render, compute, blit kinds) and emits pass begin/end records — source: `d3d11_context_impl.cpp:3379-3390,4306-4348,4415,4576-4616`.
- **Chunks**: a ring of 32 `CommandChunk`s. `CommitCurrentChunk` stamps the chunk (id, completion-event value, frame), publishes it to the encode thread with an atomic counter plus `notify_one`, then **blocks the caller while 31 chunks are in flight** — source: `dxmt_command_queue.cpp:93-118`, `dxmt_command_queue.hpp:91,209-213`. The queue is documented in the header as not thread-safe — source: `dxmt_command_queue.hpp:245-250`. Encoding is asynchronous by a compile-time switch (`ASYNC_ENCODING 1`) — source: `dxmt_command_queue.cpp:27,103-115`.
- **Encode thread** (`dxmt-encode-thread`, one per device): waits on the counter, opens an autorelease pool, creates the `MTLCommandBuffer`, runs the chunk's closures against the `ArgumentEncodingContext` (which builds intermediate encoder records), then `flushCommands` reorders/merges encoders using fence relations, creates the real render/compute/blit encoders, encodes the present, signals the shared event at the chunk's value, and commits — source: `dxmt_command_queue.cpp:120-185`, `src/dxmt/dxmt_context.hpp:124-278`, `src/dxmt/dxmt_context.cpp:864-1269` (encoders at `:965,1060,1079`, present `:1087-1106`, signal `:1264`). So command buffers, encoders, pipeline lookups and drawables are touched only on this thread (inference from these lines).
- **Finish thread** (`dxmt-finish-thread`): waits for each command buffer (`waitUntilCompleted`), logs errors, signals the frame-latency fence, destroys the chunk's closures and releases the allocations it kept alive, signals the CPU-coherence fence with the chunk's sequence number, frees ring memory — source: `dxmt_command_queue.cpp:187-225`; allocation retention: `dxmt_context.cpp:461-465`, `dxmt_command_queue.cpp:227-235`, `src/dxmt/dxmt_allocation.hpp:26-66`.
- **Waiting** is C++20 `std::atomic::wait`/`notify` on monotonic 64-bit counters (`CpuFence`) — source: `src/util/util_cpu_fence.hpp:7-35`, `dxmt_command_queue.hpp:279-282`.
- **Other threads**: a shared-event listener thread per queue (returns at once in native builds), the pipeline/shader workers (2 up to 2 × cores), Metal's own — source: `dxmt_command_queue.cpp:41-42`, `winemetal_unix.c:2602-2624`, `src/dxmt/dxmt_tasks.hpp:48-56,72-144`. Threads are `std::thread`, locks `std::mutex` off Windows — source: `src/util/thread.hpp:325-362`.

### 2.3 Deferred contexts

- A deferred context uses a null mutex and records into its own `MTLD3D11CommandList` (own command list, staging ring and CPU ring) — source: `d3d11_context_def.cpp:16-43,470`, `d3d11_context_impl.cpp:5423-5432,5500-5555`. `Map(WRITE_DISCARD)` takes a fresh allocation and records a rename; `NO_OVERWRITE` without a prior discard fails; queries are recorded as lists; `GetData` returns `E_FAIL`; `Flush` does nothing — source: `d3d11_context_def.cpp:58-66,109-240,285-388`. `FinishCommandList` hands over the list and takes a new one from a locked pool — source: `:398-420,486-512`.
- `ExecuteCommandList` on the immediate context resets state, sets up occlusion queries, publishes the list's latest dynamic allocations as the current names, and emits one closure that replays the list on the encode thread (in place, so a list can be executed more than once — inference) — source: `d3d11_context_imm.cpp:508-569`, `d3d11_context_impl.cpp:5525-5527`.

### 2.4 Resources

- `dxmt::Buffer` and `dxmt::Texture` are logical objects pointing at a current **allocation**; `rename()` swaps the allocation — source: `src/dxmt/dxmt_buffer.cpp:210-215`, `dxmt_texture.cpp:324-329`. Storage flags map to Metal storage modes: shared by default (hazard tracking off), private, managed, with an optional write-combined CPU cache mode; small buffers can be sub-allocated from one page — source: `dxmt_buffer.cpp:52-66,171-188`, `dxmt_buffer.hpp:32-44`, `dxmt_texture.cpp:176-203`, `dxmt_texture.hpp:31-42`.
- **Buffers**: dynamic → write-combined and sub-allocated; immutable → read-only; with initial data → managed; GPU-written without data → private, zero-filled through the resource initializer; staging → a separate staging buffer class; every buffer without output binds also gets a `DynamicBuffer` renamer — source: `src/d3d11/d3d11_buffer.cpp:85-119`, `d3d11_device.cpp:89-96`.
- **Textures**: default/immutable → `MTLTexture` (private, or managed with CPU access); dynamic → a linear texture made on an `MTLBuffer` when possible, else a managed texture plus one `DynamicBuffer` per subresource uploaded at `Unmap`; staging → one staging buffer per subresource — source: `d3d11_texture_device.cpp:461-473`, `d3d11_device.cpp:851-856`, `d3d11_texture_linear.cpp:48-58`, `d3d11_texture_dynamic.cpp:103-152`, `d3d11_resource_staging.cpp:99-137`.
- **`WRITE_DISCARD` renaming**: a `DynamicBuffer` keeps a FIFO of retired allocations, each reusable once the CPU-coherence fence passes the sequence at which it was retired; the immediate context first tries the next sub-allocation in the page, else takes a new allocation, returns its pointer to the caller at once and emits the rename for the encode thread — source: `src/dxmt/dxmt_dynamic.cpp:21-63`, `d3d11_context_imm.cpp:119-134`.
- **Staging**: each staging resource tracks when the CPU may read and until when the GPU uses it; mapping answers "mappable", "wait n sequences" or "renamable" — source: `src/dxmt/dxmt_staging.hpp:78-82`, `dxmt_staging.cpp:43-55`.

### 2.5 Views, states, input layouts, shaders, pipelines

- Views keep raw resource pointers and a view key; the Metal texture views are created lazily per allocation — source: `src/d3d11/d3d11_view.hpp:39-185`, `dxmt_texture.cpp:40-57,99-134`. UAVs carry a counter object; depth views a renamable texture pool — source: `d3d11_view.hpp`.
- Sampler → two `MTLSamplerState`s (normal, cube); depth-stencil → three `MTLDepthStencilState` variants; rasterizer → no Metal object, fills render commands; blend → fills the pipeline descriptor; all cached by descriptor under a mutex — source: `src/dxmt/dxmt_sampler.cpp:22,40`, `d3d11_state_object.cpp:485-555,616-641`, `d3d11_state_object.hpp:33,49,158-168`.
- Input layouts, shaders (by SHA-1) and graphics/geometry/tessellation/compute pipelines are cached in the pipeline cache, each map under its own mutex; pipelines are built by worker tasks (section 3.3) — source: `d3d11_pipeline_cache.cpp:17-22,198-240,380-446`, `d3d11_pipeline.cpp:49-99`.

### 2.6 How `Map`, `UpdateSubresource`, copies, queries and `Present` are carried

- **`Map`** (immediate): dynamic buffer `WRITE_DISCARD` renames (and marks vertex/constant/SRV bindings dirty), `NO_OVERWRITE` returns current memory, read/write maps on dynamic resources are `E_INVALIDARG`; dynamic textures discard only; staging resources loop: renamable → new allocation plus copy plus rename; otherwise `DO_NOT_WAIT` → `DXGI_ERROR_WAS_STILL_DRAWING`, else **flush and block the caller** on the CPU-coherence fence — source: `d3d11_context_imm.cpp:80,119-279`.
- **`UpdateSubresource`**: buffers with a renamer go through the internal map path; others and textures are copied on the calling thread into the staging ring and an upload blit is recorded — source: `d3d11_context_impl.cpp:1218-1258,4219-4245`. (This is where issue #204's per-draw rename cost comes from: section 6.3.)
- **Copies**: per-subresource buffer/texture copy records; a copy into staging sets "promote flush" so the chunk is committed at the next pass change — source: `d3d11_context_impl.cpp:1052-1095,3835-3870,3957-3992`.
- **Queries**: event and timestamp-disjoint `End` allocate a shared-event value if work was recorded and set promote-flush; `GetData` compares with the shared event's signalled value and never blocks (after 64 polls it forces a flush; with `DONOTFLUSH` it does not flush) — source: `d3d11_context_imm.cpp:350-367,415,471-474`, `d3d11_query.hpp:118,139-155`. Occlusion results are read back when the finishing chunk is reset; timestamps use Metal counter sample buffers — source: `src/dxmt/dxmt_occlusion_query.hpp:159-260`, `dxmt_context.cpp:898-904,1217-1245`.
- **`Flush`**: commits the chunk if anything is pending; `Flush1` with an event handle uses a Win32-event hook that is a no-op natively — source: `d3d11_context_imm.cpp:486-500,587-593`, `winemetal_unix.c:2602-2607`.
- **`Present`**: under the device mutex, end the pass, tag the chunk for the frame-latency fence, optionally wait on the waitable-object fence, record one closure that presents on the encode thread, commit, release the mutex, then **block in `PresentBoundary`** until frame N − max latency (default **3**, set by `IDXGIDevice1::SetMaximumFrameLatency`) has finished — source: `d3d11_swapchain.cpp:726-734,739-818`, `dxmt_command_queue.hpp:173,255-273`, `d3d11_device.cpp:1260-1263`. Section 4.3 has the encode-thread side.

### 2.7 Locking

- The device mutex is a recursive spin lock that **does nothing unless the application calls `ID3D11Multithread::SetMultithreadProtected(TRUE)`** (default off; only the D3D10 path turns it on) — source: `src/d3d11/d3d11_multithread.cpp:9-37`, `d3d11_multithread.hpp:16`, `src/d3d10/d3d10core.cpp:35`. Context entry points and `Present` take it; `GetData` does not — source: e.g. `d3d11_context_impl.cpp:686`, `d3d11_context_imm.cpp:140,290,488`. So a caller that uses the immediate context from two threads without that call races on the current chunk (inference); whether the game touches the immediate context from more than one thread is not established in this note.

## 3. Shaders

### 3.1 From Direct3D bytecode to a Metal function

- **Creation is cheap and deduplicated.** Every `Create*Shader` goes to one helper that hashes the bytecode (SHA-1), returns the existing object for a known hash, and otherwise calls `SM50Initialize` — source: `src/d3d11/d3d11_pipeline_cache.cpp:220-320`. A class-linkage argument is logged as unsupported and ignored — source: `src/d3d11/d3d11_device.cpp:242-255`.
- **`SM50Initialize`** parses the DXBC container with Microsoft's `DXBCParser`, takes the `SHEX` chunk (else `SHDR`), requires the input and output signatures, builds a control-flow graph of basic blocks, assigns argument-table slots to constant buffers, samplers, SRVs and UAVs, and fills DXMT's own binding description `MTL_SHADER_REFLECTION` — source: `src/airconv/dxbc_converter.cpp:984-1316`. Slot formulas (constant buffer = slot; sampler = slot+32; SRV = slot×3+128; UAV = slot×3+512) — source: `src/airconv/airconv_public.h:176-188`. Bindings are delivered through two buffers at fixed indices (29 constant-buffer table, 30 argument table) — source: `dxbc_converter.cpp:1235-1238`.
- **Nothing is compiled at creation.** A Metal function is produced per *variant* when a pipeline first needs it (3.3).
- **`SM50Compile`** (and the stage-pair functions for tessellation and geometry) make a fresh `LLVMContext` with **typed pointers** (opaque pointers switched off), set up a module with triple `air64-apple-macosx14.0.0`, Apple's AIR data layout and `air.*` module flags, translate instruction by instruction into LLVM IR that calls AIR intrinsics by name (`air.sample_*`, `air.read_*`, `air.write_*`, `air.atomic*`, ...), link small helper modules only when needed (MSAD, sample positions, the tessellator), run a hand-picked optimisation pipeline (SROA, EarlyCSE, inlining, IPSCCP, GlobalOpt, mem2reg, InstCombine, SimplifyCFG and two DXMT passes), and write a metallib — source: `dxbc_converter.cpp:1341-1655,1362,1379-1397,1442-1462`; `src/airconv/airconv_context.cpp:40-63,67-156,165-201`; `src/airconv/nt/air_builder.cpp` (intrinsic names). Vertex, pixel and compute shaders convert alone; hull, domain and geometry shaders only as part of a pair; anything else is `UnsupportedFeature` — source: `dxbc_converter.cpp:863-899`.
- **AIR version**: AIR 2.7 / Metal 3.2 (macOS 15 triple) when the device's Metal version is 3.2, else AIR 2.6 / Metal 3.1 — source: `dxbc_converter.cpp:204-232`.
- **The metallib container is written by DXMT itself**, without Apple tools: bitcode via LLVM's `WriteBitcodeToFile`, a SHA-256 per function, and the tag list (name, type, hash, sizes, offsets, version; vertex attribute tables; a tessellation tag) with a header for platform macOS 14.4 — source: `src/airconv/metallib_writer.cpp:30-399`, `metallib_writer.hpp:26-56`. The per-function version tag is fixed at AIR 2.6 / MSL 3.1 even for Metal 3.2 modules (source: `metallib_writer.cpp:42-369`); that Metal accepts this is my inference from the project's runs.
- **Loading**: a worker task asks the disk cache first; on a miss it runs the compile closure, wraps the bytes in a `dispatch_data`, calls `MTLDevice newLibraryWithData:` and `newFunctionWithName:` — source: `src/d3d11/d3d11_shader.cpp:48-102`. The GPU driver then compiles AIR to machine code when the pipeline state is created (my inference; standard Metal behaviour, timed by the project at about 90 ms for a tiny pipeline, the DXMT measurement).

### 3.2 What it links against

- **LLVM 15, exact major** (docs: llvmorg-15.0.7, `LLVM_TARGETS_TO_BUILD=""`), about 34 static LLVM libraries (Passes, Core, BitWriter, BitReader, IRReader, Linker, InstCombine, ScalarOpts, ipo, Vectorize, Analysis, Support, ...) plus `-lm -lz -lcurses -lxml2` — documented: `docs/DEVELOPMENT.md:8,29-43`; source: `src/airconv/meson.build:24-38`, `src/airconv/darwin/meson.build:12-15`. Built `-fno-exceptions -fno-rtti` — source: `src/airconv/meson.build:40-50`.
- The typed-pointer calls (`setOpaquePointers(false)` at every entry point, `getNonOpaquePointerElementType`) tie it to an LLVM that still has typed pointers — source: `dxbc_converter.cpp:1362,1423,1487,1552,1615`, `dxbc_converter_basicblock.cpp:137,153`; that this means LLVM 16 or older is my inference (typed pointers were removed in LLVM 17).
- **Apple's toolchain is a build-time dependency only**: `xcrun metal` compiles the three helper `.metal` files (`air_msad`, `air_samplepos`, `air_tessellation`) to AIR, `xxd` embeds them, and they are linked into converted shaders at run time with `Linker::linkModules(..., LinkOnlyNeeded)` — source: `src/airconv/meson.build:65-77`, `airconv_context.cpp:29-31,165-201`. Run time needs only Metal.framework (inference from 3.1).
- **Wine build vs native**: in the Wine build the PE side reaches the compiler through 12 unix calls (`airconv_thunks.c`), with 32-bit conversion of the linked argument chain for WoW64; in the native build `d3d11` calls the `SM50*` symbols of `winemetal.dylib` directly — source: `src/winemetal/airconv_thunks.c:5-169`, `winemetal_unix.c:1740-1870,1897-1905`, `src/nativemetal/meson.build:1-23`.

### 3.3 What is specialised per pipeline

- The compile call takes a linked list of argument structs: common (Metal version, NaN-to-zero sampling, FMA defusing); pixel-shader PSO data (sample mask, dual-source blending, depth-output disable, a mask of render targets that are 8-bit UNORM, pixel formats); input-assembler layout (index format, slot mask, per-element register/slot/offset/format/step) for **vertex pulling**; GS pass-through (render-target/viewport array index registers, rasterisation disabled); GS strip topology; tessellator maximum factor; vertex stream-output emulation; and a root-signature block used by D3D12 — source: `airconv_public.h:197-312`. Direct3D 11 leaves the pixel-format array zero and passes only the UNORM mask — source: `d3d11_shader.cpp:186`, `src/d3d11/d3d11_pipeline.cpp:22-26`.
- Variant kinds: vertex, pixel, default (compute), tessellation vertex+hull, tessellation domain, vertex with stream output, geometry vertex, geometry — source: `src/d3d11/d3d11_shader.hpp:89-186`. Each variant is identified by a SHA-1 over the global flags and its own fields (partner shaders' hashes, the input-layout digest); the Metal function name is a stage prefix, the first 8 hex digits of the bytecode hash and the variant digest — source: `d3d11_shader.cpp:119-443`; input-layout digest: `d3d11_pipeline_cache.cpp:163-174`.
- A **pipeline** key holds the shaders, blend state, input layout, stream-output layout, render-target and depth formats, topology class, index format, sample count and mask, GS strip flag and pass-through flag (the in-memory hash leaves the blend state out, equality does not) — source: `src/d3d11/d3d11_pipeline.hpp:10-73`.
- **Where it compiles**: pipeline lookups insert a task and submit it to a scheduler; shader-variant tasks are created lazily and a pipeline task returns the shader task it depends on, which the scheduler parks and requeues — source: `d3d11_pipeline_cache.cpp:115-126,380-448`, `src/dxmt/dxmt_tasks.hpp:48-56,98-127,138-141`, `d3d11_pipeline.cpp:60-65`. Workers: 2 at start, up to twice the core count, asking for time-critical priority (a stub in native builds) — source: `dxmt_tasks.hpp:48-56,75`.
- **Where it waits**: the command recorded for a draw fetches its pipeline on the encode thread and blocks on the pipeline's ready flag (the call sites are commented "may block") — source: `d3d11_pipeline.cpp:49-52,153-156`, `d3d11_pipeline_gs.cpp:65-68`, `d3d11_pipeline_ts.cpp:93-97`, `src/d3d11/d3d11_context_impl.cpp:4737,4785,4841,5001`. So a first-use stall lands on the encode thread and reaches the game thread only when the chunk queue fills (inference; consistent with the DXMT measurement).
- In-memory hashing of shader variants returns only the variant kind (marked FIXME), so all variants of a kind share one hash bucket — source: `d3d11_shader.hpp:228-233`; the cost of that is my inference (lookup cost grows with variants per shader, not correctness).

### 3.4 Caching on disk

- **DXMT's cache**: SQLite, one database per Metal version, `shaders_<310|320>.db`, under `$(DARWIN_USER_CACHE_DIR)/dxmt/<exe>/` or the absolute `DXMT_SHADER_CACHE_PATH`; `DXMT_SHADER_CACHE=0` turns it off; table `cache_<AIRCONV_VERSION>` (26 at this commit), key = (bytecode SHA-1, variant SHA-1), value = metallib bytes; WAL mode; a cached library that fails to load is recompiled — source: `src/dxmt/dxmt_shader_cache.cpp:21-34`, `dxmt_shader_cache.hpp:8`, `airconv_public.h:8`, `src/winemetal/unix/cache.c:22-153`, `d3d11_pipeline_cache.cpp:147-157`, `d3d11_shader.cpp:54-71,90`. The native build's empty executable name collapses `<exe>`.
- **Metal's own cache directory** is redirected with the non-public `MTLSetShaderCachePath` from `dxgi`'s `DllMain`, Windows builds only, unless `DXMT_USE_DEFAULT_METAL_CACHE=1` — source: `src/dxgi/dxgi.cpp:53-61`, `cache.c:224-238`.
- **Binary archives** (`MTLBinaryArchive`) are wired up in `winemetal` but nothing in `src/d3d11` or `src/dxmt` uses them at this commit — source: `winemetal_unix.c:386-394,568-579,638-640`; "unused" from a grep with no caller.
- **Ahead of time**: the `airconv` tool converts one DXBC file to AIR, LLVM text or a metallib (Metal 3.2, function name `shader_main`, stage pairs by option) but takes no input layout, pixel or stream-output data, so it cannot produce DXMT's variants, function names or cache keys — source: `src/airconv/airconv_cli.cpp:27-53,144-160,218-246,366-403`. Its use for us is a **conversion sweep** (does every game shader translate?), not cache filling (as the project concluded, the DXMT measurement).

### 3.5 Geometry shaders and tessellation

- **Geometry shaders run as Metal mesh pipelines**: the vertex shader (with vertex pulling) becomes the object function and the geometry shader the mesh function, with a 16,256-byte payload; GS instancing uses the mesh grid's second dimension; output topologies other than triangle strip, line strip and point list fail — source: `src/d3d11/d3d11_pipeline_gs.cpp:99-108`, `src/airconv/dxbc_converter_gs.cpp:150-157,515,873,881`. A geometry shader that only passes vertices through is detected and folded into the previous stage — source: `dxbc_converter.cpp:923-982,1297-1305`.
- **Tessellation also runs as a mesh pipeline**: vertex+hull shader as the object function, domain shader as the mesh function, with DXMT's own fixed-point tessellator written in Metal — source: `d3d11_pipeline_ts.cpp:128-142`, `src/airconv/shaders/air_tessellation.metal`. Limits: at most 32 hull threads per patch, payload ≤ 16 KiB, estimated mesh ≤ 32 KiB, maximum tess factor 8 on GPUs below family Apple9 unless `d3d11.maxTessFactor` (4–64) says otherwise; pipeline creation is serialised by a global mutex — source: `dxbc_converter.cpp:1251-1296`, `d3d11_pipeline_ts.cpp:55-64,174-178`. Point and isoline tessellator output, and a real geometry shader together with tessellation, are refused and flagged — source: `d3d11_context_impl.cpp:4695-4717`; documented: `docs/COMPATIBILITY_FLAG.md:6-11`.
- **Stream output** is emulated only for a vertex shader given to `CreateGeometryShaderWithStreamOutput` with no rasterised stream; the vertex shader then writes the output buffers itself; other cases log "not supported, expect problem" — source: `d3d11_device.cpp:274-291`, `dxbc_converter.cpp:640-680`; multiple streams only assert stream 0 — source: `src/airconv/dxbc_converter_cfg.cpp:638-652`. (The game creates three geometry shaders and uses no stream output: the D3D11 analysis.)

### 3.6 Reflection and gaps in the translator

- **No `D3DReflect`, `ID3D11ShaderReflection`, `D3DGetBlobPart` or `D3DCompile`**, and no `RDEF` parsing; the only `d3dcompiler` use is the Windows build of the command-line tool's disassembly option — source: grep over `src` and `libs`; `airconv_cli.cpp:21,175-203`; `src/airconv/meson.build:98-103`; DXBCParser knows no `RDEF` four-character code — source: `libs/DXBCParser/BlobContainer.h:20-36`.
- Markers in `src/airconv`: `UnsupportedFeature` 18, `assert(0` 72, `TODO` 37, `FIXME` 11, `llvm_unreachable` 2 — source: grep counts at this commit.
- **An unknown opcode is dropped in a release build**: the default branch prints "unhandled dxbc instruction", asserts (compiled out with `b_ndebug=if-release`) and returns a no-op — source: `src/airconv/dxbc_instructions.cpp:2098-2101`, `meson.build:5`. So a shader using an instruction the translator lacks compiles to something wrong rather than failing (inference); the conversion sweep should run a **debug** build of `airconv` to make these fatal.
- Doubles: unsupported (the device also reports no double support) — source: `src/airconv/shader_common.hpp:14-15`, `d3d11_inspection.cpp` (`m_doubles`). Interfaces and function tables (`dcl_interface`, `dcl_function_table`, `dcl_function_body`) are ignored and interface resources assert — source: `dxbc_converter_cfg.cpp:588-594`, `dxbc_instructions.cpp:435`.

## 4. The window and presentation

### 4.1 How it gets a layer from Wine's macOS driver

- The swap-chain constructor calls `WMT::CreateMetalViewFromHWND(hWnd, device, &layer)` (unix call 72) and **calls `abort()` if no view comes back** — source: `src/d3d11/d3d11_swapchain.cpp:135-140`, `winemetal_thunks.c:715-724`.
- The unix side finds Wine's Mac driver by symbol lookup in the process: first a table `macdrv_functions` (fields include `get_win_data`, `release_win_data`, `macdrv_view_create_metal_view`, `macdrv_view_get_metal_layer`, `macdrv_view_release_metal_view`, `on_main_thread`), else the individual symbols `get_win_data`, `release_win_data`, `macdrv_view_create_metal_view`, `macdrv_view_get_metal_layer` via `dlsym(RTLD_DEFAULT, ...)`. With them it takes the window's `client_cocoa_view`, has the driver create a Metal view inside it, and returns that view and its `CAMetalLayer` — source: `winemetal_unix.c:1663-1719`. `ReleaseMetalView` asks the driver to release the view — source: `:1721-1737`.
- This function is compiled into the native build as well; with no Wine in the process every lookup fails and the constructor aborts — source: same lines (not under `#ifndef DXMT_NATIVE`); observed by the project, the DXMT measurement
- The Wine build also uses `D3DKMTEscape(D3DKMT_ESCAPE_SET_PRESENT_RECT_WINE)` from the Win32 window code — source: `src/util/wsi_window_win32.cpp:133-138,178-183,244-249`.

### 4.2 What it asks of the window system

- **Window functions** (namespace `wsi`): `getWindowSize`, `resizeWindow`, `setWindowMode`, `enterFullscreenMode`, `leaveFullscreenMode`, `restoreDisplayMode`, `getWindowMonitor`, `isWindow`, `updateFullscreenWindow`, `isForeground`, `isMinimized` — source: `src/util/wsi_window.hpp:27-108`. The Wine build answers them with `GetClientRect`, `GetWindowLongW`/`SetWindowLongW`, `SetWindowPos`, `MoveWindow`, `AdjustWindowRectEx`, `ChangeDisplaySettingsExW`, `GetForegroundWindow`, `IsIconic`, `IsWindow`, `MonitorFromPoint` and others — source: `src/util/wsi_window_win32.cpp` (calls counted).
- **Monitor functions**: `getDefaultMonitor`, `enumMonitors`, `getDisplayName`, `getDesktopCoordinates`, `getDisplayMode`, `getCurrentDisplayMode`, `getDesktopDisplayMode` — source: `src/util/wsi_monitor.hpp:42-102`; Win32 versions use `EnumDisplayMonitors`, `GetMonitorInfoW`, `EnumDisplaySettings(Ex)W` — source: `wsi_monitor_win32.cpp`.
- **In the native build all of these are constants**: window size 1024 x 1024, every window valid, foreground and not minimized, no monitor, every mode and fullscreen function fails, every monitor function returns null or false — source: `src/util/wsi_window_headless.cpp:19-62`, `src/util/wsi_monitor_headless.cpp:15-46`. Consequences: no `IDXGIOutput` (adapter `EnumOutputs` iterates `wsi::enumMonitors`, `src/dxgi/dxgi_adapter.cpp:197`), no mode list, and the refresh rate for pacing stays unknown (`init_refresh_rate_` keeps `DBL_MAX`, `d3d11_swapchain.cpp:171-177,1082`).
- **Direct AppKit/CoreGraphics reads from the unix side**: display ids (`CGMainDisplayID`, `CGGetOnlineDisplayList`), colour gamut from the display's ColorSync profile, EDR headroom from the `NSScreen`, and, per present, the layer's `delegate` view → window → screen — source: `winemetal_unix.c:2287-2390,2392-2416,2482-2511`.

### 4.3 What it does to the layer, and on which thread

- `Presenter` reads the layer's properties at construction and then forces `device`, `opaque = true`, `displaySyncEnabled = false`, `framebufferOnly = false`, `contentsScale` x scale factor; `drawableSize` and `pixelFormat` (sRGB stripped) follow the swap chain — source: `src/dxmt/dxmt_presenter.cpp:11-21,38-55`.
- Layer setters run through `execute_on_main`, which runs the block directly on the main thread and otherwise does `dispatch_sync` to the main queue: `MetalLayer_setProps` and `MetalLayer_setColorSpace` (which also sets `wantsExtendedDynamicRangeContent`) — source: `winemetal_unix.c:20-26,1624-1639,2266-2284`. They are called on the first present after a change of format, colour space, sample count, gamma ramp or HDR metadata, and from `ResizeBuffers` when nothing is invalidated — source: `dxmt_presenter.cpp:38-55,104-127`. The project measured the consequence (a render thread blocks until the main thread services its queue): the DXMT measurement
- **Present path**: `Present1` takes the device mutex, flushes the context, emits a closure into the current command chunk that will (on the encode thread) call `ctx.present(...)`, commits the chunk, drops the mutex and waits in `PresentBoundary()` for frame latency — source: `src/d3d11/d3d11_swapchain.cpp:739-817`. On the encode thread the presenter calls `layer.nextDrawable`, encodes one full-screen triangle that copies or scales the back buffer (gamma LUT, HDR PQ, sRGB and MSAA handled by function constants), and the command buffer presents with `presentDrawable:afterMinimumDuration:` when a pacing interval is set, else `presentDrawable:` — source: `dxmt_presenter.cpp:153-186,188-232`, `src/dxmt/dxmt_context.cpp:1087-1105`.
- Only back buffer 0 exists; the back buffer is a separate texture copied into the drawable — source: `d3d11_swapchain.cpp:178-202` (one `backbuffer_desc_`); the DXMT measurement (`GetBuffer` index 0 only).

### 4.4 Vertical sync, full screen, HDR

- **Vsync**: never by the layer's `displaySyncEnabled` (forced off); pacing is `SyncInterval / refresh rate` (or a configured `d3d11.preferredMaxFrameRate`) passed to `presentDrawable:afterMinimumDuration:` — source: `d3d11_swapchain.cpp:167-177,769-773`, `dxmt_context.cpp:1101-1104`. Default swap-chain frame latency constant 1 — source: `d3d11_swapchain.cpp:34,150`.
- **Present status**: returns `DXGI_STATUS_OCCLUDED` when the window is minimized or the size is 0 (bitblt models only) — source: `:745-765`.
- **Full screen**: `SetFullscreenState` → `EnterFullscreenMode` → optional mode switch through `wsi::setWindowMode`, then `wsi::enterFullscreenMode`; failure is `DXGI_ERROR_NOT_CURRENTLY_AVAILABLE` — source: `:267-344,404-424`. With the headless window layer these always fail (source: `wsi_window_headless.cpp:31-44`). A swap chain created with `Windowed = FALSE` calls `EnterFullscreenMode` in the constructor and ignores the result — source: `d3d11_swapchain.cpp:204-205`. Alt-tab handling is behind `dxgi.handleAltTab` — source: `:179,444-445,751`.
- **HDR**: colour spaces sRGB, linear sRGB / scRGB (extended linear), BT.2020, HDR PQ (BT.2100) are mapped to CoreGraphics colour spaces; HDR ones turn on EDR on the layer — source: `winemetal_unix.c:2234-2284`. EDR scaling and maximum luminance come from the screen's EDR values and HDR metadata — source: `dxmt_presenter.cpp:104-151`. A per-display "display setting" record (colour space plus HDR metadata) can be pushed with `WMTUpdateDisplaySetting` — source: `winemetal_unix.c:2418-2479`.
- **MetalFX spatial upscaling of the swap chain** when `DXMT_METALFX_SPATIAL_SWAPCHAIN=1` and the GPU supports it (factor from `d3d11.metalSpatialUpscaleFactor`, default 2) — source: `d3d11_swapchain.cpp:142-144,515-535,782-798,1123-1131`.
- **HUD**: every swap chain registers a label with the Metal developer HUD through a non-public class (`_CADeveloperHUDProperties`, or `MTLHUDService` from macOS 27) — source: `d3d11_swapchain.cpp:133,181,821-898`, `winemetal_unix.c:1539-1594`.

### 4.5 What a program that is not Wine must provide instead

- A `CAMetalLayer` (any layer the host owns), handed to `_CreateMetalViewFromHWND` in place of the macdrv lookup, and released in `_ReleaseMetalView` — the two functions at `winemetal_unix.c:1685-1737`. The project has done this with a host-owned `DXMTNativeWindow` record (magic, version, layer, callbacks) passed as the `HWND`: the DXMT measurement, patches `patches/dxmt/0001..0003`.
- Answers for the eleven `wsi` window functions and the seven monitor functions (source: `wsi_window.hpp`, `wsi_monitor.hpp`): at minimum client size, minimized, foreground, the display (for the refresh rate), the monitor list with modes; fullscreen can stay "unavailable" (the project's patch 0002 answers monitors from CoreGraphics: the DXMT measurement).
- A main thread that services the main dispatch queue while another thread is in `Present`/`ResizeBuffers` (because of `execute_on_main`), and autorelease pools on threads calling into DXMT (the per-present AppKit reads autorelease objects) — inference from `winemetal_unix.c:20-26,2482-2511`; measured in the DXMT measurement
- Nothing for `HWND` identity beyond that: DXMT stores the handle and gives it back from `GetDesc`/`GetHwnd` — source: `d3d11_swapchain.cpp:689-696,1076`, `dxgi_factory.cpp:204-226,324`.

## 5. What a native, Wine-free build needs

The starting point is that the native build already exists upstream (section 1): it compiles the
front end with clang for arm64 macOS against DXMT's own minimal Windows types, links the Metal side into
one `winemetal.dylib`, and replaces the unix call by a direct table call. So "Wine-free" is mostly done
by DXMT; what remains is (a) the ABI gap between that native library and **our caller, which is
recompiled Windows x64 code that passes arguments in Windows x64 layout through thunks of its own**, and
(b) the few places where the native build still stubs or aborts.

### 5.1 Item by item

| # | Dependency on Wine or the Windows ABI | What DXMT does in its native build | What must be replaced or stubbed for our caller | Mark |
|---|---|---|---|---|
| 1 | **Calling convention** of COM methods and exports | `STDMETHODCALLTYPE`, `__stdcall` and `WINAPI` are empty, interfaces are C++ abstract classes: AAPCS64 (Apple), `this` in `x0`, Itanium vtable with the slots in declaration order | the guest calls through Windows x64 vtables: every interface pointer the guest holds must be a **guest-side proxy** whose vtable slots are thunks converting Windows x64 → AAPCS64 (and the reverse for the few calls DXMT makes into guest objects). Integer/pointer arguments map by position; the methods with `FLOAT` or by-value struct arguments (`ClearDepthStencilView`, `SetResourceMinLOD`, `GetResourceMinLOD`, `EnumAdapterByLuid`) need per-method code; 32-bit return values in `w0` need extension | source: `include/native/windows/windows_base.h:209-224`; counts from the DXMT measurement |
| 2 | **`HRESULT` and COM conventions** | `HRESULT` is `int32_t`, `IUnknown` declares `QueryInterface/AddRef/Release` as pure virtuals; GUIDs are 16 bytes; `__uuidof` is emulated with a template | layouts match Windows x64 except `WCHAR`; the proxy must keep **one proxy per (native object, interface)**, because `QueryInterface` returns different sub-objects for different IIDs; `SetPrivateDataInterface` makes DXMT call `AddRef`/`Release` on a guest `IUnknown` (needs a host-to-guest thunk object) | source: `windows_base.h:28,94-97`; `src/util/com/com_private_data.cpp:30,80,95`; the DXMT measurement |
| 3 | **`WCHAR`** | `wchar_t`, 4 bytes on macOS (Windows: 2) | repack `DXGI_ADAPTER_DESC*` (`Description[128]`) and `DXGI_OUTPUT_DESC*` (`DeviceName[32]`) in the proxy; note the adapter name is written as 2-byte UTF-16 into the 4-byte array (`dxgi_adapter.cpp:156`) while the custom-name branch writes 4-byte units | source: `windows_base.h:30`, `src/dxgi/dxgi_adapter.cpp:150-156` |
| 4 | **`HWND`** | `void*`, stored and returned; given to `CreateMetalViewFromHWND` and the `wsi` functions | translate the guest's `HWND` (our runtime's handle) to a host window record holding the layer at `CreateSwapChain`, `CreateSwapChainForHwnd`, `MakeWindowAssociation`, `D3D11CreateDeviceAndSwapChain`, and back at `GetDesc`/`GetHwnd`/`GetWindowAssociation`; replace `_CreateMetalViewFromHWND`/`_ReleaseMetalView` and the two `wsi_*_headless.cpp` files (done by the project's patches 0001/0002) | source: `d3d11_swapchain.cpp:135,689-696`, `dxgi_factory.cpp:104,204-226`, `winemetal_unix.c:1685-1737`; the DXMT measurement |
| 5 | **Windows-side use of the C runtime and `kernel32`** | Win32 calls not available natively are inline stubs that log a warning and return failure: `GetCurrentProcess`, `GetCurrentThread`, `GetCurrentProcessId`, `ProcessIdToSessionId`, `SetThreadPriority`, `GetModuleHandle`, `ExitThread`, `SendMessage`, `GetProcessId`, `GetWindowThreadProcessId`, `CreateSemaphore`, `ReleaseSemaphore`, `CloseHandle`, `DuplicateHandle`; the D3DKMT family (shared resources) returns failure; the C runtime is the host's libc/libc++ | nothing is required for correctness (the same-process check in `CreateSwapChain` passes because both sides read 0; the frame-latency semaphore is null). For quiet logs and a real `GetFrameLatencyWaitableObject` the semaphore stubs can be made real; `getExePath()` returns `""` on macOS, which empties the per-program shader-cache directory, config section and log-file names — implement it or set `DXMT_SHADER_CACHE_PATH`/`DXMT_LOG_PATH` | source: `src/util/util_win32_compat.h:9-101`, `src/util/util_env.cpp:79-97`, `d3d11_swapchain.cpp:150-152,797,807,1107-1112`, `src/util/util_d3dkmt.h:517-655` |
| 6 | **Structured exceptions** | none: no `__try`/`__except`, no vectored handler, no `RaiseException` in the tree; DXMT uses C++ exceptions internally (`MTLD3DError`) and catches them in the exported creators; airconv is built `-fno-exceptions` | the guest's SEH must never unwind through DXMT frames: the proxy thunks are the boundary, and a C++ exception escaping a DXMT method (one not caught inside) would cross into lifted code; a fault inside DXMT is a native crash with no guest frame | source: grep over `src` (no hits for SEH constructs); `src/d3d11/d3d11.cpp:107` region (catch), `src/airconv/meson.build:40-50`; the "unwind" consequence is my inference |
| 7 | **Thread-local storage** | only C++ `thread_local` (a thread id counter, a timestamp on the finish thread); no `TlsAlloc`/`FlsAlloc`; threads are `std::thread`/pthreads | nothing: DXMT never reads the guest TEB or `gs`; our runtime's guest TLS is unaffected (inference) | source: `src/util/thread.cpp:33`, `src/dxmt/dxmt_occlusion_query.hpp:280-283`, `src/util/thread.hpp:325-361` (per the DXMT measurement) |
| 8 | **The unix-call boundary** | in native builds a direct call through `__wine_unix_call_funcs[]`; no Wine | nothing to replace. It is a convenient seam: every Metal operation goes through 145 numbered C functions taking one parameter struct, so it is the place to log or count Metal calls (inference) | source: `src/nativemetal/wineunixlib.h:1-13`, `winemetal_unix.c:3166-3313` |
| 9 | **Objective-C memory management on calling threads** | AppKit/QuartzCore reads autorelease objects on the thread that calls `Present`; the Wine side relies on the caller's environment | our runtime must wrap calls into DXMT in autorelease pools (at least `Present`, `ResizeBuffers`, device and swap-chain creation) | source: `winemetal_unix.c:2482-2511` (reads), `:100-105` (`NSAutoreleasePool_alloc_init` exists); measured in the DXMT measurement |
| 10 | **Main-thread dispatch** | `execute_on_main` → `dispatch_sync` to the main queue for layer setters | the runtime's main thread must pump (or the setters must be made to run on the calling thread when the layer is ours) | source: `winemetal_unix.c:20-26,1624-1639,2277`; the DXMT measurement |
| 11 | **Entry points and adapter** | exports are `extern "C"` (`D3D11CreateDevice`, `D3D11CreateDeviceAndSwapChain`, `D3D11CoreCreateDevice`, `CreateDXGIFactory`/`1`/`2`); a device needs DXMT's own `IMTLDXGIAdapter` | the guest's `D3D11CreateDevice` import is a thunk to the native export; an adapter the guest passes must be a proxy of a DXMT adapter, unwrapped before the call | source: `src/d3d11/d3d11.cpp:32-111,113-216`, `src/dxgi/dxgi_factory.cpp:327-351`, `d3d11.cpp:38-41` |
| 12 | **`D3DReflect`, `D3DGetBlobPart`** (the game's other two Direct3D imports) | not provided anywhere | must be written by us (DXBC container walk; `RDEF`/`ISGN`/`OSGN` reader behind guest-ABI objects); `libs/DXBCParser` (MIT) can parse the container | source: no definition in `src` or `libs` (grep); the DXMT measurement; the game's imports: the D3D11 analysis |
| 13 | **Abort paths** | `UNIMPLEMENTED(...)` logs and calls `abort()`; the swap chain aborts without a layer | the runtime should turn an abort into a report naming the last guest call (a `SIGABRT` handler in the host, or patch the macro to return an error) | source: `src/d3d11/d3d11_private.h:14-17`, `d3d11_swapchain.cpp:137-140`; remedy is my inference |
| 14 | **Page size and `aligned_alloc`** | `DXMT_PAGE_SIZE=4096` on a 16 KiB-page system; `aligned_malloc` passes its arguments to `aligned_alloc` in the wrong order | harmless on arm64 today (the affected allocations are 32-bit only, and Metal allocates instead when it fails); fix if the placed-buffer path is ever enabled | source: `meson.build:140`, `src/util/wsi_platform_darwin.cpp:6-8`; reach: the DXMT measurement |

### 5.2 The smallest set of source changes

Files that must change for a host-owned window and a quiet native run (the project's three patches cover
the first four; the rest are what this reading adds). Line ranges are at `e94c312`:

1. `src/winemetal/unix/winemetal_unix.c` — `_CreateMetalViewFromHWND` and `_ReleaseMetalView` (`:1685-1737`): in a native build, take the layer from the host's window record instead of `dlsym`-ing Wine's driver.
2. `src/util/wsi_window_headless.cpp` (`:19-62`) — answer size, minimized, foreground, monitor, `isWindow` from the host record.
3. `src/util/wsi_monitor_headless.cpp` (`:15-46`) — monitors and modes (from CoreGraphics).
4. `src/util/util_win32_compat.h` (`:70-88`) — semaphore stubs: silent for a null handle, or real.
5. `src/util/util_env.cpp` (`:79-97`) — `getExePath()` for macOS (`_NSGetExecutablePath` or `proc_pidpath`), or leave and set the two environment variables.
6. `src/d3d11/d3d11_private.h` (`:14-17`) — optional: make `UNIMPLEMENTED` report through a hook the runtime installs, instead of a bare `abort()`.
7. `src/dxgi/dxgi_adapter.cpp` (`:150-156`) — optional: one encoding for the adapter description.
8. Optional, if the runtime cannot guarantee a pumping main thread: `winemetal_unix.c` `execute_on_main` (`:20-26`) — run on the calling thread when the layer belongs to the host.

Everything else (COM proxies, `WCHAR` repacking, `HWND` translation, autorelease pools, `D3DReflect`,
`D3DGetBlobPart`) lives on **our** side of the boundary and needs no DXMT change (inference from items 1-12
above).

### 5.3 What one feature-level-11_0 game can leave out

- **Feature level**: DXMT picks the highest level in the caller's list up to its maximum (11_1 on Apple7 GPUs, else 11_0), with a config cap `d3d11.maxFeatureLevel` — source: `src/d3d11/d3d11.cpp:48-92`. The game asks with the default list and rejects below 11_0 (the D3D11 analysis, summary item 1), so it gets 11_0 (observed by the project, the DXMT measurement).
- **Deferred contexts: the source lets one find out at run time, and the project already found out statically.** DXMT reports `DriverCommandLists = TRUE` and `DriverConcurrentCreates = TRUE` (source: `src/d3d11/d3d11_inspection.cpp:13-14`), so a game that asks would be told command lists are native; but the game never calls `CheckFeatureSupport`, `CreateDeferredContext`, `FinishCommandList` or `ExecuteCommandList` at any typed call site (the D3D11 analysis, item 6 and its table "Deferred contexts, command lists: not used"). A proxy that logs `CreateDeferredContext*` (source: `d3d11_device.cpp:447-454,719-726`) would confirm it at run time. So `src/d3d11/d3d11_context_def.cpp` can be left untouched and unexercised.
- **Not needed by this game per the D3D11 analysis** (all still compiled; nothing to remove, only nothing to test): hull/domain shaders (tessellation path `d3d11_pipeline_ts.cpp`), stream output, predication, `DrawAuto`, `GenerateMips`, shared resources and fences (`d3d11_fence.cpp`, D3DKMT), class linkage, counters, the Direct3D 10 front end and `d3d10core`, `d3d12`, nvapi/nvngx, MetalFX. **Needed**: geometry shaders without stream output (3 created), event/occlusion/timestamp/disjoint queries, `Map` with discard and no-overwrite, BC formats, the swap chain.
- Leaving things out of the **build** is possible only for the optional components (`enable_d3d12`, `enable_nvapi`, `enable_nvngx`, `enable_tests`); the D3D10 front end is compiled into `d3d11.dylib` unconditionally — source: `meson.options:8-11`, `src/d3d11/meson.build:32-44`.

## 6. Coverage and risk

### 6.1 DXMT's own markers of unimplemented paths, at `e94c312`

Raw line counts (`grep`; a count can include comments and a definition):

| Marker | `src/d3d11` | `src/dxgi` | `src/dxmt` | `src/util` | `src/airconv` |
|---|---|---|---|---|---|
| `UNIMPLEMENTED(` (log, then `abort()`) | 19 uses + definition | 0 | 0 | 0 | — |
| `E_NOTIMPL` | 14 | 12 | 0 | 0 | — |
| "not implemented" | 16 | 16 | 3 | 35 (the Win32 and D3DKMT stubs) | — |
| "not supported" | 24 | 1 | 1 | 0 | 4 |
| `TODO` / `FIXME` | 19 / 24 | 2 / 3 | 10 / 7 | 1 / 1 | 37 / 11 |
| `UnsupportedFeature` / `assert(0` | — | — | — | — | 18 / 72 |

Source: `UNIMPLEMENTED` defined at `src/d3d11/d3d11_private.h:14-17`; counts by grep at this commit.

**The 19 aborting sites** — source, each line read:

| Method | Trigger | Where |
|---|---|---|
| `ExecuteCommandList` on a **deferred** context | nesting command lists (immediate-context version works, `d3d11_context_imm.cpp:508`) | `d3d11_context_def.cpp:393` |
| `SwapDeviceContextState` | any call | `d3d11_context_impl.cpp:1910` |
| tiled resources: `UpdateTileMappings`, `CopyTileMappings`, `CopyTiles`, `UpdateTiles`, `ResizeTilePool`, `TiledResourceBarrier`, `GetResourceTiling` | any call | `d3d11_context_impl.cpp:3207-3238`, `d3d11_device.cpp:803` |
| `UpdateSubresource` into a **staging** buffer / texture | destination usage staging | `d3d11_context_impl.cpp:1242,4255` |
| copies between a block-compressed and a non-block-compressed texture where one side is staging | format-reinterpreting copy with staging | `d3d11_context_impl.cpp:4107,4111,4158,4162` (dispatch at `:3911-3918`) |
| `WriteToSubresource` / `ReadFromSubresource` | map-on-default | `d3d11_device.cpp:1000,1008` |
| `IDXGIDevice::CreateSurface` | any call | `d3d11_device.cpp:1233` |
| `Map` of a default-usage resource with a null output pointer | (otherwise `E_INVALIDARG`) | `d3d11_context_imm.cpp:282-284` |

**Returns `E_NOTIMPL`** — source: class instances (`d3d11_class_linkage.cpp:43,50`), `CreateCounter`/`CheckCounter` (`d3d11_device.cpp:444,581`), `SetExceptionMode` (`:706`), stream-output statistics and overflow queries in `CreateQuery1` (`:975-980`), `D3D11On12CreateDevice` (`d3d11.cpp:226`), swap chain `GetCoreWindow`, `GetRestrictToOutput`, `SetBackgroundColor`, `SetRotation`, `SetSourceSize`, matrix transform (`d3d11_swapchain.cpp:701,908,915,932,947,998,1004`), factory `CreateSwapChainForCoreWindow/Composition` and the occlusion/stereo registration methods (`dxgi_factory.cpp:148-251`), output `SetDisplaySurface`, `GetDisplaySurfaceData(1)`, `GetFrameStatistics` (`dxgi_output.cpp:344-358,534`).

**Partial or approximate** — source unless marked:

- Queries: event, occlusion, occlusion predicate, timestamp, timestamp-disjoint are real; pipeline statistics returns zeros; timestamp-disjoint always reports 1 GHz and not disjoint — `d3d11_device.cpp:961-980`, `d3d11_context_imm.cpp:456-465`. Open issue #230 reports a timestamp query not ready after a later event query signalled (documented: https://github.com/3Shain/dxmt/issues/230).
- `Map`: dynamic buffers take `WRITE_DISCARD` (rename) and `WRITE_NO_OVERWRITE`; `READ`/`WRITE`/`READ_WRITE` on dynamic resources are `E_INVALIDARG`; dynamic textures take discard only; staging maps wait on a CPU fence (with a flush), `DO_NOT_WAIT` gives `DXGI_ERROR_WAS_STILL_DRAWING` unless `d3d11.ignoreMapFlagNoWait` — `d3d11_context_imm.cpp:80,149-279`.
- **The device claims `MapOnDefaultBuffers = TRUE`** while mapping a default-usage resource returns `E_INVALIDARG` — `d3d11_inspection.cpp:82`, `d3d11_context_imm.cpp:282-284`. Only a game that asks `CheckFeatureSupport` and acts on it is affected; this game never calls it (the D3D11 analysis).
- `CheckMultisampleQualityLevels` answers from the device's sample-count support without looking at the format — `d3d11_device.cpp:807-830`.
- Predication stored, not applied; `DrawAuto` a no-op; `DiscardResource`/`DiscardView` no-ops — `d3d11_context_impl.cpp:1832-1836,1892-1900,1268-1290`.
- Geometry-shader draws above the object-threadgroup limit are skipped with a warning; the limit is 1024 on non-Apple GPUs and unbounded on Apple7+ — `d3d11_context_impl.cpp:1556-1558`, `src/dxmt/dxmt_device.cpp:48,53`.
- Swap chain: only buffer 0; cross-process refused; non-flip swap effects with several buffers only warned about — `d3d11_swapchain.cpp:254-260,1107-1122`.
- `DedicatedVideoMemory` on unified-memory Macs is half of Metal's recommended working set (marked FIXME) — `src/dxgi/dxgi_adapter.cpp:176-177`.
- Interfaces offered: device `ID3D11Device`..`5`, `ID3D10Device`/`1`, `IDXGIDevice`..`3`, `ID3D11Multithread` (refuses `ID3D11Debug`, `ID3D11InfoQueue`, capture-tool GUIDs); context `ID3D11DeviceContext`..`4`, `ID3DUserDefinedAnnotation`; swap chain `IDXGISwapChain`..`4`; factory `IDXGIFactory`..`6`; adapter `IDXGIAdapter`..`4`; output `IDXGIOutput`..`6` — `d3d11_device.cpp:1169-1208`, `d3d11_context_impl.cpp:640-656`, `d3d11_swapchain.cpp:223-228`, `dxgi_factory.cpp:29-33`, `dxgi_adapter.cpp:54-57`, `dxgi_output.cpp:170-174`.

**Against what this game calls** (the D3D11 analysis: 146 methods, no deferred contexts, no tessellation, no stream output, no predication, no `GenerateMips`, no `CheckFeatureSupport`; three geometry shaders; four query kinds; 71 `Map` sites, 58 write-discard): none of the 19 aborting sites is in the game's list **unless** it calls `UpdateSubresource` on a staging resource or copies between BC and non-BC formats through staging — which the D3D11 analysis's typed call sites cannot rule out by descriptor (my inference; a proxy that logs the destination usage at `UpdateSubresource` and both formats at `CopyResource`/`CopySubresourceRegion` settles it).

### 6.2 Supported macOS and Metal versions

- **Documented**: building needs macOS Sonoma (14) or later, Xcode 16+ with the Metal toolchain, LLVM 15 — `docs/DEVELOPMENT.md:5-11`. Run time: macOS 14 minimum, 15 recommended, all Apple-silicon Macs; some Intel Macs with AMD or Intel graphics are experimental and planned to be dropped — https://github.com/3Shain/dxmt/wiki/Device-System-Runtime-Specifications. Upstream CI builds on `macos-15` — `.github/workflows/ci.yml:31`.
- **In the source**: Metal language 3.2 on macOS 15+, else 3.1, with an override `dxmt.shaderMetalVersion` — `src/dxmt/dxmt_device.cpp:28-43`; a GPU that is not Apple7 or newer is "experimental", forced to Metal 3.1 and limited to 1024 object threadgroups — `:45-54`; feature level 11_1 on Apple7+, else 11_0 — `src/d3d11/d3d11.cpp:58-59`; mesh-shader paths (geometry, tessellation) need Apple7; tess factor capped at 8 below Apple9 — `d3d11_pipeline_ts.cpp:55-57`; format capabilities tiered Apple7/8/9 and Mac2 — `src/dxmt/dxmt_format.cpp:37-268`; helper shaders built for `air64-apple-macos14.0` — `src/airconv/meson.build:73`; `@available(macOS 15)` around binary-archive options, `macOS 16` around MetalFX temporal scaler set-up, a HUD class switch at macOS 27 — `winemetal_unix.c:636,650,1437-1457,1534-1546`. MetalFX is linked strongly — `src/nativemetal/meson.build:10`.
- **Non-public Apple interfaces**, on by default: logic-operation selectors on render-pipeline descriptors, `MTLSetShaderCachePath`, the developer-HUD classes (the last not behind the switch); `-DDXMT_NO_PRIVATE_API` removes the first two and there is no meson option for it — `winemetal_unix.c:482,548,615,1539-1594`, `cache.c:224-238`, `d3d11_device.cpp:652`, `meson.options`.
- Release notes: on Sonoma, unsupported Metal 3.2 intrinsics become no-ops and may mis-render (documented: v0.71, https://github.com/3Shain/dxmt/releases).

### 6.3 Known limitations that bear on a large Direct3D 11 game

From the documentation (documented):

- `docs/COMPATIBILITY_FLAG.md:6-12`: tessellator point/isoline output, geometry with tessellation, `DrawAuto`, predication, stream-output append, multiple streams — shown only on a debug build's HUD (`dxmt_debug`).
- Stream output from geometry shaders is planned for 1.0; tiled resources, video and tessellation+geometry after 1.0 — https://github.com/3Shain/dxmt/issues/151, #28.

From the issue tracker (documented; fetched for this note):

- **#204, open** (2026-08-07): one `UpdateSubresource` per draw into a default-usage constant buffer bound to the **pixel shader** is quadratic in draws (87 ms vs 12 ms for Apple's D3DMetal at 24k draws), because each update renames into a new Metal buffer; no maintainer reply at the time of reading — https://github.com/3Shain/dxmt/issues/204. A Source-engine renderer updating constant buffers per draw is the pattern at risk (my inference; whether the game uses default-usage constant buffers with `UpdateSubresource` or dynamic ones with `Map` is in the D3D11 analysis's descriptors, not established here).
- **#56, closed** "Titanfall 2 crashes whenever player dies" (2025-02-09), closed by the reporter the same day with no cause or fix recorded — https://github.com/3Shain/dxmt/issues/56. Release v0.50 lists a "Titanfall 2 fullscreen crash fix" — https://github.com/3Shain/dxmt/releases. So this game has been run on DXMT under Wine by users (documented), which says nothing about our native path.
- Open: #230 (timestamp queries), #228 (`Buffer<float3>` over an `R32G32B32_FLOAT` view), #200 (geometry/tessellation draw arguments), #96/#95 (tessellation limits), #99 (null bindings), #37 (pixel-shader output vs render-target format), #26 (vsync on ProMotion displays); closed and relevant: #220 (a texture copy silently dropped, fixed in `e40c9d7`, the parent of the commit read), #206 (presents to an older swap chain never shown), #94 (a game relying on hazard tracking), #186 (uninitialised pipeline "ready" flags) — https://github.com/3Shain/dxmt/issues.
- First-use pipeline compiles stall the encode thread (section 3.3); release v0.71 says slow first-run compiles were improved (documented, releases page).

### 6.4 How active the project is

- Full history in the clone (not shallow): **1,378 commits** from 2023-11-09 to 2026-10-06; 18 author names, of which three (`3Shain`, `Feifan He` at two addresses) are the maintainer with **1,318** commits; the next contributor has 26 — source: `git rev-list --count`, `git shortlog -sne` at `e94c312`.
- Commits per month in 2026: May 6, June 25, July 92, August 66, September 47, October 1-6: 16; 262 commits since the last tag — source: `git log`.
- Latest release **v0.80, 2026-04-23**, the last under MIT; the code is LGPL 2.1+ since, "Copyright 2023-2026 Feifan He for CodeWeavers" — source: `git tag`, `LICENSE:1`, `LICENSE.OLD`; documented: releases page; relicensing discussed in open issue #152.
- 21 open and 49 closed issues at the time of reading — documented: https://github.com/3Shain/dxmt/issues.
- **Contribution policy**: the project accepts no contribution written or co-written by an AI; contributors may use AI for their own research but should not open pull requests from it; commits need semantic messages and a sign-off with a legal name — source: `AGENTS.md`, `CONTRIBUTING.md:4-29`. For us this means our patches stay in a private fork and are never offered upstream (matches the project's rule, the DXMT measurement).
- Reading (my inference): a one-maintainer project funded by CodeWeavers, very active, with no API freeze: expect the files named in 5.2 to move under us; pin a commit and rebase deliberately.

## What I did not reach

- **Not built, not run.** Nothing here was compiled or executed; the project's Mac-side builds did that
  at the same commit.
- **Translator internals**: the full instruction table (133 `case` labels in `dxbc_instructions.cpp`), vertex
  pulling (`dxbc_converter.cpp:690-860`), the bodies of `dxbc_converter_gs.cpp` / `_ts.cpp`,
  `air_signature.cpp`, `air_operations.cpp`. So **which DXBC instructions are missing** is not listed; the
  conversion sweep with a debug `airconv` over the game's shaders is the way to answer it for this game.
- **Ring-allocator blocking** (`dxmt_ring_bump_allocator.hpp`): whether `UpdateSubresource`/`Map` can block
  the caller when the staging ring is full was not traced.
- `ResizeBuffers` and fullscreen paths of the swap chain beyond the lines cited; `ResourceInitializer`
  threading; MTLFence/encoder-merge rules in `flushCommands`; `RenamableTexturePool`; the keyed mutex;
  where `EventListener` (`dxmt_fence.cpp`) is created; the D3D10 wrappers; `src/d3d12`.
- **Issue tracker**: only the two list pages and the issues named were read; comments beyond #35, #56, #151,
  #204 were not; `dxmt.report` (known-issues site linked from releases) did not resolve; the wiki pages other
  than the runtime-specification page were not read.
- The LLVM licence is stated from general knowledge (not in the repository, not fetched).
- Whether this game updates constant buffers with `UpdateSubresource` on default-usage buffers (the issue #204
  pattern) or with `Map(WRITE_DISCARD)` on dynamic ones: answerable from the D3D11 analysis's descriptors, not
  checked here.
