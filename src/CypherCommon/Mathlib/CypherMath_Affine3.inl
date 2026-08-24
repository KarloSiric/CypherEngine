//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Affine3.inl
//  Purpose: Implements constexpr affine transform operations.
//  Details: The compact storage contains four three-component columns, matching
//           the upper three rows of a column-major four-by-four matrix.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Affine3 Template Definitions

Operations follow CypherMath coordinate, storage, and multiplication conventions. Inputs may
alias only where documented, and normalization handles degenerate values explicitly. Template
definitions remain visible at the call site so each concrete instantiation can be compiled
without a separate registration step.
================
*/

#ifndef CYPHER_COMMON_MATH_AFFINE3_INL
#define CYPHER_COMMON_MATH_AFFINE3_INL

#ifndef CYPHER_COMMON_MATH_AFFINE3_H
    #include "CypherMath_Affine3.h"
#endif

#ifndef PRAGMA_ONCE
    #pragma once
#endif

namespace cypher::math
{

// Storage is four contiguous vec3 columns: basis X/Y/Z followed by translation.
constexpr u32 Affine3_Index( u32 row, u32 column ) noexcept
{
    return column * 3u + row;
}

constexpr affine3_t Affine3_FromColumns(
    vec3_t column0,
    vec3_t column1,
    vec3_t column2,
    vec3_t translation ) noexcept
{
    return { {
        column0.x, column0.y, column0.z,
        column1.x, column1.y, column1.z,
        column2.x, column2.y, column2.z,
        translation.x, translation.y, translation.z
    } };
}

constexpr vec3_t Affine3_Column( affine3_t value, u32 column ) noexcept
{
    return Vec3_Make(
        value.m[Affine3_Index( 0u, column )],
        value.m[Affine3_Index( 1u, column )],
        value.m[Affine3_Index( 2u, column )] );
}

constexpr mat3_t Affine3_LinearPart( affine3_t value ) noexcept
{
    return Mat3_FromColumns(
        Affine3_Column( value, 0u ),
        Affine3_Column( value, 1u ),
        Affine3_Column( value, 2u ) );
}

constexpr vec3_t Affine3_Translation( affine3_t value ) noexcept
{
    return Affine3_Column( value, 3u );
}

constexpr vec3_t Affine3_TransformDirection(
    affine3_t transform,
    vec3_t direction ) noexcept
{
    return Vec3_Add(
        Vec3_Add(
            Vec3_Scale( Affine3_Column( transform, 0u ), direction.x ),
            Vec3_Scale( Affine3_Column( transform, 1u ), direction.y ) ),
        Vec3_Scale( Affine3_Column( transform, 2u ), direction.z ) );
}

constexpr vec3_t Affine3_TransformPoint(
    affine3_t transform,
    vec3_t point ) noexcept
{
    return Vec3_Add(
        Affine3_TransformDirection( transform, point ),
        Affine3_Translation( transform ) );
}

constexpr affine3_t Affine3_Multiply( affine3_t a, affine3_t b ) noexcept
{
    // Basis columns transform as directions; b translation transforms as a point.
    return Affine3_FromColumns(
        Affine3_TransformDirection( a, Affine3_Column( b, 0u ) ),
        Affine3_TransformDirection( a, Affine3_Column( b, 1u ) ),
        Affine3_TransformDirection( a, Affine3_Column( b, 2u ) ),
        Affine3_TransformPoint( a, Affine3_Translation( b ) ) );
}

constexpr affine3_t Affine3_FromTranslation( vec3_t translation ) noexcept
{
    return Affine3_FromColumns(
        CY_VEC3_FORWARD, CY_VEC3_LEFT, CY_VEC3_UP, translation );
}

constexpr affine3_t Affine3_FromScale( vec3_t scale ) noexcept
{
    return Affine3_FromColumns(
        Vec3_Make( scale.x, 0.0f, 0.0f ),
        Vec3_Make( 0.0f, scale.y, 0.0f ),
        Vec3_Make( 0.0f, 0.0f, scale.z ),
        CY_VEC3_ZERO );
}

constexpr mat4_t Affine3_ToMat4( affine3_t value ) noexcept
{
    return Mat4_FromColumns(
        Vec4_FromVec3( Affine3_Column( value, 0u ), 0.0f ),
        Vec4_FromVec3( Affine3_Column( value, 1u ), 0.0f ),
        Vec4_FromVec3( Affine3_Column( value, 2u ), 0.0f ),
        Vec4_FromVec3( Affine3_Translation( value ), 1.0f ) );
}

} // namespace cypher::math

#endif // CYPHER_COMMON_MATH_AFFINE3_INL
