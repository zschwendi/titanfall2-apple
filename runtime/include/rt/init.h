/* Starting the runtime (docs/RUNTIME.md appendix A).
 *
 *     RtConfig cfg = { .state_dir = "...", .install_root = "...", .tso = RT_TSO_REQUIRE };
 *     rt_init(&cfg);
 *     Ctx *ctx = rt_main_ctx();                      // guest thread 1 = the calling host thread
 *     RtModule *tier0 = rt_module_load(ctx, u"tier0.dll", 0);
 *     GUESTFN f = rt_module_export(tier0, "Plat_FloatTime", 0);
 *     double t = rt_guest_call_fp(ctx, f, NULL, 0);
 */
#ifndef RT_INIT_H
#define RT_INIT_H

#include "rt/module.h"
#include "rt/thread.h"

/* The x86 memory-ordering mode (thread_set_x86_64_compat) for guest threads, section 6.1.
 * The environment variable RT_TSO (require, try, off) overrides the configuration. */
typedef enum {
    RT_TSO_REQUIRE = 0,   /* the call must succeed; fatal otherwise.  For the entitled app bundle */
    RT_TSO_TRY,           /* call it and carry on if refused.  For tests run as plain programs */
    RT_TSO_OFF            /* do not call it */
} RtTsoMode;

typedef struct RtConfig {
    const char *state_dir;        /* host directory behind C:\Users\Player and the Temp directory */
    const char *install_root;     /* host directory behind C:\Titanfall2 */
    RtTsoMode   tso;

    /* The arguments for the guest's command line, UTF-8: argv[0 .. argc-1] are appended, in
     * order and quoted as the Microsoft C runtime parses them, after the program path
     * "C:\Titanfall2\Titanfall2.exe", which the runtime supplies itself (section 8.2).
     * argv does NOT begin with a program name: a host main() passes argc - 1 and argv + 1.
     * argc 0 (argv may then be NULL) gives a command line that is the program path alone.
     * Fatal in rt_init: argc below 0; argc above 0 with argv NULL; an argument that is not
     * valid UTF-8.  The strings are copied; they need not outlive rt_init. */
    int         argc;
    char      **argv;

    /* Guest environment variables set by the embedding program: NULL, or a NULL-terminated
     * list of UTF-8 strings "NAME=value" (the layout of environ, no prefix), so that an app
     * bundle can configure the guest without depending on variables of the host
     * environment.  rt_init applies them after the fixed set of section 8.2 and BEFORE the
     * host's RT_GUEST_<NAME> variables, which therefore win over this list, as RT_TSO wins
     * over `tso`.  Every entry sets its variable (an empty value sets it to the empty
     * string; nothing removes a variable); the rules and the fatal cases are those of
     * rt_env_apply_config (rt/env.h).  rt_init copies the names and values into the
     * environment and does not read the list again. */
    const char *const *env_overrides;

    /* Shims and native modules that are not part of librt.a, in the format of the
     * generated tables.  For tests (the `rttest` library) and for embedding programs. */
    const RtModuleDef *extra_modules;
    unsigned           n_extra_modules;
    const RtShimEntry *extra_shims;
    unsigned           n_extra_shims;

    /* Called by rt_init after every native module and stub exists and just before the
     * dispatch table becomes read-only: the last moment at which rt_native_import can
     * create a trap stub and rt_native_stub or rt_disp_add can add an entry. */
    void (*before_seal)(void *user);
    void  *user;
} RtConfig;

/* Start the runtime on the calling host thread, which becomes guest thread 1.  In order:
 * logging; reserve the two arenas; machine description, process block and clocks; process
 * heap and handle table; path roots, standard handles, environment, registry, locale; the
 * module registry with every base assigned and the native modules from the shim tables;
 * the loader's pre-scan (trap stubs for every import of every lifted module);
 * cfg->before_seal; seal the dispatch table; attach the main thread (thread block, guest
 * stack, ordering mode).  Fatal on any failure.  Call it once.
 *
 * Attaching the main thread is the last step, so no init function (rt_<name>_init, and
 * before_seal) has a guest thread: no Ctx, rt_thread_self() is NULL, the pseudo-handles are
 * fatal and critical sections cannot be entered.  rt/thread.h has the full list. */
void rt_init(const RtConfig *cfg);

/* Start-up hooks of subsystems that are not built yet and have no file of their own.
 * runtime/core/init.c calls each at its place in the sequence and holds a weak definition
 * that does nothing; the unit that builds the subsystem defines the function in its own
 * file (a strong definition replaces the weak one at link time) and edits nothing else.
 *   rt_crash_init     signal handlers and the alternate signal stack (section 9.3)
 *   rt_env_init       the environment block and the command line (section 8.2)
 *   rt_file_init      the standard handles (section 7.3); runs after the handle table and paths
 *   rt_registry_init  the registry tree, loaded from the state directory (section 8.3)
 *   rt_locale_init    code page and case tables (section 8.4)
 *   rt_unwind_init    the unwinder's view of the lifted modules' unwind tables (section 9.3);
 *                     runs after the loader's pre-scan */
void rt_crash_init(void);
void rt_env_init(void);
void rt_file_init(void);
void rt_registry_init(void);
void rt_locale_init(void);
void rt_unwind_init(void);

/* The configuration rt_init was given (a copy). */
const RtConfig *rt_config(void);

/* Ids handed out at start-up.  Fixed so that runs are comparable. */
#define RT_PROCESS_ID       0x1000u
#define RT_FIRST_THREAD_ID  0x1004u

#endif /* RT_INIT_H */
