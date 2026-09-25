//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Planar_Offset_Tests.cpp
//  Purpose: Contract tests for polyline stroking and region offsetting.
//  Details: Mitre and bevel results have exact closed-form areas. Round
//           joins are polygonal approximations of a circle, so they are
//           bounded: strictly larger than bevel, no larger than the true
//           circular area.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Planar_Offset.h"
#include "CypherGeometry_PlanarRegionValidation.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>

namespace cypher::editor::geometry {

using Catch::Approx;
using math::vec2d_t;

namespace {

constexpr double kPi = 3.14159265358979323846;

planar_frame_t XY()
{
    planar_frame_t f{};
    REQUIRE( PlanarFrame_TryFromPlane( math::Planed_Make( math::Vec3d_Make( 0, 0, 1 ), 0.0 ), &f ) ==
             geometry_status_t::OK );
    return f;
}

struct Out {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    planar_region_t r{};
    geometry_source_id_allocator_t ids{ geometry_source_id_t{ 100u } };
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
    ~Out() { PlanarRegion_Shutdown( &r ); }
    double Area() const { return PlanarRegion_Area( &r ); }
    void CheckValid() const {
        const planar_region_validation_t v = PlanarRegion_Validate( &r, geometry_policy_t{} );
        INFO( PlanarRegionFault_Name( v.fault ) );
        CHECK( v.status == geometry_status_t::OK );
    }
};

template <common::usize N>
void Stroke( Out &o, const vec2d_t ( &p )[N], bool closed, double hw, planar_offset_options_t opt = {} )
{
    o.status = Planar_TryStrokePolyline( XY(), common::span_t<const vec2d_t>{ p, N }, closed, hw, opt,
                                         geometry_policy_t{}, geometry_source_id_t{ 1u }, &o.ids,
                                         &o.allocator, &o.r );
}

struct Src {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    planar_region_t r{};
    common::u64 id{ 10u };
    Src() {
        REQUIRE( PlanarRegion_Init( &r, &allocator, XY(), geometry_source_id_t{ id++ } ) ==
                 geometry_status_t::OK );
    }
    ~Src() { PlanarRegion_Shutdown( &r ); }
    void Rect( double x0, double y0, double x1, double y1 ) {
        const vec2d_t p[] = { { x0, y0 }, { x1, y0 }, { x1, y1 }, { x0, y1 } };
        const geometry_source_id_t a{ id++ }, b{ id++ };
        REQUIRE( PlanarRegion_TryAddPolygon( &r, a, b, common::span_t<const vec2d_t>{ p, 4 }, nullptr ) ==
                 geometry_status_t::OK );
    }
    void HoleRect( double x0, double y0, double x1, double y1 ) {
        const vec2d_t p[] = { { x0, y0 }, { x0, y1 }, { x1, y1 }, { x1, y0 } };
        REQUIRE( PlanarRegion_TryAddHole( &r, geometry_source_id_t{ id++ },
                                          common::span_t<const vec2d_t>{ p, 4 } ) == geometry_status_t::OK );
    }
};

void Offset( Out &o, const Src &s, double d, planar_offset_options_t opt = {} )
{
    o.status = Planar_TryOffsetRegion( &s.r, d, opt, geometry_policy_t{}, geometry_source_id_t{ 2u },
                                       &o.ids, &o.allocator, &o.r );
}

} // namespace

// ===========================================================================
// Stroke
// ===========================================================================

TEST_CASE( "PlanarStroke: single segment with butt and round caps", "[Planar][Offset]" )
{
    const vec2d_t seg[] = { { 0, 0 }, { 4, 0 } };
    Out butt;
    Stroke( butt, seg, false, 1.0 );
    REQUIRE( butt.status == geometry_status_t::OK );
    CHECK( butt.Area() == Approx( 8.0 ) );
    butt.CheckValid();

    planar_offset_options_t o{};
    o.bRoundCaps = true;
    Out round;
    Stroke( round, seg, false, 1.0, o );
    REQUIRE( round.status == geometry_status_t::OK );
    CHECK( round.Area() > 8.0 );
    CHECK( round.Area() <= 8.0 + kPi + 1e-9 );
    round.CheckValid();
}

TEST_CASE( "PlanarStroke: L corner with each join", "[Planar][Offset]" )
{
    const vec2d_t L[] = { { 0, 0 }, { 4, 0 }, { 4, 4 } };
    planar_offset_options_t miter{};
    Out m;
    Stroke( m, L, false, 1.0, miter );
    REQUIRE( m.status == geometry_status_t::OK );
    CHECK( m.Area() == Approx( 16.0 ) ); // 8 + 8 - 1 overlap + 1 mitre square
    CHECK( PlanarRegion_PolygonCount( &m.r ) == 1u );
    m.CheckValid();

    planar_offset_options_t bevel{};
    bevel.join = planar_join_t::BEVEL;
    Out b;
    Stroke( b, L, false, 1.0, bevel );
    REQUIRE( b.status == geometry_status_t::OK );
    CHECK( b.Area() == Approx( 15.5 ) );

    planar_offset_options_t round{};
    round.join = planar_join_t::ROUND;
    Out r;
    Stroke( r, L, false, 1.0, round );
    REQUIRE( r.status == geometry_status_t::OK );
    CHECK( r.Area() > 15.5 );
    CHECK( r.Area() <= 15.0 + kPi / 4.0 + 1e-9 );
}

TEST_CASE( "PlanarStroke: closed square becomes an annulus", "[Planar][Offset]" )
{
    const vec2d_t sq[] = { { 0, 0 }, { 4, 0 }, { 4, 4 }, { 0, 4 } };
    Out o;
    Stroke( o, sq, true, 0.5 );
    REQUIRE( o.status == geometry_status_t::OK );
    CHECK( PlanarRegion_PolygonCount( &o.r ) == 1u );
    CHECK( PlanarRegion_ContourCount( &o.r ) == 2u );
    CHECK( o.Area() == Approx( 25.0 - 9.0 ) );
    CHECK( PlanarRegion_PointCount( &o.r ) == 8u ); // collinear piece seams removed
    o.CheckValid();
}

TEST_CASE( "PlanarStroke: mitre limit falls back to bevel", "[Planar][Offset]" )
{
    const vec2d_t spike[] = { { 0, 0 }, { 10, 0.5 }, { 0, 1 } };
    planar_offset_options_t miter{};
    miter.fMiterLimit = 2.0;
    Out limited;
    Stroke( limited, spike, false, 0.2, miter );
    REQUIRE( limited.status == geometry_status_t::OK );
    planar_offset_options_t bevel{};
    bevel.join = planar_join_t::BEVEL;
    Out bev;
    Stroke( bev, spike, false, 0.2, bevel );
    REQUIRE( bev.status == geometry_status_t::OK );
    CHECK( limited.Area() == Approx( bev.Area() ) );
}

TEST_CASE( "PlanarStroke: duplicates ignored, degenerate rejected", "[Planar][Offset]" )
{
    const vec2d_t dup[] = { { 0, 0 }, { 0, 0 }, { 2, 0 }, { 2, 0 } };
    Out o;
    Stroke( o, dup, false, 0.5 );
    REQUIRE( o.status == geometry_status_t::OK );
    CHECK( o.Area() == Approx( 2.0 ) );

    const vec2d_t pt[] = { { 1, 1 }, { 1, 1 } };
    Out d;
    Stroke( d, pt, false, 0.5 );
    CHECK( d.status == geometry_status_t::DEGENERATE );
    CHECK( d.ids.next.value == 100u ); // untouched on failure

    const vec2d_t seg[] = { { 0, 0 }, { 1, 0 } };
    Out bad;
    Stroke( bad, seg, false, 0.0 );
    CHECK( bad.status == geometry_status_t::INVALID_ARGUMENT );
}

// ===========================================================================
// Region offset
// ===========================================================================

TEST_CASE( "PlanarOffset: outset and inset a square", "[Planar][Offset]" )
{
    Src s;
    s.Rect( 0, 0, 4, 4 );
    Out out;
    Offset( out, s, 1.0 );
    REQUIRE( out.status == geometry_status_t::OK );
    CHECK( out.Area() == Approx( 36.0 ) );
    CHECK( PlanarRegion_PointCount( &out.r ) == 4u );
    out.CheckValid();

    Out in;
    Offset( in, s, -1.0 );
    REQUIRE( in.status == geometry_status_t::OK );
    CHECK( in.Area() == Approx( 4.0 ) );
    in.CheckValid();

    planar_offset_options_t round{};
    round.join = planar_join_t::ROUND;
    Out r;
    Offset( r, s, 1.0, round );
    REQUIRE( r.status == geometry_status_t::OK );
    CHECK( r.Area() > 16.0 + 16.0 + 2.0 ); // above bevel (4 x 0.5)
    CHECK( r.Area() <= 16.0 + 16.0 + kPi + 1e-9 );
}

TEST_CASE( "PlanarOffset: inset past the half-width vanishes", "[Planar][Offset]" )
{
    Src s;
    s.Rect( 0, 0, 4, 4 );
    Out in;
    Offset( in, s, -2.5 );
    REQUIRE( in.status == geometry_status_t::OK );
    CHECK( PlanarRegion_PolygonCount( &in.r ) == 0u );
}

TEST_CASE( "PlanarOffset: holes grow when the region shrinks", "[Planar][Offset]" )
{
    Src s;
    s.Rect( 0, 0, 10, 10 );
    s.HoleRect( 4, 4, 6, 6 );
    Out in;
    Offset( in, s, -1.0 );
    REQUIRE( in.status == geometry_status_t::OK );
    CHECK( in.Area() == Approx( 64.0 - 16.0 ) );
    CHECK( PlanarRegion_ContourCount( &in.r ) == 2u );
    in.CheckValid();

    Out out;
    Offset( out, s, 1.0 ); // hole [4,6]^2 shrinks to nothing (2 wide, grows 1 each side)
    REQUIRE( out.status == geometry_status_t::OK );
    CHECK( PlanarRegion_ContourCount( &out.r ) == 1u );
    CHECK( out.Area() == Approx( 144.0 ) );
}

TEST_CASE( "PlanarOffset: concave L with mitre joins", "[Planar][Offset]" )
{
    Src s;
    const vec2d_t L[] = { { 0, 0 }, { 4, 0 }, { 4, 1 }, { 1, 1 }, { 1, 4 }, { 0, 4 } };
    const geometry_source_id_t a{ 90 }, b{ 91 };
    REQUIRE( PlanarRegion_TryAddPolygon( &s.r, a, b, common::span_t<const vec2d_t>{ L, 6 }, nullptr ) ==
             geometry_status_t::OK );
    Out out;
    Offset( out, s, 0.5 );
    REQUIRE( out.status == geometry_status_t::OK );
    // Mitred outset of an L by 0.5 is the L with arms 1 -> 2 wide and
    // lengths 4 -> 5: [-0.5,4.5]x[-0.5,1.5] ∪ [-0.5,1.5]x[-0.5,4.5].
    CHECK( out.Area() == Approx( 5.0 * 2.0 + 2.0 * 5.0 - 4.0 ) );
    out.CheckValid();
}

TEST_CASE( "PlanarOffset: argument contract", "[Planar][Offset]" )
{
    Src s;
    s.Rect( 0, 0, 1, 1 );
    Out o;
    Offset( o, s, 0.0 );
    CHECK( o.status == geometry_status_t::INVALID_ARGUMENT );
    planar_region_t empty{};
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_source_id_allocator_t ids{};
    planar_region_t out{};
    CHECK( Planar_TryOffsetRegion( &empty, 1.0, {}, {}, geometry_source_id_t{ 1 }, &ids, &allocator, &out ) ==
           geometry_status_t::NOT_INITIALIZED );
}

} // namespace cypher::editor::geometry
