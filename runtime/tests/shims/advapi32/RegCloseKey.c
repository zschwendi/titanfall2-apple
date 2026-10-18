#include "rt/test.h"
#include "rt/handle.h"
#include "rt/registry.h"

/* The test works under a key of its own, made with the core's calls as in
 * runtime/tests/core/registry_ansi.c.  A handle of another kind is made with the core's own
 * calls, as in runtime/tests/shims/kernel32/CloseHandle.c. */
#define TEST_KEY  "Software\\RtShimTest"

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

TEST(advapi32, RegCloseKey)
{
    HKEY k = NULL, doomed = NULL, ev;

    T_CASE("closes an open key, so the next close of it fails with ERROR_INVALID_HANDLE");
    T_EQ(rt_reg_create_key_ex_a(HKEY_CURRENT_USER, TEST_KEY "\\Child", 0, NULL, 0, KEY_ALL_ACCESS,
                                NULL, &k, NULL), ERROR_SUCCESS);
    T_REQUIRE(k != NULL);
    T_EQ(CALL(advapi32, RegCloseKey, k), ERROR_SUCCESS);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(advapi32, RegCloseKey, k), ERROR_INVALID_HANDLE);
    T_EQ(T_LAST_ERROR(), 0x1234);                   /* a failed handle lookup leaves last error alone */

    T_CASE("a predefined key closes with ERROR_SUCCESS and nothing happens");
    T_EQ(CALL(advapi32, RegCloseKey, HKEY_CURRENT_USER), ERROR_SUCCESS);
    T_EQ(CALL(advapi32, RegCloseKey, HKEY_CURRENT_USER), ERROR_SUCCESS);
    k = NULL;
    T_EQ(CALL(advapi32, RegOpenKeyExA, HKEY_CURRENT_USER, TEST_KEY, 0, KEY_READ, &k), ERROR_SUCCESS);
    T_TRUE(k != NULL && k != HKEY_CURRENT_USER);
    T_EQ(CALL(advapi32, RegCloseKey, k), ERROR_SUCCESS);

    T_CASE("NULL and a handle that is not open fail with ERROR_INVALID_HANDLE");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(advapi32, RegCloseKey, NULL), ERROR_INVALID_HANDLE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(advapi32, RegCloseKey, (HKEY)(uintptr_t)4), ERROR_INVALID_HANDLE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("a handle of another kind (an event) fails with ERROR_INVALID_HANDLE");
    ev = make_event();
    T_REQUIRE(ev != NULL);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(advapi32, RegCloseKey, ev), ERROR_INVALID_HANDLE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(kernel32, CloseHandle, ev), TRUE);    /* the event was not closed by RegCloseKey */

    T_CASE("a handle to a deleted key can be closed");
    T_EQ(rt_reg_create_key_ex_a(HKEY_CURRENT_USER, TEST_KEY "\\Doomed", 0, NULL, 0, KEY_ALL_ACCESS,
                                NULL, &doomed, NULL), ERROR_SUCCESS);
    T_REQUIRE(doomed != NULL);
    T_EQ(rt_reg_delete_key_ex_a(HKEY_CURRENT_USER, TEST_KEY "\\Doomed", 0, 0), ERROR_SUCCESS);
    T_EQ(CALL(advapi32, RegCloseKey, doomed), ERROR_SUCCESS);

    T_CASE("does not change last error on success");
    T_EQ(rt_reg_create_key_ex_a(HKEY_CURRENT_USER, TEST_KEY "\\Child", 0, NULL, 0, KEY_ALL_ACCESS,
                                NULL, &k, NULL), ERROR_SUCCESS);
    T_REQUIRE(k != NULL);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(advapi32, RegCloseKey, k), ERROR_SUCCESS);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(advapi32, RegCloseKey, HKEY_CURRENT_USER), ERROR_SUCCESS);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
