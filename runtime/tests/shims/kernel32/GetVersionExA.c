#include "rt/test.h"
#include "rt/machine.h"
#include "rt/win/sysinfo.h"
#include <string.h>

/* The guest's view of a structure before the call: every byte 0xA5, the size set by the caller. */
static void fill(OSVERSIONINFOEXA *vi, DWORD size)
{
    memset(vi, 0xA5, sizeof *vi);
    vi->dwOSVersionInfoSize = size;
}

/* szCSDVersion holds exactly the service pack text and zeros to the end of its 128 bytes. */
static void check_csd(const CHAR *csd)
{
    const char *want = "Service Pack 1";
    unsigned char exp[128];
    memset(exp, 0, sizeof exp);
    memcpy(exp, want, strlen(want));
    T_EQ(memcmp(csd, exp, sizeof exp), 0);
    T_EQ(strcmp(csd, rt_machine()->sp_name), 0);
}

TEST(kernel32, GetVersionExA)
{
    const RtMachine *m = rt_machine();
    OSVERSIONINFOEXA vi;

    T_CASE("size 148 fills OSVERSIONINFOA and returns TRUE");
    fill(&vi, sizeof(OSVERSIONINFOA));
    T_EQ(CALL(kernel32, GetVersionExA, (LPOSVERSIONINFOA)&vi), TRUE);
    T_EQ(vi.dwOSVersionInfoSize, 148);              /* the size member is the caller's: unchanged */
    T_EQ(vi.dwMajorVersion, 6);
    T_EQ(vi.dwMinorVersion, 1);
    T_EQ(vi.dwBuildNumber, 7601);
    T_EQ(vi.dwPlatformId, VER_PLATFORM_WIN32_NT);
    check_csd(vi.szCSDVersion);                     /* all 128 bytes */
    T_EQ(vi.dwMajorVersion, m->os_major);           /* and the same numbers as the machine */
    T_EQ(vi.dwMinorVersion, m->os_minor);
    T_EQ(vi.dwBuildNumber, m->os_build);

    T_CASE("size 148 writes nothing past its 148 bytes");
    fill(&vi, sizeof(OSVERSIONINFOA));
    T_EQ(CALL(kernel32, GetVersionExA, (LPOSVERSIONINFOA)&vi), TRUE);
    {
        const unsigned char *raw = (const unsigned char *)&vi;
        int bad = 0;
        for (int i = 148; i < 156; i++)             /* the 8 bytes after the 148 */
            if (raw[i] != 0xA5) bad++;
        T_EQ(bad, 0);
    }

    T_CASE("size 156 fills OSVERSIONINFOEXA and returns TRUE");
    fill(&vi, sizeof(OSVERSIONINFOEXA));
    T_EQ(CALL(kernel32, GetVersionExA, (LPOSVERSIONINFOA)&vi), TRUE);
    T_EQ(vi.dwOSVersionInfoSize, 156);              /* unchanged */
    T_EQ(vi.dwMajorVersion, 6);
    T_EQ(vi.dwMinorVersion, 1);
    T_EQ(vi.dwBuildNumber, 7601);
    T_EQ(vi.dwPlatformId, VER_PLATFORM_WIN32_NT);
    check_csd(vi.szCSDVersion);
    T_EQ(vi.wServicePackMajor, 1);
    T_EQ(vi.wServicePackMinor, 0);
    T_EQ(vi.wServicePackMajor, m->sp_major);
    T_EQ(vi.wServicePackMinor, m->sp_minor);
    T_EQ(vi.wSuiteMask, VER_SUITE_SINGLEUSERTS);
    T_EQ(vi.wProductType, VER_NT_WORKSTATION);
    T_EQ(vi.wReserved, 0);

    T_CASE("the numbers agree with GetVersion");
    DWORD v = CALL(kernel32, GetVersion);
    fill(&vi, sizeof(OSVERSIONINFOA));
    T_EQ(CALL(kernel32, GetVersionExA, (LPOSVERSIONINFOA)&vi), TRUE);
    T_EQ(vi.dwMajorVersion, v & 0xFF);              /* major in the low byte */
    T_EQ(vi.dwMinorVersion, (v >> 8) & 0xFF);       /* minor in the next */
    T_EQ(vi.dwBuildNumber, v >> 16);                /* build in the high word */

    T_CASE("success leaves last error alone");
    fill(&vi, sizeof(OSVERSIONINFOEXA));
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, GetVersionExA, (LPOSVERSIONINFOA)&vi), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* The stops: sizes the documentation does not name, and NULL.  Each proved with T_TRAPS. */
    fill(&vi, 0);
    T_TRAPS(CALL(kernel32, GetVersionExA, (LPOSVERSIONINFOA)&vi), "dwOSVersionInfoSize other than 148 or 156 (0)");
    fill(&vi, 152);
    T_TRAPS(CALL(kernel32, GetVersionExA, (LPOSVERSIONINFOA)&vi), "dwOSVersionInfoSize other than 148 or 156 (152)");
    T_TRAPS(CALL(kernel32, GetVersionExA, NULL), "NULL lpVersionInformation");
}
