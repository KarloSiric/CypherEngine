//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshKnife.cpp
//  Purpose: Implements the knife cut: exact preflight of the whole path,
//           then allocation-free edge splits and face splits.
//  Details: Preflight resolves every boundary point to a key (an existing
//           vertex, or one entry of the deduplicated, sorted edge-point
//           table), finds the one face each segment cuts, builds that face's
//           post-split corner ring, and checks both pieces with exact 2D
//           predicates. Only then are records counted and reserved.
//
//           Mutation order matters: edges are split first, so every edge
//           point is a real vertex in both adjacent loops before any face is
//           split; face splits then only ever connect two vertices already
//           on the face's loop. The ring built in preflight inserts edge
//           points in the same order the edge split does (ascending t along
//           the edge record's half-edge, descending along its twin), so the
//           preflight indices describe exactly the loop the mutation sees.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshKnife.h"
#include "CypherGeometry_MeshPlanar.h"
#include "CypherGeometry_MeshRecordAccess.h"
#include "CypherGeometry_MeshSource.h"

#include <algorithm>
#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;
using namespace mesh_detail;

namespace
{

constexpr u32 kFaceCornersMax = kMeshSourceCornersPerFaceMax;
// A fan longer than this is treated as corrupt links rather than walked
// forever; no authored vertex has thousands of faces around it.
constexpr u32 kFanMax = 4096u;

// A resolved boundary point: an existing vertex (by handle key) or an entry
// of the unique edge-point table.
struct point_key_t {
    bool bEdge{ false };
    u64 value{ 0u };
};

bool KeyEq( point_key_t a, point_key_t b ) noexcept { return a.bEdge == b.bEdge && a.value == b.value; }

struct edge_point_t {
    u64 edgeKey{ 0u };
    f64 t{ 0.0 };
    geometry_mesh_edge_handle_t hEdge{};
    math::vec3d_t position{};
};

bool EdgePointLess( const edge_point_t &a, const edge_point_t &b ) noexcept
{
    return a.edgeKey != b.edgeKey ? a.edgeKey < b.edgeKey : a.t < b.t;
}

struct ring_entry_t {
    point_key_t key{};
    math::vec3d_t position{};
};

struct segment_t {
    u32 iFrom{ 0u }; // path index of A
    u32 iTo{ 0u };   // path index of B
    geometry_mesh_face_handle_t hFace{};
    bool bSkip{ false }; // runs along an existing edge: nothing to cut
};

// [lo, hi) of the edge points on one edge (table sorted by EdgePointLess).
void EdgePointRange( const vector_t<edge_point_t> &eps, u64 edgeKey, usize *pLo, usize *pHi ) noexcept
{
    const edge_point_t *pB = eps.pData, *pE = eps.pData + eps.nCount;
    const edge_point_t *lo =
        std::lower_bound( pB, pE, edgeKey, []( const edge_point_t &x, u64 k ) { return x.edgeKey < k; } );
    const edge_point_t *hi =
        std::upper_bound( pB, pE, edgeKey, []( u64 k, const edge_point_t &x ) { return k < x.edgeKey; } );
    *pLo = static_cast<usize>( lo - pB );
    *pHi = static_cast<usize>( hi - pB );
}

// The face's corner ring after every edge point is inserted, in loop order
// from the loop's first half-edge.
geometry_status_t BuildRing(
    const editable_mesh_t *pMesh,
    geometry_mesh_face_handle_t hFace,
    const vector_t<edge_point_t> &eps,
    vector_t<ring_entry_t> *pRing ) noexcept
{
    Vector_Clear( pRing );
    const mesh_face_record_t *pF = GenerationPool_Get( &pMesh->faces, hFace );
    const mesh_loop_record_t *pL = pF ? GenerationPool_Get( &pMesh->loops, pF->hOuterLoop ) : nullptr;
    if ( pL == nullptr ) { return geometry_status_t::CORRUPT_STATE; }
    geometry_mesh_half_edge_handle_t h = pL->hFirstHalfEdge;
    for ( u32 k = 0u; k < pL->cHalfEdges; ++k ) {
        const mesh_half_edge_record_t *pH = He( pMesh, h );
        const mesh_vertex_record_t *pV = pH ? GenerationPool_Get( &pMesh->vertices, pH->hOrigin ) : nullptr;
        const mesh_edge_record_t *pE = pH ? GenerationPool_Get( &pMesh->edges, pH->hEdge ) : nullptr;
        if ( pV == nullptr || pE == nullptr ) { return geometry_status_t::CORRUPT_STATE; }
        if ( !Vector_PushBack( pRing, ring_entry_t{ point_key_t{ false, Key( pH->hOrigin ) }, pV->position } ) ) {
            return geometry_status_t::ALLOCATION_FAILED;
        }
        usize lo = 0u, hi = 0u;
        EdgePointRange( eps, Key( pH->hEdge ), &lo, &hi );
        const bool bForward = Same( h, pE->hHalfEdge );
        for ( usize i = 0u; i < hi - lo; ++i ) {
            const usize idx = bForward ? lo + i : hi - 1u - i;
            if ( !Vector_PushBack( pRing, ring_entry_t{ point_key_t{ true, idx }, eps.pData[idx].position } ) ) {
                return geometry_status_t::ALLOCATION_FAILED;
            }
        }
        h = pH->hNext;
    }
    return geometry_status_t::OK;
}

u32 FindInRing( const vector_t<ring_entry_t> &ring, point_key_t key ) noexcept
{
    for ( usize i = 0u; i < ring.nCount; ++i ) {
        if ( KeyEq( ring.pData[i].key, key ) ) { return static_cast<u32>( i ); }
    }
    return CY_INVALID_INDEX;
}

// Every outgoing half-edge of v, found by walking its fan both ways from
// the stored out half-edge (so a vertex whose stored half-edge is not the
// boundary one is still fully covered).
geometry_status_t OutHalfEdges(
    const editable_mesh_t *pMesh,
    geometry_mesh_vertex_handle_t hV,
    vector_t<geometry_mesh_half_edge_handle_t> *pOut ) noexcept
{
    Vector_Clear( pOut );
    const mesh_vertex_record_t *pV = GenerationPool_Get( &pMesh->vertices, hV );
    if ( pV == nullptr ) { return geometry_status_t::INVALID_HANDLE; }
    const geometry_mesh_half_edge_handle_t hStart = pV->hOutHalfEdge;
    geometry_mesh_half_edge_handle_t h = hStart;
    for ( u32 guard = 0u;; ++guard ) {
        const mesh_half_edge_record_t *pH = He( pMesh, h );
        if ( guard >= kFanMax || pH == nullptr || !Same( pH->hOrigin, hV ) ) { return geometry_status_t::CORRUPT_STATE; }
        if ( !Vector_PushBack( pOut, h ) ) { return geometry_status_t::ALLOCATION_FAILED; }
        const mesh_half_edge_record_t *pP = He( pMesh, pH->hPrev );
        if ( pP == nullptr ) { return geometry_status_t::CORRUPT_STATE; }
        if ( IsBoundaryHe( pMesh, *pP ) ) { break; }
        h = pP->hTwin;
        if ( Same( h, hStart ) ) { return geometry_status_t::OK; }
    }
    // Open fan: the walk above stopped at one boundary side; collect the
    // rest by rotating the other way from the start.
    h = hStart;
    for ( u32 guard = 0u;; ++guard ) {
        const mesh_half_edge_record_t *pH = He( pMesh, h );
        if ( guard >= kFanMax || pH == nullptr ) { return geometry_status_t::CORRUPT_STATE; }
        if ( IsBoundaryHe( pMesh, *pH ) ) { return geometry_status_t::OK; }
        h = He( pMesh, pH->hTwin )->hNext;
        const mesh_half_edge_record_t *pN = He( pMesh, h );
        if ( pN == nullptr || !Same( pN->hOrigin, hV ) ) { return geometry_status_t::CORRUPT_STATE; }
        if ( Same( h, hStart ) ) { return geometry_status_t::OK; }
        if ( !Vector_PushBack( pOut, h ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    }
}

// Corner v of cut piece `piece` (0: ring A..B then the chain backwards;
// 1: ring B..A then the chain forwards), in 3D.
math::vec3d_t PiecePosition(
    const vector_t<ring_entry_t> &ring,
    u32 from,
    u32 steps,
    const mesh_knife_point_t *pChain,
    u32 k,
    u32 piece,
    u32 v ) noexcept
{
    const u32 n = static_cast<u32>( ring.nCount );
    if ( v <= steps ) { return ring.pData[( from + v ) % n].position; }
    const u32 c = v - steps - 1u;
    return pChain[piece == 0u ? k - 1u - c : c].position;
}

// Checks the cut of `ring` from ring[iA] through chain[0..k) to ring[iB].
// Both pieces must be simple with the ring's orientation. Each ring edge
// lies in exactly one piece together with the whole chain, so two simple
// pieces mean the chain crosses neither itself nor the face boundary; equal
// orientation then rules out a chain running outside the face (that would
// make one piece wind backwards).
geometry_status_t CheckCut(
    const vector_t<ring_entry_t> &ring,
    u32 iA,
    u32 iB,
    const mesh_knife_point_t *pChain,
    u32 k,
    vector_t<math::vec2d_t> *pScratch ) noexcept
{
    const u32 n = static_cast<u32>( ring.nCount );
    // Projection axis from the ring's Newell normal: only the axis choice
    // depends on this floating-point sum; the tests below are exact.
    math::vec3d_t normal{};
    for ( u32 i = 0u; i < n; ++i ) {
        const math::vec3d_t a = ring.pData[i].position, b = ring.pData[( i + 1u ) % n].position;
        normal.x += ( a.y - b.y ) * ( a.z + b.z );
        normal.y += ( a.z - b.z ) * ( a.x + b.x );
        normal.z += ( a.x - b.x ) * ( a.y + b.y );
    }
    const f64 ax = std::fabs( normal.x ), ay = std::fabs( normal.y ), az = std::fabs( normal.z );
    if ( !( ax > 0.0 || ay > 0.0 || az > 0.0 ) ) { return geometry_status_t::DEGENERATE; }
    const u32 axis = ( ax >= ay && ax >= az ) ? 0u : ( ay >= az ? 1u : 2u );

    if ( !Vector_Resize( pScratch, n ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    for ( u32 i = 0u; i < n; ++i ) { pScratch->pData[i] = Project( ring.pData[i].position, axis ); }
    const i32 reference = OrientationOf( pScratch->pData, n );
    if ( reference == 0 ) { return geometry_status_t::DEGENERATE; }

    const u32 d = ( iB + n - iA ) % n; // ring steps from A forward to B
    for ( u32 piece = 0u; piece < 2u; ++piece ) {
        // Piece 0: ring A..B then the chain backwards; piece 1: ring B..A then
        // the chain forwards (the loops MeshKnife_Cut builds).
        const u32 from = piece == 0u ? iA : iB;
        const u32 steps = piece == 0u ? d : n - d;
        const u32 size = steps + 1u + k;
        if ( !Vector_Resize( pScratch, n + size ) ) { return geometry_status_t::ALLOCATION_FAILED; }
        math::vec2d_t *pPiece = pScratch->pData + n;
        for ( u32 s = 0u; s <= steps; ++s ) { pPiece[s] = Project( ring.pData[( from + s ) % n].position, axis ); }
        for ( u32 c = 0u; c < k; ++c ) {
            const u32 iChain = piece == 0u ? k - 1u - c : c;
            pPiece[steps + 1u + c] = Project( pChain[iChain].position, axis );
        }
        if ( !IsSimple( pPiece, size ) || OrientationOf( pPiece, size ) != reference ) {
            return geometry_status_t::SELF_INTERSECTING;
        }
        // Exactly valid is not enough: the piece must also clear the
        // source's minimum face area (see FaceAreaDescribable).
        math::vec3d_t pieceNormal{};
        for ( u32 v = 0u; v < size; ++v ) {
            const math::vec3d_t a = PiecePosition( ring, from, steps, pChain, k, piece, v );
            const math::vec3d_t b = PiecePosition( ring, from, steps, pChain, k, piece, ( v + 1u ) % size );
            pieceNormal.x += ( a.y - b.y ) * ( a.z + b.z );
            pieceNormal.y += ( a.z - b.z ) * ( a.x + b.x );
            pieceNormal.z += ( a.x - b.x ) * ( a.y + b.y );
        }
        if ( !FaceAreaDescribable( pieceNormal ) ) { return geometry_status_t::DEGENERATE; }
        (void)Vector_Resize( pScratch, n );
    }
    return geometry_status_t::OK;
}

// ---------------------------------------------------------------------------
// Mutation helpers (capacity already reserved; cannot fail)
// ---------------------------------------------------------------------------

// Splits edge hEdge (half-edge h: a -> b) at new vertices v1..vm, ordered by
// ascending t from a. h keeps origin a and becomes (a -> v1); its twin
// becomes (v1 -> a). So the original edge record keeps the piece next to a
// on both sides - its hHalfEdge stays valid, including the boundary-edge
// convention - and every other piece gets a new edge record with the same
// crease weight.
void SplitEdgeAt( editable_mesh_t *pMesh, geometry_mesh_edge_handle_t hEdge, const geometry_mesh_vertex_handle_t *pV, u32 m ) noexcept
{
    const mesh_edge_record_t *pE = GenerationPool_Get( &pMesh->edges, hEdge );
    const f64 crease = pE->creaseWeight;
    const geometry_mesh_half_edge_handle_t h = pE->hHalfEdge;
    const mesh_half_edge_record_t hRec = *He( pMesh, h );
    const geometry_mesh_vertex_handle_t b = DestOf( pMesh, hRec );
    const geometry_mesh_half_edge_handle_t hn = hRec.hNext;
    const bool bTwin = !IsBoundaryHe( pMesh, hRec );

    // Forward pieces f_1..f_m in h's loop: a -> v1 (h), v1 -> v2, ..., vm -> b.
    geometry_mesh_half_edge_handle_t last = h;
    for ( u32 i = 0u; i < m; ++i ) {
        const geometry_mesh_edge_handle_t hS = GenerationPool_Insert( &pMesh->edges, mesh_edge_record_t{} ).handle;
        const geometry_mesh_half_edge_handle_t hF = GenerationPool_Insert( &pMesh->halfEdges, mesh_half_edge_record_t{} ).handle;
        mesh_half_edge_record_t *pF = HeMut( pMesh, hF );
        pF->hOrigin = pV[i];
        pF->hPrev = last;
        pF->hNext = hn;
        pF->hEdge = hS;
        pF->hLoop = hRec.hLoop;
        HeMut( pMesh, last )->hNext = hF;
        mesh_edge_record_t *pS = GenerationPool_Get( &pMesh->edges, hS );
        pS->hHalfEdge = hF; // the boundary half-edge when the edge is open
        pS->creaseWeight = crease;
        GenerationPool_Get( &pMesh->vertices, pV[i] )->hOutHalfEdge = hF;
        last = hF;
    }
    HeMut( pMesh, hn )->hPrev = last;
    GenerationPool_Get( &pMesh->loops, hRec.hLoop )->cHalfEdges += m;
    if ( !bTwin ) { return; }

    // Twin side: tw (b -> a) becomes (v1 -> a); new g_i (v_{i+1} -> v_i)
    // twin f_i, inserted before tw so the loop reads b, vm, ..., v1, a.
    const geometry_mesh_half_edge_handle_t tw = hRec.hTwin;
    const mesh_half_edge_record_t twRec = *He( pMesh, tw );
    geometry_mesh_half_edge_handle_t nextG = tw;
    geometry_mesh_half_edge_handle_t f = He( pMesh, h )->hNext;
    for ( u32 i = 0u; i < m; ++i ) {
        const mesh_half_edge_record_t *pF = He( pMesh, f );
        const geometry_mesh_vertex_handle_t dest = i + 1u < m ? pV[i + 1u] : b;
        const geometry_mesh_half_edge_handle_t hG = GenerationPool_Insert( &pMesh->halfEdges, mesh_half_edge_record_t{} ).handle;
        pF = He( pMesh, f );
        mesh_half_edge_record_t *pG = HeMut( pMesh, hG );
        pG->hOrigin = dest;
        pG->hTwin = f;
        pG->hNext = nextG;
        pG->hEdge = pF->hEdge;
        pG->hLoop = twRec.hLoop;
        HeMut( pMesh, nextG )->hPrev = hG;
        HeMut( pMesh, f )->hTwin = hG;
        nextG = hG;
        f = pF->hNext;
    }
    // nextG is now g_m (b -> vm).
    HeMut( pMesh, nextG )->hPrev = twRec.hPrev;
    HeMut( pMesh, twRec.hPrev )->hNext = nextG;
    HeMut( pMesh, tw )->hOrigin = pV[0];
    mesh_vertex_record_t *pB = GenerationPool_Get( &pMesh->vertices, b );
    if ( Same( pB->hOutHalfEdge, tw ) ) { pB->hOutHalfEdge = nextG; }
    GenerationPool_Get( &pMesh->loops, twRec.hLoop )->cHalfEdges += m;
}

geometry_mesh_half_edge_handle_t FindOutInLoop(
    const editable_mesh_t *pMesh,
    const mesh_loop_record_t *pL,
    geometry_mesh_vertex_handle_t hV ) noexcept
{
    geometry_mesh_half_edge_handle_t h = pL->hFirstHalfEdge;
    for ( u32 k = 0u; k < pL->cHalfEdges; ++k ) {
        const mesh_half_edge_record_t *pH = He( pMesh, h );
        if ( Same( pH->hOrigin, hV ) ) { return h; }
        h = pH->hNext;
    }
    return GEOMETRY_HANDLE_INVALID<geometry_mesh_half_edge_tag_t>;
}

} // namespace

mesh_knife_result_t MeshKnife_Cut(
    editable_mesh_t *pMesh,
    span_t<const mesh_knife_point_t> path,
    vector_t<geometry_mesh_vertex_handle_t> *pPointVerticesOut,
    vector_t<mesh_knife_split_t> *pSplitsOut ) noexcept
{
    mesh_knife_result_t r{};
    if ( pMesh == nullptr ) { return r; }
    if ( !EditableMesh_IsInitialized( pMesh ) ) {
        r.status = geometry_status_t::NOT_INITIALIZED;
        return r;
    }
    const u32 n = static_cast<u32>( path.nCount );
    if ( path.pData == nullptr || path.nCount < 2u || path.nCount > kMeshKnifePathMax ||
         ( pPointVerticesOut != nullptr && pPointVerticesOut->pAllocator == nullptr ) ||
         ( pSplitsOut != nullptr && pSplitsOut->pAllocator == nullptr ) ||
         path.pData[0].kind == mesh_knife_point_kind_t::FACE || path.pData[n - 1u].kind == mesh_knife_point_kind_t::FACE ) {
        r.status = geometry_status_t::INVALID_ARGUMENT;
        return r;
    }
    for ( u32 i = 0u; i < n; ++i ) {
        const mesh_knife_point_t &p = path.pData[i];
        switch ( p.kind ) {
            case mesh_knife_point_kind_t::VERTEX:
                if ( !GenerationPool_Contains( &pMesh->vertices, p.hVertex ) ) {
                    r.status = geometry_status_t::INVALID_HANDLE;
                    return r;
                }
                break;
            case mesh_knife_point_kind_t::EDGE: {
                const mesh_edge_record_t *pE = GenerationPool_Get( &pMesh->edges, p.hEdge );
                if ( pE == nullptr || He( pMesh, pE->hHalfEdge ) == nullptr ) {
                    r.status = geometry_status_t::INVALID_HANDLE;
                    return r;
                }
                if ( !std::isfinite( p.t ) || !( p.t > 0.0 && p.t < 1.0 ) ) { return r; }
                break;
            }
            case mesh_knife_point_kind_t::FACE:
                if ( !GenerationPool_Contains( &pMesh->faces, p.hFace ) ) {
                    r.status = geometry_status_t::INVALID_HANDLE;
                    return r;
                }
                if ( !math::Vec3d_IsFinite( p.position ) ) {
                    r.status = geometry_status_t::NUMERIC_FAILURE;
                    return r;
                }
                break;
            default: return r;
        }
    }

    const allocator_t *pA = pMesh->pAllocator;
    vector_t<edge_point_t> eps{};
    vector_t<point_key_t> keys{};
    vector_t<segment_t> segs{};
    vector_t<ring_entry_t> ring{};
    vector_t<geometry_mesh_half_edge_handle_t> outs{};
    vector_t<math::vec2d_t> flat{};
    vector_t<u8> faceUsed{};
    vector_t<geometry_mesh_face_handle_t> touched{};
    vector_t<geometry_mesh_vertex_handle_t> pointVertex{};
    vector_t<geometry_mesh_vertex_handle_t> epVertex{};
    vector_t<math::vec3d_t> normalScratch{};
    vector_t<u8> fixMask{};
    auto cleanup = [&]() noexcept {
        Vector_Shutdown( &eps );
        Vector_Shutdown( &keys );
        Vector_Shutdown( &segs );
        Vector_Shutdown( &ring );
        Vector_Shutdown( &outs );
        Vector_Shutdown( &flat );
        Vector_Shutdown( &faceUsed );
        Vector_Shutdown( &touched );
        Vector_Shutdown( &pointVertex );
        Vector_Shutdown( &epVertex );
        Vector_Shutdown( &normalScratch );
        Vector_Shutdown( &fixMask );
    };
    auto fail = [&]( geometry_status_t st ) noexcept {
        cleanup();
        r = mesh_knife_result_t{};
        r.status = st;
        return r;
    };
    if ( !Vector_Init( &eps, pA ) || !Vector_Init( &keys, pA ) || !Vector_Init( &segs, pA ) || !Vector_Init( &ring, pA ) ||
         !Vector_Init( &outs, pA ) || !Vector_Init( &flat, pA ) || !Vector_Init( &faceUsed, pA ) ||
         !Vector_Init( &touched, pA ) || !Vector_Init( &pointVertex, pA ) || !Vector_Init( &epVertex, pA ) ||
         !Vector_Init( &normalScratch, pA ) || !Vector_Init( &fixMask, pA ) || !Vector_Resize( &keys, n ) || !Vector_Resize( &pointVertex, n ) ||
         !Vector_Resize( &faceUsed, pMesh->faces.cSlots ) || !Vector_Reserve( &normalScratch, kFaceCornersMax ) ) {
        return fail( geometry_status_t::ALLOCATION_FAILED );
    }
    for ( usize i = 0u; i < faceUsed.nCount; ++i ) { faceUsed.pData[i] = 0u; }

    // ---- Unique edge points ----
    for ( u32 i = 0u; i < n; ++i ) {
        const mesh_knife_point_t &p = path.pData[i];
        if ( p.kind != mesh_knife_point_kind_t::EDGE ) { continue; }
        const mesh_half_edge_record_t *pH = He( pMesh, GenerationPool_Get( &pMesh->edges, p.hEdge )->hHalfEdge );
        const mesh_vertex_record_t *pVa = GenerationPool_Get( &pMesh->vertices, pH->hOrigin );
        const mesh_vertex_record_t *pVb = GenerationPool_Get( &pMesh->vertices, DestOf( pMesh, *pH ) );
        if ( pVa == nullptr || pVb == nullptr ) { return fail( geometry_status_t::CORRUPT_STATE ); }
        // Same formula as MeshSourceEdit_TrySplitEdge, so a knife point and
        // a plain edge split at the same t land on the same position.
        const math::vec3d_t pos =
            math::Vec3d_Add( math::Vec3d_Scale( pVa->position, 1.0 - p.t ), math::Vec3d_Scale( pVb->position, p.t ) );
        if ( !math::Vec3d_IsFinite( pos ) ) { return fail( geometry_status_t::NUMERIC_FAILURE ); }
        if ( math::Vec3d_EqualsExact( pos, pVa->position ) || math::Vec3d_EqualsExact( pos, pVb->position ) ) {
            return fail( geometry_status_t::DEGENERATE );
        }
        if ( !Vector_PushBack( &eps, edge_point_t{ Key( p.hEdge ), p.t, p.hEdge, pos } ) ) {
            return fail( geometry_status_t::ALLOCATION_FAILED );
        }
    }
    std::sort( eps.pData, eps.pData + eps.nCount, EdgePointLess );
    {
        usize w = 0u;
        for ( usize i = 0u; i < eps.nCount; ++i ) {
            if ( w > 0u && eps.pData[w - 1u].edgeKey == eps.pData[i].edgeKey && eps.pData[w - 1u].t == eps.pData[i].t ) {
                continue;
            }
            eps.pData[w++] = eps.pData[i];
        }
        (void)Vector_Resize( &eps, w );
    }
    // Two distinct t on one edge can still round to the same position.
    for ( usize i = 1u; i < eps.nCount; ++i ) {
        if ( eps.pData[i - 1u].edgeKey == eps.pData[i].edgeKey &&
             math::Vec3d_EqualsExact( eps.pData[i - 1u].position, eps.pData[i].position ) ) {
            return fail( geometry_status_t::DEGENERATE );
        }
    }
    for ( u32 i = 0u; i < n; ++i ) {
        const mesh_knife_point_t &p = path.pData[i];
        if ( p.kind == mesh_knife_point_kind_t::VERTEX ) {
            keys.pData[i] = point_key_t{ false, Key( p.hVertex ) };
        } else if ( p.kind == mesh_knife_point_kind_t::EDGE ) {
            const edge_point_t probe{ Key( p.hEdge ), p.t, {}, {} };
            const edge_point_t *pAt = std::lower_bound( eps.pData, eps.pData + eps.nCount, probe, EdgePointLess );
            keys.pData[i] = point_key_t{ true, static_cast<u64>( pAt - eps.pData ) };
        }
    }

    // ---- Segments: find the face each one cuts and check the cut ----
    u32 cChainVertices = 0u, cChainEdges = 0u, cCuts = 0u;
    for ( u32 i = 0u; i + 1u < n; ) {
        u32 j = i + 1u;
        while ( path.pData[j].kind == mesh_knife_point_kind_t::FACE ) { ++j; }
        segment_t seg{ i, j, {}, false };
        const u32 k = j - i - 1u;
        if ( KeyEq( keys.pData[i], keys.pData[j] ) ) { return fail( geometry_status_t::DEGENERATE ); }
        u32 iA = CY_INVALID_INDEX, iB = CY_INVALID_INDEX;
        if ( k > 0u ) {
            seg.hFace = path.pData[i + 1u].hFace;
            for ( u32 c = i + 1u; c < j; ++c ) {
                if ( !Same( path.pData[c].hFace, seg.hFace ) ) { return fail( geometry_status_t::INVALID_ARGUMENT ); }
            }
            const geometry_status_t st = BuildRing( pMesh, seg.hFace, eps, &ring );
            if ( st != geometry_status_t::OK ) { return fail( st ); }
            iA = FindInRing( ring, keys.pData[i] );
            iB = FindInRing( ring, keys.pData[j] );
            if ( iA == CY_INVALID_INDEX || iB == CY_INVALID_INDEX ) { return fail( geometry_status_t::INVALID_ARGUMENT ); }
        } else {
            const mesh_knife_point_t &pa = path.pData[i];
            const mesh_knife_point_t &pb = path.pData[j];
            // Candidate faces: every face on A's side (its fan, or the edge's
            // one or two faces).
            geometry_mesh_face_handle_t cand[2] = {};
            u32 cCand = 0u;
            geometry_mesh_half_edge_handle_t single[2] = {};
            u32 cSingle = 0u;
            if ( pa.kind == mesh_knife_point_kind_t::VERTEX ) {
                const geometry_status_t st = OutHalfEdges( pMesh, pa.hVertex, &outs );
                if ( st != geometry_status_t::OK ) { return fail( st ); }
                if ( pb.kind == mesh_knife_point_kind_t::VERTEX ) {
                    for ( usize o = 0u; o < outs.nCount; ++o ) {
                        if ( Same( DestOf( pMesh, *He( pMesh, outs.pData[o] ) ), pb.hVertex ) ) { seg.bSkip = true; }
                    }
                }
            } else {
                const mesh_half_edge_record_t *pH = He( pMesh, GenerationPool_Get( &pMesh->edges, pa.hEdge )->hHalfEdge );
                Vector_Clear( &outs );
                single[cSingle++] = GenerationPool_Get( &pMesh->edges, pa.hEdge )->hHalfEdge;
                if ( !IsBoundaryHe( pMesh, *pH ) ) { single[cSingle++] = pH->hTwin; }
            }
            if ( !seg.bSkip ) {
                const usize cOuts = pa.kind == mesh_knife_point_kind_t::VERTEX ? outs.nCount : cSingle;
                for ( usize o = 0u; o < cOuts; ++o ) {
                    const geometry_mesh_half_edge_handle_t hOut =
                        pa.kind == mesh_knife_point_kind_t::VERTEX ? outs.pData[o] : single[o];
                    const geometry_mesh_face_handle_t hF = FaceOf( pMesh, hOut );
                    if ( !GeometryHandle_IsValid( hF ) ) { return fail( geometry_status_t::CORRUPT_STATE ); }
                    const geometry_status_t st = BuildRing( pMesh, hF, eps, &ring );
                    if ( st != geometry_status_t::OK ) { return fail( st ); }
                    const u32 fa = FindInRing( ring, keys.pData[i] ), fb = FindInRing( ring, keys.pData[j] );
                    if ( fa == CY_INVALID_INDEX || fb == CY_INVALID_INDEX ) { continue; }
                    const u32 rn = static_cast<u32>( ring.nCount );
                    if ( ( fa + 1u ) % rn == fb || ( fb + 1u ) % rn == fa ) { continue; } // already joined
                    if ( cCand < 2u ) { cand[cCand] = hF; }
                    ++cCand;
                }
                if ( cCand != 1u ) { return fail( geometry_status_t::INVALID_ARGUMENT ); }
                seg.hFace = cand[0];
                const geometry_status_t st = BuildRing( pMesh, seg.hFace, eps, &ring );
                if ( st != geometry_status_t::OK ) { return fail( st ); }
                iA = FindInRing( ring, keys.pData[i] );
                iB = FindInRing( ring, keys.pData[j] );
            }
        }
        if ( !seg.bSkip ) {
            if ( faceUsed.pData[seg.hFace.nSlot] != 0u ) { return fail( geometry_status_t::INVALID_ARGUMENT ); }
            faceUsed.pData[seg.hFace.nSlot] = 1u;
            // Consecutive identical positions would give a zero-length edge.
            for ( u32 c = i; c < j; ++c ) {
                const math::vec3d_t from = c == i ? ring.pData[iA].position : path.pData[c].position;
                const math::vec3d_t to = c + 1u == j ? ring.pData[iB].position : path.pData[c + 1u].position;
                if ( math::Vec3d_EqualsExact( from, to ) ) { return fail( geometry_status_t::DEGENERATE ); }
            }
            const geometry_status_t st = CheckCut( ring, iA, iB, path.pData + i + 1u, k, &flat );
            if ( st != geometry_status_t::OK ) { return fail( st ); }
            const u32 rn = static_cast<u32>( ring.nCount ), d = ( iB + rn - iA ) % rn;
            if ( d + 1u + k > kFaceCornersMax || rn - d + 1u + k > kFaceCornersMax ) {
                return fail( geometry_status_t::LIMIT_EXCEEDED );
            }
            cChainVertices += k;
            cChainEdges += k + 1u;
            ++cCuts;
        }
        if ( !Vector_PushBack( &segs, seg ) ) { return fail( geometry_status_t::ALLOCATION_FAILED ); }
        i = j;
    }

    // ---- Faces that only gain edge points: size limit, normal recompute ----
    usize cSplitHalfEdges = 0u;
    for ( usize g = 0u; g < eps.nCount; ++g ) {
        if ( g > 0u && eps.pData[g - 1u].edgeKey == eps.pData[g].edgeKey ) { continue; }
        const mesh_half_edge_record_t *pH = He( pMesh, GenerationPool_Get( &pMesh->edges, eps.pData[g].hEdge )->hHalfEdge );
        const geometry_mesh_half_edge_handle_t sides[2] = { GenerationPool_Get( &pMesh->edges, eps.pData[g].hEdge )->hHalfEdge,
                                                            pH->hTwin };
        const u32 cSides = IsBoundaryHe( pMesh, *pH ) ? 1u : 2u;
        for ( u32 s = 0u; s < cSides; ++s ) {
            const geometry_mesh_face_handle_t hF = FaceOf( pMesh, sides[s] );
            if ( !GeometryHandle_IsValid( hF ) ) { return fail( geometry_status_t::CORRUPT_STATE ); }
            if ( faceUsed.pData[hF.nSlot] == 0u ) {
                const geometry_status_t st = BuildRing( pMesh, hF, eps, &ring );
                if ( st != geometry_status_t::OK ) { return fail( st ); }
                if ( ring.nCount > kFaceCornersMax ) { return fail( geometry_status_t::LIMIT_EXCEEDED ); }
            }
            if ( !Vector_PushBack( &touched, hF ) ) { return fail( geometry_status_t::ALLOCATION_FAILED ); }
        }
    }
    for ( usize e = 0u; e < eps.nCount; ++e ) {
        const mesh_half_edge_record_t *pH = He( pMesh, GenerationPool_Get( &pMesh->edges, eps.pData[e].hEdge )->hHalfEdge );
        cSplitHalfEdges += IsBoundaryHe( pMesh, *pH ) ? 1u : 2u;
    }

    // ---- Reservations ----
    const usize cNewVertices = eps.nCount + cChainVertices;
    const usize cNewEdges = eps.nCount + cChainEdges;
    geometry_status_t st = ReserveMore( &pMesh->vertices, cNewVertices );
    if ( st == geometry_status_t::OK ) { st = ReserveMore( &pMesh->edges, cNewEdges ); }
    if ( st == geometry_status_t::OK ) { st = ReserveMore( &pMesh->halfEdges, cSplitHalfEdges + 2u * cChainEdges ); }
    if ( st == geometry_status_t::OK ) { st = ReserveMore( &pMesh->loops, cCuts ); }
    if ( st == geometry_status_t::OK ) { st = ReserveMore( &pMesh->faces, cCuts ); }
    if ( st == geometry_status_t::OK &&
         ( !Vector_Reserve( &epVertex, eps.nCount ) || !Vector_Reserve( &touched, touched.nCount + 2u * cCuts ) ||
           ( pPointVerticesOut != nullptr && !Vector_Reserve( pPointVerticesOut, pPointVerticesOut->nCount + n ) ) ||
           ( pSplitsOut != nullptr && !Vector_Reserve( pSplitsOut, pSplitsOut->nCount + cCuts ) ) ||
           !Vector_Resize( &fixMask, pMesh->vertices.cSlots ) ) ) {
        st = geometry_status_t::ALLOCATION_FAILED;
    }
    if ( st != geometry_status_t::OK ) { return fail( st ); }

    // ---- Mutation: edge points ----
    for ( usize i = 0u; i < fixMask.nCount; ++i ) { fixMask.pData[i] = 0u; }
    for ( usize e = 0u; e < eps.nCount; ++e ) {
        const geometry_mesh_vertex_handle_t hV =
            GenerationPool_Insert( &pMesh->vertices, mesh_vertex_record_t{ eps.pData[e].position, {} } ).handle;
        (void)Vector_PushBack( &epVertex, hV );
    }
    for ( usize g = 0u; g < eps.nCount; ) {
        usize e = g;
        while ( e < eps.nCount && eps.pData[e].edgeKey == eps.pData[g].edgeKey ) { ++e; }
        SplitEdgeAt( pMesh, eps.pData[g].hEdge, epVertex.pData + g, static_cast<u32>( e - g ) );
        g = e;
    }
    for ( u32 i = 0u; i < n; ++i ) {
        const mesh_knife_point_t &p = path.pData[i];
        if ( p.kind == mesh_knife_point_kind_t::VERTEX ) { pointVertex.pData[i] = p.hVertex; }
        if ( p.kind == mesh_knife_point_kind_t::EDGE ) { pointVertex.pData[i] = epVertex.pData[keys.pData[i].value]; }
    }

    // ---- Mutation: face splits ----
    u32 cFacesCreated = 0u;
    for ( usize s = 0u; s < segs.nCount; ++s ) {
        const segment_t &seg = segs.pData[s];
        if ( seg.bSkip ) { continue; }
        const u32 k = seg.iTo - seg.iFrom - 1u;
        const geometry_mesh_vertex_handle_t hVA = pointVertex.pData[seg.iFrom];
        const geometry_mesh_vertex_handle_t hVB = pointVertex.pData[seg.iTo];
        mesh_face_record_t *pF = GenerationPool_Get( &pMesh->faces, seg.hFace );
        const geometry_mesh_loop_handle_t hL = pF->hOuterLoop;
        const mesh_face_record_t faceCopy = *pF;
        const mesh_loop_record_t *pL = GenerationPool_Get( &pMesh->loops, hL );
        const u32 cOld = pL->cHalfEdges;
        const geometry_mesh_half_edge_handle_t hA = FindOutInLoop( pMesh, pL, hVA );
        const geometry_mesh_half_edge_handle_t hB = FindOutInLoop( pMesh, pL, hVB );
        const geometry_mesh_half_edge_handle_t pa = He( pMesh, hA )->hPrev;
        const geometry_mesh_half_edge_handle_t pb = He( pMesh, hB )->hPrev;

        for ( u32 c = 0u; c < k; ++c ) {
            const u32 iPath = seg.iFrom + 1u + c;
            pointVertex.pData[iPath] =
                GenerationPool_Insert( &pMesh->vertices, mesh_vertex_record_t{ path.pData[iPath].position, {} } ).handle;
        }
        const geometry_mesh_face_handle_t hNewFace = GenerationPool_Insert( &pMesh->faces, mesh_face_record_t{} ).handle;
        const geometry_mesh_loop_handle_t hNewLoop = GenerationPool_Insert( &pMesh->loops, mesh_loop_record_t{} ).handle;

        // Chain c_0 = A, c_1..c_k, c_{k+1} = B. x_j (c_j -> c_{j+1}) goes to
        // the new face's loop, y_j (c_{j+1} -> c_j) to the kept loop.
        geometry_mesh_half_edge_handle_t prevX = pa, prevY = hA, x0{};
        for ( u32 c = 0u; c <= k; ++c ) {
            const geometry_mesh_vertex_handle_t from = c == 0u ? hVA : pointVertex.pData[seg.iFrom + c];
            const geometry_mesh_vertex_handle_t to = c == k ? hVB : pointVertex.pData[seg.iFrom + 1u + c];
            const geometry_mesh_edge_handle_t hR = GenerationPool_Insert( &pMesh->edges, mesh_edge_record_t{} ).handle;
            const geometry_mesh_half_edge_handle_t hX = GenerationPool_Insert( &pMesh->halfEdges, mesh_half_edge_record_t{} ).handle;
            const geometry_mesh_half_edge_handle_t hY = GenerationPool_Insert( &pMesh->halfEdges, mesh_half_edge_record_t{} ).handle;
            GenerationPool_Get( &pMesh->edges, hR )->hHalfEdge = hX;
            mesh_half_edge_record_t *pX = HeMut( pMesh, hX );
            pX->hOrigin = from;
            pX->hTwin = hY;
            pX->hPrev = prevX;
            pX->hEdge = hR;
            pX->hLoop = hNewLoop;
            HeMut( pMesh, prevX )->hNext = hX;
            mesh_half_edge_record_t *pY = HeMut( pMesh, hY );
            pY->hOrigin = to;
            pY->hTwin = hX;
            pY->hNext = prevY;
            pY->hEdge = hR;
            pY->hLoop = hL;
            HeMut( pMesh, prevY )->hPrev = hY;
            if ( c > 0u ) { GenerationPool_Get( &pMesh->vertices, from )->hOutHalfEdge = hX; }
            if ( c == 0u ) { x0 = hX; }
            prevX = hX;
            prevY = hY;
        }
        HeMut( pMesh, prevX )->hNext = hB;
        HeMut( pMesh, hB )->hPrev = prevX;
        HeMut( pMesh, prevY )->hPrev = pb;
        HeMut( pMesh, pb )->hNext = prevY;

        // The original run B..pa moves to the new loop.
        u32 cMoved = 0u;
        for ( geometry_mesh_half_edge_handle_t h = hB; !Same( h, x0 ); h = He( pMesh, h )->hNext ) {
            HeMut( pMesh, h )->hLoop = hNewLoop;
            ++cMoved;
        }
        mesh_loop_record_t *pKept = GenerationPool_Get( &pMesh->loops, hL );
        pKept->hFirstHalfEdge = hA;
        pKept->cHalfEdges = cOld - cMoved + k + 1u;
        mesh_loop_record_t *pNewLoop = GenerationPool_Get( &pMesh->loops, hNewLoop );
        pNewLoop->hFirstHalfEdge = hB;
        pNewLoop->hFace = hNewFace;
        pNewLoop->cHalfEdges = cMoved + k + 1u;
        mesh_face_record_t *pNewFace = GenerationPool_Get( &pMesh->faces, hNewFace );
        pNewFace->hOuterLoop = hNewLoop;
        pNewFace->hShell = faceCopy.hShell;
        pNewFace->normal = faceCopy.normal;
        pNewFace->iSourceSide = faceCopy.iSourceSide;
        GenerationPool_Get( &pMesh->shells, faceCopy.hShell )->cFaces += 1u;
        (void)Vector_PushBack( &touched, seg.hFace );
        (void)Vector_PushBack( &touched, hNewFace );
        if ( pSplitsOut != nullptr ) { (void)Vector_PushBack( pSplitsOut, mesh_knife_split_t{ seg.hFace, hNewFace } ); }
        ++cFacesCreated;
    }

    // Every vertex whose fan changed: edge points, and both ends of every
    // cut (a face split can move which of their outgoing half-edges starts
    // an open fan). Chain vertices are interior and already valid.
    for ( u32 i = 0u; i < n; ++i ) {
        if ( GeometryHandle_IsValid( pointVertex.pData[i] ) ) { fixMask.pData[pointVertex.pData[i].nSlot] = 1u; }
    }
    FixOutEdges( pMesh, fixMask );
    (void)Vector_Resize( &normalScratch, kFaceCornersMax );
    for ( usize t = 0u; t < touched.nCount; ++t ) {
        RecomputeNormal( pMesh, touched.pData[t], normalScratch.pData, kFaceCornersMax );
    }
    if ( pPointVerticesOut != nullptr ) {
        for ( u32 i = 0u; i < n; ++i ) { (void)Vector_PushBack( pPointVerticesOut, pointVertex.pData[i] ); }
    }

    r.cVerticesCreated = static_cast<u32>( cNewVertices );
    r.cEdgesCreated = static_cast<u32>( cNewEdges );
    r.cFacesCreated = cFacesCreated;
    r.status = geometry_status_t::OK;
    cleanup();
    return r;
}

} // namespace cypher::editor::geometry
