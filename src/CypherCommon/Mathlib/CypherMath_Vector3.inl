//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Vector3.inl
//  Purpose: Implements compile-time and lightweight Vector3 operations.
//  Details: These definitions remain visible to every translation unit so
//           constexpr evaluation and normal compiler inlining are possible.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Vector3 Template Definitions

Operations follow CypherMath coordinate, storage, and multiplication conventions. Inputs may
alias only where documented, and normalization handles degenerate values explicitly. Template
definitions remain visible at the call site so each concrete instantiation can be compiled
without a separate registration step.
================
*/


#ifndef CYPHER_COMMON_MATH_VECTOR3_INL
#define CYPHER_COMMON_MATH_VECTOR3_INL

#ifndef CYPHER_COMMON_MATH_VECTOR3_H
    #include "CypherMath_Vector3.h"
#endif

#ifndef PRAGMA_ONCE
    #pragma once
#endif


namespace cypher::math
{

// Hot component operations stay visible for constant folding and normal inlining.
constexpr vec3_t Vec3_Make( f32 x, f32 y, f32 z ) noexcept
{
    return vec3_t{ x, y, z };
}

constexpr vec3_t Vec3_Splat( f32 value ) noexcept
{
    return Vec3_Make( value, value, value );
}

constexpr bool_t Vec3_EqualsExact( vec3_t a, vec3_t b ) noexcept
{
    return ( a.x == b.x && a.y == b.y && a.z == b.z );
}

constexpr vec3_t Vec3_Add( vec3_t a, vec3_t b ) noexcept
{
    return Vec3_Make( a.x + b.x, a.y + b.y, a.z + b.z );
}

constexpr vec3_t Vec3_Subtract( vec3_t a, vec3_t b ) noexcept
{
    return Vec3_Make( a.x - b.x, a.y - b.y, a.z - b.z );
}

constexpr vec3_t Vec3_MultiplyComponents( vec3_t a, vec3_t b ) noexcept
{
    return Vec3_Make( a.x * b.x, a.y * b.y, a.z * b.z );
}

constexpr vec3_t Vec3_DivideComponents( vec3_t a, vec3_t b ) noexcept
{
    return Vec3_Make( a.x / b.x, a.y / b.y, a.z / b.z );
}

constexpr vec3_t Vec3_Scale( vec3_t value, f32 scale ) noexcept
{
    return Vec3_Make( value.x * scale, value.y * scale, value.z * scale );
}

constexpr vec3_t Vec3_DivideScalar( vec3_t value, f32 divisor ) noexcept
{
    return Vec3_Make( value.x / divisor, value.y / divisor, value.z / divisor );
}

constexpr vec3_t Vec3_Negate( vec3_t value ) noexcept
{
    return Vec3_Make( -value.x, -value.y, -value.z );
}

constexpr vec3_t Vec3_MulAdd( vec3_t a, vec3_t b, f32 scale ) noexcept
{
    return Vec3_Make(
        a.x + b.x * scale, a.y + b.y * scale, a.z + b.z * scale );
}

constexpr f32 Vec3_Dot( vec3_t a, vec3_t b ) noexcept
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

constexpr vec3_t Vec3_Cross( vec3_t a, vec3_t b ) noexcept
{
    // Right-handed cross product under CypherMath's forward/left/up basis.
    return Vec3_Make(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x );
}

constexpr f32 Vec3_SumComponents( vec3_t v ) noexcept
{
    return v.x + v.y + v.z;
}

constexpr f32 Vec3_ProductComponents( vec3_t v ) noexcept
{
    return v.x * v.y * v.z;
}

constexpr f32 Vec3_LengthSquared( vec3_t v ) noexcept
{
    return Vec3_Dot( v, v );
}

constexpr f32 Vec3_LengthXYSquared( vec3_t v ) noexcept
{
    return v.x * v.x + v.y * v.y;
}

constexpr f32 Vec3_DistanceSquared( vec3_t a, vec3_t b ) noexcept
{
    return Vec3_LengthSquared( Vec3_Subtract( a, b ) );
}

constexpr vec3_t Vec3_Lerp( vec3_t a, vec3_t b, f32 t ) noexcept
{
    return Vec3_MulAdd( a, Vec3_Subtract( b, a ), t );
}

constexpr vec3_t Vec3_ProjectOntoUnit( vec3_t v, vec3_t unit ) noexcept
{
    return Vec3_Scale( unit, Vec3_Dot( v, unit ) );
}

constexpr vec3_t Vec3_RejectFromUnit( vec3_t v, vec3_t unit ) noexcept
{
    return Vec3_Subtract( v, Vec3_ProjectOntoUnit( v, unit ) );
}

constexpr vec3_t Vec3_ProjectOntoPlaneUnitNormal( vec3_t v, vec3_t n ) noexcept
{
    return Vec3_RejectFromUnit( v, n );
}

constexpr vec3_t Vec3_ReflectUnitNormal( vec3_t incident, vec3_t n ) noexcept
{
    return Vec3_MulAdd( incident, n, -2.0f * Vec3_Dot( incident, n ) );
}

constexpr vec3d_t Vec3d_Make( f64 x, f64 y, f64 z ) noexcept
{
    return { x, y, z };
}

// Added for the constructioning from the vec3!!!! -> for geometrical purposes
constexpr vec3d_t Vec3d_FromVec3( vec3_t value ) noexcept
{
    return Vec3d_Make(
        static_cast<f64>( value.x ),
        static_cast<f64>( value.y ),
        static_cast<f64>( value.z ) );
}

constexpr vec3d_t Vec3d_Splat( f64 value ) noexcept
{
    return Vec3d_Make( value, value, value );
}

constexpr bool_t Vec3d_EqualsExact( vec3d_t a, vec3d_t b ) noexcept
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

constexpr vec3d_t Vec3d_Add( vec3d_t a, vec3d_t b ) noexcept
{
    return Vec3d_Make( a.x + b.x, a.y + b.y, a.z + b.z );
}

constexpr vec3d_t Vec3d_Subtract( vec3d_t a, vec3d_t b ) noexcept
{
    return Vec3d_Make( a.x - b.x, a.y - b.y, a.z - b.z );
}

constexpr vec3d_t Vec3d_Scale( vec3d_t value, f64 scale ) noexcept
{
    return Vec3d_Make( value.x * scale, value.y * scale, value.z * scale );
}

constexpr vec3d_t Vec3d_DivideScalar( vec3d_t value, f64 divisor ) noexcept
{
    return Vec3d_Make(
        value.x / divisor,
        value.y / divisor,
        value.z / divisor );
}

constexpr vec3d_t Vec3d_Negate( vec3d_t value ) noexcept
{
    return Vec3d_Make( -value.x, -value.y, -value.z );
}

constexpr f64 Vec3d_Dot( vec3d_t a, vec3d_t b ) noexcept
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

constexpr vec3d_t Vec3d_Cross( vec3d_t a, vec3d_t b ) noexcept
{
    return Vec3d_Make(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x );
}

constexpr f64 Vec3d_LengthSquared( vec3d_t value ) noexcept
{
    return Vec3d_Dot( value, value );
}

constexpr vec3d_t Vec3d_MultiplyComponents( vec3d_t a, vec3d_t b ) noexcept
{
    return Vec3d_Make( a.x * b.x, a.y * b.y, a.z * b.z );
}

constexpr vec3d_t Vec3d_DivideComponents( vec3d_t a, vec3d_t b ) noexcept
{
    return Vec3d_Make( a.x / b.x, a.y / b.y, a.z / b.z );
}

constexpr vec3d_t Vec3d_MulAdd( vec3d_t a, vec3d_t b, f64 scale ) noexcept
{
    return Vec3d_Make( a.x + b.x * scale, a.y + b.y * scale, a.z + b.z * scale );
}

constexpr f64 Vec3d_SumComponents( vec3d_t v ) noexcept
{
    return v.x + v.y + v.z;
}

constexpr f64 Vec3d_ProductComponents( vec3d_t v ) noexcept
{
    return v.x * v.y * v.z;
}

constexpr f64 Vec3d_DistanceSquared( vec3d_t a, vec3d_t b ) noexcept
{
    return Vec3d_LengthSquared( Vec3d_Subtract( a, b ) );
}

constexpr vec3d_t Vec3d_Lerp( vec3d_t a, vec3d_t b, f64 t ) noexcept
{
    return Vec3d_MulAdd( a, Vec3d_Subtract( b, a ), t );
}

constexpr vec3d_t Vec3d_ProjectOntoUnit( vec3d_t v, vec3d_t unit ) noexcept
{
    return Vec3d_Scale( unit, Vec3d_Dot( v, unit ) );
}

constexpr vec3d_t Vec3d_RejectFromUnit( vec3d_t v, vec3d_t unit ) noexcept
{
    return Vec3d_Subtract( v, Vec3d_ProjectOntoUnit( v, unit ) );
}

constexpr vec3d_t Vec3d_ProjectOntoPlaneUnitNormal( vec3d_t v, vec3d_t n ) noexcept
{
    return Vec3d_RejectFromUnit( v, n );
}

constexpr vec3d_t Vec3d_ReflectUnitNormal( vec3d_t incident, vec3d_t n ) noexcept
{
    return Vec3d_MulAdd( incident, n, -2.0 * Vec3d_Dot( incident, n ) );
}

constexpr bool_t Vec3d_LexicographicLess( vec3d_t a, vec3d_t b ) noexcept
{
    if ( a.x != b.x ) {
        return a.x < b.x;
    }
    if ( a.y != b.y ) {
        return a.y < b.y;
    }
    return a.z < b.z;
}

}           // namespace cypher::math

#endif      // CYPHER_COMMON_MATH_VECTOR3_INL
