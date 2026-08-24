//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Spline.cpp
//  Purpose: Implements cubic curves and arc-length sampling.
//  Details: Polynomial evaluation avoids allocation; arc tables use cumulative
//           chord length and binary search for repeatable distance lookup.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMath_Spline.h"

#include "CypherCommon_Assert.h"

#include <algorithm>

namespace cypher::math
{

//==========================================================================
// Cubic Bezier curves
//==========================================================================

vec3_t Spline_BezierPoint( cubic_bezier3_t curve, f32 t ) noexcept
{
    // Bernstein basis evaluation keeps the four control-point weights explicit.
    const f32 oneMinusT = 1.0f - t;
    const f32 b0 = oneMinusT * oneMinusT * oneMinusT;
    const f32 b1 = 3.0f * oneMinusT * oneMinusT * t;
    const f32 b2 = 3.0f * oneMinusT * t * t;
    const f32 b3 = t * t * t;
    return Vec3_Add(
        Vec3_Add( Vec3_Scale( curve.p0, b0 ), Vec3_Scale( curve.p1, b1 ) ),
        Vec3_Add( Vec3_Scale( curve.p2, b2 ), Vec3_Scale( curve.p3, b3 ) ) );
}

vec3_t Spline_BezierDerivative( cubic_bezier3_t curve, f32 t ) noexcept
{
    const f32 oneMinusT = 1.0f - t;
    return Vec3_Add(
        Vec3_Add(
            Vec3_Scale( Vec3_Subtract( curve.p1, curve.p0 ),
                3.0f * oneMinusT * oneMinusT ),
            Vec3_Scale( Vec3_Subtract( curve.p2, curve.p1 ),
                6.0f * oneMinusT * t ) ),
        Vec3_Scale( Vec3_Subtract( curve.p3, curve.p2 ), 3.0f * t * t ) );
}

vec3_t Spline_BezierSecondDerivative( cubic_bezier3_t curve, f32 t ) noexcept
{
    const vec3_t first = Vec3_Add(
        Vec3_Subtract( curve.p2, Vec3_Scale( curve.p1, 2.0f ) ),
        curve.p0 );
    const vec3_t second = Vec3_Add(
        Vec3_Subtract( curve.p3, Vec3_Scale( curve.p2, 2.0f ) ),
        curve.p1 );
    return Vec3_Scale( Vec3_Lerp( first, second, t ), 6.0f );
}

void Spline_BezierSplit(
    cubic_bezier3_t curve,
    f32 t,
    cubic_bezier3_t *pLeft,
    cubic_bezier3_t *pRight ) noexcept
{
    const bool_t bValidOutputs = pLeft != nullptr && pRight != nullptr;
    CY_ASSERT_MSG( bValidOutputs, "Spline_BezierSplit requires two outputs." );
    if ( !bValidOutputs ) {
        return;
    }
    // de Casteljau subdivision produces two curves that meet exactly at t.
    const vec3_t p01 = Vec3_Lerp( curve.p0, curve.p1, t );
    const vec3_t p12 = Vec3_Lerp( curve.p1, curve.p2, t );
    const vec3_t p23 = Vec3_Lerp( curve.p2, curve.p3, t );
    const vec3_t p012 = Vec3_Lerp( p01, p12, t );
    const vec3_t p123 = Vec3_Lerp( p12, p23, t );
    const vec3_t split = Vec3_Lerp( p012, p123, t );
    *pLeft = { curve.p0, p01, p012, split };
    *pRight = { split, p123, p23, curve.p3 };
}

vec3_t Spline_HermitePoint( cubic_hermite3_t curve, f32 t ) noexcept
{
    const f32 t2 = t * t;
    const f32 t3 = t2 * t;
    const f32 h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
    const f32 h10 = t3 - 2.0f * t2 + t;
    const f32 h01 = -2.0f * t3 + 3.0f * t2;
    const f32 h11 = t3 - t2;
    return Vec3_Add(
        Vec3_Add( Vec3_Scale( curve.p0, h00 ), Vec3_Scale( curve.tangent0, h10 ) ),
        Vec3_Add( Vec3_Scale( curve.p1, h01 ), Vec3_Scale( curve.tangent1, h11 ) ) );
}

vec3_t Spline_HermiteDerivative( cubic_hermite3_t curve, f32 t ) noexcept
{
    const f32 t2 = t * t;
    const f32 h00 = 6.0f * t2 - 6.0f * t;
    const f32 h10 = 3.0f * t2 - 4.0f * t + 1.0f;
    const f32 h01 = -6.0f * t2 + 6.0f * t;
    const f32 h11 = 3.0f * t2 - 2.0f * t;
    return Vec3_Add(
        Vec3_Add( Vec3_Scale( curve.p0, h00 ), Vec3_Scale( curve.tangent0, h10 ) ),
        Vec3_Add( Vec3_Scale( curve.p1, h01 ), Vec3_Scale( curve.tangent1, h11 ) ) );
}

//==========================================================================
// Catmull-Rom conversion
//==========================================================================

vec3_t Spline_CatmullRomPoint(
    catmull_rom3_t curve,
    f32 t,
    f32 tension ) noexcept
{
    const f32 tangentScale = 1.0f - tension;
    // Convert the local Catmull-Rom span to Hermite form and reuse its evaluator.
    const cubic_hermite3_t hermite{
        curve.p1,
        Vec3_Scale( Vec3_Subtract( curve.p2, curve.p0 ), 0.5f * tangentScale ),
        curve.p2,
        Vec3_Scale( Vec3_Subtract( curve.p3, curve.p1 ), 0.5f * tangentScale )
    };
    return Spline_HermitePoint( hermite, t );
}

vec3_t Spline_CatmullRomDerivative(
    catmull_rom3_t curve,
    f32 t,
    f32 tension ) noexcept
{
    const f32 tangentScale = 1.0f - tension;
    const cubic_hermite3_t hermite{
        curve.p1,
        Vec3_Scale( Vec3_Subtract( curve.p2, curve.p0 ), 0.5f * tangentScale ),
        curve.p2,
        Vec3_Scale( Vec3_Subtract( curve.p3, curve.p1 ), 0.5f * tangentScale )
    };
    return Spline_HermiteDerivative( hermite, t );
}

bool_t Spline_TryBuildBezierArcTable(
    cubic_bezier3_t curve,
    usize cSamples,
    spline_arc_sample_t *pSamples,
    usize cSampleCapacity,
    spline_arc_table_result_t *pResult ) noexcept
{
    const bool_t bValidOutput = pResult != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Spline_TryBuildBezierArcTable requires result storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pResult = {};
    if ( pSamples == nullptr || cSamples < 2u || cSampleCapacity < cSamples ) {
        return false;
    }

    vec3_t previous = Spline_BezierPoint( curve, 0.0f );
    pSamples[0] = { 0.0f, 0.0f };
    f32 totalLength = 0.0f;
    // This is a chord-length approximation. Increasing cSamples trades build
    // time and table memory for a closer distance-to-parameter mapping.
    for ( usize i = 1u; i < cSamples; ++i ) {
        const f32 parameter = static_cast<f32>( i ) /
            static_cast<f32>( cSamples - 1u );
        const vec3_t point = Spline_BezierPoint( curve, parameter );
        totalLength += Vec3_Distance( previous, point );
        if ( !Scalar_IsFinite( totalLength ) ) {
            return false;
        }
        pSamples[i] = { parameter, totalLength };
        previous = point;
    }
    *pResult = { cSamples, totalLength };
    return true;
}

bool_t Spline_TryArcParameterAtDistance(
    const spline_arc_sample_t *pSamples,
    usize cSamples,
    f32 distance,
    f32 *pParameter ) noexcept
{
    const bool_t bValidOutput = pParameter != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Spline_TryArcParameterAtDistance requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pParameter = 0.0f;
    if ( pSamples == nullptr || cSamples < 2u || !Scalar_IsFinite( distance ) ) {
        return false;
    }
    if ( distance <= 0.0f ) {
        return true;
    }
    if ( distance >= pSamples[cSamples - 1u].distance ) {
        *pParameter = pSamples[cSamples - 1u].parameter;
        return true;
    }

    usize low = 0u;
    usize high = cSamples - 1u;
    // Cumulative distances are monotonic, so locate the enclosing samples in
    // logarithmic time and interpolate their original curve parameters.
    while ( low + 1u < high ) {
        const usize middle = low + ( high - low ) / 2u;
        if ( pSamples[middle].distance < distance ) {
            low = middle;
        } else {
            high = middle;
        }
    }
    const f32 interval = pSamples[high].distance - pSamples[low].distance;
    if ( interval <= 0.0f ) {
        *pParameter = pSamples[low].parameter;
        return true;
    }
    const f32 fraction = ( distance - pSamples[low].distance ) / interval;
    *pParameter = Scalar_Lerp(
        pSamples[low].parameter,
        pSamples[high].parameter,
        std::clamp( fraction, 0.0f, 1.0f ) );
    return true;
}

} // namespace cypher::math
