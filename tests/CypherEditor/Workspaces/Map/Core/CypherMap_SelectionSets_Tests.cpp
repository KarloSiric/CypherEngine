//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_SelectionSets_Tests.cpp
//  Purpose: Tests creating, growing, shrinking, renaming and deleting
//           selection sets, and that they save with the map root.
//
//  History:
//  - Created by Karlo Siric on 2026-10-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMap_SelectionSets.h"
#include "CypherMap_Edit.h"
#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>
#include <vector>

using namespace cypher;
using namespace cypher::common;
using namespace cypher::editor::map;

namespace
{
string_view_t SV( const char *p ) { return StringView_FromCString( p ); }
string_view_t SV( const std::string &s ) { return { s.data(), s.size() }; }

struct saved_t {
    std::string root;
    map_status_t Save( map_document_t *pMap )
    {
        text_buffer_t text{};
        if ( !TextBuffer_Init( &text, Allocator_GetSystem() ) ) { return map_status_t::OUT_OF_MEMORY; }
        const map_status_t status = MapDocument_Save( pMap, &text );
        if ( status == map_status_t::OK ) { root.assign( TextBuffer_Data( &text ), TextBuffer_Length( &text ) ); }
        return status;
    }
};

struct fixture_t {
    map_document_t map{};
    fixture_t() { REQUIRE( MapDocument_Create( &map, Allocator_GetSystem(), { SV( "Sets fixture" ), SV( "reap" ), {} } ) == map_status_t::OK ); }
    u64 Box( f64 x )
    {
        map_primitive_desc_t desc{}; desc.kind = map_primitive_kind_t::BOX; desc.bounds = { { x, 0, 0 }, { x + 64, 64, 64 } }; desc.axis = 2;
        u64 id = 0;
        REQUIRE( MapEdit_CreatePrimitive( &map, desc, SV( "materials/dev/dev_grid.cymat" ), {}, &id ) == map_status_t::OK );
        return id;
    }
};
} // namespace

TEST_CASE( "Selection sets keep sorted members, grow, shrink, rename and delete", "[map][selection_sets]" )
{
    fixture_t f;
    const u64 a = f.Box( 0 ), b = f.Box( 128 ), c = f.Box( 256 );
    CHECK( MapSelectionSets_All( &f.map ).empty() );
    const u64 members[]{ c, a, c };
    u64 set = 0;
    REQUIRE( MapSelectionSets_New( &f.map, SV( "Doors" ), { members, 3 }, &set ) == map_status_t::OK );
    CHECK( set > c );
    auto all = MapSelectionSets_All( &f.map );
    REQUIRE( all.size() == 1u );
    CHECK( all[0].id == set ); CHECK( all[0].name == "Doors" );
    CHECK( all[0].members == std::vector<u64>{ a, c } );

    bool changed = false;
    REQUIRE( MapSelectionSets_Edit( &f.map, set, { &b, 1 }, {}, &changed ) == map_status_t::OK ); CHECK( changed );
    CHECK( MapSelectionSets_All( &f.map )[0].members == std::vector<u64>{ a, b, c } );
    changed = false;
    REQUIRE( MapSelectionSets_Edit( &f.map, set, { &b, 1 }, {}, &changed ) == map_status_t::OK ); CHECK_FALSE( changed );
    REQUIRE( MapSelectionSets_Edit( &f.map, set, {}, { &a, 1 }, &changed ) == map_status_t::OK ); CHECK( changed );
    CHECK( MapSelectionSets_All( &f.map )[0].members == std::vector<u64>{ b, c } );

    changed = false;
    REQUIRE( MapSelectionSets_Rename( &f.map, set, SV( "Arena doors" ), &changed ) == map_status_t::OK ); CHECK( changed );
    changed = false;
    REQUIRE( MapSelectionSets_Rename( &f.map, set, SV( "Arena doors" ), &changed ) == map_status_t::OK ); CHECK_FALSE( changed );
    CHECK( MapSelectionSets_Rename( &f.map, set, SV( "" ), &changed ) == map_status_t::INVALID_ARGUMENT );
    CHECK( MapSelectionSets_Edit( &f.map, set + 1000, { &a, 1 }, {}, &changed ) == map_status_t::UNKNOWN_OBJECT );

    // A second set; deleting the first leaves the second and every object.
    u64 second = 0;
    REQUIRE( MapSelectionSets_New( &f.map, SV( "Lights" ), { &a, 1 }, &second ) == map_status_t::OK );
    REQUIRE( MapSelectionSets_Delete( &f.map, set ) == map_status_t::OK );
    all = MapSelectionSets_All( &f.map );
    REQUIRE( all.size() == 1u ); CHECK( all[0].id == second );
    CHECK( MapSelectionSets_Delete( &f.map, set ) == map_status_t::UNKNOWN_OBJECT );
}

TEST_CASE( "Selection sets save with the map root and load back", "[map][selection_sets]" )
{
    fixture_t f;
    const u64 a = f.Box( 0 ), b = f.Box( 128 );
    const u64 members[]{ b, a };
    u64 set = 0;
    REQUIRE( MapSelectionSets_New( &f.map, SV( "Room shell" ), { members, 2 }, &set ) == map_status_t::OK );
    saved_t saved;
    REQUIRE( saved.Save( &f.map ) == map_status_t::OK );
    CHECK( saved.root.find( "selection_sets" ) != std::string::npos );

    map_document_t loaded{};
    REQUIRE( MapDocument_Load( &loaded, Allocator_GetSystem(), SV( saved.root ) ) == map_status_t::OK );
    const auto all = MapSelectionSets_All( &loaded );
    REQUIRE( all.size() == 1u );
    CHECK( all[0].id == set ); CHECK( all[0].name == "Room shell" );
    CHECK( all[0].members == std::vector<u64>{ a, b } );
    // A new ID never reuses the set's.
    CHECK( MapDocument_AllocateId( &loaded ) > set );
}

TEST_CASE( "Selection sets refuse read-only maps and bad names", "[map][selection_sets]" )
{
    fixture_t f;
    const u64 a = f.Box( 0 );
    u64 set = 0;
    CHECK( MapSelectionSets_New( &f.map, SV( "" ), { &a, 1 }, &set ) == map_status_t::INVALID_ARGUMENT );
    const std::string tooLong( MAP_NAME_MAX_LENGTH + 1, 'x' );
    CHECK( MapSelectionSets_New( &f.map, SV( tooLong ), { &a, 1 }, &set ) == map_status_t::INVALID_ARGUMENT );
    f.map.bReadOnly = true;
    CHECK( MapSelectionSets_New( &f.map, SV( "Doors" ), { &a, 1 }, &set ) == map_status_t::READ_ONLY );
    f.map.bReadOnly = false;
}
