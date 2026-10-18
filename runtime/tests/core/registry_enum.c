/* The registry (rt/registry.h): enumeration of subkeys and values, and RegQueryInfoKey.  What is
 * behind these: advapi32's RegEnumKeyEx, RegEnumValue and RegQueryInfoKey. */
#include "registry_util.c"

#define SENTINEL 0x5A5Au

static void fill(WCHAR *b, size_t n) { for (size_t i = 0; i < n; i++) b[i] = SENTINEL; }

static bool unfilled(const WCHAR *b, size_t n) {
    for (size_t i = 0; i < n; i++) if (b[i] != SENTINEL) return false;
    return true;
}

/* ---- subkeys ---------------------------------------------------------------------------------- */

TEST(core, registry_enum_key_order)
{
    T_CASE("subkeys are listed sorted by name, ignoring case, in the case they were created with");
    HKEY k = mk(HKEY_CURRENT_USER, "Software\\Order");
    const char *made[] = { "banana", "Apple", "cherry", "apple2", "Apricot", "_under", "10", "9", "Zed" };
    for (unsigned i = 0; i < sizeof made / sizeof made[0]; i++) cl(mk(k, made[i]));
    const char *want[] = { "10", "9", "Apple", "apple2", "Apricot", "banana", "cherry", "Zed", "_under" };
    char name[32];
    for (unsigned i = 0; i < 9; i++) {
        T_TRUE(key_name(k, i, name, sizeof name));
        T_TRUE(!strcmp(name, want[i]));
    }
    T_TRUE(!key_name(k, 9, name, sizeof name));
    cl(k);
}

TEST(core, registry_enum_key_end)
{
    HKEY k = mk(HKEY_CURRENT_USER, "Software\\End");
    cl(mk(k, "one"));
    cl(mk(k, "two"));
    WCHAR name[16];
    DWORD cch;

    T_CASE("the last index works and the next one is ERROR_NO_MORE_ITEMS, which leaves the arguments alone");
    cch = 16;
    T_EQ(rt_reg_enum_key_ex_w(k, 1, name, &cch, NULL, NULL, NULL, NULL), ERROR_SUCCESS);
    T_TRUE(cch == 3 && weq(name, "two"));
    fill(name, 16);
    cch = 16;
    T_EQ(rt_reg_enum_key_ex_w(k, 2, name, &cch, NULL, NULL, NULL, NULL), ERROR_NO_MORE_ITEMS);
    T_EQ(cch, 16);
    T_TRUE(unfilled(name, 16));
    cch = 16;
    T_EQ(rt_reg_enum_key_ex_w(k, 0xFFFFFFFFu, name, &cch, NULL, NULL, NULL, NULL), ERROR_NO_MORE_ITEMS);

    T_CASE("a key with no subkeys has nothing to list");
    HKEY e = mk(HKEY_CURRENT_USER, "Software\\Empty");
    cch = 16;
    T_EQ(rt_reg_enum_key_ex_w(e, 0, name, &cch, NULL, NULL, NULL, NULL), ERROR_NO_MORE_ITEMS);
    cl(e);
    cl(k);
}

TEST(core, registry_enum_key_name_buffer)
{
    HKEY k = mk(HKEY_CURRENT_USER, "Software\\NameBuf");
    cl(mk(k, "abcde"));
    WCHAR name[16];
    DWORD cch;

    T_CASE("the size counts the terminator: 6 holds a 5-character name, 5 does not");
    fill(name, 16);
    cch = 6;
    T_EQ(rt_reg_enum_key_ex_w(k, 0, name, &cch, NULL, NULL, NULL, NULL), ERROR_SUCCESS);
    T_TRUE(cch == 5 && weq(name, "abcde"));
    T_EQ(name[6], SENTINEL);
    fill(name, 16);
    cch = 5;
    T_EQ(rt_reg_enum_key_ex_w(k, 0, name, &cch, NULL, NULL, NULL, NULL), ERROR_MORE_DATA);
    T_TRUE(unfilled(name, 16));                             /* nothing written for what does not fit */
    T_EQ(cch, 5);                                           /* UNSURE (rt/registry.h): left as it was */

    T_CASE("room to spare: the count is the length of the name");
    fill(name, 16);
    cch = 16;
    T_EQ(rt_reg_enum_key_ex_w(k, 0, name, &cch, NULL, NULL, NULL, NULL), ERROR_SUCCESS);
    T_EQ(cch, 5);
    T_EQ(name[5], 0);
    T_EQ(name[6], SENTINEL);

    T_CASE("a zero-size buffer cannot hold even the terminator");
    cch = 0;
    T_EQ(rt_reg_enum_key_ex_w(k, 0, name, &cch, NULL, NULL, NULL, NULL), ERROR_MORE_DATA);

    T_CASE("name and size are required, and so is the size of a class buffer; reserved must be NULL");
    cch = 16;
    DWORD cc = 16, reserved = 0;
    WCHAR cls[16];
    T_EQ(rt_reg_enum_key_ex_w(k, 0, NULL, &cch, NULL, NULL, NULL, NULL), ERROR_INVALID_PARAMETER);
    T_EQ(rt_reg_enum_key_ex_w(k, 0, name, NULL, NULL, NULL, NULL, NULL), ERROR_INVALID_PARAMETER);
    T_EQ(rt_reg_enum_key_ex_w(k, 0, name, &cch, NULL, cls, NULL, NULL), ERROR_INVALID_PARAMETER);
    T_EQ(rt_reg_enum_key_ex_w(k, 0, name, &cch, &reserved, NULL, NULL, NULL), ERROR_INVALID_PARAMETER);
    T_EQ(rt_reg_enum_key_ex_w(k, 0, name, &cch, NULL, cls, &cc, NULL), ERROR_SUCCESS);
    cl(k);
}

TEST(core, registry_enum_key_class_and_time)
{
    HKEY k = mk(HKEY_CURRENT_USER, "Software\\ClassTime");
    HKEY a = NULL;
    T_EQ(rt_reg_create_key_ex_w(k, W("WithClass"), 0, W("MyClass"), 0, KEY_ALL_ACCESS, NULL, &a, NULL), ERROR_SUCCESS);
    cl(a);
    cl(mk(k, "NoClass"));
    WCHAR name[32], cls[16];
    DWORD cn, cc;
    FILETIME ft;

    T_CASE("the class is returned with its length; a key without a class has the empty string");
    cn = 32; cc = 16;
    T_EQ(rt_reg_enum_key_ex_w(k, 1, name, &cn, NULL, cls, &cc, &ft), ERROR_SUCCESS);
    T_TRUE(weq(name, "WithClass") && weq(cls, "MyClass") && cc == 7);
    T_TRUE(ft.dwHighDateTime != 0);
    fill(cls, 16);
    cn = 32; cc = 16;
    T_EQ(rt_reg_enum_key_ex_w(k, 0, name, &cn, NULL, cls, &cc, &ft), ERROR_SUCCESS);
    T_TRUE(weq(name, "NoClass") && cc == 0 && cls[0] == 0 && cls[1] == SENTINEL);

    T_CASE("a class that does not fit is ERROR_MORE_DATA and nothing is written, the name either");
    fill(name, 32); fill(cls, 16);
    cn = 32; cc = 7;
    T_EQ(rt_reg_enum_key_ex_w(k, 1, name, &cn, NULL, cls, &cc, &ft), ERROR_MORE_DATA);
    T_TRUE(unfilled(name, 32) && unfilled(cls, 16));
    cn = 32; cc = 8;                                        /* exactly the class and its terminator */
    T_EQ(rt_reg_enum_key_ex_w(k, 1, name, &cn, NULL, cls, &cc, &ft), ERROR_SUCCESS);
    T_TRUE(weq(cls, "MyClass") && cc == 7);

    T_CASE("the last write time is that of creation, and an update of the child moves its own, not the parent's");
    FILETIME before, after, parent_before, parent_after;
    T_EQ(rt_reg_query_info_key_w(k, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &parent_before), ERROR_SUCCESS);
    usleep(5000);
    HKEY wc = op(k, "WithClass", KEY_ALL_ACCESS);
    T_EQ(rt_reg_query_info_key_w(wc, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &before), ERROR_SUCCESS);
    T_EQ(put_dw(wc, "v", 1), ERROR_SUCCESS);
    T_EQ(rt_reg_query_info_key_w(wc, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &after), ERROR_SUCCESS);
    T_TRUE(((uint64_t)after.dwHighDateTime << 32 | after.dwLowDateTime) > ((uint64_t)before.dwHighDateTime << 32 | before.dwLowDateTime));
    T_EQ(rt_reg_query_info_key_w(k, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &parent_after), ERROR_SUCCESS);
    T_TRUE(parent_after.dwLowDateTime == parent_before.dwLowDateTime && parent_after.dwHighDateTime == parent_before.dwHighDateTime);
    cn = 32; cc = 16;
    T_EQ(rt_reg_enum_key_ex_w(k, 1, name, &cn, NULL, cls, &cc, &ft), ERROR_SUCCESS);
    T_TRUE(ft.dwLowDateTime == after.dwLowDateTime && ft.dwHighDateTime == after.dwHighDateTime);

    T_CASE("creating or deleting a subkey moves the parent's last write time");
    usleep(5000);
    cl(mk(k, "Third"));
    T_EQ(rt_reg_query_info_key_w(k, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &parent_after), ERROR_SUCCESS);
    uint64_t p1 = (uint64_t)parent_after.dwHighDateTime << 32 | parent_after.dwLowDateTime;
    T_TRUE(p1 > ((uint64_t)parent_before.dwHighDateTime << 32 | parent_before.dwLowDateTime));
    usleep(5000);
    T_EQ(rt_reg_delete_key_ex_w(k, W("Third"), 0, 0), ERROR_SUCCESS);
    T_EQ(rt_reg_query_info_key_w(k, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &parent_after), ERROR_SUCCESS);
    T_TRUE(((uint64_t)parent_after.dwHighDateTime << 32 | parent_after.dwLowDateTime) > p1);
    cl(wc);
    cl(k);
}

TEST(core, registry_enum_stable)
{
    T_CASE("the indices of an enumeration are stable while nothing changes");
    HKEY k = mk(HKEY_CURRENT_USER, "Software\\Stable");
    for (int i = 0; i < 40; i++) {
        char n[16];
        snprintf(n, sizeof n, "key%02d", (i * 17) % 40);
        cl(mk(k, n));
        snprintf(n, sizeof n, "val%02d", (i * 7) % 40);
        T_EQ(put_dw(k, n, (DWORD)i), ERROR_SUCCESS);
    }
    char first[40][16], vfirst[40][16];
    for (DWORD i = 0; i < 40; i++) {
        T_TRUE(key_name(k, i, first[i], 16));
        T_TRUE(value_name(k, i, vfirst[i], 16));
    }
    for (int round = 0; round < 3; round++)
        for (DWORD i = 0; i < 40; i++) {
            char again[16];
            T_TRUE(key_name(k, i, again, 16) && !strcmp(again, first[i]));
            T_TRUE(value_name(k, i, again, 16) && !strcmp(again, vfirst[i]));
        }
    T_CASE("subkeys come out sorted, values in the order they were created");
    for (DWORD i = 1; i < 40; i++) T_TRUE(strcmp(first[i - 1], first[i]) < 0);
    T_TRUE(!strcmp(vfirst[0], "val00") && !strcmp(vfirst[1], "val07") && !strcmp(vfirst[2], "val14"));

    T_CASE("deleting one shifts the ones after it down by one");
    T_EQ(rt_reg_delete_key_ex_w(k, W("key05"), 0, 0), ERROR_SUCCESS);
    char again[16];
    T_TRUE(key_name(k, 4, again, 16) && !strcmp(again, "key04"));
    T_TRUE(key_name(k, 5, again, 16) && !strcmp(again, "key06"));
    T_TRUE(!key_name(k, 39, again, 16));
    cl(k);
}

/* ---- values ------------------------------------------------------------------------------------ */

TEST(core, registry_enum_value_basic)
{
    HKEY k = mk(HKEY_CURRENT_USER, "Software\\EnumValue");
    static const WCHAR hello[] = { 'h', 'i', 0 };
    T_EQ(put_dw(k, "Alpha", 0x11223344), ERROR_SUCCESS);
    T_EQ(put_raw(k, "Beta", REG_SZ, hello, 6), ERROR_SUCCESS);
    T_EQ(put_raw(k, "Gamma", REG_BINARY, NULL, 0), ERROR_SUCCESS);
    WCHAR name[16];
    BYTE data[16];
    DWORD cch, cb, type;

    T_CASE("values come in creation order with their type and data");
    cch = 16; cb = 16; type = 0;
    T_EQ(rt_reg_enum_value_w(k, 0, name, &cch, NULL, &type, data, &cb), ERROR_SUCCESS);
    T_TRUE(weq(name, "Alpha") && cch == 5 && type == REG_DWORD && cb == 4);
    T_TRUE(!memcmp(data, "\x44\x33\x22\x11", 4));
    cch = 16; cb = 16;
    T_EQ(rt_reg_enum_value_w(k, 1, name, &cch, NULL, &type, data, &cb), ERROR_SUCCESS);
    T_TRUE(weq(name, "Beta") && type == REG_SZ && cb == 6 && !memcmp(data, hello, 6));
    cch = 16; cb = 16;
    T_EQ(rt_reg_enum_value_w(k, 2, name, &cch, NULL, &type, data, &cb), ERROR_SUCCESS);
    T_TRUE(weq(name, "Gamma") && type == REG_BINARY && cb == 0);

    T_CASE("past the end is ERROR_NO_MORE_ITEMS");
    cch = 16; cb = 16;
    T_EQ(rt_reg_enum_value_w(k, 3, name, &cch, NULL, &type, data, &cb), ERROR_NO_MORE_ITEMS);
    T_EQ(rt_reg_enum_value_w(k, 0xFFFFFFFFu, name, &cch, NULL, &type, data, &cb), ERROR_NO_MORE_ITEMS);

    T_CASE("the data and the type are optional, and the name only needs its own buffer");
    cch = 16;
    T_EQ(rt_reg_enum_value_w(k, 0, name, &cch, NULL, NULL, NULL, NULL), ERROR_SUCCESS);
    cch = 16; cb = 0;
    T_EQ(rt_reg_enum_value_w(k, 1, name, &cch, NULL, NULL, NULL, &cb), ERROR_SUCCESS);   /* data NULL: the size is returned */
    T_EQ(cb, 6);
    T_EQ(rt_reg_enum_value_w(k, 1, name, &cch, NULL, NULL, data, NULL), ERROR_INVALID_PARAMETER);
    DWORD reserved = 0;
    T_EQ(rt_reg_enum_value_w(k, 1, name, &cch, &reserved, NULL, NULL, NULL), ERROR_INVALID_PARAMETER);
    T_EQ(rt_reg_enum_value_w(k, 1, NULL, &cch, NULL, NULL, NULL, NULL), ERROR_INVALID_PARAMETER);
    T_EQ(rt_reg_enum_value_w(k, 1, name, NULL, NULL, NULL, NULL, NULL), ERROR_INVALID_PARAMETER);
    cl(k);
}

TEST(core, registry_enum_value_buffers)
{
    HKEY k = mk(HKEY_CURRENT_USER, "Software\\EnumBuf");
    BYTE ten[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    T_EQ(put_raw(k, "name12345", REG_BINARY, ten, 10), ERROR_SUCCESS);
    WCHAR name[32];
    BYTE data[32];
    DWORD cch, cb, type;

    T_CASE("the name size counts the terminator; one short is ERROR_MORE_DATA and nothing is written");
    fill(name, 32);
    cch = 10; cb = 32; type = 0xFFFF;
    T_EQ(rt_reg_enum_value_w(k, 0, name, &cch, NULL, &type, data, &cb), ERROR_SUCCESS);
    T_TRUE(cch == 9 && weq(name, "name12345"));
    fill(name, 32);
    cch = 9; cb = 32; type = 0xFFFF;
    T_EQ(rt_reg_enum_value_w(k, 0, name, &cch, NULL, &type, data, &cb), ERROR_MORE_DATA);
    T_TRUE(unfilled(name, 32));
    T_EQ(cch, 9);

    T_CASE("data one byte short: ERROR_MORE_DATA, the name stored, the size and the type returned");
    memset(data, 0xA5, sizeof data);
    cch = 32; cb = 9; type = 0xFFFF;
    T_EQ(rt_reg_enum_value_w(k, 0, name, &cch, NULL, &type, data, &cb), ERROR_MORE_DATA);
    T_TRUE(weq(name, "name12345") && cch == 9);
    T_EQ(cb, 10);
    T_EQ(type, REG_BINARY);
    T_EQ(data[0], 0xA5);
    cb = 10; cch = 32;
    T_EQ(rt_reg_enum_value_w(k, 0, name, &cch, NULL, &type, data, &cb), ERROR_SUCCESS);
    T_TRUE(cb == 10 && !memcmp(data, ten, 10));
    cl(k);
}

TEST(core, registry_enum_value_changes)
{
    T_CASE("deleting a value moves the later ones up; replacing one keeps its place; a new one goes last");
    HKEY k = mk(HKEY_CURRENT_USER, "Software\\EnumChange");
    for (int i = 0; i < 5; i++) { char n[8]; snprintf(n, sizeof n, "v%d", i); T_EQ(put_dw(k, n, (DWORD)i), ERROR_SUCCESS); }
    T_EQ(rt_reg_delete_value_w(k, W("v1")), ERROR_SUCCESS);
    T_EQ(put_dw(k, "V3", 33), ERROR_SUCCESS);
    T_EQ(put_dw(k, "v1", 11), ERROR_SUCCESS);
    const char *want[] = { "v0", "v2", "v3", "v4", "v1" };
    char name[8];
    for (DWORD i = 0; i < 5; i++) T_TRUE(value_name(k, i, name, sizeof name) && !strcmp(name, want[i]));
    cl(k);
}

/* ---- RegQueryInfoKey ---------------------------------------------------------------------------- */

TEST(core, registry_query_info)
{
    T_CASE("the counts and the maxima");
    HKEY k = NULL;
    T_EQ(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("Software\\Info"), 0, W("Klass"), 0, KEY_ALL_ACCESS, NULL, &k, NULL), ERROR_SUCCESS);
    HKEY s;
    T_EQ(rt_reg_create_key_ex_w(k, W("short"), 0, W("c1"), 0, KEY_ALL_ACCESS, NULL, &s, NULL), ERROR_SUCCESS); cl(s);
    T_EQ(rt_reg_create_key_ex_w(k, W("a much longer name"), 0, W("a long class"), 0, KEY_ALL_ACCESS, NULL, &s, NULL), ERROR_SUCCESS); cl(s);
    cl(mk(k, "mid"));
    static const BYTE blob[100] = { 1 };
    T_EQ(put_raw(k, "x", REG_BINARY, blob, 10), ERROR_SUCCESS);
    T_EQ(put_raw(k, "a value with a long name", REG_BINARY, blob, 3), ERROR_SUCCESS);   /* a long name, a short value */
    T_EQ(put_raw(k, NULL, REG_SZ, blob, 8), ERROR_SUCCESS);

    DWORD nsub, maxsub, maxcls, nval, maxname, maxdata;
    FILETIME ft;
    T_EQ(rt_reg_query_info_key_w(k, NULL, NULL, NULL, &nsub, &maxsub, &maxcls, &nval, &maxname, &maxdata, NULL, &ft), ERROR_SUCCESS);
    T_EQ(nsub, 3);
    T_EQ(maxsub, 18);
    T_EQ(maxcls, 12);
    T_EQ(nval, 3);
    T_EQ(maxname, 24);
    T_EQ(maxdata, 10);
    T_TRUE(ft.dwHighDateTime != 0);

    T_CASE("every output is optional");
    T_EQ(rt_reg_query_info_key_w(k, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ERROR_SUCCESS);
    nsub = 77;
    T_EQ(rt_reg_query_info_key_w(k, NULL, NULL, NULL, &nsub, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ERROR_SUCCESS);
    T_EQ(nsub, 3);

    T_CASE("the class of the key itself, with the length when there is no buffer");
    WCHAR cls[16];
    DWORD cc = 16;
    fill(cls, 16);
    T_EQ(rt_reg_query_info_key_w(k, cls, &cc, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ERROR_SUCCESS);
    T_TRUE(weq(cls, "Klass") && cc == 5);
    cc = 0;
    T_EQ(rt_reg_query_info_key_w(k, NULL, &cc, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ERROR_SUCCESS);
    T_EQ(cc, 5);
    cc = 6;
    T_EQ(rt_reg_query_info_key_w(k, cls, &cc, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ERROR_SUCCESS);
    fill(cls, 16);
    cc = 5;
    T_EQ(rt_reg_query_info_key_w(k, cls, &cc, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ERROR_MORE_DATA);
    T_TRUE(unfilled(cls, 16));
    T_EQ(cc, 5);                                            /* the length needed (UNSURE: Windows) */
    cc = 0;
    T_EQ(rt_reg_query_info_key_w(k, cls, &cc, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ERROR_MORE_DATA);
    T_EQ(cc, 5);

    T_CASE("a key with no class has the empty string");
    HKEY sub = op(k, "mid", KEY_READ);
    cc = 16;
    fill(cls, 16);
    T_EQ(rt_reg_query_info_key_w(sub, cls, &cc, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ERROR_SUCCESS);
    T_TRUE(cc == 0 && cls[0] == 0 && cls[1] == SENTINEL);
    T_EQ(rt_reg_query_info_key_w(sub, NULL, NULL, NULL, &nsub, &maxsub, &maxcls, &nval, &maxname, &maxdata, NULL, NULL), ERROR_SUCCESS);
    T_TRUE(nsub == 0 && maxsub == 0 && maxcls == 0 && nval == 0 && maxname == 0 && maxdata == 0);
    cl(sub);

    T_CASE("a class buffer without a size, a reserved argument, and a security descriptor size");
    DWORD reserved = 0, sd = 0;
    T_EQ(rt_reg_query_info_key_w(k, cls, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ERROR_INVALID_PARAMETER);
    T_EQ(rt_reg_query_info_key_w(k, NULL, NULL, &reserved, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ERROR_INVALID_PARAMETER);
    T_TRAPS(rt_reg_query_info_key_w(k, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &sd, NULL), "security descriptor");
    cl(k);
}

TEST(core, registry_query_info_predefined)
{
    T_CASE("the roots of the tree: HKEY_LOCAL_MACHINE has the three hives of the seed, the others none");
    DWORD n = 99;
    T_EQ(rt_reg_query_info_key_w(HKEY_LOCAL_MACHINE, NULL, NULL, NULL, &n, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ERROR_SUCCESS);
    T_EQ(n, 3);
    char name[32];
    T_TRUE(key_name(HKEY_LOCAL_MACHINE, 0, name, sizeof name) && !strcmp(name, "HARDWARE"));
    T_TRUE(key_name(HKEY_LOCAL_MACHINE, 1, name, sizeof name) && !strcmp(name, "SOFTWARE"));
    T_TRUE(key_name(HKEY_LOCAL_MACHINE, 2, name, sizeof name) && !strcmp(name, "SYSTEM"));
    T_EQ(rt_reg_query_info_key_w(HKEY_CURRENT_USER, NULL, NULL, NULL, &n, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ERROR_SUCCESS);
    T_EQ(n, 0);
    T_EQ(rt_reg_query_info_key_w(HKEY_USERS, NULL, NULL, NULL, &n, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ERROR_SUCCESS);
    T_EQ(n, 0);
    DWORD nv = 99;
    T_EQ(rt_reg_query_info_key_w(HKEY_LOCAL_MACHINE, NULL, NULL, NULL, NULL, NULL, NULL, &nv, NULL, NULL, NULL, NULL), ERROR_SUCCESS);
    T_EQ(nv, 0);
}
