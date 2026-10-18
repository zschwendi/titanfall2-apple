/* The registry (rt/registry.h): access rights, the two WOW64 views, deleting keys, deleted keys with
 * open handles, volatile keys, the roots that take no values, duplicated handles, and the
 * predefined views HKEY_CLASSES_ROOT and HKEY_CURRENT_CONFIG.  What is behind these: RegOpenKeyEx's
 * samDesired, RegDeleteKey(Ex), RegCreateKeyEx's REG_OPTION_VOLATILE and DuplicateHandle on a key. */
#include "registry_util.c"
#include "rt/handle.h"

/* ---- access rights ----------------------------------------------------------------------------- */

static HKEY with_value(const char *path) {
    HKEY k = mk(HKEY_CURRENT_USER, path);
    T_EQ(put_dw(k, "v", 5), ERROR_SUCCESS);
    cl(mk(k, "child"));
    return k;
}

TEST(core, registry_access_rights)
{
    HKEY full = with_value("Software\\Access");
    DWORD v = 1, cb = 4, n = 0;
    WCHAR name[16];
    DWORD cch = 16;

    T_CASE("KEY_QUERY_VALUE alone reads values and the key's info, and nothing else");
    HKEY q = op(HKEY_CURRENT_USER, "Software\\Access", KEY_QUERY_VALUE);
    T_EQ(rt_reg_query_value_ex_w(q, W("v"), NULL, NULL, (BYTE *)&v, &cb), ERROR_SUCCESS);
    T_EQ(rt_reg_query_info_key_w(q, NULL, NULL, NULL, &n, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ERROR_SUCCESS);
    T_EQ(rt_reg_enum_value_w(q, 0, name, &cch, NULL, NULL, NULL, NULL), ERROR_SUCCESS);
    T_EQ(put_dw(q, "v", 2), ERROR_ACCESS_DENIED);
    T_EQ(rt_reg_delete_value_w(q, W("v")), ERROR_ACCESS_DENIED);
    cch = 16;
    T_EQ(rt_reg_enum_key_ex_w(q, 0, name, &cch, NULL, NULL, NULL, NULL), ERROR_ACCESS_DENIED);
    cl(q);

    T_CASE("KEY_SET_VALUE alone writes and deletes values, and nothing else");
    HKEY s = op(HKEY_CURRENT_USER, "Software\\Access", KEY_SET_VALUE);
    T_EQ(put_dw(s, "v", 3), ERROR_SUCCESS);
    T_EQ(put_dw(s, "w", 4), ERROR_SUCCESS);
    T_EQ(rt_reg_delete_value_w(s, W("w")), ERROR_SUCCESS);
    cb = 4;
    T_EQ(rt_reg_query_value_ex_w(s, W("v"), NULL, NULL, (BYTE *)&v, &cb), ERROR_ACCESS_DENIED);
    T_EQ(rt_reg_query_value_ex_w(s, W("v"), NULL, NULL, NULL, NULL), ERROR_ACCESS_DENIED);
    T_EQ(rt_reg_query_info_key_w(s, NULL, NULL, NULL, &n, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ERROR_ACCESS_DENIED);
    cch = 16;
    T_EQ(rt_reg_enum_value_w(s, 0, name, &cch, NULL, NULL, NULL, NULL), ERROR_ACCESS_DENIED);
    cl(s);

    T_CASE("KEY_ENUMERATE_SUB_KEYS alone lists the subkeys, and nothing else");
    HKEY e = op(HKEY_CURRENT_USER, "Software\\Access", KEY_ENUMERATE_SUB_KEYS);
    cch = 16;
    T_EQ(rt_reg_enum_key_ex_w(e, 0, name, &cch, NULL, NULL, NULL, NULL), ERROR_SUCCESS);
    T_EQ(rt_reg_query_info_key_w(e, NULL, NULL, NULL, &n, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ERROR_ACCESS_DENIED);
    cb = 4;
    T_EQ(rt_reg_query_value_ex_w(e, W("v"), NULL, NULL, (BYTE *)&v, &cb), ERROR_ACCESS_DENIED);
    T_EQ(put_dw(e, "v", 2), ERROR_ACCESS_DENIED);
    cl(e);

    T_CASE("the rights are checked before the value is looked up: a missing value is still access denied");
    HKEY w = op(HKEY_CURRENT_USER, "Software\\Access", KEY_WRITE);
    T_EQ(rt_reg_query_value_ex_w(w, W("never"), NULL, NULL, NULL, NULL), ERROR_ACCESS_DENIED);
    T_EQ(put_dw(w, "x", 1), ERROR_SUCCESS);
    cl(w);

    T_CASE("samDesired 0 gives a handle that can be closed and nothing else");
    HKEY z = op(HKEY_CURRENT_USER, "Software\\Access", 0);
    cb = 4;
    T_EQ(rt_reg_query_value_ex_w(z, W("v"), NULL, NULL, (BYTE *)&v, &cb), ERROR_ACCESS_DENIED);
    T_EQ(put_dw(z, "v", 2), ERROR_ACCESS_DENIED);
    cch = 16;
    T_EQ(rt_reg_enum_key_ex_w(z, 0, name, &cch, NULL, NULL, NULL, NULL), ERROR_ACCESS_DENIED);
    T_EQ(rt_reg_flush_key(z), ERROR_SUCCESS);
    HKEY sub = NULL;
    T_EQ(rt_reg_open_key_ex_w(z, W("child"), 0, KEY_READ, &sub), ERROR_SUCCESS);   /* opening a child needs no right on the parent */
    cl(sub);
    cl(z);
    cl(full);
}

TEST(core, registry_access_masks)
{
    HKEY full = with_value("Software\\Masks");
    DWORD v = 0, cb = 4;
    WCHAR name[16];
    DWORD cch = 16;

    T_CASE("KEY_READ reads and lists; KEY_WRITE writes; KEY_ALL_ACCESS does both");
    HKEY r = op(HKEY_CURRENT_USER, "Software\\Masks", KEY_READ);
    T_EQ(rt_reg_query_value_ex_w(r, W("v"), NULL, NULL, (BYTE *)&v, &cb), ERROR_SUCCESS);
    T_EQ(rt_reg_enum_key_ex_w(r, 0, name, &cch, NULL, NULL, NULL, NULL), ERROR_SUCCESS);
    T_EQ(put_dw(r, "v", 2), ERROR_ACCESS_DENIED);
    cl(r);
    HKEY w = op(HKEY_CURRENT_USER, "Software\\Masks", KEY_WRITE);
    T_EQ(put_dw(w, "v", 2), ERROR_SUCCESS);
    cb = 4;
    T_EQ(rt_reg_query_value_ex_w(w, W("v"), NULL, NULL, (BYTE *)&v, &cb), ERROR_ACCESS_DENIED);
    cl(w);

    T_CASE("the generic rights map as Windows maps them for keys");
    struct { DWORD sam; bool read, write; } g[] = {
        { GENERIC_READ, true, false }, { GENERIC_EXECUTE, true, false }, { GENERIC_WRITE, false, true },
        { GENERIC_ALL, true, true }, { MAXIMUM_ALLOWED, true, true }, { KEY_EXECUTE, true, false },
        { GENERIC_READ | GENERIC_WRITE, true, true }, { KEY_QUERY_VALUE | KEY_SET_VALUE, true, true },
    };
    for (unsigned i = 0; i < sizeof g / sizeof g[0]; i++) {
        HKEY k = op(HKEY_CURRENT_USER, "Software\\Masks", g[i].sam);
        cb = 4; v = 0;
        T_EQ(rt_reg_query_value_ex_w(k, W("v"), NULL, NULL, (BYTE *)&v, &cb), g[i].read ? ERROR_SUCCESS : ERROR_ACCESS_DENIED);
        T_EQ(put_dw(k, "v", 7), g[i].write ? ERROR_SUCCESS : ERROR_ACCESS_DENIED);
        cl(k);
    }

    T_CASE("the WOW64 flags are accepted, alone and together, with any other rights, and name the same tree");
    DWORD flags[] = { KEY_WOW64_32KEY, KEY_WOW64_64KEY, KEY_WOW64_32KEY | KEY_WOW64_64KEY };
    for (unsigned i = 0; i < 3; i++) {
        HKEY k = op(HKEY_CURRENT_USER, "Software\\Masks", KEY_ALL_ACCESS | flags[i]);
        T_EQ(put_dw(k, "v", 100 + i), ERROR_SUCCESS);
        cl(k);
        T_TRUE(is_dw(full, "v", 100 + i));
        HKEY only = op(HKEY_CURRENT_USER, "Software\\Masks", flags[i]);     /* the flag alone: no rights, but accepted */
        cl(only);
        HKEY made = NULL;
        T_EQ(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("Software\\Masks\\child"), 0, NULL, 0, KEY_READ | flags[i], NULL, &made, NULL), ERROR_SUCCESS);
        cl(made);
    }
    T_TRUE(!has_key(HKEY_LOCAL_MACHINE, "SOFTWARE\\Wow6432Node"));     /* nothing is redirected: the name is an ordinary one */
    HKEY wow = NULL;
    DWORD disp = 0;
    T_EQ(rt_reg_create_key_ex_w(HKEY_LOCAL_MACHINE, W("SOFTWARE\\Wow6432Node\\X"), 0, NULL, 0, KEY_ALL_ACCESS | KEY_WOW64_32KEY, NULL, &wow, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_CREATED_NEW_KEY);
    cl(wow);
    T_TRUE(has_key(HKEY_LOCAL_MACHINE, "SOFTWARE\\Wow6432Node\\X"));
    HKEY ok = NULL;
    T_EQ(rt_reg_open_key_ex_w(HKEY_LOCAL_MACHINE, W("SOFTWARE\\Wow6432Node\\X"), 0, KEY_READ | KEY_WOW64_64KEY, &ok), ERROR_SUCCESS);
    cl(ok);

    T_CASE("a bit that is none of the documented rights, and ACCESS_SYSTEM_SECURITY, stop the runtime");
    HKEY k = NULL;
    T_TRAPS(rt_reg_open_key_ex_w(HKEY_CURRENT_USER, W("Software\\Masks"), 0, 0x4000, &k), "0x4000");
    T_TRAPS(rt_reg_open_key_ex_w(HKEY_CURRENT_USER, W("Software\\Masks"), 0, KEY_READ | 0x400, &k), "0x400");
    T_TRAPS(rt_reg_open_key_ex_w(HKEY_CURRENT_USER, W("Software\\Masks"), 0, SYNCHRONIZE, &k), "0x100000");
    T_TRAPS(rt_reg_open_key_ex_w(HKEY_CURRENT_USER, W("Software\\Masks"), 0, ACCESS_SYSTEM_SECURITY, &k), "ACCESS_SYSTEM_SECURITY");
    T_TRAPS(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("Software\\Masks"), 0, NULL, 0, 0x8000, NULL, &k, NULL), "0x8000");
    cl(full);
}

TEST(core, registry_create_needs_no_handle_right)
{
    T_CASE("creating a key through a handle does not look at the handle's rights: Windows checks the parent's ACL");
    HKEY r = mk(HKEY_CURRENT_USER, "Software\\NoRight");
    HKEY ro = op(HKEY_CURRENT_USER, "Software\\NoRight", KEY_READ);
    HKEY k = NULL;
    DWORD disp = 0;
    T_EQ(rt_reg_create_key_ex_w(ro, W("Made"), 0, NULL, 0, KEY_READ, NULL, &k, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_CREATED_NEW_KEY);
    cl(k);
    cl(ro);
    cl(r);
}

/* ---- delete key ------------------------------------------------------------------------------- */

TEST(core, registry_delete_key)
{
    HKEY r = mk(HKEY_CURRENT_USER, "Software\\DelKey");
    cl(mk(r, "a"));
    cl(mk(r, "b\\c"));

    T_CASE("a key without subkeys is deleted, by any spelling of its name; deleting it again is not found");
    T_EQ(rt_reg_delete_key_ex_w(r, W("A"), 0, 0), ERROR_SUCCESS);
    T_TRUE(!has_key(r, "a"));
    T_EQ(rt_reg_delete_key_ex_w(r, W("a"), 0, 0), ERROR_FILE_NOT_FOUND);
    T_EQ(rt_reg_delete_key_ex_w(r, W("nothing"), 0, 0), ERROR_FILE_NOT_FOUND);
    T_EQ(rt_reg_delete_key_ex_w(r, W("nothing\\at\\all"), 0, 0), ERROR_FILE_NOT_FOUND);

    T_CASE("a key with subkeys is ERROR_ACCESS_DENIED and stays; deleting the leaf first allows it");
    T_EQ(rt_reg_delete_key_ex_w(r, W("b"), 0, 0), ERROR_ACCESS_DENIED);
    T_TRUE(has_key(r, "b\\c"));
    T_EQ(rt_reg_delete_key_ex_w(r, W("b\\c"), 0, 0), ERROR_SUCCESS);
    T_TRUE(has_key(r, "b") && !has_key(r, "b\\c"));
    T_EQ(rt_reg_delete_key_ex_w(r, W("b"), 0, 0), ERROR_SUCCESS);

    T_CASE("a key with values and no subkeys is deleted with its values");
    HKEY v = mk(r, "withvalues");
    T_EQ(put_dw(v, "x", 1), ERROR_SUCCESS);
    cl(v);
    T_EQ(rt_reg_delete_key_ex_w(r, W("withvalues"), 0, 0), ERROR_SUCCESS);
    HKEY again = mk(r, "withvalues");
    T_TRUE(!has_value(again, "x"));
    cl(again);

    T_CASE("the handle's rights do not matter; a NULL subkey is invalid; the WOW64 flags are accepted");
    HKEY zero = op(HKEY_CURRENT_USER, "Software\\DelKey", 0);
    T_EQ(rt_reg_delete_key_ex_w(zero, W("withvalues"), KEY_WOW64_64KEY, 0), ERROR_SUCCESS);
    T_EQ(rt_reg_delete_key_ex_w(zero, NULL, 0, 0), ERROR_INVALID_PARAMETER);
    cl(zero);

    T_CASE("an empty subkey, other flags, and a nonzero reserved argument stop the runtime");
    T_TRAPS(rt_reg_delete_key_ex_w(r, W(""), 0, 0), "empty subkey");
    T_TRAPS(rt_reg_delete_key_ex_w(r, W("x"), KEY_READ, 0), "samDesired");
    T_TRAPS(rt_reg_delete_key_ex_w(r, W("x"), 0, 1), "Reserved");
    cl(r);
}

TEST(core, registry_delete_key_handles)
{
    T_CASE("a handle that is open to a deleted key stays valid for closing only; everything else is ERROR_KEY_DELETED");
    HKEY root = mk(HKEY_CURRENT_USER, "Software\\Doomed");
    HKEY k = mk(root, "victim");
    T_EQ(put_dw(k, "v", 1), ERROR_SUCCESS);
    HKEY dup = op(root, "victim", KEY_ALL_ACCESS);
    T_EQ(rt_reg_delete_key_ex_w(root, W("victim"), 0, 0), ERROR_SUCCESS);

    DWORD v = 0, cb = 4, n = 0;
    WCHAR name[8];
    DWORD cch = 8;
    HKEY sub = NULL;
    HKEY hs[] = { k, dup };
    for (int i = 0; i < 2; i++) {
        cb = 4;
        T_EQ(rt_reg_query_value_ex_w(hs[i], W("v"), NULL, NULL, (BYTE *)&v, &cb), ERROR_KEY_DELETED);
        T_EQ(put_dw(hs[i], "v", 2), ERROR_KEY_DELETED);
        T_EQ(rt_reg_delete_value_w(hs[i], W("v")), ERROR_KEY_DELETED);
        cch = 8;
        T_EQ(rt_reg_enum_key_ex_w(hs[i], 0, name, &cch, NULL, NULL, NULL, NULL), ERROR_KEY_DELETED);
        cch = 8;
        T_EQ(rt_reg_enum_value_w(hs[i], 0, name, &cch, NULL, NULL, NULL, NULL), ERROR_KEY_DELETED);
        T_EQ(rt_reg_query_info_key_w(hs[i], NULL, NULL, NULL, &n, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ERROR_KEY_DELETED);
        T_EQ(rt_reg_open_key_ex_w(hs[i], W("x"), 0, KEY_READ, &sub), ERROR_KEY_DELETED);
        T_EQ(rt_reg_create_key_ex_w(hs[i], W("x"), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &sub, NULL), ERROR_KEY_DELETED);
        T_EQ(rt_reg_delete_key_ex_w(hs[i], W("x"), 0, 0), ERROR_KEY_DELETED);
        T_EQ(rt_reg_flush_key(hs[i]), ERROR_KEY_DELETED);
    }
    T_EQ(rt_reg_close_key(k), ERROR_SUCCESS);
    T_EQ(rt_reg_close_key(dup), ERROR_SUCCESS);

    T_CASE("a key made again under the old name is a new key; the old handles do not reach it");
    HKEY again = NULL;
    DWORD disp = 0;
    T_EQ(rt_reg_create_key_ex_w(root, W("victim"), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &again, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_CREATED_NEW_KEY);
    T_TRUE(!has_value(again, "v"));
    cl(again);
    cl(root);
}

TEST(core, registry_deleted_parent_chain)
{
    T_CASE("a deleted key's handle works as far as closing even after its parent is deleted too");
    HKEY a = mk(HKEY_CURRENT_USER, "Software\\Chain\\A");
    HKEY chain = op(HKEY_CURRENT_USER, "Software", KEY_ALL_ACCESS);
    T_EQ(rt_reg_delete_key_ex_w(chain, W("Chain\\A"), 0, 0), ERROR_SUCCESS);
    T_EQ(rt_reg_delete_key_ex_w(chain, W("Chain"), 0, 0), ERROR_SUCCESS);
    DWORD n = 0;
    T_EQ(rt_reg_query_info_key_w(a, NULL, NULL, NULL, &n, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ERROR_KEY_DELETED);
    cl(a);
    cl(chain);
}

TEST(core, registry_delete_key_open_handle_parent)
{
    T_CASE("a key whose subkey a handle keeps open can be deleted only after the subkey is");
    HKEY c = mk(HKEY_CURRENT_USER, "Software\\Pinned\\Child");
    HKEY sw = op(HKEY_CURRENT_USER, "Software", KEY_ALL_ACCESS);
    T_EQ(rt_reg_delete_key_ex_w(sw, W("Pinned"), 0, 0), ERROR_ACCESS_DENIED);       /* still has a subkey, open or not */
    cl(c);
    T_EQ(rt_reg_delete_key_ex_w(sw, W("Pinned"), 0, 0), ERROR_ACCESS_DENIED);
    T_EQ(rt_reg_delete_key_ex_w(sw, W("Pinned\\Child"), 0, 0), ERROR_SUCCESS);
    T_EQ(rt_reg_delete_key_ex_w(sw, W("Pinned"), 0, 0), ERROR_SUCCESS);
    cl(sw);
}

/* ---- volatile keys ------------------------------------------------------------------------------ */

TEST(core, registry_volatile_keys)
{
    T_CASE("a volatile key works like any other while the process lives");
    cl(mk(HKEY_CURRENT_USER, "Software"));                  /* the parent is stable; keys made on the way to a volatile key are volatile */
    HKEY v = NULL;
    DWORD disp = 0;
    T_EQ(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("Software\\Vol"), 0, NULL, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &v, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_CREATED_NEW_KEY);
    T_EQ(put_dw(v, "x", 9), ERROR_SUCCESS);
    T_TRUE(is_dw(v, "x", 9));
    T_TRUE(has_key(HKEY_CURRENT_USER, "Software\\Vol"));

    T_CASE("a key made below a volatile key as non-volatile is ERROR_CHILD_MUST_BE_VOLATILE; a volatile one is fine");
    HKEY c = (HKEY)0x99;
    T_EQ(rt_reg_create_key_ex_w(v, W("Child"), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &c, NULL), ERROR_CHILD_MUST_BE_VOLATILE);
    T_TRUE(c == (HKEY)0x99);
    T_TRUE(!has_key(v, "Child"));
    T_EQ(rt_reg_create_key_ex_w(v, W("Child"), 0, NULL, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &c, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_CREATED_NEW_KEY);
    cl(c);
    T_EQ(rt_reg_create_key_ex_w(v, W("Child"), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &c, &disp), ERROR_SUCCESS);   /* it exists: no new key, no error */
    T_EQ(disp, REG_OPENED_EXISTING_KEY);
    cl(c);

    T_CASE("a deep path whose first missing key is below a volatile key needs the volatile option for all of it");
    T_EQ(rt_reg_create_key_ex_w(v, W("A\\B\\C"), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &c, NULL), ERROR_CHILD_MUST_BE_VOLATILE);
    T_EQ(rt_reg_create_key_ex_w(v, W("A\\B\\C"), 0, NULL, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &c, NULL), ERROR_SUCCESS);
    cl(c);

    T_CASE("the keys made on the way to a volatile key are volatile too");
    T_EQ(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("Software\\VolRoot\\Leaf"), 0, NULL, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &c, NULL), ERROR_SUCCESS);
    cl(c);
    T_EQ(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("Software\\VolRoot\\Sibling"), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &c, NULL), ERROR_CHILD_MUST_BE_VOLATILE);

    T_CASE("the volatile option on a key that exists changes nothing about it");
    HKEY n = mk(HKEY_CURRENT_USER, "Software\\NonVol");
    cl(n);
    T_EQ(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("Software\\NonVol"), 0, NULL, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &n, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_OPENED_EXISTING_KEY);
    HKEY under = NULL;
    T_EQ(rt_reg_create_key_ex_w(n, W("StillStable"), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &under, NULL), ERROR_SUCCESS);    /* its parent is not volatile */
    cl(under);
    cl(n);
    cl(v);
}

/* ---- roots ---------------------------------------------------------------------------------------- */

TEST(core, registry_root_keys)
{
    T_CASE("HKEY_LOCAL_MACHINE and HKEY_USERS take no values and no new direct subkeys");
    DWORD x = 1;
    T_EQ(rt_reg_set_value_ex_w(HKEY_LOCAL_MACHINE, W("v"), 0, REG_DWORD, (const BYTE *)&x, 4), ERROR_ACCESS_DENIED);
    T_EQ(rt_reg_set_value_ex_w(HKEY_USERS, W("v"), 0, REG_DWORD, (const BYTE *)&x, 4), ERROR_ACCESS_DENIED);
    HKEY k = NULL;
    T_EQ(rt_reg_create_key_ex_w(HKEY_LOCAL_MACHINE, W("Brand New"), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL), ERROR_ACCESS_DENIED);
    T_EQ(rt_reg_create_key_ex_w(HKEY_USERS, W("S-1-5-21"), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL), ERROR_ACCESS_DENIED);
    T_EQ(rt_reg_create_key_ex_w(HKEY_LOCAL_MACHINE, W("Brand New\\Deeper"), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL), ERROR_ACCESS_DENIED);
    T_TRUE(!has_key(HKEY_LOCAL_MACHINE, "Brand New"));
    T_EQ(rt_reg_query_value_ex_w(HKEY_LOCAL_MACHINE, W("v"), NULL, NULL, NULL, NULL), ERROR_FILE_NOT_FOUND);
    T_EQ(rt_reg_delete_value_w(HKEY_LOCAL_MACHINE, W("v")), ERROR_FILE_NOT_FOUND);

    T_CASE("the keys that exist below them are opened by create, not created");
    DWORD disp = 0;
    T_EQ(rt_reg_create_key_ex_w(HKEY_LOCAL_MACHINE, W("software"), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_OPENED_EXISTING_KEY);
    HKEY made = NULL;
    T_EQ(rt_reg_create_key_ex_w(k, W("Mine"), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &made, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_CREATED_NEW_KEY);
    cl(made);
    cl(k);

    T_CASE("HKEY_CURRENT_USER is an ordinary key: it takes values and keeps them in the file");
    T_EQ(rt_reg_set_value_ex_w(HKEY_CURRENT_USER, W("RootValue"), 0, REG_DWORD, (const BYTE *)&x, 4), ERROR_SUCCESS);
    T_TRUE(is_dw(HKEY_CURRENT_USER, "RootValue", 1));
    char *text = reg_text(NULL);
    T_TRUE(contains(text, "[HKEY_CURRENT_USER]\n\"RootValue\" = dword:00000001\n"));
    free(text);
    rt_registry_test_reload();
    T_TRUE(is_dw(HKEY_CURRENT_USER, "RootValue", 1));
    T_EQ(rt_reg_delete_value_w(HKEY_CURRENT_USER, W("RootValue")), ERROR_SUCCESS);
}

TEST(core, registry_views)
{
    T_CASE("HKEY_CLASSES_ROOT is HKEY_LOCAL_MACHINE\\Software\\Classes");
    HKEY k = mk(HKEY_CLASSES_ROOT, ".bsp\\shell");
    T_EQ(put_sz(k, NULL, "open"), ERROR_SUCCESS);
    cl(k);
    HKEY via = op(HKEY_LOCAL_MACHINE, "SOFTWARE\\Classes\\.bsp\\shell", KEY_READ);
    T_TRUE(is_sz(via, NULL, "open"));
    cl(via);
    HKEY other = mk(HKEY_LOCAL_MACHINE, "software\\CLASSES\\.dem");
    cl(other);
    T_TRUE(has_key(HKEY_CLASSES_ROOT, ".dem"));
    char name[16];
    T_TRUE(key_name(HKEY_CLASSES_ROOT, 0, name, sizeof name) && !strcmp(name, ".bsp"));
    T_TRUE(key_name(HKEY_CLASSES_ROOT, 1, name, sizeof name) && !strcmp(name, ".dem"));
    T_TRUE(!key_name(HKEY_CLASSES_ROOT, 2, name, sizeof name));
    T_EQ(rt_reg_delete_key_ex_w(HKEY_CLASSES_ROOT, W(".dem"), 0, 0), ERROR_SUCCESS);
    T_TRUE(!has_key(HKEY_LOCAL_MACHINE, "SOFTWARE\\Classes\\.dem"));

    T_CASE("HKEY_CURRENT_USER\\Software\\Classes is not merged into it");
    HKEY u = mk(HKEY_CURRENT_USER, "Software\\Classes\\.sav");
    cl(u);
    T_TRUE(!has_key(HKEY_CLASSES_ROOT, ".sav"));

    T_CASE("HKEY_CURRENT_CONFIG is HKEY_LOCAL_MACHINE\\System\\CurrentControlSet\\Hardware Profiles\\Current");
    HKEY c = mk(HKEY_CURRENT_CONFIG, "Software\\Fonts");
    T_EQ(put_dw(c, "LogPixels", 96), ERROR_SUCCESS);
    cl(c);
    HKEY via2 = op(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Hardware Profiles\\Current\\Software\\Fonts", KEY_READ);
    T_TRUE(is_dw(via2, "LogPixels", 96));
    cl(via2);
}

/* ---- duplicated handles --------------------------------------------------------------------------- */

TEST(core, registry_duplicate_handle)
{
    T_CASE("a key handle duplicated with the handle table works until the last one is closed");
    HKEY k = mk(HKEY_CURRENT_USER, "Software\\Dup");
    HKEY d = (HKEY)rt_handle_dup((HANDLE)k);
    T_TRUE(d != NULL && d != k);
    T_EQ(put_dw(k, "v", 1), ERROR_SUCCESS);
    T_TRUE(is_dw(d, "v", 1));
    cl(k);
    T_EQ(rt_reg_close_key(k), ERROR_INVALID_HANDLE);
    T_TRUE(is_dw(d, "v", 1));
    cl(d);
    T_EQ(rt_reg_close_key(d), ERROR_INVALID_HANDLE);

    T_CASE("CloseHandle (the table's close) closes a registry key too");
    HKEY e = mk(HKEY_CURRENT_USER, "Software\\Dup");
    T_TRUE(rt_handle_close((HANDLE)e));
    T_EQ(rt_reg_close_key(e), ERROR_INVALID_HANDLE);
}

TEST(core, registry_many_handles)
{
    T_CASE("a thousand open handles to one key, each closed once");
    HKEY h[1000];
    for (int i = 0; i < 1000; i++) h[i] = mk(HKEY_CURRENT_USER, "Software\\Many");
    for (int i = 0; i < 1000; i++) for (int j = 0; j < i; j++) if (h[i] == h[j]) T_TRUE(false);
    T_EQ(put_dw(h[500], "v", 5), ERROR_SUCCESS);
    for (int i = 0; i < 1000; i++) { T_TRUE(is_dw(h[i], "v", 5)); cl(h[i]); }
    for (int i = 0; i < 1000; i++) T_EQ(rt_reg_close_key(h[i]), ERROR_INVALID_HANDLE);
}

TEST(core, registry_reload_invalidates_handles)
{
    T_CASE("keys open when the tree is thrown away become deleted keys");
    HKEY k = mk(HKEY_CURRENT_USER, "Software\\Reloaded");
    T_EQ(put_dw(k, "v", 1), ERROR_SUCCESS);
    rt_registry_test_reload();
    DWORD n = 0;
    T_EQ(rt_reg_query_info_key_w(k, NULL, NULL, NULL, &n, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ERROR_KEY_DELETED);
    cl(k);
    T_TRUE(has_key(HKEY_CURRENT_USER, "Software\\Reloaded"));        /* the file had it */
}
