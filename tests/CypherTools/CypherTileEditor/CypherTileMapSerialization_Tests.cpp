//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileMapSerialization_Tests.cpp
//  Purpose: Tests deterministic authored tile-map persistence.
//  Details: Covers stable sparse output, lossless round trips, exact headers,
//           malformed values, duplicates, and transactional load failure.
//
//  History:
//  - Created by Karlo Siric on 2026-09-16
//
//////////////////////////////////////////////////////////////////////////

#include "CypherTileMapSerialization.h"
#include "CypherTileMapGeometry.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace cypher::common;
using namespace cypher::tools::tile_editor;

namespace
{

unique_id_t ParseId( const char *pText )
{
    unique_id_t id{};
    REQUIRE( UniqueId_FromString( StringView_FromCString( pText ), &id ) );
    return id;
}

std::string ValidMapText(
    const std::string &cells = "[]",
    const std::string &markers = "[]",
    const char *pSchemaId = "cypher.map",
    u32 nSchemaVersion = 1u )
{
    return
        "@cykv 1\n@schema \"" + std::string( pSchemaId ) + "\" " +
        std::to_string( nSchemaVersion ) + "\n"
        "{\n"
        "  map_id = \"00112233-4455-4677-8899-aabbccddeeff\"\n"
        "  dimensions = { width = 8u height = 6u }\n"
        "  metrics = { cell_size = 2.0 level_height = 3.0 }\n"
        "  cells = " + cells + "\n"
        "  markers = " + markers + "\n"
        "}\n";
}

void RequireFresh( const tile_map_document_t &document )
{
    REQUIRE( document.pAllocator == nullptr );
    REQUIRE_FALSE( UniqueId_IsValid( document.mapId ) );
    REQUIRE( document.nWidth == 0u );
    REQUIRE( document.nHeight == 0u );
    REQUIRE( document.cells.pData == nullptr );
    REQUIRE( document.cells.nCount == 0u );
    REQUIRE( document.markers.pData == nullptr );
    REQUIRE( document.markers.nCount == 0u );
}

} // namespace

TEST_CASE( "Tile-map serialization is deterministic and sparse",
           "[CypherTools][CypherTileEditor][Serialization]" )
{
    tile_map_document_t document{};
    REQUIRE(
        CypherTileMapDocument_Init(
            &document,
            Allocator_GetSystem(),
            { 8u, 6u, 2.0f, 3.0f } ) ==
        tile_map_document_status_t::OK );
    document.mapId = ParseId( "00112233-4455-4677-8899-aabbccddeeff" );

    tile_map_cell_t *pLate = CypherTileMapDocument_CellAt(
        &document,
        { 2, 4 } );
    tile_map_cell_t *pEarly = CypherTileMapDocument_CellAt(
        &document,
        { 7, 1 } );
    REQUIRE( pLate != nullptr );
    REQUIRE( pEarly != nullptr );
    *pLate = { -2, 2u, 9u, TILE_MAP_CELL_FLAG_FLOOR };
    *pEarly = { 1, 1u, 3u, TILE_MAP_CELL_FLAG_FLOOR };

    // Deliberately append larger UUID first. Save sorts marker records by UUID.
    REQUIRE( Vector_PushBack(
        &document.markers,
        tile_map_marker_t{
            ParseId( "ffffffff-ffff-4fff-8fff-ffffffffffff" ),
            tile_map_marker_kind_t::PLAYER_SPAWN,
            { 2, 4 },
            180.0f
        } ) );
    REQUIRE( Vector_PushBack(
        &document.markers,
        tile_map_marker_t{
            ParseId( "11111111-1111-4111-8111-111111111111" ),
            tile_map_marker_kind_t::PLAYER_SPAWN,
            { 7, 1 },
            90.0f
        } ) );

    text_buffer_t first{};
    text_buffer_t second{};
    REQUIRE( TextBuffer_Init( &first, Allocator_GetSystem() ) );
    REQUIRE( TextBuffer_Init( &second, Allocator_GetSystem() ) );

    const tile_map_serialization_result_t firstSaved =
        CypherTileMapSerialization_SaveToText( &document, &first );
    const tile_map_serialization_result_t secondSaved =
        CypherTileMapSerialization_SaveToText( &document, &second );
    REQUIRE( firstSaved.status == tile_map_serialization_status_t::OK );
    REQUIRE( secondSaved.status == tile_map_serialization_status_t::OK );
    REQUIRE( firstSaved.cchText == TextBuffer_Length( &first ) );
    REQUIRE( StringView_Equals(
        TextBuffer_View( &first ),
        TextBuffer_View( &second ) ) );

    const std::string saved(
        TextBuffer_Data( &first ),
        TextBuffer_Length( &first ) );
    REQUIRE( saved.starts_with(
        "@cykv 1\n@schema \"cypher.map\" 3\n" ) );

    const usize iEarly = saved.find(
        "\"x\" = 7u\n      \"y\" = 1u" );
    const usize iLate = saved.find(
        "\"x\" = 2u\n      \"y\" = 4u" );
    REQUIRE( iEarly != std::string::npos );
    REQUIRE( iLate != std::string::npos );
    REQUIRE( iEarly < iLate );

    const usize iSmallMarker = saved.find(
        "11111111-1111-4111-8111-111111111111" );
    const usize iLargeMarker = saved.find(
        "ffffffff-ffff-4fff-8fff-ffffffffffff" );
    REQUIRE( iSmallMarker != std::string::npos );
    REQUIRE( iLargeMarker != std::string::npos );
    REQUIRE( iSmallMarker < iLargeMarker );

    CypherTileMapDocument_Shutdown( &document );
}

TEST_CASE( "Tile-map serialization round trips authored state",
           "[CypherTools][CypherTileEditor][Serialization]" )
{
    const std::string source = ValidMapText(
        "[\n"
        "  { x = 5u y = 3u floor_level = -1 "
        "wall_height_levels = 2u material_slot = 7u flags = 1u },\n"
        "  { x = 1u y = 0u floor_level = 0 "
        "wall_height_levels = 1u material_slot = 0u flags = 1u }\n"
        "]",
        "[\n"
        "  { id = \"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa\" "
        "kind = \"player_spawn\" x = 5u y = 3u yaw_degrees = 45.0 }\n"
        "]" );

    tile_map_document_t loaded{};
    const tile_map_serialization_result_t loadResult =
        CypherTileMapSerialization_LoadFromText(
            { source.data(), source.size() },
            Allocator_GetSystem(),
            &loaded );
    INFO( CypherTileMapSerialization_StatusName( loadResult.status ) );
    REQUIRE( loadResult.status == tile_map_serialization_status_t::OK );
    REQUIRE( CypherTileMapDocument_IsInitialized( &loaded ) );
    REQUIRE_FALSE( CypherTileMapDocument_IsDirty( &loaded ) );
    REQUIRE( loaded.nWidth == 8u );
    REQUIRE( loaded.nHeight == 6u );
    REQUIRE( loaded.nCellSize == 2.0f );
    REQUIRE( loaded.nLevelHeight == 3.0f );

    const tile_map_cell_t *pFirst = CypherTileMapDocument_CellAt(
        &loaded,
        { 5, 3 } );
    const tile_map_cell_t *pSecond = CypherTileMapDocument_CellAt(
        &loaded,
        { 1, 0 } );
    const tile_map_cell_t *pEmpty = CypherTileMapDocument_CellAt(
        &loaded,
        { 0, 0 } );
    REQUIRE( pFirst != nullptr );
    REQUIRE( pSecond != nullptr );
    REQUIRE( pEmpty != nullptr );
    REQUIRE( pFirst->nFloorLevel == -1 );
    REQUIRE( pFirst->nWallHeightLevels == 2u );
    REQUIRE( pFirst->nMaterialSlot == 7u );
    REQUIRE( pFirst->flags == TILE_MAP_CELL_FLAG_FLOOR );
    REQUIRE( pSecond->flags == TILE_MAP_CELL_FLAG_FLOOR );
    REQUIRE( pEmpty->flags == TILE_MAP_CELL_FLAG_NONE );
    REQUIRE( Vector_Count( &loaded.markers ) == 1u );
    REQUIRE( loaded.markers.pData[0].cell.x == 5 );
    REQUIRE( loaded.markers.pData[0].cell.y == 3 );
    REQUIRE( loaded.markers.pData[0].yawDegrees == 45.0f );

    text_buffer_t firstSave{};
    text_buffer_t secondSave{};
    REQUIRE( TextBuffer_Init( &firstSave, Allocator_GetSystem() ) );
    REQUIRE( TextBuffer_Init( &secondSave, Allocator_GetSystem() ) );
    REQUIRE(
        CypherTileMapSerialization_SaveToText(
            &loaded,
            &firstSave ).status == tile_map_serialization_status_t::OK );

    tile_map_document_t reloaded{};
    REQUIRE(
        CypherTileMapSerialization_LoadFromText(
            TextBuffer_View( &firstSave ),
            Allocator_GetSystem(),
            &reloaded ).status == tile_map_serialization_status_t::OK );
    REQUIRE(
        CypherTileMapSerialization_SaveToText(
            &reloaded,
            &secondSave ).status == tile_map_serialization_status_t::OK );
    REQUIRE( StringView_Equals(
        TextBuffer_View( &firstSave ),
        TextBuffer_View( &secondSave ) ) );

    CypherTileMapDocument_Shutdown( &reloaded );
    CypherTileMapDocument_Shutdown( &loaded );
}

TEST_CASE( "Tile-map serialization round trips cardinal door markers",
           "[CypherTools][CypherTileEditor][Serialization][Door]" )
{
    const std::string source = ValidMapText(
        "[{ x = 1u y = 1u floor_level = 0 "
        "wall_height_levels = 1u material_slot = 4u flags = 1u }]",
        "[\n"
        "  { id = \"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa\" "
        "kind = \"player_spawn\" x = 1u y = 1u yaw_degrees = 0.0 },\n"
        "  { id = \"bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb\" "
        "kind = \"door\" x = 1u y = 1u side = \"east\" }\n"
        "]" );

    tile_map_document_t document{};
    const tile_map_serialization_result_t loaded =
        CypherTileMapSerialization_LoadFromText(
            { source.data(), source.size() },
            Allocator_GetSystem(),
            &document );
    INFO( CypherTileMapSerialization_StatusName( loaded.status ) );
    REQUIRE( loaded.status == tile_map_serialization_status_t::OK );
    REQUIRE( Vector_Count( &document.markers ) == 2u );
    const tile_map_marker_t *pDoor = CypherTileMapDocument_DoorAt(
        &document,
        { 1, 1 },
        tile_map_marker_side_t::EAST );
    REQUIRE( pDoor != nullptr );
    REQUIRE( pDoor->kind == tile_map_marker_kind_t::DOOR );
    REQUIRE( pDoor->yawDegrees == 0.0f );

    text_buffer_t first{};
    text_buffer_t second{};
    REQUIRE( TextBuffer_Init( &first, Allocator_GetSystem() ) );
    REQUIRE( TextBuffer_Init( &second, Allocator_GetSystem() ) );
    REQUIRE( CypherTileMapSerialization_SaveToText(
                 &document,
                 &first ).status == tile_map_serialization_status_t::OK );
    const std::string saved(
        TextBuffer_Data( &first ),
        TextBuffer_Length( &first ) );
    REQUIRE( saved.find( "\"kind\" = \"door\"" ) != std::string::npos );
    REQUIRE( saved.find( "\"side\" = \"east\"" ) != std::string::npos );

    tile_map_document_t reloaded{};
    REQUIRE( CypherTileMapSerialization_LoadFromText(
                 TextBuffer_View( &first ),
                 Allocator_GetSystem(),
                 &reloaded ).status == tile_map_serialization_status_t::OK );
    REQUIRE( CypherTileMapSerialization_SaveToText(
                 &reloaded,
                 &second ).status == tile_map_serialization_status_t::OK );
    REQUIRE( StringView_Equals(
        TextBuffer_View( &first ),
        TextBuffer_View( &second ) ) );

    CypherTileMapDocument_Shutdown( &reloaded );
    CypherTileMapDocument_Shutdown( &document );
}

TEST_CASE( "Tile-map loading rejects the wrong schema and malformed CYKV",
           "[CypherTools][CypherTileEditor][Serialization]" )
{
    SECTION( "schema id" )
    {
        const std::string source = ValidMapText(
            "[]",
            "[]",
            "cypher.material",
            1u );
        tile_map_document_t output{};
        const tile_map_serialization_result_t result =
            CypherTileMapSerialization_LoadFromText(
                { source.data(), source.size() },
                Allocator_GetSystem(),
                &output );
        REQUIRE( result.status ==
                 tile_map_serialization_status_t::HEADER_MISMATCH );
        REQUIRE( std::string( result.field ) == "@schema.id" );
        REQUIRE( result.location.nLine == 2u );
        RequireFresh( output );
    }

    SECTION( "schema version" )
    {
        const std::string source = ValidMapText(
            "[]",
            "[]",
            "cypher.map",
            TILE_MAP_SCHEMA_VERSION + 1u );
        tile_map_document_t output{};
        const tile_map_serialization_result_t result =
            CypherTileMapSerialization_LoadFromText(
                { source.data(), source.size() },
                Allocator_GetSystem(),
                &output );
        REQUIRE( result.status ==
                 tile_map_serialization_status_t::HEADER_MISMATCH );
        REQUIRE( std::string( result.field ) == "@schema.version" );
        RequireFresh( output );
    }

    SECTION( "duplicate object key" )
    {
        std::string source = ValidMapText();
        const usize iClose = source.rfind( '}' );
        REQUIRE( iClose != std::string::npos );
        source.insert(
            iClose,
            "  map_id = \"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa\"\n" );

        tile_map_document_t output{};
        const tile_map_serialization_result_t result =
            CypherTileMapSerialization_LoadFromText(
                { source.data(), source.size() },
                Allocator_GetSystem(),
                &output );
        REQUIRE( result.status ==
                 tile_map_serialization_status_t::CYKV_PARSE_FAILED );
        REQUIRE( result.parseStatus == key_value_parse_status_t::DUPLICATE_KEY );
        REQUIRE( result.location.nLine != 0u );
        RequireFresh( output );
    }
}

TEST_CASE( "Tile-map loading rejects duplicate and out-of-range records",
           "[CypherTools][CypherTileEditor][Serialization]" )
{
    SECTION( "duplicate cell" )
    {
        const std::string cell =
            "{ x = 2u y = 1u floor_level = 0 "
            "wall_height_levels = 1u material_slot = 0u flags = 1u }";
        const std::string source = ValidMapText(
            "[" + cell + "," + cell + "]" );
        tile_map_document_t output{};
        const tile_map_serialization_result_t result =
            CypherTileMapSerialization_LoadFromText(
                { source.data(), source.size() },
                Allocator_GetSystem(),
                &output );
        REQUIRE( result.status ==
                 tile_map_serialization_status_t::DUPLICATE_CELL );
        REQUIRE( result.iElement == 1u );
        RequireFresh( output );
    }

    SECTION( "cell outside dimensions" )
    {
        const std::string source = ValidMapText(
            "[{ x = 8u y = 0u floor_level = 0 "
            "wall_height_levels = 1u material_slot = 0u flags = 1u }]" );
        tile_map_document_t output{};
        const tile_map_serialization_result_t result =
            CypherTileMapSerialization_LoadFromText(
                { source.data(), source.size() },
                Allocator_GetSystem(),
                &output );
        REQUIRE( result.status ==
                 tile_map_serialization_status_t::VALUE_OUT_OF_RANGE );
        REQUIRE( std::string( result.field ) == "x" );
        REQUIRE( result.iElement == 0u );
        RequireFresh( output );
    }

    SECTION( "duplicate marker identity" )
    {
        const std::string marker =
            "{ id = \"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa\" "
            "kind = \"player_spawn\" x = 1u y = 1u yaw_degrees = 0.0 }";
        const std::string source = ValidMapText(
            "[]",
            "[" + marker + "," + marker + "]" );
        tile_map_document_t output{};
        const tile_map_serialization_result_t result =
            CypherTileMapSerialization_LoadFromText(
                { source.data(), source.size() },
                Allocator_GetSystem(),
                &output );
        REQUIRE( result.status ==
                 tile_map_serialization_status_t::DUPLICATE_MARKER_ID );
        REQUIRE( result.iElement == 1u );
        RequireFresh( output );
    }

    SECTION( "invalid door side" )
    {
        const std::string source = ValidMapText(
            "[]",
            "[{ id = \"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa\" "
            "kind = \"door\" x = 1u y = 1u side = \"up\" }]" );
        tile_map_document_t output{};
        const tile_map_serialization_result_t result =
            CypherTileMapSerialization_LoadFromText(
                { source.data(), source.size() },
                Allocator_GetSystem(),
                &output );
        REQUIRE( result.status ==
                 tile_map_serialization_status_t::VALUE_OUT_OF_RANGE );
        REQUIRE( std::string( result.field ) == "side" );
        REQUIRE( result.iElement == 0u );
        RequireFresh( output );
    }
}

TEST_CASE( "Tile-map loading is strict and transactional",
           "[CypherTools][CypherTileEditor][Serialization]" )
{
    SECTION( "unknown root field" )
    {
        std::string source = ValidMapText();
        const usize iClose = source.rfind( '}' );
        REQUIRE( iClose != std::string::npos );
        source.insert( iClose, "  future_data = 1u\n" );

        tile_map_document_t output{};
        const tile_map_serialization_result_t result =
            CypherTileMapSerialization_LoadFromText(
                { source.data(), source.size() },
                Allocator_GetSystem(),
                &output );
        REQUIRE( result.status ==
                 tile_map_serialization_status_t::UNKNOWN_FIELD );
        REQUIRE( std::string( result.field ) == "future_data" );
        RequireFresh( output );
    }

    SECTION( "non-float metric" )
    {
        std::string source = ValidMapText();
        const std::string expected = "cell_size = 2.0";
        const usize iMetric = source.find( expected );
        REQUIRE( iMetric != std::string::npos );
        source.replace( iMetric, expected.size(), "cell_size = 2u" );

        tile_map_document_t output{};
        const tile_map_serialization_result_t result =
            CypherTileMapSerialization_LoadFromText(
                { source.data(), source.size() },
                Allocator_GetSystem(),
                &output );
        REQUIRE( result.status ==
                 tile_map_serialization_status_t::TYPE_MISMATCH );
        REQUIRE( std::string( result.field ) == "cell_size" );
        RequireFresh( output );
    }

    SECTION( "already initialized destination" )
    {
        const std::string source = ValidMapText();
        tile_map_document_t output{};
        REQUIRE(
            CypherTileMapDocument_Init(
                &output,
                Allocator_GetSystem(),
                {} ) == tile_map_document_status_t::OK );
        const unique_id_t previousId = output.mapId;
        const tile_map_serialization_result_t result =
            CypherTileMapSerialization_LoadFromText(
                { source.data(), source.size() },
                Allocator_GetSystem(),
                &output );
        REQUIRE( result.status ==
                 tile_map_serialization_status_t::DESTINATION_NOT_EMPTY );
        REQUIRE( CypherTileMapDocument_IsInitialized( &output ) );
        REQUIRE( UniqueId_Equals( output.mapId, previousId ) );
        CypherTileMapDocument_Shutdown( &output );
    }
}

TEST_CASE( "Tile-map saving preserves prior text when validation fails",
           "[CypherTools][CypherTileEditor][Serialization]" )
{
    tile_map_document_t document{};
    REQUIRE(
        CypherTileMapDocument_Init(
            &document,
            Allocator_GetSystem(),
            { 4u, 4u, 2.0f, 3.0f } ) ==
        tile_map_document_status_t::OK );

    tile_map_cell_t *pCell = CypherTileMapDocument_CellAt(
        &document,
        { 1, 1 } );
    REQUIRE( pCell != nullptr );
    pCell->flags = static_cast<flags16_t>( CYPHER_BIT32( 7 ) );

    text_buffer_t output{};
    REQUIRE( TextBuffer_Init( &output, Allocator_GetSystem() ) );
    REQUIRE( TextBuffer_Assign(
        &output,
        StringView_FromCString( "prior contents" ) ) );

    const tile_map_serialization_result_t result =
        CypherTileMapSerialization_SaveToText( &document, &output );
    REQUIRE( result.status ==
             tile_map_serialization_status_t::INVALID_DOCUMENT );
    REQUIRE( StringView_Equals(
        TextBuffer_View( &output ),
        StringView_FromCString( "prior contents" ) ) );

    CypherTileMapDocument_Shutdown( &document );
}

TEST_CASE( "Tile-map persistence enforces shared authoring invariants",
           "[CypherTools][CypherTileEditor][Serialization][Validation]" )
{
    SECTION( "inactive cells must be canonical" )
    {
        tile_map_document_t document{};
        REQUIRE( CypherTileMapDocument_Init(
                     &document,
                     Allocator_GetSystem(),
                     { 4u, 4u, 2.0f, 3.0f } ) ==
                 tile_map_document_status_t::OK );
        tile_map_cell_t *pCell = CypherTileMapDocument_CellAt(
            &document,
            { 1, 1 } );
        REQUIRE( pCell != nullptr );
        pCell->nMaterialSlot = 9u;

        text_buffer_t output{};
        REQUIRE( TextBuffer_Init( &output, Allocator_GetSystem() ) );
        REQUIRE( TextBuffer_Assign(
            &output,
            StringView_FromCString( "prior contents" ) ) );
        const tile_map_serialization_result_t result =
            CypherTileMapSerialization_SaveToText( &document, &output );
        REQUIRE( result.status ==
                 tile_map_serialization_status_t::INVALID_DOCUMENT );
        REQUIRE( std::string( result.field ) == "cells[]" );
        REQUIRE( result.iElement == 5u );
        REQUIRE( StringView_Equals(
            TextBuffer_View( &output ),
            StringView_FromCString( "prior contents" ) ) );
        CypherTileMapDocument_Shutdown( &document );
    }

    SECTION( "active-cell limit is shared with the document" )
    {
        tile_map_document_t document{};
        REQUIRE( CypherTileMapDocument_Init(
                     &document,
                     Allocator_GetSystem(),
                     { 513u, 512u, 2.0f, 3.0f } ) ==
                 tile_map_document_status_t::OK );
        for ( usize iCell = 0u;
              iCell <= TILE_MAP_MAX_ACTIVE_CELLS;
              ++iCell ) {
            document.cells.pData[iCell] = {
                0,
                1u,
                0u,
                TILE_MAP_CELL_FLAG_FLOOR
            };
        }

        text_buffer_t output{};
        REQUIRE( TextBuffer_Init( &output, Allocator_GetSystem() ) );
        const tile_map_serialization_result_t result =
            CypherTileMapSerialization_SaveToText( &document, &output );
        REQUIRE( result.status ==
                 tile_map_serialization_status_t::LIMIT_EXCEEDED );
        REQUIRE( std::string( result.field ) == "cells" );
        CypherTileMapDocument_Shutdown( &document );
    }

    SECTION( "metrics must fit the default generated thicknesses" )
    {
        std::string source = ValidMapText();
        const std::string expected = "cell_size = 2.0";
        const usize iMetric = source.find( expected );
        REQUIRE( iMetric != std::string::npos );
        source.replace( iMetric, expected.size(), "cell_size = 0.19" );

        tile_map_document_t output{};
        const tile_map_serialization_result_t result =
            CypherTileMapSerialization_LoadFromText(
                { source.data(), source.size() },
                Allocator_GetSystem(),
                &output );
        REQUIRE( result.status ==
                 tile_map_serialization_status_t::VALUE_OUT_OF_RANGE );
        REQUIRE( std::string( result.field ) == "metrics.cell_size" );
        RequireFresh( output );
    }

    SECTION( "textual metric minima are accepted" )
    {
        std::string source = ValidMapText();
        const std::string cellSize = "cell_size = 2.0";
        const std::string levelHeight = "level_height = 3.0";
        const usize iCellSize = source.find( cellSize );
        const usize iLevelHeight = source.find( levelHeight );
        REQUIRE( iCellSize != std::string::npos );
        REQUIRE( iLevelHeight != std::string::npos );
        source.replace( iCellSize, cellSize.size(), "cell_size = 0.20" );
        source.replace(
            source.find( levelHeight ),
            levelHeight.size(),
            "level_height = 0.25" );

        tile_map_document_t output{};
        const tile_map_serialization_result_t result =
            CypherTileMapSerialization_LoadFromText(
                { source.data(), source.size() },
                Allocator_GetSystem(),
                &output );
        REQUIRE( result.status == tile_map_serialization_status_t::OK );
        REQUIRE( output.nCellSize == TILE_MAP_MIN_CELL_SIZE );
        REQUIRE( output.nLevelHeight == TILE_MAP_MIN_LEVEL_HEIGHT );
        CypherTileMapDocument_Shutdown( &output );
    }
}

TEST_CASE( "Checked-in tile-map sample canonicalizes and builds expected boxes",
           "[CypherTools][CypherTileEditor][Serialization][Fixture]" )
{
    const std::filesystem::path repositoryRoot =
        std::filesystem::path( __FILE__ )
            .parent_path()
            .parent_path()
            .parent_path()
            .parent_path();
    const std::filesystem::path mapPath =
        repositoryRoot / "assets/maps/tile_editor_demo.cymap";
    std::ifstream input( mapPath, std::ios::binary );
    REQUIRE( input.good() );
    const std::string source{
        std::istreambuf_iterator<char>{ input },
        std::istreambuf_iterator<char>{}
    };
    REQUIRE_FALSE( source.empty() );

    tile_map_document_t document{};
    const tile_map_serialization_result_t loaded =
        CypherTileMapSerialization_LoadFromText(
            { source.data(), source.size() },
            Allocator_GetSystem(),
            &document );
    INFO( CypherTileMapSerialization_StatusName( loaded.status ) );
    REQUIRE( loaded.status == tile_map_serialization_status_t::OK );

    tile_map_validation_report_t report{};
    REQUIRE( CypherTileMapValidationReport_Init(
                 &report,
                 Allocator_GetSystem() ) ==
             tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_Validate( &document, &report ) ==
             tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapValidationReport_IsValid( &report ) );

    tile_map_geometry_t geometry{};
    REQUIRE( CypherTileMapGeometry_Init(
                 &geometry,
                 Allocator_GetSystem() ) ==
             tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapGeometry_Build(
                 &document,
                 {},
                 &geometry ) == tile_map_document_status_t::OK );
    usize nExpectedFloors = 0u;
    usize nExpectedWalls = 0u;
    constexpr tile_map_grid_coord_t neighborOffsets[]{
        { 0, -1 },
        { 1, 0 },
        { 0, 1 },
        { -1, 0 }
    };
    for ( u32 y = 0u; y < document.nHeight; ++y ) {
        for ( u32 x = 0u; x < document.nWidth; ++x ) {
            const tile_map_grid_coord_t coordinate{
                static_cast<i32>( x ),
                static_cast<i32>( y )
            };
            const tile_map_cell_t *pCell = document.cells.pData +
                static_cast<usize>( y ) * document.nWidth + x;
            if ( ( pCell->flags & TILE_MAP_CELL_FLAG_FLOOR ) == 0u ) {
                continue;
            }
            ++nExpectedFloors;
            for ( const tile_map_grid_coord_t offset : neighborOffsets ) {
                const tile_map_cell_t *pNeighbor =
                    CypherTileMapDocument_CellAt(
                        &document,
                        { coordinate.x + offset.x,
                          coordinate.y + offset.y } );
                const bool bNeighborHasFloor = pNeighbor != nullptr &&
                    ( pNeighbor->flags & TILE_MAP_CELL_FLAG_FLOOR ) != 0u;
                nExpectedWalls += !bNeighborHasFloor ||
                    pCell->nFloorLevel > pNeighbor->nFloorLevel;
            }
        }
    }
    REQUIRE( CypherTileMapGeometry_CountKind(
                 &geometry,
                 tile_map_geometry_box_kind_t::FLOOR ) == nExpectedFloors );
    REQUIRE( CypherTileMapGeometry_CountKind(
                 &geometry,
                 tile_map_geometry_box_kind_t::WALL ) == nExpectedWalls );

    text_buffer_t canonical{};
    REQUIRE( TextBuffer_Init( &canonical, Allocator_GetSystem() ) );
    REQUIRE( CypherTileMapSerialization_SaveToText(
                 &document,
                 &canonical ).status == tile_map_serialization_status_t::OK );

    // Authored CYKV accepts comments and other non-semantic formatting, while
    // SaveToText deliberately emits one deterministic representation. Verify
    // that representation is a fixed point instead of requiring an editable
    // sample map to contain no comments.
    tile_map_document_t canonicalDocument{};
    const tile_map_serialization_result_t canonicalLoaded =
        CypherTileMapSerialization_LoadFromText(
            TextBuffer_View( &canonical ),
            Allocator_GetSystem(),
            &canonicalDocument );
    REQUIRE( canonicalLoaded.status == tile_map_serialization_status_t::OK );
    text_buffer_t canonicalAgain{};
    REQUIRE( TextBuffer_Init( &canonicalAgain, Allocator_GetSystem() ) );
    REQUIRE( CypherTileMapSerialization_SaveToText(
                 &canonicalDocument,
                 &canonicalAgain ).status ==
             tile_map_serialization_status_t::OK );
    REQUIRE( StringView_Equals(
        TextBuffer_View( &canonical ),
        TextBuffer_View( &canonicalAgain ) ) );

    TextBuffer_Shutdown( &canonicalAgain );
    CypherTileMapDocument_Shutdown( &canonicalDocument );
    TextBuffer_Shutdown( &canonical );
    CypherTileMapGeometry_Shutdown( &geometry );
    CypherTileMapValidationReport_Shutdown( &report );
    CypherTileMapDocument_Shutdown( &document );
}

TEST_CASE( "Tile-map version 2 preserves staircase orientation and tread count",
           "[CypherTools][TileMap][Serialization][Stairs]" )
{
    tile_map_document_t source{};
    REQUIRE( CypherTileMapDocument_Init( &source, Allocator_GetSystem(),
                 { 4u, 2u, 2.0f, 3.0f } ) == tile_map_document_status_t::OK );
    for ( u8 i = 0u; i < 4u; ++i ) {
        REQUIRE( CypherTileMapDocument_PaintCell( &source, { i, 0 },
                     { -2, 1u, static_cast<u16>( i + 1u ),
                       static_cast<tile_map_cell_shape_t>( i + 1u ),
                       static_cast<u16>( 2u + i * 10u ) } ) == tile_map_document_status_t::OK );
    }
    text_buffer_t text{};
    REQUIRE( TextBuffer_Init( &text, Allocator_GetSystem() ) );
    REQUIRE( CypherTileMapSerialization_SaveToText( &source, &text ).status ==
             tile_map_serialization_status_t::OK );
    tile_map_document_t loaded{};
    REQUIRE( CypherTileMapSerialization_LoadFromText( TextBuffer_View( &text ),
                 Allocator_GetSystem(), &loaded ).status == tile_map_serialization_status_t::OK );
    for ( i32 i = 0; i < 4; ++i ) {
        const auto *cell = CypherTileMapDocument_CellAt( &loaded, { i, 0 } );
        REQUIRE( cell->shape == static_cast<tile_map_cell_shape_t>( i + 1 ) );
        REQUIRE( cell->nStairSteps == 2u + i * 10u );
        REQUIRE( cell->nFloorLevel == -2 );
        REQUIRE( cell->nMaterialSlot == i + 1u );
    }
    text_buffer_t again{};
    REQUIRE( TextBuffer_Init( &again, Allocator_GetSystem() ) );
    REQUIRE( CypherTileMapSerialization_SaveToText( &loaded, &again ).status ==
             tile_map_serialization_status_t::OK );
    REQUIRE( StringView_Equals( TextBuffer_View( &text ), TextBuffer_View( &again ) ) );
    CypherTileMapDocument_Shutdown( &loaded );
    CypherTileMapDocument_Shutdown( &source );
    TextBuffer_Shutdown( &text );
    TextBuffer_Shutdown( &again );
}

TEST_CASE( "Legacy flat maps and omitted version 2 stair defaults remain readable",
           "[CypherTools][TileMap][Serialization][Stairs]" )
{
    for ( const u32 version : { 1u, 2u } ) {
        const auto text = ValidMapText(
            "[{ x=0u y=0u floor_level=0 wall_height_levels=1u material_slot=0u flags=1u }]",
            "[]", "cypher.map", version );
        tile_map_document_t loaded{};
        REQUIRE( CypherTileMapSerialization_LoadFromText( { text.data(), text.size() },
                     Allocator_GetSystem(), &loaded ).status == tile_map_serialization_status_t::OK );
        const auto *cell = CypherTileMapDocument_CellAt( &loaded, { 0, 0 } );
        REQUIRE( cell->shape == tile_map_cell_shape_t::FLAT );
        REQUIRE( cell->nStairSteps == TILE_MAP_DEFAULT_STAIR_STEPS );
        CypherTileMapDocument_Shutdown( &loaded );
    }
    const auto text = ValidMapText(
        "[{ x=0u y=0u floor_level=0 wall_height_levels=1u material_slot=0u flags=1u shape=\"stairs_north\" }]",
        "[]", "cypher.map", 2u );
    tile_map_document_t loaded{};
    REQUIRE( CypherTileMapSerialization_LoadFromText( { text.data(), text.size() },
                 Allocator_GetSystem(), &loaded ).status == tile_map_serialization_status_t::OK );
    REQUIRE( loaded.cells.pData[0].shape == tile_map_cell_shape_t::STAIRS_NORTH );
    REQUIRE( loaded.cells.pData[0].nStairSteps == TILE_MAP_DEFAULT_STAIR_STEPS );
    CypherTileMapDocument_Shutdown( &loaded );
}

TEST_CASE( "Malformed stair fields fail transactionally and remain forbidden in version 1",
           "[CypherTools][TileMap][Serialization][Stairs]" )
{
    for ( const auto &extra : {
              "shape=\"ramp\"", "shape=2u", "stair_steps=0u", "stair_steps=1u",
              "stair_steps=33u", "stair_steps=-1", "stair_steps=\"eight\"" } ) {
        const auto text = ValidMapText(
            "[{ x=0u y=0u floor_level=0 wall_height_levels=1u material_slot=0u flags=1u " +
            std::string( extra ) + " }]", "[]", "cypher.map", 2u );
        tile_map_document_t loaded{};
        CAPTURE( extra );
        REQUIRE( CypherTileMapSerialization_LoadFromText( { text.data(), text.size() },
                     Allocator_GetSystem(), &loaded ).status != tile_map_serialization_status_t::OK );
        RequireFresh( loaded );
    }
    const auto text = ValidMapText(
        "[{ x=0u y=0u floor_level=0 wall_height_levels=1u material_slot=0u flags=1u shape=\"stairs_north\" }]" );
    tile_map_document_t loaded{};
    REQUIRE( CypherTileMapSerialization_LoadFromText( { text.data(), text.size() },
                 Allocator_GetSystem(), &loaded ).status == tile_map_serialization_status_t::UNKNOWN_FIELD );
    RequireFresh( loaded );
}
