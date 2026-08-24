//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Intersection.inl
//  Purpose: Implements direct primitive overlap aliases.
//  Details: More involved ray and frustum queries remain out of line to keep
//           tolerance validation and numerical branches centralized.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Intersection Template Definitions

Geometry queries keep boundary policy explicit: hit ranges, parallel tolerances, and
inside/outside tests are returned as data rather than inferred from global state. Template
definitions remain visible at the call site so each concrete instantiation can be compiled
without a separate registration step.
================
*/

#ifndef CYPHER_COMMON_MATH_INTERSECTION_INL
#define CYPHER_COMMON_MATH_INTERSECTION_INL

#ifndef CYPHER_COMMON_MATH_INTERSECTION_H
    #include "CypherMath_Intersection.h"
#endif

#ifndef PRAGMA_ONCE
    #pragma once
#endif

namespace cypher::math
{

// Primitive aliases retain the inclusive touching behavior of their source APIs.
constexpr bool_t Intersection_AabbAabb( aabb_t a, aabb_t b ) noexcept
{
    return Aabb_Overlaps( a, b );
}

constexpr bool_t Intersection_SphereSphere( sphere_t a, sphere_t b ) noexcept
{
    return Sphere_Overlaps( a, b );
}

} // namespace cypher::math

#endif // CYPHER_COMMON_MATH_INTERSECTION_INL
