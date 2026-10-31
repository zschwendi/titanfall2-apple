# titanfall2-apple

**Titanfall 2, recompiled ahead of time into native arm64 code and playable on iPad, first mission so far. Vision Pro is next.**

titanfall2-apple is an unofficial port built on [titanfall2-mac](https://github.com/rkutyna/titanfall2-mac), a static
recompiler that turns the Windows x86-64 DLLs of Titanfall 2 (the 2016 PC version) into native arm64 code and links
them with a runtime that stands in for Windows. Graphics go through [DXMT](https://github.com/3Shain/dxmt), an
open-source Direct3D 11 implementation on Metal. This fork adds an iOS/iPadOS build of that runtime and renderer, a
UIKit game app with a small SwiftUI launcher, game-controller input through Apple's GameController framework, and tools
to stage your own game files onto the device. There is no emulator, JIT, Wine or streaming PC: the game's own code runs
on the iPad.

[**Install from scratch →**](#install-from-scratch) · [Current status](#current-status) ·
[Troubleshooting](#troubleshooting) · [Port details](PORTING.md) · [Mac (upstream)](docs/MAC.md)

**You need your own copy of Titanfall 2 for PC.** This repository ships source code, patches and tools only. It contains
no game files, no recompiled game code, and no signed app, IPA or TestFlight build. Everything game-derived is generated
on your Mac from your copy and installed on your own device.

## Current status

As of 31 October 2026. Only what is described as tested has been run on hardware.

| Platform | Status |
| --- | --- |
| **iPad** | **Playable in the first mission on a physical iPad Pro 11-inch (M5), iPadOS 27.0.1, accepted by the owner.** The intro, the main menu (v2.0.11.0) and *The Pilot's Gauntlet* load, and the owner played the Gauntlet's timed run with a DualSense: **controls and sound work**. Earlier measurements at 1280×720 showed mostly 59.5 to 59.9 presents a second, with dips to 17 to 40 while the game streams or loads, and a memory footprint of about 4.6 to 5.6 GB. |
| **Controllers** | **DualSense works** (owner-accepted) in the menus and in the Gauntlet's timed run, with the game's controller prompts. Once, a launch started from the Mac while another app was in front got no controller input; relaunching the app fixed it. The cause is not found yet (see [Troubleshooting](#troubleshooting)). Rumble is not implemented, and there are no touch controls. |
| **Display** | The game now draws at 1920×1080 by default, and the launcher's Settings offer sizes from 1280×720 up to the panel's native size; DXMT lists matching display modes to the game. The owner reports that resolution and smoothness improved over the earlier fixed 1280×720; that is an impression, and frame pacing on the panel has not been measured. 16:9 sizes are letterboxed; a *Fit to Screen* option that fills the panel without stretching is planned. |
| **iPhone** | The app target includes iPhone, but no iPhone has been tested. |
| **Vision Pro** | The iPad app installs and opens on Vision Pro as a compatible iPad app; game files were still transferring at the time of writing. **No headset gameplay has been tested.** Immersive stereo, head tracking and PSVR2 Sense controllers are not implemented. |
| **Mac** | Upstream's Mac build path and defaults are kept ([docs/MAC.md](docs/MAC.md)). The Mac game has not been re-tested after this fork's changes. |
| **Multiplayer, Northstar, VR mod** | **Not supported.** The runtime is offline by design; see [Online play and VR](#online-play-and-vr). |

Only the menus and *The Pilot's Gauntlet* are staged onto the device (about 49.2 GB). Later campaign levels are not
copied, so the campaign cannot continue past the first mission yet. No full-campaign run, long play session, or thermal
and sustained-memory measurement has been done on iPad.

**Known-good build.** The build the owner accepted on 31 October 2026 (app built 06:41 UTC, installed 06:54 UTC) was
made from the source in commit
[`3f2be79`](https://github.com/zschwendi/titanfall2-apple/commit/3f2be798f109d16321e6ac3e30bf05a610ed6576), the commit
that added this port, together with the twelve `patches/dxmt` and
three `patches/dxmt-ios` patches. This was checked against the build's file times, its install record and strings in
the installed binary. The published source differs from the build only in what publishing required: bundle identifiers
come from `TF_BUNDLE_PREFIX` instead of the developer's own, no signing team or device is hard-coded, and asset staging
also accepts a normal install folder.

## What you need

**Hardware**

- An **Apple silicon Mac**. Tested with macOS 27.0 and Xcode 27.2.
- An **iPad with an M-series chip**. Tested only on an iPad Pro 11-inch (M5) with iPadOS 27.0.1. The app targets
  iPadOS 18 or later and used about 4.6 to 5.6 GB of memory in the first mission; other models are untested.
- A **game controller** paired with the iPad. A DualSense is the tested controller.
- A USB-C cable is recommended: the 49 GB copy also works over the local network, but more slowly.

**Accounts**

- An **Apple Developer account signed in to Xcode**. The app asks for the Extended Virtual Addressing and Increased
  Memory Limit entitlements, and it was tested with a paid Apple Developer Program team. A free Personal Team is
  untested and may not be granted those entitlements.
- **Titanfall 2 for PC on Steam** (app 1237970). The Windows files can be downloaded on the Mac through Steam's
  console. A copy of the install folder from a Windows PC or the EA app has the same files, but only the Steam download
  has been used for the iPad.

**Software on the Mac**

- Xcode with the iOS SDK, plus Apple's Metal Toolchain: `xcodebuild -downloadComponent MetalToolchain`.
- [Homebrew](https://brew.sh) packages: `brew install cmake ninja xcodegen python`.
- Python 3. `./tfrecomp setup` creates `.venv` with `capstone` and `unicorn`; you add Meson 1.5.2 to it.

**Storage**

- Mac: the game's files (about 67.6 GB for the main depot plus 0.76 GB of English audio) and room to build. Upstream
  asks for about 40 GB free during a game build. The finished iOS build outputs and caches took about 3 GB here, and
  the LLVM 15 source about 0.75 GB.
- iPad: about **50 GB free** for the staged files and the 282 MB app. Check *Settings > General > iPad Storage*.

## Install from scratch

Run every command from the top of your checkout. Replace `YOURTEAMID` with your Apple team ID, `com.yourname` with a
bundle-identifier prefix you control, and `DEVICE` with your iPad's name or identifier from `xcrun devicectl list devices`.

### 1. Get the source and the game files

```sh
git clone https://github.com/zschwendi/titanfall2-apple.git
cd titanfall2-apple
```

Open Steam's console with `open steam://open/console`, then download the main game and its English audio:

```text
download_depot 1237970 1237971
download_depot 1237970 1237972
```

Steam prints each folder when its download finishes. On a Mac they are usually under
`~/Library/Application Support/Steam/Steam.AppBundle/Steam/Contents/MacOS/steamapps/content/app_1237970/`.
Keep the checkout on the **same volume** as the game files: asset staging (step 7) makes hard links instead of a
second 49 GB copy.

### 2. Set up the checkout

```sh
./tfrecomp setup --game "/path/to/app_1237970/depot_1237971"
.venv/bin/pip install meson==1.5.2
```

`setup` checks that the game's DLLs are there, links `game` to that folder, creates `.venv`, and clones DXMT at the
pinned commit into `third_party/dxmt`. If you copied a whole install folder from a PC, pass that folder instead; it
already contains the English files.

### 3. Build LLVM 15 for the shader converter

DXMT's shader converter needs LLVM 15. Build its host tools once, then its iOS static libraries. These options are the
ones recorded in the tested build's CMake configuration.

```sh
mkdir -p build third_party
curl -L -o build/llvm-15.0.7.src.tar.xz https://github.com/llvm/llvm-project/releases/download/llvmorg-15.0.7/llvm-15.0.7.src.tar.xz
curl -L -o build/cmake-15.0.7.src.tar.xz https://github.com/llvm/llvm-project/releases/download/llvmorg-15.0.7/cmake-15.0.7.src.tar.xz
shasum -a 256 -c <<'SUMS'
4ad8b2cc8003c86d0078d15d987d84e3a739f24aae9033865c027abae93ee7a4  build/llvm-15.0.7.src.tar.xz
8986f29b634fdaa9862eedda78513969fe9788301c9f2d938f4c10a3e7a3e7ea  build/cmake-15.0.7.src.tar.xz
SUMS
tar -xf build/llvm-15.0.7.src.tar.xz -C third_party && mv third_party/llvm-15.0.7.src third_party/llvm15
tar -xf build/cmake-15.0.7.src.tar.xz -C third_party && mv third_party/cmake-15.0.7.src third_party/cmake

LLVM_OPTS="-G Ninja -DCMAKE_BUILD_TYPE=Release -DLLVM_TARGETS_TO_BUILD= -DLLVM_INCLUDE_TESTS=OFF \
  -DLLVM_INCLUDE_DOCS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_ENABLE_ZLIB=OFF \
  -DLLVM_ENABLE_ZSTD=OFF -DLLVM_ENABLE_TERMINFO=OFF -DLLVM_ENABLE_LIBXML2=OFF"
cmake -S third_party/llvm15 -B build/llvm15-host $LLVM_OPTS
ninja -C build/llvm15-host llvm-tblgen llvm-config
cmake -S third_party/llvm15 -B build/llvm15-ios $LLVM_OPTS -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=18.0 \
  -DLLVM_TABLEGEN="$PWD/build/llvm15-host/bin/llvm-tblgen"
ninja -C build/llvm15-ios LLVMPasses
```

The host build only provides tools; never link its libraries into the iOS build.

### 4. Patch and build DXMT for iOS

Apply upstream's twelve DXMT patches, then this fork's three iOS patches, and build the three renderer frameworks.
`TF_BUNDLE_PREFIX` sets the frameworks' bundle identifiers.

```sh
git -C third_party/dxmt checkout -b tf2 e94c312f5c054263acf261cfa109edf13e757587
git -C third_party/dxmt -c user.name=dxmt-build -c user.email=dxmt-build@localhost am "$PWD"/patches/dxmt/*.patch
for p in patches/dxmt-ios/*.patch; do git -C third_party/dxmt apply "$PWD/$p"; done
.venv/bin/python tools/ios/build-dxmt.py
TF_BUNDLE_PREFIX=com.yourname .venv/bin/python tools/ios/package-dxmt.py
```

The result is `build/ios-dxmt-frameworks/{winemetal,dxgi,d3d11}.framework`.

### 5. Build the runtime and recompile the game for iOS

```sh
sh tools/ios/build-runtime.sh
make -j1 PY=.venv/bin/python RT_PLATFORM=ios RT_VM_LAYOUT=ios \
  'RT_CC=xcrun --sdk iphoneos clang -target arm64-apple-ios18.0' \
  RT_OUT=build/ios-iphoneos/runtime RT_GEN=build/ios-iphoneos/gen \
  GUEST_MEM=tsob GATE3_OUT=build/ios-game LIFT_CACHE=build/ios-game-cache \
  GATE3_JOBS=2 build/ios-game/lifted/objects.filelist build/ios-game/gen/gate3_table.c
```

The `make` command reads your game DLLs, translates their 21 modules (about 165,000 functions) to C and compiles them
for iOS. It takes hours. Finished modules are cached, so an interrupted run resumes where it stopped; keep a few GB of
disk free while it runs. iOS uses software memory ordering (`GUEST_MEM=tsob`) because the Mac's hardware x86 ordering
mode is not available there.

### 6. Build, sign and install the app

```sh
xcodegen generate --spec tools/ios/native-game/project.yml
xcodebuild -project tools/ios/native-game/TFNative.xcodeproj -scheme TFNative -configuration Release \
  -destination 'generic/platform=iOS' -derivedDataPath build/ios-native-game \
  DEVELOPMENT_TEAM=YOURTEAMID TF_BUNDLE_PREFIX=com.yourname -allowProvisioningUpdates build
xcrun devicectl device install app --device DEVICE \
  build/ios-native-game/Build/Products/Release-iphoneos/TFNative.app
```

On the iPad, turn on *Settings > Privacy & Security > Developer Mode* if asked, keep the iPad unlocked during the
install, and trust your developer certificate under *Settings > General > VPN & Device Management* if iPadOS asks. The
app is called **TF Native**. To update it later, install over it with the same bundle identifier: that keeps the game
files and saves.

### 7. Stage and copy your game files

```sh
python3 tools/ios/stage-game-assets.py
xcrun devicectl device copy to --device DEVICE --domain-type appDataContainer \
  --domain-identifier com.yourname.titanfall.native \
  --source build/ios-game-assets/Game --destination Documents/Game
```

The staging script selects the menus, shared packs, English audio and *The Pilot's Gauntlet* (1,209 files and
49,196,834,062 bytes with the tested game version) as hard links under `build/ios-game-assets/Game`. It leaves out
installer files, multiplayer maps and later campaign levels. Never edit staged files: they share their bytes with your
Steam files. If your English depot is somewhere else, point `TF_ENGLISH_DEPOT` at it.

When the copy has finished, check it, and only then send the ready marker:

```sh
xcrun devicectl device info files --device DEVICE --domain-type appDataContainer \
  --domain-identifier com.yourname.titanfall.native --subdirectory Documents/Game \
  --timeout 60 --json-output build/device-files.json
python3 tools/ios/verify-device-assets.py build/device-files.json
xcrun devicectl device copy to --device DEVICE --domain-type appDataContainer \
  --domain-identifier com.yourname.titanfall.native \
  --source build/ios-game-assets/game-files-ready.json --destination Documents/game-files-ready.json
```

The check compares every path and size with the manifest; it does not hash the files. The app checks every path and
size again before it starts the game.

### 8. Play

1. Pair your controller in *Settings > Bluetooth*.
2. Open **TF Native**. The launcher shows *Ready to launch* once the ready marker is there.
3. Press Cross or tap the Titanfall 2 card. Options or the gear button opens Settings, where you can choose the render
   size; it applies the next time the game starts.

The game reaches its main menu about 20 seconds after it starts, and *The Pilot's Gauntlet* takes under a minute more
to load. Only the menus and that mission are installed. The device runs loaded the mission with the diagnostic launch
in [Troubleshooting](#troubleshooting); starting it from the game's own *Campaign* menu has not been tested separately.

## Controls

The game sees the controller as an Xbox 360 pad (XInput). On a DualSense, Cross, Circle, Square and Triangle are A, B,
X and Y; Options is Start; Create or a touchpad click is Back; the sticks, stick clicks, L1/R1 and the analog L2/R2
triggers map to their XInput counterparts. The game shows Xbox button names in its prompts. The controller used most
recently becomes player one, and input is neutral while the app is in the background.

## Saves and files on the iPad

| What | Where in the app's container |
| --- | --- |
| Saves and settings | `Library/Application Support/PlayerState` (included in device backups) |
| Game files | `Documents/Game` (excluded from backups; copy them again if needed) |
| Shader cache | `Library/Caches/DXMT` |
| Logs and diagnostics | `Documents/game-runtime.log`, `game-console.log`, `game-progress.json`, `game-result.json`, `device-info.json`, `frames/` |

Deleting the app deletes its game files and saves. Install updates over the existing app instead.

## Troubleshooting

| Symptom | What to do |
| --- | --- |
| Xcode cannot register the bundle identifier | Use a prefix you own, `TF_BUNDLE_PREFIX=com.yourname`, and the same one in every step. |
| The install fails with a CoreDevice error while the iPad is locked | Unlock the iPad, keep it awake, and install again. |
| *"Game files are still being transferred"* | The ready marker is missing. Finish step 7, verify, then send `game-files-ready.json`. |
| *"Game file is missing or incomplete: …"* | The copy is incomplete. Copy `Game` again and re-run `verify-device-assets.py`. |
| The game stops with *"cannot reserve the iOS … Extended Virtual Addressing is required"* | The app was signed without the Extended Virtual Addressing entitlement. Sign with a team that grants it. |
| No sound | Check the volume and silent mode. `game-runtime.log` has an `audio: coreaudio output` line about every 10 seconds with the frames written and their peak level. |
| The controller does nothing in the game | Close TF Native completely and open it again from the Home Screen with the controller already connected. This happened once after a launch from the Mac while another app was in front. `game-console.log` shows `TFNative controller connected` when iPadOS hands the pad to the app. |
| The game module build stops on low disk | Free some space and run the same `make` command again; finished modules are kept. |
| The iPad is out of space | The staged files need about 49.2 GB plus the app. Free space on the iPad first; nothing in this project deletes data on the device. |

To bring a run's logs and frames back to the Mac (frames are converted to PNG):

```sh
TF_DEVICE=DEVICE TF_BUNDLE_PREFIX=com.yourname sh tools/ios/pull-native-run.sh build/run1
```

For diagnostics, `TF_AUTOSTART=1` skips the launcher, and `TF_PILOT_TRAINING=1` loads *The Pilot's Gauntlet* three
seconds after the main menu opens:

```sh
xcrun devicectl device process launch --device DEVICE \
  --environment-variables '{"TF_AUTOSTART":"1","TF_PILOT_TRAINING":"1"}' com.yourname.titanfall.native
```

[PORTING.md](PORTING.md) has the design notes, the separate runtime, engine, renderer and memory-layout check apps,
and how each part was verified.

## Online play and VR

- **Multiplayer and Northstar.** The runtime keeps all networking inside the process and answers EA's SDK with an
  offline local profile ([docs/EA-STANDINS.md](docs/EA-STANDINS.md)), so the game's data-center search never connects.
  Northstar is a Windows launcher that injects DLLs into the original game; that does not carry over to recompiled
  arm64 code. Online play would need real network transport, protocol and authentication work and a compatible
  server; none of that exists here.
- **Titanfall VR mod.** [CircuitLord's VR mod installer](https://github.com/CircuitLord/CircuitLordVRModInstaller) is
  open source, but the VR mod itself is a closed-source Windows plugin that relies on Northstar's hooks. Because this
  port runs precompiled arm64 functions, patching the original x86 code at run time does not change what runs. Using
  that mod would need its author's source or a separately proven adaptation.
- **Vision Pro plan:** windowed play with a controller first, then immersive stereoscopic play with head tracking, then
  PSVR2 Sense motion controls. None of these has been tested yet.

## How it works

1. **Recompile.** Upstream's lifter translates every function of the game's 21 Windows modules from x86-64 to C. They
   are compiled for `arm64-apple-ios18.0` and linked into the app. Software memory ordering preserves x86's ordering
   rules on iOS.
2. **Runtime.** Upstream's runtime answers the Windows APIs the game uses: files, threads, memory, the registry, sound
   (WASAPI to CoreAudio) and input (XInput). This fork adds an iOS memory layout inside a 20 GB reservation, which
   needs Extended Virtual Addressing, a UIKit host with a `CAMetalLayer` and an audio session, and GameController input.
3. **Graphics.** DXMT runs the game's Direct3D 11 on Metal. The iOS patches add Metal's iOS shader target and
   metallib format, the LLVM shader converter for iOS, iOS-safe buffer storage, and a display that reports the iPad's
   panel and a list of modes.
4. **App.** `tools/ios/native-game` is the app. Its launcher checks the files, then starts the game on a worker thread
   through `gate3_run_embedded`, which leaves out the Mac driver's fork, exec and event loop.

| Directory | Contents |
| --- | --- |
| `runtime/host_ios/`, `runtime/host/gamepad.m`, `runtime/core/gamepad.c` | The iOS window and audio host, and controller input (new) |
| `patches/dxmt-ios/` | DXMT patches for iOS (new) |
| `tools/ios/` | The iOS app, check apps, and build and staging scripts (new) |
| `lifter/`, `harness/`, `runtime/`, `gate2/`, `gate3/` | Upstream's recompiler, runtime and drivers, with this fork's iOS changes |
| `app/`, `tfrecomp` | Upstream's Mac app bundle and command-line tool |
| `docs/`, `research_notes/` | Upstream's designs, findings and limits; [docs/MAC.md](docs/MAC.md) is upstream's README |

Tests that need no game files: `make test` (upstream's suite) and the controller checks in
[PORTING.md](PORTING.md#build-and-verify-the-runtime).

## Credits and licenses

- **[rkutyna/titanfall2-mac](https://github.com/rkutyna/titanfall2-mac)** (MIT): the static recompiler, the runtime,
  the Mac port, the DXMT native patches and the documentation under `docs/`. This repository's history starts from that
  project's published commit, and its MIT license and copyright notice are kept.
- **[DXMT](https://github.com/3Shain/dxmt)** by 3Shain (LGPL 2.1 or later): Direct3D 11 on Metal. DXMT is not included;
  `./tfrecomp setup` clones it. The patches in `patches/dxmt/` and `patches/dxmt-ios/` change DXMT and are under its
  license.
- **[LLVM](https://llvm.org) 15.0.7** (Apache 2.0 with LLVM exceptions): downloaded and built on your Mac; not included.
- Build-time tools, not included: [capstone](https://www.capstone-engine.org) (BSD), [unicorn](https://www.unicorn-engine.org)
  (GPL 2.0), [Meson](https://mesonbuild.com) (Apache 2.0) and [XcodeGen](https://github.com/yonaskolb/XcodeGen) (MIT).
  The optional engine-file download helper (`tools/ios/fetch-engine.command`) runs
  [DepotDownloader](https://github.com/SteamRE/DepotDownloader) (GPL 2.0), which you install yourself.
- Interface facts in `runtime/com/desc/` come from the MinGW-w64 headers and Wine's IDL files; see [NOTICE](NOTICE).

This repository's own code is under the MIT License ([LICENSE](LICENSE)), except as listed in [NOTICE](NOTICE).

## Legal

Titanfall is a trademark of Electronic Arts Inc. This project is not affiliated with or endorsed by Electronic Arts,
Respawn Entertainment, Valve, Sony or Apple. It contains no game files and nothing taken from them. Use it only with a
copy of Titanfall 2 you own, and do not share built apps, recompiled code or staged game files: they contain the game.

## Issues

Found a reproducible problem? [Check existing issues](https://github.com/zschwendi/titanfall2-apple/issues) first. For
an iPad report, include the device model, the iPadOS version, the step that failed and the relevant lines of
`game-runtime.log` or `game-console.log`, with personal paths and identifiers removed.
