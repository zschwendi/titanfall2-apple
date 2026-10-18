/* The guest environment (docs/RUNTIME.md section 8.2): rt/env.h.  These tests call the core
 * functions directly; the shims that expose them are tested with their own files.  The
 * command line's quoting is in env_cmdline.c.
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rt/env.h"
#include "rt/machine.h"
#include "rt/test.h"
#include "rt/vm.h"
#include "rt/win/error.h"

/* ---- helpers ---------------------------------------------------------------------------------- */

/* ASCII text as a WCHAR string.  Eight rotating buffers, so one call may take several names.
 * Not for use from more than one thread. */
static WCHAR *W(const char *s) {
    static WCHAR ring[8][512];
    static unsigned next;
    WCHAR *b = ring[next++ % 8];
    size_t i = 0;
    for (; s[i] && i < 511; i++) b[i] = (unsigned char)s[i];
    b[i] = 0;
    return b;
}

static size_t wl(const WCHAR *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

/* True if the WCHAR string w is exactly the ASCII string s. */
static bool is_w(const WCHAR *w, const char *s) {
    for (size_t i = 0;; i++) {
        if (w[i] != (unsigned char)s[i]) return false;
        if (!s[i]) return true;
    }
}

static bool filled(const WCHAR *b, size_t n, WCHAR v) {
    for (size_t i = 0; i < n; i++) if (b[i] != v) return false;
    return true;
}

/* The value of a variable, read into a buffer large enough for any value, equals `want`. */
static bool value_is(const char *name, const char *want) {
    static WCHAR buf[RT_ENV_VALUE_MAX + 1];
    DWORD err = 0x55;
    DWORD n = rt_env_get_w(W(name), buf, (DWORD)(sizeof buf / sizeof buf[0]), &err);
    return err == 0 && n == strlen(want) && is_w(buf, want);
}

/* The entry "NAME=value" (ASCII) is in the block, exactly. */
static bool block_has(const WCHAR *blk, const char *entry) {
    for (const WCHAR *p = blk; *p; p += wl(p) + 1)
        if (is_w(p, entry)) return true;
    return false;
}

/* The same, for an entry given as WCHAR units. */
static bool block_has_w(const WCHAR *blk, const WCHAR *entry, size_t n) {
    for (const WCHAR *p = blk; *p; p += wl(p) + 1)
        if (wl(p) == n && !memcmp(p, entry, n * sizeof *p)) return true;
    return false;
}

/* The last entry of the block is exactly `entry`. */
static bool last_entry_is(const WCHAR *blk, const char *entry) {
    const WCHAR *last = NULL;
    for (const WCHAR *p = blk; *p; p += wl(p) + 1) last = p;
    return last && is_w(last, entry);
}

/* Number of units in a block, the final NUL included. */
static size_t block_units(const WCHAR *blk) {
    size_t n = 0;
    for (const WCHAR *p = blk; *p; p += wl(p) + 1) n = (size_t)(p - blk) + wl(p) + 1;
    return n + 1;
}

/* ---- the fixed set and its spelling -------------------------------------------------------------- */

TEST(core, env_fixed_set)
{
    T_CASE("the fixed set holds the values of the pretend machine");
    T_TRUE(value_is("SystemRoot", "C:\\Windows"));
    T_TRUE(value_is("windir", "C:\\Windows"));
    T_TRUE(value_is("USERPROFILE", "C:\\Users\\Player"));
    T_TRUE(value_is("TEMP", "C:\\Users\\Player\\AppData\\Local\\Temp"));
    T_TRUE(value_is("TMP", "C:\\Users\\Player\\AppData\\Local\\Temp"));
    T_TRUE(value_is("OS", "Windows_NT"));
    T_TRUE(value_is("PROCESSOR_ARCHITECTURE", "AMD64"));
    T_TRUE(value_is("PROCESSOR_LEVEL", "6"));
    T_TRUE(value_is("USERNAME", "Player"));
    T_TRUE(value_is("COMPUTERNAME", "PLAYER-PC"));
    T_TRUE(value_is("Path", "C:\\Windows\\system32;C:\\Windows"));

    /* NUMBER_OF_PROCESSORS is the machine description's processor count, in decimal. */
    char count[16];
    snprintf(count, sizeof count, "%u", (unsigned)rt_machine()->cpu_count);
    T_TRUE(rt_machine()->cpu_count >= 1);
    T_TRUE(value_is("NUMBER_OF_PROCESSORS", count));

    /* Names are kept with the spelling they were given (PATH is "Path" in the table). */
    DWORD err = 0;
    WCHAR *blk = rt_env_block_w(&err);
    T_REQUIRE(blk != NULL);
    T_TRUE(block_has(blk, "Path=C:\\Windows\\system32;C:\\Windows"));
    T_TRUE(!block_has(blk, "PATH=C:\\Windows\\system32;C:\\Windows"));
    T_TRUE(block_has(blk, "TEMP=C:\\Users\\Player\\AppData\\Local\\Temp"));
    T_TRUE(rt_env_block_free(blk));
}

TEST(core, env_get_ignores_case)
{
    T_CASE("names compare ignoring ASCII case; an update keeps the first spelling");
    T_TRUE(value_is("systemroot", "C:\\Windows"));
    T_TRUE(value_is("SYSTEMROOT", "C:\\Windows"));
    T_TRUE(value_is("sYsTeMrOoT", "C:\\Windows"));
    T_TRUE(value_is("path", "C:\\Windows\\system32;C:\\Windows"));

    DWORD err = 0;
    T_TRUE(rt_env_set_w(W("PATH"), W("x"), &err));
    T_EQ(err, 0);
    T_TRUE(value_is("Path", "x"));
    WCHAR *blk = rt_env_block_w(&err);
    T_REQUIRE(blk != NULL);
    T_TRUE(block_has(blk, "Path=x"));
    T_TRUE(!block_has(blk, "PATH=x"));
    T_TRUE(rt_env_block_free(blk));
}

TEST(core, env_get_missing)
{
    T_CASE("a variable that does not exist: 0, ERROR_ENVVAR_NOT_FOUND, buffer untouched");
    WCHAR buf[8];
    DWORD err;
    for (int i = 0; i < 8; i++) buf[i] = 0x1234;

    err = 0;
    T_EQ(rt_env_get_w(W("NoSuchVariable"), buf, 8, &err), 0);
    T_EQ(err, ERROR_ENVVAR_NOT_FOUND);
    T_TRUE(filled(buf, 8, 0x1234));

    /* A prefix or an extension of a real name is another name. */
    err = 0;
    T_EQ(rt_env_get_w(W("SystemRoo"), buf, 8, &err), 0);
    T_EQ(err, ERROR_ENVVAR_NOT_FOUND);
    err = 0;
    T_EQ(rt_env_get_w(W("SystemRootX"), buf, 8, &err), 0);
    T_EQ(err, ERROR_ENVVAR_NOT_FOUND);

    /* A value is not a name. */
    err = 0;
    T_EQ(rt_env_get_w(W("C:\\Windows"), buf, 8, &err), 0);
    T_EQ(err, ERROR_ENVVAR_NOT_FOUND);

    /* A size query for a missing variable is still not found, not a size. */
    err = 0;
    T_EQ(rt_env_get_w(W("NoSuchVariable"), NULL, 0, &err), 0);
    T_EQ(err, ERROR_ENVVAR_NOT_FOUND);
    T_TRUE(filled(buf, 8, 0x1234));
}

TEST(core, env_get_buffer_sizes)
{
    T_CASE("exact fit, one short, room to spare, a zero-size buffer and a size query");
    DWORD err = 0;
    T_TRUE(rt_env_set_w(W("SIZES"), W("abcdef"), &err));
    WCHAR buf[10];

    /* Exact fit: six characters and the terminator. */
    for (int i = 0; i < 10; i++) buf[i] = 0x1234;
    T_EQ(rt_env_get_w(W("SIZES"), buf, 7, &err), 6);
    T_EQ(err, 0);
    T_TRUE(is_w(buf, "abcdef"));
    T_EQ(buf[6], 0);
    T_EQ(buf[7], 0x1234);                       /* nothing past the terminator */

    /* Room to spare: the same count, the same text. */
    for (int i = 0; i < 10; i++) buf[i] = 0x1234;
    T_EQ(rt_env_get_w(W("SIZES"), buf, 10, &err), 6);
    T_TRUE(is_w(buf, "abcdef"));
    T_EQ(buf[7], 0x1234);

    /* One short: the size needed, including the terminator, and nothing is written. */
    for (int i = 0; i < 10; i++) buf[i] = 0x1234;
    err = 0;
    T_EQ(rt_env_get_w(W("SIZES"), buf, 6, &err), 7);
    T_EQ(err, 0);
    T_TRUE(filled(buf, 10, 0x1234));

    /* Size 1 and size 0 with a buffer: the same answer, still nothing written. */
    T_EQ(rt_env_get_w(W("SIZES"), buf, 1, &err), 7);
    T_TRUE(filled(buf, 10, 0x1234));
    T_EQ(rt_env_get_w(W("SIZES"), buf, 0, &err), 7);
    T_TRUE(filled(buf, 10, 0x1234));

    /* Size 0 with no buffer is the size query. */
    err = 0;
    T_EQ(rt_env_get_w(W("SIZES"), NULL, 0, &err), 7);
    T_EQ(err, 0);
    T_TRUE(rt_env_set_w(W("SIZES"), NULL, &err));
}

TEST(core, env_empty_value)
{
    T_CASE("an empty value exists: 0 with no error, and only NULL deletes it");
    DWORD err = 0x77;
    T_TRUE(rt_env_set_w(W("EMPTYVAR"), W(""), &err));
    T_EQ(err, 0);
    WCHAR buf[4];

    buf[0] = 0x55;
    err = 0x77;
    T_EQ(rt_env_get_w(W("EMPTYVAR"), buf, 4, &err), 0);
    T_EQ(err, 0);
    T_EQ(buf[0], 0);

    /* A size-1 buffer takes just the terminator: a success with 0 again. */
    buf[0] = 0x55;
    err = 0x77;
    T_EQ(rt_env_get_w(W("EMPTYVAR"), buf, 1, &err), 0);
    T_EQ(err, 0);
    T_EQ(buf[0], 0);

    /* A size query says one character. */
    err = 0;
    T_EQ(rt_env_get_w(W("EMPTYVAR"), NULL, 0, &err), 1);
    T_EQ(err, 0);

    /* It is in the block as "EMPTYVAR=". */
    WCHAR *blk = rt_env_block_w(&err);
    T_REQUIRE(blk != NULL);
    T_TRUE(block_has(blk, "EMPTYVAR="));
    T_TRUE(rt_env_block_free(blk));

    T_TRUE(rt_env_set_w(W("EMPTYVAR"), NULL, &err));
    err = 0;
    T_EQ(rt_env_get_w(W("EMPTYVAR"), buf, 4, &err), 0);
    T_EQ(err, ERROR_ENVVAR_NOT_FOUND);
}

TEST(core, env_set_and_delete)
{
    T_CASE("set, overwrite, delete; a new variable goes last and an updated one keeps its place");
    DWORD err = 0;
    T_TRUE(rt_env_set_w(W("Fresh"), W("one"), &err));
    T_EQ(err, 0);
    T_TRUE(value_is("FRESH", "one"));

    T_TRUE(rt_env_set_w(W("FRESH"), W("two"), &err));
    T_TRUE(value_is("fresh", "two"));
    WCHAR *blk = rt_env_block_w(&err);
    T_REQUIRE(blk != NULL);
    T_TRUE(block_has(blk, "Fresh=two"));
    T_TRUE(!block_has(blk, "FRESH=two"));
    T_TRUE(last_entry_is(blk, "Fresh=two"));
    T_TRUE(rt_env_block_free(blk));

    /* Updating an earlier variable does not move it, nor Fresh. */
    T_TRUE(rt_env_set_w(W("OS"), W("Other"), &err));
    T_TRUE(value_is("OS", "Other"));
    blk = rt_env_block_w(&err);
    T_REQUIRE(blk != NULL);
    T_TRUE(last_entry_is(blk, "Fresh=two"));
    T_TRUE(rt_env_block_free(blk));

    /* Delete through another spelling; the second delete finds nothing. */
    err = 0;
    T_TRUE(rt_env_set_w(W("fRESH"), NULL, &err));
    T_EQ(err, 0);
    err = 0;
    T_EQ(rt_env_get_w(W("Fresh"), NULL, 0, &err), 0);
    T_EQ(err, ERROR_ENVVAR_NOT_FOUND);
    err = 0;
    T_TRUE(!rt_env_set_w(W("Fresh"), NULL, &err));
    T_EQ(err, ERROR_ENVVAR_NOT_FOUND);

    /* A variable that was never set: the same. */
    err = 0;
    T_TRUE(!rt_env_set_w(W("NeverSet"), NULL, &err));
    T_EQ(err, ERROR_ENVVAR_NOT_FOUND);

    /* Set again: it goes to the end. */
    T_TRUE(rt_env_set_w(W("Fresh"), W("three"), &err));
    blk = rt_env_block_w(&err);
    T_REQUIRE(blk != NULL);
    T_TRUE(last_entry_is(blk, "Fresh=three"));
    T_TRUE(rt_env_block_free(blk));
    T_TRUE(rt_env_set_w(W("Fresh"), NULL, &err));
}

TEST(core, env_value_limit)
{
    T_CASE("a value of RT_ENV_VALUE_MAX characters is stored; one more is refused and the old value stays");
    const size_t max = RT_ENV_VALUE_MAX;
    WCHAR *big = malloc((max + 2) * sizeof *big);
    WCHAR *got = malloc((max + 2) * sizeof *got);
    T_REQUIRE(big != NULL && got != NULL);
    for (size_t i = 0; i < max; i++) big[i] = 'x';
    big[max] = 0;

    DWORD err = 0;
    T_TRUE(rt_env_set_w(W("BIGVAR"), big, &err));
    T_EQ(err, 0);

    /* One short of the value and its terminator: the size needed, nothing written. */
    for (size_t i = 0; i < max + 2; i++) got[i] = 0x1234;
    T_EQ(rt_env_get_w(W("BIGVAR"), got, (DWORD)max, &err), max + 1);
    T_EQ(err, 0);
    T_TRUE(filled(got, max + 2, 0x1234));

    /* Exact fit. */
    T_EQ(rt_env_get_w(W("BIGVAR"), got, (DWORD)(max + 1), &err), max);
    T_TRUE(filled(got, max, 'x'));
    T_EQ(got[max], 0);
    T_EQ(got[max + 1], 0x1234);

    /* One character more than the limit: refused, and the variable keeps its old value. */
    big[max] = 'y';
    big[max + 1] = 0;
    err = 0;
    T_TRUE(!rt_env_set_w(W("BIGVAR"), big, &err));
    T_EQ(err, ERROR_INVALID_PARAMETER);
    for (size_t i = 0; i < max + 2; i++) got[i] = 0x1234;
    T_EQ(rt_env_get_w(W("BIGVAR"), got, (DWORD)(max + 1), &err), max);
    T_TRUE(filled(got, max, 'x'));
    T_EQ(got[max], 0);
    /* And a short value still goes in afterwards. */
    T_TRUE(rt_env_set_w(W("BIGVAR"), W("small"), &err));
    T_TRUE(value_is("BIGVAR", "small"));

    free(big);
    free(got);
}

TEST(core, env_empty_name)
{
    T_CASE("the empty name is no variable");
    DWORD err = 0;
    WCHAR buf[4];
    T_EQ(rt_env_get_w(W(""), buf, 4, &err), 0);
    T_EQ(err, ERROR_ENVVAR_NOT_FOUND);
    err = 0;
    T_TRUE(!rt_env_set_w(W(""), W("x"), &err));
    T_EQ(err, ERROR_INVALID_PARAMETER);
    err = 0;
    T_TRUE(!rt_env_set_w(W(""), NULL, &err));
    T_EQ(err, ERROR_ENVVAR_NOT_FOUND);
}

TEST(core, env_name_beginning_with_equals)
{
    T_CASE("a name may begin with '=': the C runtime's _chdir keeps a drive's directory in \"=C:\"");
    DWORD err = 0x55;
    WCHAR buf[32];
    CHAR abuf[32];

    T_EQ(rt_env_get_w(W("=C:"), buf, 32, &err), 0);
    T_EQ(err, ERROR_ENVVAR_NOT_FOUND);
    err = 0x55;
    T_EQ(rt_env_set_w(W("=C:"), NULL, &err), FALSE);
    T_EQ(err, ERROR_ENVVAR_NOT_FOUND);

    err = 0x55;
    T_EQ(rt_env_set_a("=C:", "C:\\Titanfall2\\bin", &err), TRUE);
    T_EQ(err, 0);
    T_TRUE(value_is("=C:", "C:\\Titanfall2\\bin"));
    T_TRUE(value_is("=c:", "C:\\Titanfall2\\bin"));
    err = 0x55;
    T_EQ(rt_env_get_a("=C:", abuf, 32, &err), 17);
    T_EQ(err, 0);
    T_TRUE(!strcmp(abuf, "C:\\Titanfall2\\bin"));
    /* Neither a shorter nor a longer name finds it, and it hides no other variable. */
    T_EQ(rt_env_get_w(W("="), buf, 32, &err), 0);
    T_EQ(err, ERROR_ENVVAR_NOT_FOUND);
    T_EQ(rt_env_get_w(W("=C"), buf, 32, &err), 0);
    T_EQ(err, ERROR_ENVVAR_NOT_FOUND);
    T_TRUE(value_is("OS", "Windows_NT"));

    /* The block carries the entry as Windows does, "=C:=C:\dir". */
    WCHAR *blk = rt_env_block_w(&err);
    T_REQUIRE(blk != NULL);
    T_TRUE(block_has(blk, "=C:=C:\\Titanfall2\\bin"));
    T_TRUE(rt_env_block_free(blk));

    /* Replacing keeps one entry; deleting removes it. */
    T_EQ(rt_env_set_w(W("=C:"), W("C:\\"), &err), TRUE);
    T_TRUE(value_is("=C:", "C:\\"));
    T_EQ(rt_env_set_w(W("=C:"), NULL, &err), TRUE);
    T_EQ(rt_env_get_w(W("=C:"), buf, 32, &err), 0);
    T_EQ(err, ERROR_ENVVAR_NOT_FOUND);
}

TEST(core, env_name_fatal_cases)
{
    T_CASE("a NULL name, a name with '=' after its first character and a name that is not ASCII are fatal, and the report says why");
    DWORD err = 0;
    WCHAR buf[4];
    WCHAR not_ascii[] = { 'E', 0xE9, 0 };
    CHAR abuf[4];

    T_TRAPS(rt_env_get_w(NULL, buf, 4, &err), "NULL variable name");
    T_TRAPS(rt_env_set_w(NULL, W("x"), &err), "NULL variable name");
    T_TRAPS(rt_env_get_a(NULL, abuf, 4, &err), "the variable name is NULL");
    T_TRAPS(rt_env_set_a(NULL, "x", &err), "the variable name is NULL");

    T_TRAPS(rt_env_get_w(W("A=B"), buf, 4, &err), "contains '=' after its first character");
    T_TRAPS(rt_env_set_w(W("A=B"), W("x"), &err), "contains '=' after its first character");
    T_TRAPS(rt_env_get_w(W("=C:="), buf, 4, &err), "contains '=' after its first character");
    T_TRAPS(rt_env_set_a("==", "x", &err), "contains '=' after its first character");

    T_TRAPS(rt_env_get_w(not_ascii, buf, 4, &err), "U+00E9, which is not ASCII");
    T_TRAPS(rt_env_set_w(not_ascii, W("x"), &err), "U+00E9, which is not ASCII");
    T_TRAPS(rt_env_get_a("\xE9t", abuf, 4, &err), "byte 0xe9 at offset 0, which is not ASCII");
    T_TRAPS(rt_env_set_a("\xE9t", "x", &err), "byte 0xe9 at offset 0, which is not ASCII");
}

TEST(core, env_non_ascii_values)
{
    T_CASE("a W value carries any UTF-16 text exactly; the A forms refuse what they cannot carry");
    const WCHAR uni[] = { 'c', 'a', 'f', 0xE9, ' ', 0xD83D, 0xDE00, 0 };    /* U+1F600 as a surrogate pair */
    DWORD err = 0;
    T_TRUE(rt_env_set_w(W("UNI"), uni, &err));
    T_EQ(err, 0);

    WCHAR got[16];
    T_EQ(rt_env_get_w(W("UNI"), got, 16, &err), 7);
    T_TRUE(memcmp(got, uni, 7 * sizeof *uni) == 0);
    T_EQ(got[7], 0);

    /* The W block holds the entry exactly; the A forms fail loudly. */
    WCHAR *blk = rt_env_block_w(&err);
    T_REQUIRE(blk != NULL);
    WCHAR entry[32];
    memcpy(entry, W("UNI="), 4 * sizeof *entry);
    memcpy(entry + 4, uni, 7 * sizeof *uni);
    T_TRUE(block_has_w(blk, entry, 11));
    T_TRUE(rt_env_block_free(blk));

    CHAR abuf[16];
    T_TRAPS(rt_env_get_a("UNI", abuf, 16, &err), "the value of UNI has U+00E9, which is not ASCII");
    T_TRAPS(rt_env_get_a("UNI", NULL, 0, &err), "the value of UNI has U+00E9, which is not ASCII");
    T_TRAPS(rt_env_block_a(&err), "variable UNI has U+00E9 in it, which is not ASCII");

    /* A byte that is not ASCII in an A value is refused before anything changes. */
    T_TRAPS(rt_env_set_a("UNI2", "caf\xC3\xA9", &err), "value has byte 0xc3 at offset 3, which is not ASCII");

    /* An ASCII A value replaces it, and the A forms work again. */
    T_TRUE(rt_env_set_a("UNI", "plain", &err));
    T_TRUE(value_is("UNI", "plain"));
    T_EQ(rt_env_get_a("UNI", abuf, 16, &err), 5);
    T_TRUE(strcmp(abuf, "plain") == 0);
    T_TRUE(rt_env_set_w(W("UNI"), NULL, &err));
}

TEST(core, env_block_layout)
{
    T_CASE("a W block: entries each followed by NUL, then one more NUL, in guest memory");
    DWORD err = 0;
    WCHAR *blk = rt_env_block_w(&err);
    T_REQUIRE(blk != NULL);
    T_EQ(err, 0);
    T_TRUE(rt_vm_in_vm_arena((uint64_t)(uintptr_t)blk));

    size_t entries = 0;
    bool all_have_equals = true;
    for (const WCHAR *p = blk; *p; p += wl(p) + 1) {
        entries++;
        bool eq = false;
        for (const WCHAR *q = p; *q; q++) if (*q == '=') eq = true;
        if (!eq) all_have_equals = false;
    }
    T_TRUE(entries >= 17);                          /* the fixed set: 15 names and 2 computed (host overrides may add) */
    T_TRUE(all_have_equals);
    T_TRUE(block_has(blk, "SystemRoot=C:\\Windows"));
    T_TRUE(rt_env_block_free(blk));
}

TEST(core, env_block_is_a_snapshot)
{
    T_CASE("a block is a copy: later changes do not show in it");
    DWORD err = 0;
    WCHAR *before = rt_env_block_w(&err);
    T_REQUIRE(before != NULL);
    T_TRUE(rt_env_set_w(W("LATER"), W("1"), &err));
    T_TRUE(!block_has(before, "LATER=1"));
    WCHAR *after = rt_env_block_w(&err);
    T_REQUIRE(after != NULL);
    T_TRUE(block_has(after, "LATER=1"));
    T_TRUE(rt_env_block_free(before));
    T_TRUE(rt_env_block_free(after));
    T_TRUE(rt_env_set_w(W("LATER"), NULL, &err));
}

TEST(core, env_block_empty_and_one)
{
    T_CASE("with no variables the block is one NUL; with one it is that entry and two NULs");
    DWORD err = 0;
    /* Delete every variable that a block names, until the block is empty. */
    for (;;) {
        WCHAR *blk = rt_env_block_w(&err);
        T_REQUIRE(blk != NULL);
        if (blk[0] == 0) {
            T_TRUE(rt_env_block_free(blk));
            break;
        }
        WCHAR name[256];
        size_t n = 0;
        while (blk[n] != '=') { name[n] = blk[n]; n++; }
        name[n] = 0;
        T_TRUE(rt_env_block_free(blk));
        T_TRUE(rt_env_set_w(name, NULL, &err));
    }

    WCHAR *blk = rt_env_block_w(&err);
    T_REQUIRE(blk != NULL);
    T_EQ(block_units(blk), 1);
    T_TRUE(rt_env_block_free(blk));

    err = 0;
    T_EQ(rt_env_get_w(W("SystemRoot"), NULL, 0, &err), 0);
    T_EQ(err, ERROR_ENVVAR_NOT_FOUND);

    T_TRUE(rt_env_set_w(W("ONLY"), W("1"), &err));
    blk = rt_env_block_w(&err);
    T_REQUIRE(blk != NULL);
    T_TRUE(is_w(blk, "ONLY=1"));
    T_EQ(block_units(blk), 8);                       /* "ONLY=1" + NUL + the final NUL */
    T_EQ(blk[6], 0);
    T_EQ(blk[7], 0);
    T_TRUE(rt_env_block_free(blk));
    T_TRUE(rt_env_set_w(W("ONLY"), NULL, &err));
}

TEST(core, env_block_a_matches_w)
{
    T_CASE("the A block carries the same entries, one byte each, in guest memory");
    DWORD err = 0;
    WCHAR *bw = rt_env_block_w(&err);
    T_REQUIRE(bw != NULL);
    CHAR *ba = rt_env_block_a(&err);
    T_REQUIRE(ba != NULL);
    T_EQ(err, 0);
    T_TRUE(rt_vm_in_vm_arena((uint64_t)(uintptr_t)ba));

    size_t total = block_units(bw);
    size_t mismatches = 0;
    for (size_t i = 0; i < total; i++) if ((unsigned char)ba[i] != bw[i]) mismatches++;
    T_EQ(mismatches, 0);
    T_EQ(ba[total - 1], 0);
    T_TRUE(rt_env_block_free(ba));
    T_TRUE(rt_env_block_free(bw));
}

TEST(core, env_block_free_rules)
{
    T_CASE("NULL is accepted; a block is freed once; any other pointer is fatal");
    T_TRUE(rt_env_block_free(NULL));

    DWORD err = 0;
    WCHAR *blk = rt_env_block_w(&err);
    T_REQUIRE(blk != NULL);
    T_TRUE(rt_env_block_free(blk));
    T_TRAPS(rt_env_block_free(blk), "is not a block from GetEnvironmentStrings, or it was freed already");

    /* Memory that is not a block of this kind. */
    uint64_t other = rt_vm_alloc(0, 64, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    T_REQUIRE(other != 0);
    T_TRAPS(rt_env_block_free((void *)(uintptr_t)other), "is not a block from GetEnvironmentStrings");
    T_TRUE(rt_vm_free(other, 0, MEM_RELEASE));

    /* A block of either kind is freed by the same call. */
    CHAR *ba = rt_env_block_a(&err);
    T_REQUIRE(ba != NULL);
    T_TRUE(rt_env_block_free(ba));
}

/* ---- start-up information and the command line ---------------------------------------------- */

TEST(core, env_startup_info)
{
    T_CASE("GetStartupInfo: cb is the structure's size; no handle and no flag is inherited");
    STARTUPINFOW si;
    memset(&si, 0xA5, sizeof si);
    rt_env_startup_info(&si);
    T_EQ(si.cb, sizeof(STARTUPINFOW));
    T_EQ(si.cb, 104);
    T_TRUE(si.lpReserved == NULL && si.lpDesktop == NULL && si.lpTitle == NULL && si.lpReserved2 == NULL);
    T_TRUE(si.hStdInput == NULL && si.hStdOutput == NULL && si.hStdError == NULL);
    T_EQ(si.dwFlags, 0);
    T_EQ(si.wShowWindow, 0);
    /* Every byte after cb is zero, padding included. */
    const unsigned char *b = (const unsigned char *)&si;
    bool zero = true;
    for (size_t i = 4; i < sizeof si; i++) if (b[i]) zero = false;
    T_TRUE(zero);

    T_TRAPS(rt_env_startup_info(NULL), "GetStartupInfoW: NULL buffer");
}

TEST(core, env_command_line)
{
    T_CASE("the command line is the program path, with no arguments, in the same place every time");
    T_EQ(rt_config()->argc, 0);                  /* the runner gives the test no host arguments */
    WCHAR *w = rt_cmdline_w();
    T_REQUIRE(w != NULL);
    T_TRUE(is_w(w, "\"C:\\Titanfall2\\Titanfall2.exe\""));
    T_TRUE(rt_cmdline_w() == w);
    CHAR *a = rt_cmdline_a();
    T_REQUIRE(a != NULL);
    T_TRUE(strcmp(a, "\"C:\\Titanfall2\\Titanfall2.exe\"") == 0);
    T_TRUE(rt_cmdline_a() == a);
    T_TRUE(rt_vm_in_vm_arena((uint64_t)(uintptr_t)w) && rt_vm_in_vm_arena((uint64_t)(uintptr_t)a));
}

/* ---- overrides from the host ----------------------------------------------------------------- */

TEST(core, env_overrides)
{
    T_CASE("RT_GUEST_ entries of the host list set guest variables; other entries are ignored");
    char *list[] = {
        "HOME=/Users/someone",
        "RT_GUESTX=ignored",
        "RT_GUEST_USERNAME=Tester",
        "RT_GUEST_NEWVAR=a=b",
        "RT_GUEST_temp=D:\\Scratch",
        "RT_GUEST_Path=",
        NULL,
    };
    rt_env_apply_overrides(list);
    T_TRUE(value_is("USERNAME", "Tester"));
    T_TRUE(value_is("NEWVAR", "a=b"));
    T_TRUE(value_is("TEMP", "D:\\Scratch"));
    T_TRUE(value_is("Path", ""));

    DWORD err = 0;
    T_EQ(rt_env_get_w(W("HOME"), NULL, 0, &err), 0);
    T_EQ(err, ERROR_ENVVAR_NOT_FOUND);
    err = 0;
    T_EQ(rt_env_get_w(W("RESTX"), NULL, 0, &err), 0);
    T_EQ(err, ERROR_ENVVAR_NOT_FOUND);

    /* The spelling of an existing variable is kept: TEMP, not temp. */
    WCHAR *blk = rt_env_block_w(&err);
    T_REQUIRE(blk != NULL);
    T_TRUE(block_has(blk, "USERNAME=Tester"));
    T_TRUE(block_has(blk, "TEMP=D:\\Scratch"));
    T_TRUE(!block_has(blk, "temp=D:\\Scratch"));
    T_TRUE(rt_env_block_free(blk));

    /* UTF-8 values come through as UTF-16: caf-e-acute, a space, U+1F600. */
    char *utf[] = { "RT_GUEST_UNI=caf\xC3\xA9 \xF0\x9F\x98\x80", NULL };
    rt_env_apply_overrides(utf);
    WCHAR got[16];
    T_EQ(rt_env_get_w(W("UNI"), got, 16, &err), 7);
    const WCHAR want[] = { 'c', 'a', 'f', 0xE9, ' ', 0xD83D, 0xDE00 };
    T_TRUE(memcmp(got, want, sizeof want) == 0);

    /* A NULL list changes nothing. */
    rt_env_apply_overrides(NULL);
    T_EQ(rt_env_get_w(W("UNI"), got, 16, &err), 7);
}

TEST(core, env_config_list)
{
    /* RtConfig.env_overrides: the embedding program's own list, "NAME=value" with no prefix
     * (rt/init.h).  rt_env_init applies it with rt_env_apply_config; the harness passes no
     * list, so the function is driven directly here. */
    T_TRUE(rt_config()->env_overrides == NULL);
    T_CASE("every entry of the list sets its variable; no prefix, nothing is ignored");
    const char *const list[] = {
        "USERNAME=Tester",
        "NEWVAR=a=b",                       /* the value is everything after the first '=' */
        "temp=D:\\Scratch",
        "Path=",
        "RT_GUEST_X=kept as named",          /* no prefix is stripped from this list */
        "UNI=caf\xC3\xA9 \xF0\x9F\x98\x80",
        NULL,
    };
    rt_env_apply_config(list);
    T_TRUE(value_is("USERNAME", "Tester"));
    T_TRUE(value_is("NEWVAR", "a=b"));
    T_TRUE(value_is("TEMP", "D:\\Scratch"));
    T_TRUE(value_is("Path", ""));
    T_TRUE(value_is("RT_GUEST_X", "kept as named"));
    DWORD err = 0;
    WCHAR got[16];
    T_EQ(rt_env_get_w(W("UNI"), got, 16, &err), 7);
    const WCHAR want[] = { 'c', 'a', 'f', 0xE9, ' ', 0xD83D, 0xDE00 };
    T_TRUE(memcmp(got, want, sizeof want) == 0);
    /* The spelling of an existing variable is kept: TEMP, not temp. */
    WCHAR *blk = rt_env_block_w(&err);
    T_REQUIRE(blk != NULL);
    T_TRUE(block_has(blk, "TEMP=D:\\Scratch"));
    T_TRUE(!block_has(blk, "temp=D:\\Scratch"));
    T_TRUE(rt_env_block_free(blk));

    T_CASE("the host's RT_GUEST_ variables are applied after the list and win");
    char *host[] = { "RT_GUEST_USERNAME=FromTheHost", "USERNAME=NotAnOverride", NULL };
    rt_env_apply_overrides(host);
    T_TRUE(value_is("USERNAME", "FromTheHost"));

    T_CASE("a NULL list changes nothing");
    rt_env_apply_config(NULL);
    T_TRUE(value_is("USERNAME", "FromTheHost"));
    T_TRUE(value_is("NEWVAR", "a=b"));

    T_CASE("a malformed entry is fatal and named");
    const char *const no_eq[] = { "NOEQUALS", NULL };
    const char *const no_name[] = { "=C:=C:\\x", NULL };
    const char *const bad_byte[] = { "BAD=\xFF", NULL };
    const char *const name_not_ascii[] = { "\xC3\xA9=1", NULL };
    T_TRAPS(rt_env_apply_config(no_eq), "environment override \"NOEQUALS\" has no '='");
    T_TRAPS(rt_env_apply_config(no_name), "names no variable");
    T_TRAPS(rt_env_apply_config(bad_byte), "an environment override value");
    T_TRAPS(rt_env_apply_config(name_not_ascii), "environment override");
    /* None of the refused lists changed anything. */
    T_TRUE(value_is("USERNAME", "FromTheHost"));
}

TEST(core, env_override_fatal_cases)
{
    T_CASE("a malformed override is fatal, and the report says what is wrong with it");
    char *no_eq[] = { "RT_GUEST_NOEQUALS", NULL };
    char *no_name[] = { "RT_GUEST_=x", NULL };
    char *bad_byte[] = { "RT_GUEST_BAD=\xFF", NULL };
    char *overlong[] = { "RT_GUEST_BAD=\xC0\x80", NULL };
    char *surrogate[] = { "RT_GUEST_BAD=\xED\xA0\x80", NULL };
    char *cut_short[] = { "RT_GUEST_BAD=\xE2\x82", NULL };
    char *name_not_ascii[] = { "RT_GUEST_\xC3\xA9=1", NULL };

    size_t n = RT_ENV_VALUE_MAX + 1;
    char *too_long_value = malloc(sizeof "RT_GUEST_BIG=" + n);
    T_REQUIRE(too_long_value != NULL);
    memcpy(too_long_value, "RT_GUEST_BIG=", sizeof "RT_GUEST_BIG=" - 1);
    memset(too_long_value + sizeof "RT_GUEST_BIG=" - 1, 'x', n);
    too_long_value[sizeof "RT_GUEST_BIG=" - 1 + n] = 0;
    char *too_long[] = { too_long_value, NULL };

    T_TRAPS(rt_env_apply_overrides(no_eq), "environment override \"RT_GUEST_NOEQUALS\" has no '='");
    T_TRAPS(rt_env_apply_overrides(no_name), "names no variable");
    T_TRAPS(rt_env_apply_overrides(bad_byte), "is not valid UTF-8: byte 0xff at offset 0");
    T_TRAPS(rt_env_apply_overrides(overlong), "is not valid UTF-8");
    T_TRAPS(rt_env_apply_overrides(surrogate), "encodes U+D800, which is not a character");
    T_TRAPS(rt_env_apply_overrides(cut_short), "is cut short");
    T_TRAPS(rt_env_apply_overrides(name_not_ascii), "U+00E9, which is not ASCII");
    T_TRAPS(rt_env_apply_overrides(too_long), "has a value of 32768 characters; the limit is 32767");
    free(too_long_value);
}

/* ---- threads ---------------------------------------------------------------------------------- */

#define NTHREADS 4
#define ROUNDS   200

typedef struct { int id; unsigned bad; } Worker;

/* Each worker keeps to its own variable, and reads the shared ones.  The checks are counted
 * here and reported by the main thread: the test harness is not thread-safe. */
static void *worker(void *arg) {
    Worker *me = arg;
    char name[32];
    snprintf(name, sizeof name, "THREAD%d", me->id);
    WCHAR wname[32];
    for (size_t i = 0; i <= strlen(name); i++) wname[i] = (unsigned char)name[i];

    for (int r = 0; r < ROUNDS; r++) {
        char value[48], entry[96];
        snprintf(value, sizeof value, "round-%d-of-%d", r, me->id);
        snprintf(entry, sizeof entry, "%s=%s", name, value);
        WCHAR wval[48];
        for (size_t i = 0; i <= strlen(value); i++) wval[i] = (unsigned char)value[i];

        DWORD err = 0;
        if (!rt_env_set_w(wname, wval, &err) || err) me->bad++;

        WCHAR got[64];
        err = 0;
        DWORD n = rt_env_get_w(wname, got, 64, &err);
        if (err || n != strlen(value)) me->bad++;
        else for (size_t i = 0; i <= n; i++) if (got[i] != (unsigned char)value[i]) { me->bad++; break; }

        /* The shared variable is never disturbed by the others. */
        WCHAR shared[] = { 'S', 'y', 's', 't', 'e', 'm', 'R', 'o', 'o', 't', 0 };
        err = 0;
        if (rt_env_get_w(shared, got, 64, &err) != 10 || err) me->bad++;

        /* A snapshot of the block shows this thread's own latest write. */
        WCHAR *blk = rt_env_block_w(&err);
        if (!blk || err) me->bad++;
        else {
            if (!block_has(blk, entry)) me->bad++;
            if (!block_has(blk, "SystemRoot=C:\\Windows")) me->bad++;
            if (!rt_env_block_free(blk)) me->bad++;
        }
        CHAR *ab = rt_env_block_a(&err);
        if (!ab || err) me->bad++;
        else if (!rt_env_block_free(ab)) me->bad++;

        err = 0;
        if (!rt_env_set_w(wname, NULL, &err) || err) me->bad++;
        err = 0;
        if (rt_env_get_w(wname, got, 64, &err) != 0 || err != ERROR_ENVVAR_NOT_FOUND) me->bad++;
    }
    return NULL;
}

TEST(core, env_threads)
{
    T_CASE("four host threads set, read, snapshot and delete variables at once, each seeing its own writes");
    pthread_t th[NTHREADS];
    Worker w[NTHREADS];
    for (int i = 0; i < NTHREADS; i++) {
        w[i].id = i;
        w[i].bad = 0;
        T_EQ(pthread_create(&th[i], NULL, worker, &w[i]), 0);
    }
    for (int i = 0; i < NTHREADS; i++) pthread_join(th[i], NULL);

    unsigned bad = 0;
    for (int i = 0; i < NTHREADS; i++) bad += w[i].bad;
    T_EQ(bad, 0);

    DWORD err = 0;
    for (int i = 0; i < NTHREADS; i++) {
        char name[32];
        snprintf(name, sizeof name, "THREAD%d", i);
        T_EQ(rt_env_get_w(W(name), NULL, 0, &err), 0);
        T_EQ(err, ERROR_ENVVAR_NOT_FOUND);
    }
    T_TRUE(value_is("SystemRoot", "C:\\Windows"));
}
