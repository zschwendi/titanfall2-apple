/* The registry (rt/registry.h): the A functions.  Names and the data of string values go through
 * code page 1252; sizes are bytes of the converted text.  What is behind these: RegOpenKeyExA,
 * RegCreateKeyExA, RegQueryValueExA, RegSetValueExA, RegDeleteValueA, RegEnumKeyExA,
 * RegEnumValueA, RegQueryInfoKeyA, RegDeleteKeyExA. */
#include "registry_util.c"

#define SENTINEL 0xA5

static HKEY mka(HKEY parent, const char *path) {
    HKEY k = NULL;
    LSTATUS st = rt_reg_create_key_ex_a(parent, path, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    T_EQ(st, ERROR_SUCCESS);
    return k;
}

static LSTATUS set_a(HKEY k, const char *name, DWORD type, const void *data, DWORD cb) {
    return rt_reg_set_value_ex_a(k, name, 0, type, (const BYTE *)data, cb);
}

/* ---- names -------------------------------------------------------------------------------------- */

TEST(core, registry_ansi_key_names)
{
    T_CASE("a key made through the A function is found by the W function under the converted name");
    HKEY root = mka(HKEY_CURRENT_USER, "Software\\Ansi");
    HKEY k = mka(root, "caf\xE9");                          /* é is 0xE9 in code page 1252 */
    cl(k);
    static const WCHAR cafe[] = { 'c', 'a', 'f', 0xE9, 0 };
    HKEY w = NULL;
    T_EQ(rt_reg_open_key_ex_w(root, cafe, 0, KEY_READ, &w), ERROR_SUCCESS);
    cl(w);

    T_CASE("the code page maps 0x80..0x9F to its own characters: 0x80 is the euro sign, 0x9F is Y with a diaeresis");
    cl(mka(root, "\x80"));
    cl(mka(root, "\x9F"));
    static const WCHAR euro[] = { 0x20AC, 0 }, yuml[] = { 0x178, 0 };
    T_EQ(rt_reg_open_key_ex_w(root, euro, 0, KEY_READ, &w), ERROR_SUCCESS); cl(w);
    T_EQ(rt_reg_open_key_ex_w(root, yuml, 0, KEY_READ, &w), ERROR_SUCCESS); cl(w);

    T_CASE("the five undefined bytes are the controls of the same value");
    cl(mka(root, "\x81"));
    static const WCHAR c81[] = { 0x81, 0 };
    T_EQ(rt_reg_open_key_ex_w(root, c81, 0, KEY_READ, &w), ERROR_SUCCESS); cl(w);

    T_CASE("names compare ignoring case across the A and W functions: 0xC9 is the capital of 0xE9");
    HKEY up = NULL;
    DWORD disp = 0;
    T_EQ(rt_reg_create_key_ex_a(root, "CAF\xC9", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &up, &disp), ERROR_SUCCESS);
    T_EQ(disp, REG_OPENED_EXISTING_KEY);
    cl(up);

    T_CASE("enumeration through the A function returns the bytes of the converted name, in the stored case");
    char name[16];
    DWORD cch;
    int found = 0;
    for (DWORD i = 0;; i++) {
        cch = 16;
        if (rt_reg_enum_key_ex_a(root, i, name, &cch, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
        if (cch == 4 && !memcmp(name, "caf\xE9", 5)) found++;
        if (cch == 1 && (unsigned char)name[0] == 0x80) found += 10;
        if (cch == 1 && (unsigned char)name[0] == 0x9F) found += 100;
        if (cch == 1 && (unsigned char)name[0] == 0x81) found += 1000;
    }
    T_EQ(found, 1111);
    cl(root);
}

TEST(core, registry_ansi_name_buffers)
{
    HKEY root = mka(HKEY_CURRENT_USER, "Software\\AnsiBuf");
    cl(mka(root, "caf\xE9"));
    char name[16];
    DWORD cch;

    T_CASE("the size counts bytes, and the terminator: 5 holds a name of 4 bytes, 4 does not");
    memset(name, SENTINEL, sizeof name);
    cch = 5;
    T_EQ(rt_reg_enum_key_ex_a(root, 0, name, &cch, NULL, NULL, NULL, NULL), ERROR_SUCCESS);
    T_TRUE(cch == 4 && !memcmp(name, "caf\xE9", 5));
    T_EQ((unsigned char)name[5], SENTINEL);
    memset(name, SENTINEL, sizeof name);
    cch = 4;
    T_EQ(rt_reg_enum_key_ex_a(root, 0, name, &cch, NULL, NULL, NULL, NULL), ERROR_MORE_DATA);
    T_EQ((unsigned char)name[0], SENTINEL);

    T_CASE("the same for value names");
    T_EQ(set_a(root, "v\xE9", REG_DWORD, "\1\0\0\0", 4), ERROR_SUCCESS);
    memset(name, SENTINEL, sizeof name);
    cch = 3;
    T_EQ(rt_reg_enum_value_a(root, 0, name, &cch, NULL, NULL, NULL, NULL), ERROR_SUCCESS);
    T_TRUE(cch == 2 && !memcmp(name, "v\xE9", 3));
    cch = 2;
    T_EQ(rt_reg_enum_value_a(root, 0, name, &cch, NULL, NULL, NULL, NULL), ERROR_MORE_DATA);
    cl(root);
}

TEST(core, registry_ansi_value_names)
{
    HKEY k = mka(HKEY_CURRENT_USER, "Software\\AnsiValueNames");
    T_CASE("value names convert like key names, and compare ignoring case across A and W");
    T_EQ(set_a(k, "n\xE9me", REG_DWORD, "\7\0\0\0", 4), ERROR_SUCCESS);
    static const WCHAR upper[] = { 'N', 0xC9, 'M', 'E', 0 };
    DWORD v = 0, cb = 4, type = 0;
    T_EQ(rt_reg_query_value_ex_w(k, upper, NULL, &type, (BYTE *)&v, &cb), ERROR_SUCCESS);
    T_TRUE(v == 7 && type == REG_DWORD);
    v = 0; cb = 4;
    T_EQ(rt_reg_query_value_ex_a(k, "N\xC9ME", NULL, &type, (BYTE *)&v, &cb), ERROR_SUCCESS);
    T_EQ(v, 7);

    T_CASE("the default value by NULL or empty name");
    T_EQ(set_a(k, NULL, REG_DWORD, "\2\0\0\0", 4), ERROR_SUCCESS);
    cb = 4; v = 0;
    T_EQ(rt_reg_query_value_ex_a(k, "", NULL, &type, (BYTE *)&v, &cb), ERROR_SUCCESS);
    T_EQ(v, 2);
    T_EQ(rt_reg_delete_value_a(k, ""), ERROR_SUCCESS);
    T_EQ(rt_reg_delete_value_a(k, NULL), ERROR_FILE_NOT_FOUND);
    T_EQ(rt_reg_delete_value_a(k, "N\xC9ME"), ERROR_SUCCESS);
    T_EQ(rt_reg_delete_value_a(k, "n\xE9me"), ERROR_FILE_NOT_FOUND);
    cl(k);
}

/* ---- the data of string values --------------------------------------------------------------------- */

TEST(core, registry_ansi_string_data)
{
    HKEY k = mka(HKEY_CURRENT_USER, "Software\\AnsiData");

    T_CASE("string data written through the A function is stored as UTF-16, one code unit per byte");
    T_EQ(set_a(k, "s", REG_SZ, "caf\xE9", 5), ERROR_SUCCESS);       /* with its terminator */
    BYTE buf[32];
    DWORD cb = 32, type = 0;
    T_EQ(rt_reg_query_value_ex_w(k, W("s"), NULL, &type, buf, &cb), ERROR_SUCCESS);
    T_TRUE(type == REG_SZ && cb == 10);
    static const WCHAR want[] = { 'c', 'a', 'f', 0xE9, 0 };
    T_TRUE(!memcmp(buf, want, 10));

    T_CASE("read through the A function it is converted back and the size is that of the converted text");
    memset(buf, SENTINEL, sizeof buf);
    cb = 32; type = 0;
    T_EQ(rt_reg_query_value_ex_a(k, "s", NULL, &type, buf, &cb), ERROR_SUCCESS);
    T_TRUE(type == REG_SZ && cb == 5);
    T_TRUE(!memcmp(buf, "caf\xE9", 5));
    T_EQ(buf[5], SENTINEL);

    T_CASE("a size query through the A function converts to measure; a small buffer gets the converted size");
    cb = 0;
    T_EQ(rt_reg_query_value_ex_a(k, "s", NULL, NULL, NULL, &cb), ERROR_SUCCESS);
    T_EQ(cb, 5);
    memset(buf, SENTINEL, sizeof buf);
    cb = 4; type = 0;
    T_EQ(rt_reg_query_value_ex_a(k, "s", NULL, &type, buf, &cb), ERROR_MORE_DATA);
    T_TRUE(cb == 5 && type == REG_SZ && buf[0] == SENTINEL);
    cb = 5;
    T_EQ(rt_reg_query_value_ex_a(k, "s", NULL, &type, buf, &cb), ERROR_SUCCESS);

    T_CASE("no terminator is added on the way in and none is removed: the size is what was written");
    T_EQ(set_a(k, "n", REG_SZ, "abc", 3), ERROR_SUCCESS);
    cb = 0;
    T_EQ(rt_reg_query_value_ex_w(k, W("n"), NULL, NULL, NULL, &cb), ERROR_SUCCESS);
    T_EQ(cb, 6);
    cb = 0;
    T_EQ(rt_reg_query_value_ex_a(k, "n", NULL, NULL, NULL, &cb), ERROR_SUCCESS);
    T_EQ(cb, 3);

    T_CASE("and on the way out, a string that does not end in a NUL gets one when there is a byte of room");
    memset(buf, SENTINEL, sizeof buf);
    cb = 3;
    T_EQ(rt_reg_query_value_ex_a(k, "n", NULL, NULL, buf, &cb), ERROR_SUCCESS);
    T_EQ(buf[3], SENTINEL);
    memset(buf, SENTINEL, sizeof buf);
    cb = 4;
    T_EQ(rt_reg_query_value_ex_a(k, "n", NULL, NULL, buf, &cb), ERROR_SUCCESS);
    T_TRUE(cb == 3 && !memcmp(buf, "abc", 3) && buf[3] == 0 && buf[4] == SENTINEL);

    T_CASE("REG_EXPAND_SZ and REG_MULTI_SZ convert as well; the NULs inside a multi-string are kept");
    T_EQ(set_a(k, "m", REG_MULTI_SZ, "a\0b\xE9\0\0", 6), ERROR_SUCCESS);
    cb = 0;
    T_EQ(rt_reg_query_value_ex_w(k, W("m"), NULL, NULL, NULL, &cb), ERROR_SUCCESS);
    T_EQ(cb, 12);
    memset(buf, SENTINEL, sizeof buf);
    cb = 32;
    T_EQ(rt_reg_query_value_ex_a(k, "m", NULL, &type, buf, &cb), ERROR_SUCCESS);
    T_TRUE(type == REG_MULTI_SZ && cb == 6 && !memcmp(buf, "a\0b\xE9\0\0", 6));
    T_EQ(set_a(k, "e", REG_EXPAND_SZ, "%PATH%", 7), ERROR_SUCCESS);
    cb = 32;
    T_EQ(rt_reg_query_value_ex_a(k, "e", NULL, &type, buf, &cb), ERROR_SUCCESS);
    T_TRUE(type == REG_EXPAND_SZ && cb == 7 && !strcmp((char *)buf, "%PATH%"));

    T_CASE("an empty string value");
    T_EQ(set_a(k, "z", REG_SZ, NULL, 0), ERROR_SUCCESS);
    cb = 99;
    T_EQ(rt_reg_query_value_ex_a(k, "z", NULL, NULL, NULL, &cb), ERROR_SUCCESS);
    T_EQ(cb, 0);
    cb = 99;
    T_EQ(rt_reg_query_value_ex_w(k, W("z"), NULL, NULL, NULL, &cb), ERROR_SUCCESS);
    T_EQ(cb, 0);

    T_CASE("the other types are not converted, on the way in or the way out");
    static const BYTE bin[6] = { 0x80, 0xE9, 0, 0xFF, 0x81, 0x9F };
    T_EQ(set_a(k, "b", REG_BINARY, bin, 6), ERROR_SUCCESS);
    T_EQ(set_a(k, "d", REG_DWORD, "\xE9\x80\0\0", 4), ERROR_SUCCESS);
    memset(buf, 0, sizeof buf);
    cb = 32;
    T_EQ(rt_reg_query_value_ex_w(k, W("b"), NULL, NULL, buf, &cb), ERROR_SUCCESS);
    T_TRUE(cb == 6 && !memcmp(buf, bin, 6));
    cb = 32;
    T_EQ(rt_reg_query_value_ex_a(k, "b", NULL, NULL, buf, &cb), ERROR_SUCCESS);
    T_TRUE(cb == 6 && !memcmp(buf, bin, 6));
    DWORD d = 0; cb = 4;
    T_EQ(rt_reg_query_value_ex_w(k, W("d"), NULL, NULL, (BYTE *)&d, &cb), ERROR_SUCCESS);
    T_EQ(d, 0x80E9);
    cl(k);
}

TEST(core, registry_ansi_wide_data_read_as_ansi)
{
    T_CASE("a string written as UTF-16 and read through the A function is converted to code page 1252");
    HKEY k = mka(HKEY_CURRENT_USER, "Software\\AnsiWide");
    static const WCHAR text[] = { 'x', 0x20AC, 0xE9, 0x178, 0 };
    T_EQ(rt_reg_set_value_ex_w(k, W("t"), 0, REG_SZ, (const BYTE *)text, sizeof text), ERROR_SUCCESS);
    BYTE buf[16];
    DWORD cb = 16;
    T_EQ(rt_reg_query_value_ex_a(k, "t", NULL, NULL, buf, &cb), ERROR_SUCCESS);
    T_TRUE(cb == 5 && !memcmp(buf, "x\x80\xE9\x9F", 5));

    T_CASE("a character the code page lacks is converted by Windows's best-fit table, or to '?' when the table has nothing for it");
    static const WCHAR bad[] = { 'a', 0x100, 0x4E2D, 0 };       /* A with macron best-fits to 'A'; the ideograph has no best fit */
    T_EQ(rt_reg_set_value_ex_w(k, W("bad"), 0, REG_SZ, (const BYTE *)bad, sizeof bad), ERROR_SUCCESS);
    cb = 0;
    T_EQ(rt_reg_query_value_ex_a(k, "bad", NULL, NULL, NULL, &cb), ERROR_SUCCESS);
    T_EQ(cb, 4);
    cb = 16;
    T_EQ(rt_reg_query_value_ex_a(k, "bad", NULL, NULL, buf, &cb), ERROR_SUCCESS);
    T_TRUE(cb == 4 && !memcmp(buf, "aA?", 4));
    HKEY root = k;
    T_EQ(rt_reg_create_key_ex_w(root, bad, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL), ERROR_SUCCESS);
    cl(k);
    char name[16];
    DWORD cch = 16;
    T_EQ(rt_reg_enum_key_ex_a(root, 0, name, &cch, NULL, NULL, NULL, NULL), ERROR_SUCCESS);
    T_TRUE(cch == 3 && !memcmp(name, "aA?", 4));

    T_CASE("a string of an odd number of bytes cannot be converted: it stops the runtime, naming the type");
    static const BYTE odd[5] = { 'a', 0, 'b', 0, 'c' };
    T_EQ(rt_reg_set_value_ex_w(root, W("odd"), 0, REG_SZ, odd, 5), ERROR_SUCCESS);
    T_TRAPS(rt_reg_query_value_ex_a(root, "odd", NULL, NULL, NULL, &cb), "not a whole number");
    cb = 5;
    T_EQ(rt_reg_query_value_ex_w(root, W("odd"), NULL, NULL, buf, &cb), ERROR_SUCCESS);   /* the W function returns it as stored */
    T_EQ(cb, 5);
    cl(root);
}

TEST(core, registry_ansi_enum_value_data)
{
    T_CASE("RegEnumValueA converts the data of a string value like RegQueryValueExA and sizes it in bytes");
    HKEY k = mka(HKEY_CURRENT_USER, "Software\\AnsiEnumData");
    T_EQ(set_a(k, "s", REG_SZ, "ab\xE9", 4), ERROR_SUCCESS);
    char name[8];
    BYTE buf[16];
    DWORD cch = 8, cb = 16, type = 0;
    T_EQ(rt_reg_enum_value_a(k, 0, name, &cch, NULL, &type, buf, &cb), ERROR_SUCCESS);
    T_TRUE(cch == 1 && name[0] == 's' && type == REG_SZ && cb == 4 && !memcmp(buf, "ab\xE9", 4));
    cch = 8; cb = 3;
    T_EQ(rt_reg_enum_value_a(k, 0, name, &cch, NULL, &type, buf, &cb), ERROR_MORE_DATA);
    T_EQ(cb, 4);
    cch = 8;
    T_EQ(rt_reg_enum_value_a(k, 0, name, &cch, NULL, NULL, NULL, &cb), ERROR_SUCCESS);
    T_EQ(cb, 4);
    cl(k);
}

TEST(core, registry_ansi_class_and_info)
{
    T_CASE("the class goes in through RegCreateKeyExA and comes out through the A functions in code page 1252");
    HKEY root = mka(HKEY_CURRENT_USER, "Software\\AnsiClass");
    HKEY k = NULL;
    T_EQ(rt_reg_create_key_ex_a(root, "K", 0, "cl\xE4ss", 0, KEY_ALL_ACCESS, NULL, &k, NULL), ERROR_SUCCESS);
    cl(k);
    char name[8], cls[8];
    DWORD cn = 8, cc = 8;
    T_EQ(rt_reg_enum_key_ex_a(root, 0, name, &cn, NULL, cls, &cc, NULL), ERROR_SUCCESS);
    T_TRUE(cn == 1 && cc == 5 && !memcmp(cls, "cl\xE4ss", 6));
    cc = 5;
    T_EQ(rt_reg_enum_key_ex_a(root, 0, name, &cn, NULL, cls, &cc, NULL), ERROR_MORE_DATA);

    T_CASE("the same class through the W function is the UTF-16 text");
    WCHAR wcls[8];
    cn = 8; cc = 8;
    WCHAR wname[8];
    T_EQ(rt_reg_enum_key_ex_w(root, 0, wname, &cn, NULL, wcls, &cc, NULL), ERROR_SUCCESS);
    static const WCHAR want[] = { 'c', 'l', 0xE4, 's', 's', 0 };
    T_TRUE(!memcmp(wcls, want, sizeof want));

    T_CASE("RegQueryInfoKeyA: the key's own class in the code page, the counts, and the sizes as the W function says");
    HKEY kk = op(root, "K", KEY_READ);
    cc = 8;
    DWORD ns = 9, nv = 9;
    T_EQ(rt_reg_query_info_key_a(kk, cls, &cc, NULL, &ns, NULL, NULL, &nv, NULL, NULL, NULL, NULL), ERROR_SUCCESS);
    T_TRUE(cc == 5 && !memcmp(cls, "cl\xE4ss", 6) && ns == 0 && nv == 0);
    cc = 4;
    T_EQ(rt_reg_query_info_key_a(kk, cls, &cc, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ERROR_MORE_DATA);
    T_EQ(cc, 5);
    cl(kk);
    DWORD sub_w, sub_a, vl_w, vl_a, nm_w, nm_a;
    T_EQ(rt_reg_query_info_key_w(root, NULL, NULL, NULL, NULL, &sub_w, NULL, NULL, &nm_w, &vl_w, NULL, NULL), ERROR_SUCCESS);
    T_EQ(rt_reg_query_info_key_a(root, NULL, NULL, NULL, NULL, &sub_a, NULL, NULL, &nm_a, &vl_a, NULL, NULL), ERROR_SUCCESS);
    T_TRUE(sub_w == sub_a && vl_w == vl_a && nm_w == nm_a);
    cl(root);
}

TEST(core, registry_ansi_delete_key)
{
    T_CASE("RegDeleteKeyExA deletes a key by its converted name");
    HKEY root = mka(HKEY_CURRENT_USER, "Software\\AnsiDel");
    cl(mka(root, "d\xE9l"));
    HKEY probe = NULL;
    T_EQ(rt_reg_open_key_ex_a(root, "D\xC9L", 0, KEY_READ, &probe), ERROR_SUCCESS);
    cl(probe);
    T_EQ(rt_reg_delete_key_ex_a(root, "D\xC9L", 0, 0), ERROR_SUCCESS);
    T_EQ(rt_reg_delete_key_ex_a(root, "d\xE9l", 0, 0), ERROR_FILE_NOT_FOUND);
    T_EQ(rt_reg_delete_key_ex_a(root, NULL, 0, 0), ERROR_INVALID_PARAMETER);
    cl(root);
}

TEST(core, registry_ansi_limits)
{
    T_CASE("the limits of names count bytes: 255 is accepted, 256 stops the runtime");
    char name[300];
    memset(name, 'k', 255); name[255] = 0;
    HKEY k = mka(HKEY_CURRENT_USER, name);
    cl(k);
    memset(name, 'k', 256); name[256] = 0;
    T_TRAPS(rt_reg_create_key_ex_a(HKEY_CURRENT_USER, name, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL), "255");

    T_CASE("a value name of 16,383 bytes is accepted, 16,384 stops the runtime");
    static char big[17000];
    memset(big, 'v', 16383); big[16383] = 0;
    HKEY h = mka(HKEY_CURRENT_USER, "Software\\AnsiLimits");
    T_EQ(set_a(h, big, REG_DWORD, "\1\0\0\0", 4), ERROR_SUCCESS);
    big[16383] = 'v'; big[16384] = 0;
    T_TRAPS(set_a(h, big, REG_DWORD, "\1\0\0\0", 4), "16384");
    T_TRAPS(rt_reg_query_value_ex_a(h, big, NULL, NULL, NULL, NULL), "16384");
    T_TRAPS(rt_reg_delete_value_a(h, big), "16384");

    T_CASE("a subkey string of more than 32,767 bytes stops the runtime");
    static char path[40000];
    size_t n = 0;
    for (int comp = 0; comp < 128; comp++) {
        if (comp) path[n++] = '\\';
        memset(path + n, 'a' + comp % 26, 255);
        n += 255;
    }
    path[n] = 0;
    T_EQ(n, 32767);
    HKEY deep = mka(HKEY_CURRENT_USER, path);
    cl(deep);
    path[n++] = '\\'; path[n++] = 'x'; path[n] = 0;
    T_TRAPS(rt_reg_open_key_ex_a(HKEY_CURRENT_USER, path, 0, KEY_READ, &k), "32767");

    T_CASE("a class of 255 bytes is accepted, 256 stops the runtime");
    memset(name, 'c', 255); name[255] = 0;
    T_EQ(rt_reg_create_key_ex_a(h, "WithBigClass", 0, name, 0, KEY_ALL_ACCESS, NULL, &k, NULL), ERROR_SUCCESS);
    cl(k);
    memset(name, 'c', 256); name[256] = 0;
    T_TRAPS(rt_reg_create_key_ex_a(h, "WithBiggerClass", 0, name, 0, KEY_ALL_ACCESS, NULL, &k, NULL), "255");
    cl(h);
}
