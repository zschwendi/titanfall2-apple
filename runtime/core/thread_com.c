/* The COM counts of a thread: CoInitializeEx, CoUninitialize, OleInitialize
 * (docs/RUNTIME.md section 3.7: there is no COM run time).
 * Interface and specification: rt/thread.h, "the COM state of a thread".
 *
 * State: three numbers in the calling thread's RtThreadPriv (thread_priv.h), read and
 * written by that thread only.  No lock, no guest call, no allocation.
 */
#include "thread_priv.h"

static RtThreadPriv *own_priv(Ctx *ctx, const char *api) {
    if (!ctx || !ctx->host || !RT_THREAD_PRIV(RT_THREAD(ctx)))
        rt_fatal("%s: the Ctx is not a guest thread's", api);
    return RT_THREAD_PRIV(RT_THREAD(ctx));
}

HRESULT rt_com_initialize(Ctx *ctx, DWORD coinit) {
    RtThreadPriv *p = own_priv(ctx, "CoInitializeEx");
    const DWORD known = COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE | COINIT_SPEED_OVER_MEMORY;
    if (coinit & ~known)
        rt_fatal("CoInitializeEx: dwCoInit 0x%x has a bit outside the four COINIT_ values; what Windows returns is not verified",
                 (unsigned)coinit);
    const uint32_t model = coinit & COINIT_APARTMENTTHREADED;     /* COINIT_MULTITHREADED is 0 */

    if (p->com_count == 0) {
        p->com_count = 1;
        p->com_model = model;
        return S_OK;
    }
    if (p->com_model != model) return RPC_E_CHANGED_MODE;
    if (p->com_count == UINT32_MAX)
        rt_fatal("CoInitializeEx: the thread's count of initialisations is at its limit");
    p->com_count++;
    return S_FALSE;
}

void rt_com_uninitialize(Ctx *ctx) {
    RtThreadPriv *p = own_priv(ctx, "CoUninitialize");
    if (p->com_count == 0) {
        rt_log(RT_CH_THREAD, RT_LL_WARN, "CoUninitialize on thread %u, which has no COM initialisation to give back",
               (unsigned)RT_THREAD(ctx)->id);
        return;
    }
    if (--p->com_count == 0) p->com_model = 0;
}

HRESULT rt_ole_initialize(Ctx *ctx) {
    RtThreadPriv *p = own_priv(ctx, "OleInitialize");
    const HRESULT hr = rt_com_initialize(ctx, COINIT_APARTMENTTHREADED);
    if (hr != S_OK && hr != S_FALSE) return hr;                   /* RPC_E_CHANGED_MODE */
    if (p->ole_count == UINT32_MAX)
        rt_fatal("OleInitialize: the thread's count of OLE initialisations is at its limit");
    return p->ole_count++ == 0 ? S_OK : S_FALSE;
}
