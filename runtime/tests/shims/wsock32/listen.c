#include "rt/test.h"

TEST(wsock32, listen)
{
    T_TRAPS(CALL(wsock32, listen, 68, 5), "listen(68, 5)");
}
