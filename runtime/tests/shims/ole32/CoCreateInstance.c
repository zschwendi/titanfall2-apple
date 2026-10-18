#include "rt/test.h"
#include "rt/log.h"
#include "rt/win/com.h"
#include "rt/heap.h"
#include "rt/mmdev.h"
#include "rt/wasapi.h"
#include <string.h>

#define CLASS_E_NOAGGREGATION ((HRESULT)0x80040110)   /* as the shim: not in rt/win */

TEST(ole32, CoCreateInstance)
{
    /* The bytes 1 to 16 as a CLSID: Data1 is 0x04030201 (little endian), Data4 is 9 to 16. */
    GUID clsid = { 0x04030201, 0x0605, 0x0807, { 9, 10, 11, 12, 13, 14, 15, 16 } };
    /* IID_IUnknown, {00000000-0000-0000-C000-000000000046}. */
    GUID iid   = { 0x00000000, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
    LPVOID p;
    HRESULT hr;

    T_CASE("class not registered returns REGDB_E_CLASSNOTREG and sets *ppv to NULL");
    p = (LPVOID)&clsid;                   /* preset: not NULL */
    hr = CALL(ole32, CoCreateInstance, &clsid, NULL, 0x1 /* CLSCTX_INPROC_SERVER */, &iid, &p);
    T_EQ(hr, REGDB_E_CLASSNOTREG);
    T_TRUE(p == NULL);

    T_CASE("class not registered logs the class and the interface");
    p = (LPVOID)&clsid;
    hr = CALL(ole32, CoCreateInstance, &clsid, NULL, 0x1, &iid, &p);
    T_EQ(hr, REGDB_E_CLASSNOTREG);
    T_LOGGED(RT_CH_IMPORT, "CoCreateInstance: class {04030201-0605-0807-090A-0B0C0D0E0F10} is not registered (interface {00000000-0000-0000-C000-000000000046})");

    T_CASE("ppv NULL returns E_POINTER");
    hr = CALL(ole32, CoCreateInstance, &clsid, NULL, 0x1, &iid, NULL);
    T_EQ(hr, E_POINTER);

    T_CASE("leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(ole32, CoCreateInstance, &clsid, NULL, 0x1, &iid, &p);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0x1234);
    CALL(ole32, CoCreateInstance, &clsid, NULL, 0x1, &iid, NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("the thread's COM state is not looked at (Windows gives CO_E_NOTINITIALIZED without COM)");
    p = (LPVOID)&clsid;                   /* no COM on this thread yet */
    hr = CALL(ole32, CoCreateInstance, &clsid, NULL, 0x1, &iid, &p);
    T_EQ(hr, REGDB_E_CLASSNOTREG);
    T_TRUE(p == NULL);
    T_EQ(CALL(ole32, CoInitializeEx, NULL, COINIT_MULTITHREADED), S_OK);
    p = (LPVOID)&clsid;                   /* COM initialised: the same answer, so the state is not read */
    hr = CALL(ole32, CoCreateInstance, &clsid, NULL, 0x1, &iid, &p);
    T_EQ(hr, REGDB_E_CLASSNOTREG);
    T_TRUE(p == NULL);
    CALL(ole32, CoUninitialize);          /* back to no COM on the thread */

    T_TRAPS(CALL(ole32, CoCreateInstance, NULL, NULL, 0x1, &iid, &p), "rclsid NULL");
    T_TRAPS(CALL(ole32, CoCreateInstance, &clsid, NULL, 0x1, NULL, &p), "riid NULL");

    /* The sound classes (runtime/xaudio/classes.c), as the game asks for them. */
    GUID xa27 = { 0x5A508685, 0xA254, 0x4FBA, { 0x9B, 0x82, 0x9A, 0x24, 0xB0, 0x03, 0x06, 0xAF } };
    GUID xa26 = { 0x3EDA9B49, 0x2085, 0x498B, { 0x9B, 0xB2, 0x39, 0xA6, 0x77, 0x84, 0x93, 0xDE } };
    GUID mmen = { 0xBCDE0395, 0xE52F, 0x467C, { 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E } };
    GUID ixa2 = { 0x8BCF1F58, 0x9FE7, 0x4583, { 0x8A, 0xC6, 0xE2, 0xAD, 0xC4, 0x65, 0xC8, 0xBB } };  /* IID_IXAudio2 */
    GUID immd = { 0xA95664D2, 0x9614, 0x4F35, { 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6 } };  /* IID_IMMDeviceEnumerator */
    LPVOID xo, mo;

    T_CASE("a class of the table makes an object and writes its face for the IID through *ppv (S_OK)");
    xo = NULL;
    T_EQ(CALL(ole32, CoCreateInstance, &xa27, NULL, 0x1, &ixa2, &xo), S_OK);
    T_TRUE(xo != NULL);
    mo = NULL;
    T_EQ(CALL(ole32, CoCreateInstance, &mmen, NULL, 0x1, &immd, &mo), S_OK);
    T_TRUE(mo != NULL);

    T_CASE("the XAudio2 object: AddRef and Release twice give 2, 1, 0");
    T_EQ(COM_CALL(IXAudio2, AddRef, xo), 2);
    T_EQ(COM_CALL(IXAudio2, Release, xo), 1);
    T_EQ(COM_CALL(IXAudio2, Release, xo), 0);

    T_CASE("a class of the table asked for an interface it does not present gives E_NOINTERFACE and *ppv NULL with nothing left alive");
    mo = (LPVOID)&clsid;
    T_EQ(CALL(ole32, CoCreateInstance, &xa27, NULL, 0x1, &immd, &mo), E_NOINTERFACE);
    T_TRUE(mo == NULL);

    T_CASE("the XAudio2 2.6 class is not in the table: REGDB_E_CLASSNOTREG (the game asks for it only after 2.7 failed)");
    xo = (LPVOID)&clsid;
    T_EQ(CALL(ole32, CoCreateInstance, &xa26, NULL, 0x1, &ixa2, &xo), REGDB_E_CLASSNOTREG);
    T_TRUE(xo == NULL);

    T_CASE("a non-NULL outer for a class of the table gives CLASS_E_NOAGGREGATION and makes nothing");
    mo = (LPVOID)&clsid;
    T_EQ(CALL(ole32, CoCreateInstance, &mmen, (LPUNKNOWN)&clsid, 0x1, &immd, &mo), CLASS_E_NOAGGREGATION);
    T_TRUE(mo == NULL);

    T_CASE("calling IXAudio2::Initialize traps with the method's name (it has no body yet)");
    xo = NULL;
    T_EQ(CALL(ole32, CoCreateInstance, &xa27, NULL, 0x1, &ixa2, &xo), S_OK);
    T_TRAPS(COM_CALL(IXAudio2, Initialize, xo, 0, 0), "unimplemented interface method IXAudio2::Initialize");

    /* The MMDevice enumerator's answers (runtime/xaudio/IMMDevice*): one made-up output endpoint. */
    LPVOID en;
    IMMDevice *dv;
    IMMDeviceCollection *cl;
    uint16_t endpoint_id[64], other_id[64];
    UINT n;
    DWORD st;
    for (unsigned i = 0; i < 64; i++) endpoint_id[i] = other_id[i] = 0;
    {
        const char *ids = "{0.0.0.00000000}.{00000000-0000-0000-0000-000000000001}";
        for (unsigned i = 0; ids[i]; i++) endpoint_id[i] = (uint16_t)ids[i];
        const char *oth = "{0.0.0.00000000}.{00000000-0000-0000-0000-000000000002}";
        for (unsigned i = 0; oth[i]; i++) other_id[i] = (uint16_t)oth[i];
    }
    en = NULL;
    T_EQ(CALL(ole32, CoCreateInstance, &mmen, NULL, 0x1, &immd, &en), S_OK);

    T_CASE("GetDefaultAudioEndpoint: NULL out-pointer gives E_POINTER");
    T_EQ(COM_CALL(IMMDeviceEnumerator, GetDefaultAudioEndpoint, en, MM_E_RENDER, MM_E_CONSOLE, NULL), E_POINTER);
    T_CASE("GetDefaultAudioEndpoint: a flow above eCapture or a role above 2 gives E_INVALIDARG and NULL");
    dv = (IMMDevice *)&clsid;
    T_EQ(COM_CALL(IMMDeviceEnumerator, GetDefaultAudioEndpoint, en, 3, MM_E_CONSOLE, &dv), E_INVALIDARG);
    T_TRUE(dv == NULL);
    dv = (IMMDevice *)&clsid;
    T_EQ(COM_CALL(IMMDeviceEnumerator, GetDefaultAudioEndpoint, en, MM_E_RENDER, 3, &dv), E_INVALIDARG);
    T_TRUE(dv == NULL);
    T_CASE("GetDefaultAudioEndpoint: eCapture gives E_NOTFOUND and NULL (the microphone is never opened)");
    dv = (IMMDevice *)&clsid;
    T_EQ(COM_CALL(IMMDeviceEnumerator, GetDefaultAudioEndpoint, en, MM_E_CAPTURE, MM_E_CONSOLE, &dv), E_NOTFOUND);
    T_TRUE(dv == NULL);
    T_CASE("GetDefaultAudioEndpoint: eRender gives S_OK and a device, for each role");
    for (unsigned role = 0; role <= 2; role++) {
        dv = NULL;
        T_EQ(COM_CALL(IMMDeviceEnumerator, GetDefaultAudioEndpoint, en, MM_E_RENDER, role, &dv), S_OK);
        T_TRUE(dv != NULL);
        T_EQ(COM_CALL(IMMDevice, Release, dv), 0);
    }
    T_CASE("GetDefaultAudioEndpoint: the device releases to nothing (last reference)");
    dv = NULL;
    T_EQ(COM_CALL(IMMDeviceEnumerator, GetDefaultAudioEndpoint, en, MM_E_RENDER, MM_E_CONSOLE, &dv), S_OK);
    T_EQ(COM_CALL(IMMDevice, Release, dv), 0);

    T_CASE("EnumAudioEndpoints: NULL gives E_POINTER; a flow above eAll or a mask of 0 or bits outside 0xF gives E_INVALIDARG");
    T_EQ(COM_CALL(IMMDeviceEnumerator, EnumAudioEndpoints, en, MM_E_RENDER, MM_DEVICE_STATE_ACTIVE, NULL), E_POINTER);
    cl = (IMMDeviceCollection *)&clsid;
    T_EQ(COM_CALL(IMMDeviceEnumerator, EnumAudioEndpoints, en, 3, MM_DEVICE_STATE_ACTIVE, &cl), E_INVALIDARG);
    T_TRUE(cl == NULL);
    cl = (IMMDeviceCollection *)&clsid;
    T_EQ(COM_CALL(IMMDeviceEnumerator, EnumAudioEndpoints, en, MM_E_RENDER, 0, &cl), E_INVALIDARG);
    T_TRUE(cl == NULL);
    cl = (IMMDeviceCollection *)&clsid;
    T_EQ(COM_CALL(IMMDeviceEnumerator, EnumAudioEndpoints, en, MM_E_RENDER, 0x10, &cl), E_INVALIDARG);
    T_TRUE(cl == NULL);
    T_CASE("EnumAudioEndpoints: a collection of 1 for render or all with ACTIVE, else 0");
    cl = NULL;
    T_EQ(COM_CALL(IMMDeviceEnumerator, EnumAudioEndpoints, en, MM_E_RENDER, MM_DEVICE_STATE_ACTIVE, &cl), S_OK);
    n = 99;
    T_EQ(COM_CALL(IMMDeviceCollection, GetCount, cl, &n), S_OK);
    T_EQ(n, 1);
    T_EQ(COM_CALL(IMMDeviceCollection, Release, cl), 0);
    cl = NULL;
    T_EQ(COM_CALL(IMMDeviceEnumerator, EnumAudioEndpoints, en, MM_E_ALL, MM_DEVICE_STATEMASK_ALL, &cl), S_OK);
    T_EQ(COM_CALL(IMMDeviceCollection, GetCount, cl, &n), S_OK);
    T_EQ(n, 1);
    T_EQ(COM_CALL(IMMDeviceCollection, Release, cl), 0);
    cl = NULL;
    T_EQ(COM_CALL(IMMDeviceEnumerator, EnumAudioEndpoints, en, MM_E_CAPTURE, MM_DEVICE_STATEMASK_ALL, &cl), S_OK);
    T_EQ(COM_CALL(IMMDeviceCollection, GetCount, cl, &n), S_OK);
    T_EQ(n, 0);
    T_EQ(COM_CALL(IMMDeviceCollection, Release, cl), 0);
    cl = NULL;
    T_EQ(COM_CALL(IMMDeviceEnumerator, EnumAudioEndpoints, en, MM_E_RENDER, 0x2 /* DISABLED */, &cl), S_OK);
    T_EQ(COM_CALL(IMMDeviceCollection, GetCount, cl, &n), S_OK);
    T_EQ(n, 0);
    T_EQ(COM_CALL(IMMDeviceCollection, Release, cl), 0);

    T_CASE("GetDevice: NULL id or NULL out-pointer gives E_POINTER");
    T_EQ(COM_CALL(IMMDeviceEnumerator, GetDevice, en, NULL, &dv), E_POINTER);
    T_EQ(COM_CALL(IMMDeviceEnumerator, GetDevice, en, endpoint_id, NULL), E_POINTER);
    T_CASE("GetDevice: the endpoint's id gives a device (S_OK); any other id gives E_NOTFOUND and NULL");
    dv = NULL;
    T_EQ(COM_CALL(IMMDeviceEnumerator, GetDevice, en, endpoint_id, &dv), S_OK);
    T_TRUE(dv != NULL);
    T_EQ(COM_CALL(IMMDevice, Release, dv), 0);
    dv = (IMMDevice *)&clsid;
    T_EQ(COM_CALL(IMMDeviceEnumerator, GetDevice, en, other_id, &dv), E_NOTFOUND);
    T_TRUE(dv == NULL);

    T_CASE("RegisterEndpointNotificationCallback: NULL gives E_POINTER; Unregister: NULL gives E_POINTER, an unregistered pointer E_NOTFOUND");
    T_EQ(COM_CALL(IMMDeviceEnumerator, RegisterEndpointNotificationCallback, en, NULL), E_POINTER);
    T_EQ(COM_CALL(IMMDeviceEnumerator, UnregisterEndpointNotificationCallback, en, NULL), E_POINTER);
    T_EQ(COM_CALL(IMMDeviceEnumerator, UnregisterEndpointNotificationCallback, en, (LPVOID)&other_id), E_NOTFOUND);
    T_CASE("RegisterEndpointNotificationCallback: a registered pointer is kept (S_OK); Unregister forgets it (S_OK, then E_NOTFOUND)");
    T_EQ(COM_CALL(IMMDeviceEnumerator, RegisterEndpointNotificationCallback, en, (LPVOID)&other_id), S_OK);
    T_EQ(COM_CALL(IMMDeviceEnumerator, UnregisterEndpointNotificationCallback, en, (LPVOID)&other_id), S_OK);
    T_EQ(COM_CALL(IMMDeviceEnumerator, UnregisterEndpointNotificationCallback, en, (LPVOID)&other_id), E_NOTFOUND);
    T_CASE("RegisterEndpointNotificationCallback: eight pointers are kept; a ninth is a stop");
    for (unsigned i = 0; i < 8; i++)
        T_EQ(COM_CALL(IMMDeviceEnumerator, RegisterEndpointNotificationCallback, en, (LPVOID)(uintptr_t)(0x1000 + i * 8)), S_OK);
    T_TRAPS(COM_CALL(IMMDeviceEnumerator, RegisterEndpointNotificationCallback, en, (LPVOID)(uintptr_t)0x2000), "IMMDeviceEnumerator::RegisterEndpointNotificationCallback");

    T_CASE("GetCount: NULL gives E_POINTER; Item: NULL gives E_POINTER, an index at or past the count E_INVALIDARG and NULL");
    cl = NULL;
    T_EQ(COM_CALL(IMMDeviceEnumerator, EnumAudioEndpoints, en, MM_E_RENDER, MM_DEVICE_STATE_ACTIVE, &cl), S_OK);
    T_EQ(COM_CALL(IMMDeviceCollection, GetCount, cl, NULL), E_POINTER);
    T_EQ(COM_CALL(IMMDeviceCollection, Item, cl, 0, NULL), E_POINTER);
    dv = (IMMDevice *)&clsid;
    T_EQ(COM_CALL(IMMDeviceCollection, Item, cl, 1, &dv), E_INVALIDARG);
    T_TRUE(dv == NULL);
    dv = NULL;
    T_EQ(COM_CALL(IMMDeviceCollection, Item, cl, 0, &dv), S_OK);
    T_TRUE(dv != NULL);
    T_EQ(COM_CALL(IMMDevice, Release, dv), 0);
    T_EQ(COM_CALL(IMMDeviceCollection, Release, cl), 0);

    T_CASE("IMMDevice::GetState: NULL gives E_POINTER; else DEVICE_STATE_ACTIVE");
    dv = NULL;
    T_EQ(COM_CALL(IMMDeviceEnumerator, GetDefaultAudioEndpoint, en, MM_E_RENDER, MM_E_CONSOLE, &dv), S_OK);
    T_EQ(COM_CALL(IMMDevice, GetState, dv, NULL), E_POINTER);
    st = 0;
    T_EQ(COM_CALL(IMMDevice, GetState, dv, &st), S_OK);
    T_EQ(st, MM_DEVICE_STATE_ACTIVE);

    T_CASE("IMMDevice::GetId: NULL gives E_POINTER; else the endpoint's id as wide text, which GetDevice takes back");
    T_EQ(COM_CALL(IMMDevice, GetId, dv, NULL), E_POINTER);
    {
        WCHAR *id = NULL;
        T_EQ(COM_CALL(IMMDevice, GetId, dv, &id), S_OK);
        T_TRUE(id != NULL);
        static const char want[] = MM_ENDPOINT_ID;
        bool same = true;
        for (size_t i = 0; i < sizeof want; i++) same = same && id[i] == (WCHAR)(unsigned char)want[i];
        T_TRUE(same);
        IMMDevice *again = NULL;
        T_EQ(COM_CALL(IMMDeviceEnumerator, GetDevice, en, id, &again), S_OK);
        T_TRUE(again != NULL);
        T_EQ(COM_CALL(IMMDevice, Release, again), 0);
        rt_gfree(id);
    }

    /* IMMDevice::Activate and the audio client (runtime/xaudio/IMMDevice/Activate.c and the IAudioClient bodies). */
    {
        GUID ac_iid = { 0x1cb9ad4c, 0xdbfa, 0x4c32, { 0xb1, 0x78, 0xc2, 0xf5, 0x68, 0xa7, 0x03, 0xb2 } };
        GUID rc_iid = { 0xf294acfc, 0x3146, 0x4483, { 0xa7, 0xbf, 0xad, 0xdc, 0xa7, 0xc2, 0x60, 0xe2 } };
        LPVOID ac = NULL;
        WAVEFORMATEX *mf = NULL, *cm = NULL;
        unsigned char b[40], mix[40];
        int64_t dp = 0, mp = 0;
        HRESULT hr;

        T_CASE("IMMDevice::Activate: NULL ppv gives E_POINTER");
        T_EQ(COM_CALL(IMMDevice, Activate, dv, &ac_iid, 0x17, NULL, NULL), E_POINTER);
        T_CASE("IMMDevice::Activate: IAudioClient gives an object (S_OK) that releases to nothing");
        ac = NULL;
        T_EQ(COM_CALL(IMMDevice, Activate, dv, &ac_iid, 0x17, NULL, &ac), S_OK);
        T_TRUE(ac != NULL);
        T_EQ(COM_CALL(IAudioClient, Release, ac), 0);
        T_CASE("IMMDevice::Activate: another interface gives NULL and E_NOINTERFACE, and one log line with its id");
        ac = (LPVOID)&clsid;
        T_EQ(COM_CALL(IMMDevice, Activate, dv, &rc_iid, 0x17, NULL, &ac), E_NOINTERFACE);
        T_TRUE(ac == NULL);
        T_LOGGED(RT_CH_COM, "MMDevice::Activate: interface {f294acfc-3146-4483-a7bf-addca7c260e2} is not one the endpoint has (only IAudioClient); E_NOINTERFACE");

        T_EQ(COM_CALL(IMMDevice, Activate, dv, &ac_iid, 0x17, NULL, &ac), S_OK);
        T_CASE("IAudioClient::GetMixFormat: NULL gives E_POINTER; else 40 bytes of the mix format, field by field");
        T_EQ(COM_CALL(IAudioClient, GetMixFormat, ac, NULL), E_POINTER);
        mf = NULL;
        T_EQ(COM_CALL(IAudioClient, GetMixFormat, ac, &mf), S_OK);
        T_TRUE(mf != NULL);
        memcpy(b, mf, 40);
        T_EQ(b[0] | (b[1] << 8), 0xFFFE);        /* wFormatTag */
        T_EQ(b[2] | (b[3] << 8), 2);             /* nChannels */
        T_EQ(b[4] | (b[5] << 8) | (b[6] << 16) | (b[7] << 24), 48000);   /* nSamplesPerSec */
        T_EQ(b[8] | (b[9] << 8) | (b[10] << 16) | (b[11] << 24), 384000); /* nAvgBytesPerSec */
        T_EQ(b[12] | (b[13] << 8), 8);           /* nBlockAlign */
        T_EQ(b[14] | (b[15] << 8), 32);          /* wBitsPerSample */
        T_EQ(b[16] | (b[17] << 8), 22);          /* cbSize */
        T_EQ(b[18] | (b[19] << 8), 32);          /* wValidBitsPerSample */
        T_EQ(b[20] | (b[21] << 8) | (b[22] << 16) | (b[23] << 24), 3);  /* dwChannelMask */
        T_TRUE(memcmp(b + 24, "\x03\x00\x00\x00\x00\x00\x10\x00\x80\x00\x00\xAA\x00\x38\x9B\x71", 16) == 0);
        CALL(ole32, CoTaskMemFree, mf);

        T_CASE("IAudioClient::GetDevicePeriod: both NULL gives E_POINTER; each non-NULL pointer gets its value");
        T_EQ(COM_CALL(IAudioClient, GetDevicePeriod, ac, NULL, NULL), E_POINTER);
        dp = mp = -1;
        T_EQ(COM_CALL(IAudioClient, GetDevicePeriod, ac, &dp, NULL), S_OK);
        T_EQ(dp, 100000);
        T_EQ(mp, -1);
        T_EQ(COM_CALL(IAudioClient, GetDevicePeriod, ac, NULL, &mp), S_OK);
        T_EQ(mp, 30000);
        dp = mp = -1;
        T_EQ(COM_CALL(IAudioClient, GetDevicePeriod, ac, &dp, &mp), S_OK);
        T_EQ(dp, 100000);
        T_EQ(mp, 30000);

        /* The mix format as the bytes of a 40-byte extensible form, for the IsFormatSupported rows. */
        CALL(ole32, CoTaskMemFree, NULL);
        mf = NULL;
        T_EQ(COM_CALL(IAudioClient, GetMixFormat, ac, &mf), S_OK);
        memcpy(mix, mf, 40);
        CALL(ole32, CoTaskMemFree, mf);

        T_CASE("IAudioClient::IsFormatSupported: exclusive mode gives AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED and NULL *ppClosest");
        cm = (WAVEFORMATEX *)&clsid;
        T_EQ(COM_CALL(IAudioClient, IsFormatSupported, ac, 1, (const WAVEFORMATEX *)mix, &cm), AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED);
        T_TRUE(cm == NULL);
        T_EQ(COM_CALL(IAudioClient, IsFormatSupported, ac, 1, (const WAVEFORMATEX *)mix, NULL), AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED);
        T_CASE("IAudioClient::IsFormatSupported: NULL pFormat gives E_POINTER; a mode other than 0 or 1 gives E_INVALIDARG");
        T_EQ(COM_CALL(IAudioClient, IsFormatSupported, ac, 0, NULL, &cm), E_POINTER);
        T_TRUE(cm == NULL);
        T_EQ(COM_CALL(IAudioClient, IsFormatSupported, ac, 2, (const WAVEFORMATEX *)mix, NULL), E_INVALIDARG);
        T_CASE("IAudioClient::IsFormatSupported: shared mode with NULL ppClosestMatch gives E_POINTER");
        T_EQ(COM_CALL(IAudioClient, IsFormatSupported, ac, 0, (const WAVEFORMATEX *)mix, NULL), E_POINTER);
        T_CASE("IAudioClient::IsFormatSupported: the mix format, extensible or plain tag 3, gives S_OK and NULL *ppClosest");
        cm = (WAVEFORMATEX *)&clsid;
        T_EQ(COM_CALL(IAudioClient, IsFormatSupported, ac, 0, (const WAVEFORMATEX *)mix, &cm), S_OK);
        T_TRUE(cm == NULL);
        memset(b, 0, sizeof b);
        b[0] = 3; b[2] = 2; b[4] = 0x80; b[5] = 0xBB; b[8] = 0; b[9] = 0x77; b[10] = 0x01;   /* 48000 = 0xBB80 */
        b[12] = 8; b[14] = 32;
        cm = (WAVEFORMATEX *)&clsid;
        T_EQ(COM_CALL(IAudioClient, IsFormatSupported, ac, 0, (const WAVEFORMATEX *)b, &cm), S_OK);
        T_TRUE(cm == NULL);
        T_CASE("IAudioClient::IsFormatSupported: any other format gives S_FALSE and a new copy of the mix format in *ppClosest");
        memcpy(b, mix, 40); b[4] = 0x44; b[5] = 0xAC; b[6] = 0; b[7] = 0;   /* 44100 */
        cm = NULL;
        T_EQ(COM_CALL(IAudioClient, IsFormatSupported, ac, 0, (const WAVEFORMATEX *)b, &cm), S_FALSE);
        T_TRUE(cm != NULL);
        T_TRUE(memcmp(cm, mix, 40) == 0);
        CALL(ole32, CoTaskMemFree, cm);
        memcpy(b, mix, 40); b[2] = 1;                                                       /* one channel */
        cm = NULL;
        T_EQ(COM_CALL(IAudioClient, IsFormatSupported, ac, 0, (const WAVEFORMATEX *)b, &cm), S_FALSE);
        CALL(ole32, CoTaskMemFree, cm);
        memcpy(b, mix, 40); b[14] = 16;                                                     /* 16 bits */
        cm = NULL;
        T_EQ(COM_CALL(IAudioClient, IsFormatSupported, ac, 0, (const WAVEFORMATEX *)b, &cm), S_FALSE);
        CALL(ole32, CoTaskMemFree, cm);
        memcpy(b, mix, 40); b[0] = 1; b[16] = 0; b[17] = 0;                                 /* tag 1, PCM, no extension */
        cm = NULL;
        T_EQ(COM_CALL(IAudioClient, IsFormatSupported, ac, 0, (const WAVEFORMATEX *)b, &cm), S_FALSE);
        CALL(ole32, CoTaskMemFree, cm);
        memcpy(b, mix, 40); b[24] = 0x01;                                                   /* extensible, sub-format PCM */
        cm = NULL;
        T_EQ(COM_CALL(IAudioClient, IsFormatSupported, ac, 0, (const WAVEFORMATEX *)b, &cm), S_FALSE);
        CALL(ole32, CoTaskMemFree, cm);

        T_CASE("IAudioClient::Initialize has its body now (unit wasapi-stream): a NULL format is E_POINTER; a volume interface still traps");
        T_EQ(COM_CALL(IAudioClient, Initialize, ac, 0, 0, 0, 0, NULL, NULL), E_POINTER);
        T_EQ(COM_CALL(IAudioClient, Release, ac), 0);
        (void)hr;
    }
    T_EQ(COM_CALL(IMMDevice, Release, dv), 0);
    T_EQ(COM_CALL(IMMDeviceEnumerator, Release, en), 0);

    T_CASE("LoadLibraryA of xaudio2_7.dll gives a module");
    T_TRUE(CALL(kernel32, LoadLibraryA, "xaudio2_7.dll") != NULL);
}
