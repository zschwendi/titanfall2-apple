#include "rt/test.h"

TEST(kernel32, InterlockedPopEntrySList)
{
    SLIST_HEADER h;
    SLIST_ENTRY e1, e2, e3;

    memset(&h, 0, sizeof h);
    memset(&e1, 0, sizeof e1);
    memset(&e2, 0, sizeof e2);
    memset(&e3, 0, sizeof e3);

    T_CASE("returns NULL and leaves the header alone for an empty list");
    T_TRUE(CALL(kernel32, InterlockedPopEntrySList, &h) == NULL);
    T_EQ(h.Alignment, 0);
    T_EQ(h.Region, 0);

    /* Three entries in the order the header holds them: e3 first, then e2, then e1. */
    e1.Next = NULL;
    e2.Next = &e1;
    e3.Next = &e2;

    T_CASE("returns the first entry and removes it");
    h.Alignment = 3;
    h.Region = (ULONGLONG)(uintptr_t)&e3;
    T_TRUE(CALL(kernel32, InterlockedPopEntrySList, &h) == &e3);
    T_EQ(h.Region, (ULONGLONG)(uintptr_t)&e2);
    T_TRUE(CALL(kernel32, InterlockedPopEntrySList, &h) == &e2);
    T_EQ(h.Region, (ULONGLONG)(uintptr_t)&e1);
    T_TRUE(CALL(kernel32, InterlockedPopEntrySList, &h) == &e1);
    T_EQ(h.Region, 0);

    T_CASE("the depth in the low 16 bits counts down");
    h.Alignment = (0xABCDull << 16) | 3;            /* depth 3 with a sequence above it */
    h.Region = (ULONGLONG)(uintptr_t)&e3;
    CALL(kernel32, InterlockedPopEntrySList, &h);
    T_EQ((USHORT)h.Alignment, 2);
    CALL(kernel32, InterlockedPopEntrySList, &h);
    T_EQ((USHORT)h.Alignment, 1);
    CALL(kernel32, InterlockedPopEntrySList, &h);
    T_EQ((USHORT)h.Alignment, 0);
    T_EQ(h.Region, 0);

    T_CASE("does not change last error");
    h.Alignment = 2;
    h.Region = (ULONGLONG)(uintptr_t)&e2;
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, InterlockedPopEntrySList, &h);   /* removes e2 */
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, InterlockedPopEntrySList, &h);   /* removes e1 */
    CALL(kernel32, InterlockedPopEntrySList, &h);   /* the list is empty now */
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_TRAPS(CALL(kernel32, InterlockedPopEntrySList, NULL), "NULL list header");
    T_TRAPS(CALL(kernel32, InterlockedPopEntrySList, (PSLIST_HEADER)((char *)&h + 8)), "not 16-byte aligned");
}
