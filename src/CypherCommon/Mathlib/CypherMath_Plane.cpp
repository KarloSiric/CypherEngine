//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Plane.cpp
//  Purpose: Implements checked plane construction and transformation.
//  Details: Affine transformation uses inverse-transpose normal handling and a
//           transformed point on the source plane to reconstruct the equation.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMath_Plane.h"
#include "CypherMath_Scalar.h"
#include "CypherCommon_Assert.h"

namespace cypher::math
{

// A plane stores n.x + d = 0. Distance queries require a unit-length normal;
// constructors and transforms therefore normalize before publishing output.

bool_t Plane_IsFinite( plane_t value ) noexcept
{
    return Vec3_IsFinite( value.normal ) && Scalar_IsFinite( value.d );
}

bool_t Plane_IsNormalized( plane_t value, f32 tolerance ) noexcept
{
    return Plane_IsFinite( value ) &&
           Vec3_IsUnitLength( value.normal, tolerance );
}

bool_t Plane_TryNormalize(
    plane_t value,
    f32 minimumNormalLength,
    plane_t *pNormalized ) noexcept
{
    const bool_t bValidOutput = pNormalized != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Plane_TryNormalize requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pNormalized = CY_PLANE_Z;

    vec3_t normal{};
    f32 originalLength = 0.0f;
    if ( !Scalar_IsFinite( value.d ) ||
         !Vec3_TryNormalize(
             value.normal, minimumNormalLength, &normal, &originalLength ) ) {
        return false;
    }
    // Dividing both n and d by the same length preserves the plane equation.
    const plane_t normalized = Plane_Make( normal, value.d / originalLength );
    if ( !Plane_IsFinite( normalized ) ) {
        return false;
    }
    *pNormalized = normalized;
    return true;
}

bool_t Plane_TryFromPointNormal(
    vec3_t point,
    vec3_t normal,
    f32 minimumNormalLength,
    plane_t *pPlane ) noexcept
{
    const bool_t bValidOutput = pPlane != nullptr;
    CY_ASSERT_MSG(
        bValidOutput,
        "Plane_TryFromPointNormal requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pPlane = CY_PLANE_Z;

    vec3_t unitNormal{};
    if ( !Vec3_IsFinite( point ) ||
         !Vec3_TryNormalize(
             normal, minimumNormalLength, &unitNormal, nullptr ) ) {
        return false;
    }
    *pPlane = Plane_Make( unitNormal, -Vec3_Dot( unitNormal, point ) );
    return true;
}

bool_t Plane_TryFromTriangle(
    vec3_t a,
    vec3_t b,
    vec3_t c,
    f32 minimumTwiceArea,
    plane_t *pPlane ) noexcept
{
    const bool_t bValidOutput = pPlane != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Plane_TryFromTriangle requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pPlane = CY_PLANE_Z;
    const vec3_t normal = Vec3_Cross(
        Vec3_Subtract( b, a ),
        Vec3_Subtract( c, a ) );
    return Plane_TryFromPointNormal(
        a, normal, minimumTwiceArea, pPlane );
}

plane_side_t Plane_ClassifyPoint(
    plane_t unitPlane,
    vec3_t point,
    f32 distanceTolerance ) noexcept
{
    const bool_t bValidTolerance = Scalar_IsFinite( distanceTolerance ) &&
                                   distanceTolerance >= 0.0f;
    CY_ASSERT_MSG(
        bValidTolerance,
        "Plane_ClassifyPoint requires a finite nonnegative tolerance." );
    if ( !bValidTolerance ) {
        return plane_side_t::ON_PLANE;
    }
    const f32 distance = Plane_SignedDistance( unitPlane, point );
    // The tolerance creates a stable coplanar band for editor and collision use.
    if ( distance > distanceTolerance ) {
        return plane_side_t::POSITIVE;
    }
    if ( distance < -distanceTolerance ) {
        return plane_side_t::NEGATIVE;
    }
    return plane_side_t::ON_PLANE;
}

bool_t Plane_TryTransform(
    plane_t plane,
    affine3_t transform,
    f32 minimumAbsDeterminant,
    f32 minimumNormalLength,
    plane_t *pTransformed ) noexcept
{
    const bool_t bValidOutput = pTransformed != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Plane_TryTransform requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pTransformed = CY_PLANE_Z;

    plane_t unitPlane{};
    if ( !Plane_TryNormalize( plane, minimumNormalLength, &unitPlane ) ) {
        return false;
    }
    // Transform one known point and the normal independently, then rebuild d.
    // This avoids assuming that translation or non-uniform scale leaves d intact.
    const vec3_t pointOnPlane = Vec3_Scale( unitPlane.normal, -unitPlane.d );
    const vec3_t transformedPoint =
        Affine3_TransformPoint( transform, pointOnPlane );
    vec3_t transformedNormal{};
    if ( !Affine3_TryTransformNormal(
             transform, unitPlane.normal, minimumAbsDeterminant,
             &transformedNormal ) ) {
        return false;
    }
    return Plane_TryFromPointNormal(
        transformedPoint, transformedNormal,
        minimumNormalLength, pTransformed );
}

} // namespace cypher::math
