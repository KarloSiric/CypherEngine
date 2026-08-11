//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Intersection.cpp
//  Purpose: Implements ray intersections and frustum classifications.
//  Details: Slab, quadratic, and Moller-Trumbore queries retain the caller's ray
//           parameterization and clip every result to an explicit interval.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMath_Intersection.h"
#include "CypherMath_Scalar.h"
#include "CypherCommon_Assert.h"

namespace cypher::math
{

namespace
{

bool_t IsValidParameterRange( f32 tMinimum, f32 tMaximum ) noexcept
{
    return !Scalar_IsNan( tMinimum ) && !Scalar_IsNan( tMaximum ) &&
           tMinimum <= tMaximum;
}

bool_t IsValidCullMode( triangle_cull_mode_t mode ) noexcept
{
    return mode == triangle_cull_mode_t::NONE ||
           mode == triangle_cull_mode_t::BACK_FACE ||
           mode == triangle_cull_mode_t::FRONT_FACE;
}

} // namespace

bool_t Intersection_RayPlane(
    ray_t ray,
    plane_t plane,
    f32 minimumAbsDenominator,
    f32 tMinimum,
    f32 tMaximum,
    f32 *pParameter,
    vec3_t *pPoint ) noexcept
{
    const bool_t bValidOutput = pParameter != nullptr;
    const bool_t bValidTolerance = Scalar_IsFinite( minimumAbsDenominator ) &&
                                   minimumAbsDenominator >= 0.0f;
    CY_ASSERT_MSG( bValidOutput, "Intersection_RayPlane requires output storage." );
    CY_ASSERT_MSG(
        bValidTolerance,
        "Intersection_RayPlane requires a finite nonnegative threshold." );
    if ( pPoint != nullptr ) {
        *pPoint = CY_VEC3_ZERO;
    }
    if ( !bValidOutput ) {
        return false;
    }
    *pParameter = 0.0f;
    if ( !bValidTolerance || !IsValidParameterRange( tMinimum, tMaximum ) ||
         !Ray_IsFinite( ray ) || !Plane_IsFinite( plane ) ) {
        return false;
    }

    const f32 denominator = Vec3_Dot( plane.normal, ray.direction );
    if ( Scalar_Abs( denominator ) <= minimumAbsDenominator ) {
        return false;
    }
    const f32 t = -Plane_SignedDistance( plane, ray.origin ) / denominator;
    if ( !Scalar_IsFinite( t ) || t < tMinimum || t > tMaximum ) {
        return false;
    }
    *pParameter = t;
    if ( pPoint != nullptr ) {
        *pPoint = Ray_PointAt( ray, t );
    }
    return true;
}

bool_t Intersection_RaySphere(
    ray_t ray,
    sphere_t sphere,
    f32 minimumDirectionLength,
    f32 tMinimum,
    f32 tMaximum,
    ray_interval_t *pInterval ) noexcept
{
    const bool_t bValidOutput = pInterval != nullptr;
    const bool_t bValidThreshold = Scalar_IsFinite( minimumDirectionLength ) &&
                                   minimumDirectionLength >= 0.0f;
    CY_ASSERT_MSG( bValidOutput, "Intersection_RaySphere requires output storage." );
    CY_ASSERT_MSG(
        bValidThreshold,
        "Intersection_RaySphere requires a finite nonnegative threshold." );
    if ( !bValidOutput ) {
        return false;
    }
    *pInterval = {};
    if ( !bValidThreshold || !IsValidParameterRange( tMinimum, tMaximum ) ||
         !Ray_IsFinite( ray ) || !Sphere_IsValid( sphere ) ) {
        return false;
    }

    const f32 directionLength = Vec3_Length( ray.direction );
    if ( !Scalar_IsFinite( directionLength ) ||
         directionLength <= minimumDirectionLength ) {
        return false;
    }
    const vec3_t offset = Vec3_Subtract( ray.origin, sphere.center );
    const f32 a = directionLength * directionLength;
    const f32 halfB = Vec3_Dot( offset, ray.direction );
    const f32 c = Vec3_Dot( offset, offset ) - sphere.radius * sphere.radius;
    const f32 discriminant = halfB * halfB - a * c;
    if ( discriminant < 0.0f || !Scalar_IsFinite( discriminant ) ) {
        return false;
    }

    const f32 root = Scalar_Sqrt( discriminant );
    const f32 inverseA = 1.0f / a;
    f32 enter = ( -halfB - root ) * inverseA;
    f32 exit = ( -halfB + root ) * inverseA;
    enter = Scalar_Max( enter, tMinimum );
    exit = Scalar_Min( exit, tMaximum );
    if ( enter > exit ) {
        return false;
    }
    *pInterval = { enter, exit };
    return true;
}

bool_t Intersection_RayAabb(
    ray_t ray,
    aabb_t bounds,
    f32 minimumAbsDirection,
    f32 tMinimum,
    f32 tMaximum,
    ray_interval_t *pInterval ) noexcept
{
    const bool_t bValidOutput = pInterval != nullptr;
    const bool_t bValidThreshold = Scalar_IsFinite( minimumAbsDirection ) &&
                                   minimumAbsDirection >= 0.0f;
    CY_ASSERT_MSG( bValidOutput, "Intersection_RayAabb requires output storage." );
    CY_ASSERT_MSG(
        bValidThreshold,
        "Intersection_RayAabb requires a finite nonnegative threshold." );
    if ( !bValidOutput ) {
        return false;
    }
    *pInterval = {};
    if ( !bValidThreshold || !IsValidParameterRange( tMinimum, tMaximum ) ||
         !Ray_IsFinite( ray ) || !Aabb_IsValid( bounds ) ) {
        return false;
    }

    const f32 origin[3]{ ray.origin.x, ray.origin.y, ray.origin.z };
    const f32 direction[3]{ ray.direction.x, ray.direction.y, ray.direction.z };
    const f32 minimum[3]{ bounds.minimum.x, bounds.minimum.y, bounds.minimum.z };
    const f32 maximum[3]{ bounds.maximum.x, bounds.maximum.y, bounds.maximum.z };
    f32 enter = tMinimum;
    f32 exit = tMaximum;
    for ( u32 axis = 0u; axis < 3u; ++axis ) {
        if ( Scalar_Abs( direction[axis] ) <= minimumAbsDirection ) {
            if ( origin[axis] < minimum[axis] || origin[axis] > maximum[axis] ) {
                return false;
            }
            continue;
        }

        const f32 inverseDirection = 1.0f / direction[axis];
        f32 axisEnter = ( minimum[axis] - origin[axis] ) * inverseDirection;
        f32 axisExit = ( maximum[axis] - origin[axis] ) * inverseDirection;
        if ( axisEnter > axisExit ) {
            const f32 temporary = axisEnter;
            axisEnter = axisExit;
            axisExit = temporary;
        }
        enter = Scalar_Max( enter, axisEnter );
        exit = Scalar_Min( exit, axisExit );
        if ( enter > exit ) {
            return false;
        }
    }
    *pInterval = { enter, exit };
    return true;
}

bool_t Intersection_RayTriangle(
    ray_t ray,
    triangle3_t triangle,
    triangle_cull_mode_t cullMode,
    f32 minimumAbsDeterminant,
    f32 barycentricTolerance,
    f32 tMinimum,
    f32 tMaximum,
    ray_triangle_hit_t *pHit ) noexcept
{
    const bool_t bValidOutput = pHit != nullptr;
    const bool_t bValidTolerances =
        Scalar_IsFinite( minimumAbsDeterminant ) &&
        minimumAbsDeterminant >= 0.0f &&
        Scalar_IsFinite( barycentricTolerance ) &&
        barycentricTolerance >= 0.0f;
    CY_ASSERT_MSG(
        bValidOutput,
        "Intersection_RayTriangle requires output storage." );
    CY_ASSERT_MSG(
        bValidTolerances,
        "Intersection_RayTriangle requires finite nonnegative tolerances." );
    if ( !bValidOutput ) {
        return false;
    }
    *pHit = {};
    if ( !bValidTolerances || !IsValidParameterRange( tMinimum, tMaximum ) ||
         !IsValidCullMode( cullMode ) || !Ray_IsFinite( ray ) ||
         !Triangle3_IsFinite( triangle ) ) {
        return false;
    }

    const vec3_t edgeAB = Vec3_Subtract( triangle.b, triangle.a );
    const vec3_t edgeAC = Vec3_Subtract( triangle.c, triangle.a );
    const vec3_t p = Vec3_Cross( ray.direction, edgeAC );
    const f32 determinant = Vec3_Dot( edgeAB, p );
    if ( cullMode == triangle_cull_mode_t::NONE ) {
        if ( Scalar_Abs( determinant ) <= minimumAbsDeterminant ) {
            return false;
        }
    } else if ( cullMode == triangle_cull_mode_t::BACK_FACE ) {
        if ( determinant <= minimumAbsDeterminant ) {
            return false;
        }
    } else if ( determinant >= -minimumAbsDeterminant ) {
        return false;
    }

    const f32 inverseDeterminant = 1.0f / determinant;
    const vec3_t fromA = Vec3_Subtract( ray.origin, triangle.a );
    const f32 weightB = Vec3_Dot( fromA, p ) * inverseDeterminant;
    if ( weightB < -barycentricTolerance ||
         weightB > 1.0f + barycentricTolerance ) {
        return false;
    }
    const vec3_t q = Vec3_Cross( fromA, edgeAB );
    const f32 weightC = Vec3_Dot( ray.direction, q ) * inverseDeterminant;
    if ( weightC < -barycentricTolerance ||
         weightB + weightC > 1.0f + barycentricTolerance ) {
        return false;
    }
    const f32 t = Vec3_Dot( edgeAC, q ) * inverseDeterminant;
    if ( !Scalar_IsFinite( t ) || t < tMinimum || t > tMaximum ) {
        return false;
    }
    *pHit = { t, weightB, weightC };
    return true;
}

bool_t Intersection_SegmentTriangle(
    segment_t segment,
    triangle3_t triangle,
    triangle_cull_mode_t cullMode,
    f32 minimumAbsDeterminant,
    f32 barycentricTolerance,
    ray_triangle_hit_t *pHit ) noexcept
{
    return Intersection_RayTriangle(
        Ray_Make( segment.start, Segment_Direction( segment ) ),
        triangle, cullMode,
        minimumAbsDeterminant, barycentricTolerance,
        0.0f, 1.0f, pHit );
}

bool_t Intersection_SphereAabb( sphere_t sphere, aabb_t bounds ) noexcept
{
    return Sphere_IsValid( sphere ) && Aabb_IsValid( bounds ) &&
           Aabb_DistanceSquaredToPoint( bounds, sphere.center ) <=
               sphere.radius * sphere.radius;
}

volume_relation_t Intersection_FrustumPoint(
    frustum_t frustum,
    vec3_t point,
    f32 distanceTolerance ) noexcept
{
    const bool_t bValidTolerance = Scalar_IsFinite( distanceTolerance ) &&
                                   distanceTolerance >= 0.0f;
    CY_ASSERT_MSG(
        bValidTolerance,
        "Intersection_FrustumPoint requires a finite nonnegative tolerance." );
    if ( !bValidTolerance || !Frustum_IsFinite( frustum ) ||
         !Vec3_IsFinite( point ) ) {
        return volume_relation_t::OUTSIDE;
    }
    for ( plane_t plane : frustum.planes ) {
        if ( Plane_SignedDistance( plane, point ) < -distanceTolerance ) {
            return volume_relation_t::OUTSIDE;
        }
    }
    return volume_relation_t::INSIDE;
}

volume_relation_t Intersection_FrustumSphere(
    frustum_t frustum,
    sphere_t sphere,
    f32 distanceTolerance ) noexcept
{
    const bool_t bValidTolerance = Scalar_IsFinite( distanceTolerance ) &&
                                   distanceTolerance >= 0.0f;
    CY_ASSERT_MSG(
        bValidTolerance,
        "Intersection_FrustumSphere requires a finite nonnegative tolerance." );
    if ( !bValidTolerance || !Frustum_IsFinite( frustum ) ||
         !Sphere_IsValid( sphere ) ) {
        return volume_relation_t::OUTSIDE;
    }

    bool_t bIntersects = false;
    for ( plane_t plane : frustum.planes ) {
        const f32 distance = Plane_SignedDistance( plane, sphere.center );
        if ( distance < -sphere.radius - distanceTolerance ) {
            return volume_relation_t::OUTSIDE;
        }
        if ( distance < sphere.radius + distanceTolerance ) {
            bIntersects = true;
        }
    }
    return bIntersects
        ? volume_relation_t::INTERSECTING
        : volume_relation_t::INSIDE;
}

volume_relation_t Intersection_FrustumAabb(
    frustum_t frustum,
    aabb_t bounds,
    f32 distanceTolerance ) noexcept
{
    const bool_t bValidTolerance = Scalar_IsFinite( distanceTolerance ) &&
                                   distanceTolerance >= 0.0f;
    CY_ASSERT_MSG(
        bValidTolerance,
        "Intersection_FrustumAabb requires a finite nonnegative tolerance." );
    if ( !bValidTolerance || !Frustum_IsFinite( frustum ) ||
         !Aabb_IsValid( bounds ) ) {
        return volume_relation_t::OUTSIDE;
    }

    bool_t bIntersects = false;
    for ( plane_t plane : frustum.planes ) {
        const vec3_t positive = Vec3_Make(
            plane.normal.x >= 0.0f ? bounds.maximum.x : bounds.minimum.x,
            plane.normal.y >= 0.0f ? bounds.maximum.y : bounds.minimum.y,
            plane.normal.z >= 0.0f ? bounds.maximum.z : bounds.minimum.z );
        if ( Plane_SignedDistance( plane, positive ) < -distanceTolerance ) {
            return volume_relation_t::OUTSIDE;
        }

        const vec3_t negative = Vec3_Make(
            plane.normal.x >= 0.0f ? bounds.minimum.x : bounds.maximum.x,
            plane.normal.y >= 0.0f ? bounds.minimum.y : bounds.maximum.y,
            plane.normal.z >= 0.0f ? bounds.minimum.z : bounds.maximum.z );
        if ( Plane_SignedDistance( plane, negative ) < distanceTolerance ) {
            bIntersects = true;
        }
    }
    return bIntersects
        ? volume_relation_t::INTERSECTING
        : volume_relation_t::INSIDE;
}

} // namespace cypher::math
