#include "rt/test.h"
#include "rt/thread.h"
#include "rt/win/com.h"
#include "rt/win/error.h"

/* CoInitialize writes no guest memory: the checks are the return values, the model it records
 * (seen through CoInitializeEx with the other model, which must get RPC_E_CHANGED_MODE), what the
 * next calls see of the thread's count, and last error.  The expected values are the page's
 * (Microsoft's CoInitialize page, objbase.h); E_INVALIDARG for a non-NULL pvReserved is from
 * memory (see the shim). */
TEST(ole32, CoInitialize)
{
    Ctx *ctx = T_CTX();
    DWORD reserved = 0;
    unsigned held = 0;          /* S_OK and S_FALSE results not yet balanced by rt_com_uninitialize */

    T_CASE("first call returns S_OK");
    T_EQ(CALL(ole32, CoInitialize, NULL), S_OK);
    held++;

    T_CASE("second call returns S_FALSE");
    T_EQ(CALL(ole32, CoInitialize, NULL), S_FALSE);
    held++;

    T_CASE("the model is apartment-threaded, so COINIT_MULTITHREADED then returns RPC_E_CHANGED_MODE and changes nothing");
    T_EQ(CALL(ole32, CoInitializeEx, NULL, COINIT_MULTITHREADED), RPC_E_CHANGED_MODE);
    T_EQ(CALL(ole32, CoInitialize, NULL), S_FALSE);                           /* still apartment-threaded; count 3 */
    held++;
    while (held) { rt_com_uninitialize(ctx); held--; }                       /* count 0 */
    T_EQ(CALL(ole32, CoInitializeEx, NULL, COINIT_MULTITHREADED), S_OK);      /* the failed call changed no count */
    held++;
    while (held) { rt_com_uninitialize(ctx); held--; }

    T_CASE("a non-NULL pvReserved fails with E_INVALIDARG");
    T_EQ(CALL(ole32, CoInitialize, &reserved), E_INVALIDARG);
    T_EQ(CALL(ole32, CoInitializeEx, NULL, COINIT_MULTITHREADED), S_OK);      /* the failed call recorded no model */
    held++;
    while (held) { rt_com_uninitialize(ctx); held--; }

    T_CASE("a preset last error is left alone");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(ole32, CoInitialize, NULL), S_OK);
    held++;
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(ole32, CoInitialize, NULL), S_FALSE);
    held++;
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(ole32, CoInitialize, &reserved), E_INVALIDARG);
    T_EQ(T_LAST_ERROR(), 0x1234);

    while (held) { rt_com_uninitialize(ctx); held--; }
}
