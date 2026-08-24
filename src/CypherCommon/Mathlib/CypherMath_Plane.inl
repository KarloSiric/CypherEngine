//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Plane.inl
//  Purpose: Implements constexpr plane operations.
//  Details: Tiny equation evaluations remain inline while checked construction
//           and affine transformation remain in the implementation unit.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Plane Template Definitions

Geometry queries keep boundary policy explicit: hit ranges, parallel tolerances, and
inside/outside tests are returned as data rather than inferred from global state. Template
definitions remain visible at the call site so each concrete instantiation can be compiled
without a separate registration step.
================
*/

#ifndef CYPHER_COMMON_MATH_PLANE_INL
#define CYPHER_COMMON_MATH_PLANE_INL

#ifndef CYPHER_COMMON_MATH_PLANE_H
    #include "CypherMath_Plane.h"
#endif

#ifndef PRAGMA_ONCE
    #pragma once
#endif

namespace cypher::math
{

// Direct equation helpers do not normalize; callers choose that cost explicitly.
constexpr plane_t Plane_Make( vec3_t normal, f32 d ) noexcept
{
    return { normal, d };
}

constexpr plane_t Plane_Flip( plane_t value ) noexcept
{
    return Plane_Make( Vec3_Negate( value.normal ), -value.d );
}

constexpr f32 Plane_SignedDistance( plane_t plane, vec3_t point ) noexcept
{
    return Vec3_Dot( plane.normal, point ) + plane.d;
}

constexpr vec3_t Plane_ProjectPointUnit(
    plane_t unitPlane,
    vec3_t point ) noexcept
{
    // Subtract the signed metric distance along the unit normal.
    return Vec3_Subtract(
        point,
        Vec3_Scale(
            unitPlane.normal,
            Plane_SignedDistance( unitPlane, point ) ) );
}

} // namespace cypher::math

#endif // CYPHER_COMMON_MATH_PLANE_INL
