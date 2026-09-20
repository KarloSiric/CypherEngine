//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Bounds.cpp
//  Purpose: Implements AABB construction, metrics, and transformation.
//  Details: Affine transformation maps center and extents using the absolute
//           linear matrix, avoiding eight separate corner transformations.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Bounds Implementation Notes

Geometry queries keep boundary policy explicit: hit ranges, parallel tolerances, and
inside/outside tests are returned as data rather than inferred from global state.
================
*/

#include "CypherMath_Bounds.h"
#include "CypherMath_Scalar.h"
#include "CypherCommon_Assert.h"

namespace cypher::math
{

bool_t Aabb_IsFinite( aabb_t bounds ) noexcept
{
    return Vec3_IsFinite( bounds.minimum ) && Vec3_IsFinite( bounds.maximum );
}

bool_t Aabb_IsValid( aabb_t bounds ) noexcept
{
    return Aabb_IsFinite( bounds ) && !Aabb_IsEmpty( bounds );
}

aabb_t Aabb_FromCenterExtents( vec3_t center, vec3_t extents ) noexcept
{
    // Negative authored extents collapse to zero instead of reversing bounds.
    const vec3_t nonnegativeExtents = Vec3_Max( extents, CY_VEC3_ZERO );
    return Aabb_Make(
        Vec3_Subtract( center, nonnegativeExtents ),
        Vec3_Add( center, nonnegativeExtents ) );
}

aabb_t Aabb_ExpandPoint( aabb_t bounds, vec3_t point ) noexcept
{
    if ( Aabb_IsEmpty( bounds ) ) {
        return Aabb_FromPoint( point );
    }
    return Aabb_Make(
        Vec3_Min( bounds.minimum, point ),
        Vec3_Max( bounds.maximum, point ) );
}

aabb_t Aabb_ExpandAabb( aabb_t bounds, aabb_t other ) noexcept
{
    return Aabb_Union( bounds, other );
}

aabb_t Aabb_Union( aabb_t a, aabb_t b ) noexcept
{
    // Empty is the identity element for union.
    if ( Aabb_IsEmpty( a ) ) {
        return b;
    }
    if ( Aabb_IsEmpty( b ) ) {
        return a;
    }
    return Aabb_Make(
        Vec3_Min( a.minimum, b.minimum ),
        Vec3_Max( a.maximum, b.maximum ) );
}

aabb_t Aabb_Intersection( aabb_t a, aabb_t b ) noexcept
{
    // Disjoint boxes return the canonical reversed-extrema sentinel.
    if ( !Aabb_Overlaps( a, b ) ) {
        return CY_AABB_EMPTY;
    }
    return Aabb_Make(
        Vec3_Max( a.minimum, b.minimum ),
        Vec3_Min( a.maximum, b.maximum ) );
}

vec3_t Aabb_Center( aabb_t bounds ) noexcept
{
    return Aabb_IsEmpty( bounds )
        ? CY_VEC3_ZERO
        : Vec3_Scale( Vec3_Add( bounds.minimum, bounds.maximum ), 0.5f );
}

vec3_t Aabb_Size( aabb_t bounds ) noexcept
{
    return Aabb_IsEmpty( bounds )
        ? CY_VEC3_ZERO
        : Vec3_Subtract( bounds.maximum, bounds.minimum );
}

vec3_t Aabb_Extents( aabb_t bounds ) noexcept
{
    return Vec3_Scale( Aabb_Size( bounds ), 0.5f );
}

f32 Aabb_Volume( aabb_t bounds ) noexcept
{
    const vec3_t size = Aabb_Size( bounds );
    return size.x * size.y * size.z;
}

f32 Aabb_SurfaceArea( aabb_t bounds ) noexcept
{
    const vec3_t size = Aabb_Size( bounds );
    return 2.0f * (
        size.x * size.y + size.x * size.z + size.y * size.z );
}

vec3_t Aabb_Corner( aabb_t bounds, u32 iCorner ) noexcept
{
    const bool_t bValidIndex = iCorner < 8u;
    CY_ASSERT_MSG( bValidIndex, "Aabb_Corner index must be in [0, 7]." );
    if ( !bValidIndex || Aabb_IsEmpty( bounds ) ) {
        return CY_VEC3_ZERO;
    }
    // Corner bits select maximum X, Y, and Z respectively.
    return Vec3_Make(
        ( iCorner & 1u ) != 0u ? bounds.maximum.x : bounds.minimum.x,
        ( iCorner & 2u ) != 0u ? bounds.maximum.y : bounds.minimum.y,
        ( iCorner & 4u ) != 0u ? bounds.maximum.z : bounds.minimum.z );
}

vec3_t Aabb_ClosestPoint( aabb_t bounds, vec3_t point ) noexcept
{
    return Aabb_IsEmpty( bounds )
        ? point
        : Vec3_Clamp( point, bounds.minimum, bounds.maximum );
}

f32 Aabb_DistanceSquaredToPoint( aabb_t bounds, vec3_t point ) noexcept
{
    return Vec3_DistanceSquared( Aabb_ClosestPoint( bounds, point ), point );
}

aabb_t Aabb_TransformAffine( aabb_t bounds, affine3_t transform ) noexcept
{
    if ( Aabb_IsEmpty( bounds ) ) {
        return CY_AABB_EMPTY;
    }

    // Transform center directly. Absolute basis columns accumulate the maximum
    // contribution of each source extent without enumerating eight corners.
    const vec3_t center = Affine3_TransformPoint(
        transform, Aabb_Center( bounds ) );
    const vec3_t sourceExtents = Aabb_Extents( bounds );
    const vec3_t transformedExtents = Vec3_Add(
        Vec3_Add(
            Vec3_Scale(
                Vec3_Abs( Affine3_Column( transform, 0u ) ),
                sourceExtents.x ),
            Vec3_Scale(
                Vec3_Abs( Affine3_Column( transform, 1u ) ),
                sourceExtents.y ) ),
        Vec3_Scale(
            Vec3_Abs( Affine3_Column( transform, 2u ) ),
            sourceExtents.z ) );
    return Aabb_FromCenterExtents( center, transformedExtents );
}

//==========================================================================
// Binary64 authoring bounds
//==========================================================================

bool_t Aabbd_IsFinite( aabbd_t bounds ) noexcept
{
    return Vec3d_IsFinite( bounds.minimum ) && Vec3d_IsFinite( bounds.maximum );
}

bool_t Aabbd_IsValid( aabbd_t bounds ) noexcept
{
    return Aabbd_IsFinite( bounds ) && !Aabbd_IsEmpty( bounds );
}

aabbd_t Aabbd_FromCenterExtents( vec3d_t center, vec3d_t extents ) noexcept
{
    const vec3d_t nonnegativeExtents = Vec3d_Max( extents, CY_VEC3D_ZERO );
    return Aabbd_Make(
        Vec3d_Subtract( center, nonnegativeExtents ),
        Vec3d_Add( center, nonnegativeExtents ) );
}

aabbd_t Aabbd_ExpandPoint( aabbd_t bounds, vec3d_t point ) noexcept
{
    if ( Aabbd_IsEmpty( bounds ) ) {
        return Aabbd_FromPoint( point );
    }
    return Aabbd_Make(
        Vec3d_Min( bounds.minimum, point ),
        Vec3d_Max( bounds.maximum, point ) );
}

aabbd_t Aabbd_ExpandAabb( aabbd_t bounds, aabbd_t other ) noexcept
{
    return Aabbd_Union( bounds, other );
}

aabbd_t Aabbd_Union( aabbd_t a, aabbd_t b ) noexcept
{
    if ( Aabbd_IsEmpty( a ) ) {
        return b;
    }
    if ( Aabbd_IsEmpty( b ) ) {
        return a;
    }
    return Aabbd_Make(
        Vec3d_Min( a.minimum, b.minimum ),
        Vec3d_Max( a.maximum, b.maximum ) );
}

aabbd_t Aabbd_Intersection( aabbd_t a, aabbd_t b ) noexcept
{
    if ( !Aabbd_Overlaps( a, b ) ) {
        return CY_AABBD_EMPTY;
    }
    return Aabbd_Make(
        Vec3d_Max( a.minimum, b.minimum ),
        Vec3d_Min( a.maximum, b.maximum ) );
}

vec3d_t Aabbd_Center( aabbd_t bounds ) noexcept
{
    return Aabbd_IsEmpty( bounds )
        ? CY_VEC3D_ZERO
        : Vec3d_Scale( Vec3d_Add( bounds.minimum, bounds.maximum ), 0.5 );
}

vec3d_t Aabbd_Size( aabbd_t bounds ) noexcept
{
    return Aabbd_IsEmpty( bounds )
        ? CY_VEC3D_ZERO
        : Vec3d_Subtract( bounds.maximum, bounds.minimum );
}

vec3d_t Aabbd_Extents( aabbd_t bounds ) noexcept
{
    return Vec3d_Scale( Aabbd_Size( bounds ), 0.5 );
}

f64 Aabbd_Volume( aabbd_t bounds ) noexcept
{
    const vec3d_t size = Aabbd_Size( bounds );
    return size.x * size.y * size.z;
}

f64 Aabbd_SurfaceArea( aabbd_t bounds ) noexcept
{
    const vec3d_t size = Aabbd_Size( bounds );
    return 2.0 * (
        size.x * size.y + size.x * size.z + size.y * size.z );
}

vec3d_t Aabbd_Corner( aabbd_t bounds, u32 iCorner ) noexcept
{
    const bool_t bValidIndex = iCorner < 8u;
    CY_ASSERT_MSG( bValidIndex, "Aabbd_Corner index must be in [0, 7]." );
    if ( !bValidIndex || Aabbd_IsEmpty( bounds ) ) {
        return CY_VEC3D_ZERO;
    }
    return Vec3d_Make(
        ( iCorner & 1u ) != 0u ? bounds.maximum.x : bounds.minimum.x,
        ( iCorner & 2u ) != 0u ? bounds.maximum.y : bounds.minimum.y,
        ( iCorner & 4u ) != 0u ? bounds.maximum.z : bounds.minimum.z );
}

vec3d_t Aabbd_ClosestPoint( aabbd_t bounds, vec3d_t point ) noexcept
{
    return Aabbd_IsEmpty( bounds )
        ? point
        : Vec3d_Clamp( point, bounds.minimum, bounds.maximum );
}

f64 Aabbd_DistanceSquaredToPoint( aabbd_t bounds, vec3d_t point ) noexcept
{
    return Vec3d_DistanceSquared( Aabbd_ClosestPoint( bounds, point ), point );
}

aabbd_t Aabbd_TransformAffine( aabbd_t bounds, affine3d_t transform ) noexcept
{
    if ( Aabbd_IsEmpty( bounds ) ) {
        return CY_AABBD_EMPTY;
    }

    const vec3d_t center = Affine3d_TransformPoint(
        transform, Aabbd_Center( bounds ) );
    const vec3d_t sourceExtents = Aabbd_Extents( bounds );
    const vec3d_t transformedExtents = Vec3d_Add(
        Vec3d_Add(
            Vec3d_Scale(
                Vec3d_Abs( Affine3d_Column( transform, 0u ) ),
                sourceExtents.x ),
            Vec3d_Scale(
                Vec3d_Abs( Affine3d_Column( transform, 1u ) ),
                sourceExtents.y ) ),
        Vec3d_Scale(
            Vec3d_Abs( Affine3d_Column( transform, 2u ) ),
            sourceExtents.z ) );
    return Aabbd_FromCenterExtents( center, transformedExtents );
}

bool_t Aabbd_TryToAabb( aabbd_t value, aabb_t *pResult ) noexcept
{
    const bool_t bValidOutput = pResult != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Aabbd_TryToAabb requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pResult = CY_AABB_EMPTY;

    vec3_t narrowedMin{};
    vec3_t narrowedMax{};
    if ( !Vec3d_TryToVec3( value.minimum, &narrowedMin ) ||
         !Vec3d_TryToVec3( value.maximum, &narrowedMax ) ) {
        return false;
    }
    *pResult = Aabb_Make( narrowedMin, narrowedMax );
    return true;
}

} // namespace cypher::math
