/* `gate3 run`: our own entry point (docs/handoff/gate3-entry.md).  In place of the original's
 * executable, which is never read or run, this starts the runtime with a game command line,
 * loads tier0.dll and launcher.dll and calls the launcher's exported main function; the
 * launcher then loads and connects everything else itself.
 *
 *     gate3 run                    with the default command line (run_default_args, below)
 *     gate3 run -- <arguments>     with exactly these game arguments (the runtime supplies the
 *                                  program path itself: rt/init.h)
 *     gate3 run --in-process [-- <arguments>]   the same run in the driver's own process
 *     gate3 summary <log> [<report>]   the summary of an earlier run, from its log
 *
 * The run happens in a child process whose descriptor 2 goes to <out>.stderr (the fatal
 * report, when RT_LOG_FILE is not in force) and whose runtime log goes to <out>.log; <out> is build/gate3/run unless GATE3_RUN_OUT
 * names another prefix.  THE CHILD IS THIS PROGRAM STARTED AGAIN (posix_spawn of the driver's
 * own executable with `run --in-process`), not a fork of the driver: in the signed app bundle
 * the kernel grants the x86 memory-ordering mode to a program it has just started and refuses
 * it to a process that was forked and did not exec (measured by the orchestrator, issue 42;
 * docs/handoff/rt-win-queue.md).  `gate3 run --in-process` by hand is that child without the
 * parent: the time limit holds, but a run that ends in a stop ends the process, so its summary
 * is `gate3 summary <log>` afterwards; a run whose LauncherMain returns prints its summary.
 *
 * THE LOG LEVEL: RT_LOG is all:info unless the caller sets it (decision D of gate3-run-7).  It
 * was all:debug until a run reached the client's and the server's start: from there on the
 * debug level writes 42 million Windows calls, 6.5 GB for one 19-second run
 * (docs/handoff/gate3-run-6.md, section 6), on a disk that is at its floor.  The run logs which
 * level it had and the summary prints it ("log level:").  What the info level leaves out: the
 * `trace` channel's Call and Ret lines, and with them the summary's per-thread call counts,
 * its list of files and registry keys, "who asked" for a module and steps 1 to 5 and 19, 24 of
 * finding 37 (the probes' steps are there at any level).  For those, ask for the channel:
 * RT_LOG=all:info,trace:debug for a run that ends BEFORE the client starts; never past it.
 *
 * THE NETWORK: every end of a run writes one line, "net: host sockets: ..." (rt/hostsock.h),
 * which the summary prints under NETWORK: how many host sockets were made for the guest
 * (none: the runtime has no host socket) and what socket descriptors the whole process had.
 *
 * HOW THE RUN'S PROCESS ENDS, and what the driver's own exit status is.  Every run ends at a
 * stop today, and a stop is expected: so the run asks for the quiet stop (gate2/modes.h, THE
 * QUIET STOP; on unless GATE_QUIET_STOP=0), with `--in-process` by hand too.  The child's end:
 *     exit status 134   a stop: a fatal report, printed whole, then abort() ending in
 *                       _exit(128 + SIGABRT) and no crash report of macOS.  (Also what a
 *                       guest that called ExitProcess(134) leaves; the summary's "the guest
 *                       ended the process" and THE STOP say which.)
 *     SIGABRT           a report the runtime's signal handler wrote (a host fault, in lifted
 *                       code or in ours; a host library's abort()), which the switch leaves
 *                       loud; or any report with GATE_QUIET_STOP=0
 *     SIGALRM           the time limit;  77  GATE3_PRESENTS;  0 to 255  the guest's ExitProcess
 * The line "gate3 run: ended ..." says which, and names the switch when it was the quiet stop.
 * THE DRIVER'S OWN STATUS DOES NOT DEPEND ON THIS: 0 when LauncherMain returned or the run
 * was ended by GATE3_PRESENTS, 1 otherwise, 2 for a usage error, as before the switch.
 *
 * GATE3_CONSOLE=<seconds>:<command> hands one console command to the engine's command buffer, <seconds>
 * after the main menu first opened (the comment above run_console_tick).  Synthetic; a person's run leaves it unset.
 *
 * GATE3_FRAMES=<path> writes one line per Present to that file: the milliseconds since the run's start, a space,
 * 1 for a frame of the mission's world and 0 for any other Present (tools/perf/frames.py reads it).  Unset: no file.
 *
 * GATE3_PEEK=<item>[,<item>...] logs guest memory inside the first Present and inside the one
 * that ends the run (with GATE3_PRESENTS), and at the entry and the return of every hit of a
 * GATE3_PROBE probe, for reading a variable of the game that decides
 * something (which one, and from which instruction, is the reader's business: the driver prints
 * numbers).  An item is `module+0xRVA/N` (the N = 1, 2, 4 or 8 bytes at that address of the
 * loaded module) or `*module+0xRVA+0xOFF/N` (the N bytes at OFF from the pointer stored there).
 *
 * STAND-INS FOR EA'S SDK.  The run does things the original does not, each marked in the
 * code, logged, and printed under "STAND-INS" in every summary with the number of answers it
 * gave: (1) our entry point sets tier0's default language (run_child;
 * GATE3_NO_DEFAULT_LANGUAGE=1 leaves it out); and the imports of originsdk.dll in the list
 * run_standins below are answered by runtime/standins/originsdk/ (rt/standin.h), each exactly
 * as posted on issue 32 before it was written, each with a switch that leaves it out so that
 * the run stops at that call as it did before:
 *     (2) OriginIsProgressiveInstallationAvailableSync   GATE3_NO_STANDIN_PROGRESSIVE_INSTALL=1
 *     (3) OriginGetSettingSync, setting 1; and, for the placeholder user, setting 2 from the
 *         engine's call at engine+0x183ac2 (an empty text) GATE3_NO_STANDIN_SETTING_ENVIRONMENT=1
 *     (4) OriginRegisterEventCallback, the engine's own callback (engine+0x182130) and third
 *         argument 0 only; never called back              GATE3_NO_STANDIN_EVENT_CALLBACKS=1
 *     (5) OriginUpdate, the engine's per-frame call at engine+0x18490f only: result 0
 *                                                         GATE3_NO_STANDIN_UPDATE=1
 *     (6) OriginGetDefaultUser, the engine's per-frame call at engine+0x1849da only: result
 *         0, "nobody is signed in"; no user is named, no id is supplied
 *                                                         GATE3_NO_STANDIN_DEFAULT_USER=1
 *     (7) OriginGetProfile, the engine's one request at engine+0x183c91, FOR THE PLACEHOLDER
 *         USER ONLY: accepted; the made-up profile is handed to the engine's callback once,
 *         from inside stand-in 5's next answer        GATE3_NO_STANDIN_PROFILE=1
 *     (8) OriginRequestAuthCode, the engine's request at engine+0x18437b, the placeholder only:
 *         accepted, never completes, no code exists   GATE3_NO_STANDIN_AUTH_CODE=1
 *     (9) OriginQueryEntitlements, the engine's query at engine+0x183fe2, the placeholder only:
 *         accepted, never completes, no entitlement    GATE3_NO_STANDIN_ENTITLEMENTS=1
 *     (10) OriginQueryBlockedUsers, the engine's query at engine+0x183cce, the placeholder only:
 *         accepted, the handle written as 0, never completes   GATE3_NO_STANDIN_BLOCKED_USERS=1
 *     (11) OriginQueryFriends, the engine's query at engine+0x183cf7, the placeholder only:
 *         accepted, the handle written as 0, never completes   GATE3_NO_STANDIN_FRIENDS=1
 *     (13) OriginSetPresence, the engine's call at engine+0x184bad, the placeholder only:
 *         accepted; nothing is done with the texts     GATE3_NO_STANDIN_PRESENCE=1
 *     (12) OriginCheckOnline, the engine's call at engine+0x183a39, the placeholder only:
 *         result 0, the byte left 0: "not online"      GATE3_NO_STANDIN_CHECK_ONLINE=1
 *     (14) OriginGetDefaultPersona, the engine's call at engine+0x18483a, the placeholder only:
 *         the made-up persona 1, nothing written        GATE3_NO_STANDIN_PERSONA=1
 *     (15) OriginGrantAchievement, the engine's call at engine+0x18487b, the placeholder only:
 *         result 0, accepted; nothing is done with it   GATE3_NO_STANDIN_ACHIEVEMENT=1
 *     (16) OriginShutdown, the engine's call at engine+0x18471b, BOTH users (no id, no data):
 *         result 0, "no error"; nothing is done at all  GATE3_NO_STANDIN_SHUTDOWN=1
 * THE USER.  GATE3_USER selects whom the stand-ins answer for (rt/standin.h, THE USER):
 *     GATE3_USER unset or `placeholder`
 *                                     THE DEFAULT since 11 October 2026, 19:00, decided by what
 *                                     the run needs to reach the mission: the game's `map`
 *                                     command does nothing while nobody is signed in
 *                                     (docs/handoff/gate3-run-10.md, 3.3) and starts the
 *                                     mission's load with this user (gate3-run-11.md).  The
 *                                     local, made-up user posted on issue 32 (comments
 *                                     6114036238, 6114239628, 6114438848) after the owner's
 *                                     decision of 11 October: stand-in 6 answers the fixed
 *                                     number 1 and the stand-ins from 7 on are handed over.
 *     GATE3_USER=nobody               stand-in 6 answers 0, "nobody is signed in"; the stand-ins
 *                                     from 7 on are not in the run, except stand-in 16 (both
 *                                     users); the run's end is the loading screen after `map`.
 * Any other value ends the run before it starts.  The first line under STAND-INS of every
 * summary names the user.
 * Stand-ins 5 and 6 are called every frame: each logs its first answer and counts the rest, and every
 * end of a run writes one line "standins: counted: ..." with the counts, which the summary
 * prints under STAND-INS.
 * EVERY OTHER IMPORT OF originsdk.dll HALTS THE RUN, AND SO DOES A LOOK-UP BY NAME THAT FINDS
 * NO STAND-IN (GetProcAddress never answers NULL for that module); run_check_origin ends a run
 * that would start otherwise, right after rt_init and before any module of the game is loaded.  With the user "nobody" no stand-in supplies an identity, a user or persona id, an entitlement,
 * an authorisation code or anything of an account; with the placeholder stand-in 6 supplies a made-up id that is nobody's.
 *
 * GATE3_MEASURE_ONLY_NOT_A_WAY_TO_RUN_SETTING1_LEFT_EMPTY=1 IS A MEASUREMENT, NOT A WAY TO RUN:
 * stand-in 3 then answers result 0 and writes nothing (rt_standin3_leave_buffer), which is not
 * the posted answer.  It was asked for once (brief of gate3-run-7, section 2: where does such
 * a run stop, and what differs before that); the summary of such a run says so under STAND-INS.
 *
 * GATE3_INPUT_SCRIPT=<file> PLAYS SCRIPTED MOUSE AND KEY INPUT INTO THE RUN (rt/input.h has the
 * format, the anchors and what is posted; unit rt-input-script).  Off unless set.  With it the
 * runtime has a mouse and a keyboard, the driver's watch on the swap chain's Present feeds the
 * script's `present` anchors, every end of the run writes one line "input: script ..." and the
 * summary prints it under INPUT: the events delivered and not yet delivered.
 *
 * RT_HOST_WINDOW=appkit|appkit-hidden gives the game's window a macOS host window (rt/window.h); appkit-hidden is
 * never shown (tests); unset or none: no host window.  With it the first thread runs the host's event loop, so it
 * cannot also run the guest (GATE3_GUEST_ON_FIRST_THREAD=1 with it is a usage error).
 * GATE3_GUEST_ON_FIRST_THREAD=1 runs the guest on the process's first thread (the old way); unset or 0, the
 * first thread parks and the guest runs on a host thread of its own, so a real window can have the first thread
 * (AppKit).  The run's log says which one it had: "calling LauncherMain ... host-first-thread=1|0".
 *
 * THE STATE: WHERE A RUN STARTS AND WHAT IT LEAVES (gate3/state.h has the rules; unit
 * gate3-run-13; the player's state, unit gate3-player-state).  Every run works on a fresh copy
 * of a named start, in <out>.state, which the driver makes new before the run:
 * GATE3_START=new (the default: an empty directory, the game's first start),
 * GATE3_START=save:<dir> (a state that a run kept) or GATE3_START=player:<dir> (a player's
 * state, carried from run to run; the game's first start when <dir> does not exist yet).
 * GATE3_STATE is not the caller's to set for a run any more.  GATE3_KEEP_STATE=<dir> keeps the
 * run's state there, and only when the run ENDED CLEANLY: LauncherMain returned, or the driver
 * ended the run at GATE3_PRESENTS at a moment when no file under the state directory was open
 * for writing (the end is put off, Present by Present, for up to RUN_END_WAIT_PRESENTS while
 * one is; then the run is ended all the same and is not clean).  A run that ended at a stop,
 * at a fault or at the time limit keeps nothing; its working copy stays at <out>.state for
 * inspection, is marked as one, and is never the start of a run.
 * A PLAYER'S END IS SAFE when no file under the state directory was open for writing at the
 * end and the end was a clean end as above, the host window's close (exit status
 * RUN_EXIT_CLOSED) or a stop of the runtime; a time limit, a signal or any other end is not
 * safe.  Then the run's working copy REPLACES <dir> (the state before it becomes <dir>.prev);
 * otherwise <dir> stays as it was.  Every run's end writes a line "state: files under the
 * state directory open for writing at this end: N" to its log (run_end_lines), and the parent
 * reads that line to decide.  The run logs its start, the summary prints it as START and the
 * driver's last line, STATE, says what became of the copy.
 *
 * THE READ-BACK: `gate3 run` ANSWERS THE DEVICE'S ONE KEPT STOP WITH ZEROS, AN INVENTED ANSWER
 * (the planner's decision for unit gate3-run-13; docs/handoff/gate3-run-12.md, section 2).  The
 * do-nothing device stops at a read mapping of contents only a renderer could have produced
 * (rt/nulldev.h, RT_D3D_READBACK; the mission reads a 32 by 32 texture back every frame).  The
 * device's own default stays `stop`; for a run the driver sets RT_D3D_READBACK=zero unless
 * the caller set that variable, and GATE3_READBACK=stop gives the stop back (`zero` is the
 * default; anything else is a usage error).  The run logs which it has, the summary prints it
 * as READ-BACK in its first lines, and the device's ledger counts every such answer (THE
 * DEVICE'S LEDGER in the summary).
 *
 * THE MENU MARK.  Two natives of the game's UI script are probed in every run: OpenMenu
 * (client+0x3b3ee0) and CloseMenu (client+0x3b3c30), addresses from the client's own
 * registration function (docs/handoff/gate3-run-10.md, section 1).  Each call is one `Script`
 * line as for any `&` probe, and one line "gate3 run: menu: ..." with the time and the Present
 * the run is at; when the menu opened is `menu_MainMenu` the driver also reports the mark
 * `main-menu` to the input script (rt/input.h, `anchor mark main-menu`): a scripted click is
 * anchored on the menu itself and not on a Present count, which missed the menu when the
 * start was slow (gate3-run-12, entry 2).
 *
 * WHAT A RUN REACHED, AND THE RUN OVER TIME.  The driver's watch on the swap chain's Present
 * is there in every run.  A Present before which the device was asked for a compute dispatch
 * is counted as a frame of the mission's world (the menu, the videos and the loading screens
 * dispatch nothing; the mission's frames four times each: gate3-run-12, entry 5), the first
 * is logged and reported to the input script as the mark `mission-frame` (for input that is
 * to start with the mission: `anchor mark mission-frame`), and every end writes "frames: ...".  Every GATE3_REPORT_EVERY seconds (10; 0:
 * never) one line "gate3 run: report: ..." has the Presents, the mission's frames, the device's
 * calls and draws per Present since the report before, the process's footprint and what the
 * device holds.  The summary prints REACHED (the main menu, the menu left, frames of the
 * mission with their count) and the reports under OVER TIME.
 *
 * GATE3_COUNTS_AT=<N>[,<N>...] (up to 16) WRITES THE DEVICE'S COUNT TABLE INTO THE LOG AT THOSE
 * Presents and lets the run go on: one line "gate3 run: count table at present N ..." with the
 * time and the number of frames the engine's main loop has made (stand-in 5 answers once in
 * each), then the table, every line with "snapshot N: " in front so that the summary's own
 * table is not mixed with it.  gate3/count_delta.py prints the difference of two snapshots
 * per Present: what one frame is made of, method by method, in a stretch of the run.
 *
 * GATE3_PROBE ITEMS `module+0xRVA&Name` AND `module+0xRVA!` WITH GATE3_UI_AT=<N>[,<N>...] ARE THE
 * UI PROBE (the comment "THE UI PROBE" below): the arguments and the result of a native of the
 * game's script, with the name, rectangle and visibility of every panel among them, and a
 * listing of the panels that are shown at a Present.  A diagnostic: it logs, reads guest memory
 * and writes none.
 *
 * GATE3_PRESENTS=mission+<N> and GATE3_COUNTS_AT and GATE3_UI_AT items `mission+<N>` count from the first frame
 * of the mission's world instead of from the run's start (its Present number differs from run
 * to run: the start's and the load's frames depend on how long they take).
 *
 * GATE3_PRESENTS=<N> ENDS THE RUN AT THE N-TH `IDXGISwapChain::Present` (the driver's switch, not
 * the game's: the game is not asked to quit, the process is ended inside the N-th call, after it
 * was counted and before its body).  With it every Present writes one line to the log: the time
 * since the Present before, the time into the run, and the process's physical footprint.  The
 * device's count table is printed as at any end, the child's exit status is RUN_EXIT_PRESENTS
 * and the driver's is 0.  When the child has ended (LauncherMain
 * returned, a stop, the guest ended the process, or GATE3_TIMEOUT seconds passed: 120) the
 * parent reads the log and prints one summary: the start, the read-back's answer, what the run
 * reached, the steps of finding 37's sequence that were seen, every module loaded and who asked for it (and the ones asked for that did not load),
 * the interfaces asked for, the threads
 * created, the files and registry keys asked for, where each thread was at the end, the stop.
 *
 * PROBES.  A step that is not a call into a Windows function is seen by a probe: a host
 * function in front of one row of a module's lifted function table (as gate3/ctors.c counts
 * constructors), which writes one `trace` line when the function is entered through the table
 * and one when it returns.  Only calls through the table are seen: imports, exports looked up
 * by name, vtable slots and stored pointers; a direct call inside a module is not.  The guest
 * sees no difference.  The table of probes is run_probes; every `CreateInterface` export gets
 * one, so that each interface asked for is in the log by name.
 *
 * The arguments of LauncherMain: finding 37 section 1.  The first is stored and later handed to
 * the engine as the application instance: the base of the module that stands in for the
 * executable.  The other three are never read by the callee: 0.
 */
#include <math.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <pthread.h>
#include <time.h>

#include "rt/com.h"
#include "rt/file.h"            /* rt_file_writers_under: is a file of the state half written? (THE STATE) */
#include "rt/init.h"
#include "rt/crash.h"
#include "rt/xaudio.h"         /* GATE3_SOUND: rt_sound_classes */
#include "rt/input.h"           /* GATE3_INPUT_SCRIPT: the scripted input source */
#include "rt/hostsock.h"        /* the line every end of a run writes about the host's sockets */
#include "rt/host.h"            /* RT_HOST_WINDOW: the host window's event loop on the first thread */
#include "rt/window.h"          /* rt_host_window_wanted */
#include "rt/log.h"
#include "rt/nulldev.h"         /* rt_iface_IDXGISwapChain, for GATE3_PRESENTS */
#include "rt/shim.h"
#include "rt/standin.h"         /* the stand-ins for EA's SDK: rows this run hands to rt_init itself */

#include "gate3_ids.h"
#include "modes.h"
#include "run.h"
#include "state.h"              /* THE STATE: where a run starts and what it leaves */

/* The command line of a first run (the brief; docs/findings/29-threads.md section 6):
 * -noorigin is the game's own switch that makes the launcher skip its call into EA's SDK
 * (finding 37 step 10; the owner's decision); -nosound; and the switches that hold the engine to
 * one working thread, EXCEPT that the job system gets one worker, not none: with no worker the
 * engine's host init later adds a job that only a worker may run and waits for it (finding 37
 * step 28, finding 29 section 6 caveat 1). */
static const char *const run_default_args[] = {
    "-noorigin", "-nosound",
    "-numworkerthreads", "1", "-numreservedcores", "0",
    "-threads", "1",
    "-noasync", "+async_mode", "1",
    "-NoQueuedPacketThread", "+net_queued_packet_thread", "0",
    "+host_thread_mode", "0",
    /* A window, not the whole screen (docs/handoff/gate3-run-3.md, entry 8): without these the
     * game goes full screen as soon as it has its swap chain (SetFullscreenState(TRUE) at
     * materialsystem_dx11+0x1560b, a stop of the device that is kept), whatever a stored
     * videoconfig.txt says.  The recorded real start was full screen: a windowed start is
     * not in the recording. */
    "-windowed", "-w", "1920", "-h", "1080",
};

/* ---- probes ---------------------------------------------------------------------------------------- */

#define PF_NAME  0x1u           /* rcx is a NUL-terminated name: print it */
#define PF_FMT   0x2u           /* rcx is a printf format (print it); if its first conversion is %s, rdx is a string (print it) */
#define PF_REC   0x4u           /* rcx points at a record: print its first six 32-bit words, and r9 (GATE3_PROBE item ending in '@') */
#define PF_SCRIPT 0x8u          /* rcx is the script VM: one line per call, at its return (GATE3_PROBE item with '&'; THE UI PROBE below) */
#define PF_TICK  0x10u          /* no line: where a panel listing that is due is written (GATE3_PROBE item with '!') */
#define PF_TEXTS 0x40u          /* r8 and r9 are printed as texts when they point at printable text (GATE3_PROBE item ending in '^') */
#define PF_AUD   0x80u          /* a decoder's "give me frames" routine (GATE3_PROBE item ending in '~'): rdx its record (channels at +0x25), r8 the float output, r9d the frames wanted; one line of statistics at the return, never a sample */
#define PF_MENU  0x20u          /* with PF_SCRIPT: OpenMenu or CloseMenu of the UI's script (THE MENU MARK at the top) */

/* THE MENU MARK (the comment at the top): the two natives, probed in every run.  Addresses:
 * the client's registration function names each native and then loads its code address
 * (tools/scout/script_natives.py; docs/handoff/gate3-run-10.md, section 1). */
#define RUN_MENU_MODULE     "client.dll"
#define RUN_MENU_OPEN_RVA   0x3b3ee0u
#define RUN_MENU_CLOSE_RVA  0x3b3c30u
#define RUN_MENU_MAIN       "\"menu_MainMenu\""       /* the panel's name as the probe's line has it */
#define RUN_MENU_MARK       "main-menu"
#define RUN_MISSION_MARK    "mission-frame"     /* reported at the first frame of the mission's world (run_on_present) */

typedef struct {
    char     module[40];        /* lower case, as RtModule.name */
    char     export_[40];       /* "": by rva */
    uint32_t rva;
    int      step;              /* of finding 37's sequence; 0: none */
    unsigned flags;
    char     what[72];
} RunProbe;

/* Addresses and step numbers: finding 37 (sections 2.1 to 2.5), read from the code there. */
static const RunProbe run_probes[] = {
    { "tier0.dll",               "ConfigureAndInitJobThreads", 0, 7, 0, "tier0 ConfigureAndInitJobThreads: the job threads" },
    { "tier0.dll",               "Pak_LoadDllIfNeeded", 0,  8, 0, "tier0 Pak_LoadDllIfNeeded: loads rtech_game" },
    { "rtech_game.dll",          "Pak_SetLoadFuncs", 0,     8, 0, "rtech_game Pak_SetLoadFuncs" },
    { "rtech_game.dll",          "", 0xa660,                9, 0, "pak loader start (g_pakLoadApi[0])" },
    { "tier0.dll",               "Tier0_InitOrigin", 0,    10, 0, "tier0 Tier0_InitOrigin: EA's SDK (not expected with -noorigin)" },
    { "launcher.dll",            "", 0x165b0,              12, 0, "outer group create: cvar, file system" },
    { "launcher.dll",            "", 0x16760,              14, 0, "outer group main: EXECUTABLE_PATH, runs the inner group" },
    { "launcher.dll",            "", 0x94e0,               15, 0, "inner group create: engine and the subsystem DLLs" },
    { "materialsystem_dx11.dll", "", 0x615a0,              16, 0, "material system told its mode (rdx)" },
    { "engine.dll",              "", 0x141340,             18, 0, "connection stage: connect of VCvarQuery001" },
    { "materialsystem_dx11.dll", "", 0x5b1c0,              19, 0, "material system connect: render thread, device" },
    { "engine.dll",              "", 0x1c4d40,             20, 0, "engine connect" },
    { "launcher.dll",            "", 0xb730,               21, 0, "inner group pre-init: game directory, start-up block" },
    { "inputsystem.dll",         "", 0x7760,               22, 0, "initialization stage: init of InputSystemVersion001" },
    { "materialsystem_dx11.dll", "", 0x8d4f0,              22, 0, "initialization stage: the material system's shader system" },
    { "engine.dll",              "", 0x1c6070,             22, 0, "initialization stage: engine init" },
    { "launcher.dll",            "", 0xb250,               23, 0, "inner group main: the engine's run" },
    { "tier0.dll",               "Error", 0,                0, PF_FMT, "tier0 Error: the game's fatal-error routine" },
};

#define RUN_MAX_PROBES 0x60
static RunProbe probe[RUN_MAX_PROBES];
static unsigned nprobe;
static LiftedFn probe_real[RUN_MAX_PROBES];
static char probe_label[RUN_MAX_PROBES][40];     /* GATE3_PROBE `&Name`: the name, for the line */

/* Appends ` "text"` to `out` (printable ASCII up to a quote, at most `max` characters of text). */
static size_t probe_text(char *out, size_t n, size_t cap, uint64_t addr, size_t max) {
    const unsigned char *s = (const unsigned char *)(uintptr_t)addr;
    if (!s || n + 4 >= cap) return n;
    out[n++] = ' ';
    out[n++] = '"';
    for (size_t k = 0; k < max && n < cap - 2 && *s >= 0x20 && *s < 0x7f && *s != '"'; s++, k++) out[n++] = (char)*s;
    out[n++] = '"';
    out[n] = 0;
    return n;
}

/* GATE3_PEEK (the comment at the top): one log line per item.  A module that is not loaded, a
 * NULL pointer or an item that does not parse is said so; nothing is written to guest memory. */
static void run_peek(const char *when, uint64_t nth) {
    const char *spec = getenv("GATE3_PEEK");
    if (!spec || !*spec) return;
    char buf[1024];
    snprintf(buf, sizeof buf, "%s", spec);
    for (char *item = strtok(buf, ","); item; item = strtok(NULL, ",")) {
        const bool deref = item[0] == '*';
        char mod[64] = "";
        unsigned long long rva = 0, off = 0;
        unsigned size = 0;
        const char *t = item + deref, *plus = strchr(t, '+'), *slash = strrchr(t, '/');
        if (plus && slash && (size_t)(plus - t) < sizeof mod - 5) {
            memcpy(mod, t, (size_t)(plus - t));
            strcat(mod, ".dll");
            char *end = NULL;
            rva = strtoull(plus + 1, &end, 16);
            if (deref && end && *end == '+') off = strtoull(end + 1, NULL, 16);
            size = (unsigned)atoi(slash + 1);
        }
        RtModule *m = mod[0] ? rt_module_find(mod) : NULL;
        if (!m || !m->loaded || (size != 1 && size != 2 && size != 4 && size != 8) || rva + 8 > m->size) {
            rt_log(RT_CH_LOADER, RT_LL_WARN, "gate3 run: peek at %s %llu: %s: not read (module not loaded, or not `[*]module+0xRVA[+0xOFF]/N`)", when, (unsigned long long)nth, item);
            continue;
        }
        uint64_t at = m->base + rva;
        if (deref) {
            at = LD64(at);
            if (at < 0x10000) { rt_log(RT_CH_LOADER, RT_LL_WARN, "gate3 run: peek at %s %llu: %s: the pointer is %#llx", when, (unsigned long long)nth, item, (unsigned long long)at); continue; }
            at += off;
        }
        uint64_t v = size == 1 ? LD8(at) : size == 2 ? LD16(at) : size == 4 ? LD32(at) : LD64(at);
        rt_log(RT_CH_LOADER, RT_LL_WARN, "gate3 run: peek at %s %llu: %s = %#llx", when, (unsigned long long)nth, item, (unsigned long long)v);
    }
}


/* ---- THE UI PROBE: what the client's UI has open, and where (unit gate3-run-10) ----------------------
 *
 * A diagnostic.  It reads guest memory and writes log lines; it writes nothing to guest memory
 * and calls no guest function.  Every read goes through the kernel (ui_rd), so that a pointer
 * that is not one ends a line with "?" and not the run.
 *
 * (1) GATE3_PROBE item `module+0xRVA&Name`: the function is a native of the game's script VM
 * (rcx is the VM).  One line per call, written when it returns:
 *     <tid>:Script Name(arg, ...) -> result
 * The layout is read from the client's own accessors: client+0x60c0 and +0x6160 take argument
 * i from the 16-byte slots at [vm+0x48] (a type word, a value); a text's characters are at
 * value+0x30; client+0x3710 pushes a result at slot [vm+0x68] of the array [vm+0x70] with type
 * 0x1000008 for a truth value; a native returns 1 when it pushed one.  An argument that is a
 * script instance is shown as a panel when it resolves as the client's own "hud element"
 * natives resolve it (client+0x114f0: [instance+0x40] is the wrapper, [wrapper+0x28] the
 * element) AND the element's panel handle ([element+0x68], its slot 0) points back at it
 * ([handle+0x40]): then its name ([element+0x70], slot 21), its rectangle and whether it is
 * shown follow, from the fields vgui2's own getters read (vgui2+0xdbb58, the handle's class:
 * slot 52 the absolute position, int16 at +0x6c and +0x6e; slot 13 the size, +0x50 and +0x52;
 * slot 27 "visible", bit 0 of +0x7a; slot 9 "visible with every parent", the same bit up the
 * chain of +0x28).  The rectangle is in the window's client coordinates.
 *
 * (2) GATE3_UI_AT=<N>[,<N>...] with a GATE3_PROBE item `module+0xRVA!` (a function the thread
 * that runs the UI calls every frame: inputsystem+0x7090, the main thread's input poll): at the
 * first call of that function after Present N the panels that are shown are listed, from the
 * root of the tree the last panel seen by (1) belongs to: one line each with depth, name,
 * rectangle, the element's class as the address of its vtable (tools/scout/flow_vtable.py
 * names it) and the number of children.  A panel that is not visible is not descended into. */

static uint64_t run_t0_ns, run_last_present_ns;
static char run_state_dir[1024];        /* this run's working copy (THE STATE) */

static bool ui_rd(uint64_t a, void *out, size_t n) {
    mach_vm_size_t got = 0;
    return a >= 0x10000 && (a >> 47) == 0 &&
           mach_vm_read_overwrite(mach_task_self(), a, n, (mach_vm_address_t)(uintptr_t)out, &got) == KERN_SUCCESS && got == n;
}
static uint64_t ui_q(uint64_t a) { uint64_t v = 0; return ui_rd(a, &v, 8) ? v : 0; }

static size_t ui_text(char *out, size_t n, size_t cap, uint64_t addr, size_t max) {
    char c[128];
    if (max > sizeof c) max = sizeof c;
    if (n + 4 >= cap) return n;
    out[n++] = '"';
    /* a text may end at the last byte of a page: read what is there, a byte at a time past the first failure */
    size_t got = ui_rd(addr, c, max) ? max : 0;
    if (!got) for (; got < max && ui_rd(addr + got, c + got, 1) && c[got]; got++) { }
    for (size_t k = 0; k < got && n < cap - 2 && c[k]; k++) out[n++] = c[k] >= 0x20 && c[k] < 0x7f && c[k] != '"' ? c[k] : '.';
    out[n++] = '"';
    out[n] = 0;
    return n;
}

static _Atomic uint64_t ui_last_handle;         /* a panel handle seen by (1): the tree to list */

/* The panel handle of a script instance, or 0. */
static uint64_t ui_handle_of(uint64_t instance, uint64_t *element) {
    const uint64_t e = ui_q(ui_q(instance + 0x40) + 0x28), h = ui_q(e + 0x68);
    if (!e || !h || ui_q(h + 0x40) != e) return 0;
    *element = e;
    return h;
}

/* ` "Name" [x,y w x h] shown|hidden|hidden-by-a-parent class=module+rva` for a panel handle. */
static size_t ui_panel(char *out, size_t n, size_t cap, uint64_t h) {
    const uint64_t e = ui_q(h + 0x40);
    int16_t xy[2] = { 0, 0 }, wh[2] = { 0, 0 };
    uint8_t flags = 0;
    ui_rd(h + 0x6c, xy, 4);
    ui_rd(h + 0x50, wh, 4);
    ui_rd(h + 0x7a, &flags, 1);
    bool all = flags & 1;
    uint64_t up = h;
    for (unsigned d = 0; all && d < 64 && (up = ui_q(up + 0x28)) != 0; d++) { uint8_t f = 0; ui_rd(up + 0x7a, &f, 1); all = f & 1; }
    if (ui_q(e + 0x70 + 0x18)) n = ui_text(out, n, cap, ui_q(e + 0x70), 64);
    else if (n + 3 < cap) { out[n++] = '"'; out[n++] = '"'; out[n] = 0; }
    const uint64_t vt = ui_q(e);
    const RtModule *vm = rt_module_from_pc(vt);
    n += (size_t)snprintf(out + n, cap - n, " [%d,%d %dx%d] %s class=%s+%#llx", xy[0], xy[1], wh[0], wh[1],
                          all ? "shown" : (flags & 1) ? "hidden-by-a-parent" : "hidden", vm ? vm->name : "?",
                          (unsigned long long)(vm ? vt - vm->base : vt));
    return n < cap ? n : cap - 1;
}

/* One script value. */
static size_t ui_value(char *out, size_t n, size_t cap, uint64_t slot) {
    uint32_t type = 0;
    uint64_t v = 0, e = 0, h;
    if (!ui_rd(slot, &type, 4) || !ui_rd(slot + 8, &v, 8)) return n + (size_t)snprintf(out + n, cap - n, "?");
    if (type == 0x08000010) return ui_text(out, n, cap, v + 0x30, 120);
    if (type == 0x05000002) return n + (size_t)snprintf(out + n, cap - n, "%d", (int)v);
    if (type == 0x05000004) { float f; uint32_t u = (uint32_t)v; memcpy(&f, &u, 4); return n + (size_t)snprintf(out + n, cap - n, "%g", (double)f); }
    if (type == 0x01000008) return n + (size_t)snprintf(out + n, cap - n, "%s", (uint32_t)v ? "true" : "false");
    if (type == 0x01000001) return n + (size_t)snprintf(out + n, cap - n, "null");
    if ((type & 0x408000) && (h = ui_handle_of(v, &e)) != 0) {
        ui_last_handle = h;
        n += (size_t)snprintf(out + n, cap - n, "panel %llx ", (unsigned long long)e);
        return ui_panel(out, n, cap, h);
    }
    return n + (size_t)snprintf(out + n, cap - n, "<type %x: %llx>", type, (unsigned long long)v);
}

/* The arguments of a native as it is entered: `(a, b, ...)` into out. */
static void ui_args(char *out, size_t cap, uint64_t vm) {
    const uint64_t base = ui_q(vm + 0x48), stack = ui_q(vm + 0x70);
    uint32_t top = 0;
    ui_rd(vm + 0x68, &top, 4);
    size_t n = 0;
    out[n++] = '(';
    out[n] = 0;
    if (base && stack && base >= stack) {
        const uint64_t first = (base - stack) / 16;
        const unsigned count = top > first && top - first <= 12 ? (unsigned)(top - first) : 0;
        for (unsigned i = 1; i < count && n + 160 < cap; i++) {      /* slot 0 is the script's `this` */
            if (i > 1) n += (size_t)snprintf(out + n, cap - n, ", ");
            n = ui_value(out, n, cap, base + 16ull * i);
        }
    }
    if (n < cap - 1) { out[n++] = ')'; out[n] = 0; }
}

static unsigned long run_ui_at[16];
static bool          run_ui_rel[16];            /* `mission+N`, as for GATE3_COUNTS_AT */
static unsigned      run_nui_at;
static _Atomic uint64_t ui_list_due;            /* the Present that asked for a listing; 0: none */

static void ui_list_walk(uint64_t h, unsigned depth, unsigned *lines) {
    char line[420];
    uint8_t flags = 0;
    uint32_t kids = 0;
    ui_rd(h + 0x7a, &flags, 1);
    if (!(flags & 1) || depth > 40 || *lines >= 3000) return;
    ui_rd(h + 0x20, &kids, 4);
    size_t n = (size_t)snprintf(line, sizeof line, "ui: %*s%u ", (int)depth, "", depth);
    n = ui_panel(line, n, sizeof line - 40, h);
    n += (size_t)snprintf(line + n, sizeof line - n, " element=%llx children=%u\n", (unsigned long long)ui_q(h + 0x40), kids);
    rt_log_raw(line, n);
    ++*lines;
    const uint64_t arr = ui_q(h + 0x8);
    for (uint32_t i = 0; i < kids && i < 2000; i++) { const uint64_t c = ui_q(arr + 8ull * i); if (c) ui_list_walk(c, depth + 1, lines); }
}

static void ui_list(uint64_t nth) {
    uint64_t h = ui_last_handle, up;
    if (!h) { rt_log(RT_CH_LOADER, RT_LL_WARN, "gate3 run: panels at present %llu (GATE3_UI_AT): no panel was seen by a `&` probe yet", (unsigned long long)nth); return; }
    for (unsigned d = 0; d < 64 && (up = ui_q(h + 0x28)) != 0; d++) h = up;
    rt_log(RT_CH_LOADER, RT_LL_WARN, "gate3 run: panels at present %llu (GATE3_UI_AT): %.3f ms into the run; every panel that is shown, "
           "from the root; lines `ui: <depth> \"name\" [x,y wxh] ...` follow", (unsigned long long)nth,
           (double)(clock_gettime_nsec_np(CLOCK_MONOTONIC) - run_t0_ns) / 1e6);
    rt_log_flush();
    unsigned lines = 0;
    ui_list_walk(h, 0, &lines);
    char end[96];
    rt_log_raw(end, (size_t)snprintf(end, sizeof end, "ui: end of the panels at present %llu: %u line(s)\n", (unsigned long long)nth, lines));
}

/* THE MENU MARK (the comment at the top).  `args` is the probe's text of the native's
 * arguments, `(panel <element> "name" [x,y wxh] ...)`: the menu's name is its first quoted
 * text.  Runs on the thread that runs the UI's script, after the native returned. */
static _Atomic uint64_t run_presents_seen;      /* the number of the last Present (run_on_present) */
static _Atomic unsigned run_menu_main_opens;
static _Atomic uint64_t run_menu_main_ns;       /* when the main menu first opened (GATE3_CONSOLE counts from it); 0: not yet */

static void run_menu(bool open, const char *args) {
    char name[80] = "?";
    const char *a = strchr(args, '"'), *b = a ? strchr(a + 1, '"') : NULL;
    if (a && b && (size_t)(b - a) < sizeof name) snprintf(name, sizeof name, "%.*s", (int)(b - a - 1), a + 1);
    const bool main_menu = strstr(args, RUN_MENU_MAIN) != NULL;
    const unsigned nth = open && main_menu ? ++run_menu_main_opens : 0;
    rt_log(RT_CH_LOADER, RT_LL_WARN, "gate3 run: menu: %s %s at %.3f s into the run, after Present %llu%s", open ? "opened" : "closed", name,
           (double)(clock_gettime_nsec_np(CLOCK_MONOTONIC) - run_t0_ns) / 1e9, (unsigned long long)atomic_load(&run_presents_seen),
           nth == 1 ? " (the main menu is reached; the mark `" RUN_MENU_MARK "` is reported to the input script)"
                    : nth ? " (the main menu again; the mark is reported again and fires nothing that has fired)" : "");
    if (nth == 1) atomic_store(&run_menu_main_ns, clock_gettime_nsec_np(CLOCK_MONOTONIC));
    if (open && main_menu) rt_input_mark(RUN_MENU_MARK);
}

/* ---- GATE3_CONSOLE=<seconds>:<command>: one console command, handed to the engine's own command buffer ----
 * <seconds> after the main menu first opened (THE MENU MARK), on the game's main thread (in the `!`
 * probe at inputsystem+0x7090, the main thread's input poll, after that function returned), the
 * driver calls the engine's two functions exactly as the engine's own code at engine+0x15cb01 does
 * for "changelevel %s": the command target of the current player (engine+0x120630, no argument),
 * then "append this text to the command buffer" (engine+0x1203b0: target, text, source 0).  The
 * engine runs the buffer in its own time.  For tests that must start somewhere a script cannot
 * click to (a later mission: `GATE3_CONSOLE='3:map sp_crashsite'` with no input script).  It is
 * synthetic input, logged as such; a person's run does not set it. */
#define RUN_CBUF_MODULE      "engine.dll"
#define RUN_CBUF_PLAYER_RVA  0x120630u
#define RUN_CBUF_ADD_RVA     0x1203b0u
#define RUN_TICK_MODULE      "inputsystem.dll"
#define RUN_TICK_RVA         0x7090u
static char             run_console_text[512];      /* the command and a newline; "" when GATE3_CONSOLE is not set */
static double           run_console_after_s;
static _Atomic int      run_console_state;          /* 0 none, 1 waiting, 2 handed over */

static void run_console_tick(Ctx *ctx) {
    if (atomic_load(&run_console_state) != 1) return;
    const uint64_t t0 = atomic_load(&run_menu_main_ns), now = clock_gettime_nsec_np(CLOCK_MONOTONIC);
    if (!t0 || (double)(now - t0) / 1e9 < run_console_after_s) return;
    int waiting = 1;
    if (!atomic_compare_exchange_strong(&run_console_state, &waiting, 2)) return;
    const RtModule *m = rt_module_find(RUN_CBUF_MODULE);
    if (!m) { rt_log(RT_CH_LOADER, RT_LL_WARN, "gate3 run: console (GATE3_CONSOLE): %s is not loaded; nothing handed over", RUN_CBUF_MODULE); return; }
    const uint64_t rax = ctx->r[0];                 /* the probed function has returned: only its result is live */
    const __typeof__(ctx->xmm[0]) x0 = ctx->xmm[0];
    const uint64_t target = rt_guest_call(ctx, (GUESTFN)(m->base + RUN_CBUF_PLAYER_RVA), 0, NULL);
    const uint64_t args[3] = { (uint32_t)target, (uint64_t)(uintptr_t)run_console_text, 0 };
    rt_guest_call(ctx, (GUESTFN)(m->base + RUN_CBUF_ADD_RVA), 3, args);
    ctx->r[0] = rax;
    ctx->xmm[0] = x0;
    rt_log(RT_CH_LOADER, RT_LL_WARN, "gate3 run: console (GATE3_CONSOLE, SYNTHETIC): `%.*s` handed to the engine's command buffer (target %u) at %.3f s into the "
           "run, %.1f s after the main menu, on thread %x", (int)strcspn(run_console_text, "\n"), run_console_text, (unsigned)target,
           (double)(now - run_t0_ns) / 1e9, (double)(now - t0) / 1e9, RT_THREAD(ctx)->id);
}

static void probe_call(unsigned k, Ctx *ctx) {
    if (probe[k].flags & PF_TICK) {                 /* no line: the place a panel listing is written */
        uint64_t due = ui_list_due;
        if (due && atomic_compare_exchange_strong(&ui_list_due, &due, 0)) ui_list(due);
        probe_real[k](ctx);
        run_console_tick(ctx);
        return;
    }
    if (probe[k].flags & PF_SCRIPT) {               /* one line, at the return: the arguments as they were at the entry */
        char line[1500], res[400] = "";
        const uint64_t vm = ctx->r[1];
        ui_args(line, sizeof line, vm);
        probe_real[k](ctx);
        if ((int32_t)ctx->r[0] == 1) {
            uint32_t top = 0;
            ui_rd(vm + 0x68, &top, 4);
            if (top) ui_value(res, 0, sizeof res, ui_q(vm + 0x70) + 16ull * (top - 1));
        }
        rt_log(RT_CH_TRACE, RT_LL_WARN, "%x:Script %s%s -> %s", RT_THREAD(ctx)->id, probe_label[k][0] ? probe_label[k] : "?", line,
               (int32_t)ctx->r[0] == 1 ? res : (int32_t)ctx->r[0] == 0 ? "(nothing)" : "(a script error)");
        if (probe[k].flags & PF_MENU) run_menu(probe[k].rva == RUN_MENU_OPEN_RVA, line);
        return;
    }
    if (probe[k].flags & PF_AUD) {
        const uint64_t rec = ctx->r[2], out = ctx->r[8];
        const uint32_t want = (uint32_t)ctx->r[9];
        probe_real[k](ctx);
        const uint32_t got = (uint32_t)ctx->r[0];
        uint8_t n = 0;
        ui_rd(rec + 0x25, &n, 1);
        if (got >= 64 && got <= 65536 && n >= 1 && n <= 8) {
            char line[600]; size_t o = 0;
            const float *f = (const float *)(uintptr_t)out;
            for (unsigned c = 0; c < n; c++) {
                double sxx = 0, s1 = 0, s4 = 0;
                for (uint32_t t = 4; t < got; t++) {
                    const double x = f[(size_t)t * n + c];
                    sxx += x * x; s1 += x * f[(size_t)(t - 1) * n + c]; s4 += x * f[(size_t)(t - 4) * n + c];
                }
                o += (size_t)snprintf(line + o, sizeof line - o, " c%u=%.4f,%.3f,%.3f", c, sqrt(sxx / (got - 4)), sxx > 0 ? s1 / sxx : 0, sxx > 0 ? s4 / sxx : 0);
            }
            rt_log(RT_CH_TRACE, RT_LL_WARN, "%x:Aud rec=%llx n=%u want=%u got=%u%s", RT_THREAD(ctx)->id, (unsigned long long)rec, n, want, got, line);
        }
        return;
    }
    const RunProbe *p = &probe[k];
    unsigned tid = RT_THREAD(ctx)->id;
    char name[400] = "";
    if ((p->flags & PF_NAME) && ctx->r[1]) probe_text(name, 0, 72, ctx->r[1], 66);
    if ((p->flags & PF_REC) && ctx->r[1] >= 0x10000)
        snprintf(name, sizeof name, " rec=%x,%x,%x,%x,%x,%x r9=%llx", LD32(ctx->r[1]), LD32(ctx->r[1] + 4), LD32(ctx->r[1] + 8),
                 LD32(ctx->r[1] + 12), LD32(ctx->r[1] + 16), LD32(ctx->r[1] + 20), (unsigned long long)ctx->r[9]);
    else if (p->flags & PF_REC)
        snprintf(name, sizeof name, " r9=%llx", (unsigned long long)ctx->r[9]);
    if ((p->flags & PF_FMT) && ctx->r[1]) {
        size_t n = probe_text(name, 0, sizeof name, ctx->r[1], 180);
        const char *f = (const char *)(uintptr_t)ctx->r[1], *c = strchr(f, '%');
        while (c && c[1] == '%') c = strchr(c + 2, '%');
        if (c && c[1] == 's') probe_text(name, n, sizeof name, ctx->r[2], 180);
    }
    if (p->flags & PF_TEXTS) {                      /* either may be a number: read through the kernel, print only what is text */
        size_t n = strlen(name);
        for (unsigned i = 8; i <= 9 && n + 140 < sizeof name; i++) {
            unsigned char c[2];
            if (!ui_rd(ctx->r[i], c, 2) || c[0] < 0x20 || c[0] >= 0x7f || (c[1] && (c[1] < 0x09 || c[1] >= 0x7f))) continue;
            n += (size_t)snprintf(name + n, sizeof name - n, " r%u=", i);
            n = ui_text(name, n, sizeof name, ctx->r[i], 120);
        }
    }
    /* Who called: the return address on the guest stack, as module+rva when it is in a module. */
    char from[96];
    const uint64_t ret = LD64(ctx->r[4]);
    const RtModule *rm = rt_module_from_pc(ret);
    if (rm) snprintf(from, sizeof from, "%s+%#llx", rm->name, (unsigned long long)(ret - rm->base));
    else    snprintf(from, sizeof from, "%#llx", (unsigned long long)ret);
    rt_log(RT_CH_TRACE, RT_LL_WARN, "%x:Probe %s+%#x step %d [%s]%s rcx=%llx rdx=%llx r8=%llx from=%s", tid, p->module, p->rva, p->step,
           p->what, name, (unsigned long long)ctx->r[1], (unsigned long long)ctx->r[2], (unsigned long long)ctx->r[8], from);
    const bool mine = !strcmp(p->what, "GATE3_PROBE");      /* a probe of the command line: GATE3_PEEK's items before and after */
    static _Atomic unsigned long hits;
    const unsigned long hit = mine ? ++hits : 0;
    if (mine) run_peek("the entry of probe hit", hit);
    probe_real[k](ctx);
    if (mine) run_peek("the return of probe hit", hit);
    rt_log(RT_CH_TRACE, RT_LL_WARN, "%x:ProbeRet %s+%#x step %d [%s]%s rax=%llx", tid, p->module, p->rva, p->step, p->what, name,
           (unsigned long long)ctx->r[0]);
}

#define P0(x)  static void probe_##x(Ctx *ctx) { probe_call(0x##x, ctx); }
#define P1(x)  P0(x##0) P0(x##1) P0(x##2) P0(x##3) P0(x##4) P0(x##5) P0(x##6) P0(x##7) \
               P0(x##8) P0(x##9) P0(x##A) P0(x##B) P0(x##C) P0(x##D) P0(x##E) P0(x##F)
P1(0) P1(1) P1(2) P1(3) P1(4) P1(5)
#undef P0
#define P0(x)  probe_##x,
static const LiftedFn probe_pool[RUN_MAX_PROBES] = { P1(0) P1(1) P1(2) P1(3) P1(4) P1(5) };
#undef P0
#undef P1

/* ---- the stand-ins for EA's SDK that are imports (rt/standin.h) ------------------------------------- */

/* The rows of rt_standin_table this driver knows, each with the environment variable that
 * leaves it out.  A row of that table that is not listed here is NOT handed to the runtime.
 * Only `gate3 run` hands any: `gate3 load`, `registry` and every other program have none. */
typedef struct { const char *name, *off_env, *number, *file, *answer; bool on, user_only; } RunStandin;   /* user_only: handed over for the placeholder user only */
static RunStandin run_standins[] = {
    { RT_STANDIN_2_NAME, "GATE3_NO_STANDIN_PROGRESSIVE_INSTALL", "2",
      "runtime/standins/originsdk/OriginIsProgressiveInstallationAvailableSync.c",
      "result 0 and the caller's byte left as it is (\"the call worked; progressive installation is not available\")", false, false },
    { RT_STANDIN_3_NAME, "GATE3_NO_STANDIN_SETTING_ENVIRONMENT", "3",
      "runtime/standins/originsdk/OriginGetSettingSync.c",
      "result 0 and the text \"production\" at the caller's buffer, for setting 1 (\"the call worked; the Origin environment is "
      "production\"); for the placeholder user also result 0 and an empty text for setting 2 from the engine's call at engine+0x183ac2 "
      "(\"the overlay is not on\"); every other setting number, call site or shape halts", false, false },
    { RT_STANDIN_4_NAME, "GATE3_NO_STANDIN_EVENT_CALLBACKS", "4",
      "runtime/standins/originsdk/OriginRegisterEventCallback.c",
      "result 0 for the engine's own callback (engine+0x182130) with third argument 0, the registration counted and nothing ever "
      "called back; any other callback or third argument halts", false, false },
    { RT_STANDIN_5_NAME, "GATE3_NO_STANDIN_UPDATE", "5",
      "runtime/standins/originsdk/OriginUpdate.c",
      "result 0 for the engine's per-frame call at engine+0x18490f (\"the kit has no error this frame\"), nothing written and nothing "
      "called back, the first answer logged and every answer counted; a call from anywhere else halts", false, false },
    { RT_STANDIN_6_NAME, "GATE3_NO_STANDIN_DEFAULT_USER", "6",
      "runtime/standins/originsdk/OriginGetDefaultUser.c",
      "result 0 for the engine's per-frame call at engine+0x1849da (\"nobody is signed in\": no user is named and no id is "
      "supplied), nothing written and nothing called back, the first answer logged and every answer counted; a call from anywhere "
      "else halts", false, false },
    { RT_STANDIN_7_NAME, "GATE3_NO_STANDIN_PROFILE", "7",
      "runtime/standins/originsdk/OriginGetProfile.c",
      "result 0 for the engine's one request OriginGetProfile(0, 10000, engine+0x184fd0, 0) at engine+0x183c91; the engine's callback is "
      "then called once, from inside stand-in 5's next answer, with THE PLACEHOLDER's made-up profile (id 1, the name \"Pilot\", two "
      "empty texts, two zero bytes); any other shape, a second request or the user \"nobody\" halts", false, true },
    { RT_STANDIN_8_NAME, "GATE3_NO_STANDIN_AUTH_CODE", "8",
      "runtime/standins/originsdk/OriginRequestAuthCode.c",
      "result 0 for the engine's request OriginRequestAuthCode(1, \"TITANFALL2-PC-SERVER\", engine+0x1857b0, 0, 30000, 0) at "
      "engine+0x18437b, and it never completes: no authorisation code exists and nothing is called back; the first answer logged, "
      "every answer counted; any other shape or the user \"nobody\" halts", false, true },
    { RT_STANDIN_9_NAME, "GATE3_NO_STANDIN_ENTITLEMENTS", "9",
      "runtime/standins/originsdk/OriginQueryEntitlements.c",
      "result 0 for the engine's query OriginQueryEntitlements(1, six zeros, \"Titanfall2PC\", engine+0x181f60, 0, 10000, 0) at engine+0x183fe2, and it never completes: no list and no entitlement exists, none is claimed, nothing is called back; the first answer logged, every answer counted; any other shape or the user \"nobody\" halts", false, true },
    { RT_STANDIN_10_NAME, "GATE3_NO_STANDIN_BLOCKED_USERS", "10",
      "runtime/standins/originsdk/OriginQueryBlockedUsers.c",
      "result 0 for the engine's query OriginQueryBlockedUsers(1, engine+0x183e90, 0, 10000, a pointer for a handle) at engine+0x183cce, the handle written as 0, and it never completes: no list of blocked users exists and nothing is called back; the first answer logged, every answer counted; any other shape or the user \"nobody\" halts", false, true },
    { RT_STANDIN_11_NAME, "GATE3_NO_STANDIN_FRIENDS", "11",
      "runtime/standins/originsdk/OriginQueryFriends.c",
      "result 0 for the engine's query OriginQueryFriends(1, engine+0x184000, 0, 10000, a pointer for a handle) at engine+0x183cf7, the handle written as 0, and it never completes: no list of friends exists and nothing is called back; the first answer logged, every answer counted; any other shape or the user \"nobody\" halts", false, true },
    { RT_STANDIN_13_NAME, "GATE3_NO_STANDIN_PRESENCE", "13",
      "runtime/standins/originsdk/OriginSetPresence.c",
      "result 0 for the engine's call OriginSetPresence(1, 3 or 6, three texts or NULL) at engine+0x184bad; nothing is done with the texts: they are not read and nothing is told to anyone; the first answer logged, every answer counted; any other shape or the user \"nobody\" halts", false, true },
    { RT_STANDIN_12_NAME, "GATE3_NO_STANDIN_CHECK_ONLINE", "12",
      "runtime/standins/originsdk/OriginCheckOnline.c",
      "result 0 for the engine's call OriginCheckOnline(a pointer to a byte that is 0) at engine+0x183a39, the byte left 0: \"not online\" (true here); nothing written; the first answer logged, every answer counted; any other shape or the user \"nobody\" halts", false, true },
    { RT_STANDIN_14_NAME, "GATE3_NO_STANDIN_PERSONA", "14",
      "runtime/standins/originsdk/OriginGetDefaultPersona.c",
      "the made-up persona 1 (in the whole register) for the engine's call OriginGetDefaultPersona() at engine+0x18483a; nothing is written and nothing called back; the first answer logged, every answer counted; any other call site or the user \"nobody\" halts", false, true },
    { RT_STANDIN_15_NAME, "GATE3_NO_STANDIN_ACHIEVEMENT", "15",
      "runtime/standins/originsdk/OriginGrantAchievement.c",
      "result 0 (\"accepted\") for the engine's call OriginGrantAchievement(1, 1, n, 1, the posted text, 10000, engine+0x182c60, 0) at engine+0x18487b; nothing is done with it: no achievement is recorded, nothing written, the callback never called; every call logged and counted; any other shape or the user \"nobody\" halts", false, true },
    { RT_STANDIN_16_NAME, "GATE3_NO_STANDIN_SHUTDOWN", "16",
      "runtime/standins/originsdk/OriginShutdown.c",
      "result 0 (\"no error\") for the engine's call OriginShutdown() at engine+0x18471b, for both users; nothing is done at all: nothing written, nothing called back, nothing sent; the first answer logged, every answer counted; any other call site halts", false, false },
};
#define RUN_STANDINS (sizeof run_standins / sizeof *run_standins)

/* The shims that are not the runtime's own: the driver's generated table (no row today) and
 * the stand-ins that are not switched off. */
static RtShimEntry run_shims[RUN_STANDINS + 1];        /* (rows of the driver's list: 16 at most, the summary's arrays) */
static unsigned    run_nshims;

static void run_check_origin(bool first);

/* THE USER of this run (the comment at the top; rt/standin.h): GATE3_USER, read in run_start. */
static RtStandinUser run_user = RT_STANDIN_USER_PLACEHOLDER;      /* THE DEFAULT since the whole posted table is built: the comment at the top */

static void run_start(const RtModuleDef *defs, int argc, char **argv) {
    const char *root = getenv("GATE3_ROOT"), *state = getenv("GATE3_STATE");
    if (!root || !*root) root = GATE3_INSTALL_ROOT;
    if (!state || !*state) { fprintf(stderr, "gate3 run: no state directory: gate3_mode_run makes the run's working copy and names it (THE STATE)\n"); exit(2); }
    if (mkdir(state, 0700) != 0 && errno != EEXIST) { perror(state); exit(2); }
    snprintf(run_state_dir, sizeof run_state_dir, "%s", state);
    if (gate3_shim_count) { fprintf(stderr, "gate3 run: the driver's own shim table has rows; run_start hands only stand-ins to the runtime\n"); exit(2); }
    const char *user = getenv("GATE3_USER");
    if (user && !strcmp(user, "nobody")) run_user = RT_STANDIN_USER_NOBODY;
    else if (user && *user && strcmp(user, "placeholder") != 0) {
        fprintf(stderr, "gate3 run: GATE3_USER=%s: the users are `placeholder` (the default) and `nobody`; not started\n", user);
        exit(2);
    }
    rt_standin_user = run_user;         /* before rt_init: no guest code has run */
    /* SOUND (rt/xaudio.h, rt_sound_classes): off unless GATE3_SOUND=on.  The sound objects are being
     * built (NOTES.md, 12 October 2026); until they are complete a run with them stops at the first
     * method without a body, even with -nosound, because the audio library asks for the device list. */
    { const char *snd = getenv("GATE3_SOUND"); rt_sound_classes = snd && !strcmp(snd, "on"); }
    run_nshims = 0;
    for (unsigned k = 0; k < RUN_STANDINS; k++) {
        if (run_user == RT_STANDIN_USER_PLACEHOLDER && !strcmp(run_standins[k].name, RT_STANDIN_6_NAME))
            run_standins[k].answer = "result 1 for the engine's per-frame call at engine+0x1849da (THE PLACEHOLDER USER: a made-up id, not the "
                "owner's and taken from nowhere), nothing written and nothing called back, the first answer logged and every answer counted; a "
                "call from anywhere else halts";
        const char *off = getenv(run_standins[k].off_env);
        run_standins[k].on = !(off && off[0] == '1') && (!run_standins[k].user_only || run_user == RT_STANDIN_USER_PLACEHOLDER);
        if (!run_standins[k].on) continue;
        const RtShimEntry *row = NULL;
        for (unsigned i = 0; i < rt_standin_count && !row; i++)
            if (!strcmp(rt_standin_table[i].lib, "originsdk") && !strcmp(rt_standin_table[i].name, run_standins[k].name)) row = &rt_standin_table[i];
        if (!row) { fprintf(stderr, "gate3 run: no stand-in for originsdk.dll!%s in rt_standin_table\n", run_standins[k].name); exit(2); }
        run_shims[run_nshims++] = *row;
    }
    const char *measure = getenv("GATE3_MEASURE_ONLY_NOT_A_WAY_TO_RUN_SETTING1_LEFT_EMPTY");
    rt_standin3_leave_buffer = measure && measure[0] == '1';       /* the comment at the top: A MEASUREMENT, NOT A WAY TO RUN */
    RtConfig cfg = {
        .state_dir = state, .install_root = root, .tso = RT_TSO_TRY, .argc = argc, .argv = argv,
        .extra_modules = defs, .n_extra_modules = gate3_module_def_count,
        .extra_shims = run_shims, .n_extra_shims = run_nshims,
    };
    rt_init(&cfg);
    run_check_origin(true);             /* THE RULE ABOUT EA'S SDK, before any module of the game is loaded (below) */
}

/* THE RULE ABOUT EA'S SDK, CHECKED IN EVERY RUN BEFORE ANY GAME CODE.  "Before any game code"
 * is meant exactly: run_start calls this right after rt_init, which builds the module
 * registry, makes every stub (the loader's pre-scan) and seals the dispatch table but loads
 * no module of the game; no entry point, no constructor, no TLS callback and no guest call
 * has run.  (Until 11 October 2026, 08:30, it was called after tier0.dll, vstdlib.dll and
 * launcher.dll were loaded and after the guest call for the language: finding 6 of
 * docs/reviews/night-2026-10-11-code-review.md.  gate3/check_origin_fence.sh, run by
 * `make gate3-check`, holds the order.)  run_child calls it a second time right before
 * LauncherMain; nothing can have changed by then (the table is sealed), and it costs nothing.
 *
 * The rule: the only imports of originsdk.dll with an answer are the stand-ins of run_standins
 * that are switched on; every other import the game's DLLs have is the stub that halts the
 * run; and a look-up by name or ordinal (GetProcAddress, the route of a delay-load helper
 * too) that finds no stand-in stops the run as well and is never answered NULL (finding 1 of
 * the same review; rt_module_export, rt/module.h).  Checked here:
 *   - originsdk.dll is a stubbed module, and the core's look-up stops on a miss in it;
 *   - its shims are exactly the stand-ins switched on, each from runtime/standins/, none with
 *     an ordinal (so no look-up by ordinal has an answer);
 *   - every other stub of the module is of the halting kind;
 *   - the look-up by name of each stand-in gives the stub an import slot gets, nothing else.
 * Anything else ends the run here, before it started.  GATE3_TEST_ORIGIN_CHECK_FAILS=1 is the
 * test's switch (gate3/check_origin_fence.sh): the verdict is then "bad" with nothing else
 * changed, so that where a refusal happens can be measured.  `first` logs the one line the
 * summary prints first under "STAND-INS". */
static void run_check_origin(bool first) {
    RtModule *o = rt_module_find("originsdk.dll");
    if (!o || o->kind != RT_MOD_STUBBED) { fprintf(stderr, "gate3 run: originsdk.dll is not a stubbed module of the runtime\n"); _exit(1); }
    unsigned on = 0, halting = 0;
    for (unsigned k = 0; k < RUN_STANDINS; k++) on += run_standins[k].on;
    bool ok = o->nshims == on && rt_module_lookup_stops(o);
    for (unsigned i = 0; i < o->nshims && ok; i++) {
        bool known = false;
        for (unsigned k = 0; k < RUN_STANDINS; k++) known |= run_standins[k].on && !strcmp(o->shims[i]->name, run_standins[k].name);
        ok = known && strstr(o->shims[i]->file, "runtime/standins/") != NULL && o->shims[i]->ordinal == 0;
        if (ok) {       /* the look-up by name of a stand-in: its own stub, in the shims' area, the one the loader binds */
            const uint64_t slot = rt_native_import("originsdk.dll", o->shims[i]->name, 0);
            ok = slot == o->base + RT_NATIVE_SHIM_STUBS + (uint64_t)RT_STUB_SIZE * i && rt_module_export(o, o->shims[i]->name, 0) == slot;
        }
    }
    ok = ok && rt_module_stubs_all_halt(o);
    ok = ok && rt_standin4_test_module == NULL && rt_standin56_test_module == NULL && rt_standin56_test_return_rva == 0   /* the tests' variables: never set in a run */
            && rt_standin7_test_callback_rva == 0;
    ok = ok && rt_standin_user == run_user;     /* the user the stand-ins answer for is the one this run selected, and the set above is that user's:
                                                 * the rows marked user_only are on for the placeholder only (run_start) */
    halting = o->nstubs;                /* the module's stubs that are not shims: all of the halting kind, as just checked */
    const char *test_bad = getenv("GATE3_TEST_ORIGIN_CHECK_FAILS");
    if (test_bad && test_bad[0] == '1') ok = false;
    if (!ok) {
        rt_log_flush();
        fprintf(stderr, "gate3 run: originsdk.dll has %u answered import(s) and %u stand-in(s) are switched on, or one of them is not a "
                "stand-in of this driver's list, or a stub of the module is not of the halting kind, or a look-up by name would be "
                "answered: not started (every import of EA's SDK but the posted stand-ins must halt)%s\n", o->nshims, on,
                test_bad && test_bad[0] == '1' ? " [GATE3_TEST_ORIGIN_CHECK_FAILS=1: the test's switch, nothing was wrong]" : "");
        _exit(1);
    }
    if (first)
        rt_log(RT_CH_LOADER, RT_LL_WARN, run_user == RT_STANDIN_USER_PLACEHOLDER
               ? "gate3 run: stand-in: THE USER OF THIS RUN: THE PLACEHOLDER (GATE3_USER=placeholder): a local, made-up user, id 1, the name "
                 "\"Pilot\", not the owner's and taken from nowhere (issue 32, comments 6114036238 and 6114239628). The stand-ins of those "
                 "posts are listed below, each with its count; every other import of EA's SDK halts. THE DEFAULT, because the game's "
                 "`map` command does nothing while nobody is signed in; GATE3_USER=nobody selects the other user"
               : "gate3 run: stand-in: THE USER OF THIS RUN: NOBODY (GATE3_USER=nobody): stand-in 6 answers 0, \"nobody is signed in\", and "
                 "the stand-ins from 7 on are not in this run except stand-in 16 (it answers both users); the game's `map` command then does nothing. The default is the local, "
                 "made-up placeholder user of issue 32, comment 6114036238. The counts of each stand-in are below");
    if (first)
        rt_log(RT_CH_LOADER, RT_LL_WARN, "gate3 run: stand-in: THE RULE ABOUT EA'S SDK HOLDS, checked before any module of the game was loaded "
               "(no entry point and no guest call had run): %u import(s) of originsdk.dll are answered, each by a posted stand-in that is "
               "switched on; the other %u that the game's DLLs have halt the run; a look-up by name or ordinal (GetProcAddress) that finds "
               "no stand-in stops the run and is never answered NULL", o->nshims, halting);
}

/* The stand-ins that are called every frame log their first answer only; this is their count,
 * one line at every end of a run (rt_com_counts_also: it may run inside a fatal report, so it
 * takes no lock and allocates nothing).  The summary reads it for STAND-INS. */
static void run_standin_counts(void) {
    static char line[800];
    int n = snprintf(line, sizeof line, "standins: counted: stand-in 5 answered %llu time(s), stand-in 6 answered %llu time(s); "
                     "of stand-in 3's answers %llu for setting 2; stand-in 7 delivered %u profile(s); stand-in 8 answered %llu time(s), stand-in 9 answered %llu time(s), stand-in 10 answered %llu time(s), stand-in 11 answered %llu time(s), stand-in 13 answered %llu time(s), stand-in 12 answered %llu time(s), stand-in 14 answered %llu time(s), stand-in 15 answered %llu time(s), stand-in 16 answered %llu time(s)\n",
                     rt_standin5_answers(), rt_standin6_answers(), rt_standin3_setting2_answers(), rt_standin7_deliveries(),
                     rt_standin8_answers(), rt_standin9_answers(), rt_standin10_answers(), rt_standin11_answers(), rt_standin13_answers(), rt_standin12_answers(), rt_standin14_answers(), rt_standin15_answers(), rt_standin16_answers());
    if (n > 0) rt_log_raw(line, (size_t)n < sizeof line ? (size_t)n : sizeof line - 1);
}

/* One line per stand-in, which the summary prints under "STAND-INS".  Logged where it always
 * was (run_child, right before LauncherMain), after the rule was checked a second time. */
static void run_log_standins(void) {
    const RtModule *o = rt_module_find("originsdk.dll");
    const unsigned halting = o->nstubs;     /* run_check_origin: every stub of this module is of the halting kind */
    for (unsigned k = 0; k < RUN_STANDINS; k++) {
        const RunStandin *st = &run_standins[k];
        if (st->on)
            rt_log(RT_CH_LOADER, RT_LL_WARN, "gate3 run: stand-in: EA's SDK, one import (stand-in %s): originsdk.dll!%s is answered by %s with %s; "
                   "in a real start EA's SDK answers it (issue 32, posted before it was written). The other %u imports of originsdk.dll that "
                   "the game's DLLs have halt the run. %s=1 leaves it out", st->number, st->name, st->file, st->answer, halting, st->off_env);
        else if (st->user_only && run_user != RT_STANDIN_USER_PLACEHOLDER)
            rt_log(RT_CH_LOADER, RT_LL_WARN, "gate3 run: stand-in: NOT IN THIS RUN (the user is nobody; it answers for GATE3_USER=placeholder "
                   "only): stand-in %s for originsdk.dll!%s; that import is one of the %u imports of originsdk.dll that halt this run",
                   st->number, st->name, halting);
        else
            rt_log(RT_CH_LOADER, RT_LL_WARN, "gate3 run: stand-in: NOT IN THIS RUN (%s=1): stand-in %s for originsdk.dll!%s; that import "
                   "is one of the %u imports of originsdk.dll that halt this run", st->off_env, st->number, st->name, halting);
    }
    if (rt_standin3_leave_buffer)
        rt_log(RT_CH_LOADER, RT_LL_WARN, "gate3 run: stand-in: THIS RUN IS A MEASUREMENT AND NOT A WAY TO RUN "
               "(GATE3_MEASURE_ONLY_NOT_A_WAY_TO_RUN_SETTING1_LEFT_EMPTY=1): stand-in 3 answers result 0 and writes nothing, which is "
               "NOT the answer posted on issue 32");
}

static void write_all(int fd, const void *p, size_t n) {
    for (const char *c = p; n; ) {
        ssize_t w = write(fd, c, n);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) _exit(96);
        c += w;
        n -= (size_t)w;
    }
}

/* Fill probe[] with the rva of every row: a child starts a runtime to read the export tables
 * (the probes must be in the module table that the run's own rt_init is given). */
static void resolve_probes(void) {
    int fd[2];
    if (pipe(fd) != 0) { perror("pipe"); exit(2); }
    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); exit(2); }
    if (pid == 0) {
        static Gate2Mods mods;
        close(fd[0]);
        unsetenv("RT_LOG_FILE");
        unsetenv("RT_LOG");
        /* This process is a fork that does not exec, to which the app bundle's kernel refuses
         * the x86 ordering mode; it runs no guest code, so it does not ask (RT_TSO=require in
         * the environment would otherwise end it here, and the run with it). */
        setenv("RT_TSO", "off", 1);
        /* run_start checks the rule about EA's SDK here too (run_check_origin): a run that
         * breaks it ends in this process, which runs no guest code, with the driver's status
         * 2.  The test's switch is for the process that would run the game, so that the test
         * measures where THAT process is refused (gate3/check_origin_fence.sh). */
        unsetenv("GATE3_TEST_ORIGIN_CHECK_FAILS");
        run_start(gate3_module_defs, 0, NULL);
        gate2_mods_collect(&mods, "gate3");
        for (unsigned k = 0; k < sizeof run_probes / sizeof *run_probes; k++) {
            RunProbe p = run_probes[k];
            if (p.export_[0]) {
                const Gate2Export *x = NULL;
                for (unsigned i = 0; i < mods.nlifted && !x; i++)
                    if (!strcmp(mods.lifted[i]->name, p.module) && mods.scans[i].present) x = gate2_find_export(&mods.scans[i], p.export_);
                if (!x) { fprintf(stderr, "gate3 run: %s has no export %s (a row of run_probes)\n", p.module, p.export_); _exit(1); }
                p.rva = x->rva;
            }
            write_all(fd[1], &p, sizeof p);
        }
        for (unsigned i = 0; i < mods.nlifted; i++) {
            const Gate2Export *x = mods.scans[i].present ? gate2_find_export(&mods.scans[i], "CreateInterface") : NULL;
            if (!x) continue;
            RunProbe p = { .rva = x->rva, .flags = PF_NAME };
            snprintf(p.module, sizeof p.module, "%s", mods.lifted[i]->name);
            snprintf(p.export_, sizeof p.export_, "CreateInterface");
            snprintf(p.what, sizeof p.what, "CreateInterface");
            write_all(fd[1], &p, sizeof p);
        }
        _exit(0);
    }
    close(fd[1]);
    for (nprobe = 0; nprobe < RUN_MAX_PROBES; nprobe++) {
        size_t got = 0;
        while (got < sizeof probe[0]) {
            ssize_t r = read(fd[0], (char *)&probe[nprobe] + got, sizeof probe[0] - got);
            if (r < 0 && errno == EINTR) continue;
            if (r <= 0) break;
            got += (size_t)r;
        }
        if (got != sizeof probe[0]) break;
    }
    close(fd[0]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) { }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "gate3 run: the process that looks the probes' addresses up did not finish (its report is above)\n");
        exit(2);
    }
}

/* A copy of the module table in which every probe's row of its module's lifted function table
 * is the probe.  A probe of a module that is not in the table, or at an rva where no function
 * starts, is an error of run_probes and ends the driver. */
static const RtModuleDef *wrap_probes(void) {
    unsigned n = gate3_module_def_count;
    RtModuleDef *out = calloc(n + 1, sizeof *out);
    if (!out) { fprintf(stderr, "gate3 run: out of memory\n"); exit(2); }
    memcpy(out, gate3_module_defs, (n + 1) * sizeof *out);
    for (unsigned k = 0; k < nprobe; k++) {
        unsigned i = 0;
        while (i < n && !(out[i].kind == RT_MOD_LIFTED && !strcasecmp(out[i].file, probe[k].module))) i++;
        if (i == n) { fprintf(stderr, "gate3 run: probe of %s, which is not a lifted module\n", probe[k].module); exit(2); }
        const RtLiftedCode *code = out[i].code;
        size_t rows = *code->count;
        if (code == gate3_module_defs[i].code) {                    /* first probe of this module: its own copy */
            LiftedEntry *table = malloc((rows + 1) * sizeof *table);
            RtLiftedCode *copy = malloc(sizeof *copy);
            if (!table || !copy) { fprintf(stderr, "gate3 run: out of memory\n"); exit(2); }
            memcpy(table, code->table, (rows + 1) * sizeof *table);
            *copy = *code;
            copy->table = table;
            out[i].code = code = copy;
        }
        LiftedEntry *table = (LiftedEntry *)code->table;
        size_t lo = 0, hi = rows;
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            if (table[mid].rva < probe[k].rva) lo = mid + 1; else hi = mid;
        }
        if (lo == rows || table[lo].rva != probe[k].rva) {
            fprintf(stderr, "gate3 run: probe %s+%#x [%s]: no lifted function starts there\n", probe[k].module, probe[k].rva, probe[k].what);
            exit(2);
        }
        probe_real[k] = table[lo].fn;
        table[lo].fn = probe_pool[k];
    }
    return out;
}

/* ---- the run ----------------------------------------------------------------------------------------- */

/* How far the run got, for the parent: the child writes one record to the descriptor named by
 * GATE3_RUN_STATUS_FD each time it changes, and the parent keeps the last whole one.  (A pipe
 * and not shared memory: the child is a new program, not a fork.) */
typedef struct { uint32_t called, returned; int32_t result; } RunShared;
static RunShared run_state;
static int run_status_fd = -1;

static void run_tell(void) {
    if (run_status_fd >= 0) write_all(run_status_fd, &run_state, sizeof run_state);
}

/* THE TIME LIMIT.  A run that reaches it explains itself before it dies: after the count
 * table, the wait report of every guest thread goes to descriptor 2 (the run's .stderr file,
 * which every summary prints): what each thread waits on, since when, who signalled that
 * last, and its guest stack as module+rva (rt/thread.h, THE WAIT REPORT OF EVERY GUEST
 * THREAD; `sample <pid> 3` beside it gives the host stacks).  The report holds other threads
 * still while it reads them, so the handler first arms a second limit with the default
 * action: whatever the report runs into, the process ends 10 s later at the latest, by the
 * same signal.  GATE3_NO_WAIT_REPORT=1 leaves the report out.  After the report the items of
 * GATE3_PEEK are logged once more ("peek at the time limit 0").  Nothing of this runs, and
 * nothing is armed, in a run that ends before its limit. */
static void on_alarm(int sig) {
    signal(sig, SIG_DFL);
    alarm(10);
    rt_log_flush_try();
    rt_com_counts_print();              /* the count table and what follows it (the network line): no lock, once */
    const char *no = getenv("GATE3_NO_WAIT_REPORT");
    if (!no || !*no || *no == '0') rt_thread_report_waits(2);
    run_peek("the time limit", 0);      /* GATE3_PEEK: the game's own variables of a run that did not end, into the log */
    rt_log_flush_try();
    raise(sig);
}

static RtModule *load_named(Ctx *ctx, const char *name) {
    WCHAR w[64];
    size_t n = strlen(name);
    for (size_t k = 0; k <= n; k++) w[k] = (WCHAR)(unsigned char)name[k];
    RtModule *m = rt_module_load(ctx, w, 0);
    if (!m) {
        fprintf(stderr, "gate3 run: %s did not load, last error %u (%s)\n", name, rt_thread_get_last_error(),
                gate2_error_name(rt_thread_get_last_error()));
        rt_log_flush();
        _exit(1);
    }
    return m;
}

/* GATE3_PRESENTS (the comment at the top).  The watch runs on the thread that presents. */
#define RUN_EXIT_PRESENTS 77
#define RUN_EXIT_PRESENTS_OPEN 78       /* ended at GATE3_PRESENTS with a file of the state still open for writing: not a clean end */
#define RUN_EXIT_CLOSED 79              /* the host window was closed, and no file of the state was open for writing (THE STATE) */
#define RUN_EXIT_CLOSED_OPEN 80         /* the host window was closed with a file of the state still open for writing: not safe */
#define RUN_END_WAIT_PRESENTS 600u      /* how long the end is put off for such a file (THE STATE at the top) */
static unsigned long run_presents;
static unsigned long run_presents_after_mission;    /* GATE3_PRESENTS=mission+N: N, until the mission's first frame sets run_presents */

static uint64_t run_footprint(void) {
    task_vm_info_data_t info;
    mach_msg_type_number_t n = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&info, &n) != KERN_SUCCESS) return 0;
    return info.phys_footprint;
}

/* GATE3_COUNTS_AT: the count table as it is inside the nth Present (which is counted), the run goes on. */
static unsigned long run_counts_at[16];
static bool          run_counts_rel[16];        /* `mission+N`: N Presents after the mission's first frame (made absolute there) */
static unsigned      run_ncounts_at;

static void run_counts_snapshot(uint64_t nth) {
    static char table[24576], line[700];        /* Present has one caller thread (the game's render thread) */
    rt_log(RT_CH_LOADER, RT_LL_WARN, "gate3 run: count table at present %llu (GATE3_COUNTS_AT): %.3f ms into the run, %llu frame(s) of "
           "the engine's main loop so far (answers of stand-in 5)", (unsigned long long)nth,
           (double)(clock_gettime_nsec_np(CLOCK_MONOTONIC) - run_t0_ns) / 1e6, rt_standin5_answers());
    rt_log_flush();
    const size_t len = rt_com_counts_format(table, sizeof table);
    for (size_t at = 0; at < len; ) {
        const char *nl = memchr(table + at, '\n', len - at);
        const size_t n = nl ? (size_t)(nl - (table + at)) : len - at;
        const int w = snprintf(line, sizeof line, "snapshot %llu: %.*s\n", (unsigned long long)nth, (int)(n < 600 ? n : 600), table + at);
        if (w > 0) rt_log_raw(line, (size_t)w);
        at += n + 1;
    }
}

/* WHAT A RUN REACHED, AND THE RUN OVER TIME (the comment at the top).  Written by the thread
 * that presents; read at any end of the run by whichever thread writes the count table. */
static unsigned run_report_every_s = 10;
static _Atomic uint64_t run_mission_frames, run_mission_first, run_mission_last;
static uint64_t run_dispatch_seen, run_report_due_ns;
static struct { uint64_t t_ns, presents, mission, calls, draws, dispatch, copies; } run_report_last;

/* Every call through an interface slot so far: the number in the count table's heading. */
static uint64_t run_calls_total(void) {
    char head[160];
    rt_com_counts_format(head, sizeof head);
    const char *c = strstr(head, "slots: ");
    return c ? strtoull(c + 7, NULL, 10) : 0;
}

static uint64_t run_draws_total(void) {
    static const char *const draw[] = { "Draw", "DrawIndexed", "DrawInstanced", "DrawIndexedInstanced", "DrawAuto",
                                        "DrawInstancedIndirect", "DrawIndexedInstancedIndirect" };
    uint64_t n = 0;
    for (unsigned i = 0; i < sizeof draw / sizeof *draw; i++) n += rt_com_calls(&rt_iface_ID3D11DeviceContext, draw[i]);
    return n;
}

static void run_report(uint64_t nth, uint64_t t) {
    NdTotals dev;
    rt_nulldev_totals(&dev);
    const uint64_t calls = run_calls_total(), draws = run_draws_total(), mission = atomic_load(&run_mission_frames);
    const uint64_t copies = rt_com_calls(&rt_iface_ID3D11DeviceContext, "CopyResource");
    const double frames = (double)(nth - run_report_last.presents), secs = (double)(t - run_report_last.t_ns) / 1e9;
    const double per = frames > 0 ? frames : 1;
    rt_log(RT_CH_LOADER, RT_LL_WARN, "gate3 run: report: at %.1f s, Present %llu: %.0f Presents in the last %.1f s (%.1f a second), %llu of them "
           "frames of the mission (%llu so far); per Present: %.1f device calls, %.1f draws, %.2f compute dispatches, %.2f CopyResource; "
           "footprint %.3f GB; the device holds %lld buffers and textures, %.1f MB kept for them; %llu frames of the engine's main loop so far",
           (double)(t - run_t0_ns) / 1e9, (unsigned long long)nth, frames, secs, secs > 0 ? frames / secs : 0,
           (unsigned long long)(mission - run_report_last.mission), (unsigned long long)mission,
           (double)(calls - run_report_last.calls) / per, (double)(draws - run_report_last.draws) / per,
           (double)(run_dispatch_seen - run_report_last.dispatch) / per, (double)(copies - run_report_last.copies) / per,
           (double)run_footprint() / 1e9, (long long)dev.resources, (double)dev.bytes / 1e6, rt_standin5_answers());
    run_report_last.t_ns = t; run_report_last.presents = nth; run_report_last.mission = mission; run_report_last.calls = calls;
    run_report_last.draws = draws; run_report_last.dispatch = run_dispatch_seen; run_report_last.copies = copies;
}

/* One line at every end of the run (rt_com_counts_also: no lock, no allocation). */
static FILE *run_frames_f;                  /* GATE3_FRAMES: one line per Present, opened where the other variables are read */
static unsigned run_frames_lines;

static void run_frames_line(void) {
    static char line[400];
    const int n = snprintf(line, sizeof line, "frames: %llu presented; %llu of them frames of the mission's world (a compute dispatch since the "
                           "Present before), the first Present %llu, the last Present %llu\n", (unsigned long long)atomic_load(&run_presents_seen),
                           (unsigned long long)atomic_load(&run_mission_frames), (unsigned long long)atomic_load(&run_mission_first),
                           (unsigned long long)atomic_load(&run_mission_last));
    if (n > 0) rt_log_raw(line, (size_t)n < sizeof line ? (size_t)n : sizeof line - 1);
}

/* THE STATE, at every end (the comment at the top): is a file under the state directory open for
 * writing?  Read with the try-lock, never waited for: this may run on a small signal stack, and
 * the lock may be held by the thread that stopped.  The line's words are the parent's (run_log_state_zero). */
static void run_state_line(void) {
    static char line[300];
    const int w = rt_file_writers_under_try(run_state_dir, NULL, 0);
    const int n = w >= 0
        ? snprintf(line, sizeof line, "state: files under the state directory open for writing at this end: %d\n", w)
        : snprintf(line, sizeof line, "state: files under the state directory open for writing at this end: unknown (the file lock is held by another thread: not waited for)\n");
    if (n > 0) rt_log_raw(line, (size_t)n < sizeof line ? (size_t)n : sizeof line - 1);
}

static void run_end_lines(void) {
    run_standin_counts();
    run_frames_line();
    if (run_frames_f) fflush(run_frames_f);
    run_state_line();
}

static void run_on_present(Ctx *ctx, const RtComMethod *m, uint64_t nth) {
    (void)ctx; (void)m;
    atomic_store(&run_presents_seen, nth);
    rt_input_present(nth);                      /* GATE3_INPUT_SCRIPT: the script's `present` anchors; nothing with no script */
    for (unsigned i = 0; i < run_ncounts_at; i++) if (!run_counts_rel[i] && run_counts_at[i] == nth) run_counts_snapshot(nth);
    for (unsigned i = 0; i < run_nui_at; i++) if (!run_ui_rel[i] && run_ui_at[i] == nth) ui_list_due = nth;   /* GATE3_UI_AT: written by the `!` probe's thread */
    const uint64_t now = clock_gettime_nsec_np(CLOCK_MONOTONIC);
    const uint64_t dispatch = rt_com_calls(&rt_iface_ID3D11DeviceContext, "Dispatch");
    bool frame_of_mission = false;
    if (dispatch != run_dispatch_seen) {        /* a frame of the mission's world (the comment at the top) */
        run_dispatch_seen = dispatch;
        frame_of_mission = true;
        atomic_store(&run_mission_last, nth);
        if (atomic_fetch_add(&run_mission_frames, 1) == 0) {
            atomic_store(&run_mission_first, nth);
            rt_input_mark(RUN_MISSION_MARK);    /* the input script's `anchor mark mission-frame` */
            /* `mission+N` in GATE3_PRESENTS and GATE3_COUNTS_AT: N Presents after this one */
            if (run_presents_after_mission) run_presents = nth + run_presents_after_mission;
            for (unsigned i = 0; i < run_ncounts_at; i++)
                if (run_counts_rel[i]) { run_counts_rel[i] = false; run_counts_at[i] += nth; if (run_counts_at[i] == nth) run_counts_snapshot(nth); }
            for (unsigned i = 0; i < run_nui_at; i++)
                if (run_ui_rel[i]) { run_ui_rel[i] = false; run_ui_at[i] += nth; }
            rt_log(RT_CH_LOADER, RT_LL_WARN, "gate3 run: reached: the first frame of the mission's world (the first Present with a compute "
                   "dispatch before it) is Present %llu, %.3f s into the run, footprint %.3f GB", (unsigned long long)nth,
                   (double)(now - run_t0_ns) / 1e9, (double)run_footprint() / 1e9);
        }
    }
    if (run_frames_f) {                         /* GATE3_FRAMES (the comment at the top) */
        fprintf(run_frames_f, "%.3f %d\n", (double)(now - run_t0_ns) / 1e6, frame_of_mission ? 1 : 0);
        if (++run_frames_lines % 600 == 0) fflush(run_frames_f);
    }
    if (run_report_every_s && now >= run_report_due_ns) {
        if (run_report_due_ns) run_report(nth, now);
        else { run_report_last.t_ns = run_t0_ns; run_report_due_ns = run_t0_ns; }
        while (run_report_due_ns <= now) run_report_due_ns += (uint64_t)run_report_every_s * 1000000000ull;
    }
    if (!run_presents) return;                  /* no GATE3_PRESENTS: nothing per Present */
    const uint64_t t = clock_gettime_nsec_np(CLOCK_MONOTONIC);
    const uint64_t since = run_last_present_ns ? run_last_present_ns : run_t0_ns;
    run_last_present_ns = t;
    rt_log(RT_CH_LOADER, RT_LL_WARN, "gate3 run: present %llu: %.3f ms after %s, %.3f ms into the run, %llu bytes in use "
           "(the process's physical footprint)", (unsigned long long)nth, (double)(t - since) / 1e6,
           nth > 1 ? "the one before" : "the start of the run", (double)(t - run_t0_ns) / 1e6, (unsigned long long)run_footprint());
    if (nth == 1 || nth == run_presents) run_peek("present", nth);
    if (nth < run_presents) return;
    /* THE STATE (the comment at the top): not while a file of the state is open for writing. */
    char open_file[600];
    const unsigned writers = rt_file_writers_under(run_state_dir, open_file, sizeof open_file);
    if (writers && nth < run_presents + RUN_END_WAIT_PRESENTS) {
        if (nth == run_presents)
            rt_log(RT_CH_LOADER, RT_LL_WARN, "gate3 run: the end at present %lu (GATE3_PRESENTS) is put off: %u file(s) under the state "
                   "directory are open for writing (%s); the run goes on until none is, %u Presents at most", run_presents, writers,
                   open_file, RUN_END_WAIT_PRESENTS);
        return;
    }
    rt_log(RT_CH_LOADER, RT_LL_WARN, "gate3 run: ended by the driver inside present %llu (GATE3_PRESENTS=%lu): the call was counted and "
           "its body did not run; the game was not asked to quit; files under the state directory open for writing at this moment: %u%s%s",
           (unsigned long long)nth, run_presents, writers, writers ? ", the first " : "", writers ? open_file : "");
    rt_log_flush();
    rt_com_counts_print();
    rt_log_flush();
    _exit(writers ? RUN_EXIT_PRESENTS_OPEN : RUN_EXIT_PRESENTS);
}

static void run_child(const RtModuleDef *defs, int argc, char **argv) {
    run_start(defs, argc, argv);
    Ctx *ctx = rt_main_ctx();
    run_t0_ns = clock_gettime_nsec_np(CLOCK_MONOTONIC);
    rt_com_counts_also(rt_hostsock_report);     /* THE NETWORK, at the top: one line at every end of the run */
    rt_com_counts_also(run_end_lines);          /* at every end of the run: the stand-ins' counts and the frames (the layer takes 4 such functions; the device's ledger and the input script have theirs) */
    const char *level = getenv("RT_LOG"), *level_by = getenv("GATE3_RUN_LOG_DEFAULT");
    rt_log(RT_CH_LOADER, RT_LL_WARN, "gate3 run: log level: RT_LOG=%s (%s)", level ? level : "(not set: the runtime's own default)",
           level_by && level_by[0] == '1' ? "the driver's default" : "set by the caller");
    /* THE STATE and THE READ-BACK (the comment at the top): what gate3_mode_run decided, for the summary's first lines. */
    const char *start = getenv("GATE3_START_LINE"), *readback = getenv("RT_D3D_READBACK"), *rb_by = getenv("GATE3_READBACK_BY");
    const bool zero = readback && !strcmp(readback, "zero");
    rt_log(RT_CH_LOADER, RT_LL_WARN, "gate3 run: start: %s; the run's working copy: %s", start && *start ? start : "(not said: a run of an older driver)", run_state_dir);
    rt_log(RT_CH_LOADER, RT_LL_WARN, "gate3 run: read-back: RT_D3D_READBACK=%s (%s): %s", zero ? "zero" : "stop", rb_by && *rb_by ? rb_by : "set by the caller",
           zero ? "AN INVENTED ANSWER: a read mapping of contents only a renderer could have produced is answered with zeros, and the "
                  "device's ledger counts each one; GATE3_READBACK=stop gives the device's stop back"
                : "the device stops at a read mapping of contents only a renderer could have produced; nothing is invented there");
    const char *every = getenv("GATE3_REPORT_EVERY");
    if (every && *every) run_report_every_s = (unsigned)strtoul(every, NULL, 10);
    const char *presents = getenv("GATE3_PRESENTS"), *script = getenv("GATE3_INPUT_SCRIPT");
    if (script && *script) {
        rt_input_script_load(script);           /* a file that is not a script ends the run here, with the line */
        rt_com_counts_also(rt_input_report);    /* one line at every end of the run, for the summary's INPUT */
        rt_log(RT_CH_LOADER, RT_LL_WARN, "gate3 run: GATE3_INPUT_SCRIPT=%s: scripted mouse and key input is on", script);
    }
    rt_com_watch(&rt_iface_IDXGISwapChain, "Present", run_on_present);     /* in every run: WHAT A RUN REACHED at the top */
    const char *counts_at = getenv("GATE3_COUNTS_AT");
    for (const char *c = counts_at; c && *c && run_ncounts_at < 16; ) {
        char *end;
        const bool rel = !strncmp(c, "mission+", 8);
        if (rel) c += 8;
        const unsigned long n = strtoul(c, &end, 10);
        if (end == c) break;
        if (n || rel) { run_counts_rel[run_ncounts_at] = rel; run_counts_at[run_ncounts_at++] = n; }
        c = *end == ',' ? end + 1 : end;
    }
    const char *ui_at = getenv("GATE3_UI_AT");
    for (const char *c = ui_at; c && *c && run_nui_at < 16; ) {
        char *end;
        const bool rel = !strncmp(c, "mission+", 8);
        if (rel) c += 8;
        const unsigned long n = strtoul(c, &end, 10);
        if (end == c) break;
        if (n) { run_ui_rel[run_nui_at] = rel; run_ui_at[run_nui_at++] = n; }
        c = *end == ',' ? end + 1 : end;
    }
    const char *frames_path = getenv("GATE3_FRAMES");
    if (frames_path && *frames_path) {
        run_frames_f = fopen(frames_path, "w");
        if (!run_frames_f) { fprintf(stderr, "gate3 run: GATE3_FRAMES=%s: cannot open it for writing\n", frames_path); exit(2); }
        setvbuf(run_frames_f, NULL, _IOFBF, 1 << 20);
    }
    if (presents && !strncmp(presents, "mission+", 8) && strtoul(presents + 8, NULL, 10) > 0) {
        run_presents_after_mission = strtoul(presents + 8, NULL, 10);
        rt_log(RT_CH_LOADER, RT_LL_WARN, "gate3 run: GATE3_PRESENTS=%s: the driver ends the run inside the %lu-th Present after the first frame "
               "of the mission's world; a line per Present from that frame on", presents, run_presents_after_mission);
    } else if (presents && strtoul(presents, NULL, 10) > 0) {
        run_presents = strtoul(presents, NULL, 10);
        rt_log(RT_CH_LOADER, RT_LL_WARN, "gate3 run: GATE3_PRESENTS=%lu: the driver ends the run inside the swap chain's Present number %lu",
               run_presents, run_presents);
    }
    char line[2000] = "";
    for (int i = 0; i < argc; i++) snprintf(line + strlen(line), sizeof line - strlen(line), "%s%s", i ? " " : "", argv[i]);
    rt_log(RT_CH_LOADER, RT_LL_WARN, "gate3 run: game arguments: %s", line);
    /* For trying the time limit, which no run reaches by itself today: GATE3_RUN_HOLD=<seconds>
     * keeps the run here that long (GATE3_TIMEOUT=1 GATE3_RUN_HOLD=3 gate3 run). */
    const char *hold = getenv("GATE3_RUN_HOLD");
    if (hold && atoi(hold) > 0) { rt_log_flush(); sleep((unsigned)atoi(hold)); }
    /* What the original's executable had loaded before the call (finding 30 section 3; finding 37
     * section 2.6): launcher.dll by full path, and through its import table tier0 and vstdlib
     * (OriginSDK.dll and wsock32.dll are native modules here).  tier0 first, then the launcher:
     * the loader brings vstdlib in as the launcher's import, which gives the trace's order. */
    load_named(ctx, "tier0.dll");
    RtModule *launcher = load_named(ctx, "launcher.dll");
    RtModule *exe = rt_module_find("titanfall2.exe");
    GUESTFN f = rt_module_export(launcher, "LauncherMain", 0);
    if (!exe || !f) { fprintf(stderr, "gate3 run: no %s\n", exe ? "export LauncherMain in launcher.dll" : "module for the executable"); _exit(1); }
    /* A STAND-IN FOR EA'S SDK: THE LANGUAGE (docs/handoff/gate3-run-3.md, section 2).  This call
     * is NOT something the original makes.  tier0 keeps a default language id in one word
     * (tier0+0x6bd48), which the file sets to 0x19 (the primary language "Russian") and which
     * only tier0's export Tier0_DetectDefaultLanguage writes (it stores
     * GetUserDefaultUILanguage()).  In a real start nobody calls that export: the executable's
     * whole list of Windows calls has no GetProcAddress for it, and no game module calls
     * GetUserDefaultUILanguage in either recording (finding 39).  The real start's language is
     * EA's: tier0's DetectLanguage (tier0+0xf560) takes the default word's name first and, when
     * Tier0_InitOrigin has set its byte (tier0+0xa9a90), asks the SDK (the import
     * OriginGetAllGameInfoSync at tier0+0xf5b9) and maps the locale code it is given ("en_US"
     * and so on, tier0+0xf8a0) to the game's name for the language.  With -noorigin there is
     * no SDK, the default word decides, and it says Russian: the game would ask for
     * russian*.vpk and r2_russian, which an English install does not have.
     *
     * Until the owner decides the SDK's step (issue 32), this call stands in for the one answer
     * of the SDK the run needs so far: it sets the default word from the user's language as
     * our runtime reports it (0x0409), which gives the language of the recorded start
     * (english).  GATE3_NO_DEFAULT_LANGUAGE=1 leaves the call out.  The summary prints the
     * line below under "STAND-INS". */
    const char *nolang = getenv("GATE3_NO_DEFAULT_LANGUAGE");
    if (!(nolang && nolang[0] == '1')) {
        GUESTFN lang = rt_module_export(rt_module_find("tier0.dll"), "Tier0_DetectDefaultLanguage", 0);
        if (!lang) { fprintf(stderr, "gate3 run: no export Tier0_DetectDefaultLanguage in tier0.dll\n"); _exit(1); }
        rt_log(RT_CH_LOADER, RT_LL_WARN, "gate3 run: stand-in: the language: tier0's Tier0_DetectDefaultLanguage() is called by our entry point; "
               "in a real start EA's SDK supplies the language and nobody makes this call (issue 32)");
        GUEST_CALL(ctx, lang);
    }
    run_check_origin(false);            /* the rule again (first checked in run_start, before tier0.dll was loaded) */
    run_log_standins();
    rt_log(RT_CH_LOADER, RT_LL_WARN, "gate3 run: calling LauncherMain(%#llx, 0, 0, 0) at launcher.dll+%#llx host-first-thread=%d", (unsigned long long)exe->base,
           (unsigned long long)(f - launcher->base), pthread_main_np());
    run_state.called = 1;
    run_tell();
    uint64_t r = GUEST_CALL(ctx, f, exe->base, 0, 0, 0);
    run_state.result = (int32_t)r;
    run_state.returned = 1;
    run_tell();
    rt_log(RT_CH_LOADER, RT_LL_WARN, "gate3 run: LauncherMain returned %d", (int32_t)r);
    rt_log_flush();
    rt_com_counts_print();              /* the calls through interface slots (rt/com.h); a stop prints them itself */
    fflush(stdout);
}

/* ---- the summary: one pass over the log -------------------------------------------------------------- */

static const struct { int step; const char *text; } step_text[] = {
    {  1, "LauncherMain entered: command line read" },
    {  2, "base directory from the executable's path" },
    {  3, "WSAStartup" },
    {  4, "the singleton mutex" },
    {  5, "SetThreadExecutionState, working directory" },
    {  7, "tier0 ConfigureAndInitJobThreads: the job threads" },
    {  8, "tier0 Pak_LoadDllIfNeeded: rtech_game loaded" },
    {  9, "the pak loader started" },
    { 10, "Tier0_InitOrigin: EA's SDK (not expected with -noorigin)" },
    { 12, "outer group create: cvar, filesystem_stdio loaded" },
    { 14, "outer group main: runs the inner group" },
    { 15, "inner group create: engine and the subsystem DLLs loaded" },
    { 16, "the material system told its mode" },
    { 17, "dependencies: localize loaded" },
    { 18, "connection stage begun" },
    { 19, "material system connect: render thread, Direct3D device" },
    { 20, "engine connect" },
    { 21, "inner group pre-init: game directory, start-up block" },
    { 22, "initialization stage: each system's init (the engine's is the ninth by the replayed order)" },
    { 23, "inner group main: the engine's run" },
    { 24, "the game window" },
};

#define SM_MODS 64
#define SM_THREADS 64
#define SM_DEPTH 24
#define SM_NAMES 400
typedef struct { char name[40]; unsigned long long base; int mapped, order; char by[200]; } SmMod;   /* order: 0 = its load did not finish */
typedef struct { unsigned tid; int depth; char frame[SM_DEPTH][200]; int item[SM_DEPTH]; unsigned long calls; } SmThread;
typedef struct { char text[230]; unsigned count, failed; } SmName;

static SmMod    sm_mod[SM_MODS];
static unsigned sm_nmod, sm_nloaded;
static SmThread sm_thr[SM_THREADS];
static unsigned sm_nthr;
static SmName   sm_file[SM_NAMES], sm_reg[SM_NAMES], sm_iface[SM_NAMES], sm_newthr[SM_NAMES], sm_out[SM_NAMES], sm_noload[SM_NAMES];
static unsigned sm_nfile, sm_nreg, sm_niface, sm_nnewthr, sm_nout, sm_nnoload;
static SmName   sm_err[8];                      /* calls of tier0's Error: format, first string argument, the thread */
static unsigned sm_nerr;
static bool     sm_step[40];
static int      sm_loading[16], sm_nloading;

static int name_add(SmName *v, unsigned *n, const char *text) {
    for (unsigned i = 0; i < *n; i++) if (!strcmp(v[i].text, text)) { v[i].count++; return (int)i; }
    if (*n == SM_NAMES) return -1;
    snprintf(v[*n].text, sizeof v[*n].text, "%s", text);
    v[*n].count = 1;
    return (int)(*n)++;
}

static SmThread *thr_of(unsigned tid) {
    for (unsigned i = 0; i < sm_nthr; i++) if (sm_thr[i].tid == tid) return &sm_thr[i];
    if (sm_nthr == SM_THREADS) return &sm_thr[SM_THREADS - 1];
    sm_thr[sm_nthr].tid = tid;
    return &sm_thr[sm_nthr++];
}

/* "module+0xrva" for a guest address inside a lifted module, else the address. */
static const char *addr_text(unsigned long long a, char *buf, size_t n) {
    const SmMod *best = NULL;
    for (unsigned i = 0; i < sm_nmod; i++)
        if (sm_mod[i].base && sm_mod[i].base <= a && (!best || sm_mod[i].base > best->base)) best = &sm_mod[i];
    if (best && a - best->base < 0x15000000ull) snprintf(buf, n, "%s+%#llx", best->name, a - best->base);
    else snprintf(buf, n, "%#llx", a);
    return buf;
}

static SmMod *mod_named(const char *name, size_t len) {
    for (unsigned i = 0; i < sm_nmod; i++) if (strlen(sm_mod[i].name) == len && !strncmp(sm_mod[i].name, name, len)) return &sm_mod[i];
    if (sm_nmod == SM_MODS || len >= sizeof sm_mod[0].name) return NULL;
    memcpy(sm_mod[sm_nmod].name, name, len);
    return &sm_mod[sm_nmod++];
}

/* The first "quoted" text of a call's arguments, without the quotes ("" if none). */
static void first_quoted(const char *s, char *out, size_t n) {
    const char *a = strchr(s, '"');
    const char *b = a ? strrchr(a + 1, '"') : NULL;
    const char *c = a ? strstr(a + 1, "\", ") : NULL;           /* the first argument's closing quote */
    if (c && c < b) b = c;
    if (c == NULL && a) { const char *d = strstr(a + 1, "\")"); if (d) b = d; }
    out[0] = 0;
    if (a && b && b > a) snprintf(out, n, "%.*s", (int)(b - a - 1), a + 1);
}

static bool starts(const char *s, const char *p) { return !strncmp(s, p, strlen(p)); }

static void summary_trace(char *s, unsigned last_tid_out[1], bool launcher_called) {
    char *colon = strchr(s, ':');
    if (!colon) return;
    unsigned tid = (unsigned)strtoul(s, NULL, 16);
    SmThread *t = thr_of(tid);
    char *e = colon + 1;
    last_tid_out[0] = tid;
    if (starts(e, "Call ")) {
        const char *call = e + 5;
        t->calls++;
        int item = -1;
        char q[200], text[230];
        if (strstr(call, ".CreateFile") || strstr(call, ".GetFileAttributes") || strstr(call, ".FindFirstFile") || strstr(call, ".CreateDirectory")) {
            first_quoted(call, q, sizeof q);
            snprintf(text, sizeof text, "%.*s %s", (int)strcspn(call, "("), call, q);
            item = name_add(sm_file, &sm_nfile, text);
        } else if (strstr(call, ".RegOpenKey") || strstr(call, ".RegCreateKey") || strstr(call, ".RegQueryValue")) {
            first_quoted(call, q, sizeof q);
            snprintf(text, sizeof text, "%.*s %s", (int)strcspn(call, "("), call, q);
            item = name_add(sm_reg, &sm_nreg, text) | 0x10000;
        } else if (strstr(call, ".CreateThread(")) {
            unsigned long long start = 0;
            const char *a = strchr(call, '(');
            for (int k = 0; a && k < 2; k++) a = strchr(a + 1, ',');
            if (a) start = strtoull(a + 1, NULL, 16);
            char st[80];
            const char *ret = strstr(call, " ret=");
            snprintf(text, sizeof text, "by thread %x from %s, start routine %s", tid, ret ? ret + 5 : "?", addr_text(start, st, sizeof st));
            name_add(sm_newthr, &sm_nnewthr, text);
        } else if (!starts(call, "kernel32.") && !starts(call, "ntdll.")) {
            snprintf(text, sizeof text, "%.*s", (int)strcspn(call, "("), call);
            name_add(sm_out, &sm_nout, text);
        }
        const char *ret = strstr(call, " ret=launcher.dll+");
        if (ret && launcher_called) {
            if (strstr(call, ".GetCommandLineA(")) sm_step[1] = true;
            if (strstr(call, ".GetModuleFileNameA(")) sm_step[2] = true;
            if (strstr(call, ".WSAStartup(")) sm_step[3] = true;
            if (strstr(call, ".CreateMutexA(")) sm_step[4] = true;
            if (strstr(call, ".SetThreadExecutionState(")) sm_step[5] = true;
        }
        if (strstr(call, ".D3D11CreateDevice(")) sm_step[19] = true;
        if (strstr(call, ".CreateWindowExW(")) sm_step[24] = true;
        if (t->depth < SM_DEPTH) { snprintf(t->frame[t->depth], sizeof t->frame[0], "%s", call); t->item[t->depth] = item; }
        t->depth++;
    } else if (starts(e, "Ret ")) {
        if (t->depth > 0) t->depth--;
        if (t->depth < SM_DEPTH && strstr(t->frame[t->depth], ".LoadLibrary") && strstr(e, "retval=00000000 ")) {
            char q[160], text[230];
            const char *ret = strstr(t->frame[t->depth], " ret=");
            first_quoted(t->frame[t->depth], q, sizeof q);
            snprintf(text, sizeof text, "%s, asked by %s", q, ret ? ret + 5 : "?");
            name_add(sm_noload, &sm_nnoload, text);
        }
        if (t->depth < SM_DEPTH && t->item[t->depth] >= 0) {
            int item = t->item[t->depth];
            const char *rv = strstr(e, "retval=");
            unsigned long long v = rv ? strtoull(rv + 7, NULL, 16) : 1;
            bool file = !(item & 0x10000);
            bool failed = file ? (v == 0 || v == 0xffffffffull || v == 0xffffffffffffffffull) : v != 0;
            if (failed) (file ? sm_file : sm_reg)[item & 0xffff].failed++;
        }
    } else if (starts(e, "Probe ")) {
        const char *st = strstr(e, " step ");
        int step = st ? atoi(st + 6) : 0;
        if (step > 0 && step < 40) sm_step[step] = true;
        const char *r = strstr(e, " rcx=");
        const char *err = strstr(e, "[tier0 Error");
        if (err && sm_nerr < 8) {
            const char *q = strchr(err, ']');
            snprintf(sm_err[sm_nerr].text, sizeof sm_err[0].text, "on thread %x:%.*s", tid, (int)(r && q && r > q ? r - q - 1 : 0), q ? q + 1 : "");
            sm_err[sm_nerr++].count = 1;
        }
        if (t->depth < SM_DEPTH) {
            snprintf(t->frame[t->depth], sizeof t->frame[0], "in %.*s", (int)(r ? r - (e + 6) : (long)strlen(e + 6)), e + 6);
            t->item[t->depth] = -1;
        }
        t->depth++;
    } else if (starts(e, "ProbeRet ")) {
        if (t->depth > 0) t->depth--;
        if (strstr(e, "[CreateInterface]")) {
            char q[80], text[230];
            first_quoted(e, q, sizeof q);
            const char *rax = strstr(e, " rax=");
            snprintf(text, sizeof text, "%-34s from %.*s = %s", q, (int)strcspn(e + 9, "+"), e + 9, rax && strtoull(rax + 5, NULL, 16) ? "an object" : "NULL");
            name_add(sm_iface, &sm_niface, text);
        }
    }
}

static void print_names(const char *title, const SmName *v, unsigned n, unsigned show, bool with_failed) {
    printf("%s (%u distinct%s):\n", title, n, n > show ? ", the first ones shown" : "");
    for (unsigned i = 0; i < n && i < show; i++) {
        printf("    %s", v[i].text);
        if (v[i].count > 1) printf("   x%u", v[i].count);
        if (with_failed && v[i].failed) printf("   (%s)", v[i].failed == v[i].count ? "failed" : "failed some of the times");
        printf("\n");
    }
}

/* What the run wrote to descriptor 2: its first 60 lines. */
static void summary_report(const char *report_path) {
    FILE *r = report_path ? fopen(report_path, "r") : NULL;
    if (!r) return;
    unsigned n = 0;
    char buf[1000];
    while (fgets(buf, sizeof buf, r)) {
        if (!n) printf("what the run wrote to descriptor 2 (%s):\n", report_path);
        if (n++ < 60) printf("    %s%s", buf, strchr(buf, '\n') ? "" : "\n");
    }
    if (n > 60) printf("    ... %u more lines\n", n - 60);
    fclose(r);
}

int gate3_mode_summary(const char *log_path, const char *report_path) {
    FILE *f = fopen(log_path, "r");
    if (!f) { perror(log_path); summary_report(report_path); return 2; }       /* a run that ended before its log began */
    static char big[1 << 20];
    setvbuf(f, big, _IOFBF, sizeof big);
    char *line = NULL;
    size_t cap = 0;
    ssize_t len;
    unsigned long nlines = 0;
    unsigned last_tid = 0, ndone = 0;
    bool called = false;
    char args[2000] = "", result[200] = "did not return", exit_line[300] = "";
    static char stand[24][900], answer[16][700], net[900], level[200], input[700];
    /* THE STATE, THE READ-BACK, WHAT A RUN REACHED (the comment at the top): the driver's own lines. */
    static char start[1900], readback[600], menu[24][300], reached[400], frames[400], reports[120][700], device[16][400];
    unsigned nmenu = 0, menu_more = 0, nreports = 0, reports_more = 0, ndevice = 0;
    bool main_opened = false, main_left = false;
    unsigned nstand = 0, nanswer[16] = { 0 };       /* answers by the stand-in's number (one or two digits) */
    unsigned long long ncounted[16] = { 0 };        /* the same from the "standins: counted" line, for those that log only their first */
    bool counted[16] = { false };
    static char counted_line[400];                  /* the "standins: counted" line as written */
    static char fatal[80][240];                 /* the fatal report: from the first line that begins with FATAL on */
    unsigned nfatal = 0, fatal_more = 0;
    static char com[480][200];                  /* the count table of the interface layer (rt/com.h): heading and rows */
    unsigned ncom = 0, com_more = 0;
    /* A fatal report is written without waiting for the log's lock (core/report.h), so it can
     * begin in the middle of a line that another thread was writing: "trace: 1004:Ret  kFATAL
     * unimplemented import ...".  Such a line is taken as the report's first when the line
     * after it is indented as a report's second line is; `cut` keeps it until then. */
    char cut[240] = "";
    bool fatal_cut = false;
    while ((len = getline(&line, &cap, f)) > 0) {
        nlines++;
        if (line[len - 1] == '\n') line[--len] = 0;
        if (!nfatal && cut[0]) {
            if (starts(line, "  ")) { snprintf(fatal[nfatal++], sizeof fatal[0], "%s", cut); fatal_cut = true; }
            cut[0] = 0;
        }
        /* The table is written in one piece after a fatal report (or when LauncherMain returns),
         * so only its heading can begin in the middle of another thread's line. */
        const char *head = strstr(line, "com: calls through interface slots: ");
        if (head || (ncom && starts(line, "com: ") && (line[5] == ' ' || isdigit((unsigned char)line[5])))) {
            if (ncom < 480) snprintf(com[ncom++], sizeof com[0], "%s", head ? head : line); else com_more++;
            continue;
        }
        /* The network line is written after the table, inside a fatal report as well: before the
         * report's own lines are taken. */
        const char *hs = strstr(line, "net: host sockets: ");
        if (hs) { snprintf(net, sizeof net, "%s", hs + 19); continue; }
        if (starts(line, "input: script ")) { snprintf(input, sizeof input, "%s", line + 14); continue; }
        if (starts(line, "frames: ")) { snprintf(frames, sizeof frames, "%s", line + 8); continue; }
        if (starts(line, "d3d: ")) { if (ndevice < 16) snprintf(device[ndevice++], sizeof device[0], "%s", line + 5); continue; }
        if (starts(line, "standins: counted: ")) {      /* the stand-ins that log their first answer only: the counts at the end */
            snprintf(counted_line, sizeof counted_line, "%s", line + 19);
            for (const char *c = line; (c = strstr(c, "stand-in ")) != NULL; c += 9) {
                unsigned k = 0; unsigned long long n = 0;
                if (sscanf(c, "stand-in %u answered %llu", &k, &n) == 2 && k < 16) { counted[k] = true; ncounted[k] = n; }
            }
            continue;
        }
        if (nfatal || starts(line, "FATAL")) {
            if (nfatal < 80) snprintf(fatal[nfatal++], sizeof fatal[0], "%s", line); else fatal_more++;
            continue;
        }
        const char *mid = strstr(line, "FATAL ");
        if (mid) { snprintf(cut, sizeof cut, "%s", mid); continue; }
        if (starts(line, "trace: ")) { summary_trace(line + 7, &last_tid, called); continue; }
        if (starts(line, "loader: gate3 run: game arguments: ")) { snprintf(args, sizeof args, "%s", line + 35); continue; }
        if (starts(line, "loader: gate3 run: log level: ")) { snprintf(level, sizeof level, "%s", line + 30); continue; }
        if (starts(line, "loader: gate3 run: start: ")) { snprintf(start, sizeof start, "%s", line + 26); continue; }
        if (starts(line, "loader: gate3 run: read-back: ")) { snprintf(readback, sizeof readback, "%s", line + 30); continue; }
        if (starts(line, "loader: gate3 run: reached: ")) { snprintf(reached, sizeof reached, "%s", line + 28); continue; }
        if (starts(line, "loader: gate3 run: report: ")) {
            if (nreports < 120) snprintf(reports[nreports++], sizeof reports[0], "%s", line + 27); else reports_more++;
            continue;
        }
        if (starts(line, "loader: gate3 run: menu: ")) {
            if (nmenu < 24) snprintf(menu[nmenu++], sizeof menu[0], "%s", line + 25); else menu_more++;
            if (starts(line + 25, "opened menu_MainMenu ")) main_opened = true;
            else if (main_opened && starts(line + 25, "closed menu_MainMenu ")) main_left = true;
            continue;
        }
        if (starts(line, "loader: gate3 run: stand-in: ")) { if (nstand < 24) snprintf(stand[nstand++], sizeof stand[0], "%s", line + 29); continue; }
        if (starts(line, "loader: stand-in ") && isdigit((unsigned char)line[17])) {       /* "stand-in N answered: " with N of one or two digits */
            const unsigned two = isdigit((unsigned char)line[18]) ? 1 : 0;
            const unsigned k = two ? (unsigned)(line[17] - '0') * 10 + (unsigned)(line[18] - '0') : (unsigned)(line[17] - '0');
            if (k < 16 && starts(line + 18 + two, " answered: ")) {
                if (!nanswer[k]++) snprintf(answer[k], sizeof answer[0], "%s", line + 29 + two);
                continue;
            }
        }
        if (starts(line, "loader: gate3 run: calling LauncherMain")) { called = true; continue; }
        if (starts(line, "loader: gate3 run: LauncherMain returned ")) { snprintf(result, sizeof result, "returned %s", line + 41); continue; }
        if (starts(line, "thread: process exit: ")) { snprintf(exit_line, sizeof exit_line, "%s", line + 8); continue; }
        if (!starts(line, "loader: ")) continue;
        const char *name = line + 8, *rest = strstr(name, ": ");
        if (!rest) continue;
        size_t nlen = (size_t)(rest - name);
        rest += 2;
        if (starts(rest, "base 0x")) {
            SmMod *m = mod_named(name, nlen);
            if (m) m->base = strtoull(rest + 5, NULL, 16);
        } else if (starts(rest, "image built at ")) {
            SmMod *m = mod_named(name, nlen);
            if (!m) continue;
            m->mapped = (int)++sm_nloaded;
            if (sm_nloading > 0) snprintf(m->by, sizeof m->by, "import of %s", sm_mod[sm_loading[sm_nloading - 1]].name);
            else {
                /* The open LoadLibrary call: on the thread traced last, else on any thread. */
                const char *who = NULL;
                for (unsigned pass = 0; pass < 2 && !who; pass++)
                    for (unsigned ti = 0; ti < sm_nthr && !who; ti++) {
                        const SmThread *t = &sm_thr[ti];
                        if ((pass == 0) != (t->tid == last_tid)) continue;
                        for (int d = t->depth < SM_DEPTH ? t->depth - 1 : SM_DEPTH - 1; d >= 0 && !who; d--)
                            if (strstr(t->frame[d], ".LoadLibrary")) who = t->frame[d];
                    }
                if (who) {
                    const char *ret = strstr(who, " ret=");
                    char q[160];
                    first_quoted(who, q, sizeof q);
                    snprintf(m->by, sizeof m->by, "%s, %.*s(\"%s\")", ret ? ret + 5 : "?", (int)strcspn(who, "("), who, q);
                } else snprintf(m->by, sizeof m->by, "%s", called ? "? (no LoadLibrary call open on any thread)" : "the driver");
            }
            if (sm_nloading < 16) sm_loading[sm_nloading++] = (int)(m - sm_mod);
            if (!strcmp(m->name, "localize.dll")) sm_step[17] = true;
        } else if (starts(rest, "loaded at ")) {
            SmMod *m = mod_named(name, nlen);
            if (m) m->order = (int)++ndone;
            if (sm_nloading > 0) sm_nloading--;
        }
    }
    free(line);
    fclose(f);

    unsigned long calls = 0;
    for (unsigned i = 0; i < sm_nthr; i++) calls += sm_thr[i].calls;
    printf("gate3 run: summary of %s (%lu lines, %lu calls into Windows functions on %u threads)\n", log_path, nlines, calls, sm_nthr);
    printf("game arguments: %s\n", args);
    printf("log level: %s\n", level[0] ? level : "not in the log (a run of a driver before gate3-run-7)");
    printf("START: %s\n", start[0] ? start : "not in the log (a run of a driver before gate3-run-13, or one that ended before its start was logged)");
    printf("READ-BACK: %s\n", readback[0] ? readback : "not in the log (a run of a driver before gate3-run-13)");
    /* No socket of the guest may reach beyond loopback (rt/hostsock.h, THE RULE). */
    printf("NETWORK (host sockets): %s\n", net[0] ? net : "NO LINE IN THE LOG: the run ended without its count table (killed from outside?)");
    /* GATE3_INPUT_SCRIPT: printed only for a run that had one (a run without is as before). */
    if (input[0]) printf("INPUT (script): %s\n", input);
    /* What our entry point does in place of something the original gets elsewhere (EA's SDK). */
    printf("STAND-INS (what this run does that the original does not):%s\n", nstand ? "" : " none");
    for (unsigned i = 0; i < nstand; i++) printf("    %s\n", stand[i]);
    /* A stand-in that is called every frame logs its first answer and counts the rest: its
     * number is the one of the run's "standins: counted" line, written at every end. */
    unsigned long long answers = 0;
    for (unsigned k = 0; k < 16; k++) answers += counted[k] ? ncounted[k] : nanswer[k];
    printf("    answers given by a stand-in for an import in this run: %llu%s\n", answers, answers ? ", by stand-in:" : "");
    for (unsigned k = 0; k < 16; k++) {
        if (counted[k] && ncounted[k])
            printf("        stand-in %u answered %llu time(s) (counted; only the first is in the log); the first: %s\n", k, ncounted[k],
                   nanswer[k] ? answer[k] : "(its line is not in the log)");
        else if (nanswer[k])
            printf("        stand-in %u answered %u time(s); the first: %s\n", k, nanswer[k], answer[k]);
    }
    if (counted_line[0]) printf("        counted at the run's end: %s\n", counted_line);
    printf("LauncherMain: %s%s\n", called ? "" : "NOT CALLED; ", result);
    if (exit_line[0]) printf("the guest ended the process: %s\n", exit_line);
    int last = 0;
    for (unsigned i = 0; i < sizeof step_text / sizeof *step_text; i++) if (sm_step[step_text[i].step]) last = step_text[i].step;
    printf("steps of finding 37 seen:");
    for (unsigned i = 0; i < sizeof step_text / sizeof *step_text; i++) if (sm_step[step_text[i].step]) printf(" %d", step_text[i].step);
    printf("\n");
    /* WHAT A RUN REACHED (the comment at the top).  Until unit gate3-run-13 this was "LAST STEP
     * REACHED", the last step of finding 37, which has been 23 (the engine's run) in every run
     * since the engine started and said nothing about the menu, the load or the mission. */
    unsigned long long presented = 0, mission = 0, first = 0, lastp = 0;
    const bool have_frames = sscanf(frames, "%llu presented; %llu of them frames of the mission's world (a compute dispatch since the Present before), "
                                    "the first Present %llu, the last Present %llu", &presented, &mission, &first, &lastp) == 4;
    const char *step = "no step of finding 37";
    for (unsigned i = 0; i < sizeof step_text / sizeof *step_text; i++) if (step_text[i].step == last) step = step_text[i].text;
    if (!have_frames) printf("REACHED: step %d of finding 37 (%s); the log has no `frames` line (a run of a driver before gate3-run-13, or one ended from outside)\n", last, step);
    else if (mission) printf("REACHED: FRAMES OF THE MISSION'S WORLD: %llu of the %llu Presents (the first is Present %llu, the last Present %llu)\n", mission, presented, first, lastp);
    else if (main_left) printf("REACHED: PAST THE MAIN MENU (the UI opened it and closed it: a video or a load); %llu Presents, none of them a frame of the mission's world\n", presented);
    else if (main_opened) printf("REACHED: THE MAIN MENU (the UI opened it and has not closed it); %llu Presents\n", presented);
    else if (presented) printf("REACHED: THE START'S FRAMES: %llu Presents; the UI has not opened the main menu\n", presented);
    else printf("REACHED: step %d of finding 37 (%s); nothing was presented\n", last, step);
    for (unsigned i = 0; i < nmenu; i++) printf("    menu: %s\n", menu[i]);
    if (menu_more) printf("    ... %u more menu lines\n", menu_more);
    if (reached[0]) printf("    %s\n", reached);
    if (nreports) printf("OVER TIME (GATE3_REPORT_EVERY; counts are since the line before):\n");
    for (unsigned i = 0; i < nreports; i++) printf("    %s\n", reports[i]);
    if (reports_more) printf("    ... %u more report lines (in the log: grep 'gate3 run: report: ')\n", reports_more);
    printf("modules loaded (%u), in the order in which their loads finished, and who asked:\n", sm_nloaded);
    for (unsigned o = 1; o <= ndone; o++)
        for (unsigned i = 0; i < sm_nmod; i++)
            if (sm_mod[i].order == (int)o) printf("    %2u %-26s %s\n", o, sm_mod[i].name, sm_mod[i].by);
    for (unsigned i = 0; i < sm_nmod; i++)
        if (sm_mod[i].mapped && !sm_mod[i].order) printf("     - %-26s %s   (ITS LOAD DID NOT FINISH)\n", sm_mod[i].name, sm_mod[i].by);
    print_names("modules asked for by name that did not load", sm_noload, sm_nnoload, SM_NAMES, false);
    print_names("interfaces asked for", sm_iface, sm_niface, SM_NAMES, false);
    print_names("threads created", sm_newthr, sm_nnewthr, SM_NAMES, false);
    print_names("files asked for", sm_file, sm_nfile, 60, true);
    print_names("registry", sm_reg, sm_nreg, 40, true);
    print_names("Windows functions called outside kernel32", sm_out, sm_nout, SM_NAMES, false);
    printf("where each thread was when the log ends (innermost first; a call that had not returned):\n");
    for (unsigned i = 0; i < sm_nthr; i++) {
        const SmThread *t = &sm_thr[i];
        printf("    thread %x, %lu calls:%s\n", t->tid, t->calls, t->depth ? "" : " in the game's own code (no call open)");
        for (int d = t->depth < SM_DEPTH ? t->depth - 1 : SM_DEPTH - 1; d >= 0; d--) printf("        %s\n", t->frame[d]);
    }
    printf("calls through interface slots, per method (the layer's count table):%s\n", ncom ? "" : " none");
    for (unsigned i = 0; i < ncom; i++) printf("    %s\n", com[i]);
    if (com_more) printf("    ... %u more lines\n", com_more);
    if (sm_nerr) print_names("the game called its fatal-error routine (tier0 Error; the format, and its first argument when that is a string)", sm_err, sm_nerr, 8, false);
    printf("THE DEVICE'S LEDGER (what the do-nothing device invented, held and was switched to; the run's last lines `d3d:`):%s\n", ndevice ? "" : " none in the log");
    for (unsigned i = 0; i < ndevice; i++) printf("    %s\n", device[i]);
    printf("THE STOP (the fatal report in the log):%s\n", nfatal ? "" : " none");
    if (fatal_cut) printf("    (it begins in the middle of a line another thread was writing)\n");
    for (unsigned i = 0; i < nfatal; i++) printf("    %s\n", fatal[i]);
    if (fatal_more) printf("    ... %u more lines\n", fatal_more);
    summary_report(report_path);
    return 0;
}

/* ---- the mode ------------------------------------------------------------------------------------------ */

/* The guest's run on its host thread (run_here): everything from run_child to the end of the run. */
struct guest_host_args { const RtModuleDef *defs; int gargc; char **gargv; const char *log_path; };

static void *run_guest_host_thread(void *arg) {
    struct guest_host_args *a = arg;
    sigset_t sigalrm;
    sigemptyset(&sigalrm);
    sigaddset(&sigalrm, SIGALRM);
    pthread_sigmask(SIG_UNBLOCK, &sigalrm, NULL);   /* the time limit is handled on this thread */
    run_child(a->defs, a->gargc, a->gargv);
    if (run_status_fd >= 0) _exit(0);
    alarm(0);
    printf("gate3 run: LauncherMain returned %d\n", run_state.result);
    gate3_mode_summary(a->log_path, NULL);
    printf("STATE: left at %s (a run by hand in the driver's own process keeps nothing; the working copy is never the start of a run)\n", run_state_dir);
    fflush(stdout);
    _exit(0);                                       /* as the child: guest threads may still run */
}

/* The run in this process: the child that `gate3 run` starts, or the user's `--in-process`. */
/* The host window's close button ends the run (rt/host.h: called on the first thread). */
static void run_host_closed(void) {
    char open_file[600];
    const unsigned writers = rt_file_writers_under(run_state_dir, open_file, sizeof open_file);   /* THE STATE: waits for the lock; not a signal */
    fprintf(stderr, "gate3 run: the host window was closed%s\n", writers ? ", WITH A FILE OF THE STATE STILL OPEN FOR WRITING" : "");
    rt_log_flush();
    rt_com_counts_print();                      /* every end: the state line (run_end_lines) and the counts */
    rt_log_flush();
    _exit(writers ? RUN_EXIT_CLOSED_OPEN : RUN_EXIT_CLOSED);
}

static int run_here(int gargc, char **gargv, const char *log_path, unsigned timeout_s) {
    const char *first_env = getenv("GATE3_GUEST_ON_FIRST_THREAD");
    if (rt_host_window_wanted() && first_env && first_env[0] == '1') {
        fprintf(stderr, "gate3 run: RT_HOST_WINDOW and GATE3_GUEST_ON_FIRST_THREAD=1 cannot both be set: the host window's event loop needs the first thread\n");
        exit(2);
    }
    const char *sfd = getenv("GATE3_RUN_STATUS_FD");
    if (sfd && *sfd) run_status_fd = atoi(sfd);
    else unlink(log_path);                          /* by hand: the parent has not made the log new */
    resolve_probes();
    /* GATE3_PROBE=module+0xRVA[@|%|^|~|&Name|!][,...]: more probes for this run, each at the start of a lifted
     * function (anything else ends the driver in wrap_probes).  The log gets the thread, three
     * argument registers and who called, for every call that goes through the function table. */
    const char *more = getenv("GATE3_PROBE");
    if (more && *more) {
        static char buf[8192];
        snprintf(buf, sizeof buf, "%s", more);
        for (char *item = strtok(buf, ","); item; item = strtok(NULL, ",")) {
            char *plus = strchr(item, '+');
            if (!plus || nprobe == RUN_MAX_PROBES || (size_t)(plus - item) > sizeof probe[0].module - 5) {
                fprintf(stderr, "gate3 run: GATE3_PROBE item '%s': not module+0xRVA, or no room for another probe\n", item);
                exit(2);
            }
            RunProbe p = { .rva = (uint32_t)strtoul(plus + 1, NULL, 16) };
            if (item[strlen(item) - 1] == '@') p.flags = PF_REC;    /* module+0xRVA@: rcx is a record, print its first words and r9 */
            if (item[strlen(item) - 1] == '%') p.flags = PF_FMT;    /* module+0xRVA%: rcx is a printf format (tier0's Msg and the like) */
            if (item[strlen(item) - 1] == '~') p.flags = PF_AUD;    /* module+0xRVA~: a decoder's frames routine: statistics of what it handed out */
            if (item[strlen(item) - 1] == '^') p.flags = PF_TEXTS;  /* module+0xRVA^: r8 and r9 as texts where they are text (a logging function whose message is its third or fourth argument) */
            char *sfx = plus + 1 + strspn(plus + 1, "0123456789abcdefABCDEFxX");
            if (*sfx == '&') { p.flags = PF_SCRIPT; snprintf(probe_label[nprobe], sizeof probe_label[0], "%s", sfx + 1); }  /* module+0xRVA&Name: a script native (THE UI PROBE) */
            if (*sfx == '!') p.flags = PF_TICK;                     /* module+0xRVA!: where a GATE3_UI_AT listing is written */
            snprintf(p.module, sizeof p.module, "%.*s.dll", (int)(plus - item), item);
            snprintf(p.what, sizeof p.what, "GATE3_PROBE");
            probe[nprobe++] = p;
        }
    }
    /* THE MENU MARK (the comment at the top): the two natives, in every run.  One that
     * GATE3_PROBE named already is that probe, with the mark added. */
    static const struct { uint32_t rva; const char *label; } menus[] = { { RUN_MENU_OPEN_RVA, "OpenMenu" }, { RUN_MENU_CLOSE_RVA, "CloseMenu" } };
    for (unsigned i = 0; i < sizeof menus / sizeof *menus; i++) {
        unsigned k = 0;
        while (k < nprobe && !(probe[k].rva == menus[i].rva && !strcmp(probe[k].module, RUN_MENU_MODULE))) k++;
        if (k == nprobe) {
            if (nprobe == RUN_MAX_PROBES) { fprintf(stderr, "gate3 run: no room for the menu mark's probes: GATE3_PROBE has too many items\n"); exit(2); }
            RunProbe p = { .rva = menus[i].rva };
            snprintf(p.module, sizeof p.module, "%s", RUN_MENU_MODULE);
            snprintf(p.what, sizeof p.what, "the menu mark");
            probe[nprobe++] = p;
        }
        probe[k].flags = PF_SCRIPT | PF_MENU;
        snprintf(probe_label[k], sizeof probe_label[0], "%s", menus[i].label);
    }
    /* GATE3_CONSOLE (the comment above run_console_tick): the command, and the `!` probe it is handed over in. */
    const char *con = getenv("GATE3_CONSOLE");
    if (con && *con) {
        char *colon = NULL;
        run_console_after_s = strtod(con, &colon);
        if (colon == con || !colon || *colon != ':' || !colon[1] || run_console_after_s < 0 || strlen(colon + 1) > sizeof run_console_text - 2) {
            fprintf(stderr, "gate3 run: GATE3_CONSOLE is <seconds after the main menu>:<command>, the command at most %zu characters: %s\n",
                    sizeof run_console_text - 2, con);
            exit(2);
        }
        snprintf(run_console_text, sizeof run_console_text, "%s\n", colon + 1);
        atomic_store(&run_console_state, 1);
        unsigned k = 0;
        while (k < nprobe && !(probe[k].rva == RUN_TICK_RVA && !strcmp(probe[k].module, RUN_TICK_MODULE))) k++;
        if (k == nprobe) {
            if (nprobe == RUN_MAX_PROBES) { fprintf(stderr, "gate3 run: no room for GATE3_CONSOLE's probe: GATE3_PROBE has too many items\n"); exit(2); }
            RunProbe p = { .rva = RUN_TICK_RVA };
            snprintf(p.module, sizeof p.module, "%s", RUN_TICK_MODULE);
            snprintf(p.what, sizeof p.what, "GATE3_CONSOLE");
            probe[nprobe++] = p;
        }
        probe[k].flags = PF_TICK;
    }
    const RtModuleDef *defs = wrap_probes();
    printf("gate3 run: %u probes; log %s; time limit %u s%s%s\n", nprobe, log_path, timeout_s,
           run_status_fd >= 0 ? "" : "; in the driver's own process",
           run_status_fd >= 0 ? "" : gate2_quiet_stop() ? "; a stop ends in exit status 134, not SIGABRT (GATE_QUIET_STOP=0 turns that off)" : "; a stop ends in SIGABRT (GATE_QUIET_STOP=0)");
    fflush(stdout);
    if (gate2_quiet_stop()) rt_report_quiet(RT_QUIET_STOPS);       /* the comment at the top: HOW THE RUN'S PROCESS ENDS */
    signal(SIGALRM, on_alarm);
    alarm(timeout_s);
    struct guest_host_args args = { defs, gargc, gargv, log_path };
    const char *first = getenv("GATE3_GUEST_ON_FIRST_THREAD");   /* 1: the guest runs on the process's first thread (for comparison) */
    if (!(first && first[0] == '1')) {
        /* The process's first thread parks; the guest runs on a host thread of its own. */
        sigset_t sigalrm;
        sigemptyset(&sigalrm);
        sigaddset(&sigalrm, SIGALRM);
        pthread_sigmask(SIG_BLOCK, &sigalrm, NULL);
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, (size_t)256 << 20);
        pthread_t th;
        int err = pthread_create(&th, &attr, run_guest_host_thread, &args);
        if (err) { fprintf(stderr, "gate3 run: pthread_create: %s\n", strerror(err)); _exit(1); }
        if (rt_host_window_wanted()) {
            /* The first thread is the host window's: its event loop runs until the guest's host thread ends the process. */
            rt_host_on_close(run_host_closed);
            rt_host_main();
        } else {
            pthread_join(th, NULL);
        }
        _exit(0);
    }
    run_guest_host_thread(&args);
    return 0;
}

/* THE STATE and THE READ-BACK (the comment at the top), before the run's process exists: the
 * working copy <out>.state is made new from GATE3_START and named to the run (GATE3_STATE);
 * RT_D3D_READBACK is set.  Returns 0, or the driver's exit status with the reason printed. */
static char run_work[1100], run_start_line[1700];

static int run_prepare(const char *out, bool by_hand) {
    const char *given = getenv("GATE3_STATE"), *start = getenv("GATE3_START"), *keep = getenv("GATE3_KEEP_STATE");
    const char *rb = getenv("GATE3_READBACK"), *rt_rb = getenv("RT_D3D_READBACK");
    if (given && *given) {
        fprintf(stderr, "gate3 run: GATE3_STATE=%s: a run's state directory is not the caller's to name: every run works on a fresh copy of "
                "its start in <out>.state (GATE3_START=new, save:<dir> or player:<dir>; GATE3_KEEP_STATE=<dir> keeps it after a clean end)\n", given);
        return 2;
    }
    if (rb && *rb && strcmp(rb, "zero") != 0 && strcmp(rb, "stop") != 0) {
        fprintf(stderr, "gate3 run: GATE3_READBACK=%s: `zero` (the default for a run: an invented answer) or `stop` (the device's own default)\n", rb);
        return 2;
    }
    if (by_hand && keep && *keep) {
        fprintf(stderr, "gate3 run: GATE3_KEEP_STATE with --in-process: a run by hand in the driver's own process keeps nothing (its end is not watched by a parent)\n");
        return 2;
    }
    if (out[0] == '/') snprintf(run_work, sizeof run_work, "%s.state", out);
    else {
        char cwd[900];
        if (!getcwd(cwd, sizeof cwd)) { perror("getcwd"); return 2; }
        snprintf(run_work, sizeof run_work, "%s/%s.state", cwd, out);
    }
    if (!gate3_state_prepare(start, run_work, keep, run_start_line, sizeof run_start_line)) {
        fprintf(stderr, "gate3 run: not started: %s\n", run_start_line);
        return 2;
    }
    setenv("GATE3_STATE", run_work, 1);
    setenv("GATE3_START_LINE", run_start_line, 1);
    if (rb && !strcmp(rb, "stop")) { setenv("RT_D3D_READBACK", "stop", 1); setenv("GATE3_READBACK_BY", "GATE3_READBACK=stop", 1); }
    else if (rb && *rb) { setenv("RT_D3D_READBACK", "zero", 1); setenv("GATE3_READBACK_BY", "GATE3_READBACK=zero", 1); }
    else if (rt_rb && *rt_rb) setenv("GATE3_READBACK_BY", "set by the caller", 1);
    else { setenv("RT_D3D_READBACK", "zero", 1); setenv("GATE3_READBACK_BY", "the driver's default for a run", 1); }
    return 0;
}

/* THE STATE, the parent's reading of the child's log (run_state_line): true when one of the last 200
 * lines of the log is the state line of this end and says 0 (no file under the state directory open
 * for writing).  The last such line decides; a log that cannot be read, or has no such line, is not 0. */
#define RUN_STATE_LINE "state: files under the state directory open for writing at this end: "
static bool run_log_state_zero(const char *log_path) {
    FILE *f = fopen(log_path, "r");
    if (!f) return false;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
    const long end = ftell(f), want = 256L * 1024;
    const long from = end > want ? end - want : 0;
    if (end < 0 || fseek(f, from, SEEK_SET) != 0) { fclose(f); return false; }
    char *buf = malloc((size_t)(end - from) + 1);
    if (!buf) { fclose(f); return false; }
    const size_t got = fread(buf, 1, (size_t)(end - from), f);
    fclose(f);
    buf[got] = 0;
    const char *starts[200];
    unsigned total = 0;
    for (char *p = buf; *p; ) {
        starts[total++ % 200] = p;
        char *nl = strchr(p, '\n');
        if (!nl) break;
        p = nl + 1;
    }
    const size_t plen = strlen(RUN_STATE_LINE);
    bool zero = false;
    for (unsigned i = total > 200 ? total - 200 : 0; i < total; i++) {
        const char *l = starts[i % 200];
        if (strncmp(l, RUN_STATE_LINE, plen) != 0) continue;
        const char *v = l + plen;
        size_t vl = 0;
        while (v[vl] && v[vl] != '\n' && v[vl] != '\r') vl++;
        zero = vl == 1 && v[0] == '0';
    }
    free(buf);
    return zero;
}

int gate3_mode_run(int argc, char **argv) {
    char **gargv = (char **)run_default_args;
    int gargc = (int)(sizeof run_default_args / sizeof *run_default_args);
    /* SOUND (GATE3_SOUND=on, rt/xaudio.h): the default command line then goes without -nosound, so
     * that the game starts its sound; a command line given after `--` is taken as it is. */
    static char *sound_args[sizeof run_default_args / sizeof *run_default_args];
    { const char *snd = getenv("GATE3_SOUND");
      if (snd && !strcmp(snd, "on")) {
          int n = 0;
          for (int i = 0; i < gargc; i++) if (strcmp(run_default_args[i], "-nosound")) sound_args[n++] = (char *)run_default_args[i];
          gargv = sound_args;
          gargc = n;
      } }
    /* WORKERS (GATE3_WORKERS=<1 to 8>): the number after -numworkerthreads of the default command line.  The
     * default is 1 (the comment at run_default_args); more is for the signed bundle, which has x86 memory
     * ordering (RT_TSO=require).  A command line given after `--` is taken as it is.
     * SIZE (GATE3_SIZE=<width>x<height>, each from 640 to 8192): the argument after -w and the one after -h of the
     * default command line (the default is 1920x1080, the comment at run_default_args).  Also applied after the
     * two above, to whatever they produced. */
    static char *worker_args[sizeof run_default_args / sizeof *run_default_args];
    static char worker_n[4];
    { const char *w = getenv("GATE3_WORKERS");
      if (w && *w) {
          if (strlen(w) != 1 || w[0] < '1' || w[0] > '8') { fprintf(stderr, "gate3 run: GATE3_WORKERS is a number from 1 to 8: %s\n", w); return 2; }
          snprintf(worker_n, sizeof worker_n, "%s", w);
          for (int i = 0; i < gargc; i++) worker_args[i] = i > 0 && !strcmp(gargv[i - 1], "-numworkerthreads") ? worker_n : gargv[i];
          gargv = worker_args;
      } }
    static char *size_args[sizeof run_default_args / sizeof *run_default_args];
    static char size_w[5], size_h[5];
    { const char *z = getenv("GATE3_SIZE");
      if (z && *z) {
          const char *x = strchr(z, 'x');
          size_t wl = x ? (size_t)(x - z) : 0, hl = x ? strlen(x + 1) : 0;
          bool ok = x != NULL && wl >= 1 && wl <= 4 && hl >= 1 && hl <= 4;
          for (size_t i = 0; ok && i < wl; i++) ok = z[i] >= '0' && z[i] <= '9';
          for (size_t i = 0; ok && i < hl; i++) ok = x[1 + i] >= '0' && x[1 + i] <= '9';
          long wv = ok ? strtol(z, NULL, 10) : 0, hv = ok ? strtol(x + 1, NULL, 10) : 0;
          ok = ok && wv >= 640 && wv <= 8192 && hv >= 640 && hv <= 8192;
          if (!ok) { fprintf(stderr, "gate3 run: GATE3_SIZE is <width>x<height>, each a number from 640 to 8192: %s\n", z); return 2; }
          snprintf(size_w, sizeof size_w, "%ld", wv);
          snprintf(size_h, sizeof size_h, "%ld", hv);
          for (int i = 0; i < gargc; i++) {
              size_args[i] = (char *)gargv[i];
              if (i > 0 && !strcmp(gargv[i - 1], "-w")) size_args[i] = size_w;
              if (i > 0 && !strcmp(gargv[i - 1], "-h")) size_args[i] = size_h;
          }
          gargv = size_args;
      } }
    /* UPSCALE (GATE3_UPSCALE=<f>, a number from 1 to 2 with at most two decimals): MetalFX spatial upscaling
     * of each frame by f, in DXMT's swapchain (runtime/dxback/dxback.cpp, RT_D3D_UPSCALE).  The game draws at
     * GATE3_SIZE; the picture on the screen is that size times f, so for the built-in display of 3024x1890
     * GATE3_SIZE=2016x1260 with 1.5 (or 1800x1125 with 1.68).  The real renderer only (RT_D3D_BACKEND=dxmt). */
    { const char *u = getenv("GATE3_UPSCALE");
      if (u && *u) {
          char *end = NULL;
          double f = strtod(u, &end);
          const char *dot = strchr(u, '.');
          bool ok = end && !*end && u[0] >= '0' && u[0] <= '9' && f >= 1.0 && f <= 2.0 && (!dot || strlen(dot + 1) <= 2);
          if (!ok) { fprintf(stderr, "gate3 run: GATE3_UPSCALE is a number from 1 to 2 with at most two decimals: %s\n", u); return 2; }
          setenv("RT_D3D_UPSCALE", u, 1);
      } }
    bool in_process = argc > 0 && !strcmp(argv[0], "--in-process");
    if (in_process) { argc--; argv++; }
    if (argc > 0) {
        if (strcmp(argv[0], "--") != 0) { fprintf(stderr, "usage: gate3 run [--in-process] [-- <game arguments>]\n"); return 2; }
        gargc = argc - 1;
        gargv = argv + 1;
    }
    char log_path[1024], err_path[1024];
    const char *out = getenv("GATE3_RUN_OUT");
    if (!out || !*out) out = GATE3_RUN_OUT;
    snprintf(log_path, sizeof log_path, "%s.log", out);
    snprintf(err_path, sizeof err_path, "%s.stderr", out);
    unsigned timeout_s = 120;
    const char *t = getenv("GATE3_TIMEOUT");
    if (t && atoi(t) > 0) timeout_s = (unsigned)atoi(t);

    /* Who chose the level, for the run's "log level" line.  The child is this function again
     * with RT_LOG already set by its parent: it keeps what the parent noted. */
    if (!getenv("RT_LOG")) setenv("GATE3_RUN_LOG_DEFAULT", "1", 1);
    else if (!getenv("GATE3_RUN_STATUS_FD")) unsetenv("GATE3_RUN_LOG_DEFAULT");
    setenv("RT_LOG", "all:info", 0);                /* THE LOG LEVEL, at the top: all:debug writes 6.5 GB past the client's start */
    setenv("RT_LOG_FILE", log_path, 0);
    snprintf(log_path, sizeof log_path, "%s", getenv("RT_LOG_FILE"));
    const bool child = getenv("GATE3_RUN_STATUS_FD") != NULL;      /* the process the parent below started: everything is prepared */
    if (!child) {
        const int bad = run_prepare(out, in_process);
        if (bad) return bad;
    }
    if (in_process) return run_here(gargc, gargv, log_path, timeout_s);

    /* Start this program again as the child (the comment at the top says why not fork). */
    static char self[4096];
    uint32_t self_size = sizeof self;
    if (_NSGetExecutablePath(self, &self_size) != 0) { fprintf(stderr, "gate3 run: the path of this program is longer than %zu bytes\n", sizeof self); return 2; }
    char **cargv = calloc((size_t)gargc + 5, sizeof *cargv);
    if (!cargv) { fprintf(stderr, "gate3 run: out of memory\n"); return 2; }
    int cargc = 0;
    cargv[cargc++] = self;
    cargv[cargc++] = "run";
    cargv[cargc++] = "--in-process";
    cargv[cargc++] = "--";
    for (int i = 0; i < gargc; i++) cargv[cargc++] = gargv[i];
    unlink(log_path);
    int fd = open(err_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { perror(err_path); return 2; }
    int sp[2];
    if (pipe(sp) != 0) { perror("pipe"); return 2; }
    char num[16];
    snprintf(num, sizeof num, "%d", sp[1]);
    setenv("GATE3_RUN_STATUS_FD", num, 1);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, fd, 2);
    posix_spawn_file_actions_addclose(&fa, fd);
    posix_spawn_file_actions_addclose(&fa, sp[0]);
    fflush(stdout);
    double t0 = gate2_now_ms();
    extern char **environ;
    pid_t pid = 0;
    int e = posix_spawn(&pid, self, &fa, NULL, cargv, environ);
    posix_spawn_file_actions_destroy(&fa);
    if (e != 0) { fprintf(stderr, "gate3 run: could not start %s again: %s\n", self, strerror(e)); return 2; }
    close(fd);
    close(sp[1]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) { }
    double ms = gate2_now_ms() - t0;
    for (RunShared got;;) {                         /* the child has ended: the pipe holds at most two records */
        ssize_t r = read(sp[0], &got, sizeof got);
        if (r < 0 && errno == EINTR) continue;
        if (r != (ssize_t)sizeof got) break;
        run_state = got;
    }
    close(sp[0]);
    if (WIFSIGNALED(status) && WTERMSIG(status) == SIGALRM)
        printf("gate3 run: NO END after %u s: stopped by the driver\n", timeout_s);
    else if (WIFSIGNALED(status))
        printf("gate3 run: ended by signal %d after %.0f ms%s\n", WTERMSIG(status), ms,
               WTERMSIG(status) != SIGABRT ? "" : gate2_quiet_stop()
                   ? " (a fatal report of the runtime's signal handler, or a host library's abort(): a crash, which the quiet stop leaves loud)"
                   : " (a fatal report of the runtime ends so; the quiet stop is off, GATE_QUIET_STOP=0)");
    else if (gate2_quiet_stop() && WEXITSTATUS(status) == RT_REPORT_QUIET_STATUS)
        printf("gate3 run: ended by a stop after %.0f ms: exit status %d, the quiet end of a fatal report (in place of signal %d and "
               "macOS's crash report; GATE_QUIET_STOP=0 turns it off)\n", ms, WEXITSTATUS(status), SIGABRT);
    else
        printf("gate3 run: the process ended with status %d after %.0f ms\n", WEXITSTATUS(status), ms);
    const bool open_at_end = WIFEXITED(status) && WEXITSTATUS(status) == RUN_EXIT_PRESENTS_OPEN;
    const bool by_presents = WIFEXITED(status) && (WEXITSTATUS(status) == RUN_EXIT_PRESENTS || open_at_end);
    const bool closed_at_end = WIFEXITED(status) && WEXITSTATUS(status) == RUN_EXIT_CLOSED_OPEN;
    const bool closed_ok = WIFEXITED(status) && WEXITSTATUS(status) == RUN_EXIT_CLOSED;
    if (closed_ok || closed_at_end) printf("gate3 run: the host window was closed%s\n", closed_at_end ? ", WITH A FILE OF THE STATE STILL OPEN FOR WRITING" : "");
    if (by_presents) printf("gate3 run: ENDED BY THE DRIVER at the swap chain's Present number %s (GATE3_PRESENTS)%s\n", getenv("GATE3_PRESENTS"),
                            open_at_end ? ", WITH A FILE OF THE STATE STILL OPEN FOR WRITING: not a clean end" : " or the first after it with no file of the state open for writing");
    if (run_state.returned) printf("gate3 run: LauncherMain returned %d\n", run_state.result);
    else printf("gate3 run: LauncherMain %s\n", run_state.called ? "was called and did not return" : "was not reached");
    gate3_mode_summary(log_path, err_path);
    /* THE STATE (the comment at the top): kept only when asked and when the end was clean. */
    const bool clean = run_state.returned || (by_presents && !open_at_end);
    char how[300], state_line[2600];
    if (run_state.returned) snprintf(how, sizeof how, "ended when LauncherMain returned %d", run_state.result);
    else if (by_presents) snprintf(how, sizeof how, "was ended by the driver at GATE3_PRESENTS=%s %s", getenv("GATE3_PRESENTS"),
                                   open_at_end ? "with a file of the state still open for writing" : "with no file under the state directory open for writing");
    else if (closed_ok || closed_at_end) snprintf(how, sizeof how, "was ended by the closing of the host window%s", closed_at_end ? " with a file of the state still open for writing" : "");
    else if (WIFSIGNALED(status) && WTERMSIG(status) == SIGALRM) snprintf(how, sizeof how, "was ended by the time limit after %u s", timeout_s);
    else if (WIFSIGNALED(status)) snprintf(how, sizeof how, "ended by signal %d", WTERMSIG(status));
    else if (gate2_quiet_stop() && WEXITSTATUS(status) == RT_REPORT_QUIET_STATUS) snprintf(how, sizeof how, "ended at a stop");
    else snprintf(how, sizeof how, "ended with status %d", WEXITSTATUS(status));
    const char *keep = getenv("GATE3_KEEP_STATE");
    /* THE STATE, a player's end (the comment at the top): safe when the window was closed with no file open (the status
     * decides), or at a clean end or a stop of the runtime with the child's line saying 0. */
    const bool stop_end = gate2_quiet_stop() && WIFEXITED(status) && WEXITSTATUS(status) == RT_REPORT_QUIET_STATUS;
    const bool safe_end = closed_ok || ((clean || stop_end) && run_log_state_zero(log_path));
    const bool kept_ok = gate3_state_finish(getenv("GATE3_START"), run_work, keep, clean, safe_end, how, state_line, sizeof state_line);
    printf("STATE: %s\n", state_line);
    if (keep && *keep && !(kept_ok && clean)) return 1;             /* a state was asked for and there is none */
    return run_state.returned || by_presents || closed_ok ? 0 : 1;
}
