/* The registry (docs/RUNTIME.md section 8.3): rt/registry.h.  This file: the predefined keys,
 * opening, creating and closing keys, names and their case, the default value and the basic value
 * round trip.  Buffers and types: registry_values.c; enumeration: registry_enum.c; the A functions:
 * registry_ansi.c; access, volatile keys, deleted keys: registry_keys.c; the file: registry_file*.c;
 * the seed: registry_seed.c; the loud cases and the log: registry_loud.c; threads: registry_threads.c.
 *
 * What is behind these: advapi32's RegOpenKeyEx, RegCreateKeyEx, RegCloseKey, RegSetValueEx,
 * RegQueryValueEx, called here as the core functions the shims forward to. */
#include "rt/handle.h"
#include "registry_util.c"

/* ---- predefined keys -------------------------------------------------------------------------- */

TEST(core, registry_predefined_constants)
{
    T_CASE("the predefined keys are the Windows constants, sign-extended to 64 bits");
    T_EQ((uint64_t)(uintptr_t)HKEY_CLASSES_ROOT, UINT64_C(0xFFFFFFFF80000000));
    T_EQ((uint64_t)(uintptr_t)HKEY_CURRENT_USER, UINT64_C(0xFFFFFFFF80000001));
    T_EQ((uint64_t)(uintptr_t)HKEY_LOCAL_MACHINE, UINT64_C(0xFFFFFFFF80000002));
    T_EQ((uint64_t)(uintptr_t)HKEY_USERS, UINT64_C(0xFFFFFFFF80000003));
    T_EQ((uint64_t)(uintptr_t)HKEY_PERFORMANCE_DATA, UINT64_C(0xFFFFFFFF80000004));
    T_EQ((uint64_t)(uintptr_t)HKEY_CURRENT_CONFIG, UINT64_C(0xFFFFFFFF80000005));
    T_EQ((uint64_t)(uintptr_t)HKEY_DYN_DATA, UINT64_C(0xFFFFFFFF80000006));
    T_EQ((uint64_t)(uintptr_t)HKEY_CURRENT_USER_LOCAL_SETTINGS, UINT64_C(0xFFFFFFFF80000007));
    T_EQ((uint64_t)(uintptr_t)HKEY_PERFORMANCE_TEXT, UINT64_C(0xFFFFFFFF80000050));
    T_EQ((uint64_t)(uintptr_t)HKEY_PERFORMANCE_NLSTEXT, UINT64_C(0xFFFFFFFF80000060));
}

TEST(core, registry_predefined_spellings)
{
    T_CASE("a predefined key given truncated to 32 bits or sign-extended names the same key");
    HKEY roots[] = { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER, HKEY_USERS, HKEY_CLASSES_ROOT, HKEY_CURRENT_CONFIG };
    for (unsigned i = 0; i < sizeof roots / sizeof roots[0]; i++) {
        HKEY ext = roots[i];
        HKEY trunc = (HKEY)(uintptr_t)(uint32_t)(uintptr_t)roots[i];
        T_TRUE((uint64_t)(uintptr_t)trunc >> 32 == 0);

        /* A key made through one spelling is found through the other. */
        char name[32];
        snprintf(name, sizeof name, "Probe%u", i);
        HKEY made = NULL;
        DWORD disp = 0;
        LSTATUS st = rt_reg_create_key_ex_w(ext, W(name), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &made, &disp);
        if (roots[i] == HKEY_LOCAL_MACHINE || roots[i] == HKEY_USERS) {
            T_EQ(st, ERROR_ACCESS_DENIED);               /* no new direct subkeys of those two */
            continue;
        }
        T_EQ(st, ERROR_SUCCESS);
        T_EQ(disp, REG_CREATED_NEW_KEY);
        cl(made);
        HKEY found = NULL;
        T_EQ(rt_reg_open_key_ex_w(trunc, W(name), 0, KEY_READ, &found), ERROR_SUCCESS);
        cl(found);
        T_EQ(rt_reg_open_key_ex_w(ext, W(name), 0, KEY_READ, &found), ERROR_SUCCESS);
        cl(found);
    }

    T_CASE("closing a predefined key succeeds and changes nothing, in either spelling");
    T_EQ(rt_reg_close_key(HKEY_LOCAL_MACHINE), ERROR_SUCCESS);
    T_EQ(rt_reg_close_key((HKEY)(uintptr_t)0x80000002u), ERROR_SUCCESS);
    T_EQ(rt_reg_close_key(HKEY_LOCAL_MACHINE), ERROR_SUCCESS);
    HKEY k = NULL;
    T_EQ(rt_reg_open_key_ex_w(HKEY_LOCAL_MACHINE, W("SOFTWARE"), 0, KEY_READ, &k), ERROR_SUCCESS);
    cl(k);
}

TEST(core, registry_predefined_invalid)
{
    T_CASE("values that are not handles are ERROR_INVALID_HANDLE");
    HKEY k = (HKEY)0x1234;
    HKEY bad[] = {
        NULL,
        (HKEY)(uintptr_t)1,
        (HKEY)(uintptr_t)4,
        INVALID_HANDLE_VALUE,                               /* (HANDLE)-1 is the process pseudo-handle */
        (HKEY)(intptr_t)-2,
        HKEY_DYN_DATA,                                      /* Windows 9x only */
        (HKEY)(uintptr_t)0x80000008u,
        (HKEY)(uintptr_t)0x80000040u,
        (HKEY)(uintptr_t)0xFFFFFFFFu,
        (HKEY)(uintptr_t)0x180000002ull,                    /* the constant with a stray bit above */
        (HKEY)(uintptr_t)0x7FFFFFFF80000002ull,
        (HKEY)(uintptr_t)0x0000000100000000ull,
    };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        k = (HKEY)0x1234;
        T_EQ(rt_reg_open_key_ex_w(bad[i], W("SOFTWARE"), 0, KEY_READ, &k), ERROR_INVALID_HANDLE);
        T_TRUE(k == (HKEY)0x1234);                          /* untouched */
        T_EQ(rt_reg_close_key(bad[i]), ERROR_INVALID_HANDLE);
        T_EQ(rt_reg_flush_key(bad[i]), ERROR_INVALID_HANDLE);
        DWORD n = 0;
        T_EQ(rt_reg_query_value_ex_w(bad[i], W("x"), NULL, NULL, NULL, &n), ERROR_INVALID_HANDLE);
        T_EQ(rt_reg_set_value_ex_w(bad[i], W("x"), 0, REG_DWORD, (const BYTE *)&n, 4), ERROR_INVALID_HANDLE);
        T_EQ(rt_reg_delete_value_w(bad[i], W("x")), ERROR_INVALID_HANDLE);
        T_EQ(rt_reg_delete_key_ex_w(bad[i], W("x"), 0, 0), ERROR_INVALID_HANDLE);
        WCHAR name[8];
        DWORD cch = 8;
        T_EQ(rt_reg_enum_key_ex_w(bad[i], 0, name, &cch, NULL, NULL, NULL, NULL), ERROR_INVALID_HANDLE);
        cch = 8;
        T_EQ(rt_reg_enum_value_w(bad[i], 0, name, &cch, NULL, NULL, NULL, NULL), ERROR_INVALID_HANDLE);
        T_EQ(rt_reg_query_info_key_w(bad[i], NULL, NULL, NULL, &n, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ERROR_INVALID_HANDLE);
    }

    T_CASE("the predefined keys the tree does not model stop the runtime");
    T_TRAPS(rt_reg_open_key_ex_w(HKEY_PERFORMANCE_DATA, W("x"), 0, KEY_READ, &k), "HKEY_PERFORMANCE_DATA");
    T_TRAPS(rt_reg_open_key_ex_w(HKEY_PERFORMANCE_TEXT, W("x"), 0, KEY_READ, &k), "HKEY_PERFORMANCE_TEXT");
    T_TRAPS(rt_reg_open_key_ex_w(HKEY_PERFORMANCE_NLSTEXT, W("x"), 0, KEY_READ, &k), "HKEY_PERFORMANCE_NLSTEXT");
    T_TRAPS(rt_reg_open_key_ex_w(HKEY_CURRENT_USER_LOCAL_SETTINGS, W("x"), 0, KEY_READ, &k), "HKEY_CURRENT_USER_LOCAL_SETTINGS");
    T_TRAPS(rt_reg_close_key(HKEY_PERFORMANCE_DATA), "HKEY_PERFORMANCE_DATA");
}

TEST(core, registry_other_kind_of_handle)
{
    T_CASE("a handle of another kind is not a registry key, and RegCloseKey does not close it");
    static const RtObjectOps other_ops = { .name = "other" };
    RtObject *o = rt_object_new(RT_K_FILE, sizeof(RtObject), &other_ops, NULL, NULL);
    T_REQUIRE(o != NULL);
    HANDLE h = rt_handle_new(o);
    T_REQUIRE(h != NULL);
    HKEY k = NULL;
    T_EQ(rt_reg_open_key_ex_w((HKEY)h, W("SOFTWARE"), 0, KEY_READ, &k), ERROR_INVALID_HANDLE);
    T_EQ(rt_reg_close_key((HKEY)h), ERROR_INVALID_HANDLE);
    RtObject *again = rt_handle_ref(h, RT_K_FILE);          /* the handle is still open */
    T_TRUE(again == o);
    if (again) rt_object_unref(again);
    T_TRUE(rt_handle_close(h));
}

/* ---- create, open, close ---------------------------------------------------------------------- */

TEST(core, registry_create_open_basic)
{
    T_CASE("create reports the disposition; creating again opens the existing key");
    HKEY a = NULL, b = NULL;
    DWORD disp = 0xAA;
    T_EQ(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("Software\\Probe"), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &a, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_CREATED_NEW_KEY);
    T_TRUE(a != NULL);
    T_EQ(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("Software\\Probe"), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &b, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_OPENED_EXISTING_KEY);
    T_TRUE(b != NULL && b != a);                            /* every open is a handle of its own */

    T_CASE("the disposition pointer is optional");
    HKEY c = NULL;
    T_EQ(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("Software\\Probe"), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &c, NULL), ERROR_SUCCESS);
    cl(a); cl(b); cl(c);

    T_CASE("a handle is a multiple of 4 below 2^31: the table's handles, never a predefined key");
    HKEY d = mk(HKEY_CURRENT_USER, "Software\\Probe");
    T_TRUE(((uintptr_t)d & 3) == 0 && ((uintptr_t)d >> 31) == 0 && d != NULL);
    cl(d);
}

TEST(core, registry_create_intermediate)
{
    T_CASE("creating a deep path creates the keys on the way; the disposition is that of the last one");
    HKEY k = NULL;
    DWORD disp = 0;
    T_EQ(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("Software\\Deep\\Er\\Est"), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_CREATED_NEW_KEY);
    cl(k);
    T_TRUE(has_key(HKEY_CURRENT_USER, "Software\\Deep"));
    T_TRUE(has_key(HKEY_CURRENT_USER, "Software\\Deep\\Er"));
    T_TRUE(has_key(HKEY_CURRENT_USER, "Software\\Deep\\Er\\Est"));
    T_TRUE(!has_key(HKEY_CURRENT_USER, "Software\\Deep\\Er\\Est\\More"));

    T_CASE("a sibling of an existing key is new; an existing middle key is opened");
    T_EQ(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("Software\\Deep\\Er\\Other"), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_CREATED_NEW_KEY);
    cl(k);
    T_EQ(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("Software\\Deep\\Er"), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_OPENED_EXISTING_KEY);
    cl(k);

    T_CASE("a path may be relative to an open key as well as to a root");
    HKEY base = mk(HKEY_CURRENT_USER, "Software\\Deep");
    T_EQ(rt_reg_create_key_ex_w(base, W("Er\\Est"), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_OPENED_EXISTING_KEY);
    cl(k);
    T_EQ(rt_reg_open_key_ex_w(base, W("Er\\Other"), 0, KEY_READ, &k), ERROR_SUCCESS);
    cl(k);
    cl(base);
}

TEST(core, registry_open_missing)
{
    T_CASE("a key that is not there is ERROR_FILE_NOT_FOUND and the result is not written");
    HKEY k = (HKEY)0x7777;
    T_EQ(rt_reg_open_key_ex_w(HKEY_CURRENT_USER, W("Software\\Nope"), 0, KEY_READ, &k), ERROR_FILE_NOT_FOUND);
    T_TRUE(k == (HKEY)0x7777);
    T_EQ(rt_reg_open_key_ex_w(HKEY_LOCAL_MACHINE, W("Nope\\Deeper"), 0, KEY_READ, &k), ERROR_FILE_NOT_FOUND);
    T_TRUE(k == (HKEY)0x7777);

    T_CASE("the last name, a middle name, and a prefix or extension of a real name all miss");
    HKEY made = mk(HKEY_CURRENT_USER, "Software\\Real");
    cl(made);
    T_EQ(open_st(HKEY_CURRENT_USER, "Software\\Rea", KEY_READ, &k), ERROR_FILE_NOT_FOUND);
    T_EQ(open_st(HKEY_CURRENT_USER, "Software\\Reall", KEY_READ, &k), ERROR_FILE_NOT_FOUND);
    T_EQ(open_st(HKEY_CURRENT_USER, "Software\\Real\\Child", KEY_READ, &k), ERROR_FILE_NOT_FOUND);
    T_EQ(open_st(HKEY_CURRENT_USER, "Softwar\\Real", KEY_READ, &k), ERROR_FILE_NOT_FOUND);
    T_TRUE(k == (HKEY)0x7777);

    T_CASE("a value is not a key, and a key is not a value");
    HKEY r = op(HKEY_CURRENT_USER, "Software\\Real", KEY_ALL_ACCESS);
    T_EQ(put_dw(r, "Thing", 1), ERROR_SUCCESS);
    T_EQ(open_st(r, "Thing", KEY_READ, &k), ERROR_FILE_NOT_FOUND);
    HKEY sub = mk(r, "Sub");
    cl(sub);
    T_EQ(rt_reg_query_value_ex_w(r, W("Sub"), NULL, NULL, NULL, NULL), ERROR_FILE_NOT_FOUND);
    cl(r);
}

TEST(core, registry_open_null_result)
{
    T_CASE("a NULL result pointer is ERROR_INVALID_PARAMETER, for open and for create");
    T_EQ(rt_reg_open_key_ex_w(HKEY_CURRENT_USER, W("Software"), 0, KEY_READ, NULL), ERROR_INVALID_PARAMETER);
    T_EQ(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("Software"), 0, NULL, 0, KEY_READ, NULL, NULL, NULL), ERROR_INVALID_PARAMETER);
    T_EQ(rt_reg_open_key_ex_a(HKEY_CURRENT_USER, "Software", 0, KEY_READ, NULL), ERROR_INVALID_PARAMETER);
    T_EQ(rt_reg_create_key_ex_a(HKEY_CURRENT_USER, "Software", 0, NULL, 0, KEY_READ, NULL, NULL, NULL), ERROR_INVALID_PARAMETER);
}

TEST(core, registry_empty_subkey)
{
    T_CASE("open of NULL or \"\" below a predefined key returns that very handle, as passed");
    HKEY k = NULL;
    T_EQ(rt_reg_open_key_ex_w(HKEY_LOCAL_MACHINE, NULL, 0, KEY_READ, &k), ERROR_SUCCESS);
    T_TRUE(k == HKEY_LOCAL_MACHINE);
    k = NULL;
    T_EQ(rt_reg_open_key_ex_w(HKEY_LOCAL_MACHINE, W(""), 0, KEY_READ, &k), ERROR_SUCCESS);
    T_TRUE(k == HKEY_LOCAL_MACHINE);
    HKEY trunc = (HKEY)(uintptr_t)0x80000001u;
    T_EQ(rt_reg_open_key_ex_w(trunc, NULL, 0, KEY_READ, &k), ERROR_SUCCESS);
    T_TRUE(k == trunc);                                     /* the truncated spelling comes back truncated */

    T_CASE("open of NULL or \"\" below an open key returns a new handle to the same key");
    HKEY a = mk(HKEY_CURRENT_USER, "Software\\Same");
    HKEY b = NULL, c = NULL;
    T_EQ(rt_reg_open_key_ex_w(a, NULL, 0, KEY_ALL_ACCESS, &b), ERROR_SUCCESS);
    T_EQ(rt_reg_open_key_ex_w(a, W(""), 0, KEY_ALL_ACCESS, &c), ERROR_SUCCESS);
    T_TRUE(b != NULL && c != NULL && b != a && c != a && b != c);
    T_EQ(put_dw(b, "x", 5), ERROR_SUCCESS);
    T_TRUE(is_dw(a, "x", 5) && is_dw(c, "x", 5));           /* the same key */
    cl(a);
    T_TRUE(is_dw(b, "x", 5));                               /* closing one handle leaves the others */
    cl(b); cl(c);

    T_CASE("create of NULL or \"\" opens the key itself with a NEW handle, also for a predefined key");
    DWORD disp = 0;
    T_EQ(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W(""), 0, NULL, 0, KEY_READ, NULL, &k, &disp), ERROR_SUCCESS);
    T_TRUE(k != HKEY_CURRENT_USER && k != NULL);
    T_EQ(disp, REG_OPENED_EXISTING_KEY);
    cl(k);
    T_EQ(rt_reg_create_key_ex_w(HKEY_LOCAL_MACHINE, NULL, 0, NULL, 0, KEY_READ, NULL, &k, &disp), ERROR_SUCCESS);
    T_TRUE(k != HKEY_LOCAL_MACHINE);
    T_EQ(disp, REG_OPENED_EXISTING_KEY);
    cl(k);
}

TEST(core, registry_close_key)
{
    T_CASE("RegCloseKey closes once; closing again, or using the handle, is ERROR_INVALID_HANDLE");
    HKEY k = mk(HKEY_CURRENT_USER, "Software\\Closing");
    T_EQ(rt_reg_close_key(k), ERROR_SUCCESS);
    T_EQ(rt_reg_close_key(k), ERROR_INVALID_HANDLE);
    HKEY x = NULL;
    T_EQ(rt_reg_open_key_ex_w(k, W("A"), 0, KEY_READ, &x), ERROR_INVALID_HANDLE);
    T_EQ(put_dw(k, "v", 1), ERROR_INVALID_HANDLE);
    T_EQ(rt_reg_flush_key(k), ERROR_INVALID_HANDLE);

    T_CASE("a handle number is not reused at once: a stale one does not name the next key");
    HKEY k2 = mk(HKEY_CURRENT_USER, "Software\\Closing");
    T_TRUE(k2 != k);
    T_EQ(rt_reg_close_key(k), ERROR_INVALID_HANDLE);
    cl(k2);

    T_CASE("the low two bits of a handle are ignored and so are the upper 32, as for every kernel handle");
    HKEY k3 = mk(HKEY_CURRENT_USER, "Software\\Closing");
    HKEY ext = (HKEY)((uintptr_t)k3 | UINT64_C(0xFFFFFFFF00000000));
    DWORD n = 0;
    T_EQ(rt_reg_query_info_key_w(ext, NULL, NULL, NULL, &n, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ERROR_SUCCESS);
    cl(k3);
}

TEST(core, registry_flush)
{
    T_CASE("RegFlushKey succeeds on an open key and on a predefined key");
    HKEY k = mk(HKEY_CURRENT_USER, "Software\\Flush");
    T_EQ(rt_reg_flush_key(k), ERROR_SUCCESS);
    T_EQ(rt_reg_flush_key(HKEY_LOCAL_MACHINE), ERROR_SUCCESS);
    T_EQ(rt_reg_flush_key(HKEY_CURRENT_USER), ERROR_SUCCESS);
    cl(k);
}

/* ---- names ------------------------------------------------------------------------------------ */

TEST(core, registry_key_name_case)
{
    T_CASE("key names compare ignoring case and keep the case they were created with");
    HKEY k = mk(HKEY_CURRENT_USER, "Software\\MixedCase");
    cl(k);
    HKEY same = NULL;
    DWORD disp = 0;
    T_EQ(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("SOFTWARE\\MIXEDCASE"), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &same, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_OPENED_EXISTING_KEY);
    cl(same);
    T_TRUE(has_key(HKEY_CURRENT_USER, "software\\mixedcase"));
    T_TRUE(has_key(HKEY_CURRENT_USER, "sOfTwArE\\mIxEdCaSe"));

    HKEY sw = op(HKEY_CURRENT_USER, "Software", KEY_READ);
    char name[64];
    T_TRUE(key_name(sw, 0, name, sizeof name));
    T_TRUE(!strcmp(name, "MixedCase"));                     /* not "MIXEDCASE": the first spelling stays */
    DWORD nkeys = 0;
    counts(sw, &nkeys, NULL);
    T_EQ(nkeys, 1);                                         /* and there is one key, not two */
    cl(sw);

    T_CASE("case folds the non-ASCII letters of Latin, Greek and Cyrillic too");
    static const WCHAR lat_lo[] = { 0xFC, 'n', 0xEF, 0 }, lat_up[] = { 0xDC, 'N', 0xCF, 0 };
    static const WCHAR gr_lo[] = { 0x3C3, 0x3B1, 0 }, gr_up[] = { 0x3A3, 0x391, 0 };     /* sigma alpha */
    static const WCHAR cy_lo[] = { 0x434, 0x43E, 0x43C, 0 }, cy_up[] = { 0x414, 0x41E, 0x41C, 0 };
    const WCHAR *pairs[][2] = { { lat_lo, lat_up }, { gr_lo, gr_up }, { cy_lo, cy_up } };
    HKEY root = mk(HKEY_CURRENT_USER, "Software\\Unicode");
    for (unsigned i = 0; i < 3; i++) {
        HKEY a = NULL, b = NULL;
        DWORD d1 = 0, d2 = 0;
        T_EQ(rt_reg_create_key_ex_w(root, pairs[i][0], 0, NULL, 0, KEY_ALL_ACCESS, NULL, &a, &d1), ERROR_SUCCESS);
        T_EQ(rt_reg_create_key_ex_w(root, pairs[i][1], 0, NULL, 0, KEY_ALL_ACCESS, NULL, &b, &d2), ERROR_SUCCESS);
        T_EQ(d1, REG_CREATED_NEW_KEY);
        T_EQ(d2, REG_OPENED_EXISTING_KEY);
        cl(a); cl(b);
    }
    DWORD n = 0;
    counts(root, &n, NULL);
    T_EQ(n, 3);
    cl(root);
}

TEST(core, registry_name_boundaries)
{
    T_CASE("a name that differs by one character, or by a trailing space, is another name");
    HKEY r = mk(HKEY_CURRENT_USER, "Software\\Edge");
    HKEY a = mk(r, "name");
    HKEY b = mk(r, "name ");
    HKEY c = mk(r, " name");
    HKEY d = mk(r, "names");
    DWORD n = 0;
    counts(r, &n, NULL);
    T_EQ(n, 4);
    cl(a); cl(b); cl(c); cl(d);

    T_CASE("ß and SS, and a letter and its accented form, are different names");
    static const WCHAR sharp_s[] = { 0xDF, 0 }, ss[] = { 'S', 'S', 0 }, e_acute[] = { 0xE9, 0 }, e[] = { 'E', 0 };
    HKEY x = NULL;
    DWORD disp;
    const WCHAR *names[] = { sharp_s, ss, e_acute, e };
    for (unsigned i = 0; i < 4; i++) {
        T_EQ(rt_reg_create_key_ex_w(r, names[i], 0, NULL, 0, KEY_ALL_ACCESS, NULL, &x, &disp), ERROR_SUCCESS);
        T_EQ(disp, REG_CREATED_NEW_KEY);
        cl(x);
    }

    T_CASE("characters outside the Basic Multilingual Plane and unpaired surrogates are names like any other");
    static const WCHAR astral[] = { 0xD83D, 0xDE00, 0 }, lone[] = { 0xD800, 'x', 0 };
    T_EQ(rt_reg_create_key_ex_w(r, astral, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &x, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_CREATED_NEW_KEY);
    cl(x);
    T_EQ(rt_reg_create_key_ex_w(r, astral, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &x, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_OPENED_EXISTING_KEY);
    cl(x);
    T_EQ(rt_reg_create_key_ex_w(r, lone, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &x, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_CREATED_NEW_KEY);
    cl(x);
    cl(r);
}

TEST(core, registry_name_limits)
{
    T_CASE("a key name of 255 characters is accepted; 256 stops the runtime");
    char name[300];
    memset(name, 'k', 255); name[255] = 0;
    HKEY k = mk(HKEY_CURRENT_USER, name);
    cl(k);
    T_TRUE(has_key(HKEY_CURRENT_USER, name));
    memset(name, 'k', 256); name[256] = 0;
    HKEY x = NULL;
    T_TRAPS(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W(name), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &x, NULL), "255");
    T_TRAPS(rt_reg_open_key_ex_w(HKEY_CURRENT_USER, W(name), 0, KEY_READ, &x), "255");

    T_CASE("a subkey string of 32,767 characters is accepted; 32,769 stops the runtime");
    static WCHAR path[40000];
    size_t n = 0;
    for (int comp = 0; comp < 128; comp++) {
        if (comp) path[n++] = '\\';
        for (int i = 0; i < 255; i++) path[n++] = (WCHAR)('a' + (comp % 26));
    }
    path[n] = 0;
    T_EQ(n, 32767);
    T_EQ(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, path, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &x, NULL), ERROR_SUCCESS);
    cl(x);
    T_EQ(rt_reg_open_key_ex_w(HKEY_CURRENT_USER, path, 0, KEY_READ, &x), ERROR_SUCCESS);
    cl(x);
    path[n++] = '\\'; path[n++] = 'x'; path[n] = 0;
    T_TRAPS(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, path, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &x, NULL), "32767");
}

TEST(core, registry_name_shapes)
{
    HKEY k = NULL, k2 = NULL;
    DWORD disp = 0;

    T_CASE("a trailing backslash, or several, is ignored by open and create: the key is the one without it");
    T_EQ(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("Software\\ShapeT\\"), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_CREATED_NEW_KEY);
    cl(k);
    T_EQ(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("Software\\ShapeT\\\\"), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_OPENED_EXISTING_KEY);
    cl(k);
    T_EQ(rt_reg_open_key_ex_w(HKEY_CURRENT_USER, W("Software\\ShapeT\\"), 0, KEY_READ, &k), ERROR_SUCCESS);
    cl(k);
    T_EQ(rt_reg_open_key_ex_a(HKEY_CURRENT_USER, "Software\\ShapeT", 0, KEY_READ, &k2), ERROR_SUCCESS);
    cl(k2);

    T_CASE("a leading backslash is ERROR_BAD_PATHNAME for open and create, and nothing is opened or made");
    k = NULL;
    T_EQ(rt_reg_open_key_ex_w(HKEY_CURRENT_USER, W("\\Software"), 0, KEY_READ, &k), ERROR_BAD_PATHNAME);
    T_TRUE(k == NULL);
    T_EQ(rt_reg_open_key_ex_a(HKEY_CURRENT_USER, "\\Software", 0, KEY_READ, &k), ERROR_BAD_PATHNAME);
    T_TRUE(k == NULL);
    T_EQ(rt_reg_open_key_ex_w(HKEY_LOCAL_MACHINE, W("\\SOFTWARE"), 0, KEY_READ, &k), ERROR_BAD_PATHNAME);
    T_EQ(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("\\Software\\ShapeBad"), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp), ERROR_BAD_PATHNAME);
    T_EQ(rt_reg_open_key_ex_w(HKEY_CURRENT_USER, W("Software\\ShapeBad"), 0, KEY_READ, &k), ERROR_FILE_NOT_FOUND);

    T_CASE("a leading backslash is skipped under HKEY_CLASSES_ROOT, whose keys are those of Software\\Classes");
    T_EQ(rt_reg_create_key_ex_w(HKEY_CLASSES_ROOT, W("\\ShapeClass"), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_CREATED_NEW_KEY);
    cl(k);
    T_EQ(rt_reg_open_key_ex_w(HKEY_CLASSES_ROOT, W("ShapeClass"), 0, KEY_READ, &k), ERROR_SUCCESS);
    cl(k);
    T_EQ(rt_reg_open_key_ex_w(HKEY_CLASSES_ROOT, W("\\ShapeClass"), 0, KEY_READ, &k), ERROR_SUCCESS);
    cl(k);
    T_EQ(rt_reg_open_key_ex_w(HKEY_LOCAL_MACHINE, W("SOFTWARE\\Classes\\ShapeClass"), 0, KEY_READ, &k), ERROR_SUCCESS);
    cl(k);

    T_CASE("two backslashes together stop the runtime and name the string; so does a backslash at either end of a delete");
    T_TRAPS(rt_reg_open_key_ex_w(HKEY_CURRENT_USER, W("Software\\\\Foo"), 0, KEY_READ, &k), "two backslashes together");
    T_TRAPS(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("Software\\\\Foo"), 0, NULL, 0, KEY_READ, NULL, &k, NULL), "two backslashes together");
    T_TRAPS(rt_reg_open_key_ex_w(HKEY_CLASSES_ROOT, W("\\\\ShapeClass"), 0, KEY_READ, &k), "two backslashes together");
    T_TRAPS(rt_reg_delete_key_ex_w(HKEY_CURRENT_USER, W("Software\\"), 0, 0), "trailing backslash");
    T_TRAPS(rt_reg_delete_key_ex_w(HKEY_CURRENT_USER, W("\\Software"), 0, 0), "leading backslash");

    T_CASE("a backslash below a handle that is itself under HKEY_CLASSES_ROOT, and backslashes only under it, stop the runtime");
    T_EQ(rt_reg_open_key_ex_w(HKEY_CLASSES_ROOT, W("ShapeClass"), 0, KEY_READ, &k), ERROR_SUCCESS);
    T_TRAPS(rt_reg_open_key_ex_w(k, W("\\Sub"), 0, KEY_READ, &k2), "below a handle to a key of HKEY_CLASSES_ROOT");
    T_TRAPS(rt_reg_open_key_ex_w(HKEY_CLASSES_ROOT, W("\\"), 0, KEY_READ, &k2), "backslashes only under HKEY_CLASSES_ROOT");
    cl(k);

    T_CASE("a name with a character whose case the locale tables do not cover stops the runtime");
    static const WCHAR odd[] = { 'a', 0x1E00, 0 };          /* LATIN CAPITAL LETTER A WITH RING BELOW */
    T_TRAPS(rt_reg_open_key_ex_w(HKEY_CURRENT_USER, odd, 0, KEY_READ, &k), "U+1E00");
    T_TRAPS(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, odd, 0, NULL, 0, KEY_READ, NULL, &k, NULL), "U+1E00");
    HKEY h = mk(HKEY_CURRENT_USER, "Software\\Odd");
    T_TRAPS(rt_reg_set_value_ex_w(h, odd, 0, REG_DWORD, (const BYTE *)"\0\0\0\0", 4), "U+1E00");
    T_TRAPS(rt_reg_query_value_ex_w(h, odd, NULL, NULL, NULL, NULL), "U+1E00");
    cl(h);
}

TEST(core, registry_options_and_security)
{
    HKEY k = NULL;
    T_CASE("open takes options 0 and REG_OPTION_OPEN_LINK; anything else stops the runtime");
    HKEY made = mk(HKEY_CURRENT_USER, "Software\\Opts");
    cl(made);
    T_EQ(rt_reg_open_key_ex_w(HKEY_CURRENT_USER, W("Software\\Opts"), REG_OPTION_OPEN_LINK, KEY_READ, &k), ERROR_SUCCESS);
    cl(k);
    T_TRAPS(rt_reg_open_key_ex_w(HKEY_CURRENT_USER, W("Software\\Opts"), 1, KEY_READ, &k), "ulOptions 0x1");
    T_TRAPS(rt_reg_open_key_ex_w(HKEY_CURRENT_USER, W("Software\\Opts"), REG_OPTION_BACKUP_RESTORE, KEY_READ, &k), "ulOptions 0x4");

    T_CASE("create takes VOLATILE and OPEN_LINK; links and backup/restore stop the runtime");
    T_EQ(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("Software\\Opts2"), 0, NULL, REG_OPTION_VOLATILE, KEY_READ, NULL, &k, NULL), ERROR_SUCCESS);
    cl(k);
    T_EQ(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("Software\\Opts3"), 0, NULL, REG_OPTION_OPEN_LINK, KEY_READ, NULL, &k, NULL), ERROR_SUCCESS);
    cl(k);
    T_EQ(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("Software\\Opts4"), 0, NULL, REG_OPTION_VOLATILE | REG_OPTION_OPEN_LINK, KEY_READ, NULL, &k, NULL), ERROR_SUCCESS);
    cl(k);
    T_TRAPS(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("Software\\Opts5"), 0, NULL, REG_OPTION_CREATE_LINK, KEY_READ, NULL, &k, NULL), "dwOptions 0x2");
    T_TRAPS(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("Software\\Opts5"), 0, NULL, REG_OPTION_BACKUP_RESTORE, KEY_READ, NULL, &k, NULL), "dwOptions 0x4");
    T_TRAPS(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("Software\\Opts5"), 0, NULL, 0x10, KEY_READ, NULL, &k, NULL), "dwOptions 0x10");

    T_CASE("the reserved argument of create must be 0");
    T_TRAPS(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("Software\\Opts5"), 1, NULL, 0, KEY_READ, NULL, &k, NULL), "Reserved is 0x1");

    T_CASE("security attributes without a descriptor are accepted; a descriptor stops the runtime");
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    T_EQ(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("Software\\Opts6"), 0, NULL, 0, KEY_READ, &sa, &k, NULL), ERROR_SUCCESS);
    cl(k);
    sa.lpSecurityDescriptor = &sa;
    T_TRAPS(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("Software\\Opts7"), 0, NULL, 0, KEY_READ, &sa, &k, NULL), "security descriptor");
    T_TRUE(!has_key(HKEY_CURRENT_USER, "Software\\Opts7"));
}

TEST(core, registry_create_class)
{
    T_CASE("the class of a new key is kept; the class of an existing key is not replaced");
    HKEY k = NULL;
    DWORD disp = 0;
    T_EQ(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("Software\\Classy"), 0, W("First"), 0, KEY_ALL_ACCESS, NULL, &k, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_CREATED_NEW_KEY);
    cl(k);
    T_EQ(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("Software\\Classy"), 0, W("Second"), 0, KEY_ALL_ACCESS, NULL, &k, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_OPENED_EXISTING_KEY);
    cl(k);
    HKEY sw = op(HKEY_CURRENT_USER, "Software", KEY_READ);
    WCHAR name[32], cls[32];
    DWORD cn = 32, cc = 32;
    T_EQ(rt_reg_enum_key_ex_w(sw, 0, name, &cn, NULL, cls, &cc, NULL), ERROR_SUCCESS);
    T_TRUE(weq(name, "Classy") && weq(cls, "First") && cc == 5);
    cl(sw);

    T_CASE("only the last key of a deep path gets the class");
    T_EQ(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("Software\\P\\Q"), 0, W("Leaf"), 0, KEY_ALL_ACCESS, NULL, &k, NULL), ERROR_SUCCESS);
    cl(k);
    HKEY p = op(HKEY_CURRENT_USER, "Software\\P", KEY_READ);
    cn = 32; cc = 32;
    T_EQ(rt_reg_enum_key_ex_w(p, 0, name, &cn, NULL, cls, &cc, NULL), ERROR_SUCCESS);
    T_TRUE(weq(name, "Q") && weq(cls, "Leaf"));
    cl(p);
    HKEY sw2 = op(HKEY_CURRENT_USER, "Software", KEY_READ);
    DWORD i;
    for (i = 0; i < 4; i++) {
        cn = 32; cc = 32;
        if (rt_reg_enum_key_ex_w(sw2, i, name, &cn, NULL, cls, &cc, NULL) != ERROR_SUCCESS) break;
        if (weq(name, "P")) { T_TRUE(cc == 0 && cls[0] == 0); }
    }
    cl(sw2);
}
