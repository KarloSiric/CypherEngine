//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshSourceComponents.cpp
//  Purpose: Implements component transform and offset on mesh sources.
//  Details: Both compute every target position first (checking the source
//           coordinate domain), then make one MeshVertices_TryMove call,
//           which validates all affected faces before writing anything.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshSourceComponents.h"

#include "CypherGeometry_MeshSelectionQueries.h"
#include "CypherGeometry_MeshVertexMove.h"

#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

// A new vertex position the source can store.
bool InSourceDomain( math::vec3d_t p ) noexcept
{
    return math::Vec3d_IsFinite( p ) && std::fabs( p.x ) <= kMeshSourceCoordinateMax &&
           std::fabs( p.y ) <= kMeshSourceCoordinateMax && std::fabs( p.z ) <= kMeshSourceCoordinateMax;
}

// Moves `verts` to `targets` (parallel), after the domain check.
geometry_status_t MoveAll(
    mesh_source_t *pSource,
    const vector_t<geometry_mesh_vertex_handle_t> &verts,
    const vector_t<math::vec3d_t> &targets ) noexcept
{
    for ( usize i = 0u; i < targets.nCount; ++i ) {
        if ( !InSourceDomain( targets.pData[i] ) ) { return geometry_status_t::NUMERIC_FAILURE; }
    }
    return MeshVertices_TryMove( &pSource->mesh, span_t<const geometry_mesh_vertex_handle_t>{ verts.pData, verts.nCount },
                                 span_t<const math::vec3d_t>{ targets.pData, targets.nCount }, nullptr );
}

} // namespace

geometry_status_t MeshSourceEdit_TryTransformComponents(
    mesh_source_t *pSource,
    const mesh_selection_t *pSelection,
    mesh_selection_mode_t mode,
    const math::affine3d_t &transform ) noexcept
{
    if ( !MeshSource_IsInitialized( pSource ) || pSelection == nullptr ) { return geometry_status_t::NOT_INITIALIZED; }
    for ( f64 v : transform.m ) {
        if ( !std::isfinite( v ) ) { return geometry_status_t::NUMERIC_FAILURE; }
    }
    const math::vec3d_t c0 = math::Affine3d_Column( transform, 0u ), c1 = math::Affine3d_Column( transform, 1u ),
                        c2 = math::Affine3d_Column( transform, 2u );
    if ( !( math::Vec3d_Dot( c0, math::Vec3d_Cross( c1, c2 ) ) > 0.0 ) ) { return geometry_status_t::UNSUPPORTED; }
    const allocator_t *pA = pSource->attributes.faces.pAllocator;
    vector_t<geometry_mesh_vertex_handle_t> verts{};
    vector_t<math::vec3d_t> targets{};
    auto cleanup = [&]() noexcept {
        Vector_Shutdown( &verts );
        Vector_Shutdown( &targets );
    };
    if ( !Vector_Init( &verts, pA ) || !Vector_Init( &targets, pA ) ) {
        cleanup();
        return geometry_status_t::ALLOCATION_FAILED;
    }
    geometry_status_t st = MeshSelection_TryGatherVertices( pSelection, pSource, mode, &verts );
    if ( st == geometry_status_t::OK && verts.nCount > 0u ) {
        if ( !Vector_Resize( &targets, verts.nCount ) ) {
            st = geometry_status_t::ALLOCATION_FAILED;
        } else {
            for ( usize i = 0u; i < verts.nCount; ++i ) {
                targets.pData[i] =
                    math::Affine3d_TransformPoint( transform, GenerationPool_Get( &pSource->mesh.vertices, verts.pData[i] )->position );
            }
            st = MoveAll( pSource, verts, targets );
        }
    }
    cleanup();
    return st;
}

geometry_status_t MeshSourceEdit_TryOffsetComponents(
    mesh_source_t *pSource,
    const mesh_selection_t *pSelection,
    mesh_selection_mode_t mode,
    f64 distance ) noexcept
{
    if ( !MeshSource_IsInitialized( pSource ) || pSelection == nullptr ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( !std::isfinite( distance ) ) { return geometry_status_t::NUMERIC_FAILURE; }
    const editable_mesh_t *pM = &pSource->mesh;
    const allocator_t *pA = pSource->attributes.faces.pAllocator;
    vector_t<geometry_mesh_vertex_handle_t> verts{};
    vector_t<math::vec3d_t> targets{}, normalSum{};
    vector_t<u32> slotToIndex{};
    auto cleanup = [&]() noexcept {
        Vector_Shutdown( &verts );
        Vector_Shutdown( &targets );
        Vector_Shutdown( &normalSum );
        Vector_Shutdown( &slotToIndex );
    };
    if ( !Vector_Init( &verts, pA ) || !Vector_Init( &targets, pA ) || !Vector_Init( &normalSum, pA ) ||
         !Vector_Init( &slotToIndex, pA ) || !Vector_Resize( &slotToIndex, pM->vertices.cSlots ) ) {
        cleanup();
        return geometry_status_t::ALLOCATION_FAILED;
    }
    geometry_status_t st = MeshSelection_TryGatherVertices( pSelection, pSource, mode, &verts );
    if ( st != geometry_status_t::OK || verts.nCount == 0u ) {
        cleanup();
        return st;
    }
    if ( !Vector_Resize( &targets, verts.nCount ) || !Vector_Resize( &normalSum, verts.nCount ) ) {
        cleanup();
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize i = 0u; i < slotToIndex.nCount; ++i ) { slotToIndex.pData[i] = CY_INVALID_INDEX; }
    for ( usize i = 0u; i < verts.nCount; ++i ) {
        slotToIndex.pData[verts.pData[i].nSlot] = static_cast<u32>( i );
        normalSum.pData[i] = math::vec3d_t{};
    }
    // Sum the normals of the contributing faces at each gathered vertex.
    (void)GenerationPool_ForEach( &pM->faces,
        [&]( geometry_mesh_face_handle_t hF, const mesh_face_record_t &f ) noexcept -> bool_t {
            if ( mode == mesh_selection_mode_t::FACE && !MeshSelection_HasFace( pSelection, MeshSource_FaceId( pSource, hF ) ) ) {
                return true;
            }
            const mesh_loop_record_t *pL = GenerationPool_Get( &pM->loops, f.hOuterLoop );
            geometry_mesh_half_edge_handle_t h = pL->hFirstHalfEdge;
            for ( u32 k = 0u; k < pL->cHalfEdges; ++k ) {
                const mesh_half_edge_record_t *pH = GenerationPool_Get( &pM->halfEdges, h );
                const u32 idx = slotToIndex.pData[pH->hOrigin.nSlot];
                if ( idx != CY_INVALID_INDEX && verts.pData[idx].nGeneration == pH->hOrigin.nGeneration ) {
                    normalSum.pData[idx] = math::Vec3d_Add( normalSum.pData[idx], f.normal );
                }
                h = pH->hNext;
            }
            return true;
        } );
    for ( usize i = 0u; i < verts.nCount && st == geometry_status_t::OK; ++i ) {
        math::vec3d_t dir{};
        if ( !math::Vec3d_TryNormalize( normalSum.pData[i], 0.0, &dir, nullptr ) ) {
            st = geometry_status_t::DEGENERATE;
            break;
        }
        targets.pData[i] = math::Vec3d_Add( GenerationPool_Get( &pM->vertices, verts.pData[i] )->position, math::Vec3d_Scale( dir, distance ) );
    }
    if ( st == geometry_status_t::OK ) { st = MoveAll( pSource, verts, targets ); }
    cleanup();
    return st;
}

} // namespace cypher::editor::geometry
