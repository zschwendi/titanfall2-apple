#include "rt/test.h"
#include "rt/log.h"
#include "rt/module.h"
#include "rt/win/mm.h"

TEST(dsound, DirectSoundCaptureCreate)
{
    HMODULE h;
    void *out;
    HRESULT hr;

    T_CASE("LoadLibraryA(\"dsound.dll\") gives a module");
    h = CALL(kernel32, LoadLibraryA, "dsound.dll");
    T_TRUE(h != NULL);

    T_CASE("GetProcAddress of DirectSoundCaptureCreate is not NULL");
    T_TRUE(CALL(kernel32, GetProcAddress, h, "DirectSoundCaptureCreate") != 0);

    T_CASE("returns DSERR_NODRIVER and leaves the out-pointer NULL");
    out = (void *)(uintptr_t)1;
    hr = CALL(dsound, DirectSoundCaptureCreate, NULL, (void **)&out, NULL);
    T_EQ(hr, DSERR_NODRIVER);
    T_TRUE(out == NULL);

    T_CASE("logs once that there is no capture device");
    T_TRUE(T_LOGGED(RT_CH_IMPORT, "DirectSoundCaptureCreate: there is no capture device (the host's microphone is never opened)"));

    T_CASE("a NULL out-pointer returns E_INVALIDARG");
    hr = CALL(dsound, DirectSoundCaptureCreate, NULL, NULL, NULL);
    T_EQ(hr, E_INVALIDARG);
}
