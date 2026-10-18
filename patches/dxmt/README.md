# Patches to DXMT (local branch `tf2` of third_party/dxmt)

One line per patch: what it does, why it is needed. Apply order is the file name order; `tools/dxmt/build.sh` creates
`tf2` from e94c312f5c054263acf261cfa109edf13e757587 when needed and applies the ones not yet applied (`git am`). Never
push these anywhere (DXMT's maintainers do not accept AI-written contributions). Design and measurements:
`docs/findings/35-dxmt-window.md` (patches 1 to 3), `docs/handoff/pic-fork.md` (4 and 5).

- `0001-native-an-HWND-is-a-host-owned-DXMTNativeWindow-carr.patch`: in a native build an `HWND` is a pointer to a
  `DXMTNativeWindow` that the host program owns (new header `include/native/dxmt_native_window.h`: the `CAMetalLayer`
  plus optional callbacks for client size, display, minimized, foreground, windowed resize request). winemetal's
  `CreateMetalViewFromHWND`/`ReleaseMetalView` retain and release that layer instead of asking Wine's Mac driver; the
  window functions answer from the handle instead of constants; `CreateSwapChain` returns `DXGI_ERROR_INVALID_CALL` for
  an `HWND` that is not such a window. Why: without it a swap chain's constructor calls `abort()`.
- `0002-native-enumerate-monitors-and-display-modes-through-.patch`: monitors are the online CoreGraphics displays
  that do not mirror another (`HMONITOR` = `CGDirectDisplayID`, output 0 = main display, sizes in points, modes reported
  and never set); util links CoreGraphics and CoreFoundation. Why: with no monitor `IDXGIAdapter::EnumOutputs` finds nothing, there is no mode
  list for an engine to read before it creates its swap chain, and the swap chain does not know the refresh rate it
  paces `Present(1)` with.
- `0003-native-no-warning-per-Present-for-the-missing-frame-.patch`: the `ReleaseSemaphore` stub in
  `src/util/util_win32_compat.h` warns only for a non-null handle. Why: a native build has no Win32 semaphores, the swap
  chain's frame latency semaphore is null, and releasing it on every `Present` logged one warning per frame (stderr and
  the log file). Not needed for correctness; drop it if it ever conflicts.
- `0004-native-the-host-program-hears-the-log-and-the-reason.patch`: the host registers a log function and an abort
  function (new header `include/native/dxmt_native_host.h`, state in `src/nativemetal/host_hooks.c`, exported from
  `winemetal.dylib` so that one registration covers dxgi, d3d11 and winemetal); the logger hands it every line before
  writing it, `UNIMPLEMENTED` and DXMT's other own `abort()` sites and, in a build with assertions, every failed
  `assert()` hand it the reason before the process dies; two debug-level lines say whether a shader variant came from
  the shader cache. Why: a wrong picture must stop the run with our report, not leave two lines in a log nobody reads.
  Proof: `sh tools/dxmt/hooks.sh` (both build kinds). Details: `docs/handoff/pic-fork.md`.
- `0005-native-dxgi-and-d3d11-find-their-neighbours-in-their.patch`: `dxgi.dylib` and `d3d11.dylib` get the rpath
  `@loader_path`. Why: copied into one directory (`Contents/Frameworks` of the signed bundle) they did not find
  `winemetal.dylib` and each other without `DYLD_LIBRARY_PATH`. Two lines of `meson.build`.
- `0006-native-DXMT_FRAME_STATS-N-prints-the-means-of-the-fr.patch`: `DXMT_FRAME_STATS=<N>` prints the means of
  DXMT's own frame counters (passes, command buffers, stalls, intervals) on standard error every N frames. A
  measuring switch; nothing runs when it is unset.
- `0007-native-DXMT_PASS_TIMES-N-prints-the-GPU-time-of-ever.patch`: `DXMT_PASS_TIMES=<N>` gives every command
  buffer a counter sample buffer and samples the GPU's clock at the stage boundaries of every render, compute and
  blit pass; every N presented frames it prints the means per frame by kind of pass (`PASS-TIMES`) and one frame's
  passes in order (`PASS-TRACE`). Why: at native size the GPU is the limit, and nothing else says where its time
  goes. All of it is in `src/winemetal/unix/winemetal_unix.c`; nothing runs when the variable is unset.
- `0008-native-DXMT_MERGE_WHY-N-prints-why-render-passes-wer.patch`: `DXMT_MERGE_WHY=<N>` counts, every N command
  buffers, what kept render passes from being joined (`MERGE-WHY`), and a render pass records whether a shader of it
  reads its own depth texture. No behaviour changes. Findings of 13 October: `NOTES.md`, entry 03:57.
- `0009-native-DXMT_EXP_SKIP_CLEAR-1-a-measuring-experiment-.patch`: `DXMT_EXP_SKIP_CLEAR=1` drops every
  depth-stencil clear that did not become a render pass's load action. **An experiment, wrong in general, off unless
  set**: it measures what the stand-alone clear before Present costs (2 to 5% at native size). The correct change
  (carrying the clear into the next command buffer) is not made. `NOTES.md`, 13 October, 06:25.
- `0010-native-measuring-experiments-for-the-native-Metal-qu.patch`: four more measuring experiments, **all off
  unless set, wrong pictures while set**: `DXMT_EXP_SCISSOR=1|2` (full-size passes drawn through a 1 by 1 scissor:
  what is left is geometry and fixed cost), `DXMT_EXP_SAMPLER=1|2|3` (no anisotropy, mip 2 at the finest, both),
  `DXMT_EXP_NO_RELOAD=1` (attachments not reloaded between passes), `DXMT_EXP_MERGE_RO=1` (read-only depth passes
  joined with writable ones even when a shader reads depth). For the native-Metal question of 14 October.
- `0011-airconv-measuring-experiments-DXMT_EXP_AIR_FAST-1-al.patch`: `DXMT_EXP_AIR_FAST=1` puts all fast-math flags on
  every floating-point instruction of a converted shader; `DXMT_EXP_AIR_O2=1` runs LLVM's O2 module pipeline after
  the converter's own pass list. **Measuring experiments, off unless set**; measure with `DXMT_SHADER_CACHE=0`.
- `0012-native-DXMT_EXP_NO_RELOAD-2-colour-attachments-only-.patch`: `DXMT_EXP_NO_RELOAD=2` skips reloading colour
  attachments only, so that the depth test and with it the work stay the same. An experiment, off unless set.

Faults and limits of DXMT that the project has met are listed in `docs/findings/dxmt-faults.md`. They are worked around
here; nothing is sent to DXMT's maintainer or to anyone else.

To regenerate after changing `tf2`:
`rm patches/dxmt/*.patch && git -C third_party/dxmt format-patch --zero-commit --no-signature -o "$PWD/patches/dxmt" e94c312f5c054263acf261cfa109edf13e757587..tf2`

## Licence

These patches change DXMT, which is under the GNU Lesser General Public License, version 2.1 or (at your
option) any later version; the patches are under the same licence (see NOTICE at the repository root).
