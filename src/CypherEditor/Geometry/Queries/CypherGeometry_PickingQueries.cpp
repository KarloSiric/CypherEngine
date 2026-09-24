//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_PickingQueries.cpp
//  Purpose: Implements allocation-free component and planar-region picking.
//  Details: Inputs are validated completely before candidate traversal. The
//           implementation has no fixed corner arrays, heap allocation, or
//           silent malformed-record skips.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_PickingQueries.h"

#include "CypherGeometry_MeshValidation.h"
#include "CypherGeometry_PlanarRegionValidation.h"

#include <cmath>

namespace cypher::editor::geometry
{

namespace
{

using common::bool_t;
using common::f64;
using common::i64;
using common::u32;
using common::u64;
using common::usize;

geometry_status_t CoordinateStatus(
    math::vec3d_t value,
    const geometry_numerical_policy_t &policy ) noexcept
{
    if ( !math::Vec3d_IsFinite( value ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }
    if ( std::abs( value.x ) > policy.fCoordinateMagnitudeLimit ||
         std::abs( value.y ) > policy.fCoordinateMagnitudeLimit ||
         std::abs( value.z ) > policy.fCoordinateMagnitudeLimit ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    return geometry_status_t::OK;
}

geometry_status_t ValidateRayAndRange(
    geometry_raycast_ray_t ray,
    const geometry_ray_distance_range_t &range,
    const geometry_policy_t &policy ) noexcept
{
    if ( !GeometryPolicy_IsValid( policy ) ||
         CoordinateStatus( ray.origin, policy.numerical ) !=
             geometry_status_t::OK ||
         !math::Vec3d_IsUnitLength(
             ray.direction, policy.numerical.fUnitNormalTolerance ) ||
         !std::isfinite( range.fMinimumDistance ) ||
         !std::isfinite( range.fMaximumDistance ) ||
         range.fMinimumDistance < 0.0 ||
         range.fMaximumDistance < range.fMinimumDistance ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    return geometry_status_t::OK;
}

geometry_status_t ValidateProximityQuery(
    geometry_raycast_ray_t ray,
    const geometry_proximity_pick_options_t &options,
    const geometry_policy_t &policy ) noexcept
{
    const geometry_status_t rayStatus =
        ValidateRayAndRange( ray, options.rayRange, policy );
    if ( rayStatus != geometry_status_t::OK ||
         !std::isfinite( options.fMaximumProximity ) ||
         options.fMaximumProximity < 0.0 ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    return geometry_status_t::OK;
}

f64 AcceptedMinimumDistance(
    const geometry_ray_distance_range_t &range,
    const geometry_numerical_policy_t &policy ) noexcept
{
    return range.fMinimumDistance > policy.fAbsoluteDistanceTolerance
        ? range.fMinimumDistance
        : policy.fAbsoluteDistanceTolerance;
}

f64 ScaledDistanceTolerance(
    f64 a,
    f64 b,
    const geometry_numerical_policy_t &policy ) noexcept
{
    const f64 scale = std::abs( a ) > std::abs( b )
        ? std::abs( a )
        : std::abs( b );
    return policy.fAbsoluteDistanceTolerance +
           policy.fRelativeDistanceTolerance * scale;
}

f64 Clamp( f64 value, f64 minimum, f64 maximum ) noexcept
{
    if ( value < minimum ) {
        return minimum;
    }
    return value > maximum ? maximum : value;
}

geometry_status_t ClosestToPoint(
    geometry_raycast_ray_t ray,
    f64 fMinimumDistance,
    f64 fMaximumDistance,
    math::vec3d_t point,
    f64 *pRayDistanceOut,
    f64 *pProximityOut ) noexcept
{
    const math::vec3d_t toPoint =
        math::Vec3d_Subtract( point, ray.origin );
    const f64 projection = math::Vec3d_Dot( toPoint, ray.direction );
    if ( !std::isfinite( projection ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }

    const f64 rayDistance = Clamp(
        projection, fMinimumDistance, fMaximumDistance );
    const math::vec3d_t rayPoint = math::Vec3d_MulAdd(
        ray.origin, ray.direction, rayDistance );
    f64 proximity = 0.0;
    if ( !math::Vec3d_IsFinite( rayPoint ) ||
         !math::Vec3d_TryLength(
             math::Vec3d_Subtract( point, rayPoint ), &proximity ) ||
         !std::isfinite( proximity ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }
    *pRayDistanceOut = rayDistance;
    *pProximityOut = proximity;
    return geometry_status_t::OK;
}

geometry_status_t ClosestToSegment(
    geometry_raycast_ray_t ray,
    f64 fMinimumDistance,
    f64 fMaximumDistance,
    math::vec3d_t a,
    math::vec3d_t b,
    const geometry_numerical_policy_t &policy,
    f64 *pRayDistanceOut,
    f64 *pEdgeParameterOut,
    f64 *pProximityOut ) noexcept
{
    const math::vec3d_t segment = math::Vec3d_Subtract( b, a );
    const math::vec3d_t fromA = math::Vec3d_Subtract( ray.origin, a );
    const f64 segmentLengthSquared =
        math::Vec3d_Dot( segment, segment );
    const f64 directionDotSegment =
        math::Vec3d_Dot( ray.direction, segment );
    const f64 directionDotFromA =
        math::Vec3d_Dot( ray.direction, fromA );
    const f64 segmentDotFromA = math::Vec3d_Dot( segment, fromA );
    if ( !std::isfinite( segmentLengthSquared ) ||
         !std::isfinite( directionDotSegment ) ||
         !std::isfinite( directionDotFromA ) ||
         !std::isfinite( segmentDotFromA ) ||
         !( segmentLengthSquared > 0.0 ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }

    const f64 denominator = segmentLengthSquared -
        directionDotSegment * directionDotSegment;
    const f64 fParallelSine =
        std::sin( policy.fAngularToleranceRadians );
    const f64 fParallelThreshold =
        segmentLengthSquared * fParallelSine * fParallelSine;

    f64 rayDistance = fMinimumDistance;
    if ( denominator > fParallelThreshold ) {
        rayDistance = Clamp(
            ( directionDotSegment * segmentDotFromA -
              directionDotFromA * segmentLengthSquared ) /
                denominator,
            fMinimumDistance,
            fMaximumDistance );
    }

    f64 edgeParameter =
        ( directionDotSegment * rayDistance + segmentDotFromA ) /
        segmentLengthSquared;
    if ( edgeParameter < 0.0 ) {
        edgeParameter = 0.0;
        rayDistance = Clamp(
            -directionDotFromA,
            fMinimumDistance,
            fMaximumDistance );
    } else if ( edgeParameter > 1.0 ) {
        edgeParameter = 1.0;
        rayDistance = Clamp(
            directionDotSegment - directionDotFromA,
            fMinimumDistance,
            fMaximumDistance );
    }

    const math::vec3d_t rayPoint = math::Vec3d_MulAdd(
        ray.origin, ray.direction, rayDistance );
    const math::vec3d_t edgePoint = math::Vec3d_MulAdd(
        a, segment, edgeParameter );
    f64 proximity = 0.0;
    if ( !math::Vec3d_IsFinite( rayPoint ) ||
         !math::Vec3d_IsFinite( edgePoint ) ||
         !math::Vec3d_TryLength(
             math::Vec3d_Subtract( edgePoint, rayPoint ),
             &proximity ) ||
         !std::isfinite( proximity ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }

    *pRayDistanceOut = rayDistance;
    *pEdgeParameterOut = edgeParameter;
    *pProximityOut = proximity;
    return geometry_status_t::OK;
}

bool MetricsPrecede(
    f64 fProximity,
    f64 fRayDistance,
    f64 fBestProximity,
    f64 fBestRayDistance,
    bool bHaveBest,
    const geometry_numerical_policy_t &policy ) noexcept
{
    if ( !bHaveBest ) {
        return true;
    }
    const f64 proximityTolerance = ScaledDistanceTolerance(
        fProximity, fBestProximity, policy );
    if ( fProximity < fBestProximity - proximityTolerance ) {
        return true;
    }
    if ( std::abs( fProximity - fBestProximity ) >
         proximityTolerance ) {
        return false;
    }

    const f64 rayTolerance = ScaledDistanceTolerance(
        fRayDistance, fBestRayDistance, policy );
    if ( fRayDistance < fBestRayDistance - rayTolerance ) {
        return true;
    }
    return std::abs( fRayDistance - fBestRayDistance ) <= rayTolerance;
}

bool BoundaryIsInitializedAndValid(
    const brush_boundary_t *pBoundary ) noexcept
{
    if ( pBoundary == nullptr ||
         pBoundary->vertices.pAllocator == nullptr ) {
        return false;
    }
    const common::allocator_t *pAllocator =
        pBoundary->vertices.pAllocator;
    return common::Allocator_IsValid( pAllocator ) &&
           pBoundary->edges.pAllocator == pAllocator &&
           pBoundary->faces.pAllocator == pAllocator &&
           pBoundary->faceVertexIndices.pAllocator == pAllocator &&
           common::Vector_IsValid( &pBoundary->vertices ) &&
           common::Vector_IsValid( &pBoundary->edges ) &&
           common::Vector_IsValid( &pBoundary->faces ) &&
           common::Vector_IsValid( &pBoundary->faceVertexIndices );
}

geometry_status_t ValidateBoundary(
    const brush_boundary_t *pBoundary,
    const geometry_policy_t &policy ) noexcept
{
    if ( pBoundary == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pBoundary->vertices.pAllocator == nullptr ||
         pBoundary->edges.pAllocator == nullptr ||
         pBoundary->faces.pAllocator == nullptr ||
         pBoundary->faceVertexIndices.pAllocator == nullptr ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !BoundaryIsInitializedAndValid( pBoundary ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    const usize cVertices = pBoundary->vertices.nCount;
    const usize cEdges = pBoundary->edges.nCount;
    const usize cFaces = pBoundary->faces.nCount;
    const usize cPacked = pBoundary->faceVertexIndices.nCount;
    if ( static_cast<u64>( cVertices ) > policy.limits.cVerticesMax ||
         static_cast<u64>( cEdges ) > policy.limits.cEdgesMax ||
         static_cast<u64>( cFaces ) > policy.limits.cFacesMax ||
         static_cast<u64>( cFaces ) >
             policy.limits.cBrushSidesPerBrushMax ||
         static_cast<u64>( cPacked ) > policy.limits.cHalfEdgesMax ||
         cVertices > static_cast<usize>( common::CY_U32_MAX ) ||
         cEdges > static_cast<usize>( common::CY_U32_MAX ) ||
         cFaces > static_cast<usize>( common::CY_U32_MAX ) ||
         cPacked > static_cast<usize>( common::CY_U32_MAX ) ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    const bool bEmpty = cVertices == 0u && cEdges == 0u &&
                        cFaces == 0u && cPacked == 0u;
    if ( bEmpty ) {
        return geometry_status_t::OK;
    }
    if ( cVertices < 4u || cEdges < 6u || cFaces < 4u ) {
        return geometry_status_t::INVALID_TOPOLOGY;
    }

    // A canonical convex brush boundary is a genus-zero polyhedron. These
    // bounds keep the deeper allocation-free incidence audit predictably
    // bounded by cBrushSidesPerBrushMax.
    const usize cVertexBound = 2u * cFaces - 4u;
    const usize cEdgeBound = 3u * cFaces - 6u;
    if ( cVertices > cVertexBound || cEdges > cEdgeBound ||
         cPacked != 2u * cEdges ||
         cVertices > static_cast<usize>( common::CY_I64_MAX ) ||
         cEdges > static_cast<usize>( common::CY_I64_MAX ) ||
         cFaces > static_cast<usize>( common::CY_I64_MAX ) ) {
        return geometry_status_t::INVALID_TOPOLOGY;
    }
    const i64 euler = static_cast<i64>( cVertices ) -
                      static_cast<i64>( cEdges ) +
                      static_cast<i64>( cFaces );
    if ( euler != 2 ) {
        return geometry_status_t::INVALID_TOPOLOGY;
    }

    for ( usize iVertex = 0u; iVertex < cVertices; ++iVertex ) {
        const geometry_status_t coordinateStatus = CoordinateStatus(
            pBoundary->vertices.pData[iVertex], policy.numerical );
        if ( coordinateStatus != geometry_status_t::OK ) {
            return coordinateStatus;
        }
    }

    for ( usize iEdge = 0u; iEdge < cEdges; ++iEdge ) {
        const brush_boundary_edge_t &edge =
            pBoundary->edges.pData[iEdge];
        if ( edge.iVertex0 >= cVertices ||
             edge.iVertex1 >= cVertices ||
             edge.iVertex0 >= edge.iVertex1 ) {
            return geometry_status_t::INVALID_TOPOLOGY;
        }
        for ( usize iPrevious = 0u; iPrevious < iEdge; ++iPrevious ) {
            const brush_boundary_edge_t &previous =
                pBoundary->edges.pData[iPrevious];
            if ( edge.iVertex0 == previous.iVertex0 &&
                 edge.iVertex1 == previous.iVertex1 ) {
                return geometry_status_t::INVALID_TOPOLOGY;
            }
        }
        f64 fLength = 0.0;
        if ( !math::Vec3d_TryLength(
                 math::Vec3d_Subtract(
                     pBoundary->vertices.pData[edge.iVertex1],
                     pBoundary->vertices.pData[edge.iVertex0] ),
                 &fLength ) ) {
            return geometry_status_t::NUMERIC_FAILURE;
        }
        if ( fLength < policy.numerical.fMinimumEdgeLength ) {
            return geometry_status_t::DEGENERATE;
        }
    }

    usize iExpectedFirst = 0u;
    for ( usize iFace = 0u; iFace < cFaces; ++iFace ) {
        const brush_boundary_face_t &face =
            pBoundary->faces.pData[iFace];
        const usize iFirst = face.iFirstIndex;
        const usize cFaceVertices = face.cVertices;
        if ( face.iSide == CY_INVALID_INDEX ||
             cFaceVertices < 3u ||
             static_cast<u64>( cFaceVertices ) >
                 policy.limits.cTraversalDepthMax ||
             iFirst != iExpectedFirst ||
             iFirst > cPacked ||
             cFaceVertices > cPacked - iFirst ) {
            return geometry_status_t::INVALID_TOPOLOGY;
        }
        for ( usize iPreviousFace = 0u;
              iPreviousFace < iFace;
              ++iPreviousFace ) {
            if ( pBoundary->faces.pData[iPreviousFace].iSide ==
                 face.iSide ) {
                return geometry_status_t::INVALID_TOPOLOGY;
            }
        }

        for ( usize i = 0u; i < cFaceVertices; ++i ) {
            const u32 iVertex =
                pBoundary->faceVertexIndices.pData[iFirst + i];
            const u32 iNext = pBoundary->faceVertexIndices.pData[
                iFirst + ( i + 1u ) % cFaceVertices];
            if ( iVertex >= cVertices || iNext >= cVertices ||
                 iVertex == iNext ) {
                return geometry_status_t::INVALID_TOPOLOGY;
            }
            for ( usize iPrevious = 0u; iPrevious < i; ++iPrevious ) {
                if ( pBoundary->faceVertexIndices.pData[
                         iFirst + iPrevious] == iVertex ) {
                    return geometry_status_t::INVALID_TOPOLOGY;
                }
            }

            const u32 iLow = iVertex < iNext ? iVertex : iNext;
            const u32 iHigh = iVertex < iNext ? iNext : iVertex;
            bool bFoundEdge = false;
            for ( usize iEdge = 0u; iEdge < cEdges; ++iEdge ) {
                const brush_boundary_edge_t &edge =
                    pBoundary->edges.pData[iEdge];
                if ( edge.iVertex0 == iLow && edge.iVertex1 == iHigh ) {
                    bFoundEdge = true;
                    break;
                }
            }
            if ( !bFoundEdge ) {
                return geometry_status_t::INVALID_TOPOLOGY;
            }
        }
        iExpectedFirst += cFaceVertices;
    }
    if ( iExpectedFirst != cPacked ) {
        return geometry_status_t::INVALID_TOPOLOGY;
    }

    for ( usize iEdge = 0u; iEdge < cEdges; ++iEdge ) {
        const brush_boundary_edge_t &edge =
            pBoundary->edges.pData[iEdge];
        usize cUses = 0u;
        for ( usize iFace = 0u; iFace < cFaces; ++iFace ) {
            const brush_boundary_face_t &face =
                pBoundary->faces.pData[iFace];
            const usize iFirst = face.iFirstIndex;
            const usize cFaceVertices = face.cVertices;
            for ( usize i = 0u; i < cFaceVertices; ++i ) {
                const u32 iA = pBoundary->faceVertexIndices.pData[
                    iFirst + i];
                const u32 iB = pBoundary->faceVertexIndices.pData[
                    iFirst + ( i + 1u ) % cFaceVertices];
                const u32 iLow = iA < iB ? iA : iB;
                const u32 iHigh = iA < iB ? iB : iA;
                if ( edge.iVertex0 == iLow &&
                     edge.iVertex1 == iHigh ) {
                    ++cUses;
                }
            }
        }
        if ( cUses != 2u ) {
            return geometry_status_t::INVALID_TOPOLOGY;
        }
    }

    for ( usize iVertex = 0u; iVertex < cVertices; ++iVertex ) {
        bool bReferenced = false;
        for ( usize i = 0u; i < cPacked; ++i ) {
            if ( pBoundary->faceVertexIndices.pData[i] == iVertex ) {
                bReferenced = true;
                break;
            }
        }
        if ( !bReferenced ) {
            return geometry_status_t::INVALID_TOPOLOGY;
        }
    }
    return geometry_status_t::OK;
}

bool MeshPoolsAreValid( const editable_mesh_t *pMesh ) noexcept
{
    return common::GenerationPool_IsValid( &pMesh->vertices ) &&
           common::GenerationPool_IsValid( &pMesh->halfEdges ) &&
           common::GenerationPool_IsValid( &pMesh->edges ) &&
           common::GenerationPool_IsValid( &pMesh->loops ) &&
           common::GenerationPool_IsValid( &pMesh->faces ) &&
           common::GenerationPool_IsValid( &pMesh->shells );
}

geometry_status_t ValidateMesh(
    const editable_mesh_t *pMesh,
    const geometry_policy_t &policy ) noexcept
{
    if ( pMesh == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !EditableMesh_IsInitialized( pMesh ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !common::Allocator_IsValid( pMesh->pAllocator ) ||
         !MeshPoolsAreValid( pMesh ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    const usize cVertices = EditableMesh_VertexCount( pMesh );
    const usize cHalfEdges = EditableMesh_HalfEdgeCount( pMesh );
    const usize cEdges = EditableMesh_EdgeCount( pMesh );
    const usize cLoops = EditableMesh_LoopCount( pMesh );
    const usize cFaces = EditableMesh_FaceCount( pMesh );
    const usize cShells = EditableMesh_ShellCount( pMesh );
    if ( static_cast<u64>( cVertices ) > policy.limits.cVerticesMax ||
         static_cast<u64>( cHalfEdges ) > policy.limits.cHalfEdgesMax ||
         static_cast<u64>( cEdges ) > policy.limits.cEdgesMax ||
         static_cast<u64>( cLoops ) > policy.limits.cLoopsMax ||
         static_cast<u64>( cFaces ) > policy.limits.cFacesMax ||
         static_cast<u64>( cShells ) > policy.limits.cShellsMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    const bool bEmpty = cVertices == 0u && cHalfEdges == 0u &&
                        cEdges == 0u && cLoops == 0u &&
                        cFaces == 0u && cShells == 0u;
    if ( bEmpty ) {
        return geometry_status_t::OK;
    }
    if ( cVertices == 0u || cHalfEdges == 0u || cEdges == 0u ||
         cLoops == 0u || cFaces == 0u || cShells == 0u ) {
        return geometry_status_t::INVALID_TOPOLOGY;
    }

    geometry_status_t status = geometry_status_t::OK;
    (void)common::GenerationPool_ForEach(
        &pMesh->vertices,
        [&]( geometry_mesh_vertex_handle_t,
             const mesh_vertex_record_t &vertex ) noexcept -> bool_t {
            status = CoordinateStatus(
                vertex.position, policy.numerical );
            return status == geometry_status_t::OK;
        } );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    (void)common::GenerationPool_ForEach(
        &pMesh->faces,
        [&]( geometry_mesh_face_handle_t,
             const mesh_face_record_t &face ) noexcept -> bool_t {
            const mesh_loop_record_t *pLoop =
                common::GenerationPool_Get(
                    &pMesh->loops, face.hOuterLoop );
            if ( !math::Vec3d_IsUnitLength(
                     face.normal,
                     policy.numerical.fUnitNormalTolerance ) ) {
                status = geometry_status_t::NUMERIC_FAILURE;
                return false;
            }
            if ( pLoop == nullptr || pLoop->cHalfEdges < 3u ) {
                status = geometry_status_t::INVALID_TOPOLOGY;
                return false;
            }
            if ( static_cast<u64>( pLoop->cHalfEdges ) >
                 policy.limits.cTraversalDepthMax ) {
                status = geometry_status_t::LIMIT_EXCEEDED;
                return false;
            }
            return true;
        } );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    (void)common::GenerationPool_ForEach(
        &pMesh->edges,
        [&]( geometry_mesh_edge_handle_t,
             const mesh_edge_record_t &edge ) noexcept -> bool_t {
            if ( !std::isfinite( edge.creaseWeight ) ||
                 edge.creaseWeight < 0.0 || edge.creaseWeight > 1.0 ) {
                status = geometry_status_t::CORRUPT_STATE;
                return false;
            }
            return true;
        } );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    const mesh_validation_result_t structural =
        MeshValidation_Validate( pMesh );
    if ( structural.status != geometry_status_t::OK ) {
        return structural.status;
    }

    (void)common::GenerationPool_ForEach(
        &pMesh->edges,
        [&]( geometry_mesh_edge_handle_t,
             const mesh_edge_record_t &edge ) noexcept -> bool_t {
            const mesh_half_edge_record_t *pHalfEdge =
                common::GenerationPool_Get(
                    &pMesh->halfEdges, edge.hHalfEdge );
            const mesh_half_edge_record_t *pTwin =
                pHalfEdge != nullptr
                    ? common::GenerationPool_Get(
                          &pMesh->halfEdges, pHalfEdge->hTwin )
                    : nullptr;
            const mesh_vertex_record_t *pA =
                pHalfEdge != nullptr
                    ? common::GenerationPool_Get(
                          &pMesh->vertices, pHalfEdge->hOrigin )
                    : nullptr;
            const mesh_vertex_record_t *pB =
                pTwin != nullptr
                    ? common::GenerationPool_Get(
                          &pMesh->vertices, pTwin->hOrigin )
                    : nullptr;
            if ( pA == nullptr || pB == nullptr ) {
                status = geometry_status_t::INVALID_TOPOLOGY;
                return false;
            }
            f64 fLength = 0.0;
            if ( !math::Vec3d_TryLength(
                     math::Vec3d_Subtract(
                         pB->position, pA->position ), &fLength ) ) {
                status = geometry_status_t::NUMERIC_FAILURE;
                return false;
            }
            if ( fLength < policy.numerical.fMinimumEdgeLength ) {
                status = geometry_status_t::DEGENERATE;
                return false;
            }
            return true;
        } );
    return status;
}

geometry_status_t ValidatePlanarRegion(
    const planar_region_t *pRegion,
    const geometry_policy_t &policy ) noexcept
{
    if ( pRegion == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pRegion->points.pAllocator == nullptr ||
         pRegion->contours.pAllocator == nullptr ||
         pRegion->polygons.pAllocator == nullptr ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    const common::allocator_t *pAllocator =
        pRegion->points.pAllocator;
    if ( !common::Allocator_IsValid( pAllocator ) ||
         pRegion->contours.pAllocator != pAllocator ||
         pRegion->polygons.pAllocator != pAllocator ||
         !common::Vector_IsValid( &pRegion->points ) ||
         !common::Vector_IsValid( &pRegion->contours ) ||
         !common::Vector_IsValid( &pRegion->polygons ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    const usize cPoints = pRegion->points.nCount;
    const usize cContours = pRegion->contours.nCount;
    const usize cPolygons = pRegion->polygons.nCount;
    if ( static_cast<u64>( cPoints ) > policy.limits.cVerticesMax ||
         static_cast<u64>( cContours ) > policy.limits.cLoopsMax ||
         static_cast<u64>( cPolygons ) > policy.limits.cFacesMax ||
         cPoints > kPlanarRegionPointsMax ||
         cContours > kPlanarRegionContoursMax ||
         cPoints > static_cast<usize>( common::CY_U32_MAX ) ||
         cContours > static_cast<usize>( common::CY_U32_MAX ) ||
         cPolygons > static_cast<usize>( common::CY_U32_MAX ) ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    if ( cPoints != 0u &&
         static_cast<u64>( cPoints ) >
             policy.limits.cIntersectionEventsMax /
                 static_cast<u64>( cPoints ) ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    if ( !math::Vec3d_IsFinite( pRegion->frame.origin ) ||
         !math::Vec3d_IsFinite( pRegion->frame.u ) ||
         !math::Vec3d_IsFinite( pRegion->frame.v ) ||
         !math::Vec3d_IsFinite( pRegion->frame.normal ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }
    const geometry_status_t originStatus = CoordinateStatus(
        pRegion->frame.origin, policy.numerical );
    if ( originStatus != geometry_status_t::OK ) {
        return originStatus;
    }
    if ( !PlanarFrame_IsValid(
             pRegion->frame,
             policy.numerical.fUnitNormalTolerance ) ||
         !GeometrySourceId_IsValid( pRegion->sourceId ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    const bool bEmpty = cPoints == 0u && cContours == 0u &&
                        cPolygons == 0u;
    if ( bEmpty ) {
        return geometry_status_t::OK;
    }
    if ( cPoints == 0u || cContours == 0u || cPolygons == 0u ) {
        return geometry_status_t::INVALID_TOPOLOGY;
    }

    usize iExpectedContour = 0u;
    for ( usize iPolygon = 0u;
          iPolygon < cPolygons;
          ++iPolygon ) {
        const planar_region_polygon_t &polygon =
            pRegion->polygons.pData[iPolygon];
        if ( polygon.cContours == 0u ||
             polygon.iFirstContour != iExpectedContour ||
             polygon.iFirstContour > cContours ||
             polygon.cContours >
                 cContours - polygon.iFirstContour ||
             !GeometrySourceId_IsValid( polygon.sourceId ) ) {
            return geometry_status_t::INVALID_TOPOLOGY;
        }
        iExpectedContour += polygon.cContours;
    }
    if ( iExpectedContour != cContours ) {
        return geometry_status_t::INVALID_TOPOLOGY;
    }

    usize iExpectedPoint = 0u;
    for ( usize iContour = 0u;
          iContour < cContours;
          ++iContour ) {
        const planar_region_contour_t &contour =
            pRegion->contours.pData[iContour];
        if ( contour.cPoints < 3u ||
             contour.cPoints > kPlanarRegionContourPointsMax ||
             static_cast<u64>( contour.cPoints ) >
                 policy.limits.cTraversalDepthMax ||
             contour.iFirstPoint != iExpectedPoint ||
             contour.iFirstPoint > cPoints ||
             contour.cPoints > cPoints - contour.iFirstPoint ||
             !GeometrySourceId_IsValid( contour.sourceId ) ) {
            return geometry_status_t::INVALID_TOPOLOGY;
        }
        iExpectedPoint += contour.cPoints;
    }
    if ( iExpectedPoint != cPoints ) {
        return geometry_status_t::INVALID_TOPOLOGY;
    }

    for ( usize iPoint = 0u; iPoint < cPoints; ++iPoint ) {
        const math::vec2d_t point = pRegion->points.pData[iPoint];
        if ( !math::Vec2d_IsFinite( point ) ) {
            return geometry_status_t::NUMERIC_FAILURE;
        }
        if ( std::abs( point.x ) >
                 policy.numerical.fCoordinateMagnitudeLimit ||
             std::abs( point.y ) >
                 policy.numerical.fCoordinateMagnitudeLimit ) {
            return geometry_status_t::LIMIT_EXCEEDED;
        }
        const geometry_status_t worldStatus = CoordinateStatus(
            PlanarFrame_Unproject( pRegion->frame, point ),
            policy.numerical );
        if ( worldStatus != geometry_status_t::OK ) {
            return worldStatus;
        }
    }

    const planar_region_validation_t validation =
        PlanarRegion_Validate( pRegion, policy );
    return validation.status;
}

bool HandlePrecedes(
    geometry_mesh_vertex_handle_t candidate,
    geometry_mesh_vertex_handle_t current ) noexcept
{
    return candidate.nSlot < current.nSlot ||
           ( candidate.nSlot == current.nSlot &&
             candidate.nGeneration < current.nGeneration );
}

bool HandlePrecedes(
    geometry_mesh_edge_handle_t candidate,
    geometry_mesh_edge_handle_t current ) noexcept
{
    return candidate.nSlot < current.nSlot ||
           ( candidate.nSlot == current.nSlot &&
             candidate.nGeneration < current.nGeneration );
}

} // namespace

geometry_status_t BoundaryQueries_TryPickNearestVertex(
    const brush_boundary_t *pBoundary,
    geometry_raycast_ray_t ray,
    const geometry_proximity_pick_options_t &options,
    const geometry_policy_t &policy,
    boundary_vertex_pick_t *pPickOut ) noexcept
{
    if ( pPickOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pPickOut = {};

    geometry_status_t status =
        ValidateProximityQuery( ray, options, policy );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    status = ValidateBoundary( pBoundary, policy );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    const f64 fMinimum = AcceptedMinimumDistance(
        options.rayRange, policy.numerical );
    if ( fMinimum > options.rayRange.fMaximumDistance ) {
        return geometry_status_t::OK;
    }

    boundary_vertex_pick_t best{};
    for ( usize iVertex = 0u;
          iVertex < pBoundary->vertices.nCount;
          ++iVertex ) {
        f64 fRayDistance = 0.0;
        f64 fProximity = 0.0;
        status = ClosestToPoint(
            ray, fMinimum, options.rayRange.fMaximumDistance,
            pBoundary->vertices.pData[iVertex],
            &fRayDistance, &fProximity );
        if ( status != geometry_status_t::OK ) {
            return status;
        }
        if ( fProximity > options.fMaximumProximity ) {
            continue;
        }

        const u32 iCandidate = static_cast<u32>( iVertex );
        if ( !MetricsPrecede(
                 fProximity, fRayDistance,
                 best.fProximity, best.fRayDistance,
                 best.bHit, policy.numerical ) ) {
            continue;
        }
        if ( best.bHit ) {
            const f64 proximityTolerance = ScaledDistanceTolerance(
                fProximity, best.fProximity, policy.numerical );
            const f64 rayTolerance = ScaledDistanceTolerance(
                fRayDistance, best.fRayDistance, policy.numerical );
            const bool bMetricsTie =
                std::abs( fProximity - best.fProximity ) <=
                    proximityTolerance &&
                std::abs( fRayDistance - best.fRayDistance ) <=
                    rayTolerance;
            if ( bMetricsTie && iCandidate >= best.iVertex ) {
                continue;
            }
        }
        best.bHit = true;
        best.fRayDistance = fRayDistance;
        best.fProximity = fProximity;
        best.iVertex = iCandidate;
    }

    *pPickOut = best;
    return geometry_status_t::OK;
}

geometry_status_t BoundaryQueries_TryPickNearestEdge(
    const brush_boundary_t *pBoundary,
    geometry_raycast_ray_t ray,
    const geometry_proximity_pick_options_t &options,
    const geometry_policy_t &policy,
    boundary_edge_pick_t *pPickOut ) noexcept
{
    if ( pPickOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pPickOut = {};

    geometry_status_t status =
        ValidateProximityQuery( ray, options, policy );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    status = ValidateBoundary( pBoundary, policy );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    const f64 fMinimum = AcceptedMinimumDistance(
        options.rayRange, policy.numerical );
    if ( fMinimum > options.rayRange.fMaximumDistance ) {
        return geometry_status_t::OK;
    }

    boundary_edge_pick_t best{};
    for ( usize iEdge = 0u;
          iEdge < pBoundary->edges.nCount;
          ++iEdge ) {
        const brush_boundary_edge_t &edge =
            pBoundary->edges.pData[iEdge];
        f64 fRayDistance = 0.0;
        f64 fEdgeParameter = 0.0;
        f64 fProximity = 0.0;
        status = ClosestToSegment(
            ray, fMinimum, options.rayRange.fMaximumDistance,
            pBoundary->vertices.pData[edge.iVertex0],
            pBoundary->vertices.pData[edge.iVertex1],
            policy.numerical,
            &fRayDistance, &fEdgeParameter, &fProximity );
        if ( status != geometry_status_t::OK ) {
            return status;
        }
        if ( fProximity > options.fMaximumProximity ) {
            continue;
        }

        const u32 iCandidate = static_cast<u32>( iEdge );
        if ( !MetricsPrecede(
                 fProximity, fRayDistance,
                 best.fProximity, best.fRayDistance,
                 best.bHit, policy.numerical ) ) {
            continue;
        }
        if ( best.bHit ) {
            const f64 proximityTolerance = ScaledDistanceTolerance(
                fProximity, best.fProximity, policy.numerical );
            const f64 rayTolerance = ScaledDistanceTolerance(
                fRayDistance, best.fRayDistance, policy.numerical );
            const bool bMetricsTie =
                std::abs( fProximity - best.fProximity ) <=
                    proximityTolerance &&
                std::abs( fRayDistance - best.fRayDistance ) <=
                    rayTolerance;
            if ( bMetricsTie && iCandidate >= best.iEdge ) {
                continue;
            }
        }
        best.bHit = true;
        best.fRayDistance = fRayDistance;
        best.fProximity = fProximity;
        best.fEdgeParameter = fEdgeParameter;
        best.iEdge = iCandidate;
    }

    *pPickOut = best;
    return geometry_status_t::OK;
}

geometry_status_t MeshQueries_TryPickNearestVertex(
    const editable_mesh_t *pMesh,
    geometry_raycast_ray_t ray,
    const geometry_proximity_pick_options_t &options,
    const geometry_policy_t &policy,
    mesh_vertex_pick_t *pPickOut ) noexcept
{
    if ( pPickOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pPickOut = {};

    geometry_status_t status =
        ValidateProximityQuery( ray, options, policy );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    status = ValidateMesh( pMesh, policy );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    const f64 fMinimum = AcceptedMinimumDistance(
        options.rayRange, policy.numerical );
    if ( fMinimum > options.rayRange.fMaximumDistance ) {
        return geometry_status_t::OK;
    }

    mesh_vertex_pick_t best{};
    (void)common::GenerationPool_ForEach(
        &pMesh->vertices,
        [&]( geometry_mesh_vertex_handle_t hVertex,
             const mesh_vertex_record_t &vertex ) noexcept -> bool_t {
            f64 fRayDistance = 0.0;
            f64 fProximity = 0.0;
            status = ClosestToPoint(
                ray, fMinimum, options.rayRange.fMaximumDistance,
                vertex.position, &fRayDistance, &fProximity );
            if ( status != geometry_status_t::OK ) {
                return false;
            }
            if ( fProximity > options.fMaximumProximity ||
                 !MetricsPrecede(
                     fProximity, fRayDistance,
                     best.fProximity, best.fRayDistance,
                     best.bHit, policy.numerical ) ) {
                return true;
            }
            if ( best.bHit ) {
                const f64 proximityTolerance = ScaledDistanceTolerance(
                    fProximity, best.fProximity, policy.numerical );
                const f64 rayTolerance = ScaledDistanceTolerance(
                    fRayDistance, best.fRayDistance, policy.numerical );
                const bool bMetricsTie =
                    std::abs( fProximity - best.fProximity ) <=
                        proximityTolerance &&
                    std::abs( fRayDistance - best.fRayDistance ) <=
                        rayTolerance;
                if ( bMetricsTie &&
                     !HandlePrecedes( hVertex, best.hVertex ) ) {
                    return true;
                }
            }
            best.bHit = true;
            best.fRayDistance = fRayDistance;
            best.fProximity = fProximity;
            best.hVertex = hVertex;
            return true;
        } );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    *pPickOut = best;
    return geometry_status_t::OK;
}

geometry_status_t MeshQueries_TryPickNearestEdge(
    const editable_mesh_t *pMesh,
    geometry_raycast_ray_t ray,
    const geometry_proximity_pick_options_t &options,
    const geometry_policy_t &policy,
    mesh_edge_pick_t *pPickOut ) noexcept
{
    if ( pPickOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pPickOut = {};

    geometry_status_t status =
        ValidateProximityQuery( ray, options, policy );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    status = ValidateMesh( pMesh, policy );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    const f64 fMinimum = AcceptedMinimumDistance(
        options.rayRange, policy.numerical );
    if ( fMinimum > options.rayRange.fMaximumDistance ) {
        return geometry_status_t::OK;
    }

    mesh_edge_pick_t best{};
    (void)common::GenerationPool_ForEach(
        &pMesh->edges,
        [&]( geometry_mesh_edge_handle_t hEdge,
             const mesh_edge_record_t &edge ) noexcept -> bool_t {
            const mesh_half_edge_record_t *pHalfEdge =
                common::GenerationPool_Get(
                    &pMesh->halfEdges, edge.hHalfEdge );
            const mesh_half_edge_record_t *pTwin =
                common::GenerationPool_Get(
                    &pMesh->halfEdges, pHalfEdge->hTwin );
            const mesh_vertex_record_t *pA =
                common::GenerationPool_Get(
                    &pMesh->vertices, pHalfEdge->hOrigin );
            const mesh_vertex_record_t *pB =
                common::GenerationPool_Get(
                    &pMesh->vertices, pTwin->hOrigin );

            f64 fRayDistance = 0.0;
            f64 fEdgeParameter = 0.0;
            f64 fProximity = 0.0;
            status = ClosestToSegment(
                ray, fMinimum, options.rayRange.fMaximumDistance,
                pA->position, pB->position, policy.numerical,
                &fRayDistance, &fEdgeParameter, &fProximity );
            if ( status != geometry_status_t::OK ) {
                return false;
            }
            if ( fProximity > options.fMaximumProximity ||
                 !MetricsPrecede(
                     fProximity, fRayDistance,
                     best.fProximity, best.fRayDistance,
                     best.bHit, policy.numerical ) ) {
                return true;
            }
            if ( best.bHit ) {
                const f64 proximityTolerance = ScaledDistanceTolerance(
                    fProximity, best.fProximity, policy.numerical );
                const f64 rayTolerance = ScaledDistanceTolerance(
                    fRayDistance, best.fRayDistance, policy.numerical );
                const bool bMetricsTie =
                    std::abs( fProximity - best.fProximity ) <=
                        proximityTolerance &&
                    std::abs( fRayDistance - best.fRayDistance ) <=
                        rayTolerance;
                if ( bMetricsTie &&
                     !HandlePrecedes( hEdge, best.hEdge ) ) {
                    return true;
                }
            }
            best.bHit = true;
            best.fRayDistance = fRayDistance;
            best.fProximity = fProximity;
            best.fEdgeParameter = fEdgeParameter;
            best.hEdge = hEdge;
            return true;
        } );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    *pPickOut = best;
    return geometry_status_t::OK;
}

geometry_status_t PlanarRegionQueries_TryPick(
    const planar_region_t *pRegion,
    geometry_raycast_ray_t ray,
    const geometry_ray_distance_range_t &range,
    const geometry_policy_t &policy,
    planar_region_pick_t *pPickOut ) noexcept
{
    if ( pPickOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pPickOut = {};

    geometry_status_t status =
        ValidateRayAndRange( ray, range, policy );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    status = ValidatePlanarRegion( pRegion, policy );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    if ( pRegion->polygons.nCount == 0u ) {
        return geometry_status_t::OK;
    }

    const f64 fMinimum = AcceptedMinimumDistance(
        range, policy.numerical );
    if ( fMinimum > range.fMaximumDistance ) {
        return geometry_status_t::OK;
    }

    const f64 denominator = math::Vec3d_Dot(
        pRegion->frame.normal, ray.direction );
    const f64 fParallelThreshold =
        std::sin( policy.numerical.fAngularToleranceRadians );
    if ( !std::isfinite( denominator ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }
    if ( std::abs( denominator ) <= fParallelThreshold ) {
        return geometry_status_t::OK;
    }

    const f64 fDistance = math::Vec3d_Dot(
        pRegion->frame.normal,
        math::Vec3d_Subtract(
            pRegion->frame.origin, ray.origin ) ) /
        denominator;
    if ( !std::isfinite( fDistance ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }
    if ( fDistance < fMinimum ||
         fDistance > range.fMaximumDistance ) {
        return geometry_status_t::OK;
    }

    const math::vec3d_t position = math::Vec3d_MulAdd(
        ray.origin, ray.direction, fDistance );
    const geometry_status_t positionStatus = CoordinateStatus(
        position, policy.numerical );
    if ( positionStatus != geometry_status_t::OK ) {
        return positionStatus;
    }
    const math::vec2d_t positionInFrame =
        PlanarFrame_Project( pRegion->frame, position );
    if ( !math::Vec2d_IsFinite( positionInFrame ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }

    for ( usize iPolygon = 0u;
          iPolygon < pRegion->polygons.nCount;
          ++iPolygon ) {
        if ( PlanarRegion_PolygonContains(
                 pRegion, iPolygon, positionInFrame ) ==
             planar_containment_t::OUTSIDE ) {
            continue;
        }
        const planar_region_polygon_t &polygon =
            pRegion->polygons.pData[iPolygon];
        pPickOut->bHit = true;
        pPickOut->fDistance = fDistance;
        pPickOut->position = position;
        pPickOut->positionInFrame = positionInFrame;
        pPickOut->regionSourceId = pRegion->sourceId;
        pPickOut->polygonSourceId = polygon.sourceId;
        pPickOut->iPolygon = static_cast<u32>( iPolygon );
        return geometry_status_t::OK;
    }
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
