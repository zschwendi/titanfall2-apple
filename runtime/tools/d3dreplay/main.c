/* d3d-replay: play a script of recorded Direct3D calls through the do-nothing device.
 *
 *   build/runtime/d3d-replay [options] SCRIPT [SCRIPT ...]        (.gz or plain; `-` = standard input)
 *
 *   --first            stop at the first stop or disagreement that is not ours by design and print
 *                      the call, the script line, what differs and the objects it names (default)
 *   --all              run on; print the table of every stop and disagreement, by method and cause
 *   --table FILE       with --all: write the table (Markdown) to FILE instead of standard output
 *   --tsv FILE         with --all: write the rows as method, cause, first call, calls
 *   --baseline FILE    with --all: hold the rows against an earlier --tsv; exit 1 only if a row is
 *                      new or affects more calls
 *   --no-scout         no forked scout: a stop is the runtime's own report and ends the player
 *   --scout            a forked scout (the default, except with RT_D3D_BACKEND=dxmt); with dxmt a usage error
 *   --foreign          also play the calls the script marks as not the game's
 *   --chunk N          lines per scout (5000)        --until N    play the first N lines only
 *   --progress         a line on standard error every 100,000 lines
 *   --skip-method Interface::Method
 *                      a script line that calls that method is not played: nothing of the device is called
 *                      for it.  Given up to 16 times.  The player counts such lines per method and prints one
 *                      line per method on standard output.  A skipped call that returns an object (a >o token)
 *                      is a usage failure (exit 2): the objects it would make would be missing.  For the stops
 *                      that end a straight-through (dxmt) run, e.g. IDXGISwapChain::SetFullscreenState
 *   --pace             leave Present's pacing as the device has it (default: off, see pacing_off)
 *   --backend NAME     none (default) or record: the back end behind the device (rt/d3dback.h).  With
 *                      record the player prints, per method a renderer must see, how often the front
 *                      took the call and how often it reached the recording back end (back_counts)
 *   --back-table FILE  with --backend record: write that table (Markdown) to FILE as well
 *   --data DIR         give the device the recorded bytes (shader bytecode, initial data,
 *                      UpdateSubresource, input layouts' signatures, writes into mapped memory)
 *                      where it otherwise gets zeros; DIR is what tools/trace/d3d/tracefile.py
 *                      `extract` wrote (data.h).  Derived from the game: under build/trace/data only
 *
 * Exit: 0 nothing differs beyond what is ours by design (or, with --baseline, nothing is worse);
 * 1 something does; 2 the player itself failed.  How it works: replay.h.  Scripts:
 * tools/trace/d3d/convert.sh.  `make d3d-replay` runs it over build/trace/script/.
 */
#include <sys/stat.h>

#include <pthread.h>

#include "replay.h"

/* runtime/include/rt/window.h (rt_host_window_wanted) and rt/host.h (rt_host_main): declared here, as those
 * headers name Windows types the player does not include.  Both are in librt. */
bool rt_host_window_wanted(void);
void rt_host_main(void);

/* Present may pace to the display's rate (unit d3d-fix-2).  A replay wants the calls, not the
 * time: this is where the device's switch is thrown, once it exists. */
static void pacing_off(void) {
    /* The device at this commit has no pacing: Present returns at once.  When it has, its switch
     * goes here (the brief of unit d3d-fix-2, item 7: "a switch turns pacing off for tests"). */
}

/* The seam's measure over a recording (unit pic-seam; rt/d3dback.h, "what a body writes").  For
 * every entry that serves a method: the calls of the method this process made and came back from
 * (the layer's count: a call the front stops on is only ever made by a scout), how many of them
 * the body handed on, how many it answered alone, and how many reached the recording back end.
 *   equal          calls == handed on + answered alone, and handed on == reached
 *   row not done   the body hands nothing on yet (tools/swarm/back_rows.py has its row)
 *   DIFFERENT      anything else: a path that forgets the back end, or reaches it twice
 * Returns the number of DIFFERENT rows. */
static int back_counts(FILE *o) {
    unsigned equal = 0, undone = 0, different = 0, idle = 0;
    uint64_t handed = 0;
    fprintf(o, "\n## The back end (%s): per method, the front's count and the back end's\n\n", rt_d3dback()->name);
    fprintf(o, "| method | calls the front took | handed on | answered alone | reached the back end | |\n|---|---|---|---|---|---|\n");
    for (unsigned e = 0; e < RT_BACK_N; e++) {
        const char *method = rt_d3dback_entry_method((RtD3dBackEntry)e);
        uint64_t asked = rt_d3dback_asked((RtD3dBackEntry)e), alone = rt_d3dback_alone((RtD3dBackEntry)e);
        uint64_t reached = rt_d3dback_record_count((RtD3dBackEntry)e);
        handed += asked;
        if (!method) {                              /* the seam's own three */
            fprintf(o, "| (%s) | | %llu | | %llu | %s |\n", rt_d3dback_entry_name((RtD3dBackEntry)e), (unsigned long long)asked,
                    (unsigned long long)reached, asked == reached ? "equal" : "DIFFERENT");
            different += asked != reached;
            continue;
        }
        char iface[64];
        size_t n = (size_t)(strstr(method, "::") - method);
        memcpy(iface, method, n);
        iface[n] = 0;
        const RtComIface *it = rt_com_iface_by_name(iface);
        uint64_t calls = it ? rt_com_calls(it, method + n + 2) : 0;
        const char *verdict = !calls && !asked && !alone ? "not called" : !asked && !alone ? "row not done" :
                              asked + alone == calls && asked == reached ? "equal" : "DIFFERENT";
        equal += verdict[0] == 'e'; undone += verdict[0] == 'r'; different += verdict[0] == 'D'; idle += verdict[0] == 'n';
        fprintf(o, "| `%s` | %llu | %llu | %llu | %llu | %s |\n", method, (unsigned long long)calls, (unsigned long long)asked,
                (unsigned long long)alone, (unsigned long long)reached, verdict);
    }
    uint64_t made = 0, live = 0;
    for (unsigned k = 0; k < RT_BK_KINDS; k++) { made += rt_d3dback_record_made((RtD3dBackKind)k); live += rt_d3dback_record_live((RtD3dBackKind)k); }
    fprintf(o, "\n%u methods equal, %u with the row not done, %u DIFFERENT, %u not called by this recording; %llu calls handed on; "
            "%llu back objects made, %llu alive at the end.\n", equal, undone, different, idle, (unsigned long long)handed,
            (unsigned long long)made, (unsigned long long)live);
    return (int)different;
}

/* the DXMT back end's counts (runtime/dxback/dxback.cpp replaces this empty default where it is linked) */
__attribute__((weak)) void rt_dxback_report(void) {}

static int play(int argc, char **argv);

/* The host window (RT_HOST_WINDOW=appkit or appkit-hidden) needs the process's first thread for its event loop
 * (rt/host.h): the player runs on a second thread with a 64 MiB stack, and that thread ends the process as main
 * always did (through _exit).  Without a host window nothing changes. */
static int g_argc;
static char **g_argv;
static void *play_thread(void *arg) {
    (void)arg;
    exit(play(g_argc, g_argv));
    return NULL;
}

int main(int argc, char **argv) {
    if (!rt_host_window_wanted()) return play(argc, argv);
    g_argc = argc;
    g_argv = argv;
    pthread_attr_t attr;
    pthread_t th;
    if (pthread_attr_init(&attr) || pthread_attr_setstacksize(&attr, (size_t)64 << 20) || pthread_create(&th, &attr, play_thread, NULL)) {
        fprintf(stderr, "d3d-replay: cannot start the player's thread\n");
        return 2;
    }
    rt_host_main();                 /* the first thread: the host window's event loop; the second thread ends the process */
    pthread_join(th, NULL);
    return 0;
}

static int play(int argc, char **argv) {
    RpOptions opt = { .scout = true };
    const char *table = NULL, *tsv = NULL, *baseline = NULL, *backend = NULL, *back_table = NULL;
    char cmd[16384] = "gzip -dcf";
    bool pace = false, from_stdin = false, scout_asked = false;
    int nfiles = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--first")) opt.keep_going = false;
        else if (!strcmp(a, "--all")) opt.keep_going = true;
        else if (!strcmp(a, "--no-scout")) opt.scout = false;
        else if (!strcmp(a, "--scout")) scout_asked = true;
        else if (!strcmp(a, "--foreign")) opt.play_foreign = true;
        else if (!strcmp(a, "--pace")) pace = true;
        else if (!strcmp(a, "--progress")) opt.progress = true;
        else if (!strcmp(a, "--table") && i + 1 < argc) table = argv[++i];
        else if (!strcmp(a, "--tsv") && i + 1 < argc) tsv = argv[++i];
        else if (!strcmp(a, "--baseline") && i + 1 < argc) baseline = argv[++i];
        else if (!strcmp(a, "--backend") && i + 1 < argc) backend = argv[++i];
        else if (!strcmp(a, "--back-table") && i + 1 < argc) back_table = argv[++i];
        else if (!strcmp(a, "--chunk") && i + 1 < argc) opt.chunk = (unsigned)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(a, "--until") && i + 1 < argc) opt.until = strtoull(argv[++i], NULL, 10);
        else if (!strcmp(a, "--data") && i + 1 < argc) opt.data = argv[++i];
        else if (!strcmp(a, "--skip-method") && i + 1 < argc) {
            if (opt.nskip == RP_SKIP_MAX) { fprintf(stderr, "d3d-replay: --skip-method at most %d times\n", RP_SKIP_MAX); return 2; }
            opt.skip[opt.nskip++] = argv[++i];
        }
        else if (!strcmp(a, "-")) from_stdin = true;
        else if (a[0] == '-' || strchr(a, '\'')) { fprintf(stderr, "d3d-replay: %s?  (the head of runtime/tools/d3dreplay/main.c has the options)\n", a); return 2; }
        else { nfiles++; snprintf(cmd + strlen(cmd), sizeof cmd - strlen(cmd), " '%s'", a); }
    }
    if (!nfiles && !from_stdin) { fprintf(stderr, "d3d-replay: no script\n"); return 2; }
    /* DXMT cannot be used in a forked child: with RT_D3D_BACKEND=dxmt the player plays straight through, in this process */
    const char *env_backend = getenv("RT_D3D_BACKEND");
    if (env_backend && !strcmp(env_backend, "dxmt")) {
        if (scout_asked) { fprintf(stderr, "d3d-replay: --scout (a forked scout) cannot be used with RT_D3D_BACKEND=dxmt: DXMT does not survive fork; the player plays in one process\n"); return 2; }
        opt.scout = false;
    }
    if (!pace) pacing_off();

    static char state[4096];
    const char *given = getenv("D3D_REPLAY_STATE");
    if (given && *given == '/') snprintf(state, sizeof state, "%s", given);
    else {                                          /* the runtime wants an absolute path */
        if (!getcwd(state, sizeof state - 64)) { perror("getcwd"); return 2; }
        strcat(state, "/build");
        mkdir(state, 0755);
        strcat(state, "/d3d-replay-state");
    }
    if (mkdir(state, 0700) != 0 && errno != EEXIST) { perror(state); return 2; }
    RtConfig cfg = { .state_dir = state, .install_root = state, .tso = RT_TSO_TRY };
    rt_init(&cfg);
    if (backend) {                                  /* before the script makes its device (rt/d3dback.h) */
        if (!strcmp(backend, "record")) rt_d3dback_use(rt_d3dback_record());
        else if (!strcmp(backend, "none")) rt_d3dback_use(NULL);
        else { fprintf(stderr, "d3d-replay: --backend %s?  (none or record)\n", backend); return 2; }
    }

    FILE *script = from_stdin ? stdin : popen(cmd, "r");
    if (!script) { perror("popen"); return 2; }
    RpTable t = { 0 };
    int bad = rp_run(rt_main_ctx(), script, &opt, &t);
    if (!from_stdin && pclose(script) != 0 && !opt.until && !(bad && !opt.keep_going)) { fprintf(stderr, "d3d-replay: reading the script failed (%s)\n", cmd); return 2; }

    if (opt.keep_going) {
        FILE *o = table ? fopen(table, "w") : stdout;
        if (!o) { perror(table); return 2; }
        if (tsv) { FILE *f = fopen(tsv, "w"); if (!f) { perror(tsv); return 2; } rp_table_tsv(&t, f); fclose(f); }
        if (baseline) {
            FILE *f = fopen(baseline, "r");
            if (!f) { perror(baseline); return 2; }
            int worse = rp_table_against(&t, f, stdout);
            fclose(f);
            printf("d3d-replay: against %s: %d rows are new or affect more calls\n", baseline, worse);
            bad = worse != 0;
        }
        rp_table_print(&t, o);
        if (table) fclose(o);
    }
    if (rt_d3dback() == rt_d3dback_record()) {
        int different = back_counts(stdout);
        if (back_table) { FILE *f = fopen(back_table, "w"); if (!f) { perror(back_table); return 2; } back_counts(f); fclose(f); }
        if (different) bad = 1;
    }
    for (unsigned k = 0; k < opt.nskip; k++) {
        if (t.skipped[k]) printf("d3d-replay: skipped by --skip-method: %s, %llu line(s), the first at line %llu\n", opt.skip[k],
                                 (unsigned long long)t.skipped[k], (unsigned long long)t.skip_first[k]);
        else printf("d3d-replay: skipped by --skip-method: %s, 0 line(s)\n", opt.skip[k]);
    }
    printf("d3d-replay: %llu lines, %llu calls played, %llu stops, %llu not played, %llu calls differ, %u rows; %.1f s, peak footprint %.1f MB%s\n",
           (unsigned long long)t.lines, (unsigned long long)t.played, (unsigned long long)t.stops, (unsigned long long)t.not_played,
           (unsigned long long)t.with_differences, t.n, t.seconds, (double)t.peak_footprint / 1048576.0, bad ? "" : baseline ? "; no row is new or affects more calls than the baseline says" : "; nothing differs beyond what is ours by design");
    fflush(stdout);
    rt_dxback_report();   /* the DXMT back end's log counts, when it is linked (runtime/dxback/) */
    _exit(bad ? 1 : 0);             /* not exit(): the runtime's exit sequence is for guest programs */
}
