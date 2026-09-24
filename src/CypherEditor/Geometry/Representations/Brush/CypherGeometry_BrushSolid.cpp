//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushSolid.cpp
//  Purpose: Implements plane-defined convex solid side management.
//  Details: All mutators validate before committing, so a failed add or
//           set never leaves the brush in a half-modified state. Growth
//           is bounded by cBrushSidesPerBrushMax and checked before
//           allocating, consistent with the attribute store pattern.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushSolid.h"

namespace cypher::editor::geometry
{

namespace
{

using cypher::common::bool_t;

bool_t IsInitialized( const brush_solid_t *pBrush ) noexcept
{
    // Same convention as the attribute store: the allocator binding is
    // what Vector_Init establishes, so its presence is the honest test.
    return pBrush != nullptr && pBrush->sides.pAllocator != nullptr;
}

} // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

geometry_status_t BrushSolid_Init(
    brush_solid_t *pBrush,
    const common::allocator_t *pAllocator,
    geometry_source_id_t brushId ) noexcept
{
    if ( pBrush == nullptr || pAllocator == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !GeometrySourceId_IsValid( brushId ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pBrush->sides.pAllocator != nullptr ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }
    if ( !common::Vector_Init( &pBrush->sides, pAllocator, 0u ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    pBrush->sourceId = brushId;
    return geometry_status_t::OK;
}

void BrushSolid_Shutdown( brush_solid_t *pBrush ) noexcept
{
    if ( pBrush == nullptr || pBrush->sides.pAllocator == nullptr ) {
        return;
    }
    common::Vector_Shutdown( &pBrush->sides );
    pBrush->sourceId = GEOMETRY_SOURCE_ID_INVALID;
}

// ---------------------------------------------------------------------------
// Side queries
// ---------------------------------------------------------------------------

common::usize BrushSolid_SideCount(
    const brush_solid_t *pBrush ) noexcept
{
    if ( !IsInitialized( pBrush ) ) {
        return 0u;
    }
    return common::Vector_Count( &pBrush->sides );
}

geometry_status_t BrushSolid_TryGetSide(
    const brush_solid_t *pBrush,
    common::usize iIndex,
    brush_solid_side_t *pSideOut ) noexcept
{
    if ( pSideOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pSideOut = {};

    if ( !IsInitialized( pBrush ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( iIndex >= common::Vector_Count( &pBrush->sides ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pSideOut = pBrush->sides.pData[iIndex];
    return geometry_status_t::OK;
}

// ---------------------------------------------------------------------------
// Side mutation
// ---------------------------------------------------------------------------

geometry_status_t BrushSolid_TryAddSide(
    brush_solid_t *pBrush,
    const geometry_limit_policy_t &limits,
    const brush_solid_side_t &side,
    common::usize *pIndexOut ) noexcept
{
    if ( !IsInitialized( pBrush ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !GeometrySourceId_IsValid( side.sourceId ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    // Plane normal must be finite so downstream reconstruction does not
    // silently produce NaN vertices. Full normalization is checked later
    // during validation, but non-finite input is rejected here because
    // no amount of later checking can recover from it.
    if ( !cypher::math::Vec3d_IsFinite( side.plane.normal ) ||
         !cypher::math::Scalar_IsFinite( side.plane.d ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }

    const common::usize nCount = common::Vector_Count( &pBrush->sides );
    if ( static_cast<common::u64>( nCount ) >= limits.cBrushSidesPerBrushMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    // Side identity must be unique within the brush, and distinct from the
    // brush's own identity: selection, provenance, and undo all resolve a
    // side by source ID, so a duplicate would make two sides aliases.
    // Linear in the side count, which is bounded by the check above.
    if ( side.sourceId.value == pBrush->sourceId.value ) {
        return geometry_status_t::IDENTITY_CONFLICT;
    }
    for ( common::usize i = 0u; i < nCount; ++i ) {
        if ( pBrush->sides.pData[i].sourceId.value == side.sourceId.value ) {
            return geometry_status_t::IDENTITY_CONFLICT;
        }
    }
    if ( !common::Vector_PushBack( &pBrush->sides, side ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    if ( pIndexOut != nullptr ) {
        *pIndexOut = nCount;
    }
    return geometry_status_t::OK;
}

geometry_status_t BrushSolid_TrySetSidePlane(
    brush_solid_t *pBrush,
    common::usize iIndex,
    cypher::math::planed_t plane ) noexcept
{
    if ( !IsInitialized( pBrush ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( iIndex >= common::Vector_Count( &pBrush->sides ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !cypher::math::Vec3d_IsFinite( plane.normal ) ||
         !cypher::math::Scalar_IsFinite( plane.d ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }

    // Only the plane changes. Identity and attribute binding are the
    // properties a drag edit must not disturb.
    pBrush->sides.pData[iIndex].plane = plane;
    return geometry_status_t::OK;
}

geometry_status_t BrushSolid_TryReserve(
    brush_solid_t *pBrush,
    const geometry_limit_policy_t &limits,
    common::usize nCapacity ) noexcept
{
    if ( !IsInitialized( pBrush ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( static_cast<common::u64>( nCapacity ) > limits.cBrushSidesPerBrushMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    if ( !common::Vector_Reserve( &pBrush->sides, nCapacity ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    return geometry_status_t::OK;
}

void BrushSolid_Clear( brush_solid_t *pBrush ) noexcept
{
    if ( !IsInitialized( pBrush ) ) {
        return;
    }
    common::Vector_Clear( &pBrush->sides );
}

} // namespace cypher::editor::geometry
