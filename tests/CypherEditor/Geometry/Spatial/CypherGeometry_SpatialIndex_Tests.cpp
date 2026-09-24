//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_SpatialIndex_Tests.cpp
//  Purpose: Verifies flat AABB broad-phase spatial index operations.
//  Details: Covers Gate 4 spatial acceptance: insert/remove/refit,
//           overlap and point queries, identity conflict, and brute-force
//           reference validation (query results match manual iteration).
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_SpatialIndex.h"

#include "CypherMath.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <limits>

namespace cypher::editor::geometry {

using cypher::math::Vec3d_Make;
using cypher::math::Aabbd_Make;
using Catch::Approx;

namespace {

struct SpatialFixture {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_spatial_index_t index{};

    SpatialFixture()
    {
        REQUIRE( GeometrySpatialIndex_Init( &index, &allocator ) ==
                 geometry_status_t::OK );
    }

    ~SpatialFixture()
    {
        GeometrySpatialIndex_Shutdown( &index );
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

TEST_CASE( "SpatialIndex: init starts empty", "[Gate4][Spatial]" )
{
    SpatialFixture f;
    REQUIRE( GeometrySpatialIndex_Count( &f.index ) == 0u );
}

TEST_CASE( "SpatialIndex: double init rejected", "[Gate4][Spatial]" )
{
    SpatialFixture f;
    REQUIRE( GeometrySpatialIndex_Init( &f.index, &f.allocator ) ==
             geometry_status_t::ALREADY_INITIALIZED );
}

TEST_CASE( "SpatialIndex: null args rejected", "[Gate4][Spatial]" )
{
    REQUIRE( GeometrySpatialIndex_Init( nullptr, nullptr ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "SpatialIndex: shutdown safe on null", "[Gate4][Spatial]" )
{
    GeometrySpatialIndex_Shutdown( nullptr );
}

// ---------------------------------------------------------------------------
// Insert / remove / refit
// ---------------------------------------------------------------------------

TEST_CASE( "SpatialIndex: insert increases count", "[Gate4][Spatial]" )
{
    SpatialFixture f;
    const math::aabbd_t box = Aabbd_Make(
        Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ) );

    REQUIRE( GeometrySpatialIndex_TryInsert(
                 &f.index, geometry_source_id_t{ 1u }, box ) ==
             geometry_status_t::OK );
    REQUIRE( GeometrySpatialIndex_Count( &f.index ) == 1u );
}

TEST_CASE( "SpatialIndex: duplicate insert rejected", "[Gate4][Spatial]" )
{
    SpatialFixture f;
    const math::aabbd_t box = Aabbd_Make(
        Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ) );

    REQUIRE( GeometrySpatialIndex_TryInsert(
                 &f.index, geometry_source_id_t{ 1u }, box ) ==
             geometry_status_t::OK );
    REQUIRE( GeometrySpatialIndex_TryInsert(
                 &f.index, geometry_source_id_t{ 1u }, box ) ==
             geometry_status_t::IDENTITY_CONFLICT );
}

TEST_CASE( "SpatialIndex: remove decreases count", "[Gate4][Spatial]" )
{
    SpatialFixture f;
    const math::aabbd_t box = Aabbd_Make(
        Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ) );

    REQUIRE( GeometrySpatialIndex_TryInsert(
                 &f.index, geometry_source_id_t{ 1u }, box ) ==
             geometry_status_t::OK );
    REQUIRE( GeometrySpatialIndex_TryRemove(
                 &f.index, geometry_source_id_t{ 1u } ) ==
             geometry_status_t::OK );
    REQUIRE( GeometrySpatialIndex_Count( &f.index ) == 0u );
}

TEST_CASE( "SpatialIndex: remove nonexistent fails", "[Gate4][Spatial]" )
{
    SpatialFixture f;
    REQUIRE( GeometrySpatialIndex_TryRemove(
                 &f.index, geometry_source_id_t{ 99u } ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "SpatialIndex: refit updates bounds", "[Gate4][Spatial]" )
{
    SpatialFixture f;
    const math::aabbd_t box1 = Aabbd_Make(
        Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 1.0, 1.0 ) );
    const math::aabbd_t box2 = Aabbd_Make(
        Vec3d_Make( 10.0, 10.0, 10.0 ), Vec3d_Make( 20.0, 20.0, 20.0 ) );

    REQUIRE( GeometrySpatialIndex_TryInsert(
                 &f.index, geometry_source_id_t{ 1u }, box1 ) ==
             geometry_status_t::OK );
    REQUIRE( GeometrySpatialIndex_TryRefit(
                 &f.index, geometry_source_id_t{ 1u }, box2 ) ==
             geometry_status_t::OK );

    math::aabbd_t stored{};
    REQUIRE( GeometrySpatialIndex_TryGetBounds(
                 &f.index, geometry_source_id_t{ 1u }, &stored ) ==
             geometry_status_t::OK );
    REQUIRE( stored.minimum.x == Approx( 10.0 ) );
    REQUIRE( stored.maximum.x == Approx( 20.0 ) );
}

TEST_CASE( "SpatialIndex: invalid bounds are never published",
           "[Gate4][Spatial][numeric][contract]" )
{
    SpatialFixture f;
    const math::aabbd_t valid = Aabbd_Make(
        Vec3d_Make( 0.0, 0.0, 0.0 ),
        Vec3d_Make( 1.0, 1.0, 1.0 ) );
    REQUIRE( GeometrySpatialIndex_TryInsert(
                 &f.index, geometry_source_id_t{ 1u }, valid ) ==
             geometry_status_t::OK );

    const math::aabbd_t reversed = Aabbd_Make(
        Vec3d_Make( 2.0, 0.0, 0.0 ),
        Vec3d_Make( 1.0, 1.0, 1.0 ) );
    const common::f64 nan =
        std::numeric_limits<common::f64>::quiet_NaN();
    const math::aabbd_t nonFinite = Aabbd_Make(
        Vec3d_Make( nan, 0.0, 0.0 ),
        Vec3d_Make( 1.0, 1.0, 1.0 ) );

    CHECK( GeometrySpatialIndex_TryInsert(
               &f.index, geometry_source_id_t{ 2u }, reversed ) ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK( GeometrySpatialIndex_TryInsert(
               &f.index, geometry_source_id_t{ 2u }, nonFinite ) ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK( GeometrySpatialIndex_TryRefit(
               &f.index, geometry_source_id_t{ 1u }, reversed ) ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK( GeometrySpatialIndex_TryRefit(
               &f.index, geometry_source_id_t{ 1u }, nonFinite ) ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK( GeometrySpatialIndex_Count( &f.index ) == 1u );

    math::aabbd_t stored{};
    REQUIRE( GeometrySpatialIndex_TryGetBounds(
                 &f.index, geometry_source_id_t{ 1u }, &stored ) ==
             geometry_status_t::OK );
    CHECK( stored.minimum.x == valid.minimum.x );
    CHECK( stored.maximum.x == valid.maximum.x );
}

// ---------------------------------------------------------------------------
// Overlap query
// ---------------------------------------------------------------------------

TEST_CASE( "SpatialIndex: overlap finds intersecting boxes",
           "[Gate4][Spatial]" )
{
    SpatialFixture f;

    // Box A at (0,0,0)-(1,1,1).
    REQUIRE( GeometrySpatialIndex_TryInsert(
                 &f.index, geometry_source_id_t{ 1u },
                 Aabbd_Make( Vec3d_Make( 0.0, 0.0, 0.0 ),
                             Vec3d_Make( 1.0, 1.0, 1.0 ) ) ) ==
             geometry_status_t::OK );

    // Box B at (0.5,0.5,0.5)-(1.5,1.5,1.5) — overlaps A.
    REQUIRE( GeometrySpatialIndex_TryInsert(
                 &f.index, geometry_source_id_t{ 2u },
                 Aabbd_Make( Vec3d_Make( 0.5, 0.5, 0.5 ),
                             Vec3d_Make( 1.5, 1.5, 1.5 ) ) ) ==
             geometry_status_t::OK );

    // Box C at (10,10,10)-(11,11,11) — far away.
    REQUIRE( GeometrySpatialIndex_TryInsert(
                 &f.index, geometry_source_id_t{ 3u },
                 Aabbd_Make( Vec3d_Make( 10.0, 10.0, 10.0 ),
                             Vec3d_Make( 11.0, 11.0, 11.0 ) ) ) ==
             geometry_status_t::OK );

    // Query with a box that covers (0,0,0)-(0.8,0.8,0.8).
    geometry_source_id_t results[8]{};
    common::usize cHits = 0u;
    REQUIRE( GeometrySpatialIndex_QueryOverlap(
                 &f.index,
                 Aabbd_Make( Vec3d_Make( 0.0, 0.0, 0.0 ),
                             Vec3d_Make( 0.8, 0.8, 0.8 ) ),
                 results, 8u, &cHits ) ==
             geometry_status_t::OK );

    // Should find A and B, not C.
    REQUIRE( cHits == 2u );
    bool foundA = false, foundB = false;
    for ( common::usize i = 0u; i < cHits; ++i ) {
        if ( results[i].value == 1u ) foundA = true;
        if ( results[i].value == 2u ) foundB = true;
    }
    REQUIRE( foundA );
    REQUIRE( foundB );
}

// ---------------------------------------------------------------------------
// Point query
// ---------------------------------------------------------------------------

TEST_CASE( "SpatialIndex: point query finds containing boxes",
           "[Gate4][Spatial]" )
{
    SpatialFixture f;

    REQUIRE( GeometrySpatialIndex_TryInsert(
                 &f.index, geometry_source_id_t{ 1u },
                 Aabbd_Make( Vec3d_Make( 0.0, 0.0, 0.0 ),
                             Vec3d_Make( 2.0, 2.0, 2.0 ) ) ) ==
             geometry_status_t::OK );

    REQUIRE( GeometrySpatialIndex_TryInsert(
                 &f.index, geometry_source_id_t{ 2u },
                 Aabbd_Make( Vec3d_Make( 5.0, 5.0, 5.0 ),
                             Vec3d_Make( 6.0, 6.0, 6.0 ) ) ) ==
             geometry_status_t::OK );

    geometry_source_id_t results[4]{};
    common::usize cHits = 0u;
    REQUIRE( GeometrySpatialIndex_QueryPoint(
                 &f.index,
                 Vec3d_Make( 1.0, 1.0, 1.0 ),
                 results, 4u, &cHits ) ==
             geometry_status_t::OK );

    REQUIRE( cHits == 1u );
    REQUIRE( results[0].value == 1u );
}

TEST_CASE( "SpatialIndex: invalid queries fail with neutral count",
           "[Gate4][Spatial][numeric][contract]" )
{
    SpatialFixture f;
    geometry_source_id_t results[2]{};
    common::usize cHits = 91u;

    const math::aabbd_t reversed = Aabbd_Make(
        Vec3d_Make( 2.0, 0.0, 0.0 ),
        Vec3d_Make( 1.0, 1.0, 1.0 ) );
    CHECK( GeometrySpatialIndex_QueryOverlap(
               &f.index, reversed, results, 2u, &cHits ) ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK( cHits == 0u );

    cHits = 91u;
    const common::f64 nan =
        std::numeric_limits<common::f64>::quiet_NaN();
    CHECK( GeometrySpatialIndex_QueryPoint(
               &f.index, Vec3d_Make( nan, 0.0, 0.0 ),
               results, 2u, &cHits ) ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK( cHits == 0u );
}

// ---------------------------------------------------------------------------
// Brute-force reference validation
// ---------------------------------------------------------------------------

TEST_CASE( "SpatialIndex: overlap matches brute-force scan",
           "[Gate4][Spatial]" )
{
    SpatialFixture f;

    // Insert 5 boxes at known positions.
    for ( common::u64 i = 1u; i <= 5u; ++i ) {
        const common::f64 base = static_cast<common::f64>( i ) * 3.0;
        REQUIRE( GeometrySpatialIndex_TryInsert(
                     &f.index, geometry_source_id_t{ i },
                     Aabbd_Make( Vec3d_Make( base, base, base ),
                                 Vec3d_Make( base + 2.0, base + 2.0,
                                             base + 2.0 ) ) ) ==
                 geometry_status_t::OK );
    }

    // Query box overlaps brushes 1 (3-5) and 2 (6-8).
    const math::aabbd_t query = Aabbd_Make(
        Vec3d_Make( 4.0, 4.0, 4.0 ), Vec3d_Make( 7.0, 7.0, 7.0 ) );

    geometry_source_id_t indexed[8]{};
    common::usize cIndexed = 0u;
    REQUIRE( GeometrySpatialIndex_QueryOverlap(
                 &f.index, query, indexed, 8u, &cIndexed ) ==
             geometry_status_t::OK );

    // Brute-force reference: manually check each box.
    common::usize cBrute = 0u;
    for ( common::u64 i = 1u; i <= 5u; ++i ) {
        const common::f64 base = static_cast<common::f64>( i ) * 3.0;
        const math::aabbd_t box = Aabbd_Make(
            Vec3d_Make( base, base, base ),
            Vec3d_Make( base + 2.0, base + 2.0, base + 2.0 ) );
        if ( math::Aabbd_Overlaps( box, query ) ) {
            ++cBrute;
        }
    }

    REQUIRE( cIndexed == cBrute );
}

} // namespace cypher::editor::geometry
