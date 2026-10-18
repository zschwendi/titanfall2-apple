#include "rt/test.h"

TEST(kernel32, InterlockedFlushSList)
{
    SLIST_HEADER h;
    SLIST_ENTRY e1, e2, e3;
    PSLIST_ENTRY first;

    memset(&h, 0, sizeof h);
    memset(&e1, 0, sizeof e1);
    memset(&e2, 0, sizeof e2);
    memset(&e3, 0, sizeof e3);

    T_CASE("returns NULL and leaves an empty list alone");
    T_TRUE(CALL(kernel32, InterlockedFlushSList, &h) == NULL);
    T_EQ(h.Alignment, 0);
    T_EQ(h.Region, 0);

    /* Three entries in the order the header holds them: e3 first, then e2, then e1. */
    e1.Next = NULL;
    e2.Next = &e1;
    e3.Next = &e2;
    h.Alignment = 3;
    h.Region = (ULONGLONG)(uintptr_t)&e3;

    first = CALL(kernel32, InterlockedFlushSList, &h);

    T_CASE("returns the first entry and leaves the list empty");
    T_TRUE(first == &e3);
    T_EQ(h.Region, 0);

    T_CASE("the entries after the first stay chained");
    T_TRUE(first->Next == &e2);
    T_TRUE(e2.Next == &e1);
    T_TRUE(e1.Next == NULL);

    T_CASE("the depth reads 0 afterwards");
    T_EQ((USHORT)h.Alignment, 0);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    h.Alignment = 2;
    h.Region = (ULONGLONG)(uintptr_t)&e2;
    CALL(kernel32, InterlockedFlushSList, &h);      /* a non-empty list */
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, InterlockedFlushSList, &h);      /* now empty */
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_TRAPS(CALL(kernel32, InterlockedFlushSList, NULL), "NULL list header");
    T_TRAPS(CALL(kernel32, InterlockedFlushSList, (PSLIST_HEADER)((char *)&h + 8)), "not 16-byte aligned");
}
