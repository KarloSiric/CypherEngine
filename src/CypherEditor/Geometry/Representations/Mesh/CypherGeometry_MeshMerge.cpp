//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshMerge.cpp
//  Purpose: Implements vertex merging, edge sewing, and merge by distance.
//  Details: Sewing and merge-by-distance only work out the groups (with a
//           union-find, since pairs and near neighbours chain); the merge
//           itself is always MeshMerge_Vertices, so there is one place
//           where faces are rebuilt and folds are rejected.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshMerge.h"
#include "CypherGeometry_MeshBoundaryOps.h"
#include "CypherGeometry_MeshRecordAccess.h"

#include <algorithm>
#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;
using namespace mesh_detail;
using math::vec3d_t;

namespace
{

u32 FindRoot( vector_t<u32> *pParent, u32 i ) noexcept
{
    while ( pParent->pData[i] != i ) {
        pParent->pData[i] = pParent->pData[pParent->pData[i]]; // path halving
        i = pParent->pData[i];
    }
    return i;
}

void Unite( vector_t<u32> *pParent, u32 a, u32 b ) noexcept
{
    a = FindRoot( pParent, a );
    b = FindRoot( pParent, b );
    if ( a != b ) { pParent->pData[a < b ? b : a] = a < b ? a : b; } // lower index wins: deterministic roots
}

mesh_merge_result_t Failed( geometry_status_t s ) noexcept
{
    mesh_merge_result_t r{};
    r.status = s;
    return r;
}

} // namespace

mesh_merge_result_t MeshMerge_Vertices(
    editable_mesh_t *pMesh,
    span_t<const geometry_mesh_vertex_handle_t> groupVertices,
    span_t<const u32> groupSizes,
    mesh_merge_target_t target,
    vector_t<mesh_merge_face_t> *pFacesOut ) noexcept
{
    if ( !EditableMesh_IsInitialized( pMesh ) ) { return Failed( geometry_status_t::NOT_INITIALIZED ); }
    if ( groupVertices.pData == nullptr || groupSizes.pData == nullptr || groupSizes.nCount == 0u ||
         target > mesh_merge_target_t::FIRST || ( pFacesOut != nullptr && pFacesOut->pAllocator == nullptr ) ) {
        return Failed( geometry_status_t::INVALID_ARGUMENT );
    }
    usize cTotal = 0u;
    for ( usize g = 0u; g < groupSizes.nCount; ++g ) {
        if ( groupSizes.pData[g] < 2u ) { return Failed( geometry_status_t::INVALID_ARGUMENT ); }
        cTotal += groupSizes.pData[g];
    }
    if ( cTotal != groupVertices.nCount ) { return Failed( geometry_status_t::INVALID_ARGUMENT ); }

    const allocator_t *pA = pMesh->pAllocator;
    vector_t<u32> groupOf{};
    vector_t<geometry_mesh_vertex_handle_t> survivors{};
    vector_t<vec3d_t> targets{}, saved{};
    if ( !Vector_Init( &groupOf, pA ) || !Vector_Init( &survivors, pA ) || !Vector_Init( &targets, pA ) ||
         !Vector_Init( &saved, pA ) || !Vector_Resize( &groupOf, pMesh->vertices.cSlots ) ||
         !Vector_Reserve( &survivors, groupSizes.nCount ) || !Vector_Reserve( &targets, groupSizes.nCount ) ||
         !Vector_Reserve( &saved, groupSizes.nCount ) ) {
        return Failed( geometry_status_t::ALLOCATION_FAILED );
    }
    for ( usize i = 0u; i < groupOf.nCount; ++i ) { groupOf.pData[i] = CY_INVALID_INDEX; }
    usize iBase = 0u;
    for ( usize g = 0u; g < groupSizes.nCount; ++g ) {
        vec3d_t sum{};
        for ( u32 k = 0u; k < groupSizes.pData[g]; ++k ) {
            const geometry_mesh_vertex_handle_t h = groupVertices.pData[iBase + k];
            const mesh_vertex_record_t *pV = GenerationPool_Get( &pMesh->vertices, h );
            if ( pV == nullptr || groupOf.pData[h.nSlot] != CY_INVALID_INDEX ) { return Failed( geometry_status_t::INVALID_HANDLE ); }
            groupOf.pData[h.nSlot] = static_cast<u32>( g );
            sum = math::Vec3d_Add( sum, pV->position );
        }
        const geometry_mesh_vertex_handle_t first = groupVertices.pData[iBase];
        const vec3d_t p = target == mesh_merge_target_t::CENTER
                              ? math::Vec3d_Scale( sum, 1.0 / static_cast<f64>( groupSizes.pData[g] ) )
                              : GenerationPool_Get( &pMesh->vertices, first )->position;
        if ( !math::Vec3d_IsFinite( p ) ) { return Failed( geometry_status_t::NUMERIC_FAILURE ); }
        (void)Vector_PushBack( &survivors, first );
        (void)Vector_PushBack( &targets, p );
        iBase += groupSizes.pData[g];
    }

    // Every face with a corner at a merged vertex is rebuilt.
    vector_t<u8> faceMarked{};
    vector_t<geometry_mesh_face_handle_t> affected{};
    if ( !Vector_Init( &faceMarked, pA ) || !Vector_Init( &affected, pA ) || !Vector_Resize( &faceMarked, pMesh->faces.cSlots ) ) {
        return Failed( geometry_status_t::ALLOCATION_FAILED );
    }
    for ( usize i = 0u; i < faceMarked.nCount; ++i ) { faceMarked.pData[i] = 0u; }
    bool bOk = true;
    (void)GenerationPool_ForEach( &pMesh->halfEdges,
        [&]( geometry_mesh_half_edge_handle_t h, const mesh_half_edge_record_t &he ) noexcept -> bool_t {
            if ( groupOf.pData[he.hOrigin.nSlot] == CY_INVALID_INDEX ) { return true; }
            const geometry_mesh_face_handle_t hFace = FaceOf( pMesh, h );
            if ( GeometryHandle_IsValid( hFace ) && faceMarked.pData[hFace.nSlot] == 0u ) {
                faceMarked.pData[hFace.nSlot] = 1u;
                bOk = Vector_PushBack( &affected, hFace );
            }
            return bOk;
        } );
    if ( !bOk ) { return Failed( geometry_status_t::ALLOCATION_FAILED ); }

    // Rings with survivors substituted; corners that merged into their
    // neighbour drop out.
    vector_t<mesh_boundary_corner_t> corners{};
    vector_t<u32> sizes{};
    vector_t<geometry_mesh_face_handle_t> sources{};
    vector_t<geometry_mesh_vertex_handle_t> ring{};
    if ( !Vector_Init( &corners, pA ) || !Vector_Init( &sizes, pA ) || !Vector_Init( &sources, pA ) || !Vector_Init( &ring, pA ) ) {
        return Failed( geometry_status_t::ALLOCATION_FAILED );
    }
    u32 cCollapsed = 0u;
    for ( usize f = 0u; f < affected.nCount; ++f ) {
        const mesh_face_record_t *pF = GenerationPool_Get( &pMesh->faces, affected.pData[f] );
        const mesh_loop_record_t *pL = GenerationPool_Get( &pMesh->loops, pF->hOuterLoop );
        Vector_Clear( &ring );
        geometry_mesh_half_edge_handle_t h = pL->hFirstHalfEdge;
        for ( u32 k = 0u; k < pL->cHalfEdges; ++k ) {
            const mesh_half_edge_record_t *pH = He( pMesh, h );
            const u32 g = groupOf.pData[pH->hOrigin.nSlot];
            const geometry_mesh_vertex_handle_t v = g == CY_INVALID_INDEX ? pH->hOrigin : survivors.pData[g];
            if ( ( ring.nCount == 0u || !Same( ring.pData[ring.nCount - 1u], v ) ) && !Vector_PushBack( &ring, v ) ) {
                return Failed( geometry_status_t::ALLOCATION_FAILED );
            }
            h = pH->hNext;
        }
        while ( ring.nCount > 1u && Same( ring.pData[ring.nCount - 1u], ring.pData[0] ) ) { Vector_PopBack( &ring ); }
        if ( ring.nCount < 3u ) {
            ++cCollapsed;
            continue;
        }
        // A vertex twice in one face would pinch it into a figure eight.
        for ( usize i = 0u; i < ring.nCount; ++i ) {
            for ( usize j = i + 1u; j < ring.nCount; ++j ) {
                if ( Same( ring.pData[i], ring.pData[j] ) ) { return Failed( geometry_status_t::NON_MANIFOLD ); }
            }
        }
        for ( usize i = 0u; i < ring.nCount; ++i ) {
            mesh_boundary_corner_t c{};
            c.hVertex = ring.pData[i];
            if ( !Vector_PushBack( &corners, c ) ) { return Failed( geometry_status_t::ALLOCATION_FAILED ); }
        }
        if ( !Vector_PushBack( &sizes, static_cast<u32>( ring.nCount ) ) || !Vector_PushBack( &sources, affected.pData[f] ) ) {
            return Failed( geometry_status_t::ALLOCATION_FAILED );
        }
    }

    // Open edges that survive the merge (their ends stay distinct); each
    // stitch closes two of them, which is how cEdgesJoined is counted.
    usize cOpenBefore = 0u;
    (void)GenerationPool_ForEach( &pMesh->halfEdges,
        [&]( geometry_mesh_half_edge_handle_t, const mesh_half_edge_record_t &he ) noexcept -> bool_t {
            if ( !IsBoundaryHe( pMesh, he ) ) { return true; }
            auto mapped = [&]( geometry_mesh_vertex_handle_t v ) noexcept {
                const u32 g = groupOf.pData[v.nSlot];
                return g == CY_INVALID_INDEX ? v : survivors.pData[g];
            };
            cOpenBefore += Same( mapped( he.hOrigin ), mapped( DestOf( pMesh, he ) ) ) ? 0u : 1u;
            return true;
        } );

    vector_t<geometry_mesh_face_handle_t> created{};
    if ( !Vector_Init( &created, pA ) || !Vector_Reserve( &created, sizes.nCount ) ||
         ( pFacesOut != nullptr && !Vector_Reserve( pFacesOut, pFacesOut->nCount + sizes.nCount ) ) ) {
        return Failed( geometry_status_t::ALLOCATION_FAILED );
    }
    // Survivors take their targets first, so the rebuild checks the final
    // shape; they move back if it is rejected.
    for ( usize g = 0u; g < survivors.nCount; ++g ) {
        vec3d_t &p = GenerationPool_Get( &pMesh->vertices, survivors.pData[g] )->position;
        (void)Vector_PushBack( &saved, p );
        p = targets.pData[g];
    }
    const mesh_boundary_replace_result_t rr = MeshBoundary_ReplaceFaces(
        pMesh, span_t<const geometry_mesh_face_handle_t>{ affected.pData, affected.nCount },
        span_t<const mesh_boundary_corner_t>{ corners.pData, corners.nCount }, span_t<const u32>{ sizes.pData, sizes.nCount }, {},
        &created );
    if ( rr.status != geometry_status_t::OK ) {
        for ( usize g = 0u; g < survivors.nCount; ++g ) {
            GenerationPool_Get( &pMesh->vertices, survivors.pData[g] )->position = saved.pData[g];
        }
        return Failed( rr.status );
    }
    if ( pFacesOut != nullptr ) {
        for ( usize i = 0u; i < created.nCount; ++i ) {
            (void)Vector_PushBack( pFacesOut, mesh_merge_face_t{ created.pData[i], sources.pData[i] } );
        }
    }
    mesh_merge_result_t r{};
    r.cVerticesMerged = static_cast<u32>( cTotal - groupSizes.nCount );
    r.cFacesRebuilt = static_cast<u32>( created.nCount );
    r.cFacesCollapsed = cCollapsed;
    const usize cOpenAfter = MeshBoundary_CountBoundaryEdges( pMesh );
    r.cEdgesJoined = cOpenBefore > cOpenAfter ? static_cast<u32>( ( cOpenBefore - cOpenAfter ) / 2u ) : 0u;
    r.status = geometry_status_t::OK;
    return r;
}

mesh_merge_result_t MeshMerge_SewEdges(
    editable_mesh_t *pMesh,
    span_t<const geometry_mesh_edge_handle_t> edges,
    mesh_merge_target_t target,
    vector_t<mesh_merge_face_t> *pFacesOut ) noexcept
{
    if ( !EditableMesh_IsInitialized( pMesh ) ) { return Failed( geometry_status_t::NOT_INITIALIZED ); }
    if ( edges.pData == nullptr || edges.nCount == 0u || ( edges.nCount % 2u ) != 0u ) {
        return Failed( geometry_status_t::INVALID_ARGUMENT );
    }
    const allocator_t *pA = pMesh->pAllocator;
    // Union-find over vertex slots; `order` lists the involved vertices,
    // first edges' ends first, so each group's first entry is a survivor
    // from a first edge.
    vector_t<u32> parent{};
    vector_t<geometry_mesh_vertex_handle_t> order{};
    vector_t<u8> edgeSeen{};
    if ( !Vector_Init( &parent, pA ) || !Vector_Init( &order, pA ) || !Vector_Init( &edgeSeen, pA ) ||
         !Vector_Resize( &parent, pMesh->vertices.cSlots ) || !Vector_Reserve( &order, 2u * edges.nCount ) ||
         !Vector_Resize( &edgeSeen, pMesh->edges.cSlots ) ) {
        return Failed( geometry_status_t::ALLOCATION_FAILED );
    }
    for ( usize i = 0u; i < parent.nCount; ++i ) { parent.pData[i] = static_cast<u32>( i ); }
    for ( usize i = 0u; i < edgeSeen.nCount; ++i ) { edgeSeen.pData[i] = 0u; }
    auto ends = [&]( geometry_mesh_edge_handle_t hEdge, geometry_mesh_vertex_handle_t *pFrom,
                     geometry_mesh_vertex_handle_t *pTo ) noexcept -> geometry_status_t {
        const mesh_edge_record_t *pE = GenerationPool_Get( &pMesh->edges, hEdge );
        const mesh_half_edge_record_t *pH = pE ? He( pMesh, pE->hHalfEdge ) : nullptr;
        if ( pH == nullptr ) { return geometry_status_t::INVALID_HANDLE; }
        if ( !IsBoundaryHe( pMesh, *pH ) || edgeSeen.pData[hEdge.nSlot] != 0u ) { return geometry_status_t::INVALID_ARGUMENT; }
        edgeSeen.pData[hEdge.nSlot] = 1u;
        *pFrom = pH->hOrigin;
        *pTo = DestOf( pMesh, *pH );
        return geometry_status_t::OK;
    };
    for ( usize i = 0u; i < edges.nCount; i += 2u ) {
        geometry_mesh_vertex_handle_t a{}, b{}, c{}, d{};
        geometry_status_t s = ends( edges.pData[i], &a, &b );
        if ( s == geometry_status_t::OK ) { s = ends( edges.pData[i + 1u], &c, &d ); }
        if ( s != geometry_status_t::OK ) { return Failed( s ); }
        // a -> b runs against c -> d once joined: a meets d, b meets c. If
        // the other pairing brings closer ends together, the edges run the
        // same way - their faces face opposite ways - and joining them
        // would twist the surface; the caller should flip one side first.
        auto gap = [&]( geometry_mesh_vertex_handle_t p, geometry_mesh_vertex_handle_t q ) noexcept {
            return std::sqrt( math::Vec3d_LengthSquared( math::Vec3d_Subtract( GenerationPool_Get( &pMesh->vertices, p )->position,
                                                                               GenerationPool_Get( &pMesh->vertices, q )->position ) ) );
        };
        if ( gap( a, c ) + gap( b, d ) < gap( a, d ) + gap( b, c ) ) { return Failed( geometry_status_t::NON_MANIFOLD ); }
        Unite( &parent, a.nSlot, d.nSlot );
        Unite( &parent, b.nSlot, c.nSlot );
        (void)Vector_PushBack( &order, a );
        (void)Vector_PushBack( &order, b );
    }
    for ( usize i = 1u; i < edges.nCount; i += 2u ) {
        geometry_mesh_vertex_handle_t c{}, d{};
        const mesh_half_edge_record_t *pH = He( pMesh, GenerationPool_Get( &pMesh->edges, edges.pData[i] )->hHalfEdge );
        c = pH->hOrigin;
        d = DestOf( pMesh, *pH );
        if ( !Vector_PushBack( &order, c ) || !Vector_PushBack( &order, d ) ) { return Failed( geometry_status_t::ALLOCATION_FAILED ); }
    }

    // Groups: order entries sorted by root, keeping appearance order within
    // a root (so a first edge's vertex leads), each vertex once.
    struct member_t {
        u32 root;
        u32 iOrder;
    };
    vector_t<member_t> members{};
    vector_t<u8> taken{};
    vector_t<geometry_mesh_vertex_handle_t> flat{};
    vector_t<u32> sizes{};
    if ( !Vector_Init( &members, pA ) || !Vector_Init( &taken, pA ) || !Vector_Init( &flat, pA ) || !Vector_Init( &sizes, pA ) ||
         !Vector_Resize( &members, order.nCount ) || !Vector_Resize( &taken, pMesh->vertices.cSlots ) ) {
        return Failed( geometry_status_t::ALLOCATION_FAILED );
    }
    for ( usize i = 0u; i < taken.nCount; ++i ) { taken.pData[i] = 0u; }
    for ( usize i = 0u; i < order.nCount; ++i ) {
        members.pData[i] = member_t{ FindRoot( &parent, order.pData[i].nSlot ), static_cast<u32>( i ) };
    }
    std::sort( members.pData, members.pData + members.nCount, []( const member_t &a, const member_t &b ) noexcept {
        return a.root != b.root ? a.root < b.root : a.iOrder < b.iOrder;
    } );
    for ( usize i = 0u; i < members.nCount; ) {
        usize j = i;
        const usize iGroupStart = flat.nCount;
        for ( ; j < members.nCount && members.pData[j].root == members.pData[i].root; ++j ) {
            const geometry_mesh_vertex_handle_t v = order.pData[members.pData[j].iOrder];
            if ( taken.pData[v.nSlot] != 0u ) { continue; }
            taken.pData[v.nSlot] = 1u;
            if ( !Vector_PushBack( &flat, v ) ) { return Failed( geometry_status_t::ALLOCATION_FAILED ); }
        }
        const usize cGroup = flat.nCount - iGroupStart;
        if ( cGroup < 2u ) {
            (void)Vector_Resize( &flat, iGroupStart ); // already shared (zipping from a common end)
        } else if ( !Vector_PushBack( &sizes, static_cast<u32>( cGroup ) ) ) {
            return Failed( geometry_status_t::ALLOCATION_FAILED );
        }
        i = j;
    }
    if ( sizes.nCount == 0u ) { return Failed( geometry_status_t::INVALID_ARGUMENT ); } // every pair already joined
    return MeshMerge_Vertices( pMesh, span_t<const geometry_mesh_vertex_handle_t>{ flat.pData, flat.nCount },
                               span_t<const u32>{ sizes.pData, sizes.nCount }, target, pFacesOut );
}

mesh_merge_result_t MeshMerge_ByDistance(
    editable_mesh_t *pMesh,
    f64 tolerance,
    vector_t<mesh_merge_face_t> *pFacesOut ) noexcept
{
    if ( !EditableMesh_IsInitialized( pMesh ) ) { return Failed( geometry_status_t::NOT_INITIALIZED ); }
    if ( !std::isfinite( tolerance ) ) { return Failed( geometry_status_t::NUMERIC_FAILURE ); }
    if ( tolerance < 0.0 ) { return Failed( geometry_status_t::INVALID_ARGUMENT ); }
    const allocator_t *pA = pMesh->pAllocator;

    // Bucket vertices in a grid whose cells are at least `tolerance` wide,
    // so any two within tolerance sit in the same or adjacent cells. The
    // cell is widened for huge coordinates so indices stay far inside i64
    // (a wider cell only costs comparisons).
    struct entry_t {
        i64 cx, cy, cz;
        u32 i;
    };
    vector_t<geometry_mesh_vertex_handle_t> verts{};
    vector_t<vec3d_t> pos{};
    vector_t<entry_t> grid{};
    vector_t<u32> parent{};
    const usize cVerts = GenerationPool_Count( &pMesh->vertices );
    if ( !Vector_Init( &verts, pA ) || !Vector_Init( &pos, pA ) || !Vector_Init( &grid, pA ) || !Vector_Init( &parent, pA ) ||
         !Vector_Reserve( &verts, cVerts ) || !Vector_Reserve( &pos, cVerts ) || !Vector_Reserve( &grid, cVerts ) ||
         !Vector_Resize( &parent, cVerts ) ) {
        return Failed( geometry_status_t::ALLOCATION_FAILED );
    }
    f64 maxAbs = 0.0;
    (void)GenerationPool_ForEach( &pMesh->vertices,
        [&]( geometry_mesh_vertex_handle_t h, const mesh_vertex_record_t &v ) noexcept -> bool_t {
            (void)Vector_PushBack( &verts, h );
            (void)Vector_PushBack( &pos, v.position );
            maxAbs = std::max( { maxAbs, std::fabs( v.position.x ), std::fabs( v.position.y ), std::fabs( v.position.z ) } );
            return true;
        } );
    if ( !std::isfinite( maxAbs ) ) { return Failed( geometry_status_t::NUMERIC_FAILURE ); }
    const f64 cell = std::max( { tolerance, maxAbs * 0x1p-40, 0x1p-900 } );
    auto cellOf = [&]( f64 x ) noexcept { return static_cast<i64>( std::floor( x / cell ) ); };
    for ( usize i = 0u; i < verts.nCount; ++i ) {
        parent.pData[i] = static_cast<u32>( i );
        (void)Vector_PushBack( &grid, entry_t{ cellOf( pos.pData[i].x ), cellOf( pos.pData[i].y ), cellOf( pos.pData[i].z ),
                                               static_cast<u32>( i ) } );
    }
    auto cellLess = []( const entry_t &a, const entry_t &b ) noexcept {
        if ( a.cx != b.cx ) { return a.cx < b.cx; }
        if ( a.cy != b.cy ) { return a.cy < b.cy; }
        return a.cz < b.cz;
    };
    std::sort( grid.pData, grid.pData + grid.nCount, cellLess );
    const f64 tol2 = tolerance * tolerance;
    for ( usize n = 0u; n < grid.nCount; ++n ) {
        const entry_t &e = grid.pData[n];
        for ( i64 dx = -1; dx <= 1; ++dx ) {
            for ( i64 dy = -1; dy <= 1; ++dy ) {
                for ( i64 dz = -1; dz <= 1; ++dz ) {
                    const entry_t probe{ e.cx + dx, e.cy + dy, e.cz + dz, 0u };
                    const entry_t *it = std::lower_bound( grid.pData, grid.pData + grid.nCount, probe, cellLess );
                    for ( ; it != grid.pData + grid.nCount && !cellLess( probe, *it ); ++it ) {
                        if ( it->i <= e.i ) { continue; }
                        const vec3d_t delta = math::Vec3d_Subtract( pos.pData[it->i], pos.pData[e.i] );
                        if ( math::Vec3d_LengthSquared( delta ) <= tol2 ) { Unite( &parent, e.i, it->i ); }
                    }
                }
            }
        }
    }

    // Groups: members in handle order, so the survivor is the lowest handle.
    vector_t<u32> byRoot{}, root{};
    vector_t<geometry_mesh_vertex_handle_t> flat{};
    vector_t<u32> sizes{};
    if ( !Vector_Init( &byRoot, pA ) || !Vector_Init( &root, pA ) || !Vector_Init( &flat, pA ) || !Vector_Init( &sizes, pA ) ||
         !Vector_Resize( &byRoot, verts.nCount ) || !Vector_Resize( &root, verts.nCount ) ) {
        return Failed( geometry_status_t::ALLOCATION_FAILED );
    }
    for ( usize i = 0u; i < verts.nCount; ++i ) {
        byRoot.pData[i] = static_cast<u32>( i );
        root.pData[i] = FindRoot( &parent, static_cast<u32>( i ) );
    }
    std::sort( byRoot.pData, byRoot.pData + byRoot.nCount, [&]( u32 a, u32 b ) noexcept {
        return root.pData[a] != root.pData[b] ? root.pData[a] < root.pData[b] : Key( verts.pData[a] ) < Key( verts.pData[b] );
    } );
    for ( usize i = 0u; i < byRoot.nCount; ) {
        usize j = i + 1u;
        while ( j < byRoot.nCount && root.pData[byRoot.pData[j]] == root.pData[byRoot.pData[i]] ) { ++j; }
        if ( j - i >= 2u ) {
            for ( usize k = i; k < j; ++k ) {
                if ( !Vector_PushBack( &flat, verts.pData[byRoot.pData[k]] ) ) { return Failed( geometry_status_t::ALLOCATION_FAILED ); }
            }
            if ( !Vector_PushBack( &sizes, static_cast<u32>( j - i ) ) ) { return Failed( geometry_status_t::ALLOCATION_FAILED ); }
        }
        i = j;
    }
    if ( sizes.nCount == 0u ) {
        mesh_merge_result_t r{};
        r.status = geometry_status_t::OK; // nothing within tolerance
        return r;
    }
    return MeshMerge_Vertices( pMesh, span_t<const geometry_mesh_vertex_handle_t>{ flat.pData, flat.nCount },
                               span_t<const u32>{ sizes.pData, sizes.nCount }, mesh_merge_target_t::FIRST, pFacesOut );
}

} // namespace cypher::editor::geometry
