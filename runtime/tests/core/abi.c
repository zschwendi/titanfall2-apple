/* The generated thunks and CALL wrappers, on every way an argument or a result crosses the
 * guest ABI (docs/RUNTIME.md section 3.1).  The shims under test are the test-only library
 * runtime/tests/abi/rttest; what is really being tested is runtime/tools/gen_shims.py.
 */
#include "rt/test.h"
#include "rttest_calls.h"

TEST(rttest, MixArgs)
{
    T_CASE("integer, float and double arguments in registers and on the stack");
    double r = CALL(rttest, MixArgs, 1, 2.0f, 3.0, -4, 5.5f, 6.25, 7, 8, 9, -10);
    T_EQ(r, 1.0 + 4.0 + 9.0 - 16.0 + 27.5 + 37.5 + 49.0 + 64.0 + 81.0 - 100.0);

    T_CASE("narrow integers with garbage above them");
    /* The harness fills the unused upper bits of every narrow argument with garbage; the
     * thunk must cut each one to its declared width and sign. */
    r = CALL(rttest, MixArgs, 0xFFFFFFFFu, 0.0f, 0.0, INT32_MIN, 0.0f, 0.0, 0, 0xFFFF, 0xFF, INT16_MIN);
    T_EQ(r, 4294967295.0 + 4.0 * -2147483648.0 + 8.0 * 65535.0 + 9.0 * 255.0 + 10.0 * -32768.0);
    r = CALL(rttest, MixArgs, 0, 0.0f, 0.0, 0, 0.0f, 0.0, 0x100000000ull, 0, 0, 0);
    T_EQ(r, 7.0 * 4294967296.0);
}

TEST(rttest, RetFloat)
{
    T_CASE("a float result comes back in xmm0");
    T_EQ(CALL(rttest, RetFloat, 1.5f, -3), -4.5f);
    T_EQ(CALL(rttest, RetFloat, 0.1f, 1), 0.1f);
}

TEST(rttest, MakeBig)
{
    T_CASE("a 24-byte struct is returned through the hidden pointer and every argument moves up one position");
    RTTEST_BIG in = { 100, 0.5, 7, 0xDEADBEEF };
    RTTEST_BIG out = CALL(rttest, MakeBig, 0x100000000ull, 1.25, 0x80000000u, in);
    T_EQ(out.a, 0x100000064ull);
    T_EQ(out.b, 1.75);
    T_EQ(out.c, 0x80000007u);
    T_EQ(out.d, 0xDEADBEEFu);

    T_CASE("a 24-byte struct argument arrives as a pointer to the caller's copy");
    T_EQ(in.a, 100);                    /* the callee worked on its own copy */
    T_EQ(in.d, 0xDEADBEEFu);
}

TEST(rttest, SwapPair)
{
    T_CASE("structs of 8 and 2 bytes are passed and returned by value in a register");
    RTTEST_PAIR p = { -5, 9 };
    RTTEST_WORD w = { 0x1234 };
    RTTEST_PAIR r = CALL(rttest, SwapPair, p, w);
    T_EQ(r.lo, 9 + 0x1234);
    T_EQ(r.hi, -5);
}

TEST(rttest, Negative)
{
    T_CASE("signed 16-bit and 8-bit arguments are sign-extended from their own width");
    T_EQ(CALL(rttest, Negative, -300, -7), -307);
    T_EQ(CALL(rttest, Negative, INT16_MIN, 127), -32768 + 127);

    T_CASE("a negative 32-bit result has a zero upper half in rax");
    /* CALL fails the test if a 32-bit result leaves the upper half of rax nonzero; look at
     * the raw register as well. */
    const RtTestArg args[2] = { { RT_AC_I16, 2, { .u = (uint64_t)(int64_t)-1 } }, { RT_AC_I8, 1, { .u = 0 } } };
    RtTestRet raw = rt_test_invoke(rt_test_import_slot(RT_ID_rttest_Negative), 2, args, NULL);
    T_EQ(raw.violations, 0);
    T_EQ(raw.rax, 0x00000000FFFFFFFFull);
}

TEST(rttest, Poke)
{
    T_CASE("a function without a result writes through its pointer argument");
    DWORD v[2] = { 1, 2 };
    CALL(rttest, Poke, &v[0], 0xCAFEF00D);
    T_EQ(v[0], 0xCAFEF00Du);
    T_EQ(v[1], 2);
}

TEST(rttest, Sum)
{
    T_CASE("a varargs function fetches its own arguments from registers and stack");
    /* SHIM_RAW: the test passes the raw 64-bit slots.  The count is a DWORD, so the garbage
     * in its upper half must be ignored. */
    const uint64_t slots[7] = { 0xDEADBEEF00000006ull, 1, 2, 3, 4, 5, 0x100000000ull };
    T_EQ(CALL(rttest, Sum, 7, slots), 15 + 0x100000000ull);
    const uint64_t none[1] = { 0 };
    T_EQ(CALL(rttest, Sum, 1, none), 0);
}

TEST(rttest, MovesRsp)
{
    T_CASE("a shim that returns with the guest stack pointer moved is stopped by its thunk");
    const uint64_t stay[1] = { 0 }, pop[1] = { 8 }, push[1] = { (uint64_t)-16 };
    T_EQ(CALL(rttest, MovesRsp, 1, stay), 1);
    T_TRAPS(CALL(rttest, MovesRsp, 1, pop), "FATAL shim rttest.dll!MovesRsp changed the guest stack pointer");
    T_TRAPS(CALL(rttest, MovesRsp, 1, pop), "(off by 8)");
    T_TRAPS(CALL(rttest, MovesRsp, 1, push), "(off by -16)");
}

TEST(rttest, ByOrdinal)
{
    T_CASE("a function exported by ordinal is bound by ordinal and by name to the same stub");
    T_EQ(CALL(rttest, ByOrdinal, 1), 8);
    uint64_t stub = rt_test_import_slot(RT_ID_rttest_ByOrdinal);
    T_EQ(rt_native_import("rttest.dll", NULL, 7), stub);
    T_EQ(rt_native_import("rttest.dll", "ByOrdinal", 0), stub);
    RtModule *m = rt_module_find("rttest");
    T_REQUIRE(m != NULL);
    T_EQ(rt_module_export(m, NULL, 7), stub);
    T_EQ(rt_module_export(m, "ByOrdinal", 0), stub);
}

TEST(rttest, HostState)
{
    T_CASE("a shim runs with the host float mode and the in-host mark set");
    /* Bit 63: in-host mark.  Bit 62: the shim is the innermost shim frame.  Low bits: FPCR,
     * which must be the host default (0) although the caller was in the guest mode. */
    T_EQ(CALL(rttest, HostState), 0xC000000000000000ull);
    T_EQ(RT_THREAD(T_CTX())->in_host, 0);           /* and the mark is cleared on the way out */
    T_TRUE(RT_THREAD(T_CTX())->shim == NULL);
}

TEST(rttest, StrLen)
{
    T_CASE("string arguments are ordinary pointers");
    T_EQ(CALL(rttest, StrLen, "hello", u"wide"), 5 + 400);
    T_EQ(CALL(rttest, StrLen, NULL, NULL), 0);

    T_CASE("the call trace prints them");
    rt_test_trace(true);
    T_EQ(CALL(rttest, StrLen, "say \"hi\"\n", u"w\u00e9"), 9 + 200);
    rt_test_trace(false);
    T_TRUE(T_LOGGED(RT_CH_TRACE, "1004:Call rttest.StrLen("));
    T_TRUE(T_LOGGED(RT_CH_TRACE, " \"say \\\"hi\\\"\\n\","));
    T_TRUE(T_LOGGED(RT_CH_TRACE, " L\"w\\xe9\") ret=rttest.dll!rt_test_return"));
    T_TRUE(T_LOGGED(RT_CH_TRACE, "1004:Ret  rttest.StrLen() retval=000000d1 ret=rttest.dll!rt_test_return"));
    CALL(rttest, StrLen, "not traced", NULL);
    T_TRUE(!T_LOGGED(RT_CH_TRACE, "not traced"));
}

TEST(rttest, Partial)
{
    T_CASE("mode 0 succeeds");
    rt_set_last_error(T_CTX(), 0);
    T_EQ(CALL(rttest, Partial, 0), TRUE);
    T_EQ(T_LAST_ERROR(), 0);                        /* success leaves last error alone */

    T_CASE("mode 1 fails with ERROR_INVALID_PARAMETER");
    T_EQ(CALL(rttest, Partial, 1), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    /* The case the shim does not handle is a fatal report, never a default (section 9.2):
     * it names the import, the case with the offending value, the source line, the guest
     * caller, the thread and the argument registers. */
    T_TRAPS(CALL(rttest, Partial, 2), "FATAL unimplemented case in rttest.dll!Partial: Partial with mode above 1: 2");
    T_TRAPS(CALL(rttest, Partial, 2), "at runtime/tests/abi/rttest/Partial.c:");
    T_TRAPS(CALL(rttest, Partial, 2), "called from rttest.dll!rt_test_return   thread 0x1004");
    T_TRAPS(CALL(rttest, Partial, 2), "in shim rttest.dll!Partial, called from rttest.dll!rt_test_return");
    T_TRAPS(CALL(rttest, Partial, 2), "rcx=");
}

TEST(rttest, TrapOnly)
{
    T_TRAPS(CALL(rttest, TrapOnly, (HANDLE)(uintptr_t)0x44), "FATAL unimplemented case in rttest.dll!TrapOnly: TrapOnly(0x44)");
}
