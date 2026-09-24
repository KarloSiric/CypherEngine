//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushValidation.cpp
//  Purpose: Implements quick and deep validation for plane-defined brushes.
//  Details: Quick validation is O(n^2) in the side count (duplicate plane
//           detection). Deep validation is dominated by boundary
//           reconstruction, which is O(n^4) via Brushd_BuildVertices.
//           Both are intended for authoring-scale brushes where n <= 256.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//  - Newell winding, area, edge-length, and identity checks on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushValidation.h"

namespace cypher::editor::geometry
{

namespace
{

using math::f64;
using math::vec3d_t;
using math::planed_t;

// Two planes are considered duplicates if their normals and distances
// agree within tolerance. Two planes are contradictory if their normals
// are opposite (dot near -1) and their distances agree in magnitude —
// they define the same geometric plane but with opposite orientation,
// which produces an empty intersection.
bool ArePlanesConflicting(
    planed_t a,
    planed_t b,
    f64 normalTolerance,
    f64 distanceTolerance ) noexcept
{
    const f64 normalDot = math::Vec3d_Dot( a.normal, b.normal );

    // Duplicate: same orientation and same distance.
    if ( normalDot > ( 1.0 - normalTolerance ) &&
         math::Scalar_Abs( a.d - b.d ) <= distanceTolerance ) {
        return true;
    }

    // Contradictory: opposite orientation and matching distance magnitude.
    // Two planes n·x + d1 = 0 and -n·x + d2 = 0 define the same geometric
    // plane when d2 == -d1. Their intersection is empty because every point
    // is on the positive side of one of them.
    if ( normalDot < -( 1.0 - normalTolerance ) &&
         math::Scalar_Abs( a.d + b.d ) <= distanceTolerance ) {
        return true;
    }

    return false;
}

// Checks whether every edge in the boundary is shared by exactly two
// faces. For a closed convex polyhedron, every edge must be a shared
// boundary between two adjacent faces — an edge touched by only one face
// means an open boundary, and three or more means a non-manifold junction.
bool VerifyEdgeSharing(
    const brush_boundary_t *pBoundary ) noexcept
{
    const common::usize cEdges = common::Vector_Count( &pBoundary->edges );
    const common::usize cFaces = common::Vector_Count( &pBoundary->faces );

    for ( common::usize iEdge = 0u; iEdge < cEdges; ++iEdge ) {
        const brush_boundary_edge_t &edge = pBoundary->edges.pData[iEdge];
        common::u32 cAdjacentFaces = 0u;

        for ( common::usize iFace = 0u; iFace < cFaces; ++iFace ) {
            const brush_boundary_face_t &face = pBoundary->faces.pData[iFace];

            // Walk the face ring looking for this edge (in either direction).
            for ( common::u32 iv = 0u; iv < face.cVertices; ++iv ) {
                const common::u32 iCurr =
                    pBoundary->faceVertexIndices.pData[face.iFirstIndex + iv];
                const common::u32 iNext =
                    pBoundary->faceVertexIndices.pData[
                        face.iFirstIndex +
                        ( ( iv + 1u ) % face.cVertices )];

                // Canonical edge ordering: smaller index first.
                const common::u32 iLow = ( iCurr < iNext ) ? iCurr : iNext;
                const common::u32 iHigh = ( iCurr < iNext ) ? iNext : iCurr;

                if ( iLow == edge.iVertex0 && iHigh == edge.iVertex1 ) {
                    ++cAdjacentFaces;
                    break;
                }
            }
        }

        if ( cAdjacentFaces != 2u ) {
            return false;
        }
    }
    return true;
}

// Checks that every face's winding agrees with its side plane normal and
// that the face encloses real area. The face normal is the Newell normal
// of the whole ring rather than the cross product of its first three
// vertices: that cross product vanishes whenever the first three vertices
// are nearly collinear, which a legitimate polygon with a shallow corner
// can produce. Newell's sum weighs every edge and equals twice the
// polygon's vector area, so its length doubles as the area test.
geometry_status_t VerifyFaceWindings(
    const brush_boundary_t *pBoundary,
    const brush_solid_t *pBrush,
    f64 minimumFaceArea ) noexcept
{
    const common::usize cFaces = common::Vector_Count( &pBoundary->faces );

    for ( common::usize iFace = 0u; iFace < cFaces; ++iFace ) {
        const brush_boundary_face_t &face = pBoundary->faces.pData[iFace];
        if ( face.cVertices < 3u ) {
            return geometry_status_t::DEGENERATE;
        }

        vec3d_t newell = math::CY_VEC3D_ZERO;
        for ( common::u32 iv = 0u; iv < face.cVertices; ++iv ) {
            const vec3d_t curr = pBoundary->vertices.pData[
                pBoundary->faceVertexIndices.pData[face.iFirstIndex + iv]];
            const vec3d_t next = pBoundary->vertices.pData[
                pBoundary->faceVertexIndices.pData[
                    face.iFirstIndex + ( ( iv + 1u ) % face.cVertices )]];
            newell.x += ( curr.y - next.y ) * ( curr.z + next.z );
            newell.y += ( curr.z - next.z ) * ( curr.x + next.x );
            newell.z += ( curr.x - next.x ) * ( curr.y + next.y );
        }

        // |newell| = 2 * area; compare squares to avoid a square root.
        const f64 twiceMinimumArea = 2.0 * minimumFaceArea;
        if ( !( math::Vec3d_LengthSquared( newell ) >=
                twiceMinimumArea * twiceMinimumArea ) ) {
            return geometry_status_t::DEGENERATE;
        }

        // Only the sign matters for orientation, so no normalization.
        const vec3d_t sideNormal =
            pBrush->sides.pData[face.iSide].plane.normal;
        if ( math::Vec3d_Dot( newell, sideNormal ) <= 0.0 ) {
            return geometry_status_t::DEGENERATE;
        }
    }
    return geometry_status_t::OK;
}

// Every boundary edge must be at least the policy's minimum edge length.
// Reconstruction merges vertices closer than the weld distance, so a
// shorter edge means the policy's weld and edge tolerances disagree and
// downstream tessellation would receive a sliver.
bool VerifyEdgeLengths(
    const brush_boundary_t *pBoundary,
    f64 minimumEdgeLength ) noexcept
{
    const f64 minimumSq = minimumEdgeLength * minimumEdgeLength;
    const common::usize cEdges = common::Vector_Count( &pBoundary->edges );
    for ( common::usize iEdge = 0u; iEdge < cEdges; ++iEdge ) {
        const brush_boundary_edge_t &edge = pBoundary->edges.pData[iEdge];
        const f64 lengthSq = math::Vec3d_DistanceSquared(
            pBoundary->vertices.pData[edge.iVertex0],
            pBoundary->vertices.pData[edge.iVertex1] );
        if ( !( lengthSq >= minimumSq ) ) {
            return false;
        }
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Quick validation
// ---------------------------------------------------------------------------

geometry_status_t BrushValidation_Quick(
    const brush_solid_t *pBrush,
    const geometry_policy_t &policy ) noexcept
{
    if ( pBrush == nullptr || pBrush->sides.pAllocator == nullptr ) {
        return geometry_status_t::NOT_INITIALIZED;
    }

    const common::usize cSides = common::Vector_Count( &pBrush->sides );

    // A tetrahedron is the simplest closed convex solid.
    if ( cSides < 4u ) {
        return geometry_status_t::DEGENERATE;
    }
    if ( static_cast<common::u64>( cSides ) >
         policy.limits.cBrushSidesPerBrushMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    const f64 normalTol = policy.numerical.fUnitNormalTolerance;
    const f64 coordLimit = policy.numerical.fCoordinateMagnitudeLimit;
    const f64 distTol = policy.numerical.fCoplanarDistanceTolerance;

    for ( common::usize i = 0u; i < cSides; ++i ) {
        const planed_t &plane = pBrush->sides.pData[i].plane;

        if ( !math::Planed_IsFinite( plane ) ) {
            return geometry_status_t::NUMERIC_FAILURE;
        }
        if ( !math::Planed_IsNormalized( plane, normalTol ) ) {
            return geometry_status_t::DEGENERATE;
        }
        if ( math::Scalar_Abs( plane.d ) > coordLimit ) {
            return geometry_status_t::LIMIT_EXCEEDED;
        }

        // Check against every earlier plane for duplicates and
        // contradictions. O(n^2) but n <= 256.
        for ( common::usize j = 0u; j < i; ++j ) {
            if ( ArePlanesConflicting(
                     plane, pBrush->sides.pData[j].plane,
                     normalTol, distTol ) ) {
                return geometry_status_t::DEGENERATE;
            }
        }
    }

    // Identity checks run after every plane check so that a brush with both
    // kinds of defect reports the geometric one first, which is what an
    // editing preview needs to show. Storage rejects these on the way in;
    // this covers brushes that bypassed it (deserialization, raw copies).
    if ( !GeometrySourceId_IsValid( pBrush->sourceId ) ) {
        return geometry_status_t::IDENTITY_CONFLICT;
    }
    for ( common::usize i = 0u; i < cSides; ++i ) {
        const geometry_source_id_t id = pBrush->sides.pData[i].sourceId;
        if ( !GeometrySourceId_IsValid( id ) ||
             id.value == pBrush->sourceId.value ) {
            return geometry_status_t::IDENTITY_CONFLICT;
        }
        for ( common::usize j = 0u; j < i; ++j ) {
            if ( pBrush->sides.pData[j].sourceId.value == id.value ) {
                return geometry_status_t::IDENTITY_CONFLICT;
            }
        }
    }

    return geometry_status_t::OK;
}

// ---------------------------------------------------------------------------
// Boundary checks
// ---------------------------------------------------------------------------

brush_validation_result_t BrushValidation_CheckBoundary(
    const brush_solid_t *pBrush,
    const brush_boundary_t *pBoundary,
    const geometry_policy_t &policy ) noexcept
{
    brush_validation_result_t result{};
    if ( pBrush == nullptr || pBrush->sides.pAllocator == nullptr ||
         pBoundary == nullptr || pBoundary->vertices.pAllocator == nullptr ) {
        result.status = geometry_status_t::NOT_INITIALIZED;
        return result;
    }
    if ( BrushBoundary_FaceCount( pBoundary ) == 0u ) {
        result.status = geometry_status_t::DEGENERATE;
        return result;
    }
    // Every face must reference a real side; a boundary built from a
    // different brush would otherwise index out of range below.
    for ( common::usize i = 0u; i < BrushBoundary_FaceCount( pBoundary ); ++i ) {
        if ( pBoundary->faces.pData[i].iSide >= BrushSolid_SideCount( pBrush ) ) {
            result.status = geometry_status_t::CORRUPT_STATE;
            return result;
        }
    }

    // Populate counts for the caller's diagnostic use.
    result.cVertices = static_cast<common::u32>(
        BrushBoundary_VertexCount( pBoundary ) );
    result.cEdges = static_cast<common::u32>(
        BrushBoundary_EdgeCount( pBoundary ) );
    result.cFaces = static_cast<common::u32>(
        BrushBoundary_FaceCount( pBoundary ) );

    // ---- Euler relation: v - e + f = 2 for a closed convex polyhedron ----

    result.eulerCharacteristic =
        static_cast<common::i32>( result.cVertices ) -
        static_cast<common::i32>( result.cEdges ) +
        static_cast<common::i32>( result.cFaces );

    if ( result.eulerCharacteristic != 2 ) {
        result.status = geometry_status_t::OPEN_VOLUME;
        return result;
    }

    // ---- Every side must contribute exactly one face ----------------------

    const common::usize cSides = BrushSolid_SideCount( pBrush );
    if ( result.cFaces != static_cast<common::u32>( cSides ) ) {
        // A side with no face means redundant planes or an unbounded solid.
        // More faces than sides should be impossible for convex brushes.
        result.status = geometry_status_t::DEGENERATE;
        return result;
    }

    // ---- Every face must have at least 3 vertices (triangle minimum) -----

    for ( common::u32 i = 0u; i < result.cFaces; ++i ) {
        if ( pBoundary->faces.pData[i].cVertices < 3u ) {
            result.status = geometry_status_t::DEGENERATE;
            return result;
        }
    }

    // ---- Every edge must be shared by exactly two faces ------------------

    if ( !VerifyEdgeSharing( pBoundary ) ) {
        result.status = geometry_status_t::OPEN_VOLUME;
        return result;
    }

    // ---- Face winding must agree with side plane normals -----------------

    result.status = VerifyFaceWindings(
        pBoundary, pBrush, policy.numerical.fMinimumFaceArea );
    if ( result.status != geometry_status_t::OK ) {
        return result;
    }

    // ---- Every edge must meet the minimum edge length --------------------

    if ( !VerifyEdgeLengths( pBoundary, policy.numerical.fMinimumEdgeLength ) ) {
        result.status = geometry_status_t::DEGENERATE;
        return result;
    }

    result.status = geometry_status_t::OK;
    result.bWatertight = true;
    return result;
}

// ---------------------------------------------------------------------------
// Deep validation
// ---------------------------------------------------------------------------

brush_validation_result_t BrushValidation_Deep(
    const brush_solid_t *pBrush,
    const geometry_policy_t &policy,
    const common::allocator_t *pAllocator ) noexcept
{
    brush_validation_result_t result{};

    // Quick validation first — no point reconstructing if the planes are bad.
    result.status = BrushValidation_Quick( pBrush, policy );
    if ( result.status != geometry_status_t::OK ) {
        return result;
    }

    // Reconstruct the boundary from the plane set.
    brush_boundary_t boundary{};
    result.status = BrushBoundary_Init( &boundary, pAllocator );
    if ( result.status != geometry_status_t::OK ) {
        return result;
    }

    result.status = BrushBoundary_TryReconstruct( &boundary, pBrush, policy );
    if ( result.status != geometry_status_t::OK ) {
        BrushBoundary_Shutdown( &boundary );
        return result;
    }

    result = BrushValidation_CheckBoundary( pBrush, &boundary, policy );
    BrushBoundary_Shutdown( &boundary );
    return result;
}

} // namespace cypher::editor::geometry
