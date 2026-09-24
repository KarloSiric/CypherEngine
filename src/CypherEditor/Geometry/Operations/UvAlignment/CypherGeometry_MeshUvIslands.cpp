//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshUvIslands.cpp
//  Purpose: Implements atomic UV seam edits and deterministic UV-island
//           discovery.
//  Details: Discovery uses a source-ID-sorted face table and a union-find
//           whose root is always the component's lowest table index. This
//           makes both island order and membership independent of pool-slot,
//           face insertion, and edge traversal order.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshUvIslands.h"

#include "CypherGeometry_MeshSourceTopology.h"

#include <algorithm>
#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

constexpr u32 kNoIndex = CY_U32_MAX;

struct face_entry_t {
    geometry_source_id_t sourceId{};
    geometry_mesh_face_handle_t handle{};
};

bool SameHandle( geometry_mesh_vertex_handle_t a, geometry_mesh_vertex_handle_t b ) noexcept
{
    return a.nSlot == b.nSlot && a.nGeneration == b.nGeneration;
}

bool SameEdgeKey( const mesh_source_edge_key_t &a, const mesh_source_edge_key_t &b ) noexcept
{
    return ( a.vertexA.value == b.vertexA.value && a.vertexB.value == b.vertexB.value ) ||
           ( a.vertexA.value == b.vertexB.value && a.vertexB.value == b.vertexA.value );
}

bool ValidSeamEdit( mesh_uv_seam_edit_t edit ) noexcept
{
    return edit == mesh_uv_seam_edit_t::SET || edit == mesh_uv_seam_edit_t::CLEAR ||
           edit == mesh_uv_seam_edit_t::TOGGLE;
}

bool WantsChangedFlag( u8 flags, mesh_uv_seam_edit_t edit ) noexcept
{
    const bool seam = ( flags & MESH_EDGE_FLAG_SEAM ) != 0u;
    return edit == mesh_uv_seam_edit_t::TOGGLE ||
           ( edit == mesh_uv_seam_edit_t::SET && !seam ) ||
           ( edit == mesh_uv_seam_edit_t::CLEAR && seam );
}

u8 EditedFlags( u8 flags, mesh_uv_seam_edit_t edit ) noexcept
{
    if ( edit == mesh_uv_seam_edit_t::SET ) {
        return static_cast<u8>( flags | MESH_EDGE_FLAG_SEAM );
    }
    if ( edit == mesh_uv_seam_edit_t::CLEAR ) {
        return static_cast<u8>( flags & ~MESH_EDGE_FLAG_SEAM );
    }
    return static_cast<u8>( flags ^ MESH_EDGE_FLAG_SEAM );
}

math::vec2d_t CornerUv( const mesh_attribute_store_t &attributes,
                        geometry_mesh_half_edge_handle_t hCorner,
                        mesh_uv_set_t uvSet ) noexcept
{
    const mesh_corner_attributes_t corner =
        MeshAttributeStore_GetCorner( &attributes, hCorner );
    return uvSet == mesh_uv_set_t::MATERIAL ? corner.uv0 : corner.uv1;
}

bool UvMatches( math::vec2d_t a, math::vec2d_t b, f64 tolerance ) noexcept
{
    return math::Vec2d_IsFinite( a ) && math::Vec2d_IsFinite( b ) &&
           std::fabs( a.x - b.x ) <= tolerance &&
           std::fabs( a.y - b.y ) <= tolerance;
}

u32 FindRoot( vector_t<u32> *pParents, u32 i ) noexcept
{
    u32 root = i;
    while ( pParents->pData[root] != root ) { root = pParents->pData[root]; }
    while ( pParents->pData[i] != i ) {
        const u32 next = pParents->pData[i];
        pParents->pData[i] = root;
        i = next;
    }
    return root;
}

void UnionByLowestIndex( vector_t<u32> *pParents, u32 a, u32 b ) noexcept
{
    a = FindRoot( pParents, a );
    b = FindRoot( pParents, b );
    if ( a == b ) { return; }
    if ( a < b ) { pParents->pData[b] = a; }
    else { pParents->pData[a] = b; }
}

bool TryFaceIndex( const vector_t<u32> &slotToIndex,
                   const vector_t<face_entry_t> &faces,
                   geometry_mesh_face_handle_t hFace,
                   u32 *pIndexOut ) noexcept
{
    if ( !GeometryHandle_IsValid( hFace ) || hFace.nSlot >= slotToIndex.nCount ) {
        return false;
    }
    const u32 i = slotToIndex.pData[hFace.nSlot];
    if ( i == kNoIndex || i >= faces.nCount ||
         faces.pData[i].handle.nGeneration != hFace.nGeneration ) {
        return false;
    }
    *pIndexOut = i;
    return true;
}

void PublishResult( mesh_uv_island_result_t *pDestination,
                    mesh_uv_island_result_t *pStaged ) noexcept
{
    MeshUvIslandResult_Shutdown( pDestination );
    Vector_Move( &pDestination->faceIds, &pStaged->faceIds );
    Vector_Move( &pDestination->islands, &pStaged->islands );
}

} // namespace

geometry_status_t MeshUv_TryEditSeams(
    mesh_source_t *pSource,
    span_t<const mesh_source_edge_key_t> edges,
    mesh_uv_seam_edit_t edit,
    u32 *pcChangedOut ) noexcept
{
    if ( pcChangedOut != nullptr ) { *pcChangedOut = 0u; }
    if ( !MeshSource_IsInitialized( pSource ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !ValidSeamEdit( edit ) || ( edges.nCount != 0u && edges.pData == nullptr ) ||
         edges.nCount > CY_U32_MAX ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    u32 cChanged = 0u;
    for ( usize i = 0u; i < edges.nCount; ++i ) {
        const mesh_source_edge_key_t &key = edges.pData[i];
        if ( !GeometrySourceId_IsValid( key.vertexA ) ||
             !GeometrySourceId_IsValid( key.vertexB ) ||
             key.vertexA.value == key.vertexB.value ) {
            return geometry_status_t::INVALID_HANDLE;
        }
        for ( usize j = 0u; j < i; ++j ) {
            if ( SameEdgeKey( key, edges.pData[j] ) ) {
                return geometry_status_t::INVALID_ARGUMENT;
            }
        }
        geometry_mesh_edge_handle_t hEdge{};
        if ( !MeshSourceEdit_TryFindEdge(
                 pSource, key.vertexA, key.vertexB, &hEdge ) ) {
            return geometry_status_t::INVALID_HANDLE;
        }
        const mesh_edge_attributes_t attributes =
            MeshAttributeStore_GetEdge( &pSource->attributes, hEdge );
        cChanged += WantsChangedFlag( attributes.flags, edit ) ? 1u : 0u;
    }

    if ( cChanged == 0u ) { return geometry_status_t::OK; }

    // Grow once, after the complete set has been validated. Vector growth is
    // transactional, and Resize cannot allocate after Reserve succeeds.
    const usize cSlots = pSource->mesh.edges.cSlots;
    if ( pSource->attributes.edges.nCount < cSlots &&
         ( !Vector_Reserve( &pSource->attributes.edges, cSlots ) ||
           !Vector_Resize( &pSource->attributes.edges, cSlots ) ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }

    for ( usize i = 0u; i < edges.nCount; ++i ) {
        geometry_mesh_edge_handle_t hEdge{};
        const bool found = MeshSourceEdit_TryFindEdge(
            pSource, edges.pData[i].vertexA, edges.pData[i].vertexB, &hEdge );
        if ( !found || hEdge.nSlot >= pSource->attributes.edges.nCount ) {
            // The source cannot change between the validation and mutation
            // passes in this single-threaded edit contract.
            return geometry_status_t::CORRUPT_STATE;
        }
        mesh_edge_attributes_t attributes =
            MeshAttributeStore_GetEdge( &pSource->attributes, hEdge );
        if ( !WantsChangedFlag( attributes.flags, edit ) ) { continue; }
        attributes.flags = EditedFlags( attributes.flags, edit );
        mesh_attribute_slot_t<mesh_edge_attributes_t> &slot =
            pSource->attributes.edges.pData[hEdge.nSlot];
        slot.nGeneration = hEdge.nGeneration;
        slot.value = attributes;
    }

    if ( pcChangedOut != nullptr ) { *pcChangedOut = cChanged; }
    return geometry_status_t::OK;
}

geometry_status_t MeshUvIslandResult_Init(
    mesh_uv_island_result_t *pResult,
    const allocator_t *pAllocator ) noexcept
{
    if ( pResult == nullptr || !Allocator_IsValid( pAllocator ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( MeshUvIslandResult_IsInitialized( pResult ) ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }
    if ( !Vector_Init( &pResult->faceIds, pAllocator ) ||
         !Vector_Init( &pResult->islands, pAllocator ) ) {
        MeshUvIslandResult_Shutdown( pResult );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    return geometry_status_t::OK;
}

void MeshUvIslandResult_Shutdown(
    mesh_uv_island_result_t *pResult ) noexcept
{
    if ( pResult == nullptr ) { return; }
    Vector_Shutdown( &pResult->faceIds );
    Vector_Shutdown( &pResult->islands );
}

bool MeshUvIslandResult_IsInitialized(
    const mesh_uv_island_result_t *pResult ) noexcept
{
    return pResult != nullptr && pResult->faceIds.pAllocator != nullptr &&
           pResult->islands.pAllocator == pResult->faceIds.pAllocator &&
           Vector_IsValid( &pResult->faceIds ) &&
           Vector_IsValid( &pResult->islands );
}

geometry_status_t MeshUv_TryDiscoverIslands(
    const mesh_source_t *pSource,
    mesh_uv_set_t uvSet,
    f64 tolerance,
    mesh_uv_island_result_t *pResult ) noexcept
{
    if ( !MeshSource_IsInitialized( pSource ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !MeshUvIslandResult_IsInitialized( pResult ) ||
         ( uvSet != mesh_uv_set_t::MATERIAL &&
           uvSet != mesh_uv_set_t::LIGHTMAP ) ||
         !std::isfinite( tolerance ) || tolerance < 0.0 ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    const allocator_t *pAllocator = pResult->faceIds.pAllocator;
    vector_t<face_entry_t> faces{};
    vector_t<u32> slotToIndex{};
    vector_t<u32> parents{};
    vector_t<u32> rootToIsland{};
    vector_t<u32> cursors{};
    mesh_uv_island_result_t staged{};

    auto cleanup = [&]() noexcept {
        MeshUvIslandResult_Shutdown( &staged );
        Vector_Shutdown( &cursors );
        Vector_Shutdown( &rootToIsland );
        Vector_Shutdown( &parents );
        Vector_Shutdown( &slotToIndex );
        Vector_Shutdown( &faces );
    };
    if ( !Vector_Init( &faces, pAllocator ) ||
         !Vector_Init( &slotToIndex, pAllocator ) ||
         !Vector_Init( &parents, pAllocator ) ||
         !Vector_Init( &rootToIsland, pAllocator ) ||
         !Vector_Init( &cursors, pAllocator ) ||
         MeshUvIslandResult_Init( &staged, pAllocator ) !=
             geometry_status_t::OK ) {
        cleanup();
        return geometry_status_t::ALLOCATION_FAILED;
    }

    const usize cFaces = GenerationPool_Count( &pSource->mesh.faces );
    if ( cFaces > CY_U32_MAX ||
         !Vector_Reserve( &faces, cFaces ) ||
         !Vector_Resize( &slotToIndex, pSource->mesh.faces.cSlots ) ||
         !Vector_Resize( &parents, cFaces ) ||
         !Vector_Resize( &rootToIsland, cFaces ) ) {
        cleanup();
        return cFaces > CY_U32_MAX
            ? geometry_status_t::LIMIT_EXCEEDED
            : geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize i = 0u; i < slotToIndex.nCount; ++i ) {
        slotToIndex.pData[i] = kNoIndex;
    }

    geometry_status_t collectStatus = geometry_status_t::OK;
    (void)GenerationPool_ForEach(
        &pSource->mesh.faces,
        [&]( geometry_mesh_face_handle_t hFace,
             const mesh_face_record_t & ) noexcept -> bool_t {
            const geometry_source_id_t id =
                MeshSource_FaceId( pSource, hFace );
            if ( !GeometrySourceId_IsValid( id ) ) {
                collectStatus = geometry_status_t::CORRUPT_STATE;
                return false;
            }
            if ( !Vector_PushBack( &faces,
                                   face_entry_t{ id, hFace } ) ) {
                collectStatus = geometry_status_t::ALLOCATION_FAILED;
                return false;
            }
            return true;
        } );
    if ( collectStatus != geometry_status_t::OK ) {
        cleanup();
        return collectStatus;
    }

    if ( faces.nCount > 1u ) {
        std::sort( faces.pData, faces.pData + faces.nCount,
            []( const face_entry_t &a,
                const face_entry_t &b ) noexcept {
                return a.sourceId.value < b.sourceId.value;
            } );
    }
    for ( usize i = 0u; i < faces.nCount; ++i ) {
        if ( i != 0u &&
             faces.pData[i - 1u].sourceId.value ==
                 faces.pData[i].sourceId.value ) {
            cleanup();
            return geometry_status_t::CORRUPT_STATE;
        }
        slotToIndex.pData[faces.pData[i].handle.nSlot] =
            static_cast<u32>( i );
        parents.pData[i] = static_cast<u32>( i );
        rootToIsland.pData[i] = kNoIndex;
    }

    bool numericFailure = false;
    (void)GenerationPool_ForEach(
        &pSource->mesh.edges,
        [&]( geometry_mesh_edge_handle_t hEdge,
             const mesh_edge_record_t &edge ) noexcept -> bool_t {
            if ( ( MeshAttributeStore_GetEdge(
                       &pSource->attributes, hEdge ).flags &
                   MESH_EDGE_FLAG_SEAM ) != 0u ) {
                return true;
            }

            const mesh_half_edge_record_t *pHalf =
                GenerationPool_Get( &pSource->mesh.halfEdges,
                                    edge.hHalfEdge );
            const mesh_half_edge_record_t *pTwin = pHalf
                ? GenerationPool_Get( &pSource->mesh.halfEdges,
                                      pHalf->hTwin )
                : nullptr;
            // Missing or inconsistent twins are boundary/non-manifold for
            // island purposes and therefore do not connect faces.
            if ( pHalf == nullptr || pTwin == nullptr ||
                 pTwin->hTwin.nSlot != edge.hHalfEdge.nSlot ||
                 pTwin->hTwin.nGeneration != edge.hHalfEdge.nGeneration ||
                 pHalf->hEdge.nSlot != hEdge.nSlot ||
                 pHalf->hEdge.nGeneration != hEdge.nGeneration ||
                 pTwin->hEdge.nSlot != hEdge.nSlot ||
                 pTwin->hEdge.nGeneration != hEdge.nGeneration ) {
                return true;
            }

            const mesh_loop_record_t *pLoopA =
                GenerationPool_Get( &pSource->mesh.loops, pHalf->hLoop );
            const mesh_loop_record_t *pLoopB =
                GenerationPool_Get( &pSource->mesh.loops, pTwin->hLoop );
            u32 iFaceA = 0u, iFaceB = 0u;
            if ( pLoopA == nullptr || pLoopB == nullptr ||
                 !TryFaceIndex( slotToIndex, faces, pLoopA->hFace,
                                &iFaceA ) ||
                 !TryFaceIndex( slotToIndex, faces, pLoopB->hFace,
                                &iFaceB ) ||
                 iFaceA == iFaceB ) {
                return true;
            }

            const mesh_half_edge_record_t *pNextA =
                GenerationPool_Get( &pSource->mesh.halfEdges,
                                    pHalf->hNext );
            const mesh_half_edge_record_t *pNextB =
                GenerationPool_Get( &pSource->mesh.halfEdges,
                                    pTwin->hNext );
            if ( pNextA == nullptr || pNextB == nullptr ||
                 !SameHandle( pHalf->hOrigin, pNextB->hOrigin ) ||
                 !SameHandle( pNextA->hOrigin, pTwin->hOrigin ) ) {
                return true;
            }

            const math::vec2d_t uvA0 = CornerUv(
                pSource->attributes, edge.hHalfEdge, uvSet );
            const math::vec2d_t uvA1 = CornerUv(
                pSource->attributes, pHalf->hNext, uvSet );
            const math::vec2d_t uvB1 = CornerUv(
                pSource->attributes, pHalf->hTwin, uvSet );
            const math::vec2d_t uvB0 = CornerUv(
                pSource->attributes, pTwin->hNext, uvSet );
            if ( !math::Vec2d_IsFinite( uvA0 ) ||
                 !math::Vec2d_IsFinite( uvA1 ) ||
                 !math::Vec2d_IsFinite( uvB0 ) ||
                 !math::Vec2d_IsFinite( uvB1 ) ) {
                numericFailure = true;
                return false;
            }
            if ( UvMatches( uvA0, uvB0, tolerance ) &&
                 UvMatches( uvA1, uvB1, tolerance ) ) {
                UnionByLowestIndex( &parents, iFaceA, iFaceB );
            }
            return true;
        } );
    if ( numericFailure ) {
        cleanup();
        return geometry_status_t::NUMERIC_FAILURE;
    }

    u32 cIslands = 0u;
    for ( u32 i = 0u; i < static_cast<u32>( cFaces ); ++i ) {
        const u32 root = FindRoot( &parents, i );
        if ( rootToIsland.pData[root] == kNoIndex ) {
            rootToIsland.pData[root] = cIslands++;
        }
    }
    if ( !Vector_Resize( &staged.faceIds, cFaces ) ||
         !Vector_Resize( &staged.islands, cIslands ) ||
         !Vector_Resize( &cursors, cIslands ) ) {
        cleanup();
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( u32 i = 0u; i < cIslands; ++i ) {
        staged.islands.pData[i] = mesh_uv_island_range_t{};
    }
    for ( u32 i = 0u; i < static_cast<u32>( cFaces ); ++i ) {
        const u32 island = rootToIsland.pData[FindRoot( &parents, i )];
        ++staged.islands.pData[island].cFaces;
    }
    u32 iFirst = 0u;
    for ( u32 i = 0u; i < cIslands; ++i ) {
        staged.islands.pData[i].iFirstFace = iFirst;
        cursors.pData[i] = iFirst;
        iFirst += staged.islands.pData[i].cFaces;
    }
    for ( u32 i = 0u; i < static_cast<u32>( cFaces ); ++i ) {
        const u32 island = rootToIsland.pData[FindRoot( &parents, i )];
        staged.faceIds.pData[cursors.pData[island]++] =
            faces.pData[i].sourceId;
    }

    PublishResult( pResult, &staged );
    cleanup();
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
