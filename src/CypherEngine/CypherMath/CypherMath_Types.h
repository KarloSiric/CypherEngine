//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherMath/CypherMath_Types.h
//  Purpose: Declares the CypherMath Math Types module.
//  Details: This file participates in math primitives used by rendering, physics,
//           world queries, and tools. Keep operations deterministic and benchmark
//           important hot paths before adding clever optimizations.
//
//  History:
//  - Created by Karlo Siric on 2026-05-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_MATH_TYPES_H
#define CYPHER_ENGINE_MATH_TYPES_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon.h"

namespace cypher::engine::math
{
/*
================
Math Constants

Typed math constants used by renderer, collision, BSP, animation and gameplay.
================
*/
constexpr common::f32 MATH_PI_F        = 3.14159265358979323846f;
constexpr common::f32 MATH_TAU_F       = 6.28318530717958647692f;
constexpr common::f32 MATH_HALF_PI_F   = MATH_PI_F * 0.5f;
constexpr common::f32 MATH_DEG2RAD_F   = MATH_PI_F / 180.0f;
constexpr common::f32 MATH_RAD2DEG_F   = 180.0f / MATH_PI_F;
constexpr common::f32 MATH_EPSILON_F   = 1.0e-6f;

constexpr common::f64 MATH_PI_D        = 3.14159265358979323846264338327950288;
constexpr common::f64 MATH_EPSILON_D   = 1.0e-12;

/*
================
Vector Types

Small POD math types used by renderer, map, collision and gameplay code.
================
*/
struct vec2_t {
    common::f32 x{};
    common::f32 y{};
};

struct vec3_t {
    common::f32 x{};
    common::f32 y{};
    common::f32 z{};
};

struct vec4_t {
    common::f32 x{};
    common::f32 y{};
    common::f32 z{};
    common::f32 w{};
};

/*
================
Matrix Types
================
*/
struct mat4_t {
    common::f32 m[16]{};
};

/*
================
Quaternion Types
================
*/
struct quat_t {
    common::f32 x{};
    common::f32 y{};
    common::f32 z{};
    common::f32 w{ 1.0f };
};

/*
================
Packed Vector Types

Used for file formats, BSP data, compact network data, and other packed storage.
Do not use these for normal runtime movement or rendering math.
================
*/

struct vec3_s_t {
    common::i16 x{};
    common::i16 y{};
    common::i16 z{};
};

/*
================
plane_t / bounds_t

Plane is the base primitive for BSP, frustum and collision work.
Bounds is an axis-aligned box used for visibility, collision and asset limits.
================
*/
struct plane_t {
    vec3_t normal{};
    common::f32 dist{};
};

struct bounds_t {
    vec3_t mins{};
    vec3_t maxs{};
};

/*
===============
ray_t

Raycasting geometrical shape of a line vector, used for various sorts in the engine.
===============
 */
struct ray_t {
    vec3_t origin{};
    vec3_t direction{};
};
/*
===============
frustum_t

Used for rendering API of the engine in order to render only what is necessary part of the map
===============
 */
enum frustum_plane_t {
    FRUSTUM_PLANE_LEFT = 0,
    FRUSTUM_PLANE_RIGHT,
    FRUSTUM_PLANE_BOTTOM,
    FRUSTUM_PLANE_TOP,
    FRUSTUM_PLANE_NEAR,
    FRUSTUM_PLANE_FAR,
    FRUSTUM_PLANE_COUNT
};

struct frustum_t {
    plane_t planes[FRUSTUM_PLANE_COUNT];
};

static_assert( sizeof( vec3_t ) == sizeof( common::f32 ) * 3u, "vec3_t must stay tightly packed" );
static_assert( sizeof( vec3_s_t ) == sizeof( common::i16 ) * 3u, "vec3_s_t must stay tightly packed" );
static_assert( sizeof( mat4_t ) == sizeof( common::f32 ) * 16u, "mat4_t must stay tightly packed" );

}       // namespace cypher::engine::math

#endif // CYPHER_ENGINE_MATH_TYPES_H
