// C++ object-model shapes as the Microsoft ABI lays them out: multiple and virtual inheritance,
// member pointers, abstract interfaces, templates, lambdas, operator overloads and placement
// construction. Classes sit in an anonymous namespace so their vtables cannot clash with the
// symbols of other files in the DLL. No exceptions, RTTI, heap or static constructors.
extern "C" {
#include "corpus.h"
}

// The build has no C++ headers, so the placement form of operator new is declared here.
inline void *operator new(usize, void *p) { return p; }

namespace {

// ---- multiple inheritance: two polymorphic bases, one overridden in the second ----------

struct MiA {
    u32 a0;
    MiA(u32 v) : a0(v) {}
    virtual u32 fa(u32 x) { return x * 3 + a0; }
    virtual u32 ga(u32 x) { return x - a0; }
};
struct MiB {
    u32 b0;
    MiB(u32 v) : b0(v) {}
    virtual u32 fb(u32 x) { return (x ^ b0) + 5; }
    virtual u32 gb(u32 x) { return x * b0; }
};
struct MiC : MiA, MiB {
    u32 c0;
    MiC(u32 a, u32 b, u32 c) : MiA(a), MiB(b), c0(c) {}
    u32 fa(u32 x) override { return MiA::fa(x) + c0; }
    u32 fb(u32 x) override { return MiB::fb(x) - c0; }
};
struct MiD : MiB {
    MiD(u32 v) : MiB(v) {}
    u32 fb(u32 x) override { return x + b0 * 7; }
};

// ---- virtual inheritance: the shared base is reached through a vbtable -------------------

struct VbBase {
    u32 v;
    VbBase(u32 x) : v(x) {}
    virtual u32 vv(u32 x) { return x + v; }
};
struct VbL : virtual VbBase {
    u32 l;
    VbL(u32 x, u32 y) : VbBase(x), l(y) {}
    u32 vv(u32 x) override { return x * l + v; }
};
struct VbR : virtual VbBase {
    u32 r;
    VbR(u32 x, u32 y) : VbBase(x), r(y) {}
    u32 vv(u32 x) override { return (x ^ r) + v; }
};
struct VbD : VbL, VbR {
    u32 d;
    VbD(u32 x, u32 y, u32 z) : VbBase(x), VbL(x, y), VbR(x, z), d(y + z) {}
    u32 vv(u32 x) override { return VbL::vv(x) - r; }
};

// ---- member function and member data pointers ---------------------------------------------

struct Acc {
    u32 k;
    Acc(u32 v) : k(v) {}
    u32 add(u32 x) { return x + k; }
    u32 mul(u32 x) { return x * k; }
    virtual u32 sub(u32 x) { return x - k; }
    u32 mix(u32 x) { return add(x) ^ mul(x); }
};
struct AccV : Acc {
    AccV(u32 v) : Acc(v) {}
    u32 sub(u32 x) override { return (x - k) * 2 + 1; }
};
typedef u32 (Acc::*AccOp)(u32);
const AccOp kAccOps[4] = { &Acc::add, &Acc::mul, &Acc::sub, &Acc::mix };

struct Pair2 {
    u32 p, q;
    Pair2(u32 a, u32 b) : p(a), q(b) {}
};
const u32 MiC::*kMiFields[4] = { &MiC::c0, &MiA::a0, &MiB::b0, &MiC::c0 };

// ---- abstract interfaces with several implementations -------------------------------------

struct IUnit {
    virtual u32 value(u32 x) = 0;
    virtual u32 tag() = 0;
    u32 both(u32 x) { return value(x) * 2 + tag(); }
};
struct UnitA : IUnit {
    u32 k;
    UnitA(u32 v) : k(v) {}
    u32 value(u32 x) override { return x + k; }
    u32 tag() override { return 1; }
};
struct UnitB : IUnit {
    u32 k;
    UnitB(u32 v) : k(v) {}
    u32 value(u32 x) override { return x * k; }
    u32 tag() override { return 2 + (k & 1); }
};
struct UnitC : IUnit {
    u32 k;
    UnitC(u32 v) : k(v) {}
    u32 value(u32 x) override { return (x ^ k) + tag(); }
    u32 tag() override { return 3; }
};
struct UnitD final : UnitB {
    UnitD(u32 v) : UnitB(v) {}
    u32 tag() override { return 9; }
};
NOINLINE u32 run_unit(IUnit *u, u32 x) { return u->both(x) + u->tag(); }

// ---- templates, instantiated at several types ---------------------------------------------

template <class T> T tpl_sum4(const T *p) { T s = p[0]; s += p[1] + p[2]; return s + p[3]; }
template <class T, u32 N> T tpl_dot(const T *a, const T *b) { T s = 0; for (u32 i = 0; i < N; i++) s += a[i] * b[i]; return s; }
template <class T> T tpl_clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
template <class T> struct Accum { T total; T count; void add(T x) { total += x; count += 1; } T get() const { return total + count; } };
template <class T> T tpl_fold(T seed, u32 n) { Accum<T> a = { seed, 0 }; n &= 15; for (u32 i = 0; i < n; i++) a.add((T)(i * 3 + 1)); return a.get(); }
template <class T> T tpl_apply(T (*f)(T), T v) { return f(f(v)); }
NOINLINE u32 bump_u32(u32 v) { return v * 2 + 1; }
NOINLINE double half_d(double v) { return v * 0.5 + 1.0; }

// ---- lambdas: captures reach a template, and a function pointer through a context ---------

template <class F> u32 tpl_apply_twice(F f, u32 x) { return f(f(x)); }
template <class F> u32 tpl_call_ctx(u32 x, void *ctx) { return (*static_cast<F *>(ctx))(x); }
NOINLINE u32 apply_fp(u32 (*f)(u32), u32 x) { return f(x) + f(x ^ 1); }
NOINLINE u32 apply_ctx(u32 (*f)(u32, void *), void *ctx, u32 x) { return f(x, ctx) * 3 + 1; }

// ---- operator overloads on small vector types ---------------------------------------------

struct V2 {
    u32 x, y;
    V2(u32 a, u32 b) : x(a), y(b) {}
    V2 &operator+=(V2 o) { x += o.x; y += o.y; return *this; }
    V2 operator-() const { return V2(0u - x, 0u - y); }
    u32 &operator[](u32 i) { return (i & 1) ? y : x; }
    u32 operator[](u32 i) const { return (i & 1) ? y : x; }
};
inline V2 operator+(V2 a, V2 b) { return V2(a.x + b.x, a.y + b.y); }
inline V2 operator-(V2 a, V2 b) { return V2(a.x - b.x, a.y - b.y); }
inline V2 operator*(V2 a, u32 s) { return V2(a.x * s, a.y * s); }
inline V2 operator*(u32 s, V2 a) { return a * s; }
inline u32 operator*(V2 a, V2 b) { return a.x * b.x + a.y * b.y; }
inline bool operator==(V2 a, V2 b) { return a.x == b.x && a.y == b.y; }
inline bool operator!=(V2 a, V2 b) { return !(a == b); }

struct V4 { u32 v[4]; };
inline V4 operator+(const V4 &a, const V4 &b) { V4 r; for (u32 i = 0; i < 4; i++) r.v[i] = a.v[i] + b.v[i]; return r; }
inline V4 operator^(const V4 &a, u32 m) { V4 r; for (u32 i = 0; i < 4; i++) r.v[i] = a.v[i] ^ m; return r; }
inline V4 operator*(const V4 &a, const V4 &b) { V4 r; for (u32 i = 0; i < 4; i++) r.v[i] = a.v[i] * b.v[i]; return r; }
const V4 kBias = { { 1u, 0x9E3779B9u, 3u, 0xFFFFFFFFu } };

struct F3 { float x, y, z; };
inline F3 operator*(F3 a, float s) { return F3{ a.x * s, a.y * s, a.z * s }; }
inline float operator*(F3 a, F3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// ---- placement construction and explicit destruction --------------------------------------

struct Acc3 {
    u32 total, count;
    Acc3(u32 t) : total(t), count(0) {}
    void add(u32 x) { total += x; count++; }
    ~Acc3() { count ^= 0x40u; }
};

// ---- other shapes: covariant returns, base-constructor virtual calls, copies, conversions --

struct CovBase {
    u32 id;
    CovBase(u32 v) : id(v) {}
    virtual CovBase *next() { return this; }
    virtual u32 weight() { return id; }
};
struct CovDerived : CovBase {
    u32 extra;
    CovDerived(u32 v, u32 e) : CovBase(v), extra(e) {}
    CovDerived *next() override { return this; }
    u32 weight() override { return id + extra; }
};
struct CovOther { u32 pad; CovOther(u32 p) : pad(p) {} };
struct CovMI : CovOther, CovDerived {
    CovMI(u32 v, u32 e, u32 p) : CovOther(p), CovDerived(v, e) {}
    CovMI *next() override { return this; }
    u32 weight() override { return CovDerived::weight() + pad; }
};

struct Ctor2 {
    u32 tag;
    Ctor2() : tag(0) { tag = vf(); }
    virtual u32 vf() { return 11; }
};
struct Ctor2D : Ctor2 {
    u32 extra;
    Ctor2D(u32 e) : extra(e) {}
    u32 vf() override { return 22 + extra; }
};

struct Cp {
    u32 a, b;
    Cp(u32 x, u32 y) : a(x), b(y) {}
    Cp(const Cp &o) : a(o.a + 1), b(o.b * 2) {}
    Cp &operator=(const Cp &o) { a = o.a ^ 1; b = o.b; return *this; }
};
NOINLINE u32 cp_take(Cp c, u32 k) { return c.a * k + c.b; }

struct Wrap {
    u32 v;
    Wrap(u32 x) : v(x) {}
    operator u32() const { return v * 2 + 1; }
    explicit operator bool() const { return v != 0; }
};

struct Builder {
    u32 a, b, c;
    Builder() : a(0), b(0), c(0) {}
    Builder &setA(u32 v) { a = v; return *this; }
    Builder &setB(u32 v) { b = v + a; return *this; }
    Builder &setC(u32 v) { c = v ^ b; return *this; }
    u32 done() const { return a + b + c; }
};

struct Scaler {
    u32 v;
    static u32 scale(u32 x) { return x * 3 + 1; }
    static constexpr u32 kBias = 0x1234u;
    u32 get() const { return v + kBias; }
};

} // namespace

extern "C" {

// Multiple inheritance.
EXPORT u32 cx_mi_first_base(u32 k, u32 x) {
    MiC c(k, x, k ^ x);
    MiA *pa = &c;
    return pa->fa(x) + pa->ga(k);
}
EXPORT u32 cx_mi_second_base(u32 k, u32 x) {
    MiC c(k, x, 9);
    MiB *pb = &c;
    return pb->fb(x) + pb->gb(k);
}
EXPORT u32 cx_mi_pick(u32 which, u32 k, u32 x) {
    MiC c(k, 3, x);
    MiD d(k + x);
    MiB *pb = (which & 1) ? static_cast<MiB *>(&c) : static_cast<MiB *>(&d);
    return pb->fb(x) + pb->gb(which);
}
EXPORT u32 cx_mi_downcast(u32 k, u32 x) {
    MiC c(k, x, 2);
    MiB *pb = &c;
    MiC *pc = static_cast<MiC *>(pb);
    return pc->c0 + pc->a0 + pc->fb(x);
}
EXPORT u32 cx_mi_crosscast(u32 k, u32 x) {
    MiC c(k, x, 5);
    MiC *pc = &c;
    MiB *pb = pc;
    MiA *pa = pc;
    return pb->b0 * 3 + pa->a0;
}
EXPORT u32 cx_mi_store(u32 *out, u32 k, u32 x) {
    MiC c(k, x, 1);
    MiB *pb = &c;
    MiA *pa = &c;
    out[0] = pb->fb(x);
    out[1] = pa->ga(out[0]);
    return out[0] ^ out[1];
}

// Virtual inheritance.
EXPORT u32 cx_vb_fields(u32 x, u32 y, u32 z) {
    VbD d(x, y, z);
    return d.v * 4 + d.l + d.r + d.d;
}
EXPORT u32 cx_vb_call(u32 x, u32 y) {
    VbD d(x, y, 3);
    VbBase *p = &d;
    return p->vv(y);
}
EXPORT u32 cx_vb_pick(u32 which, u32 x, u32 y) {
    VbD d(x, y, 4);
    VbL *pl = &d;
    VbR *pr = &d;
    VbBase *p = (which & 1) ? static_cast<VbBase *>(pl) : static_cast<VbBase *>(pr);
    return p->vv(which) + p->v;
}
EXPORT u32 cx_vb_write(u32 x, u32 y) {
    VbD d(x, 0, y);
    VbBase *p = &d;
    p->v = x ^ y;
    VbR *r = &d;
    r->v += 3;
    return d.v + d.r + p->vv(x);
}
EXPORT u32 cx_vb_store(u32 *out, u32 x, u32 y) {
    VbD d(x, y, 1);
    VbBase *p = &d;
    out[0] = p->vv(x);
    out[1] = d.v;
    return out[0] ^ out[1];
}

// Pointers to member functions.
EXPORT u32 cx_pmf_pick(u32 which, u32 k, u32 x) {
    u32 (Acc::*f)(u32) = (which & 1) ? &Acc::add : &Acc::mul;
    Acc a(k);
    return (a.*f)(x);
}
EXPORT u32 cx_pmf_table(u32 which, u32 k, u32 x) {
    AccV v(k);
    Acc *p = &v;
    return (p->*kAccOps[which & 3])(x);
}
EXPORT u32 cx_pmf_virtual(u32 k, u32 x) {
    AccOp f = &Acc::sub;
    AccV v(k);
    Acc *p = &v;
    return (p->*f)(x) + (v.*f)(x + 1);
}
EXPORT u32 cx_pmf_store(u32 *out, u32 k, u32 x) {
    Acc a(k);
    out[0] = (a.*kAccOps[3])(x);
    out[1] = (a.*kAccOps[x & 1])(k);
    return out[0] + out[1];
}
EXPORT u32 cx_pmf_derived(u32 which, u32 k, u32 x) {
    u32 (AccV::*f)(u32) = &Acc::mix;
    if (which & 1) f = &AccV::sub;
    AccV v(k);
    return (v.*f)(x);
}
EXPORT u32 cx_pmf_mi_base(u32 k, u32 x) {
    u32 (MiC::*g)(u32) = &MiB::gb;
    MiC c(k, 2, x);
    return (c.*g)(x);
}
EXPORT u32 cx_pmf_mi_pick(u32 which, u32 k, u32 x) {
    u32 (MiC::*f)(u32) = (which & 1) ? &MiC::fb : &MiC::fa;
    MiC c(k, x, 7);
    return (c.*f)(which);
}
EXPORT u32 cx_pmf_compare(u32 which, u32 x) {
    u32 (Acc::*f)(u32) = (which & 1) ? &Acc::add : &Acc::sub;
    return (f == &Acc::add ? 5u : 2u) + (f != &Acc::mul ? x : 0u);
}

// Pointers to data members.
EXPORT u32 cx_pdm_read(u32 which, u32 a, u32 b) {
    u32 Pair2::*f = (which & 1) ? &Pair2::p : &Pair2::q;
    Pair2 o(a, b);
    return o.*f + (which >> 1);
}
EXPORT u32 cx_pdm_write(Pair2 *o, u32 which, u32 v) {
    u32 Pair2::*f = (which & 1) ? &Pair2::p : &Pair2::q;
    o->*f = v;
    o->*f ^= 0x55u;
    return o->p + o->q;
}
EXPORT u32 cx_pdm_swap(Pair2 *o, u32 which) {
    u32 Pair2::*f = (which & 1) ? &Pair2::q : &Pair2::p;
    u32 Pair2::*g = (which & 1) ? &Pair2::p : &Pair2::q;
    u32 t = o->*f;
    o->*f = o->*g;
    o->*g = t;
    return o->p * 3 + o->q;
}
EXPORT u32 cx_pdm_table(u32 which, u32 k, u32 x) {
    MiC c(k, x ^ 1, k + 2);
    return (c.*kMiFields[which & 3]) + (c.*kMiFields[(which >> 2) & 3]);
}
EXPORT u32 cx_pdm_base(u32 k, u32 x) {
    u32 MiC::*pb = &MiB::b0;
    MiC c(k, 1, x);
    MiC *p = &c;
    return (p->*pb) + c.a0 + (p->*kMiFields[2]);
}

// Abstract interfaces.
EXPORT u32 cx_iface_pick(u32 which, u32 k, u32 x) {
    UnitA a(k);
    UnitB b(k);
    UnitC c(k);
    IUnit *tbl[3] = { &a, &b, &c };
    IUnit *u = tbl[which % 3];
    return u->both(x) + u->value(which);
}
EXPORT u32 cx_iface_all(u32 k, u32 x) {
    UnitA a(k);
    UnitB b(k + 1);
    UnitC c(k + 2);
    UnitD d(k + 3);
    IUnit *tbl[4] = { &a, &b, &c, &d };
    u32 s = 0;
    for (u32 i = 0; i < 4; i++) s = s * 31 + tbl[i]->both(x + i);
    return s;
}
EXPORT u32 cx_iface_ref(u32 which, u32 k, u32 x) {
    UnitA a(k);
    UnitC c(k * 3);
    IUnit &r = (which & 1) ? static_cast<IUnit &>(a) : static_cast<IUnit &>(c);
    return r.value(x) ^ r.tag();
}
EXPORT u32 cx_iface_arg(u32 which, u32 k, u32 x) {
    UnitA a(k);
    UnitB b(k);
    UnitC c(k);
    UnitD d(k);
    switch (which & 3) {
    case 0: return run_unit(&a, x);
    case 1: return run_unit(&b, x);
    case 2: return run_unit(&c, x);
    default: return run_unit(&d, x);
    }
}
EXPORT u32 cx_iface_store(u32 *out, u32 k, u32 x) {
    UnitB b(k);
    UnitC c(x);
    IUnit *u = &b;
    out[0] = u->value(k);
    u = &c;
    out[1] = u->value(x) + u->tag();
    return out[0] * out[1];
}

// Templates at several types.
EXPORT u32 cx_tpl_sum_u8(const u8 *p) { return tpl_sum4(p); }
EXPORT u32 cx_tpl_sum_i16(const i16 *p) { return (u32)tpl_sum4(p); }
EXPORT u32 cx_tpl_sum_u32(const u32 *p) { return tpl_sum4(p); }
EXPORT u64 cx_tpl_sum_u64(const u64 *p) { return tpl_sum4(p); }
EXPORT float cx_tpl_sum_f(const float *p) { return tpl_sum4(p); }
EXPORT double cx_tpl_sum_d(const double *p) { return tpl_sum4(p); }
EXPORT u32 cx_tpl_dot_u32(const u32 *a, const u32 *b) { return tpl_dot<u32, 4>(a, b); }
EXPORT u32 cx_tpl_dot_u8(const u8 *a, const u8 *b) { return tpl_dot<u8, 8>(a, b); }
EXPORT float cx_tpl_dot_f(const float *a, const float *b) { return tpl_dot<float, 3>(a, b); }
EXPORT i32 cx_tpl_clamp_i32(i32 v, i32 lo, i32 hi) { return tpl_clamp(v, lo, hi); }
EXPORT i64 cx_tpl_clamp_i64(i64 v, i64 lo, i64 hi) { return tpl_clamp(v, lo, hi); }
EXPORT float cx_tpl_clamp_f(float v, float lo, float hi) { return tpl_clamp(v, lo, hi); }
EXPORT u32 cx_tpl_fold_u32(u32 seed, u32 n) { return tpl_fold(seed, n); }
EXPORT u64 cx_tpl_fold_u64(u64 seed, u32 n) { return tpl_fold(seed, n); }
EXPORT float cx_tpl_fold_f(float seed, u32 n) { return tpl_fold(seed, n); }
EXPORT u32 cx_tpl_apply_u32(u32 v) { return tpl_apply(bump_u32, v); }
EXPORT double cx_tpl_apply_d(double v) { return tpl_apply(half_d, v); }

// Lambdas.
EXPORT u32 cx_lam_capture_tpl(u32 a, u32 b) {
    u32 k = a | 1;
    return tpl_apply_twice([k, b](u32 x) { return x * k + b; }, a);
}
EXPORT u32 cx_lam_capture_ref(u32 a, u32 b) {
    u32 acc = a;
    u32 r = tpl_apply_twice([&acc, b](u32 x) { acc += x ^ b; return acc; }, b);
    return r + acc;
}
EXPORT u32 cx_lam_fnptr(u32 a) {
    u32 (*f)(u32) = [](u32 x) { return x * 5 - 3; };
    return apply_fp(f, a);
}
EXPORT u32 cx_lam_ctx(u32 a, u32 b) {
    u32 k = b;
    auto f = [k, a](u32 x) { return (x ^ k) + a; };
    return apply_ctx(&tpl_call_ctx<decltype(f)>, &f, a);
}
EXPORT u32 cx_lam_out(u32 *out, u32 a, u32 b) {
    auto add = [out, b](u32 x) { out[0] += x * b; return out[0]; };
    return tpl_apply_twice(add, a) + out[0];
}
EXPORT u32 cx_lam_nested(u32 a, u32 b) {
    auto inner = [b](u32 x) { return x + b; };
    auto outer = [inner, a](u32 x) { return inner(x) * a; };
    return tpl_apply_twice(outer, b);
}
EXPORT u32 cx_lam_mutable(u32 a) {
    auto f = [a](u32 x) mutable { a += x; return a; };
    u32 r = f(1);
    return r * 7 + f(2);
}

// Operator overloads.
EXPORT u32 cx_v2_arith(u32 a, u32 b, u32 c, u32 d) {
    V2 p(a, b), q(c, d);
    V2 r = (p + q) * 3u - (p - q);
    return r.x * 7u + r.y;
}
EXPORT u32 cx_v2_dot(u32 a, u32 b, u32 c, u32 d) {
    V2 p(a, b), q(c, d);
    return (p * q) + (u32)(p == q) + (u32)(p != q) * 2u;
}
EXPORT u32 cx_v2_scalar_left(u32 a, u32 b) {
    V2 r = 2u * V2(a, b);
    return r[0] ^ r[1];
}
EXPORT u32 cx_v2_compound(u32 a, u32 b, u32 i) {
    V2 v(a, b);
    v += V2(i, 1);
    V2 w = -v;
    w += V2(a, b);
    return w[i] ^ (v[i] + (u32)(w != v));
}
EXPORT u32 cx_v2_store(V2 *out, u32 a, u32 b) {
    V2 v(a, b);
    *out = v * (a | 1u);
    return out->x ^ out->y;
}
EXPORT u32 cx_v2_array_sum(const V2 *arr, u32 n) {
    V2 acc(0, 0);
    n &= 7;
    for (u32 i = 0; i < n; i++) acc += arr[i];
    return acc.x * 3u + acc.y;
}
EXPORT u32 cx_v4_add(const V4 *p, const V4 *q, V4 *out) {
    *out = (*p + *q) ^ 0x0Fu;
    return out->v[0] + out->v[3];
}
EXPORT u32 cx_v4_bias(const V4 *p, u32 which) {
    V4 r = *p * kBias;
    return r.v[which & 3] ^ r.v[0];
}
EXPORT u32 cx_v4_bias_xor(const V4 *p, u32 m) {
    V4 r = (*p ^ m) * kBias;
    return r.v[1] + r.v[2];
}
EXPORT float cx_f3_dot(const F3 *a, const F3 *b, u32 which) {
    F3 s = *a * (float)(which & 7);
    return s * *b;
}

// Placement construction into a caller-supplied buffer.
EXPORT u32 cx_place_acc(u8 *buf, u32 seed, u32 n) {
    Acc3 *a = new (buf + 8) Acc3(seed);
    n &= 31;
    for (u32 i = 0; i < n; i++) a->add(i * i + seed);
    u32 r = a->total ^ a->count;
    a->~Acc3();
    return r;
}
EXPORT u32 cx_place_pair(u8 *buf, u32 a, u32 b) {
    Acc3 *x = new (buf) Acc3(a);
    Acc3 *y = new (buf + 16) Acc3(b);
    x->add(y->total);
    y->add(x->count);
    return x->total + y->total * 3;
}
EXPORT u32 cx_place_mi(u8 *buf, u32 k, u32 x) {
    MiC *m = new (buf) MiC(k, x, k ^ x);
    MiB *pb = m;
    return pb->fb(x) + pb->gb(k) + m->c0;
}
EXPORT u32 cx_place_vb(u8 *buf, u32 x, u32 y) {
    VbD *d = new (buf) VbD(x, y, x + y);
    VbBase *p = d;
    return p->vv(y) + d->v;
}
EXPORT u32 cx_place_units(u8 *buf, u32 which, u32 k, u32 x) {
    IUnit *u = (which & 1) ? static_cast<IUnit *>(new (buf) UnitA(k))
                           : static_cast<IUnit *>(new (buf + 16) UnitC(k));
    return u->both(x);
}
EXPORT u32 cx_place_loop(u8 *buf, u32 n) {
    u32 s = 0;
    n &= 7;
    for (u32 i = 0; i < n; i++) {
        Acc3 *a = new (buf + 8 * i) Acc3(i);
        a->add(i + 1);
        s += a->total + a->count;
    }
    return s;
}

// Covariant returns, virtual calls in constructors, copies, conversions, chained builders.
EXPORT u32 cx_cov_next(u32 v, u32 e, u32 p) {
    CovMI m(v, e, p);
    CovBase *b = &m;
    CovBase *n = b->next();
    return n->weight() + n->id;
}
EXPORT u32 cx_cov_direct(u32 v, u32 e, u32 p) {
    CovMI m(v, e, p);
    CovMI *q = m.next();
    return q->weight() * 2 + q->pad;
}
EXPORT u32 cx_ctor_virtual(u32 e) {
    Ctor2D d(e);
    return d.tag * 2 + d.vf();
}
EXPORT u32 cx_copy_ctor(u32 x, u32 y) {
    Cp c(x, y);
    Cp d = c;
    Cp e(0, 0);
    e = d;
    return cp_take(e, x) + cp_take(c, y);
}
EXPORT u32 cx_conv_op(u32 x) {
    Wrap w(x);
    u32 u = w;
    return u + (u32)(bool)w;
}
EXPORT u32 cx_builder(u32 x, u32 y) {
    Builder bld;
    return bld.setA(x).setB(y).setC(x * y).done();
}
EXPORT u32 cx_static_member(u32 x, u32 y) {
    Scaler s = { x };
    u32 (*sf)(u32) = &Scaler::scale;
    return sf(y) + s.get() + Scaler::scale(s.v);
}

} // extern "C"
