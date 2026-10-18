#include "rt/test.h"

TEST(kernel32, CreateProcessW)
{
    T_TRAPS(CALL(kernel32, CreateProcessW, NULL, NULL, NULL, NULL, FALSE, 0, NULL, NULL, NULL, NULL), "CreateProcessW(application");
}
