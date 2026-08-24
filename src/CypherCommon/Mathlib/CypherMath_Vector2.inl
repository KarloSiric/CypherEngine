//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Vector2.inl
//  Purpose: Implements constexpr Vector2 operations.
//  Details: These definitions remain visible for compile-time evaluation and
//           ordinary compiler inlining without forcing checked code into headers.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Vector2 Template Definitions

Operations follow CypherMath coordinate, storage, and multiplication conventions. Inputs may
alias only where documented, and normalization handles degenerate values explicitly. Template
definitions remain visible at the call site so each concrete instantiation can be compiled
without a separate registration step.
================
*/

#ifndef CYPHER_COMMON_MATH_VECTOR2_INL
#define CYPHER_COMMON_MATH_VECTOR2_INL

#ifndef CYPHER_COMMON_MATH_VECTOR2_H
    #include "CypherMath_Vector2.h"
#endif

#ifndef PRAGMA_ONCE
    #pragma once
#endif

namespace cypher::math
{

// Direct component operations are constexpr and leave validation to checked APIs.
constexpr vec2_t Vec2_Make( f32 x, f32 y ) noexcept { return { x, y }; }
constexpr vec2_t Vec2_Splat( f32 value ) noexcept { return { value, value }; }
constexpr bool_t Vec2_EqualsExact( vec2_t a, vec2_t b ) noexcept
{
    return a.x == b.x && a.y == b.y;
}
constexpr vec2_t Vec2_Add( vec2_t a, vec2_t b ) noexcept
{
    return { a.x + b.x, a.y + b.y };
}
constexpr vec2_t Vec2_Subtract( vec2_t a, vec2_t b ) noexcept
{
    return { a.x - b.x, a.y - b.y };
}
constexpr vec2_t Vec2_MultiplyComponents( vec2_t a, vec2_t b ) noexcept
{
    return { a.x * b.x, a.y * b.y };
}
constexpr vec2_t Vec2_DivideComponents( vec2_t a, vec2_t b ) noexcept
{
    return { a.x / b.x, a.y / b.y };
}
constexpr vec2_t Vec2_Scale( vec2_t value, f32 scale ) noexcept
{
    return { value.x * scale, value.y * scale };
}
constexpr vec2_t Vec2_DivideScalar( vec2_t value, f32 divisor ) noexcept
{
    return { value.x / divisor, value.y / divisor };
}
constexpr vec2_t Vec2_Negate( vec2_t value ) noexcept { return { -value.x, -value.y }; }
constexpr vec2_t Vec2_MulAdd( vec2_t a, vec2_t b, f32 scale ) noexcept
{
    return { a.x + b.x * scale, a.y + b.y * scale };
}
constexpr f32 Vec2_Dot( vec2_t a, vec2_t b ) noexcept
{
    return a.x * b.x + a.y * b.y;
}
constexpr f32 Vec2_Cross( vec2_t a, vec2_t b ) noexcept
{
    // The scalar determinant is positive when b is counter-clockwise from a.
    return a.x * b.y - a.y * b.x;
}
constexpr f32 Vec2_LengthSquared( vec2_t value ) noexcept { return Vec2_Dot( value, value ); }
constexpr f32 Vec2_DistanceSquared( vec2_t a, vec2_t b ) noexcept
{
    return Vec2_LengthSquared( Vec2_Subtract( a, b ) );
}
constexpr vec2_t Vec2_PerpendicularCCW( vec2_t value ) noexcept
{
    return { -value.y, value.x };
}
constexpr vec2_t Vec2_PerpendicularCW( vec2_t value ) noexcept
{
    return { value.y, -value.x };
}
constexpr vec2_t Vec2_Lerp( vec2_t a, vec2_t b, f32 t ) noexcept
{
    return Vec2_MulAdd( a, Vec2_Subtract( b, a ), t );
}
constexpr vec2_t Vec2_ProjectOntoUnit( vec2_t value, vec2_t unitDirection ) noexcept
{
    return Vec2_Scale( unitDirection, Vec2_Dot( value, unitDirection ) );
}
constexpr vec2_t Vec2_RejectFromUnit( vec2_t value, vec2_t unitDirection ) noexcept
{
    return Vec2_Subtract( value, Vec2_ProjectOntoUnit( value, unitDirection ) );
}
constexpr vec2_t Vec2_ReflectUnitNormal( vec2_t incident, vec2_t unitNormal ) noexcept
{
    return Vec2_MulAdd(
        incident, unitNormal, -2.0f * Vec2_Dot( incident, unitNormal ) );
}

} // namespace cypher::math

#endif // CYPHER_COMMON_MATH_VECTOR2_INL
