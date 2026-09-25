//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushValidation.cpp
//  Purpose: Implements quick and deep validation for plane-defined brushes.
//  Details: Quick validation is O(n^2) in the side count (identity and
//           duplicate-plane detection). Deep validation is dominated by
//           boundary reconstruction, which is O(n^4) via Brushd_BuildVertices.
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
using math::planed_t;
using math::vec3d_t;

template <typename type_t>
bool VectorIsCanonicalEmpty(
    const common::vector_t<type_t> &vector ) noexcept
{
    return vector.pData == nullptr && vector.nCount == 0u &&
           vector.nCapacity == 0u && vector.pAllocator == nullptr;
}

geometry_status_t ValidateBrushStorage(
    const brush_solid_t *pBrush ) noexcept
{
    if ( pBrush == nullptr ) {
        return geometry_status_t::NOT_INITIALIZED;
    }

    if ( pBrush->sides.pAllocator == nullptr ) {
        return VectorIsCanonicalEmpty( pBrush->sides ) &&
                       !GeometrySourceId_IsValid( pBrush->sourceId )
            ? geometry_status_t::NOT_INITIALIZED
            : geometry_status_t::CORRUPT_STATE;
    }

    if ( !common::Vector_IsValid( &pBrush->sides ) ||
         !common::Allocator_IsValid( pBrush->sides.pAllocator ) ||
         !GeometrySourceId_IsValid( pBrush->sourceId ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    return geometry_status_t::OK;
}

// Two planes are considered duplicates if their normals and distances
// agree within tolerance. Two planes are contradictory if their normals
// are opposite and their signed distances describe the same geometric plane.
bool ArePlanesConflicting(
    planed_t a,
    planed_t b,
    f64 normalTolerance,
    f64 distanceTolerance ) noexcept
{
    const f64 normalDot = math::Vec3d_Dot( a.normal, b.normal );

    if ( normalDot > ( 1.0 - normalTolerance ) &&
         math::Scalar_Abs( a.d - b.d ) <= distanceTolerance ) {
        return true;
    }

    if ( normalDot < -( 1.0 - normalTolerance ) &&
         math::Scalar_Abs( a.d + b.d ) <= distanceTolerance ) {
        return true;
    }

    return false;
}

bool BoundaryStorageIsValid(
    const brush_boundary_t &boundary ) noexcept
{
    if ( !common::Vector_IsValid( &boundary.vertices ) ||
         !common::Vector_IsValid( &boundary.edges ) ||
         !common::Vector_IsValid( &boundary.faces ) ||
         !common::Vector_IsValid( &boundary.faceVertexIndices ) ) {
        return false;
    }

    const common::allocator_t *pAllocator =
        boundary.vertices.pAllocator;
    return common::Allocator_IsValid( pAllocator ) &&
           boundary.edges.pAllocator == pAllocator &&
           boundary.faces.pAllocator == pAllocator &&
           boundary.faceVertexIndices.pAllocator == pAllocator;
}

bool CoordinateIsWithinLimit( f64 value, f64 limit ) noexcept
{
    return math::Scalar_Abs( value ) <= limit;
}

geometry_status_t ValidateBoundaryVertices(
    const brush_boundary_t &boundary,
    const brush_solid_t &brush,
    const geometry_policy_t &policy ) noexcept
{
    const common::usize cVertices = boundary.vertices.nCount;
    const common::usize cSides = brush.sides.nCount;
    const f64 coordinateLimit =
        policy.numerical.fCoordinateMagnitudeLimit;
    const f64 weldDistanceSquared =
        policy.numerical.fWeldDistance *
        policy.numerical.fWeldDistance;

    for ( common::usize iVertex = 0u;
          iVertex < cVertices;
          ++iVertex ) {
        const vec3d_t &vertex = boundary.vertices.pData[iVertex];
        if ( !math::Vec3d_IsFinite( vertex ) ) {
            return geometry_status_t::NUMERIC_FAILURE;
        }
        if ( !CoordinateIsWithinLimit( vertex.x, coordinateLimit ) ||
             !CoordinateIsWithinLimit( vertex.y, coordinateLimit ) ||
             !CoordinateIsWithinLimit( vertex.z, coordinateLimit ) ) {
            return geometry_status_t::LIMIT_EXCEEDED;
        }

        for ( common::usize iSide = 0u;
              iSide < cSides;
              ++iSide ) {
            const planed_t &plane = brush.sides.pData[iSide].plane;
            const f64 signedDistance =
                math::Vec3d_Dot( plane.normal, vertex ) + plane.d;
            if ( !math::Scalar_IsFinite( signedDistance ) ) {
                return geometry_status_t::NUMERIC_FAILURE;
            }
            if ( signedDistance >
                 policy.numerical.fCoplanarDistanceTolerance ) {
                return geometry_status_t::CORRUPT_STATE;
            }
        }

        for ( common::usize iPrior = 0u;
              iPrior < iVertex;
              ++iPrior ) {
            if ( math::Vec3d_DistanceSquared(
                     vertex,
                     boundary.vertices.pData[iPrior] ) <=
                 weldDistanceSquared ) {
                return geometry_status_t::DEGENERATE;
            }
        }
    }

    return geometry_status_t::OK;
}

geometry_status_t ValidateBoundaryFaces(
    const brush_boundary_t &boundary,
    const brush_solid_t &brush,
    const geometry_policy_t &policy ) noexcept
{
    const common::usize cVertices = boundary.vertices.nCount;
    const common::usize cFaces = boundary.faces.nCount;
    const common::usize cIndices = boundary.faceVertexIndices.nCount;
    const common::usize cSides = brush.sides.nCount;
    common::usize iExpectedFirstIndex = 0u;

    for ( common::usize iFace = 0u; iFace < cFaces; ++iFace ) {
        const brush_boundary_face_t &face =
            boundary.faces.pData[iFace];
        const common::usize iFirst = face.iFirstIndex;
        const common::usize cFaceVertices = face.cVertices;

        if ( face.iSide >= cSides ) {
            return geometry_status_t::CORRUPT_STATE;
        }
        if ( cFaceVertices < 3u ) {
            return geometry_status_t::DEGENERATE;
        }
        if ( iFirst != iExpectedFirstIndex ||
             iFirst > cIndices ||
             cFaceVertices > cIndices - iFirst ) {
            return geometry_status_t::CORRUPT_STATE;
        }

        for ( common::usize iPriorFace = 0u;
              iPriorFace < iFace;
              ++iPriorFace ) {
            if ( boundary.faces.pData[iPriorFace].iSide == face.iSide ) {
                return geometry_status_t::DEGENERATE;
            }
        }

        const planed_t &sidePlane =
            brush.sides.pData[face.iSide].plane;
        vec3d_t windingNormal = math::CY_VEC3D_ZERO;

        for ( common::usize iCorner = 0u;
              iCorner < cFaceVertices;
              ++iCorner ) {
            const common::u32 iVertex =
                boundary.faceVertexIndices.pData[iFirst + iCorner];
            if ( iVertex >= cVertices ) {
                return geometry_status_t::CORRUPT_STATE;
            }

            for ( common::usize iPriorCorner = 0u;
                  iPriorCorner < iCorner;
                  ++iPriorCorner ) {
                if ( boundary.faceVertexIndices.pData[
                         iFirst + iPriorCorner] == iVertex ) {
                    return geometry_status_t::DEGENERATE;
                }
            }

            const common::u32 iNextVertex =
                boundary.faceVertexIndices.pData[
                    iFirst + ( ( iCorner + 1u ) % cFaceVertices )];
            if ( iNextVertex >= cVertices ||
                 iNextVertex == iVertex ) {
                return iNextVertex >= cVertices
                    ? geometry_status_t::CORRUPT_STATE
                    : geometry_status_t::DEGENERATE;
            }

            const vec3d_t &current =
                boundary.vertices.pData[iVertex];
            const vec3d_t &next =
                boundary.vertices.pData[iNextVertex];
            const f64 planeDistance =
                math::Vec3d_Dot( sidePlane.normal, current ) +
                sidePlane.d;
            if ( !math::Scalar_IsFinite( planeDistance ) ) {
                return geometry_status_t::NUMERIC_FAILURE;
            }
            if ( math::Scalar_Abs( planeDistance ) >
                 policy.numerical.fPlanarityTolerance ) {
                return geometry_status_t::NON_PLANAR;
            }

            // Newell's method uses the whole ring and remains valid when
            // individual consecutive triples happen to be collinear.
            windingNormal.x +=
                ( current.y - next.y ) * ( current.z + next.z );
            windingNormal.y +=
                ( current.z - next.z ) * ( current.x + next.x );
            windingNormal.z +=
                ( current.x - next.x ) * ( current.y + next.y );
        }

        if ( !math::Vec3d_IsFinite( windingNormal ) ) {
            return geometry_status_t::NUMERIC_FAILURE;
        }
        const f64 windingDot =
            math::Vec3d_Dot( windingNormal, sidePlane.normal );
        if ( !math::Scalar_IsFinite( windingDot ) ) {
            return geometry_status_t::NUMERIC_FAILURE;
        }
        if ( windingDot <= 0.0 ) {
            return geometry_status_t::DEGENERATE;
        }

        iExpectedFirstIndex += cFaceVertices;
    }

    if ( iExpectedFirstIndex != cIndices ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    for ( common::usize iVertex = 0u;
          iVertex < cVertices;
          ++iVertex ) {
        bool bReferenced = false;
        for ( common::usize iIndex = 0u;
              iIndex < cIndices;
              ++iIndex ) {
            if ( boundary.faceVertexIndices.pData[iIndex] == iVertex ) {
                bReferenced = true;
                break;
            }
        }
        if ( !bReferenced ) {
            return geometry_status_t::CORRUPT_STATE;
        }
    }

    return geometry_status_t::OK;
}

bool BoundaryContainsEdge(
    const brush_boundary_t &boundary,
    common::u32 iLow,
    common::u32 iHigh ) noexcept
{
    for ( common::usize iEdge = 0u;
          iEdge < boundary.edges.nCount;
          ++iEdge ) {
        const brush_boundary_edge_t &edge =
            boundary.edges.pData[iEdge];
        if ( edge.iVertex0 == iLow && edge.iVertex1 == iHigh ) {
            return true;
        }
    }
    return false;
}

geometry_status_t ValidateBoundaryEdges(
    const brush_boundary_t &boundary ) noexcept
{
    const common::usize cVertices = boundary.vertices.nCount;
    const common::usize cEdges = boundary.edges.nCount;
    const common::usize cFaces = boundary.faces.nCount;
    const common::usize cIndices = boundary.faceVertexIndices.nCount;

    if ( cEdges > common::CY_USIZE_MAX / 2u ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    if ( cIndices != cEdges * 2u ) {
        return geometry_status_t::OPEN_VOLUME;
    }

    for ( common::usize iEdge = 0u; iEdge < cEdges; ++iEdge ) {
        const brush_boundary_edge_t &edge =
            boundary.edges.pData[iEdge];
        if ( edge.iVertex0 >= cVertices ||
             edge.iVertex1 >= cVertices ||
             edge.iVertex0 >= edge.iVertex1 ) {
            return geometry_status_t::CORRUPT_STATE;
        }
        for ( common::usize iPrior = 0u;
              iPrior < iEdge;
              ++iPrior ) {
            const brush_boundary_edge_t &prior =
                boundary.edges.pData[iPrior];
            if ( prior.iVertex0 == edge.iVertex0 &&
                 prior.iVertex1 == edge.iVertex1 ) {
                return geometry_status_t::CORRUPT_STATE;
            }
        }
    }

    for ( common::usize iFace = 0u; iFace < cFaces; ++iFace ) {
        const brush_boundary_face_t &face =
            boundary.faces.pData[iFace];
        const common::usize iFirst = face.iFirstIndex;
        const common::usize cFaceVertices = face.cVertices;
        for ( common::usize iCorner = 0u;
              iCorner < cFaceVertices;
              ++iCorner ) {
            const common::u32 iCurrent =
                boundary.faceVertexIndices.pData[iFirst + iCorner];
            const common::u32 iNext =
                boundary.faceVertexIndices.pData[
                    iFirst + ( ( iCorner + 1u ) % cFaceVertices )];
            const common::u32 iLow =
                iCurrent < iNext ? iCurrent : iNext;
            const common::u32 iHigh =
                iCurrent < iNext ? iNext : iCurrent;
            if ( !BoundaryContainsEdge( boundary, iLow, iHigh ) ) {
                return geometry_status_t::CORRUPT_STATE;
            }
        }
    }

    // Each edge must be used once in each direction. Merely seeing two uses
    // would also accept two equally wound adjacent faces.
    for ( common::usize iEdge = 0u; iEdge < cEdges; ++iEdge ) {
        const brush_boundary_edge_t &edge =
            boundary.edges.pData[iEdge];
        common::usize cForwardUses = 0u;
        common::usize cReverseUses = 0u;

        for ( common::usize iFace = 0u; iFace < cFaces; ++iFace ) {
            const brush_boundary_face_t &face =
                boundary.faces.pData[iFace];
            const common::usize iFirst = face.iFirstIndex;
            const common::usize cFaceVertices = face.cVertices;
            for ( common::usize iCorner = 0u;
                  iCorner < cFaceVertices;
                  ++iCorner ) {
                const common::u32 iCurrent =
                    boundary.faceVertexIndices.pData[iFirst + iCorner];
                const common::u32 iNext =
                    boundary.faceVertexIndices.pData[
                        iFirst +
                        ( ( iCorner + 1u ) % cFaceVertices )];
                if ( iCurrent == edge.iVertex0 &&
                     iNext == edge.iVertex1 ) {
                    ++cForwardUses;
                } else if ( iCurrent == edge.iVertex1 &&
                            iNext == edge.iVertex0 ) {
                    ++cReverseUses;
                }
            }
        }

        if ( cForwardUses != 1u || cReverseUses != 1u ) {
            return geometry_status_t::OPEN_VOLUME;
        }
    }

    return geometry_status_t::OK;
}

geometry_status_t ValidateReconstructedBoundary(
    const brush_boundary_t &boundary,
    const brush_solid_t &brush,
    const geometry_policy_t &policy,
    brush_validation_result_t *pResult ) noexcept
{
    if ( !BoundaryStorageIsValid( boundary ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    const common::usize cVertices = boundary.vertices.nCount;
    const common::usize cEdges = boundary.edges.nCount;
    const common::usize cFaces = boundary.faces.nCount;
    const common::usize cIndices = boundary.faceVertexIndices.nCount;
    const common::usize cSides = brush.sides.nCount;

    if ( cVertices > common::CY_U32_MAX ||
         cEdges > common::CY_U32_MAX ||
         cFaces > common::CY_U32_MAX ||
         cIndices > common::CY_U32_MAX ||
         static_cast<common::u64>( cVertices ) >
             policy.limits.cVerticesMax ||
         static_cast<common::u64>( cEdges ) > policy.limits.cEdgesMax ||
         static_cast<common::u64>( cFaces ) > policy.limits.cFacesMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    pResult->cVertices = static_cast<common::u32>( cVertices );
    pResult->cEdges = static_cast<common::u32>( cEdges );
    pResult->cFaces = static_cast<common::u32>( cFaces );

    if ( cVertices < 4u || cEdges < 6u || cFaces < 4u ) {
        return geometry_status_t::DEGENERATE;
    }

    const common::i64 euler =
        static_cast<common::i64>( cVertices ) -
        static_cast<common::i64>( cEdges ) +
        static_cast<common::i64>( cFaces );
    if ( euler < common::CY_I32_MIN || euler > common::CY_I32_MAX ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    pResult->eulerCharacteristic = static_cast<common::i32>( euler );
    if ( euler != 2 ) {
        return geometry_status_t::OPEN_VOLUME;
    }

    if ( cFaces != cSides ) {
        return geometry_status_t::DEGENERATE;
    }

    geometry_status_t status = ValidateBoundaryVertices(
        boundary, brush, policy );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    status = ValidateBoundaryFaces( boundary, brush, policy );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    return ValidateBoundaryEdges( boundary );
}

} // namespace

// ---------------------------------------------------------------------------
// Quick validation
// ---------------------------------------------------------------------------

geometry_status_t BrushValidation_Quick(
    const brush_solid_t *pBrush,
    const geometry_policy_t &policy ) noexcept
{
    geometry_status_t status = ValidateBrushStorage( pBrush );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    if ( !GeometryPolicy_IsValid( policy ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    const common::usize cSides = pBrush->sides.nCount;
    if ( cSides < 4u ) {
        return geometry_status_t::DEGENERATE;
    }
    if ( static_cast<common::u64>( cSides ) >
         policy.limits.cBrushSidesPerBrushMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    // Identity is canonical brush state. Complete this pass before plane
    // diagnostics so conflicts are deterministic.
    for ( common::usize i = 0u; i < cSides; ++i ) {
        const geometry_source_id_t id =
            pBrush->sides.pData[i].sourceId;
        if ( !GeometrySourceId_IsValid( id ) ) {
            return geometry_status_t::CORRUPT_STATE;
        }
        if ( id.value == pBrush->sourceId.value ) {
            return geometry_status_t::IDENTITY_CONFLICT;
        }
        for ( common::usize j = 0u; j < i; ++j ) {
            if ( pBrush->sides.pData[j].sourceId.value == id.value ) {
                return geometry_status_t::IDENTITY_CONFLICT;
            }
        }
    }

    const f64 normalTolerance =
        policy.numerical.fUnitNormalTolerance;
    const f64 coordinateLimit =
        policy.numerical.fCoordinateMagnitudeLimit;
    const f64 distanceTolerance =
        policy.numerical.fCoplanarDistanceTolerance;

    for ( common::usize i = 0u; i < cSides; ++i ) {
        const planed_t &plane = pBrush->sides.pData[i].plane;

        if ( !math::Planed_IsFinite( plane ) ) {
            return geometry_status_t::NUMERIC_FAILURE;
        }
        if ( !math::Planed_IsNormalized(
                 plane, normalTolerance ) ) {
            return geometry_status_t::DEGENERATE;
        }
        if ( !CoordinateIsWithinLimit( plane.d, coordinateLimit ) ) {
            return geometry_status_t::LIMIT_EXCEEDED;
        }

        for ( common::usize j = 0u; j < i; ++j ) {
            if ( ArePlanesConflicting(
                     plane,
                     pBrush->sides.pData[j].plane,
                     normalTolerance,
                     distanceTolerance ) ) {
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

    if ( !common::Allocator_IsValid( pAllocator ) ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }

    result.status = BrushValidation_Quick( pBrush, policy );
    if ( result.status != geometry_status_t::OK ) {
        return result;
    }

    brush_boundary_t boundary{};
    result.status = BrushBoundary_Init( &boundary, pAllocator );
    if ( result.status != geometry_status_t::OK ) {
        return result;
    }

    result.status = BrushBoundary_TryReconstruct(
        &boundary, pBrush, policy );
    if ( result.status != geometry_status_t::OK ) {
        BrushBoundary_Shutdown( &boundary );
        return result;
    }

    result.status = ValidateReconstructedBoundary(
        boundary, *pBrush, policy, &result );
    BrushBoundary_Shutdown( &boundary );

    if ( result.status == geometry_status_t::OK ) {
        result.bWatertight = true;
    }
    return result;
}

} // namespace cypher::editor::geometry
