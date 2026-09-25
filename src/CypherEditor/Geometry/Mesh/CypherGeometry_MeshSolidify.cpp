//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshSolidify.cpp
//  Purpose: Implements bounded, failure-atomic solidification of open meshes.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshSolidify.h"

#include "CypherGeometry_MeshBoundaryOps.h"
#include "CypherGeometry_MeshGeometricValidation.h"
#include "CypherGeometry_MeshValidation.h"

#include <algorithm>
#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

constexpr f64 kPi = 3.14159265358979323846264338327950288;

struct edge_occurrence_t {
    u32 iLow{ 0u };
    u32 iHigh{ 0u };
    u32 iFrom{ 0u };
    u32 iTo{ 0u };
    u32 iFace{ 0u };
    u32 iCornerFrom{ 0u };
    u32 iCornerTo{ 0u };
};

struct boundary_wall_t {
    edge_occurrence_t edge{};
    bool bTriangulated{ false };
    bool bDiagonal02{ true };
};

struct shell_volume_accumulator_t {
    math::vec3d_t reference{};
    long double fSixTimesVolume{ 0.0L };
    long double fCompensation{ 0.0L };
    bool bHasReference{ false };
};

struct source_shell_topology_t {
    u64 cVertices{ 0u };
    u64 cEdges{ 0u };
    u64 cFaces{ 0u };
    bool bHasBoundary{ false };
};

geometry_status_t StatusFromSourceFault( mesh_source_fault_t fault ) noexcept
{
    switch ( fault ) {
        case mesh_source_fault_t::NONE: return geometry_status_t::OK;
        case mesh_source_fault_t::NOT_INITIALIZED: return geometry_status_t::NOT_INITIALIZED;
        case mesh_source_fault_t::NON_FINITE:
        case mesh_source_fault_t::COORDINATE_RANGE:
        case mesh_source_fault_t::INVALID_ATTRIBUTES: return geometry_status_t::NUMERIC_FAILURE;
        case mesh_source_fault_t::VALIDATION_INCOMPLETE: return geometry_status_t::ALLOCATION_FAILED;
        case mesh_source_fault_t::DUPLICATE_SOURCE_ID: return geometry_status_t::IDENTITY_CONFLICT;
        case mesh_source_fault_t::INVALID_ROOT_ID:
        case mesh_source_fault_t::MISSING_VERTEX_ID:
        case mesh_source_fault_t::MISSING_FACE_ID:
        case mesh_source_fault_t::INVALID_TOPOLOGY: return geometry_status_t::INVALID_TOPOLOGY;
    }
    return geometry_status_t::CORRUPT_STATE;
}

bool PositionInDomain( math::vec3d_t position, const geometry_policy_t &policy ) noexcept
{
    const f64 limit = std::fmin(
        kMeshSourceCoordinateMax,
        policy.numerical.fCoordinateMagnitudeLimit );
    return math::Vec3d_IsFinite( position ) &&
           std::fabs( position.x ) <= limit &&
           std::fabs( position.y ) <= limit &&
           std::fabs( position.z ) <= limit;
}

geometry_status_t TryFaceNormal(
    const mesh_source_description_t &source,
    const mesh_source_face_t &face,
    const geometry_policy_t &policy,
    math::vec3d_t *pNormalOut ) noexcept
{
    math::vec3d_t newell{};
    for ( u32 k = 0u; k < face.cCorners; ++k ) {
        const u32 iA = source.corners.pData[face.iFirstCorner + k].iVertex;
        const u32 iB = source.corners.pData[
            face.iFirstCorner + ( k + 1u ) % face.cCorners].iVertex;
        const math::vec3d_t a = source.vertices.pData[iA].position;
        const math::vec3d_t b = source.vertices.pData[iB].position;
        newell.x += ( a.y - b.y ) * ( a.z + b.z );
        newell.y += ( a.z - b.z ) * ( a.x + b.x );
        newell.z += ( a.x - b.x ) * ( a.y + b.y );
    }
    const f64 lengthSquared = math::Vec3d_LengthSquared( newell );
    const f64 minimumTwiceArea = 2.0 * policy.numerical.fMinimumFaceArea;
    if ( !std::isfinite( lengthSquared ) ||
         lengthSquared <= minimumTwiceArea * minimumTwiceArea ||
         !math::Vec3d_TryNormalize(
             newell, minimumTwiceArea, pNormalOut, nullptr ) ) {
        return geometry_status_t::DEGENERATE;
    }
    return geometry_status_t::OK;
}

f64 TriangleArea(
    math::vec3d_t a,
    math::vec3d_t b,
    math::vec3d_t c ) noexcept
{
    const math::vec3d_t cross = math::Vec3d_Cross(
        math::Vec3d_Subtract( b, a ),
        math::Vec3d_Subtract( c, a ) );
    const f64 lengthSquared = math::Vec3d_LengthSquared( cross );
    return lengthSquared >= 0.0 && std::isfinite( lengthSquared )
        ? 0.5 * std::sqrt( lengthSquared )
        : 0.0;
}

geometry_status_t ClassifyWall(
    const math::vec3d_t positions[4],
    const geometry_policy_t &policy,
    bool *pbTriangulateOut,
    bool *pbDiagonal02Out ) noexcept
{
    const f64 area012 = TriangleArea(
        positions[0], positions[1], positions[2] );
    const f64 area023 = TriangleArea(
        positions[0], positions[2], positions[3] );
    const f64 area013 = TriangleArea(
        positions[0], positions[1], positions[3] );
    const f64 area123 = TriangleArea(
        positions[1], positions[2], positions[3] );
    const f64 min02 = std::fmin( area012, area023 );
    const f64 min13 = std::fmin( area013, area123 );
    const bool bDiagonal02 = min02 >= min13;
    const f64 saferMinimum = bDiagonal02 ? min02 : min13;
    if ( !( saferMinimum > policy.numerical.fMinimumFaceArea ) ) {
        return geometry_status_t::DEGENERATE;
    }

    const math::vec3d_t e0 = math::Vec3d_Subtract(
        positions[1], positions[0] );
    const math::vec3d_t e1 = math::Vec3d_Subtract(
        positions[2], positions[0] );
    math::vec3d_t planeNormal{};
    if ( !math::Vec3d_TryNormalize(
             math::Vec3d_Cross( e0, e1 ),
             2.0 * policy.numerical.fMinimumFaceArea,
             &planeNormal,
             nullptr ) ) {
        return geometry_status_t::DEGENERATE;
    }
    const f64 deviation = std::fabs( math::Vec3d_Dot(
        planeNormal,
        math::Vec3d_Subtract( positions[3], positions[0] ) ) );
    if ( !std::isfinite( deviation ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }
    *pbTriangulateOut = deviation > policy.numerical.fPlanarityTolerance;
    *pbDiagonal02Out = bDiagonal02;
    return geometry_status_t::OK;
}

geometry_status_t TryAddAuthoredFace(
    mesh_source_description_t *pOut,
    span_t<const u32> indices,
    span_t<const mesh_corner_attributes_t> cornerAttributes,
    geometry_source_id_t faceId,
    const mesh_face_attributes_t &faceAttributes ) noexcept
{
    if ( indices.nCount != cornerAttributes.nCount ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    u32 iFace = 0u;
    geometry_status_t status = MeshSourceDescription_TryAddFace(
        pOut, indices, faceId, faceAttributes, &iFace );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    const mesh_source_face_t &face = pOut->faces.pData[iFace];
    for ( usize k = 0u; k < cornerAttributes.nCount; ++k ) {
        pOut->corners.pData[face.iFirstCorner + k].attributes =
            cornerAttributes.pData[k];
    }
    return geometry_status_t::OK;
}

geometry_status_t TryAddSurfaceFace(
    const mesh_source_description_t &source,
    const mesh_source_face_t &face,
    u32 iVertexOffset,
    bool bReverse,
    geometry_source_id_t faceId,
    mesh_source_description_t *pOut ) noexcept
{
    u32 indices[kMeshSourceCornersPerFaceMax]{};
    mesh_corner_attributes_t attributes[kMeshSourceCornersPerFaceMax]{};
    for ( u32 k = 0u; k < face.cCorners; ++k ) {
        const u32 iSourceCorner = bReverse
            ? face.cCorners - 1u - k
            : k;
        const mesh_source_corner_t &corner =
            source.corners.pData[face.iFirstCorner + iSourceCorner];
        indices[k] = corner.iVertex + iVertexOffset;
        attributes[k] = corner.attributes;
    }
    return TryAddAuthoredFace(
        pOut,
        span_t<const u32>{ indices, face.cCorners },
        span_t<const mesh_corner_attributes_t>{ attributes, face.cCorners },
        faceId,
        face.attributes );
}

geometry_status_t TryAllocateId(
    geometry_source_id_allocator_t *pIds,
    geometry_source_id_t *pIdOut ) noexcept
{
    const geometry_source_id_result_t result =
        GeometrySourceIdAllocator_Allocate( pIds );
    if ( result.status == geometry_status_t::OK ) {
        *pIdOut = result.id;
    }
    return result.status;
}

bool FitsResultLimits(
    u64 cVertices,
    u64 cEdges,
    u64 cFaces,
    u64 cCorners,
    u64 cShells,
    const geometry_policy_t &policy ) noexcept
{
    return cVertices <= kMeshSourceVerticesMax &&
           cFaces <= kMeshSourceFacesMax &&
           cCorners <= kMeshSourceCornersMax &&
           cVertices <= policy.limits.cVerticesMax &&
           cEdges <= policy.limits.cEdgesMax &&
           cFaces <= policy.limits.cFacesMax &&
           cFaces <= policy.limits.cLoopsMax &&
           cCorners <= policy.limits.cHalfEdgesMax &&
           cShells <= policy.limits.cShellsMax;
}

geometry_status_t ValidatePositiveShellVolumes(
    const editable_mesh_t *pMesh,
    const allocator_t *pAllocator,
    const geometry_policy_t &policy ) noexcept
{
    vector_t<shell_volume_accumulator_t> volumes{};
    if ( !Vector_Init( &volumes, pAllocator ) ||
         !Vector_Resize( &volumes, pMesh->shells.cSlots ) ) {
        Vector_Shutdown( &volumes );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize i = 0u; i < volumes.nCount; ++i ) {
        volumes.pData[i] = shell_volume_accumulator_t{};
    }

    geometry_status_t status = geometry_status_t::OK;
    (void)GenerationPool_ForEach(
        &pMesh->faces,
        [&]( geometry_mesh_face_handle_t,
             const mesh_face_record_t &face ) noexcept -> bool_t {
            if ( face.hShell.nSlot >= volumes.nCount ||
                 GenerationPool_Get(
                     &pMesh->shells, face.hShell ) == nullptr ) {
                status = geometry_status_t::CORRUPT_STATE;
                return false;
            }
            const mesh_loop_record_t *pLoop = GenerationPool_Get(
                &pMesh->loops, face.hOuterLoop );
            if ( pLoop == nullptr || pLoop->cHalfEdges < 3u ) {
                status = geometry_status_t::CORRUPT_STATE;
                return false;
            }

            const mesh_half_edge_record_t *pFirst = GenerationPool_Get(
                &pMesh->halfEdges, pLoop->hFirstHalfEdge );
            const mesh_vertex_record_t *pFirstVertex = pFirst != nullptr
                ? GenerationPool_Get(
                      &pMesh->vertices, pFirst->hOrigin )
                : nullptr;
            const mesh_half_edge_record_t *pPrevious = pFirst != nullptr
                ? GenerationPool_Get(
                      &pMesh->halfEdges, pFirst->hNext )
                : nullptr;
            const mesh_vertex_record_t *pPreviousVertex =
                pPrevious != nullptr
                ? GenerationPool_Get(
                      &pMesh->vertices, pPrevious->hOrigin )
                : nullptr;
            if ( pFirstVertex == nullptr || pPreviousVertex == nullptr ) {
                status = geometry_status_t::CORRUPT_STATE;
                return false;
            }

            shell_volume_accumulator_t &volume =
                volumes.pData[face.hShell.nSlot];
            if ( !volume.bHasReference ) {
                volume.reference = pFirstVertex->position;
                volume.bHasReference = true;
            }

            const math::vec3d_t a = math::Vec3d_Subtract(
                pFirstVertex->position, volume.reference );
            math::vec3d_t b = math::Vec3d_Subtract(
                pPreviousVertex->position, volume.reference );
            geometry_mesh_half_edge_handle_t hCurrent =
                pPrevious->hNext;
            for ( u32 k = 2u; k < pLoop->cHalfEdges; ++k ) {
                const mesh_half_edge_record_t *pCurrent =
                    GenerationPool_Get(
                        &pMesh->halfEdges, hCurrent );
                const mesh_vertex_record_t *pCurrentVertex =
                    pCurrent != nullptr
                    ? GenerationPool_Get(
                          &pMesh->vertices, pCurrent->hOrigin )
                    : nullptr;
                if ( pCurrent == nullptr || pCurrentVertex == nullptr ) {
                    status = geometry_status_t::CORRUPT_STATE;
                    return false;
                }
                const math::vec3d_t c = math::Vec3d_Subtract(
                    pCurrentVertex->position, volume.reference );
                const long double tripleProduct =
                    static_cast<long double>( a.x ) *
                        ( static_cast<long double>( b.y ) * c.z -
                          static_cast<long double>( b.z ) * c.y ) -
                    static_cast<long double>( a.y ) *
                        ( static_cast<long double>( b.x ) * c.z -
                          static_cast<long double>( b.z ) * c.x ) +
                    static_cast<long double>( a.z ) *
                        ( static_cast<long double>( b.x ) * c.y -
                          static_cast<long double>( b.y ) * c.x );
                const long double corrected =
                    tripleProduct - volume.fCompensation;
                const long double sum =
                    volume.fSixTimesVolume + corrected;
                volume.fCompensation =
                    ( sum - volume.fSixTimesVolume ) - corrected;
                volume.fSixTimesVolume = sum;
                b = c;
                hCurrent = pCurrent->hNext;
            }
            return true;
        } );

    if ( status == geometry_status_t::OK ) {
        const long double minimumVolume =
            static_cast<long double>(
                policy.numerical.fMinimumFaceArea ) *
            static_cast<long double>(
                policy.numerical.fMinimumEdgeLength ) / 3.0L;
        (void)GenerationPool_ForEach(
            &pMesh->shells,
            [&]( geometry_mesh_shell_handle_t hShell,
                 const mesh_shell_record_t & ) noexcept -> bool_t {
                if ( hShell.nSlot >= volumes.nCount ) {
                    status = geometry_status_t::CORRUPT_STATE;
                    return false;
                }
                const shell_volume_accumulator_t &volume =
                    volumes.pData[hShell.nSlot];
                const long double signedVolume =
                    volume.fSixTimesVolume / 6.0L;
                if ( !volume.bHasReference ||
                     !std::isfinite( signedVolume ) ) {
                    status = geometry_status_t::CORRUPT_STATE;
                    return false;
                }
                if ( !( signedVolume > minimumVolume ) ) {
                    status = geometry_status_t::OPEN_VOLUME;
                    return false;
                }
                return true;
            } );
    }
    Vector_Shutdown( &volumes );
    return status;
}

} // namespace

geometry_status_t MeshSolidify_TryBuild(
    const mesh_source_t *pSource,
    const allocator_t *pAllocator,
    geometry_source_id_allocator_t *pIdAllocator,
    const geometry_policy_t &policy,
    f64 thickness,
    mesh_source_t *pResultOut,
    mesh_solidify_report_t *pReportOut ) noexcept
{
    if ( pReportOut != nullptr ) {
        *pReportOut = mesh_solidify_report_t{};
    }
    if ( pSource == nullptr || pResultOut == nullptr ||
         pSource == pResultOut || pIdAllocator == nullptr ||
         !Allocator_IsValid( pAllocator ) ||
         !GeometryPolicy_IsValid( policy ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !MeshSource_IsInitialized( pSource ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( EditableMesh_IsInitialized( &pResultOut->mesh ) ||
         pResultOut->attributes.corners.pAllocator != nullptr ||
         pResultOut->attributes.faces.pAllocator != nullptr ||
         pResultOut->attributes.edges.pAllocator != nullptr ||
         pResultOut->vertexIds.pAllocator != nullptr ||
         pResultOut->faceIds.pAllocator != nullptr ||
         GeometrySourceId_IsValid( pResultOut->sourceId ) ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }
    if ( !std::isfinite( thickness ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }
    if ( !( std::fabs( thickness ) >
            policy.numerical.fMinimumEdgeLength ) ) {
        return geometry_status_t::DEGENERATE;
    }

    const mesh_source_validation_t sourceValidation =
        MeshSource_Validate( pSource, pAllocator );
    if ( sourceValidation.fault != mesh_source_fault_t::NONE ) {
        return StatusFromSourceFault( sourceValidation.fault );
    }
    mesh_geometric_options_t geometryOptions{};
    geometryOptions.bCheckSelfIntersection = true;
    geometryOptions.bCheckCoincidentVertices = true;
    const mesh_geometric_validation_t inputGeometry =
        MeshValidation_ValidateGeometry(
            &pSource->mesh, policy, geometryOptions );
    if ( inputGeometry.status != geometry_status_t::OK ) {
        return inputGeometry.status;
    }

    mesh_source_description_t source{};
    mesh_source_description_t result{};
    vector_t<math::vec3d_t> normalSums{};
    vector_t<math::vec3d_t> offsetPositions{};
    vector_t<edge_occurrence_t> occurrences{};
    vector_t<boundary_wall_t> walls{};
    vector_t<source_shell_topology_t> shellTopology{};
    auto cleanup = [&]() noexcept {
        Vector_Shutdown( &shellTopology );
        Vector_Shutdown( &walls );
        Vector_Shutdown( &occurrences );
        Vector_Shutdown( &offsetPositions );
        Vector_Shutdown( &normalSums );
        MeshSourceDescription_Shutdown( &result );
        MeshSourceDescription_Shutdown( &source );
    };

    geometry_status_t status = MeshSourceDescription_Init(
        &source, pAllocator, pSource->sourceId );
    if ( status == geometry_status_t::OK ) {
        status = MeshSourceDescription_Init(
            &result, pAllocator, pSource->sourceId );
    }
    if ( status == geometry_status_t::OK ) {
        status = MeshSource_TryDescribe( pSource, &source );
    }
    if ( status != geometry_status_t::OK ) {
        cleanup();
        return status;
    }

    const usize cVertices = source.vertices.nCount;
    const usize cFaces = source.faces.nCount;
    const usize cCorners = source.corners.nCount;
    const usize cShells = EditableMesh_ShellCount( &pSource->mesh );
    if ( cVertices == 0u || cFaces == 0u || cShells == 0u ) {
        cleanup();
        return geometry_status_t::DEGENERATE;
    }
    if ( !Vector_Init( &normalSums, pAllocator, cVertices ) ||
         !Vector_Resize( &normalSums, cVertices ) ||
         !Vector_Init( &offsetPositions, pAllocator, cVertices ) ||
         !Vector_Resize( &offsetPositions, cVertices ) ||
         !Vector_Init( &occurrences, pAllocator, cCorners ) ||
         !Vector_Reserve( &occurrences, cCorners ) ||
         !Vector_Init( &walls, pAllocator ) ||
         !Vector_Init( &shellTopology, pAllocator ) ||
         !Vector_Resize(
             &shellTopology, pSource->mesh.shells.cSlots ) ) {
        cleanup();
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize i = 0u; i < cVertices; ++i ) {
        normalSums.pData[i] = math::vec3d_t{};
        if ( !PositionInDomain( source.vertices.pData[i].position, policy ) ) {
            cleanup();
            return geometry_status_t::NUMERIC_FAILURE;
        }
    }
    for ( usize i = 0u; i < shellTopology.nCount; ++i ) {
        shellTopology.pData[i] = source_shell_topology_t{};
    }

    for ( usize iFace = 0u; iFace < cFaces; ++iFace ) {
        const mesh_source_face_t &face = source.faces.pData[iFace];
        math::vec3d_t faceNormal{};
        status = TryFaceNormal(
            source, face, policy, &faceNormal );
        if ( status != geometry_status_t::OK ) {
            cleanup();
            return status;
        }
        for ( u32 k = 0u; k < face.cCorners; ++k ) {
            const u32 iCorner = face.iFirstCorner + k;
            const u32 iPreviousCorner = face.iFirstCorner +
                ( k + face.cCorners - 1u ) % face.cCorners;
            const u32 iNextCorner = face.iFirstCorner +
                ( k + 1u ) % face.cCorners;
            const u32 iVertex = source.corners.pData[iCorner].iVertex;
            const u32 iPrevious =
                source.corners.pData[iPreviousCorner].iVertex;
            const u32 iNext = source.corners.pData[iNextCorner].iVertex;
            const math::vec3d_t current =
                source.vertices.pData[iVertex].position;
            const math::vec3d_t toNext = math::Vec3d_Subtract(
                source.vertices.pData[iNext].position, current );
            const math::vec3d_t toPrevious = math::Vec3d_Subtract(
                source.vertices.pData[iPrevious].position, current );
            const math::vec3d_t cross = math::Vec3d_Cross(
                toNext, toPrevious );
            const f64 crossLength = std::sqrt(
                math::Vec3d_LengthSquared( cross ) );
            const f64 dot = math::Vec3d_Dot(
                toNext, toPrevious );
            if ( !std::isfinite( crossLength ) || !std::isfinite( dot ) ) {
                cleanup();
                return geometry_status_t::NUMERIC_FAILURE;
            }
            f64 angle = std::atan2( crossLength, dot );
            if ( math::Vec3d_Dot( cross, faceNormal ) < 0.0 ) {
                angle = 2.0 * kPi - angle;
            }
            if ( !std::isfinite( angle ) ) {
                cleanup();
                return geometry_status_t::NUMERIC_FAILURE;
            }
            normalSums.pData[iVertex] = math::Vec3d_Add(
                normalSums.pData[iVertex],
                math::Vec3d_Scale( faceNormal, angle ) );

            edge_occurrence_t occurrence{};
            occurrence.iFrom = iVertex;
            occurrence.iTo = iNext;
            occurrence.iLow = std::min( iVertex, iNext );
            occurrence.iHigh = std::max( iVertex, iNext );
            occurrence.iFace = static_cast<u32>( iFace );
            occurrence.iCornerFrom = iCorner;
            occurrence.iCornerTo = iNextCorner;
            if ( !Vector_PushBack( &occurrences, occurrence ) ) {
                cleanup();
                return geometry_status_t::ALLOCATION_FAILED;
            }
        }
    }

    // A closed component cannot be thickened under this operation's
    // stationary/open-surface contract. The current editable-mesh validator
    // accepts genus-zero closed shells, so the source component must be a
    // topological disk (Euler characteristic one) before it is doubled.
    bool bTopologyOk = true;
    bool bUnsupportedTopology = false;
    (void)GenerationPool_ForEach(
        &pSource->mesh.vertices,
        [&]( geometry_mesh_vertex_handle_t,
             const mesh_vertex_record_t &vertex ) noexcept -> bool_t {
            const mesh_half_edge_record_t *pHalfEdge =
                GenerationPool_Get(
                    &pSource->mesh.halfEdges,
                    vertex.hOutHalfEdge );
            const mesh_loop_record_t *pLoop = pHalfEdge != nullptr
                ? GenerationPool_Get(
                      &pSource->mesh.loops, pHalfEdge->hLoop )
                : nullptr;
            const mesh_face_record_t *pFace = pLoop != nullptr
                ? GenerationPool_Get(
                      &pSource->mesh.faces, pLoop->hFace )
                : nullptr;
            if ( pFace == nullptr ||
                 pFace->hShell.nSlot >= shellTopology.nCount ||
                 GenerationPool_Get(
                     &pSource->mesh.shells,
                     pFace->hShell ) == nullptr ) {
                bTopologyOk = false;
                return false;
            }
            ++shellTopology.pData[pFace->hShell.nSlot].cVertices;
            return true;
        } );
    if ( bTopologyOk ) {
        (void)GenerationPool_ForEach(
            &pSource->mesh.faces,
            [&]( geometry_mesh_face_handle_t,
                 const mesh_face_record_t &face ) noexcept -> bool_t {
                if ( face.hShell.nSlot >= shellTopology.nCount ||
                     GenerationPool_Get(
                         &pSource->mesh.shells,
                         face.hShell ) == nullptr ) {
                    bTopologyOk = false;
                    return false;
                }
                ++shellTopology.pData[face.hShell.nSlot].cFaces;
                return true;
            } );
    }
    (void)GenerationPool_ForEach(
        &pSource->mesh.edges,
        [&]( geometry_mesh_edge_handle_t,
             const mesh_edge_record_t &edge ) noexcept -> bool_t {
            const mesh_half_edge_record_t *pHalfEdge =
                GenerationPool_Get(
                    &pSource->mesh.halfEdges, edge.hHalfEdge );
            if ( pHalfEdge == nullptr ) {
                bTopologyOk = false;
                return false;
            }
            if ( GeometryHandle_IsValid( pHalfEdge->hTwin ) ) {
                return true;
            }
            const mesh_loop_record_t *pLoop = GenerationPool_Get(
                &pSource->mesh.loops, pHalfEdge->hLoop );
            const mesh_face_record_t *pFace = pLoop != nullptr
                ? GenerationPool_Get(
                      &pSource->mesh.faces, pLoop->hFace )
                : nullptr;
            if ( pFace == nullptr ||
                 pFace->hShell.nSlot >= shellTopology.nCount ||
                 GenerationPool_Get(
                     &pSource->mesh.shells, pFace->hShell ) == nullptr ) {
                bTopologyOk = false;
                return false;
            }
            source_shell_topology_t &topology =
                shellTopology.pData[pFace->hShell.nSlot];
            ++topology.cEdges;
            topology.bHasBoundary = true;
            return true;
        } );
    if ( bTopologyOk ) {
        // Interior edges were skipped above after validating their record;
        // count them here without changing boundary ownership.
        (void)GenerationPool_ForEach(
            &pSource->mesh.edges,
            [&]( geometry_mesh_edge_handle_t,
                 const mesh_edge_record_t &edge ) noexcept -> bool_t {
                const mesh_half_edge_record_t *pHalfEdge =
                    GenerationPool_Get(
                        &pSource->mesh.halfEdges,
                        edge.hHalfEdge );
                if ( pHalfEdge == nullptr ||
                     !GeometryHandle_IsValid( pHalfEdge->hTwin ) ) {
                    return true;
                }
                const mesh_loop_record_t *pLoop = GenerationPool_Get(
                    &pSource->mesh.loops, pHalfEdge->hLoop );
                const mesh_face_record_t *pFace = pLoop != nullptr
                    ? GenerationPool_Get(
                          &pSource->mesh.faces, pLoop->hFace )
                    : nullptr;
                if ( pFace == nullptr ||
                     pFace->hShell.nSlot >= shellTopology.nCount ) {
                    bTopologyOk = false;
                    return false;
                }
                ++shellTopology.pData[pFace->hShell.nSlot].cEdges;
                return true;
            } );
    }
    if ( bTopologyOk ) {
        (void)GenerationPool_ForEach(
            &pSource->mesh.shells,
            [&]( geometry_mesh_shell_handle_t hShell,
                 const mesh_shell_record_t & ) noexcept -> bool_t {
                if ( hShell.nSlot >= shellTopology.nCount ) {
                    bTopologyOk = false;
                    return false;
                }
                const source_shell_topology_t &topology =
                    shellTopology.pData[hShell.nSlot];
                if ( !topology.bHasBoundary ||
                     topology.cVertices + topology.cFaces !=
                         topology.cEdges + 1u ) {
                    bUnsupportedTopology = true;
                    return false;
                }
                return true;
            } );
    }
    if ( !bTopologyOk ) {
        cleanup();
        return geometry_status_t::INVALID_TOPOLOGY;
    }
    if ( bUnsupportedTopology ) {
        cleanup();
        return geometry_status_t::UNSUPPORTED;
    }

    std::sort(
        occurrences.pData,
        occurrences.pData + occurrences.nCount,
        []( const edge_occurrence_t &a,
            const edge_occurrence_t &b ) noexcept {
            if ( a.iLow != b.iLow ) { return a.iLow < b.iLow; }
            if ( a.iHigh != b.iHigh ) { return a.iHigh < b.iHigh; }
            if ( a.iFace != b.iFace ) { return a.iFace < b.iFace; }
            return a.iCornerFrom < b.iCornerFrom;
        } );
    for ( usize i = 0u; i < occurrences.nCount; ) {
        usize j = i + 1u;
        while ( j < occurrences.nCount &&
                occurrences.pData[j].iLow == occurrences.pData[i].iLow &&
                occurrences.pData[j].iHigh == occurrences.pData[i].iHigh ) {
            ++j;
        }
        const usize cUses = j - i;
        if ( cUses > 2u ) {
            cleanup();
            return geometry_status_t::NON_MANIFOLD;
        }
        if ( cUses == 2u ) {
            const edge_occurrence_t &a = occurrences.pData[i];
            const edge_occurrence_t &b = occurrences.pData[i + 1u];
            if ( a.iFrom != b.iTo || a.iTo != b.iFrom ) {
                cleanup();
                return geometry_status_t::INVALID_TOPOLOGY;
            }
        } else if ( !Vector_PushBack(
                        &walls,
                        boundary_wall_t{ occurrences.pData[i], false, true } ) ) {
            cleanup();
            return geometry_status_t::ALLOCATION_FAILED;
        }
        i = j;
    }
    if ( walls.nCount == 0u ||
         walls.nCount != MeshBoundary_CountBoundaryEdges(
                             &pSource->mesh ) ) {
        cleanup();
        return walls.nCount == 0u
            ? geometry_status_t::UNSUPPORTED
            : geometry_status_t::CORRUPT_STATE;
    }

    for ( usize i = 0u; i < cVertices; ++i ) {
        math::vec3d_t normal{};
        if ( !math::Vec3d_TryNormalize(
                 normalSums.pData[i],
                 policy.numerical.fAngularToleranceRadians,
                 &normal,
                 nullptr ) ) {
            cleanup();
            return geometry_status_t::DEGENERATE;
        }
        const math::vec3d_t offset = math::Vec3d_Add(
            source.vertices.pData[i].position,
            math::Vec3d_Scale( normal, thickness ) );
        if ( !PositionInDomain( offset, policy ) ) {
            cleanup();
            return geometry_status_t::NUMERIC_FAILURE;
        }
        if ( math::Vec3d_EqualsExact(
                 offset, source.vertices.pData[i].position ) ) {
            cleanup();
            return geometry_status_t::DEGENERATE;
        }
        offsetPositions.pData[i] = offset;
    }

    u64 cSideFaces = 0u;
    u64 cTriangulatedWalls = 0u;
    u64 cSideCorners = 0u;
    const bool bPositive = thickness > 0.0;
    for ( usize iWall = 0u; iWall < walls.nCount; ++iWall ) {
        boundary_wall_t &wall = walls.pData[iWall];
        const edge_occurrence_t &edge = wall.edge;
        const math::vec3d_t a = source.vertices.pData[edge.iFrom].position;
        const math::vec3d_t b = source.vertices.pData[edge.iTo].position;
        const math::vec3d_t ao = offsetPositions.pData[edge.iFrom];
        const math::vec3d_t bo = offsetPositions.pData[edge.iTo];
        const math::vec3d_t positions[4] = {
            bPositive ? a : ao,
            bPositive ? b : bo,
            bPositive ? bo : b,
            bPositive ? ao : a
        };
        status = ClassifyWall(
            positions, policy,
            &wall.bTriangulated, &wall.bDiagonal02 );
        if ( status != geometry_status_t::OK ) {
            cleanup();
            return status;
        }
        cSideFaces += wall.bTriangulated ? 2u : 1u;
        cSideCorners += wall.bTriangulated ? 6u : 4u;
        cTriangulatedWalls += wall.bTriangulated ? 1u : 0u;
    }

    const u64 cSourceEdges = static_cast<u64>(
        EditableMesh_EdgeCount( &pSource->mesh ) );
    const u64 cResultVertices = static_cast<u64>( cVertices ) * 2u;
    const u64 cResultEdges = cSourceEdges * 2u +
        static_cast<u64>( walls.nCount ) + cTriangulatedWalls;
    const u64 cResultFaces = static_cast<u64>( cFaces ) * 2u + cSideFaces;
    const u64 cResultCorners = static_cast<u64>( cCorners ) * 2u + cSideCorners;
    if ( cResultCorners != cResultEdges * 2u ) {
        cleanup();
        return geometry_status_t::CORRUPT_STATE;
    }
    if ( !FitsResultLimits(
             cResultVertices, cResultEdges,
             cResultFaces, cResultCorners,
             static_cast<u64>( cShells ), policy ) ) {
        cleanup();
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    if ( !Vector_Reserve( &result.vertices, cResultVertices ) ||
         !Vector_Reserve( &result.faces, cResultFaces ) ||
         !Vector_Reserve( &result.corners, cResultCorners ) ||
         !Vector_Reserve(
             &result.edges, source.edges.nCount * 2u ) ) {
        cleanup();
        return geometry_status_t::ALLOCATION_FAILED;
    }

    geometry_source_id_allocator_t stagedIds = *pIdAllocator;
    for ( usize i = 0u; i < cVertices; ++i ) {
        const mesh_source_vertex_t &vertex = source.vertices.pData[i];
        status = MeshSourceDescription_TryAddVertex(
            &result, vertex.position, vertex.sourceId, nullptr );
        if ( status != geometry_status_t::OK ) {
            cleanup();
            return status;
        }
    }
    for ( usize i = 0u; i < cVertices; ++i ) {
        geometry_source_id_t id{};
        status = TryAllocateId( &stagedIds, &id );
        if ( status == geometry_status_t::OK ) {
            status = MeshSourceDescription_TryAddVertex(
                &result, offsetPositions.pData[i], id, nullptr );
        }
        if ( status != geometry_status_t::OK ) {
            cleanup();
            return status;
        }
    }

    for ( usize i = 0u; i < cFaces; ++i ) {
        const mesh_source_face_t &face = source.faces.pData[i];
        status = TryAddSurfaceFace(
            source, face, 0u, bPositive,
            face.sourceId, &result );
        if ( status != geometry_status_t::OK ) {
            cleanup();
            return status;
        }
    }
    for ( usize i = 0u; i < cFaces; ++i ) {
        geometry_source_id_t id{};
        status = TryAllocateId( &stagedIds, &id );
        if ( status == geometry_status_t::OK ) {
            status = TryAddSurfaceFace(
                source, source.faces.pData[i],
                static_cast<u32>( cVertices ), !bPositive,
                id, &result );
        }
        if ( status != geometry_status_t::OK ) {
            cleanup();
            return status;
        }
    }

    for ( usize iWall = 0u; iWall < walls.nCount; ++iWall ) {
        const boundary_wall_t &wall = walls.pData[iWall];
        const edge_occurrence_t &edge = wall.edge;
        const u32 duplicateOffset = static_cast<u32>( cVertices );
        const u32 indices[4] = {
            bPositive ? edge.iFrom : edge.iFrom + duplicateOffset,
            bPositive ? edge.iTo : edge.iTo + duplicateOffset,
            bPositive ? edge.iTo + duplicateOffset : edge.iTo,
            bPositive ? edge.iFrom + duplicateOffset : edge.iFrom
        };
        const mesh_corner_attributes_t fromAttributes =
            source.corners.pData[edge.iCornerFrom].attributes;
        const mesh_corner_attributes_t toAttributes =
            source.corners.pData[edge.iCornerTo].attributes;
        const mesh_corner_attributes_t attributes[4] = {
            fromAttributes, toAttributes, toAttributes, fromAttributes
        };
        mesh_face_attributes_t faceAttributes =
            source.faces.pData[edge.iFace].attributes;
        faceAttributes.smoothingGroups = 0u;

        if ( !wall.bTriangulated ) {
            geometry_source_id_t id{};
            status = TryAllocateId( &stagedIds, &id );
            if ( status == geometry_status_t::OK ) {
                status = TryAddAuthoredFace(
                    &result,
                    span_t<const u32>{ indices, 4u },
                    span_t<const mesh_corner_attributes_t>{ attributes, 4u },
                    id, faceAttributes );
            }
        } else {
            const u32 triangleCorners[2][3] = {
                { 0u, 1u, wall.bDiagonal02 ? 2u : 3u },
                { wall.bDiagonal02 ? 0u : 1u, 2u, 3u }
            };
            for ( u32 iTriangle = 0u;
                  iTriangle < 2u && status == geometry_status_t::OK;
                  ++iTriangle ) {
                u32 triangleIndices[3]{};
                mesh_corner_attributes_t triangleAttributes[3]{};
                for ( u32 k = 0u; k < 3u; ++k ) {
                    const u32 iCorner = triangleCorners[iTriangle][k];
                    triangleIndices[k] = indices[iCorner];
                    triangleAttributes[k] = attributes[iCorner];
                }
                geometry_source_id_t id{};
                status = TryAllocateId( &stagedIds, &id );
                if ( status == geometry_status_t::OK ) {
                    status = TryAddAuthoredFace(
                        &result,
                        span_t<const u32>{ triangleIndices, 3u },
                        span_t<const mesh_corner_attributes_t>{
                            triangleAttributes, 3u },
                        id, faceAttributes );
                }
            }
        }
        if ( status != geometry_status_t::OK ) {
            cleanup();
            return status;
        }
    }

    for ( usize i = 0u; i < source.edges.nCount; ++i ) {
        const mesh_source_edge_t &edge = source.edges.pData[i];
        status = MeshSourceDescription_TrySetEdge(
            &result, edge.iVertexA, edge.iVertexB,
            edge.attributes, edge.creaseWeight );
        if ( status == geometry_status_t::OK ) {
            status = MeshSourceDescription_TrySetEdge(
                &result,
                edge.iVertexA + static_cast<u32>( cVertices ),
                edge.iVertexB + static_cast<u32>( cVertices ),
                edge.attributes, edge.creaseWeight );
        }
        if ( status != geometry_status_t::OK ) {
            cleanup();
            return status;
        }
    }

    status = MeshSource_TryBuild(
        &result, pAllocator, pResultOut );
    if ( status == geometry_status_t::OK &&
         ( EditableMesh_VertexCount( &pResultOut->mesh ) !=
               cResultVertices ||
           EditableMesh_HalfEdgeCount( &pResultOut->mesh ) !=
               cResultCorners ||
           EditableMesh_EdgeCount( &pResultOut->mesh ) !=
               cResultEdges ||
           EditableMesh_LoopCount( &pResultOut->mesh ) !=
               cResultFaces ||
           EditableMesh_FaceCount( &pResultOut->mesh ) !=
               cResultFaces ||
           EditableMesh_ShellCount( &pResultOut->mesh ) != cShells ) ) {
        status = geometry_status_t::CORRUPT_STATE;
    }
    if ( status == geometry_status_t::OK ) {
        const mesh_source_validation_t outputValidation =
            MeshSource_Validate( pResultOut, pAllocator );
        status = StatusFromSourceFault( outputValidation.fault );
    }
    if ( status == geometry_status_t::OK &&
         MeshBoundary_CountBoundaryEdges( &pResultOut->mesh ) != 0u ) {
        status = geometry_status_t::OPEN_VOLUME;
    }
    if ( status == geometry_status_t::OK ) {
        status = MeshValidation_Validate(
            &pResultOut->mesh ).status;
    }
    if ( status == geometry_status_t::OK ) {
        status = ValidatePositiveShellVolumes(
            &pResultOut->mesh, pAllocator, policy );
    }
    if ( status == geometry_status_t::OK ) {
        status = MeshValidation_ValidateGeometry(
            &pResultOut->mesh, policy, geometryOptions ).status;
    }
    if ( status != geometry_status_t::OK ) {
        MeshSource_Shutdown( pResultOut );
        cleanup();
        return status;
    }

    *pIdAllocator = stagedIds;
    if ( pReportOut != nullptr ) {
        pReportOut->cSourceVertices = static_cast<u32>( cVertices );
        pReportOut->cSourceFaces = static_cast<u32>( cFaces );
        pReportOut->cBoundaryEdges = static_cast<u32>( walls.nCount );
        pReportOut->cOffsetVertices = static_cast<u32>( cVertices );
        pReportOut->cOffsetFaces = static_cast<u32>( cFaces );
        pReportOut->cSideFaces = static_cast<u32>( cSideFaces );
        pReportOut->cTriangulatedSideWalls =
            static_cast<u32>( cTriangulatedWalls );
        pReportOut->cNewSourceIds = static_cast<u32>(
            static_cast<u64>( cVertices ) +
            static_cast<u64>( cFaces ) + cSideFaces );
    }
    cleanup();
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
