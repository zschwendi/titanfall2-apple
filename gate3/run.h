/* `gate3 run` and `gate3 summary` (gate3/run.c). */
#ifndef GATE3_RUN_H
#define GATE3_RUN_H

/* argv: nothing, or "--" and the game's arguments.  Returns the exit status: 0 when
 * LauncherMain returned, 1 when the run ended any other way, 2 for a usage error. */
int gate3_mode_run(int argc, char **argv);

/* Print the summary of a run from its log; `report_path` (may be NULL) is the file that got
 * what the run wrote to descriptor 2. */
int gate3_mode_summary(const char *log_path, const char *report_path);

#endif
