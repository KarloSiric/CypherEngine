//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CurveSampling.cpp
//  Purpose: Implements arc-length sampling and double-reflection RMF.
//  Details: Arc length inside a step is inverted with a dense chord table
//           (kTableSteps chords per step, linear interpolation between).
//           That is accurate to well below authoring tolerance for smooth
//           cubic segments and exact for linear ones; the sample *positions*
//           are always evaluated on the true curve, only their spacing uses
//           the table.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_CurveSampling.h"

#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

constexpr u32 kTableSteps = 64u;

bool Normalize( math::vec3d_t v, math::vec3d_t *pOut ) noexcept
{
    return math::Vec3d_TryNormalize( v, 1.0e-300, pOut, nullptr );
}

// Tangent at a step parameter, robust to vanishing derivatives at Bézier
// endpoints with coincident handles: fall back to a nearby parameter, then
// to the chord.
bool StepTangent( const curve_network_t *pNet, curve_path_step_t st, f64 t, math::vec3d_t *pT ) noexcept
{
    if ( Normalize( CurveNetwork_EvaluateStepDerivative( pNet, st, t ), pT ) ) { return true; }
    const f64 t2 = t < 0.5 ? t + 1e-4 : t - 1e-4;
    if ( Normalize( CurveNetwork_EvaluateStepDerivative( pNet, st, t2 ), pT ) ) { return true; }
    return Normalize( math::Vec3d_Subtract( CurveNetwork_EvaluateStep( pNet, st, 1.0 ),
                                            CurveNetwork_EvaluateStep( pNet, st, 0.0 ) ), pT );
}

math::vec3d_t Reflect( math::vec3d_t v, math::vec3d_t axis, f64 axisLenSq ) noexcept
{
    return math::Vec3d_Subtract( v, math::Vec3d_Scale( axis, 2.0 * math::Vec3d_Dot( axis, v ) / axisLenSq ) );
}

// Rotates v about unit axis k by angle a (Rodrigues).
math::vec3d_t Rotate( math::vec3d_t v, math::vec3d_t k, f64 a ) noexcept
{
    const f64 c = std::cos( a ), s = std::sin( a );
    return math::Vec3d_Add(
        math::Vec3d_Add( math::Vec3d_Scale( v, c ), math::Vec3d_Scale( math::Vec3d_Cross( k, v ), s ) ),
        math::Vec3d_Scale( k, math::Vec3d_Dot( k, v ) * ( 1.0 - c ) ) );
}

} // namespace

geometry_status_t CurveSampling_TrySamplePath(
    const curve_network_t *pNet,
    u32 iPath,
    const curve_sampling_options_t &options,
    vector_t<curve_sample_t> *pOut ) noexcept
{
    if ( pOut == nullptr || pOut->pAllocator == nullptr || !( options.fMaxSpacing > 0.0 ) ||
         !math::Vec3d_IsFinite( options.initialUp ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !CurveNetwork_IsInitialized( pNet ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( iPath >= pNet->paths.nCount ) { return geometry_status_t::INVALID_ARGUMENT; }
    Vector_Clear( pOut );
    const curve_path_t &path = pNet->paths.pData[iPath];

    // ---- Positions, tangents, distances ---------------------------------------
    f64 distBase = 0.0;
    f64 table[kTableSteps + 1u];
    for ( u32 si = 0u; si < path.cSteps; ++si ) {
        const curve_path_step_t st = pNet->steps.pData[path.iFirstStep + si];
        table[0] = 0.0;
        math::vec3d_t prev = CurveNetwork_EvaluateStep( pNet, st, 0.0 );
        for ( u32 k = 1u; k <= kTableSteps; ++k ) {
            const math::vec3d_t p = CurveNetwork_EvaluateStep( pNet, st, static_cast<f64>( k ) / kTableSteps );
            table[k] = table[k - 1u] + std::sqrt( math::Vec3d_LengthSquared( math::Vec3d_Subtract( p, prev ) ) );
            prev = p;
        }
        const f64 len = table[kTableSteps];
        if ( !( len > 0.0 ) ) { Vector_Clear( pOut ); return geometry_status_t::DEGENERATE; }
        const u32 cDiv = static_cast<u32>( std::ceil( len / options.fMaxSpacing ) );
        const u32 nDiv = cDiv < 1u ? 1u : cDiv;
        // Emit samples k = 0 .. nDiv-1 (the step end is the next step's start,
        // or — for the last step of an open path — emitted after the loop).
        for ( u32 k = 0u; k < nDiv; ++k ) {
            const f64 target = len * static_cast<f64>( k ) / static_cast<f64>( nDiv );
            u32 j = 0u;
            while ( j < kTableSteps && table[j + 1u] < target ) { ++j; }
            const f64 span = table[j + 1u] - table[j];
            const f64 frac = span > 0.0 ? ( target - table[j] ) / span : 0.0;
            const f64 t = ( static_cast<f64>( j ) + frac ) / kTableSteps;
            curve_sample_t s{};
            s.position = CurveNetwork_EvaluateStep( pNet, st, t );
            if ( !StepTangent( pNet, st, t, &s.tangent ) ) { Vector_Clear( pOut ); return geometry_status_t::DEGENERATE; }
            s.distance = distBase + target;
            s.iStep = si;
            s.t = t;
            if ( pOut->nCount >= kCurveSamplesMax ) { Vector_Clear( pOut ); return geometry_status_t::LIMIT_EXCEEDED; }
            if ( !Vector_PushBack( pOut, s ) ) { Vector_Clear( pOut ); return geometry_status_t::ALLOCATION_FAILED; }
        }
        distBase += len;
    }
    if ( !path.bClosed ) {
        const curve_path_step_t last = pNet->steps.pData[path.iFirstStep + path.cSteps - 1u];
        curve_sample_t s{};
        s.position = CurveNetwork_EvaluateStep( pNet, last, 1.0 );
        if ( !StepTangent( pNet, last, 1.0, &s.tangent ) ) { Vector_Clear( pOut ); return geometry_status_t::DEGENERATE; }
        s.distance = distBase;
        s.iStep = path.cSteps - 1u;
        s.t = 1.0;
        if ( !Vector_PushBack( pOut, s ) ) { Vector_Clear( pOut ); return geometry_status_t::ALLOCATION_FAILED; }
    }

    // ---- Frames: seed, then double reflection -----------------------------------
    curve_sample_t *S = pOut->pData;
    const usize n = pOut->nCount;
    {
        math::vec3d_t up = options.initialUp;
        math::vec3d_t nrm{};
        const math::vec3d_t proj = math::Vec3d_Subtract( up, math::Vec3d_Scale( S[0].tangent, math::Vec3d_Dot( up, S[0].tangent ) ) );
        if ( !Normalize( proj, &nrm ) ) {
            // Up parallel to the tangent: use the least-aligned world axis.
            const math::vec3d_t t = S[0].tangent;
            const f64 ax = std::fabs( t.x ), ay = std::fabs( t.y ), az = std::fabs( t.z );
            up = ( ax <= ay && ax <= az ) ? math::Vec3d_Make( 1, 0, 0 )
               : ( ay <= az ) ? math::Vec3d_Make( 0, 1, 0 ) : math::Vec3d_Make( 0, 0, 1 );
            (void)Normalize( math::Vec3d_Subtract( up, math::Vec3d_Scale( t, math::Vec3d_Dot( up, t ) ) ), &nrm );
        }
        S[0].normal = nrm;
        S[0].binormal = math::Vec3d_Cross( S[0].tangent, nrm );
    }
    auto propagate = [&]( const curve_sample_t &a, curve_sample_t &b ) noexcept {
        const math::vec3d_t v1 = math::Vec3d_Subtract( b.position, a.position );
        const f64 c1 = math::Vec3d_LengthSquared( v1 );
        if ( !( c1 > 0.0 ) ) { b.normal = a.normal; return; }
        const math::vec3d_t rL = Reflect( a.normal, v1, c1 );
        const math::vec3d_t tL = Reflect( a.tangent, v1, c1 );
        const math::vec3d_t v2 = math::Vec3d_Subtract( b.tangent, tL );
        const f64 c2 = math::Vec3d_LengthSquared( v2 );
        math::vec3d_t r = c2 > 0.0 ? Reflect( rL, v2, c2 ) : rL;
        // Re-orthonormalize against the exact tangent to stop drift.
        r = math::Vec3d_Subtract( r, math::Vec3d_Scale( b.tangent, math::Vec3d_Dot( r, b.tangent ) ) );
        if ( !Normalize( r, &b.normal ) ) { b.normal = a.normal; }
    };
    for ( usize i = 1u; i < n; ++i ) {
        propagate( S[i - 1u], S[i] );
        S[i].binormal = math::Vec3d_Cross( S[i].tangent, S[i].normal );
    }

    // ---- Closed-loop twist correction ---------------------------------------------
    if ( path.bClosed && options.bCorrectClosedTwist && n > 1u ) {
        curve_sample_t wrap = S[0]; // where the frame *should* end up
        curve_sample_t end = S[n - 1u];
        wrap.normal = math::Vec3d_Make( 0, 0, 0 );
        propagate( end, wrap );       // RMF carried from the last sample to the start
        // Signed angle from the carried normal to the true start normal.
        const f64 angle = std::atan2( math::Vec3d_Dot( math::Vec3d_Cross( wrap.normal, S[0].normal ), S[0].tangent ),
                                      math::Vec3d_Dot( wrap.normal, S[0].normal ) );
        const f64 total = distBase;
        for ( usize i = 1u; i < n; ++i ) {
            const f64 a = angle * ( S[i].distance / total );
            S[i].normal = Rotate( S[i].normal, S[i].tangent, a );
            S[i].binormal = math::Vec3d_Cross( S[i].tangent, S[i].normal );
        }
    }
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
