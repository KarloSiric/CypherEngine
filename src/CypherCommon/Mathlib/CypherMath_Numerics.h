//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Numerics.h
//  Purpose: Declares robust numerical helpers shared by tools and physics.
//  Details: Solvers use double-precision intermediates and integration APIs make
//           timestep and angular-velocity space explicit at every call site.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_MATH_NUMERICS_H
#define CYPHER_COMMON_MATH_NUMERICS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMath_Quaternion.h"
#include "CypherMath_Ray.h"

namespace cypher::math
{

enum class polynomial_solution_count_t : common::u8 {
    ZERO = 0u, // No real root satisfies the equation.
    ONE,       // One real root, including a repeated quadratic root.
    TWO,       // Two distinct real roots.
    INFINITE,  // All values satisfy a degenerate zero equation.
    COUNT      // Enum bound; not a solver result.
};

enum class angular_velocity_space_t : common::u8 {
    LOCAL = 0u, // Angular velocity is expressed in body-local axes.
    WORLD,      // Angular velocity is expressed in world axes.
    COUNT       // Enum bound; not an integration policy.
};

struct quadratic_solution_t {
    polynomial_solution_count_t count; // Number and interpretation of valid roots.
    f64 root0;                         // Smaller real root when two exist.
    f64 root1;                         // Larger real root; equals root0 for one root.
};

struct segment_closest_points_t {
    vec3_t pointA;       // Closest point on segment A.
    vec3_t pointB;       // Closest point on segment B.
    f32 parameterA;      // Normalized [0, 1] parameter on A.
    f32 parameterB;      // Normalized [0, 1] parameter on B.
    f32 distanceSquared; // Squared distance between pointA and pointB.
};

// Solves a*x^2 + b*x + c = 0 and returns sorted real roots.
CYPHER_NODISCARD CYPHER_MATH_API bool_t Numerics_TrySolveQuadratic(
    f64 a,
    f64 b,
    f64 c,
    f64 coefficientTolerance,
    f64 discriminantTolerance,
    CY_OUT quadratic_solution_t *pSolution ) noexcept;

// Handles point-like and parallel segments without normalized directions.
CYPHER_NODISCARD CYPHER_MATH_API bool_t Numerics_TryClosestSegmentPoints(
    segment_t a,
    segment_t b,
    f64 relativeParallelTolerance,
    CY_OUT segment_closest_points_t *pClosest ) noexcept;

// Advances velocity first, then position using the updated velocity.
CYPHER_NODISCARD CYPHER_MATH_API bool_t Numerics_TryIntegrateLinearSemiImplicit(
    vec3_t position,
    vec3_t velocity,
    vec3_t acceleration,
    f32 deltaSeconds,
    CY_OUT vec3_t *pNextPosition,
    CY_OUT vec3_t *pNextVelocity ) noexcept;

// Integrates radians-per-second angular velocity using an exponential-map step.
CYPHER_NODISCARD CYPHER_MATH_API bool_t Numerics_TryIntegrateAngularVelocity(
    quat_t orientation,
    vec3_t angularVelocity,
    angular_velocity_space_t velocitySpace,
    f32 deltaSeconds,
    f32 minimumLength,
    CY_OUT quat_t *pNextOrientation ) noexcept;

} // namespace cypher::math

#endif // CYPHER_COMMON_MATH_NUMERICS_H
