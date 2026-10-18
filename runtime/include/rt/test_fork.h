/* TEST ONLY: what the test harness calls first in a process it forked (rt_test_forked in
 * runtime/tests/harness.c, which T_TRAPS and rt/test_child.h call before anything else).
 *
 * The runtime never forks; its locks are not built to survive a fork.  fork() copies a lock
 * that another thread holds, held, into a child in which that thread does not exist, and the
 * child waits for it for ever.  Each function below puts one such lock back into the unlocked
 * state.  The data a lock guards may be in the middle of a change: these two are the locks
 * whose data stays usable (the log's sink and output; the wait queues of objects whose
 * waiters are all gone with their threads), and the two an exiting thread can still hold
 * after a wait on its thread object has returned (rt/thread.h, rt_thread_host_threads).
 *
 * Call them in the child only, before anything else, on its one thread.
 */
#ifndef RT_TEST_FORK_H
#define RT_TEST_FORK_H

void rt_log_forked_child(void);         /* core/log.c: the log's mutex */
void rt_dispatcher_forked_child(void);  /* core/wait.c: the dispatcher lock */

#endif /* RT_TEST_FORK_H */
