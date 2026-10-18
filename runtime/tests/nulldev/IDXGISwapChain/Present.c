/* Expected values: the planner's decision in unit d3d-fix-2's brief (item 7), after the red
 * team's R1: with a sync interval of 1 to 4 Present returns at the next multiple of the 60 Hz
 * mode the device reports; a switch turns the pacing off.  And the planner's decision B in
 * unit gate3-run-6's brief: by default a sync interval of 0 is paced as 1 (an invented answer
 * with a kind of its own in the ledger, "present: interval 0 paced as 1"); with
 * RT_D3D_PRESENT=immediate it returns at once, as on Windows, and is not counted in that kind.  Six intervals of
 * 1/60 s are 100 ms; the bounds below are wide because other programs share the machine:
 * a run that was not paced would take well under 50 ms, and a paced one cannot take less than
 * five whole periods (83 ms) after the first call has put it on a boundary. */
#include <stdlib.h>
#include <string.h>

#include "rt/test.h"
#include "rt/nulldev.h"

TEST(IDXGISwapChain, Present)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    IDXGISwapChain *sc = rt_nulldev_new(device, &rt_nulldev_swapchain_class);
    unsetenv("RT_D3D_PRESENT");
    rt_nulldev_switches_reset();

    T_CASE("a sync interval of 0 is paced as an interval of 1 and the ledger counts each call as a frame and as an interval 0 paced as 1");
    uint64_t frames = rt_nulldev_invented_count(ND_INV_PRESENT), as1 = rt_nulldev_invented_count(ND_INV_PRESENT_0);
    T_EQ(COM_CALL(IDXGISwapChain, Present, sc, 0, 0), S_OK);            /* onto a boundary */
    uint64_t t0 = rt_nulldev_ticks();
    for (int i = 0; i < 6; i++) T_EQ(COM_CALL(IDXGISwapChain, Present, sc, 0, 0), S_OK);
    uint64_t took0 = rt_nulldev_ticks() - t0;
    T_TRUE(took0 >= 5 * ND_TICKS_PER_SECOND / 60);                      /* unpaced, the six take well under a millisecond */
    T_TRUE(took0 < ND_TICKS_PER_SECOND);
    T_EQ(rt_nulldev_invented_count(ND_INV_PRESENT), frames + 7);
    T_EQ(rt_nulldev_invented_count(ND_INV_PRESENT_0), as1 + 7);
    T_TRUE(T_LOGGED(RT_CH_COM, "present: interval 0 paced as 1"));       /* the first of its kind, with its call site */

    T_CASE("a sync interval of 1 returns at boundaries of 1/60 s, so six frames take about a tenth of a second");
    T_EQ(COM_CALL(IDXGISwapChain, Present, sc, 1, 0), S_OK);            /* onto a boundary */
    t0 = rt_nulldev_ticks();
    for (int i = 0; i < 6; i++) T_EQ(COM_CALL(IDXGISwapChain, Present, sc, 1, 0), S_OK);
    uint64_t took = rt_nulldev_ticks() - t0;
    T_TRUE(took >= 5 * ND_TICKS_PER_SECOND / 60);
    T_TRUE(took < ND_TICKS_PER_SECOND);
    t0 = rt_nulldev_ticks();
    T_EQ(COM_CALL(IDXGISwapChain, Present, sc, 4, 0), S_OK);            /* the fourth boundary: more than 3/60 s away */
    T_TRUE(rt_nulldev_ticks() - t0 >= 3 * ND_TICKS_PER_SECOND / 60);
    T_EQ(rt_nulldev_invented_count(ND_INV_PRESENT), frames + 15);
    T_EQ(rt_nulldev_invented_count(ND_INV_PRESENT_0), as1 + 7);         /* an interval of 1 to 4 is what was asked for */

    T_CASE("RT_D3D_PRESENT=immediate returns at once whatever the interval, and a sync interval of 0 is then not counted as paced");
    setenv("RT_D3D_PRESENT", "immediate", 1);
    rt_nulldev_switches_reset();
    t0 = rt_nulldev_ticks();
    for (int i = 0; i < 10; i++) T_EQ(COM_CALL(IDXGISwapChain, Present, sc, 4, 0), S_OK);
    T_TRUE(rt_nulldev_ticks() - t0 < ND_TICKS_PER_SECOND / 20);         /* paced, these would take two thirds of a second */
    t0 = rt_nulldev_ticks();
    for (int i = 0; i < 10; i++) T_EQ(COM_CALL(IDXGISwapChain, Present, sc, 0, 0), S_OK);
    T_TRUE(rt_nulldev_ticks() - t0 < ND_TICKS_PER_SECOND / 20);         /* paced as 1, these would take a sixth of a second */
    T_EQ(rt_nulldev_invented_count(ND_INV_PRESENT), frames + 35);
    T_EQ(rt_nulldev_invented_count(ND_INV_PRESENT_0), as1 + 7);
    unsetenv("RT_D3D_PRESENT");
    rt_nulldev_switches_reset();

    T_TRAPS(COM_CALL(IDXGISwapChain, Present, sc, 5, 0), "Present with a sync interval of 5");
    T_TRAPS(COM_CALL(IDXGISwapChain, Present, sc, 1, 1), "Present with flags");
}
