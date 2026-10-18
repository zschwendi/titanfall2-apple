# Limits

What does not work, or is not claimed, in this version.

- **Single-player only.** Multiplayer is not supported. The runtime opens no sockets and makes no network connection.
- **Apple silicon only.** The build targets arm64 macOS. Intel Macs are not supported.
- **One Mac model tested.** Results come from one MacBook Pro, Apple M3 Pro, 18 GB, macOS 27.0.1.
- **One game version.** Only the game version whose DLL hashes the guest patches name
  (`lifter/guest_patches.toml`). Other versions are not checked.
- **Full speed in heavy scenes needs the hardware memory-ordering mode, and so an Apple ID in Xcode.** With
  a paid Developer Program membership it is known to work; with a free Apple ID (the `-unmanaged`
  entitlement, `./tfrecomp signing`) it is untested, and its profile has to be renewed every 7 days. Without it the build orders memory in software (`GUEST_MEM=tsob`: acquire loads and a
  store barrier before each store, which is x86's order): correct, and about 2.5 times slower on the CPU side,
  47 frames a second on the CPU benchmark (`sp_tday`, lowest settings, 960x540) against 120. In play at
  3024x1890 with MetalFX from 1800x1125: 102 frames a second in a light scene (`sp_sewers1`; 85 to 96 with the
  hardware mode, which is GPU-bound there) and 54 on average in a heavy one (`sp_tday`; 76 at first, 39 in the
  fight; 82 with the hardware mode). Release stores (`stlr`) would cost six times instead: each waits for every
  earlier load (measured, `runtime/rt.mk`, GUEST_DIAG).
- **The first build is slow.** Lifting and compiling about 10 GB of output per build kind takes a long time.
- **Known DXMT faults.** They are listed in `docs/findings/dxmt-faults.md`.
- **Anti-tamper and EA checks.** The game's own anti-tamper and EA checks are covered only as far as
  the stand-ins go. Fifteen Origin SDK calls are answered; any other call into the SDK stops the game.
- **Stops end the run.** An instruction with no translation halts the game with a report. The game is not resumed.
- **Speed is GPU-bound at the built-in display.** 43 to 46 fps average at 3024x1890 with highest settings and
  no upscaling; 82 to 96 with MetalFX upscaling from 1800x1125. Upscaling from 2016x1260 (x1.5) slows to about
  30 fps with long stalls after the first minute of one test scene; the cause is not known yet.
- **Not affiliated with or endorsed by Electronic Arts or Respawn Entertainment.** Titanfall is their trademark.
- **Licence.** MIT for this repository; the DXMT patches are under DXMT's LGPL 2.1 or later (NOTICE).
