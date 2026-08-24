//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Matrix4.inl
//  Purpose: Implements constexpr Matrix4 operations.
//  Details: Storage is column-major, vectors are columns, and matrix products are
//           written in the same composition order used by renderer APIs.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Matrix4 Template Definitions

Operations follow CypherMath coordinate, storage, and multiplication conventions. Inputs may
alias only where documented, and normalization handles degenerate values explicitly. Template
definitions remain visible at the call site so each concrete instantiation can be compiled
without a separate registration step.
================
*/

#ifndef CYPHER_COMMON_MATH_MATRIX4_INL
#define CYPHER_COMMON_MATH_MATRIX4_INL

#ifndef CYPHER_COMMON_MATH_MATRIX4_H
    #include "CypherMath_Matrix4.h"
#endif

#ifndef PRAGMA_ONCE
    #pragma once
#endif

namespace cypher::math
{

// Column-major indexing is shared with GPU upload and projection helpers.
constexpr u32 Mat4_Index( u32 row, u32 column ) noexcept
{
    return column * 4u + row;
}

constexpr mat4_t Mat4_FromColumns(
    vec4_t column0,
    vec4_t column1,
    vec4_t column2,
    vec4_t column3 ) noexcept
{
    return { {
        column0.x, column0.y, column0.z, column0.w,
        column1.x, column1.y, column1.z, column1.w,
        column2.x, column2.y, column2.z, column2.w,
        column3.x, column3.y, column3.z, column3.w
    } };
}

constexpr mat4_t Mat4_FromRows(
    vec4_t row0,
    vec4_t row1,
    vec4_t row2,
    vec4_t row3 ) noexcept
{
    return Mat4_FromColumns(
        Vec4_Make( row0.x, row1.x, row2.x, row3.x ),
        Vec4_Make( row0.y, row1.y, row2.y, row3.y ),
        Vec4_Make( row0.z, row1.z, row2.z, row3.z ),
        Vec4_Make( row0.w, row1.w, row2.w, row3.w ) );
}

constexpr vec4_t Mat4_Column( mat4_t value, u32 column ) noexcept
{
    return Vec4_Make(
        value.m[Mat4_Index( 0u, column )],
        value.m[Mat4_Index( 1u, column )],
        value.m[Mat4_Index( 2u, column )],
        value.m[Mat4_Index( 3u, column )] );
}

constexpr vec4_t Mat4_Row( mat4_t value, u32 row ) noexcept
{
    return Vec4_Make(
        value.m[Mat4_Index( row, 0u )],
        value.m[Mat4_Index( row, 1u )],
        value.m[Mat4_Index( row, 2u )],
        value.m[Mat4_Index( row, 3u )] );
}

constexpr mat4_t Mat4_Add( mat4_t a, mat4_t b ) noexcept
{
    mat4_t result{};
    for ( u32 i = 0u; i < 16u; ++i ) {
        result.m[i] = a.m[i] + b.m[i];
    }
    return result;
}

constexpr mat4_t Mat4_Subtract( mat4_t a, mat4_t b ) noexcept
{
    mat4_t result{};
    for ( u32 i = 0u; i < 16u; ++i ) {
        result.m[i] = a.m[i] - b.m[i];
    }
    return result;
}

constexpr mat4_t Mat4_Scale( mat4_t value, f32 scale ) noexcept
{
    mat4_t result{};
    for ( u32 i = 0u; i < 16u; ++i ) {
        result.m[i] = value.m[i] * scale;
    }
    return result;
}

constexpr mat4_t Mat4_Transpose( mat4_t value ) noexcept
{
    return Mat4_FromRows(
        Mat4_Column( value, 0u ),
        Mat4_Column( value, 1u ),
        Mat4_Column( value, 2u ),
        Mat4_Column( value, 3u ) );
}

constexpr vec4_t Mat4_TransformVector4( mat4_t matrix, vec4_t vector ) noexcept
{
    return Vec4_Add(
        Vec4_Add(
            Vec4_Scale( Mat4_Column( matrix, 0u ), vector.x ),
            Vec4_Scale( Mat4_Column( matrix, 1u ), vector.y ) ),
        Vec4_Add(
            Vec4_Scale( Mat4_Column( matrix, 2u ), vector.z ),
            Vec4_Scale( Mat4_Column( matrix, 3u ), vector.w ) ) );
}

constexpr mat4_t Mat4_Multiply( mat4_t a, mat4_t b ) noexcept
{
    // Transform each column of b by a; under column vectors b acts first.
    return Mat4_FromColumns(
        Mat4_TransformVector4( a, Mat4_Column( b, 0u ) ),
        Mat4_TransformVector4( a, Mat4_Column( b, 1u ) ),
        Mat4_TransformVector4( a, Mat4_Column( b, 2u ) ),
        Mat4_TransformVector4( a, Mat4_Column( b, 3u ) ) );
}

constexpr vec3_t Mat4_TransformPointAffine( mat4_t matrix, vec3_t point ) noexcept
{
    // Affine points use w=1, so translation contributes to the result.
    return Vec4_XYZ( Mat4_TransformVector4( matrix, Vec4_FromVec3( point, 1.0f ) ) );
}

constexpr vec3_t Mat4_TransformDirection( mat4_t matrix, vec3_t direction ) noexcept
{
    return Vec4_XYZ( Mat4_TransformVector4( matrix, Vec4_FromVec3( direction, 0.0f ) ) );
}

constexpr mat4_t Mat4_FromTranslation( vec3_t translation ) noexcept
{
    mat4_t result = CY_MAT4_IDENTITY;
    result.m[Mat4_Index( 0u, 3u )] = translation.x;
    result.m[Mat4_Index( 1u, 3u )] = translation.y;
    result.m[Mat4_Index( 2u, 3u )] = translation.z;
    return result;
}

constexpr mat4_t Mat4_FromScale( vec3_t scale ) noexcept
{
    mat4_t result = CY_MAT4_IDENTITY;
    result.m[Mat4_Index( 0u, 0u )] = scale.x;
    result.m[Mat4_Index( 1u, 1u )] = scale.y;
    result.m[Mat4_Index( 2u, 2u )] = scale.z;
    return result;
}

constexpr vec3_t Mat4_Translation( mat4_t value ) noexcept
{
    return Vec3_Make(
        value.m[Mat4_Index( 0u, 3u )],
        value.m[Mat4_Index( 1u, 3u )],
        value.m[Mat4_Index( 2u, 3u )] );
}

constexpr mat3_t Mat4_LinearPart( mat4_t value ) noexcept
{
    return Mat3_FromColumns(
        Vec4_XYZ( Mat4_Column( value, 0u ) ),
        Vec4_XYZ( Mat4_Column( value, 1u ) ),
        Vec4_XYZ( Mat4_Column( value, 2u ) ) );
}

} // namespace cypher::math

#endif // CYPHER_COMMON_MATH_MATRIX4_INL
