//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshSelectionQueries.cpp
//  Purpose: Implements the selection queries and the component vertex set /
//           pivot.
//  Details: Each query walks the editable mesh once, collects the matching
//           IDs into scratch, and hands them to MergeSorted, which reserves
//           the final size before touching the selection and then merges in
//           place from the back. So a query either adds everything it found
//           or leaves the selection unchanged.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshSelectionQueries.h"

#include <algorithm>
#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

bool IdLess( geometry_source_id_t a, geometry_source_id_t b ) noexcept { return a.value < b.value; }
bool IdEq( geometry_source_id_t a, geometry_source_id_t b ) noexcept { return a.value == b.value; }
bool EdgeLess( const mesh_edge_ref_t &x, const mesh_edge_ref_t &y ) noexcept
{
    return x.a.value != y.a.value ? x.a.value < y.a.value : x.b.value < y.b.value;
}
bool EdgeEq( const mesh_edge_ref_t &x, const mesh_edge_ref_t &y ) noexcept
{
    return x.a.value == y.a.value && x.b.value == y.b.value;
}

// Adds `add` (any order, may repeat or overlap the selection) to the sorted
// set *pDst. Reserves first, so failure leaves *pDst unchanged.
template <typename t, typename less_t, typename eq_t>
geometry_status_t MergeSorted( vector_t<t> *pDst, vector_t<t> *pAdd, less_t less, eq_t eq ) noexcept
{
    std::sort( pAdd->pData, pAdd->pData + pAdd->nCount, less );
    usize w = 0u;
    for ( usize i = 0u; i < pAdd->nCount; ++i ) {
        const t &x = pAdd->pData[i];
        if ( w > 0u && eq( pAdd->pData[w - 1u], x ) ) { continue; }
        const t *pEnd = pDst->pData + pDst->nCount;
        const t *p = std::lower_bound( static_cast<const t *>( pDst->pData ), pEnd, x, less );
        if ( p != pEnd && eq( *p, x ) ) { continue; }
        pAdd->pData[w++] = x;
    }
    if ( w == 0u ) { return geometry_status_t::OK; }
    const usize n = pDst->nCount;
    if ( !Vector_Reserve( pDst, n + w ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    (void)Vector_Resize( pDst, n + w ); // within the reserved capacity
    usize i = n, j = w, k = n + w;
    while ( j > 0u ) {
        if ( i > 0u && less( pAdd->pData[j - 1u], pDst->pData[i - 1u] ) ) {
            pDst->pData[--k] = pDst->pData[--i];
        } else {
            pDst->pData[--k] = pAdd->pData[--j];
        }
    }
    return geometry_status_t::OK;
}

bool Ready( const mesh_selection_t *pSelection, const mesh_source_t *pMesh ) noexcept
{
    return pSelection != nullptr && pSelection->faces.pAllocator != nullptr && MeshSource_IsInitialized( pMesh );
}

const allocator_t *AllocatorOf( const mesh_selection_t *pSelection ) noexcept { return pSelection->faces.pAllocator; }

// Calls fn(hFace, face, loop) for every live face.
template <typename fn_t>
void ForEachFace( const mesh_source_t *pMesh, fn_t &&fn ) noexcept
{
    (void)GenerationPool_ForEach( &pMesh->mesh.faces,
        [&]( geometry_mesh_face_handle_t hF, const mesh_face_record_t &f ) noexcept -> bool_t {
            const mesh_loop_record_t *pL = GenerationPool_Get( &pMesh->mesh.loops, f.hOuterLoop );
            if ( pL != nullptr ) { fn( hF, f, *pL ); }
            return true;
        } );
}

// Calls fn(position) for every corner of a loop.
template <typename fn_t>
void ForEachCorner( const mesh_source_t *pMesh, const mesh_loop_record_t &loop, fn_t &&fn ) noexcept
{
    geometry_mesh_half_edge_handle_t h = loop.hFirstHalfEdge;
    for ( u32 k = 0u; k < loop.cHalfEdges; ++k ) {
        const mesh_half_edge_record_t *pH = GenerationPool_Get( &pMesh->mesh.halfEdges, h );
        if ( pH == nullptr ) { return; }
        const mesh_vertex_record_t *pV = GenerationPool_Get( &pMesh->mesh.vertices, pH->hOrigin );
        if ( pV != nullptr ) { fn( pH->hOrigin, pV->position ); }
        h = pH->hNext;
    }
}

f64 CosOf( f64 angle ) noexcept { return std::cos( angle ); }

bool AngleOk( f64 angle ) noexcept { return std::isfinite( angle ) && angle >= 0.0; }

// Source-ID edge reference of edge record hEdge (invalid when either end has
// no ID yet).
bool EdgeRefOf( const mesh_source_t *pMesh, geometry_mesh_edge_handle_t hEdge, mesh_edge_ref_t *pOut ) noexcept
{
    const mesh_edge_record_t *pE = GenerationPool_Get( &pMesh->mesh.edges, hEdge );
    const mesh_half_edge_record_t *pH = pE ? GenerationPool_Get( &pMesh->mesh.halfEdges, pE->hHalfEdge ) : nullptr;
    const mesh_half_edge_record_t *pN = pH ? GenerationPool_Get( &pMesh->mesh.halfEdges, pH->hNext ) : nullptr;
    if ( pN == nullptr ) { return false; }
    const geometry_source_id_t a = MeshSource_VertexId( pMesh, pH->hOrigin ), b = MeshSource_VertexId( pMesh, pN->hOrigin );
    if ( !GeometrySourceId_IsValid( a ) || !GeometrySourceId_IsValid( b ) ) { return false; }
    *pOut = MeshEdgeRef_Make( a, b );
    return true;
}

} // namespace

geometry_status_t MeshSelection_TrySelectFacesByMaterial(
    mesh_selection_t *pSelection,
    const mesh_source_t *pMesh,
    geometry_material_ref_t material ) noexcept
{
    if ( !Ready( pSelection, pMesh ) ) { return geometry_status_t::NOT_INITIALIZED; }
    vector_t<geometry_source_id_t> found{};
    if ( !Vector_Init( &found, AllocatorOf( pSelection ) ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    bool bOk = true;
    ForEachFace( pMesh, [&]( geometry_mesh_face_handle_t hF, const mesh_face_record_t &, const mesh_loop_record_t & ) noexcept {
        const geometry_source_id_t id = MeshSource_FaceId( pMesh, hF );
        if ( bOk && GeometrySourceId_IsValid( id ) &&
             MeshAttributeStore_GetFace( &pMesh->attributes, hF ).material.value == material.value ) {
            bOk = Vector_PushBack( &found, id );
        }
    } );
    const geometry_status_t st = bOk ? MergeSorted( &pSelection->faces, &found, IdLess, IdEq ) : geometry_status_t::ALLOCATION_FAILED;
    Vector_Shutdown( &found );
    return st;
}

geometry_status_t MeshSelection_TrySelectCoplanar(
    mesh_selection_t *pSelection,
    const mesh_source_t *pMesh,
    geometry_source_id_t seedFace,
    f64 maxAngle,
    f64 maxDistance ) noexcept
{
    if ( !Ready( pSelection, pMesh ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( !AngleOk( maxAngle ) || !std::isfinite( maxDistance ) || maxDistance < 0.0 ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    geometry_mesh_face_handle_t hSeed{};
    if ( !MeshSource_TryFindFace( pMesh, seedFace, &hSeed ) ) { return geometry_status_t::INVALID_HANDLE; }
    const editable_mesh_t *pM = &pMesh->mesh;
    const mesh_face_record_t *pSeed = GenerationPool_Get( &pM->faces, hSeed );
    const math::vec3d_t n0 = pSeed->normal;
    const mesh_loop_record_t *pSeedLoop = GenerationPool_Get( &pM->loops, pSeed->hOuterLoop );
    // The seed's plane passes through its first corner.
    const mesh_half_edge_record_t *pFirst = GenerationPool_Get( &pM->halfEdges, pSeedLoop->hFirstHalfEdge );
    const f64 d0 = -math::Vec3d_Dot( n0, GenerationPool_Get( &pM->vertices, pFirst->hOrigin )->position );
    const f64 cosMax = CosOf( maxAngle );

    vector_t<geometry_mesh_face_handle_t> queue{};
    vector_t<u8> seen{};
    vector_t<geometry_source_id_t> found{};
    const allocator_t *pA = AllocatorOf( pSelection );
    auto cleanup = [&]() noexcept {
        Vector_Shutdown( &queue );
        Vector_Shutdown( &seen );
        Vector_Shutdown( &found );
    };
    if ( !Vector_Init( &queue, pA ) || !Vector_Init( &seen, pA ) || !Vector_Init( &found, pA ) ||
         !Vector_Resize( &seen, pM->faces.cSlots ) || !Vector_PushBack( &queue, hSeed ) ) {
        cleanup();
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize i = 0u; i < seen.nCount; ++i ) { seen.pData[i] = 0u; }
    seen.pData[hSeed.nSlot] = 1u;
    bool bOk = true;
    for ( usize q = 0u; bOk && q < queue.nCount; ++q ) {
        const geometry_mesh_face_handle_t hF = queue.pData[q];
        const geometry_source_id_t id = MeshSource_FaceId( pMesh, hF );
        if ( GeometrySourceId_IsValid( id ) ) { bOk = Vector_PushBack( &found, id ); }
        const mesh_loop_record_t *pL = GenerationPool_Get( &pM->loops, GenerationPool_Get( &pM->faces, hF )->hOuterLoop );
        geometry_mesh_half_edge_handle_t h = pL->hFirstHalfEdge;
        for ( u32 k = 0u; bOk && k < pL->cHalfEdges; ++k ) {
            const mesh_half_edge_record_t *pH = GenerationPool_Get( &pM->halfEdges, h );
            const mesh_half_edge_record_t *pT = GenerationPool_Get( &pM->halfEdges, pH->hTwin );
            const mesh_loop_record_t *pTL = pT ? GenerationPool_Get( &pM->loops, pT->hLoop ) : nullptr;
            if ( pTL != nullptr && seen.pData[pTL->hFace.nSlot] == 0u ) {
                seen.pData[pTL->hFace.nSlot] = 1u;
                const mesh_face_record_t *pN = GenerationPool_Get( &pM->faces, pTL->hFace );
                bool bCoplanar = math::Vec3d_Dot( pN->normal, n0 ) >= cosMax;
                ForEachCorner( pMesh, *pTL, [&]( geometry_mesh_vertex_handle_t, math::vec3d_t p ) noexcept {
                    bCoplanar = bCoplanar && std::fabs( math::Vec3d_Dot( n0, p ) + d0 ) <= maxDistance;
                } );
                if ( bCoplanar ) { bOk = Vector_PushBack( &queue, pTL->hFace ); }
            }
            h = pH->hNext;
        }
    }
    const geometry_status_t st = bOk ? MergeSorted( &pSelection->faces, &found, IdLess, IdEq ) : geometry_status_t::ALLOCATION_FAILED;
    cleanup();
    return st;
}

geometry_status_t MeshSelection_TrySelectFacesFacing(
    mesh_selection_t *pSelection,
    const mesh_source_t *pMesh,
    math::vec3d_t direction,
    f64 maxAngle ) noexcept
{
    if ( !Ready( pSelection, pMesh ) ) { return geometry_status_t::NOT_INITIALIZED; }
    math::vec3d_t dir{};
    if ( !AngleOk( maxAngle ) || !math::Vec3d_TryNormalize( direction, 0.0, &dir, nullptr ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const f64 cosMax = CosOf( maxAngle );
    vector_t<geometry_source_id_t> found{};
    if ( !Vector_Init( &found, AllocatorOf( pSelection ) ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    bool bOk = true;
    ForEachFace( pMesh, [&]( geometry_mesh_face_handle_t hF, const mesh_face_record_t &f, const mesh_loop_record_t & ) noexcept {
        const geometry_source_id_t id = MeshSource_FaceId( pMesh, hF );
        if ( bOk && GeometrySourceId_IsValid( id ) && math::Vec3d_Dot( f.normal, dir ) >= cosMax ) {
            bOk = Vector_PushBack( &found, id );
        }
    } );
    const geometry_status_t st = bOk ? MergeSorted( &pSelection->faces, &found, IdLess, IdEq ) : geometry_status_t::ALLOCATION_FAILED;
    Vector_Shutdown( &found );
    return st;
}

geometry_status_t MeshSelection_TrySelectSharpEdges(
    mesh_selection_t *pSelection,
    const mesh_source_t *pMesh,
    f64 minAngle ) noexcept
{
    if ( !Ready( pSelection, pMesh ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( !AngleOk( minAngle ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    // Sharp when the normals' angle >= minAngle, i.e. their dot <= cos.
    const f64 cosMin = CosOf( minAngle );
    const editable_mesh_t *pM = &pMesh->mesh;
    vector_t<mesh_edge_ref_t> found{};
    if ( !Vector_Init( &found, AllocatorOf( pSelection ) ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    bool bOk = true;
    (void)GenerationPool_ForEach( &pM->edges,
        [&]( geometry_mesh_edge_handle_t hE, const mesh_edge_record_t &e ) noexcept -> bool_t {
            const mesh_half_edge_record_t *pH = GenerationPool_Get( &pM->halfEdges, e.hHalfEdge );
            const mesh_half_edge_record_t *pT = pH ? GenerationPool_Get( &pM->halfEdges, pH->hTwin ) : nullptr;
            if ( pT == nullptr ) { return true; } // boundary: no dihedral angle
            const mesh_loop_record_t *pL0 = GenerationPool_Get( &pM->loops, pH->hLoop );
            const mesh_loop_record_t *pL1 = GenerationPool_Get( &pM->loops, pT->hLoop );
            const math::vec3d_t n0 = GenerationPool_Get( &pM->faces, pL0->hFace )->normal;
            const math::vec3d_t n1 = GenerationPool_Get( &pM->faces, pL1->hFace )->normal;
            mesh_edge_ref_t ref{};
            if ( math::Vec3d_Dot( n0, n1 ) <= cosMin && EdgeRefOf( pMesh, hE, &ref ) ) { bOk = Vector_PushBack( &found, ref ); }
            return bOk;
        } );
    const geometry_status_t st = bOk ? MergeSorted( &pSelection->edges, &found, EdgeLess, EdgeEq ) : geometry_status_t::ALLOCATION_FAILED;
    Vector_Shutdown( &found );
    return st;
}

geometry_status_t MeshSelection_TrySelectBoundaryEdges( mesh_selection_t *pSelection, const mesh_source_t *pMesh ) noexcept
{
    if ( !Ready( pSelection, pMesh ) ) { return geometry_status_t::NOT_INITIALIZED; }
    const editable_mesh_t *pM = &pMesh->mesh;
    vector_t<mesh_edge_ref_t> found{};
    if ( !Vector_Init( &found, AllocatorOf( pSelection ) ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    bool bOk = true;
    (void)GenerationPool_ForEach( &pM->edges,
        [&]( geometry_mesh_edge_handle_t hE, const mesh_edge_record_t &e ) noexcept -> bool_t {
            const mesh_half_edge_record_t *pH = GenerationPool_Get( &pM->halfEdges, e.hHalfEdge );
            mesh_edge_ref_t ref{};
            if ( pH != nullptr && GenerationPool_Get( &pM->halfEdges, pH->hTwin ) == nullptr && EdgeRefOf( pMesh, hE, &ref ) ) {
                bOk = Vector_PushBack( &found, ref );
            }
            return bOk;
        } );
    const geometry_status_t st = bOk ? MergeSorted( &pSelection->edges, &found, EdgeLess, EdgeEq ) : geometry_status_t::ALLOCATION_FAILED;
    Vector_Shutdown( &found );
    return st;
}

geometry_status_t MeshSelection_TrySelectFacesBySize(
    mesh_selection_t *pSelection,
    const mesh_source_t *pMesh,
    u32 minCorners,
    u32 maxCorners ) noexcept
{
    if ( !Ready( pSelection, pMesh ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( minCorners > maxCorners ) { return geometry_status_t::INVALID_ARGUMENT; }
    vector_t<geometry_source_id_t> found{};
    if ( !Vector_Init( &found, AllocatorOf( pSelection ) ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    bool bOk = true;
    ForEachFace( pMesh, [&]( geometry_mesh_face_handle_t hF, const mesh_face_record_t &, const mesh_loop_record_t &l ) noexcept {
        const geometry_source_id_t id = MeshSource_FaceId( pMesh, hF );
        if ( bOk && GeometrySourceId_IsValid( id ) && l.cHalfEdges >= minCorners && l.cHalfEdges <= maxCorners ) {
            bOk = Vector_PushBack( &found, id );
        }
    } );
    const geometry_status_t st = bOk ? MergeSorted( &pSelection->faces, &found, IdLess, IdEq ) : geometry_status_t::ALLOCATION_FAILED;
    Vector_Shutdown( &found );
    return st;
}

geometry_status_t MeshSelection_TryInvert( mesh_selection_t *pSelection, const mesh_source_t *pMesh, mesh_selection_mode_t mode ) noexcept
{
    if ( !Ready( pSelection, pMesh ) ) { return geometry_status_t::NOT_INITIALIZED; }
    const editable_mesh_t *pM = &pMesh->mesh;
    const allocator_t *pA = AllocatorOf( pSelection );
    if ( mode == mesh_selection_mode_t::EDGE ) {
        vector_t<mesh_edge_ref_t> all{};
        bool bOk = Vector_Init( &all, pA );
        (void)GenerationPool_ForEach( &pM->edges,
            [&]( geometry_mesh_edge_handle_t hE, const mesh_edge_record_t & ) noexcept -> bool_t {
                mesh_edge_ref_t ref{};
                if ( bOk && EdgeRefOf( pMesh, hE, &ref ) &&
                     !std::binary_search( static_cast<const mesh_edge_ref_t *>( pSelection->edges.pData ),
                                          static_cast<const mesh_edge_ref_t *>( pSelection->edges.pData + pSelection->edges.nCount ),
                                          ref, EdgeLess ) ) {
                    bOk = Vector_PushBack( &all, ref );
                }
                return bOk;
            } );
        // Swap in the complement only once it is fully built.
        if ( bOk && Vector_Reserve( &pSelection->edges, all.nCount ) ) {
            std::sort( all.pData, all.pData + all.nCount, EdgeLess );
            (void)Vector_Resize( &pSelection->edges, all.nCount );
            for ( usize i = 0u; i < all.nCount; ++i ) { pSelection->edges.pData[i] = all.pData[i]; }
            Vector_Shutdown( &all );
            return geometry_status_t::OK;
        }
        Vector_Shutdown( &all );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    const bool bFaces = mode == mesh_selection_mode_t::FACE;
    vector_t<geometry_source_id_t> *pSet = bFaces ? &pSelection->faces : &pSelection->vertices;
    vector_t<geometry_source_id_t> all{};
    bool bOk = Vector_Init( &all, pA );
    auto consider = [&]( geometry_source_id_t id ) noexcept {
        if ( bOk && GeometrySourceId_IsValid( id ) &&
             !std::binary_search( static_cast<const geometry_source_id_t *>( pSet->pData ),
                                  static_cast<const geometry_source_id_t *>( pSet->pData + pSet->nCount ), id, IdLess ) ) {
            bOk = Vector_PushBack( &all, id );
        }
    };
    if ( bFaces ) {
        (void)GenerationPool_ForEach( &pM->faces, [&]( geometry_mesh_face_handle_t h, const mesh_face_record_t & ) noexcept -> bool_t {
            consider( MeshSource_FaceId( pMesh, h ) );
            return bOk;
        } );
    } else {
        (void)GenerationPool_ForEach( &pM->vertices,
            [&]( geometry_mesh_vertex_handle_t h, const mesh_vertex_record_t & ) noexcept -> bool_t {
                consider( MeshSource_VertexId( pMesh, h ) );
                return bOk;
            } );
    }
    if ( bOk && Vector_Reserve( pSet, all.nCount ) ) {
        std::sort( all.pData, all.pData + all.nCount, IdLess );
        (void)Vector_Resize( pSet, all.nCount );
        for ( usize i = 0u; i < all.nCount; ++i ) { pSet->pData[i] = all.pData[i]; }
        Vector_Shutdown( &all );
        return geometry_status_t::OK;
    }
    Vector_Shutdown( &all );
    return geometry_status_t::ALLOCATION_FAILED;
}

geometry_status_t MeshSelection_TrySelectInVolume(
    mesh_selection_t *pSelection,
    const mesh_source_t *pMesh,
    span_t<const math::planed_t> planes,
    mesh_selection_mode_t mode,
    mesh_volume_rule_t rule ) noexcept
{
    if ( !Ready( pSelection, pMesh ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( planes.nCount == 0u || planes.pData == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    for ( usize i = 0u; i < planes.nCount; ++i ) {
        if ( !math::Vec3d_IsFinite( planes.pData[i].normal ) || !std::isfinite( planes.pData[i].d ) ) {
            return geometry_status_t::NUMERIC_FAILURE;
        }
    }
    auto inside = [&]( math::vec3d_t p ) noexcept {
        for ( usize i = 0u; i < planes.nCount; ++i ) {
            if ( math::Vec3d_Dot( planes.pData[i].normal, p ) + planes.pData[i].d > 0.0 ) { return false; }
        }
        return true;
    };
    const editable_mesh_t *pM = &pMesh->mesh;
    const allocator_t *pA = AllocatorOf( pSelection );
    // Judges a point set by the rule: all inside, any inside, or its mean.
    struct judge_t {
        u32 cPoints{ 0u };
        u32 cInside{ 0u };
        math::vec3d_t sum{};
    };
    auto verdict = [&]( const judge_t &j ) noexcept {
        switch ( rule ) {
            case mesh_volume_rule_t::CONTAINED: return j.cPoints > 0u && j.cInside == j.cPoints;
            case mesh_volume_rule_t::ANY_VERTEX: return j.cInside > 0u;
            default: return j.cPoints > 0u && inside( math::Vec3d_Scale( j.sum, 1.0 / static_cast<f64>( j.cPoints ) ) );
        }
    };
    geometry_status_t st = geometry_status_t::OK;
    if ( mode == mesh_selection_mode_t::VERTEX ) {
        vector_t<geometry_source_id_t> found{};
        bool bOk = Vector_Init( &found, pA );
        (void)GenerationPool_ForEach( &pM->vertices,
            [&]( geometry_mesh_vertex_handle_t h, const mesh_vertex_record_t &v ) noexcept -> bool_t {
                const geometry_source_id_t id = MeshSource_VertexId( pMesh, h );
                if ( bOk && GeometrySourceId_IsValid( id ) && inside( v.position ) ) { bOk = Vector_PushBack( &found, id ); }
                return bOk;
            } );
        st = bOk ? MergeSorted( &pSelection->vertices, &found, IdLess, IdEq ) : geometry_status_t::ALLOCATION_FAILED;
        Vector_Shutdown( &found );
    } else if ( mode == mesh_selection_mode_t::EDGE ) {
        vector_t<mesh_edge_ref_t> found{};
        bool bOk = Vector_Init( &found, pA );
        (void)GenerationPool_ForEach( &pM->edges,
            [&]( geometry_mesh_edge_handle_t hE, const mesh_edge_record_t &e ) noexcept -> bool_t {
                const mesh_half_edge_record_t *pH = GenerationPool_Get( &pM->halfEdges, e.hHalfEdge );
                const mesh_half_edge_record_t *pN = pH ? GenerationPool_Get( &pM->halfEdges, pH->hNext ) : nullptr;
                if ( pN == nullptr ) { return true; }
                judge_t j{};
                for ( geometry_mesh_vertex_handle_t hv : { pH->hOrigin, pN->hOrigin } ) {
                    const math::vec3d_t p = GenerationPool_Get( &pM->vertices, hv )->position;
                    ++j.cPoints;
                    j.cInside += inside( p ) ? 1u : 0u;
                    j.sum = math::Vec3d_Add( j.sum, p );
                }
                mesh_edge_ref_t ref{};
                if ( bOk && verdict( j ) && EdgeRefOf( pMesh, hE, &ref ) ) { bOk = Vector_PushBack( &found, ref ); }
                return bOk;
            } );
        st = bOk ? MergeSorted( &pSelection->edges, &found, EdgeLess, EdgeEq ) : geometry_status_t::ALLOCATION_FAILED;
        Vector_Shutdown( &found );
    } else {
        vector_t<geometry_source_id_t> found{};
        bool bOk = Vector_Init( &found, pA );
        ForEachFace( pMesh, [&]( geometry_mesh_face_handle_t hF, const mesh_face_record_t &, const mesh_loop_record_t &l ) noexcept {
            judge_t j{};
            ForEachCorner( pMesh, l, [&]( geometry_mesh_vertex_handle_t, math::vec3d_t p ) noexcept {
                ++j.cPoints;
                j.cInside += inside( p ) ? 1u : 0u;
                j.sum = math::Vec3d_Add( j.sum, p );
            } );
            const geometry_source_id_t id = MeshSource_FaceId( pMesh, hF );
            if ( bOk && GeometrySourceId_IsValid( id ) && verdict( j ) ) { bOk = Vector_PushBack( &found, id ); }
        } );
        st = bOk ? MergeSorted( &pSelection->faces, &found, IdLess, IdEq ) : geometry_status_t::ALLOCATION_FAILED;
        Vector_Shutdown( &found );
    }
    return st;
}

geometry_status_t MeshSelection_TryGatherVertices(
    const mesh_selection_t *pSelection,
    const mesh_source_t *pMesh,
    mesh_selection_mode_t mode,
    vector_t<geometry_mesh_vertex_handle_t> *pVerticesOut ) noexcept
{
    if ( !Ready( pSelection, pMesh ) || pVerticesOut == nullptr || pVerticesOut->pAllocator == nullptr ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    Vector_Clear( pVerticesOut );
    const editable_mesh_t *pM = &pMesh->mesh;
    auto push = [&]( geometry_mesh_vertex_handle_t h ) noexcept { return Vector_PushBack( pVerticesOut, h ) != 0; };
    auto vertex = [&]( geometry_source_id_t id, geometry_mesh_vertex_handle_t *pH ) noexcept {
        return MeshSource_TryFindVertex( pMesh, id, pH );
    };
    if ( mode == mesh_selection_mode_t::VERTEX ) {
        for ( usize i = 0u; i < pSelection->vertices.nCount; ++i ) {
            geometry_mesh_vertex_handle_t h{};
            if ( !vertex( pSelection->vertices.pData[i], &h ) ) { return geometry_status_t::INVALID_HANDLE; }
            if ( !push( h ) ) { return geometry_status_t::ALLOCATION_FAILED; }
        }
    } else if ( mode == mesh_selection_mode_t::EDGE ) {
        for ( usize i = 0u; i < pSelection->edges.nCount; ++i ) {
            geometry_mesh_vertex_handle_t a{}, b{};
            if ( !vertex( pSelection->edges.pData[i].a, &a ) || !vertex( pSelection->edges.pData[i].b, &b ) ) {
                return geometry_status_t::INVALID_HANDLE;
            }
            if ( !push( a ) || !push( b ) ) { return geometry_status_t::ALLOCATION_FAILED; }
        }
    } else {
        for ( usize i = 0u; i < pSelection->faces.nCount; ++i ) {
            geometry_mesh_face_handle_t hF{};
            if ( !MeshSource_TryFindFace( pMesh, pSelection->faces.pData[i], &hF ) ) { return geometry_status_t::INVALID_HANDLE; }
            const mesh_loop_record_t *pL = GenerationPool_Get( &pM->loops, GenerationPool_Get( &pM->faces, hF )->hOuterLoop );
            bool bOk = true;
            ForEachCorner( pMesh, *pL, [&]( geometry_mesh_vertex_handle_t h, math::vec3d_t ) noexcept { bOk = bOk && push( h ); } );
            if ( !bOk ) { return geometry_status_t::ALLOCATION_FAILED; }
        }
    }
    auto keyOf = []( geometry_mesh_vertex_handle_t h ) noexcept {
        return ( static_cast<u64>( h.nSlot ) << 32 ) | h.nGeneration;
    };
    std::sort( pVerticesOut->pData, pVerticesOut->pData + pVerticesOut->nCount,
               [&]( geometry_mesh_vertex_handle_t x, geometry_mesh_vertex_handle_t y ) { return keyOf( x ) < keyOf( y ); } );
    usize w = 0u;
    for ( usize i = 0u; i < pVerticesOut->nCount; ++i ) {
        if ( w == 0u || keyOf( pVerticesOut->pData[w - 1u] ) != keyOf( pVerticesOut->pData[i] ) ) {
            pVerticesOut->pData[w++] = pVerticesOut->pData[i];
        }
    }
    (void)Vector_Resize( pVerticesOut, w );
    return geometry_status_t::OK;
}

geometry_status_t MeshSelection_TryComputePivot(
    const mesh_selection_t *pSelection,
    const mesh_source_t *pMesh,
    mesh_selection_mode_t mode,
    mesh_pivot_mode_t pivotMode,
    math::vec3d_t *pPivotOut ) noexcept
{
    if ( !Ready( pSelection, pMesh ) || pPivotOut == nullptr ) { return geometry_status_t::NOT_INITIALIZED; }
    vector_t<geometry_mesh_vertex_handle_t> verts{};
    if ( !Vector_Init( &verts, AllocatorOf( pSelection ) ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    geometry_status_t st = MeshSelection_TryGatherVertices( pSelection, pMesh, mode, &verts );
    if ( st == geometry_status_t::OK && verts.nCount == 0u ) { st = geometry_status_t::INVALID_ARGUMENT; }
    if ( st == geometry_status_t::OK ) {
        math::vec3d_t sum{}, lo{}, hi{};
        for ( usize i = 0u; i < verts.nCount; ++i ) {
            const math::vec3d_t p = GenerationPool_Get( &pMesh->mesh.vertices, verts.pData[i] )->position;
            sum = math::Vec3d_Add( sum, p );
            lo = i == 0u ? p : math::Vec3d_Make( std::min( lo.x, p.x ), std::min( lo.y, p.y ), std::min( lo.z, p.z ) );
            hi = i == 0u ? p : math::Vec3d_Make( std::max( hi.x, p.x ), std::max( hi.y, p.y ), std::max( hi.z, p.z ) );
        }
        *pPivotOut = pivotMode == mesh_pivot_mode_t::CENTROID ? math::Vec3d_Scale( sum, 1.0 / static_cast<f64>( verts.nCount ) )
                                                             : math::Vec3d_Scale( math::Vec3d_Add( lo, hi ), 0.5 );
    }
    Vector_Shutdown( &verts );
    return st;
}

} // namespace cypher::editor::geometry
