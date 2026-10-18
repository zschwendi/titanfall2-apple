/* Kernel-object handles (docs/RUNTIME.md section 4.1).  Interface and specification: rt/handle.h.
 *
 * WHAT IS HERE
 *
 *   objects     rt_object_new / rt_object_open / rt_object_ref / rt_object_unref: reference-
 *               counted host objects with a kind and an ops table, and the one name space
 *               they share.
 *   table       rt_handle_new / _ref / _close / _close_kind / _dup: a table of slots that
 *               turns a 32-bit handle into an object.  The table is a VALUE (RtHandleTable)
 *               that can be created more than once; the kernel handle table is the one
 *               instance rt_handle_init makes.  The second instance RUNTIME.md 4.1 plans for
 *               window handles uses the rt_htab_* functions (declared in rt/handle.h).
 *   pseudo      (HANDLE)-1 and (HANDLE)-2, resolved through two hooks (see PSEUDO-HANDLES).
 *
 * HANDLE VALUES
 *
 *   (generation << 22) | (index << 2).  Index is 20 bits (at most 1,048,576 slots), generation
 *   9 bits, so the value is always below 2^31: never negative as a 32-bit value.  Generations
 *   are never 0, so the value is never 0 either, and a small integer a guest passes by mistake
 *   (1, 4, 8 ...) never names a slot.  A slot's generation advances every time the slot is
 *   freed, so a stale handle names a free slot or a slot of another generation and fails.
 *   The generation wraps, so a very old stale handle can name a newer object again; two
 *   things make that remote: a freed slot waits in a FIFO until `min_free` other slots have
 *   been freed after it before it is handed out again (the kernel table: 1024), and the
 *   kernel table's generations are 1..255.  Reuse of one slot thus needs at least 255 reuses
 *   of it, each after 1,024 other frees.  The window table is given generations 256..511,
 *   so a kernel handle passed to it (or the reverse) can never name anything: mixing the
 *   two spaces fails cleanly, as RUNTIME.md 4.1 requires.
 *
 *   A lookup uses only the low 32 bits of the HANDLE and ignores the low two bits, as
 *   Windows does.  The one exception is the pseudo-handles, which are recognised by their
 *   full 64-bit value (the sign-extended -1 and -2): a zero-extended 0xFFFFFFFF is not one
 *   (it is a "negative" 32-bit value, which no slot has, so it fails with
 *   ERROR_INVALID_HANDLE).  UNSURE: Windows might also accept some other spelling.
 *
 * LOCKING: how a reference taken by one thread keeps an object alive while another thread
 * closes the handle
 *
 *   There are exactly two locks, `table->lock` (one per table) and `names_lock` (the name
 *   space).  Both are LEAF locks: while holding one, this file calls nothing outside the
 *   standard library (no ops->destroy, no logging, no other lock).  Callers may therefore
 *   hold any lock of their own, the dispatcher lock included, when they call into this file.
 *
 *   1. A slot holds one reference to its object, for as long as the slot is occupied.
 *   2. rt_handle_ref finds the slot and increments the object's count in ONE critical
 *      section of table->lock.  While the slot is occupied the count is at least 1 (rule 1),
 *      so the increment can never bring a dying object back to life.
 *   3. rt_handle_close (and rt_handle_close_kind) empty the slot in a critical section of
 *      table->lock and only AFTER leaving it drop the slot's reference.  So a thread that
 *      found the slot before the close holds its own reference, the count stays above 0 and
 *      the object survives until that thread calls rt_object_unref; a thread that looks after
 *      the close finds an empty slot and fails with ERROR_INVALID_HANDLE.  The two orders
 *      are the only two, because both steps are under the same lock.
 *   4. Counts are changed with atomic operations, so rt_object_ref (by a thread that already
 *      holds a reference) and rt_object_unref need no table lock.
 *   5. The last rt_object_unref calls ops->destroy and frees the object, on the calling
 *      thread, with NO lock of this file held.  destroy may call anything in this file.
 *      If the caller of rt_object_unref holds the dispatcher lock, so does destroy.
 *   6. NAMED objects: the name space can hand out a new reference (rt_object_new and
 *      rt_object_open find an object by name), which rules 1 to 4 do not cover, since a named
 *      object may have no slot.  So the count of a named object is changed under
 *      names_lock when it falls, and the same critical section removes the name when it
 *      reaches 0.  An object that is still in the name space therefore always has a count of
 *      at least 1, and a lookup that finds it increments under the same lock.  (A named
 *      object's other increments and decrements to a non-zero value are as in rule 4.)
 *
 *   Not covered by any lock here, and therefore a rule for the unit that owns a kind: a
 *   NAMED object is findable by name the moment rt_object_new returns it with *existed ==
 *   false, before the creator has filled in the fields after the RtObject header.  A creator
 *   that must not be observed half-built (an event created signalled, a mutex created owned)
 *   has to initialise under a lock that the Open function of the same kind also takes
 *   (the dispatcher lock does for waitable kinds, since waiting and signalling take it).
 *
 * NAMES (RUNTIME.md 4.1: one name space, local to the process)
 *
 *   - Comparison is CASE-SENSITIVE and exact on UTF-16 code units.  Rule: the documentation
 *     of CreateEvent, CreateMutex, CreateSemaphore, CreateFileMapping and the Open functions
 *     says "Name comparison is case sensitive".
 *   - Event, mutex, semaphore and file-mapping (section) objects have names; no other kind
 *     does (files, threads, the process, find state, pipes, sockets and registry keys live
 *     in other name spaces or have none).  A name given with any other kind is a bug of
 *     the caller and is fatal.
 *   - A name that exists with a different kind: the lookup fails with ERROR_INVALID_HANDLE.
 *     Rule: CreateEvent and CreateMutex document it for the name of an existing semaphore,
 *     mutex, waitable timer, job or file-mapping object.
 *   - "Local\" prefix: names a session-local object, which is the name space a name without a
 *     prefix is in, so "Local\X" and "X" are the same name.  "Global\X" is a different name
 *     from "X" (the global name space is distinct from the session's).  Rule: the Windows
 *     documentation "Kernel Object Namespaces".  UNSURE: whether the prefixes themselves are
 *     case-sensitive; here they are, so "global\X" is a name with a backslash (see next).
 *   - Not handled, FATAL with the name in the report (Windows's behaviour is not documented
 *     well enough to pick a failure return): an empty name; an empty name after the prefix;
 *     a backslash after the prefix (the documentation says a name may contain any character
 *     except the backslash, and does not say what happens to one that does; Windows may
 *     report ERROR_PATH_NOT_FOUND, but that is from memory); the "Session\<n>\" form; more
 *     than MAX_PATH (260) UTF-16 code units, the documented limit.
 *
 * PSEUDO-HANDLES
 *
 *   (HANDLE)-1 is the current process and (HANDLE)-2 the current thread.  The objects behind
 *   them are not made here and the units that make them do not exist yet, so:
 *     process  rt_handle_set_process_object(obj) registers the one RT_K_PROCESS object.  The
 *              table takes its own reference.
 *     thread   the thread unit stores a pointer to the thread's own RT_K_THREAD object in
 *              RtThread.sub[RT_SUB_HANDLE] (rt/thread.h already reserves the slot for this).
 *              The pointer is BORROWED: the thread unit keeps one reference for as long as
 *              the thread runs and clears the slot before it drops that reference.
 *   Until a hook is filled, resolving the pseudo-handle is fatal and says which one.
 *   A pseudo-handle resolves only where the caller's kind allows it: RT_K_ANY, or
 *   RT_K_PROCESS for -1 and RT_K_THREAD for -2; any other kind fails with
 *   ERROR_INVALID_HANDLE.  CloseHandle of one succeeds and does nothing (the documentation
 *   of GetCurrentProcess: "Calling the CloseHandle function with a pseudo handle has no
 *   effect").  DuplicateHandle of one makes a real handle.
 *
 * LAST ERROR
 *
 *   Set through rt_thread_set_last_error.  A host thread that is not a guest thread has no
 *   thread block, so its value goes to a variable of thread.c that is local to that host
 *   thread (rt/thread.h).
 *
 * FATAL REPORTS
 *
 *   A case this file does not handle ends in rt_unimplemented (the skeleton's report, which
 *   names the shim that was running and the offending value); a misuse of the interface by
 *   a core caller (a NULL object, a count that goes below 0, a kind that is not a kind) ends
 *   in rt_fatal.  Neither returns.
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core.h"
#include "rt/handle.h"

/* ------------------------------------------------------------------------------------------ */
/* Constants                                                                                  */
/* ------------------------------------------------------------------------------------------ */

#define H_INDEX_SHIFT    2
#define H_GEN_SHIFT      22
#define H_MAX_SLOTS      (UINT32_C(1) << 20)         /* index bits 2..21 */
#define H_INDEX_MASK     (H_MAX_SLOTS - 1)
#define H_GEN_MAX        511u                        /* bit 31 stays clear: never negative */
#define H_NO_SLOT        UINT32_MAX

#define KERNEL_GEN_LO    1u                          /* 0 is never a generation: a handle is never 0 */
#define KERNEL_GEN_HI    255u                        /* 256..511 are the window table's (see above) */
#define KERNEL_MIN_FREE  1024u

#define PSEUDO_PROCESS   UINT64_MAX                  /* (HANDLE)-1 */
#define PSEUDO_THREAD    (UINT64_MAX - 1)            /* (HANDLE)-2 */

#define NAME_BUCKETS     1024u
#define NAME_MAX_UNITS   MAX_PATH                    /* the documented limit on a name's length */

/* ------------------------------------------------------------------------------------------ */
/* Small helpers                                                                              */
/* ------------------------------------------------------------------------------------------ */

static void mutex_lock(pthread_mutex_t *m) {
    int e = pthread_mutex_lock(m);
    if (e) rt_fatal("handle table: pthread_mutex_lock failed (error %d)", e);
}

static void mutex_unlock(pthread_mutex_t *m) {
    int e = pthread_mutex_unlock(m);
    if (e) rt_fatal("handle table: pthread_mutex_unlock failed (error %d)", e);
}

static Ctx *self_ctx(void) {
    RtThread *t = rt_thread_self();
    return t ? &t->ctx : NULL;
}

/* A case this unit does not handle: the skeleton's report, naming the offending value. */
#define UNHANDLED(...)  rt_unimplemented(self_ctx(), __FILE__, __LINE__, __VA_ARGS__)

static const char *kind_name(uint32_t kind) {
    static const char *const names[RT_K_COUNT] = {
        "RT_K_ANY", "RT_K_FILE", "RT_K_EVENT", "RT_K_MUTEX", "RT_K_SEMAPHORE", "RT_K_THREAD", "RT_K_PROCESS",
        "RT_K_SECTION", "RT_K_FIND", "RT_K_PIPE", "RT_K_SOCKET", "RT_K_REGKEY",
        "RT_K_WINDOW", "RT_K_DC", "RT_K_GDIOBJ", "RT_K_HOOK", "RT_K_MSGQUEUE",
    };
    return kind < RT_K_COUNT ? names[kind] : "(not a kind)";
}

/* A name for a report: printable ASCII as is, everything else as \uXXXX, cut at 100 units. */
static void name_for_report(const WCHAR *w, char *buf, size_t n) {
    size_t o = 0;
    for (size_t i = 0; w[i] && o + 8 < n; i++) {
        if (i == 100) { o += (size_t)snprintf(buf + o, n - o, "..."); break; }
        if (w[i] >= 0x20 && w[i] < 0x7F) buf[o++] = (char)w[i];
        else o += (size_t)snprintf(buf + o, n - o, "\\u%04x", w[i]);
    }
    buf[o] = 0;
}

static size_t wlen(const WCHAR *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

/* ------------------------------------------------------------------------------------------ */
/* Objects                                                                                    */
/* ------------------------------------------------------------------------------------------ */

/* Hidden header in front of every RtObject (16 bytes, so the object stays 16-byte aligned). */
#define OBJ_MAGIC  UINT32_C(0x4F424A31)              /* "OBJ1" */
#define OBJ_DEAD   UINT32_C(0xDEADDEAD)

typedef struct ObjPrefix {
    uint32_t  magic;          /* OBJ_MAGIC while alive: catches a pointer that is not an object */
    uint32_t  hash;           /* hash of the name's key, if named */
    RtObject *name_next;      /* chain in the name space; guarded by names_lock */
} ObjPrefix;

_Static_assert(sizeof(ObjPrefix) == 16, "the hidden header keeps objects 16-byte aligned");

static ObjPrefix *prefix_of(RtObject *obj) { return (ObjPrefix *)obj - 1; }

static ObjPrefix *checked_prefix(RtObject *obj, const char *fn) {
    if (!obj) rt_fatal("%s: NULL object", fn);
    ObjPrefix *p = prefix_of(obj);
    if (p->magic != OBJ_MAGIC)
        rt_fatal("%s: %p is not a live object made by rt_object_new (header word %#x%s)", fn, (void *)obj,
                 p->magic, p->magic == OBJ_DEAD ? ": it was already destroyed" : "");
    return p;
}

/* The name space: chained hash table, case-sensitive.  names_lock guards the chains and the
 * count of every NAMED object when it falls (rule 6). */
static pthread_mutex_t names_lock = PTHREAD_MUTEX_INITIALIZER;
static RtObject       *name_buckets[NAME_BUCKETS];

static bool kind_may_be_named(uint32_t kind) {
    return kind == RT_K_EVENT || kind == RT_K_MUTEX || kind == RT_K_SEMAPHORE || kind == RT_K_SECTION;
}

static const WCHAR local_prefix[]  = { 'L', 'o', 'c', 'a', 'l', '\\' };
static const WCHAR global_prefix[] = { 'G', 'l', 'o', 'b', 'a', 'l', '\\' };
static const WCHAR session_prefix[] = { 'S', 'e', 's', 's', 'i', 'o', 'n', '\\' };

static bool starts_with(const WCHAR *s, const WCHAR *prefix, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (s[i] != prefix[i]) return false;
    return true;
}

/* The identity of an already validated name: "Local\X" is "X" (the session name space is the
 * default one); "Global\X" stays as it is and is therefore a different name. */
static const WCHAR *name_key(const WCHAR *name) {
    return starts_with(name, local_prefix, sizeof local_prefix / sizeof *local_prefix)
               ? name + sizeof local_prefix / sizeof *local_prefix
               : name;
}

/* Validate a name given by the guest and return its key.  Every case Windows's rules do not
 * settle is fatal with the name in the report (see NAMES above). */
static const WCHAR *checked_name_key(const WCHAR *name, const char *fn) {
    char shown[128];
    name_for_report(name, shown, sizeof shown);
    size_t n = wlen(name);
    if (n == 0)
        UNHANDLED("%s: an empty object name \"\" (what Windows does with one is not documented)", fn);
    if (n > NAME_MAX_UNITS)
        UNHANDLED("%s: object name \"%s\" is %zu UTF-16 code units; the documented limit is MAX_PATH (%d) and what "
                  "Windows does beyond it is not documented", fn, shown, n, MAX_PATH);
    const WCHAR *rest = name;
    if (starts_with(name, session_prefix, sizeof session_prefix / sizeof *session_prefix))
        UNHANDLED("%s: object name \"%s\" uses the \"Session\\<n>\\\" form, which is not supported", fn, shown);
    if (starts_with(name, local_prefix, sizeof local_prefix / sizeof *local_prefix))
        rest = name + sizeof local_prefix / sizeof *local_prefix;
    else if (starts_with(name, global_prefix, sizeof global_prefix / sizeof *global_prefix))
        rest = name + sizeof global_prefix / sizeof *global_prefix;
    if (!*rest)
        UNHANDLED("%s: object name \"%s\" has nothing after its prefix (what Windows does with it is not documented)",
                  fn, shown);
    for (const WCHAR *c = rest; *c; c++)
        if (*c == '\\')
            UNHANDLED("%s: object name \"%s\" contains a backslash after its prefix; the documentation allows any "
                      "character except the backslash and does not say what a backslash does (it may be "
                      "ERROR_PATH_NOT_FOUND, from memory)", fn, shown);
    return name_key(name);
}

static uint32_t key_hash(const WCHAR *key) {
    uint32_t h = UINT32_C(2166136261);
    for (; *key; key++) {
        h = (h ^ (*key & 0xFFu)) * UINT32_C(16777619);
        h = (h ^ (*key >> 8)) * UINT32_C(16777619);
    }
    return h;
}

static bool key_equal(const WCHAR *a, const WCHAR *b) {
    for (; *a && *a == *b; a++, b++) { }
    return *a == *b;
}

/* names_lock held. */
static RtObject *name_find_locked(const WCHAR *key, uint32_t hash) {
    for (RtObject *o = name_buckets[hash % NAME_BUCKETS]; o; o = prefix_of(o)->name_next)
        if (prefix_of(o)->hash == hash && key_equal(name_key(o->name), key)) return o;
    return NULL;
}

/* names_lock held. */
static void name_insert_locked(RtObject *obj) {
    ObjPrefix *p = prefix_of(obj);
    RtObject **head = &name_buckets[p->hash % NAME_BUCKETS];
    p->name_next = *head;
    *head = obj;
}

/* names_lock held. */
static void name_unlink_locked(RtObject *obj) {
    RtObject **link = &name_buckets[prefix_of(obj)->hash % NAME_BUCKETS];
    while (*link && *link != obj) link = &prefix_of(*link)->name_next;
    if (!*link) rt_fatal("object %p (%s) is named but is not in the name space", (void *)obj, kind_name(obj->kind));
    *link = prefix_of(obj)->name_next;
    prefix_of(obj)->name_next = NULL;
}

/* One more reference.  The caller either holds a reference already or is inside the critical
 * section that makes the object findable (rules 2, 4, 6), so the count is at least 1. */
static void object_retain(RtObject *obj, const char *fn) {
    uint32_t old = __atomic_fetch_add(&obj->refs, 1, __ATOMIC_RELAXED);
    if (old == 0)
        rt_fatal("%s: object %p has no references: it was released once too often or is already destroyed",
                 fn, (void *)obj);
    if (old >= UINT32_C(0x7FFFFFFF))
        rt_fatal("%s: object %p (%s) has %u references; a reference count that large is a leak", fn, (void *)obj,
                 kind_name(obj->kind), old);
}

/* Drop one reference; true if it was the last. */
static bool object_release(RtObject *obj, const char *fn) {
    uint32_t old = __atomic_fetch_sub(&obj->refs, 1, __ATOMIC_ACQ_REL);
    if (old == 0)
        rt_fatal("%s: object %p (%s) released more often than it was referenced", fn, (void *)obj,
                 kind_name(obj->kind));
    return old == 1;
}

/* Free an object that was never shared (the speculative allocation of rt_object_new when the
 * name turned out to exist).  ops->destroy is NOT called: nothing was ever built. */
static void object_discard(RtObject *obj) {
    free(obj->name);
    prefix_of(obj)->magic = OBJ_DEAD;
    free(prefix_of(obj));
}

/* The last reference is gone and the object is out of the name space. */
static void object_destroy(RtObject *obj) {
    if (obj->waiters)
        rt_fatal("object %p (%s) reached zero references while threads are still queued on it: a waiting thread "
                 "must hold a reference", (void *)obj, kind_name(obj->kind));
    if (obj->ops->destroy) obj->ops->destroy(obj);
    object_discard(obj);
}

static RtKind checked_kind(uint32_t kind, bool allow_any, const char *fn) {
    if (kind >= RT_K_COUNT || (kind == RT_K_ANY && !allow_any))
        rt_fatal("%s: %u is not an object kind (valid kinds are 1 to %d%s)", fn, kind, RT_K_COUNT - 1,
                 allow_any ? ", or RT_K_ANY" : "");
    return (RtKind)kind;
}

RtObject *rt_object_new(RtKind kind, size_t size, const RtObjectOps *ops, const WCHAR *name, bool *existed) {
    checked_kind(kind, false, "rt_object_new");
    if (size < sizeof(RtObject))
        rt_fatal("rt_object_new(%s): size %zu is smaller than the RtObject header (%zu bytes); the object's own "
                 "struct must start with an RtObject", kind_name(kind), size, sizeof(RtObject));
    if (!ops || !ops->name)
        rt_fatal("rt_object_new(%s): %s", kind_name(kind), ops ? "the ops table has no name" : "no ops table");

    const WCHAR *key = NULL;
    if (name) {
        if (!existed)
            rt_fatal("rt_object_new(%s): a named object needs `existed`, or the caller cannot report "
                     "ERROR_ALREADY_EXISTS", kind_name(kind));
        if (!kind_may_be_named(kind)) {
            char shown[128];
            name_for_report(name, shown, sizeof shown);
            rt_fatal("rt_object_new: a name (\"%s\") was given for kind %s, which has no name in the kernel object "
                     "name space (only events, mutexes, semaphores and file-mapping objects do)", shown, kind_name(kind));
        }
        key = checked_name_key(name, "rt_object_new");
    }
    if (existed) *existed = false;

    if (size > SIZE_MAX - sizeof(ObjPrefix)) {
        rt_thread_set_last_error(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    ObjPrefix *p = calloc(1, sizeof *p + size);
    WCHAR *copy = NULL;
    if (p && name) {
        copy = malloc((wlen(name) + 1) * sizeof *copy);
        if (!copy) { free(p); p = NULL; }
    }
    if (!p) {
        rt_thread_set_last_error(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    RtObject *obj = (RtObject *)(p + 1);
    p->magic = OBJ_MAGIC;
    obj->kind = (uint32_t)kind;
    obj->refs = 1;
    obj->ops = ops;
    if (!name) return obj;

    memcpy(copy, name, (wlen(name) + 1) * sizeof *copy);
    obj->name = copy;
    p->hash = key_hash(key);

    mutex_lock(&names_lock);
    RtObject *found = name_find_locked(key, p->hash);
    if (!found) {
        name_insert_locked(obj);
        mutex_unlock(&names_lock);
        return obj;
    }
    /* The name exists.  Same kind: that object is the result.  Another kind: failure (rule of
     * CreateEvent and CreateMutex, see NAMES). */
    bool same = found->kind == (uint32_t)kind;
    if (same) object_retain(found, "rt_object_new");
    mutex_unlock(&names_lock);
    object_discard(obj);
    if (!same) {
        rt_thread_set_last_error(ERROR_INVALID_HANDLE);
        return NULL;
    }
    *existed = true;
    return found;
}

RtObject *rt_object_open(RtKind kind, const WCHAR *name) {
    checked_kind(kind, false, "rt_object_open");
    if (!kind_may_be_named(kind))
        rt_fatal("rt_object_open: kind %s has no name in the kernel object name space", kind_name(kind));
    if (!name) {
        /* Confirmed by Wine's kernel32/tests/sync.c (test_event, test_mutex, test_semaphore: a NULL
         * name gives ERROR_INVALID_PARAMETER on Windows).  It was written from memory of ReactOS and Wine (OpenEvent(..., NULL) fails with
         * ERROR_INVALID_PARAMETER); the Microsoft page does not say. */
        rt_thread_set_last_error(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    const WCHAR *key = checked_name_key(name, "rt_object_open");
    uint32_t hash = key_hash(key);

    mutex_lock(&names_lock);
    RtObject *found = name_find_locked(key, hash);
    bool same = found && found->kind == (uint32_t)kind;
    if (same) object_retain(found, "rt_object_open");
    mutex_unlock(&names_lock);
    if (!found) { rt_thread_set_last_error(ERROR_FILE_NOT_FOUND); return NULL; }
    if (!same)  { rt_thread_set_last_error(ERROR_INVALID_HANDLE); return NULL; }
    return found;
}

void rt_object_ref(RtObject *obj) {
    checked_prefix(obj, "rt_object_ref");
    object_retain(obj, "rt_object_ref");
}

void rt_object_unref(RtObject *obj) {
    checked_prefix(obj, "rt_object_unref");
    bool last;
    if (obj->name) {
        /* Rule 6: the decrement that can reach 0 and the removal of the name are one critical
         * section, so no lookup by name can find an object whose count is already 0. */
        mutex_lock(&names_lock);
        last = object_release(obj, "rt_object_unref");
        if (last) name_unlink_locked(obj);
        mutex_unlock(&names_lock);
    } else {
        last = object_release(obj, "rt_object_unref");
    }
    if (last) object_destroy(obj);          /* rule 5: no lock held */
}

/* ------------------------------------------------------------------------------------------ */
/* The table                                                                                  */
/* ------------------------------------------------------------------------------------------ */

typedef struct HSlot {
    RtObject *obj;            /* NULL: the slot is free */
    uint32_t  gen;            /* the generation a handle to this slot must carry now */
    uint32_t  next;           /* free list: the next free slot, H_NO_SLOT at the end */
} HSlot;

struct RtHandleTable {
    pthread_mutex_t lock;
    const char     *name;               /* for reports */
    HSlot          *slots;              /* calloc'ed once, never moved */
    uint32_t        capacity, min_free, gen_lo, gen_hi;
    uint32_t        frontier;           /* slots below this index have been handed out at least once */
    uint32_t        live;               /* occupied slots */
    uint32_t        free_head, free_tail, free_count;   /* FIFO of freed slots */
};

RtHandleTable *rt_htab_create(const RtHandleTableConfig *cfg) {
    if (!cfg || !cfg->name) rt_fatal("rt_htab_create: no configuration or no table name");
    if (cfg->capacity < 1 || cfg->capacity > H_MAX_SLOTS)
        rt_fatal("rt_htab_create(%s): capacity %u is outside 1 to %u", cfg->name, cfg->capacity, H_MAX_SLOTS);
    if (cfg->gen_lo < 1 || cfg->gen_hi < cfg->gen_lo || cfg->gen_hi > H_GEN_MAX)
        rt_fatal("rt_htab_create(%s): generations %u..%u are not within 1..%u", cfg->name, cfg->gen_lo, cfg->gen_hi,
                 H_GEN_MAX);
    RtHandleTable *t = calloc(1, sizeof *t);
    HSlot *slots = calloc(cfg->capacity, sizeof *slots);
    if (!t || !slots) rt_fatal("rt_htab_create(%s): cannot allocate %u slots", cfg->name, cfg->capacity);
    if (pthread_mutex_init(&t->lock, NULL)) rt_fatal("rt_htab_create(%s): pthread_mutex_init failed", cfg->name);
    t->name = cfg->name;
    t->slots = slots;
    t->capacity = cfg->capacity;
    t->min_free = cfg->min_free;
    t->gen_lo = cfg->gen_lo;
    t->gen_hi = cfg->gen_hi;
    t->free_head = t->free_tail = H_NO_SLOT;
    return t;
}

void rt_htab_destroy(RtHandleTable *t) {
    if (!t) rt_fatal("rt_htab_destroy: NULL table");
    for (uint32_t i = 0; i < t->frontier; i++)
        if (t->slots[i].obj) rt_object_unref(t->slots[i].obj);
    pthread_mutex_destroy(&t->lock);
    free(t->slots);
    free(t);
}

static HANDLE make_handle(uint32_t gen, uint32_t index) {
    return (HANDLE)(uintptr_t)(((uint64_t)gen << H_GEN_SHIFT) | ((uint64_t)index << H_INDEX_SHIFT));
}

/* table lock held.  Slot for a new object, or H_NO_SLOT if the table is full.  A freed slot is
 * reused only once `min_free` slots are waiting (FIFO), or when there is nothing else, so the
 * generation of any one slot advances slowly. */
static uint32_t slot_take_locked(RtHandleTable *t) {
    uint32_t idx;
    if (t->free_count > 0 && (t->free_count >= t->min_free || t->frontier == t->capacity)) {
        idx = t->free_head;
        t->free_head = t->slots[idx].next;
        if (--t->free_count == 0) t->free_tail = H_NO_SLOT;
    } else if (t->frontier < t->capacity) {
        idx = t->frontier++;
        t->slots[idx].gen = t->gen_lo;
    } else {
        return H_NO_SLOT;
    }
    t->slots[idx].next = H_NO_SLOT;
    return idx;
}

/* table lock held.  Empty the slot and queue it for reuse with the next generation. */
static void slot_release_locked(RtHandleTable *t, uint32_t idx) {
    HSlot *s = &t->slots[idx];
    s->obj = NULL;
    s->gen = s->gen >= t->gen_hi ? t->gen_lo : s->gen + 1;
    s->next = H_NO_SLOT;
    if (t->free_tail == H_NO_SLOT) t->free_head = idx;
    else t->slots[t->free_tail].next = idx;
    t->free_tail = idx;
    t->free_count++;
    t->live--;
}

typedef enum { LK_OK = 0, LK_INDEX, LK_FREE, LK_STALE, LK_KIND, LK_NO_CLOSE } LookupResult;

static const char *lookup_why(LookupResult r) {
    switch (r) {
    case LK_INDEX:    return "index beyond the table (or a generation no table uses)";
    case LK_FREE:     return "free slot";
    case LK_STALE:    return "stale generation";
    case LK_KIND:     return "object of another kind";
    case LK_NO_CLOSE: return "kind that CloseHandle does not close";
    default:          return "ok";
    }
}

/* table lock held. */
static LookupResult slot_find_locked(RtHandleTable *t, uint64_t v, HSlot **out) {
    uint32_t lo = (uint32_t)v;                          /* only the low 32 bits count, the low two are ignored */
    uint32_t idx = (lo >> H_INDEX_SHIFT) & H_INDEX_MASK, gen = lo >> H_GEN_SHIFT;
    if (idx >= t->capacity) return LK_INDEX;
    HSlot *s = &t->slots[idx];
    if (!s->obj) return LK_FREE;
    if (s->gen != gen) return LK_STALE;                 /* also: a generation (0, 512..1023) no slot has */
    *out = s;
    return LK_OK;
}

static void log_bad_handle(const RtHandleTable *t, uint64_t v, uint32_t kind, LookupResult r) {
    if (RT_LOG_ON(RT_CH_FILE, RT_LL_DEBUG))
        rt_log(RT_CH_FILE, RT_LL_DEBUG, "%s handle %#llx rejected (wanted %s): %s", t->name,
               (unsigned long long)v, kind_name(kind), lookup_why(r));
}

HANDLE rt_htab_new(RtHandleTable *t, RtObject *obj) {
    if (!t) rt_fatal("rt_htab_new: NULL table");
    checked_prefix(obj, "rt_htab_new");
    if (__atomic_load_n(&obj->refs, __ATOMIC_RELAXED) == 0) rt_fatal("rt_htab_new: object %p (%s) has no reference to give to the table", (void *)obj,
                                 kind_name(obj->kind));
    mutex_lock(&t->lock);
    uint32_t idx = slot_take_locked(t);
    HANDLE h = NULL;
    if (idx != H_NO_SLOT) {
        t->slots[idx].obj = obj;                         /* the caller's reference is now the slot's (rule 1) */
        t->live++;
        h = make_handle(t->slots[idx].gen, idx);
    }
    mutex_unlock(&t->lock);
    if (h) return h;
    rt_log(RT_CH_FILE, RT_LL_WARN, "%s handle table is full (%u handles)", t->name, t->capacity);
    rt_thread_set_last_error(ERROR_NOT_ENOUGH_MEMORY);
    rt_object_unref(obj);                                /* "the reference is dropped" */
    return NULL;
}

RtObject *rt_htab_ref(RtHandleTable *t, HANDLE h, uint32_t kind) {
    if (!t) rt_fatal("rt_htab_ref: NULL table");
    uint64_t v = (uint64_t)(uintptr_t)h;
    HSlot *s = NULL;
    RtObject *obj = NULL;
    mutex_lock(&t->lock);
    LookupResult r = slot_find_locked(t, v, &s);
    if (r == LK_OK) {
        if (kind == RT_K_ANY || s->obj->kind == kind) {
            obj = s->obj;
            object_retain(obj, "rt_handle_ref");         /* rule 2: same critical section as the lookup */
        } else {
            r = LK_KIND;
        }
    }
    mutex_unlock(&t->lock);
    if (obj) return obj;
    log_bad_handle(t, v, kind, r);
    rt_thread_set_last_error(ERROR_INVALID_HANDLE);
    return NULL;
}

BOOL rt_htab_close(RtHandleTable *t, HANDLE h, uint32_t kind) {
    if (!t) rt_fatal("rt_htab_close: NULL table");
    uint64_t v = (uint64_t)(uintptr_t)h;
    HSlot *s = NULL;
    RtObject *obj = NULL;
    const RtObjectOps *ops = NULL;
    mutex_lock(&t->lock);
    LookupResult r = slot_find_locked(t, v, &s);
    if (r == LK_OK) {
        ops = s->obj->ops;
        if (kind != RT_K_ANY && s->obj->kind != kind) r = LK_KIND;
        /* A plain close (kind 0) must not close a kind that has its own close function
         * (find-file handles: FindClose).  Naming the kind is that close function. */
        else if (kind == RT_K_ANY && ops->no_close_handle) r = LK_NO_CLOSE;
        else {
            obj = s->obj;
            slot_release_locked(t, (uint32_t)(s - t->slots));    /* rule 3: the slot is emptied under the lock ... */
        }
    }
    mutex_unlock(&t->lock);
    if (obj) {
        rt_object_unref(obj);                            /* ... and its reference dropped after it */
        return TRUE;
    }
    if (r == LK_NO_CLOSE)
        rt_log(RT_CH_FILE, RT_LL_WARN, "CloseHandle(%#llx) on a %s handle fails: that kind has its own close function "
               "(FindClose for find-file handles)", (unsigned long long)v, ops->name);
    else
        log_bad_handle(t, v, kind, r);
    rt_thread_set_last_error(ERROR_INVALID_HANDLE);
    return FALSE;
}

uint32_t rt_htab_live(RtHandleTable *t) {
    if (!t) rt_fatal("rt_htab_live: NULL table");
    mutex_lock(&t->lock);
    uint32_t n = t->live;
    mutex_unlock(&t->lock);
    return n;
}

/* ------------------------------------------------------------------------------------------ */
/* The kernel handle table and the pseudo-handles                                             */
/* ------------------------------------------------------------------------------------------ */

static RtHandleTable *kernel_table;
static RtObject      *process_object;          /* set once by rt_handle_set_process_object */

static RtHandleTable *kernel(const char *fn) {
    if (!kernel_table) rt_fatal("%s before rt_handle_init", fn);
    return kernel_table;
}

void rt_handle_init(void) {
    if (kernel_table) rt_fatal("rt_handle_init called twice");
    RtHandleTableConfig cfg = {
        .name = "kernel", .capacity = H_MAX_SLOTS, .min_free = KERNEL_MIN_FREE,
        .gen_lo = KERNEL_GEN_LO, .gen_hi = KERNEL_GEN_HI,
    };
    kernel_table = rt_htab_create(&cfg);
}

/* Register the process object behind (HANDLE)-1 (rt/handle.h).  Call once, from the unit that
 * makes the process object (not built yet).  The table takes its own reference; the caller
 * keeps its own. */
void rt_handle_set_process_object(RtObject *process) {
    kernel("rt_handle_set_process_object");
    checked_prefix(process, "rt_handle_set_process_object");
    if (process->kind != RT_K_PROCESS)
        rt_fatal("rt_handle_set_process_object: object %p is of kind %s, not RT_K_PROCESS", (void *)process,
                 kind_name(process->kind));
    object_retain(process, "rt_handle_set_process_object");
    RtObject *expected = NULL;
    if (!__atomic_compare_exchange_n(&process_object, &expected, process, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        rt_fatal("rt_handle_set_process_object: a process object (%p) is already registered; there is one process",
                 (void *)expected);
}

static bool is_pseudo(uint64_t v) { return v == PSEUDO_PROCESS || v == PSEUDO_THREAD; }
static uint32_t pseudo_kind(uint64_t v) { return v == PSEUDO_PROCESS ? RT_K_PROCESS : RT_K_THREAD; }

/* The object behind a pseudo-handle, with one more reference, or fatal if its hook is not
 * filled yet.  The caller has checked that the kind allows it. */
static RtObject *pseudo_object(uint64_t v) {
    if (v == PSEUDO_PROCESS) {
        RtObject *p = __atomic_load_n(&process_object, __ATOMIC_ACQUIRE);
        if (!p)
            UNHANDLED("(HANDLE)-1, the current-process pseudo-handle, was used before any process object was "
                      "registered with rt_handle_set_process_object (the unit that makes the process object does "
                      "not exist yet)");
        object_retain(p, "rt_handle_ref");
        return p;
    }
    RtThread *self = rt_thread_self();
    if (!self)
        UNHANDLED("(HANDLE)-2, the current-thread pseudo-handle, was used on a host thread that is not a guest "
                  "thread");
    RtObject *o = self->sub[RT_SUB_HANDLE];
    if (!o)
        UNHANDLED("(HANDLE)-2, the current-thread pseudo-handle, was used on guest thread 0x%x, whose thread object "
                  "is not in RtThread.sub[RT_SUB_HANDLE] (the thread unit does not store it yet)", self->id);
    checked_prefix(o, "the thread object in RtThread.sub[RT_SUB_HANDLE]");
    if (o->kind != RT_K_THREAD)
        rt_fatal("RtThread.sub[RT_SUB_HANDLE] of guest thread 0x%x holds an object of kind %s, not RT_K_THREAD",
                 self->id, kind_name(o->kind));
    object_retain(o, "rt_handle_ref");
    return o;
}

HANDLE rt_handle_new(RtObject *obj) {
    return rt_htab_new(kernel("rt_handle_new"), obj);
}

RtObject *rt_handle_ref(HANDLE h, RtKind kind) {
    RtHandleTable *t = kernel("rt_handle_ref");
    checked_kind(kind, true, "rt_handle_ref");
    uint64_t v = (uint64_t)(uintptr_t)h;
    if (is_pseudo(v)) {
        if (kind != RT_K_ANY && kind != pseudo_kind(v)) {
            log_bad_handle(t, v, kind, LK_KIND);
            rt_thread_set_last_error(ERROR_INVALID_HANDLE);
            return NULL;
        }
        return pseudo_object(v);
    }
    return rt_htab_ref(t, h, kind);
}

BOOL rt_handle_close(HANDLE h) {
    RtHandleTable *t = kernel("rt_handle_close");
    if (is_pseudo((uint64_t)(uintptr_t)h)) return TRUE;            /* "has no effect" */
    return rt_htab_close(t, h, RT_K_ANY);
}

BOOL rt_handle_close_kind(HANDLE h, RtKind kind) {
    RtHandleTable *t = kernel("rt_handle_close_kind");
    checked_kind(kind, false, "rt_handle_close_kind");
    uint64_t v = (uint64_t)(uintptr_t)h;
    if (is_pseudo(v)) {
        /* A pseudo-handle is of the kind it stands for; closing it has no effect. */
        if (kind == pseudo_kind(v)) return TRUE;
        log_bad_handle(t, v, kind, LK_KIND);
        rt_thread_set_last_error(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    return rt_htab_close(t, h, kind);
}

HANDLE rt_handle_dup(HANDLE h) {
    RtHandleTable *t = kernel("rt_handle_dup");
    uint64_t v = (uint64_t)(uintptr_t)h;
    RtObject *obj = is_pseudo(v) ? pseudo_object(v) : rt_htab_ref(t, h, RT_K_ANY);
    if (!obj) return NULL;
    /* A kind CloseHandle cannot close (find-file state) is not a kernel object on Windows
     * either; DuplicateHandle of one fails.  UNSURE of the exact error, from reasoning, not
     * from a page: kernel32 hands out a pointer for these, which the kernel rejects as an
     * invalid handle. */
    if (obj->ops->no_close_handle) {
        rt_log(RT_CH_FILE, RT_LL_WARN, "DuplicateHandle(%#llx) of a %s handle fails: that kind has its own close "
               "function and is not a kernel handle", (unsigned long long)v, obj->ops->name);
        rt_object_unref(obj);
        rt_thread_set_last_error(ERROR_INVALID_HANDLE);
        return NULL;
    }
    return rt_htab_new(t, obj);                                    /* consumes the reference taken above */
}
