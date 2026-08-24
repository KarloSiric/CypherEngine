//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Snap.cpp
//  Purpose: Implements deterministic editor grid and angle snapping.
//  Details: Computation uses double intermediates to preserve stable grid indices
//           across large authoring coordinates before converting to float storage.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMath_Snap.h"

#include "CypherCommon_Assert.h"

#include <cmath>
#include <limits>

namespace cypher::math
{

namespace
{

bool_t SnapModeValid( snap_mode_t mode ) noexcept
{
    return mode == snap_mode_t::NEAREST || mode == snap_mode_t::FLOOR ||
           mode == snap_mode_t::CEIL;
}

f64 SnapRound( f64 value, snap_mode_t mode ) noexcept
{
    switch ( mode ) {
        case snap_mode_t::NEAREST:
            return std::round( value );
        case snap_mode_t::FLOOR:
            return std::floor( value );
        case snap_mode_t::CEIL:
            return std::ceil( value );
        default:
            return value;
    }
}

bool_t SnapTryGridComponent(
    f32 value,
    f32 step,
    f32 origin,
    snap_mode_t mode,
    i64 *pGrid ) noexcept
{
    if ( pGrid == nullptr || step <= 0.0f || !Scalar_IsFinite( value ) ||
         !Scalar_IsFinite( step ) || !Scalar_IsFinite( origin ) ||
         !SnapModeValid( mode ) ) {
        return false;
    }
    // Convert into integer grid space first. Keeping this calculation in f64
    // avoids unstable cell selection at large editor coordinates.
    const f64 coordinate = SnapRound(
        ( static_cast<f64>( value ) - origin ) / step,
        mode );
    if ( coordinate < static_cast<f64>( std::numeric_limits<i64>::min() ) ||
         coordinate > static_cast<f64>( std::numeric_limits<i64>::max() ) ) {
        return false;
    }
    *pGrid = static_cast<i64>( coordinate );
    return true;
}

} // namespace

bool_t Snap_TryScalar(
    f32 value,
    f32 step,
    f32 origin,
    snap_mode_t mode,
    f32 *pSnapped ) noexcept
{
    const bool_t bValidOutput = pSnapped != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Snap_TryScalar requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pSnapped = 0.0f;

    i64 grid = 0;
    if ( !SnapTryGridComponent( value, step, origin, mode, &grid ) ) {
        return false;
    }
    const f64 snapped = static_cast<f64>( origin ) +
        static_cast<f64>( grid ) * step;
    const f32 result = static_cast<f32>( snapped );
    if ( !Scalar_IsFinite( result ) ) {
        return false;
    }
    *pSnapped = result;
    return true;
}

bool_t Snap_TryVec3(
    vec3_t value,
    vec3_t step,
    vec3_t origin,
    snap_mode_t mode,
    vec3_t *pSnapped ) noexcept
{
    const bool_t bValidOutput = pSnapped != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Snap_TryVec3 requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pSnapped = CY_VEC3_ZERO;

    // Compute all components into a temporary so failure never publishes a
    // partially snapped vector.
    vec3_t result{};
    if ( !Snap_TryScalar( value.x, step.x, origin.x, mode, &result.x ) ||
         !Snap_TryScalar( value.y, step.y, origin.y, mode, &result.y ) ||
         !Snap_TryScalar( value.z, step.z, origin.z, mode, &result.z ) ) {
        return false;
    }
    *pSnapped = result;
    return true;
}

bool_t Snap_TryAngle(
    angle_t value,
    angle_t step,
    angle_t origin,
    snap_mode_t mode,
    bool_t bNormalizePositive,
    angle_t *pSnapped ) noexcept
{
    const bool_t bValidOutput = pSnapped != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Snap_TryAngle requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pSnapped = CY_ANGLE_ZERO;

    f32 radians = 0.0f;
    if ( !Snap_TryScalar(
             value.radians, step.radians, origin.radians, mode, &radians ) ) {
        return false;
    }
    const angle_t result = Angle_FromRadians( radians );
    *pSnapped = bNormalizePositive
        ? Angle_NormalizePositive( result )
        : Angle_NormalizeSigned( result );
    return true;
}

bool_t Snap_TryWorldToGrid(
    vec3_t value,
    vec3_t step,
    vec3_t origin,
    snap_mode_t mode,
    grid_coord3_t *pGrid ) noexcept
{
    const bool_t bValidOutput = pGrid != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Snap_TryWorldToGrid requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pGrid = {};

    grid_coord3_t result{};
    if ( !SnapTryGridComponent( value.x, step.x, origin.x, mode, &result.x ) ||
         !SnapTryGridComponent( value.y, step.y, origin.y, mode, &result.y ) ||
         !SnapTryGridComponent( value.z, step.z, origin.z, mode, &result.z ) ) {
        return false;
    }
    *pGrid = result;
    return true;
}

bool_t Snap_TryGridToWorld(
    grid_coord3_t grid,
    vec3_t step,
    vec3_t origin,
    vec3_t *pWorld ) noexcept
{
    const bool_t bValidOutput = pWorld != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Snap_TryGridToWorld requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pWorld = CY_VEC3_ZERO;
    if ( step.x <= 0.0f || step.y <= 0.0f || step.z <= 0.0f ||
         !Vec3_IsFinite( step ) || !Vec3_IsFinite( origin ) ) {
        return false;
    }
    
    const vec3_t result = Vec3_Make(
        static_cast<f32>( static_cast<f64>( origin.x ) +
            static_cast<f64>( grid.x ) * static_cast<f64>( step.x ) ),
        static_cast<f32>( static_cast<f64>( origin.y ) +
            static_cast<f64>( grid.y ) * static_cast<f64>( step.y ) ),
        static_cast<f32>( static_cast<f64>( origin.z ) +
            static_cast<f64>( grid.z ) * static_cast<f64>( step.z ) ) );
    if ( !Vec3_IsFinite( result ) ) {
        return false;
    }
    *pWorld = result;
    return true;
}

vec3_t Snap_ProjectPointToPlaneUnit( vec3_t point, plane_t unitPlane ) noexcept
{
    return Plane_ProjectPointUnit( unitPlane, point );
}

vec3_t Snap_DirectionToPrincipalAxis( vec3_t direction ) noexcept
{
    if ( !Vec3_IsFinite( direction ) || Vec3_EqualsExact( direction, CY_VEC3_ZERO ) ) {
        return CY_VEC3_ZERO;
    }
    // Select the signed basis vector belonging to the largest magnitude
    // component; ties resolve X, then Y, then Z for deterministic tools.
    const vec3_t absolute = Vec3_Abs( direction );
    if ( absolute.x >= absolute.y && absolute.x >= absolute.z ) {
        return direction.x >= 0.0f ? CY_VEC3_FORWARD : CY_VEC3_BACKWARD;
    }
    if ( absolute.y >= absolute.z ) {
        return direction.y >= 0.0f ? CY_VEC3_LEFT : CY_VEC3_RIGHT;
    }
    return direction.z >= 0.0f ? CY_VEC3_UP : CY_VEC3_DOWN;
}

} // namespace cypher::math
