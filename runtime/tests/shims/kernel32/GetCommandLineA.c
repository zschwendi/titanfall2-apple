#include <string.h>

#include "rt/test.h"
#include "rt/env.h"

/* The oracle is the core's command line: the shim must hand back that same string, at the same
 * address, every time.  GetCommandLineA writes no memory and sets no last error. */
TEST(kernel32, GetCommandLineA)
{
    CHAR *p, *q;

    T_CASE("returns a pointer to the command line");
    p = CALL(kernel32, GetCommandLineA);
    T_REQUIRE(p != NULL);

    T_CASE("the same pointer on every call");
    q = CALL(kernel32, GetCommandLineA);
    T_EQ(q, p);
    T_EQ(CALL(kernel32, GetCommandLineA), CALL(kernel32, GetCommandLineA));

    T_CASE("the line is not empty and is what rt_cmdline_a() gives");
    T_TRUE(p[0] != '\0');
    T_EQ(p, rt_cmdline_a());
    T_TRUE(strcmp(p, rt_cmdline_a()) == 0);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, GetCommandLineA);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
