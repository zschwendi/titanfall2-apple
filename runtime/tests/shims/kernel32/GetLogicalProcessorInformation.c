#include "rt/test.h"
#include "rt/machine.h"
#include "rt/win/sysinfo.h"

#define RECORD 32u                  /* sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION), asserted in sysinfo.h */

/* The buffer is 70 records: the largest answer (64 processors: 66 records) and a margin. */
#define BUF_RECORDS 70u

static void fill(unsigned char *p, unsigned len, unsigned char v)
{
    for (unsigned k = 0; k < len; k++) p[k] = v;
}

static unsigned count_not(const unsigned char *p, unsigned len, unsigned char v)
{
    unsigned bad = 0;
    for (unsigned k = 0; k < len; k++) if (p[k] != v) bad++;
    return bad;
}

/* The 32 bytes a record must have (little endian, as the guest reads them): the mask, the
 * relationship, and every other byte 0. */
static void expect_bytes(unsigned char want[RECORD], ULONG_PTR mask, LOGICAL_PROCESSOR_RELATIONSHIP rel)
{
    for (unsigned k = 0; k < RECORD; k++) want[k] = 0;
    for (unsigned k = 0; k < 8; k++) want[k] = (unsigned char)(mask >> (8 * k));
    for (unsigned k = 0; k < 4; k++) want[8 + k] = (unsigned char)((unsigned)rel >> (8 * k));
}

static void check_record(const unsigned char *rec, ULONG_PTR mask, LOGICAL_PROCESSOR_RELATIONSHIP rel)
{
    unsigned char want[RECORD];
    expect_bytes(want, mask, rel);
    for (unsigned k = 0; k < RECORD; k++) T_EQ(rec[k], want[k]);
}

TEST(kernel32, GetLogicalProcessorInformation)
{
    const RtMachine *m = rt_machine();
    const DWORD n = m->cpu_count;
    const DWORD needed = (n + 2) * RECORD;      /* n core records, one NUMA node, one package */
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION buf[BUF_RECORDS];
    unsigned char *bytes = (unsigned char *)buf;
    DWORD len;

    T_CASE("size query with NULL Buffer and length 0 returns the needed size and ERROR_INSUFFICIENT_BUFFER");
    CALL(kernel32, SetLastError, 0);
    len = 0;
    T_EQ(CALL(kernel32, GetLogicalProcessorInformation, NULL, &len), FALSE);
    T_EQ(len, needed);
    T_EQ(T_LAST_ERROR(), ERROR_INSUFFICIENT_BUFFER);

    T_CASE("a buffer 1 byte too small fails, reports the needed size and writes nothing");
    fill(bytes, sizeof buf, 0xA5);
    len = needed - 1;
    T_EQ(CALL(kernel32, GetLogicalProcessorInformation, buf, &len), FALSE);
    T_EQ(len, needed);
    T_EQ(T_LAST_ERROR(), ERROR_INSUFFICIENT_BUFFER);
    T_EQ(count_not(bytes, sizeof buf, 0xA5), 0);

    T_CASE("an exact buffer succeeds with the needed size");
    fill(bytes, sizeof buf, 0xA5);
    len = needed;
    T_EQ(CALL(kernel32, GetLogicalProcessorInformation, buf, &len), TRUE);
    T_EQ(len, needed);

    T_CASE("a buffer 32 bytes larger succeeds and writes nothing past the needed size");
    fill(bytes, sizeof buf, 0xA5);
    len = needed + RECORD;
    T_EQ(CALL(kernel32, GetLogicalProcessorInformation, buf, &len), TRUE);
    T_EQ(len, needed);
    T_EQ(count_not(bytes + needed, RECORD, 0xA5), 0);

    T_CASE("one core record per processor, a NUMA node record and a package record, every byte as specified");
    T_TRUE(n >= 1 && n <= 64);
    fill(bytes, sizeof buf, 0xA5);
    len = needed;
    T_EQ(CALL(kernel32, GetLogicalProcessorInformation, buf, &len), TRUE);
    {
        const ULONG_PTR all = (n >= 64) ? ~(ULONG_PTR)0 : (((ULONG_PTR)1 << n) - 1);
        for (DWORD i = 0; i < n; i++)
            check_record(bytes + i * RECORD, (ULONG_PTR)1 << i, RelationProcessorCore);
        check_record(bytes + n * RECORD, all, RelationNumaNode);
        check_record(bytes + (n + 1) * RECORD, all, RelationProcessorPackage);
    }

    T_CASE("the core records and the package mask agree with GetSystemInfo");
    {
        SYSTEM_INFO si;
        rt_machine_system_info(&si);
        unsigned cores = 0;
        ULONG_PTR covered = 0;
        for (DWORD i = 0; i < len / RECORD; i++) {
            if (buf[i].Relationship == RelationProcessorCore) {
                cores++;
                covered |= buf[i].ProcessorMask;
            }
        }
        T_EQ(cores, si.dwNumberOfProcessors);
        T_EQ(covered, si.dwActiveProcessorMask);
        T_EQ(buf[n + 1].ProcessorMask, si.dwActiveProcessorMask);
        T_EQ(len / RECORD, si.dwNumberOfProcessors + 2);
    }

    T_CASE("does not change last error on success");
    CALL(kernel32, SetLastError, 0x1234);
    len = needed;
    T_EQ(CALL(kernel32, GetLogicalProcessorInformation, buf, &len), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* The two stops: the documentation does not settle them (no T_CASE of their own). */
    T_TRAPS(CALL(kernel32, GetLogicalProcessorInformation, buf, NULL), "NULL ReturnedLength");
    len = needed;
    T_TRAPS(CALL(kernel32, GetLogicalProcessorInformation, NULL, &len), "NULL Buffer");
}
