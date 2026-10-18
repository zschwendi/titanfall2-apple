#include "rt/test.h"
#include "rt/registry.h"
#include "rt/handle.h"

/* The values are made under HKEY_CURRENT_USER\Software\RtShimTest, a key of the test's own (every
 * test has a fresh registry).  Values are made with rt_reg_set_value_ex_a and read back with
 * rt_reg_query_value_ex_a, the core's own calls. */
#define TEST_KEY "Software\\RtShimTest"

static const char def[] = "default";              /* 8 bytes with its NUL */
static const BYTE dword42[4] = { 0x2A, 0, 0, 0 };
static char long_name[RT_REG_VALUE_NAME_MAX + 2]; /* filled in by the test: one character too long */

static void event_destroy(RtObject *o)
{
    (void)o;
}

static const RtObjectOps event_ops = { .name = "test-event", .destroy = event_destroy };

static HKEY key_with(REGSAM sam)
{
    HKEY k = NULL;
    DWORD disp = 0;
    T_EQ(rt_reg_create_key_ex_a(HKEY_CURRENT_USER, TEST_KEY, 0, NULL, 0, sam, NULL, &k, &disp), ERROR_SUCCESS);
    return k;
}

static void expect_value(HKEY k, LPCSTR name, DWORD type, const void *data, DWORD cb)
{
    BYTE buf[64];
    DWORD got_type = 0xFFFFFFFF, got_cb = sizeof buf;
    T_EQ(rt_reg_query_value_ex_a(k, name, NULL, &got_type, buf, &got_cb), ERROR_SUCCESS);
    T_EQ(got_type, type);
    T_EQ(got_cb, cb);
    T_TRUE(memcmp(buf, data, cb) == 0);
}

TEST(advapi32, RegDeleteValueA)
{
    HKEY k, ro = NULL, sk = NULL;
    RtObject *o;
    HANDLE ev;

    memset(long_name, 'n', RT_REG_VALUE_NAME_MAX + 1);
    long_name[RT_REG_VALUE_NAME_MAX + 1] = 0;

    T_CASE("deletes a value, so it is gone and a second delete is ERROR_FILE_NOT_FOUND");
    k = key_with(KEY_ALL_ACCESS);
    T_EQ(rt_reg_set_value_ex_a(k, "Gone", 0, REG_DWORD, dword42, 4), ERROR_SUCCESS);
    T_EQ(CALL(advapi32, RegDeleteValueA, k, "Gone"), ERROR_SUCCESS);
    T_EQ(rt_reg_query_value_ex_a(k, "Gone", NULL, NULL, NULL, NULL), ERROR_FILE_NOT_FOUND);
    T_EQ(CALL(advapi32, RegDeleteValueA, k, "Gone"), ERROR_FILE_NOT_FOUND);

    T_CASE("the default value is deleted with a NULL or an empty name");
    T_EQ(rt_reg_set_value_ex_a(k, NULL, 0, REG_SZ, (const BYTE *)def, (DWORD)sizeof def), ERROR_SUCCESS);
    T_EQ(CALL(advapi32, RegDeleteValueA, k, NULL), ERROR_SUCCESS);
    T_EQ(rt_reg_query_value_ex_a(k, NULL, NULL, NULL, NULL, NULL), ERROR_FILE_NOT_FOUND);
    T_EQ(rt_reg_set_value_ex_a(k, NULL, 0, REG_SZ, (const BYTE *)def, (DWORD)sizeof def), ERROR_SUCCESS);
    T_EQ(CALL(advapi32, RegDeleteValueA, k, ""), ERROR_SUCCESS);
    T_EQ(rt_reg_query_value_ex_a(k, "", NULL, NULL, NULL, NULL), ERROR_FILE_NOT_FOUND);

    T_CASE("a key without KEY_SET_VALUE fails with ERROR_ACCESS_DENIED and keeps the value");
    T_EQ(rt_reg_set_value_ex_a(k, "Keep", 0, REG_DWORD, dword42, 4), ERROR_SUCCESS);
    T_EQ(rt_reg_open_key_ex_a(HKEY_CURRENT_USER, TEST_KEY, 0, KEY_QUERY_VALUE, &ro), ERROR_SUCCESS);
    T_EQ(CALL(advapi32, RegDeleteValueA, ro, "Keep"), ERROR_ACCESS_DENIED);
    expect_value(k, "Keep", REG_DWORD, dword42, 4);

    T_CASE("a handle that is not a key fails with ERROR_INVALID_HANDLE");
    o = rt_object_new(RT_K_EVENT, sizeof(RtObject), &event_ops, NULL, NULL);
    T_REQUIRE(o != NULL);
    ev = rt_handle_new(o);
    T_REQUIRE(ev != NULL);
    T_EQ(CALL(advapi32, RegDeleteValueA, (HKEY)ev, "Keep"), ERROR_INVALID_HANDLE);
    T_EQ(CALL(advapi32, RegDeleteValueA, (HKEY)(uintptr_t)4, "Keep"), ERROR_INVALID_HANDLE);

    T_CASE("last error is never touched");
    T_EQ(rt_reg_set_value_ex_a(k, "Le", 0, REG_DWORD, dword42, 4), ERROR_SUCCESS);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(advapi32, RegDeleteValueA, k, "Le"), ERROR_SUCCESS);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(advapi32, RegDeleteValueA, k, "Le"), ERROR_FILE_NOT_FOUND);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(advapi32, RegDeleteValueA, ro, "Keep"), ERROR_ACCESS_DENIED);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* The core's stops (RT-TRAPS "core: ..."): each one is fatal and is proved in a child.  The
     * seeded value is CurrentBuild of HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion,
     * which the core seeds at start-up. */
    T_EQ(rt_reg_open_key_ex_a(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0,
                              KEY_ALL_ACCESS, &sk), ERROR_SUCCESS);
    T_TRAPS(CALL(advapi32, RegDeleteValueA, sk, "CurrentBuild"),
            "deleting the seeded value \"CurrentBuild\"");
    T_TRAPS(CALL(advapi32, RegDeleteValueA, k, long_name),
            "a value name of 16384 characters (the limit is 16383)");
    T_TRAPS(CALL(advapi32, RegDeleteValueA, HKEY_PERFORMANCE_DATA, "Keep"),
            "the predefined key HKEY_PERFORMANCE_DATA (0x80000004) is not modelled");
    T_TRAPS(CALL(advapi32, RegDeleteValueA, HKEY_PERFORMANCE_TEXT, "Keep"),
            "the predefined key HKEY_PERFORMANCE_TEXT (0x80000050) is not modelled");
    T_TRAPS(CALL(advapi32, RegDeleteValueA, HKEY_PERFORMANCE_NLSTEXT, "Keep"),
            "the predefined key HKEY_PERFORMANCE_NLSTEXT (0x80000060) is not modelled");
    T_TRAPS(CALL(advapi32, RegDeleteValueA, HKEY_CURRENT_USER_LOCAL_SETTINGS, "Keep"),
            "the predefined key HKEY_CURRENT_USER_LOCAL_SETTINGS (0x80000007) is not modelled");
}
