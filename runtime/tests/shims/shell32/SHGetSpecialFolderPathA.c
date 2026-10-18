#include <string.h>

#include "rt/test.h"
#include "rt/win/base.h"
#include "rt/win/error.h"
#include "rt/win/shell.h"

/* The six texts, spelled here: the folders of the guest's tree (docs/RUNTIME.md 7.1). None ends
 * in a backslash (the page of SHGetFolderPath). */
static const char documents[]   = "C:\\Users\\Player\\Documents";
static const char local_app[]   = "C:\\Users\\Player\\AppData\\Local";
static const char roaming_app[] = "C:\\Users\\Player\\AppData\\Roaming";
static const char profile[]     = "C:\\Users\\Player";
static const char windows[]     = "C:\\Windows";
static const char system32[]    = "C:\\Windows\\system32";

#define FILL 0xCD               /* what the buffer holds before a call */

static void fill(char *b)
{
    memset(b, FILL, MAX_PATH);
}

/* 1 if b holds `want` and its terminator, and every byte after the terminator is still FILL. */
static int holds(const char *b, const char *want)
{
    size_t n = strlen(want), i;
    if (memcmp(b, want, n + 1) != 0)
        return 0;
    for (i = n + 1; i < MAX_PATH; i++)
        if ((unsigned char)b[i] != FILL)
            return 0;
    return 1;
}

TEST(shell32, SHGetSpecialFolderPathA)
{
    char buf[MAX_PATH];

    T_CASE("returns TRUE and the text of each of the six folders, without a trailing backslash");
    fill(buf);
    T_EQ(CALL(shell32, SHGetSpecialFolderPathA, NULL, buf, CSIDL_PERSONAL, FALSE), TRUE);
    T_TRUE(holds(buf, documents));
    fill(buf);
    T_EQ(CALL(shell32, SHGetSpecialFolderPathA, NULL, buf, CSIDL_LOCAL_APPDATA, FALSE), TRUE);
    T_TRUE(holds(buf, local_app));
    fill(buf);
    T_EQ(CALL(shell32, SHGetSpecialFolderPathA, NULL, buf, CSIDL_APPDATA, FALSE), TRUE);
    T_TRUE(holds(buf, roaming_app));
    fill(buf);
    T_EQ(CALL(shell32, SHGetSpecialFolderPathA, NULL, buf, CSIDL_PROFILE, FALSE), TRUE);
    T_TRUE(holds(buf, profile));
    fill(buf);
    T_EQ(CALL(shell32, SHGetSpecialFolderPathA, NULL, buf, CSIDL_WINDOWS, FALSE), TRUE);
    T_TRUE(holds(buf, windows));
    fill(buf);
    T_EQ(CALL(shell32, SHGetSpecialFolderPathA, NULL, buf, CSIDL_SYSTEM, FALSE), TRUE);
    T_TRUE(holds(buf, system32));

    T_CASE("CSIDL_PERSONAL with CSIDL_FLAG_CREATE gives the Documents text");
    fill(buf);
    T_EQ(CALL(shell32, SHGetSpecialFolderPathA, NULL, buf, CSIDL_PERSONAL | CSIDL_FLAG_CREATE, FALSE), TRUE);
    T_TRUE(holds(buf, documents));

    T_CASE("fCreate TRUE and FALSE give the same text");
    fill(buf);
    T_EQ(CALL(shell32, SHGetSpecialFolderPathA, NULL, buf, CSIDL_PERSONAL, TRUE), TRUE);
    T_TRUE(holds(buf, documents));
    fill(buf);
    T_EQ(CALL(shell32, SHGetSpecialFolderPathA, NULL, buf, CSIDL_PROFILE, TRUE), TRUE);
    T_TRUE(holds(buf, profile));

    T_CASE("every byte after the terminator stays");
    fill(buf);
    T_EQ(CALL(shell32, SHGetSpecialFolderPathA, NULL, buf, CSIDL_PROFILE, FALSE), TRUE);
    T_EQ(buf[strlen(profile)], 0);
    T_TRUE(holds(buf, profile));
    fill(buf);
    T_EQ(CALL(shell32, SHGetSpecialFolderPathA, NULL, buf, CSIDL_SYSTEM, FALSE), TRUE);
    T_TRUE(holds(buf, system32));

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(shell32, SHGetSpecialFolderPathA, NULL, buf, CSIDL_PERSONAL, FALSE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(shell32, SHGetSpecialFolderPathA, NULL, buf, CSIDL_PROFILE | CSIDL_FLAG_CREATE, TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_TRAPS(CALL(shell32, SHGetSpecialFolderPathA, NULL, NULL, CSIDL_PERSONAL, FALSE), "NULL pszPath");
    T_TRAPS(CALL(shell32, SHGetSpecialFolderPathA, NULL, buf, CSIDL_DESKTOP, FALSE), "another folder");
}
