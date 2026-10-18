#include "rt/test.h"
#include "rt/registry.h"

/* The tests work under HKEY_CURRENT_USER\Software\RtShimTest, a registry of their own.  The tree
 * is made with the core's own calls (rt_reg_create_key_ex_a, rt_reg_set_value_ex_a); the calls
 * under test go through the guest ABI.  The stops come last, each with T_TRAPS. */
#define BASE "Software\\RtShimTest"
#define TOP  BASE "\\Top"

static HKEY make_key(HKEY parent, const char *path)
{
    HKEY k = NULL;
    T_EQ(rt_reg_create_key_ex_a(parent, path, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL), ERROR_SUCCESS);
    return k;
}

TEST(shlwapi, SHDeleteKeyA)
{
    HKEY a, b, k, h = NULL;
    DWORD data = 0x1234, type = 0, cb = 4;
    BYTE buf[4];
    static char name[300];
    static char path[40000];
    int i, n, comp;

    T_CASE("deletes a key with two subkeys, so opening it afterwards fails with ERROR_FILE_NOT_FOUND");
    a = make_key(HKEY_CURRENT_USER, TOP "\\A");     /* kept open for the handle case below */
    b = make_key(HKEY_CURRENT_USER, TOP "\\B");
    T_REQUIRE(a != NULL && b != NULL);
    T_EQ(rt_reg_close_key(b), ERROR_SUCCESS);
    T_EQ(CALL(shlwapi, SHDeleteKeyA, HKEY_CURRENT_USER, TOP), ERROR_SUCCESS);
    T_EQ(rt_reg_open_key_ex_a(HKEY_CURRENT_USER, TOP, 0, KEY_READ, &h), ERROR_FILE_NOT_FOUND);
    T_EQ(rt_reg_open_key_ex_a(HKEY_CURRENT_USER, TOP "\\A", 0, KEY_READ, &h), ERROR_FILE_NOT_FOUND);
    T_EQ(rt_reg_open_key_ex_a(HKEY_CURRENT_USER, BASE, 0, KEY_READ, &h), ERROR_SUCCESS);  /* the parent stays */
    T_EQ(rt_reg_close_key(h), ERROR_SUCCESS);

    T_CASE("removes the values of the keys it deletes");
    k = make_key(HKEY_CURRENT_USER, BASE "\\Vals\\X");
    T_REQUIRE(k != NULL);
    T_EQ(rt_reg_set_value_ex_a(k, "v", 0, REG_DWORD, (const BYTE *)&data, 4), ERROR_SUCCESS);
    T_EQ(rt_reg_close_key(k), ERROR_SUCCESS);
    T_EQ(CALL(shlwapi, SHDeleteKeyA, HKEY_CURRENT_USER, BASE "\\Vals"), ERROR_SUCCESS);
    k = make_key(HKEY_CURRENT_USER, BASE "\\Vals\\X");
    T_REQUIRE(k != NULL);
    T_EQ(rt_reg_query_value_ex_a(k, "v", NULL, &type, buf, &cb), ERROR_FILE_NOT_FOUND);
    T_EQ(rt_reg_close_key(k), ERROR_SUCCESS);

    T_CASE("a handle open to a key of the tree is ERROR_KEY_DELETED and closes with ERROR_SUCCESS");
    T_EQ(CALL(shlwapi, SHDeleteKeyA, a, "x"), ERROR_KEY_DELETED);
    T_EQ(rt_reg_close_key(a), ERROR_SUCCESS);

    T_CASE("deletes a key without subkeys");
    k = make_key(HKEY_CURRENT_USER, BASE "\\Leaf");
    T_EQ(rt_reg_close_key(k), ERROR_SUCCESS);
    T_EQ(CALL(shlwapi, SHDeleteKeyA, HKEY_CURRENT_USER, BASE "\\Leaf"), ERROR_SUCCESS);
    T_EQ(rt_reg_open_key_ex_a(HKEY_CURRENT_USER, BASE "\\Leaf", 0, KEY_READ, &h), ERROR_FILE_NOT_FOUND);

    T_CASE("deleting it again fails with ERROR_FILE_NOT_FOUND");
    T_EQ(CALL(shlwapi, SHDeleteKeyA, HKEY_CURRENT_USER, TOP), ERROR_FILE_NOT_FOUND);

    T_CASE("a handle that is not open fails with ERROR_INVALID_HANDLE");
    k = make_key(HKEY_CURRENT_USER, BASE);
    T_EQ(rt_reg_close_key(k), ERROR_SUCCESS);
    T_EQ(CALL(shlwapi, SHDeleteKeyA, k, "Missing"), ERROR_INVALID_HANDLE);

    T_CASE("a preset last error (0x1234) stays on HKEY_CURRENT_USER, on success and on failure");
    k = make_key(HKEY_CURRENT_USER, BASE "\\Leaf2");
    T_EQ(rt_reg_close_key(k), ERROR_SUCCESS);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(shlwapi, SHDeleteKeyA, HKEY_CURRENT_USER, BASE "\\Leaf2"), ERROR_SUCCESS);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(shlwapi, SHDeleteKeyA, HKEY_CURRENT_USER, BASE "\\Leaf2"), ERROR_FILE_NOT_FOUND);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* The stops (fatal in the core; RT-TRAPS).  Each runs in a forked child: nothing here changes. */
    T_TRAPS(CALL(shlwapi, SHDeleteKeyA, HKEY_CURRENT_USER, NULL), "SHDeleteKeyA: a NULL subkey");
    T_TRAPS(CALL(shlwapi, SHDeleteKeyA, HKEY_CURRENT_USER, ""), "SHDeleteKeyA: an empty subkey");
    T_TRAPS(CALL(shlwapi, SHDeleteKeyA, HKEY_LOCAL_MACHINE, "HARDWARE"), "deleting the seeded key");
    for (i = 0; i < 256; i++) name[i] = 'k';
    name[256] = 0;
    T_TRAPS(CALL(shlwapi, SHDeleteKeyA, HKEY_CURRENT_USER, name), "255");
    n = 0;
    for (comp = 0; comp < 128; comp++) {            /* 128 names of 255 characters: 32,767 */
        if (comp) path[n++] = '\\';
        for (i = 0; i < 255; i++) path[n++] = (char)('a' + comp % 26);
    }
    path[n++] = '\\';                               /* and two more: 32,769 */
    path[n++] = 'x';
    path[n] = 0;
    T_TRAPS(CALL(shlwapi, SHDeleteKeyA, HKEY_CURRENT_USER, path), "32767");
    T_TRAPS(CALL(shlwapi, SHDeleteKeyA, HKEY_CURRENT_USER, "\\Software"), "leading backslash");
    T_TRAPS(CALL(shlwapi, SHDeleteKeyA, HKEY_CURRENT_USER, "Software\\"), "trailing backslash");
    T_TRAPS(CALL(shlwapi, SHDeleteKeyA, HKEY_CURRENT_USER, "Software\\\\Foo"), "two backslashes together");
}
