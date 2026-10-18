#include "rt/test.h"

TEST(kernel32, InitializeSListHead)
{
    SLIST_HEADER h;
    SLIST_ENTRY e;

    T_CASE("empties a header that held entries");
    memset(&h, 0xA5, sizeof h);
    h.Alignment = 0x0123456789ABCDEFull;            /* depth 0xCDEF and a sequence: all to be cleared */
    h.Region = (ULONGLONG)(uintptr_t)&e;            /* a first entry */
    CALL(kernel32, InitializeSListHead, &h);
    T_EQ(h.Alignment, 0);
    T_EQ(h.Region, 0);
    T_EQ((USHORT)h.Alignment, 0);

    T_CASE("a zeroed header stays empty");
    memset(&h, 0, sizeof h);
    CALL(kernel32, InitializeSListHead, &h);
    T_EQ(h.Alignment, 0);
    T_EQ(h.Region, 0);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, InitializeSListHead, &h);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_TRAPS(CALL(kernel32, InitializeSListHead, NULL), "NULL list header");
    T_TRAPS(CALL(kernel32, InitializeSListHead, (PSLIST_HEADER)((char *)&h + 8)), "not 16-byte aligned");
}
