#include "rt/test.h"
#include "rt/registry.h"
#include "rt/handle.h"

/* Every key of this test is under HKEY_CURRENT_USER\Software\RtShimTest (a fresh registry for each
 * test), except the two that the rules put elsewhere.  Setup and checks that are not the shim's
 * own use the core's calls (rt_reg_*), as runtime/tests/core/registry_ansi.c does. */
#define BASE    "Software\\RtShimTest"
#define SENT    0x1234u

static char long_path[33100];   /* a subkey string over RT_REG_SUBKEY_MAX (32767) characters */

static void noop_destroy(RtObject *o)
{
    (void)o;
}

static const RtObjectOps event_ops = { .name = "test-event", .destroy = noop_destroy };

/* One RegCreateKeyExA call.  The last error is set to a sentinel first and must still be the
 * sentinel after the call: the registry functions never set it. */
static LSTATUS create(HKEY parent, LPCSTR sub, LPSTR cls, DWORD options, REGSAM sam,
                      LPSECURITY_ATTRIBUTES sa, PHKEY out, LPDWORD disp)
{
    LSTATUS st;

    CALL(kernel32, SetLastError, SENT);
    st = CALL(advapi32, RegCreateKeyExA, parent, sub, 0, cls, options, sam, sa, out, disp);
    T_EQ(T_LAST_ERROR(), SENT);
    return st;
}

/* The same call with a handle that the handle table refuses: registry.h (RULE 1) says a failed
 * lookup of a handle may leave last error at ERROR_INVALID_HANDLE, so either is accepted. */
static LSTATUS create_bad_handle(HKEY parent, LPCSTR sub, PHKEY out, LPDWORD disp)
{
    LSTATUS st;

    CALL(kernel32, SetLastError, SENT);
    st = CALL(advapi32, RegCreateKeyExA, parent, sub, 0, NULL, 0, KEY_ALL_ACCESS, NULL, out, disp);
    T_TRUE(T_LAST_ERROR() == SENT || T_LAST_ERROR() == ERROR_INVALID_HANDLE);
    return st;
}

static void close_key(HKEY k)
{
    T_EQ(rt_reg_close_key(k), ERROR_SUCCESS);
}

/* n one-letter components joined by backslashes: buf needs 2 * n bytes. */
static void levels(char *buf, int n)
{
    for (int i = 0; i < n; i++) {
        buf[2 * i] = 'x';
        buf[2 * i + 1] = i + 1 < n ? '\\' : '\0';
    }
    buf[2 * n - 1] = '\0';
}

TEST(advapi32, RegCreateKeyExA)
{
    HKEY k = NULL, k2 = NULL, parent = NULL, hcls = NULL;
    DWORD disp = 0xDEAD, v = 1;
    LSTATUS st;
    char deep[64], long255[257], buf[16];
    DWORD cch;
    SECURITY_ATTRIBUTES sa = { .nLength = sizeof(SECURITY_ATTRIBUTES), .lpSecurityDescriptor = NULL, .bInheritHandle = TRUE };

    T_CASE("a new key reports REG_CREATED_NEW_KEY, and the same call again REG_OPENED_EXISTING_KEY");
    T_EQ(create(HKEY_CURRENT_USER, BASE "\\One", NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_CREATED_NEW_KEY);
    T_TRUE(k != NULL);
    close_key(k);
    T_EQ(create(HKEY_CURRENT_USER, BASE "\\One", NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_OPENED_EXISTING_KEY);
    T_TRUE(k != NULL);
    close_key(k);

    T_CASE("a path of two new components creates both");
    T_EQ(create(HKEY_CURRENT_USER, BASE "\\Two\\Leaf", NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_CREATED_NEW_KEY);
    close_key(k);
    T_EQ(create(HKEY_CURRENT_USER, BASE "\\Two", NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_OPENED_EXISTING_KEY);
    close_key(k);

    T_CASE("a path of 32 levels, the most the documentation allows, is created");
    levels(deep, 32);
    T_EQ(create(HKEY_CURRENT_USER, deep, NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_CREATED_NEW_KEY);
    close_key(k);

    T_CASE("lpdwDisposition NULL is allowed");
    T_EQ(create(HKEY_CURRENT_USER, BASE "\\NoDisp", NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL), ERROR_SUCCESS);
    close_key(k);
    T_EQ(create(HKEY_CURRENT_USER, BASE "\\NoDisp", NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL), ERROR_SUCCESS);
    close_key(k);

    T_CASE("an empty subkey is a new handle to the parent itself, and to a predefined parent a handle that is not the predefined one");
    T_EQ(create(HKEY_CURRENT_USER, BASE "\\Five", NULL, 0, KEY_ALL_ACCESS, NULL, &parent, &disp), ERROR_SUCCESS);
    T_EQ(create(parent, "", NULL, 0, KEY_ALL_ACCESS, NULL, &k2, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_OPENED_EXISTING_KEY);
    T_TRUE(k2 != NULL && k2 != parent);
    close_key(k2);
    close_key(parent);
    T_EQ(create(HKEY_CURRENT_USER, "", NULL, 0, KEY_ALL_ACCESS, NULL, &k2, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_OPENED_EXISTING_KEY);
    T_TRUE(k2 != NULL && k2 != HKEY_CURRENT_USER);
    close_key(k2);

    T_CASE("REG_OPTION_VOLATILE makes the key volatile, and is ignored for a key that exists");
    T_EQ(create(HKEY_CURRENT_USER, BASE "\\Keep", NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_CREATED_NEW_KEY);
    T_EQ(create(HKEY_CURRENT_USER, BASE "\\Keep", NULL, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &k2, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_OPENED_EXISTING_KEY);
    close_key(k2);
    T_EQ(create(k, "Plain", NULL, 0, KEY_ALL_ACCESS, NULL, &k2, &disp), ERROR_SUCCESS);   /* Keep is not volatile */
    close_key(k2);
    close_key(k);
    T_EQ(create(HKEY_CURRENT_USER, BASE "\\Vol", NULL, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &k, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_CREATED_NEW_KEY);
    T_EQ(create(k, "Plain", NULL, 0, KEY_ALL_ACCESS, NULL, &k2, &disp), ERROR_CHILD_MUST_BE_VOLATILE);   /* Vol is volatile */
    T_EQ(create(k, "Vol2", NULL, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &k2, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_CREATED_NEW_KEY);
    close_key(k2);
    close_key(k);

    T_CASE("REG_OPTION_OPEN_LINK is ignored, and a key created with it is an ordinary key");
    T_EQ(create(HKEY_CURRENT_USER, BASE "\\Link", NULL, REG_OPTION_OPEN_LINK, KEY_ALL_ACCESS, NULL, &k, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_CREATED_NEW_KEY);
    T_EQ(create(k, "Plain", NULL, 0, KEY_ALL_ACCESS, NULL, &k2, &disp), ERROR_SUCCESS);   /* Link is not volatile */
    close_key(k2);
    close_key(k);
    T_EQ(create(HKEY_CURRENT_USER, BASE "\\Link", NULL, REG_OPTION_OPEN_LINK, KEY_ALL_ACCESS, NULL, &k, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_OPENED_EXISTING_KEY);
    close_key(k);

    T_CASE("lpClass is kept with a key that is created, and a call that finds the key does not change it");
    T_EQ(create(HKEY_CURRENT_USER, BASE "\\Cls", "Class1", 0, KEY_ALL_ACCESS, NULL, &k, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_CREATED_NEW_KEY);
    cch = sizeof buf;
    T_EQ(rt_reg_query_info_key_a(k, buf, &cch, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ERROR_SUCCESS);
    T_EQ(cch, 6);
    T_TRUE(memcmp(buf, "Class1", 7) == 0);
    T_EQ(create(HKEY_CURRENT_USER, BASE "\\Cls", "Other", 0, KEY_ALL_ACCESS, NULL, &k2, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_OPENED_EXISTING_KEY);
    close_key(k2);
    cch = sizeof buf;
    T_EQ(rt_reg_query_info_key_a(k, buf, &cch, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ERROR_SUCCESS);
    T_EQ(cch, 6);
    T_TRUE(memcmp(buf, "Class1", 7) == 0);
    close_key(k);

    T_CASE("samDesired is the access of the new handle");
    T_EQ(create(HKEY_CURRENT_USER, BASE "\\Sam", NULL, 0, KEY_READ, NULL, &k, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_CREATED_NEW_KEY);
    T_EQ(rt_reg_set_value_ex_a(k, "v", 0, REG_DWORD, (const BYTE *)&v, sizeof v), ERROR_ACCESS_DENIED);
    close_key(k);
    T_EQ(create(HKEY_CURRENT_USER, BASE "\\Sam", NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_OPENED_EXISTING_KEY);
    T_EQ(rt_reg_set_value_ex_a(k, "v", 0, REG_DWORD, (const BYTE *)&v, sizeof v), ERROR_SUCCESS);
    close_key(k);

    T_CASE("samDesired 0 gives a handle that can be closed, flushed and used as a base");
    T_EQ(create(HKEY_CURRENT_USER, BASE "\\Zero", NULL, 0, 0, NULL, &k, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_CREATED_NEW_KEY);
    T_TRUE(k != NULL);
    T_EQ(rt_reg_flush_key(k), ERROR_SUCCESS);
    T_EQ(create(k, "Sub", NULL, 0, KEY_ALL_ACCESS, NULL, &k2, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_CREATED_NEW_KEY);
    close_key(k2);
    T_EQ(rt_reg_open_key_ex_a(k, "Sub", 0, KEY_READ, &k2), ERROR_SUCCESS);
    close_key(k2);
    close_key(k);

    T_CASE("a security attributes structure with no descriptor is accepted");
    T_EQ(create(HKEY_CURRENT_USER, BASE "\\Sec", NULL, 0, KEY_ALL_ACCESS, &sa, &k, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_CREATED_NEW_KEY);
    close_key(k);

    T_CASE("result NULL fails with ERROR_INVALID_PARAMETER");
    T_EQ(create(HKEY_CURRENT_USER, BASE "\\NoResult", NULL, 0, KEY_ALL_ACCESS, NULL, NULL, &disp), ERROR_INVALID_PARAMETER);

    T_CASE("a handle that is not open fails with ERROR_INVALID_HANDLE");
    T_EQ(create_bad_handle(NULL, BASE "\\X", &k, &disp), ERROR_INVALID_HANDLE);
    T_EQ(create_bad_handle(HKEY_DYN_DATA, BASE "\\X", &k, &disp), ERROR_INVALID_HANDLE);
    T_EQ(create(HKEY_CURRENT_USER, BASE "\\Closed", NULL, 0, KEY_ALL_ACCESS, NULL, &parent, &disp), ERROR_SUCCESS);
    close_key(parent);
    T_EQ(create_bad_handle(parent, "Sub", &k, &disp), ERROR_INVALID_HANDLE);
    {
        RtObject *o = rt_object_new(RT_K_EVENT, sizeof(RtObject), &event_ops, NULL, NULL);
        T_REQUIRE(o != NULL);
        parent = (HKEY)rt_handle_new(o);
        T_REQUIRE(parent != NULL);
    }
    T_EQ(create_bad_handle(parent, BASE "\\X", &k, &disp), ERROR_INVALID_HANDLE);

    T_CASE("a parent deleted while open fails with ERROR_KEY_DELETED");
    T_EQ(create(HKEY_CURRENT_USER, BASE "\\Gone", NULL, 0, KEY_ALL_ACCESS, NULL, &parent, &disp), ERROR_SUCCESS);
    T_EQ(rt_reg_delete_key_ex_a(HKEY_CURRENT_USER, BASE "\\Gone", 0, 0), ERROR_SUCCESS);
    T_EQ(create(parent, "Sub", NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp), ERROR_KEY_DELETED);
    close_key(parent);

    T_CASE("a key made directly under HKEY_LOCAL_MACHINE or HKEY_USERS fails with ERROR_ACCESS_DENIED, and an existing one is opened");
    T_EQ(create(HKEY_LOCAL_MACHINE, "RtShimTestNew", NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp), ERROR_ACCESS_DENIED);
    T_EQ(create(HKEY_USERS, "RtShimTestNew", NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp), ERROR_ACCESS_DENIED);
    T_EQ(create(HKEY_LOCAL_MACHINE, "SOFTWARE", NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_OPENED_EXISTING_KEY);
    close_key(k);
    T_EQ(create(HKEY_LOCAL_MACHINE, "SOFTWARE\\RtShimTestDeep", NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_CREATED_NEW_KEY);
    close_key(k);

    T_CASE("a non-volatile key below a volatile key fails with ERROR_CHILD_MUST_BE_VOLATILE");
    T_EQ(create(HKEY_CURRENT_USER, BASE "\\VolP", NULL, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &parent, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_CREATED_NEW_KEY);
    T_EQ(create(parent, "Plain", NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp), ERROR_CHILD_MUST_BE_VOLATILE);
    T_EQ(create(parent, "Vol", NULL, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &k, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_CREATED_NEW_KEY);
    close_key(k);
    close_key(parent);

    T_CASE("a trailing backslash is ignored; a leading one is ERROR_BAD_PATHNAME, or skipped under HKEY_CLASSES_ROOT");
    T_EQ(create(HKEY_CURRENT_USER, BASE "\\Trail\\", NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_CREATED_NEW_KEY);
    close_key(k);
    T_EQ(create(HKEY_CURRENT_USER, BASE "\\Trail\\\\", NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_OPENED_EXISTING_KEY);
    close_key(k);
    T_EQ(rt_reg_open_key_ex_a(HKEY_CURRENT_USER, BASE "\\Trail", 0, KEY_READ, &k2), ERROR_SUCCESS);
    close_key(k2);
    T_EQ(create(HKEY_CURRENT_USER, "\\" BASE "\\Lead", NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp), ERROR_BAD_PATHNAME);
    T_EQ(rt_reg_open_key_ex_a(HKEY_CURRENT_USER, BASE "\\Lead", 0, KEY_READ, &k2), ERROR_FILE_NOT_FOUND);
    T_EQ(create(HKEY_CLASSES_ROOT, "\\RtShimClassNew", NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_CREATED_NEW_KEY);
    close_key(k);
    T_EQ(rt_reg_open_key_ex_a(HKEY_LOCAL_MACHINE, "SOFTWARE\\Classes\\RtShimClassNew", 0, KEY_READ, &k2), ERROR_SUCCESS);
    close_key(k2);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, SENT);
    st = CALL(advapi32, RegCreateKeyExA, HKEY_CURRENT_USER, BASE "\\LastErr", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp);
    T_EQ(st, ERROR_SUCCESS);
    T_EQ(T_LAST_ERROR(), SENT);
    close_key(k);
    st = CALL(advapi32, RegCreateKeyExA, HKEY_CURRENT_USER, BASE "\\NoResult", 0, NULL, 0, KEY_ALL_ACCESS, NULL, NULL, &disp);
    T_EQ(st, ERROR_INVALID_PARAMETER);
    T_EQ(T_LAST_ERROR(), SENT);

    /* The traps: each is a fatal report, no case of its own. */
    for (int i = 0; i < 256; i++)
        long255[i] = 'k';
    long255[256] = '\0';
    /* 129 names of 255 characters with backslashes between: 33023 characters, over the 32767 limit. */
    for (int i = 0, p = 0; i < 129; i++) {
        for (int j = 0; j < 255; j++)
            long_path[p++] = 'k';
        if (i < 128)
            long_path[p++] = '\\';
        long_path[p] = '\0';
    }

    T_TRAPS(CALL(advapi32, RegCreateKeyExA, HKEY_CURRENT_USER, NULL, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp), "lpSubKey NULL");
    T_TRAPS(CALL(advapi32, RegCreateKeyExA, HKEY_CURRENT_USER, BASE "\\Res", 1, NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp), "Reserved is 0x1");
    T_TRAPS(CALL(advapi32, RegCreateKeyExA, HKEY_CURRENT_USER, BASE "\\CL", 0, NULL, REG_OPTION_CREATE_LINK, KEY_ALL_ACCESS, NULL, &k, &disp), "dwOptions 0x2");
    T_TRAPS(CALL(advapi32, RegCreateKeyExA, HKEY_CURRENT_USER, BASE "\\BR", 0, NULL, REG_OPTION_BACKUP_RESTORE, KEY_ALL_ACCESS, NULL, &k, &disp), "dwOptions 0x4");
    {
        SECURITY_ATTRIBUTES sd = { .nLength = sizeof(SECURITY_ATTRIBUTES), .lpSecurityDescriptor = &disp, .bInheritHandle = FALSE };
        T_TRAPS(CALL(advapi32, RegCreateKeyExA, HKEY_CURRENT_USER, BASE "\\SD", 0, NULL, 0, KEY_ALL_ACCESS, &sd, &k, &disp), "security descriptor");
    }
    T_TRAPS(CALL(advapi32, RegCreateKeyExA, HKEY_CURRENT_USER, BASE "\\Bit", 0, NULL, 0, 0x400, NULL, &k, &disp), "samDesired 0x400");
    T_TRAPS(CALL(advapi32, RegCreateKeyExA, HKEY_PERFORMANCE_DATA, BASE "\\Perf", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp), "HKEY_PERFORMANCE_DATA");
    T_TRAPS(CALL(advapi32, RegCreateKeyExA, HKEY_PERFORMANCE_TEXT, BASE "\\PerfT", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp), "HKEY_PERFORMANCE_TEXT");
    T_TRAPS(CALL(advapi32, RegCreateKeyExA, HKEY_PERFORMANCE_NLSTEXT, BASE "\\PerfN", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp), "HKEY_PERFORMANCE_NLSTEXT");
    T_TRAPS(CALL(advapi32, RegCreateKeyExA, HKEY_CURRENT_USER_LOCAL_SETTINGS, BASE "\\Local", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp), "HKEY_CURRENT_USER_LOCAL_SETTINGS");
    T_TRAPS(CALL(advapi32, RegCreateKeyExA, HKEY_CURRENT_USER, BASE "\\\\Dbl", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp), "two backslashes together");
    T_TRAPS(CALL(advapi32, RegCreateKeyExA, HKEY_CLASSES_ROOT, "\\", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp), "backslashes only under HKEY_CLASSES_ROOT");
    T_EQ(rt_reg_open_key_ex_a(HKEY_CLASSES_ROOT, "RtShimClassNew", 0, KEY_READ, &hcls), ERROR_SUCCESS);
    T_TRAPS(CALL(advapi32, RegCreateKeyExA, hcls, "\\Sub", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp), "below a handle to a key of HKEY_CLASSES_ROOT");
    close_key(hcls);
    T_TRAPS(CALL(advapi32, RegCreateKeyExA, HKEY_CURRENT_USER, long255, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp), "the limit is 255");
    T_TRAPS(CALL(advapi32, RegCreateKeyExA, HKEY_CURRENT_USER, long_path, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp), "the limit is 32767");
}
