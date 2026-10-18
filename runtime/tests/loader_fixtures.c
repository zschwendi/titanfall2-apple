/* Test support for the loader's tests (rt/test_pe.h): the "lifted code" of the test slots
 * t_lift_a/b/c.dll and t_ui(11).dll (slot d) and files in the test's install root.  The PE writer declared in
 * the same header is runtime/tests/pe_build.c. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "rt/test_pe.h"

static LiftedFn hooks[4][RT_TEST_LIFT_FNS];
static const char slot_names[4] = { 'a', 'b', 'c', 'd' };

static void run(Ctx *ctx, unsigned slot, unsigned i) {
    if (!hooks[slot][i]) rt_fatal("fixture function t_lift_%c.dll+%#x is not set (rt_test_lift_set)", slot_names[slot], RT_TEST_LIFT_RVA(i));
    hooks[slot][i](ctx);
}

#define FN(s, n, i)  static void t_lift_##s##_##i(Ctx *ctx) { run(ctx, n, i); }
#define SLOT(s, n) \
    FN(s, n, 0) FN(s, n, 1) FN(s, n, 2) FN(s, n, 3) FN(s, n, 4) FN(s, n, 5) FN(s, n, 6) FN(s, n, 7) \
    uint64_t g_base_t_lift_##s; \
    const LiftedEntry t_lift_##s##_table[] = { \
        { RT_TEST_LIFT_RVA(0), t_lift_##s##_0 }, { RT_TEST_LIFT_RVA(1), t_lift_##s##_1 }, \
        { RT_TEST_LIFT_RVA(2), t_lift_##s##_2 }, { RT_TEST_LIFT_RVA(3), t_lift_##s##_3 }, \
        { RT_TEST_LIFT_RVA(4), t_lift_##s##_4 }, { RT_TEST_LIFT_RVA(5), t_lift_##s##_5 }, \
        { RT_TEST_LIFT_RVA(6), t_lift_##s##_6 }, { RT_TEST_LIFT_RVA(7), t_lift_##s##_7 }, { 0, NULL } }; \
    const size_t t_lift_##s##_table_count = RT_TEST_LIFT_FNS; \
    const LiftedSkip t_lift_##s##_skips[] = { { 0, 0, NULL } }; \
    const size_t t_lift_##s##_skips_count = 0;
SLOT(a, 0)
SLOT(b, 1)
SLOT(c, 2)
SLOT(d, 3)                          /* t_ui(11).dll, the late row (module=t_lift_d) */

static _Noreturn void die(const char *what, const char *arg) {
    fprintf(stderr, "test support: %s: %s (%s)\n", what, arg, errno ? strerror(errno) : "misuse");
    abort();
}

void rt_test_lift_set(char slot, unsigned i, LiftedFn fn) {
    if (slot < 'a' || slot > 'd' || i >= RT_TEST_LIFT_FNS) { errno = 0; die("rt_test_lift_set", "no such slot or function"); }
    hooks[slot - 'a'][i] = fn;
}

void rt_test_install_write(const char *rel, const void *bytes, size_t n) {
    char path[2048];
    snprintf(path, sizeof path, "%s/%s", rt_test_install_root(), rel);
    for (char *p = path + strlen(rt_test_install_root()) + 1; *p; p++) {
        if (*p != '/') continue;
        *p = 0;
        if (mkdir(path, 0777) != 0 && errno != EEXIST) die("cannot create directory", path);
        *p = '/';
    }
    FILE *f = fopen(path, "wb");
    if (!f || fwrite(bytes, 1, n, f) != n || fclose(f) != 0) die("cannot write", path);
}

void rt_test_install_copy(const char *host_src, const char *rel) {
    FILE *f = fopen(host_src, "rb");
    if (!f) die("cannot open", host_src);
    static unsigned char buf[4 << 20];
    size_t n = fread(buf, 1, sizeof buf, f);
    if (ferror(f) || n == sizeof buf) die("cannot read (or larger than 4 MB)", host_src);
    fclose(f);
    rt_test_install_write(rel, buf, n);
}
