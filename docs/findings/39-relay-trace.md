# Finding 39: the second reference trace (every Windows call of the game's DLLs): the index

2026-10-10. The capture was made by hand on a test machine (how, and what was measured about the recording, is not repeated here); this
finding is what is in it, where the pieces are, and how to cut another piece. The logs stay on
a test machine, whole. Every number below was printed by a tool of `tools/trace/`
(the command is given, or is in the slice named); **[R]** marks a reading of mine.
Times are seconds after the first log line of the game's process.

## The captures

| name for `cut.py` | directory there | what | size, measured by `cut.py <name> index` |
|---|---|---|---|
| `mission1` | `a3-mission1` | **the capture**: start, main menu, a campaign mission (`sp_training`), play, quit from the game's menu | 1,770,022,571 bytes, 18,574,218 lines; game process `04e8`, 267.794 s |
| `start2` | `a3-snoop2` | a second start with the same lists, stopped by the script; no snoop line in it (this Wine cannot record calls into native DLLs on 64-bit) | 238,531,009 bytes, 2,602,901 lines; game process `055c`, 63.883 s |
| `test1` | `a3-test1` | the first test: the caller filter did not work, so **every** module's calls are in it; never reached the title screen | 6,346,712,026 bytes, 73,969,891 lines; game process `051c`, 79.934 s |

## What is in the capture, and what cannot be

Recorded (`mission1`, `cut.py mission1 census --commit-safe`, committed as
`docs/trace/census-mission1.tsv`): **8,720,710 calls of 259 names** made by the modules of the
install directory (the game's DLLs and the executable), each with arguments, result, thread and
call site; 257 of the names are functions, two are calls *into* the game (`(wndproc)`,
`(dllmain)`). Besides those: 528,267 window-procedure calls, 82,822 hook-procedure calls, 5,461
DllMain calls and 804 TLS callbacks of the process (the index's `special_calls`), the module
loads, the thread starts and names, and the exception channel.

Not recorded, each for its own reason:

1. **Calls into native DLLs.** Relay records calls into Wine's own DLLs only. Of the 739 imports
   of `runtime/tools/imports.def`: all 43 of `msvcr110.dll` and 34 of `msvcp110.dll` (the C and
   C++ runtime), `d3d11.dll` (1) and `d3d9.dll` (2), are native here and invisible, and so is every
   Direct3D and DXGI method, every call from one game DLL into another, and everything of
   `OriginSDK.dll`. `gbclient` (52 imports) and `mss64` (19) were never loaded in this run.
   A file opened through `fopen` is therefore not in the files table: `msvcr110` is not a caller.
2. **The excluded hot functions** (`tools/trace/capture/exclude-1.list`). Among the game's
   imports these are 18 names of kernel32 and one of rpcrt4: `GetLastError`, `SetLastError`,
   `TlsGetValue`, `TlsSetValue`, `FlsGetValue`, `FlsSetValue`, `EncodePointer`, `DecodePointer`,
   `EnterCriticalSection`, `LeaveCriticalSection`, `TryEnterCriticalSection`,
   `InterlockedPushEntrySList`, `InterlockedPopEntrySList`, the four SRW lock functions,
   `QueryPerformanceCounter`, `UuidCreate`. Their shares are known from `test1` (next section).
3. **Four functions Wine does not relay at all**: `GetCurrentThreadId`, `GetCurrentProcessId`,
   `GetCurrentThread`, `GetCurrentProcess`. Measured: not one call of them from any module of
   any process in `test1`'s 73,969,891 lines (`cut.py test1 census --from all --fn ...`), while
   our own run's log has 4,035 calls of the first from the same call sites.
4. **Anything of EA's or Steam's**: their modules were left off the callers; calls made by or into them were
   counted by the tools and never printed.
5. What a call wrote through a pointer (the bytes a read returned, a structure filled in): relay
   prints arguments on the way in and the return value only.

## The excluded functions' shares (the first test)

`cut.py test1 census --commit-safe`: 7,564,640 calls of 244 functions from the game's own modules
in the test's game process (the logging slowed the game, so these are shares, not rates):

| function | calls | share |
|---|---|---|
| `GetLastError` | 3,800,936 | 50.2% |
| `RtlInterlockedPopEntrySList` / `PushEntrySList` | 1,463,733 / 1,159,750 | 34.7% |
| `UuidCreate` | 289,618 | 3.8% |
| operator delete / new of Wine's `msvcrt` (from `D3DCOMPILER_46`) | 184,691 / 58,365 | 3.2% |
| `RtlAcquireSRWLockExclusive` / `Release` | 118,876 / 118,874 | 3.1% |
| `InitializeCriticalSectionAndSpinCount` | 92,234 | 1.2% |
| `QueryPerformanceCounter` | 23,319 | 0.3% |

`GetLastError` by caller in the same log, all modules: client 2,016,520, server 1,750,290,
filesystem_stdio 26,099. Half of everything the game asks Windows is "what was the last error".

## Phases

`cut.py mission1 phases --commit-safe` and, for input, `cut.py mission1 calls --from
engine --fn wndproc --grep 'msg=WM_LBUTTONDOWN|msg=WM_KEYDOWN,wp=0000001b'`. The step numbers are
the launcher-flow map's; the names of the phases are [R] (`docs/trace/phases.tsv` is this table for scripts).

| time | landmark (thread) |
|---|---|
| 0.000 | first line of the process; 0.716 the executable mapped; 0.798, 0.928, 0.967 EA's three modules loaded; 0.997 the run's only exception |
| 2.134 | the executable's first call (its C runtime); it loads `launcher.dll`, which brings `originsdk`, `tier0`, `vstdlib` (2.150 to 2.152) |
| 2.262 | the executable fetches `LauncherMain`; 2.263 steps 3 to 5 (`WSAStartup`, the mutex, `SetThreadExecutionState`, `SetCurrentDirectoryA`) |
| 2.264 | step 7: two job threads; 2.266 step 8 `rtech_game`; 2.268 step 9 the pak loader's thread |
| 2.294 | step 12 `filesystem_stdio`; 2.297 to 2.302 a pool of four threads |
| 2.320 | step 15 `CoInitialize`; 2.341 to 2.410 `bink2w64`, `engine`, `inputsystem`, `vphysics`, `materialsystem_dx11` (with `d3d11`, `dxgi`), `datacache`, `studiorender`, `vguimatsurface`, `vgui2`; 2.415 step 17 `localize` |
| 2.417 | step 19: thread `05d4` starts; 2.579 the translation layer's threads appear (the device exists) |
| 2.585 | step 21: `R2\GameInfo.txt`, 2.594 `vpk\enable.txt` |
| 2.686 | step 22: `XInputGetCapabilities` (where our own run ends today) |
| 2.857 | **the window**: `RegisterClassExW`, `CreateWindowExW` (class `Respawn001`), `ShowWindow` at 2.861, all on `05d4` |
| 4.702 | the main thread's first `PeekMessageW` (engine+0x1c590f); 4.706 to 5.563 `dsound`, `xaudio2_6`, `mileswin64`, `binkawin64`, `client`, `server` |
| 6.862 | start-up videos: `respawn.bik`, 12.500 `ea.bik`, 13.499 `legal_english.bik`; 8.632 `ui(11).dll`; 9.049 the first socket |
| 19.343 | `settings.cfg`, `profile.cfg`; 20.998 `savegame.sav` |
| 21.189 | `menu_act01.bik`: **title screen** [R]; 24.125 first raw input; clicks at 26.611 and 27.991: **main menu** [R] |
| 29.559 | third click and `sp_training_loadscreen.rpak` in the same millisecond: **the mission's load** |
| 45.580 | `sp_training.stbsp`; a click at 46.386; first key at 49.447: **play** from about 46 s [R] |
| 261.357 | Escape; clicks 263.124 to 267.103: **quit from the game's menu** |
| 267.239 | `WSACleanup`; 267.334 to 267.714 `server`, `client`, `binkawin64`, `mileswin64` unloaded; `DestroyWindow`; 267.793 `TerminateProcess(self, 0)` from materialsystem_dx11+0x110f8; 267.794 last line |

The second start (`start2`) has the same landmarks in the same order from the same call sites
(`docs/trace/start-sequence.md`); its census has 242 names in 63.9 s.

## Against the launcher-flow map (the trace wins)

1. **The window is not made by the main thread.** The class is registered, the window created
   and every one of its messages handled on thread `05d4`, the thread started at step 19 on which
   the Direct3D device is created. The loop that takes and dispatches messages (`PeekMessageW`,
   `TranslateMessage`, `DispatchMessageW` at materialsystem_dx11+0x877eb, +0x87839, +0x87844) runs
   on `05d4` too; the main thread (`04ec`) asks `PeekMessageW` from 4.702 on (engine+0x1c590f) and
   is answered 0 in every line `docs/trace/window.md` was cut from. Step 31's pump is therefore
   not where the messages are handled [R].
2. **The executable** (question 9 of the launcher-flow map, and the inference of an earlier run): after its C
   runtime it calls, in order, `GetModuleFileNameA`, `SetEnvironmentVariableA("PATH", <the
   install's bin\x64_retail first>)`, `LoadLibraryExA(launcher.dll, 0, 8)`, `LoadLibraryA(tier0.dll)`
   and `GetProcAddress(launcher, "LauncherMain")`, and nothing after that in the whole run. It
   never asks `GetProcAddress` for a tier0 export, and no game module calls
   `GetUserDefaultUILanguage` in either capture, which is the first thing tier0's
   `Tier0_DetectDefaultLanguage` does in our own run (tier0+0xfbaa). **The capture does not
   support that the executable calls that export, and speaks against it** [R: a call through a
   static import would not be relayed, but its `GetUserDefaultUILanguage` would be].
3. `LauncherMain` does not return in this run: the process ends in `TerminateProcess` called by
   the material system during shutdown. What the executable does with a return value stays open.
4. `bink2w64` is loaded with the engine (2.341), before anything of step 15's list.
5. Exceptions: one per run, `40010006`, inside Wine's `OutputDebugString` on a thread of EA's
   overlay; none from game code in 267.8 s (`docs/trace/exceptions.md`).

## The census against our runtime

`tools/trace/census_runtime.py` reads the committed census, `runtime/tools/imports.def`, the shim
files and the task rows, writes `docs/trace/census-runtime.tsv` (in order of first call, with the
phase) and prints what a start needs next. On this tree, of the 257 functions: **173 have a shim
(87.6% of the calls), 34 are task rows waiting (6.5%), 16 are imports with neither, 34 are not
imports at all** (reached through `GetProcAddress`: the C runtime's optional functions such as
`LCMapStringEx`, the XInput functions, or calls into `amd_ags_x64`, which Wine replaced). The
table is a snapshot: rerun the script after shims are merged.

    tools/trace/census_runtime.py --next 20 --imports     the next 20 imports a start calls that we lack
    tools/trace/census_runtime.py --phase 'step 22'       the lacking ones of one phase

## The first difference from our own run

`tools/trace/compare_run.py` walks the main thread of our own run's log beside the capture's
(`docs/trace/start-sequence.md` has the table). Of 19,680 reference calls in the window and 18,298
of ours, **17,364 pair up call site by call site in the same order**, to the known end at step 22.
In between: (a) each DLL's C runtime asks `GetProcAddress` for 28 optional functions and gets an
address in the original and 0 from us, so ours takes the older path (`TlsAlloc` for `FlsAlloc`,
`LCMapStringW` for `LCMapStringEx`...); `GetStdHandle` answers 0 there and a handle here; (b)
**at 2.296, the first difference that is not the C runtime's: the original builds a pool of four
threads and hands work to it (blocks of about 52 calls per hand-over on the main thread), ours
does that work with no pool**; (c) single answers: `LoadIconA` (a handle there, 0 here),
`GetTimeZoneInformation` (2 there, 0 here), one overlapped `ReadFile` (pending there, done here).

## How to cut a slice

`tools/trace/cut.py <capture> <subcommand> [options]` runs `tools/trace/relay.py` on the other
machine over `ssh` (the script travels on standard input; nothing is installed, written or
started there) and prints what it prints. The first use of a capture makes one pass (16 s for
`mission1`) for an index kept locally; after that `calls` and `census` with
`--t A:B` begin with a seek and take about a second (0.8 s measured), and a `multi` pass over the
whole log took 80 s with four collectors and 126 s with twelve.
One command at a time (a lock file makes a second one wait). Anything that will be committed
gets `--commit-safe`, and the finished file goes through `tools/trace/safe_scan.py`.

    tools/trace/cut.py mission1 calls --commit-safe --from engine --dll user32 --t 2.85:2.88
    tools/trace/cut.py mission1 census --commit-safe --dll ws2_32,wsock32 --by-module
    tools/trace/cut.py mission1 multi -- threads --commit-safe -- files --commit-safe -- exceptions

Subcommands: `calls` (the lines `time thread caller+rva function(args) = result`; `--from`,
`--dll`, `--fn`, `--grep`, `--tid`, `--t`, `--after`/`--until` marker calls, `--collapse`),
`census` (per function; `--key` a regex group of the arguments, `--pow2`/`--sum` an argument's
size class and total, `--by-module`), `threads`, `files` (`--bins`), `phases` (`--mark`),
`exceptions`, `multi` (several in one pass), `index`, and `cut.py <capture> text <file>` for the
translation layer's logs. `relay.py <subcommand> --help` lists the options; the self-test
(`tools/trace/selftest.py`, in `make test`) shows each on a made-up log.

## The slices (`docs/trace/`)

| file | for |
|---|---|
| `start-sequence.md` | the entry point and the run: the executable's calls, each step with the call that shows it, the second start, our own run against it |
| `window.md` | the window pieces: creation, functions in order, messages, the pump (short; `window-calls.md` has the calls) |
| `device.md` | what the relay log and the translation layer's logs say about Direct3D (the methods are not covered here) |
| `threads.md` | every thread, its creator, priorities, waits and wakes |
| `files-start.md`, `files-mission.md` | the files of each phase against the game-data lists |
| `memory.md` | `VirtualAlloc` and friends by module and size; the heap from the first test |
| `input-sound-net.md` | XInput, HID, sound and socket functions in order (short; `net-calls.md`) |
| `exceptions.md` | every exception dispatched |
| `census-mission1.tsv`, `census-runtime.tsv`, `phases.tsv` | the tables behind the above |

## Limits of the tools

- A return address becomes `module+rva` by the nearest module base below it (the load lines
  carry no size). A DWORD argument is printed as the whole register: its upper half is noise
  (`6fff00000003` is 3); the files table masks it, `calls` prints what the log has.
- After a seek, nesting (the indentation in `calls`) counts only calls opened inside the window.
- `files`: bytes are what was asked of `ReadFile`/`ReadFileEx`, not what came back.
- `threads`: a thread's creator is known only when `CreateThread` was called by a recorded
  module; handles are followed from creation to `CloseHandle`.
- `--commit-safe` lets a run of 24 or more letters, underscores and hyphens through (a symbol or
  a file name) and the name given to `GetProcAddress` or `LoadLibrary`; `--strict` does not.
