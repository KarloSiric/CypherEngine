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
//           The reconstruction allocates temporary arrays for planes, raw
//           vertex candidates, canonical ordering, and per-face polygons,
//           then builds the final indexed boundary in the persistent
//           output. All temporaries use the boundary's allocator and are
//           released by scope guards, so every early return is leak-free.
//           Any failure clears the output before returning.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//  - Canonical ordering and failure atomicity added on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushBoundary.h"

#include "CypherGeometry_Kernel_CoordinateKey.h"

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

// Owns one temporary vector for the duration of a scope. Reconstruction
// has many early returns; tying each temporary's release to scope exit is
// what keeps every one of them leak-free without a cleanup ladder.
template <typename type_t>
struct scoped_vector_t {
    common::vector_t<type_t> v{};

    scoped_vector_t() noexcept = default;
    scoped_vector_t( const scoped_vector_t & ) = delete;
    scoped_vector_t &operator=( const scoped_vector_t & ) = delete;
    ~scoped_vector_t() noexcept
    {
        if ( v.pAllocator != nullptr ) {
            common::Vector_Shutdown( &v );
        }
    }

    // Initializes and sizes the vector to exactly cCount elements.
    CYPHER_NODISCARD bool_t TryInitSized(
        const common::allocator_t *pAllocator, common::usize cCount ) noexcept
    {
        return common::Vector_Init( &v, pAllocator, cCount ) &&
               common::Vector_Resize( &v, cCount );
    }
};

void ClearBoundary( brush_boundary_t *pBoundary ) noexcept
{
    common::Vector_Clear( &pBoundary->vertices );
    common::Vector_Clear( &pBoundary->edges );
    common::Vector_Clear( &pBoundary->faces );
    common::Vector_Clear( &pBoundary->faceVertexIndices );
}

bool_t IsWithinMagnitudeLimit( math::f64 value, math::f64 limit ) noexcept
{
    return math::Scalar_Abs( value ) <= limit;
}

// Finds the index in the unique vertex array whose position is within
// mergeTolerance of the query point. Face polygons are built from exact
// copies of the unique vertices, so a miss means the two passes disagree
// and the boundary cannot be trusted.
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

// Canonical vertex ordering record: the lattice key decides order, the raw
// coordinates break the (policy-dependent, normally impossible) tie where
// two distinct vertices share a lattice cell.
struct canonical_vertex_t {
    geometry_coordinate_key_t key;
    math::vec3d_t position;
};

bool_t CanonicalVertexLess(
    const canonical_vertex_t &a, const canonical_vertex_t &b ) noexcept
{
    if ( Kernel_CoordinateKeyLess( a.key, b.key ) ) {
        return true;
    }
    if ( Kernel_CoordinateKeyLess( b.key, a.key ) ) {
        return false;
    }
    if ( a.position.x != b.position.x ) {
        return a.position.x < b.position.x;
    }
    if ( a.position.y != b.position.y ) {
        return a.position.y < b.position.y;
    }
    return a.position.z < b.position.z;
}

bool_t EdgeLess(
    const brush_boundary_edge_t &a, const brush_boundary_edge_t &b ) noexcept
{
    return a.iVertex0 != b.iVertex0 ? a.iVertex0 < b.iVertex0
                                    : a.iVertex1 < b.iVertex1;
}

// Stable insertion sort. Authoring brushes have at most a few hundred
// vertices and edges, and the quartic vertex enumeration dominates the
// cost of reconstruction by orders of magnitude, so the simple sort is
// the right trade: no allocation, deterministic, trivially auditable.
template <typename type_t, typename less_t>
void InsertionSort( type_t *pData, common::usize cCount, less_t less ) noexcept
{
    for ( common::usize i = 1u; i < cCount; ++i ) {
        const type_t value = pData[i];
        common::usize j = i;
        while ( j > 0u && less( value, pData[j - 1u] ) ) {
            pData[j] = pData[j - 1u];
            --j;
        }
        pData[j] = value;
    }
}

// Performs the reconstruction into an already-cleared boundary. The caller
// clears the boundary again on any non-OK return, which is what makes the
// public entry point failure-atomic.
geometry_status_t ReconstructInto(
    brush_boundary_t *pBoundary,
    const brush_solid_t *pBrush,
    const geometry_policy_t &policy ) noexcept
{
    const common::allocator_t *pAllocator = pBoundary->vertices.pAllocator;
    const common::usize cSides = common::Vector_Count( &pBrush->sides );

    // A tetrahedron is the minimum closed convex solid — fewer than 4
    // planes cannot bound a finite volume.
    if ( cSides < 4u ) {
        return geometry_status_t::DEGENERATE;
    }
    if ( static_cast<common::u64>( cSides ) >
         policy.limits.cBrushSidesPerBrushMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    // ---- Step 1: Extract and validate planes ---------------------------

    scoped_vector_t<math::planed_t> planes{};
    if ( !planes.TryInitSized( pAllocator, cSides ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( common::usize i = 0u; i < cSides; ++i ) {
        const math::planed_t &plane = pBrush->sides.pData[i].plane;
        if ( !math::Planed_IsFinite( plane ) ) {
            return geometry_status_t::NUMERIC_FAILURE;
        }
        if ( !math::Planed_IsNormalized(
                 plane, policy.numerical.fUnitNormalTolerance ) ) {
            return geometry_status_t::DEGENERATE;
        }
        if ( !IsWithinMagnitudeLimit(
                 plane.d, policy.numerical.fCoordinateMagnitudeLimit ) ) {
            return geometry_status_t::LIMIT_EXCEEDED;
        }
        planes.v.pData[i] = plane;
    }

    // ---- Step 2: Build unique vertices from plane triples ---------------

    // 2F bounds the 2F - 4 vertices a convex polyhedron can have, and is
    // far smaller than the C(F, 3) raw candidate count.
    const common::usize cCandidateBound =
        math::Brush_MaximumVertexCandidates( cSides );
    const common::usize cVertexCapacity =
        ( 2u * cSides < cCandidateBound ) ? 2u * cSides : cCandidateBound;

    scoped_vector_t<math::vec3d_t> rawVertices{};
    if ( !rawVertices.TryInitSized( pAllocator, cVertexCapacity ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }

    // The minimum determinant threshold controls how nearly-parallel a
    // plane triple can be before we reject its intersection. Using the
    // absolute distance tolerance keeps this consistent with the policy.
    const math::f64 minAbsDet = policy.numerical.fAbsoluteDistanceTolerance;
    const math::f64 insideTol = policy.numerical.fCoplanarDistanceTolerance;
    const math::f64 mergeTol = policy.numerical.fWeldDistance;

    const math::brush_vertex_result_t vertexResult = math::Brushd_BuildVertices(
        planes.v.pData, cSides,
        minAbsDet, insideTol, mergeTol,
        rawVertices.v.pData, cVertexCapacity );

    // INSUFFICIENT_CAPACITY here means more vertices than any convex
    // polyhedron with this many sides can have: the input is not a solid.
    if ( vertexResult.status != math::brush_build_status_t::OK ||
         vertexResult.cVerticesWritten < 4u ) {
        return geometry_status_t::DEGENERATE;
    }
    const common::usize cBoundaryVerts = vertexResult.cVerticesWritten;

    // ---- Step 3: Canonical vertex order ---------------------------------

    scoped_vector_t<canonical_vertex_t> ordered{};
    if ( !ordered.TryInitSized( pAllocator, cBoundaryVerts ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( common::usize i = 0u; i < cBoundaryVerts; ++i ) {
        canonical_vertex_t &entry = ordered.v.pData[i];
        entry.position = rawVertices.v.pData[i];
        const geometry_status_t keyStatus = Kernel_TryQuantizePoint(
            policy.numerical, entry.position, &entry.key );
        if ( keyStatus != geometry_status_t::OK ) {
            return keyStatus;
        }
    }
    InsertionSort( ordered.v.pData, cBoundaryVerts, CanonicalVertexLess );

    if ( !common::Vector_Reserve( &pBoundary->vertices, cBoundaryVerts ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( common::usize i = 0u; i < cBoundaryVerts; ++i ) {
        // Cannot fail: capacity reserved above.
        ( void )common::Vector_PushBack(
            &pBoundary->vertices, ordered.v.pData[i].position );
    }

    // ---- Step 4: Build face polygons per side ---------------------------

    scoped_vector_t<math::vec3d_t> faceVerts{};
    scoped_vector_t<common::u32> faceRing{};
    if ( !faceVerts.TryInitSized( pAllocator, cBoundaryVerts ) ||
         !faceRing.TryInitSized( pAllocator, cBoundaryVerts ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    if ( !common::Vector_Reserve( &pBoundary->faces, cSides ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }

    for ( common::usize iSide = 0u; iSide < cSides; ++iSide ) {
        const math::brush_vertex_result_t faceResult =
            math::Brushd_BuildFacePolygon(
                planes.v.pData[iSide],
                pBoundary->vertices.pData, cBoundaryVerts,
                insideTol,
                policy.numerical.fAbsoluteDistanceTolerance,
                faceVerts.v.pData, cBoundaryVerts );

        // A side touching fewer than 3 vertices does not form a polygon:
        // its plane is redundant (or the solid is open). Deep validation
        // reports the missing face; reconstruction simply omits it.
        if ( faceResult.status == math::brush_build_status_t::DEGENERATE ) {
            continue;
        }
        if ( faceResult.status != math::brush_build_status_t::OK ) {
            return geometry_status_t::CORRUPT_STATE;
        }

        const common::usize cFaceVerts = faceResult.cVerticesWritten;
        common::usize iRingStart = 0u;
        for ( common::usize iv = 0u; iv < cFaceVerts; ++iv ) {
            common::u32 iMapped = 0u;
            if ( !FindVertexIndex(
                     pBoundary->vertices.pData, cBoundaryVerts,
                     faceVerts.v.pData[iv], mergeTol, &iMapped ) ) {
                return geometry_status_t::CORRUPT_STATE;
            }
            faceRing.v.pData[iv] = iMapped;
            if ( iMapped < faceRing.v.pData[iRingStart] ) {
                iRingStart = iv;
            }
        }

        // Rotating (never reflecting) the ring keeps its CCW winding while
        // making the starting vertex independent of the angular sort's
        // tangent basis.
        const common::u32 iFirstIndex = static_cast<common::u32>(
            common::Vector_Count( &pBoundary->faceVertexIndices ) );
        if ( !common::Vector_Reserve(
                 &pBoundary->faceVertexIndices, iFirstIndex + cFaceVerts ) ) {
            return geometry_status_t::ALLOCATION_FAILED;
        }
        for ( common::usize iv = 0u; iv < cFaceVerts; ++iv ) {
            ( void )common::Vector_PushBack(
                &pBoundary->faceVertexIndices,
                faceRing.v.pData[( iRingStart + iv ) % cFaceVerts] );
        }

        const brush_boundary_face_t face{
            static_cast<common::u32>( iSide ),
            iFirstIndex,
            static_cast<common::u32>( cFaceVerts )
        };
        // Cannot fail: capacity for one face per side reserved above.
        ( void )common::Vector_PushBack( &pBoundary->faces, face );
    }

    if ( common::Vector_Count( &pBoundary->faces ) < 4u ) {
        return geometry_status_t::DEGENERATE;
    }

    // ---- Step 5: Unique, sorted edges -----------------------------------

    // Every face-ring step names one directed edge; each undirected edge
    // of a closed solid appears twice. Collect all steps in canonical
    // (low, high) form, sort, then compact duplicates in place.
    const common::usize cSteps =
        common::Vector_Count( &pBoundary->faceVertexIndices );
    if ( !common::Vector_Reserve( &pBoundary->edges, cSteps ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    const common::usize cFaces = common::Vector_Count( &pBoundary->faces );
    for ( common::usize iFace = 0u; iFace < cFaces; ++iFace ) {
        const brush_boundary_face_t &face = pBoundary->faces.pData[iFace];
        for ( common::u32 iv = 0u; iv < face.cVertices; ++iv ) {
            const common::u32 iCurr =
                pBoundary->faceVertexIndices.pData[face.iFirstIndex + iv];
            const common::u32 iNext =
                pBoundary->faceVertexIndices.pData[
                    face.iFirstIndex + ( ( iv + 1u ) % face.cVertices )];
            const brush_boundary_edge_t edge{
                ( iCurr < iNext ) ? iCurr : iNext,
                ( iCurr < iNext ) ? iNext : iCurr
            };
            ( void )common::Vector_PushBack( &pBoundary->edges, edge );
        }
    }

    brush_boundary_edge_t *pEdges = pBoundary->edges.pData;
    InsertionSort( pEdges, cSteps, EdgeLess );
    common::usize cUnique = 0u;
    for ( common::usize i = 0u; i < cSteps; ++i ) {
        if ( cUnique == 0u ||
             pEdges[cUnique - 1u].iVertex0 != pEdges[i].iVertex0 ||
             pEdges[cUnique - 1u].iVertex1 != pEdges[i].iVertex1 ) {
            pEdges[cUnique++] = pEdges[i];
        }
    }
    while ( common::Vector_Count( &pBoundary->edges ) > cUnique ) {
        common::Vector_PopBack( &pBoundary->edges );
    }

    return geometry_status_t::OK;
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
    ClearBoundary( pBoundary );

    const geometry_status_t status = ReconstructInto( pBoundary, pBrush, policy );
    if ( status != geometry_status_t::OK ) {
        ClearBoundary( pBoundary );
    }
    return status;
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

geometry_status_t BrushBoundary_TryGetVertex(
    const brush_boundary_t *pBoundary,
    common::usize iVertex,
    math::vec3d_t *pVertexOut ) noexcept
{
    if ( pVertexOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pVertexOut = math::CY_VEC3D_ZERO;
    if ( !IsInitialized( pBoundary ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( iVertex >= common::Vector_Count( &pBoundary->vertices ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pVertexOut = pBoundary->vertices.pData[iVertex];
    return geometry_status_t::OK;
}

geometry_status_t BrushBoundary_TryGetEdge(
    const brush_boundary_t *pBoundary,
    common::usize iEdge,
    brush_boundary_edge_t *pEdgeOut ) noexcept
{
    if ( pEdgeOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pEdgeOut = {};
    if ( !IsInitialized( pBoundary ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( iEdge >= common::Vector_Count( &pBoundary->edges ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pEdgeOut = pBoundary->edges.pData[iEdge];
    return geometry_status_t::OK;
}

geometry_status_t BrushBoundary_TryGetFace(
    const brush_boundary_t *pBoundary,
    common::usize iFace,
    brush_boundary_face_t *pFaceOut ) noexcept
{
    if ( pFaceOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pFaceOut = {};
    if ( !IsInitialized( pBoundary ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( iFace >= common::Vector_Count( &pBoundary->faces ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pFaceOut = pBoundary->faces.pData[iFace];
    return geometry_status_t::OK;
}

geometry_status_t BrushBoundary_TryFindFaceForSide(
    const brush_boundary_t *pBoundary,
    common::usize iSide,
    common::usize *pFaceOut ) noexcept
{
    if ( pFaceOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pFaceOut = 0u;
    if ( !IsInitialized( pBoundary ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    // Faces are emitted in ascending side order, but a linear scan keeps
    // this correct without depending on that ordering detail.
    const common::usize cFaces = common::Vector_Count( &pBoundary->faces );
    for ( common::usize iFace = 0u; iFace < cFaces; ++iFace ) {
        if ( pBoundary->faces.pData[iFace].iSide == iSide ) {
            *pFaceOut = iFace;
            return geometry_status_t::OK;
        }
    }
    return geometry_status_t::INVALID_ARGUMENT;
}

geometry_status_t BrushBoundary_TryGetBounds(
    const brush_boundary_t *pBoundary,
    math::aabbd_t *pBoundsOut ) noexcept
{
    if ( pBoundsOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pBoundsOut = math::CY_AABBD_EMPTY;
    if ( !IsInitialized( pBoundary ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    const common::usize cVertices = common::Vector_Count( &pBoundary->vertices );
    if ( cVertices == 0u ) {
        return geometry_status_t::DEGENERATE;
    }
    math::aabbd_t bounds = math::CY_AABBD_EMPTY;
    for ( common::usize i = 0u; i < cVertices; ++i ) {
        bounds = math::Aabbd_ExpandPoint( bounds, pBoundary->vertices.pData[i] );
    }
    *pBoundsOut = bounds;
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
