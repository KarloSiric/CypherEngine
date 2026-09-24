//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushShapes.cpp
//  Purpose: Implements the box-fitted brush shapes.
//  Details: Everything round is built from one 2D circle polygon (per circle
//           mode) and one of two convex constructions: a prism (the polygon
//           extruded along an axis) or a pyramid (the polygon joined to an
//           apex). Planes come straight from polygon edges, so there is no
//           plane fitting; convexity of the polygon is checked with the
//           exact Orient2D predicate and collinear corners are dropped, so a
//           prism never gets two sides on one plane. UV spheroids take their
//           planes directly from their band quads and pole triangles rather
//           than from a point-cloud hull, so each planar quad stays one side.
//
//           Every brush is built in a private temporary, deep-validated, and
//           only then moved into the caller's (canonical-empty) slot; the
//           ID allocator is restored from a snapshot on any failure.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushShapes.h"

#include "CypherGeometry_BrushBoundary.h"
#include "CypherGeometry_BrushGenerator.h"
#include "CypherGeometry_BrushTransform.h"
#include "CypherGeometry_BrushValidation.h"
#include "CypherMath_Predicates.h"

#include <algorithm>
#include <cmath>
#include <new>

namespace cypher::editor::geometry
{

using namespace cypher::common;
using math::vec2d_t;
using math::vec3d_t;

namespace
{

constexpr f64 kPi = 3.14159265358979323846;

bool CanonicalEmpty( const brush_solid_t &b ) noexcept
{
    return b.sides.pData == nullptr && b.sides.nCount == 0u && b.sides.nCapacity == 0u && b.sides.pAllocator == nullptr &&
           !GeometrySourceId_IsValid( b.sourceId );
}

bool BoxValid( const brush_shape_box_t &box, const geometry_policy_t &policy ) noexcept
{
    const f64 lim = policy.numerical.fCoordinateMagnitudeLimit;
    const f64 lo[3] = { box.lo.x, box.lo.y, box.lo.z }, hi[3] = { box.hi.x, box.hi.y, box.hi.z };
    for ( int i = 0; i < 3; ++i ) {
        if ( !std::isfinite( lo[i] ) || !std::isfinite( hi[i] ) || !( lo[i] < hi[i] ) || std::fabs( lo[i] ) > lim ||
             std::fabs( hi[i] ) > lim ) {
            return false;
        }
    }
    return true;
}

f64 Comp( vec3d_t v, u32 axis ) noexcept { return axis == 0u ? v.x : ( axis == 1u ? v.y : v.z ); }

vec3d_t Axis( u32 axis, f64 value ) noexcept
{
    return axis == 0u ? math::Vec3d_Make( value, 0, 0 ) : ( axis == 1u ? math::Vec3d_Make( 0, value, 0 ) : math::Vec3d_Make( 0, 0, value ) );
}

// Point from (u, v, a) coordinates on the given axes.
vec3d_t Point3( u32 uAxis, u32 vAxis, u32 aAxis, f64 u, f64 v, f64 a ) noexcept
{
    return math::Vec3d_Add( math::Vec3d_Add( Axis( uAxis, u ), Axis( vAxis, v ) ), Axis( aAxis, a ) );
}

// ---------------------------------------------------------------------------
// Circles
// ---------------------------------------------------------------------------

// Rounded quarter arc (0..90 degrees, k segments) of integer radius R.
void QuarterTemplate( u32 R, u32 k, i64 *pX, i64 *pY ) noexcept
{
    for ( u32 j = 0u; j <= k; ++j ) {
        const f64 t = 0.5 * kPi * static_cast<f64>( j ) / static_cast<f64>( k );
        pX[j] = j == k ? 0 : std::llround( static_cast<f64>( R ) * std::cos( t ) );
        pY[j] = j == 0u ? 0 : std::llround( static_cast<f64>( R ) * std::sin( t ) );
    }
}

i64 Cross( i64 ax, i64 ay, i64 bx, i64 by ) noexcept { return ax * by - ay * bx; }

// The smallest integer radius >= n/2 whose rounded quarter arc is strictly
// convex, including the turn into the next quarter (0 if n unsupported).
u32 ScalableRadius( u32 n ) noexcept
{
    if ( n != 12u && n != 24u && n != 48u && n != 96u ) { return 0u; }
    const u32 k = n / 4u;
    i64 x[25], y[25];
    for ( u32 R = n / 2u; R < 64u * n; ++R ) {
        QuarterTemplate( R, k, x, y );
        bool bConvex = true;
        for ( u32 j = 0u; j < k && bConvex; ++j ) { bConvex = x[j + 1] != x[j] || y[j + 1] != y[j]; }
        for ( u32 j = 1u; j < k && bConvex; ++j ) {
            bConvex = Cross( x[j] - x[j - 1], y[j] - y[j - 1], x[j + 1] - x[j], y[j + 1] - y[j] ) > 0;
        }
        // Into the next quarter: its first edge is this quarter's first
        // edge rotated by 90 degrees, (dx, dy) -> (-dy, dx).
        if ( bConvex ) {
            bConvex = Cross( x[k] - x[k - 1], y[k] - y[k - 1], -( y[1] - y[0] ), x[1] - x[0] ) > 0;
        }
        if ( bConvex ) { return R; }
    }
    return 0u;
}

// Circle polygon; bKeepDuplicates keeps the (up to four) coincident
// straight-run vertices of a square SCALABLE box, so two circles of the same
// side count always have the same vertex count (the arch pairs them).
geometry_status_t MakeCircle(
    vec2d_t lo, vec2d_t hi, u32 n, brush_circle_mode_t mode, bool bKeepDuplicates, vec2d_t *pOut, u32 cCapacity,
    u32 *pCountOut ) noexcept
{
    if ( !std::isfinite( lo.x ) || !std::isfinite( lo.y ) || !std::isfinite( hi.x ) || !std::isfinite( hi.y ) ||
         !( lo.x < hi.x ) || !( lo.y < hi.y ) || n < 3u || n > kBrushCircleSidesMax || pOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( ( mode == brush_circle_mode_t::EDGE_ALIGNED && n % 4u != 0u ) ||
         ( mode == brush_circle_mode_t::SCALABLE && ScalableRadius( n ) == 0u ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const u32 cNeeded = mode == brush_circle_mode_t::SCALABLE ? n + 4u : n;
    if ( cCapacity < cNeeded ) { return geometry_status_t::INSUFFICIENT_CAPACITY; }
    const vec2d_t c{ 0.5 * ( lo.x + hi.x ), 0.5 * ( lo.y + hi.y ) };
    const vec2d_t r{ 0.5 * ( hi.x - lo.x ), 0.5 * ( hi.y - lo.y ) };
    auto place = [&]( f64 ux, f64 uy ) noexcept {
        // Exact box contact where the unit coordinate is +-1 (cos/sin of
        // the quarter angles are not exact in floating point).
        const f64 x = ux >= 1.0 ? hi.x : ( ux <= -1.0 ? lo.x : c.x + r.x * ux );
        const f64 y = uy >= 1.0 ? hi.y : ( uy <= -1.0 ? lo.y : c.y + r.y * uy );
        return vec2d_t{ std::clamp( x, lo.x, hi.x ), std::clamp( y, lo.y, hi.y ) };
    };
    u32 count = 0u;
    if ( mode == brush_circle_mode_t::VERTEX_ALIGNED || mode == brush_circle_mode_t::EDGE_ALIGNED ) {
        const bool bEdge = mode == brush_circle_mode_t::EDGE_ALIGNED;
        const f64 R = bEdge ? 1.0 / std::cos( kPi / static_cast<f64>( n ) ) : 1.0;
        for ( u32 k = 0u; k < n; ++k ) {
            f64 ux = 0.0, uy = 0.0;
            if ( !bEdge && ( 4u * k ) % n == 0u ) {
                // A quarter point: exactly on an axis.
                const u32 q = ( 4u * k ) / n;
                ux = q == 0u ? 1.0 : ( q == 2u ? -1.0 : 0.0 );
                uy = q == 1u ? 1.0 : ( q == 3u ? -1.0 : 0.0 );
            } else {
                const f64 t = kPi * static_cast<f64>( bEdge ? 2u * k + 1u : 2u * k ) / static_cast<f64>( n );
                ux = R * std::cos( t );
                uy = R * std::sin( t );
                // Circumscribed corners next to an axis sit exactly on the box.
                if ( std::fabs( ux ) > 1.0 - 1e-12 ) { ux = ux > 0.0 ? 1.0 : -1.0; }
                if ( std::fabs( uy ) > 1.0 - 1e-12 ) { uy = uy > 0.0 ? 1.0 : -1.0; }
            }
            pOut[count++] = place( ux, uy );
        }
    } else {
        const u32 R = ScalableRadius( n ), k = n / 4u;
        i64 x[25], y[25];
        QuarterTemplate( R, k, x, y );
        const f64 m = std::min( 2.0 * r.x, 2.0 * r.y );
        const f64 unit = m / ( 2.0 * static_cast<f64>( R ) );
        const f64 ex = r.x - 0.5 * m, ey = r.y - 0.5 * m;
        auto emit = [&]( f64 px, f64 py ) noexcept {
            const vec2d_t p{ std::clamp( px, lo.x, hi.x ), std::clamp( py, lo.y, hi.y ) };
            if ( !bKeepDuplicates && count > 0u && pOut[count - 1u].x == p.x && pOut[count - 1u].y == p.y ) { return; }
            pOut[count++] = p;
        };
        for ( int q = 0; q < 4; ++q ) {
            for ( u32 j = 0u; j <= k; ++j ) {
                const f64 tx = unit * static_cast<f64>( x[j] ), ty = unit * static_cast<f64>( y[j] );
                switch ( q ) {
                    case 0: emit( c.x + ex + tx, c.y + ey + ty ); break;
                    case 1: emit( c.x - ex - ty, c.y + ey + tx ); break;
                    case 2: emit( c.x - ex - tx, c.y - ey - ty ); break;
                    default: emit( c.x + ex + ty, c.y - ey - tx ); break;
                }
            }
        }
        if ( !bKeepDuplicates && count > 1u && pOut[count - 1u].x == pOut[0].x && pOut[count - 1u].y == pOut[0].y ) { --count; }
    }
    *pCountOut = count;
    return geometry_status_t::OK;
}

// Drops repeated and collinear corners of a closed polygon, then checks it
// is strictly convex and counter-clockwise (exact predicate). Returns the
// cleaned count, 0 when the polygon is not a proper convex polygon.
u32 CleanConvex( vec2d_t *p, u32 n ) noexcept
{
    bool bChanged = true;
    while ( bChanged && n >= 3u ) {
        bChanged = false;
        for ( u32 i = 0u; i < n; ++i ) {
            const vec2d_t a = p[( i + n - 1u ) % n], b = p[i], cc = p[( i + 1u ) % n];
            const bool bRepeat = a.x == b.x && a.y == b.y;
            if ( bRepeat || math::Orient2D( a, b, cc ) == 0 ) {
                for ( u32 j = i; j + 1u < n; ++j ) { p[j] = p[j + 1u]; }
                --n;
                bChanged = true;
                break;
            }
        }
    }
    if ( n < 3u ) { return 0u; }
    for ( u32 i = 0u; i < n; ++i ) {
        if ( math::Orient2D( p[( i + n - 1u ) % n], p[i], p[( i + 1u ) % n] ) <= 0 ) { return 0u; }
    }
    return n;
}

// ---------------------------------------------------------------------------
// Brush construction
// ---------------------------------------------------------------------------

math::planed_t PlaneFrom( vec3d_t normal, vec3d_t point ) noexcept
{
    math::vec3d_t n{};
    (void)math::Vec3d_TryNormalize( normal, 0.0, &n, nullptr );
    return math::planed_t{ n, -math::Vec3d_Dot( n, point ) };
}

geometry_status_t AddSide( brush_solid_t *pB, const geometry_policy_t &policy, geometry_source_id_allocator_t *pIds, math::planed_t plane ) noexcept
{
    const geometry_source_id_result_t id = GeometrySourceIdAllocator_Allocate( pIds );
    if ( id.status != geometry_status_t::OK ) { return id.status; }
    brush_solid_side_t side{};
    side.plane = plane;
    side.sourceId = id.id;
    side.iAttributeIndex = static_cast<u32>( BrushSolid_SideCount( pB ) );
    return BrushSolid_TryAddSide( pB, policy.limits, side, nullptr );
}

geometry_status_t BeginBrush( brush_solid_t *pB, const allocator_t *pA, geometry_source_id_allocator_t *pIds ) noexcept
{
    const geometry_source_id_result_t id = GeometrySourceIdAllocator_Allocate( pIds );
    if ( id.status != geometry_status_t::OK ) { return id.status; }
    return BrushSolid_Init( pB, pA, id.id );
}

geometry_status_t Finish( brush_solid_t *pB, const geometry_policy_t &policy, const allocator_t *pA ) noexcept
{
    return BrushValidation_Deep( pB, policy, pA ).status;
}

// The convex polygon (in the uAxis/vAxis plane) extruded over [aLo, aHi].
geometry_status_t BuildPrism(
    brush_solid_t *pB, const allocator_t *pA, const geometry_policy_t &policy, geometry_source_id_allocator_t *pIds,
    vec2d_t *poly, u32 n, u32 uAxis, u32 vAxis, u32 aAxis, f64 aLo, f64 aHi ) noexcept
{
    n = CleanConvex( poly, n );
    if ( n == 0u ) { return geometry_status_t::DEGENERATE; }
    geometry_status_t st = BeginBrush( pB, pA, pIds );
    if ( st == geometry_status_t::OK ) { st = BrushSolid_TryReserve( pB, policy.limits, n + 2u ); }
    for ( u32 i = 0u; st == geometry_status_t::OK && i < n; ++i ) {
        const vec2d_t p = poly[i], q = poly[( i + 1u ) % n];
        // Outward normal of a counter-clockwise edge: (dy, -dx).
        const vec3d_t normal = math::Vec3d_Add( Axis( uAxis, q.y - p.y ), Axis( vAxis, -( q.x - p.x ) ) );
        st = AddSide( pB, policy, pIds, PlaneFrom( normal, Point3( uAxis, vAxis, aAxis, p.x, p.y, aLo ) ) );
    }
    if ( st == geometry_status_t::OK ) { st = AddSide( pB, policy, pIds, math::planed_t{ Axis( aAxis, 1.0 ), -aHi } ); }
    if ( st == geometry_status_t::OK ) { st = AddSide( pB, policy, pIds, math::planed_t{ Axis( aAxis, -1.0 ), aLo } ); }
    if ( st == geometry_status_t::OK ) { st = Finish( pB, policy, pA ); }
    return st;
}

// Runs one single-brush construction privately and publishes on success.
template <typename build_t>
geometry_status_t BuildOne( brush_solid_t *pOut, const allocator_t *pA, const geometry_policy_t &policy,
                            geometry_source_id_allocator_t *pIds, build_t &&build ) noexcept
{
    if ( pOut == nullptr || pA == nullptr || pIds == nullptr || !GeometryPolicy_IsValid( policy ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !CanonicalEmpty( *pOut ) ) { return geometry_status_t::ALREADY_INITIALIZED; }
    const geometry_source_id_allocator_t snapshot = *pIds;
    brush_solid_t temp{};
    const geometry_status_t st = build( &temp );
    if ( st != geometry_status_t::OK ) {
        BrushSolid_Shutdown( &temp );
        *pIds = snapshot;
        return st;
    }
    pOut->sourceId = temp.sourceId;
    Vector_Move( &pOut->sides, &temp.sides );
    temp.sourceId = GEOMETRY_SOURCE_ID_INVALID;
    return geometry_status_t::OK;
}

// Runs a multi-brush construction into private temporaries and publishes
// all of them on success. build(temps, capacity, *count) fills temps.
template <typename build_t>
geometry_status_t BuildMany( brush_solid_t *pOut, u32 cCapacity, u32 *pCountOut, const allocator_t *pA,
                             const geometry_policy_t &policy, geometry_source_id_allocator_t *pIds, u32 cMax,
                             build_t &&build ) noexcept
{
    if ( pCountOut != nullptr ) { *pCountOut = 0u; }
    if ( pOut == nullptr || pCountOut == nullptr || pA == nullptr || pIds == nullptr || !GeometryPolicy_IsValid( policy ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    for ( u32 i = 0u; i < cCapacity; ++i ) {
        if ( !CanonicalEmpty( pOut[i] ) ) { return geometry_status_t::ALREADY_INITIALIZED; }
    }
    brush_solid_t *pTemp = Allocator_AllocateArrayStorage<brush_solid_t>( pA, cMax );
    if ( pTemp == nullptr ) { return geometry_status_t::ALLOCATION_FAILED; }
    for ( u32 i = 0u; i < cMax; ++i ) { ::new ( static_cast<void *>( &pTemp[i] ) ) brush_solid_t{}; }
    const geometry_source_id_allocator_t snapshot = *pIds;
    u32 count = 0u;
    geometry_status_t st = build( pTemp, cMax, &count );
    if ( st == geometry_status_t::OK && count > cCapacity ) {
        *pCountOut = count;
        st = geometry_status_t::INSUFFICIENT_CAPACITY;
    }
    if ( st == geometry_status_t::OK ) {
        for ( u32 i = 0u; i < count; ++i ) {
            pOut[i].sourceId = pTemp[i].sourceId;
            Vector_Move( &pOut[i].sides, &pTemp[i].sides );
            pTemp[i].sourceId = GEOMETRY_SOURCE_ID_INVALID;
        }
        *pCountOut = count;
    } else {
        *pIds = snapshot;
    }
    for ( u32 i = 0u; i < cMax; ++i ) {
        BrushSolid_Shutdown( &pTemp[i] );
        pTemp[i].~brush_solid_t();
    }
    Allocator_FreeArrayStorage( pA, pTemp, cMax );
    return st;
}

void OtherAxes( u32 axis, u32 *pU, u32 *pV ) noexcept
{
    *pU = ( axis + 1u ) % 3u;
    *pV = ( axis + 2u ) % 3u;
}

} // namespace

u32 BrushShapes_ScalableCircleUnits( u32 nSides ) noexcept { return 2u * ScalableRadius( nSides ); }

geometry_status_t BrushShapes_TryMakeCircle(
    vec2d_t lo, vec2d_t hi, u32 nSides, brush_circle_mode_t mode, vec2d_t *pPoints, u32 cCapacity, u32 *pCountOut ) noexcept
{
    if ( pCountOut == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    *pCountOut = 0u;
    return MakeCircle( lo, hi, nSides, mode, false, pPoints, cCapacity, pCountOut );
}

geometry_status_t BrushShapes_TryMakeCuboid(
    brush_solid_t *pBrush, const allocator_t *pAllocator, const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator, const brush_shape_box_t &box ) noexcept
{
    if ( !BoxValid( box, policy ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    return BuildOne( pBrush, pAllocator, policy, pIdAllocator, [&]( brush_solid_t *pB ) noexcept {
        vec2d_t rect[4] = { { box.lo.x, box.lo.y }, { box.hi.x, box.lo.y }, { box.hi.x, box.hi.y }, { box.lo.x, box.hi.y } };
        return BuildPrism( pB, pAllocator, policy, pIdAllocator, rect, 4u, 0u, 1u, 2u, box.lo.z, box.hi.z );
    } );
}

geometry_status_t BrushShapes_TryMakeCylinder(
    brush_solid_t *pBrush, const allocator_t *pAllocator, const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator, const brush_shape_box_t &box, u32 axis, u32 nSides,
    brush_circle_mode_t mode ) noexcept
{
    if ( !BoxValid( box, policy ) || axis > 2u ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( nSides + 2u > policy.limits.cBrushSidesPerBrushMax ) { return geometry_status_t::LIMIT_EXCEEDED; }
    u32 uA = 0u, vA = 0u;
    OtherAxes( axis, &uA, &vA );
    vec2d_t poly[kBrushCircleSidesMax + 4u];
    u32 n = 0u;
    const geometry_status_t cs = MakeCircle( { Comp( box.lo, uA ), Comp( box.lo, vA ) }, { Comp( box.hi, uA ), Comp( box.hi, vA ) },
                                             nSides, mode, false, poly, kBrushCircleSidesMax + 4u, &n );
    if ( cs != geometry_status_t::OK ) { return cs; }
    return BuildOne( pBrush, pAllocator, policy, pIdAllocator, [&]( brush_solid_t *pB ) noexcept {
        return BuildPrism( pB, pAllocator, policy, pIdAllocator, poly, n, uA, vA, axis, Comp( box.lo, axis ), Comp( box.hi, axis ) );
    } );
}

geometry_status_t BrushShapes_TryMakeCone(
    brush_solid_t *pBrush, const allocator_t *pAllocator, const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator, const brush_shape_box_t &box, u32 axis, u32 nSides,
    brush_circle_mode_t mode ) noexcept
{
    if ( !BoxValid( box, policy ) || axis > 2u ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( nSides + 1u > policy.limits.cBrushSidesPerBrushMax ) { return geometry_status_t::LIMIT_EXCEEDED; }
    u32 uA = 0u, vA = 0u;
    OtherAxes( axis, &uA, &vA );
    vec2d_t poly[kBrushCircleSidesMax + 4u];
    u32 n = 0u;
    const vec2d_t lo2{ Comp( box.lo, uA ), Comp( box.lo, vA ) }, hi2{ Comp( box.hi, uA ), Comp( box.hi, vA ) };
    const geometry_status_t cs = MakeCircle( lo2, hi2, nSides, mode, false, poly, kBrushCircleSidesMax + 4u, &n );
    if ( cs != geometry_status_t::OK ) { return cs; }
    return BuildOne( pBrush, pAllocator, policy, pIdAllocator, [&]( brush_solid_t *pB ) noexcept {
        n = CleanConvex( poly, n );
        if ( n == 0u ) { return geometry_status_t::DEGENERATE; }
        const f64 aLo = Comp( box.lo, axis ), aHi = Comp( box.hi, axis );
        const vec2d_t c{ 0.5 * ( lo2.x + hi2.x ), 0.5 * ( lo2.y + hi2.y ) };
        const vec3d_t apex = Point3( uA, vA, axis, c.x, c.y, aHi );
        const vec3d_t inside = Point3( uA, vA, axis, c.x, c.y, aLo + 0.25 * ( aHi - aLo ) );
        geometry_status_t st = BeginBrush( pB, pAllocator, pIdAllocator );
        if ( st == geometry_status_t::OK ) { st = BrushSolid_TryReserve( pB, policy.limits, n + 1u ); }
        for ( u32 i = 0u; st == geometry_status_t::OK && i < n; ++i ) {
            const vec3d_t p = Point3( uA, vA, axis, poly[i].x, poly[i].y, aLo );
            const vec3d_t q = Point3( uA, vA, axis, poly[( i + 1u ) % n].x, poly[( i + 1u ) % n].y, aLo );
            vec3d_t normal = math::Vec3d_Cross( math::Vec3d_Subtract( q, p ), math::Vec3d_Subtract( apex, p ) );
            if ( math::Vec3d_Dot( normal, math::Vec3d_Subtract( inside, p ) ) > 0.0 ) { normal = math::Vec3d_Scale( normal, -1.0 ); }
            st = AddSide( pB, policy, pIdAllocator, PlaneFrom( normal, p ) );
        }
        if ( st == geometry_status_t::OK ) { st = AddSide( pB, policy, pIdAllocator, math::planed_t{ Axis( axis, -1.0 ), aLo } ); }
        if ( st == geometry_status_t::OK ) { st = Finish( pB, policy, pAllocator ); }
        return st;
    } );
}

geometry_status_t BrushShapes_TryMakeUvSphere(
    brush_solid_t *pBrush, const allocator_t *pAllocator, const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator, const brush_shape_box_t &box, u32 nSides, u32 nRings,
    brush_circle_mode_t mode ) noexcept
{
    if ( !BoxValid( box, policy ) || nRings < 2u || nRings > kBrushCircleSidesMax || pAllocator == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    // Faces: nSides pole triangles at each end plus nSides quads per inner
    // band. Planes are taken directly from these faces rather than from a
    // point-cloud hull: each band quad is planar by symmetry, and building
    // its plane from all four corners (Newell) keeps it one side instead of
    // two nearly coplanar hull triangles.
    const u32 cRingMax = kBrushCircleSidesMax + 4u;
    vector_t<vec2d_t> rings{};
    if ( !Vector_Init( &rings, pAllocator ) || !Vector_Resize( &rings, static_cast<usize>( nRings - 1u ) * cRingMax ) ) {
        Vector_Shutdown( &rings );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    const vec3d_t c = math::Vec3d_Scale( math::Vec3d_Add( box.lo, box.hi ), 0.5 );
    const f64 rx = 0.5 * ( box.hi.x - box.lo.x ), ry = 0.5 * ( box.hi.y - box.lo.y ), rz = 0.5 * ( box.hi.z - box.lo.z );
    f64 ringZ[kBrushCircleSidesMax];
    u32 n = 0u;
    geometry_status_t st = geometry_status_t::OK;
    for ( u32 j = 1u; st == geometry_status_t::OK && j < nRings; ++j ) {
        const f64 phi = -0.5 * kPi + kPi * static_cast<f64>( j ) / static_cast<f64>( nRings );
        // The ring at the equator touches the box exactly.
        const f64 sc = 2u * j == nRings ? 1.0 : std::cos( phi );
        ringZ[j - 1u] = 2u * j == nRings ? c.z : c.z + rz * std::sin( phi );
        u32 cRing = 0u;
        st = MakeCircle( { c.x - sc * rx, c.y - sc * ry }, { c.x + sc * rx, c.y + sc * ry }, nSides, mode, true,
                         rings.pData + static_cast<usize>( j - 1u ) * cRingMax, cRingMax, &cRing );
        if ( st == geometry_status_t::OK && j > 1u && cRing != n ) { st = geometry_status_t::CORRUPT_STATE; }
        n = cRing;
    }
    if ( st == geometry_status_t::OK && static_cast<u64>( n ) * nRings > policy.limits.cBrushSidesPerBrushMax ) {
        st = geometry_status_t::LIMIT_EXCEEDED;
    }
    if ( st != geometry_status_t::OK ) {
        Vector_Shutdown( &rings );
        return st;
    }
    auto ringPoint = [&]( u32 ring, u32 k ) noexcept {
        const vec2d_t q = rings.pData[static_cast<usize>( ring ) * cRingMax + ( k % n )];
        return math::Vec3d_Make( q.x, q.y, ringZ[ring] );
    };
    const vec3d_t south = math::Vec3d_Make( c.x, c.y, box.lo.z ), north = math::Vec3d_Make( c.x, c.y, box.hi.z );
    st = BuildOne( pBrush, pAllocator, policy, pIdAllocator, [&]( brush_solid_t *pB ) noexcept {
        geometry_status_t bs = BeginBrush( pB, pAllocator, pIdAllocator );
        // One face: plane from its corners (Newell), oriented away from the
        // centre; faces that collapse (repeated scalable corners) are skipped.
        auto face = [&]( const vec3d_t *pts, u32 cPts ) noexcept {
            if ( bs != geometry_status_t::OK ) { return; }
            vec3d_t normal{}, centroid{};
            for ( u32 i = 0u; i < cPts; ++i ) {
                const vec3d_t a = pts[i], b = pts[( i + 1u ) % cPts];
                normal.x += ( a.y - b.y ) * ( a.z + b.z );
                normal.y += ( a.z - b.z ) * ( a.x + b.x );
                normal.z += ( a.x - b.x ) * ( a.y + b.y );
                centroid = math::Vec3d_Add( centroid, a );
            }
            centroid = math::Vec3d_Scale( centroid, 1.0 / static_cast<f64>( cPts ) );
            if ( !( math::Vec3d_LengthSquared( normal ) > 0.0 ) ) { return; }
            if ( math::Vec3d_Dot( normal, math::Vec3d_Subtract( centroid, c ) ) < 0.0 ) { normal = math::Vec3d_Scale( normal, -1.0 ); }
            bs = AddSide( pB, policy, pIdAllocator, PlaneFrom( normal, centroid ) );
        };
        for ( u32 k = 0u; k < n; ++k ) {
            const vec3d_t bottom[3] = { south, ringPoint( 0u, k + 1u ), ringPoint( 0u, k ) };
            face( bottom, 3u );
            for ( u32 j = 0u; j + 2u < nRings; ++j ) {
                const vec3d_t quad[4] = { ringPoint( j, k ), ringPoint( j, k + 1u ), ringPoint( j + 1u, k + 1u ), ringPoint( j + 1u, k ) };
                face( quad, 4u );
            }
            const vec3d_t top[3] = { north, ringPoint( nRings - 2u, k ), ringPoint( nRings - 2u, k + 1u ) };
            face( top, 3u );
        }
        if ( bs == geometry_status_t::OK ) { bs = BrushSolid_TryCanonicalizeSides( pB, policy ); }
        if ( bs == geometry_status_t::OK ) { bs = Finish( pB, policy, pAllocator ); }
        return bs;
    } );
    Vector_Shutdown( &rings );
    return st;
}

geometry_status_t BrushShapes_TryMakeIcoSphere(
    brush_solid_t *pBrush, const allocator_t *pAllocator, const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator, const brush_shape_box_t &box, u32 nSubdivisions ) noexcept
{
    if ( !BoxValid( box, policy ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    const vec3d_t c = math::Vec3d_Scale( math::Vec3d_Add( box.lo, box.hi ), 0.5 );
    const vec3d_t half = math::Vec3d_Scale( math::Vec3d_Subtract( box.hi, box.lo ), 0.5 );
    return BuildOne( pBrush, pAllocator, policy, pIdAllocator, [&]( brush_solid_t *pB ) noexcept {
        // A unit sphere at the centre, then stretched to the box.
        geometry_status_t st = BrushGenerator_TryMakeSphere( pB, pAllocator, policy, pIdAllocator, c, 1.0, nSubdivisions );
        if ( st == geometry_status_t::OK ) { st = BrushTransform_TryScale( pB, c, half ); }
        if ( st == geometry_status_t::OK ) { st = Finish( pB, policy, pAllocator ); }
        return st;
    } );
}

geometry_status_t BrushShapes_TryMakeArch(
    brush_solid_t *pBrushes, u32 cCapacity, u32 *pCountOut, const allocator_t *pAllocator, const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator, const brush_shape_box_t &box, u32 axis, u32 nSides, brush_circle_mode_t mode,
    f64 thickness ) noexcept
{
    if ( pCountOut != nullptr ) { *pCountOut = 0u; }
    if ( !BoxValid( box, policy ) || axis > 1u || !std::isfinite( thickness ) || !( thickness > 0.0 ) || nSides % 2u != 0u ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    // Width runs across the tunnel, up is Z.
    const u32 wA = axis == 0u ? 1u : 0u;
    const f64 wLo = Comp( box.lo, wA ), wHi = Comp( box.hi, wA ), zLo = box.lo.z, zHi = box.hi.z;
    const f64 W = wHi - wLo, H = zHi - zLo;
    if ( !( thickness < 0.5 * W ) || !( thickness < H ) ) { return geometry_status_t::DEGENERATE; }
    // Full outlines (twice as tall, centred on the floor), built around the
    // origin so the floor is exactly y = 0 (a centre computed from
    // zLo - H and zHi could round off it), duplicates kept so outer and
    // inner pair up vertex for vertex.
    vec2d_t outer[kBrushCircleSidesMax + 4u], inner[kBrushCircleSidesMax + 4u];
    u32 nOuter = 0u, nInner = 0u;
    const f64 cw = 0.5 * ( wLo + wHi );
    const f64 hwOut = 0.5 * W, hwIn = 0.5 * W - thickness, hhIn = H - thickness;
    geometry_status_t cs = MakeCircle( { -hwOut, -H }, { hwOut, H }, nSides, mode, true, outer, kBrushCircleSidesMax + 4u, &nOuter );
    if ( cs == geometry_status_t::OK ) {
        cs = MakeCircle( { -hwIn, -hhIn }, { hwIn, hhIn }, nSides, mode, true, inner, kBrushCircleSidesMax + 4u, &nInner );
    }
    if ( cs != geometry_status_t::OK ) { return cs; }
    if ( nOuter != nInner ) { return geometry_status_t::CORRUPT_STATE; }
    // Upper chains, counter-clockwise from the right foot to the left foot:
    // the outline clipped to y >= 0. Vertex-aligned and scalable outlines
    // have vertices on the floor line; edge-aligned ones cross it on their
    // vertical side edges, where the crossing is inserted.
    auto upperChain = [&]( const vec2d_t *poly, u32 n, vec2d_t *pChain, u32 *pCount ) noexcept {
        u32 count = 0u;
        u32 start = 0u;
        for ( u32 i = 0u; i < n; ++i ) {
            if ( poly[i].y < 0.0 && poly[( i + 1u ) % n].y >= 0.0 ) { start = ( i + 1u ) % n; }
        }
        auto crossing = [&]( vec2d_t a, vec2d_t b ) noexcept {
            const f64 t = ( 0.0 - a.y ) / ( b.y - a.y );
            return vec2d_t{ a.x + t * ( b.x - a.x ), 0.0 };
        };
        if ( poly[start].y > 0.0 ) { pChain[count++] = crossing( poly[( start + n - 1u ) % n], poly[start] ); }
        for ( u32 s = 0u; s < n; ++s ) {
            const u32 i = ( start + s ) % n;
            if ( poly[i].y < 0.0 ) {
                const vec2d_t a = poly[( i + n - 1u ) % n];
                if ( a.y > 0.0 ) { pChain[count++] = crossing( a, poly[i] ); }
                break;
            }
            pChain[count++] = poly[i];
        }
        // Into world coordinates: floor at zLo, centred across the width.
        for ( u32 i = 0u; i < count; ++i ) { pChain[i] = vec2d_t{ cw + pChain[i].x, zLo + pChain[i].y }; }
        *pCount = count;
    };
    vec2d_t oc[kBrushCircleSidesMax + 8u], ic[kBrushCircleSidesMax + 8u];
    u32 nOc = 0u, nIc = 0u;
    upperChain( outer, nOuter, oc, &nOc );
    upperChain( inner, nInner, ic, &nIc );
    if ( nOc != nIc || nOc < 2u ) { return geometry_status_t::DEGENERATE; }
    const u32 cMax = nOc - 1u;
    return BuildMany( pBrushes, cCapacity, pCountOut, pAllocator, policy, pIdAllocator, cMax,
                      [&]( brush_solid_t *pTemp, u32, u32 *pCount ) noexcept {
                          u32 count = 0u;
                          for ( u32 i = 0u; i + 1u < nOc; ++i ) {
                              const bool bOuterFlat = oc[i].x == oc[i + 1u].x && oc[i].y == oc[i + 1u].y;
                              const bool bInnerFlat = ic[i].x == ic[i + 1u].x && ic[i].y == ic[i + 1u].y;
                              if ( bOuterFlat && bInnerFlat ) { continue; }
                              vec2d_t quad[4] = { oc[i], oc[i + 1u], ic[i + 1u], ic[i] };
                              const geometry_status_t st = BuildPrism( &pTemp[count], pAllocator, policy, pIdAllocator, quad, 4u, wA, 2u,
                                                                       axis, Comp( box.lo, axis ), Comp( box.hi, axis ) );
                              if ( st != geometry_status_t::OK ) { return st; }
                              ++count;
                          }
                          *pCount = count;
                          return geometry_status_t::OK;
                      } );
}

geometry_status_t BrushShapes_TryMakeStairs(
    brush_solid_t *pBrushes, u32 cCapacity, u32 *pCountOut, const allocator_t *pAllocator, const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator, const brush_shape_box_t &box, brush_stairs_direction_t direction,
    f64 stepHeight ) noexcept
{
    if ( pCountOut != nullptr ) { *pCountOut = 0u; }
    if ( !BoxValid( box, policy ) || !std::isfinite( stepHeight ) || !( stepHeight > 0.0 ) ||
         static_cast<u8>( direction ) > static_cast<u8>( brush_stairs_direction_t::NEG_Y ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const f64 H = box.hi.z - box.lo.z;
    const f64 steps = std::ceil( H / stepHeight - 1e-9 );
    if ( !( steps >= 1.0 ) || steps > static_cast<f64>( policy.limits.cBrushesMax ) || steps > 65536.0 ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    const u32 n = static_cast<u32>( steps );
    const bool bAlongX = direction == brush_stairs_direction_t::POS_X || direction == brush_stairs_direction_t::NEG_X;
    const bool bPositive = direction == brush_stairs_direction_t::POS_X || direction == brush_stairs_direction_t::POS_Y;
    const u32 dA = bAlongX ? 0u : 1u, sA = bAlongX ? 1u : 0u;
    const f64 dLo = Comp( box.lo, dA ), dHi = Comp( box.hi, dA ), depth = ( dHi - dLo ) / static_cast<f64>( n );
    if ( n > cCapacity ) {
        if ( pCountOut != nullptr ) { *pCountOut = n; }
        return geometry_status_t::INSUFFICIENT_CAPACITY;
    }
    return BuildMany( pBrushes, cCapacity, pCountOut, pAllocator, policy, pIdAllocator, n,
                      [&]( brush_solid_t *pTemp, u32, u32 *pCount ) noexcept {
                          for ( u32 i = 0u; i < n; ++i ) {
                              // Step i spans [s0, s1] measured from where the climb
                              // starts; the last step ends exactly at the far side.
                              const f64 s0 = depth * static_cast<f64>( i );
                              const f64 s1 = i + 1u == n ? dHi - dLo : depth * static_cast<f64>( i + 1u );
                              const f64 lo = bPositive ? ( i == 0u ? dLo : dLo + s0 ) : ( i + 1u == n ? dLo : dHi - s1 );
                              const f64 hi = bPositive ? ( i + 1u == n ? dHi : dLo + s1 ) : ( i == 0u ? dHi : dHi - s0 );
                              const f64 top = i + 1u == n ? box.hi.z : box.lo.z + stepHeight * static_cast<f64>( i + 1u );
                              vec2d_t rect[4] = { { lo, box.lo.z }, { hi, box.lo.z }, { hi, top }, { lo, top } };
                              // (dA, z) plane extruded across the stair width.
                              const geometry_status_t st = BuildPrism( &pTemp[i], pAllocator, policy, pIdAllocator, rect, 4u, dA, 2u, sA,
                                                                       Comp( box.lo, sA ), Comp( box.hi, sA ) );
                              if ( st != geometry_status_t::OK ) { return st; }
                          }
                          *pCount = n;
                          return geometry_status_t::OK;
                      } );
}

} // namespace cypher::editor::geometry
