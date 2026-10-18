#include "rt/test.h"

TEST(wsock32, gethostbyname)
{
    T_TRAPS(CALL(wsock32, gethostbyname, "example.invalid"), "gethostbyname(example.invalid)");
}
