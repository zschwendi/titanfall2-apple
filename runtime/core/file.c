/* The file object, its lifetime, and open (docs/RUNTIME.md section 7.3).  Specification:
 * rt/file.h; what the other pieces of the file layer use from here: core/file_priv.h.
 *
 * THE FILE LOCK guards one thing, the list of open file objects, which exists for the
 * sharing log (sharing modes are recorded and not enforced).  It is a leaf lock: nothing is
 * called under it.  An object is put on the list before its handle exists and taken off in
 * its destroy operation, which the handle unit runs on whichever thread drops the last
 * reference; so no reference to a file is ever dropped with the lock held.
 *
 * rt_file_init is here, and not in file_std.c, for the linker: core/init.c holds a weak
 * rt_file_init that does nothing, and the strong one is only taken if its object file is
 * linked.  Every piece of the file layer references this file, so it is.
 */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <os/lock.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "file_priv.h"
#include "rt/time.h"

static os_unfair_lock file_lock = OS_UNFAIR_LOCK_INIT;
static RtFile        *open_files;           /* file lock */
static unsigned       open_count;           /* file lock */
static bool           file_ready;
static _Atomic(uint64_t) std_slot[3];

#define ACCESS_OK  (GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE | GENERIC_ALL | DELETE | \
                    READ_CONTROL | SYNCHRONIZE | 0x1FFu)
#define ATTRS_OK   (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | \
                    FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_TEMPORARY | \
                    FILE_ATTRIBUTE_NOT_CONTENT_INDEXED)
#define FLAGS_OK   (FILE_FLAG_WRITE_THROUGH | FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING | \
                    FILE_FLAG_RANDOM_ACCESS | FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_DELETE_ON_CLOSE | \
                    FILE_FLAG_BACKUP_SEMANTICS)

/* ---- helpers the other pieces share ------------------------------------------------------- */

DWORD rt_file_name_to_host(RtName name, uint32_t flags, char host[RT_PATH_MAX]) {
    if (name.w && name.a) rt_fatal("rt_file_name_to_host: a name with both a wide and a narrow form");
    const DWORD e = name.a ? rt_path_to_host_a(name.a, flags, host) : rt_path_to_host_w(name.w, flags, host);
    /* Game data (file_partial.c).  A name that is not in the install tree may be a file that
     * is only partly on this machine: its bytes are beside the tree, and `host` leads there.
     * Otherwise a name the install has and this machine does not is a stop, not a miss. */
    struct stat st;
    const bool absent = e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND;
    if (absent || (e == 0 && (flags & RT_PATH_CREATE) && lstat(host, &st) != 0 && errno == ENOENT)) {
        if (rt_partial_redirect(name, host)) return 0;
        if (absent) rt_gamedata_name_missing(name, e);
    }
    return e;
}

bool rt_file_host_is_null(const char *host) { return strcmp(host, "/dev/null") == 0; }

void rt_file_invalidate_parent(const char *host) {
    char dir[RT_PATH_MAX];
    const char *slash = strrchr(host, '/');
    size_t n = slash ? (size_t)(slash - host) : 0;
    if (n == 0) n = slash ? 1 : 0;                  /* "/x": the root */
    if (n == 0 || n >= sizeof dir) return;
    memcpy(dir, host, n);
    dir[n] = 0;
    rt_path_invalidate(dir);
}

static FILETIME filetime(struct timespec ts) {
    uint64_t t = rt_time_filetime_from_unix(ts.tv_sec, ts.tv_nsec);
    return (FILETIME){ (DWORD)t, (DWORD)(t >> 32) };
}

void rt_file_attr_from_stat(const struct stat *st, DWORD *attrs, FILETIME *creation,
                            FILETIME *access, FILETIME *write, ULONGLONG *size) {
    const bool dir = S_ISDIR(st->st_mode);
    if (!dir && !S_ISREG(st->st_mode))
        RT_FILE_STOP("rt_file_attr_from_stat", "a host object of type 0%o", (unsigned)(st->st_mode & S_IFMT));
    if (attrs) *attrs = dir ? FILE_ATTRIBUTE_DIRECTORY
                            : FILE_ATTRIBUTE_ARCHIVE | ((st->st_mode & S_IWUSR) ? 0 : FILE_ATTRIBUTE_READONLY);
    if (creation) *creation = filetime(st->st_birthtimespec);
    if (access)   *access   = filetime(st->st_atimespec);
    if (write)    *write    = filetime(st->st_mtimespec);
    if (size)     *size     = dir ? 0 : (ULONGLONG)st->st_size;
}

/* RT_FILE_OPEN_LOG (file_publish): -1 until the first open has read the variable. */
static _Atomic int      open_log = -1;
static _Atomic unsigned open_seq;

/* For tests/core/file_open_log.c: the next open reads RT_FILE_OPEN_LOG again and numbers from 1. */
void rt_file_test_open_log_reset(void) {
    atomic_store_explicit(&open_seq, 0, memory_order_relaxed);
    atomic_store_explicit(&open_log, -1, memory_order_relaxed);
}

/* The count of rt_file_writers_under; `try`: the file lock is taken with a try-lock, and -1 is
 * returned (nothing counted) when another thread holds it. */
static int writers_under(const char *dir, char *first, size_t n, bool try) {
    char root[PATH_MAX], real[PATH_MAX];
    if (first && n) first[0] = 0;
    if (!dir || !realpath(dir, root)) return 0;
    const size_t len = strcmp(root, "/") ? strlen(root) : 0;      /* the root itself: every path is under it */
    if (try) {
        if (!os_unfair_lock_trylock(&file_lock)) return -1;
    } else {
        os_unfair_lock_lock(&file_lock);
    }
    int found = 0;
    for (RtFile *f = open_files; f; f = f->next) {
        if (!f->host_path || !f->can_write || !realpath(f->host_path, real)) continue;
        if (strncmp(real, root, len) != 0 || (real[len] != '/' && real[len] != 0)) continue;
        if (!found++ && first && n) snprintf(first, n, "%s", f->host_path);
    }
    os_unfair_lock_unlock(&file_lock);
    return found;
}

unsigned rt_file_writers_under(const char *dir, char *first, size_t n) {
    return (unsigned)writers_under(dir, first, n, false);
}

int rt_file_writers_under_try(const char *dir, char *first, size_t n) {
    return writers_under(dir, first, n, true);
}

bool rt_file_log_if_open(const char *host, const char *what) {
    struct stat st;
    if (stat(host, &st)) return false;
    bool found = false;
    os_unfair_lock_lock(&file_lock);
    for (RtFile *f = open_files; f && !found; f = f->next)
        found = f->host_path && f->dev == (uint64_t)st.st_dev && f->ino == (uint64_t)st.st_ino &&
                !(f->share & FILE_SHARE_DELETE) &&
                (f->can_read || f->can_write || (f->access & (DELETE | GENERIC_ALL)));
    os_unfair_lock_unlock(&file_lock);
    if (found)
        rt_log(RT_CH_FILE, RT_LL_WARN, "sharing violation not enforced: %s of %s, which is open without "
               "FILE_SHARE_DELETE", what, host);
    return found;
}

static unsigned std_index(unsigned i) {
    if (i > 2) rt_fatal("standard handle slot %u does not exist", i);
    return i;
}
HANDLE rt_file_std_slot(unsigned i) { return (HANDLE)(uintptr_t)atomic_load(&std_slot[std_index(i)]); }
HANDLE rt_file_std_slot_swap(unsigned i, HANDLE h) {
    return (HANDLE)(uintptr_t)atomic_exchange(&std_slot[std_index(i)], (uint64_t)(uintptr_t)h);
}

/* ---- the object ----------------------------------------------------------------------------- */

static void file_destroy(RtObject *obj) {
    RtFile *f = (RtFile *)obj;
    os_unfair_lock_lock(&file_lock);
    if (f->prev) f->prev->next = f->next; else open_files = f->next;
    if (f->next) f->next->prev = f->prev;
    open_count--;
    os_unfair_lock_unlock(&file_lock);
    if (f->delete_on_close && f->host_path) {
        if (unlink(f->host_path))
            rt_log(RT_CH_FILE, RT_LL_WARN, "delete on close: unlink %s: %s", f->host_path, strerror(errno));
        else
            rt_file_invalidate_parent(f->host_path);
    }
    close(f->fd);
    free(f->host_path);
    rt_partial_free(f->partial);
}

static const RtObjectOps file_ops = { .name = "file", .destroy = file_destroy };

/* True if Windows would refuse to have both opens of one file at once.  An open that asks
 * for no read, write or delete access (attributes only) takes no part: the kernel neither
 * checks its share mode nor records it. */
static bool share_conflict(const RtFile *a, const RtFile *b) {
    const DWORD del_a = a->access & (DELETE | GENERIC_ALL), del_b = b->access & (DELETE | GENERIC_ALL);
    if (!(a->can_read || a->can_write || del_a) || !(b->can_read || b->can_write || del_b)) return false;
    return (a->can_read && !(b->share & FILE_SHARE_READ)) || (a->can_write && !(b->share & FILE_SHARE_WRITE)) ||
           (del_a && !(b->share & FILE_SHARE_DELETE)) ||
           (b->can_read && !(a->share & FILE_SHARE_READ)) || (b->can_write && !(a->share & FILE_SHARE_WRITE)) ||
           (del_b && !(a->share & FILE_SHARE_DELETE));
}

/* The object for an open descriptor, on the list, and its handle.  `proto` carries every
 * field but obj and the links; its host_path (malloc'd, may be NULL) and its fd become the
 * object's, also on failure, which is NULL with ERROR_NOT_ENOUGH_MEMORY. */
static HANDLE file_publish(const RtFile *proto) {
    RtFile *f = (RtFile *)rt_object_new(RT_K_FILE, sizeof *f, &file_ops, NULL, NULL);
    if (!f) {
        close(proto->fd);
        free(proto->host_path);
        rt_partial_free(proto->partial);
        rt_thread_set_last_error(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    const RtObject head = f->obj;
    *f = *proto;
    f->obj = head;
    f->delete_on_close = false;                     /* not until the handle exists */
    bool conflict = false;
    os_unfair_lock_lock(&file_lock);
    for (RtFile *o = open_files; o && f->host_path && !conflict; o = o->next)
        conflict = o->host_path && o->dev == f->dev && o->ino == f->ino && share_conflict(f, o);
    f->prev = NULL;
    f->next = open_files;
    if (open_files) open_files->prev = f;
    open_files = f;
    open_count++;
    os_unfair_lock_unlock(&file_lock);
    if (conflict)
        rt_log(RT_CH_FILE, RT_LL_WARN, "sharing violation not enforced: %s opened with access 0x%x share 0x%x "
               "while another open of it does not allow that", f->host_path, f->access, f->share);
    /* RT_FILE_OPEN_LOG=1: one info line per successful open that has a host path, numbered, so
     * that a run at the info level can be compared with a recording's files in order
     * (gate3-run-12).  A diagnostic: off unless asked for, nothing else depends on it. */
    int want = atomic_load_explicit(&open_log, memory_order_relaxed);
    if (want < 0) {
        const char *e = getenv("RT_FILE_OPEN_LOG");
        want = e && e[0] == '1';
        atomic_store_explicit(&open_log, want, memory_order_relaxed);
    }
    if (want && f->host_path)
        rt_log(RT_CH_FILE, RT_LL_INFO, "open %u: %s (access 0x%x)",
               atomic_fetch_add_explicit(&open_seq, 1, memory_order_relaxed) + 1, f->host_path, f->access);
    rt_object_ref(&f->obj);                         /* ours, across rt_handle_new */
    HANDLE h = rt_handle_new(&f->obj);
    if (h) f->delete_on_close = proto->delete_on_close;
    else   rt_thread_set_last_error(ERROR_NOT_ENOUGH_MEMORY);
    rt_object_unref(&f->obj);
    return h;
}

HANDLE rt_file_wrap_fd(int fd, RtFileType type, bool can_read, bool can_write) {
    const RtFile proto = { .fd = fd, .type = (uint8_t)type, .can_read = can_read, .can_write = can_write };
    return file_publish(&proto);
}

int rt_file_test_fd(HANDLE h) {
    const DWORD saved = rt_thread_get_last_error();
    RtFile *f = rt_file_ref(h);
    rt_thread_set_last_error(saved);
    if (!f) return -1;
    const int fd = f->fd;
    rt_file_unref(f);
    return fd;
}

void rt_file_test_hold_lock(bool hold) {
    if (hold) os_unfair_lock_lock(&file_lock);
    else os_unfair_lock_unlock(&file_lock);
}

unsigned rt_file_test_open_count(void) {
    os_unfair_lock_lock(&file_lock);
    const unsigned n = open_count;
    os_unfair_lock_unlock(&file_lock);
    return n;
}

/* ---- start-up: the three standard handles (rt/file.h, STANDARD HANDLES) ------------------- */

static HANDLE std_make(int host_fd, bool input) {
    int fd = input ? -1 : fcntl(host_fd, F_DUPFD_CLOEXEC, 3);
    if (fd < 0) {
        if (!input) rt_log(RT_CH_FILE, RT_LL_WARN, "host descriptor %d is not open: that standard handle "
                           "writes to /dev/null", host_fd);
        fd = open("/dev/null", (input ? O_RDONLY : O_WRONLY) | O_CLOEXEC);
        if (fd < 0) rt_fatal("rt_file_init: cannot open /dev/null: %s", strerror(errno));
    }
    HANDLE h = rt_file_wrap_fd(fd, RT_FT_CHAR, input, !input);
    if (!h) rt_fatal("rt_file_init: no handle for a standard handle");
    return h;
}

void rt_file_init(void) {
    if (file_ready) rt_fatal("rt_file_init called twice");
    file_ready = true;
    rt_file_std_slot_swap(0, std_make(0, true));
    rt_file_std_slot_swap(1, std_make(1, false));
    rt_file_std_slot_swap(2, std_make(2, false));
}

/* ---- open ------------------------------------------------------------------------------------- */

static int open_retry(const char *host, int flags, mode_t perm) {
    int fd;
    do fd = open(host, flags, perm); while (fd < 0 && errno == EINTR);
    return fd;
}

static HANDLE open_fail(DWORD e) { rt_thread_set_last_error(e); return INVALID_HANDLE_VALUE; }

HANDLE rt_file_create(RtName name, DWORD access, DWORD share, DWORD disposition,
                      DWORD flags_attrs, HANDLE template_file) {
    if (!file_ready) rt_fatal("rt_file_create before rt_file_init");

    /* 1. What is not built, or not verified. */
    if (template_file) RT_FILE_STOP("rt_file_create", "a template file, handle %p", (void *)template_file);
    if (share & ~(FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE))
        RT_FILE_STOP("rt_file_create", "share mode 0x%x", share);
    if (access & ~ACCESS_OK) RT_FILE_STOP("rt_file_create", "access 0x%x", access);
    const bool can_read  = (access & (GENERIC_READ | GENERIC_ALL | FILE_READ_DATA)) != 0;
    const bool can_write = (access & (GENERIC_WRITE | GENERIC_ALL | FILE_WRITE_DATA)) != 0;
    if ((access & FILE_APPEND_DATA) && !can_write)
        RT_FILE_STOP("rt_file_create", "an append-only handle, access 0x%x", access);
    if (flags_attrs & ~(ATTRS_OK | FLAGS_OK))
        RT_FILE_STOP("rt_file_create", "flags and attributes 0x%x", flags_attrs);

    /* 2. Argument errors. */
    if (disposition < CREATE_NEW || disposition > TRUNCATE_EXISTING) return open_fail(ERROR_INVALID_PARAMETER);
    if (disposition == TRUNCATE_EXISTING && !can_write) return open_fail(ERROR_INVALID_PARAMETER);

    /* 3. The name. */
    const bool must_exist = disposition == OPEN_EXISTING || disposition == TRUNCATE_EXISTING;
    char host[RT_PATH_MAX];
    DWORD e = rt_file_name_to_host(name, must_exist ? RT_PATH_EXISTING : RT_PATH_CREATE, host);
    if (e) return open_fail(e);
    const bool is_null = rt_file_host_is_null(host);
    if (is_null && disposition == CREATE_NEW) RT_FILE_STOP("rt_file_create", "CREATE_NEW on NUL");

    /* 4. The host open.  "Did it exist" must not be a separate question from the open: try
     * to open what is there, then to create exclusively, and go round again if the name
     * appeared or vanished in between. */
    const int   base = ((can_write || (disposition == CREATE_ALWAYS && !is_null)) ? O_RDWR : O_RDONLY) | O_CLOEXEC;
    const int   trunc = (disposition == CREATE_ALWAYS || disposition == TRUNCATE_EXISTING) ? O_TRUNC : 0;
    const mode_t perm = (flags_attrs & FILE_ATTRIBUTE_READONLY) ? 0444 : 0644;
    int  fd = -1;
    bool existed = false;
    /* Game data (file_partial.c).  A partial install file is never written or truncated: asked
     * before the open, which would truncate.  And a name that may be created here although
     * the install has it (it is only not on this machine) is the stop of a missing file,
     * except for CREATE_ALWAYS, which keeps nothing of the file. */
    if (!is_null && ((base & O_ACCMODE) == O_RDWR || trunc))
        rt_partial_refuse_host(host, "rt_file_create", "an open for writing or truncation");
    if (!is_null && !must_exist && disposition != CREATE_ALWAYS) {
        struct stat probe;
        if (lstat(host, &probe) != 0 && errno == ENOENT) rt_gamedata_name_missing(name, ERROR_FILE_NOT_FOUND);
    }
    if (is_null) {
        fd = open_retry(host, base, 0);
    } else for (int tries = 0; ; tries++) {
        if (disposition != CREATE_NEW) {
            fd = open_retry(host, base | trunc, 0);
            if (fd >= 0) { existed = true; break; }
            if (errno != ENOENT || must_exist) break;
        }
        fd = open_retry(host, base | O_CREAT | O_EXCL, perm);
        if (fd >= 0 || errno != EEXIST || disposition == CREATE_NEW || tries == 8) break;
    }
    if (fd < 0) return open_fail(rt_win32_from_errno(errno, RT_ERR_FILE));
    if (!existed && !is_null) rt_file_invalidate_parent(host);

    /* 5. What was opened. */
    struct stat st;
    if (fstat(fd, &st)) {
        const int en = errno;
        close(fd);
        return open_fail(rt_win32_from_errno(en, RT_ERR_FILE));
    }
    RtFile proto = { .fd = fd, .can_read = can_read, .can_write = can_write, .access = access, .share = share,
                     .flags = flags_attrs, .dev = (uint64_t)st.st_dev, .ino = (uint64_t)st.st_ino,
                     .overlapped = (flags_attrs & FILE_FLAG_OVERLAPPED) != 0,
                     .delete_on_close = (flags_attrs & FILE_FLAG_DELETE_ON_CLOSE) != 0 && !is_null };
    if (is_null) {
        proto.type = RT_FT_CHAR;
    } else if (S_ISREG(st.st_mode)) {
        proto.type = RT_FT_DISK;
        proto.partial = rt_partial_open(host, &st);     /* NULL: whole */
    } else if (S_ISDIR(st.st_mode)) {
        if (!(flags_attrs & FILE_FLAG_BACKUP_SEMANTICS)) { close(fd); return open_fail(ERROR_ACCESS_DENIED); }
        proto.type = RT_FT_DIR;
        proto.can_read = proto.can_write = false;
    } else {
        RT_FILE_STOP("rt_file_create", "%s is a host object of type 0%o", host, (unsigned)(st.st_mode & S_IFMT));
    }

    /* 6. Hints.  A host that refuses one changes nothing the guest can see. */
    if (proto.type == RT_FT_DISK) {
        if (flags_attrs & FILE_FLAG_NO_BUFFERING)    (void)fcntl(fd, F_NOCACHE, 1);
        if (flags_attrs & FILE_FLAG_SEQUENTIAL_SCAN) (void)fcntl(fd, F_RDAHEAD, 1);
        if (flags_attrs & FILE_FLAG_RANDOM_ACCESS)   (void)fcntl(fd, F_RDAHEAD, 0);
    }

    /* 7 and 8. The object (the sharing log is in file_publish) and the handle. */
    proto.host_path = is_null ? NULL : strdup(host);
    if (!is_null && !proto.host_path) { close(fd); rt_partial_free(proto.partial); return open_fail(ERROR_NOT_ENOUGH_MEMORY); }
    HANDLE h = file_publish(&proto);
    if (!h) return INVALID_HANDLE_VALUE;            /* last error is set */
    rt_thread_set_last_error(existed && (disposition == CREATE_ALWAYS || disposition == OPEN_ALWAYS)
                             ? ERROR_ALREADY_EXISTS : ERROR_SUCCESS);
    return h;
}
