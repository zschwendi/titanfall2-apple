#include "rt/test.h"

TEST(kernel32, CreateProcessA)
{
    char cmd[] = "notepad.exe x";
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;

    T_TRAPS(CALL(kernel32, CreateProcessA, NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi), "CreateProcessA(application \"(null)\", command line \"notepad.exe x\"");
}
