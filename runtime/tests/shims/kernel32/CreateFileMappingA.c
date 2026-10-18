#include "rt/test.h"
#include "rt/handle.h"
#include "rt/sync.h"
#include "rt/win/memory.h"

/* Named objects of another kind are made with the core's own calls (rt_mutex_create).  The
 * names here are ASCII, so they are copied to WCHAR by hand. */
static void ascii_to_wide(WCHAR *w, const char *s)
{
    while ((*w++ = (WCHAR)(unsigned char)*s++) != 0)
        ;
}

/* A string of n copies of c, NUL-terminated; s must hold n + 1 chars. */
static void fill_name(char *s, char c, unsigned n)
{
    unsigned i;

    for (i = 0; i < n; i++)
        s[i] = c;
    s[n] = 0;
}

TEST(kernel32, CreateFileMappingA)
{
    HANDLE h1, h2, h3, m;
    char name[MAX_PATH + 40];
    WCHAR wname[32];
    SECURITY_ATTRIBUTES sa_in = { .nLength = sizeof(SECURITY_ATTRIBUTES), .lpSecurityDescriptor = NULL, .bInheritHandle = TRUE };
    SECURITY_ATTRIBUTES sa_no = { .nLength = sizeof(SECURITY_ATTRIBUTES), .lpSecurityDescriptor = NULL, .bInheritHandle = FALSE };

    T_CASE("a section with no file and no name gives a handle and ERROR_SUCCESS");
    CALL(kernel32, SetLastError, 0x1234);
    h1 = CALL(kernel32, CreateFileMappingA, INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 65536, NULL);
    T_REQUIRE(h1 != NULL);
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);
    T_EQ(rt_handle_close(h1), TRUE);

    T_CASE("a first create with a name gives ERROR_SUCCESS and a second create with the same name a handle and ERROR_ALREADY_EXISTS");
    CALL(kernel32, SetLastError, 0x1234);
    h1 = CALL(kernel32, CreateFileMappingA, INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 65536, "shared-section");
    T_REQUIRE(h1 != NULL);
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);
    CALL(kernel32, SetLastError, 0x1234);
    h2 = CALL(kernel32, CreateFileMappingA, INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 65536, "shared-section");
    T_REQUIRE(h2 != NULL);
    T_EQ(T_LAST_ERROR(), ERROR_ALREADY_EXISTS);
    T_EQ(rt_handle_close(h1), TRUE);
    T_EQ(rt_handle_close(h2), TRUE);

    T_CASE("a name of MAX_PATH - 1 characters is accepted and one of MAX_PATH characters or more fails with ERROR_FILENAME_EXCED_RANGE");
    fill_name(name, 'n', MAX_PATH - 1);                              /* one short of MAX_PATH: with its terminator it fits */
    CALL(kernel32, SetLastError, 0x1234);
    h1 = CALL(kernel32, CreateFileMappingA, INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 65536, name);
    T_REQUIRE(h1 != NULL);
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);
    T_EQ(name[MAX_PATH - 2], 'n');                                   /* the name is not changed */
    T_EQ(name[MAX_PATH - 1], 0);
    T_EQ(rt_handle_close(h1), TRUE);
    fill_name(name, 'n', MAX_PATH);                                  /* exactly MAX_PATH: refused */
    CALL(kernel32, SetLastError, 0x1234);
    h1 = CALL(kernel32, CreateFileMappingA, INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 65536, name);
    T_TRUE(h1 == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_FILENAME_EXCED_RANGE);
    fill_name(name, 'n', MAX_PATH + 30);
    CALL(kernel32, SetLastError, 0x1234);
    h1 = CALL(kernel32, CreateFileMappingA, INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 65536, name);
    T_TRUE(h1 == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_FILENAME_EXCED_RANGE);

    T_CASE("no file and a size of 0 fails with ERROR_INVALID_PARAMETER");
    CALL(kernel32, SetLastError, 0x1234);
    h1 = CALL(kernel32, CreateFileMappingA, INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 0, NULL);
    T_TRUE(h1 == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    T_CASE("a closed handle given as the file fails with ERROR_INVALID_HANDLE");
    h1 = CALL(kernel32, CreateFileMappingA, INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 65536, NULL);
    T_REQUIRE(h1 != NULL);
    T_EQ(rt_handle_close(h1), TRUE);                                 /* h1 is not open any more */
    CALL(kernel32, SetLastError, 0x1234);
    h2 = CALL(kernel32, CreateFileMappingA, h1, NULL, PAGE_READWRITE, 0, 65536, NULL);
    T_TRUE(h2 == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_CASE("a name of another kind of object fails with ERROR_INVALID_HANDLE");
    ascii_to_wide(wname, "kind-clash");
    m = rt_mutex_create(T_CTX(), FALSE, wname);
    T_REQUIRE(m != NULL);
    CALL(kernel32, SetLastError, 0x1234);
    h1 = CALL(kernel32, CreateFileMappingA, INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 65536, "kind-clash");
    T_TRUE(h1 == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(rt_handle_close(m), TRUE);

    T_CASE("the security attributes pointer, NULL or not, has no effect");
    CALL(kernel32, SetLastError, 0x1234);
    h1 = CALL(kernel32, CreateFileMappingA, INVALID_HANDLE_VALUE, &sa_in, PAGE_READWRITE, 0, 65536, "sa-named");
    T_REQUIRE(h1 != NULL);
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);
    CALL(kernel32, SetLastError, 0x1234);
    h2 = CALL(kernel32, CreateFileMappingA, INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 65536, "sa-named");
    T_REQUIRE(h2 != NULL);
    T_EQ(T_LAST_ERROR(), ERROR_ALREADY_EXISTS);
    h3 = CALL(kernel32, CreateFileMappingA, INVALID_HANDLE_VALUE, &sa_no, PAGE_READWRITE, 0, 65536, NULL);
    T_REQUIRE(h3 != NULL);
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);                             /* unnamed: not an existing name */
    T_EQ(sa_in.nLength, sizeof(SECURITY_ATTRIBUTES));                /* not written */
    T_TRUE(sa_in.lpSecurityDescriptor == NULL);
    T_EQ(sa_in.bInheritHandle, TRUE);
    T_EQ(sa_no.nLength, sizeof(SECURITY_ATTRIBUTES));
    T_TRUE(sa_no.lpSecurityDescriptor == NULL);
    T_EQ(sa_no.bInheritHandle, FALSE);
    T_EQ(rt_handle_close(h1), TRUE);
    T_EQ(rt_handle_close(h2), TRUE);
    T_EQ(rt_handle_close(h3), TRUE);

    /* The core stops on these, with no guard in the shim; each report contains its text. */
    T_TRAPS(CALL(kernel32, CreateFileMappingA, NULL, NULL, PAGE_READWRITE, 0, 65536, NULL), "a NULL file handle");
    T_TRAPS(CALL(kernel32, CreateFileMappingA, INVALID_HANDLE_VALUE, NULL, PAGE_EXECUTE_READ, 0, 65536, NULL), "protection 0x20");
    T_TRAPS(CALL(kernel32, CreateFileMappingA, INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 65536, ""), "an empty object name");
    T_TRAPS(CALL(kernel32, CreateFileMappingA, INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 65536, "a\\b"), "contains a backslash");
}
