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

#include <algorithm>
#include <limits>

namespace cypher::math
{

// A plane stores n.x + d = 0. Distance queries require a unit-length normal;
// constructors and transforms therefore normalize before publishing output.

bool_t Plane_IsFinite( plane_t value ) noexcept
{
    return Vec3_IsFinite( value.normal ) && Scalar_IsFinite( value.d );
}

bool_t Plane_IsValid( plane_t value, f32 minimumNormalLength ) noexcept
{
    if ( !Plane_IsFinite( value ) ||
         !Scalar_IsFinite( minimumNormalLength ) || minimumNormalLength < 0.0f ) {
        return false;
    }

    // Validity only needs a nondegenerate normal. Compare squared lengths in
    // double precision so this query avoids normalization's square root and
    // does not overflow for finite float coefficients near FLT_MAX.
    const f64 x = static_cast<f64>( value.normal.x );
    const f64 y = static_cast<f64>( value.normal.y );
    const f64 z = static_cast<f64>( value.normal.z );
    const f64 lengthSquared = x * x + y * y + z * z;
    const f64 minimum = static_cast<f64>( minimumNormalLength );
    return lengthSquared > minimum * minimum;
}

bool_t Plane_IsNormalized( plane_t value, f32 tolerance ) noexcept
{
    if ( !Scalar_IsFinite( tolerance ) || tolerance < 0.0f ||
         !Plane_IsFinite( value ) ) {
        return false;
    }

    // Compare squared bounds in double precision. This is equivalent to an
    // absolute tolerance on vector length without paying for a square root.
    const f64 x = static_cast<f64>( value.normal.x );
    const f64 y = static_cast<f64>( value.normal.y );
    const f64 z = static_cast<f64>( value.normal.z );
    const f64 lengthSquared = x * x + y * y + z * z;
    const f64 tolerance64 = static_cast<f64>( tolerance );
    const f64 minimumLength = std::max( 0.0, 1.0 - tolerance64 );
    const f64 maximumLength = 1.0 + tolerance64;
    return lengthSquared > 0.0 &&
           lengthSquared >= minimumLength * minimumLength &&
           lengthSquared <= maximumLength * maximumLength;
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
    const plane_t plane = Plane_Make(
        unitNormal, -Vec3_Dot( unitNormal, point ) );
    if ( !Plane_IsFinite( plane ) ) {
        return false;
    }
    *pPlane = plane;
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
    if ( !bValidTolerance ||
         !Plane_IsNormalized( unitPlane, CY_PLANE_UNIT_TOLERANCE ) ||
         !Vec3_IsFinite( point ) ) {
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

//==========================================================================
// Binary64 authoring plane
//==========================================================================

bool_t Planed_IsFinite( planed_t value ) noexcept
{
    return Vec3d_IsFinite( value.normal ) && Scalar_IsFinite( value.d );
}

bool_t Planed_IsNormalized( planed_t value, f64 tolerance ) noexcept
{
    return Planed_IsFinite( value ) &&
           Vec3d_IsUnitLength( value.normal, tolerance );
}

bool_t Planed_TryNormalize(
    planed_t value,
    f64 minimumNormalLength,
    planed_t *pNormalized ) noexcept
{
    const bool_t bValidOutput = pNormalized != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Planed_TryNormalize requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pNormalized = CY_PLANED_Z;

    vec3d_t normal{};
    f64 originalLength = 0.0;
    if ( !Scalar_IsFinite( value.d ) ||
         !Vec3d_TryNormalize(
             value.normal, minimumNormalLength, &normal, &originalLength ) ) {
        return false;
    }
    // Dividing both n and d by the same length preserves the plane equation.
    const planed_t normalized = Planed_Make( normal, value.d / originalLength );
    if ( !Planed_IsFinite( normalized ) ) {
        return false;
    }
    *pNormalized = normalized;
    return true;
}

bool_t Planed_TryFromPointNormal(
    vec3d_t point,
    vec3d_t normal,
    f64 minimumNormalLength,
    planed_t *pPlane ) noexcept
{
    const bool_t bValidOutput = pPlane != nullptr;
    CY_ASSERT_MSG(
        bValidOutput,
        "Planed_TryFromPointNormal requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pPlane = CY_PLANED_Z;

    vec3d_t unitNormal{};
    if ( !Vec3d_IsFinite( point ) ||
         !Vec3d_TryNormalize(
             normal, minimumNormalLength, &unitNormal, nullptr ) ) {
        return false;
    }
    const planed_t plane = Planed_Make(
        unitNormal, -Vec3d_Dot( unitNormal, point ) );
    if ( !Planed_IsFinite( plane ) ) {
        return false;
    }
    *pPlane = plane;
    return true;
}

bool_t Planed_TryFromTriangle(
    vec3d_t a,
    vec3d_t b,
    vec3d_t c,
    f64 minimumTwiceArea,
    planed_t *pPlane ) noexcept
{
    const bool_t bValidOutput = pPlane != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Planed_TryFromTriangle requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pPlane = CY_PLANED_Z;
    const vec3d_t normal = Vec3d_Cross(
        Vec3d_Subtract( b, a ),
        Vec3d_Subtract( c, a ) );
    return Planed_TryFromPointNormal(
        a, normal, minimumTwiceArea, pPlane );
}

plane_side_t Planed_ClassifyPoint(
    planed_t unitPlane,
    vec3d_t point,
    f64 distanceTolerance ) noexcept
{
    const bool_t bValidTolerance = Scalar_IsFinite( distanceTolerance ) &&
                                   distanceTolerance >= 0.0;
    CY_ASSERT_MSG(
        bValidTolerance,
        "Planed_ClassifyPoint requires a finite nonnegative tolerance." );
    if ( !bValidTolerance ||
         !Planed_IsNormalized(
             unitPlane, static_cast<f64>( CY_PLANE_UNIT_TOLERANCE ) ) ||
         !Vec3d_IsFinite( point ) ) {
        return plane_side_t::ON_PLANE;
    }
    const f64 distance = Planed_SignedDistance( unitPlane, point );
    if ( distance > distanceTolerance ) {
        return plane_side_t::POSITIVE;
    }
    if ( distance < -distanceTolerance ) {
        return plane_side_t::NEGATIVE;
    }
    return plane_side_t::ON_PLANE;
}

bool_t Planed_TryTransform(
    planed_t plane,
    affine3d_t transform,
    f64 minimumAbsDeterminant,
    f64 minimumNormalLength,
    planed_t *pTransformed ) noexcept
{
    const bool_t bValidOutput = pTransformed != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Planed_TryTransform requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pTransformed = CY_PLANED_Z;

    planed_t unitPlane{};
    if ( !Planed_TryNormalize( plane, minimumNormalLength, &unitPlane ) ) {
        return false;
    }
    const vec3d_t pointOnPlane = Vec3d_Scale( unitPlane.normal, -unitPlane.d );
    const vec3d_t transformedPoint =
        Affine3d_TransformPoint( transform, pointOnPlane );
    vec3d_t transformedNormal{};
    if ( !Affine3d_TryTransformNormal(
             transform, unitPlane.normal, minimumAbsDeterminant,
             &transformedNormal ) ) {
        return false;
    }
    return Planed_TryFromPointNormal(
        transformedPoint, transformedNormal,
        minimumNormalLength, pTransformed );
}

bool_t Planed_TryToPlane( planed_t value, plane_t *pResult ) noexcept
{
    const bool_t bValidOutput = pResult != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Planed_TryToPlane requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pResult = CY_PLANE_Z;

    vec3_t narrowedNormal{};
    if ( !Vec3d_TryToVec3( value.normal, &narrowedNormal ) ) {
        return false;
    }
    constexpr f64 kMaxF32AsF64 = static_cast<f64>( std::numeric_limits<f32>::max() );
    if ( !Scalar_IsFinite( value.d ) || Scalar_Abs( value.d ) > kMaxF32AsF64 ) {
        return false;
    }
    *pResult = Plane_Make( narrowedNormal, static_cast<f32>( value.d ) );
    return true;
}

} // namespace cypher::math
