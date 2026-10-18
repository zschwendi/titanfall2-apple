/* The C runtime's start-up, replayed against the core (review finding R26).
 *
 * Every game DLL links the Visual Studio 2012 C runtime statically, and its entry point runs
 * _heap_init, _mtinit, _ioinit, __crtGetEnvironmentStringsA, _setargv (whose first step is
 * __initmbctable -> _setmbcp -> setSBUpLow) and _setenvp before any game code.  Each test
 * below makes one routine's calls into Windows, in the routine's order and with the arguments
 * its source passes, through the core functions the shims call.  The sequences are from
 * memory of crt/src (ioinit.c, a_env.c, mbctype.c, a_str.c, a_map.c, stdargv.c, stdenvp.c)
 * and are not verified against the source.
 *
 * The unit tests call one function at a time with arguments their author chose; the point
 * here is that the sequence runs to its end.  Calls whose units are not built are named where
 * they belong and not made: FlsAlloc and TlsAlloc of _mtinit (thread), GetStdHandle and
 * GetFileType of _ioinit (file), GetModuleFileNameA of _setargv (loader).  Add them as those
 * units arrive.
 */
#include <string.h>

#include "rt/env.h"
#include "rt/heap.h"
#include "rt/locale.h"
#include "rt/lock.h"
#include "rt/test.h"
#include "rt/win/error.h"

#define SENTINEL 0x7A5A5A01u
static void set_err(void) { rt_set_last_error(T_CTX(), SENTINEL); }

/* _malloc_crt and _free_crt: HeapAlloc and HeapFree on _crtheap, which _heap_init set to
 * GetProcessHeap(). */
static void *malloc_crt(size_t n) { return rt_heap_alloc(rt_heap_process(), 0, n); }
static void free_crt(void *p) { T_TRUE(rt_heap_free(rt_heap_process(), 0, p)); }

/* __crtGetEnvironmentStringsA: the W block converted with CP_ACP and flags 0, terminators
 * included.  The result is from malloc_crt; NULL if a call failed. */
static char *crt_get_environment_strings_a(int *size)
{
    DWORD err = 0x55;
    WCHAR *wenv = rt_env_block_w(&err);
    if (!wenv) return NULL;
    const WCHAR *end = wenv;
    while (*end) { if (*++end == 0) end++; }
    int nw = (int)(end - wenv + 1);
    int na = rt_wide_to_mb(CP_ACP, 0, wenv, nw, NULL, 0, NULL, NULL);
    char *aenv = na ? malloc_crt((size_t)na) : NULL;
    if (aenv && !rt_wide_to_mb(CP_ACP, 0, wenv, nw, aenv, na, NULL, NULL)) {
        free_crt(aenv);
        aenv = NULL;
    }
    T_TRUE(rt_env_block_free(wenv));
    *size = na;
    return aenv;
}

/* _setenvp's first pass: the number of entries it copies into _environ.  It leaves out the
 * entries that begin with '=', the drive directories. */
static int setenvp_count(const char *aenv)
{
    int n = 0;
    for (const char *p = aenv; *p; p += strlen(p) + 1)
        if (*p != '=') n++;
    return n;
}

TEST(core, crt_startup_heap_init)
{
    T_CASE("_heap_init: the process heap exists, and _malloc_crt and _free_crt work on it");
    HANDLE heap = rt_heap_process();
    T_REQUIRE(heap != NULL);
    char *p = malloc_crt(4096);
    T_REQUIRE(p != NULL);
    memset(p, 0x5A, 4096);
    T_TRUE(rt_heap_size(heap, 0, p) >= 4096);
    free_crt(p);
}

TEST(core, crt_startup_ioinit)
{
    T_CASE("_ioinit: no inherited file handles in the start-up information; the three standard streams' locks");
    STARTUPINFOW si;
    memset(&si, 0xA5, sizeof si);
    rt_env_startup_info(&si);
    /* The inherited-handle branch is taken when both are set; here neither is. */
    T_EQ(si.cbReserved2, 0);
    T_TRUE(si.lpReserved2 == NULL);

    /* (GetStdHandle and GetFileType for each of the three: the file unit.)
     * InitializeCriticalSectionAndSpinCount(&pio->lock, _CRT_SPINCOUNT) for each. */
    CRITICAL_SECTION lock[3];
    for (int fh = 0; fh < 3; fh++) rt_cs_init(&lock[fh], 4000);
    for (int fh = 0; fh < 3; fh++) {
        rt_cs_enter(T_CTX(), &lock[fh]);
        rt_cs_leave(T_CTX(), &lock[fh]);
    }
    for (int fh = 0; fh < 3; fh++) rt_cs_delete(&lock[fh]);
}

TEST(core, crt_startup_environment)
{
    T_CASE("__crtGetEnvironmentStringsA and _setenvp: the block converts with CP_ACP and flags 0, before and after a _chdir");
    int size = 0;
    char *aenv = crt_get_environment_strings_a(&size);
    T_REQUIRE(aenv != NULL);
    T_TRUE(size >= 2 && aenv[size - 1] == 0 && aenv[size - 2] == 0);
    int count = setenvp_count(aenv);
    T_TRUE(count >= 1);
    for (const char *p = aenv; *p; p += strlen(p) + 1) T_TRUE(strchr(p + 1, '=') != NULL);
    free_crt(aenv);

    /* _chdir, after SetCurrentDirectory succeeds: SetEnvironmentVariableA("=C:", the new
     * directory).  A DLL loaded afterwards starts with that entry in the block. */
    DWORD err = 0x55;
    T_EQ(rt_env_set_a("=C:", "C:\\Titanfall2\\bin\\x64_retail", &err), TRUE);
    T_EQ(err, 0);
    aenv = crt_get_environment_strings_a(&size);
    T_REQUIRE(aenv != NULL);
    bool has_drive = false;
    for (const char *p = aenv; *p; p += strlen(p) + 1)
        if (!strcmp(p, "=C:=C:\\Titanfall2\\bin\\x64_retail")) has_drive = true;
    T_TRUE(has_drive);
    T_EQ(setenvp_count(aenv), count);
    free_crt(aenv);
    T_EQ(rt_env_set_a("=C:", NULL, &err), TRUE);
}

/* __crtLCMapStringA(NULL, NULL, flags, src, 256, dst, 256, cp, FALSE): to wide with
 * MB_PRECOMPOSED, LCMapString with locale 0 (the locale name is NULL and LCMapStringEx is
 * absent), back with flags 0.  Returns what the last call returns. */
static int crt_lcmap_string_a(UINT cp, DWORD flags, const char *src, char *dst)
{
    WCHAR in[256], out[256];
    set_err();
    int n = rt_mb_to_wide(cp, MB_PRECOMPOSED, src, 256, NULL, 0);
    T_EQ(n, 256);
    T_EQ(rt_mb_to_wide(cp, MB_PRECOMPOSED, src, 256, in, n), 256);
    int out_n = rt_lcmap_string(LOCALE_NEUTRAL, flags, in, n, NULL, 0);
    T_EQ(out_n, 256);
    T_EQ(rt_lcmap_string(LOCALE_NEUTRAL, flags, in, n, out, out_n), 256);
    int r = rt_wide_to_mb(cp, 0, out, out_n, dst, 256, NULL, NULL);
    T_EQ(T_LAST_ERROR(), SENTINEL);
    return r;
}

TEST(core, crt_startup_initmbctable)
{
    T_CASE("__initmbctable: _setmbcp(GetACP()) and setSBUpLow's character types and case maps of the 256 bytes");
    UINT cp = rt_cp_acp();
    T_EQ(cp, 1252);
    /* _setmbcp_nolock: not one of the five built-in double-byte pages, so GetCPInfo; no lead
     * bytes means a single-byte page.  setSBUpLow asks again. */
    CPINFO info;
    T_REQUIRE(rt_cp_info(cp, &info));
    T_EQ(info.MaxCharSize, 1);
    T_EQ(info.LeadByte[0], 0);
    T_EQ(info.LeadByte[1], 0);

    char sb[256];
    for (int i = 0; i < 256; i++) sb[i] = (char)i;
    sb[0] = ' ';

    /* __crtGetStringTypeA(NULL, CT_CTYPE1, sb, 256, types, cp, lcid, FALSE). */
    WCHAR wide[256];
    WORD types[256];
    set_err();
    int n = rt_mb_to_wide(cp, MB_PRECOMPOSED, sb, 256, NULL, 0);
    T_EQ(n, 256);
    T_EQ(rt_mb_to_wide(cp, MB_PRECOMPOSED, sb, 256, wide, n), 256);
    T_EQ(rt_string_type(CT_CTYPE1, wide, n, types), TRUE);
    T_EQ(T_LAST_ERROR(), SENTINEL);

    uint8_t low[256], up[256];
    memset(low, 0xEE, sizeof low);
    memset(up, 0xEE, sizeof up);
    T_EQ(crt_lcmap_string_a(cp, LCMAP_LOWERCASE, sb, (char *)low), 256);
    T_EQ(crt_lcmap_string_a(cp, LCMAP_UPPERCASE, sb, (char *)up), 256);

    /* The table setSBUpLow builds from the three vectors: the other case of each letter. */
    uint8_t casemap[256];
    for (int i = 0; i < 256; i++)
        casemap[i] = (types[i] & C1_UPPER) ? low[i] : (types[i] & C1_LOWER) ? up[i] : 0;
    T_EQ(casemap['A'], 'a');
    T_EQ(casemap['z'], 'Z');
    T_EQ(casemap['0'], 0);
    T_EQ(casemap[0xC9], 0xE9);          /* E acute */
    T_EQ(casemap[0xE9], 0xC9);
    T_EQ(casemap[0x8A], 0x9A);          /* S caron: both cases are in the 0x80 block */
    T_EQ(casemap[0xFF], 0x9F);          /* y diaeresis: its capital, U+0178, is 0x9F */
    T_EQ(casemap[0xDF], 0xDF);          /* sharp s has no capital */
    /* The two whose capital is not in the page (review finding R2): U+0192 -> U+0191, which
     * best-fits back to 0x83 (Microsoft's bestfit1252.txt); U+00B5 -> U+039C, which the table
     * does not have and which gets the default character.  The conversions are the table's;
     * that Windows's upper-casing gives U+0191 and U+039C is from memory and not verified. */
    T_EQ(casemap[0x83], 0x83);
    T_EQ(casemap[0xB5], '?');
}

TEST(core, crt_startup_setargv)
{
    T_CASE("_setargv: GetCommandLineA gives a line that begins with the program");
    /* (GetModuleFileNameA(NULL, _pgmname, MAX_PATH): the loader.)  parse_cmdline is the C
     * runtime's own code; what it needs is a non-empty line. */
    const CHAR *line = rt_cmdline_a();
    T_REQUIRE(line != NULL);
    T_TRUE(line[0] != 0 && line[0] != ' ');
    const WCHAR *wline = rt_cmdline_w();
    T_REQUIRE(wline != NULL);
    T_EQ(wline[0], (unsigned char)line[0]);
}
