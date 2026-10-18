/* Expected values: the test puts a size of its own into a blob's record (NdBlob), one that does
 * not fit in 32 bits, and expects it back whole; that the method returns "the size of the data,
 * in bytes" as a SIZE_T is Microsoft's page (read 2026-10-11). */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D10Blob, GetBufferSize)
{
    ID3D10Blob *blob = rt_com_new(&rt_nulldev_blob_class);
    NdBlob *b = rt_com_state_of(blob, &rt_nulldev_blob_class);

    T_CASE("the number of bytes of the blob's data");
    T_EQ(COM_CALL(ID3D10Blob, GetBufferSize, blob), 0);         /* a blob with no data yet */
    b->size = 120;
    T_EQ(COM_CALL(ID3D10Blob, GetBufferSize, blob), 120);
    b->size = UINT64_C(0x123456789);
    T_EQ(COM_CALL(ID3D10Blob, GetBufferSize, blob), UINT64_C(0x123456789));
    b->size = 0;
    T_EQ(COM_CALL(ID3D10Blob, Release, blob), 0);
}
