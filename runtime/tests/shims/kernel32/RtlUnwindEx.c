#include "rt/test.h"
#include "rt/win/exception.h"
#include <string.h>

/* RtlUnwindEx is a trap: it has no case, only two refusals.  The first is a call with no
 * exception record; the second carries a longjmp's exception code (0x80000026). */
TEST(kernel32, RtlUnwindEx)
{
    EXCEPTION_RECORD rec;

    T_TRAPS(CALL(kernel32, RtlUnwindEx, NULL, NULL, NULL, NULL, NULL, NULL), "RtlUnwindEx: a transfer of control");

    memset(&rec, 0, sizeof rec);
    rec.ExceptionCode = 0x80000026;
    T_TRAPS(CALL(kernel32, RtlUnwindEx, NULL, NULL, &rec, NULL, NULL, NULL), "exception code 0x80000026");
}
