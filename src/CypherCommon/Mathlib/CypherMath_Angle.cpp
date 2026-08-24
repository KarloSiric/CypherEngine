//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Angle.cpp
//  Purpose: Implements semantic angle operations.
//  Details: Normalization uses half-open ranges so equivalent turn boundaries
//           always produce one deterministic representation.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Angle Implementation Notes

Angle helpers make degree/radian conversion explicit and normalize only when the called
operation promises it.
================
*/

#include "CypherMath_Angle.h"
#include "CypherCommon_Assert.h"

namespace cypher::math
{

bool_t Angle_IsFinite( angle_t angle ) noexcept
{
    return Scalar_IsFinite( angle.radians );
}

angle_t Angle_NormalizePositive( angle_t angle ) noexcept
{
    // Canonical positive range is [0, 2*pi).
    return Angle_FromRadians( Scalar_WrapRadiansPositive( angle.radians ) );
}

angle_t Angle_NormalizeSigned( angle_t angle ) noexcept
{
    // Canonical signed range is [-pi, pi).
    return Angle_FromRadians( Scalar_WrapRadiansSigned( angle.radians ) );
}

angle_t Angle_ShortestDelta( angle_t from, angle_t to ) noexcept
{
    // Wrapping the raw difference selects the shortest signed turn.
    return Angle_NormalizeSigned( Angle_Subtract( to, from ) );
}

angle_t Angle_LerpShortest( angle_t from, angle_t to, f32 t ) noexcept
{
    return Angle_Add( from, Angle_Scale( Angle_ShortestDelta( from, to ), t ) );
}

bool_t Angle_NearlyEquivalent(
    angle_t a,
    angle_t b,
    f32 toleranceRadians ) noexcept
{
    const bool_t bValidTolerance = Scalar_IsFinite( toleranceRadians ) &&
                                   toleranceRadians >= 0.0f;
    CY_ASSERT_MSG(
        bValidTolerance,
        "Angle_NearlyEquivalent requires a finite nonnegative tolerance." );
    if ( !bValidTolerance || !Angle_IsFinite( a ) || !Angle_IsFinite( b ) ) {
        return false;
    }
    // Compare modulo a full turn rather than comparing stored radians directly.
    return Scalar_Abs( Angle_ShortestDelta( a, b ).radians ) <= toleranceRadians;
}

f32 Angle_Sin( angle_t angle ) noexcept { return Scalar_Sin( angle.radians ); }
f32 Angle_Cos( angle_t angle ) noexcept { return Scalar_Cos( angle.radians ); }
f32 Angle_Tan( angle_t angle ) noexcept { return Scalar_Tan( angle.radians ); }

void Angle_SinCos( angle_t angle, f32 *pSin, f32 *pCos ) noexcept
{
    Scalar_SinCos( angle.radians, pSin, pCos );
}

} // namespace cypher::math
