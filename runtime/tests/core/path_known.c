/* The known directories (rt_path_known_w, rt_path_known_a; the specification is rt/path.h).
 *
 * Where the expected values come from: the texts are the table of docs/RUNTIME.md section 7.1
 * (C:\Titanfall2 apart, the directories the guest's tree has) and the start values of TMP,
 * USERPROFILE and SystemRoot in section 8.2; the size rule is the one Microsoft's pages of
 * GetSystemDirectory and GetTempPath state; "a buffer that is too small stays untouched" is
 * Wine's kernel32/tests/path.c test_GetTempPathA.
 */
#include <string.h>
#include <sys/stat.h>

#include "rt/test.h"
#include "rt/path.h"
#include "rt/env.h"

static const struct { RtKnownPath which; const char *text; } known[] = {
    { RT_KNOWN_WINDOWS,       "C:\\Windows" },
    { RT_KNOWN_SYSTEM,        "C:\\Windows\\system32" },
    { RT_KNOWN_TEMP,          "C:\\Users\\Player\\AppData\\Local\\Temp\\" },
    { RT_KNOWN_PROFILE,       "C:\\Users\\Player" },
    { RT_KNOWN_DOCUMENTS,     "C:\\Users\\Player\\Documents" },
    { RT_KNOWN_LOCAL_APPDATA, "C:\\Users\\Player\\AppData\\Local" },
    { RT_KNOWN_APPDATA,       "C:\\Users\\Player\\AppData\\Roaming" },
};
#define NKNOWN (sizeof known / sizeof known[0])

TEST(core, path_known)
{
    Ctx *ctx = T_CTX();
    rt_set_last_error(ctx, 0x5EA1);

    T_CASE("each text, in both forms, with the length returned without the terminator");
    for (unsigned i = 0; i < NKNOWN; i++) {
        const DWORD n = (DWORD)strlen(known[i].text);
        char a[300];
        WCHAR w[300];
        memset(a, '#', sizeof a);
        for (unsigned k = 0; k < 300; k++) w[k] = 0x2323;
        T_EQ(rt_path_known_a(known[i].which, a, 300), n);
        T_TRUE(strcmp(a, known[i].text) == 0);
        T_EQ(a[n + 1], '#');                                    /* nothing after the terminator */
        T_EQ(rt_path_known_w(known[i].which, w, 300), n);
        for (unsigned k = 0; k <= n; k++) T_EQ(w[k], (unsigned char)known[i].text[k]);
        T_EQ(w[n + 1], 0x2323);
    }

    T_CASE("only the temp directory ends in a backslash");
    for (unsigned i = 0; i < NKNOWN; i++) {
        char a[300];
        const DWORD n = rt_path_known_a(known[i].which, a, 300);
        T_EQ(a[n - 1] == '\\', known[i].which == RT_KNOWN_TEMP);
    }

    T_CASE("a buffer that is too small is left untouched and the size with the terminator is returned");
    for (unsigned i = 0; i < NKNOWN; i++) {
        const DWORD n = (DWORD)strlen(known[i].text);
        char a[300];
        WCHAR w[300];
        memset(a, '#', sizeof a);
        for (unsigned k = 0; k < 300; k++) w[k] = 0x2323;
        T_EQ(rt_path_known_a(known[i].which, a, n), n + 1);     /* one byte short */
        T_EQ(rt_path_known_a(known[i].which, a, 1), n + 1);
        T_EQ(rt_path_known_a(known[i].which, a, 0), n + 1);
        T_EQ(rt_path_known_a(known[i].which, NULL, 0), n + 1);  /* the size query */
        T_EQ(rt_path_known_a(known[i].which, NULL, n), n + 1);
        T_EQ(rt_path_known_w(known[i].which, w, n), n + 1);
        T_EQ(rt_path_known_w(known[i].which, w, 0), n + 1);
        T_EQ(rt_path_known_w(known[i].which, NULL, 0), n + 1);
        for (unsigned k = 0; k < 300; k++) { T_EQ(a[k], '#'); T_EQ(w[k], 0x2323); }
        T_EQ(rt_path_known_a(known[i].which, a, n + 1), n);     /* exactly enough */
        T_TRUE(strcmp(a, known[i].text) == 0);
        T_EQ(a[n + 1], '#');
        T_EQ(rt_path_known_w(known[i].which, w, n + 1), n);
        T_EQ(w[n], 0);
        T_EQ(w[n + 1], 0x2323);
    }

    T_CASE("each one is an existing directory of the guest's tree");
    for (unsigned i = 0; i < NKNOWN; i++) {
        char a[300], host[RT_PATH_MAX];
        struct stat st;
        rt_path_known_a(known[i].which, a, 300);
        T_EQ(rt_path_to_host_a(a, RT_PATH_EXISTING | RT_PATH_DIR, host), 0);
        T_TRUE(stat(host, &st) == 0 && S_ISDIR(st.st_mode));
    }

    T_CASE("the texts agree with the variables the environment starts with");
    {
        static const struct { const char *var; RtKnownPath which; bool slash; } env[] = {
            { "SystemRoot", RT_KNOWN_WINDOWS, false }, { "windir", RT_KNOWN_WINDOWS, false },
            { "USERPROFILE", RT_KNOWN_PROFILE, false }, { "TMP", RT_KNOWN_TEMP, true },
            { "TEMP", RT_KNOWN_TEMP, true },
        };
        for (unsigned i = 0; i < sizeof env / sizeof env[0]; i++) {
            char v[300], a[300];
            DWORD err = 1;
            DWORD n = rt_env_get_a(env[i].var, v, 300, &err);
            T_EQ(err, 0);
            if (env[i].slash) { v[n] = '\\'; v[n + 1] = 0; }    /* GetTempPath adds the backslash */
            rt_path_known_a(env[i].which, a, 300);
            T_TRUE(strcmp(a, v) == 0);
        }
    }

    T_CASE("last error is never touched");
    T_EQ(T_LAST_ERROR(), 0x5EA1);

    T_CASE("a value that is no known directory, and no buffer where one is needed, are fatal");
    {
        char a[300];
        WCHAR w[300];
        T_TRAPS(rt_path_known_a((RtKnownPath)0, a, 300), "is not a known directory");
        T_TRAPS(rt_path_known_w((RtKnownPath)8, w, 300), "is not a known directory");
        T_TRAPS(rt_path_known_a(RT_KNOWN_SYSTEM, NULL, 300), "rt_path_known_a: no buffer");
        T_TRAPS(rt_path_known_w(RT_KNOWN_SYSTEM, NULL, 300), "rt_path_known_w: no buffer");
    }
}
