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
    if ( pBoundary == nullptr ||
         pBoundary->vertices.pAllocator == nullptr ) {
        return false;
    }
    const common::allocator_t *pAllocator =
        pBoundary->vertices.pAllocator;
    return pBoundary->edges.pAllocator == pAllocator &&
           pBoundary->faces.pAllocator == pAllocator &&
           pBoundary->faceVertexIndices.pAllocator == pAllocator &&
           common::Vector_IsValid( &pBoundary->vertices ) &&
           common::Vector_IsValid( &pBoundary->edges ) &&
           common::Vector_IsValid( &pBoundary->faces ) &&
           common::Vector_IsValid( &pBoundary->faceVertexIndices );
}

template <typename type_t>
bool_t VectorIsCanonicalEmpty(
    const common::vector_t<type_t> &vector ) noexcept
{
    return vector.pData == nullptr && vector.nCount == 0u &&
           vector.nCapacity == 0u && vector.pAllocator == nullptr;
}

bool_t BoundaryIsCanonicalEmpty(
    const brush_boundary_t &boundary ) noexcept
{
    return VectorIsCanonicalEmpty( boundary.vertices ) &&
           VectorIsCanonicalEmpty( boundary.edges ) &&
           VectorIsCanonicalEmpty( boundary.faces ) &&
           VectorIsCanonicalEmpty( boundary.faceVertexIndices );
}

void ClearBoundaryData( brush_boundary_t *pBoundary ) noexcept
{
    common::Vector_Clear( &pBoundary->vertices );
    common::Vector_Clear( &pBoundary->edges );
    common::Vector_Clear( &pBoundary->faces );
    common::Vector_Clear( &pBoundary->faceVertexIndices );
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

bool_t PlanesAreEquivalent(
    math::planed_t a,
    math::planed_t b,
    const geometry_numerical_policy_t &numerical ) noexcept
{
    return math::Scalar_Abs( a.normal.x - b.normal.x ) <=
               numerical.fUnitNormalTolerance &&
           math::Scalar_Abs( a.normal.y - b.normal.y ) <=
               numerical.fUnitNormalTolerance &&
           math::Scalar_Abs( a.normal.z - b.normal.z ) <=
               numerical.fUnitNormalTolerance &&
           math::Scalar_Abs( a.d - b.d ) <=
               numerical.fCoplanarDistanceTolerance;
}

// Inserts an edge into the edge array if it does not already exist.
// Edges are stored with iVertex0 < iVertex1 for canonical ordering, so
// the same edge discovered from two adjacent faces is deduplicated.
bool_t TryInsertUniqueEdge(
    common::vector_t<brush_boundary_edge_t> *pEdges,
    common::u32 iA,
    common::u32 iB ) noexcept
{
    if ( iA == iB ) {
        return false;
    }
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
            return true;
        }
    }
    const brush_boundary_edge_t edge{ iLow, iHigh };
    return common::Vector_PushBack( pEdges, edge );
}

bool_t BoundaryTopologyIsClosed(
    const brush_boundary_t &boundary ) noexcept
{
    const common::usize cVertices = boundary.vertices.nCount;
    const common::usize cEdges = boundary.edges.nCount;
    const common::usize cFaces = boundary.faces.nCount;
    if ( cVertices < 4u || cEdges < 6u || cFaces < 4u ) {
        return false;
    }
    if ( cVertices > static_cast<common::usize>( common::CY_I64_MAX ) ||
         cEdges > static_cast<common::usize>( common::CY_I64_MAX ) ||
         cFaces > static_cast<common::usize>( common::CY_I64_MAX ) ) {
        return false;
    }

    const common::i64 euler =
        static_cast<common::i64>( cVertices ) -
        static_cast<common::i64>( cEdges ) +
        static_cast<common::i64>( cFaces );
    if ( euler != 2 ) {
        return false;
    }

    for ( common::usize iEdge = 0u; iEdge < cEdges; ++iEdge ) {
        const brush_boundary_edge_t &edge = boundary.edges.pData[iEdge];
        common::usize cUses = 0u;
        for ( common::usize iFace = 0u; iFace < cFaces; ++iFace ) {
            const brush_boundary_face_t &face =
                boundary.faces.pData[iFace];
            const common::usize iFirst = face.iFirstIndex;
            const common::usize cFaceVertices = face.cVertices;
            if ( cFaceVertices < 3u ||
                 iFirst > boundary.faceVertexIndices.nCount ||
                 cFaceVertices >
                     boundary.faceVertexIndices.nCount - iFirst ) {
                return false;
            }
            for ( common::usize iVertex = 0u;
                  iVertex < cFaceVertices;
                  ++iVertex ) {
                const common::u32 iA =
                    boundary.faceVertexIndices.pData[iFirst + iVertex];
                const common::u32 iB = boundary.faceVertexIndices.pData[
                    iFirst + ( ( iVertex + 1u ) % cFaceVertices )];
                const common::u32 iLow = iA < iB ? iA : iB;
                const common::u32 iHigh = iA < iB ? iB : iA;
                if ( edge.iVertex0 == iLow && edge.iVertex1 == iHigh ) {
                    ++cUses;
                }
            }
        }
        if ( cUses != 2u ) {
            return false;
        }
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

geometry_status_t BrushBoundary_Init(
    brush_boundary_t *pBoundary,
    const common::allocator_t *pAllocator ) noexcept
{
    if ( pBoundary == nullptr || pAllocator == nullptr ||
         !common::Allocator_IsValid( pAllocator ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( IsInitialized( pBoundary ) ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }
    if ( !BoundaryIsCanonicalEmpty( *pBoundary ) ) {
        return geometry_status_t::CORRUPT_STATE;
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

geometry_status_t TryReconstructBoundary(
    brush_boundary_t *pBoundary,
    const brush_solid_t *pBrush,
    const geometry_policy_t &policy,
    bool_t bAllowRedundantSides ) noexcept
{
    if ( !IsInitialized( pBoundary ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }

    // Once the destination is known to be initialized, every return path
    // must satisfy the public failure contract: no previous or partial
    // boundary data remains visible.
    ClearBoundaryData( pBoundary );

    if ( pBrush == nullptr || pBrush->sides.pAllocator == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !common::Vector_IsValid( &pBrush->sides ) ||
         !common::Allocator_IsValid( pBrush->sides.pAllocator ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    if ( !GeometryPolicy_IsValid( policy ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    const common::usize cSides = common::Vector_Count( &pBrush->sides );

    // A tetrahedron is the minimum closed convex solid — fewer than 4
    // planes cannot bound a finite volume.
    if ( cSides < 4u ) {
        return geometry_status_t::DEGENERATE;
    }
    if ( static_cast<common::u64>( cSides ) >
             policy.limits.cBrushSidesPerBrushMax ||
         cSides > static_cast<common::usize>( common::CY_U32_MAX ) ) {
        return geometry_status_t::LIMIT_EXCEEDED;
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
        if ( !common::Vector_Resize( &heapPlanes, cSides ) ) {
            common::Vector_Shutdown( &heapPlanes );
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

    // A bounded convex polyhedron with F faces has at most 2F-4 vertices.
    // Using C(F,3) as writable storage would reserve tens of megabytes even
    // for ordinary high-sided brushes and can overflow before policy is
    // consulted. The linear Euler bound is both sufficient and predictable.
    if ( cSides > ( common::CY_USIZE_MAX - 4u ) / 2u ) {
        if ( heapPlanes.pAllocator != nullptr ) {
            common::Vector_Shutdown( &heapPlanes );
        }
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    const common::usize cMaxCandidates = cSides * 2u - 4u;
    if ( static_cast<common::u64>( cMaxCandidates ) >
         policy.limits.cVerticesMax ) {
        if ( heapPlanes.pAllocator != nullptr ) {
            common::Vector_Shutdown( &heapPlanes );
        }
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    const common::u64 cbPlanes =
        static_cast<common::u64>( cSides ) * sizeof( math::planed_t );
    const common::u64 cbVertexBuffers =
        static_cast<common::u64>( cMaxCandidates ) *
        sizeof( math::vec3d_t ) * 2u;
    if ( cbPlanes > policy.limits.cbScratchMax ||
         cbVertexBuffers > policy.limits.cbScratchMax - cbPlanes ) {
        if ( heapPlanes.pAllocator != nullptr ) {
            common::Vector_Shutdown( &heapPlanes );
        }
        return geometry_status_t::LIMIT_EXCEEDED;
    }

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
        switch ( vertexResult.status ) {
        case math::brush_build_status_t::DEGENERATE:
            return geometry_status_t::DEGENERATE;
        case math::brush_build_status_t::INSUFFICIENT_CAPACITY:
            return geometry_status_t::INSUFFICIENT_CAPACITY;
        case math::brush_build_status_t::INVALID_ARGUMENT:
        case math::brush_build_status_t::COUNT:
        case math::brush_build_status_t::OK:
            return geometry_status_t::CORRUPT_STATE;
        }
        return geometry_status_t::CORRUPT_STATE;
    }

    // A valid convex polyhedron needs at least 4 vertices (tetrahedron).
    if ( vertexResult.cVerticesWritten < 4u ) {
        common::Vector_Shutdown( &rawVertices );
        return geometry_status_t::DEGENERATE;
    }
    if ( vertexResult.cVerticesWritten >
         static_cast<common::usize>( common::CY_U32_MAX ) ) {
        common::Vector_Shutdown( &rawVertices );
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    for ( common::usize i = 0u;
          i < vertexResult.cVerticesWritten;
          ++i ) {
        const math::vec3d_t &vertex = rawVertices.pData[i];
        if ( !math::Vec3d_IsFinite( vertex ) ) {
            common::Vector_Shutdown( &rawVertices );
            return geometry_status_t::NUMERIC_FAILURE;
        }
        if ( !IsWithinMagnitudeLimit(
                 vertex.x, policy.numerical.fCoordinateMagnitudeLimit ) ||
             !IsWithinMagnitudeLimit(
                 vertex.y, policy.numerical.fCoordinateMagnitudeLimit ) ||
             !IsWithinMagnitudeLimit(
                 vertex.z, policy.numerical.fCoordinateMagnitudeLimit ) ) {
            common::Vector_Shutdown( &rawVertices );
            return geometry_status_t::LIMIT_EXCEEDED;
        }
    }

    // Copy unique vertices into the boundary's persistent storage.
    if ( !common::Vector_Reserve(
             &pBoundary->vertices, vertexResult.cVerticesWritten ) ) {
        common::Vector_Shutdown( &rawVertices );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( common::usize i = 0u; i < vertexResult.cVerticesWritten; ++i ) {
        if ( !common::Vector_PushBack(
                 &pBoundary->vertices, rawVertices.pData[i] ) ) {
            common::Vector_Shutdown( &rawVertices );
            ClearBoundaryData( pBoundary );
            return geometry_status_t::ALLOCATION_FAILED;
        }
    }

    // ---- Step 3: Build face polygons per side ---------------------------

    // Temporary buffer for one face polygon at a time. The maximum number
    // of vertices a single face can have equals the total vertex count.
    const common::usize cBoundaryVerts = vertexResult.cVerticesWritten;
    common::vector_t<math::vec3d_t> faceVerts{};
    if ( !common::Vector_Init( &faceVerts,
             pBoundary->vertices.pAllocator, cBoundaryVerts ) ) {
        common::Vector_Shutdown( &rawVertices );
        ClearBoundaryData( pBoundary );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    if ( !common::Vector_Resize( &faceVerts, cBoundaryVerts ) ) {
        common::Vector_Shutdown( &faceVerts );
        common::Vector_Shutdown( &rawVertices );
        ClearBoundaryData( pBoundary );
        return geometry_status_t::ALLOCATION_FAILED;
    }

    for ( common::usize iSide = 0u; iSide < cSides; ++iSide ) {
        const math::planed_t sidePlane = pBrush->sides.pData[iSide].plane;

        if ( bAllowRedundantSides ) {
            bool_t bDuplicate = false;
            for ( common::usize iPrevious = 0u;
                  iPrevious < iSide;
                  ++iPrevious ) {
                if ( PlanesAreEquivalent(
                         sidePlane,
                         pBrush->sides.pData[iPrevious].plane,
                         policy.numerical ) ) {
                    bDuplicate = true;
                    break;
                }
            }
            if ( bDuplicate ) {
                continue;
            }
        }

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
            if ( bAllowRedundantSides &&
                 faceResult.status == math::brush_build_status_t::DEGENERATE ) {
                continue;
            }
            common::Vector_Shutdown( &faceVerts );
            common::Vector_Shutdown( &rawVertices );
            ClearBoundaryData( pBoundary );
            if ( faceResult.status ==
                 math::brush_build_status_t::INSUFFICIENT_CAPACITY ) {
                return geometry_status_t::INSUFFICIENT_CAPACITY;
            }
            return faceResult.status ==
                       math::brush_build_status_t::INVALID_ARGUMENT
                ? geometry_status_t::CORRUPT_STATE
                : geometry_status_t::DEGENERATE;
        }

        if ( faceResult.cVerticesWritten >
             static_cast<common::usize>( common::CY_U32_MAX ) ||
             common::Vector_Count( &pBoundary->faceVertexIndices ) >
                 static_cast<common::usize>( common::CY_U32_MAX ) -
                     faceResult.cVerticesWritten ) {
            common::Vector_Shutdown( &faceVerts );
            common::Vector_Shutdown( &rawVertices );
            ClearBoundaryData( pBoundary );
            return geometry_status_t::LIMIT_EXCEEDED;
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
            if ( !common::Vector_PushBack(
                     &pBoundary->faceVertexIndices, iMapped ) ) {
                common::Vector_Shutdown( &faceVerts );
                common::Vector_Shutdown( &rawVertices );
                ClearBoundaryData( pBoundary );
                return geometry_status_t::ALLOCATION_FAILED;
            }
        }

        if ( bMappingFailed ) {
            // Roll back the indices we just appended for this face.
            while ( common::Vector_Count( &pBoundary->faceVertexIndices ) >
                    iFirstIndex ) {
                common::Vector_PopBack( &pBoundary->faceVertexIndices );
            }
            common::Vector_Shutdown( &faceVerts );
            common::Vector_Shutdown( &rawVertices );
            ClearBoundaryData( pBoundary );
            return geometry_status_t::CORRUPT_STATE;
        }

        // Record the face.
        const brush_boundary_face_t face{
            static_cast<common::u32>( iSide ),
            iFirstIndex,
            cFaceVerts
        };
        if ( !common::Vector_PushBack( &pBoundary->faces, face ) ) {
            common::Vector_Shutdown( &faceVerts );
            common::Vector_Shutdown( &rawVertices );
            ClearBoundaryData( pBoundary );
            return geometry_status_t::ALLOCATION_FAILED;
        }

        // ---- Step 4: Extract edges from consecutive vertex pairs --------

        // Each consecutive pair in the CCW ring is an edge. The edge
        // wraps around: the last vertex connects back to the first.
        for ( common::u32 iv = 0u; iv < cFaceVerts; ++iv ) {
            const common::u32 iCurr =
                pBoundary->faceVertexIndices.pData[iFirstIndex + iv];
            const common::u32 iNext =
                pBoundary->faceVertexIndices.pData[
                    iFirstIndex + ( ( iv + 1u ) % cFaceVerts )];
            if ( iCurr == iNext ) {
                common::Vector_Shutdown( &faceVerts );
                common::Vector_Shutdown( &rawVertices );
                ClearBoundaryData( pBoundary );
                return geometry_status_t::DEGENERATE;
            }
            if ( !TryInsertUniqueEdge( &pBoundary->edges, iCurr, iNext ) ) {
                common::Vector_Shutdown( &faceVerts );
                common::Vector_Shutdown( &rawVertices );
                ClearBoundaryData( pBoundary );
                return geometry_status_t::ALLOCATION_FAILED;
            }
        }
    }

    common::Vector_Shutdown( &faceVerts );
    common::Vector_Shutdown( &rawVertices );

    // ---- Step 5: Final consistency check --------------------------------

    // Every side of a valid convex brush must contribute exactly one face.
    if ( ( !bAllowRedundantSides &&
           common::Vector_Count( &pBoundary->faces ) != cSides ) ||
         !BoundaryTopologyIsClosed( *pBoundary ) ) {
        ClearBoundaryData( pBoundary );
        return geometry_status_t::DEGENERATE;
    }

    return geometry_status_t::OK;
}

geometry_status_t BrushBoundary_TryReconstruct(
    brush_boundary_t *pBoundary,
    const brush_solid_t *pBrush,
    const geometry_policy_t &policy ) noexcept
{
    return TryReconstructBoundary(
        pBoundary, pBrush, policy, false );
}

geometry_status_t BrushBoundary_TryReconstructAllowRedundantSides(
    brush_boundary_t *pBoundary,
    const brush_solid_t *pBrush,
    const geometry_policy_t &policy ) noexcept
{
    return TryReconstructBoundary(
        pBoundary, pBrush, policy, true );
}

geometry_status_t BrushSolid_TryCanonicalizeSides(
    brush_solid_t *pBrush,
    const geometry_policy_t &policy ) noexcept
{
    if ( pBrush == nullptr || pBrush->sides.pAllocator == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !common::Vector_IsValid( &pBrush->sides ) ||
         !common::Allocator_IsValid( pBrush->sides.pAllocator ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    if ( !GeometryPolicy_IsValid( policy ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    brush_boundary_t boundary{};
    geometry_status_t status = BrushBoundary_Init(
        &boundary, pBrush->sides.pAllocator );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    status = BrushBoundary_TryReconstructAllowRedundantSides(
        &boundary, pBrush, policy );
    if ( status != geometry_status_t::OK ) {
        BrushBoundary_Shutdown( &boundary );
        return status;
    }

    const common::usize cSides = pBrush->sides.nCount;
    const common::usize cActiveSides = boundary.faces.nCount;
    common::u32 iPreviousSide = 0u;
    for ( common::usize iFace = 0u; iFace < cActiveSides; ++iFace ) {
        const common::u32 iSide = boundary.faces.pData[iFace].iSide;
        if ( iSide >= cSides ||
             ( iFace != 0u && iSide <= iPreviousSide ) ) {
            BrushBoundary_Shutdown( &boundary );
            return geometry_status_t::CORRUPT_STATE;
        }
        iPreviousSide = iSide;
    }

    // From here publication cannot fail. Face records are emitted in side
    // order, so compacting retained records preserves their relative order.
    for ( common::usize iFace = 0u; iFace < cActiveSides; ++iFace ) {
        const common::usize iSide = boundary.faces.pData[iFace].iSide;
        pBrush->sides.pData[iFace] = pBrush->sides.pData[iSide];
    }
    pBrush->sides.nCount = cActiveSides;

    BrushBoundary_Shutdown( &boundary );
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
