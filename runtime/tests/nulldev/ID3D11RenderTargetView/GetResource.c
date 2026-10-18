/* Expected values, none from the body.  A view holds the resource it was given (rt/nulldev.h,
 * NdView.resource: "the face the creator was given"); the test gives it that way a creator does,
 * with rt_nulldev_bind.  GetResource hands the resource out with one more reference for the
 * receiver: Microsoft's page of ID3D11View::GetResource (read 2026-10-10: "This function
 * increments the reference count of the resource by one").  The counts are the test's own
 * arithmetic: the texture has one from its creation and one from the view that holds it, so
 * two before the call and three after.  A view made by rt_nulldev_new has no resource (its
 * record is zeroed), so NULL.  The stop is the notes' rule: the page names no result for a
 * NULL ppResource. */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11RenderTargetView, GetResource)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    ID3D11Texture2D *tex = rt_nulldev_new(device, &rt_nulldev_texture2d_class);
    ID3D11RenderTargetView *view = rt_nulldev_new(device, &rt_nulldev_rtv_class);
    NdView *v = rt_com_state_of(view, &rt_nulldev_rtv_class);
    ID3D11Resource *out = NULL;

    T_CASE("a view with a resource gives it, with one more reference for the receiver");
    T_EQ(rt_com_refs(tex), 1);
    rt_nulldev_bind(&v->resource, tex);
    T_EQ(rt_com_refs(tex), 2);                          /* the view holds it */
    T_EQ(rt_com_refs(view), 1);
    COM_CALL(ID3D11RenderTargetView, GetResource, view, &out);
    T_TRUE(out == (ID3D11Resource *)tex);
    T_EQ(rt_com_refs(tex), 3);
    T_EQ(rt_com_refs(view), 1);
    T_EQ(COM_CALL(ID3D11Texture2D, Release, (ID3D11Texture2D *)out), 1);   /* Release answers the caller's own references: the creator's one is left; the view's hold is not one of them (rt/com.h, the references rule of d3d-fix-2) */

    T_CASE("a view with no resource gives NULL");
    ID3D11RenderTargetView *bare = rt_nulldev_new(device, &rt_nulldev_rtv_class);
    out = (ID3D11Resource *)tex;                        /* not NULL before the call: the call must overwrite it */
    COM_CALL(ID3D11RenderTargetView, GetResource, bare, &out);
    T_TRUE(out == NULL);
    T_EQ(rt_com_refs(tex), 2);

    T_TRAPS(COM_CALL(ID3D11RenderTargetView, GetResource, view, NULL), "GetResource with a NULL out-pointer");
}
