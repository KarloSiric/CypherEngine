//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushShapes_Tests.cpp
//  Purpose: Contract tests for the box-fitted draw-shape brushes.
//  Details: Oracles are closed forms: the inscribed polygon of an ellipse
//           with semi-axes a, b has area (n/2) a b sin(2 pi / n), the
//           circumscribed one n a b tan(pi / n); prisms are area x height,
//           cones area x height / 3; arches are the difference of the two
//           half-outlines x depth; stairs sum their steps. Scalable circles
//           must put every vertex on integer coordinates when the box is a
//           multiple of their template size. Every brush must deep-validate
//           and stay inside its box, and every failure must leave the
//           destinations and the ID allocator untouched.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushShapes.h"
#include "CypherGeometry_BrushBoundary.h"
#include "CypherGeometry_BrushSource.h"
#include "CypherGeometry_BrushValidation.h"
#include "CypherMath_Predicates.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <vector>

namespace cypher::editor::geometry {

using Catch::Approx;
using math::vec2d_t;
using math::vec3d_t;
using math::Vec3d_Make;

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Env {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t ids{ geometry_source_id_t{ 100 } };
};

struct Brush {
    brush_solid_t b{};
    ~Brush() { BrushSolid_Shutdown( &b ); }
};

brush_shape_box_t Box( double x0, double y0, double z0, double x1, double y1, double z1 ) {
    return brush_shape_box_t{ Vec3d_Make( x0, y0, z0 ), Vec3d_Make( x1, y1, z1 ) };
}

// Volume and bounds of a brush from its reconstructed boundary; also checks
// the brush deep-validates and every vertex lies in the box.
double Volume( Env &e, const brush_solid_t &b, const brush_shape_box_t *pBox = nullptr, vec3d_t *pLo = nullptr,
               vec3d_t *pHi = nullptr ) {
    REQUIRE( BrushValidation_Deep( &b, e.policy, &e.allocator ).status == geometry_status_t::OK );
    brush_boundary_t bd{};
    REQUIRE( BrushBoundary_Init( &bd, &e.allocator ) == geometry_status_t::OK );
    REQUIRE( BrushBoundary_TryReconstruct( &bd, &b, e.policy ) == geometry_status_t::OK );
    double v = 0.0;
    vec3d_t lo = bd.vertices.pData[0], hi = bd.vertices.pData[0];
    for ( common::usize i = 0; i < bd.vertices.nCount; ++i ) {
        const vec3d_t p = bd.vertices.pData[i];
        lo = Vec3d_Make( std::min( lo.x, p.x ), std::min( lo.y, p.y ), std::min( lo.z, p.z ) );
        hi = Vec3d_Make( std::max( hi.x, p.x ), std::max( hi.y, p.y ), std::max( hi.z, p.z ) );
        if ( pBox != nullptr ) {
            const double eps = 1e-9;
            CHECK( p.x >= pBox->lo.x - eps );
            CHECK( p.y >= pBox->lo.y - eps );
            CHECK( p.z >= pBox->lo.z - eps );
            CHECK( p.x <= pBox->hi.x + eps );
            CHECK( p.y <= pBox->hi.y + eps );
            CHECK( p.z <= pBox->hi.z + eps );
        }
    }
    for ( common::usize f = 0; f < BrushBoundary_FaceCount( &bd ); ++f ) {
        common::u32 idx[512];
        common::usize n = 0;
        REQUIRE( BrushBoundary_TryGetFaceVertexIndices( &bd, f, idx, 512, &n ) == geometry_status_t::OK );
        const vec3d_t p0 = bd.vertices.pData[idx[0]];
        for ( common::usize k = 1; k + 1 < n; ++k ) {
            const vec3d_t p1 = bd.vertices.pData[idx[k]], p2 = bd.vertices.pData[idx[k + 1]];
            v += math::Vec3d_Dot( p0, math::Vec3d_Cross( p1, p2 ) ) / 6.0;
        }
    }
    if ( pLo != nullptr ) { *pLo = lo; }
    if ( pHi != nullptr ) { *pHi = hi; }
    BrushBoundary_Shutdown( &bd );
    return v;
}

double PolygonArea( const vec2d_t *p, common::u32 n ) {
    double a = 0.0;
    for ( common::u32 i = 0; i < n; ++i ) { a += p[i].x * p[( i + 1 ) % n].y - p[( i + 1 ) % n].x * p[i].y; }
    return 0.5 * a;
}

bool StrictlyConvex( const vec2d_t *p, common::u32 n ) {
    for ( common::u32 i = 0; i < n; ++i ) {
        if ( math::Orient2D( p[( i + n - 1 ) % n], p[i], p[( i + 1 ) % n] ) <= 0 ) { return false; }
    }
    return true;
}

} // namespace

TEST_CASE( "Circle modes fit the rectangle the way they promise", "[geometry][brushshapes]" ) {
    vec2d_t p[300];
    common::u32 n = 0;
    SECTION( "Vertex-aligned 4: a diamond touching the sides" ) {
        REQUIRE( BrushShapes_TryMakeCircle( { -1, -1 }, { 1, 1 }, 4u, brush_circle_mode_t::VERTEX_ALIGNED, p, 300, &n ) ==
                 geometry_status_t::OK );
        REQUIRE( n == 4u );
        CHECK( ( p[0].x == 1 && p[0].y == 0 && p[1].x == 0 && p[1].y == 1 && p[2].x == -1 && p[2].y == 0 && p[3].x == 0 && p[3].y == -1 ) );
    }
    SECTION( "Edge-aligned 4: exactly the rectangle" ) {
        REQUIRE( BrushShapes_TryMakeCircle( { -1, -1 }, { 1, 1 }, 4u, brush_circle_mode_t::EDGE_ALIGNED, p, 300, &n ) ==
                 geometry_status_t::OK );
        REQUIRE( n == 4u );
        CHECK( ( p[0].x == 1 && p[0].y == 1 && p[1].x == -1 && p[1].y == 1 && p[2].x == -1 && p[2].y == -1 && p[3].x == 1 && p[3].y == -1 ) );
    }
    SECTION( "Areas of inscribed and circumscribed ellipse polygons" ) {
        for ( common::u32 sides : { 8u, 16u, 32u } ) {
            CAPTURE( sides );
            REQUIRE( BrushShapes_TryMakeCircle( { 0, 0 }, { 6, 2 }, sides, brush_circle_mode_t::VERTEX_ALIGNED, p, 300, &n ) ==
                     geometry_status_t::OK );
            CHECK( n == sides );
            CHECK( PolygonArea( p, n ) == Approx( 0.5 * sides * 3.0 * 1.0 * std::sin( 2 * kPi / sides ) ) );
            CHECK( StrictlyConvex( p, n ) );
            REQUIRE( BrushShapes_TryMakeCircle( { 0, 0 }, { 6, 2 }, sides, brush_circle_mode_t::EDGE_ALIGNED, p, 300, &n ) ==
                     geometry_status_t::OK );
            CHECK( PolygonArea( p, n ) == Approx( sides * 3.0 * 1.0 * std::tan( kPi / sides ) ) );
            CHECK( StrictlyConvex( p, n ) );
            // Four edges lie exactly on the rectangle.
            int cOnBox = 0;
            for ( common::u32 i = 0; i < n; ++i ) {
                const vec2d_t a = p[i], b = p[( i + 1 ) % n];
                cOnBox += ( a.x == b.x && ( a.x == 0 || a.x == 6 ) ) || ( a.y == b.y && ( a.y == 0 || a.y == 2 ) );
                CHECK( ( a.x >= 0 && a.x <= 6 && a.y >= 0 && a.y <= 2 ) );
            }
            CHECK( cOnBox == 4 );
        }
    }
    SECTION( "Scalable circles put every vertex on the grid" ) {
        for ( common::u32 sides : { 12u, 24u, 48u, 96u } ) {
            CAPTURE( sides );
            const common::u32 units = BrushShapes_ScalableCircleUnits( sides );
            REQUIRE( units > 0u );
            const double u = static_cast<double>( units );
            REQUIRE( BrushShapes_TryMakeCircle( { 0, 0 }, { u, u }, sides, brush_circle_mode_t::SCALABLE, p, 300, &n ) ==
                     geometry_status_t::OK );
            CHECK( n == sides );
            CHECK( StrictlyConvex( p, n ) );
            for ( common::u32 i = 0; i < n; ++i ) {
                CHECK( p[i].x == std::round( p[i].x ) );
                CHECK( p[i].y == std::round( p[i].y ) );
            }
            // A taller box keeps the round corners and adds straight runs.
            REQUIRE( BrushShapes_TryMakeCircle( { 0, 0 }, { u, u + 8 }, sides, brush_circle_mode_t::SCALABLE, p, 300, &n ) ==
                     geometry_status_t::OK );
            CHECK( n == sides + 2u );
            CHECK( StrictlyConvex( p, n ) );
            for ( common::u32 i = 0; i < n; ++i ) {
                CHECK( p[i].x == std::round( p[i].x ) );
                CHECK( p[i].y == std::round( p[i].y ) );
            }
        }
        CHECK( BrushShapes_ScalableCircleUnits( 12u ) == 12u );
    }
    SECTION( "Rejections" ) {
        CHECK( BrushShapes_TryMakeCircle( { 0, 0 }, { 1, 1 }, 6u, brush_circle_mode_t::EDGE_ALIGNED, p, 300, &n ) ==
               geometry_status_t::INVALID_ARGUMENT );
        CHECK( BrushShapes_TryMakeCircle( { 0, 0 }, { 1, 1 }, 16u, brush_circle_mode_t::SCALABLE, p, 300, &n ) ==
               geometry_status_t::INVALID_ARGUMENT );
        CHECK( BrushShapes_TryMakeCircle( { 0, 0 }, { 1, 1 }, 2u, brush_circle_mode_t::VERTEX_ALIGNED, p, 300, &n ) ==
               geometry_status_t::INVALID_ARGUMENT );
        CHECK( BrushShapes_TryMakeCircle( { 1, 0 }, { 1, 1 }, 8u, brush_circle_mode_t::VERTEX_ALIGNED, p, 300, &n ) ==
               geometry_status_t::INVALID_ARGUMENT );
        CHECK( BrushShapes_TryMakeCircle( { 0, 0 }, { 1, 1 }, 12u, brush_circle_mode_t::SCALABLE, p, 12, &n ) ==
               geometry_status_t::INSUFFICIENT_CAPACITY );
    }
}

TEST_CASE( "Cuboid, cylinders and cones fill their box", "[geometry][brushshapes]" ) {
    Env e;
    SECTION( "Cuboid" ) {
        Brush b;
        const auto box = Box( 1, 2, 3, 4, 6, 8 );
        REQUIRE( BrushShapes_TryMakeCuboid( &b.b, &e.allocator, e.policy, &e.ids, box ) == geometry_status_t::OK );
        CHECK( Volume( e, b.b, &box ) == Approx( 3.0 * 4.0 * 5.0 ) );
    }
    SECTION( "Cylinder along each axis" ) {
        for ( common::u32 axis : { 0u, 1u, 2u } ) {
            for ( brush_circle_mode_t mode : { brush_circle_mode_t::VERTEX_ALIGNED, brush_circle_mode_t::EDGE_ALIGNED } ) {
                CAPTURE( axis, static_cast<int>( mode ) );
                // Cross-section 4 x 2 (semi-axes 2, 1), length 3 along the axis.
                const double ext[3][3] = { { 3, 4, 2 }, { 2, 3, 4 }, { 4, 2, 3 } };
                const auto box = Box( 0, 0, 0, ext[axis][0], ext[axis][1], ext[axis][2] );
                Brush b;
                REQUIRE( BrushShapes_TryMakeCylinder( &b.b, &e.allocator, e.policy, &e.ids, box, axis, 16u, mode ) ==
                         geometry_status_t::OK );
                CHECK( BrushSolid_SideCount( &b.b ) == 18u );
                const double area = mode == brush_circle_mode_t::VERTEX_ALIGNED ? 8.0 * 2.0 * std::sin( kPi / 8.0 )
                                                                                 : 16.0 * 2.0 * std::tan( kPi / 16.0 );
                vec3d_t lo{}, hi{};
                CHECK( Volume( e, b.b, &box, &lo, &hi ) == Approx( area * 3.0 ) );
                // Touches all six box faces.
                CHECK( ( lo.x == Approx( 0 ).margin( 1e-12 ) && lo.y == Approx( 0 ).margin( 1e-12 ) && lo.z == Approx( 0 ).margin( 1e-12 ) ) );
                CHECK( ( hi.x == Approx( box.hi.x ) && hi.y == Approx( box.hi.y ) && hi.z == Approx( box.hi.z ) ) );
            }
        }
    }
    SECTION( "Scalable cylinder" ) {
        Brush b;
        const auto box = Box( 0, 0, 0, 12, 20, 4 );
        REQUIRE( BrushShapes_TryMakeCylinder( &b.b, &e.allocator, e.policy, &e.ids, box, 2u, 12u, brush_circle_mode_t::SCALABLE ) ==
                 geometry_status_t::OK );
        CHECK( BrushSolid_SideCount( &b.b ) == 14u + 2u );
        CHECK( Volume( e, b.b, &box ) > 0.0 );
    }
    SECTION( "Cone" ) {
        Brush b;
        const auto box = Box( 0, 0, 0, 4, 2, 3 );
        REQUIRE( BrushShapes_TryMakeCone( &b.b, &e.allocator, e.policy, &e.ids, box, 2u, 16u, brush_circle_mode_t::VERTEX_ALIGNED ) ==
                 geometry_status_t::OK );
        CHECK( BrushSolid_SideCount( &b.b ) == 17u );
        vec3d_t lo{}, hi{};
        CHECK( Volume( e, b.b, &box, &lo, &hi ) == Approx( 8.0 * 2.0 * std::sin( kPi / 8.0 ) * 3.0 / 3.0 ) );
        CHECK( hi.z == Approx( 3.0 ) );
    }
}

TEST_CASE( "Spheroids fill their box", "[geometry][brushshapes]" ) {
    Env e;
    SECTION( "UV spheroid" ) {
        Brush b;
        const auto box = Box( -1, -1, -1, 1, 1, 1 );
        REQUIRE( BrushShapes_TryMakeUvSphere( &b.b, &e.allocator, e.policy, &e.ids, box, 16u, 8u, brush_circle_mode_t::VERTEX_ALIGNED ) ==
                 geometry_status_t::OK );
        vec3d_t lo{}, hi{};
        const double v = Volume( e, b.b, &box, &lo, &hi );
        CHECK( v < 4.0 / 3.0 * kPi );
        CHECK( v > 0.9 * 4.0 / 3.0 * kPi );
        CHECK( ( lo.x == Approx( -1 ) && lo.y == Approx( -1 ) && lo.z == Approx( -1 ) ) );
        CHECK( ( hi.x == Approx( 1 ) && hi.y == Approx( 1 ) && hi.z == Approx( 1 ) ) );
        // Quads merge into single sides: 16 around x 8 bands.
        CHECK( BrushSolid_SideCount( &b.b ) == 16u * 8u );
    }
    SECTION( "Ellipsoidal UV spheroid" ) {
        Brush b;
        const auto box = Box( 0, 0, 0, 8, 4, 2 );
        REQUIRE( BrushShapes_TryMakeUvSphere( &b.b, &e.allocator, e.policy, &e.ids, box, 12u, 6u, brush_circle_mode_t::VERTEX_ALIGNED ) ==
                 geometry_status_t::OK );
        CHECK( Volume( e, b.b, &box ) < 4.0 / 3.0 * kPi * 4.0 * 2.0 * 1.0 );
    }
    SECTION( "Icosahedral spheroid" ) {
        Brush b;
        const auto box = Box( 0, 0, 0, 2, 4, 6 );
        REQUIRE( BrushShapes_TryMakeIcoSphere( &b.b, &e.allocator, e.policy, &e.ids, box, 1u ) == geometry_status_t::OK );
        const double v = Volume( e, b.b, &box );
        CHECK( v < 4.0 / 3.0 * kPi * 1.0 * 2.0 * 3.0 );
        CHECK( v > 0.8 * 4.0 / 3.0 * kPi * 1.0 * 2.0 * 3.0 );
    }
}

TEST_CASE( "Arches and stairs are sets of valid brushes with exact volumes", "[geometry][brushshapes]" ) {
    Env e;
    SECTION( "Vertex-aligned arch" ) {
        // Tunnel along X, 4 wide (Y), 2 tall, walls 0.5.
        const auto box = Box( 0, -2, 0, 2, 2, 2 );
        brush_solid_t arch[16]{};
        common::u32 n = 0;
        REQUIRE( BrushShapes_TryMakeArch( arch, 16, &n, &e.allocator, e.policy, &e.ids, box, 0u, 16u,
                                          brush_circle_mode_t::VERTEX_ALIGNED, 0.5 ) == geometry_status_t::OK );
        CHECK( n == 8u );
        double v = 0.0;
        for ( common::u32 i = 0; i < n; ++i ) { v += Volume( e, arch[i], &box ); }
        const double outer = 0.5 * 8.0 * 2.0 * 2.0 * std::sin( kPi / 8.0 ), inner = 0.5 * 8.0 * 1.5 * 1.5 * std::sin( kPi / 8.0 );
        CHECK( v == Approx( ( outer - inner ) * 2.0 ) );
        for ( auto &b : arch ) { BrushSolid_Shutdown( &b ); }
    }
    SECTION( "Edge-aligned and scalable arches, with supports" ) {
        for ( brush_circle_mode_t mode : { brush_circle_mode_t::EDGE_ALIGNED, brush_circle_mode_t::SCALABLE } ) {
            CAPTURE( static_cast<int>( mode ) );
            // Taller than half the width: scalable gets straight supports.
            const auto box = Box( -2, 0, 0, 2, 3, 4 );
            brush_solid_t arch[40]{};
            common::u32 n = 0;
            REQUIRE( BrushShapes_TryMakeArch( arch, 40, &n, &e.allocator, e.policy, &e.ids, box, 1u, 12u, mode, 0.5 ) ==
                     geometry_status_t::OK );
            CHECK( n >= 6u );
            vec3d_t lo{}, hi{};
            double minZ = 1e9, maxZ = -1e9;
            for ( common::u32 i = 0; i < n; ++i ) {
                CHECK( Volume( e, arch[i], &box, &lo, &hi ) > 0.0 );
                minZ = std::min( minZ, lo.z );
                maxZ = std::max( maxZ, hi.z );
            }
            CHECK( minZ == Approx( 0.0 ).margin( 1e-12 ) ); // stands on the floor
            CHECK( maxZ == Approx( 4.0 ) );                  // reaches the top
            for ( auto &b : arch ) { BrushSolid_Shutdown( &b ); }
        }
    }
    SECTION( "Stairs in all four directions" ) {
        for ( brush_stairs_direction_t dir : { brush_stairs_direction_t::POS_X, brush_stairs_direction_t::NEG_X,
                                               brush_stairs_direction_t::POS_Y, brush_stairs_direction_t::NEG_Y } ) {
            CAPTURE( static_cast<int>( dir ) );
            const bool bX = dir == brush_stairs_direction_t::POS_X || dir == brush_stairs_direction_t::NEG_X;
            const auto box = bX ? Box( 0, 0, 0, 4, 2, 3 ) : Box( 0, 0, 0, 2, 4, 3 );
            brush_solid_t steps[8]{};
            common::u32 n = 0;
            REQUIRE( BrushShapes_TryMakeStairs( steps, 8, &n, &e.allocator, e.policy, &e.ids, box, dir, 1.0 ) == geometry_status_t::OK );
            REQUIRE( n == 3u );
            double v = 0.0;
            vec3d_t lo{}, hi{};
            for ( common::u32 i = 0; i < n; ++i ) {
                v += Volume( e, steps[i], &box, &lo, &hi );
                CHECK( hi.z == Approx( static_cast<double>( i + 1 ) ) ); // step i reaches i + 1
            }
            CHECK( v == Approx( ( 4.0 / 3.0 ) * 2.0 * ( 1 + 2 + 3 ) ) );
            // The first step is at the start of the climb.
            REQUIRE( Volume( e, steps[0], &box, &lo, &hi ) > 0.0 );
            const double start = dir == brush_stairs_direction_t::POS_X ? lo.x
                                 : dir == brush_stairs_direction_t::NEG_X ? 4.0 - hi.x
                                 : dir == brush_stairs_direction_t::POS_Y ? lo.y
                                                                          : 4.0 - hi.y;
            CHECK( start == Approx( 0.0 ).margin( 1e-12 ) );
            for ( auto &b : steps ) { BrushSolid_Shutdown( &b ); }
        }
    }
    SECTION( "Too few slots: nothing written, allocator untouched" ) {
        const auto box = Box( 0, 0, 0, 4, 2, 3 );
        brush_solid_t steps[2]{};
        common::u32 n = 0;
        const geometry_source_id_allocator_t before = e.ids;
        CHECK( BrushShapes_TryMakeStairs( steps, 2, &n, &e.allocator, e.policy, &e.ids, box, brush_stairs_direction_t::POS_X, 1.0 ) ==
               geometry_status_t::INSUFFICIENT_CAPACITY );
        CHECK( n == 3u );
        CHECK( e.ids.next.value == before.next.value );
        CHECK( ( steps[0].sides.pData == nullptr && steps[1].sides.pData == nullptr ) );
    }
}

TEST_CASE( "Shape failures leave the destination and allocator unchanged", "[geometry][brushshapes]" ) {
    Env e;
    const geometry_source_id_allocator_t before = e.ids;
    Brush b;
    CHECK( BrushShapes_TryMakeCylinder( &b.b, &e.allocator, e.policy, &e.ids, Box( 0, 0, 0, 1, 1, 1 ), 3u, 8u,
                                        brush_circle_mode_t::VERTEX_ALIGNED ) == geometry_status_t::INVALID_ARGUMENT );
    CHECK( BrushShapes_TryMakeCylinder( &b.b, &e.allocator, e.policy, &e.ids, Box( 0, 0, 0, 0, 1, 1 ), 2u, 8u,
                                        brush_circle_mode_t::VERTEX_ALIGNED ) == geometry_status_t::INVALID_ARGUMENT );
    CHECK( BrushShapes_TryMakeCylinder( &b.b, &e.allocator, e.policy, &e.ids, Box( 0, 0, 0, 1, 1, 1 ), 2u, 300u,
                                        brush_circle_mode_t::VERTEX_ALIGNED ) != geometry_status_t::OK );
    CHECK( BrushShapes_TryMakeArch( &b.b, 1, nullptr, &e.allocator, e.policy, &e.ids, Box( 0, 0, 0, 1, 1, 1 ), 0u, 8u,
                                    brush_circle_mode_t::VERTEX_ALIGNED, 0.9 ) != geometry_status_t::OK );
    CHECK( e.ids.next.value == before.next.value );
    CHECK( b.b.sides.pData == nullptr );
    // A generated shape becomes an authored brush with its own surfaces.
    REQUIRE( BrushShapes_TryMakeCylinder( &b.b, &e.allocator, e.policy, &e.ids, Box( 0, 0, 0, 2, 2, 2 ), 2u, 8u,
                                          brush_circle_mode_t::EDGE_ALIGNED ) == geometry_status_t::OK );
    CHECK( BrushShapes_TryMakeCuboid( &b.b, &e.allocator, e.policy, &e.ids, Box( 0, 0, 0, 1, 1, 1 ) ) ==
           geometry_status_t::ALREADY_INITIALIZED );
    brush_source_t src{};
    REQUIRE( BrushSource_TryBuildDefault( &b.b, &e.allocator, e.policy, &src ) == geometry_status_t::OK );
    CHECK( BrushSource_Validate( &src, e.policy ).status == geometry_status_t::OK );
    BrushSource_Shutdown( &src );
}

} // namespace cypher::editor::geometry
