//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Mathlib/CypherCommon_Mathlib_AuthoringProperties_Tests.cpp
//  Purpose: Tests deterministic math used by authoring tools and serialization.
//  Details: Round-trip properties cover grids, cubic curves, planar UV mapping,
//           decomposed transforms, and complete small quantization codebooks.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMath.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace cypher::math;
using Catch::Approx;

namespace
{

void RequireVec2Near( vec2_t actual, vec2_t expected, f32 margin = 0.00005f )
{
    REQUIRE( actual.x == Approx( expected.x ).margin( margin ) );
    REQUIRE( actual.y == Approx( expected.y ).margin( margin ) );
}

void RequireVec3Near( vec3_t actual, vec3_t expected, f32 margin = 0.00005f )
{
    REQUIRE( actual.x == Approx( expected.x ).margin( margin ) );
    REQUIRE( actual.y == Approx( expected.y ).margin( margin ) );
    REQUIRE( actual.z == Approx( expected.z ).margin( margin ) );
}

} // namespace

TEST_CASE( "Snap modes are deterministic around positive and negative cells",
           "[CypherCommon][Mathlib][Snap][Property]" )
{
    struct snap_case_t {
        f32 value;
        snap_mode_t mode;
        f32 expected;
    };
    constexpr snap_case_t cases[]{
        { 1.24f, snap_mode_t::NEAREST, 1.0f },
        { 1.24f, snap_mode_t::FLOOR, 1.0f },
        { 1.24f, snap_mode_t::CEIL, 1.5f },
        { -1.24f, snap_mode_t::NEAREST, -1.0f },
        { -1.24f, snap_mode_t::FLOOR, -1.5f },
        { -1.24f, snap_mode_t::CEIL, -1.0f }
    };
    for ( const snap_case_t &test : cases ) {
        f32 snapped = 99.0f;
        REQUIRE( Snap_TryScalar(
            test.value, 0.5f, 0.0f, test.mode, &snapped ) );
        REQUIRE( snapped == Approx( test.expected ) );
    }

    const vec3_t step = Vec3_Make( 0.25f, 2.0f, 4.0f );
    const vec3_t origin = Vec3_Make( 0.125f, -3.0f, 7.0f );
    constexpr grid_coord3_t grids[]{
        { 0, 0, 0 },
        { 1, -1, 2 },
        { -4096, 8192, -16384 },
        { 100000, -100000, 50000 }
    };
    for ( grid_coord3_t source : grids ) {
        vec3_t world{};
        grid_coord3_t restored{};
        REQUIRE( Snap_TryGridToWorld( source, step, origin, &world ) );
        REQUIRE( Snap_TryWorldToGrid(
            world, step, origin, snap_mode_t::NEAREST, &restored ) );
        REQUIRE( restored.x == source.x );
        REQUIRE( restored.y == source.y );
        REQUIRE( restored.z == source.z );
    }

    f32 output = 42.0f;
    REQUIRE_FALSE( Snap_TryScalar(
        1.0f, 0.0f, 0.0f, snap_mode_t::NEAREST, &output ) );
    REQUIRE( output == 0.0f );
}

TEST_CASE( "Bezier subdivision evaluates to the unsplit curve",
           "[CypherCommon][Mathlib][Spline][Property]" )
{
    const cubic_bezier3_t curve{
        Vec3_Make( -2.0f, 1.0f, 0.0f ),
        Vec3_Make( 3.0f, 8.0f, -1.0f ),
        Vec3_Make( 7.0f, -4.0f, 5.0f ),
        Vec3_Make( 11.0f, 2.0f, 3.0f )
    };
    constexpr f32 splits[]{ 0.2f, 0.5f, 0.8f };
    constexpr f32 localParameters[]{ 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };

    for ( f32 split : splits ) {
        cubic_bezier3_t left{};
        cubic_bezier3_t right{};
        Spline_BezierSplit( curve, split, &left, &right );
        for ( f32 local : localParameters ) {
            CAPTURE( split, local );
            RequireVec3Near(
                Spline_BezierPoint( left, local ),
                Spline_BezierPoint( curve, split * local ), 0.0001f );
            RequireVec3Near(
                Spline_BezierPoint( right, local ),
                Spline_BezierPoint(
                    curve, split + ( 1.0f - split ) * local ), 0.0001f );
        }
    }

    spline_arc_sample_t samples[65]{};
    spline_arc_table_result_t result{};
    REQUIRE( Spline_TryBuildBezierArcTable(
        curve, 65u, samples, 65u, &result ) );
    REQUIRE( result.cSamplesWritten == 65u );
    for ( usize i = 1u; i < result.cSamplesWritten; ++i ) {
        REQUIRE( samples[i].parameter > samples[i - 1u].parameter );
        REQUIRE( samples[i].distance >= samples[i - 1u].distance );
        f32 parameter = -1.0f;
        REQUIRE( Spline_TryArcParameterAtDistance(
            samples, result.cSamplesWritten, samples[i].distance,
            &parameter ) );
        REQUIRE( parameter == Approx( samples[i].parameter ).margin( 0.00001f ) );
    }
}

TEST_CASE( "Planar UV mapping round trips rotation mirroring and normal offset",
           "[CypherCommon][Mathlib][UV][Property]" )
{
    constexpr f32 rotations[]{ 0.0f, 37.0f, 123.0f };
    constexpr vec2_t scales[]{
        { 0.25f, 0.5f },
        { -0.25f, 0.5f },
        { 2.0f, -4.0f }
    };
    constexpr vec2_t coordinates[]{
        { 0.0f, 0.0f },
        { 1.25f, -3.5f },
        { -20.0f, 11.0f }
    };

    for ( f32 rotation : rotations ) {
        for ( vec2_t scale : scales ) {
            planar_uv_mapping_t mapping{};
            REQUIRE( Uv_TryBuildPlanarMapping(
                Vec3_Make( 3.0f, -7.0f, 11.0f ),
                Vec3_Make( 1.0f, 2.0f, 3.0f ), CY_VEC3_UP,
                scale, Angle_FromDegrees( rotation ),
                Vec2_Make( 0.25f, -0.75f ), 0.000001f, &mapping ) );

            for ( vec2_t uv : coordinates ) {
                vec3_t world{};
                vec2_t projected{};
                REQUIRE( Uv_TryUnprojectPlanarPoint(
                    mapping, uv, 2.5f, 0.000001f, &world ) );
                REQUIRE( Uv_TryProjectPlanarPoint(
                    mapping, world, 0.000001f, &projected ) );
                CAPTURE( rotation, scale.x, scale.y, uv.x, uv.y );
                RequireVec2Near( projected, uv, 0.0001f );

                vec3_t restored{};
                REQUIRE( Uv_TryUnprojectPlanarPoint(
                    mapping, projected, 2.5f, 0.000001f, &restored ) );
                RequireVec3Near( restored, world, 0.0002f );
            }
        }
    }
}

TEST_CASE( "TRS inverse helpers restore points and directions across reflections",
           "[CypherCommon][Mathlib][Transform][Property]" )
{
    constexpr vec3_t scales[]{
        { 1.0f, 1.0f, 1.0f },
        { 2.0f, 3.0f, 4.0f },
        { -2.0f, 3.0f, 4.0f },
        { -2.0f, -3.0f, -4.0f }
    };
    constexpr vec3_t points[]{
        { 0.0f, 0.0f, 0.0f },
        { 1.0f, -2.0f, 3.0f },
        { -10.0f, 5.0f, 0.25f }
    };

    for ( vec3_t scale : scales ) {
        const transform_t transform = Transform_Make(
            Vec3_Make( 7.0f, -3.0f, 2.0f ),
            Quat_FromEulerXYZ( Vec3_Make( 0.4f, -0.8f, 1.2f ) ),
            scale );
        for ( vec3_t source : points ) {
            vec3_t restoredPoint{};
            vec3_t restoredDirection{};
            REQUIRE( Transform_TryInversePoint(
                transform, Transform_TransformPoint( transform, source ),
                0.000001f, &restoredPoint ) );
            REQUIRE( Transform_TryInverseDirection(
                transform, Transform_TransformDirection( transform, source ),
                0.000001f, &restoredDirection ) );
            RequireVec3Near( restoredPoint, source, 0.0002f );
            RequireVec3Near( restoredDirection, source, 0.0002f );
        }
    }

    const transform_t a = CY_TRANSFORM_IDENTITY;
    const transform_t b = Transform_Make(
        Vec3_Make( 5.0f, 6.0f, 7.0f ),
        Quat_FromEulerXYZ( Vec3_Make( 0.5f, 0.25f, -0.75f ) ),
        Vec3_Make( 2.0f, 3.0f, 4.0f ) );
    REQUIRE( Transform_NearlyEquals(
        Transform_InterpolateClamped( a, b, -2.0f ), a,
        0.0f, 0.0f, 0.000001f ) );
    REQUIRE( Transform_NearlyEquals(
        Transform_InterpolateClamped( a, b, 2.0f ), b,
        0.0f, 0.0f, 0.000001f ) );
}

TEST_CASE( "Small quantization codebooks decode and re-encode exactly",
           "[CypherCommon][Mathlib][Quantization][Property]" )
{
    for ( u32 cBits = 1u; cBits <= 10u; ++cBits ) {
        const u32 maximumCode = Quantization_MaxCode( cBits );
        for ( u32 code = 0u; code <= maximumCode; ++code ) {
            CAPTURE( cBits, code );
            f32 unorm = 0.0f;
            u32 unormRoundTrip = 0u;
            REQUIRE( Quantization_TryDecodeUnorm( code, cBits, &unorm ) );
            REQUIRE( Quantization_TryEncodeUnorm(
                unorm, cBits, &unormRoundTrip ) );
            REQUIRE( unormRoundTrip == code );

            f32 snorm = 0.0f;
            u32 snormRoundTrip = 0u;
            REQUIRE( Quantization_TryDecodeSnorm( code, cBits, &snorm ) );
            REQUIRE( Quantization_TryEncodeSnorm(
                snorm, cBits, &snormRoundTrip ) );
            REQUIRE( snormRoundTrip == code );

            angle_t angle{};
            u32 angleRoundTrip = 0u;
            REQUIRE( Quantization_TryDecodeAngle( code, cBits, &angle ) );
            REQUIRE( Quantization_TryEncodeAngle(
                angle, cBits, &angleRoundTrip ) );
            REQUIRE( angleRoundTrip == code );
        }
    }

    f32 value = 42.0f;
    REQUIRE_FALSE( Quantization_TryDecodeUnorm( 0u, 0u, &value ) );
    REQUIRE( value == 0.0f );
    REQUIRE_FALSE( Quantization_TryDecodeUnorm( 2u, 1u, &value ) );
    REQUIRE( value == 0.0f );
}
