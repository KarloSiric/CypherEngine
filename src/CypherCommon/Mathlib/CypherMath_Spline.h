//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Spline.h
//  Purpose: Declares cubic curves and arc-length sampling.
//  Details: Bezier, Hermite, and Catmull-Rom curves support editor paths,
//           animation trajectories, camera rails, and authored entity motion.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_MATH_SPLINE_H
#define CYPHER_COMMON_MATH_SPLINE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMath_Scalar.h"
#include "CypherMath_Vector3.h"

namespace cypher::math
{

using common::usize;

struct cubic_bezier3_t {
    vec3_t p0;
    vec3_t p1;
    vec3_t p2;
    vec3_t p3;
};

struct cubic_hermite3_t {
    vec3_t p0;
    vec3_t tangent0;
    vec3_t p1;
    vec3_t tangent1;
};

struct catmull_rom3_t {
    vec3_t p0;
    vec3_t p1;
    vec3_t p2;
    vec3_t p3;
};

struct spline_arc_sample_t {
    f32 parameter;
    f32 distance;
};

struct spline_arc_table_result_t {
    usize cSamplesWritten;
    f32 totalLength;
};

CYPHER_NODISCARD CYPHER_MATH_API vec3_t Spline_BezierPoint(
    cubic_bezier3_t curve, f32 t ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec3_t Spline_BezierDerivative(
    cubic_bezier3_t curve, f32 t ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec3_t Spline_BezierSecondDerivative(
    cubic_bezier3_t curve, f32 t ) noexcept;
CYPHER_MATH_API void Spline_BezierSplit(
    cubic_bezier3_t curve,
    f32 t,
    CY_OUT cubic_bezier3_t *pLeft,
    CY_OUT cubic_bezier3_t *pRight ) noexcept;

CYPHER_NODISCARD CYPHER_MATH_API vec3_t Spline_HermitePoint(
    cubic_hermite3_t curve, f32 t ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec3_t Spline_HermiteDerivative(
    cubic_hermite3_t curve, f32 t ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec3_t Spline_CatmullRomPoint(
    catmull_rom3_t curve, f32 t, f32 tension ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API vec3_t Spline_CatmullRomDerivative(
    catmull_rom3_t curve, f32 t, f32 tension ) noexcept;

CYPHER_NODISCARD CYPHER_MATH_API bool_t Spline_TryBuildBezierArcTable(
    cubic_bezier3_t curve,
    usize cSamples,
    CY_OUT_WRITES( cSampleCapacity ) spline_arc_sample_t *pSamples,
    usize cSampleCapacity,
    CY_OUT spline_arc_table_result_t *pResult ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API bool_t Spline_TryArcParameterAtDistance(
    CY_IN_READS( cSamples ) const spline_arc_sample_t *pSamples,
    usize cSamples,
    f32 distance,
    CY_OUT f32 *pParameter ) noexcept;

} // namespace cypher::math

#endif // CYPHER_COMMON_MATH_SPLINE_H
