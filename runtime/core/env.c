/* The guest's environment block, command line and start-up information (docs/RUNTIME.md
 * section 8.2).  Interface and the Windows rules it follows: rt/env.h.
 *
 * The environment is a table of "NAME=value" entries in the order they were added, kept in
 * one UTF-16 buffer laid out exactly like the block the guest receives: each entry ends in a
 * NUL, and the buffer is followed by one more NUL.  The table is host memory, so malloc is
 * fine for it.  Every block given to the guest is a copy in guest memory from rt_vm_alloc, and
 * is kept in a list until FreeEnvironmentStrings releases it.
 *
 * One mutex guards the table and the list of blocks.  The command line and the start-up
 * information are written once by rt_env_init and never change after it.
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core.h"
#include "rt/env.h"
#include "rt/machine.h"
#include "rt/vm.h"
#include "rt/win/error.h"
#include "rt/win/memory.h"

#define GUEST_PREFIX   "RT_GUEST_"                          /* host overrides, rt_env_apply_overrides */
#define PROGRAM_PATH   "C:\\Titanfall2\\Titanfall2.exe"     /* section 8.2 */

extern char **environ;      /* the host's environment: read for RT_GUEST_ overrides only */

/* The fixed set (section 8.2).  Every directory named here is one that the path table maps
 * (rt/path.h), so a program that follows a value finds a directory that exists.  Left out on
 * purpose: APPDATA, LOCALAPPDATA, ProgramData, ProgramFiles, ComSpec and the rest of the
 * Windows set, whose directories the path table does not map.  Add them when the path table
 * does.  NUMBER_OF_PROCESSORS and PROCESSOR_LEVEL come from the machine description and are
 * added in rt_env_init. */
static const struct { const char *name, *value; } fixed[] = {
    { "COMPUTERNAME",           "PLAYER-PC" },
    { "HOMEDRIVE",              "C:" },
    { "HOMEPATH",               "\\Users\\Player" },
    { "OS",                     "Windows_NT" },
    { "Path",                   "C:\\Windows\\system32;C:\\Windows" },
    { "PATHEXT",                ".COM;.EXE;.BAT;.CMD;.VBS;.VBE;.JS;.JSE;.WSF;.WSH;.MSC" },
    { "PROCESSOR_ARCHITECTURE", "AMD64" },
    { "SystemDrive",            "C:" },
    { "SystemRoot",             "C:\\Windows" },
    { "TEMP",                   "C:\\Users\\Player\\AppData\\Local\\Temp" },
    { "TMP",                    "C:\\Users\\Player\\AppData\\Local\\Temp" },
    { "USERDOMAIN",             "PLAYER-PC" },
    { "USERNAME",               "Player" },
    { "USERPROFILE",            "C:\\Users\\Player" },
    { "windir",                 "C:\\Windows" },
};

typedef struct Block {
    uint64_t      addr;         /* guest address, the region base returned by rt_vm_alloc */
    struct Block *next;
} Block;

static pthread_mutex_t env_lock = PTHREAD_MUTEX_INITIALIZER;
static bool     ready;          /* set by rt_env_init, before any other thread can call in */
static WCHAR   *tab;            /* "NAME=value\0" entries back to back; guarded by env_lock */
static size_t   tab_len;        /* WCHARs in use */
static size_t   tab_cap;        /* WCHARs allocated */
static Block   *blocks;         /* live blocks from GetEnvironmentStrings; guarded by env_lock */

static WCHAR   *cmdline_w;      /* set by rt_env_init */
static CHAR    *cmdline_a;      /* NULL if the command line is not ASCII ... */
static char     cmdline_a_problem[128];   /* ... and this says why */
static STARTUPINFOW startup;

/* ---- small helpers ------------------------------------------------------------------------- */

static void require_ready(const char *call) {
    if (!ready) rt_fatal("%s before rt_env_init", call);
}

static void require_err(const DWORD *err, const char *call) {
    if (!err) rt_fatal("%s: NULL error out-parameter", call);
}

static size_t wlen(const WCHAR *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

/* ASCII case folding, the only folding this runtime does (see check_name). */
static WCHAR fold(WCHAR c) {
    return (c >= 'a' && c <= 'z') ? (WCHAR)(c - 'a' + 'A') : c;
}

/* A string for a message: ASCII as is, anything else as '?'.  At most cap-1 characters. */
static void narrow(char *dst, size_t cap, const WCHAR *s, size_t n) {
    size_t k = 0;
    for (size_t i = 0; i < n && k + 1 < cap; i++)
        dst[k++] = (s[i] > 0 && s[i] < 0x80) ? (char)s[i] : '?';
    dst[k] = 0;
}

/* UTF-8 bytes to UTF-16, strictly.  Overlong forms, surrogates, code points above U+10FFFF and
 * truncated sequences are fatal, naming the byte.  Host argv and environment strings are
 * UTF-8 on macOS.  Returns a malloc'd, NUL-terminated buffer; *n gets the units before the
 * terminator.  A UTF-8 sequence of k bytes gives at most k units, so len + 1 units suffice. */
static WCHAR *utf8_to_w(const char *s, size_t len, const char *what, size_t *n) {
    WCHAR *out = malloc((len + 1) * sizeof *out);
    if (!out) rt_fatal("out of memory converting %s", what);
    const unsigned char *p = (const unsigned char *)s;
    size_t k = 0, i = 0;
    while (i < len) {
        unsigned c = p[i];
        if (c < 0x80) { out[k++] = (WCHAR)c; i++; continue; }

        uint32_t cp, min;
        unsigned extra;
        if ((c & 0xE0) == 0xC0)      { cp = c & 0x1F; extra = 1; min = 0x80; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; min = 0x800; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; min = 0x10000; }
        else rt_fatal("%s is not valid UTF-8: byte %#x at offset %zu", what, c, i);

        if (i + extra >= len) rt_fatal("%s is not valid UTF-8: a sequence starting with byte %#x at offset %zu is cut short",
                                       what, c, i);
        for (unsigned j = 1; j <= extra; j++) {
            unsigned b = p[i + j];
            if ((b & 0xC0) != 0x80) rt_fatal("%s is not valid UTF-8: byte %#x at offset %zu continues a sequence "
                                             "wrongly", what, b, i + j);
            cp = (cp << 6) | (b & 0x3F);
        }
        if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
            rt_fatal("%s is not valid UTF-8: the sequence at offset %zu encodes U+%04X, which is not a character",
                     what, i, (unsigned)cp);
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out[k++] = (WCHAR)(0xD800 | (cp >> 10));
            out[k++] = (WCHAR)(0xDC00 | (cp & 0x3FF));
        } else {
            out[k++] = (WCHAR)cp;
        }
        i += extra + 1;
    }
    out[k] = 0;
    *n = k;
    return out;
}

/* A name or value that came through an A function: each byte must be ASCII, because the A
 * functions carry ASCII until the locale unit converts code pages.  Returns a malloc'd buffer. */
static WCHAR *widen_a(const CHAR *s, const char *call, const char *what) {
    if (!s) rt_fatal("%s: %s is NULL", call, what);
    size_t n = strlen(s);
    WCHAR *w = malloc((n + 1) * sizeof *w);
    if (!w) rt_fatal("out of memory in %s", call);
    for (size_t i = 0; i <= n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c >= 0x80) {
            free(w);
            rt_fatal("%s: %s has byte %#x at offset %zu, which is not ASCII; code page conversion needs the locale "
                     "unit (rt-core-locale)", call, what, (unsigned)c, i);
        }
        w[i] = c;
    }
    return w;
}

/* Memory for the guest: a region from rt_vm_alloc.  NULL if none is left. */
static void *guest_alloc_or_null(uint64_t bytes) {
    uint64_t p = rt_vm_alloc(0, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    return p ? (void *)(uintptr_t)p : NULL;
}

/* ---- the environment table ------------------------------------------------------------------ */

/* Checks a variable name before use.  NULL is a guest bug (Windows faults on it).  A '=' after
 * the first character cannot be told apart from the NAME=value separator.  A non-ASCII name
 * cannot be case-folded the way Windows folds it: that needs the locale unit's upper-case
 * table.  All three are fatal.  A '=' as the first character is part of the name, as on
 * Windows: the C runtime's _chdir sets "=C:" to the new directory after every
 * SetCurrentDirectory, and the block carries it as "=C:=C:\dir".  Nothing here gives such a
 * variable a meaning; it is stored and returned like any other.  The empty name is allowed
 * here; the callers say what it means. */
static void check_name(const WCHAR *name, const char *call) {
    if (!name) rt_fatal("%s: NULL variable name", call);
    size_t n = wlen(name);
    for (size_t i = 0; i < n; i++) {
        if (name[i] == '=' && i == 0) continue;
        if (name[i] != '=' && name[i] < 0x80) continue;
        char shown[96];
        narrow(shown, sizeof shown, name, n);
        if (name[i] == '=')
            rt_fatal("%s: variable name \"%s\" contains '=' after its first character; such a name cannot "
                     "be told from NAME=value", call, shown);
        rt_fatal("%s: variable name \"%s\" has U+%04X, which is not ASCII; case folding for it needs the locale "
                 "unit (rt-core-locale), which is not built", call, shown, (unsigned)name[i]);
    }
}

/* Find the entry whose name equals `name` (n units, already checked) ignoring ASCII case.
 * On success: *at is its offset in tab, *entry_len its length including the NUL, and *value
 * and *vlen describe its value.  Entries always have a name of at least one unit, a '=' after
 * it and a NUL, so the scan is safe; it starts at 1 because a name may begin with '='. */
static bool find_locked(const WCHAR *name, size_t n, size_t *at, size_t *entry_len,
                        const WCHAR **value, size_t *vlen) {
    size_t pos = 0;
    while (pos < tab_len) {
        const WCHAR *e = tab + pos;
        size_t eq = 1;
        while (e[eq] != '=') eq++;
        size_t len = eq;
        while (e[len]) len++;                       /* len: the entry without its NUL */
        if (eq == n) {
            size_t i = 0;
            while (i < n && fold(e[i]) == fold(name[i])) i++;
            if (i == n) {
                *at = pos;
                *entry_len = len + 1;
                *value = e + eq + 1;
                *vlen = len - eq - 1;
                return true;
            }
        }
        pos += len + 1;
    }
    return false;
}

/* Replace the `old_len` units at `at` with the `new_len` units at `entry` (either may be 0).
 * Deleting is old_len > 0 with new_len 0; adding is at = tab_len with old_len 0. */
static void table_splice_locked(size_t at, size_t old_len, const WCHAR *entry, size_t new_len) {
    size_t tail = tab_len - at - old_len;
    size_t total = tab_len - old_len + new_len;
    if (total > tab_cap) {
        size_t cap = tab_cap ? tab_cap : 256;
        while (cap < total) cap *= 2;
        WCHAR *t = realloc(tab, cap * sizeof *tab);
        if (!t) rt_fatal("out of memory: the environment needs %zu characters", total);
        tab = t;
        tab_cap = cap;
    }
    if (tail && new_len != old_len) memmove(tab + at + new_len, tab + at + old_len, tail * sizeof *tab);
    if (new_len) memcpy(tab + at, entry, new_len * sizeof *tab);
    tab_len = total;
}

/* Set the variable.  `name` is checked and non-empty; `value` has vn <= RT_ENV_VALUE_MAX units.
 * An existing variable keeps its place and its spelling; a new one goes at the end.  The Windows
 * documentation says nothing about either order, so this is our rule (not verified). */
static void set_locked(const WCHAR *name, size_t n, const WCHAR *value, size_t vn) {
    size_t at, old_len, old_vn;
    const WCHAR *old_value;
    bool found = find_locked(name, n, &at, &old_len, &old_value, &old_vn);
    const WCHAR *spelling = found ? tab + at : name;
    size_t total = n + 1 + vn + 1;
    WCHAR *e = malloc(total * sizeof *e);
    if (!e) rt_fatal("out of memory setting variable of %zu characters", n);
    memcpy(e, spelling, n * sizeof *e);
    e[n] = '=';
    memcpy(e + n + 1, value, vn * sizeof *e);
    e[total - 1] = 0;
    if (found) table_splice_locked(at, old_len, e, total);
    else       table_splice_locked(tab_len, 0, e, total);
    free(e);
}

/* Set a variable whose text is ASCII, from the start-up code. */
static void put_ascii_locked(const char *name, const char *value) {
    size_t nn, vn;
    WCHAR *n = utf8_to_w(name, strlen(name), "a built-in variable name", &nn);
    WCHAR *v = utf8_to_w(value, strlen(value), "a built-in variable value", &vn);
    set_locked(n, nn, v, vn);
    free(n);
    free(v);
}

/* The entries of a "NAME=value" list that begin with `prefix` each set the guest variable
 * named by the rest of the name; the others are skipped.  Prefix RT_GUEST_: the overrides of
 * a host environment list.  Prefix "": every entry, the list of RtConfig.env_overrides.
 * (See rt/env.h for both.) */
static void apply_overrides_locked(const char *const *entries, const char *prefix) {
    if (!entries) return;
    const size_t plen = strlen(prefix);
    for (size_t i = 0; entries[i]; i++) {
        const char *e = entries[i];
        if (strncmp(e, prefix, plen) != 0) continue;
        const char *name = e + plen;
        const char *eq = strchr(name, '=');
        /* The entry is echoed at most 80 bytes: a value can be as long as the limit. */
        if (!eq) rt_fatal("environment override \"%.80s\" has no '='", e);
        if (eq == name) rt_fatal("environment override \"%.80s\" names no variable", e);
        size_t nn, vn;
        WCHAR *nw = utf8_to_w(name, (size_t)(eq - name), "an environment override name", &nn);
        WCHAR *vw = utf8_to_w(eq + 1, strlen(eq + 1), "an environment override value", &vn);
        check_name(nw, "environment override");
        if (vn > RT_ENV_VALUE_MAX)
            rt_fatal("environment override \"%.80s\" has a value of %zu characters; the limit is %u",
                     e, vn, (unsigned)RT_ENV_VALUE_MAX);
        set_locked(nw, nn, vw, vn);
        free(nw);
        free(vw);
    }
}

/* Fatal, naming the character, if the environment has one that the A block cannot carry. */
static void require_ascii_block_locked(void) {
    for (size_t i = 0; i < tab_len; i++) {
        if (tab[i] < 0x80) continue;
        size_t start = i;
        while (start > 0 && tab[start - 1] != 0) start--;
        size_t eq = start + 1;
        while (tab[eq] != '=') eq++;
        char name[96];
        narrow(name, sizeof name, tab + start, eq - start);
        rt_fatal("GetEnvironmentStringsA: variable %s has U+%04X in it, which is not ASCII; the A block cannot carry "
                 "it until the locale unit converts code pages", name, (unsigned)tab[i]);
    }
}

/* ---- the process's start-up data ---------------------------------------------------------- */

/* The command line.  Arguments come from RtConfig.argv (UTF-8, without the host program's
 * name); the program path is always first, always quoted.  The W form is built in guest memory
 * first; the A form is built only if every character is ASCII, and otherwise its absence is
 * fatal when GetCommandLineA asks for it. */
static void build_command_line_locked(void) {
    const RtConfig *cfg = rt_config();
    if (cfg->argc < 0) rt_fatal("rt_init: argc is %d", cfg->argc);
    if (cfg->argc > 0 && !cfg->argv) rt_fatal("rt_init: argc is %d but argv is NULL", cfg->argc);
    DWORD nargs = (DWORD)cfg->argc;

    size_t pn;
    WCHAR *program = utf8_to_w(PROGRAM_PATH, strlen(PROGRAM_PATH), "the program path", &pn);
    WCHAR **args = calloc(nargs + 1, sizeof *args);
    if (!args) rt_fatal("out of memory for the command line");
    for (DWORD i = 0; i < nargs; i++) {
        char what[64];
        snprintf(what, sizeof what, "host program argument %u", (unsigned)i);
        size_t n;
        args[i] = utf8_to_w(cfg->argv[i], strlen(cfg->argv[i]), what, &n);
    }

    SIZE_T need = rt_cmdline_build_w(program, (const WCHAR *const *)args, nargs, NULL, 0);
    cmdline_w = guest_alloc_or_null(need * sizeof(WCHAR));
    if (!cmdline_w) rt_fatal("cannot allocate the command line (%llu characters)", (unsigned long long)need);
    rt_cmdline_build_w(program, (const WCHAR *const *)args, nargs, cmdline_w, need);

    size_t bad = 0;
    while (bad < need - 1 && cmdline_w[bad] < 0x80) bad++;
    if (bad == need - 1) {
        cmdline_a = guest_alloc_or_null(need);
        if (!cmdline_a) rt_fatal("cannot allocate the command line (%llu bytes)", (unsigned long long)need);
        for (size_t i = 0; i < need; i++) cmdline_a[i] = (CHAR)cmdline_w[i];
    } else {
        snprintf(cmdline_a_problem, sizeof cmdline_a_problem,
                 "the command line has U+%04X at character %zu, which is not ASCII; the A form cannot carry it until "
                 "the locale unit converts code pages", (unsigned)cmdline_w[bad], bad);
    }

    for (DWORD i = 0; i < nargs; i++) free(args[i]);
    free(args);
    free(program);
}

/* ---- start-up and overrides ---------------------------------------------------------------- */

void rt_env_init(void) {
    if (ready) rt_fatal("rt_env_init called twice");
    const RtMachine *m = rt_machine();
    pthread_mutex_lock(&env_lock);
    for (size_t i = 0; i < sizeof fixed / sizeof fixed[0]; i++) put_ascii_locked(fixed[i].name, fixed[i].value);
    char num[16];
    snprintf(num, sizeof num, "%u", (unsigned)m->cpu_count);
    put_ascii_locked("NUMBER_OF_PROCESSORS", num);
    snprintf(num, sizeof num, "%u", (unsigned)m->cpu_family);
    put_ascii_locked("PROCESSOR_LEVEL", num);

    /* After the fixed set, so an override wins: first the embedding program's list, then
     * the host's RT_GUEST_ variables, which therefore win over both (as RT_TSO wins over
     * RtConfig.tso: what the person at the terminal says goes). */
    apply_overrides_locked(rt_config()->env_overrides, "");
    apply_overrides_locked((const char *const *)environ, GUEST_PREFIX);
    build_command_line_locked();

    memset(&startup, 0, sizeof startup);   /* see rt_env_startup_info for each zero */
    startup.cb = (DWORD)sizeof startup;
    ready = true;
    pthread_mutex_unlock(&env_lock);
}

void rt_env_apply_overrides(char *const *entries) {
    require_ready("rt_env_apply_overrides");
    pthread_mutex_lock(&env_lock);
    apply_overrides_locked((const char *const *)entries, GUEST_PREFIX);
    pthread_mutex_unlock(&env_lock);
}

void rt_env_apply_config(const char *const *entries) {
    require_ready("rt_env_apply_config");
    pthread_mutex_lock(&env_lock);
    apply_overrides_locked(entries, "");
    pthread_mutex_unlock(&env_lock);
}

/* ---- GetEnvironmentVariable and SetEnvironmentVariable ------------------------------------- */

DWORD rt_env_get_w(const WCHAR *name, WCHAR *buf, DWORD size, DWORD *err) {
    require_ready("GetEnvironmentVariableW");
    require_err(err, "GetEnvironmentVariableW");
    check_name(name, "GetEnvironmentVariableW");
    *err = 0;
    size_t n = wlen(name);

    pthread_mutex_lock(&env_lock);
    size_t at, elen, vn;
    const WCHAR *value;
    if (!find_locked(name, n, &at, &elen, &value, &vn)) {
        pthread_mutex_unlock(&env_lock);
        *err = ERROR_ENVVAR_NOT_FOUND;
        return 0;
    }
    DWORD need = (DWORD)(vn + 1);
    /* Too small, or a size query: nothing is written.  The documentation says the contents of
     * the buffer are then undefined; writing nothing is our choice. */
    if (size < need) {
        pthread_mutex_unlock(&env_lock);
        return need;
    }
    if (!buf) {
        pthread_mutex_unlock(&env_lock);
        rt_fatal("GetEnvironmentVariableW: NULL buffer of %u characters for a value of %zu",
                 (unsigned)size, vn);
    }
    memcpy(buf, value, vn * sizeof *buf);
    buf[vn] = 0;
    pthread_mutex_unlock(&env_lock);
    /* vn is 0 for an empty value: a success with *err 0.  The shim leaves the last error alone
     * on this path as on every other success (rt/env.h, rt_env_get_w); this file does not touch
     * thread state. */
    return (DWORD)vn;
}

DWORD rt_env_get_a(const CHAR *name, CHAR *buf, DWORD size, DWORD *err) {
    require_ready("GetEnvironmentVariableA");
    require_err(err, "GetEnvironmentVariableA");
    WCHAR *wname = widen_a(name, "GetEnvironmentVariableA", "the variable name");
    check_name(wname, "GetEnvironmentVariableA");
    size_t n = wlen(wname);
    *err = 0;

    pthread_mutex_lock(&env_lock);
    size_t at, elen, vn;
    const WCHAR *value;
    if (!find_locked(wname, n, &at, &elen, &value, &vn)) {
        pthread_mutex_unlock(&env_lock);
        free(wname);
        *err = ERROR_ENVVAR_NOT_FOUND;
        return 0;
    }
    for (size_t i = 0; i < vn; i++) {
        if (value[i] >= 0x80) {
            pthread_mutex_unlock(&env_lock);
            rt_fatal("GetEnvironmentVariableA: the value of %s has U+%04X, which is not ASCII; code page conversion "
                     "needs the locale unit (rt-core-locale)", name, (unsigned)value[i]);
        }
    }
    DWORD need = (DWORD)(vn + 1);
    if (size < need) {
        pthread_mutex_unlock(&env_lock);
        free(wname);
        return need;
    }
    if (!buf) {
        pthread_mutex_unlock(&env_lock);
        free(wname);
        rt_fatal("GetEnvironmentVariableA: NULL buffer of %u bytes for a value of %zu", (unsigned)size, vn);
    }
    for (size_t i = 0; i < vn; i++) buf[i] = (CHAR)value[i];
    buf[vn] = 0;
    pthread_mutex_unlock(&env_lock);
    free(wname);
    return (DWORD)vn;
}

BOOL rt_env_set_w(const WCHAR *name, const WCHAR *value, DWORD *err) {
    require_ready("SetEnvironmentVariableW");
    require_err(err, "SetEnvironmentVariableW");
    check_name(name, "SetEnvironmentVariableW");
    *err = 0;
    size_t n = wlen(name);
    size_t vn = value ? wlen(value) : 0;
    /* The error codes below are our choice where the documentation is silent: the empty name
     * and the over-long value (ERROR_INVALID_PARAMETER, not verified); deleting what is not
     * there (ERROR_ENVVAR_NOT_FOUND, as GetEnvironmentVariable reports a missing name). */
    if (n == 0) {
        *err = value ? ERROR_INVALID_PARAMETER : ERROR_ENVVAR_NOT_FOUND;
        return FALSE;
    }
    if (value && vn > RT_ENV_VALUE_MAX) {
        *err = ERROR_INVALID_PARAMETER;
        return FALSE;
    }

    pthread_mutex_lock(&env_lock);
    size_t at, elen, old_vn;
    const WCHAR *old_value;
    bool found = find_locked(name, n, &at, &elen, &old_value, &old_vn);
    if (value) {
        set_locked(name, n, value, vn);
    } else if (found) {
        table_splice_locked(at, elen, NULL, 0);
    }
    pthread_mutex_unlock(&env_lock);

    if (!value && !found) {
        *err = ERROR_ENVVAR_NOT_FOUND;
        return FALSE;
    }
    return TRUE;
}

BOOL rt_env_set_a(const CHAR *name, const CHAR *value, DWORD *err) {
    require_ready("SetEnvironmentVariableA");
    require_err(err, "SetEnvironmentVariableA");
    WCHAR *wn = widen_a(name, "SetEnvironmentVariableA", "the variable name");
    WCHAR *wv = value ? widen_a(value, "SetEnvironmentVariableA", "the value") : NULL;
    BOOL ok = rt_env_set_w(wn, wv, err);
    free(wn);
    free(wv);
    return ok;
}

/* ---- ExpandEnvironmentStrings ----------------------------------------------------------------- */

#define EXPAND_MAX 32767u       /* bytes of a source and of a result, terminator not counted */

/* The value of the variable whose name is the `n` ASCII bytes at `name`, ignoring case. */
static bool find_bytes_locked(const CHAR *name, size_t n, WCHAR *scratch, const WCHAR **value, size_t *vn) {
    size_t at, elen;
    for (size_t i = 0; i < n; i++) scratch[i] = (WCHAR)(unsigned char)name[i];
    return find_locked(scratch, n, &at, &elen, value, vn);
}

DWORD rt_env_expand_a(const CHAR *src, CHAR *dst, DWORD size) {
    static const char api[] = "ExpandEnvironmentStringsA";
    require_ready(api);
    if (!src) rt_fatal("%s: NULL source string; what Windows returns for it is not verified", api);
    const size_t slen = strlen(src);
    if (slen > EXPAND_MAX) rt_fatal("%s: a source string of %zu bytes; the limit is %u", api, slen, EXPAND_MAX);
    for (size_t i = 0; i < slen; i++)
        if ((unsigned char)src[i] >= 0x80)
            rt_fatal("%s: the source string has byte 0x%02X, which is not ASCII; code page conversion needs the "
                     "locale unit (rt-core-locale)", api, (unsigned)(unsigned char)src[i]);

    /* The result is built whole before anything is written to dst.  A piece is never longer
     * than EXPAND_MAX (a source run) or RT_ENV_VALUE_MAX (a value), and building stops at the
     * first piece that takes the result past EXPAND_MAX. */
    const size_t cap = (size_t)EXPAND_MAX + RT_ENV_VALUE_MAX + 2;
    CHAR *out = malloc(cap);
    WCHAR *scratch = malloc((slen + 1) * sizeof *scratch);
    if (!out || !scratch) rt_fatal("%s: no host memory", api);
    size_t len = 0, pos = 0;
    enum { OK, NOT_ASCII, TWO_READINGS } stop = OK;
    unsigned bad_unit = 0;
    size_t bad_at = 0, bad_n = 0;                   /* the name the report shows */

    pthread_mutex_lock(&env_lock);
    while (pos < slen && len <= EXPAND_MAX && stop == OK) {
        const CHAR *piece = src + pos;
        size_t n;
        if (*piece != '%') {                        /* text up to the next reference */
            const CHAR *pct = memchr(piece, '%', slen - pos);
            n = pct ? (size_t)(pct - piece) : slen - pos;
            memcpy(out + len, piece, n);
            len += n;
            pos += n;
            continue;
        }
        const CHAR *close = memchr(piece + 1, '%', slen - pos - 1);
        if (!close) {                               /* a `%` with no partner: the rest as it is */
            n = slen - pos;
            memcpy(out + len, piece, n);
            len += n;
            pos += n;
            continue;
        }
        const size_t name_n = (size_t)(close - piece) - 1;
        const WCHAR *value;
        size_t vn;
        if (find_bytes_locked(piece + 1, name_n, scratch, &value, &vn)) {
            for (size_t i = 0; i < vn && stop == OK; i++) {
                if (value[i] >= 0x80) { stop = NOT_ASCII; bad_unit = value[i]; bad_at = pos + 1; bad_n = name_n; }
                else out[len + i] = (CHAR)value[i];
            }
            len += vn;
        } else {
            /* Not a variable: the reference stays.  If the text after it, up to the next `%`,
             * names a variable, Windows may read this reference's second `%` as the start of
             * that one: not verified, so not answered. */
            const size_t after = pos + name_n + 2;
            const CHAR *next = memchr(src + after, '%', slen - after);
            if (next && find_bytes_locked(src + after, (size_t)(next - (src + after)), scratch, &value, &vn)) {
                stop = TWO_READINGS; bad_at = after; bad_n = (size_t)(next - (src + after));
            }
            memcpy(out + len, piece, name_n + 2);
            len += name_n + 2;
        }
        pos += name_n + 2;
    }
    pthread_mutex_unlock(&env_lock);

    if (stop != OK) {
        char shown[96];
        snprintf(shown, sizeof shown, "%.*s", (int)(bad_n < 80 ? bad_n : 80), src + bad_at);
        if (stop == NOT_ASCII)
            rt_fatal("%s: the value of %s has U+%04X, which is not ASCII; code page conversion needs the locale "
                     "unit (rt-core-locale)", api, shown, bad_unit);
        rt_fatal("%s: a reference to a variable that does not exist is followed by \"%s%%\", and %s is a variable: "
                 "whether Windows expands it is not verified", api, shown, shown);
    }
    if (len > EXPAND_MAX) rt_fatal("%s: a result of more than %u bytes; what Windows returns is not verified", api, EXPAND_MAX);

    /* The A form's sizes: one byte more than the text and its terminator. */
    DWORD ret;
    if ((uint64_t)size >= (uint64_t)len + 2) {
        if (!dst) rt_fatal("%s: NULL buffer of %u bytes for a result of %zu", api, (unsigned)size, len);
        memcpy(dst, out, len);
        dst[len] = 0;
        ret = (DWORD)len + 1;
    } else {
        if (dst) dst[0] = 0;
        ret = (DWORD)len + 2;
    }
    free(scratch);
    free(out);
    return ret;
}

/* ---- environment blocks ----------------------------------------------------------------------- */

static void block_track_locked(uint64_t addr) {
    Block *b = malloc(sizeof *b);
    if (!b) rt_fatal("out of memory recording an environment block");
    b->addr = addr;
    b->next = blocks;
    blocks = b;
}

WCHAR *rt_env_block_w(DWORD *err) {
    require_ready("GetEnvironmentStringsW");
    require_err(err, "GetEnvironmentStringsW");
    *err = 0;
    pthread_mutex_lock(&env_lock);
    WCHAR *b = guest_alloc_or_null(((uint64_t)tab_len + 1) * sizeof *b);
    if (!b) {
        pthread_mutex_unlock(&env_lock);
        *err = ERROR_NOT_ENOUGH_MEMORY;
        return NULL;
    }
    if (tab_len) memcpy(b, tab, tab_len * sizeof *b);
    b[tab_len] = 0;
    block_track_locked((uint64_t)(uintptr_t)b);
    pthread_mutex_unlock(&env_lock);
    return b;
}

CHAR *rt_env_block_a(DWORD *err) {
    require_ready("GetEnvironmentStringsA");
    require_err(err, "GetEnvironmentStringsA");
    *err = 0;
    pthread_mutex_lock(&env_lock);
    require_ascii_block_locked();
    CHAR *b = guest_alloc_or_null((uint64_t)tab_len + 1);
    if (!b) {
        pthread_mutex_unlock(&env_lock);
        *err = ERROR_NOT_ENOUGH_MEMORY;
        return NULL;
    }
    for (size_t i = 0; i < tab_len; i++) b[i] = (CHAR)tab[i];
    b[tab_len] = 0;
    block_track_locked((uint64_t)(uintptr_t)b);
    pthread_mutex_unlock(&env_lock);
    return b;
}

BOOL rt_env_block_free(void *block) {
    require_ready("FreeEnvironmentStrings");
    /* Freeing NULL succeeds and does nothing.  The documentation does not say so for
     * FreeEnvironmentStrings; we assume it, as LocalFree(NULL) does (not verified). */
    if (!block) return TRUE;
    uint64_t addr = (uint64_t)(uintptr_t)block;

    pthread_mutex_lock(&env_lock);
    Block **pp = &blocks;
    while (*pp && (*pp)->addr != addr) pp = &(*pp)->next;
    if (!*pp) {
        pthread_mutex_unlock(&env_lock);
        rt_fatal("FreeEnvironmentStrings: %p is not a block from GetEnvironmentStrings, or it was freed already", block);
    }
    Block *gone = *pp;
    *pp = gone->next;
    free(gone);
    pthread_mutex_unlock(&env_lock);

    if (!rt_vm_free(addr, 0, MEM_RELEASE))
        rt_fatal("FreeEnvironmentStrings: releasing %p failed (Win32 error %u)", block, rt_thread_get_last_error());
    return TRUE;
}

/* ---- the command line and start-up information ---------------------------------------------- */

WCHAR *rt_cmdline_w(void) {
    require_ready("GetCommandLineW");
    return cmdline_w;
}

CHAR *rt_cmdline_a(void) {
    require_ready("GetCommandLineA");
    if (!cmdline_a) rt_fatal("GetCommandLineA: %s", cmdline_a_problem);
    return cmdline_a;
}

/* Output of one argument or of the program, appended at *k.  With out NULL nothing is written
 * and only *k advances: that is the size query. */
static void emit(WCHAR *out, SIZE_T *k, WCHAR c) {
    if (out) out[*k] = c;
    (*k)++;
}

static void emit_arg(WCHAR *out, SIZE_T *k, const WCHAR *a, bool always_quote) {
    bool quote = always_quote || !a[0];
    for (const WCHAR *p = a; *p && !quote; p++)
        if (*p == ' ' || *p == '\t' || *p == '"') quote = true;
    if (!quote) {
        for (const WCHAR *p = a; *p; p++) emit(out, k, *p);
        return;
    }
    emit(out, k, '"');
    const WCHAR *p = a;
    for (;;) {
        SIZE_T nbs = 0;
        while (*p == '\\') { nbs++; p++; }
        if (!*p) {                                  /* the closing quote follows: double them */
            for (SIZE_T i = 0; i < 2 * nbs; i++) emit(out, k, '\\');
            break;
        }
        if (*p == '"') {                            /* 2n+1 backslashes escape the quote */
            for (SIZE_T i = 0; i < 2 * nbs + 1; i++) emit(out, k, '\\');
            emit(out, k, '"');
        } else {                                    /* other backslashes are literal */
            for (SIZE_T i = 0; i < nbs; i++) emit(out, k, '\\');
            emit(out, k, *p);
        }
        p++;
    }
    emit(out, k, '"');
}

SIZE_T rt_cmdline_build_w(const WCHAR *program, const WCHAR *const *args, DWORD nargs,
                          WCHAR *out, SIZE_T cch) {
    if (!program) rt_fatal("rt_cmdline_build_w: NULL program");
    if (nargs && !args) rt_fatal("rt_cmdline_build_w: %u arguments but a NULL list", (unsigned)nargs);
    SIZE_T k = 0;
    emit_arg(NULL, &k, program, true);
    for (DWORD i = 0; i < nargs; i++) {
        if (!args[i]) rt_fatal("rt_cmdline_build_w: argument %u is NULL", (unsigned)i);
        emit(NULL, &k, ' ');
        emit_arg(NULL, &k, args[i], false);
    }
    SIZE_T need = k + 1;
    if (!out || cch < need) return need;
    k = 0;
    emit_arg(out, &k, program, true);
    for (DWORD i = 0; i < nargs; i++) {
        emit(out, &k, ' ');
        emit_arg(out, &k, args[i], false);
    }
    out[k] = 0;
    return need;
}

void rt_env_startup_info(STARTUPINFOW *out) {
    require_ready("GetStartupInfoW");
    if (!out) rt_fatal("GetStartupInfoW: NULL buffer");
    *out = startup;
}
