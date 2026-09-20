//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Bounds.inl
//  Purpose: Implements constexpr AABB predicates and construction.
//  Details: Inclusive comparisons make touching boxes overlap and allow boundary
//           points to remain contained, matching broad-phase expectations.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Bounds Template Definitions

Geometry queries keep boundary policy explicit: hit ranges, parallel tolerances, and
inside/outside tests are returned as data rather than inferred from global state. Template
definitions remain visible at the call site so each concrete instantiation can be compiled
without a separate registration step.
================
*/

#ifndef CYPHER_COMMON_MATH_BOUNDS_INL
#define CYPHER_COMMON_MATH_BOUNDS_INL

#ifndef CYPHER_COMMON_MATH_BOUNDS_H
    #include "CypherMath_Bounds.h"
#endif

#ifndef PRAGMA_ONCE
    #pragma once
#endif

namespace cypher::math
{

// Bounds predicates are inclusive: touching points and faces remain contained.
constexpr aabb_t Aabb_Make( vec3_t minimum, vec3_t maximum ) noexcept
{
    return { minimum, maximum };
}

constexpr aabb_t Aabb_FromPoint( vec3_t point ) noexcept
{
    return Aabb_Make( point, point );
}

constexpr bool_t Aabb_IsEmpty( aabb_t bounds ) noexcept
{
    // One reversed axis is enough to represent an empty Cartesian product.
    return bounds.minimum.x > bounds.maximum.x ||
           bounds.minimum.y > bounds.maximum.y ||
           bounds.minimum.z > bounds.maximum.z;
}

constexpr bool_t Aabb_ContainsPoint( aabb_t bounds, vec3_t point ) noexcept
{
    return !Aabb_IsEmpty( bounds ) &&
           point.x >= bounds.minimum.x && point.x <= bounds.maximum.x &&
           point.y >= bounds.minimum.y && point.y <= bounds.maximum.y &&
           point.z >= bounds.minimum.z && point.z <= bounds.maximum.z;
}

constexpr bool_t Aabb_ContainsAabb( aabb_t outer, aabb_t inner ) noexcept
{
    return !Aabb_IsEmpty( outer ) && !Aabb_IsEmpty( inner ) &&
           inner.minimum.x >= outer.minimum.x &&
           inner.minimum.y >= outer.minimum.y &&
           inner.minimum.z >= outer.minimum.z &&
           inner.maximum.x <= outer.maximum.x &&
           inner.maximum.y <= outer.maximum.y &&
           inner.maximum.z <= outer.maximum.z;
}

constexpr bool_t Aabb_Overlaps( aabb_t a, aabb_t b ) noexcept
{
    return !Aabb_IsEmpty( a ) && !Aabb_IsEmpty( b ) &&
           a.minimum.x <= b.maximum.x && a.maximum.x >= b.minimum.x &&
           a.minimum.y <= b.maximum.y && a.maximum.y >= b.minimum.y &&
           a.minimum.z <= b.maximum.z && a.maximum.z >= b.minimum.z;
}

constexpr aabbd_t Aabbd_Make( vec3d_t minimum, vec3d_t maximum ) noexcept
{
    return { minimum, maximum };
}

constexpr aabbd_t Aabbd_FromPoint( vec3d_t point ) noexcept
{
    return Aabbd_Make( point, point );
}

constexpr bool_t Aabbd_IsEmpty( aabbd_t bounds ) noexcept
{
    return bounds.minimum.x > bounds.maximum.x ||
           bounds.minimum.y > bounds.maximum.y ||
           bounds.minimum.z > bounds.maximum.z;
}

constexpr bool_t Aabbd_ContainsPoint( aabbd_t bounds, vec3d_t point ) noexcept
{
    return !Aabbd_IsEmpty( bounds ) &&
           point.x >= bounds.minimum.x && point.x <= bounds.maximum.x &&
           point.y >= bounds.minimum.y && point.y <= bounds.maximum.y &&
           point.z >= bounds.minimum.z && point.z <= bounds.maximum.z;
}

constexpr bool_t Aabbd_ContainsAabb( aabbd_t outer, aabbd_t inner ) noexcept
{
    return !Aabbd_IsEmpty( outer ) && !Aabbd_IsEmpty( inner ) &&
           inner.minimum.x >= outer.minimum.x &&
           inner.minimum.y >= outer.minimum.y &&
           inner.minimum.z >= outer.minimum.z &&
           inner.maximum.x <= outer.maximum.x &&
           inner.maximum.y <= outer.maximum.y &&
           inner.maximum.z <= outer.maximum.z;
}

constexpr bool_t Aabbd_Overlaps( aabbd_t a, aabbd_t b ) noexcept
{
    return !Aabbd_IsEmpty( a ) && !Aabbd_IsEmpty( b ) &&
           a.minimum.x <= b.maximum.x && a.maximum.x >= b.minimum.x &&
           a.minimum.y <= b.maximum.y && a.maximum.y >= b.minimum.y &&
           a.minimum.z <= b.maximum.z && a.maximum.z >= b.minimum.z;
}

constexpr aabbd_t Aabbd_FromAabb( aabb_t value ) noexcept
{
    return Aabbd_Make(
        Vec3d_FromVec3( value.minimum ), Vec3d_FromVec3( value.maximum ) );
}

} // namespace cypher::math

#endif // CYPHER_COMMON_MATH_BOUNDS_INL
