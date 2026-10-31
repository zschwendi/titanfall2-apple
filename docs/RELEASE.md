# Releases and IPAs

**There is no downloadable IPA, and no playable build will be published here.** This page explains why, what a release
of this project is, and how to package your own build for your own devices.

## Current version

| | |
| --- | --- |
| Source | `main`, version 0.1 (build 1) in `tools/ios/native-game/Info.plist` |
| Known-good source | Tag [`ipad-known-good-2026-10-31`](https://github.com/zschwendi/titanfall2-apple/tree/ipad-known-good-2026-10-31): the source of the build the owner accepted on 31 October 2026 (main menu and *The Pilot's Gauntlet*, with sound and DualSense, on an M5 iPad) |
| Binary release | None. GitHub Releases stays empty until a release can contain only files that may be shared. |

## Why there is no IPA

A playable app contains the game. The build translates every function of the game's 21 Windows modules to C, compiles
them, and links them into one executable. In the tested app that executable is about 267 MB (255 MiB), almost all of it
translated code, with about 177,000 symbols named after the game's modules:

- Electronic Arts' and Respawn's own code: `engine`, `client`, `server`, `materialsystem_dx11`, `vphysics`,
  `datacache`, `studiorender`, `vgui2`, `vguimatsurface`, `localize`, `inputsystem`, `filesystem_stdio`,
  `rtech_game`, `launcher`, `tier0`, `vstdlib` and `ui(11)`;
- licensed middleware shipped with the game: Bink video (`bink2w64`, `binkawin64`) and Miles audio (`mileswin64`),
  and AMD's `amd_ags_x64`;
- a module table generated from those DLLs.

Leaving out the game's data files (`.rpak`, `.vpk`, `.mstr`, `.bik`) does not change that: the code itself comes from
the game. Like upstream titanfall2-mac, this project publishes no recompiled game code. Each player builds it on their
own Mac from their own copy. The same applies to TestFlight and the App Store.

Files that can be shared, and are:

| Part | License | Where |
| --- | --- | --- |
| Recompiler, runtime, iOS host, launcher, tools | MIT | This repository |
| DXMT patches | LGPL 2.1 or later | `patches/dxmt/`, `patches/dxmt-ios/` |
| DXMT, LLVM | LGPL 2.1+, Apache 2.0 with LLVM exceptions | Fetched and built by you |

## Options considered

| Option | Decision |
| --- | --- |
| Publish the playable TFNative IPA | **No.** It contains translated game code and middleware that we have no right to distribute. |
| Publish the launcher-only app (`TFAssetSetup`) as an IPA | **No.** It cannot play anything, and calling it a release would mislead. It would also install only on devices registered to its signer. |
| Publish a "shell" IPA without game code, to which each player adds a locally built game framework and re-signs everything | Possible in principle, but not built. The game code would have to move from the executable into a separate framework that the app loads, and each player would still need a Mac, Xcode, their game files, the multi-hour recompile and their own paid team to re-sign. It would save only the DXMT and LLVM build steps. |
| Players build and install their own app, optionally packaging it as an IPA for their own devices | **Supported**; see below. |

Publishing a playable IPA would need permission from the rights holders (Electronic Arts, and RAD Game Tools/Epic Games
for Bink and Miles).

## Package your own build

After README steps 1 to 6, package the app you built into an IPA kept outside this checkout, for example for a backup or
to install later on your own registered devices:

```sh
sh tools/ios/package-ipa.sh build/ios-native-game/Build/Products/Release-iphoneos/TFNative.app ~/TF2-private-builds
```

The script refuses an unsigned app or an output folder inside the checkout, packages the app without changing its
signature, checks the signature again inside the IPA, and writes three files:

- `TFNative-<version>-<build>-<date>.ipa`;
- `.ipa.sha256`, the checksum: check it with `shasum -a 256 -c <file>.ipa.sha256`;
- `.json`, a manifest with the bundle identifier, the profile's expiry date and number of registered devices, the
  entitlements, and `contains_game_code`. A TFNative app always reports `true`: **never publish or share it.**

The IPA installs only on the devices registered in its provisioning profile, and only until that profile expires
(7 days for a Personal Team, a year for the tested paid team's development profile; see
[REQUIREMENTS.md](REQUIREMENTS.md)). Apple documents installing such builds with Device Hub or Apple Configurator;
we have not tested installing an IPA. The tested install is `xcrun devicectl device install app` with the `.app`.

## What a release will contain

A release is published only when its files have passed `tools/publish/check.py`, contain no game code or game files,
and every feature it claims has been accepted on a physical device. Its notes will list the tested devices, the
known-good tag, and SHA-256 checksums for each attached file.
