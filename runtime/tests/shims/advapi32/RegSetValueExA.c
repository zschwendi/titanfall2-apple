#include "rt/test.h"
#include "rt/registry.h"
#include "rt/handle.h"

/* The values are made under HKEY_CURRENT_USER\Software\RtShimTest, a key of the test's own (every
 * test has a fresh registry).  Each value is read back with rt_reg_query_value_ex_a. */
#define TEST_KEY "Software\\RtShimTest"

static const char sz[] = "hello";                 /* 6 bytes with its NUL */
static const char bye[] = "bye";                  /* 4 bytes with its NUL */
static const char def[] = "default";              /* 8 bytes with its NUL */
static const BYTE dword42[4] = { 0x2A, 0, 0, 0 };
/* Bytes above 0x7F are stored as given: REG_BINARY is not a string type, so nothing converts them. */
static const BYTE binary[5] = { 0xDE, 0xAD, 0x00, 0x80, 0xEF };
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

TEST(advapi32, RegSetValueExA)
{
    HKEY k, ro = NULL;
    RtObject *o;
    HANDLE ev;

    memset(long_name, 'n', RT_REG_VALUE_NAME_MAX + 1);
    long_name[RT_REG_VALUE_NAME_MAX + 1] = 0;

    T_CASE("stores a REG_SZ, a REG_DWORD and a REG_BINARY value that the query reads back");
    k = key_with(KEY_ALL_ACCESS);
    T_EQ(CALL(advapi32, RegSetValueExA, k, "Sz", 0, REG_SZ, (const BYTE *)sz, (DWORD)sizeof sz), ERROR_SUCCESS);
    expect_value(k, "Sz", REG_SZ, sz, (DWORD)sizeof sz);
    T_EQ(CALL(advapi32, RegSetValueExA, k, "Dw", 0, REG_DWORD, dword42, 4), ERROR_SUCCESS);
    expect_value(k, "Dw", REG_DWORD, dword42, 4);
    T_EQ(CALL(advapi32, RegSetValueExA, k, "Bin", 0, REG_BINARY, binary, (DWORD)sizeof binary), ERROR_SUCCESS);
    expect_value(k, "Bin", REG_BINARY, binary, (DWORD)sizeof binary);

    T_CASE("a second set replaces the first value, type and data");
    T_EQ(CALL(advapi32, RegSetValueExA, k, "Sz", 0, REG_SZ, (const BYTE *)bye, (DWORD)sizeof bye), ERROR_SUCCESS);
    expect_value(k, "Sz", REG_SZ, bye, (DWORD)sizeof bye);
    T_EQ(CALL(advapi32, RegSetValueExA, k, "Dw", 0, REG_BINARY, binary, 1), ERROR_SUCCESS);
    expect_value(k, "Dw", REG_BINARY, binary, 1);

    T_CASE("the default value is set with a NULL or an empty name");
    T_EQ(CALL(advapi32, RegSetValueExA, k, NULL, 0, REG_SZ, (const BYTE *)def, (DWORD)sizeof def), ERROR_SUCCESS);
    expect_value(k, NULL, REG_SZ, def, (DWORD)sizeof def);
    T_EQ(CALL(advapi32, RegSetValueExA, k, "", 0, REG_SZ, (const BYTE *)bye, (DWORD)sizeof bye), ERROR_SUCCESS);
    expect_value(k, "", REG_SZ, bye, (DWORD)sizeof bye);

    T_CASE("a key without KEY_SET_VALUE, and HKEY_LOCAL_MACHINE, fail with ERROR_ACCESS_DENIED");
    T_EQ(rt_reg_open_key_ex_a(HKEY_CURRENT_USER, TEST_KEY, 0, KEY_QUERY_VALUE, &ro), ERROR_SUCCESS);
    T_EQ(CALL(advapi32, RegSetValueExA, ro, "Denied", 0, REG_DWORD, dword42, 4), ERROR_ACCESS_DENIED);
    T_EQ(CALL(advapi32, RegSetValueExA, HKEY_LOCAL_MACHINE, "Denied", 0, REG_DWORD, dword42, 4), ERROR_ACCESS_DENIED);
    T_EQ(rt_reg_query_value_ex_a(k, "Denied", NULL, NULL, NULL, NULL), ERROR_FILE_NOT_FOUND);

    T_CASE("a handle that is not a key fails with ERROR_INVALID_HANDLE");
    o = rt_object_new(RT_K_EVENT, sizeof(RtObject), &event_ops, NULL, NULL);
    T_REQUIRE(o != NULL);
    ev = rt_handle_new(o);
    T_REQUIRE(ev != NULL);
    T_EQ(CALL(advapi32, RegSetValueExA, (HKEY)ev, "Nk", 0, REG_DWORD, dword42, 4), ERROR_INVALID_HANDLE);
    T_EQ(CALL(advapi32, RegSetValueExA, (HKEY)(uintptr_t)4, "Nk", 0, REG_DWORD, dword42, 4), ERROR_INVALID_HANDLE);

    T_CASE("last error is never touched");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(advapi32, RegSetValueExA, k, "Le", 0, REG_DWORD, dword42, 4), ERROR_SUCCESS);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(advapi32, RegSetValueExA, ro, "Le", 0, REG_DWORD, dword42, 4), ERROR_ACCESS_DENIED);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* The core's stops (RT-TRAPS "core: ..."): each one is fatal and is proved in a child. */
    T_TRAPS(CALL(advapi32, RegSetValueExA, k, "Trap", 1, REG_DWORD, dword42, 4),
            "Reserved is 0x1; it must be 0");
    T_TRAPS(CALL(advapi32, RegSetValueExA, k, "Trap", 0, REG_BINARY, NULL, 4),
            "lpData is NULL but cbData is 4");
    T_TRAPS(CALL(advapi32, RegSetValueExA, k, "Trap", 0, REG_BINARY, binary, RT_REG_VALUE_DATA_MAX + 1),
            "a value of 1048577 bytes (the limit is 1048576)");
    T_TRAPS(CALL(advapi32, RegSetValueExA, k, long_name, 0, REG_DWORD, dword42, 4),
            "a value name of 16384 characters (the limit is 16383)");
    T_TRAPS(CALL(advapi32, RegSetValueExA, HKEY_PERFORMANCE_DATA, "Trap", 0, REG_DWORD, dword42, 4),
            "the predefined key HKEY_PERFORMANCE_DATA (0x80000004) is not modelled");
    T_TRAPS(CALL(advapi32, RegSetValueExA, HKEY_PERFORMANCE_TEXT, "Trap", 0, REG_DWORD, dword42, 4),
            "the predefined key HKEY_PERFORMANCE_TEXT (0x80000050) is not modelled");
    T_TRAPS(CALL(advapi32, RegSetValueExA, HKEY_PERFORMANCE_NLSTEXT, "Trap", 0, REG_DWORD, dword42, 4),
            "the predefined key HKEY_PERFORMANCE_NLSTEXT (0x80000060) is not modelled");
    T_TRAPS(CALL(advapi32, RegSetValueExA, HKEY_CURRENT_USER_LOCAL_SETTINGS, "Trap", 0, REG_DWORD, dword42, 4),
            "the predefined key HKEY_CURRENT_USER_LOCAL_SETTINGS (0x80000007) is not modelled");
}
