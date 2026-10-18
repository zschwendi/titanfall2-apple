/* Portable C reference for tests/corpus/intrare.c, the native side of the "intrare" benchmark module.
 *
 * intrare.c is x86 inline assembly from top to bottom (each exported function is a C wrapper
 * around one or two instructions the compiler rarely emits), so it cannot be built for arm64.
 * This file defines every function it exports, with the same name, parameters and result type,
 * in plain C written from the definition of each instruction (Intel SDM, Operation sections),
 * not from the lifter's code.  The benchmark driver runs it as the correctness oracle for the
 * translated x86 code, so it has to match the x86 function bit for bit: the result, and every
 * byte written through a pointer, as seen through the wrapper (its masking and fix-ups of the
 * inputs included).
 *
 * Notes on what "exactly" means here:
 *   - Where the wrapper returns a register of which the instruction only wrote the low part
 *     (ir_cwd: cwd writes dx, the wrapper returns edx), the other bits are whatever the caller
 *     left in the register.  The reference returns zero there; the table declares the narrower
 *     result type so that only the defined bits are compared.
 *   - ir_enter returns a stack address and cannot be reproduced; it returns 0 and has no row.
 *   - The MXCSR functions behave as if stmxcsr returned the reset value 0x1F80; they do not
 *     touch the host's floating-point control register.
 *   - Lock-prefixed read-modify-writes, xadd and cmpxchg use __atomic builtins with
 *     __ATOMIC_SEQ_CST, so that the native side does a real atomic operation too.
 *   - The string instructions run with DF clear (cld) except the *_back functions, which set
 *     DF: their pointers walk downward from the given addresses.
 *
 * Nothing is included (the prefix header is force-included and the build is -ffreestanding).
 * The 128-bit division that div64 needs is done by hand (divlu below) because the compiler
 * would call __udivti3 for unsigned __int128 division.
 */

typedef unsigned __int128 u128;
typedef __int128 i128;

/* ---- shared helpers ---------------------------------------------------------------------- */

/* Rotate a `bits`-wide value (8, 16 or 32) through CF.  The 5-bit count mask applies to every
   operand size but 64; for 8 and 16 bits the masked count is then reduced modulo bits + 1,
   because the rotation is over bits + 1 positions (the value and CF).  A count of 0 leaves
   the value alone.  `left` selects rcl, otherwise rcr. */
static u32 rotc_small(u32 x, u32 count, u32 cf, u32 bits, int left) {
    u32 c = count & 31;
    u32 w = bits + 1;
    u64 vmask = (1ull << bits) - 1;
    u64 wmask = (1ull << w) - 1;
    u64 v;
    if (bits < 32) c %= w;
    v = ((u64)(cf & 1) << bits) | ((u64)x & vmask);
    if (left) v = ((v << c) | (v >> (w - c))) & wmask;
    else      v = ((v >> c) | (v << (w - c))) & wmask;
    return (u32)(v & vmask);
}

/* The 64-bit operand: 65 positions, count masked to 6 bits. */
static u64 rotc64(u64 x, u32 count, u32 cf, int left) {
    u32 c = count & 63;
    u128 wmask = (((u128)1) << 65) - 1;
    u128 v = ((u128)(cf & 1) << 64) | x;
    if (left) v = ((v << c) | (v >> (65 - c))) & wmask;
    else      v = ((v >> c) | (v << (65 - c))) & wmask;
    return (u64)v;
}

/* EFLAGS image (CF=bit 0, PF=2, AF=4, ZF=6, SF=7, OF=11) after the `bits`-wide addition a + b. */
static u32 eflags_add(u64 a, u64 b, u32 bits) {
    u64 m = bits == 64 ? ~0ull : (1ull << bits) - 1;
    u64 x = a & m, y = b & m, r = (x + y) & m;
    u32 cf = r < x;                                         /* carry out of the top bit */
    u32 pf = !__builtin_parity((u32)(r & 0xFF));            /* even parity of the low byte of the result */
    u32 af = (u32)((x ^ y ^ r) >> 4) & 1;                   /* carry out of bit 3 */
    u32 zf = r == 0;
    u32 sf = (u32)(r >> (bits - 1)) & 1;
    u32 of = (u32)(((x ^ r) & (y ^ r)) >> (bits - 1)) & 1;  /* operands of one sign, result of the other */
    return cf | pf << 2 | af << 4 | zf << 6 | sf << 7 | of << 11;
}

/* Unsigned (u1:u0) / v with u1 < v, so the quotient fits in 64 bits; stores the remainder.
   Knuth's algorithm D on 32-bit digits (Hacker's Delight, divlu). */
static u64 divlu(u64 u1, u64 u0, u64 v, u64 *rem) {
    const u64 b = 1ull << 32;
    u64 un64, un21, un10, un1, un0, vn1, vn0, q1, q0, rhat;
    int s = __builtin_clzll(v);
    v <<= s;
    vn1 = v >> 32;
    vn0 = v & 0xFFFFFFFFull;
    un64 = s ? (u1 << s) | (u0 >> (64 - s)) : u1;
    un10 = u0 << s;
    un1 = un10 >> 32;
    un0 = un10 & 0xFFFFFFFFull;
    q1 = un64 / vn1;
    rhat = un64 - q1 * vn1;
    while (q1 >= b || q1 * vn0 > b * rhat + un1) {
        q1--;
        rhat += vn1;
        if (rhat >= b) break;
    }
    un21 = un64 * b + un1 - q1 * v;
    q0 = un21 / vn1;
    rhat = un21 - q0 * vn1;
    while (q0 >= b || q0 * vn0 > b * rhat + un0) {
        q0--;
        rhat += vn1;
        if (rhat >= b) break;
    }
    *rem = (un21 * b + un0 - q0 * v) >> s;
    return q1 * b + q0;
}

/* ---- rotate through carry ---- */

EXPORT u32 ir_rcl1(u32 x, u32 cf) { return rotc_small(x, 1, cf, 32, 1); }

EXPORT u8 ir_rcl8_imm(u8 x, u32 cf) { return (u8)rotc_small(x, 3, cf, 8, 1); }

EXPORT u32 ir_rcl32_cl(u32 x, u32 n, u32 cf) { return rotc_small(x, n & 31, cf, 32, 1); }

EXPORT u16 ir_rcr16_1(u16 x, u32 cf) { return (u16)rotc_small(x, 1, cf, 16, 0); }

EXPORT u64 ir_rcr64_cl(u64 x, u32 n, u32 cf) { return rotc64(x, n & 63, cf, 0); }

EXPORT u32 ir_rcr32_mem(u32 *p, u32 n, u32 cf) {
    *p = rotc_small(*p, n & 31, cf, 32, 0);
    return *p;
}

/* ---- flag transfers ---- */

/* lahf copies EFLAGS bits 7..0 into AH: SF ZF 0 AF 0 PF 1 CF.  Bits 5 and 3 of EFLAGS read as
   zero and bit 1 as one, so AH = (flags & 0xD5) | 2. */
EXPORT u32 ir_lahf(u32 a, u32 b) {
    u32 x = (u8)a, y = (u8)b;
    u32 sum = (x + y) & 0xFF;
    u32 ah = (eflags_add(x, y, 8) & 0xD5) | 2;
    return ah << 8 | sum;
}

/* sahf loads CF from bit 0 of AH, which is bit 8 of the masked argument. */
EXPORT u32 ir_sahf(u32 ax, u32 b) {
    (void)b;
    return ((ax & 0xFF00) >> 8) & 1;
}

EXPORT u64 ir_pushfq_popfq(u64 x, u64 y) {
    u64 sum = x + y;
    return (eflags_add(x, y, 64) & 0x8D5) ^ sum;
}

/* popfq loads CF PF AF ZF SF OF, and pushfq returns them unchanged. */
EXPORT u64 ir_popfq_reload(u64 flags) { return flags & 0x8D5; }

/* ---- table translation ---- */

EXPORT u8 ir_xlatb(const u8 *tbl, u32 idx) { return tbl[idx & 0xFF]; }

EXPORT u8 ir_xlatb_mem(const u8 *tbl, u32 idx) { return tbl[idx & 0xFF]; }

/* ---- byte swap moves ---- */

EXPORT u32 ir_movbe32(const u32 *p) { return __builtin_bswap32(*p); }

EXPORT void ir_movbe32_store(u32 *p, u32 v) { *p = __builtin_bswap32(v); }

EXPORT u64 ir_movbe64(const u64 *p) { return __builtin_bswap64(*p); }

EXPORT u16 ir_movbe16(const u16 *p) { return __builtin_bswap16(*p); }

/* ---- andn (BMI1): dest = ~first & second ---- */

EXPORT u32 ir_andn32(u32 a, u32 b) { return ~a & b; }

EXPORT u64 ir_andn64_mem(u64 a, const u64 *p) { return ~a & *p; }

/* ---- double-precision shifts: a masked count of 0 leaves the destination alone ---- */

EXPORT u32 ir_shld32_imm(u32 a, u32 b) { return (a << 5) | (b >> (32 - 5)); }

EXPORT u64 ir_shld64_cl(u64 a, u64 b, u32 n) {
    n &= 63;
    return n ? (a << n) | (b >> (64 - n)) : a;
}

EXPORT u16 ir_shrd16_imm(u16 a, u16 b) { return (u16)(((u32)a >> 3) | ((u32)b << (16 - 3))); }

EXPORT u32 ir_shrd32_cl(u32 a, u32 b, u32 n) {
    n &= 31;
    return n ? (a >> n) | (b << (32 - n)) : a;
}

EXPORT void ir_shld32_mem(u32 *p, u32 b, u32 n) {
    n &= 31;
    if (n) *p = (*p << n) | (b >> (32 - n));
}

EXPORT void ir_shrd64_mem(u64 *p, u64 b) { *p = (*p >> 17) | (b << (64 - 17)); }

/* ---- bit scans: the wrappers make the source nonzero ---- */

EXPORT u32 ir_bsf32(u32 x) { return (u32)__builtin_ctz(x | 0x100u); }

EXPORT u64 ir_bsr64(u64 x) { return (u64)(63 - __builtin_clzll(x | 1)); }

EXPORT u32 ir_bsf_mem(u32 *p) {
    if (!*p) *p = 0x40u;
    return (u32)__builtin_ctz(*p);
}

EXPORT u16 ir_bsr16(u16 x) { return (u16)(31 - __builtin_clz((u32)(u16)(x | 1))); }

/* ---- bit test family ----
   Register destination: the offset is taken modulo the operand size.  Memory destination with
   a register offset: the destination is a bit string starting at the address, dword
   (offset >> 5) from it, bit offset & 31 (the wrappers reduce the offset to 0..127).  Memory
   destination with an immediate offset: modulo the operand size, like a register. */

EXPORT u32 ir_bt32_reg(u32 x, u32 bit) { return (x >> (bit & 31)) & 1; }

EXPORT u32 ir_bt_mem_far(const u32 *bits, u32 bit) {
    bit &= 127;
    return (bits[bit >> 5] >> (bit & 31)) & 1;
}

EXPORT u32 ir_bts_reg(u32 x, u32 bit) { return x | (1u << (bit & 31)); }

EXPORT u64 ir_btr64_reg(u64 x, u64 bit) { return x & ~(1ull << (bit & 63)); }

EXPORT u32 ir_btc_imm(u32 x) { return x ^ (1u << 31); }

EXPORT u32 ir_bts_mem(u32 *bits, u32 bit) {
    u32 *w;
    u32 old;
    bit &= 127;
    w = bits + (bit >> 5);
    old = (*w >> (bit & 31)) & 1;
    *w |= 1u << (bit & 31);
    return old;
}

EXPORT u32 ir_btr_mem(u32 *bits, u32 bit) {
    u32 *w;
    u32 old;
    bit &= 127;
    w = bits + (bit >> 5);
    old = (*w >> (bit & 31)) & 1;
    *w &= ~(1u << (bit & 31));
    return old;
}

EXPORT u32 ir_btc_mem(u32 *bits, u32 bit) {
    u32 *w;
    u32 old;
    bit &= 127;
    w = bits + (bit >> 5);
    old = (*w >> (bit & 31)) & 1;
    *w ^= 1u << (bit & 31);
    return old;
}

/* Immediate 40 on a dword in memory: 40 mod 32 = bit 8 of the dword at the address. */
EXPORT u32 ir_bts_mem_imm(u32 *bits) {
    u32 old = (*bits >> 8) & 1;
    *bits |= 1u << 8;
    return old;
}

/* ---- lock-prefixed read-modify-write on memory ---- */

EXPORT void ir_lock_add(u32 *p, u32 v) { __atomic_fetch_add(p, v, __ATOMIC_SEQ_CST); }
EXPORT void ir_lock_sub(u32 *p, u32 v) { __atomic_fetch_sub(p, v, __ATOMIC_SEQ_CST); }
EXPORT void ir_lock_and(u32 *p, u32 v) { __atomic_fetch_and(p, v, __ATOMIC_SEQ_CST); }
EXPORT void ir_lock_or(u32 *p, u32 v)  { __atomic_fetch_or(p, v, __ATOMIC_SEQ_CST); }
EXPORT void ir_lock_xor(u32 *p, u32 v) { __atomic_fetch_xor(p, v, __ATOMIC_SEQ_CST); }
EXPORT void ir_lock_inc(u32 *p) { __atomic_fetch_add(p, 1u, __ATOMIC_SEQ_CST); }
EXPORT void ir_lock_dec(u64 *p) { __atomic_fetch_sub(p, 1ull, __ATOMIC_SEQ_CST); }
EXPORT void ir_lock_not(u16 *p) { __atomic_fetch_xor(p, (u16)0xFFFF, __ATOMIC_SEQ_CST); }
EXPORT void ir_lock_addb(u8 *p, u8 v) { __atomic_fetch_add(p, v, __ATOMIC_SEQ_CST); }

/* The bit tests return the old value of the bit (CF). */
EXPORT u32 ir_lock_bts(u32 *p, u32 bit) {
    bit &= 31;
    return (__atomic_fetch_or(p, 1u << bit, __ATOMIC_SEQ_CST) >> bit) & 1;
}

EXPORT u32 ir_lock_btr(u32 *p, u32 bit) {
    bit &= 31;
    return (__atomic_fetch_and(p, ~(1u << bit), __ATOMIC_SEQ_CST) >> bit) & 1;
}

EXPORT u32 ir_lock_btc(u32 *p, u32 bit) {
    bit &= 31;
    return (__atomic_fetch_xor(p, 1u << bit, __ATOMIC_SEQ_CST) >> bit) & 1;
}

/* ---- xadd and cmpxchg ----
   xadd returns the old value and stores the sum.  cmpxchg compares the accumulator (expect)
   with the destination: equal, it stores the source and sets ZF; otherwise it loads the
   destination into the accumulator.  Either way the accumulator ends up holding the old value
   of the destination, which is what the builtin leaves in `expect`. */

EXPORT u32 ir_xadd32(u32 *p, u32 v) { return __atomic_fetch_add(p, v, __ATOMIC_SEQ_CST); }

EXPORT u8 ir_xadd8(u8 *p, u8 v) { return __atomic_fetch_add(p, v, __ATOMIC_SEQ_CST); }

EXPORT u64 ir_cmpxchg64(u64 *p, u64 expect, u64 want) {
    __atomic_compare_exchange_n(p, &expect, want, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return expect;
}

EXPORT u32 ir_cmpxchg32_flag(u32 *p, u32 expect, u32 want) {
    u32 ok = __atomic_compare_exchange_n(p, &expect, want, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return expect ^ (ok << 31);
}

EXPORT u16 ir_cmpxchg16(u16 *p, u16 expect, u16 want) {
    __atomic_compare_exchange_n(p, &expect, want, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return expect;
}

/* ---- mul, div, sign extension ---- */

EXPORT u64 ir_mul32_wide(u32 a, u32 b) { return (u64)a * b; }

EXPORT i64 ir_imul64_1op(i64 a, i64 b, i64 *hi) {
    i128 prod = (i128)a * b;
    *hi = (i64)(prod >> 64);
    return (i64)prod;
}

/* The divide is edx:eax / dd with the high word reduced below the divisor, so it cannot trap. */
EXPORT u32 ir_div32(u64 n, u32 d, u32 *rem) {
    u32 dd = d | 1, hi = (u32)(n >> 32) % dd, lo = (u32)n;
    u64 num = (u64)hi << 32 | lo;
    *rem = (u32)(num % dd);
    return (u32)(num / dd);
}

/* The dividend is the dword n sign-extended to 64 bits; -1 is replaced by 3 as in the wrapper. */
EXPORT i32 ir_idiv32(i64 n, i32 d, i32 *rem) {
    i32 dd = d | 1;
    i32 nn = (i32)n;
    if (dd == -1) dd = 3;
    *rem = nn % dd;
    return nn / dd;
}

EXPORT u32 ir_div16(u32 n, u16 d) {
    u32 dd = (u16)(d | 1), hi = (n >> 16) % dd, lo = n & 0xFFFF;
    u32 num = hi << 16 | lo;
    return (num % dd) << 16 | (num / dd);
}

/* idiv r/m8 divides AX by the byte: AL = quotient, AH = remainder.  The wrapper returns AL. */
EXPORT i8 ir_idiv8(i8 n, i8 d) {
    i8 dd = (i8)(d | 1);
    if (dd == -1) dd = 3;
    return (i8)((int)n / (int)dd);
}

EXPORT i32 ir_imul3(i32 a) { return (i32)((u32)a * 1000003u); }

EXPORT i32 ir_imul_mem(const i32 *p) { return (i32)((u32)p[0] * (u32)p[1]); }

EXPORT i16 ir_imul16_imm(i16 a) { return (i16)(u16)((u32)(u16)a * 0xFFF9u); }

EXPORT i64 ir_imul64_mem(i64 a, const i64 *p) { return (i64)((u64)a * (u64)*p); }

/* The upper bits of eax stay as the argument left them for cbw and of edx for cwd. */
EXPORT u32 ir_cbw(u32 x) { return (x & 0xFFFF0000u) | (u16)(i16)(i8)x; }

EXPORT u32 ir_cwde(u32 x) { return (u32)(i32)(i16)x; }

EXPORT u64 ir_cdqe(u64 x) { return (u64)(i64)(i32)x; }

/* cwd writes only dx (0xFFFF or 0); the upper half of edx is not defined by the function, so the
   table compares just 16 bits. */
EXPORT u32 ir_cwd(u32 x) { return (x & 0x8000u) ? 0xFFFFu : 0u; }

EXPORT u32 ir_cdq(u32 x) { return (x & 0x80000000u) ? 0xFFFFFFFFu : 0u; }

EXPORT u64 ir_cqo(u64 x) { return (x >> 63) ? ~0ull : 0ull; }

/* ---- loops and jrcxz ----
   `loop` decrements rcx and branches while it is not zero; loope / loopne also need ZF set /
   clear, ZF being the one the arithmetic instruction in the body left. */

EXPORT u32 ir_loop(u32 n, u32 acc) {
    u64 cnt = (n & 15) + 1, a = acc;
    do {
        a += cnt;           /* the body adds the live counter */
        cnt--;
    } while (cnt != 0);
    return (u32)a;
}

EXPORT u32 ir_loope(u32 n, u32 acc) {
    u64 cnt = (n & 15) + 1, a = acc;
    do {
        a -= 1;
        cnt--;
    } while (cnt != 0 && a == 0);
    return (u32)a;
}

EXPORT u32 ir_loopne(u32 n, u32 acc) {
    u64 cnt = (n & 15) + 1, a = acc;
    do {
        a += 3;
        cnt--;
    } while (cnt != 0 && a != 0);
    return (u32)a;
}

EXPORT u32 ir_jrcxz(u32 n) { return (n & 3) ? 99u : 7u; }

/* ---- enter and leave ---- */

/* The x86 function returns rbp after `enter`, which is an address on the guest's stack; there is
   nothing to reproduce.  Defined (so the symbol exists), returns 0, and has no row in the table. */
EXPORT u64 ir_enter(u64 x) {
    (void)x;
    return 0;
}

EXPORT u64 ir_leave_frame(u64 x) { return x; }

/* ---- string instructions ----
   rep: while the count is not zero do one step and decrement it.  repe / repne also stop after
   a step whose comparison left ZF clear / set, and the count then holds what is left. */

EXPORT void ir_movsb(u8 *d, const u8 *s, u32 n) {
    n &= 63;
    for (u32 i = 0; i < n; i++) d[i] = s[i];
}

EXPORT void ir_movsw(u16 *d, const u16 *s, u32 n) {
    n &= 31;
    for (u32 i = 0; i < n; i++) d[i] = s[i];
}

EXPORT void ir_movsd(u32 *d, const u32 *s, u32 n) {
    n &= 31;
    for (u32 i = 0; i < n; i++) d[i] = s[i];
}

EXPORT void ir_movsq(u64 *d, const u64 *s, u32 n) {
    n &= 15;
    for (u32 i = 0; i < n; i++) d[i] = s[i];
}

/* DF set: both pointers start at the given addresses and move down. */
EXPORT void ir_movsb_back(u8 *d, const u8 *s, u32 n) {
    n &= 31;
    for (u32 i = 0; i < n; i++) *(d - i) = *(s - i);
}

EXPORT void ir_movsq_once(u64 *d, const u64 *s) { *d = *s; }

EXPORT void ir_stosb(u8 *d, u8 v, u32 n) {
    n &= 63;
    for (u32 i = 0; i < n; i++) d[i] = v;
}

EXPORT void ir_stosw(u16 *d, u16 v, u32 n) {
    n &= 31;
    for (u32 i = 0; i < n; i++) d[i] = v;
}

EXPORT void ir_stosd(u32 *d, u32 v, u32 n) {
    n &= 31;
    for (u32 i = 0; i < n; i++) d[i] = v;
}

EXPORT void ir_stosq(u64 *d, u64 v, u32 n) {
    n &= 15;
    for (u32 i = 0; i < n; i++) d[i] = v;
}

EXPORT void ir_stosb_back(u8 *d, u8 v, u32 n) {
    n &= 31;
    for (u32 i = 0; i < n; i++) *(d - i) = v;
}

/* repe cmpsb: stops after the first pair that differs. */
EXPORT u32 ir_cmpsb(const u8 *a, const u8 *b, u32 n) {
    n &= 63;
    while (n) {
        u8 x = *a++, y = *b++;
        n--;
        if (x != y) break;
    }
    return n;
}

/* repne cmpsb: stops after the first pair that is equal. */
EXPORT u32 ir_cmpsb_ne(const u8 *a, const u8 *b, u32 n) {
    n &= 63;
    while (n) {
        u8 x = *a++, y = *b++;
        n--;
        if (x == y) break;
    }
    return n;
}

/* repne scasb: compares AL with each byte, stops after the first equal one. */
EXPORT u32 ir_scasb(const u8 *p, u32 n, u32 needle) {
    u32 left = n & 63;
    u8 al = (u8)needle;
    while (left) {
        u8 x = *p++;
        left--;
        if (x == al) break;
    }
    return left;
}

/* repe scasw: compares AX with each word, stops after the first different one. */
EXPORT u32 ir_scasw(const u16 *p, u32 n, u32 needle) {
    u32 left = n & 31;
    u16 ax = (u16)needle;
    while (left) {
        u16 x = *p++;
        left--;
        if (x != ax) break;
    }
    return left;
}

EXPORT u32 ir_scasb_once(const u8 *p, u32 needle) { return *p == (u8)needle; }

EXPORT u8 ir_lodsb(const u8 *p, u32 k) { return (u8)(*p ^ (u8)k); }

EXPORT u32 ir_lodsb_loop(const u8 *p, u32 n) {
    u32 acc = 0;
    n &= 15;
    for (u32 i = 0; i < n; i++) acc = acc * 31u + p[i];
    return acc;
}

/* ---- MXCSR: the guest starts at the reset value 0x1F80; the round-trips restore it ---- */

EXPORT u32 ir_stmxcsr(u32 x) { return 0x1F80u ^ x; }

EXPORT u32 ir_mxcsr_round_restore(u32 x) {
    (void)x;
    return 0x1F80u;
}

EXPORT u32 ir_mxcsr_default(u32 x) {
    (void)x;
    return 0x1F80u;
}

/* ---- adc and sbb: CF comes in from bit 0 of the cf argument ---- */

EXPORT u32 ir_adc32_reg(u32 a, u32 b, u32 cf) { return a + b + (cf & 1); }

EXPORT u64 ir_adc64_imm(u64 a, u32 cf) { return a + 0xFFFFFFFFFFFFFFF7ull + (cf & 1); }   /* imm -9 */

EXPORT void ir_adc64_mem(u64 *p, u64 b, u32 cf) { *p = *p + b + (cf & 1); }

EXPORT u8 ir_adc8(u8 a, u8 b, u32 cf) { return (u8)(a + b + (cf & 1)); }

EXPORT u32 ir_sbb32_reg(u32 a, u32 b, u32 cf) { return a - b - (cf & 1); }

EXPORT u16 ir_sbb16_imm(u16 a, u32 cf) { return (u16)(a - 0xFFFBu - (cf & 1)); }   /* imm -5 as 16 bits */

EXPORT void ir_sbb32_mem(u32 *p, u32 b, u32 cf) { *p = *p - b - (cf & 1); }

EXPORT u64 ir_sbb64_reg(u64 a, u64 b, u32 cf) { return a - b - (cf & 1); }

/* ---- mul and div at the remaining widths ---- */

EXPORT u16 ir_mul8(u8 a, u8 b) { return (u16)((u32)a * b); }   /* AX = AL * src */

EXPORT u64 ir_mul64_wide(u64 a, u64 b, u64 *hi) {
    u128 prod = (u128)a * b;
    *hi = (u64)(prod >> 64);
    return (u64)prod;
}

/* rdx:rax / dd with rdx = hi % dd, so the quotient fits. */
EXPORT u64 ir_div64(u64 hi, u64 lo, u64 d, u64 *rem) {
    u64 dd = d | 1, h = hi % dd;
    return divlu(h, lo, dd, rem);
}

EXPORT i64 ir_idiv64(i64 n, i64 d, i64 *rem) {
    i64 dd = d | 1;
    if (dd == -1) dd = 3;
    *rem = n % dd;
    return n / dd;
}

/* ---- cmpxchg16b ----
   rdx:rax (expect[1]:expect[0]) is compared with the 16 bytes at p (p[1]:p[0]); equal, rcx:rbx
   (hi:lo) is stored and ZF set, otherwise rdx:rax is loaded from memory.  The wrapper stores
   rax / rdx back to expect in both cases and returns ZF.  The 128-bit builtin compare-exchange
   compiles to a single casp, no library call. */

EXPORT u32 ir_cmpxchg16b_direct(u64 *p, u64 *expect, u64 lo, u64 hi) {
    u128 e = (u128)expect[1] << 64 | expect[0];
    u128 want = (u128)hi << 64 | lo;
    u32 ok;
    if ((usize)p & 15) return 2;                       /* the wrapper refuses a misaligned operand */
    ok = __atomic_compare_exchange_n((u128 *)p, &e, want, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    expect[0] = (u64)e;
    expect[1] = (u64)(e >> 64);
    return ok;
}

/* Works on an aligned local copy of p[0..1], copied back afterwards (as the wrapper does).  The copy is
   volatile so that the compiler keeps the real compare-exchange on it instead of folding it away. */
EXPORT u32 ir_cmpxchg16b_copy(u64 *p, u64 *expect, u64 lo, u64 hi) {
    volatile u128 cell;
    u128 e = (u128)expect[1] << 64 | expect[0];
    u128 want = (u128)hi << 64 | lo;
    u32 ok;
    u128 res;
    cell = (u128)p[1] << 64 | p[0];
    ok = __atomic_compare_exchange_n(&cell, &e, want, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    res = cell;
    p[0] = (u64)res;
    p[1] = (u64)(res >> 64);
    expect[0] = (u64)e;
    expect[1] = (u64)(e >> 64);
    return ok;
}
