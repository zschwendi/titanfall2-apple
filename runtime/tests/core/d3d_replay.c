/* The Direct3D replay player (runtime/tools/d3dreplay/replay.h) on made-up scripts: that it plays
 * a script through the device's interface slots, holds results and out-values against the
 * recorded ones, tells what is ours by design from what is not, survives a stop inside the
 * device in the table mode, and ends at the first disagreement in the other.
 *
 * Expected values: the sample script runtime/tools/d3dreplay/madeup.d3ds is the converter's output
 * for the made-up call lines of madeup.txt beside it (the shape of the game's first calls and of one
 * resource of each kind, with values of mine), written so that the device as contracted agrees
 * with every line except where the recorded answer is another machine's (the display modes, the
 * adapter, "not ready" from a query): docs/handoff/d3d-contract.md.  The second script is written
 * here; each of its lines says what it is for.  The stop it relies on is the interface layer's
 * own (rt/com.h: a call through a dead object stops), not a decision of any method body.
 */
#include "rt/test.h"
#include "../../tools/d3dreplay/replay.h"

#ifndef RP_SAMPLE
#define RP_SAMPLE "runtime/tools/d3dreplay/madeup.d3ds"
#endif

static const char faulty[] =
    "0 . D3D11CreateDevice =0 n i1 i0 i0 n i0 i7 >o1 >I4[45056] >o2\n"
    "1 . ID3D11Device::QueryInterface =0 o1 g0f97db777662ba48ba28070143b4392c >o3\n"
    "2 . IDXGIDevice1::GetParent =0 o3 g618f03293938264691fd086879011a05 >o4\n"
    /* 3: the recorded result is E_INVALIDARG where the device answers S_OK: a disagreement */
    "3 . ID3D11Device::CreateBuffer =2147942487 o1 sD3D11_BUFFER_DESC{ByteWidth=64,Usage=2,BindFlags=4,CPUAccessFlags=65536,MiscFlags=0,StructureByteStride=0} n >o?\n"
    /* 4, 5: a buffer, released to 0 */
    "4 . ID3D11Device::CreateBuffer =0 o1 sD3D11_BUFFER_DESC{ByteWidth=64,Usage=2,BindFlags=4,CPUAccessFlags=65536,MiscFlags=0,StructureByteStride=0} n >o5\n"
    "5 . ID3D11Buffer::Release =0 o5\n"
    /* 6: a call through the dead buffer: the layer stops; object 6 is never made */
    "6 . ID3D11Buffer::QueryInterface =0 o5 g0000000000000000c000000000000046 >o6\n"
    /* 7: needs object 6: not played */
    "7 . ID3D11Buffer::Release =0 o6\n"
    /* 8: a method the descriptions do not have; 9: a write the converter could not place */
    "8 U ID3D11Device::NoSuchMethod -\n"
    "9 W 0 0 0 16\n"
    /* 10: the replay goes on after all that */
    "10 . IDXGIAdapter1::EnumOutputs =2289696770 o4 i7 >o?\n";

static FILE *script_of(const char *text) {
    FILE *f = tmpfile();
    if (!f) return NULL;
    fputs(text, f);
    rewind(f);
    return f;
}

static const RpRow *row(const RpTable *t, const char *method, const char *cause_begins) {
    for (unsigned i = 0; i < t->n; i++)
        if (!strcmp(t->rows[i].method, method) && !strncmp(t->rows[i].cause, cause_begins, strlen(cause_begins))) return &t->rows[i];
    return NULL;
}

TEST(core, d3d_replay)
{
    RpTable t = { 0 };
    RpOptions all = { .keep_going = true, .scout = true }, first = { .scout = true };
    FILE *f, *sink = tmpfile();
    T_REQUIRE(sink != NULL);
    first.out = sink;

    T_CASE("the sample script: every call is played and nothing differs beyond what is ours by design");
    f = fopen(RP_SAMPLE, "r");
    T_REQUIRE(f != NULL);
    T_EQ(rp_run(T_CTX(), f, &all, &t), 0);
    fclose(f);
    T_EQ(t.lines, 27);
    T_EQ(t.game_calls, 25);
    T_EQ(t.played, 25);
    T_EQ(t.foreign, 1);                         /* GetImmediateContext, marked as not the game's */
    T_EQ(t.writes_done, 1);                     /* 96 bytes into the mapped constant buffer */
    T_EQ(t.stops, 0);
    T_EQ(t.with_differences, 0);
    T_EQ(t.clean + t.with_expected_only, 25);
    T_TRUE(row(&t, "IDXGIAdapter1::GetDesc", "desc differs") != NULL);
    T_TRUE(row(&t, "ID3D11DeviceContext::GetData", "result is 0 where the recording has 0x1") != NULL);
    for (unsigned i = 0; i < t.n; i++) T_TRUE(t.rows[i].expected != NULL);
    rp_table_free(&t);

    T_CASE("the same in the first-disagreement mode: it reaches the end");
    f = fopen(RP_SAMPLE, "r");
    T_REQUIRE(f != NULL);
    T_EQ(rp_run(T_CTX(), f, &first, &t), 0);
    fclose(f);
    T_EQ(t.lines, 27);
    rp_table_free(&t);

    T_CASE("the table mode runs on past a disagreement, a stop inside the device and what follows from it");
    f = script_of(faulty);
    T_REQUIRE(f != NULL);
    T_EQ(rp_run(T_CTX(), f, &all, &t), 1);
    fclose(f);
    T_EQ(t.lines, 11);
    T_EQ(t.stops, 1);
    T_TRUE(t.scouts >= 1);
    T_EQ(t.scouts_by_signal, 0);                /* the scout that stopped exited (the quiet end, rt/crash.h): no crash report */
    T_EQ(t.not_played, 1);
    T_EQ(t.undescribed, 1);
    T_EQ(t.played, 7);                          /* lines 0 to 5 and 10 */
    const RpRow *w = row(&t, "ID3D11Device::CreateBuffer", "result is 0 where the recording has 0x80070057");
    T_REQUIRE(w != NULL);
    T_EQ(w->first, 3);
    T_EQ(w->count, 1);
    T_TRUE(row(&t, "ID3D11Device::CreateBuffer", "ppBuffer is written where the recording wrote nothing") != NULL);
    w = row(&t, "ID3D11Buffer::QueryInterface", "STOP: ");
    T_REQUIRE(w != NULL);
    T_EQ(w->first, 6);
    T_TRUE(strstr(w->cause, "destroyed") != NULL);
    w = row(&t, "ID3D11Buffer::Release", "not played: needs an object our device never returned (ID3D11Buffer::QueryInterface: STOP: ");
    T_REQUIRE(w != NULL);
    T_EQ(w->first, 7);
    T_TRUE(row(&t, "write into mapped memory", "not played: the converter found no open mapping") != NULL);
    T_TRUE(row(&t, "ID3D11Device::NoSuchMethod", "not played: ") != NULL);
    T_TRUE(row(&t, "IDXGIAdapter1::EnumOutputs", "") == NULL);         /* the last line was played and agrees */

    T_CASE("the rows as a file, and a later table held against it: a new row or a larger count is worse");
    FILE *rows = tmpfile(), *said = tmpfile();
    T_REQUIRE(rows != NULL && said != NULL);
    rp_table_tsv(&t, rows);
    rewind(rows);
    T_EQ(rp_table_against(&t, rows, said), 0);
    rewind(rows);
    t.rows[0].count += 5;
    T_EQ(rp_table_against(&t, rows, said), 1);
    fclose(rows);
    fclose(said);
    rp_table_free(&t);

    T_CASE("the first-disagreement mode ends at line 3 and says where");
    f = script_of(faulty);
    T_REQUIRE(f != NULL);
    T_EQ(rp_run(T_CTX(), f, &first, &t), 1);
    fclose(f);
    T_EQ(t.lines, 4);
    T_EQ(t.played, 4);
    rewind(sink);
    static char said1[4096];
    said1[fread(said1, 1, sizeof said1 - 1, sink)] = 0;
    T_TRUE(strstr(said1, "FIRST DISAGREEMENT at call 3") != NULL);
    T_TRUE(strstr(said1, "ID3D11Device::CreateBuffer: result is 0 where the recording has 0x80070057") != NULL);
    rp_table_free(&t);
    fclose(sink);
}
