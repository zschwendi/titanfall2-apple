/* The Gate 3 driver: the 20 boot-path DLLs, and the pak loader's ui(11).dll as the 21st module
 * (gate3/modules.def), in the native runtime (docs/handoff/gate3-build.md).
 * Built by gate3/gate3.mk into build/gate3/gate3.  The modules are gate3/modules.def; the
 * `registry` and `imports` modes are the Gate 2 driver's (gate2/modes.c).
 *
 *     gate3 registry [--exports]   rt_init with the lifted modules, then one line per module:
 *                                  present or absent, base, size, lifted functions, exports, imports
 *     gate3 imports [--load]       every import of each module and what it binds to
 *     gate3 imports --summary      one line per module: how many imports bind to what
 *     gate3 ctors                  the initializer tables found in each DLL (gate3/ctors.h)
 *     gate3 load [module ...]      load the named modules in the order given, or with no name all
 *                                  of them in the order of gate3/modules.def (the original's order
 *                                  of first loads).  One line per module: loaded, FAILED (the
 *                                  load returned NULL), STOPPED (a fatal report, of which the first
 *                                  lines follow) or not tried; the time; constructors run.
 *                                  EACH MODULE IS LOADED IN A PROCESS OF ITS OWN, after the
 *                                  modules before it on the list that loaded, so that a stop in
 *                                  one does not hide the others.  A module that imports from one
 *                                  that stopped is not tried.
 *     gate3 load --in-process ...  the same loads one after the other in the driver's own
 *                                  process: the first stop ends it, with its whole report, where
 *                                  a debugger can see it
 *     gate3 load --no-count ...    without the counting functions in front of the constructors
 *     gate3 run [--in-process] [-- <arguments>]   our own entry point: load tier0 and the launcher, call the
 *                                  launcher's main function with a game command line, follow the
 *                                  run to its end and print a summary (gate3/run.c)
 *     gate3 summary <log> [<report>]   that summary again, from the log of an earlier run
 *     gate3 words <module> <rva> <n>   load the module (and what it imports from) in this process and
 *                                  print n 32-bit words of its image at rva: for checking a value
 *                                  that a module's own start-up code leaves in its data
 *
 * Environment: GATE3_ROOT (the install root; default: the game/ of the checkout the driver was
 * built in), GATE3_STATE (the runtime's state directory), GATE3_TIMEOUT (seconds one module's
 * process may take; 120), GATE3_REPORT_LINES (lines of a stop report that are printed; 8),
 * GATE2_STDERR (a file that gets everything each child wrote to descriptor 2),
 * GATE_QUIET_STOP (0: a stop in a child of `load` or in the run of `run` dies of SIGABRT again;
 * by default it exits with status 134 and macOS writes no crash report: gate2/modes.h, THE
 * QUIET STOP), and the runtime's own (RT_LOG, RT_TSO).  Exit status: 0; 1 when a module of
 * `load` did not load (`run`: gate3/run.c); 2 for a usage error; a fatal report in the
 * driver's own process ends it by abort() and SIGABRT, whatever the switch says.
 */
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "rt/init.h"
#include "rt/log.h"

#include "gate3_ids.h"          /* generated: gate3_module_defs, gate3_shim_table */
#include "ctors.h"
#include "modes.h"
#include "run.h"

static Gate2Mods  mods;
static Gate3Ctors ctors[GATE2_MAX_LIFTED];
static bool       counting;                     /* the counting functions are in the tables */

static void start_runtime(const RtModuleDef *defs) {
    const char *root = getenv("GATE3_ROOT"), *state = getenv("GATE3_STATE");
    if (!root || !*root) root = GATE3_INSTALL_ROOT;
    if (!state || !*state) state = GATE3_STATE_DIR;
    if (mkdir(state, 0700) != 0 && errno != EEXIST) { perror(state); exit(2); }
    RtConfig cfg = {
        .state_dir = state, .install_root = root, .tso = RT_TSO_TRY,
        .extra_modules = defs, .n_extra_modules = gate3_module_def_count,
        .extra_shims = gate3_shim_table, .n_extra_shims = gate3_shim_count,
    };
    rt_init(&cfg);
    gate2_mods_collect(&mods, "gate3");
}

/* ---- imports --summary --------------------------------------------------------------------------- */

static int imports_summary(void) {
    printf("gate3 imports --summary: imports of each module by what they bind to (computed from the tables; nothing is loaded)\n");
    printf("%-24s %6s %6s %6s %9s %9s %12s %6s\n", "module", "all", "shim", "lifted", "no shim", "OriginSDK", "d3dcompiler", "GFSDK");
    unsigned tot[7] = { 0 };
    for (unsigned i = 0; i < mods.nlifted; i++) {
        const Gate2Scan *s = &mods.scans[i];
        unsigned n[7] = { s->nimports, 0, 0, 0, 0, 0, 0 };
        for (unsigned k = 0; k < s->nimports; k++) {
            const Gate2Import *im = &s->imports[k];
            RtModule *from = rt_module_find(im->dll);
            if (!from) { fprintf(stderr, "gate3: %s imports from %s, which the manifest does not have\n", mods.lifted[i]->name, im->dll); return 1; }
            if (!from->native) n[2]++;
            else if (!strcmp(from->name, "originsdk.dll")) n[4]++;
            else if (!strcmp(from->name, "d3dcompiler_46.dll")) n[5]++;
            else if (!strncmp(from->name, "gfsdk_", 6)) n[6]++;
            else {
                /* A shim whose status is `trap` stops like a missing one and is counted as one. */
                uint64_t off = rt_native_import(im->dll, im->name, im->ordinal) - from->base;
                bool shim = off < RT_NATIVE_TRAP_STUBS && from->shims[(off - RT_NATIVE_SHIM_STUBS) / RT_STUB_SIZE]->status != RT_ST_TRAP;
                n[shim ? 1 : 3]++;
            }
        }
        printf("%-24s %6u %6u %6u %9u %9u %12u %6u\n", mods.lifted[i]->name, n[0], n[1], n[2], n[3], n[4], n[5], n[6]);
        for (int k = 0; k < 7; k++) tot[k] += n[k];
    }
    printf("%-24s %6u %6u %6u %9u %9u %12u %6u\n", "all (not distinct)", tot[0], tot[1], tot[2], tot[3], tot[4], tot[5], tot[6]);
    printf("shim: a Windows function the runtime has; lifted: an export of another lifted module; the other four stop\n"
           "the run when called and name the DLL and the function (no shim: a Windows function with none, or with one\n"
           "whose status is `trap`).\n");
    return 0;
}

/* ---- the initializer tables ------------------------------------------------------------------------ */

static unsigned lifted_rows(void) {
    unsigned n = 0;
    for (unsigned i = 0; i < gate3_module_def_count; i++) n += gate3_module_defs[i].kind == RT_MOD_LIFTED;
    return n;
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

static bool read_all(int fd, void *p, size_t n) {
    for (char *c = p; n; ) {
        ssize_t r = read(fd, c, n);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return false;
        c += r;
        n -= (size_t)r;
    }
    return true;
}

/* Fill ctors[] before this process has a runtime: a child starts one (the tables are found
 * through the loader's reader, which needs rt_init, and the counting functions must be in the
 * module table that rt_init is given). */
static void scan_tables_in_child(void) {
    int fd[2];
    unsigned n = lifted_rows();
    if (n > GATE2_MAX_LIFTED) { fprintf(stderr, "gate3: more than %d lifted modules\n", GATE2_MAX_LIFTED); exit(2); }
    if (pipe(fd) != 0) { perror("pipe"); exit(2); }
    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); exit(2); }
    if (pid == 0) {
        close(fd[0]);
        start_runtime(gate3_module_defs);
        for (unsigned i = 0; i < mods.nlifted; i++) {
            Gate3Ctors c;
            gate3_ctor_scan(mods.lifted[i], &c);
            write_all(fd[1], &c, sizeof c);
            write_all(fd[1], c.fn_rva, c.nfn * sizeof *c.fn_rva);
            write_all(fd[1], c.fn_table, c.nfn);
        }
        _exit(0);
    }
    close(fd[1]);
    bool ok = true;
    for (unsigned i = 0; ok && i < n; i++) {
        Gate3Ctors *c = &ctors[i];
        ok = read_all(fd[0], c, sizeof *c);
        if (!ok) break;
        c->fn_rva = calloc(c->nfn ? c->nfn : 1, sizeof *c->fn_rva);
        c->fn_table = calloc(c->nfn ? c->nfn : 1, 1);
        if (!c->fn_rva || !c->fn_table) { fprintf(stderr, "gate3: out of memory\n"); exit(2); }
        ok = read_all(fd[0], c->fn_rva, c->nfn * sizeof *c->fn_rva) && read_all(fd[0], c->fn_table, c->nfn);
    }
    close(fd[0]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) { }
    if (!ok || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "gate3: the process that looks for the initializer tables did not finish (its report is above); "
                        "`gate3 load --no-count` loads without it\n");
        exit(2);
    }
}

static int mode_ctors(void) {
    start_runtime(gate3_module_defs);
    printf("gate3 ctors: the initializer tables found in each DLL by the rule of gate3/ctors.h\n");
    for (unsigned i = 0; i < mods.nlifted; i++) {
        Gate3Ctors c;
        gate3_ctor_scan(mods.lifted[i], &c);
        printf("ctors %s: %u tables, %u entries", mods.lifted[i]->name, c.ntables, c.nfn);
        for (unsigned k = 0; k < c.ntables; k++)
            printf("%s [+%#x, +%#x) %u of %u slots, named at +%#x", k ? ";" : ":", c.tables[k].lo, c.tables[k].hi,
                   c.tables[k].nonnull, (c.tables[k].hi - c.tables[k].lo) / 8, c.tables[k].site);
        printf("\n");
    }
    return 0;
}

/* "constructors 51 of 51": the entries entered, of the entries of the tables that were
 * walked at all; tables in which nothing was entered are named after it. */
static const char *ctor_text(unsigned i, char *buf, size_t n) {
    const Gate3Ctors *c = &ctors[i];
    if (!counting) return "constructors not counted";
    if (!c->ntables) return "no initializer table found";
    unsigned hit = 0, of = 0, idle = 0, nidle = 0;
    for (unsigned k = 0; k < c->ntables; k++) {
        unsigned h = gate3_shared->hits[i][k];
        if (h) { hit += h; of += c->tables[k].nonnull; }
        else { idle += c->tables[k].nonnull; nidle++; }
    }
    int w = hit ? snprintf(buf, n, "constructors %u of %u", hit, of) : snprintf(buf, n, "constructors 0 of %u", idle);
    if (hit && nidle && w > 0 && (size_t)w < n)
        snprintf(buf + w, n - (size_t)w, " (and %u in %u table%s not walked)", idle, nidle, nidle == 1 ? "" : "s");
    return buf;
}

/* ---- load ---------------------------------------------------------------------------------------- */

static RtModule *load_one(unsigned i) {
    WCHAR name[64];
    size_t n = strlen(mods.lifted[i]->name);
    for (size_t k = 0; k <= n; k++) name[k] = (WCHAR)(unsigned char)mods.lifted[i]->name[k];
    return rt_module_load(rt_main_ctx(), name, 0);
}

enum { ST_NONE = 0, ST_LOADED, ST_FAILED, ST_STOPPED, ST_NOT_TRIED };

typedef struct {
    unsigned       target;                      /* index in mods of the module this process is for */
    unsigned       nbefore;
    const unsigned *before;                     /* the modules before it that loaded, in order */
} LoadJob;

static void child_load(void *arg) {
    const LoadJob *j = arg;
    for (unsigned k = 0; k < j->nbefore; k++)
        if (!load_one(j->before[k])) {
            fprintf(stderr, "FATAL (gate3 driver) %s loaded in its own process and does not load in the process of %s\n",
                    mods.lifted[j->before[k]]->name, mods.lifted[j->target]->name);
            _exit(97);
        }
    gate3_shared->t_start_ms = gate2_now_ms();
    RtModule *m = load_one(j->target);
    gate3_shared->ms = gate2_now_ms() - gate3_shared->t_start_ms;
    gate3_shared->error = m ? 0 : rt_thread_get_last_error();
    gate3_shared->loaded = m != NULL;
    gate3_shared->returned = 1;
    fflush(stdout);
    _exit(m ? 0 : 1);
}

/* The lifted module that `i` imports from and that is in state FAILED, STOPPED or NOT_TRIED; -1: none. */
static int blocked_by(unsigned i, const int *state) {
    const Gate2Scan *s = &mods.scans[i];
    for (unsigned k = 0; k < s->nimports; k++) {
        RtModule *from = rt_module_find(s->imports[k].dll);
        if (!from || from->native) continue;
        for (unsigned j = 0; j < mods.nlifted; j++)
            if (mods.lifted[j] == from && state[j] != ST_NONE && state[j] != ST_LOADED) return (int)j;
    }
    return -1;
}

static int find_module(const char *arg) {
    char want[80];
    size_t n = strlen(arg);
    if (n >= sizeof want - 5) return -1;
    for (size_t k = 0; k <= n; k++) want[k] = (char)tolower((unsigned char)arg[k]);
    if (n < 4 || strcmp(want + n - 4, ".dll") != 0) strcat(want, ".dll");
    for (unsigned i = 0; i < mods.nlifted; i++) if (!strcmp(mods.lifted[i]->name, want)) return (int)i;
    return -1;
}

static int mode_load(int argc, char **argv) {
    bool in_process = false, count = true;
    int first = 0;
    for (; first < argc && !strncmp(argv[first], "--", 2); first++) {
        if (!strcmp(argv[first], "--in-process")) in_process = true;
        else if (!strcmp(argv[first], "--no-count")) count = false;
        else { fprintf(stderr, "gate3 load: unknown option %s\n", argv[first]); return 2; }
    }
    gate3_shared = mmap(NULL, sizeof *gate3_shared, PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0);
    if (gate3_shared == MAP_FAILED) { perror("mmap"); return 2; }
    const RtModuleDef *defs = gate3_module_defs;
    if (count) {
        unsigned uncounted = 0;
        scan_tables_in_child();
        defs = gate3_ctor_wrap(gate3_module_defs, gate3_module_def_count, ctors, lifted_rows(), &uncounted);
        counting = true;
        if (uncounted) printf("load: %u initializer entries are beyond the %d counting functions and are not counted\n", uncounted, GATE3_CTOR_POOL);
    }
    start_runtime(defs);

    unsigned order[GATE2_MAX_LIFTED], norder = 0;
    if (first == argc) {
        for (unsigned i = 0; i < mods.nlifted; i++) order[norder++] = i;
    } else {
        for (int a = first; a < argc; a++) {
            int i = find_module(argv[a]);
            if (i < 0) { fprintf(stderr, "gate3 load: %s is not a lifted module of gate3/modules.def\n", argv[a]); return 2; }
            for (unsigned k = 0; k < norder; k++)
                if (order[k] == (unsigned)i) { fprintf(stderr, "gate3 load: %s is named twice\n", argv[a]); return 2; }
            order[norder++] = (unsigned)i;
        }
    }

    unsigned timeout_s = 120, report_lines = 8;
    const char *t = getenv("GATE3_TIMEOUT");
    if (t && atoi(t) > 0) timeout_s = (unsigned)atoi(t);
    t = getenv("GATE3_REPORT_LINES");
    if (t && atoi(t) > 0) report_lines = (unsigned)atoi(t);

    int state[GATE2_MAX_LIFTED] = { ST_NONE };
    unsigned before[GATE2_MAX_LIFTED], nbefore = 0, tally[5] = { 0 };
    char text[200];
    printf("gate3 load: %u modules, %s\n", norder, in_process ? "one after the other in this process"
           : "each in a process of its own after the modules before it that loaded");
    for (unsigned p = 0; p < norder; p++) {
        unsigned i = order[p];
        const char *name = mods.lifted[i]->name;
        if (!mods.scans[i].present) {
            printf("load %s: FAILED: no DLL file under the install root\n", name);
            state[i] = ST_FAILED;
        } else if (in_process) {
            printf("load %s: calling rt_module_load\n", name);
            fflush(stdout);
            double t0 = gate2_now_ms();
            RtModule *m = load_one(i);
            double ms = gate2_now_ms() - t0;
            if (m) printf("load %s: loaded, %.1f ms, %s\n", name, ms, ctor_text(i, text, sizeof text));
            else {
                DWORD e = rt_thread_get_last_error();
                printf("load %s: FAILED, last error %u (%s), %.1f ms, %s\n", name, e, gate2_error_name(e), ms, ctor_text(i, text, sizeof text));
            }
            state[i] = m ? ST_LOADED : ST_FAILED;
        } else {
            int dep = blocked_by(i, state);
            if (dep >= 0) {
                printf("load %s: not tried: it imports from %s, which %s\n", name, mods.lifted[dep]->name,
                       state[dep] == ST_STOPPED ? "stopped" : state[dep] == ST_FAILED ? "failed" : "was not tried");
                state[i] = ST_NOT_TRIED;
            } else {
                LoadJob job = { i, nbefore, before };
                Gate2Child ch;
                memset(gate3_shared, 0, sizeof *gate3_shared);
                gate2_run_child(child_load, &job, timeout_s, &ch);
                double now = gate2_now_ms();
                if (gate3_shared->returned && gate3_shared->loaded) {
                    printf("load %s: loaded, %.1f ms, %s\n", name, gate3_shared->ms, ctor_text(i, text, sizeof text));
                    state[i] = ST_LOADED;
                } else if (gate3_shared->returned) {
                    printf("load %s: FAILED, last error %u (%s), %.1f ms, %s\n", name, gate3_shared->error,
                           gate2_error_name(gate3_shared->error), gate3_shared->ms, ctor_text(i, text, sizeof text));
                    state[i] = ST_FAILED;
                } else {
                    if (gate3_shared->t_start_ms > 0)
                        printf("load %s: STOPPED after %.1f ms, %s: %s\n", name, now - gate3_shared->t_start_ms,
                               ctor_text(i, text, sizeof text), ch.first);
                    else
                        printf("load %s: STOPPED before its own load began: %s\n", name, ch.first);
                    const char *line = ch.report + strcspn(ch.report, "\n");
                    for (unsigned k = 1; k < report_lines && *line; k++) {
                        line += 1;
                        size_t len = strcspn(line, "\n");
                        if (len) printf("    %.*s\n", (int)len, line);
                        line += len;
                    }
                    state[i] = ST_STOPPED;
                }
            }
        }
        if (state[i] == ST_LOADED) before[nbefore++] = i;
        tally[state[i]]++;
        fflush(stdout);
    }
    printf("load: %u of %u modules loaded, %u stopped, %u failed, %u not tried\n", tally[ST_LOADED], norder, tally[ST_STOPPED],
           tally[ST_FAILED], tally[ST_NOT_TRIED]);
    return tally[ST_LOADED] == norder ? 0 : 1;
}

/* ---- main ---------------------------------------------------------------------------------------- */

static int mode_imports(int argc, char **argv) {
    bool loaded = argc > 0 && !strcmp(argv[0], "--load");
    start_runtime(gate3_module_defs);
    if (argc > 0 && !strcmp(argv[0], "--summary")) return imports_summary();
    if (loaded)
        for (unsigned i = 0; i < mods.nlifted; i++) {
            printf("load %s: calling rt_module_load\n", mods.lifted[i]->name);
            fflush(stdout);
            if (!load_one(i)) { printf("load %s: FAILED, last error %u\n", mods.lifted[i]->name, rt_thread_get_last_error()); return 1; }
        }
    return gate2_mode_imports(&mods, loaded);
}

static int mode_words(char **argv) {
    start_runtime(gate3_module_defs);
    int i = find_module(argv[0]);
    unsigned long rva = strtoul(argv[1], NULL, 0), n = strtoul(argv[2], NULL, 0);
    if (i < 0) { fprintf(stderr, "gate3 words: %s is not a lifted module of gate3/modules.def\n", argv[0]); return 2; }
    RtModule *m = load_one((unsigned)i);
    if (!m) { fprintf(stderr, "gate3 words: %s did not load, last error %u\n", argv[0], rt_thread_get_last_error()); return 1; }
    if (rva + 4 * n > m->size) { fprintf(stderr, "gate3 words: +%#lx and %lu words are not inside the image (%#llx bytes)\n", rva, n, (unsigned long long)m->size); return 2; }
    printf("%s+%#lx after its load:", m->name, rva);
    for (unsigned long k = 0; k < n; k++) {
        uint32_t v;
        memcpy(&v, (const void *)(uintptr_t)(m->base + rva + 4 * k), 4);
        printf(" 0x%08x", v);
    }
    printf("\n");
    return 0;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (argc >= 2 && !strcmp(argv[1], "registry")) {
        start_runtime(gate3_module_defs);
        return gate2_mode_registry(&mods, argc > 2 && !strcmp(argv[2], "--exports"));
    }
    if (argc >= 2 && !strcmp(argv[1], "imports")) return mode_imports(argc - 2, argv + 2);
    if (argc == 2 && !strcmp(argv[1], "ctors"))   return mode_ctors();
    if (argc >= 2 && !strcmp(argv[1], "load"))    return mode_load(argc - 2, argv + 2);
    if (argc == 5 && !strcmp(argv[1], "words"))   return mode_words(argv + 2);
    if (argc >= 2 && !strcmp(argv[1], "run"))     return gate3_mode_run(argc - 2, argv + 2);
    if (argc >= 3 && argc <= 4 && !strcmp(argv[1], "summary")) return gate3_mode_summary(argv[2], argc == 4 ? argv[3] : NULL);
    fprintf(stderr, "usage: gate3 registry [--exports] | imports [--load | --summary] | ctors | load [--in-process] [--no-count] [module ...]\n"
                    "       gate3 run [--in-process] [-- <game arguments>] | summary <log> [<report>] | words <module> <rva> <n>\n");
    return 2;
}
