/* RT-STATUS: partial
 * RT-DOC:    none
 * RT-CASES:  the observed call (setting 1, a buffer, a pointer to a size of 0x40): result 0 and the 11 bytes of the text "production" with its terminating zero written at the buffer; the size and every byte after the eleventh are left as they were; a size of exactly 11 is answered; one line is logged for each answer; setting 2 from the engine's call at engine+0x183ac2 with the placeholder user: result 0 and an empty text written at the buffer, the size left as it is, the first answer logged and the rest counted
 * RT-TRAPS:  a setting number that is not 1 (2 and 3 among them); a NULL buffer; a NULL pointer to the size; a size under 11; stand-in 3 was called for setting 2 in a shape other than the one it answers
 */

/* STAND-IN 3 FOR EA'S SDK.  THIS IS NOT A SHIM AND NOT A PART OF WINDOWS.
 *
 * It is not in the runtime's shim table (rt_shim_table): it is a row of rt_standin_table
 * (rt/standin.h), and a program has it only if it hands that row to rt_init itself.  `gate3
 * run` does, unless GATE3_NO_STANDIN_SETTING_ENVIRONMENT=1 (gate3/run.c).  In every other
 * program, and for every import of originsdk.dll that has no stand-in, the import is the stub
 * that halts the run.
 *
 * What it is: the one answer, posted on issue 32 on 11 October 2026 before this file was
 * written (RT-DOC above; the owner's rule is the plan's answer 2.1), to the engine's call
 *
 *     originsdk.dll!OriginGetSettingSync(1, <a 64-byte buffer>, <a pointer to a size of 0x40>)
 *
 * made at engine+0x1837ed on the main thread (finding 37, step 30; docs/handoff/gate3-run-6.md,
 * section 4).  The answer is: result 0, and the text "production" with its terminating zero
 * (11 bytes) written at the buffer; the size slot is left as it is.  By the engine's own error
 * text for a failure of this call ("Failed to get Origin environment setting") setting 1 is
 * "the Origin environment", and `production` is the word the engine itself compares the answer
 * with before it marks itself RETAIL: the name of the service environment a retail install
 * talks to.  That is a fact of the install.  It is no identity, no user or persona id, no
 * entitlement, no authorisation code and nothing of an account.  IT IS NOT MEASURED: the
 * recording cannot show what the real SDK wrote there.
 *
 * ONLY SETTING 1.  The engine asks two more settings, numbers 2 and 3, from engine+0x183a80
 * (each into a 256-byte buffer, each compared with the text "true"); what they are is not
 * known from the engine's side and nobody has decided an answer.  Every setting number but 1
 * stops here, with the three arguments printed.
 *
 * Where everything here comes from: THE ENGINE'S SIDE OF THE CALL ONLY.  OriginSDK.dll was not
 * read, disassembled, lifted or run.  The engine's code around the call (engine+0x1837d3 to
 * +0x183851; tools/scout/flow_show.py engine.dll 0x183780) passes 1 in rcx (`mov ecx, 1`), the
 * address of its buffer at .data+0x1397a304 in rdx and the address of a stack slot in r8,
 * which it has just set to 0x40 with a 64-bit store; afterwards it tests eax (not 0: its own
 * fatal error), compares the buffer with "production" and copies the buffer's text into a
 * string variable.  It does not read the size slot again.
 *
 * The types of the three arguments are not known from that side.  The first two instructions
 * define the whole 64-bit register, so the setting number is compared as a whole register;
 * the size is read as the 64 bits the engine stores.  The fourth register is not an argument
 * as far as the engine's code shows and is not read.
 *
 * It reads its three arguments and the size, and writes 11 bytes at the buffer.
 *
 * THE MEASUREMENT SWITCH.  rt_standin3_leave_buffer (rt/standin.h), false unless a program
 * sets it, makes this function answer result 0 and WRITE NOTHING: the other candidate the
 * post names and rejects ("an empty buffer also lets the engine go on, but as 'not
 * production', a path no retail start takes").  It exists for one measurement the post left
 * open (where does such a run stop, and what differs before that) and is not a way to run:
 * `gate3 run` sets it only for GATE3_MEASURE_ONLY_NOT_A_WAY_TO_RUN_SETTING1_LEFT_EMPTY=1 and
 * says so at the top of the summary.  The log line of each answer says which of the two was
 * given. */
#include <stdatomic.h>
#include <string.h>

#include "rt/shim.h"
#include "rt/standin.h"

bool rt_standin3_leave_buffer;

/* THE SECOND CASE: SETTING 2, "IS EA'S OVERLAY ON" (issue 32, comment 6114438848, posted on 11
 * October 2026 before this was written, after the run with stand-ins 6 and 7 stood at the
 * call: docs/handoff/gate3-run-11.md, sections 2b and 3).  The engine's function
 * engine+0x183a80 asks setting 2 at +0x183ac2 (returning to +0x183ac7) with a 256-byte buffer
 * on its stack that it does not clear and a pointer to a size it set to 0x100; it ignores
 * the result and compares the buffer with the text "true"; only if it is "true" does it ask
 * setting 3.  The posted answer: RESULT 0 AND AN EMPTY TEXT (ONE ZERO BYTE) WRITTEN AT THE
 * BUFFER, the size left as it is: "the overlay is not on", which is true here: there is no
 * overlay.  A fact of the install, nothing of an account.
 *
 * It answers ONLY the call from engine+0x183ac2, with setting 2, a non-NULL buffer and a size
 * of at least 1, AND ONLY FOR THE PLACEHOLDER USER: the engine reaches that call only behind
 * a user id (engine+0x183c40, +0x183f40, +0x1842e0), so with the user "nobody" it never comes
 * and stays the stop it was.  Setting 3, every other setting number and every other call
 * site stay stops.  The call may come in many frames: the first answer is logged, the rest
 * are counted (rt_standin3_setting2_answers; `gate3 run` prints the count at every end). */
static _Atomic unsigned long long setting2_answers;

unsigned long long rt_standin3_setting2_answers(void) { return atomic_load(&setting2_answers); }

SHIM(originsdk, DWORD, OriginGetSettingSync, ULONG_PTR setting, char *buffer, ULONG64 *size)
{
    static const char text[11] = "production";            /* ten letters and the terminating zero */
    if (setting == 2) {                                   /* THE SECOND CASE (above) */
        uint64_t ret;
        const RtModule *engine;
        if (!rt_standin_site(ctx, RT_STANDIN_3_SETTING2_RETURN_RVA, &ret, &engine) || rt_standin_user != RT_STANDIN_USER_PLACEHOLDER
            || buffer == NULL || size == NULL || *size < 1)
            RT_UNIMPLEMENTED(ctx, "stand-in 3 was called for setting 2 in a shape other than the one it answers (the placeholder user, the "
                             "engine's call at " RT_STANDIN_56_CALLER_MODULE "+%#x, which returns to +%#x, a non-NULL buffer, a non-NULL "
                             "pointer to a size of 1 or more): the user is %s, the return address is %#llx, buffer %#llx, pointer to the "
                             "size %#llx", (unsigned)RT_STANDIN_3_SETTING2_CALL_RVA, (unsigned)RT_STANDIN_3_SETTING2_RETURN_RVA,
                             rt_standin_user == RT_STANDIN_USER_PLACEHOLDER ? "the placeholder" : "nobody", (unsigned long long)ret,
                             (unsigned long long)(uintptr_t)buffer, (unsigned long long)(uintptr_t)size);
        buffer[0] = 0;
        if (atomic_fetch_add(&setting2_answers, 1) == 0)
            rt_log(RT_CH_LOADER, RT_LL_WARN, "stand-in 3 answered: originsdk.dll!OriginGetSettingSync(2, %#llx, size %#llx) from "
                   RT_STANDIN_56_CALLER_MODULE "+%#x: result 0, an empty text (one zero byte) written at the buffer, the size left as it is "
                   "(\"the overlay is not on\": there is no overlay here); this is its first answer for setting 2: the answers after "
                   "this one are counted and not logged; EA's SDK is not here and did not answer (issue 32, comment 6114438848)",
                   (unsigned long long)(uintptr_t)buffer, (unsigned long long)*size, (unsigned)RT_STANDIN_3_SETTING2_CALL_RVA);
        return 0;
    }
    if (setting != 1 || buffer == NULL || size == NULL)
        RT_UNIMPLEMENTED(ctx, "stand-in 3 was called with a shape other than the one it answers (setting 1, a non-NULL buffer, a "
                         "non-NULL pointer to a size of 11 or more): setting %#llx, buffer %#llx, pointer to the size %#llx",
                         (unsigned long long)setting, (unsigned long long)(uintptr_t)buffer, (unsigned long long)(uintptr_t)size);
    if (*size < sizeof text)
        RT_UNIMPLEMENTED(ctx, "stand-in 3 was called with a size of %#llx, under the 11 bytes of its answer: setting %#llx, buffer "
                         "%#llx, pointer to the size %#llx", (unsigned long long)*size, (unsigned long long)setting,
                         (unsigned long long)(uintptr_t)buffer, (unsigned long long)(uintptr_t)size);
    if (rt_standin3_leave_buffer) {
        rt_log(RT_CH_LOADER, RT_LL_WARN, "stand-in 3 answered: originsdk.dll!OriginGetSettingSync(1, %#llx, size %#llx): result 0 and "
               "NOTHING WRITTEN, the buffer left as the engine had it: THE MEASUREMENT SWITCH IS ON, this is not the posted answer "
               "and not a way to run; EA's SDK is not here and did not answer (issue 32)",
               (unsigned long long)(uintptr_t)buffer, (unsigned long long)*size);
        return 0;
    }
    memcpy(buffer, text, sizeof text);
    rt_log(RT_CH_LOADER, RT_LL_WARN, "stand-in 3 answered: originsdk.dll!OriginGetSettingSync(1, %#llx, size %#llx): result 0, the 11 "
           "bytes of \"production\" written at the buffer, the size left as it is (\"the call worked; the Origin environment is "
           "production\"); EA's SDK is not here and did not answer (issue 32)",
           (unsigned long long)(uintptr_t)buffer, (unsigned long long)*size);
    return 0;
}
