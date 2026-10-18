#include "rt/test.h"

TEST(kernel32, DebugBreak)
{
    T_CASE("the call never returns and gives the fatal report FATAL guest called DebugBreak");
    T_TRAPS(CALL(kernel32, DebugBreak), "FATAL guest called DebugBreak");
}
