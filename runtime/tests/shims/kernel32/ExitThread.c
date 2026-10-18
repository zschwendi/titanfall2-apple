#include "rt/test.h"
#include "rt/sync.h"
#include "rt/thread.h"

/* The start routine calls ExitThread from guest code.  It must not return from that call, so
 * the flag below stays clear.  Only the thread's own state is written: the main thread reads
 * the flag after the thread object is signalled, so the test never runs ExitThread itself.
 *
 * The call is made the way lifted code makes it (home space and a return address on the guest
 * stack, the argument in rcx, then a call through the import slot to the dispatch table), not
 * with CALL: CALL always runs the shim on guest thread 1's Ctx (rt_main_ctx, harness.c), and
 * rt_thread_exit is fatal for a Ctx that is not the calling thread's own.  This is the path
 * tests/core/guest_call.c uses for calls_a_shim.  Because CALL is not used on the new thread,
 * the harness records no call of this shim (the "#rt calls" line the shim check looks for). */
static volatile int after_exit_ran;

GUEST_FN(exit_with_seven)
{
    ctx->r[4] -= 0x28;                              /* home space; rsp is 16-byte aligned again */
    ctx->r[1] = 7;                                  /* rcx: dwExitCode */
    ctx->r[4] -= 8;
    ST64(ctx->r[4], T_GUEST_ADDR(exit_with_seven) + 1);   /* a return address that is never used */
    rt_dispatch(ctx, rt_test_import_slot(RT_ID_kernel32_ExitThread));
    after_exit_ran = 1;                             /* never reached */
    GUEST_RET(ctx);
}

TEST(kernel32, ExitThread)
{
    HANDLE h, other;
    DWORD code = 0;

    T_CASE("ends the calling thread, and its thread object becomes signalled with dwExitCode as its exit code");
    after_exit_ran = 0;
    h = rt_thread_create(T_CTX(), 0, T_GUEST_ADDR(exit_with_seven), 0, 0, NULL);
    T_REQUIRE(h != NULL);
    T_EQ(rt_wait_handle(T_CTX(), h, 5000, FALSE), WAIT_OBJECT_0);
    T_EQ(rt_thread_get_exit_code(h, &code), TRUE);  /* GetExitCodeThread: the code ExitThread gave */
    T_EQ(code, 7);

    T_CASE("does not return: nothing after the call runs");
    T_EQ(after_exit_ran, 0);
    T_EQ(rt_handle_close(h), TRUE);

    /* RT-TRAPS "core: ExitThread on guest thread 1 while other guest threads run": the core
     * makes this fatal (on Windows the process would live on without its first thread).  The
     * other thread is created suspended, so it runs nothing; it is resumed afterwards and ends
     * itself.  The statement runs in a forked child, so the main thread's state is unchanged. */
    other = rt_thread_create(T_CTX(), 0, T_GUEST_ADDR(exit_with_seven), 0, CREATE_SUSPENDED, NULL);
    T_REQUIRE(other != NULL);
    T_TRAPS(CALL(kernel32, ExitThread, 7), "ExitThread on guest thread 1 while 1 other guest thread");
    (void)rt_thread_resume(other);
    T_EQ(rt_wait_handle(T_CTX(), other, 5000, FALSE), WAIT_OBJECT_0);
    T_EQ(rt_handle_close(other), TRUE);
}
