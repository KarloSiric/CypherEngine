//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Ray.cpp
//  Purpose: Implements checked ray and segment calculations.
//  Details: Degenerate directions produce explicit failure or stable endpoint
//           results rather than divisions by very small squared lengths.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMath_Ray.h"
#include "CypherMath_Scalar.h"
#include "CypherCommon_Assert.h"

namespace cypher::math
{

bool_t Ray_IsFinite( ray_t ray ) noexcept
{
    return Vec3_IsFinite( ray.origin ) && Vec3_IsFinite( ray.direction );
}

bool_t Segment_IsFinite( segment_t segment ) noexcept
{
    return Vec3_IsFinite( segment.start ) && Vec3_IsFinite( segment.end );
}

f32 Segment_Length( segment_t segment ) noexcept
{
    return Vec3_Distance( segment.start, segment.end );
}

bool_t Ray_TryNormalizeDirection(
    ray_t ray,
    f32 minimumDirectionLength,
    ray_t *pNormalized,
    f32 *pOriginalDirectionLength ) noexcept
{
    const bool_t bValidOutput = pNormalized != nullptr;
    CY_ASSERT_MSG(
        bValidOutput,
        "Ray_TryNormalizeDirection requires output storage." );
    if ( pOriginalDirectionLength != nullptr ) {
        *pOriginalDirectionLength = 0.0f;
    }
    if ( !bValidOutput ) {
        return false;
    }
    *pNormalized = Ray_Make( ray.origin, CY_VEC3_ZERO );

    vec3_t direction{};
    if ( !Vec3_IsFinite( ray.origin ) ||
         !Vec3_TryNormalize(
             ray.direction, minimumDirectionLength,
             &direction, pOriginalDirectionLength ) ) {
        return false;
    }
    *pNormalized = Ray_Make( ray.origin, direction );
    return true;
}

bool_t Ray_TryClosestParameterToPoint(
    ray_t ray,
    vec3_t point,
    f32 minimumDirectionLength,
    f32 *pParameter ) noexcept
{
    const bool_t bValidOutput = pParameter != nullptr;
    const bool_t bValidThreshold = Scalar_IsFinite( minimumDirectionLength ) &&
                                   minimumDirectionLength >= 0.0f;
    CY_ASSERT_MSG(
        bValidOutput,
        "Ray_TryClosestParameterToPoint requires output storage." );
    CY_ASSERT_MSG(
        bValidThreshold,
        "Ray_TryClosestParameterToPoint requires a finite nonnegative threshold." );
    if ( !bValidOutput ) {
        return false;
    }
    *pParameter = 0.0f;
    if ( !bValidThreshold || !Ray_IsFinite( ray ) || !Vec3_IsFinite( point ) ) {
        return false;
    }

    vec3_t unitDirection{};
    f32 directionLength = 0.0f;
    if ( !Vec3_TryNormalize(
             ray.direction, minimumDirectionLength,
             &unitDirection, &directionLength ) ) {
        return false;
    }
    const f32 lineParameter = Vec3_Dot(
        Vec3_Subtract( point, ray.origin ), unitDirection ) /
        directionLength;
    *pParameter = Scalar_Max( lineParameter, 0.0f );
    return Scalar_IsFinite( *pParameter );
}

vec3_t Segment_ClosestPoint( segment_t segment, vec3_t point ) noexcept
{
    const vec3_t direction = Segment_Direction( segment );
    const f32 denominator = Vec3_LengthSquared( direction );
    if ( !Scalar_IsFinite( denominator ) || denominator <= 0.0f ) {
        return segment.start;
    }
    const f32 t = Scalar_Saturate(
        Vec3_Dot( Vec3_Subtract( point, segment.start ), direction ) /
        denominator );
    return Segment_PointAt( segment, t );
}

f32 Segment_DistanceSquaredToPoint(
    segment_t segment,
    vec3_t point ) noexcept
{
    return Vec3_DistanceSquared( Segment_ClosestPoint( segment, point ), point );
}

} // namespace cypher::math
