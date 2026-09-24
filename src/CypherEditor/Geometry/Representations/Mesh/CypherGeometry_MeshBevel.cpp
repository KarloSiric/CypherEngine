//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshBevel.cpp
//  Purpose: Implements the edge bevel as a read-only construction of the new
//           face rings followed by one MeshBoundary_ReplaceFaces call.
//  Details: Construction, per touched vertex v (fan F_0..F_{k-1}, where F_i
//           lies between edges E_i and E_{i+1}, d_i the unit direction of
//           E_i away from v):
//             1. boundary corners (BVs) - where v's corner moves to in each
//                face (miter point, or a point slid along a side edge);
//             2. profiles - for every beveled edge, the arc between the two
//                BVs on either side of it, computed once and shared by
//                everything that runs along it (strip end, end cap, the
//                neighbouring strip, corner patch), so shared vertices are
//                the same vertices rather than equal-looking copies;
//             3. corner patches where three or more beveled edges meet.
//           Then every face around a touched vertex is re-emitted with v
//           replaced by its BV (or by BV + profile + BV on an end cap), and
//           each beveled edge emits its strip between the profiles at its
//           two ends.
//
//           The profile between BVs A and B with corner K is the affine
//           quarter circle P(t) = C + (A - C) cos t + (B - C) sin t with
//           C = A + B - K: exactly circular when A - K and B - K are
//           perpendicular and equally long (a box edge), and always passing
//           through A and B tangent to the faces there. K is v itself at an
//           edge end or in a two-edge chain (so both strips of a chain meet
//           on the miter plane), and v's projection onto the edge line at a
//           corner with three or more beveled edges (so each strip ends in
//           its own cross-section plane and the patch closes the corner).
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshBevel.h"
#include "CypherGeometry_MeshBoundaryOps.h"
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

constexpr u32 kFanMax = 4096u;
constexpr f64 kHalfPi = 1.57079632679489661923;

struct fan_entry_t {
    geometry_mesh_half_edge_handle_t hOut{}; // v -> x_i, in F_i
    geometry_mesh_face_handle_t hFace{};     // F_i
    geometry_mesh_edge_handle_t hEdge{};     // E_i
    math::vec3d_t dir{};                     // unit, v -> x_i
    bool bBeveled{ false };
};

// The corners that replace vertex v in face F (indices into positions).
struct replace_t {
    u64 faceKey{ 0u };
    u64 vertexKey{ 0u };
    u32 iFirst{ 0u }; // into replIdx
    u32 count{ 0u };
};

bool ReplaceLess( const replace_t &a, const replace_t &b ) noexcept
{
    return a.faceKey != b.faceKey ? a.faceKey < b.faceKey : a.vertexKey < b.vertexKey;
}

// The arc between two BVs at one vertex; interior points are stored from
// bvLo to bvHi.
struct profile_t {
    u64 vertexKey{ 0u };
    u32 bvLo{ 0u };
    u32 bvHi{ 0u };
    u32 iFirstInterior{ 0u };
    math::vec3d_t center{}; // C = A + B - K
};

bool ProfileLess( const profile_t &a, const profile_t &b ) noexcept
{
    if ( a.vertexKey != b.vertexKey ) { return a.vertexKey < b.vertexKey; }
    return a.bvLo != b.bvLo ? a.bvLo < b.bvLo : a.bvHi < b.bvHi;
}

struct patch_t {
    u32 iFirst{ 0u }; // ring into patchIdx
    u32 count{ 0u };
    u32 iCenter{ CY_INVALID_INDEX }; // rounded corners: the fan's apex
    geometry_mesh_face_handle_t hSource{};
};

struct build_t {
    vector_t<math::vec3d_t> positions{};
    vector_t<u32> replIdx{};
    vector_t<replace_t> repl{};
    vector_t<profile_t> profiles{};
    vector_t<u32> patchIdx{};
    vector_t<patch_t> patches{};
    vector_t<fan_entry_t> fan{};
    vector_t<geometry_mesh_face_handle_t> removed{};
    bool bOk{ true }; // false once any push failed

    void Init( const allocator_t *pA ) noexcept
    {
        bOk = Vector_Init( &positions, pA ) && Vector_Init( &replIdx, pA ) && Vector_Init( &repl, pA ) &&
              Vector_Init( &profiles, pA ) && Vector_Init( &patchIdx, pA ) && Vector_Init( &patches, pA ) &&
              Vector_Init( &fan, pA ) && Vector_Init( &removed, pA );
    }
    void Shutdown() noexcept
    {
        Vector_Shutdown( &positions );
        Vector_Shutdown( &replIdx );
        Vector_Shutdown( &repl );
        Vector_Shutdown( &profiles );
        Vector_Shutdown( &patchIdx );
        Vector_Shutdown( &patches );
        Vector_Shutdown( &fan );
        Vector_Shutdown( &removed );
    }
    u32 AddPosition( math::vec3d_t p ) noexcept
    {
        bOk = bOk && Vector_PushBack( &positions, p );
        return static_cast<u32>( positions.nCount - 1u );
    }
    void AddReplace( geometry_mesh_face_handle_t hF, geometry_mesh_vertex_handle_t hV, const u32 *pIdx, u32 c ) noexcept
    {
        const u32 iFirst = static_cast<u32>( replIdx.nCount );
        for ( u32 i = 0u; i < c; ++i ) { bOk = bOk && Vector_PushBack( &replIdx, pIdx[i] ); }
        bOk = bOk && Vector_PushBack( &repl, replace_t{ Key( hF ), Key( hV ), iFirst, c } );
    }
};

const replace_t *FindReplace( const build_t &b, geometry_mesh_face_handle_t hF, u64 vertexKey ) noexcept
{
    const replace_t probe{ Key( hF ), vertexKey, 0u, 0u };
    const replace_t *pBegin = b.repl.pData, *pEnd = b.repl.pData + b.repl.nCount;
    const replace_t *p = std::lower_bound( pBegin, pEnd, probe, ReplaceLess );
    return ( p != pEnd && p->faceKey == probe.faceKey && p->vertexKey == vertexKey ) ? p : nullptr;
}

math::vec3d_t Sub( math::vec3d_t a, math::vec3d_t b ) noexcept { return math::Vec3d_Subtract( a, b ); }
math::vec3d_t Add( math::vec3d_t a, math::vec3d_t b ) noexcept { return math::Vec3d_Add( a, b ); }
math::vec3d_t Scale( math::vec3d_t a, f64 s ) noexcept { return math::Vec3d_Scale( a, s ); }
f64 Length( math::vec3d_t a ) noexcept { return std::sqrt( math::Vec3d_LengthSquared( a ) ); }

// Sine of the angle between two unit vectors.
f64 SinBetween( math::vec3d_t a, math::vec3d_t b ) noexcept { return Length( math::Vec3d_Cross( a, b ) ); }

// The closed fan around v, starting at its stored outgoing half-edge and
// rotating to the face across the incoming edge (twin of prev).
geometry_status_t CollectFan(
    const editable_mesh_t *pMesh,
    geometry_mesh_vertex_handle_t hV,
    const vector_t<u8> &beveled,
    vector_t<fan_entry_t> *pOut ) noexcept
{
    Vector_Clear( pOut );
    const mesh_vertex_record_t *pV = GenerationPool_Get( &pMesh->vertices, hV );
    const geometry_mesh_half_edge_handle_t hStart = pV->hOutHalfEdge;
    geometry_mesh_half_edge_handle_t h = hStart;
    for ( u32 guard = 0u;; ++guard ) {
        const mesh_half_edge_record_t *pH = He( pMesh, h );
        if ( guard >= kFanMax || pH == nullptr || !Same( pH->hOrigin, hV ) ) { return geometry_status_t::CORRUPT_STATE; }
        const mesh_vertex_record_t *pX = GenerationPool_Get( &pMesh->vertices, DestOf( pMesh, *pH ) );
        if ( pX == nullptr ) { return geometry_status_t::CORRUPT_STATE; }
        const math::vec3d_t d = Sub( pX->position, pV->position );
        const f64 len = Length( d );
        if ( !( len > 0.0 ) || !std::isfinite( len ) ) { return geometry_status_t::DEGENERATE; }
        fan_entry_t e{};
        e.hOut = h;
        e.hFace = FaceOf( pMesh, h );
        e.hEdge = pH->hEdge;
        e.dir = Scale( d, 1.0 / len );
        e.bBeveled = beveled.pData[pH->hEdge.nSlot] != 0u;
        if ( !Vector_PushBack( pOut, e ) ) { return geometry_status_t::ALLOCATION_FAILED; }
        const mesh_half_edge_record_t *pP = He( pMesh, pH->hPrev );
        if ( pP == nullptr ) { return geometry_status_t::CORRUPT_STATE; }
        if ( IsBoundaryHe( pMesh, *pP ) ) { return geometry_status_t::UNSUPPORTED; } // open fan
        h = pP->hTwin;
        if ( Same( h, hStart ) ) { return geometry_status_t::OK; }
    }
}

// Finds or creates the profile between BVs x and y at vertex v with corner
// K, and returns it with x -> y traversal direction in *pForward.
const profile_t *Profile(
    build_t *pB,
    u64 vertexKey,
    u32 x,
    u32 y,
    math::vec3d_t k,
    u32 cSegments,
    bool *pForward ) noexcept
{
    const u32 lo = std::min( x, y ), hi = std::max( x, y );
    *pForward = x == lo;
    const profile_t probe{ vertexKey, lo, hi, 0u, {} };
    for ( usize i = 0u; i < pB->profiles.nCount; ++i ) {
        const profile_t &p = pB->profiles.pData[i];
        if ( !ProfileLess( p, probe ) && !ProfileLess( probe, p ) ) { return &p; }
    }
    const math::vec3d_t a = pB->positions.pData[lo], b = pB->positions.pData[hi];
    profile_t p{ vertexKey, lo, hi, static_cast<u32>( pB->positions.nCount ), Sub( Add( a, b ), k ) };
    for ( u32 j = 1u; j < cSegments; ++j ) {
        const f64 t = kHalfPi * static_cast<f64>( j ) / static_cast<f64>( cSegments );
        const math::vec3d_t q = Add( p.center, Add( Scale( Sub( a, p.center ), std::cos( t ) ), Scale( Sub( b, p.center ), std::sin( t ) ) ) );
        (void)pB->AddPosition( q );
    }
    pB->bOk = pB->bOk && Vector_PushBack( &pB->profiles, p );
    return pB->bOk ? &pB->profiles.pData[pB->profiles.nCount - 1u] : nullptr;
}

// The existing profile between BVs x and y at vertex v (nullptr if none).
const profile_t *FindProfile( const build_t &b, u64 vertexKey, u32 x, u32 y, bool *pForward ) noexcept
{
    const u32 lo = std::min( x, y ), hi = std::max( x, y );
    *pForward = x == lo;
    for ( usize i = 0u; i < b.profiles.nCount; ++i ) {
        const profile_t &p = b.profiles.pData[i];
        if ( p.vertexKey == vertexKey && p.bvLo == lo && p.bvHi == hi ) { return &p; }
    }
    return nullptr;
}

// Appends the profile's points strictly between its ends, in x -> y order.
void AppendInterior( const profile_t &p, bool bForward, u32 cSegments, vector_t<u32> *pOut, bool *pOk ) noexcept
{
    for ( u32 j = 1u; j < cSegments; ++j ) {
        const u32 idx = bForward ? p.iFirstInterior + j - 1u : p.iFirstInterior + cSegments - 1u - j;
        *pOk = *pOk && Vector_PushBack( pOut, idx );
    }
}

// Exact validity of one output ring: simple in the projection that drops
// `axis`, and (when reference != 0) wound like the reference orientation.
bool RingValid( const math::vec3d_t *pPts, u32 n, u32 axis, i32 reference, vector_t<math::vec2d_t> *pFlat ) noexcept
{
    if ( !Vector_Resize( pFlat, n ) ) { return false; }
    for ( u32 i = 0u; i < n; ++i ) { pFlat->pData[i] = Project( pPts[i], axis ); }
    if ( !IsSimple( pFlat->pData, n ) ) { return false; }
    return reference == 0 || OrientationOf( pFlat->pData, n ) == reference;
}

} // namespace

mesh_bevel_result_t MeshBevel_Edges(
    editable_mesh_t *pMesh,
    span_t<const geometry_mesh_edge_handle_t> edges,
    const mesh_bevel_params_t &params,
    vector_t<mesh_bevel_face_t> *pFacesOut ) noexcept
{
    mesh_bevel_result_t r{};
    if ( pMesh == nullptr ) { return r; }
    if ( !EditableMesh_IsInitialized( pMesh ) ) {
        r.status = geometry_status_t::NOT_INITIALIZED;
        return r;
    }
    if ( edges.nCount == 0u || edges.pData == nullptr || params.cSegments < 1u || params.cSegments > kMeshBevelSegmentsMax ||
         ( pFacesOut != nullptr && pFacesOut->pAllocator == nullptr ) ) {
        return r;
    }
    if ( !std::isfinite( params.width ) ) {
        r.status = geometry_status_t::NUMERIC_FAILURE;
        return r;
    }
    if ( !( params.width > 0.0 ) ) { return r; }
    const f64 w = params.width;
    const u32 s = params.cSegments;

    const allocator_t *pA = pMesh->pAllocator;
    build_t b{};
    b.Init( pA );
    vector_t<u8> beveled{}, faceSeen{};
    vector_t<geometry_mesh_vertex_handle_t> touched{};
    vector_t<mesh_boundary_corner_t> corners{};
    vector_t<u32> sizes{}, ringIdx{};
    vector_t<math::vec3d_t> ringPts{}, origPts{};
    vector_t<math::vec2d_t> flat{};
    vector_t<geometry_mesh_face_handle_t> created{};
    vector_t<geometry_mesh_face_handle_t> stripSource{};
    auto cleanup = [&]() noexcept {
        b.Shutdown();
        Vector_Shutdown( &beveled );
        Vector_Shutdown( &faceSeen );
        Vector_Shutdown( &touched );
        Vector_Shutdown( &corners );
        Vector_Shutdown( &sizes );
        Vector_Shutdown( &ringIdx );
        Vector_Shutdown( &ringPts );
        Vector_Shutdown( &origPts );
        Vector_Shutdown( &flat );
        Vector_Shutdown( &created );
        Vector_Shutdown( &stripSource );
    };
    auto fail = [&]( geometry_status_t st ) noexcept {
        cleanup();
        r = mesh_bevel_result_t{};
        r.status = st;
        return r;
    };
    if ( !b.bOk || !Vector_Init( &beveled, pA ) || !Vector_Init( &faceSeen, pA ) || !Vector_Init( &touched, pA ) ||
         !Vector_Init( &corners, pA ) || !Vector_Init( &sizes, pA ) || !Vector_Init( &ringIdx, pA ) ||
         !Vector_Init( &ringPts, pA ) || !Vector_Init( &origPts, pA ) || !Vector_Init( &flat, pA ) ||
         !Vector_Init( &created, pA ) || !Vector_Init( &stripSource, pA ) ||
         !Vector_Resize( &beveled, pMesh->edges.cSlots ) || !Vector_Resize( &faceSeen, pMesh->faces.cSlots ) ) {
        return fail( geometry_status_t::ALLOCATION_FAILED );
    }
    for ( usize i = 0u; i < beveled.nCount; ++i ) { beveled.pData[i] = 0u; }
    for ( usize i = 0u; i < faceSeen.nCount; ++i ) { faceSeen.pData[i] = 0u; }

    // ---- Edges and touched vertices ----
    for ( usize i = 0u; i < edges.nCount; ++i ) {
        const mesh_edge_record_t *pE = GenerationPool_Get( &pMesh->edges, edges.pData[i] );
        const mesh_half_edge_record_t *pH = pE ? He( pMesh, pE->hHalfEdge ) : nullptr;
        if ( pH == nullptr ) { return fail( geometry_status_t::INVALID_HANDLE ); }
        if ( beveled.pData[edges.pData[i].nSlot] != 0u ) { return fail( geometry_status_t::INVALID_ARGUMENT ); }
        if ( IsBoundaryHe( pMesh, *pH ) ) { return fail( geometry_status_t::UNSUPPORTED ); }
        beveled.pData[edges.pData[i].nSlot] = 1u;
        if ( !Vector_PushBack( &touched, pH->hOrigin ) || !Vector_PushBack( &touched, DestOf( pMesh, *pH ) ) ) {
            return fail( geometry_status_t::ALLOCATION_FAILED );
        }
    }
    std::sort( touched.pData, touched.pData + touched.nCount,
               []( geometry_mesh_vertex_handle_t x, geometry_mesh_vertex_handle_t y ) { return Key( x ) < Key( y ); } );
    {
        usize wr = 0u;
        for ( usize i = 0u; i < touched.nCount; ++i ) {
            if ( wr == 0u || !Same( touched.pData[wr - 1u], touched.pData[i] ) ) { touched.pData[wr++] = touched.pData[i]; }
        }
        (void)Vector_Resize( &touched, wr );
    }

    // ---- Per vertex: BVs, profiles, patches ----
    for ( usize iv = 0u; iv < touched.nCount; ++iv ) {
        const geometry_mesh_vertex_handle_t hV = touched.pData[iv];
        const u64 vKey = Key( hV );
        const math::vec3d_t v = GenerationPool_Get( &pMesh->vertices, hV )->position;
        const geometry_status_t fanStatus = CollectFan( pMesh, hV, beveled, &b.fan );
        if ( fanStatus != geometry_status_t::OK ) { return fail( fanStatus ); }
        const u32 k = static_cast<u32>( b.fan.nCount );
        const fan_entry_t *pFan = b.fan.pData;
        u32 cBev = 0u;
        for ( u32 i = 0u; i < k; ++i ) {
            cBev += pFan[i].bBeveled ? 1u : 0u;
            if ( !faceSeen.pData[pFan[i].hFace.nSlot] ) {
                faceSeen.pData[pFan[i].hFace.nSlot] = 1u;
                b.bOk = b.bOk && Vector_PushBack( &b.removed, pFan[i].hFace );
            }
        }

        if ( cBev == 1u ) {
            // End of a beveled edge: slide along the two side edges; the end
            // cap (the third face) takes the profile.
            if ( k != 3u ) { return fail( geometry_status_t::UNSUPPORTED ); }
            u32 j = 0u;
            while ( !pFan[j].bBeveled ) { ++j; }
            const u32 jn = ( j + 1u ) % 3u, jp = ( j + 2u ) % 3u;
            const f64 sinA = SinBetween( pFan[j].dir, pFan[jn].dir ), sinB = SinBetween( pFan[j].dir, pFan[jp].dir );
            if ( !( sinA > 0.0 ) || !( sinB > 0.0 ) ) { return fail( geometry_status_t::DEGENERATE ); }
            const u32 iSa = b.AddPosition( Add( v, Scale( pFan[jn].dir, w / sinA ) ) ); // on E_{j+1}, in F_j
            const u32 iSb = b.AddPosition( Add( v, Scale( pFan[jp].dir, w / sinB ) ) ); // on E_{j-1}, in F_{j-1}
            b.AddReplace( pFan[j].hFace, hV, &iSa, 1u );
            b.AddReplace( pFan[jp].hFace, hV, &iSb, 1u );
            bool bForward = true;
            const profile_t *pP = Profile( &b, vKey, iSb, iSa, v, s, &bForward );
            if ( pP == nullptr ) { return fail( geometry_status_t::ALLOCATION_FAILED ); }
            // Cap loop runs x_{j-1} -> v -> x_{j+1}: S_b, the arc, S_a.
            Vector_Clear( &ringIdx );
            bool bOk = Vector_PushBack( &ringIdx, iSb );
            AppendInterior( *pP, bForward, s, &ringIdx, &bOk );
            bOk = bOk && Vector_PushBack( &ringIdx, iSa );
            if ( !bOk ) { return fail( geometry_status_t::ALLOCATION_FAILED ); }
            b.AddReplace( pFan[jn].hFace, hV, ringIdx.pData, static_cast<u32>( ringIdx.nCount ) );
            continue;
        }

        // cBev >= 2: sectors of faces between consecutive beveled edges.
        // Beveled edges around one vertex are few; 64 bounds the scratch.
        u32 bevAt[64];
        if ( cBev > 64u ) { return fail( geometry_status_t::UNSUPPORTED ); }
        u32 c = 0u;
        for ( u32 i = 0u; i < k; ++i ) {
            if ( pFan[i].bBeveled ) { bevAt[c++] = i; }
        }
        u32 sectorBV[64];
        for ( u32 t = 0u; t < cBev; ++t ) {
            const u32 p = bevAt[t], q = bevAt[( t + 1u ) % cBev];
            const u32 m = ( q + k - p ) % k;
            if ( m == 1u ) {
                // Miter inside F_p between E_p and E_{p+1}: on the bisector at
                // w / sin(phi / 2); only for a convex corner of the face.
                const math::vec3d_t d0 = pFan[p].dir, d1 = pFan[( p + 1u ) % k].dir;
                const mesh_face_record_t *pF = GenerationPool_Get( &pMesh->faces, pFan[p].hFace );
                if ( !( math::Vec3d_Dot( math::Vec3d_Cross( d0, d1 ), pF->normal ) > 0.0 ) ) {
                    return fail( geometry_status_t::UNSUPPORTED );
                }
                const math::vec3d_t bis = Add( d0, d1 );
                const f64 lenBis = Length( bis ), sinHalf = 0.5 * Length( Sub( d0, d1 ) );
                if ( !( lenBis > 0.0 ) || !( sinHalf > 0.0 ) ) { return fail( geometry_status_t::DEGENERATE ); }
                sectorBV[t] = b.AddPosition( Add( v, Scale( bis, w / ( sinHalf * lenBis ) ) ) );
                b.AddReplace( pFan[p].hFace, hV, &sectorBV[t], 1u );
            } else if ( m == 2u ) {
                // Slide along E_{p+1}, the non-beveled edge between F_p and
                // F_{p+1}: the average of the offsets from both beveled edges
                // (equal on a box).
                const math::vec3d_t dg = pFan[( p + 1u ) % k].dir;
                const f64 s1 = SinBetween( pFan[p].dir, dg ), s2 = SinBetween( pFan[q].dir, dg );
                if ( !( s1 > 0.0 ) || !( s2 > 0.0 ) ) { return fail( geometry_status_t::DEGENERATE ); }
                sectorBV[t] = b.AddPosition( Add( v, Scale( dg, 0.5 * ( w / s1 + w / s2 ) ) ) );
                b.AddReplace( pFan[p].hFace, hV, &sectorBV[t], 1u );
                b.AddReplace( pFan[( p + 1u ) % k].hFace, hV, &sectorBV[t], 1u );
            } else {
                return fail( geometry_status_t::UNSUPPORTED );
            }
        }
        // Profiles: edge E_{bevAt[t]} lies between sector t-1 and sector t.
        math::vec3d_t centerSum{};
        for ( u32 t = 0u; t < cBev; ++t ) {
            const u32 x = sectorBV[( t + cBev - 1u ) % cBev], y = sectorBV[t];
            math::vec3d_t corner = v;
            if ( cBev >= 3u ) {
                const math::vec3d_t d = pFan[bevAt[t]].dir;
                const math::vec3d_t mid = Scale( Add( b.positions.pData[x], b.positions.pData[y] ), 0.5 );
                corner = Add( v, Scale( d, math::Vec3d_Dot( Sub( mid, v ), d ) ) );
            }
            bool bForward = true;
            const profile_t *pP = Profile( &b, vKey, x, y, corner, s, &bForward );
            if ( pP == nullptr ) { return fail( geometry_status_t::ALLOCATION_FAILED ); }
            centerSum = Add( centerSum, pP->center );
        }
        if ( cBev >= 3u ) {
            // Patch ring, anticlockwise seen from outside like the fan: each
            // sector's BV, then the profile of the next beveled edge.
            patch_t patch{};
            patch.iFirst = static_cast<u32>( b.patchIdx.nCount );
            patch.hSource = pFan[0].hFace;
            bool bOk = true;
            for ( u32 t = 0u; t < cBev; ++t ) {
                const u32 x = sectorBV[t], y = sectorBV[( t + 1u ) % cBev];
                bOk = bOk && Vector_PushBack( &b.patchIdx, x );
                bool bForward = true;
                const profile_t *pP = FindProfile( b, vKey, x, y, &bForward );
                if ( pP == nullptr ) { return fail( geometry_status_t::CORRUPT_STATE ); }
                AppendInterior( *pP, bForward, s, &b.patchIdx, &bOk );
            }
            if ( !bOk ) { return fail( geometry_status_t::ALLOCATION_FAILED ); }
            patch.count = static_cast<u32>( b.patchIdx.nCount ) - patch.iFirst;
            if ( s > 1u ) {
                // Apex on the corner's "sphere": centre O = mean profile
                // centre, radius = mean distance of the ring from O, in the
                // direction of v (exact for a box corner).
                const math::vec3d_t o = Scale( centerSum, 1.0 / static_cast<f64>( cBev ) );
                f64 radius = 0.0;
                math::vec3d_t centroid{};
                for ( u32 i = 0u; i < patch.count; ++i ) {
                    const math::vec3d_t p = b.positions.pData[b.patchIdx.pData[patch.iFirst + i]];
                    radius += Length( Sub( p, o ) );
                    centroid = Add( centroid, p );
                }
                radius /= static_cast<f64>( patch.count );
                centroid = Scale( centroid, 1.0 / static_cast<f64>( patch.count ) );
                const math::vec3d_t toV = Sub( v, o );
                const f64 lenToV = Length( toV );
                patch.iCenter = b.AddPosition( lenToV > 0.0 ? Add( o, Scale( toV, radius / lenToV ) ) : centroid );
            }
            b.bOk = b.bOk && Vector_PushBack( &b.patches, patch );
        }
        if ( !b.bOk ) { return fail( geometry_status_t::ALLOCATION_FAILED ); }
    }
    if ( !b.bOk ) { return fail( geometry_status_t::ALLOCATION_FAILED ); }
    std::sort( b.repl.pData, b.repl.pData + b.repl.nCount, ReplaceLess );
    std::sort( b.removed.pData, b.removed.pData + b.removed.nCount,
               []( geometry_mesh_face_handle_t x, geometry_mesh_face_handle_t y ) { return Key( x ) < Key( y ); } );
    for ( usize i = 0u; i < b.positions.nCount; ++i ) {
        if ( !math::Vec3d_IsFinite( b.positions.pData[i] ) ) { return fail( geometry_status_t::NUMERIC_FAILURE ); }
    }

    // ---- Rings: reshaped faces, strips, patches (with exact checks) ----
    auto newCorner = [&]( u32 idx ) noexcept {
        mesh_boundary_corner_t c{};
        c.iNew = idx;
        return c;
    };
    auto emit = [&]( u32 n ) noexcept { return Vector_PushBack( &sizes, n ); };
    for ( usize f = 0u; f < b.removed.nCount; ++f ) {
        const geometry_mesh_face_handle_t hF = b.removed.pData[f];
        const mesh_face_record_t *pF = GenerationPool_Get( &pMesh->faces, hF );
        const mesh_loop_record_t *pL = GenerationPool_Get( &pMesh->loops, pF->hOuterLoop );
        Vector_Clear( &ringPts );
        Vector_Clear( &origPts );
        u32 n = 0u;
        geometry_mesh_half_edge_handle_t h = pL->hFirstHalfEdge;
        for ( u32 kk = 0u; kk < pL->cHalfEdges; ++kk ) {
            const mesh_half_edge_record_t *pH = He( pMesh, h );
            const math::vec3d_t pos = GenerationPool_Get( &pMesh->vertices, pH->hOrigin )->position;
            if ( !Vector_PushBack( &origPts, pos ) ) { return fail( geometry_status_t::ALLOCATION_FAILED ); }
            if ( const replace_t *pR = FindReplace( b, hF, Key( pH->hOrigin ) ) ) {
                for ( u32 i = 0u; i < pR->count; ++i ) {
                    const u32 idx = b.replIdx.pData[pR->iFirst + i];
                    if ( !Vector_PushBack( &corners, newCorner( idx ) ) || !Vector_PushBack( &ringPts, b.positions.pData[idx] ) ) {
                        return fail( geometry_status_t::ALLOCATION_FAILED );
                    }
                    ++n;
                }
            } else {
                mesh_boundary_corner_t cc{};
                cc.hVertex = pH->hOrigin;
                if ( !Vector_PushBack( &corners, cc ) || !Vector_PushBack( &ringPts, pos ) ) {
                    return fail( geometry_status_t::ALLOCATION_FAILED );
                }
                ++n;
            }
            h = pH->hNext;
        }
        if ( n > kMeshSourceCornersPerFaceMax ) { return fail( geometry_status_t::LIMIT_EXCEEDED ); }
        // Still the same face: simple and wound as before, in the original
        // face's projection.
        const u32 axis = DominantAxis( NewellOf( origPts.pData, pL->cHalfEdges ) );
        if ( !Vector_Resize( &flat, pL->cHalfEdges ) ) { return fail( geometry_status_t::ALLOCATION_FAILED ); }
        for ( u32 i = 0u; i < pL->cHalfEdges; ++i ) { flat.pData[i] = Project( origPts.pData[i], axis ); }
        const i32 reference = OrientationOf( flat.pData, pL->cHalfEdges );
        if ( reference == 0 || !RingValid( ringPts.pData, n, axis, reference, &flat ) ) {
            return fail( geometry_status_t::SELF_INTERSECTING );
        }
        if ( !emit( n ) ) { return fail( geometry_status_t::ALLOCATION_FAILED ); }
    }
    const u32 cReshaped = static_cast<u32>( b.removed.nCount );

    // Strips between the profiles at both ends of each beveled edge.
    for ( usize i = 0u; i < edges.nCount; ++i ) {
        const mesh_edge_record_t *pE = GenerationPool_Get( &pMesh->edges, edges.pData[i] );
        const mesh_half_edge_record_t *pH = He( pMesh, pE->hHalfEdge );
        const geometry_mesh_face_handle_t f1 = FaceOf( pMesh, pE->hHalfEdge ), f2 = FaceOf( pMesh, pH->hTwin );
        const geometry_mesh_vertex_handle_t va = pH->hOrigin, vc = DestOf( pMesh, *pH );
        u32 ends[2][kMeshBevelSegmentsMax + 1u];
        for ( u32 e = 0u; e < 2u; ++e ) {
            const geometry_mesh_vertex_handle_t hV = e == 0u ? va : vc;
            const replace_t *p1 = FindReplace( b, f1, Key( hV ) ), *p2 = FindReplace( b, f2, Key( hV ) );
            if ( p1 == nullptr || p2 == nullptr || p1->count != 1u || p2->count != 1u ) {
                return fail( geometry_status_t::CORRUPT_STATE );
            }
            const u32 x = b.replIdx.pData[p1->iFirst], y = b.replIdx.pData[p2->iFirst];
            bool bForward = true;
            const profile_t *pP = FindProfile( b, Key( hV ), x, y, &bForward );
            if ( pP == nullptr ) { return fail( geometry_status_t::CORRUPT_STATE ); }
            Vector_Clear( &ringIdx );
            bool bOk = Vector_PushBack( &ringIdx, x );
            AppendInterior( *pP, bForward, s, &ringIdx, &bOk );
            bOk = bOk && Vector_PushBack( &ringIdx, y );
            if ( !bOk ) { return fail( geometry_status_t::ALLOCATION_FAILED ); }
            for ( u32 j = 0u; j <= s; ++j ) { ends[e][j] = ringIdx.pData[j]; }
        }
        for ( u32 j = 0u; j < s; ++j ) {
            const u32 quad[4] = { ends[0][j], ends[0][j + 1u], ends[1][j + 1u], ends[1][j] };
            math::vec3d_t pts[4];
            for ( u32 q = 0u; q < 4u; ++q ) {
                pts[q] = b.positions.pData[quad[q]];
                if ( !Vector_PushBack( &corners, newCorner( quad[q] ) ) ) { return fail( geometry_status_t::ALLOCATION_FAILED ); }
            }
            if ( !RingValid( pts, 4u, DominantAxis( NewellOf( pts, 4u ) ), 0, &flat ) ) {
                return fail( geometry_status_t::SELF_INTERSECTING );
            }
            if ( !emit( 4u ) || !Vector_PushBack( &stripSource, f1 ) ) { return fail( geometry_status_t::ALLOCATION_FAILED ); }
        }
    }
    const u32 cStrips = static_cast<u32>( sizes.nCount ) - cReshaped;

    // Corner patches: one polygon for a chamfer, a triangle fan otherwise.
    u32 cCorner = 0u;
    for ( usize i = 0u; i < b.patches.nCount; ++i ) {
        const patch_t &p = b.patches.pData[i];
        if ( p.iCenter == CY_INVALID_INDEX ) {
            Vector_Clear( &ringPts );
            for ( u32 j = 0u; j < p.count; ++j ) {
                const u32 idx = b.patchIdx.pData[p.iFirst + j];
                if ( !Vector_PushBack( &corners, newCorner( idx ) ) || !Vector_PushBack( &ringPts, b.positions.pData[idx] ) ) {
                    return fail( geometry_status_t::ALLOCATION_FAILED );
                }
            }
            if ( !RingValid( ringPts.pData, p.count, DominantAxis( NewellOf( ringPts.pData, p.count ) ), 0, &flat ) ) {
                return fail( geometry_status_t::SELF_INTERSECTING );
            }
            if ( !emit( p.count ) ) { return fail( geometry_status_t::ALLOCATION_FAILED ); }
            ++cCorner;
            continue;
        }
        for ( u32 j = 0u; j < p.count; ++j ) {
            const u32 tri[3] = { b.patchIdx.pData[p.iFirst + j], b.patchIdx.pData[p.iFirst + ( j + 1u ) % p.count], p.iCenter };
            math::vec3d_t pts[3];
            for ( u32 q = 0u; q < 3u; ++q ) {
                pts[q] = b.positions.pData[tri[q]];
                if ( !Vector_PushBack( &corners, newCorner( tri[q] ) ) ) { return fail( geometry_status_t::ALLOCATION_FAILED ); }
            }
            if ( !RingValid( pts, 3u, DominantAxis( NewellOf( pts, 3u ) ), 0, &flat ) ) {
                return fail( geometry_status_t::SELF_INTERSECTING );
            }
            if ( !emit( 3u ) ) { return fail( geometry_status_t::ALLOCATION_FAILED ); }
            ++cCorner;
        }
    }

    // ---- Mutation: one failure-atomic replace ----
    if ( !Vector_Reserve( &created, sizes.nCount ) ||
         ( pFacesOut != nullptr && !Vector_Reserve( pFacesOut, pFacesOut->nCount + sizes.nCount ) ) ) {
        return fail( geometry_status_t::ALLOCATION_FAILED );
    }
    const mesh_boundary_replace_result_t rr = MeshBoundary_ReplaceFaces(
        pMesh, span_t<const geometry_mesh_face_handle_t>{ b.removed.pData, b.removed.nCount },
        span_t<const mesh_boundary_corner_t>{ corners.pData, corners.nCount }, span_t<const u32>{ sizes.pData, sizes.nCount },
        span_t<const math::vec3d_t>{ b.positions.pData, b.positions.nCount }, &created );
    if ( rr.status != geometry_status_t::OK ) { return fail( rr.status ); }

    if ( pFacesOut != nullptr ) {
        u32 iPatchFace = 0u;
        usize iPatch = 0u;
        for ( usize i = 0u; i < created.nCount; ++i ) {
            mesh_bevel_face_t out{};
            out.hFace = created.pData[i];
            if ( i < cReshaped ) {
                out.hSource = b.removed.pData[i];
                out.role = mesh_bevel_face_role_t::RESHAPED;
            } else if ( i < cReshaped + cStrips ) {
                out.hSource = stripSource.pData[i - cReshaped];
                out.role = mesh_bevel_face_role_t::STRIP;
            } else {
                // Patch faces come patch by patch: one face per chamfer patch,
                // `count` triangles per rounded patch.
                const patch_t &p = b.patches.pData[iPatch];
                out.hSource = p.hSource;
                out.role = mesh_bevel_face_role_t::CORNER;
                const u32 cFaces = p.iCenter == CY_INVALID_INDEX ? 1u : p.count;
                if ( ++iPatchFace == cFaces ) {
                    iPatchFace = 0u;
                    ++iPatch;
                }
            }
            (void)Vector_PushBack( pFacesOut, out );
        }
    }
    r.cFacesReshaped = cReshaped;
    r.cStripFaces = cStrips;
    r.cCornerFaces = cCorner;
    r.cVerticesCreated = rr.cVerticesCreated;
    r.cVerticesRemoved = rr.cVerticesRemoved;
    r.status = geometry_status_t::OK;
    cleanup();
    return r;
}

} // namespace cypher::editor::geometry
