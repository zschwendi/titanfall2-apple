#include "rt/test.h"
#include "rt/win/sysinfo.h"

TEST(kernel32, VerSetConditionMask)
{
    ULONGLONG m, m2;

    T_CASE("stores the operator of one version member and keeps the operators already in the mask");
    m = CALL(kernel32, VerSetConditionMask, 0, VER_MAJORVERSION, VER_GREATER_EQUAL);
    T_TRUE(m != 0);
    m2 = CALL(kernel32, VerSetConditionMask, m, VER_MINORVERSION, VER_LESS);
    T_TRUE(m2 != m);
    T_EQ(m2 & m, m);                                   /* nothing of the first call is lost */
    T_TRUE(CALL(kernel32, VerSetConditionMask, 0, VER_MINORVERSION, VER_LESS) !=
           CALL(kernel32, VerSetConditionMask, 0, VER_MAJORVERSION, VER_LESS));
    T_TRUE(CALL(kernel32, VerSetConditionMask, 0, VER_SERVICEPACKMAJOR, VER_EQUAL) !=
           CALL(kernel32, VerSetConditionMask, 0, VER_SERVICEPACKMINOR, VER_EQUAL));
    /* the four members and the five operators give twenty different masks */
    {
        static const DWORD t[4] = { VER_MAJORVERSION, VER_MINORVERSION, VER_SERVICEPACKMAJOR, VER_SERVICEPACKMINOR };
        ULONGLONG seen[20];
        unsigned n = 0;
        for (unsigned i = 0; i < 4; i++)
            for (BYTE c = VER_EQUAL; c <= VER_LESS_EQUAL; c++) {
                ULONGLONG x = CALL(kernel32, VerSetConditionMask, 0, t[i], c);
                for (unsigned k = 0; k < n; k++)
                    T_TRUE(seen[k] != x);
                seen[n++] = x;
            }
        T_EQ(n, 20);
    }
    /* the order of the calls does not matter */
    T_EQ(CALL(kernel32, VerSetConditionMask, CALL(kernel32, VerSetConditionMask, 0, VER_MINORVERSION, VER_LESS),
              VER_MAJORVERSION, VER_GREATER_EQUAL), m2);

    T_CASE("a later call for the same member replaces nothing and adds its bits");
    m = CALL(kernel32, VerSetConditionMask, 0, VER_MAJORVERSION, VER_EQUAL);
    m2 = CALL(kernel32, VerSetConditionMask, m, VER_MAJORVERSION, VER_GREATER);
    T_EQ(m2, CALL(kernel32, VerSetConditionMask, 0, VER_MAJORVERSION, VER_GREATER_EQUAL));   /* 1 | 2 == 3 */

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    (void)CALL(kernel32, VerSetConditionMask, 0, VER_MAJORVERSION, VER_GREATER_EQUAL);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_TRAPS(CALL(kernel32, VerSetConditionMask, 0, VER_BUILDNUMBER, VER_EQUAL), "VerSetConditionMask with TypeMask 0x4");
    T_TRAPS(CALL(kernel32, VerSetConditionMask, 0, VER_MAJORVERSION | VER_MINORVERSION, VER_EQUAL), "VerSetConditionMask with TypeMask 0x3");
    T_TRAPS(CALL(kernel32, VerSetConditionMask, 0, 0, VER_EQUAL), "VerSetConditionMask with TypeMask 0x0");
    T_TRAPS(CALL(kernel32, VerSetConditionMask, 0, VER_MAJORVERSION, 0), "VerSetConditionMask with Condition 0");
    T_TRAPS(CALL(kernel32, VerSetConditionMask, 0, VER_MAJORVERSION, VER_AND), "VerSetConditionMask with Condition 6");
}
