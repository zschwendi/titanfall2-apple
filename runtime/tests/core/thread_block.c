/* Guest thread 1: the thread block, the process block and the guest stack
 * (docs/RUNTIME.md sections 5.3, 6.1, 6.2), and the start-up state around them.
 */
#include <sys/stat.h>

#include "rt/test.h"
#include "rt/heap.h"
#include "rt/machine.h"
#include "rt/vm.h"

TEST(core, main_thread)
{
    Ctx *ctx = T_CTX();
    RtThread *t = RT_THREAD(ctx);
    T_TRUE(ctx == rt_main_ctx());
    T_TRUE(t == rt_thread_self());
    T_TRUE(&t->ctx == ctx);
    T_EQ(t->id, RT_FIRST_THREAD_ID);
    T_EQ(t->is_main, 1);
    T_EQ(t->in_host, 0);
    T_TRUE(t->shim == NULL);
    T_EQ(ctx->gs_base, t->teb);
    T_EQ(ctx->mxcsr, 0x1F80);
    T_EQ(ctx->df, 0);
    T_TRUE(t->tso == 0 || t->tso == 1);         /* the ordering mode is requested, not required, in tests */
    T_EQ(tf_fpcr_read(), TF_FPCR_HOST);         /* host code runs with the host float mode */

    rt_thread_set_name(t, "Main Thread");
    T_TRUE(!strcmp(t->name, "Main Thread"));
    char longer[100];
    memset(longer, 'x', sizeof longer - 1);
    longer[sizeof longer - 1] = 0;
    rt_thread_set_name(t, longer);
    T_EQ(strlen(t->name), sizeof t->name - 1);

    /* Last error for core code that has no ctx is the same field GetLastError reads. */
    rt_thread_set_last_error(ERROR_NOT_SUPPORTED);
    T_EQ(LD32(ctx->gs_base + 0x68), ERROR_NOT_SUPPORTED);
    T_EQ(rt_thread_get_last_error(), ERROR_NOT_SUPPORTED);
    T_EQ(T_LAST_ERROR(), ERROR_NOT_SUPPORTED);
}

TEST(core, thread_block)
{
    /* The fields of section 6.2, at the Windows offsets, read as guest code reads them. */
    Ctx *ctx = T_CTX();
    RtThread *t = RT_THREAD(ctx);
    uint64_t teb = ctx->gs_base;
    T_TRUE(rt_vm_in_vm_arena(teb));
    T_EQ(teb & 0xFFFF, 0);
    T_EQ(LD64(teb + 0x30), teb);                                /* Self */
    T_EQ(LD64(teb + 0x40), RT_PROCESS_ID);
    T_EQ(LD64(teb + 0x48), RT_FIRST_THREAD_ID);
    T_EQ(RT_PROCESS_ID % 4, 0);
    T_EQ(RT_FIRST_THREAD_ID % 4, 0);
    T_EQ(LD32(teb + 0x68), 0);                                  /* LastErrorValue starts at 0 */

    /* The guest stack (section 5.3): 8 MiB for the first thread, fully committed, with
     * 64 KB below it that is never accessible. */
    uint64_t base = LD64(teb + 0x08), limit = LD64(teb + 0x10), region = LD64(teb + 0x1478);
    T_EQ(base, t->stack_base);
    T_EQ(limit, t->stack_limit);
    T_EQ(region, t->stack_region);
    T_EQ(base - limit, UINT64_C(8) << 20);
    T_EQ(limit - region, 0x10000);
    T_EQ(region & 0xFFFF, 0);
    T_TRUE(rt_vm_in_vm_arena(region) && rt_vm_in_vm_arena(base - 1));
    T_TRUE(ctx->r[4] > limit && ctx->r[4] < base);
    T_EQ(ctx->r[4] & 15, 0);
    ST64(limit, 0x1234);                                        /* the lowest usable address is usable */
    T_EQ(LD64(limit), 0x1234);
    ST64(base - 8, 0x5678);
    T_EQ(LD64(base - 8), 0x5678);

    /* The static TLS pointer array exists and is zero until a lifted module is loaded;
     * the dynamic TLS slots are zero. */
    uint64_t tls = LD64(teb + 0x58);
    T_TRUE(tls != 0 && rt_vm_in_vm_arena(tls));
    for (unsigned i = 0; i < 64; i++) T_REQUIRE(LD64(tls + 8 * (uint64_t)i) == 0);
    for (unsigned i = 0; i < 64; i++) T_REQUIRE(LD64(teb + 0x1480 + 8 * (uint64_t)i) == 0);
    T_EQ(LD64(teb + 0x1780), 0);                                /* TlsExpansionSlots: not allocated */
}

TEST(core, process_block)
{
    uint64_t peb = LD64(T_CTX()->gs_base + 0x60);
    const RtMachine *mc = rt_machine();
    T_TRUE(peb != 0 && rt_vm_in_vm_arena(peb));
    T_EQ(LD8(peb + 0x02), 0);                                   /* BeingDebugged */
    T_EQ(LD64(peb + 0x10), rt_module_exe()->base);              /* ImageBaseAddress */
    T_EQ(LD32(peb + 0xB8), mc->cpu_count);                      /* NumberOfProcessors */
    T_EQ(LD32(peb + 0x118), 6);                                 /* Windows 7 SP1: 6.1.7601 */
    T_EQ(LD32(peb + 0x11C), 1);
    T_EQ(LD16(peb + 0x120), 7601);
    T_EQ(LD16(peb + 0x122), 0x0100);
    T_EQ(LD32(peb + 0x124), 2);
    T_EQ(LD64(peb + 0x30), (uint64_t)rt_heap_process());        /* ProcessHeap */
}

TEST(core, machine_description)
{
    const RtMachine *mc = rt_machine();
    T_EQ(mc->os_major, 6);
    T_EQ(mc->os_minor, 1);
    T_EQ(mc->os_build, 7601);
    T_EQ(mc->sp_major, 1);
    T_TRUE(!strcmp(mc->sp_name, "Service Pack 1"));
    T_EQ(mc->page_size, 4096);
    T_EQ(mc->granularity, 65536);
    T_EQ(mc->min_address, 0x10000);
    T_EQ(mc->max_address, UINT64_C(0x7FFFFFEFFFF));
    T_EQ(mc->cpu_family, 6);
    T_EQ(mc->cpu_hz, UINT64_C(3000000000));
    T_EQ(mc->host_page_size, rt_vm_host_page());
    T_TRUE(mc->host_cores >= 1 && mc->host_perf_cores >= 1 && mc->host_perf_cores <= mc->host_cores);
    T_TRUE(mc->cpu_count >= 1 && mc->cpu_count <= 64);
    T_TRUE(mc->phys_total >= (UINT64_C(1) << 30) && mc->phys_avail == mc->phys_total / 2);
    T_EQ(mc->lcid, 0x0409);
    T_EQ(mc->ansi_cp, 1252);
    T_EQ(mc->oem_cp, 437);
}

TEST(core, start_up)
{
    /* Each test gets its own directories from the runner, and the runtime was told about them. */
    const RtConfig *cfg = rt_config();
    struct stat st;
    T_TRUE(!strcmp(cfg->state_dir, rt_test_state_dir()) && !strcmp(cfg->install_root, rt_test_install_root()));
    T_TRUE(stat(cfg->state_dir, &st) == 0 && S_ISDIR(st.st_mode));
    T_TRUE(stat(cfg->install_root, &st) == 0 && S_ISDIR(st.st_mode));
    T_TRUE(strstr(cfg->state_dir, "rt-test.") != NULL);
    T_EQ(cfg->tso, RT_TSO_TRY);
    /* Starting twice is a bug in the caller. */
    T_TRAPS(rt_init(cfg), "rt_init called twice");
}
