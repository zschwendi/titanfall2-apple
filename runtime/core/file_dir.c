/* Delete, move, create and remove a directory, drive type (docs/RUNTIME.md section 7.3).
 * Specification: rt/file.h.  Shared internals: core/file_priv.h (all built, in core/file.c).
 *
 * BUILT: this file is the piece `rt-file-dir` (docs/handoff/rt-contract-file.md has its task,
 * its test plan and its brief).  Each function does what rt/file.h states; a case the
 * specification leaves unverified STOPS.
 *
 * STATE: none.  LOCKS: none of its own (rt_file_log_if_open takes the file lock inside).
 * Every name is translated with the flags rt/file.h states: RT_PATH_DELETE and RT_PATH_RENAME
 * are what protects the skeleton of the virtual drive, and the check is made nowhere else.
 */
#include <errno.h>
#include <stdio.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <unistd.h>

#include "file_priv.h"

/* Two host entries are the same file: equal device and inode (rt/file.h, delete and move). */
static bool same_host_file(const struct stat *a, const struct stat *b) {
    return a->st_dev == b->st_dev && a->st_ino == b->st_ino;
}

/* ---- delete ----------------------------------------------------------------------------- */

/* A NULL name given to DeleteFile or CreateDirectory: ERROR_PATH_NOT_FOUND, not the
 * ERROR_INVALID_PARAMETER that rt_path_to_host_w(NULL) answers.  It is what the W functions
 * give on Windows, and one of the two values the A functions give (Wine's
 * kernel32/tests/file.c test_DeleteFileW and test_DeleteFileA, tests/directory.c
 * test_CreateDirectoryW and test_CreateDirectoryA). */
static bool name_is_null(RtName name) { return !name.w && !name.a; }

BOOL rt_file_delete(RtName name) {
    /* 1. The name: it must exist, and the skeleton of the virtual drive is refused here. */
    if (name_is_null(name)) return rt_file_fail(ERROR_PATH_NOT_FOUND);
    char host[RT_PATH_MAX];
    DWORD e = rt_file_name_to_host(name, RT_PATH_EXISTING | RT_PATH_DELETE, host);
    if (e) return rt_file_fail(e);

    /* 2. What the entry is: lstat, so a link is looked at, not what it leads to.  A directory
     * and a file with no owner write bit are refused and stay. */
    struct stat st;
    if (lstat(host, &st)) return rt_file_fail_errno(errno, RT_ERR_FILE);
    if (S_ISDIR(st.st_mode) || !(st.st_mode & S_IWUSR)) return rt_file_fail(ERROR_ACCESS_DENIED);

    /* 3. The delete.  The sharing log is written before the unlink, while the name still
     * leads to the file; an open file stays readable through its handles. */
    rt_partial_refuse_host(host, "rt_file_delete", "a delete");       /* game data only partly here */
    rt_file_log_if_open(host, "DeleteFile");
    if (unlink(host)) return rt_file_fail_errno(errno, RT_ERR_FILE);
    rt_file_invalidate_parent(host);
    return TRUE;
}

/* ---- move ------------------------------------------------------------------------------- */

BOOL rt_file_move(RtName from, RtName to, DWORD flags) {
    /* 1. What is not built: flags outside the three known, and the delete-at-reboot form. */
    if (flags & ~(MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH))
        RT_FILE_STOP("rt_file_move", "flags 0x%x", (unsigned)flags);
    if (!to.w && !to.a) RT_FILE_STOP("rt_file_move", "a NULL target: delete at reboot");

    /* 2. The names: the source first, then the target; the first error is the result. */
    char from_host[RT_PATH_MAX], to_host[RT_PATH_MAX];
    DWORD e = rt_file_name_to_host(from, RT_PATH_EXISTING | RT_PATH_RENAME, from_host);
    if (e) return rt_file_fail(e);
    e = rt_file_name_to_host(to, RT_PATH_CREATE | RT_PATH_RENAME, to_host);
    if (e) return rt_file_fail(e);

    /* 3. The target.  An existing entry that is not the source itself: refused unless
     * REPLACE_EXISTING; then a directory on either side is refused, and a read-only target
     * is not verified. */
    struct stat src, dst;
    if (lstat(from_host, &src)) return rt_file_fail_errno(errno, RT_ERR_FILE);
    rt_partial_refuse_host(from_host, "rt_file_move", "a move");       /* game data only partly here */
    rt_partial_refuse_host(to_host, "rt_file_move", "a move onto it");
    if (lstat(to_host, &dst) == 0 && !same_host_file(&src, &dst)) {
        if (!(flags & MOVEFILE_REPLACE_EXISTING)) return rt_file_fail(ERROR_ALREADY_EXISTS);
        if (S_ISDIR(src.st_mode) || S_ISDIR(dst.st_mode)) return rt_file_fail(ERROR_ACCESS_DENIED);
        if (!(dst.st_mode & S_IWUSR))
            RT_FILE_STOP("rt_file_move", "replacing the read-only file %s", to_host);
    }

    /* 4. The sharing log for the source, then the rename.  A move between two host volumes
     * is the copy that MOVEFILE_COPY_ALLOWED asks for: not built. */
    rt_file_log_if_open(from_host, "MoveFile");
    if (rename(from_host, to_host)) {
        const int en = errno;
        if (en == EXDEV && (flags & MOVEFILE_COPY_ALLOWED))
            RT_FILE_STOP("rt_file_move", "a move between two volumes with MOVEFILE_COPY_ALLOWED");
        return rt_file_fail_errno(en, RT_ERR_FILE);
    }

    /* 5. The cached listings of both directories. */
    rt_file_invalidate_parent(from_host);
    rt_file_invalidate_parent(to_host);
    return TRUE;
}

/* ---- directories ------------------------------------------------------------------------ */

BOOL rt_dir_create(RtName name) {
    /* 1. The name, for a new directory: a name that exists (as a file or a directory) is
     * refused by mkdir itself. */
    if (name_is_null(name)) return rt_file_fail(ERROR_PATH_NOT_FOUND);
    char host[RT_PATH_MAX];
    DWORD e = rt_file_name_to_host(name, RT_PATH_CREATE | RT_PATH_DIR, host);
    if (e) return rt_file_fail(e);

    /* 2. The directory call, then the parent's listing. */
    if (mkdir(host, 0755)) return rt_file_fail_errno(errno, RT_ERR_DIR);
    rt_file_invalidate_parent(host);
    return TRUE;
}

/* The guest's current directory is kept by the runtime as a Windows name (rt/path.h): the host
 * process never changes directory.  So the test translates that name and compares the host
 * entry it leads to with `host`, by device and inode.  A current directory with no host form
 * any more is not `host`. */
static bool leads_to_current_dir(const char *host) {
    struct stat a, b;
    if (stat(host, &a)) return false;
    WCHAR cwd[RT_PATH_MAX];                  /* the stored name has at most CWD_BUF-1 bytes: it fits */
    char cwd_host[RT_PATH_MAX];
    rt_path_get_cwd_w(cwd, RT_PATH_MAX);
    if (rt_file_name_to_host(RT_NAME_W(cwd), RT_PATH_EXISTING | RT_PATH_DIR, cwd_host)) return false;
    return stat(cwd_host, &b) == 0 && same_host_file(&a, &b);
}

BOOL rt_dir_remove(RtName name) {
    /* 1. The current directory first: a name that leads to it is ERROR_SHARING_VIOLATION, the
     * skeleton included (the install root is the start directory).  The name is translated
     * here without RT_PATH_DELETE; if that fails, step 2 gives the same error as before. */
    char host[RT_PATH_MAX];
    if (rt_file_name_to_host(name, RT_PATH_EXISTING | RT_PATH_DIR, host) == 0 && leads_to_current_dir(host))
        return rt_file_fail(ERROR_SHARING_VIOLATION);

    /* 2. The name: it must exist, and the skeleton of the virtual drive is refused here. */
    DWORD e = rt_file_name_to_host(name, RT_PATH_EXISTING | RT_PATH_DIR | RT_PATH_DELETE, host);
    if (e) return rt_file_fail(e);

    /* 3. What the name leads to must be a directory; a file is not built (not verified). */
    struct stat st;
    if (stat(host, &st)) return rt_file_fail_errno(errno, RT_ERR_DIR);
    if (!S_ISDIR(st.st_mode)) RT_FILE_STOP("rt_dir_remove", "a name that is not a directory: %s", host);

    /* 4. The directory call, then the parent's listing. */
    if (rmdir(host)) return rt_file_fail_errno(errno, RT_ERR_DIR);
    rt_file_invalidate_parent(host);
    return TRUE;
}

/* ---- drive type ------------------------------------------------------------------------- */

/* The first four code units of a root name, as numbers, and how many there are before the
 * NUL (at most four; nothing past the NUL is read).  A narrow name's units are its bytes. */
static unsigned root_units(RtName root, uint32_t u[4]) {
    unsigned n = 0;
    for (; n < 4; n++) {
        if (root.w) { if (!root.w[n]) break; u[n] = root.w[n]; }
        else        { if (!root.a[n]) break; u[n] = (unsigned char)root.a[n]; }
    }
    return n;
}

UINT rt_drive_type(RtName root) {
    /* Looks at the text only: no host call, no last error. */
    if (root.w && root.a) rt_fatal("rt_drive_type: a name with both a wide and a narrow form");
    if (!root.w && !root.a) return DRIVE_FIXED;     /* the drive of the current directory */

    uint32_t u[4] = { 0, 0, 0, 0 };
    const unsigned n = root_units(root, u);
    const bool c = (u[0] == 'C' || u[0] == 'c');
    const bool letter = c || (u[0] >= 'A' && u[0] <= 'Z') || (u[0] >= 'a' && u[0] <= 'z');
    /* "X:" answers as "X:\" does (review X9): no slash, or a slash of either kind. */
    if (letter && u[1] == ':' && (n == 2 || (n == 3 && (u[2] == '\\' || u[2] == '/'))))
        return c ? DRIVE_FIXED : DRIVE_NO_ROOT_DIR;
    /* "C", "c", "C?" (C in either case), "?" and "" answer DRIVE_NO_ROOT_DIR (review X9).  The
     * review lists C only, so another letter without a colon still STOPS (not verified). */
    if (n == 0 || (n == 1 && (c || u[0] == '?')) || (n == 2 && c && u[1] == '?'))
        return DRIVE_NO_ROOT_DIR;
    RT_FILE_STOP("rt_drive_type", "a root of %u units, starting 0x%x 0x%x 0x%x", n,
                 (unsigned)u[0], (unsigned)u[1], (unsigned)u[2]);
}

/* ---- disk space ----------------------------------------------------------------------------- */

/* blocks * block size, or a stop if the product does not fit 64 bits. */
static ULONGLONG disk_bytes(uint64_t blocks, uint64_t bsize, const char *host) {
    uint64_t bytes;
    if (__builtin_mul_overflow(blocks, bsize, &bytes))
        RT_FILE_STOP("rt_disk_space", "a volume of %llu blocks of %llu bytes behind %s",
                     (unsigned long long)blocks, (unsigned long long)bsize, host);
    return bytes;
}

BOOL rt_disk_space(RtName dir, ULONGLONG *avail, ULONGLONG *total, ULONGLONG *free) {
    /* 1. The name: NULL is the root of the only drive; the empty string is refused here, with
     * the code Windows gives, before the translation can say anything else to it. */
    static const WCHAR root[] = { 'C', ':', '\\', 0 };
    if (dir.w && dir.a) rt_fatal("rt_disk_space: a name with both a wide and a narrow form");
    if (name_is_null(dir)) dir = RT_NAME_W(root);
    if ((dir.w && !dir.w[0]) || (dir.a && !dir.a[0])) return rt_file_fail(ERROR_PATH_NOT_FOUND);
    char host[RT_PATH_MAX];
    DWORD e = rt_file_name_to_host(dir, RT_PATH_EXISTING | RT_PATH_DIR, host);
    if (e) return rt_file_fail(e);

    /* 2. It must lead to a directory; a file is not built (not verified). */
    struct stat st;
    if (stat(host, &st)) return rt_file_fail_errno(errno, RT_ERR_DIR);
    if (!S_ISDIR(st.st_mode)) RT_FILE_STOP("rt_disk_space", "a name that is not a directory: %s", host);

    /* 3. The host volume that holds it. */
    struct statfs fs;
    if (statfs(host, &fs)) return rt_file_fail_errno(errno, RT_ERR_DIR);
    const ULONGLONG a = disk_bytes(fs.f_bavail, fs.f_bsize, host);
    const ULONGLONG t = disk_bytes(fs.f_blocks, fs.f_bsize, host);
    const ULONGLONG f = disk_bytes(fs.f_bfree, fs.f_bsize, host);
    if (avail) *avail = a;
    if (total) *total = t;
    if (free) *free = f;
    return TRUE;
}
