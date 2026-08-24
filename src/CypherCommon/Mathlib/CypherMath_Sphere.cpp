//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Sphere.cpp
//  Purpose: Implements sphere metrics, merging, and transformation.
//  Details: Affine transformation scales radius by an induced matrix-norm bound,
//           remaining conservative under rotation, scale, reflection, and shear.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Sphere Implementation Notes

Geometry queries keep boundary policy explicit: hit ranges, parallel tolerances, and
inside/outside tests are returned as data rather than inferred from global state.
================
*/

#include "CypherMath_Sphere.h"
#include "CypherMath_Scalar.h"

namespace cypher::math
{

bool_t Sphere_IsValid( sphere_t sphere ) noexcept
{
    return Vec3_IsFinite( sphere.center ) &&
           Scalar_IsFinite( sphere.radius ) && sphere.radius >= 0.0f;
}

vec3_t Sphere_ClosestPoint(
    sphere_t sphere,
    vec3_t point,
    f32 minimumDirectionLength ) noexcept
{
    // A point at the center has no unique radial direction; return the center
    // rather than selecting an arbitrary axis.
    const vec3_t offset = Vec3_Subtract( point, sphere.center );
    vec3_t direction{};
    if ( !Vec3_TryNormalize(
             offset, minimumDirectionLength, &direction, nullptr ) ) {
        return sphere.center;
    }
    return Vec3_MulAdd( sphere.center, direction, sphere.radius );
}

f32 Sphere_DistanceToPoint( sphere_t sphere, vec3_t point ) noexcept
{
    return Scalar_Max( Vec3_Distance( sphere.center, point ) - sphere.radius, 0.0f );
}

f32 Sphere_Volume( sphere_t sphere ) noexcept
{
    return ( 4.0f / 3.0f ) * CY_PI_F *
           sphere.radius * sphere.radius * sphere.radius;
}

f32 Sphere_SurfaceArea( sphere_t sphere ) noexcept
{
    return 4.0f * CY_PI_F * sphere.radius * sphere.radius;
}

sphere_t Sphere_FromAabb( aabb_t bounds ) noexcept
{
    if ( Aabb_IsEmpty( bounds ) ) {
        return CY_SPHERE_ZERO;
    }
    // The half diagonal reaches every AABB corner and is therefore conservative.
    const vec3_t center = Aabb_Center( bounds );
    return Sphere_Make( center, Vec3_Distance( center, bounds.maximum ) );
}

sphere_t Sphere_Merge(
    sphere_t a,
    sphere_t b,
    f32 minimumCenterDistance ) noexcept
{
    // Coincident centers and complete containment avoid unstable division and
    // preserve the existing tighter sphere exactly.
    const vec3_t offset = Vec3_Subtract( b.center, a.center );
    const f32 centerDistance = Vec3_Length( offset );
    if ( centerDistance <= minimumCenterDistance ) {
        return a.radius >= b.radius ? a : b;
    }
    if ( a.radius >= centerDistance + b.radius ) {
        return a;
    }
    if ( b.radius >= centerDistance + a.radius ) {
        return b;
    }

    // For two exposed spheres, the minimum enclosing diameter spans the two
    // opposite extreme points along the center line.
    const f32 radius =
        ( centerDistance + a.radius + b.radius ) * 0.5f;
    const f32 shift = ( radius - a.radius ) / centerDistance;
    return Sphere_Make(
        Vec3_MulAdd( a.center, offset, shift ),
        radius );
}

sphere_t Sphere_TransformAffineConservative(
    sphere_t sphere,
    affine3_t transform ) noexcept
{
    // A general affine transform turns a sphere into an ellipsoid. Bound its
    // spectral norm by sqrt(||A||1 * ||A||inf) to retain a safe sphere.
    const mat3_t linear = Affine3_LinearPart( transform );
    f32 maximumColumnSum = 0.0f;
    f32 maximumRowSum = 0.0f;
    for ( u32 column = 0u; column < 3u; ++column ) {
        maximumColumnSum = Scalar_Max(
            maximumColumnSum,
            Vec3_SumComponents( Vec3_Abs( Mat3_Column( linear, column ) ) ) );
    }
    for ( u32 row = 0u; row < 3u; ++row ) {
        maximumRowSum = Scalar_Max(
            maximumRowSum,
            Vec3_SumComponents( Vec3_Abs( Mat3_Row( linear, row ) ) ) );
    }
    const f32 scaleBound =
        Scalar_Sqrt( maximumColumnSum * maximumRowSum );
    return Sphere_Make(
        Affine3_TransformPoint( transform, sphere.center ),
        sphere.radius * scaleBound );
}

} // namespace cypher::math
