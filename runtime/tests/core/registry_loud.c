/* The registry (rt/registry.h): what is logged, and what stops the runtime.  A key or value that is
 * not there is logged once per name, so that the first runs show what the game asks for; a case the
 * registry does not model ends in a report that names the Windows function and the value. */
#include "registry_util.c"

#define SENTINEL_KEY ((HKEY)0x77)

/* ---- logged once per name ---------------------------------------------------------------------- */

TEST(core, registry_missing_key_logged_once)
{
    HKEY k = SENTINEL_KEY;
    cl(mk(HKEY_CURRENT_USER, "Software"));
    unsigned base = T_LOG_COUNT(RT_CH_REGISTRY);

    T_CASE("a missing key is logged the first time it is asked for, with the path as asked");
    T_EQ(open_st(HKEY_CURRENT_USER, "Software\\Ghost", KEY_READ, &k), ERROR_FILE_NOT_FOUND);
    T_EQ(T_LOG_COUNT(RT_CH_REGISTRY), base + 1);
    T_TRUE(T_LOGGED(RT_CH_REGISTRY, "RegOpenKeyExW: key not found: HKEY_CURRENT_USER\\Software\\Ghost"));

    T_CASE("and not again: not by the same call, another spelling, the A function, an open key plus a relative path");
    T_EQ(open_st(HKEY_CURRENT_USER, "Software\\Ghost", KEY_READ, &k), ERROR_FILE_NOT_FOUND);
    T_EQ(open_st(HKEY_CURRENT_USER, "SOFTWARE\\GHOST", KEY_READ, &k), ERROR_FILE_NOT_FOUND);
    T_EQ(open_st((HKEY)(uintptr_t)0x80000001u, "software\\ghost", KEY_READ, &k), ERROR_FILE_NOT_FOUND);
    T_EQ(rt_reg_open_key_ex_a(HKEY_CURRENT_USER, "Software\\ghost", 0, KEY_READ, &k), ERROR_FILE_NOT_FOUND);
    HKEY sw = op(HKEY_CURRENT_USER, "Software", KEY_READ);
    T_EQ(open_st(sw, "Ghost", KEY_READ, &k), ERROR_FILE_NOT_FOUND);
    T_EQ(rt_reg_delete_key_ex_w(sw, W("GHOST"), 0, 0), ERROR_FILE_NOT_FOUND);
    T_TRUE(T_LOG_COUNT(RT_CH_REGISTRY) == base + 1 + 1 /* the successful open of Software, logged at debug level */);

    T_CASE("another path is logged on its own, and so is a deeper one below the same missing key");
    unsigned before = T_LOG_COUNT(RT_CH_REGISTRY);
    T_EQ(open_st(HKEY_CURRENT_USER, "Software\\Ghost2", KEY_READ, &k), ERROR_FILE_NOT_FOUND);
    T_EQ(open_st(HKEY_CURRENT_USER, "Software\\Ghost\\Deeper", KEY_READ, &k), ERROR_FILE_NOT_FOUND);
    T_EQ(T_LOG_COUNT(RT_CH_REGISTRY), before + 2);
    T_TRUE(T_LOGGED(RT_CH_REGISTRY, "HKEY_CURRENT_USER\\Software\\Ghost\\Deeper"));

    T_CASE("the same name below another root is another name");
    before = T_LOG_COUNT(RT_CH_REGISTRY);
    T_EQ(open_st(HKEY_LOCAL_MACHINE, "Software\\Ghost", KEY_READ, &k), ERROR_FILE_NOT_FOUND);
    T_EQ(T_LOG_COUNT(RT_CH_REGISTRY), before + 1);
    T_TRUE(T_LOGGED(RT_CH_REGISTRY, "key not found: HKEY_LOCAL_MACHINE\\Software\\Ghost"));
    cl(sw);

    T_CASE("the view HKEY_CLASSES_ROOT is logged under the key it is a view of");
    T_EQ(open_st(HKEY_CLASSES_ROOT, ".bsp", KEY_READ, &k), ERROR_FILE_NOT_FOUND);
    T_TRUE(T_LOGGED(RT_CH_REGISTRY, "HKEY_LOCAL_MACHINE\\SOFTWARE\\Classes\\.bsp"));
    T_TRUE(k == SENTINEL_KEY);
}

TEST(core, registry_missing_value_logged_once)
{
    HKEY a = mk(HKEY_CURRENT_USER, "Software\\ValueLog");
    HKEY b = mk(HKEY_CURRENT_USER, "Software\\ValueLog2");
    unsigned base = T_LOG_COUNT(RT_CH_REGISTRY);

    T_CASE("a missing value is logged once per key and name, whatever the case and whichever function asks");
    T_EQ(rt_reg_query_value_ex_w(a, W("Nope"), NULL, NULL, NULL, NULL), ERROR_FILE_NOT_FOUND);
    T_EQ(T_LOG_COUNT(RT_CH_REGISTRY), base + 1);
    T_TRUE(T_LOGGED(RT_CH_REGISTRY, "RegQueryValueExW: value not found: HKEY_CURRENT_USER\\Software\\ValueLog value \"Nope\""));
    T_EQ(rt_reg_query_value_ex_w(a, W("Nope"), NULL, NULL, NULL, NULL), ERROR_FILE_NOT_FOUND);
    T_EQ(rt_reg_query_value_ex_w(a, W("NOPE"), NULL, NULL, NULL, NULL), ERROR_FILE_NOT_FOUND);
    T_EQ(rt_reg_query_value_ex_a(a, "nope", NULL, NULL, NULL, NULL), ERROR_FILE_NOT_FOUND);
    T_EQ(rt_reg_delete_value_w(a, W("nOpE")), ERROR_FILE_NOT_FOUND);
    T_EQ(T_LOG_COUNT(RT_CH_REGISTRY), base + 1);

    T_CASE("another name, or the same name in another key, is logged");
    T_EQ(rt_reg_query_value_ex_w(a, W("Other"), NULL, NULL, NULL, NULL), ERROR_FILE_NOT_FOUND);
    T_EQ(rt_reg_query_value_ex_w(b, W("Nope"), NULL, NULL, NULL, NULL), ERROR_FILE_NOT_FOUND);
    T_EQ(T_LOG_COUNT(RT_CH_REGISTRY), base + 3);
    T_TRUE(T_LOGGED(RT_CH_REGISTRY, "HKEY_CURRENT_USER\\Software\\ValueLog2 value \"Nope\""));

    T_CASE("the default value is logged as such, by the NULL name and the empty name alike");
    T_EQ(rt_reg_query_value_ex_w(a, NULL, NULL, NULL, NULL, NULL), ERROR_FILE_NOT_FOUND);
    T_EQ(rt_reg_query_value_ex_w(a, W(""), NULL, NULL, NULL, NULL), ERROR_FILE_NOT_FOUND);
    T_EQ(rt_reg_delete_value_a(a, ""), ERROR_FILE_NOT_FOUND);
    T_EQ(T_LOG_COUNT(RT_CH_REGISTRY), base + 4);
    T_TRUE(T_LOGGED(RT_CH_REGISTRY, "HKEY_CURRENT_USER\\Software\\ValueLog value (Default)"));

    T_CASE("a missing key and a missing value of the same name do not hide each other");
    unsigned before = T_LOG_COUNT(RT_CH_REGISTRY);
    HKEY k = SENTINEL_KEY;
    T_EQ(open_st(a, "Nope", KEY_READ, &k), ERROR_FILE_NOT_FOUND);
    T_EQ(T_LOG_COUNT(RT_CH_REGISTRY), before + 1);

    T_CASE("a value written afterwards is not 'missing' any more, and deleting it and asking again is not logged twice");
    T_EQ(put_dw(a, "Later", 1), ERROR_SUCCESS);
    T_TRUE(is_dw(a, "Later", 1));
    T_EQ(rt_reg_delete_value_w(a, W("Later")), ERROR_SUCCESS);
    before = T_LOG_COUNT(RT_CH_REGISTRY);
    T_EQ(rt_reg_query_value_ex_w(a, W("Later"), NULL, NULL, NULL, NULL), ERROR_FILE_NOT_FOUND);
    T_EQ(T_LOG_COUNT(RT_CH_REGISTRY), before + 1);
    T_EQ(rt_reg_query_value_ex_w(a, W("Later"), NULL, NULL, NULL, NULL), ERROR_FILE_NOT_FOUND);
    T_EQ(T_LOG_COUNT(RT_CH_REGISTRY), before + 1);
    cl(a); cl(b);
}

TEST(core, registry_log_survives_restart_of_the_tree)
{
    T_CASE("a name already logged is not logged again after the tree is read again");
    HKEY k = SENTINEL_KEY;
    T_EQ(open_st(HKEY_CURRENT_USER, "Gone", KEY_READ, &k), ERROR_FILE_NOT_FOUND);
    unsigned n = T_LOG_COUNT(RT_CH_REGISTRY);
    rt_registry_test_reload();
    T_EQ(open_st(HKEY_CURRENT_USER, "Gone", KEY_READ, &k), ERROR_FILE_NOT_FOUND);
    T_EQ(T_LOG_COUNT(RT_CH_REGISTRY), n);
}

TEST(core, registry_log_shows_what_is_found)
{
    T_CASE("at debug level an open and a query that find something are logged too");
    HKEY k = mk(HKEY_CURRENT_USER, "Software\\Found");
    T_EQ(put_dw(k, "v", 1), ERROR_SUCCESS);
    HKEY o = op(HKEY_CURRENT_USER, "Software\\Found", KEY_READ);
    T_TRUE(T_LOGGED(RT_CH_REGISTRY, "RegOpenKeyExW: opened HKEY_CURRENT_USER\\Software\\Found"));
    T_TRUE(is_dw(o, "v", 1));
    T_TRUE(T_LOGGED(RT_CH_REGISTRY, "RegQueryValueExW: HKEY_CURRENT_USER\\Software\\Found value \"v\": type 4, 4 bytes"));
    cl(o); cl(k);
}

/* ---- the report names the function and the value --------------------------------------------------- */

TEST(core, registry_reports_name_function_and_value)
{
    T_CASE("each report starts with FATAL unimplemented and names the Windows function and what was asked");
    HKEY k = SENTINEL_KEY;
    T_TRAPS(rt_reg_open_key_ex_w(HKEY_CURRENT_USER, W("a"), 0x4, KEY_READ, &k), "RegOpenKeyExW: ulOptions 0x4");
    T_TRAPS(rt_reg_open_key_ex_a(HKEY_CURRENT_USER, "a", 0x4, KEY_READ, &k), "RegOpenKeyExA: ulOptions 0x4");
    T_TRAPS(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("a"), 0, NULL, REG_OPTION_CREATE_LINK, KEY_READ, NULL, &k, NULL), "RegCreateKeyExW: dwOptions 0x2");
    T_TRAPS(rt_reg_create_key_ex_a(HKEY_CURRENT_USER, "a", 0, NULL, REG_OPTION_CREATE_LINK, KEY_READ, NULL, &k, NULL), "RegCreateKeyExA: dwOptions 0x2");
    HKEY h = mk(HKEY_CURRENT_USER, "Software\\Loud");
    T_TRAPS(rt_reg_set_value_ex_w(h, W("v"), 5, REG_DWORD, (const BYTE *)"\0\0\0\0", 4), "RegSetValueExW: Reserved is 0x5");
    T_TRAPS(rt_reg_set_value_ex_a(h, "v", 5, REG_DWORD, (const BYTE *)"\0\0\0\0", 4), "RegSetValueExA: Reserved is 0x5");
    T_TRAPS(rt_reg_delete_key_ex_a(h, "", 0, 0), "RegDeleteKeyExA: an empty subkey");
    T_TRAPS(rt_reg_open_key_ex_w(h, W("a\\\\b"), 0, KEY_READ, &k), "\"a\\\\b\"");          /* the string, escaped, is in the report */
    cl(h);

    T_CASE("the report is a FATAL block with a thread line");
    T_TRAPS(rt_reg_open_key_ex_w(HKEY_PERFORMANCE_DATA, W("a"), 0, KEY_READ, &k), "FATAL unimplemented");
}

TEST(core, registry_init_twice_and_hooks)
{
    T_CASE("starting the registry again is a bug and stops the runtime");
    T_TRAPS(rt_registry_init(), "rt_registry_init called twice");
}

TEST(core, registry_delete_misses_are_logged)
{
    T_CASE("a key or value that a delete asks for and is not there is logged, with the name of the function that asked");
    HKEY k = mk(HKEY_CURRENT_USER, "Software\\DelLog");
    T_EQ(rt_reg_delete_key_ex_w(HKEY_CURRENT_USER, W("Software\\NeverBuilt"), 0, 0), ERROR_FILE_NOT_FOUND);
    T_TRUE(T_LOGGED(RT_CH_REGISTRY, "RegDeleteKeyExW: key not found: HKEY_CURRENT_USER\\Software\\NeverBuilt"));
    T_EQ(rt_reg_delete_key_ex_a(k, "NeverBuiltA", 0, 0), ERROR_FILE_NOT_FOUND);
    T_TRUE(T_LOGGED(RT_CH_REGISTRY, "RegDeleteKeyExA: key not found: HKEY_CURRENT_USER\\Software\\DelLog\\NeverBuiltA"));
    T_EQ(rt_reg_delete_value_w(k, W("NeverSet")), ERROR_FILE_NOT_FOUND);
    T_TRUE(T_LOGGED(RT_CH_REGISTRY, "RegDeleteValueW: value not found: HKEY_CURRENT_USER\\Software\\DelLog value \"NeverSet\""));
    T_EQ(rt_reg_delete_value_a(k, "NeverSetA"), ERROR_FILE_NOT_FOUND);
    T_TRUE(T_LOGGED(RT_CH_REGISTRY, "RegDeleteValueA: value not found: HKEY_CURRENT_USER\\Software\\DelLog value \"NeverSetA\""));

    T_CASE("the path of a deeper miss is logged whole, with the intermediate keys that do exist");
    T_EQ(rt_reg_delete_key_ex_w(k, W("A\\B\\C"), 0, 0), ERROR_FILE_NOT_FOUND);
    T_TRUE(T_LOGGED(RT_CH_REGISTRY, "HKEY_CURRENT_USER\\Software\\DelLog\\A\\B\\C"));
    cl(k);
}
