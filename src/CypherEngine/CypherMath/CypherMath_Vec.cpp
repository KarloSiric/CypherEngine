//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherMath/CypherMath_Vec.cpp
//  Purpose: Implements the CypherMath Math Vec module.
//  Details: This file participates in math primitives used by rendering, physics,
//           world queries, and tools. Keep operations deterministic and benchmark
//           important hot paths before adding clever optimizations.
//
//  History:
//  - Created by Karlo Siric on 2026-05-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMath_Vec.h"

#include <cmath>    // std::sqrt / std::fabs

namespace cypher::engine::math
{

/*
==================
Vec2
==================
*/
common::f32 CypherMath_Vec2Length( const vec2_t &v ) {
    return std::sqrt( CypherMath_Vec2Dot( v, v ) );
}

common::f32 CypherMath_Vec2Distance( const vec2_t &v1, const vec2_t &v2 ) {
    return CypherMath_Vec2Length( CypherMath_Vec2Sub( v1, v2 ) );
}

vec2_t CypherMath_Vec2Normalize(const vec2_t &v ) {
    const common::f32 nVecLen = CypherMath_Vec2Length( v );

    // @NOTE: In case the length of the vector is very very small we count it as 0.
    if ( nVecLen <= MATH_EPSILON_F ) {
        return vec2_t{};
    }

    return CypherMath_Vec2Scale( v, 1.0f / nVecLen );
}

common::f32 CypherMath_Vec2NormalizeLength( vec2_t &v ) {
    const common::f32 nVecLen = CypherMath_Vec2Length( v );

    if ( nVecLen <= MATH_EPSILON_F ) {
        v = vec2_t{};
        return 0.0f;
    }

    v = CypherMath_Vec2Scale( v, 1.0f / nVecLen );
    return nVecLen;
}

bool CypherMath_Vec2Near( const vec2_t &v1, const vec2_t &v2, common::f32 epsilon ) {
    return ( std::fabs( v1.x - v2.x ) <= epsilon &&
             std::fabs( v1.y - v2.y ) <= epsilon );
}

vec2_t CypherMath_Vec2Lerp( const vec2_t &v1, const vec2_t &v2, common::f32 t ) {

    return vec2_t{
        v1.x + ( v2.x - v1.x ) * t,
        v1.y + ( v2.y - v1.y ) * t
    };
}

vec2_t CypherMath_Vec2Min( const vec2_t &v1, const vec2_t &v2 ) {
    return vec2_t{
        ( v1.x < v2.x ) ? v1.x : v2.x,
        ( v1.y < v2.y ) ? v1.y : v2.y
    };
}

vec2_t CypherMath_Vec2Max( const vec2_t &v1, const vec2_t &v2 ) {
    return vec2_t{
        ( v1.x > v2.x ) ? v1.x : v2.x,
        ( v1.y > v2.y ) ? v1.y : v2.y
    };
}

/*
==================
Vec3
==================
*/
vec3_t CypherMath_Vec3Cross( const vec3_t &v1, const vec3_t &v2 )
{
    return vec3_t{
        v1.y * v2.z - v1.z * v2.y,
        v1.z * v2.x - v1.x * v2.z,
        v1.x * v2.y - v1.y * v2.x
    };
}

common::f32 CypherMath_Vec3Length( const vec3_t &v )
{
    return std::sqrt( CypherMath_Vec3Dot( v, v ) );
}

common::f32 CypherMath_Vec3Distance( const vec3_t &v1, const vec3_t &v2 ) {
    return CypherMath_Vec3Length( CypherMath_Vec3Sub( v1, v2 ) );
}



vec3_t CypherMath_Vec3Normalize( const vec3_t &v ) {
    const common::f32 nVecLen = CypherMath_Vec3Length( v );

    if ( nVecLen <= MATH_EPSILON_F ) {
        return vec3_t{};
    }

    return CypherMath_Vec3Scale( v, 1.0f / nVecLen );
}

common::f32 CypherMath_Vec3NormalizeLength( vec3_t &v ) {
    const common::f32 nVecLen = CypherMath_Vec3Length( v );

    if ( nVecLen <= MATH_EPSILON_F ) {
        v = vec3_t{};
        return 0.0f;
    }

    v = CypherMath_Vec3Scale( v, ( 1.0f / nVecLen ) );

    return nVecLen;
}

bool CypherMath_Vec3Near( const vec3_t &v1, const vec3_t &v2, common::f32 epsilon ) {
    return ( std::fabs( v1.x - v2.x ) <= epsilon &&
             std::fabs( v1.y - v2.y ) <= epsilon &&
             std::fabs( v1.z - v2.z ) <= epsilon );

}

vec3_t CypherMath_Vec3Lerp( const vec3_t &v1, const vec3_t &v2, common::f32 t ) {
    return vec3_t{
        v1.x + ( v2.x - v1.x ) * t,
        v1.y + ( v2.y - v1.y ) * t,
        v1.z + ( v2.z - v1.z ) * t
    };
}

vec3_t CypherMath_Vec3Min( const vec3_t &v1, const vec3_t &v2 ) {
    return vec3_t{
        ( v1.x < v2.x ) ? v1.x : v2.x,
        ( v1.y < v2.y ) ? v1.y : v2.y,
        ( v1.z < v2.z ) ? v1.z : v2.z
    };
}

vec3_t CypherMath_Vec3Max( const vec3_t &v1, const vec3_t &v2 ) {
    return vec3_t{
        ( v1.x > v2.x ) ? v1.x : v2.x,
        ( v1.y > v2.y ) ? v1.y : v2.y,
        ( v1.z > v2.z ) ? v1.z : v2.z
    };
}

vec3_t CypherMath_Vec3Reflect( const vec3_t &v, const vec3_t &normal ) {
    common::f32 reflectDot = CypherMath_Vec3Dot( v, normal );
    return CypherMath_Vec3Sub( v, CypherMath_Vec3Scale( normal, reflectDot * 2.0f ) );
}

vec3_t CypherMath_Vec3Project( const vec3_t &v, const vec3_t &onto ) {
    common::f32 normOnto = CypherMath_Vec3LengthSquared( onto );
    if ( normOnto <= MATH_EPSILON_F ) {
        return vec3_t{};
    }

    common::f32 scale = CypherMath_Vec3Dot( v, onto ) / normOnto;
    return CypherMath_Vec3Scale( onto, scale );
}

vec3_t CypherMath_Vec3Reject( const vec3_t &v, const vec3_t &from ) {
    return CypherMath_Vec3Sub( v, CypherMath_Vec3Project( v, from ) );
}

/*
==================
Vec4
==================
*/
common::f32 CypherMath_Vec4Length( const vec4_t &v ) {
    return std::sqrt( CypherMath_Vec4Dot( v, v ) );
}

vec4_t CypherMath_Vec4Normalize( const vec4_t &v ) {
    const common::f32 nVecLen = CypherMath_Vec4Length( v );

    if ( nVecLen <= MATH_EPSILON_F ) {
        return vec4_t{};
    }
    return CypherMath_Vec4Scale( v, 1.0f / nVecLen );
}

bool CypherMath_Vec4Near( const vec4_t &v1, const vec4_t &v2, common::f32 epsilon ) {
    return ( std::fabs( v1.x - v2.x ) <= epsilon &&
             std::fabs( v1.y - v2.y ) <= epsilon &&
             std::fabs( v1.z - v2.z ) <= epsilon &&
             std::fabs( v1.w - v2.w ) <= epsilon
             );
}

vec4_t CypherMath_Vec4Lerp( const vec4_t &v1, const vec4_t &v2, common::f32 t ) {
    return vec4_t{
        v1.x + ( v2.x - v1.x ) * t,
        v1.y + ( v2.y - v1.y ) * t,
        v1.z + ( v2.z - v1.z ) * t,
        v1.w + ( v2.w - v1.w ) * t
    };
}

vec4_t CypherMath_Vec4Min( const vec4_t &v1, const vec4_t &v2 ) {
    return vec4_t{
        ( v1.x < v2.x ) ? v1.x : v2.x,
        ( v1.y < v2.y ) ? v1.y : v2.y,
        ( v1.z < v2.z ) ? v1.z : v2.z,
        ( v1.w < v2.w ) ? v1.w : v2.w
    };
}

vec4_t CypherMath_Vec4Max( const vec4_t &v1, const vec4_t &v2 ) {
    return vec4_t{
        ( v1.x > v2.x ) ? v1.x : v2.x,
        ( v1.y > v2.y ) ? v1.y : v2.y,
        ( v1.z > v2.z ) ? v1.z : v2.z,
        ( v1.w > v2.w ) ? v1.w : v2.w
    };
}

}       // namespace cypher::engine::math
