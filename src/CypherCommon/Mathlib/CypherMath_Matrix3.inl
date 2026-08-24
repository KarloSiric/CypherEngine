//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Matrix3.inl
//  Purpose: Implements constexpr Matrix3 operations.
//  Details: Storage is column-major and multiplication follows column-vector order.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Matrix3 Template Definitions

Operations follow CypherMath coordinate, storage, and multiplication conventions. Inputs may
alias only where documented, and normalization handles degenerate values explicitly. Template
definitions remain visible at the call site so each concrete instantiation can be compiled
without a separate registration step.
================
*/

#ifndef CYPHER_COMMON_MATH_MATRIX3_INL
#define CYPHER_COMMON_MATH_MATRIX3_INL

#ifndef CYPHER_COMMON_MATH_MATRIX3_H
    #include "CypherMath_Matrix3.h"
#endif

#ifndef PRAGMA_ONCE
    #pragma once
#endif

namespace cypher::math
{

// Column-major indexing matches Matrix4 and renderer-facing storage.
constexpr u32 Mat3_Index( u32 row, u32 column ) noexcept
{
    return column * 3u + row;
}

constexpr mat3_t Mat3_FromColumns(
    vec3_t column0,
    vec3_t column1,
    vec3_t column2 ) noexcept
{
    return { {
        column0.x, column0.y, column0.z,
        column1.x, column1.y, column1.z,
        column2.x, column2.y, column2.z
    } };
}

constexpr mat3_t Mat3_FromRows( vec3_t row0, vec3_t row1, vec3_t row2 ) noexcept
{
    return Mat3_FromColumns(
        Vec3_Make( row0.x, row1.x, row2.x ),
        Vec3_Make( row0.y, row1.y, row2.y ),
        Vec3_Make( row0.z, row1.z, row2.z ) );
}

constexpr vec3_t Mat3_Column( mat3_t value, u32 column ) noexcept
{
    return Vec3_Make(
        value.m[Mat3_Index( 0u, column )],
        value.m[Mat3_Index( 1u, column )],
        value.m[Mat3_Index( 2u, column )] );
}

constexpr vec3_t Mat3_Row( mat3_t value, u32 row ) noexcept
{
    return Vec3_Make(
        value.m[Mat3_Index( row, 0u )],
        value.m[Mat3_Index( row, 1u )],
        value.m[Mat3_Index( row, 2u )] );
}

constexpr mat3_t Mat3_Add( mat3_t a, mat3_t b ) noexcept
{
    mat3_t result{};
    for ( u32 i = 0u; i < 9u; ++i ) {
        result.m[i] = a.m[i] + b.m[i];
    }
    return result;
}

constexpr mat3_t Mat3_Subtract( mat3_t a, mat3_t b ) noexcept
{
    mat3_t result{};
    for ( u32 i = 0u; i < 9u; ++i ) {
        result.m[i] = a.m[i] - b.m[i];
    }
    return result;
}

constexpr mat3_t Mat3_Scale( mat3_t value, f32 scale ) noexcept
{
    mat3_t result{};
    for ( u32 i = 0u; i < 9u; ++i ) {
        result.m[i] = value.m[i] * scale;
    }
    return result;
}

constexpr mat3_t Mat3_Transpose( mat3_t value ) noexcept
{
    return Mat3_FromRows(
        Mat3_Column( value, 0u ),
        Mat3_Column( value, 1u ),
        Mat3_Column( value, 2u ) );
}

constexpr vec3_t Mat3_TransformVector( mat3_t matrix, vec3_t vector ) noexcept
{
    return Vec3_Add(
        Vec3_Add(
            Vec3_Scale( Mat3_Column( matrix, 0u ), vector.x ),
            Vec3_Scale( Mat3_Column( matrix, 1u ), vector.y ) ),
        Vec3_Scale( Mat3_Column( matrix, 2u ), vector.z ) );
}

constexpr mat3_t Mat3_Multiply( mat3_t a, mat3_t b ) noexcept
{
    // Transform each column of b by a; this applies b before a.
    return Mat3_FromColumns(
        Mat3_TransformVector( a, Mat3_Column( b, 0u ) ),
        Mat3_TransformVector( a, Mat3_Column( b, 1u ) ),
        Mat3_TransformVector( a, Mat3_Column( b, 2u ) ) );
}

constexpr f32 Mat3_Determinant( mat3_t value ) noexcept
{
    return Vec3_Dot(
        Mat3_Column( value, 0u ),
        Vec3_Cross( Mat3_Column( value, 1u ), Mat3_Column( value, 2u ) ) );
}

constexpr mat3_t Mat3_FromScale( vec3_t scale ) noexcept
{
    return Mat3_FromColumns(
        Vec3_Make( scale.x, 0.0f, 0.0f ),
        Vec3_Make( 0.0f, scale.y, 0.0f ),
        Vec3_Make( 0.0f, 0.0f, scale.z ) );
}

} // namespace cypher::math

#endif // CYPHER_COMMON_MATH_MATRIX3_INL
