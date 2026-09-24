//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Planar_Offset.cpp
//  Purpose: Implements stroke-piece generation, balanced union reduction,
//           and region offsetting on top of Planar_TryOverlay.
//  Details: Offset points are always computed through one helper (Off) so
//           that a rectangle corner and the join piece that meets it are
//           bit-identical: the overlay then sees one exact shared vertex
//           instead of two nearly equal ones it would have to merge.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Planar_Offset.h"
#include "CypherGeometry_PlanarRegionValidation.h"

#include <cmath>
#include <new>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

constexpr f64 kPi = 3.14159265358979323846;

template <typename type_t>
struct buf_t {
    vector_t<type_t> v{};
    bool ok{ true };
    bool Init( const allocator_t *a, usize cap ) noexcept { return ok = Vector_Init( &v, a, cap ); }
    ~buf_t() { Vector_Shutdown( &v ); }
    void Push( const type_t &x ) noexcept { if ( ok && !Vector_PushBack( &v, x ) ) { ok = false; } }
    usize Size() const noexcept { return v.nCount; }
    type_t &operator[]( usize i ) noexcept { return v.pData[i]; }
    type_t *Data() noexcept { return v.pData; }
};

struct piece_t {
    u32 iFirst{ 0u };
    u32 cPoints{ 0u };
};

math::vec2d_t Off( math::vec2d_t p, math::vec2d_t n, f64 k ) noexcept
{
    return math::Vec2d_Add( p, math::Vec2d_Scale( n, k ) );
}

// Collects stroke pieces as CCW polygons in a flat point buffer.
struct piece_builder_t {
    buf_t<math::vec2d_t> pts;
    buf_t<piece_t> pieces;
    f64 minArea{ 0.0 };

    void Add( const math::vec2d_t *p, u32 n ) noexcept {
        if ( n < 3u ) { return; }
        const f64 area = Planar_RingSignedArea( span_t<const math::vec2d_t>{ p, n } );
        if ( !( std::fabs( area ) > minArea ) ) { return; } // degenerate sliver: contributes nothing
        piece_t pc{ static_cast<u32>( pts.Size() ), n };
        for ( u32 i = 0u; i < n; ++i ) { pts.Push( area > 0.0 ? p[i] : p[n - 1u - i] ); }
        pieces.Push( pc );
    }
};

bool LeftNormal( math::vec2d_t a, math::vec2d_t b, math::vec2d_t *pN ) noexcept
{
    const math::vec2d_t d = math::Vec2d_Subtract( b, a );
    const f64 len = std::sqrt( d.x * d.x + d.y * d.y );
    if ( !( len > 0.0 ) ) { return false; }
    *pN = math::Vec2d_Make( -d.y / len, d.x / len );
    return true;
}

// Arc points from direction angle a0 to a1 (radians, CCW positive) around c.
void AppendArc( math::vec2d_t *out, u32 *pCount, math::vec2d_t c, f64 r, f64 a0, f64 a1,
                u32 perQuarter, u32 maxPoints ) noexcept
{
    const f64 sweep = a1 - a0;
    u32 steps = static_cast<u32>( std::ceil( std::fabs( sweep ) / ( 0.5 * kPi ) *
                                             static_cast<f64>( perQuarter ) ) );
    if ( steps < 1u ) { steps = 1u; }
    for ( u32 i = 1u; i < steps && *pCount < maxPoints; ++i ) {
        const f64 a = a0 + sweep * static_cast<f64>( i ) / static_cast<f64>( steps );
        out[( *pCount )++] = math::Vec2d_Make( c.x + r * std::cos( a ), c.y + r * std::sin( a ) );
    }
}

// Join piece at corner v between segment normals n1 (incoming) and n2
// (outgoing), placed on side s (+1 = left, -1 = right).
void AddJoin( piece_builder_t *pb, math::vec2d_t v, math::vec2d_t n1, math::vec2d_t n2, f64 s,
              f64 hw, const planar_offset_options_t &opt ) noexcept
{
    const math::vec2d_t p1 = Off( v, n1, s * hw );
    const math::vec2d_t p2 = Off( v, n2, s * hw );
    planar_join_t join = opt.join;
    if ( join == planar_join_t::MITER ) {
        const f64 dotN = n1.x * n2.x + n1.y * n2.y;
        const f64 ratio = std::sqrt( 2.0 / ( 1.0 + dotN ) ); // mitre length / hw
        if ( !( 1.0 + dotN > 1e-12 ) || !( ratio <= opt.fMiterLimit ) ) {
            join = planar_join_t::BEVEL;
        } else {
            const math::vec2d_t bis = math::Vec2d_Scale( math::Vec2d_Add( n1, n2 ), 1.0 / ( 1.0 + dotN ) );
            const math::vec2d_t m = Off( v, bis, s * hw );
            const math::vec2d_t kite[4] = { v, p1, m, p2 };
            pb->Add( kite, 4u );
            return;
        }
    }
    if ( join == planar_join_t::BEVEL ) {
        const math::vec2d_t tri[3] = { v, p1, p2 };
        pb->Add( tri, 3u );
        return;
    }
    // ROUND: fan from p1 to p2 the short way around v.
    math::vec2d_t fan[2u + 4u * 64u];
    u32 n = 0u;
    fan[n++] = v;
    fan[n++] = p1;
    f64 a0 = std::atan2( p1.y - v.y, p1.x - v.x );
    f64 a1 = std::atan2( p2.y - v.y, p2.x - v.x );
    f64 sweep = a1 - a0;
    while ( sweep > kPi ) { sweep -= 2.0 * kPi; }
    while ( sweep < -kPi ) { sweep += 2.0 * kPi; }
    const u32 perQuarter = opt.roundSegments < 1u ? 1u : ( opt.roundSegments > 64u ? 64u : opt.roundSegments );
    AppendArc( fan, &n, v, hw, a0, a0 + sweep, perQuarter, 2u + 4u * 64u - 1u );
    fan[n++] = p2;
    pb->Add( fan, n );
}

// Half-disk cap at endpoint p, bulging along direction `outward` (unit).
void AddRoundCap( piece_builder_t *pb, math::vec2d_t p, math::vec2d_t outward, f64 hw,
                  const planar_offset_options_t &opt ) noexcept
{
    const math::vec2d_t n = math::Vec2d_Make( -outward.y, outward.x );
    math::vec2d_t cap[2u + 4u * 64u];
    u32 cnt = 0u;
    cap[cnt++] = Off( p, n, -hw );
    const f64 a0 = std::atan2( -n.y, -n.x );
    const u32 perQuarter = opt.roundSegments < 1u ? 1u : ( opt.roundSegments > 64u ? 64u : opt.roundSegments );
    AppendArc( cap, &cnt, p, hw, a0, a0 + kPi, perQuarter, 2u + 4u * 64u - 1u );
    cap[cnt++] = Off( p, n, hw );
    pb->Add( cap, cnt );
}

geometry_status_t BuildStrokePieces(
    span_t<const math::vec2d_t> raw,
    bool bClosed,
    f64 hw,
    const planar_offset_options_t &opt,
    const allocator_t *pAlloc,
    piece_builder_t *pb ) noexcept
{
    // Drop consecutive duplicates (and a closing duplicate).
    buf_t<math::vec2d_t> p;
    if ( !p.Init( pAlloc, raw.nCount ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    for ( usize i = 0u; i < raw.nCount; ++i ) {
        if ( !math::Vec2d_IsFinite( raw.pData[i] ) ) { return geometry_status_t::NUMERIC_FAILURE; }
        if ( p.Size() > 0u && p[p.Size() - 1u].x == raw.pData[i].x &&
             p[p.Size() - 1u].y == raw.pData[i].y ) {
            continue;
        }
        p.Push( raw.pData[i] );
    }
    if ( bClosed && p.Size() > 1u && p[0].x == p[p.Size() - 1u].x && p[0].y == p[p.Size() - 1u].y ) {
        --p.v.nCount;
    }
    const usize n = p.Size();
    if ( n < 2u || ( bClosed && n < 3u ) ) { return geometry_status_t::DEGENERATE; }
    const usize cSeg = bClosed ? n : n - 1u;
    if ( cSeg * 2u + 2u > kPlanarOffsetPiecesMax ) { return geometry_status_t::LIMIT_EXCEEDED; }

    for ( usize i = 0u; i < cSeg; ++i ) {
        const math::vec2d_t a = p[i], b = p[( i + 1u ) % n];
        math::vec2d_t nrm{};
        if ( !LeftNormal( a, b, &nrm ) ) { continue; }
        const math::vec2d_t rect[4] = { Off( a, nrm, -hw ), Off( b, nrm, -hw ), Off( b, nrm, hw ),
                                        Off( a, nrm, hw ) };
        pb->Add( rect, 4u );
    }
    const usize first = bClosed ? 0u : 1u;
    const usize last = bClosed ? n : n - 1u;
    for ( usize i = first; i < last; ++i ) {
        const math::vec2d_t prev = p[( i + n - 1u ) % n], v = p[i], next = p[( i + 1u ) % n];
        math::vec2d_t n1{}, n2{};
        if ( !LeftNormal( prev, v, &n1 ) || !LeftNormal( v, next, &n2 ) ) { continue; }
        const i32 turn = math::Orient2D( prev, v, next );
        if ( turn > 0 ) { AddJoin( pb, v, n1, n2, -1.0, hw, opt ); }      // left turn: right side opens
        else if ( turn < 0 ) { AddJoin( pb, v, n1, n2, +1.0, hw, opt ); } // right turn: left side opens
        else if ( n1.x * n2.x + n1.y * n2.y < 0.0 ) {
            // Exact U-turn: both sides open; cap it round/square via two joins
            // through the perpendicular.
            const math::vec2d_t d = math::Vec2d_Make( n1.y, -n1.x ); // incoming direction
            AddRoundCap( pb, v, d, hw, opt );
        }
    }
    if ( !bClosed && opt.bRoundCaps ) {
        math::vec2d_t n0{}, nl{};
        if ( LeftNormal( p[0], p[1], &n0 ) ) {
            AddRoundCap( pb, p[0], math::Vec2d_Make( -n0.y, n0.x ), hw, opt );
        }
        if ( LeftNormal( p[n - 2u], p[n - 1u], &nl ) ) {
            AddRoundCap( pb, p[n - 1u], math::Vec2d_Make( nl.y, -nl.x ), hw, opt );
        }
    }
    if ( !p.ok || !pb->pts.ok || !pb->pieces.ok ) { return geometry_status_t::ALLOCATION_FAILED; }
    return geometry_status_t::OK;
}

// Owns an array of regions for the balanced union.
struct region_array_t {
    const allocator_t *pAlloc{ nullptr };
    planar_region_t *p{ nullptr };
    usize n{ 0u };
    bool Init( const allocator_t *a, usize count ) noexcept {
        pAlloc = a;
        n = count;
        p = static_cast<planar_region_t *>(
            Allocator_Allocate( a, sizeof( planar_region_t ) * ( count ? count : 1u ), alignof( planar_region_t ) ) );
        if ( p == nullptr ) { n = 0u; return false; }
        for ( usize i = 0u; i < n; ++i ) { new ( &p[i] ) planar_region_t{}; }
        return true;
    }
    ~region_array_t() {
        if ( p == nullptr ) { return; }
        for ( usize i = 0u; i < n; ++i ) {
            PlanarRegion_Shutdown( &p[i] );
            p[i].~planar_region_t();
        }
        Allocator_Free( pAlloc, p, sizeof( planar_region_t ) * ( n ? n : 1u ), alignof( planar_region_t ) );
    }
};

void MoveRegion( planar_region_t *pDst, planar_region_t *pSrc ) noexcept
{
    pDst->frame = pSrc->frame;
    pDst->sourceId = pSrc->sourceId;
    Vector_Move( &pDst->points, &pSrc->points );
    Vector_Move( &pDst->contours, &pSrc->contours );
    Vector_Move( &pDst->polygons, &pSrc->polygons );
}

// Unions all pieces into one region (in *pOut, zero-initialized). Uses a
// private ID domain; the caller re-identifies the final result.
geometry_status_t UnionPieces(
    const planar_frame_t &frame,
    piece_builder_t *pb,
    const geometry_policy_t &policy,
    const allocator_t *pAlloc,
    planar_region_t *pOut ) noexcept
{
    const usize cP = pb->pieces.Size();
    if ( cP == 0u ) { return geometry_status_t::DEGENERATE; }
    geometry_source_id_allocator_t ids{};
    region_array_t arr;
    if ( !arr.Init( pAlloc, cP ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    for ( usize i = 0u; i < cP; ++i ) {
        geometry_source_id_result_t r0 = GeometrySourceIdAllocator_Allocate( &ids );
        geometry_status_t s = PlanarRegion_Init( &arr.p[i], pAlloc, frame, r0.id );
        if ( s != geometry_status_t::OK ) { return s; }
        const geometry_source_id_result_t r1 = GeometrySourceIdAllocator_Allocate( &ids );
        const geometry_source_id_result_t r2 = GeometrySourceIdAllocator_Allocate( &ids );
        const piece_t &pc = pb->pieces[i];
        s = PlanarRegion_TryAddPolygon( &arr.p[i], r1.id, r2.id,
                                        span_t<const math::vec2d_t>{ pb->pts.Data() + pc.iFirst, pc.cPoints },
                                        nullptr );
        if ( s != geometry_status_t::OK ) { return s; }
    }
    usize count = cP;
    while ( count > 1u ) {
        const usize half = count / 2u;
        for ( usize i = 0u; i < half; ++i ) {
            planar_region_t merged{};
            const geometry_source_id_result_t rid = GeometrySourceIdAllocator_Allocate( &ids );
            const geometry_status_t s = Planar_TryOverlay( &arr.p[2u * i], &arr.p[2u * i + 1u],
                                                           planar_boolean_op_t::UNION, policy, rid.id,
                                                           &ids, pAlloc, &merged, nullptr );
            if ( s != geometry_status_t::OK ) {
                PlanarRegion_Shutdown( &merged );
                return s;
            }
            PlanarRegion_Shutdown( &arr.p[2u * i] );
            PlanarRegion_Shutdown( &arr.p[2u * i + 1u] );
            MoveRegion( &arr.p[i], &merged );
        }
        if ( count & 1u ) {
            planar_region_t tmp{};
            MoveRegion( &tmp, &arr.p[count - 1u] );
            MoveRegion( &arr.p[half], &tmp );
        }
        count = half + ( count & 1u );
    }
    MoveRegion( pOut, &arr.p[0] );
    return geometry_status_t::OK;
}

// Copies pSrc into pOut with fresh IDs from *pIds (caller publishes).
geometry_status_t CopyWithIds(
    const planar_region_t *pSrc,
    geometry_source_id_t regionId,
    geometry_source_id_allocator_t *pIds,
    const allocator_t *pAlloc,
    planar_region_t *pOut ) noexcept
{
    geometry_status_t s = PlanarRegion_Init( pOut, pAlloc, pSrc->frame, regionId );
    for ( usize p = 0u; p < pSrc->polygons.nCount && s == geometry_status_t::OK; ++p ) {
        const planar_region_polygon_t &poly = pSrc->polygons.pData[p];
        for ( u32 c = 0u; c < poly.cContours && s == geometry_status_t::OK; ++c ) {
            const geometry_source_id_result_t a = GeometrySourceIdAllocator_Allocate( pIds );
            if ( a.status != geometry_status_t::OK ) { s = a.status; break; }
            const span_t<const math::vec2d_t> ring = PlanarRegion_ContourPoints( pSrc, poly.iFirstContour + c );
            if ( c == 0u ) {
                const geometry_source_id_result_t b = GeometrySourceIdAllocator_Allocate( pIds );
                if ( b.status != geometry_status_t::OK ) { s = b.status; break; }
                s = PlanarRegion_TryAddPolygon( pOut, a.id, b.id, ring, nullptr );
            } else {
                s = PlanarRegion_TryAddHole( pOut, a.id, ring );
            }
        }
    }
    if ( s != geometry_status_t::OK ) { PlanarRegion_Shutdown( pOut ); }
    return s;
}

bool ArgsOk( geometry_source_id_t regionId, geometry_source_id_allocator_t *pIds,
             const allocator_t *pAlloc, planar_region_t *pOut ) noexcept
{
    return GeometrySourceId_IsValid( regionId ) && pIds != nullptr && Allocator_IsValid( pAlloc ) &&
           pOut != nullptr && !PlanarRegion_IsInitialized( pOut );
}

} // namespace

geometry_status_t Planar_TryStrokePolyline(
    const planar_frame_t &frame,
    span_t<const math::vec2d_t> points,
    bool bClosed,
    f64 halfWidth,
    const planar_offset_options_t &options,
    const geometry_policy_t &policy,
    geometry_source_id_t resultRegionId,
    geometry_source_id_allocator_t *pIdAllocator,
    const allocator_t *pAllocator,
    planar_region_t *pOut ) noexcept
{
    if ( !ArgsOk( resultRegionId, pIdAllocator, pAllocator, pOut ) ||
         ( points.pData == nullptr && points.nCount != 0u ) || !std::isfinite( halfWidth ) ||
         !( halfWidth > policy.numerical.fMinimumEdgeLength ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    piece_builder_t pb;
    pb.minArea = policy.numerical.fMinimumFaceArea;
    if ( !pb.pts.Init( pAllocator, points.nCount * 8u + 8u ) ||
         !pb.pieces.Init( pAllocator, points.nCount * 2u + 2u ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    geometry_status_t s = BuildStrokePieces( points, bClosed, halfWidth, options, pAllocator, &pb );
    if ( s != geometry_status_t::OK ) { return s; }

    planar_region_t merged{};
    s = UnionPieces( frame, &pb, policy, pAllocator, &merged );
    if ( s != geometry_status_t::OK ) { PlanarRegion_Shutdown( &merged ); return s; }

    geometry_source_id_allocator_t ids = *pIdAllocator;
    planar_region_t result{};
    s = CopyWithIds( &merged, resultRegionId, &ids, pAllocator, &result );
    PlanarRegion_Shutdown( &merged );
    if ( s != geometry_status_t::OK ) { return s; }
    MoveRegion( pOut, &result );
    *pIdAllocator = ids;
    return geometry_status_t::OK;
}

geometry_status_t Planar_TryOffsetRegion(
    const planar_region_t *pRegion,
    f64 distance,
    const planar_offset_options_t &options,
    const geometry_policy_t &policy,
    geometry_source_id_t resultRegionId,
    geometry_source_id_allocator_t *pIdAllocator,
    const allocator_t *pAllocator,
    planar_region_t *pOut ) noexcept
{
    if ( pRegion == nullptr || !ArgsOk( resultRegionId, pIdAllocator, pAllocator, pOut ) ||
         !std::isfinite( distance ) ||
         !( std::fabs( distance ) > policy.numerical.fMinimumEdgeLength ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !PlanarRegion_IsInitialized( pRegion ) ) { return geometry_status_t::NOT_INITIALIZED; }
    const planar_region_validation_t rv = PlanarRegion_Validate( pRegion, policy );
    if ( rv.status != geometry_status_t::OK ) { return rv.status; }

    // Band = stroke of every contour, merged in one balanced union.
    piece_builder_t pb;
    pb.minArea = policy.numerical.fMinimumFaceArea;
    if ( !pb.pts.Init( pAllocator, pRegion->points.nCount * 8u + 8u ) ||
         !pb.pieces.Init( pAllocator, pRegion->points.nCount * 2u + 2u ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    const f64 hw = std::fabs( distance );
    for ( usize c = 0u; c < pRegion->contours.nCount; ++c ) {
        const geometry_status_t s = BuildStrokePieces( PlanarRegion_ContourPoints( pRegion, c ), true, hw,
                                                       options, pAllocator, &pb );
        if ( s != geometry_status_t::OK ) { return s; }
    }
    planar_region_t band{};
    geometry_status_t s = UnionPieces( pRegion->frame, &pb, policy, pAllocator, &band );
    if ( s != geometry_status_t::OK ) { PlanarRegion_Shutdown( &band ); return s; }

    s = Planar_TryOverlay( pRegion, &band,
                           distance > 0.0 ? planar_boolean_op_t::UNION : planar_boolean_op_t::DIFFERENCE,
                           policy, resultRegionId, pIdAllocator, pAllocator, pOut, nullptr );
    PlanarRegion_Shutdown( &band );
    return s;
}

} // namespace cypher::editor::geometry
