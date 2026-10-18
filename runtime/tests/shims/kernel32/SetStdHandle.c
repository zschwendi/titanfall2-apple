#include "rt/test.h"
#include "rt/handle.h"
#include "rt/win/file.h"

/* Handles are made with the core's own calls, as in CloseHandle's test.  The standard handles
 * are saved first and put back at the end, so the rest of the test runs on the real ones. */
static void nothing_destroyed(RtObject *o)
{
    (void)o;
}

static const RtObjectOps event_ops = { .name = "test-event", .destroy = nothing_destroyed };

static HANDLE make_handle(void)
{
    RtObject *o = rt_object_new(RT_K_EVENT, sizeof(RtObject), &event_ops, NULL, NULL);
    T_TRUE(o != NULL);
    return o ? rt_handle_new(o) : NULL;
}

TEST(kernel32, SetStdHandle)
{
    const DWORD which[3] = { STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE };
    HANDLE orig[3], h1, h2;
    unsigned i;

    for (i = 0; i < 3; i++) orig[i] = CALL(kernel32, GetStdHandle, which[i]);
    h1 = make_handle();
    h2 = make_handle();
    T_REQUIRE(h1 != NULL && h2 != NULL);            /* nothing is changed yet */

    T_CASE("stores a handle, which GetStdHandle then returns");
    T_EQ(CALL(kernel32, SetStdHandle, STD_OUTPUT_HANDLE, h1), TRUE);
    T_EQ(CALL(kernel32, GetStdHandle, STD_OUTPUT_HANDLE), h1);

    T_CASE("stores NULL and INVALID_HANDLE_VALUE as they are");
    T_EQ(CALL(kernel32, SetStdHandle, STD_ERROR_HANDLE, NULL), TRUE);
    T_EQ(CALL(kernel32, GetStdHandle, STD_ERROR_HANDLE), NULL);
    T_EQ(CALL(kernel32, SetStdHandle, STD_ERROR_HANDLE, INVALID_HANDLE_VALUE), TRUE);
    T_EQ(CALL(kernel32, GetStdHandle, STD_ERROR_HANDLE), INVALID_HANDLE_VALUE);

    T_CASE("replaces a handle and closes nothing: the old one stays open");
    T_EQ(CALL(kernel32, SetStdHandle, STD_OUTPUT_HANDLE, h2), TRUE);   /* replaces h1 */
    T_EQ(CALL(kernel32, GetStdHandle, STD_OUTPUT_HANDLE), h2);
    T_EQ(CALL(kernel32, CloseHandle, h1), TRUE);    /* h1 was not closed: CloseHandle succeeds */

    T_CASE("another number fails with ERROR_INVALID_HANDLE and changes nothing");
    T_EQ(CALL(kernel32, SetStdHandle, 0, h1), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(CALL(kernel32, SetStdHandle, 1, h1), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(CALL(kernel32, SetStdHandle, (DWORD)-9, h1), FALSE);      /* just above STD_INPUT_HANDLE */
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(CALL(kernel32, SetStdHandle, (DWORD)-13, h1), FALSE);     /* just below STD_ERROR_HANDLE */
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(CALL(kernel32, GetStdHandle, STD_INPUT_HANDLE), orig[0]);
    T_EQ(CALL(kernel32, GetStdHandle, STD_OUTPUT_HANDLE), h2);
    T_EQ(CALL(kernel32, GetStdHandle, STD_ERROR_HANDLE), INVALID_HANDLE_VALUE);

    T_CASE("does not change last error on success");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, SetStdHandle, STD_INPUT_HANDLE, h1), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, SetStdHandle, STD_ERROR_HANDLE, NULL), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* put the original standard handles back (the core accepts every value) */
    for (i = 0; i < 3; i++) CALL(kernel32, SetStdHandle, which[i], orig[i]);
}
