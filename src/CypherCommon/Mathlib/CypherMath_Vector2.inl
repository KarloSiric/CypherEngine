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
// NOTE: double precision d componenets for the vector2 calculations needed for the geometry later on.
constexpr vec2d_t Vec2d_MultiplyComponents( vec2d_t a, vec2d_t b ) noexcept
{
    return { a.x * b.x, a.y * b.y };
}
constexpr vec2d_t Vec2d_DivideComponents( vec2d_t a, vec2d_t b ) noexcept
{
    return Vec2d_Make( a.x / b.x, a.y / b.y );
}
constexpr vec2d_t Vec2d_MulAdd( vec2d_t a, vec2d_t b, f64 scale ) noexcept
{
    return Vec2d_Make( a.x + b.x * scale, a.y + b.y * scale );
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
/* Double precision DistanceSquared for the geometrical work */
constexpr f64 Vec2d_DistanceSquared( vec2d_t a, vec2d_t b ) noexcept
{
    return Vec2d_LengthSquared( Vec2d_Subtract( a, b ) );
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
// NOTE: Double precision lerping!
/*************************/
constexpr vec2d_t Vec2d_Lerp( vec2d_t a, vec2d_t b, f64 t ) noexcept
{
    return Vec2d_MulAdd( a, Vec2d_Subtract( b, a ), t );
}
/************************/
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

constexpr vec2d_t Vec2d_Make( f64 x, f64 y ) noexcept
{
    return { x, y };
}

constexpr vec2d_t Vec2d_FromVec2( vec2_t value ) noexcept
{
    return Vec2d_Make( static_cast<f64>( value.x ), static_cast<f64>( value.y ) );
}

constexpr vec2d_t Vec2d_Splat( f64 value ) noexcept
{
    return Vec2d_Make( value, value );
}

constexpr bool_t Vec2d_EqualsExact( vec2d_t a, vec2d_t b ) noexcept
{
    return a.x == b.x && a.y == b.y;
}

constexpr vec2d_t Vec2d_Add( vec2d_t a, vec2d_t b ) noexcept
{
    return Vec2d_Make( a.x + b.x, a.y + b.y );
}

constexpr vec2d_t Vec2d_Subtract( vec2d_t a, vec2d_t b ) noexcept
{
    return Vec2d_Make( a.x - b.x, a.y - b.y );
}

constexpr vec2d_t Vec2d_Scale( vec2d_t value, f64 scale ) noexcept
{
    return Vec2d_Make( value.x * scale, value.y * scale );
}

constexpr vec2d_t Vec2d_DivideScalar( vec2d_t value, f64 divisor ) noexcept
{
    return Vec2d_Make( value.x / divisor, value.y / divisor );
}

constexpr vec2d_t Vec2d_Negate( vec2d_t value ) noexcept
{
    return Vec2d_Make( -value.x, -value.y );
}

constexpr f64 Vec2d_Dot( vec2d_t a, vec2d_t b ) noexcept
{
    return a.x * b.x + a.y * b.y;
}

constexpr f64 Vec2d_Cross( vec2d_t a, vec2d_t b ) noexcept
{
    return a.x * b.y - a.y * b.x;
}

constexpr f64 Vec2d_LengthSquared( vec2d_t value ) noexcept
{
    return Vec2d_Dot( value, value );
}

constexpr vec2d_t Vec2d_PerpendicularCCW( vec2d_t value ) noexcept
{
    return Vec2d_Make( -value.y, value.x );
}

constexpr vec2d_t Vec2d_PerpendicularCW( vec2d_t value ) noexcept
{
    return Vec2d_Make( value.y, -value.x );
}

/* NOTE: Double precisions for projections *******************/
constexpr vec2d_t Vec2d_ProjectOntoUnit( vec2d_t value, vec2d_t unitDirection ) noexcept
{
    return Vec2d_Scale( unitDirection, Vec2d_Dot( value, unitDirection ) );
}

constexpr vec2d_t Vec2d_RejectFromUnit( vec2d_t value, vec2d_t unitDirection ) noexcept
{
    return Vec2d_Subtract( value, Vec2d_ProjectOntoUnit( value, unitDirection ) );
}

constexpr vec2d_t Vec2d_ReflectUnitNormal( vec2d_t incident, vec2d_t unitNormal ) noexcept
{
    return Vec2d_MulAdd( incident, unitNormal, -2.0 * Vec2d_Dot( incident, unitNormal ) );
}

constexpr bool_t Vec2d_LexicographicLess( vec2d_t a, vec2d_t b ) noexcept
{
    if ( a.x != b.x ) {
        return a.x < b.x;
    }
    return a.y < b.y;
}

} // namespace cypher::math

#endif // CYPHER_COMMON_MATH_VECTOR2_INL
