# How it works: from a game DLL to a running game

This document follows one path: from the game's Windows DLLs to the native program that runs the
campaign. Terms: *guest* is the original x86-64 Windows code and its view of the world; *host* is
the arm64 macOS process; *module* is one game DLL; *RVA* is an offset from a module's base address.

## 1. Finding and lifting functions

The lifter reads one PE32+ DLL and writes, under `lifted/<module>/` (git-ignored):

- generated C, about 200 guest functions per file, and a header of prototypes;
- a sorted table of `{rva, host function}` for indirect calls;
- a report with one record per function, a skip list and a stamp;
- a control-flow file (instruction and branch addresses, and stops) used by the tests;
- a file of sample instructions per operand shape, used by the instruction tests.

Functions are found from the unwind table (`.pdata`). A chained entry is a fragment of the function
its chain leads to. Leaf functions have no unwind entry; they are found by decoding from the entry
point, exports, direct call targets, tail-jump targets and code pointers. (`docs/DESIGN.md`, section 2.)

A function's status is one of three:

| Status | Meaning |
|---|---|
| `lifted` | Translated in full |
| `lifted-without-handlers` | Translated; some bytes of its unwind ranges are exception-handler code, which is not lifted yet |
| `skipped` | Not translated. Its stub stops the run and gives the reason |

**Stops.** An instruction that is decoded but has no translation is a *stop*. The lifted code runs
everything before it exactly, then halts the run with a report at that instruction. Any other
refusal makes the lifter skip the whole function. The lifter never emits a partial or approximate
translation: a missing function is visible, a wrong one is not. (`docs/DESIGN.md`, section 1 and section 3.)

## 2. Guest state, memory and calls

- **Registers and flags.** The guest state is one `Ctx` struct: the 16 general registers, the flags,
  the 16 SSE registers, the MXCSR, the x87 control word and the guest TEB address (`gs_base`).
  A 32-bit write zero-extends to 64 bits, as on x86. (`docs/DESIGN.md`, section 4.)
- **Memory.** A guest address is a host address. Loads and stores go through `LD*` and `ST*`
  helpers. A module can load at any base; each reference is `base + rva`. The guest stack is
  ordinary memory, and `push`, `pop`, `call` and `ret` move it as x86 does. (`docs/DESIGN.md`, section 5.)
- **Calls.** Every way out of a function is a macro, so the same generated C works in the test
  harness and in the linked program: `CALL_DIRECT` and `CALL_INDIRECT` for calls, `TAIL_DIRECT` and
  `TAIL_INDIRECT` for tail jumps, and `RET`. Indirect targets are looked up in the module's table.
  (`docs/DESIGN.md`, section 6.)
- **Special instructions.** `lock`-prefixed instructions and `xchg` with memory go through helpers
  that use sequentially consistent host atomics. x87 instructions other than the control word are
  stops. (`docs/DESIGN.md`, section 7.)

## 3. Memory ordering: two ways

x86 programs assume stronger memory ordering than arm64 gives. There are two ways to deal with it.

| Way | What it needs | Result |
|---|---|---|
| Hardware mode | macOS grants the x86 memory-ordering mode (`thread_set_x86_64_compat`) only to an app signed with a cross-architecture entitlement and a development provisioning profile: `com.apple.developer.cross-architecture-support` (paid Apple Developer Program; works) or `com.apple.developer.cross-architecture-support-unmanaged` (a free Apple ID's Personal Team; untested). `tools/signing/make_profile.sh` gets the profile through Xcode's automatic signing | The hardware orders the arm64 code's memory accesses as x86 does |
| Software mode (`GUEST_MEM=tsob`) | No Apple account. Every guest load is an acquire load (`ldapr`) and every store is preceded by a store barrier (`dmb ishst`): x86's own order. The app is signed ad hoc | Works without an account; about 2.5 times slower on the CPU side (47 against 120 frames a second on the CPU benchmark). A release store (`stlr`) instead of the barrier would cost six times, because it also waits for every earlier load |

## 4. The runtime and its Windows stand-ins

The runtime is the native program the lifted C is linked into. (`docs/RUNTIME.md`.)

- **One host program.** The lifted C of every module is linked into one executable in the app
  bundle. At start-up the loader reads each module's original DLL file and maps its image into
  guest memory, including `.text`. The image is data only: it is never executed. Keeping the whole
  image keeps code addresses that guest code stores and compares real and unique.
- **Fixed addresses.** Two address ranges are reserved at start-up, one for the images and one for
  memory the guest sees (`VirtualAlloc`, file views, stacks). Each module's base is the same in
  every run of a build.
- **Dispatch.** A guest code address is turned into a host function by a table lookup.
- **Shims.** A shim is a host C function that stands in for one imported Windows function. The
  runtime keeps one table of handles for kernel objects, and it implements waiting, the
  `VirtualAlloc` family on 16 KB pages, guest stacks and heaps.
- **Start-up.** The game's executable, `Titanfall2.exe`, is never read or run. The project has its
  own entry point, and it calls the launcher DLL's main function.
- **EA's Origin SDK.** The Origin SDK DLL is never read, lifted or run. Fifteen of its calls are
  answered locally by small stand-ins in `runtime/standins/originsdk/`. Each answer was decided by
  the owner before it was written, so the campaign starts offline. Any other call into the SDK stops
  the game. The runtime opens no sockets.

## 5. Graphics through DXMT

Direct3D 11 calls go to DXMT (`https://github.com/3Shain/dxmt`), which implements them on Metal.
The project uses DXMT with 12 patches in `patches/dxmt/`:

- a native macOS window in place of Wine's window;
- monitor enumeration and display modes;
- a log and an abort function that the host program receives, so that a stop gives the host's report;
- frame and pass statistics;
- measuring experiments, which are off unless an environment variable turns them on.

DXMT is under the GNU LGPL. The patches are under the same licence.

## 6. The app bundle and signing

`app/app.mk` wraps a built program in an app bundle, `TFRecomp.app`, and signs it. With a development
identity the bundle carries the entitlement and an embedded profile, and the hardware memory-ordering
mode is granted. Without one, the bundle is signed ad hoc and the build uses `GUEST_MEM=tsob`.
A local signing file, `app/signing.local.mk`, holds the App ID, team and profile. It is git-ignored;
`app/signing.example.mk` is its template. Nothing in the build talks to Apple.

## 7. The lift cache

`make gate3-build` does not lift into the checkout. `tools/liftcache/liftcache.py` keeps each module's
lifted C and compiled objects under `.cache/lifted/` (git-ignored), one entry per module and key. The
key covers the DLL, the lifter, the headers under `harness/`, the compiler flags and the compiler, so a
change to any of them builds that module again and anything else links the cached objects. Each build
kind (hardware or software memory ordering) has entries of its own, about 10 GB in all; the first build
lifts and compiles everything and takes a long time. (`make lift MODULE=<m>` writes one module's C to
`lifted/<m>/` for reading and for the difference tests.)

## 8. Testing

| Check | What it does | Needs game files |
|---|---|---|
| Difference tests (`docs/DESIGN.md`, section 8) | Run the same function or instruction in the Unicorn CPU emulator (the oracle) and in the lifted code compiled for arm64, and compare the results. The seeds give the initial state | No |
| Real-PC oracle (`tools/hworacle`) | Checks instructions where the emulators disagree, on a real x86-64 PC | No |
| `make test` | The test suite | No |
| `make gate2-check` | Two modules (tier0, vstdlib) lifted, loaded and called in one program; checks the module table | Yes (skipped without them) |
| `make gate3-check` | Builds the whole game and checks it | Yes |
| `./tfrecomp smoke` (`gate3/smoke_maps.sh`) | Starts each campaign map hidden, with nobody playing, for 170 seconds by default, and reports how far it got | Yes |
