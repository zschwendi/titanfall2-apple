#include "rt/test.h"

TEST(kernel32, QueryDepthSList)
{
    SLIST_HEADER h, before;
    SLIST_ENTRY e;

    memset(&h, 0, sizeof h);
    memset(&e, 0, sizeof e);

    T_CASE("returns 0 for an empty list");
    T_EQ(CALL(kernel32, QueryDepthSList, &h), 0);

    T_CASE("returns the depth in the low 16 bits, up to 65535");
    h.Alignment = 5;
    h.Region = (ULONGLONG)(uintptr_t)&e;
    T_EQ(CALL(kernel32, QueryDepthSList, &h), 5);
    h.Alignment = 0xFFFF;                           /* the largest count the field holds */
    T_EQ(CALL(kernel32, QueryDepthSList, &h), 65535);

    T_CASE("ignores the sequence above the depth");
    h.Alignment = 0x0123456789AB0000ull;            /* depth 0 with a sequence above it */
    T_EQ(CALL(kernel32, QueryDepthSList, &h), 0);
    h.Alignment = 0xFFFFFFFFFFFF0007ull;            /* depth 7 with a full sequence */
    T_EQ(CALL(kernel32, QueryDepthSList, &h), 7);

    T_CASE("the header is not changed");
    before = h;
    CALL(kernel32, QueryDepthSList, &h);
    T_TRUE(h.Alignment == before.Alignment && h.Region == before.Region);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, QueryDepthSList, &h);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_TRAPS(CALL(kernel32, QueryDepthSList, NULL), "NULL list header");
    T_TRAPS(CALL(kernel32, QueryDepthSList, (PSLIST_HEADER)((char *)&h + 8)), "not 16-byte aligned");
}
