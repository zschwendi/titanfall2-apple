/* The audio client's stream (rt/wasapi.h; runtime/xaudio/wasapi.c and the bodies under runtime/xaudio/
 * IAudioClient, IAudioRenderClient, IAudioClock).  The error rows of the brief's table; the game's own
 * Initialize call and the data path into a WAV file (RT_HOST_AUDIO=file-fast:<path>); the room in the buffer;
 * the timing of the device with RT_HOST_AUDIO unset (no sound); Reset.  The expected values are written here
 * by hand from the brief.  No test makes a sound.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "rt/log.h"
#include "rt/test.h"
#include "rt/wasapi.h"

#define FLAGS_EVENT 0x00040000u     /* AUDCLNT_STREAMFLAGS_EVENTCALLBACK */

/* IID_IAudioRenderClient f294acfc-3146-4483-a7bf-addca7c260e2, IID_IAudioClock cd63314f-3fba-4a1b-812c-ef96358728e7,
 * IID_IAudioStreamVolume 93014887-242d-4068-8a15-cf5e93b90fe3 (the descriptions in runtime/com/desc). */
static const GUID iid_render = { 0xf294acfc, 0x3146, 0x4483, { 0xa7, 0xbf, 0xad, 0xdc, 0xa7, 0xc2, 0x60, 0xe2 } };
static const GUID iid_clock  = { 0xcd63314f, 0x3fba, 0x4a1b, { 0x81, 0x2c, 0xef, 0x96, 0x35, 0x87, 0x28, 0xe7 } };
static const GUID iid_volume = { 0x93014887, 0x242d, 0x4068, { 0x8a, 0x15, 0xcf, 0x5e, 0x93, 0xb9, 0x0f, 0xe3 } };

static void set_env(const char *v)
{
    if (v) setenv("RT_HOST_AUDIO", v, 1);
    else unsetenv("RT_HOST_AUDIO");
}

static const char *wav_path(void)
{
    static char path[512];
    snprintf(path, sizeof path, "%s/wasapi_stream.wav", rt_test_state_dir());
    return path;
}

static double ms_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

/* A 16-bit PCM format (not the mix format): a plain WAVEFORMATEX, 18 bytes. */
static void pcm16(unsigned char *f)
{
    memset(f, 0, 18);
    f[0] = 1;                                   /* WAVE_FORMAT_PCM */
    f[2] = 2;                                   /* 2 channels */
    f[4] = 0x80; f[5] = 0xBB;                   /* 48000 */
    f[8] = 0x00; f[9] = 0x77; f[10] = 0x02;     /* 192000 bytes a second */
    f[12] = 4;                                  /* block align */
    f[14] = 16;                                 /* bits */
}

/* A client with the mix format, shared, EVENTCALLBACK, the given duration (100 ns units). */
static void *new_client(REFERENCE_TIME duration, HRESULT *hr)
{
    unsigned char mix[AC_MIX_FORMAT_BYTES];
    ac_fill_mix_format(mix);
    void *cl = rt_audio_client_new();
    *hr = COM_CALL(IAudioClient, Initialize, cl, AUDCLNT_SHAREMODE_SHARED, FLAGS_EVENT, duration, 0,
                   (const WAVEFORMATEX *)mix, NULL);
    return cl;
}

/* ---- 1. the error rows of the table ------------------------------------------------------------- */

TEST(core, wasapi_stream_rows)
{
    unsigned char mix[AC_MIX_FORMAT_BYTES];
    unsigned char pcm[18];
    const WAVEFORMATEX *fmt = (const WAVEFORMATEX *)mix;
    ac_fill_mix_format(mix);
    pcm16(pcm);
    set_env(NULL);
    HRESULT hr;
    UINT32 n32;
    REFERENCE_TIME lat;
    UINT64 u64;
    DWORD dw;
    void *cl, *obj;

    T_CASE("Initialize twice: _ALREADY_INITIALIZED");
    cl = rt_audio_client_new();
    T_EQ(COM_CALL(IAudioClient, Initialize, cl, 0, FLAGS_EVENT, 0, 0, fmt, NULL), S_OK);
    T_EQ(COM_CALL(IAudioClient, Initialize, cl, 0, FLAGS_EVENT, 0, 0, fmt, NULL), AUDCLNT_E_ALREADY_INITIALIZED);
    T_EQ(COM_CALL(IAudioClient, Release, cl), 0);

    T_CASE("Initialize with a NULL format: E_POINTER");
    cl = rt_audio_client_new();
    T_EQ(COM_CALL(IAudioClient, Initialize, cl, 0, FLAGS_EVENT, 0, 0, NULL, NULL), E_POINTER);
    T_CASE("Initialize in exclusive mode: _EXCLUSIVE_MODE_NOT_ALLOWED");
    T_EQ(COM_CALL(IAudioClient, Initialize, cl, AUDCLNT_SHAREMODE_EXCLUSIVE, FLAGS_EVENT, 0, 0, fmt, NULL),
         AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED);
    T_CASE("Initialize with a format that is not the mix format: _UNSUPPORTED_FORMAT");
    T_EQ(COM_CALL(IAudioClient, Initialize, cl, 0, FLAGS_EVENT, 0, 0, (const WAVEFORMATEX *)pcm, NULL),
         AUDCLNT_E_UNSUPPORTED_FORMAT);
    T_CASE("Initialize with a flag outside the four: a stop that names the flags");
    T_TRAPS(COM_CALL(IAudioClient, Initialize, cl, 0, 0x00000001, 0, 0, fmt, NULL),
            "IAudioClient::Initialize with StreamFlags 0x00000001");
    T_EQ(COM_CALL(IAudioClient, Release, cl), 0);

    T_CASE("before Initialize: GetBufferSize, GetStreamLatency, GetCurrentPadding, SetEventHandle, Start, Stop, Reset, GetService: _NOT_INITIALIZED");
    cl = rt_audio_client_new();
    T_EQ(COM_CALL(IAudioClient, GetBufferSize, cl, &n32), AUDCLNT_E_NOT_INITIALIZED);
    T_EQ(COM_CALL(IAudioClient, GetStreamLatency, cl, &lat), AUDCLNT_E_NOT_INITIALIZED);
    T_EQ(COM_CALL(IAudioClient, GetCurrentPadding, cl, &n32), AUDCLNT_E_NOT_INITIALIZED);
    T_EQ(COM_CALL(IAudioClient, SetEventHandle, cl, NULL), AUDCLNT_E_NOT_INITIALIZED);
    T_EQ(COM_CALL(IAudioClient, Start, cl), AUDCLNT_E_NOT_INITIALIZED);
    T_EQ(COM_CALL(IAudioClient, Stop, cl), AUDCLNT_E_NOT_INITIALIZED);
    T_EQ(COM_CALL(IAudioClient, Reset, cl), AUDCLNT_E_NOT_INITIALIZED);
    obj = (void *)&lat;
    T_EQ(COM_CALL(IAudioClient, GetService, cl, &iid_render, &obj), AUDCLNT_E_NOT_INITIALIZED);
    T_TRUE(obj == NULL);
    T_EQ(COM_CALL(IAudioClient, Release, cl), 0);

    T_CASE("NULL out-pointers: GetBufferSize, GetStreamLatency, GetCurrentPadding, GetService (ppv): E_POINTER");
    cl = new_client(0, &hr);
    T_EQ(hr, S_OK);
    T_EQ(COM_CALL(IAudioClient, GetBufferSize, cl, NULL), E_POINTER);
    T_EQ(COM_CALL(IAudioClient, GetStreamLatency, cl, NULL), E_POINTER);
    T_EQ(COM_CALL(IAudioClient, GetCurrentPadding, cl, NULL), E_POINTER);
    T_EQ(COM_CALL(IAudioClient, GetService, cl, &iid_render, NULL), E_POINTER);
    T_CASE("GetStreamLatency: one period, 100000; GetCurrentPadding of a new stream: 0");
    T_EQ(COM_CALL(IAudioClient, GetStreamLatency, cl, &lat), S_OK);
    T_EQ(lat, 100000);
    n32 = 99;
    T_EQ(COM_CALL(IAudioClient, GetCurrentPadding, cl, &n32), S_OK);
    T_EQ(n32, 0u);

    T_CASE("SetEventHandle without EVENTCALLBACK: _EVENTHANDLE_NOT_EXPECTED");
    void *plain = rt_audio_client_new();
    T_EQ(COM_CALL(IAudioClient, Initialize, plain, 0, 0, 0, 0, fmt, NULL), S_OK);
    HANDLE ev = CALL(kernel32, CreateEventA, NULL, FALSE, FALSE, NULL);
    T_EQ(COM_CALL(IAudioClient, SetEventHandle, plain, ev), AUDCLNT_E_EVENTHANDLE_NOT_EXPECTED);
    T_EQ(COM_CALL(IAudioClient, Release, plain), 0);

    T_CASE("SetEventHandle: NULL is E_INVALIDARG; a mutex is not an event: E_INVALIDARG; an event: S_OK");
    HANDLE mu = CALL(kernel32, CreateMutexA, NULL, FALSE, NULL);
    T_EQ(COM_CALL(IAudioClient, SetEventHandle, cl, NULL), E_INVALIDARG);
    T_EQ(COM_CALL(IAudioClient, SetEventHandle, cl, mu), E_INVALIDARG);
    T_EQ(COM_CALL(IAudioClient, SetEventHandle, cl, ev), S_OK);
    T_EQ(CALL(kernel32, CloseHandle, mu), TRUE);

    T_CASE("Start with EVENTCALLBACK and no event: _EVENTHANDLE_NOT_SET; Stop when not started: S_FALSE");
    void *noev = new_client(0, &hr);
    T_EQ(hr, S_OK);
    T_EQ(COM_CALL(IAudioClient, Start, noev), AUDCLNT_E_EVENTHANDLE_NOT_SET);
    T_EQ(COM_CALL(IAudioClient, Release, noev), 0);
    T_EQ(COM_CALL(IAudioClient, Stop, cl), S_FALSE);

    T_CASE("GetService: IAudioRenderClient and IAudioClock give objects, S_OK; any other interface: NULL, E_NOINTERFACE, one log line");
    void *rc = NULL, *ck = NULL, *vol = (void *)&lat;
    T_EQ(COM_CALL(IAudioClient, GetService, cl, &iid_render, &rc), S_OK);
    T_TRUE(rc != NULL);
    T_EQ(COM_CALL(IAudioClient, GetService, cl, &iid_clock, &ck), S_OK);
    T_TRUE(ck != NULL);
    T_EQ(COM_CALL(IAudioClient, GetService, cl, &iid_volume, &vol), E_NOINTERFACE);
    T_TRUE(vol == NULL);
    T_LOGGED(RT_CH_COM, "IAudioClient::GetService: interface {93014887-242d-4068-8a15-cf5e93b90fe3} is not one the audio client has");

    T_CASE("GetBuffer: NULL ppData E_POINTER; a second GetBuffer while one is open: _OUT_OF_ORDER; more than the room: _BUFFER_TOO_LARGE and NULL");
    BYTE *p = NULL;
    T_EQ(COM_CALL(IAudioRenderClient, GetBuffer, rc, 1, NULL), E_POINTER);
    T_EQ(COM_CALL(IAudioRenderClient, GetBuffer, rc, 961, &p), AUDCLNT_E_BUFFER_TOO_LARGE);
    T_TRUE(p == NULL);
    T_EQ(COM_CALL(IAudioRenderClient, GetBuffer, rc, 0, &p), S_OK);
    T_TRUE(p != NULL);
    T_EQ(COM_CALL(IAudioRenderClient, GetBuffer, rc, 0, &p), AUDCLNT_E_OUT_OF_ORDER);
    T_CASE("ReleaseBuffer: n above the grant: _INVALID_SIZE; a flag other than SILENT: E_INVALIDARG; then none open: n 0 S_OK, n 1 _OUT_OF_ORDER");
    T_EQ(COM_CALL(IAudioRenderClient, ReleaseBuffer, rc, 1, 0), AUDCLNT_E_INVALID_SIZE);
    T_EQ(COM_CALL(IAudioRenderClient, ReleaseBuffer, rc, 0, 0x4), E_INVALIDARG);
    T_EQ(COM_CALL(IAudioRenderClient, ReleaseBuffer, rc, 0, 0), S_OK);
    T_EQ(COM_CALL(IAudioRenderClient, ReleaseBuffer, rc, 0, 0), S_OK);
    T_EQ(COM_CALL(IAudioRenderClient, ReleaseBuffer, rc, 1, 0), AUDCLNT_E_OUT_OF_ORDER);

    T_CASE("the clock: GetFrequency 48000; GetCharacteristics 1; NULL pointers E_POINTER; GetPosition of a new stream 0");
    T_EQ(COM_CALL(IAudioClock, GetFrequency, ck, NULL), E_POINTER);
    T_EQ(COM_CALL(IAudioClock, GetFrequency, ck, &u64), S_OK);
    T_EQ(u64, 48000u);
    T_EQ(COM_CALL(IAudioClock, GetCharacteristics, ck, NULL), E_POINTER);
    T_EQ(COM_CALL(IAudioClock, GetCharacteristics, ck, &dw), S_OK);
    T_EQ(dw, 1u);
    T_EQ(COM_CALL(IAudioClock, GetPosition, ck, NULL, NULL), E_POINTER);
    u64 = 99;
    T_EQ(COM_CALL(IAudioClock, GetPosition, ck, &u64, NULL), S_OK);
    T_EQ(u64, 0u);

    T_CASE("the objects are released: the render client and the clock first, then the client, which goes to 0");
    T_EQ(COM_CALL(IAudioRenderClient, Release, rc), 0);
    T_EQ(COM_CALL(IAudioClock, Release, ck), 0);
    T_EQ(COM_CALL(IAudioClient, Release, cl), 0);
}

static void *open_sink_env(void)
{
    static char env[600];
    snprintf(env, sizeof env, "file-fast:%s", wav_path());
    remove(wav_path());
    set_env(env);
    return NULL;
}

/* ---- 2. the data path: the game's call, a 20 ms stream, the ramp into the WAV file ---------------- */

TEST(core, wasapi_stream_data)
{
    unsigned char mix[AC_MIX_FORMAT_BYTES];
    ac_fill_mix_format(mix);
    const WAVEFORMATEX *fmt = (const WAVEFORMATEX *)mix;
    HRESULT hr;
    UINT32 n32;
    void *cl, *rc = NULL, *ck = NULL;
    BYTE *p = NULL;
    HANDLE ev;
    open_sink_env();

    T_CASE("the game's own Initialize call: shared mode, EVENTCALLBACK, the mix format, duration 0, NULL session: S_OK, 960 frames");
    cl = rt_audio_client_new();
    T_EQ(COM_CALL(IAudioClient, Initialize, cl, AUDCLNT_SHAREMODE_SHARED, FLAGS_EVENT, 0, 0, fmt, NULL), S_OK);
    T_EQ(COM_CALL(IAudioClient, GetBufferSize, cl, &n32), S_OK);
    T_EQ(n32, 960u);
    T_EQ(COM_CALL(IAudioClient, Release, cl), 0);

    T_CASE("the data path: 20 ms, an event, the render client; 480 frames of a ramp; padding 480; Start; the event within 1 s; Stop");
    cl = new_client(200000, &hr);
    T_EQ(hr, S_OK);
    ev = CALL(kernel32, CreateEventA, NULL, FALSE, FALSE, NULL);
    T_EQ(COM_CALL(IAudioClient, SetEventHandle, cl, ev), S_OK);
    T_EQ(COM_CALL(IAudioClient, GetService, cl, &iid_render, &rc), S_OK);
    T_EQ(COM_CALL(IAudioRenderClient, GetBuffer, rc, 480, &p), S_OK);
    float *w = (float *)p;
    for (unsigned j = 0; j < 960; j++) w[j] = (float)j;      /* frame f, channel c = sample 2f+c = 2f+c */
    T_EQ(COM_CALL(IAudioRenderClient, ReleaseBuffer, rc, 480, 0), S_OK);
    n32 = 0;
    T_EQ(COM_CALL(IAudioClient, GetCurrentPadding, cl, &n32), S_OK);
    T_EQ(n32, 480u);
    T_EQ(COM_CALL(IAudioClient, Start, cl), S_OK);
    T_EQ(COM_CALL(IAudioClient, Start, cl), AUDCLNT_E_NOT_STOPPED);
    T_EQ(CALL(kernel32, WaitForSingleObject, ev, 1000), WAIT_OBJECT_0);
    T_EQ(COM_CALL(IAudioClient, Stop, cl), S_OK);
    T_EQ(COM_CALL(IAudioClient, Stop, cl), S_FALSE);

    T_CASE("released: the WAV file is 44 bytes of header, then the 480 frames exactly");
    T_EQ(COM_CALL(IAudioRenderClient, Release, rc), 0);
    T_EQ(COM_CALL(IAudioClient, Release, cl), 0);
    T_EQ(CALL(kernel32, CloseHandle, ev), TRUE);
    FILE *f = fopen(wav_path(), "rb");
    T_TRUE(f != NULL);
    if (f) {
        unsigned char hdr[44];
        float got[960];
        T_EQ(fread(hdr, 1, 44, f), 44u);
        T_TRUE(memcmp(hdr, "RIFF", 4) == 0 && memcmp(hdr + 8, "WAVE", 4) == 0);
        T_EQ(fread(got, sizeof(float), 960, f), 960u);
        bool same = true;
        for (unsigned j = 0; j < 960; j++)
            if (got[j] != (float)j) same = false;
        T_TRUE(same);
        fclose(f);
    }
    set_env(NULL);
    (void)ck;
}

/* ---- 3. room: GetBuffer for more than the room; the room is back after the device took a period ----- */

TEST(core, wasapi_stream_room)
{
    HRESULT hr;
    UINT32 n32;
    void *cl, *rc = NULL;
    BYTE *p = NULL;
    HANDLE ev;
    open_sink_env();

    cl = new_client(200000, &hr);
    T_EQ(hr, S_OK);
    ev = CALL(kernel32, CreateEventA, NULL, FALSE, FALSE, NULL);
    T_EQ(COM_CALL(IAudioClient, SetEventHandle, cl, ev), S_OK);
    T_EQ(COM_CALL(IAudioClient, GetService, cl, &iid_render, &rc), S_OK);

    T_CASE("GetBuffer of more than the room: _BUFFER_TOO_LARGE and NULL; the room after 480 written is 480");
    T_EQ(COM_CALL(IAudioRenderClient, GetBuffer, rc, 480, &p), S_OK);
    T_EQ(COM_CALL(IAudioRenderClient, ReleaseBuffer, rc, 480, 0), S_OK);
    p = (BYTE *)&hr;
    T_EQ(COM_CALL(IAudioRenderClient, GetBuffer, rc, 481, &p), AUDCLNT_E_BUFFER_TOO_LARGE);
    T_TRUE(p == NULL);
    T_EQ(COM_CALL(IAudioRenderClient, GetBuffer, rc, 480, &p), S_OK);
    T_EQ(COM_CALL(IAudioRenderClient, ReleaseBuffer, rc, 0, 0), S_OK);

    T_CASE("after the device took a period (the event) and Stop, the padding is 0 and the room is 960 again");
    T_EQ(COM_CALL(IAudioClient, Start, cl), S_OK);
    T_EQ(CALL(kernel32, WaitForSingleObject, ev, 1000), WAIT_OBJECT_0);
    T_EQ(COM_CALL(IAudioClient, Stop, cl), S_OK);
    n32 = 99;
    T_EQ(COM_CALL(IAudioClient, GetCurrentPadding, cl, &n32), S_OK);
    T_EQ(n32, 0u);
    T_EQ(COM_CALL(IAudioRenderClient, GetBuffer, rc, 960, &p), S_OK);
    T_EQ(COM_CALL(IAudioRenderClient, ReleaseBuffer, rc, 0, 0), S_OK);
    T_EQ(COM_CALL(IAudioRenderClient, GetBuffer, rc, 961, &p), AUDCLNT_E_BUFFER_TOO_LARGE);

    T_CASE("released");
    T_EQ(COM_CALL(IAudioRenderClient, Release, rc), 0);
    T_EQ(COM_CALL(IAudioClient, Release, cl), 0);
    T_EQ(CALL(kernel32, CloseHandle, ev), TRUE);
    set_env(NULL);
}

/* ---- 4. timing, no sound (RT_HOST_AUDIO unset): the device signals 5 to 15 times in 100 ms ------------ */

TEST(core, wasapi_stream_timing)
{
    HRESULT hr;
    void *cl, *rc = NULL, *ck = NULL;
    HANDLE ev;
    UINT64 pos0 = 0, pos1 = 0;
    set_env(NULL);

    cl = new_client(200000, &hr);
    T_EQ(hr, S_OK);
    ev = CALL(kernel32, CreateEventA, NULL, FALSE, FALSE, NULL);
    T_EQ(COM_CALL(IAudioClient, SetEventHandle, cl, ev), S_OK);
    T_EQ(COM_CALL(IAudioClient, GetService, cl, &iid_clock, &ck), S_OK);
    T_EQ(COM_CALL(IAudioClient, GetService, cl, &iid_render, &rc), S_OK);

    T_CASE("after Start, in 100 ms of waiting the event is signalled 5 to 15 times");
    T_EQ(COM_CALL(IAudioClient, Start, cl), S_OK);
    T_EQ(COM_CALL(IAudioClock, GetPosition, ck, &pos0, NULL), S_OK);
    double t0 = ms_now();
    unsigned count = 0;
    while (ms_now() - t0 < 100.0)
        if (CALL(kernel32, WaitForSingleObject, ev, 1) == WAIT_OBJECT_0) count++;
    T_EQ(COM_CALL(IAudioClock, GetPosition, ck, &pos1, NULL), S_OK);
    T_EQ(COM_CALL(IAudioClient, Stop, cl), S_OK);
    T_TRUE(count >= 5 && count <= 15);

    T_CASE("the clock's position grows by about 480 frames a signal (no writes by the game)");
    UINT64 delta = pos1 - pos0;
    T_TRUE(delta >= 480ull * count && delta <= 480ull * (count + 2));

    T_CASE("released");
    T_EQ(COM_CALL(IAudioRenderClient, Release, rc), 0);
    T_EQ(COM_CALL(IAudioClock, Release, ck), 0);
    T_EQ(COM_CALL(IAudioClient, Release, cl), 0);
    T_EQ(CALL(kernel32, CloseHandle, ev), TRUE);
}

/* ---- 5. Reset: refused while started and while a GetBuffer is open; empties the ring after Stop ------- */

TEST(core, wasapi_stream_reset)
{
    HRESULT hr;
    UINT32 n32;
    UINT64 u64;
    void *cl, *rc = NULL, *ck = NULL;
    BYTE *p = NULL;
    HANDLE ev;
    set_env(NULL);

    cl = new_client(200000, &hr);
    T_EQ(hr, S_OK);
    ev = CALL(kernel32, CreateEventA, NULL, FALSE, FALSE, NULL);
    T_EQ(COM_CALL(IAudioClient, SetEventHandle, cl, ev), S_OK);
    T_EQ(COM_CALL(IAudioClient, GetService, cl, &iid_render, &rc), S_OK);
    T_EQ(COM_CALL(IAudioClient, GetService, cl, &iid_clock, &ck), S_OK);

    T_CASE("Reset with frames in the ring and nothing started: S_OK, padding 0");
    T_EQ(COM_CALL(IAudioRenderClient, GetBuffer, rc, 480, &p), S_OK);
    T_EQ(COM_CALL(IAudioRenderClient, ReleaseBuffer, rc, 480, 0), S_OK);
    T_EQ(COM_CALL(IAudioClient, Reset, cl), S_OK);
    n32 = 99;
    T_EQ(COM_CALL(IAudioClient, GetCurrentPadding, cl, &n32), S_OK);
    T_EQ(n32, 0u);

    T_CASE("Reset with a GetBuffer open: _BUFFER_OPERATION_PENDING");
    T_EQ(COM_CALL(IAudioRenderClient, GetBuffer, rc, 0, &p), S_OK);
    T_EQ(COM_CALL(IAudioClient, Reset, cl), AUDCLNT_E_BUFFER_OPERATION_PENDING);
    T_EQ(COM_CALL(IAudioRenderClient, ReleaseBuffer, rc, 0, 0), S_OK);

    T_CASE("Reset while started: _NOT_STOPPED");
    T_EQ(COM_CALL(IAudioClient, Start, cl), S_OK);
    T_EQ(COM_CALL(IAudioClient, Reset, cl), AUDCLNT_E_NOT_STOPPED);
    T_EQ(CALL(kernel32, WaitForSingleObject, ev, 1000), WAIT_OBJECT_0);
    T_EQ(COM_CALL(IAudioClient, Stop, cl), S_OK);

    T_CASE("Reset after Stop: S_OK; padding 0 and the clock's position 0");
    T_EQ(COM_CALL(IAudioClient, Reset, cl), S_OK);
    n32 = 99;
    T_EQ(COM_CALL(IAudioClient, GetCurrentPadding, cl, &n32), S_OK);
    T_EQ(n32, 0u);
    u64 = 99;
    T_EQ(COM_CALL(IAudioClock, GetPosition, ck, &u64, NULL), S_OK);
    T_EQ(u64, 0u);

    T_CASE("released");
    T_EQ(COM_CALL(IAudioRenderClient, Release, rc), 0);
    T_EQ(COM_CALL(IAudioClock, Release, ck), 0);
    T_EQ(COM_CALL(IAudioClient, Release, cl), 0);
    T_EQ(CALL(kernel32, CloseHandle, ev), TRUE);
}
