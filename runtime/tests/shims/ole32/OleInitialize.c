#include "rt/test.h"
#include "rt/thread.h"
#include "rt/win/com.h"
#include "rt/win/error.h"

/* OleInitialize writes no guest memory: the checks are the return values, what the next calls see
 * of the thread's apartment count and of the OLE count (S_OK only while the OLE count is 0), and
 * last error.  The state is set up with rt_com_initialize and rt_com_uninitialize, the core's own
 * entries.  The expected values are the page's (Microsoft's OleInitialize page, ole2.h). */
TEST(ole32, OleInitialize)
{
    Ctx *ctx = T_CTX();
    DWORD reserved = 0;

    T_CASE("a non-NULL pvReserved fails with E_INVALIDARG");
    T_EQ(CALL(ole32, OleInitialize, &reserved), E_INVALIDARG);

    T_CASE("the other model returns RPC_E_CHANGED_MODE and changes nothing");
    T_EQ(rt_com_initialize(ctx, COINIT_MULTITHREADED), S_OK);
    T_EQ(CALL(ole32, OleInitialize, NULL), RPC_E_CHANGED_MODE);
    rt_com_uninitialize(ctx);                                  /* the count is 0 again: the failed call added nothing */

    T_CASE("first call returns S_OK");
    T_EQ(CALL(ole32, OleInitialize, NULL), S_OK);              /* the OLE count was 0: the failed calls changed none */

    T_CASE("second call returns S_FALSE");
    T_EQ(CALL(ole32, OleInitialize, NULL), S_FALSE);

    T_CASE("a preset last error is left alone");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(ole32, OleInitialize, NULL), S_FALSE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(ole32, OleInitialize, &reserved), E_INVALIDARG);
    T_EQ(T_LAST_ERROR(), 0x1234);

    rt_com_uninitialize(ctx);                                  /* the three successful calls above each */
    rt_com_uninitialize(ctx);                                  /* hold one apartment initialisation */
    rt_com_uninitialize(ctx);
}
