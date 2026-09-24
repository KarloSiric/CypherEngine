//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Snap_Tests.cpp
//  Purpose: Verifies deterministic snapping functions.
//  Details: Covers Gate 4 constraint acceptance: grid snap idempotency,
//           tie-breaking toward positive infinity, angle snap, component
//           snap nearest-neighbor with documented tie behavior, disabled
//           snap (zero/negative spacing), and no implicit weld.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Snap.h"

#include "CypherMath.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <limits>

namespace cypher::editor::geometry {

using cypher::math::Vec3d_Make;
using Catch::Approx;

// ---------------------------------------------------------------------------
// Grid snap — scalar
// ---------------------------------------------------------------------------

TEST_CASE( "Snap: grid scalar rounds to nearest multiple",
           "[Gate4][Constraints]" )
{
    REQUIRE( Snap_GridScalar( 0.7, 0.5 ) == Approx( 0.5 ) );
    REQUIRE( Snap_GridScalar( 0.8, 0.5 ) == Approx( 1.0 ) );
    REQUIRE( Snap_GridScalar( 1.0, 0.5 ) == Approx( 1.0 ) );
    REQUIRE( Snap_GridScalar( 1.24, 0.25 ) == Approx( 1.25 ) );
}

TEST_CASE( "Snap: grid scalar midpoint rounds toward positive infinity",
           "[Gate4][Constraints]" )
{
    // Exact midpoint: 0.25 is equidistant between 0.0 and 0.5 at
    // spacing 0.5. floor(0.25/0.5 + 0.5) = floor(1.0) = 1.0 → 0.5.
    REQUIRE( Snap_GridScalar( 0.25, 0.5 ) == Approx( 0.5 ) );

    // Negative midpoint: -0.25 → floor(-0.5+0.5) = floor(0.0) = 0 → 0.0.
    REQUIRE( Snap_GridScalar( -0.25, 0.5 ) == Approx( 0.0 ) );
}

TEST_CASE( "Snap: grid scalar is idempotent", "[Gate4][Constraints]" )
{
    const common::f64 once = Snap_GridScalar( 1.37, 0.25 );
    const common::f64 twice = Snap_GridScalar( once, 0.25 );
    REQUIRE( once == Approx( twice ) );
}

TEST_CASE( "Snap: grid scalar disabled with zero spacing",
           "[Gate4][Constraints]" )
{
    REQUIRE( Snap_GridScalar( 1.37, 0.0 ) == Approx( 1.37 ) );
}

TEST_CASE( "Snap: grid scalar disabled with negative spacing",
           "[Gate4][Constraints]" )
{
    REQUIRE( Snap_GridScalar( 1.37, -1.0 ) == Approx( 1.37 ) );
}

TEST_CASE( "Snap: non-finite scalar inputs are not propagated into valid values",
           "[Gate4][Constraints][numeric]" )
{
    const common::f64 nan =
        std::numeric_limits<common::f64>::quiet_NaN();
    const common::f64 infinity =
        std::numeric_limits<common::f64>::infinity();

    CHECK( Snap_GridScalar( 3.0, nan ) == 3.0 );
    CHECK( Snap_GridScalar( 3.0, infinity ) == 3.0 );
    CHECK( std::isnan( Snap_GridScalar( nan, 1.0 ) ) );
    CHECK( Snap_GridScalar( infinity, 1.0 ) == infinity );
}

// ---------------------------------------------------------------------------
// Grid snap — 3D point
// ---------------------------------------------------------------------------

TEST_CASE( "Snap: grid point snaps each axis independently",
           "[Gate4][Constraints]" )
{
    const math::vec3d_t result = Snap_GridPoint(
        Vec3d_Make( 1.3, 2.7, 4.1 ), 0.5 );
    REQUIRE( result.x == Approx( 1.5 ) );
    REQUIRE( result.y == Approx( 2.5 ) );
    REQUIRE( result.z == Approx( 4.0 ) );
}

TEST_CASE( "Snap: grid point is idempotent", "[Gate4][Constraints]" )
{
    const math::vec3d_t once = Snap_GridPoint(
        Vec3d_Make( 1.3, 2.7, 4.1 ), 0.25 );
    const math::vec3d_t twice = Snap_GridPoint( once, 0.25 );
    REQUIRE( once.x == Approx( twice.x ) );
    REQUIRE( once.y == Approx( twice.y ) );
    REQUIRE( once.z == Approx( twice.z ) );
}

// ---------------------------------------------------------------------------
// Angle snap
// ---------------------------------------------------------------------------

TEST_CASE( "Snap: angle rounds to nearest increment",
           "[Gate4][Constraints]" )
{
    const common::f64 pi = std::acos( -1.0 );
    const common::f64 deg15 = pi / 12.0;

    // 20 degrees → nearest 15-degree multiple is 15 degrees.
    const common::f64 deg20 = pi / 9.0;
    REQUIRE( Snap_Angle( deg20, deg15 ) == Approx( deg15 ) );

    // 40 degrees → nearest 15-degree multiple is 45 degrees.
    const common::f64 deg40 = pi * 40.0 / 180.0;
    const common::f64 deg45 = pi / 4.0;
    REQUIRE( Snap_Angle( deg40, deg15 ) == Approx( deg45 ) );
}

TEST_CASE( "Snap: angle is idempotent", "[Gate4][Constraints]" )
{
    const common::f64 inc = 0.1;
    const common::f64 once = Snap_Angle( 0.37, inc );
    const common::f64 twice = Snap_Angle( once, inc );
    REQUIRE( once == Approx( twice ) );
}

TEST_CASE( "Snap: angle disabled with zero increment",
           "[Gate4][Constraints]" )
{
    REQUIRE( Snap_Angle( 1.23, 0.0 ) == Approx( 1.23 ) );
}

// ---------------------------------------------------------------------------
// Component snap
// ---------------------------------------------------------------------------

TEST_CASE( "Snap: component finds nearest target",
           "[Gate4][Constraints]" )
{
    const math::vec3d_t targets[] = {
        Vec3d_Make( 0.0, 0.0, 0.0 ),
        Vec3d_Make( 5.0, 0.0, 0.0 ),
        Vec3d_Make( 10.0, 0.0, 0.0 ),
    };

    const snap_component_result_t result = Snap_NearestComponent(
        Vec3d_Make( 4.0, 0.0, 0.0 ), targets, 3u, 2.0 );

    REQUIRE( result.bFound );
    REQUIRE( result.iTarget == 1u );
    REQUIRE( result.fDistanceSquared == Approx( 1.0 ) );
}

TEST_CASE( "Snap: component reports no match when out of range",
           "[Gate4][Constraints]" )
{
    const math::vec3d_t targets[] = {
        Vec3d_Make( 0.0, 0.0, 0.0 ),
        Vec3d_Make( 10.0, 0.0, 0.0 ),
    };

    const snap_component_result_t result = Snap_NearestComponent(
        Vec3d_Make( 5.0, 0.0, 0.0 ), targets, 2u, 1.0 );

    REQUIRE_FALSE( result.bFound );
}

TEST_CASE( "Snap: component threshold is inclusive",
           "[Gate4][Constraints][contract]" )
{
    const math::vec3d_t targets[] = {
        Vec3d_Make( 3.0, 4.0, 0.0 )
    };
    const snap_component_result_t result = Snap_NearestComponent(
        Vec3d_Make( 0.0, 0.0, 0.0 ), targets, 1u, 5.0 );

    REQUIRE( result.bFound );
    CHECK( result.iTarget == 0u );
    CHECK( result.fDistanceSquared == Approx( 25.0 ) );
}

TEST_CASE( "Snap: component ignores non-finite input deterministically",
           "[Gate4][Constraints][numeric]" )
{
    const common::f64 nan =
        std::numeric_limits<common::f64>::quiet_NaN();
    const math::vec3d_t targets[] = {
        Vec3d_Make( nan, 0.0, 0.0 ),
        Vec3d_Make( 1.0, 0.0, 0.0 )
    };

    const snap_component_result_t valid = Snap_NearestComponent(
        Vec3d_Make( 0.0, 0.0, 0.0 ), targets, 2u, 2.0 );
    REQUIRE( valid.bFound );
    CHECK( valid.iTarget == 1u );

    CHECK_FALSE( Snap_NearestComponent(
        Vec3d_Make( nan, 0.0, 0.0 ), targets, 2u, 2.0 ).bFound );
    CHECK_FALSE( Snap_NearestComponent(
        Vec3d_Make( 0.0, 0.0, 0.0 ), targets, 2u, nan ).bFound );
}

TEST_CASE( "Snap: component tie-break favors lower index",
           "[Gate4][Constraints]" )
{
    // Two targets equidistant from the candidate.
    const math::vec3d_t targets[] = {
        Vec3d_Make( 1.0, 0.0, 0.0 ),
        Vec3d_Make( -1.0, 0.0, 0.0 ),
    };

    const snap_component_result_t result = Snap_NearestComponent(
        Vec3d_Make( 0.0, 0.0, 0.0 ), targets, 2u, 5.0 );

    REQUIRE( result.bFound );
    // Both are distance 1.0. Lower index (0) wins.
    REQUIRE( result.iTarget == 0u );
}

TEST_CASE( "Snap: component with null targets returns no match",
           "[Gate4][Constraints]" )
{
    const snap_component_result_t result = Snap_NearestComponent(
        Vec3d_Make( 0.0, 0.0, 0.0 ), nullptr, 0u, 1.0 );
    REQUIRE_FALSE( result.bFound );
}

TEST_CASE( "Snap: component does not perform implicit weld",
           "[Gate4][Constraints]" )
{
    // Even when a target is found, the function returns advisory data.
    // The caller's candidate point is not modified — it is a pure function.
    const math::vec3d_t targets[] = {
        Vec3d_Make( 1.0, 1.0, 1.0 ),
    };

    const math::vec3d_t candidate = Vec3d_Make( 0.9, 0.9, 0.9 );
    const snap_component_result_t result = Snap_NearestComponent(
        candidate, targets, 1u, 1.0 );

    REQUIRE( result.bFound );
    // The candidate itself was not modified (it's passed by value).
    REQUIRE( candidate.x == Approx( 0.9 ) );
    REQUIRE( candidate.y == Approx( 0.9 ) );
    REQUIRE( candidate.z == Approx( 0.9 ) );
}

} // namespace cypher::editor::geometry
