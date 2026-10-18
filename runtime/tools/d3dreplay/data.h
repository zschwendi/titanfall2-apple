/* The recorded data for the replay player (`--data DIR`, RpOptions.data): where the script says
 * "a pointer to <size> bytes" the device is given the bytes the real game passed, instead of zeros.
 *
 * DIR is what tools/trace/d3d/tracefile.py `extract` wrote from the recording's trace file:
 *
 *     DIR/index.tsv              call, ordinal, kind, size, sha256, where     (sorted by call)
 *     DIR/blobs/<2 hex>/<sha256> the bytes, once per distinct content
 *
 * kind `b` is a blob the game passed in (initial data, UpdateSubresource, an input layout's
 * signature), `B` a shader's bytecode, `W` the bytes of a write into mapped memory; `d` rows (what
 * the device wrote) are not read.  The n-th `b` or `B` token of a script line, counted through its
 * structures in the order they stand, is the n-th `b` or `B` row of that call: tracefile.py `check`
 * holds every script line's sizes against the trace in that order, and a size that differs here
 * ends the player ("not this script's data").
 *
 * DIR IS DERIVED FROM THE GAME.  Nothing here prints a byte of a blob: messages carry call numbers,
 * sizes, hashes and the reason a container was refused (which holds offsets and chunk tags).
 *
 * What the device sees is as before in everything but the contents: a block of exactly the recorded
 * size that it may read and not write, ending at a page that cannot be touched.  A call for which
 * the index has no row gets what the player gave without the option, and is counted.
 *
 * THE FRONT'S READING OF SHADERS.  Nothing in the device opens a shader's container when the
 * shader is created (CreateInputLayout opens the bytes it is given, and D3DReflect and
 * D3DGetBlobPart, which the recorder could not see, open theirs).  So that a replay says how
 * many of the recording's containers our runtime/nulldev/dxbc.c reads, the player itself puts
 * every container it hands out (a shader's bytecode, and the bytes CreateInputLayout gets,
 * counted apart) through rt_dxbc_open and, where the container has one input
 * signature, through rt_dxbc_signature_count and rt_dxbc_signature_element for each element:
 * the same three functions, in the same order, as the reflection object and the signature blob.
 * A refusal is a row of the table.
 *
 * Included twice by replay.h: first for the types, then (RP_DATA_BODY) for the functions.
 */
#ifndef RP_DATA_TYPES
#define RP_DATA_TYPES

#include <fcntl.h>

#include "rt/dxbc.h"

typedef struct { uint64_t call, size; char kind; uint8_t hash[32]; } RpBlobRef;

typedef struct {
    uint64_t given, given_bytes;                /* b and B tokens that got recorded bytes */
    uint64_t absent, absent_bytes;              /* tokens and writes the index has no row for */
    uint64_t written, written_bytes;            /* W lines that wrote recorded bytes */
    struct RpFront {
        uint64_t n, open;                       /* containers handed out; of them, opened by rt_dxbc_open */
        uint64_t signatures, elements;          /* input signatures read whole, and their elements */
        uint64_t no_signature;                  /* opened containers without exactly one ISGN or ISG1 */
    } shaders, layouts;                         /* what Create*Shader got; what CreateInputLayout got */
} RpDataCounts;

typedef struct {
    char      *dir;
    RpBlobRef *refs;
    size_t     n;
    struct RpHeld { void *base; size_t len; } *held;
    unsigned   nheld, capheld;
    uint64_t   call;                            /* the call whose rows `at` walks */
    size_t     at;
    uint8_t   *scratch;
    size_t     scratch_cap;
} RpData;

static void rp_data_release(RpData *d) {        /* after a call: the blocks its arguments pointed to */
    if (!d) return;
    while (d->nheld) { d->nheld--; munmap(d->held[d->nheld].base, d->held[d->nheld].len); }
}

#endif /* RP_DATA_TYPES */

#if defined(RP_DATA_BODY) && !defined(RP_DATA_BODY_DONE)
#define RP_DATA_BODY_DONE

static RpData *rp_data_open(const char *dir) {
    char path[4096];
    snprintf(path, sizeof path, "%s/index.tsv", dir);
    FILE *f = fopen(path, "r");
    if (!f) rp_die("--data: %s: %s (tools/trace/d3d/tracefile.py extract writes it)", path, strerror(errno));
    RpData *d = calloc(1, sizeof *d);
    d->dir = strdup(dir);
    d->call = UINT64_MAX;
    size_t cap = 0, lineno = 0;
    char *line = NULL;
    size_t linecap = 0;
    uint64_t last = 0;
    while (getline(&line, &linecap, f) > 0) {
        lineno++;
        char *end, *p = line;
        uint64_t call = strtoull(p, &end, 10);
        if (end == p || *end != '\t') rp_die("--data: %s line %zu: no call number", path, lineno);
        strtoull(end + 1, &p, 10);                                  /* the ordinal among all of the call's blobs */
        if (*p != '\t' || !p[1] || p[2] != '\t') rp_die("--data: %s line %zu: no kind", path, lineno);
        char kind = p[1];
        uint64_t size = strtoull(p + 3, &end, 10);
        if (*end != '\t' || strlen(end + 1) < 64) rp_die("--data: %s line %zu: no hash", path, lineno);
        if (call < last) rp_die("--data: %s line %zu: not sorted by call", path, lineno);
        last = call;
        if (kind != 'b' && kind != 'B' && kind != 'W') continue;
        if (d->n == cap) { cap = cap ? cap * 2 : 1 << 16; d->refs = realloc(d->refs, cap * sizeof *d->refs); }
        RpBlobRef *b = &d->refs[d->n++];
        b->call = call; b->size = size; b->kind = kind;
        for (int k = 0; k < 32; k++) {
            unsigned v;
            if (sscanf(end + 1 + 2 * k, "%2x", &v) != 1) rp_die("--data: %s line %zu: the hash is not 64 hex digits", path, lineno);
            b->hash[k] = (uint8_t)v;
        }
    }
    free(line);
    fclose(f);
    return d;
}

static void rp_data_close(RpData *d) {
    if (!d) return;
    rp_data_release(d);
    free(d->held); free(d->refs); free(d->dir); free(d->scratch); free(d);
}

/* The next b, B or W row of the call being played, or NULL when the index has no further one. */
static const RpBlobRef *rp_data_next(RpRun *r) {
    RpData *d = r->data;
    if (d->call != r->no) {
        size_t lo = 0, hi = d->n;
        while (lo < hi) { size_t mid = lo + (hi - lo) / 2; if (d->refs[mid].call < r->no) lo = mid + 1; else hi = mid; }
        d->call = r->no;
        d->at = lo;
    }
    if (d->at >= d->n || d->refs[d->at].call != r->no) return NULL;
    return &d->refs[d->at++];
}

static void rp_data_read(RpRun *r, const RpBlobRef *b, uint8_t *to) {
    char path[4096], hex[65];
    for (int k = 0; k < 32; k++) snprintf(hex + 2 * k, 3, "%02x", b->hash[k]);
    snprintf(path, sizeof path, "%s/blobs/%.2s/%s", r->data->dir, hex, hex);
    int fd = open(path, O_RDONLY);
    if (fd < 0) rp_die("call %llu: %s: %s", (unsigned long long)r->no, path, strerror(errno));
    for (size_t got = 0; got < b->size; ) {
        ssize_t k = read(fd, to + got, b->size - got);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) rp_die("call %llu: %s holds fewer than the index's %llu bytes", (unsigned long long)r->no, path, (unsigned long long)b->size);
        got += (size_t)k;
    }
    close(fd);
}

static const RpBlobRef *rp_data_row(RpRun *r, size_t n, bool write) {
    const RpBlobRef *b = rp_data_next(r);
    RpDataCounts *c = &r->tab->data;
    if (b && (b->kind == 'W') != write) b = NULL;
    if (!b) { if (r->record) { c->absent++; c->absent_bytes += n; } return NULL; }
    if (b->size != n)
        rp_die("call %llu: the script has %zu bytes here and the data's index %llu: this is not this script's data",
               (unsigned long long)r->no, n, (unsigned long long)b->size);
    return b;
}

/* The front's reading of one bytecode: see the head of this file. */
static void rp_data_front(RpRun *r, const uint8_t *p, size_t n, bool layout) {
    struct RpFront *c = layout ? &r->tab->data.layouts : &r->tab->data.shaders;
    RtDxbc box;
    char why[RT_DXBC_WHY] = "", cause[RT_DXBC_WHY + 80];
    uint32_t tag = 0, len = 0, found = 0, count = 0;
    const uint8_t *data = NULL;
    c->n++;
    bool ok = rt_dxbc_open(p, n, &box, why);
    const char *stage = "rt_dxbc_open refuses the container";
    if (ok) {
        c->open++;
        for (uint32_t i = 0; i < box.nchunks; i++) {
            uint32_t t, l;
            const uint8_t *dd;
            rt_dxbc_chunk(&box, i, &t, &dd, &l);
            if ((t == RT_DXBC_ISGN || t == RT_DXBC_ISG1) && !found++) { tag = t; data = dd; len = l; }
        }
        if (found != 1) { c->no_signature++; return; }
        stage = "the input signature is refused";
        ok = rt_dxbc_signature_count(tag, data, len, &count, why);
        for (uint32_t i = 0; ok && i < count; i++) {
            RtDxbcElement e;
            ok = rt_dxbc_signature_element(tag, data, len, i, &e, why);
        }
        if (ok) { c->signatures++; c->elements += count; return; }
    }
    snprintf(cause, sizeof cause, "the front: %s: %s", stage, why);
    for (char *q = cause; *q; q++) if (*q >= '0' && *q <= '9') *q = '#';        /* one row per reason, not per number */
    rp_differ(r, layout ? "pShaderBytecodeWithInputSignature" : "pShaderBytecode", cause, "%zu bytes: %s", n, why);
}

/* n recorded bytes for the next b (or, shader, B) token of the call, ending at a page that cannot be
 * touched; 0 when the index has none for it. */
static uint64_t rp_data_block(RpRun *r, size_t n, bool shader) {
    if (!n) return 0;
    const RpBlobRef *b = rp_data_row(r, n, false);
    if (!b) return 0;
    RpData *d = r->data;
    size_t page = (size_t)getpagesize(), len = (n + page - 1) & ~(page - 1);
    uint8_t *base = mmap(NULL, len + page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (base == MAP_FAILED) rp_die("call %llu: no memory for %zu bytes of data", (unsigned long long)r->no, n);
    uint8_t *p = base + len - n;
    rp_data_read(r, b, p);
    if (mprotect(base, len, PROT_READ) || mprotect(base + len, page, PROT_NONE)) rp_die("mprotect: %s", strerror(errno));
    if (d->nheld == d->capheld) { d->capheld = d->capheld ? d->capheld * 2 : 64; d->held = realloc(d->held, d->capheld * sizeof *d->held); }
    d->held[d->nheld++] = (struct RpHeld){ base, len + page };
    if (r->record) {
        r->tab->data.given++;
        r->tab->data.given_bytes += n;
        if (shader) rp_data_front(r, p, n, b->kind != 'B');
    }
    return (uint64_t)(uintptr_t)p;
}

/* What a `b` token points to: the recorded bytes, or the zeros of a run without --data. */
static uint64_t rp_in_block(RpRun *r, size_t n) {
    uint64_t p = r->data ? rp_data_block(r, n, false) : 0;
    return p ? p : rp_zero_block(r, n);
}

/* A W line: the recorded bytes of the write, copied to `to` as the game's memcpy did (so a write
 * that ends outside the mapping faults here as the zeros did).  False: the index has none. */
static bool rp_data_write(RpRun *r, void *to, size_t n) {
    const RpBlobRef *b = rp_data_row(r, n, true);
    if (!b) return false;
    RpData *d = r->data;
    if (n > d->scratch_cap) { d->scratch_cap = n < 4096 ? 4096 : n; free(d->scratch); d->scratch = malloc(d->scratch_cap); }
    rp_data_read(r, b, d->scratch);
    memcpy(to, d->scratch, n);
    if (r->record) { r->tab->data.written++; r->tab->data.written_bytes += n; }
    return true;
}

static void rp_data_print(const RpDataCounts *c, const char *dir, FILE *o) {
    fprintf(o, "With the recorded data of %s: %llu blocks of %llu bytes given to calls and %llu writes of %llu bytes made with the recorded "
            "bytes; %llu blocks or writes (%llu bytes) have no row in the index and were zeros.\n\n", dir,
            (unsigned long long)c->given, (unsigned long long)c->given_bytes, (unsigned long long)c->written, (unsigned long long)c->written_bytes,
            (unsigned long long)c->absent, (unsigned long long)c->absent_bytes);
    for (int k = 0; k < 2; k++) {
        const struct RpFront *f = k ? &c->layouts : &c->shaders;
        fprintf(o, "%s: %llu containers given; rt_dxbc_open opens %llu; of those %llu have one input signature that reads whole "
                "(%llu elements) and %llu have none or more than one.\n\n", k ? "Input layouts (CreateInputLayout)" : "Shaders (the six creators)",
                (unsigned long long)f->n, (unsigned long long)f->open, (unsigned long long)f->signatures, (unsigned long long)f->elements,
                (unsigned long long)f->no_signature);
    }
}

#endif /* RP_DATA_BODY */
