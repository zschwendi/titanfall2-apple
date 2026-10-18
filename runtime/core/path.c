/* Path translation: the virtual C: drive (docs/RUNTIME.md sections 7.1 and 7.2).  Interface and
 * specification: rt/path.h.
 *
 * WHAT THE GUEST SEES.  A tree of seven directories (the "nodes" below).  Three are real host
 * trees, the mounts: C:\Titanfall2 (the install root), C:\Users\Player (<state>/home) and
 * C:\Users\Player\AppData\Local\Temp (<state>/tmp).  The other four, C:\, C:\Users, C:\Windows
 * and C:\Windows\system32, are VIRTUAL: they exist and are empty apart from the nodes below
 * them.  Each virtual directory is a real, empty host directory under <state>/vfs, created at
 * start-up, so that a shim can stat and enumerate it like any other; the translation refuses
 * to hand out a path for anything new inside one (creating there is ERROR_ACCESS_DENIED).  The
 * host directories <state>/vfs/Titanfall2 and <state>/vfs/Users/Player are placeholders so
 * that a listing of C:\ and C:\Users shows the mounts, and <state>/home/AppData/Local/Temp
 * does the same inside the home tree; the guest cannot name any of them.  Everything else is
 * ERROR_PATH_NOT_FOUND.
 *
 * HOW A PATH IS TRANSLATED.  (1) The Windows path, in UTF-8 (a W string is converted; an A
 * string is code page 1252, converted by the table below), is classified by its prefix.
 * (2) It is normalised TEXTUALLY, as Windows does and without looking at any file: separators
 * unified, empty components dropped, "." and ".." resolved against the guest's current
 * directory (".." at the root stays at the root), trailing dots and spaces trimmed.  (3) The
 * result is a list of components below C:\.  The longest node whose components are a prefix of
 * that list (compared ASCII case-insensitively) says which host directory the rest goes
 * under.  (4) The rest is appended to that host directory component by component; on a
 * case-sensitive volume each component is looked up in a listing of its directory first.
 *
 * WHY A GUEST CAN NOT NAME A HOST FILE OUTSIDE THE MAPPED ROOTS.  Step 2 consumes every "."
 * and ".." and every separator, so a component that reaches step 4 is a non-empty name without
 * "/" or NUL that is neither "." nor "..": appending it can only descend.  The \\?\ prefix,
 * which Windows does not normalise, goes through a separate parser that REJECTS "." and ".."
 * components, empty components and "/" inside a name.  What the guest can still do is follow
 * a symbolic link that someone put inside a root: such a link is followed (a game installed
 * through a symlinked directory must work); no call the guest has creates one.
 *
 * WHAT IS NOT HANDLED, AND HOW IT FAILS.  The header says that UNC paths, other drives and
 * device paths return a Win32 error and are logged on the `file` channel; they do.  Every
 * other case that Windows accepts and this runtime does not (alternate data streams, a DOS
 * device name with an extension, a lone UTF-16 surrogate, a ".." search pattern, flags the
 * caller made up) ends in the unimplemented-case report (rt_unimplemented) naming the
 * offending value; it never returns a plausible answer.
 *
 * THE LISTING CACHE (case-sensitive volumes only; a case-insensitive volume is passed names
 * unchanged and never listed).  A directory's listing is kept, sorted by folded name, for up to
 * LISTING_SLOTS directories (the one used longest ago is dropped).  It is used while the
 * directory's device, inode and modification time are what they were when it was read, and only
 * if the directory had been quiet for SETTLE_NS before the read: a directory changed more
 * recently than that is read again on every use, because a volume with a coarse clock could change
 * it again without changing its time.  A change that leaves the time as it was (a clock set back)
 * is not seen; the code that creates, deletes or renames in a directory says so with
 * rt_path_invalidate.  Exact spelling wins over a folded match; two names that fold alike with
 * neither exact is an unimplemented-case report, never a choice.  The system calls are made outside
 * the lock.
 *
 * TEST HOOKS.  rt_path_test_force_case, rt_path_test_stats, rt_path_test_probe and
 * rt_path_test_fake_listing at the end of the file are not in rt/path.h; the tests that use them
 * declare them.  They change nothing a product shim can reach.
 *
 * Where a comment says UNSURE, the behaviour of Windows is not established and the choice
 * made is the documented reading or the cautious one; the unit's report lists them all.
 */
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "core.h"
#include "rt/path.h"

/* ---- limits ------------------------------------------------------------------------------- */

#define NAME_UNITS    255      /* NTFS: a file name is at most 255 UTF-16 code units */
#define NAME_BYTES    255      /* macOS NAME_MAX: at most 255 bytes of UTF-8 */
#define WIN_UNITS     32767    /* a UNICODE_STRING carries 65534 bytes: the longest Windows path */
#define CWD_BUF       1024     /* canonical Windows form of the current directory, NUL included */
#define SHOW_MAX      240      /* longest rendering of a guest string in a log line or report */
#define LISTING_SLOTS 32       /* cached directory listings */
#define DISK_NAME     1025     /* room for any name readdir can return (struct dirent has d_name[1024]) */
#define SETTLE_NS     INT64_C(2000000000)   /* a listing is trusted only if its directory was last
                                               changed at least this long before it was read */

#define ALL_FLAGS  (RT_PATH_EXISTING | RT_PATH_CREATE | RT_PATH_DIR | RT_PATH_WILDCARD | RT_PATH_DELETE | RT_PATH_RENAME)
#define GUARD_FLAGS  (RT_PATH_DELETE | RT_PATH_RENAME)     /* the caller is about to remove or move the target */

/* ---- unhandled cases ------------------------------------------------------------------------ */

static Ctx *path_ctx(void) {
    RtThread *t = rt_thread_self();
    return t ? &t->ctx : NULL;
}
#define UNHANDLED(...)  rt_unimplemented(path_ctx(), __FILE__, __LINE__, __VA_ARGS__)

/* ---- strings -------------------------------------------------------------------------------- */

static inline char fold(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
static inline bool is_sep(char c) { return c == '\\' || c == '/'; }
static inline bool is_alpha(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

/* ASCII-only case-insensitive equality: the only folding the runtime does (section 7.2). */
static bool ieq(const char *a, const char *b) {
    for (; *a && *b; a++, b++) if (fold(*a) != fold(*b)) return false;
    return *a == *b;
}
static bool ieq_n(const char *a, size_t n, const char *b) {
    if (strlen(b) != n) return false;
    for (size_t i = 0; i < n; i++) if (fold(a[i]) != fold(b[i])) return false;
    return true;
}

/* Code page 1252, bytes 0x80 to 0x9F; 0xA0 to 0xFF are U+00A0 to U+00FF and 0x00 to 0x7F are
 * ASCII.  Five bytes, 0x81 0x8D 0x8F 0x90 0x9D, have no character in the code page.  They are
 * mapped to the C1 control with the same value (U+0081 and so on), which is what Windows'
 * own table for 1252 (MultiByteToWideChar) does; the result is a valid, if odd, file name
 * character.  UNSURE: from memory of that table, not checked against Windows. */
static const uint16_t cp1252_80[32] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
};

static size_t put_utf8(char *o, uint32_t c) {
    if (c < 0x80)    { o[0] = (char)c; return 1; }
    if (c < 0x800)   { o[0] = (char)(0xC0 | (c >> 6)); o[1] = (char)(0x80 | (c & 0x3F)); return 2; }
    if (c < 0x10000) { o[0] = (char)(0xE0 | (c >> 12)); o[1] = (char)(0x80 | ((c >> 6) & 0x3F));
                       o[2] = (char)(0x80 | (c & 0x3F)); return 3; }
    o[0] = (char)(0xF0 | (c >> 18)); o[1] = (char)(0x80 | ((c >> 12) & 0x3F));
    o[2] = (char)(0x80 | ((c >> 6) & 0x3F)); o[3] = (char)(0x80 | (c & 0x3F));
    return 4;
}

/* One code point of well-formed UTF-8: its length in bytes, or 0 (overlong forms, surrogates,
 * values above U+10FFFF and truncated sequences are all "not well-formed"). */
static size_t get_utf8(const char *p, size_t n, uint32_t *cp) {
    const unsigned char *s = (const unsigned char *)p;
    if (n == 0) return 0;
    unsigned c = s[0];
    if (c < 0x80) { *cp = c; return 1; }
    if (c >= 0xC2 && c <= 0xDF && n >= 2 && (s[1] & 0xC0) == 0x80) {
        *cp = ((c & 0x1Fu) << 6) | (s[1] & 0x3Fu);
        return 2;
    }
    if (c >= 0xE0 && c <= 0xEF && n >= 3 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
        uint32_t v = ((c & 0x0Fu) << 12) | ((s[1] & 0x3Fu) << 6) | (s[2] & 0x3Fu);
        if (v < 0x800 || (v >= 0xD800 && v <= 0xDFFF)) return 0;
        *cp = v;
        return 3;
    }
    if (c >= 0xF0 && c <= 0xF4 && n >= 4 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80) {
        uint32_t v = ((c & 0x07u) << 18) | ((s[1] & 0x3Fu) << 12) | ((s[2] & 0x3Fu) << 6) | (s[3] & 0x3Fu);
        if (v < 0x10000 || v > 0x10FFFF) return 0;
        *cp = v;
        return 4;
    }
    return 0;
}

/* n UTF-16 units to UTF-8; `out` has room for 3n+1 bytes.  Returns the length, or SIZE_MAX
 * with *bad set to the index of an unpaired surrogate. */
static size_t w_to_utf8(const WCHAR *w, size_t n, char *out, size_t *bad) {
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        uint32_t c = w[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < n && w[i + 1] >= 0xDC00 && w[i + 1] <= 0xDFFF) {
            c = 0x10000 + ((c - 0xD800) << 10) + (w[i + 1] - 0xDC00u);
            i++;
        } else if (c >= 0xD800 && c <= 0xDFFF) {
            *bad = i;
            return SIZE_MAX;
        }
        o += put_utf8(out + o, c);
    }
    out[o] = 0;
    return o;
}

/* Code page 1252 bytes to UTF-8; `out` has room for 3n+1 bytes. */
static size_t a_to_utf8(const char *a, size_t n, char *out) {
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned b = (unsigned char)a[i];
        o += put_utf8(out + o, b >= 0x80 && b < 0xA0 ? cp1252_80[b - 0x80] : b);
    }
    out[o] = 0;
    return o;
}

/* Well-formed UTF-8 to UTF-16.  Writes to `out` if it is not NULL (the caller sized it).
 * Returns the number of units, or -1 if the bytes are not well-formed UTF-8. */
static long utf8_to_utf16(const char *s, size_t n, WCHAR *out) {
    long u = 0;
    for (size_t i = 0; i < n;) {
        uint32_t c;
        size_t k = get_utf8(s + i, n - i, &c);
        if (!k) return -1;
        i += k;
        if (c >= 0x10000) {
            if (out) { out[u] = (WCHAR)(0xD800 + ((c - 0x10000) >> 10)); out[u + 1] = (WCHAR)(0xDC00 + ((c - 0x10000) & 0x3FF)); }
            u += 2;
        } else {
            if (out) out[u] = (WCHAR)c;
            u++;
        }
    }
    return u;
}

static size_t utf16_units(const char *s, size_t n) {
    size_t u = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if ((c & 0xC0) == 0x80) continue;           /* continuation byte */
        u += c >= 0xF0 ? 2 : 1;
    }
    return u;
}

/* A printable rendering of a guest or host string for logs and reports: control characters
 * and ill-formed bytes as \xNN, cut off with "..." when long. */
static void show_utf8(const char *s, size_t n, char *out, size_t cap) {
    size_t o = 0;
    for (size_t i = 0; i < n;) {
        char tmp[8];
        size_t k;
        uint32_t cp;
        unsigned char c = (unsigned char)s[i];
        if (c >= 0x20 && c < 0x7F) { tmp[0] = (char)c; k = 1; tmp[1] = 0; i++; }
        else if (c >= 0x80 && (k = get_utf8(s + i, n - i, &cp)) != 0) { memcpy(tmp, s + i, k); tmp[k] = 0; i += k; }
        else { snprintf(tmp, sizeof tmp, "\\x%02X", c); k = strlen(tmp); i++; }
        if (o + k + 4 >= cap) { memcpy(out + o, "...", 3); o += 3; break; }
        memcpy(out + o, tmp, k);
        o += k;
    }
    out[o] = 0;
}

static void show_w(const WCHAR *w, size_t n, char *out, size_t cap) {
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        char tmp[8];
        if (w[i] >= 0x20 && w[i] < 0x7F) snprintf(tmp, sizeof tmp, "%c", (char)w[i]);
        else snprintf(tmp, sizeof tmp, "\\u%04X", (unsigned)w[i]);
        size_t k = strlen(tmp);
        if (o + k + 4 >= cap) { memcpy(out + o, "...", 3); o += 3; break; }
        memcpy(out + o, tmp, k);
        o += k;
    }
    out[o] = 0;
}

/* ---- the table of nodes ------------------------------------------------------------------------ */

typedef struct {
    const char *wname;              /* canonical Windows path */
    const char *comp[5];            /* its components below C:\ */
    size_t      ncomp;
    bool        mount;              /* a real tree (else a virtual, empty directory) */
    char        host[RT_PATH_MAX];  /* host directory, absolute, no trailing slash */
    char        real[RT_PATH_MAX];  /* realpath(host) if that is spelled differently, else "" */
    bool        probe_cs;           /* the volume is case-sensitive, as asked at start-up */
} Node;

enum { N_ROOT, N_INSTALL, N_USERS, N_HOME, N_TEMP, N_WINDOWS, N_SYS32, N_COUNT };

static const struct { const char *wname; const char *comp[5]; size_t ncomp; bool mount; } node_def[N_COUNT] = {
    [N_ROOT]    = { "C:\\",                                   { 0 },                                            0, false },
    [N_INSTALL] = { "C:\\Titanfall2",                         { "Titanfall2" },                                 1, true  },
    [N_USERS]   = { "C:\\Users",                              { "Users" },                                      1, false },
    [N_HOME]    = { "C:\\Users\\Player",                      { "Users", "Player" },                            2, true  },
    [N_TEMP]    = { "C:\\Users\\Player\\AppData\\Local\\Temp", { "Users", "Player", "AppData", "Local", "Temp" }, 5, true  },
    [N_WINDOWS] = { "C:\\Windows",                            { "Windows" },                                    1, false },
    [N_SYS32]   = { "C:\\Windows\\system32",                  { "Windows", "system32" },                        2, false },
};

static struct {
    bool       ready;
    Node       nodes[N_COUNT];
    _Atomic int force;              /* test override of the volume probe: -1 none, 0 insensitive, 1 sensitive */
} g = { .force = -1 };

/* Index of the node that owns the component list: the longest one whose components are a
 * case-insensitive prefix of it.  N_ROOT (no components) always matches. */
static int find_node(const char *const *comps, size_t nc) {
    int best = N_ROOT;
    for (int i = 1; i < N_COUNT; i++) {
        const Node *nd = &g.nodes[i];
        if (nd->ncomp > nc || nd->ncomp <= g.nodes[best].ncomp) continue;
        size_t k = 0;
        while (k < nd->ncomp && ieq(nd->comp[k], comps[k])) k++;
        if (k == nd->ncomp) best = i;
    }
    return best;
}

static bool node_cs(const Node *nd) {
    int f = atomic_load(&g.force);
    return f >= 0 ? f != 0 : nd->probe_cs;
}

static void need_ready(const char *fn) {
    if (!g.ready) UNHANDLED("%s called before rt_path_init", fn);
}

/* ---- logging a refusal ----------------------------------------------------------------------- */

/* Return `err`, after logging why on the `file` channel.  Refusals of paths this runtime
 * does not provide (UNC, other drives, devices) log at warn level, so they show by default;
 * failures that Windows reports for the same text log at debug level. */
static DWORD refuse(RtLogLevel lvl, DWORD err, const char *why, const char *s, size_t n) {
    if (RT_LOG_ON(RT_CH_FILE, lvl)) {
        char shown[SHOW_MAX + 8];
        show_utf8(s, n, shown, sizeof shown);
        rt_log(RT_CH_FILE, lvl, "path: %s: \"%s\" (Win32 error %u)", why, shown, (unsigned)err);
    }
    return err;
}

/* ---- the current directory ---------------------------------------------------------------------- */

static pthread_mutex_t g_cwd_lock = PTHREAD_MUTEX_INITIALIZER;
static char g_cwd[CWD_BUF];         /* canonical: "C:\" or "C:\a\b" with the caller's spelling */

static void cwd_copy(char out[CWD_BUF]) {
    pthread_mutex_lock(&g_cwd_lock);
    memcpy(out, g_cwd, CWD_BUF);
    pthread_mutex_unlock(&g_cwd_lock);
}

/* ---- directory listings (case-sensitive volumes only) -------------------------------------------- */

typedef struct {
    char           *dir;            /* host directory: the key */
    dev_t           dev;
    ino_t           ino;
    struct timespec mtime;
    bool            settled;        /* read long enough after the last change to be trusted */
    bool            fake;           /* installed by a test: not checked against the directory */
    size_t          n;
    char          **names;          /* sorted by (ASCII-folded name, name) */
    uint64_t        stamp;          /* last use, for eviction */
} Listing;

static pthread_mutex_t g_cache_lock = PTHREAD_MUTEX_INITIALIZER;
static Listing  g_listing[LISTING_SLOTS];
static uint64_t g_stamp;
static _Atomic unsigned g_reads;     /* directory reads since start-up, for the tests */

static void listing_clear(Listing *l) {
    for (size_t i = 0; i < l->n; i++) free(l->names[i]);
    free(l->names);
    free(l->dir);
    memset(l, 0, sizeof *l);
}

static int cmp_fold(const char *a, const char *b) {
    for (;; a++, b++) {
        char x = fold(*a), y = fold(*b);
        if (x != y) return (unsigned char)x < (unsigned char)y ? -1 : 1;
        if (!x) return 0;
    }
}
static int cmp_entry(const void *pa, const void *pb) {
    const char *a = *(char *const *)pa, *b = *(char *const *)pb;
    int c = cmp_fold(a, b);
    return c ? c : strcmp(a, b);
}

static int64_t ts_ns(const struct timespec *t) { return (int64_t)t->tv_sec * 1000000000 + t->tv_nsec; }

/* Read directory `dir` (whose stat is `st`) into `l`.  0, or an errno. */
static int listing_load(Listing *l, const char *dir, const struct stat *st) {
    DIR *d = opendir(dir);
    if (!d) return errno;
    size_t cap = 64, n = 0;
    char **names = malloc(cap * sizeof *names);
    if (!names) rt_fatal("out of memory listing %s", dir);
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (n == cap) {
            cap *= 2;
            names = realloc(names, cap * sizeof *names);
            if (!names) rt_fatal("out of memory listing %s", dir);
        }
        names[n] = strdup(e->d_name);
        if (!names[n]) rt_fatal("out of memory listing %s", dir);
        n++;
    }
    closedir(d);
    qsort(names, n, sizeof *names, cmp_entry);
    l->dir = strdup(dir);
    if (!l->dir) rt_fatal("out of memory listing %s", dir);
    l->dev = st->st_dev;
    l->ino = st->st_ino;
    l->mtime = st->st_mtimespec;
    l->n = n;
    l->names = names;
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    /* A directory changed within SETTLE_NS of this read might change again without its
     * modification time changing (coarse clocks), so such a listing is read again next time. */
    l->settled = ts_ns(&now) - ts_ns(&st->st_mtimespec) >= SETTLE_NS;
    atomic_fetch_add(&g_reads, 1);
    return 0;
}

static Listing *listing_find(const char *dir) {
    for (int i = 0; i < LISTING_SLOTS; i++)
        if (g_listing[i].dir && !strcmp(g_listing[i].dir, dir)) return &g_listing[i];
    return NULL;
}

/* An empty slot, or else the one used longest ago. */
static Listing *listing_victim(void) {
    Listing *v = &g_listing[0];
    for (int i = 0; i < LISTING_SLOTS; i++) {
        if (!g_listing[i].dir) return &g_listing[i];
        if (g_listing[i].stamp < v->stamp) v = &g_listing[i];
    }
    return v;
}

/* Look `name` up in a listing: exact match first, then ASCII case-folded.  Returns 1 and the on-disk
 * spelling in `out` if found, 0 if not.  Several names that match only when folded, and none
 * exactly, is not something Windows can have: it is not decided which is meant, so it stops.
 * The caller holds g_cache_lock. */
static int listing_search(const Listing *l, const char *dir, const char *name, char out[DISK_NAME]) {
    /* Binary search for the first entry whose folded name is not below `name`'s. */
    size_t lo = 0, hi = l->n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (cmp_fold(l->names[mid], name) < 0) lo = mid + 1; else hi = mid;
    }
    size_t first = lo, run = 0;
    const char *exact = NULL;
    for (size_t i = first; i < l->n && cmp_fold(l->names[i], name) == 0; i++) {
        run++;
        if (!strcmp(l->names[i], name)) exact = l->names[i];
    }
    if (run > 1 && !exact) {
        char shown[SHOW_MAX + 8], a[SHOW_MAX + 8], b[SHOW_MAX + 8];
        show_utf8(name, strlen(name), shown, sizeof shown);
        show_utf8(l->names[first], strlen(l->names[first]), a, sizeof a);
        show_utf8(l->names[first + 1], strlen(l->names[first + 1]), b, sizeof b);
        UNHANDLED("rt_path: \"%s\" matches %zu names that differ only in case in %s (\"%s\", \"%s\", ...); "
                  "Windows can not have such a directory, so which one is meant is not decided", shown, run, dir, a, b);
    }
    const char *hit = exact ? exact : (run == 1 ? l->names[first] : NULL);
    if (!hit) return 0;
    strcpy(out, hit);
    return 1;
}

/* Look `name` up in directory `dir`.  Returns 1 and the on-disk spelling in `out` if it is there, 0 if
 * not, -1 with *err an errno if `dir` can not be read.  The listing is cached; it is checked against
 * the directory's device, inode and modification time on every call, and read again if any of them
 * changed or if the directory was changed too recently to tell (SETTLE_NS).  The system calls are made
 * outside the lock, so that threads that only look names up do not wait for each other's disk. */
static int dir_lookup(const char *dir, const char *name, char out[DISK_NAME], int *err) {
    struct stat st;
    const int srt = stat(dir, &st) == 0 ? 0 : errno;
    int rc;
    pthread_mutex_lock(&g_cache_lock);
    Listing *l = listing_find(dir);
    if (l && l->fake) {                                 /* a test's listing stands in for the directory */
        l->stamp = ++g_stamp;
        rc = listing_search(l, dir, name, out);
        goto out;
    }
    if (srt || !S_ISDIR(st.st_mode)) {
        *err = srt ? srt : ENOTDIR;
        if (l) listing_clear(l);
        rc = -1;
        goto out;
    }
    if (l && l->settled && l->dev == st.st_dev && l->ino == st.st_ino &&
        l->mtime.tv_sec == st.st_mtimespec.tv_sec && l->mtime.tv_nsec == st.st_mtimespec.tv_nsec) {
        l->stamp = ++g_stamp;
        rc = listing_search(l, dir, name, out);
        goto out;
    }
    pthread_mutex_unlock(&g_cache_lock);

    Listing fresh;
    memset(&fresh, 0, sizeof fresh);
    const int e = listing_load(&fresh, dir, &st);       /* the directory was stat'ed first: a change during the read shows next time */

    pthread_mutex_lock(&g_cache_lock);
    l = listing_find(dir);                              /* another thread may have read it meanwhile: replace that */
    if (e) {
        *err = e;
        if (l) listing_clear(l);
        rc = -1;
        goto out;
    }
    if (!l) l = listing_victim();
    listing_clear(l);
    *l = fresh;
    l->stamp = ++g_stamp;
    rc = listing_search(l, dir, name, out);
out:
    pthread_mutex_unlock(&g_cache_lock);
    return rc;
}

void rt_path_invalidate(const char *host_dir) {
    need_ready("rt_path_invalidate");
    if (!host_dir) UNHANDLED("rt_path_invalidate: no directory given");
    char norm[RT_PATH_MAX];
    if (strlen(host_dir) >= RT_PATH_MAX) return;        /* no listing of ours has a name that long */
    /* Same lexical spelling as the keys: the paths this file builds never contain "//" or
     * a trailing slash, the caller's might. */
    size_t o = 0;
    for (const char *p = host_dir; *p; p++) {
        if (*p == '/' && (o > 0 && norm[o - 1] == '/')) continue;
        norm[o++] = *p;
    }
    while (o > 1 && norm[o - 1] == '/') o--;
    norm[o] = 0;
    pthread_mutex_lock(&g_cache_lock);
    for (int i = 0; i < LISTING_SLOTS; i++)
        if (g_listing[i].dir && !strcmp(g_listing[i].dir, norm)) listing_clear(&g_listing[i]);
    pthread_mutex_unlock(&g_cache_lock);
}

/* ---- names ------------------------------------------------------------------------------------ */

/* Characters Windows does not allow in a file name.  Returns 0 if the name is fine, 1 if it has a
 * character that makes it invalid (ERROR_INVALID_NAME), 2 if it has ':' (an alternate data
 * stream on NTFS, which this runtime does not provide).  `*` and `?` are allowed in the
 * pattern of a search.  `/` can not reach here from a normalised path (it is a separator) and is
 * invalid inside a \\?\ path. */
static int name_chars(const char *s, size_t n, bool allow_wild) {
    int colon = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20) return 1;
        switch (c) {
        case '<': case '>': case '"': case '|': case '/': case '\\': return 1;
        case '*': case '?': if (!allow_wild) return 1; break;
        case ':': colon = 1; break;
        default: break;
        }
    }
    return colon ? 2 : 0;
}

/* DOS device names.  Returns 0 if `s` is not one, 1 for exactly NUL, 2 for any other device
 * name, 3 for a device name followed by '.' and an extension or by ':' ("NUL:"; only
 * rt_path_full_w can see that one, the translation reports any ':' before it gets here).  Windows (up to 10, at least)
 * treats a device name with any extension as the device ("nul.txt" is NUL), later versions
 * may not; UNSURE which applies, so case 3 is reported rather than guessed.  The superscript
 * digits that Windows 10 also reserves (COM¹ and so on) are NOT recognised here (UNSURE). */
static int reserved_kind(const char *s) {
    size_t n = strlen(s), base = 0;
    while (base < n && s[base] != '.' && s[base] != ':') base++;
    static const char *const names[] = {
        "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
        "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9", "CONIN$", "CONOUT$",
    };
    for (size_t i = 0; i < sizeof names / sizeof *names; i++) {
        if (!ieq_n(s, base, names[i])) continue;
        if (base < n) return 3;
        return ieq(names[i], "NUL") ? 1 : 2;
    }
    return 0;
}

/* A name that translates to itself in both directions when written as an ordinary (not \\?\)
 * path: used to decide whether a host name can be shown to the guest at all. */
static bool name_is_plain(const char *s) {
    size_t n = strlen(s);
    if (!n || n > NAME_BYTES || utf16_units(s, n) > NAME_UNITS) return false;
    if (!strcmp(s, ".") || !strcmp(s, "..")) return false;
    if (s[n - 1] == '.' || s[n - 1] == ' ') return false;
    if (name_chars(s, n, false)) return false;
    return reserved_kind(s) == 0;
}

/* ---- classification and normalisation of one path -------------------------------------------------- */

typedef struct {
    uint32_t     flags;
    const char **comps;             /* normalised components below C:\ (NUL-terminated strings) */
    size_t       nc;
    const char  *pattern;           /* RT_PATH_WILDCARD: the unchanged last component, else NULL */
    bool         trailing_sep;      /* the guest's path ended in a separator */
} Parsed;

static bool host_add(char *hp, size_t *hl, const char *s) {
    size_t l = strlen(s);
    if (*hl + l > RT_PATH_MAX - 1) return false;
    memcpy(hp + *hl, s, l + 1);
    *hl += l;
    return true;
}

static DWORD map_dir_errno(int e) {
    if (e == ENOENT || e == ENOTDIR) return ERROR_PATH_NOT_FOUND;
    return rt_win32_from_errno(e, RT_ERR_DIR);
}

/* Build the host path of a parsed Windows path.  See the head of the file for the node
 * table; see rt/path.h for the flags. */
static DWORD to_host(const Parsed *P, char host[RT_PATH_MAX]) {
    const uint32_t flags = P->flags;
    const bool wild = P->pattern != NULL;
    const Node *nd = &g.nodes[find_node(P->comps, P->nc)];
    const size_t rem = P->nc - nd->ncomp;         /* components below the node */
    char hp[RT_PATH_MAX];

    /* RT_PATH_DELETE, RT_PATH_RENAME: the caller is about to remove or move what the path names.
     * The nodes, and the directories that lead to a node (they hold the placeholder that makes
     * the node list as their child), are the skeleton of the drive and real host directories:
     * refuse before the disk is looked at.  A path is one of them exactly when its components
     * are a prefix of a node's (all of them: the node itself; none: the root). */
    if (flags & GUARD_FLAGS) {
        for (int i = 0; i < N_COUNT; i++) {
            const Node *x = &g.nodes[i];
            if (P->nc > x->ncomp) continue;
            size_t k = 0;
            while (k < P->nc && ieq(x->comp[k], P->comps[k])) k++;
            if (k == P->nc) return ERROR_ACCESS_DENIED;
        }
    }

    size_t hl = 0, root_len = strlen(nd->host), parent_len;
    hp[0] = 0;
    if (!host_add(hp, &hl, nd->host)) return ERROR_FILENAME_EXCED_RANGE;
    parent_len = hl;

    if (!nd->mount) {
        /* A virtual directory has no children but the nodes below it, and those were found by
         * find_node.  Anything else in it does not exist, and creating there is refused. */
        if (rem > 0) {
            if (wild || rem >= 2) return ERROR_PATH_NOT_FOUND;
            if (!(flags & RT_PATH_EXISTING)) return ERROR_ACCESS_DENIED;
            return (flags & RT_PATH_DIR) ? ERROR_PATH_NOT_FOUND : ERROR_FILE_NOT_FOUND;
        }
        if (wild && !(host_add(hp, &hl, "/") && host_add(hp, &hl, P->pattern))) return ERROR_FILENAME_EXCED_RANGE;
        goto done;
    }

    const bool cs = node_cs(nd);
    for (size_t i = nd->ncomp; i < P->nc; i++) {
        const bool last = i + 1 == P->nc && !wild;
        const char *name = P->comps[i];
        char disk[DISK_NAME];
        if (cs) {
            int err = 0;
            int r = dir_lookup(hp, name, disk, &err);
            if (r < 0) return map_dir_errno(err);
            if (r == 1) name = disk;
            else if (!last) return ERROR_PATH_NOT_FOUND;
            else if (flags & RT_PATH_EXISTING) return (flags & RT_PATH_DIR) ? ERROR_PATH_NOT_FOUND : ERROR_FILE_NOT_FOUND;
            /* else: a name that does not exist yet keeps the caller's spelling */
        }
        parent_len = hl;
        if (!host_add(hp, &hl, "/") || !host_add(hp, &hl, name)) return ERROR_FILENAME_EXCED_RANGE;
    }
    if (wild) {
        parent_len = hl;
        if (!host_add(hp, &hl, "/") || !host_add(hp, &hl, P->pattern)) return ERROR_FILENAME_EXCED_RANGE;
    }

    if ((!cs && parent_len > root_len) || (cs && wild && rem > 0)) {
        /* A case-insensitive volume passes names through, but the error for a missing
         * directory is still ERROR_PATH_NOT_FOUND and for a missing last component
         * ERROR_FILE_NOT_FOUND; the host call alone could not tell them apart.  (On a
         * case-sensitive volume the listings have shown that every directory exists, except
         * that the directory a search is in was found but not looked into.) */
        char dir[RT_PATH_MAX];
        memcpy(dir, hp, parent_len);
        dir[parent_len] = 0;
        struct stat st;
        if (stat(dir, &st) != 0) return map_dir_errno(errno);
        if (!S_ISDIR(st.st_mode)) return ERROR_PATH_NOT_FOUND;       /* a file in the middle of the path */
    }
    if (!cs && !wild && rem > 0 && (flags & RT_PATH_EXISTING)) {
        struct stat st;
        if (lstat(hp, &st) != 0) {
            if (errno == ENOENT) return (flags & RT_PATH_DIR) ? ERROR_PATH_NOT_FOUND : ERROR_FILE_NOT_FOUND;
            return rt_win32_from_errno(errno, RT_ERR_FILE);
        }
    }

done:
    /* A trailing separator is kept as a trailing slash: it makes the host call insist on a
     * directory, as Windows does. */
    if (P->trailing_sep && !wild && !host_add(hp, &hl, "/")) return ERROR_FILENAME_EXCED_RANGE;
    memcpy(host, hp, hl + 1);
    return 0;
}

/* The components of the current directory `cwd` ("C:\" followed by components), copied to `w`
 * and appended to comps[*pnc].  Returns the first free byte of `w`. */
static char *cwd_comps(const char *cwd, char *w, const char **comps, size_t *pnc) {
    for (const char *c = cwd + 3; *c;) {
        const char *e = c;
        while (*e && *e != '\\') e++;
        memcpy(w, c, (size_t)(e - c));
        w[e - c] = 0;
        comps[(*pnc)++] = w;
        w += (e - c) + 1;
        c = *e ? e + 1 : e;
    }
    return w;
}

typedef struct {
    const char *pattern;        /* a search: its unchanged last segment; otherwise NULL */
    bool        dot_search;     /* a search whose last segment is "." or "..": the caller reports it */
    bool        emptied;        /* the last segment was nothing but periods and spaces and is gone */
} Tail;

/* THE normalisation, shared by rt_path_to_host_* and rt_path_full_w so that they can not
 * disagree: split `tl` (what follows the root of a path; '/' already replaced by '\'; changed
 * in place) into components appended to comps[*pnc], as Windows does (RtlGetFullPathName_U;
 * the rules of the Microsoft page "File path formats on Windows systems"): runs of separators
 * are one; "." is dropped and ".." removes the component before it (and does nothing when
 * there is none); then trailing characters are trimmed.  `wild`: the last segment of a text that
 * does not end in a separator is a search pattern and is returned unchanged in t->pattern. */
static void norm_tail(char *tl, bool wild, const char **comps, size_t *pnc, Tail *t) {
    size_t nc = *pnc;
    *t = (Tail){ 0 };
    for (char *p = tl; *p;) {
        while (*p == '\\') p++;
        if (!*p) break;
        char *q = p;
        while (*q && *q != '\\') q++;
        const bool final = *q == 0;           /* the last segment of a path that does not end in a separator */
        const char saved = *q;
        *q = 0;
        size_t sl = (size_t)(q - p);
        const bool dot = sl == 1 && p[0] == '.', dotdot = sl == 2 && p[0] == '.' && p[1] == '.';
        if (wild && final && (dot || dotdot)) {
            t->pattern = p;
            t->dot_search = true;
            break;
        }
        if (dot) {
            /* the current directory: nothing */
        } else if (dotdot) {
            if (nc > 0) nc--;                      /* at the root, ".." stays at the root */
        } else if (wild && final) {
            t->pattern = p;                        /* the search pattern is kept unchanged, trailing dots and all */
        } else if (final) {
            /* "If the path doesn't end in a separator, all trailing periods and spaces are removed."
             * This also turns a last segment of three or more dots into nothing. */
            while (sl && (p[sl - 1] == '.' || p[sl - 1] == ' ')) sl--;
            p[sl] = 0;
            if (sl) comps[nc++] = p; else t->emptied = true;
        } else {
            /* "If a segment ends in a single period, that period is removed." (A segment of
             * three or more periods is a valid name and is not touched; one that ends in two
             * periods, or in a space, is not "a single period" and stays.  UNSURE: this is the
             * documented reading, not checked against Windows.) */
            if (sl >= 2 && p[sl - 1] == '.' && p[sl - 2] != '.') p[--sl] = 0;
            comps[nc++] = p;
        }
        p = saved ? q + 1 : q;
    }
    *pnc = nc;
}

static DWORD nul_result(uint32_t flags, char host[RT_PATH_MAX]) {
    /* RT_PATH_DELETE, RT_PATH_RENAME: never hand /dev/null to an unlink or a rename.  This comes
     * first: rt/path.h promises ERROR_ACCESS_DENIED for NUL whatever the other flags are, so
     * RemoveDirectory("NUL") (RT_PATH_DIR with a guard flag) is refused like DeleteFile("NUL"). */
    if (flags & GUARD_FLAGS) return ERROR_ACCESS_DENIED;
    /* NUL names a device, not a directory and not a search.  UNSURE what Windows reports for
     * CreateDirectory("NUL") or FindFirstFile("NUL"); these two are the closest documented. */
    if (flags & RT_PATH_DIR) return ERROR_PATH_NOT_FOUND;
    if (flags & RT_PATH_WILDCARD) return ERROR_FILE_NOT_FOUND;
    strcpy(host, "/dev/null");
    return 0;
}

/* Translate the UTF-8 Windows path s[0..n).  `norm`, if not NULL, receives the canonical
 * Windows form of the result (CWD_BUF bytes).  `work` has room for strlen(cwd) + n + 4 bytes
 * and `comps` for (strlen(cwd) + n) / 2 + 4 entries. */
static DWORD resolve_in(const char *s, size_t n, uint32_t flags, bool allow_raw, char host[RT_PATH_MAX],
                        char *norm, const char *cwd, char *work, const char **comps) {
    const bool wild = (flags & RT_PATH_WILDCARD) != 0;
    enum { K_ABS, K_ROOTED, K_REL, K_DRIVEREL, K_RAW } kind;
    const char *tail;
    size_t tn;

    host[0] = 0;
    if (n == 0) {
        /* CreateFile("") and GetFileAttributes("") are ERROR_PATH_NOT_FOUND; UNSURE for the rest. */
        return refuse(RT_LL_DEBUG, ERROR_PATH_NOT_FOUND, "empty path", s, n);
    }

    /* ---- 1. the prefix: what kind of path is it? ---- */
    if (n >= 4 && !memcmp(s, "\\\\?\\", 4)) {
        const char *t = s + 4;
        size_t m = n - 4;
        if (m == 0) return refuse(RT_LL_WARN, ERROR_BAD_PATHNAME, "\\\\?\\ with nothing after it", s, n);
        if (m >= 2 && is_alpha(t[0]) && t[1] == ':') {
            if (m == 2) return refuse(RT_LL_WARN, ERROR_PATH_NOT_FOUND, "volume device path (not provided)", s, n);
            if (t[2] != '\\') return refuse(RT_LL_WARN, ERROR_BAD_PATHNAME, "malformed \\\\?\\ drive path", s, n);
            if (fold(t[0]) != 'c') return refuse(RT_LL_WARN, ERROR_PATH_NOT_FOUND, "no such drive (only C: exists)", s, n);
            if (!allow_raw) {
                char shown[SHOW_MAX + 8];
                show_utf8(s, n, shown, sizeof shown);
                UNHANDLED("rt_path_set_cwd_w: a \\\\?\\ path as the current directory (what Windows stores and "
                          "GetCurrentDirectory then returns is not established): \"%s\"", shown);
            }
            kind = K_RAW;
            tail = t + 3;
            tn = m - 3;
        } else if (m >= 3 && ieq_n(t, 3, "UNC") && (m == 3 || t[3] == '\\')) {
            return refuse(RT_LL_WARN, ERROR_PATH_NOT_FOUND, "UNC path (not provided)", s, n);
        } else if (m == 3 && ieq_n(t, 3, "NUL")) {
            return nul_result(flags, host);
        } else {
            return refuse(RT_LL_WARN, ERROR_PATH_NOT_FOUND, "device path (not provided)", s, n);
        }
    } else if (n >= 2 && is_sep(s[0]) && is_sep(s[1])) {
        if (n == 2 || is_sep(s[2])) return refuse(RT_LL_WARN, ERROR_BAD_PATHNAME, "UNC path without a server name", s, n);
        if ((s[2] == '.' || s[2] == '?') && (n == 3 || is_sep(s[3]))) {
            const char *t = s + (n >= 4 ? 4 : 3);
            size_t m = n - (n >= 4 ? 4 : 3);
            if (m == 3 && ieq_n(t, 3, "NUL")) return nul_result(flags, host);
            return refuse(RT_LL_WARN, ERROR_PATH_NOT_FOUND, "device path (not provided)", s, n);
        }
        /* UNSURE: Windows says ERROR_BAD_NETPATH (53) for a server that does not answer; that code is
         * not in rt/win/error.h, and docs/RUNTIME.md 7.1 says ERROR_PATH_NOT_FOUND. */
        return refuse(RT_LL_WARN, ERROR_PATH_NOT_FOUND, "UNC path (not provided)", s, n);
    } else if (n >= 4 && !memcmp(s, "\\??\\", 4)) {
        return refuse(RT_LL_WARN, ERROR_PATH_NOT_FOUND, "NT-namespace path (not provided)", s, n);
    } else if (n >= 2 && is_alpha(s[0]) && s[1] == ':') {
        if (fold(s[0]) != 'c') return refuse(RT_LL_WARN, ERROR_PATH_NOT_FOUND, "no such drive (only C: exists)", s, n);
        if (n >= 3 && is_sep(s[2])) { kind = K_ABS; tail = s + 3; tn = n - 3; }
        else                        { kind = K_DRIVEREL; tail = s + 2; tn = n - 2; }
    } else if (is_sep(s[0])) {
        kind = K_ROOTED; tail = s + 1; tn = n - 1;
    } else {
        kind = K_REL; tail = s; tn = n;
    }

    size_t nc = 0;
    const char *pattern = NULL;
    bool trailing_sep = false;
    char *w = work;

    if (kind == K_RAW) {
        /* ---- 2a. \\?\C:\...: no normalisation at all.  Components are split at '\' only; "." and
         * ".." and empty components are errors (NTFS rejects them: UNSURE whether as
         * ERROR_INVALID_NAME, as chosen, or ERROR_PATH_NOT_FOUND); trailing dots and spaces stay. ---- */
        if (tn == 0) {
            trailing_sep = true;                               /* \\?\C:\ is the root */
        } else {
            memcpy(w, tail, tn);
            w[tn] = 0;
            char *p = w;
            for (;;) {
                char *q = p;
                while (*q && *q != '\\') q++;
                const bool final = *q == 0;
                const size_t sl = (size_t)(q - p);
                if (sl == 0) {
                    if (final) { trailing_sep = true; break; }
                    return refuse(RT_LL_DEBUG, ERROR_INVALID_NAME, "empty component after \\\\?\\", s, n);
                }
                *q = 0;
                if ((sl == 1 && p[0] == '.') || (sl == 2 && p[0] == '.' && p[1] == '.'))
                    return refuse(RT_LL_DEBUG, ERROR_INVALID_NAME, "\".\" or \"..\" after \\\\?\\", s, n);
                if (wild && final) pattern = p; else comps[nc++] = p;
                if (final) break;
                p = q + 1;
            }
        }
    } else {
        /* ---- 2b. normalise as Windows does (RtlGetFullPathName_U; the rules of the Microsoft page
         * "File path formats on Windows systems"): '/' is '\'; runs of separators are one;
         * "." is dropped and ".." removes the component before it (and does nothing at the
         * root); then trailing characters are trimmed.  ---- */
        if (kind == K_REL || kind == K_DRIVEREL) w = cwd_comps(cwd, w, comps, &nc);
        char *tl = w;
        memcpy(tl, tail, tn);
        tl[tn] = 0;
        for (size_t i = 0; i < tn; i++) if (tl[i] == '/') tl[i] = '\\';
        trailing_sep = tn > 0 ? tl[tn - 1] == '\\' : (kind == K_ABS || kind == K_ROOTED);

        Tail t;
        norm_tail(tl, wild, comps, &nc, &t);
        pattern = t.pattern;
        if (t.dot_search) {
            char shown[SHOW_MAX + 8];
            show_utf8(s, n, shown, sizeof shown);
            UNHANDLED("rt_path_to_host: a search whose last component is \"%s\" (what FindFirstFile returns for it "
                      "is not established): \"%s\"", pattern, shown);
        }
    }

    if (wild && !pattern) {
        /* A search that names no pattern ("C:\dir\"): UNSURE whether Windows says ERROR_FILE_NOT_FOUND
         * or ERROR_PATH_NOT_FOUND; nothing is found either way. */
        return refuse(RT_LL_DEBUG, ERROR_FILE_NOT_FOUND, "search path has no file pattern", s, n);
    }

    /* ---- 3. check what is left ---- */
    for (size_t i = 0; i < nc + (pattern ? 1 : 0); i++) {
        const char *c = i < nc ? comps[i] : pattern;
        size_t l = strlen(c);
        int k = name_chars(c, l, c == pattern);
        if (k == 1) return refuse(RT_LL_DEBUG, ERROR_INVALID_NAME, "character not allowed in a file name", s, n);
        if (k == 2) {
            char shown[SHOW_MAX + 8];
            show_utf8(s, n, shown, sizeof shown);
            UNHANDLED("rt_path: ':' in a path component, which is an NTFS alternate data stream (or a bad drive "
                      "prefix); streams are not provided: \"%s\"", shown);
        }
        /* UNSURE: NTFS may report ERROR_INVALID_NAME for a component over 255 units; the header lists
         * ERROR_FILENAME_EXCED_RANGE, which is also what ENAMETOOLONG maps to. */
        if (l > NAME_BYTES || utf16_units(c, l) > NAME_UNITS)
            return refuse(RT_LL_DEBUG, ERROR_FILENAME_EXCED_RANGE, "file name too long", s, n);
    }
    if (kind != K_RAW && !wild && !trailing_sep && nc > 0) {
        /* A DOS device name as the last component, in any directory ("C:\dir\NUL"). */
        switch (reserved_kind(comps[nc - 1])) {
        case 1: return nul_result(flags, host);
        case 2: return refuse(RT_LL_WARN, ERROR_PATH_NOT_FOUND, "DOS device name (not provided)", s, n);
        case 3: {
            char shown[SHOW_MAX + 8];
            show_utf8(s, n, shown, sizeof shown);
            UNHANDLED("rt_path: a DOS device name with an extension as the last component (Windows treats it as the "
                      "device on some versions and as a file on others): \"%s\"", shown);
        }
        default: break;
        }
    }

    if (norm) {
        size_t o = 0;
        memcpy(norm, "C:", 2);
        o = 2;
        if (nc == 0) norm[o++] = '\\';
        for (size_t i = 0; i < nc; i++) {
            size_t l = strlen(comps[i]);
            if (o + 1 + l + 1 > CWD_BUF) return refuse(RT_LL_DEBUG, ERROR_FILENAME_EXCED_RANGE, "path too long", s, n);
            norm[o++] = '\\';
            memcpy(norm + o, comps[i], l);
            o += l;
        }
        norm[o] = 0;
    }

    Parsed P = { .flags = flags, .comps = comps, .nc = nc, .pattern = pattern, .trailing_sep = trailing_sep };
    DWORD rc = to_host(&P, host);
    if (rc) host[0] = 0;
    return rc;
}

static DWORD resolve(const char *s, size_t n, uint32_t flags, bool allow_raw, char host[RT_PATH_MAX], char *norm) {
    char cwd[CWD_BUF];
    cwd_copy(cwd);
    const size_t cwdlen = strlen(cwd);
    const size_t wcap = cwdlen + n + 4, ccap = (cwdlen + n) / 2 + 4;
    char sbuf[1024];
    const char *scomps[64];
    char *work = wcap <= sizeof sbuf ? sbuf : malloc(wcap);
    const char **comps = ccap <= 64 ? scomps : malloc(ccap * sizeof *comps);
    if (!work || !comps) rt_fatal("out of memory translating a path");
    DWORD rc = resolve_in(s, n, flags, allow_raw, host, norm, cwd, work, comps);
    if (work != sbuf) free(work);
    if (comps != scomps) free((void *)comps);
    return rc;
}

static void check_flags(const char *fn, uint32_t flags) {
    if (flags & ~(uint32_t)ALL_FLAGS) UNHANDLED("%s: unknown flag bits %#x in flags %#x", fn, flags & ~(uint32_t)ALL_FLAGS, flags);
    if ((flags & RT_PATH_EXISTING) && (flags & RT_PATH_CREATE))
        UNHANDLED("%s: RT_PATH_EXISTING and RT_PATH_CREATE together (the last component can not both have to exist and be new): flags %#x", fn, flags);
    if ((flags & RT_PATH_WILDCARD) && (flags & (RT_PATH_EXISTING | RT_PATH_CREATE | RT_PATH_DIR | GUARD_FLAGS)))
        UNHANDLED("%s: RT_PATH_WILDCARD with another flag (a pattern is not looked up): flags %#x", fn, flags);
}

/* ---- the two entry points ---------------------------------------------------------------------- */

/* A path the guest gave, as UTF-8 in a buffer that is freed by the caller. */
typedef struct { char *p; size_t n; char buf[1024]; } U8;

static void u8_release(U8 *u) { if (u->p != u->buf) free(u->p); }

static char *u8_alloc(U8 *u, size_t units) {
    size_t need = 3 * units + 1;
    u->p = need <= sizeof u->buf ? u->buf : malloc(need);
    if (!u->p) rt_fatal("out of memory translating a path");
    return u->p;
}

/* Convert the W string (NUL-terminated).  Returns 0 and fills `u`, or a Win32 error. */
static DWORD from_w(const WCHAR *win, U8 *u) {
    size_t n = 0;
    while (n <= WIN_UNITS && win[n]) n++;
    if (n > WIN_UNITS) {
        rt_log(RT_CH_FILE, RT_LL_DEBUG, "path: a path of more than %u UTF-16 units (Win32 error %u)", WIN_UNITS, ERROR_FILENAME_EXCED_RANGE);
        return ERROR_FILENAME_EXCED_RANGE;
    }
    char *p = u8_alloc(u, n);
    size_t bad = 0;
    u->n = w_to_utf8(win, n, p, &bad);
    if (u->n == SIZE_MAX) {
        char shown[SHOW_MAX + 8];
        show_w(win, n, shown, sizeof shown);
        UNHANDLED("rt_path: unpaired UTF-16 surrogate U+%04X at index %zu (Windows allows it in a name, a UTF-8 host path can not "
                  "hold it): \"%s\"", (unsigned)win[bad], bad, shown);
    }
    return 0;
}

static DWORD from_a(const char *a, U8 *u) {
    size_t n = 0;
    while (n <= WIN_UNITS && a[n]) n++;
    if (n > WIN_UNITS) {
        rt_log(RT_CH_FILE, RT_LL_DEBUG, "path: a path of more than %u bytes (Win32 error %u)", WIN_UNITS, ERROR_FILENAME_EXCED_RANGE);
        return ERROR_FILENAME_EXCED_RANGE;
    }
    u->p = u8_alloc(u, n);
    u->n = a_to_utf8(a, n, u->p);
    return 0;
}

DWORD rt_path_to_host_w(const WCHAR *win, uint32_t flags, char host[RT_PATH_MAX]) {
    need_ready("rt_path_to_host_w");
    check_flags("rt_path_to_host_w", flags);
    if (!host) UNHANDLED("rt_path_to_host_w: no output buffer");
    host[0] = 0;
    /* A NULL name comes from a guest that passed NULL; Windows reports ERROR_INVALID_PARAMETER for
     * most calls (CreateFile has said ERROR_PATH_NOT_FOUND on some versions: UNSURE). */
    if (!win) return ERROR_INVALID_PARAMETER;
    U8 u;
    DWORD rc = from_w(win, &u);
    if (rc) return rc;
    rc = resolve(u.p, u.n, flags, true, host, NULL);
    u8_release(&u);
    return rc;
}

DWORD rt_path_to_host_a(const char *win, uint32_t flags, char host[RT_PATH_MAX]) {
    need_ready("rt_path_to_host_a");
    check_flags("rt_path_to_host_a", flags);
    if (!host) UNHANDLED("rt_path_to_host_a: no output buffer");
    host[0] = 0;
    if (!win) return ERROR_INVALID_PARAMETER;
    U8 u;
    DWORD rc = from_a(win, &u);
    if (rc) return rc;
    rc = resolve(u.p, u.n, flags, true, host, NULL);
    u8_release(&u);
    return rc;
}

/* ---- the current directory: get and set --------------------------------------------------------------- */

DWORD rt_path_get_cwd_w(WCHAR *out, DWORD cch) {
    need_ready("rt_path_get_cwd_w");
    if (!out && cch > 0) UNHANDLED("rt_path_get_cwd_w: no buffer for %u units", (unsigned)cch);
    char cwd[CWD_BUF];
    cwd_copy(cwd);
    long units = utf8_to_utf16(cwd, strlen(cwd), NULL);
    if (units < 0) rt_fatal("rt_path_get_cwd_w: the current directory is not well-formed UTF-8");
    /* GetCurrentDirectoryW: the length without the terminator if the buffer holds it, else the
     * size needed with it.  A buffer that is too small is left with an empty string (Windows leaves
     * it alone; nothing can depend on that). */
    if ((uint64_t)cch >= (uint64_t)units + 1) {
        utf8_to_utf16(cwd, strlen(cwd), out);
        out[units] = 0;
        return (DWORD)units;
    }
    if (cch > 0) out[0] = 0;
    return (DWORD)units + 1;
}

/* ---- the known directories ------------------------------------------------------------------ */

/* ASCII, each inside the tree of node_def.  Only the temp directory ends in a backslash. */
static const char *known_text(RtKnownPath which, const char *fn) {
    switch (which) {
    case RT_KNOWN_WINDOWS:       return "C:\\Windows";
    case RT_KNOWN_SYSTEM:        return "C:\\Windows\\system32";
    case RT_KNOWN_TEMP:          return "C:\\Users\\Player\\AppData\\Local\\Temp\\";
    case RT_KNOWN_PROFILE:       return "C:\\Users\\Player";
    case RT_KNOWN_DOCUMENTS:     return "C:\\Users\\Player\\Documents";
    case RT_KNOWN_LOCAL_APPDATA: return "C:\\Users\\Player\\AppData\\Local";
    case RT_KNOWN_APPDATA:       return "C:\\Users\\Player\\AppData\\Roaming";
    }
    rt_fatal("%s: %d is not a known directory", fn, (int)which);
}

DWORD rt_path_known_w(RtKnownPath which, WCHAR *out, DWORD cch) {
    const char *text = known_text(which, "rt_path_known_w");
    const size_t n = strlen(text);
    if (cch < n + 1) return (DWORD)n + 1;
    if (!out) rt_fatal("rt_path_known_w: no buffer for %u units", (unsigned)cch);
    for (size_t i = 0; i <= n; i++) out[i] = (WCHAR)(unsigned char)text[i];
    return (DWORD)n;
}

DWORD rt_path_known_a(RtKnownPath which, char *out, DWORD cch) {
    const char *text = known_text(which, "rt_path_known_a");
    const size_t n = strlen(text);
    if (cch < n + 1) return (DWORD)n + 1;
    if (!out) rt_fatal("rt_path_known_a: no buffer for %u bytes", (unsigned)cch);
    memcpy(out, text, n + 1);
    return (DWORD)n;
}

DWORD rt_path_set_cwd_w(const WCHAR *win) {
    need_ready("rt_path_set_cwd_w");
    if (!win) return ERROR_INVALID_PARAMETER;
    U8 u;
    DWORD rc = from_w(win, &u);
    if (rc) return rc;
    char host[RT_PATH_MAX], norm[CWD_BUF];
    /* The directory has to exist: a missing one is ERROR_PATH_NOT_FOUND (what cmd's cd reports), a file
     * is ERROR_DIRECTORY ("The directory name is invalid"). */
    rc = resolve(u.p, u.n, RT_PATH_EXISTING | RT_PATH_DIR, false, host, norm);
    u8_release(&u);
    if (rc) return rc;
    size_t hl = strlen(host);
    while (hl > 1 && host[hl - 1] == '/') host[--hl] = 0;      /* a trailing slash would turn "it is a file" into ENOTDIR */
    struct stat st;
    if (stat(host, &st) != 0) return map_dir_errno(errno);
    if (!S_ISDIR(st.st_mode)) return ERROR_DIRECTORY;
    pthread_mutex_lock(&g_cwd_lock);
    memcpy(g_cwd, norm, CWD_BUF);
    pthread_mutex_unlock(&g_cwd_lock);
    return 0;
}

/* ---- the full path as text: GetFullPathName ------------------------------------------------------------ */

#define PART_NONE  SIZE_MAX

#define FULL_UNHANDLED(what) do {                                                              \
        char shown_[SHOW_MAX + 8];                                                             \
        show_utf8(s, n, shown_, sizeof shown_);                                                \
        UNHANDLED("rt_path_full_w: " what ": \"%s\"", shown_);                                 \
    } while (0)

/* rt_path_full_w on the UTF-8 form s[0..n) (NUL-terminated).  `work` has room for
 * strlen(cwd) + n + 4 bytes, `comps` for (strlen(cwd) + n) / 2 + 4 entries and `o`, which
 * receives the result, for strlen(cwd) + n + 8 bytes.  *part is the offset in `o` of the last
 * component, or PART_NONE.  The rules are in rt/path.h and nowhere else. */
static DWORD full_in(const char *s, size_t n, const char *cwd, char *work, const char **comps,
                     char *o, size_t *olen, size_t *part) {
    size_t sp = n;
    while (sp && s[sp - 1] == ' ') sp--;
    if (sp == 0) return refuse(RT_LL_DEBUG, ERROR_INVALID_NAME, "GetFullPathName of an empty name", s, n);

    size_t ol, nc = 0;
    const char *tail;
    bool dos = true;                    /* a DOS device name as the last component is the device */
    char *w = work;

    if (is_sep(s[0]) && is_sep(s[1])) {
        dos = false;
        o[0] = o[1] = '\\';
        if ((s[2] == '.' || s[2] == '?') && (n == 3 || is_sep(s[3]))) {
            if (n == 3) FULL_UNHANDLED("\"\\\\.\" or \"\\\\?\" with nothing after it (what GetFullPathName returns is not established)");
            o[2] = s[2];
            o[3] = '\\';
            ol = 4;
            tail = s + 4;
        } else {
            if (n == 2 || is_sep(s[2])) FULL_UNHANDLED("a UNC path without a server name (what GetFullPathName returns is not established)");
            size_t i = 2;
            ol = 2;
            for (int k = 0; k < 2 && i < n; k++) {             /* the server, then the share: copied, not normalised */
                while (i < n && !is_sep(s[i])) o[ol++] = s[i++];
                if (i < n) {
                    o[ol++] = '\\';
                    while (i < n && is_sep(s[i])) i++;
                }
            }
            tail = s + i;
        }
    } else if (is_alpha(s[0]) && s[1] == ':' && (is_sep(s[2]) || fold(s[0]) != 'c')) {
        /* X:\rest on any drive, and X:rest on a drive that is not C: (its current directory is its root). */
        o[0] = s[0];
        o[1] = ':';
        o[2] = '\\';
        ol = 3;
        tail = s + (is_sep(s[2]) ? 3 : 2);
    } else {
        memcpy(o, "C:\\", 3);
        ol = 3;
        if (is_sep(s[0])) {
            tail = s + 1;                                      /* \rest: the root of the current drive */
        } else {
            w = cwd_comps(cwd, w, comps, &nc);                 /* rest, C:rest: the current directory */
            tail = is_alpha(s[0]) && s[1] == ':' ? s + 2 : s;
        }
    }

    const size_t tn = (size_t)(s + n - tail);
    char *tl = w;
    memcpy(tl, tail, tn);
    tl[tn] = 0;
    for (size_t i = 0; i < tn; i++) if (tl[i] == '/') tl[i] = '\\';
    const bool tail_sep = tn > 0 && tl[tn - 1] == '\\';      /* asked before norm_tail cuts `tl` up */
    Tail t;
    norm_tail(tl, false, comps, &nc, &t);
    /* The separator in front of a last segment that was trimmed away stays ("C:\a\..." is "C:\a\"). */
    const bool ends_sep = tail_sep || t.emptied;

    *part = PART_NONE;
    if (dos && !ends_sep && nc > 0) {
        const int k = reserved_kind(comps[nc - 1]);
        if (k == 3) FULL_UNHANDLED("a DOS device name followed by '.' or ':' as the last component (Windows treats it "
                                   "as the device on some versions and as a file on others)");
        if (k) {
            const size_t l = strlen(comps[nc - 1]);
            memcpy(o, "\\\\.\\", 4);
            memcpy(o + 4, comps[nc - 1], l + 1);
            *olen = 4 + l;
            return 0;
        }
    }
    for (size_t i = 0; i < nc; i++) {                          /* `o` ends in a separator whenever components follow it */
        const size_t l = strlen(comps[i]);
        if (i) o[ol++] = '\\';
        if (i + 1 == nc && !ends_sep) *part = ol;
        memcpy(o + ol, comps[i], l);
        ol += l;
    }
    if (ends_sep && nc > 0) o[ol++] = '\\';
    o[ol] = 0;
    *olen = ol;
    return 0;
}

DWORD rt_path_full_w(const WCHAR *win, WCHAR *out, DWORD cch, DWORD *len, DWORD *file_part) {
    need_ready("rt_path_full_w");
    if (!len) UNHANDLED("rt_path_full_w: no place for the length");
    if (!out && cch > 0) UNHANDLED("rt_path_full_w: no buffer for %u units", (unsigned)cch);
    *len = 0;
    if (file_part) *file_part = RT_PATH_NO_PART;
    if (!win) return ERROR_INVALID_NAME;
    U8 u;
    DWORD rc = from_w(win, &u);
    if (rc) return rc;

    char cwd[CWD_BUF];
    cwd_copy(cwd);
    const size_t cwdlen = strlen(cwd);
    const size_t cap = cwdlen + u.n + 8, ccap = (cwdlen + u.n) / 2 + 4;
    char sbuf[2048];
    const char *scomps[64];
    char *mem = 2 * cap <= sizeof sbuf ? sbuf : malloc(2 * cap);     /* the work area, then the result */
    const char **comps = ccap <= 64 ? scomps : malloc(ccap * sizeof *comps);
    if (!mem || !comps) rt_fatal("out of memory normalising a path");
    char *o = mem + cap;
    size_t ol = 0, part = PART_NONE;
    rc = full_in(u.p, u.n, cwd, mem, comps, o, &ol, &part);
    if (!rc) {
        const long units = utf8_to_utf16(o, ol, NULL);
        if (units < 0) rt_fatal("rt_path_full_w: the result is not well-formed UTF-8");
        if (units > WIN_UNITS) {
            rc = refuse(RT_LL_DEBUG, ERROR_FILENAME_EXCED_RANGE, "GetFullPathName: the full path is too long", u.p, u.n);
        } else if ((uint64_t)cch >= (uint64_t)units + 1) {
            utf8_to_utf16(o, ol, out);
            out[units] = 0;
            *len = (DWORD)units;
            if (file_part && part != PART_NONE) *file_part = (DWORD)utf8_to_utf16(o, part, NULL);
        } else {
            *len = (DWORD)units + 1;                           /* too small: the size needed, nothing written */
        }
    }
    if (mem != sbuf) free(mem);
    if (comps != scomps) free((void *)comps);
    u8_release(&u);
    return rc;
}

/* ---- host path to Windows path ------------------------------------------------------------------------ */

/* Collapse "//", "." and ".." of an absolute host path, textually.  `out` is "/" or "/a/b".
 * Symbolic links are not looked at, so "link/.." is taken to be the directory that holds "link". */
static bool lex_norm(const char *in, char *out, size_t cap) {
    size_t o = 0;
    out[0] = 0;
    for (const char *p = in; *p;) {
        while (*p == '/') p++;
        if (!*p) break;
        const char *q = p;
        while (*q && *q != '/') q++;
        size_t l = (size_t)(q - p);
        if (l == 1 && p[0] == '.') {
            /* nothing */
        } else if (l == 2 && p[0] == '.' && p[1] == '.') {
            while (o > 0 && out[o - 1] != '/') o--;
            if (o > 0) o--;
            out[o] = 0;
        } else {
            if (o + 1 + l + 1 > cap) return false;
            out[o++] = '/';
            memcpy(out + o, p, l);
            o += l;
            out[o] = 0;
        }
        p = q;
    }
    if (o == 0) {
        if (cap < 2) return false;
        strcpy(out, "/");
    }
    return true;
}

int rt_path_to_win_w(const char *host, WCHAR *out, int cch) {
    need_ready("rt_path_to_win_w");
    if (!host) UNHANDLED("rt_path_to_win_w: no host path");
    if (cch < 0) UNHANDLED("rt_path_to_win_w: negative buffer size %d", cch);
    if (!out && cch > 0) UNHANDLED("rt_path_to_win_w: no buffer for %d units", cch);
    /* A path that is not absolute, or is too long to have come from rt_path_to_host_*, is under
     * no root. */
    if (host[0] != '/' || strlen(host) >= RT_PATH_MAX) return 0;
    char lex[RT_PATH_MAX];
    if (!lex_norm(host, lex, sizeof lex)) return 0;

    /* The mapped root with the longest name that is a prefix of the path.  A root may be spelled
     * two ways (as configured and as realpath() gives it, which is what F_GETPATH reports);
     * either matches. */
    int best = -1;
    size_t best_len = 0;
    for (int i = 0; i < N_COUNT; i++) {
        const char *spell[2] = { g.nodes[i].host, g.nodes[i].real };
        for (int k = 0; k < 2; k++) {
            size_t l = strlen(spell[k]);
            if (!l || l < best_len) continue;
            if (strncmp(lex, spell[k], l) == 0 && (lex[l] == 0 || lex[l] == '/')) { best = i; best_len = l; }
        }
    }
    if (best < 0) return 0;
    const Node *nd = &g.nodes[best];

    /* Components below the node.  They must be well-formed UTF-8, and names the guest can write
     * in an ordinary path: otherwise the guest could not name this file, and translating the
     * result forward would not reproduce the host path. */
    const char *rest = lex + best_len;
    const char *wc[RT_PATH_MAX / 2 + 8];
    char names[RT_PATH_MAX];
    size_t nw = 0, no = 0;
    for (size_t k = 0; k < nd->ncomp; k++) wc[nw++] = nd->comp[k];
    while (*rest) {
        rest++;                                              /* the '/' */
        const char *e = rest;
        while (*e && *e != '/') e++;
        size_t l = (size_t)(e - rest);
        if (!nd->mount) return 0;                            /* a virtual directory holds only placeholders */
        memcpy(names + no, rest, l);
        names[no + l] = 0;
        if (utf8_to_utf16(names + no, l, NULL) < 0) {
            char shown[SHOW_MAX + 8];
            show_utf8(host, strlen(host), shown, sizeof shown);
            UNHANDLED("rt_path_to_win_w: a host name that is not well-formed UTF-8 can not be turned into UTF-16: \"%s\"", shown);
        }
        if (!name_is_plain(names + no)) return 0;
        wc[nw++] = names + no;
        no += l + 1;
        rest = e;
    }
    /* Shadowed: a file under the home tree at AppData/Local/Temp, say, is not what C:\Users\Player\
     * AppData\Local\Temp\... reaches. */
    if (find_node(wc, nw) != best) return 0;

    char win[RT_PATH_MAX * 2];
    size_t o = 0;
    memcpy(win, "C:", 2);
    o = 2;
    if (nw == 0) win[o++] = '\\';
    for (size_t k = 0; k < nw; k++) {
        size_t l = strlen(wc[k]);
        if (o + 1 + l + 1 > sizeof win) return 0;
        win[o++] = '\\';
        memcpy(win + o, wc[k], l);
        o += l;
    }
    win[o] = 0;

    long units = utf8_to_utf16(win, o, NULL);
    if ((int64_t)cch >= (int64_t)units + 1) {
        utf8_to_utf16(win, o, out);
        out[units] = 0;
        return (int)units;
    }
    if (cch > 0) out[0] = 0;
    return (int)units + 1;
}

/* ---- start-up --------------------------------------------------------------------------------------- */

static void ensure_dir(const char *path) {
    if (mkdir(path, 0755) == 0) return;
    struct stat st;
    if (errno == EEXIST && stat(path, &st) == 0 && S_ISDIR(st.st_mode)) return;
    rt_fatal("rt_path_init: can not create the directory %s: %s", path, errno == EEXIST ? "it exists and is not a directory" : strerror(errno));
}

static void set_host(Node *nd, const char *dir, const char *what) {
    if (!lex_norm(dir, nd->host, sizeof nd->host) || !strcmp(nd->host, "/"))
        rt_fatal("rt_path_init: %s %s is not usable as a root", what, dir);
    nd->real[0] = 0;
}

/* The second spelling of a root: realpath() of it, if that differs (the directory must exist). */
static void set_real(Node *nd) {
    char real[PATH_MAX];
    nd->real[0] = 0;
    if (realpath(nd->host, real) && strcmp(real, nd->host) != 0) {
        char norm[RT_PATH_MAX];
        if (lex_norm(real, norm, sizeof norm)) strcpy(nd->real, norm);
    }
}

void rt_path_init(void) {
    const RtConfig *cfg = rt_config();
    const char *what_state = "state_dir", *what_install = "install_root";
    if (!cfg->state_dir || cfg->state_dir[0] != '/') rt_fatal("rt_path_init: %s must be an absolute path, got %s", what_state, cfg->state_dir ? cfg->state_dir : "NULL");
    if (!cfg->install_root || cfg->install_root[0] != '/') rt_fatal("rt_path_init: %s must be an absolute path, got %s", what_install, cfg->install_root ? cfg->install_root : "NULL");

    char state[RT_PATH_MAX];
    if (!lex_norm(cfg->state_dir, state, sizeof state) || !strcmp(state, "/")) rt_fatal("rt_path_init: state_dir %s is not usable as a root", cfg->state_dir);
    struct stat st;
    if (stat(state, &st) != 0 || !S_ISDIR(st.st_mode)) rt_fatal("rt_path_init: state_dir %s is not a directory that exists", cfg->state_dir);
    if (stat(cfg->install_root, &st) != 0 || !S_ISDIR(st.st_mode)) rt_fatal("rt_path_init: install_root %s is not a directory that exists", cfg->install_root);

    pthread_mutex_lock(&g_cache_lock);
    for (int i = 0; i < LISTING_SLOTS; i++) listing_clear(&g_listing[i]);
    atomic_store(&g_reads, 0);
    pthread_mutex_unlock(&g_cache_lock);
    atomic_store(&g.force, -1);

    /* The host directory of each node. */
    char buf[RT_PATH_MAX];
#define SUB(base, rel) (snprintf(buf, sizeof buf, "%s/%s", (base), (rel)) >= (int)sizeof buf ? \
                        (rt_fatal("rt_path_init: %s/%s is too long", (base), (rel)), (const char *)NULL) : buf)
    for (int i = 0; i < N_COUNT; i++) {
        Node *nd = &g.nodes[i];
        nd->wname = node_def[i].wname;
        nd->ncomp = node_def[i].ncomp;
        nd->mount = node_def[i].mount;
        for (size_t k = 0; k < 5; k++) nd->comp[k] = node_def[i].comp[k];
        nd->probe_cs = false;
    }
    set_host(&g.nodes[N_INSTALL], cfg->install_root, "install_root");
    set_host(&g.nodes[N_HOME], SUB(state, "home"), "the home directory");
    set_host(&g.nodes[N_TEMP], SUB(state, "tmp"), "the temp directory");
    set_host(&g.nodes[N_ROOT], SUB(state, "vfs"), "the virtual root");
    set_host(&g.nodes[N_USERS], SUB(state, "vfs/Users"), "the virtual Users directory");
    set_host(&g.nodes[N_WINDOWS], SUB(state, "vfs/Windows"), "the virtual Windows directory");
    set_host(&g.nodes[N_SYS32], SUB(state, "vfs/Windows/system32"), "the virtual system32 directory");

    /* The tree the guest expects to find (section 7.1).  Virtual directories are empty. */
    static const char *const dirs[] = {
        "home", "home/Documents", "home/AppData", "home/AppData/Local", "home/AppData/Roaming", "home/Saved Games",
        "home/AppData/Local/Temp",                       /* placeholder: lists as a child of Local; C:\...\Temp is <state>/tmp */
        "tmp", "vfs", "vfs/Titanfall2", "vfs/Users", "vfs/Users/Player", "vfs/Windows", "vfs/Windows/system32",
    };
    for (size_t i = 0; i < sizeof dirs / sizeof *dirs; i++) ensure_dir(SUB(state, dirs[i]));
#undef SUB
    for (int i = 0; i < N_COUNT; i++) set_real(&g.nodes[i]);

    /* Ask each real volume whether it is case-sensitive (section 7.2).  No answer is fatal. */
    for (int i = 0; i < N_COUNT; i++) {
        Node *nd = &g.nodes[i];
        if (!nd->mount) continue;
        errno = 0;
        long r = pathconf(nd->host, _PC_CASE_SENSITIVE);
        if (r < 0) rt_fatal("rt_path_init: can not tell whether the volume of %s is case-sensitive: %s", nd->host, strerror(errno));
        nd->probe_cs = r != 0;
        rt_log(RT_CH_FILE, RT_LL_INFO, "path: %s -> %s (%s)", nd->wname, nd->host, nd->probe_cs ? "case-sensitive volume" : "case-insensitive volume");
    }

    pthread_mutex_lock(&g_cwd_lock);
    strcpy(g_cwd, "C:\\Titanfall2");
    pthread_mutex_unlock(&g_cwd_lock);
    g.ready = true;
}

/* ---- hooks for the tests (not in rt/path.h; a test declares what it uses) ---------------------------------- */

/* Override the answer of the volume probe for every mount: -1 uses the probe, 0 says "case-insensitive",
 * 1 says "case-sensitive".  Drops every cached listing.  The macOS volume of a developer machine is
 * case-insensitive, so this is how the folded lookup is tested. */
void rt_path_test_force_case(int mode) {
    need_ready("rt_path_test_force_case");
    atomic_store(&g.force, mode < 0 ? -1 : (mode ? 1 : 0));
    pthread_mutex_lock(&g_cache_lock);
    for (int i = 0; i < LISTING_SLOTS; i++) listing_clear(&g_listing[i]);
    pthread_mutex_unlock(&g_cache_lock);
}

/* Directory reads since start-up, and listings cached now. */
void rt_path_test_stats(unsigned *reads, unsigned *cached) {
    pthread_mutex_lock(&g_cache_lock);
    if (reads) *reads = atomic_load(&g_reads);
    if (cached) {
        unsigned c = 0;
        for (int i = 0; i < LISTING_SLOTS; i++) c += g_listing[i].dir != NULL;
        *cached = c;
    }
    pthread_mutex_unlock(&g_cache_lock);
}

/* Install a listing for host directory `dir` that stands in for the real one until something drops it,
 * so that the choice between an exact and a folded match (which a case-insensitive volume can not
 * be made to show) is tested.  `names` need not be sorted. */
void rt_path_test_fake_listing(const char *dir, const char *const *names, size_t n) {
    need_ready("rt_path_test_fake_listing");
    pthread_mutex_lock(&g_cache_lock);
    Listing *l = NULL;
    for (int i = 0; i < LISTING_SLOTS && !l; i++)
        if (g_listing[i].dir && !strcmp(g_listing[i].dir, dir)) l = &g_listing[i];
    for (int i = 0; i < LISTING_SLOTS && !l; i++)
        if (!g_listing[i].dir) l = &g_listing[i];
    if (!l) l = &g_listing[0];
    listing_clear(l);
    l->dir = strdup(dir);
    l->names = malloc((n ? n : 1) * sizeof *l->names);
    if (!l->dir || !l->names) rt_fatal("out of memory");
    for (size_t i = 0; i < n; i++) {
        l->names[i] = strdup(names[i]);
        if (!l->names[i]) rt_fatal("out of memory");
    }
    l->n = n;
    qsort(l->names, n, sizeof *l->names, cmp_entry);
    l->settled = true;
    l->fake = true;
    l->stamp = ++g_stamp;
    pthread_mutex_unlock(&g_cache_lock);
}

/* Whether the probe found the volume of mount `win_root` (a node's Windows path) case-sensitive: the
 * answer without the override. */
int rt_path_test_probe(const char *win_root) {
    need_ready("rt_path_test_probe");
    for (int i = 0; i < N_COUNT; i++)
        if (g.nodes[i].mount && ieq(g.nodes[i].wname, win_root)) return g.nodes[i].probe_cs;
    return -1;
}
