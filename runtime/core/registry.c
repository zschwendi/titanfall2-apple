/* The registry: an in-memory tree of keys and values kept in one text file
 * (docs/RUNTIME.md section 8.3).  Interface and specification: rt/registry.h, which also
 * documents the file format, the seed, and when the file is saved and why.
 *
 * LAYOUT OF THIS FILE
 *   1. types and the global state            4. the tree: keys, values, paths
 *   2. small tools: buffers, escapes, text   5. the seed
 *   3. logging of missing names              6. the file: writer, parser, load, save
 *   7. handles                               8. the functions of rt/registry.h
 *
 * LOCKING.  One mutex, G.lock, guards the whole tree and the file.  Every public function takes it
 * after it has converted its arguments (A to W conversion needs no registry state) and drops it
 * before it returns.  Inside the lock this file calls: the handle table (a leaf lock: it may
 * be called with any lock held, rt/handle.c), the log (its own lock; nothing it calls comes back
 * here), the locale tables (constant data), and rt_object_unref, whose destroy function
 * (reg_destroy) only drops an atomic reference on a Key and takes no lock.  Key nodes are reference
 * counted with atomics for that reason: a handle object holds a reference on its key, a key holds
 * one on its parent (so a deleted key's parent chain stays alive for as long as a handle needs
 * it), and the tree holds one on each child it links.  A key unlinked from the tree has its
 * `deleted` flag set while the lock is held; a thread that reaches it through a handle sees the
 * flag under the lock.
 *
 * WHERE THIS FILE IS NOT SURE WHAT WINDOWS DOES, the comment says UNSURE, and the unit's report
 * lists the same places.  A case that Windows accepts and this file does not model ends in
 * rt_unimplemented (the macro UNHANDLED) naming the Windows function and the offending value.
 */
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "core.h"
#include "rt/handle.h"
#include "rt/locale.h"
#include "rt/log.h"
#include "rt/machine.h"
#include "rt/registry.h"
#include "rt/thread.h"
#include "rt/time.h"
#include "rt/win/error.h"

/* ============================================================================================
 * 1. Types and the global state
 * ========================================================================================== */

#define FILE_NAME        "registry.txt"
#define FILE_HEADER      "titanfall-registry 1"
#define FILE_MAX_BYTES   (64u * 1024u * 1024u)    /* a registry file larger than this is not ours */
#define CLASS_MAX        255u                     /* characters of a key's class */

typedef struct Value {
    WCHAR   *name;          /* NUL-terminated copy, spelling kept */
    uint32_t nlen;
    uint32_t type;
    uint8_t *data;          /* NULL when size is 0 */
    uint32_t size;
    bool     seeded;        /* this (key, name) has a seed definition: it cannot be deleted */
    uint32_t stype, ssize;  /* the seed's type and data, to tell whether the guest changed it */
    uint8_t *sdata;
} Value;

typedef struct Key {
    _Atomic uint32_t refs;
    struct Key *parent;     /* a counted reference; NULL for the three roots */
    WCHAR      *name;       /* the spelling the key was created with */
    uint32_t    nlen;
    WCHAR      *cls;        /* class string, or NULL */
    uint32_t    clen;
    struct Key **kids;      /* sorted by folded name; the tree owns one reference on each */
    uint32_t    nkids, kcap;
    Value      *vals;       /* in creation order */
    uint32_t    nvals, vcap;
    uint64_t    mtime;      /* last write time, FILETIME units */
    bool        vol;        /* volatile: never written to the file */
    bool        deleted;    /* unlinked from the tree; only handles still reach it */
    bool        seeded;     /* part of the seed's structure: cannot be deleted */
    bool        defined;    /* a section of the file defined it (the parser's duplicate check) */
    bool        need;       /* scratch of the writer: this key or something below it is written */
    uint8_t     root;       /* 0 for an ordinary key; 1 + index into G.root for a root */
} Key;

enum { R_HKLM = 0, R_HKCU = 1, R_HKU = 2, R_COUNT = 3 };
static const char *const root_name[R_COUNT] = { "HKEY_LOCAL_MACHINE", "HKEY_CURRENT_USER", "HKEY_USERS" };

static struct {
    pthread_mutex_t lock;
    bool     ready;
    char    *path, *tmp_path;
    Key     *root[R_COUNT];
    Key     *classes;       /* HKLM\Software\Classes: what HKEY_CLASSES_ROOT is */
    Key     *config;        /* HKLM\System\CurrentControlSet\Hardware Profiles\Current */
    char    *saved;         /* what the file holds (or would hold) now, to skip a pointless write */
    size_t   saved_len;
} G = { .lock = PTHREAD_MUTEX_INITIALIZER };

/* The calling guest thread's Ctx, for the report of rt_unimplemented (NULL on a host thread that
 * is not a guest thread, which the report copes with). */
static Ctx *self_ctx(void) {
    RtThread *t = rt_thread_self();
    return t ? &t->ctx : NULL;
}
#define UNHANDLED(...)  rt_unimplemented(self_ctx(), __FILE__, __LINE__, __VA_ARGS__)

static void lock(void) {
    if (!G.ready) rt_fatal("the registry was used before rt_registry_init");
    pthread_mutex_lock(&G.lock);
}
static void unlock(void) { pthread_mutex_unlock(&G.lock); }

/* ============================================================================================
 * 2. Small tools
 * ========================================================================================== */

static void *xcalloc(size_t n, size_t sz) {
    void *p = calloc(n ? n : 1, sz ? sz : 1);
    if (!p) rt_fatal("registry: out of memory (%zu x %zu bytes)", n, sz);
    return p;
}
static void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n ? n : 1);
    if (!q) rt_fatal("registry: out of memory (%zu bytes)", n);
    return q;
}
static void *xdup(const void *p, size_t n) {
    void *q = xcalloc(n, 1);
    if (n) memcpy(q, p, n);
    return q;
}

static uint32_t wlen(const WCHAR *s) {
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

/* A NUL-terminated copy of n code units. */
static WCHAR *wdup(const WCHAR *s, uint32_t n) {
    WCHAR *q = xcalloc((size_t)n + 1, sizeof(WCHAR));
    if (n) memcpy(q, s, (size_t)n * sizeof(WCHAR));
    return q;
}

/* A growing byte buffer (text, or binary data). */
typedef struct { char *p; size_t n, cap; } Buf;

static void buf_reserve(Buf *b, size_t extra) {
    if (b->n + extra + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 256;
        while (cap < b->n + extra + 1) cap *= 2;
        b->p = xrealloc(b->p, cap);
        b->cap = cap;
    }
}
static void buf_put(Buf *b, const void *s, size_t n) {
    buf_reserve(b, n);
    if (n) memcpy(b->p + b->n, s, n);
    b->n += n;
    b->p[b->n] = 0;
}
static void buf_puts(Buf *b, const char *s) { buf_put(b, s, strlen(s)); }
static void buf_putc(Buf *b, char c) { buf_put(b, &c, 1); }
static void buf_printf(Buf *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void buf_printf(Buf *b, const char *fmt, ...) {
    char tmp[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= sizeof tmp) rt_fatal("registry: internal: formatted text too long");
    buf_put(b, tmp, (size_t)n);
}

/* A growing vector of UTF-16 code units. */
typedef struct { WCHAR *p; uint32_t n, cap; } WVec;

static void wvec_put(WVec *v, WCHAR c) {
    if (v->n + 2 > v->cap) {
        v->cap = v->cap ? v->cap * 2 : 64;
        v->p = xrealloc(v->p, (size_t)v->cap * sizeof(WCHAR));
    }
    v->p[v->n++] = c;
    v->p[v->n] = 0;
}

static const char hexd[] = "0123456789abcdef";

/* Text for logs, reports and the file: the code units s[0..n) as printable ASCII.  A backslash
 * and a double quote are escaped with a backslash (mode 0, the body of a quoted string); a
 * percent sign and a closing bracket with %u (mode 1, a component of a key path); every code
 * unit outside 0x20..0x7E is \uXXXX (mode 0) or %uXXXX (mode 1).  With `fold` each unit is
 * upper-cased first, which makes the text a key for "the same name in any spelling". */
static void put_units(Buf *b, const WCHAR *s, uint32_t n, int mode, bool fold) {
    char esc = mode ? '%' : '\\';
    for (uint32_t i = 0; i < n; i++) {
        WCHAR c = fold ? rt_wchar_upper(s[i]) : s[i];
        if (mode == 0 && (c == '\\' || c == '"')) { buf_putc(b, '\\'); buf_putc(b, (char)c); }
        else if (c >= 0x20 && c <= 0x7E && !(mode == 1 && (c == '%' || c == ']'))) buf_putc(b, (char)c);
        else {
            char t[6] = { esc, 'u', hexd[(c >> 12) & 15], hexd[(c >> 8) & 15], hexd[(c >> 4) & 15], hexd[c & 15] };
            buf_put(b, t, 6);
        }
    }
}

/* A short rendering of a counted string for a report: escaped, cut at 80 characters. */
static const char *show(const WCHAR *s, uint32_t n) {
    static _Thread_local char ring[4][400];
    static _Thread_local unsigned next;
    char *out = ring[next++ & 3];
    Buf b = { 0 };
    put_units(&b, s, n > 60 ? 60 : n, 0, false);
    if (n > 60) buf_puts(&b, "...");
    snprintf(out, sizeof ring[0], "\"%s\"", b.p ? b.p : "");
    free(b.p);
    return out;
}

/* ASCII to UTF-16, for the seed. */
static WCHAR *ascii_w(const char *s, uint32_t *n) {
    uint32_t len = (uint32_t)strlen(s);
    WCHAR *w = xcalloc((size_t)len + 1, sizeof(WCHAR));
    for (uint32_t i = 0; i < len; i++) w[i] = (unsigned char)s[i];
    if (n) *n = len;
    return w;
}

static uint64_t now100(void) { return rt_time_utc_100ns(); }

static const WCHAR empty_w[1] = { 0 };

/* Case-insensitive comparison of two counted names: code unit by code unit through the upper-case
 * table of rt/locale.h (Windows folds names with its upper-case table), then by length.  Names
 * entering the registry have all been checked with check_case, so this never meets a unit whose
 * case the table does not cover. */
static int name_cmp(const WCHAR *a, uint32_t an, const WCHAR *b, uint32_t bn) {
    uint32_t n = an < bn ? an : bn;
    for (uint32_t i = 0; i < n; i++) {
        if (a[i] == b[i]) continue;
        WCHAR x = rt_wchar_upper(a[i]), y = rt_wchar_upper(b[i]);
        if (x != y) return x < y ? -1 : 1;
    }
    return an == bn ? 0 : (an < bn ? -1 : 1);
}

/* A name with a code unit whose case the locale core does not cover is not compared: the
 * answer would be a guess.  Fatal, naming the name. */
static void check_case(const WCHAR *s, uint32_t n, const char *api, const char *what) {
    for (uint32_t i = 0; i < n; i++)
        if (!rt_wchar_case_known(s[i]))
            UNHANDLED("%s: %s %s has the character U+%04X, whose upper case is not in the locale tables, "
                      "so names cannot be compared", api, what, show(s, n), s[i]);
}

/* ============================================================================================
 * 3. Logging of missing names
 * ========================================================================================== */

/* The path of a key as text.  `fold` upper-cases it (the dedupe key). */
static void path_append(Buf *b, const Key *k, bool fold) {
    uint32_t depth = 0;
    for (const Key *p = k; p; p = p->parent) depth++;
    const Key **chain = xcalloc(depth, sizeof *chain);
    uint32_t i = depth;
    for (const Key *p = k; p; p = p->parent) chain[--i] = p;
    for (i = 0; i < depth; i++) {
        if (i) buf_putc(b, '\\');
        if (chain[i]->root) buf_puts(b, root_name[chain[i]->root - 1]);
        else put_units(b, chain[i]->name, chain[i]->nlen, 0, fold);
    }
    free(chain);
}

/* A subkey string as text: the backslashes between names stay single, the names are escaped. */
static void put_subkey_text(Buf *b, const WCHAR *path, bool fold) {
    uint32_t n = wlen(path), start = 0;
    for (uint32_t i = 0; i <= n; i++) {
        if (i < n && path[i] != '\\') continue;
        put_units(b, path + start, i - start, 0, fold);
        if (i < n) buf_putc(b, '\\');
        start = i + 1;
    }
}

/* A subkey string for a report: quoted, cut at 80 characters, the separators as they are. */
static const char *show_path(const WCHAR *path) {
    static _Thread_local char ring[4][400];
    static _Thread_local unsigned next;
    char *out = ring[next++ & 3];
    uint32_t n = wlen(path);
    Buf b = { 0 };
    WCHAR cut[61];
    bool cutting = n > 60;
    if (cutting) { memcpy(cut, path, 60 * sizeof(WCHAR)); cut[60] = 0; put_subkey_text(&b, cut, false); buf_puts(&b, "..."); }
    else put_subkey_text(&b, path, false);
    snprintf(out, sizeof ring[0], "\"%s\"", b.p ? b.p : "");
    free(b.p);
    return out;
}

/* "<op>: key not found: <path>" once per distinct path, whatever the spelling.  `sub` is what
 * the guest asked for below `base`. */
static void log_missing_key(const char *op, const Key *base, const WCHAR *sub) {
    if (!RT_LOG_ON(RT_CH_REGISTRY, RT_LL_WARN)) return;
    Buf id = { 0 }, shown = { 0 };
    buf_puts(&id, "K ");
    path_append(&id, base, true);
    path_append(&shown, base, false);
    if (sub && *sub) {
        buf_putc(&id, '\\');    put_subkey_text(&id, sub, true);
        buf_putc(&shown, '\\'); put_subkey_text(&shown, sub, false);
    }
    rt_log_once(RT_CH_REGISTRY, RT_LL_WARN, id.p, "%s: key not found: %s", op, shown.p);
    free(id.p);
    free(shown.p);
}

/* "<op>: value not found: <path> value <name>" once per distinct (key, value name). */
static void log_missing_value(const char *op, const Key *k, const WCHAR *name, uint32_t nlen) {
    if (!RT_LOG_ON(RT_CH_REGISTRY, RT_LL_WARN)) return;
    Buf id = { 0 }, shown = { 0 };
    buf_puts(&id, "V ");
    path_append(&id, k, true);
    buf_putc(&id, '\n');
    put_units(&id, name, nlen, 0, true);
    path_append(&shown, k, false);
    buf_puts(&shown, " value ");
    if (nlen) { buf_putc(&shown, '"'); put_units(&shown, name, nlen, 0, false); buf_putc(&shown, '"'); }
    else buf_puts(&shown, "(Default)");
    rt_log_once(RT_CH_REGISTRY, RT_LL_WARN, id.p, "%s: value not found: %s", op, shown.p);
    free(id.p);
    free(shown.p);
}

/* ============================================================================================
 * 4. The tree
 * ========================================================================================== */

static void key_ref(Key *k) { atomic_fetch_add(&k->refs, 1); }

static void value_free(Value *v) {
    free(v->name);
    free(v->data);
    free(v->sdata);
}

static void key_unref(Key *k) {
    while (k && atomic_fetch_sub(&k->refs, 1) == 1) {
        Key *parent = k->parent;
        for (uint32_t i = 0; i < k->nvals; i++) value_free(&k->vals[i]);
        free(k->vals);
        free(k->kids);          /* empty: a child holds a reference on its parent */
        free(k->name);
        free(k->cls);
        free(k);
        k = parent;             /* the reference this key held on its parent */
    }
}

static Key *key_new(Key *parent, const WCHAR *name, uint32_t nlen) {
    Key *k = xcalloc(1, sizeof *k);
    atomic_init(&k->refs, 1);
    k->parent = parent;
    if (parent) key_ref(parent);
    k->name = wdup(name, nlen);
    k->nlen = nlen;
    k->mtime = now100();
    return k;
}

/* Binary search of the sorted subkeys: the index of the name, or where it would go. */
static uint32_t kid_search(const Key *k, const WCHAR *n, uint32_t nl, bool *found) {
    uint32_t lo = 0, hi = k->nkids;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        int c = name_cmp(k->kids[mid]->name, k->kids[mid]->nlen, n, nl);
        if (c == 0) { *found = true; return mid; }
        if (c < 0) lo = mid + 1; else hi = mid;
    }
    *found = false;
    return lo;
}

static Key *kid_find(const Key *k, const WCHAR *n, uint32_t nl) {
    bool f;
    uint32_t i = kid_search(k, n, nl, &f);
    return f ? k->kids[i] : NULL;
}

/* A new child at its sorted place; the tree's reference on it is the one key_new made. */
static Key *kid_add(Key *k, const WCHAR *n, uint32_t nl, bool vol) {
    bool f;
    uint32_t at = kid_search(k, n, nl, &f);
    if (f) rt_fatal("registry: internal: kid_add of a name that exists");
    Key *c = key_new(k, n, nl);
    c->vol = vol;
    if (k->nkids == k->kcap) {
        k->kcap = k->kcap ? k->kcap * 2 : 4;
        k->kids = xrealloc(k->kids, (size_t)k->kcap * sizeof *k->kids);
    }
    memmove(&k->kids[at + 1], &k->kids[at], (size_t)(k->nkids - at) * sizeof *k->kids);
    k->kids[at] = c;
    k->nkids++;
    return c;
}

/* Unlink a child (by index) and mark it deleted; the tree's reference is dropped. */
static void kid_remove(Key *k, uint32_t at) {
    Key *c = k->kids[at];
    memmove(&k->kids[at], &k->kids[at + 1], (size_t)(k->nkids - at - 1) * sizeof *k->kids);
    k->nkids--;
    c->deleted = true;
    key_unref(c);
}

/* Discard a whole tree (the test hook): every key below `k` is marked deleted so that open
 * handles see ERROR_KEY_DELETED, and the tree's references are dropped. */
static void tree_discard(Key *k) {
    k->deleted = true;
    for (uint32_t i = 0; i < k->nkids; i++) { tree_discard(k->kids[i]); key_unref(k->kids[i]); }
    k->nkids = 0;
}

static int val_find(const Key *k, const WCHAR *n, uint32_t nl) {
    for (uint32_t i = 0; i < k->nvals; i++)
        if (name_cmp(k->vals[i].name, k->vals[i].nlen, n, nl) == 0) return (int)i;
    return -1;
}

static Value *val_append(Key *k, const WCHAR *n, uint32_t nl) {
    if (k->nvals == k->vcap) {
        k->vcap = k->vcap ? k->vcap * 2 : 4;
        k->vals = xrealloc(k->vals, (size_t)k->vcap * sizeof *k->vals);
    }
    Value *v = &k->vals[k->nvals++];
    memset(v, 0, sizeof *v);
    v->name = wdup(n, nl);
    v->nlen = nl;
    return v;
}

static void val_assign(Value *v, uint32_t type, const void *data, uint32_t size) {
    free(v->data);
    v->data = size ? xdup(data, size) : NULL;
    v->size = size;
    v->type = type;
}

static void val_remove(Key *k, uint32_t at) {
    value_free(&k->vals[at]);
    memmove(&k->vals[at], &k->vals[at + 1], (size_t)(k->nvals - at - 1) * sizeof *k->vals);
    k->nvals--;
}

/* A seeded value that still has exactly the seed's data is not written to the file. */
static bool val_pristine(const Value *v) {
    return v->seeded && v->type == v->stype && v->size == v->ssize &&
           (v->size == 0 || memcmp(v->data, v->sdata, v->size) == 0);
}

static bool is_string_type(uint32_t t) { return t == REG_SZ || t == REG_EXPAND_SZ || t == REG_MULTI_SZ; }

/* HKEY_LOCAL_MACHINE and HKEY_USERS: no values, no new direct subkeys (they are not stored keys
 * on Windows).  UNSURE that ERROR_ACCESS_DENIED is what Windows 7 says. */
static bool restricted_root(const Key *k) { return k->root == 1 + R_HKLM || k->root == 1 + R_HKU; }

/* ---- paths ------------------------------------------------------------------------------ */

typedef struct {
    Key      *key;
    DWORD     access;
    RtObject *obj;              /* the reference taken on a table handle; NULL for a predefined key */
    bool      predef;
} KeyRef;

typedef struct { const WCHAR *s; uint32_t n; } Comp;

/* True for the key that HKEY_CLASSES_ROOT is (G.classes) and for every key below it. */
static bool in_classes(const Key *k) {
    for (; k; k = k->parent) if (k == G.classes) return true;
    return false;
}

/* Split a subkey string into names, relative to the handle `base`.  NULL and "" are no names.
 * Everything else must be non-empty names joined by single backslashes; the other shapes are
 * fatal (rt/registry.h, NAMES).  The exception is an open or create (`open_or_create`), where
 * trailing backslashes are ignored and a leading one is ERROR_BAD_PATHNAME, or skipped under the
 * predefined HKEY_CLASSES_ROOT (NAMES).  The status is ERROR_SUCCESS, or what the call returns
 * when it fails: nothing is split then.  The result is malloc'd (NULL for none). */
static LSTATUS split_path(const KeyRef *base, const WCHAR *path, const char *api, bool open_or_create,
                          Comp **out, uint32_t *count) {
    *out = NULL;
    *count = 0;
    if (!path || !path[0]) return ERROR_SUCCESS;
    uint32_t total = wlen(path);
    if (total > RT_REG_SUBKEY_MAX)
        UNHANDLED("%s: a subkey string of %u characters (the limit is %u)", api, total, RT_REG_SUBKEY_MAX);
    uint32_t start = 0;
    if (open_or_create) {
        if (path[0] == '\\') {
            if (base->predef && base->key == G.classes) start = 1;
            else if (in_classes(base->key))
                UNHANDLED("%s: the subkey string %s starts with a backslash, below a handle to a key of "
                          "HKEY_CLASSES_ROOT; what Windows returns for that is not established", api, show_path(path));
            else return ERROR_BAD_PATHNAME;
        }
        while (total > start && path[total - 1] == '\\') total--;
        if (start && total == start)
            UNHANDLED("%s: the subkey string %s is backslashes only under HKEY_CLASSES_ROOT; what Windows "
                      "returns for that is not established", api, show_path(path));
    }
    uint32_t n = 1;
    for (uint32_t i = start; i < total; i++) if (path[i] == '\\') n++;
    Comp *c = xcalloc(n, sizeof *c);
    uint32_t k = 0, from = start;
    for (uint32_t i = start; i <= total; i++) {
        if (i < total && path[i] != '\\') continue;
        if (i == from) {
            free(c);
            UNHANDLED("%s: the subkey string %s has an empty name (%s); what Windows returns for that is not established",
                      api, show_path(path), i == 0 ? "a leading backslash" : i == total ? "a trailing backslash" : "two backslashes together");
        }
        if (i - from > RT_REG_KEY_NAME_MAX) {
            free(c);
            UNHANDLED("%s: the key name %s is %u characters (the limit is %u)", api, show(path + from, i - from),
                      i - from, RT_REG_KEY_NAME_MAX);
        }
        check_case(path + from, i - from, api, "key name");
        c[k].s = path + from;
        c[k].n = i - from;
        k++;
        from = i + 1;
    }
    *out = c;
    *count = k;
    return ERROR_SUCCESS;
}

/* The access mask a new handle gets (rt/registry.h, ACCESS). */
static DWORD map_access(DWORD sam, const char *api) {
    const DWORD known = KEY_ALL_ACCESS | KEY_WOW64_RES | MAXIMUM_ALLOWED | ACCESS_SYSTEM_SECURITY |
                        GENERIC_ALL | GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE;
    if (sam & ~known)
        UNHANDLED("%s: samDesired %#x has bits (%#x) that are not registry access rights", api, sam, sam & ~known);
    if (sam & ACCESS_SYSTEM_SECURITY)
        UNHANDLED("%s: samDesired %#x asks for ACCESS_SYSTEM_SECURITY, which needs a privilege that is not modelled", api, sam);
    DWORD a = sam & ~(GENERIC_ALL | GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE | MAXIMUM_ALLOWED);
    if (sam & (GENERIC_READ | GENERIC_EXECUTE)) a |= KEY_READ;
    if (sam & GENERIC_WRITE) a |= KEY_WRITE;
    if (sam & (GENERIC_ALL | MAXIMUM_ALLOWED)) a |= KEY_ALL_ACCESS;
    return a;
}

/* ============================================================================================
 * 5. The seed
 * ========================================================================================== */

/* The seeded key `path` ("A\B\C", ASCII) below root `r`, created as needed.  Every key on the path
 * is part of the seed's structure and cannot be deleted. */
static Key *seed_key(Key *r, const char *path) {
    Key *cur = r;
    const char *p = path;
    while (*p) {
        const char *e = strchr(p, '\\');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        char tmp[128];
        if (n >= sizeof tmp) rt_fatal("registry: internal: seed name too long");
        memcpy(tmp, p, n);
        tmp[n] = 0;
        uint32_t wn;
        WCHAR *w = ascii_w(tmp, &wn);
        Key *kid = kid_find(cur, w, wn);
        if (!kid) kid = kid_add(cur, w, wn, false);
        free(w);
        kid->seeded = true;
        cur = kid;
        p += n + (e ? 1 : 0);
    }
    return cur;
}

/* A seeded value: set if the file did not define it, kept (and protected from deletion) if it did. */
static void seed_value(Key *k, const char *name, uint32_t type, const void *data, uint32_t size) {
    uint32_t wn;
    WCHAR *w = ascii_w(name, &wn);
    int i = val_find(k, w, wn);
    Value *v = i >= 0 ? &k->vals[i] : NULL;
    if (!v) {
        v = val_append(k, w, wn);
        val_assign(v, type, data, size);
    }
    free(w);
    v->seeded = true;
    v->stype = type;
    v->ssize = size;
    free(v->sdata);
    v->sdata = size ? xdup(data, size) : NULL;
}

static void seed_dword(Key *k, const char *name, uint32_t x) {
    uint8_t b[4] = { (uint8_t)x, (uint8_t)(x >> 8), (uint8_t)(x >> 16), (uint8_t)(x >> 24) };
    seed_value(k, name, REG_DWORD, b, 4);
}

/* REG_SZ with its terminator, as Windows stores it. */
static void seed_sz(Key *k, const char *name, const char *text) {
    uint32_t n;
    WCHAR *w = ascii_w(text, &n);
    seed_value(k, name, REG_SZ, w, (n + 1) * (uint32_t)sizeof(WCHAR));
    free(w);
}

static void cpuid_leaf(uint32_t leaf, uint32_t out[4]) {
    Ctx c;
    memset(&c, 0, sizeof c);
    c.r[0] = leaf;
    rt_cpuid(&c);
    out[0] = (uint32_t)c.r[0];      /* eax */
    out[1] = (uint32_t)c.r[3];      /* ebx */
    out[2] = (uint32_t)c.r[1];      /* ecx */
    out[3] = (uint32_t)c.r[2];      /* edx */
}

/* The processor description: from cpuid, which is what the guest's own cpuid instruction sees. */
static void seed_processors(Key *hklm) {
    const RtMachine *m = rt_machine();
    uint32_t l0[4], l1[4];
    cpuid_leaf(0, l0);
    cpuid_leaf(1, l1);
    char vendor[13];
    memcpy(vendor, &l0[1], 4);          /* ebx, edx, ecx */
    memcpy(vendor + 4, &l0[3], 4);
    memcpy(vendor + 8, &l0[2], 4);
    vendor[12] = 0;

    uint32_t eax = l1[0];
    uint32_t family = (eax >> 8) & 0xF, model = (eax >> 4) & 0xF, stepping = eax & 0xF;
    if (family == 0xF) family += (eax >> 20) & 0xFF;
    if (((eax >> 8) & 0xF) == 6 || ((eax >> 8) & 0xF) == 0xF) model |= ((eax >> 16) & 0xF) << 4;

    /* Windows writes "Intel64 Family 6 Model 13 Stepping 8" on an Intel x64 machine and "AMD64 ..." on
     * an AMD one.  Any other vendor is not described: stop rather than make up a prefix. */
    const char *prefix = !strcmp(vendor, "GenuineIntel") ? "Intel64" : !strcmp(vendor, "AuthenticAMD") ? "AMD64" : NULL;
    if (!prefix) rt_fatal("registry seed: the processor vendor '%s' has no Windows identifier format", vendor);
    char ident[96];
    snprintf(ident, sizeof ident, "%s Family %u Model %u Stepping %u", prefix, family, model, stepping);

    /* The brand string, if cpuid has one (leaves 0x80000002 to 0x80000004; Windows trims leading blanks). */
    char brand[49];
    memset(brand, 0, sizeof brand);
    for (uint32_t i = 0; i < 3; i++) {
        uint32_t r[4];
        cpuid_leaf(0x80000002u + i, r);
        memcpy(brand + 16 * i, r, 16);
    }
    const char *b = brand;
    while (*b == ' ') b++;

    for (uint32_t n = 0; n < m->cpu_count; n++) {
        char path[64];
        snprintf(path, sizeof path, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\%u", n);
        Key *k = seed_key(hklm, path);
        seed_dword(k, "~MHz", (uint32_t)(m->cpu_hz / 1000000u));
        seed_sz(k, "Identifier", ident);
        seed_sz(k, "VendorIdentifier", vendor);
        if (*b) seed_sz(k, "ProcessorNameString", b);
    }
}

static void seed_os(Key *hklm) {
    const RtMachine *m = rt_machine();
    char text[32];

    Key *k = seed_key(hklm, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion");
    snprintf(text, sizeof text, "%u.%u", m->os_major, m->os_minor);
    seed_sz(k, "CurrentVersion", text);
    snprintf(text, sizeof text, "%u", m->os_build);
    seed_sz(k, "CurrentBuild", text);
    seed_sz(k, "CurrentBuildNumber", text);
    if (m->sp_name && m->sp_name[0]) seed_sz(k, "CSDVersion", m->sp_name);

    k = seed_key(hklm, "SYSTEM\\CurrentControlSet\\Control\\Windows");
    seed_dword(k, "CSDVersion", ((uint32_t)m->sp_major << 8) | m->sp_minor);

    /* The machine is a workstation (RtMachine: "Windows 7 SP1 x64, workstation"). */
    k = seed_key(hklm, "SYSTEM\\CurrentControlSet\\Control\\ProductOptions");
    seed_sz(k, "ProductType", "WinNT");
}

static void seed_tree(void) {
    Key *hklm = G.root[R_HKLM];
    seed_processors(hklm);
    seed_os(hklm);
    G.classes = seed_key(hklm, "SOFTWARE\\Classes");
    G.config = seed_key(hklm, "SYSTEM\\CurrentControlSet\\Hardware Profiles\\Current");
}

/* ============================================================================================
 * 6. The file
 * ========================================================================================== */

/* ---- writer ------------------------------------------------------------------------------- */

/* Does this key, or anything below it, go into the file?  Sets k->need on the way. */
static bool mark_need(Key *k) {
    bool need = false;
    if (!k->vol) {
        need = !k->seeded && !k->root;
        for (uint32_t i = 0; i < k->nvals && !need; i++) if (!val_pristine(&k->vals[i])) need = true;
        for (uint32_t i = 0; i < k->nkids; i++) if (mark_need(k->kids[i])) need = true;
    }
    k->need = need;
    return need;
}

/* Could `data` of a REG_MULTI_SZ be written as multi_sz:"a","b"?  (Strings, each non-empty, each
 * ended by a NUL, then one more NUL.) */
static bool multi_sz_text_form(const uint8_t *d, uint32_t size) {
    if (size < 2 || size % 2) return false;
    uint32_t m = size / 2;
    WCHAR first;       /* the host is little-endian, as the stored data is: units are read from memory as they are */
    memcpy(&first, d, 2);
    WCHAR last;
    memcpy(&last, d + size - 2, 2);
    if (last != 0) return false;
    if (m == 1) return true;
    WCHAR prev_last;
    memcpy(&prev_last, d + size - 4, 2);
    if (prev_last != 0 || first == 0) return false;
    for (uint32_t i = 1; i + 1 < m; i++) {      /* no empty string in the middle: no two NULs in a row */
        WCHAR a, b;
        memcpy(&a, d + 2 * (size_t)i, 2);
        memcpy(&b, d + 2 * (size_t)(i - 1), 2);
        if (a == 0 && b == 0) return false;
    }
    return true;
}

static void put_value_line(Buf *b, const Value *v) {
    if (v->nlen == 0) buf_puts(b, "@ = ");
    else { buf_putc(b, '"'); put_units(b, v->name, v->nlen, 0, false); buf_puts(b, "\" = "); }

    const uint8_t *d = v->data;
    uint32_t size = v->size;
    WCHAR last = 0;
    if (size >= 2) memcpy(&last, d + size - 2, 2);
    if ((v->type == REG_SZ || v->type == REG_EXPAND_SZ) && size >= 2 && size % 2 == 0 && last == 0) {
        WCHAR *u = xcalloc(size / 2, sizeof(WCHAR));
        memcpy(u, d, size);
        buf_puts(b, v->type == REG_SZ ? "sz:\"" : "expand_sz:\"");
        put_units(b, u, size / 2 - 1, 0, false);
        buf_putc(b, '"');
        free(u);
    } else if (v->type == REG_MULTI_SZ && multi_sz_text_form(d, size)) {
        WCHAR *u = xcalloc(size / 2, sizeof(WCHAR));
        memcpy(u, d, size);
        uint32_t m = size / 2;
        buf_puts(b, "multi_sz:");
        uint32_t start = 0;
        bool first = true;
        for (uint32_t i = 0; i + 1 < m; i++) {
            if (u[i] != 0) continue;
            buf_puts(b, first ? "\"" : ",\"");
            put_units(b, u + start, i - start, 0, false);
            buf_putc(b, '"');
            first = false;
            start = i + 1;
        }
        free(u);
    } else if (v->type == REG_DWORD && size == 4) {
        uint32_t x = (uint32_t)d[0] | (uint32_t)d[1] << 8 | (uint32_t)d[2] << 16 | (uint32_t)d[3] << 24;
        buf_printf(b, "dword:%08x", x);
    } else if (v->type == REG_QWORD && size == 8) {
        uint64_t x = 0;
        for (int i = 7; i >= 0; i--) x = x << 8 | d[i];
        buf_printf(b, "qword:%016llx", (unsigned long long)x);
    } else {
        if (v->type == REG_BINARY) buf_puts(b, "hex:");
        else buf_printf(b, "hex(%u):", v->type);
        buf_reserve(b, (size_t)size * 3);
        for (uint32_t i = 0; i < size; i++) {
            if (i) buf_putc(b, ',');
            buf_putc(b, hexd[d[i] >> 4]);
            buf_putc(b, hexd[d[i] & 15]);
        }
    }
    buf_putc(b, '\n');
}

static void emit_key(Buf *out, Buf *path, Key *k) {
    size_t mark = path->n;
    if (k->root) buf_puts(path, root_name[k->root - 1]);
    else { buf_putc(path, '\\'); put_units(path, k->name, k->nlen, 1, false); }

    bool own_values = false;
    for (uint32_t i = 0; i < k->nvals; i++) if (!val_pristine(&k->vals[i])) own_values = true;
    if (k->need && (!k->root || own_values)) {
        buf_putc(out, '\n');
        buf_putc(out, '[');
        buf_put(out, path->p, path->n);
        buf_putc(out, ']');
        if (!k->root) buf_printf(out, " time=%016llX", (unsigned long long)k->mtime);
        if (k->cls) { buf_puts(out, " class=\""); put_units(out, k->cls, k->clen, 0, false); buf_putc(out, '"'); }
        buf_putc(out, '\n');
        for (uint32_t i = 0; i < k->nvals; i++) if (!val_pristine(&k->vals[i])) put_value_line(out, &k->vals[i]);
    }
    for (uint32_t i = 0; i < k->nkids; i++) if (k->kids[i]->need) emit_key(out, path, k->kids[i]);
    path->n = mark;
    path->p[mark] = 0;
}

/* The text of the file for the tree as it is now. */
static void serialize(Buf *out) {
    buf_puts(out, "# Registry of the Titanfall recompilation runtime (runtime/include/rt/registry.h describes the format).\n"
                  "# The runtime rewrites this file whole after every change; comments and layout are not kept.\n");
    buf_puts(out, FILE_HEADER "\n");
    Buf path = { 0 };
    buf_put(&path, "", 0);
    for (int r = 0; r < R_COUNT; r++) {
        mark_need(G.root[r]);
        path.n = 0;
        path.p[0] = 0;
        emit_key(out, &path, G.root[r]);
    }
    free(path.p);
}

/* Write `text` to the temporary name, force it to the disk, rename it over the real file.  A power
 * cut leaves the old file or the new one, never part of either. */
static void write_atomic(const char *text, size_t n) {
    int fd = open(G.tmp_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) rt_fatal("registry: cannot save to %s: open %s: %s", G.path, G.tmp_path, strerror(errno));
    size_t done = 0;
    while (done < n) {
        ssize_t w = write(fd, text + done, n - done);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) {
            int e = w < 0 ? errno : EIO;
            close(fd);
            rt_fatal("registry: cannot save to %s: write %s: %s", G.path, G.tmp_path, strerror(e));
        }
        done += (size_t)w;
    }
    /* macOS: fsync does not ask the drive to flush its cache; F_FULLFSYNC does.  Without it the
     * rename below could outlive the data after a power cut. */
    if (fcntl(fd, F_FULLFSYNC) != 0 && fsync(fd) != 0) {
        int e = errno;
        close(fd);
        rt_fatal("registry: cannot save to %s: flush %s: %s", G.path, G.tmp_path, strerror(e));
    }
    if (close(fd) != 0) rt_fatal("registry: cannot save to %s: close %s: %s", G.path, G.tmp_path, strerror(errno));
    if (rename(G.tmp_path, G.path) != 0)
        rt_fatal("registry: cannot save to %s: rename %s: %s", G.path, G.tmp_path, strerror(errno));
}

/* Save if the text of the file would change.  Called with the lock held after every change. */
static void persist(void) {
    Buf b = { 0 };
    serialize(&b);
    if (G.saved && G.saved_len == b.n && memcmp(G.saved, b.p, b.n) == 0) { free(b.p); return; }
    write_atomic(b.p, b.n);
    free(G.saved);
    G.saved = b.p;
    G.saved_len = b.n;
}

/* ---- parser ------------------------------------------------------------------------------- */

typedef struct {
    const char *file;
    const char *end;            /* end of the current line (a CR before the LF is already cut) */
    unsigned    line;
} Parser;

static _Noreturn void pfail(const Parser *ps, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static _Noreturn void pfail(const Parser *ps, const char *fmt, ...) {
    char msg[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    rt_fatal("registry file %s, line %u: %s", ps->file, ps->line, msg);
}

static int hexv(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* One code point of well-formed UTF-8: its length, or 0 (overlong, surrogate, above U+10FFFF, truncated). */
static size_t utf8_get(const unsigned char *s, const unsigned char *e, uint32_t *cp) {
    if (s >= e) return 0;
    unsigned c = s[0];
    size_t n;
    uint32_t v, min;
    if (c < 0x80) { *cp = c; return 1; }
    else if (c >= 0xC2 && c <= 0xDF) { n = 2; v = c & 0x1F; min = 0x80; }
    else if (c >= 0xE0 && c <= 0xEF) { n = 3; v = c & 0x0F; min = 0x800; }
    else if (c >= 0xF0 && c <= 0xF4) { n = 4; v = c & 0x07; min = 0x10000; }
    else return 0;
    if ((size_t)(e - s) < n) return 0;
    for (size_t i = 1; i < n; i++) {
        if ((s[i] & 0xC0) != 0x80) return 0;
        v = v << 6 | (s[i] & 0x3F);
    }
    if (v < min || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) return 0;
    *cp = v;
    return n;
}

static void put_cp(WVec *v, uint32_t cp) {
    if (cp >= 0x10000) {
        cp -= 0x10000;
        wvec_put(v, (WCHAR)(0xD800 + (cp >> 10)));
        wvec_put(v, (WCHAR)(0xDC00 + (cp & 0x3FF)));
    } else wvec_put(v, (WCHAR)cp);
}

static void skip_blanks(const char **p, const char *e) {
    while (*p < e && (**p == ' ' || **p == '\t')) (*p)++;
}

/* Four hex digits after an escape introducer already consumed. */
static WCHAR parse_u4(Parser *ps, const char **p, const char *e) {
    unsigned x = 0;
    for (int i = 0; i < 4; i++) {
        int h = *p < e ? hexv((unsigned char)**p) : -1;
        if (h < 0) pfail(ps, "an escape needs four hex digits after \\u or %%u");
        x = x << 4 | (unsigned)h;
        (*p)++;
    }
    return (WCHAR)x;
}

/* A raw non-ASCII character in the file, as UTF-16. */
static void parse_raw(Parser *ps, const char **p, const char *e, WVec *out) {
    uint32_t cp;
    size_t n = utf8_get((const unsigned char *)*p, (const unsigned char *)e, &cp);
    if (!n) pfail(ps, "the text is not valid UTF-8");
    put_cp(out, cp);
    *p += n;
}

/* A double-quoted string at *p; leaves *p after the closing quote. */
static void parse_quoted(Parser *ps, const char **pp, WVec *out, bool allow_nul, const char *what) {
    const char *p = *pp, *e = ps->end;
    if (p >= e || *p != '"') pfail(ps, "a double-quoted string was expected");
    p++;
    for (;;) {
        if (p >= e) pfail(ps, "the string is not closed by a double quote");
        unsigned char c = (unsigned char)*p;
        if (c == '"') { p++; break; }
        if (c == '\\') {
            p++;
            if (p >= e) pfail(ps, "the string ends in a backslash");
            if (*p == '\\' || *p == '"') { wvec_put(out, (WCHAR)*p); p++; }
            else if (*p == 'u') { p++; wvec_put(out, parse_u4(ps, &p, e)); }
            else pfail(ps, "unknown escape \\%c in a string (the escapes are \\\\ \\\" and \\uXXXX)", *p);
        } else if (c < 0x20 || c == 0x7F) {
            pfail(ps, "control character %#x in a string (write it as \\u%04x)", c, c);
        } else if (c < 0x80) { wvec_put(out, c); p++; }
        else parse_raw(ps, &p, e, out);
    }
    if (!allow_nul)
        for (uint32_t i = 0; i < out->n; i++)
            if (out->p[i] == 0) pfail(ps, "a NUL (\\u0000) cannot be part of a %s", what);
    *pp = p;
}

/* One component of a key path at *pp, up to a backslash or the closing bracket. */
static void parse_component(Parser *ps, const char **pp, WVec *out) {
    const char *p = *pp, *e = ps->end;
    while (p < e && *p != '\\' && *p != ']') {
        unsigned char c = (unsigned char)*p;
        if (c == '%') {
            p++;
            if (p >= e || *p != 'u') pfail(ps, "a percent sign in a key name must start %%uXXXX");
            p++;
            wvec_put(out, parse_u4(ps, &p, e));
        } else if (c < 0x20 || c == 0x7F) {
            pfail(ps, "control character %#x in a key name (write it as %%u%04x)", c, c);
        } else if (c < 0x80) { wvec_put(out, c); p++; }
        else parse_raw(ps, &p, e, out);
    }
    *pp = p;
}

static void validate_name(Parser *ps, const WVec *v, unsigned max, const char *what) {
    if (v->n == 0) pfail(ps, "a %s cannot be empty", what);
    if (v->n > max) pfail(ps, "the %s is %u characters; the limit is %u", what, v->n, max);
    for (uint32_t i = 0; i < v->n; i++) {
        if (v->p[i] == 0) pfail(ps, "a NUL (%%u0000) cannot be part of a %s", what);
        if (v->p[i] == '\\') pfail(ps, "a backslash (%%u005c) cannot be part of a %s", what);
        if (!rt_wchar_case_known(v->p[i]))
            pfail(ps, "the character U+%04X of a %s has a case the locale tables do not cover, so names cannot be compared",
                  v->p[i], what);
    }
}

/* The key line at ps->cur (after blanks, at '['). Returns the key. */
static Key *parse_key_line(Parser *ps, const char *p) {
    const char *e = ps->end;
    p++;                                            /* [ */
    const char *rs = p;
    while (p < e && *p != '\\' && *p != ']') p++;
    int r = -1;
    for (int i = 0; i < R_COUNT; i++)
        if ((size_t)(p - rs) == strlen(root_name[i]) && !memcmp(rs, root_name[i], (size_t)(p - rs))) r = i;
    if (r < 0)
        pfail(ps, "the key must start with HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER or HKEY_USERS (found '%.*s')",
              (int)(p - rs), rs);
    Key *cur = G.root[r];
    while (p < e && *p == '\\') {
        p++;
        WVec name = { 0 };
        parse_component(ps, &p, &name);
        validate_name(ps, &name, RT_REG_KEY_NAME_MAX, "key name");
        Key *kid = kid_find(cur, name.p, name.n);
        if (!kid) kid = kid_add(cur, name.p, name.n, false);
        free(name.p);
        cur = kid;
    }
    if (p >= e || *p != ']') pfail(ps, "the key line is not closed by ]");
    p++;

    if (cur->defined) pfail(ps, "the key appears twice");
    cur->defined = true;
    bool has_time = false, has_class = false;
    for (;;) {
        skip_blanks(&p, e);
        if (p >= e) break;
        if (e - p >= 5 && !memcmp(p, "time=", 5)) {
            if (has_time) pfail(ps, "time= appears twice");
            if (cur->root) pfail(ps, "a root key has no time");
            p += 5;
            uint64_t t = 0;
            for (int i = 0; i < 16; i++) {
                int h = p < e ? hexv((unsigned char)*p) : -1;
                if (h < 0) pfail(ps, "time= needs 16 hex digits");
                t = t << 4 | (uint64_t)h;
                p++;
            }
            if (p < e && *p != ' ' && *p != '\t') pfail(ps, "time= needs exactly 16 hex digits");
            cur->mtime = t;
            has_time = true;
        } else if (e - p >= 6 && !memcmp(p, "class=", 6)) {
            if (has_class) pfail(ps, "class= appears twice");
            if (cur->root) pfail(ps, "a root key has no class");
            p += 6;
            WVec c = { 0 };
            parse_quoted(ps, &p, &c, false, "class");
            if (c.n > CLASS_MAX) pfail(ps, "the class is %u characters; the limit is %u", c.n, CLASS_MAX);
            free(cur->cls);
            cur->cls = c.n ? wdup(c.p, c.n) : NULL;
            cur->clen = c.n;
            free(c.p);
            has_class = true;
        } else {
            pfail(ps, "unknown text after the key (the attributes are time= and class=)");
        }
    }
    return cur;
}

/* The value line at p (after blanks) belongs to key k. */
static void parse_value_line(Parser *ps, const char *p, Key *k) {
    const char *e = ps->end;
    if (restricted_root(k)) pfail(ps, "%s has no values", root_name[k->root - 1]);
    WVec name = { 0 };
    if (p < e && *p == '@') p++;
    else {
        parse_quoted(ps, &p, &name, false, "value name");
        if (name.n == 0) pfail(ps, "a value name cannot be empty (the default value is @)");
        if (name.n > RT_REG_VALUE_NAME_MAX) pfail(ps, "the value name is %u characters; the limit is %u", name.n, RT_REG_VALUE_NAME_MAX);
        for (uint32_t i = 0; i < name.n; i++)
            if (!rt_wchar_case_known(name.p[i]))
                pfail(ps, "the character U+%04X of a value name has a case the locale tables do not cover", name.p[i]);
    }
    skip_blanks(&p, e);
    if (p >= e || *p != '=') pfail(ps, "'=' was expected after the value name");
    p++;
    skip_blanks(&p, e);

    uint32_t type;
    Buf data = { 0 };
    buf_put(&data, "", 0);
    size_t rem = (size_t)(e - p);
    if (rem >= 3 && !memcmp(p, "sz:", 3)) {
        p += 3;
        WVec s = { 0 };
        parse_quoted(ps, &p, &s, true, "string");
        wvec_put(&s, 0);
        type = REG_SZ;
        buf_put(&data, s.p, (size_t)s.n * 2);
        free(s.p);
    } else if (rem >= 10 && !memcmp(p, "expand_sz:", 10)) {
        p += 10;
        WVec s = { 0 };
        parse_quoted(ps, &p, &s, true, "string");
        wvec_put(&s, 0);
        type = REG_EXPAND_SZ;
        buf_put(&data, s.p, (size_t)s.n * 2);
        free(s.p);
    } else if (rem >= 9 && !memcmp(p, "multi_sz:", 9)) {
        p += 9;
        type = REG_MULTI_SZ;
        WVec all = { 0 };
        skip_blanks(&p, e);
        bool first = true;
        while (p < e) {
            if (!first) {
                if (*p != ',') pfail(ps, "',' or the end of the line was expected between the strings of a multi_sz");
                p++;
                skip_blanks(&p, e);
            }
            WVec s = { 0 };
            parse_quoted(ps, &p, &s, false, "multi_sz string");
            if (s.n == 0) pfail(ps, "a string of a multi_sz cannot be empty (it would end the list)");
            for (uint32_t i = 0; i < s.n; i++) wvec_put(&all, s.p[i]);
            wvec_put(&all, 0);
            free(s.p);
            first = false;
            skip_blanks(&p, e);
        }
        wvec_put(&all, 0);
        buf_put(&data, all.p, (size_t)all.n * 2);
        free(all.p);
    } else if (rem >= 6 && !memcmp(p, "dword:", 6)) {
        p += 6;
        type = REG_DWORD;
        uint32_t x = 0;
        for (int i = 0; i < 8; i++) {
            int h = p < e ? hexv((unsigned char)*p) : -1;
            if (h < 0) pfail(ps, "dword: needs exactly 8 hex digits");
            x = x << 4 | (uint32_t)h;
            p++;
        }
        uint8_t b[4] = { (uint8_t)x, (uint8_t)(x >> 8), (uint8_t)(x >> 16), (uint8_t)(x >> 24) };
        buf_put(&data, b, 4);
    } else if (rem >= 6 && !memcmp(p, "qword:", 6)) {
        p += 6;
        type = REG_QWORD;
        uint64_t x = 0;
        for (int i = 0; i < 16; i++) {
            int h = p < e ? hexv((unsigned char)*p) : -1;
            if (h < 0) pfail(ps, "qword: needs exactly 16 hex digits");
            x = x << 4 | (uint64_t)h;
            p++;
        }
        uint8_t b[8];
        for (int i = 0; i < 8; i++) b[i] = (uint8_t)(x >> (8 * i));
        buf_put(&data, b, 8);
    } else if ((rem >= 4 && !memcmp(p, "hex:", 4)) || (rem >= 4 && !memcmp(p, "hex(", 4))) {
        if (p[3] == ':') { type = REG_BINARY; p += 4; }
        else {
            p += 4;
            uint64_t t = 0;
            int digits = 0;
            while (p < e && *p >= '0' && *p <= '9') { t = t * 10 + (uint64_t)(*p - '0'); digits++; p++; if (t > 0xFFFFFFFFu) pfail(ps, "the type number of hex(...) is above 4294967295"); }
            if (!digits) pfail(ps, "hex( needs a decimal type number");
            if (p + 1 >= e || p[0] != ')' || p[1] != ':') pfail(ps, "hex(%llu) must be followed by ':'", (unsigned long long)t);
            p += 2;
            type = (uint32_t)t;
        }
        bool first = true;
        while (p < e && *p != ' ' && *p != '\t') {
            if (!first) {
                if (*p != ',') pfail(ps, "',' was expected between the bytes");
                p++;
            }
            int h1 = p < e ? hexv((unsigned char)p[0]) : -1;
            int h2 = p + 1 < e ? hexv((unsigned char)p[1]) : -1;
            if (h1 < 0 || h2 < 0) pfail(ps, "a byte is two hex digits");
            if (data.n >= RT_REG_VALUE_DATA_MAX) pfail(ps, "the value is longer than %u bytes", RT_REG_VALUE_DATA_MAX);
            char byte = (char)(h1 << 4 | h2);
            buf_put(&data, &byte, 1);
            p += 2;
            first = false;
        }
    } else {
        pfail(ps, "unknown data (the forms are sz: expand_sz: multi_sz: dword: qword: hex: hex(N):)");
    }
    skip_blanks(&p, e);
    if (p < e) pfail(ps, "unexpected text after the value");
    if (data.n > RT_REG_VALUE_DATA_MAX) pfail(ps, "the value is %zu bytes; the limit is %u", data.n, RT_REG_VALUE_DATA_MAX);

    if (val_find(k, name.p ? name.p : empty_w, name.n) >= 0) pfail(ps, "the value appears twice in this key");
    Value *v = val_append(k, name.p ? name.p : empty_w, name.n);
    val_assign(v, type, data.p, (uint32_t)data.n);
    free(name.p);
    free(data.p);
}

static void parse_file(const char *file, const char *text, size_t len) {
    Parser ps = { .file = file, .line = 0 };
    const char *p = text, *end = text + len;
    bool header = false;
    Key *cur = NULL;
    while (p < end) {
        const char *eol = memchr(p, '\n', (size_t)(end - p));
        const char *next = eol ? eol + 1 : end;
        if (!eol) eol = end;
        ps.line++;
        ps.end = (eol > p && eol[-1] == '\r') ? eol - 1 : eol;
        const char *q = p;
        skip_blanks(&q, ps.end);
        for (const char *c = q; c < ps.end; c++)
            if ((unsigned char)*c == 0) pfail(&ps, "the file has a NUL byte");
        p = next;
        if (q >= ps.end || *q == '#') continue;
        if (!header) {
            size_t n = strlen(FILE_HEADER);
            if ((size_t)(ps.end - q) < n || memcmp(q, FILE_HEADER, n)) {
                pfail(&ps, "the first line must be '" FILE_HEADER "' (found '%.*s')", (int)(ps.end - q > 40 ? 40 : ps.end - q), q);
            }
            const char *r = q + n;
            skip_blanks(&r, ps.end);
            if (r < ps.end) pfail(&ps, "unexpected text after the header");
            header = true;
            continue;
        }
        if (*q == '[') cur = parse_key_line(&ps, q);
        else {
            if (!cur) pfail(&ps, "a value line must follow a key line");
            parse_value_line(&ps, q, cur);
        }
    }
    if (!header) { ps.line = ps.line ? ps.line : 1; pfail(&ps, "the file has no header line '" FILE_HEADER "'"); }
}

/* ---- load --------------------------------------------------------------------------------- */

static void make_roots(void) {
    for (int i = 0; i < R_COUNT; i++) {
        uint32_t n;
        WCHAR *w = ascii_w(root_name[i], &n);
        Key *k = key_new(NULL, w, n);
        free(w);
        k->root = (uint8_t)(1 + i);
        k->seeded = true;
        G.root[i] = k;
    }
}

/* Read the whole file into memory.  NULL (and *len 0) if it does not exist. */
static char *read_whole(const char *path, size_t *len) {
    *len = 0;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        if (errno == ENOENT) return NULL;
        rt_fatal("registry: cannot read %s: %s", path, strerror(errno));
    }
    struct stat st;
    if (fstat(fd, &st) != 0) rt_fatal("registry: cannot read %s: %s", path, strerror(errno));
    if (!S_ISREG(st.st_mode)) rt_fatal("registry: %s is not a regular file", path);
    if (st.st_size > (off_t)FILE_MAX_BYTES)
        rt_fatal("registry: %s is %lld bytes; the limit is %u", path, (long long)st.st_size, FILE_MAX_BYTES);
    char *buf = xcalloc((size_t)st.st_size + 1, 1);
    size_t got = 0;
    while (got < (size_t)st.st_size) {
        ssize_t r = read(fd, buf + got, (size_t)st.st_size - got);
        if (r < 0 && errno == EINTR) continue;
        if (r < 0) rt_fatal("registry: cannot read %s: %s", path, strerror(errno));
        if (r == 0) break;
        got += (size_t)r;
    }
    close(fd);
    *len = got;
    return buf;
}

/* Build the tree as start-up does: the file, then the seed. */
static void build_tree(void) {
    unlink(G.tmp_path);                     /* a save that was cut short; the real file is whole */
    make_roots();
    size_t len;
    char *text = read_whole(G.path, &len);
    if (text) parse_file(G.path, text, len);
    seed_tree();
    free(G.saved);
    G.saved = NULL;
    G.saved_len = 0;
    if (text) { G.saved = text; G.saved_len = len; }
    else {
        Buf b = { 0 };
        serialize(&b);                      /* what a tree with nothing of the guest's in it says */
        G.saved = b.p;
        G.saved_len = b.n;
    }
}

void rt_registry_init(void) {
    if (G.ready) rt_fatal("rt_registry_init called twice");
    const char *dir = rt_config()->state_dir;
    if (!dir || !dir[0]) rt_fatal("the registry needs RtConfig.state_dir, which is not set");
    size_t n = strlen(dir) + 1 + sizeof FILE_NAME + 8;
    G.path = xcalloc(n, 1);
    G.tmp_path = xcalloc(n, 1);
    snprintf(G.path, n, "%s/%s", dir, FILE_NAME);
    snprintf(G.tmp_path, n, "%s/%s.tmp", dir, FILE_NAME);
    build_tree();
    G.ready = true;
}

const char *rt_registry_path(void) {
    if (!G.ready) rt_fatal("rt_registry_path before rt_registry_init");
    return G.path;
}

void rt_registry_test_reload(void) {
    lock();
    for (int i = 0; i < R_COUNT; i++) { tree_discard(G.root[i]); key_unref(G.root[i]); G.root[i] = NULL; }
    G.classes = G.config = NULL;
    build_tree();
    unlock();
}

/* ============================================================================================
 * 7. Handles
 * ========================================================================================== */

typedef struct RegHandle {
    RtObject obj;
    Key     *key;               /* a counted reference */
    DWORD    access;
} RegHandle;

static void reg_destroy(RtObject *o) { key_unref(((RegHandle *)o)->key); }

static const RtObjectOps reg_ops = { .name = "regkey", .destroy = reg_destroy };

static const char *predef_name(uint32_t lo) {
    switch (lo) {
    case 0x80000000u: return "HKEY_CLASSES_ROOT";
    case 0x80000001u: return "HKEY_CURRENT_USER";
    case 0x80000002u: return "HKEY_LOCAL_MACHINE";
    case 0x80000003u: return "HKEY_USERS";
    case 0x80000004u: return "HKEY_PERFORMANCE_DATA";
    case 0x80000005u: return "HKEY_CURRENT_CONFIG";
    case 0x80000006u: return "HKEY_DYN_DATA";
    case 0x80000007u: return "HKEY_CURRENT_USER_LOCAL_SETTINGS";
    case 0x80000050u: return "HKEY_PERFORMANCE_TEXT";
    case 0x80000060u: return "HKEY_PERFORMANCE_NLSTEXT";
    default: return NULL;
    }
}

/* Turn a guest HKEY into a key and the rights it has.  Predefined keys (the rule in rt/registry.h)
 * need no table; anything else is a handle of the table.  On success a table handle's object is
 * referenced and release() must be called. */
static LSTATUS acquire(HKEY h, const char *api, KeyRef *r) {
    memset(r, 0, sizeof *r);
    uint64_t v = (uint64_t)(uintptr_t)h;
    uint32_t lo = (uint32_t)v, hi = (uint32_t)(v >> 32);
    if (lo & 0x80000000u) {
        if (hi != 0 && hi != 0xFFFFFFFFu) return ERROR_INVALID_HANDLE;
        Key *k;
        switch (lo) {
        case 0x80000000u: k = G.classes; break;
        case 0x80000001u: k = G.root[R_HKCU]; break;
        case 0x80000002u: k = G.root[R_HKLM]; break;
        case 0x80000003u: k = G.root[R_HKU]; break;
        case 0x80000005u: k = G.config; break;
        case 0x80000004u: case 0x80000007u: case 0x80000050u: case 0x80000060u:
            UNHANDLED("%s: the predefined key %s (%#x) is not modelled", api, predef_name(lo), lo);
        default:
            return ERROR_INVALID_HANDLE;        /* HKEY_DYN_DATA (Windows 9x only), and every other value */
        }
        r->key = k;
        r->access = KEY_ALL_ACCESS;
        r->predef = true;
        return ERROR_SUCCESS;
    }
    /* rt_handle_ref sets last error when it fails; the registry leaves last error alone
     * (rt/registry.h, rule 1), so the caller's value is put back. */
    DWORD last = rt_thread_get_last_error();
    RtObject *o = rt_handle_ref(h, RT_K_REGKEY);
    rt_thread_set_last_error(last);
    if (!o) return ERROR_INVALID_HANDLE;
    RegHandle *rh = (RegHandle *)o;
    r->key = rh->key;
    r->access = rh->access;
    r->obj = o;
    return ERROR_SUCCESS;
}

static void release(KeyRef *r) {
    if (r->obj) rt_object_unref(r->obj);
    r->obj = NULL;
}

/* acquire + the access right + not deleted, in the order the kernel checks them. */
static LSTATUS prepare(HKEY h, DWORD need, const char *api, KeyRef *r) {
    LSTATUS st = acquire(h, api, r);
    if (st) return st;
    if ((r->access & need) != need) { release(r); return ERROR_ACCESS_DENIED; }
    if (r->key->deleted) { release(r); return ERROR_KEY_DELETED; }
    return ERROR_SUCCESS;
}

static LSTATUS new_handle(Key *k, DWORD access, HKEY *out) {
    RegHandle *rh = (RegHandle *)rt_object_new(RT_K_REGKEY, sizeof *rh, &reg_ops, NULL, NULL);
    if (!rh) return ERROR_NOT_ENOUGH_MEMORY;
    key_ref(k);
    rh->key = k;
    rh->access = access;
    HANDLE h = rt_handle_new(&rh->obj);         /* consumes our reference, or drops it (and the key's) on failure */
    if (!h) return ERROR_NOT_ENOUGH_MEMORY;
    *out = h;
    return ERROR_SUCCESS;
}

/* ============================================================================================
 * 8. The functions
 * ========================================================================================== */

/* ---- ANSI conversion ----------------------------------------------------------------------- */

/* A NUL-terminated ANSI string to a malloc'd UTF-16 string.  NULL stays NULL. */
static WCHAR *a2w_str(const char *s, const char *api, const char *what, uint32_t max_chars) {
    if (!s) return NULL;
    size_t bytes = strlen(s);
    if (bytes > max_chars)
        UNHANDLED("%s: %s of %zu characters (the limit is %u)", api, what, bytes, max_chars);
    int need = rt_mb_to_wide(rt_cp_acp(), 0, s, -1, NULL, 0);
    if (need <= 0) rt_fatal("registry: internal: MultiByteToWideChar failed on a %zu-byte string", bytes);
    WCHAR *w = xcalloc((size_t)need, sizeof(WCHAR));
    if (rt_mb_to_wide(rt_cp_acp(), 0, s, -1, w, need) != need) rt_fatal("registry: internal: MultiByteToWideChar changed size");
    return w;
}

/* Bytes of the ANSI text for n UTF-16 units; fatal (in the locale core) for a character the
 * code page cannot show. */
static uint32_t w2a_size(const WCHAR *w, uint32_t n) {
    if (!n) return 0;
    int r = rt_wide_to_mb(rt_cp_acp(), 0, w, (int)n, NULL, 0, NULL, NULL);
    if (r <= 0) rt_fatal("registry: internal: WideCharToMultiByte failed on %u units", n);
    return (uint32_t)r;
}

static void w2a_write(const WCHAR *w, uint32_t n, char *dst, uint32_t size) {
    if (!n) return;
    if (rt_wide_to_mb(rt_cp_acp(), 0, w, (int)n, dst, (int)size, NULL, NULL) != (int)size)
        rt_fatal("registry: internal: WideCharToMultiByte changed size");
}

/* Store a name or class into a caller's buffer of `cap` characters (counting the NUL).  Returns the
 * length it has or needs (no NUL) in *len and whether it fitted; nothing is written if it did not. */
static bool put_string(bool ansi, const WCHAR *s, uint32_t n, void *buf, DWORD cap, DWORD *len) {
    uint32_t units = ansi ? w2a_size(s, n) : n;
    *len = units;
    if ((uint64_t)units + 1 > cap) return false;
    if (ansi) { w2a_write(s, n, buf, units); ((char *)buf)[units] = 0; }
    else { if (n) memcpy(buf, s, (size_t)n * sizeof(WCHAR)); ((WCHAR *)buf)[n] = 0; }
    return true;
}

/* The data of a value as the caller sees it: its size in the caller's unit (bytes of converted text
 * for a string read through an A function), checked, and written into buf (cap bytes) if given
 * and large enough.  A string type without its terminator gets one after it when there is room
 * (rt_reg_query_value_ex_w).  Returns false if it did not fit. */
static uint32_t data_size(bool ansi, const Value *v, const char *api) {
    if (!(ansi && is_string_type(v->type))) return v->size;
    if (v->size % 2)
        UNHANDLED("%s: the string value of type %u has %u bytes, which is not a whole number of UTF-16 characters, "
                  "and cannot be converted to the ANSI code page", api, v->type, v->size);
    return w2a_size((const WCHAR *)v->data, v->size / 2);
}

static void data_write(bool ansi, const Value *v, uint32_t out_size, uint8_t *buf, uint32_t cap) {
    if (ansi && is_string_type(v->type)) {
        w2a_write((const WCHAR *)v->data, v->size / 2, (char *)buf, out_size);
        if (out_size >= 1 && buf[out_size - 1] != 0 && cap >= out_size + 1) buf[out_size] = 0;
    } else {
        if (v->size) memcpy(buf, v->data, v->size);
        if (is_string_type(v->type) && v->size >= 2 && v->size % 2 == 0 && cap >= v->size + 2) {
            WCHAR last;
            memcpy(&last, v->data + v->size - 2, 2);
            if (last != 0) { buf[v->size] = 0; buf[v->size + 1] = 0; }
        }
    }
}

/* A value name from the guest: NULL means the default value; checked against the limits. */
static const WCHAR *vname(const WCHAR *name, const char *api, uint32_t *n) {
    if (!name) name = empty_w;
    *n = wlen(name);
    if (*n > RT_REG_VALUE_NAME_MAX)
        UNHANDLED("%s: a value name of %u characters (the limit is %u)", api, *n, RT_REG_VALUE_NAME_MAX);
    check_case(name, *n, api, "value name");
    return name;
}

/* ---- open and create ------------------------------------------------------------------------ */

static LSTATUS open_key(const char *api, HKEY parent, const WCHAR *subkey, DWORD options, REGSAM sam, PHKEY result) {
    if (!result) return ERROR_INVALID_PARAMETER;
    if (options & ~REG_OPTION_OPEN_LINK)
        UNHANDLED("%s: ulOptions %#x (only 0 and REG_OPTION_OPEN_LINK are modelled)", api, options);
    DWORD access = map_access(sam, api);
    KeyRef r;
    LSTATUS st = acquire(parent, api, &r);
    if (st) return st;
    uint32_t n;
    Comp *c;
    st = split_path(&r, subkey, api, true, &c, &n);
    if (st) { release(&r); return st; }
    if (r.key->deleted) { free(c); release(&r); return ERROR_KEY_DELETED; }
    if (n == 0) {
        free(c);
        if (r.predef) { *result = parent; return ERROR_SUCCESS; }    /* "the same handle passed into the function" */
        st = new_handle(r.key, access, result);
        release(&r);
        return st;
    }
    Key *cur = r.key;
    for (uint32_t i = 0; i < n; i++) {
        cur = kid_find(cur, c[i].s, c[i].n);
        if (!cur) {
            free(c);
            log_missing_key(api, r.key, subkey);
            release(&r);
            return ERROR_FILE_NOT_FOUND;
        }
    }
    free(c);
    st = new_handle(cur, access, result);
    if (st == ERROR_SUCCESS && RT_LOG_ON(RT_CH_REGISTRY, RT_LL_DEBUG)) {
        Buf p = { 0 };
        path_append(&p, cur, false);
        rt_log(RT_CH_REGISTRY, RT_LL_DEBUG, "%s: opened %s", api, p.p);
        free(p.p);
    }
    release(&r);
    return st;
}

LSTATUS rt_reg_open_key_ex_w(HKEY parent, LPCWSTR subkey, DWORD options, REGSAM sam, PHKEY result) {
    lock();
    LSTATUS st = open_key("RegOpenKeyExW", parent, subkey, options, sam, result);
    unlock();
    return st;
}

LSTATUS rt_reg_open_key_ex_a(HKEY parent, LPCSTR subkey, DWORD options, REGSAM sam, PHKEY result) {
    WCHAR *w = a2w_str(subkey, "RegOpenKeyExA", "a subkey string", RT_REG_SUBKEY_MAX);
    lock();
    LSTATUS st = open_key("RegOpenKeyExA", parent, w, options, sam, result);
    unlock();
    free(w);
    return st;
}

static LSTATUS create_key(const char *api, HKEY parent, const WCHAR *subkey, DWORD reserved, const WCHAR *cls,
                          DWORD options, REGSAM sam, const SECURITY_ATTRIBUTES *sa, PHKEY result, LPDWORD disposition) {
    if (!result) return ERROR_INVALID_PARAMETER;
    if (reserved) UNHANDLED("%s: Reserved is %#x; it must be 0", api, reserved);
    if (options & ~(REG_OPTION_VOLATILE | REG_OPTION_OPEN_LINK))
        UNHANDLED("%s: dwOptions %#x (only REG_OPTION_VOLATILE and REG_OPTION_OPEN_LINK are modelled; links and "
                  "backup/restore are not)", api, options);
    if (sa && sa->lpSecurityDescriptor)
        UNHANDLED("%s: a security descriptor (%p) is not modelled", api, sa->lpSecurityDescriptor);
    uint32_t clen = 0;
    if (cls) {
        clen = wlen(cls);
        if (clen > CLASS_MAX) UNHANDLED("%s: a class of %u characters (the limit is %u)", api, clen, CLASS_MAX);
        check_case(cls, clen, api, "class");
    }
    DWORD access = map_access(sam, api);
    bool vol = (options & REG_OPTION_VOLATILE) != 0;
    KeyRef r;
    LSTATUS st = acquire(parent, api, &r);
    if (st) return st;
    uint32_t n;
    Comp *c;
    st = split_path(&r, subkey, api, true, &c, &n);
    if (st) { release(&r); return st; }
    if (r.key->deleted) { free(c); release(&r); return ERROR_KEY_DELETED; }

    Key *cur = r.key;
    bool created_last = false, created_any = false;
    for (uint32_t i = 0; i < n; i++) {
        Key *kid = kid_find(cur, c[i].s, c[i].n);
        created_last = false;
        if (!kid) {
            if (restricted_root(cur)) { st = ERROR_ACCESS_DENIED; break; }
            if (cur->vol && !vol) { st = ERROR_CHILD_MUST_BE_VOLATILE; break; }
            kid = kid_add(cur, c[i].s, c[i].n, vol);
            cur->mtime = kid->mtime;
            created_last = created_any = true;
        }
        cur = kid;
    }
    free(c);
    if (st == ERROR_SUCCESS) {
        if (created_last && clen) { cur->cls = wdup(cls, clen); cur->clen = clen; }
        st = new_handle(cur, access, result);
        if (st == ERROR_SUCCESS && disposition) *disposition = created_last ? REG_CREATED_NEW_KEY : REG_OPENED_EXISTING_KEY;
    }
    if (created_any) persist();
    release(&r);
    return st;
}

LSTATUS rt_reg_create_key_ex_w(HKEY parent, LPCWSTR subkey, DWORD reserved, LPCWSTR cls, DWORD options, REGSAM sam,
                               const SECURITY_ATTRIBUTES *sa, PHKEY result, LPDWORD disposition) {
    lock();
    LSTATUS st = create_key("RegCreateKeyExW", parent, subkey, reserved, cls, options, sam, sa, result, disposition);
    unlock();
    return st;
}

LSTATUS rt_reg_create_key_ex_a(HKEY parent, LPCSTR subkey, DWORD reserved, LPCSTR cls, DWORD options, REGSAM sam,
                               const SECURITY_ATTRIBUTES *sa, PHKEY result, LPDWORD disposition) {
    WCHAR *w = a2w_str(subkey, "RegCreateKeyExA", "a subkey string", RT_REG_SUBKEY_MAX);
    WCHAR *wc = a2w_str(cls, "RegCreateKeyExA", "a class", CLASS_MAX);
    lock();
    LSTATUS st = create_key("RegCreateKeyExA", parent, w, reserved, wc, options, sam, sa, result, disposition);
    unlock();
    free(w);
    free(wc);
    return st;
}

LSTATUS rt_reg_close_key(HKEY key) {
    lock();
    uint64_t v = (uint64_t)(uintptr_t)key;
    uint32_t lo = (uint32_t)v, hi = (uint32_t)(v >> 32);
    LSTATUS st;
    if (lo & 0x80000000u) {
        /* A predefined key is not in the table: closing it succeeds and does nothing.  Any other
         * value with the top bit set is not a handle. */
        if ((hi != 0 && hi != 0xFFFFFFFFu) || !predef_name(lo) || lo == 0x80000006u) st = ERROR_INVALID_HANDLE;
        else if (lo == 0x80000004u || lo == 0x80000007u || lo == 0x80000050u || lo == 0x80000060u)
            UNHANDLED("RegCloseKey: the predefined key %s (%#x) is not modelled", predef_name(lo), lo);
        else st = ERROR_SUCCESS;
    } else {
        /* rt_handle_close_kind sets last error when it fails; put the caller's value back. */
        DWORD last = rt_thread_get_last_error();
        BOOL closed = rt_handle_close_kind(key, RT_K_REGKEY);
        rt_thread_set_last_error(last);
        st = closed ? ERROR_SUCCESS : ERROR_INVALID_HANDLE;
    }
    unlock();
    return st;
}

LSTATUS rt_reg_flush_key(HKEY key) {
    lock();
    KeyRef r;
    LSTATUS st = acquire(key, "RegFlushKey", &r);
    if (st == ERROR_SUCCESS) {
        if (r.key->deleted) st = ERROR_KEY_DELETED;      /* every change is on disk already: nothing to flush */
        release(&r);
    }
    unlock();
    return st;
}

/* ---- delete key ---------------------------------------------------------------------------- */

static LSTATUS delete_key(const char *api, HKEY parent, const WCHAR *subkey, REGSAM sam, DWORD reserved) {
    if (!subkey) return ERROR_INVALID_PARAMETER;
    if (reserved) UNHANDLED("%s: Reserved is %#x; it must be 0", api, reserved);
    if (sam & ~KEY_WOW64_RES) UNHANDLED("%s: samDesired %#x (only the KEY_WOW64 flags mean anything here)", api, sam);
    KeyRef r;
    LSTATUS st = acquire(parent, api, &r);
    if (st) return st;
    uint32_t n;
    Comp *c;
    st = split_path(&r, subkey, api, false, &c, &n);
    if (st) { release(&r); return st; }
    if (n == 0) UNHANDLED("%s: an empty subkey (Windows NT deletes the key that hKey names; not modelled)", api);
    if (r.key->deleted) { free(c); release(&r); return ERROR_KEY_DELETED; }

    Key *par = r.key;
    for (uint32_t i = 0; i + 1 < n && par; i++) par = kid_find(par, c[i].s, c[i].n);
    bool found = false;
    uint32_t at = 0;
    if (par) at = kid_search(par, c[n - 1].s, c[n - 1].n, &found);
    free(c);
    if (!found) {
        log_missing_key(api, r.key, subkey);
        release(&r);
        return ERROR_FILE_NOT_FOUND;
    }
    Key *target = par->kids[at];
    if (target->nkids) { release(&r); return ERROR_ACCESS_DENIED; }
    if (target->seeded) {
        Buf p = { 0 };
        path_append(&p, target, false);
        UNHANDLED("%s: deleting the seeded key %s (it would come back at the next start; remove it from the seed "
                  "deliberately instead)", api, p.p);
    }
    kid_remove(par, at);
    par->mtime = now100();
    persist();                              /* a volatile key leaves the file alone, but its parent's last write time moved */
    release(&r);
    return ERROR_SUCCESS;
}

LSTATUS rt_reg_delete_key_ex_w(HKEY parent, LPCWSTR subkey, REGSAM sam, DWORD reserved) {
    lock();
    LSTATUS st = delete_key("RegDeleteKeyExW", parent, subkey, sam, reserved);
    unlock();
    return st;
}

LSTATUS rt_reg_delete_key_ex_a(HKEY parent, LPCSTR subkey, REGSAM sam, DWORD reserved) {
    WCHAR *w = a2w_str(subkey, "RegDeleteKeyExA", "a subkey string", RT_REG_SUBKEY_MAX);
    lock();
    LSTATUS st = delete_key("RegDeleteKeyExA", parent, w, sam, reserved);
    unlock();
    free(w);
    return st;
}

/* ---- delete a key with everything below it ------------------------------------------------ */

/* The first seeded key in the tree of `k`, itself included, or NULL. */
static const Key *tree_seeded(const Key *k) {
    if (k->seeded) return k;
    for (uint32_t i = 0; i < k->nkids; i++) {
        const Key *s = tree_seeded(k->kids[i]);
        if (s) return s;
    }
    return NULL;
}

static LSTATUS delete_tree(const char *api, HKEY parent, const WCHAR *subkey) {
    if (!subkey) UNHANDLED("%s: a NULL subkey (what Windows deletes then is not verified)", api);
    KeyRef r;
    LSTATUS st = acquire(parent, api, &r);
    if (st) return st;
    uint32_t n;
    Comp *c;
    st = split_path(&r, subkey, api, false, &c, &n);
    if (st) { release(&r); return st; }
    if (n == 0) UNHANDLED("%s: an empty subkey (what Windows deletes then is not verified)", api);
    if (r.key->deleted) { free(c); release(&r); return ERROR_KEY_DELETED; }

    Key *par = r.key;
    for (uint32_t i = 0; i + 1 < n && par; i++) par = kid_find(par, c[i].s, c[i].n);
    bool found = false;
    uint32_t at = 0;
    if (par) at = kid_search(par, c[n - 1].s, c[n - 1].n, &found);
    free(c);
    if (!found) {
        log_missing_key(api, r.key, subkey);
        release(&r);
        return ERROR_FILE_NOT_FOUND;
    }
    Key *target = par->kids[at];
    const Key *seed = tree_seeded(target);
    if (seed) {
        Buf p = { 0 };
        path_append(&p, seed, false);
        UNHANDLED("%s: deleting the seeded key %s (it would come back at the next start; remove it from the seed "
                  "deliberately instead)", api, p.p);
    }
    tree_discard(target);                   /* every key below it: marked deleted, out of the tree */
    kid_remove(par, at);                    /* and the key itself */
    par->mtime = now100();
    persist();                              /* once, for the whole tree */
    release(&r);
    return ERROR_SUCCESS;
}

LSTATUS rt_reg_delete_tree_w(HKEY parent, LPCWSTR subkey) {
    lock();
    LSTATUS st = delete_tree("SHDeleteKeyW", parent, subkey);
    unlock();
    return st;
}

LSTATUS rt_reg_delete_tree_a(HKEY parent, LPCSTR subkey) {
    WCHAR *w = a2w_str(subkey, "SHDeleteKeyA", "a subkey string", RT_REG_SUBKEY_MAX);
    lock();
    LSTATUS st = delete_tree("SHDeleteKeyA", parent, w);
    unlock();
    free(w);
    return st;
}

/* ---- values -------------------------------------------------------------------------------- */

static LSTATUS query_value(const char *api, bool ansi, HKEY h, const WCHAR *name, LPDWORD reserved, LPDWORD type,
                           LPBYTE data, LPDWORD cb) {
    if (reserved) return ERROR_INVALID_PARAMETER;
    if (data && !cb) return ERROR_INVALID_PARAMETER;
    uint32_t nl;
    name = vname(name, api, &nl);
    KeyRef r;
    LSTATUS st = prepare(h, KEY_QUERY_VALUE, api, &r);
    if (st) return st;
    int i = val_find(r.key, name, nl);
    if (i < 0) {
        log_missing_value(api, r.key, name, nl);
        release(&r);
        return ERROR_FILE_NOT_FOUND;
    }
    const Value *v = &r.key->vals[i];
    uint32_t size = data_size(ansi, v, api);
    if (RT_LOG_ON(RT_CH_REGISTRY, RT_LL_DEBUG)) {
        Buf p = { 0 };
        path_append(&p, r.key, false);
        rt_log(RT_CH_REGISTRY, RT_LL_DEBUG, "%s: %s value %s: type %u, %u bytes", api, p.p,
               nl ? show(name, nl) : "(Default)", v->type, size);
        free(p.p);
    }
    if (data) {
        DWORD cap = *cb;
        if (cap < size) st = ERROR_MORE_DATA;
        else data_write(ansi, v, size, data, cap);
        if (type) *type = v->type;
        *cb = size;
    } else {
        if (type) *type = v->type;
        if (cb) *cb = size;
    }
    release(&r);
    return st;
}

LSTATUS rt_reg_query_value_ex_w(HKEY key, LPCWSTR name, LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD cb) {
    lock();
    LSTATUS st = query_value("RegQueryValueExW", false, key, name, reserved, type, data, cb);
    unlock();
    return st;
}

LSTATUS rt_reg_query_value_ex_a(HKEY key, LPCSTR name, LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD cb) {
    WCHAR *w = a2w_str(name, "RegQueryValueExA", "a value name", RT_REG_VALUE_NAME_MAX);
    lock();
    LSTATUS st = query_value("RegQueryValueExA", true, key, w, reserved, type, data, cb);
    unlock();
    free(w);
    return st;
}

static LSTATUS set_value(const char *api, bool ansi, HKEY h, const WCHAR *name, DWORD reserved, DWORD type,
                         const BYTE *data, DWORD cb) {
    if (reserved) UNHANDLED("%s: Reserved is %#x; it must be 0", api, reserved);
    if (cb && !data) UNHANDLED("%s: lpData is NULL but cbData is %u", api, cb);
    if (cb > RT_REG_VALUE_DATA_MAX)
        UNHANDLED("%s: a value of %u bytes (the limit is %u)", api, cb, RT_REG_VALUE_DATA_MAX);
    uint32_t nl;
    name = vname(name, api, &nl);
    KeyRef r;
    LSTATUS st = prepare(h, KEY_SET_VALUE, api, &r);
    if (st) return st;
    Key *k = r.key;
    if (restricted_root(k)) { release(&r); return ERROR_ACCESS_DENIED; }

    /* The registry holds UTF-16: an A call converts the data of a string type on the way in. */
    uint8_t *conv = NULL;
    const uint8_t *src = data;
    uint32_t size = cb;
    if (ansi && is_string_type(type) && cb) {
        int need = rt_mb_to_wide(rt_cp_acp(), 0, (const char *)data, (int)cb, NULL, 0);
        if (need <= 0) rt_fatal("registry: internal: MultiByteToWideChar failed on %u bytes", cb);
        conv = xcalloc((size_t)need, sizeof(WCHAR));
        if (rt_mb_to_wide(rt_cp_acp(), 0, (const char *)data, (int)cb, (WCHAR *)conv, need) != need)
            rt_fatal("registry: internal: MultiByteToWideChar changed size");
        src = conv;
        size = (uint32_t)need * 2;
    }

    int i = val_find(k, name, nl);
    if (i >= 0) {
        Value *v = &k->vals[i];
        if (v->type == type && v->size == size && (size == 0 || memcmp(v->data, src, size) == 0)) {
            free(conv);                     /* the value already says this: nothing changes (UNSURE: Windows) */
            release(&r);
            return ERROR_SUCCESS;
        }
        val_assign(v, type, src, size);
    } else {
        Value *v = val_append(k, name, nl);
        val_assign(v, type, src, size);
    }
    free(conv);
    k->mtime = now100();
    if (!k->vol) persist();
    release(&r);
    return ERROR_SUCCESS;
}

LSTATUS rt_reg_set_value_ex_w(HKEY key, LPCWSTR name, DWORD reserved, DWORD type, const BYTE *data, DWORD cb) {
    lock();
    LSTATUS st = set_value("RegSetValueExW", false, key, name, reserved, type, data, cb);
    unlock();
    return st;
}

LSTATUS rt_reg_set_value_ex_a(HKEY key, LPCSTR name, DWORD reserved, DWORD type, const BYTE *data, DWORD cb) {
    WCHAR *w = a2w_str(name, "RegSetValueExA", "a value name", RT_REG_VALUE_NAME_MAX);
    lock();
    LSTATUS st = set_value("RegSetValueExA", true, key, w, reserved, type, data, cb);
    unlock();
    free(w);
    return st;
}

static LSTATUS delete_value(const char *api, HKEY h, const WCHAR *name) {
    uint32_t nl;
    name = vname(name, api, &nl);
    KeyRef r;
    LSTATUS st = prepare(h, KEY_SET_VALUE, api, &r);
    if (st) return st;
    int i = val_find(r.key, name, nl);
    if (i < 0) {
        log_missing_value(api, r.key, name, nl);
        release(&r);
        return ERROR_FILE_NOT_FOUND;
    }
    if (r.key->vals[i].seeded) {
        Buf p = { 0 };
        path_append(&p, r.key, false);
        UNHANDLED("%s: deleting the seeded value %s of %s (it would come back at the next start; remove it "
                  "from the seed deliberately instead)", api, nl ? show(name, nl) : "(Default)", p.p);
    }
    val_remove(r.key, (uint32_t)i);
    r.key->mtime = now100();
    if (!r.key->vol) persist();
    release(&r);
    return ERROR_SUCCESS;
}

LSTATUS rt_reg_delete_value_w(HKEY key, LPCWSTR name) {
    lock();
    LSTATUS st = delete_value("RegDeleteValueW", key, name);
    unlock();
    return st;
}

LSTATUS rt_reg_delete_value_a(HKEY key, LPCSTR name) {
    WCHAR *w = a2w_str(name, "RegDeleteValueA", "a value name", RT_REG_VALUE_NAME_MAX);
    lock();
    LSTATUS st = delete_value("RegDeleteValueA", key, w);
    unlock();
    free(w);
    return st;
}

/* ---- enumeration --------------------------------------------------------------------------- */

static LSTATUS enum_value(const char *api, bool ansi, HKEY h, DWORD index, void *name, LPDWORD cch_name,
                          LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD cb) {
    if (reserved) return ERROR_INVALID_PARAMETER;
    if (!name || !cch_name) return ERROR_INVALID_PARAMETER;
    if (data && !cb) return ERROR_INVALID_PARAMETER;
    KeyRef r;
    LSTATUS st = prepare(h, KEY_QUERY_VALUE, api, &r);
    if (st) return st;
    if (index >= r.key->nvals) { release(&r); return ERROR_NO_MORE_ITEMS; }
    const Value *v = &r.key->vals[index];

    DWORD len;
    if (!put_string(ansi, v->name, v->nlen, name, *cch_name, &len)) { release(&r); return ERROR_MORE_DATA; }
    *cch_name = len;
    uint32_t size = data_size(ansi, v, api);
    if (data) {
        DWORD cap = *cb;
        if (cap < size) st = ERROR_MORE_DATA;
        else data_write(ansi, v, size, data, cap);
        *cb = size;
    } else if (cb) {
        *cb = size;
    }
    if (type) *type = v->type;
    release(&r);
    return st;
}

LSTATUS rt_reg_enum_value_w(HKEY key, DWORD index, LPWSTR name, LPDWORD cch_name, LPDWORD reserved, LPDWORD type,
                            LPBYTE data, LPDWORD cb) {
    lock();
    LSTATUS st = enum_value("RegEnumValueW", false, key, index, name, cch_name, reserved, type, data, cb);
    unlock();
    return st;
}

LSTATUS rt_reg_enum_value_a(HKEY key, DWORD index, LPSTR name, LPDWORD cch_name, LPDWORD reserved, LPDWORD type,
                            LPBYTE data, LPDWORD cb) {
    lock();
    LSTATUS st = enum_value("RegEnumValueA", true, key, index, name, cch_name, reserved, type, data, cb);
    unlock();
    return st;
}

static void store_time(PFILETIME ft, uint64_t t) {
    ft->dwLowDateTime = (DWORD)t;
    ft->dwHighDateTime = (DWORD)(t >> 32);
}

static LSTATUS enum_key(const char *api, bool ansi, HKEY h, DWORD index, void *name, LPDWORD cch_name,
                        LPDWORD reserved, void *cls, LPDWORD cch_cls, PFILETIME last_write) {
    if (reserved) return ERROR_INVALID_PARAMETER;
    if (!name || !cch_name) return ERROR_INVALID_PARAMETER;
    if (cls && !cch_cls) return ERROR_INVALID_PARAMETER;
    KeyRef r;
    LSTATUS st = prepare(h, KEY_ENUMERATE_SUB_KEYS, api, &r);
    if (st) return st;
    if (index >= r.key->nkids) { release(&r); return ERROR_NO_MORE_ITEMS; }
    const Key *kid = r.key->kids[index];

    /* Check both before writing either: a failure writes nothing. */
    DWORD nlen = ansi ? w2a_size(kid->name, kid->nlen) : kid->nlen;
    DWORD clen = 0;
    if (cls) clen = ansi ? w2a_size(kid->cls ? kid->cls : empty_w, kid->clen) : kid->clen;
    if ((uint64_t)nlen + 1 > *cch_name || (cls && (uint64_t)clen + 1 > *cch_cls)) { release(&r); return ERROR_MORE_DATA; }
    DWORD tmp;
    put_string(ansi, kid->name, kid->nlen, name, *cch_name, &tmp);
    *cch_name = tmp;
    if (cls) {
        put_string(ansi, kid->cls ? kid->cls : empty_w, kid->clen, cls, *cch_cls, &tmp);
        *cch_cls = tmp;
    }
    if (last_write) store_time(last_write, kid->mtime);
    release(&r);
    return ERROR_SUCCESS;
}

LSTATUS rt_reg_enum_key_ex_w(HKEY key, DWORD index, LPWSTR name, LPDWORD cch_name, LPDWORD reserved, LPWSTR cls,
                             LPDWORD cch_cls, PFILETIME last_write) {
    lock();
    LSTATUS st = enum_key("RegEnumKeyExW", false, key, index, name, cch_name, reserved, cls, cch_cls, last_write);
    unlock();
    return st;
}

LSTATUS rt_reg_enum_key_ex_a(HKEY key, DWORD index, LPSTR name, LPDWORD cch_name, LPDWORD reserved, LPSTR cls,
                             LPDWORD cch_cls, PFILETIME last_write) {
    lock();
    LSTATUS st = enum_key("RegEnumKeyExA", true, key, index, name, cch_name, reserved, cls, cch_cls, last_write);
    unlock();
    return st;
}

static LSTATUS query_info(const char *api, bool ansi, HKEY h, void *cls, LPDWORD cch_cls, LPDWORD reserved,
                          LPDWORD n_subkeys, LPDWORD max_subkey_len, LPDWORD max_class_len, LPDWORD n_values,
                          LPDWORD max_value_name_len, LPDWORD max_value_len, LPDWORD sd_len, PFILETIME last_write) {
    if (reserved) return ERROR_INVALID_PARAMETER;
    if (cls && !cch_cls) return ERROR_INVALID_PARAMETER;
    if (sd_len) UNHANDLED("%s: lpcbSecurityDescriptor is not NULL; security descriptors are not modelled", api);
    KeyRef r;
    LSTATUS st = prepare(h, KEY_QUERY_VALUE, api, &r);
    if (st) return st;
    const Key *k = r.key;

    if (cls) {
        DWORD len;
        const WCHAR *cs = k->cls ? k->cls : empty_w;
        DWORD need = ansi ? w2a_size(cs, k->clen) : k->clen;
        if (!put_string(ansi, cs, k->clen, cls, *cch_cls, &len)) { *cch_cls = need; release(&r); return ERROR_MORE_DATA; }
        *cch_cls = len;
    } else if (cch_cls) {
        *cch_cls = ansi ? w2a_size(k->cls ? k->cls : empty_w, k->clen) : k->clen;
    }

    uint32_t msub = 0, mcls = 0, mval = 0, mdata = 0;
    for (uint32_t i = 0; i < k->nkids; i++) {
        if (k->kids[i]->nlen > msub) msub = k->kids[i]->nlen;
        if (k->kids[i]->clen > mcls) mcls = k->kids[i]->clen;
    }
    for (uint32_t i = 0; i < k->nvals; i++) {
        if (k->vals[i].nlen > mval) mval = k->vals[i].nlen;
        if (k->vals[i].size > mdata) mdata = k->vals[i].size;
    }
    if (n_subkeys) *n_subkeys = k->nkids;
    if (max_subkey_len) *max_subkey_len = msub;
    if (max_class_len) *max_class_len = mcls;
    if (n_values) *n_values = k->nvals;
    if (max_value_name_len) *max_value_name_len = mval;
    if (max_value_len) *max_value_len = mdata;
    if (last_write) store_time(last_write, k->mtime);
    release(&r);
    return ERROR_SUCCESS;
}

LSTATUS rt_reg_query_info_key_w(HKEY key, LPWSTR cls, LPDWORD cch_cls, LPDWORD reserved, LPDWORD n_subkeys,
                                LPDWORD max_subkey_len, LPDWORD max_class_len, LPDWORD n_values,
                                LPDWORD max_value_name_len, LPDWORD max_value_len, LPDWORD sd_len,
                                PFILETIME last_write) {
    lock();
    LSTATUS st = query_info("RegQueryInfoKeyW", false, key, cls, cch_cls, reserved, n_subkeys, max_subkey_len,
                            max_class_len, n_values, max_value_name_len, max_value_len, sd_len, last_write);
    unlock();
    return st;
}

LSTATUS rt_reg_query_info_key_a(HKEY key, LPSTR cls, LPDWORD cch_cls, LPDWORD reserved, LPDWORD n_subkeys,
                                LPDWORD max_subkey_len, LPDWORD max_class_len, LPDWORD n_values,
                                LPDWORD max_value_name_len, LPDWORD max_value_len, LPDWORD sd_len,
                                PFILETIME last_write) {
    lock();
    LSTATUS st = query_info("RegQueryInfoKeyA", true, key, cls, cch_cls, reserved, n_subkeys, max_subkey_len,
                            max_class_len, n_values, max_value_name_len, max_value_len, sd_len, last_write);
    unlock();
    return st;
}
