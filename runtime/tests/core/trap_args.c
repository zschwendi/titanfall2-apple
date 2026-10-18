/* The stack arguments in the report of an import that has no shim (docs/RUNTIME.md section
 * 9.1; core/trap.c).
 *
 * Where the expected text comes from: the Windows x64 calling convention as Microsoft
 * documents it ("x64 calling convention": four arguments in rcx, rdx, r8 and r9, 32 bytes of
 * home space that the caller allocates, the fifth argument on at the next 8-byte slots above
 * it), which the harness's rt_test_invoke implements on its own; and the values this test
 * passes.
 */
#include <stdio.h>

#include "rt/test.h"
#include "rt/win/memory.h"

#define U64(x)  { RT_AC_U64, 8, { .u = (uint64_t)(x) } }

TEST(core, trap_args)
{
    uint64_t target = rt_test_trap_import();
    uint32_t *mem = CALL(kernel32, VirtualAlloc, NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    T_REQUIRE(mem != NULL);
    mem[0] = 0xb000;                                /* an array of two for argument 5, its count in argument 6 */
    mem[1] = 0xa100;
    mem[16] = 0xdeadbeef;                           /* where argument 8, an out-pointer, points */
    mem[23] = 0x12345678;
    const RtTestArg a[10] = { U64(0), U64(1), U64(0), U64(0), U64((uintptr_t)mem), U64(2), U64(7),
                              U64((uintptr_t)(mem + 16)), U64(0x7fff00000000ull), U64(0xFFFF) };
    char want[400];

    T_CASE("the words above the home space are printed as arguments 5 on");
    T_TRAPS(rt_test_invoke(target, 10, a, NULL), "FATAL unimplemented import rttest.dll!NotImplemented\n");
    T_TRAPS(rt_test_invoke(target, 10, a, NULL),
            "\n  the stack above the home space (arguments 5 to 12, if the function takes that many):\n    arg5  ");
    T_TRAPS(rt_test_invoke(target, 10, a, NULL), "\n    arg6  0000000000000002\n    arg7  0000000000000007\n    arg8  ");

    T_CASE("an argument that is the address of readable guest memory shows the 32 bytes there");
    snprintf(want, sizeof want, "    arg5  %016llx -> 0000b000 0000a100 00000000 00000000 00000000 00000000 00000000 00000000\n",
             (unsigned long long)(uintptr_t)mem);
    T_TRAPS(rt_test_invoke(target, 10, a, NULL), want);
    snprintf(want, sizeof want, "    arg8  %016llx -> deadbeef 00000000 00000000 00000000 00000000 00000000 00000000 12345678\n",
             (unsigned long long)(uintptr_t)(mem + 16));
    T_TRAPS(rt_test_invoke(target, 10, a, NULL), want);

    T_CASE("a small number and an address outside guest memory are printed alone");
    T_TRAPS(rt_test_invoke(target, 10, a, NULL), "\n    arg9  00007fff00000000\n    arg10 000000000000ffff\n    arg11 ");

    T_CASE("a call with no stack argument still gets its report");
    T_TRAPS(rt_test_invoke(target, 0, NULL, NULL), "\n  rcx=");
    T_TRAPS(rt_test_invoke(target, 0, NULL, NULL), "\n    arg12 ");
}
