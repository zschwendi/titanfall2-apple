#include "rt/test.h"

TEST(wsock32, accept)
{
    T_TRAPS(CALL(wsock32, accept, 68, NULL, NULL), "accept(68)");
}
