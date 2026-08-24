//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Gizmo.cpp
//  Purpose: Implements transform-gizmo picking geometry for editor viewports.
//  Details: Queries normalize their geometric axes, reject invalid handles, and
//           constrain ray hits to the forward half-line.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMath_Gizmo.h"

#include "CypherCommon_Assert.h"

#include <algorithm>
#include <cmath>

namespace cypher::math
{

namespace
{

bool_t TryNormalizedRay(
    ray_t ray,
    f32 minimumDirectionLength,
    ray_t *pNormalized ) noexcept
{
    return Ray_TryNormalizeDirection(
        ray, minimumDirectionLength, pNormalized, nullptr );
}

} // namespace

bool_t Gizmo_TryHitAxis(
    ray_t ray,
    vec3_t axisOrigin,
    vec3_t axisDirection,
    f32 minimumDirectionLength,
    f32 relativeParallelTolerance,
    f32 pickRadius,
    gizmo_axis_hit_t *pHit ) noexcept
{
    const bool_t bValidOutput = pHit != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Gizmo_TryHitAxis requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pHit = {};
    if ( !Vec3_IsFinite( axisOrigin ) || !Vec3_IsFinite( axisDirection ) ||
         minimumDirectionLength < 0.0f || relativeParallelTolerance < 0.0f ||
         pickRadius < 0.0f || !Scalar_IsFinite( minimumDirectionLength ) ||
         !Scalar_IsFinite( relativeParallelTolerance ) ||
         !Scalar_IsFinite( pickRadius ) ) {
        return false;
    }

    ray_t unitRay{};
    vec3_t unitAxis{};
    if ( !TryNormalizedRay( ray, minimumDirectionLength, &unitRay ) ||
         !Vec3_TryNormalize(
             axisDirection, minimumDirectionLength, &unitAxis, nullptr ) ) {
        return false;
    }

    const vec3_t relative = Vec3_Subtract( unitRay.origin, axisOrigin );
    const f64 directionDot = Vec3_Dot( unitRay.direction, unitAxis );
    const f64 rayProjection = Vec3_Dot( unitRay.direction, relative );
    const f64 axisProjection = Vec3_Dot( unitAxis, relative );
    const f64 denominator = 1.0 - directionDot * directionDot;
    f64 rayDistance = 0.0;
    f64 axisDistance = axisProjection;

    // Solve the closest points on two infinite lines. Near-parallel lines use
    // the ray origin as the stable fallback; picking still tests separation.
    if ( denominator > relativeParallelTolerance ) {
        rayDistance =
            ( directionDot * axisProjection - rayProjection ) / denominator;
        axisDistance =
            ( axisProjection - directionDot * rayProjection ) / denominator;
        if ( rayDistance < 0.0 ) {
            rayDistance = 0.0;
            axisDistance = axisProjection;
        }
    }

    const vec3_t pointOnRay = Ray_PointAt(
        unitRay, static_cast<f32>( rayDistance ) );
    const vec3_t pointOnAxis = Vec3_MulAdd(
        axisOrigin, unitAxis, static_cast<f32>( axisDistance ) );
    const f32 separation = Vec3_Distance( pointOnRay, pointOnAxis );
    if ( !Scalar_IsFinite( separation ) || separation > pickRadius ) {
        return false;
    }
    *pHit = {
        pointOnRay,
        pointOnAxis,
        static_cast<f32>( rayDistance ),
        static_cast<f32>( axisDistance ),
        separation
    };
    return true;
}

bool_t Gizmo_TryHitPlane(
    ray_t ray,
    vec3_t planeOrigin,
    vec3_t uAxis,
    vec3_t vAxis,
    vec2_t halfExtent,
    f32 minimumDirectionLength,
    f32 minimumAbsDenominator,
    gizmo_plane_hit_t *pHit ) noexcept
{
    const bool_t bValidOutput = pHit != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Gizmo_TryHitPlane requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pHit = {};
    if ( !Vec3_IsFinite( planeOrigin ) || !Vec3_IsFinite( uAxis ) ||
         !Vec3_IsFinite( vAxis ) || !Vec2_IsFinite( halfExtent ) ||
         halfExtent.x < 0.0f || halfExtent.y < 0.0f ||
         minimumDirectionLength < 0.0f || minimumAbsDenominator < 0.0f ||
         !Scalar_IsFinite( minimumDirectionLength ) ||
         !Scalar_IsFinite( minimumAbsDenominator ) ) {
        return false;
    }

    ray_t unitRay{};
    vec3_t unitU{};
    vec3_t unitV{};
    vec3_t normal{};
    if ( !TryNormalizedRay( ray, minimumDirectionLength, &unitRay ) ||
         !Vec3_TryNormalize( uAxis, minimumDirectionLength, &unitU, nullptr ) ||
         !Vec3_TryNormalize( vAxis, minimumDirectionLength, &unitV, nullptr ) ||
         !Vec3_TryNormalize(
             Vec3_Cross( unitU, unitV ), minimumDirectionLength,
             &normal, nullptr ) ) {
        return false;
    }

    // Rebuild V from the normalized plane normal so skewed caller axes become
    // an orthonormal coordinate frame for the rectangular handle.
    unitV = Vec3_Cross( normal, unitU );

    plane_t plane{};
    f32 rayDistance = 0.0f;
    vec3_t point{};
    if ( !Plane_TryFromPointNormal(
             planeOrigin, normal, minimumDirectionLength, &plane ) ||
         !Intersection_RayPlane(
             unitRay, plane, minimumAbsDenominator, 0.0f,
             common::CY_F32_MAX, &rayDistance, &point ) ) {
        return false;
    }
    const vec3_t relative = Vec3_Subtract( point, planeOrigin );
    const vec2_t coordinates = Vec2_Make(
        Vec3_Dot( relative, unitU ), Vec3_Dot( relative, unitV ) );
    if ( std::abs( coordinates.x ) > halfExtent.x ||
         std::abs( coordinates.y ) > halfExtent.y ) {
        return false;
    }
    *pHit = { point, coordinates, rayDistance };
    return true;
}

bool_t Gizmo_TryHitRing(
    ray_t ray,
    vec3_t center,
    vec3_t normal,
    f32 radius,
    f32 halfThickness,
    f32 minimumDirectionLength,
    f32 minimumAbsDenominator,
    gizmo_ring_hit_t *pHit ) noexcept
{
    const bool_t bValidOutput = pHit != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Gizmo_TryHitRing requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pHit = {};
    if ( !Vec3_IsFinite( center ) || !Vec3_IsFinite( normal ) ||
         radius < 0.0f || halfThickness < 0.0f ||
         minimumDirectionLength < 0.0f || minimumAbsDenominator < 0.0f ||
         !Scalar_IsFinite( radius ) || !Scalar_IsFinite( halfThickness ) ||
         !Scalar_IsFinite( minimumDirectionLength ) ||
         !Scalar_IsFinite( minimumAbsDenominator ) ) {
        return false;
    }

    ray_t unitRay{};
    vec3_t unitNormal{};
    plane_t plane{};
    f32 rayDistance = 0.0f;
    vec3_t point{};
    if ( !TryNormalizedRay( ray, minimumDirectionLength, &unitRay ) ||
         !Vec3_TryNormalize(
             normal, minimumDirectionLength, &unitNormal, nullptr ) ||
         !Plane_TryFromPointNormal(
             center, unitNormal, minimumDirectionLength, &plane ) ||
         !Intersection_RayPlane(
             unitRay, plane, minimumAbsDenominator, 0.0f,
             common::CY_F32_MAX, &rayDistance, &point ) ) {
        return false;
    }
    
    // Intersect the ring plane first, then accept only points in the annulus
    // [radius - halfThickness, radius + halfThickness].
    const vec3_t radial = Vec3_Subtract( point, center );
    const f32 radialDistance = Vec3_Length( radial );
    if ( !Scalar_IsFinite( radialDistance ) ||
         std::abs( radialDistance - radius ) > halfThickness ) {
        return false;
    }
    vec3_t radialDirection = CY_VEC3_ZERO;
    if ( radialDistance > minimumDirectionLength ) {
        radialDirection = Vec3_Scale( radial, 1.0f / radialDistance );
    }
    *pHit = { point, radialDirection, rayDistance, radialDistance };
    return true;
}

} // namespace cypher::math
