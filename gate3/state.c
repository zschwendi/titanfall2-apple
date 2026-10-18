/* Where a run starts and what it leaves behind: gate3/state.h has the rules. */
#include <copyfile.h>
#include <dirent.h>
#include <errno.h>
#include <removefile.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "state.h"

static bool is_dir(const char *p) {
    struct stat st;
    return lstat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool exists(const char *p) {
    struct stat st;
    return lstat(p, &st) == 0;
}

static bool has(const char *dir, const char *name) {
    char p[2048];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    struct stat st;
    return lstat(p, &st) == 0 && S_ISREG(st.st_mode);
}

static bool is_empty_dir(const char *p) {
    DIR *d = opendir(p);
    if (!d) return false;
    bool empty = true;
    for (struct dirent *e; empty && (e = readdir(d)) != NULL; )
        empty = !strcmp(e->d_name, ".") || !strcmp(e->d_name, "..");
    closedir(d);
    return empty;
}

static void now_text(char *buf, size_t n) {
    const time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(buf, n, "%Y-%m-%d %H:%M:%S %z", &tm);
}

static bool write_mark(const char *dir, const char *name, const char *text) {
    char p[2048];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "w");
    if (!f) return false;
    const bool ok = fputs(text, f) >= 0;
    return fclose(f) == 0 && ok;
}

/* The first line of a mark, without its end of line ("" when there is none). */
static void mark_line(const char *dir, const char *name, char *out, size_t n) {
    char p[2048];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    out[0] = 0;
    FILE *f = fopen(p, "r");
    if (!f) return;
    if (!fgets(out, (int)n, f)) out[0] = 0;
    fclose(f);
    out[strcspn(out, "\r\n")] = 0;
}

static bool remove_tree(const char *p) {
    removefile_state_t s = removefile_state_alloc();
    const int r = removefile(p, s, REMOVEFILE_RECURSIVE);
    removefile_state_free(s);
    return r == 0;
}

bool gate3_state_prepare(const char *start, const char *work, const char *keep, char *what, size_t n)
{
    const bool fresh = !start || !*start || !strcmp(start, "new");
    const bool player = start && !strncmp(start, "player:", 7);
    bool player_new = false;                    /* player:<dir> and <dir> does not exist yet */
    const char *from = NULL;
    if (player) {
        if (keep && *keep) { snprintf(what, n, "GATE3_START=%s and GATE3_KEEP_STATE=%s together: a player's state is taken over at the end of the run, not kept: one or the other", start, keep); return false; }
        from = start + 7;
        if (!from[0]) { snprintf(what, n, "GATE3_START=%s: the directory is missing (player:<dir>)", start); return false; }
        if (!exists(from)) player_new = true;
        else if (!is_dir(from)) { snprintf(what, n, "GATE3_START=%s: %s is not a directory", start, from); return false; }
        else if (has(from, GATE3_STATE_MARK_WORK)) {
            snprintf(what, n, "GATE3_START=%s: %s is the working copy a run left (%s), not a player's state: it is never the start of a run", start, from, GATE3_STATE_MARK_WORK);
            return false;
        }
        else if (!has(from, GATE3_STATE_MARK_KEPT)) {
            snprintf(what, n, "GATE3_START=%s: %s has no %s: not a player's state that a run took over (a directory of another kind is not used)", start, from, GATE3_STATE_MARK_KEPT);
            return false;
        }
    } else if (!fresh) {
        if (strncmp(start, "save:", 5) != 0 || !start[5]) {
            snprintf(what, n, "GATE3_START=%s: a start is `new` (an empty state directory), `save:<dir>` (a state a run kept with GATE3_KEEP_STATE) or `player:<dir>` (a player's state, taken over at the end of each run)", start);
            return false;
        }
        from = start + 5;
        if (!is_dir(from)) { snprintf(what, n, "GATE3_START=%s: %s is not a directory", start, from); return false; }
        if (has(from, GATE3_STATE_MARK_WORK)) {
            snprintf(what, n, "GATE3_START=%s: %s is the working copy a run left (%s), not a state that was kept: it is never the start of a run", start, from, GATE3_STATE_MARK_WORK);
            return false;
        }
        if (!has(from, GATE3_STATE_MARK_KEPT)) {
            snprintf(what, n, "GATE3_START=%s: %s has no %s: not a state that a run kept after a clean end", start, from, GATE3_STATE_MARK_KEPT);
            return false;
        }
    }
    if (!work || !*work) { snprintf(what, n, "no working directory named"); return false; }
    if (keep && *keep) {
        if (!strcmp(keep, work)) { snprintf(what, n, "GATE3_KEEP_STATE=%s is the run's own working directory", keep); return false; }
        if (exists(keep)) { snprintf(what, n, "GATE3_KEEP_STATE=%s exists already: a kept state is never written over", keep); return false; }
    }
    if (from && !strcmp(from, work)) { snprintf(what, n, "GATE3_START=%s is the run's own working directory", start); return false; }
    if (exists(work)) {
        if (!is_dir(work) || !(has(work, GATE3_STATE_MARK_WORK) || is_empty_dir(work))) {
            snprintf(what, n, "%s is in the way and is not a working copy this driver made (no %s): not removed, the run is not started", work, GATE3_STATE_MARK_WORK);
            return false;
        }
        if (!remove_tree(work)) { snprintf(what, n, "could not remove the last run's working copy %s: %s", work, strerror(errno)); return false; }
    }
    char when[64], line[400] = "";
    now_text(when, sizeof when);
    if (fresh || player_new) {
        if (mkdir(work, 0700) != 0) { snprintf(what, n, "mkdir %s: %s", work, strerror(errno)); return false; }
        if (player_new) snprintf(what, n, "player:%s (no state there yet: the game's first start)", from);
        else snprintf(what, n, "new (an empty state directory: the game's first start, no settings, no profile, no save)");
    } else {
        copyfile_state_t s = copyfile_state_alloc();
        const int r = copyfile(from, work, s, COPYFILE_ALL | COPYFILE_RECURSIVE);
        const int e = errno;
        copyfile_state_free(s);
        if (r != 0) { snprintf(what, n, "copying %s to %s: %s (what was copied is left there)", from, work, strerror(e)); return false; }
        mark_line(work, GATE3_STATE_MARK_KEPT, line, sizeof line);
        char p[2048];
        snprintf(p, sizeof p, "%s/%s", work, GATE3_STATE_MARK_KEPT);
        unlink(p);                                  /* the copy is a working copy, not a kept state */
        if (player) snprintf(what, n, "player:%s (a fresh copy of the player's state; its mark: %s)", from, line[0] ? line : "(empty)");
        else snprintf(what, n, "save:%s (a fresh copy of a kept state; its mark: %s)", from, line[0] ? line : "(empty)");
    }
    char text[1600];
    snprintf(text, sizeof text,
             "The working copy of one `gate3 run`, made %s by the driver (gate3/state.h).\n"
             "start: %s\n"
             "It is never the start of another run and the next run with the same output name removes it.\n",
             when, what);
    if (!write_mark(work, GATE3_STATE_MARK_WORK, text)) { snprintf(what, n, "could not write %s/%s: %s", work, GATE3_STATE_MARK_WORK, strerror(errno)); return false; }
    return true;
}

/* Put the mark of a working copy back on `work` after a takeover that did not happen. */
static void restore_working(const char *work) {
    char p[2048];
    snprintf(p, sizeof p, "%s/%s", work, GATE3_STATE_MARK_KEPT);
    unlink(p);
    write_mark(work, GATE3_STATE_MARK_WORK, "The working copy of one `gate3 run`; the takeover of its player's state did not happen.\n");
}

/* The end of a `player:<dir>` run (the comment at the top of state.h): `safe` says whether the
 * run's state may replace <dir>.  Returns true when it did, or when it was not safe (nothing
 * moved); false when a takeover was refused or failed, with everything as it was. */
static bool player_finish(const char *start, const char *work, bool safe, const char *how, char *what, size_t n)
{
    const char *dir = start + 7;
    char prev[2100], p[2048], when[64], text[2400];
    snprintf(prev, sizeof prev, "%s.prev", dir);
    if (!safe) {
        snprintf(what, n, "PLAYER STATE NOT TAKEN OVER (the run %s): %s is as it was before this run; this run's state is left at %s", how, dir, work);
        return true;
    }
    if (exists(prev) && !has(prev, GATE3_STATE_MARK_KEPT)) {
        snprintf(what, n, "PLAYER STATE NOT TAKEN OVER (%s exists and has no %s: it is not a state a run took over): %s is as it was before this run; this run's state is left at %s",
                 prev, GATE3_STATE_MARK_KEPT, dir, work);
        return false;
    }
    now_text(when, sizeof when);
    snprintf(text, sizeof text,
             "taken over from a player's run (gate3/state.h)\n"
             "how the run ended: %s\n"
             "the run's start: %s\n"
             "the time: %s\n",
             how, start, when);
    if (!write_mark(work, GATE3_STATE_MARK_KEPT, text)) {
        restore_working(work);
        snprintf(what, n, "PLAYER STATE NOT TAKEN OVER (could not write the mark in %s: %s): %s is as it was before this run; this run's state is left at %s", work, strerror(errno), dir, work);
        return false;
    }
    snprintf(p, sizeof p, "%s/%s", work, GATE3_STATE_MARK_WORK);
    unlink(p);
    if (exists(prev) && !remove_tree(prev)) {
        restore_working(work);
        snprintf(what, n, "PLAYER STATE NOT TAKEN OVER (could not remove %s: %s): %s is as it was before this run; this run's state is left at %s", prev, strerror(errno), dir, work);
        return false;
    }
    const bool had = exists(dir);
    if (had && rename(dir, prev) != 0) {
        const int e = errno;
        restore_working(work);
        snprintf(what, n, "PLAYER STATE NOT TAKEN OVER (rename %s to %s: %s): %s is as it was before this run; this run's state is left at %s", dir, prev, strerror(e), dir, work);
        return false;
    }
    if (rename(work, dir) != 0) {
        const int e = errno;
        if (had && rename(prev, dir) != 0) {
            snprintf(what, n, "PLAYER STATE NOT TAKEN OVER (rename %s to %s: %s; and %s back to %s failed: %s): the state before this run is at %s and this run's state is left at %s",
                     work, dir, strerror(e), prev, dir, strerror(errno), prev, work);
            restore_working(work);
            return false;
        }
        restore_working(work);
        snprintf(what, n, "PLAYER STATE NOT TAKEN OVER (rename %s to %s: %s): %s is as it was before this run; this run's state is left at %s", work, dir, strerror(e), dir, work);
        return false;
    }
    if (had) snprintf(what, n, "PLAYER STATE TAKEN OVER: %s (the state before this run is %s)", dir, prev);
    else snprintf(what, n, "PLAYER STATE TAKEN OVER: %s (there was no state before this run: no %s)", dir, prev);
    return true;
}

bool gate3_state_finish(const char *start, const char *work, const char *keep, bool clean, bool safe, const char *how, char *what, size_t n)
{
    if (start && !strncmp(start, "player:", 7)) return player_finish(start, work, safe, how, what, n);
    const bool asked = keep && *keep;
    if (!asked || !clean) {
        snprintf(what, n, "%s%s%sleft for inspection at %s (the working copy of a run that %s); it is never the start of another run",
                 asked ? "NOT KEPT as " : "", asked ? keep : "", asked ? " (the run did not end cleanly): " : "", work, how);
        return true;
    }
    if (exists(keep)) { snprintf(what, n, "NOT KEPT: %s exists now (it did not when the run began); the state is left at %s", keep, work); return false; }
    char when[64], text[2400], p[2048];
    now_text(when, sizeof when);
    snprintf(text, sizeof text,
             "kept %s from a run that %s\n"
             "the run's start: %s\n"
             "the run's working directory was: %s\n"
             "A state directory of `gate3 run` (gate3/state.h): GATE3_START=save:<this directory> starts a run from a fresh copy of it.\n",
             when, how, start && *start ? start : "new", work);
    if (!write_mark(work, GATE3_STATE_MARK_KEPT, text)) { snprintf(what, n, "NOT KEPT: could not write the mark in %s: %s", work, strerror(errno)); return false; }
    snprintf(p, sizeof p, "%s/%s", work, GATE3_STATE_MARK_WORK);
    unlink(p);
    if (rename(work, keep) != 0) {
        const int e = errno;
        write_mark(work, GATE3_STATE_MARK_WORK, "The working copy of one `gate3 run`; it could not be moved to where it was to be kept.\n");
        snprintf(p, sizeof p, "%s/%s", work, GATE3_STATE_MARK_KEPT);
        unlink(p);
        snprintf(what, n, "NOT KEPT: rename %s to %s: %s (the same volume is needed); the state is left at %s", work, keep, strerror(e), work);
        return false;
    }
    snprintf(what, n, "KEPT as %s (the run %s); GATE3_START=save:%s starts a run from a copy of it", keep, how, keep);
    return true;
}
