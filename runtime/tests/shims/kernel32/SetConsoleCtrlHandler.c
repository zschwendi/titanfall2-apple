#include "rt/test.h"

/* A handler is a guest code address that nothing calls: any nonzero number serves. */
#define HANDLER ((PHANDLER_ROUTINE)0x140001000)

TEST(kernel32, SetConsoleCtrlHandler)
{
    T_CASE("sets the Ctrl+C ignore flag with no routine");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, SetConsoleCtrlHandler, (PHANDLER_ROUTINE)0, TRUE), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("clears the Ctrl+C ignore flag with no routine");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, SetConsoleCtrlHandler, (PHANDLER_ROUTINE)0, FALSE), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("appends a routine to the handler list");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, SetConsoleCtrlHandler, HANDLER, TRUE), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("removes the entry added last with a routine");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, SetConsoleCtrlHandler, HANDLER, FALSE), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("the same routine added twice and removed twice");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, SetConsoleCtrlHandler, HANDLER, TRUE), TRUE);
    T_EQ(CALL(kernel32, SetConsoleCtrlHandler, HANDLER, TRUE), TRUE);
    T_EQ(CALL(kernel32, SetConsoleCtrlHandler, HANDLER, FALSE), TRUE);
    T_EQ(CALL(kernel32, SetConsoleCtrlHandler, HANDLER, FALSE), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("holds up to 64 handlers");
    for (unsigned i = 0; i < 64; i++) {
        CALL(kernel32, SetLastError, 0x1234);
        T_EQ(CALL(kernel32, SetConsoleCtrlHandler, HANDLER, TRUE), TRUE);
        T_EQ(T_LAST_ERROR(), 0x1234);
    }
    T_TRAPS(CALL(kernel32, SetConsoleCtrlHandler, HANDLER, TRUE), "more than 64 handlers");
    for (unsigned i = 0; i < 64; i++) {             /* remove what the test added */
        T_EQ(CALL(kernel32, SetConsoleCtrlHandler, HANDLER, FALSE), TRUE);
    }
    T_TRAPS(CALL(kernel32, SetConsoleCtrlHandler, HANDLER, FALSE), "which is not in the list");
}
