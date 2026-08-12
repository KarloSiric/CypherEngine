//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Mathlib/CypherCommon_Mathlib_Core_Bench.cpp
//  Purpose: Benchmarks representative CypherMath runtime operations.
//  Details: The suite covers runtime hot paths plus representative editor and
//           serialization workloads without mixing allocation or I/O costs.
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

static void BM_Vec3DotCross( benchmark::State &state )
{
    vec3_t a = Vec3_Make( 1.25f, -7.5f, 3.0f );
    vec3_t b = Vec3_Make( -2.0f, 0.25f, 9.5f );
    for ( auto _ : state ) {
        f32 dot = Vec3_Dot( a, b );
        vec3_t cross = Vec3_Cross( a, b );
        benchmark::DoNotOptimize( dot );
        benchmark::DoNotOptimize( cross );
        a = Vec3_Add( a, Vec3_Scale( cross, 0.0000001f ) );
        benchmark::ClobberMemory();
    }
}

BENCHMARK( BM_Vec3DotCross );

static void BM_QuaternionRotateVector( benchmark::State &state )
{
    quat_t rotation = Quat_FromEulerXYZ(
        Vec3_Make( 0.3f, -0.7f, 1.2f ) );
    vec3_t value = Vec3_Make( 12.0f, -2.0f, 8.0f );
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( rotation );
        value = Quat_RotateVectorUnit( rotation, value );
        benchmark::DoNotOptimize( value );
        benchmark::ClobberMemory();
    }
}

BENCHMARK( BM_QuaternionRotateVector );

static void BM_Matrix4Multiply( benchmark::State &state )
{
    mat4_t a = Mat4_FromTRS(
        Vec3_Make( 3.0f, 7.0f, -2.0f ),
        Quat_FromEulerXYZ( Vec3_Make( 0.2f, 0.4f, 0.6f ) ),
        Vec3_Make( 2.0f, 1.0f, 0.5f ) );
    mat4_t b = Mat4_FromTRS(
        Vec3_Make( -8.0f, 1.0f, 5.0f ),
        Quat_FromEulerXYZ( Vec3_Make( -0.3f, 0.8f, 0.1f ) ),
        Vec3_Make( 1.5f, 3.0f, 2.0f ) );
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( a );
        benchmark::DoNotOptimize( b );
        mat4_t result = Mat4_Multiply( a, b );
        benchmark::DoNotOptimize( result );
        benchmark::ClobberMemory();
    }
}

BENCHMARK( BM_Matrix4Multiply );

static void BM_Matrix4Inverse( benchmark::State &state )
{
    mat4_t matrix = Mat4_FromTRS(
        Vec3_Make( 3.0f, -7.0f, 2.0f ),
        Quat_FromEulerXYZ( Vec3_Make( 0.4f, -0.8f, 1.3f ) ),
        Vec3_Make( 2.0f, 3.0f, 0.25f ) );
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( matrix );
        mat4_t inverse{};
        bool_t bInverted =
            Mat4_TryInverse( matrix, 0.0000001f, &inverse );
        benchmark::DoNotOptimize( bInverted );
        benchmark::DoNotOptimize( inverse );
        benchmark::ClobberMemory();
    }
}

BENCHMARK( BM_Matrix4Inverse );

static void BM_AabbTransform( benchmark::State &state )
{
    aabb_t bounds = Aabb_Make(
        Vec3_Make( -10.0f, -20.0f, -5.0f ),
        Vec3_Make( 12.0f, 8.0f, 16.0f ) );
    affine3_t transform = Affine3_FromTRS(
        Vec3_Make( 100.0f, 20.0f, -40.0f ),
        Quat_FromEulerXYZ( Vec3_Make( 0.2f, 0.5f, -0.1f ) ),
        Vec3_Make( 1.5f, 0.75f, 2.0f ) );
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( bounds );
        benchmark::DoNotOptimize( transform );
        aabb_t transformed = Aabb_TransformAffine( bounds, transform );
        benchmark::DoNotOptimize( transformed );
        benchmark::ClobberMemory();
    }
}

BENCHMARK( BM_AabbTransform );

static void BM_RayAabbIntersection( benchmark::State &state )
{
    ray_t ray = Ray_Make(
        Vec3_Make( -100.0f, 1.0f, 2.0f ),
        Vec3_Make( 1.0f, 0.01f, -0.02f ) );
    aabb_t bounds = Aabb_Make(
        Vec3_Make( -5.0f, -5.0f, -5.0f ),
        Vec3_Make( 5.0f, 5.0f, 5.0f ) );
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( ray );
        benchmark::DoNotOptimize( bounds );
        ray_interval_t interval{};
        bool_t bHit = Intersection_RayAabb(
            ray, bounds, 0.000001f, 0.0f, 1000.0f, &interval );
        benchmark::DoNotOptimize( bHit );
        benchmark::DoNotOptimize( interval );
        benchmark::ClobberMemory();
    }
}

BENCHMARK( BM_RayAabbIntersection );

static void BM_ScalarTransformPoints( benchmark::State &state )
{
    const usize cPoints = static_cast<usize>( state.range( 0 ) );
    std::vector<vec3_t> input( cPoints );
    std::vector<vec3_t> output( cPoints );
    for ( usize i = 0u; i < cPoints; ++i ) {
        input[i] = Vec3_Make(
            static_cast<f32>( i % 31u ),
            static_cast<f32>( i % 17u ),
            static_cast<f32>( i % 11u ) );
    }
    const mat4_t transform = Mat4_FromTRS(
        Vec3_Make( 100.0f, -20.0f, 8.0f ),
        Quat_FromEulerXYZ( Vec3_Make( 0.2f, -0.6f, 1.1f ) ),
        Vec3_Make( 1.5f, 0.75f, 2.0f ) );
    for ( auto _ : state ) {
        for ( usize i = 0u; i < cPoints; ++i ) {
            output[i] = Mat4_TransformPointAffine( transform, input[i] );
        }
        benchmark::DoNotOptimize( output.data() );
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(
        state.iterations() * static_cast<benchmark::IterationCount>( cPoints ) );
}

BENCHMARK( BM_ScalarTransformPoints )->Arg( 64 )->Arg( 1024 )->Arg( 16384 );

static void BM_BatchTransformPoints( benchmark::State &state )
{
    const usize cPoints = static_cast<usize>( state.range( 0 ) );
    std::vector<vec3_t> input( cPoints );
    std::vector<vec3_t> output( cPoints );
    for ( usize i = 0u; i < cPoints; ++i ) {
        input[i] = Vec3_Make(
            static_cast<f32>( i % 31u ),
            static_cast<f32>( i % 17u ),
            static_cast<f32>( i % 11u ) );
    }
    const mat4_t transform = Mat4_FromTRS(
        Vec3_Make( 100.0f, -20.0f, 8.0f ),
        Quat_FromEulerXYZ( Vec3_Make( 0.2f, -0.6f, 1.1f ) ),
        Vec3_Make( 1.5f, 0.75f, 2.0f ) );
    for ( auto _ : state ) {
        Vec3Batch_TransformPointsAffine(
            transform, input.data(), output.data(), cPoints );
        benchmark::DoNotOptimize( output.data() );
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(
        state.iterations() * static_cast<benchmark::IterationCount>( cPoints ) );
}

BENCHMARK( BM_BatchTransformPoints )->Arg( 64 )->Arg( 1024 )->Arg( 16384 );

static void BM_ConcavePolygonTriangulation( benchmark::State &state )
{
    constexpr vec2_t polygon[]{
        { 0.0f, 0.0f },
        { 4.0f, 0.0f },
        { 5.0f, 2.0f },
        { 4.0f, 4.0f },
        { 2.5f, 2.5f },
        { 0.0f, 4.0f },
        { -1.0f, 2.0f }
    };
    u32 scratch[7]{};
    u32 indices[15]{};
    for ( auto _ : state ) {
        polygon_triangulation_result_t result = Polygon2_Triangulate(
            polygon, 7u, 0.000001, scratch, 7u, indices, 15u );
        benchmark::DoNotOptimize( result );
        benchmark::DoNotOptimize( indices );
        benchmark::ClobberMemory();
    }
}

BENCHMARK( BM_ConcavePolygonTriangulation );

static void BM_CubeBrushVertexBuild( benchmark::State &state )
{
    constexpr plane_t planes[]{
        { { 1.0f, 0.0f, 0.0f }, -1.0f },
        { { -1.0f, 0.0f, 0.0f }, -1.0f },
        { { 0.0f, 1.0f, 0.0f }, -1.0f },
        { { 0.0f, -1.0f, 0.0f }, -1.0f },
        { { 0.0f, 0.0f, 1.0f }, -1.0f },
        { { 0.0f, 0.0f, -1.0f }, -1.0f }
    };
    vec3_t vertices[20]{};
    for ( auto _ : state ) {
        brush_vertex_result_t result = Brush_BuildVertices(
            planes, 6u, 0.000001, 0.00001f, 0.00001f, vertices, 20u );
        benchmark::DoNotOptimize( result );
        benchmark::DoNotOptimize( vertices );
        benchmark::ClobberMemory();
    }
}

BENCHMARK( BM_CubeBrushVertexBuild );

static void BM_ViewportPickingRay( benchmark::State &state )
{
    mat4_t view{};
    mat4_t projection{};
    const bool_t bViewBuilt = Mat4_TryLookAtRH(
        CY_VEC3_ZERO, CY_VEC3_FORWARD, CY_VEC3_UP, 0.000001f, &view );
    const bool_t bProjectionBuilt = Mat4_TryPerspectiveRH(
        Angle_FromDegrees( 75.0f ), 16.0f / 9.0f, 0.1f, 10000.0f,
        clip_depth_range_t::NEGATIVE_ONE_TO_ONE, &projection );
    mat4_t clipToWorld{};
    const bool_t bInverseBuilt = Mat4_TryInverse(
        Mat4_Multiply( projection, view ), 0.000001f, &clipToWorld );
    if ( !bViewBuilt || !bProjectionBuilt || !bInverseBuilt ) {
        state.SkipWithError( "Failed to initialize viewport benchmark matrices." );
        return;
    }
    constexpr viewport_rect_t viewport{ 0.0f, 0.0f, 1920.0f, 1080.0f };
    for ( auto _ : state ) {
        ray_t ray{};
        bool_t bBuilt = Viewport_TryBuildPickingRay(
            clipToWorld, viewport, viewport_origin_t::TOP_LEFT,
            clip_depth_range_t::NEGATIVE_ONE_TO_ONE,
            Vec2_Make( 913.0f, 517.0f ), 0.000001f, 0.000001f, &ray );
        benchmark::DoNotOptimize( bBuilt );
        benchmark::DoNotOptimize( ray );
        benchmark::ClobberMemory();
    }
}

BENCHMARK( BM_ViewportPickingRay );

static void BM_FixedPointMultiplyDivide( benchmark::State &state )
{
    fixed16_16_t a{};
    fixed16_16_t b{};
    const bool_t bAConverted = Fixed16_16_TryFromF64( 123.25, &a );
    const bool_t bBConverted = Fixed16_16_TryFromF64( -7.75, &b );
    if ( !bAConverted || !bBConverted ) {
        state.SkipWithError( "Failed to initialize fixed-point benchmark values." );
        return;
    }
    for ( auto _ : state ) {
        fixed16_16_t product{};
        fixed16_16_t quotient{};
        bool_t bMultiplied = Fixed16_16_TryMultiply( a, b, &product );
        bool_t bDivided = Fixed16_16_TryDivide( product, b, &quotient );
        benchmark::DoNotOptimize( bMultiplied );
        benchmark::DoNotOptimize( bDivided );
        benchmark::DoNotOptimize( quotient );
        benchmark::ClobberMemory();
    }
}

BENCHMARK( BM_FixedPointMultiplyDivide );

static void BM_QuaternionQuantization( benchmark::State &state )
{
    const quat_t source = Quat_FromEulerXYZ(
        Vec3_Make( 0.3f, -1.1f, 2.2f ) );
    for ( auto _ : state ) {
        quantized_quat_t code{};
        quat_t decoded{};
        bool_t bEncoded = Quantization_TryEncodeQuatSmallestThree(
            source, 12u, 0.000001f, &code );
        bool_t bDecoded = Quantization_TryDecodeQuatSmallestThree(
            code, 12u, 0.000001f, &decoded );
        benchmark::DoNotOptimize( bEncoded );
        benchmark::DoNotOptimize( bDecoded );
        benchmark::DoNotOptimize( decoded );
        benchmark::ClobberMemory();
    }
}

BENCHMARK( BM_QuaternionQuantization );
