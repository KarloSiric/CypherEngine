//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Mathlib/CypherCommon_Mathlib_Authoring_Bench.cpp
//  Purpose: Benchmarks editor-facing mathematical workloads.
//  Details: Measurements cover grid conversion, curve evaluation and sampling,
//           UV mapping, transform inversion, frustum queries, and quantization.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMath.h"

#include <benchmark/benchmark.h>

#include <vector>

using namespace cypher::math;

static void BM_GridWorldRoundTrip( benchmark::State &state )
{
    const vec3_t step = Vec3_Make( 0.25f, 2.0f, 4.0f );
    const vec3_t origin = Vec3_Make( 0.125f, -3.0f, 7.0f );
    grid_coord3_t grid{ 4096, -8192, 16384 };
    for ( auto _ : state ) {
        vec3_t world{};
        grid_coord3_t restored{};
        bool_t bWorld = Snap_TryGridToWorld(
            grid, step, origin, &world );
        bool_t bGrid = Snap_TryWorldToGrid(
            world, step, origin, snap_mode_t::NEAREST, &restored );
        benchmark::DoNotOptimize( bWorld );
        benchmark::DoNotOptimize( bGrid );
        benchmark::DoNotOptimize( restored );
        grid.x = grid.x < 8192 ? grid.x + 1 : 4096;
    }
}

BENCHMARK( BM_GridWorldRoundTrip );

static void BM_BezierPointAndDerivatives( benchmark::State &state )
{
    const cubic_bezier3_t curve{
        Vec3_Make( -2.0f, 1.0f, 0.0f ),
        Vec3_Make( 3.0f, 8.0f, -1.0f ),
        Vec3_Make( 7.0f, -4.0f, 5.0f ),
        Vec3_Make( 11.0f, 2.0f, 3.0f )
    };
    f32 t = 0.25f;
    for ( auto _ : state ) {
        vec3_t point = Spline_BezierPoint( curve, t );
        vec3_t first = Spline_BezierDerivative( curve, t );
        vec3_t second = Spline_BezierSecondDerivative( curve, t );
        benchmark::DoNotOptimize( point );
        benchmark::DoNotOptimize( first );
        benchmark::DoNotOptimize( second );
        t = t < 0.75f ? t + 0.000001f : 0.25f;
    }
}

BENCHMARK( BM_BezierPointAndDerivatives );

static void BM_BezierArcTable( benchmark::State &state )
{
    const usize cSamples = static_cast<usize>( state.range( 0 ) );
    const cubic_bezier3_t curve{
        Vec3_Make( -2.0f, 1.0f, 0.0f ),
        Vec3_Make( 3.0f, 8.0f, -1.0f ),
        Vec3_Make( 7.0f, -4.0f, 5.0f ),
        Vec3_Make( 11.0f, 2.0f, 3.0f )
    };
    std::vector<spline_arc_sample_t> samples( cSamples );
    for ( auto _ : state ) {
        spline_arc_table_result_t result{};
        bool_t bBuilt = Spline_TryBuildBezierArcTable(
            curve, cSamples, samples.data(), samples.size(), &result );
        benchmark::DoNotOptimize( bBuilt );
        benchmark::DoNotOptimize( result );
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(
        state.iterations() * static_cast<benchmark::IterationCount>( cSamples ) );
}

BENCHMARK( BM_BezierArcTable )->Arg( 17 )->Arg( 65 )->Arg( 257 );

static void BM_PlanarUvRoundTrip( benchmark::State &state )
{
    planar_uv_mapping_t mapping{};
    const bool_t bMapping = Uv_TryBuildPlanarMapping(
        Vec3_Make( 3.0f, -7.0f, 11.0f ),
        Vec3_Make( 1.0f, 2.0f, 3.0f ), CY_VEC3_UP,
        Vec2_Make( -0.25f, 0.5f ), Angle_FromDegrees( 37.0f ),
        Vec2_Make( 0.25f, -0.75f ), 0.000001f, &mapping );
    if ( !bMapping ) {
        state.SkipWithError( "Failed to initialize planar UV mapping." );
        return;
    }
    vec2_t uv = Vec2_Make( 1.25f, -3.5f );
    for ( auto _ : state ) {
        vec3_t world{};
        vec2_t projected{};
        bool_t bWorld = Uv_TryUnprojectPlanarPoint(
            mapping, uv, 2.5f, 0.000001f, &world );
        bool_t bProjected = Uv_TryProjectPlanarPoint(
            mapping, world, 0.000001f, &projected );
        benchmark::DoNotOptimize( bWorld );
        benchmark::DoNotOptimize( bProjected );
        benchmark::DoNotOptimize( projected );
        uv.x = uv.x < 2.25f ? uv.x + 0.000001f : 1.25f;
    }
}

BENCHMARK( BM_PlanarUvRoundTrip );

static void BM_TransformPointRoundTrip( benchmark::State &state )
{
    const transform_t transform = Transform_Make(
        Vec3_Make( 7.0f, -3.0f, 2.0f ),
        Quat_FromEulerXYZ( Vec3_Make( 0.4f, -0.8f, 1.2f ) ),
        Vec3_Make( -2.0f, 3.0f, 4.0f ) );
    vec3_t source = Vec3_Make( 1.0f, -2.0f, 3.0f );
    for ( auto _ : state ) {
        vec3_t world = Transform_TransformPoint( transform, source );
        vec3_t restored{};
        bool_t bRestored = Transform_TryInversePoint(
            transform, world, 0.000001f, &restored );
        benchmark::DoNotOptimize( bRestored );
        benchmark::DoNotOptimize( restored );
        source.x = source.x < 2.0f ? source.x + 0.000001f : 1.0f;
    }
}

BENCHMARK( BM_TransformPointRoundTrip );

static void BM_FrustumAabbClassification( benchmark::State &state )
{
    mat4_t projection{};
    frustum_t frustum{};
    const bool_t bProjection = Mat4_TryPerspectiveRH(
        Angle_FromDegrees( 75.0f ), 16.0f / 9.0f, 0.1f, 10000.0f,
        clip_depth_range_t::NEGATIVE_ONE_TO_ONE, &projection );
    const bool_t bFrustum = Frustum_TryFromViewProjection(
        projection, clip_depth_range_t::NEGATIVE_ONE_TO_ONE,
        0.000001f, &frustum );
    if ( !bProjection || !bFrustum ) {
        state.SkipWithError( "Failed to initialize frustum benchmark." );
        return;
    }
    aabb_t bounds = Aabb_FromCenterExtents(
        Vec3_Make( 0.0f, 0.0f, -100.0f ), Vec3_Splat( 4.0f ) );
    for ( auto _ : state ) {
        volume_relation_t relation = Intersection_FrustumAabb(
            frustum, bounds, 0.00001f );
        benchmark::DoNotOptimize( relation );
        const f32 shift = bounds.minimum.x < -3.0f ? 0.000001f : -1.0f;
        bounds.minimum.x += shift;
        bounds.maximum.x += shift;
    }
}

BENCHMARK( BM_FrustumAabbClassification );

static void BM_RangeQuantizationRoundTrip( benchmark::State &state )
{
    f32 value = 17.25f;
    for ( auto _ : state ) {
        u32 code = 0u;
        f32 decoded = 0.0f;
        bool_t bEncoded = Quantization_TryEncodeRange(
            value, -100.0f, 100.0f, 16u, &code );
        bool_t bDecoded = Quantization_TryDecodeRange(
            code, -100.0f, 100.0f, 16u, &decoded );
        benchmark::DoNotOptimize( bEncoded );
        benchmark::DoNotOptimize( bDecoded );
        benchmark::DoNotOptimize( decoded );
        value = value < 90.0f ? value + 0.000001f : 17.25f;
    }
}

BENCHMARK( BM_RangeQuantizationRoundTrip );
