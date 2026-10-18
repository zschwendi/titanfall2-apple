#include <string.h>
#include "rt/test.h"
#include "rt/thread.h"
#include "rt/win/exception.h"

/* The three codes that return, and the fatal ones, through the guest ABI.  Last error is
 * set to 0x1234 first, so that a touch of it by any of the three codes shows. */
TEST(kernel32, RaiseException)
{
    ULONG_PTR a[4];
    DWORD self, n;

    T_CASE("names the calling thread with the thread-name exception");
    self = CALL(kernel32, GetCurrentThreadId);
    a[0] = RT_EXC_THREAD_NAME_TYPE;
    a[1] = (ULONG_PTR)(uintptr_t)"first";
    a[2] = 0xFFFFFFFFu;
    CALL(kernel32, RaiseException, RT_EXC_THREAD_NAME, 0, 3, a);
    T_TRUE(strcmp(RT_THREAD(T_CTX())->name, "first") == 0);
    a[1] = (ULONG_PTR)(uintptr_t)"second";
    a[2] = self;
    CALL(kernel32, RaiseException, RT_EXC_THREAD_NAME, 0, 3, a);
    T_TRUE(strcmp(RT_THREAD(T_CTX())->name, "second") == 0);

    T_CASE("names another thread: logged and ignored");
    n = T_LOG_COUNT(RT_CH_THREAD);
    a[1] = (ULONG_PTR)(uintptr_t)"other";
    a[2] = self + 4;
    CALL(kernel32, RaiseException, RT_EXC_THREAD_NAME, 0, 3, a);
    T_EQ(T_LOG_COUNT(RT_CH_THREAD), n + 1);
    T_TRUE(T_LOGGED(RT_CH_THREAD, "thread name \"other\" for thread 0x"));
    T_TRUE(T_LOGGED(RT_CH_THREAD, "ignored: naming another thread is not built"));
    T_TRUE(strcmp(RT_THREAD(T_CTX())->name, "second") == 0);    /* the calling thread keeps its name */

    T_CASE("a malformed thread-name exception is logged and ignored");
    n = T_LOG_COUNT(RT_CH_THREAD);
    CALL(kernel32, RaiseException, RT_EXC_THREAD_NAME, 0, 0, NULL);          /* no arguments */
    T_EQ(T_LOG_COUNT(RT_CH_THREAD), n + 1);
    T_TRUE(T_LOGGED(RT_CH_THREAD, "malformed thread-name exception ignored"));
    a[0] = 0x2000;                                                          /* not 0x1000 */
    a[1] = (ULONG_PTR)(uintptr_t)"third";
    a[2] = self;
    CALL(kernel32, RaiseException, RT_EXC_THREAD_NAME, 0, 3, a);
    T_EQ(T_LOG_COUNT(RT_CH_THREAD), n + 2);
    a[0] = RT_EXC_THREAD_NAME_TYPE;
    a[1] = 0;                                                               /* no name */
    CALL(kernel32, RaiseException, RT_EXC_THREAD_NAME, 0, 3, a);
    T_EQ(T_LOG_COUNT(RT_CH_THREAD), n + 3);
    T_TRUE(strcmp(RT_THREAD(T_CTX())->name, "second") == 0);

    T_CASE("DBG_PRINTEXCEPTION_C prints its text on guest.debug");
    n = T_LOG_COUNT(RT_CH_GUEST_DEBUG);
    a[0] = 7;                                                               /* six characters and the NUL */
    a[1] = (ULONG_PTR)(uintptr_t)"hello\n";
    CALL(kernel32, RaiseException, DBG_PRINTEXCEPTION_C, 0, 2, a);
    T_EQ(T_LOG_COUNT(RT_CH_GUEST_DEBUG), n + 1);
    T_TRUE(T_LOGGED(RT_CH_GUEST_DEBUG, "hello"));

    T_CASE("DBG_PRINTEXCEPTION_WIDE_C prints its ANSI text on guest.debug");
    n = T_LOG_COUNT(RT_CH_GUEST_DEBUG);
    a[0] = 0;                                                               /* the wide copy is not read */
    a[1] = 0;
    a[2] = 7;                                                               /* length of the ANSI copy */
    a[3] = (ULONG_PTR)(uintptr_t)"wide\n";
    CALL(kernel32, RaiseException, DBG_PRINTEXCEPTION_WIDE_C, 0, 4, a);
    T_EQ(T_LOG_COUNT(RT_CH_GUEST_DEBUG), n + 1);
    T_TRUE(T_LOGGED(RT_CH_GUEST_DEBUG, "wide"));

    T_CASE("the printed text is limited to the length given");
    n = T_LOG_COUNT(RT_CH_GUEST_DEBUG);
    a[0] = 4;
    a[1] = (ULONG_PTR)(uintptr_t)"abcdefgh";
    CALL(kernel32, RaiseException, DBG_PRINTEXCEPTION_C, 0, 2, a);
    T_EQ(T_LOG_COUNT(RT_CH_GUEST_DEBUG), n + 1);
    T_TRUE(T_LOGGED(RT_CH_GUEST_DEBUG, "abcd"));
    T_TRUE(!T_LOGGED(RT_CH_GUEST_DEBUG, "abcde"));

    T_CASE("a print code of another shape logs nothing");
    n = T_LOG_COUNT(RT_CH_GUEST_DEBUG);
    CALL(kernel32, RaiseException, DBG_PRINTEXCEPTION_C, 0, 0, NULL);       /* no arguments */
    a[0] = 7;
    a[1] = 0;                                                               /* no text */
    CALL(kernel32, RaiseException, DBG_PRINTEXCEPTION_C, 0, 2, a);
    a[1] = (ULONG_PTR)(uintptr_t)"unseen";
    CALL(kernel32, RaiseException, DBG_PRINTEXCEPTION_C, 0, 1, a);          /* the text is not counted */
    a[2] = 7;
    a[3] = (ULONG_PTR)(uintptr_t)"unseen";
    CALL(kernel32, RaiseException, DBG_PRINTEXCEPTION_WIDE_C, 0, 3, a);     /* the ANSI copy is not counted */
    a[3] = 0;
    CALL(kernel32, RaiseException, DBG_PRINTEXCEPTION_WIDE_C, 0, 4, a);     /* no ANSI text */
    T_EQ(T_LOG_COUNT(RT_CH_GUEST_DEBUG), n);
    T_TRUE(!T_LOGGED(RT_CH_GUEST_DEBUG, "unseen"));

    T_CASE("every other code ends the run with a fatal report");
    T_TRAPS(CALL(kernel32, RaiseException, RT_EXC_CXX, 1, 0, NULL), "FATAL guest raised exception 0x");
    a[0] = 0;
    a[1] = 0x1234;
    T_TRAPS(CALL(kernel32, RaiseException, EXCEPTION_ACCESS_VIOLATION, 0, 2, a), "FATAL guest raised exception 0x");

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, RaiseException, RT_EXC_THREAD_NAME, 0, 0, NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);
    a[0] = 7;
    a[1] = (ULONG_PTR)(uintptr_t)"quiet\n";
    CALL(kernel32, RaiseException, DBG_PRINTEXCEPTION_C, 0, 2, a);
    T_EQ(T_LAST_ERROR(), 0x1234);
    a[0] = RT_EXC_THREAD_NAME_TYPE;
    a[1] = (ULONG_PTR)(uintptr_t)"fourth";
    a[2] = self;
    CALL(kernel32, RaiseException, RT_EXC_THREAD_NAME, 0, 3, a);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
