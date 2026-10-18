/* Types of the test-only library `rttest` (runtime/tests/abi): shims that exist to prove the
 * generator and the test harness on every way an argument or a result can cross the guest
 * ABI.  Nothing here is part of the runtime proper.
 */
#ifndef RTTEST_H
#define RTTEST_H

#include "rt/wintypes.h"

/* 24 bytes: returned through a hidden pointer, passed as a pointer to the caller's copy. */
typedef struct { ULONGLONG a; double b; DWORD c; DWORD d; } RTTEST_BIG;
/* 8 and 2 bytes: passed and returned by value in a register. */
typedef struct { LONG lo; LONG hi; } RTTEST_PAIR;
typedef struct { WORD w; } RTTEST_WORD;

_Static_assert(sizeof(RTTEST_BIG) == 24 && sizeof(RTTEST_PAIR) == 8 && sizeof(RTTEST_WORD) == 2, "rttest structs");

#endif
