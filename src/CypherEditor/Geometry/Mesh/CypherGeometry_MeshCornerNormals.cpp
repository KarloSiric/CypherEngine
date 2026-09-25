//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshCornerNormals.cpp
//  Purpose: Implements smoothing-group / hard-edge split normals.
//  Details: Rotation around a vertex uses the half-edge identities
//             ccw(h) = next(twin(h))   (next outgoing half-edge around v)
//             cw(h)  = twin(prev(h))   (previous outgoing half-edge)
//           Crossing from h's face to ccw(h)'s face crosses edge(h)'s twin
//           side, i.e. the edge of h itself; crossing to cw(h)'s face
//           crosses edge(prev(h)).
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshCornerNormals.h"

#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

struct ctx_t {
    const editable_mesh_t *pMesh;
    const mesh_attribute_store_t *pStore;
};

const mesh_face_record_t *FaceOf( const ctx_t &c, const mesh_half_edge_record_t *pH ) noexcept
{
    const mesh_loop_record_t *pL = GenerationPool_Get( &c.pMesh->loops, pH->hLoop );
    return pL ? GenerationPool_Get( &c.pMesh->faces, pL->hFace ) : nullptr;
}

geometry_mesh_face_handle_t FaceHandleOf( const ctx_t &c, const mesh_half_edge_record_t *pH ) noexcept
{
    const mesh_loop_record_t *pL = GenerationPool_Get( &c.pMesh->loops, pH->hLoop );
    return pL ? pL->hFace : geometry_mesh_face_handle_t{};
}

u32 Groups( const ctx_t &c, geometry_mesh_face_handle_t hF ) noexcept
{
    return c.pStore ? MeshAttributeStore_GetFace( c.pStore, hF ).smoothingGroups : 1u;
}

// Is the edge between faces fa/fb (edge handle hE) smooth?
bool Smooth( const ctx_t &c, geometry_mesh_edge_handle_t hE, geometry_mesh_face_handle_t fa,
             geometry_mesh_face_handle_t fb ) noexcept
{
    if ( c.pStore && ( MeshAttributeStore_GetEdge( c.pStore, hE ).flags & MESH_EDGE_FLAG_HARD ) ) {
        return false;
    }
    return ( Groups( c, fa ) & Groups( c, fb ) ) != 0u;
}

// Interior angle of the face at the corner starting half-edge h.
f64 CornerAngle( const ctx_t &c, const mesh_half_edge_record_t *pH ) noexcept
{
    const mesh_half_edge_record_t *pN = GenerationPool_Get( &c.pMesh->halfEdges, pH->hNext );
    const mesh_half_edge_record_t *pP = GenerationPool_Get( &c.pMesh->halfEdges, pH->hPrev );
    if ( pN == nullptr || pP == nullptr ) { return 0.0; }
    const mesh_vertex_record_t *pV = GenerationPool_Get( &c.pMesh->vertices, pH->hOrigin );
    const mesh_vertex_record_t *pA = GenerationPool_Get( &c.pMesh->vertices, pN->hOrigin );
    const mesh_vertex_record_t *pB = GenerationPool_Get( &c.pMesh->vertices, pP->hOrigin );
    if ( pV == nullptr || pA == nullptr || pB == nullptr ) { return 0.0; }
    const math::vec3d_t e1 = math::Vec3d_Subtract( pA->position, pV->position );
    const math::vec3d_t e2 = math::Vec3d_Subtract( pB->position, pV->position );
    const f64 cr = std::sqrt( math::Vec3d_LengthSquared( math::Vec3d_Cross( e1, e2 ) ) );
    // atan2(|cross|, dot) is well conditioned for all angles, unlike acos.
    return std::atan2( cr, math::Vec3d_Dot( e1, e2 ) );
}

} // namespace

geometry_status_t MeshNormals_TryComputeCornerNormals(
    const editable_mesh_t *pMesh,
    const mesh_attribute_store_t *pStore,
    vector_t<mesh_corner_normal_record_t> *pOut ) noexcept
{
    if ( pMesh == nullptr || pOut == nullptr || pOut->pAllocator == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !EditableMesh_IsInitialized( pMesh ) ||
         ( pStore != nullptr && !MeshAttributeStore_IsInitialized( pStore ) ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    Vector_Clear( pOut );
    const ctx_t c{ pMesh, pStore };

    // Group ids: slot-indexed "already assigned" map so every corner of one
    // smooth fan gets the id of the fan's first-visited corner.
    const usize cSlots = GenerationPool_Capacity( &pMesh->halfEdges );
    vector_t<u32> groupOf{};
    if ( !Vector_Init( &groupOf, pOut->pAllocator, cSlots ) || !Vector_Resize( &groupOf, cSlots ) ||
         !Vector_Reserve( pOut, EditableMesh_HalfEdgeCount( pMesh ) ) ) {
        Vector_Shutdown( &groupOf );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize i = 0u; i < cSlots; ++i ) { groupOf.pData[i] = CY_INVALID_INDEX; }
    u32 nextGroup = 0u;
    bool ok = true;

    (void)GenerationPool_ForEach( &pMesh->halfEdges,
        [&]( geometry_mesh_half_edge_handle_t h0, const mesh_half_edge_record_t &he0 ) noexcept -> bool_t {
            const mesh_face_record_t *pF0 = FaceOf( c, &he0 );
            if ( pF0 == nullptr ) { ok = false; return false; }
            math::vec3d_t sum = math::Vec3d_Scale( pF0->normal, CornerAngle( c, &he0 ) );
            u32 group = groupOf.pData[h0.nSlot];
            const bool newGroup = group == CY_INVALID_INDEX;
            if ( newGroup ) { group = nextGroup++; groupOf.pData[h0.nSlot] = group; }

            // Rotate CCW: from h to next(twin(h)), crossing edge(h).
            geometry_mesh_half_edge_handle_t h = h0;
            const mesh_half_edge_record_t *pH = &he0;
            bool wrapped = false;
            for ( u32 guard = 0u; guard < 1024u; ++guard ) {
                const mesh_half_edge_record_t *pT = GenerationPool_Get( &pMesh->halfEdges, pH->hTwin );
                if ( pT == nullptr ) { break; } // open boundary
                const geometry_mesh_half_edge_handle_t hn = pT->hNext;
                const mesh_half_edge_record_t *pN = GenerationPool_Get( &pMesh->halfEdges, hn );
                if ( pN == nullptr ) { ok = false; return false; }
                if ( !Smooth( c, pH->hEdge, FaceHandleOf( c, pH ), FaceHandleOf( c, pN ) ) ) { break; }
                if ( hn.nSlot == h0.nSlot && hn.nGeneration == h0.nGeneration ) { wrapped = true; break; }
                const mesh_face_record_t *pFn = FaceOf( c, pN );
                if ( pFn == nullptr ) { ok = false; return false; }
                sum = math::Vec3d_Add( sum, math::Vec3d_Scale( pFn->normal, CornerAngle( c, pN ) ) );
                if ( newGroup ) { groupOf.pData[hn.nSlot] = group; }
                h = hn;
                pH = pN;
            }
            // Rotate CW (unless the fan already closed): from h to
            // twin(prev(h)), crossing edge(prev(h)).
            if ( !wrapped ) {
                pH = &he0;
                for ( u32 guard = 0u; guard < 1024u; ++guard ) {
                    const mesh_half_edge_record_t *pP = GenerationPool_Get( &pMesh->halfEdges, pH->hPrev );
                    if ( pP == nullptr ) { ok = false; return false; }
                    const geometry_mesh_half_edge_handle_t hc = pP->hTwin;
                    const mesh_half_edge_record_t *pC = GenerationPool_Get( &pMesh->halfEdges, hc );
                    if ( pC == nullptr ) { break; }
                    if ( !Smooth( c, pP->hEdge, FaceHandleOf( c, pH ), FaceHandleOf( c, pC ) ) ) { break; }
                    if ( hc.nSlot == h0.nSlot && hc.nGeneration == h0.nGeneration ) { break; }
                    const mesh_face_record_t *pFc = FaceOf( c, pC );
                    if ( pFc == nullptr ) { ok = false; return false; }
                    sum = math::Vec3d_Add( sum, math::Vec3d_Scale( pFc->normal, CornerAngle( c, pC ) ) );
                    if ( newGroup ) { groupOf.pData[hc.nSlot] = group; }
                    pH = pC;
                }
            }
            math::vec3d_t n{};
            if ( !math::Vec3d_TryNormalize( sum, 1.0e-300, &n, nullptr ) ) { n = pF0->normal; }
            (void)Vector_PushBack( pOut, mesh_corner_normal_record_t{ h0, n, group } );
            (void)h;
            return true;
        } );
    Vector_Shutdown( &groupOf );
    if ( !ok ) {
        Vector_Clear( pOut );
        return geometry_status_t::CORRUPT_STATE;
    }
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
