"""Rows of tools/swarm/make_shim_tasks.py for the Windows functions a recorded real start calls
and the runtime had neither a shim nor a row for (unit rt-census-rows).

The answers are the blocks of docs/reviews/census-rows.md, part 1 (a cloud unit read Microsoft's
pages and Wine's conformance tests for each function), with what this unit added from the
recording's arguments (the bcrypt calls) and from two more pages and one more test (raw input).
In every note: [MS] is the function's page on learn.microsoft.com, [W] a conformance test of
Wine (file and function named), [T] the recording (docs/findings/39-relay-trace.md).  Where a
block marks a case "not settled", or has Wine's implementation as its only source, the row
makes it a stop or says "this runtime's choice" with the reason.

`stage` is the order in which a run meets the functions (docs/handoff/rt-census-rows.md has
the table): A 2.860 s, B 4.9 to 5.7 s, C 9.060 s, D 24 s, E never on this machine or at
shutdown.  Every row needs unit rt-census-rows itself: its merge brings the types and the three
core pieces (rt/bcrypt.h, rt/rawinput.h, rt_thread_set_ideal_processor).

rows(N) gets the generator's module for its shared note blocks.
"""
import shim_rows_r3 as R3

UNIT = "rt-census-rows"

SRC = """\
- The answer below is a decision, case by case, from `docs/reviews/census-rows.md` (part 1):
  [MS] is the function's page, [W] a conformance test of Wine, [T] the recording of a real
  start. Do not read that review; the notes carry what you need. Write exactly these cases; a
  case the notes do not name is `RT_UNIMPLEMENTED` and goes in your reply.
"""
BCRYPT = SRC + """\
- The core of `rt/bcrypt.h` does all of it and its comment is the specification: the shim is
  one line, `return rt_bcrypt_...(the arguments in order);`. The functions return an `NTSTATUS`
  and NEVER touch last error: the test presets it (`CALL(kernel32, SetLastError, 0x1234)`) and
  checks it is still 0x1234 after a success and after a failure.
- Where the core's comment says a case STOPS, the shim adds no guard for it. List each such
  case that your test reaches in `RT-TRAPS` as `core: <case>` (the status is then `partial`)
  and prove each listed one with a `T_TRAPS(..., "<core function>: not implemented")`: list and
  prove exactly the stops the test list below names, and name the others in your reply.
- A case the core's comment answers and the test list does not name (another property, another
  mode) needs no guard and no test: the shim passes everything through.
- A test makes what it needs through the core (include `rt/bcrypt.h` in the test), never through
  another shim: `BCRYPT_ALG_HANDLE aes; rt_bcrypt_open_provider(&aes, u"AES", NULL, 0);`, and a
  key with `UCHAR object[RT_BCRYPT_AES_OBJECT_LENGTH], secret[16]; BCRYPT_KEY_HANDLE key;
  rt_bcrypt_generate_symmetric_key(aes, &key, object, sizeof object, secret, 16, 0);`. Wide
  strings are written `u"AES"` (a `WCHAR` is 16 bits here). `runtime/tests/core/bcrypt.c` shows
  every call (read its first 60 lines only).
"""


def rows(N):
    out = []

    def row(**kw):
        kw.setdefault("needs", [UNIT])
        kw.setdefault("issue", 30)
        out.append(kw)

    # ---- raw input (user32, reached through GetProcAddress: runtime/tools/imports_extra.def) ----
    row(id="raw-input-register", lib="user32", members=["RegisterRawInputDevices"], stage="A",
        header="rt/rawinput.h", core=["rt_raw_input_register"], model="SetThreadAffinityMask",
        protos=["BOOL RegisterRawInputDevices(PCRAWINPUTDEVICE pRawInputDevices, UINT uiNumDevices, UINT cbSize)"],
        notes=SRC + N.CORE_STOPS + """\
- One core call: `return rt_raw_input_register(pRawInputDevices, uiNumDevices, cbSize);`. The
  core sets last error when it fails and leaves it alone on success; the shim sets nothing.
  The game calls this once, `RegisterRawInputDevices(devices, 1, 16)` at inputsystem+0x6c77
  ([T], where it returned 1); it is not an import: the input system fetches it with
  `GetProcAddress`, and today gets NULL and leaves raw input off.
- The machine has a keyboard and a mouse and no game controller, and no input arrives in this
  runtime yet: the registration is recorded and nothing is ever posted for it. Status
  `partial` (the core's stops).
- Include `rt/rawinput.h` in the shim and the test (it brings `RAWINPUTDEVICE` and the
  `RIDEV_` values). A test that needs a window makes it through the core, as
  `runtime/tests/core/rawinput.c` does in its first 20 lines (copy them; name the class
  differently).
- Tests ([W] `user32/tests/input.c`, `test_RegisterRawInputDevices`, for the first four):
  `(NULL, 0, 0)` and a good list of two with size 0 are FALSE with `ERROR_INVALID_PARAMETER`
  after a preset 0xdeadbeef; two entries, usage page 1 with usages 5 and 4, flags 0, no
  target, size 16: TRUE, last error still 0xdeadbeef, and `rt_raw_input_count()` is 2; the same
  two with `RIDEV_INPUTSINK` and no target: FALSE with `ERROR_INVALID_PARAMETER`; the same two
  with `RIDEV_INPUTSINK` and a window as target: TRUE, and `rt_raw_input_find(1, 5, &d, NULL)`
  gives `RIDEV_INPUTSINK` and the window; one
  entry (page 1, usage 2, flags 0, the window) with size 16, the game's shape: TRUE; the
  stops, listed in `RT-TRAPS` as `core: RIDEV_DEVNOTIFY` and `core: RIDEV_REMOVE with a target
  window`: flags `RIDEV_DEVNOTIFY` (0x2000) with no target, proved with `T_TRAPS(...,
  "rt_raw_input_register: not implemented: flags 0x2000")`, and `RIDEV_REMOVE` with the window
  as target, proved with `T_TRAPS(..., "rt_raw_input_register: not implemented: RIDEV_REMOVE
  with a target window")` (the text a `T_TRAPS` looks for is a piece of the core's report, not
  the `RT-TRAPS` entry). The core's other stops need no test here; name them in your reply.""")
    row(id="raw-input-data", lib="user32", members=["GetRawInputData"], stage="D",
        header=None, core=[], model="user32/GetRawInputDeviceList",
        protos=["UINT GetRawInputData(HRAWINPUT hRawInput, UINT uiCommand, LPVOID pData, PUINT pcbSize, "
                "UINT cbSizeHeader)"],
        notes=SRC + """\
- An `HRAWINPUT` is the `lParam` of a `WM_INPUT` message ([MS]), and this runtime never posts
  one (no input arrives yet), so no handle exists and every value is invalid: `return
  FAIL(ctx, ERROR_INVALID_HANDLE, (UINT)-1);` ([MS]: "If there is an error, the return value
  is (UINT)-1"; [W] `user32/tests/input.c`, `test_GetRawInputData`, asserts -1 with
  `ERROR_INVALID_HANDLE` for `GetRawInputData(NULL, RID_INPUT, NULL, &size,
  sizeof(RAWINPUTHEADER))`; that a value which is not NULL answers the same is this runtime's
  choice: none was ever given out). Nothing is written through `pData` or `pcbSize`. Status
  `partial`. No log line.
- Stops, because the order in which Windows checks them against the handle is not settled:
  `pcbSize` NULL; `cbSizeHeader` other than 24 (`sizeof(RAWINPUTHEADER)`); `uiCommand` other
  than `RID_INPUT` or `RID_HEADER`. Check them first, each with a message that starts with the
  `RT-TRAPS` text.
- Include `rt/win/rawinput.h` (the types and `RID_INPUT` 0x10000003, `RID_HEADER` 0x10000005).
  It is not an import: the input system fetches it with `GetProcAddress` (inputsystem+0x7855)
  and calls it once per `WM_INPUT` (119,601 times in the recording, from 24.125 s).
- Tests: the [W] call above after a preset 0xdeadbeef; `(HRAWINPUT)0x1234` with `RID_HEADER`, a
  buffer of 48 bytes preset to 0xA5 and a size preset to 48: -1, the code, the buffer and the
  size unchanged; the three stops.""")

    # ---- winmm: the microphone's mixer line (engine, 4.872 s) -----------------------------------
    row(id="mixer-line", lib="winmm", members=["mixerGetLineInfoA", "mixerGetLineControlsA"], stage="B",
        header=None, core=[], model="winmm/mixerGetDevCapsA",
        protos=["MMRESULT mixerGetLineInfoA(HMIXEROBJ hmxobj, LPMIXERLINEA pmxl, DWORD fdwInfo)",
                "MMRESULT mixerGetLineControlsA(HMIXEROBJ hmxobj, LPMIXERLINECONTROLSA pmxlc, DWORD fdwControls)"],
        notes=SRC + R3.NOTHING_THERE + """\
- What is not there: a mixer device (`mixerGetNumDevs` answers 0 and `mixerOpen` fails, both
  written). The result is returned and last error is never set. The flags' low four bits are
  the query, their top four bits (`MIXER_OBJECTF_TYPEMASK`, `rt/win/mm.h`) the kind of `hmxobj`.
  Both functions, in this order:
  1. a query value outside the function's own (`mixerGetLineInfoA`: 0 to 4;
     `mixerGetLineControlsA`: 0 to 2; mask `MIXER_GETLINEINFOF_QUERYMASK`), or any bit outside
     the two masks: `RT_UNIMPLEMENTED` with the flags ([W] `winmm/tests/mixer.c` asserts
     `MMSYSERR_INVALFLAG` on an OPEN mixer; whether Windows looks at the object first is not
     settled).
  2. kind `MIXER_OBJECTF_MIXER` (0): `return MMSYSERR_BADDEVICEID;` for every identifier ([MS]
     "The hmxobj parameter specifies an invalid device identifier": with no device every
     identifier is; [W] `mixer.c` asserts it for an identifier past the last in the sibling
     functions).
  3. kind `MIXER_OBJECTF_HMIXER` (0x80000000): `return MMSYSERR_INVALHANDLE;` ([MS] "The hmxobj
     parameter specifies an invalid handle": no handle was ever given out).
  4. any other kind (a wave, MIDI or aux identifier or handle): `RT_UNIMPLEMENTED` with the
     flags (Windows answers `MMSYSERR_NODRIVER` when the object exists and has no mixer;
     whether this machine has such an object is the sound unit's decision).
  Nothing is written through `pmxl` / `pmxlc`. Log line (cases 2 and 3): `<Name>: there is no
  mixer device`. `RT-STATUS` is `partial`, not `absent`, because of the two stops (the build
  refuses `absent` with an `RT-TRAPS` list); the stops' messages are `"<Name> flags 0x%08X: a
  query value the function does not have"` and `"<Name> flags 0x%08X: an object that is not a
  mixer"`, and `RT-TRAPS` is `a query value the function does not have; an object that is not a
  mixer`. In `mixerGetLineControlsA` the query mask is `MIXER_GETLINECONTROLSF_QUERYMASK`.
  Include `rt/win/mm.h` in both shims and both tests (the masks, the kinds and the pointer
  types are there; the model shim needs none of them and does not include it).
- Tests, for each function: identifier 0 and 5 with flags 0 (`MMSYSERR_BADDEVICEID`); a handle
  `(HMIXEROBJ)0x1234` with flags 0x80000000 (`MMSYSERR_INVALHANDLE`); a buffer of 200 bytes
  preset to 0xA5 keeps them in both; last error preset to 0x1234 is left alone; the log line;
  the stops for flags 0x0000000F and for flags 0x10000000.""")

    # ---- kernel32 -----------------------------------------------------------------------------------
    row(id="thread-ideal", lib="kernel32", members=["SetThreadIdealProcessor"], stage="B",
        header="rt/thread.h", core=["rt_thread_set_ideal_processor"], model="SetThreadAffinityMask",
        protos=["DWORD SetThreadIdealProcessor(HANDLE hThread, DWORD dwIdealProcessor)"],
        notes=SRC + N.CORE_STOPS + """\
- One core call: `return rt_thread_set_ideal_processor(hThread, dwIdealProcessor);`. The core's
  comment is the specification; it sets last error when it fails and leaves it alone on
  success. Status `advisory` if you list no stop, `partial` with the core's stop listed (list
  it: `core: a processor the machine does not report`).
- The game ([T]): vstdlib asks processors 0, 1 and 2 for three threads, the video decoder
  processor 2 for four; every call returned 0 there.
- Tests (`(HANDLE)(intptr_t)-2` is the calling thread; the machine's count is
  `rt_machine()->cpu_count` of `rt/machine.h`, at least 1): processor 0 returns 0; with a count
  above 1, `count - 1` returns 0 and then 0 returns `count - 1`; 64 returns the stored value
  twice; 65 is `(DWORD)-1` with `ERROR_INVALID_PARAMETER`; NULL is `(DWORD)-1` with
  `ERROR_INVALID_HANDLE`; success leaves a preset last error alone; the stop for processor
  `count` when the count is below 64 (`"rt_thread_set_ideal_processor: not implemented"`).""")

    # ---- bcrypt: the network channel's keys (engine, thread of the first sockets, 9.060 s) --------
    row(id="bcrypt-provider", lib="bcrypt", members=["BCryptOpenAlgorithmProvider"], stage="C",
        header="rt/bcrypt.h", core=["rt_bcrypt_open_provider"], model="bcrypt/BCryptGenRandom",
        protos=["NTSTATUS BCryptOpenAlgorithmProvider(BCRYPT_ALG_HANDLE *phAlgorithm, LPCWSTR pszAlgId, "
                "LPCWSTR pszImplementation, ULONG dwFlags)"],
        notes=BCRYPT + """\
- `return rt_bcrypt_open_provider(phAlgorithm, pszAlgId, pszImplementation, dwFlags);`. The
  game's calls ([T]): `(&h, L"AES", NULL, 0)` five times at engine+0x2155b6 and `(&h, L"RNG",
  NULL, 0)` once at engine+0x215472, each returning 0.
- Tests: `u"AES"` with NULL and 0 is `STATUS_SUCCESS` and stores a handle for which
  `RT_BCRYPT_IS_ALG` is true; a second call stores a different one; `u"RNG"` likewise;
  `u"AES"` with `u"Microsoft Primitive Provider"`; last error; the stops for `u"SHA256"`, for
  flags 8 and for a NULL `phAlgorithm`.""")
    row(id="bcrypt-property", lib="bcrypt", members=["BCryptGetProperty", "BCryptSetProperty"], stage="C",
        header="rt/bcrypt.h", core=["rt_bcrypt_get_property", "rt_bcrypt_set_property"], model="bcrypt/BCryptGenRandom",
        protos=["NTSTATUS BCryptGetProperty(BCRYPT_HANDLE hObject, LPCWSTR pszProperty, PUCHAR pbOutput, "
                "ULONG cbOutput, ULONG *pcbResult, ULONG dwFlags)",
                "NTSTATUS BCryptSetProperty(BCRYPT_HANDLE hObject, LPCWSTR pszProperty, PUCHAR pbInput, "
                "ULONG cbInput, ULONG dwFlags)"],
        notes=BCRYPT + """\
- `return rt_bcrypt_get_property(hObject, pszProperty, pbOutput, cbOutput, pcbResult, dwFlags);`
  and `return rt_bcrypt_set_property(hObject, pszProperty, pbInput, cbInput, dwFlags);`. The
  game's calls ([T]): `BCryptSetProperty(hAes, L"ChainingMode", <a mode's name>, 8, 0)` at
  engine+0x2155fd (the 8 is the game's own: the core's comment says how it is read) and
  `BCryptGetProperty(hAes, L"ObjectLength", &n, 4, &cb, 0)` at engine+0x21563e.
- Tests, get: `u"ObjectLength"` stores 654 and 4; `u"BlockLength"` 16 and 4; `u"ChainingMode"`
  with 64 bytes stores `u"ChainingModeCBC"` (compare 32 bytes) and 64, with 63 bytes is
  `STATUS_BUFFER_TOO_SMALL` with 64 and a buffer preset to 0xA5 unchanged; on a key
  `u"KeyStrength"` stores 128 and 4. Set: `u"ChainingMode"` to `(PUCHAR)u"ChainingModeGCM"` with
  `cbInput` 32 and again with `cbInput` 8 is `STATUS_SUCCESS`, and the getter then gives that
  name; on a key it is `STATUS_NOT_SUPPORTED`. Last error. The stops: get of
  `u"HashDigestLength"`; set of `(PUCHAR)u"ChainingModeECB"`.""")
    row(id="bcrypt-key", lib="bcrypt", members=["BCryptGenerateSymmetricKey", "BCryptDestroyKey"], stage="C",
        header="rt/bcrypt.h", core=["rt_bcrypt_generate_symmetric_key", "rt_bcrypt_destroy_key"],
        model="bcrypt/BCryptGenRandom",
        protos=["NTSTATUS BCryptGenerateSymmetricKey(BCRYPT_ALG_HANDLE hAlgorithm, BCRYPT_KEY_HANDLE *phKey, "
                "PUCHAR pbKeyObject, ULONG cbKeyObject, PUCHAR pbSecret, ULONG cbSecret, ULONG dwFlags)",
                "NTSTATUS BCryptDestroyKey(BCRYPT_KEY_HANDLE hKey)"],
        notes=BCRYPT + """\
- `return rt_bcrypt_generate_symmetric_key(hAlgorithm, phKey, pbKeyObject, cbKeyObject, pbSecret,
  cbSecret, dwFlags);` and `return rt_bcrypt_destroy_key(hKey);`. The game's calls ([T]):
  `BCryptGenerateSymmetricKey(hAes, &hKey, object, 0x28e, secret, 16, 0)` six times at
  engine+0x215980 and `BCryptDestroyKey(hKey)` once at engine+0x215946.
- Tests: a 16-byte secret with an object buffer of 654 bytes is `STATUS_SUCCESS`, stores a
  handle for which `RT_BCRYPT_IS_KEY` is true, and leaves the object buffer (preset to 0xA5)
  unchanged; 24 and 32 bytes; a NULL provider is `STATUS_INVALID_HANDLE`, secrets of 1 and 17
  bytes `STATUS_INVALID_PARAMETER`, an object buffer of 653 bytes `STATUS_BUFFER_TOO_SMALL`, and
  a `*phKey` preset to `(BCRYPT_KEY_HANDLE)0x1111` keeps its value in all four; destroy of a key
  is `STATUS_SUCCESS`, of NULL `STATUS_INVALID_HANDLE`; last error. The stops: a secret of 20
  bytes; destroy of the same key a second time.""")
    row(id="bcrypt-export", lib="bcrypt", members=["BCryptExportKey"], stage="C",
        header="rt/bcrypt.h", core=["rt_bcrypt_export_key"], model="bcrypt/BCryptGenRandom",
        protos=["NTSTATUS BCryptExportKey(BCRYPT_KEY_HANDLE hKey, BCRYPT_KEY_HANDLE hExportKey, LPCWSTR pszBlobType, "
                "PUCHAR pbOutput, ULONG cbOutput, ULONG *pcbResult, ULONG dwFlags)"],
        notes=BCRYPT + """\
- `return rt_bcrypt_export_key(hKey, hExportKey, pszBlobType, pbOutput, cbOutput, pcbResult,
  dwFlags);`. The game's calls ([T]): `BCryptExportKey(hKey, NULL, L"OpaqueKeyBlob", buffer,
  0x230, &cb, 0)` six times at engine+0x2159c0.
- Tests, with a key made of the 16 bytes 0x10 to 0x1F: `u"OpaqueKeyBlob"` into 0x230 bytes
  preset to 0xA5 is `STATUS_SUCCESS`, stores 28, writes `RT_BCRYPT_OPAQUE_MAGIC`, 1, 16 (three
  `ULONG`s) and the 16 bytes, and leaves the other 532 bytes; `u"KeyDataBlob"` into 28 bytes
  writes 0x4d42444b, 1, 16 and the 16 bytes; either type into 27 bytes is
  `STATUS_BUFFER_TOO_SMALL` with 28 stored and nothing written; last error. The stops: blob
  type `u"AesWrapKeyBlob"`; an export key (pass the key itself); a NULL `pbOutput`.""")

    # ---- hid, setupapi: never reached on this machine (the interface list is empty) ---------------
    row(id="hid-attributes", lib="hid", members=["HidD_GetAttributes"], stage="E",
        header=None, core=[], model="GetTickCount",
        protos=["BOOLEAN HidD_GetAttributes(HANDLE HidDeviceObject, PHIDD_ATTRIBUTES Attributes)"],
        notes=SRC + R3.TRAP + """\
- This machine has no HID collection: `SetupDiGetClassDevsA` gives the empty set and
  `SetupDiEnumDeviceInterfaces` ends the game's loop at index 0, so no device path exists and
  no handle a caller could pass is a HID device's. [MS] says only "TRUE if succeeds; otherwise
  FALSE" and names no last error, so a made-up failure would be a guess: the whole body is
  `RT_UNIMPLEMENTED(ctx, "HidD_GetAttributes on handle %p: this machine has no HID collection",
  HidDeviceObject);` and `(void)Attributes;` before it. `RT-TRAPS: every call`.
- Include `rt/win/devices.h` (`HIDD_ATTRIBUTES`). Test: one `T_TRAPS` with NULL and a
  structure, text `"this machine has no HID collection"`.""")
    row(id="setupdi-detail", lib="setupapi", members=["SetupDiGetDeviceInterfaceDetailA"], stage="E",
        header=None, core=[], model="setupapi/SetupDiEnumDeviceInterfaces",
        protos=["BOOL SetupDiGetDeviceInterfaceDetailA(HDEVINFO DeviceInfoSet, "
                "PSP_DEVICE_INTERFACE_DATA DeviceInterfaceData, "
                "PSP_DEVICE_INTERFACE_DETAIL_DATA_A DeviceInterfaceDetailData, DWORD DeviceInterfaceDetailDataSize, "
                "PDWORD RequiredSize, LPVOID DeviceInfoData)"],
        notes=SRC + """\
- The one device information set this runtime has is the empty one, `RT_HDEVINFO_EMPTY`
  (`rt/win/devices.h`), and no interface was ever handed out of it. In this order:
  1. `DeviceInfoSet` is not `RT_HDEVINFO_EMPTY` (NULL and `INVALID_HANDLE_VALUE` among them):
     `return FAIL(ctx, ERROR_INVALID_HANDLE, FALSE);` ([W] `setupapi/tests/devinst.c`,
     `test_device_iface_detail`, asserts it for NULL with every other argument NULL).
  2. `DeviceInterfaceData` NULL: `return FAIL(ctx, ERROR_INVALID_PARAMETER, FALSE);` ([W] the
     same function, on a fresh set).
  3. any interface structure: `RT_UNIMPLEMENTED(ctx, "SetupDiGetDeviceInterfaceDetailA: an
     interface this set never gave out (...)", ...)` (what Windows answers for a structure it
     does not know is not asserted by a test).
  Nothing is written through any pointer; no log line. Status `partial`; `RT-TRAPS: an
  interface this set never gave out` (a piece of the stop's message, which is what the check
  and the test's `T_TRAPS` look for). `DeviceInfoData` is
  `LPVOID` here (the page's `PSP_DEVINFO_DATA`, never read).
- Tests: NULL and `INVALID_HANDLE_VALUE` as the set with everything else NULL, after a preset
  0x1234 (`ERROR_INVALID_HANDLE`); the empty set with `DeviceInterfaceData` NULL
  (`ERROR_INVALID_PARAMETER`); in both a detail buffer of 64 bytes and a `RequiredSize` preset
  to 0xA5 bytes keep them; the stop for the empty set with a zeroed `SP_DEVICE_INTERFACE_DATA`
  whose `cbSize` is 32.""")

    # ---- dsound: a proposal for the orchestrator ------------------------------------------------------
    row(id="dsound-capture", lib="dsound", members=["DirectSoundCaptureCreate"], stage="B",
        hold="orchestrator: dsound.dll is not a module of the runtime (runtime/modules.def), so the engine's "
             "LoadLibrary gets nothing today, which no Windows 7 machine does; releasing this row means adding "
             "`system dsound.dll` at the end of modules.def in the same merge (docs/handoff/rt-census-rows.md)",
        header=None, core=[], model="winmm/mixerGetDevCapsA",
        protos=["HRESULT DirectSoundCaptureCreate(LPCGUID pcGuidDevice, LPVOID *ppDSC, LPUNKNOWN pUnkOuter)"],
        notes=SRC + R3.NOTHING_THERE + """\
- What is not there: a sound capture device (a microphone). The engine fetches this function
  with `GetProcAddress` and calls it once at engine+0xbff8 (4.707 s, [T]). The prototype is
  Wine's public `include/dsound.h` (the DirectSound reference page was not reachable);
  `ppDSC` is `LPVOID *` here (the header's `LPDIRECTSOUNDCAPTURE *`).
  1. `ppDSC` NULL: `return DSERR_INVALIDPARAM;` ([W] `dsound/tests/capture.c`, `test_capture`).
  2. `pUnkOuter` not NULL: `RT_UNIMPLEMENTED` (aggregation: not read).
  3. `pcGuidDevice` NULL: store NULL in `*ppDSC` and `return DSERR_NODRIVER;` ([W] the same
     file accepts `DSERR_NODRIVER` for the default device on a machine without one; that
     `*ppDSC` becomes NULL is this runtime's choice, the safe value for a caller that tests
     the pointer: no test asserts it).
  4. any GUID: `RT_UNIMPLEMENTED` with its first 32 bits (the two default-capture GUIDs would
     answer as 3, and their values are not in `rt/win/`).
  Constants in `rt/win/mm.h`. Log line (case 3): `DirectSoundCaptureCreate: there is no sound
  capture device`. Last error is never touched. `RT-STATUS` is `partial` (the two stops: the
  build refuses `absent` with an `RT-TRAPS` list).
- Tests: case 1; case 3 with a pointer preset to `(LPVOID)0x1111` (becomes NULL, the result,
  the log line, a preset last error unchanged); the stops for an outer object and for a GUID.""")
    return out
