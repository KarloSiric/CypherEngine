//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushGenerator.cpp
//  Purpose: Implements pure primitive generators for brush solids.
//  Details: The box generator is the simplest non-trivial brush: six
//           axis-aligned planes whose intersection is a rectangular
//           parallelepiped. It exists primarily to close Gate 2's
//           acceptance criterion (8 vertices, 12 edges, 6 faces) and to
//           serve as the default creation command in any host editor.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushGenerator.h"

namespace cypher::editor::geometry
{

namespace
{

using math::f64;
using math::vec3d_t;
// using math::planed_t;

// The six outward normals of an axis-aligned box, in the deterministic
// order documented in the header: +X, -X, +Y, -Y, +Z, -Z.
constexpr vec3d_t cBoxNormals[6] = {
    {  1.0,  0.0,  0.0 },
    { -1.0,  0.0,  0.0 },
    {  0.0,  1.0,  0.0 },
    {  0.0, -1.0,  0.0 },
    {  0.0,  0.0,  1.0 },
    {  0.0,  0.0, -1.0 }
};

// Computes the plane distance `d` for one face of an axis-aligned box.
// Convention: dot(normal, point) + d = 0, interior is nonpositive.
// For a +X face at center.x + halfExtent: d = -(center.x + halfExtent).
// For a -X face at center.x - halfExtent: d = center.x - halfExtent.
f64 BoxPlaneDistance(
    vec3d_t center, vec3d_t halfExtents, common::usize iFace ) noexcept
{
    // Each pair of faces is one axis. The positive face (even index) has
    // d = -(center_component + halfExtent_component). The negative face
    // (odd index) has d = center_component - halfExtent_component.
    const common::usize iAxis = iFace / 2u;
    const f64 c = ( iAxis == 0u ) ? center.x
               : ( iAxis == 1u ) ? center.y
               :                   center.z;
    const f64 h = ( iAxis == 0u ) ? halfExtents.x
               : ( iAxis == 1u ) ? halfExtents.y
               :                   halfExtents.z;

    const bool bPositiveFace = ( iFace % 2u == 0u );
    return bPositiveFace ? -( c + h ) : ( c - h );
}

} // namespace

geometry_status_t BrushGenerator_TryMakeBox(
    brush_solid_t *pBrush,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    vec3d_t center,
    vec3d_t halfExtents ) noexcept
{
    if ( pBrush == nullptr || pAllocator == nullptr ||
         pIdAllocator == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    // ---- Input validation -----------------------------------------------

    if ( !math::Vec3d_IsFinite( center ) ||
         !math::Vec3d_IsFinite( halfExtents ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }

    // Half-extents must be strictly positive — a zero extent produces a
    // degenerate face with no area, and a negative one inverts the solid.
    if ( halfExtents.x <= 0.0 || halfExtents.y <= 0.0 ||
         halfExtents.z <= 0.0 ) {
        return geometry_status_t::DEGENERATE;
    }

    // The farthest corner from the origin determines whether the box
    // fits within the document's coordinate limit.
    const f64 limit = policy.numerical.fCoordinateMagnitudeLimit;
    if ( math::Scalar_Abs( center.x ) + halfExtents.x > limit ||
         math::Scalar_Abs( center.y ) + halfExtents.y > limit ||
         math::Scalar_Abs( center.z ) + halfExtents.z > limit ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    // ---- Allocate source IDs before touching the brush ------------------
    // If the allocator is near exhaustion, this fails cleanly before any
    // state has been modified.

    constexpr common::usize cIdCount = 7u; // 1 brush + 6 sides
    geometry_source_id_t ids[cIdCount];

    for ( common::usize i = 0u; i < cIdCount; ++i ) {
        const geometry_source_id_result_t result =
            GeometrySourceIdAllocator_Allocate( pIdAllocator );
        if ( result.status != geometry_status_t::OK ) {
            return result.status;
        }
        ids[i] = result.id;
    }

    // ---- Build the brush ------------------------------------------------

    geometry_status_t status = BrushSolid_Init( pBrush, pAllocator, ids[0] );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    // Pre-allocate all 6 side slots so the loop below never fails on
    // allocation and we don't have to unwind a half-built brush.
    status = BrushSolid_TryReserve( pBrush, policy.limits, 6u );
    if ( status != geometry_status_t::OK ) {
        BrushSolid_Shutdown( pBrush );
        return status;
    }

    for ( common::usize i = 0u; i < 6u; ++i ) {
        brush_solid_side_t side{};
        side.plane = math::Planed_Make(
            cBoxNormals[i],
            BoxPlaneDistance( center, halfExtents, i ) );
        side.sourceId = ids[1u + i];
        side.iAttributeIndex = static_cast<common::u32>( i );

        // Cannot fail: capacity was reserved above and the plane is
        // finite by construction (center and halfExtents are validated).
        ( void )BrushSolid_TryAddSide( pBrush, policy.limits, side, nullptr );
    }

    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
