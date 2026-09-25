//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshSubdivision.cpp
//  Purpose: Implements checked Catmull-Clark and linear mesh subdivision.
//  Details: Subdivision is built out-of-place. The implementation preflights
//           exact output counts, allocates dynamic work storage for arbitrary
//           supported face sizes and valences, preserves disconnected shells
//           and crease weights, and publishes only a fully valid mesh.
//
//  History:
//  - Created by Karlo Siric on 2026-09-22
//  - Hardened failure atomicity and removed fixed topology ceilings on 2026-09-23
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshSubdivision.h"
#include "CypherGeometry_MeshValidation.h"
#include "CypherCommon_Vector.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

enum class subdivision_scheme_t : u8 {
    LINEAR = 0u,
    CATMULL_CLARK
};

struct subdivision_counts_t {
    usize cVerticesIn{ 0u };
    usize cEdgesIn{ 0u };
    usize cFacesIn{ 0u };
    usize cHalfEdgesIn{ 0u };
    usize cShellsIn{ 0u };
    usize cCorners{ 0u };
    usize cVerticesOut{ 0u };
    usize cHalfEdgesOut{ 0u };
    usize cEdgesOut{ 0u };
    usize cLoopsOut{ 0u };
    usize cFacesOut{ 0u };
    usize cShellsOut{ 0u };
};

struct face_work_t {
    geometry_mesh_face_handle_t hFace{};
    geometry_mesh_shell_handle_t hSourceShell{};
    geometry_mesh_vertex_handle_t hFacePointOut{};
    usize iFirstCorner{ 0u };
    u32 cEdges{ 0u };
    u32 iSourceSide{ CY_INVALID_INDEX };
};

struct directed_edge_t {
    geometry_mesh_half_edge_handle_t hHalfEdge{};
    u32 iOriginSlot{ CY_INVALID_INDEX };
    u32 iDestinationSlot{ CY_INVALID_INDEX };
    f64 creaseWeight{ 0.0 };
};

template <typename handle_t>
struct slot_handle_map_t {
    vector_t<handle_t> handles;
};

struct subdivision_work_t {
    vector_t<face_work_t> faces;
    vector_t<geometry_mesh_half_edge_handle_t> faceHalfEdges;
    vector_t<geometry_mesh_vertex_handle_t> faceVertices;
    vector_t<directed_edge_t> directedEdges;
    slot_handle_map_t<geometry_mesh_vertex_handle_t> vertexPoints;
    slot_handle_map_t<geometry_mesh_vertex_handle_t> edgePoints;
    slot_handle_map_t<geometry_mesh_vertex_handle_t> facePoints;
    slot_handle_map_t<geometry_mesh_shell_handle_t> shells;
};

template <typename type_t>
geometry_status_t TryInitSizedVector(
    vector_t<type_t> *pVector,
    const allocator_t *pAllocator,
    usize cElements ) noexcept
{
    if ( !Vector_Init( pVector, pAllocator ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    if ( !Vector_Resize( pVector, cElements ) ) {
        Vector_Shutdown( pVector );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    return geometry_status_t::OK;
}

template <typename handle_t>
geometry_status_t TryInitSlotMap(
    slot_handle_map_t<handle_t> *pMap,
    const allocator_t *pAllocator,
    usize cSlots ) noexcept
{
    const geometry_status_t status = TryInitSizedVector(
        &pMap->handles, pAllocator, cSlots );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    for ( usize i = 0u; i < cSlots; ++i ) {
        pMap->handles.pData[i] = handle_t{};
    }
    return geometry_status_t::OK;
}

bool MeshIsEmpty( const editable_mesh_t &mesh ) noexcept
{
    return GenerationPool_Count( &mesh.vertices ) == 0u &&
           GenerationPool_Count( &mesh.halfEdges ) == 0u &&
           GenerationPool_Count( &mesh.edges ) == 0u &&
           GenerationPool_Count( &mesh.loops ) == 0u &&
           GenerationPool_Count( &mesh.faces ) == 0u &&
           GenerationPool_Count( &mesh.shells ) == 0u;
}

bool MeshStorageIsValid( const editable_mesh_t &mesh ) noexcept
{
    return Allocator_IsValid( mesh.pAllocator ) &&
           GenerationPool_IsValid( &mesh.vertices ) &&
           GenerationPool_IsValid( &mesh.halfEdges ) &&
           GenerationPool_IsValid( &mesh.edges ) &&
           GenerationPool_IsValid( &mesh.loops ) &&
           GenerationPool_IsValid( &mesh.faces ) &&
           GenerationPool_IsValid( &mesh.shells );
}

template <typename handle_t>
bool HandlesEqual( handle_t a, handle_t b ) noexcept
{
    return a.nSlot == b.nSlot && a.nGeneration == b.nGeneration;
}

bool TryAddCount( usize a, usize b, usize *pOut ) noexcept
{
    if ( b > CY_USIZE_MAX - a ) {
        return false;
    }
    *pOut = a + b;
    return true;
}

bool TryMultiplyCount( usize a, usize b, usize *pOut ) noexcept
{
    if ( a != 0u && b > CY_USIZE_MAX / a ) {
        return false;
    }
    *pOut = a * b;
    return true;
}

geometry_status_t TryComputeNormal(
    const math::vec3d_t *pVertices,
    usize cVertices,
    math::vec3d_t *pNormalOut ) noexcept
{
    if ( pVertices == nullptr || pNormalOut == nullptr || cVertices < 3u ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    math::vec3d_t normal = math::Vec3d_Make( 0.0, 0.0, 0.0 );
    for ( usize i = 0u; i < cVertices; ++i ) {
        const math::vec3d_t &a = pVertices[i];
        const math::vec3d_t &b = pVertices[( i + 1u ) % cVertices];
        if ( !math::Vec3d_IsFinite( a ) || !math::Vec3d_IsFinite( b ) ) {
            return geometry_status_t::NUMERIC_FAILURE;
        }
        normal.x += ( a.y - b.y ) * ( a.z + b.z );
        normal.y += ( a.z - b.z ) * ( a.x + b.x );
        normal.z += ( a.x - b.x ) * ( a.y + b.y );
        if ( !math::Vec3d_IsFinite( normal ) ) {
            return geometry_status_t::NUMERIC_FAILURE;
        }
    }
    const f64 lengthSquared = math::Vec3d_LengthSquared( normal );
    if ( !std::isfinite( lengthSquared ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }
    if ( lengthSquared <= 1.0e-30 ) {
        return geometry_status_t::DEGENERATE;
    }
    *pNormalOut = math::Vec3d_Scale(
        normal, 1.0 / std::sqrt( lengthSquared ) );
    return math::Vec3d_IsFinite( *pNormalOut )
        ? geometry_status_t::OK
        : geometry_status_t::NUMERIC_FAILURE;
}

geometry_status_t ValidateAndCountInput(
    const editable_mesh_t &meshIn,
    const editable_mesh_t &meshOut,
    subdivision_counts_t *pCountsOut ) noexcept
{
    subdivision_counts_t counts{};
    counts.cVerticesIn = EditableMesh_VertexCount( &meshIn );
    counts.cEdgesIn = EditableMesh_EdgeCount( &meshIn );
    counts.cFacesIn = EditableMesh_FaceCount( &meshIn );
    counts.cHalfEdgesIn = EditableMesh_HalfEdgeCount( &meshIn );
    counts.cShellsIn = EditableMesh_ShellCount( &meshIn );

    if ( counts.cVerticesIn == 0u || counts.cFacesIn == 0u ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( counts.cVerticesIn < 4u || counts.cEdgesIn < 6u ||
         counts.cFacesIn < 4u || counts.cHalfEdgesIn < 12u ||
         counts.cShellsIn == 0u ||
         EditableMesh_LoopCount( &meshIn ) != counts.cFacesIn ) {
        return geometry_status_t::DEGENERATE;
    }

    geometry_status_t status = geometry_status_t::OK;
    (void)GenerationPool_ForEach( &meshIn.vertices,
        [&]( geometry_mesh_vertex_handle_t,
             const mesh_vertex_record_t &vertex ) noexcept -> bool_t {
            if ( !math::Vec3d_IsFinite( vertex.position ) ) {
                status = geometry_status_t::NUMERIC_FAILURE;
                return false;
            }
            return true;
        } );
    if ( status != geometry_status_t::OK ) { return status; }

    (void)GenerationPool_ForEach( &meshIn.edges,
        [&]( geometry_mesh_edge_handle_t,
             const mesh_edge_record_t &edge ) noexcept -> bool_t {
            if ( !std::isfinite( edge.creaseWeight ) ||
                 edge.creaseWeight < 0.0 || edge.creaseWeight > 1.0 ) {
                status = geometry_status_t::INVALID_TOPOLOGY;
                return false;
            }
            return true;
        } );
    if ( status != geometry_status_t::OK ) { return status; }

    (void)GenerationPool_ForEach( &meshIn.faces,
        [&]( geometry_mesh_face_handle_t hFace,
             const mesh_face_record_t &face ) noexcept -> bool_t {
            const mesh_loop_record_t *pLoop = GenerationPool_Get(
                &meshIn.loops, face.hOuterLoop );
            const mesh_shell_record_t *pShell = GenerationPool_Get(
                &meshIn.shells, face.hShell );
            if ( pLoop == nullptr || pShell == nullptr ||
                 pLoop->cHalfEdges < 3u ||
                 !HandlesEqual( pLoop->hFace, hFace ) ) {
                status = geometry_status_t::INVALID_TOPOLOGY;
                return false;
            }
            if ( static_cast<usize>( pLoop->cHalfEdges ) >
                 CY_USIZE_MAX - counts.cCorners ) {
                status = geometry_status_t::LIMIT_EXCEEDED;
                return false;
            }
            counts.cCorners += static_cast<usize>( pLoop->cHalfEdges );
            return true;
        } );
    if ( status != geometry_status_t::OK ) { return status; }
    if ( counts.cCorners != counts.cHalfEdgesIn ||
         ( counts.cHalfEdgesIn & 1u ) != 0u ) {
        return geometry_status_t::INVALID_TOPOLOGY;
    }

    (void)GenerationPool_ForEach( &meshIn.shells,
        [&]( geometry_mesh_shell_handle_t hShell,
             const mesh_shell_record_t &shell ) noexcept -> bool_t {
            usize cFaces = 0u;
            usize cCorners = 0u;
            (void)GenerationPool_ForEach( &meshIn.faces,
                [&]( geometry_mesh_face_handle_t,
                     const mesh_face_record_t &face ) noexcept -> bool_t {
                    if ( !HandlesEqual( face.hShell, hShell ) ) {
                        return true;
                    }
                    const mesh_loop_record_t *pLoop = GenerationPool_Get(
                        &meshIn.loops, face.hOuterLoop );
                    if ( pLoop == nullptr ) {
                        status = geometry_status_t::INVALID_TOPOLOGY;
                        return false;
                    }
                    ++cFaces;
                    cCorners += static_cast<usize>( pLoop->cHalfEdges );
                    return true;
                } );
            const mesh_face_record_t *pAnyFace = GenerationPool_Get(
                &meshIn.faces, shell.hAnyFace );
            if ( status != geometry_status_t::OK || cFaces == 0u ||
                 cFaces != static_cast<usize>( shell.cFaces ) ||
                 pAnyFace == nullptr ||
                 !HandlesEqual( pAnyFace->hShell, hShell ) ||
                 cCorners > static_cast<usize>(
                     std::numeric_limits<u32>::max() ) ) {
                status = cCorners > static_cast<usize>(
                    std::numeric_limits<u32>::max() )
                    ? geometry_status_t::LIMIT_EXCEEDED
                    : geometry_status_t::INVALID_TOPOLOGY;
                return false;
            }
            return true;
        } );
    if ( status != geometry_status_t::OK ) { return status; }

    const mesh_validation_result_t validation =
        MeshValidation_Validate( &meshIn );
    if ( validation.status != geometry_status_t::OK ) {
        return validation.status;
    }

    usize vertexAndEdgeCount = 0u;
    if ( !TryAddCount(
             counts.cVerticesIn, counts.cEdgesIn,
             &vertexAndEdgeCount ) ||
         !TryAddCount(
             vertexAndEdgeCount, counts.cFacesIn,
             &counts.cVerticesOut ) ||
         !TryMultiplyCount(
             counts.cCorners, 4u, &counts.cHalfEdgesOut ) ||
         !TryMultiplyCount(
             counts.cCorners, 2u, &counts.cEdgesOut ) ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    counts.cLoopsOut = counts.cCorners;
    counts.cFacesOut = counts.cCorners;
    counts.cShellsOut = counts.cShellsIn;

    const usize cPoolMaximum = CY_GENERATION_POOL_MAX_CAPACITY;
    if ( counts.cVerticesOut > meshOut.limits.cVertexMax ||
         counts.cHalfEdgesOut > meshOut.limits.cHalfEdgeMax ||
         counts.cEdgesOut > meshOut.limits.cEdgeMax ||
         counts.cLoopsOut > meshOut.limits.cLoopMax ||
         counts.cFacesOut > meshOut.limits.cFaceMax ||
         counts.cShellsOut > meshOut.limits.cShellMax ||
         counts.cVerticesOut > cPoolMaximum ||
         counts.cHalfEdgesOut > cPoolMaximum ||
         counts.cEdgesOut > cPoolMaximum ||
         counts.cLoopsOut > cPoolMaximum ||
         counts.cFacesOut > cPoolMaximum ||
         counts.cShellsOut > cPoolMaximum ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    *pCountsOut = counts;
    return geometry_status_t::OK;
}

geometry_status_t TryPrepareWork(
    subdivision_work_t *pWork,
    const editable_mesh_t &meshIn,
    const subdivision_counts_t &counts,
    const allocator_t *pAllocator ) noexcept
{
    geometry_status_t status = TryInitSizedVector(
        &pWork->faces, pAllocator, counts.cFacesIn );
    if ( status != geometry_status_t::OK ) { return status; }
    status = TryInitSizedVector(
        &pWork->faceHalfEdges, pAllocator, counts.cCorners );
    if ( status != geometry_status_t::OK ) { return status; }
    status = TryInitSizedVector(
        &pWork->faceVertices, pAllocator, counts.cCorners );
    if ( status != geometry_status_t::OK ) { return status; }
    status = TryInitSizedVector(
        &pWork->directedEdges, pAllocator, counts.cHalfEdgesOut );
    if ( status != geometry_status_t::OK ) { return status; }
    status = TryInitSlotMap(
        &pWork->vertexPoints, pAllocator,
        GenerationPool_Capacity( &meshIn.vertices ) );
    if ( status != geometry_status_t::OK ) { return status; }
    status = TryInitSlotMap(
        &pWork->edgePoints, pAllocator,
        GenerationPool_Capacity( &meshIn.edges ) );
    if ( status != geometry_status_t::OK ) { return status; }
    status = TryInitSlotMap(
        &pWork->facePoints, pAllocator,
        GenerationPool_Capacity( &meshIn.faces ) );
    if ( status != geometry_status_t::OK ) { return status; }
    return TryInitSlotMap(
        &pWork->shells, pAllocator,
        GenerationPool_Capacity( &meshIn.shells ) );
}

geometry_status_t TryReserveOutput(
    editable_mesh_t *pMeshOut,
    const subdivision_counts_t &counts ) noexcept
{
    auto reserve = []( auto *pPool, usize cRecords ) noexcept {
        return GeometryStatus_FromGenerationPoolStatus(
            GenerationPool_Reserve( pPool, cRecords ) );
    };
    geometry_status_t status = reserve(
        &pMeshOut->vertices, counts.cVerticesOut );
    if ( status != geometry_status_t::OK ) { return status; }
    status = reserve( &pMeshOut->halfEdges, counts.cHalfEdgesOut );
    if ( status != geometry_status_t::OK ) { return status; }
    status = reserve( &pMeshOut->edges, counts.cEdgesOut );
    if ( status != geometry_status_t::OK ) { return status; }
    status = reserve( &pMeshOut->loops, counts.cLoopsOut );
    if ( status != geometry_status_t::OK ) { return status; }
    status = reserve( &pMeshOut->faces, counts.cFacesOut );
    if ( status != geometry_status_t::OK ) { return status; }
    return reserve( &pMeshOut->shells, counts.cShellsOut );
}

geometry_status_t TryCreateShells(
    const editable_mesh_t &meshIn,
    editable_mesh_t *pMeshOut,
    subdivision_work_t *pWork ) noexcept
{
    geometry_status_t status = geometry_status_t::OK;
    (void)GenerationPool_ForEach( &meshIn.shells,
        [&]( geometry_mesh_shell_handle_t hSourceShell,
             const mesh_shell_record_t & ) noexcept -> bool_t {
            const auto result = GenerationPool_Insert(
                &pMeshOut->shells, mesh_shell_record_t{} );
            if ( result.status != generation_pool_status_t::OK ) {
                status = GeometryStatus_FromGenerationPoolStatus(
                    result.status );
                return false;
            }
            if ( static_cast<usize>( hSourceShell.nSlot ) >=
                 pWork->shells.handles.nCount ) {
                status = geometry_status_t::CORRUPT_STATE;
                return false;
            }
            pWork->shells.handles.pData[hSourceShell.nSlot] =
                result.handle;
            return true;
        } );
    return status;
}

geometry_status_t TryCollectFacesAndCreateFacePoints(
    const editable_mesh_t &meshIn,
    editable_mesh_t *pMeshOut,
    subdivision_work_t *pWork,
    const subdivision_counts_t &counts ) noexcept
{
    geometry_status_t status = geometry_status_t::OK;
    usize iFaceWork = 0u;
    usize iCorner = 0u;
    (void)GenerationPool_ForEach( &meshIn.faces,
        [&]( geometry_mesh_face_handle_t hFace,
             const mesh_face_record_t &face ) noexcept -> bool_t {
            if ( iFaceWork >= pWork->faces.nCount ) {
                status = geometry_status_t::CORRUPT_STATE;
                return false;
            }
            const mesh_loop_record_t *pLoop = GenerationPool_Get(
                &meshIn.loops, face.hOuterLoop );
            if ( pLoop == nullptr || pLoop->cHalfEdges < 3u ||
                 iCorner > pWork->faceHalfEdges.nCount ||
                 static_cast<usize>( pLoop->cHalfEdges ) >
                     pWork->faceHalfEdges.nCount - iCorner ) {
                status = geometry_status_t::INVALID_TOPOLOGY;
                return false;
            }
            face_work_t &work = pWork->faces.pData[iFaceWork];
            work.hFace = hFace;
            work.hSourceShell = face.hShell;
            work.iFirstCorner = iCorner;
            work.cEdges = pLoop->cHalfEdges;
            work.iSourceSide = face.iSourceSide;

            math::vec3d_t centroid = math::Vec3d_Make( 0.0, 0.0, 0.0 );
            geometry_mesh_half_edge_handle_t hCurrent =
                pLoop->hFirstHalfEdge;
            for ( u32 i = 0u; i < pLoop->cHalfEdges; ++i ) {
                const mesh_half_edge_record_t *pHalfEdge =
                    GenerationPool_Get( &meshIn.halfEdges, hCurrent );
                const mesh_vertex_record_t *pVertex = pHalfEdge != nullptr
                    ? GenerationPool_Get(
                        &meshIn.vertices, pHalfEdge->hOrigin )
                    : nullptr;
                if ( pHalfEdge == nullptr || pVertex == nullptr ) {
                    status = geometry_status_t::INVALID_TOPOLOGY;
                    return false;
                }
                pWork->faceHalfEdges.pData[iCorner + i] = hCurrent;
                pWork->faceVertices.pData[iCorner + i] =
                    pHalfEdge->hOrigin;
                centroid = math::Vec3d_Add( centroid, pVertex->position );
                if ( !math::Vec3d_IsFinite( centroid ) ) {
                    status = geometry_status_t::NUMERIC_FAILURE;
                    return false;
                }
                hCurrent = pHalfEdge->hNext;
            }
            if ( !HandlesEqual( hCurrent, pLoop->hFirstHalfEdge ) ) {
                status = geometry_status_t::INVALID_TOPOLOGY;
                return false;
            }
            centroid = math::Vec3d_Scale(
                centroid, 1.0 / static_cast<f64>( pLoop->cHalfEdges ) );
            if ( !math::Vec3d_IsFinite( centroid ) ) {
                status = geometry_status_t::NUMERIC_FAILURE;
                return false;
            }
            mesh_vertex_record_t facePoint{};
            facePoint.position = centroid;
            const auto inserted = GenerationPool_Insert(
                &pMeshOut->vertices, facePoint );
            if ( inserted.status != generation_pool_status_t::OK ) {
                status = GeometryStatus_FromGenerationPoolStatus(
                    inserted.status );
                return false;
            }
            work.hFacePointOut = inserted.handle;
            if ( static_cast<usize>( hFace.nSlot ) >=
                 pWork->facePoints.handles.nCount ) {
                status = geometry_status_t::CORRUPT_STATE;
                return false;
            }
            pWork->facePoints.handles.pData[hFace.nSlot] = inserted.handle;
            iCorner += static_cast<usize>( pLoop->cHalfEdges );
            ++iFaceWork;
            return true;
        } );
    if ( status == geometry_status_t::OK &&
         ( iFaceWork != counts.cFacesIn || iCorner != counts.cCorners ) ) {
        status = geometry_status_t::CORRUPT_STATE;
    }
    return status;
}

geometry_status_t TryCreateEdgePoints(
    subdivision_scheme_t scheme,
    const editable_mesh_t &meshIn,
    editable_mesh_t *pMeshOut,
    subdivision_work_t *pWork ) noexcept
{
    geometry_status_t status = geometry_status_t::OK;
    (void)GenerationPool_ForEach( &meshIn.edges,
        [&]( geometry_mesh_edge_handle_t hEdge,
             const mesh_edge_record_t &edge ) noexcept -> bool_t {
            const mesh_half_edge_record_t *pHalfEdge = GenerationPool_Get(
                &meshIn.halfEdges, edge.hHalfEdge );
            const mesh_half_edge_record_t *pTwin = pHalfEdge != nullptr
                ? GenerationPool_Get(
                    &meshIn.halfEdges, pHalfEdge->hTwin )
                : nullptr;
            const mesh_vertex_record_t *pA = pHalfEdge != nullptr
                ? GenerationPool_Get(
                    &meshIn.vertices, pHalfEdge->hOrigin )
                : nullptr;
            const mesh_vertex_record_t *pB = pTwin != nullptr
                ? GenerationPool_Get(
                    &meshIn.vertices, pTwin->hOrigin )
                : nullptr;
            if ( pHalfEdge == nullptr || pTwin == nullptr ||
                 pA == nullptr || pB == nullptr ) {
                status = geometry_status_t::INVALID_TOPOLOGY;
                return false;
            }
            const math::vec3d_t midpoint = math::Vec3d_Scale(
                math::Vec3d_Add( pA->position, pB->position ), 0.5 );
            math::vec3d_t point = midpoint;
            if ( scheme == subdivision_scheme_t::CATMULL_CLARK ) {
                const mesh_loop_record_t *pLoopA = GenerationPool_Get(
                    &meshIn.loops, pHalfEdge->hLoop );
                const mesh_loop_record_t *pLoopB = GenerationPool_Get(
                    &meshIn.loops, pTwin->hLoop );
                if ( pLoopA == nullptr || pLoopB == nullptr ||
                     static_cast<usize>( pLoopA->hFace.nSlot ) >=
                         pWork->facePoints.handles.nCount ||
                     static_cast<usize>( pLoopB->hFace.nSlot ) >=
                         pWork->facePoints.handles.nCount ) {
                    status = geometry_status_t::INVALID_TOPOLOGY;
                    return false;
                }
                const mesh_vertex_record_t *pFaceA = GenerationPool_Get(
                    &pMeshOut->vertices,
                    pWork->facePoints.handles.pData[pLoopA->hFace.nSlot] );
                const mesh_vertex_record_t *pFaceB = GenerationPool_Get(
                    &pMeshOut->vertices,
                    pWork->facePoints.handles.pData[pLoopB->hFace.nSlot] );
                if ( pFaceA == nullptr || pFaceB == nullptr ) {
                    status = geometry_status_t::CORRUPT_STATE;
                    return false;
                }
                const math::vec3d_t smooth = math::Vec3d_Scale(
                    math::Vec3d_Add(
                        math::Vec3d_Add( pA->position, pB->position ),
                        math::Vec3d_Add(
                            pFaceA->position, pFaceB->position ) ),
                    0.25 );
                point = math::Vec3d_Add(
                    math::Vec3d_Scale( smooth, 1.0 - edge.creaseWeight ),
                    math::Vec3d_Scale( midpoint, edge.creaseWeight ) );
            }
            if ( !math::Vec3d_IsFinite( point ) ) {
                status = geometry_status_t::NUMERIC_FAILURE;
                return false;
            }
            mesh_vertex_record_t record{};
            record.position = point;
            const auto inserted = GenerationPool_Insert(
                &pMeshOut->vertices, record );
            if ( inserted.status != generation_pool_status_t::OK ) {
                status = GeometryStatus_FromGenerationPoolStatus(
                    inserted.status );
                return false;
            }
            if ( static_cast<usize>( hEdge.nSlot ) >=
                 pWork->edgePoints.handles.nCount ) {
                status = geometry_status_t::CORRUPT_STATE;
                return false;
            }
            pWork->edgePoints.handles.pData[hEdge.nSlot] = inserted.handle;
            return true;
        } );
    return status;
}

geometry_status_t TryCreateVertexPoints(
    subdivision_scheme_t scheme,
    const editable_mesh_t &meshIn,
    editable_mesh_t *pMeshOut,
    subdivision_work_t *pWork,
    const subdivision_counts_t &counts ) noexcept
{
    geometry_status_t status = geometry_status_t::OK;
    (void)GenerationPool_ForEach( &meshIn.vertices,
        [&]( geometry_mesh_vertex_handle_t hVertex,
             const mesh_vertex_record_t &vertex ) noexcept -> bool_t {
            math::vec3d_t newPosition = vertex.position;
            if ( scheme == subdivision_scheme_t::CATMULL_CLARK ) {
                math::vec3d_t facePointSum =
                    math::Vec3d_Make( 0.0, 0.0, 0.0 );
                math::vec3d_t midpointSum =
                    math::Vec3d_Make( 0.0, 0.0, 0.0 );
                math::vec3d_t creaseNeighborSum =
                    math::Vec3d_Make( 0.0, 0.0, 0.0 );
                u32 valence = 0u;
                u32 cCreasedEdges = 0u;
                f64 maximumCrease = 0.0;
                bool bClosed = false;
                geometry_mesh_half_edge_handle_t hCurrent =
                    vertex.hOutHalfEdge;
                const geometry_mesh_half_edge_handle_t hStart = hCurrent;
                for ( usize i = 0u; i < counts.cHalfEdgesIn; ++i ) {
                    const mesh_half_edge_record_t *pHalfEdge =
                        GenerationPool_Get( &meshIn.halfEdges, hCurrent );
                    if ( pHalfEdge == nullptr ||
                         pHalfEdge->hOrigin.nSlot != hVertex.nSlot ||
                         pHalfEdge->hOrigin.nGeneration !=
                             hVertex.nGeneration ) {
                        status = geometry_status_t::INVALID_TOPOLOGY;
                        return false;
                    }
                    const mesh_half_edge_record_t *pTwin =
                        GenerationPool_Get(
                            &meshIn.halfEdges, pHalfEdge->hTwin );
                    const mesh_loop_record_t *pLoop = GenerationPool_Get(
                        &meshIn.loops, pHalfEdge->hLoop );
                    const mesh_edge_record_t *pEdge = GenerationPool_Get(
                        &meshIn.edges, pHalfEdge->hEdge );
                    const mesh_vertex_record_t *pNeighbor = pTwin != nullptr
                        ? GenerationPool_Get(
                            &meshIn.vertices, pTwin->hOrigin )
                        : nullptr;
                    if ( pTwin == nullptr || pLoop == nullptr ||
                         pEdge == nullptr || pNeighbor == nullptr ||
                         static_cast<usize>( pLoop->hFace.nSlot ) >=
                             pWork->facePoints.handles.nCount ) {
                        status = geometry_status_t::INVALID_TOPOLOGY;
                        return false;
                    }
                    const mesh_vertex_record_t *pFacePoint =
                        GenerationPool_Get(
                            &pMeshOut->vertices,
                            pWork->facePoints.handles.pData[
                                pLoop->hFace.nSlot] );
                    if ( pFacePoint == nullptr ) {
                        status = geometry_status_t::CORRUPT_STATE;
                        return false;
                    }
                    facePointSum = math::Vec3d_Add(
                        facePointSum, pFacePoint->position );
                    midpointSum = math::Vec3d_Add(
                        midpointSum,
                        math::Vec3d_Scale(
                            math::Vec3d_Add(
                                vertex.position, pNeighbor->position ),
                            0.5 ) );
                    if ( pEdge->creaseWeight > 0.0 ) {
                        ++cCreasedEdges;
                        maximumCrease = std::fmax(
                            maximumCrease, pEdge->creaseWeight );
                        creaseNeighborSum = math::Vec3d_Add(
                            creaseNeighborSum, pNeighbor->position );
                    }
                    if ( !math::Vec3d_IsFinite( facePointSum ) ||
                         !math::Vec3d_IsFinite( midpointSum ) ||
                         !math::Vec3d_IsFinite( creaseNeighborSum ) ) {
                        status = geometry_status_t::NUMERIC_FAILURE;
                        return false;
                    }
                    ++valence;
                    hCurrent = pTwin->hNext;
                    if ( HandlesEqual( hCurrent, hStart ) ) {
                        bClosed = true;
                        break;
                    }
                }
                if ( !bClosed || valence < 3u ) {
                    status = geometry_status_t::INVALID_TOPOLOGY;
                    return false;
                }
                const f64 n = static_cast<f64>( valence );
                const math::vec3d_t faceAverage = math::Vec3d_Scale(
                    facePointSum, 1.0 / n );
                const math::vec3d_t midpointAverage = math::Vec3d_Scale(
                    midpointSum, 1.0 / n );
                const math::vec3d_t smoothPosition = math::Vec3d_Scale(
                    math::Vec3d_Add(
                        math::Vec3d_Add(
                            faceAverage,
                            math::Vec3d_Scale( midpointAverage, 2.0 ) ),
                        math::Vec3d_Scale(
                            vertex.position, n - 3.0 ) ),
                    1.0 / n );
                newPosition = smoothPosition;
                if ( cCreasedEdges >= 2u ) {
                    const math::vec3d_t creasePosition =
                        cCreasedEdges == 2u
                        ? math::Vec3d_Scale(
                            math::Vec3d_Add(
                                math::Vec3d_Scale(
                                    vertex.position, 6.0 ),
                                creaseNeighborSum ),
                            0.125 )
                        : vertex.position;
                    newPosition = math::Vec3d_Add(
                        math::Vec3d_Scale(
                            smoothPosition, 1.0 - maximumCrease ),
                        math::Vec3d_Scale(
                            creasePosition, maximumCrease ) );
                }
            }
            if ( !math::Vec3d_IsFinite( newPosition ) ) {
                status = geometry_status_t::NUMERIC_FAILURE;
                return false;
            }
            mesh_vertex_record_t record{};
            record.position = newPosition;
            const auto inserted = GenerationPool_Insert(
                &pMeshOut->vertices, record );
            if ( inserted.status != generation_pool_status_t::OK ) {
                status = GeometryStatus_FromGenerationPoolStatus(
                    inserted.status );
                return false;
            }
            if ( static_cast<usize>( hVertex.nSlot ) >=
                 pWork->vertexPoints.handles.nCount ) {
                status = geometry_status_t::CORRUPT_STATE;
                return false;
            }
            pWork->vertexPoints.handles.pData[hVertex.nSlot] =
                inserted.handle;
            return true;
        } );
    return status;
}

u32 UndirectedMinimum( const directed_edge_t &edge ) noexcept
{
    return edge.iOriginSlot < edge.iDestinationSlot
        ? edge.iOriginSlot : edge.iDestinationSlot;
}

u32 UndirectedMaximum( const directed_edge_t &edge ) noexcept
{
    return edge.iOriginSlot < edge.iDestinationSlot
        ? edge.iDestinationSlot : edge.iOriginSlot;
}

bool SameUndirectedEdge(
    const directed_edge_t &a,
    const directed_edge_t &b ) noexcept
{
    return UndirectedMinimum( a ) == UndirectedMinimum( b ) &&
           UndirectedMaximum( a ) == UndirectedMaximum( b );
}

geometry_status_t TryPairHalfEdges(
    editable_mesh_t *pMeshOut,
    vector_t<directed_edge_t> *pDirectedEdges ) noexcept
{
    std::sort(
        pDirectedEdges->pData,
        pDirectedEdges->pData + pDirectedEdges->nCount,
        []( const directed_edge_t &a,
            const directed_edge_t &b ) noexcept {
            const u32 aMinimum = UndirectedMinimum( a );
            const u32 bMinimum = UndirectedMinimum( b );
            if ( aMinimum != bMinimum ) { return aMinimum < bMinimum; }
            const u32 aMaximum = UndirectedMaximum( a );
            const u32 bMaximum = UndirectedMaximum( b );
            if ( aMaximum != bMaximum ) { return aMaximum < bMaximum; }
            if ( a.iOriginSlot != b.iOriginSlot ) {
                return a.iOriginSlot < b.iOriginSlot;
            }
            return a.iDestinationSlot < b.iDestinationSlot;
        } );
    usize i = 0u;
    while ( i < pDirectedEdges->nCount ) {
        usize iEnd = i + 1u;
        while ( iEnd < pDirectedEdges->nCount &&
                SameUndirectedEdge(
                    pDirectedEdges->pData[i],
                    pDirectedEdges->pData[iEnd] ) ) {
            ++iEnd;
        }
        if ( iEnd - i != 2u ) {
            return geometry_status_t::NON_MANIFOLD;
        }
        const directed_edge_t &a = pDirectedEdges->pData[i];
        const directed_edge_t &b = pDirectedEdges->pData[i + 1u];
        if ( a.iOriginSlot != b.iDestinationSlot ||
             a.iDestinationSlot != b.iOriginSlot ||
             a.iOriginSlot == a.iDestinationSlot ||
             std::fabs( a.creaseWeight - b.creaseWeight ) > 1.0e-12 ) {
            return geometry_status_t::NON_MANIFOLD;
        }
        mesh_half_edge_record_t *pA = GenerationPool_Get(
            &pMeshOut->halfEdges, a.hHalfEdge );
        mesh_half_edge_record_t *pB = GenerationPool_Get(
            &pMeshOut->halfEdges, b.hHalfEdge );
        if ( pA == nullptr || pB == nullptr ) {
            return geometry_status_t::CORRUPT_STATE;
        }
        pA->hTwin = b.hHalfEdge;
        pB->hTwin = a.hHalfEdge;
        mesh_edge_record_t edge{};
        edge.hHalfEdge = a.hHalfEdge;
        edge.creaseWeight = a.creaseWeight;
        const auto inserted = GenerationPool_Insert(
            &pMeshOut->edges, edge );
        if ( inserted.status != generation_pool_status_t::OK ) {
            return GeometryStatus_FromGenerationPoolStatus(
                inserted.status );
        }
        pA->hEdge = inserted.handle;
        pB->hEdge = inserted.handle;
        i = iEnd;
    }
    return geometry_status_t::OK;
}

geometry_status_t TryBuildTopology(
    const editable_mesh_t &meshIn,
    editable_mesh_t *pMeshOut,
    subdivision_work_t *pWork,
    const subdivision_counts_t &counts ) noexcept
{
    usize iDirectedEdge = 0u;
    for ( usize iFace = 0u; iFace < pWork->faces.nCount; ++iFace ) {
        const face_work_t &work = pWork->faces.pData[iFace];
        if ( work.cEdges < 3u ||
             static_cast<usize>( work.hSourceShell.nSlot ) >=
                 pWork->shells.handles.nCount ) {
            return geometry_status_t::INVALID_TOPOLOGY;
        }
        const geometry_mesh_shell_handle_t hOutputShell =
            pWork->shells.handles.pData[work.hSourceShell.nSlot];
        mesh_shell_record_t *pOutputShell = GenerationPool_Get(
            &pMeshOut->shells, hOutputShell );
        if ( pOutputShell == nullptr ) {
            return geometry_status_t::CORRUPT_STATE;
        }
        for ( u32 iCorner = 0u; iCorner < work.cEdges; ++iCorner ) {
            const u32 iPrevious =
                ( iCorner + work.cEdges - 1u ) % work.cEdges;
            const usize iCurrentWork = work.iFirstCorner + iCorner;
            const usize iPreviousWork = work.iFirstCorner + iPrevious;
            const mesh_half_edge_record_t *pPreviousSourceHalfEdge =
                GenerationPool_Get(
                    &meshIn.halfEdges,
                    pWork->faceHalfEdges.pData[iPreviousWork] );
            const mesh_half_edge_record_t *pCurrentSourceHalfEdge =
                GenerationPool_Get(
                    &meshIn.halfEdges,
                    pWork->faceHalfEdges.pData[iCurrentWork] );
            if ( pPreviousSourceHalfEdge == nullptr ||
                 pCurrentSourceHalfEdge == nullptr ||
                 static_cast<usize>(
                     pPreviousSourceHalfEdge->hEdge.nSlot ) >=
                     pWork->edgePoints.handles.nCount ||
                 static_cast<usize>(
                     pCurrentSourceHalfEdge->hEdge.nSlot ) >=
                     pWork->edgePoints.handles.nCount ) {
                return geometry_status_t::INVALID_TOPOLOGY;
            }
            const mesh_edge_record_t *pPreviousSourceEdge =
                GenerationPool_Get(
                    &meshIn.edges,
                    pPreviousSourceHalfEdge->hEdge );
            const mesh_edge_record_t *pCurrentSourceEdge =
                GenerationPool_Get(
                    &meshIn.edges,
                    pCurrentSourceHalfEdge->hEdge );
            const geometry_mesh_vertex_handle_t hSourceVertex =
                pWork->faceVertices.pData[iCurrentWork];
            if ( pPreviousSourceEdge == nullptr ||
                 pCurrentSourceEdge == nullptr ||
                 static_cast<usize>( hSourceVertex.nSlot ) >=
                     pWork->vertexPoints.handles.nCount ) {
                return geometry_status_t::INVALID_TOPOLOGY;
            }
            const geometry_mesh_vertex_handle_t quadVertices[4] = {
                work.hFacePointOut,
                pWork->edgePoints.handles.pData[
                    pPreviousSourceHalfEdge->hEdge.nSlot],
                pWork->vertexPoints.handles.pData[hSourceVertex.nSlot],
                pWork->edgePoints.handles.pData[
                    pCurrentSourceHalfEdge->hEdge.nSlot]
            };
            const f64 creaseWeights[4] = {
                0.0,
                pPreviousSourceEdge->creaseWeight,
                pCurrentSourceEdge->creaseWeight,
                0.0
            };
            geometry_mesh_half_edge_handle_t quadHalfEdges[4]{};
            math::vec3d_t quadPositions[4]{};
            for ( u32 i = 0u; i < 4u; ++i ) {
                const mesh_vertex_record_t *pVertex = GenerationPool_Get(
                    &pMeshOut->vertices, quadVertices[i] );
                if ( pVertex == nullptr ||
                     iDirectedEdge >= pWork->directedEdges.nCount ) {
                    return geometry_status_t::CORRUPT_STATE;
                }
                quadPositions[i] = pVertex->position;
                mesh_half_edge_record_t halfEdge{};
                halfEdge.hOrigin = quadVertices[i];
                const auto inserted = GenerationPool_Insert(
                    &pMeshOut->halfEdges, halfEdge );
                if ( inserted.status != generation_pool_status_t::OK ) {
                    return GeometryStatus_FromGenerationPoolStatus(
                        inserted.status );
                }
                quadHalfEdges[i] = inserted.handle;
                const u32 iNext = ( i + 1u ) % 4u;
                directed_edge_t &directed =
                    pWork->directedEdges.pData[iDirectedEdge++];
                directed.hHalfEdge = inserted.handle;
                directed.iOriginSlot = quadVertices[i].nSlot;
                directed.iDestinationSlot = quadVertices[iNext].nSlot;
                directed.creaseWeight = creaseWeights[i];
                mesh_vertex_record_t *pMutableVertex = GenerationPool_Get(
                    &pMeshOut->vertices, quadVertices[i] );
                if ( pMutableVertex == nullptr ) {
                    return geometry_status_t::CORRUPT_STATE;
                }
                if ( !GenerationHandle_IsValid(
                         pMutableVertex->hOutHalfEdge ) ) {
                    pMutableVertex->hOutHalfEdge = inserted.handle;
                }
            }
            for ( u32 i = 0u; i < 4u; ++i ) {
                mesh_half_edge_record_t *pHalfEdge = GenerationPool_Get(
                    &pMeshOut->halfEdges, quadHalfEdges[i] );
                if ( pHalfEdge == nullptr ) {
                    return geometry_status_t::CORRUPT_STATE;
                }
                pHalfEdge->hNext = quadHalfEdges[( i + 1u ) % 4u];
                pHalfEdge->hPrev = quadHalfEdges[( i + 3u ) % 4u];
            }
            mesh_loop_record_t loop{};
            loop.hFirstHalfEdge = quadHalfEdges[0];
            loop.cHalfEdges = 4u;
            const auto insertedLoop = GenerationPool_Insert(
                &pMeshOut->loops, loop );
            if ( insertedLoop.status != generation_pool_status_t::OK ) {
                return GeometryStatus_FromGenerationPoolStatus(
                    insertedLoop.status );
            }
            for ( geometry_mesh_half_edge_handle_t hHalfEdge :
                  quadHalfEdges ) {
                mesh_half_edge_record_t *pHalfEdge = GenerationPool_Get(
                    &pMeshOut->halfEdges, hHalfEdge );
                if ( pHalfEdge == nullptr ) {
                    return geometry_status_t::CORRUPT_STATE;
                }
                pHalfEdge->hLoop = insertedLoop.handle;
            }
            math::vec3d_t normal{};
            geometry_status_t status = TryComputeNormal(
                quadPositions, 4u, &normal );
            if ( status != geometry_status_t::OK ) { return status; }
            mesh_face_record_t face{};
            face.hOuterLoop = insertedLoop.handle;
            face.hShell = hOutputShell;
            face.normal = normal;
            face.iSourceSide = work.iSourceSide;
            const auto insertedFace = GenerationPool_Insert(
                &pMeshOut->faces, face );
            if ( insertedFace.status != generation_pool_status_t::OK ) {
                return GeometryStatus_FromGenerationPoolStatus(
                    insertedFace.status );
            }
            mesh_loop_record_t *pLoop = GenerationPool_Get(
                &pMeshOut->loops, insertedLoop.handle );
            if ( pLoop == nullptr ) {
                return geometry_status_t::CORRUPT_STATE;
            }
            pLoop->hFace = insertedFace.handle;
            if ( !GenerationHandle_IsValid( pOutputShell->hAnyFace ) ) {
                pOutputShell->hAnyFace = insertedFace.handle;
            }
            if ( pOutputShell->cFaces ==
                 std::numeric_limits<u32>::max() ) {
                return geometry_status_t::LIMIT_EXCEEDED;
            }
            ++pOutputShell->cFaces;
        }
    }
    if ( iDirectedEdge != counts.cHalfEdgesOut ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    return TryPairHalfEdges( pMeshOut, &pWork->directedEdges );
}

geometry_status_t TrySubdivide(
    subdivision_scheme_t scheme,
    const editable_mesh_t *pMeshIn,
    editable_mesh_t *pMeshOut ) noexcept
{
    if ( pMeshIn == nullptr || pMeshOut == nullptr ||
         pMeshIn == pMeshOut ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !EditableMesh_IsInitialized( pMeshOut ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    auto fail = [&]( geometry_status_t status ) noexcept {
        EditableMesh_Shutdown( pMeshOut );
        return status;
    };
    if ( !EditableMesh_IsInitialized( pMeshIn ) ) {
        return fail( geometry_status_t::NOT_INITIALIZED );
    }
    const bool bInputStorageValid = MeshStorageIsValid( *pMeshIn );
    const bool bOutputStorageValid = MeshStorageIsValid( *pMeshOut );
    if ( !bInputStorageValid || !bOutputStorageValid ) {
        return bOutputStorageValid
            ? fail( geometry_status_t::CORRUPT_STATE )
            : geometry_status_t::CORRUPT_STATE;
    }
    if ( !MeshIsEmpty( *pMeshOut ) ) {
        return fail( geometry_status_t::INVALID_ARGUMENT );
    }
    subdivision_counts_t counts{};
    geometry_status_t status = ValidateAndCountInput(
        *pMeshIn, *pMeshOut, &counts );
    if ( status != geometry_status_t::OK ) { return fail( status ); }
    subdivision_work_t work{};
    status = TryPrepareWork(
        &work, *pMeshIn, counts, pMeshOut->pAllocator );
    if ( status != geometry_status_t::OK ) { return fail( status ); }
    status = TryReserveOutput( pMeshOut, counts );
    if ( status == geometry_status_t::OK ) {
        status = TryCreateShells( *pMeshIn, pMeshOut, &work );
    }
    if ( status == geometry_status_t::OK ) {
        status = TryCollectFacesAndCreateFacePoints(
            *pMeshIn, pMeshOut, &work, counts );
    }
    if ( status == geometry_status_t::OK ) {
        status = TryCreateEdgePoints(
            scheme, *pMeshIn, pMeshOut, &work );
    }
    if ( status == geometry_status_t::OK ) {
        status = TryCreateVertexPoints(
            scheme, *pMeshIn, pMeshOut, &work, counts );
    }
    if ( status == geometry_status_t::OK ) {
        status = TryBuildTopology(
            *pMeshIn, pMeshOut, &work, counts );
    }
    if ( status != geometry_status_t::OK ) { return fail( status ); }
    if ( EditableMesh_VertexCount( pMeshOut ) != counts.cVerticesOut ||
         EditableMesh_HalfEdgeCount( pMeshOut ) != counts.cHalfEdgesOut ||
         EditableMesh_EdgeCount( pMeshOut ) != counts.cEdgesOut ||
         EditableMesh_LoopCount( pMeshOut ) != counts.cLoopsOut ||
         EditableMesh_FaceCount( pMeshOut ) != counts.cFacesOut ||
         EditableMesh_ShellCount( pMeshOut ) != counts.cShellsOut ) {
        return fail( geometry_status_t::CORRUPT_STATE );
    }
    const mesh_validation_result_t validation =
        MeshValidation_Validate( pMeshOut );
    if ( validation.status != geometry_status_t::OK ) {
        return fail( validation.status );
    }
    return geometry_status_t::OK;
}

} // namespace

geometry_status_t MeshSubdivision_TryCatmullClark(
    const editable_mesh_t *pMeshIn,
    editable_mesh_t *pMeshOut ) noexcept
{
    return TrySubdivide(
        subdivision_scheme_t::CATMULL_CLARK, pMeshIn, pMeshOut );
}

geometry_status_t MeshSubdivision_TryLinear(
    const editable_mesh_t *pMeshIn,
    editable_mesh_t *pMeshOut ) noexcept
{
    return TrySubdivide(
        subdivision_scheme_t::LINEAR, pMeshIn, pMeshOut );
}

} // namespace cypher::editor::geometry
