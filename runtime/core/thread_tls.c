/* TLS slots: TlsAlloc, TlsFree, TlsGetValue, TlsSetValue (docs/RUNTIME.md section 6.3).
 * Interface and specification: rt/thread.h, "TLS slots".  Every function here does what
 * that specification says.
 *
 * State: one bitmap of the allocated indices, guarded by the THREAD lock
 * (rt_thread_list_lock, thread_priv.h).  The values are in guest memory: indices 0 to 63 at
 * teb + RT_TEB_TLS_SLOTS, indices 64 to 1,087 in the expansion array whose address is at
 * teb + RT_TEB_TLS_EXPANSION (one rt_galloc block of RT_TLS_EXPANSION_SLOTS * 8 bytes, made
 * by rt_tls_slot with `create`; core/thread.c frees it when the thread ends).  A thread
 * reads and writes its own slots with no lock (LD64, ST64); rt_tls_free writes the zero into
 * the other threads' slots under the THREAD lock, walking rt_thread_list_head().
 */
#include "thread_priv.h"
#include "rt/heap.h"

#define TLS_SLOTS  (RT_TLS_MINIMUM_SLOTS + RT_TLS_EXPANSION_SLOTS)     /* 1,088 */

/* Bit i set: index i is allocated.  THREAD lock. */
static uint64_t tls_bitmap[TLS_SLOTS / 64];

static uint64_t tls_bit(DWORD index) { return UINT64_C(1) << (index % 64); }

DWORD rt_tls_alloc(void) {
    DWORD index = TLS_SLOTS;

    rt_thread_list_lock();
    for (DWORD i = 0; i < TLS_SLOTS; i++) {
        if (!(tls_bitmap[i / 64] & tls_bit(i))) {
            tls_bitmap[i / 64] |= tls_bit(i);
            index = i;
            break;
        }
    }
    rt_thread_list_unlock();

    if (index == TLS_SLOTS) rt_fatal("TlsAlloc: all 1088 TLS indices are allocated");
    return index;
}

BOOL rt_tls_free(DWORD index) {
    bool freed = false;

    rt_thread_list_lock();
    if (index < TLS_SLOTS && (tls_bitmap[index / 64] & tls_bit(index))) {
        tls_bitmap[index / 64] &= ~tls_bit(index);
        for (RtThread *t = rt_thread_list_head(); t; t = t->next) {
            if (index < RT_TLS_MINIMUM_SLOTS) {
                ST64(t->teb + RT_TEB_TLS_SLOTS + 8 * index, 0);
            } else {
                uint64_t expansion = LD64(t->teb + RT_TEB_TLS_EXPANSION);
                if (expansion) ST64(expansion + 8 * (index - RT_TLS_MINIMUM_SLOTS), 0);
            }
        }
        freed = true;
    }
    rt_thread_list_unlock();

    if (!freed) rt_thread_set_last_error(ERROR_INVALID_PARAMETER);
    return freed;
}

uint64_t rt_tls_get(Ctx *ctx, DWORD index) {
    if (index >= TLS_SLOTS) return FAIL(ctx, ERROR_INVALID_PARAMETER, 0);
    uint64_t slot = rt_tls_slot(ctx, index, false);
    rt_set_last_error(ctx, ERROR_SUCCESS);
    return slot ? LD64(slot) : 0;
}

BOOL rt_tls_set(Ctx *ctx, DWORD index, uint64_t value) {
    if (index >= TLS_SLOTS) return FAIL(ctx, ERROR_INVALID_PARAMETER, FALSE);
    uint64_t slot = rt_tls_slot(ctx, index, true);
    ST64(slot, value);
    return TRUE;
}

uint64_t rt_tls_slot(Ctx *ctx, DWORD index, bool create) {
    if (index >= TLS_SLOTS) return 0;
    uint64_t teb = RT_THREAD(ctx)->teb;
    if (index < RT_TLS_MINIMUM_SLOTS) return teb + RT_TEB_TLS_SLOTS + 8 * index;

    uint64_t expansion = LD64(teb + RT_TEB_TLS_EXPANSION);
    if (!expansion) {
        if (!create) return 0;
        expansion = (uint64_t)(uintptr_t)rt_galloc(RT_TLS_EXPANSION_SLOTS * 8);
        ST64(teb + RT_TEB_TLS_EXPANSION, expansion);
    }
    return expansion + 8 * (index - RT_TLS_MINIMUM_SLOTS);
}
