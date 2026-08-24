//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Vector4.inl
//  Purpose: Implements constexpr Vector4 operations.
//  Details: Arithmetic remains visible for constant evaluation and compiler
//           optimization while checked operations remain in the source file.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Vector4 Template Definitions

Operations follow CypherMath coordinate, storage, and multiplication conventions. Inputs may
alias only where documented, and normalization handles degenerate values explicitly. Template
definitions remain visible at the call site so each concrete instantiation can be compiled
without a separate registration step.
================
*/

#ifndef CYPHER_COMMON_MATH_VECTOR4_INL
#define CYPHER_COMMON_MATH_VECTOR4_INL

#ifndef CYPHER_COMMON_MATH_VECTOR4_H
    #include "CypherMath_Vector4.h"
#endif

#ifndef PRAGMA_ONCE
    #pragma once
#endif

namespace cypher::math
{

// Direct component operations do not guard division or normalize exceptional values.
constexpr vec4_t Vec4_Make( f32 x, f32 y, f32 z, f32 w ) noexcept
{
    return { x, y, z, w };
}
constexpr vec4_t Vec4_Splat( f32 value ) noexcept
{
    return { value, value, value, value };
}
constexpr vec4_t Vec4_FromVec3( vec3_t xyz, f32 w ) noexcept
{
    return { xyz.x, xyz.y, xyz.z, w };
}
constexpr vec3_t Vec4_XYZ( vec4_t value ) noexcept
{
    return Vec3_Make( value.x, value.y, value.z );
}
constexpr bool_t Vec4_EqualsExact( vec4_t a, vec4_t b ) noexcept
{
    return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}
constexpr vec4_t Vec4_Add( vec4_t a, vec4_t b ) noexcept
{
    return { a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w };
}
constexpr vec4_t Vec4_Subtract( vec4_t a, vec4_t b ) noexcept
{
    return { a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w };
}
constexpr vec4_t Vec4_MultiplyComponents( vec4_t a, vec4_t b ) noexcept
{
    return { a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w };
}
constexpr vec4_t Vec4_DivideComponents( vec4_t a, vec4_t b ) noexcept
{
    return { a.x / b.x, a.y / b.y, a.z / b.z, a.w / b.w };
}
constexpr vec4_t Vec4_Scale( vec4_t value, f32 scale ) noexcept
{
    return { value.x * scale, value.y * scale, value.z * scale, value.w * scale };
}
constexpr vec4_t Vec4_DivideScalar( vec4_t value, f32 divisor ) noexcept
{
    return { value.x / divisor, value.y / divisor, value.z / divisor, value.w / divisor };
}
constexpr vec4_t Vec4_Negate( vec4_t value ) noexcept
{
    return { -value.x, -value.y, -value.z, -value.w };
}
constexpr vec4_t Vec4_MulAdd( vec4_t a, vec4_t b, f32 scale ) noexcept
{
    return {
        a.x + b.x * scale,
        a.y + b.y * scale,
        a.z + b.z * scale,
        a.w + b.w * scale
    };
}
constexpr f32 Vec4_Dot( vec4_t a, vec4_t b ) noexcept
{
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}
constexpr f32 Vec4_LengthSquared( vec4_t value ) noexcept { return Vec4_Dot( value, value ); }
constexpr f32 Vec4_DistanceSquared( vec4_t a, vec4_t b ) noexcept
{
    return Vec4_LengthSquared( Vec4_Subtract( a, b ) );
}
constexpr vec4_t Vec4_Lerp( vec4_t a, vec4_t b, f32 t ) noexcept
{
    return Vec4_MulAdd( a, Vec4_Subtract( b, a ), t );
}

} // namespace cypher::math

#endif // CYPHER_COMMON_MATH_VECTOR4_INL
