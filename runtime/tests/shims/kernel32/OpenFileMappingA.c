#include "rt/test.h"
#include "rt/handle.h"
#include "rt/win/file.h"
#include "rt/win/memory.h"

/* A string of n copies of c, NUL-terminated; s must hold n + 1 chars. */
static void fill_name(char *s, char c, unsigned n)
{
    unsigned i;

    for (i = 0; i < n; i++)
        s[i] = c;
    s[n] = 0;
}

TEST(kernel32, OpenFileMappingA)
{
    HANDLE h1, h2;
    char name[MAX_PATH + 40];

    T_CASE("a name of a section that exists gives a handle and leaves last error alone");
    h1 = CALL(kernel32, CreateFileMappingA, INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 65536, "opened-section");
    T_REQUIRE(h1 != NULL);
    CALL(kernel32, SetLastError, 0x1234);
    h2 = CALL(kernel32, OpenFileMappingA, FILE_MAP_ALL_ACCESS, FALSE, "opened-section");
    T_REQUIRE(h2 != NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);                                    /* success: not set */
    T_EQ(rt_handle_close(h2), TRUE);
    T_EQ(rt_handle_close(h1), TRUE);

    T_CASE("a name nobody made fails with ERROR_FILE_NOT_FOUND");
    CALL(kernel32, SetLastError, 0x1234);
    h1 = CALL(kernel32, OpenFileMappingA, FILE_MAP_READ, FALSE, "nobody-made-this");
    T_TRUE(h1 == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_FILE_NOT_FOUND);

    T_CASE("a name of MAX_PATH - 1 characters is looked up and one of MAX_PATH characters or more fails with ERROR_FILENAME_EXCED_RANGE");
    fill_name(name, 'n', MAX_PATH - 1);                              /* one short of MAX_PATH: with its terminator it fits */
    h1 = CALL(kernel32, CreateFileMappingA, INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 65536, name);
    T_REQUIRE(h1 != NULL);
    CALL(kernel32, SetLastError, 0x1234);
    h2 = CALL(kernel32, OpenFileMappingA, FILE_MAP_READ, FALSE, name);
    T_REQUIRE(h2 != NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(rt_handle_close(h2), TRUE);
    T_EQ(rt_handle_close(h1), TRUE);
    fill_name(name, 'n', MAX_PATH);                                  /* exactly MAX_PATH: refused */
    CALL(kernel32, SetLastError, 0x1234);
    h1 = CALL(kernel32, OpenFileMappingA, FILE_MAP_READ, FALSE, name);
    T_TRUE(h1 == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_FILENAME_EXCED_RANGE);
    fill_name(name, 'n', MAX_PATH + 30);
    CALL(kernel32, SetLastError, 0x1234);
    h1 = CALL(kernel32, OpenFileMappingA, FILE_MAP_READ, FALSE, name);
    T_TRUE(h1 == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_FILENAME_EXCED_RANGE);

    /* The shim refuses a NULL name (RT_UNIMPLEMENTED); the core stops on the other two, with no
     * guard in the shim.  Each report contains its text. */
    T_TRAPS(CALL(kernel32, OpenFileMappingA, FILE_MAP_READ, FALSE, NULL), "OpenFileMappingA with a NULL name");
    T_TRAPS(CALL(kernel32, OpenFileMappingA, FILE_MAP_READ, FALSE, ""), "an empty object name");
    T_TRAPS(CALL(kernel32, OpenFileMappingA, FILE_MAP_READ, FALSE, "a\\b"), "contains a backslash");
}
