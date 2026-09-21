//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushBoundary.cpp
//  Purpose: Implements derived boundary reconstruction for plane-defined
//           brushes.
//  Details: Reconstruction delegates the heavy numerical work to the
//           existing Brushd_BuildVertices and Brushd_BuildFacePolygon
//           functions in CypherMath. This layer adds policy validation,
//           indexed topology extraction, and edge deduplication.
//
//           The reconstruction is intentionally allocation-heavy relative
//           to the math layer: it allocates temporary arrays for raw
//           vertex candidates and per-face polygons, then builds the final
//           indexed boundary in the persistent output. All temporaries use
//           the same allocator as the boundary itself.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushBoundary.h"

// #include "CypherGeometry_Kernel_Classification.h"

using namespace cypher::math;

namespace cypher::editor::geometry
{

namespace
{

using cypher::common::bool_t;

bool_t IsInitialized( const brush_boundary_t *pBoundary ) noexcept
{
    return pBoundary != nullptr &&
           pBoundary->vertices.pAllocator != nullptr;
}

// Finds the index in the unique vertex array whose position is within
// mergeTolerance of the query point. Returns false if no match exists,
// which means the face polygon produced a vertex that the global vertex
// pass did not — a consistency failure.
bool_t FindVertexIndex(
    const math::vec3d_t *pVertices,
    common::usize cVertices,
    math::vec3d_t query,
    math::f64 mergeTolerance,
    common::u32 *pIndexOut ) noexcept
{
    const math::f64 toleranceSq = mergeTolerance * mergeTolerance;
    for ( common::usize i = 0u; i < cVertices; ++i ) {
        if ( math::Vec3d_DistanceSquared( pVertices[i], query ) <= toleranceSq ) {
            *pIndexOut = static_cast<common::u32>( i );
            return true;
        }
    }
    return false;
}

bool_t IsWithinMagnitudeLimit( math::f64 value, math::f64 limit ) noexcept
{
    return math::Scalar_Abs( value ) <= limit;
}

// Inserts an edge into the edge array if it does not already exist.
// Edges are stored with iVertex0 < iVertex1 for canonical ordering, so
// the same edge discovered from two adjacent faces is deduplicated.
void InsertUniqueEdge(
    common::vector_t<brush_boundary_edge_t> *pEdges,
    common::u32 iA,
    common::u32 iB ) noexcept
{
    // Canonical ordering: smaller index first.
    const common::u32 iLow = ( iA < iB ) ? iA : iB;
    const common::u32 iHigh = ( iA < iB ) ? iB : iA;

    // Linear scan is fine for authoring-scale brushes (max 256 sides →
    // at most a few hundred edges). A hash set would be justified only
    // if this ever becomes a measured bottleneck.
    const common::usize cEdges = common::Vector_Count( pEdges );
    for ( common::usize i = 0u; i < cEdges; ++i ) {
        if ( pEdges->pData[i].iVertex0 == iLow &&
             pEdges->pData[i].iVertex1 == iHigh ) {
            return;
        }
    }
    const brush_boundary_edge_t edge{ iLow, iHigh };
    ( void )common::Vector_PushBack( pEdges, edge );
}

} // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

geometry_status_t BrushBoundary_Init(
    brush_boundary_t *pBoundary,
    const common::allocator_t *pAllocator ) noexcept
{
    if ( pBoundary == nullptr || pAllocator == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pBoundary->vertices.pAllocator != nullptr ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }

    // All four arrays must succeed or none are left half-initialized.
    if ( !common::Vector_Init( &pBoundary->vertices, pAllocator, 0u ) ||
         !common::Vector_Init( &pBoundary->edges, pAllocator, 0u ) ||
         !common::Vector_Init( &pBoundary->faces, pAllocator, 0u ) ||
         !common::Vector_Init( &pBoundary->faceVertexIndices, pAllocator, 0u ) ) {
        // Partial init cleanup — Shutdown is safe on zero-initialized vectors.
        BrushBoundary_Shutdown( pBoundary );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    return geometry_status_t::OK;
}

void BrushBoundary_Shutdown( brush_boundary_t *pBoundary ) noexcept
{
    if ( pBoundary == nullptr ) {
        return;
    }
    if ( pBoundary->faceVertexIndices.pAllocator != nullptr ) {
        common::Vector_Shutdown( &pBoundary->faceVertexIndices );
    }
    if ( pBoundary->faces.pAllocator != nullptr ) {
        common::Vector_Shutdown( &pBoundary->faces );
    }
    if ( pBoundary->edges.pAllocator != nullptr ) {
        common::Vector_Shutdown( &pBoundary->edges );
    }
    if ( pBoundary->vertices.pAllocator != nullptr ) {
        common::Vector_Shutdown( &pBoundary->vertices );
    }
}

// ---------------------------------------------------------------------------
// Reconstruction
// ---------------------------------------------------------------------------

geometry_status_t BrushBoundary_TryReconstruct(
    brush_boundary_t *pBoundary,
    const brush_solid_t *pBrush,
    const geometry_policy_t &policy ) noexcept
{
    if ( !IsInitialized( pBoundary ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( pBrush == nullptr || pBrush->sides.pAllocator == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    // Clear previous boundary data. The allocations stay so a repeated
    // reconstruct after a plane drag does not re-allocate from zero.
    common::Vector_Clear( &pBoundary->vertices );
    common::Vector_Clear( &pBoundary->edges );
    common::Vector_Clear( &pBoundary->faces );
    common::Vector_Clear( &pBoundary->faceVertexIndices );

    const common::usize cSides = common::Vector_Count( &pBrush->sides );

    // A tetrahedron is the minimum closed convex solid — fewer than 4
    // planes cannot bound a finite volume.
    if ( cSides < 4u ) {
        return geometry_status_t::DEGENERATE;
    }

    // ---- Step 1: Extract and validate planes ---------------------------

    // Stack-allocate for small brushes, fall back to the boundary's
    // allocator for large ones. 256 is cBrushSidesPerBrushMax.
    constexpr common::usize cStackPlanes = 256u;
    math::planed_t stackPlanes[cStackPlanes];
    math::planed_t *pPlanes = stackPlanes;

    common::vector_t<math::planed_t> heapPlanes{};
    if ( cSides > cStackPlanes ) {
        if ( !common::Vector_Init( &heapPlanes,
                 pBoundary->vertices.pAllocator, cSides ) ) {
            return geometry_status_t::ALLOCATION_FAILED;
        }
        pPlanes = heapPlanes.pData;
    }

    for ( common::usize i = 0u; i < cSides; ++i ) {
        const math::planed_t &plane = pBrush->sides.pData[i].plane;

        if ( !math::Planed_IsFinite( plane ) ) {
            if ( heapPlanes.pAllocator != nullptr ) {
                common::Vector_Shutdown( &heapPlanes );
            }
            return geometry_status_t::NUMERIC_FAILURE;
        }
        if ( !math::Planed_IsNormalized(
                 plane, policy.numerical.fUnitNormalTolerance ) ) {
            if ( heapPlanes.pAllocator != nullptr ) {
                common::Vector_Shutdown( &heapPlanes );
            }
            return geometry_status_t::DEGENERATE;
        }
        if ( !IsWithinMagnitudeLimit(
                 plane.d, policy.numerical.fCoordinateMagnitudeLimit ) ) {
            if ( heapPlanes.pAllocator != nullptr ) {
                common::Vector_Shutdown( &heapPlanes );
            }
            return geometry_status_t::LIMIT_EXCEEDED;
        }

        pPlanes[i] = plane;
    }

    // ---- Step 2: Build unique vertices from plane triples ---------------

    const common::usize cMaxCandidates =
        math::Brush_MaximumVertexCandidates( cSides );

    // Temporary buffer for raw vertex positions from the math layer.
    common::vector_t<math::vec3d_t> rawVertices{};
    if ( !common::Vector_Init( &rawVertices,
             pBoundary->vertices.pAllocator, cMaxCandidates ) ) {
        if ( heapPlanes.pAllocator != nullptr ) {
            common::Vector_Shutdown( &heapPlanes );
        }
        return geometry_status_t::ALLOCATION_FAILED;
    }
    // Ensure the buffer is large enough for the math layer to write into.
    if ( !common::Vector_Resize( &rawVertices, cMaxCandidates ) ) {
        common::Vector_Shutdown( &rawVertices );
        if ( heapPlanes.pAllocator != nullptr ) {
            common::Vector_Shutdown( &heapPlanes );
        }
        return geometry_status_t::ALLOCATION_FAILED;
    }

    // The minimum determinant threshold controls how nearly-parallel a
    // plane triple can be before we reject its intersection. Using the
    // absolute distance tolerance keeps this consistent with the policy.
    const math::f64 minAbsDet = policy.numerical.fAbsoluteDistanceTolerance;
    const math::f64 insideTol = policy.numerical.fCoplanarDistanceTolerance;
    const math::f64 mergeTol = policy.numerical.fWeldDistance;

    const math::brush_vertex_result_t vertexResult = math::Brushd_BuildVertices(
        pPlanes, cSides,
        minAbsDet, insideTol, mergeTol,
        rawVertices.pData, cMaxCandidates );

    if ( heapPlanes.pAllocator != nullptr ) {
        common::Vector_Shutdown( &heapPlanes );
    }

    if ( vertexResult.status != math::brush_build_status_t::OK ) {
        common::Vector_Shutdown( &rawVertices );
        return geometry_status_t::DEGENERATE;
    }

    // A valid convex polyhedron needs at least 4 vertices (tetrahedron).
    if ( vertexResult.cVerticesWritten < 4u ) {
        common::Vector_Shutdown( &rawVertices );
        return geometry_status_t::DEGENERATE;
    }

    // Copy unique vertices into the boundary's persistent storage.
    if ( !common::Vector_Reserve(
             &pBoundary->vertices, vertexResult.cVerticesWritten ) ) {
        common::Vector_Shutdown( &rawVertices );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( common::usize i = 0u; i < vertexResult.cVerticesWritten; ++i ) {
        ( void )common::Vector_PushBack( &pBoundary->vertices, rawVertices.pData[i] );
    }

    // ---- Step 3: Build face polygons per side ---------------------------

    // Temporary buffer for one face polygon at a time. The maximum number
    // of vertices a single face can have equals the total vertex count.
    const common::usize cBoundaryVerts = vertexResult.cVerticesWritten;
    common::vector_t<math::vec3d_t> faceVerts{};
    if ( !common::Vector_Init( &faceVerts,
             pBoundary->vertices.pAllocator, cBoundaryVerts ) ) {
        common::Vector_Shutdown( &rawVertices );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    if ( !common::Vector_Resize( &faceVerts, cBoundaryVerts ) ) {
        common::Vector_Shutdown( &faceVerts );
        common::Vector_Shutdown( &rawVertices );
        return geometry_status_t::ALLOCATION_FAILED;
    }

    for ( common::usize iSide = 0u; iSide < cSides; ++iSide ) {
        const math::planed_t sidePlane = pBrush->sides.pData[iSide].plane;

        const math::brush_vertex_result_t faceResult =
            math::Brushd_BuildFacePolygon(
                sidePlane,
                rawVertices.pData, cBoundaryVerts,
                insideTol,
                policy.numerical.fAbsoluteDistanceTolerance,
                faceVerts.pData, cBoundaryVerts );

        // A side with fewer than 3 vertices does not form a polygon. For
        // a valid convex brush, every side should contribute a face; a
        // missing face means the plane is redundant or the brush is open.
        if ( faceResult.status != math::brush_build_status_t::OK ||
             faceResult.cVerticesWritten < 3u ) {
            continue;
        }

        // Map raw face positions back to unique vertex indices.
        const common::u32 iFirstIndex = static_cast<common::u32>(
            common::Vector_Count( &pBoundary->faceVertexIndices ) );
        const common::u32 cFaceVerts =
            static_cast<common::u32>( faceResult.cVerticesWritten );

        bool bMappingFailed = false;
        for ( common::u32 iv = 0u; iv < cFaceVerts; ++iv ) {
            common::u32 iMapped = 0u;
            if ( !FindVertexIndex(
                     pBoundary->vertices.pData, cBoundaryVerts,
                     faceVerts.pData[iv], mergeTol, &iMapped ) ) {
                bMappingFailed = true;
                break;
            }
            ( void )common::Vector_PushBack( &pBoundary->faceVertexIndices, iMapped );
        }

        if ( bMappingFailed ) {
            // Roll back the indices we just appended for this face.
            while ( common::Vector_Count( &pBoundary->faceVertexIndices ) >
                    iFirstIndex ) {
                common::Vector_PopBack( &pBoundary->faceVertexIndices );
            }
            continue;
        }

        // Record the face.
        const brush_boundary_face_t face{
            static_cast<common::u32>( iSide ),
            iFirstIndex,
            cFaceVerts
        };
        ( void )common::Vector_PushBack( &pBoundary->faces, face );

        // ---- Step 4: Extract edges from consecutive vertex pairs --------

        // Each consecutive pair in the CCW ring is an edge. The edge
        // wraps around: the last vertex connects back to the first.
        for ( common::u32 iv = 0u; iv < cFaceVerts; ++iv ) {
            const common::u32 iCurr =
                pBoundary->faceVertexIndices.pData[iFirstIndex + iv];
            const common::u32 iNext =
                pBoundary->faceVertexIndices.pData[
                    iFirstIndex + ( ( iv + 1u ) % cFaceVerts )];
            InsertUniqueEdge( &pBoundary->edges, iCurr, iNext );
        }
    }

    common::Vector_Shutdown( &faceVerts );
    common::Vector_Shutdown( &rawVertices );

    // ---- Step 5: Final consistency check --------------------------------

    // Every side of a valid convex brush must contribute exactly one face.
    if ( common::Vector_Count( &pBoundary->faces ) < 4u ) {
        common::Vector_Clear( &pBoundary->vertices );
        common::Vector_Clear( &pBoundary->edges );
        common::Vector_Clear( &pBoundary->faces );
        common::Vector_Clear( &pBoundary->faceVertexIndices );
        return geometry_status_t::DEGENERATE;
    }

    return geometry_status_t::OK;
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

common::usize BrushBoundary_VertexCount(
    const brush_boundary_t *pBoundary ) noexcept
{
    if ( !IsInitialized( pBoundary ) ) {
        return 0u;
    }
    return common::Vector_Count( &pBoundary->vertices );
}

common::usize BrushBoundary_EdgeCount(
    const brush_boundary_t *pBoundary ) noexcept
{
    if ( !IsInitialized( pBoundary ) ) {
        return 0u;
    }
    return common::Vector_Count( &pBoundary->edges );
}

common::usize BrushBoundary_FaceCount(
    const brush_boundary_t *pBoundary ) noexcept
{
    if ( !IsInitialized( pBoundary ) ) {
        return 0u;
    }
    return common::Vector_Count( &pBoundary->faces );
}

geometry_status_t BrushBoundary_TryGetFaceVertexIndices(
    const brush_boundary_t *pBoundary,
    common::usize iFace,
    CY_OUT_WRITES( nOutputCapacity ) common::u32 *pIndicesOut,
    common::usize nOutputCapacity,
    common::usize *pCountOut ) noexcept
{
    if ( pCountOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pCountOut = 0u;

    if ( !IsInitialized( pBoundary ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( iFace >= common::Vector_Count( &pBoundary->faces ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    const brush_boundary_face_t &face = pBoundary->faces.pData[iFace];
    *pCountOut = face.cVertices;

    if ( pIndicesOut == nullptr || nOutputCapacity < face.cVertices ) {
        return geometry_status_t::INSUFFICIENT_CAPACITY;
    }

    for ( common::u32 i = 0u; i < face.cVertices; ++i ) {
        pIndicesOut[i] =
            pBoundary->faceVertexIndices.pData[face.iFirstIndex + i];
    }
    return geometry_status_t::OK;
}

geometry_status_t BrushBoundary_TryGetFaceNormal(
    const brush_boundary_t *pBoundary,
    const brush_solid_t *pBrush,
    common::usize iFace,
    math::vec3d_t *pNormalOut ) noexcept
{
    if ( pNormalOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pNormalOut = math::CY_VEC3D_ZERO;

    if ( !IsInitialized( pBoundary ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( pBrush == nullptr || pBrush->sides.pAllocator == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( iFace >= common::Vector_Count( &pBoundary->faces ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    const common::u32 iSide = pBoundary->faces.pData[iFace].iSide;
    if ( iSide >= common::Vector_Count( &pBrush->sides ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    *pNormalOut = pBrush->sides.pData[iSide].plane.normal;
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
