/* The Gate 2 driver: tier0.dll and vstdlib.dll in the native runtime (PLAN.md, Gate 2;
 * docs/handoff/gate2-build.md).  Built by gate2/gate2.mk into build/gate2/gate2.
 *
 *     gate2 registry            rt_init with the two lifted modules, then one line per module:
 *                               present or absent, base, size, lifted functions, exports,
 *                               exported functions with no lifted entry (each then listed)
 *     gate2 registry --exports  the same, then every export: ordinal, rva, kind, name
 *     gate2 load                rt_module_load of each module in list order: result, last
 *                               error on failure, time taken
 *     gate2 call [n | a-b ...]  the first-call list of finding 21 (gate2/calls.c) and the wide set
 *                               after it (ranks 31 and up, gate2/calls_wide.c), each rank in
 *                               a child process: PASS, FAIL, SKIP or STOP per rank, then a count
 *     gate2 call --map-only     the same without the load: the images are mapped and nothing
 *                               else, so only entries that need no import, no thread-local and
 *                               no static constructor run
 *     gate2 call --list         the table itself: what each rank calls and checks
 *     gate2 imports [--load]    every import of each module and what it binds to
 *
 * Environment: GATE2_ROOT (the directory that holds bin/x64_retail; default: the game/ of the
 * checkout the driver was built in), GATE2_STATE (the runtime's state directory),
 * GATE2_TIMEOUT (seconds a rank of `call` may take before the driver stops it; 120),
 * GATE2_STDERR (a file to which the first 64 KB each child wrote to descriptor 2 are appended),
 * GATE_QUIET_STOP (0: a rank of `call` that stops dies of SIGABRT again; by default its process
 * exits with status 134 and macOS writes no crash report: modes.h, THE QUIET STOP), and the
 * runtime's own (RT_LOG, RT_TSO).  Exit status: 0, or 1 when a load fails or a call FAILs or
 * STOPs, or 2 for a usage error; a fatal report of the runtime in the driver's own process
 * (`load`) ends it by abort() and SIGABRT, whatever the switch says.
 */
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "rt/heap.h"
#include "rt/init.h"
#include "rt/log.h"

#include "gate2_ids.h"          /* generated: gate2_module_defs, gate2_shim_table */
#include "calls.h"
#include "host.h"
#include "modes.h"
#include "peek.h"
#include "values.h"

static Gate2Mods mods;                  /* the lifted modules of the registry, in list order (modes.h) */

static void start_runtime(void) {
    const char *root = getenv("GATE2_ROOT"), *state = getenv("GATE2_STATE");
    if (!root || !*root) root = GATE2_INSTALL_ROOT;
    if (!state || !*state) state = GATE2_STATE_DIR;
    if (mkdir(state, 0700) != 0 && errno != EEXIST) { perror(state); exit(2); }
    RtConfig cfg = {
        .state_dir = state, .install_root = root, .tso = RT_TSO_TRY,
        .extra_modules = gate2_module_defs, .n_extra_modules = gate2_module_def_count,
        .extra_shims = gate2_shim_table, .n_extra_shims = gate2_shim_count,
        .before_seal = gate2_host_before_seal,
    };
    rt_init(&cfg);
    gate2_mods_collect(&mods, "gate2");
}

/* ---- registry and imports: gate2/modes.c ----------------------------------------------------------- */

static int mode_registry(int argc, char **argv) {
    start_runtime();
    return gate2_mode_registry(&mods, argc > 0 && !strcmp(argv[0], "--exports"));
}

/* ---- load ---------------------------------------------------------------------------------------- */

/* rt_module_load of every lifted module, in list order.  Returns how many failed. */
static unsigned load_all(bool report) {
    Ctx *ctx = rt_main_ctx();
    unsigned failed = 0;
    for (unsigned i = 0; i < mods.nlifted; i++) {
        WCHAR name[64];
        size_t n = strlen(mods.lifted[i]->name);
        for (size_t k = 0; k <= n; k++) name[k] = (WCHAR)(unsigned char)mods.lifted[i]->name[k];
        if (report) { printf("load %s: calling rt_module_load\n", mods.lifted[i]->name); fflush(stdout); }
        double t0 = gate2_now_ms();
        RtModule *m = rt_module_load(ctx, name, 0);
        double ms = gate2_now_ms() - t0;
        if (m && report)
            printf("load %s: loaded at %#llx, load count %d, %.3f ms\n", m->name, (unsigned long long)m->base, m->load_count, ms);
        if (!m) {
            DWORD e = rt_thread_get_last_error();
            failed++;
            printf("load %s: FAILED, last error %u (%s), %.3f ms\n", mods.lifted[i]->name, e, gate2_error_name(e), ms);
        }
        fflush(stdout);
    }
    return failed;
}

static int mode_load(void) {
    start_runtime();
    unsigned failed = load_all(true);
    printf("load: %u of %u modules loaded\n", mods.nlifted - failed, mods.nlifted);
    return failed ? 1 : 0;
}

/* ---- imports ------------------------------------------------------------------------------------- */

static int mode_imports(int argc, char **argv) {
    bool loaded = argc > 0 && !strcmp(argv[0], "--load");
    start_runtime();
    if (loaded && load_all(true)) return 1;
    return gate2_mode_imports(&mods, loaded);
}

/* ---- call ---------------------------------------------------------------------------------------- */

GUESTFN gate2_export(Call *c, RtModule *m, const char *name) {
    GUESTFN f = 0;
    if (c->map_only) {                          /* not loaded: rt_module_export would refuse */
        const Gate2Export *x = gate2_find_export(gate2_scan_of(&mods, m), name);
        if (x) f = m->base + x->rva;
    } else {
        f = rt_module_export(m, name, 0);
    }
    if (!f) gate2_fail_text(c, "no export %s in %s", name, m->name);
    return f;
}

void gate2_fail(Call *c, const char *what, uint64_t got, uint64_t want) {
    gate2_fail_text(c, "%s: got %#llx (%lld), expected %#llx (%lld)", what, (unsigned long long)got, (long long)got,
                    (unsigned long long)want, (long long)want);
}

/* The values the entry in this child has checked (gate2/values.h); GATE2_VALUES=1 prints them. */
static Gate2Values values;

bool gate2_check(Call *c, const char *what, uint64_t got, uint64_t want) {
    gate2_values_record(&values, 'E', what, got);
    if (got == want) return true;
    gate2_fail(c, what, got, want);
    return false;
}

bool gate2_check_true(Call *c, const char *what, int holds) {
    gate2_values_record(&values, 'E', what, (uint64_t)holds);
    if (holds) return true;
    gate2_fail_text(c, "%s: does not hold", what);
    return false;
}

void gate2_value(Call *c, const char *what, uint64_t value) {
    (void)c;
    gate2_values_record(&values, 'M', what, value);
}

/* With GATE2_VALUES set and not empty: the V lines and the R line of the rank, before the
 * line for people.  tools/ref/compare.py reads them beside the reference program's. */
static void print_values(const Entry *e, const Call *c) {
    const char *on = getenv("GATE2_VALUES");
    char line[1200];
    if (!on || !*on) return;
    for (unsigned i = 0; gate2_values_line(&values, i, e->rank, line, sizeof line) > 0; i++) printf("%s\n", line);
    gate2_values_result(&values, e->rank, c->result == GATE2_PASS ? "PASS" : c->result == GATE2_FAIL ? "FAIL" : "SKIP", e->name, c->msg,
                        line, sizeof line);
    printf("%s\n", line);
}

void gate2_fail_text(Call *c, const char *fmt, ...) {
    va_list ap;
    if (c->result != GATE2_PASS) return;        /* the first failure is the one reported */
    c->result = GATE2_FAIL;
    va_start(ap, fmt);
    vsnprintf(c->msg, sizeof c->msg, fmt, ap);
    va_end(ap);
}

void gate2_skip(Call *c, const char *fmt, ...) {
    va_list ap;
    if (c->result != GATE2_PASS) return;
    c->result = GATE2_SKIP;
    va_start(ap, fmt);
    vsnprintf(c->msg, sizeof c->msg, fmt, ap);
    va_end(ap);
}

void gate2_note(Call *c, const char *text) {
    if (c->result == GATE2_PASS) snprintf(c->msg, sizeof c->msg, "(%s)", text);
}

void *gate2_mem(size_t size) { return rt_galloc(size ? size : 1); }

/* A line on descriptor 2, which run_child keeps: the last one is added to a STOP report. */
void gate2_progress(const char *what) {
    char line[200];
    int n = snprintf(line, sizeof line, "AT %s\n", what);
    if (n > 0 && write(2, line, (size_t)n) < 0) { }
}

#define CHILD_TIMEOUT_S 120
enum { EXIT_PASS = 0, EXIT_FAIL = 10, EXIT_SKIP = 11 };

static bool map_only;

static void prepare(void) {                     /* in a child: bring the modules up */
    if (map_only) {
        for (unsigned i = 0; i < mods.nlifted; i++) gate2_map_only(mods.lifted[i]);
    } else if (load_all(false)) {
        fflush(stdout);
        _exit(EXIT_FAIL);
    }
}

static void child_load(void *arg) { (void)arg; prepare(); }

static const Entry *entry_running;             /* in a child: the entry child_entry is running */

/* End the child now with what `c` has recorded, as if the entry had returned: for an entry
 * that learns its result on a host thread while the guest thread it started cannot return. */
void gate2_end(Call *c) {
    const Entry *e = entry_running;
    rt_log_flush_try();
    print_values(e, c);
    printf("%2d  %-4s  %s%s%s\n", e->rank, c->result == GATE2_PASS ? "PASS" : c->result == GATE2_FAIL ? "FAIL" : "SKIP",
           e->name, c->msg[0] ? ": " : "", c->msg);
    fflush(stdout);
    _exit(c->result == GATE2_PASS ? EXIT_PASS : c->result == GATE2_FAIL ? EXIT_FAIL : EXIT_SKIP);
}

static void child_entry(void *arg) {
    const Entry *e = arg;
    Call c = { .ctx = rt_main_ctx(), .tier0 = rt_module_find("tier0.dll"), .vstdlib = rt_module_find("vstdlib.dll"),
               .map_only = map_only };
    entry_running = e;
    if (!e->run) {
        gate2_skip(&c, "not typed in yet");
    } else if (map_only && e->needs) {
        gate2_skip(&c, "needs the load:%s%s%s", e->needs & NEEDS_IMPORTS ? " bound imports" : "",
                   e->needs & NEEDS_TLS ? " static TLS" : "", e->needs & NEEDS_ENTRY ? " the entry point" : "");
    } else {
        prepare();
        e->run(&c);
    }
    print_values(e, &c);
    printf("%2d  %-4s  %s%s%s\n", e->rank, c.result == GATE2_PASS ? "PASS" : c.result == GATE2_FAIL ? "FAIL" : "SKIP",
           e->name, c.msg[0] ? ": " : "", c.msg);
    fflush(stdout);
    _exit(c.result == GATE2_PASS ? EXIT_PASS : c.result == GATE2_FAIL ? EXIT_FAIL : EXIT_SKIP);
}

/* The call table: the finding's 30 ranks (calls.c), then the wide set (calls_wide.c). */
static unsigned nentries(void) { return gate2_ncalls + gate2_ncalls_wide; }
static const Entry *entry_at(unsigned i) { return i < gate2_ncalls ? &gate2_calls[i] : &gate2_calls_wide[i - gate2_ncalls]; }

static int mode_call(int argc, char **argv) {
    bool list = false, want[256] = { false }, any = false;
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--map-only")) map_only = true;
        else if (!strcmp(argv[i], "--list")) list = true;
        else {
            char *end;                          /* a rank, or a range of ranks: 31-60 */
            long n = strtol(argv[i], &end, 10), last = n;
            if (*end == '-' && end != argv[i]) last = strtol(end + 1, &end, 10);
            if (*end || n < 1 || last < n || last > 255) { fprintf(stderr, "gate2 call: '%s' is not a rank or a range of ranks\n", argv[i]); return 2; }
            for (; n <= last; n++) want[n] = any = true;
        }
    }
    if (list) {
        for (unsigned i = 0; i < nentries(); i++) {
            const Entry *e = entry_at(i);
            printf("%2d  %s\n    call:  %s\n    check: %s\n", e->rank, e->name, e->call, e->check);
            if (e->blocked) printf("    blocked by the lifter (finding 21): %s\n", e->blocked);
            printf("    in the driver: %s%s%s%s\n", e->run ? "typed in" : "not typed in", e->needs & NEEDS_IMPORTS ? "; needs imports" : "",
                   e->needs & NEEDS_TLS ? "; needs TLS" : "", e->needs & NEEDS_ENTRY ? "; needs the entry point" : "");
        }
        return 0;
    }

    start_runtime();
    Gate2Child ch;
    bool up = true;
    unsigned timeout_s = CHILD_TIMEOUT_S;
    const char *t = getenv("GATE2_TIMEOUT");    /* seconds each rank may take */
    if (t && atoi(t) > 0) timeout_s = (unsigned)atoi(t);
    if (map_only) {
        printf("gate2 call --map-only: images mapped and relocated; no imports bound, no TLS, no entry point run\n");
    } else {
        gate2_run_child(child_load, NULL, CHILD_TIMEOUT_S, &ch);
        up = WIFEXITED(ch.status) && WEXITSTATUS(ch.status) == 0;
        if (up) printf("gate2 call: the load completes; each rank loads again in its own process\n");
        else    printf("gate2 call: the load does not complete: %s\n", ch.first);
    }
    unsigned npass = 0, nfail = 0, nskip = 0, nstop = 0, nrun = 0;
    for (unsigned i = 0; i < nentries(); i++) {
        const Entry *e = entry_at(i);
        if (any && !want[e->rank]) continue;
        nrun++;
        if (!up) {
            printf("%2d  SKIP  %s: the load does not complete\n", e->rank, e->name);
            nskip++;
            continue;
        }
        gate2_run_child(child_entry, (void *)(uintptr_t)e, timeout_s, &ch);
        if (WIFEXITED(ch.status) && WEXITSTATUS(ch.status) == EXIT_PASS) npass++;
        else if (WIFEXITED(ch.status) && WEXITSTATUS(ch.status) == EXIT_FAIL) nfail++;
        else if (WIFEXITED(ch.status) && WEXITSTATUS(ch.status) == EXIT_SKIP) nskip++;
        else { printf("%2d  STOP  %s: %s\n", e->rank, e->name, ch.first); nstop++; }
    }
    printf("call: %u PASS, %u FAIL, %u SKIP, %u STOP of %u%s\n", npass, nfail, nskip, nstop, nrun,
           map_only ? " (map only)" : up ? "" : " (the load does not complete)");
    return nfail || nstop || !up ? 1 : 0;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (argc >= 2 && !strcmp(argv[1], "registry")) return mode_registry(argc - 2, argv + 2);
    if (argc == 2 && !strcmp(argv[1], "load"))     return mode_load();
    if (argc >= 2 && !strcmp(argv[1], "imports"))  return mode_imports(argc - 2, argv + 2);
    if (argc >= 2 && !strcmp(argv[1], "call"))     return mode_call(argc - 2, argv + 2);
    fprintf(stderr, "usage: gate2 registry [--exports] | load | call [--map-only] [--list] [rank ...] | imports [--load]\n");
    return 2;
}
