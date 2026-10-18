#include "rt/test.h"

TEST(kernel32, InterlockedPushEntrySList)
{
    SLIST_HEADER h;
    SLIST_ENTRY e1, e2, e3, e4;

    memset(&h, 0, sizeof h);
    memset(&e1, 0, sizeof e1);
    memset(&e2, 0, sizeof e2);
    memset(&e3, 0, sizeof e3);
    memset(&e4, 0, sizeof e4);

    T_CASE("returns NULL for an empty list");
    T_TRUE(CALL(kernel32, InterlockedPushEntrySList, &h, &e1) == NULL);
    T_EQ(h.Region, (ULONGLONG)(uintptr_t)&e1);
    T_EQ((USHORT)h.Alignment, 1);                   /* the depth field, read directly as inlined guest code does */
    T_TRUE(e1.Next == NULL);

    T_CASE("returns the previous first entry");
    T_TRUE(CALL(kernel32, InterlockedPushEntrySList, &h, &e2) == &e1);
    T_EQ(h.Region, (ULONGLONG)(uintptr_t)&e2);

    T_CASE("the entry becomes the first, its Next is the previous first");
    T_TRUE(e2.Next == &e1);
    T_TRUE(CALL(kernel32, InterlockedPushEntrySList, &h, &e3) == &e2);
    T_TRUE(h.Region == (ULONGLONG)(uintptr_t)&e3);
    T_TRUE(e3.Next == &e2);
    T_TRUE(e2.Next == &e1 && e1.Next == NULL);     /* the whole chain, first to last */

    T_CASE("the depth in the low 16 bits counts up");
    T_EQ((USHORT)h.Alignment, 3);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, InterlockedPushEntrySList, &h, &e4);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_TRAPS(CALL(kernel32, InterlockedPushEntrySList, NULL, &e4), "NULL list header");
    T_TRAPS(CALL(kernel32, InterlockedPushEntrySList, &h, NULL), "NULL list entry");
    T_TRAPS(CALL(kernel32, InterlockedPushEntrySList, (PSLIST_HEADER)((char *)&h + 8), &e4), "not 16-byte aligned");
    T_TRAPS(CALL(kernel32, InterlockedPushEntrySList, &h, (PSLIST_ENTRY)((char *)&e4 + 8)), "not 16-byte aligned");
}
