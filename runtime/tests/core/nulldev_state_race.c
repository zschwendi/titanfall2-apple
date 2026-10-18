/* A state object whose last reference is being released on one thread is never handed out by
 * a creator on another (docs/reviews/d3d-code-review.md, G4, whose test this is; rt/com.h,
 * rt_com_addref_live).
 *
 * Expected values: the layer's own promise (rt/com.h, THREADS: "objects may be created, used
 * and released on any guest thread") and Microsoft's page of CreateSamplerState (an equal
 * description gives the same interface): each create returns a live object with a reference
 * of the caller's, whichever thread released an equal one a moment before. */
#include <pthread.h>
#include <stdatomic.h>

#include "rt/test.h"
#include "rt/thread.h"
#include "rt/nulldev.h"

#define ROUNDS 100000

static ID3D11Device *race_device;
static _Atomic unsigned race_dead, race_failed;

static void *race_loop(void *arg)
{
    (void)arg;
    RtThread *t = rt_thread_attach("state-race");
    if (!t) { atomic_fetch_add(&race_failed, 1); return NULL; }
    D3D11_SAMPLER_DESC d = { 0x15, 1, 1, 1, 0.0f, 1, 1, { 0, 0, 0, 0 }, 0.0f, 3.402823466e+38f };   /* linear, WRAP: as the game's */
    for (unsigned i = 0; i < ROUNDS; i++) {
        void *s = NULL;
        if (rt_nulldev_state_create(&t->ctx, race_device, &rt_nulldev_sampler_class, &d, &s) != S_OK || !s) { atomic_fetch_add(&race_failed, 1); break; }
        if (rt_com_class_of(s) != &rt_nulldev_sampler_class) atomic_fetch_add(&race_dead, 1);
        rt_com_release(s);              /* fatal if the object died under this thread's reference */
    }
    rt_thread_detach(t);
    return NULL;
}

TEST(core, nulldev_state_race)
{
    ID3D11DeviceContext *context;
    rt_nulldev_create(&race_device, &context);
    pthread_t th[2];

    T_CASE("two threads create and release an equal sampler state 100,000 times each: every object handed out is alive");
    for (unsigned i = 0; i < 2; i++) T_REQUIRE(pthread_create(&th[i], NULL, race_loop, NULL) == 0);
    for (unsigned i = 0; i < 2; i++) pthread_join(th[i], NULL);
    T_EQ(atomic_load(&race_failed), 0);
    T_EQ(atomic_load(&race_dead), 0);
    T_EQ(((NdDevice *)rt_com_state_of(race_device, &rt_nulldev_device_class))->nstates[3], 0);
    T_EQ(rt_com_refs(race_device), 2);                  /* every state gave its device reference back */
}
