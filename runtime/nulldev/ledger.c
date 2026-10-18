/* The ledger of invented answers, the run switches, and the device's totals (rt/nulldev.h;
 * unit d3d-fix-2; docs/reviews/null-device-redteam.md, R20, R7, R1).
 *
 * A device that draws nothing answers some questions with something it cannot know.  Each such
 * answer is counted here by KIND (one method gives several kinds, so the call table alone hides
 * them), the first of each kind is logged with its call site, and the whole is printed with
 * the call table: at the end of every fatal report and at exit (rt_com_counts_also).
 * Everything here is atomics: it may be read inside a fatal report. */
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "rt/log.h"
#include "rt/module.h"
#include "rt/nulldev.h"
#include "rt/thread.h"
#include "rt/time.h"

static const char *const kind_text[ND_INV_KINDS] = {
    "occlusion: samples that passed, of a device that draws none",
    "timestamp: the host's monotonic clock at End, in 100 ns",
    "disjoint: frequency 10,000,000 per second, not disjoint",
    "event: \"the GPU has finished\", at once",
    "read-back: zeroes for contents only a renderer could have produced (RT_D3D_READBACK=zero)",
    "present: S_OK for a frame nobody saw",
    "present: interval 0 paced as 1",
};
_Static_assert(sizeof kind_text / sizeof kind_text[0] == ND_INV_KINDS, "a text for every kind of invented answer");

static struct {
    _Atomic uint64_t count, first_caller, first_value;
    _Atomic uint32_t first_thread;
} inv[ND_INV_KINDS];

static _Atomic int64_t  resources, resources_peak, blocks, bytes, bytes_peak, dropped_bytes, masks, masks_peak, masks_made;
static _Atomic uint64_t first_present, last_present;
static _Atomic int      sw_occlusion = -1, sw_readback = -1, sw_present = -1, sw_keep = -1;

static void report(void);

void rt_nulldev_invented(Ctx *ctx, unsigned kind, uint64_t value) {
    if (kind >= ND_INV_KINDS) rt_fatal("rt_nulldev_invented: kind %u", kind);
    if (kind == ND_INV_PRESENT) {
        uint64_t now = rt_nulldev_ticks(), zero = 0;
        atomic_compare_exchange_strong(&first_present, &zero, now);
        atomic_store(&last_present, now);
    }
    if (atomic_fetch_add(&inv[kind].count, 1)) return;
    char where[160];
    uint64_t caller = LD64(ctx->r[4]);                  /* inside a method: rsp still points at the return address */
    atomic_store(&inv[kind].first_caller, caller);
    atomic_store(&inv[kind].first_value, value);
    atomic_store(&inv[kind].first_thread, RT_THREAD(ctx)->id);
    rt_com_counts_also(report);
    rt_log(RT_CH_COM, RT_LL_WARN, "d3d: first invented answer of its kind (%s): value %llu (0x%llx), asked from %s on thread 0x%x",
           kind_text[kind], (unsigned long long)value, (unsigned long long)value, rt_module_describe(caller, where, sizeof where),
           RT_THREAD(ctx)->id);
}

uint64_t rt_nulldev_invented_count(unsigned kind) { return kind < ND_INV_KINDS ? atomic_load(&inv[kind].count) : 0; }

/* ---- the switches (environment, read once; rt_nulldev_switches_reset makes the next use read again) */

static int sw(_Atomic int *cache, const char *name, const char *on) {
    int v = atomic_load(cache);
    if (v < 0) {
        const char *e = getenv(name);
        v = e && !strcmp(e, on);
        atomic_store(cache, v);
    }
    return v;
}

bool rt_nulldev_occlusion_all(void)   { return sw(&sw_occlusion, "RT_D3D_OCCLUSION", "all"); }
bool rt_nulldev_readback_zero(void)   { return sw(&sw_readback, "RT_D3D_READBACK", "zero"); }
bool rt_nulldev_present_paced(void)   { return !sw(&sw_present, "RT_D3D_PRESENT", "immediate"); }
bool rt_nulldev_keep_all(void)        { return sw(&sw_keep, "RT_D3D_KEEP", "all"); }
void rt_nulldev_switches_reset(void)  {
    atomic_store(&sw_occlusion, -1); atomic_store(&sw_readback, -1); atomic_store(&sw_present, -1); atomic_store(&sw_keep, -1);
}

uint64_t rt_nulldev_draws(void) {
    static const char *const draw[] = { "Draw", "DrawIndexed", "DrawInstanced", "DrawIndexedInstanced", "DrawInstancedIndirect",
                                        "DrawIndexedInstancedIndirect", "DrawAuto" };
    uint64_t n = 0;
    for (unsigned i = 0; i < sizeof draw / sizeof draw[0]; i++) n += rt_com_calls(&rt_iface_ID3D11DeviceContext, draw[i]);
    return n;
}

/* Present's wait: until the n-th next boundary of the device's one mode, 60 Hz, on the host's
 * monotonic clock.  Bounded: the target is at most 4/60 s away, and after a quarter of a second
 * without reaching it the wait is given up and said aloud (red team R21: nothing here may hang
 * a run silently). */
void rt_nulldev_pace(unsigned n) {
    enum { HZ = 60 };
    uint64_t start = rt_nulldev_ticks(), now = start;
    uint64_t boundary = start * HZ / ND_TICKS_PER_SECOND + n;
    uint64_t target = (boundary * ND_TICKS_PER_SECOND + HZ - 1) / HZ;
    while (now < target && now - start < ND_TICKS_PER_SECOND / 4) {
        nanosleep(&(struct timespec){ 0, (long)((target - now) * 100) }, NULL);        /* under 67 ms */
        now = rt_nulldev_ticks();
    }
    if (now < target)
        rt_log(RT_CH_COM, RT_LL_WARN, "d3d: Present waited %llu ms for a boundary %u/60 s away and gave up the wait",
               (unsigned long long)((now - start) / 10000), n);
}

/* ---- totals ---------------------------------------------------------------------------------------- */

static void peak(_Atomic int64_t *p, int64_t now) {
    int64_t old = atomic_load(p);
    while (now > old && !atomic_compare_exchange_weak(p, &old, now)) { }
}

void rt_nulldev_account(int resource_delta, int block_delta, int64_t byte_delta, int64_t dropped) {
    if (resource_delta) peak(&resources_peak, atomic_fetch_add(&resources, resource_delta) + resource_delta);
    if (block_delta) atomic_fetch_add(&blocks, block_delta);
    if (byte_delta) peak(&bytes_peak, atomic_fetch_add(&bytes, byte_delta) + byte_delta);
    if (dropped) atomic_fetch_add(&dropped_bytes, dropped);
    if (resource_delta > 0) rt_com_counts_also(report);
}

void rt_nulldev_account_mask(int delta) {
    peak(&masks_peak, atomic_fetch_add(&masks, delta) + delta);
    if (delta > 0) atomic_fetch_add(&masks_made, 1);
}

void rt_nulldev_totals(NdTotals *t) {
    t->resources = atomic_load(&resources); t->resources_peak = atomic_load(&resources_peak);
    t->blocks = atomic_load(&blocks); t->bytes = atomic_load(&bytes); t->bytes_peak = atomic_load(&bytes_peak);
    t->dropped_bytes = atomic_load(&dropped_bytes);
    t->masks = atomic_load(&masks); t->masks_peak = atomic_load(&masks_peak); t->masks_made = atomic_load(&masks_made);
}

static void report(void) {
    static char text[3072];             /* static: a fatal report may be on a small signal stack */
    char where[160];
    size_t at = 0;
    bool any = false;
    for (unsigned k = 0; k < ND_INV_KINDS; k++) any = any || atomic_load(&inv[k].count);
    if (any) at += (size_t)snprintf(text + at, sizeof text - at, "d3d: answers the device invented, by kind (a device that draws nothing cannot know them):\n");
    for (unsigned k = 0; k < ND_INV_KINDS && at < sizeof text; k++) {
        uint64_t n = atomic_load(&inv[k].count);
        if (!n) continue;
        at += (size_t)snprintf(text + at, sizeof text - at, "d3d: %10llu  %s; first from %s, thread 0x%x, value %llu\n", (unsigned long long)n,
                               kind_text[k], rt_module_describe(atomic_load(&inv[k].first_caller), where, sizeof where),
                               (unsigned)atomic_load(&inv[k].first_thread), (unsigned long long)atomic_load(&inv[k].first_value));
    }
    uint64_t frames = atomic_load(&inv[ND_INV_PRESENT].count), span = atomic_load(&last_present) - atomic_load(&first_present);
    if (frames > 1 && span && at < sizeof text)
        at += (size_t)snprintf(text + at, sizeof text - at, "d3d: %llu frames presented in %.1f s: %.1f a second (pacing %s)\n",
                               (unsigned long long)frames, (double)span / ND_TICKS_PER_SECOND,
                               (double)(frames - 1) * ND_TICKS_PER_SECOND / (double)span, rt_nulldev_present_paced() ? "on" : "off");
    if (at < sizeof text)
        at += (size_t)snprintf(text + at, sizeof text - at, "d3d: buffers and textures alive %lld (peak %lld); bytes held for them %lld in %lld "
                               "blocks (peak %lld); bytes of the game's data read and not kept %lld\n", (long long)atomic_load(&resources),
                               (long long)atomic_load(&resources_peak), (long long)atomic_load(&bytes), (long long)atomic_load(&blocks),
                               (long long)atomic_load(&bytes_peak), (long long)atomic_load(&dropped_bytes));
    if (atomic_load(&masks_made) && at < sizeof text)
        at += (size_t)snprintf(text + at, sizeof text - at, "d3d: subresources written in part, with a record of which units are the game's: "
                               "%lld made, %lld now, at most %lld at once\n", (long long)atomic_load(&masks_made),
                               (long long)atomic_load(&masks), (long long)atomic_load(&masks_peak));
    if (at < sizeof text)
        at += (size_t)snprintf(text + at, sizeof text - at, "d3d: switches: RT_D3D_OCCLUSION=%s RT_D3D_READBACK=%s RT_D3D_PRESENT=%s RT_D3D_KEEP=%s\n",
                               rt_nulldev_occlusion_all() ? "all" : "none", rt_nulldev_readback_zero() ? "zero" : "stop",
                               rt_nulldev_present_paced() ? "paced" : "immediate", rt_nulldev_keep_all() ? "all" : "mappable");
    rt_log_raw(text, at < sizeof text ? at : sizeof text - 1);
}
