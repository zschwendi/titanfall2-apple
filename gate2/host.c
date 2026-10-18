/* The driver's own guest-callable functions and its second guest thread: host.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rt/handle.h"
#include "rt/heap.h"
#include "rt/log.h"
#include "rt/module.h"
#include "rt/sync.h"
#include "rt/thread.h"

#include "host.h"
#include "pool.h"

/* ---- a second guest thread ------------------------------------------------------------------- */

typedef struct {
    void (*fn)(Call *c, void *arg);
    void  *arg;
    Call  *parent;
    int    result;
    char   msg[600];
    HANDLE h;
    DWORD  id;
} Job;

static uint64_t stub_thread_main;

HOST_FN(thread_main) {                          /* DWORD WINAPI start(LPVOID job) */
    Job *job = (Job *)(uintptr_t)ARG_U64(ctx, 0);
    Call c = *job->parent;
    c.ctx = ctx;
    c.result = GATE2_PASS;
    c.msg[0] = 0;
    job->fn(&c, job->arg);
    job->result = c.result;
    memcpy(job->msg, c.msg, sizeof job->msg);
    ctx->r[0] = 0;
}

void *gate2_thread_start(Call *c, void (*fn)(Call *c2, void *arg), void *arg) {
    Job *job = calloc(1, sizeof *job);
    job->fn = fn; job->arg = arg; job->parent = c; job->result = GATE2_PASS;
    job->h = rt_thread_create(c->ctx, 0, stub_thread_main, (uint64_t)(uintptr_t)job, 0, &job->id);
    if (!job->h) { gate2_fail_text(c, "rt_thread_create failed, last error %u", (unsigned)rt_thread_get_last_error()); free(job); return NULL; }
    return job;
}

bool gate2_thread_finish(Call *c, void *token) {
    Job *job = token;
    DWORD code = 0;
    DWORD w = rt_wait_handle(c->ctx, job->h, 60000, FALSE);
    if (w != 0) { gate2_fail_text(c, "the second guest thread did not end in 60 s (wait result %#x)", (unsigned)w); return false; }
    rt_thread_get_exit_code(job->h, &code);
    rt_handle_close(job->h);
    bool ok = false;
    if (job->result == GATE2_FAIL) gate2_fail_text(c, "on the second thread: %s", job->msg);
    else if (job->result == GATE2_SKIP) gate2_skip(c, "%s", job->msg);
    else if (code != 0) gate2_fail_text(c, "the second guest thread ended with code %u", (unsigned)code);
    else ok = true;
    free(job);
    return ok;
}

bool gate2_on_thread(Call *c, void (*fn)(Call *c2, void *arg), void *arg, uint32_t *thread_id) {
    Job *job = gate2_thread_start(c, fn, arg);
    if (!job) return false;
    if (thread_id) *thread_id = job->id;
    return gate2_thread_finish(c, job);
}

/* ---- a logging listener ------------------------------------------------------------------------ */
/* tier0's logging system calls slot 1 of each registered listener as
 * Log(this, const context *, const char *message), where the context begins with four dwords:
 * channel id, channel flags, severity, colour (read from tier0.dll+0x10760, the routine every
 * spew function ends in).  Slot 0 is never called on the paths read; it reports if it is. */

static uint64_t       stub_listener_slot0, stub_listener_log;
static Gate2Listener *listener_rec;

HOST_FN(listener_slot0) {
    if (listener_rec) listener_rec->slot0_calls++;
    ctx->r[0] = 0;
}

HOST_FN(listener_log) {
    const uint32_t *context = (const uint32_t *)(uintptr_t)ARG_U64(ctx, 1);
    const char *text = (const char *)(uintptr_t)ARG_U64(ctx, 2);
    Gate2Listener *r = listener_rec;
    if (r) {
        unsigned i = r->calls++;
        if (i < GATE2_LISTENER_KEPT) {
            r->channel[i] = context[0];
            r->severity[i] = context[2];
            r->colour[i] = context[3];
            snprintf(r->text[i], sizeof r->text[i], "%s", text ? text : "(null)");
        }
        if (context[2] > r->worst) r->worst = context[2];
        if (text && (strstr(text, "FAIL") || strstr(text, "invalid value")) && r->fails++ == 0)
            snprintf(r->first_fail, sizeof r->first_fail, "%s", text);
        /* A debugging aid for a rank that hangs late (rank 29): GATE2_TRACE_AFTER=<n> turns the
         * runtime's trace and thread channels on when the n-th message that begins with "done"
         * arrives (each list or queue self-test ends with one), so that the log (RT_LOG_FILE)
         * holds only what follows. */
        static int trace_after = -1, done;
        if (trace_after < 0) { const char *e = getenv("GATE2_TRACE_AFTER"); trace_after = e ? atoi(e) : 0; }
        if (trace_after > 0 && text && !strncmp(text, "done", 4) && ++done == trace_after) {
            rt_log_level[RT_CH_TRACE] = RT_LL_DEBUG;
            rt_log_level[RT_CH_THREAD] = RT_LL_DEBUG;
        }
        gate2_pool_touch();                     /* a message is progress, for the pool watcher */
        if (r->echo && text) {
            char line[160];
            snprintf(line, sizeof line, "message %u: %s", r->calls, text);
            for (char *p = line; *p; p++) if (*p == '\n') *p = ' ';
            gate2_progress(line);
        }
    }
    ctx->r[0] = 0;
}

uint64_t gate2_listener(Gate2Listener *rec) {
    uint64_t *vtable = rt_galloc(16), *obj = rt_galloc(8);
    vtable[0] = stub_listener_slot0;
    vtable[1] = stub_listener_log;
    obj[0] = (uint64_t)(uintptr_t)vtable;
    memset(rec, 0, sizeof *rec);
    listener_rec = rec;
    return (uint64_t)(uintptr_t)obj;
}

/* ---- start-up ---------------------------------------------------------------------------------- */

void gate2_host_before_seal(void *user) {
    (void)user;
    RtModule *m = rt_module_find("ntdll.dll");
    stub_thread_main = rt_native_stub(m, "gate2_thread_main", thread_main);
    stub_listener_slot0 = rt_native_stub(m, "gate2_listener_slot0", listener_slot0);
    stub_listener_log = rt_native_stub(m, "gate2_listener_log", listener_log);
    gate2_pool_before_seal(m);
    gate2_wide_before_seal(m);
}
