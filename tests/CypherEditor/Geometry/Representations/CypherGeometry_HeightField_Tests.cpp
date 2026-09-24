//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_HeightField_Tests.cpp
//  Purpose: Contract tests for the HeightField representation, bounded
//           edits with dirty tracking, queries, and tile tessellation.
//  Details: Oracles: a linear height function z = a x + b y + c is
//           reproduced exactly by either cell diagonal, so HeightAt and
//           ray casts have closed-form answers; a vertical ray must agree
//           with HeightAt everywhere (same triangulation rule); stitched
//           fine-tile edge vertices must lie on the coarse neighbour's
//           edge segments.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_HeightField.h"
#include "CypherGeometry_HeightFieldTessellation.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace cypher::editor::geometry {

using Catch::Approx;
using math::vec3d_t;
using math::Vec3d_Make;

namespace {

geometry_source_id_t Id( common::u64 v ) { return geometry_source_id_t{ v }; }

struct Lcg {
    common::u64 state;
    double Next() {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<double>( state >> 11 ) * ( 1.0 / 9007199254740992.0 );
    }
    double Range( double a, double b ) { return a + ( b - a ) * Next(); }
};

struct Field {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_source_id_allocator_t ids{ geometry_source_id_t{ 500u } };
    heightfield_t f{};
    // 16 x 8 cells of size 2 in 4-cell tiles: 4 x 2 tiles.
    explicit Field( common::u32 cx = 16, common::u32 cy = 8, common::u32 tile = 4 ) {
        REQUIRE( HeightField_TryInit( &f, &allocator, Vec3d_Make( -10.0, 5.0, 1.0 ), 2.0, cx, cy, tile, Id( 1 ),
                                      &ids ) == geometry_status_t::OK );
    }
    ~Field() { HeightField_Shutdown( &f ); }

    void SetAll( double ( *fn )( double, double ) ) {
        const common::u32 sx = HeightField_SamplesX( &f ), sy = HeightField_SamplesY( &f );
        std::vector<double> v( static_cast<size_t>( sx ) * sy );
        for ( common::u32 j = 0; j < sy; ++j ) {
            for ( common::u32 i = 0; i < sx; ++i ) {
                const vec3d_t p = HeightField_SamplePosition( &f, i, j );
                v[static_cast<size_t>( j ) * sx + i] = fn( p.x, p.y );
            }
        }
        REQUIRE( HeightField_TryWriteHeights( &f, heightfield_rect_t{ 0, 0, sx, sy },
                                              common::span_t<const double>{ v.data(), v.size() } ) ==
                 geometry_status_t::OK );
    }
    void Bumpy( common::u64 seed ) {
        const common::u32 sx = HeightField_SamplesX( &f ), sy = HeightField_SamplesY( &f );
        std::vector<double> v( static_cast<size_t>( sx ) * sy );
        Lcg rng{ seed };
        for ( double &h : v ) { h = rng.Range( -3.0, 3.0 ); }
        REQUIRE( HeightField_TryWriteHeights( &f, heightfield_rect_t{ 0, 0, sx, sy },
                                              common::span_t<const double>{ v.data(), v.size() } ) ==
                 geometry_status_t::OK );
    }
    std::vector<common::u32> Dirty() {
        common::vector_t<common::u32> out{};
        REQUIRE( common::Vector_Init( &out, &allocator ) );
        REQUIRE( HeightField_TryCollectDirtyTiles( &f, &out ) == geometry_status_t::OK );
        std::vector<common::u32> r( out.pData, out.pData + out.nCount );
        common::Vector_Shutdown( &out );
        return r;
    }
    void ClearAllDirty() {
        for ( common::u32 i = 0; i < f.tiles.nCount; ++i ) {
            (void)HeightField_ClearDirty( &f, i, f.tiles.pData[i].revision );
        }
    }
};

double Linear( double x, double y ) { return 0.25 * x - 0.5 * y + 3.0; }

struct TileMesh {
    heightfield_tile_mesh_t m{};
    explicit TileMesh( const common::allocator_t *a ) {
        REQUIRE( HeightFieldTileMesh_Init( &m, a ) == geometry_status_t::OK );
    }
    ~TileMesh() { HeightFieldTileMesh_Shutdown( &m ); }
};

} // namespace

TEST_CASE( "HeightField init validates layout and allocates tile identity", "[geometry][heightfield]" ) {
    common::allocator_t a{ *common::Allocator_GetSystem() };
    geometry_source_id_allocator_t ids{ geometry_source_id_t{ 10u } };
    heightfield_t f{};
    const vec3d_t o = Vec3d_Make( 0, 0, 0 );
    CHECK( HeightField_TryInit( &f, &a, o, 1.0, 12, 8, 3, Id( 1 ), &ids ) == geometry_status_t::INVALID_ARGUMENT );
    CHECK( HeightField_TryInit( &f, &a, o, 1.0, 12, 8, 8, Id( 1 ), &ids ) == geometry_status_t::INVALID_ARGUMENT );
    CHECK( HeightField_TryInit( &f, &a, o, 1.0, 0, 8, 4, Id( 1 ), &ids ) == geometry_status_t::INVALID_ARGUMENT );
    CHECK( HeightField_TryInit( &f, &a, o, 0.0, 8, 8, 4, Id( 1 ), &ids ) == geometry_status_t::NUMERIC_FAILURE );
    CHECK( HeightField_TryInit( &f, &a, o, 1e6, 8, 8, 4, Id( 1 ), &ids ) == geometry_status_t::NUMERIC_FAILURE );
    CHECK( HeightField_TryInit( &f, &a, o, 1.0, 8, 8, 4, GEOMETRY_SOURCE_ID_INVALID, &ids ) ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK( ids.next.value == 10u ); // failures consume no IDs
    REQUIRE( HeightField_TryInit( &f, &a, o, 1.0, 12, 8, 4, Id( 1 ), &ids ) == geometry_status_t::OK );
    CHECK( HeightField_TryInit( &f, &a, o, 1.0, 12, 8, 4, Id( 1 ), &ids ) == geometry_status_t::ALREADY_INITIALIZED );
    CHECK( f.cTilesX == 3u );
    CHECK( f.cTilesY == 2u );
    CHECK( ids.next.value == 16u );
    CHECK( f.tiles.pData[0].sourceId.value == 10u );
    CHECK( f.tiles.pData[5].sourceId.value == 15u );
    CHECK( HeightField_SamplesX( &f ) == 13u );
    CHECK( HeightField_SamplesY( &f ) == 9u );
    CHECK( HeightField_Validate( &f, &a ).fault == heightfield_fault_t::NONE );
    for ( common::u32 i = 0; i < 6; ++i ) { CHECK( f.tiles.pData[i].bDirty ); }
    CHECK( HeightField_TileOfCell( &f, 4, 3 ) == 1u );
    CHECK( HeightField_TileOfCell( &f, 11, 7 ) == 5u );
    CHECK( HeightField_TileOfCell( &f, 12, 0 ) == CY_INVALID_INDEX );
    HeightField_Shutdown( &f );
    CHECK_FALSE( HeightField_IsInitialized( &f ) );
}

TEST_CASE( "HeightField init keeps field and tile source identities disjoint",
           "[geometry][heightfield]" ) {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_source_id_allocator_t ids{ Id( 1u ) };
    heightfield_t field{};

    REQUIRE( HeightField_TryInit(
                 &field,
                 &allocator,
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 1.0,
                 4u,
                 4u,
                 2u,
                 Id( 1u ),
                 &ids ) == geometry_status_t::OK );
    REQUIRE( field.tiles.nCount == 4u );
    CHECK( field.tiles.pData[0].sourceId.value == 2u );
    CHECK( field.tiles.pData[3].sourceId.value == 5u );
    CHECK( ids.next.value == 6u );
    CHECK( HeightField_Validate( &field, &allocator ).fault ==
           heightfield_fault_t::NONE );

    HeightField_Shutdown( &field );
}

TEST_CASE( "HeightField region write and read round-trip and dirty exactly the touched tiles",
           "[geometry][heightfield]" ) {
    Field t;
    t.ClearAllDirty();
    CHECK( t.Dirty().empty() );

    // Interior sample of tile 0 -> only tile 0.
    const double one = 4.5;
    REQUIRE( HeightField_TryWriteHeights( &t.f, heightfield_rect_t{ 2, 2, 1, 1 },
                                          common::span_t<const double>{ &one, 1 } ) == geometry_status_t::OK );
    CHECK( t.Dirty() == std::vector<common::u32>{ 0u } );
    CHECK( HeightField_Height( &t.f, 2, 2 ) == 4.5 );
    CHECK( HeightField_SamplePosition( &t.f, 2, 2 ).z == 5.5 );
    t.ClearAllDirty();

    // Sample on the corner shared by tiles 0, 1, 4, 5 -> all four.
    REQUIRE( HeightField_TryWriteHeights( &t.f, heightfield_rect_t{ 4, 4, 1, 1 },
                                          common::span_t<const double>{ &one, 1 } ) == geometry_status_t::OK );
    CHECK( t.Dirty() == std::vector<common::u32>{ 0u, 1u, 4u, 5u } );

    // Stale revision does not clear: a second edit landed after "cooking".
    const common::u64 seen = t.f.tiles.pData[1].revision;
    REQUIRE( HeightField_TryWriteHeights( &t.f, heightfield_rect_t{ 5, 1, 1, 1 },
                                          common::span_t<const double>{ &one, 1 } ) == geometry_status_t::OK );
    CHECK_FALSE( HeightField_ClearDirty( &t.f, 1, seen ) );
    CHECK( t.f.tiles.pData[1].bDirty );
    CHECK( HeightField_ClearDirty( &t.f, 1, t.f.tiles.pData[1].revision ) );
    CHECK_FALSE( t.f.tiles.pData[1].bDirty );

    std::vector<double> region( 6 );
    REQUIRE( HeightField_TryReadHeights( &t.f, heightfield_rect_t{ 3, 3, 3, 2 },
                                         common::span_t<double>{ region.data(), region.size() } ) ==
             geometry_status_t::OK );
    CHECK( region[4] == 4.5 ); // (4, 4)
    CHECK( region[0] == 0.0 );

    // Rejections leave everything untouched.
    const common::u64 rev = t.f.revision;
    const double bad = std::nan( "" );
    CHECK( HeightField_TryWriteHeights( &t.f, heightfield_rect_t{ 0, 0, 1, 1 },
                                        common::span_t<const double>{ &bad, 1 } ) == geometry_status_t::NUMERIC_FAILURE );
    const double far = kHeightFieldCoordinateMax;
    CHECK( HeightField_TryWriteHeights( &t.f, heightfield_rect_t{ 0, 0, 1, 1 },
                                        common::span_t<const double>{ &far, 1 } ) == geometry_status_t::NUMERIC_FAILURE );
    CHECK( HeightField_TryWriteHeights( &t.f, heightfield_rect_t{ 16, 0, 2, 1 },
                                        common::span_t<const double>{ region.data(), 2 } ) ==
           geometry_status_t::INVALID_HANDLE );
    CHECK( HeightField_TryWriteHeights( &t.f, heightfield_rect_t{ 0, 0, 2, 2 },
                                        common::span_t<const double>{ region.data(), 3 } ) ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK( t.f.revision == rev );
}

TEST_CASE( "HeightAt reproduces a linear field exactly and respects holes", "[geometry][heightfield]" ) {
    Field t;
    t.SetAll( Linear );
    Lcg rng{ 3u };
    for ( int k = 0; k < 200; ++k ) {
        const double x = rng.Range( -10.0, 22.0 ), y = rng.Range( 5.0, 21.0 );
        double z = 0.0;
        REQUIRE( HeightField_TryHeightAt( &t.f, x, y, &z ) );
        CHECK( z == Approx( 1.0 + Linear( x, y ) ).margin( 1e-9 ) );
    }
    double z = 0.0;
    CHECK( HeightField_TryHeightAt( &t.f, 22.0, 21.0, &z ) ); // far corner is inside
    CHECK_FALSE( HeightField_TryHeightAt( &t.f, -10.5, 6.0, &z ) );
    CHECK_FALSE( HeightField_TryHeightAt( &t.f, 0.0, 21.5, &z ) );
    CHECK_FALSE( HeightField_TryHeightAt( &t.f, std::nan( "" ), 6.0, &z ) );

    // Cell (3, 2) spans x in [-4, -2], y in [9, 11].
    const common::u64 rev = t.f.revision;
    REQUIRE( HeightField_TrySetHole( &t.f, 3, 2, true ) == geometry_status_t::OK );
    CHECK( t.f.revision == rev + 1u );
    CHECK( HeightField_TrySetHole( &t.f, 3, 2, true ) == geometry_status_t::OK ); // no-op
    CHECK( t.f.revision == rev + 1u );
    CHECK( HeightField_IsHole( &t.f, 3, 2 ) );
    CHECK_FALSE( HeightField_TryHeightAt( &t.f, -3.0, 10.0, &z ) );
    CHECK( HeightField_TryHeightAt( &t.f, -1.0, 10.0, &z ) );
    CHECK( HeightField_TrySetHole( &t.f, 16, 0, true ) == geometry_status_t::INVALID_HANDLE );
}

TEST_CASE( "Vertical ray casts agree with HeightAt on a rough field", "[geometry][heightfield]" ) {
    Field t;
    t.Bumpy( 17u );
    Lcg rng{ 5u };
    for ( int k = 0; k < 300; ++k ) {
        const double x = rng.Range( -9.9, 21.9 ), y = rng.Range( 5.1, 20.9 );
        double z = 0.0;
        REQUIRE( HeightField_TryHeightAt( &t.f, x, y, &z ) );
        heightfield_ray_hit_t hit{};
        REQUIRE( HeightField_TryRaycast( &t.f, Vec3d_Make( x, y, 100.0 ), Vec3d_Make( 0, 0, -1 ), 1000.0, &hit ) );
        CHECK( hit.position.z == Approx( z ).margin( 1e-9 ) );
        CHECK( hit.normal.z > 0.0 );
        CHECK( hit.iTile == HeightField_TileOfCell( &t.f, hit.cx, hit.cy ) );
    }
}

TEST_CASE( "Oblique ray casts hit the analytic plane, skip holes, and stop at maxT", "[geometry][heightfield]" ) {
    Field t;
    t.SetAll( Linear );
    // Ray from above the field travelling in +x and down.
    const vec3d_t o = Vec3d_Make( -9.0, 12.3, 20.0 );
    const vec3d_t d = Vec3d_Make( 1.0, 0.1, -1.0 );
    heightfield_ray_hit_t hit{};
    REQUIRE( HeightField_TryRaycast( &t.f, o, d, 1000.0, &hit ) );
    // Solve o.z + t d.z = 1 + 0.25 (o.x + t d.x) - 0.5 (o.y + t d.y) + 3.
    const double tExpected = ( 1.0 + 0.25 * o.x - 0.5 * o.y + 3.0 - o.z ) / ( d.z - 0.25 * d.x + 0.5 * d.y );
    CHECK( hit.t == Approx( tExpected ).epsilon( 1e-12 ) );
    CHECK( hit.normal.x < 0.0 ); // plane rises with x, so the normal leans -x

    CHECK_FALSE( HeightField_TryRaycast( &t.f, o, d, tExpected * 0.5, &hit ) );
    // Ray that never enters the field rectangle.
    CHECK_FALSE( HeightField_TryRaycast( &t.f, Vec3d_Make( -30, 0, 5 ), Vec3d_Make( 0, -1, 0 ), 100.0, &hit ) );
    // Ray from below travelling up still reports the crossing.
    REQUIRE( HeightField_TryRaycast( &t.f, Vec3d_Make( 0.5, 10.5, -50.0 ), Vec3d_Make( 0, 0, 1 ), 100.0, &hit ) );

    // A hole under a vertical ray lets it fall through.
    double z = 0.0;
    REQUIRE( HeightField_TryHeightAt( &t.f, -3.0, 10.0, &z ) );
    REQUIRE( HeightField_TrySetHole( &t.f, 3, 2, true ) == geometry_status_t::OK );
    CHECK_FALSE( HeightField_TryRaycast( &t.f, Vec3d_Make( -3.0, 10.0, 50.0 ), Vec3d_Make( 0, 0, -1 ), 100.0, &hit ) );
    CHECK_FALSE( HeightField_TryRaycast( &t.f, o, Vec3d_Make( 0, 0, 0 ), 100.0, &hit ) );
}

TEST_CASE( "HeightField brushes raise, flatten, and smooth within their radius", "[geometry][heightfield]" ) {
    Field t;
    t.ClearAllDirty();
    // Sample (8, 4) sits at world (6, 13).
    heightfield_brush_t b{};
    b.op = heightfield_brush_op_t::RAISE;
    b.centerX = 6.0;
    b.centerY = 13.0;
    b.radius = 5.0;
    b.amount = 2.0;
    heightfield_rect_t touched{};
    REQUIRE( HeightField_TryApplyBrush( &t.f, b, &touched ) == geometry_status_t::OK );
    CHECK( HeightField_Height( &t.f, 8, 4 ) == 2.0 );
    CHECK( HeightField_Height( &t.f, 9, 4 ) > 0.0 );
    CHECK( HeightField_Height( &t.f, 9, 4 ) < 2.0 );
    CHECK( HeightField_Height( &t.f, 11, 4 ) == 0.0 ); // 6 units away: outside
    CHECK( touched.x0 == 6u );
    CHECK( touched.cx == 5u );
    CHECK( HeightField_Height( &t.f, 7, 4 ) == HeightField_Height( &t.f, 9, 4 ) ); // radial symmetry
    CHECK( t.Dirty() == std::vector<common::u32>{ 1u, 2u, 5u, 6u } );

    // Flatten at full strength pulls the centre exactly to the target.
    b.op = heightfield_brush_op_t::FLATTEN;
    b.targetHeight = -1.0;
    b.strength = 1.0;
    REQUIRE( HeightField_TryApplyBrush( &t.f, b, nullptr ) == geometry_status_t::OK );
    CHECK( HeightField_Height( &t.f, 8, 4 ) == -1.0 );

    // Smooth a single spike: the centre moves to its neighbours' mean and
    // the result is symmetric (Jacobi, order independent).
    Field s;
    const double spike = 8.0;
    REQUIRE( HeightField_TryWriteHeights( &s.f, heightfield_rect_t{ 8, 4, 1, 1 },
                                          common::span_t<const double>{ &spike, 1 } ) == geometry_status_t::OK );
    b.op = heightfield_brush_op_t::SMOOTH;
    b.strength = 1.0;
    b.radius = 3.0;
    REQUIRE( HeightField_TryApplyBrush( &s.f, b, nullptr ) == geometry_status_t::OK );
    CHECK( HeightField_Height( &s.f, 8, 4 ) == 0.0 );
    const double e = HeightField_Height( &s.f, 9, 4 );
    CHECK( e > 0.0 );
    CHECK( HeightField_Height( &s.f, 7, 4 ) == e );
    CHECK( HeightField_Height( &s.f, 8, 5 ) == e );
    CHECK( HeightField_Height( &s.f, 8, 3 ) == e );

    // Failure-atomic: a raise that would leave the coordinate range changes
    // nothing, not even the revision.
    const common::u64 rev = s.f.revision;
    b.op = heightfield_brush_op_t::RAISE;
    b.amount = kHeightFieldCoordinateMax * 2.0;
    CHECK( HeightField_TryApplyBrush( &s.f, b, &touched ) == geometry_status_t::NUMERIC_FAILURE );
    CHECK( s.f.revision == rev );
    CHECK( HeightField_Height( &s.f, 9, 4 ) == e );
    CHECK( touched.cx == 0u );

    // Outside the field: nothing to do.
    b.amount = 1.0;
    b.centerX = 500.0;
    CHECK( HeightField_TryApplyBrush( &s.f, b, &touched ) == geometry_status_t::OK );
    CHECK( s.f.revision == rev );
    b.radius = 0.0;
    CHECK( HeightField_TryApplyBrush( &s.f, b, nullptr ) == geometry_status_t::INVALID_ARGUMENT );
    b.radius = 1.0;
    b.strength = 1.5;
    CHECK( HeightField_TryApplyBrush( &s.f, b, nullptr ) == geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "HeightField validation reports faults", "[geometry][heightfield]" ) {
    Field t;
    CHECK( HeightField_Validate( &t.f, &t.allocator ).fault == heightfield_fault_t::NONE );
    t.f.tiles.pData[6].sourceId = t.f.tiles.pData[2].sourceId;
    heightfield_validation_result_t r = HeightField_Validate( &t.f, &t.allocator );
    CHECK( r.fault == heightfield_fault_t::DUPLICATE_SOURCE_ID );
    CHECK( r.index == 6u );
    t.f.tiles.pData[6].sourceId = Id( 9999 );
    t.f.heights.pData[20] = std::nan( "" );
    r = HeightField_Validate( &t.f, &t.allocator );
    CHECK( r.fault == heightfield_fault_t::NON_FINITE );
    CHECK( r.index == 20u );
    t.f.heights.pData[20] = kHeightFieldCoordinateMax;
    CHECK( HeightField_Validate( &t.f, &t.allocator ).fault == heightfield_fault_t::COORDINATE_RANGE );
    t.f.heights.pData[20] = 0.0;
    t.f.holes.pData[3] = 2u;
    CHECK( HeightField_Validate( &t.f, &t.allocator ).fault == heightfield_fault_t::INVALID_HOLE_VALUE );
    t.f.holes.pData[3] = 0u;
    t.f.tileCells = 8u;
    CHECK( HeightField_Validate( &t.f, &t.allocator ).fault == heightfield_fault_t::INVALID_DIMENSIONS );
    t.f.tileCells = 4u;
    CHECK( HeightField_Validate( &t.f, &t.allocator ).fault == heightfield_fault_t::NONE );

    t.f.tiles.pData[3].sourceId = t.f.sourceId;
    r = HeightField_Validate( &t.f, &t.allocator );
    CHECK( r.fault == heightfield_fault_t::DUPLICATE_SOURCE_ID );
    CHECK( r.index == 3u );
}

TEST_CASE( "HeightField public operations reject corrupt layout and neutralize query outputs",
           "[geometry][heightfield]" ) {
    Field t;

    double z = 123.0;
    CHECK_FALSE( HeightField_TryHeightAt( &t.f, -1000.0, -1000.0, &z ) );
    CHECK( z == 0.0 );

    heightfield_ray_hit_t hit{};
    hit.t = 99.0;
    hit.iTile = 3u;
    CHECK_FALSE( HeightField_TryRaycast(
        &t.f,
        Vec3d_Make( -1000.0, -1000.0, 10.0 ),
        Vec3d_Make( 0.0, 0.0, -1.0 ),
        100.0,
        &hit ) );
    CHECK( hit.t == 0.0 );
    CHECK( hit.iTile == CY_INVALID_INDEX );

    const common::u32 savedCellsX = t.f.cCellsX;
    t.f.cCellsX = savedCellsX + 1u;
    CHECK( HeightField_ValidateStructure( &t.f ) ==
           geometry_status_t::CORRUPT_STATE );
    CHECK( HeightField_Height( &t.f, 0u, 0u ) == 0.0 );
    CHECK( math::Vec3d_EqualsExact(
        HeightField_SamplePosition( &t.f, 0u, 0u ), vec3d_t{} ) );
    z = 123.0;
    CHECK_FALSE( HeightField_TryHeightAt( &t.f, 0.0, 0.0, &z ) );
    CHECK( z == 0.0 );
    CHECK_FALSE( HeightField_ClearDirty( &t.f, 0u, 0u ) );
    t.f.cCellsX = savedCellsX;
    CHECK( HeightField_ValidateStructure( &t.f ) == geometry_status_t::OK );
}

TEST_CASE( "HeightField edits are atomic when the revision counter is exhausted",
           "[geometry][heightfield]" ) {
    Field t;
    t.ClearAllDirty();
    t.f.revision = common::CY_U64_MAX;
    const double oldHeight = HeightField_Height( &t.f, 2u, 2u );
    const double replacement = oldHeight + 1.0;

    CHECK( HeightField_TryWriteHeights(
               &t.f,
               heightfield_rect_t{ 2u, 2u, 1u, 1u },
               common::span_t<const double>{ &replacement, 1u } ) ==
           geometry_status_t::INSUFFICIENT_CAPACITY );
    CHECK( HeightField_Height( &t.f, 2u, 2u ) == oldHeight );
    CHECK_FALSE( t.f.tiles.pData[0].bDirty );

    CHECK( HeightField_TrySetHole( &t.f, 0u, 0u, true ) ==
           geometry_status_t::INSUFFICIENT_CAPACITY );
    CHECK_FALSE( HeightField_IsHole( &t.f, 0u, 0u ) );
    CHECK( t.f.revision == common::CY_U64_MAX );
}

// ---------------------------------------------------------------------------
// Tile tessellation
// ---------------------------------------------------------------------------

TEST_CASE( "LOD 0 tile tessellation matches samples, winding, and source mapping", "[geometry][heightfield]" ) {
    Field t;
    t.Bumpy( 9u );
    REQUIRE( HeightField_TrySetHole( &t.f, 5, 1, true ) == geometry_status_t::OK ); // tile 1
    TileMesh m( &t.allocator );
    REQUIRE( HeightFieldTessellation_TryBuildTile( &t.f, 1, heightfield_tile_lods_t{}, &m.m ) ==
             geometry_status_t::OK );
    CHECK( m.m.positions.nCount == 25u );
    CHECK( m.m.indices.nCount == ( 16u - 1u ) * 2u * 3u );
    CHECK( m.m.cHoleQuadsSkipped == 1u );
    CHECK( m.m.tileId.value == t.f.tiles.pData[1].sourceId.value );
    CHECK( m.m.revision == t.f.tiles.pData[1].revision );
    for ( common::usize v = 0; v < m.m.positions.nCount; ++v ) {
        const common::u32 s = m.m.vertexSample.pData[v];
        const common::u32 sx = s % 17u, sy = s / 17u;
        CHECK( sx >= 4u );
        CHECK( sx <= 8u );
        CHECK( math::Vec3d_EqualsExact( m.m.positions.pData[v], HeightField_SamplePosition( &t.f, sx, sy ) ) );
        CHECK( m.m.vertexSnapped.pData[v] == 0u );
    }
    const common::usize cTris = m.m.indices.nCount / 3u;
    for ( common::usize i = 0; i < cTris; ++i ) {
        const vec3d_t a = m.m.positions.pData[m.m.indices.pData[i * 3]];
        const vec3d_t b = m.m.positions.pData[m.m.indices.pData[i * 3 + 1]];
        const vec3d_t c = m.m.positions.pData[m.m.indices.pData[i * 3 + 2]];
        const vec3d_t n = math::Vec3d_Cross( math::Vec3d_Subtract( b, a ), math::Vec3d_Subtract( c, a ) );
        CHECK( n.z > 0.0 );
        const common::u32 cell = m.m.triangleCell.pData[i];
        CHECK( HeightField_TileOfCell( &t.f, cell % 16u, cell / 16u ) == 1u );
        CHECK( cell != 1u * 16u + 5u ); // never the hole
        // The triangle centroid lies on the same surface HeightAt reports.
        const vec3d_t g = math::Vec3d_Scale( math::Vec3d_Add( math::Vec3d_Add( a, b ), c ), 1.0 / 3.0 );
        double z = 0.0;
        REQUIRE( HeightField_TryHeightAt( &t.f, g.x, g.y, &z ) );
        CHECK( z == Approx( g.z ).margin( 1e-9 ) );
    }
}

TEST_CASE( "Neighbouring LOD 0 tiles share bit-identical border vertices and normals", "[geometry][heightfield]" ) {
    Field t;
    t.Bumpy( 23u );
    TileMesh a( &t.allocator ), b( &t.allocator );
    REQUIRE( HeightFieldTessellation_TryBuildTile( &t.f, 0, heightfield_tile_lods_t{}, &a.m ) ==
             geometry_status_t::OK );
    REQUIRE( HeightFieldTessellation_TryBuildTile( &t.f, 1, heightfield_tile_lods_t{}, &b.m ) ==
             geometry_status_t::OK );
    for ( common::u32 j = 0; j < 5; ++j ) {
        const common::u32 ia = j * 5u + 4u, ib = j * 5u; // east column of 0, west column of 1
        CHECK( a.m.vertexSample.pData[ia] == b.m.vertexSample.pData[ib] );
        CHECK( math::Vec3d_EqualsExact( a.m.positions.pData[ia], b.m.positions.pData[ib] ) );
        CHECK( math::Vec3d_EqualsExact( a.m.normals.pData[ia], b.m.normals.pData[ib] ) );
    }
}

TEST_CASE( "Fine tiles stitch onto coarser neighbours without gaps", "[geometry][heightfield]" ) {
    Field t( 16, 16, 8 ); // 2 x 2 tiles of 8 cells; MaxLod = 3
    t.Bumpy( 31u );
    CHECK( HeightFieldTessellation_MaxLod( &t.f ) == 3u );
    // Tile 0 at LOD 0; its east neighbour (tile 1) at LOD 2, north (tile 2)
    // at LOD 1.
    heightfield_tile_lods_t fine{};
    fine.lod = 0u;
    fine.neighbourLod[HEIGHTFIELD_EDGE_EAST] = 2u;
    fine.neighbourLod[HEIGHTFIELD_EDGE_NORTH] = 1u;
    heightfield_tile_lods_t coarseE{};
    coarseE.lod = 2u;
    coarseE.neighbourLod[HEIGHTFIELD_EDGE_WEST] = 0u; // finer: ignored
    TileMesh m0( &t.allocator ), m1( &t.allocator );
    REQUIRE( HeightFieldTessellation_TryBuildTile( &t.f, 0, fine, &m0.m ) == geometry_status_t::OK );
    REQUIRE( HeightFieldTessellation_TryBuildTile( &t.f, 1, coarseE, &m1.m ) == geometry_status_t::OK );
    CHECK( m1.m.positions.nCount == 9u ); // (8 / 4 + 1)^2
    CHECK( m1.m.indices.nCount == 4u * 2u * 3u );

    // East edge of tile 0 (x sample 8): vertices at y = 0..8. Tile 1's west
    // edge has vertices at y = 0, 4, 8. Every fine vertex must lie on the
    // coarse segment covering it.
    common::u32 cSnapped = 0;
    for ( common::u32 j = 0; j <= 8; ++j ) {
        const common::u32 v = j * 9u + 8u;
        const vec3d_t p = m0.m.positions.pData[v];
        const common::u32 seg = std::min( j / 4u, 1u );
        // Tile 1 is a 3 x 3 grid; its west column is indices 0, 3, 6.
        const vec3d_t lo = m1.m.positions.pData[seg * 3u];
        const vec3d_t hi = m1.m.positions.pData[( seg + 1u ) * 3u];
        const double f = ( p.y - lo.y ) / ( hi.y - lo.y );
        CHECK( p.x == lo.x );
        CHECK( p.z == Approx( lo.z + ( hi.z - lo.z ) * f ).margin( 1e-12 ) );
        if ( j % 4u == 0u ) {
            CHECK( m0.m.vertexSnapped.pData[v] == 0u );
            CHECK( math::Vec3d_EqualsExact( p, m1.m.positions.pData[( j / 4u ) * 3u] ) );
        } else {
            CHECK( m0.m.vertexSnapped.pData[v] == 1u );
            ++cSnapped;
        }
    }
    CHECK( cSnapped == 6u );
    // North edge snaps to step 2: odd x samples.
    common::u32 cNorth = 0;
    for ( common::u32 i = 0; i <= 8; ++i ) { cNorth += m0.m.vertexSnapped.pData[8u * 9u + i]; }
    CHECK( cNorth == 4u );
    // West and south are field borders: untouched.
    for ( common::u32 k = 1; k < 8; ++k ) {
        CHECK( m0.m.vertexSnapped.pData[k * 9u] == 0u );
        CHECK( m0.m.vertexSnapped.pData[k] == 0u );
    }
}

TEST_CASE( "Coarse LOD omits quads covering holes and validates arguments", "[geometry][heightfield]" ) {
    Field t( 16, 16, 8 );
    REQUIRE( HeightField_TrySetHole( &t.f, 1, 1, true ) == geometry_status_t::OK );
    TileMesh m( &t.allocator );
    heightfield_tile_lods_t l{};
    l.lod = 1u;
    REQUIRE( HeightFieldTessellation_TryBuildTile( &t.f, 0, l, &m.m ) == geometry_status_t::OK );
    CHECK( m.m.cHoleQuadsSkipped == 1u );
    CHECK( m.m.indices.nCount == ( 16u - 1u ) * 6u );
    l.lod = 4u;
    CHECK( HeightFieldTessellation_TryBuildTile( &t.f, 0, l, &m.m ) == geometry_status_t::INVALID_ARGUMENT );
    CHECK( m.m.positions.nCount == 0u );
    l.lod = 0u;
    l.neighbourLod[HEIGHTFIELD_EDGE_NORTH] = 9u;
    CHECK( HeightFieldTessellation_TryBuildTile( &t.f, 0, l, &m.m ) == geometry_status_t::INVALID_ARGUMENT );
    l.neighbourLod[HEIGHTFIELD_EDGE_NORTH] = 0u;
    CHECK( HeightFieldTessellation_TryBuildTile( &t.f, 4, l, &m.m ) == geometry_status_t::INVALID_HANDLE );
    heightfield_tile_mesh_t never{};
    CHECK( HeightFieldTessellation_TryBuildTile( &t.f, 0, l, &never ) == geometry_status_t::NOT_INITIALIZED );

    const common::usize savedNormalCount = m.m.normals.nCount;
    m.m.normals.nCount = savedNormalCount + 1u;
    CHECK( HeightFieldTessellation_TryBuildTile( &t.f, 0u, heightfield_tile_lods_t{}, &m.m ) ==
           geometry_status_t::CORRUPT_STATE );
    m.m.normals.nCount = savedNormalCount;
}

TEST_CASE( "Dirty-tile cook loop rebuilds only what an edit touched", "[geometry][heightfield]" ) {
    // The HeightField acceptance gate: small tiled field, one hole, a
    // bounded edit, and only the dirty tiles re-tessellated.
    Field t;
    REQUIRE( HeightField_TrySetHole( &t.f, 13, 6, true ) == geometry_status_t::OK );
    TileMesh m( &t.allocator );
    auto cook = [&]() {
        std::vector<common::u32> built;
        for ( common::u32 iTile : t.Dirty() ) {
            REQUIRE( HeightFieldTessellation_TryBuildTile( &t.f, iTile, heightfield_tile_lods_t{}, &m.m ) ==
                     geometry_status_t::OK );
            CHECK( HeightField_ClearDirty( &t.f, iTile, m.m.revision ) );
            built.push_back( iTile );
        }
        return built;
    };
    CHECK( cook().size() == 8u ); // initial build: everything
    CHECK( cook().empty() );

    heightfield_brush_t b{};
    b.centerX = 16.0; // sample (13, 2)
    b.centerY = 9.0;
    b.radius = 1.5;
    b.amount = 1.0;
    REQUIRE( HeightField_TryApplyBrush( &t.f, b, nullptr ) == geometry_status_t::OK );
    CHECK( cook() == std::vector<common::u32>{ 3u } );
    CHECK( cook().empty() );
}

} // namespace cypher::editor::geometry
