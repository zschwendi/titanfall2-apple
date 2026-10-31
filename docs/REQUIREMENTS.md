# Requirements: Apple accounts, entitlements, devices and iCloud

What titanfall2-apple needs from Apple's side, with the evidence for each answer. Every row says whether it was
**tested** on our hardware, **documented** by Apple, or **unknown**. Apple pages were read on 31 October 2026; check
them again before relying on them, because Apple changes these rules.

## Short answer

You need a **paid Apple Developer Program membership** (or an Enterprise membership) to run the current build.

The game's iOS memory layout needs the Extended Virtual Addressing entitlement. Apple's capability table lists that
entitlement for the Apple Developer Program and the Enterprise program but not for free accounts (Xcode's Personal
Team), and without it the memory reservation fails on the device. We have not tried a Personal Team ourselves: that
answer comes from Apple's table and our own Baseline-versus-Extended probe, not from a failed Personal Team build.

## Entitlements the app uses

| Entitlement | Why the app needs it | Apple's documentation | Our evidence |
| --- | --- | --- | --- |
| `com.apple.developer.kernel.extended-virtual-addressing` | The runtime reserves a 4 GiB image arena at 448 GiB and a 16 GiB allocation arena at 456 GiB with fixed `vm_map` calls. | iOS and iPadOS 14.0 or later, and tvOS. visionOS is not listed. [Supported capabilities (iOS)](https://developer.apple.com/help/account/reference/supported-capabilities-ios): Apple Developer Program yes, Enterprise yes, free Apple Developer account **no**. [Entitlement page](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.developer.kernel.extended-virtual-addressing). | **Tested** on the M5 iPad: with the entitlement both arenas are reserved exactly; the same probe signed without it fails its reservation (Mach error 1). See [PORTING.md](../PORTING.md#what-has-been-verified-on-hardware). |
| `com.apple.developer.kernel.increased-memory-limit` | The first mission's footprint was about 4.6 to 5.6 GB on the M5 iPad. | iOS and iPadOS 15.0 or later, visionOS 2.5 or later. "Only available on some device models"; apps should check `os_proc_available_memory`. It is **not listed** in Apple's supported-capabilities table, so its availability to free accounts is **not documented**. [Entitlement page](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.developer.kernel.increased-memory-limit). | Signed into the tested build by a paid team. Whether the game runs without it, or on which models it raises the limit, is **untested**. |

## Account types

| | Free Apple Developer account (Personal Team) | Apple Developer Program |
| --- | --- | --- |
| Extended Virtual Addressing | **Not available** (Apple's capability table) | Available; **tested** |
| Increased Memory Limit | Not documented either way | Available; **tested** |
| iCloud (iCloud Documents, CloudKit, key-value storage) | **Not available** (Apple's capability table) | Available |
| Background modes | Available (Apple's capability table) | Available |
| How long an installed build keeps working | Provisioning profiles expire **7 days** after they are issued; then rebuild and reinstall ([Developer account overview](https://developer.apple.com/support/compare-memberships/)). | The Xcode-managed development profile of the tested build expires **one year** after it was issued (**observed** in its embedded profile). |
| Devices | Up to 3 registered devices, which expire after 7 days; up to 3 apps per device (same page) | Up to 100 devices per product family per membership year ([Devices overview](https://developer.apple.com/help/account/devices/devices-overview)) |
| Can run titanfall2-apple today | **No**, because of Extended Virtual Addressing (documented, not tried) | **Yes** (tested) |

Apple defines the free tier as Apple Account holders who accepted the Apple Developer Agreement; there is no fee, and
they cannot distribute apps. Xcode calls that account's team a Personal Team.

## First launch needs the internet once

For teams created after 6 June 2021, development- and ad hoc-signed iOS and iPadOS apps check their signing
certificate with Apple (`ppq.apple.com`) the first time they launch, and may not launch if the device cannot connect.
Apple offers 7-day offline development profiles and says longer offline validity needs a request
([Provisioning profile updates](https://developer.apple.com/help/account/provisioning-profiles/provisioning-profile-updates/)).
The game itself never connects to anything.

## Devices and system versions

| | Status |
| --- | --- |
| iPad Pro 11-inch (M5), iPadOS 27.0.1 | **Tested** (main menu and *The Pilot's Gauntlet*) |
| Other M-series iPads | Untested. The memory footprint, not the chip, is the likely limit. |
| iPads with A-series chips, any iPhone | Untested. An A14 iPad was tried only with the memory probe, and Guided Access blocked the launch. |
| Minimum system | The app targets iOS/iPadOS 18.0. Only iPadOS 27.0.1 has been tested. |
| Vision Pro (the iPad app, visionOS 27.2) | The app installs and its launcher opens, with about 6.4 GB available to it. The game has not been started there. Apple does not list visionOS for Extended Virtual Addressing, but a separate native visionOS build in development reserved both of the game's arenas exactly with it on Vision Pro (**tested**; not yet in this repository). Whether the iPad app running on Vision Pro gets the same is still **untested**. |
| Mac for building | Apple silicon, tested with macOS 27.0 and Xcode 27.2 |

## Bundle identifiers, teams and iCloud saves

- Every builder signs with their own team and their own bundle identifier (`TF_BUNDLE_PREFIX`). An App ID belongs to
  one team, so two people's builds are two different apps to iOS.
- **iCloud saves are not in the current build.** When they are added they will follow Dark Eco's design: the game
  keeps playing from local saves, and an iCloud Documents ("ubiquity") container holds a mirror plus a conflict
  archive, reconciled when the launcher appears, before play, and after each completed save.
- iCloud needs a paid membership (Apple's capability table) and an iCloud container. Container identifiers start with
  `iCloud.` and use reverse-DNS notation, and Xcode registers them in your own developer account
  ([Configuring iCloud services](https://developer.apple.com/documentation/xcode/configuring-icloud-services)). The
  container is part of the app's entitlements, so you will create your own, for example
  `iCloud.com.yourname.titanfall`.
- Consequences: saves sync only between your own builds that use the same container and the same Apple Account, on
  devices signed into that account with iCloud Drive on. Builds made by different people never share saves. If you
  change your bundle prefix, team or container later, the new build sees neither the old app's local saves (a
  different bundle identifier is a different app container) nor its iCloud copy, so export your saves first. A free
  account cannot use iCloud at all.

## Installing and re-signing

| Route | Status |
| --- | --- |
| Build with `xcodebuild`, install the `.app` with `xcrun devicectl device install app` | **Tested** (README steps 6 and 7) |
| Package your own signed build as an `.ipa` with `tools/ios/package-ipa.sh` | Packaging **tested** on the project's check apps; installing such an `.ipa` has not been tried |
| Install an `.ipa` with Apple's tools | Apple documents installing exported builds on registered devices with Device Hub or Apple Configurator ([Distributing your app to registered devices](https://developer.apple.com/documentation/xcode/distributing-your-app-to-registered-devices)); **untested** here |
| Xcode archive and export (`xcodebuild -exportArchive`, method `debugging` or `release-testing`) | Apple's documented route to export and re-sign for a team (`teamID`, `signingStyle`); **untested** here |
| Third-party re-signing or sideloading tools | Not documented by Apple and **unsupported** here |
| TestFlight or App Store | **Not possible**: the app contains code translated from the game, which cannot be distributed ([docs/RELEASE.md](RELEASE.md)) |

An `.ipa` signed with a development or ad hoc profile installs only on the devices registered in that profile, and
stops launching when the profile expires.
