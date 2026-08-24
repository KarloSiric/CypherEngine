//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Angle.h
//  Purpose: Declares a radians-backed semantic angle value.
//  Details: The type prevents accidental degree/radian mixing while retaining a
//           tightly packed scalar representation suitable for runtime data.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Angle Contract

Angle helpers make degree/radian conversion explicit and normalize only when the called
operation promises it.
================
*/

#ifndef CYPHER_COMMON_MATH_ANGLE_H
#define CYPHER_COMMON_MATH_ANGLE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMath_Scalar.h"

#include <type_traits>

namespace cypher::math
{

struct angle_t {
    f32 radians; // Stored unit; conversion to degrees is always explicit.
};

inline constexpr angle_t CY_ANGLE_ZERO{ 0.0f };                  // 0 degrees.
inline constexpr angle_t CY_ANGLE_QUARTER_TURN{ CY_HALF_PI_F };  // 90 degrees.
inline constexpr angle_t CY_ANGLE_HALF_TURN{ CY_PI_F };          // 180 degrees.
inline constexpr angle_t CY_ANGLE_FULL_TURN{ CY_TAU_F };         // 360 degrees.

CYPHER_NODISCARD constexpr angle_t Angle_FromRadians( f32 radians ) noexcept
{
    return angle_t{ radians };
}

CYPHER_NODISCARD constexpr angle_t Angle_FromDegrees( f32 degrees ) noexcept
{
    return angle_t{ Scalar_DegreesToRadians( degrees ) };
}

CYPHER_NODISCARD constexpr f32 Angle_Radians( angle_t angle ) noexcept
{
    return angle.radians;
}

CYPHER_NODISCARD constexpr f32 Angle_Degrees( angle_t angle ) noexcept
{
    return Scalar_RadiansToDegrees( angle.radians );
}

CYPHER_NODISCARD constexpr angle_t Angle_Add( angle_t a, angle_t b ) noexcept
{
    return Angle_FromRadians( a.radians + b.radians );
}

CYPHER_NODISCARD constexpr angle_t Angle_Subtract( angle_t a, angle_t b ) noexcept
{
    return Angle_FromRadians( a.radians - b.radians );
}

CYPHER_NODISCARD constexpr angle_t Angle_Scale( angle_t angle, f32 scale ) noexcept
{
    return Angle_FromRadians( angle.radians * scale );
}

CYPHER_NODISCARD constexpr angle_t Angle_Negate( angle_t angle ) noexcept
{
    return Angle_FromRadians( -angle.radians );
}

// Wrapped comparison and interpolation ------------------------------------------
CYPHER_NODISCARD CYPHER_MATH_API bool_t Angle_IsFinite( angle_t angle ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API angle_t Angle_NormalizePositive( angle_t angle ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API angle_t Angle_NormalizeSigned( angle_t angle ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API angle_t Angle_ShortestDelta(
    angle_t from, angle_t to ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API angle_t Angle_LerpShortest(
    angle_t from, angle_t to, f32 t ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Angle_NearlyEquivalent(
    angle_t a, angle_t b, f32 toleranceRadians ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Angle_Sin( angle_t angle ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Angle_Cos( angle_t angle ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f32 Angle_Tan( angle_t angle ) noexcept;
CYPHER_MATH_API void Angle_SinCos(
    angle_t angle, CY_OUT f32 *pSin, CY_OUT f32 *pCos ) noexcept;

static_assert( sizeof( angle_t ) == sizeof( f32 ) );
static_assert( std::is_standard_layout_v<angle_t> );
static_assert( std::is_trivially_copyable_v<angle_t> );

} // namespace cypher::math

#endif // CYPHER_COMMON_MATH_ANGLE_H
