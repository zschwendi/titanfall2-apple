#include "rt/test.h"

/* A RECT from its coordinates, in the order of the structure: left, top, right, bottom. */
static RECT make_rect(LONG l, LONG t, LONG r, LONG b)
{
    RECT x;

    x.left = l;
    x.top = t;
    x.right = r;
    x.bottom = b;
    return x;
}

/* Every field of rc is checked with T_EQ, so each comparison is reported on its own line. */
#define CHECK_RECT(rc, vl, vt, vr, vb)                  \
    do {                                                \
        T_EQ((rc).left, (vl));                          \
        T_EQ((rc).top, (vt));                           \
        T_EQ((rc).right, (vr));                         \
        T_EQ((rc).bottom, (vb));                        \
    } while (0)

TEST(user32, IntersectRect)
{
    RECT a, b, d;
    BOOL r;

    T_CASE("overlapping rectangles give their common part");
    a = make_rect(0, 0, 10, 10);
    b = make_rect(5, 3, 20, 8);
    d = make_rect(-1, -1, -1, -1);          /* a sentinel: every field must be written */
    r = CALL(user32, IntersectRect, &d, &a, &b);
    T_EQ(r, TRUE);
    CHECK_RECT(d, 5, 3, 10, 8);
    r = CALL(user32, IntersectRect, &d, &b, &a);
    T_EQ(r, TRUE);
    CHECK_RECT(d, 5, 3, 10, 8);

    T_CASE("rectangles touching at an edge do not intersect");
    a = make_rect(0, 0, 10, 10);
    b = make_rect(10, 0, 20, 10);           /* they share the edge x = 10 */
    d = make_rect(-1, -1, -1, -1);
    r = CALL(user32, IntersectRect, &d, &a, &b);
    T_EQ(r, FALSE);
    CHECK_RECT(d, 0, 0, 0, 0);
    b = make_rect(0, 10, 10, 20);           /* they share the edge y = 10 */
    d = make_rect(-1, -1, -1, -1);
    r = CALL(user32, IntersectRect, &d, &a, &b);
    T_EQ(r, FALSE);
    CHECK_RECT(d, 0, 0, 0, 0);

    T_CASE("one rectangle inside the other gives the inner one");
    a = make_rect(0, 0, 100, 100);
    b = make_rect(20, 30, 40, 50);
    d = make_rect(-1, -1, -1, -1);
    r = CALL(user32, IntersectRect, &d, &a, &b);
    T_EQ(r, TRUE);
    CHECK_RECT(d, 20, 30, 40, 50);
    r = CALL(user32, IntersectRect, &d, &b, &a);
    T_EQ(r, TRUE);
    CHECK_RECT(d, 20, 30, 40, 50);

    T_CASE("an empty source in either position gives FALSE and an empty destination");
    a = make_rect(5, 0, 5, 10);             /* empty: left == right, in the first position */
    b = make_rect(0, 0, 10, 10);
    d = make_rect(-1, -1, -1, -1);
    r = CALL(user32, IntersectRect, &d, &a, &b);
    T_EQ(r, FALSE);
    CHECK_RECT(d, 0, 0, 0, 0);
    a = make_rect(0, 0, 10, 10);
    b = make_rect(0, 5, 10, 5);             /* empty: top == bottom, in the second position */
    d = make_rect(-1, -1, -1, -1);
    r = CALL(user32, IntersectRect, &d, &a, &b);
    T_EQ(r, FALSE);
    CHECK_RECT(d, 0, 0, 0, 0);

    T_CASE("the destination may be one of the sources");
    a = make_rect(0, 0, 10, 10);
    b = make_rect(5, 3, 20, 8);
    r = CALL(user32, IntersectRect, &a, &a, &b);        /* destination is the first source */
    T_EQ(r, TRUE);
    CHECK_RECT(a, 5, 3, 10, 8);
    a = make_rect(0, 0, 10, 10);
    b = make_rect(5, 3, 20, 8);
    r = CALL(user32, IntersectRect, &b, &a, &b);        /* destination is the second source */
    T_EQ(r, TRUE);
    CHECK_RECT(b, 5, 3, 10, 8);
    a = make_rect(0, 0, 10, 10);
    b = make_rect(20, 20, 30, 30);
    r = CALL(user32, IntersectRect, &a, &a, &b);        /* no overlap: the aliased source is zeroed */
    T_EQ(r, FALSE);
    CHECK_RECT(a, 0, 0, 0, 0);

    T_CASE("a NULL pointer in any position returns FALSE and leaves the destination alone");
    a = make_rect(0, 0, 10, 10);
    b = make_rect(5, 3, 20, 8);
    d = make_rect(7, 7, 9, 9);              /* the preset destination */
    r = CALL(user32, IntersectRect, NULL, &a, &b);
    T_EQ(r, FALSE);
    CHECK_RECT(a, 0, 0, 10, 10);            /* the sources are not written either */
    CHECK_RECT(b, 5, 3, 20, 8);
    r = CALL(user32, IntersectRect, &d, NULL, &b);
    T_EQ(r, FALSE);
    CHECK_RECT(d, 7, 7, 9, 9);
    r = CALL(user32, IntersectRect, &d, &a, NULL);
    T_EQ(r, FALSE);
    CHECK_RECT(d, 7, 7, 9, 9);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    a = make_rect(0, 0, 10, 10);
    b = make_rect(5, 3, 20, 8);
    CALL(user32, IntersectRect, &d, &a, &b);
    T_EQ(T_LAST_ERROR(), 0x1234);
    b = make_rect(20, 20, 30, 30);
    CALL(user32, IntersectRect, &d, &a, &b);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
