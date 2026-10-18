/* File mappings: sections and views (docs/RUNTIME.md section 7.3).
 * Specification: rt/file.h.  Shared internals: core/file_priv.h (all built, in core/file.c).
 * Built by the unit rt-file-map (docs/handoff/rt-file-map.md).
 *
 * A SECTION is an object of kind RT_K_SECTION that owns one host descriptor: a dup of the
 * file's, or an unlinked temporary file under <state>/tmp for a section with no file.  Its
 * three fields are set once, before any other thread can reach the object, and never change:
 * a thread that holds a reference (through a handle, or found by name under map_lock) reads
 * them without a lock.
 *
 * A VIEW is a region of the VM map (rt/vm.h, FILE VIEWS) with the section's descriptor mapped
 * over it:
 *   1. ONE rt_vm_alloc(0, len, MEM_RESERVE | MEM_COMMIT, protect, RT_VM_MAPPED).  The base is
 *      64 KB aligned, so it is host-page aligned; every 4 KB page of the region is committed
 *      at once and nothing is ever committed inside it again (the map refuses a guest's
 *      MEM_COMMIT inside a view with the unimplemented report), so the map never writes zeros
 *      into a page of the file.
 *   2. mmap(MAP_FIXED) of the descriptor over [base, base + len rounded up to the HOST page),
 *      MAP_SHARED, or MAP_PRIVATE for a copy-on-write view (which this host makes a snapshot
 *      of the file: measured, see rt_view_map in rt/file.h).  The rounded range belongs to the
 *      region alone (no host page is shared by two regions), and the file offset is a multiple
 *      of 64 KB, which the host page divides (checked, not assumed).
 *   3. rt_vm_release(base, RT_VM_MAPPED) gives it back; never munmap.
 * WHAT THE 16 KB HOST PAGE COSTS.  The 4 KB pages between the end of the view and the end of
 * its last host page are in no region (VirtualQuery says free) but the host has the file
 * there: an access that faults on Windows reads, or with a read-write view writes, the file
 * at the offset the address stands for.  It never reaches another region or another file.
 * Bytes after the end of the FILE in its last host page read as zeros and are not written
 * back (the kernel's rule for a partial page); host pages wholly beyond the end of the file
 * cannot arise at creation (a section is never larger than its file: rt_section_create
 * extends the file or fails) and fault with SIGBUS if the file is truncated afterwards.
 *
 * STATE: `map_lock` guards the list of views and is THE lock of the kind RT_K_SECTION
 * (rt/handle.h, "A NAMED OBJECT IS VISIBLE BEFORE ITS CREATOR HAS FILLED IT IN"): rt_object_new
 * and the initialisation of a section are one critical section of it, and rt_section_open
 * takes it around rt_object_open.  Never held across mmap, rt_vm_alloc or rt_vm_release, nor
 * together with another lock of the file layer.  ONE host call is made under it: the
 * ftruncate that extends the file of a NEW section, because the file must have its size
 * before another thread can find the section by name and map a view of it.
 */
#include <errno.h>
#include <fcntl.h>
#include <os/lock.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "file_priv.h"

#include "rt/vm.h"

/* The object behind a handle of kind RT_K_SECTION. */
typedef struct RtSection {
    RtObject  obj;
    int       fd;               /* its own descriptor: a dup of the file's, or an unlinked temporary file */
    bool      owns_fd;          /* fd is set (a zeroed object must not close descriptor 0) */
    DWORD     protect;          /* PAGE_READONLY, PAGE_READWRITE or PAGE_WRITECOPY */
    ULONGLONG size;             /* bytes; a multiple of 4 KB for a section with no file */
} RtSection;

typedef struct RtView { uint64_t base, size; RtSection *section; struct RtView *next; } RtView;

static os_unfair_lock map_lock = OS_UNFAIR_LOCK_INIT;
static RtView        *views;            /* map_lock */
static atomic_uint    live_sections, live_views;
static atomic_uint    test_create_pause_us;     /* a test hook: rt_file_map_test_create_pause */

/* No section is larger than this: sizes and their roundings then fit off_t and never wrap. */
#define SIZE_LIMIT  ((ULONGLONG)1 << 62)
#define GPAGE  ((ULONGLONG)RT_GUEST_PAGE)
#define GRAN   ((ULONGLONG)RT_GUEST_GRANULARITY)

static void section_destroy(RtObject *obj) {
    RtSection *s = (RtSection *)obj;
    if (s->owns_fd) {
        close(s->fd);
        atomic_fetch_sub(&live_sections, 1);
    }
}

static const RtObjectOps section_ops = { .name = "section", .destroy = section_destroy };

static HANDLE section_fail(DWORD e) { rt_thread_set_last_error(e); return NULL; }
static uint64_t view_fail(DWORD e)  { rt_thread_set_last_error(e); return 0; }

/* The backing of a section with no file: an unlinked file of `size` bytes under <state>/tmp,
 * which reads as zeros.  -1 if the host refuses (logged). */
static int backing_file(ULONGLONG size) {
    char path[RT_PATH_MAX];
    int n = snprintf(path, sizeof path, "%s/tmp/rt-section.XXXXXX", rt_config()->state_dir);
    if (n < 0 || (size_t)n >= sizeof path) rt_fatal("rt_section_create: the state directory's path is too long");
    int fd = mkstemp(path);
    if (fd < 0) {
        rt_log(RT_CH_FILE, RT_LL_WARN, "section with no file: mkstemp %s: %s", path, strerror(errno));
        return -1;
    }
    unlink(path);
    if (fcntl(fd, F_SETFD, FD_CLOEXEC) != 0 || ftruncate(fd, (off_t)size) != 0) {
        rt_log(RT_CH_FILE, RT_LL_WARN, "section with no file: %llu bytes under %s/tmp: %s",
               (unsigned long long)size, rt_config()->state_dir, strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

HANDLE rt_section_create(HANDLE file, DWORD protect, ULONGLONG max_size, const WCHAR *name) {
    /* 1. Stops. */
    const DWORD prot = protect & ~SEC_COMMIT;
    if (prot != PAGE_READONLY && prot != PAGE_READWRITE && prot != PAGE_WRITECOPY)
        RT_FILE_STOP("rt_section_create", "protection 0x%x", protect);
    if (file == NULL)
        RT_FILE_STOP("rt_section_create", "a NULL file handle (Windows is remembered to treat it as "
                     "INVALID_HANDLE_VALUE, a section with no file; not verified)");

    /* 2. The descriptor and the size.  Nothing here changes a file. */
    int       fd;
    ULONGLONG size;
    bool      extend = false;
    if (file == INVALID_HANDLE_VALUE) {
        if (max_size == 0) return section_fail(ERROR_INVALID_PARAMETER);
        if (max_size > SIZE_LIMIT) return section_fail(ERROR_NOT_ENOUGH_MEMORY);
        size = (max_size + GPAGE - 1) & ~(GPAGE - 1);
        fd = backing_file(size);
        if (fd < 0) return section_fail(ERROR_NOT_ENOUGH_MEMORY);
    } else {
        RtFile *f = rt_file_ref(file);
        if (!f) return NULL;                                /* ERROR_INVALID_HANDLE */
        if (f->type != RT_FT_DISK)
            RT_FILE_STOP("rt_section_create", "a section of a directory or a character device (file type %u)", f->type);
        rt_partial_refuse(f, "rt_section_create", "a file mapping");    /* game data only partly here */
        struct stat st;
        DWORD e = 0;
        fd = -1;
        if (!f->can_read || (prot == PAGE_READWRITE && !f->can_write)) {
            e = ERROR_ACCESS_DENIED;
        } else if (fstat(f->fd, &st) != 0) {
            e = rt_win32_from_errno(errno, RT_ERR_FILE);
        } else if (max_size == 0 && st.st_size == 0) {
            e = ERROR_FILE_INVALID;
        } else if (max_size > (ULONGLONG)st.st_size && prot != PAGE_READWRITE) {
            e = ERROR_NOT_ENOUGH_MEMORY;
        } else if (max_size > SIZE_LIMIT) {
            e = ERROR_DISK_FULL;                            /* the extension below could not succeed */
        } else if ((fd = fcntl(f->fd, F_DUPFD_CLOEXEC, 0)) < 0) {
            e = rt_win32_from_errno(errno, RT_ERR_FILE);
        }
        rt_file_unref(f);
        if (e) return section_fail(e);
        size = max_size ? max_size : (ULONGLONG)st.st_size;
        extend = size > (ULONGLONG)st.st_size;
    }

    /* 3. The object.  One critical section from the name to the last field (rt/handle.h). */
    bool  existed = false, fd_given = false;
    DWORD err = 0;
    int   host_errno = 0;
    os_unfair_lock_lock(&map_lock);
    RtSection *s = (RtSection *)rt_object_new(RT_K_SECTION, sizeof *s, &section_ops, name, &existed);
    if (!s) {
        err = rt_thread_get_last_error();                   /* ERROR_INVALID_HANDLE, ERROR_NOT_ENOUGH_MEMORY */
    } else if (!existed) {
        /* The object is in the name space and holds nothing yet.  (A test makes this moment
         * long, to see that nobody can look.) */
        const unsigned pause = atomic_load(&test_create_pause_us);
        if (pause) usleep(pause);
        s->fd = fd;
        s->owns_fd = fd_given = true;                       /* the object closes it from here on */
        s->protect = prot;
        s->size = size;
        atomic_fetch_add(&live_sections, 1);
        if (extend && ftruncate(fd, (off_t)size) != 0) {
            /* Documented: "If the file on disk cannot be increased, CreateFileMapping fails and
             * GetLastError returns ERROR_DISK_FULL."  Nobody can have found the object: every
             * path to a section by name takes map_lock. */
            host_errno = errno;
            err = ERROR_DISK_FULL;
            rt_object_unref(&s->obj);
            s = NULL;
        }
    }
    os_unfair_lock_unlock(&map_lock);

    if (!s) {
        if (!fd_given) close(fd);
        if (host_errno)
            rt_log(RT_CH_FILE, RT_LL_WARN, "section: the file cannot be extended to %llu bytes: %s",
                   (unsigned long long)size, strerror(host_errno));
        return section_fail(err);
    }
    if (existed) {
        close(fd);
        if (extend)
            RT_FILE_STOP("rt_section_create", "the name exists and the call asks for %llu bytes of a smaller file "
                         "(whether Windows extends the file before it finds the name is not verified)",
                         (unsigned long long)size);
    }
    HANDLE h = rt_handle_new(&s->obj);
    if (!h) return NULL;                                    /* ERROR_NOT_ENOUGH_MEMORY; the reference is gone */
    rt_thread_set_last_error(existed ? ERROR_ALREADY_EXISTS : ERROR_SUCCESS);
    return h;
}

HANDLE rt_section_open(const WCHAR *name) {
    os_unfair_lock_lock(&map_lock);
    RtObject *o = rt_object_open(RT_K_SECTION, name);
    os_unfair_lock_unlock(&map_lock);
    if (!o) return NULL;                                    /* rt_object_open's three codes */
    return rt_handle_new(o);                                /* NULL: ERROR_NOT_ENOUGH_MEMORY */
}

uint64_t rt_view_map(HANDLE section, DWORD access, ULONGLONG offset, SIZE_T size) {
    /* 1. The access, and the stop for every form that is not documented one way. */
    DWORD prot;
    if (access == FILE_MAP_WRITE || access == (FILE_MAP_WRITE | FILE_MAP_READ) || access == FILE_MAP_ALL_ACCESS)
        prot = PAGE_READWRITE;
    else if (access == FILE_MAP_READ)
        prot = PAGE_READONLY;
    else if (access == FILE_MAP_COPY || access == (FILE_MAP_COPY | FILE_MAP_READ))
        prot = PAGE_WRITECOPY;
    else
        RT_FILE_STOP("rt_view_map", "access 0x%x", access);

    const uint64_t hp = rt_vm_host_page();
    if (hp == 0 || GRAN % hp) rt_fatal("rt_view_map: the host page (%llu bytes) does not divide 64 KB", (unsigned long long)hp);

    /* 2. The arguments. */
    if (offset & (GRAN - 1)) return view_fail(ERROR_MAPPED_ALIGNMENT);
    RtSection *s = (RtSection *)rt_handle_ref(section, RT_K_SECTION);
    if (!s) return 0;                                       /* ERROR_INVALID_HANDLE */

    const ULONGLONG paged = (s->size + GPAGE - 1) & ~(GPAGE - 1);   /* cannot wrap: SIZE_LIMIT */
    ULONGLONG len = 0;
    DWORD err = 0;
    if (prot == PAGE_READWRITE && s->protect != PAGE_READWRITE) err = ERROR_ACCESS_DENIED;
    else if (offset >= s->size) err = ERROR_ACCESS_DENIED;
    else if (size == 0) len = s->size - offset;
    else if (size > paged - offset) err = ERROR_ACCESS_DENIED;
    else if (size > s->size - offset)
        RT_FILE_STOP("rt_view_map", "a view of 0x%llx bytes at offset 0x%llx that ends after the section's 0x%llx bytes "
                     "but inside its last 4 KB page (not verified which of the two Windows compares with)",
                     (unsigned long long)size, (unsigned long long)offset, (unsigned long long)s->size);
    else len = size;

    RtView *v = err ? NULL : malloc(sizeof *v);
    if (!err && !v) err = ERROR_NOT_ENOUGH_MEMORY;
    if (err) {
        rt_object_unref(&s->obj);
        return view_fail(err);
    }

    /* 3. The region, then the file over it (the top of this file). */
    uint64_t base = rt_vm_alloc(0, len, MEM_RESERVE | MEM_COMMIT, prot, RT_VM_MAPPED);
    if (!base) {                                            /* rt_vm_alloc set last error */
        free(v);
        rt_object_unref(&s->obj);
        return 0;
    }
    const uint64_t host_len = (len + hp - 1) & ~(hp - 1);
    void *p = mmap((void *)(uintptr_t)base, host_len, prot == PAGE_READONLY ? PROT_READ : PROT_READ | PROT_WRITE,
                   MAP_FIXED | (prot == PAGE_WRITECOPY ? MAP_PRIVATE : MAP_SHARED), s->fd, (off_t)offset);
    if (p == MAP_FAILED) {
        int host_errno = errno;
        /* Whatever the failed call left there, the release puts fresh inaccessible pages over
         * the whole range. */
        if (!rt_vm_release(base, RT_VM_MAPPED))
            rt_fatal("rt_view_map: the VM map lost the region at %#llx", (unsigned long long)base);
        rt_log(RT_CH_FILE, RT_LL_WARN, "view of %#llx bytes at offset %#llx: mmap: %s", (unsigned long long)len,
               (unsigned long long)offset, strerror(host_errno));
        free(v);
        rt_object_unref(&s->obj);
        return view_fail(rt_win32_from_errno(host_errno, RT_ERR_FILE));
    }

    if (prot == PAGE_WRITECOPY)
        rt_log_once(RT_CH_FILE, RT_LL_WARN, "copy-on-write view",
                    "a copy-on-write view (FILE_MAP_COPY) at %#llx: on this host it is a snapshot of the file as "
                    "it is now and does not follow later changes of the file (rt/file.h, rt_view_map; logged once)",
                    (unsigned long long)base);

    *v = (RtView){ .base = base, .size = len, .section = s };      /* the view keeps the reference */
    os_unfair_lock_lock(&map_lock);
    v->next = views;
    views = v;
    os_unfair_lock_unlock(&map_lock);
    atomic_fetch_add(&live_views, 1);
    return base;
}

BOOL rt_view_unmap(uint64_t base) {
    /* The view that CONTAINS the address: Windows unmaps a view for any address inside it,
     * not only for its base (Wine's kernel32/tests/virtual.c test_MapViewOfFile unmaps with
     * addresses 100, 4096 and 4196 bytes into a view and Windows succeeds; the
     * documentation only says what the caller must pass, "identical to the value returned"). */
    RtView  *v = NULL;
    os_unfair_lock_lock(&map_lock);
    for (RtView **pp = &views; *pp; pp = &(*pp)->next) {
        if (base - (*pp)->base < (*pp)->size) {
            v = *pp;
            *pp = v->next;                                  /* out of the list BEFORE the range can be reused */
            break;
        }
    }
    os_unfair_lock_unlock(&map_lock);
    if (!v) return rt_file_fail(ERROR_INVALID_ADDRESS);
    if (base != v->base)
        rt_log(RT_CH_FILE, RT_LL_DEBUG, "UnmapViewOfFile(%#llx): an address inside the view at %#llx; the view is unmapped",
               (unsigned long long)base, (unsigned long long)v->base);
    base = v->base;
    if (!rt_vm_release(base, RT_VM_MAPPED))
        rt_fatal("rt_view_unmap: the VM map has no mapped region at %#llx for a view of the list", (unsigned long long)base);
    atomic_fetch_sub(&live_views, 1);
    rt_object_unref(&v->section->obj);                      /* no lock held: this may close the descriptor */
    free(v);
    return TRUE;
}

/* ---- test hooks -------------------------------------------------------------------------------- */

/* Make every creation of a new section sleep for `usec` microseconds between rt_object_new
 * and the initialisation of the fields, inside the critical section; 0 turns it off. */
void rt_file_map_test_create_pause(unsigned usec);
void rt_file_map_test_create_pause(unsigned usec) { atomic_store(&test_create_pause_us, usec); }

/* The number of section objects and of views alive now.  Either pointer may be NULL.  (Tests
 * declare it by hand, as the hooks of core/file.c.) */
void rt_file_map_test_counts(unsigned *sections, unsigned *views_out);
void rt_file_map_test_counts(unsigned *sections, unsigned *views_out) {
    if (sections) *sections = atomic_load(&live_sections);
    if (views_out) *views_out = atomic_load(&live_views);
}
