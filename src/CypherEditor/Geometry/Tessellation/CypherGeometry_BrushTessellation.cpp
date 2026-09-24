//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushTessellation.cpp
//  Purpose: Implements fan triangulation of convex brush boundary faces.
//  Details: Each face polygon is a convex ring with CCW winding. Fan
//           triangulation from vertex 0 is exact for convex polygons and
//           preserves winding order. A quad face produces 2 triangles;
//           a triangle face produces 1. Every emitted triangle carries
//           the source side index for provenance.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushTessellation.h"

#include <limits>

namespace cypher::editor::geometry
{

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

geometry_status_t BrushTessellation_Init(
    brush_tessellation_t *pTessellation,
    const common::allocator_t *pAllocator ) noexcept
{
    if ( pTessellation == nullptr || pAllocator == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pTessellation->triangles.pAllocator != nullptr ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }

    if ( !common::Vector_Init(
             &pTessellation->triangles, pAllocator, 0u ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }

    pTessellation->cVertices = 0u;
    return geometry_status_t::OK;
}

void BrushTessellation_Shutdown(
    brush_tessellation_t *pTessellation ) noexcept
{
    if ( pTessellation == nullptr ) {
        return;
    }
    common::Vector_Shutdown( &pTessellation->triangles );
    pTessellation->cVertices = 0u;
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

geometry_status_t BrushTessellation_TryBuild(
    brush_tessellation_t *pTessellation,
    const brush_boundary_t *pBoundary ) noexcept
{
    if ( pTessellation == nullptr || pBoundary == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pTessellation->triangles.pAllocator == nullptr ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( pBoundary->vertices.pAllocator == nullptr ||
         pBoundary->edges.pAllocator == nullptr ||
         pBoundary->faces.pAllocator == nullptr ||
         pBoundary->faceVertexIndices.pAllocator == nullptr ) {
        return geometry_status_t::NOT_INITIALIZED;
    }

    // A failed rebuild must never expose triangles from an older boundary.
    common::Vector_Clear( &pTessellation->triangles );
    pTessellation->cVertices = 0u;

    const common::usize cFaces = BrushBoundary_FaceCount( pBoundary );
    const common::usize cVerts = BrushBoundary_VertexCount( pBoundary );
    if ( cFaces == 0u || cVerts < 4u ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    // Validate every packed face range and vertex reference before reading a
    // ring or publishing a triangle. Boundary is a derived cache, but callers
    // still need a checked sink rather than an out-of-bounds read if that cache
    // is stale or corrupted.
    common::usize cTotalTriangles = 0u;
    for ( common::usize iFace = 0u; iFace < cFaces; ++iFace ) {
        const brush_boundary_face_t &face = pBoundary->faces.pData[iFace];
        if ( face.cVertices < 3u ) {
            return geometry_status_t::CORRUPT_STATE;
        }

        const common::usize iFirst = face.iFirstIndex;
        const common::usize cFaceVertices = face.cVertices;
        const common::usize cPacked =
            pBoundary->faceVertexIndices.nCount;
        if ( iFirst > cPacked || cFaceVertices > cPacked - iFirst ) {
            return geometry_status_t::CORRUPT_STATE;
        }

        for ( common::usize i = 0u; i < cFaceVertices; ++i ) {
            if ( pBoundary->faceVertexIndices.pData[iFirst + i] >=
                 cVerts ) {
                return geometry_status_t::CORRUPT_STATE;
            }
        }

        const common::usize cFaceTriangles = cFaceVertices - 2u;
        if ( cTotalTriangles >
             std::numeric_limits<common::usize>::max() -
                 cFaceTriangles ) {
            return geometry_status_t::LIMIT_EXCEEDED;
        }
        cTotalTriangles += cFaceTriangles;
    }

    if ( !common::Vector_Reserve(
             &pTessellation->triangles, cTotalTriangles ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }

    // Fan-triangulate each face. For a convex polygon with vertices
    // [v0, v1, v2, ..., vN-1], the fan produces triangles:
    //   (v0, v1, v2), (v0, v2, v3), ..., (v0, vN-2, vN-1)
    for ( common::usize iFace = 0u; iFace < cFaces; ++iFace ) {
        const brush_boundary_face_t &face = pBoundary->faces.pData[iFace];
        if ( face.cVertices < 3u ) {
            continue;
        }

        const common::u32 *pIndices =
            &pBoundary->faceVertexIndices.pData[face.iFirstIndex];

        for ( common::u32 j = 1u; j < face.cVertices - 1u; ++j ) {
            brush_tessellation_triangle_t tri{};
            tri.iVertex0 = pIndices[0];
            tri.iVertex1 = pIndices[j];
            tri.iVertex2 = pIndices[j + 1u];
            tri.iSourceSide = face.iSide;

            if ( !common::Vector_PushBack(
                     &pTessellation->triangles, tri ) ) {
                common::Vector_Clear( &pTessellation->triangles );
                return geometry_status_t::ALLOCATION_FAILED;
            }
        }
    }

    pTessellation->cVertices = cVerts;
    return geometry_status_t::OK;
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

common::usize BrushTessellation_TriangleCount(
    const brush_tessellation_t *pTessellation ) noexcept
{
    if ( pTessellation == nullptr ||
         pTessellation->triangles.pAllocator == nullptr ) {
        return 0u;
    }
    return common::Vector_Count( &pTessellation->triangles );
}

common::usize BrushTessellation_VertexCount(
    const brush_tessellation_t *pTessellation ) noexcept
{
    if ( pTessellation == nullptr ||
         pTessellation->triangles.pAllocator == nullptr ) {
        return 0u;
    }
    return pTessellation->cVertices;
}

geometry_status_t BrushTessellation_TryGetTriangle(
    const brush_tessellation_t *pTessellation,
    common::usize iTriangle,
    brush_tessellation_triangle_t *pTriangleOut ) noexcept
{
    if ( pTriangleOut != nullptr ) {
        *pTriangleOut = {};
    }
    if ( pTessellation == nullptr || pTriangleOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pTessellation->triangles.pAllocator == nullptr ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( iTriangle >= pTessellation->triangles.nCount ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    *pTriangleOut = pTessellation->triangles.pData[iTriangle];
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
