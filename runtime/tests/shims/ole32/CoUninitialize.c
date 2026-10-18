#include "rt/test.h"
#include "rt/log.h"
#include "rt/thread.h"
#include "rt/win/com.h"
#include "rt/win/error.h"

/* CoUninitialize returns nothing and writes no guest memory: the checks are what the next calls
 * see of the thread's count and model (the initialisations are made with rt_com_initialize, the
 * core's own entry, and read back through CoInitializeEx), the warn line of a call without an
 * initialisation, and last error.  The expected values are the page's (Microsoft's CoUninitialize
 * page, combaseapi.h) and the core's comment in rt/thread.h. */
TEST(ole32, CoUninitialize)
{
    Ctx *ctx = T_CTX();

    T_CASE("after S_OK and S_FALSE, one call leaves the thread initialised in its model");
    T_EQ(rt_com_initialize(ctx, COINIT_MULTITHREADED), S_OK);
    T_EQ(rt_com_initialize(ctx, COINIT_MULTITHREADED), S_FALSE);
    CALL(ole32, CoUninitialize);                                                             /* count 1 */
    T_EQ(CALL(ole32, CoInitializeEx, NULL, COINIT_APARTMENTTHREADED), RPC_E_CHANGED_MODE);   /* still multithreaded */

    T_CASE("a second call uninitialises the thread, which may then take either model");
    CALL(ole32, CoUninitialize);                                                             /* count 0 */
    T_EQ(CALL(ole32, CoInitializeEx, NULL, COINIT_APARTMENTTHREADED), S_OK);                 /* count 1, apartment */
    CALL(ole32, CoUninitialize);                                                             /* count 0 */
    T_EQ(CALL(ole32, CoInitializeEx, NULL, COINIT_MULTITHREADED), S_OK);                     /* count 1, multithreaded */
    CALL(ole32, CoUninitialize);                                                             /* count 0 */

    T_CASE("a call on a thread without an initialisation is logged at warn level and does nothing");
    CALL(ole32, CoUninitialize);
    T_LOGGED(RT_CH_THREAD, "no COM initialisation to give back");
    T_EQ(CALL(ole32, CoInitializeEx, NULL, COINIT_APARTMENTTHREADED), S_OK);                 /* the call went no lower than 0 */
    CALL(ole32, CoUninitialize);                                                             /* count 0 */

    T_CASE("a preset last error is left alone");
    T_EQ(rt_com_initialize(ctx, COINIT_MULTITHREADED), S_OK);
    CALL(kernel32, SetLastError, 0x1234);
    CALL(ole32, CoUninitialize);                                                             /* count 0 */
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0x1234);
    CALL(ole32, CoUninitialize);                                                             /* a call without an initialisation */
    T_EQ(T_LAST_ERROR(), 0x1234);
}
