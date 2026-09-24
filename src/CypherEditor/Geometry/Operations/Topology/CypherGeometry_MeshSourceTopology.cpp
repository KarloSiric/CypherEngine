//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshSourceTopology.cpp
//  Purpose: Implements identity-addressed topology edits via the shared
//           capture -> op -> resolve bracket.
//  Details: IDs are resolved to handles before the bracket starts, so an
//           unknown ID fails before anything is captured or changed.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshSourceTopology.h"

#include "CypherGeometry_MeshEditBracket.h"
#include "CypherGeometry_MeshTopologyOps.h"

#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

bool Vertex( const mesh_source_t *pSource, geometry_source_id_t id, geometry_mesh_vertex_handle_t *pOut ) noexcept
{
    return MeshSource_TryFindVertex( pSource, id, pOut );
}

bool PositionInSourceDomain( math::vec3d_t p ) noexcept
{
    return math::Vec3d_IsFinite( p ) && std::fabs( p.x ) <= kMeshSourceCoordinateMax &&
           std::fabs( p.y ) <= kMeshSourceCoordinateMax && std::fabs( p.z ) <= kMeshSourceCoordinateMax;
}

// The face owning the half-edge a -> b or b -> a (boundary edges have one).
bool FaceOnEdge( const mesh_source_t *pSource, geometry_mesh_edge_handle_t hEdge, geometry_mesh_face_handle_t *pOut ) noexcept
{
    const editable_mesh_t *pMesh = &pSource->mesh;
    const mesh_edge_record_t *pE = GenerationPool_Get( &pMesh->edges, hEdge );
    const mesh_half_edge_record_t *pH = pE ? GenerationPool_Get( &pMesh->halfEdges, pE->hHalfEdge ) : nullptr;
    const mesh_loop_record_t *pL = pH ? GenerationPool_Get( &pMesh->loops, pH->hLoop ) : nullptr;
    if ( pL == nullptr ) {
        // The recorded half-edge may be the boundary side; try its twin.
        const mesh_half_edge_record_t *pT = pH ? GenerationPool_Get( &pMesh->halfEdges, pH->hTwin ) : nullptr;
        pL = pT ? GenerationPool_Get( &pMesh->loops, pT->hLoop ) : nullptr;
    }
    if ( pL == nullptr || !GenerationPool_Contains( &pMesh->faces, pL->hFace ) ) { return false; }
    *pOut = pL->hFace;
    return true;
}

} // namespace

bool MeshSourceEdit_TryFindEdge(
    const mesh_source_t *pSource,
    geometry_source_id_t vertexA,
    geometry_source_id_t vertexB,
    geometry_mesh_edge_handle_t *pEdgeOut ) noexcept
{
    geometry_mesh_vertex_handle_t a{}, b{};
    if ( pEdgeOut == nullptr || !Vertex( pSource, vertexA, &a ) || !Vertex( pSource, vertexB, &b ) ) { return false; }
    const editable_mesh_t *pMesh = &pSource->mesh;
    bool bFound = false;
    (void)GenerationPool_ForEach( &pMesh->halfEdges,
        [&]( geometry_mesh_half_edge_handle_t, const mesh_half_edge_record_t &h ) noexcept -> bool_t {
            if ( h.hOrigin.nSlot != a.nSlot || h.hOrigin.nGeneration != a.nGeneration ) { return true; }
            const mesh_half_edge_record_t *pN = GenerationPool_Get( &pMesh->halfEdges, h.hNext );
            if ( pN != nullptr && pN->hOrigin.nSlot == b.nSlot && pN->hOrigin.nGeneration == b.nGeneration ) {
                *pEdgeOut = h.hEdge;
                bFound = true;
                return false;
            }
            return true;
        } );
    if ( bFound ) { return true; }
    // Boundary edges exist as a single half-edge, possibly b -> a.
    (void)GenerationPool_ForEach( &pMesh->halfEdges,
        [&]( geometry_mesh_half_edge_handle_t, const mesh_half_edge_record_t &h ) noexcept -> bool_t {
            if ( h.hOrigin.nSlot != b.nSlot || h.hOrigin.nGeneration != b.nGeneration ) { return true; }
            const mesh_half_edge_record_t *pN = GenerationPool_Get( &pMesh->halfEdges, h.hNext );
            if ( pN != nullptr && pN->hOrigin.nSlot == a.nSlot && pN->hOrigin.nGeneration == a.nGeneration ) {
                *pEdgeOut = h.hEdge;
                bFound = true;
                return false;
            }
            return true;
        } );
    return bFound;
}

geometry_status_t MeshSourceEdit_TrySplitEdge(
    mesh_source_t *pSource,
    geometry_source_id_t vertexA,
    geometry_source_id_t vertexB,
    f64 t,
    geometry_mesh_vertex_handle_t *pNewVertexOut,
    mesh_edit_report_t *pReportOut ) noexcept
{
    if ( !MeshSource_IsInitialized( pSource ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( !std::isfinite( t ) || t <= 0.0 || t >= 1.0 ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    geometry_mesh_edge_handle_t hEdge{};
    geometry_mesh_vertex_handle_t a{}, b{};
    if ( !MeshSourceEdit_TryFindEdge( pSource, vertexA, vertexB, &hEdge ) ||
         !Vertex( pSource, vertexA, &a ) ||
         !Vertex( pSource, vertexB, &b ) ) {
        return geometry_status_t::INVALID_HANDLE;
    }
    const mesh_vertex_record_t *pA =
        GenerationPool_Get( &pSource->mesh.vertices, a );
    const mesh_vertex_record_t *pB =
        GenerationPool_Get( &pSource->mesh.vertices, b );
    if ( pA == nullptr || pB == nullptr ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    const math::vec3d_t splitPosition = math::Vec3d_Add(
        math::Vec3d_Scale( pA->position, 1.0 - t ),
        math::Vec3d_Scale( pB->position, t ) );
    if ( !math::Vec3d_IsFinite( splitPosition ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }
    if ( math::Vec3d_EqualsExact( splitPosition, pA->position ) ||
         math::Vec3d_EqualsExact( splitPosition, pB->position ) ||
         math::Vec3d_LengthSquared(
             math::Vec3d_Subtract(
                 splitPosition, pA->position ) ) == 0.0 ||
         math::Vec3d_LengthSquared(
             math::Vec3d_Subtract(
                 splitPosition, pB->position ) ) == 0.0 ) {
        return geometry_status_t::DEGENERATE;
    }
    // MeshOps measures t from the edge's recorded start; flip it when that
    // start is b so the caller's t is always measured from a.
    const mesh_edge_record_t *pE = GenerationPool_Get( &pSource->mesh.edges, hEdge );
    const mesh_half_edge_record_t *pH = GenerationPool_Get( &pSource->mesh.halfEdges, pE->hHalfEdge );
    const bool bFromA = pH->hOrigin.nSlot == a.nSlot && pH->hOrigin.nGeneration == a.nGeneration;
    const f64 tOp = bFromA ? t : 1.0 - t;
    geometry_mesh_vertex_handle_t hNew{};
    const geometry_status_t st = MeshEdit_Bracket(
        pSource,
        [&]( vector_t<mesh_edit_face_parent_t> * ) noexcept {
            const mesh_split_edge_result_t r = MeshOps_SplitEdge( &pSource->mesh, hEdge, tOp );
            hNew = r.hNewVertex;
            return r.status;
        },
        pReportOut );
    if ( st == geometry_status_t::OK && pNewVertexOut ) { *pNewVertexOut = hNew; }
    return st;
}

geometry_status_t MeshSourceEdit_TrySplitFace(
    mesh_source_t *pSource,
    geometry_source_id_t faceId,
    geometry_source_id_t vertexA,
    geometry_source_id_t vertexB,
    mesh_edit_report_t *pReportOut ) noexcept
{
    if ( !MeshSource_IsInitialized( pSource ) ) { return geometry_status_t::NOT_INITIALIZED; }
    geometry_mesh_face_handle_t hFace{};
    geometry_mesh_vertex_handle_t a{}, b{};
    if ( !MeshSource_TryFindFace( pSource, faceId, &hFace ) || !Vertex( pSource, vertexA, &a ) ||
         !Vertex( pSource, vertexB, &b ) ) {
        return geometry_status_t::INVALID_HANDLE;
    }
    return MeshEdit_Bracket(
        pSource,
        [&]( vector_t<mesh_edit_face_parent_t> *pParents ) noexcept {
            const mesh_split_face_result_t r = MeshOps_SplitFace( &pSource->mesh, hFace, a, b );
            if ( r.status == geometry_status_t::OK && GeometryHandle_IsValid( r.hNewFace ) &&
                 !Vector_PushBack( pParents, mesh_edit_face_parent_t{ r.hNewFace, hFace, false } ) ) {
                return geometry_status_t::ALLOCATION_FAILED;
            }
            return r.status;
        },
        pReportOut );
}

geometry_status_t MeshSourceEdit_TryCollapseEdge(
    mesh_source_t *pSource,
    geometry_source_id_t keepId,
    geometry_source_id_t removeId,
    mesh_edit_report_t *pReportOut ) noexcept
{
    if ( !MeshSource_IsInitialized( pSource ) ) { return geometry_status_t::NOT_INITIALIZED; }
    geometry_mesh_edge_handle_t hEdge{};
    geometry_mesh_vertex_handle_t keep{};
    if ( !MeshSourceEdit_TryFindEdge( pSource, keepId, removeId, &hEdge ) || !Vertex( pSource, keepId, &keep ) ) {
        return geometry_status_t::INVALID_HANDLE;
    }
    // CollapseEdge keeps the edge's recorded start vertex and may move it
    // (e.g. to the midpoint). "Keep" means the kept vertex stays where it
    // is under its own ID, so the survivor is always moved back to the kept
    // position, and when the survivor is the other vertex it also takes
    // `keepId` (the removed vertex's ID retires at commit).
    const mesh_edge_record_t *pE = GenerationPool_Get( &pSource->mesh.edges, hEdge );
    const mesh_half_edge_record_t *pH = GenerationPool_Get( &pSource->mesh.halfEdges, pE->hHalfEdge );
    const bool bKeepIsStart = pH->hOrigin.nSlot == keep.nSlot && pH->hOrigin.nGeneration == keep.nGeneration;
    const math::vec3d_t keepPosition = GenerationPool_Get( &pSource->mesh.vertices, keep )->position;
    const geometry_status_t st = MeshEdit_Bracket(
        pSource,
        [&]( vector_t<mesh_edit_face_parent_t> * ) noexcept {
            const mesh_collapse_edge_result_t r = MeshOps_CollapseEdge( &pSource->mesh, hEdge );
            if ( r.status != geometry_status_t::OK ) { return r.status; }
            geometry_status_t s = MeshOps_MoveVertex( &pSource->mesh, r.hSurvivor, keepPosition );
            if ( s == geometry_status_t::OK && !bKeepIsStart ) {
                s = MeshSource_TrySetVertexId( pSource, r.hSurvivor, keepId );
            }
            return s;
        },
        pReportOut );
    // Lineage: the removed vertex merged into the kept one.
    if ( st == geometry_status_t::OK && pReportOut != nullptr && pReportOut->pProvenance != nullptr ) {
        geometry_mesh_vertex_handle_t hKeep{};
        if ( !MeshSource_TryFindVertex( pSource, keepId, &hKeep ) ||
             !Vector_PushBack( &pReportOut->pProvenance->merges, mesh_edit_vertex_merge_t{ removeId, hKeep } ) ) {
            return geometry_status_t::ALLOCATION_FAILED;
        }
    }
    return st;
}

geometry_status_t MeshSourceEdit_TryDissolveEdge(
    mesh_source_t *pSource,
    geometry_source_id_t vertexA,
    geometry_source_id_t vertexB,
    mesh_edit_report_t *pReportOut ) noexcept
{
    if ( !MeshSource_IsInitialized( pSource ) ) { return geometry_status_t::NOT_INITIALIZED; }
    geometry_mesh_edge_handle_t hEdge{};
    if ( !MeshSourceEdit_TryFindEdge( pSource, vertexA, vertexB, &hEdge ) ) { return geometry_status_t::INVALID_HANDLE; }
    return MeshEdit_Bracket(
        pSource,
        [&]( vector_t<mesh_edit_face_parent_t> * ) noexcept { return MeshOps_DissolveEdge( &pSource->mesh, hEdge ); },
        pReportOut );
}

geometry_status_t MeshSourceEdit_TryWeld(
    mesh_source_t *pSource,
    f64 tolerance,
    u32 *pMergedOut,
    mesh_edit_report_t *pReportOut ) noexcept
{
    if ( pMergedOut ) { *pMergedOut = 0u; }
    if ( !MeshSource_IsInitialized( pSource ) ) { return geometry_status_t::NOT_INITIALIZED; }
    // Record every vertex (ID, position) so merges can be attributed to the
    // survivor that absorbed them.
    struct pre_vertex_t {
        u64 id;
        math::vec3d_t position;
    };
    vector_t<pre_vertex_t> before{};
    const bool bWantLineage = pReportOut != nullptr && pReportOut->pProvenance != nullptr;
    if ( bWantLineage ) {
        if ( !Vector_Init( &before, pSource->attributes.faces.pAllocator ) ||
             !Vector_Reserve( &before, GenerationPool_Count( &pSource->mesh.vertices ) ) ) {
            Vector_Shutdown( &before );
            return geometry_status_t::ALLOCATION_FAILED;
        }
        (void)GenerationPool_ForEach( &pSource->mesh.vertices,
            [&]( geometry_mesh_vertex_handle_t h, const mesh_vertex_record_t &v ) noexcept -> bool_t {
                (void)Vector_PushBack( &before, pre_vertex_t{ MeshSource_VertexId( pSource, h ).value, v.position } );
                return true;
            } );
    }
    u32 cMerged = 0u;
    const geometry_status_t st = MeshEdit_Bracket(
        pSource,
        [&]( vector_t<mesh_edit_face_parent_t> * ) noexcept {
            const mesh_weld_result_t r = MeshOps_WeldVertices( &pSource->mesh, tolerance );
            cMerged = r.cVerticesMerged;
            return r.status;
        },
        pReportOut );
    if ( st == geometry_status_t::OK && pMergedOut ) { *pMergedOut = cMerged; }
    if ( st == geometry_status_t::OK && bWantLineage ) {
        // A vanished vertex merged into the nearest surviving vertex within
        // twice the tolerance (weld moves both ends at most `tolerance`
        // towards each other). Ties go to the lower ID.
        for ( usize i = 0u; i < before.nCount; ++i ) {
            geometry_mesh_vertex_handle_t h{};
            if ( MeshSource_TryFindVertex( pSource, geometry_source_id_t{ before.pData[i].id }, &h ) ) { continue; }
            geometry_mesh_vertex_handle_t hBest{};
            f64 best = 4.0 * tolerance * tolerance;
            u64 bestId = ~0ull;
            (void)GenerationPool_ForEach( &pSource->mesh.vertices,
                [&]( geometry_mesh_vertex_handle_t hv, const mesh_vertex_record_t &v ) noexcept -> bool_t {
                    const f64 d = math::Vec3d_DistanceSquared( v.position, before.pData[i].position );
                    const u64 id = MeshSource_VertexId( pSource, hv ).value;
                    if ( d < best || ( d == best && id < bestId ) ) {
                        best = d;
                        bestId = id;
                        hBest = hv;
                    }
                    return true;
                } );
            if ( GeometryHandle_IsValid( hBest ) &&
                 !Vector_PushBack( &pReportOut->pProvenance->merges,
                                   mesh_edit_vertex_merge_t{ geometry_source_id_t{ before.pData[i].id }, hBest } ) ) {
                Vector_Shutdown( &before );
                return geometry_status_t::ALLOCATION_FAILED;
            }
        }
    }
    Vector_Shutdown( &before );
    return st;
}

geometry_status_t MeshSourceEdit_TryFillHole(
    mesh_source_t *pSource,
    geometry_source_id_t vertexA,
    geometry_source_id_t vertexB,
    mesh_edit_report_t *pReportOut ) noexcept
{
    if ( !MeshSource_IsInitialized( pSource ) ) { return geometry_status_t::NOT_INITIALIZED; }
    geometry_mesh_edge_handle_t hEdge{};
    geometry_mesh_face_handle_t hAcross{};
    if ( !MeshSourceEdit_TryFindEdge( pSource, vertexA, vertexB, &hEdge ) || !FaceOnEdge( pSource, hEdge, &hAcross ) ) {
        return geometry_status_t::INVALID_HANDLE;
    }
    return MeshEdit_Bracket(
        pSource,
        [&]( vector_t<mesh_edit_face_parent_t> *pParents ) noexcept {
            const mesh_boundary_fill_result_t r = MeshBoundary_FillHole( &pSource->mesh, hEdge );
            if ( r.status == geometry_status_t::OK &&
                 !Vector_PushBack( pParents, mesh_edit_face_parent_t{ r.hFace, hAcross, false } ) ) {
                return geometry_status_t::ALLOCATION_FAILED;
            }
            return r.status;
        },
        pReportOut );
}

geometry_status_t MeshSourceEdit_TryBridge(
    mesh_source_t *pSource,
    geometry_source_id_t vertexA0,
    geometry_source_id_t vertexB0,
    geometry_source_id_t vertexA1,
    geometry_source_id_t vertexB1,
    u32 *pFacesCreatedOut,
    mesh_edit_report_t *pReportOut ) noexcept
{
    if ( pFacesCreatedOut ) { *pFacesCreatedOut = 0u; }
    if ( !MeshSource_IsInitialized( pSource ) ) { return geometry_status_t::NOT_INITIALIZED; }
    geometry_mesh_edge_handle_t e0{}, e1{};
    if ( !MeshSourceEdit_TryFindEdge( pSource, vertexA0, vertexB0, &e0 ) ||
         !MeshSourceEdit_TryFindEdge( pSource, vertexA1, vertexB1, &e1 ) ) {
        return geometry_status_t::INVALID_HANDLE;
    }
    u32 cCreated = 0u;
    const geometry_status_t st = MeshEdit_Bracket(
        pSource,
        [&]( vector_t<mesh_edit_face_parent_t> * ) noexcept {
            const mesh_boundary_add_result_t r = MeshBoundary_Bridge( &pSource->mesh, e0, e1, nullptr );
            cCreated = r.cFacesCreated;
            return r.status;
        },
        pReportOut );
    if ( st == geometry_status_t::OK && pFacesCreatedOut ) { *pFacesCreatedOut = cCreated; }
    return st;
}

namespace
{

// Resolves face IDs to handles (all must be live).
geometry_status_t ResolveFaces(
    const mesh_source_t *pSource,
    span_t<const geometry_source_id_t> ids,
    vector_t<geometry_mesh_face_handle_t> *pOut ) noexcept
{
    if ( !Vector_Init( pOut, pSource->attributes.faces.pAllocator ) || !Vector_Resize( pOut, ids.nCount ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize i = 0u; i < ids.nCount; ++i ) {
        if ( !MeshSource_TryFindFace( pSource, ids.pData[i], &pOut->pData[i] ) ) { return geometry_status_t::INVALID_HANDLE; }
    }
    return geometry_status_t::OK;
}

} // namespace

geometry_status_t MeshSourceEdit_TryDeleteFaces(
    mesh_source_t *pSource,
    span_t<const geometry_source_id_t> faceIds,
    mesh_edit_report_t *pReportOut ) noexcept
{
    if ( !MeshSource_IsInitialized( pSource ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( faceIds.nCount == 0u || faceIds.pData == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    vector_t<geometry_mesh_face_handle_t> handles{};
    geometry_status_t st = ResolveFaces( pSource, faceIds, &handles );
    if ( st == geometry_status_t::OK ) {
        st = MeshEdit_Bracket(
            pSource,
            [&]( vector_t<mesh_edit_face_parent_t> * ) noexcept {
                return MeshBoundary_DeleteFaces(
                           &pSource->mesh, span_t<const geometry_mesh_face_handle_t>{ handles.pData, handles.nCount } )
                    .status;
            },
            pReportOut );
    }
    Vector_Shutdown( &handles );
    return st;
}

geometry_status_t MeshSourceEdit_TryExtrudeEdges(
    mesh_source_t *pSource,
    span_t<const geometry_source_id_t> edgeVertexIds,
    math::vec3d_t offset,
    vector_t<geometry_mesh_edge_handle_t> *pOuterEdgesOut,
    mesh_edit_report_t *pReportOut ) noexcept
{
    if ( !MeshSource_IsInitialized( pSource ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( edgeVertexIds.nCount == 0u || edgeVertexIds.pData == nullptr || edgeVertexIds.nCount % 2u != 0u ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !math::Vec3d_IsFinite( offset ) ) { return geometry_status_t::NUMERIC_FAILURE; }
    vector_t<geometry_mesh_edge_handle_t> handles{};
    if ( !Vector_Init( &handles, pSource->attributes.faces.pAllocator ) ||
         !Vector_Resize( &handles, edgeVertexIds.nCount / 2u ) ) {
        Vector_Shutdown( &handles );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize i = 0u; i < handles.nCount; ++i ) {
        const geometry_source_id_t idA = edgeVertexIds.pData[2u * i];
        const geometry_source_id_t idB = edgeVertexIds.pData[2u * i + 1u];
        geometry_mesh_vertex_handle_t hA{}, hB{};
        if ( !MeshSourceEdit_TryFindEdge( pSource, idA, idB, &handles.pData[i] ) || !Vertex( pSource, idA, &hA ) ||
             !Vertex( pSource, idB, &hB ) ) {
            Vector_Shutdown( &handles );
            return geometry_status_t::INVALID_HANDLE;
        }
        const mesh_vertex_record_t *pA = GenerationPool_Get( &pSource->mesh.vertices, hA );
        const mesh_vertex_record_t *pB = GenerationPool_Get( &pSource->mesh.vertices, hB );
        if ( pA == nullptr || pB == nullptr ) {
            Vector_Shutdown( &handles );
            return geometry_status_t::CORRUPT_STATE;
        }
        if ( !PositionInSourceDomain( math::Vec3d_Add( pA->position, offset ) ) ||
             !PositionInSourceDomain( math::Vec3d_Add( pB->position, offset ) ) ) {
            Vector_Shutdown( &handles );
            return geometry_status_t::NUMERIC_FAILURE;
        }
    }
    // New quads find their parent (the face owning the pulled edge) through
    // the generic shared-vertex rule: that face is the only pre-edit face
    // containing both endpoints, so its UV layout is continued exactly.
    const geometry_status_t st = MeshEdit_Bracket(
        pSource,
        [&]( vector_t<mesh_edit_face_parent_t> * ) noexcept {
            return MeshBoundary_ExtrudeEdges( &pSource->mesh,
                                              span_t<const geometry_mesh_edge_handle_t>{ handles.pData, handles.nCount },
                                              offset, pOuterEdgesOut )
                .status;
        },
        pReportOut );
    Vector_Shutdown( &handles );
    return st;
}

geometry_status_t MeshSourceEdit_TryAddFace(
    mesh_source_t *pSource,
    span_t<const mesh_edit_corner_t> corners,
    geometry_mesh_face_handle_t *pNewFaceOut,
    mesh_edit_report_t *pReportOut ) noexcept
{
    if ( !MeshSource_IsInitialized( pSource ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( corners.nCount < 3u || corners.nCount > kMeshBoundaryLoopMax || corners.pData == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    mesh_boundary_corner_t ring[kMeshBoundaryLoopMax];
    math::vec3d_t fresh[kMeshBoundaryLoopMax];
    u32 cFresh = 0u;
    for ( usize k = 0u; k < corners.nCount; ++k ) {
        if ( GeometrySourceId_IsValid( corners.pData[k].vertexId ) ) {
            if ( !MeshSource_TryFindVertex( pSource, corners.pData[k].vertexId, &ring[k].hVertex ) ) {
                return geometry_status_t::INVALID_HANDLE;
            }
            ring[k].iNew = CY_INVALID_INDEX;
        } else {
            if ( !PositionInSourceDomain( corners.pData[k].position ) ) { return geometry_status_t::NUMERIC_FAILURE; }
            fresh[cFresh] = corners.pData[k].position;
            ring[k].iNew = cFresh++;
        }
    }
    vector_t<geometry_mesh_face_handle_t> created{};
    if ( !Vector_Init( &created, pSource->attributes.faces.pAllocator ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    const u32 size = static_cast<u32>( corners.nCount );
    const geometry_status_t st = MeshEdit_Bracket(
        pSource,
        [&]( vector_t<mesh_edit_face_parent_t> * ) noexcept {
            return MeshBoundary_AddFaces( &pSource->mesh, span_t<const mesh_boundary_corner_t>{ ring, corners.nCount },
                                          span_t<const u32>{ &size, 1u }, span_t<const math::vec3d_t>{ fresh, cFresh },
                                          &created )
                .status;
        },
        pReportOut );
    if ( st == geometry_status_t::OK && pNewFaceOut != nullptr && created.nCount == 1u ) { *pNewFaceOut = created.pData[0]; }
    Vector_Shutdown( &created );
    return st;
}

geometry_status_t MeshSourceEdit_TryDetachFaces(
    mesh_source_t *pSource,
    span_t<const geometry_source_id_t> faceIds,
    mesh_edit_report_t *pReportOut ) noexcept
{
    if ( !MeshSource_IsInitialized( pSource ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( faceIds.nCount == 0u || faceIds.pData == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    vector_t<geometry_mesh_face_handle_t> handles{};
    if ( !Vector_Init( &handles, pSource->attributes.faces.pAllocator ) || !Vector_Resize( &handles, faceIds.nCount ) ) {
        Vector_Shutdown( &handles );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize i = 0u; i < faceIds.nCount; ++i ) {
        if ( !MeshSource_TryFindFace( pSource, faceIds.pData[i], &handles.pData[i] ) ) {
            Vector_Shutdown( &handles );
            return geometry_status_t::INVALID_HANDLE;
        }
    }
    const geometry_status_t st = MeshEdit_Bracket(
        pSource,
        [&]( vector_t<mesh_edit_face_parent_t> * ) noexcept {
            return MeshBoundary_DetachFaces(
                       &pSource->mesh, span_t<const geometry_mesh_face_handle_t>{ handles.pData, handles.nCount } )
                .status;
        },
        pReportOut );
    Vector_Shutdown( &handles );
    return st;
}

geometry_status_t MeshSourceEdit_TryKnife(
    mesh_source_t *pSource,
    span_t<const mesh_edit_knife_point_t> path,
    vector_t<geometry_mesh_vertex_handle_t> *pPointVerticesOut,
    mesh_edit_report_t *pReportOut ) noexcept
{
    if ( !MeshSource_IsInitialized( pSource ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( path.pData == nullptr || path.nCount < 2u || path.nCount > kMeshKnifePathMax ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const allocator_t *pA = pSource->attributes.faces.pAllocator;
    vector_t<mesh_knife_point_t> resolved{};
    vector_t<mesh_knife_split_t> splits{};
    auto cleanup = [&]() noexcept {
        Vector_Shutdown( &resolved );
        Vector_Shutdown( &splits );
    };
    if ( !Vector_Init( &resolved, pA ) || !Vector_Init( &splits, pA ) || !Vector_Resize( &resolved, path.nCount ) ) {
        cleanup();
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize i = 0u; i < path.nCount; ++i ) {
        const mesh_edit_knife_point_t &in = path.pData[i];
        mesh_knife_point_t &out = resolved.pData[i];
        out = mesh_knife_point_t{};
        out.kind = in.kind;
        bool bFound = true;
        switch ( in.kind ) {
            case mesh_knife_point_kind_t::VERTEX: bFound = Vertex( pSource, in.vertexId, &out.hVertex ); break;
            case mesh_knife_point_kind_t::EDGE: {
                geometry_mesh_vertex_handle_t a{};
                bFound = MeshSourceEdit_TryFindEdge( pSource, in.vertexId, in.otherVertexId, &out.hEdge ) &&
                         Vertex( pSource, in.vertexId, &a );
                if ( bFound ) {
                    // MeshKnife measures t from the edge record's half-edge
                    // origin; the caller measured it from vertexId.
                    const mesh_edge_record_t *pE = GenerationPool_Get( &pSource->mesh.edges, out.hEdge );
                    const mesh_half_edge_record_t *pH = GenerationPool_Get( &pSource->mesh.halfEdges, pE->hHalfEdge );
                    const bool bFromA = pH->hOrigin.nSlot == a.nSlot && pH->hOrigin.nGeneration == a.nGeneration;
                    out.t = bFromA ? in.t : 1.0 - in.t;
                }
                break;
            }
            case mesh_knife_point_kind_t::FACE:
                // A new vertex must be storable in the source (same rule as
                // extrusion); edge points are convex combinations of stored
                // vertices, so only interior points need the check.
                if ( !PositionInSourceDomain( in.position ) ) {
                    cleanup();
                    return geometry_status_t::NUMERIC_FAILURE;
                }
                bFound = MeshSource_TryFindFace( pSource, in.faceId, &out.hFace );
                out.position = in.position;
                break;
            default:
                cleanup();
                return geometry_status_t::INVALID_ARGUMENT;
        }
        if ( !bFound ) {
            cleanup();
            return geometry_status_t::INVALID_HANDLE;
        }
    }
    const geometry_status_t st = MeshEdit_Bracket(
        pSource,
        [&]( vector_t<mesh_edit_face_parent_t> *pParents ) noexcept {
            // Reserve before cutting: once the mesh is mutated nothing may
            // fail, or the bracket would skip resolve on a changed mesh. A
            // path of n points has fewer than n segments.
            if ( !Vector_Reserve( pParents, pParents->nCount + path.nCount ) ) { return geometry_status_t::ALLOCATION_FAILED; }
            const mesh_knife_result_t r = MeshKnife_Cut(
                &pSource->mesh, span_t<const mesh_knife_point_t>{ resolved.pData, resolved.nCount }, pPointVerticesOut,
                &splits );
            if ( r.status != geometry_status_t::OK ) { return r.status; }
            for ( usize i = 0u; i < splits.nCount; ++i ) {
                (void)Vector_PushBack( pParents, mesh_edit_face_parent_t{ splits.pData[i].hNewFace, splits.pData[i].hFace, false } );
            }
            return geometry_status_t::OK;
        },
        pReportOut );
    cleanup();
    return st;
}

} // namespace cypher::editor::geometry
