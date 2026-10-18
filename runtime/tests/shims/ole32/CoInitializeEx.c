#include "rt/test.h"
#include "rt/thread.h"
#include "rt/win/com.h"
#include "rt/win/error.h"

/* CoInitializeEx writes no guest memory: the checks are the return values, what the next calls
 * see of the thread's count and model (every S_OK and S_FALSE is balanced by rt_com_uninitialize,
 * and a call that gets RPC_E_CHANGED_MODE must not have changed the count), and last error.  The
 * expected values are the page's (Microsoft's CoInitializeEx page, combaseapi.h); E_INVALIDARG for
 * a non-NULL pvReserved is from memory (see the shim). */
TEST(ole32, CoInitializeEx)
{
    Ctx *ctx = T_CTX();
    DWORD reserved = 0;
    unsigned held = 0;          /* S_OK and S_FALSE results not yet balanced by rt_com_uninitialize */

    T_CASE("first call with COINIT_MULTITHREADED returns S_OK");
    T_EQ(CALL(ole32, CoInitializeEx, NULL, COINIT_MULTITHREADED), S_OK);
    held++;

    T_CASE("second call with the same model returns S_FALSE");
    T_EQ(CALL(ole32, CoInitializeEx, NULL, COINIT_MULTITHREADED), S_FALSE);
    held++;

    T_CASE("the other model returns RPC_E_CHANGED_MODE and changes nothing");
    T_EQ(CALL(ole32, CoInitializeEx, NULL, COINIT_APARTMENTTHREADED), RPC_E_CHANGED_MODE);
    T_EQ(CALL(ole32, CoInitializeEx, NULL, COINIT_MULTITHREADED), S_FALSE);   /* still multithreaded; count 3 */
    held++;
    while (held) { rt_com_uninitialize(ctx); held--; }                       /* count 0 */
    T_EQ(CALL(ole32, CoInitializeEx, NULL, COINIT_APARTMENTTHREADED), S_OK);  /* the failed call changed no count */
    held++;

    T_CASE("the flags COINIT_DISABLE_OLE1DDE and COINIT_SPEED_OVER_MEMORY are accepted");
    T_EQ(CALL(ole32, CoInitializeEx, NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE | COINIT_SPEED_OVER_MEMORY), S_FALSE);
    held++;                                                                  /* count 2, apartment-threaded */
    T_EQ(CALL(ole32, CoInitializeEx, NULL, COINIT_DISABLE_OLE1DDE | COINIT_SPEED_OVER_MEMORY), RPC_E_CHANGED_MODE);
    T_EQ(CALL(ole32, CoInitializeEx, NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE), S_FALSE);
    held++;                                                                  /* count 3 */
    while (held) { rt_com_uninitialize(ctx); held--; }                       /* count 0 */

    T_CASE("a non-NULL pvReserved fails with E_INVALIDARG");
    T_EQ(CALL(ole32, CoInitializeEx, &reserved, COINIT_MULTITHREADED), E_INVALIDARG);
    T_EQ(CALL(ole32, CoInitializeEx, &reserved, COINIT_APARTMENTTHREADED), E_INVALIDARG);
    T_EQ(CALL(ole32, CoInitializeEx, NULL, COINIT_APARTMENTTHREADED), S_OK);  /* the failed calls recorded no model */
    held++;
    while (held) { rt_com_uninitialize(ctx); held--; }

    T_CASE("a preset last error is left alone");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(ole32, CoInitializeEx, NULL, COINIT_MULTITHREADED), S_OK);
    held++;
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(ole32, CoInitializeEx, NULL, COINIT_MULTITHREADED), S_FALSE);
    held++;
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(ole32, CoInitializeEx, &reserved, COINIT_MULTITHREADED), E_INVALIDARG);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_TRAPS(CALL(ole32, CoInitializeEx, NULL, 0x10), "outside the four COINIT_ values");
    T_TRAPS(CALL(ole32, CoInitializeEx, NULL, COINIT_APARTMENTTHREADED | 0x1), "outside the four COINIT_ values");

    while (held) { rt_com_uninitialize(ctx); held--; }
}
