//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Euler.cpp
//  Purpose: Implements the Euler operators.
//  Details: MEF and KEF run on the existing MeshOps edits, wrapped with
//           the precondition checks and postcondition proofs the header
//           promises. SEMV and JEKV are done here as exact mirror images,
//           because MeshOps_SplitEdge refuses boundary edges (it expects
//           every half-edge to have a twin) and nothing joined edges at
//           all. SEMV reserves every record first, so the linking that
//           follows cannot fail; JEKV allocates nothing.
//
//           The whole-mesh check (bFullValidation) is MeshValidation minus
//           its twin test, plus a twin test that accepts boundary
//           half-edges: MeshValidation's version counts a missing twin as
//           broken, which would reject every open mesh.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Euler.h"
#include "CypherGeometry_MeshTopologyOps.h"
#include "CypherGeometry_MeshValidation.h"

#include <cmath>
#include <initializer_list>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

using he_t = geometry_mesh_half_edge_handle_t;

// Scratch bounds for walking one face or one vertex fan on the stack. A
// face or fan beyond them is refused (LIMIT_EXCEEDED / treated as joined)
// rather than half-checked.
constexpr u32 kCornersMax = 1024u;
constexpr u32 kFanMax = 1024u;

template <typename tag_t> bool Same( generation_handle_t<tag_t> a, generation_handle_t<tag_t> b ) noexcept
{
    return a.nSlot == b.nSlot && a.nGeneration == b.nGeneration;
}

f64 Len( math::vec3d_t v ) noexcept { return std::sqrt( math::Vec3d_LengthSquared( v ) ); }

template <typename tag_t> u64 Key( generation_handle_t<tag_t> h ) noexcept
{
    return ( static_cast<u64>( h.nSlot ) << 32u ) | h.nGeneration;
}

const mesh_half_edge_record_t *He( const editable_mesh_t *pMesh, he_t h ) noexcept { return GenerationPool_Get( &pMesh->halfEdges, h ); }
mesh_half_edge_record_t *HeMut( editable_mesh_t *pMesh, he_t h ) noexcept { return GenerationPool_Get( &pMesh->halfEdges, h ); }

geometry_mesh_vertex_handle_t Dest( const editable_mesh_t *pMesh, const mesh_half_edge_record_t &h ) noexcept
{
    const mesh_half_edge_record_t *pNext = He( pMesh, h.hNext );
    return pNext != nullptr ? pNext->hOrigin : geometry_mesh_vertex_handle_t{};
}

math::vec3d_t Pos( const editable_mesh_t *pMesh, geometry_mesh_vertex_handle_t v ) noexcept
{
    const mesh_vertex_record_t *p = GenerationPool_Get( &pMesh->vertices, v );
    return p != nullptr ? p->position : math::vec3d_t{};
}

// Half-edges leaving `v`, walking its fan from the stored out half-edge
// (a fan start: its prev has no twin on an open fan) through next(twin).
// *pBoundaryIn receives the incoming boundary half-edge of an open fan.
// Returns false when the walk does not close properly (corrupt links).
bool OutgoingFan( const editable_mesh_t *pMesh, geometry_mesh_vertex_handle_t v, he_t *pOut, u32 cCapacity, u32 *pCount,
                  he_t *pBoundaryIn ) noexcept
{
    *pCount = 0u;
    *pBoundaryIn = he_t{};
    const mesh_vertex_record_t *pV = GenerationPool_Get( &pMesh->vertices, v );
    if ( pV == nullptr ) { return false; }
    const he_t start = pV->hOutHalfEdge;
    const mesh_half_edge_record_t *pStart = He( pMesh, start );
    if ( pStart == nullptr ) { return false; }
    const mesh_half_edge_record_t *pPrev = He( pMesh, pStart->hPrev );
    if ( pPrev != nullptr && !GenerationPool_Contains( &pMesh->halfEdges, pPrev->hTwin ) ) { *pBoundaryIn = pStart->hPrev; }
    he_t h = start;
    for ( u32 guard = 0u; guard <= cCapacity; ++guard ) {
        const mesh_half_edge_record_t *p = He( pMesh, h );
        if ( p == nullptr || !Same( p->hOrigin, v ) ) { return false; }
        if ( *pCount >= cCapacity ) { return false; }
        pOut[( *pCount )++] = h;
        const mesh_half_edge_record_t *pTwin = He( pMesh, p->hTwin );
        if ( pTwin == nullptr ) { return true; } // open fan ends at an outgoing boundary edge
        h = pTwin->hNext;
        if ( Same( h, start ) ) { return true; }
    }
    return false;
}

// True when an edge already joins a and b.
bool Joined( const editable_mesh_t *pMesh, geometry_mesh_vertex_handle_t a, geometry_mesh_vertex_handle_t b ) noexcept
{
    he_t fan[kFanMax];
    u32 n = 0u;
    he_t boundaryIn{};
    // A fan that cannot be walked counts as joined, so callers refuse.
    if ( !OutgoingFan( pMesh, a, fan, kFanMax, &n, &boundaryIn ) ) { return true; }
    for ( u32 i = 0u; i < n; ++i ) {
        if ( Same( Dest( pMesh, *He( pMesh, fan[i] ) ), b ) ) { return true; }
    }
    const mesh_half_edge_record_t *pIn = He( pMesh, boundaryIn );
    return pIn != nullptr && Same( pIn->hOrigin, b );
}

// Twice the area vector of the polygon (Newell's method).
math::vec3d_t Newell( const math::vec3d_t *p, u32 n ) noexcept
{
    math::vec3d_t s{};
    for ( u32 i = 0u; i < n; ++i ) {
        const math::vec3d_t a = p[i], b = p[( i + 1u ) % n];
        s.x += ( a.y - b.y ) * ( a.z + b.z );
        s.y += ( a.z - b.z ) * ( a.x + b.x );
        s.z += ( a.x - b.x ) * ( a.y + b.y );
    }
    return s;
}

// A polygon has usable area when its Newell vector is not negligible
// against its size (scale-free: compares area to perimeter squared).
bool HasArea( const math::vec3d_t *p, u32 n ) noexcept
{
    if ( n < 3u ) { return false; }
    f64 perimeter = 0.0;
    for ( u32 i = 0u; i < n; ++i ) { perimeter += Len( math::Vec3d_Subtract( p[( i + 1u ) % n], p[i] ) ); }
    const f64 area2 = Len( Newell( p, n ) );
    return std::isfinite( area2 ) && area2 > 1e-12 * perimeter * perimeter;
}

// Loop corners in order, starting at the loop's first half-edge.
bool LoopVertices( const editable_mesh_t *pMesh, geometry_mesh_loop_handle_t hLoop, geometry_mesh_vertex_handle_t *pOut, u32 cCapacity,
                   u32 *pCount ) noexcept
{
    const mesh_loop_record_t *pL = GenerationPool_Get( &pMesh->loops, hLoop );
    if ( pL == nullptr || pL->cHalfEdges > cCapacity ) { return false; }
    he_t h = pL->hFirstHalfEdge;
    for ( u32 i = 0u; i < pL->cHalfEdges; ++i ) {
        const mesh_half_edge_record_t *p = He( pMesh, h );
        if ( p == nullptr ) { return false; }
        pOut[i] = p->hOrigin;
        h = p->hNext;
    }
    *pCount = pL->cHalfEdges;
    return Same( h, pL->hFirstHalfEdge );
}

// Corner count of a loop, or 0 for a stale loop.
u32 LoopSize( const editable_mesh_t *pMesh, geometry_mesh_loop_handle_t hLoop ) noexcept
{
    const mesh_loop_record_t *pL = GenerationPool_Get( &pMesh->loops, hLoop );
    return pL != nullptr ? pL->cHalfEdges : 0u;
}

geometry_mesh_loop_handle_t LoopOfFace( const editable_mesh_t *pMesh, geometry_mesh_face_handle_t f ) noexcept
{
    const mesh_face_record_t *p = GenerationPool_Get( &pMesh->faces, f );
    return p != nullptr ? p->hOuterLoop : geometry_mesh_loop_handle_t{};
}

// A loop is closed, its cached count is exact, every half-edge names it,
// next/prev agree, and twins are reciprocal on one edge.
bool LoopSound( const editable_mesh_t *pMesh, geometry_mesh_loop_handle_t hLoop ) noexcept
{
    const mesh_loop_record_t *pL = GenerationPool_Get( &pMesh->loops, hLoop );
    if ( pL == nullptr || pL->cHalfEdges < 3u ) { return false; }
    he_t h = pL->hFirstHalfEdge;
    for ( u32 i = 0u; i < pL->cHalfEdges; ++i ) {
        const mesh_half_edge_record_t *p = He( pMesh, h );
        if ( p == nullptr || !Same( p->hLoop, hLoop ) ) { return false; }
        const mesh_half_edge_record_t *pNext = He( pMesh, p->hNext );
        if ( pNext == nullptr || !Same( pNext->hPrev, h ) ) { return false; }
        if ( !GenerationPool_Contains( &pMesh->edges, p->hEdge ) ) { return false; }
        if ( const mesh_half_edge_record_t *pT = He( pMesh, p->hTwin ) ) {
            if ( !Same( pT->hTwin, h ) || !Same( pT->hEdge, p->hEdge ) || !Same( pT->hOrigin, pNext->hOrigin ) ) { return false; }
        }
        h = p->hNext;
    }
    return Same( h, pL->hFirstHalfEdge );
}

bool CountsMatch( const euler_counts_t &before, const euler_counts_t &after, const euler_counts_t &delta ) noexcept
{
    return after.cVertices - before.cVertices == delta.cVertices && after.cEdges - before.cEdges == delta.cEdges &&
           after.cFaces - before.cFaces == delta.cFaces && after.cShells - before.cShells == delta.cShells;
}

bool FullValid( const editable_mesh_t *pMesh ) noexcept
{
    const mesh_validation_result_t v = MeshValidation_Validate( pMesh );
    if ( !( v.bClosedLoops && v.bAllVerticesReferenced && v.bEdgeLinks && v.bShellLinks ) ) { return false; }
    bool bOk = true;
    (void)GenerationPool_ForEach( &pMesh->halfEdges, [&]( he_t h, const mesh_half_edge_record_t &r ) noexcept -> bool_t {
        if ( const mesh_half_edge_record_t *pT = He( pMesh, r.hTwin ) ) {
            bOk = Same( pT->hTwin, h ) && Same( pT->hEdge, r.hEdge ) && Same( pT->hOrigin, Dest( pMesh, r ) );
        } else {
            // A boundary half-edge must be its edge's only side.
            const mesh_edge_record_t *pE = GenerationPool_Get( &pMesh->edges, r.hEdge );
            bOk = pE != nullptr && Same( pE->hHalfEdge, h );
        }
        return bOk;
    } );
    return bOk;
}

// Makes room for `extra` more records without touching live ones
// (retired slots cannot be reused, so they count as occupied).
template <typename record_t, typename tag_t>
geometry_status_t ReserveMore( generation_pool_t<record_t, tag_t> *pPool, usize extra, usize limit ) noexcept
{
    const usize cOccupied = pPool->cRecords + pPool->cRetiredSlots;
    if ( pPool->cRecords + extra > limit || cOccupied + extra > pPool->cSlotLimit ) { return geometry_status_t::LIMIT_EXCEEDED; }
    const generation_pool_status_t st = GenerationPool_Reserve( pPool, cOccupied + extra );
    if ( st != generation_pool_status_t::OK ) { return GeometryStatus_FromGenerationPoolStatus( st ); }
    return pPool->cSlots - cOccupied >= extra ? geometry_status_t::OK : geometry_status_t::ALLOCATION_FAILED;
}

void RecomputeNormal( editable_mesh_t *pMesh, geometry_mesh_face_handle_t f ) noexcept
{
    geometry_mesh_vertex_handle_t vs[kCornersMax];
    math::vec3d_t ps[kCornersMax];
    u32 n = 0u;
    mesh_face_record_t *pF = GenerationPool_Get( &pMesh->faces, f );
    if ( pF == nullptr || !LoopVertices( pMesh, pF->hOuterLoop, vs, kCornersMax, &n ) ) { return; }
    for ( u32 i = 0u; i < n; ++i ) { ps[i] = Pos( pMesh, vs[i] ); }
    const math::vec3d_t nv = Newell( ps, n );
    const f64 len = Len( nv );
    if ( len > 0.0 && std::isfinite( len ) ) { pF->normal = math::Vec3d_Scale( nv, 1.0 / len ); }
}

// The two faces through a vertex's out half-edges and the half-edge
// entering the vertex in each (JEKV works on these pairs).
struct jekv_plan_t {
    he_t out[2]{};
    he_t in[2]{};
    u32 cFaces{ 0u };
    geometry_mesh_edge_handle_t keep{}, kill{};
    geometry_mesh_vertex_handle_t a{}, b{};
};

geometry_status_t PlanJoinEdges( const editable_mesh_t *pMesh, geometry_mesh_vertex_handle_t v, jekv_plan_t *pPlan ) noexcept
{
    if ( !GenerationPool_Contains( &pMesh->vertices, v ) ) { return geometry_status_t::INVALID_HANDLE; }
    he_t fan[3];
    u32 n = 0u;
    he_t boundaryIn{};
    if ( !OutgoingFan( pMesh, v, fan, 3u, &n, &boundaryIn ) ) {
        // More than two outgoing half-edges means more than two edges.
        return n >= 3u ? geometry_status_t::INVALID_ARGUMENT : geometry_status_t::CORRUPT_STATE;
    }
    const bool bOpen = GenerationPool_Contains( &pMesh->halfEdges, boundaryIn );
    const u32 cEdges = n + ( bOpen ? 1u : 0u );
    // Two edges: an interior vertex with two faces, or a boundary vertex
    // inside one face.
    if ( cEdges != 2u ) { return geometry_status_t::INVALID_ARGUMENT; }
    pPlan->cFaces = n;
    for ( u32 i = 0u; i < n; ++i ) {
        pPlan->out[i] = fan[i];
        pPlan->in[i] = He( pMesh, fan[i] )->hPrev;
    }
    // One face on both sides of the vertex (a slit) would lose two corners
    // from one loop; that is not the simple join this operator is.
    if ( n == 2u && Same( He( pMesh, fan[0] )->hLoop, He( pMesh, fan[1] )->hLoop ) ) { return geometry_status_t::NON_MANIFOLD; }
    const mesh_half_edge_record_t &o0 = *He( pMesh, pPlan->out[0] );
    const mesh_half_edge_record_t &i0 = *He( pMesh, pPlan->in[0] );
    pPlan->a = i0.hOrigin;
    pPlan->b = Dest( pMesh, o0 );
    if ( Same( pPlan->a, pPlan->b ) || Same( pPlan->a, v ) || Same( pPlan->b, v ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    // Keep the edge with the lower key so the choice is deterministic.
    const bool bKeepIn = Key( i0.hEdge ) < Key( o0.hEdge );
    pPlan->keep = bKeepIn ? i0.hEdge : o0.hEdge;
    pPlan->kill = bKeepIn ? o0.hEdge : i0.hEdge;
    // Every face through v keeps three corners and area once v is gone
    // (checked first: a triangle's corner is degenerate before anything).
    geometry_mesh_vertex_handle_t vs[kCornersMax];
    math::vec3d_t ps[kCornersMax];
    for ( u32 i = 0u; i < n; ++i ) {
        u32 c = 0u, k = 0u;
        if ( LoopSize( pMesh, He( pMesh, pPlan->out[i] )->hLoop ) > kCornersMax ) { return geometry_status_t::LIMIT_EXCEEDED; }
        if ( !LoopVertices( pMesh, He( pMesh, pPlan->out[i] )->hLoop, vs, kCornersMax, &c ) ) { return geometry_status_t::CORRUPT_STATE; }
        if ( c < 4u ) { return geometry_status_t::DEGENERATE; }
        for ( u32 j = 0u; j < c; ++j ) {
            if ( !Same( vs[j], v ) ) { ps[k++] = Pos( pMesh, vs[j] ); }
        }
        if ( !HasArea( ps, k ) ) { return geometry_status_t::DEGENERATE; }
    }
    if ( Joined( pMesh, pPlan->a, pPlan->b ) ) { return geometry_status_t::NON_MANIFOLD; }
    return geometry_status_t::OK;
}

} // namespace

euler_counts_t Euler_Counts( const editable_mesh_t *pMesh ) noexcept
{
    if ( pMesh == nullptr ) { return euler_counts_t{}; }
    return euler_counts_t{ static_cast<i64>( EditableMesh_VertexCount( pMesh ) ), static_cast<i64>( EditableMesh_EdgeCount( pMesh ) ),
                           static_cast<i64>( EditableMesh_FaceCount( pMesh ) ), static_cast<i64>( EditableMesh_ShellCount( pMesh ) ) };
}

euler_counts_t Euler_Delta( euler_operator_t op ) noexcept
{
    switch ( op ) {
    case euler_operator_t::SEMV: return euler_counts_t{ 1, 1, 0, 0 };
    case euler_operator_t::JEKV: return euler_counts_t{ -1, -1, 0, 0 };
    case euler_operator_t::MEF: return euler_counts_t{ 0, 1, 1, 0 };
    case euler_operator_t::KEF: return euler_counts_t{ 0, -1, -1, 0 };
    }
    return euler_counts_t{};
}

euler_operator_t Euler_Inverse( euler_operator_t op ) noexcept
{
    switch ( op ) {
    case euler_operator_t::SEMV: return euler_operator_t::JEKV;
    case euler_operator_t::JEKV: return euler_operator_t::SEMV;
    case euler_operator_t::MEF: return euler_operator_t::KEF;
    case euler_operator_t::KEF: return euler_operator_t::MEF;
    }
    return op;
}

// ---------------------------------------------------------------------------
// SEMV
// ---------------------------------------------------------------------------

geometry_status_t Euler_CheckSplitEdge( const editable_mesh_t *pMesh, geometry_mesh_edge_handle_t hEdge, f64 t ) noexcept
{
    if ( pMesh == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    const mesh_edge_record_t *pE = GenerationPool_Get( &pMesh->edges, hEdge );
    if ( pE == nullptr ) { return geometry_status_t::INVALID_HANDLE; }
    if ( !std::isfinite( t ) || !( t > 0.0 ) || !( t < 1.0 ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    const mesh_half_edge_record_t *pH = He( pMesh, pE->hHalfEdge );
    if ( pH == nullptr ) { return geometry_status_t::CORRUPT_STATE; }
    const math::vec3d_t p0 = Pos( pMesh, pH->hOrigin ), p1 = Pos( pMesh, Dest( pMesh, *pH ) );
    if ( !( Len( math::Vec3d_Subtract( p1, p0 ) ) > 0.0 ) ) { return geometry_status_t::DEGENERATE; }
    const bool bTwin = GenerationPool_Contains( &pMesh->halfEdges, pH->hTwin );
    if ( EditableMesh_VertexCount( pMesh ) + 1u > pMesh->limits.cVertexMax || EditableMesh_EdgeCount( pMesh ) + 1u > pMesh->limits.cEdgeMax ||
         EditableMesh_HalfEdgeCount( pMesh ) + ( bTwin ? 2u : 1u ) > pMesh->limits.cHalfEdgeMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    return geometry_status_t::OK;
}

euler_semv_result_t Euler_SplitEdge( editable_mesh_t *pMesh, geometry_mesh_edge_handle_t hEdge, f64 t, bool bFullValidation ) noexcept
{
    euler_semv_result_t r{};
    r.status = Euler_CheckSplitEdge( pMesh, hEdge, t );
    if ( r.status != geometry_status_t::OK ) { return r; }
    // Copy what the linking needs: reserving may move pool records.
    const he_t h = GenerationPool_Get( &pMesh->edges, hEdge )->hHalfEdge;
    const he_t tw = He( pMesh, h )->hTwin;
    const bool bTwin = GenerationPool_Contains( &pMesh->halfEdges, tw );
    const geometry_mesh_vertex_handle_t a = He( pMesh, h )->hOrigin, b = Dest( pMesh, *He( pMesh, h ) );
    const math::vec3d_t pos = math::Vec3d_Lerp( Pos( pMesh, a ), Pos( pMesh, b ), t );
    if ( !math::Vec3d_IsFinite( pos ) || math::Vec3d_EqualsExact( pos, Pos( pMesh, a ) ) || math::Vec3d_EqualsExact( pos, Pos( pMesh, b ) ) ) {
        // An interior t can still round onto an endpoint: a zero-length edge.
        r.status = geometry_status_t::DEGENERATE;
        return r;
    }
    const f64 crease = GenerationPool_Get( &pMesh->edges, hEdge )->creaseWeight;
    geometry_status_t st = ReserveMore( &pMesh->vertices, 1u, pMesh->limits.cVertexMax );
    if ( st == geometry_status_t::OK ) { st = ReserveMore( &pMesh->edges, 1u, pMesh->limits.cEdgeMax ); }
    if ( st == geometry_status_t::OK ) { st = ReserveMore( &pMesh->halfEdges, bTwin ? 2u : 1u, pMesh->limits.cHalfEdgeMax ); }
    if ( st != geometry_status_t::OK ) {
        r.status = st;
        return r;
    }
    const euler_counts_t before = Euler_Counts( pMesh );

    // h (a -> b) becomes a -> m and h2 (m -> b) follows it; on the other
    // side tw (b -> a) becomes b -> m and t2 (m -> a) follows it. The
    // original edge keeps {h, t2}; the new edge is {h2, tw}.
    mesh_vertex_record_t mv{};
    mv.position = pos;
    const geometry_mesh_vertex_handle_t m = GenerationPool_Insert( &pMesh->vertices, mv ).handle;
    mesh_edge_record_t ne{};
    ne.creaseWeight = crease;
    const geometry_mesh_edge_handle_t e2 = GenerationPool_Insert( &pMesh->edges, ne ).handle;
    const he_t h2 = GenerationPool_Insert( &pMesh->halfEdges, mesh_half_edge_record_t{} ).handle;
    const he_t t2 = bTwin ? GenerationPool_Insert( &pMesh->halfEdges, mesh_half_edge_record_t{} ).handle : he_t{};

    auto insertAfter = [&]( he_t first, he_t second ) noexcept {
        mesh_half_edge_record_t *pF = HeMut( pMesh, first );
        mesh_half_edge_record_t *pS = HeMut( pMesh, second );
        pS->hOrigin = m;
        pS->hPrev = first;
        pS->hNext = pF->hNext;
        pS->hLoop = pF->hLoop;
        HeMut( pMesh, pF->hNext )->hPrev = second;
        pF->hNext = second;
        GenerationPool_Get( &pMesh->loops, pF->hLoop )->cHalfEdges += 1u;
    };
    insertAfter( h, h2 );
    if ( bTwin ) { insertAfter( tw, t2 ); }
    mesh_half_edge_record_t *pH = HeMut( pMesh, h );
    mesh_half_edge_record_t *pH2 = HeMut( pMesh, h2 );
    pH2->hEdge = e2;
    if ( bTwin ) {
        mesh_half_edge_record_t *pT = HeMut( pMesh, tw );
        mesh_half_edge_record_t *pT2 = HeMut( pMesh, t2 );
        pH->hTwin = t2;
        pT2->hTwin = h;
        pT2->hEdge = hEdge;
        pH2->hTwin = tw;
        pT->hTwin = h2;
        pT->hEdge = e2;
    }
    GenerationPool_Get( &pMesh->edges, hEdge )->hHalfEdge = h;
    GenerationPool_Get( &pMesh->edges, e2 )->hHalfEdge = h2;
    // m's fan starts at h2: on a boundary its prev (h) has no twin, which
    // is the open-fan convention; inside, any out half-edge will do.
    GenerationPool_Get( &pMesh->vertices, m )->hOutHalfEdge = h2;

    r.hNewVertex = m;
    r.hNewEdge = e2;
    bool bOk = CountsMatch( before, Euler_Counts( pMesh ), Euler_Delta( euler_operator_t::SEMV ) ) && LoopSound( pMesh, He( pMesh, h )->hLoop );
    if ( bTwin ) { bOk = bOk && LoopSound( pMesh, He( pMesh, tw )->hLoop ); }
    if ( bOk && bFullValidation ) { bOk = FullValid( pMesh ); }
    r.status = bOk ? geometry_status_t::OK : geometry_status_t::CORRUPT_STATE;
    return r;
}

// ---------------------------------------------------------------------------
// JEKV
// ---------------------------------------------------------------------------

geometry_status_t Euler_CheckJoinEdges( const editable_mesh_t *pMesh, geometry_mesh_vertex_handle_t hVertex ) noexcept
{
    if ( pMesh == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    jekv_plan_t plan{};
    return PlanJoinEdges( pMesh, hVertex, &plan );
}

euler_jekv_result_t Euler_JoinEdges( editable_mesh_t *pMesh, geometry_mesh_vertex_handle_t hVertex, bool bFullValidation ) noexcept
{
    euler_jekv_result_t r{};
    if ( pMesh == nullptr ) { return r; }
    jekv_plan_t plan{};
    r.status = PlanJoinEdges( pMesh, hVertex, &plan );
    if ( r.status != geometry_status_t::OK ) { return r; }
    const euler_counts_t before = Euler_Counts( pMesh );

    // In each face: `in` (a -> v) survives and now ends where `out`
    // (v -> b) ended; `out` is unlinked.
    geometry_mesh_loop_handle_t loops[2]{};
    geometry_mesh_face_handle_t faces[2]{};
    for ( u32 i = 0u; i < plan.cFaces; ++i ) {
        mesh_half_edge_record_t *pIn = HeMut( pMesh, plan.in[i] );
        mesh_half_edge_record_t *pOut = HeMut( pMesh, plan.out[i] );
        mesh_half_edge_record_t *pAfter = HeMut( pMesh, pOut->hNext );
        pIn->hNext = pOut->hNext;
        pAfter->hPrev = plan.in[i];
        mesh_loop_record_t *pLoop = GenerationPool_Get( &pMesh->loops, pOut->hLoop );
        pLoop->cHalfEdges -= 1u;
        if ( Same( pLoop->hFirstHalfEdge, plan.out[i] ) ) { pLoop->hFirstHalfEdge = plan.in[i]; }
        loops[i] = pOut->hLoop;
        faces[i] = pLoop->hFace;
    }
    // The surviving incoming half-edges are the new edge's two sides.
    if ( plan.cFaces == 2u ) {
        mesh_half_edge_record_t *p0 = HeMut( pMesh, plan.in[0] );
        mesh_half_edge_record_t *p1 = HeMut( pMesh, plan.in[1] );
        p0->hTwin = plan.in[1];
        p1->hTwin = plan.in[0];
        p0->hEdge = plan.keep;
        p1->hEdge = plan.keep;
    } else {
        HeMut( pMesh, plan.in[0] )->hEdge = plan.keep;
    }
    GenerationPool_Get( &pMesh->edges, plan.keep )->hHalfEdge = plan.in[0];
    // Neither neighbour's out half-edge can be one of the removed ones
    // (those all start at v), and each remaining half-edge's prev keeps its
    // boundary status, so both fans keep a valid start.
    for ( u32 i = 0u; i < plan.cFaces; ++i ) { (void)GenerationPool_Remove( &pMesh->halfEdges, plan.out[i] ); }
    (void)GenerationPool_Remove( &pMesh->edges, plan.kill );
    (void)GenerationPool_Remove( &pMesh->vertices, hVertex );
    for ( u32 i = 0u; i < plan.cFaces; ++i ) { RecomputeNormal( pMesh, faces[i] ); }

    bool bOk = CountsMatch( before, Euler_Counts( pMesh ), Euler_Delta( euler_operator_t::JEKV ) ) &&
               !GenerationPool_Contains( &pMesh->vertices, hVertex ) && !GenerationPool_Contains( &pMesh->edges, plan.kill ) &&
               GenerationPool_Contains( &pMesh->edges, plan.keep );
    for ( u32 i = 0u; i < plan.cFaces; ++i ) { bOk = bOk && LoopSound( pMesh, loops[i] ) && !GenerationPool_Contains( &pMesh->halfEdges, plan.out[i] ); }
    if ( bOk && bFullValidation ) { bOk = FullValid( pMesh ); }
    r.hSurvivingEdge = plan.keep;
    r.status = bOk ? geometry_status_t::OK : geometry_status_t::CORRUPT_STATE;
    return r;
}

// ---------------------------------------------------------------------------
// MEF
// ---------------------------------------------------------------------------

geometry_status_t Euler_CheckSplitFace( const editable_mesh_t *pMesh, geometry_mesh_face_handle_t hFace, geometry_mesh_vertex_handle_t hA,
                                        geometry_mesh_vertex_handle_t hB ) noexcept
{
    if ( pMesh == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( !GenerationPool_Contains( &pMesh->faces, hFace ) || !GenerationPool_Contains( &pMesh->vertices, hA ) ||
         !GenerationPool_Contains( &pMesh->vertices, hB ) ) {
        return geometry_status_t::INVALID_HANDLE;
    }
    if ( Same( hA, hB ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    geometry_mesh_vertex_handle_t vs[kCornersMax];
    u32 n = 0u;
    if ( LoopSize( pMesh, LoopOfFace( pMesh, hFace ) ) > kCornersMax ) { return geometry_status_t::LIMIT_EXCEEDED; }
    if ( !LoopVertices( pMesh, LoopOfFace( pMesh, hFace ), vs, kCornersMax, &n ) ) { return geometry_status_t::CORRUPT_STATE; }
    u32 ia = n, ib = n;
    for ( u32 i = 0u; i < n; ++i ) {
        if ( Same( vs[i], hA ) ) { ia = i; }
        if ( Same( vs[i], hB ) ) { ib = i; }
    }
    if ( ia == n || ib == n ) { return geometry_status_t::INVALID_ARGUMENT; }
    const u32 gap = ia < ib ? ib - ia : ia - ib;
    if ( gap == 1u || gap == n - 1u ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( Joined( pMesh, hA, hB ) ) { return geometry_status_t::NON_MANIFOLD; }
    // Both halves (A..B and B..A around the loop) need area.
    math::vec3d_t ps[kCornersMax];
    for ( const u32 from : { ia, ib } ) {
        const u32 to = from == ia ? ib : ia;
        u32 k = 0u;
        for ( u32 i = from;; i = ( i + 1u ) % n ) {
            ps[k++] = Pos( pMesh, vs[i] );
            if ( i == to ) { break; }
        }
        if ( !HasArea( ps, k ) ) { return geometry_status_t::DEGENERATE; }
    }
    if ( EditableMesh_EdgeCount( pMesh ) + 1u > pMesh->limits.cEdgeMax || EditableMesh_FaceCount( pMesh ) + 1u > pMesh->limits.cFaceMax ||
         EditableMesh_LoopCount( pMesh ) + 1u > pMesh->limits.cLoopMax || EditableMesh_HalfEdgeCount( pMesh ) + 2u > pMesh->limits.cHalfEdgeMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    return geometry_status_t::OK;
}

euler_mef_result_t Euler_SplitFace( editable_mesh_t *pMesh, geometry_mesh_face_handle_t hFace, geometry_mesh_vertex_handle_t hA,
                                    geometry_mesh_vertex_handle_t hB, bool bFullValidation ) noexcept
{
    euler_mef_result_t r{};
    r.status = Euler_CheckSplitFace( pMesh, hFace, hA, hB );
    if ( r.status != geometry_status_t::OK ) { return r; }
    const euler_counts_t before = Euler_Counts( pMesh );
    const mesh_split_face_result_t s = MeshOps_SplitFace( pMesh, hFace, hA, hB );
    if ( s.status != geometry_status_t::OK ) {
        r.status = s.status;
        return r;
    }
    r.hNewEdge = s.hNewEdge;
    r.hNewFace = s.hNewFace;
    bool bOk = CountsMatch( before, Euler_Counts( pMesh ), Euler_Delta( euler_operator_t::MEF ) ) &&
               GenerationPool_Contains( &pMesh->faces, hFace ) && GenerationPool_Contains( &pMesh->faces, s.hNewFace ) &&
               GenerationPool_Contains( &pMesh->edges, s.hNewEdge ) && LoopSound( pMesh, LoopOfFace( pMesh, hFace ) ) &&
               LoopSound( pMesh, LoopOfFace( pMesh, s.hNewFace ) );
    if ( bOk && bFullValidation ) { bOk = FullValid( pMesh ); }
    r.status = bOk ? geometry_status_t::OK : geometry_status_t::CORRUPT_STATE;
    return r;
}

// ---------------------------------------------------------------------------
// KEF
// ---------------------------------------------------------------------------

geometry_status_t Euler_CheckJoinFaces( const editable_mesh_t *pMesh, geometry_mesh_edge_handle_t hEdge ) noexcept
{
    if ( pMesh == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    const mesh_edge_record_t *pE = GenerationPool_Get( &pMesh->edges, hEdge );
    if ( pE == nullptr ) { return geometry_status_t::INVALID_HANDLE; }
    const mesh_half_edge_record_t *pH = He( pMesh, pE->hHalfEdge );
    if ( pH == nullptr ) { return geometry_status_t::CORRUPT_STATE; }
    const mesh_half_edge_record_t *pT = He( pMesh, pH->hTwin );
    if ( pT == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( Same( pH->hLoop, pT->hLoop ) ) { return geometry_status_t::NON_MANIFOLD; }
    // The faces may share only this edge's two corners.
    geometry_mesh_vertex_handle_t va[kCornersMax], vb[kCornersMax];
    u32 na = 0u, nb = 0u;
    if ( LoopSize( pMesh, pH->hLoop ) > kCornersMax || LoopSize( pMesh, pT->hLoop ) > kCornersMax ) { return geometry_status_t::LIMIT_EXCEEDED; }
    if ( !LoopVertices( pMesh, pH->hLoop, va, kCornersMax, &na ) || !LoopVertices( pMesh, pT->hLoop, vb, kCornersMax, &nb ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    u32 cShared = 0u;
    for ( u32 i = 0u; i < na; ++i ) {
        for ( u32 j = 0u; j < nb; ++j ) { cShared += Same( va[i], vb[j] ) ? 1u : 0u; }
    }
    return cShared == 2u ? geometry_status_t::OK : geometry_status_t::NON_MANIFOLD;
}

euler_kef_result_t Euler_JoinFaces( editable_mesh_t *pMesh, geometry_mesh_edge_handle_t hEdge, bool bFullValidation ) noexcept
{
    euler_kef_result_t r{};
    r.status = Euler_CheckJoinFaces( pMesh, hEdge );
    if ( r.status != geometry_status_t::OK ) { return r; }
    const mesh_half_edge_record_t &h = *He( pMesh, GenerationPool_Get( &pMesh->edges, hEdge )->hHalfEdge );
    const geometry_mesh_face_handle_t f0 = GenerationPool_Get( &pMesh->loops, h.hLoop )->hFace;
    const geometry_mesh_face_handle_t f1 = GenerationPool_Get( &pMesh->loops, He( pMesh, h.hTwin )->hLoop )->hFace;
    const euler_counts_t before = Euler_Counts( pMesh );
    r.status = MeshOps_DissolveEdge( pMesh, hEdge );
    if ( r.status != geometry_status_t::OK ) { return r; }
    const bool b0 = GenerationPool_Contains( &pMesh->faces, f0 ), b1 = GenerationPool_Contains( &pMesh->faces, f1 );
    r.hSurvivingFace = b0 ? f0 : f1;
    r.hKilledFace = b0 ? f1 : f0;
    bool bOk = CountsMatch( before, Euler_Counts( pMesh ), Euler_Delta( euler_operator_t::KEF ) ) && ( b0 != b1 ) &&
               !GenerationPool_Contains( &pMesh->edges, hEdge ) && LoopSound( pMesh, LoopOfFace( pMesh, r.hSurvivingFace ) );
    if ( bOk && bFullValidation ) { bOk = FullValid( pMesh ); }
    r.status = bOk ? geometry_status_t::OK : geometry_status_t::CORRUPT_STATE;
    return r;
}

} // namespace cypher::editor::geometry
