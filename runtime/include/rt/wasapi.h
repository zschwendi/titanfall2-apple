/* The WASAPI audio client the game gets from IMMDevice::Activate (runtime/xaudio/wasapi.c, the
 * bodies in runtime/xaudio/<Interface>/<Method>.c).  The client is a stream on the made-up
 * endpoint: the game writes frames through IAudioRenderClient into a ring, a device thread takes
 * a period at a time to the audio sink (rt/audio.h) and signals the game's event after each one,
 * and IAudioClock reports the frames the device has taken. */
#ifndef RT_WASAPI_H
#define RT_WASAPI_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#include "rt/audio.h"
#include "rt/xaudio.h"
#include "com_ids.h"            /* generated: rt_iface_IAudioClient, IAudioRenderClient, IAudioClock */

/* The state of one audio client (bytes per object in rt_com_new).  Every field is guarded by
 * `mu` (created at rt_audio_client_new, destroyed with the object); the device thread takes it
 * for each step, and never runs guest code. */
typedef struct AcClient {
    pthread_mutex_t mu;
    bool        initialized;
    bool        started;            /* the device thread is to run */
    bool        thread_on;          /* the device thread exists and must be joined */
    bool        written;            /* a ReleaseBuffer has put frames in the ring */
    bool        open;               /* a GetBuffer is open (granted frames) */
    uint32_t    flags;              /* Initialize's StreamFlags */
    uint32_t    buffer_frames;      /* the ring's size; the write buffer's too */
    uint32_t    padding;            /* frames in the ring, written and not yet taken */
    uint32_t    head;               /* ring index of the oldest frame in it */
    uint32_t    granted;            /* frames of the open GetBuffer */
    uint64_t    position;           /* frames the device has taken since the stream was made */
    uint64_t    underruns;          /* periods the device took short of frames, after a write */
    float      *ring;               /* buffer_frames frames of 2 floats (host memory) */
    void       *write_buf;          /* buffer_frames frames, guest-visible (rt_galloc) */
    struct RtObject *event;         /* SetEventHandle's event, a reference of ours */
    RtAudioSink *sink;              /* opened by the first Start, closed at destroy */
    pthread_t   thread;
} AcClient;
extern const RtComClass rt_audio_client_class;

/* The objects a client hands out (GetService): each holds a reference on the client through
 * rt_com_hold, released when it is destroyed.  `ac` is the client's state. */
typedef struct AcBound {
    void       *client;             /* the client's face, held */
    AcClient   *ac;
} AcBound;
extern const RtComClass rt_audio_render_class;
extern const RtComClass rt_audio_clock_class;

/* A new audio client object (one reference). */
void *rt_audio_client_new(void);
/* A render client or a clock bound to the client whose face is `client_face` (one reference). */
void *rt_audio_render_new(void *client_face);
void *rt_audio_clock_new(void *client_face);

/* Device-thread control, used by the bodies (all take the client's mutex themselves). */
HRESULT ac_device_start(AcClient *s);
void    ac_device_stop_join(AcClient *s);     /* ends the thread (if any) and waits for it */
void    ac_device_destroy(void *state);       /* the client's destroy function */

/* One log line for an interface the client does not have: `where` names the method. */
void ac_log_no_interface(const char *where, const GUID *iid);

/* The made-up endpoint's mix format */
#define AC_MIX_FORMAT_BYTES      40
#define AC_MIX_CHANNELS          2
#define AC_MIX_RATE              48000
#define AC_MIX_BITS              32
/* The device period (one period is taken from the ring at a time), and the bounds of the
 * buffer in frames.  AC_PERIOD_FRAMES is AC_DEVICE_PERIOD at AC_MIX_RATE. */
#define AC_PERIOD_FRAMES         480
#define AC_MIN_BUFFER_FRAMES     960
#define AC_MAX_BUFFER_FRAMES     96000
void ac_fill_mix_format(void *buf);
/* Whether the WAVEFORMATEX (plain, tag 3) or WAVEFORMATEXTENSIBLE (tag 0xFFFE, cbSize 22 or more,
 * sub-format IEEE float) at `fmt` has the mix format's channels, rate, bits and float-ness.  Reads
 * only the bytes the form has; the caller has checked the pointer is not NULL. */
bool ac_format_is_mix(const void *fmt);

/* The device period and the minimum period, in units of 100 ns (REFERENCE_TIME). */
#define AC_DEVICE_PERIOD         100000
#define AC_MIN_PERIOD            30000

/* Errors and share modes (audiosessiontypes.h, audioclient.h). */
#define AUDCLNT_E_NOT_INITIALIZED              ((HRESULT)0x88890001)
#define AUDCLNT_E_ALREADY_INITIALIZED          ((HRESULT)0x88890002)
#define AUDCLNT_E_NOT_STOPPED                  ((HRESULT)0x88890005)
#define AUDCLNT_E_BUFFER_TOO_LARGE             ((HRESULT)0x88890006)
#define AUDCLNT_E_OUT_OF_ORDER                 ((HRESULT)0x88890007)
#define AUDCLNT_E_UNSUPPORTED_FORMAT           ((HRESULT)0x88890008)
#define AUDCLNT_E_INVALID_SIZE                 ((HRESULT)0x88890009)
#define AUDCLNT_E_BUFFER_OPERATION_PENDING     ((HRESULT)0x8889000B)
#define AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED   ((HRESULT)0x8889000E)
#define AUDCLNT_E_EVENTHANDLE_NOT_EXPECTED     ((HRESULT)0x88890011)
#define AUDCLNT_E_EVENTHANDLE_NOT_SET          ((HRESULT)0x88890014)
#define AUDCLNT_SHAREMODE_SHARED               0
#define AUDCLNT_SHAREMODE_EXCLUSIVE            1
/* Initialize's StreamFlags and ReleaseBuffer's flags (audioclient.h). */
#define AUDCLNT_STREAMFLAGS_EVENTCALLBACK      0x00040000
#define AUDCLNT_STREAMFLAGS_NOPERSIST          0x00080000
#define AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM     0x80000000
#define AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY 0x08000000
#define AUDCLNT_BUFFERFLAGS_SILENT             0x2

#endif /* RT_WASAPI_H */
