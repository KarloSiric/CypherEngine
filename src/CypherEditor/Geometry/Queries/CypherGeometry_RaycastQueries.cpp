//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_RaycastQueries.cpp
//  Purpose: Implements brute-force authoring ray casts for brushes and meshes.
//  Details: Brush polygons use their convex fan directly. Editable mesh faces
//           use the shared binary64 polygon ear clipper and explicit bounded
//           Geometry scratch so concave faces remain queryable without hidden
//           heap allocation or fixed-size working arrays.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_RaycastQueries.h"

#include "CypherGeometry_BrushValidation.h"
#include "CypherGeometry_MeshValidation.h"

#include "CypherMath_Polygon.h"

#include <cmath>
#include <limits>

namespace cypher::editor::geometry
{

namespace
{

using common::bool_t;
using common::f64;
using common::u32;
using common::u64;
using common::usize;

struct triangle_hit_t {
    f64 fDistance{ 0.0 };
    math::vec3d_t barycentric{};
};

struct mesh_query_profile_t {
    usize cMaximumFaceVertices{ 0u };
    usize cTriangles{ 0u };
};

bool IsCullModeValid( geometry_raycast_cull_mode_t mode ) noexcept
{
    return static_cast<common::u8>( mode ) <
           static_cast<common::u8>( geometry_raycast_cull_mode_t::COUNT );
}

bool CoordinateIsInPolicyRange(
    math::vec3d_t value,
    const geometry_numerical_policy_t &policy ) noexcept
{
    return math::Vec3d_IsFinite( value ) &&
           std::abs( value.x ) <= policy.fCoordinateMagnitudeLimit &&
           std::abs( value.y ) <= policy.fCoordinateMagnitudeLimit &&
           std::abs( value.z ) <= policy.fCoordinateMagnitudeLimit;
}

geometry_status_t ValidateQuery(
    geometry_raycast_ray_t ray,
    const geometry_raycast_options_t &options,
    const geometry_policy_t &policy ) noexcept
{
    if ( !GeometryPolicy_IsValid( policy ) ||
         !CoordinateIsInPolicyRange( ray.origin, policy.numerical ) ||
         !math::Vec3d_IsUnitLength(
             ray.direction, policy.numerical.fUnitNormalTolerance ) ||
         !std::isfinite( options.fMinimumDistance ) ||
         !std::isfinite( options.fMaximumDistance ) ||
         options.fMinimumDistance < 0.0 ||
         options.fMaximumDistance < options.fMinimumDistance ||
         !IsCullModeValid( options.cullMode ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    return geometry_status_t::OK;
}

f64 AcceptedMinimumDistance(
    const geometry_raycast_options_t &options,
    const geometry_numerical_policy_t &policy ) noexcept
{
    return options.fMinimumDistance > policy.fAbsoluteDistanceTolerance
        ? options.fMinimumDistance
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

bool TryIntersectTriangle(
    geometry_raycast_ray_t ray,
    const geometry_raycast_options_t &options,
    const geometry_numerical_policy_t &policy,
    math::vec3d_t a,
    math::vec3d_t b,
    math::vec3d_t c,
    triangle_hit_t *pHitOut,
    bool *pDegenerateOut ) noexcept
{
    *pHitOut = {};
    *pDegenerateOut = false;

    if ( !math::Vec3d_IsFinite( a ) ||
         !math::Vec3d_IsFinite( b ) ||
         !math::Vec3d_IsFinite( c ) ) {
        *pDegenerateOut = true;
        return false;
    }

    const math::vec3d_t edgeAB = math::Vec3d_Subtract( b, a );
    const math::vec3d_t edgeAC = math::Vec3d_Subtract( c, a );
    const math::vec3d_t areaVector = math::Vec3d_Cross( edgeAB, edgeAC );
    f64 fTwiceArea = 0.0;
    if ( !math::Vec3d_TryLength( areaVector, &fTwiceArea ) ||
         !std::isfinite( fTwiceArea ) ||
         fTwiceArea <= 2.0 * policy.fMinimumFaceArea ) {
        *pDegenerateOut = true;
        return false;
    }

    const math::vec3d_t p = math::Vec3d_Cross( ray.direction, edgeAC );
    const f64 determinant = math::Vec3d_Dot( edgeAB, p );
    const f64 fParallelThreshold =
        fTwiceArea * std::sin( policy.fAngularToleranceRadians );
    if ( !std::isfinite( determinant ) ||
         !std::isfinite( fParallelThreshold ) ) {
        return false;
    }

    if ( options.cullMode == geometry_raycast_cull_mode_t::NONE ) {
        if ( std::abs( determinant ) <= fParallelThreshold ) {
            return false;
        }
    } else if ( options.cullMode ==
                geometry_raycast_cull_mode_t::BACK_FACE ) {
        if ( determinant <= fParallelThreshold ) {
            return false;
        }
    } else if ( determinant >= -fParallelThreshold ) {
        return false;
    }

    const f64 inverseDeterminant = 1.0 / determinant;
    const math::vec3d_t fromA = math::Vec3d_Subtract( ray.origin, a );
    const f64 weightB = math::Vec3d_Dot( fromA, p ) * inverseDeterminant;
    const f64 fBarycentricTolerance =
        policy.fRelativeDistanceTolerance * 8.0;
    if ( !std::isfinite( weightB ) ||
         weightB < -fBarycentricTolerance ||
         weightB > 1.0 + fBarycentricTolerance ) {
        return false;
    }

    const math::vec3d_t q = math::Vec3d_Cross( fromA, edgeAB );
    const f64 weightC =
        math::Vec3d_Dot( ray.direction, q ) * inverseDeterminant;
    if ( !std::isfinite( weightC ) ||
         weightC < -fBarycentricTolerance ||
         weightB + weightC > 1.0 + fBarycentricTolerance ) {
        return false;
    }

    const f64 t = math::Vec3d_Dot( edgeAC, q ) * inverseDeterminant;
    const f64 fMinimum = AcceptedMinimumDistance( options, policy );
    if ( !std::isfinite( t ) || t < fMinimum ||
         t > options.fMaximumDistance ) {
        return false;
    }

    const f64 weightA = 1.0 - weightB - weightC;
    if ( !std::isfinite( weightA ) ) {
        return false;
    }

    pHitOut->fDistance = t;
    pHitOut->barycentric =
        math::Vec3d_Make( weightA, weightB, weightC );
    return true;
}

bool BrushCandidatePrecedes(
    f64 fDistance,
    geometry_source_id_t sideId,
    u32 iFace,
    u32 iTriangle,
    const brush_raycast_hit_t &current,
    const geometry_numerical_policy_t &policy ) noexcept
{
    if ( !current.bHit ) {
        return true;
    }
    const f64 tolerance = ScaledDistanceTolerance(
        fDistance, current.fDistance, policy );
    if ( fDistance < current.fDistance - tolerance ) {
        return true;
    }
    if ( std::abs( fDistance - current.fDistance ) > tolerance ) {
        return false;
    }
    if ( sideId.value != current.sideSourceId.value ) {
        return sideId.value < current.sideSourceId.value;
    }
    if ( iFace != current.iFace ) {
        return iFace < current.iFace;
    }
    return iTriangle < current.iTriangleInFace;
}

bool MeshCandidatePrecedes(
    f64 fDistance,
    geometry_mesh_face_handle_t hFace,
    u32 iTriangle,
    const mesh_raycast_hit_t &current,
    const geometry_numerical_policy_t &policy ) noexcept
{
    if ( !current.bHit ) {
        return true;
    }
    const f64 tolerance = ScaledDistanceTolerance(
        fDistance, current.fDistance, policy );
    if ( fDistance < current.fDistance - tolerance ) {
        return true;
    }
    if ( std::abs( fDistance - current.fDistance ) > tolerance ) {
        return false;
    }
    if ( hFace.nSlot != current.hFace.nSlot ) {
        return hFace.nSlot < current.hFace.nSlot;
    }
    if ( hFace.nGeneration != current.hFace.nGeneration ) {
        return hFace.nGeneration < current.hFace.nGeneration;
    }
    return iTriangle < current.iTriangleInFace;
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
    if ( !common::Allocator_IsValid( pAllocator ) ||
         pBoundary->edges.pAllocator != pAllocator ||
         pBoundary->faces.pAllocator != pAllocator ||
         pBoundary->faceVertexIndices.pAllocator != pAllocator ) {
        return false;
    }
    return common::Vector_IsValid( &pBoundary->vertices ) &&
           common::Vector_IsValid( &pBoundary->edges ) &&
           common::Vector_IsValid( &pBoundary->faces ) &&
           common::Vector_IsValid( &pBoundary->faceVertexIndices );
}

bool BoundaryCorrespondsToBrush(
    const brush_boundary_t *pBoundary,
    const brush_solid_t *pBrush,
    const geometry_numerical_policy_t &policy ) noexcept
{
    const usize cVertices = pBoundary->vertices.nCount;
    const usize cSides = pBrush->sides.nCount;
    for ( usize iVertex = 0u; iVertex < cVertices; ++iVertex ) {
        const math::vec3d_t vertex =
            pBoundary->vertices.pData[iVertex];
        if ( !CoordinateIsInPolicyRange( vertex, policy ) ) {
            return false;
        }
        for ( usize iSide = 0u; iSide < cSides; ++iSide ) {
            const f64 distance = math::Planed_SignedDistance(
                pBrush->sides.pData[iSide].plane, vertex );
            if ( !math::Scalar_IsFinite( distance ) ||
                 distance > policy.fCoplanarDistanceTolerance ) {
                return false;
            }
        }
    }

    for ( usize iEdge = 0u;
          iEdge < pBoundary->edges.nCount;
          ++iEdge ) {
        const brush_boundary_edge_t &edge =
            pBoundary->edges.pData[iEdge];
        if ( edge.iVertex0 >= cVertices ||
             edge.iVertex1 >= cVertices ||
             edge.iVertex0 >= edge.iVertex1 ) {
            return false;
        }
    }

    usize iExpectedFirst = 0u;
    const usize cPacked = pBoundary->faceVertexIndices.nCount;
    for ( usize iFace = 0u;
          iFace < pBoundary->faces.nCount;
          ++iFace ) {
        const brush_boundary_face_t &face =
            pBoundary->faces.pData[iFace];
        const usize iFirst = face.iFirstIndex;
        const usize cFaceVertices = face.cVertices;
        if ( static_cast<usize>( face.iSide ) != iFace ||
             cFaceVertices < 3u || iFirst != iExpectedFirst ||
             iFirst > cPacked || cFaceVertices > cPacked - iFirst ) {
            return false;
        }

        const math::planed_t sidePlane =
            pBrush->sides.pData[face.iSide].plane;
        for ( usize i = 0u; i < cFaceVertices; ++i ) {
            const u32 iVertex =
                pBoundary->faceVertexIndices.pData[iFirst + i];
            if ( iVertex >= cVertices ) {
                return false;
            }
            const f64 distance = math::Planed_SignedDistance(
                sidePlane, pBoundary->vertices.pData[iVertex] );
            if ( !math::Scalar_IsFinite( distance ) ||
                 math::Scalar_Abs( distance ) >
                     policy.fCoplanarDistanceTolerance ) {
                return false;
            }
        }
        iExpectedFirst += cFaceVertices;
    }
    return iExpectedFirst == cPacked;
}

geometry_status_t ValidateBrushRaycastInput(
    const brush_solid_t *pBrush,
    const brush_boundary_t *pBoundary,
    const geometry_policy_t &policy,
    usize *pTriangleCountOut ) noexcept
{
    *pTriangleCountOut = 0u;
    if ( pBrush == nullptr || pBoundary == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pBrush->sides.pAllocator == nullptr ||
         pBoundary->vertices.pAllocator == nullptr ||
         pBoundary->edges.pAllocator == nullptr ||
         pBoundary->faces.pAllocator == nullptr ||
         pBoundary->faceVertexIndices.pAllocator == nullptr ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !common::Vector_IsValid( &pBrush->sides ) ||
         !BoundaryIsInitializedAndValid( pBoundary ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    const geometry_status_t quick = BrushValidation_Quick( pBrush, policy );
    if ( quick != geometry_status_t::OK ) {
        return quick;
    }
    if ( !GeometrySourceId_IsValid( pBrush->sourceId ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    const usize cSides = pBrush->sides.nCount;
    const usize cVertices = pBoundary->vertices.nCount;
    const usize cEdges = pBoundary->edges.nCount;
    const usize cFaces = pBoundary->faces.nCount;
    const usize cPacked = pBoundary->faceVertexIndices.nCount;
    if ( cVertices < 4u || cEdges < 6u || cFaces < 4u ||
         cFaces != cSides ) {
        return geometry_status_t::INVALID_TOPOLOGY;
    }
    if ( static_cast<u64>( cSides ) >
             policy.limits.cBrushSidesPerBrushMax ||
         static_cast<u64>( cVertices ) > policy.limits.cVerticesMax ||
         static_cast<u64>( cEdges ) > policy.limits.cEdgesMax ||
         static_cast<u64>( cFaces ) > policy.limits.cFacesMax ||
         static_cast<u64>( cPacked ) > policy.limits.cHalfEdgesMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    for ( usize iVertex = 0u; iVertex < cVertices; ++iVertex ) {
        if ( !CoordinateIsInPolicyRange(
                 pBoundary->vertices.pData[iVertex], policy.numerical ) ) {
            return geometry_status_t::NUMERIC_FAILURE;
        }
    }
    for ( usize iEdge = 0u; iEdge < pBoundary->edges.nCount; ++iEdge ) {
        const brush_boundary_edge_t &edge = pBoundary->edges.pData[iEdge];
        if ( edge.iVertex0 >= cVertices || edge.iVertex1 >= cVertices ||
             edge.iVertex0 >= edge.iVertex1 ) {
            return geometry_status_t::CORRUPT_STATE;
        }
    }

    usize cTriangles = 0u;
    for ( usize iFace = 0u; iFace < cFaces; ++iFace ) {
        const brush_boundary_face_t &face = pBoundary->faces.pData[iFace];
        const usize iFirst = face.iFirstIndex;
        const usize cFaceVertices = face.cVertices;
        if ( face.iSide >= cSides || cFaceVertices < 3u ||
             iFirst > cPacked || cFaceVertices > cPacked - iFirst ) {
            return geometry_status_t::CORRUPT_STATE;
        }
        if ( !GeometrySourceId_IsValid(
                 pBrush->sides.pData[face.iSide].sourceId ) ) {
            return geometry_status_t::CORRUPT_STATE;
        }
        for ( usize iPrevious = 0u; iPrevious < iFace; ++iPrevious ) {
            if ( pBoundary->faces.pData[iPrevious].iSide == face.iSide ) {
                return geometry_status_t::CORRUPT_STATE;
            }
        }
        for ( usize i = 0u; i < cFaceVertices; ++i ) {
            const u32 iVertex =
                pBoundary->faceVertexIndices.pData[iFirst + i];
            const u32 iNext = pBoundary->faceVertexIndices.pData[
                iFirst + ( i + 1u ) % cFaceVertices];
            if ( iVertex >= cVertices || iVertex == iNext ) {
                return geometry_status_t::CORRUPT_STATE;
            }
        }
        const usize cFaceTriangles = cFaceVertices - 2u;
        if ( cTriangles > std::numeric_limits<usize>::max() -
                             cFaceTriangles ) {
            return geometry_status_t::LIMIT_EXCEEDED;
        }
        cTriangles += cFaceTriangles;
    }
    if ( static_cast<u64>( cTriangles ) >
         policy.limits.cIntersectionEventsMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    if ( !BoundaryCorrespondsToBrush(
             pBoundary, pBrush, policy.numerical ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pTriangleCountOut = cTriangles;
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

geometry_status_t ValidateMeshRaycastInput(
    const editable_mesh_t *pMesh,
    const geometry_policy_t &policy,
    mesh_query_profile_t *pProfileOut ) noexcept
{
    *pProfileOut = {};
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

    if ( cVertices == 0u && cHalfEdges == 0u && cEdges == 0u &&
         cLoops == 0u && cFaces == 0u && cShells == 0u ) {
        return geometry_status_t::OK;
    }
    if ( cVertices == 0u || cHalfEdges == 0u || cEdges == 0u ||
         cLoops == 0u || cFaces == 0u || cShells == 0u ) {
        return geometry_status_t::INVALID_TOPOLOGY;
    }

    bool bCoordinatesValid = true;
    (void)common::GenerationPool_ForEach(
        &pMesh->vertices,
        [&]( geometry_mesh_vertex_handle_t,
             const mesh_vertex_record_t &vertex ) noexcept -> bool_t {
            bCoordinatesValid = CoordinateIsInPolicyRange(
                vertex.position, policy.numerical );
            return bCoordinatesValid;
        } );
    if ( !bCoordinatesValid ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }

    bool bNormalsValid = true;
    (void)common::GenerationPool_ForEach(
        &pMesh->faces,
        [&]( geometry_mesh_face_handle_t,
             const mesh_face_record_t &face ) noexcept -> bool_t {
            bNormalsValid = math::Vec3d_IsUnitLength(
                face.normal, policy.numerical.fUnitNormalTolerance );
            return bNormalsValid;
        } );
    if ( !bNormalsValid ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }

    const mesh_validation_result_t validation =
        MeshValidation_Validate( pMesh );
    if ( validation.status != geometry_status_t::OK ) {
        return validation.status;
    }

    mesh_query_profile_t profile{};
    geometry_status_t status = geometry_status_t::OK;
    (void)common::GenerationPool_ForEach(
        &pMesh->faces,
        [&]( geometry_mesh_face_handle_t,
             const mesh_face_record_t &face ) noexcept -> bool_t {
            const mesh_loop_record_t *pLoop =
                common::GenerationPool_Get(
                    &pMesh->loops, face.hOuterLoop );
            if ( pLoop == nullptr || pLoop->cHalfEdges < 3u ) {
                status = geometry_status_t::INVALID_TOPOLOGY;
                return false;
            }
            const usize cFaceVertices = pLoop->cHalfEdges;
            if ( static_cast<u64>( cFaceVertices ) >
                     policy.limits.cTraversalDepthMax ||
                 static_cast<u64>( cFaceVertices ) >
                     policy.limits.cHalfEdgesMax ) {
                status = geometry_status_t::LIMIT_EXCEEDED;
                return false;
            }
            if ( cFaceVertices > profile.cMaximumFaceVertices ) {
                profile.cMaximumFaceVertices = cFaceVertices;
            }
            const usize cFaceTriangles = cFaceVertices - 2u;
            if ( profile.cTriangles >
                 std::numeric_limits<usize>::max() - cFaceTriangles ) {
                status = geometry_status_t::LIMIT_EXCEEDED;
                return false;
            }
            profile.cTriangles += cFaceTriangles;
            if ( static_cast<u64>( profile.cTriangles ) >
                 policy.limits.cIntersectionEventsMax ) {
                status = geometry_status_t::LIMIT_EXCEEDED;
                return false;
            }
            return true;
        } );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    *pProfileOut = profile;
    return geometry_status_t::OK;
}

bool TryMultiply( usize a, usize b, usize *pResult ) noexcept
{
    if ( a != 0u && b > std::numeric_limits<usize>::max() / a ) {
        *pResult = 0u;
        return false;
    }
    *pResult = a * b;
    return true;
}

bool TryAdd( usize a, usize b, usize *pResult ) noexcept
{
    if ( b > std::numeric_limits<usize>::max() - a ) {
        *pResult = 0u;
        return false;
    }
    *pResult = a + b;
    return true;
}

template <typename type_t>
bool TryAddScratchArray(
    usize nCount,
    usize *pBytes ) noexcept
{
    usize cbArray = 0u;
    if ( !common::Cy_TryArrayByteCount<type_t>( nCount, cbArray ) ) {
        return false;
    }
    usize cbWithPadding = 0u;
    if ( !TryAdd( cbArray, alignof( type_t ) - 1u,
                  &cbWithPadding ) ) {
        return false;
    }
    return TryAdd( *pBytes, cbWithPadding, pBytes );
}

geometry_status_t TryComputeScratchSize(
    const mesh_query_profile_t &profile,
    const geometry_policy_t &policy,
    usize *pBytesOut ) noexcept
{
    *pBytesOut = 0u;
    if ( profile.cMaximumFaceVertices == 0u ) {
        return geometry_status_t::OK;
    }

    usize cOutputIndices = 0u;
    if ( !TryMultiply(
             profile.cMaximumFaceVertices - 2u, 3u,
             &cOutputIndices ) ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    usize cbScratch = 0u;
    if ( !TryAddScratchArray<math::vec3d_t>(
             profile.cMaximumFaceVertices, &cbScratch ) ||
         !TryAddScratchArray<geometry_mesh_vertex_handle_t>(
             profile.cMaximumFaceVertices, &cbScratch ) ||
         !TryAddScratchArray<math::vec2d_t>(
             profile.cMaximumFaceVertices, &cbScratch ) ||
         !TryAddScratchArray<u32>(
             profile.cMaximumFaceVertices, &cbScratch ) ||
         !TryAddScratchArray<u32>( cOutputIndices, &cbScratch ) ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    if ( static_cast<u64>( cbScratch ) > policy.limits.cbScratchMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    *pBytesOut = cbScratch;
    return geometry_status_t::OK;
}

geometry_status_t MapTriangulationStatus(
    math::polygon_triangulation_status_t status ) noexcept
{
    switch ( status ) {
        case math::polygon_triangulation_status_t::OK:
            return geometry_status_t::OK;
        case math::polygon_triangulation_status_t::INVALID_ARGUMENT:
            return geometry_status_t::CORRUPT_STATE;
        case math::polygon_triangulation_status_t::DEGENERATE:
            return geometry_status_t::DEGENERATE;
        case math::polygon_triangulation_status_t::NOT_SIMPLE:
            return geometry_status_t::SELF_INTERSECTING;
        case math::polygon_triangulation_status_t::INSUFFICIENT_OUTPUT:
        case math::polygon_triangulation_status_t::INSUFFICIENT_SCRATCH:
            return geometry_status_t::CORRUPT_STATE;
        case math::polygon_triangulation_status_t::COUNT:
            break;
    }
    return geometry_status_t::CORRUPT_STATE;
}

} // namespace

geometry_status_t BrushQueries_TryRaycast(
    const brush_solid_t *pBrush,
    const brush_boundary_t *pBoundary,
    geometry_raycast_ray_t ray,
    const geometry_raycast_options_t &options,
    const geometry_policy_t &policy,
    brush_raycast_hit_t *pHitOut ) noexcept
{
    if ( pHitOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pHitOut = {};

    const geometry_status_t queryStatus =
        ValidateQuery( ray, options, policy );
    if ( queryStatus != geometry_status_t::OK ) {
        return queryStatus;
    }

    usize cTriangles = 0u;
    const geometry_status_t inputStatus = ValidateBrushRaycastInput(
        pBrush, pBoundary, policy, &cTriangles );
    if ( inputStatus != geometry_status_t::OK ) {
        return inputStatus;
    }
    (void)cTriangles;

    brush_raycast_hit_t best{};
    for ( usize iFace = 0u; iFace < pBoundary->faces.nCount; ++iFace ) {
        const brush_boundary_face_t &face =
            pBoundary->faces.pData[iFace];
        const brush_solid_side_t &side =
            pBrush->sides.pData[face.iSide];
        const u32 *pRing =
            pBoundary->faceVertexIndices.pData + face.iFirstIndex;
        const u32 iRoot = pRing[0];
        bool bFaceHasArea = false;

        for ( u32 iTriangle = 0u;
              iTriangle + 2u < face.cVertices;
              ++iTriangle ) {
            const u32 iB = pRing[iTriangle + 1u];
            const u32 iC = pRing[iTriangle + 2u];
            const math::vec3d_t a = pBoundary->vertices.pData[iRoot];
            const math::vec3d_t b = pBoundary->vertices.pData[iB];
            const math::vec3d_t c = pBoundary->vertices.pData[iC];

            const math::vec3d_t areaVector = math::Vec3d_Cross(
                math::Vec3d_Subtract( b, a ),
                math::Vec3d_Subtract( c, a ) );
            f64 fTwiceArea = 0.0;
            if ( !math::Vec3d_TryLength( areaVector, &fTwiceArea ) ) {
                return geometry_status_t::NUMERIC_FAILURE;
            }
            if ( fTwiceArea <= 2.0 * policy.numerical.fMinimumFaceArea ) {
                continue;
            }
            bFaceHasArea = true;
            if ( math::Vec3d_Dot( areaVector, side.plane.normal ) <= 0.0 ) {
                return geometry_status_t::INVALID_TOPOLOGY;
            }

            triangle_hit_t triangleHit{};
            bool bDegenerate = false;
            if ( !TryIntersectTriangle(
                     ray, options, policy.numerical,
                     a, b, c, &triangleHit, &bDegenerate ) ) {
                if ( bDegenerate ) {
                    continue;
                }
                continue;
            }
            if ( !BrushCandidatePrecedes(
                     triangleHit.fDistance, side.sourceId,
                     static_cast<u32>( iFace ), iTriangle,
                     best, policy.numerical ) ) {
                continue;
            }

            const math::vec3d_t position = math::Vec3d_MulAdd(
                ray.origin, ray.direction, triangleHit.fDistance );
            if ( !math::Vec3d_IsFinite( position ) ) {
                return geometry_status_t::NUMERIC_FAILURE;
            }

            best = {};
            best.bHit = true;
            best.fDistance = triangleHit.fDistance;
            best.position = position;
            best.normal = side.plane.normal;
            best.barycentric = triangleHit.barycentric;
            best.brushSourceId = pBrush->sourceId;
            best.sideSourceId = side.sourceId;
            best.iFace = static_cast<u32>( iFace );
            best.iSide = face.iSide;
            best.iTriangleInFace = iTriangle;
            best.iVertex0 = iRoot;
            best.iVertex1 = iB;
            best.iVertex2 = iC;
        }
        if ( !bFaceHasArea ) {
            return geometry_status_t::DEGENERATE;
        }
    }

    *pHitOut = best;
    return geometry_status_t::OK;
}

geometry_status_t MeshQueries_TryGetRaycastScratchSize(
    const editable_mesh_t *pMesh,
    const geometry_policy_t &policy,
    usize *pBytesOut ) noexcept
{
    if ( pBytesOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pBytesOut = 0u;
    if ( !GeometryPolicy_IsValid( policy ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    mesh_query_profile_t profile{};
    const geometry_status_t validateStatus =
        ValidateMeshRaycastInput( pMesh, policy, &profile );
    if ( validateStatus != geometry_status_t::OK ) {
        return validateStatus;
    }
    return TryComputeScratchSize( profile, policy, pBytesOut );
}

geometry_status_t MeshQueries_TryRaycast(
    const editable_mesh_t *pMesh,
    geometry_source_id_t meshSourceId,
    geometry_raycast_ray_t ray,
    const geometry_raycast_options_t &options,
    const geometry_policy_t &policy,
    geometry_scratch_t *pScratch,
    mesh_raycast_hit_t *pHitOut ) noexcept
{
    if ( pHitOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pHitOut = {};

    const geometry_status_t queryStatus =
        ValidateQuery( ray, options, policy );
    if ( queryStatus != geometry_status_t::OK ||
         !GeometrySourceId_IsValid( meshSourceId ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    mesh_query_profile_t profile{};
    geometry_status_t status =
        ValidateMeshRaycastInput( pMesh, policy, &profile );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    if ( profile.cMaximumFaceVertices == 0u ) {
        return geometry_status_t::OK;
    }
    if ( pScratch == nullptr ||
         !GeometryScratch_IsInitialized( pScratch ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }

    usize cbRequired = 0u;
    status = TryComputeScratchSize( profile, policy, &cbRequired );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    geometry_scratch_stats_t scratchStats{};
    status = GeometryScratch_QueryStats( pScratch, &scratchStats );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    if ( cbRequired > scratchStats.cbRemaining ) {
        return geometry_status_t::INSUFFICIENT_CAPACITY;
    }

    geometry_scratch_mark_t mark{};
    status = GeometryScratch_Mark( pScratch, &mark );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    const auto finish = [&]( geometry_status_t operationStatus ) noexcept {
        const geometry_status_t rewindStatus =
            GeometryScratch_Rewind( pScratch, mark );
        return rewindStatus == geometry_status_t::OK
            ? operationStatus
            : rewindStatus;
    };

    const usize cMaximum = profile.cMaximumFaceVertices;
    usize cOutputIndices = 0u;
    if ( !TryMultiply( cMaximum - 2u, 3u, &cOutputIndices ) ) {
        return finish( geometry_status_t::LIMIT_EXCEEDED );
    }

    math::vec3d_t *pPositions = nullptr;
    geometry_mesh_vertex_handle_t *pVertexHandles = nullptr;
    math::vec2d_t *pProjected = nullptr;
    u32 *pIndexScratch = nullptr;
    u32 *pTriangleIndices = nullptr;
    status = GeometryScratch_AllocateArrayStorage(
        pScratch, cMaximum, &pPositions );
    if ( status == geometry_status_t::OK ) {
        status = GeometryScratch_AllocateArrayStorage(
            pScratch, cMaximum, &pVertexHandles );
    }
    if ( status == geometry_status_t::OK ) {
        status = GeometryScratch_AllocateArrayStorage(
            pScratch, cMaximum, &pProjected );
    }
    if ( status == geometry_status_t::OK ) {
        status = GeometryScratch_AllocateArrayStorage(
            pScratch, cMaximum, &pIndexScratch );
    }
    if ( status == geometry_status_t::OK ) {
        status = GeometryScratch_AllocateArrayStorage(
            pScratch, cOutputIndices, &pTriangleIndices );
    }
    if ( status != geometry_status_t::OK ) {
        return finish( status );
    }

    mesh_raycast_hit_t best{};
    (void)common::GenerationPool_ForEach(
        &pMesh->faces,
        [&]( geometry_mesh_face_handle_t hFace,
             const mesh_face_record_t &face ) noexcept -> bool_t {
            const mesh_loop_record_t *pLoop =
                common::GenerationPool_Get(
                    &pMesh->loops, face.hOuterLoop );
            if ( pLoop == nullptr || pLoop->cHalfEdges < 3u ||
                 pLoop->cHalfEdges > cMaximum ) {
                status = geometry_status_t::CORRUPT_STATE;
                return false;
            }

            const usize cFaceVertices = pLoop->cHalfEdges;
            geometry_mesh_half_edge_handle_t hCurrent =
                pLoop->hFirstHalfEdge;
            for ( usize i = 0u; i < cFaceVertices; ++i ) {
                const mesh_half_edge_record_t *pHalfEdge =
                    common::GenerationPool_Get(
                        &pMesh->halfEdges, hCurrent );
                if ( pHalfEdge == nullptr ||
                     pHalfEdge->hLoop.nSlot != face.hOuterLoop.nSlot ||
                     pHalfEdge->hLoop.nGeneration !=
                         face.hOuterLoop.nGeneration ) {
                    status = geometry_status_t::CORRUPT_STATE;
                    return false;
                }
                const mesh_vertex_record_t *pVertex =
                    common::GenerationPool_Get(
                        &pMesh->vertices, pHalfEdge->hOrigin );
                if ( pVertex == nullptr ) {
                    status = geometry_status_t::CORRUPT_STATE;
                    return false;
                }
                pPositions[i] = pVertex->position;
                pVertexHandles[i] = pHalfEdge->hOrigin;
                hCurrent = pHalfEdge->hNext;
            }
            if ( hCurrent.nSlot != pLoop->hFirstHalfEdge.nSlot ||
                 hCurrent.nGeneration !=
                     pLoop->hFirstHalfEdge.nGeneration ) {
                status = geometry_status_t::INVALID_TOPOLOGY;
                return false;
            }

            math::polygon3d_basis_t basis{};
            const f64 fMinimumNormalLength =
                2.0 * policy.numerical.fMinimumFaceArea;
            if ( !math::Polygon3d_TryBasis(
                     pPositions, cFaceVertices,
                     fMinimumNormalLength, &basis ) ) {
                status = geometry_status_t::DEGENERATE;
                return false;
            }
            if ( !math::Polygon3d_IsPlanar(
                     pPositions, cFaceVertices, basis,
                     policy.numerical.fPlanarityTolerance ) ) {
                status = geometry_status_t::NON_PLANAR;
                return false;
            }
            if ( math::Vec3d_Dot( basis.normal, face.normal ) <= 0.0 ) {
                status = geometry_status_t::INVALID_TOPOLOGY;
                return false;
            }

            const usize cFaceOutput = ( cFaceVertices - 2u ) * 3u;
            const math::polygon_triangulation_result_t triangulation =
                math::Polygon3d_Triangulate(
                    pPositions, cFaceVertices, basis,
                    policy.numerical.fAbsoluteDistanceTolerance,
                    policy.numerical.fMinimumFaceArea,
                    pProjected, cMaximum,
                    pIndexScratch, cMaximum,
                    pTriangleIndices, cFaceOutput );
            status = MapTriangulationStatus( triangulation.status );
            if ( status != geometry_status_t::OK ||
                 triangulation.cIndicesWritten != cFaceOutput ||
                 triangulation.cTriangles != cFaceVertices - 2u ) {
                if ( status == geometry_status_t::OK ) {
                    status = geometry_status_t::CORRUPT_STATE;
                }
                return false;
            }

            for ( usize iTriangle = 0u;
                  iTriangle < triangulation.cTriangles;
                  ++iTriangle ) {
                const u32 iA = pTriangleIndices[iTriangle * 3u];
                const u32 iB = pTriangleIndices[iTriangle * 3u + 1u];
                const u32 iC = pTriangleIndices[iTriangle * 3u + 2u];
                if ( iA >= cFaceVertices || iB >= cFaceVertices ||
                     iC >= cFaceVertices ) {
                    status = geometry_status_t::CORRUPT_STATE;
                    return false;
                }

                triangle_hit_t triangleHit{};
                bool bDegenerate = false;
                if ( !TryIntersectTriangle(
                         ray, options, policy.numerical,
                         pPositions[iA], pPositions[iB], pPositions[iC],
                         &triangleHit, &bDegenerate ) ) {
                    if ( bDegenerate ) {
                        status = geometry_status_t::DEGENERATE;
                        return false;
                    }
                    continue;
                }
                if ( !MeshCandidatePrecedes(
                         triangleHit.fDistance, hFace,
                         static_cast<u32>( iTriangle ), best,
                         policy.numerical ) ) {
                    continue;
                }

                const math::vec3d_t position = math::Vec3d_MulAdd(
                    ray.origin, ray.direction, triangleHit.fDistance );
                if ( !math::Vec3d_IsFinite( position ) ) {
                    status = geometry_status_t::NUMERIC_FAILURE;
                    return false;
                }

                best = {};
                best.bHit = true;
                best.fDistance = triangleHit.fDistance;
                best.position = position;
                best.normal = basis.normal;
                best.barycentric = triangleHit.barycentric;
                best.meshSourceId = meshSourceId;
                best.hShell = face.hShell;
                best.hFace = hFace;
                best.hLoop = face.hOuterLoop;
                best.hVertex0 = pVertexHandles[iA];
                best.hVertex1 = pVertexHandles[iB];
                best.hVertex2 = pVertexHandles[iC];
                best.iSourceSide = face.iSourceSide;
                best.iTriangleInFace = static_cast<u32>( iTriangle );
            }
            return true;
        } );

    const geometry_status_t finalStatus = finish( status );
    if ( finalStatus == geometry_status_t::OK ) {
        *pHitOut = best;
    }
    return finalStatus;
}

} // namespace cypher::editor::geometry
