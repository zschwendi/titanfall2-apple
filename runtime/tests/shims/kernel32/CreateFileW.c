/* CreateFileW through the guest ABI: what the shim returns and what the guest sees.  The
 * rules are the core's (rt_file_create, rt/file.h).  Files are made under C:\Titanfall2, the
 * install root, named as runtime/tests/core/file_open.c names them: a guest path, converted to
 * wide characters by W(). */
#include "rt/test.h"
#include "rt/handle.h"
#include "rt/log.h"

#define RD   GENERIC_READ
#define WR   GENERIC_WRITE
#define SH   (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE)
#define BAD  INVALID_HANDLE_VALUE

static const WCHAR *W(const char *a) {
    static WCHAR buf[4][512];
    static unsigned k;
    WCHAR *b = buf[k++ & 3];
    size_t i = 0;
    for (; a[i]; i++) b[i] = (unsigned char)a[i];
    b[i] = 0;
    return b;
}

/* CreateFileW with last error set to a marker first, so that "written on success" shows. */
static HANDLE cf(const char *name, DWORD access, DWORD share, DWORD disp, DWORD fa) {
    CALL(kernel32, SetLastError, 0xDEAD);
    return CALL(kernel32, CreateFileW, W(name), access, share, NULL, disp, fa, NULL);
}

TEST(kernel32, CreateFileW)
{
    HANDLE h, h2;

    /* The stops of the core (RT-TRAPS): each is a fatal report, before anything is created. */
    T_TRAPS(CALL(kernel32, CreateFileW, W("C:\\Titanfall2\\t.bin"), RD, SH, NULL, OPEN_EXISTING, 0, (HANDLE)(uintptr_t)4), "rt_file_create: not implemented: a template file");
    T_TRAPS(CALL(kernel32, CreateFileW, W("C:\\Titanfall2\\t.bin"), RD, 8, NULL, OPEN_EXISTING, 0, NULL), "rt_file_create: not implemented: share mode");
    T_TRAPS(CALL(kernel32, CreateFileW, W("C:\\Titanfall2\\t.bin"), RD | 0x200, SH, NULL, OPEN_EXISTING, 0, NULL), "rt_file_create: not implemented: access 0x");
    T_TRAPS(CALL(kernel32, CreateFileW, W("C:\\Titanfall2\\t.bin"), FILE_APPEND_DATA | RD, SH, NULL, OPEN_EXISTING, 0, NULL), "rt_file_create: not implemented: an append-only handle");
    T_TRAPS(CALL(kernel32, CreateFileW, W("C:\\Titanfall2\\t.bin"), RD, SH, NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL), "rt_file_create: not implemented: flags and attributes");
    T_TRAPS(CALL(kernel32, CreateFileW, W("NUL"), WR, SH, NULL, CREATE_NEW, 0, NULL), "rt_file_create: not implemented: CREATE_NEW on NUL");

    T_CASE("CREATE_NEW makes a file and fails the second time with ERROR_FILE_EXISTS");
    h = cf("C:\\Titanfall2\\new.bin", WR, SH, CREATE_NEW, 0);
    T_REQUIRE(h != BAD);
    T_EQ(T_LAST_ERROR(), 0);
    T_EQ(rt_handle_close(h), TRUE);
    T_EQ(cf("C:\\Titanfall2\\new.bin", WR, SH, CREATE_NEW, 0), BAD);
    T_EQ(T_LAST_ERROR(), ERROR_FILE_EXISTS);

    T_CASE("OPEN_EXISTING on a missing file fails with ERROR_FILE_NOT_FOUND and returns INVALID_HANDLE_VALUE");
    T_EQ(cf("C:\\Titanfall2\\none.bin", RD, SH, OPEN_EXISTING, 0), BAD);
    T_EQ(T_LAST_ERROR(), ERROR_FILE_NOT_FOUND);
    h = cf("C:\\Titanfall2\\new.bin", RD, SH, OPEN_EXISTING, 0);
    T_REQUIRE(h != BAD);
    T_EQ(rt_handle_close(h), TRUE);

    T_CASE("TRUNCATE_EXISTING on a missing file fails with ERROR_FILE_NOT_FOUND");
    T_EQ(cf("C:\\Titanfall2\\none.bin", WR, SH, TRUNCATE_EXISTING, 0), BAD);
    T_EQ(T_LAST_ERROR(), ERROR_FILE_NOT_FOUND);

    T_CASE("CREATE_ALWAYS and OPEN_ALWAYS report an existing file through ERROR_ALREADY_EXISTS");
    h = cf("C:\\Titanfall2\\always.bin", WR, SH, OPEN_ALWAYS, 0);
    T_REQUIRE(h != BAD);
    T_EQ(T_LAST_ERROR(), 0);
    T_EQ(rt_handle_close(h), TRUE);
    h = cf("C:\\Titanfall2\\always.bin", WR, SH, OPEN_ALWAYS, 0);
    T_REQUIRE(h != BAD);
    T_EQ(T_LAST_ERROR(), ERROR_ALREADY_EXISTS);
    T_EQ(rt_handle_close(h), TRUE);
    h = cf("C:\\Titanfall2\\always.bin", WR, SH, CREATE_ALWAYS, 0);
    T_REQUIRE(h != BAD);
    T_EQ(T_LAST_ERROR(), ERROR_ALREADY_EXISTS);
    T_EQ(rt_handle_close(h), TRUE);
    h = cf("C:\\Titanfall2\\fresh.bin", WR, SH, CREATE_ALWAYS, 0);
    T_REQUIRE(h != BAD);
    T_EQ(T_LAST_ERROR(), 0);
    T_EQ(rt_handle_close(h), TRUE);

    T_CASE("last error is written on success and on failure");
    h = cf("C:\\Titanfall2\\always.bin", RD, SH, OPEN_ALWAYS, 0);
    T_REQUIRE(h != BAD);
    T_EQ(T_LAST_ERROR(), ERROR_ALREADY_EXISTS);
    T_EQ(rt_handle_close(h), TRUE);
    T_EQ(cf("C:\\Titanfall2\\always.bin", WR, SH, CREATE_NEW, 0), BAD);
    T_EQ(T_LAST_ERROR(), ERROR_FILE_EXISTS);

    T_CASE("A and W open the same file");
    h = CALL(kernel32, CreateFileA, "C:\\Titanfall2\\same.bin", WR, SH, NULL, CREATE_NEW, 0, NULL);
    T_REQUIRE(h != BAD);
    T_EQ(rt_handle_close(h), TRUE);
    h = cf("C:\\Titanfall2\\same.bin", RD, SH, OPEN_EXISTING, 0);
    T_REQUIRE(h != BAD);
    T_EQ(rt_handle_close(h), TRUE);
    T_EQ(cf("C:\\Titanfall2\\same.bin", WR, SH, CREATE_NEW, 0), BAD);
    T_EQ(T_LAST_ERROR(), ERROR_FILE_EXISTS);

    T_CASE("a NULL name fails with ERROR_INVALID_PARAMETER");
    CALL(kernel32, SetLastError, 0xDEAD);
    T_EQ(CALL(kernel32, CreateFileW, NULL, RD, SH, NULL, OPEN_EXISTING, 0, NULL), BAD);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    T_CASE("an invalid disposition fails with ERROR_INVALID_PARAMETER");
    T_EQ(cf("C:\\Titanfall2\\new.bin", RD, SH, 0, 0), BAD);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_EQ(cf("C:\\Titanfall2\\new.bin", RD, SH, 6, 0), BAD);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    T_CASE("TRUNCATE_EXISTING without write access fails with ERROR_INVALID_PARAMETER");
    T_EQ(cf("C:\\Titanfall2\\new.bin", RD, SH, TRUNCATE_EXISTING, 0), BAD);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    T_CASE("a directory opens only with FILE_FLAG_BACKUP_SEMANTICS, otherwise ERROR_ACCESS_DENIED");
    T_EQ(cf("C:\\Titanfall2", RD, SH, OPEN_EXISTING, 0), BAD);
    T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);
    h = cf("C:\\Titanfall2", RD, SH, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS);
    T_REQUIRE(h != BAD);
    T_EQ(rt_handle_close(h), TRUE);

    T_CASE("NUL opens as a device");
    h = cf("NUL", RD, SH, OPEN_EXISTING, 0);
    T_REQUIRE(h != BAD);
    T_EQ(rt_handle_close(h), TRUE);

    T_CASE("DELETE_ON_CLOSE removes the name when the handle closes");
    h = cf("C:\\Titanfall2\\gone.bin", WR, SH, CREATE_NEW, FILE_FLAG_DELETE_ON_CLOSE);
    T_REQUIRE(h != BAD);
    T_EQ(rt_handle_close(h), TRUE);
    T_EQ(cf("C:\\Titanfall2\\gone.bin", RD, SH, OPEN_EXISTING, 0), BAD);
    T_EQ(T_LAST_ERROR(), ERROR_FILE_NOT_FOUND);

    T_CASE("a read-only file cannot be opened for writing: ERROR_ACCESS_DENIED");
    h = cf("C:\\Titanfall2\\ro.bin", WR, SH, CREATE_NEW, FILE_ATTRIBUTE_READONLY);
    T_REQUIRE(h != BAD);
    T_EQ(rt_handle_close(h), TRUE);
    T_EQ(cf("C:\\Titanfall2\\ro.bin", WR, SH, OPEN_EXISTING, 0), BAD);
    T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);
    h = cf("C:\\Titanfall2\\ro.bin", RD, SH, OPEN_EXISTING, 0);
    T_REQUIRE(h != BAD);
    T_EQ(rt_handle_close(h), TRUE);

    T_CASE("a sharing violation is logged and not enforced");
    h = cf("C:\\Titanfall2\\share.bin", WR, FILE_SHARE_READ, CREATE_NEW, 0);
    T_REQUIRE(h != BAD);
    h2 = cf("C:\\Titanfall2\\share.bin", WR, SH, OPEN_EXISTING, 0);
    T_REQUIRE(h2 != BAD);
    T_TRUE(T_LOGGED(RT_CH_FILE, "sharing violation not enforced"));
    T_EQ(rt_handle_close(h2), TRUE);
    T_EQ(rt_handle_close(h), TRUE);
}
