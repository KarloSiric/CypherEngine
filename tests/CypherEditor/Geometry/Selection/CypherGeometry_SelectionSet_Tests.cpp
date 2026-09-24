//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_SelectionSet_Tests.cpp
//  Purpose: Verifies typed geometry component selection sets.
//  Details: Covers Gate 4 selection acceptance: add/remove/toggle/clear,
//           contains/count, level retention, duplicate rejection, and
//           error paths.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_SelectionSet.h"

#include <catch2/catch_test_macros.hpp>

namespace cypher::editor::geometry {

namespace {

struct SelectionFixture {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_selection_set_t set{};

    SelectionFixture()
    {
        REQUIRE( GeometrySelectionSet_Init(
                     &set, &allocator,
                     geometry_selection_level_t::BRUSH ) ==
                 geometry_status_t::OK );
    }

    ~SelectionFixture()
    {
        GeometrySelectionSet_Shutdown( &set );
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

TEST_CASE( "Selection: init starts empty at correct level",
           "[Gate4][Selection]" )
{
    SelectionFixture f;
    REQUIRE( GeometrySelectionSet_IsEmpty( &f.set ) );
    REQUIRE( GeometrySelectionSet_Count( &f.set ) == 0u );
    REQUIRE( GeometrySelectionSet_GetLevel( &f.set ) ==
             geometry_selection_level_t::BRUSH );
}

TEST_CASE( "Selection: double init rejected", "[Gate4][Selection]" )
{
    SelectionFixture f;
    REQUIRE( GeometrySelectionSet_Init(
                 &f.set, &f.allocator,
                 geometry_selection_level_t::SIDE ) ==
             geometry_status_t::ALREADY_INITIALIZED );
}

TEST_CASE( "Selection: invalid level rejected", "[Gate4][Selection]" )
{
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_selection_set_t set{};
    REQUIRE( GeometrySelectionSet_Init(
                 &set, &allocator,
                 geometry_selection_level_t::INVALID ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Selection: null args rejected", "[Gate4][Selection]" )
{
    REQUIRE( GeometrySelectionSet_Init(
                 nullptr, nullptr,
                 geometry_selection_level_t::BRUSH ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Selection: shutdown safe on null", "[Gate4][Selection]" )
{
    GeometrySelectionSet_Shutdown( nullptr );
}

TEST_CASE( "Selection: partial initialization is rejected as corrupt",
           "[Gate4][Selection][contract]" )
{
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_selection_set_t set{};
    REQUIRE( common::Vector_Init( &set.ids, &allocator ) );

    CHECK( GeometrySelectionSet_Init(
               &set, &allocator,
               geometry_selection_level_t::BRUSH ) ==
           geometry_status_t::CORRUPT_STATE );

    GeometrySelectionSet_Shutdown( &set );
}

// ---------------------------------------------------------------------------
// Add / remove / contains
// ---------------------------------------------------------------------------

TEST_CASE( "Selection: add increases count", "[Gate4][Selection]" )
{
    SelectionFixture f;
    REQUIRE( GeometrySelectionSet_TryAdd(
                 &f.set, geometry_source_id_t{ 1u } ) ==
             geometry_status_t::OK );
    REQUIRE( GeometrySelectionSet_Count( &f.set ) == 1u );
    REQUIRE( GeometrySelectionSet_Contains(
                 &f.set, geometry_source_id_t{ 1u } ) );
}

TEST_CASE( "Selection: duplicate add rejected", "[Gate4][Selection]" )
{
    SelectionFixture f;
    REQUIRE( GeometrySelectionSet_TryAdd(
                 &f.set, geometry_source_id_t{ 1u } ) ==
             geometry_status_t::OK );
    REQUIRE( GeometrySelectionSet_TryAdd(
                 &f.set, geometry_source_id_t{ 1u } ) ==
             geometry_status_t::IDENTITY_CONFLICT );
    REQUIRE( GeometrySelectionSet_Count( &f.set ) == 1u );
}

TEST_CASE( "Selection: remove decreases count", "[Gate4][Selection]" )
{
    SelectionFixture f;
    REQUIRE( GeometrySelectionSet_TryAdd(
                 &f.set, geometry_source_id_t{ 1u } ) ==
             geometry_status_t::OK );
    REQUIRE( GeometrySelectionSet_TryRemove(
                 &f.set, geometry_source_id_t{ 1u } ) ==
             geometry_status_t::OK );
    REQUIRE( GeometrySelectionSet_Count( &f.set ) == 0u );
    REQUIRE_FALSE( GeometrySelectionSet_Contains(
                       &f.set, geometry_source_id_t{ 1u } ) );
}

TEST_CASE( "Selection: remove nonexistent fails", "[Gate4][Selection]" )
{
    SelectionFixture f;
    REQUIRE( GeometrySelectionSet_TryRemove(
                 &f.set, geometry_source_id_t{ 99u } ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Selection: invalid ID rejected", "[Gate4][Selection]" )
{
    SelectionFixture f;
    REQUIRE( GeometrySelectionSet_TryAdd(
                 &f.set, geometry_source_id_t{ 0u } ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

// ---------------------------------------------------------------------------
// Toggle
// ---------------------------------------------------------------------------

TEST_CASE( "Selection: toggle adds when absent", "[Gate4][Selection]" )
{
    SelectionFixture f;
    bool selected = false;
    REQUIRE( GeometrySelectionSet_Toggle(
                 &f.set, geometry_source_id_t{ 5u }, &selected ) ==
             geometry_status_t::OK );
    REQUIRE( selected );
    REQUIRE( GeometrySelectionSet_Contains(
                 &f.set, geometry_source_id_t{ 5u } ) );
}

TEST_CASE( "Selection: toggle removes when present", "[Gate4][Selection]" )
{
    SelectionFixture f;
    REQUIRE( GeometrySelectionSet_TryAdd(
                 &f.set, geometry_source_id_t{ 5u } ) ==
             geometry_status_t::OK );

    bool selected = true;
    REQUIRE( GeometrySelectionSet_Toggle(
                 &f.set, geometry_source_id_t{ 5u }, &selected ) ==
             geometry_status_t::OK );
    REQUIRE_FALSE( selected );
    REQUIRE_FALSE( GeometrySelectionSet_Contains(
                       &f.set, geometry_source_id_t{ 5u } ) );
}

TEST_CASE( "Selection: failed toggle neutralizes its output",
           "[Gate4][Selection][contract]" )
{
    SelectionFixture f;
    bool selected = true;
    CHECK( GeometrySelectionSet_Toggle(
               &f.set, GEOMETRY_SOURCE_ID_INVALID, &selected ) ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK_FALSE( selected );
}

// ---------------------------------------------------------------------------
// Clear
// ---------------------------------------------------------------------------

TEST_CASE( "Selection: clear removes all IDs", "[Gate4][Selection]" )
{
    SelectionFixture f;
    REQUIRE( GeometrySelectionSet_TryAdd(
                 &f.set, geometry_source_id_t{ 1u } ) ==
             geometry_status_t::OK );
    REQUIRE( GeometrySelectionSet_TryAdd(
                 &f.set, geometry_source_id_t{ 2u } ) ==
             geometry_status_t::OK );
    REQUIRE( GeometrySelectionSet_TryAdd(
                 &f.set, geometry_source_id_t{ 3u } ) ==
             geometry_status_t::OK );

    GeometrySelectionSet_Clear( &f.set );
    REQUIRE( GeometrySelectionSet_IsEmpty( &f.set ) );
    REQUIRE( GeometrySelectionSet_Count( &f.set ) == 0u );
}

// ---------------------------------------------------------------------------
// GetAll
// ---------------------------------------------------------------------------

TEST_CASE( "Selection: GetAll returns all selected IDs",
           "[Gate4][Selection]" )
{
    SelectionFixture f;
    REQUIRE( GeometrySelectionSet_TryAdd(
                 &f.set, geometry_source_id_t{ 10u } ) ==
             geometry_status_t::OK );
    REQUIRE( GeometrySelectionSet_TryAdd(
                 &f.set, geometry_source_id_t{ 20u } ) ==
             geometry_status_t::OK );

    geometry_source_id_t ids[4]{};
    common::usize count = 0u;
    REQUIRE( GeometrySelectionSet_TryGetAll(
                 &f.set, ids, 4u, &count ) ==
             geometry_status_t::OK );
    REQUIRE( count == 2u );

    // Both IDs present (order not guaranteed).
    bool found10 = false, found20 = false;
    for ( common::usize i = 0u; i < count; ++i ) {
        if ( ids[i].value == 10u ) found10 = true;
        if ( ids[i].value == 20u ) found20 = true;
    }
    REQUIRE( found10 );
    REQUIRE( found20 );
}

TEST_CASE( "Selection: GetAll with insufficient capacity",
           "[Gate4][Selection]" )
{
    SelectionFixture f;
    REQUIRE( GeometrySelectionSet_TryAdd(
                 &f.set, geometry_source_id_t{ 1u } ) ==
             geometry_status_t::OK );
    REQUIRE( GeometrySelectionSet_TryAdd(
                 &f.set, geometry_source_id_t{ 2u } ) ==
             geometry_status_t::OK );

    geometry_source_id_t ids[1]{};
    common::usize count = 0u;
    REQUIRE( GeometrySelectionSet_TryGetAll(
                 &f.set, ids, 1u, &count ) ==
             geometry_status_t::INSUFFICIENT_CAPACITY );
    REQUIRE( count == 2u );
}

TEST_CASE( "Selection: GetAll supports a count-only query",
           "[Gate4][Selection][contract]" )
{
    SelectionFixture f;
    REQUIRE( GeometrySelectionSet_TryAdd(
                 &f.set, geometry_source_id_t{ 1u } ) ==
             geometry_status_t::OK );
    REQUIRE( GeometrySelectionSet_TryAdd(
                 &f.set, geometry_source_id_t{ 2u } ) ==
             geometry_status_t::OK );

    common::usize count = 99u;
    CHECK( GeometrySelectionSet_TryGetAll(
               &f.set, nullptr, 0u, &count ) ==
           geometry_status_t::INSUFFICIENT_CAPACITY );
    CHECK( count == 2u );
}

// ---------------------------------------------------------------------------
// Multiple levels
// ---------------------------------------------------------------------------

TEST_CASE( "Selection: side-level selection works",
           "[Gate4][Selection]" )
{
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_selection_set_t set{};
    REQUIRE( GeometrySelectionSet_Init(
                 &set, &allocator,
                 geometry_selection_level_t::SIDE ) ==
             geometry_status_t::OK );
    REQUIRE( GeometrySelectionSet_GetLevel( &set ) ==
             geometry_selection_level_t::SIDE );

    REQUIRE( GeometrySelectionSet_TryAdd(
                 &set, geometry_source_id_t{ 42u } ) ==
             geometry_status_t::OK );
    REQUIRE( GeometrySelectionSet_Contains(
                 &set, geometry_source_id_t{ 42u } ) );

    GeometrySelectionSet_Shutdown( &set );
}

} // namespace cypher::editor::geometry
