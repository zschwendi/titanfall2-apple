/* The test of gate3/state.c (where a run starts and what it leaves behind: gate3/state.h).
 * Plain host files in a scratch directory; no runtime and no file of the game.
 *
 *     state_test <scratch directory>      (made new by the test; `make gate3-state-test`)
 *
 * Every expected value is the rule as gate3/state.h states it (the planner's decision 1 of
 * unit gate3-run-13). */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../state.h"

static unsigned checks, failed;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failed++; printf("state-test: FAILED %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

static char root[1024];
static const char *P(const char *rel) {
    static char buf[8][1400];
    static unsigned k;
    char *b = buf[k++ & 7];
    snprintf(b, 1400, "%s/%s", root, rel);
    return b;
}
static bool there(const char *p) { struct stat st; return lstat(p, &st) == 0; }
static void put(const char *p, const char *text) {
    FILE *f = fopen(p, "w");
    if (!f || fputs(text, f) < 0 || fclose(f) != 0) { printf("state-test: cannot write %s\n", p); exit(2); }
}
static bool holds(const char *p, const char *text) {
    char buf[4000] = "";
    FILE *f = fopen(p, "r");
    if (!f) return false;
    const size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    return strstr(buf, text) != NULL;
}

int main(int argc, char **argv)
{
    if (argc != 2) { fprintf(stderr, "usage: state_test <scratch directory>\n"); return 2; }
    snprintf(root, sizeof root, "%s", argv[1]);
    char cmd[1200], what[1600];
    snprintf(cmd, sizeof cmd, "rm -rf '%s' && mkdir -p '%s'", root, root);
    if (strstr(root, "'") || system(cmd) != 0) { fprintf(stderr, "state_test: cannot make %s new\n", root); return 2; }

    /* 1. `new` is an empty directory with the working copy's mark, and it is new every time. */
    CHECK(gate3_state_prepare("new", P("a.state"), NULL, what, sizeof what), "%s", what);
    CHECK(strncmp(what, "new (", 5) == 0, "%s", what);
    CHECK(there(P("a.state/" GATE3_STATE_MARK_WORK)) && !there(P("a.state/home")), "the working copy is not an empty, marked directory");
    mkdir(P("a.state/home"), 0700);
    put(P("a.state/home/savegame.sav"), "cut off");            /* what a run that was stopped leaves */
    CHECK(gate3_state_prepare(NULL, P("a.state"), NULL, what, sizeof what), "%s", what);
    CHECK(!there(P("a.state/home")), "the last run's files are still in the next run's start");
    CHECK(gate3_state_prepare("", P("a.state"), NULL, what, sizeof what) && strncmp(what, "new (", 5) == 0, "%s", what);

    /* 2. A run's working copy is never a start; nor is a directory without the kept mark. */
    put(P("a.state/file"), "x");
    snprintf(cmd, sizeof cmd, "save:%s", P("a.state"));
    CHECK(!gate3_state_prepare(cmd, P("b.state"), NULL, what, sizeof what) && strstr(what, "never the start of a run"), "%s", what);
    CHECK(!there(P("b.state")), "a refused start made a working copy");
    mkdir(P("plain"), 0700);
    put(P("plain/file"), "x");
    snprintf(cmd, sizeof cmd, "save:%s", P("plain"));
    CHECK(!gate3_state_prepare(cmd, P("b.state"), NULL, what, sizeof what) && strstr(what, "not a state that a run kept"), "%s", what);
    snprintf(cmd, sizeof cmd, "save:%s", P("nowhere"));
    CHECK(!gate3_state_prepare(cmd, P("b.state"), NULL, what, sizeof what) && strstr(what, "is not a directory"), "%s", what);
    CHECK(!gate3_state_prepare("load", P("b.state"), NULL, what, sizeof what) && strstr(what, "a start is `new`"), "%s", what);
    CHECK(!gate3_state_prepare("save:", P("b.state"), NULL, what, sizeof what), "%s", what);

    /* 3. A run that did not end cleanly keeps nothing: the working copy stays, marked, and
     *    nothing is made where the state was to be kept. */
    CHECK(gate3_state_finish("new", P("a.state"), P("kept"), false, false, "ended at a stop", what, sizeof what), "%s", what);
    CHECK(strstr(what, "NOT KEPT as ") && strstr(what, "left for inspection at "), "%s", what);
    CHECK(!there(P("kept")) && there(P("a.state/file")) && there(P("a.state/" GATE3_STATE_MARK_WORK)), "a stopped run's state was kept or its copy removed");
    /*    ... and so does a run that did not ask. */
    CHECK(gate3_state_finish("new", P("a.state"), NULL, true, false, "was ended by the driver", what, sizeof what) && strncmp(what, "left for inspection at ", 23) == 0, "%s", what);
    CHECK(there(P("a.state/file")), "the working copy is gone");

    /* 4. A clean run that asked: the working copy is moved, marked as kept, no longer a working copy. */
    CHECK(gate3_state_finish("new", P("a.state"), P("kept"), true, false, "was ended by the driver at Present 5", what, sizeof what), "%s", what);
    CHECK(strncmp(what, "KEPT as ", 8) == 0, "%s", what);
    CHECK(!there(P("a.state")) && there(P("kept/file")) && there(P("kept/" GATE3_STATE_MARK_KEPT)) && !there(P("kept/" GATE3_STATE_MARK_WORK)), "the kept state is not whole");
    CHECK(holds(P("kept/" GATE3_STATE_MARK_KEPT), "was ended by the driver at Present 5") && holds(P("kept/" GATE3_STATE_MARK_KEPT), "the run's start: new"), "the mark does not say how the run ended and where it started");

    /* 5. A kept state is never written over: asked before the run, and again after it. */
    CHECK(!gate3_state_prepare("new", P("c.state"), P("kept"), what, sizeof what) && strstr(what, "exists already"), "%s", what);
    CHECK(!there(P("c.state")), "a refused run made a working copy");
    CHECK(gate3_state_prepare("new", P("c.state"), P("kept2"), what, sizeof what), "%s", what);
    mkdir(P("kept2"), 0700);                                    /* someone made it while the run went on */
    CHECK(!gate3_state_finish("new", P("c.state"), P("kept2"), true, false, "x", what, sizeof what) && strstr(what, "NOT KEPT"), "%s", what);
    CHECK(there(P("c.state/" GATE3_STATE_MARK_WORK)), "the working copy lost its mark");
    CHECK(!gate3_state_prepare("new", P("c.state"), P("c.state"), what, sizeof what), "%s", what);

    /* 6. `save:<kept>`: a fresh copy each time; the run's changes never reach the kept state. */
    snprintf(cmd, sizeof cmd, "save:%s", P("kept"));
    CHECK(gate3_state_prepare(cmd, P("d.state"), NULL, what, sizeof what), "%s", what);
    CHECK(strncmp(what, "save:", 5) == 0 && strstr(what, "was ended by the driver at Present 5"), "%s", what);
    CHECK(holds(P("d.state/file"), "x") && there(P("d.state/" GATE3_STATE_MARK_WORK)) && !there(P("d.state/" GATE3_STATE_MARK_KEPT)), "the copy is not a marked working copy of the kept state");
    put(P("d.state/file"), "changed by the run");
    put(P("d.state/new-file"), "y");
    CHECK(holds(P("kept/file"), "x") && !holds(P("kept/file"), "changed") && !there(P("kept/new-file")), "the run wrote into the kept state");
    CHECK(gate3_state_prepare(cmd, P("d.state"), NULL, what, sizeof what), "%s", what);
    CHECK(holds(P("d.state/file"), "x") && !holds(P("d.state/file"), "changed") && !there(P("d.state/new-file")), "the second run started from what the first left");
    CHECK(!gate3_state_prepare(cmd, P("kept"), NULL, what, sizeof what), "%s", what);

    /* 7. Something at the working directory's place that the driver did not make is not removed. */
    mkdir(P("e.state"), 0700);
    put(P("e.state/somebody-elses"), "z");
    CHECK(!gate3_state_prepare("new", P("e.state"), NULL, what, sizeof what) && strstr(what, "is in the way"), "%s", what);
    CHECK(there(P("e.state/somebody-elses")), "a directory the driver did not make was removed");
    CHECK(!gate3_state_prepare("new", P("kept"), NULL, what, sizeof what) && there(P("kept/file")), "a kept state was taken as a working directory");
    mkdir(P("f.state"), 0700);                                  /* an empty directory may be used */
    CHECK(gate3_state_prepare("new", P("f.state"), NULL, what, sizeof what), "%s", what);

    /* 8. `player:<dir>`: a player's state carried from run to run (gate3/state.h). */
    snprintf(cmd, sizeof cmd, "player:%s", P("pl"));
    CHECK(gate3_state_prepare(cmd, P("g.state"), NULL, what, sizeof what) && strstr(what, "(no state there yet: the game's first start)"), "%s", what);
    CHECK(there(P("g.state/" GATE3_STATE_MARK_WORK)) && !there(P("pl")), "a player's first start is not a new working copy");
    mkdir(P("g.state/home"), 0700);
    put(P("g.state/home/savegame.sav"), "run one");
    CHECK(gate3_state_finish(cmd, P("g.state"), NULL, true, true, "was ended by the driver at Present 5", what, sizeof what), "%s", what);
    CHECK(strstr(what, "PLAYER STATE TAKEN OVER: ") && strstr(what, "(there was no state before this run"), "%s", what);
    CHECK(there(P("pl/home/savegame.sav")) && holds(P("pl/" GATE3_STATE_MARK_KEPT), "taken over from a player's run") && !there(P("g.state")) && !there(P("pl.prev")), "the first run's state did not become the player's state");

    /*    a second run starts from it; its safe end moves the first run's state to <dir>.prev. */
    CHECK(gate3_state_prepare(cmd, P("g.state"), NULL, what, sizeof what) && strncmp(what, "player:", 7) == 0, "%s", what);
    CHECK(holds(P("g.state/home/savegame.sav"), "run one") && there(P("g.state/" GATE3_STATE_MARK_WORK)) && !there(P("g.state/" GATE3_STATE_MARK_KEPT)), "the second run did not start from the player's state");
    put(P("g.state/home/savegame.sav"), "run two");
    CHECK(gate3_state_finish(cmd, P("g.state"), NULL, true, true, "was ended by the driver at Present 5", what, sizeof what), "%s", what);
    CHECK(strstr(what, "(the state before this run is ") && holds(P("pl/home/savegame.sav"), "run two") && holds(P("pl.prev/home/savegame.sav"), "run one") && there(P("pl.prev/" GATE3_STATE_MARK_KEPT)), "the second run's takeover is wrong");

    /*    a third: <dir>.prev is the second run's, and nothing older is left. */
    CHECK(gate3_state_prepare(cmd, P("g.state"), NULL, what, sizeof what), "%s", what);
    put(P("g.state/home/savegame.sav"), "run three");
    CHECK(gate3_state_finish(cmd, P("g.state"), NULL, true, true, "was ended at a stop", what, sizeof what), "%s", what);
    CHECK(holds(P("pl/home/savegame.sav"), "run three") && holds(P("pl.prev/home/savegame.sav"), "run two") && !holds(P("pl.prev/home/savegame.sav"), "run one") && !there(P("g.state")), "one generation back is not all that is kept");

    /*    a run whose end is not safe moves nothing: <dir> as before, the working copy left with its mark. */
    CHECK(gate3_state_prepare(cmd, P("g.state"), NULL, what, sizeof what), "%s", what);
    put(P("g.state/home/savegame.sav"), "run four, unsafe");
    CHECK(gate3_state_finish(cmd, P("g.state"), NULL, false, false, "was ended by the time limit after 30 s", what, sizeof what), "%s", what);
    CHECK(strstr(what, "PLAYER STATE NOT TAKEN OVER") && strstr(what, "left at ") && strstr(what, P("g.state")), "%s", what);
    CHECK(holds(P("pl/home/savegame.sav"), "run three") && holds(P("pl.prev/home/savegame.sav"), "run two") && there(P("pl.prev/" GATE3_STATE_MARK_KEPT)), "a run that was not safe changed the player's state");
    CHECK(there(P("g.state/" GATE3_STATE_MARK_WORK)) && holds(P("g.state/home/savegame.sav"), "run four, unsafe"), "the unsafe run's state is not left at its working copy");

    /*    a `player:` directory that carries the working copy's mark is refused; so is one with no kept mark. */
    mkdir(P("pl2"), 0700);
    put(P("pl2/" GATE3_STATE_MARK_WORK), "x");
    snprintf(cmd, sizeof cmd, "player:%s", P("pl2"));
    CHECK(!gate3_state_prepare(cmd, P("h.state"), NULL, what, sizeof what) && strstr(what, "never the start of a run") && !there(P("h.state")), "%s", what);
    mkdir(P("pl4"), 0700);
    put(P("pl4/file"), "x");
    snprintf(cmd, sizeof cmd, "player:%s", P("pl4"));
    CHECK(!gate3_state_prepare(cmd, P("h.state"), NULL, what, sizeof what) && !there(P("h.state")), "%s", what);

    /*    `<dir>.prev` without the kept mark: the takeover is refused and nothing moved. */
    mkdir(P("pl3"), 0700);
    put(P("pl3/" GATE3_STATE_MARK_KEPT), "k");
    put(P("pl3/file"), "kept");
    mkdir(P("pl3.prev"), 0700);
    put(P("pl3.prev/file"), "not a player's state");
    snprintf(cmd, sizeof cmd, "player:%s", P("pl3"));
    CHECK(gate3_state_prepare(cmd, P("i.state"), NULL, what, sizeof what), "%s", what);
    CHECK(!gate3_state_finish(cmd, P("i.state"), NULL, true, true, "was ended by the driver at Present 5", what, sizeof what) && strstr(what, "PLAYER STATE NOT TAKEN OVER"), "%s", what);
    CHECK(holds(P("pl3/file"), "kept") && holds(P("pl3.prev/file"), "not a player's state") && there(P("i.state/" GATE3_STATE_MARK_WORK)), "the takeover moved something although <dir>.prev had no kept mark");

    /*    `player:` with GATE3_KEEP_STATE is refused: one or the other. */
    CHECK(!gate3_state_prepare(cmd, P("j.state"), P("keepx"), what, sizeof what) && strstr(what, "together") && !there(P("j.state")) && !there(P("keepx")), "%s", what);

    if (failed) { printf("state-test: FAILED: %u of %u checks\n", failed, checks); return 1; }
    printf("state-test: ok: %u checks (a run's start is a fresh copy; a stopped run's state is never kept and never a start)\n", checks);
    return 0;
}
