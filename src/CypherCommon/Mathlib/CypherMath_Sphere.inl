//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Sphere.inl
//  Purpose: Implements constexpr sphere predicates.
//  Details: Squared-distance comparisons avoid square roots for containment and
//           overlap tests while preserving inclusive boundary behavior.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Sphere Template Definitions

Geometry queries keep boundary policy explicit: hit ranges, parallel tolerances, and
inside/outside tests are returned as data rather than inferred from global state. Template
definitions remain visible at the call site so each concrete instantiation can be compiled
without a separate registration step.
================
*/

#ifndef CYPHER_COMMON_MATH_SPHERE_INL
#define CYPHER_COMMON_MATH_SPHERE_INL

#ifndef CYPHER_COMMON_MATH_SPHERE_H
    #include "CypherMath_Sphere.h"
#endif

#ifndef PRAGMA_ONCE
    #pragma once
#endif

namespace cypher::math
{

// Squared-distance tests keep these broad-phase predicates free of square roots.
constexpr sphere_t Sphere_Make( vec3_t center, f32 radius ) noexcept
{
    return { center, radius };
}

constexpr bool_t Sphere_ContainsPoint( sphere_t sphere, vec3_t point ) noexcept
{
    return Vec3_DistanceSquared( sphere.center, point ) <=
           sphere.radius * sphere.radius;
}

constexpr bool_t Sphere_ContainsSphere( sphere_t outer, sphere_t inner ) noexcept
{
    // The inner center may move only by the radius left inside the outer sphere.
    const f32 remainingRadius = outer.radius - inner.radius;
    return remainingRadius >= 0.0f &&
           Vec3_DistanceSquared( outer.center, inner.center ) <=
               remainingRadius * remainingRadius;
}

constexpr bool_t Sphere_Overlaps( sphere_t a, sphere_t b ) noexcept
{
    const f32 combinedRadius = a.radius + b.radius;
    return Vec3_DistanceSquared( a.center, b.center ) <=
           combinedRadius * combinedRadius;
}

} // namespace cypher::math

#endif // CYPHER_COMMON_MATH_SPHERE_INL
