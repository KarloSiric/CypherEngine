//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_PatchPrimitives_Tests.cpp
//  Purpose: Contract tests for revolved and extruded patch primitives.
//  Details: Oracles: the quadratic circle approximation's radial error lies
//           in [0, e(n)] with e(n) = (cos(pi/n) + sec(pi/n)) / 2 - 1 and
//           reaches e(n) mid-arc; sphere points are within
//           [R, R (1 + e)^2] because both factors of the tensor product
//           err outward; tangent-intersection controls give G1 joins, so
//           normals agree across sub-patch seams.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_PatchPrimitives.h"
#include "CypherGeometry_PatchTessellation.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace cypher::editor::geometry {

using Catch::Approx;
using math::vec2d_t;
using math::vec3d_t;
using math::Vec3d_Make;

namespace {

geometry_source_id_t Id( common::u64 v ) { return geometry_source_id_t{ v }; }

struct Fixture {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_source_id_allocator_t ids{ geometry_source_id_t{ 1000u } };
    patch_surface_t patch{};
    ~Fixture() { Patch_Shutdown( &patch ); }
};

// Distance from the z axis through (cx, cy).
double AxisDistance( vec3d_t p, double cx = 0.0, double cy = 0.0 ) {
    return std::sqrt( ( p.x - cx ) * ( p.x - cx ) + ( p.y - cy ) * ( p.y - cy ) );
}

} // namespace

TEST_CASE( "Circle radial error formula matches known values", "[geometry][patch][primitives]" ) {
    CHECK( PatchPrimitive_CircleRadialError( 4 ) == Approx( 0.0606601717798 ).epsilon( 1e-9 ) );
    CHECK( PatchPrimitive_CircleRadialError( 8 ) == Approx( 0.0031358664 ).epsilon( 1e-8 ) );
    CHECK( PatchPrimitive_CircleRadialError( 16 ) == Approx( 0.000188219305774 ).epsilon( 1e-9 ) );
    CHECK( PatchPrimitive_CircleRadialError( 2 ) == std::numeric_limits<double>::infinity() );
    CHECK( PatchPrimitive_CircleRadialError( kPatchPrimitiveSegmentsMax + 1u ) ==
           std::numeric_limits<double>::infinity() );
}

TEST_CASE( "Cylinder patch stays in its radial error band and closes its seam", "[geometry][patch][primitives]" ) {
    for ( common::u32 n : { 3u, 4u, 8u, 13u } ) {
        Fixture f;
        patch_revolve_frame_t frame{};
        frame.center = Vec3d_Make( 2.0, -1.0, 0.5 );
        patch_primitive_common_t c{};
        c.cSegments = n;
        REQUIRE( PatchPrimitive_TryMakeCone( frame, 3.0, 3.0, 5.0, c, Id( 1 ), &f.ids, &f.allocator, &f.patch ) ==
                 geometry_status_t::OK );
        CHECK( f.patch.cColumns == 2u * n + 1u );
        CHECK( f.patch.cRows == 3u );
        CHECK( Patch_Validate( &f.patch, &f.allocator ).fault == patch_fault_t::NONE );
        CHECK( f.ids.next.value == 1000u + ( 2u * n + 1u ) * 3u );

        const double e = PatchPrimitive_CircleRadialError( n );
        double worst = 0.0;
        for ( int iu = 0; iu <= 200; ++iu ) {
            const double u = iu / 200.0;
            const patch_sample_t s = Patch_Evaluate( &f.patch, u, 0.37 );
            const double r = AxisDistance( s.position, 2.0, -1.0 );
            CHECK( r >= 3.0 * ( 1.0 - 1e-12 ) );
            CHECK( r <= 3.0 * ( 1.0 + e ) * ( 1.0 + 1e-12 ) );
            worst = std::max( worst, r / 3.0 - 1.0 );
            // Height is exactly linear in v.
            CHECK( s.position.z == Approx( 0.5 + 0.37 * 5.0 ) );
            // Outward normal.
            const vec3d_t radial = Vec3d_Make( s.position.x - 2.0, s.position.y + 1.0, 0.0 );
            CHECK( math::Vec3d_Dot( s.normal, radial ) > 0.0 );
        }
        // The peak (mid-arc) is sampled when 200 is a multiple of 2n, and is
        // approached closely otherwise.
        CHECK( worst == Approx( e ).epsilon( 0.02 ) );
        // Seam: first and last column are bit-identical in every row.
        for ( common::u32 j = 0; j < 3; ++j ) {
            CHECK( math::Vec3d_EqualsExact( Patch_Control( &f.patch, 0, j )->position,
                                            Patch_Control( &f.patch, 2u * n, j )->position ) );
            CHECK( Patch_Control( &f.patch, 0, j )->uv.x == 0.0 );
            CHECK( Patch_Control( &f.patch, 2u * n, j )->uv.x == 1.0 );
        }
    }
}

TEST_CASE( "Arc joins are tangent-continuous (G1)", "[geometry][patch][primitives]" ) {
    Fixture f;
    patch_primitive_common_t c{};
    c.cSegments = 6u;
    REQUIRE( PatchPrimitive_TryMakeSphere( patch_revolve_frame_t{}, 4.0, 4u, c, Id( 1 ), &f.ids, &f.allocator,
                                           &f.patch ) == geometry_status_t::OK );
    const common::u32 cu = Patch_SubPatchColumns( &f.patch ), cv = Patch_SubPatchRows( &f.patch );
    CHECK( cu == 6u );
    CHECK( cv == 4u );
    for ( common::u32 a = 0; a + 1u < cu; ++a ) {
        for ( common::u32 b = 0; b < cv; ++b ) {
            for ( double t : { 0.3, 0.8 } ) {
                const patch_sample_t l = Patch_EvaluateSubPatch( &f.patch, a, b, 1.0, t );
                const patch_sample_t r = Patch_EvaluateSubPatch( &f.patch, a + 1u, b, 0.0, t );
                CHECK( math::Vec3d_DistanceSquared( l.position, r.position ) < 1e-24 );
                CHECK( math::Vec3d_Dot( l.normal, r.normal ) > 1.0 - 1e-12 );
            }
        }
    }
    // Across meridian arcs too.
    for ( common::u32 b = 0; b + 1u < cv; ++b ) {
        const patch_sample_t lo = Patch_EvaluateSubPatch( &f.patch, 2, b, 0.4, 1.0 );
        const patch_sample_t hi = Patch_EvaluateSubPatch( &f.patch, 2, b + 1u, 0.4, 0.0 );
        CHECK( math::Vec3d_Dot( lo.normal, hi.normal ) > 1.0 - 1e-12 );
    }
}

TEST_CASE( "Sphere patch: collapsed poles, outward normals, bounded error", "[geometry][patch][primitives]" ) {
    Fixture f;
    patch_revolve_frame_t frame{};
    frame.center = Vec3d_Make( 0.0, 0.0, 10.0 );
    patch_primitive_common_t c{};
    c.cSegments = 8u;
    REQUIRE( PatchPrimitive_TryMakeSphere( frame, 2.0, 4u, c, Id( 1 ), &f.ids, &f.allocator, &f.patch ) ==
             geometry_status_t::OK );
    CHECK( Patch_Validate( &f.patch, &f.allocator ).fault == patch_fault_t::NONE );
    for ( common::u32 i = 0; i < f.patch.cColumns; ++i ) {
        CHECK( math::Vec3d_EqualsExact( Patch_Control( &f.patch, i, 0 )->position, Vec3d_Make( 0, 0, 8 ) ) );
        CHECK( math::Vec3d_EqualsExact( Patch_Control( &f.patch, i, f.patch.cRows - 1u )->position,
                                        Vec3d_Make( 0, 0, 12 ) ) );
    }
    const double eu = PatchPrimitive_CircleRadialError( 8 );
    const double ev = PatchPrimitive_CircleRadialError( 8 ); // 4 half-meridian arcs = 45 deg each
    for ( int iu = 0; iu <= 40; ++iu ) {
        for ( int iv = 0; iv <= 40; ++iv ) {
            const patch_sample_t s = Patch_Evaluate( &f.patch, iu / 40.0, iv / 40.0 );
            const vec3d_t d = math::Vec3d_Subtract( s.position, frame.center );
            const double r = std::sqrt( math::Vec3d_LengthSquared( d ) );
            CHECK( r >= 2.0 * ( 1.0 - 1e-12 ) );
            CHECK( r <= 2.0 * ( 1.0 + eu ) * ( 1.0 + ev ) * ( 1.0 + 1e-12 ) );
            REQUIRE( s.bNormalValid );
            CHECK( math::Vec3d_Dot( s.normal, d ) > 0.0 );
        }
    }
    // Pole normals come from the interior fallback and point along the axis.
    const patch_sample_t south = Patch_Evaluate( &f.patch, 0.3, 0.0 );
    CHECK( south.bNormalFromNeighbour );
    CHECK( south.normal.z < -0.99 );

    // Tessellation drops the collapsed pole triangles and stays consistent.
    patch_tessellation_t t{};
    REQUIRE( PatchTessellation_Init( &t, &f.allocator ) == geometry_status_t::OK );
    patch_tessellation_options_t o{};
    o.fMaxChordError = 0.01;
    REQUIRE( PatchTessellation_TryBuild( &f.patch, o, &t ) == geometry_status_t::OK );
    CHECK( t.cCollapsedTriangles > 0u );
    CHECK( t.cUnresolvedNormals == 0u );
    const common::usize cTris = t.indices.nCount / 3u;
    for ( common::usize k = 0; k < cTris; ++k ) {
        const vec3d_t a = t.positions.pData[t.indices.pData[k * 3]];
        const vec3d_t b = t.positions.pData[t.indices.pData[k * 3 + 1]];
        const vec3d_t cc = t.positions.pData[t.indices.pData[k * 3 + 2]];
        const vec3d_t n = math::Vec3d_Cross( math::Vec3d_Subtract( b, a ), math::Vec3d_Subtract( cc, a ) );
        const vec3d_t g = math::Vec3d_Subtract(
            math::Vec3d_Scale( math::Vec3d_Add( math::Vec3d_Add( a, b ), cc ), 1.0 / 3.0 ), frame.center );
        CHECK( math::Vec3d_Dot( n, g ) > 0.0 );
    }
    PatchTessellation_Shutdown( &t );
}

TEST_CASE( "Inward flag flips the front face", "[geometry][patch][primitives]" ) {
    Fixture out, in;
    patch_primitive_common_t c{};
    REQUIRE( PatchPrimitive_TryMakeCone( patch_revolve_frame_t{}, 2.0, 2.0, 1.0, c, Id( 1 ), &out.ids, &out.allocator,
                                         &out.patch ) == geometry_status_t::OK );
    c.bInward = true;
    REQUIRE( PatchPrimitive_TryMakeCone( patch_revolve_frame_t{}, 2.0, 2.0, 1.0, c, Id( 1 ), &in.ids, &in.allocator,
                                         &in.patch ) == geometry_status_t::OK );
    for ( double u : { 0.1, 0.45, 0.9 } ) {
        const patch_sample_t a = Patch_Evaluate( &out.patch, u, 0.5 );
        const patch_sample_t b = Patch_Evaluate( &in.patch, 1.0 - u, 0.5 );
        CHECK( math::Vec3d_DistanceSquared( a.position, b.position ) < 1e-24 );
        CHECK( math::Vec3d_Dot( a.normal, b.normal ) < -0.999999 );
    }
}

TEST_CASE( "Cone apex collapses and disc caps share the cylinder rim", "[geometry][patch][primitives]" ) {
    patch_revolve_frame_t frame{};
    frame.center = Vec3d_Make( 1.0, 1.0, 0.0 );
    frame.axis = Vec3d_Make( 0.0, 0.0, 2.0 ); // any length
    patch_primitive_common_t c{};
    c.cSegments = 8u;

    Fixture cone;
    REQUIRE( PatchPrimitive_TryMakeCone( frame, 2.0, 0.0, 3.0, c, Id( 1 ), &cone.ids, &cone.allocator,
                                         &cone.patch ) == geometry_status_t::OK );
    CHECK( math::Vec3d_EqualsExact( Patch_Evaluate( &cone.patch, 0.3, 1.0 ).position, Vec3d_Make( 1.0, 1.0, 3.0 ) ) );
    CHECK( Patch_Evaluate( &cone.patch, 0.3, 1.0 ).bNormalFromNeighbour );

    Fixture wall, cap;
    REQUIRE( PatchPrimitive_TryMakeCone( frame, 2.0, 2.0, 3.0, c, Id( 2 ), &wall.ids, &wall.allocator,
                                         &wall.patch ) == geometry_status_t::OK );
    c.bInward = true; // bottom cap faces -axis
    REQUIRE( PatchPrimitive_TryMakeDisc( frame, 2.0, c, Id( 3 ), &cap.ids, &cap.allocator, &cap.patch ) ==
             geometry_status_t::OK );
    CHECK( Patch_Evaluate( &cap.patch, 0.4, 0.5 ).normal.z == Approx( -1.0 ) );
    // Rim of the cap (row 0) equals the wall's bottom row as a point set:
    // the cap's columns are reversed by bInward.
    for ( common::u32 i = 0; i < wall.patch.cColumns; ++i ) {
        const vec3d_t w = Patch_Control( &wall.patch, i, 0 )->position;
        const vec3d_t k = Patch_Control( &cap.patch, cap.patch.cColumns - 1u - i, 0 )->position;
        CHECK( math::Vec3d_EqualsExact( w, k ) );
    }
    // Every cap point is flat.
    for ( double u : { 0.0, 0.33, 0.8 } ) {
        for ( double v : { 0.0, 0.5, 1.0 } ) {
            CHECK( Patch_Evaluate( &cap.patch, u, v ).position.z == 0.0 );
        }
    }
}

TEST_CASE( "Torus patch closes in both directions", "[geometry][patch][primitives]" ) {
    Fixture f;
    patch_primitive_common_t c{};
    c.cSegments = 8u;
    REQUIRE( PatchPrimitive_TryMakeTorus( patch_revolve_frame_t{}, 5.0, 1.0, 6u, c, Id( 1 ), &f.ids, &f.allocator,
                                          &f.patch ) == geometry_status_t::OK );
    CHECK( f.patch.cRows == 13u );
    for ( common::u32 i = 0; i < f.patch.cColumns; ++i ) {
        CHECK( math::Vec3d_EqualsExact( Patch_Control( &f.patch, i, 0 )->position,
                                        Patch_Control( &f.patch, i, 12 )->position ) );
    }
    for ( int k = 0; k <= 30; ++k ) {
        const patch_sample_t s = Patch_Evaluate( &f.patch, 0.17, k / 30.0 );
        // Distance from the tube's centre circle, measured in the meridian
        // plane of the sample.
        const double ring = AxisDistance( s.position );
        const double tube = std::sqrt( ( ring - 5.0 ) * ( ring - 5.0 ) + s.position.z * s.position.z );
        CHECK( tube > 0.9 );
        CHECK( tube < 1.2 );
        // Normal points away from the tube centre.
        const vec3d_t centre = math::Vec3d_Scale( Vec3d_Make( s.position.x, s.position.y, 0.0 ), 5.0 / ring );
        CHECK( math::Vec3d_Dot( s.normal, math::Vec3d_Subtract( s.position, centre ) ) > 0.0 );
    }
    // A tube too fat for the ring is rejected.
    Fixture g;
    CHECK( PatchPrimitive_TryMakeTorus( patch_revolve_frame_t{}, 1.0, 0.99, 3u, c, Id( 1 ), &g.ids, &g.allocator,
                                        &g.patch ) == geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Bevel and extrusion build ruled strips", "[geometry][patch][primitives]" ) {
    Fixture f;
    REQUIRE( PatchPrimitive_TryMakeBevel( Vec3d_Make( 0, 0, 0 ), Vec3d_Make( 2, 0, 0 ), Vec3d_Make( 0, 2, 0 ),
                                          Vec3d_Make( 0, 0, 4 ), false, Id( 1 ), &f.ids, &f.allocator, &f.patch ) ==
             geometry_status_t::OK );
    CHECK( f.patch.cColumns == 3u );
    CHECK( f.patch.cRows == 3u );
    // Midpoint of the quadratic (2,0)-(0,0)-(0,2) is (0.5, 0.5).
    const patch_sample_t mid = Patch_Evaluate( &f.patch, 0.5, 0.25 );
    CHECK( mid.position.x == Approx( 0.5 ) );
    CHECK( mid.position.y == Approx( 0.5 ) );
    CHECK( mid.position.z == Approx( 1.0 ) );
    // Tangent to the sides at the ends: dPdu at u = 0 is along -x.
    const patch_sample_t start = Patch_Evaluate( &f.patch, 0.0, 0.5 );
    CHECK( std::fabs( start.dPdu.y ) < 1e-12 );
    CHECK( start.dPdu.x < 0.0 );
    // Front face: dPdu x depth = (-2, 2, 0) x (0, 0, 4) = (8, 8, 0), i.e.
    // away from the corner for this winding.
    CHECK( mid.normal.x > 0.0 );
    CHECK( mid.normal.y > 0.0 );

    Fixture g;
    const vec3d_t profile[5] = { Vec3d_Make( 0, 0, 0 ), Vec3d_Make( 1, 1, 0 ), Vec3d_Make( 2, 0, 0 ),
                                 Vec3d_Make( 3, -1, 0 ), Vec3d_Make( 4, 0, 0 ) };
    REQUIRE( PatchPrimitive_TryExtrude( common::span_t<const vec3d_t>{ profile, 5 }, Vec3d_Make( 0, 0, 3 ), 2u, false,
                                        7u, Id( 1 ), &g.ids, &g.allocator, &g.patch ) == geometry_status_t::OK );
    CHECK( g.patch.cRows == 5u );
    CHECK( g.patch.materialId == 7u );
    CHECK( Patch_Evaluate( &g.patch, 0.25, 0.5 ).position.y == Approx( 0.5 ) );
    CHECK( Patch_Evaluate( &g.patch, 0.25, 0.5 ).position.z == Approx( 1.5 ) );
}

TEST_CASE( "Patch primitives reject bad parameters without consuming IDs", "[geometry][patch][primitives]" ) {
    Fixture f;
    patch_primitive_common_t c{};
    patch_revolve_frame_t frame{};
    CHECK( PatchPrimitive_TryMakeCone( frame, 1.0, 1.0, 0.0, c, Id( 1 ), &f.ids, &f.allocator, &f.patch ) ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK( PatchPrimitive_TryMakeCone( frame, 0.0, 0.0, 1.0, c, Id( 1 ), &f.ids, &f.allocator, &f.patch ) ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK( PatchPrimitive_TryMakeCone( frame, -1.0, 1.0, 1.0, c, Id( 1 ), &f.ids, &f.allocator, &f.patch ) ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK( PatchPrimitive_TryMakeSphere( frame, 1.0, 1u, c, Id( 1 ), &f.ids, &f.allocator, &f.patch ) ==
           geometry_status_t::INVALID_ARGUMENT );
    c.cSegments = 2u;
    CHECK( PatchPrimitive_TryMakeDisc( frame, 1.0, c, Id( 1 ), &f.ids, &f.allocator, &f.patch ) ==
           geometry_status_t::INVALID_ARGUMENT );
    c.cSegments = 8u;
    frame.reference = Vec3d_Make( 0.0, 0.0, 5.0 ); // parallel to the axis
    CHECK( PatchPrimitive_TryMakeDisc( frame, 1.0, c, Id( 1 ), &f.ids, &f.allocator, &f.patch ) ==
           geometry_status_t::INVALID_ARGUMENT );
    frame = patch_revolve_frame_t{};
    frame.center.x = std::nan( "" );
    CHECK( PatchPrimitive_TryMakeDisc( frame, 1.0, c, Id( 1 ), &f.ids, &f.allocator, &f.patch ) ==
           geometry_status_t::INVALID_ARGUMENT );
    frame = patch_revolve_frame_t{};
    CHECK( PatchPrimitive_TryMakeDisc( frame, kPatchCoordinateMax * 2.0, c, Id( 1 ), &f.ids, &f.allocator,
                                       &f.patch ) == geometry_status_t::NUMERIC_FAILURE );
    const vec2d_t evenProfile[2] = { { 1.0, 0.0 }, { 1.0, 1.0 } };
    CHECK( PatchPrimitive_TryRevolve( common::span_t<const vec2d_t>{ evenProfile, 2 }, frame, c, Id( 1 ), &f.ids,
                                      &f.allocator, &f.patch ) == geometry_status_t::INVALID_ARGUMENT );
    const vec2d_t axisOnly[3] = { { 0.0, 0.0 }, { 0.0, 1.0 }, { 0.0, 2.0 } };
    CHECK( PatchPrimitive_TryRevolve( common::span_t<const vec2d_t>{ axisOnly, 3 }, frame, c, Id( 1 ), &f.ids,
                                      &f.allocator, &f.patch ) == geometry_status_t::DEGENERATE );
    CHECK( PatchPrimitive_TryMakeBevel( Vec3d_Make( 0, 0, 0 ), Vec3d_Make( 1, 0, 0 ), Vec3d_Make( 2, 0, 0 ),
                                        Vec3d_Make( 0, 0, 1 ), false, Id( 1 ), &f.ids, &f.allocator, &f.patch ) ==
           geometry_status_t::DEGENERATE );
    CHECK( PatchPrimitive_TryMakeBevel( Vec3d_Make( 0, 0, 0 ), Vec3d_Make( 1, 0, 0 ), Vec3d_Make( 0, 1, 0 ),
                                        Vec3d_Make( 0, 0, 0 ), false, Id( 1 ), &f.ids, &f.allocator, &f.patch ) ==
           geometry_status_t::DEGENERATE );
    CHECK( f.ids.next.value == 1000u );
    CHECK_FALSE( Patch_IsInitialized( &f.patch ) );
}

} // namespace cypher::editor::geometry
