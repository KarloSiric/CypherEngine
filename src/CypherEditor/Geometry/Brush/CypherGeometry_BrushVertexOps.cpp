//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushVertexOps.cpp
//  Purpose: Implements vertex-level manipulation on convex brushes.
//  Details: Operations stage a point-cloud edit, rebuild a convex brush,
//           validate it, transfer retained face identity once, and publish
//           through a pointer-only ownership move. The live brush and source
//           ID allocator therefore remain unchanged on every failed path.
//
//  History:
//  - Created by Karlo Siric on 2026-09-22
//  - Hardened failure-atomic rebuild and dynamic workspace on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushVertexOps.h"
#include "CypherGeometry_BrushValidation.h"
#include "CypherGeometry_Kernel_ConvexHull.h"
#include "CypherGeometry_Snap.h"

#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;
using namespace cypher::math;

namespace
{

// A small angular drift is allowed when associating a rebuilt face with an
// authored face. This remains an interim brush-specific propagation policy;
// full mesh/CSG propagation uses explicit provenance records.
constexpr f64 kNormalMatchThreshold = 0.9961946980917455; // cos(5 degrees)

bool BoundaryStorageIsValid( const brush_boundary_t *pBoundary ) noexcept
{
    if ( pBoundary == nullptr ||
         pBoundary->vertices.pAllocator == nullptr ) {
        return false;
    }
    const allocator_t *pAllocator = pBoundary->vertices.pAllocator;
    return pBoundary->edges.pAllocator == pAllocator &&
           pBoundary->faces.pAllocator == pAllocator &&
           pBoundary->faceVertexIndices.pAllocator == pAllocator &&
           Vector_IsValid( &pBoundary->vertices ) &&
           Vector_IsValid( &pBoundary->edges ) &&
           Vector_IsValid( &pBoundary->faces ) &&
           Vector_IsValid( &pBoundary->faceVertexIndices );
}

geometry_status_t ValidateBoundaryStructure(
    const brush_boundary_t *pBoundary,
    const brush_solid_t *pBrush,
    const geometry_policy_t &policy ) noexcept
{
    if ( !BoundaryStorageIsValid( pBoundary ) ) {
        return pBoundary != nullptr &&
                       pBoundary->vertices.pAllocator == nullptr
            ? geometry_status_t::NOT_INITIALIZED
            : geometry_status_t::CORRUPT_STATE;
    }

    const usize cVertices = pBoundary->vertices.nCount;
    const usize cEdges = pBoundary->edges.nCount;
    const usize cFaces = pBoundary->faces.nCount;
    const usize cIndices = pBoundary->faceVertexIndices.nCount;
    const usize cSides = BrushSolid_SideCount( pBrush );

    if ( cVertices < 4u || cEdges < 6u || cFaces < 4u ||
         cFaces != cSides ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    if ( static_cast<u64>( cVertices ) > policy.limits.cVerticesMax ||
         static_cast<u64>( cEdges ) > policy.limits.cEdgesMax ||
         static_cast<u64>( cFaces ) > policy.limits.cFacesMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    for ( usize iVertex = 0u; iVertex < cVertices; ++iVertex ) {
        const vec3d_t vertex = pBoundary->vertices.pData[iVertex];
        if ( !Vec3d_IsFinite( vertex ) ) {
            return geometry_status_t::NUMERIC_FAILURE;
        }
        const f64 limit = policy.numerical.fCoordinateMagnitudeLimit;
        if ( Scalar_Abs( vertex.x ) > limit ||
             Scalar_Abs( vertex.y ) > limit ||
             Scalar_Abs( vertex.z ) > limit ) {
            return geometry_status_t::LIMIT_EXCEEDED;
        }
    }

    for ( usize iEdge = 0u; iEdge < cEdges; ++iEdge ) {
        const brush_boundary_edge_t &edge = pBoundary->edges.pData[iEdge];
        if ( edge.iVertex0 >= cVertices || edge.iVertex1 >= cVertices ||
             edge.iVertex0 >= edge.iVertex1 ) {
            return geometry_status_t::CORRUPT_STATE;
        }
    }

    for ( usize iFace = 0u; iFace < cFaces; ++iFace ) {
        const brush_boundary_face_t &face = pBoundary->faces.pData[iFace];
        if ( face.iSide >= cSides || face.cVertices < 3u ||
             face.iFirstIndex > cIndices ||
             face.cVertices > cIndices - face.iFirstIndex ) {
            return geometry_status_t::CORRUPT_STATE;
        }
        for ( usize i = 0u; i < face.cVertices; ++i ) {
            if ( pBoundary->faceVertexIndices.pData[
                     face.iFirstIndex + i] >= cVertices ) {
                return geometry_status_t::CORRUPT_STATE;
            }
        }
    }
    return geometry_status_t::OK;
}

geometry_status_t ValidateOperationInputs(
    const brush_solid_t *pBrush,
    const brush_boundary_t *pBoundary,
    const allocator_t *pAllocator,
    const geometry_policy_t &policy,
    const geometry_source_id_allocator_t *pIdAllocator ) noexcept
{
    if ( pBrush == nullptr || pBoundary == nullptr ||
         pAllocator == nullptr || pIdAllocator == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !Allocator_IsValid( pAllocator ) ||
         !GeometryPolicy_IsValid( policy ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pBrush->sides.pAllocator == nullptr ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !Vector_IsValid( &pBrush->sides ) ||
         !Allocator_IsValid( pBrush->sides.pAllocator ) ||
         pBrush->sides.pAllocator != pAllocator ||
         !GeometrySourceId_IsValid( pBrush->sourceId ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    const usize cSides = pBrush->sides.nCount;
    u64 maximumLiveId = pBrush->sourceId.value;
    if ( cSides < 4u ) {
        return geometry_status_t::DEGENERATE;
    }
    if ( static_cast<u64>( cSides ) >
         policy.limits.cBrushSidesPerBrushMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    for ( usize i = 0u; i < cSides; ++i ) {
        const geometry_source_id_t id = pBrush->sides.pData[i].sourceId;
        if ( !GeometrySourceId_IsValid( id ) ) {
            return geometry_status_t::CORRUPT_STATE;
        }
        if ( id.value > maximumLiveId ) {
            maximumLiveId = id.value;
        }
        for ( usize j = 0u; j < i; ++j ) {
            if ( pBrush->sides.pData[j].sourceId.value == id.value ) {
                return geometry_status_t::IDENTITY_CONFLICT;
            }
        }
    }
    if ( GeometrySourceId_IsValid( pIdAllocator->next ) &&
         pIdAllocator->next.value <= maximumLiveId ) {
        return geometry_status_t::IDENTITY_CONFLICT;
    }

    geometry_status_t status = ValidateBoundaryStructure(
        pBoundary, pBrush, policy );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    const brush_validation_result_t validation = BrushValidation_Deep(
        pBrush, policy, pAllocator );
    if ( validation.status != geometry_status_t::OK ) {
        return validation.status;
    }
    if ( validation.cVertices != pBoundary->vertices.nCount ||
         validation.cEdges != pBoundary->edges.nCount ||
         validation.cFaces != pBoundary->faces.nCount ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    return geometry_status_t::OK;
}

geometry_status_t TryCreatePointWorkspace(
    vector_t<vec3d_t> *pPoints,
    const allocator_t *pAllocator,
    const geometry_policy_t &policy,
    usize cPoints,
    usize *pScratchBytesOut ) noexcept
{
    if ( pPoints == nullptr || pScratchBytesOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pScratchBytesOut = 0u;
    if ( cPoints < 4u ) {
        return geometry_status_t::DEGENERATE;
    }
    if ( cPoints > static_cast<usize>( CY_U32_MAX ) ||
         static_cast<u64>( cPoints ) > policy.limits.cVerticesMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    usize cbPoints = 0u;
    if ( !Cy_TryArrayByteCount<vec3d_t>( cPoints, cbPoints ) ||
         static_cast<u64>( cbPoints ) > policy.limits.cbScratchMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    if ( !Vector_Init( pPoints, pAllocator, cPoints ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    if ( !Vector_Resize( pPoints, cPoints ) ) {
        Vector_Shutdown( pPoints );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    *pScratchBytesOut = cbPoints;
    return geometry_status_t::OK;
}

bool PlanesAreEquivalent(
    planed_t a,
    planed_t b,
    const geometry_numerical_policy_t &numerical ) noexcept
{
    const f64 minimumDot = std::cos(
        numerical.fAngularToleranceRadians );
    return Vec3d_Dot( a.normal, b.normal ) >= minimumDot &&
           Scalar_Abs( a.d - b.d ) <=
               numerical.fCoplanarDistanceTolerance;
}

bool BrushesAreGeometricallyEquivalent(
    const brush_solid_t *pA,
    const brush_solid_t *pB,
    const geometry_numerical_policy_t &numerical ) noexcept
{
    const usize cSides = BrushSolid_SideCount( pA );
    if ( cSides != BrushSolid_SideCount( pB ) ) {
        return false;
    }
    for ( usize iA = 0u; iA < cSides; ++iA ) {
        bool bFound = false;
        for ( usize iB = 0u; iB < cSides; ++iB ) {
            if ( PlanesAreEquivalent(
                     pA->sides.pData[iA].plane,
                     pB->sides.pData[iB].plane,
                     numerical ) ) {
                bFound = true;
                break;
            }
        }
        if ( !bFound ) {
            return false;
        }
    }
    return true;
}

bool OldSideAlreadyAssigned(
    const brush_solid_t *pNewBrush,
    usize cAssignedNewSides,
    geometry_source_id_t oldId ) noexcept
{
    for ( usize i = 0u; i < cAssignedNewSides; ++i ) {
        if ( pNewBrush->sides.pData[i].sourceId.value == oldId.value ) {
            return true;
        }
    }
    return false;
}

geometry_status_t TryTransferPlaneIdentity(
    brush_solid_t *pNewBrush,
    const brush_solid_t *pOldBrush,
    geometry_source_id_allocator_t *pFinalIds ) noexcept
{
    const usize cNewSides = BrushSolid_SideCount( pNewBrush );
    const usize cOldSides = BrushSolid_SideCount( pOldBrush );

    for ( usize iNew = 0u; iNew < cNewSides; ++iNew ) {
        brush_solid_side_t &newSide = pNewBrush->sides.pData[iNew];
        f64 bestDot = -2.0;
        usize iBestOld = CY_USIZE_MAX;

        for ( usize iOld = 0u; iOld < cOldSides; ++iOld ) {
            const brush_solid_side_t &oldSide =
                pOldBrush->sides.pData[iOld];
            if ( OldSideAlreadyAssigned(
                     pNewBrush, iNew, oldSide.sourceId ) ) {
                continue;
            }
            const f64 dot = Vec3d_Dot(
                newSide.plane.normal, oldSide.plane.normal );
            if ( dot > bestDot ) {
                bestDot = dot;
                iBestOld = iOld;
            }
        }

        if ( iBestOld != CY_USIZE_MAX &&
             bestDot >= kNormalMatchThreshold ) {
            const brush_solid_side_t &oldSide =
                pOldBrush->sides.pData[iBestOld];
            newSide.sourceId = oldSide.sourceId;
            newSide.iAttributeIndex = oldSide.iAttributeIndex;
            continue;
        }

        const geometry_source_id_result_t newId =
            GeometrySourceIdAllocator_Allocate( pFinalIds );
        if ( newId.status != geometry_status_t::OK ) {
            return newId.status;
        }
        newSide.sourceId = newId.id;
        newSide.iAttributeIndex = 0u;
    }
    return geometry_status_t::OK;
}

brush_vertex_op_result_t RebuildFromPoints(
    brush_solid_t *pBrush,
    const allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    vec3d_t *pPoints,
    usize cPoints,
    usize cbPointWorkspace ) noexcept
{
    brush_vertex_op_result_t result{};
    if ( cPoints < 4u ) {
        result.status = geometry_status_t::DEGENERATE;
        return result;
    }
    if ( cbPointWorkspace > policy.limits.cbScratchMax ) {
        result.status = geometry_status_t::LIMIT_EXCEEDED;
        return result;
    }

    geometry_policy_t remainingPolicy = policy;
    remainingPolicy.limits.cbScratchMax -= cbPointWorkspace;
    if ( remainingPolicy.limits.cbScratchMax == 0u ) {
        result.status = geometry_status_t::LIMIT_EXCEEDED;
        return result;
    }

    geometry_source_id_allocator_t temporaryIds = *pIdAllocator;
    brush_solid_t newBrush{};
    result.status = ConvexHull_TryBuildBrush(
        &newBrush, pAllocator, remainingPolicy, &temporaryIds,
        pPoints, cPoints );
    if ( result.status != geometry_status_t::OK ) {
        return result;
    }

    const brush_validation_result_t validation = BrushValidation_Deep(
        &newBrush, remainingPolicy, pAllocator );
    if ( validation.status != geometry_status_t::OK ) {
        BrushSolid_Shutdown( &newBrush );
        result.status = validation.status;
        return result;
    }

    result.cVertices = validation.cVertices;
    result.cEdges = validation.cEdges;
    result.cFaces = validation.cFaces;

    if ( BrushesAreGeometricallyEquivalent(
             pBrush, &newBrush, policy.numerical ) ) {
        BrushSolid_Shutdown( &newBrush );
        result.status = geometry_status_t::OK;
        return result;
    }

    geometry_source_id_allocator_t finalIds = *pIdAllocator;
    result.status = TryTransferPlaneIdentity(
        &newBrush, pBrush, &finalIds );
    if ( result.status != geometry_status_t::OK ) {
        BrushSolid_Shutdown( &newBrush );
        result.cVertices = 0u;
        result.cEdges = 0u;
        result.cFaces = 0u;
        return result;
    }

    const geometry_source_id_t retainedBrushId = pBrush->sourceId;
    BrushSolid_Shutdown( pBrush );
    pBrush->sourceId = retainedBrushId;
    Vector_Move( &pBrush->sides, &newBrush.sides );
    newBrush.sourceId = GEOMETRY_SOURCE_ID_INVALID;
    *pIdAllocator = finalIds;

    result.status = geometry_status_t::OK;
    return result;
}

brush_vertex_op_result_t InvalidResult( geometry_status_t status ) noexcept
{
    brush_vertex_op_result_t result{};
    result.status = status;
    return result;
}

} // namespace

brush_vertex_op_result_t BrushVertexOps_TryMoveVertex(
    brush_solid_t *pBrush,
    const brush_boundary_t *pBoundary,
    const allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    usize iVertex,
    vec3d_t newPosition ) noexcept
{
    geometry_status_t status = ValidateOperationInputs(
        pBrush, pBoundary, pAllocator, policy, pIdAllocator );
    if ( status != geometry_status_t::OK ) {
        return InvalidResult( status );
    }
    const usize cVertices = pBoundary->vertices.nCount;
    if ( iVertex >= cVertices || !Vec3d_IsFinite( newPosition ) ) {
        return InvalidResult( geometry_status_t::INVALID_ARGUMENT );
    }
    if ( newPosition.x == pBoundary->vertices.pData[iVertex].x &&
         newPosition.y == pBoundary->vertices.pData[iVertex].y &&
         newPosition.z == pBoundary->vertices.pData[iVertex].z ) {
        brush_vertex_op_result_t result{};
        result.status = geometry_status_t::OK;
        result.cVertices = static_cast<u32>( cVertices );
        result.cEdges = static_cast<u32>( pBoundary->edges.nCount );
        result.cFaces = static_cast<u32>( pBoundary->faces.nCount );
        return result;
    }

    vector_t<vec3d_t> points{};
    usize cbPoints = 0u;
    status = TryCreatePointWorkspace(
        &points, pAllocator, policy, cVertices, &cbPoints );
    if ( status != geometry_status_t::OK ) {
        return InvalidResult( status );
    }
    for ( usize i = 0u; i < cVertices; ++i ) {
        points.pData[i] = pBoundary->vertices.pData[i];
    }
    points.pData[iVertex] = newPosition;

    const brush_vertex_op_result_t result = RebuildFromPoints(
        pBrush, pAllocator, policy, pIdAllocator,
        points.pData, cVertices, cbPoints );
    Vector_Shutdown( &points );
    return result;
}

brush_vertex_op_result_t BrushVertexOps_TryMoveEdge(
    brush_solid_t *pBrush,
    const brush_boundary_t *pBoundary,
    const allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    usize iEdge,
    vec3d_t delta ) noexcept
{
    geometry_status_t status = ValidateOperationInputs(
        pBrush, pBoundary, pAllocator, policy, pIdAllocator );
    if ( status != geometry_status_t::OK ) {
        return InvalidResult( status );
    }
    if ( iEdge >= pBoundary->edges.nCount || !Vec3d_IsFinite( delta ) ) {
        return InvalidResult( geometry_status_t::INVALID_ARGUMENT );
    }
    const usize cVertices = pBoundary->vertices.nCount;
    const brush_boundary_edge_t edge = pBoundary->edges.pData[iEdge];

    vector_t<vec3d_t> points{};
    usize cbPoints = 0u;
    status = TryCreatePointWorkspace(
        &points, pAllocator, policy, cVertices, &cbPoints );
    if ( status != geometry_status_t::OK ) {
        return InvalidResult( status );
    }
    for ( usize i = 0u; i < cVertices; ++i ) {
        points.pData[i] = pBoundary->vertices.pData[i];
    }
    points.pData[edge.iVertex0] = Vec3d_Add(
        points.pData[edge.iVertex0], delta );
    points.pData[edge.iVertex1] = Vec3d_Add(
        points.pData[edge.iVertex1], delta );

    const brush_vertex_op_result_t result = RebuildFromPoints(
        pBrush, pAllocator, policy, pIdAllocator,
        points.pData, cVertices, cbPoints );
    Vector_Shutdown( &points );
    return result;
}

brush_vertex_op_result_t BrushVertexOps_TryMoveFace(
    brush_solid_t *pBrush,
    const brush_boundary_t *pBoundary,
    const allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    usize iFace,
    vec3d_t delta ) noexcept
{
    geometry_status_t status = ValidateOperationInputs(
        pBrush, pBoundary, pAllocator, policy, pIdAllocator );
    if ( status != geometry_status_t::OK ) {
        return InvalidResult( status );
    }
    if ( iFace >= pBoundary->faces.nCount || !Vec3d_IsFinite( delta ) ) {
        return InvalidResult( geometry_status_t::INVALID_ARGUMENT );
    }
    const usize cVertices = pBoundary->vertices.nCount;

    vector_t<vec3d_t> points{};
    usize cbPoints = 0u;
    status = TryCreatePointWorkspace(
        &points, pAllocator, policy, cVertices, &cbPoints );
    if ( status != geometry_status_t::OK ) {
        return InvalidResult( status );
    }
    for ( usize i = 0u; i < cVertices; ++i ) {
        points.pData[i] = pBoundary->vertices.pData[i];
    }

    const brush_boundary_face_t &face = pBoundary->faces.pData[iFace];
    for ( usize i = 0u; i < face.cVertices; ++i ) {
        const usize iVertex = pBoundary->faceVertexIndices.pData[
            face.iFirstIndex + i];
        points.pData[iVertex] = Vec3d_Add(
            pBoundary->vertices.pData[iVertex], delta );
    }

    const brush_vertex_op_result_t result = RebuildFromPoints(
        pBrush, pAllocator, policy, pIdAllocator,
        points.pData, cVertices, cbPoints );
    Vector_Shutdown( &points );
    return result;
}

brush_vertex_op_result_t BrushVertexOps_TryAddVertex(
    brush_solid_t *pBrush,
    const brush_boundary_t *pBoundary,
    const allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    vec3d_t position ) noexcept
{
    geometry_status_t status = ValidateOperationInputs(
        pBrush, pBoundary, pAllocator, policy, pIdAllocator );
    if ( status != geometry_status_t::OK ) {
        return InvalidResult( status );
    }
    if ( !Vec3d_IsFinite( position ) ) {
        return InvalidResult( geometry_status_t::INVALID_ARGUMENT );
    }
    const usize cVertices = pBoundary->vertices.nCount;
    if ( cVertices == CY_USIZE_MAX ) {
        return InvalidResult( geometry_status_t::LIMIT_EXCEEDED );
    }

    vector_t<vec3d_t> points{};
    usize cbPoints = 0u;
    status = TryCreatePointWorkspace(
        &points, pAllocator, policy, cVertices + 1u, &cbPoints );
    if ( status != geometry_status_t::OK ) {
        return InvalidResult( status );
    }
    for ( usize i = 0u; i < cVertices; ++i ) {
        points.pData[i] = pBoundary->vertices.pData[i];
    }
    points.pData[cVertices] = position;

    const brush_vertex_op_result_t result = RebuildFromPoints(
        pBrush, pAllocator, policy, pIdAllocator,
        points.pData, cVertices + 1u, cbPoints );
    Vector_Shutdown( &points );
    return result;
}

brush_vertex_op_result_t BrushVertexOps_TryRemoveVertex(
    brush_solid_t *pBrush,
    const brush_boundary_t *pBoundary,
    const allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    usize iVertex ) noexcept
{
    geometry_status_t status = ValidateOperationInputs(
        pBrush, pBoundary, pAllocator, policy, pIdAllocator );
    if ( status != geometry_status_t::OK ) {
        return InvalidResult( status );
    }
    const usize cVertices = pBoundary->vertices.nCount;
    if ( iVertex >= cVertices ) {
        return InvalidResult( geometry_status_t::INVALID_ARGUMENT );
    }
    if ( cVertices < 5u ) {
        return InvalidResult( geometry_status_t::DEGENERATE );
    }

    vector_t<vec3d_t> points{};
    usize cbPoints = 0u;
    status = TryCreatePointWorkspace(
        &points, pAllocator, policy, cVertices - 1u, &cbPoints );
    if ( status != geometry_status_t::OK ) {
        return InvalidResult( status );
    }
    usize iWrite = 0u;
    for ( usize i = 0u; i < cVertices; ++i ) {
        if ( i != iVertex ) {
            points.pData[iWrite++] = pBoundary->vertices.pData[i];
        }
    }

    const brush_vertex_op_result_t result = RebuildFromPoints(
        pBrush, pAllocator, policy, pIdAllocator,
        points.pData, iWrite, cbPoints );
    Vector_Shutdown( &points );
    return result;
}

brush_vertex_op_result_t BrushVertexOps_TrySnapToGrid(
    brush_solid_t *pBrush,
    const brush_boundary_t *pBoundary,
    const allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    f64 gridSpacing ) noexcept
{
    geometry_status_t status = ValidateOperationInputs(
        pBrush, pBoundary, pAllocator, policy, pIdAllocator );
    if ( status != geometry_status_t::OK ) {
        return InvalidResult( status );
    }
    if ( !Scalar_IsFinite( gridSpacing ) || gridSpacing <= 0.0 ) {
        return InvalidResult( geometry_status_t::INVALID_ARGUMENT );
    }
    const usize cVertices = pBoundary->vertices.nCount;

    vector_t<vec3d_t> points{};
    usize cbPoints = 0u;
    status = TryCreatePointWorkspace(
        &points, pAllocator, policy, cVertices, &cbPoints );
    if ( status != geometry_status_t::OK ) {
        return InvalidResult( status );
    }
    for ( usize i = 0u; i < cVertices; ++i ) {
        points.pData[i] = Snap_GridPoint(
            pBoundary->vertices.pData[i], gridSpacing );
    }

    const brush_vertex_op_result_t result = RebuildFromPoints(
        pBrush, pAllocator, policy, pIdAllocator,
        points.pData, cVertices, cbPoints );
    Vector_Shutdown( &points );
    return result;
}

} // namespace cypher::editor::geometry
