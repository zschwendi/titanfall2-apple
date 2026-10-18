#include "rt/test.h"
#include "rt/handle.h"
#include "rt/registry.h"
#include "rt/win/registry.h"

/* Values are stored with rt_reg_set_value_ex_a, under HKEY_CURRENT_USER\Software\RtShimTest; every
 * test has a fresh registry.  The handle that is not a key is an event object, made with the
 * core's own calls as in runtime/tests/shims/kernel32/CloseHandle.c. */
#define SENT 0xA5

static void nothing(RtObject *o)
{
    (void)o;
}

static const RtObjectOps event_ops = { .name = "test-event", .destroy = nothing };

static void fill(BYTE *p, unsigned n, BYTE v)
{
    for (unsigned i = 0; i < n; i++)
        p[i] = v;
}

static int all_are(const BYTE *p, unsigned n, BYTE v)
{
    for (unsigned i = 0; i < n; i++)
        if (p[i] != v)
            return 0;
    return 1;
}

static HKEY test_key(REGSAM sam)
{
    HKEY k = NULL;
    T_EQ(rt_reg_create_key_ex_a(HKEY_CURRENT_USER, "Software\\RtShimTest", 0, NULL, 0, sam, NULL, &k, NULL),
         ERROR_SUCCESS);
    return k;
}

TEST(advapi32, RegQueryValueExA)
{
    HKEY k, ro;
    HANDLE h;
    BYTE buf[16];
    DWORD type, cb, reserved;
    RtObject *o;

    k = test_key(KEY_ALL_ACCESS);
    T_REQUIRE(k != NULL);
    T_EQ(rt_reg_set_value_ex_a(k, "s", 0, REG_SZ, (const BYTE *)"caf\xE9", 5), ERROR_SUCCESS);
    T_EQ(rt_reg_set_value_ex_a(k, "d", 0, REG_DWORD, (const BYTE *)"\x78\x56\x34\x12", 4), ERROR_SUCCESS);

    T_CASE("a REG_SZ value comes back with its type, its data and its size");
    fill(buf, 16, SENT);
    type = 0;
    cb = 16;
    T_EQ(CALL(advapi32, RegQueryValueExA, k, "s", NULL, &type, buf, &cb), ERROR_SUCCESS);
    T_EQ(type, REG_SZ);
    T_EQ(cb, 5);                                    /* the size includes the terminator, in bytes of CP1252 */
    T_TRUE(memcmp(buf, "caf\xE9", 5) == 0);
    T_TRUE(all_are(buf + 5, 11, SENT));
    fill(buf, 16, SENT);                            /* exactly the size fits */
    type = 0;
    cb = 5;
    T_EQ(CALL(advapi32, RegQueryValueExA, k, "s", NULL, &type, buf, &cb), ERROR_SUCCESS);
    T_EQ(cb, 5);
    T_TRUE(memcmp(buf, "caf\xE9", 5) == 0);
    T_TRUE(all_are(buf + 5, 11, SENT));

    T_CASE("a REG_DWORD value comes back with its type, its data and a size of 4");
    fill(buf, 16, SENT);
    type = 0;
    cb = 16;
    T_EQ(CALL(advapi32, RegQueryValueExA, k, "d", NULL, &type, buf, &cb), ERROR_SUCCESS);
    T_EQ(type, REG_DWORD);
    T_EQ(cb, 4);
    T_TRUE(memcmp(buf, "\x78\x56\x34\x12", 4) == 0);
    T_TRUE(all_are(buf + 4, 12, SENT));

    T_CASE("a size query with NULL data gives the size and copies nothing");
    type = 0;
    cb = 0;
    T_EQ(CALL(advapi32, RegQueryValueExA, k, "s", NULL, &type, NULL, &cb), ERROR_SUCCESS);
    T_EQ(type, REG_SZ);
    T_EQ(cb, 5);
    cb = 0;
    T_EQ(CALL(advapi32, RegQueryValueExA, k, "d", NULL, NULL, NULL, &cb), ERROR_SUCCESS);
    T_EQ(cb, 4);
    T_EQ(CALL(advapi32, RegQueryValueExA, k, "d", NULL, NULL, NULL, NULL), ERROR_SUCCESS);  /* only whether it exists */

    T_CASE("a buffer too small gives ERROR_MORE_DATA with the size needed and the type, and leaves the buffer alone");
    fill(buf, 16, SENT);
    type = 0;
    cb = 4;
    T_EQ(CALL(advapi32, RegQueryValueExA, k, "s", NULL, &type, buf, &cb), ERROR_MORE_DATA);
    T_EQ(cb, 5);
    T_EQ(type, REG_SZ);
    T_TRUE(all_are(buf, 16, SENT));

    T_CASE("a value that does not exist fails with ERROR_FILE_NOT_FOUND and leaves the outputs alone");
    fill(buf, 16, SENT);
    type = 0x77;
    cb = 16;
    T_EQ(CALL(advapi32, RegQueryValueExA, k, "nope", NULL, &type, buf, &cb), ERROR_FILE_NOT_FOUND);
    T_EQ(type, 0x77);
    T_EQ(cb, 16);
    T_TRUE(all_are(buf, 16, SENT));

    T_CASE("a handle that is not a key fails with ERROR_INVALID_HANDLE and leaves the outputs alone");
    o = rt_object_new(RT_K_EVENT, sizeof(RtObject), &event_ops, NULL, NULL);
    T_REQUIRE(o != NULL);
    h = rt_handle_new(o);
    T_REQUIRE(h != NULL);
    fill(buf, 16, SENT);
    type = 0x77;
    cb = 16;
    T_EQ(CALL(advapi32, RegQueryValueExA, (HKEY)h, "s", NULL, &type, buf, &cb), ERROR_INVALID_HANDLE);
    T_EQ(type, 0x77);
    T_EQ(cb, 16);
    T_TRUE(all_are(buf, 16, SENT));
    T_EQ(CALL(advapi32, RegQueryValueExA, NULL, "s", NULL, NULL, NULL, NULL), ERROR_INVALID_HANDLE);
    rt_handle_close(h);

    T_CASE("a key without KEY_QUERY_VALUE fails with ERROR_ACCESS_DENIED");
    ro = test_key(KEY_SET_VALUE);
    T_REQUIRE(ro != NULL);
    fill(buf, 16, SENT);
    type = 0x77;
    cb = 16;
    T_EQ(CALL(advapi32, RegQueryValueExA, ro, "s", NULL, &type, buf, &cb), ERROR_ACCESS_DENIED);
    T_EQ(type, 0x77);
    T_EQ(cb, 16);
    T_TRUE(all_are(buf, 16, SENT));
    rt_handle_close(ro);

    T_CASE("a reserved pointer that is not NULL, or data without a size, fails with ERROR_INVALID_PARAMETER");
    fill(buf, 16, SENT);
    type = 0x77;
    cb = 16;
    reserved = 0;
    T_EQ(CALL(advapi32, RegQueryValueExA, k, "s", &reserved, &type, buf, &cb), ERROR_INVALID_PARAMETER);
    T_EQ(CALL(advapi32, RegQueryValueExA, k, "s", NULL, &type, buf, NULL), ERROR_INVALID_PARAMETER);
    T_EQ(type, 0x77);
    T_EQ(cb, 16);
    T_TRUE(all_are(buf, 16, SENT));

    T_CASE("does not change last error, on success or failure");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(advapi32, RegQueryValueExA, k, "s", NULL, &type, buf, &cb), ERROR_SUCCESS);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(advapi32, RegQueryValueExA, k, "nope", NULL, &type, buf, &cb), ERROR_FILE_NOT_FOUND);
    T_EQ(T_LAST_ERROR(), 0x1234);

    rt_handle_close(k);
}
