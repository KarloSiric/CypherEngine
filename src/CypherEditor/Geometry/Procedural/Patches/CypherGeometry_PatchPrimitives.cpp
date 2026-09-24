//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_PatchPrimitives.cpp
//  Purpose: Implements the revolve and extrude patch generators and the
//           primitives built on them.
//  Details: The circle's arc ends are computed once as 2D unit points and
//           the closing point is copied from the first rather than
//           recomputed from cos/sin(2 pi), which would differ in the last
//           bits. Every revolved row reuses the same unit points, so the u
//           seam is bit-exact in every row.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_PatchPrimitives.h"

#include <cmath>
#include <limits>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

constexpr f64 kPi = 3.14159265358979323846;

// Unit circle controls for n arcs: 2n + 1 points, even indices on the
// circle, odd indices at the tangent intersections, last == first.
void UnitCircleControls( u32 n, math::vec2d_t *pOut ) noexcept
{
    const f64 half = kPi / static_cast<f64>( n );
    const f64 ctrl = 1.0 / std::cos( half );
    for ( u32 k = 0u; k < n; ++k ) {
        const f64 a = 2.0 * half * static_cast<f64>( k );
        pOut[2u * k] = math::vec2d_t{ std::cos( a ), std::sin( a ) };
        pOut[2u * k + 1u] = math::vec2d_t{ ctrl * std::cos( a + half ), ctrl * std::sin( a + half ) };
    }
    pOut[2u * n] = pOut[0];
}

bool FrameBasis( const patch_revolve_frame_t &frame, math::vec3d_t *pE1, math::vec3d_t *pE2, math::vec3d_t *pAxis ) noexcept
{
    if ( !math::Vec3d_IsFinite( frame.center ) || !math::Vec3d_IsFinite( frame.axis ) ||
         !math::Vec3d_IsFinite( frame.reference ) ) {
        return false;
    }
    if ( !math::Vec3d_TryNormalize( frame.axis, 1e-12, pAxis, nullptr ) ) { return false; }
    const math::vec3d_t rejected = math::Vec3d_RejectFromUnit( frame.reference, *pAxis );
    // A reference (nearly) parallel to the axis gives no angle-0 direction.
    if ( !math::Vec3d_TryNormalize( rejected, 1e-9 * std::sqrt( math::Vec3d_LengthSquared( frame.reference ) ),
                                    pE1, nullptr ) ) {
        return false;
    }
    // e2 = axis x e1 makes (e1, e2, axis) right-handed, so increasing angle
    // is counter-clockwise seen from +axis and walls face outward.
    *pE2 = math::Vec3d_Cross( *pAxis, *pE1 );
    return true;
}

bool ValidProfileCount( usize count ) noexcept
{
    return count >= 3u && count <= kPatchControlsPerAxisMax && ( count - 1u ) % 2u == 0u;
}

// Fills a freshly initialized patch control-by-control from a position
// callback, assigning UVs from the grid and IDs from a local allocator
// copy. Commits the allocator only on success.
template <typename position_fn_t>
geometry_status_t FillPatch(
    patch_surface_t *pOut,
    const allocator_t *pAllocator,
    u32 cColumns,
    u32 cRows,
    geometry_source_id_t patchId,
    geometry_source_id_allocator_t *pIdAllocator,
    u32 materialId,
    bool bReverseColumns,
    position_fn_t &&positionAt ) noexcept
{
    if ( pIdAllocator == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( static_cast<u64>( cColumns ) * cRows > kPatchControlsMax ) { return geometry_status_t::LIMIT_EXCEEDED; }
    const geometry_status_t init =
        Patch_Init( pOut, pAllocator, patch_basis_t::BIQUADRATIC_BEZIER, cColumns, cRows, patchId );
    if ( init != geometry_status_t::OK ) { return init; }
    geometry_source_id_allocator_t ids = *pIdAllocator;
    for ( u32 j = 0u; j < cRows; ++j ) {
        for ( u32 i = 0u; i < cColumns; ++i ) {
            const geometry_source_id_result_t id = GeometrySourceIdAllocator_Allocate( &ids );
            if ( id.status != geometry_status_t::OK ) {
                Patch_Shutdown( pOut );
                return id.status;
            }
            patch_control_t c{};
            c.position = positionAt( i, j );
            c.uv = math::vec2d_t{ static_cast<f64>( i ) / static_cast<f64>( cColumns - 1u ),
                                  static_cast<f64>( j ) / static_cast<f64>( cRows - 1u ) };
            c.sourceId = id.id;
            const geometry_status_t set = Patch_TrySetControl( pOut, i, j, c );
            if ( set != geometry_status_t::OK ) {
                Patch_Shutdown( pOut );
                return set;
            }
        }
    }
    if ( bReverseColumns ) { Patch_ReverseColumns( pOut ); }
    pOut->materialId = materialId;
    if ( Patch_Validate( pOut, pAllocator ).fault == patch_fault_t::COLLAPSED_SURFACE ) {
        Patch_Shutdown( pOut );
        return geometry_status_t::DEGENERATE;
    }
    *pIdAllocator = ids;
    return geometry_status_t::OK;
}

bool PositiveFinite( f64 v ) noexcept { return v > 0.0 && std::isfinite( v ); }

} // namespace

f64 PatchPrimitive_CircleRadialError( u32 cSegments ) noexcept
{
    if ( cSegments < kPatchPrimitiveSegmentsMin || cSegments > kPatchPrimitiveSegmentsMax ) {
        return std::numeric_limits<f64>::infinity();
    }
    const f64 half = kPi / static_cast<f64>( cSegments );
    return 0.5 * ( std::cos( half ) + 1.0 / std::cos( half ) ) - 1.0;
}

geometry_status_t PatchPrimitive_TryRevolve(
    span_t<const math::vec2d_t> profile,
    const patch_revolve_frame_t &frame,
    const patch_primitive_common_t &common,
    geometry_source_id_t patchId,
    geometry_source_id_allocator_t *pIdAllocator,
    const allocator_t *pAllocator,
    patch_surface_t *pOut ) noexcept
{
    if ( pOut == nullptr || ( profile.nCount > 0u && profile.pData == nullptr ) || !ValidProfileCount( profile.nCount ) ||
         common.cSegments < kPatchPrimitiveSegmentsMin || common.cSegments > kPatchPrimitiveSegmentsMax ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    bool bAnyRadius = false;
    for ( usize k = 0u; k < profile.nCount; ++k ) {
        const math::vec2d_t p = profile.pData[k];
        if ( !math::Vec2d_IsFinite( p ) ) { return geometry_status_t::NUMERIC_FAILURE; }
        if ( p.x < 0.0 ) { return geometry_status_t::INVALID_ARGUMENT; }
        bAnyRadius = bAnyRadius || p.x > 0.0;
    }
    if ( !bAnyRadius ) { return geometry_status_t::DEGENERATE; }
    math::vec3d_t e1{}, e2{}, axis{};
    if ( !FrameBasis( frame, &e1, &e2, &axis ) ) { return geometry_status_t::INVALID_ARGUMENT; }

    math::vec2d_t circle[2u * kPatchPrimitiveSegmentsMax + 1u];
    UnitCircleControls( common.cSegments, circle );
    const u32 cColumns = 2u * common.cSegments + 1u;
    const u32 cRows = static_cast<u32>( profile.nCount );
    return FillPatch( pOut, pAllocator, cColumns, cRows, patchId, pIdAllocator, common.materialId, common.bInward,
                      [&]( u32 i, u32 j ) noexcept {
                          const f64 r = profile.pData[j].x, z = profile.pData[j].y;
                          return math::Vec3d_Add(
                              frame.center,
                              math::Vec3d_Add( math::Vec3d_Add( math::Vec3d_Scale( e1, r * circle[i].x ),
                                                                math::Vec3d_Scale( e2, r * circle[i].y ) ),
                                               math::Vec3d_Scale( axis, z ) ) );
                      } );
}

geometry_status_t PatchPrimitive_TryMakeCone(
    const patch_revolve_frame_t &frame,
    f64 radiusBottom,
    f64 radiusTop,
    f64 height,
    const patch_primitive_common_t &common,
    geometry_source_id_t patchId,
    geometry_source_id_allocator_t *pIdAllocator,
    const allocator_t *pAllocator,
    patch_surface_t *pOut ) noexcept
{
    if ( !PositiveFinite( height ) || !std::isfinite( radiusBottom ) || !std::isfinite( radiusTop ) ||
         radiusBottom < 0.0 || radiusTop < 0.0 || ( radiusBottom == 0.0 && radiusTop == 0.0 ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    // A straight side needs only one quadratic span with its middle control
    // on the midpoint: the profile is then exactly linear.
    const math::vec2d_t profile[3] = { { radiusBottom, 0.0 },
                                       { 0.5 * ( radiusBottom + radiusTop ), 0.5 * height },
                                       { radiusTop, height } };
    return PatchPrimitive_TryRevolve( span_t<const math::vec2d_t>{ profile, 3u }, frame, common, patchId,
                                      pIdAllocator, pAllocator, pOut );
}

geometry_status_t PatchPrimitive_TryMakeSphere(
    const patch_revolve_frame_t &frame,
    f64 radius,
    u32 cMeridianSegments,
    const patch_primitive_common_t &common,
    geometry_source_id_t patchId,
    geometry_source_id_allocator_t *pIdAllocator,
    const allocator_t *pAllocator,
    patch_surface_t *pOut ) noexcept
{
    // One arc cannot span 180 degrees (its control would be at infinity).
    if ( !PositiveFinite( radius ) || cMeridianSegments < 2u || cMeridianSegments > kPatchPrimitiveSegmentsMax ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const u32 m = cMeridianSegments;
    const f64 half = 0.5 * kPi / static_cast<f64>( m );
    const f64 ctrl = radius / std::cos( half );
    math::vec2d_t profile[2u * kPatchPrimitiveSegmentsMax + 1u];
    for ( u32 k = 0u; k <= m; ++k ) {
        const f64 phi = -0.5 * kPi + 2.0 * half * static_cast<f64>( k );
        profile[2u * k] = math::vec2d_t{ radius * std::cos( phi ), radius * std::sin( phi ) };
        if ( k < m ) {
            profile[2u * k + 1u] = math::vec2d_t{ ctrl * std::cos( phi + half ), ctrl * std::sin( phi + half ) };
        }
    }
    // Poles exactly on the axis: cos(+-pi/2) is ~6e-17, not 0, and a tiny
    // ring would read as a real (degenerate) circle rather than a point.
    profile[0] = math::vec2d_t{ 0.0, -radius };
    profile[2u * m] = math::vec2d_t{ 0.0, radius };
    return PatchPrimitive_TryRevolve( span_t<const math::vec2d_t>{ profile, 2u * m + 1u }, frame, common, patchId,
                                      pIdAllocator, pAllocator, pOut );
}

geometry_status_t PatchPrimitive_TryMakeTorus(
    const patch_revolve_frame_t &frame,
    f64 ringRadius,
    f64 tubeRadius,
    u32 cTubeSegments,
    const patch_primitive_common_t &common,
    geometry_source_id_t patchId,
    geometry_source_id_allocator_t *pIdAllocator,
    const allocator_t *pAllocator,
    patch_surface_t *pOut ) noexcept
{
    if ( !PositiveFinite( ringRadius ) || !PositiveFinite( tubeRadius ) || cTubeSegments < kPatchPrimitiveSegmentsMin ||
         cTubeSegments > kPatchPrimitiveSegmentsMax ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    // The tube's control polygon reaches tubeRadius / cos(pi/n) from its
    // centre; keeping that inside the ring radius keeps every profile
    // control at r >= 0 (no self-intersection through the axis).
    const f64 reach = tubeRadius / std::cos( kPi / static_cast<f64>( cTubeSegments ) );
    if ( !( reach < ringRadius ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    math::vec2d_t unit[2u * kPatchPrimitiveSegmentsMax + 1u];
    UnitCircleControls( cTubeSegments, unit );
    math::vec2d_t profile[2u * kPatchPrimitiveSegmentsMax + 1u];
    const u32 cProfile = 2u * cTubeSegments + 1u;
    for ( u32 k = 0u; k < cProfile; ++k ) {
        profile[k] = math::vec2d_t{ ringRadius + tubeRadius * unit[k].x, tubeRadius * unit[k].y };
    }
    return PatchPrimitive_TryRevolve( span_t<const math::vec2d_t>{ profile, cProfile }, frame, common, patchId,
                                      pIdAllocator, pAllocator, pOut );
}

geometry_status_t PatchPrimitive_TryMakeDisc(
    const patch_revolve_frame_t &frame,
    f64 radius,
    const patch_primitive_common_t &common,
    geometry_source_id_t patchId,
    geometry_source_id_allocator_t *pIdAllocator,
    const allocator_t *pAllocator,
    patch_surface_t *pOut ) noexcept
{
    if ( !PositiveFinite( radius ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    // Rim to centre: with u counter-clockwise and v pointing inward, the
    // front face (dPdu x dPdv) is +axis.
    const math::vec2d_t profile[3] = { { radius, 0.0 }, { 0.5 * radius, 0.0 }, { 0.0, 0.0 } };
    return PatchPrimitive_TryRevolve( span_t<const math::vec2d_t>{ profile, 3u }, frame, common, patchId,
                                      pIdAllocator, pAllocator, pOut );
}

geometry_status_t PatchPrimitive_TryExtrude(
    span_t<const math::vec3d_t> profile,
    math::vec3d_t direction,
    u32 cSubPatchesV,
    bool bInward,
    u32 materialId,
    geometry_source_id_t patchId,
    geometry_source_id_allocator_t *pIdAllocator,
    const allocator_t *pAllocator,
    patch_surface_t *pOut ) noexcept
{
    if ( pOut == nullptr || ( profile.nCount > 0u && profile.pData == nullptr ) || !ValidProfileCount( profile.nCount ) ||
         cSubPatchesV == 0u || 2u * static_cast<u64>( cSubPatchesV ) + 1u > kPatchControlsPerAxisMax ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !math::Vec3d_IsFinite( direction ) ) { return geometry_status_t::NUMERIC_FAILURE; }
    if ( math::Vec3d_LengthSquared( direction ) == 0.0 ) { return geometry_status_t::DEGENERATE; }
    for ( usize k = 0u; k < profile.nCount; ++k ) {
        if ( !math::Vec3d_IsFinite( profile.pData[k] ) ) { return geometry_status_t::NUMERIC_FAILURE; }
    }
    const u32 cRows = 2u * cSubPatchesV + 1u;
    return FillPatch( pOut, pAllocator, static_cast<u32>( profile.nCount ), cRows, patchId, pIdAllocator, materialId,
                      bInward, [&]( u32 i, u32 j ) noexcept {
                          const f64 f = static_cast<f64>( j ) / static_cast<f64>( cRows - 1u );
                          return math::Vec3d_Add( profile.pData[i], math::Vec3d_Scale( direction, f ) );
                      } );
}

geometry_status_t PatchPrimitive_TryMakeBevel(
    math::vec3d_t corner,
    math::vec3d_t sideA,
    math::vec3d_t sideB,
    math::vec3d_t depth,
    bool bInward,
    geometry_source_id_t patchId,
    geometry_source_id_allocator_t *pIdAllocator,
    const allocator_t *pAllocator,
    patch_surface_t *pOut ) noexcept
{
    if ( !math::Vec3d_IsFinite( corner ) || !math::Vec3d_IsFinite( sideA ) || !math::Vec3d_IsFinite( sideB ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }
    // Parallel sides leave no corner to round.
    if ( math::Vec3d_LengthSquared( math::Vec3d_Cross( sideA, sideB ) ) == 0.0 ) {
        return geometry_status_t::DEGENERATE;
    }
    const math::vec3d_t profile[3] = { math::Vec3d_Add( corner, sideA ), corner, math::Vec3d_Add( corner, sideB ) };
    return PatchPrimitive_TryExtrude( span_t<const math::vec3d_t>{ profile, 3u }, depth, 1u, bInward, 0u, patchId,
                                      pIdAllocator, pAllocator, pOut );
}

} // namespace cypher::editor::geometry
