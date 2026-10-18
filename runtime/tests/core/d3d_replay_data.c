/* The replay player's recorded data (runtime/tools/d3dreplay/data.h; `--data DIR`): that a script's
 * blocks and writes carry the bytes of the data directory instead of zeros, that the block the
 * device is given still ends at a page it cannot touch, that the player's reading of every
 * container through rt/dxbc.h is counted and a refusal is a row, that a block the index does not
 * have is zeros and counted, and that without the option the player is what it was.
 *
 * Everything here is made up by the test: the container is assembled by rt/test_dxbc.h with a
 * filler chunk where a shader's code would be, the other blobs are counting bytes, and the
 * "hashes" that name the files are four made-up names (the player does not hash).  Expected
 * values: the counts are those of the blobs this file writes; that the device accepts the
 * script's calls is the contract the sample script of d3d_replay.c already rests on
 * (docs/handoff/d3d-contract.md); that CreateInputLayout warns when the bytes it is given do not
 * open, and not when they do, is runtime/nulldev/reflection.c's own log line (d3d-reflect).
 */
#include <sys/stat.h>

#include "rt/test.h"
#include "rt/test_dxbc.h"
#include "../../tools/d3dreplay/replay.h"

#define H_WRITE  "1111111111111111111111111111111111111111111111111111111111111111"
#define H_INIT   "2222222222222222222222222222222222222222222222222222222222222222"
#define H_CODE   "3333333333333333333333333333333333333333333333333333333333333333"
#define H_BROKEN "4444444444444444444444444444444444444444444444444444444444444444"

static char     dd_dir[512];
static uint8_t  dd_code[T_DXBC_MAX], dd_write[96], dd_init[16];
static uint32_t dd_code_len;

static bool dd_put(const char *hash, const uint8_t *bytes, size_t n) {
    char path[700];
    snprintf(path, sizeof path, "%s/blobs/%.2s", dd_dir, hash);
    mkdir(path, 0700);
    snprintf(path, sizeof path, "%s/blobs/%.2s/%s", dd_dir, hash, hash);
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fwrite(bytes, 1, n, f) == n;
    return fclose(f) == 0 && ok;
}

/* The data directory: an index of six rows (one of them a kind the player does not read) and four files. */
static bool dd_make(void) {
    static const TDxbcElement in[] = { { .name = "POSITION", .component_type = 3, .reg = 0, .mask = 7, .rw_mask = 7 } };
    uint8_t sig[128], filler[64], broken[T_DXBC_MAX];
    char path[700];
    const char *tmp = getenv("TMPDIR");
    snprintf(dd_dir, sizeof dd_dir, "%s/rt-d3d-replay-data-XXXXXX", tmp && *tmp ? tmp : "/tmp");
    if (!mkdtemp(dd_dir)) return false;
    snprintf(path, sizeof path, "%s/blobs", dd_dir);
    if (mkdir(path, 0700)) return false;
    for (unsigned i = 0; i < sizeof filler; i++) filler[i] = (uint8_t)(0xC0 + i);
    for (unsigned i = 0; i < sizeof dd_write; i++) dd_write[i] = (uint8_t)(i + 1);
    for (unsigned i = 0; i < sizeof dd_init; i++) dd_init[i] = (uint8_t)(0xA0 + i);
    uint32_t sig_len = t_dxbc_signature(sig, false, in, 1);
    TDxbcChunk chunks[] = { { "ISGN", sig, sig_len }, { "SHEX", filler, sizeof filler } };
    dd_code_len = t_dxbc_container(dd_code, chunks, 2);
    memcpy(broken, dd_code, dd_code_len);
    broken[dd_code_len - 1] ^= 1;                   /* the checksum no longer holds */
    if (!dd_put(H_WRITE, dd_write, sizeof dd_write) || !dd_put(H_INIT, dd_init, sizeof dd_init) ||
        !dd_put(H_CODE, dd_code, dd_code_len) || !dd_put(H_BROKEN, broken, dd_code_len)) return false;
    snprintf(path, sizeof path, "%s/index.tsv", dd_dir);
    FILE *f = fopen(path, "w");
    if (!f) return false;
    fprintf(f, "22\t0\tW\t96\t" H_WRITE "\tsrc\n24\t0\tb\t16\t" H_INIT "\tpInitialData.pSysMem\n25\t0\td\t8\t" H_INIT "\tpData\n"
               "26\t0\tB\t%u\t" H_CODE "\tpShaderBytecode\n27\t0\tb\t%u\t" H_CODE "\tpShaderBytecodeWithInputSignature\n"
               "28\t0\tB\t%u\t" H_BROKEN "\tpShaderBytecode\n", dd_code_len, dd_code_len, dd_code_len);
    return fclose(f) == 0;
}

static void dd_remove(void) {
    static const char *const h[] = { H_WRITE, H_INIT, H_CODE, H_BROKEN };
    char path[700];
    for (unsigned i = 0; i < 4; i++) {
        snprintf(path, sizeof path, "%s/blobs/%.2s/%s", dd_dir, h[i], h[i]); unlink(path);
        snprintf(path, sizeof path, "%s/blobs/%.2s", dd_dir, h[i]); rmdir(path);
    }
    snprintf(path, sizeof path, "%s/index.tsv", dd_dir); unlink(path);
    snprintf(path, sizeof path, "%s/blobs", dd_dir); rmdir(path);
    rmdir(dd_dir);
}

/* The script: a device, a dynamic buffer mapped and written, a buffer with initial data, a vertex
 * shader, an input layout made from the shader's bytes; then, by `more`, a shader whose container
 * is damaged (1) and a buffer the index has no data for (2). */
static FILE *dd_script(int more) {
    FILE *f = tmpfile();
    if (!f) return NULL;
    fprintf(f,
        "0 . D3D11CreateDevice =0 n i1 i0 i0 n i0 i7 >o1 >I4[45056] >o2\n"
        "20 . ID3D11Device::CreateBuffer =0 o1 sD3D11_BUFFER_DESC{ByteWidth=112,Usage=2,BindFlags=4,CPUAccessFlags=65536,MiscFlags=0,StructureByteStride=0} n >o9\n"
        "21 . ID3D11DeviceContext::Map =0 o2 o9 i0 i4 i0 >sD3D11_MAPPED_SUBRESOURCE{pData=589824,RowPitch=112,DepthPitch=112}\n"
        "22 W 9 0 16 96\n"
        "23 . ID3D11DeviceContext::Unmap - o2 o9 i0\n"
        "24 . ID3D11Device::CreateBuffer =0 o1 sD3D11_BUFFER_DESC{ByteWidth=16,Usage=0,BindFlags=1,CPUAccessFlags=0,MiscFlags=0,StructureByteStride=0} "
            "sD3D11_SUBRESOURCE_DATA{pSysMem=b16,SysMemPitch=0,SysMemSlicePitch=0} >o10\n"
        "26 . ID3D11Device::CreateVertexShader =0 o1 B%u i%u n >o12\n"
        "27 . ID3D11Device::CreateInputLayout =0 o1 SD3D11_INPUT_ELEMENT_DESC[{SemanticName=\"POSITION\",SemanticIndex=0,Format=6,InputSlot=0,"
            "AlignedByteOffset=0,InputSlotClass=0,InstanceDataStepRate=0}] i1 B%u i%u >o13\n", dd_code_len, dd_code_len, dd_code_len, dd_code_len);
    if (more == 1) fprintf(f, "28 . ID3D11Device::CreateVertexShader =0 o1 B%u i%u n >o14\n", dd_code_len, dd_code_len);
    if (more == 2)
        fprintf(f, "29 . ID3D11Device::CreateBuffer =0 o1 sD3D11_BUFFER_DESC{ByteWidth=16,Usage=0,BindFlags=1,CPUAccessFlags=0,MiscFlags=0,StructureByteStride=0} "
                   "sD3D11_SUBRESOURCE_DATA{pSysMem=b16,SysMemPitch=0,SysMemSlicePitch=0} >o15\n");
    rewind(f);
    return f;
}

TEST(core, d3d_replay_data)
{
    RpTable t = { 0 };
    T_REQUIRE(dd_make());
    RpOptions all = { .keep_going = true, .scout = true, .data = dd_dir };
    FILE *f;

    T_CASE("with data: every block and the write carry recorded bytes, and both containers read whole");
    T_REQUIRE((f = dd_script(0)) != NULL);
    T_EQ(rp_run(T_CTX(), f, &all, &t), 0);
    fclose(f);
    T_EQ(t.played, 7);
    T_EQ(t.stops, 0);
    T_EQ(t.with_differences, 0);
    T_EQ(t.writes_done, 1);
    T_EQ(t.data.given, 3);                              /* the initial data, the bytecode, the layout's bytes */
    T_EQ(t.data.given_bytes, 16 + 2 * (uint64_t)dd_code_len);
    T_EQ(t.data.written, 1);
    T_EQ(t.data.written_bytes, 96);
    T_EQ(t.data.absent, 0);
    T_EQ(t.data.shaders.n, 1);
    T_EQ(t.data.shaders.open, 1);
    T_EQ(t.data.shaders.signatures, 1);
    T_EQ(t.data.shaders.elements, 1);
    T_EQ(t.data.layouts.n, 1);
    T_EQ(t.data.layouts.open, 1);
    T_EQ(t.data.layouts.signatures, 1);
    T_TRUE(!T_LOGGED(RT_CH_COM, "do not open as a container"));      /* the device read a container, not zeros */
    T_TRUE(!T_LOGGED(RT_CH_COM, "no single input signature"));
    rp_table_free(&t);
    t = (RpTable){ 0 };

    T_CASE("a container that rt_dxbc_open refuses is a row, and the run says so");
    T_REQUIRE((f = dd_script(1)) != NULL);
    T_EQ(rp_run(T_CTX(), f, &all, &t), 1);
    fclose(f);
    T_EQ(t.played, 8);
    T_EQ(t.data.shaders.n, 2);
    T_EQ(t.data.shaders.open, 1);
    T_EQ(t.with_differences, 1);
    bool found = false;
    for (unsigned i = 0; i < t.n; i++)
        if (!strcmp(t.rows[i].method, "ID3D11Device::CreateVertexShader") && strstr(t.rows[i].cause, "the front: rt_dxbc_open refuses the container")) {
            found = true;
            T_EQ(t.rows[i].count, 1);
            T_EQ(t.rows[i].first, 28);
            T_TRUE(t.rows[i].expected == NULL);
        }
    T_TRUE(found);
    rp_table_free(&t);
    t = (RpTable){ 0 };

    T_CASE("a block the index has no row for is zeros, and is counted");
    T_REQUIRE((f = dd_script(2)) != NULL);
    T_EQ(rp_run(T_CTX(), f, &all, &t), 0);
    fclose(f);
    T_EQ(t.played, 8);
    T_EQ(t.data.given, 3);
    T_EQ(t.data.absent, 1);
    T_EQ(t.data.absent_bytes, 16);
    rp_table_free(&t);
    t = (RpTable){ 0 };

    T_CASE("the block handed out: the recorded bytes, read-only, ending at a page that cannot be touched");
    RpRun r = { .tab = &t, .record = true };
    r.data = rp_data_open(dd_dir);
    T_EQ(r.data->n, 5);                                 /* the `d` row is not read */
    r.no = 24;
    const uint8_t *p = (const uint8_t *)(uintptr_t)rp_data_block(&r, 16, false);
    T_REQUIRE(p != NULL);
    T_TRUE(!memcmp(p, dd_init, 16));
    T_EQ(((uintptr_t)p + 16) % (uintptr_t)getpagesize(), 0);
    int fds[2], zero = open("/dev/zero", O_RDONLY);     /* a pipe: the kernel reads what it is given to write */
    T_REQUIRE(pipe(fds) == 0 && zero >= 0);
    T_EQ(write(fds[1], p, 16), 16);
    errno = 0;
    T_EQ(write(fds[1], p + 16, 1), -1);                 /* the page after it cannot be read */
    T_EQ(errno, EFAULT);
    errno = 0;
    T_EQ(read(zero, (void *)(uintptr_t)p, 1), -1);      /* and the block cannot be written */
    T_EQ(errno, EFAULT);
    close(fds[0]); close(fds[1]); close(zero);
    T_EQ(rp_data_block(&r, 16, false), 0);              /* the call has one row */
    T_EQ(t.data.absent, 1);
    r.no = 22;
    uint8_t got[96] = { 0 };
    T_TRUE(rp_data_write(&r, got, sizeof got));
    T_TRUE(!memcmp(got, dd_write, sizeof got));
    r.no = 24;
    T_TRUE(!rp_data_write(&r, got, 16));                /* a block's row is not a write's */
    rp_data_close(r.data);
    rp_table_free(&t);
    dd_remove();
}

TEST(core, d3d_replay_no_data)
{
    RpTable t = { 0 };
    T_REQUIRE(dd_make());                               /* for the container's size only: the directory is not given */
    RpOptions all = { .keep_going = true, .scout = true };
    FILE *f;
    T_CASE("without the option: zeros as before, nothing counted, and CreateInputLayout meets bytes that are no container");
    T_REQUIRE((f = dd_script(1)) != NULL);
    T_EQ(rp_run(T_CTX(), f, &all, &t), 0);
    fclose(f);
    T_EQ(t.played, 8);
    T_EQ(t.with_differences, 0);
    T_EQ(t.writes_done, 1);
    T_EQ(t.data.given + t.data.written + t.data.absent + t.data.shaders.n + t.data.layouts.n, 0);
    T_TRUE(T_LOGGED(RT_CH_COM, "do not open as a container"));
    rp_table_free(&t);
    dd_remove();
}
