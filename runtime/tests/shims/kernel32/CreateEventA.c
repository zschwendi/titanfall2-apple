#include "rt/test.h"
#include "rt/handle.h"
#include "rt/sync.h"

/* Named objects of another kind are made with the core's own calls (rt_mutex_create,
 * rt_semaphore_create).  The names here are ASCII, so they are copied to WCHAR by hand. */
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

TEST(kernel32, CreateEventA)
{
    HANDLE h1, h2, h3, m, s;
    char name[MAX_PATH + 40];
    WCHAR wname[32];
    SECURITY_ATTRIBUTES sa_in = { .nLength = sizeof(SECURITY_ATTRIBUTES), .lpSecurityDescriptor = NULL, .bInheritHandle = TRUE };
    SECURITY_ATTRIBUTES sa_no = { .nLength = sizeof(SECURITY_ATTRIBUTES), .lpSecurityDescriptor = NULL, .bInheritHandle = FALSE };

    T_CASE("an unnamed event starts set or not as bInitialState says");
    h1 = CALL(kernel32, CreateEventA, NULL, TRUE, TRUE, NULL);       /* manual-reset, set */
    T_REQUIRE(h1 != NULL);
    T_EQ(rt_wait_handle(T_CTX(), h1, 0, FALSE), WAIT_OBJECT_0);
    T_EQ(rt_handle_close(h1), TRUE);
    h1 = CALL(kernel32, CreateEventA, NULL, TRUE, FALSE, NULL);      /* manual-reset, not set */
    T_REQUIRE(h1 != NULL);
    T_EQ(rt_wait_handle(T_CTX(), h1, 0, FALSE), WAIT_TIMEOUT);
    T_EQ(rt_handle_close(h1), TRUE);
    h1 = CALL(kernel32, CreateEventA, NULL, FALSE, TRUE, NULL);      /* auto-reset, set */
    T_REQUIRE(h1 != NULL);
    T_EQ(rt_wait_handle(T_CTX(), h1, 0, FALSE), WAIT_OBJECT_0);
    T_EQ(rt_handle_close(h1), TRUE);
    h1 = CALL(kernel32, CreateEventA, NULL, FALSE, FALSE, NULL);     /* auto-reset, not set */
    T_REQUIRE(h1 != NULL);
    T_EQ(rt_wait_handle(T_CTX(), h1, 0, FALSE), WAIT_TIMEOUT);
    T_EQ(rt_handle_close(h1), TRUE);

    T_CASE("manual-reset stays set after a wait and auto-reset is reset by one");
    h1 = CALL(kernel32, CreateEventA, NULL, TRUE, TRUE, NULL);
    T_REQUIRE(h1 != NULL);
    T_EQ(rt_wait_handle(T_CTX(), h1, 0, FALSE), WAIT_OBJECT_0);
    T_EQ(rt_wait_handle(T_CTX(), h1, 0, FALSE), WAIT_OBJECT_0);      /* still set */
    T_EQ(rt_handle_close(h1), TRUE);
    h1 = CALL(kernel32, CreateEventA, NULL, FALSE, TRUE, NULL);
    T_REQUIRE(h1 != NULL);
    T_EQ(rt_wait_handle(T_CTX(), h1, 0, FALSE), WAIT_OBJECT_0);
    T_EQ(rt_wait_handle(T_CTX(), h1, 0, FALSE), WAIT_TIMEOUT);       /* reset by the wait */
    T_EQ(rt_handle_close(h1), TRUE);

    T_CASE("two unnamed creates give two events, each with ERROR_SUCCESS");
    CALL(kernel32, SetLastError, 0x1234);
    h1 = CALL(kernel32, CreateEventA, NULL, FALSE, FALSE, NULL);
    T_REQUIRE(h1 != NULL);
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);
    CALL(kernel32, SetLastError, 0x1234);
    h2 = CALL(kernel32, CreateEventA, NULL, FALSE, FALSE, NULL);
    T_REQUIRE(h2 != NULL);
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);
    T_EQ(rt_event_set(T_CTX(), h1), TRUE);
    T_EQ(rt_wait_handle(T_CTX(), h2, 0, FALSE), WAIT_TIMEOUT);       /* h2 is another event */
    T_EQ(rt_wait_handle(T_CTX(), h1, 0, FALSE), WAIT_OBJECT_0);
    T_EQ(rt_handle_close(h1), TRUE);
    T_EQ(rt_handle_close(h2), TRUE);

    T_CASE("a first create with a name gives ERROR_SUCCESS");
    CALL(kernel32, SetLastError, 0x1234);
    h1 = CALL(kernel32, CreateEventA, NULL, FALSE, TRUE, "first-named");
    T_REQUIRE(h1 != NULL);
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);
    T_EQ(rt_handle_close(h1), TRUE);

    T_CASE("a second create with the same name gives a handle to the same event with ERROR_ALREADY_EXISTS, and ignores its flags");
    CALL(kernel32, SetLastError, 0x1234);
    h1 = CALL(kernel32, CreateEventA, NULL, FALSE, FALSE, "shared");   /* auto-reset, not set */
    T_REQUIRE(h1 != NULL);
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);
    CALL(kernel32, SetLastError, 0x1234);
    h2 = CALL(kernel32, CreateEventA, NULL, TRUE, TRUE, "shared");    /* manual-reset, set: ignored */
    T_REQUIRE(h2 != NULL);
    T_EQ(T_LAST_ERROR(), ERROR_ALREADY_EXISTS);
    T_EQ(rt_wait_handle(T_CTX(), h2, 0, FALSE), WAIT_TIMEOUT);       /* not set: the same event */
    T_EQ(rt_event_set(T_CTX(), h1), TRUE);
    T_EQ(rt_wait_handle(T_CTX(), h2, 0, FALSE), WAIT_OBJECT_0);      /* the same event */
    T_EQ(rt_wait_handle(T_CTX(), h1, 0, FALSE), WAIT_TIMEOUT);       /* still auto-reset */
    T_EQ(rt_handle_close(h1), TRUE);
    T_EQ(rt_handle_close(h2), TRUE);

    T_CASE("names are case sensitive");
    h1 = CALL(kernel32, CreateEventA, NULL, FALSE, FALSE, "CaseName");
    T_REQUIRE(h1 != NULL);
    h2 = CALL(kernel32, CreateEventA, NULL, FALSE, FALSE, "casename");
    T_REQUIRE(h2 != NULL);
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);                             /* another name: a new event */
    T_EQ(rt_event_set(T_CTX(), h1), TRUE);
    T_EQ(rt_wait_handle(T_CTX(), h2, 0, FALSE), WAIT_TIMEOUT);
    T_EQ(rt_wait_handle(T_CTX(), h1, 0, FALSE), WAIT_OBJECT_0);
    T_EQ(rt_handle_close(h1), TRUE);
    T_EQ(rt_handle_close(h2), TRUE);

    T_CASE("a name of MAX_PATH characters is accepted");
    fill_name(name, 'n', MAX_PATH - 1);                              /* one shorter: accepted too */
    CALL(kernel32, SetLastError, 0x1234);
    h1 = CALL(kernel32, CreateEventA, NULL, FALSE, FALSE, name);
    T_REQUIRE(h1 != NULL);
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);
    T_EQ(name[MAX_PATH - 2], 'n');                                   /* the name is not changed */
    T_EQ(name[MAX_PATH - 1], 0);
    name[MAX_PATH - 2] = 'm';                                        /* differs only in the last char */
    h2 = CALL(kernel32, CreateEventA, NULL, FALSE, FALSE, name);
    T_REQUIRE(h2 != NULL);
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);                             /* the whole name is compared */
    name[MAX_PATH - 2] = 'n';
    h3 = CALL(kernel32, CreateEventA, NULL, FALSE, FALSE, name);
    T_REQUIRE(h3 != NULL);
    T_EQ(T_LAST_ERROR(), ERROR_ALREADY_EXISTS);
    T_EQ(rt_handle_close(h1), TRUE);
    T_EQ(rt_handle_close(h2), TRUE);
    T_EQ(rt_handle_close(h3), TRUE);
    fill_name(name, 'n', MAX_PATH);                                  /* exactly MAX_PATH: accepted */
    CALL(kernel32, SetLastError, 0x1234);
    h1 = CALL(kernel32, CreateEventA, NULL, FALSE, FALSE, name);
    T_REQUIRE(h1 != NULL);
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);
    T_EQ(rt_handle_close(h1), TRUE);

    T_CASE("a name longer than MAX_PATH fails with ERROR_FILENAME_EXCED_RANGE");
    fill_name(name, 'n', MAX_PATH + 1);
    CALL(kernel32, SetLastError, 0x1234);
    h1 = CALL(kernel32, CreateEventA, NULL, FALSE, FALSE, name);
    T_TRUE(h1 == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_FILENAME_EXCED_RANGE);
    fill_name(name, 'n', MAX_PATH + 30);
    CALL(kernel32, SetLastError, 0x1234);
    h1 = CALL(kernel32, CreateEventA, NULL, FALSE, FALSE, name);
    T_TRUE(h1 == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_FILENAME_EXCED_RANGE);

    T_CASE("a name of another kind of object fails with ERROR_INVALID_HANDLE");
    ascii_to_wide(wname, "kind-clash");
    m = rt_mutex_create(T_CTX(), FALSE, wname);
    T_REQUIRE(m != NULL);
    CALL(kernel32, SetLastError, 0x1234);
    h1 = CALL(kernel32, CreateEventA, NULL, FALSE, FALSE, "kind-clash");
    T_TRUE(h1 == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    ascii_to_wide(wname, "kind-sem");
    s = rt_semaphore_create(T_CTX(), 0, 1, wname);
    T_REQUIRE(s != NULL);
    CALL(kernel32, SetLastError, 0x1234);
    h1 = CALL(kernel32, CreateEventA, NULL, FALSE, FALSE, "kind-sem");
    T_TRUE(h1 == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(rt_handle_close(m), TRUE);
    T_EQ(rt_handle_close(s), TRUE);

    T_CASE("the security attributes pointer, NULL or not, has no effect");
    h1 = CALL(kernel32, CreateEventA, &sa_in, TRUE, TRUE, NULL);     /* inherit: ignored */
    T_REQUIRE(h1 != NULL);
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);
    T_EQ(rt_wait_handle(T_CTX(), h1, 0, FALSE), WAIT_OBJECT_0);      /* the flags still apply */
    h2 = CALL(kernel32, CreateEventA, &sa_no, FALSE, FALSE, "sa-named");
    T_REQUIRE(h2 != NULL);
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);
    h3 = CALL(kernel32, CreateEventA, NULL, FALSE, FALSE, "sa-named");
    T_REQUIRE(h3 != NULL);
    T_EQ(T_LAST_ERROR(), ERROR_ALREADY_EXISTS);
    T_EQ(rt_event_set(T_CTX(), h2), TRUE);
    T_EQ(rt_wait_handle(T_CTX(), h3, 0, FALSE), WAIT_OBJECT_0);      /* the same event */
    T_EQ(sa_in.nLength, sizeof(SECURITY_ATTRIBUTES));                /* not written */
    T_TRUE(sa_in.lpSecurityDescriptor == NULL);
    T_EQ(sa_in.bInheritHandle, TRUE);
    T_EQ(sa_no.nLength, sizeof(SECURITY_ATTRIBUTES));
    T_TRUE(sa_no.lpSecurityDescriptor == NULL);
    T_EQ(sa_no.bInheritHandle, FALSE);
    T_EQ(rt_handle_close(h1), TRUE);
    T_EQ(rt_handle_close(h2), TRUE);
    T_EQ(rt_handle_close(h3), TRUE);

    /* The empty name is refused by the shim; a backslash after the prefix by the core (core/handle.c). */
    T_TRAPS(CALL(kernel32, CreateEventA, NULL, TRUE, FALSE, ""), "empty name");
    T_TRAPS(CALL(kernel32, CreateEventA, NULL, TRUE, FALSE, "a\\b"), "backslash");   /* the core stops the run */
}
