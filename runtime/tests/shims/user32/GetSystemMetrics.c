#include "rt/test.h"
#include "rt/win/user.h"

TEST(user32, GetSystemMetrics)
{
    T_CASE("every metric the core answers, with its value");
    T_EQ(CALL(user32, GetSystemMetrics, SM_CXSCREEN), 1920);
    T_EQ(CALL(user32, GetSystemMetrics, SM_CYSCREEN), 1080);
    T_EQ(CALL(user32, GetSystemMetrics, SM_XVIRTUALSCREEN), 0);
    T_EQ(CALL(user32, GetSystemMetrics, SM_YVIRTUALSCREEN), 0);
    T_EQ(CALL(user32, GetSystemMetrics, SM_CXVIRTUALSCREEN), 1920);
    T_EQ(CALL(user32, GetSystemMetrics, SM_CYVIRTUALSCREEN), 1080);
    T_EQ(CALL(user32, GetSystemMetrics, SM_CMONITORS), 1);
    T_EQ(CALL(user32, GetSystemMetrics, SM_SAMEDISPLAYFORMAT), 1);
    T_EQ(CALL(user32, GetSystemMetrics, SM_REMOTESESSION), 0);
    T_EQ(CALL(user32, GetSystemMetrics, SM_CLEANBOOT), 0);
    T_EQ(CALL(user32, GetSystemMetrics, SM_SWAPBUTTON), 0);
    T_EQ(CALL(user32, GetSystemMetrics, SM_CXICON), 32);
    T_EQ(CALL(user32, GetSystemMetrics, SM_CYICON), 32);
    T_EQ(CALL(user32, GetSystemMetrics, SM_CXCURSOR), 32);
    T_EQ(CALL(user32, GetSystemMetrics, SM_CYCURSOR), 32);
    T_EQ(CALL(user32, GetSystemMetrics, SM_CXSMICON), 16);
    T_EQ(CALL(user32, GetSystemMetrics, SM_CYSMICON), 16);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(user32, GetSystemMetrics, SM_CXSCREEN);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* RT-TRAPS: a metric outside the core's table is never guessed.  The numbers are written
     * out because rt/win/user.h names only the metrics that are answered: 4 is SM_CYCAPTION
     * (a frame size: piece win-frame), 19 is SM_MOUSEPRESENT (the mouse: Gate 4). */
    T_TRAPS(CALL(user32, GetSystemMetrics, 4), "a metric the core does not answer: 4");
    T_TRAPS(CALL(user32, GetSystemMetrics, 19), "a metric the core does not answer: 19");
    T_TRAPS(CALL(user32, GetSystemMetrics, 10000), "a metric the core does not answer: 10000");
}
