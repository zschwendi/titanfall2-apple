// C++ with the Microsoft ABI: vtables, virtual calls, multiple inheritance, member pointers.
// No exceptions, no RTTI, no heap.
extern "C" {
#include "corpus.h"
}

struct Shape {
    virtual i32 area() const = 0;
    virtual i32 scale(i32 k) { return area() * k; }
    virtual ~Shape() {}
};
struct Rect : Shape {
    i32 w, h;
    Rect(i32 w_, i32 h_) : w(w_), h(h_) {}
    i32 area() const override { return w * h; }
};
struct Square : Rect {
    Square(i32 s) : Rect(s, s) {}
    i32 scale(i32 k) override { return area() * k + 1; }
};
struct Tri : Shape {
    i32 b, h;
    Tri(i32 b_, i32 h_) : b(b_), h(h_) {}
    i32 area() const override { return b * h / 2; }
};
struct Named { virtual u32 id() const { return 7; } u32 pad = 3; };
struct Counter { virtual i32 bump(i32 by) { n += by; return n; } i32 n = 0; };
struct Both : Named, Counter {            // second base needs a this-adjusting thunk
    u32 id() const override { return 42 + pad; }
    i32 bump(i32 by) override { n += by * 2; return n + (i32)pad; }
};

extern "C" int _purecall(void) { return 0; }
void operator delete(void *, usize) {}

NOINLINE static i32 use_shape(Shape &s, i32 k) { return s.area() + s.scale(k); }
NOINLINE static i32 use_counter(Counter &c, i32 by) { return c.bump(by) + c.bump(1); }

extern "C" EXPORT i32 shapes(i32 a, i32 b, i32 k) {
    Rect r(a & 255, b & 255); Square q(a & 15); Tri t(b & 63, a & 63);
    return use_shape(r, k) + use_shape(q, k) * 3 + use_shape(t, k) * 5;
}
extern "C" EXPORT i32 pick(u32 which, i32 a, i32 k) {
    Rect r(a & 31, 3); Square q(a & 7); Tri t(a & 15, 8);
    Shape *v[3] = { &r, &q, &t };
    return v[which % 3]->scale(k);
}
extern "C" EXPORT i32 multi(i32 by) { Both b; Named &n = b; return use_counter(b, by) + (i32)n.id(); }
extern "C" EXPORT i32 member_ptr(u32 which, i32 a) {
    Rect r(a & 63, 5);
    i32 (Shape::*f)(i32) = &Shape::scale;
    i32 Rect::*m = (which & 1) ? &Rect::w : &Rect::h;
    return (r.*f)(2) + r.*m;
}
