//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushPrimitives.cpp
//  Purpose: Implements convex primitive and architectural generators.
//  Details: Generators work in a local frame (run, lateral, up) and map to
//           world axes at the end. Prisms and cones build their planes
//           directly; shapes whose faces are easier to describe by their
//           corners (spheres, arch voussoirs, pipe segments) use the point
//           hull. Every piece is reduced before it leaves.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushPrimitives.h"

#include <cmath>

namespace cypher::editor::geometry
{

namespace
{

using common::bool_t;
using common::u32;
using common::usize;
using math::f64;
using math::vec3d_t;

constexpr f64 cPi = 3.14159265358979323846;

// Local frame: x = run, y = lateral, z = up.
struct local_frame_t {
    u32 up;
    u32 run;
    u32 lateral;
    vec3d_t minimum; // local
    vec3d_t maximum; // local
};

f64 Get( vec3d_t v, u32 axis ) noexcept
{
    return axis == 0u ? v.x : ( axis == 1u ? v.y : v.z );
}

void Set( vec3d_t *pV, u32 axis, f64 value ) noexcept
{
    ( axis == 0u ? pV->x : ( axis == 1u ? pV->y : pV->z ) ) = value;
}

vec3d_t ToWorld( const local_frame_t &frame, vec3d_t local ) noexcept
{
    vec3d_t world{};
    Set( &world, frame.run, local.x );
    Set( &world, frame.lateral, local.y );
    Set( &world, frame.up, local.z );
    return world;
}

geometry_status_t MakeFrame(
    const geometry_policy_t &policy,
    const geometry_primitive_frame_t &frame,
    local_frame_t *pOut ) noexcept
{
    const u32 up = static_cast<u32>( frame.up );
    if ( up > 2u || !math::Aabbd_IsFinite( frame.bounds ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const f64 limit = policy.numerical.fCoordinateMagnitudeLimit;
    for ( u32 axis = 0u; axis < 3u; ++axis ) {
        const f64 lo = Get( frame.bounds.minimum, axis );
        const f64 hi = Get( frame.bounds.maximum, axis );
        if ( !( hi > lo ) ) {
            return geometry_status_t::INVALID_ARGUMENT;
        }
        if ( hi - lo < policy.numerical.fMinimumEdgeLength ) {
            return geometry_status_t::DEGENERATE;
        }
        if ( math::Scalar_Abs( lo ) > limit || math::Scalar_Abs( hi ) > limit ) {
            return geometry_status_t::LIMIT_EXCEEDED;
        }
    }
    pOut->up = up;
    pOut->run = ( up + 1u ) % 3u;
    pOut->lateral = ( up + 2u ) % 3u;
    pOut->minimum = math::Vec3d_Make( Get( frame.bounds.minimum, pOut->run ),
                                      Get( frame.bounds.minimum, pOut->lateral ),
                                      Get( frame.bounds.minimum, up ) );
    pOut->maximum = math::Vec3d_Make( Get( frame.bounds.maximum, pOut->run ),
                                      Get( frame.bounds.maximum, pOut->lateral ),
                                      Get( frame.bounds.maximum, up ) );
    return geometry_status_t::OK;
}

// Appends the plane with local outward normal n through local point p.
geometry_status_t AddLocalPlane(
    geometry_brush_piece_t *pPiece,
    const geometry_policy_t &policy,
    const local_frame_t &frame,
    vec3d_t localNormal,
    vec3d_t localPoint ) noexcept
{
    vec3d_t n{};
    if ( !math::Vec3d_TryNormalize( ToWorld( frame, localNormal ), 1.0e-12, &n, nullptr ) ) {
        return geometry_status_t::DEGENERATE;
    }
    geometry_piece_plane_t plane{};
    plane.plane = math::Planed_Make( n, -math::Vec3d_Dot( n, ToWorld( frame, localPoint ) ) );
    return BrushPiece_TryAppendPlane( pPiece, policy, plane );
}

// Local box planes; mask selects which of +x -x +y -y +z -z to emit.
geometry_status_t AddBoxPlanes(
    geometry_brush_piece_t *pPiece,
    const geometry_policy_t &policy,
    const local_frame_t &frame,
    vec3d_t minimum,
    vec3d_t maximum,
    u32 mask ) noexcept
{
    const vec3d_t normals[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
                                 { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    geometry_status_t status = geometry_status_t::OK;
    for ( u32 i = 0u; status == geometry_status_t::OK && i < 6u; ++i ) {
        if ( ( mask & ( 1u << i ) ) != 0u ) {
            status = AddLocalPlane( pPiece, policy, frame, normals[i],
                                    ( i % 2u == 0u ) ? maximum : minimum );
        }
    }
    return status;
}

geometry_status_t Finish(
    geometry_brush_piece_t *pPiece, const geometry_policy_t &policy, geometry_status_t status ) noexcept
{
    if ( status == geometry_status_t::OK ) {
        geometry_piece_extent_t extent{};
        status = BrushPiece_TryReduce( pPiece, policy, &extent, nullptr );
        if ( status == geometry_status_t::OK && extent != geometry_piece_extent_t::SOLID ) {
            status = geometry_status_t::DEGENERATE;
        }
    }
    if ( status != geometry_status_t::OK ) {
        BrushPiece_Clear( pPiece );
    }
    return status;
}

geometry_status_t FromLocalPoints(
    geometry_brush_piece_t *pPiece,
    const geometry_policy_t &policy,
    const local_frame_t &frame,
    vec3d_t *pPoints,
    usize cPoints ) noexcept
{
    for ( usize i = 0u; i < cPoints; ++i ) {
        pPoints[i] = ToWorld( frame, pPoints[i] );
    }
    return BrushPiece_TryFromPoints( pPiece, { pPoints, cPoints }, policy );
}

// Ellipse ring point k of n in the local (x, y) plane around the frame
// center, fitted per alignment.
vec3d_t RingPoint( const local_frame_t &frame, u32 k, u32 n, geometry_circle_alignment_t alignment,
                   f64 z ) noexcept
{
    const f64 cx = 0.5 * ( frame.minimum.x + frame.maximum.x );
    const f64 cy = 0.5 * ( frame.minimum.y + frame.maximum.y );
    f64 rx = 0.5 * ( frame.maximum.x - frame.minimum.x );
    f64 ry = 0.5 * ( frame.maximum.y - frame.minimum.y );
    const f64 sector = 2.0 * cPi / static_cast<f64>( n );
    f64 angle = sector * static_cast<f64>( k );
    if ( alignment == geometry_circle_alignment_t::EDGE ) {
        // Offset by half a sector and push vertices out so edge midpoints
        // touch the ellipse -- the polygon then fills its bounds on the
        // axes that have an edge there.
        angle += 0.5 * sector;
        const f64 stretch = 1.0 / std::cos( 0.5 * sector );
        rx *= stretch;
        ry *= stretch;
    }
    return math::Vec3d_Make( cx + rx * std::cos( angle ), cy + ry * std::sin( angle ), z );
}

bool_t IsListReady( const geometry_piece_list_t *pList ) noexcept
{
    return pList != nullptr && pList->planes.pAllocator != nullptr;
}

struct scoped_piece_t {
    geometry_brush_piece_t piece{};
    ~scoped_piece_t() noexcept { BrushPiece_Shutdown( &piece ); }
};

} // namespace

geometry_status_t Primitive_TryBox(
    const geometry_policy_t &policy,
    const geometry_primitive_frame_t &frame,
    geometry_brush_piece_t *pOut ) noexcept
{
    if ( pOut == nullptr || pOut->planes.pAllocator == nullptr ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    BrushPiece_Clear( pOut );
    local_frame_t local{};
    geometry_status_t status = MakeFrame( policy, frame, &local );
    if ( status == geometry_status_t::OK ) {
        status = AddBoxPlanes( pOut, policy, local, local.minimum, local.maximum, 0x3Fu );
    }
    return Finish( pOut, policy, status );
}

geometry_status_t Primitive_TryWedge(
    const geometry_policy_t &policy,
    const geometry_primitive_frame_t &frame,
    bool_t bDescending,
    geometry_brush_piece_t *pOut ) noexcept
{
    if ( pOut == nullptr || pOut->planes.pAllocator == nullptr ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    BrushPiece_Clear( pOut );
    local_frame_t local{};
    geometry_status_t status = MakeFrame( policy, frame, &local );
    if ( status != geometry_status_t::OK ) {
        return Finish( pOut, policy, status );
    }
    const vec3d_t mn = local.minimum;
    const vec3d_t mx = local.maximum;
    const f64 dx = mx.x - mn.x;
    const f64 dz = mx.z - mn.z;
    // Bottom, both lateral walls, the tall back wall, and the slope.
    status = AddBoxPlanes( pOut, policy, local, mn, mx,
                           ( 1u << 2u ) | ( 1u << 3u ) | ( 1u << 5u ) |
                               ( bDescending ? ( 1u << 1u ) : ( 1u << 0u ) ) );
    if ( status == geometry_status_t::OK ) {
        const vec3d_t normal = bDescending ? math::Vec3d_Make( dz, 0.0, dx )
                                           : math::Vec3d_Make( -dz, 0.0, dx );
        const vec3d_t through = bDescending ? math::Vec3d_Make( mn.x, mn.y, mx.z )
                                            : math::Vec3d_Make( mx.x, mn.y, mx.z );
        status = AddLocalPlane( pOut, policy, local, normal, through );
    }
    return Finish( pOut, policy, status );
}

geometry_status_t Primitive_TryCylinder(
    const geometry_policy_t &policy,
    const geometry_primitive_frame_t &frame,
    u32 cSides,
    geometry_circle_alignment_t alignment,
    geometry_brush_piece_t *pOut ) noexcept
{
    if ( pOut == nullptr || pOut->planes.pAllocator == nullptr ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    BrushPiece_Clear( pOut );
    if ( cSides < PRIMITIVE_SIDES_MIN || cSides > PRIMITIVE_SIDES_MAX ||
         static_cast<u32>( alignment ) >= static_cast<u32>( geometry_circle_alignment_t::COUNT ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    local_frame_t local{};
    geometry_status_t status = MakeFrame( policy, frame, &local );
    if ( status == geometry_status_t::OK ) {
        status = AddBoxPlanes( pOut, policy, local, local.minimum, local.maximum,
                               ( 1u << 4u ) | ( 1u << 5u ) );
    }
    for ( u32 k = 0u; status == geometry_status_t::OK && k < cSides; ++k ) {
        const vec3d_t a = RingPoint( local, k, cSides, alignment, local.minimum.z );
        const vec3d_t b = RingPoint( local, ( k + 1u ) % cSides, cSides, alignment, local.minimum.z );
        // CCW ring: outward normal of edge a->b is (dy, -dx).
        status = AddLocalPlane( pOut, policy, local, math::Vec3d_Make( b.y - a.y, a.x - b.x, 0.0 ), a );
    }
    return Finish( pOut, policy, status );
}

geometry_status_t Primitive_TryCone(
    const geometry_policy_t &policy,
    const geometry_primitive_frame_t &frame,
    u32 cSides,
    geometry_circle_alignment_t alignment,
    geometry_brush_piece_t *pOut ) noexcept
{
    if ( pOut == nullptr || pOut->planes.pAllocator == nullptr ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    BrushPiece_Clear( pOut );
    if ( cSides < PRIMITIVE_SIDES_MIN || cSides > PRIMITIVE_SIDES_MAX ||
         static_cast<u32>( alignment ) >= static_cast<u32>( geometry_circle_alignment_t::COUNT ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    local_frame_t local{};
    geometry_status_t status = MakeFrame( policy, frame, &local );
    if ( status == geometry_status_t::OK ) {
        status = AddBoxPlanes( pOut, policy, local, local.minimum, local.maximum, 1u << 5u );
    }
    const vec3d_t apex = math::Vec3d_Make( 0.5 * ( local.minimum.x + local.maximum.x ),
                                           0.5 * ( local.minimum.y + local.maximum.y ),
                                           local.maximum.z );
    for ( u32 k = 0u; status == geometry_status_t::OK && k < cSides; ++k ) {
        const vec3d_t a = RingPoint( local, k, cSides, alignment, local.minimum.z );
        const vec3d_t b = RingPoint( local, ( k + 1u ) % cSides, cSides, alignment, local.minimum.z );
        // (b - a) x (apex - a) points outward for a CCW base ring.
        const vec3d_t normal = math::Vec3d_Cross( math::Vec3d_Subtract( b, a ),
                                                  math::Vec3d_Subtract( apex, a ) );
        status = AddLocalPlane( pOut, policy, local, normal, a );
    }
    return Finish( pOut, policy, status );
}

geometry_status_t Primitive_TrySphere(
    const geometry_policy_t &policy,
    const geometry_primitive_frame_t &frame,
    u32 cSlices,
    u32 cStacks,
    geometry_brush_piece_t *pOut ) noexcept
{
    if ( pOut == nullptr || pOut->planes.pAllocator == nullptr ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    BrushPiece_Clear( pOut );
    if ( cSlices < 3u || cSlices > 32u || cStacks < 2u || cStacks > 16u ||
         cSlices * ( cStacks - 1u ) + 2u > BRUSH_PIECE_HULL_POINTS_MAX ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    local_frame_t local{};
    geometry_status_t status = MakeFrame( policy, frame, &local );
    if ( status != geometry_status_t::OK ) {
        return Finish( pOut, policy, status );
    }
    const vec3d_t center = math::Vec3d_Scale( math::Vec3d_Add( local.minimum, local.maximum ), 0.5 );
    const vec3d_t radius = math::Vec3d_Scale( math::Vec3d_Subtract( local.maximum, local.minimum ), 0.5 );
    vec3d_t points[BRUSH_PIECE_HULL_POINTS_MAX];
    usize cPoints = 0u;
    points[cPoints++] = math::Vec3d_Make( center.x, center.y, local.minimum.z );
    points[cPoints++] = math::Vec3d_Make( center.x, center.y, local.maximum.z );
    for ( u32 s = 1u; s < cStacks; ++s ) {
        const f64 polar = cPi * static_cast<f64>( s ) / static_cast<f64>( cStacks );
        for ( u32 k = 0u; k < cSlices; ++k ) {
            const f64 azimuth = 2.0 * cPi * static_cast<f64>( k ) / static_cast<f64>( cSlices );
            points[cPoints++] = math::Vec3d_Make(
                center.x + radius.x * std::sin( polar ) * std::cos( azimuth ),
                center.y + radius.y * std::sin( polar ) * std::sin( azimuth ),
                center.z - radius.z * std::cos( polar ) );
        }
    }
    status = FromLocalPoints( pOut, policy, local, points, cPoints );
    return Finish( pOut, policy, status );
}

geometry_status_t Primitive_TryStairs(
    const geometry_policy_t &policy,
    const geometry_primitive_frame_t &frame,
    f64 stepHeight,
    bool_t bDescending,
    geometry_piece_list_t *pOut ) noexcept
{
    if ( !IsListReady( pOut ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    local_frame_t local{};
    geometry_status_t status = MakeFrame( policy, frame, &local );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    const f64 height = local.maximum.z - local.minimum.z;
    if ( !math::Scalar_IsFinite( stepHeight ) || !( stepHeight > 0.0 ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const f64 cStepsReal = std::ceil( height / stepHeight );
    if ( cStepsReal > static_cast<f64>( PRIMITIVE_SIDES_MAX ) ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    const u32 cSteps = static_cast<u32>( cStepsReal < 1.0 ? 1.0 : cStepsReal );
    const f64 tread = ( local.maximum.x - local.minimum.x ) / static_cast<f64>( cSteps );

    const usize cEntry = PieceList_Count( pOut );
    scoped_piece_t step{};
    status = BrushPiece_Init( &step.piece, pOut->planes.pAllocator );
    for ( u32 i = 0u; status == geometry_status_t::OK && i < cSteps; ++i ) {
        const u32 slot = bDescending ? ( cSteps - 1u - i ) : i;
        vec3d_t mn = local.minimum;
        vec3d_t mx = local.maximum;
        mn.x = local.minimum.x + tread * static_cast<f64>( slot );
        mx.x = ( slot + 1u == cSteps ) ? local.maximum.x : mn.x + tread;
        const f64 top = local.minimum.z + stepHeight * static_cast<f64>( i + 1u );
        mx.z = top < local.maximum.z ? top : local.maximum.z;
        BrushPiece_Clear( &step.piece );
        status = Finish( &step.piece, policy,
                         AddBoxPlanes( &step.piece, policy, local, mn, mx, 0x3Fu ) );
        if ( status == geometry_status_t::OK ) {
            status = PieceList_TryAppend( pOut, &step.piece );
        }
    }
    if ( status != geometry_status_t::OK ) {
        PieceList_Truncate( pOut, cEntry );
    }
    return status;
}

geometry_status_t Primitive_TryArch(
    const geometry_policy_t &policy,
    const geometry_primitive_frame_t &frame,
    u32 cSegments,
    f64 thickness,
    geometry_piece_list_t *pOut ) noexcept
{
    if ( !IsListReady( pOut ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( cSegments < 1u || cSegments > PRIMITIVE_SIDES_MAX || !math::Scalar_IsFinite( thickness ) ||
         !( thickness > 0.0 ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    local_frame_t local{};
    geometry_status_t status = MakeFrame( policy, frame, &local );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    // Outer semi-ellipse fills the frame; the springing line is its bottom.
    const f64 cx = 0.5 * ( local.minimum.x + local.maximum.x );
    const f64 outerX = 0.5 * ( local.maximum.x - local.minimum.x );
    const f64 outerZ = local.maximum.z - local.minimum.z;
    const f64 innerX = outerX - thickness;
    const f64 innerZ = outerZ - thickness;
    if ( !( innerX > policy.numerical.fMinimumEdgeLength ) ||
         !( innerZ > policy.numerical.fMinimumEdgeLength ) ) {
        return geometry_status_t::DEGENERATE;
    }

    const usize cEntry = PieceList_Count( pOut );
    scoped_piece_t segment{};
    status = BrushPiece_Init( &segment.piece, pOut->planes.pAllocator );
    for ( u32 i = 0u; status == geometry_status_t::OK && i < cSegments; ++i ) {
        const f64 a0 = cPi * static_cast<f64>( i ) / static_cast<f64>( cSegments );
        const f64 a1 = cPi * static_cast<f64>( i + 1u ) / static_cast<f64>( cSegments );
        vec3d_t points[8];
        usize c = 0u;
        for ( f64 angle : { a0, a1 } ) {
            for ( f64 y : { local.minimum.y, local.maximum.y } ) {
                points[c++] = math::Vec3d_Make( cx + outerX * std::cos( angle ), y,
                                                local.minimum.z + outerZ * std::sin( angle ) );
                points[c++] = math::Vec3d_Make( cx + innerX * std::cos( angle ), y,
                                                local.minimum.z + innerZ * std::sin( angle ) );
            }
        }
        status = Finish( &segment.piece, policy,
                         FromLocalPoints( &segment.piece, policy, local, points, c ) );
        if ( status == geometry_status_t::OK ) {
            status = PieceList_TryAppend( pOut, &segment.piece );
        }
    }
    if ( status != geometry_status_t::OK ) {
        PieceList_Truncate( pOut, cEntry );
    }
    return status;
}

geometry_status_t Primitive_TryPipe(
    const geometry_policy_t &policy,
    const geometry_primitive_frame_t &frame,
    u32 cSides,
    f64 wallThickness,
    geometry_piece_list_t *pOut ) noexcept
{
    if ( !IsListReady( pOut ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( cSides < PRIMITIVE_SIDES_MIN || cSides > PRIMITIVE_SIDES_MAX ||
         !math::Scalar_IsFinite( wallThickness ) || !( wallThickness > 0.0 ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    local_frame_t local{};
    geometry_status_t status = MakeFrame( policy, frame, &local );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    // Inner ring: the outer ellipse shrunk by the wall thickness.
    local_frame_t inner = local;
    inner.minimum.x += wallThickness;
    inner.minimum.y += wallThickness;
    inner.maximum.x -= wallThickness;
    inner.maximum.y -= wallThickness;
    if ( !( inner.maximum.x - inner.minimum.x > policy.numerical.fMinimumEdgeLength ) ||
         !( inner.maximum.y - inner.minimum.y > policy.numerical.fMinimumEdgeLength ) ) {
        return geometry_status_t::DEGENERATE;
    }

    const usize cEntry = PieceList_Count( pOut );
    scoped_piece_t segment{};
    status = BrushPiece_Init( &segment.piece, pOut->planes.pAllocator );
    for ( u32 k = 0u; status == geometry_status_t::OK && k < cSides; ++k ) {
        const u32 next = ( k + 1u ) % cSides;
        vec3d_t points[8];
        usize c = 0u;
        for ( f64 z : { local.minimum.z, local.maximum.z } ) {
            points[c++] = RingPoint( local, k, cSides, geometry_circle_alignment_t::EDGE, z );
            points[c++] = RingPoint( local, next, cSides, geometry_circle_alignment_t::EDGE, z );
            points[c++] = RingPoint( inner, k, cSides, geometry_circle_alignment_t::EDGE, z );
            points[c++] = RingPoint( inner, next, cSides, geometry_circle_alignment_t::EDGE, z );
        }
        status = Finish( &segment.piece, policy,
                         FromLocalPoints( &segment.piece, policy, local, points, c ) );
        if ( status == geometry_status_t::OK ) {
            status = PieceList_TryAppend( pOut, &segment.piece );
        }
    }
    if ( status != geometry_status_t::OK ) {
        PieceList_Truncate( pOut, cEntry );
    }
    return status;
}

} // namespace cypher::editor::geometry
