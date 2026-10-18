/* The registry (rt/registry.h): the seed.  What a Windows machine always has, taken from the
 * machine description (rt/machine.h) and cpuid so that it cannot disagree with GetVersionEx or
 * the cpuid instruction; and what must NOT be there.  How the seed meets the file. */
#include "registry_util.c"

/* ---- the whole tree as text ------------------------------------------------------------------- */

typedef struct { char *p; size_t n, cap; } Out;

static void out_put(Out *o, const char *s) {
    size_t l = strlen(s);
    if (o->n + l + 1 > o->cap) { o->cap = (o->n + l + 1) * 2; o->p = realloc(o->p, o->cap); }
    memcpy(o->p + o->n, s, l + 1);
    o->n += l;
}

/* "K <path>" for every key and "V <path> :: <name>" for every value, subkeys in the registry's order. */
static void dump(HKEY k, const char *path, Out *o) {
    char line[1500];
    snprintf(line, sizeof line, "K %s\n", path);
    out_put(o, line);
    DWORD nvals = 0, nkeys = 0;
    counts(k, &nkeys, &nvals);
    for (DWORD i = 0; i < nvals; i++) {
        char name[300];
        T_TRUE(value_name(k, i, name, sizeof name));
        snprintf(line, sizeof line, "V %s :: %s\n", path, name);
        out_put(o, line);
    }
    for (DWORD i = 0; i < nkeys; i++) {
        char name[300], sub[1200];
        T_TRUE(key_name(k, i, name, sizeof name));
        snprintf(sub, sizeof sub, "%s\\%s", path, name);
        HKEY c = NULL;
        T_EQ(open_st(k, name, KEY_READ, &c), ERROR_SUCCESS);
        if (c) { dump(c, sub, o); cl(c); }
    }
}

static void cpuid_leaf(uint32_t leaf, uint32_t out[4]) {
    Ctx c;
    memset(&c, 0, sizeof c);
    c.r[0] = leaf;
    rt_cpuid(&c);
    out[0] = (uint32_t)c.r[0]; out[1] = (uint32_t)c.r[3]; out[2] = (uint32_t)c.r[1]; out[3] = (uint32_t)c.r[2];
}

static int cmp_str(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }

/* The processor numbers as the registry sorts them: as text. */
static void cpu_names(uint32_t n, char names[][8]) {
    char *ptrs[64];
    for (uint32_t i = 0; i < n; i++) { snprintf(names[i], 8, "%u", i); ptrs[i] = names[i]; }
    qsort(ptrs, n, sizeof ptrs[0], cmp_str);
    char tmp[64][8];
    for (uint32_t i = 0; i < n; i++) strcpy(tmp[i], ptrs[i]);
    for (uint32_t i = 0; i < n; i++) strcpy(names[i], tmp[i]);
}

static bool brand_is_empty(void) {
    char brand[49];
    memset(brand, 0, sizeof brand);
    for (uint32_t i = 0; i < 3; i++) {
        uint32_t r[4];
        cpuid_leaf(0x80000002u + i, r);
        memcpy(brand + 16 * i, r, 16);
    }
    const char *b = brand;
    while (*b == ' ') b++;
    return *b == 0;
}

/* ---- what is there ------------------------------------------------------------------------------- */

TEST(core, registry_seed_tree_exactly)
{
    T_CASE("the tree at start-up holds the processor description, the OS version, and nothing else");
    const RtMachine *m = rt_machine();
    char names[64][8];
    T_REQUIRE(m->cpu_count >= 1 && m->cpu_count <= 64);
    cpu_names(m->cpu_count, names);
    bool brand = !brand_is_empty();

    Out want = { 0 };
    out_put(&want, "K HKEY_LOCAL_MACHINE\n");
    out_put(&want, "K HKEY_LOCAL_MACHINE\\HARDWARE\n");
    out_put(&want, "K HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\n");
    out_put(&want, "K HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\n");
    out_put(&want, "K HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\n");
    for (uint32_t i = 0; i < m->cpu_count; i++) {
        char line[300];
        const char *base = "HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor";
        snprintf(line, sizeof line, "K %s\\%s\n", base, names[i]); out_put(&want, line);
        snprintf(line, sizeof line, "V %s\\%s :: ~MHz\n", base, names[i]); out_put(&want, line);
        snprintf(line, sizeof line, "V %s\\%s :: Identifier\n", base, names[i]); out_put(&want, line);
        snprintf(line, sizeof line, "V %s\\%s :: VendorIdentifier\n", base, names[i]); out_put(&want, line);
        if (brand) { snprintf(line, sizeof line, "V %s\\%s :: ProcessorNameString\n", base, names[i]); out_put(&want, line); }
    }
    out_put(&want,
        "K HKEY_LOCAL_MACHINE\\SOFTWARE\n"
        "K HKEY_LOCAL_MACHINE\\SOFTWARE\\Classes\n"
        "K HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\n"
        "K HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\n"
        "K HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\n"
        "V HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion :: CurrentVersion\n"
        "V HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion :: CurrentBuild\n"
        "V HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion :: CurrentBuildNumber\n"
        "V HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion :: CSDVersion\n"
        "K HKEY_LOCAL_MACHINE\\SYSTEM\n"
        "K HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\n"
        "K HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\n"
        "K HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\ProductOptions\n"
        "V HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\ProductOptions :: ProductType\n"
        "K HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Windows\n"
        "V HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Windows :: CSDVersion\n"
        "K HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Hardware Profiles\n"
        "K HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Hardware Profiles\\Current\n"
        "K HKEY_CURRENT_USER\n"
        "K HKEY_USERS\n");

    Out got = { 0 };
    dump(HKEY_LOCAL_MACHINE, "HKEY_LOCAL_MACHINE", &got);
    dump(HKEY_CURRENT_USER, "HKEY_CURRENT_USER", &got);
    dump(HKEY_USERS, "HKEY_USERS", &got);
    if (strcmp(got.p, want.p)) fprintf(stderr, "---- the tree is:\n%s---- expected:\n%s----\n", got.p, want.p);
    T_TRUE(!strcmp(got.p, want.p));
    free(got.p);
    free(want.p);
}

TEST(core, registry_seed_processors)
{
    const RtMachine *m = rt_machine();
    T_CASE("one key per processor of the machine description, with the clock, the identifier and the vendor");
    uint32_t l0[4], l1[4];
    cpuid_leaf(0, l0);
    cpuid_leaf(1, l1);
    char vendor[13];
    memcpy(vendor, &l0[1], 4); memcpy(vendor + 4, &l0[3], 4); memcpy(vendor + 8, &l0[2], 4);
    vendor[12] = 0;
    SYSTEM_INFO si;
    rt_machine_system_info(&si);
    char ident[96];
    /* The family, model and stepping GetSystemInfo reports are the ones of cpuid leaf 1. */
    snprintf(ident, sizeof ident, "%s Family %u Model %u Stepping %u",
             !strcmp(vendor, "GenuineIntel") ? "Intel64" : "AMD64", si.wProcessorLevel, si.wProcessorRevision >> 8, si.wProcessorRevision & 0xFF);
    T_EQ((l1[0] >> 8) & 0xF, si.wProcessorLevel);
    T_EQ(((l1[0] >> 4) & 0xF) | ((l1[0] >> 12) & 0xF0), si.wProcessorRevision >> 8);
    T_EQ(l1[0] & 0xF, si.wProcessorRevision & 0xFF);

    HKEY cp = op(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor", KEY_READ);
    DWORD n = 0;
    counts(cp, &n, NULL);
    T_EQ(n, m->cpu_count);
    for (uint32_t i = 0; i < m->cpu_count; i++) {
        char name[8];
        snprintf(name, sizeof name, "%u", i);
        HKEY k = op(cp, name, KEY_READ);
        T_TRUE(is_dw(k, "~MHz", (DWORD)(m->cpu_hz / 1000000u)));
        T_TRUE(is_sz(k, "Identifier", ident));
        T_TRUE(is_sz(k, "VendorIdentifier", vendor));
        cl(k);
    }
    T_EQ(m->cpu_hz, 3000000000u);
    HKEY first = op(cp, "0", KEY_READ);
    T_TRUE(is_dw(first, "~MHz", 3000));
    cl(first);

    T_CASE("no brand string, no ProcessorNameString: the value is absent, not invented");
    if (brand_is_empty()) {
        HKEY k0 = op(cp, "0", KEY_READ);
        T_EQ(rt_reg_query_value_ex_w(k0, W("ProcessorNameString"), NULL, NULL, NULL, NULL), ERROR_FILE_NOT_FOUND);
        cl(k0);
    }
    cl(cp);
}

TEST(core, registry_seed_os_version)
{
    const RtMachine *m = rt_machine();
    T_CASE("the version numbers are those of the machine description: 6.1, build 7601, Service Pack 1");
    char text[32];
    HKEY cv = op(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", KEY_READ);
    snprintf(text, sizeof text, "%u.%u", m->os_major, m->os_minor);
    T_TRUE(is_sz(cv, "CurrentVersion", text));
    T_TRUE(is_sz(cv, "CurrentVersion", "6.1"));
    snprintf(text, sizeof text, "%u", m->os_build);
    T_TRUE(is_sz(cv, "CurrentBuild", text) && is_sz(cv, "CurrentBuildNumber", text));
    T_TRUE(is_sz(cv, "CurrentBuildNumber", "7601"));
    T_TRUE(is_sz(cv, "CSDVersion", m->sp_name) && is_sz(cv, "CSDVersion", "Service Pack 1"));
    cl(cv);

    T_CASE("the service pack as a number, and the product type of a workstation");
    HKEY w = op(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\Windows", KEY_READ);
    T_TRUE(is_dw(w, "CSDVersion", ((DWORD)m->sp_major << 8) | m->sp_minor));
    T_TRUE(is_dw(w, "CSDVersion", 0x100));
    cl(w);
    HKEY po = op(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\ProductOptions", KEY_READ);
    T_TRUE(is_sz(po, "ProductType", "WinNT"));
    cl(po);

    T_CASE("strings are stored with their terminator, as Windows stores them");
    cv = op(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", KEY_READ);
    DWORD cb = 0, type = 0;
    T_EQ(rt_reg_query_value_ex_w(cv, W("CurrentVersion"), NULL, &type, NULL, &cb), ERROR_SUCCESS);
    T_TRUE(type == REG_SZ && cb == 8);                      /* "6.1" and the NUL, in UTF-16 */
    cl(cv);
}

TEST(core, registry_seed_leaves_out_the_publisher)
{
    T_CASE("nothing under the keys of the game's publisher, its launcher or the engine's vendor is seeded");
    const char *absent[] = {
        "SOFTWARE\\Origin", "SOFTWARE\\Wow6432Node\\Origin", "SOFTWARE\\Electronic Arts", "SOFTWARE\\Wow6432Node\\Electronic Arts",
        "SOFTWARE\\EA Games", "SOFTWARE\\Respawn", "SOFTWARE\\Wow6432Node\\Respawn", "SOFTWARE\\Valve", "SOFTWARE\\Wow6432Node\\Valve",
        "SOFTWARE\\Valve\\Steam", "SOFTWARE\\Microsoft\\Cryptography", "SOFTWARE\\Microsoft\\DirectX",
        "SOFTWARE\\Microsoft\\Windows\\CurrentVersion", "SYSTEM\\CurrentControlSet\\Control\\Session Manager", "SYSTEM\\Select",
        "SYSTEM\\ControlSet001", "HARDWARE\\DEVICEMAP\\VIDEO",
    };
    for (unsigned i = 0; i < sizeof absent / sizeof absent[0]; i++) {
        HKEY k = (HKEY)0x77;
        T_EQ(open_st(HKEY_LOCAL_MACHINE, absent[i], KEY_READ, &k), ERROR_FILE_NOT_FOUND);
        T_TRUE(k == (HKEY)0x77);
    }
    const char *absent_user[] = { "Software\\Valve", "Software\\Valve\\Source\\Settings", "Software\\Valve\\Source\\r2\\Settings",
                                  "Software\\Origin", "Software\\Electronic Arts", "Software\\Respawn", "Software\\Microsoft" };
    for (unsigned i = 0; i < sizeof absent_user / sizeof absent_user[0]; i++) {
        HKEY k = (HKEY)0x77;
        T_EQ(open_st(HKEY_CURRENT_USER, absent_user[i], KEY_READ, &k), ERROR_FILE_NOT_FOUND);
    }
    T_CASE("and the values the traced game asked for are not there either");
    HKEY cv = op(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", KEY_READ);
    const char *values[] = { "ProductName", "SystemRoot", "InstallDate", "ProductId", "RegisteredOwner", "BuildLab", "EditionID" };
    for (unsigned i = 0; i < sizeof values / sizeof values[0]; i++)
        T_EQ(rt_reg_query_value_ex_w(cv, W(values[i]), NULL, NULL, NULL, NULL), ERROR_FILE_NOT_FOUND);
    cl(cv);
}

/* ---- the seed and the file --------------------------------------------------------------------------- */

TEST(core, registry_seed_not_written)
{
    T_CASE("the seed is not in the file, whatever else is");
    cl(mk(HKEY_CURRENT_USER, "Software\\Something"));
    char *t = reg_text(NULL);
    T_TRUE(t && !contains(t, "CentralProcessor") && !contains(t, "CurrentVersion") && !contains(t, "ProductType") &&
           !contains(t, "~MHz") && !contains(t, "HKEY_LOCAL_MACHINE") && !contains(t, "Classes"));
    free(t);

    T_CASE("a guest's key below a seeded key writes the seeded ancestors without their seeded values");
    HKEY h = mk(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Mine");
    T_EQ(put_dw(h, "x", 1), ERROR_SUCCESS);
    cl(h);
    t = reg_text(NULL);
    T_TRUE(contains(t, "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Mine]"));
    T_TRUE(contains(t, "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion] time="));
    T_TRUE(!contains(t, "\"CurrentVersion\" =") && !contains(t, "\"CurrentBuild\"") && !contains(t, "CSDVersion"));
    free(t);

    T_CASE("a guest's value in a seeded key writes only that value");
    HKEY cp = op(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", KEY_ALL_ACCESS);
    T_EQ(put_sz(cp, "Custom", "mine"), ERROR_SUCCESS);
    t = reg_text(NULL);
    T_TRUE(contains(t, "[HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0] time="));
    T_TRUE(contains(t, "\"Custom\" = sz:\"mine\"\n"));
    T_TRUE(!contains(t, "~MHz") && !contains(t, "VendorIdentifier") && !contains(t, "Identifier"));
    T_TRUE(!contains(t, "CentralProcessor\\1"));
    free(t);
    cl(cp);
    rt_registry_test_reload();
    cp = op(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", KEY_ALL_ACCESS);
    T_TRUE(is_sz(cp, "Custom", "mine") && is_dw(cp, "~MHz", 3000));
    DWORD nvals = 0;
    counts(cp, NULL, &nvals);
    T_TRUE(nvals == 4 || nvals == 5);                       /* the seed's three or four, and the guest's */
    cl(cp);
}

TEST(core, registry_seed_overridden_by_guest)
{
    T_CASE("a seeded value the guest overwrites is written, and wins at the next start");
    HKEY po = op(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\ProductOptions", KEY_ALL_ACCESS);
    T_EQ(put_sz(po, "ProductType", "ServerNT"), ERROR_SUCCESS);
    T_TRUE(is_sz(po, "ProductType", "ServerNT"));
    char *t = reg_text(NULL);
    T_TRUE(contains(t, "[HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\ProductOptions] time="));
    T_TRUE(contains(t, "\"ProductType\" = sz:\"ServerNT\"\n"));
    free(t);
    rt_registry_test_reload();
    HKEY again = op(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\ProductOptions", KEY_ALL_ACCESS);
    T_TRUE(is_sz(again, "ProductType", "ServerNT"));
    cl(po);

    T_CASE("written back as the seed has it, it is the seed again and leaves the file");
    T_EQ(put_sz(again, "ProductType", "WinNT"), ERROR_SUCCESS);
    t = reg_text(NULL);
    T_TRUE(!contains(t, "ProductType") && !contains(t, "ProductOptions"));
    free(t);
    cl(again);
    rt_registry_test_reload();
    HKEY third = op(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\ProductOptions", KEY_ALL_ACCESS);
    T_TRUE(is_sz(third, "ProductType", "WinNT"));
    cl(third);

    T_CASE("a seeded value that a file from an older machine description defines keeps the file's data");
    put_file("titanfall-registry 1\n"
             "[HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0]\n"
             "\"~MHz\" = dword:000003e8\n");
    rt_registry_test_reload();
    HKEY cp = op(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", KEY_ALL_ACCESS);
    T_TRUE(is_dw(cp, "~MHz", 1000));
    T_TRUE(has_value(cp, "Identifier") && has_value(cp, "VendorIdentifier"));       /* the others come from the seed */
    char name[32];
    T_TRUE(value_name(cp, 0, name, sizeof name) && !strcmp(name, "~MHz"));          /* where the file had it */

    T_CASE("such a value is still a seeded one: it cannot be deleted");
    T_TRAPS(rt_reg_delete_value_w(cp, W("~MHz")), "seeded value");
    cl(cp);
}

TEST(core, registry_seed_cannot_be_deleted)
{
    T_CASE("deleting a seeded value or a seeded key stops the runtime and says why");
    HKEY cv = op(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", KEY_ALL_ACCESS);
    T_TRAPS(rt_reg_delete_value_w(cv, W("CurrentBuild")), "seeded value");
    T_TRAPS(rt_reg_delete_value_a(cv, "currentbuild"), "come back at the next start");
    HKEY hw = op(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System", KEY_ALL_ACCESS);
    T_TRAPS(rt_reg_delete_key_ex_w(hw, W("CentralProcessor\\0"), 0, 0), "seeded key");
    HKEY sw = op(HKEY_LOCAL_MACHINE, "SOFTWARE", KEY_ALL_ACCESS);
    T_TRAPS(rt_reg_delete_key_ex_w(sw, W("Classes"), 0, 0), "seeded key");
    T_TRAPS(rt_reg_delete_key_ex_w(HKEY_LOCAL_MACHINE, W("SYSTEM\\CurrentControlSet\\Hardware Profiles\\Current"), 0, 0), "seeded key");
    T_TRUE(has_value(cv, "CurrentBuild"));

    T_CASE("a seeded key that has subkeys is refused the usual way");
    T_EQ(rt_reg_delete_key_ex_w(sw, W("Microsoft"), 0, 0), ERROR_ACCESS_DENIED);

    T_CASE("what the guest added to a seeded key can be deleted");
    T_EQ(put_dw(cv, "Extra", 1), ERROR_SUCCESS);
    T_EQ(rt_reg_delete_value_w(cv, W("Extra")), ERROR_SUCCESS);
    HKEY mine = mk(cv, "Mine");
    cl(mine);
    T_EQ(rt_reg_delete_key_ex_w(cv, W("Mine"), 0, 0), ERROR_SUCCESS);
    cl(hw); cl(sw); cl(cv);
}
