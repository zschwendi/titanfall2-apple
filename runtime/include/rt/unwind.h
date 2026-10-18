/* Read-only unwinding of the guest stack (docs/RUNTIME.md section 9.3).
 *
 *     Windows function          core function         shim checks itself
 *     RtlCaptureContext         rt_unwind_capture     ContextRecord not NULL
 *     RtlLookupFunctionEntry    rt_unwind_lookup      ImageBase not NULL
 *     RtlVirtualUnwind          rt_unwind_step        FunctionEntry, ContextRecord, HandlerData, EstablisherFrame not NULL
 *     RtlPcToFileHeader         rt_module_from_pc     (rt/module.h; its shim exists)
 *     RtlUnwindEx               none: stays a stop    (plan A: no transfer of control)
 *     (a fatal report)          rt_unwind_backtrace   and its two relatives
 * Body: runtime/core/unwind.c.  Its own header rather than a section of rt/crash.h, because
 * rt/crash.h is the contract of things that END the process and everything here returns;
 * the structures are in rt/win/exception.h beside CONTEXT.
 *
 * WHY THIS IS ONLY COMPUTATION.  Lifted code keeps the guest stack exactly as the x86 code
 * did: every call pushes the guest return address, every function moves rsp and saves
 * registers where the original did.  So each lifted module's exception directory (.pdata)
 * and unwind information (.xdata), which the loader maps with the rest of the image,
 * describe the guest stack truthfully.  Nothing here looks at the host stack, runs guest
 * code, or changes anything but the CONTEXT it is handed.
 *
 * TWO WAYS OF READING GUEST MEMORY.  The three functions that serve the Windows calls read
 * guest memory directly, as Windows does: a CONTEXT whose Rsp points nowhere faults, and the
 * fault is the process's fatal report (it names the shim).  The backtrace functions run
 * inside a fatal report, perhaps on the thread that faulted: they read every byte of guest
 * memory (the stack, the image's header, table and unwind information, the code bytes of an
 * epilogue) only after rt_report_readable has said yes, take NO lock, allocate nothing, and
 * end the walk where the Windows-facing functions would fault or be fatal.
 *
 * DECISIONS (the ones the format leaves open; docs/handoff/rt-unwind.md has the reasons).
 *  1. NATIVE MODULES HAVE NO UNWIND DATA, EXCEPT ONE ADDRESS.  A code address in a native
 *     module (a shim stub, a trap stub) has no function entry: rt_unwind_lookup returns
 *     NULL.  The exception is the return-to-host stub (rt_return_to_host_address, rt/shim.h),
 *     the one native address that ever is a return address on a guest stack: it is what
 *     every guest function called by host code (rt_guest_call: a callback from a shim, a
 *     DllMain, a thread's start routine) returns to.  For it rt_unwind_lookup returns a
 *     function entry made by the runtime, and rt_unwind_step over that entry makes ALL THE
 *     HOST FRAMES BETWEEN THE GUEST CALLEE AND THE GUEST CALLER OF THE SHIM ONE FRAME:
 *       - if the calling thread is inside a shim whose call lies above the frame (the
 *         innermost RtShimFrame with entry_rsp > context->Rsp), the caller is the guest code
 *         that called that shim: Rip = the return address at entry_rsp, Rsp = entry_rsp + 8,
 *         every other register unchanged (a shim changes no non-volatile guest register);
 *       - otherwise this was the outermost guest call of the thread (its start routine):
 *         Rip = 0, which is how a Windows stack ends, and Rsp is left alone.
 *     So a guest walker of either common shape ("stop when the lookup fails", "treat a
 *     failed lookup as a leaf and stop at Rip 0") ends cleanly and sees the guest callers
 *     across a callback.  A walker that reads .pdata itself finds none in a native module
 *     and is on its own.  The entry is 16 bytes of ntdll.dll's native module at
 *     RT_NATIVE_UNWIND, written on first use.
 *  2. EPILOGUES ARE DECODED, AS WINDOWS DOES.  Version 1 unwind information does not say
 *     where a function's epilogues are; Windows looks at the code bytes at the pc, and so
 *     does rt_unwind_step (the bytes are in guest memory: the image is mapped whole).  The
 *     forms recognised are listed at rt_unwind_step.  Version 2 marks its epilogues with
 *     UWOP_EPILOG codes and no byte is decoded.
 *  3. A FRAME THAT CANNOT BE EXPLAINED: for the Windows-facing functions, unwind information
 *     that is not understood is a fatal report that names the function (a wrong frame would
 *     be silent); for a backtrace it ends the walk.  "Not understood" is: a version other
 *     than 1 and 2; an operation code this header does not list, or 6 or 7 in version 1; a
 *     code that runs past the count; more than RT_UNWIND_CHAIN_MAX chained entries; a
 *     function entry with bit 0 of UnwindData set (an indirect entry, which only Windows'
 *     own images use and no published document describes).
 *  4. THE FLAGS, MXCSR AND SEGMENTS OF A CAPTURED CONTEXT are stated at rt_unwind_capture.
 *
 * LAST ERROR: no function here reads or writes it.  LOCKS: rt_unwind_lookup takes the
 * loader lock for the length of the look (rt_module_from_pc), so the caller must hold no
 * other runtime lock; nothing else takes any.  THREADS: every function may be called from
 * any thread; rt_unwind_step's decision 1 uses the CALLING thread's shim frames, so a
 * CONTEXT is unwound through the stub correctly only on the thread it was captured on
 * (another thread's context ends at the stub, with Rip 0, unless the frame happens to lie
 * below one of the caller's shim frames; there is no GetThreadContext to make one).
 */
#ifndef RT_UNWIND_H
#define RT_UNWIND_H

#include "runtime.h"
#include "rt/wintypes.h"
#include "rt/win/exception.h"

#define RT_UNWIND_CHAIN_MAX  32u        /* chained function entries followed for one frame */
#define RT_NATIVE_UNWIND     0x001000u  /* offset in ntdll.dll's native module (rt/module.h) of the
                                           return-to-host stub's RUNTIME_FUNCTION; its UNWIND_INFO
                                           (version 1, no codes) is at + 0x10 */

/* RtlLookupFunctionEntry(ControlPc, ImageBase, HistoryTable): the function entry that
 * covers the guest code address `pc`, and in *image_base the base its RVAs are relative to.
 *   pc in a loaded lifted module   binary search of the module's mapped exception directory
 *       for the row with BeginAddress <= pc - base < EndAddress.  The row is returned as it
 *       stands in the image: when its unwind information is chained (UNW_FLAG_CHAININFO)
 *       that is the chained row, as on Windows, because its own codes count; the chain is
 *       followed by rt_unwind_step.  NULL if no row covers pc: a leaf function (it moves
 *       rsp nowhere and its return address is at [rsp]), or a module with no exception
 *       directory, or one whose directory does not lie inside the image.
 *       *image_base = the module's base either way.
 *   pc is the return-to-host stub (any of its 16 bytes)   the runtime's entry (decision 1);
 *       *image_base = ntdll.dll's base.
 *   pc elsewhere in a native module   NULL; *image_base = that module's base.
 *   pc in no module, or in a lifted module that is not loaded   NULL; *image_base = 0.
 * `image_base` may be NULL.  The HistoryTable argument of the Windows function is a cache
 * for the caller's benefit and has no counterpart: the shim ignores it.  For a pc in no
 * module Windows stores 0 (older Windows leaves it unchanged: Wine ntdll/tests/unwind.c);
 * for a pc in a module with no entry, what it stores is not verified.
 * Fatal: a row with bit 0 of UnwindData set (decision 3). */
const RUNTIME_FUNCTION *rt_unwind_lookup(uint64_t pc, uint64_t *image_base);

/* RtlVirtualUnwind(HandlerType, ImageBase, ControlPc, FunctionEntry, ContextRecord,
 * HandlerData, EstablisherFrame, ContextPointers): turn `context`, the state at `pc` inside
 * the function `fn` describes, into the state of its caller just after the call returned.
 *
 *   fn NULL (the shim does not pass this on: Windows faults)   a leaf function:
 *       Rip = [Rsp], Rsp += 8, *establisher_frame = the old Rsp.  Returns 0.
 *   fn is the return-to-host stub's entry   decision 1.  *establisher_frame = the old Rsp.
 *       Returns 0.
 *   otherwise, with off = pc - (image_base + fn->BeginAddress) and the UNWIND_INFO at
 *   image_base + fn->UnwindData:
 *     1. *establisher_frame, the base of the function's fixed stack allocation: Rsp if the
 *        function has no frame register; else <frame register> - 16 * <frame offset> once
 *        the register is established (off >= size of prologue, or the entry is chained, or
 *        off >= the offset of its UWOP_SET_FPREG code), Rsp before that.
 *     2. THE EPILOGUE RULE.  Version 1: the bytes at pc are decoded.  It is an epilogue if
 *        they are, in this order: optionally `add rsp, imm8` (48 83 C4 ib), `add rsp, imm32`
 *        (48 81 C4 id), or `lea rsp, [fp + disp8 | disp32]` on the function's frame
 *        register (48|49 8D 6x ib, 48|49 8D Ax id); any number of `pop r64` (58+r, 4x 58+r);
 *        optionally the prefix F2; then one of `ret` (C3), `ret imm16` (C2), `rep ret`
 *        (F3 C3), `jmp rel8 | rel32` (EB, E9) to a target outside the function (outside
 *        this entry and not in another entry of the same chain, or exactly at the start of
 *        the function), `jmp [rip + disp32]` (FF 25), or an indirect jump with a REX.W
 *        prefix (48 FF /4), which compilers use only for tail calls.  Then those
 *        instructions are carried out on the context (rsp set, registers popped, and a
 *        return whatever the last instruction was) and the unwind codes are not used.
 *        Version 2: pc is in an epilogue if it lies in one of the ranges the UWOP_EPILOG
 *        codes give; an epilogue of version 2 is only pops (the mirror image of the
 *        UWOP_PUSH_NONVOL codes, those already executed judged by the byte offset into the
 *        epilogue) and the return.  Returns 0 from an epilogue.
 *     3. THE PROLOGUE RULE AND THE CODES.  Each code whose prologue offset is <= off is
 *        undone, in the order of the array (which is the reverse of the prologue); a code
 *        whose offset is beyond off has not happened yet and is skipped:
 *          UWOP_PUSH_NONVOL        register = [Rsp]; Rsp += 8
 *          UWOP_ALLOC_LARGE/SMALL  Rsp += size
 *          UWOP_SET_FPREG          Rsp = <frame register> - 16 * <frame offset>
 *          UWOP_SAVE_NONVOL(_FAR)  register = [*establisher_frame + offset]
 *          UWOP_SAVE_XMM128(_FAR)  the 16 bytes of the Xmm register (in context->FltSave)
 *                                  = [*establisher_frame + offset]
 *          UWOP_PUSH_MACHFRAME     Rip = [Rsp + e], Rsp = [Rsp + e + 24], e = 8 with an
 *                                  error code and 0 without; no return address is popped
 *          UWOP_EPILOG, UWOP_SPARE_CODE (version 2)   nothing
 *        If the information is chained, the parent entry that follows the codes is
 *        processed the same way (pc measured from the parent's start), up to
 *        RT_UNWIND_CHAIN_MAX entries.  Then, unless a machine frame set them:
 *        Rip = [Rsp], Rsp += 8.
 *     4. The result.  `handler_type` is UNW_FLAG_NHANDLER (the result is then always 0),
 *        UNW_FLAG_EHANDLER or UNW_FLAG_UHANDLER.  If the primary (last, not chained) entry
 *        has that flag and pc is past its prologue: returns image_base + the handler's RVA
 *        and stores in *handler_data the address of the language-specific data that
 *        follows it.  Otherwise returns 0 and leaves *handler_data alone.  The handler is
 *        only reported: nothing here ever calls one (plan A).
 *   pointers   if not NULL: for each integer or Xmm register restored from memory, the
 *       address it was read from; the other entries are not written.
 *   handler_data, establisher_frame   may be NULL here (the shim requires them, as Windows does).
 * Only Rsp, Rip, the sixteen integer registers and Xmm0-15 of `context` are read or written.
 * Fatal: unwind information that is not understood (decision 3). */
GUESTFN rt_unwind_step(DWORD handler_type, uint64_t image_base, uint64_t pc, const RUNTIME_FUNCTION *fn,
                       CONTEXT *context, uint64_t *handler_data, uint64_t *establisher_frame,
                       KNONVOLATILE_CONTEXT_POINTERS *pointers);

/* RtlCaptureContext(ContextRecord): the state of the guest thread `ctx` as its caller will
 * find it when the call it is making now has returned.  `ctx` must be at a call boundary:
 * ctx->r[4] is the address of the return address, which is what a shim's ctx is.
 *   Rip            the return address, [ctx->r[4]]
 *   Rsp            ctx->r[4] + 8
 *   Rax..R15       ctx->r[] (so Rcx is the ContextRecord pointer itself, as on Windows)
 *   EFlags         the lifter's pushfq image (lifter/insns/int_pushf.py): CF bit 0, bit 1 set,
 *                  PF bit 2, AF bit 4, ZF bit 6, SF bit 7, IF (bit 9) set, DF bit 10, OF bit 11,
 *                  from the seven flag bytes of Ctx; every other bit 0
 *   MxCsr          ctx->mxcsr, in CONTEXT.MxCsr and in the save area (FltSave + 24)
 *   SegCs          0x33;  SegDs, SegEs, SegGs, SegSs 0x2B;  SegFs 0x53 (64-bit user mode)
 *   ContextFlags   CONTEXT_FULL | CONTEXT_SEGMENTS (0x10000F)
 *   FltSave        the 512-byte save area: control word = ctx->fpcw, MxCsr as above, MxCsr
 *                  mask 0xFFFF, Xmm0-15 = ctx->xmm[]; everything else in it 0
 * NOT MEANINGFUL: the x87 part of the save area (status word, tags and the eight registers
 * read as an empty stack: Ctx has no x87 registers); Dr0-Dr7, P1Home-P6Home, VectorRegister,
 * VectorControl and the five debug-control fields are NOT WRITTEN, as on Windows, so they
 * hold whatever the caller's buffer held.
 * ContextFlags and the list of registers written agree with Wine's ntdll/tests/exception.c
 * (test_thread_context, x86-64 part); that the fields named above stay unwritten is not
 * asserted there.  Fatal: nothing (a bad `context` faults). */
void rt_unwind_capture(const Ctx *ctx, CONTEXT *context);

/* ---- the guest call stack for a fatal report ---------------------------------------------------
 * Each writes at most `max` guest code addresses to out[], innermost first, and returns how
 * many.  out[0] is where the walk starts; every later entry is a return address.  The
 * return-to-host stub appears as a frame of its own (it reads "ntdll.dll!rt_return_to_host"
 * in rt_module_describe) and the walk goes on above it as decision 1 says, with the shim
 * frames of RT_THREAD(ctx) (so pass the thread's own ctx; a ctx with no thread behind it,
 * host NULL, ends at the stub).
 *
 * THE WALK ENDS, without ever faulting or looping, at the first of:
 *   - `max` frames;
 *   - the stub with no shim frame above it: the base of the thread's guest calls.  This and
 *     only this is a complete stack;
 *   - an address in a native module other than the stub (written to out[], then the end);
 *   - an address in no module, or in a lifted module that is not loaded (not written);
 *   - guest memory that rt_report_readable refuses;
 *   - unwind information that is not understood (decision 3);
 *   - a frame after which Rsp is not higher than before.
 * `end`, if not NULL, receives which (RtUnwindEnd), for the report's last line. */
typedef enum {
    RT_UNWIND_END_MAX = 0, RT_UNWIND_END_BASE, RT_UNWIND_END_NATIVE, RT_UNWIND_END_NO_MODULE,
    RT_UNWIND_END_UNREADABLE, RT_UNWIND_END_BAD_DATA, RT_UNWIND_END_RSP
} RtUnwindEnd;

/* From a thread at a call boundary (inside a shim, a trap stub, a guest callback written in
 * C): the return address is at [ctx->r[4]].  out[0] is that return address. */
unsigned rt_unwind_backtrace(const Ctx *ctx, uint64_t *out, unsigned max);

/* The same, with the return address at the guest address `ret_addr_at` instead of at
 * ctx->r[4] (a shim frame's entry_rsp).  The registers are taken from `ctx`. */
unsigned rt_unwind_backtrace_at(const Ctx *ctx, uint64_t ret_addr_at, uint64_t *out, unsigned max, RtUnwindEnd *end);

/* From a thread stopped somewhere inside the lifted function at guest address `fn`, when
 * the instruction is not known (a host signal: rt/crash.h).  The frame is unwound as if
 * the pc were in the function's BODY: every unwind code applies, no epilogue.  out[0] is
 * `fn` itself.  It is exact unless the thread was in the function's prologue or epilogue,
 * or ctx->r[4] and the frame register in `ctx` are older than the fault; then the walk is
 * wrong from the first step and usually ends at once (an address in no module). */
unsigned rt_unwind_backtrace_body(const Ctx *ctx, uint64_t fn, uint64_t *out, unsigned max, RtUnwindEnd *end);

#endif /* RT_UNWIND_H */
