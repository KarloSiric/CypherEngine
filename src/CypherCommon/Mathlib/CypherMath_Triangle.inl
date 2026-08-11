//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Triangle.inl
//  Purpose: Implements constexpr triangle operations.
//  Details: Construction, centroid, winding, interpolation, and transformation
//           are direct operations without tolerance or failure policy.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_MATH_TRIANGLE_INL
#define CYPHER_COMMON_MATH_TRIANGLE_INL

#ifndef CYPHER_COMMON_MATH_TRIANGLE_H
    #include "CypherMath_Triangle.h"
#endif

#ifndef PRAGMA_ONCE
    #pragma once
#endif

namespace cypher::math
{

constexpr triangle3_t Triangle3_Make(
    vec3_t a,
    vec3_t b,
    vec3_t c ) noexcept
{
    return { a, b, c };
}

constexpr vec3_t Triangle3_Centroid( triangle3_t triangle ) noexcept
{
    return Vec3_Scale(
        Vec3_Add( Vec3_Add( triangle.a, triangle.b ), triangle.c ),
        1.0f / 3.0f );
}

constexpr vec3_t Triangle3_NormalUnnormalized(
    triangle3_t triangle ) noexcept
{
    return Vec3_Cross(
        Vec3_Subtract( triangle.b, triangle.a ),
        Vec3_Subtract( triangle.c, triangle.a ) );
}

constexpr vec3_t Triangle3_PointFromBarycentric(
    triangle3_t triangle,
    vec3_t barycentric ) noexcept
{
    return Vec3_Add(
        Vec3_Add(
            Vec3_Scale( triangle.a, barycentric.x ),
            Vec3_Scale( triangle.b, barycentric.y ) ),
        Vec3_Scale( triangle.c, barycentric.z ) );
}

constexpr triangle3_t Triangle3_TransformAffine(
    triangle3_t triangle,
    affine3_t transform ) noexcept
{
    return Triangle3_Make(
        Affine3_TransformPoint( transform, triangle.a ),
        Affine3_TransformPoint( transform, triangle.b ),
        Affine3_TransformPoint( transform, triangle.c ) );
}

} // namespace cypher::math

#endif // CYPHER_COMMON_MATH_TRIANGLE_INL
