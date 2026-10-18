/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-present
 * RT-CASES:  a sync interval of 0 is paced as an interval of 1 and the ledger counts each call as a frame and as an interval 0 paced as 1; a sync interval of 1 returns at boundaries of 1/60 s, so six frames take about a tenth of a second; RT_D3D_PRESENT=immediate returns at once whatever the interval, and a sync interval of 0 is then not counted as paced
 * RT-TRAPS:  Present with a sync interval of; Present with flags
 */
#include "rt/nulldev.h"

/* RT-DOC, as docs/reviews/null-device-redteam.md (R1) read it on 11 October in Microsoft's
 * source of the page: with a sync interval of 1 to 4 a bit-block-transfer swap chain
 * "synchronize[s] presentation after the nth vertical blank"; with 0 presentation is
 * immediate.  S_OK for a successful present is from memory.
 *
 * Nothing is shown.  What the game can observe is the result and how long the call took, and
 * a game may use that wait as its frame pacing (R1): so the call returns at the n-th next
 * boundary of the one mode the device reports, 60 Hz (IDXGIOutput::GetDisplayModeList), on the
 * host's monotonic clock; with 0, on Windows, it returns at once (here: see below).
 * RT_D3D_PRESENT=immediate turns the wait off (tests, and runs that want speed).  Each call is counted in the ledger as an answer the
 * device invents (a frame nobody saw), and the ledger prints the frames and their rate with
 * the call table.
 *
 * A SYNC INTERVAL OF 0 IS PACED AS 1 (the planner's decision B in unit gate3-run-6's brief, 11
 * October; not what Windows does, where 0 presents at once).  A windowed game passes 0: the
 * run's own `-windowed` makes materialsystem_dx11+0x170d8 call Present(0, 0), where the
 * recorded full-screen start passes 1 (docs/handoff/gate3-run-5.md, section 4.1).  Nothing
 * else paces such a run: a frame took 0.23 ms, the log grew by 30 MB a second and 70 frames
 * were presented per picture of the start-up video against the recording's one.  A run that
 * follows a 60 Hz recording should present at 60 Hz, so with the pacing on a sync interval of
 * 0 waits exactly as 1 does.  The answer is invented and is counted as such, in a kind of its
 * own (ND_INV_PRESENT_0, "present: interval 0 paced as 1"), beside the frame itself.
 * RT_D3D_PRESENT=immediate keeps Windows' behaviour: no wait, and nothing counted in that kind.
 *
 * The wait cannot hang a run (R21): its target is at most 4/60 s away, and if it has not been
 * reached after a quarter of a second (a host that was suspended, a clock that jumped) the
 * call says so in the log and returns (rt_nulldev_pace, runtime/nulldev/ledger.c).
 * Stops: a sync interval above 4 (outside the documented range); any flag: the game's one
 * call site passes 0 (finding 31, section 5) and so does every recorded call. */
COM_METHOD(IDXGISwapChain, HRESULT, Present, UINT sync_interval, UINT flags)
{
    NdSwapChain *s = rt_com_state(ctx, This, &rt_nulldev_swapchain_class);
    if (sync_interval > 4) RT_UNIMPLEMENTED(ctx, "Present with a sync interval of %u (the documented values are 0 to 4)", (unsigned)sync_interval);
    if (flags) RT_UNIMPLEMENTED(ctx, "Present with flags 0x%x", (unsigned)flags);
    /* The seam (rt/d3dback.h, rule 6): with a back end the present, its wait and its result are
     * the back end's (S_OK, or a status such as DXGI_STATUS_OCCLUDED); the front neither paces
     * nor counts an invented answer.  A failure where the front accepted the call is a stop. */
    if (rt_d3dback()) {
        HRESULT hr = RT_BACK_ANSWER(ctx, Present, s->back, sync_interval, flags);
        if (hr < 0) rt_d3dback_differs(ctx, RT_BACK_Present, S_OK, hr);      /* a failure: the high bit of an HRESULT */
        return hr;
    }
    if (rt_nulldev_present_paced()) {
        rt_nulldev_pace(sync_interval ? sync_interval : 1);
        if (!sync_interval) rt_nulldev_invented(ctx, ND_INV_PRESENT_0, 1);
    }
    rt_nulldev_invented(ctx, ND_INV_PRESENT, sync_interval);
    return S_OK;
}
