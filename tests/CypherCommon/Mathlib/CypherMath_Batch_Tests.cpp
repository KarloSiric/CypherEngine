//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Mathlib/CypherMath_Batch_Tests.cpp
//  Purpose: Tests SIMD-friendly four-lane and arbitrary-count math.
//  Details: Every batch result is checked against the scalar contract, including
//           in-place writes and tails that do not fill a complete SIMD register.
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

void RequireVec3Near( vec3_t actual, vec3_t expected, f32 margin = 0.00001f )
{
    REQUIRE( actual.x == Approx( expected.x ).margin( margin ) );
    REQUIRE( actual.y == Approx( expected.y ).margin( margin ) );
    REQUIRE( actual.z == Approx( expected.z ).margin( margin ) );
}

} // namespace

TEST_CASE( "batch vectors retain a stable aligned layout", "[CypherMath][Batch]" )
{
    REQUIRE( alignof( vec3_soa4_t ) == 16u );
    REQUIRE( alignof( f32_soa4_t ) == 16u );
    REQUIRE( MathBatch_CompiledBackend() != math_batch_backend_t::COUNT );
}

TEST_CASE( "batch gather arithmetic and scatter match scalar vectors",
           "[CypherMath][Batch]" )
{
    const vec3_t a[CY_MATH_BATCH_LANES]{
        Vec3_Make( 1.0f, 2.0f, 3.0f ),
        Vec3_Make( -2.0f, 5.0f, 4.0f ),
        Vec3_Make( 8.0f, -1.0f, 0.5f ),
        Vec3_Make( 0.0f, 0.0f, -7.0f )
    };
    const vec3_t b[CY_MATH_BATCH_LANES]{
        Vec3_Make( 4.0f, -3.0f, 2.0f ),
        Vec3_Make( 6.0f, 1.0f, -5.0f ),
        Vec3_Make( -2.0f, 9.0f, 1.5f ),
        Vec3_Make( 3.0f, -4.0f, 2.0f )
    };

    const vec3_soa4_t soaA = Vec3Soa4_Load( a );
    const vec3_soa4_t soaB = Vec3Soa4_Load( b );
    const vec3_soa4_t combined = Vec3Soa4_MulAdd( soaA, soaB, 0.25f );
    const f32_soa4_t dots = Vec3Soa4_Dot( soaA, soaB );
    vec3_t output[CY_MATH_BATCH_LANES]{};
    Vec3Soa4_Store( combined, output );

    for ( u32 i = 0u; i < CY_MATH_BATCH_LANES; ++i ) {
        RequireVec3Near( output[i], Vec3_MulAdd( a[i], b[i], 0.25f ) );
        REQUIRE( dots.lane[i] == Approx( Vec3_Dot( a[i], b[i] ) ) );
    }
}

TEST_CASE( "batch affine transforms match scalar operations and handle tails",
           "[CypherMath][Batch]" )
{
    constexpr usize cValues = 7u;
    vec3_t input[cValues]{
        Vec3_Make( 1.0f, 2.0f, 3.0f ),
        Vec3_Make( -4.0f, 5.0f, 6.0f ),
        Vec3_Make( 7.0f, -8.0f, 9.0f ),
        Vec3_Make( 0.5f, 1.5f, -2.5f ),
        Vec3_Make( 11.0f, 12.0f, 13.0f ),
        Vec3_Make( -14.0f, 15.0f, -16.0f ),
        Vec3_Make( 17.0f, -18.0f, 19.0f )
    };
    const mat4_t matrix = Mat4_FromTRS(
        Vec3_Make( 3.0f, -2.0f, 5.0f ),
        Quat_FromEulerXYZ( Vec3_Make( 0.2f, -0.4f, 0.7f ) ),
        Vec3_Make( 2.0f, 0.5f, 1.5f ) );

    vec3_t expectedPoints[cValues]{};
    vec3_t expectedDirections[cValues]{};
    for ( usize i = 0u; i < cValues; ++i ) {
        expectedPoints[i] = Mat4_TransformPointAffine( matrix, input[i] );
        expectedDirections[i] = Mat4_TransformDirection( matrix, input[i] );
    }

    vec3_t points[cValues]{};
    Vec3Batch_TransformPointsAffine( matrix, input, points, cValues );
    for ( usize i = 0u; i < cValues; ++i ) {
        RequireVec3Near( points[i], expectedPoints[i] );
    }

    Vec3Batch_TransformDirections( matrix, input, input, cValues );
    for ( usize i = 0u; i < cValues; ++i ) {
        RequireVec3Near( input[i], expectedDirections[i] );
    }

    Vec3Batch_TransformPointsAffine( matrix, nullptr, nullptr, 0u );
}

TEST_CASE( "batch dot handles complete registers and scalar tails",
           "[CypherMath][Batch]" )
{
    constexpr usize cValues = 9u;
    vec3_t a[cValues]{};
    vec3_t b[cValues]{};
    f32 output[cValues]{};
    for ( usize i = 0u; i < cValues; ++i ) {
        const f32 value = static_cast<f32>( i + 1u );
        a[i] = Vec3_Make( value, value * 2.0f, -value );
        b[i] = Vec3_Make( -value, 0.5f * value, 3.0f );
    }

    Vec3Batch_Dot( a, b, output, cValues );
    for ( usize i = 0u; i < cValues; ++i ) {
        REQUIRE( output[i] == Approx( Vec3_Dot( a[i], b[i] ) ) );
    }
}
