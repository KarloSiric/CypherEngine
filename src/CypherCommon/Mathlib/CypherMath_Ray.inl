//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Ray.inl
//  Purpose: Implements constexpr ray and segment operations.
//  Details: Parameter evaluation and affine transformation are direct vector
//           operations and do not impose normalization policy.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Ray Template Definitions

Geometry queries keep boundary policy explicit: hit ranges, parallel tolerances, and
inside/outside tests are returned as data rather than inferred from global state. Template
definitions remain visible at the call site so each concrete instantiation can be compiled
without a separate registration step.
================
*/

#ifndef CYPHER_COMMON_MATH_RAY_INL
#define CYPHER_COMMON_MATH_RAY_INL

#ifndef CYPHER_COMMON_MATH_RAY_H
    #include "CypherMath_Ray.h"
#endif

#ifndef PRAGMA_ONCE
    #pragma once
#endif

namespace cypher::math
{

// Ray t uses the supplied direction scale; segment t is always normalized [0, 1].
constexpr ray_t Ray_Make( vec3_t origin, vec3_t direction ) noexcept
{
    return { origin, direction };
}

constexpr segment_t Segment_Make( vec3_t start, vec3_t end ) noexcept
{
    return { start, end };
}

constexpr vec3_t Ray_PointAt( ray_t ray, f32 t ) noexcept
{
    return Vec3_MulAdd( ray.origin, ray.direction, t );
}

constexpr vec3_t Segment_Direction( segment_t segment ) noexcept
{
    return Vec3_Subtract( segment.end, segment.start );
}

constexpr vec3_t Segment_PointAt( segment_t segment, f32 t ) noexcept
{
    return Vec3_Lerp( segment.start, segment.end, t );
}

constexpr f32 Segment_LengthSquared( segment_t segment ) noexcept
{
    return Vec3_LengthSquared( Segment_Direction( segment ) );
}

constexpr ray_t Ray_TransformAffine(
    ray_t ray,
    affine3_t transform ) noexcept
{
    // Origins transform as points while directions intentionally omit translation.
    return Ray_Make(
        Affine3_TransformPoint( transform, ray.origin ),
        Affine3_TransformDirection( transform, ray.direction ) );
}

constexpr segment_t Segment_TransformAffine(
    segment_t segment,
    affine3_t transform ) noexcept
{
    return Segment_Make(
        Affine3_TransformPoint( transform, segment.start ),
        Affine3_TransformPoint( transform, segment.end ) );
}

} // namespace cypher::math

#endif // CYPHER_COMMON_MATH_RAY_INL
