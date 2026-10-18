/* RT-STATUS: partial
 * RT-DOC:    none
 * RT-CASES:  the engine's one request (0, 10000, the callback engine+0x184fd0, 0, from engine+0x183c91) with the placeholder user selected: result 0, the request accepted and logged; nothing is written by the call itself; the profile is delivered once, from inside stand-in 5's next answer on the same thread: the callback is called as callback(0, record, 0, 0) with the posted record
 * RT-TRAPS:  stand-in 7 was called in a shape other than the one it answers; the per-frame call that would deliver the profile came on another thread
 */

/* STAND-IN 7 FOR EA'S SDK.  THIS IS NOT A SHIM AND NOT A PART OF WINDOWS.
 *
 * It is a row of rt_standin_table (rt/standin.h) that a program has only if it hands the row
 * to rt_init itself.  `gate3 run` does for GATE3_USER=placeholder only, unless
 * GATE3_NO_STANDIN_PROFILE=1 (gate3/run.c).  In every other program, and with the user
 * "nobody", the import is the stub that halts the run.
 *
 * What it is: the answer posted on issue 32 on 11 October 2026 before this file was written
 * (comment 6114036238, row 7, after the owner's words "go for the 2nd option ... try the made
 * up one first"; the record's fields in full: comment 6114239628, RT-DOC above) to the
 * engine's call
 *
 *     originsdk.dll!OriginGetProfile(0, 10000, engine+0x184fd0, 0)
 *
 * made at engine+0x183c91 on the main thread, in the frame in which stand-in 6 named the
 * placeholder user.  The answer: RESULT 0, "accepted".  The engine's callback is then called
 * ONCE, on the same thread, from inside stand-in 5's next answer (the kit's per-frame turn),
 * as callback(0, record, 0, 0), with a record in guest memory that holds, by offset:
 *
 *     +0x00  0                 (the callback does not read it)
 *     +0x08  1                 the made-up id (a 64-bit number; the engine shows it as EA-PID-1)
 *     +0x10  a pointer to the text "Pilot"            the name
 *     +0x18  a pointer to an empty text               (a second text; no use of it was found)
 *     +0x20  a pointer to an empty text               no country (the engine compares it with "JP")
 *     +0x28  0,  +0x29  0      two bytes about the account
 *     every other byte of its 0x40 bytes: 0
 *
 * IT IS A MADE-UP USER: the id is not the owner's and is taken from nowhere, the name is
 * made up, there is no country, no entitlement, no authorisation code.
 *
 * BUILT ONLY AFTER THE RUN STOOD THERE (docs/handoff/gate3-run-11.md, section 2):
 *     FATAL unimplemented import originsdk.dll!OriginGetProfile
 *       called from engine.dll+0x183c96   thread 0x1004 ""
 *       rcx=0000000000000000 rdx=0000000000002710 r8=<engine.dll+0x184fd0> r9=0000000000000000
 *
 * Where everything here comes from: THE ENGINE'S SIDE OF THE CALL ONLY.  OriginSDK.dll was
 * not read, disassembled, lifted or run.  The call site and the callback were read with
 * tools/scout/flow_show.py engine.dll 0x183c40 --all and 0x184fd0 --all (the handoff's section
 * 1 has what the callback reads, at which offset and what the engine does with each: it reads
 * only its second argument and nothing of the record beyond +0x29; it copies through the
 * three text pointers without testing them, so each points at a real text).  What EA's kit
 * passes in the callback's first, third and fourth argument is not knowable from the
 * engine's side and the callback reads none of them: 0 each.  The record's true size is not
 * knowable either: 0x40 bytes, zero beyond what is listed.
 *
 * THE STOPS.  Any other shape halts and nothing is counted: a call from anywhere but
 * engine+0x183c91, any argument other than (0, 10000, engine+0x184fd0, 0) in the whole
 * registers, a call while the user is "nobody", and A SECOND REQUEST (the post has the
 * callback called once; the engine asks once, when it first has a user id).  If the per-frame
 * call that would deliver comes on another thread than the request did, that halts too.
 *
 * The record and its three texts are one page of guest memory of their own (rt_vm_alloc, as
 * VirtualAlloc gives), never freed and not written again. */
#include <stdatomic.h>
#include <string.h>

#include "rt/shim.h"
#include "rt/standin.h"
#include "rt/vm.h"

uint32_t rt_standin7_test_callback_rva;

static _Atomic unsigned requests, deliveries;
static Ctx     *asked_on;               /* the thread of the accepted request */
static uint64_t asked_callback;         /* the engine's callback, as the request named it */

unsigned rt_standin7_requests(void)   { return atomic_load(&requests); }
unsigned rt_standin7_deliveries(void) { return atomic_load(&deliveries); }

SHIM(originsdk, DWORD, OriginGetProfile, ULONG_PTR first, ULONG_PTR timeout, ULONG_PTR callback, ULONG_PTR fourth)
{
    uint64_t ret;
    const RtModule *engine;
    const bool site = rt_standin_site(ctx, RT_STANDIN_7_RETURN_RVA, &ret, &engine);
    const uint32_t cb = rt_standin56_test_module ? rt_standin7_test_callback_rva : RT_STANDIN_7_CALLBACK_RVA;
    if (rt_standin_user != RT_STANDIN_USER_PLACEHOLDER || !site || first != 0 || timeout != RT_STANDIN_7_TIMEOUT_MS
        || callback != engine->base + cb || fourth != 0 || atomic_load(&requests) != 0)
        RT_UNIMPLEMENTED(ctx, "stand-in 7 was called in a shape other than the one it answers (the placeholder user's one request, "
                         "OriginGetProfile(0, %u, " RT_STANDIN_56_CALLER_MODULE "+%#x, 0) from " RT_STANDIN_56_CALLER_MODULE "+%#x, which "
                         "returns to +%#x): the user is %s, the return address is %#llx, the arguments are %#llx, %#llx, %#llx, %#llx, "
                         "requests accepted before this one: %u",
                         (unsigned)RT_STANDIN_7_TIMEOUT_MS, (unsigned)RT_STANDIN_7_CALLBACK_RVA, (unsigned)RT_STANDIN_7_CALL_RVA,
                         (unsigned)RT_STANDIN_7_RETURN_RVA, rt_standin_user == RT_STANDIN_USER_PLACEHOLDER ? "the placeholder" : "nobody",
                         (unsigned long long)ret, (unsigned long long)first, (unsigned long long)timeout, (unsigned long long)callback,
                         (unsigned long long)fourth, atomic_load(&requests));
    asked_on = ctx;
    asked_callback = callback;
    atomic_store(&requests, 1);
    rt_log(RT_CH_LOADER, RT_LL_WARN, "stand-in 7 answered: originsdk.dll!OriginGetProfile(0, %u, " RT_STANDIN_56_CALLER_MODULE "+%#x, 0) from "
           RT_STANDIN_56_CALLER_MODULE "+%#x: result 0, the request accepted; THE PLACEHOLDER USER's profile (made up: id 1, the name \""
           RT_STANDIN_7_PROFILE_NAME "\", no country, nothing else) will be handed to the engine's callback once, from inside stand-in 5's "
           "next answer; EA's SDK is not here and did not answer (issue 32, comments 6114036238 and 6114239628)",
           (unsigned)RT_STANDIN_7_TIMEOUT_MS, (unsigned)RT_STANDIN_7_CALLBACK_RVA, (unsigned)RT_STANDIN_7_CALL_RVA);
    return 0;
}

void rt_standin7_deliver(Ctx *ctx)
{
    if (atomic_load(&requests) == 0 || atomic_load(&deliveries) != 0) return;
    if (ctx != asked_on)
        RT_UNIMPLEMENTED(ctx, "stand-in 7: the per-frame call that would deliver the profile came on another thread than the request did");
    const uint64_t page = rt_vm_alloc(0, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    if (page == 0) RT_UNIMPLEMENTED(ctx, "stand-in 7: no page of guest memory for the profile's record");
    uint8_t *const rec = (uint8_t *)(uintptr_t)page;                        /* a fresh commit: every byte is 0 */
    const uint64_t name = page + RT_STANDIN_7_RECORD_SIZE, text2 = name + 0x10, country = text2 + 8, id = RT_STANDIN_PLACEHOLDER_USER_ID;
    memcpy((char *)(uintptr_t)name, RT_STANDIN_7_PROFILE_NAME, sizeof RT_STANDIN_7_PROFILE_NAME);
    memcpy(rec + RT_STANDIN_7_OFF_ID, &id, 8);
    memcpy(rec + RT_STANDIN_7_OFF_NAME, &name, 8);
    memcpy(rec + RT_STANDIN_7_OFF_TEXT2, &text2, 8);                        /* points at a 0 byte: an empty text */
    memcpy(rec + RT_STANDIN_7_OFF_COUNTRY, &country, 8);                    /* the same: no country */
    atomic_store(&deliveries, 1);                                           /* before the call: a per-frame call from inside the callback delivers nothing */
    const uint64_t r = GUEST_CALL(ctx, asked_callback, 0, page, 0, 0);
    rt_log(RT_CH_LOADER, RT_LL_WARN, "stand-in 7 delivered: the engine's profile callback (" RT_STANDIN_56_CALLER_MODULE "+%#x) was called "
           "once, from inside stand-in 5's answer, as callback(0, record, 0, 0); the record, made up: +0x08 the id %llu, +0x10 the name \""
           RT_STANDIN_7_PROFILE_NAME "\", +0x18 an empty text, +0x20 an empty text (no country), +0x28 and +0x29 the bytes 0 and 0, "
           "every other byte 0; the callback returned %#x; nothing more is ever called back",
           (unsigned)RT_STANDIN_7_CALLBACK_RVA, (unsigned long long)id, (unsigned)r);
}
