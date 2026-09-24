//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshTessellation.cpp
//  Purpose: Implements EditableMesh tessellation.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshTessellation.h"
#include "CypherGeometry_Planar_Triangulate.h"

#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

constexpr u32 kMaxFaceCorners = 256u;

void ClearAll( mesh_tessellation_t *p ) noexcept
{
    Vector_Clear( &p->positions );
    Vector_Clear( &p->vertexSource );
    Vector_Clear( &p->indices );
    Vector_Clear( &p->triangleFace );
    Vector_Clear( &p->triangleCorners );
}

} // namespace

geometry_status_t MeshTessellation_Init(
    mesh_tessellation_t *pOut,
    const allocator_t *pAllocator ) noexcept
{
    if ( pOut == nullptr || !Allocator_IsValid( pAllocator ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pOut->positions.pAllocator != nullptr ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }
    if ( !Vector_Init( &pOut->positions, pAllocator ) ||
         !Vector_Init( &pOut->vertexSource, pAllocator ) ||
         !Vector_Init( &pOut->indices, pAllocator ) ||
         !Vector_Init( &pOut->triangleFace, pAllocator ) ||
         !Vector_Init( &pOut->triangleCorners, pAllocator ) ) {
        MeshTessellation_Shutdown( pOut );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    return geometry_status_t::OK;
}

void MeshTessellation_Shutdown( mesh_tessellation_t *pOut ) noexcept
{
    if ( pOut == nullptr ) { return; }
    Vector_Shutdown( &pOut->positions );
    Vector_Shutdown( &pOut->vertexSource );
    Vector_Shutdown( &pOut->indices );
    Vector_Shutdown( &pOut->triangleFace );
    Vector_Shutdown( &pOut->triangleCorners );
}

usize MeshTessellation_TriangleCount( const mesh_tessellation_t *pTess ) noexcept
{
    return pTess != nullptr ? pTess->triangleFace.nCount : 0u;
}

geometry_status_t MeshTessellation_TryBuild(
    const editable_mesh_t *pMesh,
    mesh_tessellation_t *pOut,
    geometry_mesh_face_handle_t *phFailedFaceOut ) noexcept
{
    if ( pOut == nullptr || pOut->positions.pAllocator == nullptr || pMesh == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !EditableMesh_IsInitialized( pMesh ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    ClearAll( pOut );
    const allocator_t *pAlloc = pOut->positions.pAllocator;

    // Dense vertex numbering by slot.
    const usize cSlots = GenerationPool_Capacity( &pMesh->vertices );
    vector_t<u32> slotToIndex{};
    vector_t<planar_ring_triangle_t> ringTris{};
    auto cleanup = [&]() noexcept {
        Vector_Shutdown( &slotToIndex );
        Vector_Shutdown( &ringTris );
    };
    if ( !Vector_Init( &slotToIndex, pAlloc, cSlots ) || !Vector_Resize( &slotToIndex, cSlots ) ||
         !Vector_Init( &ringTris, pAlloc, kMaxFaceCorners ) ||
         !Vector_Reserve( &pOut->positions, EditableMesh_VertexCount( pMesh ) ) ||
         !Vector_Reserve( &pOut->vertexSource, EditableMesh_VertexCount( pMesh ) ) ) {
        cleanup();
        ClearAll( pOut );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    (void)GenerationPool_ForEach( &pMesh->vertices,
        [&]( geometry_mesh_vertex_handle_t h, const mesh_vertex_record_t &v ) noexcept -> bool_t {
            slotToIndex.pData[h.nSlot] = static_cast<u32>( pOut->positions.nCount );
            (void)Vector_PushBack( &pOut->positions, v.position );
            (void)Vector_PushBack( &pOut->vertexSource, h );
            return true;
        } );

    geometry_status_t s = geometry_status_t::OK;
    math::vec3d_t corners[kMaxFaceCorners];
    u32 cornerIdx[kMaxFaceCorners];
    geometry_mesh_half_edge_handle_t cornerHe[kMaxFaceCorners];
    math::vec2d_t ring[kMaxFaceCorners];
    (void)GenerationPool_ForEach( &pMesh->faces,
        [&]( geometry_mesh_face_handle_t hF, const mesh_face_record_t &f ) noexcept -> bool_t {
            const mesh_loop_record_t *pL = GenerationPool_Get( &pMesh->loops, f.hOuterLoop );
            if ( pL == nullptr ) { s = geometry_status_t::CORRUPT_STATE; return false; }
            if ( pL->cHalfEdges > kMaxFaceCorners ) {
                s = geometry_status_t::LIMIT_EXCEEDED;
                if ( phFailedFaceOut ) { *phFailedFaceOut = hF; }
                return false;
            }
            const u32 n = pL->cHalfEdges;
            geometry_mesh_half_edge_handle_t hCur = pL->hFirstHalfEdge;
            math::vec3d_t newell = math::Vec3d_Make( 0.0, 0.0, 0.0 );
            for ( u32 k = 0u; k < n; ++k ) {
                const mesh_half_edge_record_t *pH = GenerationPool_Get( &pMesh->halfEdges, hCur );
                const mesh_vertex_record_t *pV = pH ? GenerationPool_Get( &pMesh->vertices, pH->hOrigin ) : nullptr;
                if ( pV == nullptr ) { s = geometry_status_t::CORRUPT_STATE; return false; }
                corners[k] = pV->position;
                cornerIdx[k] = slotToIndex.pData[pH->hOrigin.nSlot];
                cornerHe[k] = hCur;
                hCur = pH->hNext;
            }
            for ( u32 k = 0u; k < n; ++k ) {
                const math::vec3d_t a = corners[k], b = corners[( k + 1u ) % n];
                newell.x += ( a.y - b.y ) * ( a.z + b.z );
                newell.y += ( a.z - b.z ) * ( a.x + b.x );
                newell.z += ( a.x - b.x ) * ( a.y + b.y );
            }
            // Drop the dominant normal axis (an exact projection). The
            // projection may mirror the face, but Planar_TryTriangulateRing
            // emits triangles with the *ring's* winding, and one projection
            // mirrors every triangle of the face the same way, so the 3D
            // triangles keep the face's orientation either way.
            const f64 ax = std::fabs( newell.x ), ay = std::fabs( newell.y ), az = std::fabs( newell.z );
            for ( u32 k = 0u; k < n; ++k ) {
                const math::vec3d_t p = corners[k];
                if ( az >= ax && az >= ay ) { ring[k] = math::Vec2d_Make( p.x, p.y ); }
                else if ( ay >= ax ) { ring[k] = math::Vec2d_Make( p.z, p.x ); }
                else { ring[k] = math::Vec2d_Make( p.y, p.z ); }
            }
            Vector_Clear( &ringTris );
            if ( n < 3u || Planar_TryTriangulateRing( span_t<const math::vec2d_t>{ ring, n }, pAlloc,
                                                      &ringTris ) != geometry_status_t::OK ) {
                s = geometry_status_t::DEGENERATE;
                if ( phFailedFaceOut ) { *phFailedFaceOut = hF; }
                return false;
            }
            for ( usize t = 0u; t < ringTris.nCount; ++t ) {
                const planar_ring_triangle_t &tr = ringTris.pData[t];
                if ( !Vector_PushBack( &pOut->indices, cornerIdx[tr.a] ) ||
                     !Vector_PushBack( &pOut->indices, cornerIdx[tr.b] ) ||
                     !Vector_PushBack( &pOut->indices, cornerIdx[tr.c] ) ||
                     !Vector_PushBack( &pOut->triangleFace, hF ) ||
                     !Vector_PushBack( &pOut->triangleCorners, cornerHe[tr.a] ) ||
                     !Vector_PushBack( &pOut->triangleCorners, cornerHe[tr.b] ) ||
                     !Vector_PushBack( &pOut->triangleCorners, cornerHe[tr.c] ) ) {
                    s = geometry_status_t::ALLOCATION_FAILED;
                    return false;
                }
            }
            return true;
        } );

    cleanup();
    if ( s != geometry_status_t::OK ) { ClearAll( pOut ); }
    return s;
}

} // namespace cypher::editor::geometry
