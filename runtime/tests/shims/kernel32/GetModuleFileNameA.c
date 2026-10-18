#include "rt/test.h"
#include "rt/module.h"

/* The names the guest sees (rt/module.h): the executable is C:\Titanfall2\Titanfall2.exe and
 * a system module is C:\Windows\system32\<name>.  Buffers are filled with SENTINEL first, so
 * a test can see every byte the call wrote and that nothing else changed. */
#define EXE_NAME       "C:\\Titanfall2\\Titanfall2.exe"
#define KERNEL32_NAME  "C:\\Windows\\system32\\kernel32.dll"
#define NAME_LEN(s)    ((DWORD)(sizeof s - 1))
#define BUF            64
#define SENTINEL       0xCC

static void fill(char *buf)
{
    memset(buf, SENTINEL, BUF);
}

/* True if buf[0..n-1] is the text s and buf[n] is the terminator. */
static int narrow_is(const char *buf, const char *s, unsigned n)
{
    return memcmp(buf, s, n) == 0 && buf[n] == 0;
}

/* True if a[0..n] equals b[0..n], terminator included. */
static int narrow_same(const char *a, const char *b, unsigned n)
{
    return memcmp(a, b, n + 1) == 0;
}

/* True if buf[from..BUF-1] still holds the sentinel. */
static int untouched_from(const char *buf, unsigned from)
{
    for (unsigned i = from; i < BUF; i++)
        if ((unsigned char)buf[i] != SENTINEL) return 0;
    return 1;
}

TEST(kernel32, GetModuleFileNameA)
{
    char buf[BUF], want[BUF];
    RtModule *exe, *k;
    HMODULE hk;
    DWORD n, want_len;

    k = rt_module_find("kernel32.dll");
    T_REQUIRE(k != NULL);
    hk = (HMODULE)(uintptr_t)k->base;

    T_CASE("the executable for a NULL module");
    exe = rt_module_from_handle(NULL);              /* the core's own answer for the executable */
    T_REQUIRE(exe != NULL);
    want_len = rt_module_filename_a(exe, want, BUF);
    T_EQ(want_len, NAME_LEN(EXE_NAME));
    T_TRUE(narrow_is(want, EXE_NAME, NAME_LEN(EXE_NAME)));
    fill(buf);
    n = CALL(kernel32, GetModuleFileNameA, NULL, buf, BUF);
    T_EQ(n, want_len);
    T_TRUE(narrow_same(buf, want, n));
    T_TRUE(untouched_from(buf, n + 1));

    T_CASE("kernel32 by its handle, with its name and its length");
    fill(buf);
    n = CALL(kernel32, GetModuleFileNameA, hk, buf, BUF);
    T_EQ(n, NAME_LEN(KERNEL32_NAME));
    T_TRUE(narrow_is(buf, KERNEL32_NAME, n));
    T_TRUE(untouched_from(buf, n + 1));

    T_CASE("a buffer one character too small returns nSize, is terminated and sets ERROR_INSUFFICIENT_BUFFER");
    fill(buf);
    CALL(kernel32, SetLastError, 0);
    n = CALL(kernel32, GetModuleFileNameA, hk, buf, NAME_LEN(KERNEL32_NAME));
    T_EQ(n, NAME_LEN(KERNEL32_NAME));
    T_EQ(T_LAST_ERROR(), ERROR_INSUFFICIENT_BUFFER);
    T_TRUE(narrow_is(buf, KERNEL32_NAME, n - 1));  /* the first nSize - 1 characters, then the terminator */
    T_TRUE(untouched_from(buf, n));                 /* and nothing after it */

    T_CASE("nSize 0 returns 0, sets ERROR_INSUFFICIENT_BUFFER and writes nothing");
    fill(buf);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetModuleFileNameA, hk, buf, 0), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INSUFFICIENT_BUFFER);
    T_TRUE(untouched_from(buf, 0));

    T_CASE("a handle that is no module fails with ERROR_MOD_NOT_FOUND");
    fill(buf);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetModuleFileNameA, (HMODULE)(uintptr_t)0x1234, buf, BUF), 0);
    T_EQ(T_LAST_ERROR(), ERROR_MOD_NOT_FOUND);
    T_TRUE(untouched_from(buf, 0));

    T_CASE("success does not change last error");
    fill(buf);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, GetModuleFileNameA, NULL, buf, BUF), want_len);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, GetModuleFileNameA, hk, buf, BUF), NAME_LEN(KERNEL32_NAME));
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* The stop of the core (RT-TRAPS): a NULL buffer with nSize not 0 is fatal. */
    T_TRAPS(CALL(kernel32, GetModuleFileNameA, NULL, NULL, 5), "null buffer");
}
