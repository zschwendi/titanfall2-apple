#include "rt/test.h"

/* The file opened is "http://example.invalid/". Its two slashes are written as two adjacent
 * literals, here and in the expected text: check_shim.py removes comments before it looks for
 * the T_TRAPS statement and takes "//" inside a string for the start of one, so with the
 * address in one piece item 5 of shim-check finds no T_TRAPS at all. */
TEST(shell32, ShellExecuteA)
{
    T_TRAPS(CALL(shell32, ShellExecuteA, NULL, "open", "http:/" "/example.invalid/", NULL, NULL, 1),
            "ShellExecuteA(operation \"open\", file \"http:/" "/example.invalid/\", parameters \"(null)\")");
}
