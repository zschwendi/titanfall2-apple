/* The kernel-object handle table (docs/RUNTIME.md section 4.1): objects and their reference
 * counts, the handle format, lookup, close, duplicate, find-file handles, the table's limit
 * and the loud failures.  Names are in handle_names.c, the pseudo-handles in
 * handle_pseudo.c, threads in handle_threads.c, table instances in handle_table.c.
 *
 * What is behind these: CloseHandle, DuplicateHandle, and the Create and Open functions of
 * every kernel object, which all go through rt_handle_new and rt_handle_ref. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rt/test.h"
#include "rt/handle.h"

typedef struct TObj {
    RtObject obj;
    uint32_t canary;            /* ALIVE while the object lives; destroy clears it */
    uint32_t id;
    uint64_t tail[3];
} TObj;

#define ALIVE 0xA11FE000u

static unsigned  destroyed_count;
static RtObject *last_destroyed;
static void    (*in_destroy)(RtObject *);     /* a test's extra action inside destroy */

static void tobj_destroy(RtObject *o) {
    TObj *t = (TObj *)o;
    T_TRUE(t->canary == ALIVE);                  /* destroyed exactly once, and not before it was built */
    t->canary = 0;
    last_destroyed = o;
    destroyed_count++;
    if (in_destroy) in_destroy(o);
}

static const RtObjectOps tops = { .name = "test", .destroy = tobj_destroy };
static const RtObjectOps find_ops = { .name = "test-find", .destroy = tobj_destroy, .no_close_handle = true };

static TObj *mk_ops(RtKind kind, const RtObjectOps *ops) {
    TObj *t = (TObj *)rt_object_new(kind, sizeof *t, ops, NULL, NULL);
    T_TRUE(t != NULL);
    if (t) t->canary = ALIVE;
    return t;
}
static TObj *mk(RtKind kind) { return mk_ops(kind, &tops); }

static uint32_t refs_of(RtObject *o) { return __atomic_load_n(&o->refs, __ATOMIC_SEQ_CST); }

static HANDLE hnew(TObj *t) {
    HANDLE h = rt_handle_new(&t->obj);
    T_TRUE(h != NULL);
    return h;
}

static HANDLE with_garbage(HANDLE h, uint64_t low2, uint64_t high32) {
    return (HANDLE)(uintptr_t)(((uint64_t)(uintptr_t)h & ~UINT64_C(0xFFFFFFFF00000003)) | low2 | (high32 << 32));
}

#define SENTINEL 0x5EA1u
static void plant_error(void) { rt_set_last_error(T_CTX(), SENTINEL); }

/* ---- objects ------------------------------------------------------------------------------- */

TEST(core, handle_object_new)
{
    /* A new object is zeroed, has one reference, its kind and ops, no name, and is aligned. */
    bool existed = true;
    TObj *o = (TObj *)rt_object_new(RT_K_EVENT, sizeof(TObj), &tops, NULL, &existed);
    T_TRUE(o != NULL);
    if (!o) return;
    T_EQ(existed, 0);                               /* an unnamed object never "existed" */
    T_EQ(o->obj.kind, RT_K_EVENT);
    T_EQ(refs_of(&o->obj), 1);
    T_TRUE(o->obj.ops == &tops);
    T_TRUE(o->obj.name == NULL);
    T_TRUE(o->obj.waiters == NULL);
    T_EQ(o->canary, 0);
    T_EQ(o->id, 0);
    T_EQ(o->tail[0] | o->tail[1] | o->tail[2], 0);
    T_EQ((uintptr_t)o & 15, 0);
    o->canary = ALIVE;
    memset((char *)o + sizeof(RtObject), 0xAB, sizeof *o - sizeof(RtObject));   /* the whole size is ours to write */
    o->canary = ALIVE;
    unsigned before = destroyed_count;
    rt_object_unref(&o->obj);
    T_EQ(destroyed_count, before + 1);
    T_TRUE(last_destroyed == &o->obj);

    /* existed may be NULL for an unnamed object, and is left false otherwise. */
    TObj *p = mk(RT_K_MUTEX);
    rt_object_unref(&p->obj);

    /* Every kind can be made; the kind is stored as given. */
    for (unsigned k = 1; k < RT_K_COUNT; k++) {
        TObj *t = mk((RtKind)k);
        if (!t) return;
        T_EQ(t->obj.kind, k);
        rt_object_unref(&t->obj);
    }

    /* Exact fit: the smallest object is the header alone; a large one is zeroed throughout. */
    RtObject *bare = rt_object_new(RT_K_SECTION, sizeof(RtObject), &(RtObjectOps){ .name = "bare" }, NULL, NULL);
    T_TRUE(bare != NULL);
    if (bare) rt_object_unref(bare);                /* destroy is NULL: allowed */
    size_t big = 1 << 20;
    unsigned char *b = (unsigned char *)rt_object_new(RT_K_FILE, big, &(RtObjectOps){ .name = "big" }, NULL, NULL);
    T_TRUE(b != NULL);
    if (b) {
        size_t nonzero = 0;
        for (size_t i = sizeof(RtObject); i < big; i++) nonzero |= b[i];
        T_EQ(nonzero, 0);
        b[big - 1] = 1;                             /* last byte writable */
        rt_object_unref((RtObject *)b);
    }

    /* An allocation that cannot succeed is an error return, not a crash. */
    plant_error();
    RtObject *huge = rt_object_new(RT_K_FILE, SIZE_MAX - 8, &tops, NULL, NULL);
    T_TRUE(huge == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_NOT_ENOUGH_MEMORY);
    plant_error();
    huge = rt_object_new(RT_K_FILE, SIZE_MAX / 2, &tops, NULL, NULL);
    T_TRUE(huge == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_NOT_ENOUGH_MEMORY);
}

TEST(core, handle_object_new_loud)
{
    /* Misuse of rt_object_new ends in a report that names the value. */
    T_TRAPS(rt_object_new(RT_K_ANY, sizeof(TObj), &tops, NULL, NULL), "rt_object_new: 0 is not an object kind");
    T_TRAPS(rt_object_new((RtKind)RT_K_COUNT, sizeof(TObj), &tops, NULL, NULL),
            "rt_object_new: 17 is not an object kind");
    T_TRAPS(rt_object_new((RtKind)99, sizeof(TObj), &tops, NULL, NULL), "rt_object_new: 99 is not an object kind");
    T_TRAPS(rt_object_new((RtKind)-1, sizeof(TObj), &tops, NULL, NULL), "is not an object kind");
    /* The header's size is the structure's (it grew by 16 bytes with the wait report's three
     * fields); the message must name it and the size that was asked for. */
    char too_small[96];
    snprintf(too_small, sizeof too_small, "size %zu is smaller than the RtObject header (%zu bytes)", sizeof(RtObject) - 1, sizeof(RtObject));
    T_TRAPS(rt_object_new(RT_K_EVENT, sizeof(RtObject) - 1, &tops, NULL, NULL), too_small);
    T_TRAPS(rt_object_new(RT_K_EVENT, 0, &tops, NULL, NULL), "size 0 is smaller than the RtObject header");
    T_TRAPS(rt_object_new(RT_K_EVENT, sizeof(TObj), NULL, NULL, NULL), "no ops table");
    T_TRAPS(rt_object_new(RT_K_EVENT, sizeof(TObj), &(RtObjectOps){ .destroy = tobj_destroy }, NULL, NULL),
            "the ops table has no name");
    /* A name needs `existed`, or the caller cannot report ERROR_ALREADY_EXISTS. */
    T_TRAPS(rt_object_new(RT_K_EVENT, sizeof(TObj), &tops, u"Name", NULL), "a named object needs `existed`");
    /* Kinds that have no name in the kernel object name space. */
    bool ex;
    T_TRAPS(rt_object_new(RT_K_FILE, sizeof(TObj), &tops, u"Name", &ex), "kind RT_K_FILE");
    T_TRAPS(rt_object_new(RT_K_THREAD, sizeof(TObj), &tops, u"Name", &ex), "kind RT_K_THREAD");
    T_TRAPS(rt_object_new(RT_K_PROCESS, sizeof(TObj), &tops, u"Name", &ex), "kind RT_K_PROCESS");
    T_TRAPS(rt_object_new(RT_K_FIND, sizeof(TObj), &tops, u"Name", &ex), "kind RT_K_FIND");
    T_TRAPS(rt_object_new(RT_K_PIPE, sizeof(TObj), &tops, u"Name", &ex), "kind RT_K_PIPE");
    T_TRAPS(rt_object_new(RT_K_SOCKET, sizeof(TObj), &tops, u"Name", &ex), "kind RT_K_SOCKET");
    T_TRAPS(rt_object_new(RT_K_REGKEY, sizeof(TObj), &tops, u"Name", &ex), "kind RT_K_REGKEY");
    /* The table cannot be started twice. */
    T_TRAPS(rt_handle_init(), "rt_handle_init called twice");
}

TEST(core, handle_object_refs)
{
    TObj *o = mk(RT_K_SEMAPHORE);
    if (!o) return;
    unsigned before = destroyed_count;
    for (unsigned i = 1; i <= 10; i++) {
        rt_object_ref(&o->obj);
        T_REQUIRE(refs_of(&o->obj) == 1 + i);
    }
    for (unsigned i = 10; i >= 1; i--) {
        rt_object_unref(&o->obj);
        T_REQUIRE(refs_of(&o->obj) == i);
        T_REQUIRE(destroyed_count == before);       /* alive while any reference remains */
    }
    rt_object_unref(&o->obj);                       /* the last one */
    T_EQ(destroyed_count, before + 1);
    T_TRUE(last_destroyed == &o->obj);

    /* destroy runs once per object even with many objects, and sees its own object. */
    TObj *a[100];
    before = destroyed_count;
    for (int i = 0; i < 100; i++) { a[i] = mk(RT_K_EVENT); a[i]->id = (uint32_t)i + 1; }
    for (int i = 0; i < 100; i++) { rt_object_unref(&a[i]->obj); }
    T_EQ(destroyed_count, before + 100);

    /* A pointer that is not an object is refused, not trusted. */
    struct { uint64_t pad[2]; RtObject obj; } fake;
    memset(&fake, 0, sizeof fake);
    T_TRAPS(rt_object_ref(&fake.obj), "is not a live object made by rt_object_new");
    T_TRAPS(rt_object_unref(&fake.obj), "is not a live object made by rt_object_new");
    T_TRAPS(rt_object_ref(NULL), "rt_object_ref: NULL object");
    T_TRAPS(rt_object_unref(NULL), "rt_object_unref: NULL object");
    T_TRAPS(rt_handle_new(NULL), "NULL object");
    T_TRAPS(rt_handle_new(&fake.obj), "is not a live object made by rt_object_new");
}

static HANDLE chain_hb, chain_hc;
static RtObject *chain_a, *chain_b;
static unsigned order[3], norder;

static void chain_closer(RtObject *obj) {
    order[norder++] = ((TObj *)obj)->id;
    if (obj == chain_a) T_EQ(rt_handle_close(chain_hb), TRUE);
    else if (obj == chain_b) T_EQ(rt_handle_close(chain_hc), TRUE);
}

TEST(core, handle_destroy_rules)
{
    /* A thread still queued on an object that dies is a bug in the wait core: loud. */
    TObj *o = mk(RT_K_EVENT);
    if (!o) return;
    o->obj.waiters = (void *)0x1234;
    T_TRAPS(rt_object_unref(&o->obj), "reached zero references while threads are still queued on it");
    o->obj.waiters = NULL;
    rt_object_unref(&o->obj);

    /* destroy runs with no lock held and may use the table: it closes another handle, and
     * the object it closes is destroyed in turn (a chain of three). */
    TObj *c = mk(RT_K_EVENT), *b = mk(RT_K_EVENT), *a = mk(RT_K_EVENT);
    a->id = 1; b->id = 2; c->id = 3;
    chain_hb = hnew(b);
    chain_hc = hnew(c);
    HANDLE ha = hnew(a);
    chain_a = &a->obj; chain_b = &b->obj;
    norder = 0;
    in_destroy = chain_closer;
    unsigned before = destroyed_count;
    T_EQ(rt_handle_close(ha), TRUE);
    in_destroy = NULL;
    T_EQ(destroyed_count, before + 3);
    T_EQ(norder, 3);
    T_EQ(order[0], 1);                                  /* each is destroyed inside the previous one's destroy */
    T_EQ(order[1], 2);
    T_EQ(order[2], 3);
    T_TRUE(rt_handle_ref(ha, RT_K_ANY) == NULL && rt_handle_ref(chain_hb, RT_K_ANY) == NULL &&
           rt_handle_ref(chain_hc, RT_K_ANY) == NULL);
}

/* ---- the handle format ----------------------------------------------------------------------- */

TEST(core, handle_format)
{
    /* Multiples of 4, never 0, never negative as 32 bits, nothing above 32 bits, all different. */
    enum { N = 3000 };
    static HANDLE hs[N];
    static TObj *os[N];
    bool format_ok = true, distinct = true, small = false;
    for (int i = 0; i < N; i++) {
        os[i] = mk(RT_K_EVENT);
        hs[i] = hnew(os[i]);
        uint64_t v = (uint64_t)(uintptr_t)hs[i];
        if (v == 0 || (v & 3) || (v >> 31)) format_ok = false;
        if (v < 0x1000) small = true;               /* small integers never name a handle */
    }
    T_TRUE(format_ok);
    T_TRUE(!small);
    for (int i = 0; i < N && distinct; i++)
        for (int j = i + 1; j < N && j < i + 40; j++) if (hs[i] == hs[j]) distinct = false;
    T_TRUE(distinct);
    /* a sorted check of all pairs */
    uint64_t *sorted = malloc(sizeof(uint64_t) * N);
    for (int i = 0; i < N; i++) sorted[i] = (uint64_t)(uintptr_t)hs[i];
    for (int i = 1; i < N; i++) {                   /* insertion sort: N is small */
        uint64_t x = sorted[i]; int j = i - 1;
        while (j >= 0 && sorted[j] > x) { sorted[j + 1] = sorted[j]; j--; }
        sorted[j + 1] = x;
    }
    bool all_different = true;
    for (int i = 1; i < N; i++) if (sorted[i] == sorted[i - 1]) all_different = false;
    T_TRUE(all_different);
    free(sorted);
    for (int i = 0; i < N; i++) T_REQUIRE(rt_handle_close(hs[i]) == TRUE);
}

TEST(core, handle_lookup_bits)
{
    /* The low two bits are ignored and only the low 32 bits count (RUNTIME.md 4.1). */
    TObj *o = mk(RT_K_EVENT);
    HANDLE h = hnew(o);
    for (uint64_t low = 0; low < 4; low++) {
        RtObject *r = rt_handle_ref(with_garbage(h, low, 0), RT_K_EVENT);
        T_TRUE(r == &o->obj);
        if (r) rt_object_unref(r);
    }
    uint64_t highs[] = { 1, 0x7FFF, 0xFFFFFFFF, 0xDEADBEEF, 0x80000000 };
    for (unsigned i = 0; i < sizeof highs / sizeof *highs; i++) {
        RtObject *r = rt_handle_ref(with_garbage(h, i & 3, highs[i]), RT_K_EVENT);
        T_TRUE(r == &o->obj);
        if (r) rt_object_unref(r);
    }
    /* Neither changes what the handle IS: a different slot or generation still fails. */
    uint64_t v = (uint64_t)(uintptr_t)h;
    T_TRUE(rt_handle_ref((HANDLE)(uintptr_t)(v + 4), RT_K_ANY) == NULL);                /* next slot: free */
    T_TRUE(rt_handle_ref((HANDLE)(uintptr_t)(v + (UINT64_C(1) << 22)), RT_K_ANY) == NULL); /* next generation */
    T_TRUE(rt_handle_ref((HANDLE)(uintptr_t)(v ^ (UINT64_C(1) << 30)), RT_K_ANY) == NULL);
    /* close uses the same decoding */
    T_EQ(rt_handle_close(with_garbage(h, 3, 0xABCD)), TRUE);
    T_TRUE(rt_handle_ref(h, RT_K_ANY) == NULL);
}

/* ---- lookup ------------------------------------------------------------------------------------ */

TEST(core, handle_ref)
{
    TObj *o = mk(RT_K_MUTEX);
    HANDLE h = hnew(o);
    T_EQ(refs_of(&o->obj), 1);                      /* the table's own */

    /* A lookup adds one reference, which the caller drops. */
    plant_error();
    RtObject *r = rt_handle_ref(h, RT_K_MUTEX);
    T_TRUE(r == &o->obj);
    T_EQ(refs_of(&o->obj), 2);
    T_EQ(T_LAST_ERROR(), SENTINEL);                 /* success leaves last error alone */
    rt_object_unref(r);
    T_EQ(refs_of(&o->obj), 1);
    r = rt_handle_ref(h, RT_K_ANY);
    T_TRUE(r == &o->obj);
    rt_object_unref(r);

    /* Every kind accepts exactly itself and RT_K_ANY. */
    for (unsigned k = 1; k < RT_K_COUNT; k++) {
        TObj *t = mk((RtKind)k);
        HANDLE ht = hnew(t);
        for (unsigned want = 0; want < RT_K_COUNT; want++) {
            plant_error();
            RtObject *got = rt_handle_ref(ht, (RtKind)want);
            if (want == 0 || want == k) {
                T_REQUIRE(got == &t->obj);
                T_REQUIRE(T_LAST_ERROR() == SENTINEL);
                rt_object_unref(got);
            } else {
                T_REQUIRE(got == NULL);
                T_REQUIRE(T_LAST_ERROR() == ERROR_INVALID_HANDLE);
            }
        }
        T_REQUIRE(rt_handle_close(ht) == TRUE);
    }
    T_EQ(refs_of(&o->obj), 1);
    T_TRAPS(rt_handle_ref(h, (RtKind)RT_K_COUNT), "rt_handle_ref: 17 is not an object kind");
    T_TRAPS(rt_handle_ref(h, (RtKind)-1), "is not an object kind");
    T_EQ(rt_handle_close(h), TRUE);
}

TEST(core, handle_ref_failures)
{
    /* Every way a handle can be invalid fails with NULL and ERROR_INVALID_HANDLE, and does
     * not disturb the table. */
    TObj *live = mk(RT_K_EVENT);
    HANDLE hlive = hnew(live);
    TObj *gone = mk(RT_K_EVENT);
    HANDLE hgone = hnew(gone);
    T_EQ(rt_handle_close(hgone), TRUE);

    uint64_t lv = (uint64_t)(uintptr_t)hlive;
    uint64_t bad[] = {
        0,                                  /* NULL */
        1, 2, 3, 4, 8, 0x10, 0x100, 0x1000, /* small integers: the generation is 0 */
        0xFFFF, 0x3FFFFC,                   /* still generation 0 */
        (uint64_t)(uintptr_t)hgone,         /* closed */
        lv + 4,                             /* a free slot of the same generation */
        lv + (UINT64_C(1) << 22),           /* wrong generation */
        lv - (UINT64_C(1) << 22),           /* generation 0 */
        (lv & 0x3FFFFC) | (UINT64_C(300) << 22),    /* generation of another table */
        (lv & 0x3FFFFC) | (UINT64_C(511) << 22),
        (lv & 0x3FFFFC) | (UINT64_C(512) << 22),    /* bit 31 set: "negative" */
        (lv & 0x3FFFFC) | (UINT64_C(1023) << 22),
        0x7FFFFFFC, 0x80000000, 0xFFFFFFFC, 0xFFFFFFFF, 0xFFFFFFFE, /* zero-extended: not pseudo-handles */
        0x00000001FFFFFFFC, 0xFFFFFFFF80000000, 0xFFFFFFFF80000004,
        UINT64_MAX - 2, UINT64_MAX - 3, UINT64_MAX - 7,             /* near the pseudo-handles but not them */
        UINT64_C(1) << 63, (UINT64_C(1) << 22) - 4,
        ((uint64_t)1 << 22) | ((uint64_t)0xFFFFF << 2),             /* the last index: free */
    };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        HANDLE h = (HANDLE)(uintptr_t)bad[i];
        if (bad[i] == lv) continue;
        plant_error();
        RtObject *r = rt_handle_ref(h, RT_K_ANY);
        if (r) { rt_object_unref(r); }
        T_REQUIRE(r == NULL);
        T_REQUIRE(T_LAST_ERROR() == ERROR_INVALID_HANDLE);
        plant_error();
        T_REQUIRE(rt_handle_ref(h, RT_K_EVENT) == NULL);
        T_REQUIRE(T_LAST_ERROR() == ERROR_INVALID_HANDLE);
        plant_error();
        T_REQUIRE(rt_handle_close(h) == FALSE);
        T_REQUIRE(T_LAST_ERROR() == ERROR_INVALID_HANDLE);
        plant_error();
        T_REQUIRE(rt_handle_dup(h) == NULL);
        T_REQUIRE(T_LAST_ERROR() == ERROR_INVALID_HANDLE);
        plant_error();
        T_REQUIRE(rt_handle_close_kind(h, RT_K_EVENT) == FALSE);
        T_REQUIRE(T_LAST_ERROR() == ERROR_INVALID_HANDLE);
    }
    T_EQ(refs_of(&live->obj), 1);
    T_EQ(rt_handle_close(hlive), TRUE);
    T_TRUE(T_LOGGED(RT_CH_FILE, "stale generation") || T_LOGGED(RT_CH_FILE, "free slot"));
}

TEST(core, handle_new_failure_reference)
{
    /* rt_handle_new consumes the caller's reference: the object lives in the table only. */
    TObj *o = mk(RT_K_EVENT);
    unsigned before = destroyed_count;
    HANDLE h = hnew(o);
    T_EQ(destroyed_count, before);
    T_EQ(refs_of(&o->obj), 1);
    T_EQ(rt_handle_close(h), TRUE);
    T_EQ(destroyed_count, before + 1);
}

/* ---- close ------------------------------------------------------------------------------------ */

TEST(core, handle_close)
{
    TObj *o = mk(RT_K_EVENT);
    HANDLE h = hnew(o);
    unsigned before = destroyed_count;

    plant_error();
    T_EQ(rt_handle_close(h), TRUE);
    T_EQ(T_LAST_ERROR(), SENTINEL);                 /* success leaves last error alone */
    T_EQ(destroyed_count, before + 1);              /* the table held the only reference */
    T_TRUE(last_destroyed == &o->obj);

    /* Closing again fails, and keeps failing. */
    for (int i = 0; i < 3; i++) {
        plant_error();
        T_EQ(rt_handle_close(h), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    }
    T_EQ(destroyed_count, before + 1);
    plant_error();
    T_EQ(rt_handle_close(NULL), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    /* A reference taken before the close keeps the object alive; the last unref kills it. */
    TObj *p = mk(RT_K_EVENT);
    HANDLE hp = hnew(p);
    RtObject *held = rt_handle_ref(hp, RT_K_EVENT);
    before = destroyed_count;
    T_EQ(rt_handle_close(hp), TRUE);
    T_EQ(destroyed_count, before);
    T_EQ(refs_of(held), 1);
    T_EQ(p->canary, ALIVE);
    T_TRUE(rt_handle_ref(hp, RT_K_ANY) == NULL);    /* no new references through a closed handle */
    rt_object_unref(held);
    T_EQ(destroyed_count, before + 1);

    /* A closed handle is not found by a later object that took its slot. */
    TObj *q = mk(RT_K_EVENT);
    HANDLE hq = hnew(q);
    T_TRUE(hq != hp);
    T_TRUE(rt_handle_ref(hp, RT_K_ANY) == NULL);
    T_EQ(rt_handle_close(hq), TRUE);
}

TEST(core, handle_stale_handles)
{
    /* Create and close thousands of objects, keeping only the handles: not one of them is
     * valid afterwards, none equals another, and a live object is never reachable through
     * an old handle. */
    enum { N = 6000 };
    static uint64_t seen[N];
    TObj *keep = mk(RT_K_MUTEX);
    HANDLE hkeep = hnew(keep);
    for (int i = 0; i < N; i++) {
        TObj *o = mk(RT_K_EVENT);
        HANDLE h = hnew(o);
        seen[i] = (uint64_t)(uintptr_t)h;
        T_REQUIRE(rt_handle_close(h) == TRUE);
        T_REQUIRE(seen[i] != (uint64_t)(uintptr_t)hkeep);
    }
    unsigned valid = 0;
    for (int i = 0; i < N; i++) {
        RtObject *r = rt_handle_ref((HANDLE)(uintptr_t)seen[i], RT_K_ANY);
        if (r) { valid++; rt_object_unref(r); }
    }
    T_EQ(valid, 0);
    /* all different (sort) */
    for (int i = 1; i < N; i++) {
        uint64_t x = seen[i]; int j = i - 1;
        while (j >= 0 && seen[j] > x) { seen[j + 1] = seen[j]; j--; }
        seen[j + 1] = x;
    }
    unsigned dups = 0;
    for (int i = 1; i < N; i++) if (seen[i] == seen[i - 1]) dups++;
    T_EQ(dups, 0);
    T_EQ(rt_handle_close(hkeep), TRUE);
}

TEST(core, handle_close_kind)
{
    /* A find-file handle: CloseHandle fails and is logged, the handle stays good, and the
     * kind's own close function closes it. */
    TObj *f = mk_ops(RT_K_FIND, &find_ops);
    HANDLE hf = hnew(f);
    unsigned before = destroyed_count;
    plant_error();
    T_EQ(rt_handle_close(hf), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_TRUE(T_LOGGED(RT_CH_FILE, "on a test-find handle fails"));
    T_TRUE(T_LOGGED(RT_CH_FILE, "FindClose"));
    T_EQ(destroyed_count, before);
    RtObject *r = rt_handle_ref(hf, RT_K_FIND);
    T_TRUE(r == &f->obj);                           /* still open */
    rt_object_unref(r);
    plant_error();
    T_EQ(rt_handle_close(hf), FALSE);               /* and again */
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    /* The wrong kind does not close it. */
    for (unsigned k = 1; k < RT_K_COUNT; k++) {
        if (k == RT_K_FIND) continue;
        plant_error();
        T_REQUIRE(rt_handle_close_kind(hf, (RtKind)k) == FALSE);
        T_REQUIRE(T_LAST_ERROR() == ERROR_INVALID_HANDLE);
    }
    T_EQ(destroyed_count, before);
    r = rt_handle_ref(hf, RT_K_FIND);
    T_TRUE(r == &f->obj);
    rt_object_unref(r);

    plant_error();
    T_EQ(rt_handle_close_kind(hf, RT_K_FIND), TRUE);
    T_EQ(T_LAST_ERROR(), SENTINEL);
    T_EQ(destroyed_count, before + 1);
    plant_error();
    T_EQ(rt_handle_close_kind(hf, RT_K_FIND), FALSE);   /* closed */
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    /* rt_handle_close_kind on an ordinary kind: it closes when the kind is exactly right. */
    TObj *e = mk(RT_K_EVENT);
    HANDLE he = hnew(e);
    plant_error();
    T_EQ(rt_handle_close_kind(he, RT_K_MUTEX), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_TRUE(rt_handle_ref(he, RT_K_EVENT) == &e->obj);
    rt_object_unref(&e->obj);
    T_EQ(rt_handle_close_kind(he, RT_K_EVENT), TRUE);
    T_EQ(e->canary, 0);

    /* "No close" belongs to the object's ops, not to the kind number. */
    TObj *g = mk_ops(RT_K_FIND, &tops);
    HANDLE hg = hnew(g);
    T_EQ(rt_handle_close(hg), TRUE);
    T_EQ(g->canary, 0);

    /* RT_K_ANY is not a kind to close by. */
    TObj *h2 = mk(RT_K_EVENT);
    HANDLE hh = hnew(h2);
    T_TRAPS(rt_handle_close_kind(hh, RT_K_ANY), "rt_handle_close_kind: 0 is not an object kind");
    T_TRAPS(rt_handle_close_kind(hh, (RtKind)RT_K_COUNT), "is not an object kind");
    T_EQ(rt_handle_close(hh), TRUE);
}

/* ---- duplicate ---------------------------------------------------------------------------------- */

TEST(core, handle_dup)
{
    TObj *o = mk(RT_K_SEMAPHORE);
    HANDLE h = hnew(o);
    unsigned before = destroyed_count;

    plant_error();
    HANDLE d = rt_handle_dup(h);
    T_TRUE(d != NULL);
    T_EQ(T_LAST_ERROR(), SENTINEL);                 /* success leaves last error alone */
    T_TRUE(d != h);
    T_EQ(refs_of(&o->obj), 2);                      /* one reference per slot */
    uint64_t v = (uint64_t)(uintptr_t)d;
    T_TRUE(v != 0 && (v & 3) == 0 && (v >> 31) == 0);
    RtObject *r = rt_handle_ref(d, RT_K_SEMAPHORE);
    T_TRUE(r == &o->obj);                           /* same object, same kind */
    rt_object_unref(r);

    /* Each handle closes independently; the object dies with the last. */
    T_EQ(rt_handle_close(h), TRUE);
    T_EQ(destroyed_count, before);
    T_TRUE(rt_handle_ref(h, RT_K_ANY) == NULL);
    r = rt_handle_ref(d, RT_K_ANY);
    T_TRUE(r == &o->obj);
    rt_object_unref(r);
    HANDLE d2 = rt_handle_dup(d);                   /* a duplicate of a duplicate */
    T_TRUE(d2 != NULL && d2 != d);
    T_EQ(rt_handle_close(d), TRUE);
    T_EQ(destroyed_count, before);
    T_EQ(rt_handle_close(d2), TRUE);
    T_EQ(destroyed_count, before + 1);

    /* Failure: nothing to duplicate. */
    plant_error();
    T_TRUE(rt_handle_dup(h) == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    plant_error();
    T_TRUE(rt_handle_dup(NULL) == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    /* Many duplicates all name the one object; none is lost when the others close. */
    TObj *m = mk(RT_K_EVENT);
    HANDLE base = hnew(m);
    HANDLE ds[50];
    for (int i = 0; i < 50; i++) { ds[i] = rt_handle_dup(base); T_REQUIRE(ds[i] != NULL); }
    T_EQ(refs_of(&m->obj), 51);
    before = destroyed_count;
    T_EQ(rt_handle_close(base), TRUE);
    for (int i = 0; i < 50; i++) {
        T_REQUIRE(destroyed_count == before);
        T_REQUIRE(rt_handle_close(ds[i]) == TRUE);
    }
    T_EQ(destroyed_count, before + 1);
}

TEST(core, handle_dup_find)
{
    /* A find-file handle is not a kernel handle: it cannot be duplicated either. */
    TObj *f = mk_ops(RT_K_FIND, &find_ops);
    HANDLE hf = hnew(f);
    plant_error();
    T_TRUE(rt_handle_dup(hf) == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(refs_of(&f->obj), 1);                      /* the reference taken for the attempt was dropped */
    T_TRUE(T_LOGGED(RT_CH_FILE, "DuplicateHandle"));
    T_EQ(rt_handle_close_kind(hf, RT_K_FIND), TRUE);
}

/* ---- the limit ---------------------------------------------------------------------------------- */

TEST(core, handle_table_full)
{
    /* The table has 2^20 slots (20 index bits).  The one after the last fails with
     * ERROR_NOT_ENOUGH_MEMORY and drops the reference it was given. */
    enum { CAP = 1 << 20 };
    HANDLE *hs = malloc(sizeof(HANDLE) * CAP);
    uint8_t *bits = calloc(CAP / 8, 1);
    if (!hs || !bits) { T_TRUE(false); return; }
    /* The kernel table is not empty when a test begins: rt_file_init put the three standard
     * handles in it (core/file.c).  Give their slots back, so that all 2^20 are free here. */
    HANDLE rt_file_std_slot(unsigned i);
    for (unsigned i = 0; i < 3; i++) T_EQ(rt_handle_close(rt_file_std_slot(i)), TRUE);
    unsigned made = 0;
    bool format_ok = true, distinct = true, failed = false;
    for (unsigned n = 0; n <= CAP; n++) {
        TObj *o = (TObj *)rt_object_new(RT_K_EVENT, sizeof *o, &tops, NULL, NULL);
        if (!o) { T_TRUE(false); return; }
        o->canary = ALIVE;
        plant_error();
        HANDLE h = rt_handle_new(&o->obj);
        if (!h) {
            T_EQ(T_LAST_ERROR(), ERROR_NOT_ENOUGH_MEMORY);
            failed = true;
            break;
        }
        uint64_t v = (uint64_t)(uintptr_t)h;
        if (v == 0 || (v & 3) || (v >> 31)) format_ok = false;
        uint32_t idx = (uint32_t)(v >> 2) & 0xFFFFF;
        if (bits[idx >> 3] & (1u << (idx & 7))) distinct = false;
        bits[idx >> 3] |= (uint8_t)(1u << (idx & 7));
        hs[made++] = h;
    }
    T_TRUE(failed);
    T_EQ(made, CAP);
    T_TRUE(format_ok);
    T_TRUE(distinct);
    T_EQ(destroyed_count, 1);                           /* the object of the failed rt_handle_new is gone */
    T_TRUE(T_LOGGED(RT_CH_FILE, "kernel handle table is full (1048576 handles)"));

    /* Closing one makes room for exactly one. */
    T_EQ(rt_handle_close(hs[12345]), TRUE);
    T_EQ(destroyed_count, 2);
    TObj *o = mk(RT_K_EVENT);
    HANDLE h = hnew(o);
    TObj *o2 = mk(RT_K_EVENT);
    plant_error();
    T_TRUE(rt_handle_new(&o2->obj) == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_NOT_ENOUGH_MEMORY);
    T_EQ(destroyed_count, 3);

    /* Duplicating into a full table fails the same way and drops the reference it took. */
    RtObject *first = rt_handle_ref(hs[0], RT_K_EVENT);
    T_TRUE(first != NULL);
    T_EQ(refs_of(first), 2);                            /* the table's and ours */
    plant_error();
    T_TRUE(rt_handle_dup(hs[0]) == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_NOT_ENOUGH_MEMORY);
    T_EQ(refs_of(first), 2);
    rt_object_unref(first);

    /* Close everything; the table is empty again and reusable. */
    unsigned closed = 0;
    for (unsigned i = 0; i < CAP; i++)
        if (i != 12345 && rt_handle_close(hs[i])) closed++;
    T_EQ(closed, CAP - 1);
    T_EQ(rt_handle_close(h), TRUE);
    T_EQ(destroyed_count, 3 + CAP);
    TObj *again = mk(RT_K_EVENT);
    HANDLE ha = hnew(again);
    T_EQ(rt_handle_close(ha), TRUE);
    free(hs);
    free(bits);
}

TEST(core, handle_kind_matrix)
{
    /* Every kind of object against every kind named by the caller: rt_handle_close_kind closes
     * exactly when the two are equal and leaves the handle open otherwise; rt_handle_close closes
     * every kind whose ops allow it; rt_handle_dup works for each of them. */
    for (unsigned have = 1; have < RT_K_COUNT; have++) {
        for (unsigned want = 1; want < RT_K_COUNT; want++) {
            TObj *o = mk((RtKind)have);
            HANDLE h = hnew(o);
            unsigned before = destroyed_count;
            plant_error();
            if (have == want) {
                T_REQUIRE(rt_handle_close_kind(h, (RtKind)want) == TRUE);
                T_REQUIRE(T_LAST_ERROR() == SENTINEL);
                T_REQUIRE(destroyed_count == before + 1);
                T_REQUIRE(rt_handle_ref(h, RT_K_ANY) == NULL);
            } else {
                T_REQUIRE(rt_handle_close_kind(h, (RtKind)want) == FALSE);
                T_REQUIRE(T_LAST_ERROR() == ERROR_INVALID_HANDLE);
                T_REQUIRE(destroyed_count == before);
                RtObject *r = rt_handle_ref(h, (RtKind)have);
                T_REQUIRE(r == &o->obj);                    /* still open, still ours */
                rt_object_unref(r);
                T_REQUIRE(rt_handle_close(h) == TRUE);
                T_REQUIRE(destroyed_count == before + 1);
            }
        }
        TObj *o = mk((RtKind)have);
        HANDLE h = hnew(o), d = rt_handle_dup(h);
        T_REQUIRE(d != NULL && d != h);
        T_REQUIRE(rt_handle_ref(d, (RtKind)have) == &o->obj);
        rt_object_unref(&o->obj);
        T_REQUIRE(rt_handle_close(h) == TRUE && rt_handle_close(d) == TRUE);
    }
}
