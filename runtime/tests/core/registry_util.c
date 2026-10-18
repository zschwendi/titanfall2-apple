/* Helpers for the registry tests (rt/registry.h).  The other registry_*.c files include this
 * file; it has no test of its own and, built alone, defines nothing but unused statics.
 *
 * The registry core is called directly (as env.c, locale.c and the others do): the shims that
 * expose it are tested with their own files.  Every test runs in its own process, in a fresh
 * state directory, so the file <state>/registry.txt starts absent and the tree starts with
 * the seed alone.
 */
#ifndef REGISTRY_TEST_UTIL
#define REGISTRY_TEST_UTIL

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rt/locale.h"
#include "rt/machine.h"
#include "rt/registry.h"
#include "rt/test.h"
#include "rt/win/error.h"

#define UT static __attribute__((unused))

/* ---- strings -------------------------------------------------------------------------------- */

/* ASCII text as a WCHAR string.  Sixteen rotating buffers, so one call may take several names.
 * Not for use from more than one thread. */
UT WCHAR *W(const char *s) {
    static WCHAR ring[16][1024];
    static unsigned next;
    WCHAR *b = ring[next++ % 16];
    size_t i = 0;
    for (; s[i] && i < 1023; i++) b[i] = (unsigned char)s[i];
    b[i] = 0;
    return b;
}

UT size_t wl(const WCHAR *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

/* The WCHAR string w is exactly the ASCII string s. */
UT bool weq(const WCHAR *w, const char *s) {
    for (size_t i = 0;; i++) {
        if (w[i] != (unsigned char)s[i]) return false;
        if (!s[i]) return true;
    }
}

/* ---- keys and values ------------------------------------------------------------------------ */

/* Create (or open) a key with full access; the call must succeed.  NULL if it did not. */
UT HKEY mk(HKEY parent, const char *path) {
    HKEY k = NULL;
    LSTATUS st = rt_reg_create_key_ex_w(parent, W(path), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    T_EQ(st, ERROR_SUCCESS);
    return st == ERROR_SUCCESS ? k : NULL;
}

UT LSTATUS open_st(HKEY parent, const char *path, REGSAM sam, HKEY *out) {
    return rt_reg_open_key_ex_w(parent, W(path), 0, sam, out);
}

/* Open with the given access; NULL (and a failed check) if it does not succeed. */
UT HKEY op(HKEY parent, const char *path, REGSAM sam) {
    HKEY k = NULL;
    LSTATUS st = open_st(parent, path, sam, &k);
    T_EQ(st, ERROR_SUCCESS);
    return st == ERROR_SUCCESS ? k : NULL;
}

UT void cl(HKEY k) { T_EQ(rt_reg_close_key(k), ERROR_SUCCESS); }

/* A REG_SZ value written with its terminator. */
UT LSTATUS put_sz(HKEY k, const char *name, const char *text) {
    WCHAR *w = W(text);
    return rt_reg_set_value_ex_w(k, name ? W(name) : NULL, 0, REG_SZ, (const BYTE *)w, (DWORD)((wl(w) + 1) * 2));
}

UT LSTATUS put_dw(HKEY k, const char *name, DWORD v) {
    return rt_reg_set_value_ex_w(k, name ? W(name) : NULL, 0, REG_DWORD, (const BYTE *)&v, 4);
}

UT LSTATUS put_raw(HKEY k, const char *name, DWORD type, const void *data, DWORD cb) {
    return rt_reg_set_value_ex_w(k, name ? W(name) : NULL, 0, type, (const BYTE *)data, cb);
}

/* The value is a REG_SZ equal to the ASCII text (terminator included in the stored size). */
UT bool is_sz(HKEY k, const char *name, const char *want) {
    BYTE buf[2048];
    DWORD cb = sizeof buf, type = 0xFFFF;
    if (rt_reg_query_value_ex_w(k, name ? W(name) : NULL, NULL, &type, buf, &cb) != ERROR_SUCCESS) return false;
    if (type != REG_SZ || cb != (wl(W(want)) + 1) * 2) return false;
    return weq((const WCHAR *)buf, want);
}

UT bool is_dw(HKEY k, const char *name, DWORD want) {
    DWORD v = 0, cb = 4, type = 0;
    if (rt_reg_query_value_ex_w(k, name ? W(name) : NULL, NULL, &type, (BYTE *)&v, &cb) != ERROR_SUCCESS) return false;
    return type == REG_DWORD && cb == 4 && v == want;
}

/* The value exists (a query that asks nothing). */
UT bool has_value(HKEY k, const char *name) {
    return rt_reg_query_value_ex_w(k, name ? W(name) : NULL, NULL, NULL, NULL, NULL) == ERROR_SUCCESS;
}

UT bool has_key(HKEY parent, const char *path) {
    HKEY k = NULL;
    if (open_st(parent, path, KEY_READ, &k) != ERROR_SUCCESS) return false;
    rt_reg_close_key(k);
    return true;
}

/* The number of subkeys and values of a key. */
UT void counts(HKEY k, DWORD *nkeys, DWORD *nvals) {
    T_EQ(rt_reg_query_info_key_w(k, NULL, NULL, NULL, nkeys, NULL, NULL, nvals, NULL, NULL, NULL, NULL), ERROR_SUCCESS);
}

/* Name number i of the subkeys, as ASCII; false if there is none. */
UT bool key_name(HKEY k, DWORD i, char *out, size_t cap) {
    WCHAR name[300];
    DWORD n = 300;
    if (rt_reg_enum_key_ex_w(k, i, name, &n, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) return false;
    size_t j = 0;
    for (; j < n && j + 1 < cap; j++) out[j] = (char)name[j];
    out[j] = 0;
    return true;
}

UT bool value_name(HKEY k, DWORD i, char *out, size_t cap) {
    WCHAR name[300];
    DWORD n = 300;
    if (rt_reg_enum_value_w(k, i, name, &n, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) return false;
    size_t j = 0;
    for (; j < n && j + 1 < cap; j++) out[j] = (char)name[j];
    out[j] = 0;
    return true;
}

/* ---- the file ------------------------------------------------------------------------------- */

UT char *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) { if (len) *len = 0; return NULL; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = calloc((size_t)n + 1, 1);
    if (n > 0 && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return NULL; }
    fclose(f);
    if (len) *len = (size_t)n;
    return b;
}

/* The registry file as it is now (malloc'd), or NULL if it does not exist. */
UT char *reg_text(size_t *len) { return slurp(rt_registry_path(), len); }

UT void spit(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    T_REQUIRE(f != NULL);
    fwrite(text, 1, strlen(text), f);
    fclose(f);
}

/* Replace the registry file with `text`. */
UT void put_file(const char *text) { spit(rt_registry_path(), text); }

UT bool exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

/* A path in the state directory. */
UT const char *state_path(const char *name) {
    static char buf[4][1200];
    static unsigned next;
    char *b = buf[next++ & 3];
    snprintf(b, sizeof buf[0], "%s/%s", rt_test_state_dir(), name);
    return b;
}

/* The text file `text`, as the writer would produce it for a registry with nothing but this content:
 * the two comment lines and the header, then the sections as given. */
UT char *with_header(const char *sections) {
    const char *hdr =
        "# Registry of the Titanfall recompilation runtime (runtime/include/rt/registry.h describes the format).\n"
        "# The runtime rewrites this file whole after every change; comments and layout are not kept.\n"
        "titanfall-registry 1\n";
    char *b = malloc(strlen(hdr) + strlen(sections) + 1);
    strcpy(b, hdr);
    strcat(b, sections);
    return b;
}

UT bool contains(const char *hay, const char *needle) { return hay && strstr(hay, needle) != NULL; }

/* Count of the lines of `hay` that are exactly `line` (no newline). */
UT unsigned count_line(const char *hay, const char *line) {
    unsigned n = 0;
    size_t ll = strlen(line);
    for (const char *p = hay; p && *p;) {
        const char *e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        if (len == ll && !memcmp(p, line, ll)) n++;
        p += len + (e ? 1 : 0);
    }
    return n;
}

#endif /* REGISTRY_TEST_UTIL */
