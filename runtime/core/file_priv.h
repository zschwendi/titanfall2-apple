/* Private to runtime/core/file*.c: what the pieces of the file layer share.  The public
 * specification is rt/file.h; nothing outside core/file*.c includes this file (tests declare
 * the two test hooks at the end by hand, as the path tests do).
 *
 * Everything declared here is BUILT, in core/file.c, except rt_file_ov_read and
 * rt_file_ov_write (core/file_ov.c).
 */
#ifndef RT_FILE_PRIV_H
#define RT_FILE_PRIV_H

#include <sys/stat.h>

#include "core.h"
#include "rt/file.h"
#include "rt/path.h"

/* A STOP of rt/file.h: the run ends with "FATAL ... <function>: not implemented: <what>".
 * `fn` is the public function's name as a string literal; a test expects the text
 * "<function>: not implemented".  Put the offending value in the message. */
#define RT_FILE_STOP(fn, fmt, ...)  rt_fatal(fn ": not implemented: " fmt, ##__VA_ARGS__)

typedef enum {
    RT_FT_DISK = 1,     /* a regular file */
    RT_FT_DIR,          /* a directory opened with FILE_FLAG_BACKUP_SEMANTICS */
    RT_FT_CHAR          /* a character device: a standard handle, NUL */
} RtFileType;

/* The object behind a handle of kind RT_K_FILE.  Every field is set before the handle
 * exists and NEVER CHANGES afterwards (the list links excepted, which only core/file.c
 * touches), so a piece that holds a reference reads them without a lock.  The position is
 * not here: it is the host descriptor's. */
typedef struct RtFile {
    RtObject obj;
    int      fd;                /* the host descriptor: open for as long as the object lives */
    uint8_t  type;              /* RtFileType */
    bool     can_read;          /* rt_file_create step 4; false for a directory handle */
    bool     can_write;
    bool     overlapped;        /* opened with FILE_FLAG_OVERLAPPED */
    bool     delete_on_close;
    DWORD    access, share, flags;      /* as given to rt_file_create; 0 for a wrapped descriptor */
    uint64_t dev, ino;          /* the host file's identity, for the sharing log */
    char    *host_path;         /* the host path it was opened by (malloc); NULL for a wrapped descriptor */
    struct RtPartial *partial;  /* core/file_partial.c: the map of an install file that is only partly on
                                 * this machine; NULL for every other file (it is whole) */
    struct RtFile *next, *prev; /* core/file.c: the list of open files, under the file lock */
} RtFile;

/* The file behind `h` with one more reference, or NULL with last error ERROR_INVALID_HANDLE
 * (rt/file.h, rt_file_read step 3).  Drop the reference with rt_file_unref on EVERY path,
 * and never while holding a lock. */
static inline RtFile *rt_file_ref(HANDLE h) { return (RtFile *)rt_handle_ref(h, RT_K_FILE); }
static inline void rt_file_unref(RtFile *f) { rt_object_unref(&f->obj); }

/* The end of a failing function: set last error, return FALSE.  The second maps a host
 * errno (pass the value read right after the failing call). */
static inline BOOL rt_file_fail(DWORD e) { rt_thread_set_last_error(e); return FALSE; }
static inline BOOL rt_file_fail_errno(int host_errno, RtErrContext what) {
    return rt_file_fail(rt_win32_from_errno(host_errno, what));
}

/* Translate a name (rt/file.h, NAMES): rt_path_to_host_w or _a with `flags`.  Returns 0 or
 * the Win32 error and sets no last error.  Fatal if both members are set. */
DWORD rt_file_name_to_host(RtName name, uint32_t flags, char host[RT_PATH_MAX]);

/* True if the translation led to NUL. */
bool rt_file_host_is_null(const char *host);

/* The runtime created, deleted or renamed `host`: drop the cached listing of the directory
 * it is in (rt_path_invalidate on the text before the last '/'). */
void rt_file_invalidate_parent(const char *host);

/* ATTRIBUTES, TIMES and SIZE of rt/file.h from a host stat: the one place they are computed.
 * Any of the five pointers may be NULL.  A host object that is neither a regular file nor a
 * directory is the stop "rt_file_attr_from_stat: not implemented". */
void rt_file_attr_from_stat(const struct stat *st, DWORD *attrs, FILETIME *creation,
                            FILETIME *access, FILETIME *write, ULONGLONG *size);

/* The sharing log for a delete or a move: if some open file object has the host file `host`
 * (same device and inode) open with read, write or delete access and without FILE_SHARE_DELETE, log one line on `file` at warn,
 * "sharing violation not enforced: <what> of <host> ...", and return true.  `what` is the
 * Windows function's name.  False, silently, if nothing has it open or `host` cannot be
 * stat-ed.  Takes the file lock. */
bool rt_file_log_if_open(const char *host, const char *what);

/* A file object around a host descriptor the caller opened, and its handle: for the standard
 * handles (and pipes, if they are built).  The object owns `fd` from now on, also when the
 * call fails: NULL with ERROR_NOT_ENOUGH_MEMORY, and the descriptor is closed. */
HANDLE rt_file_wrap_fd(int fd, RtFileType type, bool can_read, bool can_write);

/* The three standard-handle slots: i is 0 (input), 1 (output), 2 (error); anything else is
 * fatal.  rt_file_init fills them.  Get reads the slot; swap stores `h` and returns what was
 * there.  Atomic; no lock. */
HANDLE rt_file_std_slot(unsigned i);
HANDLE rt_file_std_slot_swap(unsigned i, HANDLE h);

/* core/file_ov.c: the OVERLAPPED forms of rt_file_read and rt_file_write (rt/file.h,
 * OVERLAPPED).  Called by those two after their steps 1 to 3, with `f` referenced by the
 * caller (who also drops it) and ov not NULL; `got` / `put` may be NULL.  They return what
 * ReadFile / WriteFile return and set last error themselves. */
BOOL rt_file_ov_read(RtFile *f, void *buf, DWORD len, DWORD *got, OVERLAPPED *ov);
BOOL rt_file_ov_write(RtFile *f, const void *buf, DWORD len, DWORD *put, OVERLAPPED *ov);

/* ---- game data that is only partly here (core/file_partial.c; its first comment is the design) -- */

/* The map of the partial file whose bytes are the host file `host` (`st` is its stat), or
 * NULL: `host` is not in the map's files directory (every file of the install tree is whole),
 * or is no regular file.  Fatal if the ranges file is not there, cannot be read, does not
 * parse, or gives another size than st->st_size. */
struct RtPartial *rt_partial_open(const char *host, const struct stat *st);
/* The guest name `name` leads to a partial file: its data file's host path into `host`, true.
 * False, and `host` untouched, if it does not (not below the install root, or no ranges
 * file).  Looks at the name as text and at the map; never at the install tree. */
bool rt_partial_redirect(RtName name, char host[RT_PATH_MAX]);
/* The partial files of the install directory `host_dir` (a host path below the install
 * root), for a listing: add(cookie, dfd, store_name, name) for each, where
 * fstatat(dfd, store_name) is its data file and `name` its name as the install spells it.
 * One that is in the install tree as well is left out. */
void rt_partial_list(const char *host_dir, void (*add)(void *cookie, int dfd, const char *store_name, const char *name), void *cookie);
void rt_partial_free(struct RtPartial *p);          /* NULL does nothing */

/* A read of `len` bytes of `f` at `off` is about to be made by `fn` (the core function's
 * name, for the report).  Returns if f is whole, or if the bytes, up to the end of the
 * file, are all present; otherwise THE STOP: guest path, offset and length, the calling
 * module+rva and the fetch command. */
void rt_partial_require(const RtFile *f, uint64_t off, uint64_t len, const char *fn);

/* A read at the handle's position for a PARTIAL file (f->partial not NULL): the position is
 * read, the range required as above, the bytes read with pread and the position moved past
 * them, all under the file's own mutex.  Returns the bytes read (fewer than len only at the
 * end of the file), or -1 with errno if nothing could be read. */
ssize_t rt_partial_read(RtFile *f, void *buf, size_t len, const char *fn);

/* What is not built for a partial file (`what`: "a file mapping", "an open for writing"):
 * the stop whose command fetches the whole file, if `f` / the host file `host` is partial;
 * otherwise they return.  A `host` that does not exist returns.  (`host` is partial only if
 * it came from rt_partial_redirect, through rt_file_name_to_host.) */
void rt_partial_refuse(const RtFile *f, const char *fn, const char *what);
void rt_partial_refuse_host(const char *host, const char *fn, const char *what);

/* The translation of `name` answered `error` (ERROR_FILE_NOT_FOUND or ERROR_PATH_NOT_FOUND).
 * With a manifest: the stop "a file of the install is not on this Mac" if the install has
 * the name, or a directory on the way to it.  Otherwise returns (and warns once that there
 * is no manifest). */
void rt_gamedata_name_missing(RtName name, DWORD error);

/* FindFirstFile read the host directory `host_dir` for the pattern `pat`.  With a manifest:
 * the stop if the install has entries of that directory which match (`matches` is the
 * listing's own test) and are neither on this machine nor partial. */
void rt_gamedata_listing_check(const char *host_dir, const char *pat, bool (*matches)(const char *pat, const char *name));

/* ---- test hooks (core/file.c) ----------------------------------------------------------------- */

/* The host descriptor behind file handle `h`, or -1 if `h` is not an open file handle.  Sets
 * no last error.  The descriptor is the object's: a test may fstat or read through it and
 * must not close it. */
int rt_file_test_fd(HANDLE h);

/* The number of file objects alive now, the three standard ones included. */
unsigned rt_file_test_open_count(void);

#endif /* RT_FILE_PRIV_H */
