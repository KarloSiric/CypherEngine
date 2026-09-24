//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshRecordAccess.h
//  Purpose: Internal record-access and capacity helpers shared by the
//           editable-mesh operation translation units (boundary ops, knife,
//           and the operations that follow them).
//  Details: Not part of the public geometry API: nothing outside
//           Representations/Mesh should include this. The helpers live in
//           mesh_detail so they cannot collide with the public MeshOps_ /
//           MeshBoundary_ names, and they are inline so each operation file
//           can use them without a separate translation unit.
//
//           Why shared: every failure-atomic mesh operation needs the same
//           handful of primitives - handle keys for sorting, tolerant
//           half-edge lookups, the Newell normal, and "reserve N more records"
//           before the mutation phase. Keeping one copy means a fix to, say,
//           the pool-limit check applies to every operation at once.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_MESH_RECORD_ACCESS_H
#define CYPHER_EDITOR_GEOMETRY_MESH_RECORD_ACCESS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_EditableMesh.h"
#include "CypherGeometry_Sanitation.h"
#include "CypherCommon_Vector.h"

#include <cmath>

namespace cypher::editor::geometry::mesh_detail
{

// Total order on handles (slot, then generation) for sorted lookups.
template <typename tag_t>
inline common::u64 Key( common::generation_handle_t<tag_t> h ) noexcept
{
    return ( static_cast<common::u64>( h.nSlot ) << 32 ) | h.nGeneration;
}

template <typename tag_t>
inline bool Same( common::generation_handle_t<tag_t> a, common::generation_handle_t<tag_t> b ) noexcept
{
    return a.nSlot == b.nSlot && a.nGeneration == b.nGeneration;
}

inline const mesh_half_edge_record_t *He( const editable_mesh_t *pMesh, geometry_mesh_half_edge_handle_t h ) noexcept
{
    return common::GenerationPool_Get( &pMesh->halfEdges, h );
}

inline mesh_half_edge_record_t *HeMut( editable_mesh_t *pMesh, geometry_mesh_half_edge_handle_t h ) noexcept
{
    return common::GenerationPool_Get( &pMesh->halfEdges, h );
}

// Face owning half-edge h, or an invalid handle.
inline geometry_mesh_face_handle_t FaceOf( const editable_mesh_t *pMesh, geometry_mesh_half_edge_handle_t h ) noexcept
{
    const mesh_half_edge_record_t *pH = He( pMesh, h );
    const mesh_loop_record_t *pL = pH ? common::GenerationPool_Get( &pMesh->loops, pH->hLoop ) : nullptr;
    return pL ? pL->hFace : GEOMETRY_HANDLE_INVALID<geometry_mesh_face_tag_t>;
}

inline geometry_mesh_vertex_handle_t DestOf( const editable_mesh_t *pMesh, const mesh_half_edge_record_t &h ) noexcept
{
    const mesh_half_edge_record_t *pN = He( pMesh, h.hNext );
    return pN ? pN->hOrigin : GEOMETRY_HANDLE_INVALID<geometry_mesh_vertex_tag_t>;
}

inline bool IsBoundaryHe( const editable_mesh_t *pMesh, const mesh_half_edge_record_t &h ) noexcept
{
    return !GeometryHandle_IsValid( h.hTwin ) || He( pMesh, h.hTwin ) == nullptr;
}

// Newell normal of a loop given as a vertex sequence (unnormalized). Used
// instead of a cross product of two edges because it stays meaningful for
// concave and slightly non-planar loops.
inline math::vec3d_t NewellOf( const math::vec3d_t *pPoints, common::u32 cPoints ) noexcept
{
    math::vec3d_t n{};
    for ( common::u32 i = 0u; i < cPoints; ++i ) {
        const math::vec3d_t a = pPoints[i], b = pPoints[( i + 1u ) % cPoints];
        n.x += ( a.y - b.y ) * ( a.z + b.z );
        n.y += ( a.z - b.z ) * ( a.x + b.x );
        n.z += ( a.x - b.x ) * ( a.y + b.y );
    }
    return n;
}

inline math::vec3d_t UnitOrZero( math::vec3d_t v ) noexcept
{
    math::vec3d_t n{};
    return math::Vec3d_TryNormalize( v, 0.0, &n, nullptr ) ? n : math::vec3d_t{};
}

// True when a face with this (unnormalized) Newell normal can be described
// as a mesh source. MeshSource_TryBuild rebuilds through Sanitation with its
// default policy, which rejects faces whose Newell area is at or below
// fMinimumFaceArea; checking the same number here means an edit can never
// produce a mesh that its own description cannot rebuild. An exact
// orientation test is not enough on its own: an edge-split point is only
// rounded onto its edge, so "collinear" corners can form a valid-but-tiny
// sliver. The formula matches Sanitation's (half the Newell length).
//
// The length must also be finite: a Newell sum over huge coordinates can
// overflow to infinity, which would otherwise pass "greater than the
// minimum" and then normalize to a zero normal.
inline bool FaceAreaDescribable( math::vec3d_t newell ) noexcept
{
    const common::f64 lengthSquared = math::Vec3d_LengthSquared( newell );
    return std::isfinite( lengthSquared ) &&
           0.5 * std::sqrt( lengthSquared ) > sanitation_policy_t{}.fMinimumFaceArea;
}

// Reserves room for `extra` more records in a pool (and checks the pool
// limit), so the following inserts cannot allocate or fail. Inserts take
// free slots (neither occupied nor retired) first, so the slot array only
// grows by the shortfall; growing by `extra` unconditionally would make
// every remove-and-add edit enlarge the pools for good. Each pool must be
// reserved once per operation with its total, since calls do not add up.
template <typename record_t, typename tag_t>
inline geometry_status_t ReserveMore( common::generation_pool_t<record_t, tag_t> *pPool, common::usize extra ) noexcept
{
    if ( common::GenerationPool_Count( pPool ) + extra > pPool->cSlotLimit ) { return geometry_status_t::LIMIT_EXCEEDED; }
    const common::usize cFree = pPool->cSlots - pPool->cRecords - pPool->cRetiredSlots;
    if ( cFree >= extra ) { return geometry_status_t::OK; }
    const common::generation_pool_status_t s = common::GenerationPool_Reserve( pPool, pPool->cSlots + ( extra - cFree ) );
    if ( s == common::generation_pool_status_t::OK ) { return geometry_status_t::OK; }
    return s == common::generation_pool_status_t::LIMIT_EXCEEDED ? geometry_status_t::LIMIT_EXCEEDED
                                                                 : geometry_status_t::ALLOCATION_FAILED;
}

// Reserves for RebuildShells, which removes every existing shell record and
// then inserts cShellsAfter new ones. The removed records' slots are reused,
// so the limit applies to cShellsAfter alone and only the shortfall beyond
// the reusable slots needs growth. (ReserveMore(old + new) would refuse legal
// edits once more than half the shell limit is in use.) A record whose
// generation is exhausted retires on removal instead of being reused.
template <typename record_t, typename tag_t>
inline geometry_status_t ReserveForRebuild( common::generation_pool_t<record_t, tag_t> *pPool, common::usize cAfter ) noexcept
{
    if ( cAfter > pPool->cSlotLimit ) { return geometry_status_t::LIMIT_EXCEEDED; }
    common::usize cWouldRetire = 0u;
    for ( common::usize i = 0u; i < pPool->cSlots; ++i ) {
        cWouldRetire += ( pPool->pSlots[i].bOccupied && pPool->pSlots[i].nGeneration == CY_U32_MAX ) ? 1u : 0u;
    }
    const common::usize cReusable = pPool->cSlots - pPool->cRetiredSlots - cWouldRetire;
    if ( cReusable >= cAfter ) { return geometry_status_t::OK; }
    const common::generation_pool_status_t s = common::GenerationPool_Reserve( pPool, pPool->cSlots + ( cAfter - cReusable ) );
    if ( s == common::generation_pool_status_t::OK ) { return geometry_status_t::OK; }
    return s == common::generation_pool_status_t::LIMIT_EXCEEDED ? geometry_status_t::LIMIT_EXCEEDED
                                                                 : geometry_status_t::ALLOCATION_FAILED;
}

// True when h starts its vertex's fan: the half-edge before it in its face
// (the incoming edge at the same vertex) has no twin, so h's face is the
// first face of an open fan.
inline bool IsFanStart( const editable_mesh_t *pMesh, const mesh_half_edge_record_t &h ) noexcept
{
    const mesh_half_edge_record_t *pPrev = He( pMesh, h.hPrev );
    return pPrev != nullptr && IsBoundaryHe( pMesh, *pPrev );
}

// Gives every vertex whose slot is marked in `touched` a live outgoing
// half-edge, and on open fans the fan's first one (IsFanStart). That is the
// convention Sanitation builds and every fan walk relies on: walks rotate
// h -> next(twin(h)), which from the fan start visits every face and stops
// at the far boundary edge. Starting anywhere else on an open fan stops
// early, so e.g. a moved rim vertex would update only some of its faces'
// normals. Interior vertices may keep any live outgoing half-edge. One pass
// over the half-edges; `touched` must cover every marked slot.
inline void FixOutEdges( editable_mesh_t *pMesh, const common::vector_t<common::u8> &touched ) noexcept
{
    (void)common::GenerationPool_ForEach( &pMesh->halfEdges,
        [&]( geometry_mesh_half_edge_handle_t h, const mesh_half_edge_record_t &he ) noexcept -> common::bool_t {
            const common::u32 slot = he.hOrigin.nSlot;
            if ( slot >= touched.nCount || touched.pData[slot] == 0u ) { return true; }
            mesh_vertex_record_t *pV = common::GenerationPool_Get( &pMesh->vertices, he.hOrigin );
            if ( pV == nullptr ) { return true; }
            const mesh_half_edge_record_t *pCur = He( pMesh, pV->hOutHalfEdge );
            const bool bCurValid = pCur != nullptr && Same( pCur->hOrigin, he.hOrigin );
            const bool bCurStart = bCurValid && IsFanStart( pMesh, *pCur );
            if ( !bCurValid || ( !bCurStart && IsFanStart( pMesh, he ) ) ) { pV->hOutHalfEdge = h; }
            return true;
        } );
}

// Recomputes a face's normal from its current outer loop (Newell, unit or
// zero). The mesh operations call this for every face whose loop they
// changed, matching MeshOps' RecomputeFaceNormal. cMax bounds the walk.
inline void RecomputeNormal( editable_mesh_t *pMesh, geometry_mesh_face_handle_t hFace, math::vec3d_t *pScratch,
                             common::u32 cMax ) noexcept
{
    mesh_face_record_t *pF = common::GenerationPool_Get( &pMesh->faces, hFace );
    const mesh_loop_record_t *pL = pF ? common::GenerationPool_Get( &pMesh->loops, pF->hOuterLoop ) : nullptr;
    if ( pL == nullptr || pL->cHalfEdges > cMax ) { return; }
    geometry_mesh_half_edge_handle_t h = pL->hFirstHalfEdge;
    for ( common::u32 k = 0u; k < pL->cHalfEdges; ++k ) {
        const mesh_half_edge_record_t *pH = He( pMesh, h );
        pScratch[k] = common::GenerationPool_Get( &pMesh->vertices, pH->hOrigin )->position;
        h = pH->hNext;
    }
    pF->normal = UnitOrZero( NewellOf( pScratch, pL->cHalfEdges ) );
}

} // namespace cypher::editor::geometry::mesh_detail

#endif // CYPHER_EDITOR_GEOMETRY_MESH_RECORD_ACCESS_H
