# Native arm64 macOS options for a D3D11 renderer (D3D11 to Metal, DXBC shader path) and a Win32 layer, as of 2026-10-08

Scope: personal research project that statically translates Titanfall 2 (Windows x86-64, D3D11) to a native arm64 Mach-O on a test machine (M3 Pro, macOS 27), with no Wine and no Rosetta. All pages were read on 2026-10-08. "Unverified" means I could not confirm it from a page I opened (search snippets, or fetch-tool summaries I could not cross-check, are labelled as such).

## Source reliability and how facts were verified

### Takeaway
The WebFetch summariser gave wrong dates and versions for DXMT and MoltenVK (it reported 2024 where the real dates are 2025-2026), so every version or date below comes from raw GitHub files or the GitHub REST API, not from a fetch-tool summary. Treat anything tagged "(summary only)" as lower confidence.

### Cited Findings
- WebFetch summary of the DXMT releases page listed v0.80 as "April 23, 2024"; the GitHub API gives v0.80 published 2026-04-23 — [API](https://api.github.com/repos/3Shain/dxmt/releases)
- WebFetch summary of MoltenVK releases listed v1.4.2 as "July 24, 2024"; the API gives v1.4.2 published 2026-07-24 — [API](https://api.github.com/repos/KhronosGroup/MoltenVK/releases)
- Several vendor sites blocked automated fetches (CodeWeavers blog returned a Cloudflare challenge/403, gitlab.winehq.org and wiki.winehq.org returned an Anubis challenge, Valve Developer Community and Phoronix returned 403). I did not attempt to bypass these; facts that depend on those pages are sourced from search-result summaries and labelled unverified.
- The unauthenticated GitHub API rate limit (60/hour) was exhausted partway through, so a few repo-metadata checks (e.g. PR merge status for DXMT #213) were not completed.

### Inferences
- Version/date claims in the report should be cited to the API or release pages listed below, not to secondary articles.

### Gaps
- No access to the CodeWeavers blog posts, the WineHQ wiki or the Valve wiki as primary pages.

---

## A1. DXMT (3Shain): maturity, licence, coupling to Wine, native (non-Wine) build

### Takeaway
DXMT is the strongest candidate: an actively developed D3D11/D3D10-to-Metal layer (v0.80 released 2026-04-23, commits through 2026-10-06) with its own DXBC-to-Metal-AIR shader compiler, geometry-shader and tessellation support via Metal mesh shaders, and an in-tree "Native Build" mode that builds everything as Mach-O dylibs. The native mode is incomplete: mainline has only a headless window-system stub, and the swapchain aborts without Wine's winemac driver symbols, so you must supply the HWND-to-CAMetalLayer step (an open SDL3 PR does this). Current licence is LGPL-2.1-or-later.

### Cited Findings
**What it is, status, cadence**
- Self-description: "a Metal-based translation layer for Direct3D 11 and 10 which allows running 3D applications on macOS using Wine" — [README](https://github.com/3Shain/dxmt/blob/main/README.md)
- Release history (API, published_at): v0.60 2025-06-15 (D3D10, HDR, D3D11 fence/multithread layer); v0.61 2025-08-12; v0.70 2025-10-17 (new tessellation on Metal mesh shaders); v0.71 2025-11-13 (AIR shader cache plus PSO cache); v0.72 2025-12-11 (shared resources via D3DKMT needs Wine 10.18+; "experimental Intel Macs support"); v0.73 2026-01-21; v0.74 2026-03-10; v0.80 2026-04-23 (timestamp queries, MTLFence/intrapass-barrier resource sync) — [API](https://api.github.com/repos/3Shain/dxmt/releases)
- v0.80 notes say v1.0 is planned "in the following months" with a roadmap at issue #151; no release newer than v0.80 appears in the API listing as of 2026-10-08 — [v0.80 release](https://github.com/3Shain/dxmt/releases/tag/v0.80)
- Repo is live: last push 2026-10-06; 1,219 stars; 31 open issues; default branch main — [API repo](https://api.github.com/repos/3Shain/dxmt). Recent commits are bug fixes (e.g. 2026-10-06 "avoid deadlock on reentrant ResizeTarget()", 2026-10-05 CopySubresourceRegion across texture types) — [API commits](https://api.github.com/repos/3Shain/dxmt/commits?per_page=15)
- Games named in release notes as fixed or working: Shadow of the Tomb Raider, Overwatch 2, Elite Dangerous, Baldur's Gate 3 (DX11), Genshin Impact, Palworld DX11, Total War: Rome II DX11, Middle-earth: Shadow of War, Yakuza series, Ys VIII, Ni no Kuni I/II, Counter-Strike 2, Warframe, Alien: Isolation, Stellaris, World of Tanks, Street Fighter V, Deadlock, BeamNG.drive, Mount & Blade II, Unity-engine titles — [API releases](https://api.github.com/repos/3Shain/dxmt/releases). Titanfall 2 is not mentioned in any release note I read.
- A user bug report (not a maintainer statement) runs Sekiro on DXMT 0.80 on macOS 26.5 under Wine 10.0 — [issue #200](https://github.com/3Shain/dxmt/issues/200)

**Licence**
- Current licence: LGPL v2.1 or later; "Copyright (c) 2023-2026 Feifan He for CodeWeavers"; COPYING.LIB is LGPL 2.1 — [LICENSE](https://github.com/3Shain/dxmt/blob/main/LICENSE), [COPYING.LIB](https://github.com/3Shain/dxmt/blob/main/COPYING.LIB)
- Releases up to and including v0.80 were MIT; later code is LGPL — [LICENSE.OLD](https://github.com/3Shain/dxmt/blob/main/LICENSE.OLD), [v0.80 notes](https://github.com/3Shain/dxmt/releases/tag/v0.80)
- GitHub's licence detector shows "Other/NOASSERTION" because of the three licence files — [API repo](https://api.github.com/repos/3Shain/dxmt). Some files are derived from DXVK and keep DXVK's zlib licence (src/util/dxvk.LICENSE; the headless WSI file header says "Derived from a part of DXVK (originally under zlib License)") — [dxvk.LICENSE](https://github.com/3Shain/dxmt/blob/main/src/util/dxvk.LICENSE), [wsi_window_headless.cpp](https://github.com/3Shain/dxmt/blob/main/src/util/wsi_window_headless.cpp)
- Contribution policy: AI-written code is not accepted upstream ("AI must not be used to generate code for contributions") — [AGENTS.md](https://github.com/3Shain/dxmt/blob/main/AGENTS.md), [CONTRIBUTING.md](https://github.com/3Shain/dxmt/blob/main/CONTRIBUTING.md). A commenter (role not verified) reacted to an AI-generated PR with "Another AI? Please read CONTRIBUTING.md" — [PR #213](https://github.com/3Shain/dxmt/pull/213)

**Platform requirements**
- Minimum macOS 14 Sonoma, macOS 15 recommended; all Apple Silicon Macs supported; Intel Macs experimental from v0.72 and "planned to be removed from upstream once macOS 28 is released" — [Wiki: Device/System/Runtime specs](https://raw.githubusercontent.com/wiki/3Shain/dxmt/Device-System-Runtime-Specifications.md)
- On Sonoma some Metal 3.2 intrinsics become no-ops and the maintainers advise Sequoia or later — [v0.71 notes](https://github.com/3Shain/dxmt/releases/tag/v0.71)
- Build needs Meson 1.3+, LLVM 15 (exact major, headers and static libs), Xcode 16+ with the Metal toolchain; "Native Build" section: "Build all components as Mach-O .dylib"; DEVELOPMENT.md says for non-cross builds "you probably should use arm64, since Intel Macs and Rosetta 2 are being deprecated" — [docs/DEVELOPMENT.md](https://github.com/3Shain/dxmt/blob/main/docs/DEVELOPMENT.md)
- Homebrew LLVM 15 on Apple Silicon failed to link (missing static zstd/libunwind handling for /opt/homebrew); reported 2026-07-09 and closed 2026-07-17 — [issue #192](https://github.com/3Shain/dxmt/issues/192), [PR #193](https://github.com/3Shain/dxmt/pull/193)

**How it is split and how tied to Wine**
- Two build modes in meson.build: `meson.is_cross_build()` builds Windows PE DLLs (d3d11.dll, dxgi.dll, winemetal.dll) with a MinGW/llvm-mingw cross compiler and runs `winebuild --builtin` on them, plus a separate Mach-O unix library `winemetal.so` that links Wine's `winemac.so` and `ntdll.so`; a non-cross build compiles everything as native Mach-O — [meson.build](https://github.com/3Shain/dxmt/blob/main/meson.build), [src/meson.build](https://github.com/3Shain/dxmt/blob/main/src/meson.build), [winemetal/meson.build](https://github.com/3Shain/dxmt/blob/main/src/winemetal/meson.build), [winemetal/unix/meson.build](https://github.com/3Shain/dxmt/blob/main/src/winemetal/unix/meson.build)
- The "winemetal" PE side is thunks (`winemetal_thunks.c`) that call into the unix side through Wine's `__wine_unix_call_funcs[]` table; the unix side (`src/winemetal/unix/winemetal_unix.c`, about 135 KB) is the Objective-C Metal/MetalFX wrapper — [winemetal_thunks.c](https://github.com/3Shain/dxmt/blob/main/src/winemetal/winemetal_thunks.c), [winemetal_unix.c](https://github.com/3Shain/dxmt/blob/main/src/winemetal/unix/winemetal_unix.c)
- Native mode reuses that same code: `src/nativemetal/meson.build` builds a `winemetal` dylib from winemetal_thunks.c + unix/winemetal_unix.c + cache.c with `-DDXMT_NATIVE=1`, links Metal/MetalFX/Cocoa/QuartzCore/sqlite3, and `src/nativemetal/wineunixlib.h` defines `WINE_UNIX_CALL(code,args)` as a direct call into `__wine_unix_call_funcs[code]` — [nativemetal/meson.build](https://github.com/3Shain/dxmt/blob/main/src/nativemetal/meson.build), [nativemetal/wineunixlib.h](https://github.com/3Shain/dxmt/blob/main/src/nativemetal/wineunixlib.h). The maintainer asked for exactly this design in the native-port PR review ("UNIX_CALL is the key") — [PR #75](https://github.com/3Shain/dxmt/pull/75)
- `DXMT_NATIVE` only guards the 32-bit WoW64 thunk variants and Wine-event signalling (e.g. `_MTLSharedEvent_setWin32EventAtValue`) in winemetal_unix.c — [winemetal_unix.c](https://github.com/3Shain/dxmt/blob/main/src/winemetal/unix/winemetal_unix.c)
- Non-cross builds add `include/native/windows` and `include/native/directx` (mingw-directx-headers submodule) so D3D11/COM headers compile on macOS, and use `wsi_*_headless.cpp` plus `wsi_platform_darwin.cpp` instead of the Win32 WSI — [meson.build](https://github.com/3Shain/dxmt/blob/main/meson.build), [src/util/meson.build](https://github.com/3Shain/dxmt/blob/main/src/util/meson.build), [.gitmodules](https://github.com/3Shain/dxmt/blob/main/.gitmodules)

**What is missing in native mode**
- The headless WSI is a stub: window size hard-coded to 1024x1024, fullscreen/mode-switch return false, `isWindow()` always true — [wsi_window_headless.cpp](https://github.com/3Shain/dxmt/blob/main/src/util/wsi_window_headless.cpp)
- Swapchain creation calls `CreateMetalViewFromHWND`, whose unix side finds `macdrv_*` functions via `dlsym(RTLD_DEFAULT, ...)` from Wine's winemac driver; if no view results the constructor logs "your Wine has no exported symbols needed by DXMT" and calls `abort()` — [d3d11_swapchain.cpp](https://github.com/3Shain/dxmt/blob/main/src/d3d11/d3d11_swapchain.cpp), [winemetal_unix.c](https://github.com/3Shain/dxmt/blob/main/src/winemetal/unix/winemetal_unix.c). In a plain Mach-O process those symbols do not exist, so presenting does not work out of the box.
- Native-mode PRs: #75/#76 by Alexander Eichner (merged 2025-07-15, via #76) added the native build and headless WSI; his own words: "the library builds and there is some support for offscreen rendering"; swapchains/presenting "not currently supported"; evaluating DXMT "to use in VirtualBox on macOS as our 3D backend because DXVK on top of MoltenVK doesn't work out" — [PR #76](https://github.com/3Shain/dxmt/pull/76), [PR #75](https://github.com/3Shain/dxmt/pull/75)
- SDL2 WSI by another contributor: PR #77 (closed by its author 2026-01-28 to redo it); replacement PR #133 "add sdl2 and sdl3 WSI backends" opened 2026-03-05, still open, with the ImGui examples running on arm64 and x86_64; sample repo [sse2/dxmt-native-sample](https://github.com/sse2/dxmt-native-sample) (needs `-Ddxmt_wsi_sdl2=true` or `-Ddxmt_wsi_sdl3=true`; the sample ImGui backend was changed to not use D3DCompile "which DXMT doesn't provide") — [PR #77](https://github.com/3Shain/dxmt/pull/77), [PR #133](https://github.com/3Shain/dxmt/pull/133)
- `configure.sh` for native builds was removed in favour of documented steps (PR #134 closed 2026-03-23); the maintainer also said he lacks confidence in continuing Intel Mac support (paraphrase) — [PR #134](https://github.com/3Shain/dxmt/pull/134)
- The maintainer pointed native contributors at DXVK's `util_win32_compat.h` as the model for handling Windows APIs on Unix — [PR #75](https://github.com/3Shain/dxmt/pull/75)

**arm64 and Windows-side builds**
- meson.build maps `cpu_family` x86, x86_64 and aarch64 to install dirs (`aarch64-windows`, `aarch64-unix`) for Wine builds; the repo ships `build-arm64ec.txt` (arm64ec-w64-mingw32 toolchain, `-marm64x`); the unix lib builds for x86_64 or aarch64; DEVELOPMENT.md says for a cross build "you most likely want x86_64 at the moment" — [meson.build](https://github.com/3Shain/dxmt/blob/main/meson.build), [build-arm64ec.txt](https://github.com/3Shain/dxmt/blob/main/build-arm64ec.txt), [DEVELOPMENT.md](https://github.com/3Shain/dxmt/blob/main/docs/DEVELOPMENT.md). Native arm64 builds are explicitly supported (see PR #76 title and DEVELOPMENT.md).
- An optional D3D12 implementation exists behind `-Denable_d3d12=true` (off by default) — [meson.options](https://github.com/3Shain/dxmt/blob/main/meson.options)

**Feature coverage (from source and docs)**
- Geometry shaders are compiled to Metal mesh functions: `dxbc_converter_gs.cpp` builds mesh outputs, and `d3d11_pipeline_gs.cpp` creates a mesh render pipeline with `info.mesh_function = gs.Function` — [dxbc_converter_gs.cpp](https://github.com/3Shain/dxmt/blob/main/src/airconv/dxbc_converter_gs.cpp), [d3d11_pipeline_gs.cpp](https://github.com/3Shain/dxmt/blob/main/src/d3d11/d3d11_pipeline_gs.cpp)
- Documented unsupported or partial cases (debug-HUD "compatibility flags"): tessellator point/isoline output, combined geometry+tessellation pipeline, `DrawAuto()`, predicated commands, stream-output appending, multiple stream-output streams — [docs/COMPATIBILITY_FLAG.md](https://github.com/3Shain/dxmt/blob/main/docs/COMPATIBILITY_FLAG.md)
- NVAPI/NGX stubs, DLSS-to-MetalFX translation, HDR, MetalFX spatial upscaling, optional frame pacing — [Wiki: Vendor extensions](https://raw.githubusercontent.com/wiki/3Shain/dxmt/Vendor-Extensions.md), [docs/CUSTOMIZATION.md](https://github.com/3Shain/dxmt/blob/main/docs/CUSTOMIZATION.md)
- Open issues give a feel for remaining rough edges: quadratic cost of UpdateSubresource on a bound USAGE_DEFAULT constant buffer (#204), Buffer<float3> over an R32G32B32_FLOAT view reading one float per element (#228, opened 2026-10-08), GS/TS draw-argument marshal shader bugs causing multi-second stalls (#200) — [#204](https://github.com/3Shain/dxmt/issues/204), [#228](https://github.com/3Shain/dxmt/issues/228), [#200](https://github.com/3Shain/dxmt/issues/200)

### Inferences
- DXMT native is the only in-tree route I found that gives a D3D11/DXGI implementation as an ordinary Mach-O library with Metal underneath. For this project, the missing piece is the Win32 window glue: whatever implements CreateWindowEx/HWND must hand DXMT a CAMetalLayer (the SDL3 PR is the template; SDL3's Metal view creation is the likely mechanism, but I did not read PR #133's diff beyond the file list: README, meson files, d3d11_swapchain.cpp, wsi_monitor_*).
- DXMT's `d3d11.dylib`/`dxgi.dylib` are compiled for the native arm64 ABI, so lifted Win64-ABI code will need a thunk at each COM vtable call and at `D3D11CreateDevice`/`CreateDXGIFactory`. Because the plan maps the game's data at original addresses in the same process, pointers inside D3D11 descriptor structs can be passed through unchanged.
- Because DXMT's shader compiler ("airconv") is separable (see section B), a fallback is to lift only DXMT's shader path and write a thin custom D3D11 front end, but the existing device/context code is large (d3d11_context_imm.cpp, pipeline cache, GS/TS paths), so adapting it is likely cheaper than rewriting.
- LGPL-2.1+ is workable for personal use (dynamic linking or source form); the licence only creates obligations on distribution.
- The no-AI-contributions rule does not stop private forks, but upstream PRs for the native WSI work would have to be hand-written.

### Gaps
- No first-party performance numbers for DXMT vs D3DMetal vs DXVK+MoltenVK. A search surfaced community claims (e.g. Raft 85 FPS on DXMT vs 45 FPS on DXVK, Subnautica "almost double" D3DMetal) from forum/blog pages I did not open; treat as unverified. The "dxmt.report" compatibility site is a JavaScript app that returned no readable content.
- I did not read PR #133's code, so the exact Metal-layer creation path in the SDL WSI is unverified, and I did not confirm that the sample actually presents frames on arm64 beyond the contributor's screenshots and statements.
- Titanfall 2 under DXMT specifically: no source found.
- PR #213 (Metal interop interface) merge status not checked (API rate limit).

---

## A2. DXVK ("dxvk-native") and MoltenVK on macOS

### Takeaway
Mainline DXVK has a native (non-Wine) build with SDL3/SDL2/GLFW window backends, but it does not target macOS, and DXVK 3.x requires Vulkan 1.4 plus VK_EXT_transform_feedback, which stock MoltenVK 1.4.2 does not list. There is no stock geometry-shader support either (an unmerged third-party MoltenVK draft adds it). I found no shipped native Mac port using dxvk-native over MoltenVK; the one public evaluation I found (VirtualBox) abandoned it.

### Cited Findings
- Current DXVK: v3.1.1 (2026-09-15), v3.1 (2026-08-28), v3.0 (2026-06-25). DXVK 3.0 replaced the old shader translator with dxbc-spirv — [API releases](https://api.github.com/repos/doitsujin/dxvk/releases)
- Licence: zlib (copyright Philip Rebohle and others) — [LICENSE](https://github.com/doitsujin/dxvk/blob/master/LICENSE)
- DXVK 3.x driver requirements: Vulkan 1.4; required extensions `VK_EXT_depth_clip_enable`, `VK_KHR_maintenance5`, `VK_KHR_maintenance6`, `VK_KHR_load_store_op_none`, `VK_EXT_robustness2`, `VK_EXT_transform_feedback`; at least 256 bytes of push constants, descriptor indexing, 8/16/64-bit integers, `scalarBlockLayout`; Wine 10.1+ for winevulkan. Legacy DXVK 1.10.3 needs only Vulkan 1.1 — [DXVK wiki: Driver support](https://raw.githubusercontent.com/wiki/doitsujin/dxvk/Driver-support.md); [v3.0 release notes](https://github.com/doitsujin/dxvk/releases/tag/v3.0) ("Many of the features and extensions added in Vulkan 1.4 are now required")
- DXVK 2.7 (2025-07-05) made `VK_KHR_maintenance5` mandatory — [v2.7 notes](https://github.com/doitsujin/dxvk/releases/tag/v2.7)
- DXVK Native in the README: runs "natively without Wine", replaces Windows-isms (HWND becomes SDL_Window*), WSI chosen with `DXVK_WSI_DRIVER` from SDL3, SDL2 or GLFW, ships a slim set of Windows headers plus the MinGW D3D headers; release builds use the Steam Runtime (Linux). The README does not mention macOS or MoltenVK — [README](https://github.com/doitsujin/dxvk/blob/master/README.md). meson.build's non-Windows branch errors out unless SDL3, SDL2 or GLFW is found — [meson.build](https://github.com/doitsujin/dxvk/blob/master/meson.build)
- Maintainer stance on macOS: PR #5577 "macOS/native: improve WSI fallback and related fixes" (2026-04-09) was closed unmerged with the comment "We're not interested in maintaining support for an incomplete Vulkan implementation just so one particular game mod works well" — [PR #5577](https://github.com/doitsujin/dxvk/pull/5577) (quote read on the page; author/merge details from fetch summary). PR #5824 "[build] fix native build on macos" (opened 2026-08-02) is a still-open draft that disables features that crash on MoltenVK and KosmicKrisp, with maintainer questions about hard-coded SDL2 (summary only) — [PR #5824](https://github.com/doitsujin/dxvk/pull/5824)
- MoltenVK releases: v1.3.0 2025-05-02 (Vulkan 1.3), v1.4.0 2025-08-20 (Vulkan 1.4), v1.4.1 2025-11-30, v1.4.2 2026-07-24; v1.4.2 raised the minimum to macOS 12 / iOS 15 and added `VK_EXT_sampler_filter_minmax` (Apple10 GPUs on macOS 26) — [API releases](https://api.github.com/repos/KhronosGroup/MoltenVK/releases). Apache-2.0 — [API repo](https://api.github.com/repos/KhronosGroup/MoltenVK)
- MoltenVK's runtime user guide lists `VK_EXT_depth_clip_enable`, `VK_EXT_robustness2`, `VK_KHR_maintenance5/6`, `VK_EXT_descriptor_indexing`, `VK_EXT_scalar_block_layout`, `VK_EXT_swapchain_maintenance1` and many more, but it contains no `VK_EXT_transform_feedback` entry and no mention of geometry shaders — [MoltenVK_Runtime_UserGuide.md](https://github.com/KhronosGroup/MoltenVK/blob/main/Docs/MoltenVK_Runtime_UserGuide.md) (I grepped the full file)
- Open draft MoltenVK PR #2786 (2026-07-23) "Rebase geometry shaders and complete transform-feedback execution" revives older WIP work (#1815 geometry shaders via Metal object/mesh stages; #1943 VK_EXT_transform_feedback from 2023). It states "Stock MoltenVK did not expose geometry shaders", reports `geometryStreams` false (stream 0 only) and `shaderCullDistance` false, says DXVK's exact FL 11_1 feature request therefore fails with `VK_ERROR_FEATURE_NOT_PRESENT`, and "does not claim general DXVK or game compatibility". A MoltenVK contributor (squidbus; role not verified) said faking features for DXVK "would probably not be accepted". The submitter says an LLM helped generate the PR — [PR #2786](https://github.com/KhronosGroup/MoltenVK/pull/2786), [PR #1943](https://github.com/KhronosGroup/MoltenVK/pull/1943)
- MoltenVK tessellation exists (v1.3.0 notes mention a tessellation fix) — [API releases](https://api.github.com/repos/KhronosGroup/MoltenVK/releases)
- What CodeWeavers did: CrossOver 23 (Aug 2023) announced "initial support" for geometry shaders and transform feedback on macOS — [9to5Mac](https://9to5mac.com/2023/08/16/crossover-23-directx-12/) (quote from a summary of the article). A former CodeWeavers engineer commented in 2023 that CW was investigating mesh shaders for it; another commenter said it appeared to be done "on the wine side" and that DXVK GS did not work — [PR #1943 comments](https://github.com/KhronosGroup/MoltenVK/pull/1943). Search-result summaries of the CodeWeavers blog say CX23 shipped DXVK 1.10.3 and MoltenVK 1.2.3 and recommended wined3d over DXVK for geometry-shader games (unverified; the blog page was blocked). The Gcenx DXVK-macOS fork's visible branch is "1.10.x" (summary only; no patch list found) — [Gcenx/DXVK-macOS](https://github.com/Gcenx/DXVK-macOS)
- Public native-port precedent: none found. The VirtualBox team's DXMT-native PR says DXVK on MoltenVK "doesn't work out" — [PR #76](https://github.com/3Shain/dxmt/pull/76)
- Apple-driven alternative Vulkan-on-Metal: LunarG/Mesa "KosmicKrisp" claims Vulkan 1.3 CTS conformance; search summaries say geometry shaders are not supported and tessellation/mesh shaders are future goals (unverified) — [LunarG at XDC 2025](https://www.lunarg.com/lunarg-at-xdc-2025-kosmickrisp-overview/)

### Inferences
- The plan's "DXVK native over MoltenVK for first pixels" would need (a) a pre-3.0 DXVK that does not require transform feedback or Vulkan 1.4, or (b) a patched MoltenVK with the unmerged geometry/XFB work, or (c) a DXVK fork that drops GS/XFB requirements. All are hacking projects, and Titanfall 2 may or may not use GS or stream-out (unknown). Compared with DXMT native (which already handles GS and stream-out via Metal), this looks like the harder first-pixels path.
- DXVK Native is "Linux-first": it is designed for SDL3/SDL2/GLFW and Steam Runtime builds, and maintainers have declined macOS support.

### Gaps
- Exact DXVK version that first required VK_EXT_transform_feedback (the wiki documents only 3.x and 1.10.3).
- Whether any 2.x DXVK still runs on current MoltenVK with the CrossOver-era patches; the CodeWeavers MoltenVK/DXVK patch set itself was not located.
- KosmicKrisp feature list from a primary page.

---

## A3. Apple Game Porting Toolkit and D3DMetal

### Takeaway
Apple's toolkit (now GPTK 4) is an evaluation environment (Wine plus D3DMetal) and a shader converter, not a linkable D3D11 library. The D3DMetal licence I read (EA18380, dated 17 Aug 2023) grants a personal licence to install and test "for the sole purpose of developing, testing, or evaluating video games for use on Apple-branded products", restricts distribution to non-commercial, and bans reverse engineering, decompiling, modifying or deriving works. I found no public documentation for calling D3DMetal from a non-Wine process.

### Cited Findings
- Apple's GPTK page (opened) offers "Download Game Porting Toolkit 4"; it includes the Metal shader converter, an "evaluation environment for Windows games" (now supporting Metal 4), Metal developer tools for Windows, Mac Remote Developer Tools, metal-cpp, and a companion GitHub repo of agent skills and sample code; it notes CrossOver and Homebrew as community routes to the evaluation environment — [Apple GPTK page](https://developer.apple.com/games/game-porting-toolkit/)
- GPTK 4 announced at WWDC26; coverage dated 2026-06-08 (article not opened, 403): agentic-porting skills, command-line Metal tools — [AppleInsider URL](https://appleinsider.com/articles/26/06/08/game-porting-toolkit-4-ushers-in-support-for-agentic-coding), [WWDC26 session 357](https://developer.apple.com/videos/play/wwdc2026/357/) (titles from search results; unverified content)
- Companion repo apple/game-porting-toolkit: agent skills, workflow skills, metal-cpp, code samples; licence Apache-2.0; it does not discuss D3DMetal or DXBC (fetch summary) — [apple/game-porting-toolkit](https://github.com/apple/game-porting-toolkit)
- Gcenx's binary redistribution of the earlier toolkit: latest release in the API listing is "Game Porting Toolkit 3.0-3" (2026-03-03); requirements "Apple Silicon Macs running macOS 14 Sonoma or higher", 16 GB RAM recommended; the archive "contains Game porting toolkit, please ensure you comply with Apple's License.pdf for D3DMetal"; source is based on apple/homebrew-apple wine sources; no GPTK 4 build appears at the top of that list — [API releases](https://api.github.com/repos/Gcenx/game-porting-toolkit/releases)
- Licence text (read in full, 6 pages): "Apple Inc. Software License Agreement for Game Porting Toolkit, for use on Apple-branded systems", form EA18380, 8/17/2023. Key terms (paraphrased): "Framework" means D3DMetal.framework; §2A limited, non-exclusive, non-transferable, personal copyright licence to install, internally use and test for the sole purpose of developing, testing or evaluating video games for use on Apple-branded products, and to distribute solely for non-commercial purposes; §2C may not be installed or run on non-Apple devices, the Framework or Redistributables may be distributed separately but remain subject to the non-commercial restriction; §2D no reverse engineering, decompiling, disassembly, decrypting, modifying or creating derivative works (except where law or open-source licences permit); an "IMPORTANT NOTE" says use only with materials you own, are authorised to use, or are non-copyrighted; §3 evaluation copies may not be resold or transferred; as-is warranty; liability cap of $50; Delaware law — [License.pdf (GitHub attachment linked from Gcenx releases)](https://github.com/user-attachments/files/23971305/License.pdf)
- Apple's page itself frames the environment as a way to "evaluate your unmodified Windows executable on Apple silicon" — [Apple GPTK page](https://developer.apple.com/games/game-porting-toolkit/)
- Search-result summaries say D3DMetal supports D3D11 and D3D12 and is patched into Wine in the toolkit (unverified) — [Gcenx releases page via search](https://github.com/Gcenx/game-porting-toolkit/releases)

### Inferences
- For a personal project, the licence's "developing, testing, or evaluating video games" purpose plausibly covers using D3DMetal while building a Mac port, but the licence grants no API, headers or linking rights, and §2D prohibits modifying or deriving from it. Using it as a library inside a custom host is outside anything the licence describes. This is not legal advice; the licence of the GPTK 4 download was not read and may differ from the 2023 text.
- Since the toolkit is wired into Wine's d3d11/dxgi shims, calling D3DMetal from a lifted-code process would mean reverse-engineering its undocumented interface, which §2D forbids. DXMT or a custom backend avoid that.

### Gaps
- Whether D3DMetal.framework has arm64/arm64e slices or is x86_64-only: not verified (no local copy on this Mac; Gcenx archive not downloaded; search returned nothing).
- GPTK 4 version string, release date, OS requirement (macOS 27?) and licence: Apple's page does not state them in the text I read.
- Whether D3DMetal can be loaded outside Wine at all: no documentation found.
- 2025-2026 D3DMetal changes beyond "GPTK 3 (Dec 2025 build) to GPTK 4 (June 2026), Metal 4 support in the evaluation environment".

---

## B. DXBC (D3D11 bytecode) to Metal shader path

### Takeaway
Apple's Metal Shader Converter takes only DXIL (SM6.0 to 6.6), not DXBC, and the only DXBC-to-DXIL converter (Microsoft's dxilconv) builds only on Windows. The practical DXBC-to-Metal options are DXMT's `airconv` (DXBC to Metal AIR/metallib, with a CLI, handles GS/HS/DS) and a two-step DXBC-to-SPIR-V (dxbc-spirv) then SPIR-V-to-MSL (SPIRV-Cross) pipeline. All can run ahead of time on the Mac.

### Cited Findings
- Metal Shader Converter (Apple): version "4.0 beta" on Apple's page; command-line `metal-shaderconverter` and dynamic library `libmetalirconverter`; basic usage "takes a DXIL file as input and produces a metallib"; the C API constructs input via `IRObjectCreateFromDXIL`; supports SM6.0-SM6.6; tool needs macOS 13 and Xcode 15 or later; produced libraries need Argument Buffers Tier 2 and macOS 14 / iOS 17 or later; version 4.0 highlights "2D compute derivatives; NaN/Inf optimization on by default" — [Apple MSC page](https://developer.apple.com/metal/shader-converter/)
- MSC licence/redistribution terms are not stated on that page (see Gaps).
- A summary of search results says MSC handles geometry and tessellation stages (from the WWDC23 session; unverified) — [WWDC23 10124](https://developer.apple.com/videos/play/wwdc2023/10124)
- DXC's `dxbc2dxil` (dxilconv) converts DXBC to DXIL, but in the DXC build files it is only added under `if(WIN32)` — [projects/CMakeLists.txt](https://github.com/microsoft/DirectXShaderCompiler/blob/main/projects/CMakeLists.txt), [projects/dxilconv/CMakeLists.txt](https://github.com/microsoft/DirectXShaderCompiler/blob/main/projects/dxilconv/CMakeLists.txt); [docs/DXIL.rst](https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/DXIL.rst) describes dxbc2dxil. DXC licence: University of Illinois/NCSA — [LICENSE.TXT](https://github.com/microsoft/DirectXShaderCompiler/blob/main/LICENSE.TXT)
- DXMT `airconv`: "DXBC to Metal AIR transpiler"; CLI flags include `-A` (write .metallib), `-S` (LLVM assembly), `--disas-dxbc`, and options to compile a stage together with its neighbour (`--vertex-before-hull`, `--hull-after-vertex`, `--hull-before-domain`, `--vertex-before-geometry`, `--geometry-after-vertex`); built as a native `airconv` executable and as a static library; writes the metallib container itself (`metallib_writer.cpp`); public C header `airconv_public.h` (version constant 26) defines reflection structs for constant buffers, tessellation, geometry shader and pixel shader outputs; depends on LLVM 15 static libs — [airconv_cli.cpp](https://github.com/3Shain/dxmt/blob/main/src/airconv/airconv_cli.cpp), [airconv/meson.build](https://github.com/3Shain/dxmt/blob/main/src/airconv/meson.build), [darwin/meson.build](https://github.com/3Shain/dxmt/blob/main/src/airconv/darwin/meson.build), [airconv_public.h](https://github.com/3Shain/dxmt/blob/main/src/airconv/airconv_public.h). Licence LGPL-2.1+ (as DXMT).
- DXMT caches converted AIR shaders and the Metal PSO cache on disk (v0.71), and found that pipeline-compile time was a stutter source — [v0.71 notes](https://github.com/3Shain/dxmt/releases/tag/v0.71)
- dxbc-spirv (Philip Rebohle, MIT): "an SSA-based compiler for Direct3D Shader Models 5.1 and older" with its own IR; lowers to SPIR-V 1.6 with the Vulkan memory model (optionally SPV_KHR_float_controls2) and says older SPIR-V compatibility "is not a priority"; ships a disassembler (`dxbc_disasm`) and compiler CLI (`dxbc_compiler shader.dxbc --spv shader.spv`); DXVK 3.0 adopted it for all supported shader models — [README](https://github.com/doitsujin/dxbc-spirv/blob/master/README.md), [LICENSE](https://github.com/doitsujin/dxbc-spirv/blob/master/LICENSE), [DXVK v3.0](https://github.com/doitsujin/dxvk/releases/tag/v3.0)
- SPIRV-Cross (Khronos) is Apache-2.0 and is the SPIR-V-to-MSL back end MoltenVK uses; MoltenVK v1.3.0 added `VK_KHR_vulkan_memory_model` — [LICENSE](https://github.com/KhronosGroup/SPIRV-Cross/blob/main/LICENSE), [MoltenVK v1.3.0 notes](https://github.com/KhronosGroup/MoltenVK/releases/tag/v1.3.0)
- HLSLcc (Unity): takes DX bytecode and outputs GLSL, GLSL ES, Vulkan-GLSL and Metal Shading Language; "used to generate all shaders in Unity for OpenGL, OpenGL ES 3.0+, Metal and Vulkan"; permissive MIT-style licence (copyright James Jones and Unity Technologies; includes bstring under its own licence) — [README](https://github.com/Unity-Technologies/HLSLcc/blob/master/README.md), [license.txt](https://github.com/Unity-Technologies/HLSLcc/blob/master/license.txt)
- vkd3d-shader (Wine): an experimental MSL target exists behind a `VKD3D_SHADER_UNSUPPORTED_MSL` build flag, described in search summaries as taking DXIL (not DXBC) input (unverified; Wine GitLab blocked) — [vkd3d ChangeLog via search](https://gitlab.winehq.org/wine/vkd3d/-/blame/master/ChangeLog.md)
- DXBC has to be fed per-pipeline: DXMT's own flags ("vertex-before-geometry" etc.) and its reflection structures show conversion depends on neighbouring stages and pipeline state.

### Inferences
- Offline-AOT candidate ranking for DXBC to Metal on macOS: (1) DXMT airconv, since it is already a DXBC-to-AIR/metallib compiler and tied to DXMT's resource-binding model (argument tables, constant-buffer tables), so shaders it emits only work with DXMT's runtime; (2) dxbc-spirv then SPIRV-Cross to MSL then Apple's `metal` compiler, which gives readable MSL for debugging but needs you to supply the binding model and GS/tessellation/stream-out emulation yourself; (3) HLSLcc, which is SM4/5-era and permissively licensed but Unity-targeted (its README does not say whether geometry or tessellation stages are handled on the Metal output).
- A full-AOT build of Titanfall 2's shader cache is plausible only for stages whose output does not depend on runtime pipeline state; DXMT specialises at PSO creation, so expect an on-disk cache (as DXMT already does) rather than a purely offline shader library.
- MSC would require either Windows-only dxilconv on another machine (not the Ubuntu box without Wine) or a different DXBC-to-DXIL route; it is not attractive here.

### Gaps
- MSC and libmetalirconverter redistribution/licence terms are not stated on Apple's page; I did not read the installer licence. Searches only surfaced forum questions about it.
- No benchmarks or correctness comparison of airconv vs dxbc-spirv+SPIRV-Cross vs HLSLcc.
- HLSLcc's geometry/tessellation coverage on the Metal output not confirmed.
- Whether any maintained DXBC-to-DXIL converter exists outside Windows: none found.

---

## A4. Other D3D11 options and "thin custom backend" models

### Takeaway
Nothing else gives D3D11 on Metal as a library: wined3d targets GL/Vulkan, ANGLE and Mesa go in other directions, and the recomp-project render layers (plume, RT64) are modern-API RHIs that sit below a custom front end, not D3D11 implementations. They are useful as a model for a small custom D3D11-subset backend, with DXMT as the reference.

### Cited Findings
- wined3d (Wine, LGPL-2.1+) has `adapter_gl.c` and `adapter_vk.c` (Vulkan) back ends, and no Metal back end — [adapter_vk.c](https://github.com/wine-mirror/wine/blob/master/dlls/wined3d/adapter_vk.c) (header: LGPL 2.1 or later, copyright Henri Verbeet for CodeWeavers), [adapter_gl.c](https://github.com/wine-mirror/wine/blob/master/dlls/wined3d/adapter_gl.c). ANGLE's own table marks desktop GL on Mac OS X "deprecated" — [ANGLE README](https://github.com/google/angle/blob/main/README.md)
- ANGLE translates OpenGL ES to Vulkan, desktop GL, GL ES, Direct3D 9/11 and Metal: the wrong direction (it consumes GL, it does not implement D3D11) — [ANGLE README](https://github.com/google/angle/blob/main/README.md). An open DXMT issue shows ANGLE's D3D11 back end running over DXMT as just another D3D11 client — [issue #183](https://github.com/3Shain/dxmt/issues/183)
- Mesa's `d3d10umd` frontend is a Windows (WDDM) user-mode D3D10 driver running on llvmpipe/softpipe, D3D10 only, no Wine integration (search summary, unverified) — [Mesa MR 10687](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/10687). Mesa's Dozen (Vulkan over D3D12) is also the wrong direction (not verified here).
- plume (renderbag, MIT): low-level RHI in C++ with Direct3D 12, Vulkan and Metal backends, "bring your own compiler" for shaders, built originally for RT64; the README says it is not yet production-ready (barrier management and texture transitions); a macOS Metal implementation is in the tree (fetch summary) — [plume](https://github.com/renderbag/plume), [LICENSE](https://github.com/renderbag/plume/blob/main/LICENSE)
- RT64 (MIT, N64 renderer) and the N64 recomp stack: N64Recomp (MIT), N64ModernRuntime (GPL-3.0; "ultramodern" reimplements libultra and expects the host to register a renderer, RT64 recommended; "librecomp" bridges recompiled code to it) — [N64ModernRuntime README](https://github.com/N64Recomp/N64ModernRuntime/blob/main/README.md), [COPYING](https://github.com/N64Recomp/N64ModernRuntime/blob/main/COPYING), [N64Recomp LICENSE](https://github.com/N64Recomp/N64Recomp/blob/main/LICENSE), [RT64 LICENSE](https://github.com/rt64/rt64/blob/main/LICENSE)
- XenonRecomp (MIT) "does not provide a runtime implementation", only converts code; it passes CPU state as the first argument to every function and the guest base pointer as the second, and uses SIMD Everywhere for ARM64 VMX — [XenonRecomp README](https://github.com/hedge-dev/XenonRecomp/blob/main/README.md). UnleashedRecomp (GPL-3.0) layers an Xbox 360 kernel translation on top and renders through plume (summary only); a README snippet in my fetch summary says macOS is not supported yet, which may be dated since plume now has Metal files (conflicting; unresolved) — [UnleashedRecomp](https://github.com/hedge-dev/UnleashedRecomp); XenosRecomp (MIT) converts Xbox 360 shaders to HLSL (summary only) — [XenosRecomp license](https://github.com/hedge-dev/XenosRecomp/blob/main/LICENSE.md)
- Closest analogue to this project: Theseus (evmar) is "a compiler that translates DOS and 32-bit Windows x86 code to Rust" plus "an implementation of (some of) the DOS/win32 API"; developed on an ARM Mac; "very experimental"; "a few intrepid people have successfully brought up some commercial games"; generated code is not a derivative work and the support crates are Apache-2.0; examples include "basic DirectX demos". It is 32-bit x86 only, so it cannot lift Titanfall 2's x86-64 code. It replaced retrowin32 (Apache-2.0, abandoned per its README) — [Theseus README](https://github.com/evmar/theseus), [LICENSE](https://github.com/evmar/theseus/blob/main/LICENSE), [blog](https://neugierig.org/software/blog/2026/04/theseus.html), [retrowin32 README](https://github.com/evmar/retrowin32)

### Inferences
- The Xbox/N64 recomps avoid implementing the original graphics API: they override rendering at function boundaries and use RHIs (plume/RT64) with offline shader translation (XenosRecomp). A Windows D3D11 game calls through COM vtables with data-driven state, so replacing it at the D3D11 API level (DXMT-style) is the cheaper route; a plume-style RHI on top of a hand-rolled D3D11-subset front end is the alternative if you want to own the stack.
- bgfx-style renderer wrapping was not researched (no source read).

### Gaps
- plume's Metal backend maturity and whether UnleashedRecomp's macOS status has changed.
- No source for how much of D3D11 a "subset" backend needs for Titanfall 2; that comes from the Phase 0 import tally.

---

## A5. D3D11 features that are pain points on Metal (for a 2016 Source-derived deferred renderer)

### Takeaway
Metal has no geometry shader or stream-out stage, so those must be emulated (DXMT does it with mesh shaders and compute, with documented gaps); tessellation maps onto Metal's tessellator but needs hull-shader emulation; BC textures and depth clamp are available. Practical hazards documented in DXMT's release notes are shader-compile time, case-sensitive signatures and fast-math/precision differences. Which of these Titanfall 2 uses is not known.

### Cited Findings
- Geometry shaders: no native Metal stage; DXMT compiles them to Metal mesh functions (source above); MoltenVK mainline lacks them (PR #2786 text); documented DXMT gaps: GS+tessellation together, stream-out appending, multiple SO streams, DrawAuto, predication — [COMPATIBILITY_FLAG.md](https://github.com/3Shain/dxmt/blob/main/docs/COMPATIBILITY_FLAG.md)
- Mesh render pipelines are available from macOS 13 — [MTLMeshRenderPipelineDescriptor](https://developer.apple.com/documentation/metal/mtlmeshrenderpipelinedescriptor) (Apple JSON doc metadata: macOS 13.0)
- Tessellation: DXMT's first implementation allocated large temporary buffers and was rewritten on mesh shaders in v0.70 (2025-10-17), which also needed indirect tessellation draws; The Medium showed memory blowups with the old design — [v0.70 notes](https://github.com/3Shain/dxmt/releases/tag/v0.70). Metal's `maxTessellationFactor` defaults to 16 and tops out at 64 — [Apple doc](https://developer.apple.com/documentation/metal/mtlrenderpipelinedescriptor/maxtessellationfactor)
- BC textures: `MTLDevice.supportsBCTextureCompression` exists ("whether you can use textures that use BC compression") since macOS 11 / iOS 16.4 — [Apple doc](https://developer.apple.com/documentation/metal/mtldevice/supportsbctexturecompression)
- Depth clip: `MTLDepthClipMode` (clip or clamp) since macOS 10.11 — [Apple doc](https://developer.apple.com/documentation/metal/mtldepthclipmode); MoltenVK exposes `VK_EXT_depth_clip_enable` — [user guide](https://github.com/KhronosGroup/MoltenVK/blob/main/Docs/MoltenVK_Runtime_UserGuide.md)
- Shader and pipeline quirks DXMT hit in real games: Metal signature names are case-sensitive but D3D11's are not (BG3 depth-of-field black background); Metal's shader compiler hangs on huge unoptimised compute shaders with thousands of branches (Elite Dangerous); a wrongly implemented `sync` caused GPU hangs (Genshin Impact, SOTTR benchmark); BGRX8 needs X read as 1.0 (Stellaris); uncached PSO compiles taking seconds caused first-run stutter; fast-math changes affected correctness vs speed — [v0.70](https://github.com/3Shain/dxmt/releases/tag/v0.70), [v0.71](https://github.com/3Shain/dxmt/releases/tag/v0.71), [v0.73](https://github.com/3Shain/dxmt/releases/tag/v0.73)
- Resource model costs: D3D11 immediate-context operations such as UpdateSubresource on bound default-usage buffers can become quadratic in DXMT today — [issue #204](https://github.com/3Shain/dxmt/issues/204). DXMT's own D3D11-to-Metal mapping table (usage modes to storage modes, view concepts) is in an unfinished glossary — [docs/GLOSSARY.md](https://github.com/3Shain/dxmt/blob/main/docs/GLOSSARY.md)
- Titanfall engine background: search-result summaries say the Titanfall engine branch is a heavily modified Source (Portal 2) branch with native D3D11 ("dxlevel 110") and a D3D12 option ("dxlevel 120"), PBR, in-house TSAA, and real-time texture streaming (GDC 2017 talk "Efficient Texture Streaming in Titanfall 2"); I could not open the Valve wiki (403) — [Valve Developer Community (via search)](https://developer.valvesoftware.com/wiki/Titanfall_engine_branch), [GDC 2017 listing](https://gdconf.com/news/attend-gdc-2017-behind-scenes-look-titanfall-2s-textures)
- Titanfall 2 compatibility data points under Wine-based layers (not native): AppleGamingWiki and doesitarm pages list "playable" results via CrossOver (search summary only) — [AppleGamingWiki](https://www.applegamingwiki.com/wiki/Titanfall_2)

### Inferences
- A deferred D3D11 renderer from 2016 commonly leans on compute shaders, UAVs, structured buffers, MRT G-buffers, cascaded shadows and possibly stream-out or GS for particles or shadows; all except GS/stream-out/tessellation are core in DXMT. Whether Titanfall 2 uses GS, HS/DS or stream-out must be measured: count D3D11 `CreateGeometryShader`/`CreateHullShader`/`CreateDomainShader`/`CreateGeometryShaderWithStreamOutput` imports and DXBC shader-type headers from the shipped shader caches in Phase 0 (DXMT's `airconv --disas-dxbc` and dxbc-spirv's `dxbc_disasm` can classify them).
- Feature level 11_0 itself is not the issue on Apple Silicon; the risk is the long tail (typed UAV loads, UAV counters, `Buffer<float3>`-style formats, ROV, conservative raster), none of which I could confirm for Metal. DXMT issue #228 is an example of format-view bugs still being found.

### Gaps
- No primary source for what Titanfall 2's D3D11 renderer uses.
- Apple Silicon support matrix for BC1-BC7 (the property exists; I did not confirm it returns true on all M-series GPUs).
- ROV, conservative rasterisation, typed UAV load support on Apple GPUs not checked.

---

## C1. Winelib on macOS arm64 and native Wine

### Takeaway
Upstream Wine could not run natively as arm64 on macOS until August 2026, when Apple shipped OS features (x18 mode, 4K pages, relocatable PAGEZERO, TSO) that Wine developers say make it possible; upstream patches were "coming soon" as of 2026-08-07 and CodeWeavers ships a preview build. Wine's DLLs are not designed to be linked into a plain Mach-O app: ntdll connects to (or starts) a wineserver and needs a Wine prefix. Winelib itself is a source-porting mechanism. Treat Wine DLLs as a source quarry, not a linkable runtime.

### Cited Findings
- Brendan Shanks (CodeWeavers) posted on wine-devel on 2026-08-07 that "all the pieces are now present in macOS to natively run ARM64 Wine"; CodeWeavers' CrossOver Preview for macOS (31 July 2026) uses ARM64 Wine plus FEX for i386/x86_64 (ARM64EC) emulation; upstream MRs were to follow — [wine-devel thread](https://list.winehq.org/hyperkitty/list/wine-devel@list.winehq.org/thread/CKG5CEN2BE5VRXZ7O7NX4YUSBH3247WH/)
- OS features listed in that post: macOS 26.5 "x18 custom ABI" mode (header `os/arch/arm64.h`; older SDK linking workaround `-Wl,-platform_version,macos,11.0,11.0`); `posix_spawnattr_set_4k_page_size_np()` for 4K pages (macOS arm64 normally uses 16K pages); PAGEZERO: arm64 macOS enforces at least 4 GB, and macOS/Xcode 26.4 add `-Wl,-x86_64_layout_emulation` so PAGEZERO can be resized (Wine's best approach: grow it to about 0x170000000); `thread_set_x86_64_compat()` enables TSO per thread (new threads do not inherit it); restricted entitlements `com.apple.developer.cross-architecture-support` (paid account) or `...-unmanaged` (free account) are required, or SIP can be disabled with kernel boot args — [wine-devel thread](https://list.winehq.org/hyperkitty/list/wine-devel@list.winehq.org/thread/CKG5CEN2BE5VRXZ7O7NX4YUSBH3247WH/)
- Before that, Wine bug 58567 (2025-08-04, Wine 10.12) reported native aarch64 macOS builds crashing: a "Malformed Mach-o file" with `-pagezero_size 0x1000`, and without it "failed to map the shared user data" at 0x7ffe0000 — [bug 58567](https://list.winehq.org/hyperkitty/list/wine-bugs@list.winehq.org/thread/AIEGB45LZWRUP7UAVUWQPPRXFVZU5I7L/)
- Search summary of the CodeWeavers announcement: the ARM64 builds are "very unpolished" and testing-only; Rosetta 2 largely discontinued with macOS 28 "next year"; timeline Wine 8.0 PE/WoW64, 9.0 native Windows ARM binaries, 10.0 ARM64EC, Nov 2025 Linux ARM64 CrossOver Preview, July 2026 macOS (unverified; blog blocked) — [CodeWeavers post](https://www.codeweavers.com/blog/mjohnson/2026/7/31/crossover-preview-the-right-to-bear-arm64-on-mac), [PortJump post](https://www.codeweavers.com/blog/orudge/2026/6/19/portjump-update-upcoming-changes-to-macos-support-for-intel-based-applications)
- Wine 11.16 released around 2026-08-21 with "improved ARM64 support" (search snippet only) — [AlternativeTo](https://alternativeto.net/news/2026/8/wine-11-16-brings-va-api-hardware-video-decoding-improved-arm64-support-and-35-bug-fixes/)
- Winelib definition: `winegcc` is "a gcc wrapper which tries to provide a MinGW compatible compiler", used to recompile Windows source "under Winelib on Linux" — [winegcc man page](https://github.com/wine-mirror/wine/blob/master/tools/winegcc/winegcc.man.in)
- wineserver dependency in code: `server_connect()` in ntdll's unix side `chdir`s to the server directory, calls `start_server()` when it is missing, retries up to six times, and honours `WINESERVERSOCKET`; the same file also does a Mach `bootstrap_look_up` of the wineserver port on macOS (purpose not examined) — [dlls/ntdll/unix/server.c](https://github.com/wine-mirror/wine/blob/master/dlls/ntdll/unix/server.c). The loader entry point `__wine_main` calls `apple_main_thread()` on macOS — [dlls/ntdll/unix/loader.c](https://github.com/wine-mirror/wine/blob/master/dlls/ntdll/unix/loader.c)
- PE/Unix split in practice (DXMT as an example): PE builtin DLL plus `.so` unix library plus `winebuild --builtin` post-processing — [winemetal/meson.build](https://github.com/3Shain/dxmt/blob/main/src/winemetal/meson.build)
- Wine DLL sources are LGPL-2.1+ (example: wined3d adapter_vk.c header) — [adapter_vk.c](https://github.com/wine-mirror/wine/blob/master/dlls/wined3d/adapter_vk.c)

### Inferences
- A native arm64 Wine on macOS exists only as a CodeWeavers preview and not in an upstream release I could confirm; and it exists to run Windows ARM64/ARM64EC binaries with FEX, a different architecture from the lifted-C plan. It does not give you "Wine DLLs as libraries".
- Linking Wine's kernel32/user32/ntdll into the project's Mach-O would drag in wineserver, a prefix, Windows TEB/PEB assumptions (x18 or gs-based) and the PE/Unix loader. For an in-process lifted game, you would end up reimplementing the same layer anyway.
- The project's own process could use the same macOS 26.4-26.5 features (PAGEZERO resize via `-x86_64_layout_emulation`, per-thread TSO via `thread_set_x86_64_compat()`, 4K page spawn attribute), but the restricted entitlements apply. This is directly relevant to the plan's open items on mapping data at original addresses (images at 0x140000000-style addresses are above 4 GB, but any low-address data needs PAGEZERO changes) and on x86 memory ordering. I did not verify that these APIs are usable by an ordinary arm64 process rather than only Wine loaders; the post states the entitlements are needed "to run binaries using any of the above features".

### Gaps
- Whether upstream Wine has merged native macOS arm64 support by 2026-10-08.
- Any documented Winelib-on-macOS-arm64 usage; no source found.
- Wine Winelib guide pages (wiki.winehq.org and gitlab.winehq.org blocked).

---

## C2. Smaller Win32 reimplementations and building blocks

### Takeaway
There is no drop-in "Win32 for native macOS" library. Candidates are reference or partial: Theseus' win32 support crates (Apache-2.0, 32-bit-oriented, Rust), the CoreCLR PAL (MIT, covers only what the runtime needs), DXVK/DXMT's util_win32_compat approach for the D3D side, and SDL3 (zlib) as the window/input/audio base. GPL-licensed runtimes (N64ModernRuntime, UnleashedRecomp, ReactOS) are models, not components you would link.

### Cited Findings
- Theseus (see A4): Apache-2.0 support crates implementing part of the DOS/win32 API, developed on an ARM Mac, target also WebAssembly; translated programs have no x86/Windows dependency — [Theseus README](https://github.com/evmar/theseus)
- retrowin32 (Apache-2.0): "abandoned" per its README; had native SDL-based running (2023/02), Rosetta-based and Unicorn-based modes; ran a few console/GUI/DirectX programs — [README](https://github.com/evmar/retrowin32/blob/main/README.md), [LICENSE](https://github.com/evmar/retrowin32/blob/main/LICENSE)
- taviso/loadlibrary (GPL-2.0): a custom PE/COFF loader derived from ndiswrapper that lets "native Linux programs" load and call Windows DLL functions, aimed at fuzzing self-contained libraries; Windows Defender engine as the demo; Linux x86 scope — [README](https://github.com/taviso/loadlibrary/blob/master/README.md), [LICENSE](https://github.com/taviso/loadlibrary/blob/master/LICENSE)
- CoreCLR PAL: `src/coreclr/pal/inc/pal.h` exists in dotnet/runtime; repo licence MIT — [pal.h](https://github.com/dotnet/runtime/blob/main/src/coreclr/pal/inc/pal.h), [LICENSE.TXT](https://github.com/dotnet/runtime/blob/main/LICENSE.TXT). (I did not open pal.h to measure API coverage.)
- ReactOS: GPL-2.0 — [COPYING](https://github.com/reactos/reactos/blob/master/COPYING), [README](https://github.com/reactos/reactos/blob/master/README.md)
- SDL: zlib licence, copyright through 2026 — [SDL LICENSE.txt](https://github.com/libsdl-org/SDL/blob/main/LICENSE.txt). DXVK Native and the DXMT native-WSI PR both take SDL3/SDL2 as the window-system layer — [DXVK README](https://github.com/doitsujin/dxvk/blob/master/README.md), [DXMT PR #133](https://github.com/3Shain/dxmt/pull/133)
- DXVK's util_win32_compat.h is the maintainer-recommended reference for mapping Windows threading/synchronisation APIs to Unix for D3D layers — [DXMT PR #75](https://github.com/3Shain/dxmt/pull/75); DXVK ships "a slim set of Windows header definitions" for its native build — [DXVK README](https://github.com/doitsujin/dxvk/blob/master/README.md)
- XenonRecomp, UnleashedRecomp and N64ModernRuntime are architectural models: recompiled code gets a CPU-state struct and a guest-memory base pointer, and calls into a host runtime reimplementing the platform's OS APIs (Xbox 360 kernel layer; libultra) — [XenonRecomp README](https://github.com/hedge-dev/XenonRecomp/blob/main/README.md), [N64ModernRuntime README](https://github.com/N64Recomp/N64ModernRuntime/blob/main/README.md). Licences: XenonRecomp MIT, N64Recomp MIT, UnleashedRecomp GPL-3.0, N64ModernRuntime GPL-3.0 (see sources in A4).

### Inferences
- The realistic plan is a purpose-built shim layer sized to the game's actual imports, using SDL3 for window/input/audio, with DXMT-native for D3D11/DXGI; Theseus' Rust crates are the closest source to crib from for API semantics, but they are 32-bit-oriented, so struct layouts and calling conventions will differ.
- GPL-3.0 runtimes can be read for design but copying code would make a personal build GPL-bound (no obligation unless distributed).

### Gaps
- Real coverage numbers (functions implemented) for Theseus, retrowin32 and the CoreCLR PAL.
- Whether any of them implements the NT-level APIs a Source-derived engine uses (threads, TLS, SEH/exception handling, file mapping, `VirtualAlloc` at fixed addresses).

---

## C3. How many Win32 imports a game of this era uses

### Takeaway
I found no published count or Wine test data for a comparable game. The number must be measured from the game's PE import tables in Phase 0, as the plan already says.

### Cited Findings
- Nothing citable found: searches for import-count data on Titanfall 2 or similar 2016 D3D11 titles returned no usable sources.
- Indirect scale hints only: DXVK Native needs "a slim set of Windows header definitions" for D3D9/11 (so the D3D layer's own Win32 surface is small) — [DXVK README](https://github.com/doitsujin/dxvk/blob/master/README.md); the DXMT native build compiles with a stub `windows.h` (59-byte header including windows_base.h of about 11 KB) — [windows.h](https://github.com/3Shain/dxmt/blob/main/include/native/windows/windows.h), [windows_base.h](https://github.com/3Shain/dxmt/blob/main/include/native/windows/windows_base.h)

### Inferences
- The D3D layers need little Win32 (threads, events, critical sections, a few string/time calls); the large surface will be the game's own engine DLLs plus the MSVC runtime, third-party middleware, and Steam/EA/Origin SDK calls. Count per DLL and exclude calls that are CRT (ucrt/api-ms-win-crt-*), which a lifter can either lift from the statically linked CRT or replace with libc.
- A cheap method: `pefile`/`llvm-objdump --private-headers` import tables for every shipped DLL, plus delay-load imports, plus `GetProcAddress` string literals (dynamic imports), deduplicated by function name.

### Gaps
- All quantitative data.

---

## X. Cross-cutting: OS and process-level constraints for a native arm64 Mac build (macOS 27)

### Takeaway
Nothing I read says macOS 27 blocks DXMT, MoltenVK or Metal-based approaches (DXMT needs macOS 14+; MoltenVK 1.4.2 needs macOS 12+; GPTK 3.0 needs macOS 14+). The notable macOS-27-era facts are for process layout: PAGEZERO, x18, page size and TSO features arrived in macOS 26.4-26.5 and need entitlements.

### Cited Findings
- DXMT min macOS 14 (15 recommended) — [Wiki](https://raw.githubusercontent.com/wiki/3Shain/dxmt/Device-System-Runtime-Specifications.md)
- MoltenVK 1.4.2 runtime minimum macOS 12 — [user guide](https://github.com/KhronosGroup/MoltenVK/blob/main/Docs/MoltenVK_Runtime_UserGuide.md); Metal Shader Converter libraries need macOS 14+ — [Apple MSC page](https://developer.apple.com/metal/shader-converter/)
- GPTK 3.0-3 needs Apple Silicon and macOS 14+ — [Gcenx release](https://github.com/Gcenx/game-porting-toolkit/releases)
- macOS 26.4 and 26.5 process-level features and entitlement requirements — [wine-devel thread](https://list.winehq.org/hyperkitty/list/wine-devel@list.winehq.org/thread/CKG5CEN2BE5VRXZ7O7NX4YUSBH3247WH/)
- DXMT's default shader-cache path (`$(getconf DARWIN_USER_CACHE_DIR)/dxmt/<executable name>`) is documented in DEVELOPMENT.md and the v0.71 notes — [docs/DEVELOPMENT.md](https://github.com/3Shain/dxmt/blob/main/docs/DEVELOPMENT.md)
- DXMT debug aids: `MTL_SHADER_VALIDATION`, `MTL_DEBUG_LAYER`, `DXMT_CAPTURE_EXECUTABLE`/`DXMT_CAPTURE_FRAME` for Metal captures, `DXMT_LOG_LEVEL` — [docs/DEVELOPMENT.md](https://github.com/3Shain/dxmt/blob/main/docs/DEVELOPMENT.md)

### Inferences
- DXMT's reliance on `-framework MetalFX` (hard link, not weak) in the native build means the dylib will not load without that framework, which is present on current macOS.
- Using the macOS 26.4+/26.5 layout features from the project's own process may need a developer account entitlement or a SIP/boot-arg change; that is a decision point for the project, not a library question.

### Gaps
- No source on macOS 27 specific changes (Metal, entitlements, or deprecations) affecting any candidate.
- Whether the PAGEZERO/x18/TSO features work in an ordinary (non-Wine) arm64 process: the post describes Wine's use only.
