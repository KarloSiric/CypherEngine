//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_Hierarchy_Tests.cpp
//  Purpose: Tests entity attachment (CYMAP.md 6.2 `parent`): setting and
//           clearing it without loops, and attached entities going with
//           their parent through moves, turns, copies and deletes.
//
//  History:
//  - Created by Karlo Siric on 2026-10-14
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMap_Hierarchy.h"
#include "CypherMap_Edit.h"
#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace cypher;
using namespace cypher::common;
using namespace cypher::editor::map;

namespace
{
string_view_t SV( const char *p ) { return StringView_FromCString( p ); }
struct fixture_t {
    map_document_t map{};
    fixture_t() { REQUIRE( MapDocument_Create( &map, Allocator_GetSystem(), { SV( "Hierarchy fixture" ), SV( "reap" ), {} } ) == map_status_t::OK ); }
    ~fixture_t() { MapDocument_Shutdown( &map ); }
    u64 Entity( math::vec3d_t at, const char *className = "info_target" )
    {
        u64 id = 0;
        REQUIRE( MapDocument_AddEntity( &map, SV( map.layers.pData[0].id ), SV( className ), at, &id ) == map_status_t::OK );
        return id;
    }
    u64 Box()
    {
        map_primitive_desc_t desc{}; desc.kind = map_primitive_kind_t::BOX; desc.bounds = { { 0, 0, 0 }, { 64, 64, 64 } }; desc.axis = 2;
        u64 id = 0;
        REQUIRE( MapEdit_CreatePrimitive( &map, desc, SV( "materials/dev/dev_grid.cymat" ), {}, &id ) == map_status_t::OK );
        return id;
    }
    math::vec3d_t Origin( u64 id )
    {
        const key_value_t *record = MapDocument_FindObject( &map, id, nullptr );
        REQUIRE( record != nullptr );
        const key_value_t *origin = KeyValue_Find( record, SV( "origin" ) );
        REQUIRE( KeyValue_ChildCount( origin ) == 3u );
        f64 v[3]{};
        for ( usize i = 0; i < 3; ++i ) { REQUIRE( KeyValue_GetF64( KeyValue_ChildAt( origin, i ), &v[i] ) ); }
        return { v[0], v[1], v[2] };
    }
};
} // namespace

TEST_CASE( "Entities attach to a parent and detach, never in a loop", "[map][hierarchy]" )
{
    fixture_t f;
    const u64 train = f.Entity( { 0, 0, 0 } ), light = f.Entity( { 32, 0, 64 } ), sound = f.Entity( { -32, 0, 16 } ), box = f.Box();
    CHECK( MapHierarchy_IsEntity( &f.map, train ) );
    CHECK_FALSE( MapHierarchy_IsEntity( &f.map, box ) );
    const u64 both[]{ light, sound };
    REQUIRE( MapHierarchy_SetParent( &f.map, { both, 2 }, train ) == map_status_t::OK );
    CHECK( MapHierarchy_Parent( &f.map, light ) == train );
    CHECK( MapHierarchy_Children( &f.map, train, false ) == std::vector<u64>{ light, sound } );
    // A grandchild shows up only when asked for every depth.
    const u64 spark = f.Entity( { 32, 0, 80 } );
    REQUIRE( MapHierarchy_SetParent( &f.map, { &spark, 1 }, light ) == map_status_t::OK );
    CHECK( MapHierarchy_Children( &f.map, train, false ).size() == 2u );
    CHECK( MapHierarchy_Children( &f.map, train, true ) == std::vector<u64>{ light, sound, spark } );
    // Refusals: itself, a loop through a descendant, geometry either way.
    CHECK( MapHierarchy_SetParent( &f.map, { &train, 1 }, train ) == map_status_t::INVALID_ARGUMENT );
    CHECK( MapHierarchy_SetParent( &f.map, { &train, 1 }, spark ) == map_status_t::INVALID_ARGUMENT );
    CHECK( MapHierarchy_SetParent( &f.map, { &box, 1 }, train ) == map_status_t::INVALID_ARGUMENT );
    CHECK( MapHierarchy_SetParent( &f.map, { &sound, 1 }, box ) == map_status_t::INVALID_ARGUMENT );
    CHECK( MapHierarchy_Parent( &f.map, train ) == 0u );
    // Detaching removes the member; the save writes `parent` only where set.
    REQUIRE( MapHierarchy_SetParent( &f.map, { &sound, 1 }, 0 ) == map_status_t::OK );
    CHECK( MapHierarchy_Parent( &f.map, sound ) == 0u );
    CHECK( KeyValue_Find( MapDocument_FindObject( &f.map, sound, nullptr ), SV( "parent" ) ) == nullptr );
    text_buffer_t text{};
    REQUIRE( TextBuffer_Init( &text, Allocator_GetSystem() ) );
    REQUIRE( MapDocument_Save( &f.map, &text ) == map_status_t::OK );
    const std::string saved( TextBuffer_Data( &text ), TextBuffer_Length( &text ) );
    CHECK( saved.find( "parent = " + std::to_string( train ) + "u" ) != std::string::npos );
    CHECK( saved.find( "parent = " + std::to_string( light ) + "u" ) != std::string::npos );
}

TEST_CASE( "Attached entities move, turn, copy and delete with their parent", "[map][hierarchy]" )
{
    fixture_t f;
    const u64 train = f.Entity( { 0, 0, 0 } ), light = f.Entity( { 32, 0, 64 } ), spark = f.Entity( { 32, 0, 80 } ), loose = f.Entity( { 500, 0, 0 } );
    REQUIRE( MapHierarchy_SetParent( &f.map, { &light, 1 }, train ) == map_status_t::OK );
    REQUIRE( MapHierarchy_SetParent( &f.map, { &spark, 1 }, light ) == map_status_t::OK );
    // Moving the parent takes the whole chain; moving a child leaves the parent.
    REQUIRE( MapEdit_Translate( &f.map, { &train, 1 }, { 100, 0, 0 } ) == map_status_t::OK );
    CHECK( f.Origin( train ).x == Catch::Approx( 100.0 ) );
    CHECK( f.Origin( light ).x == Catch::Approx( 132.0 ) );
    CHECK( f.Origin( spark ).x == Catch::Approx( 132.0 ) );
    CHECK( f.Origin( loose ).x == Catch::Approx( 500.0 ) );
    REQUIRE( MapEdit_Translate( &f.map, { &light, 1 }, { 0, 10, 0 } ) == map_status_t::OK );
    CHECK( f.Origin( train ).y == Catch::Approx( 0.0 ) );
    CHECK( f.Origin( spark ).y == Catch::Approx( 10.0 ) );
    // A quarter turn about the parent swings the children round it.
    REQUIRE( MapEdit_Rotate( &f.map, { &train, 1 }, { 0, 0, 90 }, f.Origin( train ) ) == map_status_t::OK );
    CHECK( f.Origin( light ).x == Catch::Approx( 90.0 ) );
    CHECK( f.Origin( light ).y == Catch::Approx( 32.0 ) );
    // Copying the parent copies the chain, attached to the copies.
    vector_t<u64> copies{};
    REQUIRE( Vector_Init( &copies, Allocator_GetSystem() ) );
    REQUIRE( MapEdit_Duplicate( &f.map, { &train, 1 }, { 0, 256, 0 }, &copies ) == map_status_t::OK );
    REQUIRE( copies.nCount == 1u );
    const u64 trainCopy = copies.pData[0];
    const auto chain = MapHierarchy_Children( &f.map, trainCopy, true );
    REQUIRE( chain.size() == 2u );
    CHECK( std::find( chain.begin(), chain.end(), light ) == chain.end() );
    CHECK( MapHierarchy_Parent( &f.map, chain[1] ) == chain[0] );
    CHECK( f.Origin( chain[0] ).y == Catch::Approx( 32.0 + 256.0 ) );
    // A child copied alone keeps its parent.
    REQUIRE( MapEdit_Duplicate( &f.map, { &spark, 1 }, { 0, 0, 32 }, &copies ) == map_status_t::OK );
    CHECK( MapHierarchy_Parent( &f.map, copies.pData[0] ) == light );
    Vector_Shutdown( &copies );
    // Deleting the parent deletes what hangs from it.
    REQUIRE( MapEdit_Delete( &f.map, { &trainCopy, 1 } ) == map_status_t::OK );
    CHECK( MapDocument_FindObject( &f.map, chain[0], nullptr ) == nullptr );
    CHECK( MapDocument_FindObject( &f.map, chain[1], nullptr ) == nullptr );
    CHECK( MapDocument_FindObject( &f.map, train, nullptr ) != nullptr );
    CHECK( MapHierarchy_Orphans( &f.map ).empty() );
}
