//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Planar_Overlay_Tests.cpp
//  Purpose: Contract tests for 2D Boolean overlay of PlanarRegions.
//  Details: Areas give an operation-independent oracle:
//             |A ∪ B| = |A| + |B| - |A ∩ B|
//             |A \ B| = |A| - |A ∩ B|
//             |A xor B| = |A ∪ B| - |A ∩ B|
//           Structural expectations (polygon/hole counts, vertex counts
//           after collinear cleanup) check topology on top of that.
//           Every result is also required to pass PlanarRegion_Validate.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Planar_Overlay.h"
#include "CypherGeometry_PlanarRegionValidation.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>

namespace cypher::editor::geometry {

using Catch::Approx;
using math::vec2d_t;

namespace {

geometry_source_id_t Id( common::u64 v ) { return geometry_source_id_t{ v }; }

planar_frame_t XYFrame()
{
    planar_frame_t f{};
    REQUIRE( PlanarFrame_TryFromPlane(
                 math::Planed_Make( math::Vec3d_Make( 0, 0, 1 ), 0.0 ), &f ) ==
             geometry_status_t::OK );
    return f;
}

// Owns a region built from rectangles/rings for concise tests.
struct Region {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    planar_region_t r{};
    common::u64 id{ 1000u };

    Region() {
        REQUIRE( PlanarRegion_Init( &r, &allocator, XYFrame(), Id( id++ ) ) ==
                 geometry_status_t::OK );
    }
    ~Region() { PlanarRegion_Shutdown( &r ); }

    void Rect( double x0, double y0, double x1, double y1 ) {
        const vec2d_t p[] = { { x0, y0 }, { x1, y0 }, { x1, y1 }, { x0, y1 } };
        const geometry_source_id_t polyId = Id( id++ );
        const geometry_source_id_t contourId = Id( id++ );
        REQUIRE( PlanarRegion_TryAddPolygon( &r, polyId, contourId,
                                             common::span_t<const vec2d_t>{ p, 4 },
                                             nullptr ) == geometry_status_t::OK );
    }
    void HoleRect( double x0, double y0, double x1, double y1 ) {
        const vec2d_t p[] = { { x0, y0 }, { x0, y1 }, { x1, y1 }, { x1, y0 } }; // CW
        REQUIRE( PlanarRegion_TryAddHole( &r, Id( id++ ),
                                          common::span_t<const vec2d_t>{ p, 4 } ) ==
                 geometry_status_t::OK );
    }
    template <common::usize N>
    void Ring( const vec2d_t ( &p )[N] ) {
        const geometry_source_id_t polyId = Id( id++ );
        const geometry_source_id_t contourId = Id( id++ );
        REQUIRE( PlanarRegion_TryAddPolygon( &r, polyId, contourId,
                                             common::span_t<const vec2d_t>{ p, N },
                                             nullptr ) == geometry_status_t::OK );
    }
};

// Runs one overlay and owns the result.
struct Result {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    planar_region_t out{};
    planar_overlay_stats_t stats{};
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
    geometry_source_id_allocator_t ids{ geometry_source_id_t{ 5000u } };

    Result( const Region &a, const Region &b, planar_boolean_op_t op ) {
        status = Planar_TryOverlay( &a.r, &b.r, op, geometry_policy_t{}, Id( 4999 ),
                                    &ids, &allocator, &out, &stats );
        if ( status == geometry_status_t::OK ) {
            const planar_region_validation_t v =
                PlanarRegion_Validate( &out, geometry_policy_t{} );
            INFO( PlanarRegionFault_Name( v.fault ) );
            CHECK( v.status == geometry_status_t::OK );
        }
    }
    ~Result() { PlanarRegion_Shutdown( &out ); }

    double Area() const { return PlanarRegion_Area( &out ); }
    common::usize Polygons() const { return PlanarRegion_PolygonCount( &out ); }
    common::usize Contours() const { return PlanarRegion_ContourCount( &out ); }
    common::usize Points() const { return PlanarRegion_PointCount( &out ); }
};

void CheckAreaIdentities( const Region &a, const Region &b )
{
    Result u( a, b, planar_boolean_op_t::UNION );
    Result i( a, b, planar_boolean_op_t::INTERSECTION );
    Result d( a, b, planar_boolean_op_t::DIFFERENCE );
    Result x( a, b, planar_boolean_op_t::SYMMETRIC_DIFFERENCE );
    REQUIRE( u.status == geometry_status_t::OK );
    REQUIRE( i.status == geometry_status_t::OK );
    REQUIRE( d.status == geometry_status_t::OK );
    REQUIRE( x.status == geometry_status_t::OK );
    const double A = PlanarRegion_Area( &a.r );
    const double B = PlanarRegion_Area( &b.r );
    CHECK( u.Area() == Approx( A + B - i.Area() ).margin( 1e-9 ) );
    CHECK( d.Area() == Approx( A - i.Area() ).margin( 1e-9 ) );
    CHECK( x.Area() == Approx( u.Area() - i.Area() ).margin( 1e-9 ) );
}

} // namespace

TEST_CASE( "PlanarOverlay: overlapping squares, all four operations",
           "[Planar][Overlay]" )
{
    Region a, b;
    a.Rect( 0, 0, 2, 2 );
    b.Rect( 1, 1, 3, 3 );

    Result u( a, b, planar_boolean_op_t::UNION );
    REQUIRE( u.status == geometry_status_t::OK );
    CHECK( u.Area() == Approx( 7.0 ) );
    CHECK( u.Polygons() == 1u );
    CHECK( u.Points() == 8u ); // L-shaped octagon outline

    Result i( a, b, planar_boolean_op_t::INTERSECTION );
    REQUIRE( i.status == geometry_status_t::OK );
    CHECK( i.Area() == Approx( 1.0 ) );
    CHECK( i.Points() == 4u );

    Result d( a, b, planar_boolean_op_t::DIFFERENCE );
    REQUIRE( d.status == geometry_status_t::OK );
    CHECK( d.Area() == Approx( 3.0 ) );
    CHECK( d.Points() == 6u );

    Result x( a, b, planar_boolean_op_t::SYMMETRIC_DIFFERENCE );
    REQUIRE( x.status == geometry_status_t::OK );
    CHECK( x.Area() == Approx( 6.0 ) );
    CHECK( x.Polygons() == 2u ); // two L shapes touching at corners
}

TEST_CASE( "PlanarOverlay: adjacent squares merge into one rectangle",
           "[Planar][Overlay]" )
{
    Region a, b;
    a.Rect( 0, 0, 1, 1 );
    b.Rect( 1, 0, 2, 1 );
    Result u( a, b, planar_boolean_op_t::UNION );
    REQUIRE( u.status == geometry_status_t::OK );
    CHECK( u.Polygons() == 1u );
    CHECK( u.Points() == 4u ); // shared edge vanished, collinear vertices removed
    CHECK( u.Area() == Approx( 2.0 ) );

    Result i( a, b, planar_boolean_op_t::INTERSECTION );
    REQUIRE( i.status == geometry_status_t::OK );
    CHECK( i.Polygons() == 0u ); // touching along an edge has zero area
}

TEST_CASE( "PlanarOverlay: containment produces and fills holes", "[Planar][Overlay]" )
{
    Region big, small;
    big.Rect( 0, 0, 10, 10 );
    small.Rect( 3, 3, 6, 6 );

    Result d( big, small, planar_boolean_op_t::DIFFERENCE );
    REQUIRE( d.status == geometry_status_t::OK );
    CHECK( d.Polygons() == 1u );
    CHECK( d.Contours() == 2u ); // outer + hole
    CHECK( d.Area() == Approx( 91.0 ) );

    // Filling the hole back gives the full square with no hole.
    Region holed;
    holed.Rect( 0, 0, 10, 10 );
    holed.HoleRect( 3, 3, 6, 6 );
    Result u( holed, small, planar_boolean_op_t::UNION );
    REQUIRE( u.status == geometry_status_t::OK );
    CHECK( u.Contours() == 1u );
    CHECK( u.Area() == Approx( 100.0 ) );

    // Small inside big: intersection is small, reverse difference empty.
    Result i( big, small, planar_boolean_op_t::INTERSECTION );
    CHECK( i.Area() == Approx( 9.0 ) );
    Result rd( small, big, planar_boolean_op_t::DIFFERENCE );
    REQUIRE( rd.status == geometry_status_t::OK );
    CHECK( rd.Polygons() == 0u );
}

TEST_CASE( "PlanarOverlay: disjoint inputs", "[Planar][Overlay]" )
{
    Region a, b;
    a.Rect( 0, 0, 1, 1 );
    b.Rect( 5, 5, 7, 6 );
    Result u( a, b, planar_boolean_op_t::UNION );
    REQUIRE( u.status == geometry_status_t::OK );
    CHECK( u.Polygons() == 2u );
    CHECK( u.Area() == Approx( 3.0 ) );
    Result i( a, b, planar_boolean_op_t::INTERSECTION );
    REQUIRE( i.status == geometry_status_t::OK );
    CHECK( i.Polygons() == 0u );
    Result d( a, b, planar_boolean_op_t::DIFFERENCE );
    CHECK( d.Area() == Approx( 1.0 ) );
}

TEST_CASE( "PlanarOverlay: identical inputs", "[Planar][Overlay]" )
{
    Region a, b;
    a.Rect( 0, 0, 3, 2 );
    b.Rect( 0, 0, 3, 2 );
    Result u( a, b, planar_boolean_op_t::UNION );
    CHECK( u.Area() == Approx( 6.0 ) );
    CHECK( u.Points() == 4u );
    Result d( a, b, planar_boolean_op_t::DIFFERENCE );
    REQUIRE( d.status == geometry_status_t::OK );
    CHECK( d.Polygons() == 0u );
    Result x( a, b, planar_boolean_op_t::SYMMETRIC_DIFFERENCE );
    CHECK( x.Polygons() == 0u );
}

TEST_CASE( "PlanarOverlay: cross shape from two bars", "[Planar][Overlay]" )
{
    Region h, v;
    h.Rect( 0, 2, 6, 4 );
    v.Rect( 2, 0, 4, 6 );
    Result u( h, v, planar_boolean_op_t::UNION );
    REQUIRE( u.status == geometry_status_t::OK );
    CHECK( u.Polygons() == 1u );
    CHECK( u.Points() == 12u );
    CHECK( u.Area() == Approx( 20.0 ) );
    Result d( h, v, planar_boolean_op_t::DIFFERENCE );
    CHECK( d.Polygons() == 2u );
    CHECK( d.Area() == Approx( 8.0 ) );
}

TEST_CASE( "PlanarOverlay: area identities on assorted pairs", "[Planar][Overlay]" )
{
    SECTION( "rotated diamond over square" ) {
        Region a, b;
        a.Rect( 0, 0, 4, 4 );
        const vec2d_t diamond[] = { { 2, -1 }, { 5, 2 }, { 2, 5 }, { -1, 2 } };
        b.Ring( diamond );
        CheckAreaIdentities( a, b );
    }
    SECTION( "square with hole vs bar through the hole" ) {
        Region a, b;
        a.Rect( 0, 0, 9, 9 );
        a.HoleRect( 3, 3, 6, 6 );
        b.Rect( 4, -1, 5, 10 );
        CheckAreaIdentities( a, b );
    }
    SECTION( "triangle partly overlapping two rectangles" ) {
        Region a, b;
        a.Rect( 0, 0, 2, 2 );
        a.Rect( 3, 0, 5, 2 );
        const vec2d_t tri[] = { { 1, 1 }, { 4, 1 }, { 2.5, 4 } };
        b.Ring( tri );
        CheckAreaIdentities( a, b );
    }
    SECTION( "shared vertex only" ) {
        Region a, b;
        a.Rect( 0, 0, 1, 1 );
        b.Rect( 1, 1, 2, 2 );
        CheckAreaIdentities( a, b );
    }
    SECTION( "collinear partial edge overlap" ) {
        Region a, b;
        a.Rect( 0, 0, 4, 2 );
        b.Rect( 1, 2, 3, 5 );
        CheckAreaIdentities( a, b );
        Result u( a, b, planar_boolean_op_t::UNION );
        CHECK( u.Polygons() == 1u );
        CHECK( u.Points() == 8u );
    }
}

TEST_CASE( "PlanarOverlay: result is independent of operand polygon order",
           "[Planar][Overlay]" )
{
    Region a1, a2, b;
    a1.Rect( 0, 0, 2, 2 );
    a1.Rect( 4, 0, 6, 2 );
    a2.Rect( 4, 0, 6, 2 );
    a2.Rect( 0, 0, 2, 2 );
    b.Rect( 1, 1, 5, 3 );
    Result r1( a1, b, planar_boolean_op_t::UNION );
    Result r2( a2, b, planar_boolean_op_t::UNION );
    REQUIRE( r1.Points() == r2.Points() );
    for ( common::usize i = 0; i < r1.Points(); ++i ) {
        CHECK( r1.out.points.pData[i].x == r2.out.points.pData[i].x );
        CHECK( r1.out.points.pData[i].y == r2.out.points.pData[i].y );
    }
}

TEST_CASE( "PlanarOverlay: argument and failure contract", "[Planar][Overlay]" )
{
    Region a, b;
    a.Rect( 0, 0, 1, 1 );
    b.Rect( 0, 0, 1, 1 );
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_source_id_allocator_t ids{ geometry_source_id_t{ 77u } };
    planar_region_t out{};

    CHECK( Planar_TryOverlay( nullptr, &b.r, planar_boolean_op_t::UNION, {}, Id( 1 ),
                              &ids, &allocator, &out, nullptr ) ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK( Planar_TryOverlay( &a.r, &b.r, planar_boolean_op_t::UNION, {},
                              GEOMETRY_SOURCE_ID_INVALID, &ids, &allocator, &out,
                              nullptr ) == geometry_status_t::INVALID_ARGUMENT );

    // Mismatched frames are rejected without touching the ID allocator.
    Region other;
    planar_frame_t shifted{};
    REQUIRE( PlanarFrame_TryFromPlane( math::Planed_Make( math::Vec3d_Make( 0, 0, 1 ), -1.0 ),
                                       &shifted ) == geometry_status_t::OK );
    PlanarRegion_Shutdown( &other.r );
    REQUIRE( PlanarRegion_Init( &other.r, &allocator, shifted, Id( 3 ) ) ==
             geometry_status_t::OK );
    CHECK( Planar_TryOverlay( &a.r, &other.r, planar_boolean_op_t::UNION, {}, Id( 1 ),
                              &ids, &allocator, &out, nullptr ) ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK( ids.next.value == 77u );
    CHECK_FALSE( PlanarRegion_IsInitialized( &out ) );

    // Successful run publishes IDs: 2 per polygon + 1 per hole.
    REQUIRE( Planar_TryOverlay( &a.r, &b.r, planar_boolean_op_t::UNION, {}, Id( 1 ),
                                &ids, &allocator, &out, nullptr ) == geometry_status_t::OK );
    CHECK( ids.next.value == 79u );
    // Initialized output is rejected.
    planar_region_t *pOut = &out;
    CHECK( Planar_TryOverlay( &a.r, &b.r, planar_boolean_op_t::UNION, {}, Id( 1 ),
                              &ids, &allocator, pOut, nullptr ) ==
           geometry_status_t::INVALID_ARGUMENT );
    PlanarRegion_Shutdown( &out );
}

TEST_CASE( "PlanarOverlay: stats describe the arrangement", "[Planar][Overlay]" )
{
    Region a, b;
    a.Rect( 0, 0, 2, 2 );
    b.Rect( 1, 1, 3, 3 );
    Result u( a, b, planar_boolean_op_t::UNION );
    REQUIRE( u.status == geometry_status_t::OK );
    CHECK( u.stats.cInputEdges == 8u );
    CHECK( u.stats.cArrangementVertices == 10u ); // 8 corners + 2 crossings
    CHECK( u.stats.cArrangementEdges == 12u );
    CHECK( u.stats.cOutputPolygons == 1u );
    CHECK( u.stats.cOutputHoles == 0u );
}

} // namespace cypher::editor::geometry
