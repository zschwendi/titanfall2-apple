#include "rt/test.h"
#include "rt/win/process.h"

/* SetErrorMode writes no guest memory and sets no last error, so the checks are the return value
 * (the mode the previous call set) and last error, which a preset value must keep.  The two
 * values the core stops on are proved with T_TRAPS after the cases, as the core's header says. */
TEST(kernel32, SetErrorMode)
{
    T_CASE("returns 0 at first");
    T_EQ(CALL(kernel32, SetErrorMode, SEM_FAILCRITICALERRORS), 0);

    T_CASE("returns the mode the previous call set");
    T_EQ(CALL(kernel32, SetErrorMode, SEM_NOGPFAULTERRORBOX), SEM_FAILCRITICALERRORS);
    T_EQ(CALL(kernel32, SetErrorMode, SEM_NOOPENFILEERRORBOX), SEM_NOGPFAULTERRORBOX);
    T_EQ(CALL(kernel32, SetErrorMode, SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX), SEM_NOOPENFILEERRORBOX);
    T_EQ(CALL(kernel32, SetErrorMode, 0), SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    T_EQ(CALL(kernel32, SetErrorMode, 0), 0);

    T_CASE("a preset last error stays");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, SetErrorMode, SEM_NOOPENFILEERRORBOX), 0);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(kernel32, SetErrorMode, 0), SEM_NOOPENFILEERRORBOX);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* The core stops (fatal) on these two values; the shim adds no guard.  Each runs in a forked
     * child, so the mode stays 0 here, which the last check shows. */
    T_TRAPS(CALL(kernel32, SetErrorMode, SEM_NOALIGNMENTFAULTEXCEPT),
            "SetErrorMode: mode 0x4 has SEM_NOALIGNMENTFAULTEXCEPT");
    T_TRAPS(CALL(kernel32, SetErrorMode, 0x0010),
            "SetErrorMode: mode 0x10 has a bit outside the four SEM_ values");
    T_EQ(CALL(kernel32, SetErrorMode, 0), 0);
}
