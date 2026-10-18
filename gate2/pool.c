/* Watching vstdlib's thread-pool self test (rank 29): pool.h says what and why. */
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "rt/log.h"
#include "rt/module.h"
#include "rt/thread.h"
#include "rt/vm.h"
#include "rt/win/memory.h"

#include "host.h"
#include "pool.h"

/* What was read from vstdlib.dll (docs/handoff/gate2-threads.md, stop 8).  All of it is checked
 * against the mapped image before anything is changed: another build of the DLL is refused. */
#define POOL_VTABLE      0x5bcc8u       /* the vtable RunThreadPoolTests gives the pool on its stack */
#define SLOT_START       3              /* Start(pool, const params *, name): params[0] = threads asked for */
#define SLOT_STOP        5              /* Stop(pool, timeout) */
#define SLOT_ADD         15             /* the add routine: (pool, job) */
#define RVA_START        0x18740u
#define RVA_STOP         0x18a10u
#define RVA_ADD          0x156a0u
#define JOB_STATUS       0x18           /* dword: 0 finished, 1 queued, 2 running, 3 aborted, 4 never queued */
#define JOB_POOL_DATA    0x50           /* qword: -1 unless a pool has put its own data there */
#define POOL_THREADS     0x240          /* dword: threads the pool has */
#define RVA_COUNTED      0x78f18u       /* dword: jobs of the round that have run (each job adds 1) */
#define RVA_TO_COUNT     0x78ee8u       /* dword: jobs of the round (4,000) */

#define HOST_FN(name) \
    static void name##_body(Ctx *ctx); \
    static void name(Ctx *ctx) { \
        tf_fp_enter_host(); \
        RtThread *t_ = RT_THREAD(ctx); \
        uint32_t was_ = t_->in_host; \
        uint64_t rsp_ = ctx->r[4]; \
        t_->in_host = 1; \
        name##_body(ctx); \
        t_->in_host = was_; \
        ctx->r[4] = rsp_ + 8;                   /* the guest `ret` */ \
        tf_fp_enter_guest(ctx->mxcsr); \
    } \
    static void name##_body(Ctx *ctx)

static uint64_t      stub_start, stub_stop, stub_add;   /* the driver's routines, as guest addresses */
static uint64_t      real_start, real_stop, real_add;   /* vstdlib's */
static uint64_t      vstdlib_base;
static Gate2Pool    *rec;
static bool          rearm, say;
static atomic_uint   stamp;                             /* goes up at every call of a watched routine */

void gate2_pool_touch(void) { atomic_fetch_add(&stamp, 1); }

static uint32_t rd32(uint64_t a) { uint32_t v; memcpy(&v, (const void *)(uintptr_t)a, 4); return v; }
static uint64_t rd64(uint64_t a) { uint64_t v; memcpy(&v, (const void *)(uintptr_t)a, 8); return v; }

static void round_line(const Gate2PoolRound *r, unsigned n) {
    char line[300];
    int len = snprintf(line, sizeof line,
                       "POOL round %u: %u thread(s); %u add(s): %u never queued, %u queued, %u running, %u finished, %u aborted, "
                       "%u other; %u taken, %u refused, %u re-armed by the driver; %u of %u counted when the pool stopped\n",
                       n, r->threads, r->adds, r->status[4], r->status[1], r->status[2], r->status[0], r->status[3], r->other,
                       r->taken, r->adds - r->taken, r->rearmed, r->counted, r->to_count);
    if (len > 0 && write(2, line, (size_t)len) < 0) { }
}

HOST_FN(pool_start) {                           /* bool Start(pool, const params *, const char *name) */
    uint64_t pool = ARG_U64(ctx, 0), params = ARG_U64(ctx, 1), name = ARG_U64(ctx, 2);
    atomic_fetch_add(&stamp, 1);
    if (rec) {
        memset(&rec->now, 0, sizeof rec->now);
        rec->now.threads = params ? rd32(params) : 0;
        rec->in_round = true;
    }
    ctx->r[0] = GUEST_CALL(ctx, real_start, pool, params, name);
}

HOST_FN(pool_stop) {                            /* bool Stop(pool, int timeout) */
    uint64_t pool = ARG_U64(ctx, 0), timeout = ARG_U64(ctx, 1);
    atomic_fetch_add(&stamp, 1);
    uint64_t r = GUEST_CALL(ctx, real_stop, pool, timeout);
    if (rec && rec->in_round) {                 /* the workers have ended: the count is final */
        rec->now.counted = rd32(vstdlib_base + RVA_COUNTED);
        rec->now.to_count = rd32(vstdlib_base + RVA_TO_COUNT);
        rec->rounds++;
        if (rec->now.adds && rec->now.counted == rec->now.to_count) rec->rounds_all_counted++;
        if (rec->now.adds && !rec->now.taken) rec->rounds_none_taken++;
        rec->total_adds += rec->now.adds;
        rec->total_taken += rec->now.taken;
        rec->total_rearmed += rec->now.rearmed;
        rec->total_counted += rec->now.counted;
        if (say) round_line(&rec->now, rec->rounds);
        rec->last = rec->now;
        rec->in_round = false;
    }
    atomic_fetch_add(&stamp, 1);
    ctx->r[0] = r;
}

HOST_FN(pool_add) {                             /* void add(pool, job) */
    uint64_t pool = ARG_U64(ctx, 0), job = ARG_U64(ctx, 1);
    atomic_fetch_add(&stamp, 1);
    if (rec) {
        Gate2PoolRound *r = &rec->now;
        r->adds++;
        if (!job) {
            r->other++;
        } else {
            uint32_t status = rd32(job + JOB_STATUS);
            if (rearm && (status == 0 || status == 3)) {
                /* The diagnostic: put a job that has finished or was aborted back to "never
                 * queued", which is what the routine needs to take it and what the test routine
                 * itself never does between its rounds. */
                uint32_t fresh = 4;
                memcpy((void *)(uintptr_t)(job + JOB_STATUS), &fresh, 4);
                r->rearmed++;
            }
            if (status <= 4) r->status[status]++; else r->other++;
            status = rd32(job + JOB_STATUS);
            /* What the routine does next, as read: it goes on to queue the job only when the
             * job's pool data is -1, the pool has a thread and the status is 1 or 4. */
            if (rd64(job + JOB_POOL_DATA) == UINT64_MAX && rd32(pool + POOL_THREADS) != 0 && (status == 1 || status == 4)) r->taken++;
        }
    }
    ctx->r[0] = GUEST_CALL(ctx, real_add, pool, job);
}

void gate2_pool_before_seal(RtModule *ntdll) {
    stub_start = rt_native_stub(ntdll, "gate2_pool_start", pool_start);
    stub_stop = rt_native_stub(ntdll, "gate2_pool_stop", pool_stop);
    stub_add = rt_native_stub(ntdll, "gate2_pool_add", pool_add);
}

static bool put_slot(Call *c, unsigned slot, uint32_t want_rva, uint64_t stub, uint64_t *real) {
    uint64_t at = vstdlib_base + POOL_VTABLE + 8ull * slot;
    uint64_t have = rd64(at);
    if (have != vstdlib_base + want_rva) {
        gate2_fail_text(c, "the pool watch was written for another vstdlib.dll: slot %u of the vtable at +0x%x holds +0x%llx, not +0x%x",
                        slot, POOL_VTABLE, (unsigned long long)(have - vstdlib_base), want_rva);
        return false;
    }
    DWORD old = 0, old2 = 0;
    if (!rt_vm_protect(at, 8, PAGE_READWRITE, &old)) {
        gate2_fail_text(c, "the pool watch cannot make vtable slot %u writable (error %u)", slot, (unsigned)rt_thread_get_last_error());
        return false;
    }
    memcpy((void *)(uintptr_t)at, &stub, 8);
    rt_vm_protect(at, 8, old, &old2);
    *real = have;
    return true;
}

bool gate2_pool_watch(Call *c, Gate2Pool *r) {
    const char *e;
    memset(r, 0, sizeof *r);
    vstdlib_base = c->vstdlib->base;
    rearm = (e = getenv("GATE2_POOL_REARM")) != NULL && atoi(e) > 0;
    say = (e = getenv("GATE2_POOL_LOG")) != NULL && atoi(e) > 0;
    r->rearm = rearm;
    if (!put_slot(c, SLOT_START, RVA_START, stub_start, &real_start)) return false;
    if (!put_slot(c, SLOT_STOP, RVA_STOP, stub_stop, &real_stop)) return false;
    if (!put_slot(c, SLOT_ADD, RVA_ADD, stub_add, &real_add)) return false;
    rec = r;
    return true;
}

/* ---- the watcher: a host thread that recognises the one stop this file explains ---------------- */

static struct { Call *c; unsigned idle_ms; void (*verdict)(Call *c); } watcher;

static void *watcher_main(void *arg) {
    (void)arg;
    unsigned seen = atomic_load(&stamp), idle = 0;
    for (;;) {
        usleep(100 * 1000);
        unsigned s = atomic_load(&stamp);
        if (s != seen) { seen = s; idle = 0; continue; }
        idle += 100;
        if (idle < watcher.idle_ms) continue;
        /* Nothing watched has been called for idle_ms.  The stop is the predicted one only if
         * the round in progress has had all its adds, every one of a job that had finished,
         * none taken and none counted: then no job is queued or running and nothing can set
         * the event the test routine is waiting for.  Anything else is left to the time limit. */
        const Gate2PoolRound *r = &rec->now;
        if (rec->in_round && !rec->rearm && r->adds && r->adds == rd32(vstdlib_base + RVA_TO_COUNT) && r->taken == 0 &&
            r->status[0] == r->adds && rd32(vstdlib_base + RVA_COUNTED) == 0) {
            rec->stopped_as_predicted = true;
            rec->now.counted = 0;
            rec->now.to_count = r->adds;
            if (say) round_line(r, rec->rounds + 1);
            watcher.verdict(watcher.c);         /* does not return */
        }
        idle = 0;
    }
    return NULL;
}

void gate2_pool_watcher(Call *c, unsigned idle_ms, void (*verdict)(Call *c)) {
    pthread_t t;
    watcher.c = c;
    watcher.idle_ms = idle_ms;
    watcher.verdict = verdict;
    if (pthread_create(&t, NULL, watcher_main, NULL) == 0) pthread_detach(t);
}
