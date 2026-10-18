/* FLS slots: FlsAlloc, FlsFree, FlsGetValue, FlsSetValue and the callbacks at thread exit
 * (docs/RUNTIME.md section 6.3).  Interface and specification: rt/thread.h, "FLS slots", and
 * rt_fls_thread_exit in thread_priv.h.
 *
 * State: which indices are allocated and each index's callback, guarded by the THREAD lock
 * (rt_thread_list_lock, thread_priv.h).  The values are per thread in RtThreadPriv.fls; the
 * rules for that array are at its declaration: the owner reads and writes its own values
 * with no lock, another thread (rt_fls_free) swaps one to zero under the THREAD lock.
 *
 * A value is called for exactly once.  Whoever swaps a nonzero value to zero owns the call
 * for it, and both places that swap (rt_fls_free for every thread, rt_fls_thread_exit for
 * its own) do so under the THREAD lock together with the read of the index's callback: a
 * free and an exit that meet at one index cannot each leave the call to the other.  No
 * callback runs with the lock held (a callback may call FlsAlloc and FlsFree).
 */
#include <stdlib.h>

#include "thread_priv.h"

/* Bit i set: index i is allocated.  Bit 0 is never set.  THREAD lock. */
static uint64_t fls_bitmap[RT_FLS_SLOTS / 64];
/* The callback of each allocated index, or 0.  THREAD lock. */
static GUESTFN  fls_callback[RT_FLS_SLOTS];

static uint64_t fls_bit(DWORD index) { return UINT64_C(1) << (index % 64); }
static bool     fls_valid(DWORD index) { return index != 0 && index < RT_FLS_SLOTS; }

DWORD rt_fls_alloc(GUESTFN callback) {
    DWORD index = 0;

    rt_thread_list_lock();
    for (DWORD i = 1; i < RT_FLS_SLOTS; i++) {
        if (!(fls_bitmap[i / 64] & fls_bit(i))) {
            fls_bitmap[i / 64] |= fls_bit(i);
            fls_callback[i] = callback;
            index = i;
            break;
        }
    }
    rt_thread_list_unlock();

    if (!index) rt_fatal("FlsAlloc: all 127 FLS indices are allocated");
    return index;
}

BOOL rt_fls_free(Ctx *ctx, DWORD index) {
    RtThread *self = rt_thread_self();
    uint64_t *values = NULL;                    /* collected: the caller's own first, then newest thread first */
    size_t    n = 0, cap = 0;
    GUESTFN   callback = 0;
    bool      freed = false;

    if (!fls_valid(index)) { rt_thread_set_last_error(ERROR_INVALID_PARAMETER); return FALSE; }

    /* The list can grow between the count and the walk: the walk is repeated with more room
     * until it fits, so that nothing is allocated with the lock held. */
    for (;;) {
        size_t threads = 0;
        bool   fits = true;
        rt_thread_list_lock();
        if (!(fls_bitmap[index / 64] & fls_bit(index))) { rt_thread_list_unlock(); break; }
        for (RtThread *t = rt_thread_list_head(); t; t = t->next) threads++;
        if (threads > cap) fits = false;
        else {
            callback = fls_callback[index];
            for (int own = 1; own >= 0; own--) {
                for (RtThread *t = rt_thread_list_head(); t; t = t->next) {
                    if ((t == self) != (own == 1)) continue;
                    uint64_t *a = __atomic_load_n(&RT_THREAD_PRIV(t)->fls, __ATOMIC_SEQ_CST);
                    uint64_t  v = a ? __atomic_exchange_n(&a[index], 0, __ATOMIC_SEQ_CST) : 0;
                    if (v) values[n++] = v;
                }
            }
            fls_bitmap[index / 64] &= ~fls_bit(index);
            fls_callback[index] = 0;
            freed = true;
        }
        rt_thread_list_unlock();
        if (fits) break;
        free(values);
        cap = threads + 8;
        values = malloc(cap * sizeof *values);
        if (!values) rt_fatal("out of memory");
    }

    if (!freed) {
        free(values);
        rt_thread_set_last_error(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (callback && n) {
        if (!self || !ctx || RT_THREAD(ctx) != self)
            rt_fatal("FlsFree: a callback is due and the calling host thread is not a guest thread");
        for (size_t i = 0; i < n; i++) rt_guest_call(ctx, callback, 1, &values[i]);
    }
    free(values);
    return TRUE;
}

uint64_t rt_fls_get(Ctx *ctx, DWORD index) {
    if (!fls_valid(index)) return FAIL(ctx, ERROR_INVALID_PARAMETER, 0);
    uint64_t *a = __atomic_load_n(&RT_THREAD_PRIV(RT_THREAD(ctx))->fls, __ATOMIC_SEQ_CST);
    rt_set_last_error(ctx, ERROR_SUCCESS);
    return a ? __atomic_load_n(&a[index], __ATOMIC_SEQ_CST) : 0;
}

BOOL rt_fls_set(Ctx *ctx, DWORD index, uint64_t value) {
    if (!fls_valid(index)) return FAIL(ctx, ERROR_INVALID_PARAMETER, FALSE);
    RtThreadPriv *p = RT_THREAD_PRIV(RT_THREAD(ctx));
    uint64_t *a = __atomic_load_n(&p->fls, __ATOMIC_SEQ_CST);
    if (!a) {
        /* Only the owner makes the array, so there is no race for the pointer. */
        a = calloc(RT_FLS_SLOTS, sizeof *a);
        if (!a) rt_fatal("out of memory");
        __atomic_store_n(&p->fls, a, __ATOMIC_SEQ_CST);
    }
    __atomic_store_n(&a[index], value, __ATOMIC_SEQ_CST);
    return TRUE;
}

void rt_fls_thread_exit(Ctx *ctx) {
    RtThreadPriv *p = RT_THREAD_PRIV(RT_THREAD(ctx));
    uint64_t *a = __atomic_load_n(&p->fls, __ATOMIC_SEQ_CST);
    if (!a) return;

    /* One pass in rising order. */
    for (DWORD i = 1; i < RT_FLS_SLOTS; i++) {
        if (!__atomic_load_n(&a[i], __ATOMIC_SEQ_CST)) continue;
        rt_thread_list_lock();
        uint64_t v = __atomic_exchange_n(&a[i], 0, __ATOMIC_SEQ_CST);
        GUESTFN  callback = (fls_bitmap[i / 64] & fls_bit(i)) ? fls_callback[i] : 0;
        rt_thread_list_unlock();
        if (v && callback) rt_guest_call(ctx, callback, 1, &v);
    }
    /* What a callback stored at an index the pass had left behind is not called for. */
    for (DWORD i = 1; i < RT_FLS_SLOTS; i++)
        if (__atomic_load_n(&a[i], __ATOMIC_SEQ_CST))
            rt_log(RT_CH_THREAD, RT_LL_WARN, "FLS value set during thread exit is dropped (index %u)", i);

    rt_thread_list_lock();                      /* a concurrent rt_fls_free never sees a freed array */
    __atomic_store_n(&p->fls, NULL, __ATOMIC_SEQ_CST);
    rt_thread_list_unlock();
    free(a);
}
