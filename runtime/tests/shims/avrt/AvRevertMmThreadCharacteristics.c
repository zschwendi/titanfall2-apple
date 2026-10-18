#include <stdint.h>

#include "rt/test.h"
#include "rt/win/mm.h"

TEST(avrt, AvRevertMmThreadCharacteristics)
{
    BOOL r;

    T_CASE("returns TRUE for the task handle");
    r = CALL(avrt, AvRevertMmThreadCharacteristics, RT_AVRT_TASK);
    T_EQ(r, TRUE);

    T_CASE("returns FALSE with ERROR_INVALID_HANDLE for NULL and for any other handle");
    CALL(kernel32, SetLastError, 0);
    r = CALL(avrt, AvRevertMmThreadCharacteristics, NULL);
    T_EQ(r, FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    CALL(kernel32, SetLastError, 0);
    r = CALL(avrt, AvRevertMmThreadCharacteristics, (HANDLE)(uintptr_t)0x44);
    T_EQ(r, FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_CASE("leaves last error alone on success");
    CALL(kernel32, SetLastError, 0x1234);
    r = CALL(avrt, AvRevertMmThreadCharacteristics, RT_AVRT_TASK);
    T_EQ(r, TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
