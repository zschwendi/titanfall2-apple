/* The handle table as a value (RUNTIME.md 4.1: "a second table instance" for window handles at
 * Gate 4).  The instance functions rt_htab_* are declared in rt/handle.h.  These tests drive small tables so that every limit is reached exactly:
 * capacity, reuse order, the generation range and its wrap, and the property that two tables
 * with different generation ranges can never mistake each other's handles. */
#include <string.h>

#include "rt/test.h"
#include "rt/handle.h"

typedef struct TObj {
    RtObject obj;
    uint32_t canary;
} TObj;

#define ALIVE 0xA11FE000u
#define SENTINEL 0x5EA1u

static unsigned destroyed_count;
static void tobj_destroy(RtObject *o) {
    T_TRUE(((TObj *)o)->canary == ALIVE);
    ((TObj *)o)->canary = 0;
    destroyed_count++;
}
static const RtObjectOps tops = { .name = "test", .destroy = tobj_destroy };
static const RtObjectOps find_ops = { .name = "test-find", .destroy = tobj_destroy, .no_close_handle = true };

static TObj *mk_ops(RtKind kind, const RtObjectOps *ops) {
    TObj *t = (TObj *)rt_object_new(kind, sizeof *t, ops, NULL, NULL);
    T_TRUE(t != NULL);
    if (t) t->canary = ALIVE;
    return t;
}
static TObj *mk(void) { return mk_ops(RT_K_EVENT, &tops); }
static uint32_t refs_of(RtObject *o) { return __atomic_load_n(&o->refs, __ATOMIC_SEQ_CST); }
static void plant_error(void) { rt_set_last_error(T_CTX(), SENTINEL); }

static RtHandleTable *tab(const char *name, uint32_t cap, uint32_t min_free, uint32_t lo, uint32_t hi) {
    RtHandleTableConfig c = { name, cap, min_free, lo, hi };
    RtHandleTable *t = rt_htab_create(&c);
    T_TRUE(t != NULL);
    return t;
}
static uint64_t V(HANDLE h) { return (uint64_t)(uintptr_t)h; }
static uint64_t HV(uint32_t gen, uint32_t idx) { return ((uint64_t)gen << 22) | ((uint64_t)idx << 2); }

/* New object in table t; returns its handle (NULL if the table is full). */
static HANDLE put(RtHandleTable *t) {
    TObj *o = mk();
    return o ? rt_htab_new(t, &o->obj) : NULL;
}
static bool valid(RtHandleTable *t, HANDLE h) {
    RtObject *o = rt_htab_ref(t, h, RT_K_ANY);
    if (o) rt_object_unref(o);
    return o != NULL;
}

TEST(core, handle_table_config)
{
    RtHandleTableConfig ok = { "t", 4, 0, 1, 511 };
    RtHandleTable *t = rt_htab_create(&ok);
    T_TRUE(t != NULL);
    T_EQ(rt_htab_live(t), 0);
    rt_htab_destroy(t);

    RtHandleTableConfig big = { "big", 1u << 20, 1024, 1, 255 };       /* the kernel table's own shape */
    t = rt_htab_create(&big);
    T_TRUE(t != NULL);
    rt_htab_destroy(t);
    RtHandleTableConfig one_gen = { "one", 1, 0, 511, 511 };           /* a single generation is allowed */
    t = rt_htab_create(&one_gen);
    T_TRUE(t != NULL);
    rt_htab_destroy(t);

    RtHandleTableConfig bad;
    bad = (RtHandleTableConfig){ "t", 0, 0, 1, 511 };
    T_TRAPS(rt_htab_create(&bad), "rt_htab_create(t): capacity 0 is outside 1 to 1048576");
    bad = (RtHandleTableConfig){ "t", (1u << 20) + 1, 0, 1, 511 };
    T_TRAPS(rt_htab_create(&bad), "capacity 1048577 is outside 1 to 1048576");
    bad = (RtHandleTableConfig){ "t", UINT32_MAX, 0, 1, 511 };
    T_TRAPS(rt_htab_create(&bad), "capacity 4294967295 is outside");
    bad = (RtHandleTableConfig){ "t", 4, 0, 0, 511 };                  /* generation 0 would allow the handle 0 */
    T_TRAPS(rt_htab_create(&bad), "generations 0..511 are not within 1..511");
    bad = (RtHandleTableConfig){ "t", 4, 0, 1, 512 };                  /* bit 31 would make it negative */
    T_TRAPS(rt_htab_create(&bad), "generations 1..512 are not within 1..511");
    bad = (RtHandleTableConfig){ "t", 4, 0, 5, 4 };
    T_TRAPS(rt_htab_create(&bad), "generations 5..4 are not within 1..511");
    bad = (RtHandleTableConfig){ "t", 4, 0, 1, UINT32_MAX };
    T_TRAPS(rt_htab_create(&bad), "are not within 1..511");
    bad = (RtHandleTableConfig){ NULL, 4, 0, 1, 511 };
    T_TRAPS(rt_htab_create(&bad), "no configuration or no table name");
    T_TRAPS(rt_htab_create(NULL), "no configuration or no table name");
    T_TRAPS(rt_htab_new(NULL, NULL), "rt_htab_new: NULL table");
    T_TRAPS(rt_htab_ref(NULL, NULL, 0), "rt_htab_ref: NULL table");
    T_TRAPS(rt_htab_close(NULL, NULL, 0), "rt_htab_close: NULL table");
    T_TRAPS(rt_htab_live(NULL), "rt_htab_live: NULL table");
    T_TRAPS(rt_htab_destroy(NULL), "rt_htab_destroy: NULL table");
}

TEST(core, handle_table_capacity)
{
    /* Exactly `capacity` handles fit; one more fails, drops its reference and reports
     * ERROR_NOT_ENOUGH_MEMORY.  Off-by-one on every boundary, with and without a quarantine. */
    uint32_t caps[] = { 1, 2, 3, 7, 8, 9, 63, 64, 65, 1000 };
    uint32_t mins[] = { 0, 3, 100000 };
    for (unsigned c = 0; c < sizeof caps / sizeof *caps; c++) {
        for (unsigned m = 0; m < sizeof mins / sizeof *mins; m++) {
            uint32_t n = caps[c];
            RtHandleTable *t = tab("cap", n, mins[m], 1, 511);
            HANDLE hs[1000];
            for (uint32_t i = 0; i < n; i++) { hs[i] = put(t); T_REQUIRE(hs[i] != NULL); }
            T_REQUIRE(rt_htab_live(t) == n);
            unsigned before = destroyed_count;
            plant_error();
            T_REQUIRE(put(t) == NULL);
            T_REQUIRE(T_LAST_ERROR() == ERROR_NOT_ENOUGH_MEMORY);
            T_REQUIRE(destroyed_count == before + 1);       /* the object given to the full table is dropped */
            T_REQUIRE(rt_htab_live(t) == n);
            /* free the first and last: two places */
            T_REQUIRE(rt_htab_close(t, hs[0], 0) == TRUE);
            if (n > 1) T_REQUIRE(rt_htab_close(t, hs[n - 1], 0) == TRUE);
            HANDLE a = put(t), b = n > 1 ? put(t) : hs[0];
            T_REQUIRE(a != NULL && b != NULL);
            T_REQUIRE(put(t) == NULL);                      /* and exactly two came back */
            T_REQUIRE(a != hs[0] && (n == 1 || (b != hs[n - 1] && a != b)));
            T_REQUIRE(rt_htab_live(t) == n);
            rt_htab_destroy(t);
        }
    }
}

TEST(core, handle_table_reuse_order)
{
    /* Fresh slots are handed out in index order, freed slots come back first-freed first,
     * each with the next generation, and the old handle of a reused slot fails. */
    RtHandleTable *t = tab("reuse", 4, 0, 1, 511);
    HANDLE h[4];
    for (uint32_t i = 0; i < 4; i++) {
        h[i] = put(t);
        T_REQUIRE(V(h[i]) == HV(1, i));
    }
    T_TRUE(put(t) == NULL);
    T_EQ(rt_htab_close(t, h[2], 0), TRUE);
    T_EQ(rt_htab_close(t, h[0], 0), TRUE);
    T_EQ(rt_htab_close(t, h[3], 0), TRUE);
    T_EQ(rt_htab_live(t), 1);
    HANDLE n2 = put(t), n0 = put(t), n3 = put(t);
    T_EQ(V(n2), HV(2, 2));                           /* FIFO: 2, 0, 3 */
    T_EQ(V(n0), HV(2, 0));
    T_EQ(V(n3), HV(2, 3));
    T_TRUE(put(t) == NULL);
    T_TRUE(!valid(t, h[0]) && !valid(t, h[2]) && !valid(t, h[3]));
    T_TRUE(valid(t, h[1]) && valid(t, n0) && valid(t, n2) && valid(t, n3));
    /* second generation of closes */
    T_EQ(rt_htab_close(t, n0, 0), TRUE);
    HANDLE n0b = put(t);
    T_EQ(V(n0b), HV(3, 0));
    T_TRUE(!valid(t, n0) && !valid(t, h[0]) && valid(t, n0b));
    rt_htab_destroy(t);
}

TEST(core, handle_table_generation_wrap)
{
    /* The generation advances at each free, stays within [gen_lo, gen_hi] and then wraps to
     * gen_lo: never 0, never above 511, so a handle is never 0 and never negative. */
    RtHandleTable *t = tab("wrap", 1, 0, 3, 5);
    uint64_t expect[] = { 3, 4, 5, 3, 4, 5, 3 };
    HANDLE first = NULL;
    for (unsigned k = 0; k < sizeof expect / sizeof *expect; k++) {
        HANDLE h = put(t);
        T_REQUIRE(V(h) == HV((uint32_t)expect[k], 0));
        if (k == 0) first = h;
        if (k == 3) T_REQUIRE(h == first);           /* the wrap: after 3 generations the value repeats (the limit) */
        T_REQUIRE(rt_htab_close(t, h, 0) == TRUE);
        T_REQUIRE(!valid(t, h));
    }

    /* the whole range: 1..511 in order, twice and a bit */
    RtHandleTable *u = tab("wrap2", 1, 0, 1, 511);
    HANDLE h0 = NULL;
    bool shape_ok = true, order_ok = true, stale_ok = true;
    for (unsigned k = 0; k < 1200; k++) {
        HANDLE h = put(u);
        uint64_t v = V(h);
        if (v == 0 || (v & 3) || (v >> 31)) shape_ok = false;
        if (v != HV(1 + k % 511, 0)) order_ok = false;
        if (k == 0) h0 = h;
        else if (k < 511) { if (h == h0 || valid(u, h0)) stale_ok = false; }   /* the first handle stays dead ... */
        else if (k == 511) { if (h != h0) stale_ok = false; }                   /* ... until its generation comes round */
        rt_htab_close(u, h, 0);
    }
    T_TRUE(shape_ok);
    T_TRUE(order_ok);
    T_TRUE(stale_ok);

    /* a single generation is allowed: the handle is the same every time */
    RtHandleTable *v1 = tab("one", 1, 0, 511, 511);
    HANDLE a = put(v1);
    T_EQ(V(a), HV(511, 0));
    T_EQ(V(a), 0x7FC00000);
    T_EQ(rt_htab_close(v1, a, 0), TRUE);
    HANDLE b = put(v1);
    T_TRUE(b == a);
    rt_htab_destroy(t);
    rt_htab_destroy(u);
    rt_htab_destroy(v1);
}

TEST(core, handle_table_quarantine)
{
    /* A freed slot is not handed out again until `min_free` slots are waiting (so that the
     * generation of one slot advances slowly), unless the table has no fresh slot left. */
    RtHandleTable *t = tab("q", 16, 4, 1, 511);
    HANDLE a = put(t);
    T_EQ(V(a), HV(1, 0));
    rt_htab_close(t, a, 0);                          /* 1 waiting */
    HANDLE b = put(t);
    T_EQ(V(b), HV(1, 1));                            /* a fresh slot, not the freed one */
    rt_htab_close(t, b, 0);                          /* 2 waiting */
    HANDLE c = put(t);
    T_EQ(V(c), HV(1, 2));
    rt_htab_close(t, c, 0);                          /* 3 waiting */
    HANDLE d = put(t);
    T_EQ(V(d), HV(1, 3));
    rt_htab_close(t, d, 0);                          /* 4 waiting: the quarantine is full */
    HANDLE e = put(t);
    T_EQ(V(e), HV(2, 0));                            /* the oldest freed slot, next generation */
    HANDLE f = put(t);                               /* 3 waiting again: fresh slot */
    T_EQ(V(f), HV(1, 4));
    T_TRUE(!valid(t, a) && !valid(t, b) && !valid(t, c) && !valid(t, d));
    T_TRUE(valid(t, e) && valid(t, f));
    rt_htab_destroy(t);

    /* A table with no fresh slot left uses the waiting ones whatever min_free says. */
    RtHandleTable *u = tab("q2", 3, 1000, 1, 511);
    HANDLE h[3] = { put(u), put(u), put(u) };
    T_EQ(V(h[2]), HV(1, 2));
    rt_htab_close(u, h[1], 0);
    rt_htab_close(u, h[0], 0);
    HANDLE r1 = put(u), r2 = put(u);
    T_EQ(V(r1), HV(2, 1));
    T_EQ(V(r2), HV(2, 0));
    T_TRUE(put(u) == NULL);
    rt_htab_destroy(u);

    /* The kernel table's own quarantine: its first thousand frees do not bring a slot back. */
    TObj *o0 = mk();
    HANDLE first = rt_handle_new(&o0->obj);
    rt_handle_close(first);
    bool reused = false;
    for (int i = 0; i < 1000; i++) {
        TObj *o = mk();
        HANDLE x = rt_handle_new(&o->obj);
        if (((V(x) >> 2) & 0xFFFFF) == ((V(first) >> 2) & 0xFFFFF)) reused = true;
        rt_handle_close(x);
    }
    T_TRUE(!reused);
}

TEST(core, handle_table_spaces_are_disjoint)
{
    /* Two tables with the same indices in use and different generation ranges cannot take
     * each other's handles: every lookup, close and duplicate with a foreign handle fails with
     * ERROR_INVALID_HANDLE and changes nothing. */
    RtHandleTable *k = tab("kernel-like", 8, 0, 1, 255), *w = tab("window-like", 8, 0, 256, 511);
    HANDLE hk[8], hw[8];
    TObj *ok[8], *ow[8];
    for (int i = 0; i < 8; i++) {
        ok[i] = mk(); hk[i] = rt_htab_new(k, &ok[i]->obj);
        ow[i] = mk(); hw[i] = rt_htab_new(w, &ow[i]->obj);
        T_REQUIRE(hk[i] && hw[i]);
        T_REQUIRE(((V(hk[i]) >> 2) & 0xFFFFF) == ((V(hw[i]) >> 2) & 0xFFFFF));      /* same index */
        T_REQUIRE((V(hk[i]) >> 22) <= 255 && (V(hw[i]) >> 22) >= 256 && (V(hw[i]) >> 22) <= 511);
        T_REQUIRE((V(hw[i]) >> 31) == 0);
    }
    for (int i = 0; i < 8; i++) {
        plant_error();
        T_REQUIRE(rt_htab_ref(k, hw[i], 0) == NULL);
        T_REQUIRE(T_LAST_ERROR() == ERROR_INVALID_HANDLE);
        plant_error();
        T_REQUIRE(rt_htab_ref(w, hk[i], 0) == NULL);
        T_REQUIRE(T_LAST_ERROR() == ERROR_INVALID_HANDLE);
        plant_error();
        T_REQUIRE(rt_htab_close(k, hw[i], 0) == FALSE && T_LAST_ERROR() == ERROR_INVALID_HANDLE);
        plant_error();
        T_REQUIRE(rt_htab_close(w, hk[i], RT_K_EVENT) == FALSE && T_LAST_ERROR() == ERROR_INVALID_HANDLE);
        /* nor does the kernel table of the runtime take either */
        plant_error();
        T_REQUIRE(rt_handle_ref(hw[i], RT_K_ANY) == NULL && T_LAST_ERROR() == ERROR_INVALID_HANDLE);
        plant_error();
        T_REQUIRE(rt_handle_close(hw[i]) == FALSE && T_LAST_ERROR() == ERROR_INVALID_HANDLE);
        plant_error();
        T_REQUIRE(rt_handle_dup(hw[i]) == NULL && T_LAST_ERROR() == ERROR_INVALID_HANDLE);
        T_REQUIRE(refs_of(&ok[i]->obj) == 1 && refs_of(&ow[i]->obj) == 1);
    }
    /* The runtime's kernel handles are no good in the window-like table either. */
    TObj *kobj = mk();
    HANDLE hkernel = rt_handle_new(&kobj->obj);
    T_TRUE(rt_htab_ref(w, hkernel, 0) == NULL);
    T_TRUE((V(hkernel) >> 22) >= 1 && (V(hkernel) >> 22) <= 255);
    T_EQ(rt_handle_close(hkernel), TRUE);
    /* Each handle still works in its own table. */
    for (int i = 0; i < 8; i++) T_REQUIRE(valid(k, hk[i]) && valid(w, hw[i]));

    /* One object can be in both tables at once; the slots are independent. */
    TObj *shared = mk();
    rt_object_ref(&shared->obj);
    HANDLE sk = rt_htab_new(k, &shared->obj);        /* (the table slots are full: close two first) */
    T_TRUE(sk == NULL);                              /* k is full: that reference was dropped */
    T_EQ(refs_of(&shared->obj), 1);
    T_EQ(rt_htab_close(k, hk[3], 0), TRUE);
    T_EQ(rt_htab_close(w, hw[3], 0), TRUE);
    rt_object_ref(&shared->obj);
    sk = rt_htab_new(k, &shared->obj);
    rt_object_ref(&shared->obj);
    HANDLE sw = rt_htab_new(w, &shared->obj);
    T_TRUE(sk && sw);
    T_EQ(refs_of(&shared->obj), 3);
    unsigned before = destroyed_count;
    T_EQ(rt_htab_close(k, sk, 0), TRUE);
    T_TRUE(valid(w, sw) && !valid(k, sk));
    T_EQ(rt_htab_close(w, sw, 0), TRUE);
    T_EQ(destroyed_count, before);                   /* our own reference is left */
    rt_object_unref(&shared->obj);
    T_EQ(destroyed_count, before + 1);
    rt_htab_destroy(k);
    rt_htab_destroy(w);
}

TEST(core, handle_table_destroy_and_live)
{
    RtHandleTable *t = tab("live", 100, 0, 1, 511);
    HANDLE hs[100];
    for (int i = 0; i < 100; i++) hs[i] = put(t);
    T_EQ(rt_htab_live(t), 100);
    for (int i = 0; i < 100; i += 2) rt_htab_close(t, hs[i], 0);
    T_EQ(rt_htab_live(t), 50);
    rt_htab_close(t, hs[0], 0);                      /* closing twice changes nothing */
    T_EQ(rt_htab_live(t), 50);
    unsigned before = destroyed_count;
    rt_htab_destroy(t);                              /* drops what is left */
    T_EQ(destroyed_count, before + 50);
}

TEST(core, handle_table_kinds)
{
    /* The generic table filters by kind (0 = any) and honours no_close_handle exactly as the
     * kernel table does. */
    RtHandleTable *t = tab("kinds", 8, 0, 1, 511);
    TObj *e = mk_ops(RT_K_EVENT, &tops), *f = mk_ops(RT_K_FIND, &find_ops);
    HANDLE he = rt_htab_new(t, &e->obj), hf = rt_htab_new(t, &f->obj);
    T_TRUE(rt_htab_ref(t, he, RT_K_EVENT) == &e->obj);
    rt_object_unref(&e->obj);
    plant_error();
    T_TRUE(rt_htab_ref(t, he, RT_K_MUTEX) == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_TRUE(rt_htab_ref(t, hf, RT_K_FIND) == &f->obj);
    rt_object_unref(&f->obj);

    plant_error();
    T_EQ(rt_htab_close(t, hf, 0), FALSE);            /* a plain close refuses a no_close kind */
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(rt_htab_close(t, hf, RT_K_EVENT), FALSE);   /* the wrong kind */
    T_EQ(rt_htab_live(t), 2);
    T_EQ(rt_htab_close(t, hf, RT_K_FIND), TRUE);     /* the kind's own close */
    T_EQ(rt_htab_close(t, he, RT_K_EVENT), TRUE);
    T_EQ(rt_htab_live(t), 0);
    rt_htab_destroy(t);
}

TEST(core, handle_table_largest)
{
    /* The largest table: 2^20 slots, the single largest generation.  One object is put in
     * every slot (a reference per slot), which makes the extreme handles cheap to reach:
     * the first is 511 << 22, the last 0x7FFFFFFC, the biggest value a handle can have. */
    enum { N = 1 << 20 };
    RtHandleTable *t = tab("largest", N, 0, 511, 511);
    TObj *o = mk();
    HANDLE first = NULL, last = NULL;
    bool shape_ok = true;
    uint64_t prev = 0;
    for (uint32_t i = 0; i < N; i++) {
        rt_object_ref(&o->obj);
        HANDLE h = rt_htab_new(t, &o->obj);
        if (!h) { shape_ok = false; break; }
        uint64_t v = V(h);
        if (v == 0 || (v & 3) || (v >> 31) || (i && v <= prev)) shape_ok = false;
        prev = v;
        if (i == 0) first = h;
        last = h;
    }
    T_TRUE(shape_ok);
    T_EQ(V(first), 0x7FC00000);
    T_EQ(V(last), 0x7FFFFFFC);
    T_EQ(V(last), HV(511, N - 1));
    T_EQ(refs_of(&o->obj), N + 1);
    plant_error();
    rt_object_ref(&o->obj);
    T_TRUE(rt_htab_new(t, &o->obj) == NULL);          /* the 2^20 + 1st */
    T_EQ(T_LAST_ERROR(), ERROR_NOT_ENOUGH_MEMORY);
    T_EQ(refs_of(&o->obj), N + 1);
    T_TRUE(rt_htab_ref(t, last, 0) == &o->obj);
    rt_object_unref(&o->obj);
    T_EQ(rt_htab_close(t, last, 0), TRUE);
    T_TRUE(!valid(t, last));
    rt_htab_destroy(t);                               /* drops the other 2^20 - 1 references */
    T_EQ(refs_of(&o->obj), 1);
    unsigned before = destroyed_count;
    rt_object_unref(&o->obj);
    T_EQ(destroyed_count, before + 1);
}
