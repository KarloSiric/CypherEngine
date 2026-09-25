//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Workplane_Tests.cpp
//  Purpose: Tests workplanes: construction from faces and points, local
//           coordinates, grid snapping on a slope, and ray placement.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshSource.h"
#include "CypherGeometry_Snap.h"
#include "CypherGeometry_Workplane.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>

namespace cypher::editor::geometry {

using Catch::Approx;
using math::vec3d_t;
using math::Vec3d_Make;

namespace {

geometry_source_id_t Id( common::u64 v ) { return geometry_source_id_t{ v }; }

void CheckNear( vec3d_t a, vec3d_t b ) {
    CHECK( a.x == Approx( b.x ).margin( 1e-9 ) );
    CHECK( a.y == Approx( b.y ).margin( 1e-9 ) );
    CHECK( a.z == Approx( b.z ).margin( 1e-9 ) );
}

} // namespace

TEST_CASE( "A workplane from a sloped face follows its longest edge", "[geometry][workplane]" ) {
    // A 4 x 2 ramp rising along y: corners (0,0,0), (4,0,0), (4,2,2), (0,2,2).
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    mesh_source_description_t d{};
    REQUIRE( MeshSourceDescription_Init( &d, &allocator, Id( 1 ) ) == geometry_status_t::OK );
    const vec3d_t pts[4] = { { 0, 0, 0 }, { 4, 0, 0 }, { 4, 2, 2 }, { 0, 2, 2 } };
    common::u32 v[4];
    for ( int i = 0; i < 4; ++i ) {
        REQUIRE( MeshSourceDescription_TryAddVertex( &d, pts[i], Id( 10u + static_cast<common::u64>( i ) ), &v[i] ) == geometry_status_t::OK );
    }
    REQUIRE( MeshSourceDescription_TryAddFace( &d, common::span_t<const common::u32>{ v, 4 }, Id( 20 ), mesh_face_attributes_t{}, nullptr ) ==
             geometry_status_t::OK );
    mesh_source_t s{};
    REQUIRE( MeshSource_TryBuild( &d, &allocator, &s ) == geometry_status_t::OK );
    MeshSourceDescription_Shutdown( &d );
    geometry_mesh_face_handle_t hFace{};
    REQUIRE( MeshSource_TryFindFace( &s, Id( 20 ), &hFace ) );

    geometry_workplane_t wp{};
    REQUIRE( Workplane_TryFromFace( &s.mesh, hFace, &wp ) == geometry_status_t::OK );
    CHECK( Workplane_IsValid( wp, 1e-12 ) );
    CheckNear( wp.normal, common::GenerationPool_Get( &s.mesh.faces, hFace )->normal );
    // The longest edges run along x (length 4 vs the slope's 2.83).
    CHECK( std::fabs( wp.u.x ) == Approx( 1.0 ) );
    // Every corner lies in the plane (height 0), and round-trips.
    for ( const vec3d_t p : pts ) {
        const vec3d_t local = Workplane_ToLocal( wp, p );
        CHECK( local.z == Approx( 0.0 ).margin( 1e-12 ) );
        CheckNear( Workplane_ToWorld( wp, local ), p );
    }
    // Snapping on the slope: local coordinates become grid multiples, the
    // point stays on the ramp, and the ramp's own corners are grid points.
    const vec3d_t near = Vec3d_Make( 1.3, 0.9, 1.2 ); // off the plane
    const vec3d_t snapped = Workplane_SnapPoint( wp, near, 0.5, true );
    const vec3d_t local = Workplane_ToLocal( wp, snapped );
    CHECK( local.z == Approx( 0.0 ).margin( 1e-12 ) );
    CHECK( local.x / 0.5 == Approx( std::round( local.x / 0.5 ) ).margin( 1e-9 ) );
    CHECK( local.y / 0.5 == Approx( std::round( local.y / 0.5 ) ).margin( 1e-9 ) );
    CheckNear( Workplane_SnapPoint( wp, pts[1], 1.0, true ), pts[1] );
    // Snapping twice changes nothing.
    CheckNear( Workplane_SnapPoint( wp, snapped, 0.5, true ), snapped );
    MeshSource_Shutdown( &s );
}

TEST_CASE( "The default workplane snaps like the world grid", "[geometry][workplane]" ) {
    const geometry_workplane_t world{};
    CHECK( Workplane_IsValid( world, 1e-12 ) );
    const vec3d_t p = Vec3d_Make( 1.26, -3.74, 0.49 );
    const vec3d_t a = Workplane_SnapPoint( world, p, 0.25, false );
    const vec3d_t b = Snap_GridPoint( p, 0.25 );
    CHECK( a.x == b.x );
    CHECK( a.y == b.y );
    CHECK( a.z == b.z );
    // A non-positive spacing leaves the point where it is.
    CheckNear( Workplane_SnapPoint( world, p, 0.0, false ), p );
    CheckNear( Workplane_Project( world, p ), Vec3d_Make( p.x, p.y, 0.0 ) );
}

TEST_CASE( "Workplanes from points, and rays placed on them", "[geometry][workplane]" ) {
    geometry_workplane_t wp{};
    REQUIRE( Workplane_TryFromPoints( Vec3d_Make( 0, 0, 5 ), Vec3d_Make( 1, 0, 5 ), Vec3d_Make( 0, 1, 5 ), &wp ) == geometry_status_t::OK );
    CHECK( Workplane_IsValid( wp, 1e-12 ) );
    CheckNear( wp.normal, Vec3d_Make( 0, 0, 1 ) );
    double t = 0.0;
    vec3d_t hit{};
    REQUIRE( Workplane_TryIntersectRay( wp, Vec3d_Make( 2, 3, 10 ), Vec3d_Make( 0, 0, -2 ), &t, &hit ) );
    CHECK( t == Approx( 2.5 ) );
    CheckNear( hit, Vec3d_Make( 2, 3, 5 ) );
    CHECK_FALSE( Workplane_TryIntersectRay( wp, Vec3d_Make( 2, 3, 10 ), Vec3d_Make( 0, 0, 1 ), &t, &hit ) ); // away
    CHECK_FALSE( Workplane_TryIntersectRay( wp, Vec3d_Make( 2, 3, 10 ), Vec3d_Make( 1, 0, 0 ), &t, &hit ) ); // parallel
    CHECK( Workplane_TryFromPoints( Vec3d_Make( 0, 0, 0 ), Vec3d_Make( 1, 1, 1 ), Vec3d_Make( 2, 2, 2 ), &wp ) == geometry_status_t::DEGENERATE );
    CHECK( Workplane_TryFromPoints( Vec3d_Make( 0, 0, 0 ), Vec3d_Make( 0, 0, 0 ), Vec3d_Make( 0, 1, 0 ), &wp ) == geometry_status_t::DEGENERATE );
    CHECK( Workplane_TryFromPoints( Vec3d_Make( std::nan( "" ), 0, 0 ), Vec3d_Make( 1, 0, 0 ), Vec3d_Make( 0, 1, 0 ), &wp ) ==
           geometry_status_t::NUMERIC_FAILURE );
}

} // namespace cypher::editor::geometry
