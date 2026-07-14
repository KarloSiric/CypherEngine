//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherMath/CypherMath_Mat.h
//  Purpose: Declares the CypherMath Math Mat module.
//  Details: This file participates in math primitives used by rendering, physics,
//           world queries, and tools. Keep operations deterministic and benchmark
//           important hot paths before adding clever optimizations.
//
//  History:
//  - Created by Karlo Siric on 2026-06-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_MATH_MAT_H
#define CYPHER_ENGINE_MATH_MAT_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMath_Types.h"

namespace cypher::engine::math
{

/*
================
Matrix Convention

Column-major storage, column vectors, OpenGL-style transform order.

Element index:
    m[column * 4 + row]

Transform order:
    clip = projection * view * model * local_position

World convention:
    +X right
    +Y up
    -Z forward
================
*/

constexpr inline common::u32 CypherMath_Mat4Index( const common::u32 column, const common::u32 row ) {
    return column * 4u + row;
}

constexpr inline common::f32 CypherMath_Mat4Get( const mat4_t &m, const common::u32 column, const common::u32 row ) {
    return m.m[CypherMath_Mat4Index( column, row )];
}

constexpr inline void CypherMath_Mat4Set( mat4_t &m, const common::u32 column, const common::u32 row, const common::f32 value ) {
    m.m[CypherMath_Mat4Index( column, row )] = value;
}

mat4_t CypherMath_Mat4Zero();

mat4_t CypherMath_Mat4Identity();

mat4_t CypherMath_Mat4Multiply( const mat4_t &m1, const mat4_t &m2 );

mat4_t CypherMath_Mat4Translation( const vec3_t &translationVector );

mat4_t CypherMath_Mat4Scale( const vec3_t &scale );

mat4_t CypherMath_Mat4RotateX( const common::f32 radians );

mat4_t CypherMath_Mat4RotateY( const common::f32 radians );

mat4_t CypherMath_Mat4RotateZ( const common::f32 radians );

mat4_t CypherMath_Mat4RotateAxis( const vec3_t &axis, const common::f32 radians );

mat4_t CypherMath_Mat4Transpose( const mat4_t &m );

vec4_t CypherMath_Mat4MulVec4( const mat4_t &m, const vec4_t &v );

vec3_t CypherMath_Mat4TransformPoint( const mat4_t &m, const vec3_t &v );

vec3_t CypherMath_Mat4TransformDirection( const mat4_t &m, const vec3_t &v );

mat4_t CypherMath_Mat4Perspective( common::f32 fovYRadians, common::f32 aspectRatio, common::f32 nearZ, common::f32 farZ );

mat4_t CypherMath_Mat4Ortho( const common::f32 left, const common::f32 right, const common::f32 bottom, const common::f32 top, const common::f32 nearZ, const common::f32 farZ );

mat4_t CypherMath_Mat4LookAt( const vec3_t &eye, const vec3_t &target, const vec3_t &up );

mat4_t CypherMath_Mat4TranslationRotationScale( const vec3_t &position, const quat_t &orientation, const vec3_t &scale );

}       // namespace cypher::engine::math

#endif // CYPHER_ENGINE_MATH_MAT_H
