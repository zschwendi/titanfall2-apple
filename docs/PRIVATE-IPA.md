# Public shell, private game payload

A playable app contains code translated from the game, so it can never be published. This route splits the app in
two:

- the **shell**: the launcher and the DXMT renderer, with no game code. It can be published as an IPA, but it cannot
  play on its own.
- the **game payload**, `TF2Game.framework`: built on your Mac from your own game files. It is never published.

A script then joins them and signs everything with your own Apple team, giving a private app for your own devices.

**Status, 31 October 2026:** the shell, a payload and an assembled, signed app have been built on the Mac, and every
check below passes. **The assembled app has not yet been installed or run on a device.** Until it has, the
single-app build in the [README](../README.md#install-from-scratch) remains the tested route, and no shell IPA is
attached to a release.

## What is where

| Part | Contents | Published |
| --- | --- | --- |
| Shell, `TF2-shell-<version>-<dxmt>.ipa` | The launcher (`main.m`, `Launcher.swift`, `GameModule.m`) and DXMT's `winemetal`, `dxgi` and `d3d11` frameworks | Yes, unsigned, once it is device-tested. Built from this repository's source and DXMT's; it reads no game files. |
| Game payload, `TF2Game.framework` | The runtime, the game driver (`gate3/run.c`), the Direct3D back end (`runtime/dxback`), the generated module table and the 21 recompiled game modules | **Never.** You build it from your game files. |
| Signing | Your provisioning profile, entitlements and certificate | Yours; they never leave your Mac |

## The interface between them

The payload exports exactly one symbol, `tf2_payload`, which returns a table
([`tools/ios/payload/tf2_payload.h`](../tools/ios/payload/tf2_payload.h)). The table holds its interface version, its
size, the DXMT fingerprint it was built against, a description of its source, and four functions: create the game
view, report activity, run the game, and count frames presented. The shell loads `Frameworks/TF2Game.framework` with
`dlopen`. It refuses a payload with a different interface version or DXMT fingerprint, and its launcher says why
(*"Game module not installed…"*, *"…built for a different version of this app…"*).

The runtime lives in the payload, not in the shell. Every indirect call or jump in the recompiled code looks up its
target through the runtime (`rt_dispatch_fn`), so keeping the runtime in the same image keeps those calls direct. The
runtime and the game code are also always compiled from the same checkout, and the code inside the payload is the
same as in the single-app build: `gate3/run.c` and the runtime are unchanged.

| Version | Value in this branch |
| --- | --- |
| Payload interface | `1` (`TF2_PAYLOAD_ABI`) |
| DXMT fingerprint | `7929cbea603b3a37`: DXMT `e94c312f…` with `patches/dxmt` (12) and `patches/dxmt-ios` 0001 to 0003 (`sh tools/ios/dxmt-fingerprint.sh`) |

## Signing

- iOS loads only frameworks signed by the same team as the app, so the assembler signs the DXMT frameworks and the
  payload first, then the app, with one certificate.
- You need a **paid Apple Developer Program team**, because the app needs Extended Virtual Addressing
  ([requirements](REQUIREMENTS.md)).
- Your profile comes from a tiny stub app, `tools/ios/profile-stub`. Xcode builds it for your team and bundle
  identifier with the game's entitlements file. The assembler copies that stub's provisioning profile and its exact
  signed entitlements, and finds its certificate in your keychain. The stub itself is never installed.
- When your profile expires, build the stub again and re-run the assembler. The payload does not need rebuilding.

## Build your private app

Run these from the top of your checkout, in zsh or bash.

1. Do README steps 1 and 2: get the source and your game files, and run `./tfrecomp setup`.

2. Patch DXMT's headers. These are the first three lines of README step 4; you do not need to build DXMT or LLVM:

   ```sh
   git -C third_party/dxmt checkout -b tf2 e94c312f5c054263acf261cfa109edf13e757587
   git -C third_party/dxmt -c user.name=dxmt-build -c user.email=dxmt-build@localhost am "$PWD"/patches/dxmt/*.patch
   for p in patches/dxmt-ios/*.patch; do git -C third_party/dxmt apply "$PWD/$p"; done
   ```

3. Build the payload. This builds the runtime in minutes, then recompiles the game, which takes hours the first time
   and is cached afterwards:

   ```sh
   sh tools/ios/build-payload.sh
   ```

4. Build the profile stub with your team and bundle prefix:

   ```sh
   xcodegen generate --spec tools/ios/profile-stub/project.yml
   xcodebuild -project tools/ios/profile-stub/TF2Profile.xcodeproj -scheme TF2Profile -configuration Release \
     -destination 'generic/platform=iOS' -derivedDataPath build/ios-profile \
     DEVELOPMENT_TEAM=YOURTEAMID TF_BUNDLE_PREFIX=com.yourname -allowProvisioningUpdates build
   ```

   Your iPad must already be registered to your team (for example by running any app on it from Xcode once).

5. Get the shell. When a release attaches it, download `TF2-shell-…ipa` and check its `.sha256` with
   `shasum -a 256 -c`. To build it yourself instead, do README steps 3 and 4 (LLVM and DXMT), then run
   `sh tools/ios/build-shell.sh`; it writes `build/ios-shell-release/TF2-shell-…ipa`.

6. Assemble and sign, into a folder outside the checkout:

   ```sh
   python3 tools/ios/assemble_private_ipa.py --shell build/ios-shell-release/TF2-shell-0.1-7929cbea603b3a37.ipa \
     --payload build/ios-payload/TF2Game.framework \
     --signing build/ios-profile/Build/Products/Release-iphoneos/TF2Profile.app --out ~/TF2-private-builds
   ```

   It writes `TFNative.app`, a private `.ipa`, its `.sha256` and a `.json` manifest. It refuses a shell and payload
   whose interface or DXMT fingerprint differ, a payload that exports anything besides `tf2_payload`, a shell that
   fails the audit, an expired profile, a stub signed without Extended Virtual Addressing, or a certificate missing
   from your keychain.

7. Install it, then copy your game files as in README step 7 (use your own bundle identifier):

   ```sh
   xcrun devicectl device install app --device DEVICE ~/TF2-private-builds/TFNative.app
   ```

To update, install over the existing app with the same bundle identifier; that keeps the game files and saves. A new
shell with the same interface and DXMT fingerprint needs only step 6. When either changes, rebuild the payload: the
game modules are cached, so only the runtime and the link run again.

## Checks

| Check | Result |
| --- | --- |
| Shell built in a clean tree: no `game` link, no game build, DXMT and the launcher only | Done (`git worktree`, branch `split-payload`) |
| `tools/ios/audit-shell.sh` on the shell: no symbols of any of the 21 recompiled modules, no module table, no runtime, no payload, no Mach-O over 64 MB | Passes (4 Mach-O files, 28 MB). The same audit fails the single-app game build on all four counts, which shows it can catch game code. |
| Payload against the accepted single-app build | Same 164,940 recompiled functions; `__text` within 16 KB (the launcher moved to the shell); exports only `tf2_payload`; imports only Apple system libraries; iOS 18.0 |
| Assembled app | `codesign --verify --deep --strict` passes; the app and all four frameworks are signed by one team; it carries Extended Virtual Addressing, Increased Memory Limit, the team and app identifiers and `get-task-allow`; manifest `contains_game_code: true` |
| On a device | **Not yet run** |
