/* The registry (rt/registry.h): values.  Types kept exactly, the default value, the buffer rules of
 * RegQueryValueEx (size query, ERROR_MORE_DATA, exact fit), strings stored with and without their
 * terminator, replacing and deleting values, the limits.  What is behind these: advapi32's
 * RegSetValueEx, RegQueryValueEx and RegDeleteValue. */
#include "registry_util.c"

#define SENTINEL 0xA5

static HKEY fresh(const char *name) { return mk(HKEY_CURRENT_USER, name); }

/* ---- types kept exactly ----------------------------------------------------------------------- */

TEST(core, registry_value_types_exact)
{
    T_CASE("every value type is returned with the type and the bytes it was written with");
    HKEY k = fresh("Software\\Types");
    struct { DWORD type; const char *name; BYTE data[24]; DWORD size; } t[] = {
        { REG_NONE,        "none",     { 1, 2, 3 }, 3 },
        { REG_SZ,          "sz",       { 'h', 0, 'i', 0, 0, 0 }, 6 },
        { REG_EXPAND_SZ,   "expand",   { '%', 0, 'P', 0, '%', 0, 0, 0 }, 8 },
        { REG_BINARY,      "binary",   { 0, 0xFF, 0x80, 0x7F, 1 }, 5 },
        { REG_DWORD,       "dword",    { 0x78, 0x56, 0x34, 0x12 }, 4 },
        { REG_DWORD_BIG_ENDIAN, "dwbe", { 0x12, 0x34, 0x56, 0x78 }, 4 },
        { REG_LINK,        "link",     { 'a', 0 }, 2 },
        { REG_MULTI_SZ,    "multi",    { 'a', 0, 0, 0, 'b', 0, 0, 0, 0, 0 }, 10 },
        { REG_RESOURCE_LIST, "res",    { 9, 8, 7, 6 }, 4 },
        { REG_FULL_RESOURCE_DESCRIPTOR, "fullres", { 1 }, 1 },
        { REG_RESOURCE_REQUIREMENTS_LIST, "reqs", { 2, 2 }, 2 },
        { REG_QWORD,       "qword",    { 1, 2, 3, 4, 5, 6, 7, 8 }, 8 },
        { 12,              "type12",   { 4 }, 1 },
        { 0x12345678,      "huge",     { 5, 5 }, 2 },
        { 0xFFFFFFFFu,     "allones",  { 6 }, 1 },
    };
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
        T_EQ(put_raw(k, t[i].name, t[i].type, t[i].data, t[i].size), ERROR_SUCCESS);
        BYTE buf[64];
        memset(buf, SENTINEL, sizeof buf);
        DWORD cb = sizeof buf, type = 0;
        T_EQ(rt_reg_query_value_ex_w(k, W(t[i].name), NULL, &type, buf, &cb), ERROR_SUCCESS);
        T_EQ(type, t[i].type);
        T_EQ(cb, t[i].size);
        T_TRUE(!memcmp(buf, t[i].data, t[i].size));
        T_EQ(buf[t[i].size], SENTINEL);                     /* no string type here gets a terminator: all end in NUL or are not strings */
    }
    cl(k);
}

TEST(core, registry_value_sizes_not_validated)
{
    T_CASE("a type is not checked against its size: a REG_DWORD of 8 bytes stays 8 bytes");
    HKEY k = fresh("Software\\Sizes");
    BYTE eight[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    T_EQ(put_raw(k, "dw8", REG_DWORD, eight, 8), ERROR_SUCCESS);
    T_EQ(put_raw(k, "dw0", REG_DWORD, NULL, 0), ERROR_SUCCESS);
    T_EQ(put_raw(k, "qw3", REG_QWORD, eight, 3), ERROR_SUCCESS);
    BYTE buf[16];
    DWORD cb = sizeof buf, type = 0;
    T_EQ(rt_reg_query_value_ex_w(k, W("dw8"), NULL, &type, buf, &cb), ERROR_SUCCESS);
    T_TRUE(type == REG_DWORD && cb == 8 && !memcmp(buf, eight, 8));
    cb = sizeof buf;
    T_EQ(rt_reg_query_value_ex_w(k, W("dw0"), NULL, &type, buf, &cb), ERROR_SUCCESS);
    T_TRUE(type == REG_DWORD && cb == 0);
    cb = sizeof buf;
    T_EQ(rt_reg_query_value_ex_w(k, W("qw3"), NULL, &type, buf, &cb), ERROR_SUCCESS);
    T_TRUE(type == REG_QWORD && cb == 3);
    cl(k);
}

TEST(core, registry_string_as_written)
{
    T_CASE("string data is returned as stored: with a terminator, without one, with two, odd, empty");
    HKEY k = fresh("Software\\Strings");
    static const WCHAR with_nul[] = { 'a', 'b', 'c', 0 };
    static const WCHAR two_nuls[] = { 'a', 0, 0 };
    static const WCHAR no_nul[] = { 'a', 'b', 'c' };
    struct { DWORD type; const char *name; const void *data; DWORD size; } t[] = {
        { REG_SZ, "with",   with_nul, 8 },
        { REG_SZ, "two",    two_nuls, 6 },
        { REG_SZ, "none",   no_nul, 6 },
        { REG_SZ, "odd",    with_nul, 5 },
        { REG_SZ, "empty",  NULL, 0 },
        { REG_SZ, "justnul", with_nul + 3, 2 },
        { REG_EXPAND_SZ, "expnone", no_nul, 6 },
        { REG_MULTI_SZ, "multinone", no_nul, 6 },
    };
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
        T_EQ(put_raw(k, t[i].name, t[i].type, t[i].data, t[i].size), ERROR_SUCCESS);
        DWORD cb = 0, type = 0;
        T_EQ(rt_reg_query_value_ex_w(k, W(t[i].name), NULL, &type, NULL, &cb), ERROR_SUCCESS);
        T_EQ(type, t[i].type);
        T_EQ(cb, t[i].size);                                /* the stored size, never one more or less */
        BYTE buf[32];
        cb = sizeof buf;
        T_EQ(rt_reg_query_value_ex_w(k, W(t[i].name), NULL, &type, buf, &cb), ERROR_SUCCESS);
        T_EQ(cb, t[i].size);
        T_TRUE(!t[i].size || !memcmp(buf, t[i].data, t[i].size));
    }
    cl(k);
}

/* ---- the default value and names ---------------------------------------------------------------- */

TEST(core, registry_default_value)
{
    T_CASE("the default value is the one with the NULL or the empty name");
    HKEY k = fresh("Software\\Default");
    T_TRUE(!has_value(k, NULL));
    T_EQ(put_sz(k, NULL, "via null"), ERROR_SUCCESS);
    T_TRUE(is_sz(k, NULL, "via null"));
    T_TRUE(is_sz(k, "", "via null"));
    T_EQ(put_sz(k, "", "via empty"), ERROR_SUCCESS);
    T_TRUE(is_sz(k, NULL, "via empty"));
    DWORD nvals = 0;
    counts(k, NULL, &nvals);
    T_EQ(nvals, 1);                                         /* one value, not two */

    T_CASE("it is enumerated with the empty name, and deleted by either spelling");
    WCHAR name[8] = { 'x', 'x' };
    DWORD cch = 8, type = 0;
    T_EQ(rt_reg_enum_value_w(k, 0, name, &cch, NULL, &type, NULL, NULL), ERROR_SUCCESS);
    T_TRUE(cch == 0 && name[0] == 0 && type == REG_SZ);
    T_EQ(rt_reg_delete_value_w(k, NULL), ERROR_SUCCESS);
    T_TRUE(!has_value(k, ""));
    T_EQ(put_sz(k, "", "again"), ERROR_SUCCESS);
    T_EQ(rt_reg_delete_value_w(k, W("")), ERROR_SUCCESS);
    T_EQ(rt_reg_delete_value_w(k, W("")), ERROR_FILE_NOT_FOUND);
    T_EQ(rt_reg_delete_value_w(k, NULL), ERROR_FILE_NOT_FOUND);

    T_CASE("a value whose name is the single character @ is a value like any other, not the default");
    T_EQ(put_sz(k, "@", "at"), ERROR_SUCCESS);
    T_TRUE(!has_value(k, NULL));
    T_TRUE(is_sz(k, "@", "at"));
    cl(k);
}

TEST(core, registry_value_name_case)
{
    T_CASE("value names compare ignoring case and keep the case they were created with");
    HKEY k = fresh("Software\\ValueCase");
    T_EQ(put_dw(k, "Foo", 1), ERROR_SUCCESS);
    T_EQ(put_dw(k, "FOO", 2), ERROR_SUCCESS);
    T_EQ(put_dw(k, "foo", 3), ERROR_SUCCESS);
    DWORD nvals = 0;
    counts(k, NULL, &nvals);
    T_EQ(nvals, 1);
    T_TRUE(is_dw(k, "fOO", 3));
    char name[32];
    T_TRUE(value_name(k, 0, name, sizeof name) && !strcmp(name, "Foo"));

    T_CASE("names that differ in more than case are different values");
    T_EQ(put_dw(k, "Foo ", 4), ERROR_SUCCESS);
    T_EQ(put_dw(k, "Fo", 5), ERROR_SUCCESS);
    T_EQ(put_dw(k, "Fooo", 6), ERROR_SUCCESS);
    counts(k, NULL, &nvals);
    T_EQ(nvals, 4);
    T_TRUE(is_dw(k, "Foo", 3) && is_dw(k, "Foo ", 4) && is_dw(k, "Fo", 5) && is_dw(k, "Fooo", 6));

    T_CASE("a value name may hold backslashes, quotes, equals signs, spaces and non-ASCII characters");
    T_EQ(put_dw(k, "a\\b", 7), ERROR_SUCCESS);
    T_EQ(put_dw(k, "say \"hi\"", 8), ERROR_SUCCESS);
    T_EQ(put_dw(k, "k=v", 9), ERROR_SUCCESS);
    static const WCHAR uni[] = { 0x20AC, ' ', 0xE9, 0 };
    DWORD v = 10;
    T_EQ(rt_reg_set_value_ex_w(k, uni, 0, REG_DWORD, (const BYTE *)&v, 4), ERROR_SUCCESS);
    T_TRUE(is_dw(k, "a\\b", 7) && is_dw(k, "say \"hi\"", 8) && is_dw(k, "k=v", 9));
    DWORD cb = 4, got = 0;
    T_EQ(rt_reg_query_value_ex_w(k, uni, NULL, NULL, (BYTE *)&got, &cb), ERROR_SUCCESS);
    T_EQ(got, 10);
    cl(k);
}

TEST(core, registry_value_name_limits)
{
    T_CASE("a value name of 16,383 characters is accepted, one of 16,384 stops the runtime");
    HKEY k = fresh("Software\\ValueLimits");
    static WCHAR big[17000];
    for (int i = 0; i < 16383; i++) big[i] = 'n';
    big[16383] = 0;
    DWORD v = 7;
    T_EQ(rt_reg_set_value_ex_w(k, big, 0, REG_DWORD, (const BYTE *)&v, 4), ERROR_SUCCESS);
    DWORD cb = 4, got = 0;
    T_EQ(rt_reg_query_value_ex_w(k, big, NULL, NULL, (BYTE *)&got, &cb), ERROR_SUCCESS);
    T_EQ(got, 7);
    big[16383] = 'n'; big[16384] = 0;
    T_TRAPS(rt_reg_set_value_ex_w(k, big, 0, REG_DWORD, (const BYTE *)&v, 4), "16384");
    T_TRAPS(rt_reg_query_value_ex_w(k, big, NULL, NULL, NULL, NULL), "16384");
    T_TRAPS(rt_reg_delete_value_w(k, big), "16384");
    cl(k);
}

/* ---- RegQueryValueEx buffers -------------------------------------------------------------------- */

TEST(core, registry_query_buffer_rules)
{
    HKEY k = fresh("Software\\Buffers");
    BYTE data[10] = { 10, 11, 12, 13, 14, 15, 16, 17, 18, 19 };
    T_EQ(put_raw(k, "ten", REG_BINARY, data, 10), ERROR_SUCCESS);
    BYTE buf[32];
    DWORD cb, type;

    T_CASE("a NULL buffer returns the size, and the type if asked");
    cb = 0; type = 0xFFFF;
    T_EQ(rt_reg_query_value_ex_w(k, W("ten"), NULL, &type, NULL, &cb), ERROR_SUCCESS);
    T_TRUE(cb == 10 && type == REG_BINARY);
    cb = 0xFFFFFFFF;
    T_EQ(rt_reg_query_value_ex_w(k, W("ten"), NULL, NULL, NULL, &cb), ERROR_SUCCESS);
    T_EQ(cb, 10);
    cb = 99;                                                /* a NULL buffer ignores the size it is given */
    T_EQ(rt_reg_query_value_ex_w(k, W("ten"), NULL, NULL, NULL, &cb), ERROR_SUCCESS);
    T_EQ(cb, 10);

    T_CASE("with no buffer and no size, the call only says whether the value exists");
    T_EQ(rt_reg_query_value_ex_w(k, W("ten"), NULL, NULL, NULL, NULL), ERROR_SUCCESS);
    type = 0;
    T_EQ(rt_reg_query_value_ex_w(k, W("ten"), NULL, &type, NULL, NULL), ERROR_SUCCESS);
    T_EQ(type, REG_BINARY);
    T_EQ(rt_reg_query_value_ex_w(k, W("absent"), NULL, NULL, NULL, NULL), ERROR_FILE_NOT_FOUND);

    T_CASE("an exact fit copies the data and nothing after it");
    memset(buf, SENTINEL, sizeof buf);
    cb = 10;
    T_EQ(rt_reg_query_value_ex_w(k, W("ten"), NULL, &type, buf, &cb), ERROR_SUCCESS);
    T_EQ(cb, 10);
    T_TRUE(!memcmp(buf, data, 10));
    T_EQ(buf[10], SENTINEL);

    T_CASE("a larger buffer: *cb becomes the size of the data, not of the buffer");
    memset(buf, SENTINEL, sizeof buf);
    cb = 32;
    T_EQ(rt_reg_query_value_ex_w(k, W("ten"), NULL, &type, buf, &cb), ERROR_SUCCESS);
    T_EQ(cb, 10);
    T_EQ(buf[10], SENTINEL);

    T_CASE("one byte short: ERROR_MORE_DATA, the size needed, the type, and the buffer untouched");
    memset(buf, SENTINEL, sizeof buf);
    cb = 9; type = 0xFFFF;
    T_EQ(rt_reg_query_value_ex_w(k, W("ten"), NULL, &type, buf, &cb), ERROR_MORE_DATA);
    T_EQ(cb, 10);
    T_EQ(type, REG_BINARY);
    for (int i = 0; i < 32; i++) T_EQ(buf[i], SENTINEL);

    T_CASE("a zero-size buffer with a pointer is ERROR_MORE_DATA for a value that has data");
    cb = 0;
    T_EQ(rt_reg_query_value_ex_w(k, W("ten"), NULL, &type, buf, &cb), ERROR_MORE_DATA);
    T_EQ(cb, 10);
    cb = 0;
    T_EQ(rt_reg_query_value_ex_w(k, W("ten"), NULL, NULL, buf, &cb), ERROR_MORE_DATA);
    T_EQ(buf[0], SENTINEL);

    T_CASE("a value with no data fits a zero-size buffer");
    T_EQ(put_raw(k, "nothing", REG_BINARY, NULL, 0), ERROR_SUCCESS);
    cb = 0; type = 0;
    T_EQ(rt_reg_query_value_ex_w(k, W("nothing"), NULL, &type, buf, &cb), ERROR_SUCCESS);
    T_TRUE(cb == 0 && type == REG_BINARY);
    cb = 5;
    T_EQ(rt_reg_query_value_ex_w(k, W("nothing"), NULL, &type, buf, &cb), ERROR_SUCCESS);
    T_EQ(cb, 0);
    cl(k);
}

TEST(core, registry_query_parameters)
{
    HKEY k = fresh("Software\\QueryParams");
    T_EQ(put_dw(k, "v", 1), ERROR_SUCCESS);
    BYTE buf[8];
    DWORD cb = 8, type = 0x55, reserved = 0;

    T_CASE("a buffer without a size is ERROR_INVALID_PARAMETER");
    T_EQ(rt_reg_query_value_ex_w(k, W("v"), NULL, &type, buf, NULL), ERROR_INVALID_PARAMETER);
    T_EQ(type, 0x55);

    T_CASE("the reserved argument must be NULL");
    T_EQ(rt_reg_query_value_ex_w(k, W("v"), &reserved, &type, buf, &cb), ERROR_INVALID_PARAMETER);
    T_EQ(rt_reg_query_value_ex_w(k, W("v"), &reserved, NULL, NULL, NULL), ERROR_INVALID_PARAMETER);

    T_CASE("a value that is not there leaves the type, the size and the buffer alone");
    memset(buf, SENTINEL, sizeof buf);
    cb = 8; type = 0x55;
    T_EQ(rt_reg_query_value_ex_w(k, W("missing"), NULL, &type, buf, &cb), ERROR_FILE_NOT_FOUND);
    T_TRUE(type == 0x55 && cb == 8 && buf[0] == SENTINEL);
    cl(k);
}

TEST(core, registry_terminator_added_on_the_way_out)
{
    T_CASE("a string stored without its terminator gets one after it when the buffer has room for two bytes");
    HKEY k = fresh("Software\\Terminate");
    static const WCHAR abc[] = { 'a', 'b', 'c' };
    T_EQ(put_raw(k, "s", REG_SZ, abc, 6), ERROR_SUCCESS);
    BYTE buf[32];
    DWORD cb, type;
    memset(buf, SENTINEL, sizeof buf);
    cb = 32;
    T_EQ(rt_reg_query_value_ex_w(k, W("s"), NULL, &type, buf, &cb), ERROR_SUCCESS);
    T_EQ(cb, 6);                                            /* the stored size, the terminator not counted */
    T_TRUE(!memcmp(buf, abc, 6));
    T_TRUE(buf[6] == 0 && buf[7] == 0);
    T_EQ(buf[8], SENTINEL);

    T_CASE("an exact fit or room for one byte only gets no terminator");
    memset(buf, SENTINEL, sizeof buf);
    cb = 6;
    T_EQ(rt_reg_query_value_ex_w(k, W("s"), NULL, &type, buf, &cb), ERROR_SUCCESS);
    T_EQ(buf[6], SENTINEL);
    memset(buf, SENTINEL, sizeof buf);
    cb = 7;
    T_EQ(rt_reg_query_value_ex_w(k, W("s"), NULL, &type, buf, &cb), ERROR_SUCCESS);
    T_EQ(buf[6], SENTINEL);
    memset(buf, SENTINEL, sizeof buf);
    cb = 8;
    T_EQ(rt_reg_query_value_ex_w(k, W("s"), NULL, &type, buf, &cb), ERROR_SUCCESS);
    T_TRUE(buf[6] == 0 && buf[7] == 0);

    T_CASE("REG_EXPAND_SZ and REG_MULTI_SZ are strings in this rule, REG_BINARY is not");
    T_EQ(put_raw(k, "e", REG_EXPAND_SZ, abc, 6), ERROR_SUCCESS);
    T_EQ(put_raw(k, "m", REG_MULTI_SZ, abc, 6), ERROR_SUCCESS);
    T_EQ(put_raw(k, "b", REG_BINARY, abc, 6), ERROR_SUCCESS);
    const char *names[] = { "e", "m", "b" };
    for (int i = 0; i < 3; i++) {
        memset(buf, SENTINEL, sizeof buf);
        cb = 32;
        T_EQ(rt_reg_query_value_ex_w(k, W(names[i]), NULL, &type, buf, &cb), ERROR_SUCCESS);
        T_EQ(cb, 6);
        T_EQ(buf[6], i < 2 ? 0 : SENTINEL);
    }

    T_CASE("a string that ends in its terminator, an empty string, and an odd size get nothing");
    static const WCHAR ab0[] = { 'a', 'b', 0 };
    T_EQ(put_raw(k, "t", REG_SZ, ab0, 6), ERROR_SUCCESS);
    T_EQ(put_raw(k, "z", REG_SZ, NULL, 0), ERROR_SUCCESS);
    T_EQ(put_raw(k, "o", REG_SZ, abc, 5), ERROR_SUCCESS);
    const char *quiet[] = { "t", "z", "o" };
    for (int i = 0; i < 3; i++) {
        memset(buf, SENTINEL, sizeof buf);
        cb = 32;
        T_EQ(rt_reg_query_value_ex_w(k, W(quiet[i]), NULL, &type, buf, &cb), ERROR_SUCCESS);
        DWORD sz = (DWORD)cb;
        T_TRUE(buf[sz] == SENTINEL);
    }
    cl(k);
}

/* ---- RegSetValueEx ---------------------------------------------------------------------------- */

TEST(core, registry_set_value_replace)
{
    T_CASE("setting a value again replaces its type and data and keeps its place in the order");
    HKEY k = fresh("Software\\Replace");
    T_EQ(put_dw(k, "first", 1), ERROR_SUCCESS);
    T_EQ(put_sz(k, "second", "two"), ERROR_SUCCESS);
    T_EQ(put_dw(k, "third", 3), ERROR_SUCCESS);
    T_EQ(put_sz(k, "FIRST", "now a string"), ERROR_SUCCESS);
    T_TRUE(is_sz(k, "first", "now a string"));
    char name[32];
    T_TRUE(value_name(k, 0, name, sizeof name) && !strcmp(name, "first"));
    T_TRUE(value_name(k, 1, name, sizeof name) && !strcmp(name, "second"));
    T_TRUE(value_name(k, 2, name, sizeof name) && !strcmp(name, "third"));
    T_TRUE(!value_name(k, 3, name, sizeof name));
    T_EQ(put_raw(k, "first", REG_BINARY, NULL, 0), ERROR_SUCCESS);
    DWORD cb = 99, type = 0;
    T_EQ(rt_reg_query_value_ex_w(k, W("first"), NULL, &type, NULL, &cb), ERROR_SUCCESS);
    T_TRUE(type == REG_BINARY && cb == 0);
    cl(k);
}

TEST(core, registry_set_value_parameters)
{
    HKEY k = fresh("Software\\SetParams");
    DWORD v = 1;

    T_CASE("an empty value may have a NULL data pointer; data with a NULL pointer stops the runtime");
    T_EQ(rt_reg_set_value_ex_w(k, W("e"), 0, REG_BINARY, NULL, 0), ERROR_SUCCESS);
    T_TRAPS(rt_reg_set_value_ex_w(k, W("n"), 0, REG_BINARY, NULL, 4), "lpData is NULL");

    T_CASE("the reserved argument must be 0");
    T_TRAPS(rt_reg_set_value_ex_w(k, W("r"), 1, REG_DWORD, (const BYTE *)&v, 4), "Reserved is 0x1");

    T_CASE("1 MiB of data is accepted and comes back intact; one byte more stops the runtime");
    BYTE *big = malloc(RT_REG_VALUE_DATA_MAX + 1);
    T_REQUIRE(big != NULL);
    for (size_t i = 0; i < RT_REG_VALUE_DATA_MAX + 1; i++) big[i] = (BYTE)(i * 7 + (i >> 8));
    T_EQ(rt_reg_set_value_ex_w(k, W("big"), 0, REG_BINARY, big, RT_REG_VALUE_DATA_MAX), ERROR_SUCCESS);
    BYTE *back = malloc(RT_REG_VALUE_DATA_MAX + 1);
    T_REQUIRE(back != NULL);
    DWORD cb = RT_REG_VALUE_DATA_MAX;
    T_EQ(rt_reg_query_value_ex_w(k, W("big"), NULL, NULL, back, &cb), ERROR_SUCCESS);
    T_EQ(cb, RT_REG_VALUE_DATA_MAX);
    T_TRUE(!memcmp(back, big, RT_REG_VALUE_DATA_MAX));
    T_TRAPS(rt_reg_set_value_ex_w(k, W("big2"), 0, REG_BINARY, big, RT_REG_VALUE_DATA_MAX + 1), "1048577");
    T_TRUE(!has_value(k, "big2"));
    free(big);
    free(back);
    cl(k);
}

TEST(core, registry_set_value_last_write)
{
    HKEY k = fresh("Software\\LastWrite");
    FILETIME t0, t1, t2, t3;
    T_EQ(put_dw(k, "v", 1), ERROR_SUCCESS);
    T_EQ(rt_reg_query_info_key_w(k, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &t0), ERROR_SUCCESS);
    T_TRUE(t0.dwHighDateTime != 0);                         /* a date after 1601 */

    T_CASE("writing a value with the type and data it has changes nothing, the last write time included");
    usleep(5000);
    T_EQ(put_dw(k, "v", 1), ERROR_SUCCESS);
    T_EQ(rt_reg_query_info_key_w(k, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &t1), ERROR_SUCCESS);
    T_TRUE(t1.dwLowDateTime == t0.dwLowDateTime && t1.dwHighDateTime == t0.dwHighDateTime);

    T_CASE("changing the data, adding a value and deleting a value each move it forward");
    usleep(5000);
    T_EQ(put_dw(k, "v", 2), ERROR_SUCCESS);
    T_EQ(rt_reg_query_info_key_w(k, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &t2), ERROR_SUCCESS);
    uint64_t a = (uint64_t)t1.dwHighDateTime << 32 | t1.dwLowDateTime, b = (uint64_t)t2.dwHighDateTime << 32 | t2.dwLowDateTime;
    T_TRUE(b > a);
    usleep(5000);
    T_EQ(put_dw(k, "w", 2), ERROR_SUCCESS);
    T_EQ(rt_reg_query_info_key_w(k, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &t3), ERROR_SUCCESS);
    uint64_t c = (uint64_t)t3.dwHighDateTime << 32 | t3.dwLowDateTime;
    T_TRUE(c > b);
    usleep(5000);
    T_EQ(rt_reg_delete_value_w(k, W("w")), ERROR_SUCCESS);
    T_EQ(rt_reg_query_info_key_w(k, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &t3), ERROR_SUCCESS);
    T_TRUE(((uint64_t)t3.dwHighDateTime << 32 | t3.dwLowDateTime) > c);
    cl(k);
}

/* ---- RegDeleteValue ---------------------------------------------------------------------------- */

TEST(core, registry_delete_value)
{
    T_CASE("a value is deleted; deleting it again, or one that never was, is ERROR_FILE_NOT_FOUND");
    HKEY k = fresh("Software\\DeleteValue");
    T_EQ(put_dw(k, "a", 1), ERROR_SUCCESS);
    T_EQ(put_dw(k, "b", 2), ERROR_SUCCESS);
    T_EQ(put_dw(k, "c", 3), ERROR_SUCCESS);
    T_EQ(rt_reg_delete_value_w(k, W("B")), ERROR_SUCCESS);   /* case-insensitive */
    T_TRUE(!has_value(k, "b") && has_value(k, "a") && has_value(k, "c"));
    T_EQ(rt_reg_delete_value_w(k, W("b")), ERROR_FILE_NOT_FOUND);
    T_EQ(rt_reg_delete_value_w(k, W("never")), ERROR_FILE_NOT_FOUND);

    T_CASE("the values after a deleted one move up, in order");
    char name[16];
    T_TRUE(value_name(k, 0, name, sizeof name) && !strcmp(name, "a"));
    T_TRUE(value_name(k, 1, name, sizeof name) && !strcmp(name, "c"));
    T_TRUE(!value_name(k, 2, name, sizeof name));

    T_CASE("a key's subkey is not a value to delete");
    HKEY sub = mk(k, "Sub");
    cl(sub);
    T_EQ(rt_reg_delete_value_w(k, W("Sub")), ERROR_FILE_NOT_FOUND);
    T_TRUE(has_key(k, "Sub"));
    cl(k);
}
