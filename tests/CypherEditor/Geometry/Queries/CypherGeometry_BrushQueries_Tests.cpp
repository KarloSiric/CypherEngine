//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushQueries_Tests.cpp
//  Purpose: Verifies brush measurement, containment, and ray queries.
//  Details: Covers the Queries acceptance gate for brushes: deterministic
//           area, volume, and centroid for canonical fixtures.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "../CypherGeometry_TestSupport.h"

#include "CypherGeometry_BrushQueries.h"

#include <catch2/catch_approx.hpp>

namespace cypher::editor::geometry {

using namespace test;
using Catch::Approx;
using cypher::math::Vec3d_Make;

TEST_CASE( "box and tetrahedron measurements match closed forms",
           "[editor][geometry][queries]" ) {
    SECTION( "off-center box" ) {
        brush_holder_t brush{};
        BuildBrush( &brush.brush, AabbPlanes( Vec3d_Make( 1.0, 2.0, 3.0 ), Vec3d_Make( 3.0, 5.0, 7.0 ) ) );
        boundary_holder_t boundary{};
        Reconstruct( &boundary.boundary, &brush.brush );
        f64 area = 0.0;
        REQUIRE( BrushQuery_TrySurfaceArea( &boundary.boundary, &area ) == geometry_status_t::OK );
        REQUIRE( area == Approx( 2.0 * ( 2 * 3 + 2 * 4 + 3 * 4 ) ) );
        f64 volume = 0.0;
        vec3d_t centroid{};
        REQUIRE( BrushQuery_TryVolumeCentroid( &boundary.boundary, &volume, &centroid ) ==
                 geometry_status_t::OK );
        REQUIRE( volume == Approx( 24.0 ) );
        REQUIRE( centroid.x == Approx( 2.0 ) );
        REQUIRE( centroid.y == Approx( 3.5 ) );
        REQUIRE( centroid.z == Approx( 5.0 ) );
        f64 faceArea = 0.0;
        REQUIRE( BrushQuery_TryFaceArea( &boundary.boundary, 0u, &faceArea ) == geometry_status_t::OK );
        REQUIRE( faceArea == Approx( 12.0 ) ); // +X face is 3 x 4
        REQUIRE( BrushQuery_TryFaceArea( &boundary.boundary, 6u, &faceArea ) ==
                 geometry_status_t::INVALID_ARGUMENT );
    }
    SECTION( "tetrahedron with vertices on alternating cube corners" ) {
        // Planes of the regular tetrahedron with vertices (1,1,-1),(1,-1,1),(-1,1,1),(-1,-1,-1)
        // are at distance 1/sqrt(3) from the origin.
        const f64 k = 1.0 / std::sqrt( 3.0 );
        brush_holder_t brush{};
        BuildBrush( &brush.brush, { UnitPlane( 1, 1, 1, k ), UnitPlane( 1, -1, -1, k ),
                                    UnitPlane( -1, 1, -1, k ), UnitPlane( -1, -1, 1, k ) } );
        boundary_holder_t boundary{};
        Reconstruct( &boundary.boundary, &brush.brush );
        REQUIRE( BrushBoundary_VertexCount( &boundary.boundary ) == 4u );
        f64 volume = 0.0;
        vec3d_t centroid{};
        REQUIRE( BrushQuery_TryVolumeCentroid( &boundary.boundary, &volume, &centroid ) ==
                 geometry_status_t::OK );
        REQUIRE( volume == Approx( 8.0 / 3.0 ) );
        REQUIRE( centroid.x == Approx( 0.0 ).margin( 1e-12 ) );
        REQUIRE( centroid.y == Approx( 0.0 ).margin( 1e-12 ) );
        REQUIRE( centroid.z == Approx( 0.0 ).margin( 1e-12 ) );
        f64 area = 0.0;
        REQUIRE( BrushQuery_TrySurfaceArea( &boundary.boundary, &area ) == geometry_status_t::OK );
        // Edge length 2*sqrt(2); four equilateral faces.
        REQUIRE( area == Approx( 4.0 * std::sqrt( 3.0 ) / 4.0 * 8.0 ) );
    }
}

TEST_CASE( "measurements reject empty and uninitialized boundaries",
           "[editor][geometry][queries]" ) {
    boundary_holder_t boundary{};
    f64 value = 1.0;
    vec3d_t centroid{};
    REQUIRE( BrushQuery_TrySurfaceArea( &boundary.boundary, &value ) ==
             geometry_status_t::NOT_INITIALIZED );
    REQUIRE( value == 0.0 );
    REQUIRE( BrushBoundary_Init( &boundary.boundary, common::Allocator_GetSystem() ) ==
             geometry_status_t::OK );
    REQUIRE( BrushQuery_TrySurfaceArea( &boundary.boundary, &value ) == geometry_status_t::DEGENERATE );
    REQUIRE( BrushQuery_TryVolumeCentroid( &boundary.boundary, &value, &centroid ) ==
             geometry_status_t::DEGENERATE );
    REQUIRE( BrushQuery_TryVolumeCentroid( &boundary.boundary, nullptr, &centroid ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "point classification distinguishes inside, boundary, and outside",
           "[editor][geometry][queries]" ) {
    brush_holder_t brush{};
    BuildBrush( &brush.brush, UnitBoxPlanes() );
    const geometry_numerical_policy_t policy{};
    geometry_containment_t containment{};

    REQUIRE( BrushQuery_TryClassifyPoint( &brush.brush, policy, Vec3d_Make( 0, 0, 0 ), &containment ) ==
             geometry_status_t::OK );
    REQUIRE( containment == geometry_containment_t::INSIDE );
    REQUIRE( BrushQuery_TryClassifyPoint( &brush.brush, policy, Vec3d_Make( 1, 0.5, 0 ), &containment ) ==
             geometry_status_t::OK );
    REQUIRE( containment == geometry_containment_t::ON_BOUNDARY );
    REQUIRE( BrushQuery_TryClassifyPoint( &brush.brush, policy, Vec3d_Make( 1, 1, 1 ), &containment ) ==
             geometry_status_t::OK );
    REQUIRE( containment == geometry_containment_t::ON_BOUNDARY );
    REQUIRE( BrushQuery_TryClassifyPoint( &brush.brush, policy, Vec3d_Make( 1.001, 0, 0 ), &containment ) ==
             geometry_status_t::OK );
    REQUIRE( containment == geometry_containment_t::OUTSIDE );
    const f64 nan = std::numeric_limits<f64>::quiet_NaN();
    REQUIRE( BrushQuery_TryClassifyPoint( &brush.brush, policy, Vec3d_Make( nan, 0, 0 ), &containment ) !=
             geometry_status_t::OK );
}

TEST_CASE( "ray casts report the entry side, misses, and interior starts",
           "[editor][geometry][queries]" ) {
    brush_holder_t brush{};
    BuildBrush( &brush.brush, UnitBoxPlanes() );
    const geometry_numerical_policy_t policy{};
    geometry_brush_ray_hit_t hit{};

    SECTION( "frontal hit on +X" ) {
        REQUIRE( BrushQuery_TryRaycast( &brush.brush, policy, Vec3d_Make( 5, 0.25, -0.5 ),
                                        Vec3d_Make( -2, 0, 0 ), 100.0, &hit ) == geometry_status_t::OK );
        REQUIRE( hit.bHit );
        REQUIRE_FALSE( hit.bStartsInside );
        REQUIRE( hit.iSide == 0u );
        REQUIRE( hit.t == Approx( 2.0 ) );
        REQUIRE( hit.point.x == Approx( 1.0 ) );
        REQUIRE( hit.normal.x == 1.0 );
    }
    SECTION( "limited by maxT" ) {
        REQUIRE( BrushQuery_TryRaycast( &brush.brush, policy, Vec3d_Make( 5, 0, 0 ),
                                        Vec3d_Make( -1, 0, 0 ), 3.5, &hit ) == geometry_status_t::OK );
        REQUIRE_FALSE( hit.bHit );
    }
    SECTION( "parallel ray outside misses" ) {
        REQUIRE( BrushQuery_TryRaycast( &brush.brush, policy, Vec3d_Make( 5, 2, 0 ),
                                        Vec3d_Make( -1, 0, 0 ), 100.0, &hit ) == geometry_status_t::OK );
        REQUIRE_FALSE( hit.bHit );
    }
    SECTION( "pointing away misses" ) {
        REQUIRE( BrushQuery_TryRaycast( &brush.brush, policy, Vec3d_Make( 5, 0, 0 ),
                                        Vec3d_Make( 1, 0, 0 ), 100.0, &hit ) == geometry_status_t::OK );
        REQUIRE_FALSE( hit.bHit );
    }
    SECTION( "origin inside" ) {
        REQUIRE( BrushQuery_TryRaycast( &brush.brush, policy, Vec3d_Make( 0, 0, 0 ),
                                        Vec3d_Make( 0, 1, 0 ), 100.0, &hit ) == geometry_status_t::OK );
        REQUIRE( hit.bHit );
        REQUIRE( hit.bStartsInside );
        REQUIRE( hit.t == 0.0 );
        REQUIRE( hit.iSide == BRUSH_QUERY_SIDE_NONE );
    }
    SECTION( "corner hit resolves to the lowest side index" ) {
        REQUIRE( BrushQuery_TryRaycast( &brush.brush, policy, Vec3d_Make( 3, 3, 0 ),
                                        Vec3d_Make( -1, -1, 0 ), 100.0, &hit ) == geometry_status_t::OK );
        REQUIRE( hit.bHit );
        REQUIRE( hit.iSide == 0u );
        REQUIRE( hit.t == Approx( 2.0 ) );
    }
    SECTION( "invalid rays" ) {
        REQUIRE( BrushQuery_TryRaycast( &brush.brush, policy, Vec3d_Make( 5, 0, 0 ),
                                        Vec3d_Make( 0, 0, 0 ), 100.0, &hit ) ==
                 geometry_status_t::INVALID_ARGUMENT );
        REQUIRE( BrushQuery_TryRaycast( &brush.brush, policy, Vec3d_Make( 5, 0, 0 ),
                                        Vec3d_Make( -1, 0, 0 ), -1.0, &hit ) ==
                 geometry_status_t::INVALID_ARGUMENT );
    }
}

} // namespace cypher::editor::geometry
