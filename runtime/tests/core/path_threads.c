/* The path subsystem from several threads at once: translations (with the listing cache in use), the code that
 * creates and deletes files and invalidates, the guest's current directory being set and read, the reverse mapping, and
 * enough directories to make the cache evict.  The workers are plain host threads (the functions need no guest thread); they
 * only count what went wrong, and the test thread checks the counts.
 */
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/path.h"

void rt_path_test_force_case(int mode);
void rt_path_test_stats(unsigned *reads, unsigned *cached);

static const char *join(const char *base, const char *rel) {
    static char buf[4][RT_PATH_MAX * 2];
    static unsigned k;
    char *b = buf[k++ & 3];
    char tmp[RT_PATH_MAX * 2];
    snprintf(tmp, sizeof tmp, *rel ? "%s/%s" : "%s", base, rel);
    size_t o = 0;                                   /* the runner's TMPDIR ends in a slash: the root is spelled with "//" */
    for (const char *q = tmp; *q; q++) if (!(*q == '/' && o > 0 && b[o - 1] == '/')) b[o++] = *q;
    b[o] = 0;
    return b;
}
static const char *I(const char *rel) { return join(rt_test_install_root(), rel); }

static void mkdir_p(const char *abs) {
    char b[RT_PATH_MAX * 2];
    snprintf(b, sizeof b, "%s", abs);
    for (char *p = b + 1; *p; p++) {
        if (*p != '/') continue;
        *p = 0;
        mkdir(b, 0755);
        *p = '/';
    }
    if (mkdir(b, 0755) && errno != EEXIST) rt_test_fail(__FILE__, __LINE__, "mkdir %s: %s", abs, strerror(errno));
}
static void touch(const char *abs) {
    int fd = open(abs, O_CREAT | O_WRONLY, 0644);
    if (fd < 0) { rt_test_fail(__FILE__, __LINE__, "create %s: %s", abs, strerror(errno)); return; }
    close(fd);
}
static void age(const char *path) {
    static time_t t;
    if (!t) t = time(NULL) - 3600;
    struct timespec ts[2] = { { t, 0 }, { t, 0 } };
    t--;
    utimensat(AT_FDCWD, path, ts, 0);
}

static void widen(const char *a, WCHAR *w) {
    size_t i = 0;
    for (; a[i]; i++) w[i] = (unsigned char)a[i];
    w[i] = 0;
}

/* ---- what the workers share ------------------------------------------------------------------------------------ */

typedef struct { WCHAR w[96]; char host[RT_PATH_MAX]; } Case;

#define NDIR   8
#define NFILE  8
#define NEVICT 40
static Case dirs[NDIR][NFILE];       /* C:\titanfall2\dir<d>\file<f>.txt  ->  <install>/Dir<d>/File<f>.TXT */
static Case evict[NEVICT];           /* C:\titanfall2\EV<n>\item          ->  <install>/ev<n>/Item */
static char churn_dir[RT_PATH_MAX];
static WCHAR rev_want[3][96];
static char rev_host[3][RT_PATH_MAX];
static atomic_uint bad, ran;

#define ITER 10000

static uint64_t next(uint64_t *x) {
    *x ^= *x << 13;
    *x ^= *x >> 7;
    *x ^= *x << 17;
    return *x;
}

static void *translator(void *arg) {
    uint64_t x = ((uint64_t)(uintptr_t)arg + 1) * UINT64_C(0x9E3779B97F4A7C15);
    for (int i = 0; i < ITER; i++) {
        uint64_t r = next(&x);
        const Case *c = &dirs[r % NDIR][(r >> 8) % NFILE];
        char h[RT_PATH_MAX];
        DWORD e = rt_path_to_host_w(c->w, RT_PATH_EXISTING, h);
        if (e != 0 || strcmp(h, c->host)) atomic_fetch_add(&bad, 1);
        atomic_fetch_add(&ran, 1);
    }
    return NULL;
}

static void *evictor(void *arg) {
    uint64_t x = ((uint64_t)(uintptr_t)arg + 1) * UINT64_C(0xD1B54A32D192ED03);
    for (int i = 0; i < ITER / 4; i++) {
        const Case *c = &evict[next(&x) % NEVICT];
        char h[RT_PATH_MAX];
        DWORD e = rt_path_to_host_w(c->w, RT_PATH_EXISTING, h);
        if (e != 0 || strcmp(h, c->host)) atomic_fetch_add(&bad, 1);
        atomic_fetch_add(&ran, 1);
    }
    return NULL;
}

/* Create and delete files in one directory, telling the runtime each time, and look for them. */
static void *churner(void *arg) {
    (void)arg;
    for (int k = 0; k < 300; k++) {
        char file[RT_PATH_MAX], win[128];
        WCHAR w[128];
        char h[RT_PATH_MAX];
        snprintf(file, sizeof file, "%s/Item%d.Dat", churn_dir, k);
        snprintf(win, sizeof win, "C:\\titanfall2\\churn\\ITEM%d.DAT", k);
        widen(win, w);
        int fd = open(file, O_CREAT | O_WRONLY, 0644);
        if (fd < 0) { atomic_fetch_add(&bad, 1); continue; }
        close(fd);
        rt_path_invalidate(churn_dir);
        DWORD e = rt_path_to_host_w(w, RT_PATH_EXISTING, h);
        if (e != 0 || strcmp(h, file)) atomic_fetch_add(&bad, 1);
        unlink(file);
        rt_path_invalidate(churn_dir);
        e = rt_path_to_host_w(w, RT_PATH_EXISTING, h);
        if (e != ERROR_FILE_NOT_FOUND) atomic_fetch_add(&bad, 1);
        atomic_fetch_add(&ran, 1);
    }
    return NULL;
}

/* Drop the cached listings of the install root and of Dir0, over and over. */
static char root_dir[RT_PATH_MAX], dir0[RT_PATH_MAX];
static void *invalidator(void *arg) {
    (void)arg;
    for (int i = 0; i < ITER; i++) {
        rt_path_invalidate(root_dir);
        rt_path_invalidate(dir0);
        atomic_fetch_add(&ran, 1);
    }
    return NULL;
}

static const WCHAR cwd_a[] = { 'C', ':', '\\', 'T', 'i', 't', 'a', 'n', 'f', 'a', 'l', 'l', '2', '\\', 'D', 'i', 'r', '1', 0 };
static const WCHAR cwd_b[] = { 'C', ':', '\\', 'T', 'i', 't', 'a', 'n', 'f', 'a', 'l', 'l', '2', '\\', 'D', 'i', 'r', '2', 0 };
static const WCHAR cwd_0[] = { 'C', ':', '\\', 'T', 'i', 't', 'a', 'n', 'f', 'a', 'l', 'l', '2', 0 };

static void *cwd_setter(void *arg) {
    (void)arg;
    for (int i = 0; i < ITER; i++) {
        if (rt_path_set_cwd_w(i & 1 ? cwd_a : cwd_b) != 0) atomic_fetch_add(&bad, 1);
        atomic_fetch_add(&ran, 1);
    }
    return NULL;
}

static bool same(const WCHAR *a, const WCHAR *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

/* Read the current directory and translate a relative name: the answers must be those of one of the directories it has been. */
static void *cwd_reader(void *arg) {
    (void)arg;
    static const WCHAR rel[] = { 'f', 'i', 'l', 'e', '0', '.', 't', 'x', 't', 0 };
    for (int i = 0; i < ITER; i++) {
        WCHAR w[64];
        DWORD n = rt_path_get_cwd_w(w, 64);
        if (n != 13 && n != 18) atomic_fetch_add(&bad, 1);
        else if (!(same(w, cwd_0) || same(w, cwd_a) || same(w, cwd_b))) atomic_fetch_add(&bad, 1);
        char h[RT_PATH_MAX];
        DWORD e = rt_path_to_host_w(rel, RT_PATH_EXISTING, h);
        if (e == 0) {
            if (strcmp(h, dirs[1][0].host) && strcmp(h, dirs[2][0].host)) atomic_fetch_add(&bad, 1);
        } else if (e != ERROR_FILE_NOT_FOUND) {                                  /* the cwd was still C:\Titanfall2 */
            atomic_fetch_add(&bad, 1);
        }
        atomic_fetch_add(&ran, 1);
    }
    return NULL;
}

static void *reverser(void *arg) {
    (void)arg;
    for (int i = 0; i < ITER; i++) {
        int k = i % 3;
        WCHAR w[96];
        int n = rt_path_to_win_w(rev_host[k], w, 96);
        if (n <= 0 || !same(w, rev_want[k])) atomic_fetch_add(&bad, 1);
        atomic_fetch_add(&ran, 1);
    }
    return NULL;
}

TEST(core, path_threads)
{
    rt_path_test_force_case(1);
    /* the tree: Dir0..Dir7 with File0.TXT..File7.TXT, ev00..ev39 with Item, and an empty Churn */
    for (int d = 0; d < NDIR; d++) {
        char rel[64], win[96];
        snprintf(rel, sizeof rel, "Dir%d", d);
        mkdir_p(I(rel));
        for (int f = 0; f < NFILE; f++) {
            snprintf(rel, sizeof rel, "Dir%d/File%d.TXT", d, f);
            touch(I(rel));
            snprintf(win, sizeof win, "C:\\titanfall2\\dir%d\\file%d.txt", d, f);
            widen(win, dirs[d][f].w);
            snprintf(dirs[d][f].host, sizeof dirs[d][f].host, "%s", I(rel));
        }
        snprintf(rel, sizeof rel, "Dir%d", d);
        age(I(rel));
    }
    for (int n = 0; n < NEVICT; n++) {
        char rel[64], win[96];
        snprintf(rel, sizeof rel, "ev%02d", n);
        mkdir_p(I(rel));
        snprintf(rel, sizeof rel, "ev%02d/Item", n);
        touch(I(rel));
        snprintf(win, sizeof win, "C:\\titanfall2\\EV%02d\\item", n);
        widen(win, evict[n].w);
        snprintf(evict[n].host, sizeof evict[n].host, "%s", I(rel));
        snprintf(rel, sizeof rel, "ev%02d", n);
        age(I(rel));
    }
    mkdir_p(I("Churn"));
    age(I("Churn"));
    snprintf(churn_dir, sizeof churn_dir, "%s", I("Churn"));
    snprintf(root_dir, sizeof root_dir, "%s", I(""));
    snprintf(dir0, sizeof dir0, "%s", I("Dir0"));
    age(I(""));
    /* the reverse mapping: three host paths and their Windows forms */
    snprintf(rev_host[0], RT_PATH_MAX, "%s", I("Dir3/File4.TXT"));
    widen("C:\\Titanfall2\\Dir3\\File4.TXT", rev_want[0]);
    snprintf(rev_host[1], RT_PATH_MAX, "%s", I(""));
    widen("C:\\Titanfall2", rev_want[1]);
    snprintf(rev_host[2], RT_PATH_MAX, "%s/home/Documents/s.sav", rt_test_state_dir());
    widen("C:\\Users\\Player\\Documents\\s.sav", rev_want[2]);

    enum { NT = 4, NE = 2, NR = 2 };
    pthread_t th[16];
    int nth = 0;
    for (int i = 0; i < NT; i++) T_REQUIRE(pthread_create(&th[nth++], NULL, translator, (void *)(uintptr_t)i) == 0);
    for (int i = 0; i < NE; i++) T_REQUIRE(pthread_create(&th[nth++], NULL, evictor, (void *)(uintptr_t)i) == 0);
    T_REQUIRE(pthread_create(&th[nth++], NULL, churner, NULL) == 0);
    T_REQUIRE(pthread_create(&th[nth++], NULL, invalidator, NULL) == 0);
    T_REQUIRE(pthread_create(&th[nth++], NULL, cwd_setter, NULL) == 0);
    for (int i = 0; i < NR; i++) T_REQUIRE(pthread_create(&th[nth++], NULL, cwd_reader, NULL) == 0);
    T_REQUIRE(pthread_create(&th[nth++], NULL, reverser, NULL) == 0);
    for (int i = 0; i < nth; i++) pthread_join(th[i], NULL);

    T_EQ(atomic_load(&bad), 0);
    T_EQ(atomic_load(&ran), NT * ITER + NE * (ITER / 4) + 300 + ITER + ITER + NR * ITER + ITER);
    unsigned c;
    rt_path_test_stats(NULL, &c);
    T_TRUE(c <= 32);
    /* afterwards everything still works */
    char h[RT_PATH_MAX];
    T_EQ(rt_path_to_host_w(dirs[5][5].w, RT_PATH_EXISTING, h), 0);
    T_TRUE(!strcmp(h, dirs[5][5].host));
}
