/* File mappings (rt/file.h, the section "file mappings"): rt_section_create, rt_section_open,
 * rt_view_map and rt_view_unmap, and what a view is to the VM map (rt/vm.h).  Each TEST is its
 * own process; every numbered case sets up what it needs, so RT_TEST_PLAN_CASE=<n> runs one
 * alone.  The plan is docs/handoff/rt-contract-file.md, "rt-file-map", refined: cases 1 to 12
 * are sections, 13 to 28 views, 29 and 30 threads.
 *
 * Files are made on the host under rt_test_install_root() (C:\Titanfall2) and opened with
 * rt_file_create.  "LE untouched" is rt_thread_set_last_error(0xDEAD) before the call and
 * 0xDEAD after it.  The workers of the last TEST are plain host threads that only count.
 */
#include <fcntl.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/file.h"
#include "rt/sync.h"
#include "rt/thread.h"
#include "rt/vm.h"

int      rt_file_test_fd(HANDLE h);
unsigned rt_file_test_open_count(void);
void     rt_file_map_test_counts(unsigned *sections, unsigned *views);
void     rt_file_map_test_create_pause(unsigned usec);

#define RD   GENERIC_READ
#define WR   GENERIC_WRITE
#define SH   (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE)
#define BAD  INVALID_HANDLE_VALUE
#define NOFILE  INVALID_HANDLE_VALUE

static const WCHAR *W(const char *a) {
    static WCHAR buf[4][512];
    static unsigned k;
    WCHAR *b = buf[k++ & 3];
    size_t i = 0;
    for (; a[i]; i++) b[i] = (unsigned char)a[i];
    b[i] = 0;
    return b;
}
static const char *I(const char *rel) {
    static char buf[4][2048];
    static unsigned k;
    char *b = buf[k++ & 3];
    snprintf(b, sizeof buf[0], "%s/%s", rt_test_install_root(), rel);
    return b;
}
static void put(const char *rel, const char *text, mode_t mode) {
    int fd = open(I(rel), O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0 || write(fd, text, strlen(text)) != (ssize_t)strlen(text)) rt_test_fail(__FILE__, __LINE__, "put %s", rel);
    if (fd >= 0) { fchmod(fd, mode); close(fd); }
}
static HANDLE cfa(const char *win, DWORD access, DWORD share, DWORD disp, DWORD fa) {
    return rt_file_create(RT_NAME_A(win), access, share, disp, fa, NULL);
}

/* ---- this file's helpers --------------------------------------------------------------------- */

/* Byte i of a pattern file: never 0, and different in every 4 KB page at the same offset. */
static uint8_t pat(uint64_t i) { return (uint8_t)(1 + (i * 7 + (i >> 12) * 13) % 251); }

static void put_pat(const char *rel, size_t size) {
    static uint8_t buf[0x40000];
    if (size > sizeof buf) { rt_test_fail(__FILE__, __LINE__, "put_pat %s: too large", rel); return; }
    for (size_t i = 0; i < size; i++) buf[i] = pat(i);
    int fd = open(I(rel), O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0 || write(fd, buf, size) != (ssize_t)size) rt_test_fail(__FILE__, __LINE__, "put_pat %s", rel);
    if (fd >= 0) close(fd);
}
/* The host file, read through a descriptor of its own. */
static uint64_t host_size(const char *rel) {
    struct stat st;
    return stat(I(rel), &st) == 0 ? (uint64_t)st.st_size : UINT64_MAX;
}
static int host_byte(const char *rel, uint64_t off) {
    uint8_t b = 0;
    int fd = open(I(rel), O_RDONLY);
    ssize_t n = fd < 0 ? -1 : pread(fd, &b, 1, (off_t)off);
    if (fd >= 0) close(fd);
    return n == 1 ? b : -1;
}
/* True if host file bytes [from, to) are the pattern. */
static bool host_is_pat(const char *rel, uint64_t from, uint64_t to) {
    static uint8_t buf[0x40000];
    int fd = open(I(rel), O_RDONLY);
    ssize_t n = fd < 0 ? -1 : pread(fd, buf, to - from, (off_t)from);
    if (fd >= 0) close(fd);
    if (n != (ssize_t)(to - from)) return false;
    for (uint64_t i = 0; i < to - from; i++) if (buf[i] != pat(from + i)) return false;
    return true;
}
static volatile uint8_t *M(uint64_t addr) { return (volatile uint8_t *)(uintptr_t)addr; }
/* True if memory [addr, addr + n) is the pattern of file offsets [off, off + n). */
static bool mem_is_pat(uint64_t addr, uint64_t off, uint64_t n) {
    for (uint64_t i = 0; i < n; i++) if (M(addr)[i] != pat(off + i)) return false;
    return true;
}
static bool mem_is_zero(uint64_t addr, uint64_t n) {
    for (uint64_t i = 0; i < n; i++) if (M(addr)[i] != 0) return false;
    return true;
}

/* Touch one byte and say whether the host let it happen (as vu_touch of vm_util.c).  A write
 * stores back the byte it read. */
static sigjmp_buf fault_jmp;
static void on_fault(int sig) { (void)sig; siglongjmp(fault_jmp, 1); }
static bool touch(uint64_t addr, bool write) {
    struct sigaction sa, old_bus, old_segv;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_fault;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGBUS, &sa, &old_bus);
    sigaction(SIGSEGV, &sa, &old_segv);
    volatile bool ok = false;
    if (sigsetjmp(fault_jmp, 1) == 0) {
        uint8_t v = *M(addr);
        if (write) *M(addr) = v;
        ok = true;
    }
    sigaction(SIGBUS, &old_bus, NULL);
    sigaction(SIGSEGV, &old_segv, NULL);
    return ok;
}

static MEMORY_BASIC_INFORMATION Q(uint64_t addr) {
    MEMORY_BASIC_INFORMATION m;
    memset(&m, 0xA5, sizeof m);
    T_EQ(rt_vm_query(addr, &m), sizeof m);
    return m;
}
/* A view as VirtualQuery must describe it: one run of committed MEM_MAPPED pages. */
static void view_is(uint64_t base, uint64_t bytes, DWORD protect) {
    MEMORY_BASIC_INFORMATION m = Q(base);
    T_EQ(m.BaseAddress, base);
    T_EQ(m.AllocationBase, base);
    T_EQ(m.RegionSize, bytes);
    T_EQ(m.State, MEM_COMMIT);
    T_EQ(m.Protect, protect);
    T_EQ(m.Type, MEM_MAPPED);
    T_EQ(base & 0xFFFF, 0);
    T_TRUE(rt_vm_in_vm_arena(base));
}
static void counts_are(unsigned sections, unsigned views) {
    unsigned s = 99, v = 99;
    rt_file_map_test_counts(&s, &v);
    T_EQ(s, sections);
    T_EQ(v, views);
}
/* The object behind a section handle, as a number (no reference kept); 0 if it is none. */
static uintptr_t obj_of(HANDLE h) {
    RtObject *o = rt_handle_ref(h, RT_K_SECTION);
    if (o) rt_object_unref(o);
    return (uintptr_t)o;
}

/* A successful create: the handle, with the last error the call must leave. */
static HANDLE sec(HANDLE file, DWORD protect, ULONGLONG size, const WCHAR *name, DWORD le) {
    rt_thread_set_last_error(0xDEAD);
    HANDLE h = rt_section_create(file, protect, size, name);
    T_TRUE(h != NULL);
    T_EQ(T_LAST_ERROR(), le);
    return h;
}
#define SEC_FAILS(err, file, protect, size, name) \
    do { rt_thread_set_last_error(0xDEAD); \
         T_TRUE(rt_section_create((file), (protect), (size), (name)) == NULL); \
         T_EQ(T_LAST_ERROR(), (err)); } while (0)
/* A successful map: the base, last error untouched. */
static uint64_t map(HANDLE s, DWORD access, ULONGLONG offset, SIZE_T size) {
    rt_thread_set_last_error(0xDEAD);
    uint64_t base = rt_view_map(s, access, offset, size);
    T_TRUE(base != 0);
    T_EQ(T_LAST_ERROR(), 0xDEAD);
    return base;
}
#define MAP_FAILS(err, s, access, offset, size) \
    do { rt_thread_set_last_error(0xDEAD); \
         T_EQ(rt_view_map((s), (access), (offset), (size)), 0); \
         T_EQ(T_LAST_ERROR(), (err)); } while (0)
static void unmap(uint64_t base) {
    rt_thread_set_last_error(0xDEAD);
    T_EQ(rt_view_unmap(base), TRUE);
    T_EQ(T_LAST_ERROR(), 0xDEAD);
}
#define UNMAP_FAILS(base) \
    do { rt_thread_set_last_error(0xDEAD); \
         T_EQ(rt_view_unmap(base), FALSE); \
         T_EQ(T_LAST_ERROR(), ERROR_INVALID_ADDRESS); } while (0)
static HANDLE open_file(const char *rel, DWORD access) {
    char win[512];
    snprintf(win, sizeof win, "C:\\Titanfall2\\%s", rel);
    HANDLE h = cfa(win, access, SH, OPEN_EXISTING, 0);
    T_TRUE(h != BAD);
    return h;
}

/* ---- sections ---------------------------------------------------------------------------------- */

TEST(core, file_map_sections)
{
    put_pat("f.bin", 0x12000);
    put("empty.bin", "", 0644);

    T_PLAN(1, "a section on a file: last error 0, and it does not need the file handle") {
        HANDLE f = open_file("f.bin", RD);
        const unsigned files = rt_file_test_open_count();
        HANDLE s = sec(f, PAGE_READONLY, 0, NULL, 0);
        counts_are(1, 0);
        T_TRUE(obj_of(s) != 0);
        T_EQ(rt_handle_close(f), TRUE);
        T_EQ(rt_file_test_open_count(), files - 1);         /* the section has a descriptor of its own */
        uint64_t v = map(s, FILE_MAP_READ, 0, 0);
        T_TRUE(mem_is_pat(v, 0, 0x12000));
        unmap(v);
        T_EQ(rt_handle_close(s), TRUE);
        counts_are(0, 0);
        f = open_file("f.bin", RD);
        HANDLE c = sec(f, PAGE_READONLY | SEC_COMMIT, 0, NULL, 0);          /* SEC_COMMIT is the default */
        T_EQ(rt_handle_close(c), TRUE);
        T_EQ(rt_handle_close(f), TRUE);
    }

    T_PLAN(2, "max_size 0 on an empty file") {
        HANDLE f = open_file("empty.bin", RD | WR);
        SEC_FAILS(ERROR_FILE_INVALID, f, PAGE_READONLY, 0, NULL);
        SEC_FAILS(ERROR_FILE_INVALID, f, PAGE_READWRITE, 0, NULL);
        SEC_FAILS(ERROR_FILE_INVALID, f, PAGE_READWRITE, 0, W("empty_named"));
        rt_thread_set_last_error(0xDEAD);
        T_TRUE(rt_section_open(W("empty_named")) == NULL);   /* the failed create left no name behind */
        T_EQ(T_LAST_ERROR(), ERROR_FILE_NOT_FOUND);
        counts_are(0, 0);
        T_EQ(host_size("empty.bin"), 0);
        rt_handle_close(f);
    }

    T_PLAN(3, "no file and size 0") {
        SEC_FAILS(ERROR_INVALID_PARAMETER, NOFILE, PAGE_READWRITE, 0, NULL);
        SEC_FAILS(ERROR_INVALID_PARAMETER, NOFILE, PAGE_READONLY, 0, W("nofile_zero"));
        counts_are(0, 0);
    }

    T_PLAN(4, "a name created twice is one object: last error 0, then 183") {
        HANDLE a = sec(NOFILE, PAGE_READWRITE, 0x3000, W("twice"), 0);
        HANDLE b = sec(NOFILE, PAGE_READWRITE, 0x3000, W("twice"), ERROR_ALREADY_EXISTS);
        T_TRUE(a != b);
        T_EQ(obj_of(a), obj_of(b));
        counts_are(1, 0);
        /* The other arguments of the second call do not matter: the section keeps its own. */
        HANDLE f = open_file("f.bin", RD);
        HANDLE c = sec(f, PAGE_READONLY, 0x1000, W("Local\\twice"), ERROR_ALREADY_EXISTS);
        T_EQ(obj_of(c), obj_of(a));
        uint64_t v = map(c, FILE_MAP_WRITE, 0, 0);          /* read-write, 0x3000 bytes, zeros: the first call's */
        view_is(v, 0x3000, PAGE_READWRITE);
        T_TRUE(mem_is_zero(v, 0x3000));
        unmap(v);
        rt_handle_close(f);
        /* Names compare exactly: another case is another section. */
        HANDLE d = sec(NOFILE, PAGE_READWRITE, 0x1000, W("TWICE"), 0);
        T_TRUE(obj_of(d) != obj_of(a));
        counts_are(2, 0);
        rt_handle_close(a); rt_handle_close(b); rt_handle_close(c); rt_handle_close(d);
        counts_are(0, 0);
        /* The name died with the object: creating it again makes a new section. */
        HANDLE e = sec(NOFILE, PAGE_READWRITE, 0x1000, W("twice"), 0);
        rt_handle_close(e);
    }

    T_PLAN(5, "a name that is an event's") {
        HANDLE ev = rt_event_create(T_CTX(), TRUE, FALSE, W("an_event"));
        T_REQUIRE(ev != NULL);
        SEC_FAILS(ERROR_INVALID_HANDLE, NOFILE, PAGE_READWRITE, 0x1000, W("an_event"));
        rt_thread_set_last_error(0xDEAD);
        T_TRUE(rt_section_open(W("an_event")) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
        counts_are(0, 0);
        /* And the other way round: a section's name is taken for an event. */
        HANDLE s = sec(NOFILE, PAGE_READWRITE, 0x1000, W("a_section"), 0);
        T_TRUE(rt_event_create(T_CTX(), TRUE, FALSE, W("a_section")) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
        rt_handle_close(s);
        rt_handle_close(ev);
    }

    T_PLAN(6, "open by name") {
        rt_thread_set_last_error(0xDEAD);
        T_TRUE(rt_section_open(W("missing")) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_FILE_NOT_FOUND);
        rt_thread_set_last_error(0xDEAD);
        T_TRUE(rt_section_open(NULL) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
        HANDLE a = sec(NOFILE, PAGE_READWRITE, 0x2000, W("shared_mem"), 0);
        rt_thread_set_last_error(0xDEAD);
        HANDLE o = rt_section_open(W("shared_mem"));
        T_TRUE(o != NULL && o != a);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        T_EQ(obj_of(o), obj_of(a));
        HANDLE g = rt_section_open(W("Local\\shared_mem"));           /* the same name (rt/handle.h) */
        T_EQ(obj_of(g), obj_of(a));
        rt_thread_set_last_error(0xDEAD);
        T_TRUE(rt_section_open(W("Global\\shared_mem")) == NULL);  /* another name */
        T_EQ(T_LAST_ERROR(), ERROR_FILE_NOT_FOUND);
        /* What one handle's view writes, the other's shows. */
        uint64_t va = map(a, FILE_MAP_WRITE, 0, 0), vo = map(o, FILE_MAP_READ, 0, 0);
        M(va)[0x1234] = 0x5A;
        T_EQ(M(vo)[0x1234], 0x5A);
        unmap(va); unmap(vo);
        /* The opened handle keeps the section when the creator's is closed. */
        rt_handle_close(a); rt_handle_close(g);
        counts_are(1, 0);
        uint64_t v2 = map(o, FILE_MAP_READ, 0, 0);
        T_EQ(M(v2)[0x1234], 0x5A);
        unmap(v2);
        rt_handle_close(o);
        counts_are(0, 0);
        T_TRUE(rt_section_open(W("shared_mem")) == NULL);
    }

    T_PLAN(7, "`file` is not a file handle") {
        HANDLE ev = rt_event_create(T_CTX(), TRUE, FALSE, NULL);
        HANDLE f = open_file("f.bin", RD);
        rt_handle_close(f);
        SEC_FAILS(ERROR_INVALID_HANDLE, ev, PAGE_READONLY, 0, NULL);
        SEC_FAILS(ERROR_INVALID_HANDLE, f, PAGE_READONLY, 0, NULL);             /* closed */
        SEC_FAILS(ERROR_INVALID_HANDLE, (HANDLE)(uintptr_t)0x12344, PAGE_READONLY, 0x1000, NULL);
        HANDLE s = sec(NOFILE, PAGE_READWRITE, 0x1000, NULL, 0);
        SEC_FAILS(ERROR_INVALID_HANDLE, s, PAGE_READONLY, 0, NULL);             /* a section is not a file */
        rt_handle_close(s);
        rt_handle_close(ev);
        counts_are(0, 0);
    }

    T_PLAN(8, "the file handle's access against the protection") {
        HANDLE ro = open_file("f.bin", RD), wo = open_file("f.bin", WR), rw = open_file("f.bin", RD | WR);
        SEC_FAILS(ERROR_ACCESS_DENIED, ro, PAGE_READWRITE, 0, NULL);
        SEC_FAILS(ERROR_ACCESS_DENIED, wo, PAGE_READONLY, 0, NULL);
        SEC_FAILS(ERROR_ACCESS_DENIED, wo, PAGE_READWRITE, 0, NULL);
        SEC_FAILS(ERROR_ACCESS_DENIED, wo, PAGE_WRITECOPY, 0, NULL);
        HANDLE a = sec(ro, PAGE_READONLY, 0, NULL, 0), b = sec(ro, PAGE_WRITECOPY, 0, NULL, 0);
        HANDLE c = sec(rw, PAGE_READONLY, 0, NULL, 0), d = sec(rw, PAGE_READWRITE, 0, NULL, 0);
        HANDLE e = sec(rw, PAGE_WRITECOPY, 0, NULL, 0);
        counts_are(5, 0);
        rt_handle_close(a); rt_handle_close(b); rt_handle_close(c); rt_handle_close(d); rt_handle_close(e);
        rt_handle_close(ro); rt_handle_close(wo); rt_handle_close(rw);
        T_EQ(host_size("f.bin"), 0x12000);
        T_TRUE(host_is_pat("f.bin", 0, 0x12000));
    }

    T_PLAN(9, "a max_size smaller than the file") {
        HANDLE f = open_file("f.bin", RD);
        HANDLE s = sec(f, PAGE_READONLY, 0x11000, NULL, 0);
        uint64_t v = map(s, FILE_MAP_READ, 0, 0);
        view_is(v, 0x11000, PAGE_READONLY);
        unmap(v);
        MAP_FAILS(ERROR_ACCESS_DENIED, s, FILE_MAP_READ, 0, 0x11001);
        T_EQ(host_size("f.bin"), 0x12000);
        rt_handle_close(s); rt_handle_close(f);
    }

    T_PLAN(10, "a max_size larger than the file extends it only for PAGE_READWRITE") {
        put_pat("grow.bin", 1000);
        HANDLE f = open_file("grow.bin", RD | WR);
        SEC_FAILS(ERROR_NOT_ENOUGH_MEMORY, f, PAGE_READONLY, 1001, NULL);
        SEC_FAILS(ERROR_NOT_ENOUGH_MEMORY, f, PAGE_WRITECOPY, 0x5000, NULL);
        T_EQ(host_size("grow.bin"), 1000);                  /* untouched */
        counts_are(0, 0);
        HANDLE same = sec(f, PAGE_READONLY, 1000, NULL, 0); /* exactly the file's size is not "larger" */
        rt_handle_close(same);
        HANDLE s = sec(f, PAGE_READWRITE, 0x5000, NULL, 0);
        T_EQ(host_size("grow.bin"), 0x5000);
        T_TRUE(host_is_pat("grow.bin", 0, 1000));
        uint64_t v = map(s, FILE_MAP_READ, 0, 0);
        view_is(v, 0x5000, PAGE_READONLY);
        T_TRUE(mem_is_pat(v, 0, 1000));
        T_TRUE(mem_is_zero(v + 1000, 0x5000 - 1000));       /* the new part reads as zeros */
        unmap(v);
        rt_handle_close(s); rt_handle_close(f);
    }

    T_PLAN(11, "the stops") {
        HANDLE f = open_file("f.bin", RD | WR);
        T_TRAPS(rt_section_create(f, PAGE_EXECUTE_READ, 0, NULL), "rt_section_create: not implemented: protection 0x20");
        T_TRAPS(rt_section_create(f, PAGE_READONLY | SEC_IMAGE, 0, NULL), "rt_section_create: not implemented");
        T_TRAPS(rt_section_create(NOFILE, PAGE_READWRITE | SEC_RESERVE, 0x10000, NULL), "rt_section_create: not implemented");
        T_TRAPS(rt_section_create(f, 0, 0, NULL), "rt_section_create: not implemented");
        T_TRAPS(rt_section_create(f, PAGE_READONLY | PAGE_READWRITE, 0, NULL), "rt_section_create: not implemented");
        T_TRAPS(rt_section_create(NULL, PAGE_READWRITE, 0x1000, NULL), "rt_section_create: not implemented: a NULL file handle");
        HANDLE dir = cfa("C:\\Titanfall2", RD, SH, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS);
        HANDLE nul = cfa("NUL", RD | WR, SH, OPEN_EXISTING, 0);
        T_REQUIRE(dir != BAD && nul != BAD);
        T_TRAPS(rt_section_create(dir, PAGE_READONLY, 0, NULL), "rt_section_create: not implemented: a section of a directory");
        T_TRAPS(rt_section_create(nul, PAGE_READWRITE, 0x1000, NULL), "rt_section_create: not implemented");
        /* A name that exists, and a call that would have extended its file. */
        HANDLE s = sec(NOFILE, PAGE_READWRITE, 0x1000, W("taken"), 0);
        T_TRAPS(rt_section_create(f, PAGE_READWRITE, 0x20000, W("taken")), "rt_section_create: not implemented: the name exists");
        T_EQ(host_size("f.bin"), 0x12000);
        rt_handle_close(s); rt_handle_close(dir); rt_handle_close(nul); rt_handle_close(f);
    }

    T_PLAN(12, "nothing is left: no section, no view, no file object") {
        HANDLE f = open_file("f.bin", RD | WR);
        for (int i = 0; i < 50; i++) {
            HANDLE s = rt_section_create(f, PAGE_READWRITE, 0, i & 1 ? W("loop") : NULL);
            uint64_t v = rt_view_map(s, FILE_MAP_WRITE, 0, 0);
            if (!s || !v || !rt_view_unmap(v) || !rt_handle_close(s)) { rt_test_fail(__FILE__, __LINE__, "round %d", i); break; }
        }
        rt_handle_close(f);
        counts_are(0, 0);
        T_EQ(rt_file_test_open_count(), 3);
    }
}

/* ---- views ------------------------------------------------------------------------------------- */

TEST(core, file_map_views)
{
    const uint64_t hp = rt_vm_host_page();
    put_pat("f.bin", 0x30000);
    put_pat("odd.bin", 5000);           /* ends inside its first host page */
    put_pat("odd2.bin", 20000);         /* ends inside its second host page (16 KB hosts) */

    T_PLAN(13, "a view shows the file's bytes and is MEM_MAPPED to the VM map") {
        HANDLE f = open_file("f.bin", RD);
        HANDLE s = sec(f, PAGE_READONLY, 0, NULL, 0);
        uint64_t v = map(s, FILE_MAP_READ, 0, 0);
        view_is(v, 0x30000, PAGE_READONLY);
        T_TRUE(mem_is_pat(v, 0, 0x30000));
        MEMORY_BASIC_INFORMATION m = Q(v + 0x2FFFF);         /* the last byte is in the same run */
        T_EQ(m.BaseAddress, v + 0x2F000);
        T_EQ(m.AllocationBase, v);
        T_EQ(m.Type, MEM_MAPPED);
        counts_are(1, 1);
        unmap(v);
        T_EQ(Q(v).State, MEM_FREE);
        rt_handle_close(s); rt_handle_close(f);
    }

    T_PLAN(14, "a file that does not end on a page: zeros after the end") {
        HANDLE f = open_file("odd.bin", RD), f2 = open_file("odd2.bin", RD);
        HANDLE s = sec(f, PAGE_READONLY, 0, NULL, 0), s2 = sec(f2, PAGE_READONLY, 0, NULL, 0);
        uint64_t v = map(s, FILE_MAP_READ, 0, 0), v2 = map(s2, FILE_MAP_READ, 0, 0);
        view_is(v, 0x2000, PAGE_READONLY);                  /* whole 4 KB pages */
        T_TRUE(mem_is_pat(v, 0, 5000));
        T_TRUE(mem_is_zero(v + 5000, 0x2000 - 5000));
        view_is(v2, 0x5000, PAGE_READONLY);
        T_TRUE(mem_is_pat(v2, 0, 20000));                   /* across the host page boundary */
        T_TRUE(mem_is_zero(v2 + 20000, 0x5000 - 20000));
        unmap(v); unmap(v2);
        rt_handle_close(s); rt_handle_close(s2); rt_handle_close(f); rt_handle_close(f2);
        T_EQ(host_size("odd.bin"), 5000);                   /* mapping never changes a size */
        T_EQ(host_size("odd2.bin"), 20000);
    }

    T_PLAN(15, "a write through a read-write view reaches the file") {
        put_pat("w.bin", 0x12000);
        HANDLE f = open_file("w.bin", RD | WR);
        HANDLE s = sec(f, PAGE_READWRITE, 0, NULL, 0);
        uint64_t v = map(s, FILE_MAP_WRITE, 0, 0);
        view_is(v, 0x12000, PAGE_READWRITE);
        M(v)[0] = 0xC1; M(v)[0x4000] = 0xC2; M(v)[0x11FFF] = 0xC3;
        T_EQ(host_byte("w.bin", 0), 0xC1);                  /* seen by a read of the file at once */
        T_EQ(host_byte("w.bin", 0x4000), 0xC2);
        T_EQ(host_byte("w.bin", 0x11FFF), 0xC3);
        unmap(v);
        T_EQ(host_byte("w.bin", 0x4000), 0xC2);             /* and it stays after the view is gone */
        T_TRUE(host_is_pat("w.bin", 1, 0x4000));
        T_EQ(host_size("w.bin"), 0x12000);
        uint64_t all = map(s, FILE_MAP_ALL_ACCESS, 0, 0), both = map(s, FILE_MAP_WRITE | FILE_MAP_READ, 0, 0);
        view_is(all, 0x12000, PAGE_READWRITE);              /* the three forms are one */
        view_is(both, 0x12000, PAGE_READWRITE);
        T_EQ(M(all)[0], 0xC1);
        unmap(all); unmap(both);
        rt_handle_close(s); rt_handle_close(f);
    }

    T_PLAN(16, "two views share pages") {
        put_pat("sh.bin", 0x12000);
        HANDLE f = open_file("sh.bin", RD | WR);
        HANDLE s = sec(f, PAGE_READWRITE, 0, NULL, 0), s2 = sec(f, PAGE_READONLY, 0, NULL, 0);
        uint64_t a = map(s, FILE_MAP_WRITE, 0, 0), b = map(s, FILE_MAP_READ, 0, 0);
        uint64_t c = map(s2, FILE_MAP_READ, 0, 0);          /* another section of the same file */
        uint64_t d = map(s, FILE_MAP_WRITE, 0x10000, 0);    /* another part of the file, overlapping a's */
        T_TRUE(a != b && b != c && a != c && d != a);
        M(a)[0x123] = 0x11; M(a)[0x10010] = 0x22;
        T_EQ(M(b)[0x123], 0x11);
        T_EQ(M(c)[0x123], 0x11);
        T_EQ(M(d)[0x10], 0x22);
        M(d)[0x20] = 0x33;
        T_EQ(M(a)[0x10020], 0x33);
        T_EQ(M(c)[0x10020], 0x33);
        counts_are(2, 4);
        unmap(a); unmap(b); unmap(c); unmap(d);
        rt_handle_close(s); rt_handle_close(s2); rt_handle_close(f);
    }

    T_PLAN(17, "the offset is a multiple of 64 KB") {
        HANDLE f = open_file("f.bin", RD);
        HANDLE s = sec(f, PAGE_READONLY, 0, NULL, 0);
        MAP_FAILS(ERROR_MAPPED_ALIGNMENT, s, FILE_MAP_READ, 0x1000, 0);
        MAP_FAILS(ERROR_MAPPED_ALIGNMENT, s, FILE_MAP_READ, hp, 0);             /* what the host would take */
        MAP_FAILS(ERROR_MAPPED_ALIGNMENT, s, FILE_MAP_READ, 0x8000, 0x1000);
        MAP_FAILS(ERROR_MAPPED_ALIGNMENT, s, FILE_MAP_READ, 0x10001, 0);
        MAP_FAILS(ERROR_MAPPED_ALIGNMENT, (HANDLE)(uintptr_t)0x12344, FILE_MAP_READ, 0x1000, 0);    /* checked before the handle */
        uint64_t v = map(s, FILE_MAP_READ, 0x10000, 0);     /* the rest of the section */
        view_is(v, 0x20000, PAGE_READONLY);
        T_TRUE(mem_is_pat(v, 0x10000, 0x20000));
        uint64_t w = map(s, FILE_MAP_READ, 0x20000, 0x1800);
        view_is(w, 0x2000, PAGE_READONLY);
        T_TRUE(mem_is_pat(w, 0x20000, 0x2000));             /* the whole last page shows the file */
        unmap(v); unmap(w);
        counts_are(1, 0);
        rt_handle_close(s); rt_handle_close(f);
    }

    T_PLAN(18, "sizes: 0 is the rest, and a view past the end fails") {
        HANDLE f = open_file("f.bin", RD), fo = open_file("odd.bin", RD);
        HANDLE s = sec(f, PAGE_READONLY, 0, NULL, 0), so = sec(fo, PAGE_READONLY, 0, NULL, 0);
        uint64_t one = map(s, FILE_MAP_READ, 0, 1);
        view_is(one, 0x1000, PAGE_READONLY);
        uint64_t all = map(s, FILE_MAP_READ, 0, 0x30000);
        view_is(all, 0x30000, PAGE_READONLY);
        uint64_t last = map(s, FILE_MAP_READ, 0x20000, 0x10000);
        view_is(last, 0x10000, PAGE_READONLY);
        unmap(one); unmap(all); unmap(last);
        MAP_FAILS(ERROR_ACCESS_DENIED, s, FILE_MAP_READ, 0, 0x30001);
        MAP_FAILS(ERROR_ACCESS_DENIED, s, FILE_MAP_READ, 0x20000, 0x10001);
        MAP_FAILS(ERROR_ACCESS_DENIED, s, FILE_MAP_READ, 0, (SIZE_T)-1);
        MAP_FAILS(ERROR_ACCESS_DENIED, s, FILE_MAP_READ, 0x30000, 0);           /* the offset is the end */
        MAP_FAILS(ERROR_ACCESS_DENIED, s, FILE_MAP_READ, 0x40000, 0x1000);
        MAP_FAILS(ERROR_ACCESS_DENIED, s, FILE_MAP_READ, (ULONGLONG)1 << 63, 0);
        /* A section that does not end on a page: exactly its size works, beyond its last page
         * fails, and in between is the stop. */
        uint64_t o = map(so, FILE_MAP_READ, 0, 5000);
        view_is(o, 0x2000, PAGE_READONLY);
        unmap(o);
        MAP_FAILS(ERROR_ACCESS_DENIED, so, FILE_MAP_READ, 0, 0x2001);
        T_TRAPS(rt_view_map(so, FILE_MAP_READ, 0, 5001), "rt_view_map: not implemented: a view of 0x1389 bytes");
        T_TRAPS(rt_view_map(so, FILE_MAP_READ, 0, 0x2000), "rt_view_map: not implemented");
        counts_are(2, 0);
        rt_handle_close(s); rt_handle_close(so); rt_handle_close(f); rt_handle_close(fo);
    }

    T_PLAN(19, "the access against the section's protection") {
        put_pat("acc.bin", 0x10000);
        HANDLE f = open_file("acc.bin", RD | WR);
        HANDLE ro = sec(f, PAGE_READONLY, 0, NULL, 0), rw = sec(f, PAGE_READWRITE, 0, NULL, 0);
        HANDLE wc = sec(f, PAGE_WRITECOPY, 0, NULL, 0);
        MAP_FAILS(ERROR_ACCESS_DENIED, ro, FILE_MAP_WRITE, 0, 0);
        MAP_FAILS(ERROR_ACCESS_DENIED, ro, FILE_MAP_ALL_ACCESS, 0, 0);
        MAP_FAILS(ERROR_ACCESS_DENIED, wc, FILE_MAP_WRITE | FILE_MAP_READ, 0, 0);
        HANDLE all[3] = { ro, rw, wc };
        for (int i = 0; i < 3; i++) {
            uint64_t r = map(all[i], FILE_MAP_READ, 0, 0), c = map(all[i], FILE_MAP_COPY, 0, 0);
            uint64_t c2 = map(all[i], FILE_MAP_COPY | FILE_MAP_READ, 0, 0);
            view_is(r, 0x10000, PAGE_READONLY);
            view_is(c, 0x10000, PAGE_WRITECOPY);
            view_is(c2, 0x10000, PAGE_WRITECOPY);
            unmap(r); unmap(c); unmap(c2);
        }
        MAP_FAILS(ERROR_INVALID_HANDLE, f, FILE_MAP_READ, 0, 0);                /* a file is not a section */
        MAP_FAILS(ERROR_INVALID_HANDLE, NULL, FILE_MAP_READ, 0, 0);
        MAP_FAILS(ERROR_INVALID_HANDLE, INVALID_HANDLE_VALUE, FILE_MAP_READ, 0, 0);
        T_TRAPS(rt_view_map(rw, 0, 0, 0), "rt_view_map: not implemented: access 0x0");
        T_TRAPS(rt_view_map(rw, FILE_MAP_READ | FILE_MAP_EXECUTE, 0, 0), "rt_view_map: not implemented: access 0x24");
        T_TRAPS(rt_view_map(rw, FILE_MAP_WRITE | FILE_MAP_COPY, 0, 0), "rt_view_map: not implemented: access 0x3");
        T_TRAPS(rt_view_map(rw, 0x100 | FILE_MAP_READ, 0, 0), "rt_view_map: not implemented");
        T_TRAPS(rt_view_map(NULL, 0x40, 0x1000, 0), "rt_view_map: not implemented");    /* the stop comes first */
        rt_handle_close(ro); rt_handle_close(rw); rt_handle_close(wc); rt_handle_close(f);
        T_TRUE(host_is_pat("acc.bin", 0, 0x10000));
    }

    T_PLAN(20, "a read-only view that is written faults") {
        put_pat("prot.bin", 0x10000);
        HANDLE f = open_file("prot.bin", RD | WR);
        HANDLE s = sec(f, PAGE_READWRITE, 0, NULL, 0);
        uint64_t r = map(s, FILE_MAP_READ, 0, 0), w = map(s, FILE_MAP_WRITE, 0, 0);
        T_TRUE(touch(r, false));
        T_TRUE(!touch(r, true));
        T_TRUE(!touch(r + 0xFFFF, true));
        T_TRUE(touch(w, true));
        T_TRUE(touch(w + 0xFFFF, true));
        /* Also when the section's file was opened for reading only. */
        HANDLE f2 = open_file("prot.bin", RD);
        HANDLE s2 = sec(f2, PAGE_READONLY, 0, NULL, 0);
        uint64_t r2 = map(s2, FILE_MAP_READ, 0, 0);
        T_TRUE(touch(r2, false) && !touch(r2, true));
        unmap(r); unmap(w); unmap(r2);
        T_TRUE(!touch(w, false));                           /* an unmapped view is gone */
        rt_handle_close(s); rt_handle_close(s2); rt_handle_close(f); rt_handle_close(f2);
        T_TRUE(host_is_pat("prot.bin", 0, 0x10000));
    }

    T_PLAN(21, "a copy-on-write view: writes stay private") {
        put_pat("cow.bin", 0x10000);
        HANDLE f = open_file("cow.bin", RD | WR), fr = open_file("cow.bin", RD);
        HANDLE ro = sec(fr, PAGE_READONLY, 0, NULL, 0), rw = sec(f, PAGE_READWRITE, 0, NULL, 0);
        uint64_t c = map(ro, FILE_MAP_COPY, 0, 0);          /* allowed on a read-only section of a read-only handle */
        uint64_t shared = map(rw, FILE_MAP_WRITE, 0, 0), c2 = map(rw, FILE_MAP_COPY, 0, 0);
        view_is(c, 0x10000, PAGE_WRITECOPY);
        T_TRUE(mem_is_pat(c, 0, 0x10000));
        T_TRUE(touch(c, true));
        M(c)[5] = 0xEE; M(c)[0xFFFF] = 0xEF;
        T_EQ(M(c)[5], 0xEE);
        T_EQ(M(shared)[5], pat(5));                         /* not in the shared view */
        T_EQ(M(c2)[5], pat(5));                             /* nor in another private one */
        T_EQ(host_byte("cow.bin", 5), pat(5));              /* nor in the file */
        T_EQ(M(shared)[0xFFFF], pat(0xFFFF));
        /* A write to the file does not reach a page the private view has written. */
        M(shared)[6] = 0x44;
        T_EQ(M(c)[5], 0xEE);
        /* THIS HOST, not Windows (rt/file.h, rt_view_map): a private view is a snapshot, so the
         * write does not show in c2 either, which has written nothing.  If this check fails
         * the host has changed and the sentence in the header is to be read again. */
        M(shared)[0x8000] = 0x45;
        T_EQ(M(c2)[6], pat(6));
        T_EQ(M(c2)[0x8000], pat(0x8000));
        T_TRUE(T_LOGGED(RT_CH_FILE, "a copy-on-write view (FILE_MAP_COPY)"));
        T_EQ(Q(c).Protect, PAGE_WRITECOPY);                 /* the value given is the value reported (rt/vm.h) */
        unmap(c); unmap(c2);
        T_EQ(host_byte("cow.bin", 5), pat(5));              /* lost with the view, never written back */
        T_EQ(host_byte("cow.bin", 0xFFFF), pat(0xFFFF));
        T_EQ(host_byte("cow.bin", 6), 0x44);
        T_EQ(host_byte("cow.bin", 0x8000), 0x45);
        unmap(shared);
        /* A section with no file takes a private view too. */
        HANDLE m = sec(NOFILE, PAGE_READWRITE, 0x1000, NULL, 0);
        uint64_t ms = map(m, FILE_MAP_WRITE, 0, 0), mc = map(m, FILE_MAP_COPY, 0, 0);
        M(mc)[0] = 9;
        T_EQ(M(ms)[0], 0);
        unmap(ms); unmap(mc);
        rt_handle_close(m); rt_handle_close(ro); rt_handle_close(rw); rt_handle_close(f); rt_handle_close(fr);
    }

    T_PLAN(22, "the VM map's calls on a view") {
        put_pat("vm.bin", 0x10000);
        HANDLE f = open_file("vm.bin", RD | WR);
        HANDLE s = sec(f, PAGE_READWRITE, 0, NULL, 0);
        uint64_t v = map(s, FILE_MAP_WRITE, 0, 0);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_vm_free(v, 0, MEM_RELEASE), FALSE);         /* a guest VirtualFree does not remove a view */
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
        T_EQ(rt_vm_free(v, 0x1000, MEM_DECOMMIT), FALSE);
        T_EQ(rt_vm_release(v, RT_VM_PRIVATE), FALSE);       /* nor does another owner's release */
        view_is(v, 0x10000, PAGE_READWRITE);
        T_TRUE(mem_is_pat(v, 0, 0x10000));
        DWORD old = 0;
        T_EQ(rt_vm_protect(v, 0x10000, PAGE_READONLY, &old), TRUE);
        T_EQ(old, PAGE_READWRITE);
        T_TRUE(!touch(v, true));
        T_EQ(Q(v).Type, MEM_MAPPED);
        T_EQ(rt_vm_protect(v, 0x10000, PAGE_READWRITE, &old), TRUE);
        T_EQ(old, PAGE_READONLY);
        M(v)[1] = 0x99;
        T_EQ(host_byte("vm.bin", 1), 0x99);                 /* still the file */
        T_TRAPS(rt_vm_alloc(v + 0x1000, 0x1000, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE),
                "MEM_COMMIT with type private inside a region of type mapped");
        unmap(v);
        rt_handle_close(s); rt_handle_close(f);
    }

    T_PLAN(23, "mapping never writes to the file (a view smaller than a host page)") {
        /* The view is one 4 KB page; its host page holds three more pages of the file, which
         * the map does not count as committed.  Nothing may zero them: not the map of the
         * view, not a second view over the same host page, not a protection change, not the
         * unmap. */
        put_pat("z.bin", 0x10000);
        HANDLE f = open_file("z.bin", RD | WR);
        HANDLE s = sec(f, PAGE_READWRITE, 0, NULL, 0);
        uint64_t v = map(s, FILE_MAP_WRITE, 0, 0x1000);
        view_is(v, 0x1000, PAGE_READWRITE);
        T_TRUE(host_is_pat("z.bin", 0, 0x10000));
        uint64_t v2 = map(s, FILE_MAP_WRITE, 0, 0x2000);
        T_TRUE(host_is_pat("z.bin", 0, 0x10000));
        DWORD old = 0;
        T_EQ(rt_vm_protect(v, 0x1000, PAGE_READONLY, &old), TRUE);
        T_EQ(rt_vm_protect(v, 0x1000, PAGE_READWRITE, &old), TRUE);
        T_EQ(rt_vm_protect(v2 + 0x1000, 0x1000, PAGE_READONLY, &old), TRUE);
        T_TRUE(host_is_pat("z.bin", 0, 0x10000));
        T_TRUE(mem_is_pat(v, 0, 0x1000) && mem_is_pat(v2, 0, 0x2000));
        MEMORY_BASIC_INFORMATION m = Q(v + 0x1000);          /* after the view: nothing of the guest's */
        T_EQ(m.State, MEM_FREE);
        unmap(v); unmap(v2);
        T_TRUE(host_is_pat("z.bin", 0, 0x10000));
        /* The same range again, as a private region: fresh zeros, and the file keeps its bytes. */
        uint64_t p = rt_vm_alloc(v, 0x4000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
        T_EQ(p, v);
        T_TRUE(mem_is_zero(p, 0x4000));
        M(p)[0] = 1; M(p)[0x3FFF] = 1;
        T_EQ(rt_vm_free(p, 0, MEM_RELEASE), TRUE);
        T_TRUE(host_is_pat("z.bin", 0, 0x10000));
        rt_handle_close(s); rt_handle_close(f);
    }

    T_PLAN(24, "unmap: once, with the address the map returned or one inside the view") {
        put_pat("u.bin", 0x20000);
        HANDLE f = open_file("u.bin", RD | WR);
        HANDLE s = sec(f, PAGE_READWRITE, 0, NULL, 0);
        uint64_t v = map(s, FILE_MAP_WRITE, 0, 0);
        UNMAP_FAILS(v + 0x20000);                           /* just after it */
        UNMAP_FAILS(0);
        UNMAP_FAILS(0x10000);
        uint64_t p = rt_vm_alloc(0, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
        T_REQUIRE(p != 0);
        UNMAP_FAILS(p);                                     /* a VirtualAlloc region is not a view */
        T_EQ(Q(p).Type, MEM_PRIVATE);
        T_EQ(rt_vm_free(p, 0, MEM_RELEASE), TRUE);
        view_is(v, 0x20000, PAGE_READWRITE);                /* none of that removed it */
        counts_are(1, 1);
        M(v)[0x10000] = 0x7E;
        unmap(v);
        UNMAP_FAILS(v);                                     /* twice */
        T_EQ(Q(v).State, MEM_FREE);
        T_EQ(host_byte("u.bin", 0x10000), 0x7E);
        counts_are(1, 0);
        /* The address is free for the next view, which unmaps like any other. */
        uint64_t again = map(s, FILE_MAP_READ, 0, 0);
        T_EQ(M(again)[0x10000], 0x7E);
        unmap(again);
        /* An address inside a view unmaps the whole view, as its base does. */
        static const uint64_t into[4] = { 100, 0x1000, 0x1064, 0x1FFFF };
        for (unsigned i = 0; i < 4; i++) {
            uint64_t w = map(s, FILE_MAP_WRITE, 0, 0);
            counts_are(1, 1);
            unmap(w + into[i]);
            T_EQ(Q(w).State, MEM_FREE);
            T_EQ(Q(w + 0x10000).State, MEM_FREE);
            counts_are(1, 0);
            UNMAP_FAILS(w + into[i]);
        }
        T_TRUE(T_LOGGED(RT_CH_FILE, "an address inside the view at"));
        rt_handle_close(s); rt_handle_close(f);
        counts_are(0, 0);
    }

    T_PLAN(25, "a view outlives the file handle and the section handle") {
        put_pat("live.bin", 0x10000);
        const unsigned files = rt_file_test_open_count();
        HANDLE f = open_file("live.bin", RD | WR);
        HANDLE s = sec(f, PAGE_READWRITE, 0, W("live"), 0);
        uint64_t v = map(s, FILE_MAP_WRITE, 0, 0);
        T_EQ(rt_handle_close(f), TRUE);
        T_EQ(rt_file_test_open_count(), files);             /* the file object is gone */
        T_EQ(rt_handle_close(s), TRUE);
        counts_are(1, 1);                                   /* the view holds the section */
        T_TRUE(mem_is_pat(v, 0, 0x10000));
        M(v)[0x2345] = 0xAB;
        T_EQ(host_byte("live.bin", 0x2345), 0xAB);
        view_is(v, 0x10000, PAGE_READWRITE);
        /* The file can be deleted on the host under it, too. */
        T_EQ(unlink(I("live.bin")), 0);
        M(v)[0x2346] = 0xAC;
        T_EQ(M(v)[0x2346], 0xAC);
        unmap(v);
        counts_are(0, 0);                                   /* the last reference was the view's */
        rt_thread_set_last_error(0xDEAD);
        T_TRUE(rt_section_open(W("live")) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_FILE_NOT_FOUND);
    }

    T_PLAN(26, "a section with no file is zero-filled and shared") {
        HANDLE s = sec(NOFILE, PAGE_READWRITE, 0x24000, NULL, 0);
        uint64_t a = map(s, FILE_MAP_WRITE, 0, 0), b = map(s, FILE_MAP_WRITE, 0, 0);
        uint64_t r = map(s, FILE_MAP_READ, 0x20000, 0);
        view_is(a, 0x24000, PAGE_READWRITE);
        view_is(r, 0x4000, PAGE_READONLY);
        T_TRUE(a != b);
        T_TRUE(mem_is_zero(a, 0x24000));
        M(a)[0] = 1; M(a)[0x20000] = 2; M(b)[0x23FFF] = 3;
        T_EQ(M(b)[0], 1);
        T_EQ(M(r)[0], 2);
        T_EQ(M(a)[0x23FFF], 3);
        T_EQ(M(r)[0x3FFF], 3);
        T_TRUE(!touch(r, true));
        /* The content lives while a view or a handle does. */
        unmap(a); unmap(b);
        uint64_t c = map(s, FILE_MAP_READ, 0, 0);
        T_EQ(M(c)[0], 1);
        T_EQ(M(c)[0x23FFF], 3);
        unmap(c); unmap(r);
        rt_handle_close(s);
        /* Its size is whole 4 KB pages; the other two protections are allowed. */
        HANDLE small = sec(NOFILE, PAGE_READWRITE, 100, NULL, 0);
        uint64_t sv = map(small, FILE_MAP_WRITE, 0, 0), sv2 = map(small, FILE_MAP_READ, 0, 0x1000);
        view_is(sv, 0x1000, PAGE_READWRITE);
        T_TRUE(mem_is_zero(sv, 0x1000));
        M(sv)[0xFFF] = 7;
        T_EQ(M(sv2)[0xFFF], 7);
        MAP_FAILS(ERROR_ACCESS_DENIED, small, FILE_MAP_READ, 0, 0x1001);
        unmap(sv); unmap(sv2);
        HANDLE ro = sec(NOFILE, PAGE_READONLY, 0x1000, NULL, 0), wc = sec(NOFILE, PAGE_WRITECOPY, 0x1000, NULL, 0);
        MAP_FAILS(ERROR_ACCESS_DENIED, ro, FILE_MAP_WRITE, 0, 0);
        uint64_t rv = map(ro, FILE_MAP_READ, 0, 0);
        T_TRUE(mem_is_zero(rv, 0x1000));
        unmap(rv);
        rt_handle_close(small); rt_handle_close(ro); rt_handle_close(wc);
        counts_are(0, 0);
    }

    T_PLAN(27, "a named section with no file: the memory dies with the object") {
        HANDLE s = sec(NOFILE, PAGE_READWRITE, 0x10000, W("mem"), 0);
        uint64_t v = map(s, FILE_MAP_WRITE, 0, 0);
        M(v)[0] = 0x42;
        unmap(v);
        HANDLE o = rt_section_open(W("mem"));
        T_REQUIRE(o != NULL);
        rt_handle_close(s);
        v = map(o, FILE_MAP_READ, 0, 0);
        T_EQ(M(v)[0], 0x42);
        unmap(v);
        rt_handle_close(o);
        counts_are(0, 0);
        s = sec(NOFILE, PAGE_READWRITE, 0x10000, W("mem"), 0);     /* a new one: 0, not 183, and zeros */
        v = map(s, FILE_MAP_READ, 0, 0);
        T_EQ(M(v)[0], 0);
        unmap(v);
        rt_handle_close(s);
    }

    T_PLAN(28, "many views at once, and nothing left") {
        HANDLE f = open_file("f.bin", RD);
        HANDLE s = sec(f, PAGE_READONLY, 0, NULL, 0);
        enum { N = 64 };
        uint64_t v[N];
        unsigned bad = 0;
        for (int i = 0; i < N; i++) {
            v[i] = rt_view_map(s, FILE_MAP_READ, (ULONGLONG)(i % 3) * 0x10000, 0x1000 + (SIZE_T)i * 0x100);
            if (!v[i] || M(v[i])[i] != pat((uint64_t)(i % 3) * 0x10000 + i)) bad++;
            for (int j = 0; j < i; j++) if (v[j] == v[i]) bad++;
        }
        T_EQ(bad, 0);
        counts_are(1, N);
        for (int i = 0; i < N; i += 2) if (!rt_view_unmap(v[i])) bad++;        /* in another order than they were made */
        for (int i = N - 1; i > 0; i -= 2) if (!rt_view_unmap(v[i])) bad++;
        T_EQ(bad, 0);
        rt_handle_close(s); rt_handle_close(f);
        counts_are(0, 0);
        T_EQ(rt_file_test_open_count(), 3);
    }
}

/* ---- threads ----------------------------------------------------------------------------------- */

enum { THREADS = 8, ROUNDS = 40, CHURN = 150 };

typedef struct {
    unsigned     index;
    const WCHAR *name;
    HANDLE       section;       /* out (race); in (churn) */
    uint64_t     view;          /* out */
} Worker;

static atomic_uint go, told_new, told_exists, told_other, failed;

/* Wait for the start, but never for long: a test must not hang. */
static void wait_go(void) {
    for (int i = 0; i < 20000 && !atomic_load(&go); i++) usleep(100);
}

static void *racer(void *arg) {
    Worker *w = arg;
    wait_go();
    rt_thread_set_last_error(0xDEAD);
    w->section = rt_section_create(INVALID_HANDLE_VALUE, PAGE_READWRITE, 0x10000, w->name);
    const DWORD le = rt_thread_get_last_error();
    if (!w->section) { atomic_fetch_add(&failed, 1); return NULL; }
    atomic_fetch_add(le == 0 ? &told_new : le == ERROR_ALREADY_EXISTS ? &told_exists : &told_other, 1);
    /* At once, as a thread that was told "exists" would: the section must be whole. */
    w->view = rt_view_map(w->section, FILE_MAP_WRITE, 0, 0);
    MEMORY_BASIC_INFORMATION m;
    if (!w->view || rt_vm_query(w->view, &m) != sizeof m || m.RegionSize != 0x10000 || m.Type != MEM_MAPPED) {
        atomic_fetch_add(&failed, 1);
        return NULL;
    }
    M(w->view)[w->index] = (uint8_t)(w->index + 1);
    return NULL;
}

static void *churner(void *arg) {
    Worker *w = arg;
    wait_go();
    for (unsigned i = 0; i < CHURN; i++) {
        /* A view of the shared section: this thread's byte, which nobody else writes. */
        uint64_t v = rt_view_map(w->section, FILE_MAP_WRITE, 0, 0);
        if (!v) { atomic_fetch_add(&failed, 1); continue; }
        M(v)[w->index] = (uint8_t)i;
        if (M(v)[w->index] != (uint8_t)i) atomic_fetch_add(&failed, 1);
        /* And a section of its own, made, mapped, checked and dropped. */
        HANDLE own = rt_section_create(INVALID_HANDLE_VALUE, PAGE_READWRITE, 0x2000, NULL);
        uint64_t o = own ? rt_view_map(own, FILE_MAP_WRITE, 0, 0) : 0;
        if (!own || !o || M(o)[0] != 0) atomic_fetch_add(&failed, 1);
        if (o) { M(o)[0] = 1; if (!rt_view_unmap(o)) atomic_fetch_add(&failed, 1); }
        if (own && !rt_handle_close(own)) atomic_fetch_add(&failed, 1);
        if (!rt_view_unmap(v)) atomic_fetch_add(&failed, 1);
        /* (Not a second unmap: by now `v` may be another thread's new view.) */
    }
    return NULL;
}

TEST(core, file_map_threads)
{
    T_PLAN(29, "eight threads create one name: one object, one is told 0") {
        unsigned bad_rounds = 0;
        for (unsigned round = 0; round < ROUNDS; round++) {
            /* Every other round the creator is held between making the named object and
             * filling it in (the naive-body case: without the lock of rt/handle.h's rule the
             * seven others would map a section that has no size and no file yet). */
            rt_file_map_test_create_pause(round & 1 ? 3000 : 0);
            WCHAR name[32];
            char text[32];
            snprintf(text, sizeof text, "race_%u", round);
            for (size_t i = 0; i <= strlen(text); i++) name[i] = (unsigned char)text[i];
            Worker w[THREADS];
            pthread_t th[THREADS];
            atomic_store(&go, 0); atomic_store(&told_new, 0); atomic_store(&told_exists, 0);
            atomic_store(&told_other, 0); atomic_store(&failed, 0);
            for (unsigned t = 0; t < THREADS; t++) {
                w[t] = (Worker){ .index = t, .name = name };
                if (pthread_create(&th[t], NULL, racer, &w[t])) { rt_test_fail(__FILE__, __LINE__, "pthread_create"); return; }
            }
            atomic_store(&go, 1);
            for (unsigned t = 0; t < THREADS; t++) pthread_join(th[t], NULL);

            bool ok = atomic_load(&failed) == 0 && atomic_load(&told_new) == 1 &&
                      atomic_load(&told_exists) == THREADS - 1 && atomic_load(&told_other) == 0;
            unsigned sections = 0, views = 0;
            rt_file_map_test_counts(&sections, &views);
            if (sections != 1 || views != THREADS) ok = false;
            for (unsigned t = 0; ok && t < THREADS; t++) {
                if (obj_of(w[t].section) != obj_of(w[0].section)) ok = false;
                /* One memory: every thread's byte shows in every thread's view. */
                for (unsigned u = 0; u < THREADS; u++) if (M(w[t].view)[u] != u + 1) ok = false;
            }
            for (unsigned t = 0; t < THREADS; t++) {
                if (w[t].view && !rt_view_unmap(w[t].view)) ok = false;
                if (w[t].section && !rt_handle_close(w[t].section)) ok = false;
            }
            rt_file_map_test_counts(&sections, &views);
            if (sections != 0 || views != 0) ok = false;
            if (!ok) bad_rounds++;
        }
        rt_file_map_test_create_pause(0);
        T_EQ(bad_rounds, 0);
    }

    T_PLAN(30, "eight threads map and unmap views of one section") {
        HANDLE s = sec(INVALID_HANDLE_VALUE, PAGE_READWRITE, 0x10000, NULL, 0);
        uint64_t mine = map(s, FILE_MAP_READ, 0, 0);
        Worker w[THREADS];
        pthread_t th[THREADS];
        atomic_store(&go, 0); atomic_store(&failed, 0);
        for (unsigned t = 0; t < THREADS; t++) {
            w[t] = (Worker){ .index = t, .section = s };
            if (pthread_create(&th[t], NULL, churner, &w[t])) { rt_test_fail(__FILE__, __LINE__, "pthread_create"); return; }
        }
        atomic_store(&go, 1);
        for (unsigned t = 0; t < THREADS; t++) pthread_join(th[t], NULL);
        T_EQ(atomic_load(&failed), 0);
        for (unsigned t = 0; t < THREADS; t++) T_EQ(M(mine)[t], (uint8_t)(CHURN - 1));
        counts_are(1, 1);
        view_is(mine, 0x10000, PAGE_READONLY);              /* the main thread's view was never touched */
        unmap(mine);
        rt_handle_close(s);
        counts_are(0, 0);
    }
}
