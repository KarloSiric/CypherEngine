//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Mathlib/CypherCommon_Mathlib_Primitives_Bench.cpp
//  Purpose: Benchmarks core geometric and spatial primitives.
//  Details: Measurements isolate checked normalization, rotation interpolation,
//           matrix inversion, bounds queries, and collision primitives.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMath.h"

#include <benchmark/benchmark.h>

using namespace cypher::math;

static void BM_Vec3TryNormalize( benchmark::State &state )
{
    vec3_t value = Vec3_Make( 123.5f, -77.25f, 41.0f );
    for ( auto _ : state ) {
        vec3_t normalized{};
        f32 length = 0.0f;
        bool_t bNormalized = Vec3_TryNormalize(
            value, 0.000001f, &normalized, &length );
        benchmark::DoNotOptimize( bNormalized );
        benchmark::DoNotOptimize( normalized );
        benchmark::DoNotOptimize( length );
        value.x = value.x < 124.5f ? value.x + 0.000001f : 123.5f;
    }
}

BENCHMARK( BM_Vec3TryNormalize );

static void BM_QuaternionSlerp( benchmark::State &state )
{
    const quat_t a = Quat_FromEulerXYZ( Vec3_Make( 0.2f, -0.7f, 1.4f ) );
    const quat_t b = Quat_FromEulerXYZ( Vec3_Make( -1.1f, 0.5f, 2.2f ) );
    f32 t = 0.25f;
    for ( auto _ : state ) {
        quat_t result = Quat_Slerp( a, b, t );
        benchmark::DoNotOptimize( result );
        t = t < 0.75f ? t + 0.000001f : 0.25f;
    }
}

BENCHMARK( BM_QuaternionSlerp );

static void BM_Matrix3Inverse( benchmark::State &state )
{
    const mat3_t matrix = Mat3_FromRows(
        Vec3_Make( 2.0f, 0.0f, 1.0f ),
        Vec3_Make( 1.0f, 3.0f, 0.0f ),
        Vec3_Make( 0.0f, 2.0f, 4.0f ) );
    for ( auto _ : state ) {
        mat3_t inverse{};
        bool_t bInverted = Mat3_TryInverse(
            matrix, 0.000001f, &inverse );
        benchmark::DoNotOptimize( bInverted );
        benchmark::DoNotOptimize( inverse );
    }
}

BENCHMARK( BM_Matrix3Inverse );

static void BM_AabbSetAndDistanceQueries( benchmark::State &state )
{
    const aabb_t a = Aabb_Make(
        Vec3_Make( -20.0f, -10.0f, -5.0f ),
        Vec3_Make( 30.0f, 40.0f, 15.0f ) );
    const aabb_t b = Aabb_Make(
        Vec3_Make( 10.0f, -30.0f, 0.0f ),
        Vec3_Make( 50.0f, 20.0f, 25.0f ) );
    vec3_t point = Vec3_Make( 60.0f, -40.0f, 10.0f );
    for ( auto _ : state ) {
        aabb_t combined = Aabb_Union( a, b );
        aabb_t overlap = Aabb_Intersection( a, b );
        vec3_t closest = Aabb_ClosestPoint( combined, point );
        f32 distance = Aabb_DistanceSquaredToPoint( overlap, point );
        benchmark::DoNotOptimize( combined );
        benchmark::DoNotOptimize( overlap );
        benchmark::DoNotOptimize( closest );
        benchmark::DoNotOptimize( distance );
        point.y = point.y < -39.0f ? point.y + 0.000001f : -40.0f;
    }
}

BENCHMARK( BM_AabbSetAndDistanceQueries );

static void BM_SphereMergeAndClosestPoint( benchmark::State &state )
{
    const sphere_t a = Sphere_Make( Vec3_Make( -5.0f, 1.0f, 2.0f ), 3.0f );
    const sphere_t b = Sphere_Make( Vec3_Make( 8.0f, -2.0f, 4.0f ), 5.0f );
    vec3_t point = Vec3_Make( 20.0f, 3.0f, -7.0f );
    for ( auto _ : state ) {
        sphere_t merged = Sphere_Merge( a, b, 0.000001f );
        vec3_t closest = Sphere_ClosestPoint(
            merged, point, 0.000001f );
        benchmark::DoNotOptimize( merged );
        benchmark::DoNotOptimize( closest );
        point.z = point.z < -6.0f ? point.z + 0.000001f : -7.0f;
    }
}

BENCHMARK( BM_SphereMergeAndClosestPoint );

static void BM_SegmentTriangleIntersection( benchmark::State &state )
{
    const triangle3_t triangle = Triangle3_Make(
        Vec3_Make( -4.0f, -4.0f, 0.0f ),
        Vec3_Make( 4.0f, -4.0f, 0.0f ),
        Vec3_Make( 0.0f, 4.0f, 0.0f ) );
    const segment_t segment = Segment_Make(
        Vec3_Make( 0.25f, 0.5f, 10.0f ),
        Vec3_Make( 0.25f, 0.5f, -10.0f ) );
    for ( auto _ : state ) {
        ray_triangle_hit_t hit{};
        bool_t bHit = Intersection_SegmentTriangle(
            segment, triangle, triangle_cull_mode_t::NONE,
            0.000001f, 0.000001f, &hit );
        benchmark::DoNotOptimize( bHit );
        benchmark::DoNotOptimize( hit );
    }
}

BENCHMARK( BM_SegmentTriangleIntersection );

static void BM_PlaneAffineTransform( benchmark::State &state )
{
    const plane_t plane = Plane_Make( CY_VEC3_UP, -2.0f );
    const affine3_t transform = Affine3_FromTRS(
        Vec3_Make( 5.0f, -3.0f, 9.0f ),
        Quat_FromEulerXYZ( Vec3_Make( 0.3f, -0.6f, 1.0f ) ),
        Vec3_Make( 2.0f, 0.75f, 3.0f ) );
    for ( auto _ : state ) {
        plane_t transformed{};
        bool_t bTransformed = Plane_TryTransform(
            plane, transform, 0.000001f, 0.000001f, &transformed );
        benchmark::DoNotOptimize( bTransformed );
        benchmark::DoNotOptimize( transformed );
    }
}

BENCHMARK( BM_PlaneAffineTransform );
