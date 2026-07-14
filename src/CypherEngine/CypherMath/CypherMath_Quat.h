//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherMath/CypherMath_Quat.h
//  Purpose: Declares the CypherMath Math Quat module.
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

#ifndef CYPHER_ENGINE_MATH_QUAT_H
#define CYPHER_ENGINE_MATH_QUAT_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMath_Types.h"

namespace cypher::engine::math
{

quat_t CypherMath_QuatIdentity();

quat_t CypherMath_QuatMake( common::f32 x, common::f32 y, common::f32 z, common::f32 w );

common::f32 CypherMath_QuatDot( const quat_t &q1, const quat_t &q2 );

quat_t CypherMath_QuatNormalize( const quat_t &q );

quat_t CypherMath_QuatConjugate( const quat_t &q );

quat_t CypherMath_QuatInverse( const quat_t &q );

quat_t CypherMath_QuatMultiply( const quat_t &q1, const quat_t &q2 );

quat_t CypherMath_QuatFromAxis( const vec3_t &axis, const common::f32 radians );

quat_t CypherMath_QuatFromEuler( const common::f32 pitch, const common::f32 yaw, const common::f32 roll );

vec3_t CypherMath_QuatForwardVec3( const quat_t &q );

vec3_t CypherMath_QuatRightVec3( const quat_t &q );

vec3_t CypherMath_QuatUpVec3( const quat_t &q );

mat4_t CypherMath_QuatToMat4( const quat_t &q );

vec3_t CypherMath_QuatRotateVec3( const quat_t &q, const vec3_t &v );

quat_t CypherMath_QuatNLerp( const quat_t &q1, const quat_t &q2, const common::f32 t );

quat_t CypherMath_QuatSlerp( const quat_t &q1, const quat_t &q2, const common::f32 t );

}       // namespace cypher::engine::math

#endif // CYPHER_ENGINE_MATH_QUAT_H
