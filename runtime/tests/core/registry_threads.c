/* The registry (rt/registry.h): several host threads at once.  The core functions are called
 * directly from plain host threads (guest or not: they take one lock).  The stress on values and
 * handles uses volatile keys, which are not saved, so that the lock and the tree are what is under
 * test and not the disk; the one test that saves from several threads is small. */
#include <stdatomic.h>

#include "registry_util.c"

typedef struct { pthread_mutex_t m; pthread_cond_t c; unsigned n, count, round; } Barrier;

static void barrier_init(Barrier *b, unsigned n) {
    pthread_mutex_init(&b->m, NULL);
    pthread_cond_init(&b->c, NULL);
    b->n = n; b->count = 0; b->round = 0;
}

static void barrier_wait(Barrier *b) {
    pthread_mutex_lock(&b->m);
    unsigned round = b->round;
    if (++b->count == b->n) { b->count = 0; b->round++; pthread_cond_broadcast(&b->c); }
    else while (round == b->round) pthread_cond_wait(&b->c, &b->m);
    pthread_mutex_unlock(&b->m);
}

typedef struct { unsigned id; Barrier *bar; } Job;

static _Atomic unsigned failures;
#define CHECK(cond) do { if (!(cond)) atomic_fetch_add(&failures, 1); } while (0)

/* ASCII to WCHAR into the caller's buffer: W() of registry_util.c is not for threads. */
static WCHAR *tw(WCHAR *out, const char *s) {
    size_t i = 0;
    for (; s[i]; i++) out[i] = (unsigned char)s[i];
    out[i] = 0;
    return out;
}
#define TW(s) tw((WCHAR[256]){ 0 }, (s))

static void run_threads(unsigned n, void *(*fn)(void *)) {
    pthread_t th[32];
    Job jobs[32];
    Barrier bar;
    barrier_init(&bar, n);
    T_REQUIRE(n <= 32);
    atomic_store(&failures, 0);
    for (unsigned i = 0; i < n; i++) {
        jobs[i] = (Job){ .id = i, .bar = &bar };
        T_REQUIRE(pthread_create(&th[i], NULL, fn, &jobs[i]) == 0);
    }
    for (unsigned i = 0; i < n; i++) pthread_join(th[i], NULL);
}

/* ---- distinct keys made at once, and listed meanwhile ------------------------------------------------- */

#define CREATORS 6
#define KEYS_EACH 10

static void *creator_or_lister(void *p) {
    Job *j = p;
    barrier_wait(j->bar);
    HKEY root = NULL;
    CHECK(rt_reg_open_key_ex_w(HKEY_CURRENT_USER, TW("Software\\Threads"), 0, KEY_ALL_ACCESS, &root) == ERROR_SUCCESS);
    if (j->id < CREATORS) {
        for (unsigned i = 0; i < KEYS_EACH; i++) {
            char text[32];
            snprintf(text, sizeof text, "t%u-k%u", j->id, i);
            HKEY h = NULL;
            DWORD disp = 0;
            CHECK(rt_reg_create_key_ex_w(root, TW(text), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &h, &disp) == ERROR_SUCCESS);
            CHECK(disp == REG_CREATED_NEW_KEY);
            DWORD v = j->id * 1000 + i;
            CHECK(rt_reg_set_value_ex_w(h, TW("v"), 0, REG_DWORD, (const BYTE *)&v, 4) == ERROR_SUCCESS);
            CHECK(rt_reg_set_value_ex_w(h, NULL, 0, REG_DWORD, (const BYTE *)&v, 4) == ERROR_SUCCESS);
            DWORD back = 0, cb = 4;
            CHECK(rt_reg_query_value_ex_w(h, TW("v"), NULL, NULL, (BYTE *)&back, &cb) == ERROR_SUCCESS && back == v);
            CHECK(rt_reg_close_key(h) == ERROR_SUCCESS);
        }
    } else {
        for (int round = 0; round < 40; round++) {
            WCHAR name[32];
            for (DWORD i = 0;; i++) {
                DWORD cch = 32;
                LSTATUS st = rt_reg_enum_key_ex_w(root, i, name, &cch, NULL, NULL, NULL, NULL);
                if (st == ERROR_NO_MORE_ITEMS) break;
                CHECK(st == ERROR_SUCCESS);
                if (st != ERROR_SUCCESS) break;
                CHECK(cch >= 4 && name[0] == 't');
            }
            DWORD n = 0;
            CHECK(rt_reg_query_info_key_w(root, NULL, NULL, NULL, &n, NULL, NULL, NULL, NULL, NULL, NULL, NULL) == ERROR_SUCCESS);
        }
    }
    CHECK(rt_reg_close_key(root) == ERROR_SUCCESS);
    return NULL;
}

TEST(core, registry_threads_create_distinct_keys)
{
    T_CASE("threads creating their own keys and values, while others list them, lose nothing");
    cl(mk(HKEY_CURRENT_USER, "Software\\Threads"));
    run_threads(CREATORS + 2, creator_or_lister);
    T_EQ(atomic_load(&failures), 0);
    HKEY root = op(HKEY_CURRENT_USER, "Software\\Threads", KEY_READ);
    DWORD nkeys = 0;
    counts(root, &nkeys, NULL);
    T_EQ(nkeys, CREATORS * KEYS_EACH);
    for (unsigned t = 0; t < CREATORS; t++)
        for (unsigned i = 0; i < KEYS_EACH; i++) {
            char name[32];
            snprintf(name, sizeof name, "t%u-k%u", t, i);
            HKEY h = op(root, name, KEY_READ);
            if (h) { T_TRUE(is_dw(h, "v", t * 1000 + i) && is_dw(h, NULL, t * 1000 + i)); cl(h); }
        }
    cl(root);

    T_CASE("and the file says the same as the tree");
    rt_registry_test_reload();
    root = op(HKEY_CURRENT_USER, "Software\\Threads", KEY_READ);
    counts(root, &nkeys, NULL);
    T_EQ(nkeys, CREATORS * KEYS_EACH);
    HKEY h = op(root, "t3-k7", KEY_READ);
    T_TRUE(is_dw(h, "v", 3007));
    cl(h);
    cl(root);
}

/* ---- the same key made by everyone ---------------------------------------------------------------------- */

#define RACERS 8
#define ROUNDS 200
static _Atomic unsigned created_count[ROUNDS];

static void *racer(void *p) {
    Job *j = p;
    for (unsigned round = 0; round < ROUNDS; round++) {
        barrier_wait(j->bar);
        char text[32];
        snprintf(text, sizeof text, "Software\\Race\\r%u", round);
        HKEY h = NULL;
        DWORD disp = 0;
        LSTATUS st = rt_reg_create_key_ex_w(HKEY_CURRENT_USER, TW(text), 0, NULL, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &h, &disp);
        CHECK(st == ERROR_SUCCESS);
        if (st == ERROR_SUCCESS) {
            if (disp == REG_CREATED_NEW_KEY) atomic_fetch_add(&created_count[round], 1);
            else CHECK(disp == REG_OPENED_EXISTING_KEY);
            CHECK(rt_reg_close_key(h) == ERROR_SUCCESS);
        }
    }
    return NULL;
}

TEST(core, registry_threads_create_race)
{
    T_CASE("of several threads creating the same key, exactly one sees it created");
    HKEY volroot = NULL;
    T_EQ(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("Software\\Race"), 0, NULL, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &volroot, NULL), ERROR_SUCCESS);
    run_threads(RACERS, racer);
    T_EQ(atomic_load(&failures), 0);
    unsigned wrong = 0;
    for (unsigned r = 0; r < ROUNDS; r++) if (atomic_load(&created_count[r]) != 1) wrong++;
    T_EQ(wrong, 0);
    DWORD n = 0;
    counts(volroot, &n, NULL);
    T_EQ(n, ROUNDS);
    cl(volroot);
}

/* ---- a value read while it is written ---------------------------------------------------------------------- */

#define VSIZE 8192
#define WRITERS 4
#define READERS 4
#define ITERS 3000

static void *value_writer_or_reader(void *p) {
    Job *j = p;
    HKEY k = NULL;
    CHECK(rt_reg_open_key_ex_w(HKEY_CURRENT_USER, TW("Software\\Tear"), 0, KEY_ALL_ACCESS, &k) == ERROR_SUCCESS);
    BYTE *buf = malloc(VSIZE);
    barrier_wait(j->bar);
    if (j->id < WRITERS) {
        for (unsigned i = 0; i < ITERS; i++) {
            DWORD size = 1 + (i * 37 + j->id) % VSIZE;
            memset(buf, (int)(j->id + 1), size);
            CHECK(rt_reg_set_value_ex_w(k, TW("v"), 0, REG_BINARY, buf, size) == ERROR_SUCCESS);
            if (i % 5 == 0) CHECK(rt_reg_set_value_ex_w(k, TW("other"), 0, REG_DWORD, (const BYTE *)&i, 4) == ERROR_SUCCESS);
        }
    } else {
        for (unsigned i = 0; i < ITERS; i++) {
            DWORD cb = VSIZE, type = 0;
            LSTATUS st = rt_reg_query_value_ex_w(k, TW("v"), NULL, &type, buf, &cb);
            if (st == ERROR_FILE_NOT_FOUND) continue;       /* before the first write */
            CHECK(st == ERROR_SUCCESS && type == REG_BINARY && cb >= 1 && cb <= VSIZE);
            if (st != ERROR_SUCCESS || cb < 1) continue;
            bool uniform = buf[0] >= 1 && buf[0] <= WRITERS;
            for (DWORD b = 1; b < cb && uniform; b++) if (buf[b] != buf[0]) uniform = false;
            CHECK(uniform);                                 /* one writer's data, never a mixture of two */
        }
    }
    free(buf);
    CHECK(rt_reg_close_key(k) == ERROR_SUCCESS);
    return NULL;
}

TEST(core, registry_threads_value_not_torn)
{
    T_CASE("a value read while writers replace it is always one writer's whole data");
    cl(mk(HKEY_CURRENT_USER, "Software"));
    HKEY k = NULL;
    T_EQ(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("Software\\Tear"), 0, NULL, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &k, NULL), ERROR_SUCCESS);
    run_threads(WRITERS + READERS, value_writer_or_reader);
    T_EQ(atomic_load(&failures), 0);
    DWORD nvals = 0;
    counts(k, NULL, &nvals);
    T_EQ(nvals, 2);
    cl(k);
}

/* ---- many threads, one handle ---------------------------------------------------------------------------- */

#define CLOSERS 8
#define HANDLES 150
static HKEY shared[HANDLES];
static _Atomic unsigned closed_ok[HANDLES];

static void *closer(void *p) {
    Job *j = p;
    for (unsigned i = 0; i < HANDLES; i++) {
        barrier_wait(j->bar);
        LSTATUS st = rt_reg_close_key(shared[i]);
        CHECK(st == ERROR_SUCCESS || st == ERROR_INVALID_HANDLE);
        if (st == ERROR_SUCCESS) atomic_fetch_add(&closed_ok[i], 1);
    }
    return NULL;
}

TEST(core, registry_threads_close_race)
{
    T_CASE("of several threads closing the same handle at once, exactly one succeeds");
    HKEY volroot = NULL;
    T_EQ(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("CloseRace"), 0, NULL, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &volroot, NULL), ERROR_SUCCESS);
    for (unsigned i = 0; i < HANDLES; i++) T_EQ(rt_reg_open_key_ex_w(volroot, NULL, 0, KEY_READ, &shared[i]), ERROR_SUCCESS);
    run_threads(CLOSERS, closer);
    T_EQ(atomic_load(&failures), 0);
    unsigned wrong = 0;
    for (unsigned i = 0; i < HANDLES; i++) if (atomic_load(&closed_ok[i]) != 1) wrong++;
    T_EQ(wrong, 0);
    cl(volroot);
}

/* ---- a key used while it is deleted --------------------------------------------------------------------- */

#define USERS 6
#define VICTIMS 100
static HKEY victim_parent;

static void *victim_user(void *p) {
    Job *j = p;
    for (unsigned round = 0; round < VICTIMS; round++) {
        barrier_wait(j->bar);
        if (j->id == 0) {                                   /* the deleter */
            char text[32];
            snprintf(text, sizeof text, "v%u", round);
            usleep(round % 7 * 50);
            LSTATUS st = rt_reg_delete_key_ex_w(victim_parent, TW(text), 0, 0);
            CHECK(st == ERROR_SUCCESS);
        } else {
            char text[32];
            snprintf(text, sizeof text, "v%u", round);
            HKEY h = NULL;
            LSTATUS st = rt_reg_open_key_ex_w(victim_parent, TW(text), 0, KEY_ALL_ACCESS, &h);
            if (st != ERROR_SUCCESS) { CHECK(st == ERROR_FILE_NOT_FOUND); continue; }
            for (int i = 0; i < 20; i++) {
                DWORD x = 5, cb = 4, nkeys = 0;
                LSTATUS a = rt_reg_set_value_ex_w(h, TW("x"), 0, REG_DWORD, (const BYTE *)&x, 4);
                LSTATUS b = rt_reg_query_value_ex_w(h, TW("x"), NULL, NULL, (BYTE *)&x, &cb);
                LSTATUS c = rt_reg_query_info_key_w(h, NULL, NULL, NULL, &nkeys, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
                CHECK(a == ERROR_SUCCESS || a == ERROR_KEY_DELETED);
                CHECK(b == ERROR_SUCCESS || b == ERROR_KEY_DELETED || b == ERROR_FILE_NOT_FOUND);
                CHECK(c == ERROR_SUCCESS || c == ERROR_KEY_DELETED);
            }
            CHECK(rt_reg_close_key(h) == ERROR_SUCCESS);
        }
    }
    return NULL;
}

TEST(core, registry_threads_use_while_delete)
{
    T_CASE("a handle used while its key is deleted gives its normal answer or ERROR_KEY_DELETED, and closes");
    T_EQ(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("Victims"), 0, NULL, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &victim_parent, NULL), ERROR_SUCCESS);
    for (unsigned i = 0; i < VICTIMS; i++) {
        char text[32];
        snprintf(text, sizeof text, "v%u", i);
        HKEY h = NULL;
        T_EQ(rt_reg_create_key_ex_w(victim_parent, W(text), 0, NULL, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &h, NULL), ERROR_SUCCESS);
        cl(h);
    }
    run_threads(USERS, victim_user);
    T_EQ(atomic_load(&failures), 0);
    DWORD n = 99;
    counts(victim_parent, &n, NULL);
    T_EQ(n, 0);
    cl(victim_parent);
}

/* ---- enumerating while the keys change --------------------------------------------------------------------- */

#define CHURN 4
#define LISTERS 3
static const WCHAR cls_kls[] = { 'k', 'l', 's', 0 };

static void *churn_or_list(void *p) {
    Job *j = p;
    HKEY root = NULL;
    CHECK(rt_reg_open_key_ex_w(HKEY_CURRENT_USER, TW("Churn"), 0, KEY_ALL_ACCESS, &root) == ERROR_SUCCESS);
    barrier_wait(j->bar);
    if (j->id < CHURN) {
        for (unsigned i = 0; i < 1500; i++) {
            char text[32];
            snprintf(text, sizeof text, "c%u-%u", j->id, i % 12);
            HKEY h = NULL;
            CHECK(rt_reg_create_key_ex_w(root, TW(text), 0, cls_kls, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &h, NULL) == ERROR_SUCCESS);
            if (h) {
                DWORD v = i;
                CHECK(rt_reg_set_value_ex_w(h, TW("v"), 0, REG_DWORD, (const BYTE *)&v, 4) == ERROR_SUCCESS);
                CHECK(rt_reg_close_key(h) == ERROR_SUCCESS);
            }
            if (i % 2) {
                LSTATUS st = rt_reg_delete_key_ex_w(root, TW(text), 0, 0);
                CHECK(st == ERROR_SUCCESS || st == ERROR_FILE_NOT_FOUND);
            }
        }
    } else {
        for (unsigned round = 0; round < 800; round++) {
            WCHAR name[32], cls[32];
            DWORD seen = 0;
            for (DWORD i = 0;; i++) {
                DWORD cn = 32, cc = 32;
                FILETIME ft;
                LSTATUS st = rt_reg_enum_key_ex_w(root, i, name, &cn, NULL, cls, &cc, &ft);
                if (st == ERROR_NO_MORE_ITEMS) break;
                CHECK(st == ERROR_SUCCESS);
                if (st != ERROR_SUCCESS) break;
                CHECK(name[0] == 'c' && cn >= 4 && cn <= 6);
                CHECK(cc == 3 && cls[0] == 'k' && cls[1] == 'l' && cls[2] == 's');
                seen++;
            }
            DWORD n = 0;
            CHECK(rt_reg_query_info_key_w(root, NULL, NULL, NULL, &n, NULL, NULL, NULL, NULL, NULL, NULL, NULL) == ERROR_SUCCESS);
            CHECK(seen <= CHURN * 12 && n <= CHURN * 12);
        }
    }
    CHECK(rt_reg_close_key(root) == ERROR_SUCCESS);
    return NULL;
}

TEST(core, registry_threads_enum_while_changing)
{
    T_CASE("listing keys that are made and deleted meanwhile gives whole entries and then ERROR_NO_MORE_ITEMS");
    HKEY root = NULL;
    T_EQ(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("Churn"), 0, NULL, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &root, NULL), ERROR_SUCCESS);
    run_threads(CHURN + LISTERS, churn_or_list);
    T_EQ(atomic_load(&failures), 0);
    cl(root);
}

/* ---- handle churn -------------------------------------------------------------------------------------- */

static void *handle_churn(void *p) {
    Job *j = p;
    barrier_wait(j->bar);
    for (unsigned i = 0; i < 4000; i++) {
        HKEY h = NULL;
        DWORD disp = 0;
        char text[32];
        snprintf(text, sizeof text, "HandleChurn\\a%u\\b%u", j->id, i % 5);
        CHECK(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, TW(text), 0, NULL, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &h, &disp) == ERROR_SUCCESS);
        HKEY h2 = NULL;
        CHECK(rt_reg_open_key_ex_w(h, NULL, 0, KEY_READ, &h2) == ERROR_SUCCESS);
        CHECK(rt_reg_close_key(h) == ERROR_SUCCESS);
        CHECK(rt_reg_close_key(h2) == ERROR_SUCCESS);
        CHECK(rt_reg_close_key(h2) == ERROR_INVALID_HANDLE);
    }
    return NULL;
}

TEST(core, registry_threads_handle_churn)
{
    T_CASE("handles opened and closed by many threads: every close once, no handle reused while open");
    HKEY root = NULL;
    T_EQ(rt_reg_create_key_ex_w(HKEY_CURRENT_USER, W("HandleChurn"), 0, NULL, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &root, NULL), ERROR_SUCCESS);
    run_threads(8, handle_churn);
    T_EQ(atomic_load(&failures), 0);
    DWORD n = 0;
    counts(root, &n, NULL);
    T_EQ(n, 8);
    cl(root);
}
