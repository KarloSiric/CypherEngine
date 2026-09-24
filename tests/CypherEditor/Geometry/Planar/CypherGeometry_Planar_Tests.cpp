//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Planar_Tests.cpp
//  Purpose: Contract tests for plane frames, exact segment relations,
//           PlanarRegion storage/validation, and hole-aware triangulation.
//  Details: Triangulation is checked by invariants rather than exact
//           output: triangle count n + 2h - 2, every triangle strictly CCW,
//           and summed area equal to the polygon's filled area. Those
//           three together rule out overlaps, gaps, and flips.
//
//  History:
//  - Created by Karlo Siric on 2026-09-23
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Planar_Frame.h"
#include "CypherGeometry_Planar_Segment.h"
#include "CypherGeometry_Planar_Triangulate.h"
#include "CypherGeometry_PlanarRegion.h"
#include "CypherGeometry_PlanarRegionValidation.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <string>

namespace cypher::editor::geometry {

using Catch::Approx;
using math::vec2d_t;
using math::vec3d_t;
using math::Vec2d_Make;
using math::Vec3d_Make;

namespace {

geometry_source_id_t Id( common::u64 v ) { return geometry_source_id_t{ v }; }

template <common::usize N>
common::span_t<const vec2d_t> Span( const vec2d_t ( &a )[N] )
{
    return common::span_t<const vec2d_t>{ a, N };
}

planar_frame_t XYFrame()
{
    planar_frame_t f{};
    REQUIRE( PlanarFrame_TryFromPlane( math::Planed_Make( Vec3d_Make( 0, 0, 1 ), 0.0 ),
                                       &f ) == geometry_status_t::OK );
    return f;
}

struct RegionFixture {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    planar_region_t region{};
    geometry_policy_t policy{};
    common::u64 nextId{ 100u };

    RegionFixture() {
        REQUIRE( PlanarRegion_Init( &region, &allocator, XYFrame(), Id( 1 ) ) ==
                 geometry_status_t::OK );
    }
    ~RegionFixture() { PlanarRegion_Shutdown( &region ); }

    template <common::usize N>
    void AddPolygon( const vec2d_t ( &pts )[N] ) {
        const geometry_source_id_t polyId = Id( nextId++ );
        const geometry_source_id_t contourId = Id( nextId++ );
        REQUIRE( PlanarRegion_TryAddPolygon( &region, polyId, contourId,
                                             Span( pts ), nullptr ) ==
                 geometry_status_t::OK );
    }
    template <common::usize N>
    void AddHole( const vec2d_t ( &pts )[N] ) {
        REQUIRE( PlanarRegion_TryAddHole( &region, Id( nextId++ ), Span( pts ) ) ==
                 geometry_status_t::OK );
    }
};

// Checks the three triangulation invariants for the whole region.
void CheckTriangulation( const planar_region_t *pRegion,
                         const common::vector_t<planar_triangle_t> &tris,
                         common::usize cExpected )
{
    REQUIRE( tris.nCount == cExpected );
    double sum = 0.0;
    for ( common::usize i = 0; i < tris.nCount; ++i ) {
        const planar_triangle_t &t = tris.pData[i];
        const vec2d_t a = pRegion->points.pData[t.a];
        const vec2d_t b = pRegion->points.pData[t.b];
        const vec2d_t c = pRegion->points.pData[t.c];
        CHECK( math::Orient2D( a, b, c ) > 0 );
        sum += 0.5 * ( ( b.x - a.x ) * ( c.y - a.y ) - ( b.y - a.y ) * ( c.x - a.x ) );
    }
    CHECK( sum == Approx( PlanarRegion_Area( pRegion ) ).epsilon( 1e-12 ) );
}

const vec2d_t kSquare4[] = { { 0, 0 }, { 4, 0 }, { 4, 4 }, { 0, 4 } };
const vec2d_t kHole1to3CW[] = { { 1, 1 }, { 1, 3 }, { 3, 3 }, { 3, 1 } };

} // namespace

// ===========================================================================
// Frames
// ===========================================================================

TEST_CASE( "PlanarFrame: plane frame is valid, right-handed, and on-plane",
           "[Planar][Frame]" )
{
    planar_frame_t f{};
    const math::planed_t plane = math::Planed_Make( Vec3d_Make( 0.0, 0.6, 0.8 ), -5.0 );
    REQUIRE( PlanarFrame_TryFromPlane( plane, &f ) == geometry_status_t::OK );
    CHECK( PlanarFrame_IsValid( f, 1e-12 ) );
    CHECK( PlanarFrame_SignedDistance( f, f.origin ) == Approx( 0.0 ).margin( 1e-12 ) );
    CHECK( math::Planed_SignedDistance( plane, f.origin ) == Approx( 0.0 ).margin( 1e-12 ) );
}

TEST_CASE( "PlanarFrame: identical planes give bit-identical frames",
           "[Planar][Frame]" )
{
    planar_frame_t a{}, b{};
    const math::planed_t plane = math::Planed_Make( Vec3d_Make( 1, 2, 3 ), 7.0 );
    REQUIRE( PlanarFrame_TryFromPlane( plane, &a ) == geometry_status_t::OK );
    REQUIRE( PlanarFrame_TryFromPlane( plane, &b ) == geometry_status_t::OK );
    CHECK( a.u.x == b.u.x );
    CHECK( a.u.y == b.u.y );
    CHECK( a.u.z == b.u.z );
    CHECK( a.origin.z == b.origin.z );
}

TEST_CASE( "PlanarFrame: non-unit plane normal describes the same plane",
           "[Planar][Frame]" )
{
    planar_frame_t f{};
    // 2z - 6 = 0 is the plane z = 3.
    REQUIRE( PlanarFrame_TryFromPlane( math::Planed_Make( Vec3d_Make( 0, 0, 2 ), -6.0 ),
                                       &f ) == geometry_status_t::OK );
    CHECK( f.origin.z == Approx( 3.0 ) );
    CHECK( f.normal.z == Approx( 1.0 ) );
}

TEST_CASE( "PlanarFrame: rejects zero and non-finite normals", "[Planar][Frame]" )
{
    planar_frame_t f{};
    CHECK( PlanarFrame_TryFromPlane( math::Planed_Make( Vec3d_Make( 0, 0, 0 ), 1.0 ), &f ) ==
           geometry_status_t::NUMERIC_FAILURE );
    CHECK( PlanarFrame_TryFromPlane( math::Planed_Make( Vec3d_Make( NAN, 0, 1 ), 0.0 ), &f ) ==
           geometry_status_t::NUMERIC_FAILURE );
    CHECK( PlanarFrame_TryFromPlane( math::Planed_Make( Vec3d_Make( 0, 0, 1 ), 0.0 ),
                                     nullptr ) == geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "PlanarFrame: project/unproject round-trips on-plane points",
           "[Planar][Frame]" )
{
    planar_frame_t f{};
    REQUIRE( PlanarFrame_TryFromPlane( math::Planed_Make( Vec3d_Make( 1, 1, 1 ), -3.0 ),
                                       &f ) == geometry_status_t::OK );
    const vec2d_t p2 = Vec2d_Make( 2.5, -7.25 );
    const vec3d_t p3 = PlanarFrame_Unproject( f, p2 );
    const vec2d_t back = PlanarFrame_Project( f, p3 );
    CHECK( back.x == Approx( p2.x ).margin( 1e-12 ) );
    CHECK( back.y == Approx( p2.y ).margin( 1e-12 ) );
    CHECK( PlanarFrame_SignedDistance( f, p3 ) == Approx( 0.0 ).margin( 1e-12 ) );
}

TEST_CASE( "PlanarFrame: loop frame keeps CCW loops CCW in 2D", "[Planar][Frame]" )
{
    // CCW around +Y seen from +Y: winding (x,z) = (0,0),(0,1),(1,1),(1,0).
    const vec3d_t loop[] = { { 0, 2, 0 }, { 0, 2, 1 }, { 1, 2, 1 }, { 1, 2, 0 } };
    const planar_frame_result_t r = PlanarFrame_TryFromLoop( loop, 4, 1e-9 );
    REQUIRE( r.status == geometry_status_t::OK );
    CHECK( PlanarFrame_IsValid( r.frame, 1e-12 ) );
    vec2d_t p2[4];
    for ( int i = 0; i < 4; ++i ) { p2[i] = PlanarFrame_Project( r.frame, loop[i] ); }
    CHECK( Planar_RingSignedArea( common::span_t<const vec2d_t>{ p2, 4 } ) > 0.0 );
}

TEST_CASE( "PlanarFrame: loop frame reports non-planarity with deviation",
           "[Planar][Frame]" )
{
    const vec3d_t loop[] = { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0.1 }, { 0, 1, 0 } };
    const planar_frame_result_t r = PlanarFrame_TryFromLoop( loop, 4, 1e-6 );
    CHECK( r.status == geometry_status_t::NON_PLANAR );
    CHECK( r.fMaxDeviation > 0.01 );
}

TEST_CASE( "PlanarFrame: degenerate and short loops rejected", "[Planar][Frame]" )
{
    const vec3d_t line[] = { { 0, 0, 0 }, { 1, 0, 0 }, { 2, 0, 0 } };
    CHECK( PlanarFrame_TryFromLoop( line, 3, 1e-9 ).status == geometry_status_t::DEGENERATE );
    CHECK( PlanarFrame_TryFromLoop( line, 2, 1e-9 ).status ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK( PlanarFrame_TryFromLoop( nullptr, 3, 1e-9 ).status ==
           geometry_status_t::INVALID_ARGUMENT );
}

// ===========================================================================
// Segments
// ===========================================================================

TEST_CASE( "PlanarSegment: every relation is classified exactly", "[Planar][Segment]" )
{
    using R = planar_segment_relation_t;
    CHECK( Planar_ClassifySegments( { 0, 0 }, { 2, 2 }, { 0, 2 }, { 2, 0 } ) == R::PROPER );
    CHECK( Planar_ClassifySegments( { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } ) == R::DISJOINT );
    CHECK( Planar_ClassifySegments( { 0, 0 }, { 2, 0 }, { 1, 0 }, { 1, 1 } ) ==
           R::TOUCH_ENDPOINT );
    CHECK( Planar_ClassifySegments( { 0, 0 }, { 2, 0 }, { 2, 0 }, { 3, 0 } ) ==
           R::TOUCH_ENDPOINT );
    CHECK( Planar_ClassifySegments( { 0, 0 }, { 2, 0 }, { 1, 0 }, { 3, 0 } ) ==
           R::COLLINEAR_OVERLAP );
    CHECK( Planar_ClassifySegments( { 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 0 } ) == R::DISJOINT );
    // Vertical collinear overlap exercises the dominant-axis choice.
    CHECK( Planar_ClassifySegments( { 5, 0 }, { 5, 4 }, { 5, 3 }, { 5, 9 } ) ==
           R::COLLINEAR_OVERLAP );
    // Degenerate segment (a point) on the other segment.
    CHECK( Planar_ClassifySegments( { 1, 1 }, { 1, 1 }, { 0, 0 }, { 2, 2 } ) ==
           R::TOUCH_ENDPOINT );
}

TEST_CASE( "PlanarSegment: near-collinear case decided exactly", "[Planar][Segment]" )
{
    // b0 sits one ulp above the line y = x; a floating cross product can
    // round this to zero, the exact predicate must not.
    const double y = std::nextafter( 0.5, 1.0 );
    CHECK( Planar_ClassifySegments( { 0, 0 }, { 1, 1 }, { 0.5, y }, { 0.5, 2 } ) ==
           planar_segment_relation_t::DISJOINT );
}

TEST_CASE( "PlanarSegment: proper intersection point", "[Planar][Segment]" )
{
    double ta = 0, tb = 0;
    vec2d_t p{};
    REQUIRE( Planar_TryIntersectProper( { 0, 0 }, { 4, 4 }, { 0, 4 }, { 4, 0 },
                                        &ta, &tb, &p ) );
    CHECK( ta == Approx( 0.5 ) );
    CHECK( tb == Approx( 0.5 ) );
    CHECK( p.x == Approx( 2.0 ) );
    CHECK( p.y == Approx( 2.0 ) );
    CHECK_FALSE( Planar_TryIntersectProper( { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 },
                                            &ta, &tb, &p ) );
}

// ===========================================================================
// PlanarRegion storage
// ===========================================================================

TEST_CASE( "PlanarRegion: polygon with hole stores contiguous contours",
           "[Planar][Region]" )
{
    RegionFixture fx;
    fx.AddPolygon( kSquare4 );
    fx.AddHole( kHole1to3CW );
    CHECK( PlanarRegion_PolygonCount( &fx.region ) == 1u );
    CHECK( PlanarRegion_ContourCount( &fx.region ) == 2u );
    CHECK( PlanarRegion_PointCount( &fx.region ) == 8u );
    CHECK( fx.region.polygons.pData[0].cContours == 2u );
    CHECK( PlanarRegion_PolygonArea( &fx.region, 0 ) == Approx( 12.0 ) );
    CHECK( PlanarRegion_ContourSignedArea( &fx.region, 1 ) == Approx( -4.0 ) );
}

TEST_CASE( "PlanarRegion: containment respects holes and boundaries",
           "[Planar][Region]" )
{
    RegionFixture fx;
    fx.AddPolygon( kSquare4 );
    fx.AddHole( kHole1to3CW );
    CHECK( PlanarRegion_Contains( &fx.region, { 0.5, 0.5 } ) == planar_containment_t::INSIDE );
    CHECK( PlanarRegion_Contains( &fx.region, { 2, 2 } ) == planar_containment_t::OUTSIDE );
    CHECK( PlanarRegion_Contains( &fx.region, { 5, 2 } ) == planar_containment_t::OUTSIDE );
    CHECK( PlanarRegion_Contains( &fx.region, { 4, 2 } ) == planar_containment_t::BOUNDARY );
    CHECK( PlanarRegion_Contains( &fx.region, { 1, 2 } ) == planar_containment_t::BOUNDARY );
    // A vertex exactly on the scan line must be counted once.
    CHECK( PlanarRegion_Contains( &fx.region, { -1, 4 } ) == planar_containment_t::OUTSIDE );
    CHECK( PlanarRegion_Contains( &fx.region, { 0.5, 1 } ) == planar_containment_t::INSIDE );
}

TEST_CASE( "PlanarRegion: failed adds leave the region unchanged", "[Planar][Region]" )
{
    RegionFixture fx;
    fx.AddPolygon( kSquare4 );
    const vec2d_t bad[] = { { 0, 0 }, { NAN, 1 }, { 1, 1 } };
    const vec2d_t tooFew[] = { { 0, 0 }, { 1, 1 } };
    CHECK( PlanarRegion_TryAddHole( &fx.region, Id( 900 ), Span( bad ) ) ==
           geometry_status_t::NUMERIC_FAILURE );
    CHECK( PlanarRegion_TryAddHole( &fx.region, Id( 901 ), Span( tooFew ) ) ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK( PlanarRegion_TryAddPolygon( &fx.region, GEOMETRY_SOURCE_ID_INVALID, Id( 3 ),
                                       Span( kSquare4 ), nullptr ) ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK( PlanarRegion_PointCount( &fx.region ) == 4u );
    CHECK( PlanarRegion_ContourCount( &fx.region ) == 1u );
    CHECK( PlanarRegion_PolygonCount( &fx.region ) == 1u );
}

TEST_CASE( "PlanarRegion: hole before any polygon is rejected", "[Planar][Region]" )
{
    RegionFixture fx;
    CHECK( PlanarRegion_TryAddHole( &fx.region, Id( 5 ), Span( kHole1to3CW ) ) ==
           geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "PlanarRegion: lifecycle guards", "[Planar][Region]" )
{
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    planar_region_t r{};
    CHECK( PlanarRegion_TryAddPolygon( &r, Id( 1 ), Id( 2 ), Span( kSquare4 ), nullptr ) ==
           geometry_status_t::NOT_INITIALIZED );
    planar_frame_t broken{};
    CHECK( PlanarRegion_Init( &r, &allocator, broken, Id( 1 ) ) ==
           geometry_status_t::INVALID_ARGUMENT );
    REQUIRE( PlanarRegion_Init( &r, &allocator, XYFrame(), Id( 1 ) ) ==
             geometry_status_t::OK );
    CHECK( PlanarRegion_Init( &r, &allocator, XYFrame(), Id( 1 ) ) ==
           geometry_status_t::ALREADY_INITIALIZED );
    PlanarRegion_Shutdown( &r );
    PlanarRegion_Shutdown( &r ); // idempotent
    CHECK_FALSE( PlanarRegion_IsInitialized( &r ) );
}

TEST_CASE( "PlanarRegion: bounds and clear", "[Planar][Region]" )
{
    RegionFixture fx;
    fx.AddPolygon( kSquare4 );
    vec2d_t mn{}, mx{};
    REQUIRE( PlanarRegion_TryBounds( &fx.region, &mn, &mx ) );
    CHECK( mn.x == 0.0 );
    CHECK( mx.y == 4.0 );
    PlanarRegion_Clear( &fx.region );
    CHECK( PlanarRegion_PolygonCount( &fx.region ) == 0u );
    CHECK_FALSE( PlanarRegion_TryBounds( &fx.region, &mn, &mx ) );
}

// ===========================================================================
// Validation
// ===========================================================================

TEST_CASE( "PlanarValidation: valid polygon with hole and island", "[Planar][Validation]" )
{
    RegionFixture fx;
    const vec2d_t outer[] = { { 0, 0 }, { 10, 0 }, { 10, 10 }, { 0, 10 } };
    const vec2d_t hole[] = { { 2, 2 }, { 2, 8 }, { 8, 8 }, { 8, 2 } };
    const vec2d_t island[] = { { 4, 4 }, { 6, 4 }, { 6, 6 }, { 4, 6 } };
    fx.AddPolygon( outer );
    fx.AddHole( hole );
    fx.AddPolygon( island );
    const planar_region_validation_t v = PlanarRegion_Validate( &fx.region, fx.policy );
    INFO( PlanarRegionFault_Name( v.fault ) );
    CHECK( v.status == geometry_status_t::OK );
}

TEST_CASE( "PlanarValidation: point contacts between contours are allowed",
           "[Planar][Validation]" )
{
    SECTION( "two squares sharing one corner" ) {
        RegionFixture fx;
        const vec2d_t a[] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
        const vec2d_t b[] = { { 1, 1 }, { 2, 1 }, { 2, 2 }, { 1, 2 } };
        fx.AddPolygon( a );
        fx.AddPolygon( b );
        CHECK( PlanarRegion_Validate( &fx.region, fx.policy ).status ==
               geometry_status_t::OK );
    }
    SECTION( "hole touching the outer at a point from inside" ) {
        RegionFixture fx;
        fx.AddPolygon( kSquare4 );
        const vec2d_t touch[] = { { 0, 2 }, { 1, 3 }, { 2, 2 }, { 1, 1 } }; // CW
        fx.AddHole( touch );
        CHECK( PlanarRegion_Validate( &fx.region, fx.policy ).status ==
               geometry_status_t::OK );
    }
    SECTION( "vertex of one polygon on the edge of another" ) {
        RegionFixture fx;
        fx.AddPolygon( kSquare4 );
        const vec2d_t t[] = { { 4, 2 }, { 6, 1 }, { 6, 3 } };
        fx.AddPolygon( t );
        CHECK( PlanarRegion_Validate( &fx.region, fx.policy ).status ==
               geometry_status_t::OK );
    }
}

TEST_CASE( "PlanarValidation: collinear corners are allowed", "[Planar][Validation]" )
{
    RegionFixture fx;
    const vec2d_t pts[] = { { 0, 0 }, { 1, 0 }, { 2, 0 }, { 2, 2 }, { 0, 2 } };
    fx.AddPolygon( pts );
    CHECK( PlanarRegion_Validate( &fx.region, fx.policy ).status == geometry_status_t::OK );
}

TEST_CASE( "PlanarValidation: each fault is detected", "[Planar][Validation]" )
{
    using F = planar_region_fault_t;
    auto faultOf = []( auto build ) {
        RegionFixture fx;
        build( fx );
        return PlanarRegion_Validate( &fx.region, fx.policy );
    };

    SECTION( "outer wound clockwise" ) {
        const auto v = faultOf( []( RegionFixture &fx ) {
            const vec2d_t cw[] = { { 0, 0 }, { 0, 4 }, { 4, 4 }, { 4, 0 } };
            fx.AddPolygon( cw );
        } );
        CHECK( v.fault == F::OUTER_NOT_CCW );
        CHECK( v.status == geometry_status_t::INVALID_TOPOLOGY );
    }
    SECTION( "hole wound counter-clockwise" ) {
        const auto v = faultOf( []( RegionFixture &fx ) {
            fx.AddPolygon( kSquare4 );
            const vec2d_t ccw[] = { { 1, 1 }, { 3, 1 }, { 3, 3 }, { 1, 3 } };
            fx.AddHole( ccw );
        } );
        CHECK( v.fault == F::HOLE_NOT_CW );
    }
    SECTION( "bow-tie self intersection" ) {
        const auto v = faultOf( []( RegionFixture &fx ) {
            // Asymmetric so the lobes do not cancel to zero signed area
            // (which would legitimately report ZERO_AREA first).
            const vec2d_t bow[] = { { 0, 0 }, { 4, 4 }, { 4, 0 }, { 0, 2 } };
            fx.AddPolygon( bow );
        } );
        CHECK( v.fault == F::SELF_INTERSECTION );
        CHECK( v.status == geometry_status_t::SELF_INTERSECTING );
    }
    SECTION( "collinear spike folds back on itself" ) {
        const auto v = faultOf( []( RegionFixture &fx ) {
            const vec2d_t spike[] = { { 0, 0 }, { 4, 0 }, { 2, 0 }, { 2, 3 } };
            fx.AddPolygon( spike );
        } );
        CHECK( v.fault == F::SELF_INTERSECTION );
    }
    SECTION( "duplicate consecutive point" ) {
        const auto v = faultOf( []( RegionFixture &fx ) {
            const vec2d_t dup[] = { { 0, 0 }, { 4, 0 }, { 4, 0 }, { 0, 4 } };
            fx.AddPolygon( dup );
        } );
        CHECK( v.fault == F::DUPLICATE_POINT );
        CHECK( v.status == geometry_status_t::DEGENERATE );
    }
    SECTION( "zero area" ) {
        const auto v = faultOf( []( RegionFixture &fx ) {
            const vec2d_t flat[] = { { 0, 0 }, { 1, 0 }, { 2, 0 } };
            fx.AddPolygon( flat );
        } );
        CHECK( v.fault == F::ZERO_AREA );
    }
    SECTION( "hole outside outer" ) {
        const auto v = faultOf( []( RegionFixture &fx ) {
            fx.AddPolygon( kSquare4 );
            const vec2d_t far[] = { { 10, 10 }, { 10, 12 }, { 12, 12 }, { 12, 10 } };
            fx.AddHole( far );
        } );
        CHECK( v.fault == F::HOLE_OUTSIDE_OUTER );
    }
    SECTION( "hole crossing outer" ) {
        const auto v = faultOf( []( RegionFixture &fx ) {
            fx.AddPolygon( kSquare4 );
            const vec2d_t cross[] = { { -1, 1 }, { 1, 3 }, { 3, 3 }, { 3, 1 } };
            fx.AddHole( cross );
        } );
        CHECK( v.fault == F::CONTOURS_INTERSECT );
    }
    SECTION( "polygons crossing through a shared vertex" ) {
        // No proper crossing anywhere: B enters A exactly at A's corner
        // (2,2) and leaves at A's corner (0,2). Only the wedge test sees it.
        const auto v = faultOf( []( RegionFixture &fx ) {
            const vec2d_t a[] = { { 0, 0 }, { 2, 0 }, { 2, 2 }, { 0, 2 } };
            const vec2d_t b[] = { { 2, 2 }, { 1, 3 }, { 0, 2 }, { 1, 1 } };
            fx.AddPolygon( a );
            fx.AddPolygon( b );
        } );
        CHECK( v.fault == F::POLYGONS_INTERSECT );
    }
    SECTION( "nested holes" ) {
        const auto v = faultOf( []( RegionFixture &fx ) {
            const vec2d_t outer[] = { { 0, 0 }, { 10, 0 }, { 10, 10 }, { 0, 10 } };
            const vec2d_t big[] = { { 1, 1 }, { 1, 9 }, { 9, 9 }, { 9, 1 } };
            const vec2d_t small[] = { { 3, 3 }, { 3, 5 }, { 5, 5 }, { 5, 3 } };
            fx.AddPolygon( outer );
            fx.AddHole( big );
            fx.AddHole( small );
        } );
        CHECK( v.fault == F::HOLES_NESTED );
    }
    SECTION( "overlapping polygons" ) {
        const auto v = faultOf( []( RegionFixture &fx ) {
            fx.AddPolygon( kSquare4 );
            const vec2d_t inner[] = { { 1, 1 }, { 2, 1 }, { 2, 2 }, { 1, 2 } };
            fx.AddPolygon( inner );
        } );
        CHECK( v.fault == F::POLYGONS_OVERLAP );
        CHECK( v.iPolygonOther == 1u );
    }
    SECTION( "crossing polygons" ) {
        const auto v = faultOf( []( RegionFixture &fx ) {
            fx.AddPolygon( kSquare4 );
            const vec2d_t shifted[] = { { 2, 2 }, { 6, 2 }, { 6, 6 }, { 2, 6 } };
            fx.AddPolygon( shifted );
        } );
        CHECK( v.fault == F::POLYGONS_INTERSECT );
    }
    SECTION( "coordinate limit" ) {
        const auto v = faultOf( []( RegionFixture &fx ) {
            const vec2d_t huge[] = { { 0, 0 }, { 1e7, 0 }, { 1e7, 1e7 } };
            fx.AddPolygon( huge );
        } );
        CHECK( v.fault == F::COORDINATE_LIMIT );
        CHECK( v.status == geometry_status_t::LIMIT_EXCEEDED );
    }
}

TEST_CASE( "PlanarValidation: uninitialized region", "[Planar][Validation]" )
{
    planar_region_t r{};
    CHECK( PlanarRegion_Validate( &r, geometry_policy_t{} ).status ==
           geometry_status_t::NOT_INITIALIZED );
    CHECK( std::string( PlanarRegionFault_Name( planar_region_fault_t::HOLES_NESTED ) ) ==
           "holes_nested" );
}

// ===========================================================================
// Triangulation
// ===========================================================================

TEST_CASE( "PlanarTriangulate: convex square", "[Planar][Triangulate]" )
{
    RegionFixture fx;
    fx.AddPolygon( kSquare4 );
    common::vector_t<planar_triangle_t> tris{};
    REQUIRE( common::Vector_Init( &tris, &fx.allocator ) );
    REQUIRE( Planar_TryTriangulateRegion( &fx.region, &tris ) == geometry_status_t::OK );
    CheckTriangulation( &fx.region, tris, 2u );
    common::Vector_Shutdown( &tris );
}

TEST_CASE( "PlanarTriangulate: concave L shape", "[Planar][Triangulate]" )
{
    RegionFixture fx;
    const vec2d_t L[] = { { 0, 0 }, { 4, 0 }, { 4, 1 }, { 1, 1 }, { 1, 4 }, { 0, 4 } };
    fx.AddPolygon( L );
    common::vector_t<planar_triangle_t> tris{};
    REQUIRE( common::Vector_Init( &tris, &fx.allocator ) );
    REQUIRE( Planar_TryTriangulateRegion( &fx.region, &tris ) == geometry_status_t::OK );
    CheckTriangulation( &fx.region, tris, 4u );
    common::Vector_Shutdown( &tris );
}

TEST_CASE( "PlanarTriangulate: collinear corners never give zero-area triangles",
           "[Planar][Triangulate]" )
{
    RegionFixture fx;
    const vec2d_t pts[] = { { 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 0 }, { 3, 2 }, { 0, 2 } };
    fx.AddPolygon( pts );
    common::vector_t<planar_triangle_t> tris{};
    REQUIRE( common::Vector_Init( &tris, &fx.allocator ) );
    REQUIRE( Planar_TryTriangulateRegion( &fx.region, &tris ) == geometry_status_t::OK );
    CheckTriangulation( &fx.region, tris, 4u );
    common::Vector_Shutdown( &tris );
}

TEST_CASE( "PlanarTriangulate: square with one hole", "[Planar][Triangulate]" )
{
    RegionFixture fx;
    fx.AddPolygon( kSquare4 );
    fx.AddHole( kHole1to3CW );
    common::vector_t<planar_triangle_t> tris{};
    REQUIRE( common::Vector_Init( &tris, &fx.allocator ) );
    REQUIRE( Planar_TryTriangulateRegion( &fx.region, &tris ) == geometry_status_t::OK );
    CheckTriangulation( &fx.region, tris, 8u ); // n=8, h=1 -> 8 + 2 - 2
    common::Vector_Shutdown( &tris );
}

TEST_CASE( "PlanarTriangulate: several holes and a second polygon",
           "[Planar][Triangulate]" )
{
    RegionFixture fx;
    const vec2d_t outer[] = { { 0, 0 }, { 12, 0 }, { 12, 6 }, { 0, 6 } };
    const vec2d_t h1[] = { { 1, 1 }, { 1, 5 }, { 3, 5 }, { 3, 1 } };
    const vec2d_t h2[] = { { 5, 1 }, { 5, 5 }, { 7, 5 }, { 7, 1 } };
    const vec2d_t h3[] = { { 9, 2 }, { 10, 4 }, { 11, 2 } }; // CW triangle
    const vec2d_t other[] = { { 20, 0 }, { 24, 0 }, { 22, 3 } };
    fx.AddPolygon( outer );
    fx.AddHole( h1 );
    fx.AddHole( h2 );
    fx.AddHole( h3 );
    fx.AddPolygon( other );
    REQUIRE( PlanarRegion_Validate( &fx.region, fx.policy ).status == geometry_status_t::OK );
    common::vector_t<planar_triangle_t> tris{};
    REQUIRE( common::Vector_Init( &tris, &fx.allocator ) );
    REQUIRE( Planar_TryTriangulateRegion( &fx.region, &tris ) == geometry_status_t::OK );
    // Polygon 0: n = 4+4+4+3 = 15, h = 3 -> 19. Polygon 1: 1.
    CheckTriangulation( &fx.region, tris, 20u );
    for ( common::usize i = 0; i < tris.nCount; ++i ) {
        CHECK( tris.pData[i].iPolygon == ( i < 19 ? 0u : 1u ) );
    }
    common::Vector_Shutdown( &tris );
}

TEST_CASE( "PlanarTriangulate: concave outer with hole in a pocket",
           "[Planar][Triangulate]" )
{
    RegionFixture fx;
    // U shape; the hole sits in the right arm so the nearest outer vertex
    // is not trivially visible from every direction.
    const vec2d_t U[] = { { 0, 0 }, { 9, 0 }, { 9, 9 }, { 6, 9 }, { 6, 3 }, { 3, 3 },
                          { 3, 9 }, { 0, 9 } };
    const vec2d_t hole[] = { { 7, 5 }, { 7, 7 }, { 8, 7 }, { 8, 5 } };
    fx.AddPolygon( U );
    fx.AddHole( hole );
    REQUIRE( PlanarRegion_Validate( &fx.region, fx.policy ).status == geometry_status_t::OK );
    common::vector_t<planar_triangle_t> tris{};
    REQUIRE( common::Vector_Init( &tris, &fx.allocator ) );
    REQUIRE( Planar_TryTriangulateRegion( &fx.region, &tris ) == geometry_status_t::OK );
    CheckTriangulation( &fx.region, tris, 12u + 2u - 2u );
    common::Vector_Shutdown( &tris );
}

TEST_CASE( "PlanarTriangulate: hole touching the outer at a point",
           "[Planar][Triangulate]" )
{
    RegionFixture fx;
    fx.AddPolygon( kSquare4 );
    const vec2d_t touch[] = { { 0, 2 }, { 1, 3 }, { 2, 2 }, { 1, 1 } }; // CW diamond
    fx.AddHole( touch );
    REQUIRE( PlanarRegion_Validate( &fx.region, fx.policy ).status == geometry_status_t::OK );
    common::vector_t<planar_triangle_t> tris{};
    REQUIRE( common::Vector_Init( &tris, &fx.allocator ) );
    REQUIRE( Planar_TryTriangulateRegion( &fx.region, &tris ) == geometry_status_t::OK );
    // The contact (0,2) sits mid-edge on the outer, so the merged ring
    // duplicates one point instead of a bridge's two: 4 + 1 + 4 - 2 = 7.
    CheckTriangulation( &fx.region, tris, 7u );
    common::Vector_Shutdown( &tris );
}

TEST_CASE( "PlanarTriangulate: hole sharing a vertex with the outer",
           "[Planar][Triangulate]" )
{
    RegionFixture fx;
    const vec2d_t outer[] = { { 0, 0 }, { 4, 0 }, { 4, 4 }, { 0, 4 } };
    const vec2d_t hole[] = { { 0, 0 }, { 1, 2 }, { 2, 2 }, { 2, 1 } }; // CW, touches corner
    fx.AddPolygon( outer );
    fx.AddHole( hole );
    REQUIRE( PlanarRegion_Validate( &fx.region, fx.policy ).status == geometry_status_t::OK );
    common::vector_t<planar_triangle_t> tris{};
    REQUIRE( common::Vector_Init( &tris, &fx.allocator ) );
    REQUIRE( Planar_TryTriangulateRegion( &fx.region, &tris ) == geometry_status_t::OK );
    CheckTriangulation( &fx.region, tris, 6u ); // ring 4 + 4, no extra point
    common::Vector_Shutdown( &tris );
}

TEST_CASE( "PlanarTriangulate: overlay output triangulates", "[Planar][Triangulate]" )
{
    // Square minus an inner square: the typical cap-face workflow.
    RegionFixture a;
    const vec2d_t outer[] = { { 0, 0 }, { 6, 0 }, { 6, 6 }, { 0, 6 } };
    const vec2d_t hole[] = { { 2, 2 }, { 2, 4 }, { 4, 4 }, { 4, 2 } };
    a.AddPolygon( outer );
    a.AddHole( hole );
    common::vector_t<planar_triangle_t> tris{};
    REQUIRE( common::Vector_Init( &tris, &a.allocator ) );
    REQUIRE( Planar_TryTriangulateRegion( &a.region, &tris ) == geometry_status_t::OK );
    CheckTriangulation( &a.region, tris, 8u );
    common::Vector_Shutdown( &tris );
}

TEST_CASE( "PlanarTriangulate: failure leaves output untouched", "[Planar][Triangulate]" )
{
    RegionFixture fx;
    fx.AddPolygon( kSquare4 );
    const vec2d_t bow[] = { { 10, 0 }, { 14, 4 }, { 14, 0 }, { 10, 4 } };
    fx.AddPolygon( bow ); // self-intersecting: cannot be ear clipped
    common::vector_t<planar_triangle_t> tris{};
    REQUIRE( common::Vector_Init( &tris, &fx.allocator ) );
    CHECK( Planar_TryTriangulateRegion( &fx.region, &tris ) == geometry_status_t::DEGENERATE );
    CHECK( tris.nCount == 0u );
    common::Vector_Shutdown( &tris );
}

TEST_CASE( "PlanarTriangulate: argument checks", "[Planar][Triangulate]" )
{
    RegionFixture fx;
    fx.AddPolygon( kSquare4 );
    common::vector_t<planar_triangle_t> uninit{};
    CHECK( Planar_TryTriangulateRegion( &fx.region, &uninit ) ==
           geometry_status_t::INVALID_ARGUMENT );
    common::vector_t<planar_triangle_t> tris{};
    REQUIRE( common::Vector_Init( &tris, &fx.allocator ) );
    CHECK( Planar_TryTriangulatePolygon( &fx.region, 5, &tris ) ==
           geometry_status_t::INVALID_ARGUMENT );
    planar_region_t empty{};
    CHECK( Planar_TryTriangulateRegion( &empty, &tris ) == geometry_status_t::NOT_INITIALIZED );
    common::Vector_Shutdown( &tris );
}

} // namespace cypher::editor::geometry
