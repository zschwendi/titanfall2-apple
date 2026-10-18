#include "rt/test.h"
#include "rt/handle.h"
#include "rt/sync.h"
#include "rt/win/process.h"

/* Two start routines.  finish_now records that it ran and returns at once, so its thread ends
 * as soon as it starts.  wait_for_gate waits for the event passed as its parameter (rcx), then
 * returns: its thread has not ended until the test sets the event.  The main thread waits for a
 * handle with rt_wait_handle and only then looks at what the thread did. */
static unsigned finish_runs;

GUEST_FN(finish_now)
{
    finish_runs++;
    ctx->r[0] = 0;
    GUEST_RET(ctx);
}

GUEST_FN(wait_for_gate)
{
    HANDLE gate = (HANDLE)(uintptr_t)ctx->r[1];
    (void)rt_wait_handle(ctx, gate, INFINITE, FALSE);
    ctx->r[0] = 9;
    GUEST_RET(ctx);
}

TEST(kernel32, ResumeThread)
{
    Ctx *ctx = T_CTX();
    HANDLE gate, h;
    DWORD id = 0;

    T_CASE("resume of a suspended thread returns 1 and starts it");
    finish_runs = 0;
    h = CALL(kernel32, CreateThread, NULL, 0, T_GUEST_ADDR(finish_now), NULL, CREATE_SUSPENDED, &id);
    T_REQUIRE(h != NULL);
    T_EQ(rt_wait_handle(ctx, h, 50, FALSE), WAIT_TIMEOUT);  /* suspended: it has not run, so has not ended */
    T_EQ(finish_runs, 0);
    T_EQ(CALL(kernel32, ResumeThread, h), 1);
    T_EQ(rt_wait_handle(ctx, h, 5000, FALSE), WAIT_OBJECT_0);
    T_EQ(finish_runs, 1);
    T_EQ(rt_handle_close(h), TRUE);

    T_CASE("resume of a running thread returns 0 and does nothing");
    gate = rt_event_create(ctx, TRUE, FALSE, NULL);     /* manual reset, not set */
    T_REQUIRE(gate != NULL);
    h = CALL(kernel32, CreateThread, NULL, 0, T_GUEST_ADDR(wait_for_gate), (LPVOID)gate, CREATE_SUSPENDED, &id);
    T_REQUIRE(h != NULL);
    T_EQ(rt_wait_handle(ctx, h, 50, FALSE), WAIT_TIMEOUT);  /* suspended, so it is not waiting on the gate yet */
    T_EQ(CALL(kernel32, ResumeThread, h), 1);               /* starts it: it now waits on the gate */
    T_EQ(CALL(kernel32, ResumeThread, h), 0);               /* running, not suspended: nothing happens */
    T_EQ(CALL(kernel32, ResumeThread, h), 0);               /* and again */
    T_EQ(rt_wait_handle(ctx, h, 50, FALSE), WAIT_TIMEOUT);  /* still waiting on the gate: it has not ended */
    T_EQ(rt_event_set(ctx, gate), TRUE);
    T_EQ(rt_wait_handle(ctx, h, 5000, FALSE), WAIT_OBJECT_0);
    T_EQ(rt_handle_close(h), TRUE);

    /* Windows: ResumeThread on a thread that has ended gives 0 and does nothing (Wine
     * kernel32/tests/thread.c, test_CreateThread_suspended: after the wait on the ended thread,
     * ResumeThread gives 0).  Last error is not asserted there; the runtime leaves it alone. */
    T_CASE("resume of a thread that has ended returns 0 and does nothing");
    finish_runs = 0;
    h = CALL(kernel32, CreateThread, NULL, 0, T_GUEST_ADDR(finish_now), NULL, 0, &id);
    T_REQUIRE(h != NULL);
    T_EQ(rt_wait_handle(ctx, h, 5000, FALSE), WAIT_OBJECT_0);
    T_EQ(finish_runs, 1);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, ResumeThread, h), 0);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(finish_runs, 1);                                   /* it does not run again */
    T_EQ(rt_wait_handle(ctx, h, 0, FALSE), WAIT_OBJECT_0);  /* still ended */
    T_EQ(rt_handle_close(h), TRUE);

    T_CASE("the pseudo-handle names the calling thread and gives 0");
    T_EQ(CALL(kernel32, ResumeThread, RT_PSEUDO_THREAD), 0);

    T_CASE("NULL, an event and a closed thread handle fail with (DWORD)-1 and ERROR_INVALID_HANDLE");
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, ResumeThread, NULL), (DWORD)-1);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(CALL(kernel32, ResumeThread, gate), (DWORD)-1);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    h = CALL(kernel32, CreateThread, NULL, 0, T_GUEST_ADDR(finish_now), NULL, CREATE_SUSPENDED, &id);
    T_REQUIRE(h != NULL);
    T_EQ(CALL(kernel32, ResumeThread, h), 1);
    T_EQ(rt_wait_handle(ctx, h, 5000, FALSE), WAIT_OBJECT_0);
    T_EQ(rt_handle_close(h), TRUE);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, ResumeThread, h), (DWORD)-1);       /* closed: not an open handle */
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_CASE("success leaves last error alone");
    h = CALL(kernel32, CreateThread, NULL, 0, T_GUEST_ADDR(finish_now), NULL, CREATE_SUSPENDED, &id);
    T_REQUIRE(h != NULL);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, ResumeThread, h), 1);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(kernel32, ResumeThread, RT_PSEUDO_THREAD), 0);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(rt_wait_handle(ctx, h, 5000, FALSE), WAIT_OBJECT_0);
    T_EQ(rt_handle_close(h), TRUE);
    T_EQ(rt_handle_close(gate), TRUE);
}
