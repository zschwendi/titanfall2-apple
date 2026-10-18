/* The kernel object name space (RUNTIME.md 4.1): one space shared by events, mutexes,
 * semaphores and file-mapping objects, local to the process.
 *
 * Rules under test, and where each comes from:
 *   - names are compared case-sensitively ("Name comparison is case sensitive" in the pages of
 *     CreateEvent, CreateMutex, CreateSemaphore, CreateFileMapping and the Open functions);
 *   - a name that exists with another kind fails with ERROR_INVALID_HANDLE (CreateEvent and
 *     CreateMutex), an unknown name in an Open function with ERROR_FILE_NOT_FOUND;
 *   - "Local\X" and "X" are one name, "Global\X" another ("Kernel Object Namespaces");
 *   - the documented limit on a name is MAX_PATH characters, and no backslash after the prefix;
 *   - cases Windows does not document well enough are fatal and name the value (empty name,
 *     backslash, over-long name, Session\<n>\). */
#include <string.h>

#include "rt/test.h"
#include "rt/handle.h"

typedef struct TObj {
    RtObject obj;
    uint32_t canary;
    uint32_t id;
} TObj;

#define ALIVE 0xA11FE000u
#define SENTINEL 0x5EA1u

static unsigned destroyed_count;
static void (*in_destroy)(RtObject *);

static void tobj_destroy(RtObject *o) {
    TObj *t = (TObj *)o;
    T_TRUE(t->canary == ALIVE);
    t->canary = 0;
    destroyed_count++;
    if (in_destroy) in_destroy(o);
}
static void other_destroy(RtObject *o) { ((TObj *)o)->canary = 0; destroyed_count += 100; }

static const RtObjectOps tops = { .name = "test", .destroy = tobj_destroy };
static const RtObjectOps tops2 = { .name = "test2", .destroy = other_destroy };

static uint32_t refs_of(RtObject *o) { return __atomic_load_n(&o->refs, __ATOMIC_SEQ_CST); }
static void plant_error(void) { rt_set_last_error(T_CTX(), SENTINEL); }

/* Create or find a named object; sets the canary on a new one. */
static TObj *mkn(RtKind kind, const WCHAR *name, bool *existed) {
    bool ex = true;
    TObj *t = (TObj *)rt_object_new(kind, sizeof *t, &tops, name, &ex);
    if (existed) *existed = ex;
    if (t && !ex) t->canary = ALIVE;
    return t;
}

static bool name_is(RtObject *o, const WCHAR *w) {
    if (!o->name) return false;
    size_t i = 0;
    for (; w[i]; i++) if (o->name[i] != w[i]) return false;
    return o->name[i] == 0;
}

static const RtKind nameable[] = { RT_K_EVENT, RT_K_MUTEX, RT_K_SEMAPHORE, RT_K_SECTION };
#define NNAMEABLE (sizeof nameable / sizeof *nameable)

/* A name of `n` copies of 'x', starting with `prefix` (may be empty).  Static buffer. */
static const WCHAR *long_name(const WCHAR *prefix, size_t total) {
    static WCHAR buf[1024];
    size_t p = 0;
    while (prefix[p]) { buf[p] = prefix[p]; p++; }
    for (; p < total; p++) buf[p] = 'x';
    buf[p] = 0;
    return buf;
}

TEST(core, handle_names_create)
{
    WCHAR src[] = u"MyEvent";
    bool ex = true;
    plant_error();
    TObj *a = mkn(RT_K_EVENT, src, &ex);
    T_TRUE(a != NULL);
    if (!a) return;
    T_EQ(ex, 0);                                    /* a new object: existed is set to false even if it was true */
    T_EQ(T_LAST_ERROR(), SENTINEL);                 /* the core does not set ERROR_ALREADY_EXISTS: the shim does */
    T_EQ(refs_of(&a->obj), 1);
    T_TRUE(a->obj.name != NULL && a->obj.name != src);      /* the core's own copy */
    T_TRUE(name_is(&a->obj, u"MyEvent"));
    src[0] = 'X';                                   /* changing the caller's string changes nothing */
    T_TRUE(name_is(&a->obj, u"MyEvent"));
    T_EQ(a->obj.kind, RT_K_EVENT);

    /* The same name and kind again: no new object, one more reference, existed. The size and
     * ops of the second call are ignored (CreateEvent on an existing event ignores bManualReset). */
    ex = false;
    plant_error();
    TObj *b = (TObj *)rt_object_new(RT_K_EVENT, sizeof(TObj) + 64, &tops2, u"MyEvent", &ex);
    T_TRUE(b == a);
    T_EQ(ex, 1);
    T_EQ(refs_of(&a->obj), 2);
    T_TRUE(a->obj.ops == &tops);
    T_EQ(T_LAST_ERROR(), SENTINEL);
    TObj *c = mkn(RT_K_EVENT, u"MyEvent", &ex);
    T_TRUE(c == a && ex);
    T_EQ(refs_of(&a->obj), 3);

    /* A different name is a different object. */
    TObj *d = mkn(RT_K_EVENT, u"MyEvent2", &ex);
    T_TRUE(d != NULL && d != a && !ex);
    TObj *e = mkn(RT_K_EVENT, u"MyEven", &ex);
    T_TRUE(e != NULL && e != a && e != d && !ex);
    TObj *f = mkn(RT_K_EVENT, u"MyEvent ", &ex);    /* trailing space is part of the name */
    T_TRUE(f != NULL && f != a && !ex);

    unsigned before = destroyed_count;
    rt_object_unref(&a->obj);
    rt_object_unref(&a->obj);
    T_EQ(destroyed_count, before);
    rt_object_unref(&a->obj);
    T_EQ(destroyed_count, before + 1);              /* once, when the last reference goes */
    rt_object_unref(&d->obj);
    rt_object_unref(&e->obj);
    rt_object_unref(&f->obj);
    T_EQ(destroyed_count, before + 4);

    /* The name died with the object and can be used again, by an object of any kind. */
    TObj *g = mkn(RT_K_MUTEX, u"MyEvent", &ex);
    T_TRUE(g != NULL && !ex);
    T_EQ(g->obj.kind, RT_K_MUTEX);
    rt_object_unref(&g->obj);

    /* Unnamed objects are not in the name space, however many there are. */
    TObj *u1 = (TObj *)rt_object_new(RT_K_EVENT, sizeof *u1, &tops, NULL, NULL);
    u1->canary = ALIVE;
    bool ex2 = true;
    TObj *u2 = (TObj *)rt_object_new(RT_K_EVENT, sizeof *u2, &tops, NULL, &ex2);
    u2->canary = ALIVE;
    T_TRUE(u1 != u2 && !ex2);
    T_TRUE(u1->obj.name == NULL && u2->obj.name == NULL);
    rt_object_unref(&u1->obj);
    rt_object_unref(&u2->obj);
}

TEST(core, handle_names_case_sensitive)
{
    /* "Name comparison is case sensitive". */
    bool ex;
    TObj *a = mkn(RT_K_EVENT, u"Event", &ex);
    T_TRUE(a && !ex);
    TObj *b = mkn(RT_K_EVENT, u"event", &ex);
    T_TRUE(b && !ex && b != a);
    TObj *c = mkn(RT_K_EVENT, u"EVENT", &ex);
    T_TRUE(c && !ex && c != a && c != b);
    TObj *d = mkn(RT_K_EVENT, u"eVENT", &ex);
    T_TRUE(d && !ex && d != a && d != b && d != c);
    T_TRUE(rt_object_open(RT_K_EVENT, u"Event") == &a->obj);
    rt_object_unref(&a->obj);
    plant_error();
    T_TRUE(rt_object_open(RT_K_EVENT, u"eveNT") == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_FILE_NOT_FOUND);
    T_TRUE(rt_object_open(RT_K_EVENT, u"event") == &b->obj);
    rt_object_unref(&b->obj);

    /* Exact on UTF-16 code units: names that differ only in the high byte, or only beyond
     * the first few characters, are different; the same unit sequence is the same name. */
    WCHAR hi[] = { 0x0141, 0 }, lo[] = { 0x0041, 0 }, hi2[] = { 0x0141, 0 };
    TObj *e = mkn(RT_K_MUTEX, hi, &ex);
    T_TRUE(e && !ex);
    TObj *f = mkn(RT_K_MUTEX, lo, &ex);
    T_TRUE(f && !ex && f != e);
    TObj *g = mkn(RT_K_MUTEX, hi2, &ex);
    T_TRUE(g == e && ex);
    TObj *h1 = mkn(RT_K_MUTEX, u"abcdefghijklmnopqrstuvwxyz0", &ex);
    TObj *h2 = mkn(RT_K_MUTEX, u"abcdefghijklmnopqrstuvwxyz1", &ex);
    T_TRUE(h1 && h2 && h1 != h2 && !ex);
    /* Surrogate pairs are two units. */
    TObj *s1 = mkn(RT_K_MUTEX, u"\xD83D\xDE00", &ex);
    TObj *s2 = mkn(RT_K_MUTEX, u"\xD83D\xDE01", &ex);
    T_TRUE(s1 && s2 && s1 != s2 && !ex);
    rt_object_unref(&a->obj); rt_object_unref(&b->obj); rt_object_unref(&c->obj); rt_object_unref(&d->obj);
    rt_object_unref(&e->obj); rt_object_unref(&e->obj); rt_object_unref(&f->obj);
    rt_object_unref(&h1->obj); rt_object_unref(&h2->obj); rt_object_unref(&s1->obj); rt_object_unref(&s2->obj);
}

TEST(core, handle_names_kinds)
{
    /* One name space for all the kinds that have names: a name taken by one kind fails for every
     * other, in both the Create and the Open function. */
    for (unsigned i = 0; i < NNAMEABLE; i++) {
        for (unsigned j = 0; j < NNAMEABLE; j++) {
            if (i == j) continue;
            bool ex = true;
            TObj *a = mkn(nameable[i], u"Shared", &ex);
            T_REQUIRE(a != NULL && !ex);

            plant_error();
            TObj *b = (TObj *)rt_object_new(nameable[j], sizeof(TObj), &tops, u"Shared", &ex);
            T_REQUIRE(b == NULL);
            T_REQUIRE(T_LAST_ERROR() == ERROR_INVALID_HANDLE);
            T_REQUIRE(ex == false);
            T_REQUIRE(refs_of(&a->obj) == 1);       /* the failed attempt took nothing */

            plant_error();
            T_REQUIRE(rt_object_open(nameable[j], u"Shared") == NULL);
            T_REQUIRE(T_LAST_ERROR() == ERROR_INVALID_HANDLE);
            T_REQUIRE(refs_of(&a->obj) == 1);

            RtObject *same = rt_object_open(nameable[i], u"Shared");
            T_REQUIRE(same == &a->obj);
            T_REQUIRE(refs_of(&a->obj) == 2);
            rt_object_unref(same);
            T_REQUIRE(a->obj.kind == nameable[i]);
            rt_object_unref(&a->obj);

            /* gone: the other kind may now have the name */
            TObj *c = mkn(nameable[j], u"Shared", &ex);
            T_REQUIRE(c != NULL && !ex);
            rt_object_unref(&c->obj);
        }
    }
}

TEST(core, handle_names_open)
{
    bool ex;
    plant_error();
    T_TRUE(rt_object_open(RT_K_EVENT, u"Nobody") == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_FILE_NOT_FOUND);
    for (unsigned i = 0; i < NNAMEABLE; i++) {
        plant_error();
        T_REQUIRE(rt_object_open(nameable[i], u"Nobody") == NULL);
        T_REQUIRE(T_LAST_ERROR() == ERROR_FILE_NOT_FOUND);
    }
    /* Opening never creates. */
    plant_error();
    T_TRUE(rt_object_open(RT_K_EVENT, u"Nobody") == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_FILE_NOT_FOUND);

    /* A NULL name (not in the Microsoft page): ERROR_INVALID_PARAMETER, as Wine's kernel32/tests/sync.c records for Windows. */
    plant_error();
    T_TRUE(rt_object_open(RT_K_EVENT, NULL) == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    TObj *a = mkn(RT_K_SEMAPHORE, u"Sem", &ex);
    plant_error();
    RtObject *o = rt_object_open(RT_K_SEMAPHORE, u"Sem");
    T_TRUE(o == &a->obj);
    T_EQ(T_LAST_ERROR(), SENTINEL);                 /* success leaves last error alone */
    T_EQ(refs_of(o), 2);
    RtObject *o2 = rt_object_open(RT_K_SEMAPHORE, u"Sem");
    T_TRUE(o2 == o);
    T_EQ(refs_of(o), 3);
    rt_object_unref(o);
    rt_object_unref(o2);
    unsigned before = destroyed_count;
    rt_object_unref(&a->obj);
    T_EQ(destroyed_count, before + 1);
    plant_error();
    T_TRUE(rt_object_open(RT_K_SEMAPHORE, u"Sem") == NULL);        /* gone with its last reference */
    T_EQ(T_LAST_ERROR(), ERROR_FILE_NOT_FOUND);

    /* Opening with a kind that cannot be named, or with RT_K_ANY, is a bug of the caller. */
    T_TRAPS(rt_object_open(RT_K_ANY, u"Sem"), "rt_object_open: 0 is not an object kind");
    T_TRAPS(rt_object_open((RtKind)RT_K_COUNT, u"Sem"), "is not an object kind");
    T_TRAPS(rt_object_open(RT_K_FILE, u"Sem"), "kind RT_K_FILE has no name");
    T_TRAPS(rt_object_open(RT_K_THREAD, u"Sem"), "kind RT_K_THREAD has no name");
    T_TRAPS(rt_object_open(RT_K_PROCESS, u"Sem"), "kind RT_K_PROCESS has no name");
    T_TRAPS(rt_object_open(RT_K_FIND, u"Sem"), "kind RT_K_FIND has no name");
    T_TRAPS(rt_object_open(RT_K_PIPE, u"Sem"), "kind RT_K_PIPE has no name");
    T_TRAPS(rt_object_open(RT_K_SOCKET, u"Sem"), "kind RT_K_SOCKET has no name");
    T_TRAPS(rt_object_open(RT_K_REGKEY, u"Sem"), "kind RT_K_REGKEY has no name");
}

TEST(core, handle_names_lifetime)
{
    /* A name lives as long as the object has any reference: the creator's, a table slot's,
     * a waiting thread's. */
    bool ex;
    TObj *a = mkn(RT_K_EVENT, u"Life", &ex);
    HANDLE h = rt_handle_new(&a->obj);              /* the creator's reference now belongs to the slot */
    T_TRUE(h != NULL);
    RtObject *o = rt_object_open(RT_K_EVENT, u"Life");
    T_TRUE(o == &a->obj);
    T_EQ(refs_of(o), 2);
    unsigned before = destroyed_count;
    T_EQ(rt_handle_close(h), TRUE);                 /* the slot is gone; "o" still holds the object */
    T_EQ(destroyed_count, before);
    RtObject *o2 = rt_object_open(RT_K_EVENT, u"Life");
    T_TRUE(o2 == o);                                /* still findable by name */
    TObj *again = mkn(RT_K_EVENT, u"Life", &ex);
    T_TRUE(again == a && ex);
    rt_object_unref(&again->obj);
    rt_object_unref(o2);
    T_EQ(destroyed_count, before);
    rt_object_unref(o);                             /* the last */
    T_EQ(destroyed_count, before + 1);
    plant_error();
    T_TRUE(rt_object_open(RT_K_EVENT, u"Life") == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_FILE_NOT_FOUND);

    /* Two handles (CreateEvent twice with the same name) keep the object until both close. */
    TObj *b = mkn(RT_K_EVENT, u"Twice", &ex);
    HANDLE h1 = rt_handle_new(&b->obj);
    TObj *b2 = mkn(RT_K_EVENT, u"Twice", &ex);
    T_TRUE(b2 == b && ex);
    HANDLE h2 = rt_handle_new(&b2->obj);
    T_TRUE(h1 && h2 && h1 != h2);
    T_EQ(refs_of(&b->obj), 2);
    HANDLE h3 = rt_handle_dup(h1);                  /* DuplicateHandle on a named object */
    T_EQ(refs_of(&b->obj), 3);
    before = destroyed_count;
    T_EQ(rt_handle_close(h1), TRUE);
    T_EQ(rt_handle_close(h2), TRUE);
    RtObject *via_name = rt_object_open(RT_K_EVENT, u"Twice");
    T_TRUE(via_name == &b->obj);                    /* h3 still holds it */
    rt_object_unref(via_name);
    T_EQ(rt_handle_close(h3), TRUE);
    T_EQ(destroyed_count, before + 1);
    T_TRUE(rt_object_open(RT_K_EVENT, u"Twice") == NULL);
}

static TObj *recreated;
static bool recreated_existed;
static void recreate_in_destroy(RtObject *o) {
    (void)o;
    recreated = (TObj *)rt_object_new(RT_K_EVENT, sizeof(TObj), &tops, u"Phoenix", &recreated_existed);
    if (recreated && !recreated_existed) recreated->canary = ALIVE;
}

TEST(core, handle_names_destroy_is_unlocked)
{
    /* destroy runs after the name is gone and with no lock held: it may create the same name
     * again (and open names, and drop other objects), and gets a brand-new object. */
    bool ex;
    TObj *a = mkn(RT_K_EVENT, u"Phoenix", &ex);
    in_destroy = recreate_in_destroy;
    unsigned before = destroyed_count;
    rt_object_unref(&a->obj);
    in_destroy = NULL;
    T_EQ(destroyed_count, before + 1);
    T_TRUE(recreated != NULL);
    T_EQ(recreated_existed, false);                 /* the old one was already out of the name space */
    T_EQ(refs_of(&recreated->obj), 1);
    T_TRUE(rt_object_open(RT_K_EVENT, u"Phoenix") == &recreated->obj);
    rt_object_unref(&recreated->obj);
    rt_object_unref(&recreated->obj);
    T_EQ(destroyed_count, before + 2);
}

TEST(core, handle_names_prefixes)
{
    /* "Local\X" is the session's own name space, the one a name without a prefix is in. */
    bool ex;
    TObj *plain = mkn(RT_K_EVENT, u"Pfx", &ex);
    T_TRUE(plain && !ex);
    TObj *local = mkn(RT_K_EVENT, u"Local\\Pfx", &ex);
    T_TRUE(local == plain && ex);                   /* the same name */
    T_TRUE(name_is(&local->obj, u"Pfx"));           /* the first spelling is kept */
    RtObject *o = rt_object_open(RT_K_EVENT, u"Local\\Pfx");
    T_TRUE(o == &plain->obj);
    rt_object_unref(o);
    /* "Global\X" is another name space: a different object. */
    TObj *global = mkn(RT_K_EVENT, u"Global\\Pfx", &ex);
    T_TRUE(global && !ex && global != plain);
    T_TRUE(name_is(&global->obj, u"Global\\Pfx"));
    T_TRUE(rt_object_open(RT_K_EVENT, u"Global\\Pfx") == &global->obj);
    rt_object_unref(&global->obj);
    TObj *global2 = mkn(RT_K_EVENT, u"Global\\Pfx", &ex);
    T_TRUE(global2 == global && ex);
    rt_object_unref(&global2->obj);
    /* Both are case-sensitive: "Global\pfx" is a third name. */
    TObj *global3 = mkn(RT_K_EVENT, u"Global\\pfx", &ex);
    T_TRUE(global3 && !ex && global3 != global);
    rt_object_unref(&global3->obj);
    rt_object_unref(&global->obj);                  /* the two references of "Global\Pfx" */
    /* A name created as Local\X is found as X. */
    TObj *loc = mkn(RT_K_MUTEX, u"Local\\Mtx", &ex);
    T_TRUE(loc && !ex);
    T_TRUE(name_is(&loc->obj, u"Local\\Mtx"));
    T_TRUE(rt_object_open(RT_K_MUTEX, u"Mtx") == &loc->obj);
    rt_object_unref(&loc->obj);
    TObj *loc2 = mkn(RT_K_MUTEX, u"Mtx", &ex);
    T_TRUE(loc2 == loc && ex);
    rt_object_unref(&loc2->obj);
    rt_object_unref(&loc->obj);
    T_TRUE(rt_object_open(RT_K_MUTEX, u"Mtx") == NULL);
    rt_object_unref(&plain->obj);
    rt_object_unref(&plain->obj);
    /* Names that only start like a prefix are ordinary names. */
    const WCHAR *ordinary[] = { u"Local", u"Global", u"Locale", u"Globalx", u"LocalX", u"Loca", u"Glob", u"Session" };
    for (unsigned i = 0; i < sizeof ordinary / sizeof *ordinary; i++) {
        TObj *t = mkn(RT_K_EVENT, ordinary[i], &ex);
        T_REQUIRE(t != NULL && !ex);
        T_REQUIRE(rt_object_open(RT_K_EVENT, ordinary[i]) == &t->obj);
        rt_object_unref(&t->obj);
        rt_object_unref(&t->obj);
    }
}

TEST(core, handle_names_characters)
{
    /* Any character except the backslash is allowed (the Create* pages): spaces, punctuation
     * that file names forbid, control characters, non-ASCII, a lone surrogate. */
    const WCHAR *names[] = {
        u" ", u"a b", u"a:b", u"a/b", u"a*b?c", u"<>|\"", u".", u"..", u"a.b.c", u"{3F2504E0-4F89-11D3-9A0C-0305E82C3301}",
        u"\x01\x02", u"\t", u"\xE9t\xE9", u"\x4E2D\x6587", u"\xD800", u"\xFFFF", u"0", u"-", u"~$name",
    };
    enum { N = sizeof names / sizeof *names };
    TObj *objs[N];
    for (unsigned i = 0; i < N; i++) {
        bool ex = true;
        objs[i] = mkn(RT_K_EVENT, names[i], &ex);
        T_REQUIRE(objs[i] != NULL && !ex);
    }
    for (unsigned i = 0; i < N; i++) {
        T_REQUIRE(rt_object_open(RT_K_EVENT, names[i]) == &objs[i]->obj);
        rt_object_unref(&objs[i]->obj);
        for (unsigned j = 0; j < i; j++) T_REQUIRE(objs[i] != objs[j]);
    }
    for (unsigned i = 0; i < N; i++) rt_object_unref(&objs[i]->obj);
}

TEST(core, handle_names_length)
{
    /* The documented limit is MAX_PATH (260) characters.  Boundaries: 1, 259, 260 work; 261
     * does not.  The prefix counts. */
    bool ex;
    size_t lens[] = { 1, 2, 255, 256, 257, 258, 259, 260 };
    for (unsigned i = 0; i < sizeof lens / sizeof *lens; i++) {
        TObj *a = mkn(RT_K_EVENT, long_name(u"", lens[i]), &ex);
        T_REQUIRE(a != NULL && !ex);
        T_REQUIRE(rt_object_open(RT_K_EVENT, long_name(u"", lens[i])) == &a->obj);
        rt_object_unref(&a->obj);
        rt_object_unref(&a->obj);
    }
    /* Names of different length with the same start are different names. */
    TObj *a1 = mkn(RT_K_EVENT, long_name(u"", 259), &ex);
    TObj *a2 = mkn(RT_K_EVENT, long_name(u"", 260), &ex);
    T_TRUE(a1 && a2 && a1 != a2 && !ex);
    rt_object_unref(&a1->obj);
    rt_object_unref(&a2->obj);
    TObj *g = mkn(RT_K_EVENT, long_name(u"Global\\", 260), &ex);
    T_TRUE(g != NULL && !ex);
    T_TRUE(name_is(&g->obj, long_name(u"Global\\", 260)));
    rt_object_unref(&g->obj);
    TObj *l = mkn(RT_K_EVENT, long_name(u"Local\\", 260), &ex);
    T_TRUE(l != NULL && !ex);
    rt_object_unref(&l->obj);

    bool ex2;
    T_TRAPS(rt_object_new(RT_K_EVENT, sizeof(TObj), &tops, long_name(u"", 261), &ex2),
            "is 261 UTF-16 code units; the documented limit is MAX_PATH (260)");
    T_TRAPS(rt_object_new(RT_K_EVENT, sizeof(TObj), &tops, long_name(u"Global\\", 261), &ex2), "261 UTF-16 code units");
    T_TRAPS(rt_object_new(RT_K_EVENT, sizeof(TObj), &tops, long_name(u"Local\\", 261), &ex2), "261 UTF-16 code units");
    T_TRAPS(rt_object_new(RT_K_EVENT, sizeof(TObj), &tops, long_name(u"", 1000), &ex2), "1000 UTF-16 code units");
    T_TRAPS(rt_object_open(RT_K_EVENT, long_name(u"", 261)), "261 UTF-16 code units");
}

TEST(core, handle_names_loud)
{
    /* Names whose treatment Windows does not document well enough: fatal, with the name. */
    bool ex;
    T_TRAPS(rt_object_new(RT_K_EVENT, sizeof(TObj), &tops, u"", &ex), "an empty object name \"\"");
    T_TRAPS(rt_object_open(RT_K_EVENT, u""), "an empty object name \"\"");
    T_TRAPS(rt_object_new(RT_K_EVENT, sizeof(TObj), &tops, u"Foo\\Bar", &ex),
            "object name \"Foo\\Bar\" contains a backslash after its prefix");
    T_TRAPS(rt_object_new(RT_K_EVENT, sizeof(TObj), &tops, u"\\Foo", &ex), "object name \"\\Foo\" contains a backslash");
    T_TRAPS(rt_object_new(RT_K_EVENT, sizeof(TObj), &tops, u"Foo\\", &ex), "object name \"Foo\\\" contains a backslash");
    T_TRAPS(rt_object_new(RT_K_EVENT, sizeof(TObj), &tops, u"\\", &ex), "contains a backslash");
    T_TRAPS(rt_object_new(RT_K_EVENT, sizeof(TObj), &tops, u"Local\\Foo\\Bar", &ex), "\"Local\\Foo\\Bar\" contains a backslash");
    T_TRAPS(rt_object_new(RT_K_EVENT, sizeof(TObj), &tops, u"Global\\Foo\\Bar", &ex), "contains a backslash");
    T_TRAPS(rt_object_new(RT_K_EVENT, sizeof(TObj), &tops, u"Global\\Local\\Foo", &ex), "contains a backslash");
    T_TRAPS(rt_object_new(RT_K_EVENT, sizeof(TObj), &tops, u"Local\\Global\\Foo", &ex), "contains a backslash");
    T_TRAPS(rt_object_new(RT_K_EVENT, sizeof(TObj), &tops, u"Local\\Local\\Foo", &ex), "contains a backslash");
    T_TRAPS(rt_object_open(RT_K_EVENT, u"Foo\\Bar"), "contains a backslash");
    /* Nothing after the prefix. */
    T_TRAPS(rt_object_new(RT_K_EVENT, sizeof(TObj), &tops, u"Local\\", &ex), "\"Local\\\" has nothing after its prefix");
    T_TRAPS(rt_object_new(RT_K_EVENT, sizeof(TObj), &tops, u"Global\\", &ex), "\"Global\\\" has nothing after its prefix");
    T_TRAPS(rt_object_open(RT_K_EVENT, u"Global\\"), "has nothing after its prefix");
    /* The prefixes are case-sensitive here: another spelling is a name with a backslash. */
    T_TRAPS(rt_object_new(RT_K_EVENT, sizeof(TObj), &tops, u"global\\Foo", &ex), "\"global\\Foo\" contains a backslash");
    T_TRAPS(rt_object_new(RT_K_EVENT, sizeof(TObj), &tops, u"LOCAL\\Foo", &ex), "contains a backslash");
    /* Session\<n>\ */
    T_TRAPS(rt_object_new(RT_K_EVENT, sizeof(TObj), &tops, u"Session\\1\\Foo", &ex), "\"Session\\<n>\\\" form");
    T_TRAPS(rt_object_open(RT_K_EVENT, u"Session\\1\\Foo"), "\"Session\\<n>\\\" form");
    /* Non-printable and non-ASCII characters in a report are escaped. */
    T_TRAPS(rt_object_new(RT_K_EVENT, sizeof(TObj), &tops, u"\x01\xE9\\", &ex), "\"\\u0001\\u00e9\\\"");
    /* None of these left anything behind. */
    plant_error();
    T_TRUE(rt_object_open(RT_K_EVENT, u"Foo") == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_FILE_NOT_FOUND);
}

TEST(core, handle_names_many)
{
    /* More names than buckets: chains, lookups in the middle of chains, removal from the
     * head, the middle and the tail of a chain. */
    enum { N = 5000 };
    static TObj *objs[N];
    WCHAR name[16];
    for (unsigned i = 0; i < N; i++) {
        bool ex = true;
        unsigned n = i;
        name[0] = 'N';
        for (int d = 5; d >= 1; d--) { name[d] = (WCHAR)('0' + n % 10); n /= 10; }
        name[6] = 0;
        objs[i] = mkn(nameable[i % NNAMEABLE], name, &ex);
        T_REQUIRE(objs[i] != NULL && !ex);
    }
    unsigned wrong = 0;
    for (unsigned i = 0; i < N; i++) {
        unsigned n = i;
        name[0] = 'N';
        for (int d = 5; d >= 1; d--) { name[d] = (WCHAR)('0' + n % 10); n /= 10; }
        name[6] = 0;
        RtObject *o = rt_object_open(nameable[i % NNAMEABLE], name);
        if (o != &objs[i]->obj) wrong++;
        if (o) rt_object_unref(o);
    }
    T_EQ(wrong, 0);
    /* destroy in a scrambled order, checking the others still resolve */
    unsigned before = destroyed_count;
    for (unsigned step = 0; step < N; step++) {
        unsigned i = (step * 2711u) % N;            /* 2711 is prime and coprime with N: visits every index once */
        rt_object_unref(&objs[i]->obj);
        objs[i] = NULL;
        if (step % 500 == 0) {
            unsigned bad = 0;
            for (unsigned k = 0; k < N; k += 97) {
                unsigned n = k;
                name[0] = 'N';
                for (int d = 5; d >= 1; d--) { name[d] = (WCHAR)('0' + n % 10); n /= 10; }
                name[6] = 0;
                RtObject *o = rt_object_open(nameable[k % NNAMEABLE], name);
                if ((o != NULL) != (objs[k] != NULL)) bad++;
                if (o) rt_object_unref(o);
            }
            T_REQUIRE(bad == 0);
        }
    }
    T_EQ(destroyed_count, before + N);
}
