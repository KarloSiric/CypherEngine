//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Mathlib/CypherCommon_Mathlib_ModuleCoverage_Bench.cpp
//  Purpose: Benchmarks Mathlib implementation modules not covered elsewhere.
//  Details: Each case targets a representative hot operation and keeps inputs
//           observable so compiler optimization cannot erase the measured work.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMath.h"

#include <benchmark/benchmark.h>

using namespace cypher::math;

static void BM_ScalarTrigonometryAndWrap( benchmark::State &state )
{
    f32 angle = 0.731f;
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( angle );
        f32 sine = 0.0f;
        f32 cosine = 0.0f;
        Scalar_SinCos( angle, &sine, &cosine );
        angle = Scalar_WrapRadiansSigned(
            angle + sine * 0.0001f + cosine * 0.0002f );
        benchmark::DoNotOptimize( angle );
    }
}
BENCHMARK( BM_ScalarTrigonometryAndWrap );

static void BM_AngleNormalizeLerpAndTrig( benchmark::State &state )
{
    angle_t angle = Angle_FromDegrees( 725.0f );
    const angle_t target = Angle_FromDegrees( -170.0f );
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( angle );
        f32 sine = 0.0f;
        f32 cosine = 0.0f;
        angle = Angle_NormalizeSigned(
            Angle_LerpShortest( angle, target, 0.01f ) );
        Angle_SinCos( angle, &sine, &cosine );
        benchmark::DoNotOptimize( angle );
        benchmark::DoNotOptimize( sine );
        benchmark::DoNotOptimize( cosine );
    }
}
BENCHMARK( BM_AngleNormalizeLerpAndTrig );

static void BM_Vector2ProjectionAndMove( benchmark::State &state )
{
    vec2_t value = Vec2_Make( 17.0f, -9.0f );
    const vec2_t target = Vec2_Make( -30.0f, 45.0f );
    const vec2_t axis = Vec2_NormalizeUnchecked( Vec2_Make( 2.0f, 3.0f ) );
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( value );
        value = Vec2_Add(
            Vec2_MoveTowards( value, target, 0.25f ),
            Vec2_RejectFromUnit( value, axis ) );
        benchmark::DoNotOptimize( value );
    }
}
BENCHMARK( BM_Vector2ProjectionAndMove );

static void BM_Vector4NormalizeAndLerp( benchmark::State &state )
{
    vec4_t value = Vec4_Make( 1.0f, 2.0f, 3.0f, 4.0f );
    const vec4_t target = Vec4_Make( -4.0f, 3.0f, -2.0f, 1.0f );
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( value );
        value = Vec4_NormalizeUnchecked( Vec4_Lerp( value, target, 0.01f ) );
        benchmark::DoNotOptimize( value );
    }
}
BENCHMARK( BM_Vector4NormalizeAndLerp );

static void BM_RayAndSegmentQueries( benchmark::State &state )
{
    const ray_t ray = Ray_Make(
        Vec3_Make( 1.0f, 2.0f, 3.0f ),
        Vec3_Make( 2.0f, -1.0f, 0.5f ) );
    const segment_t segment = Segment_Make(
        Vec3_Make( -10.0f, 1.0f, 4.0f ),
        Vec3_Make( 20.0f, 7.0f, -8.0f ) );
    const vec3_t point = Vec3_Make( 5.0f, 11.0f, 2.0f );
    for ( auto _ : state ) {
        f32 parameter = 0.0f;
        bool_t found = Ray_TryClosestParameterToPoint(
            ray, point, 0.000001f, &parameter );
        vec3_t closest = Segment_ClosestPoint( segment, point );
        benchmark::DoNotOptimize( found );
        benchmark::DoNotOptimize( parameter );
        benchmark::DoNotOptimize( closest );
    }
}
BENCHMARK( BM_RayAndSegmentQueries );

static void BM_TriangleBarycentricAndClosestPoint( benchmark::State &state )
{
    const triangle3_t triangle = Triangle3_Make(
        Vec3_Make( -2.0f, -3.0f, 0.0f ),
        Vec3_Make( 8.0f, -1.0f, 1.0f ),
        Vec3_Make( 1.0f, 9.0f, -2.0f ) );
    const vec3_t point = Vec3_Make( 3.0f, 2.0f, 5.0f );
    for ( auto _ : state ) {
        vec3_t barycentric{};
        bool_t found = Triangle3_TryBarycentric(
            triangle, point, 0.000001f, &barycentric );
        vec3_t closest = Triangle3_ClosestPoint( triangle, point );
        benchmark::DoNotOptimize( found );
        benchmark::DoNotOptimize( barycentric );
        benchmark::DoNotOptimize( closest );
    }
}
BENCHMARK( BM_TriangleBarycentricAndClosestPoint );

static void BM_ConvexPolygonClip( benchmark::State &state )
{
    constexpr vec3_t polygon[]{
        { -4.0f, -3.0f, 0.0f }, { 2.0f, -5.0f, 0.0f },
        { 7.0f, -1.0f, 0.0f }, { 6.0f, 5.0f, 0.0f },
        { 0.0f, 8.0f, 0.0f }, { -6.0f, 3.0f, 0.0f }
    };
    vec3_t output[7]{};
    const plane_t plane = Plane_Make(
        Vec3_NormalizeUnchecked( Vec3_Make( 1.0f, 1.0f, 0.0f ) ), -1.0f );
    for ( auto _ : state ) {
        polygon_clip_result_t result = Clip_PolygonAgainstPlane(
            polygon, 6u, plane, 0.00001f, output, 7u );
        benchmark::DoNotOptimize( result );
        benchmark::DoNotOptimize( output );
        benchmark::ClobberMemory();
    }
}
BENCHMARK( BM_ConvexPolygonClip );

static void BM_Polygon3AreaContainment( benchmark::State &state )
{
    constexpr vec3_t polygon[]{
        { 0.0f, 0.0f, 2.0f }, { 8.0f, 0.0f, 2.0f },
        { 10.0f, 4.0f, 2.0f }, { 4.0f, 9.0f, 2.0f },
        { -2.0f, 5.0f, 2.0f }
    };
    polygon3_basis_t basis{};
    bool_t built = Polygon3_TryBasis(
        polygon, 5u, 0.000001f, &basis );
    benchmark::DoNotOptimize( built );
    vec2_t scratch[5]{};
    for ( auto _ : state ) {
        f32 area = 0.0f;
        vec3_t centroid{};
        bool_t hasArea = Polygon3_TryAreaCentroid(
            polygon, 5u, basis, 0.000001, &area, &centroid );
        bool_t contains = Polygon3_ContainsPoint(
            polygon, 5u, basis, Vec3_Make( 3.0f, 3.0f, 2.0f ),
            0.00001f, 0.00001f, true, scratch, 5u );
        benchmark::DoNotOptimize( hasArea );
        benchmark::DoNotOptimize( contains );
        benchmark::DoNotOptimize( area );
        benchmark::DoNotOptimize( centroid );
    }
}
BENCHMARK( BM_Polygon3AreaContainment );

static void BM_GizmoAxisPicking( benchmark::State &state )
{
    const ray_t ray = Ray_Make(
        Vec3_Make( 2.0f, 0.1f, 5.0f ),
        Vec3_Make( 0.0f, 0.0f, -1.0f ) );
    for ( auto _ : state ) {
        gizmo_axis_hit_t hit{};
        bool_t found = Gizmo_TryHitAxis(
            ray, CY_VEC3_ZERO, CY_VEC3_FORWARD,
            0.000001f, 0.000001f, 0.2f, &hit );
        benchmark::DoNotOptimize( found );
        benchmark::DoNotOptimize( hit );
    }
}
BENCHMARK( BM_GizmoAxisPicking );

static void BM_NumericsQuadraticAndIntegration( benchmark::State &state )
{
    vec3_t position = Vec3_Make( 10.0f, -4.0f, 2.0f );
    vec3_t velocity = Vec3_Make( 1.0f, 2.0f, 3.0f );
    const vec3_t acceleration = Vec3_Make( 0.0f, 0.0f, -9.81f );
    for ( auto _ : state ) {
        quadratic_solution_t roots{};
        bool_t solved = Numerics_TrySolveQuadratic(
            1.0, -3.0, -4.0, 1.0e-12, 1.0e-12, &roots );
        vec3_t nextPosition{};
        vec3_t nextVelocity{};
        bool_t integrated = Numerics_TryIntegrateLinearSemiImplicit(
            position, velocity, acceleration, 1.0f / 60.0f,
            &nextPosition, &nextVelocity );
        position = nextPosition;
        velocity = nextVelocity;
        benchmark::DoNotOptimize( solved );
        benchmark::DoNotOptimize( integrated );
        benchmark::DoNotOptimize( roots );
        benchmark::DoNotOptimize( position );
    }
}
BENCHMARK( BM_NumericsQuadraticAndIntegration );
