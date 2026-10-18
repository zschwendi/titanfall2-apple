/* The parts of the Gate 2 driver that the Gate 3 driver uses too: see modes.h.  Moved here
 * from driver.c unchanged but for the list of modules, which is now an argument. */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "rt/crash.h"
#include "rt/log.h"

#include "modes.h"

void gate2_mods_collect(Gate2Mods *g, const char *prog) {
    g->prog = prog;
    g->nlifted = 0;
    for (unsigned i = 0; i < rt_module_count(); i++) {
        RtModule *m = rt_module_at(i);
        if (m->native) continue;
        if (g->nlifted == GATE2_MAX_LIFTED) { fprintf(stderr, "%s: more than %d lifted modules\n", prog, GATE2_MAX_LIFTED); exit(2); }
        g->lifted[g->nlifted] = m;
        gate2_scan(m, &g->scans[g->nlifted]);
        g->nlifted++;
    }
}

const Gate2Scan *gate2_scan_of(const Gate2Mods *g, const RtModule *m) {
    for (unsigned i = 0; i < g->nlifted; i++) if (g->lifted[i] == m) return &g->scans[i];
    return NULL;
}

/* The table is sorted by rva. */
bool gate2_has_lifted_entry(const RtModule *m, uint32_t rva) {
    const RtLiftedCode *code = m->def->code;
    size_t lo = 0, hi = *code->count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (code->table[mid].rva == rva) return !rt_lifted_skip(code, rva);
        if (code->table[mid].rva < rva) lo = mid + 1; else hi = mid;
    }
    return false;
}

/* What a call to a function without a lifted entry does: the lifter skipped it, and the stub
 * in its row of the table stops with the reason, or the table has no row for the address. */
static const char *unlifted_call(const RtModule *m, uint32_t rva, char *buf, size_t n) {
    const LiftedSkip *skip = rt_lifted_skip(m->def->code, rva);
    if (skip) snprintf(buf, n, "skipped by the lifter, %s at +%#x: a call stops with that reason", skip->why, skip->at);
    else snprintf(buf, n, "no row in the lifted table: a call is a dispatch miss");
    return buf;
}

const Gate2Export *gate2_find_export(const Gate2Scan *s, const char *name) {
    for (unsigned i = 0; i < s->nexports; i++)
        if (s->exports[i].name && !strcmp(s->exports[i].name, name)) return &s->exports[i];
    return NULL;
}

double gate2_now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e3 + (double)t.tv_nsec / 1e6;
}

const char *gate2_error_name(uint32_t e) {
    return e == 126 ? "ERROR_MOD_NOT_FOUND" : e == 1114 ? "ERROR_DLL_INIT_FAILED" : e == 87 ? "ERROR_INVALID_PARAMETER" : "?";
}

/* ---- registry ---------------------------------------------------------------------------------- */

int gate2_mode_registry(const Gate2Mods *g, bool all) {
    printf("%s registry: %u lifted modules of %u in the registry\n", g->prog, g->nlifted, rt_module_count());
    for (unsigned i = 0; i < g->nlifted; i++) {
        const RtModule *m = g->lifted[i];
        const Gate2Scan *s = &g->scans[i];
        unsigned kinds[3] = { 0, 0, 0 }, unlifted = 0;
        for (unsigned k = 0; k < s->nexports; k++) {
            kinds[s->exports[k].kind]++;
            unlifted += s->exports[k].kind == GATE2_EXP_CODE && !gate2_has_lifted_entry(m, s->exports[k].rva);
        }
        printf("module %s %s base=%#llx size=%#llx lifted=%zu skipped=%zu", m->name, s->present ? "present" : "absent",
               (unsigned long long)m->base, (unsigned long long)m->size, *m->def->code->count - *m->def->code->nskips,
               *m->def->code->nskips);
        if (s->present)
            printf(" exports=%u functions=%u data=%u forwarded=%u unlifted=%u imports=%u entry=+%#x tls_index=%d sections=%u",
                   s->nexports, kinds[GATE2_EXP_CODE], kinds[GATE2_EXP_DATA], kinds[GATE2_EXP_FORWARDED], unlifted,
                   s->nimports, s->entry_rva, s->tls_index, s->nsections);
        printf("\n");
    }
    for (unsigned i = 0; i < g->nlifted; i++) {
        const RtModule *m = g->lifted[i];
        const Gate2Scan *s = &g->scans[i];
        char why[256];
        if (!s->present) { printf("%s: no DLL file under the install root; rt_module_load will fail with ERROR_MOD_NOT_FOUND\n", m->name); continue; }
        printf("%s: file %s\n", m->name, s->host_path);
        if (s->entry_rva && !gate2_has_lifted_entry(m, s->entry_rva))
            printf("%s: THE ENTRY POINT +%#x HAS NO LIFTED ENTRY: the load will not get past it (%s)\n", m->name, s->entry_rva,
                   unlifted_call(m, s->entry_rva, why, sizeof why));
        for (unsigned k = 0; k < s->nexports; k++) {
            const Gate2Export *x = &s->exports[k];
            bool miss = x->kind == GATE2_EXP_CODE && !gate2_has_lifted_entry(m, x->rva);
            if (miss)
                printf("%s: exported function with no lifted entry (%s): +%#x #%u %s\n",
                       m->name, unlifted_call(m, x->rva, why, sizeof why), x->rva, x->ordinal, x->name ? x->name : "(no name)");
            if (all)
                printf("export %s #%u +%#x %s %s\n", m->name, x->ordinal, x->rva,
                       x->kind == GATE2_EXP_DATA ? "data" : x->kind == GATE2_EXP_FORWARDED ? "forwarded" : miss ? "function-unlifted" : "function",
                       x->name ? x->name : "(no name)");
        }
    }
    if (*gate2_standins())
        printf("note: this build links the driver's stand-ins for %s, which are stubs in runtime/core (gate2/peek.c)\n", gate2_standins());
    return 0;
}

/* ---- imports ------------------------------------------------------------------------------------- */

static const char *status_name(uint8_t st) {
    static const char *const names[] = { "?", "full", "partial", "absent", "advisory", "trap" };
    return st < sizeof names / sizeof names[0] ? names[st] : "?";
}

int gate2_mode_imports(const Gate2Mods *g, bool loaded) {
    if (loaded) {
        printf("%s imports: what each import slot holds after the load, with the binding computed from the tables beside it\n", g->prog);
    } else {
        printf("%s imports: computed from the generated tables (the shim table, the lifted function tables) and each DLL's "
               "import table; nothing is loaded, so no bound slot was read\n", g->prog);
    }
    for (unsigned i = 0; i < g->nlifted; i++) {
        const RtModule *m = g->lifted[i];
        const Gate2Scan *s = &g->scans[i];
        unsigned nshim = 0, ntrap = 0, nlift = 0, nbad = 0, nwrong = 0;
        if (!s->present) { printf("%s: absent\n", m->name); continue; }
        for (unsigned k = 0; k < s->nimports; k++) {
            const Gate2Import *im = &s->imports[k];
            RtModule *from = rt_module_find(im->dll);
            char label[24], what[400];
            uint64_t addr = 0;
            snprintf(label, sizeof label, "#%u", im->ordinal);
            if (!from) {
                snprintf(what, sizeof what, "NO SUCH MODULE in the manifest");
                nbad++;
            } else if (from->native) {
                addr = rt_native_import(im->dll, im->name, im->ordinal);
                uint64_t off = addr - from->base;
                if (off >= RT_NATIVE_TRAP_STUBS) {
                    snprintf(what, sizeof what, "trap stub (no shim%s)", from->kind == RT_MOD_STUBBED ? "; a stubbed game DLL" : "");
                    ntrap++;
                } else {
                    const RtShimEntry *e = from->shims[(off - RT_NATIVE_SHIM_STUBS) / RT_STUB_SIZE];
                    snprintf(what, sizeof what, "shim (%s) %s", status_name(e->status), e->file);
                    nshim++;
                }
            } else {
                const Gate2Scan *fs = gate2_scan_of(g, from);
                const Gate2Export *x = fs && fs->present && im->name ? gate2_find_export(fs, im->name) : NULL;
                if (!x) {
                    snprintf(what, sizeof what, "NO SUCH EXPORT in lifted module %s", from->name);
                    nbad++;
                } else {
                    addr = from->base + x->rva;
                    bool miss = x->kind == GATE2_EXP_CODE && !gate2_has_lifted_entry(from, x->rva);
                    char why[256];
                    snprintf(what, sizeof what, "lifted %s+%#x%s%s%s", from->name, x->rva,
                             x->kind == GATE2_EXP_DATA ? " (data)" : miss ? " (NO LIFTED ENTRY: " : "",
                             miss ? unlifted_call(from, x->rva, why, sizeof why) : "", miss ? ")" : "");
                    nlift++;
                    nbad += miss;
                }
            }
            printf("import %s %s %s -> %s", m->name, im->dll, im->name ? im->name : label, what);
            if (loaded) {
                uint64_t slot;
                memcpy(&slot, (const void *)(uintptr_t)(m->base + im->slot_rva), 8);
                if (slot != addr) { printf("  SLOT HOLDS %#llx, computed %#llx", (unsigned long long)slot, (unsigned long long)addr); nwrong++; }
            }
            printf("\n");
        }
        printf("imports %s: %u in all: %u shim, %u trap stub, %u from lifted modules, %u unresolvable or unlifted",
               m->name, s->nimports, nshim, ntrap, nlift, nbad);
        if (loaded) printf(", %u slots differ from the computed binding", nwrong);
        printf("\n");
    }
    return 0;
}

/* ---- a child process ----------------------------------------------------------------------------- */

/* The time limit of a child: get the runtime's log out (RT_LOG_FILE is buffered), then end by
 * the same signal, so that the parent still sees a child stopped by SIGALRM. */
static void on_alarm(int sig) {
    rt_log_flush_try();
    signal(sig, SIG_DFL);
    raise(sig);
}

bool gate2_quiet_stop(void) {
    const char *q = getenv("GATE_QUIET_STOP");
    return !(q && !strcmp(q, "0"));
}

void gate2_run_child(void (*fn)(void *), void *arg, unsigned timeout_s, Gate2Child *res) {
    char path[1100];
    static char out[1 << 16];
    const char *tmp = getenv("TMPDIR");
    snprintf(path, sizeof path, "%s/gate2-child.XXXXXX", tmp && *tmp ? tmp : "/tmp");
    int fd = mkstemp(path);
    if (fd < 0) { perror("mkstemp"); exit(2); }
    unlink(path);
    fflush(stdout);
    fflush(stderr);
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); exit(2); }
    if (pid == 0) {
        if (gate2_quiet_stop()) rt_report_quiet(RT_QUIET_STOPS);       /* modes.h, THE QUIET STOP */
        signal(SIGALRM, on_alarm);
        alarm(timeout_s);
        if (dup2(fd, 2) < 0) _exit(98);
        close(fd);
        fn(arg);
        fflush(stdout);
        _exit(0);
    }
    res->status = 0;
    while (waitpid(pid, &res->status, 0) < 0 && errno == EINTR) { }
    ssize_t n = pread(fd, out, sizeof out - 1, 0);
    out[n > 0 ? n : 0] = 0;
    const char *keep = getenv("GATE2_STDERR");  /* a file that gets what each child wrote to descriptor 2 */
    if (keep && *keep) {
        FILE *f = fopen(keep, "a");
        if (f) { fputs(out, f); fclose(f); }
    }
    close(fd);
    const char *line = strstr(out, "FATAL");
    bool timed_out = WIFSIGNALED(res->status) && WTERMSIG(res->status) == SIGALRM;
    if (!line) {                                /* the first line that is not a progress line */
        line = out + strspn(out, "\n");
        while (!strncmp(line, "AT ", 3) || !strncmp(line, "POOL ", 5)) { line += strcspn(line, "\n"); line += strspn(line, "\n"); }
    }
    snprintf(res->first, sizeof res->first, "%.*s", (int)strcspn(line, "\n"), line);
    snprintf(res->report, sizeof res->report, "%s", line);
    if (timed_out && strncmp(res->first, "FATAL", 5) != 0)      /* whatever it wrote first is not why it stopped */
        res->first[0] = 0;
    if (!res->first[0]) {
        if (timed_out)
            snprintf(res->first, sizeof res->first, "no answer after %u s (stopped by the driver)", timeout_s);
        else if (WIFSIGNALED(res->status))
            snprintf(res->first, sizeof res->first, "ended by signal %d with nothing written", WTERMSIG(res->status));
        else if (WEXITSTATUS(res->status) == RT_REPORT_QUIET_STATUS)   /* the report went elsewhere (RT_LOG_FILE) */
            snprintf(res->first, sizeof res->first, "exit status %d (a fatal report's quiet end, in place of signal %d) with nothing written",
                     WEXITSTATUS(res->status), SIGABRT);
        else
            snprintf(res->first, sizeof res->first, "exit status %d with nothing written", WEXITSTATUS(res->status));
    }
    const char *at[4] = { NULL };               /* the last gate2_progress lines, newest last */
    for (const char *p = out; (p = strstr(p, "AT ")) != NULL; p += 3)
        if (p == out || p[-1] == '\n') { at[0] = at[1]; at[1] = at[2]; at[2] = at[3]; at[3] = p + 3; }
    for (int i = 0; i < 4; i++)
        if (at[i]) {
            size_t len = strlen(res->first);
            snprintf(res->first + len, sizeof res->first - len, "%s%.*s", at[i] == at[3] && (i == 0 || !at[i - 1]) ? " [during: " : i && at[i - 1] ? "; " : " [during: ",
                     (int)strcspn(at[i], "\n"), at[i]);
        }
    if (at[3]) { size_t len = strlen(res->first); snprintf(res->first + len, sizeof res->first - len, "]"); }
}
