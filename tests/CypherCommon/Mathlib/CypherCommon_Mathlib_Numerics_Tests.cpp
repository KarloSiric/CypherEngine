//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Mathlib/CypherCommon_Mathlib_Numerics_Tests.cpp
//  Purpose: Tests numerical helpers used by tools and physics.
//  Details: Covers stable roots, degenerate closest-point queries, and explicit
//           linear and angular integration behavior.
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

void RequireVec3(
    vec3_t value,
    f32 x,
    f32 y,
    f32 z,
    f32 margin = 0.00005f )
{
    REQUIRE( value.x == Approx( x ).margin( margin ) );
    REQUIRE( value.y == Approx( y ).margin( margin ) );
    REQUIRE( value.z == Approx( z ).margin( margin ) );
}

} // namespace

TEST_CASE( "quadratic solving remains stable for separated roots",
           "[CypherCommon][Mathlib][Numerics][Quadratic]" )
{
    quadratic_solution_t solution{};
    REQUIRE( Numerics_TrySolveQuadratic(
        1.0, 100000000.0, 1.0, 1.0e-15, 1.0e-12, &solution ) );
    REQUIRE( solution.count == polynomial_solution_count_t::TWO );
    REQUIRE( solution.root0 == Approx( -100000000.0 ).epsilon( 1.0e-14 ) );
    REQUIRE( solution.root1 == Approx( -1.0e-8 ).epsilon( 1.0e-14 ) );
    REQUIRE( solution.root0 <= solution.root1 );
}

TEST_CASE( "quadratic solving classifies repeated, linear, and absent roots",
           "[CypherCommon][Mathlib][Numerics][Quadratic]" )
{
    quadratic_solution_t solution{};
    REQUIRE( Numerics_TrySolveQuadratic(
        1.0, -2.0, 1.0, 1.0e-15, 1.0e-12, &solution ) );
    REQUIRE( solution.count == polynomial_solution_count_t::ONE );
    REQUIRE( solution.root0 == Approx( 1.0 ) );

    REQUIRE( Numerics_TrySolveQuadratic(
        0.0, 2.0, -8.0, 1.0e-15, 1.0e-12, &solution ) );
    REQUIRE( solution.count == polynomial_solution_count_t::ONE );
    REQUIRE( solution.root0 == Approx( 4.0 ) );

    REQUIRE( Numerics_TrySolveQuadratic(
        1.0, 0.0, 1.0, 1.0e-15, 1.0e-12, &solution ) );
    REQUIRE( solution.count == polynomial_solution_count_t::ZERO );

    REQUIRE( Numerics_TrySolveQuadratic(
        0.0, 0.0, 0.0, 1.0e-15, 1.0e-12, &solution ) );
    REQUIRE( solution.count == polynomial_solution_count_t::INFINITE );
}

TEST_CASE( "closest segment points cover crossing, skew, and point segments",
           "[CypherCommon][Mathlib][Numerics][ClosestPoint]" )
{
    segment_closest_points_t closest{};
    REQUIRE( Numerics_TryClosestSegmentPoints(
        Segment_Make( Vec3_Make( -1.0f, 0.0f, 0.0f ),
                      Vec3_Make( 1.0f, 0.0f, 0.0f ) ),
        Segment_Make( Vec3_Make( 0.0f, -1.0f, 0.0f ),
                      Vec3_Make( 0.0f, 1.0f, 0.0f ) ),
        0.000001, &closest ) );
    RequireVec3( closest.pointA, 0.0f, 0.0f, 0.0f );
    RequireVec3( closest.pointB, 0.0f, 0.0f, 0.0f );
    REQUIRE( closest.parameterA == Approx( 0.5f ) );
    REQUIRE( closest.parameterB == Approx( 0.5f ) );
    REQUIRE( closest.distanceSquared == Approx( 0.0f ) );

    REQUIRE( Numerics_TryClosestSegmentPoints(
        Segment_Make( CY_VEC3_ZERO, CY_VEC3_ZERO ),
        Segment_Make( Vec3_Make( 2.0f, -1.0f, 0.0f ),
                      Vec3_Make( 2.0f, 1.0f, 0.0f ) ),
        0.000001, &closest ) );
    RequireVec3( closest.pointA, 0.0f, 0.0f, 0.0f );
    RequireVec3( closest.pointB, 2.0f, 0.0f, 0.0f );
    REQUIRE( closest.distanceSquared == Approx( 4.0f ) );

    REQUIRE( Numerics_TryClosestSegmentPoints(
        Segment_Make( Vec3_Make( 0.0f, 0.0f, 0.0f ),
                      Vec3_Make( 2.0f, 0.0f, 0.0f ) ),
        Segment_Make( Vec3_Make( 0.0f, 1.0f, 0.0f ),
                      Vec3_Make( 2.0f, 1.0f, 0.0f ) ),
        0.000001, &closest ) );
    REQUIRE( closest.distanceSquared == Approx( 1.0f ) );
}

TEST_CASE( "semi-implicit integration updates velocity before position",
           "[CypherCommon][Mathlib][Numerics][Integration]" )
{
    vec3_t position{};
    vec3_t velocity{};
    REQUIRE( Numerics_TryIntegrateLinearSemiImplicit(
        Vec3_Make( 1.0f, 2.0f, 3.0f ),
        Vec3_Make( 2.0f, 0.0f, 0.0f ),
        Vec3_Make( 0.0f, 4.0f, 0.0f ),
        0.5f, &position, &velocity ) );
    RequireVec3( velocity, 2.0f, 2.0f, 0.0f );
    RequireVec3( position, 2.0f, 3.0f, 3.0f );
}

TEST_CASE( "angular integration follows the right-handed axis convention",
           "[CypherCommon][Mathlib][Numerics][Integration]" )
{
    quat_t orientation{};
    REQUIRE( Numerics_TryIntegrateAngularVelocity(
        CY_QUAT_IDENTITY,
        Vec3_Make( 0.0f, 0.0f, CY_HALF_PI_F ),
        angular_velocity_space_t::WORLD,
        1.0f,
        0.000001f,
        &orientation ) );
    REQUIRE( Quat_IsUnit( orientation, 0.00001f ) );
    RequireVec3(
        Quat_RotateVectorUnit( orientation, CY_VEC3_FORWARD ),
        0.0f, 1.0f, 0.0f, 0.0001f );
}
