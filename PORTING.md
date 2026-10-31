# Titanfall 2 native Apple port: design notes

Started 28 October 2026. Base: [`rkutyna/titanfall2-mac`](https://github.com/rkutyna/titanfall2-mac), commit
`7e6a6e522c2dd825b70ccebf9424bd42210588db`. The [README](README.md) has the current status and the full install
steps; this page explains how the iOS port is put together, what each check proves, and what is still open.

## Delivery order

1. Campaign gameplay on a physical iPhone or iPad, with a DualSense.
2. Vision Pro windowed gameplay, with a DualSense.
3. Vision Pro immersive stereoscopic gameplay with tracked head movement, with a DualSense.
4. Immersive motion controls with both PSVR2 Sense controllers, using the Titanfall VR mod where feasible.

Campaign and online multiplayer are both wanted. The base port is single-player only: its Winsock implementation keeps
traffic inside the process, and its EA SDK stand-ins provide an offline local profile. Online play would need real
network transport, protocol and authentication work, and a verified compatible server. Nothing here enables it.

## What has been verified on hardware

All device results are from one iPad Pro 11-inch (M5) with iPadOS 27.0.1.

| Check | Result |
| --- | --- |
| Memory layout probe (`tools/ios/vm-probe`) | Upstream's 1 TiB Mac layout fails on iOS. With Extended Virtual Addressing, a 4 GiB image arena at 448 GiB and a 16 GiB allocation arena at 456 GiB are reserved exactly with fixed `vm_map` calls. Without the entitlement the same reservation fails (Mach error 1). |
| Runtime check (`tools/ios/runtime-smoke`) | The native runtime initializes 36 native modules and 1,107 dispatch entries; allocation, zero fill, read/write, read-only protection and query, decommit, zero-filled recommit and release all pass. |
| Engine check (`tools/ios/engine-smoke`) | Two recompiled engine modules (`tier0.dll`, `vstdlib.dll`) load: 38 modules, 5,059 dispatch entries, all five export checks pass. |
| Renderer check (`tools/ios/renderer-check`) | DXMT on iOS converts DXBC shaders, draws a solid and an interpolated triangle and reads them back: 8,192 coloured and 57,344 clear pixels with none unexpected, and the expected interpolated colours. The first run exposed a Metal assertion on Managed buffer storage, fixed by using Shared storage on iOS only. |
| Full game app | 21 game modules, 164,940 functions, none skipped, 858 objects; the 282 MB app links and signs with the Extended Virtual Addressing entitlement. On the device: 57 modules and 166,151 dispatch entries, Direct3D 11 feature level 11_1 through DXMT, Miles audio through the WASAPI stand-in. Frames show the Respawn intro (present 60) and the main menu, v2.0.11.0 (present 300), about 18 s in. |
| First mission | With `TF_PILOT_TRAINING=1`, *The Pilot's Gauntlet* loads: first mission frame 64 to 74 s into the run, then about 60 presents a second at 1280×720 with roughly 200 to 300 draws a frame and a 4.6 GB footprint. Screenshots of the panel show the opening cinematic and the pod's "Look at the lights" prompt, 16:9 with the status bar hidden. |
| Sound | Originally silent on iOS: the runtime's audio sink opens a CoreAudio queue only when `RT_HOST_AUDIO=coreaudio`, which the Mac launcher set and the iOS app did not. The app now sets it, and the owner confirmed audible sound on the device. |
| Controller | A DualSense connects and becomes the current controller. The owner reported that its input does not reach the game in the latest build; that is being fixed and is not accepted. |

Not yet verified: controller gameplay, starting the mission from the game's own menus, the new resolution choices on
the device, frame pacing on the panel, sustained memory and thermal behaviour, later campaign levels, any iPhone, and
any gameplay on Vision Pro.

## Runtime changes

- **Memory layout.** The Mac layout puts the image arena at 1 TiB and a 256 GiB allocation arena above it. iOS rejects
  those addresses and relocates ordinary `mmap` hints, so `RT_VM_LAYOUT=ios` (the default when `RT_PLATFORM=ios`)
  selects a 4 GiB image arena at 448 GiB and a 16 GiB allocation arena at 456 GiB. They are reserved with a fixed,
  non-overwriting `vm_map` and need Extended Virtual Addressing. The layout is chosen at build time and never relocated
  silently at startup. The 20 GiB is address space, not physical memory: the game's allocations must still fit the
  16 GiB arena and the device's memory limit. The macOS layout and reservation method are unchanged.
- **Region queries.** `mach_vm_region` is replaced by the SDK-supported 64-bit `vm_region_64`, and the returned object
  port is released.
- **Memory ordering.** iOS builds use software ordering (`GUEST_MEM=tsob`). The runtime refuses hardware-TSO modes on
  non-macOS targets instead of linking the macOS-only call or running unordered code.
- **UIKit host** (`runtime/host_ios/window.m`, `RT_HOST_WINDOW=uikit`). The app creates the view with its
  `CAMetalLayer` before the game thread starts; the host owns the scene lifecycle and activates the audio session.
  The macOS event loop cannot serve as the iOS entry point.
- **Controllers** (`runtime/host/gamepad.m`, `runtime/core/gamepad.c`). GameController snapshots feed the game's three
  XInput 1.3 functions: buttons, both sticks, analog triggers, packet numbers, connection and disconnection, and
  neutral input while the app is inactive. The current controller becomes slot 0; on disconnect the next one is
  promoted. A DualSense or DualShock touchpad click maps to Back. Rumble is not implemented: a nonzero vibration
  request returns `ERROR_NOT_SUPPORTED`.
- **Sound.** The CoreAudio sink logs what it wrote and what the device took about every 10 seconds, and restarts its
  queue if the device takes nothing for 2 seconds (for example after an audio-session interruption).
- **Embedded driver** (`gate3_run_embedded` in `gate3/run.c`). The app starts the existing driver on its own worker
  thread. This path leaves out the Mac driver's fork, exec and event loop, forces software ordering, keeps the offline
  SDK stand-ins, and installs the menu-mark and `GATE3_CONSOLE` probes by RVA itself. `gate3_embedded_presents()`
  counts real presents. A `+map` startup argument is ignored by the game, so `TF_PILOT_TRAINING=1` hands
  `map sp_training` to the engine's command buffer 3 s after the main menu opens, as upstream's map smoke test does.
- **Build switches.** `RT_PLATFORM=ios` swaps the AppKit host for the UIKit host and links UIKit, AVFAudio and
  GameController. Builds default to macOS. `make rt-link-check` force-loads every runtime object into one link so an
  unsupported framework or missing symbol cannot hide in the archive.

## Renderer changes (`patches/dxmt-ios/`)

Apply after upstream's twelve native-host patches (`patches/dxmt/`) on DXMT
`e94c312f5c054263acf261cfa109edf13e757587`, in order:

- `0001-ios-metal-bridge.patch`: DXMT's Metal bridge (`winemetal`) on iOS: UIKit instead of AppKit, and iOS paths for
  buffer storage, display sync, colour space and display queries, with macOS code unchanged.
- `0002-ios-renderer-and-shader-target.patch`: the LLVM shader-conversion path for iOS, iOS AIR and metallib metadata, an
  app-provided single-display description (`DXMTSetNativeDisplay`), Apple's iOS cross build, and Shared instead of
  Managed buffer storage on iOS-family targets. macOS keeps its storage modes.
- `0003-ios-display-mode-list.patch`: the iPad's panel is reported as a monitor with the usual 16:9 and 16:10 sizes
  that fit it, plus the panel's own size, so the game can offer resolutions.

Output is SDR sRGB. HDR, secondary displays, desktop-wide settings and cross-process bootstrap sharing are not
implemented. The output contract is not a measurement of the panel's gamut.

DXMT's converter needs LLVM 15, built twice: host tools (`llvm-tblgen`, `llvm-config`) and iOS static libraries
(target `LLVMPasses`), with no backend targets and no tests, docs, zlib, zstd, terminfo or libxml2. The commands are in
the README. The Metal toolchain (`metal`, `metallib`) is a separate Xcode download.

## The app (`tools/ios/native-game`)

- `project.yml` (XcodeGen) defines `TFNative`, the full game, and `TFAssetSetup`, a small app with the same bundle
  identifier that only creates the app container, so game files can be copied before the game itself is built. Bundle
  identifiers come from the `TF_BUNDLE_PREFIX` build setting (default `com.example`); pass your own prefix and
  `DEVELOPMENT_TEAM` to `xcodebuild`.
- `TFNative.entitlements`: Extended Virtual Addressing and Increased Memory Limit.
- `Launcher.swift`: a card-style SwiftUI launcher. Cross starts the game on release (so the press is not carried into
  play), Options opens Settings, and polling stops while the app is inactive. Settings show the controller, the render
  size and the file status.
- `main.m`: validates every path and size in `game-files-ready.json` against `Documents/Game` before starting, keeps
  saves in `Application Support/PlayerState`, excludes only the replaceable game files from backup, puts the shader
  cache in `Caches/DXMT`, and writes `game-progress.json`, `device-info.json`, logs and selected frames to `Documents`.
  It hides the status bar and home indicator and defers edge gestures during play. The game draws at the chosen render
  size (default 1920×1080, or the panel's own size) and the layer scales it to the screen. `TF_AUTOSTART=1` skips the
  launcher for diagnostics.

Game files are staged by `tools/ios/stage-game-assets.py`: the menus, shared streaming packs, English audio and
`sp_training`, as same-volume hard links (1,209 files, 49,196,834,062 bytes with the tested version). Installer files,
multiplayer maps and later campaign levels are left out; this is not a complete campaign, and later levels have not
been tried. `tools/ios/verify-device-assets.py` checks a `devicectl` inventory against the manifest by path and size,
not by hash. Send the ready marker only after the copy is complete and verified.

## Build and verify the runtime

No game files are needed for these checks:

```sh
sh tools/ios/build-runtime.sh
TF_IOS_SDK=iphonesimulator sh tools/ios/build-runtime.sh
```

Controller regression tests run through the guest ABI on the Mac, plus a GameController check with a synthetic
controller. The generated corpus contains only this repository's test code:

```sh
sh tests/corpus/build.sh
make -j4 rt-test PY=.venv/bin/python \
  SHIM='xinput1_3/XInputGetState xinput1_3/XInputGetCapabilities xinput1_3/XInputSetState'
mkdir -p build
xcrun clang -fobjc-arc -Wall -Wextra -Werror -Iruntime/include \
  tools/ios/check-gamepad.m runtime/host/gamepad.m runtime/core/gamepad.c \
  -framework Foundation -framework GameController -o build/check-gamepad
build/check-gamepad
```

The synthetic check covers mapping, packet stability, focus, primary-controller handoff, hotplug and restart. It does
not prove Bluetooth pairing or real game controls.

## Diagnostic apps

Each is a separate app with its own bundle identifier under `TF_BUNDLE_PREFIX`. Build with your team:

```sh
xcodegen generate --spec tools/ios/<app>/project.yml
xcodebuild -project tools/ios/<app>/<Project>.xcodeproj -scheme <Scheme> -configuration <Config> \
  -destination 'generic/platform=iOS' -derivedDataPath build/ios-<app> \
  DEVELOPMENT_TEAM=YOURTEAMID TF_BUNDLE_PREFIX=com.yourname -allowProvisioningUpdates build
```

- `vm-probe` (`TFMemoryProbe`, configurations `Baseline` and `Extended`): compares the Mac layout and smaller candidates
  with `mmap` hints and fixed `vm_map` reservations, commits no pages, releases every mapping, and writes
  `Documents/vm-probe.json`.
- `runtime-smoke` (`TFRuntimeSmoke`, `Debug`): links the iOS runtime, initializes it with software ordering and
  exercises one 64 KiB guest allocation. Run `sh tools/ios/build-runtime.sh` first.
- `engine-smoke` (`TFEngineSmoke`, `Debug`): runs `tier0.dll` and `vstdlib.dll` recompiled. `sh
  tools/ios/build-engine-check.sh` verifies both DLLs' hashes, copies only those two files into the build folder and
  prepares the project. The resulting app contains game code; keep it to yourself.
- `renderer-check` (`TFRendererCheck`, `Release`): needs the DXMT frameworks from README step 4.

`tools/ios/fetch-engine.command` and `tools/ios/steam-authorize.py` are an optional way to download only the engine
DLLs with DepotDownloader and Steam Guard's QR code, shown on a loopback-only page. They save no login.

## Vision Pro and VR

The iPad app runs on Vision Pro as a compatible iPad app, the fastest native windowed path; it installs and opens
there, and no headset gameplay has been tested. Immersive stereo needs a separate renderer path (Compositor Services)
with per-eye cameras and head tracking fed into the game's view.

[CircuitLord's VR mod installer](https://github.com/CircuitLord/CircuitLordVRModInstaller) is MIT, but the VR mod
itself is closed source and depends on a Windows plugin and Northstar hooks. The recompiler dispatches original x86
addresses to precompiled arm64 functions, so patching the original x86 bytes at run time does not change the code that
runs. Using that mod would need its author's source or a separately proven adaptation. No mod compatibility is claimed.
