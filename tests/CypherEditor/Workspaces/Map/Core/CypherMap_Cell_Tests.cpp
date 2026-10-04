//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_Cell_Tests.cpp
//  Purpose: Contract tests for map cells and chunk-file names.
//  Details: Every editor must compute the same cell for the same position,
//           so these pin the floor rule, the name spelling, and the parser's
//           refusal of non-canonical names.
//
//  History:
//  - Created by Karlo Siric on 2026-09-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMap_Cell.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"
#include "CypherCommon/Tier1/CypherCommon_KeyValueParser.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <string>

using namespace cypher;
using namespace cypher::common;
using namespace cypher::editor::map;

namespace
{

std::string Name( const map_cell_t &cell )
{
    char buffer[MAP_CELL_NAME_CAPACITY]{};
    const usize cch = MapCell_FormatName( cell, buffer );
    return std::string( buffer, cch );
}

map_cell_t Parse( const char *pText, bool &bOk )
{
    map_cell_t cell{};
    bOk = MapCell_ParseName( StringView_FromCString( pText ), &cell );
    return cell;
}

} // namespace

TEST_CASE( "Cells use floor division, so negative positions get negative cells", "[map][cell]" )
{
    const map_cell_grid_t grid{};
    CHECK( Name( MapCell_ForPosition( grid, { 0.0, 0.0, 0.0 } ) ) == "x0_y0" );
    CHECK( Name( MapCell_ForPosition( grid, { 8191.9, 8192.0, 99999.0 } ) ) == "x0_y1" );
    CHECK( Name( MapCell_ForPosition( grid, { -0.5, -8192.0, 0.0 } ) ) == "x-1_y-1" );
    CHECK( Name( MapCell_ForPosition( grid, { -8192.5, 0.0, 0.0 } ) ) == "x-2_y0" );
}

TEST_CASE( "Cutting the z axis adds a z index; cutting nothing means global", "[map][cell]" )
{
    map_cell_grid_t grid{};
    grid.size[2] = 1024.0;
    CHECK( Name( MapCell_ForPosition( grid, { 0.0, 0.0, -1.0 } ) ) == "x0_y0_z-1" );
    grid = map_cell_grid_t{ { 0.0, 0.0, 0.0 } };
    CHECK( MapCell_ForPosition( grid, { 5.0, 5.0, 5.0 } ).bGlobal );
    CHECK( Name( MapCell_ForPosition( grid, { 5.0, 5.0, 5.0 } ) ) == "global" );
}

TEST_CASE( "Non-finite and runaway positions fall back to global", "[map][cell]" )
{
    const map_cell_grid_t grid{};
    CHECK( MapCell_ForPosition( grid, { std::nan( "" ), 0.0, 0.0 } ).bGlobal );
    CHECK( MapCell_ForPosition( grid, { std::numeric_limits<f64>::infinity(), 0.0, 0.0 } ).bGlobal );
    CHECK( MapCell_ForPosition( grid, { 1.0e300, 0.0, 0.0 } ).bGlobal );
}

TEST_CASE( "Names parse back to the same cell", "[map][cell]" )
{
    for ( const char *pName : { "global", "x0_y0", "x-1_y7", "x12_y-3_z4", "x1000000000_y-1000000000" } ) {
        bool bOk = false;
        const map_cell_t cell = Parse( pName, bOk );
        INFO( pName );
        REQUIRE( bOk );
        CHECK( Name( cell ) == pName );
    }
}

TEST_CASE( "Non-canonical names are refused", "[map][cell]" )
{
    for ( const char *pName : { "", "x", "x-0_y0", "y0_x0", "x0_x1", "x0y0", "x0_y0_", "_x0_y0", "x01a_y0", "X0_Y0",
                                "x1000000001_y0", "Global" } ) {
        bool bOk = true;
        ( void )Parse( pName, bOk );
        INFO( pName );
        CHECK_FALSE( bOk );
    }
}

TEST_CASE( "Cell values read and write as global or integer arrays", "[map][cell]" )
{
    key_value_document_desc_t desc{};
    desc.pAllocator = Allocator_GetSystem();
    key_value_document_t *pDocument = KeyValue_CreateDocument( desc );
    REQUIRE( pDocument != nullptr );
    REQUIRE( KeyValue_ParseText( StringView_FromCString( "@cykv 1\n@schema \"test.cell\" 1\n{ a = \"global\" b = [ -3, 4 ] c = [ 1, 2, 3 ] d = [ 1 ] e = \"x0_y0\" f = [ 1.5, 2 ] }" ),
                                 key_value_parse_options_t{}, pDocument ).status == key_value_parse_status_t::OK );
    key_value_t *pRoot = KeyValue_Root( pDocument );
    map_cell_t cell{};
    REQUIRE( MapCell_Read( KeyValue_Find( pRoot, StringView_FromCString( "a" ) ), &cell ) );
    CHECK( cell.bGlobal );
    REQUIRE( MapCell_Read( KeyValue_Find( pRoot, StringView_FromCString( "b" ) ), &cell ) );
    CHECK( Name( cell ) == "x-3_y4" );
    REQUIRE( MapCell_Read( KeyValue_Find( pRoot, StringView_FromCString( "c" ) ), &cell ) );
    CHECK( Name( cell ) == "x1_y2_z3" );
    CHECK_FALSE( MapCell_Read( KeyValue_Find( pRoot, StringView_FromCString( "d" ) ), &cell ) );
    CHECK_FALSE( MapCell_Read( KeyValue_Find( pRoot, StringView_FromCString( "e" ) ), &cell ) );
    CHECK_FALSE( MapCell_Read( KeyValue_Find( pRoot, StringView_FromCString( "f" ) ), &cell ) );

    // Write, then read back.
    key_value_t *pOut = KeyValue_ObjectInsert( pDocument, pRoot, StringView_FromCString( "out" ), key_value_type_t::NULL_VALUE );
    REQUIRE( pOut != nullptr );
    bool bOk = false;
    const map_cell_t written = Parse( "x-7_y8_z-9", bOk );
    REQUIRE( bOk );
    REQUIRE( MapCell_Write( pDocument, pOut, written ) );
    REQUIRE( MapCell_Read( pOut, &cell ) );
    CHECK( MapCell_Equals( cell, written ) );
    REQUIRE( MapCell_Write( pDocument, pOut, MapCell_Global() ) );
    REQUIRE( MapCell_Read( pOut, &cell ) );
    CHECK( cell.bGlobal );
    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "Cells order global first, then by axes and index", "[map][cell]" )
{
    bool bOk = false;
    const map_cell_t a = Parse( "x-1_y0", bOk );
    const map_cell_t b = Parse( "x0_y-5", bOk );
    const map_cell_t c = Parse( "x0_y0_z0", bOk );
    CHECK( MapCell_Compare( MapCell_Global(), a ) < 0 );
    CHECK( MapCell_Compare( a, b ) < 0 );
    CHECK( MapCell_Compare( b, c ) < 0 );
    CHECK( MapCell_Compare( c, c ) == 0 );
    CHECK( MapCell_MatchesGrid( a, map_cell_grid_t{} ) );
    CHECK_FALSE( MapCell_MatchesGrid( c, map_cell_grid_t{} ) );
}
