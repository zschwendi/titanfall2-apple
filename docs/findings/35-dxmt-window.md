# 35: a Direct3D 11 swap chain that presents into a Metal layer we own

Built and run 2026-10-09 on this Mac (Apple M3 Pro, macOS 27.0.1, Xcode 27.0, SDL3 3.4.14 from Homebrew). Subject: DXMT
commit `e94c312f5c054263acf261cfa109edf13e757587` plus `patches/dxmt/*.patch` (local branch `tf2` of
`third_party/dxmt`), built into `third_party/dxmt/build-native`. Follows `docs/findings/35-dxmt-native.md` (the earlier survey) and
`docs/findings/35-dxmt-build.md` (the build study). Tags: **[V: ran]** executed on this Mac, and where it says "checked"
the test program made the comparison itself; **[V: read]** source read; **[N]** inferred; **[?]** not determined. Paths in
`third_party/dxmt/` are written relative to it. The display during the measurements was the built-in panel at 1512x982
points, 120 Hz (the external monitor of the earlier studies was not connected); no measurement was made on a 60 Hz display.

## Summary

1. It works. With three patches (590 patch lines; +337/-19 in 7 DXMT files) a swap chain made with `IDXGIFactory::CreateSwapChain` on native DXMT presents into a `CAMetalLayer` that our program owns, in an SDL3 window: `Present` with sync interval 0, 1 and 2, `GetBuffer(0)`, `ResizeBuffers`, release and re-creation; nothing aborts.
2. An `HWND` is a pointer to a `DXMTNativeWindow` that the host owns: magic and version, the layer, and optional callbacks (client size, display, minimized, foreground, windowed resize request). Not the bare layer (earlier survey's idea): a layer cannot say what size the host told the application, and a wrong pointer could not be refused.
3. Monitors come from CoreGraphics inside DXMT (`HMONITOR` = `CGDirectDisplayID`, output 0 = main display, sizes in points): here one output, 132 modes from 960x600 to 3024x1964, current 1512x982 @ 120 Hz, equal to what SDL reports. The list must be the *online* displays: the *active* list is empty while the display sleeps (found because the display was asleep for the first run).
4. `sh tools/dxmt/window.sh` exits 0: `window_test` makes 1255 checks in about 6 s (window on screen about 5 s, not activated): every call's result, and for 99 frames both the back buffer read through a staging copy before `Present` and the layer's drawable copied back with Metal after it (every pixel one of the two frame-dependent colours, triangle area and orientation), at 640x360, 800x450 after a resize, 320x180, and from a second thread.
5. Pacing, on the live 120 Hz display: `Present(1)` frames were shown every 8.33 ms (sd 0.00, 72 of 72 shown), `Present(2)` every 16.67 ms; DXMT asks Metal for exactly `SyncInterval / refresh` (`presentDrawable:afterMinimumDuration:` 8.333 ms). `Present(0)` still ran at 120 fps with the layer's default three drawables, and at 234 fps with two. A free-running `Present(1)` loop has 62 ms from `Present` to shown (46 ms with frame latency 1).
6. DXMT still sets layer properties with `dispatch_sync` to the main queue: on a render thread `ResizeBuffers` blocked for the 301 ms the main thread slept and returned 0.4 ms after it began pumping SDL events. So SDL's pump does service it, and a main thread that waits for the render thread at that moment would deadlock.
7. DXMT autoreleases Objective-C objects on the thread that calls `Present` (ten per frame: it reads the layer's view, window and screen). Without an autorelease pool there, they pile up (1749 pool entries after 282 frames) and the view, window and layer are never freed; with a pool per frame the layer is deallocated at teardown. The runtime must wrap its calls into DXMT in pools.
8. `SDL_Metal_CreateView`'s layer has a trap: SDL rewrites `drawableSize` on every window size event, so a frame presented after a resize and before `ResizeBuffers` lands in the top-left corner of a larger drawable. A view of our own (test option `--own-layer`) does not have it. The runtime should own the view.
9. Log: per swap chain 4 Win32 stub warnings at creation and 1 at release, nothing per frame (patch 0003 removed one warning per `Present`); `SetFullscreenState(TRUE)` fails cleanly with `DXGI_ERROR_NOT_CURRENTLY_AVAILABLE`.
10. Deferred: fullscreen, mode switches, physical-pixel units for monitors, HDR, the HUD label. Needs a human: that the picture on the glass looks right (run `sh tools/dxmt/window.sh --foreground --hold 5`). Not determined: a 60 Hz or external display, a sleeping display with the final test, tearing with `Present(0)`.

## 1. Design

### 1.1 What an `HWND` is

In DXMT's native build an `HWND` is **a pointer to a `DXMTNativeWindow`, a small struct the host program owns**
(`include/native/dxmt_native_window.h`, added by patch 0001):

    struct DXMTNativeWindow {
      uint32_t magic, version;          /* 'DXNW', 1: checked wherever DXMT takes an HWND */
      void *metal_layer;                /* CAMetalLayer *, created and owned by the host; required */
      void *user;                       /* passed back to the callbacks */
      void     (*get_client_size)(void *user, uint32_t *width, uint32_t *height);   /* for width/height 0 requests */
      uint32_t (*get_display_id)(void *user);                                       /* CGDirectDisplayID; NULL or 0 = main */
      int      (*is_minimized)(void *user);                                         /* Present returns DXGI_STATUS_OCCLUDED */
      int      (*is_foreground)(void *user);
      void     (*request_client_size)(void *user, uint32_t width, uint32_t height); /* windowed ResizeTarget() */
    };

Every callback may be NULL and may be called from any thread that calls into DXGI or Direct3D. The struct must stay
valid, at the same address, until every swap chain created for it is released; DXMT stores the pointer and returns it from
`GetDesc` and `GetHwnd`.

Why not the earlier survey's suggestion (the `HWND` is the `CAMetalLayer*` itself). It is three files and was tempting, but the
layer cannot answer what DXMT asks of a window without DXMT guessing:

- *Client size.* DXMT needs it when the application passes 0 for a swap chain's width or height. From a bare layer it
  would be `bounds x contentsScale`, which is the host's window size only if the host and DXMT agree on the unit (points
  or pixels), and it has to be read from whatever thread calls `ResizeBuffers`. Only the host knows what size it told the
  application (our `GetClientRect`), so the host answers.
- *Minimized / foreground / which display.* From a layer these are `layer.delegate` (an `NSView`) `.window.isMiniaturized`
  and `.window.screen`: AppKit objects read off the main thread, and only when the layer happens to back a view.
- *Requests going the other way* (`ResizeTarget` in windowed mode) have nowhere to go.
- A wrong pointer cannot be told from a right one. The struct has a magic number, so a stale or foreign pointer that
  still points at readable memory is refused with `DXGI_ERROR_INVALID_CALL` instead of being sent Objective-C messages.

Why a struct that is pointed to, and not a registry of integer handles inside DXMT: a registry needs one home for its
table (DXMT's window code is a static library linked into both `dxgi.dylib` and `d3d11.dylib`, so it would have to
move into `winemetal.dylib`), a lock, and a registration call; the pointer needs none of that. The price is that an
`HWND` that points at unreadable memory crashes, as any bad pointer does. Our runtime's `HWND` is a handle-table value
(`docs/RUNTIME.md` 4.1), not this pointer, so the runtime validates the guest's `HWND` and translates it before DXMT
sees it (section 8).

### 1.2 Who creates the layer, and on which thread

The host, on the main thread, before it creates the swap chain; DXMT never creates a view or a layer. Any
`CAMetalLayer` will do: the one behind `SDL_Metal_CreateView` (`SDL_Metal_GetLayer`), or a layer in a view of the
host's own (both ran; section 7.3 says why the second is the one to use). SDL3 documents its three Metal view functions
as main-thread only. DXMT retains the layer when the swap chain is created (`_CreateMetalViewFromHWND`) and releases it
when the swap chain is destroyed (`_ReleaseMetalView`); the layer's retain count went 2, 3, 3, 2 over a run [V: ran].

What DXMT then does to the layer [V: read `src/dxmt/dxmt_presenter.cpp`, `src/winemetal/unix/winemetal_unix.c`; V: ran,
the properties are printed before and after the first `Present`]:

- reads its properties once on the thread that creates the swap chain (no dispatch);
- sets `device`, `opaque = YES`, `framebufferOnly = NO` (it was YES), `displaySyncEnabled = NO` (it was YES),
  `pixelFormat` (BGRA8Unorm for both `R8G8B8A8_UNORM` and `B8G8R8A8_UNORM` swap chains), `drawableSize` = the swap
  chain's size, and writes back the `contentsScale` it found (1.00 stayed 1.00, 2.00 stayed 2.00); sets `colorspace`
  (`kCGColorSpaceSRGB`; none before) and `wantsExtendedDynamicRangeContent` (stayed 0);
- calls `nextDrawable` once per `Present`, on its encode thread.

DXMT therefore decides the drawable's size in pixels and never the layer's size on screen; Core Animation scales the
drawable to the layer's bounds (a 320x180 swap chain in an 800x450 window presented correctly [V: ran, checked]). The
host decides the unit: whatever numbers it gives the application for the client area are the numbers the application
gives DXMT for the swap chain. Both units ran: a 640x360-point window with a 640x360 swap chain (`contentsScale` 1), and
with `SDL_WINDOW_HIGH_PIXEL_DENSITY` a 1280x720 swap chain (`contentsScale` 2) [V: ran, checked].

### 1.3 What DXMT needs from the "window", and where each answer now comes from

| Need | Used for | Answer |
|---|---|---|
| the `CAMetalLayer` | presenting | `DXMTNativeWindow::metal_layer` |
| client size | swap chain or `ResizeBuffers` with width or height 0 (`dxgi_factory.cpp:121`, `d3d11_swapchain.cpp:160,485`) | `get_client_size`; NULL gives 0x0 (treated like a minimized window) |
| is it a window | `CreateSwapChain` (new check), `ResizeTarget`, `GetContainingOutput`, fullscreen entry | magic, version and a non-null layer |
| the display it is on | refresh rate for `SyncInterval` pacing, read once in the constructor (`d3d11_swapchain.cpp:171-177`); `GetContainingOutput` | `get_display_id`, else the main display |
| minimized | every `Present`: skip the present and return `DXGI_STATUS_OCCLUDED` (`:745-749`) | `is_minimized`, else never |
| foreground | only with the `dxgi.handleAltTab` option in fullscreen | `is_foreground`, else always |
| resize the window | `ResizeTarget` in windowed mode (`:569-572`) | `request_client_size`, else ignored |
| scale factor | nothing: the presenter keeps the layer's `contentsScale` | the layer |
| monitors: list, name, desktop rectangle, mode list, current mode | `IDXGIAdapter::EnumOutputs`, `IDXGIOutput::GetDesc`, `GetDisplayModeList`, `FindClosestMatchingMode`, the refresh rate above | CoreGraphics, inside DXMT (patch 0002) |
| fullscreen, mode switch | `SetFullscreenState`, a swap chain created with `Windowed = FALSE` | not available: the four functions still return false (section 9) |

Monitors are answered by DXMT itself rather than by the host because they are not tied to a window, the CoreGraphics
display functions can be called from any thread (SDL3's display functions are main-thread only), and the host then
needs no code at all for an engine that only enumerates outputs. An `HMONITOR` is a `CGDirectDisplayID`. Output 0 is
the main display. **Sizes are in points** (`CGDisplayBounds`, `CGDisplayModeGetWidth`): the same unit as a window
created without high-pixel-density, which is also what Wine reports to DXMT by default [N: from memory of Wine's Mac
driver, whose source is not here]. A host that wants swap chains in physical pixels on a Retina display has to scale
these answers too; that is deferred.

### 1.4 Main-thread dispatches that remain

DXMT's `execute_on_main` (`winemetal_unix.c:20-26`) runs a block directly when called on the main thread and otherwise
does `dispatch_sync(dispatch_get_main_queue(), ...)`. It is unchanged. Two functions use it, both layer setters:

| Call | When | On whose thread |
|---|---|---|
| `_MetalLayer_setProps` | first `Present` of a swap chain; every `ResizeBuffers` that keeps format and sample count (`Presenter::changeLayerProperties`); the next `Present` after a change of format, colour space, sample count, gamma ramp or HDR metadata | the thread calling `Present` / `ResizeBuffers` |
| `_MetalLayer_setColorSpace` | the same `Present` calls as above (`Presenter::synchronizeLayerProperties`) | the thread calling `Present` |

So: nothing per frame in steady state; two on the first `Present`; one per `ResizeBuffers`. Measured in section 7.2. Not
dispatched, but Objective-C messages to AppKit objects from the calling thread on every `Present`:
`_WMTQueryDisplaySettingForLayer` reads `layer.delegate` and, when that is an `NSView`, `view.window.screen` and
`screen.deviceDescription` (`winemetal_unix.c:2482-2511`, called from `dxmt_presenter.cpp:108`); `_MetalLayer_getEDRValue`
does the same on swap chain creation and `ResizeBuffers`. These are reads; they are the source of the autoreleased
objects in section 7.1.

## 2. The patches

On branch `tf2` of the clone, saved with `git format-patch` into `patches/dxmt/`, applied by `tools/dxmt/build.sh`
(`git am`, skipped when already applied). A clone made at `e94c312` with the three patches applied has the same tree
(`6e41d40c164592f167c8f84c1f99542033335b0a`) as the branch that was built and tested [V: ran]. Nothing was pushed; the
branch has no upstream.

| Patch | Files | Size | What and why |
|---|---|---|---|
| `0001-native-an-HWND-is-a-host-owned-DXMTNativeWindow-carr.patch` | `include/native/dxmt_native_window.h` (new, 87 lines), `src/util/wsi_window_headless.cpp` (+49/-6), `src/winemetal/unix/winemetal_unix.c` (+24), `src/d3d11/d3d11_swapchain.cpp` (+5) | +165/-6 | Defines the window handle. `_CreateMetalViewFromHWND` (native build only, `#ifdef DXMT_NATIVE`) retains the handle's layer and returns it as both "view" and layer; `_ReleaseMetalView` releases it. The window functions answer from the handle instead of constants (1024x1024, "always a window"). `CreateSwapChain` returns `DXGI_ERROR_INVALID_CALL` when `wsi::isWindow` says no. Without this the constructor finds no Wine Mac driver and calls `abort()`. |
| `0002-native-enumerate-monitors-and-display-modes-through-.patch` | `src/util/wsi_monitor_headless.cpp` (+162/-12), `src/util/meson.build` (+6) | +168/-12 | Monitors are the online CoreGraphics displays that do not mirror another; name `\\.\DISPLAYn`, bounds, current mode, and a mode list (the current mode plus every mode usable for the desktop, including HiDPI duplicates, de-duplicated by size and rate; a rate of 0 is reported as 60 Hz). util links CoreGraphics and CoreFoundation. Without it `EnumOutputs` finds nothing, there is no mode list, and the swap chain's refresh rate stays unknown, which makes the `SyncInterval` pacing value about 0. |
| `0003-native-no-warning-per-Present-for-the-missing-frame-.patch` | `src/util/util_win32_compat.h` (+4/-1) | +4/-1 | The `ReleaseSemaphore` stub warns only for a non-null handle. The swap chain's frame latency semaphore is null in a native build and is "released" on every `Present`: one warning per frame on stderr and in the log file (267 in a 267-frame run before the patch, 0 after). Not needed for correctness. |

Two details of 0002 worth knowing. It includes `<CoreGraphics/CGDirectDisplay.h>` and `CGDisplayConfiguration.h`, not
`<CoreGraphics/CoreGraphics.h>`, because the umbrella header reaches an IOKit header that uses `interface` as a field
name and DXMT's `windows_base.h` defines `interface` as a macro. And the list was first built from
`CGGetActiveDisplayList`; with the display asleep that returned 0 displays and `EnumOutputs(0)` failed, while
`CGGetOnlineDisplayList` returned the panel [V: ran]. An engine must not lose its outputs because the screen went dark.

What the patches leave as it was: the constructor still aborts if the handle's `metal_layer` is an Objective-C object
that is not a `CAMetalLayer` (the handle check only tests for non-null); the four fullscreen functions; every other
Win32 stub; `getExePath()` returning `""` (log file name, cache directory: `DXMT_LOG_PATH` and `DXMT_SHADER_CACHE_PATH`
are set by the scripts instead).

## 3. The test

`sh tools/dxmt/window.sh` runs `build.sh`, compiles three programs into `third_party/dxmt/build-native/tf2-window/`, and
runs two of them; exit 0 only if both pass. About 14 s in all, of which the window is on screen about 5 s.

- `tools/dxmt/window_probe.cpp` (no window): in five forked children, `D3D11CreateDeviceAndSwapChain` with a null `HWND`,
  64 zero bytes, a handle with a wrong magic number, one with an unknown version, one without a layer. Each must return
  `DXGI_ERROR_INVALID_CALL` and no child may die of a signal. Unpatched DXMT aborted for every non-null `HWND`. [V: ran,
  checked: five refusals, no signal]
- `tools/dxmt/window_test.cpp` with `window_test_layer.mm` / `.h` (the Objective-C half, separate because DXMT's
  `windows.h` and Objective-C headers both define `BOOL`): the phases below. The window is 640x360, created hidden,
  given its layer, moved to (60, 90) and shown without being activated (the process is an accessory app with
  `SDL_HINT_MAC_BACKGROUND_APP` and `SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN=0`: no Dock icon, no focus change). A watchdog
  thread ends the process after 30 s; `window.sh` kills it after 75 s.

A frame "checked in full" means: draw (a clear colour and a triangle colour that both step every frame, the triangle of
the earlier study); `CopyResource` the back buffer to a staging texture, `Map` it, and require every pixel to be one of the
two colours within 1, the triangle's pixel count within 2% of one eighth of the target, and twelve sample points to have
the right colour (four of them tell an upright triangle from a flipped one); `Present`; wait for the GPU (a copy and a
read mapping of a small staging texture); then take the drawable that DXMT obtained from our layer for this `Present`
and require it to have the swap chain's size and to pass the same pixel checks. The drawable is reached without any
screen capture: the layer is made an instance of a subclass that overrides `-nextDrawable` (an `object_setClass` on SDL's
plain `CAMetalLayer`, or our own layer class with `--own-layer`), which records each drawable's texture; DXMT sets
`framebufferOnly = NO`, so the texture can be copied to a buffer with a blit on a command queue of our own.

| Phase | What | Checked |
|---|---|---|
| outputs | `EnumOutputs`, `GetDesc`, `GetDisplayModeList(R8G8B8A8_UNORM)`, `FindClosestMatchingMode`, before a window exists | at least one output; name; a monitor handle; modes non-empty, sorted, each with non-zero size and rate; one has the desktop's size; output 0's rectangle and rate equal SDL's primary display |
| handles | `IDXGIFactory::CreateSwapChain` with null, wrong magic, no layer, unknown version | `DXGI_ERROR_INVALID_CALL` each; no drawable taken |
| swap chain | `CreateSwapChain` (the factory obtained as device -> `IDXGIDevice::GetAdapter` -> `GetParent`), `GetDesc`, `GetFullscreenState`, `GetContainingOutput`, `GetBuffer(0)` | S_OK; the description; the output's monitor is the display the window is on; DXGI's and SDL's refresh rate agree; the buffer's size, format and bind flags |
| V | 36 frames, `Present(1, 0)` | each frame in full; the layer's properties after the first `Present`; 36 present requests of the expected kind |
| A, A2, B | 72, 24, 72 frames with `Present(1)`, `Present(2)`, `Present(0)` and nothing else in the loop | one drawable and one present request per `Present`; the last drawable's pixels; the requests (below); and, when Core Animation reports the frames as shown, their times |
| L | 36 frames, `Present(1)`, `IDXGIDevice1::SetMaximumFrameLatency(1)` | as A |
| C | `Present` while the host says "minimized"; `SetFullscreenState(TRUE)`; `ResizeTarget(1024x576)` | `DXGI_STATUS_OCCLUDED` and no drawable taken; a failure code and `GetFullscreenState` still FALSE; the host callback got 1024x576 once; a full frame afterwards |
| D | `SDL_SetWindowSize(800, 450)`, release the buffer's views, `ResizeBuffers(0, 0, 0, UNKNOWN, 0)` | the host was asked for the size; the back buffer is 800x450; 24 frames in full |
| E | `ResizeBuffers(0, 320, 180, ...)` in the 800x450 window | 8 frames in full, drawable 320x180 |
| F | a second thread calls `ResizeBuffers` and presents 24 frames while the main thread sleeps 300 ms and then pumps SDL events | the thread finishes; 24 frames in full; the timing is printed |
| G | release the swap chain; create one with `B8G8R8A8_UNORM`, 1 buffer, size 0x0; 6 frames; release | `Release()` returns 0 both times; `GetLastPresentCount` equals the presents made; the 0x0 chain has the window's size; frames in full |
| teardown | release everything, destroy the view and the window | the device's `Release()` returns 0; the `CAMetalLayer` is deallocated (a weak reference becomes nil) |

"The requests": the command buffer class's `presentDrawable:`, `presentDrawable:afterMinimumDuration:` and
`presentDrawable:atTime:` are hooked (method swizzling, in the test process only) and each call is recorded. For
`SyncInterval` 1 and 2 every request must be `afterMinimumDuration` with `SyncInterval / refresh rate` within 1%; for 0
a plain `presentDrawable:`. This is checked in every run. The times at which frames were shown come from each drawable's
presented handler (`presentedTime`); they exist only when the display is awake, the screen unlocked and the window
visible. Then the test
requires the mean interval to be between 0.95 and 2.2 times the requested one and no interval under half of it, and
prints `ON-SCREEN PACING CHECKED`. Otherwise it prints `ON-SCREEN PACING NOT CHECKED` with the reason and still exits 0
if everything else held. That choice is deliberate: the acceptance commands have to work on an unattended Mac whose
display has gone to sleep, and what cannot be observed there should be reported as not observed, not as a failure of
the patch. The bounds are loose on the slow side so that a busy machine does not fail the run.

Options, for runs by hand (`window.sh` passes them on): `--hidden` never shows the window (everything runs; no frame is
shown, as with a sleeping display); `--own-layer` uses a view and layer of the program's own instead of
`SDL_Metal_CreateView`; `--hidpi` creates the window with `SDL_WINDOW_HIGH_PIXEL_DENSITY` and a swap chain in pixels;
`--no-pool` runs without autorelease pools (section 7.1); `--stale-size` is the experiment of section 7.3;
`--max-drawables 2`; `--no-probe`; `--frames N`; `--foreground` and `--hold S` for a human (section 10).

## 4. Results

[V: ran, checked] unless marked. Default configuration, window visible, display awake: **1255 checks, 0 failed**. The same
with `--own-layer` (1255) and `--hidpi` (1255); `--hidden` 1245 (the ten on-screen checks are not made), six hidden runs
in a row; `--no-probe` 714; `--max-drawables 2 --hold 1` 1516. `window.sh` passed each time it was run with the final
patches.

- Outputs: `output 0: "\\.\DISPLAY1" desktop (0,0)-(1512,982) attached=1 monitor=0x1; 132 modes for R8G8B8A8_UNORM, from
  960x600 to 3024x1964; current 1512x982 @ 120.000 Hz`; SDL: "Built-in Retina Display", bounds (0,0) 1512x982, 120 Hz,
  pixel density 2.00. With the external 4K monitor attached the evening before, a stand-alone CoreGraphics program listed
  two displays, 226 and 132 raw modes [V: ran, not through DXMT]; DXGI with two outputs was not run.
- Swap chain creation: S_OK in 3.9 to 5.0 ms. `GetBuffer(1)` returns `DXGI_ERROR_UNSUPPORTED` (DXMT serves buffer 0
  only, as the earlier survey read; printed, not a failure).
- The first swap chain made 297 presents in one run (V 36, A 72, A2 24, B 72, L 36, C 1, D 24, E 8, F 24); the layer handed
  out 297 drawables and Metal received 297 present requests; `GetLastPresentCount` said 297.
- Resize: after `SDL_SetWindowSize` the layer's bounds were 800x450; `ResizeBuffers(0, 0)` took 0.09 to 0.12 ms on the
  main thread, asked the host once, and the back buffer and the next drawables were 800x450.
- Release: both swap chains' `Release()` returned 0; then vertex shader, pixel shader, input layout, vertex buffer, two
  textures, context and device 0; the factory and the adapter returned 1 each (each still referenced by the other object
  that was about to be released [N]). The layer was deallocated as soon as the view and window were destroyed.

## 5. What the log shows

Default log level, stderr of one `window_test` run (`DXMT_LOG_PATH=none`, so no log file) [V: ran]:

| Lines | Count | When |
|---|---|---|
| `warn: D3DKMTOpenAdapterFromLuid not implemented.`, `warn: Failed to open D3DKMT adapter`, `warn: GetModuleHandle not implemented.`, `info: Maximum supported feature level ...`, `info: Using feature level ...` | 1 each | device creation (as in the build study) |
| `warn: GetCurrentThread not implemented.` + `warn: SetThreadPriority not implemented.` | 4 each | start of DXMT's encode, finish and first two worker threads |
| `warn: GetWindowThreadProcessId not implemented.`, `warn: GetCurrentProcess not implemented.`, `warn: GetProcessId not implemented.` | 1 each per swap chain | `CreateSwapChain`'s same-process check; both sides read 0, so it passes |
| `warn: CreateSemaphore not implemented.` | 1 per swap chain | the constructor's frame latency semaphore (null from then on) |
| `warn: CloseHandle not implemented.` | 1 per swap chain | the destructor |
| nothing | per `Present` | (before patch 0003: `warn: ReleaseSemaphore not implemented.` once per `Present`) |
| `err: CreateSwapChain: invalid window handle` | 1 per refused handle | patch 0001's check (a null `HWND` is refused earlier, silently) |
| `err: DXGI: EnterFullscreenMode: Failed to change display mode` | 1 | the test's `SetFullscreenState(TRUE)` |
| `err: Non zero-index buffer is not supported` | 1 | the test's `GetBuffer(1)` |

At `DXMT_LOG_LEVEL=trace` the only per-frame lines are `trace: staging map block` / `staging map ready` from the test's own
`Map` calls; the present pipeline shows as `Start compiling 1 PSO` / `Compiled 1 PSO` twice (once per swap chain format).
Nothing aborts, nothing is logged about a missing Apple interface. Consequence of the null semaphore:
`IDXGISwapChain2::GetFrameLatencyWaitableObject` returns null [V: read]; only an application that creates its swap chain
with `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT` would notice.

## 6. Measured times

One 640x360 window on the built-in 120 Hz panel, awake, the test window visible and not covered; trivial scene (one
clear, one triangle). Each line is one phase of one run; three visible runs agreed to the digits shown unless a range
is given. "Shown" is Core Animation's `presentedTime` for the drawable. [V: ran]

| Loop | Frames | `Present` loop | Shown every (mean / min / max / sd, ms) | `Present()` call (mean / max, ms) | `Present` to shown (mean, ms) |
|---|---|---|---|---|---|
| A: `Present(1, 0)`, nothing else | 72 | 119.9 to 120.3 fps | 8.33 / 8.33 / 8.33-8.34 / 0.00, 72 of 72 shown | 7.6-7.7 / 10.3-13.3 | 62.2 to 62.4 |
| A2: `Present(2, 0)` | 24 | 65.7 to 66.6 fps (first sixth excluded) | 16.67 / 16.67 / 16.67 / 0.00, 24 of 24 | 12.8 / 17-19 | 101 to 102 |
| B: `Present(0, 0)` | 72 | 118.0 to 120.1 fps | 8.33-8.47 / 8.33 / 8.33-16.67, 70 of 72 (2 never shown) | 7.6-7.8 / 15.6-17.6 | 47.8 to 49.4 |
| L: `Present(1, 0)`, maximum frame latency 1 | 36 | 119.5 to 120.8 fps | 8.33 / 8.33 / 8.33 / 0.00, 36 of 36 | 7.5-7.8 / 11.5-22.0 | 44.9 to 46.5 |
| V: `Present(1, 0)` with the test's own readbacks in the loop | 36 | 43 to 50 fps | 20-23 / 16.67 / 33.33 | 0.08-0.09 / 1.1-2.0 | 20.2 to 21.2 |
| with `--max-drawables 2` (one run): A | 72 | 119.5 fps | 8.33 / 8.33 / 8.33 / 0.00 | 7.8 / 12.4 | 53.1 |
| with `--max-drawables 2`: L | 36 | 122.6 fps | 8.33 / 8.33 / 8.33 / 0.00 | 8.1 / 18.7 | 36.2 |
| with `--max-drawables 2`: B | 72 | **234.2 fps** | 4.43 / 2.31 / 10.64 / 2.05, 72 of 72 | 4.5 / 19.6 | 21.5 |

Reading:

- **Sync interval 1 and 2 are paced exactly by the display.** DXMT's request to Metal was
  `presentDrawable:afterMinimumDuration:` with 8.333 ms and 16.667 ms on every frame (never anything else), made on the
  encode thread; the frames were then shown exactly one and two refresh periods apart.
- **What blocks the application.** `Present()` itself costs under 0.1 ms when the loop is slower than the display (V).
  In a loop that could run faster, `Present()` blocks about 7.7 ms per call: the encode thread waits in `nextDrawable`
  for one of the layer's three drawables (mean wait 7.6 to 7.8 ms), and the caller waits for the frame three back
  (DXMT's device default `MaximumFrameLatency` is 3, as on Windows).
- **Latency is the price.** A free-running `Present(1)` loop has 62 ms between calling `Present` and the frame being
  shown, 7.5 refresh periods; with frame latency 1, 46 ms; with two drawables instead of three about one period less
  (53 and 36 ms). The host owns the layer, so `maximumDrawableCount = 2` is the host's to set. A game that sleeps between
  frames instead of running flat out would sit near V's 20 ms. Not tuned further here.
- **Sync interval 0 is not uncapped by default.** With three drawables the loop still ran at the display's 120 fps
  (two of 72 frames were replaced before being shown). With two drawables it ran at 234 fps and frames were reported
  shown 2.3 to 10.6 ms apart, i.e. not locked to the refresh. Whether that tears on the glass is for a human [?].
- Not shown at all (hidden window; a locked screen, which is what the last run on the committed tree met; and the
  first run, made while the display slept): every drawable's presented handler ran with `presentedTime` 0. Hidden, the loops still ran at about 120 fps, also for `Present(2)` (113.7 fps):
  the minimum duration has nothing to hold on to when nothing is shown. With the display asleep an earlier version of
  the test, with readbacks in every loop, ran 24-frame phases at 54 to 70 fps without error; the final test was not run
  against a sleeping display.
- Other calls: `CreateSwapChain` 3.9 to 5.0 ms; `ResizeBuffers` 0.09 to 0.12 ms on the main thread; the very first
  `Present` after the present pipeline was new to Metal's cache took 80 ms, later first presents 1.1 to 3.6 ms.

## 7. Things the runtime has to know

### 7.1 The presenting thread needs an autorelease pool, drained every frame

Run with `--no-pool` (no autorelease pool on the threads that call DXMT) the test passes its frame checks, but at
teardown the layer is **not** deallocated, and the Objective-C runtime's own listing of the main thread's pending
autoreleases has 1749 entries after 282 presents on that thread: per `Present` two `NSConcreteValue`, one `NSDictionary`
(the screen's `deviceDescription`), and the `NSScreen` autoreleased twice, the window (`NSKVONotifying_SDL3Window`) three
times and the `SDL3_cocoametalview` twice (the listing merges repeats of one object into one entry with a count), so
ten autoreleases per frame; the layer itself is in the list once [V: ran]. They come from the per-`Present` reads of
section 1.4; on a thread with no pool nothing releases them until the thread ends. With a pool pushed and popped around
each frame (`objc_autoreleasePoolPush` / `Pop`, callable from C) the layer's retain count returns to its starting value
and the layer is freed with its view [V: ran, checked]. This is not specific to the swap chain: device creation left
two `MTLCommandQueueDescriptor` objects pending the same way.

### 7.2 The main thread must keep servicing its dispatch queue

Phase F: a second thread called `ResizeBuffers` while the main thread slept 300 ms without running anything; the call
returned after 301.1 to 301.4 ms, 0.2 to 0.6 ms after the main thread began calling `SDL_PumpEvents` [V: ran, three
runs]. So (a) the dispatch of section 1.4 is real, (b) SDL3's Cocoa event pump services the main dispatch queue (the
earlier survey's open question), and (c) a render thread stalls in `ResizeBuffers`, and in the first `Present`, for as long as
the main thread does not pump. If the main thread is at that moment waiting for the render thread, both wait for ever.
After that, the same thread presented 24 frames checked in full with no further dependence on the main thread.

### 7.3 Do not let SDL own the layer

`SDL_Metal_CreateView`'s view rewrites its layer's `drawableSize` (and `contentsScale`) whenever SDL sees the window's
pixel size change. DXMT also writes `drawableSize`, and assumes the drawables it gets have the size it set. Experiment
(`--hidden --stale-size`): swap chain 320x180, then `SDL_SetWindowSize(900, 500)` and one `Present` with **no**
`ResizeBuffers` [V: ran]:

    SDL's layer:   layer drawableSize 900x500; the drawable DXMT was given is 900x500;
                   this frame's colours: 57600 of 57600 pixels inside the top-left 320x180, 0 of 392400 in the rest
    our own layer: layer drawableSize 320x180; the drawable DXMT was given is 320x180 (Core Animation scales it)

With SDL's layer the picture shrinks into a corner of a drawable whose other pixels were never written (DXMT's present
pass does not clear) until the application calls `ResizeBuffers`. An engine normally does that on `WM_SIZE`, but frames
can be presented in between, and a swap chain deliberately smaller than the window (fullscreen at a lower resolution,
later) would break on the first size event. A layer-backed `NSView` of our own inside the SDL window (the test's
`--own-layer`: 60 lines of Objective-C, `hitTest:` returning nil so that events still reach SDL's view) has no second
writer and passed every phase, including this one.

### 7.4 Smaller points

- **A sleeping display, a locked screen.** With the display asleep `CGGetActiveDisplayList` is empty (hence patch
  0002's use of the online list), no frame is reported shown, and presenting continued without error (earlier version
  of the test). With the display awake and the screen locked the window is not visible, no frame is reported shown,
  and the final test passes everything else (1245 checks) [V: ran].
- **The Metal HUD** (`MTL_HUD_ENABLED=1`) draws into the layer's drawables: with it the test's drawable comparison fails
  from the first frame that carries the overlay (12,028 foreign pixels in a 640x360 drawable) and nothing crashes
  [V: ran, hidden]. `window.sh` unsets the variable. DXMT's own label in that HUD goes through a class
  (`MTLHUDService`) that the earlier survey did not find on macOS 27; what the HUD shows was not looked at.
- **Formats.** `R8G8B8A8_UNORM` and `B8G8R8A8_UNORM` swap chains both present through a BGRA8Unorm layer with exact
  pixel values (tolerance 1) in sRGB; other formats, sRGB swap chain formats, MSAA swap chains and HDR were not run.

### 7.5 Which thread does what

Thread names as the test listed them while a swap chain existed (DXMT cuts names to 15 characters) [V: ran for the
names, for who calls `nextDrawable` and for who makes the present request; V: read for the rest]:

| Thread | Count | What it does for a swap chain |
|---|---|---|
| the thread that calls `Present` (the application's; in the test the main thread, in phase F `window_test-render`) | 1 | reads the layer's view, window and screen (7.1); on the first `Present` and after a property change builds the present pipeline and sets the layer's properties through the main queue (7.2); appends the present to DXMT's command chunk and commits it; then waits in `PresentBoundary` until the frame `MaximumFrameLatency` (3) frames back has finished on the GPU. `ResizeBuffers` and `CreateSwapChain` run here too. |
| `dxmt-encode-thr` (`dxmt-encode-thread`) | 1 per device | turns command chunks into Metal command buffers. For a present: **calls `-[CAMetalLayer nextDrawable]`** (every one of the test's drawables was requested on this thread, never on the main thread), encodes the pass that copies the back buffer into the drawable, **calls `presentDrawable:` / `presentDrawable:afterMinimumDuration:`** (every request was made on this thread), updates the HUD statistics, commits. It is where the frame rate is throttled: it waits for a free drawable. |
| `dxmt-finish-thr` (`dxmt-finish-thread`) | 1 per device | waits for each command buffer to complete and signals what waiting callers sleep on: a `Map` of a staging resource, and `Present`'s latency wait. |
| unnamed | 7 to 12 | DXMT's pipeline and shader compile workers (2 at start, up to twice the core count, not named: `src/dxmt/dxmt_tasks.hpp:48-56`) and system threads (dispatch workers that run Metal's completion and presented handlers, Core Animation). Not told apart [N]. |
| `com.apple.NSEventThread` | 1 | AppKit's |
| the main thread | 1 | owns the window, the view and the layer; pumps SDL events, which runs DXMT's two layer setters when another thread asks (7.2) |

The event-listener thread the earlier survey listed ends at once in a native build and was not in the list. All three DXMT
thread kinds log `GetCurrentThread` / `SetThreadPriority` "not implemented" once at start: they run at default priority.

## 8. What our runtime must provide

- **The window handle type.** The guest's `HWND` stays the runtime's handle-table value (`docs/RUNTIME.md` 4.1). The
  runtime's window object embeds a `DXMTNativeWindow`; its `user` points back at the window object. The COM proxy
  translates at exactly the places the earlier survey listed: guest `HWND` -> `&window->dxmt` in `DXGI_SWAP_CHAIN_DESC::OutputWindow`
  (`IDXGIFactory::CreateSwapChain`), `IDXGIFactory2::CreateSwapChainForHwnd`, `MakeWindowAssociation`; and back in
  `IDXGISwapChain::GetDesc`, `IDXGISwapChain1::GetHwnd`, `GetWindowAssociation`. An `HWND` the handle table does not know
  never reaches DXMT. The window object must outlive its swap chains (the guest can destroy a window first: keep the
  struct until the last swap chain is gone). `HMONITOR` in `DXGI_OUTPUT_DESC` is a `CGDirectDisplayID`; if the guest's
  `MonitorFromWindow` / `GetMonitorInfo` shims use the same value, nothing needs translating.
- **The layer.** Created on the main thread when the guest creates its window (or lazily, on the main thread, before the
  swap chain): a layer-backed `NSView` of the runtime's own in the SDL window's content view (7.3), not
  `SDL_Metal_CreateView`. `contentsScale` 1 and sizes in points to start with.
- **The callbacks.** `get_client_size` returns what `GetClientRect` tells the guest; `get_display_id` the
  `CGDirectDisplayID` of the window's screen, cached on the main thread when SDL reports a display change (the
  callbacks run on whatever thread calls DXGI, where AppKit must not be asked); `is_minimized` from SDL's minimized and
  restored events; `request_client_size` posts a resize to the main thread, which then delivers `WM_SIZE` as for any
  resize.
- **Who pumps events.** The host's main thread, which is the first guest thread (RUNTIME.md 6.1), pumps SDL events
  inside the guest's `PeekMessage` / `GetMessage` shims; that also services DXMT's dispatches (7.2). The danger is a
  guest main thread that blocks in `WaitForSingleObject`, `SendMessage` or a lock held by the render thread while the
  render thread is inside `ResizeBuffers` or its first `Present`. Which thread the game presents on is not known yet
  (it needs the renderer's call trace). If it is not the main thread, one of these is needed: the runtime's wait
  primitives service the main dispatch queue while the main thread waits; or the swap chain proxy runs `Present` and
  `ResizeBuffers` so that the dispatch cannot coincide with such a wait; or a fourth patch makes the two layer setters
  run on the calling thread in native builds (not tried; the layer is ours, and with a view of our own nothing else
  writes those properties).
- **Autorelease pools.** Every proxy thunk that calls into DXMT (RUNTIME.md 3.7) should run inside
  `objc_autoreleasePoolPush` / `Pop`; at the very least `Present`, `ResizeBuffers`, swap chain and device creation (7.1).
- **The calling convention bridge (RUNTIME.md 3.7).** Nothing new beyond the earlier survey's list: the swap chain, output and
  factory interfaces used here take integers, pointers and plain structs; the two `WCHAR` structs are
  `DXGI_OUTPUT_DESC` / `DXGI_OUTPUT_DESC1` (`DeviceName` is written here as native 4-byte `wchar_t`, by `swprintf`,
  unlike the adapter description which DXMT writes as 2-byte units: the thunk for outputs must narrow 4-byte units to
  2-byte ones). `Present` returns `DXGI_STATUS_OCCLUDED` (a success code that is not 0) while minimized: the thunk
  passes `HRESULT`s through unchanged.
- **Environment.** `DXMT_SHADER_CACHE_PATH` and `DXMT_LOG_PATH` must be set by the runtime (the executable name is
  still empty in native mode); not `MTL_HUD_ENABLED` unless wanted.

## 9. Deferred

- **Fullscreen.** `setWindowMode`, `enterFullscreenMode`, `leaveFullscreenMode`, `restoreDisplayMode` return false, so
  `SetFullscreenState(TRUE)` logs one error and returns `DXGI_ERROR_NOT_CURRENTLY_AVAILABLE` with the swap chain still
  windowed [V: ran, checked], and a later `Present` works. A swap chain *created* with `Windowed = FALSE` would believe
  it is fullscreen (the constructor ignores the failure and keeps the flag; `SetFullscreenState(FALSE)` would then
  return an error once) [V: read]: the proxy should force `Windowed = TRUE` at creation until fullscreen exists. What
  fullscreen should become here is a borderless window covering the display with the swap chain scaled by Core
  Animation: two more callbacks in `DXMTNativeWindow` (enter with a size and rate, leave) and the two wsi functions
  calling them.
- **Mode switches.** Modes are reported, never set; there is nothing to restore.
- **Physical pixels.** Monitor rectangles and modes are in points. A host that gives the guest pixel sizes (the test's
  `--hidpi`, which works for the swap chain) would also need the monitor answers in pixels: `CGDisplayModeGetPixelWidth`
  and a scaled `CGDisplayBounds`, behind a switch.
- **HDR and wide colour.** Untouched and unexercised: `IDXGIOutput6::GetDesc1` reports 10 bits and a PQ colour space
  when the screen's potential EDR headroom is above 1 (it is, on this panel) [V: read], `SetColorSpace1` and
  `R16G16B16A16_FLOAT` swap chains go through the same layer setters. The game is an SDR title.
- **Several outputs.** Enumerated (main first) but only one was connected; `get_display_id` picks the output for the
  refresh rate once, at swap chain creation, so a window dragged to a display with another rate keeps the old pacing
  value until the swap chain is re-created [V: read]. Upstream behaves the same.
- **The HUD**, **the per-swap-chain stub warnings**, **`getExePath()`**: see 7.4, 5, and the build study.
- **A robust handle.** An `HWND` that points at unmapped memory crashes; the proxy must never pass one.

## 10. What was verified by the program, and what needs a human

Verified by `window_test` without a human, in every run: all of section 3's table. Verified in runs where the display was
awake, the screen unlocked and the window visible (five such runs with all three patches, two more before the third): the times Core Animation reports for frames being shown. The run of the acceptance commands
on the committed tree afterwards found the screen locked, so that run checked
everything except those times and said so in its last line. That is as far as a program can go without capturing the screen, which needs a permission prompt and was not
done: the drawable that Core Animation is handed has the right pixels, and Core Animation says it showed it at the
right times.

Needs a human to look at the screen, once:

    sh tools/dxmt/window.sh --foreground --hold 5

The window comes to the front and keeps presenting for five more seconds. Expected: a 640x360 window that becomes
800x450 after about two seconds, a solid background whose colour changes smoothly every frame, a triangle in the middle,
apex up, whose colour also changes, covering the middle half of the width and height; for a moment (phase E) the same
picture at low resolution filling the window; no flicker to black, no picture confined to a corner, no garbage. (The
`--foreground` path differs from the tested one only in not setting two SDL hints and the accessory activation policy;
it was not run, so as not to take the keyboard focus away from work in progress.) Also for a human: whether
`Present(0)` with two drawables tears; what the Metal HUD shows.

## 11. What I could not determine

- **Anything on a 60 Hz or external display**, or with two displays: the built-in 120 Hz panel was the only display
  connected. The request DXMT makes scales with the refresh rate by construction, and the test computes its
  expectations from the rate DXGI reports, but only 120 Hz was observed.
- **The final test against a sleeping display.** The first complete run happened while the display slept ; it led to patch 0002's fix and to the split between "requests" and "shown" in the test. The display has been
  awake since, and I did not put it to sleep. The hidden-window runs cover the same "nothing is shown" path in the
  test, not necessarily the same path in Core Animation.
- **Which thread the game presents on**, and therefore how serious 7.2 is.
- **Whether `nextDrawable` can return nil here** (it can after a one-second timeout when no drawable is free) and what
  DXMT does then; it never happened in about 5,000 presents.
- **Tearing, colour accuracy on the glass, the HUD's contents, a window covered by other windows or on another Space,
  a really minimized window** (the minimized state was only simulated through the callback).
- **Long runs.** The longest swap chain lived for 297 presents plus a one-second hold; no soak test (the test machine has had power cuts, and the window is on a live screen).
- **Why the factory and adapter report one remaining reference** at their last `Release()` in the test's order of
  release; not traced.

## 12. How to repeat, and what was written where

    sh tools/dxmt/build.sh      # applies patches/dxmt/*.patch on branch tf2 of the clone if needed, builds
    sh tools/dxmt/probe.sh      # the build study's device and off-screen triangle checks; still pass
    sh tools/dxmt/window.sh     # this study: window_probe, then window_test (opens the window)
    sh tools/dxmt/window.sh --hidden            # the same without showing a window

Files of this study: `patches/dxmt/0001..0003*.patch`, `patches/dxmt/README.md`, `tools/dxmt/window.sh`,
`window_test.cpp`, `window_test_layer.mm`, `window_test_layer.h`, `window_probe.cpp` (rewritten: it used to demonstrate
the abort), one line in `device_probe.cpp` (its text about "no outputs" was no longer true), this file.

Written outside the repository: `third_party/dxmt` (branch `tf2`, three commits, never pushed; `build-native/` with
`tf2-window/` holding the test programs, their output and DXMT's shader cache); the session scratchpad. DXMT's shader
cache and log were kept out of the user's directories (`DXMT_SHADER_CACHE_PATH`, `DXMT_LOG_PATH=none`; `window_test`
sets both itself when they are unset); the user's `dxmt` cache directory still holds only `cs2.exe`, which is not ours.
Metal's own per-user shader cache (`com.apple.metal` under the same cache root) is written by the system for any
Metal program and received entries, as in the build study.
