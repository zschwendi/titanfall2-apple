#include "rt/test.h"
#include "rt/handle.h"
#include "rt/sync.h"
#include "rt/thread.h"
#include "rt/win/process.h"

/* The start routine records its parameter (rcx) and that it ran, and returns 7.  The main
 * thread waits for the thread's handle to be signalled before it looks at these. */
static uint64_t seen_param;
static unsigned seen_runs;

GUEST_FN(store_param)
{
    seen_param = ctx->r[1];
    seen_runs++;
    ctx->r[0] = 7;
    GUEST_RET(ctx);
}

TEST(kernel32, CreateThread)
{
    Ctx *ctx = T_CTX();
    HANDLE h;
    DWORD id;
    unsigned n;

    /* RT-TRAPS: the core stops on these (fatal).  T_TRAPS forks, so the checks run before this
     * test has made any thread, when no host thread of a created thread can be mid-exit. */
    T_TRAPS(CALL(kernel32, CreateThread, NULL, 0, T_GUEST_ADDR(store_param), NULL, 0x8, NULL),
            "creation flags 0x8 are not handled");
    T_TRAPS(CALL(kernel32, CreateThread, NULL, 0, T_GUEST_ADDR(store_param), NULL, CREATE_SUSPENDED | 0x80000000u, NULL),
            "creation flags 0x80000000 are not handled");
    T_TRAPS(CALL(kernel32, CreateThread, NULL, 0, 0, NULL, 0, NULL), "the start address is 0");

    T_CASE("a thread runs its start routine with the parameter, and the handle becomes signalled");
    seen_param = 0;
    seen_runs = 0;
    h = CALL(kernel32, CreateThread, NULL, 0, T_GUEST_ADDR(store_param),
             (LPVOID)(uintptr_t)UINT64_C(0x1122334455667788), 0, &id);
    T_REQUIRE(h != NULL);
    T_EQ(rt_wait_handle(ctx, h, 5000, FALSE), WAIT_OBJECT_0);
    T_EQ(seen_runs, 1);
    T_EQ(seen_param, UINT64_C(0x1122334455667788));
    T_EQ(rt_handle_close(h), TRUE);

    T_CASE("lpThreadId receives a non-zero id, and may be NULL");
    id = 0;
    h = CALL(kernel32, CreateThread, NULL, 0, T_GUEST_ADDR(store_param), NULL, 0, &id);
    T_REQUIRE(h != NULL);
    T_TRUE(id != 0);
    T_EQ(rt_wait_handle(ctx, h, 5000, FALSE), WAIT_OBJECT_0);
    T_EQ(rt_handle_close(h), TRUE);
    h = CALL(kernel32, CreateThread, NULL, 0, T_GUEST_ADDR(store_param), NULL, 0, NULL);
    T_REQUIRE(h != NULL);
    T_EQ(rt_wait_handle(ctx, h, 5000, FALSE), WAIT_OBJECT_0);
    T_EQ(rt_handle_close(h), TRUE);

    T_CASE("success leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    h = CALL(kernel32, CreateThread, NULL, 0, T_GUEST_ADDR(store_param), NULL, 0, NULL);
    T_REQUIRE(h != NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);                       /* the creator's; the new thread has its own, 0 */
    T_EQ(rt_wait_handle(ctx, h, 5000, FALSE), WAIT_OBJECT_0);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(rt_handle_close(h), TRUE);

    T_CASE("a CREATE_SUSPENDED thread does not run until ResumeThread, which returns 1");
    seen_runs = 0;
    h = CALL(kernel32, CreateThread, NULL, 0, T_GUEST_ADDR(store_param), NULL, CREATE_SUSPENDED, &id);
    T_REQUIRE(h != NULL);
    T_EQ(rt_wait_handle(ctx, h, 50, FALSE), WAIT_TIMEOUT);    /* it has not run, so it has not ended */
    T_EQ(seen_runs, 0);
    T_EQ(CALL(kernel32, ResumeThread, h), 1);
    T_EQ(rt_wait_handle(ctx, h, 5000, FALSE), WAIT_OBJECT_0);
    T_EQ(seen_runs, 1);
    T_EQ(rt_handle_close(h), TRUE);

    T_CASE("STACK_SIZE_PARAM_IS_A_RESERVATION is accepted and changes nothing");
    seen_runs = 0;
    h = CALL(kernel32, CreateThread, NULL, 0x180001, T_GUEST_ADDR(store_param), NULL,
             STACK_SIZE_PARAM_IS_A_RESERVATION, &id);
    T_REQUIRE(h != NULL);
    T_EQ(rt_wait_handle(ctx, h, 5000, FALSE), WAIT_OBJECT_0);
    T_EQ(seen_runs, 1);
    T_EQ(rt_handle_close(h), TRUE);

    T_CASE("a stack above 1 GiB fails with ERROR_NOT_ENOUGH_MEMORY and creates nothing");
    n = rt_thread_count();
    id = 0xDEADBEEF;
    CALL(kernel32, SetLastError, 0);
    h = CALL(kernel32, CreateThread, NULL, (UINT64_C(1) << 30) + 1, T_GUEST_ADDR(store_param), NULL, 0, &id);
    T_EQ(h, NULL);
    T_EQ(T_LAST_ERROR(), ERROR_NOT_ENOUGH_MEMORY);
    T_EQ(id, 0xDEADBEEF);                               /* *lpThreadId is not written on failure */
    T_EQ(rt_thread_count(), n);
    T_EQ(seen_runs, 1);                                 /* still the case above's one run: nothing new ran */
}
