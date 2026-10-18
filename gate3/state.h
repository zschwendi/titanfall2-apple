/* WHERE A RUN STARTS AND WHAT IT LEAVES BEHIND (gate3/state.c; unit gate3-run-13, item 1).
 *
 * The runtime's state directory holds everything the game writes as "the user's files": its
 * settings, its profile, its save (C:\Users\Player is <state>/home), the registry file and the
 * Temp directory.  Until 11 October 2026 `gate3 run` used one directory, build/gate3/state,
 * run after run: a run that was ended by a stop while the game was writing its save left the
 * file cut off, and the next run loaded it and faulted (docs/handoff/gate3-run-12.md, entry
 * 7).  From here on:
 *
 *   - EVERY RUN WORKS ON A FRESH COPY OF A NAMED START, in a working directory of its own,
 *     <out>.state (GATE3_RUN_OUT; build/gate3/run.state by default), which the driver makes new
 *     before the run and never reads as a start:
 *         GATE3_START=new          (the default) an empty directory: the game's first start,
 *                                  with no settings, no profile and no save
 *         GATE3_START=save:<dir>   a copy of <dir>, which must be a state that a run KEPT (below)
 *         GATE3_START=player:<dir> a player's state, carried from run to run: a copy of <dir>, which
 *                                  must carry the kept mark (or does not exist yet: then the game's
 *                                  first start, as for `new`).  When the run's end is safe (below)
 *                                  the run's working copy REPLACES <dir>, and the state before it
 *                                  is kept as <dir>.prev (one generation only, never more).  With
 *                                  GATE3_KEEP_STATE it is refused: one or the other.
 *   - A RUN'S STATE IS KEPT ONLY WHEN THE RUN ASKS FOR IT AND ENDED CLEANLY:
 *         GATE3_KEEP_STATE=<dir>   <dir> must not exist.  When the run ended cleanly its
 *                                  working directory is moved there and marked as kept; when
 *                                  it did not, nothing is made at <dir>.
 *     "Cleanly" is decided by the caller (gate3/run.c, THE STATE): LauncherMain returned, or
 *     the driver ended the run at GATE3_PRESENTS at a moment when no file under the state
 *     directory was open for writing.  A stop, a fault, the time limit and every other end
 *     are not clean.
 *   - A PLAYER'S END IS SAFE (`safe`, decided by gate3/run.c) when no file under the state
 *     directory was open for writing at the end, and the end was a clean end as above, the
 *     host window's close, or a stop of the runtime.  The time limit, a signal and any other
 *     end are never safe.  A safe end of `player:<dir>` takes the working copy over: it is
 *     marked as a player's state taken over (how the run ended, its start, the time) and
 *     becomes <dir>; <dir> becomes <dir>.prev, replacing the one before it, which must carry
 *     the kept mark (else nothing is moved).  An end that is not safe moves nothing: <dir> is
 *     as it was before the run, and the run's state stays at its working copy, marked.
 *   - A RUN THAT DID NOT END CLEANLY, or did not ask, LEAVES ITS WORKING DIRECTORY WHERE IT IS,
 *     for inspection.  It carries the mark of a working copy and `save:` refuses it; the next
 *     run with the same <out> replaces it.
 *
 * The two marks are files at the directory's root, where the game never looks:
 * GATE3_STATE_MARK_WORK in a working copy (the driver made this directory and may remove it)
 * and GATE3_STATE_MARK_KEPT in a kept state (its text says which run kept it, from which start
 * and how the run ended).  The driver removes a directory only if it carries the working
 * copy's mark or is empty: anything else at <out>.state ends the run before it starts.
 *
 * Nothing here touches the runtime: plain host files, so that gate3/tests/state_test.c tests
 * it without the game (`make gate3-check`). */
#ifndef GATE3_STATE_H
#define GATE3_STATE_H

#include <stdbool.h>
#include <stddef.h>

#define GATE3_STATE_MARK_WORK "GATE3-WORKING-COPY-OF-A-RUN.txt"
#define GATE3_STATE_MARK_KEPT "GATE3-STATE-KEPT-FROM-A-CLEAN-RUN.txt"

/* Make `work` the fresh copy of `start` ("new", NULL or "" for new; "save:<dir>").  `keep`
 * (NULL or "": none) is only checked here: it must not exist and must not be `work`.
 * Returns true and writes to `what` the line the run logs ("new (...)" or "save:<dir> (...)");
 * returns false with the reason in `what`, and then nothing was removed or made unless the
 * reason says so. */
bool gate3_state_prepare(const char *start, const char *work, const char *keep, char *what, size_t n);

/* After the run.  `clean`: the run ended cleanly; `safe`: the end was safe (above; used only
 * for a `player:` start, where `keep` is NULL); `how`: how it ended, in words, for the mark
 * and the line.  With `keep` and `clean`, `work` is moved to `keep` and marked as kept;
 * otherwise `work` stays.  `what` gets the line the driver prints.  Returns false when a
 * state that should have been kept, or a player's state taken over, could not be (the reason
 * is in `what`; `work` stays); true otherwise, including a player's end that was not safe. */
bool gate3_state_finish(const char *start, const char *work, const char *keep, bool clean, bool safe, const char *how, char *what, size_t n);

#endif
