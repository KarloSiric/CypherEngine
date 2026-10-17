//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_UvTools_Tests.cpp
//  Purpose: Contract tests for the UV-view manipulations.
//  Details: Rotating or scaling about a pivot must leave the pivot's UV
//           unchanged while other points move exactly as a rotation/scale in
//           UV space about it; shifting adds to every UV; invalid edits leave
//           the record untouched; shear is refused.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_UvTools.h"
#include "CypherMath_UV.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>

namespace cypher::editor::geometry {

using Catch::Approx;
using math::vec2d_t;
using math::vec3d_t;
using math::Vec3d_Make;

namespace {

geometry_brush_side_attributes_t Floor() {
    geometry_brush_side_attributes_t rec{};
    REQUIRE( math::Uvd_TryBuildPlanarMapping( Vec3d_Make( 1, 2, 0 ), Vec3d_Make( 0, 0, 1 ), Vec3d_Make( 0, 1, 0 ), { 0.5, 0.25 }, 0.2,
                                              { 0.3, -0.4 }, 1e-12, &rec.uvProjection ) );
    return rec;
}

vec2d_t Uv( const geometry_brush_side_attributes_t &r, vec3d_t p ) {
    vec2d_t uv{};
    REQUIRE( math::Uvd_TryProjectPlanarPoint( r.uvProjection, p, 0.0, &uv ) );
    return uv;
}

} // namespace

TEST_CASE( "UV tools keep the pivot fixed", "[geometry][uvtools]" ) {
    const geometry_numerical_policy_t policy{};
    const vec3d_t pivot = Vec3d_Make( 3, 5, 0 ), other = Vec3d_Make( 4, 6.5, 0 );
    SECTION( "Rotate about a pivot" ) {
        geometry_brush_side_attributes_t r = Floor();
        const vec2d_t p0 = Uv( r, pivot ), q0 = Uv( r, other );
        REQUIRE( UvTools_TryRotateAbout( &r, pivot, 0.7, policy ) == geometry_status_t::OK );
        const vec2d_t p1 = Uv( r, pivot ), q1 = Uv( r, other );
        CHECK( p1.x == Approx( p0.x ).margin( 1e-12 ) );
        CHECK( p1.y == Approx( p0.y ).margin( 1e-12 ) );
        // The other point turned by 0.7 about the pivot in UV space.
        const double dx = q0.x - p0.x, dy = q0.y - p0.y;
        CHECK( q1.x - p1.x == Approx( std::cos( 0.7 ) * dx - std::sin( 0.7 ) * dy ) );
        CHECK( q1.y - p1.y == Approx( std::sin( 0.7 ) * dx + std::cos( 0.7 ) * dy ) );
    }
    SECTION( "Scale about a pivot" ) {
        geometry_brush_side_attributes_t r = Floor();
        const vec2d_t p0 = Uv( r, pivot ), q0 = Uv( r, other );
        REQUIRE( UvTools_TryScaleAbout( &r, pivot, { 2.0, 4.0 }, policy ) == geometry_status_t::OK );
        const vec2d_t p1 = Uv( r, pivot ), q1 = Uv( r, other );
        CHECK( p1.x == Approx( p0.x ).margin( 1e-12 ) );
        CHECK( p1.y == Approx( p0.y ).margin( 1e-12 ) );
        // A larger texture: UV distances from the pivot shrink.
        const double before = std::hypot( q0.x - p0.x, q0.y - p0.y ), after = std::hypot( q1.x - p1.x, q1.y - p1.y );
        CHECK( after < before );
        CHECK( UvTools_TryScaleAbout( &r, pivot, { 0.0, 1.0 }, policy ) == geometry_status_t::DEGENERATE );
    }
    SECTION( "Shift, refusals" ) {
        geometry_brush_side_attributes_t r = Floor();
        const vec2d_t q0 = Uv( r, other );
        REQUIRE( UvTools_TryShift( &r, { 0.25, -1.5 }, policy ) == geometry_status_t::OK );
        const vec2d_t q1 = Uv( r, other );
        CHECK( q1.x == Approx( q0.x + 0.25 ) );
        CHECK( q1.y == Approx( q0.y - 1.5 ) );
        const geometry_brush_side_attributes_t before = r;
        CHECK( UvTools_TryRotateAbout( &r, pivot, std::nan( "" ), policy ) == geometry_status_t::NUMERIC_FAILURE );
        CHECK( UvTools_TryShear( &r, { 0.1, 0.0 } ) == geometry_status_t::UNSUPPORTED );
        CHECK( r.uvProjection.offset.x == before.uvProjection.offset.x );
        CHECK( r.uvProjection.rotationRadians == before.uvProjection.rotationRadians );
    }
}

TEST_CASE( "A UV-space transform is held exactly, turning the axes only when it must", "[geometry][uvtools]" ) {
    const geometry_numerical_policy_t policy{};
    const vec3d_t points[] = { Vec3d_Make( 3, 5, 0 ), Vec3d_Make( 4, 6.5, 0 ), Vec3d_Make( -2, 1, 0 ), Vec3d_Make( 7, -3, 0 ) };
    const vec2d_t pivot{ 0.75, -1.25 };
    // Every point's UV must land at pivot + L * (uv - pivot).
    const auto expect = [&]( vec2d_t rowU, vec2d_t rowV ) {
        geometry_brush_side_attributes_t r = Floor();
        vec2d_t before[4];
        for ( int i = 0; i < 4; ++i ) { before[i] = Uv( r, points[i] ); }
        REQUIRE( UvTools_TryTransformUv( &r, rowU, rowV, pivot, policy ) == geometry_status_t::OK );
        for ( int i = 0; i < 4; ++i ) {
            const vec2d_t d{ before[i].x - pivot.x, before[i].y - pivot.y }, after = Uv( r, points[i] );
            CHECK( after.x == Approx( pivot.x + rowU.x * d.x + rowU.y * d.y ).margin( 1e-9 ) );
            CHECK( after.y == Approx( pivot.y + rowV.x * d.x + rowV.y * d.y ).margin( 1e-9 ) );
        }
        return r;
    };
    // Floor() is turned 0.2 rad, so a U-only scale in UV space needs new axes.
    const geometry_brush_side_attributes_t scaled = expect( { 2.0, 0.0 }, { 0.0, 1.0 } );
    CHECK( std::fabs( math::Vec3d_Dot( scaled.uvProjection.uAxis, scaled.uvProjection.vAxis ) ) < 1e-12 );
    CHECK( std::fabs( scaled.uvProjection.uAxis.z ) < 1e-12 ); // Still in the face plane.
    (void)expect( { 1.0, 0.4 }, { 0.0, 1.0 } );              // Shear along U.
    (void)expect( { 1.0, 0.0 }, { -0.3, 1.0 } );             // Shear along V.
    (void)expect( { -1.0, 0.0 }, { 0.0, 1.0 } );             // Mirror.
    // A uniform scale and a turn keep the axes and the mirror sense.
    const geometry_brush_side_attributes_t same = expect( { 0.0, -1.5 }, { 1.5, 0.0 } );
    const geometry_brush_side_attributes_t floor = Floor();
    CHECK( same.uvProjection.uAxis.x == Approx( floor.uvProjection.uAxis.x ) );
    CHECK( same.uvProjection.uAxis.y == Approx( floor.uvProjection.uAxis.y ) );
    CHECK( same.uvProjection.worldUnitsPerUv.x == Approx( floor.uvProjection.worldUnitsPerUv.x / 1.5 ) );
    CHECK( same.uvProjection.worldUnitsPerUv.y == Approx( floor.uvProjection.worldUnitsPerUv.y / 1.5 ) );
    // Singular maps are refused and leave the record alone.
    geometry_brush_side_attributes_t r = Floor();
    CHECK( UvTools_TryTransformUv( &r, { 1.0, 2.0 }, { 0.5, 1.0 }, pivot, policy ) == geometry_status_t::DEGENERATE );
    CHECK( r.uvProjection.offset.x == floor.uvProjection.offset.x );
}

} // namespace cypher::editor::geometry
