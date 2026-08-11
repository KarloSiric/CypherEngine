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

}           // namespace cypher::math

#endif      // CYPHER_COMMON_MATH_VECTOR3_INL
