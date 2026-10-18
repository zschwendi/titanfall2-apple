/* Attributes, times and information by handle (docs/RUNTIME.md section 7.3).
 * Specification: rt/file.h.  Shared internals: core/file_priv.h (all built, in core/file.c).
 *
 * BUILT: each function does what the attributes-and-times section of rt/file.h says, and
 * stops or fails exactly where that section says; none returns a default.  Names are
 * translated with RT_PATH_EXISTING, and every attribute, time and size comes from
 * rt_file_attr_from_stat.
 * 
 * STATE: none.  Attributes, times and sizes come from rt_file_attr_from_stat and nowhere else.
 * LOCKS: none.
 */
#include <errno.h>
#include <sys/attr.h>
#include <sys/stat.h>
#include <unistd.h>

#include "file_priv.h"

/* FILETIME of 1970-01-01.  A FILETIME of 0 means "unchanged" to SetFileTime. */
#define FILETIME_UNIX_EPOCH  116444736000000000ull
/* The host mode's write bits: READONLY removes them all; a clear READONLY gives back the owner's. */
#define WRITE_BITS  0222
/* Accepted by SetFileAttributes and not stored: only READONLY is. */
#define NOT_STORED_ATTRS  (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE | \
                           FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED)
#define SET_ATTRS_OK  (FILE_ATTRIBUTE_READONLY | NOT_STORED_ATTRS)

static DWORD fail_dword(DWORD e) { rt_thread_set_last_error(e); return INVALID_FILE_ATTRIBUTES; }

static uint64_t filetime_value(const FILETIME *t) {
    return ((uint64_t)t->dwHighDateTime << 32) | t->dwLowDateTime;
}

/* A FILETIME this piece cannot store: 0xFFFFFFFF:0xFFFFFFFF, or a time before 1970 that is not 0. */
static bool bad_time(uint64_t t) { return t == ~(uint64_t)0 || (t && t < FILETIME_UNIX_EPOCH); }

/* The timespec futimens takes for a FILETIME: UTIME_OMIT for 0, which leaves the time alone. */
static struct timespec time_spec(uint64_t t) {
    struct timespec ts = { 0, UTIME_OMIT };
    if (t) {
        const uint64_t rel = t - FILETIME_UNIX_EPOCH;
        ts.tv_sec = (time_t)(rel / 10000000u);
        ts.tv_nsec = (long)((rel % 10000000u) * 100u);
    }
    return ts;
}

/* The name translated with RT_PATH_EXISTING and stat-ed: 0, with `host` and *st filled, or the
 * Win32 error with nothing set.  The caller stops on NUL, which names its own function. */
static DWORD stat_name(RtName name, char host[RT_PATH_MAX], struct stat *st) {
    const DWORD e = rt_file_name_to_host(name, RT_PATH_EXISTING, host);
    if (e) return e;
    if (stat(host, st)) return rt_win32_from_errno(errno, RT_ERR_FILE);
    return ERROR_SUCCESS;
}

DWORD rt_file_get_attributes(RtName name) {
    char host[RT_PATH_MAX];
    struct stat st;
    const DWORD e = stat_name(name, host, &st);
    if (e) return fail_dword(e);
    if (rt_file_host_is_null(host)) RT_FILE_STOP("rt_file_get_attributes", "the NUL device");
    DWORD attrs = 0;
    rt_file_attr_from_stat(&st, &attrs, NULL, NULL, NULL, NULL);
    return attrs;
}

BOOL rt_file_get_attributes_ex(RtName name, DWORD level, WIN32_FILE_ATTRIBUTE_DATA *out) {
    if (!out) RT_FILE_STOP("rt_file_get_attributes_ex", "out NULL");
    if (level != GetFileExInfoStandard) return rt_file_fail(ERROR_INVALID_PARAMETER);
    char host[RT_PATH_MAX];
    struct stat st;
    const DWORD e = stat_name(name, host, &st);
    if (e) return rt_file_fail(e);
    if (rt_file_host_is_null(host)) RT_FILE_STOP("rt_file_get_attributes_ex", "the NUL device");
    DWORD attrs = 0;
    FILETIME creation, last_access, last_write;
    ULONGLONG size = 0;
    rt_file_attr_from_stat(&st, &attrs, &creation, &last_access, &last_write, &size);
    out->dwFileAttributes = attrs;
    out->ftCreationTime = creation;
    out->ftLastAccessTime = last_access;
    out->ftLastWriteTime = last_write;
    out->nFileSizeHigh = (DWORD)(size >> 32);
    out->nFileSizeLow = (DWORD)size;
    return TRUE;
}

BOOL rt_file_set_attributes(RtName name, DWORD attrs) {
    if (attrs & ~(SET_ATTRS_OK | FILE_ATTRIBUTE_DIRECTORY)) RT_FILE_STOP("rt_file_set_attributes", "attributes 0x%x", attrs);
    char host[RT_PATH_MAX];
    struct stat st;
    DWORD cur = 0;
    const DWORD e = stat_name(name, host, &st);
    if (!e) rt_file_attr_from_stat(&st, &cur, NULL, NULL, NULL, NULL);
    /* A directory: nothing changes, and FILE_ATTRIBUTE_DIRECTORY (which GetFileAttributes returns
     * for one) is accepted and ignored.  DIRECTORY on any other name stops, a missing one too: it
     * is not known to be a directory, and that case is not built. */
    if (cur & FILE_ATTRIBUTE_DIRECTORY) return TRUE;
    if (attrs & FILE_ATTRIBUTE_DIRECTORY)
        RT_FILE_STOP("rt_file_set_attributes", "attributes 0x%x on a name that is not a directory", attrs);
    if (e) return rt_file_fail(e);
    const mode_t mode = st.st_mode & 07777;
    const mode_t want = (attrs & FILE_ATTRIBUTE_READONLY) ? (mode & ~WRITE_BITS) : (mode | S_IWUSR);
    if (chmod(host, want)) return rt_file_fail_errno(errno, RT_ERR_FILE);
    if (attrs & NOT_STORED_ATTRS)
        rt_log(RT_CH_FILE, RT_LL_DEBUG, "SetFileAttributes of %s: 0x%x is not stored, only READONLY is",
               host, attrs & NOT_STORED_ATTRS);
    return TRUE;
}

BOOL rt_file_info_by_handle(HANDLE h, BY_HANDLE_FILE_INFORMATION *out) {
    if (!out) RT_FILE_STOP("rt_file_info_by_handle", "out NULL");
    RtFile *f = rt_file_ref(h);
    if (!f) return FALSE;
    if (f->type == RT_FT_CHAR) RT_FILE_STOP("rt_file_info_by_handle", "a character device, handle %p", (void *)h);
    struct stat st;
    if (fstat(f->fd, &st)) {
        const int en = errno;
        rt_file_unref(f);
        return rt_file_fail_errno(en, RT_ERR_FILE);
    }
    rt_file_unref(f);
    DWORD attrs = 0;
    FILETIME creation, last_access, last_write;
    ULONGLONG size = 0;
    rt_file_attr_from_stat(&st, &attrs, &creation, &last_access, &last_write, &size);
    out->dwFileAttributes = attrs;
    out->ftCreationTime = creation;
    out->ftLastAccessTime = last_access;
    out->ftLastWriteTime = last_write;
    out->dwVolumeSerialNumber = RT_VOLUME_SERIAL;
    out->nFileSizeHigh = (DWORD)(size >> 32);
    out->nFileSizeLow = (DWORD)size;
    out->nNumberOfLinks = (attrs & FILE_ATTRIBUTE_DIRECTORY) ? 1 : (DWORD)st.st_nlink;
    out->nFileIndexHigh = (DWORD)((uint64_t)st.st_ino >> 32);
    out->nFileIndexLow = (DWORD)(uint64_t)st.st_ino;
    return TRUE;
}

BOOL rt_file_set_times(HANDLE h, const FILETIME *creation, const FILETIME *access, const FILETIME *write) {
    /* 1. What is not built: a FILETIME of 0xFFFFFFFF:0xFFFFFFFF, and a time before 1970 (each of
     * the three, unless it is 0). */
    const uint64_t c = creation ? filetime_value(creation) : 0;
    const uint64_t a = access ? filetime_value(access) : 0;
    const uint64_t w = write ? filetime_value(write) : 0;
    if (bad_time(c)) RT_FILE_STOP("rt_file_set_times", "a creation time 0x%llx", (unsigned long long)c);
    if (bad_time(a)) RT_FILE_STOP("rt_file_set_times", "an access time 0x%llx", (unsigned long long)a);
    if (bad_time(w)) RT_FILE_STOP("rt_file_set_times", "a write time 0x%llx", (unsigned long long)w);

    /* 2. The handle, then the host calls: the access and write times with futimens (none if both
     * are left alone), then the birth time with fsetattrlist (none if it is left alone).  The birth
     * time comes last: futimens lowers the birth time to a modification time that is earlier, so a
     * creation time set first would not stick. */
    RtFile *f = rt_file_ref(h);
    if (!f) return FALSE;
    if (f->type == RT_FT_CHAR) RT_FILE_STOP("rt_file_set_times", "a character device, handle %p", (void *)h);
    if (a || w) {
        const struct timespec ts[2] = { time_spec(a), time_spec(w) };
        if (futimens(f->fd, ts)) {
            const int en = errno;
            rt_file_unref(f);
            return rt_file_fail_errno(en, RT_ERR_FILE);
        }
    }
    if (c) {
        struct attrlist al = { .bitmapcount = ATTR_BIT_MAP_COUNT, .commonattr = ATTR_CMN_CRTIME };
        struct timespec birth = time_spec(c);
        if (fsetattrlist(f->fd, &al, &birth, sizeof birth, 0)) {
            const int en = errno;
            rt_file_unref(f);
            return rt_file_fail_errno(en, RT_ERR_FILE);
        }
    }
    rt_file_unref(f);
    return TRUE;
}
