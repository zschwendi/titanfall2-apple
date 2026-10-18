/* Game data that is only partly on this machine (docs/handoff/rt-game-data.md).
 *
 * The owner's install is 64 GB and lives on another machine; this one holds what a run asked
 * for so far.  tools/gamedata/fetch.py brings a file, or some byte ranges of a file, and keeps
 * a MAP of what it brought.  This piece reads the map and makes sure that the guest never
 * gets a byte that is not the install's: a read that touches bytes which were not fetched
 * ENDS THE RUN with a report that says which bytes and prints the command that fetches them.
 * Never zeros, never a short read.
 *
 * WHERE THE MAP IS.  In the directory <real path of the install root>.partial, beside the
 * install root and so outside everything the guest can see or enumerate.  "Real path": the
 * worktrees reach one shared install through a link each, and all of them must see one map.
 *
 *   <map>/manifest.tsv                 every file and directory the install has:
 *                                          gamedata-manifest 1
 *                                          f <TAB> size <TAB> path        (path as the install
 *                                          d <TAB> 0    <TAB> path         spells it, '/' between)
 *   <map>/ranges/<key>.ranges          one per PARTIAL file; <key> is its install path with
 *                                      ASCII letters in lower case:
 *                                          gamedata-ranges 1
 *                                          path <path as the install spells it>
 *                                          size <true size>
 *                                          <lo> <hi>          present bytes [lo, hi), ascending,
 *                                          ...                 not touching one another
 *                                          end <number of ranges>
 *   <map>/files/<key>                  its bytes: a sparse host file of the TRUE SIZE
 *
 * A PARTIAL FILE IS NOT IN THE INSTALL TREE.  Every file under the install root is whole, as
 * before this piece existed, and a runtime built without this piece sees a partial file as a
 * file that is not there: it can never read the holes of one as zeros.  This runtime finds it
 * where the name is translated (rt_file_name_to_host, the one place the file layer turns a
 * guest name into a host path): a name that is not under the install root and has a ranges
 * file leads to <map>/files/<key>, and from there on everything is a host file of the true
 * size: size, attributes and times need nothing else.  (The times are those of the fetch;
 * the install's are not kept.)  FindFirstFile adds the partial files of a directory to its
 * listing under the install's spelling (rt_partial_list).  A ranges file that cannot be read
 * or does not parse, whose size is not the host file's, or whose data file is not there, is
 * fatal: nothing is ever taken for whole that is not.
 *
 * WHAT STOPS (each is rt_fatal; the last line of the report is "  fetch: <command>"):
 *   - a read of a partial file (rt_file_read with and without an OVERLAPPED, rt_file_read_ex)
 *     whose bytes, up to the end of the file, are not all present;
 *   - a partial file opened for writing or truncation, deleted, or moved; a section
 *     (CreateFileMapping) of one.
 *     The game maps no install file as far as its imports say (docs/findings/24: one
 *     MapViewOfFile site in engine, for a named section), so views of a partial file are
 *     not built: the command printed fetches the whole file;
 *   - WITH A MANIFEST: a name the install has and this machine does not (the translation
 *     answered ERROR_FILE_NOT_FOUND or ERROR_PATH_NOT_FOUND), and a FindFirstFile whose
 *     pattern matches install entries of that directory that are neither here nor partial.  A name the
 *     install does not have stays the miss it is on Windows.
 *   Without a manifest nothing can tell the two misses apart, and both are answered as
 *   "not found", as before; one line on `file` at warn says so at the first miss.
 *
 * LOCKS.  The map directory and the manifest are found once (pthread_once) and never change
 * afterwards.  A partial file has a mutex of its own for reads at the handle's position
 * (rt_partial_read): the read is a check and a pread, and two threads reading one handle must
 * still get adjacent pieces.  It is held across the pread and with no other lock.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "file_priv.h"
#include "rt/module.h"
#include "rt/thread.h"

#define FETCH        "tools/gamedata/fetch.py"
#define GUEST_ROOT   "C:\\Titanfall2"
#define MAX_RANGES   (1u << 22)

typedef struct { uint64_t lo, hi; } Range;

struct RtPartial {
    uint64_t        size;       /* the true size, which is also the host file's */
    uint32_t        n;
    pthread_mutex_t pos;        /* rt_partial_read */
    char           *path;       /* as the install spells it (the ranges file's `path` line) */
    Range           r[];
};

typedef struct { char *path, *key; uint64_t size; bool dir; } Entry;

static struct {
    pthread_once_t once;
    char   root[RT_PATH_MAX], real[RT_PATH_MAX], map[RT_PATH_MAX], files[RT_PATH_MAX + 8];
    size_t root_len, real_len, files_len;
    Entry *man;                 /* sorted by key (the path with ASCII letters in lower case) */
    size_t n_man;
    bool   have_man;
    _Atomic(bool) warned;
} gd = { .once = PTHREAD_ONCE_INIT };

/* ---- the map directory and the manifest -------------------------------------------------- */

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static char *fold_dup(const char *s) {
    char *k = strdup(s);
    if (!k) rt_fatal("game data: out of memory");
    for (char *p = k; *p; p++) *p = lower(*p);
    return k;
}

static int entry_cmp(const void *a, const void *b) { return strcmp(((const Entry *)a)->key, ((const Entry *)b)->key); }

static void load_manifest(void) {
    char path[RT_PATH_MAX + 32];
    snprintf(path, sizeof path, "%s/manifest.tsv", gd.map);
    FILE *fp = fopen(path, "r");
    if (!fp) {
        if (errno != ENOENT && errno != ENOTDIR) rt_fatal("game data: %s cannot be opened: %s", path, strerror(errno));
        return;
    }
    char line[RT_PATH_MAX + 64];
    size_t cap = 0, lineno = 0;
    while (fgets(line, sizeof line, fp)) {
        lineno++;
        size_t len = strlen(line);
        if (len == 0 || line[len - 1] != '\n') rt_fatal("game data: %s line %zu is too long or has no end", path, lineno);
        line[--len] = 0;
        if (lineno == 1) {
            if (strcmp(line, "gamedata-manifest 1")) rt_fatal("game data: %s does not begin with \"gamedata-manifest 1\"", path);
            continue;
        }
        char *end = NULL;
        const bool dir = line[0] == 'd';
        if ((line[0] != 'f' && !dir) || line[1] != '\t') rt_fatal("game data: %s line %zu: no f or d", path, lineno);
        errno = 0;
        const unsigned long long size = strtoull(line + 2, &end, 10);
        if (errno || end == line + 2 || *end != '\t' || end[1] == 0 || end[1] == '/')
            rt_fatal("game data: %s line %zu: no size and path", path, lineno);
        if (gd.n_man == cap) {
            cap = cap ? cap * 2 : 1024;
            gd.man = realloc(gd.man, cap * sizeof *gd.man);
            if (!gd.man) rt_fatal("game data: out of memory");
        }
        Entry *e = &gd.man[gd.n_man++];
        e->path = strdup(end + 1);
        if (!e->path) rt_fatal("game data: out of memory");
        e->key = fold_dup(e->path);
        e->size = size;
        e->dir = dir;
    }
    if (ferror(fp) || lineno == 0) rt_fatal("game data: %s cannot be read", path);
    fclose(fp);
    qsort(gd.man, gd.n_man, sizeof *gd.man, entry_cmp);
    gd.have_man = true;
    rt_log(RT_CH_FILE, RT_LL_INFO, "game data: the manifest %s names %zu entries of the install", path, gd.n_man);
}

static void gd_init(void) {
    const char *root = rt_config()->install_root;
    gd.root_len = strlen(root);
    while (gd.root_len > 1 && root[gd.root_len - 1] == '/') gd.root_len--;
    if (gd.root_len >= sizeof gd.root) rt_fatal("game data: the install root is too long");
    memcpy(gd.root, root, gd.root_len);
    char real[PATH_MAX];
    if (!realpath(root, real)) rt_fatal("game data: realpath(%s): %s", root, strerror(errno));
    gd.real_len = strlen(real);
    if (gd.real_len + sizeof ".partial" > sizeof gd.map) rt_fatal("game data: the install root is too long");
    memcpy(gd.real, real, gd.real_len + 1);
    snprintf(gd.map, sizeof gd.map, "%s.partial", real);
    gd.files_len = (size_t)snprintf(gd.files, sizeof gd.files, "%s/files", gd.map);
    load_manifest();
}

static void gd_ready(void) { pthread_once(&gd.once, gd_init); }

/* The key of the partial file whose bytes are the host file `host`, or NULL: `host` is not in
 * the map's files directory, so it is no partial file. */
static const char *store_key(const char *host) {
    gd_ready();
    if (!strncmp(host, gd.files, gd.files_len) && host[gd.files_len] == '/' && host[gd.files_len + 1]) return host + gd.files_len + 1;
    return NULL;
}

/* The part of `host` below the install root, or NULL if it is not below it.  "" is the root. */
static const char *rel_of(const char *host) {
    gd_ready();
    if (!strncmp(host, gd.root, gd.root_len) && (host[gd.root_len] == '/' || host[gd.root_len] == 0))
        return host[gd.root_len] ? host + gd.root_len + 1 : "";
    if (!strncmp(host, gd.real, gd.real_len) && (host[gd.real_len] == '/' || host[gd.real_len] == 0))
        return host[gd.real_len] ? host + gd.real_len + 1 : "";
    return NULL;
}

static const Entry *man_find(const char *key) {
    size_t lo = 0, hi = gd.n_man;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        const int c = strcmp(gd.man[mid].key, key);
        if (c == 0) return &gd.man[mid];
        if (c < 0) lo = mid + 1; else hi = mid;
    }
    return NULL;
}

/* ---- the report ---------------------------------------------------------------------------- */

/* The guest code that called the Windows function the thread is in: "module+rva (through
 * lib.dll!Name)". */
static const char *caller(char *buf, size_t n) {
    const RtThread *t = rt_thread_self();
    const RtShimFrame *f = t ? t->shim : NULL;
    if (!f) {
        snprintf(buf, n, "(no Windows function on this thread: the runtime's own call)");
        return buf;
    }
    char where[160];
    if (f->entry_rsp >= t->stack_limit && f->entry_rsp + 8 <= t->stack_base)
        rt_module_describe(LD64(f->entry_rsp), where, sizeof where);
    else
        snprintf(where, sizeof where, "?");
    snprintf(buf, n, "%s (through %s.dll!%s)", where, f->shim->lib, f->shim->name);
    return buf;
}

/* `path` in single quotes for a shell. */
static const char *quoted(const char *path, char *buf, size_t n) {
    size_t at = 0;
    if (n < 8) rt_fatal("game data: quoted: no room");
    buf[at++] = '\'';
    for (const char *p = path; *p && at + 6 < n; p++) {
        if (*p == '\'') { memcpy(buf + at, "'\\''", 4); at += 4; }
        else buf[at++] = *p;
    }
    buf[at++] = '\'';
    buf[at] = 0;
    return buf;
}

/* The Windows name of install path `rel`. */
static const char *guest_name(const char *rel, char *buf, size_t n) {
    snprintf(buf, n, GUEST_ROOT "%s%s", *rel ? "\\" : "", rel);
    for (char *p = buf; *p; p++) if (*p == '/') *p = '\\';
    return buf;
}

/* ---- a partial file -------------------------------------------------------------------------- */

static void ranges_path(const char *key, char *path, size_t n) { snprintf(path, n, "%s/ranges/%s.ranges", gd.map, key); }

/* The map of the partial file `key`, whose data file `host` has `host_size` bytes. */
static struct RtPartial *load_ranges(const char *key, uint64_t host_size, const char *host) {
    char path[2 * RT_PATH_MAX + 32];
    ranges_path(key, path, sizeof path);
    FILE *fp = fopen(path, "r");
    if (!fp) rt_fatal("game data: the map %s of the partial file %s cannot be opened: %s", path, host, strerror(errno));
    char line[RT_PATH_MAX + 64];
    struct RtPartial *p = NULL;
    uint32_t cap = 0, n = 0;
    unsigned long long size = 0, count = 0;
    char *name = NULL;
    bool have_size = false, ended = false;
    for (size_t lineno = 1; fgets(line, sizeof line, fp); lineno++) {
        size_t len = strlen(line);
        if (len == 0 || line[len - 1] != '\n') rt_fatal("game data: the map %s: line %zu is too long or has no end", path, lineno);
        line[--len] = 0;
        if (ended) rt_fatal("game data: the map %s: text after its end line", path);
        if (lineno == 1) {
            if (strcmp(line, "gamedata-ranges 1")) rt_fatal("game data: the map %s does not begin with \"gamedata-ranges 1\"", path);
        } else if (lineno == 2) {
            if (strncmp(line, "path ", 5) || !line[5]) rt_fatal("game data: the map %s: line 2 is not the path", path);
            name = strdup(line + 5);
        } else if (lineno == 3) {
            char *end = NULL;
            errno = 0;
            if (strncmp(line, "size ", 5)) rt_fatal("game data: the map %s: line 3 is not the size", path);
            size = strtoull(line + 5, &end, 10);
            if (errno || end == line + 5 || *end) rt_fatal("game data: the map %s: line 3 is not the size", path);
            have_size = true;
        } else if (!strncmp(line, "end ", 4)) {
            char *end = NULL;
            errno = 0;
            count = strtoull(line + 4, &end, 10);
            if (errno || end == line + 4 || *end) rt_fatal("game data: the map %s: a bad end line", path);
            ended = true;
        } else {
            char *e1 = NULL, *e2 = NULL;
            errno = 0;
            const unsigned long long lo = strtoull(line, &e1, 10);
            const unsigned long long hi = (e1 != line && *e1 == ' ') ? strtoull(e1 + 1, &e2, 10) : 0;
            if (errno || !e2 || e2 == e1 + 1 || *e2 || lo >= hi || hi > size || (n && lo <= p->r[n - 1].hi))
                rt_fatal("game data: the map %s: line %zu is not a range inside the file after the one before", path, lineno);
            if (n == cap) {
                cap = cap ? cap * 2 : 64;
                if (cap > MAX_RANGES) rt_fatal("game data: the map %s has more than %u ranges", path, MAX_RANGES);
                p = realloc(p, sizeof *p + cap * sizeof p->r[0]);
                if (!p) rt_fatal("game data: out of memory");
            }
            p->r[n].lo = lo;
            p->r[n].hi = hi;
            n++;
        }
    }
    if (ferror(fp) || !ended || !have_size || !name || count != n)
        rt_fatal("game data: the map %s is cut short or does not count its ranges (%u read)", path, n);
    fclose(fp);
    if (size != host_size)
        rt_fatal("game data: the map %s says %llu bytes and the file %s has %llu: one of them is not the install's",
                 path, size, host, (unsigned long long)host_size);
    if (!p && !(p = malloc(sizeof *p))) rt_fatal("game data: out of memory");
    p->size = size;
    p->n = n;
    p->path = name;
    pthread_mutex_init(&p->pos, NULL);
    return p;
}

struct RtPartial *rt_partial_open(const char *host, const struct stat *st) {
    const char *key = store_key(host);
    if (!key || !S_ISREG(st->st_mode)) return NULL;
    return load_ranges(key, (uint64_t)st->st_size, host);
}

/* True if `key` is a partial file: it has a ranges file.  (That the file parses and fits its
 * data is found, fatally if not, when the file is opened.) */
static bool is_partial(const char *key) {
    char path[2 * RT_PATH_MAX + 32];
    struct stat st;
    ranges_path(key, path, sizeof path);
    if (stat(path, &st) == 0) return true;
    if (errno != ENOENT && errno != ENOTDIR) rt_fatal("game data: the map %s cannot be looked at: %s", path, strerror(errno));
    return false;
}

void rt_partial_free(struct RtPartial *p) {
    if (!p) return;
    pthread_mutex_destroy(&p->pos);
    free(p->path);
    free(p);
}

/* The first absent stretch of [lo, hi): false if there is none. */
static bool first_absent(const struct RtPartial *p, uint64_t lo, uint64_t hi, uint64_t *a_lo, uint64_t *a_hi) {
    uint32_t i = 0, j = p->n;
    while (i < j) {                                 /* the first range that ends after lo */
        const uint32_t mid = i + (j - i) / 2;
        if (p->r[mid].hi <= lo) i = mid + 1; else j = mid;
    }
    if (i < p->n && p->r[i].lo <= lo) {
        if (p->r[i].hi >= hi) return false;
        lo = p->r[i].hi;                            /* ranges do not touch: a gap begins here */
        i++;
    }
    *a_lo = lo;
    *a_hi = (i < p->n && p->r[i].lo < hi) ? p->r[i].lo : hi;
    return true;
}

void rt_partial_require(const RtFile *f, uint64_t off, uint64_t len, const char *fn) {
    const struct RtPartial *p = f->partial;
    if (!p || len == 0 || off >= p->size) return;
    const uint64_t hi = len > p->size - off ? p->size : off + len;
    uint64_t a_lo, a_hi;
    if (!first_absent(p, off, hi, &a_lo, &a_hi)) return;
    char who[320], q[RT_PATH_MAX + 16], win[RT_PATH_MAX + 32];
    rt_fatal("game data: a read touches bytes that are not on this Mac\n"
             "  file    %s  (%llu bytes in the install, %u ranges of it here)\n"
             "  asked   offset 0x%llx length 0x%llx  (%s)\n"
             "  absent  0x%llx to 0x%llx, the first stretch of what was asked that is not here\n"
             "  caller  %s\n"
             "  fetch: " FETCH " %s 0x%llx+0x%llx",
             guest_name(p->path, win, sizeof win), (unsigned long long)p->size, p->n,
             (unsigned long long)off, (unsigned long long)len, fn,
             (unsigned long long)a_lo, (unsigned long long)a_hi, caller(who, sizeof who),
             quoted(p->path, q, sizeof q), (unsigned long long)off, (unsigned long long)(hi - off));
}

ssize_t rt_partial_read(RtFile *f, void *buf, size_t len, const char *fn) {
    struct RtPartial *p = f->partial;
    pthread_mutex_lock(&p->pos);
    const off_t pos = lseek(f->fd, 0, SEEK_CUR);
    if (pos < 0) {
        const int en = errno;
        pthread_mutex_unlock(&p->pos);
        errno = en;
        return -1;
    }
    rt_partial_require(f, (uint64_t)pos, len, fn);
    size_t total = 0;
    int en = 0;
    while (total < len) {
        ssize_t n;
        do n = pread(f->fd, (uint8_t *)buf + total, len - total, pos + (off_t)total); while (n < 0 && errno == EINTR);
        if (n < 0) { en = errno; break; }
        if (n == 0) break;                          /* the end of the file */
        total += (size_t)n;
    }
    if (total && lseek(f->fd, pos + (off_t)total, SEEK_SET) < 0) rt_fatal("%s: the position of a partial file cannot be set: %s", fn, strerror(errno));
    pthread_mutex_unlock(&p->pos);
    if (en && total == 0) { errno = en; return -1; }
    return (ssize_t)total;
}

static _Noreturn void refuse(const char *path, uint64_t size, const char *fn, const char *what) {
    char who[320], q[RT_PATH_MAX + 16], win[RT_PATH_MAX + 32];
    rt_fatal("game data: %s of a file that is only partly on this Mac\n"
             "  file    %s  (%llu bytes in the install)\n"
             "  asked   %s  (%s): not built for a partial file, the whole file is needed\n"
             "  caller  %s\n"
             "  fetch: " FETCH " %s",
             what, guest_name(path, win, sizeof win), (unsigned long long)size, what, fn,
             caller(who, sizeof who), quoted(path, q, sizeof q));
}

void rt_partial_refuse(const RtFile *f, const char *fn, const char *what) {
    if (f->partial) refuse(f->partial->path, f->partial->size, fn, what);
}

void rt_partial_refuse_host(const char *host, const char *fn, const char *what) {
    struct stat st;
    if (stat(host, &st)) return;
    struct RtPartial *p = rt_partial_open(host, &st);
    if (p) refuse(p->path, p->size, fn, what);
}

/* ---- a name the install has and this machine does not ---------------------------------------- */

static _Noreturn void missing(const Entry *e, const char *fn, const char *asked) {
    char who[320], q[RT_PATH_MAX + 16], win[RT_PATH_MAX + 32];
    if (e->dir)
        rt_fatal("game data: a directory of the install is not on this Mac\n"
                 "  file    %s\n"
                 "  asked   %s  (%s)\n"
                 "  caller  %s\n"
                 "  fetch: " FETCH " --manifest",
                 guest_name(e->path, win, sizeof win), asked, fn, caller(who, sizeof who));
    rt_fatal("game data: a file of the install is not on this Mac\n"
             "  file    %s  (%llu bytes in the install)\n"
             "  asked   %s  (%s)\n"
             "  caller  %s\n"
             "  fetch: " FETCH " --open %s",
             guest_name(e->path, win, sizeof win), (unsigned long long)e->size, asked, fn,
             caller(who, sizeof who), quoted(e->path, q, sizeof q));
}

static void warn_no_manifest(void) {
    if (!atomic_exchange(&gd.warned, true))
        rt_log(RT_CH_FILE, RT_LL_WARN, "game data: no manifest of the install at %s/manifest.tsv: a file the install has "
               "and this Mac lacks is answered as not found, like a file the install lacks (" FETCH " --manifest)", gd.map);
}

/* The install path a guest name leads to, as a key (lower case, '/' between, no '/' at the
 * end), from the name as TEXT: the full path GetFullPathName gives, which looks at no disk.
 * False if the name does not lie below the install root or is not ASCII. */
static bool name_key(RtName name, char key[RT_PATH_MAX]) {
    WCHAR wide[RT_PATH_MAX], full[RT_PATH_MAX];
    const WCHAR *w = name.w;
    if (name.a) {
        size_t i = 0;
        for (; name.a[i] && i < RT_PATH_MAX - 1; i++) wide[i] = (uint8_t)name.a[i] < 0x80 ? (WCHAR)name.a[i] : (WCHAR)'?';
        if (name.a[i]) return false;
        wide[i] = 0;
        w = wide;
    }
    DWORD len = 0;
    if (!w || rt_path_full_w(w, full, RT_PATH_MAX, &len, NULL) || len == 0 || len >= RT_PATH_MAX) return false;
    const size_t root = sizeof GUEST_ROOT - 1;
    if (len <= root + 1) return false;
    for (size_t i = 0; i < root; i++) if (full[i] >= 0x80 || lower((char)full[i]) != lower(GUEST_ROOT[i])) return false;
    if (full[root] != '\\' && full[root] != '/') return false;
    size_t n = 0;
    for (size_t i = root + 1; i < len; i++) {
        if (full[i] >= 0x80) return false;          /* the install's names are ASCII */
        key[n++] = (full[i] == '\\') ? '/' : lower((char)full[i]);
    }
    while (n && key[n - 1] == '/') n--;
    key[n] = 0;
    return n != 0;
}

bool rt_partial_redirect(RtName name, char host[RT_PATH_MAX]) {
    char key[RT_PATH_MAX];
    gd_ready();
    if (!name_key(name, key) || !is_partial(key)) return false;
    if (gd.files_len + 1 + strlen(key) >= RT_PATH_MAX) rt_fatal("game data: the path of the partial file %s is too long", key);
    char data[RT_PATH_MAX];
    struct stat st;
    snprintf(data, sizeof data, "%s/%s", gd.files, key);
    if (lstat(data, &st) != 0 || !S_ISREG(st.st_mode))
        rt_fatal("game data: the map has ranges for %s and its bytes are not at %s: never answered as a file that is not there", key, data);
    memcpy(host, data, strlen(data) + 1);
    return true;
}

void rt_gamedata_name_missing(RtName name, DWORD error) {
    gd_ready();
    if (!gd.have_man) { warn_no_manifest(); return; }
    char key[RT_PATH_MAX];
    if (!name_key(name, key)) return;
    const Entry *e = man_find(key);
    if (e) missing(e, "a name the game gave", error == ERROR_PATH_NOT_FOUND ? "its directory is not here" : "the name is not here");
    /* A directory on the way that the install has and this machine does not.  Only the name's
     * own directory can show one: if the install has that directory it has every one above it,
     * and "path not found" here means one of them is missing on this machine.  If the install
     * lacks the name's own directory, "path not found" is the install's answer too, whatever
     * lies above it (the game asks for platform\resource\game_x64.ico, and the install has
     * platform but no platform\resource): a miss, not a stop. */
    if (error != ERROR_PATH_NOT_FOUND) return;
    char *slash = strrchr(key, '/');
    if (!slash) return;
    *slash = 0;
    e = man_find(key);
    if (e && e->dir) missing(e, "a name the game gave", "a directory on the way to the name is not here");
}

void rt_gamedata_listing_check(const char *host_dir, const char *pat, bool (*matches)(const char *pat, const char *name)) {
    const char *rel = rel_of(host_dir);
    if (!rel) return;
    if (!gd.have_man) return;                       /* the warning is for a miss, not for every listing */
    char *key = fold_dup(rel);
    const size_t klen = strlen(key);
    const Entry *first = NULL;
    unsigned absent = 0;
    for (size_t i = 0; i < gd.n_man; i++) {
        const Entry *e = &gd.man[i];
        const char *name = e->path;
        if (klen) {
            if (strncmp(e->key, key, klen) || e->key[klen] != '/') continue;
            name = e->path + klen + 1;
        }
        if (strchr(name, '/') || !matches(pat, name)) continue;
        char host[2 * RT_PATH_MAX];
        struct stat st;
        snprintf(host, sizeof host, "%s/%s", host_dir, name);
        if (lstat(host, &st) == 0 || errno != ENOENT) continue;
        if (!e->dir && is_partial(e->key)) continue;   /* rt_partial_list shows it */
        if (!absent++) first = e;
    }
    free(key);
    if (!absent) return;
    if (absent == 1) missing(first, "a directory listing", pat);
    char who[320], q[2 * RT_PATH_MAX + 16], win[RT_PATH_MAX + 32], full[2 * RT_PATH_MAX];
    snprintf(full, sizeof full, "%s%s%s", rel, *rel ? "/" : "", pat);
    rt_fatal("game data: a directory listing would leave out %u entries of the install that are not on this Mac\n"
             "  file    %s  (the first of them: %s)\n"
             "  asked   the pattern %s  (a directory listing)\n"
             "  caller  %s\n"
             "  fetch: " FETCH " --open-matching %s",
             absent, guest_name(full, win, sizeof win), first->path, pat, caller(who, sizeof who), quoted(full, q, sizeof q));
}

void rt_partial_list(const char *host_dir, void (*add)(void *cookie, int dfd, const char *store_name, const char *name), void *cookie) {
    const char *rel = rel_of(host_dir);
    if (!rel) return;
    char *key = fold_dup(rel);
    char dir[2 * RT_PATH_MAX + 16];
    snprintf(dir, sizeof dir, "%s%s%s", gd.files, *key ? "/" : "", key);
    DIR *d = opendir(dir);
    if (!d) {
        if (errno != ENOENT && errno != ENOTDIR) rt_fatal("game data: %s cannot be listed: %s", dir, strerror(errno));
        free(key);
        return;
    }
    for (struct dirent *de; (de = readdir(d)) != NULL; ) {
        if (de->d_type == DT_DIR || !strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        /* The entry's name as the install spells it: the last component of its map's path
         * line.  A data file with no ranges file is one being fetched: not there yet. */
        char path[2 * RT_PATH_MAX + 300], line[RT_PATH_MAX + 64];
        snprintf(path, sizeof path, "%s/ranges/%s%s%s.ranges", gd.map, key, *key ? "/" : "", de->d_name);
        FILE *fp = fopen(path, "r");
        if (!fp) {
            if (errno == ENOENT) continue;
            rt_fatal("game data: the map %s cannot be opened: %s", path, strerror(errno));
        }
        const bool ok = fgets(line, sizeof line, fp) && fgets(line, sizeof line, fp) && !strncmp(line, "path ", 5) && strchr(line, '\n');
        fclose(fp);
        if (!ok) rt_fatal("game data: the map %s has no path line", path);
        *strchr(line, '\n') = 0;
        const char *name = strrchr(line + 5, '/');
        name = name ? name + 1 : line + 5;
        /* In the install tree as well (a fetch that made it whole and was stopped before it
         * tidied up): the whole file is the entry, once. */
        struct stat st;
        snprintf(path, sizeof path, "%s/%s", host_dir, name);
        if (lstat(path, &st) == 0) continue;
        add(cookie, dirfd(d), de->d_name, name);
    }
    closedir(d);
    free(key);
}
