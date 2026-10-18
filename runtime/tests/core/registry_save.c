/* The registry (rt/registry.h): that a save is atomic.  A temporary file is written and renamed; a
 * stale temporary is ignored and removed; a save that cannot be done stops the runtime and leaves
 * the good file as it was; a process killed in the middle of saving leaves a file that reads. */
#include <signal.h>
#include <sys/wait.h>

#include "registry_util.c"

static bool same_bytes(const char *a, size_t an, const char *b, size_t bn) {
    return a && b && an == bn && !memcmp(a, b, an);
}

/* ---- the temporary file ------------------------------------------------------------------------- */

TEST(core, registry_save_leaves_no_temporary)
{
    T_CASE("after a save there is the file and no temporary");
    for (int i = 0; i < 5; i++) {
        char name[32];
        snprintf(name, sizeof name, "Software\\Tmp%d", i);
        cl(mk(HKEY_CURRENT_USER, name));
        T_TRUE(exists(rt_registry_path()));
        T_TRUE(!exists(state_path("registry.txt.tmp")));
    }
    T_CASE("and nothing else was made in the state directory by the registry");
    T_TRUE(!exists(state_path("registry.txt.bak")) && !exists(state_path("registry.txt~")) && !exists(state_path("registry.tmp")));
}

TEST(core, registry_save_stale_temporary)
{
    T_CASE("a temporary left by a save that was cut short does not matter: it is removed when the tree is read");
    cl(mk(HKEY_CURRENT_USER, "Software\\Good"));
    size_t good_len;
    char *good = reg_text(&good_len);
    T_TRUE(good != NULL);
    spit(state_path("registry.txt.tmp"), "titanfall-regis");                // half of a file
    rt_registry_test_reload();
    T_TRUE(!exists(state_path("registry.txt.tmp")));
    size_t len;
    char *now = reg_text(&len);
    T_TRUE(same_bytes(good, good_len, now, len));
    T_TRUE(has_key(HKEY_CURRENT_USER, "Software\\Good"));
    free(now);

    T_CASE("and one that is there when a save is made is overwritten, not read and not appended to");
    spit(state_path("registry.txt.tmp"), "# stale\ntitanfall-registry 1\n[HKEY_CURRENT_USER\\Stale]\n");
    cl(mk(HKEY_CURRENT_USER, "Software\\Newer"));
    T_TRUE(!exists(state_path("registry.txt.tmp")));
    now = reg_text(NULL);
    T_TRUE(contains(now, "Software\\Newer") && !contains(now, "Stale"));
    free(now);
    rt_registry_test_reload();
    T_TRUE(!has_key(HKEY_CURRENT_USER, "Stale"));
    free(good);
}

/* ---- a save that cannot be done ------------------------------------------------------------------- */

TEST(core, registry_save_failure_stops_and_keeps_the_good_file)
{
    cl(mk(HKEY_CURRENT_USER, "Software\\Kept"));
    size_t good_len;
    char *good = reg_text(&good_len);
    T_REQUIRE(good != NULL);

    T_CASE("a directory that cannot be written: the runtime stops, naming the file and the reason");
    T_REQUIRE(chmod(rt_test_state_dir(), 0500) == 0);
    HKEY k = NULL;
    T_TRAPS(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("Software\\Lost"), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL), "cannot save to");
    T_TRAPS(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("Software\\Lost"), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL), "registry.txt");
    T_TRAPS(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("Software\\Lost"), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL), "Permission denied");
    T_REQUIRE(chmod(rt_test_state_dir(), 0700) == 0);

    T_CASE("the good file is exactly as it was");
    size_t len;
    char *now = reg_text(&len);
    T_TRUE(same_bytes(good, good_len, now, len));
    free(now);
    T_TRUE(!exists(state_path("registry.txt.tmp")));

    T_CASE("a value written when the save fails stops the runtime too");
    HKEY h = op(HKEY_CURRENT_USER, "Software\\Kept", KEY_ALL_ACCESS);
    T_REQUIRE(chmod(rt_test_state_dir(), 0500) == 0);
    T_TRAPS(put_dw(h, "v", 1), "cannot save to");
    T_TRAPS(rt_reg_delete_key_ex_w(HKEY_CURRENT_USER, W("Software\\Kept"), 0, 0), "cannot save to");
    T_REQUIRE(chmod(rt_test_state_dir(), 0700) == 0);
    now = reg_text(&len);
    T_TRUE(same_bytes(good, good_len, now, len));
    free(now);

    T_CASE("the same calls work once the directory can be written");
    T_EQ(put_dw(h, "v", 1), ERROR_SUCCESS);
    now = reg_text(NULL);
    T_TRUE(contains(now, "\"v\" = dword:00000001"));
    free(now);
    cl(h);
    free(good);
}

TEST(core, registry_save_rename_failure)
{
    T_CASE("a rename that fails (the file's name is taken by a directory) stops the runtime and names the step");
    cl(mk(HKEY_CURRENT_USER, "Software\\First"));
    T_TRUE(unlink(rt_registry_path()) == 0);
    T_TRUE(mkdir(rt_registry_path(), 0700) == 0);
    HKEY k = NULL;
    T_TRAPS(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("Software\\Second"), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL), "rename");
    T_TRAPS(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("Software\\Second"), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL), "cannot save to");
    T_TRUE(rmdir(rt_registry_path()) == 0);
    unlink(state_path("registry.txt.tmp"));
}

TEST(core, registry_load_refuses_a_directory_and_unreadable_files)
{
    T_CASE("a path that is not a regular file, or cannot be read, stops the runtime at start-up");
    put_file("titanfall-registry 1\n");
    T_REQUIRE(chmod(rt_registry_path(), 0000) == 0);
    T_TRAPS(rt_registry_test_reload(), "cannot read");
    T_REQUIRE(chmod(rt_registry_path(), 0600) == 0);
    T_TRUE(unlink(rt_registry_path()) == 0);
    T_TRUE(mkdir(rt_registry_path(), 0700) == 0);
    T_TRAPS(rt_registry_test_reload(), "is not a regular file");
    T_TRUE(rmdir(rt_registry_path()) == 0);
    rt_registry_test_reload();                              /* absent again: an empty registry */
}

/* ---- killed in the middle of saving --------------------------------------------------------------- */

#define BIG 200000u

static unsigned lcg(unsigned *s) { *s = *s * 1664525u + 1013904223u; return *s >> 8; }

/* A child that changes the registry as fast as it can until it is killed.  No test checks here: the
 * parent looks at the file afterwards. */
static _Noreturn void writer_child(unsigned first)
{
    HKEY k = NULL;
    rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("Software\\Torture"), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    BYTE *big = malloc(BIG);
    for (unsigned i = first;; i++) {
        memset(big, (int)(i & 0xFF), BIG);
        rt_reg_set_value_ex_w(k, W("big"), 0, REG_BINARY, big, BIG);
        rt_reg_set_value_ex_w(k, W("n"), 0, REG_DWORD, (const BYTE *)&i, 4);
        HKEY sub = NULL;
        char name[16];
        snprintf(name, sizeof name, "s%u", i % 7);
        rt_reg_create_key_ex_w(k, W(name), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &sub, NULL);
        if (sub) rt_reg_close_key(sub);
        if (i % 3 == 0) rt_reg_delete_key_ex_w(k, W(name), 0, 0);
    }
}

TEST(core, registry_save_killed_mid_write)
{
    T_CASE("a process killed at any moment of a stream of saves leaves a file that reads, with a consistent value");
    unsigned seed = 12345;
    unsigned next = 1, max_n = 0;
    int survived_with_data = 0, stale = 0;
    /* At least 14 kills, and more until three of them have left a temporary behind, which is proof that the
     * process died in the middle of a save. */
    for (int round = 0; round < 100 && (round < 14 || stale < 3); round++) {
        pid_t pid = fork();
        T_REQUIRE(pid >= 0);
        if (pid == 0) writer_child(next);
        usleep(2000 + lcg(&seed) % 30000);
        kill(pid, SIGKILL);
        int status;
        waitpid(pid, &status, 0);
        T_TRUE(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);     /* our kill, and not a report of its own */
        if (exists(state_path("registry.txt.tmp"))) stale++;

        rt_registry_test_reload();                          /* stops the test if the file does not parse */
        T_TRUE(!exists(state_path("registry.txt.tmp")));
        HKEY k = NULL;
        if (open_st(HKEY_CURRENT_USER, "Software\\Torture", KEY_READ, &k) != ERROR_SUCCESS) continue;
        survived_with_data++;
        DWORD n = 0, cb = 4;
        if (rt_reg_query_value_ex_w(k, W("n"), NULL, NULL, (BYTE *)&n, &cb) == ERROR_SUCCESS) {
            T_TRUE(n >= max_n);                             /* the file only moves forward */
            max_n = n;
            next = n + 1;
        }
        DWORD size = 0;
        if (rt_reg_query_value_ex_w(k, W("big"), NULL, NULL, NULL, &size) == ERROR_SUCCESS) {
            T_EQ(size, BIG);
            BYTE *buf = malloc(BIG);
            cb = BIG;
            T_EQ(rt_reg_query_value_ex_w(k, W("big"), NULL, NULL, buf, &cb), ERROR_SUCCESS);
            bool uniform = true;
            for (size_t i = 1; i < BIG; i++) if (buf[i] != buf[0]) { uniform = false; break; }
            T_TRUE(uniform);                                /* all of one write, never a mixture */
            free(buf);
        }
        cl(k);
        /* The file on disk reads again, and says what the tree says. */
        char *t = reg_text(NULL);
        T_TRUE(t && strncmp(t, "# Registry", 10) == 0);
        free(t);
    }
    T_TRUE(survived_with_data > 0);
    T_TRUE(stale >= 3);
}

TEST(core, registry_save_large_tree)
{
    T_CASE("a registry of thousands of keys and values reads, enumerates and is written back");
    const int KEYS = 3000;
    size_t cap = 400 + (size_t)KEYS * 160;
    char *t = malloc(cap);
    T_REQUIRE(t != NULL);
    size_t n = (size_t)snprintf(t, cap, "titanfall-registry 1\n[HKEY_CURRENT_USER\\Software\\Big] time=0000000000000007\n");
    for (int i = 0; i < KEYS; i++)
        n += (size_t)snprintf(t + n, cap - n, "[HKEY_CURRENT_USER\\Software\\Big\\key%05d] time=00000000000%05d\n"
                                              "\"a\" = dword:%08x\n\"b\" = sz:\"value %d\"\n\"c\" = hex:%02x,%02x\n",
                              (i * 7919) % KEYS, i, (unsigned)i, i, i & 255, (i >> 8) & 255);
    put_file(t);
    free(t);
    rt_registry_test_reload();
    HKEY big = op(HKEY_CURRENT_USER, "Software\\Big", KEY_ALL_ACCESS);
    DWORD nkeys = 0;
    counts(big, &nkeys, NULL);
    T_EQ(nkeys, KEYS);
    char name[32];
    for (DWORD i = 0; i < nkeys; i += 997) {
        T_TRUE(key_name(big, i, name, sizeof name));
        char want[32];
        snprintf(want, sizeof want, "key%05u", i);
        T_TRUE(!strcmp(name, want));
    }
    HKEY one = op(big, "key01234", KEY_READ);
    DWORD nvals = 0;
    counts(one, NULL, &nvals);
    T_EQ(nvals, 3);
    cl(one);
    T_EQ(rt_reg_delete_key_ex_w(big, W("key01234"), 0, 0), ERROR_SUCCESS);
    rt_registry_test_reload();
    HKEY again = op(HKEY_CURRENT_USER, "Software\\Big", KEY_READ);
    counts(again, &nkeys, NULL);
    T_EQ(nkeys, KEYS - 1);
    cl(again);
    cl(big);
}
