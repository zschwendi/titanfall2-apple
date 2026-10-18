/* Tests of the seam behind the do-nothing device (rt/d3dback.h): what a test of one method's
 * forward is written with.  runtime/tests/back/<Interface>/<Method>.c, one BACK_TEST each:
 *
 *     #include "rt/test_back.h"
 *
 *     BACK_TEST(ID3D11DeviceContext, DrawIndexed)
 *     {
 *         ID3D11Device *device;
 *         ID3D11DeviceContext *context;
 *         rt_nulldev_create(&device, &context);
 *         T_CASE("a draw is handed on once, with its numbers");
 *         COM_CALL(ID3D11DeviceContext, DrawIndexed, context, 36, 6, -2);
 *         if (T_BACK) {
 *             T_EQ(T_BACK_COUNT(DrawIndexed), 1);
 *             T_EQ(T_BACK_ARG(DrawIndexed, 0), T_BACK_OF(context));
 *             T_EQ(T_BACK_ARG(DrawIndexed, 1), 36);
 *         }
 *     }
 *
 * THE BODY RUNS TWICE, in two processes: as test back_<Interface>/<Method> with the recording
 * back end chosen (T_BACK is true), and as none_<Interface>/<Method> with no back end (T_BACK
 * is false).  What the game can see (results, values written, reference counts) is checked
 * outside `if (T_BACK)`, so the same checks hold in both: the device with a back end answers
 * as the device without one, except where rt/d3dback.h says the answer is the back end's.
 *
 * WHAT THE WRAPPER CHECKS ITSELF, after the body, for the entry named like the method:
 *   with the back end   calls of the method that returned  ==  asked + answered alone (the
 *                       seam's counts), asked == what the recording back end counted, and the
 *                       method was called at least once;
 *   with none           the seam counted nothing and the recording back end was never reached.
 * So a body that forwards on one path and forgets another, or forwards twice, fails here
 * whatever the test itself asserts.
 *
 * A STOP MUST COME BEFORE THE BACK END (rule 1): for every stop of the body,
 *     T_BACK_STOPS_BEFORE(COM_CALL(...the call that stops...));
 * It passes with the back end only when the fatal report says the call had not reached the
 * back end; with none it only requires the stop.
 */
#ifndef RT_TEST_BACK_H
#define RT_TEST_BACK_H

#include "rt/test.h"
#include "rt/nulldev.h"

/* True in the half of the test that has the recording back end. */
#define T_BACK  rt_back_

/* How often the recording back end was reached at `entry`; argument `i` of its last call as 64
 * bits (0 is the back end's device, context or swap chain); the back object of a front object
 * as the same 64 bits (0 for NULL); element `j` of the object array that was argument `i`. */
#define T_BACK_COUNT(entry)       rt_d3dback_record_count(RT_BACK_##entry)
#define T_BACK_ARG(entry, i)      rt_d3dback_record_arg(RT_BACK_##entry, (i))
#define T_BACK_OF(front)          ((uint64_t)(uintptr_t)rt_d3dback_of(front))
#define T_BACK_PTR(p)             ((uint64_t)(uintptr_t)(p))
#define T_BACK_ELEMENT(entry, i, j)  rt_test_back_element(RT_BACK_##entry, (i), (j))
#define T_BACK_ARRAY_LEN(entry, i)   rt_d3dback_record_array(RT_BACK_##entry, (i), NULL, 0)

static inline __attribute__((unused)) uint64_t rt_test_back_element(RtD3dBackEntry entry, unsigned i, unsigned j) {
    void *all[128] = { 0 };
    unsigned n = rt_d3dback_record_array(entry, i, all, 128);
    return j < n && j < 128 ? (uint64_t)(uintptr_t)all[j] : UINT64_C(0xbadbadbad);
}

/* A front object of class `cls` made without its creator (rt_nulldev_new: the record is zeroed
 * but for the device), with a back object of its kind when the back end is there: something to
 * bind or to pass before its creator's row is written.  T_BACK_ADOPT gives a front object made
 * by any other means (rt_nulldev_texture_new, rt_nulldev_buffer_new) its back object. */
static inline __attribute__((unused)) void *rt_test_back_adopt(void *front) {
    if (rt_d3dback() && rt_d3dback_claim(front))
        rt_d3dback_attach(front, rt_d3dback_record_object((RtD3dBackKind)rt_nulldev_back_kind(rt_com_class_of(front))));
    return front;
}
#define T_BACK_ADOPT(front)        rt_test_back_adopt(front)
#define T_BACK_NEW(device, cls)    rt_test_back_adopt(rt_nulldev_new((device), (cls)))

/* The statement must stop, and with the back end the stop must come before the back end was
 * reached (the line the seam adds to every fatal report: rt/d3dback.h, rule 1). */
#define T_BACK_STOPS_BEFORE(stmt) \
    T_TRAPS(stmt, T_BACK ? "d3dback: the call that stopped had not reached the back end" : "FATAL")

/* The wrapper's own checks (above). */
static inline __attribute__((unused)) void rt_test_back_sum(bool back, const RtComIface *iface, const char *method, RtD3dBackEntry entry,
                                                            const char *file, int line) {
    uint64_t calls = rt_com_calls(iface, method), asked = rt_d3dback_asked(entry), alone = rt_d3dback_alone(entry);
    uint64_t reached = rt_d3dback_record_count(entry);
    if (!back) {
        if (asked || alone || reached)
            rt_test_fail(file, line, "with no back end the seam counted %s: asked %llu, alone %llu, and the recording back end %llu", method,
                         (unsigned long long)asked, (unsigned long long)alone, (unsigned long long)reached);
        return;
    }
    if (!calls) rt_test_fail(file, line, "the test never called %s::%s (a call that returned)", iface->name, method);
    if (asked + alone != calls)
        rt_test_fail(file, line, "%s::%s returned %llu times, and the body handed %llu of them on and answered %llu alone: every "
                     "accepted call passes one of the RT_BACK macros exactly once", iface->name, method, (unsigned long long)calls,
                     (unsigned long long)asked, (unsigned long long)alone);
    if (reached != asked)
        rt_test_fail(file, line, "the body asked for %s %llu times and the recording back end was reached %llu times", method,
                     (unsigned long long)asked, (unsigned long long)reached);
}

#define BACK_TEST(iface, method) \
    static void rt_backfn_##iface##_##method(bool rt_back_); \
    TEST(back_##iface, method) { \
        rt_d3dback_use(rt_d3dback_record()); \
        rt_backfn_##iface##_##method(true); \
        rt_test_back_sum(true, &rt_iface_##iface, #method, RT_BACK_##method, __FILE__, __LINE__); \
    } \
    TEST(none_##iface, method) { \
        rt_d3dback_use(NULL); \
        rt_backfn_##iface##_##method(false); \
        rt_test_back_sum(false, &rt_iface_##iface, #method, RT_BACK_##method, __FILE__, __LINE__); \
    } \
    static void rt_backfn_##iface##_##method(bool rt_back_ __attribute__((unused)))

#endif /* RT_TEST_BACK_H */
