#include "rt/test.h"
#include "rt/sync.h"

/* The events are made with the core's own calls (rt_event_create, rt_mutex_create), which take
 * the name wide, and are used through the core's wait and set calls, since no shim for them is
 * needed here.  wide_name turns the ASCII name the test uses into UTF-16. */
static void wide_name(WCHAR *out, const char *s)
{
    while ((*out++ = (WCHAR)(unsigned char)*s++) != 0) { }
}

TEST(kernel32, OpenEventA)
{
    WCHAR wname[MAX_PATH + 1];
    char longname[MAX_PATH + 2];
    HANDLE made, h, h2;
    unsigned i;

    T_CASE("opens an existing named event, and a set through one handle shows through the other");
    wide_name(wname, "OpenEventA-shared");
    made = rt_event_create(T_CTX(), TRUE, FALSE, wname);     /* manual-reset, not set */
    T_REQUIRE(made != NULL);
    h = CALL(kernel32, OpenEventA, 0, FALSE, "OpenEventA-shared");
    T_REQUIRE(h != NULL);
    T_EQ(rt_wait_handle(T_CTX(), h, 0, FALSE), WAIT_TIMEOUT);
    T_EQ(rt_event_set(T_CTX(), made), TRUE);
    T_EQ(rt_wait_handle(T_CTX(), h, 0, FALSE), WAIT_OBJECT_0);    /* the set shows through h */
    T_EQ(rt_event_reset(T_CTX(), h), TRUE);
    T_EQ(rt_wait_handle(T_CTX(), made, 0, FALSE), WAIT_TIMEOUT);  /* and the reset through made */

    T_CASE("a name with a Local prefix names the same event as the name without it");
    h = CALL(kernel32, OpenEventA, 0, FALSE, "Local\\OpenEventA-shared");
    T_REQUIRE(h != NULL);
    T_EQ(rt_event_set(T_CTX(), h), TRUE);
    T_EQ(rt_wait_handle(T_CTX(), made, 0, FALSE), WAIT_OBJECT_0);
    T_EQ(rt_event_reset(T_CTX(), made), TRUE);

    T_CASE("the access mask and the inherit flag are ignored");
    h = CALL(kernel32, OpenEventA, 0, FALSE, "OpenEventA-shared");
    T_REQUIRE(h != NULL);
    h2 = CALL(kernel32, OpenEventA, (DWORD)-1, TRUE, "OpenEventA-shared");
    T_REQUIRE(h2 != NULL);
    T_EQ(rt_event_set(T_CTX(), h2), TRUE);
    T_EQ(rt_wait_handle(T_CTX(), h, 0, FALSE), WAIT_OBJECT_0);
    T_EQ(rt_event_reset(T_CTX(), h), TRUE);

    T_CASE("a name of MAX_PATH characters is accepted");
    for (i = 0; i < MAX_PATH; i++)
        longname[i] = 'm';
    longname[MAX_PATH] = 0;
    wide_name(wname, longname);
    made = rt_event_create(T_CTX(), TRUE, FALSE, wname);
    T_REQUIRE(made != NULL);
    h = CALL(kernel32, OpenEventA, 0, FALSE, longname);
    T_REQUIRE(h != NULL);
    T_EQ(rt_wait_handle(T_CTX(), h, 0, FALSE), WAIT_TIMEOUT);

    T_CASE("a name that does not exist fails with ERROR_FILE_NOT_FOUND");
    T_TRUE(CALL(kernel32, OpenEventA, 0, FALSE, "OpenEventA-missing") == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_FILE_NOT_FOUND);

    T_CASE("a NULL name fails with ERROR_INVALID_PARAMETER");
    T_TRUE(CALL(kernel32, OpenEventA, 0, FALSE, NULL) == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    T_CASE("a name that belongs to an object of another kind fails with ERROR_INVALID_HANDLE");
    wide_name(wname, "OpenEventA-mutex");
    T_REQUIRE(rt_mutex_create(T_CTX(), FALSE, wname) != NULL);
    T_TRUE(CALL(kernel32, OpenEventA, 0, FALSE, "OpenEventA-mutex") == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_CASE("a name longer than MAX_PATH fails with ERROR_FILENAME_EXCED_RANGE");
    for (i = 0; i < MAX_PATH + 1; i++)
        longname[i] = 'm';
    longname[MAX_PATH + 1] = 0;
    T_TRUE(CALL(kernel32, OpenEventA, 0, FALSE, longname) == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_FILENAME_EXCED_RANGE);

    T_CASE("does not change last error on success");
    CALL(kernel32, SetLastError, 0x1234);
    h = CALL(kernel32, OpenEventA, 0, FALSE, "OpenEventA-shared");
    T_REQUIRE(h != NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* The empty name is refused by the shim as unimplemented (RT-TRAPS).  The name forms the core
     * refuses are checked here as fatal reports, with the name in them: the shim does not compare
     * backslashes itself. */
    T_TRAPS(CALL(kernel32, OpenEventA, 0, FALSE, ""), "an empty name");
    T_TRAPS(CALL(kernel32, OpenEventA, 0, FALSE, "Local\\"), "after its prefix");
    T_TRAPS(CALL(kernel32, OpenEventA, 0, FALSE, "Foo\\Bar"), "after its prefix");
}
