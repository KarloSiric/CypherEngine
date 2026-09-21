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

// Checks that every face's winding agrees with its side plane normal.
// The geometric normal of the CCW vertex ring must point in the same
// direction as the side plane's outward normal (positive dot product).
bool VerifyFaceWindings(
    const brush_boundary_t *pBoundary,
    const brush_solid_t *pBrush ) noexcept
{
    const common::usize cFaces = common::Vector_Count( &pBoundary->faces );

    for ( common::usize iFace = 0u; iFace < cFaces; ++iFace ) {
        const brush_boundary_face_t &face = pBoundary->faces.pData[iFace];
        if ( face.cVertices < 3u ) {
            return false;
        }

        // Use the first three vertices to compute the face's geometric
        // normal via cross product. For a convex face, any three
        // consecutive vertices give a consistent result.
        const common::u32 i0 =
            pBoundary->faceVertexIndices.pData[face.iFirstIndex];
        const common::u32 i1 =
            pBoundary->faceVertexIndices.pData[face.iFirstIndex + 1u];
        const common::u32 i2 =
            pBoundary->faceVertexIndices.pData[face.iFirstIndex + 2u];

        const vec3d_t v0 = pBoundary->vertices.pData[i0];
        const vec3d_t v1 = pBoundary->vertices.pData[i1];
        const vec3d_t v2 = pBoundary->vertices.pData[i2];

        const vec3d_t edge1 = math::Vec3d_Subtract( v1, v0 );
        const vec3d_t edge2 = math::Vec3d_Subtract( v2, v0 );
        const vec3d_t faceNormal = math::Vec3d_Cross( edge1, edge2 );

        // The face normal must agree with the side plane's outward normal.
        // We only need the sign, not the magnitude, so no normalization.
        const vec3d_t sideNormal =
            pBrush->sides.pData[face.iSide].plane.normal;
        if ( math::Vec3d_Dot( faceNormal, sideNormal ) <= 0.0 ) {
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

    return geometry_status_t::OK;
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

    // Populate counts for the caller's diagnostic use.
    result.cVertices = static_cast<common::u32>(
        BrushBoundary_VertexCount( &boundary ) );
    result.cEdges = static_cast<common::u32>(
        BrushBoundary_EdgeCount( &boundary ) );
    result.cFaces = static_cast<common::u32>(
        BrushBoundary_FaceCount( &boundary ) );

    // ---- Euler relation: v - e + f = 2 for a closed convex polyhedron ----

    result.eulerCharacteristic =
        static_cast<common::i32>( result.cVertices ) -
        static_cast<common::i32>( result.cEdges ) +
        static_cast<common::i32>( result.cFaces );

    if ( result.eulerCharacteristic != 2 ) {
        result.status = geometry_status_t::OPEN_VOLUME;
        BrushBoundary_Shutdown( &boundary );
        return result;
    }

    // ---- Every side must contribute exactly one face ----------------------

    const common::usize cSides = BrushSolid_SideCount( pBrush );
    if ( result.cFaces != static_cast<common::u32>( cSides ) ) {
        // A side with no face means redundant planes or an unbounded solid.
        // More faces than sides should be impossible for convex brushes.
        result.status = geometry_status_t::DEGENERATE;
        BrushBoundary_Shutdown( &boundary );
        return result;
    }

    // ---- Every face must have at least 3 vertices (triangle minimum) -----

    for ( common::u32 i = 0u; i < result.cFaces; ++i ) {
        if ( boundary.faces.pData[i].cVertices < 3u ) {
            result.status = geometry_status_t::DEGENERATE;
            BrushBoundary_Shutdown( &boundary );
            return result;
        }
    }

    // ---- Every edge must be shared by exactly two faces ------------------

    if ( !VerifyEdgeSharing( &boundary ) ) {
        result.status = geometry_status_t::OPEN_VOLUME;
        BrushBoundary_Shutdown( &boundary );
        return result;
    }

    // ---- Face winding must agree with side plane normals -----------------

    if ( !VerifyFaceWindings( &boundary, pBrush ) ) {
        result.status = geometry_status_t::DEGENERATE;
        BrushBoundary_Shutdown( &boundary );
        return result;
    }

    BrushBoundary_Shutdown( &boundary );

    result.status = geometry_status_t::OK;
    result.bWatertight = true;
    return result;
}

} // namespace cypher::editor::geometry
