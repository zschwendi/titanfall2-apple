/* Read, write, position, flush, size and end of file (docs/RUNTIME.md section 7.3).
 * Specification: rt/file.h.  Shared internals: core/file_priv.h (all built, in core/file.c).
 *
 * BUILT: every function here.  An OVERLAPPED given to read or write goes to rt_file_ov_read
 * and rt_file_ov_write (core/file_ov.c, another piece of the file layer).
 *
 * STATE: none.  The position is the host descriptor's; each function is rt_file_ref, the
 * checks in the order rt/file.h gives, one host call, rt_file_unref on every path.
 * LOCKS: none taken, none held by the caller.
 */
#include <errno.h>
#include <unistd.h>

#include "file_priv.h"

/* A host read of more than this many bytes is made in pieces (rt/file.h, ReadFile step 8). */
#define RT_FILE_PIECE  0x40000000u

/* One host read or write of n bytes at the position, again only if a signal came before any
 * byte (rt/file.h, ReadFile step 8). */
static ssize_t host_read(int fd, void *p, size_t n) {
    ssize_t r;
    do r = read(fd, p, n); while (r < 0 && errno == EINTR);
    return r;
}

static ssize_t host_write(int fd, const void *p, size_t n) {
    ssize_t r;
    do r = write(fd, p, n); while (r < 0 && errno == EINTR);
    return r;
}

/* A read or write with no OVERLAPPED through a disk file's handle that was opened with
 * FILE_FLAG_OVERLAPPED: such a handle has no position to read at, and Windows refuses the
 * call with ERROR_INVALID_PARAMETER, whatever its length (Wine's tests on Windows:
 * ntdll/tests/file.c test_read_write; ReadFile: "The lpOverlapped parameter must not be
 * NULL").  A character device takes no offset and is not refused. */
static bool no_offset_refused(const RtFile *f) {
    return f->overlapped && f->type == RT_FT_DISK;
}

BOOL rt_file_read(HANDLE h, void *buf, DWORD len, DWORD *got, OVERLAPPED *ov) {
    if (!got && !ov) RT_FILE_STOP("rt_file_read", "got NULL and no OVERLAPPED");
    if (got) *got = 0;
    RtFile *f = rt_file_ref(h);
    if (!f) return FALSE;
    if (ov) {
        const BOOL ok = rt_file_ov_read(f, buf, len, got, ov);
        rt_file_unref(f);
        return ok;
    }
    /* A directory first: rt_file_create gives a directory handle neither read nor write
     * access, so the access test below would answer for it and the stop would never be
     * reached. */
    if (f->type == RT_FT_DIR) {
        rt_file_unref(f);
        RT_FILE_STOP("rt_file_read", "a directory handle %p", (void *)h);
    }
    if (!f->can_read) { rt_file_unref(f); return rt_file_fail(ERROR_ACCESS_DENIED); }
    if (no_offset_refused(f)) { rt_file_unref(f); return rt_file_fail(ERROR_INVALID_PARAMETER); }
    if (len == 0) { rt_file_unref(f); return TRUE; }
    if (f->partial) {
        /* An install file that is only partly here: every byte is checked against its map
         * before it is read, and a read that touches an absent one ends the run. */
        const ssize_t n = rt_partial_read(f, buf, len, "rt_file_read");
        const int host_errno = errno;
        rt_file_unref(f);
        if (n < 0) return rt_file_fail_errno(host_errno, RT_ERR_FILE);
        *got = (DWORD)n;
        return TRUE;
    }
    DWORD total = 0;
    int en = 0;
    while (total < len) {
        const size_t want = (len - total) < RT_FILE_PIECE ? (size_t)(len - total) : RT_FILE_PIECE;
        const ssize_t n = host_read(f->fd, (uint8_t *)buf + total, want);
        if (n < 0) { en = errno; break; }
        total += (DWORD)n;
        if ((size_t)n < want) break;                /* the end of the file: a short piece */
    }
    rt_file_unref(f);
    *got = total;
    if (en && total == 0) return rt_file_fail_errno(en, RT_ERR_FILE);
    return TRUE;
}

BOOL rt_file_write(HANDLE h, const void *buf, DWORD len, DWORD *put, OVERLAPPED *ov) {
    if (!put && !ov) RT_FILE_STOP("rt_file_write", "put NULL and no OVERLAPPED");
    if (put) *put = 0;
    RtFile *f = rt_file_ref(h);
    if (!f) return FALSE;
    if (ov) {
        const BOOL ok = rt_file_ov_write(f, buf, len, put, ov);
        rt_file_unref(f);
        return ok;
    }
    /* A directory first: rt_file_create gives a directory handle neither read nor write
     * access, so the access test below would answer for it and the stop would never be
     * reached. */
    if (f->type == RT_FT_DIR) {
        rt_file_unref(f);
        RT_FILE_STOP("rt_file_write", "a directory handle %p", (void *)h);
    }
    if (!f->can_write) { rt_file_unref(f); return rt_file_fail(ERROR_ACCESS_DENIED); }
    if (no_offset_refused(f)) { rt_file_unref(f); return rt_file_fail(ERROR_INVALID_PARAMETER); }
    if (len == 0) { rt_file_unref(f); return TRUE; }
    DWORD total = 0;
    int en = 0;
    while (total < len) {
        const ssize_t n = host_write(f->fd, (const uint8_t *)buf + total, len - total);
        if (n < 0) { en = errno; break; }
        total += (DWORD)n;
    }
    rt_file_unref(f);
    *put = total;
    if (en) return rt_file_fail_errno(en, RT_ERR_FILE);
    return TRUE;
}

BOOL rt_file_seek(HANDLE h, LONGLONG distance, LONGLONG *new_pos, DWORD method) {
    RtFile *f = rt_file_ref(h);
    if (!f) return FALSE;
    if (f->type != RT_FT_DISK) {
        rt_file_unref(f);
        RT_FILE_STOP("rt_file_seek", "a character device or a directory handle %p", (void *)h);
    }
    int whence;
    switch (method) {
    case FILE_BEGIN:   whence = SEEK_SET; break;
    case FILE_CURRENT: whence = SEEK_CUR; break;
    case FILE_END:     whence = SEEK_END; break;
    default: rt_file_unref(f); return rt_file_fail(ERROR_INVALID_PARAMETER);
    }
    const off_t pos = lseek(f->fd, (off_t)distance, whence);
    const int en = errno;
    rt_file_unref(f);
    if (pos < 0) return rt_file_fail(en == EINVAL ? ERROR_NEGATIVE_SEEK
                                                  : rt_win32_from_errno(en, RT_ERR_FILE));
    if (new_pos) *new_pos = (LONGLONG)pos;
    return TRUE;
}

BOOL rt_file_flush(HANDLE h) {
    RtFile *f = rt_file_ref(h);
    if (!f) return FALSE;
    if (f->type != RT_FT_DISK) {
        rt_file_unref(f);
        RT_FILE_STOP("rt_file_flush", "a character device or a directory handle %p", (void *)h);
    }
    if (!f->can_write) { rt_file_unref(f); return rt_file_fail(ERROR_ACCESS_DENIED); }
    const int en = fsync(f->fd) ? errno : 0;
    rt_file_unref(f);
    if (en) return rt_file_fail_errno(en, RT_ERR_FILE);
    return TRUE;
}

BOOL rt_file_size(HANDLE h, LONGLONG *size) {
    if (!size) RT_FILE_STOP("rt_file_size", "size NULL");
    RtFile *f = rt_file_ref(h);
    if (!f) return FALSE;
    if (f->type != RT_FT_DISK) {
        rt_file_unref(f);
        RT_FILE_STOP("rt_file_size", "a character device or a directory handle %p", (void *)h);
    }
    struct stat st;
    const int en = fstat(f->fd, &st) ? errno : 0;
    rt_file_unref(f);
    if (en) return rt_file_fail_errno(en, RT_ERR_FILE);
    *size = (LONGLONG)st.st_size;
    return TRUE;
}

BOOL rt_file_set_eof(HANDLE h) {
    RtFile *f = rt_file_ref(h);
    if (!f) return FALSE;
    if (f->type != RT_FT_DISK) {
        rt_file_unref(f);
        RT_FILE_STOP("rt_file_set_eof", "a character device or a directory handle %p", (void *)h);
    }
    if (!f->can_write) { rt_file_unref(f); return rt_file_fail(ERROR_ACCESS_DENIED); }
    const off_t pos = lseek(f->fd, 0, SEEK_CUR);     /* the position does not move */
    int en = pos < 0 ? errno : 0;
    if (!en && ftruncate(f->fd, pos)) en = errno;
    rt_file_unref(f);
    if (en) return rt_file_fail_errno(en, RT_ERR_FILE);
    return TRUE;
}
