#include "rt/test.h"
#include "rt/handle.h"
#include "rt/registry.h"

/* The test works under a key of its own, made with the core's calls as in
 * runtime/tests/core/registry_ansi.c.  A handle of another kind is made with the core's own
 * calls, as in runtime/tests/shims/kernel32/CloseHandle.c. */
#define TEST_KEY  "Software\\RtShimTest"
#define SENTINEL  ((HKEY)(uintptr_t)0x5555)     /* a value *phkResult must keep on failure */

static void count_destroy(RtObject *o)
{
    (void)o;
}

static const RtObjectOps event_ops = { .name = "test-event", .destroy = count_destroy };

static HKEY make_event(void)
{
    RtObject *o = rt_object_new(RT_K_EVENT, sizeof(RtObject), &event_ops, NULL, NULL);
    T_TRUE(o != NULL);
    return o ? (HKEY)rt_handle_new(o) : NULL;
}

TEST(advapi32, RegOpenKeyExA)
{
    static char long_name[257];
    HKEY base = NULL, child = NULL, doomed = NULL, h = NULL, gone = NULL, ev, hcls = NULL;

    T_EQ(rt_reg_create_key_ex_a(HKEY_CURRENT_USER, TEST_KEY "\\Child", 0, NULL, 0, KEY_ALL_ACCESS,
                                NULL, &child, NULL), ERROR_SUCCESS);
    T_REQUIRE(child != NULL);
    T_EQ(rt_reg_close_key(child), ERROR_SUCCESS);
    /* A key below HKEY_CLASSES_ROOT (Software\Classes), and a handle to it: the subkey of that
     * handle cannot be skipped, the subkey of HKEY_CLASSES_ROOT itself can. */
    T_EQ(rt_reg_create_key_ex_a(HKEY_CLASSES_ROOT, "RtShimClass", 0, NULL, 0, KEY_ALL_ACCESS,
                                NULL, &hcls, NULL), ERROR_SUCCESS);
    T_REQUIRE(hcls != NULL);

    T_CASE("opens an existing key as a new handle");
    T_EQ(CALL(advapi32, RegOpenKeyExA, HKEY_CURRENT_USER, TEST_KEY, 0, KEY_READ, &base), ERROR_SUCCESS);
    T_TRUE(base != NULL && base != HKEY_CURRENT_USER);
    T_EQ(CALL(advapi32, RegOpenKeyExA, base, "Child", 0, KEY_READ, &h), ERROR_SUCCESS);
    T_TRUE(h != NULL && h != base && h != HKEY_CURRENT_USER);
    T_EQ(rt_reg_close_key(h), ERROR_SUCCESS);

    T_CASE("a missing key fails with ERROR_FILE_NOT_FOUND and leaves *phkResult alone");
    h = SENTINEL;
    T_EQ(CALL(advapi32, RegOpenKeyExA, base, "Missing", 0, KEY_READ, &h), ERROR_FILE_NOT_FOUND);
    T_TRUE(h == SENTINEL);

    T_CASE("a NULL or empty subkey gives the predefined key itself, or a new handle under an open key");
    h = NULL;
    T_EQ(CALL(advapi32, RegOpenKeyExA, HKEY_CURRENT_USER, NULL, 0, KEY_READ, &h), ERROR_SUCCESS);
    T_TRUE(h == HKEY_CURRENT_USER);
    h = NULL;
    T_EQ(CALL(advapi32, RegOpenKeyExA, HKEY_CURRENT_USER, "", 0, KEY_READ, &h), ERROR_SUCCESS);
    T_TRUE(h == HKEY_CURRENT_USER);
    h = NULL;
    T_EQ(CALL(advapi32, RegOpenKeyExA, base, "", 0, KEY_READ, &h), ERROR_SUCCESS);
    T_TRUE(h != NULL && h != base);
    T_EQ(rt_reg_close_key(h), ERROR_SUCCESS);

    T_CASE("a trailing backslash is ignored; a leading one is ERROR_BAD_PATHNAME, or skipped under HKEY_CLASSES_ROOT");
    h = NULL;
    T_EQ(CALL(advapi32, RegOpenKeyExA, HKEY_CURRENT_USER, TEST_KEY "\\", 0, KEY_READ, &h), ERROR_SUCCESS);
    T_TRUE(h != NULL && h != HKEY_CURRENT_USER);
    T_EQ(rt_reg_close_key(h), ERROR_SUCCESS);
    h = SENTINEL;
    T_EQ(CALL(advapi32, RegOpenKeyExA, HKEY_CURRENT_USER, "\\" TEST_KEY, 0, KEY_READ, &h), ERROR_BAD_PATHNAME);
    T_TRUE(h == SENTINEL);
    h = NULL;
    T_EQ(CALL(advapi32, RegOpenKeyExA, HKEY_CLASSES_ROOT, "\\RtShimClass", 0, KEY_READ, &h), ERROR_SUCCESS);
    T_TRUE(h != NULL && h != HKEY_CLASSES_ROOT);
    T_EQ(rt_reg_close_key(h), ERROR_SUCCESS);

    T_CASE("a parent that is not open fails with ERROR_INVALID_HANDLE");
    h = SENTINEL;
    T_EQ(CALL(advapi32, RegOpenKeyExA, (HKEY)(uintptr_t)4, "Child", 0, KEY_READ, &h), ERROR_INVALID_HANDLE);
    T_TRUE(h == SENTINEL);
    T_EQ(CALL(advapi32, RegOpenKeyExA, HKEY_DYN_DATA, "Child", 0, KEY_READ, &h), ERROR_INVALID_HANDLE);
    T_TRUE(h == SENTINEL);
    ev = make_event();
    T_REQUIRE(ev != NULL);
    T_EQ(CALL(advapi32, RegOpenKeyExA, ev, "Child", 0, KEY_READ, &h), ERROR_INVALID_HANDLE);
    T_TRUE(h == SENTINEL);
    T_EQ(CALL(advapi32, RegOpenKeyExA, base, "Child", 0, KEY_READ, &gone), ERROR_SUCCESS);
    T_EQ(rt_reg_close_key(gone), ERROR_SUCCESS);
    T_EQ(CALL(advapi32, RegOpenKeyExA, gone, "Child", 0, KEY_READ, &h), ERROR_INVALID_HANDLE);
    T_TRUE(h == SENTINEL);

    T_CASE("a deleted parent fails with ERROR_KEY_DELETED");
    T_EQ(rt_reg_create_key_ex_a(HKEY_CURRENT_USER, TEST_KEY "\\Doomed", 0, NULL, 0, KEY_ALL_ACCESS,
                                NULL, &doomed, NULL), ERROR_SUCCESS);
    T_REQUIRE(doomed != NULL);
    T_EQ(rt_reg_delete_key_ex_a(HKEY_CURRENT_USER, TEST_KEY "\\Doomed", 0, 0), ERROR_SUCCESS);
    h = SENTINEL;
    T_EQ(CALL(advapi32, RegOpenKeyExA, doomed, "Child", 0, KEY_READ, &h), ERROR_KEY_DELETED);
    T_TRUE(h == SENTINEL);

    T_CASE("a NULL result fails with ERROR_INVALID_PARAMETER");
    T_EQ(CALL(advapi32, RegOpenKeyExA, HKEY_CURRENT_USER, TEST_KEY, 0, KEY_READ, NULL), ERROR_INVALID_PARAMETER);

    T_CASE("last error is left alone");
    CALL(kernel32, SetLastError, 0x1234);
    h = NULL;
    T_EQ(CALL(advapi32, RegOpenKeyExA, HKEY_CURRENT_USER, TEST_KEY, 0, KEY_READ, &h), ERROR_SUCCESS);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(rt_reg_close_key(h), ERROR_SUCCESS);
    h = SENTINEL;
    T_EQ(CALL(advapi32, RegOpenKeyExA, base, "Missing", 0, KEY_READ, &h), ERROR_FILE_NOT_FOUND);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(advapi32, RegOpenKeyExA, (HKEY)(uintptr_t)4, "Child", 0, KEY_READ, &h), ERROR_INVALID_HANDLE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(advapi32, RegOpenKeyExA, doomed, "Child", 0, KEY_READ, &h), ERROR_KEY_DELETED);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(advapi32, RegOpenKeyExA, HKEY_CURRENT_USER, TEST_KEY, 0, KEY_READ, NULL), ERROR_INVALID_PARAMETER);
    T_EQ(T_LAST_ERROR(), 0x1234);
    h = NULL;
    T_EQ(CALL(advapi32, RegOpenKeyExA, HKEY_CURRENT_USER, TEST_KEY "\\", 0, KEY_READ, &h), ERROR_SUCCESS);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(rt_reg_close_key(h), ERROR_SUCCESS);
    h = SENTINEL;
    T_EQ(CALL(advapi32, RegOpenKeyExA, HKEY_CURRENT_USER, "\\" TEST_KEY, 0, KEY_READ, &h), ERROR_BAD_PATHNAME);
    T_EQ(T_LAST_ERROR(), 0x1234);
    h = NULL;
    T_EQ(CALL(advapi32, RegOpenKeyExA, HKEY_CLASSES_ROOT, "\\RtShimClass", 0, KEY_READ, &h), ERROR_SUCCESS);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(rt_reg_close_key(h), ERROR_SUCCESS);

    /* The traps: each is a fatal report of the core, and the shim adds no guard for it. */
    T_TRAPS(CALL(advapi32, RegOpenKeyExA, HKEY_CURRENT_USER, TEST_KEY, 1, KEY_READ, &h), "ulOptions");
    T_TRAPS(CALL(advapi32, RegOpenKeyExA, HKEY_CURRENT_USER, TEST_KEY, 0, KEY_READ | 0x00400000u, &h), "not registry access rights");
    T_TRAPS(CALL(advapi32, RegOpenKeyExA, HKEY_PERFORMANCE_DATA, TEST_KEY, 0, KEY_READ, &h), "HKEY_PERFORMANCE_DATA");
    T_TRAPS(CALL(advapi32, RegOpenKeyExA, HKEY_CURRENT_USER, "Software\\\\RtShimTest", 0, KEY_READ, &h), "has an empty name");
    T_TRAPS(CALL(advapi32, RegOpenKeyExA, HKEY_CLASSES_ROOT, "\\", 0, KEY_READ, &h), "backslashes only under HKEY_CLASSES_ROOT");
    T_TRAPS(CALL(advapi32, RegOpenKeyExA, hcls, "\\Child", 0, KEY_READ, &h), "below a handle to a key of HKEY_CLASSES_ROOT");
    memset(long_name, 'a', 256);
    long_name[256] = '\0';
    T_TRAPS(CALL(advapi32, RegOpenKeyExA, HKEY_CURRENT_USER, long_name, 0, KEY_READ, &h), "the limit is 255");

    rt_reg_close_key(base);
    rt_reg_close_key(doomed);
    rt_reg_close_key(hcls);
}
