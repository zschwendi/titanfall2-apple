/* The registry (rt/registry.h): reading the file.  What the parser accepts, what it refuses and
 * with which line number (a file that does not parse stops the runtime at start-up; the tests
 * start the runtime again with rt_registry_test_reload in a child that is expected to abort). */
#include "registry_util.c"

#define H "titanfall-registry 1\n"

/* Reading `text` must stop the runtime with a report that names the file and `line`, and says `reason`
 * (a part of its sentence). */
#define BAD(text, line, reason) \
    do { put_file(text); \
         T_TRAPS(rt_registry_test_reload(), "registry.txt, line " line ": "); \
         T_TRAPS(rt_registry_test_reload(), reason); } while (0)

/* ---- refused ---------------------------------------------------------------------------------- */

TEST(core, registry_parse_refuses_header)
{
    T_CASE("the header line: missing, wrong, misplaced");
    BAD("", "1", "the file has no header line");
    BAD("\n\n# only comments\n", "3", "the file has no header line");
    BAD("[HKEY_CURRENT_USER\\a]\n", "1", "the first line must be 'titanfall-registry 1'");
    BAD("titanfall-registry 2\n", "1", "the first line must be");
    BAD("titanfall-registry 1 extra\n", "1", "unexpected text after the header");
    BAD("# a comment\n\n  \ntitanfall-registry\n", "4", "the first line must be");
    BAD("Titanfall-Registry 1\n", "1", "the first line must be");
    BAD(H H, "2", "a value line must follow a key line");
}

TEST(core, registry_parse_refuses_keys)
{
    T_CASE("key lines");
    BAD(H "[HKEY_BOGUS\\x]\n", "2", "HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER or HKEY_USERS");
    BAD(H "[hkey_current_user\\x]\n", "2", "must start with");                 /* the root name is exact */
    BAD(H "[HKEY_CLASSES_ROOT\\x]\n", "2", "must start with");
    BAD(H "[]\n", "2", "must start with");
    BAD(H "[HKEY_CURRENT_USER\\a\n", "2", "not closed by ]");
    BAD(H "[HKEY_CURRENT_USER\\\\a]\n", "2", "cannot be empty");
    BAD(H "[HKEY_CURRENT_USER\\a\\]\n", "2", "cannot be empty");
    BAD(H "[HKEY_CURRENT_USER\\a%u00]\n", "2", "an escape needs four hex digits");
    BAD(H "[HKEY_CURRENT_USER\\a%x0041]\n", "2", "%uXXXX");
    BAD(H "[HKEY_CURRENT_USER\\a%u0000b]\n", "2", "NUL");
    BAD(H "[HKEY_CURRENT_USER\\a%u005cb]\n", "2", "backslash");
    BAD(H "[HKEY_CURRENT_USER\\x%u1e00]\n", "2", "U+1E00");
    BAD(H "[HKEY_CURRENT_USER\\a\tb]\n", "2", "control character 0x9");
    BAD(H "[HKEY_CURRENT_USER\\a\xff]\n", "2", "not valid UTF-8");
    BAD(H "[HKEY_CURRENT_USER\\a\xc0\x80]\n", "2", "not valid UTF-8");                /* overlong NUL */
    BAD(H "[HKEY_CURRENT_USER\\a\xed\xa0\x80]\n", "2", "not valid UTF-8");            /* an encoded surrogate */
    BAD(H "[HKEY_CURRENT_USER\\a\xf4\x90\x80\x80]\n", "2", "not valid UTF-8");        /* above U+10FFFF */
    BAD(H "[HKEY_CURRENT_USER\\a\xe2\x82]\n", "2", "not valid UTF-8");                /* truncated */
    BAD(H "[HKEY_CURRENT_USER\\a\xc1\xbf]\n", "2", "not valid UTF-8");                /* U+007F in two bytes */
    BAD(H "[HKEY_CURRENT_USER\\a\xe0\x80\x80]\n", "2", "not valid UTF-8");            /* U+0000 in three */
    BAD(H "[HKEY_CURRENT_USER\\a\xe0\x9f\xbf]\n", "2", "not valid UTF-8");            /* U+07FF in three */
    BAD(H "[HKEY_CURRENT_USER\\a\xf0\x80\x80\x80]\n", "2", "not valid UTF-8");        /* U+0000 in four */
    BAD(H "[HKEY_CURRENT_USER\\a\xf0\x8f\xbf\xbf]\n", "2", "not valid UTF-8");        /* U+FFFF in four */
    BAD(H "[HKEY_CURRENT_USER\\a\xf8\x88\x80\x80\x80]\n", "2", "not valid UTF-8");   /* a five-byte form */
    BAD(H "[HKEY_CURRENT_USER\\a\x80]\n", "2", "not valid UTF-8");                     /* a lone continuation byte */

    char big[600];
    snprintf(big, sizeof big, H "[HKEY_CURRENT_USER\\%0256d]\n", 0);
    BAD(big, "2", "256 characters; the limit is 255");
    T_CASE("a name of 255 characters is read");
    snprintf(big, sizeof big, H "[HKEY_CURRENT_USER\\%0255d]\n", 0);
    put_file(big);
    rt_registry_test_reload();
}

TEST(core, registry_parse_refuses_attributes)
{
    T_CASE("time= and class=");
    BAD(H "[HKEY_CURRENT_USER\\a] time=12\n", "2", "time= needs 16 hex digits");
    BAD(H "[HKEY_CURRENT_USER\\a] time=000000000000000g\n", "2", "time= needs 16 hex digits");
    BAD(H "[HKEY_CURRENT_USER\\a] time=00000000000000000\n", "2", "exactly 16 hex digits");
    BAD(H "[HKEY_CURRENT_USER\\a] time=0000000000000000 time=0000000000000000\n", "2", "time= appears twice");
    BAD(H "[HKEY_CURRENT_USER\\a] class=\"x\" class=\"y\"\n", "2", "class= appears twice");
    BAD(H "[HKEY_CURRENT_USER\\a] class=x\n", "2", "double-quoted string was expected");
    BAD(H "[HKEY_CURRENT_USER\\a] class=\"x\n", "2", "not closed by a double quote");
    BAD(H "[HKEY_CURRENT_USER\\a] class=\"a\\u0000\"\n", "2", "NUL");
    BAD(H "[HKEY_CURRENT_USER\\a] volatile\n", "2", "unknown text after the key");
    BAD(H "[HKEY_CURRENT_USER\\a]x\n", "2", "unknown text after the key");
    BAD(H "[HKEY_CURRENT_USER] time=0000000000000000\n", "2", "a root key has no time");
    BAD(H "[HKEY_CURRENT_USER] class=\"x\"\n", "2", "a root key has no class");

    char cls[400];
    char text[600];
    memset(cls, 'c', 256); cls[256] = 0;
    snprintf(text, sizeof text, H "[HKEY_CURRENT_USER\\a] class=\"%s\"\n", cls);
    BAD(text, "2", "the class is 256 characters; the limit is 255");
    T_CASE("a class of 255 characters is read");
    cls[255] = 0;
    snprintf(text, sizeof text, H "[HKEY_CURRENT_USER\\a] class=\"%s\"\n", cls);
    put_file(text);
    rt_registry_test_reload();
}

TEST(core, registry_parse_refuses_duplicates)
{
    T_CASE("a key twice, a value twice, whatever the case of the names");
    BAD(H "[HKEY_CURRENT_USER\\a]\n[HKEY_CURRENT_USER\\a]\n", "3", "the key appears twice");
    BAD(H "[HKEY_CURRENT_USER\\a]\n[HKEY_CURRENT_USER\\b]\n[HKEY_CURRENT_USER\\A]\n", "4", "the key appears twice");
    BAD(H "[HKEY_CURRENT_USER]\n[HKEY_CURRENT_USER]\n", "3", "the key appears twice");
    BAD(H "[HKEY_CURRENT_USER\\a]\n\"v\" = dword:00000001\n\"V\" = dword:00000002\n", "4", "the value appears twice");
    BAD(H "[HKEY_CURRENT_USER\\a]\n@ = sz:\"\"\n@ = sz:\"\"\n", "4", "the value appears twice");

    T_CASE("a key that exists only as the parent of another may still be given its own line later");
    put_file(H "[HKEY_CURRENT_USER\\a\\b]\n[HKEY_CURRENT_USER\\a]\n\"v\" = dword:00000001\n");
    rt_registry_test_reload();
    HKEY a = op(HKEY_CURRENT_USER, "a", KEY_READ);
    T_TRUE(is_dw(a, "v", 1));
    cl(a);
}

TEST(core, registry_parse_refuses_values)
{
    T_CASE("value lines");
    const char *k = H "[HKEY_CURRENT_USER\\a]\n";
    char t[1024];
#define VBAD(line, reason) do { snprintf(t, sizeof t, "%s%s\n", k, line); BAD(t, "3", reason); } while (0)
    VBAD("\"v\"", "'=' was expected");
    VBAD("\"v\" 5", "'=' was expected");
    VBAD("v = dword:00000001", "double-quoted string was expected");
    VBAD("\"v\" = ", "unknown data");
    VBAD("\"v\" = foo:1", "unknown data");
    VBAD("\"v\" = DWORD:00000001", "unknown data");
    VBAD("\"v\" = dword:1", "dword: needs exactly 8 hex digits");
    VBAD("\"v\" = dword:0000000g", "dword: needs exactly 8 hex digits");
    VBAD("\"v\" = dword:000000011", "unexpected text after the value");
    VBAD("\"v\" = dword:00000001 x", "unexpected text after the value");
    VBAD("\"v\" = qword:00000001", "qword: needs exactly 16 hex digits");
    VBAD("\"v\" = qword:00000000000000001", "unexpected text after the value");
    VBAD("\"v\" = sz:abc", "double-quoted string was expected");
    VBAD("\"v\" = sz:\"abc", "not closed by a double quote");
    VBAD("\"v\" = sz:\"a\\qb\"", "unknown escape \\q");
    VBAD("\"v\" = sz:\"a\\", "ends in a backslash");
    VBAD("\"v\" = sz:\"a\\u12\"", "an escape needs four hex digits");
    VBAD("\"v\" = sz:\"a\\u12G4\"", "an escape needs four hex digits");
    VBAD("\"v\" = sz:\"a\tb\"", "control character 0x9");
    VBAD("\"v\" = sz:\"a\x7f\"", "control character 0x7f");
    VBAD("\"v\" = sz:\"a\xc3\"", "not valid UTF-8");
    VBAD("\"v\" = sz:\"a\" \"b\"", "unexpected text after the value");
    VBAD("\"v\" = expand_sz:", "double-quoted string was expected");
    VBAD("\"v\" = multi_sz:\"a\" \"b\"", "',' or the end of the line was expected");
    VBAD("\"v\" = multi_sz:\"a\",", "double-quoted string was expected");
    VBAD("\"v\" = multi_sz:\"a\",\"\"", "cannot be empty");
    VBAD("\"v\" = multi_sz:\"\"", "cannot be empty");
    VBAD("\"v\" = multi_sz:\"a\\u0000b\"", "NUL");
    VBAD("\"v\" = hex:0", "a byte is two hex digits");
    VBAD("\"v\" = hex:0g", "a byte is two hex digits");
    VBAD("\"v\" = hex:00,", "a byte is two hex digits");
    VBAD("\"v\" = hex:00,,01", "a byte is two hex digits");
    VBAD("\"v\" = hex:00 01", "unexpected text after the value");
    VBAD("\"v\" = hex:0001", "',' was expected between the bytes");
    VBAD("\"v\" = hex:00, 01", "a byte is two hex digits");
    VBAD("\"v\" = hex(4294967296):00", "above 4294967295");
    VBAD("\"v\" = hex(99999999999999999999):00", "above 4294967295");
    VBAD("\"v\" = hex():00", "needs a decimal type number");
    VBAD("\"v\" = hex(x):00", "needs a decimal type number");
#undef VBAD
}

TEST(core, registry_parse_refuses_names_and_places)
{
    T_CASE("value names");
    const char *k = H "[HKEY_CURRENT_USER\\a]\n";
    char t[2048];
#define VBAD(line, reason) do { snprintf(t, sizeof t, "%s%s\n", k, line); BAD(t, "3", reason); } while (0)
    VBAD("\"\" = dword:00000000", "cannot be empty");
    VBAD("\"a\\u0000b\" = dword:00000000", "NUL");
    VBAD("\"x\\u1e00\" = dword:00000000", "U+1E00");
    VBAD("\"unterminated = dword:00000000", "not closed by a double quote");
    VBAD("@x = dword:00000000", "'=' was expected");
    VBAD("= dword:00000000", "double-quoted string was expected");
#undef VBAD
    char big[20000];
    char *p = big + snprintf(big, sizeof big, "%s\"", k);
    memset(p, 'n', 16384); p += 16384;
    strcpy(p, "\" = dword:00000000\n");
    BAD(big, "3", "16384 characters; the limit is 16383");
    p = big + snprintf(big, sizeof big, "%s\"", k);
    memset(p, 'n', 16383); p += 16383;
    strcpy(p, "\" = dword:00000000\n");
    put_file(big);
    rt_registry_test_reload();                              /* 16383 is read */

    T_CASE("a value line before any key line; values of the two roots that have none");
    BAD(H "\"v\" = dword:00000001\n", "2", "must follow a key line");
    BAD(H "@ = sz:\"x\"\n", "2", "must follow a key line");
    BAD(H "[HKEY_LOCAL_MACHINE]\n\"v\" = dword:00000001\n", "3", "HKEY_LOCAL_MACHINE has no values");
    BAD(H "[HKEY_USERS]\n@ = sz:\"x\"\n", "3", "HKEY_USERS has no values");
    T_CASE("but HKEY_CURRENT_USER has");
    put_file(H "[HKEY_CURRENT_USER]\n\"v\" = dword:00000001\n");
    rt_registry_test_reload();
    T_TRUE(is_dw(HKEY_CURRENT_USER, "v", 1));
}

TEST(core, registry_parse_line_numbers)
{
    T_CASE("the line number counts comments, blank lines and CRLF line ends");
    BAD("# one\n# two\n\n   \n" H "# six\n\n[HKEY_CURRENT_USER\\a]\n\"v\" = dword:zz\n", "9", "dword:");
    BAD("# one\r\n" H "[HKEY_CURRENT_USER\\a]\r\n\"v\" = dword:zz\r\n", "4", "dword:");
    BAD("\r\n\r\n" H "\r\n\r\n[HKEY_BOGUS]\r\n", "6", "must start with");

    T_CASE("the last line without a line end");
    BAD(H "[HKEY_CURRENT_USER\\a]\n\"v\" = dword:zz", "3", "dword:");

    T_CASE("a NUL byte anywhere in the file");
    put_file(H "[HKEY_CURRENT_USER\\a]\n");
    {
        FILE *f = fopen(rt_registry_path(), "ab");
        T_REQUIRE(f != NULL);
        fwrite("\"v\" = dword:0000000\0\n", 1, 21, f);
        fclose(f);
    }
    T_TRAPS(rt_registry_test_reload(), "registry.txt, line 3: the file has a NUL byte");

    T_CASE("the report names the file, so that the person fixing it knows which one");
    put_file("nonsense\n");
    T_TRAPS(rt_registry_test_reload(), rt_registry_path());

    T_CASE("a file that does not parse is left as it is");
    char *after = slurp(rt_registry_path(), NULL);
    T_TRUE(after && !strcmp(after, "nonsense\n"));
    free(after);
}

TEST(core, registry_parse_size_limits)
{
    T_CASE("a value of exactly 1 MiB is read; one byte more is refused with its line");
    size_t n = RT_REG_VALUE_DATA_MAX;
    char *t = malloc(n * 3 + 200);
    T_REQUIRE(t != NULL);
    char *p = t + sprintf(t, "%s[HKEY_CURRENT_USER\\a]\n\"v\" = hex:", H);
    for (size_t i = 0; i < n; i++) { if (i) *p++ = ','; *p++ = "0123456789abcdef"[(i >> 4) & 15]; *p++ = "0123456789abcdef"[i & 15]; }
    strcpy(p, "\n");
    put_file(t);
    rt_registry_test_reload();
    HKEY a = op(HKEY_CURRENT_USER, "a", KEY_READ);
    DWORD cb = 0, type = 0;
    T_EQ(rt_reg_query_value_ex_w(a, W("v"), NULL, &type, NULL, &cb), ERROR_SUCCESS);
    T_TRUE(cb == n && type == REG_BINARY);
    BYTE *buf = malloc(n);
    cb = (DWORD)n;
    T_EQ(rt_reg_query_value_ex_w(a, W("v"), NULL, NULL, buf, &cb), ERROR_SUCCESS);
    bool same = true;
    for (size_t i = 0; i < n; i++) if (buf[i] != (BYTE)(i & 255)) { same = false; break; }
    T_TRUE(same);
    free(buf);
    cl(a);
    strcpy(p, ",00\n");
    put_file(t);
    T_TRAPS(rt_registry_test_reload(), "registry.txt, line 3: the value is longer than 1048576 bytes");
    free(t);
}

/* ---- accepted --------------------------------------------------------------------------------------- */

static const char *hand_written =
    "# comment\n"
    "  # indented comment\n"
    "\n"
    "   \t \n"
    "titanfall-registry 1   \n"
    "  [HKEY_CURRENT_USER\\Software\\Hand]   class=\"Hand Class\"  time=01DC000000000001  \n"
    "@=sz:\"default\"\n"
    "   \"s\"   =   sz:\"a \\\"quoted\\\" \\\\ back \\u00e9 \xc3\xa9 \xe2\x82\xac \xf0\x9f\x98\x80\"\n"
    "\"e\" = expand_sz:\"%SystemRoot%\\\\x\"\n"
    "\"m\" = multi_sz:\"one\",\"two\" , \"three\"\n"
    "\"m0\" = multi_sz:\n"
    "\"m1\" = multi_sz:  \"only\"  \n"
    "\"d\" = dword:DEADBEEF\n"
    "\"d2\" = dword:deadbeef\n"
    "\"q\" = qword:0123456789ABCDEF\n"
    "\"b\" = hex:00,ff,A0,a1\n"
    "\"b0\" = hex:\n"
    "\"t\" = hex(7):61,00,00,00\n"
    "\"t1\" = hex(1):61,00\n"
    "\"n\" = hex(0):01\n"
    "\"big\" = hex(4294967295):01,02\n"
    "\"sz0\" = sz:\"a\\u0000b\"\n"
    "\"@\" = dword:00000007\n"
    "\"\\u0041\\u00e9\" = dword:00000008\n";

static void check_hand_written(void)
{
    HKEY k = op(HKEY_CURRENT_USER, "Software\\Hand", KEY_READ);
    if (!k) return;
    static const WCHAR s_want[] = { 'a', ' ', '"', 'q', 'u', 'o', 't', 'e', 'd', '"', ' ', '\\', ' ', 'b', 'a', 'c', 'k', ' ',
                                    0xE9, ' ', 0xE9, ' ', 0x20AC, ' ', 0xD83D, 0xDE00, 0 };
    BYTE buf[128];
    DWORD cb, type;

    T_TRUE(is_sz(k, NULL, "default"));
    cb = sizeof buf;
    T_EQ(rt_reg_query_value_ex_w(k, W("s"), NULL, &type, buf, &cb), ERROR_SUCCESS);
    T_TRUE(type == REG_SZ && cb == sizeof s_want && !memcmp(buf, s_want, sizeof s_want));
    cb = sizeof buf;
    T_EQ(rt_reg_query_value_ex_w(k, W("e"), NULL, &type, buf, &cb), ERROR_SUCCESS);
    T_TRUE(type == REG_EXPAND_SZ && cb == (wl(W("%SystemRoot%\\x")) + 1) * 2 && weq((WCHAR *)buf, "%SystemRoot%\\x"));

    static const WCHAR m_want[] = { 'o', 'n', 'e', 0, 't', 'w', 'o', 0, 't', 'h', 'r', 'e', 'e', 0, 0 };
    cb = sizeof buf;
    T_EQ(rt_reg_query_value_ex_w(k, W("m"), NULL, &type, buf, &cb), ERROR_SUCCESS);
    T_TRUE(type == REG_MULTI_SZ && cb == sizeof m_want && !memcmp(buf, m_want, sizeof m_want));
    cb = sizeof buf;
    T_EQ(rt_reg_query_value_ex_w(k, W("m0"), NULL, &type, buf, &cb), ERROR_SUCCESS);
    T_TRUE(type == REG_MULTI_SZ && cb == 2 && buf[0] == 0 && buf[1] == 0);
    static const WCHAR m1_want[] = { 'o', 'n', 'l', 'y', 0, 0 };
    cb = sizeof buf;
    T_EQ(rt_reg_query_value_ex_w(k, W("m1"), NULL, &type, buf, &cb), ERROR_SUCCESS);
    T_TRUE(cb == sizeof m1_want && !memcmp(buf, m1_want, sizeof m1_want));

    T_TRUE(is_dw(k, "d", 0xDEADBEEF) && is_dw(k, "d2", 0xDEADBEEF));
    cb = sizeof buf;
    T_EQ(rt_reg_query_value_ex_w(k, W("q"), NULL, &type, buf, &cb), ERROR_SUCCESS);
    T_TRUE(type == REG_QWORD && cb == 8 && !memcmp(buf, "\xEF\xCD\xAB\x89\x67\x45\x23\x01", 8));
    cb = sizeof buf;
    T_EQ(rt_reg_query_value_ex_w(k, W("b"), NULL, &type, buf, &cb), ERROR_SUCCESS);
    T_TRUE(type == REG_BINARY && cb == 4 && !memcmp(buf, "\x00\xff\xa0\xa1", 4));
    cb = sizeof buf;
    T_EQ(rt_reg_query_value_ex_w(k, W("b0"), NULL, &type, buf, &cb), ERROR_SUCCESS);
    T_TRUE(type == REG_BINARY && cb == 0);
    cb = sizeof buf;
    T_EQ(rt_reg_query_value_ex_w(k, W("t"), NULL, &type, buf, &cb), ERROR_SUCCESS);
    T_TRUE(type == REG_MULTI_SZ && cb == 4 && !memcmp(buf, "a\0\0\0", 4));
    cb = sizeof buf;
    T_EQ(rt_reg_query_value_ex_w(k, W("t1"), NULL, &type, buf, &cb), ERROR_SUCCESS);
    T_TRUE(type == REG_SZ && cb == 2);
    cb = sizeof buf;
    T_EQ(rt_reg_query_value_ex_w(k, W("n"), NULL, &type, buf, &cb), ERROR_SUCCESS);
    T_TRUE(type == REG_NONE && cb == 1 && buf[0] == 1);
    cb = sizeof buf;
    T_EQ(rt_reg_query_value_ex_w(k, W("big"), NULL, &type, buf, &cb), ERROR_SUCCESS);
    T_TRUE(type == 0xFFFFFFFFu && cb == 2);
    cb = sizeof buf;
    T_EQ(rt_reg_query_value_ex_w(k, W("sz0"), NULL, &type, buf, &cb), ERROR_SUCCESS);
    T_TRUE(type == REG_SZ && cb == 8 && !memcmp(buf, "a\0\0\0b\0\0\0", 8));
    T_TRUE(is_dw(k, "@", 7));
    static const WCHAR ae[] = { 'A', 0xE9, 0 };
    DWORD x = 0;
    cb = 4;
    T_EQ(rt_reg_query_value_ex_w(k, ae, NULL, NULL, (BYTE *)&x, &cb), ERROR_SUCCESS);
    T_EQ(x, 8);

    DWORD nvals = 0;
    counts(k, NULL, &nvals);
    T_EQ(nvals, 18);

    WCHAR cls[32];
    HKEY sw = op(HKEY_CURRENT_USER, "Software", KEY_READ);
    WCHAR name[16];
    DWORD cn = 16, cc = 32;
    FILETIME ft;
    T_EQ(rt_reg_enum_key_ex_w(sw, 0, name, &cn, NULL, cls, &cc, &ft), ERROR_SUCCESS);
    T_TRUE(weq(name, "Hand") && weq(cls, "Hand Class") && cc == 10);
    T_TRUE(ft.dwHighDateTime == 0x01DC0000u && ft.dwLowDateTime == 1);
    cl(sw);
    cl(k);
}

TEST(core, registry_parse_accepts_all_forms)
{
    T_CASE("blanks, comments, every form of data, both cases of hex, raw UTF-8 and escapes");
    put_file(hand_written);
    rt_registry_test_reload();
    check_hand_written();
}

TEST(core, registry_parse_accepts_crlf)
{
    T_CASE("the same file with CR LF line ends reads the same");
    size_t n = strlen(hand_written);
    char *crlf = malloc(n * 2 + 1);
    char *o = crlf;
    for (const char *p = hand_written; *p; p++) { if (*p == '\n') *o++ = '\r'; *o++ = *p; }
    *o = 0;
    put_file(crlf);
    free(crlf);
    rt_registry_test_reload();
    check_hand_written();
}

TEST(core, registry_parse_accepts_no_final_newline)
{
    T_CASE("a last line with no line end");
    put_file(H "[HKEY_CURRENT_USER\\a]\n\"v\" = dword:00000001");
    rt_registry_test_reload();
    HKEY a = op(HKEY_CURRENT_USER, "a", KEY_READ);
    T_TRUE(is_dw(a, "v", 1));
    cl(a);
    T_CASE("an empty registry: the header alone");
    put_file(H);
    rt_registry_test_reload();
    DWORD n = 9;
    T_EQ(rt_reg_query_info_key_w(HKEY_CURRENT_USER, NULL, NULL, NULL, &n, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ERROR_SUCCESS);
    T_EQ(n, 0);
}

TEST(core, registry_parse_two_files_one_tree)
{
    T_CASE("two spellings of one tree are written back as the same bytes");
    const char *a =
        "titanfall-registry 1\n"
        "[HKEY_CURRENT_USER\\Software] time=0000000000000010\n"
        "[HKEY_CURRENT_USER\\Software\\Det] time=0000000000000011 class=\"c\"\n"
        "\"b\" = dword:00000002\n"
        "\"a\" = sz:\"x\"\n"
        "@ = multi_sz:\"p\",\"q\"\n";
    const char *b =
        "# the same tree, written differently\r\n"
        "titanfall-registry 1\r\n"
        "\r\n"
        "[HKEY_CURRENT_USER\\Software\\Det]    class=\"c\"   time=0000000000000011\r\n"
        "  \"b\"   =   hex(4):02,00,00,00\r\n"
        "  \"a\"   =   hex(1):78,00,00,00\r\n"
        "  @ = hex(7):70,00,00,00,71,00,00,00,00,00\r\n"
        "[HKEY_CURRENT_USER\\Software]\ttime=0000000000000010\r\n";
    char *out[2];
    const char *in[2] = { a, b };
    for (int i = 0; i < 2; i++) {
        put_file(in[i]);
        rt_registry_test_reload();
        HKEY v = NULL;
        T_EQ(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("Trigger"), 0, NULL, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &v, NULL), ERROR_SUCCESS);
        cl(v);
        out[i] = reg_text(NULL);
        T_TRUE(out[i] != NULL);
    }
    T_TRUE(out[0] && out[1] && !strcmp(out[0], out[1]));
    char *want = with_header(
        "\n[HKEY_CURRENT_USER\\Software] time=0000000000000010\n"
        "\n[HKEY_CURRENT_USER\\Software\\Det] time=0000000000000011 class=\"c\"\n"
        "\"b\" = dword:00000002\n"
        "\"a\" = sz:\"x\"\n"
        "@ = multi_sz:\"p\",\"q\"\n");
    T_TRUE(out[0] && !strcmp(out[0], want));
    free(want);
    free(out[0]);
    free(out[1]);
}
