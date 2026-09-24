//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshBoundaryOps.cpp
//  Purpose: Implements delete-faces, fill-hole, extrude-boundary-edges, and
//           detach-faces with exact preflight and allocation-free mutation.
//  Details: Each operation has three phases:
//             1. preflight - validate, compute every new record, simulate
//                the post-edit shell components on a virtual graph, and
//                reserve pools and scratch (all fallible work);
//             2. mutation  - create and rewire records inside the reserved
//                capacity (cannot fail);
//             3. shells    - recompute connected components into the
//                reserved shell capacity (cannot fail).
//           Scratch vectors are sized in phase 1 to their phase-3 maximum,
//           so Vector_Resize/PushBack never allocate after the first write.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshBoundaryOps.h"
#include "CypherGeometry_MeshRecordAccess.h"

#include <algorithm>
#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;
using namespace mesh_detail;

namespace
{

constexpr u32 kUnlabelled = CY_INVALID_INDEX;

// ---------------------------------------------------------------------------
// Shell components
// ---------------------------------------------------------------------------

// Scratch for component labelling, sized once in preflight.
struct shell_scratch_t {
    vector_t<u32> labels{};                              // by face slot
    vector_t<geometry_mesh_face_handle_t> queue{};
    vector_t<geometry_mesh_shell_handle_t> oldShells{};
};

void ShutdownScratch( shell_scratch_t *pS ) noexcept
{
    Vector_Shutdown( &pS->labels );
    Vector_Shutdown( &pS->queue );
    Vector_Shutdown( &pS->oldShells );
}

// Sizes the scratch for a mesh that will have at most cFaceSlotsAfter face
// slots and cFacesAfter faces.
bool PrepareScratch( shell_scratch_t *pS, const editable_mesh_t *pMesh, usize cFaceSlotsAfter, usize cFacesAfter ) noexcept
{
    return Vector_Init( &pS->labels, pMesh->pAllocator ) && Vector_Reserve( &pS->labels, cFaceSlotsAfter ) &&
           Vector_Init( &pS->queue, pMesh->pAllocator ) && Vector_Reserve( &pS->queue, cFacesAfter ) &&
           Vector_Init( &pS->oldShells, pMesh->pAllocator ) &&
           Vector_Reserve( &pS->oldShells, GenerationPool_Count( &pMesh->shells ) );
}

// RebuildShells first stores the old shells and then reuses the same vector
// for the replacement shells. A cut can create more components than existed
// before the edit, so reserve that exact post-edit count while failure is
// still harmless.
bool PrepareRebuildShells( shell_scratch_t *pS, usize cShellsAfter ) noexcept
{
    return Vector_Reserve( &pS->oldShells, cShellsAfter );
}

// Counts components of the face graph where faces with skip(face) are
// ignored and crossing half-edge h is forbidden when cut(h). Uses the
// scratch (capacity must already suffice). Deterministic: seeds in pool
// order, breadth-first.
template <typename skip_t, typename cut_t>
u32 LabelComponents( const editable_mesh_t *pMesh, shell_scratch_t *pS, skip_t &&skip, cut_t &&cut ) noexcept
{
    (void)Vector_Resize( &pS->labels, pMesh->faces.cSlots );
    for ( usize i = 0u; i < pS->labels.nCount; ++i ) { pS->labels.pData[i] = kUnlabelled; }
    u32 cComponents = 0u;
    (void)GenerationPool_ForEach( &pMesh->faces,
        [&]( geometry_mesh_face_handle_t hSeed, const mesh_face_record_t & ) noexcept -> bool_t {
            if ( skip( hSeed ) || pS->labels.pData[hSeed.nSlot] != kUnlabelled ) { return true; }
            const u32 label = cComponents++;
            Vector_Clear( &pS->queue );
            (void)Vector_PushBack( &pS->queue, hSeed );
            pS->labels.pData[hSeed.nSlot] = label;
            for ( usize q = 0u; q < pS->queue.nCount; ++q ) {
                const mesh_face_record_t *pF = GenerationPool_Get( &pMesh->faces, pS->queue.pData[q] );
                const mesh_loop_record_t *pL = pF ? GenerationPool_Get( &pMesh->loops, pF->hOuterLoop ) : nullptr;
                if ( pL == nullptr ) { continue; }
                geometry_mesh_half_edge_handle_t h = pL->hFirstHalfEdge;
                for ( u32 k = 0u; k < pL->cHalfEdges; ++k ) {
                    const mesh_half_edge_record_t *pH = He( pMesh, h );
                    if ( pH == nullptr ) { break; }
                    if ( !cut( h ) && !IsBoundaryHe( pMesh, *pH ) ) {
                        const geometry_mesh_face_handle_t hN = FaceOf( pMesh, pH->hTwin );
                        if ( GeometryHandle_IsValid( hN ) && !skip( hN ) && pS->labels.pData[hN.nSlot] == kUnlabelled ) {
                            pS->labels.pData[hN.nSlot] = label;
                            (void)Vector_PushBack( &pS->queue, hN );
                        }
                    }
                    h = pH->hNext;
                }
            }
            return true;
        } );
    return cComponents;
}

// Replaces every shell record by one per current component. Needs the
// scratch sized for the current mesh and the shell pool reserved for the
// component count.
void RebuildShells( editable_mesh_t *pMesh, shell_scratch_t *pS ) noexcept
{
    Vector_Clear( &pS->oldShells );
    (void)GenerationPool_ForEach( &pMesh->shells,
        [&]( geometry_mesh_shell_handle_t h, const mesh_shell_record_t & ) noexcept -> bool_t {
            (void)Vector_PushBack( &pS->oldShells, h );
            return true;
        } );
    for ( usize i = 0u; i < pS->oldShells.nCount; ++i ) { (void)GenerationPool_Remove( &pMesh->shells, pS->oldShells.pData[i] ); }

    const u32 cComponents = LabelComponents(
        pMesh, pS, []( geometry_mesh_face_handle_t ) noexcept { return false; },
        []( geometry_mesh_half_edge_handle_t ) noexcept { return false; } );
    // Shell k is created when its first face (pool order) is met, so shell
    // creation order is deterministic too.
    Vector_Clear( &pS->oldShells );
    for ( u32 k = 0u; k < cComponents; ++k ) {
        const auto ins = GenerationPool_Insert( &pMesh->shells, mesh_shell_record_t{} );
        (void)Vector_PushBack( &pS->oldShells, ins.handle );
    }
    (void)GenerationPool_ForEach( &pMesh->faces,
        [&]( geometry_mesh_face_handle_t hF, mesh_face_record_t &f ) noexcept -> bool_t {
            const u32 label = pS->labels.pData[hF.nSlot];
            const geometry_mesh_shell_handle_t hS = pS->oldShells.pData[label];
            f.hShell = hS;
            mesh_shell_record_t *pShell = GenerationPool_Get( &pMesh->shells, hS );
            if ( pShell->cFaces == 0u ) { pShell->hAnyFace = hF; }
            ++pShell->cFaces;
            return true;
        } );
}

geometry_status_t ValidateFaceList(
    const editable_mesh_t *pMesh,
    span_t<const geometry_mesh_face_handle_t> faces,
    vector_t<u8> *pMask ) noexcept
{
    if ( !Vector_Resize( pMask, pMesh->faces.cSlots ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    for ( usize i = 0u; i < pMask->nCount; ++i ) { pMask->pData[i] = 0u; }
    for ( usize i = 0u; i < faces.nCount; ++i ) {
        const geometry_mesh_face_handle_t h = faces.pData[i];
        if ( !GenerationPool_Contains( &pMesh->faces, h ) || pMask->pData[h.nSlot] != 0u ) {
            return geometry_status_t::INVALID_HANDLE;
        }
        pMask->pData[h.nSlot] = 1u;
    }
    return geometry_status_t::OK;
}

} // namespace

bool MeshBoundary_IsBoundaryEdge( const editable_mesh_t *pMesh, geometry_mesh_edge_handle_t hEdge ) noexcept
{
    if ( !EditableMesh_IsInitialized( pMesh ) ) { return false; }
    const mesh_edge_record_t *pE = GenerationPool_Get( &pMesh->edges, hEdge );
    const mesh_half_edge_record_t *pH = pE ? He( pMesh, pE->hHalfEdge ) : nullptr;
    return pH != nullptr && IsBoundaryHe( pMesh, *pH );
}

usize MeshBoundary_CountBoundaryEdges( const editable_mesh_t *pMesh ) noexcept
{
    if ( !EditableMesh_IsInitialized( pMesh ) ) { return 0u; }
    usize c = 0u;
    (void)GenerationPool_ForEach( &pMesh->halfEdges,
        [&]( geometry_mesh_half_edge_handle_t, const mesh_half_edge_record_t &h ) noexcept -> bool_t {
            c += IsBoundaryHe( pMesh, h ) ? 1u : 0u;
            return true;
        } );
    return c;
}

// ---------------------------------------------------------------------------
// Delete faces
// ---------------------------------------------------------------------------

mesh_boundary_delete_result_t MeshBoundary_DeleteFaces(
    editable_mesh_t *pMesh,
    span_t<const geometry_mesh_face_handle_t> faces ) noexcept
{
    // Deleting is replacing with nothing: one implementation of removal,
    // edge/vertex fates, and the one-fan-per-vertex check.
    mesh_boundary_delete_result_t r{};
    const mesh_boundary_replace_result_t rr = MeshBoundary_ReplaceFaces( pMesh, faces, {}, {}, {}, nullptr );
    r.status = rr.status;
    if ( rr.status == geometry_status_t::OK ) {
        r.cFacesRemoved = rr.cFacesRemoved;
        r.cEdgesRemoved = rr.cEdgesRemoved;
        r.cVerticesRemoved = rr.cVerticesRemoved;
    }
    return r;
}

// ---------------------------------------------------------------------------
// Fill hole
// ---------------------------------------------------------------------------

mesh_boundary_fill_result_t MeshBoundary_FillHole( editable_mesh_t *pMesh, geometry_mesh_edge_handle_t hEdge ) noexcept
{
    mesh_boundary_fill_result_t r{};
    if ( !EditableMesh_IsInitialized( pMesh ) ) {
        r.status = geometry_status_t::NOT_INITIALIZED;
        return r;
    }
    const mesh_edge_record_t *pE = GenerationPool_Get( &pMesh->edges, hEdge );
    const mesh_half_edge_record_t *pStart = pE ? He( pMesh, pE->hHalfEdge ) : nullptr;
    if ( pStart == nullptr ) {
        r.status = geometry_status_t::INVALID_HANDLE;
        return r;
    }
    if ( !IsBoundaryHe( pMesh, *pStart ) ) {
        r.status = geometry_status_t::INVALID_ARGUMENT;
        return r;
    }

    // Incoming boundary half-edges keyed by destination vertex.
    struct incoming_t {
        u64 destKey;
        geometry_mesh_half_edge_handle_t h;
    };
    const allocator_t *pA = pMesh->pAllocator;
    vector_t<incoming_t> incoming{};
    vector_t<geometry_mesh_half_edge_handle_t> ring{};
    vector_t<math::vec3d_t> points{};
    shell_scratch_t scratch{};
    auto cleanup = [&]() noexcept {
        Vector_Shutdown( &incoming );
        Vector_Shutdown( &ring );
        Vector_Shutdown( &points );
        ShutdownScratch( &scratch );
    };
    bool bOk = Vector_Init( &incoming, pA ) && Vector_Init( &ring, pA ) && Vector_Init( &points, pA );
    (void)GenerationPool_ForEach( &pMesh->halfEdges,
        [&]( geometry_mesh_half_edge_handle_t h, const mesh_half_edge_record_t &he ) noexcept -> bool_t {
            if ( bOk && IsBoundaryHe( pMesh, he ) ) {
                bOk = Vector_PushBack( &incoming, incoming_t{ Key( DestOf( pMesh, he ) ), h } );
            }
            return bOk;
        } );
    if ( !bOk ) {
        cleanup();
        r.status = geometry_status_t::ALLOCATION_FAILED;
        return r;
    }
    std::sort( incoming.pData, incoming.pData + incoming.nCount,
               []( const incoming_t &a, const incoming_t &b ) { return a.destKey < b.destKey; } );

    // Walk: the next boundary half-edge around the hole ends where the
    // current one starts.
    geometry_mesh_half_edge_handle_t cur = pE->hHalfEdge;
    for ( ;; ) {
        if ( ring.nCount >= kMeshBoundaryLoopMax ) {
            cleanup();
            r.status = geometry_status_t::LIMIT_EXCEEDED;
            return r;
        }
        if ( !Vector_PushBack( &ring, cur ) ) {
            cleanup();
            r.status = geometry_status_t::ALLOCATION_FAILED;
            return r;
        }
        const u64 want = Key( He( pMesh, cur )->hOrigin );
        const incoming_t *pBegin = incoming.pData, *pEnd = pBegin + incoming.nCount;
        const incoming_t *lo = std::lower_bound( pBegin, pEnd, want, []( const incoming_t &x, u64 k ) { return x.destKey < k; } );
        const incoming_t *hi = std::upper_bound( pBegin, pEnd, want, []( u64 k, const incoming_t &x ) { return k < x.destKey; } );
        if ( hi - lo != 1 ) {
            cleanup();
            r.status = geometry_status_t::NON_MANIFOLD; // pinched or dangling boundary vertex
            return r;
        }
        cur = lo->h;
        if ( Same( cur, pE->hHalfEdge ) ) { break; }
    }
    const u32 n = static_cast<u32>( ring.nCount );
    if ( n < 3u ) {
        cleanup();
        r.status = geometry_status_t::DEGENERATE;
        return r;
    }
    // New face corner i starts at the destination of ring[i].
    if ( !Vector_Resize( &points, n ) ) {
        cleanup();
        r.status = geometry_status_t::ALLOCATION_FAILED;
        return r;
    }
    for ( u32 i = 0u; i < n; ++i ) {
        points.pData[i] = GenerationPool_Get( &pMesh->vertices, DestOf( pMesh, *He( pMesh, ring.pData[i] ) ) )->position;
    }
    const math::vec3d_t normal = NewellOf( points.pData, n );
    if ( !FaceAreaDescribable( normal ) ) {
        cleanup();
        r.status = geometry_status_t::DEGENERATE;
        return r;
    }

    // Reservations. The new face touches existing components only, so the
    // component count cannot grow.
    const usize cShellsBefore = GenerationPool_Count( &pMesh->shells );
    geometry_status_t st = ReserveMore( &pMesh->halfEdges, n );
    if ( st == geometry_status_t::OK ) { st = ReserveMore( &pMesh->loops, 1u ); }
    if ( st == geometry_status_t::OK ) { st = ReserveMore( &pMesh->faces, 1u ); }
    if ( st == geometry_status_t::OK ) { st = ReserveForRebuild( &pMesh->shells, cShellsBefore ); } // filling can only merge
    if ( st == geometry_status_t::OK &&
         !PrepareScratch( &scratch, pMesh, pMesh->faces.cSlots, GenerationPool_Count( &pMesh->faces ) + 1u ) ) {
        st = geometry_status_t::ALLOCATION_FAILED;
    }
    if ( st != geometry_status_t::OK ) {
        cleanup();
        r.status = st;
        return r;
    }

    // ---- Mutation ----
    const geometry_mesh_face_handle_t hFace = GenerationPool_Insert( &pMesh->faces, mesh_face_record_t{} ).handle;
    const geometry_mesh_loop_handle_t hLoop = GenerationPool_Insert( &pMesh->loops, mesh_loop_record_t{} ).handle;
    geometry_mesh_half_edge_handle_t created[kMeshBoundaryLoopMax];
    for ( u32 i = 0u; i < n; ++i ) {
        created[i] = GenerationPool_Insert( &pMesh->halfEdges, mesh_half_edge_record_t{} ).handle;
    }
    for ( u32 i = 0u; i < n; ++i ) {
        mesh_half_edge_record_t *pB = HeMut( pMesh, ring.pData[i] );
        mesh_half_edge_record_t *pT = HeMut( pMesh, created[i] );
        pT->hOrigin = DestOf( pMesh, *pB );
        pT->hTwin = ring.pData[i];
        pT->hNext = created[( i + 1u ) % n];
        pT->hPrev = created[( i + n - 1u ) % n];
        pT->hEdge = pB->hEdge;
        pT->hLoop = hLoop;
        pB->hTwin = created[i];
    }
    mesh_loop_record_t *pLoop = GenerationPool_Get( &pMesh->loops, hLoop );
    pLoop->hFirstHalfEdge = created[0];
    pLoop->hFace = hFace;
    pLoop->cHalfEdges = n;
    mesh_face_record_t *pFace = GenerationPool_Get( &pMesh->faces, hFace );
    pFace->hOuterLoop = hLoop;
    pFace->normal = UnitOrZero( normal );
    pFace->iSourceSide = CY_INVALID_INDEX;
    RebuildShells( pMesh, &scratch );

    r.hFace = hFace;
    r.cSides = n;
    r.status = geometry_status_t::OK;
    cleanup();
    return r;
}

// ---------------------------------------------------------------------------
// Extrude boundary edges
// ---------------------------------------------------------------------------

mesh_boundary_extrude_result_t MeshBoundary_ExtrudeEdges(
    editable_mesh_t *pMesh,
    span_t<const geometry_mesh_edge_handle_t> edges,
    math::vec3d_t offset,
    vector_t<geometry_mesh_edge_handle_t> *pOuterEdgesOut ) noexcept
{
    mesh_boundary_extrude_result_t r{};
    if ( !EditableMesh_IsInitialized( pMesh ) ) {
        r.status = geometry_status_t::NOT_INITIALIZED;
        return r;
    }
    if ( edges.nCount == 0u || edges.pData == nullptr ||
         ( pOuterEdgesOut != nullptr && pOuterEdgesOut->pAllocator == nullptr ) ) {
        r.status = geometry_status_t::INVALID_ARGUMENT;
        return r;
    }
    if ( !math::Vec3d_IsFinite( offset ) ) {
        r.status = geometry_status_t::NUMERIC_FAILURE;
        return r;
    }
    const f64 offsetLengthSquared = math::Vec3d_LengthSquared( offset );
    if ( !std::isfinite( offsetLengthSquared ) ) {
        r.status = geometry_status_t::NUMERIC_FAILURE;
        return r;
    }
    if ( !( offsetLengthSquared > 0.0 ) ) {
        r.status = geometry_status_t::DEGENERATE;
        return r;
    }

    // Selected boundary half-edges and the vertices they touch.
    struct sel_t {
        geometry_mesh_half_edge_handle_t h;
        geometry_mesh_vertex_handle_t a, b;
    };
    struct vert_t {
        u64 key;
        geometry_mesh_vertex_handle_t v;
        u32 iOut; // selected edge starting here, or kNone
        u32 iIn;  // selected edge ending here, or kNone
        geometry_mesh_vertex_handle_t copy;
        geometry_mesh_edge_handle_t side;
    };
    const allocator_t *pA = pMesh->pAllocator;
    const usize E = edges.nCount;
    vector_t<sel_t> sel{};
    vector_t<vert_t> verts{};
    vector_t<u64> edgeKeys{};
    vector_t<geometry_mesh_half_edge_handle_t> quads{}; // 4 per selected edge
    vector_t<u8> touched{};
    shell_scratch_t scratch{};
    auto cleanup = [&]() noexcept {
        Vector_Shutdown( &sel );
        Vector_Shutdown( &verts );
        Vector_Shutdown( &edgeKeys );
        Vector_Shutdown( &quads );
        Vector_Shutdown( &touched );
        ShutdownScratch( &scratch );
    };
    auto fail = [&]( geometry_status_t s ) noexcept {
        cleanup();
        r.status = s;
        return r;
    };
    if ( !Vector_Init( &sel, pA ) || !Vector_Reserve( &sel, E ) || !Vector_Init( &verts, pA ) ||
         !Vector_Reserve( &verts, 2u * E ) || !Vector_Init( &edgeKeys, pA ) || !Vector_Reserve( &edgeKeys, E ) ||
         !Vector_Init( &quads, pA ) || !Vector_Resize( &quads, 4u * E ) || !Vector_Init( &touched, pA ) ||
         !Vector_Resize( &touched, pMesh->vertices.cSlots ) ) {
        return fail( geometry_status_t::ALLOCATION_FAILED );
    }
    for ( usize i = 0u; i < E; ++i ) {
        const mesh_edge_record_t *pE = GenerationPool_Get( &pMesh->edges, edges.pData[i] );
        const mesh_half_edge_record_t *pH = pE ? He( pMesh, pE->hHalfEdge ) : nullptr;
        if ( pH == nullptr ) { return fail( geometry_status_t::INVALID_HANDLE ); }
        if ( !IsBoundaryHe( pMesh, *pH ) ) { return fail( geometry_status_t::INVALID_ARGUMENT ); }
        const geometry_mesh_vertex_handle_t hDest = DestOf( pMesh, *pH );
        const mesh_vertex_record_t *pOrigin = GenerationPool_Get( &pMesh->vertices, pH->hOrigin );
        const mesh_vertex_record_t *pDest = GenerationPool_Get( &pMesh->vertices, hDest );
        if ( pOrigin == nullptr || pDest == nullptr ) { return fail( geometry_status_t::CORRUPT_STATE ); }
        const math::vec3d_t areaVector = math::Vec3d_Cross(
            math::Vec3d_Subtract( pDest->position, pOrigin->position ), offset );
        const f64 areaSquared = math::Vec3d_LengthSquared( areaVector );
        if ( !std::isfinite( areaSquared ) ) { return fail( geometry_status_t::NUMERIC_FAILURE ); }
        // The side quad must also clear the source's minimum face area; the
        // Newell vector of a parallelogram is twice this cross product.
        if ( !FaceAreaDescribable( math::Vec3d_Scale( areaVector, 2.0 ) ) ) { return fail( geometry_status_t::DEGENERATE ); }
        (void)Vector_PushBack( &edgeKeys, Key( edges.pData[i] ) );
        (void)Vector_PushBack( &sel, sel_t{ pE->hHalfEdge, pH->hOrigin, hDest } );
        (void)Vector_PushBack( &verts, vert_t{ Key( pH->hOrigin ), pH->hOrigin, kUnlabelled, kUnlabelled, {}, {} } );
        (void)Vector_PushBack( &verts, vert_t{ Key( hDest ), hDest, kUnlabelled, kUnlabelled, {}, {} } );
    }
    std::sort( edgeKeys.pData, edgeKeys.pData + E );
    for ( usize i = 1u; i < E; ++i ) {
        if ( edgeKeys.pData[i] == edgeKeys.pData[i - 1u] ) { return fail( geometry_status_t::INVALID_ARGUMENT ); }
    }
    std::sort( verts.pData, verts.pData + verts.nCount, []( const vert_t &x, const vert_t &y ) { return x.key < y.key; } );
    usize w = 0u;
    for ( usize i = 0u; i < verts.nCount; ++i ) {
        if ( w == 0u || verts.pData[w - 1u].key != verts.pData[i].key ) { verts.pData[w++] = verts.pData[i]; }
    }
    verts.nCount = w;
    auto vertIndex = [&]( geometry_mesh_vertex_handle_t v ) noexcept -> usize {
        const vert_t *pBegin = verts.pData, *pEnd = pBegin + verts.nCount;
        return static_cast<usize>( std::lower_bound( pBegin, pEnd, Key( v ),
                                                     []( const vert_t &x, u64 k ) { return x.key < k; } ) -
                                   pBegin );
    };
    for ( usize i = 0u; i < E; ++i ) {
        vert_t &a = verts.pData[vertIndex( sel.pData[i].a )];
        vert_t &b = verts.pData[vertIndex( sel.pData[i].b )];
        // Two selected boundary edges leaving (or entering) one vertex means
        // a pinched boundary: the strip would be ambiguous there.
        if ( a.iOut != kUnlabelled || b.iIn != kUnlabelled ) { return fail( geometry_status_t::NON_MANIFOLD ); }
        a.iOut = static_cast<u32>( i );
        b.iIn = static_cast<u32>( i );
    }
    for ( usize i = 0u; i < verts.nCount; ++i ) {
        const math::vec3d_t p =
            math::Vec3d_Add( GenerationPool_Get( &pMesh->vertices, verts.pData[i].v )->position, offset );
        if ( !math::Vec3d_IsFinite( p ) ) { return fail( geometry_status_t::NUMERIC_FAILURE ); }
    }

    const usize V = verts.nCount;
    geometry_status_t st = ReserveMore( &pMesh->vertices, V );
    if ( st == geometry_status_t::OK ) { st = ReserveMore( &pMesh->halfEdges, 4u * E ); }
    if ( st == geometry_status_t::OK ) { st = ReserveMore( &pMesh->edges, V + E ); }
    if ( st == geometry_status_t::OK ) { st = ReserveMore( &pMesh->loops, E ); }
    if ( st == geometry_status_t::OK ) { st = ReserveMore( &pMesh->faces, E ); }
    if ( st == geometry_status_t::OK ) { st = ReserveForRebuild( &pMesh->shells, GenerationPool_Count( &pMesh->shells ) ); } // strips join their shell
    if ( st == geometry_status_t::OK && pOuterEdgesOut != nullptr &&
         !Vector_Reserve( pOuterEdgesOut, pOuterEdgesOut->nCount + E ) ) {
        st = geometry_status_t::ALLOCATION_FAILED;
    }
    if ( st == geometry_status_t::OK &&
         ( !PrepareScratch( &scratch, pMesh, pMesh->faces.cSlots, GenerationPool_Count( &pMesh->faces ) + E ) ||
           !Vector_Resize( &touched, pMesh->vertices.cSlots ) ) ) {
        // The mask must also cover the copies' (possibly new) slots.
        st = geometry_status_t::ALLOCATION_FAILED;
    }
    if ( st != geometry_status_t::OK ) { return fail( st ); }

    // ---- Mutation ----
    for ( usize i = 0u; i < touched.nCount; ++i ) { touched.pData[i] = 0u; }
    for ( usize i = 0u; i < V; ++i ) {
        mesh_vertex_record_t rec{};
        rec.position = math::Vec3d_Add( GenerationPool_Get( &pMesh->vertices, verts.pData[i].v )->position, offset );
        verts.pData[i].copy = GenerationPool_Insert( &pMesh->vertices, rec ).handle;
        verts.pData[i].side = GenerationPool_Insert( &pMesh->edges, mesh_edge_record_t{} ).handle;
        touched.pData[verts.pData[i].v.nSlot] = 1u;
        touched.pData[verts.pData[i].copy.nSlot] = 1u;
    }
    // Quad i: q0 = b->a (twin of the selected boundary edge), q1 = a->a',
    // q2 = a'->b' (the new outer boundary edge), q3 = b'->b. Winding is
    // forced by q0 running against the boundary edge, so it always matches
    // the face the edge belonged to.
    for ( usize i = 0u; i < E; ++i ) {
        const sel_t &e = sel.pData[i];
        const vert_t &va = verts.pData[vertIndex( e.a )];
        const vert_t &vb = verts.pData[vertIndex( e.b )];
        const geometry_mesh_face_handle_t hFace = GenerationPool_Insert( &pMesh->faces, mesh_face_record_t{} ).handle;
        const geometry_mesh_loop_handle_t hLoop = GenerationPool_Insert( &pMesh->loops, mesh_loop_record_t{} ).handle;
        const geometry_mesh_edge_handle_t hOuter = GenerationPool_Insert( &pMesh->edges, mesh_edge_record_t{} ).handle;
        geometry_mesh_half_edge_handle_t *q = quads.pData + 4u * i;
        for ( int k = 0; k < 4; ++k ) { q[k] = GenerationPool_Insert( &pMesh->halfEdges, mesh_half_edge_record_t{} ).handle; }
        const geometry_mesh_vertex_handle_t origins[4] = { e.b, e.a, va.copy, vb.copy };
        const geometry_mesh_edge_handle_t edgesOf[4] = { He( pMesh, e.h )->hEdge, va.side, hOuter, vb.side };
        for ( int k = 0; k < 4; ++k ) {
            mesh_half_edge_record_t *pQ = HeMut( pMesh, q[k] );
            pQ->hOrigin = origins[k];
            pQ->hNext = q[( k + 1 ) % 4];
            pQ->hPrev = q[( k + 3 ) % 4];
            pQ->hEdge = edgesOf[k];
            pQ->hLoop = hLoop;
            pQ->hTwin = GEOMETRY_HANDLE_INVALID<geometry_mesh_half_edge_tag_t>;
        }
        HeMut( pMesh, q[0] )->hTwin = e.h;
        HeMut( pMesh, e.h )->hTwin = q[0];
        GenerationPool_Get( &pMesh->edges, hOuter )->hHalfEdge = q[2];
        mesh_loop_record_t *pLoop = GenerationPool_Get( &pMesh->loops, hLoop );
        pLoop->hFirstHalfEdge = q[0];
        pLoop->hFace = hFace;
        pLoop->cHalfEdges = 4u;
        const math::vec3d_t pts[4] = { GenerationPool_Get( &pMesh->vertices, e.b )->position,
                                       GenerationPool_Get( &pMesh->vertices, e.a )->position,
                                       GenerationPool_Get( &pMesh->vertices, va.copy )->position,
                                       GenerationPool_Get( &pMesh->vertices, vb.copy )->position };
        mesh_face_record_t *pFace = GenerationPool_Get( &pMesh->faces, hFace );
        pFace->hOuterLoop = hLoop;
        pFace->normal = UnitOrZero( NewellOf( pts, 4u ) );
        pFace->iSourceSide = CY_INVALID_INDEX;
        if ( pOuterEdgesOut != nullptr ) { (void)Vector_PushBack( pOuterEdgesOut, hOuter ); }
    }
    // Side edges: neighbouring quads share the edge (v, v'), q1 of the quad
    // leaving v against q3 of the quad arriving at v.
    for ( usize i = 0u; i < V; ++i ) {
        const vert_t &v = verts.pData[i];
        const geometry_mesh_half_edge_handle_t hUp =
            v.iOut != kUnlabelled ? quads.pData[4u * v.iOut + 1u] : GEOMETRY_HANDLE_INVALID<geometry_mesh_half_edge_tag_t>;
        const geometry_mesh_half_edge_handle_t hDown =
            v.iIn != kUnlabelled ? quads.pData[4u * v.iIn + 3u] : GEOMETRY_HANDLE_INVALID<geometry_mesh_half_edge_tag_t>;
        if ( GeometryHandle_IsValid( hUp ) && GeometryHandle_IsValid( hDown ) ) {
            HeMut( pMesh, hUp )->hTwin = hDown;
            HeMut( pMesh, hDown )->hTwin = hUp;
        }
        GenerationPool_Get( &pMesh->edges, v.side )->hHalfEdge = GeometryHandle_IsValid( hUp ) ? hUp : hDown;
        // Any live outgoing edge of the copy; FixOutEdges then picks the
        // start of its open fan.
        GenerationPool_Get( &pMesh->vertices, v.copy )->hOutHalfEdge =
            v.iOut != kUnlabelled ? quads.pData[4u * v.iOut + 2u] : hDown;
    }
    FixOutEdges( pMesh, touched );
    RebuildShells( pMesh, &scratch );

    r.cFacesCreated = static_cast<u32>( E );
    r.cVerticesCreated = static_cast<u32>( V );
    r.status = geometry_status_t::OK;
    cleanup();
    return r;
}

// ---------------------------------------------------------------------------
// Detach faces
// ---------------------------------------------------------------------------

mesh_boundary_detach_result_t MeshBoundary_DetachFaces(
    editable_mesh_t *pMesh,
    span_t<const geometry_mesh_face_handle_t> faces ) noexcept
{
    mesh_boundary_detach_result_t r{};
    if ( !EditableMesh_IsInitialized( pMesh ) ) {
        r.status = geometry_status_t::NOT_INITIALIZED;
        return r;
    }
    if ( faces.nCount == 0u || faces.pData == nullptr ) {
        r.status = geometry_status_t::INVALID_ARGUMENT;
        return r;
    }
    const allocator_t *pA = pMesh->pAllocator;
    vector_t<u8> inSel{}, usedBySel{}, usedByOther{}, touched{};
    vector_t<geometry_mesh_half_edge_handle_t> cut{}; // selected side of each cut edge
    // Outgoing half-edges of shared vertices, grouped into post-cut fans.
    struct fan_entry_t {
        u64 vKey;
        u64 hKey;
        geometry_mesh_half_edge_handle_t h;
        u32 parent; // union-find over fan links
        bool bSel;
    };
    vector_t<fan_entry_t> fan{};
    vector_t<geometry_mesh_vertex_handle_t> copyOf{}; // by entry root (after mutation)
    shell_scratch_t scratch{};
    auto cleanup = [&]() noexcept {
        Vector_Shutdown( &inSel );
        Vector_Shutdown( &usedBySel );
        Vector_Shutdown( &usedByOther );
        Vector_Shutdown( &touched );
        Vector_Shutdown( &cut );
        Vector_Shutdown( &fan );
        Vector_Shutdown( &copyOf );
        ShutdownScratch( &scratch );
    };
    auto fail = [&]( geometry_status_t s ) noexcept {
        cleanup();
        r.status = s;
        return r;
    };
    if ( !Vector_Init( &inSel, pA ) || !Vector_Init( &usedBySel, pA ) || !Vector_Init( &usedByOther, pA ) ||
         !Vector_Init( &touched, pA ) || !Vector_Init( &cut, pA ) || !Vector_Init( &fan, pA ) || !Vector_Init( &copyOf, pA ) ) {
        return fail( geometry_status_t::ALLOCATION_FAILED );
    }
    const geometry_status_t faceListStatus = ValidateFaceList( pMesh, faces, &inSel );
    if ( faceListStatus != geometry_status_t::OK ) { return fail( faceListStatus ); }
    if ( faces.nCount == GenerationPool_Count( &pMesh->faces ) ) {
        cleanup();
        r.status = geometry_status_t::OK; // nothing to separate from
        return r;
    }
    const usize cVSlots = pMesh->vertices.cSlots;
    if ( !Vector_Resize( &usedBySel, cVSlots ) || !Vector_Resize( &usedByOther, cVSlots ) ||
         !Vector_Resize( &touched, cVSlots ) ) {
        return fail( geometry_status_t::ALLOCATION_FAILED );
    }
    for ( usize i = 0u; i < cVSlots; ++i ) { usedBySel.pData[i] = usedByOther.pData[i] = touched.pData[i] = 0u; }
    bool bOk = true;
    auto isSelHe = [&]( const mesh_half_edge_record_t &he ) noexcept {
        const geometry_mesh_face_handle_t f = FaceOf( pMesh, he.hNext ); // same loop as he
        return GeometryHandle_IsValid( f ) && inSel.pData[f.nSlot] != 0u;
    };
    (void)GenerationPool_ForEach( &pMesh->halfEdges,
        [&]( geometry_mesh_half_edge_handle_t h, const mesh_half_edge_record_t &he ) noexcept -> bool_t {
            const bool bSel = isSelHe( he );
            ( bSel ? usedBySel : usedByOther ).pData[he.hOrigin.nSlot] = 1u;
            if ( bSel && !IsBoundaryHe( pMesh, he ) && !isSelHe( *He( pMesh, he.hTwin ) ) ) {
                bOk = bOk && Vector_PushBack( &cut, h );
            }
            return true;
        } );
    if ( !bOk ) { return fail( geometry_status_t::ALLOCATION_FAILED ); }

    // Fans after the cut, at every vertex both sides use. Around such a
    // vertex the selected faces may form several separate sectors, and so
    // may the unselected ones; each sector becomes its own fan once the cut
    // edges are severed, and a vertex may only have one fan. So every fan
    // gets its own vertex: the first unselected fan keeps the original
    // (and with it its source identity), every other fan gets a copy.
    (void)GenerationPool_ForEach( &pMesh->halfEdges,
        [&]( geometry_mesh_half_edge_handle_t h, const mesh_half_edge_record_t &he ) noexcept -> bool_t {
            const u32 slot = he.hOrigin.nSlot;
            if ( usedBySel.pData[slot] && usedByOther.pData[slot] ) {
                bOk = bOk && Vector_PushBack( &fan, fan_entry_t{ Key( he.hOrigin ), Key( h ), h, 0u, isSelHe( he ) } );
            }
            return bOk;
        } );
    if ( !bOk || !Vector_Resize( &copyOf, fan.nCount ) ) { return fail( geometry_status_t::ALLOCATION_FAILED ); }
    std::sort( fan.pData, fan.pData + fan.nCount, []( const fan_entry_t &a, const fan_entry_t &b ) {
        return a.vKey != b.vKey ? a.vKey < b.vKey : a.hKey < b.hKey;
    } );
    for ( usize i = 0u; i < fan.nCount; ++i ) { fan.pData[i].parent = static_cast<u32>( i ); }
    auto root = [&]( u32 i ) noexcept {
        while ( fan.pData[i].parent != i ) {
            fan.pData[i].parent = fan.pData[fan.pData[i].parent].parent;
            i = fan.pData[i].parent;
        }
        return i;
    };
    usize cDup = 0u;
    for ( usize lo = 0u; lo < fan.nCount; ) {
        usize hi = lo;
        while ( hi < fan.nCount && fan.pData[hi].vKey == fan.pData[lo].vKey ) { ++hi; }
        // Link h to the next outgoing half-edge around v, next(twin(h)),
        // unless h's edge is open or cut.
        for ( usize i = lo; i < hi; ++i ) {
            const mesh_half_edge_record_t *pH = He( pMesh, fan.pData[i].h );
            if ( IsBoundaryHe( pMesh, *pH ) ) { continue; }
            const mesh_half_edge_record_t *pT = He( pMesh, pH->hTwin );
            if ( isSelHe( *pT ) != fan.pData[i].bSel ) { continue; }
            const u64 nextKey = Key( pT->hNext );
            for ( usize j = lo; j < hi; ++j ) {
                if ( fan.pData[j].hKey == nextKey ) {
                    fan.pData[root( static_cast<u32>( i ) )].parent = root( static_cast<u32>( j ) );
                    break;
                }
            }
        }
        u32 keeper = CY_INVALID_INDEX;
        usize cRoots = 0u;
        for ( usize i = lo; i < hi; ++i ) {
            if ( root( static_cast<u32>( i ) ) == i ) { ++cRoots; }
            if ( keeper == CY_INVALID_INDEX && !fan.pData[i].bSel ) { keeper = root( static_cast<u32>( i ) ); }
        }
        cDup += cRoots - 1u; // every fan but the keeper's gets a copy
        (void)keeper;
        lo = hi;
    }
    // The keeper per vertex: the fan of its first unselected outgoing
    // half-edge (fans never mix sides: links skip cut edges).
    auto keeperOf = [&]( usize lo, usize hi ) noexcept {
        for ( usize i = lo; i < hi; ++i ) {
            if ( !isSelHe( *He( pMesh, fan.pData[i].h ) ) ) { return root( static_cast<u32>( i ) ); }
        }
        return static_cast<u32>( CY_INVALID_INDEX );
    };

    // Components after the cut.
    if ( !PrepareScratch( &scratch, pMesh, pMesh->faces.cSlots, GenerationPool_Count( &pMesh->faces ) ) ) {
        return fail( geometry_status_t::ALLOCATION_FAILED );
    }
    const u32 cShells = LabelComponents(
        pMesh, &scratch, []( geometry_mesh_face_handle_t ) noexcept { return false; },
        [&]( geometry_mesh_half_edge_handle_t h ) noexcept {
            const mesh_half_edge_record_t *pH = He( pMesh, h );
            return isSelHe( *pH ) != ( !IsBoundaryHe( pMesh, *pH ) && isSelHe( *He( pMesh, pH->hTwin ) ) );
        } );
    geometry_status_t st = PrepareRebuildShells( &scratch, cShells ) ? geometry_status_t::OK
                                                                      : geometry_status_t::ALLOCATION_FAILED;
    if ( st == geometry_status_t::OK ) { st = ReserveMore( &pMesh->vertices, cDup ); }
    if ( st == geometry_status_t::OK ) { st = ReserveMore( &pMesh->edges, cut.nCount ); }
    if ( st == geometry_status_t::OK ) { st = ReserveForRebuild( &pMesh->shells, cShells ); }
    // The mask must also cover the copies' (possibly new) slots.
    if ( st == geometry_status_t::OK && !Vector_Resize( &touched, pMesh->vertices.cSlots ) ) {
        st = geometry_status_t::ALLOCATION_FAILED;
    }
    if ( st != geometry_status_t::OK ) { return fail( st ); }

    // ---- Mutation ----
    for ( usize i = cVSlots; i < touched.nCount; ++i ) { touched.pData[i] = 0u; }
    // One copy per non-keeper fan; its half-edges leave from the copy. Fan
    // membership was computed on the uncut mesh, so it is still valid here.
    for ( usize lo = 0u; lo < fan.nCount; ) {
        usize hi = lo;
        while ( hi < fan.nCount && fan.pData[hi].vKey == fan.pData[lo].vKey ) { ++hi; }
        const u32 keeper = keeperOf( lo, hi );
        const geometry_mesh_vertex_handle_t hV = He( pMesh, fan.pData[lo].h )->hOrigin;
        touched.pData[hV.nSlot] = 1u;
        for ( usize i = lo; i < hi; ++i ) {
            const u32 rt = root( static_cast<u32>( i ) );
            if ( rt == i && rt != keeper ) {
                mesh_vertex_record_t rec{};
                rec.position = GenerationPool_Get( &pMesh->vertices, hV )->position;
                copyOf.pData[i] = GenerationPool_Insert( &pMesh->vertices, rec ).handle;
                touched.pData[copyOf.pData[i].nSlot] = 1u;
            }
        }
        for ( usize i = lo; i < hi; ++i ) {
            const u32 rt = root( static_cast<u32>( i ) );
            if ( rt == keeper ) { continue; }
            HeMut( pMesh, fan.pData[i].h )->hOrigin = copyOf.pData[rt];
            GenerationPool_Get( &pMesh->vertices, copyOf.pData[rt] )->hOutHalfEdge = fan.pData[i].h;
        }
        lo = hi;
    }
    // Sever the cut edges: the selected side gets a new edge record.
    for ( usize i = 0u; i < cut.nCount; ++i ) {
        mesh_half_edge_record_t *pH = HeMut( pMesh, cut.pData[i] );
        mesh_half_edge_record_t *pT = HeMut( pMesh, pH->hTwin );
        mesh_edge_record_t *pOld = GenerationPool_Get( &pMesh->edges, pH->hEdge );
        mesh_edge_record_t fresh{};
        fresh.hHalfEdge = cut.pData[i];
        fresh.creaseWeight = pOld->creaseWeight;
        pOld->hHalfEdge = pH->hTwin;
        pT->hTwin = GEOMETRY_HANDLE_INVALID<geometry_mesh_half_edge_tag_t>;
        pH->hTwin = GEOMETRY_HANDLE_INVALID<geometry_mesh_half_edge_tag_t>;
        pH->hEdge = GenerationPool_Insert( &pMesh->edges, fresh ).handle;
    }
    // Originals and copies on the cut now sit on open fans: point each at
    // its fan's start.
    FixOutEdges( pMesh, touched );
    RebuildShells( pMesh, &scratch );

    r.cVerticesDuplicated = static_cast<u32>( cDup );
    r.cEdgesCut = static_cast<u32>( cut.nCount );
    r.status = geometry_status_t::OK;
    cleanup();
    return r;
}

// ---------------------------------------------------------------------------
// Add faces
// ---------------------------------------------------------------------------

namespace
{

constexpr u64 kNewVertexBit = 1ull << 63;

struct directed_t {
    u64 from;
    u64 to;
    u32 index; // existing: unused; new: index into the new half-edge list
    geometry_mesh_half_edge_handle_t h; // existing half-edge (invalid for new)
};

bool DirectedLess( const directed_t &a, const directed_t &b ) noexcept
{
    return a.from != b.from ? a.from < b.from : a.to < b.to;
}

const directed_t *FindDirected( const vector_t<directed_t> &v, u64 from, u64 to ) noexcept
{
    const directed_t key{ from, to, 0u, {} };
    const directed_t *pBegin = v.pData, *pEnd = pBegin + v.nCount;
    const directed_t *p = std::lower_bound( pBegin, pEnd, key, DirectedLess );
    return ( p != pEnd && p->from == from && p->to == to ) ? p : nullptr;
}

} // namespace

mesh_boundary_add_result_t MeshBoundary_AddFaces(
    editable_mesh_t *pMesh,
    span_t<const mesh_boundary_corner_t> faceCorners,
    span_t<const u32> faceSizes,
    span_t<const math::vec3d_t> newPositions,
    vector_t<geometry_mesh_face_handle_t> *pNewFacesOut ) noexcept
{
    // Adding is replacing nothing: the same stitching and the same
    // post-edit fan check as every other face rebuild.
    mesh_boundary_add_result_t r{};
    if ( faceSizes.nCount == 0u ) { return r; }
    const mesh_boundary_replace_result_t rr =
        MeshBoundary_ReplaceFaces( pMesh, {}, faceCorners, faceSizes, newPositions, pNewFacesOut );
    r.status = rr.status;
    if ( rr.status == geometry_status_t::OK ) {
        r.cFacesCreated = rr.cFacesCreated;
        r.cVerticesCreated = rr.cVerticesCreated;
        r.cEdgesJoined = rr.cEdgesJoined;
    }
    return r;
}

// ---------------------------------------------------------------------------
// Bridge
// ---------------------------------------------------------------------------

namespace
{

// Collects the boundary loop starting at boundary half-edge hStart: each
// next boundary half-edge leaves the current one's destination. A vertex
// with two outgoing boundary half-edges makes the walk ambiguous.
geometry_status_t WalkBoundaryLoop(
    const editable_mesh_t *pMesh,
    geometry_mesh_half_edge_handle_t hStart,
    const vector_t<directed_t> &boundaryByOrigin,
    vector_t<geometry_mesh_half_edge_handle_t> *pOut ) noexcept
{
    geometry_mesh_half_edge_handle_t cur = hStart;
    for ( ;; ) {
        if ( pOut->nCount >= kMeshBoundaryLoopMax ) { return geometry_status_t::LIMIT_EXCEEDED; }
        if ( !Vector_PushBack( pOut, cur ) ) { return geometry_status_t::ALLOCATION_FAILED; }
        const u64 dest = Key( DestOf( pMesh, *He( pMesh, cur ) ) );
        const directed_t probe{ dest, 0u, 0u, {} };
        const directed_t *pBegin = boundaryByOrigin.pData, *pEnd = pBegin + boundaryByOrigin.nCount;
        const directed_t *lo = std::lower_bound( pBegin, pEnd, probe, DirectedLess );
        const directed_t *hi = lo;
        while ( hi != pEnd && hi->from == dest ) { ++hi; }
        if ( hi - lo != 1 ) { return geometry_status_t::NON_MANIFOLD; }
        cur = lo->h;
        if ( Same( cur, hStart ) ) { return geometry_status_t::OK; }
    }
}

} // namespace

mesh_boundary_add_result_t MeshBoundary_Bridge(
    editable_mesh_t *pMesh,
    geometry_mesh_edge_handle_t hEdgeA,
    geometry_mesh_edge_handle_t hEdgeB,
    vector_t<geometry_mesh_face_handle_t> *pNewFacesOut ) noexcept
{
    mesh_boundary_add_result_t r{};
    if ( !EditableMesh_IsInitialized( pMesh ) ) {
        r.status = geometry_status_t::NOT_INITIALIZED;
        return r;
    }
    if ( !MeshBoundary_IsBoundaryEdge( pMesh, hEdgeA ) || !MeshBoundary_IsBoundaryEdge( pMesh, hEdgeB ) ) {
        r.status = GenerationPool_Contains( &pMesh->edges, hEdgeA ) && GenerationPool_Contains( &pMesh->edges, hEdgeB )
                       ? geometry_status_t::INVALID_ARGUMENT
                       : geometry_status_t::INVALID_HANDLE;
        return r;
    }
    const allocator_t *pA = pMesh->pAllocator;
    vector_t<directed_t> boundary{};
    vector_t<geometry_mesh_half_edge_handle_t> loopA{}, loopB{};
    vector_t<mesh_boundary_corner_t> corners{};
    vector_t<u32> sizes{};
    auto cleanup = [&]() noexcept {
        Vector_Shutdown( &boundary );
        Vector_Shutdown( &loopA );
        Vector_Shutdown( &loopB );
        Vector_Shutdown( &corners );
        Vector_Shutdown( &sizes );
    };
    if ( !Vector_Init( &boundary, pA ) || !Vector_Init( &loopA, pA ) || !Vector_Init( &loopB, pA ) ||
         !Vector_Init( &corners, pA ) || !Vector_Init( &sizes, pA ) ) {
        cleanup();
        r.status = geometry_status_t::ALLOCATION_FAILED;
        return r;
    }
    bool bOk = true;
    (void)GenerationPool_ForEach( &pMesh->halfEdges,
        [&]( geometry_mesh_half_edge_handle_t h, const mesh_half_edge_record_t &he ) noexcept -> bool_t {
            if ( IsBoundaryHe( pMesh, he ) ) {
                bOk = bOk && Vector_PushBack( &boundary, directed_t{ Key( he.hOrigin ), Key( DestOf( pMesh, he ) ), 0u, h } );
            }
            return true;
        } );
    if ( !bOk ) {
        cleanup();
        r.status = geometry_status_t::ALLOCATION_FAILED;
        return r;
    }
    std::sort( boundary.pData, boundary.pData + boundary.nCount, DirectedLess );
    const geometry_mesh_half_edge_handle_t hA = GenerationPool_Get( &pMesh->edges, hEdgeA )->hHalfEdge;
    const geometry_mesh_half_edge_handle_t hB = GenerationPool_Get( &pMesh->edges, hEdgeB )->hHalfEdge;
    geometry_status_t st = WalkBoundaryLoop( pMesh, hA, boundary, &loopA );
    if ( st == geometry_status_t::OK ) { st = WalkBoundaryLoop( pMesh, hB, boundary, &loopB ); }
    if ( st == geometry_status_t::OK ) {
        for ( usize i = 0u; i < loopA.nCount; ++i ) {
            if ( Same( loopA.pData[i], hB ) ) { st = geometry_status_t::INVALID_ARGUMENT; } // same loop
        }
        if ( loopA.nCount != loopB.nCount ) { st = geometry_status_t::INVALID_ARGUMENT; }
    }
    const usize n = loopA.nCount;
    if ( st == geometry_status_t::OK &&
         ( !Vector_Reserve( &corners, 4u * n ) || !Vector_Resize( &sizes, n ) ) ) {
        st = geometry_status_t::ALLOCATION_FAILED;
    }
    if ( st != geometry_status_t::OK ) {
        cleanup();
        r.status = st;
        return r;
    }
    // a_i = origin(A_i); b_j = origin(B_j); B is walked backwards starting
    // at b_1 so quad i = (a_{i+1}, a_i, c_i, c_{i+1}) with c_i = b_{1-i}.
    auto a = [&]( usize i ) noexcept { return He( pMesh, loopA.pData[i % n] )->hOrigin; };
    auto c = [&]( usize i ) noexcept { return He( pMesh, loopB.pData[( n + 1u - ( i % n ) ) % n] )->hOrigin; };
    for ( usize i = 0u; i < n; ++i ) {
        sizes.pData[i] = 4u;
        (void)Vector_PushBack( &corners, mesh_boundary_corner_t{ a( i + 1u ), CY_INVALID_INDEX } );
        (void)Vector_PushBack( &corners, mesh_boundary_corner_t{ a( i ), CY_INVALID_INDEX } );
        (void)Vector_PushBack( &corners, mesh_boundary_corner_t{ c( i ), CY_INVALID_INDEX } );
        (void)Vector_PushBack( &corners, mesh_boundary_corner_t{ c( i + 1u ), CY_INVALID_INDEX } );
    }
    r = MeshBoundary_AddFaces( pMesh, span_t<const mesh_boundary_corner_t>{ corners.pData, corners.nCount },
                               span_t<const u32>{ sizes.pData, sizes.nCount }, span_t<const math::vec3d_t>{}, pNewFacesOut );
    cleanup();
    return r;
}

// ---------------------------------------------------------------------------
// Replace faces
// ---------------------------------------------------------------------------

namespace
{

// One corner of the post-edit mesh at a vertex: the neighbour it comes
// from and the neighbour it goes to in its face (keys as in AddFaces).
struct fan_corner_t {
    u64 vertex;
    u64 in;
    u64 out;
};

bool FanCornerLess( const fan_corner_t &a, const fan_corner_t &b ) noexcept
{
    return a.vertex != b.vertex ? a.vertex < b.vertex : a.in < b.in;
}

// True when the corners of one vertex form exactly one fan. Corner c is
// followed by the corner whose in-neighbour is c's out-neighbour (the face
// across c's outgoing edge). Directed edges are unique by then, so every
// corner has at most one successor and one predecessor, and the corners
// form disjoint paths and cycles; one fan means one path or one cycle
// through all of them.
bool SingleFan( const fan_corner_t *pBegin, const fan_corner_t *pEnd ) noexcept
{
    const usize g = static_cast<usize>( pEnd - pBegin );
    auto byIn = [&]( u64 in ) noexcept -> const fan_corner_t * {
        const fan_corner_t *p =
            std::lower_bound( pBegin, pEnd, in, []( const fan_corner_t &x, u64 k ) { return x.in < k; } );
        return ( p != pEnd && p->in == in ) ? p : nullptr;
    };
    const fan_corner_t *pStart = pBegin;
    for ( const fan_corner_t *c = pBegin; c != pEnd; ++c ) {
        bool bHasPredecessor = false;
        for ( const fan_corner_t *o = pBegin; o != pEnd && !bHasPredecessor; ++o ) { bHasPredecessor = o->out == c->in; }
        if ( !bHasPredecessor ) {
            pStart = c; // the open end of a fan
            break;
        }
    }
    usize cVisited = 0u;
    const fan_corner_t *c = pStart;
    do {
        if ( ++cVisited > g ) { return false; }
        c = byIn( c->out );
    } while ( c != nullptr && c != pStart );
    return cVisited == g;
}

} // namespace

mesh_boundary_replace_result_t MeshBoundary_ReplaceFaces(
    editable_mesh_t *pMesh,
    span_t<const geometry_mesh_face_handle_t> removeFaces,
    span_t<const mesh_boundary_corner_t> faceCorners,
    span_t<const u32> faceSizes,
    span_t<const math::vec3d_t> newPositions,
    vector_t<geometry_mesh_face_handle_t> *pNewFacesOut ) noexcept
{
    mesh_boundary_replace_result_t r{};
    if ( !EditableMesh_IsInitialized( pMesh ) ) {
        r.status = geometry_status_t::NOT_INITIALIZED;
        return r;
    }
    const usize F = faceSizes.nCount;
    if ( ( removeFaces.nCount > 0u && removeFaces.pData == nullptr ) ||
         ( F > 0u && ( faceSizes.pData == nullptr || faceCorners.pData == nullptr ) ) ||
         ( newPositions.nCount > 0u && newPositions.pData == nullptr ) ||
         ( pNewFacesOut != nullptr && pNewFacesOut->pAllocator == nullptr ) || ( removeFaces.nCount == 0u && F == 0u ) ) {
        return r;
    }
    usize cTotal = 0u;
    for ( usize f = 0u; f < F; ++f ) {
        if ( faceSizes.pData[f] < 3u || faceSizes.pData[f] > kMeshBoundaryLoopMax ) { return r; }
        cTotal += faceSizes.pData[f];
    }
    if ( cTotal != faceCorners.nCount ) { return r; }
    for ( usize i = 0u; i < newPositions.nCount; ++i ) {
        if ( !math::Vec3d_IsFinite( newPositions.pData[i] ) ) {
            r.status = geometry_status_t::NUMERIC_FAILURE;
            return r;
        }
    }

    const allocator_t *pA = pMesh->pAllocator;
    vector_t<u8> dead{}, heDead{}, vTouched{}, fixMask{}, newUsed{};
    vector_t<directed_t> existing{}, fresh{};
    vector_t<fan_corner_t> corners{};
    vector_t<geometry_mesh_half_edge_handle_t> doomedHe{}, created{};
    vector_t<geometry_mesh_edge_handle_t> doomedEdges{};
    vector_t<geometry_mesh_vertex_handle_t> doomedVertices{}, newHandles{};
    vector_t<math::vec3d_t> ring{};
    shell_scratch_t scratch{};
    auto cleanup = [&]() noexcept {
        Vector_Shutdown( &dead );
        Vector_Shutdown( &heDead );
        Vector_Shutdown( &vTouched );
        Vector_Shutdown( &fixMask );
        Vector_Shutdown( &newUsed );
        Vector_Shutdown( &existing );
        Vector_Shutdown( &fresh );
        Vector_Shutdown( &corners );
        Vector_Shutdown( &doomedHe );
        Vector_Shutdown( &created );
        Vector_Shutdown( &doomedEdges );
        Vector_Shutdown( &doomedVertices );
        Vector_Shutdown( &newHandles );
        Vector_Shutdown( &ring );
        ShutdownScratch( &scratch );
    };
    auto fail = [&]( geometry_status_t st ) noexcept {
        cleanup();
        r = mesh_boundary_replace_result_t{};
        r.status = st;
        return r;
    };
    if ( !Vector_Init( &dead, pA ) || !Vector_Init( &heDead, pA ) || !Vector_Init( &vTouched, pA ) ||
         !Vector_Init( &fixMask, pA ) || !Vector_Init( &newUsed, pA ) || !Vector_Init( &existing, pA ) ||
         !Vector_Init( &fresh, pA ) || !Vector_Init( &corners, pA ) || !Vector_Init( &doomedHe, pA ) ||
         !Vector_Init( &created, pA ) || !Vector_Init( &doomedEdges, pA ) || !Vector_Init( &doomedVertices, pA ) ||
         !Vector_Init( &newHandles, pA ) || !Vector_Init( &ring, pA ) || !Vector_Reserve( &fresh, cTotal ) ||
         !Vector_Resize( &newUsed, newPositions.nCount ) || !Vector_Resize( &newHandles, newPositions.nCount ) ||
         !Vector_Resize( &created, cTotal ) || !Vector_Reserve( &ring, kMeshBoundaryLoopMax ) ||
         !Vector_Resize( &heDead, pMesh->halfEdges.cSlots ) || !Vector_Resize( &vTouched, pMesh->vertices.cSlots ) ) {
        return fail( geometry_status_t::ALLOCATION_FAILED );
    }
    for ( usize i = 0u; i < newUsed.nCount; ++i ) { newUsed.pData[i] = 0u; }
    for ( usize i = 0u; i < heDead.nCount; ++i ) { heDead.pData[i] = 0u; }
    for ( usize i = 0u; i < vTouched.nCount; ++i ) { vTouched.pData[i] = 0u; }
    const geometry_status_t faceListStatus = ValidateFaceList( pMesh, removeFaces, &dead );
    if ( faceListStatus != geometry_status_t::OK ) { return fail( faceListStatus ); }
    const usize cFacesBefore = GenerationPool_Count( &pMesh->faces );
    if ( F == 0u && removeFaces.nCount == cFacesBefore ) { return fail( geometry_status_t::DEGENERATE ); }

    // ---- Removed half-edges ----
    for ( usize i = 0u; i < removeFaces.nCount; ++i ) {
        const mesh_face_record_t *pF = GenerationPool_Get( &pMesh->faces, removeFaces.pData[i] );
        const mesh_loop_record_t *pL = GenerationPool_Get( &pMesh->loops, pF->hOuterLoop );
        if ( pL == nullptr ) { return fail( geometry_status_t::CORRUPT_STATE ); }
        geometry_mesh_half_edge_handle_t h = pL->hFirstHalfEdge;
        for ( u32 k = 0u; k < pL->cHalfEdges; ++k ) {
            const mesh_half_edge_record_t *pH = He( pMesh, h );
            if ( pH == nullptr ) { return fail( geometry_status_t::CORRUPT_STATE ); }
            heDead.pData[h.nSlot] = 1u;
            vTouched.pData[pH->hOrigin.nSlot] = 1u;
            if ( !Vector_PushBack( &doomedHe, h ) ) { return fail( geometry_status_t::ALLOCATION_FAILED ); }
            h = pH->hNext;
        }
    }
    auto twinDead = [&]( const mesh_half_edge_record_t &h ) noexcept {
        return !IsBoundaryHe( pMesh, h ) && heDead.pData[h.hTwin.nSlot] != 0u;
    };

    // ---- New rings ----
    auto keyOf = [&]( const mesh_boundary_corner_t &c ) noexcept -> u64 {
        return c.iNew != CY_INVALID_INDEX ? ( kNewVertexBit | c.iNew ) : Key( c.hVertex );
    };
    auto posOf = [&]( const mesh_boundary_corner_t &c ) noexcept -> math::vec3d_t {
        return c.iNew != CY_INVALID_INDEX ? newPositions.pData[c.iNew]
                                          : GenerationPool_Get( &pMesh->vertices, c.hVertex )->position;
    };
    usize iBase = 0u;
    for ( usize f = 0u; f < F; ++f ) {
        const u32 n = faceSizes.pData[f];
        Vector_Clear( &ring );
        for ( u32 k = 0u; k < n; ++k ) {
            const mesh_boundary_corner_t &c = faceCorners.pData[iBase + k];
            if ( c.iNew != CY_INVALID_INDEX ) {
                if ( c.iNew >= newPositions.nCount ) { return fail( geometry_status_t::INVALID_ARGUMENT ); }
                newUsed.pData[c.iNew] = 1u;
            } else if ( !GenerationPool_Contains( &pMesh->vertices, c.hVertex ) ) {
                return fail( geometry_status_t::INVALID_HANDLE );
            } else {
                vTouched.pData[c.hVertex.nSlot] = 1u;
            }
            for ( u32 m = 0u; m < k; ++m ) {
                if ( keyOf( faceCorners.pData[iBase + m] ) == keyOf( c ) ) { return fail( geometry_status_t::INVALID_ARGUMENT ); }
            }
            (void)Vector_PushBack( &ring, posOf( c ) );
            const mesh_boundary_corner_t &next = faceCorners.pData[iBase + ( k + 1u ) % n];
            (void)Vector_PushBack( &fresh, directed_t{ keyOf( c ), keyOf( next ), static_cast<u32>( iBase + k ), {} } );
        }
        if ( !FaceAreaDescribable( NewellOf( ring.pData, n ) ) ) { return fail( geometry_status_t::DEGENERATE ); }
        iBase += n;
    }
    for ( usize i = 0u; i < newUsed.nCount; ++i ) {
        if ( newUsed.pData[i] == 0u ) { return fail( geometry_status_t::INVALID_ARGUMENT ); }
    }
    std::sort( fresh.pData, fresh.pData + fresh.nCount, DirectedLess );
    for ( usize i = 1u; i < fresh.nCount; ++i ) {
        if ( fresh.pData[i].from == fresh.pData[i - 1u].from && fresh.pData[i].to == fresh.pData[i - 1u].to ) {
            return fail( geometry_status_t::NON_MANIFOLD );
        }
    }

    // ---- Surviving half-edges and edge classification ----
    if ( !Vector_Reserve( &existing, GenerationPool_Count( &pMesh->halfEdges ) ) ) {
        return fail( geometry_status_t::ALLOCATION_FAILED );
    }
    (void)GenerationPool_ForEach( &pMesh->halfEdges,
        [&]( geometry_mesh_half_edge_handle_t h, const mesh_half_edge_record_t &he ) noexcept -> bool_t {
            if ( heDead.pData[h.nSlot] == 0u ) {
                (void)Vector_PushBack( &existing, directed_t{ Key( he.hOrigin ), Key( DestOf( pMesh, he ) ), 0u, h } );
            }
            return true;
        } );
    std::sort( existing.pData, existing.pData + existing.nCount, DirectedLess );
    usize cNewEdges = 0u;
    u32 cJoined = 0u;
    for ( usize i = 0u; i < fresh.nCount; ++i ) {
        const directed_t &d = fresh.pData[i];
        if ( FindDirected( existing, d.from, d.to ) != nullptr ) { return fail( geometry_status_t::NON_MANIFOLD ); }
        const directed_t *pRev = FindDirected( existing, d.to, d.from );
        if ( pRev != nullptr ) {
            const mesh_half_edge_record_t *pR = He( pMesh, pRev->h );
            if ( !IsBoundaryHe( pMesh, *pR ) && !twinDead( *pR ) ) { return fail( geometry_status_t::NON_MANIFOLD ); }
            ++cJoined;
        } else if ( FindDirected( fresh, d.to, d.from ) == nullptr || d.from < d.to ) {
            ++cNewEdges; // new boundary edge, or one record per new-new pair
        }
    }
    // Edges no surviving half-edge uses (one entry per edge).
    for ( usize i = 0u; i < doomedHe.nCount; ++i ) {
        const mesh_half_edge_record_t *pH = He( pMesh, doomedHe.pData[i] );
        if ( IsBoundaryHe( pMesh, *pH ) ) {
            if ( !Vector_PushBack( &doomedEdges, pH->hEdge ) ) { return fail( geometry_status_t::ALLOCATION_FAILED ); }
        } else if ( twinDead( *pH ) && Key( doomedHe.pData[i] ) < Key( pH->hTwin ) ) {
            if ( !Vector_PushBack( &doomedEdges, pH->hEdge ) ) { return fail( geometry_status_t::ALLOCATION_FAILED ); }
        }
    }

    // ---- One fan per vertex afterwards ----
    // Post-edit corners at every touched existing vertex (surviving ones
    // plus new ones) and at every new vertex.
    bool bOk = true;
    (void)GenerationPool_ForEach( &pMesh->halfEdges,
        [&]( geometry_mesh_half_edge_handle_t h, const mesh_half_edge_record_t &he ) noexcept -> bool_t {
            if ( heDead.pData[h.nSlot] != 0u || vTouched.pData[he.hOrigin.nSlot] == 0u ) { return true; }
            const mesh_half_edge_record_t *pPrev = He( pMesh, he.hPrev );
            bOk = bOk && pPrev != nullptr &&
                  Vector_PushBack( &corners, fan_corner_t{ Key( he.hOrigin ), Key( pPrev->hOrigin ), Key( DestOf( pMesh, he ) ) } );
            return bOk;
        } );
    iBase = 0u;
    for ( usize f = 0u; bOk && f < F; ++f ) {
        const u32 n = faceSizes.pData[f];
        for ( u32 k = 0u; bOk && k < n; ++k ) {
            bOk = Vector_PushBack( &corners, fan_corner_t{ keyOf( faceCorners.pData[iBase + k] ),
                                                           keyOf( faceCorners.pData[iBase + ( k + n - 1u ) % n] ),
                                                           keyOf( faceCorners.pData[iBase + ( k + 1u ) % n] ) } );
        }
        iBase += n;
    }
    if ( !bOk ) { return fail( geometry_status_t::ALLOCATION_FAILED ); }
    std::sort( corners.pData, corners.pData + corners.nCount, FanCornerLess );
    for ( usize i = 0u; i < corners.nCount; ) {
        usize j = i;
        while ( j < corners.nCount && corners.pData[j].vertex == corners.pData[i].vertex ) { ++j; }
        if ( !SingleFan( corners.pData + i, corners.pData + j ) ) { return fail( geometry_status_t::NON_MANIFOLD ); }
        i = j;
    }
    // Touched existing vertices with no corner left are removed.
    for ( usize slot = 0u; slot < vTouched.nCount; ++slot ) {
        if ( vTouched.pData[slot] == 0u || pMesh->vertices.pSlots[slot].bOccupied == false ) { continue; }
        const geometry_mesh_vertex_handle_t hV{ static_cast<u32>( slot ), pMesh->vertices.pSlots[slot].nGeneration };
        const fan_corner_t probe{ Key( hV ), 0u, 0u };
        const fan_corner_t *p = std::lower_bound( corners.pData, corners.pData + corners.nCount, probe, FanCornerLess );
        if ( p == corners.pData + corners.nCount || p->vertex != Key( hV ) ) {
            if ( !Vector_PushBack( &doomedVertices, hV ) ) { return fail( geometry_status_t::ALLOCATION_FAILED ); }
        }
    }

    // ---- Reservations and shells ----
    const usize cFacesAfter = cFacesBefore - removeFaces.nCount + F;
    geometry_status_t st = ReserveMore( &pMesh->vertices, newPositions.nCount );
    if ( st == geometry_status_t::OK ) { st = ReserveMore( &pMesh->halfEdges, cTotal ); }
    if ( st == geometry_status_t::OK ) { st = ReserveMore( &pMesh->edges, cNewEdges ); }
    if ( st == geometry_status_t::OK ) { st = ReserveMore( &pMesh->loops, F ); }
    if ( st == geometry_status_t::OK ) { st = ReserveMore( &pMesh->faces, F ); }
    if ( st == geometry_status_t::OK && !PrepareScratch( &scratch, pMesh, pMesh->faces.cSlots, cFacesAfter + 1u ) ) {
        st = geometry_status_t::ALLOCATION_FAILED;
    }
    // Shells after: the surviving faces' components, plus at most one per
    // new face (a new face either joins a component or starts one).
    u32 cShellsAfter = 0u;
    if ( st == geometry_status_t::OK ) {
        cShellsAfter = LabelComponents(
                           pMesh, &scratch, [&]( geometry_mesh_face_handle_t f ) noexcept { return dead.pData[f.nSlot] != 0u; },
                           []( geometry_mesh_half_edge_handle_t ) noexcept { return false; } ) +
                       static_cast<u32>( F );
        st = ReserveForRebuild( &pMesh->shells, cShellsAfter );
    }
    if ( st == geometry_status_t::OK &&
         ( !PrepareRebuildShells( &scratch, cShellsAfter ) || !Vector_Resize( &fixMask, pMesh->vertices.cSlots ) ||
           ( pNewFacesOut != nullptr && !Vector_Reserve( pNewFacesOut, pNewFacesOut->nCount + F ) ) ) ) {
        st = geometry_status_t::ALLOCATION_FAILED;
    }
    if ( st != geometry_status_t::OK ) { return fail( st ); }

    // ---- Mutation (cannot fail from here) ----
    for ( usize i = 0u; i < fixMask.nCount; ++i ) { fixMask.pData[i] = i < vTouched.nCount ? vTouched.pData[i] : 0u; }
    for ( usize i = 0u; i < doomedHe.nCount; ++i ) {
        const mesh_half_edge_record_t *pH = He( pMesh, doomedHe.pData[i] );
        if ( !IsBoundaryHe( pMesh, *pH ) && heDead.pData[pH->hTwin.nSlot] == 0u ) {
            // The surviving side becomes the boundary and owns the edge.
            HeMut( pMesh, pH->hTwin )->hTwin = GEOMETRY_HANDLE_INVALID<geometry_mesh_half_edge_tag_t>;
            GenerationPool_Get( &pMesh->edges, pH->hEdge )->hHalfEdge = pH->hTwin;
        }
    }
    for ( usize i = 0u; i < removeFaces.nCount; ++i ) {
        const geometry_mesh_loop_handle_t hLoop = GenerationPool_Get( &pMesh->faces, removeFaces.pData[i] )->hOuterLoop;
        (void)GenerationPool_Remove( &pMesh->loops, hLoop );
        (void)GenerationPool_Remove( &pMesh->faces, removeFaces.pData[i] );
    }
    for ( usize i = 0u; i < doomedHe.nCount; ++i ) { (void)GenerationPool_Remove( &pMesh->halfEdges, doomedHe.pData[i] ); }
    for ( usize i = 0u; i < doomedEdges.nCount; ++i ) { (void)GenerationPool_Remove( &pMesh->edges, doomedEdges.pData[i] ); }
    for ( usize i = 0u; i < doomedVertices.nCount; ++i ) { (void)GenerationPool_Remove( &pMesh->vertices, doomedVertices.pData[i] ); }

    for ( usize i = 0u; i < newPositions.nCount; ++i ) {
        mesh_vertex_record_t rec{};
        rec.position = newPositions.pData[i];
        newHandles.pData[i] = GenerationPool_Insert( &pMesh->vertices, rec ).handle;
        fixMask.pData[newHandles.pData[i].nSlot] = 1u;
    }
    auto handleOf = [&]( const mesh_boundary_corner_t &c ) noexcept {
        return c.iNew != CY_INVALID_INDEX ? newHandles.pData[c.iNew] : c.hVertex;
    };
    iBase = 0u;
    for ( usize f = 0u; f < F; ++f ) {
        const u32 n = faceSizes.pData[f];
        const geometry_mesh_face_handle_t hFace = GenerationPool_Insert( &pMesh->faces, mesh_face_record_t{} ).handle;
        const geometry_mesh_loop_handle_t hLoop = GenerationPool_Insert( &pMesh->loops, mesh_loop_record_t{} ).handle;
        for ( u32 k = 0u; k < n; ++k ) {
            created.pData[iBase + k] = GenerationPool_Insert( &pMesh->halfEdges, mesh_half_edge_record_t{} ).handle;
        }
        Vector_Clear( &ring );
        for ( u32 k = 0u; k < n; ++k ) {
            const mesh_boundary_corner_t &c = faceCorners.pData[iBase + k];
            mesh_half_edge_record_t *pH = HeMut( pMesh, created.pData[iBase + k] );
            pH->hOrigin = handleOf( c );
            pH->hNext = created.pData[iBase + ( k + 1u ) % n];
            pH->hPrev = created.pData[iBase + ( k + n - 1u ) % n];
            pH->hLoop = hLoop;
            pH->hTwin = GEOMETRY_HANDLE_INVALID<geometry_mesh_half_edge_tag_t>;
            (void)Vector_PushBack( &ring, posOf( c ) );
        }
        mesh_loop_record_t *pLoop = GenerationPool_Get( &pMesh->loops, hLoop );
        pLoop->hFirstHalfEdge = created.pData[iBase];
        pLoop->hFace = hFace;
        pLoop->cHalfEdges = n;
        mesh_face_record_t *pFace = GenerationPool_Get( &pMesh->faces, hFace );
        pFace->hOuterLoop = hLoop;
        pFace->normal = UnitOrZero( NewellOf( ring.pData, n ) );
        pFace->iSourceSide = CY_INVALID_INDEX;
        if ( pNewFacesOut != nullptr ) { (void)Vector_PushBack( pNewFacesOut, hFace ); }
        iBase += n;
    }
    for ( usize i = 0u; i < fresh.nCount; ++i ) {
        const directed_t &d = fresh.pData[i];
        const geometry_mesh_half_edge_handle_t hNew = created.pData[d.index];
        mesh_half_edge_record_t *pNew = HeMut( pMesh, hNew );
        if ( const directed_t *pRev = FindDirected( existing, d.to, d.from ) ) {
            mesh_half_edge_record_t *pOld = HeMut( pMesh, pRev->h );
            pOld->hTwin = hNew;
            pNew->hTwin = pRev->h;
            pNew->hEdge = pOld->hEdge;
        } else if ( const directed_t *pPair = FindDirected( fresh, d.to, d.from ) ) {
            pNew->hTwin = created.pData[pPair->index];
            if ( d.from < d.to ) {
                mesh_edge_record_t e{};
                e.hHalfEdge = hNew;
                const geometry_mesh_edge_handle_t hE = GenerationPool_Insert( &pMesh->edges, e ).handle;
                pNew->hEdge = hE;
                HeMut( pMesh, created.pData[pPair->index] )->hEdge = hE;
            }
        } else {
            mesh_edge_record_t e{};
            e.hHalfEdge = hNew;
            pNew->hEdge = GenerationPool_Insert( &pMesh->edges, e ).handle;
        }
    }
    FixOutEdges( pMesh, fixMask );
    RebuildShells( pMesh, &scratch );

    r.cFacesRemoved = static_cast<u32>( removeFaces.nCount );
    r.cFacesCreated = static_cast<u32>( F );
    r.cVerticesCreated = static_cast<u32>( newPositions.nCount );
    r.cVerticesRemoved = static_cast<u32>( doomedVertices.nCount );
    r.cEdgesRemoved = static_cast<u32>( doomedEdges.nCount );
    r.cEdgesJoined = cJoined;
    r.status = geometry_status_t::OK;
    cleanup();
    return r;
}

} // namespace cypher::editor::geometry
