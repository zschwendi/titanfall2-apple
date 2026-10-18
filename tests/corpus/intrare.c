/* Integer instructions the compiler rarely emits on its own: rotates through carry, double
   shifts, bit strings, string moves with rep prefixes, loops, enter/leave, flag transfers,
   lock-prefixed read-modify-writes, and MXCSR. Each function is one small probe. */
#include "corpus.h"

/* ---- rotate through carry ---- */

EXPORT u32 ir_rcl1(u32 x, u32 cf) {
    u32 r = x;
    __asm__("bt $0, %1\n\trcl $1, %0" : "+r"(r) : "r"(cf) : "cc");
    return r;
}

EXPORT u8 ir_rcl8_imm(u8 x, u32 cf) {
    u8 r = x;
    __asm__("bt $0, %1\n\trcl $3, %0" : "+r"(r) : "r"(cf) : "cc");
    return r;
}

EXPORT u32 ir_rcl32_cl(u32 x, u32 n, u32 cf) {
    u32 r = x;
    __asm__("bt $0, %2\n\trcl %b1, %0" : "+r"(r) : "c"(n & 31), "r"(cf) : "cc");
    return r;
}

EXPORT u16 ir_rcr16_1(u16 x, u32 cf) {
    u16 r = x;
    __asm__("bt $0, %1\n\trcr $1, %0" : "+r"(r) : "r"(cf) : "cc");
    return r;
}

EXPORT u64 ir_rcr64_cl(u64 x, u32 n, u32 cf) {
    u64 r = x;
    __asm__("bt $0, %2\n\trcr %b1, %0" : "+r"(r) : "c"(n & 63), "r"(cf) : "cc");
    return r;
}

EXPORT u32 ir_rcr32_mem(u32 *p, u32 n, u32 cf) {
    __asm__("bt $0, %2\n\trcrl %%cl, %0" : "+m"(*p) : "c"(n & 31), "r"(cf) : "cc", "memory");
    return *p;
}

/* ---- flag transfers ---- */

EXPORT u32 ir_lahf(u32 a, u32 b) {
    u8 x = (u8)a, y = (u8)b, flags;
    __asm__("addb %2, %1\n\tlahf\n\tmovb %%ah, %0" : "=q"(flags), "+q"(x) : "q"(y) : "cc", "rax");
    return (u32)flags << 8 | x;
}

EXPORT u32 ir_sahf(u32 ax, u32 b) {
    u32 r;
    __asm__("sahf\n\tsetc %b0\n\tmovzbl %b0, %k0" : "=q"(r) : "a"(ax & 0xFF00), "0"(b) : "cc");
    return r;
}

/* Only the arithmetic flags (CF PF AF ZF SF OF) are ever loaded by popfq, so TF and DF
   cannot be set and trap or reverse later string code. */
EXPORT u64 ir_pushfq_popfq(u64 x, u64 y) {
    u64 out;
    __asm__("addq %2, %1\n\tpushfq\n\tpopq %0" : "=r"(out), "+r"(x) : "r"(y) : "cc", "memory");
    return (out & 0x8D5) ^ x;
}

EXPORT u64 ir_popfq_reload(u64 flags) {
    u64 out;
    __asm__("pushq %1\n\tpopfq\n\tpushfq\n\tpopq %0" : "=r"(out) : "r"(flags & 0x8D5) : "cc", "memory");
    return out & 0x8D5;
}

/* ---- table translation ---- */

EXPORT u8 ir_xlatb(const u8 *tbl, u32 idx) {
    u8 r;
    __asm__("xlatb" : "=a"(r) : "a"(idx & 0xFF), "b"(tbl) : "cc", "memory");
    return r;
}

EXPORT u8 ir_xlatb_mem(const u8 *tbl, u32 idx) {
    u8 r;
    __asm__("movb %b2, %%al\n\txlatb" : "=a"(r) : "b"(tbl), "r"(idx) : "cc", "memory");
    return r;
}

/* ---- byte swap moves ---- */

EXPORT u32 ir_movbe32(const u32 *p) {
    u32 r;
    __asm__("movbe (%1), %0" : "=r"(r) : "r"(p) : "memory");
    return r;
}

EXPORT void ir_movbe32_store(u32 *p, u32 v) {
    __asm__("movbe %1, (%0)" : : "r"(p), "r"(v) : "memory");
}

EXPORT u64 ir_movbe64(const u64 *p) {
    u64 r;
    __asm__("movbe (%1), %0" : "=r"(r) : "r"(p) : "memory");
    return r;
}

EXPORT u16 ir_movbe16(const u16 *p) {
    u16 r;
    __asm__("movbe (%1), %w0" : "=r"(r) : "r"(p) : "memory");
    return r;
}

/* ---- andn (BMI1) ---- */

EXPORT u32 ir_andn32(u32 a, u32 b) {
    u32 r;
    __asm__("andn %2, %1, %0" : "=r"(r) : "r"(a), "r"(b) : "cc");
    return r;
}

EXPORT u64 ir_andn64_mem(u64 a, const u64 *p) {
    u64 r;
    __asm__("andn %2, %1, %0" : "=r"(r) : "r"(a), "m"(*p) : "cc");
    return r;
}

/* ---- double-precision shifts ---- */

EXPORT u32 ir_shld32_imm(u32 a, u32 b) {
    u32 r = a;
    __asm__("shld $5, %1, %0" : "+r"(r) : "r"(b) : "cc");
    return r;
}

EXPORT u64 ir_shld64_cl(u64 a, u64 b, u32 n) {
    u64 r = a;
    __asm__("shld %%cl, %1, %0" : "+r"(r) : "r"(b), "c"(n & 63) : "cc");
    return r;
}

EXPORT u16 ir_shrd16_imm(u16 a, u16 b) {
    u16 r = a;
    __asm__("shrd $3, %1, %0" : "+r"(r) : "r"(b) : "cc");
    return r;
}

EXPORT u32 ir_shrd32_cl(u32 a, u32 b, u32 n) {
    u32 r = a;
    __asm__("shrd %%cl, %1, %0" : "+r"(r) : "r"(b), "c"(n & 31) : "cc");
    return r;
}

EXPORT void ir_shld32_mem(u32 *p, u32 b, u32 n) {
    __asm__("shld %%cl, %1, %0" : "+m"(*p) : "r"(b), "c"(n & 31) : "cc", "memory");
}

EXPORT void ir_shrd64_mem(u64 *p, u64 b) {
    __asm__("shrd $17, %1, %0" : "+m"(*p) : "r"(b) : "cc", "memory");
}

/* ---- bit scans ---- */

EXPORT u32 ir_bsf32(u32 x) {
    u32 r;
    __asm__("bsfl %1, %0" : "=r"(r) : "r"(x | 0x100u) : "cc");
    return r;
}

EXPORT u64 ir_bsr64(u64 x) {
    u64 r;
    __asm__("bsrq %1, %0" : "=r"(r) : "r"(x | 1) : "cc");
    return r;
}

/* bsf/bsr leave the destination undefined for a zero source, so the source is made nonzero first. */
EXPORT u32 ir_bsf_mem(u32 *p) {
    u32 r;
    if (!*p) *p = 0x40u;
    __asm__("bsfl (%1), %0" : "=r"(r) : "r"(p) : "cc", "memory");
    return r;
}

EXPORT u16 ir_bsr16(u16 x) {
    u16 r, src = x | 1;
    __asm__("bsrw %1, %0" : "=r"(r) : "r"(src) : "cc");
    return r;
}

/* ---- bit test family ---- */

EXPORT u32 ir_bt32_reg(u32 x, u32 bit) {
    u32 cf;
    __asm__("btl %2, %1\n\tsetc %b0\n\tmovzbl %b0, %k0" : "=q"(cf) : "r"(x), "r"(bit) : "cc");
    return cf;
}

/* Register bit offset into a memory bit string: offset is reduced to 128 bits (16 bytes). */
EXPORT u32 ir_bt_mem_far(const u32 *bits, u32 bit) {
    u32 cf;
    __asm__("btl %2, %1\n\tsetc %b0\n\tmovzbl %b0, %k0" : "=q"(cf) : "m"(*bits), "r"(bit & 127) : "cc");
    return cf;
}

EXPORT u32 ir_bts_reg(u32 x, u32 bit) {
    u32 r = x;
    __asm__("btsl %1, %0" : "+r"(r) : "r"(bit & 31) : "cc");
    return r;
}

EXPORT u64 ir_btr64_reg(u64 x, u64 bit) {
    u64 r = x;
    __asm__("btrq %1, %0" : "+r"(r) : "r"(bit & 63) : "cc");
    return r;
}

EXPORT u32 ir_btc_imm(u32 x) {
    u32 r = x;
    __asm__("btcl $31, %0" : "+r"(r) : : "cc");
    return r;
}

/* Bit string in memory: the offset can reach past the first dword. */
EXPORT u32 ir_bts_mem(u32 *bits, u32 bit) {
    u32 cf;
    __asm__("btsl %2, %1\n\tsetc %b0\n\tmovzbl %b0, %k0" : "=q"(cf), "+m"(*bits) : "r"(bit & 127) : "cc", "memory");
    return cf;
}

EXPORT u32 ir_btr_mem(u32 *bits, u32 bit) {
    u32 cf;
    __asm__("btrl %2, %1\n\tsetc %b0\n\tmovzbl %b0, %k0" : "=q"(cf), "+m"(*bits) : "r"(bit & 127) : "cc", "memory");
    return cf;
}

EXPORT u32 ir_btc_mem(u32 *bits, u32 bit) {
    u32 cf;
    __asm__("btcl %2, %1\n\tsetc %b0\n\tmovzbl %b0, %k0" : "=q"(cf), "+m"(*bits) : "r"(bit & 127) : "cc", "memory");
    return cf;
}

EXPORT u32 ir_bts_mem_imm(u32 *bits) {
    u32 cf;
    __asm__("btsl $40, %1\n\tsetc %b0\n\tmovzbl %b0, %k0" : "=q"(cf), "+m"(*bits) : : "cc", "memory");
    return cf;
}

/* ---- lock-prefixed read-modify-write on memory ---- */

EXPORT void ir_lock_add(u32 *p, u32 v) { __asm__ volatile("lock addl %1, %0" : "+m"(*p) : "r"(v) : "cc", "memory"); }
EXPORT void ir_lock_sub(u32 *p, u32 v) { __asm__ volatile("lock subl %1, %0" : "+m"(*p) : "r"(v) : "cc", "memory"); }
EXPORT void ir_lock_and(u32 *p, u32 v) { __asm__ volatile("lock andl %1, %0" : "+m"(*p) : "r"(v) : "cc", "memory"); }
EXPORT void ir_lock_or(u32 *p, u32 v)  { __asm__ volatile("lock orl %1, %0" : "+m"(*p) : "r"(v) : "cc", "memory"); }
EXPORT void ir_lock_xor(u32 *p, u32 v) { __asm__ volatile("lock xorl %1, %0" : "+m"(*p) : "r"(v) : "cc", "memory"); }
EXPORT void ir_lock_inc(u32 *p) { __asm__ volatile("lock incl %0" : "+m"(*p) : : "cc", "memory"); }
EXPORT void ir_lock_dec(u64 *p) { __asm__ volatile("lock decq %0" : "+m"(*p) : : "cc", "memory"); }
EXPORT void ir_lock_not(u16 *p) { __asm__ volatile("lock notw %0" : "+m"(*p) : : "memory"); }
EXPORT void ir_lock_addb(u8 *p, u8 v) { __asm__ volatile("lock addb %1, %0" : "+m"(*p) : "q"(v) : "cc", "memory"); }

EXPORT u32 ir_lock_bts(u32 *p, u32 bit) {
    u32 cf;
    __asm__ volatile("lock btsl %2, %1\n\tsetc %b0\n\tmovzbl %b0, %k0" : "=q"(cf), "+m"(*p) : "r"(bit & 31) : "cc", "memory");
    return cf;
}

EXPORT u32 ir_lock_btr(u32 *p, u32 bit) {
    u32 cf;
    __asm__ volatile("lock btrl %2, %1\n\tsetc %b0\n\tmovzbl %b0, %k0" : "=q"(cf), "+m"(*p) : "r"(bit & 31) : "cc", "memory");
    return cf;
}

EXPORT u32 ir_lock_btc(u32 *p, u32 bit) {
    u32 cf;
    __asm__ volatile("lock btcl %2, %1\n\tsetc %b0\n\tmovzbl %b0, %k0" : "=q"(cf), "+m"(*p) : "r"(bit & 31) : "cc", "memory");
    return cf;
}

/* ---- xadd and cmpxchg ---- */

EXPORT u32 ir_xadd32(u32 *p, u32 v) {
    __asm__ volatile("lock xaddl %1, %0" : "+m"(*p), "+r"(v) : : "cc", "memory");
    return v;
}

EXPORT u8 ir_xadd8(u8 *p, u8 v) {
    __asm__ volatile("lock xaddb %1, %0" : "+m"(*p), "+q"(v) : : "cc", "memory");
    return v;
}

EXPORT u64 ir_cmpxchg64(u64 *p, u64 expect, u64 want) {
    u64 old = expect;
    __asm__ volatile("lock cmpxchgq %2, %1" : "+a"(old), "+m"(*p) : "r"(want) : "cc", "memory");
    return old;
}

EXPORT u32 ir_cmpxchg32_flag(u32 *p, u32 expect, u32 want) {
    u32 old = expect, ok;
    __asm__ volatile("lock cmpxchgl %3, %1\n\tsete %b2\n\tmovzbl %b2, %k2" : "+a"(old), "+m"(*p), "=q"(ok) : "r"(want) : "cc", "memory");
    return old ^ (ok << 31);
}

EXPORT u16 ir_cmpxchg16(u16 *p, u16 expect, u16 want) {
    u16 old = expect;
    __asm__ volatile("lock cmpxchgw %2, %1" : "+a"(old), "+m"(*p) : "r"(want) : "cc", "memory");
    return old;
}

/* ---- mul, div, sign extension ---- */

EXPORT u64 ir_mul32_wide(u32 a, u32 b) {
    u32 lo, hi;
    __asm__("mull %3" : "=a"(lo), "=d"(hi) : "a"(a), "r"(b) : "cc");
    return (u64)hi << 32 | lo;
}

EXPORT i64 ir_imul64_1op(i64 a, i64 b, i64 *hi) {
    i64 lo, h;
    __asm__("imulq %3" : "=a"(lo), "=d"(h) : "a"(a), "r"(b) : "cc");
    *hi = h;
    return lo;
}

/* Divisors are forced odd so never zero; the high dividend word is reduced below the divisor
   so the quotient always fits and the divide cannot trap. */
EXPORT u32 ir_div32(u64 n, u32 d, u32 *rem) {
    u32 dd = d | 1, hi = (u32)(n >> 32) % dd, lo = (u32)n, q, r;
    __asm__("divl %4" : "=a"(q), "=d"(r) : "a"(lo), "d"(hi), "r"(dd) : "cc");
    *rem = r;
    return q;
}

EXPORT i32 ir_idiv32(i64 n, i32 d, i32 *rem) {
    i32 dd = d | 1, q, r;
    i64 nn = (i32)n;
    u32 lo, hi;
    if (dd == -1) dd = 3;
    lo = (u32)nn;
    hi = (u32)((u64)nn >> 32);
    __asm__("idivl %4" : "=a"(q), "=d"(r) : "a"(lo), "d"(hi), "r"(dd) : "cc");
    *rem = r;
    return q;
}

EXPORT u32 ir_div16(u32 n, u16 d) {
    u16 dd = d | 1, hi = (u16)((n >> 16) % dd), lo = (u16)n, q, r;
    __asm__("divw %4" : "=a"(q), "=d"(r) : "a"(lo), "d"(hi), "r"(dd) : "cc");
    return (u32)r << 16 | q;
}

EXPORT i8 ir_idiv8(i8 n, i8 d) {
    i8 dd = d | 1;
    i16 r = (i8)n;
    if (dd == -1) dd = 3;
    __asm__("idivb %1" : "+a"(r) : "q"(dd) : "cc");
    return (i8)r;
}

EXPORT i32 ir_imul3(i32 a) { return a * 1000003; }
EXPORT i32 ir_imul_mem(const i32 *p) { return p[0] * p[1]; }
EXPORT i16 ir_imul16_imm(i16 a) { i16 r; __asm__("imulw $-7, %1, %0" : "=r"(r) : "r"(a) : "cc"); return r; }
/* Two-operand form. A three-operand spelling without an immediate assembles to an APX (EVEX)
   encoding that no SSE2-only CPU or the lifter's decoder accepts, so it is not used here. */
EXPORT i64 ir_imul64_mem(i64 a, const i64 *p) { i64 r = a; __asm__("imulq (%1), %0" : "+r"(r) : "r"(p) : "cc", "memory"); return r; }

EXPORT u32 ir_cbw(u32 x) { u32 r; __asm__("cbw" : "=a"(r) : "0"(x) : "cc"); return r; }
EXPORT u32 ir_cwde(u32 x) { u32 r; __asm__("cwde" : "=a"(r) : "0"(x) : "cc"); return r; }
EXPORT u64 ir_cdqe(u64 x) { u64 r; __asm__("cdqe" : "=a"(r) : "0"(x) : "cc"); return r; }
EXPORT u32 ir_cwd(u32 x) { u32 r; __asm__("cwd" : "=d"(r) : "a"(x) : "cc"); return r; }
EXPORT u32 ir_cdq(u32 x) { u32 r; __asm__("cdq" : "=d"(r) : "a"(x) : "cc"); return r; }
EXPORT u64 ir_cqo(u64 x) { u64 r; __asm__("cqo" : "=d"(r) : "a"(x) : "cc"); return r; }

/* ---- loops and jrcxz ---- */

/* rcx is the count (at most 16 here), so each loop runs a bounded number of times. */
EXPORT u32 ir_loop(u32 n, u32 acc) {
    u64 cnt = (n & 15) + 1, a = acc;
    __asm__("1:\n\tadd %1, %0\n\tloop 1b" : "+r"(a), "+c"(cnt) : : "cc");
    return (u32)a;
}

EXPORT u32 ir_loope(u32 n, u32 acc) {
    u64 cnt = (n & 15) + 1, a = acc;
    __asm__("1:\n\tsub $1, %0\n\tloope 1b" : "+r"(a), "+c"(cnt) : : "cc");
    return (u32)a;
}

EXPORT u32 ir_loopne(u32 n, u32 acc) {
    u64 cnt = (n & 15) + 1, a = acc;
    __asm__("1:\n\tadd $3, %0\n\tloopne 1b" : "+r"(a), "+c"(cnt) : : "cc");
    return (u32)a;
}

EXPORT u32 ir_jrcxz(u32 n) {
    u64 cnt = n & 3;
    u32 r = 7;
    __asm__("jrcxz 1f\n\tmovl $99, %0\n\t1:" : "+r"(r), "+c"(cnt) : : "cc");
    return r;
}

/* ---- enter and leave ---- */

/* enter pushes rbp and leave pops it, so rbp and rsp are balanced and rbp is not clobbered. */
EXPORT u64 ir_enter(u64 x) {
    u64 r = x;
    __asm__("enter $16, $0\n\tmovq %%rbp, %0\n\tleave" : "+r"(r) : : "memory", "cc");
    return r;
}

EXPORT u64 ir_leave_frame(u64 x) {
    u64 r;
    __asm__("pushq %%rbp\n\tmovq %%rsp, %%rbp\n\tsubq $32, %%rsp\n\tmovq %1, -8(%%rbp)\n\tmovq -8(%%rbp), %0\n\tleave"
            : "=r"(r) : "r"(x) : "memory", "cc");
    return r;
}

/* ---- string instructions ---- */

EXPORT void ir_movsb(u8 *d, const u8 *s, u32 n) {
    n &= 63;
    __asm__ volatile("cld\n\trep movsb" : "+D"(d), "+S"(s), "+c"(n) : : "cc", "memory");
}

EXPORT void ir_movsw(u16 *d, const u16 *s, u32 n) {
    n &= 31;
    __asm__ volatile("cld\n\trep movsw" : "+D"(d), "+S"(s), "+c"(n) : : "cc", "memory");
}

EXPORT void ir_movsd(u32 *d, const u32 *s, u32 n) {
    n &= 31;
    __asm__ volatile("cld\n\trep movsd" : "+D"(d), "+S"(s), "+c"(n) : : "cc", "memory");
}

EXPORT void ir_movsq(u64 *d, const u64 *s, u32 n) {
    n &= 15;
    __asm__ volatile("cld\n\trep movsq" : "+D"(d), "+S"(s), "+c"(n) : : "cc", "memory");
}

EXPORT void ir_movsb_back(u8 *d, const u8 *s, u32 n) {
    n &= 31;
    __asm__ volatile("std\n\trep movsb\n\tcld" : "+D"(d), "+S"(s), "+c"(n) : : "cc", "memory");
}

EXPORT void ir_movsq_once(u64 *d, const u64 *s) {
    __asm__ volatile("cld\n\tmovsq" : "+D"(d), "+S"(s) : : "cc", "memory");
}

EXPORT void ir_stosb(u8 *d, u8 v, u32 n) {
    n &= 63;
    __asm__ volatile("cld\n\trep stosb" : "+D"(d), "+c"(n) : "a"(v) : "cc", "memory");
}

EXPORT void ir_stosw(u16 *d, u16 v, u32 n) {
    n &= 31;
    __asm__ volatile("cld\n\trep stosw" : "+D"(d), "+c"(n) : "a"(v) : "cc", "memory");
}

EXPORT void ir_stosd(u32 *d, u32 v, u32 n) {
    n &= 31;
    __asm__ volatile("cld\n\trep stosl" : "+D"(d), "+c"(n) : "a"(v) : "cc", "memory");
}

EXPORT void ir_stosq(u64 *d, u64 v, u32 n) {
    n &= 15;
    __asm__ volatile("cld\n\trep stosq" : "+D"(d), "+c"(n) : "a"(v) : "cc", "memory");
}

EXPORT void ir_stosb_back(u8 *d, u8 v, u32 n) {
    n &= 31;
    __asm__ volatile("std\n\trep stosb\n\tcld" : "+D"(d), "+c"(n) : "a"(v) : "cc", "memory");
}

EXPORT u32 ir_cmpsb(const u8 *a, const u8 *b, u32 n) {
    n &= 63;
    __asm__ volatile("cld\n\trepe cmpsb" : "+S"(a), "+D"(b), "+c"(n) : : "cc", "memory");
    return n;
}

EXPORT u32 ir_cmpsb_ne(const u8 *a, const u8 *b, u32 n) {
    n &= 63;
    __asm__ volatile("cld\n\trepne cmpsb" : "+S"(a), "+D"(b), "+c"(n) : : "cc", "memory");
    return n;
}

EXPORT u32 ir_scasb(const u8 *p, u32 n, u32 needle) {
    u32 left = n & 63;
    __asm__ volatile("cld\n\trepne scasb" : "+D"(p), "+c"(left) : "a"(needle & 0xFF) : "cc", "memory");
    return left;
}

EXPORT u32 ir_scasw(const u16 *p, u32 n, u32 needle) {
    u32 left = n & 31;
    __asm__ volatile("cld\n\trepe scasw" : "+D"(p), "+c"(left) : "a"(needle & 0xFFFF) : "cc", "memory");
    return left;
}

EXPORT u32 ir_scasb_once(const u8 *p, u32 needle) {
    u32 r;
    __asm__ volatile("cld\n\tscasb" : "+D"(p), "=@ccz"(r) : "a"(needle & 0xFF) : "cc", "memory");
    return r;
}

EXPORT u8 ir_lodsb(const u8 *p, u32 k) {
    u8 r;
    __asm__ volatile("cld\n\tlodsb" : "+S"(p), "=a"(r) : : "cc", "memory");
    return r ^ (u8)k;
}

EXPORT u32 ir_lodsb_loop(const u8 *p, u32 n) {
    u32 acc = 0;
    n &= 15;
    for (u32 i = 0; i < n; i++) {
        u8 r;
        __asm__ volatile("cld\n\tlodsb" : "+S"(p), "=a"(r) : : "cc", "memory");
        acc = acc * 31u + r;
    }
    return acc;
}

/* ---- MXCSR ---- */

EXPORT u32 ir_stmxcsr(u32 x) {
    u32 saved;
    __asm__ volatile("stmxcsr %0" : "=m"(saved));
    return saved ^ x;
}

EXPORT u32 ir_mxcsr_round_restore(u32 x) {
    u32 saved, mx = 0x1F80 | ((x & 3) << 13);
    __asm__ volatile("stmxcsr %0" : "=m"(saved));
    __asm__ volatile("ldmxcsr %0" : : "m"(mx) : "memory");
    __asm__ volatile("ldmxcsr %0" : : "m"(saved) : "memory");
    return saved;
}

EXPORT u32 ir_mxcsr_default(u32 x) {
    u32 saved, dflt = 0x1F80;
    __asm__ volatile("stmxcsr %0" : "=m"(saved));
    __asm__ volatile("ldmxcsr %0" : : "m"(dflt) : "memory");
    __asm__ volatile("ldmxcsr %0" : : "m"(saved) : "memory");
    return saved ^ (x & 0);
}

/* ---- adc and sbb: the carry comes in through CF set by bt ---- */

EXPORT u32 ir_adc32_reg(u32 a, u32 b, u32 cf) {
    u32 r = a;
    __asm__("bt $0, %2\n\tadcl %1, %0" : "+r"(r) : "r"(b), "r"(cf) : "cc");
    return r;
}

EXPORT u64 ir_adc64_imm(u64 a, u32 cf) {
    u64 r = a;
    __asm__("bt $0, %1\n\tadcq $-9, %0" : "+r"(r) : "r"(cf) : "cc");
    return r;
}

EXPORT void ir_adc64_mem(u64 *p, u64 b, u32 cf) {
    __asm__("bt $0, %2\n\tadcq %1, %0" : "+m"(*p) : "r"(b), "r"(cf) : "cc", "memory");
}

EXPORT u8 ir_adc8(u8 a, u8 b, u32 cf) {
    u8 r = a;
    __asm__("bt $0, %2\n\tadcb %1, %0" : "+q"(r) : "q"(b), "r"(cf) : "cc");
    return r;
}

EXPORT u32 ir_sbb32_reg(u32 a, u32 b, u32 cf) {
    u32 r = a;
    __asm__("bt $0, %2\n\tsbbl %1, %0" : "+r"(r) : "r"(b), "r"(cf) : "cc");
    return r;
}

EXPORT u16 ir_sbb16_imm(u16 a, u32 cf) {
    u16 r = a;
    __asm__("bt $0, %1\n\tsbbw $-5, %0" : "+r"(r) : "r"(cf) : "cc");
    return r;
}

EXPORT void ir_sbb32_mem(u32 *p, u32 b, u32 cf) {
    __asm__("bt $0, %2\n\tsbbl %1, %0" : "+m"(*p) : "r"(b), "r"(cf) : "cc", "memory");
}

EXPORT u64 ir_sbb64_reg(u64 a, u64 b, u32 cf) {
    u64 r = a;
    __asm__("bt $0, %2\n\tsbbq %1, %0" : "+r"(r) : "r"(b), "r"(cf) : "cc");
    return r;
}

/* ---- mul and div at the remaining widths ---- */

EXPORT u16 ir_mul8(u8 a, u8 b) {
    u16 r;
    __asm__("mulb %2" : "=a"(r) : "a"(a), "q"(b) : "cc");
    return r;
}

EXPORT u64 ir_mul64_wide(u64 a, u64 b, u64 *hi) {
    u64 lo, h;
    __asm__("mulq %3" : "=a"(lo), "=d"(h) : "a"(a), "r"(b) : "cc");
    *hi = h;
    return lo;
}

EXPORT u64 ir_div64(u64 hi, u64 lo, u64 d, u64 *rem) {
    u64 dd = d | 1, h = hi % dd, q, r;
    __asm__("divq %4" : "=a"(q), "=d"(r) : "a"(lo), "d"(h), "r"(dd) : "cc");
    *rem = r;
    return q;
}

EXPORT i64 ir_idiv64(i64 n, i64 d, i64 *rem) {
    i64 dd = d | 1, q, r;
    if (dd == -1) dd = 3;
    __asm__("idivq %4" : "=a"(q), "=d"(r) : "a"(n), "d"(n >> 63), "r"(dd) : "cc");
    *rem = r;
    return q;
}

/* ---- cmpxchg16b: the operand must be 16-byte aligned, so the direct form checks first ---- */

struct ir_pair16 { u64 w[2]; };

EXPORT u32 ir_cmpxchg16b_direct(u64 *p, u64 *expect, u64 lo, u64 hi) {
    struct ir_pair16 *m = (struct ir_pair16 *)p;
    u64 e0 = expect[0], e1 = expect[1];
    u32 ok;
    if ((usize)p & 15) return 2;
    __asm__ volatile("lock cmpxchg16b %1\n\tsete %b2\n\tmovzbl %b2, %k2"
                     : "+a"(e0), "+m"(*m), "=&q"(ok), "+d"(e1) : "b"(lo), "c"(hi) : "cc", "memory");
    expect[0] = e0;
    expect[1] = e1;
    return ok;
}

EXPORT u32 ir_cmpxchg16b_copy(u64 *p, u64 *expect, u64 lo, u64 hi) {
    __attribute__((aligned(16))) struct ir_pair16 cell;
    u64 e0 = expect[0], e1 = expect[1];
    u32 ok;
    cell.w[0] = p[0];
    cell.w[1] = p[1];
    __asm__ volatile("lock cmpxchg16b %1\n\tsete %b2\n\tmovzbl %b2, %k2"
                     : "+a"(e0), "+m"(cell), "=&q"(ok), "+d"(e1) : "b"(lo), "c"(hi) : "cc", "memory");
    p[0] = cell.w[0];
    p[1] = cell.w[1];
    expect[0] = e0;
    expect[1] = e1;
    return ok;
}
