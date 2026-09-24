//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Attributes_MeshStore.cpp
//  Purpose: Implements generation-stamped sidecar mesh attribute storage.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Attributes_MeshStore.h"

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

template <typename record_t, typename tag_t>
record_t Get( const vector_t<mesh_attribute_slot_t<record_t>> &v, geometry_handle_t<tag_t> h ) noexcept
{
    if ( !GenerationHandle_IsValid( h ) || h.nSlot >= v.nCount ) { return record_t{}; }
    const mesh_attribute_slot_t<record_t> &s = v.pData[h.nSlot];
    return s.nGeneration == h.nGeneration ? s.value : record_t{};
}

template <typename record_t, typename tag_t>
bool Has( const vector_t<mesh_attribute_slot_t<record_t>> &v, geometry_handle_t<tag_t> h ) noexcept
{
    return GenerationHandle_IsValid( h ) && h.nSlot < v.nCount &&
           v.pData[h.nSlot].nGeneration == h.nGeneration;
}

template <typename record_t, typename tag_t>
geometry_status_t Set( vector_t<mesh_attribute_slot_t<record_t>> *pV, geometry_handle_t<tag_t> h,
                       const record_t &value, usize cLimit ) noexcept
{
    if ( !GenerationHandle_IsValid( h ) ) { return geometry_status_t::INVALID_HANDLE; }
    if ( static_cast<usize>( h.nSlot ) >= cLimit ) { return geometry_status_t::LIMIT_EXCEEDED; }
    if ( h.nSlot >= pV->nCount ) {
        // Grow geometrically so a sequential fill is amortized O(1);
        // Resize value-initializes new slots to generation 0 ("unwritten").
        usize want = pV->nCapacity ? pV->nCapacity : 16u;
        while ( want <= h.nSlot ) { want *= 2u; }
        if ( want > cLimit ) { want = cLimit; }
        if ( !Vector_Reserve( pV, want ) || !Vector_Resize( pV, static_cast<usize>( h.nSlot ) + 1u ) ) {
            return geometry_status_t::ALLOCATION_FAILED;
        }
    }
    pV->pData[h.nSlot].nGeneration = h.nGeneration;
    pV->pData[h.nSlot].value = value;
    return geometry_status_t::OK;
}

} // namespace

bool MeshAttributeStore_IsInitialized( const mesh_attribute_store_t *pStore ) noexcept
{
    return pStore != nullptr && pStore->corners.pAllocator != nullptr;
}

geometry_status_t MeshAttributeStore_Init(
    mesh_attribute_store_t *pStore,
    const allocator_t *pAllocator ) noexcept
{
    if ( pStore == nullptr || !Allocator_IsValid( pAllocator ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( MeshAttributeStore_IsInitialized( pStore ) ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }
    if ( !Vector_Init( &pStore->corners, pAllocator ) || !Vector_Init( &pStore->faces, pAllocator ) ||
         !Vector_Init( &pStore->edges, pAllocator ) ) {
        MeshAttributeStore_Shutdown( pStore );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    return geometry_status_t::OK;
}

void MeshAttributeStore_Shutdown( mesh_attribute_store_t *pStore ) noexcept
{
    if ( pStore == nullptr ) { return; }
    Vector_Shutdown( &pStore->corners );
    Vector_Shutdown( &pStore->faces );
    Vector_Shutdown( &pStore->edges );
}

void MeshAttributeStore_Clear( mesh_attribute_store_t *pStore ) noexcept
{
    if ( !MeshAttributeStore_IsInitialized( pStore ) ) { return; }
    Vector_Clear( &pStore->corners );
    Vector_Clear( &pStore->faces );
    Vector_Clear( &pStore->edges );
}

mesh_corner_attributes_t MeshAttributeStore_GetCorner(
    const mesh_attribute_store_t *pStore, geometry_mesh_half_edge_handle_t h ) noexcept
{
    return pStore ? Get( pStore->corners, h ) : mesh_corner_attributes_t{};
}

mesh_face_attributes_t MeshAttributeStore_GetFace(
    const mesh_attribute_store_t *pStore, geometry_mesh_face_handle_t h ) noexcept
{
    return pStore ? Get( pStore->faces, h ) : mesh_face_attributes_t{};
}

mesh_edge_attributes_t MeshAttributeStore_GetEdge(
    const mesh_attribute_store_t *pStore, geometry_mesh_edge_handle_t h ) noexcept
{
    return pStore ? Get( pStore->edges, h ) : mesh_edge_attributes_t{};
}

bool MeshAttributeStore_HasCorner(
    const mesh_attribute_store_t *pStore, geometry_mesh_half_edge_handle_t h ) noexcept
{
    return pStore != nullptr && Has( pStore->corners, h );
}

bool MeshAttributeStore_HasFace(
    const mesh_attribute_store_t *pStore, geometry_mesh_face_handle_t h ) noexcept
{
    return pStore != nullptr && Has( pStore->faces, h );
}

geometry_status_t MeshAttributeStore_TrySetCorner(
    mesh_attribute_store_t *pStore, geometry_mesh_half_edge_handle_t h,
    const mesh_corner_attributes_t &value, usize cSlotLimit ) noexcept
{
    if ( !MeshAttributeStore_IsInitialized( pStore ) ) { return geometry_status_t::NOT_INITIALIZED; }
    return Set( &pStore->corners, h, value, cSlotLimit );
}

geometry_status_t MeshAttributeStore_TrySetFace(
    mesh_attribute_store_t *pStore, geometry_mesh_face_handle_t h,
    const mesh_face_attributes_t &value, usize cSlotLimit ) noexcept
{
    if ( !MeshAttributeStore_IsInitialized( pStore ) ) { return geometry_status_t::NOT_INITIALIZED; }
    return Set( &pStore->faces, h, value, cSlotLimit );
}

geometry_status_t MeshAttributeStore_TrySetEdge(
    mesh_attribute_store_t *pStore, geometry_mesh_edge_handle_t h,
    const mesh_edge_attributes_t &value, usize cSlotLimit ) noexcept
{
    if ( !MeshAttributeStore_IsInitialized( pStore ) ) { return geometry_status_t::NOT_INITIALIZED; }
    return Set( &pStore->edges, h, value, cSlotLimit );
}

geometry_status_t MeshAttributeStore_Validate(
    const mesh_attribute_store_t *pStore,
    const editable_mesh_t *pMesh,
    geometry_mesh_half_edge_handle_t *phBadCornerOut ) noexcept
{
    if ( !MeshAttributeStore_IsInitialized( pStore ) || !EditableMesh_IsInitialized( pMesh ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    geometry_status_t s = geometry_status_t::OK;
    (void)GenerationPool_ForEach( &pMesh->halfEdges,
        [&]( geometry_mesh_half_edge_handle_t h, const mesh_half_edge_record_t & ) noexcept -> bool_t {
            if ( !Has( pStore->corners, h ) ) { return true; }
            const mesh_corner_attributes_t &c = pStore->corners.pData[h.nSlot].value;
            if ( !math::Vec2d_IsFinite( c.uv0 ) || !math::Vec2d_IsFinite( c.uv1 ) ) {
                s = geometry_status_t::NUMERIC_FAILURE;
                if ( phBadCornerOut ) { *phBadCornerOut = h; }
                return false;
            }
            return true;
        } );
    return s;
}

mesh_attribute_counts_t MeshAttributeStore_CountLive(
    const mesh_attribute_store_t *pStore,
    const editable_mesh_t *pMesh ) noexcept
{
    mesh_attribute_counts_t c{};
    if ( !MeshAttributeStore_IsInitialized( pStore ) || !EditableMesh_IsInitialized( pMesh ) ) {
        return c;
    }
    (void)GenerationPool_ForEach( &pMesh->halfEdges,
        [&]( geometry_mesh_half_edge_handle_t h, const mesh_half_edge_record_t & ) noexcept -> bool_t {
            c.cCorners += Has( pStore->corners, h ) ? 1u : 0u;
            return true;
        } );
    (void)GenerationPool_ForEach( &pMesh->faces,
        [&]( geometry_mesh_face_handle_t h, const mesh_face_record_t & ) noexcept -> bool_t {
            c.cFaces += Has( pStore->faces, h ) ? 1u : 0u;
            return true;
        } );
    (void)GenerationPool_ForEach( &pMesh->edges,
        [&]( geometry_mesh_edge_handle_t h, const mesh_edge_record_t & ) noexcept -> bool_t {
            c.cEdges += Has( pStore->edges, h ) ? 1u : 0u;
            return true;
        } );
    return c;
}

} // namespace cypher::editor::geometry
