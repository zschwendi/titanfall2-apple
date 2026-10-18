/* Watching vstdlib's thread-pool self test (rank 29, RunThreadPoolTests).  gate2/pool.c.
 *
 * WHY.  The self test never returns, here or (as the code reads) on Windows.  Its test routine
 * (vstdlib.dll+0x19150) makes 4,000 job objects once and gives the same 4,000 to the pool in
 * each of its 24 rounds without putting them back to the state of a new job.  The pool's add
 * routine (vstdlib.dll+0x156a0) takes a job only while its status is "never queued" or
 * "queued"; a job that ran is "finished" and one the pool dropped at its stop is "aborted",
 * and both are refused without a word.  So only the first round of each call of the test
 * routine gives the pool anything.  The first four calls do not wait for their jobs and go
 * through 23 empty rounds each; the fifth is the first that waits for "all 4,000 counted",
 * its first round counts them, and its second round waits for ever with nothing queued.
 * docs/handoff/gate2-threads.md, stop 8, has the reading and the runs.
 *
 * WHAT.  The pool object is built on RunThreadPoolTests's stack with the vtable at
 * vstdlib.dll+0x5bcc8.  gate2_pool_watch puts three native stubs of the driver into that
 * vtable in the mapped image (not in the file): Start, Stop and the add routine.  Each
 * counts and then calls vstdlib's routine with the same arguments, so the pool runs as it
 * would without them.  A host thread (gate2_pool_watcher) recognises the stop described above
 * from the counts and lets the entry report it at once instead of at the time limit.
 *
 * Environment:
 *   GATE2_POOL_LOG=1     one line per round on descriptor 2 (kept by GATE2_STDERR): the threads,
 *                        the adds by the job's status on entry, how many the routine took, how
 *                        many jobs were counted when the pool stopped.
 *   GATE2_POOL_REARM=1   A DIAGNOSTIC, not a fix: before the add routine sees a job that has
 *                        finished or was aborted, the driver sets its status back to "never
 *                        queued".  With that every round gives the pool its 4,000 jobs and the
 *                        whole self test runs: 1 to 7 threads, suspended and not, hundreds of
 *                        thousands of jobs through the lifted pool and the runtime's events and
 *                        waits.  It shows that the missing re-arming is the only thing between
 *                        the self test and its end; it changes what the game's code is given,
 *                        so a run with it says nothing about rank 29 as the finding states it.
 */
#ifndef GATE2_POOL_H
#define GATE2_POOL_H

#include "calls.h"

typedef struct {
    unsigned threads;           /* asked of Start */
    unsigned adds;              /* calls of the add routine since Start */
    unsigned status[5];         /* of them, by the job's status on entry (index = the status, see pool.c) */
    unsigned other;             /* a NULL job or a status outside 0 to 4 */
    unsigned taken;             /* calls in which the routine goes on to queue the job */
    unsigned rearmed;           /* GATE2_POOL_REARM: jobs the driver set back to "never queued" */
    unsigned counted, to_count; /* the test's two counters after Stop returned */
} Gate2PoolRound;

typedef struct {
    bool           rearm;                       /* GATE2_POOL_REARM was set */
    bool           in_round;                    /* between Start and the return of Stop */
    Gate2PoolRound now, last;                   /* the round in progress; the last one ended */
    unsigned       rounds;                      /* rounds ended (Stop returned) */
    unsigned       rounds_all_counted;          /* of them, with adds and every job counted */
    unsigned       rounds_none_taken;           /* of them, with adds and none taken */
    unsigned long  total_adds, total_taken, total_rearmed, total_counted;
    bool           stopped_as_predicted;        /* set by the watcher before it calls the verdict */
} Gate2Pool;

/* gate2_host_before_seal calls this: the three stubs. */
void gate2_pool_before_seal(RtModule *ntdll);

/* Put the stubs into the pool's vtable; `r` is cleared and receives the counts.  False after
 * recording FAIL when the mapped vstdlib.dll is not the build the offsets were read from. */
bool gate2_pool_watch(Call *c, Gate2Pool *r);

/* Start the host thread that calls verdict(c) (which must not return) once nothing watched
 * has been called for idle_ms and the round in progress is the predicted stop: every add of
 * the round made, each of a finished job, none taken, none counted. */
void gate2_pool_watcher(Call *c, unsigned idle_ms, void (*verdict)(Call *c));

/* Something happened that is progress of the self test (a message of its own): the watcher's
 * idle time starts again. */
void gate2_pool_touch(void);

#endif /* GATE2_POOL_H */
