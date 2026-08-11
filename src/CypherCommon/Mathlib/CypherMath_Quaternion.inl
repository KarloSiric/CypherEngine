//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Quaternion.inl
//  Purpose: Implements constexpr quaternion operations.
//  Details: The Hamilton product remains visible because composition is a central
//           hot path for animation and transform evaluation.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_MATH_QUATERNION_INL
#define CYPHER_COMMON_MATH_QUATERNION_INL

#ifndef CYPHER_COMMON_MATH_QUATERNION_H
    #include "CypherMath_Quaternion.h"
#endif

#ifndef PRAGMA_ONCE
    #pragma once
#endif

namespace cypher::math
{

constexpr quat_t Quat_Make( f32 x, f32 y, f32 z, f32 w ) noexcept
{
    return { x, y, z, w };
}
constexpr quat_t Quat_FromVectorScalar( vec3_t vector, f32 scalar ) noexcept
{
    return { vector.x, vector.y, vector.z, scalar };
}
constexpr vec3_t Quat_VectorPart( quat_t value ) noexcept
{
    return Vec3_Make( value.x, value.y, value.z );
}
constexpr bool_t Quat_EqualsExact( quat_t a, quat_t b ) noexcept
{
    return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}
constexpr quat_t Quat_Add( quat_t a, quat_t b ) noexcept
{
    return { a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w };
}
constexpr quat_t Quat_Subtract( quat_t a, quat_t b ) noexcept
{
    return { a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w };
}
constexpr quat_t Quat_Scale( quat_t value, f32 scale ) noexcept
{
    return { value.x * scale, value.y * scale, value.z * scale, value.w * scale };
}
constexpr quat_t Quat_Negate( quat_t value ) noexcept
{
    return { -value.x, -value.y, -value.z, -value.w };
}
constexpr quat_t Quat_Conjugate( quat_t value ) noexcept
{
    return { -value.x, -value.y, -value.z, value.w };
}
constexpr f32 Quat_Dot( quat_t a, quat_t b ) noexcept
{
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}
constexpr f32 Quat_LengthSquared( quat_t value ) noexcept
{
    return Quat_Dot( value, value );
}
constexpr quat_t Quat_Multiply( quat_t a, quat_t b ) noexcept
{
    return {
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
    };
}

} // namespace cypher::math

#endif // CYPHER_COMMON_MATH_QUATERNION_INL
