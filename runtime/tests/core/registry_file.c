/* The registry (rt/registry.h): the file.  When it is saved, what it says, that it reads back to
 * the same tree, that two saves of one tree are the same bytes, that a save is atomic.  The
 * parser's errors and what it accepts are in registry_file_parse.c; the seed's part in the file
 * is in registry_seed.c. */
#include "registry_util.c"

/* Replace every "time=<16 hex digits>" with zeros, so that text can be compared. */
static void scrub(char *t) {
    for (char *p = t; (p = strstr(p, "time=")) != NULL; p += 5) {
        int ok = 1;
        for (int i = 0; i < 16; i++) if (!((p[5 + i] >= '0' && p[5 + i] <= '9') || (p[5 + i] >= 'A' && p[5 + i] <= 'F'))) ok = 0;
        if (ok) memset(p + 5, '0', 16);
    }
}

static char *scrubbed_text(void) {
    char *t = reg_text(NULL);
    if (t) scrub(t);
    return t;
}

/* The file's text equals the sections (after the header), times scrubbed. */
static bool file_is(const char *sections) {
    char *got = scrubbed_text();
    char *want = with_header(sections);
    bool same = got && !strcmp(got, want);
    if (!same) fprintf(stderr, "---- file is:\n%s---- expected:\n%s----\n", got ? got : "(no file)\n", want);
    free(got);
    free(want);
    return same;
}

static unsigned long long inode_of(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 ? (unsigned long long)st.st_ino : 0;
}

/* ---- when it is written --------------------------------------------------------------------------- */

TEST(core, registry_file_absent_until_a_change)
{
    T_CASE("there is no file until the guest changes something; start-up and reads write nothing");
    T_TRUE(!exists(rt_registry_path()));
    T_TRUE(!exists(state_path("registry.txt.tmp")));
    HKEY k = NULL;
    T_EQ(rt_reg_open_key_ex_w(HKEY_LOCAL_MACHINE, W("SOFTWARE\\Microsoft"), 0, KEY_READ, &k), ERROR_SUCCESS);
    BYTE buf[64];
    DWORD cb = sizeof buf;
    HKEY cur = NULL;
    T_EQ(rt_reg_open_key_ex_w(HKEY_LOCAL_MACHINE, W("SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"), 0, KEY_READ, &cur), ERROR_SUCCESS);
    T_EQ(rt_reg_query_value_ex_w(cur, W("CurrentVersion"), NULL, NULL, buf, &cb), ERROR_SUCCESS);
    T_EQ(rt_reg_query_value_ex_w(cur, W("NoSuchValue"), NULL, NULL, buf, &cb), ERROR_FILE_NOT_FOUND);
    cl(cur);
    cl(k);
    T_TRUE(!exists(rt_registry_path()));

    T_CASE("the path is registry.txt in the state directory, and nothing else is in it but the guest's directories");
    T_TRUE(!strcmp(rt_registry_path(), state_path("registry.txt")));

    T_CASE("the first change creates it, whole and with no temporary left behind");
    HKEY made = mk(HKEY_CURRENT_USER, "Software\\First");
    cl(made);
    T_TRUE(exists(rt_registry_path()));
    T_TRUE(!exists(state_path("registry.txt.tmp")));
    T_TRUE(file_is("\n[HKEY_CURRENT_USER\\Software] time=0000000000000000\n"
                   "\n[HKEY_CURRENT_USER\\Software\\First] time=0000000000000000\n"));
}

TEST(core, registry_file_every_change_is_saved)
{
    T_CASE("each of create, set, delete value and delete key is on disk when the call returns");
    HKEY k = mk(HKEY_CURRENT_USER, "Software\\Each");
    T_TRUE(contains(reg_text(NULL), "[HKEY_CURRENT_USER\\Software\\Each]"));
    T_EQ(put_dw(k, "v", 1), ERROR_SUCCESS);
    char *t = reg_text(NULL);
    T_TRUE(contains(t, "\"v\" = dword:00000001\n"));
    free(t);
    T_EQ(put_dw(k, "v", 2), ERROR_SUCCESS);
    t = reg_text(NULL);
    T_TRUE(contains(t, "\"v\" = dword:00000002\n") && !contains(t, "dword:00000001"));
    free(t);
    T_EQ(rt_reg_delete_value_w(k, W("v")), ERROR_SUCCESS);
    t = reg_text(NULL);
    T_TRUE(!contains(t, "\"v\""));
    free(t);
    HKEY sub = mk(k, "Sub");
    cl(sub);
    t = reg_text(NULL);
    T_TRUE(contains(t, "[HKEY_CURRENT_USER\\Software\\Each\\Sub]"));
    free(t);
    T_EQ(rt_reg_delete_key_ex_w(k, W("Sub"), 0, 0), ERROR_SUCCESS);
    t = reg_text(NULL);
    T_TRUE(!contains(t, "Sub]") && contains(t, "[HKEY_CURRENT_USER\\Software\\Each]"));
    free(t);
    cl(k);

    T_CASE("a key made on the way to a deeper key is saved with it");
    cl(mk(HKEY_CURRENT_USER, "Software\\Way\\To\\Deep"));
    t = reg_text(NULL);
    T_TRUE(contains(t, "[HKEY_CURRENT_USER\\Software\\Way]") && contains(t, "[HKEY_CURRENT_USER\\Software\\Way\\To]") &&
           contains(t, "[HKEY_CURRENT_USER\\Software\\Way\\To\\Deep]"));
    free(t);
}

TEST(core, registry_file_unchanged_text_writes_nothing)
{
    HKEY k = mk(HKEY_CURRENT_USER, "Software\\Quiet");
    T_EQ(put_dw(k, "v", 1), ERROR_SUCCESS);
    unsigned long long ino = inode_of(rt_registry_path());
    T_TRUE(ino != 0);

    T_CASE("a value written with the type and data it has");
    T_EQ(put_dw(k, "v", 1), ERROR_SUCCESS);
    T_EQ(inode_of(rt_registry_path()), ino);

    T_CASE("reads and failed calls");
    DWORD cb = 4, v = 0;
    T_EQ(rt_reg_query_value_ex_w(k, W("v"), NULL, NULL, (BYTE *)&v, &cb), ERROR_SUCCESS);
    T_EQ(rt_reg_delete_value_w(k, W("missing")), ERROR_FILE_NOT_FOUND);
    T_EQ(rt_reg_delete_key_ex_w(k, W("missing"), 0, 0), ERROR_FILE_NOT_FOUND);
    T_EQ(rt_reg_set_value_ex_w(HKEY_LOCAL_MACHINE, W("v"), 0, REG_DWORD, (const BYTE *)&v, 4), ERROR_ACCESS_DENIED);
    HKEY ro = op(HKEY_CURRENT_USER, "Software\\Quiet", KEY_READ);
    T_EQ(put_dw(ro, "v", 5), ERROR_ACCESS_DENIED);
    cl(ro);
    T_EQ(inode_of(rt_registry_path()), ino);

    T_CASE("changes inside a volatile key; making or deleting the volatile key itself changes its parent's last write time, which is saved");
    HKEY vol = NULL;
    T_EQ(rt_reg_create_key_ex_w(k, W("Vol"), 0, NULL, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &vol, NULL), ERROR_SUCCESS);
    unsigned long long ino2 = inode_of(rt_registry_path());
    T_TRUE(ino2 != ino);
    T_EQ(put_dw(vol, "x", 1), ERROR_SUCCESS);
    T_EQ(put_sz(vol, "y", "z"), ERROR_SUCCESS);
    T_EQ(rt_reg_delete_value_w(vol, W("x")), ERROR_SUCCESS);
    HKEY deeper = NULL;
    T_EQ(rt_reg_create_key_ex_w(vol, W("Deeper"), 0, NULL, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &deeper, NULL), ERROR_SUCCESS);
    cl(deeper);
    T_EQ(rt_reg_delete_key_ex_w(vol, W("Deeper"), 0, 0), ERROR_SUCCESS);
    cl(vol);
    T_EQ(inode_of(rt_registry_path()), ino2);
    T_EQ(rt_reg_delete_key_ex_w(k, W("Vol"), 0, 0), ERROR_SUCCESS);
    T_TRUE(inode_of(rt_registry_path()) != ino2);

    T_CASE("a real change writes a new file");
    unsigned long long ino3 = inode_of(rt_registry_path());
    T_EQ(put_dw(k, "v", 2), ERROR_SUCCESS);
    T_TRUE(inode_of(rt_registry_path()) != ino3);
    cl(k);
}

TEST(core, registry_file_canonical_file_is_not_rewritten)
{
    T_CASE("a file the runtime wrote is read back as it is, and a change that leaves its text alone does not rewrite it");
    HKEY k = mk(HKEY_CURRENT_USER, "Software\\Canon");
    T_EQ(put_dw(k, "v", 1), ERROR_SUCCESS);
    cl(k);
    unsigned long long ino = inode_of(rt_registry_path());
    rt_registry_test_reload();
    HKEY v = NULL;
    T_EQ(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("VolTop"), 0, NULL, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &v, NULL), ERROR_SUCCESS);
    cl(v);
    T_EQ(inode_of(rt_registry_path()), ino);

    T_CASE("but a file that is not in the runtime's form is rewritten by the first change, even one that adds nothing");
    put_file("titanfall-registry 1\n# a person's edit\n[HKEY_CURRENT_USER\\Software] time=0000000000000001\n"
             "[HKEY_CURRENT_USER\\Software\\Canon] time=0000000000000002\n\"v\"   =   dword:00000001\n");
    rt_registry_test_reload();
    unsigned long long edited = inode_of(rt_registry_path());
    T_EQ(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("VolTop"), 0, NULL, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &v, NULL), ERROR_SUCCESS);
    cl(v);
    T_TRUE(inode_of(rt_registry_path()) != edited);
    char *t = reg_text(NULL);
    T_TRUE(contains(t, "\"v\" = dword:00000001\n") && !contains(t, "person's"));
    free(t);
}

/* ---- what it says --------------------------------------------------------------------------------- */

TEST(core, registry_file_text_of_every_form)
{
    T_CASE("each type is written in the first form that holds it exactly");
    HKEY k = mk(HKEY_CURRENT_USER, "Software\\Forms");
    static const WCHAR abc0[] = { 'a', 'b', 'c', 0 };
    static const WCHAR abc[] = { 'a', 'b', 'c' };
    static const WCHAR multi[] = { 'o', 'n', 'e', 0, 't', 'w', 'o', 0, 0 };
    static const WCHAR multi_empty_mid[] = { 'a', 0, 0, 'b', 0, 0 };
    static const WCHAR multi_one_nul[] = { 'a', 0 };
    static const WCHAR multi_blank[] = { 0 };
    static const WCHAR with_nul_inside[] = { 'a', 0, 'b', 0 };
    T_EQ(put_raw(k, NULL, REG_SZ, abc0, 8), ERROR_SUCCESS);
    T_EQ(put_raw(k, "sz", REG_SZ, abc0, 8), ERROR_SUCCESS);
    T_EQ(put_raw(k, "szempty", REG_SZ, abc0 + 3, 2), ERROR_SUCCESS);
    T_EQ(put_raw(k, "szinside", REG_SZ, with_nul_inside, 8), ERROR_SUCCESS);
    T_EQ(put_raw(k, "expand", REG_EXPAND_SZ, abc0, 8), ERROR_SUCCESS);
    T_EQ(put_raw(k, "nonul", REG_SZ, abc, 6), ERROR_SUCCESS);
    T_EQ(put_raw(k, "odd", REG_SZ, abc0, 7), ERROR_SUCCESS);
    T_EQ(put_raw(k, "szzero", REG_SZ, NULL, 0), ERROR_SUCCESS);
    T_EQ(put_raw(k, "multi", REG_MULTI_SZ, multi, 18), ERROR_SUCCESS);
    T_EQ(put_raw(k, "multiempty", REG_MULTI_SZ, multi_blank, 2), ERROR_SUCCESS);
    T_EQ(put_raw(k, "multimid", REG_MULTI_SZ, multi_empty_mid, 12), ERROR_SUCCESS);
    T_EQ(put_raw(k, "multione", REG_MULTI_SZ, multi_one_nul, 4), ERROR_SUCCESS);
    T_EQ(put_dw(k, "dword", 0xDEADBEEF), ERROR_SUCCESS);
    T_EQ(put_raw(k, "dword8", REG_DWORD, "\1\2\3\4\5\6\7\x08", 8), ERROR_SUCCESS);
    T_EQ(put_raw(k, "qword", REG_QWORD, "\xEF\xCD\xAB\x89\x67\x45\x23\x01", 8), ERROR_SUCCESS);
    T_EQ(put_raw(k, "qword4", REG_QWORD, "\1\2\3\4", 4), ERROR_SUCCESS);
    T_EQ(put_raw(k, "bin", REG_BINARY, "\0\xFF\x0A", 3), ERROR_SUCCESS);
    T_EQ(put_raw(k, "binempty", REG_BINARY, NULL, 0), ERROR_SUCCESS);
    T_EQ(put_raw(k, "none", REG_NONE, "\1", 1), ERROR_SUCCESS);
    T_EQ(put_raw(k, "dwbe", REG_DWORD_BIG_ENDIAN, "\0\0\0\1", 4), ERROR_SUCCESS);
    T_EQ(put_raw(k, "huge", 0xFFFFFFFFu, "\2", 1), ERROR_SUCCESS);
    cl(k);
    T_TRUE(file_is(
        "\n[HKEY_CURRENT_USER\\Software] time=0000000000000000\n"
        "\n[HKEY_CURRENT_USER\\Software\\Forms] time=0000000000000000\n"
        "@ = sz:\"abc\"\n"
        "\"sz\" = sz:\"abc\"\n"
        "\"szempty\" = sz:\"\"\n"
        "\"szinside\" = sz:\"a\\u0000b\"\n"
        "\"expand\" = expand_sz:\"abc\"\n"
        "\"nonul\" = hex(1):61,00,62,00,63,00\n"
        "\"odd\" = hex(1):61,00,62,00,63,00,00\n"
        "\"szzero\" = hex(1):\n"
        "\"multi\" = multi_sz:\"one\",\"two\"\n"
        "\"multiempty\" = multi_sz:\n"
        "\"multimid\" = hex(7):61,00,00,00,00,00,62,00,00,00,00,00\n"
        "\"multione\" = hex(7):61,00,00,00\n"
        "\"dword\" = dword:deadbeef\n"
        "\"dword8\" = hex(4):01,02,03,04,05,06,07,08\n"
        "\"qword\" = qword:0123456789abcdef\n"
        "\"qword4\" = hex(11):01,02,03,04\n"
        "\"bin\" = hex:00,ff,0a\n"
        "\"binempty\" = hex:\n"
        "\"none\" = hex(0):01\n"
        "\"dwbe\" = hex(5):00,00,00,01\n"
        "\"huge\" = hex(4294967295):02\n"));
}

TEST(core, registry_file_names_escaped)
{
    T_CASE("names are written as printable ASCII with escapes, and read back to the same names");
    static const WCHAR odd_key[] = { 'O', 'd', 'd', ' ', '[', 'N', ']', ' ', '1', '0', '0', '%', 0 };
    static const WCHAR uni_key[] = { 'c', 'a', 'f', 0xE9, ' ', 0x20AC, 0 };
    static const WCHAR astral_key[] = { 0xD83D, 0xDE00, 0 };
    static const WCHAR lone_key[] = { 'x', 0xD800, 0 };
    static const WCHAR quote_key[] = { 'q', '"', 'u', 0 };
    static const WCHAR ctrl_key[] = { 'a', 1, 0x7F, 'z', 0 };
    HKEY root = mk(HKEY_CURRENT_USER, "Software\\Esc");
    const WCHAR *keys[] = { odd_key, uni_key, astral_key, lone_key, quote_key, ctrl_key };
    for (unsigned i = 0; i < 6; i++) {
        HKEY k = NULL;
        T_EQ(rt_reg_create_key_ex_w(root, keys[i], 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL), ERROR_SUCCESS);
        cl(k);
    }
    static const WCHAR v1[] = { 'a', '\\', 'b', 0 }, v2[] = { 'q', '"', 'u', 'o', 't', 'e', 0 }, v3[] = { 'n', 'l', '\n', 0 },
                       v4[] = { 0x20AC, 0xE9, 0 }, v5[] = { ' ', 'l', 'e', 'a', 'd', ' ', 0 }, v6[] = { '@', 0 }, v7[] = { 'k', '=', 'v', 0 },
                       v8[] = { 0xDC00, 'x', 0 };
    const WCHAR *vals[] = { v1, v2, v3, v4, v5, v6, v7, v8 };
    for (unsigned i = 0; i < 8; i++) {
        DWORD x = i;
        T_EQ(rt_reg_set_value_ex_w(root, vals[i], 0, REG_DWORD, (const BYTE *)&x, 4), ERROR_SUCCESS);
    }
    static const WCHAR str[] = { 'a', '"', '\\', 'b', 0x20AC, 0xD83D, 0xDE00, '\t', 0 };
    T_EQ(rt_reg_set_value_ex_w(root, W("str"), 0, REG_SZ, (const BYTE *)str, sizeof str), ERROR_SUCCESS);
    cl(root);

    char *t = reg_text(NULL);
    T_TRUE(contains(t, "[HKEY_CURRENT_USER\\Software\\Esc\\Odd [N%u005d 100%u0025]"));
    T_TRUE(contains(t, "[HKEY_CURRENT_USER\\Software\\Esc\\caf%u00e9 %u20ac]"));
    T_TRUE(contains(t, "[HKEY_CURRENT_USER\\Software\\Esc\\%ud83d%ude00]"));
    T_TRUE(contains(t, "[HKEY_CURRENT_USER\\Software\\Esc\\x%ud800]"));
    T_TRUE(contains(t, "[HKEY_CURRENT_USER\\Software\\Esc\\q\"u]"));
    T_TRUE(contains(t, "[HKEY_CURRENT_USER\\Software\\Esc\\a%u0001%u007fz]"));
    T_TRUE(contains(t, "\"a\\\\b\" = dword:00000000\n"));
    T_TRUE(contains(t, "\"q\\\"uote\" = dword:00000001\n"));
    T_TRUE(contains(t, "\"nl\\u000a\" = dword:00000002\n"));
    T_TRUE(contains(t, "\"\\u20ac\\u00e9\" = dword:00000003\n"));
    T_TRUE(contains(t, "\" lead \" = dword:00000004\n"));
    T_TRUE(contains(t, "\"@\" = dword:00000005\n"));
    T_TRUE(contains(t, "\"k=v\" = dword:00000006\n"));
    T_TRUE(contains(t, "\"\\udc00x\" = dword:00000007\n"));
    T_TRUE(contains(t, "\"str\" = sz:\"a\\\"\\\\b\\u20ac\\ud83d\\ude00\\u0009\"\n"));
    for (const char *p = t; *p; p++) T_TRUE((unsigned char)*p < 0x80);     /* the file is ASCII */
    free(t);

    T_CASE("the tree read back from the file has the same names");
    rt_registry_test_reload();
    HKEY r = op(HKEY_CURRENT_USER, "Software\\Esc", KEY_ALL_ACCESS);
    for (unsigned i = 0; i < 6; i++) {
        HKEY k = NULL;
        T_EQ(rt_reg_open_key_ex_w(r, keys[i], 0, KEY_READ, &k), ERROR_SUCCESS);
        cl(k);
    }
    for (unsigned i = 0; i < 8; i++) {
        DWORD x = 99, cb = 4;
        T_EQ(rt_reg_query_value_ex_w(r, vals[i], NULL, NULL, (BYTE *)&x, &cb), ERROR_SUCCESS);
        T_EQ(x, i);
    }
    BYTE buf[64];
    DWORD cb = sizeof buf;
    T_EQ(rt_reg_query_value_ex_w(r, W("str"), NULL, NULL, buf, &cb), ERROR_SUCCESS);
    T_TRUE(cb == sizeof str && !memcmp(buf, str, sizeof str));
    DWORD nkeys = 0, nvals = 0;
    counts(r, &nkeys, &nvals);
    T_TRUE(nkeys == 6 && nvals == 9);

    T_CASE("@ as a name is a value of its own, distinct from the default value");
    T_TRUE(!has_value(r, NULL));
    cl(r);
}

TEST(core, registry_file_round_trip)
{
    T_CASE("every value reads back with the type and the bytes it had, in the same order, after a restart");
    HKEY k = NULL;
    T_EQ(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("Software\\Round\\Trip"), 0, W("Some Class"), 0, KEY_ALL_ACCESS, NULL, &k, NULL), ERROR_SUCCESS);
    static const struct { const char *name; DWORD type; BYTE data[40]; DWORD size; } v[] = {
        { "z-first", REG_SZ,       { 'h', 0, 'i', 0, 0, 0 }, 6 },
        { "a-second", REG_SZ,      { 'h', 0, 'i', 0 }, 4 },
        { "dw", REG_DWORD,         { 0xDE, 0xAD, 0xBE, 0xEF }, 4 },
        { "dw3", REG_DWORD,        { 1, 2, 3 }, 3 },
        { "qw", REG_QWORD,         { 1, 2, 3, 4, 5, 6, 7, 8 }, 8 },
        { "bin", REG_BINARY,       { 0, 1, 2, 0xFE, 0xFF }, 5 },
        { "binzero", REG_BINARY,   { 0 }, 0 },
        { "multi", REG_MULTI_SZ,   { 'a', 0, 0, 0, 'b', 'c', 0, 0, 0, 0 }, 10 },
        { "multi2", REG_MULTI_SZ,  { 'a', 0, 0, 0, 0, 0, 'b', 0, 0, 0, 0, 0 }, 12 },
        { "exp", REG_EXPAND_SZ,    { '%', 0, 'x', 0, '%', 0, 0, 0 }, 8 },
        { "link", REG_LINK,        { 1, 2 }, 2 },
        { "none", REG_NONE,        { 9 }, 1 },
        { "t12", 12,               { 1, 2, 3 }, 3 },
        { "tmax", 0xFFFFFFFFu,     { 7 }, 1 },
    };
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++)
        T_EQ(put_raw(k, v[i].name, v[i].type, v[i].data, v[i].size), ERROR_SUCCESS);
    T_EQ(put_sz(k, NULL, "the default"), ERROR_SUCCESS);
    cl(k);
    HKEY sub = mk(HKEY_CURRENT_USER, "Software\\Round\\Trip\\Zeta");
    cl(sub);
    cl(mk(HKEY_CURRENT_USER, "Software\\Round\\Trip\\alpha"));
    FILETIME t_before;
    HKEY again = op(HKEY_CURRENT_USER, "Software\\Round\\Trip", KEY_READ);
    T_EQ(rt_reg_query_info_key_w(again, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &t_before), ERROR_SUCCESS);
    cl(again);

    rt_registry_test_reload();

    HKEY r = op(HKEY_CURRENT_USER, "Software\\Round\\Trip", KEY_READ);
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
        BYTE buf[64];
        memset(buf, 0xCC, sizeof buf);
        DWORD cb = sizeof buf, type = 0;
        T_EQ(rt_reg_query_value_ex_w(r, W(v[i].name), NULL, &type, buf, &cb), ERROR_SUCCESS);
        T_TRUE(type == v[i].type && cb == v[i].size && !memcmp(buf, v[i].data, v[i].size));
    }
    T_TRUE(is_sz(r, NULL, "the default"));
    char name[32];
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++)
        T_TRUE(value_name(r, i, name, sizeof name) && !strcmp(name, v[i].name));
    T_TRUE(value_name(r, sizeof v / sizeof v[0], name, sizeof name) && !strcmp(name, ""));      /* created last */
    T_TRUE(key_name(r, 0, name, sizeof name) && !strcmp(name, "alpha"));
    T_TRUE(key_name(r, 1, name, sizeof name) && !strcmp(name, "Zeta"));

    T_CASE("the class and the last write time are kept");
    cl(r);
    HKEY parent = op(HKEY_CURRENT_USER, "Software\\Round", KEY_READ);
    WCHAR wname[16], wcls[16];
    DWORD cn = 16, cc = 16;
    FILETIME t_after;
    T_EQ(rt_reg_enum_key_ex_w(parent, 0, wname, &cn, NULL, wcls, &cc, &t_after), ERROR_SUCCESS);
    T_TRUE(weq(wname, "Trip") && weq(wcls, "Some Class"));
    T_TRUE(t_after.dwLowDateTime == t_before.dwLowDateTime && t_after.dwHighDateTime == t_before.dwHighDateTime);
    cl(parent);
}

TEST(core, registry_file_volatile_not_saved)
{
    T_CASE("volatile keys and what is in them are not written, and are gone after a restart");
    HKEY stable = mk(HKEY_CURRENT_USER, "Software\\Stay");
    T_EQ(put_dw(stable, "v", 1), ERROR_SUCCESS);
    HKEY vol = NULL;
    T_EQ(rt_reg_create_key_ex_w(stable, W("Vanishing"), 0, W("cls"), REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &vol, NULL), ERROR_SUCCESS);
    T_EQ(put_sz(vol, "x", "y"), ERROR_SUCCESS);
    HKEY deeper = NULL;
    T_EQ(rt_reg_create_key_ex_w(vol, W("Deeper"), 0, NULL, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &deeper, NULL), ERROR_SUCCESS);
    cl(deeper);
    cl(vol);
    char *t = reg_text(NULL);
    T_TRUE(contains(t, "[HKEY_CURRENT_USER\\Software\\Stay]") && !contains(t, "Vanishing") && !contains(t, "Deeper"));
    free(t);
    cl(stable);
    rt_registry_test_reload();
    T_TRUE(has_key(HKEY_CURRENT_USER, "Software\\Stay"));
    T_TRUE(!has_key(HKEY_CURRENT_USER, "Software\\Stay\\Vanishing"));
}

/* Make the runtime rewrite the file from its tree without changing any time that is saved: a
 * volatile key directly below HKEY_CURRENT_USER changes only the last write time of that root,
 * which is not saved. */
static void rewrite_canonical(void) {
    HKEY v = NULL;
    T_EQ(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("RewriteTrigger"), 0, NULL, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &v, NULL), ERROR_SUCCESS);
    cl(v);
}

TEST(core, registry_file_section_order)
{
    T_CASE("the roots come in the order HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER, HKEY_USERS; subkeys sorted; values as created");
    put_file("titanfall-registry 1\n"
             "[HKEY_USERS\\S-1-5-21-1] time=0000000000000001\n"
             "[HKEY_CURRENT_USER\\Software] time=0000000000000002\n"
             "[HKEY_CURRENT_USER\\Software\\b] time=0000000000000003\n"
             "[HKEY_CURRENT_USER\\Software\\B2] time=0000000000000004\n"
             "[HKEY_CURRENT_USER\\Software\\a] time=0000000000000005\n"
             "\"second\" = dword:00000002\n"
             "\"First\" = dword:00000001\n"
             "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Vendor] time=0000000000000006\n");
    rt_registry_test_reload();
    rewrite_canonical();
    /* The HKLM\SOFTWARE ancestor is seeded; it is written because something below it is not. */
    T_TRUE(file_is(
        "\n[HKEY_LOCAL_MACHINE\\SOFTWARE] time=0000000000000000\n"
        "\n[HKEY_LOCAL_MACHINE\\SOFTWARE\\Vendor] time=0000000000000000\n"
        "\n[HKEY_CURRENT_USER\\Software] time=0000000000000000\n"
        "\n[HKEY_CURRENT_USER\\Software\\a] time=0000000000000000\n"
        "\"second\" = dword:00000002\n"
        "\"First\" = dword:00000001\n"
        "\n[HKEY_CURRENT_USER\\Software\\b] time=0000000000000000\n"
        "\n[HKEY_CURRENT_USER\\Software\\B2] time=0000000000000000\n"
        "\n[HKEY_USERS\\S-1-5-21-1] time=0000000000000000\n"));
}

TEST(core, registry_file_times_kept)
{
    T_CASE("a time written in the file is the key's last write time, and is written back exactly");
    put_file("titanfall-registry 1\n"
             "[HKEY_CURRENT_USER\\Software] time=01DC0123456789AB\n"
             "[HKEY_CURRENT_USER\\Software\\Timed] time=01dc0123456789ab\n"
             "[HKEY_CURRENT_USER\\Software\\Untimed]\n");
    rt_registry_test_reload();
    HKEY sw = op(HKEY_CURRENT_USER, "Software", KEY_READ);
    WCHAR name[16];
    DWORD cn = 16;
    FILETIME ft;
    T_EQ(rt_reg_enum_key_ex_w(sw, 0, name, &cn, NULL, NULL, NULL, &ft), ERROR_SUCCESS);
    T_TRUE(weq(name, "Timed") && ft.dwHighDateTime == 0x01DC0123u && ft.dwLowDateTime == 0x456789ABu);
    cn = 16;
    T_EQ(rt_reg_enum_key_ex_w(sw, 1, name, &cn, NULL, NULL, NULL, &ft), ERROR_SUCCESS);
    T_TRUE(weq(name, "Untimed") && ft.dwHighDateTime >= 0x01D00000u);           /* a key without a time got the time of the start */
    FILETIME sft;
    T_EQ(rt_reg_query_info_key_w(sw, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &sft), ERROR_SUCCESS);
    T_TRUE(sft.dwHighDateTime == 0x01DC0123u && sft.dwLowDateTime == 0x456789ABu);
    cl(sw);

    T_CASE("and written back exactly, in capitals, when the file is rewritten");
    rewrite_canonical();
    char *t = reg_text(NULL);
    T_TRUE(contains(t, "[HKEY_CURRENT_USER\\Software] time=01DC0123456789AB\n"));
    T_TRUE(contains(t, "[HKEY_CURRENT_USER\\Software\\Timed] time=01DC0123456789AB\n"));
    free(t);
}
