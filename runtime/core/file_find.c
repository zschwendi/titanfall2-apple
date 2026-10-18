/* Find-file: FindFirstFile, FindNextFile, FindClose (docs/RUNTIME.md section 7.3).
 * Specification: rt/file.h.  Shared internals: core/file_priv.h (all built, in core/file.c).
 *
 * BUILT: the piece `rt-file-find` (docs/handoff/rt-contract-file.md has its task, its test
 * plan and its brief).  The bodies below are the find-file body; the state below is its own.
 * The includes and the signatures are the placeholder's, and no other source file is edited.
 *
 * STATE: per handle only, the object below.  Its `lock` guards `next`; nothing is called
 * under it.  The entries are complete before the handle exists and never change.
 * The ops of the kind must have .name = "find", .destroy (frees `entries`) and
 * .no_close_handle = true.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <os/lock.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "file_priv.h"

#include "rt/locale.h"

/* The object behind a handle of kind RT_K_FIND. */
typedef struct RtFind {
    RtObject          obj;
    os_unfair_lock    lock;         /* guards `next` */
    WIN32_FIND_DATAW *entries;      /* malloc: the matches, in the order of rt/file.h */
    uint32_t          count;
    uint32_t          next;         /* index of the entry the next FindNextFile returns */
} RtFind;

/* ---- the pattern and the names (rt/file.h, rt_find_first_w) ------------------------------------ */

static uint8_t fold8(uint8_t c) { return (c >= 'a' && c <= 'z') ? (uint8_t)(c - 32) : c; }
static WCHAR   foldw(WCHAR c)   { return (c >= 'a' && c <= 'z') ? (WCHAR)(c - 32) : c; }

/* A `*` stands for any run of characters, empty and with periods included; ASCII letters
 * compare without case, every other byte exactly. */
static bool glob_match(const char *p, const char *n) {
    const char *star = NULL, *star_n = NULL;
    while (*n) {
        if (*p == '*') {
            star = p++;
            star_n = n;
        } else if (*p && fold8((uint8_t)*p) == fold8((uint8_t)*n)) {
            p++;
            n++;
        } else if (star) {
            p = star + 1;
            n = ++star_n;
        } else {
            return false;
        }
    }
    while (*p == '*') p++;
    return *p == '\0';
}

/* "*.*" matches every name, one without a period too. */
static bool name_matches(const char *pat, const char *name) {
    return strcmp(pat, "*.*") == 0 || glob_match(pat, name);
}

/* Step 2: the patterns that STOP (DOS matching rules that are not verified). */
static bool pattern_stops(const char *pat) {
    const size_t len = strlen(pat);
    if (len == 0) return true;
    const bool dotstar = len >= 2 && pat[len - 2] == '.' && pat[len - 1] == '*' && strcmp(pat, "*.*") != 0;
    return strpbrk(pat, "?<>\"") != NULL || pat[len - 1] == '.' || dotstar;
}

/* Step 1: the host directory ("/" for the root) and the pattern, the last component, from the
 * guest's name translated with RT_PATH_WILDCARD.  Returns 0 or the translation's error. */
static DWORD host_pattern(RtName name, char dir[RT_PATH_MAX], char pat[RT_PATH_MAX]) {
    char host[RT_PATH_MAX];
    const DWORD e = rt_file_name_to_host(name, RT_PATH_WILDCARD, host);
    if (e) return e;
    const char *slash = strrchr(host, '/');
    if (!slash) rt_fatal("rt_find_first: the host path %s has no '/'", host);
    strcpy(pat, slash + 1);
    const size_t n = slash == host ? 1 : (size_t)(slash - host);
    memcpy(dir, host, n);
    dir[n] = '\0';
    return 0;
}

/* A host name as WCHARs, NUL included: false if it is not valid UTF-8 or has more than 259. */
static bool host_name_to_wide(const char *name, WCHAR out[260]) {
    const int n = rt_mb_to_wide(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, NULL, 0);
    if (n <= 0 || n > 260) return false;
    return rt_mb_to_wide(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, out, n) == n;
}

/* The order of rt/file.h: "." first, then "..", then the others by name, comparing UTF-16
 * units with a-z folded to A-Z, and by the unfolded units where that ties. */
static int entry_rank(const WIN32_FIND_DATAW *d) {
    if (d->cFileName[0] == '.' && d->cFileName[1] == 0) return 0;
    if (d->cFileName[0] == '.' && d->cFileName[1] == '.' && d->cFileName[2] == 0) return 1;
    return 2;
}

static int entry_cmp(const void *a, const void *b) {
    const WIN32_FIND_DATAW *x = a, *y = b;
    const int rx = entry_rank(x), ry = entry_rank(y);
    if (rx != ry) return rx < ry ? -1 : 1;
    for (unsigned pass = 0; pass < 2; pass++) {
        for (unsigned i = 0; ; i++) {
            WCHAR cx = x->cFileName[i], cy = y->cFileName[i];
            if (pass == 0) { cx = foldw(cx); cy = foldw(cy); }
            if (cx != cy) return cx < cy ? -1 : 1;
            if (cx == 0) break;
        }
    }
    return 0;
}

/* One matching host entry as a WIN32_FIND_DATAW (zeroed, then the fields of rt/file.h), or
 * false: dropped, and logged, if its stat fails, it is neither a file nor a directory, or its
 * name is not UTF-8 or too long. */
static bool fill_entry_as(int dfd, const char *host_name, const char *name, WIN32_FIND_DATAW *e);
static bool fill_entry(int dfd, const char *name, WIN32_FIND_DATAW *e) { return fill_entry_as(dfd, name, name, e); }

/* The same for the host entry `host_name` of the directory `dfd`, shown under `name` (a file
 * that is only partly on this machine: its bytes are kept under another name). */
static bool fill_entry_as(int dfd, const char *host_name, const char *name, WIN32_FIND_DATAW *e) {
    struct stat st;
    WCHAR w[260] = { 0 };
    if (fstatat(dfd, host_name, &st, 0) || !(S_ISREG(st.st_mode) || S_ISDIR(st.st_mode))) {
        rt_log(RT_CH_FILE, RT_LL_WARN, "find: %s is dropped: it cannot be statted, or is neither a file nor a "
               "directory", name);
        return false;
    }
    if (!host_name_to_wide(name, w)) {
        rt_log(RT_CH_FILE, RT_LL_WARN, "find: %s is dropped: its name is not UTF-8 or is longer than 259 "
               "characters", name);
        return false;
    }
    ULONGLONG size = 0;
    memset(e, 0, sizeof *e);
    rt_file_attr_from_stat(&st, &e->dwFileAttributes, &e->ftCreationTime, &e->ftLastAccessTime,
                           &e->ftLastWriteTime, &size);
    e->nFileSizeHigh = (DWORD)(size >> 32);
    e->nFileSizeLow = (DWORD)size;
    memcpy(e->cFileName, w, sizeof w);
    return true;
}

typedef struct { const char *pat; WIN32_FIND_DATAW *list; uint32_t n, cap; DWORD err; } Scan;

static void scan_add(Scan *sc, const WIN32_FIND_DATAW *e) {
    if (sc->err) return;
    if (sc->n == sc->cap) {
        const uint32_t grow = sc->cap ? sc->cap * 2 : 16;
        WIN32_FIND_DATAW *g = realloc(sc->list, grow * sizeof *g);
        if (!g) { sc->err = ERROR_NOT_ENOUGH_MEMORY; return; }
        sc->list = g;
        sc->cap = grow;
    }
    sc->list[sc->n++] = *e;
}

/* rt_partial_list's callback: a file of this directory that is only partly on this machine. */
static void scan_add_partial(void *cookie, int dfd, const char *store_name, const char *name) {
    Scan *sc = cookie;
    WIN32_FIND_DATAW e;
    if (name_matches(sc->pat, name) && fill_entry_as(dfd, store_name, name, &e)) scan_add(sc, &e);
}

/* Reads the host directory `dir` once: the matches, sorted.  Returns 0 and the array (NULL if
 * there are none), or the Win32 error: a directory call failed (mapped), or no memory. */
static DWORD scan_dir(const char *dir, const char *pat, WIN32_FIND_DATAW **list_out, uint32_t *count_out) {
    DIR *d = opendir(dir);
    if (!d) return rt_win32_from_errno(errno, RT_ERR_DIR);
    Scan sc = { .pat = pat };
    const int dfd = dirfd(d);
    for (;;) {
        errno = 0;
        struct dirent *de = readdir(d);
        if (!de) {
            if (errno) sc.err = rt_win32_from_errno(errno, RT_ERR_DIR);
            break;
        }
        if (!name_matches(pat, de->d_name)) continue;
        WIN32_FIND_DATAW e;
        if (!fill_entry(dfd, de->d_name, &e)) continue;
        scan_add(&sc, &e);
        if (sc.err) break;
    }
    closedir(d);
    if (!sc.err) rt_partial_list(dir, scan_add_partial, &sc);     /* game data only partly here */
    if (sc.err) { free(sc.list); return sc.err; }
    if (sc.n) qsort(sc.list, sc.n, sizeof *sc.list, entry_cmp);
    *list_out = sc.list;
    *count_out = sc.n;
    return 0;
}

/* ---- the handle ----------------------------------------------------------------------------- */

static void find_destroy(RtObject *obj) { free(((RtFind *)obj)->entries); }

static const RtObjectOps find_ops = { .name = "find", .destroy = find_destroy, .no_close_handle = true };

/* The find object behind `h`, referenced: NULL, with ERROR_INVALID_HANDLE set by rt_handle_ref. */
static RtFind *find_ref(HANDLE h) { return (RtFind *)rt_handle_ref(h, RT_K_FIND); }

/* The A form of one entry: its name in code page 1252 without best fit.  False if a character
 * has no byte of its own or the name is longer than 259 bytes (the caller STOPS). */
static bool entry_to_a(const WIN32_FIND_DATAW *w, WIN32_FIND_DATAA *a) {
    char name[260] = { 0 };
    BOOL used = FALSE;
    const int n = rt_wide_to_mb(1252, WC_NO_BEST_FIT_CHARS, w->cFileName, -1, name, sizeof name, NULL, &used);
    if (n <= 0 || n > 260 || used) return false;
    memset(a, 0, sizeof *a);
    a->dwFileAttributes = w->dwFileAttributes;
    a->ftCreationTime = w->ftCreationTime;
    a->ftLastAccessTime = w->ftLastAccessTime;
    a->ftLastWriteTime = w->ftLastWriteTime;
    a->nFileSizeHigh = w->nFileSizeHigh;
    a->nFileSizeLow = w->nFileSizeLow;
    memcpy(a->cFileName, name, sizeof name);
    return true;
}

/* The body of rt_find_first_w and _a (`wide` names the function for the stops).  The handle
 * exists only with its entries; *out is written only on success. */
static HANDLE find_first(bool wide, RtName name, WIN32_FIND_DATAW *out) {
    char dir[RT_PATH_MAX], pat[RT_PATH_MAX];
    DWORD e = host_pattern(name, dir, pat);
    if (e) { rt_thread_set_last_error(e); return INVALID_HANDLE_VALUE; }
    if (pattern_stops(pat)) {
        if (wide) RT_FILE_STOP("rt_find_first_w", "the pattern \"%s\"", pat);
        RT_FILE_STOP("rt_find_first_a", "the pattern \"%s\"", pat);
    }
    WIN32_FIND_DATAW *list = NULL;
    uint32_t count = 0;
    e = scan_dir(dir, pat, &list, &count);
    if (e) { rt_thread_set_last_error(e); return INVALID_HANDLE_VALUE; }
    /* Game data: entries the install has in this directory and this machine lacks are a
     * stop, not a shorter listing (file_partial.c).  Nothing is held: the list leaks into
     * the end of the run. */
    rt_gamedata_listing_check(dir, pat, name_matches);
    if (count == 0) { rt_thread_set_last_error(ERROR_FILE_NOT_FOUND); return INVALID_HANDLE_VALUE; }
    RtFind *f = (RtFind *)rt_object_new(RT_K_FIND, sizeof(RtFind), &find_ops, NULL, NULL);
    if (!f) { free(list); rt_thread_set_last_error(ERROR_NOT_ENOUGH_MEMORY); return INVALID_HANDLE_VALUE; }
    f->lock = OS_UNFAIR_LOCK_INIT;
    f->entries = list;
    f->count = count;
    f->next = 1;                                    /* entry 0 is returned now */
    HANDLE h = rt_handle_new(&f->obj);              /* consumes the reference, also on failure */
    if (!h) { rt_thread_set_last_error(ERROR_NOT_ENOUGH_MEMORY); return INVALID_HANDLE_VALUE; }
    *out = list[0];
    return h;
}

/* The body of rt_find_next_w and _a: the next entry into *out, or ERROR_NO_MORE_FILES. */
static BOOL find_next(HANDLE find, WIN32_FIND_DATAW *out) {
    RtFind *f = find_ref(find);
    if (!f) return FALSE;
    os_unfair_lock_lock(&f->lock);
    const bool more = f->next < f->count;
    const uint32_t i = f->next;
    if (more) f->next++;
    os_unfair_lock_unlock(&f->lock);
    if (more) *out = f->entries[i];                 /* entries never change; our reference holds them */
    rt_object_unref(&f->obj);
    return more ? TRUE : rt_file_fail(ERROR_NO_MORE_FILES);
}

HANDLE rt_find_first_w(const WCHAR *pattern, WIN32_FIND_DATAW *out) {
    if (!out) RT_FILE_STOP("rt_find_first_w", "lpFindFileData NULL");
    return find_first(true, RT_NAME_W(pattern), out);
}

BOOL rt_find_next_w(HANDLE find, WIN32_FIND_DATAW *out) {
    if (!out) RT_FILE_STOP("rt_find_next_w", "lpFindFileData NULL");
    return find_next(find, out);
}

HANDLE rt_find_first_a(const char *pattern, WIN32_FIND_DATAA *out) {
    if (!out) RT_FILE_STOP("rt_find_first_a", "lpFindFileData NULL");
    WIN32_FIND_DATAW w = { 0 };
    const HANDLE h = find_first(false, RT_NAME_A(pattern), &w);
    if (h != INVALID_HANDLE_VALUE && !entry_to_a(&w, out))
        RT_FILE_STOP("rt_find_first_a", "an entry name outside code page 1252, or longer than 259 bytes");
    return h;
}

BOOL rt_find_next_a(HANDLE find, WIN32_FIND_DATAA *out) {
    if (!out) RT_FILE_STOP("rt_find_next_a", "lpFindFileData NULL");
    WIN32_FIND_DATAW w = { 0 };
    if (!find_next(find, &w)) return FALSE;
    if (!entry_to_a(&w, out))
        RT_FILE_STOP("rt_find_next_a", "an entry name outside code page 1252, or longer than 259 bytes");
    return TRUE;
}

BOOL rt_find_close(HANDLE find) {
    return rt_handle_close_kind(find, RT_K_FIND);
}
