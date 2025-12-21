//===== Copyright © 2005-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: A set of utilities to render standard shapes
//
//===========================================================================//
//
///////////////////////////////////////////////////////////////////////////////

#include "mathlib/color.h"
#include "mathlib/vector.h"
#include "mathlib/vector2d.h"
#include "mathlib/vector4d.h"
#include "mathlib/mathlib.h"
#include "tier2/renderutils.h"
#include "rtech/pak/pakstate.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/cmaterialsystem.h"
#include "materialsystem/cmatrendercontext.h"
#include "materialsystem/cmatqueuedrendercontext.h"
#include "materialsystem/meshbuilder.h"

//-----------------------------------------------------------------------------
// Purpose: standard materials
//-----------------------------------------------------------------------------
static IMaterial* s_opaqueIgnoreZWire;
static IMaterial* s_transIgnoreZWire;
static IMaterial* s_opaqueNormalZWire;
static IMaterial* s_transNormalZWire;
static IMaterial* s_opaqueIgnoreZFront;
static IMaterial* s_transIgnoreZFront;
static IMaterial* s_opaqueNormalZFront;
static IMaterial* s_transNormalZFront;
static IMaterial* s_opaqueIgnoreZBoth;
static IMaterial* s_transIgnoreZBoth;
static IMaterial* s_opaqueNormalZBoth;
static IMaterial* s_transNormalZBoth;

static bool s_standardMaterialsInitialized = false;

//-----------------------------------------------------------------------------
// Purpose: initializes the standard materials; these must always be available
//-----------------------------------------------------------------------------
static void InitializeStandardMaterials()
{
    // Load engine materials first before proceeding with the SDK.
    // The engine's impl handles LOCAL_THREAD_LOCK internally.
    v_InitializeStandardMaterials();

    LOCAL_THREAD_LOCK();

    if (s_standardMaterialsInitialized)
        return;

    s_standardMaterialsInitialized = true;
    Assert(g_pakLoadApi);

    s_opaqueIgnoreZWire = (IMaterial*)g_pakLoadApi->FindAssetByName("material/opaque_ignorez_wire_rgdu.rpak");
    s_transIgnoreZWire = (IMaterial*)g_pakLoadApi->FindAssetByName("material/trans_ignorez_wire_rgdu.rpak");

    s_opaqueNormalZWire = (IMaterial*)g_pakLoadApi->FindAssetByName("material/opaque_normalz_wire_rgdu.rpak");
    s_transNormalZWire = (IMaterial*)g_pakLoadApi->FindAssetByName("material/trans_normalz_wire_rgdu.rpak");

    s_opaqueIgnoreZFront = (IMaterial*)g_pakLoadApi->FindAssetByName("material/opaque_ignorez_front_rgdu.rpak");
    s_transIgnoreZFront = (IMaterial*)g_pakLoadApi->FindAssetByName("material/trans_ignorez_front_rgdu.rpak");

    s_opaqueNormalZFront = (IMaterial*)g_pakLoadApi->FindAssetByName("material/opaque_normalz_front_rgdu.rpak");
    s_transNormalZFront = (IMaterial*)g_pakLoadApi->FindAssetByName("material/trans_normalz_front_rgdu.rpak");

    s_opaqueIgnoreZBoth = (IMaterial*)g_pakLoadApi->FindAssetByName("material/opaque_ignorez_both_rgdu.rpak");
    s_transIgnoreZBoth = (IMaterial*)g_pakLoadApi->FindAssetByName("material/trans_ignorez_both_rgdu.rpak");

    s_opaqueNormalZBoth = (IMaterial*)g_pakLoadApi->FindAssetByName("material/opaque_normalz_both_rgdu.rpak");
    s_transNormalZBoth = (IMaterial*)g_pakLoadApi->FindAssetByName("material/trans_normalz_both_rgdu.rpak");

    // Make sure all standard materials have loaded, if this fails, then most
    // likely the startup.rpak is corrupt or the materials have been renamed.
    Assert(s_opaqueIgnoreZWire);
    Assert(s_transIgnoreZWire);

    Assert(s_opaqueNormalZWire);
    Assert(s_transNormalZWire);

    Assert(s_opaqueIgnoreZFront);
    Assert(s_transIgnoreZFront);

    Assert(s_opaqueNormalZFront);
    Assert(s_transNormalZFront);

    Assert(s_opaqueIgnoreZBoth);
    Assert(s_transIgnoreZBoth);

    Assert(s_opaqueNormalZBoth);
    Assert(s_transNormalZBoth);
}

struct RenderLineQueue_s
{
    void (*function)(const Vector3D& p1, const Vector3D& p2, const Color c, IMaterial* const pMaterial);
    Vector3D v1;
    Vector3D v2;
    Color color;
    IMaterial* material;
};

//-----------------------------------------------------------------------------
// Purpose: process and advance line render queue
//-----------------------------------------------------------------------------
static void RenderLineQueueFunctor(CallQueue_s* const queue)
{
    RenderLineQueue_s* const item = (RenderLineQueue_s*)queue->GetCurrentCallItem();
    item->function(item->v1, item->v2, item->color, item->material);

    // Advance the queue.
    queue->currentCallIndex += sizeof(RenderLineQueue_s);
}

//-----------------------------------------------------------------------------
// Purpose: renders a line:
//        _+v1
//        /|
//       /
//      /
//     /
//    /
//  |/
//   -+v2
//-----------------------------------------------------------------------------
static void RenderLineInternal(const Vector3D& v1, const Vector3D& v2, const Color c, IMaterial* const pMaterial)
{
    InitializeStandardMaterials();

    // Queue it off if this is called outside the render thread.
    if ((*g_fnHasRenderCallQueue)())
    {
        CallQueue_s* const queue = (*g_fnAddRenderCallQueueItem)(RenderLineQueueFunctor, sizeof(RenderLineQueue_s), 7);
        RenderLineQueue_s* const item = (RenderLineQueue_s*)queue->GetCurrentAllocatedItem();

        item->function = RenderLineInternal;
        item->v1 = v1;
        item->v2 = v2;
        item->color = c;
        item->material = pMaterial;

        (*g_fnAdvanceRenderCallQueue)(sizeof(RenderLineQueue_s));
        return;
    }

    CMatRenderContext* const ctx = g_pMaterialSystem->GetRenderContext();
    CMeshVertexBuilder vertexBuilder;

    if (vertexBuilder.Begin(ctx, 2))
    {
        ctx->Bind(pMaterial);

        vertexBuilder.AppendVertex(v1, c);
        vertexBuilder.AppendVertex(v2, c);

        vertexBuilder.End(ctx);
        ctx->DrawLineList(vertexBuilder.GetParams(), nullptr, 0);
    }

    // Need to call this to decrement context ref counter.
    ctx->EndRenderer();
}

//-----------------------------------------------------------------------------
// purpose: box indices
//-----------------------------------------------------------------------------
static const int s_boxFaceIndices[6][4] =
{
    { 0, 4, 6, 2 }, // -x
    { 5, 1, 3, 7 }, // +x
    { 0, 1, 5, 4 }, // -y
    { 2, 6, 7, 3 }, // +y
    { 0, 2, 3, 1 },	// -z
    { 4, 5, 7, 6 }	// +z
};
static const int s_boxFaceIndicesInsideOut[6][4] =
{
    { 0, 2, 6, 4 }, // -x
    { 5, 7, 3, 1 }, // +x
    { 0, 4, 5, 1 }, // -y
    { 2, 3, 7, 6 }, // +y
    { 0, 1, 3, 2 },	// -z
    { 4, 6, 7, 5 }	// +z
};

//-----------------------------------------------------------------------------
// Purpose: generates the box vertices from the rotation matrix
//-----------------------------------------------------------------------------
static void GenerateBoxVertices(const matrix3x4_t& fTransformMatrix, const Vector3D& vMins, const Vector3D& vMaxs, Vector3D pVerts[8])
{
    Vector3D vecPos;
    for (int i = 0; i < 8; ++i)
    {
        vecPos.x = (i & 0x1) ? vMaxs.x : vMins.x;
        vecPos.y = (i & 0x2) ? vMaxs.y : vMins.y;
        vecPos.z = (i & 0x4) ? vMaxs.z : vMins.z;

        VectorTransform(vecPos, fTransformMatrix, pVerts[i]);
    }
}

struct RenderBoxQueue_s
{
    void (*function)(const matrix3x4_t& fTransformMatrix, const Vector3D& vMins, const Vector3D& vMaxs, const Color c, IMaterial* const pMaterial, const bool bInsideOut);
    matrix3x4_t fTransformMatrix;
    Vector3D vMins;
    Vector3D vMaxs;
    Color color;
    IMaterial* material;
    bool bInsideOut;
};

//-----------------------------------------------------------------------------
// Purpose: process and advance box render queue
//-----------------------------------------------------------------------------
static void RenderBoxQueueFunctor(CallQueue_s* const queue)
{
    RenderBoxQueue_s* const item = (RenderBoxQueue_s*)queue->GetCurrentCallItem();
    item->function(item->fTransformMatrix, item->vMins, item->vMaxs, item->color, item->material, item->bInsideOut);

    // Advance the queue.
    queue->currentCallIndex += sizeof(RenderBoxQueue_s);
}

//-----------------------------------------------------------------------------
// Purpose: renders a solid box:
// +z              _+y
// ^               /|
// |              /
// |  +----------+
// | /::::::::::/|
//  /::::::::::/:|
// +----------+::|
// |::::::::::|::+
// |::::::::::|:/
// |::::::::::|/
// +----------+ --> +x
//-----------------------------------------------------------------------------
static void RenderBoxInternal(const matrix3x4_t& fTransformMatrix, const Vector3D& vMins, const Vector3D& vMaxs, const Color c, IMaterial* const pMaterial, const bool bInsideOut)
{
    InitializeStandardMaterials();

    if ((*g_fnHasRenderCallQueue)())
    {
        CallQueue_s* const queue = (*g_fnAddRenderCallQueueItem)(RenderBoxQueueFunctor, sizeof(RenderBoxQueue_s), 7);
        RenderBoxQueue_s* const item = (RenderBoxQueue_s*)queue->GetCurrentAllocatedItem();

        item->function = RenderBoxInternal;
        item->fTransformMatrix = fTransformMatrix;
        item->vMins = vMins;
        item->vMaxs = vMaxs;
        item->color = c;
        item->material = pMaterial;
        item->bInsideOut = bInsideOut;

        (*g_fnAdvanceRenderCallQueue)(sizeof(RenderBoxQueue_s));
        return;
    }

    CMatRenderContext* const ctx = g_pMaterialSystem->GetRenderContext();
    CMeshVertexBuilder vertexBuilder;

    if (vertexBuilder.Begin(ctx, 36))
    {
        ctx->Bind(pMaterial);

        Vector3D p[8];
        GenerateBoxVertices(fTransformMatrix, vMins, vMaxs, p);

        // Draw the box
        for (int i = 0; i < 6; i++)
        {
            const int* const ppFaceIndices = bInsideOut ? s_boxFaceIndicesInsideOut[i] : s_boxFaceIndices[i];
            for (int j = 1; j < 3; ++j)
            {
                const int i0 = ppFaceIndices[0];
                const int i1 = ppFaceIndices[j];
                const int i2 = ppFaceIndices[j + 1];

                vertexBuilder.AppendVertex(p[i0], c);
                vertexBuilder.AppendVertex(p[i2], c);
                vertexBuilder.AppendVertex(p[i1], c);
            }
        }

        vertexBuilder.End(ctx);
        ctx->DrawTriangleList(vertexBuilder.GetParams(), nullptr, 0);
    }

    // Need to call this to decrement context ref counter.
    ctx->EndRenderer();
}

struct RenderWireframeBoxQueue_s
{
    void (*function)(const matrix3x4_t& fTransformMatrix, const Vector3D& vMins, const Vector3D& vMaxs, const Color c, IMaterial* const pMaterial);
    matrix3x4_t fTransformMatrix;
    Vector3D vMins;
    Vector3D vMaxs;
    Color color;
    IMaterial* material;
};

//-----------------------------------------------------------------------------
// Purpose: process and advance wireframe box render queue
//-----------------------------------------------------------------------------
static void RenderWireframeBoxQueueFunctor(CallQueue_s* const queue)
{
    RenderWireframeBoxQueue_s* const item = (RenderWireframeBoxQueue_s*)queue->GetCurrentCallItem();
    item->function(item->fTransformMatrix, item->vMins, item->vMaxs, item->color, item->material);

    // Advance the queue.
    queue->currentCallIndex += sizeof(RenderWireframeBoxQueue_s);
}

//-----------------------------------------------------------------------------
// Purpose: renders a wireframe box:
// +z              _+y
// ^               /|
// |              /
// |  +----------+
// | /|         /|
//  / |        / |
// +----------+  |
// |  +-------|--+
// | /        | /
// |/         |/
// +----------+ --> +x
//-----------------------------------------------------------------------------
static void RenderWireframeBoxInternal(const matrix3x4_t& fTransformMatrix, const Vector3D& vMins, const Vector3D& vMaxs, const Color c, IMaterial* const pMaterial)
{
    InitializeStandardMaterials();

    if ((*g_fnHasRenderCallQueue)())
    {
        CallQueue_s* const queue = (*g_fnAddRenderCallQueueItem)(RenderWireframeBoxQueueFunctor, sizeof(RenderWireframeBoxQueue_s), 7);
        RenderWireframeBoxQueue_s* const item = (RenderWireframeBoxQueue_s*)queue->GetCurrentAllocatedItem();

        item->function = RenderWireframeBoxInternal;
        item->fTransformMatrix = fTransformMatrix;
        item->vMins = vMins;
        item->vMaxs = vMaxs;
        item->color = c;
        item->material = pMaterial;

        (*g_fnAdvanceRenderCallQueue)(sizeof(RenderWireframeBoxQueue_s));
        return;
    }

    CMatRenderContext* const ctx = g_pMaterialSystem->GetRenderContext();
    CMeshVertexBuilder vertexBuilder;

    if (vertexBuilder.Begin(ctx, 48))
    {
        ctx->Bind(pMaterial);

        Vector3D p[8];
        GenerateBoxVertices(fTransformMatrix, vMins, vMaxs, p);

        // Draw the box
        for (int i = 0; i < 6; i++)
        {
            const int* const ppFaceIndices = s_boxFaceIndices[i];

            for (int j = 0; j < 4; ++j)
            {
                vertexBuilder.AppendVertex(p[ppFaceIndices[j]], c);
                vertexBuilder.AppendVertex(p[ppFaceIndices[(j == 3) ? 0 : j + 1]], c);
            }
        }

        vertexBuilder.End(ctx);
        ctx->DrawLineList(vertexBuilder.GetParams(), nullptr, 0);
    }

    // Need to call this to decrement context ref counter.
    ctx->EndRenderer();
}

//-----------------------------------------------------------------------------
// Purpose: appends axes to the provided mesh
//-----------------------------------------------------------------------------
static void AppendAxes(const Vector3D& origin, Vector3D* const pts, const int idx, const Color c, CMeshVertexBuilder& vertexBuilder)
{
    Vector3D start, temp;
    VectorAdd(pts[idx], origin, start);

    vertexBuilder.AppendVertex(start, c);

    int endidx = (idx & 0x1) ? idx - 1 : idx + 1;
    VectorAdd(pts[endidx], origin, temp);

    vertexBuilder.AppendVertex(temp, c);
    vertexBuilder.AppendVertex(start, c);

    endidx = (idx & 0x2) ? idx - 2 : idx + 2;
    VectorAdd(pts[endidx], origin, temp);
    vertexBuilder.AppendVertex(temp, c);
    vertexBuilder.AppendVertex(start, c);

    endidx = (idx & 0x4) ? idx - 4 : idx + 4;
    VectorAdd(pts[endidx], origin, temp);

    vertexBuilder.AppendVertex(temp, c);
}

//-----------------------------------------------------------------------------
// Purpose: appends extrusion faces to the provided mesh
//-----------------------------------------------------------------------------
static void AppendExtrusionFace(const Vector3D& start, const Vector3D& end,
    Vector3D* const pts, const int idx1, const int idx2, const Color c, CMeshVertexBuilder& vertexBuilder)
{
    Vector3D temp;
    VectorAdd(pts[idx1], start, temp);
    vertexBuilder.AppendVertex(temp, c);

    VectorAdd(pts[idx2], start, temp);

    vertexBuilder.AppendVertex(temp, c);
    vertexBuilder.AppendVertex(temp, c);

    VectorAdd(pts[idx2], end, temp);

    vertexBuilder.AppendVertex(temp, c);
    vertexBuilder.AppendVertex(temp, c);

    VectorAdd(pts[idx1], end, temp);

    vertexBuilder.AppendVertex(temp, c);
    vertexBuilder.AppendVertex(temp, c);

    VectorAdd(pts[idx1], start, temp);
    vertexBuilder.AppendVertex(temp, c);
}

struct RenderSweptBoxQueue_s
{
    void (*function)(const Vector3D& vStart, const Vector3D& vEnd, const QAngle& angles,
        const Vector3D& vMins, const Vector3D& vMaxs, const Color c, IMaterial* const pMaterial);
    Vector3D vStart;
    Vector3D vEnd;
    QAngle angles;
    Vector3D vMins;
    Vector3D vMaxs;
    Color color;
    IMaterial* pMaterial;
};

//-----------------------------------------------------------------------------
// Purpose: process and advance swept box render queue
//-----------------------------------------------------------------------------
static void RenderSweptBoxQueueFunctor(CallQueue_s* const queue)
{
    RenderSweptBoxQueue_s* const item = (RenderSweptBoxQueue_s*)queue->GetCurrentCallItem();
    item->function(item->vStart, item->vEnd, item->angles, item->vMins, item->vMaxs, item->color, item->pMaterial);

    // Advance the queue.
    queue->currentCallIndex += sizeof(RenderSweptBoxQueue_s);
}

//-----------------------------------------------------------------------------
// Purpose: renders an extruded box:
//            +----------+ --> vStart
//           /|         /|
//          / |        / |
//         +----------+  |
//        /|  +------/|--+
//       / | /      / | /
//      /  |/      /  |/
//     /   +------/---+
//    /   /      /   /
//   /   /      /   /
//  +----------+ --/--> angles
// |   /      |   /
// |  +-------|--+
// | /|       | /|
// |/ |       |/ |
// +----------+  |
// |  +-------|--+
// | /        | /
// |/         |/
// +----------+ --> vEnd
//-----------------------------------------------------------------------------
static void RenderWireframeSweptBoxInternal(const Vector3D& vStart, const Vector3D& vEnd,
    const QAngle& angles, const Vector3D& vMins, const Vector3D& vMaxs, const Color c, IMaterial* const pMaterial)
{
    InitializeStandardMaterials();

    // Queue it off if this is called outside the render thread.
    if ((*g_fnHasRenderCallQueue)())
    {
        CallQueue_s* const queue = (*g_fnAddRenderCallQueueItem)(RenderSweptBoxQueueFunctor, sizeof(RenderSweptBoxQueue_s), 7);
        RenderSweptBoxQueue_s* const item = (RenderSweptBoxQueue_s*)queue->GetCurrentAllocatedItem();

        item->function = RenderWireframeSweptBoxInternal;
        item->vStart = vStart;
        item->vEnd = vEnd;
        item->angles = angles;
        item->vMins = vMins;
        item->vMaxs = vMaxs;
        item->color = c;
        item->pMaterial = pMaterial;

        (*g_fnAdvanceRenderCallQueue)(sizeof(RenderSweptBoxQueue_s));
        return;
    }

    CMatRenderContext* const ctx = g_pMaterialSystem->GetRenderContext();
    CMeshVertexBuilder vertexBuilder;

    if (vertexBuilder.Begin(ctx, 60))
    {
        ctx->Bind(pMaterial);

        // Build a rotation matrix from angles
        matrix3x4_t fRotateMatrix;
        AngleMatrix(angles, fRotateMatrix);

        Vector3D vDelta;
        VectorSubtract(vEnd, vStart, vDelta);

        // Compute the box points, rotated but without the origin added
        Vector3D temp;
        Vector3D pts[8];
        float dot[8];
        int minidx = 0;
        for (int i = 0; i < 8; ++i)
        {
            temp.x = (i & 0x1) ? vMaxs[0] : vMins[0];
            temp.y = (i & 0x2) ? vMaxs[1] : vMins[1];
            temp.z = (i & 0x4) ? vMaxs[2] : vMins[2];

            // Rotate the corner point
            VectorRotate(temp, fRotateMatrix, pts[i]);

            // Find the dot product with dir
            dot[i] = DotProduct(pts[i], vDelta);
            if (dot[i] < dot[minidx])
            {
                minidx = i;
            }
        }

        // Choose opposite corner
        const int maxidx = minidx ^ 0x7;

        // Draw the start + end axes...
        AppendAxes(vStart, pts, minidx, c, vertexBuilder);
        AppendAxes(vEnd, pts, maxidx, c, vertexBuilder);

        // Draw the extrusion faces
        for (int j = 0; j < 3; ++j)
        {
            const int dirflag1 = (1 << ((j + 1) % 3));
            const int dirflag2 = (1 << ((j + 2) % 3));

            const int idx1 = (minidx & dirflag1) ? minidx - dirflag1 : minidx + dirflag1;
            const int idx2 = (minidx & dirflag2) ? minidx - dirflag2 : minidx + dirflag2;
            const int idx3 = (minidx & dirflag2) ? idx1 - dirflag2 : idx1 + dirflag2;

            AppendExtrusionFace(vStart, vEnd, pts, idx1, idx3, c, vertexBuilder);
            AppendExtrusionFace(vStart, vEnd, pts, idx2, idx3, c, vertexBuilder);
        }

        vertexBuilder.End(ctx);
        ctx->DrawLineList(vertexBuilder.GetParams(), nullptr, 0);
    }

    // Need to call this to decrement context ref counter.
    ctx->EndRenderer();
}

struct RenderTriangleQueue_s
{
    void (*function)(const Vector3D& p1, const Vector3D& p2, const Vector3D& p3, const Color c, IMaterial* const pMaterial);
    Vector3D p1;
    Vector3D p2;
    Vector3D p3;
    Color color;
    IMaterial* material;
};

//-----------------------------------------------------------------------------
// Purpose: process and advance triangle render queue
//-----------------------------------------------------------------------------
static void RenderTriangleQueueFunctor(CallQueue_s* const queue)
{
    RenderTriangleQueue_s* const item = (RenderTriangleQueue_s*)queue->GetCurrentCallItem();
    item->function(item->p1, item->p2, item->p3, item->color, item->material);

    // Advance the queue.
    queue->currentCallIndex += sizeof(RenderTriangleQueue_s);
}

//-----------------------------------------------------------------------------
// Purpose: renders a triangle:
// +z              _+y
// |               /|
// |      /\      /
// |     /::\    /
// |    /::::\  /
// |   /::::::\
// |  /::::::::\
// | /::::::::::\
//  /::::::::::::\
// '--------------' --> +x
//-----------------------------------------------------------------------------
static void RenderTriangleInternal(const Vector3D& p1, const Vector3D& p2, const Vector3D& p3, const Color c, IMaterial* const pMaterial)
{
    InitializeStandardMaterials();

    // Queue it off if this is called outside the render thread.
    if ((*g_fnHasRenderCallQueue)())
    {
        CallQueue_s* const queue = (*g_fnAddRenderCallQueueItem)(RenderTriangleQueueFunctor, sizeof(RenderTriangleQueue_s), 7);
        RenderTriangleQueue_s* const item = (RenderTriangleQueue_s*)queue->GetCurrentAllocatedItem();

        item->function = RenderTriangleInternal;
        item->p1 = p1;
        item->p2 = p2;
        item->p3 = p3;
        item->color = c;
        item->material = pMaterial;

        (*g_fnAdvanceRenderCallQueue)(sizeof(RenderTriangleQueue_s));
        return;
    }

    CMatRenderContext* const ctx = g_pMaterialSystem->GetRenderContext();
    CMeshVertexBuilder vertexBuilder;

    if (vertexBuilder.Begin(ctx, 3))
    {
        ctx->Bind(pMaterial);

        vertexBuilder.AppendVertex(p3, c);
        vertexBuilder.AppendVertex(p2, c);
        vertexBuilder.AppendVertex(p1, c);

        vertexBuilder.End(ctx);
        ctx->DrawTriangleList(vertexBuilder.GetParams(), nullptr, 0);
    }

    // Need to call this to decrement context ref counter.
    ctx->EndRenderer();
}

struct RenderSphereQueue_s
{
    void (*function)(const Vector3D& vCenter, const float flRadius, const int nTheta,
        const int nPhi, const Color c, IMaterial* const pMaterial);
    Vector3D vCenter;
    float flRadius;
    int nTheta;
    int nPhi;
    Color color;
    IMaterial* pMaterial;
};

//-----------------------------------------------------------------------------
// Purpose: process and advance swept box render queue
//-----------------------------------------------------------------------------
static void RenderSphereQueueFunctor(CallQueue_s* const queue)
{
    RenderSphereQueue_s* const item = (RenderSphereQueue_s*)queue->GetCurrentCallItem();
    item->function(item->vCenter, item->flRadius, item->nTheta, item->nPhi, item->color, item->pMaterial);

    // Advance the queue.
    queue->currentCallIndex += sizeof(RenderSphereQueue_s);
}

//-----------------------------------------------------------------------------
// Purpose: render a sphere:
// +z                _+y
// ^                 /|
// |                /
// |   .--"|"--.   /
//  .'     |     '.
// /       |       \
// | <----( )---->-|--> +r
// \       |       /
//  '.     |     .'
//    "-.._|_..-"   --> +x
//-----------------------------------------------------------------------------
static void RenderSphereInternal(const Vector3D& vCenter, const float flRadius, const int nTheta,
    const int nPhi, const Color c, IMaterial* const pMaterial)
{
    InitializeStandardMaterials();

    // Queue it off if this is called outside the render thread.
    if ((*g_fnHasRenderCallQueue)())
    {
        CallQueue_s* const queue = (*g_fnAddRenderCallQueueItem)(RenderSphereQueueFunctor, sizeof(RenderSphereQueue_s), 7);
        RenderSphereQueue_s* const item = (RenderSphereQueue_s*)queue->GetCurrentAllocatedItem();

        item->function = RenderSphereInternal;
        item->vCenter = vCenter;
        item->flRadius = flRadius;
        item->nTheta = nTheta;
        item->nPhi = nPhi;
        item->color = c;
        item->pMaterial = pMaterial;

        (*g_fnAdvanceRenderCallQueue)(sizeof(RenderSphereQueue_s));
        return;
    }

    CMatRenderContext* const ctx = g_pMaterialSystem->GetRenderContext();
    CMeshVertexBuilder vertexBuilder;

    if (vertexBuilder.Begin(ctx, nPhi * (nTheta + 1)))
    {
        ctx->Bind(pMaterial);

        for (int i = 0; i < nPhi; ++i)
        {
            const float phi = (i / (float)(nPhi - 1)) * (float)M_PI;
            const float cs = cos(phi) * flRadius;
            const float sn = sin(phi) * flRadius;

            for (int j = 0; j < (nTheta + 1); ++j)
            {
                const float u = j / (float)nTheta;
                const float theta = 2.0f * (float)M_PI * u;

                Vector3D vecPos;

                vecPos.x = (cos(theta) * sn) + vCenter.x;
                vecPos.y = (sin(theta) * sn) + vCenter.y;
                vecPos.z = cs + vCenter.z;

                vertexBuilder.AppendVertex(vecPos, c);
            }
        }

        vertexBuilder.End(ctx);
        CMeshIndexBuilder indexBuilder;

        if (indexBuilder.Begin(ctx, nTheta * (6 * nPhi - 6)))
        {
            // Emit the triangle strips.
            for (int i = 0; i < nPhi - 1; ++i)
            {
                for (int j = 0; j < nTheta; j++)
                {
                    const int curr = (nTheta + 1) * i + j;

                    indexBuilder.AppendIndex((u16)(curr));
                    indexBuilder.AppendIndex((u16)(curr + 1));
                    indexBuilder.AppendIndex((u16)(curr + (nTheta + 1) + 1));
                    indexBuilder.AppendIndex((u16)(curr));
                    indexBuilder.AppendIndex((u16)(curr + (nTheta + 1) + 1));
                    indexBuilder.AppendIndex((u16)(curr + (nTheta + 1)));
                }
            }

            indexBuilder.End(ctx);
            ctx->DrawTriangleListIndexed(vertexBuilder.GetParams(), indexBuilder.GetParams(), 0);
        }
    }

    // Need to call this to decrement context ref counter.
    ctx->EndRenderer();
}

//-----------------------------------------------------------------------------
// purpose: capsule vertices
//-----------------------------------------------------------------------------
#define CAPSULE_VERTS 74
#define CAPSULE_LINES 69 // note(kawe): make this 117 and uncomment the indices
                         // below if you want more detailed hemi-spheres on the
                         // capsule ends. The single-divided hemi-sphere turned
                         // out to look good enough while dropping 84 vertices.
static const Vector3D g_capsuleVertPositions[CAPSULE_VERTS] = {
    { -0.01f, -0.01f, 1.0f },	{ 0.51f, 0.0f, 0.86f },		{ 0.44f, 0.25f, 0.86f },	{ 0.25f, 0.44f, 0.86f },	{ -0.01f, 0.51f, 0.86f },	{ -0.26f, 0.44f, 0.86f },	{ -0.45f, 0.25f, 0.86f },	{ -0.51f, 0.0f, 0.86f },	{ -0.45f, -0.26f, 0.86f },
    { -0.26f, -0.45f, 0.86f },	{ -0.01f, -0.51f, 0.86f },	{ 0.25f, -0.45f, 0.86f },	{ 0.44f, -0.26f, 0.86f },	{ 0.86f, 0.0f, 0.51f },		{ 0.75f, 0.43f, 0.51f },	{ 0.43f, 0.75f, 0.51f },	{ -0.01f, 0.86f, 0.51f },	{ -0.44f, 0.75f, 0.51f },
    { -0.76f, 0.43f, 0.51f },	{ -0.87f, 0.0f, 0.51f },	{ -0.76f, -0.44f, 0.51f },	{ -0.44f, -0.76f, 0.51f },	{ -0.01f, -0.87f, 0.51f },	{ 0.43f, -0.76f, 0.51f },	{ 0.75f, -0.44f, 0.51f },	{ 1.0f, 0.0f, 0.01f },		{ 0.86f, 0.5f, 0.01f },
    { 0.49f, 0.86f, 0.01f },	{ -0.01f, 1.0f, 0.01f },	{ -0.51f, 0.86f, 0.01f },	{ -0.87f, 0.5f, 0.01f },	{ -1.0f, 0.0f, 0.01f },		{ -0.87f, -0.5f, 0.01f },	{ -0.51f, -0.87f, 0.01f },	{ -0.01f, -1.0f, 0.01f },	{ 0.49f, -0.87f, 0.01f },
    { 0.86f, -0.51f, 0.01f },	{ 1.0f, 0.0f, -0.02f },		{ 0.86f, 0.5f, -0.02f },	{ 0.49f, 0.86f, -0.02f },	{ -0.01f, 1.0f, -0.02f },	{ -0.51f, 0.86f, -0.02f },	{ -0.87f, 0.5f, -0.02f },	{ -1.0f, 0.0f, -0.02f },	{ -0.87f, -0.5f, -0.02f },
    { -0.51f, -0.87f, -0.02f },	{ -0.01f, -1.0f, -0.02f },	{ 0.49f, -0.87f, -0.02f },	{ 0.86f, -0.51f, -0.02f },	{ 0.86f, 0.0f, -0.51f },	{ 0.75f, 0.43f, -0.51f },	{ 0.43f, 0.75f, -0.51f },	{ -0.01f, 0.86f, -0.51f },	{ -0.44f, 0.75f, -0.51f },
    { -0.76f, 0.43f, -0.51f },	{ -0.87f, 0.0f, -0.51f },	{ -0.76f, -0.44f, -0.51f },	{ -0.44f, -0.76f, -0.51f },	{ -0.01f, -0.87f, -0.51f },	{ 0.43f, -0.76f, -0.51f },	{ 0.75f, -0.44f, -0.51f },	{ 0.51f, 0.0f, -0.87f },	{ 0.44f, 0.25f, -0.87f },
    { 0.25f, 0.44f, -0.87f },	{ -0.01f, 0.51f, -0.87f },	{ -0.26f, 0.44f, -0.87f },	{ -0.45f, 0.25f, -0.87f },	{ -0.51f, 0.0f, -0.87f },	{ -0.45f, -0.26f, -0.87f },	{ -0.26f, -0.45f, -0.87f },	{ -0.01f, -0.51f, -0.87f },	{ 0.25f, -0.45f, -0.87f },
    { 0.44f, -0.26f, -0.87f },	{ 0.0f, 0.0f, -1.0f },
};
static const int g_capsuleLineIndices[CAPSULE_LINES] = { -1,
    14,		0,	1,	13,	25,	37,	49,	61,	73,	67,	55,	43,	31,	19,	7,		-1,
    14,		0,	4,	16,	28,	40,	52,	64,	73,	70,	58,	46,	34,	22,	10,		-1,
    12,		25,	26,	27,	28,	29,	30,	31,	32,	33,	34,	35,	36,				-1,
    12,		37,	38,	39,	40,	41,	42,	43,	44,	45,	46,	47,	48,				-1,
//    12,		13,	14,	15,	16,	17,	18,	19,	20,	21,	22,	23,	24,				-1,
//    12,		49,	50,	51,	52,	53,	54,	55,	56,	57,	58,	59,	60,				-1,
//    12,		1,	2,	3,	4,	5,	6,	7,	8,	9,	10,	11,	12,				-1,
//    12,		61,	62,	63,	64,	65,	66,	67,	68,	69,	70,	71,	72,				-1,
};

struct RenderCapsuleQueue_s
{
    void (*function)(const Vector3D& vStart, const Vector3D& vEnd, const float flRadius, const Color c, IMaterial* const pMaterial);
    Vector3D vStart;
    Vector3D vEnd;
    float flRadius;
    Color color;
    IMaterial* material;
};

//-----------------------------------------------------------------------------
// Purpose: process and advance capsule render queue
//-----------------------------------------------------------------------------
static void RenderCapsuleQueueFunctor(CallQueue_s* const queue)
{
    RenderCapsuleQueue_s* const item = (RenderCapsuleQueue_s*)queue->GetCurrentCallItem();
    item->function(item->vStart, item->vEnd, item->flRadius, item->color, item->material);

    // Advance the queue.
    queue->currentCallIndex += sizeof(RenderCapsuleQueue_s);
}

//-----------------------------------------------------------------------------
// Purpose: render a capsule:
// +z           _+y
// ^            /|
// |           /
// |.-'"|"'-. /
// |----|----|
// |    |    |
// |    |    |
// | <--+--> |--> +r
// |    |    |
// |    |    |
// |----|----|
//  "-..|..-" --> +x
//-----------------------------------------------------------------------------
static void RenderCapsuleInternal(const Vector3D& vStart, const Vector3D& vEnd, const float flRadius, const Color c, IMaterial* const pMaterial)
{
    InitializeStandardMaterials();

    // Queue it off if this is called outside the render thread.
    if ((*g_fnHasRenderCallQueue)())
    {
        CallQueue_s* const queue = (*g_fnAddRenderCallQueueItem)(RenderCapsuleQueueFunctor, sizeof(RenderCapsuleQueue_s), 7);
        RenderCapsuleQueue_s* const item = (RenderCapsuleQueue_s*)queue->GetCurrentAllocatedItem();

        item->function = RenderCapsuleInternal;
        item->vStart = vStart;
        item->vEnd = vEnd;
        item->flRadius = flRadius;
        item->color = c;
        item->material = pMaterial;

        (*g_fnAdvanceRenderCallQueue)(sizeof(RenderCapsuleQueue_s));
        return;
    }

    const Vector3D vecCapsuleCoreNormal = (vStart - vEnd).Normalized();

    matrix3x4_t matCapsuleRotationSpace;
    VectorMatrix(Vector3D(0, 0, 1), matCapsuleRotationSpace);

    matrix3x4_t matCapsuleSpace;
    VectorMatrix(vecCapsuleCoreNormal, matCapsuleSpace);

    const Vector3D vecLen = (vEnd - vStart);
    Vector3D v[CAPSULE_VERTS];

    for (int i = 0; i < CAPSULE_VERTS; i++)
    {
        Vector3D vecCapsuleVert = g_capsuleVertPositions[i];

        VectorRotate(vecCapsuleVert, matCapsuleRotationSpace, vecCapsuleVert);
        VectorRotate(vecCapsuleVert, matCapsuleSpace, vecCapsuleVert);

        vecCapsuleVert *= flRadius;

        if (g_capsuleVertPositions[i].z > 0)
        {
            vecCapsuleVert += vecLen;
        }

        v[i] = vecCapsuleVert + vStart;
    }

    CMatRenderContext* const ctx = g_pMaterialSystem->GetRenderContext();
    CMeshVertexBuilder vertexBuilder;

    // note(kawe): see comment at the 'CAPSULE_LINES' define,
    // if you wish to have more detailed hemi-spheres on the
    // capsule ends, allocate 200 vertices here instead of
    // 116 after increasing 'CAPSULE_LINES'.
    if (vertexBuilder.Begin(ctx, 116))
    {
        ctx->Bind(pMaterial);
        int loopStartIndex = -1; // Track where each loop starts.

        for (int i = 0; i < CAPSULE_LINES; i++)
        {
            if (g_capsuleLineIndices[i] == -1)
            {
                if (loopStartIndex != -1)
                {
                    // Close the loop properly.
                    const int lastIndex = g_capsuleLineIndices[i - 1];
                    const int firstIndex = g_capsuleLineIndices[loopStartIndex];

                    if (lastIndex >= 0 && firstIndex >= 0)
                    {
                        vertexBuilder.AppendVertex(v[lastIndex], c);
                        vertexBuilder.AppendVertex(v[firstIndex], c);
                    }
                }

                // Prepare for the next loop.
                if (++i >= CAPSULE_LINES)
                    break;

                loopStartIndex = i + 1;
                continue;
            }

            if (i + 1 < CAPSULE_LINES && g_capsuleLineIndices[i + 1] != -1)
            {
                const int idx1 = g_capsuleLineIndices[i];
                const int idx2 = g_capsuleLineIndices[i + 1];

                if (idx1 >= 0 && idx2 >= 0)
                {
                    vertexBuilder.AppendVertex(v[idx1], c);
                    vertexBuilder.AppendVertex(v[idx2], c);
                }
            }
        }

        vertexBuilder.End(ctx);
        ctx->DrawLineList(vertexBuilder.GetParams(), nullptr, 0);
    }

    // Need to call this to decrement context ref counter.
    ctx->EndRenderer();
}

static inline IMaterial* DetermineWireframeMaterial(const Color c, const bool bZBuffer)
{
    if (c.a() == 255)
        return bZBuffer ? s_opaqueNormalZWire : s_opaqueIgnoreZWire;
    else
        return bZBuffer ? s_transNormalZWire : s_transIgnoreZWire;
}

static inline IMaterial* DetermineFaceMaterial(const Color c, const bool bZBuffer)
{
    if (c.a() == 255)
        return bZBuffer ? s_opaqueNormalZFront : s_opaqueIgnoreZFront;
    else
        return bZBuffer ? s_transNormalZFront : s_transIgnoreZFront;
}

static inline IMaterial* DetermineTriangleMaterial(Color& c, const bool bZBuffer)
{
    if (c.a() == 0)
    {
        c[3] = 255;
        return bZBuffer ? s_opaqueNormalZWire : s_opaqueIgnoreZWire;
    }
    else
    {
        if (c.a() == 255)
            return bZBuffer ? s_opaqueNormalZBoth : s_opaqueIgnoreZBoth;
        else
            return bZBuffer ? s_transNormalZBoth : s_transIgnoreZBoth;
    }
}

//-----------------------------------------------------------------------------
// Purpose: public proxy for RenderLineInternal
//-----------------------------------------------------------------------------
void RenderLine(const Vector3D& v1, const Vector3D& v2, const Color color, const bool bZBuffer)
{
    IMaterial* const pMaterial = DetermineFaceMaterial(color, bZBuffer);
    RenderLineInternal(v1, v2, color, pMaterial);
}

//-----------------------------------------------------------------------------
// Purpose: public proxies for RenderBoxInternal
//-----------------------------------------------------------------------------
void RenderBox(const matrix3x4_t& vTransforms, const Vector3D& vMins, const Vector3D& vMaxs, const Color c, const bool bZBuffer)
{
    IMaterial* const pMaterial = DetermineFaceMaterial(c, bZBuffer);
    RenderBoxInternal(vTransforms, vMins, vMaxs, c, pMaterial, false);
}
void RenderWireframeBox(const matrix3x4_t& vTransforms, const Vector3D& vMins, const Vector3D& vMaxs, const Color c, const bool bZBuffer)
{
    IMaterial* const pMaterial = DetermineWireframeMaterial(c, bZBuffer);
    RenderWireframeBoxInternal(vTransforms, vMins, vMaxs, c, pMaterial);
}

//-----------------------------------------------------------------------------
// Purpose: public proxy for RenderWireframeSweptBoxInternal
//-----------------------------------------------------------------------------
void RenderWireframeSweptBox(const Vector3D& vStart, const Vector3D& vEnd, const QAngle& angles,
    const Vector3D& vMins, const Vector3D& vMaxs, const Color c, const bool bZBuffer)
{
    IMaterial* const pMaterial = DetermineWireframeMaterial(c, bZBuffer);
    RenderWireframeSweptBoxInternal(vStart, vEnd, angles, vMins, vMaxs, c, pMaterial);
}

//-----------------------------------------------------------------------------
// Purpose: public proxy for RenderTriangleInternal
//-----------------------------------------------------------------------------
void RenderTriangle(const Vector3D& p1, const Vector3D& p2, const Vector3D& p3, Color c, const bool bZBuffer)
{
    IMaterial* const pMaterial = DetermineTriangleMaterial(c, bZBuffer);
    RenderTriangleInternal(p1, p2, p3, c, pMaterial);
}

//-----------------------------------------------------------------------------
// Purpose: public proxies for RenderSphereInternal
//-----------------------------------------------------------------------------
void RenderSphere(const Vector3D& vCenter, const float flRadius, const int nTheta, const int nPhi, const Color c, const bool bZBuffer)
{
    IMaterial* const pMaterial = DetermineFaceMaterial(c, bZBuffer);
    RenderSphereInternal(vCenter, flRadius, nTheta, nPhi, c, pMaterial);
}
void RenderWireframeSphere(const Vector3D& vCenter, const float flRadius, const int nTheta, const int nPhi, const Color c, const bool bZBuffer)
{
    IMaterial* const pMaterial = DetermineWireframeMaterial(c, bZBuffer);
    RenderSphereInternal(vCenter, flRadius, nTheta, nPhi, c, pMaterial);
}

//-----------------------------------------------------------------------------
// Purpose: public proxy for RenderCapsuleInternal
//-----------------------------------------------------------------------------
void RenderCapsule(const Vector3D& vStart, const Vector3D& vEnd, const float flRadius, const Color c, const bool bZBuffer)
{
    IMaterial* const pMaterial = DetermineWireframeMaterial(c, bZBuffer);
    RenderCapsuleInternal(vStart, vEnd, flRadius, c, pMaterial);
}

///////////////////////////////////////////////////////////////////////////////
// Below a set of helper functions for shapes utilizing the render code above
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
// Purpose: render angled box:
// +z              _+y
// ^               /|
// |              /
// |  +----------+
// | /|         /|
//  / |        / |
// +----------+  |
// |  +-------|--+
// | /        | /
// |/         |/
// +----------+ --> +x
//-----------------------------------------------------------------------------
void DebugDrawBox(const Vector3D& vOrigin, const QAngle& vAngles, const Vector3D& vMins, const Vector3D& vMaxs, const Color color, const bool bZBuffer)
{
    Vector3D vPoints[8];
    PointsFromAngledBox(vAngles, vMins, vMaxs, &*vPoints);

    RenderLine(vOrigin + vPoints[0], vOrigin + vPoints[1], color, bZBuffer);
    RenderLine(vOrigin + vPoints[1], vOrigin + vPoints[2], color, bZBuffer);
    RenderLine(vOrigin + vPoints[2], vOrigin + vPoints[3], color, bZBuffer);
    RenderLine(vOrigin + vPoints[3], vOrigin + vPoints[0], color, bZBuffer);

    RenderLine(vOrigin + vPoints[4], vOrigin + vPoints[5], color, bZBuffer);
    RenderLine(vOrigin + vPoints[5], vOrigin + vPoints[6], color, bZBuffer);
    RenderLine(vOrigin + vPoints[6], vOrigin + vPoints[7], color, bZBuffer);
    RenderLine(vOrigin + vPoints[7], vOrigin + vPoints[4], color, bZBuffer);

    RenderLine(vOrigin + vPoints[0], vOrigin + vPoints[4], color, bZBuffer);
    RenderLine(vOrigin + vPoints[1], vOrigin + vPoints[5], color, bZBuffer);
    RenderLine(vOrigin + vPoints[2], vOrigin + vPoints[6], color, bZBuffer);
    RenderLine(vOrigin + vPoints[3], vOrigin + vPoints[7], color, bZBuffer);
}

//-----------------------------------------------------------------------------
// Purpose: render cylinder:
// +z           _+y
// ^            /|
// |           /
// |.-'"|"'-. /
// (----|----)
// |'-._|_.-'|
// |    |    |
// |    |    |
// | <--+--> |--> +r
// |    |    |
// |    |    |
//  "-._|_.-" --> +x
//-----------------------------------------------------------------------------
void DebugDrawCylinder(const Vector3D& vOrigin, const QAngle& vAngles, const float flRadius, const float flHeight, const Color color, const int nSides, const bool bZBuffer)
{
    const float flDegrees = 360.f / float(nSides);

    QAngle vComposed;
    Vector3D vForward;
    CUtlVector<Vector3D> vecPoints(0, nSides);

    AngleVectors(vAngles, &vForward);

    for (int i = 0; i < nSides; i++)
    {
        Vector3D right;

        AngleCompose(vAngles, { 0.f, 0.f, flDegrees * i }, vComposed);
        AngleVectors(vComposed, nullptr, &right, nullptr);
        vecPoints.AddToTail(vOrigin + (right * flRadius));
    }

    for (int i = 0; i < nSides; i++)
    {
        const Vector3D& vStart = vecPoints[i];
        const Vector3D& vEnd = i == 0 ? vecPoints[nSides - 1] : vecPoints[i - 1];

        RenderLine(vStart, vEnd, color, bZBuffer);
        RenderLine(vStart + (vForward * flHeight), vEnd + (vForward * flHeight), color, bZBuffer);
        RenderLine(vStart, vStart + (vForward * flHeight), color, bZBuffer);
    }
}

//-----------------------------------------------------------------------------
// Purpose: render sphere:
// +z                _+y
// ^                 /|
// |                /
// |   .--"|"--.   /
//  .'     |     '.
// /       |       \
// | <----( )---->-|--> +r
// \       |       /
//  '.     |     .'
//    "-.._|_..-"   --> +x
//-----------------------------------------------------------------------------
void DebugDrawSphere(const Vector3D& vOrigin, const float flRadius, const Color color, const int nSegments, const bool bZBuffer)
{
    DebugDrawCircle(vOrigin, { 90.f, 0.f, 0.f }, flRadius, color, nSegments, bZBuffer);
    DebugDrawCircle(vOrigin, { 0.f, 90.f, 0.f }, flRadius, color, nSegments, bZBuffer);
    DebugDrawCircle(vOrigin, { 0.f, 0.f, 90.f }, flRadius, color, nSegments, bZBuffer);
}

//-----------------------------------------------------------------------------
// Purpose: render hemisphere:
// +z                _+y
// ^                 /|
// |                /
// |   .--"|"--.   /
//  .'     |     '.
// /       |       \ /--> +r
// | <----( )---->-|/ --> +x
//-----------------------------------------------------------------------------
void DebugDrawHemiSphere(const Vector3D& vOrigin, const QAngle& vAngles, const Vector3D& vRadius, const Color color, const int nSegments, const bool bZBuffer)
{
    const float flDegrees = 360.0f / float(nSegments * 2);
    bool bFirstLoop = true;

    Vector3D vStart[4], vEnd[4], vForward[4];
    QAngle vComposed[4];

    for (int i = 0; i < (nSegments + 1); i++)
    {
        const float angleOffset = flDegrees * i;

        AngleCompose(vAngles, { angleOffset - 180, 0, 0 }, vComposed[0]);
        AngleCompose(vAngles, { 0, angleOffset - 90, 0 }, vComposed[1]);
        AngleCompose(vAngles, { angleOffset + 180, 90, 0 }, vComposed[2]);
        AngleCompose(vAngles, { 0, angleOffset + 90, 0 }, vComposed[3]);

        AngleVectors(vComposed[0], &vForward[0]);
        AngleVectors(vComposed[1], &vForward[1]);
        AngleVectors(vComposed[2], &vForward[2]);
        AngleVectors(vComposed[3], &vForward[3]);

        vEnd[0] = vOrigin + (vForward[0] * vRadius);
        vEnd[1] = vOrigin + (vForward[1] * vRadius);
        vEnd[2] = vOrigin + (vForward[2] * vRadius);
        vEnd[3] = vOrigin + (vForward[3] * vRadius);

        if (!bFirstLoop)
        {
            RenderLine(vStart[0], vEnd[0], color, bZBuffer);
            RenderLine(vStart[1], vEnd[1], color, bZBuffer);
            RenderLine(vStart[2], vEnd[2], color, bZBuffer);
            RenderLine(vStart[3], vEnd[3], color, bZBuffer);
        }

        bFirstLoop = false;

        vStart[0] = vEnd[0];
        vStart[1] = vEnd[1];
        vStart[2] = vEnd[2];
        vStart[3] = vEnd[3];
    }
}

//-----------------------------------------------------------------------------
// Purpose: render circle:
// +z                _+y
// ^                 /|
// |                /
// |   .--"""--.   /
//  .'           '.
// /               \
// | <----( )---->-|--> +r
// \               /
//  '.           .'
//    "-..___..-"   --> +x
//-----------------------------------------------------------------------------
void DebugDrawCircle(const Vector3D& vOrigin, const QAngle& vAngles, const float flRadius, const Color color, const int nSegments, const bool bZBuffer)
{
    const float flDegrees = 360.f / float(nSegments);

    Vector3D vStart, vEnd, vFirstend, vForward;
    QAngle vComposed;

    bool bFirstLoop = true;

    for (int i = 0; i < nSegments; i++)
    {
        AngleCompose(vAngles, { 0.f, flDegrees * i, 0.f }, vComposed);
        AngleVectors(vComposed, &vForward);
        vEnd = vOrigin + (vForward * flRadius);

        if (bFirstLoop)
            vFirstend = vEnd;

        if (!bFirstLoop)
            RenderLine(vStart, vEnd, color, bZBuffer);

        vStart = vEnd;

        bFirstLoop = false;
    }

    RenderLine(vEnd, vFirstend, color, bZBuffer);
}

//-----------------------------------------------------------------------------
// Purpose: render square:
// +z              _+y
// |               /|
// .--------------.
// |              |
// |              |
// |              |
// |              |
// |              |
// |              |
// '--------------' --> +x
//-----------------------------------------------------------------------------
void DebugDrawSquare(const Vector3D& vOrigin, const QAngle& vAngles, const float flSquareSize, const Color color, const bool bZBuffer)
{
    DebugDrawCircle(vOrigin, vAngles, flSquareSize, color, 4, bZBuffer);
}

//-----------------------------------------------------------------------------
// Purpose: render triangle:
// +z              _+y
// |               /|
// |      /\      /
// |     /  \    /
// |    /    \  /
// |   /      \
// |  /        \
// | /          \
//  /            \
// '--------------' --> +x
//-----------------------------------------------------------------------------
void DebugDrawTriangle(const Vector3D& vOrigin, const QAngle& vAngles, const float flTriangleSize, const Color color, const bool bZBuffer)
{
    DebugDrawCircle(vOrigin, vAngles, flTriangleSize, color, 3, bZBuffer);
}

//-----------------------------------------------------------------------------
// Purpose: render mark:
// +z     _+y
// |      /|
// |     /
//   \  /--> +r
// ___\/___
//    /\
//   /  \
//  /    --> +x
//-----------------------------------------------------------------------------
void DebugDrawMark(const Vector3D& vOrigin, float flRadius, const Color c, const bool bZBuffer)
{
    RenderLine((vOrigin - Vector3D{ flRadius, 0.f, 0.f }), (vOrigin + Vector3D{ flRadius, 0.f, 0.f }), c, bZBuffer);
    RenderLine((vOrigin - Vector3D{ 0.f, flRadius, 0.f }), (vOrigin + Vector3D{ 0.f, flRadius, 0.f }), c, bZBuffer);
    RenderLine((vOrigin - Vector3D{ 0.f, 0.f, flRadius }), (vOrigin + Vector3D{ 0.f, 0.f, flRadius }), c, bZBuffer);
}

//-----------------------------------------------------------------------------
// Purpose: render star:
// +z     _+y
// |      /|
// |     /
//   \  /--> +r
// ___\/___
//    /\
//   /  \
//       --> +x
//-----------------------------------------------------------------------------
void DrawStar(const Vector3D& vOrigin, const float flRadius, const bool bZBuffer)
{
    Vector3D vForward;
    for (int i = 0; i < 50; i++)
    {
        AngleVectors({ RandomFloat(0.f, 360.f), RandomFloat(0.f, 360.f), RandomFloat(0.f, 360.f) }, &vForward);
        RenderLine(vOrigin, vOrigin + vForward * flRadius, Color(RandomInt(0, 255), RandomInt(0, 255), RandomInt(0, 255), 255), bZBuffer);
    }
}

//-----------------------------------------------------------------------------
// Purpose: render arrow:
// +z     _+y
// |      /|
// |  .  /
// | / \
//  /   \
// /_____\ --> r
//    |
//    |
//    |   --> +x
//-----------------------------------------------------------------------------
void DebugDrawArrow(const Vector3D& vOrigin, const Vector3D& vEnd, const float flArraySize, const Color color, const bool bZBuffer)
{
    Vector3D vAngles;

    RenderLine(vOrigin, vEnd, color, bZBuffer);
    AngleVectors(Vector3D(vEnd - vOrigin).Normalized().AsQAngle(), &vAngles);
    DebugDrawCircle(vEnd, vAngles.AsQAngle(), flArraySize, color, 3, bZBuffer);
}

//-----------------------------------------------------------------------------
// Purpose: render 3d axis:
// +z
// ^
// |   _+y
// |   /|
// |  /
// | /
// |/
// +----------> +x
//-----------------------------------------------------------------------------
void DebugDrawAxis(const Vector3D& vOrigin, const QAngle& vAngles, const float flScale, const bool bZBuffer)
{
    Vector3D vForward, vRight, vUp;
    AngleVectors(vAngles, &vForward, &vRight, &vUp);

    RenderLine(vOrigin, vOrigin + vForward * flScale, Color(0, 255, 0, 255), bZBuffer);
    RenderLine(vOrigin, vOrigin + vUp * flScale, Color(255, 0, 0, 255), bZBuffer);
    RenderLine(vOrigin, vOrigin + vRight * flScale, Color(0, 0, 255, 255), bZBuffer);
}

void V_RenderUtils::Detour(const bool bAttach) const
{
    DetourSetup(&v_InitializeStandardMaterials, &InitializeStandardMaterials, bAttach);
}
