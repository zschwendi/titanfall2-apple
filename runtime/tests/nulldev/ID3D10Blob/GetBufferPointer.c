/* Expected values: the test puts a block of its own bytes into a blob's record (NdBlob) and
 * expects that pointer back; that the method returns the pointer to the blob's data is
 * Microsoft's page ("Gets a pointer to the data."; read 2026-10-11). */
#include <stdlib.h>
#include <string.h>

#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D10Blob, GetBufferPointer)
{
    ID3D10Blob *blob = rt_com_new(&rt_nulldev_blob_class);
    NdBlob *b = rt_com_state_of(blob, &rt_nulldev_blob_class);
    uint8_t *bytes = malloc(24);                /* the blob frees it when it is destroyed */
    T_REQUIRE(bytes != NULL);
    memset(bytes, 0x5c, 24);
    b->data = bytes;
    b->size = 24;

    T_CASE("the blob's bytes, the same pointer on every call");
    T_TRUE(COM_CALL(ID3D10Blob, GetBufferPointer, blob) == (void *)bytes);
    T_TRUE(COM_CALL(ID3D10Blob, GetBufferPointer, blob) == (void *)bytes);
    T_EQ(((const uint8_t *)COM_CALL(ID3D10Blob, GetBufferPointer, blob))[23], 0x5c);
    T_EQ(COM_CALL(ID3D10Blob, Release, blob), 0);
}
