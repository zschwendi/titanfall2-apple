# Titanfall 2 on the Mac (upstream titanfall2-mac)

This page is the README of [rkutyna/titanfall2-mac](https://github.com/rkutyna/titanfall2-mac) at commit
`7e6a6e522c2dd825b70ccebf9424bd42210588db`, the base of titanfall2-apple, kept with its links adjusted for this
folder. The results and measurements below are the upstream author's, on their Mac. titanfall2-apple keeps the Mac
build path and its defaults, but the Mac game has not been re-tested after this fork's iOS changes.
Apple-device instructions are in the [main README](../README.md).

---

Titanfall 2's single-player campaign, running natively on Apple-silicon Macs.

This is a static recompiler. It turns the Windows x86-64 DLLs of Titanfall 2 (the 2016 PC version) into
native arm64 code ahead of time, and links them with a runtime that stands in for the parts of Windows the
game uses: files, threads, memory, windows, input, sound and the registry. Graphics go through
[DXMT](https://github.com/3Shain/dxmt), an open-source Direct3D 11 implementation on Metal. There is no
emulator and no Wine at run time: the game's code runs as an ordinary Mac program.

**You need your own purchased copy of Titanfall 2 for PC.** This repository contains no game files, no
recompiled game code and nothing taken from the game. The build reads your copy of the game's DLLs on your
Mac and writes everything it makes there. Multiplayer is not supported, and nothing ever connects to EA or
anywhere else: the runtime opens no network sockets (see [EA's SDK](#eas-sdk)).

## Status

- The campaign plays on the development Mac (MacBook Pro, Apple M3 Pro, 18 GB, macOS 27): long sessions
  with sound, and all 13 campaign maps load and run in automated tests (`./tfrecomp smoke`).
- Tested on that one Mac model only. Reports from other Macs are welcome ([CONTRIBUTING.md](../CONTRIBUTING.md)).
- Speed at the 14-inch MacBook Pro's full resolution (3024x1890), highest settings:

| | With the hardware memory-ordering mode (Apple ID) | Without (no Apple account) |
|---|---|---|
| Native 3024x1890, average | 43 to 46 fps (the GPU is the limit) | not measured |
| MetalFX from 1800x1125, light scene, average / 1% low | 85 to 96 | 102 / 92 |
| MetalFX from 1800x1125, heavy fight, average / 1% low | 82 / 40 | 54 / 27 |

The two columns are explained under [Memory ordering](#memory-ordering-and-your-apple-account).
What does not work yet is in [docs/LIMITS.md](LIMITS.md).

## What you need

- An Apple-silicon Mac with macOS 26.5 or later, and about 40 GB of free disk space for the build, besides
  the game's own files (about 60 GB).
- Xcode, with the Metal Toolchain: `xcodebuild -downloadComponent MetalToolchain`.
- Homebrew packages: `llvm@15`, `meson`, `ninja`.
- Python 3 (`./tfrecomp setup` installs `capstone` and `unicorn` into `.venv`, see `requirements.txt`).
- The game's PC files: the whole install folder of Titanfall 2, from Steam (`steamapps/common/Titanfall2`)
  or the EA app (`EA Games/Titanfall2`) on a Windows PC, copied to the Mac (an external drive works).
  `tools/gamedata/fetch.py` can also copy it over SSH from a Linux machine that has it (for example a Proton
  install); it uses GNU `find`, `stat` and `dd` there.
- Optional, for full speed in heavy scenes: an Apple ID signed into Xcode (see below).

## Quick start

```
./tfrecomp doctor                    # checks the tools above
./tfrecomp setup --game <folder>     # <folder>: your copy of the game's install folder
./tfrecomp build                     # recompiles the game; the first build takes a long time
./tfrecomp play --size 1800x1125 --upscale 1.68
```

`./tfrecomp play` options: `--size WxH` (the size the game draws at), `--upscale F` (MetalFX upscaling of
the picture by F; 1800x1125 times 1.68 fills a 3024x1890 display, about twice as fast as drawing at full
size), `--window full|hidden`, `--state DIR`, `--volume V`. Saves and settings live in
`~/Library/Application Support/tfrecomp/player`.

## Memory ordering and your Apple account

x86 programs rely on stricter ordering of memory accesses between threads than arm64 gives. Apple silicon
has a hardware x86 ordering mode (the one Rosetta uses), and macOS grants it only to an app whose
provisioning profile carries a cross-architecture entitlement:

| Apple account | Entitlement | Status |
|---|---|---|
| Paid Apple Developer Program | `com.apple.developer.cross-architecture-support` | Works |
| Free Apple ID (Xcode's "Personal Team") | `com.apple.developer.cross-architecture-support-unmanaged` | **Experimental, untested.** Reported to be available to free accounts (wine-devel, 7 August 2026). The profile lasts 7 days |

To use it: Xcode > Settings > Accounts, add your Apple ID; open `app/profile-stub/TFRecompProfile.xcodeproj`
once, pick your team under Signing & Capabilities and build it; then

```
./tfrecomp signing --team <TEAMID> --bundle-id com.yourname.tfrecomp     # add --paid for a paid team
```

It builds a small stub app with your team, checks the profile, runs a memory-ordering test (it must report
0 violations) and writes `app/signing.local.mk`; `./tfrecomp build` then signs the game with it. If you try
this with a free Apple ID, please report what happens (there is an issue template for it).

Without any Apple account, `./tfrecomp` signs the game ad hoc and builds it with memory ordering done in
software (acquire loads and a store barrier before every store, which is exactly x86's order). The game runs
correctly; light scenes run at full speed and heavy fights at about half (the table above). How this was
measured is in [docs/LIMITS.md](LIMITS.md) and [docs/HOW-IT-WORKS.md](HOW-IT-WORKS.md).

## EA's SDK

Titanfall 2 links against EA's SDK (`OriginSDK.dll`). This project never reads, loads or runs that DLL, nor
`Titanfall2.exe`. When the game calls one of 15 SDK functions, the runtime answers it itself. By default it
presents a made-up local player (user id 1, persona 1, the name "Pilot") so that the game has someone to keep
its progress for, and otherwise answers the way the SDK does when EA's service cannot be reached: not online,
requests accepted that never complete. No answer claims an entitlement or hands over an authorisation code,
nothing comes from any real account, and nothing is sent anywhere. Any other SDK call stops the game with a
report. Each answer is listed in [docs/EA-STANDINS.md](EA-STANDINS.md).

## How it was made

[docs/HOW-IT-WORKS.md](HOW-IT-WORKS.md) follows the pipeline from a game DLL to the running game;
[docs/DESIGN.md](DESIGN.md) and [docs/RUNTIME.md](RUNTIME.md) are the detailed designs of the
recompiler and the runtime; [docs/METHOD.md](METHOD.md) describes how the work was organised (one planner
model and many small-model tasks, each checked before it was merged); `docs/findings/` and `docs/research/`
hold the measurements and background research. Some code comments cite internal planning notes
(`docs/handoff/`, `docs/reviews/`, `docs/scout/`) that are not part of this repository, and "issue N" in a
code comment means an issue of the earlier private repository, not of this one (issue 32 was the review of
every answer to EA's SDK; those answers are listed in [docs/EA-STANDINS.md](EA-STANDINS.md)).

Tests: `make test` (no game files needed). `make gate3-check` builds the whole game and checks it.

| Directory | Contents |
|---|---|
| `lifter/` | The static recompiler: x86-64 functions to C |
| `harness/` | The header lifted code includes, and the difference-test harness (Unicorn as the oracle) |
| `runtime/` | The native runtime: Windows stand-ins, the loader, threads, graphics and sound back ends |
| `gate2/`, `gate3/` | Drivers: two modules (gate2) and the whole game (gate3), with their checks |
| `app/` | The app bundle and its signing |
| `tools/` | Build and test tools, the task runner, the publishing checks |
| `patches/dxmt/` | The 12 patches to DXMT (a native macOS window, statistics, measuring switches) |
| `docs/`, `research_notes/` | Designs, findings, research, limits, method |
| `tests/`, `bench/` | Test programs and speed measurements |

## Legal

- This repository is under the MIT License ([LICENSE](../LICENSE)), except the DXMT patches, which are under
  DXMT's LGPL 2.1 or later; see [NOTICE](../NOTICE).
- Titanfall is a trademark of Electronic Arts Inc. This project is not affiliated with or endorsed by
  Electronic Arts or Respawn Entertainment.
- It contains no game files. Use it only with a copy of Titanfall 2 you own.
