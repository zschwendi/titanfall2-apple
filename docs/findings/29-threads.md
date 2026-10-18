# 29: how the engine creates and uses threads, and whether it can be held to one

From the game's binaries (16 Source-engine modules plus 8 third-party
SDK DLLs). `Titanfall2.exe` is wrapped and was not looked at. All addresses are RVAs; every
DLL has image base `0x180000000`. Tags: **[V]** verified from code (disassembly, import and
export tables, `.pdata`), **[N]** inferred from names, strings or RTTI only, **[?]** not
determined.

## Headline

1. The engine has **two** job/thread systems, both reached through exports:
   - Respawn's **JT** job-thread system, inside `tier0.dll` (38 `JT_*`/`JTGuts_*` exports plus
     `ConfigureAndInitJobThreads`). Per-frame parallelism (server frame, snapshots, bones,
     particles, PVS, physics, pak loading) runs on it. 28 internal slots at most, plus four
     fixed external slots (30 to 33).
   - Source's **`CThreadPool`/`CJob`** pool in `vstdlib.dll` (`g_pThreadPool`,
     `CreateNewThreadPool`), used for the global pool and the async-filesystem IO pool.
2. Worker counts come from `-numworkerthreads`, `-numreservedcores` (tier0), `-threads`
   (vstdlib) and `-noasync` (filesystem), plus the CPU count from
   `GetLogicalProcessorInformation`. `host_thread_mode` (default 1) only threads the host frame
   when the JT system has at least one worker. All [V].
3. **Zero workers is reachable**: `-numworkerthreads 0` (or a shim reporting at most 2
   processors) for the JT system, `-threads 1` for the vstdlib pool, where `AddJob` then runs
   the job inline. The host-frame code handles zero JT workers explicitly; other job
   submitters were not audited (section 6, caveat 1). Neither setting removes the threads
   listed in point 4.
4. Four threads are created with no command-line or ConVar switch in front of them:
   RenderThread (inside materialsystem), the pak loader (rtech_game), `VPKMasterCacheThread`
   (filesystem) and a chat/net frame-loop thread (engine). The Miles `CSOM` thread sits
   behind the client's sound init. A headless bring-up has to stub or shim these (section 6).

## 1. Every place a thread is created [V]

Search method: all call sites of the `CreateThread` import (`call [IAT]`; the `jmp [IAT]` import
stubs at the end of `.text` were checked and are not called for any thread or sync API),
`_beginthreadex` (static CRT copies identified by shape, see below), and cross-module calls to
tier0's `CThread::Start` / `CreateSimpleThread`. No module imports or resolves
`CreateFiber`, `QueueUserWorkItem`, `CreateThreadpool*`, `CreateTimerQueueTimer`,
`RegisterWaitForSingleObject`, `timeSetEvent`, `CreateRemoteThread` or `NtCreateThread`. The
`CreateThreadpoolTimer`/`SetThreadpoolWait` strings in every module are the static CRT's
dynamic lookups. Scripts: `tools/scout/threads_scan.py`, `threads_createsites.py`.

**17 `CreateThread` call sites in 7 Source modules** (launcher, vstdlib, datacache,
studiorender, localize, vgui2, vguimatsurface, vphysics and server have none):

| Module | Function | Start routine | What it is |
|---|---|---|---|
| tier0 | 0x8210 | 0x9710 | **JT worker threads**, one per slot 2.., named `Worker %u`; stack size comes from the config struct (2 MB, set by `ConfigureAndInitJobThreads`) |
| tier0 | 0x1b5a0 (export `CThread::Start`) | 0x1b890 (export `CThread::ThreadProc`) | every `CThread` subclass: vstdlib `CJobThread`, engine `CQueuedPacketSender`, filesystem `CIODelayAlarmThread` |
| tier0 | 0x1bf10 (export `CreateSimpleThread`) | 0x1ba10 | callers: engine (Origin invite thread), filesystem (VPK cache thread), materialsystem (RenderThread) |
| tier0 | 0x1acc0 | 0x1ba10 | only reached from the TSList/TSQueue self-tests (`RunTSListTests`, `RunTSQueueTests`) |
| tier0 | 0xeae0, 0xf030 (exports `ClearHardwareBreakpoint`, `SetHardwareBreakpoint`) | 0xe6f0 | short-lived debug-register helper; no module imports either export |
| tier0 | 0x9aa0 | none | 12-instruction CreateThread wrapper; no call, lea or data reference to it |
| engine | 0x1f11e0 | 0x1f1240 | thread named `chatserver:Net_FrameLoop`, 80 KB stack, started once from the straight-line path of the host init function (0x1567e0) |
| engine | 0x526dc4 | 0x526fe8 | static-CRT **`_beginthreadex`** (allocates a 0x478-byte `_tiddata`, start routine is `_threadstartex`, `ExitThread` on failure). Single caller 0x26dd80, whose chain ends at the host-name resolve code (`init_resolve_thread() failed`) |
| filesystem_stdio | 0x8d9e8 | 0x8dc0c | second static-CRT `_beginthreadex`, single caller 0x79910 in the embedded lzham compressor (`lzham_win32_threading`); offline compression tooling, not a runtime thread [N] |
| inputsystem | 0xbf30 (2 sites) | 0xc230, 0xc340 | two reader threads on a device opened with `CreateFileA` and drained with `ReadFile`, started from the input-device init chain next to the XInput and `-nojoy` code [N] |
| inputsystem | 0xcad0 | none | unreferenced wrapper (same shape as tier0 0x9aa0) |
| materialsystem_dx11 | 0x4a70 | 0x47c0 | worker that registers JT slot 0x20; the neighbouring function 0x4b30 reads `stream_resource_thread` [N] |
| rtech_game | 0x1cb0 | 0x1640 | pak loader: semaphore plus 32-entry request free-list, registers JT slot 0x1e. Single caller 0xa660 (strings `Pak Patch`, `patch_master.rpak`), whose address is taken inside the export `Pak_SetLoadFuncs`, so it runs when that callback is invoked [N] |
| client | 0x57dce0 | 0x57dde0 | `CSOM` Miles async-file service thread, JT slot 0x1f, priority raised; called from the sound-system init (0x57c410) just before `MilesStartup` |
| client | 0x5806d0 | none | unreferenced wrapper |

Third-party SDKs (not Source code, not under our control): `mileswin64` 3 sites (a
`WASAPI Service Thread`, a WASAPI device thread, a generic RAD thread factory), `bink2w64` 1
(RAD thread factory), `auditionwin64` 1 (`Auditioning Thread`), `OriginSDK` 2
`_beginthreadex` (`Thread::Begin` exports) plus 1 `QueueUserAPC`. `GFSDK_*` and `amd_ags`
create none (D3D11 and the GPU driver create their own threads, invisible here).

Thread IDs are handed out by `AllocateThreadID`, which scans 128 slots (`MAX_THREADS_SUPPORTED`
is 0x80) [V].

## 2. The job and pool systems [V unless tagged]

### JT (tier0.dll)

38 exports, ordinals 78 to 115, plus `ConfigureAndInitJobThreads` (0x1a600). Their state is
in tier0 `.data`; per-thread state is one 0x40-byte record per slot. Key facts:

- `ConfigureAndInitJobThreads` is called once, unconditionally, from the launcher's main
  routine (0xd074). It builds a config struct (defaults: 2 external slots, reserved cores 0,
  max workers 28, stack 2 MB, a 12-entry table), reads the two switches in section 3, then
  calls the init routine 0x8210 (which also registers four `ExecuteCall` call-job types). It
  then calls `JT_SetThreadIdx(0)` for the calling (main) thread and sets each `ExecuteCall`
  type to `JT_SetJobTypeAffinity(type, 0x0FFFFFFC, 0x0FFFFFFF)` (workers run them; slots 0 and 1
  may only help when explicitly waiting).
- Slot map: 0 main thread, 1 RenderThread (`JT_SetThreadIdx(1)` in its start routine), 2..27
  workers, 30 pak loader (rtech), 31 `CSOM` (client), 32 materialsystem stream worker,
  33 `VPKMasterCacheThread` (each of those four calls `JT_SetThreadIdx` with that constant near
  the top of its start routine). The value 28 (0x1c) is written when a worker exits and by the JT
  shutdown routine (0x90e0); I read it as "not a JT thread" [N].
  The index lives in static TLS (read through `gs:[0x58]`); `JT_GetCurrentThread` has
  **5,577 call sites in server.dll** and many more in the others, so per-thread arrays indexed
  by it are everywhere.
- A worker is a loop: set index, run the optional start callback, call the scheduler (0x6e30)
  until stopped, otherwise sleep on a per-thread condition variable (`SleepConditionVariableSRW`).
  Waiting threads are expected to help: `JT_WaitForJobAndOnlyHelpWithJobTypes` (the most-called
  job-wait export: 69 sites in client, 36 in engine, 21 in server) and `JT_HelpWithJobTypes[OrSleep]`.
- `JT_SetJobTypeAffinity(type, A, B)` writes two 64-bit masks per thread (bit n = job type n).
  A = threads that run the type, B = threads that may help with it. Every registration whose
  masks I could decode uses A = B, except the four `ExecuteCall` types above.

Registered job types and affinities (`tools/scout/threads_jobtypes.py`; bit 0 main, bit 1
render slot, bits 2.. workers):

| Job type (name string) | Module | Run / help mask |
|---|---|---|
| `RunFrameServer` | engine | 0x0FFFFFFC (workers only) |
| `ProcessSnapshot_WT`, `Host_Init_DuringVideo`, `RunSaveDiskWrite` | engine | 0xFFFFFFFC (workers only) |
| `SaveEntity_MasterJob` | server | 0xFFFFFFFC (workers only) |
| `PrecacheTaskItem` | filesystem_stdio | 0x0FFFFFFC (workers only) |
| `CSOM` | client | 0x0FFFFFFC |
| `BuildWorldMeshBatches` | engine | 0x0FFFFFFE (render slot and workers) |
| `BuildWorldMeshBatchesMemcpy` | engine | 0x0FFFFFFF (everyone) |
| `PrecacheMTVF` | engine | 0x1 (main thread only) |

Not decoded (masks computed at run time or passed through variables): `PakDispatchLoad`
(rtech), `PhysicsMaster`, `PhysicsSmall*`, `Pvs*`, `GPRecordProcess`, `SetupBonesOnBaseAnimating`,
`BuildRenderableRenderLists`, particle jobs, `DeferredTrace`, `SaveEntity`.

JT use by module (call sites of any `JT*` import): client 327, server 5,699 (5,577 are
`JT_GetCurrentThread`), engine 157, materialsystem 56, rtech_game 43, filesystem 35,
vphysics 21, datacache 7; launcher, vstdlib, inputsystem, localize, studiorender, vgui2 and
vguimatsurface use none. `JT_NumWorkerThreads` has one consumer, the host threaded-mode
decision (section 3).

### CThreadPool / CJob (vstdlib.dll, plus tier0 `CThread`)

RTTI walk (`tools/scout/threads_rtti.py`; vtable addresses are RVAs):

| Class | Module | Vtable | Slots | Base |
|---|---|---|---|---|
| `CThread` | tier0 | 0x520b0 (also exported as `??_7CThread@@6B@`) | 3 | none |
| `CWorkerThread` | vstdlib | 0x5ba40 | 3 | `CThread` |
| `CJobThread` | vstdlib | 0x5be80 | 3 (slot 2 = 0x18280, 625 bytes, probably the run loop [N]) | `CWorkerThread` |
| `IThreadPool` | vstdlib | 0x5ba68 | 21 | `IRefCounted` |
| `CThreadPool` | vstdlib | 0x5bcc8 | 21 | `IThreadPool` |
| `CGlobalThreadPool` | vstdlib | 0x5bd98 | 21 (overrides `Start`, slot 4 = 0x18630) | `CThreadPool` |
| `CJob` | vstdlib | 0x5bb48 | 6 | `CRefCounted1<IRefCounted>` |
| `CFunctorJob` | vstdlib | 0x5bba0 | 6 | `CJob` |
| `CFileAsync{Read,Write,WriteFile,AppendFile,FileSize,DirectoryScan}Job` | filesystem_stdio | 0xbf0b8 to 0xbf340 | 8 | `CFileAsyncJob` then `CJob` |
| `CIODelayAlarmThread` | filesystem_stdio | 0xbeb38 | 3 | `CThread` |
| `CQueuedPacketSender` | engine | 0x60eff8 | 3 | `CThread` |
| `CMatQueuedRenderContext` | materialsystem_dx11 | 0x1d13a8 | 203 | `CMatRenderContextBase` |

`CThread` is the only class from which other Source code derives threads; no module derives
a thread class that is not in this table (RTTI covers every module). Exports that expose all
of this:

- tier0: the `CThread` class (ctor, `Start`, `Join`, `Resume`, `Suspend`, `Terminate`,
  `SetName`, `SetPriority`, `ThreadProc`, `Sleep`, `Yield`), `CreateSimpleThread`, `ThreadJoin`,
  `ThreadSetAffinity`, `ThreadSetPriority`, `ThreadSetDebugName`, `ThreadSleep`,
  `AllocateThreadID`, `DeclareCurrentThreadIsMainThread`,
  `DeclareCurrentThread[Not]ServerFrameThread` (engine calls them from 0x15a090/0x15a130),
  `ThreadInMainThread`, `ThreadInServerFrameThread`, `ThreadCouldDoServerWork`, the
  `CThread{Event,Mutex,FullMutex,Semaphore,RWLock,SpinRWLock,SyncObject}` classes,
  `RunTSListTests`, `RunTSQueueTests`, `GetCPUInformation`, and the JT exports.
- vstdlib: `CreateNewThreadPool` (0x19e40), `DestroyThreadPool` (0x19ea0),
  `RunThreadPoolTests` (0x19ec0), data export `g_pThreadPool` (0x73ce0, imported by engine,
  client, materialsystem, vguimatsurface), and eleven `Coroutine_*` exports. **No module
  imports a `Coroutine_*` function and no module references a fiber API**, so coroutines
  and fibers are unused (the Squirrel "threads" in client/server are VM coroutines).

## 3. How the worker count is chosen [V]

CPU count: tier0 helper 0x5220 calls `GetLogicalProcessorInformation`, keeps one bit per
core and returns **physical cores in the low dword and logical processors in the high dword**.

JT workers (tier0 0x1a600 and 0x8210):

```
N       = physical cores            (logical processors if physical <= 2)
avail   = N - min(N, reserved + 2)               reserved = -numreservedcores (default 0)
workers = min(avail, maxWorkers)                 maxWorkers = -numworkerthreads (default 28)
total   = min(workers + 2, 28)                   threads created: slots 2..total-1, so at most 26
```

`-numworkerthreads 0`, or a processor count of 1 or 2, creates **no worker thread**.
`JT_NumWorkerThreads()` returns the number actually created.

Global vstdlib pool (`g_pThreadPool`, name `GlobPool`): the engine host init (0x155ea0) calls
`Start` with `nThreads = -1`. `CGlobalThreadPool::Start` (0x18630) reads `-threads N` and uses
**N - 1** pool threads. Without it, `CThreadPool::Start` (0x18740) takes
the logical-processor field of `GetCPUInformation`, halves it if the HT flag is set, subtracts 2
and caps the result at 3. With `nThreads <= 0` no thread is created.
The `AddJob`-shaped method (vtable slot 15, 0x156a0) checks the pool's thread count (the
thread vector's count, at +0x240); when it is zero it calls the job-execute routine (0x162b0:
take the job lock, run the job's virtual body, signal completion) immediately, so **jobs run
inline on the submitting thread** [V for the behaviour, [N] for the method names].

IO pool (filesystem, name `IOJob`): created in 0x1fcb0 with `CreateNewThreadPool` unless
`-noasync` is on the command line; default thread count is the **logical processor count**
(capped at 116). The launcher also reads `-noasync` (0xd297). The ConVar `async_mode`
(default 0; "0 = async, 1 = synchronous") is read in five filesystem functions.

Command-line switches that touch threads (each read through `CommandLine()->FindParm`):

| Switch | Read in | Effect |
|---|---|---|
| `-numworkerthreads N` | tier0 0x1a600 | cap on JT workers |
| `-numreservedcores N` | tier0 0x1a600 | cores held back from workers |
| `-threads N` | vstdlib 0x18630 | global pool gets N-1 threads |
| `-noasync` | launcher 0xd074, filesystem 0x1fcb0 | no IO thread pool |
| `-NoQueuedPacketThread` | engine 0x21a790 | skips starting `CQueuedPacketSender` |
| `+host_thread_mode` | engine 0x1c1690 | engine appends `+host_thread_mode 0` itself when `+map` is `mp_model_viewer` |
| `-nosound` | engine 0x7720, client 0x57c410 | engine skips its sound init (0x7760) when present [V]; the client's sound init tests it only after a failed `MilesStartup`, to suppress the error. Whether the client's `CSOM` thread is skipped depends on whether the engine path above prevents that init from running [N] |
| `-noshaderapi` | engine, materialsystem 0x8d4f0, launcher | null shader API; no evidence it avoids RenderThread (section 6) [?] |
| `-low`, `-high` | launcher 0xd074 | process priority class only |
| `-swapcores` | engine 0x1567e0, client 0x35bc70 | name only [?] |

Threading ConVars (defaults decoded from the registration functions with
`tools/scout/threads_convars.py`; "read at" is the function that reads the value where I
found it):

| ConVar | Module | Default | Notes |
|---|---|---|---|
| `host_thread_mode` | engine | 1 | Values 0 to 2: off, on if multicore, forced. Engine reader: 0x1578e0 only (value 0 gives not threaded; 1 gives threaded iff `JT_NumWorkerThreads() != 0`; 2 forces threaded; also gated on the host state and a few other flags). The decision is then used by 0x1591d0, which registers `RunFrameServer`. server.dll and client.dll look the ConVar up by name and read it themselves (server: a late-precache check) |
| `host_server_thread_min_ticks` | engine | 1 | read at 0x1591d0 |
| `host_thread_join_fast` | engine | 0 | read at 0x1591d0 |
| `host_threaded_sound` | engine | 0 | no reader found [?] |
| `threadpool_affinity` | engine | 1 | read at 0x158a30 (after pool start) |
| `net_queued_packet_thread` | engine | 1 | read at 0x21b410, 0x21bd30, 0x21c32f |
| `net_threadedProcessPacket` | engine | 1 | read at 0x70250 (`ProcessSnapshot_WT` job) |
| `net_threadedEntityDeltas` | engine | 1 | read at 0x8fa20 |
| `net_async_sendto` | engine | 0 | |
| `sv_parallel_sendsnapshot` | engine | 1 | read at 0x118680 |
| `save_server_on_thread` | engine | 1 | read at 0x166230, 0x1667b0, 0x16afd8 |
| `sv_single_core_dedi` | engine, server | 0 | name only [?] |
| `async_mode` | filesystem | 0 | five readers |
| `stream_resource_thread` | materialsystem | 1 | read at 0x4b30 |
| `rt_worker` | materialsystem | 1 | read in the RenderThread loop (0x87f20): when set the render thread also helps with jobs |
| `rt_sync_message_pump` | materialsystem | 1 | |
| `mat_sync_rt`, `mat_syncGPU`, `mat_checkStalls` | materialsystem | 0 | debug sync |
| `studio_queue_mode` | studiorender | 1 | |
| `r_brush_queue_mode` | engine | 0 | |
| `vphysics_threadmode` | vphysics | 1 | |
| `physics_async_cl` / `physics_async_sv` | client / server | 1 / 0 | |
| `cl_threaded_bone_setup`, `cl_async_bone_setup`, `cl_parallel_clientside_animations`, `cl_parallelParticlePreDrawWork`, `r_threaded_particles`, `lerp_threaded`, `postdataupdate_threaded`, `leaf_threadedRecompute`, `cl_apply_ent_invalidate_threaded` | client | 1 each | job-system fan-out |
| `cl_apply_ent_invalidate_threaded_background`, `cl_updatedirty_async`, `cl_validateModelThreaded` | client | 0 | |
| `sv_threaded_post_process_ai`, `sv_threaded_post_process_players`, `teamSpot_threaded`, `save_thread_entities` | server | 1 each | |

There is **no `mat_queue_mode`** in this build (no such string in any module); the equivalents
are `rt_worker`, `rt_sync_message_pump`, `studio_queue_mode` and `r_brush_queue_mode`.

## 4. Synchronisation primitives [V]

Call sites of the import (direct `call [IAT]`), from `tools/scout/threads_scan.py` and
`threads_report.py`. The static MSVC 2012 CRT is linked into every module, so the totals
include its copies (for example, 369 `InitializeCriticalSectionAndSpinCount` sites across the Source modules).
A module can import an API and never call it (vstdlib imports the four `Tls*` functions and
`CreateSemaphoreW` with zero references, checked with a full disassembly).

| Module | Crit sec | SRW | Cond var | Event | Semaphore | Mutex | Wait* | Sleep | TLS API | SList |
|---|---|---|---|---|---|---|---|---|---|---|
| tier0 | 187 | 15 | 7 | 19 | 7 | 2 | 19 | 16 | 22 | 78 |
| vstdlib | 211 | 0 | 0 | 0 | 0 | 0 | 4 | 1 | 0 | 28 |
| launcher | 29 | 0 | 0 | 0 | 0 | 3 | 2 | 0 | 0 | 0 |
| engine | 703 | 208 | 0 | 1 | 0 | 0 | 6 | 7 | 0 | 73 |
| filesystem_stdio | 320 | 33 | 15 | 0 | 12 | 0 | 5 | 2 | 0 | 52 |
| datacache | 99 | 0 | 0 | 10 | 0 | 0 | 1 | 0 | 0 | 0 |
| inputsystem | 89 | 22 | 0 | 1 | 0 | 0 | 4 | 3 | 0 | 0 |
| localize | 29 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| materialsystem_dx11 | 76 | 116 | 9 | 0 | 0 | 0 | 2 | 3 | 0 | 13 |
| studiorender | 21 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| vgui2 | 40 | 0 | 0 | 0 | 0 | 0 | 1 | 0 | 0 | 0 |
| vguimatsurface | 242 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 13 |
| vphysics | 418 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 27 |
| rtech_game | 21 | 43 | 4 | 0 | 4 | 0 | 2 | 0 | 0 | 0 |
| client | 482 | 44 | 0 | 15 | 0 | 0 | 6 | 9 | 0 | 77 |
| server | 404 | 44 | 0 | 10 | 0 | 0 | 1 | 3 | 0 | 73 |
| **Source total** | **3,371** | **525** | **35** | **56** | **23** | **5** | **53** | **44** | **22** | **434** |
| third-party total (8 DLLs) | 1,595 | 0 | 0 | 9 | 51 | 277 | 265 | 60 | 79 | 0 |

Columns: critical sections = Initialize/Enter/Leave/TryEnter/Delete; SRW = Init/Acquire/Release
exclusive and shared; Wait* = `WaitForSingleObject` (48), `WaitForSingleObjectEx` (2),
`WaitForMultipleObjects` (2), `MsgWaitForMultipleObjects` (1);
SList = `InterlockedPush/Pop/FlushEntrySList`, `QueryDepthSList`, `InitializeSListHead`. **Fibers: 0.**
Other counts in Source modules: `Sleep` 40 and `SleepEx` 4; `SetThreadPriority` 10 (tier0 9);
`SetThreadAffinityMask` 7, `SetProcessAffinityMask` 4, `GetProcessAffinityMask` 10 (all tier0 or
vstdlib); `GetLogicalProcessorInformation` 32 (2 per module: every module carries its own copy of the
same processor-count helper, with identical error strings; tier0's copy is 0x5220);
`GetSystemInfo` 14; `SuspendThread` 1, `ResumeThread` 2, `TerminateThread` 1, `ExitThread` 4,
`OpenThread` 2, `Get/SetThreadContext` 1 each (all `CThread` or the hardware-breakpoint helper);
`TlsAlloc` 4 (tier0 only). Thread-local data is otherwise static TLS read through `gs:[0x58]`.

Atomics and ordering (`tools/scout/threads_atomics.py`, a linear sweep over `.pdata`
functions of the 16 Source modules): **1,754 lock-prefixed instructions** (`lock xadd` 610,
`lock dec` 512, `lock cmpxchg` 412, `lock inc` 148, `lock or` 54, `lock cmpxchg16b` 7,
`lock add` 6, `lock and` 5), 318 `xchg` with a memory operand, 37 `pause`, and **a single fence
instruction in the whole Source code base** (one in materialsystem). The per-module counts
match the brief's tier0/engine/client/server figures to within 4 (201, 256, 237, 210 vs
203, 256, 239, 214), the difference being code outside `.pdata` ranges. Consequences: shared
state is published with plain `mov` and relies on x86 ordering (the hardware route settled in
an earlier measurement fits); the 7 `cmpxchg16b` and the 434 SList call sites need a 16-byte-aligned 128-bit CAS
(`CASP`/`LDXP`) on arm64; the JT scheduler packs a tag counter into the high half of
64-bit words and compare-exchanges them.

## 5. Named long-lived threads

| Thread | Module | How it is created | Evidence |
|---|---|---|---|
| Main thread, JT slot 0 | process main thread | `JT_SetThreadIdx(0)` from `ConfigureAndInitJobThreads`; `JT_SetThreadIdx(0)` | [V] |
| `RenderThread`, JT slot 1 | materialsystem_dx11 | a `CMaterialSystem` method (vtable 0x1d2ae8, slot 1, 0x5b1c0; takes a factory pointer, so probably `Connect` [N]) calls 0x87000, which calls `CreateSimpleThread` with start routine 0x87f20 | [V] code path; the only preconditions seen are a non-null factory argument and a non-null global |
| `Worker N` job threads, slots 2..27 | tier0 | `ConfigureAndInitJobThreads` | [V] |
| Global pool threads (`GlobPool0`..) | vstdlib | `CGlobalThreadPool::Start` via engine host init | [V] code; name composed with `%s%d` [N] |
| IO pool threads (`IOJob0`..) | vstdlib, driven by filesystem_stdio | `CreateNewThreadPool` in 0x1fcb0 | [V] |
| `VPKMasterCacheThread`, JT slot 33 | filesystem_stdio | `CreateSimpleThread` at 0x62870, from an init function (0x134e0) | [V] creation; purpose [N] |
| `QueuedPacketSender` | engine | `CQueuedPacketSender::Start` (vtable 0x60eff8 slot 1, 0x21f510) calls `CThread::Start`, raises priority, names the thread; invoked through 0x21f440 from 0x21a790 unless `-NoQueuedPacketThread` | [V] |
| `chatserver:Net_FrameLoop` | engine | 0x1f11e0, once, from host init | [V] creation; role [N] |
| host-name resolve thread | engine | static-CRT `_beginthreadex` at 0x526dc4 | [V] |
| Pak loader, JT slot 30 | rtech_game | 0x1cb0 | [V] |
| `CSOM` (Miles async file service), JT slot 31 | client | 0x57dce0 | [V] creation; role [N] |
| Miles WASAPI and mixer threads, Bink decode threads | mileswin64, bink2w64 | SDK-internal | [N] |
| stream-resource worker, JT slot 32 | materialsystem_dx11 | 0x4a70 | [V] creation; role [N] (adjacent `stream_resource_thread`) |
| input device readers (2) | inputsystem | 0xbf30 | [V] creation; role [N] |
| Origin threads: `OriginInviteThread`, `OriginProgressiveInstallationThreadMain` | engine | `CreateSimpleThread` at 0x184600 (guarded by two flags) and in the 0x6ae0 region | [V] creation; online only |
| `CIODelayAlarmThread` | filesystem_stdio | `CThread` subclass (vtable 0xbeb38), for `async_simulate_delay` | [N] |
| audio authoring thread `Auditioning Thread` | auditionwin64 | | [N], tools only |

There is no separate audio-mixing thread in the Source code: sound runs in Miles' own threads
plus the `CSOM` service thread, and `host_threaded_sound` defaults to 0.

## 6. Can it be held to one thread? Bottom line

**Settings that remove every thread controlled by a switch** [V: the code path behind each
one was read; the effect of `-nosound` on the client's threads is [N]]:

```
-numworkerthreads 0     (tier0 JT: no Worker N threads)  -- or shim processors <= 2
-numreservedcores 0     (default, shown for completeness)
-threads 1              (vstdlib global pool: 0 threads, AddJob runs inline)
-noasync +async_mode 1  (no IO pool; synchronous async filesystem)
-NoQueuedPacketThread +net_queued_packet_thread 0
+host_thread_mode 0     (no threaded host frame; redundant once workers = 0)
-nosound                (engine skips its sound init)
```

Belt and braces, to keep the job system from fanning work out at all (all default 1 unless
noted, all Source ConVars decoded in section 3): `net_threadedProcessPacket 0`,
`net_threadedEntityDeltas 0`, `sv_parallel_sendsnapshot 0`, `save_server_on_thread 0`,
`sv_threaded_post_process_ai 0`, `sv_threaded_post_process_players 0`, `teamSpot_threaded 0`,
`save_thread_entities 0`, `physics_async_cl 0`, `vphysics_threadmode 0`, `r_threaded_particles 0`,
`cl_threaded_bone_setup 0`, `cl_async_bone_setup 0`, `cl_parallel_clientside_animations 0`,
`cl_parallelParticlePreDrawWork 0`, `lerp_threaded 0`, `postdataupdate_threaded 0`,
`leaf_threadedRecompute 0`, `cl_apply_ent_invalidate_threaded 0`, `studio_queue_mode 0`,
`rt_worker 0`, `stream_resource_thread 0`. Which of these a headless map load actually reaches
is [N]; they are listed because each is a registered switch for a code path that submits work to
the JT system or to a thread.

**What that leaves.** Threads created with no switch in front of them, so a bring-up has to
shim, stub or tolerate them:

| Thread | Why it cannot be switched off | Cheapest handling |
|---|---|---|
| RenderThread (+ stream worker) | created inside a `CMaterialSystem` method (probably `Connect`) and the shader-set init [V]; no switch seen. `-noshaderapi` sets a null-device flag in `CShaderSystem` (0x8d4f0) but I found nothing that skips thread creation [?] | do not load `materialsystem_dx11.dll`; provide a native stub with the same interfaces, so neither thread exists |
| pak loader (rtech_game) | created by 0xa660, a callback registered through `Pak_SetLoadFuncs`; no switch in the creation path [V], when it is invoked [N] | keep it as a real thread (it blocks on a semaphore), or run its start routine cooperatively |
| `VPKMasterCacheThread` (filesystem) | created at filesystem init [V] | keep as a real thread |
| `chatserver:Net_FrameLoop` (engine) | started unconditionally from host init [V]; its first act is a flag test that can end it at once (flag not traced [?]) | keep, or make `CreateThread` for start routine 0x1f1240 a no-op |
| `CSOM` and Miles threads (client) | sound-system init [V that it creates the thread; the guard that skips sound init on the client side is [N]] | `-nosound`, or stub `mileswin64.dll` |
| host-name resolve (engine), Origin threads, input device readers | demand-driven or online/device only | not started on a plain map load [N] |

**Smallest thread set to load a map with no rendering.**
Main thread, plus (with the settings above, and materialsystem and Miles stubbed) most likely
three background threads whose creation code has no switch: pak loader,
`VPKMasterCacheThread`, `chatserver:Net_FrameLoop` (that they are reached on a headless map
load is [N]; I did not trace what calls their creators). If so, they are idle or blocked on a
semaphore, condition variable or event most of the time. So the answer to "one thread" is:
**one thread doing real work, up to three parked helpers**, unless the runtime also intercepts
those three `CreateThread` calls. Everything else in the engine's threading
(workers, pools, render, packet sender, server-frame thread) can be turned off by the
switches or by not loading the module.

**Caveats that decide whether zero workers is safe [?]:**

1. Seven registered job types are *worker-only* (their run mask excludes threads 0 and 1:
   `RunFrameServer`, `ProcessSnapshot_WT`, `Host_Init_DuringVideo`, `RunSaveDiskWrite`,
   `SaveEntity_MasterJob`, `PrecacheTaskItem`, `CSOM`). With no worker, a job of such a type that
   is added and then waited for by the main thread has no thread allowed to run it, and I did not
   audit every submitter for an inline fallback. The host-frame path does have one (it checks
   `JT_NumWorkerThreads()` and `host_thread_mode`). If a map load stalls waiting on a job, run with
   one worker (`-numworkerthreads 1`) before suspecting anything else. A single worker is
   the practical minimum for a safe first boot; `-numworkerthreads 0` is the stricter experiment.
2. `Titanfall2.exe` (wrapped) was not analysed; any thread it starts before `launcher.dll` is
   missing from this list.
3. The meaning of the second affinity mask (A = run, B = help) is read from
   `JT_SetJobTypeAffinity` and the scheduler's mask expression; I did not run it.
4. Conditions inside third-party SDKs (Miles, Bink, OriginSDK) and the D3D11 runtime were not
   examined beyond the creation sites.

## 7. What the runtime must provide for threads

From the counts above, the minimum set: `CreateThread`/`ExitThread` with a start-routine
trampoline that turns on the per-thread x86 mode, static-TLS block per thread behind
`gs:[0x58]` and the TEB fields, critical sections (3,371
sites), SRW locks and condition variables (560), events, semaphores and mutexes (84 in Source
code), `WaitForSingleObject[Ex]`/`WaitForMultipleObjects`, `Sleep`/`SleepEx`, the SList family
with a 128-bit CAS (434 sites), `SetThreadPriority` (10 sites), and no-op or best-effort
`SetThreadAffinityMask`/`SetProcessAffinityMask`/`GetProcessAffinityMask`. The processor-count
call (`GetLogicalProcessorInformation`, tier0 0x5220) is the lever that sets the worker count;
a shim that returns the real core layout gives 9 workers on the 11-core M3 Pro (no SMT, so
N = 11 and workers = 11 - 2) by the formula in section 3, capped at 26 in any case.

## Reproducing

All scripts are read-only on the game files and live in `tools/scout/`; run from the repo root
with `.venv/bin/python`. Pick any scratch directory for `CACHE`.

```
tools/scout/threads_scan.py CACHE              # import call sites per module (4 processes)
tools/scout/threads_createsites.py CACHE       # CreateThread sites: args, strings, imports
tools/scout/threads_report.py CACHE            # section 4 tables
tools/scout/threads_rtti.py CACHE REGEX        # RTTI classes and vtables
tools/scout/threads_xmod.py REGEX [-v]         # who imports tier0/vstdlib thread exports
tools/scout/threads_strings.py REGEX [module]  # strings with the functions that load them
tools/scout/threads_convars.py MODULE [--reads] NAME...   # ConVar defaults and readers
tools/scout/threads_jobtypes.py                # JT job types and affinity masks
tools/scout/threads_atomics.py                 # lock-prefix, xchg, fence, pause counts
tools/scout/threads_disasm.py / _chain.py / _callers.py / _ptrrefs.py / _fstrings.py / _argsat.py
                                               # terminal helpers used to read functions
```

`threads_lib.py` is the shared module (PE import slots, `.pdata` function map, byte-level
reference scans). The reference scans are raw byte patterns, so a hit could in principle be
inside another instruction; every address quoted above was confirmed by disassembling the
function.
