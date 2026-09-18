//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileMapCore_Tests.cpp
//  Purpose: Tests the Qt-independent tile-map document and box builder.
//
//  History:
//  - Created by Karlo Siric on 2026-09-16
//
//////////////////////////////////////////////////////////////////////////

#include "CypherTileMapDocument.h"
#include "CypherTileMapGeometry.h"

#include "CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

using namespace cypher::common;
using namespace cypher::tools::tile_editor;

namespace
{

struct failing_allocator_state_t {
    usize nSuccessfulAllocationsRemaining{ CY_USIZE_MAX };
    usize nOutstandingAllocations{ 0u };
};

void *FailingAllocate(
    void *pUserData,
    usize cbSize,
    usize nAlignment ) noexcept
{
    auto &state = *static_cast<failing_allocator_state_t *>( pUserData );
    if ( state.nSuccessfulAllocationsRemaining == 0u ) {
        return nullptr;
    }
    if ( state.nSuccessfulAllocationsRemaining != CY_USIZE_MAX ) {
        --state.nSuccessfulAllocationsRemaining;
    }
    void *pMemory = Allocator_Allocate(
        Allocator_GetSystem(),
        cbSize,
        nAlignment );
    if ( pMemory != nullptr ) {
        ++state.nOutstandingAllocations;
    }
    return pMemory;
}

void FailingFree(
    void *pUserData,
    void *pMemory,
    usize cbSize,
    usize nAlignment ) noexcept
{
    auto &state = *static_cast<failing_allocator_state_t *>( pUserData );
    if ( pMemory != nullptr ) {
        --state.nOutstandingAllocations;
    }
    Allocator_Free(
        Allocator_GetSystem(),
        pMemory,
        cbSize,
        nAlignment );
}

allocator_t FailingAllocator( failing_allocator_state_t &state ) noexcept
{
    return {
        &FailingAllocate,
        nullptr,
        &FailingFree,
        &state
    };
}

bool HasDiagnostic(
    const tile_map_validation_report_t &report,
    tile_map_validation_code_t code ) noexcept
{
    for ( usize iDiagnostic = 0u;
          iDiagnostic < Vector_Count( &report.diagnostics );
          ++iDiagnostic ) {
        if ( report.diagnostics.pData[iDiagnostic].code == code ) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE( "Tile-map document owns a safe dense row-major grid",
           "[CypherTools][TileMap][Document]" )
{
    tile_map_document_t document{};
    REQUIRE( CypherTileMapDocument_Init(
                 &document,
                 Allocator_GetSystem(),
                 { 64u, 64u, 2.0f, 3.0f } ) ==
             tile_map_document_status_t::OK );
    REQUIRE( Vector_Count( &document.cells ) == 4096u );
    REQUIRE( CypherTileMapDocument_CellAt( &document, { 0, 0 } ) ==
             document.cells.pData );
    REQUIRE( CypherTileMapDocument_CellAt( &document, { 63, 63 } ) ==
             document.cells.pData + 4095u );
    REQUIRE( CypherTileMapDocument_CellAt( &document, { -1, 0 } ) == nullptr );
    REQUIRE( CypherTileMapDocument_CellAt( &document, { 64, 0 } ) == nullptr );
    REQUIRE( CypherTileMapDocument_IsDirty( &document ) );
    CypherTileMapDocument_MarkSaved( &document );
    REQUIRE_FALSE( CypherTileMapDocument_IsDirty( &document ) );
    CypherTileMapDocument_Shutdown( &document );
}

TEST_CASE( "Tile-map grouped edits undo, redo, cancel, and branch atomically",
           "[CypherTools][TileMap][History]" )
{
    tile_map_document_t document{};
    REQUIRE( CypherTileMapDocument_Init(
                 &document,
                 Allocator_GetSystem(),
                 { 8u, 8u, 2.0f, 3.0f } ) ==
             tile_map_document_status_t::OK );
    CypherTileMapDocument_MarkSaved( &document );

    REQUIRE( CypherTileMapDocument_BeginEditGroup(
                 &document,
                 StringView_FromCString( "Room stamp" ) ) ==
             tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_PaintRect(
                 &document,
                 { 1, 1, 3u, 2u },
                 {} ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_PaintCell(
                 &document,
                 { 4, 2 },
                 {} ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_CommitEditGroup( &document ) ==
             tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_HistoryCount( &document ) == 1u );
    REQUIRE( CypherTileMapDocument_CellHasFloor( &document, { 1, 1 } ) );
    REQUIRE( CypherTileMapDocument_CellHasFloor( &document, { 4, 2 } ) );

    REQUIRE( CypherTileMapDocument_Undo( &document ) ==
             tile_map_document_status_t::OK );
    REQUIRE_FALSE( CypherTileMapDocument_CellHasFloor( &document, { 1, 1 } ) );
    REQUIRE_FALSE( CypherTileMapDocument_CellHasFloor( &document, { 4, 2 } ) );
    REQUIRE_FALSE( CypherTileMapDocument_IsDirty( &document ) );

    REQUIRE( CypherTileMapDocument_Redo( &document ) ==
             tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_CellHasFloor( &document, { 1, 1 } ) );
    REQUIRE( CypherTileMapDocument_BeginEditGroup(
                 &document,
                 StringView_FromCString( "Cancelled" ) ) ==
             tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_EraseRect(
                 &document,
                 { 1, 1, 3u, 2u } ) ==
             tile_map_document_status_t::OK );
    CypherTileMapDocument_CancelEditGroup( &document );
    REQUIRE( CypherTileMapDocument_CellHasFloor( &document, { 1, 1 } ) );
    CypherTileMapDocument_Shutdown( &document );
}

TEST_CASE( "Tile-map grouped edits collapse net-zero cell changes",
           "[CypherTools][TileMap][History][Coalescing]" )
{
    tile_map_document_t document{};
    REQUIRE( CypherTileMapDocument_Init(
                 &document,
                 Allocator_GetSystem(),
                 { 4u, 4u, 2.0f, 3.0f } ) ==
             tile_map_document_status_t::OK );
    CypherTileMapDocument_MarkSaved( &document );
    const u64 nSavedRevision = document.nSavedRevision;

    REQUIRE( CypherTileMapDocument_BeginEditGroup(
                 &document,
                 StringView_FromCString( "Net zero" ) ) ==
             tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_PaintCell(
                 &document,
                 { 1, 1 },
                 { 0, 1u, 3u } ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_EraseCell(
                 &document,
                 { 1, 1 } ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_CommitEditGroup( &document ) ==
             tile_map_document_status_t::OK );

    REQUIRE_FALSE( CypherTileMapDocument_CellHasFloor(
        &document,
        { 1, 1 } ) );
    REQUIRE( CypherTileMapDocument_HistoryCount( &document ) == 0u );
    REQUIRE_FALSE( CypherTileMapDocument_CanUndo( &document ) );
    REQUIRE_FALSE( CypherTileMapDocument_IsDirty( &document ) );
    REQUIRE( document.nCurrentRevision == nSavedRevision );

    CypherTileMapDocument_Shutdown( &document );
}

TEST_CASE( "Tile-map history exposes bounded read-only stack information",
           "[CypherTools][TileMap][History][Introspection]" )
{
    tile_map_history_info_t history{ 99u, 99u, 99u, CY_TRUE };
    tile_map_history_entry_info_t entry{
        StringView_FromCString( "stale" ), 99u, 99u, 99u, 99u };
    CHECK_FALSE( CypherTileMapDocument_HistoryInfo( nullptr, &history ) );
    CHECK( history.nEntryCount == 0u );
    CHECK_FALSE( CypherTileMapDocument_HistoryEntryInfo( nullptr, 0u, &entry ) );
    CHECK( entry.label.cchLength == 0u );
    CHECK_FALSE( CypherTileMapDocument_HistoryInfo( nullptr, nullptr ) );
    CHECK_FALSE( CypherTileMapDocument_HistoryEntryInfo( nullptr, 0u, nullptr ) );

    tile_map_document_t document{};
    REQUIRE( CypherTileMapDocument_Init(
                 &document,
                 Allocator_GetSystem(),
                 { 8u, 8u, 2.0f, 3.0f } ) ==
             tile_map_document_status_t::OK );
    CypherTileMapDocument_MarkSaved( &document );
    const u64 savedRevision = document.nSavedRevision;

    REQUIRE( CypherTileMapDocument_HistoryInfo( &document, &history ) );
    CHECK( history.nEntryCount == 0u );
    CHECK( history.iCursor == 0u );
    CHECK( history.cbStoredChanges == 0u );
    CHECK_FALSE( history.bEditGroupOpen );
    CHECK_FALSE( CypherTileMapDocument_HistoryEntryInfo( &document, 0u, &entry ) );

    REQUIRE( CypherTileMapDocument_BeginEditGroup(
                 &document,
                 StringView_FromCString( "Paint test room" ) ) ==
             tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_PaintRect(
                 &document,
                 { 1, 1, 2u, 2u },
                 {} ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_HistoryInfo( &document, &history ) );
    CHECK( history.nEntryCount == 0u );
    CHECK( history.iCursor == 0u );
    CHECK( history.bEditGroupOpen );
    REQUIRE( CypherTileMapDocument_CommitEditGroup( &document ) ==
             tile_map_document_status_t::OK );

    REQUIRE( CypherTileMapDocument_HistoryInfo( &document, &history ) );
    CHECK( history.nEntryCount == 1u );
    CHECK( history.iCursor == 1u );
    CHECK( history.cbStoredChanges > 0u );
    CHECK_FALSE( history.bEditGroupOpen );
    REQUIRE( CypherTileMapDocument_HistoryEntryInfo( &document, 0u, &entry ) );
    CHECK( StringView_Equals(
        entry.label,
        StringView_FromCString( "Paint test room" ) ) );
    CHECK( entry.nBeforeRevision == savedRevision );
    CHECK( entry.nAfterRevision == document.nCurrentRevision );
    CHECK( entry.nAffectedElementCount == 4u );
    CHECK( entry.cbStoredChanges == history.cbStoredChanges );

    REQUIRE( CypherTileMapDocument_Undo( &document ) ==
             tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_HistoryInfo( &document, &history ) );
    CHECK( history.nEntryCount == 1u );
    CHECK( history.iCursor == 0u );
    CHECK( document.nCurrentRevision == savedRevision );

    REQUIRE( CypherTileMapDocument_BeginEditGroup(
                 &document,
                 StringView_FromCString( "Branch paint" ) ) ==
             tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_PaintCell(
                 &document,
                 { 6, 6 },
                 {} ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_CommitEditGroup( &document ) ==
             tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_HistoryInfo( &document, &history ) );
    CHECK( history.nEntryCount == 1u );
    CHECK( history.iCursor == 1u );
    REQUIRE( CypherTileMapDocument_HistoryEntryInfo( &document, 0u, &entry ) );
    CHECK( StringView_Equals(
        entry.label,
        StringView_FromCString( "Branch paint" ) ) );
    CHECK( entry.nBeforeRevision == savedRevision );
    CHECK( entry.nAffectedElementCount == 1u );

    entry = { StringView_FromCString( "stale" ), 99u, 99u, 99u, 99u };
    CHECK_FALSE( CypherTileMapDocument_HistoryEntryInfo( &document, 1u, &entry ) );
    CHECK( entry.label.cchLength == 0u );
    CHECK( entry.nBeforeRevision == 0u );
    CypherTileMapDocument_Shutdown( &document );
}

TEST_CASE( "Tile-map door markers are queryable and undoable",
           "[CypherTools][TileMap][Door][History]" )
{
    tile_map_document_t document{};
    REQUIRE( CypherTileMapDocument_Init(
                 &document,
                 Allocator_GetSystem(),
                 { 4u, 4u, 2.0f, 3.0f } ) ==
             tile_map_document_status_t::OK );

    unique_id_t doorId{};
    REQUIRE( CypherTileMapDocument_PlaceDoor(
                 &document,
                 { 1, 2 },
                 tile_map_marker_side_t::EAST,
                 &doorId ) == tile_map_document_status_t::OK );
    REQUIRE( UniqueId_IsValid( doorId ) );
    REQUIRE( CypherTileMapDocument_HistoryCount( &document ) == 1u );
    const tile_map_marker_t *pDoor = CypherTileMapDocument_DoorAt(
        &document,
        { 1, 2 },
        tile_map_marker_side_t::EAST );
    REQUIRE( pDoor != nullptr );
    REQUIRE( UniqueId_Equals( pDoor->id, doorId ) );
    REQUIRE( CypherTileMapDocument_DoorById( &document, doorId ) == pDoor );

    unique_id_t duplicateId{};
    REQUIRE( CypherTileMapDocument_PlaceDoor(
                 &document,
                 { 1, 2 },
                 tile_map_marker_side_t::EAST,
                 &duplicateId ) == tile_map_document_status_t::OK );
    REQUIRE( UniqueId_Equals( duplicateId, doorId ) );
    REQUIRE( CypherTileMapDocument_HistoryCount( &document ) == 1u );

    REQUIRE( CypherTileMapDocument_Undo( &document ) ==
             tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_DoorById( &document, doorId ) == nullptr );
    REQUIRE( CypherTileMapDocument_Redo( &document ) ==
             tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_DoorById( &document, doorId ) != nullptr );

    REQUIRE( CypherTileMapDocument_RemoveDoorAt(
                 &document,
                 { 1, 2 },
                 tile_map_marker_side_t::EAST ) ==
             tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_DoorById( &document, doorId ) == nullptr );
    REQUIRE( CypherTileMapDocument_Undo( &document ) ==
             tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_DoorById( &document, doorId ) != nullptr );

    REQUIRE( CypherTileMapDocument_PlaceDoor(
                 &document,
                 { 1, 1 },
                 tile_map_marker_side_t::NONE,
                 nullptr ) ==
             tile_map_document_status_t::INVALID_MARKER_SIDE );
    REQUIRE( CypherTileMapDocument_PlaceDoor(
                 &document,
                 { -1, 1 },
                 tile_map_marker_side_t::WEST,
                 nullptr ) == tile_map_document_status_t::OUT_OF_BOUNDS );

    CypherTileMapDocument_Shutdown( &document );
}

TEST_CASE( "Tile-map validation diagnoses spawn requirements",
           "[CypherTools][TileMap][Validation]" )
{
    tile_map_document_t document{};
    tile_map_validation_report_t report{};
    REQUIRE( CypherTileMapDocument_Init(
                 &document,
                 Allocator_GetSystem(),
                 { 4u, 4u, 2.0f, 3.0f } ) ==
             tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapValidationReport_Init(
                 &report,
                 Allocator_GetSystem() ) ==
             tile_map_document_status_t::OK );

    REQUIRE( CypherTileMapDocument_Validate( &document, &report ) ==
             tile_map_document_status_t::OK );
    REQUIRE( Vector_Count( &report.diagnostics ) == 1u );
    REQUIRE( report.diagnostics.pData[0].code ==
             tile_map_validation_code_t::MISSING_PLAYER_SPAWN );

    REQUIRE( CypherTileMapDocument_PlacePlayerSpawn(
                 &document,
                 { 1, 1 },
                 90.0f ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_Validate( &document, &report ) ==
             tile_map_document_status_t::OK );
    REQUIRE( Vector_Count( &report.diagnostics ) == 1u );
    REQUIRE( report.diagnostics.pData[0].code ==
             tile_map_validation_code_t::PLAYER_SPAWN_OUTSIDE_FLOOR );

    REQUIRE( CypherTileMapDocument_PaintCell(
                 &document,
                 { 1, 1 },
                 {} ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_Validate( &document, &report ) ==
             tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapValidationReport_IsValid( &report ) );

    tile_map_marker_t duplicate{};
    REQUIRE( UniqueId_CreateRandom( &duplicate.id ) );
    duplicate.kind = tile_map_marker_kind_t::PLAYER_SPAWN;
    duplicate.cell = { 8, 8 };
    REQUIRE( Vector_PushBack( &document.markers, duplicate ) );
    REQUIRE( CypherTileMapDocument_Validate( &document, &report ) ==
             tile_map_document_status_t::OK );
    REQUIRE( Vector_Count( &report.diagnostics ) == 2u );
    REQUIRE( report.diagnostics.pData[0].code ==
             tile_map_validation_code_t::DUPLICATE_PLAYER_SPAWN );
    REQUIRE( report.diagnostics.pData[1].code ==
             tile_map_validation_code_t::PLAYER_SPAWN_OUT_OF_BOUNDS );

    REQUIRE( CypherTileMapDocument_PlacePlayerSpawn(
                 &document,
                 { 1, 1 },
                 180.0f ) == tile_map_document_status_t::OK );
    REQUIRE( Vector_Count( &document.markers ) == 1u );
    REQUIRE( CypherTileMapDocument_Validate( &document, &report ) ==
             tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapValidationReport_IsValid( &report ) );

    CypherTileMapValidationReport_Shutdown( &report );
    CypherTileMapDocument_Shutdown( &document );
}

TEST_CASE( "Tile-map validation diagnoses malformed door markers",
           "[CypherTools][TileMap][Door][Validation]" )
{
    tile_map_document_t document{};
    tile_map_validation_report_t report{};
    REQUIRE( CypherTileMapDocument_Init(
                 &document,
                 Allocator_GetSystem(),
                 { 4u, 4u, 2.0f, 3.0f } ) ==
             tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapValidationReport_Init(
                 &report,
                 Allocator_GetSystem() ) ==
             tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_PaintCell(
                 &document,
                 { 1, 1 },
                 {} ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_PlacePlayerSpawn(
                 &document,
                 { 1, 1 },
                 0.0f ) == tile_map_document_status_t::OK );
    unique_id_t doorId{};
    REQUIRE( CypherTileMapDocument_PlaceDoor(
                 &document,
                 { 1, 1 },
                 tile_map_marker_side_t::EAST,
                 &doorId ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_Validate( &document, &report ) ==
             tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapValidationReport_IsValid( &report ) );

    tile_map_marker_t *pDoor = nullptr;
    for ( usize iMarker = 0u;
          iMarker < Vector_Count( &document.markers );
          ++iMarker ) {
        if ( document.markers.pData[iMarker].kind ==
             tile_map_marker_kind_t::DOOR ) {
            pDoor = document.markers.pData + iMarker;
            break;
        }
    }
    REQUIRE( pDoor != nullptr );

    pDoor->id = CY_UNIQUE_ID_INVALID;
    REQUIRE( CypherTileMapDocument_Validate( &document, &report ) ==
             tile_map_document_status_t::OK );
    REQUIRE( HasDiagnostic(
        report,
        tile_map_validation_code_t::INVALID_MARKER_ID ) );
    pDoor->id = doorId;

    pDoor->side = tile_map_marker_side_t::NONE;
    REQUIRE( CypherTileMapDocument_Validate( &document, &report ) ==
             tile_map_document_status_t::OK );
    REQUIRE( HasDiagnostic(
        report,
        tile_map_validation_code_t::DOOR_INVALID_SIDE ) );
    pDoor->side = tile_map_marker_side_t::EAST;

    pDoor->cell = { 8, 8 };
    REQUIRE( CypherTileMapDocument_Validate( &document, &report ) ==
             tile_map_document_status_t::OK );
    REQUIRE( HasDiagnostic(
        report,
        tile_map_validation_code_t::DOOR_OUT_OF_BOUNDS ) );
    pDoor->cell = { 2, 2 };
    REQUIRE( CypherTileMapDocument_Validate( &document, &report ) ==
             tile_map_document_status_t::OK );
    REQUIRE( HasDiagnostic(
        report,
        tile_map_validation_code_t::DOOR_OUTSIDE_FLOOR ) );

    pDoor->cell = { 1, 1 };
    REQUIRE( CypherTileMapDocument_PaintCell(
                 &document,
                 { 2, 1 },
                 {} ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_Validate( &document, &report ) ==
             tile_map_document_status_t::OK );
    REQUIRE( HasDiagnostic(
        report,
        tile_map_validation_code_t::DOOR_NOT_ON_BOUNDARY ) );

    tile_map_marker_t duplicateDoor = *pDoor;
    REQUIRE( Vector_PushBack( &document.markers, duplicateDoor ) );
    REQUIRE( CypherTileMapDocument_Validate( &document, &report ) ==
             tile_map_document_status_t::OK );
    REQUIRE( HasDiagnostic(
        report,
        tile_map_validation_code_t::DUPLICATE_MARKER_ID ) );
    REQUIRE( HasDiagnostic(
        report,
        tile_map_validation_code_t::DUPLICATE_DOOR_EDGE ) );

    CypherTileMapValidationReport_Shutdown( &report );
    CypherTileMapDocument_Shutdown( &document );
}

TEST_CASE( "Tile-map geometry suppresses shared walls and uses Z-up boxes",
           "[CypherTools][TileMap][Geometry]" )
{
    tile_map_document_t document{};
    tile_map_geometry_t geometry{};
    REQUIRE( CypherTileMapDocument_Init(
                 &document,
                 Allocator_GetSystem(),
                 { 4u, 4u, 2.0f, 3.0f } ) ==
             tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapGeometry_Init(
                 &geometry,
                 Allocator_GetSystem() ) ==
             tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_PaintRect(
                 &document,
                 { 1, 1, 2u, 1u },
                 {} ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapGeometry_Build(
                 &document,
                 {},
                 &geometry ) == tile_map_document_status_t::OK );

    REQUIRE( CypherTileMapGeometry_CountKind(
                 &geometry,
                 tile_map_geometry_box_kind_t::FLOOR ) == 2u );
    REQUIRE( CypherTileMapGeometry_CountKind(
                 &geometry,
                 tile_map_geometry_box_kind_t::WALL ) == 6u );
    REQUIRE( Vector_Count( &geometry.boxes ) == 8u );
    REQUIRE( geometry.boxes.pData[0].centerX == 3.0f );
    REQUIRE( geometry.boxes.pData[0].centerY == 3.0f );
    REQUIRE( geometry.boxes.pData[0].centerZ < 0.0f );
    REQUIRE( geometry.boxes.pData[0].halfExtentZ == 0.125f );
    REQUIRE( geometry.boundsMaxZ == 3.0f );

    const usize nOldBoxCount = Vector_Count( &geometry.boxes );
    REQUIRE( CypherTileMapGeometry_Build(
                 &document,
                 { -1.0f, 0.2f },
                 &geometry ) == tile_map_document_status_t::INVALID_METRICS );
    REQUIRE( Vector_Count( &geometry.boxes ) == nOldBoxCount );

    CypherTileMapGeometry_Shutdown( &geometry );
    CypherTileMapDocument_Shutdown( &document );
}

TEST_CASE( "Tile-map geometry replaces a door edge wall with a door leaf",
           "[CypherTools][TileMap][Door][Geometry]" )
{
    tile_map_document_t document{};
    tile_map_geometry_t geometry{};
    REQUIRE( CypherTileMapDocument_Init(
                 &document,
                 Allocator_GetSystem(),
                 { 4u, 4u, 2.0f, 3.0f } ) ==
             tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapGeometry_Init(
                 &geometry,
                 Allocator_GetSystem() ) ==
             tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_PaintCell(
                 &document,
                 { 1, 1 },
                 { 0, 1u, 7u } ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_PlaceDoor(
                 &document,
                 { 1, 1 },
                 tile_map_marker_side_t::EAST,
                 nullptr ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapGeometry_Build(
                 &document,
                 {},
                 &geometry ) == tile_map_document_status_t::OK );

    REQUIRE( CypherTileMapGeometry_CountKind(
                 &geometry,
                 tile_map_geometry_box_kind_t::FLOOR ) == 1u );
    REQUIRE( CypherTileMapGeometry_CountKind(
                 &geometry,
                 tile_map_geometry_box_kind_t::WALL ) == 3u );
    REQUIRE( CypherTileMapGeometry_CountKind(
                 &geometry,
                 tile_map_geometry_box_kind_t::DOOR ) == 1u );

    const tile_map_geometry_box_t *pDoorBox = nullptr;
    for ( usize iBox = 0u;
          iBox < Vector_Count( &geometry.boxes );
          ++iBox ) {
        const tile_map_geometry_box_t &box = geometry.boxes.pData[iBox];
        REQUIRE_FALSE((
            box.kind == tile_map_geometry_box_kind_t::WALL &&
            box.side == tile_map_geometry_side_t::EAST ));
        if ( box.kind == tile_map_geometry_box_kind_t::DOOR ) {
            pDoorBox = geometry.boxes.pData + iBox;
        }
    }
    REQUIRE( pDoorBox != nullptr );
    REQUIRE( pDoorBox->side == tile_map_geometry_side_t::EAST );
    REQUIRE( pDoorBox->sourceCell.x == 1 );
    REQUIRE( pDoorBox->sourceCell.y == 1 );
    REQUIRE( pDoorBox->nMaterialSlot == 7u );
    REQUIRE( pDoorBox->halfExtentY < document.nCellSize * 0.5f );
    REQUIRE( pDoorBox->centerZ > 0.0f );
    REQUIRE( pDoorBox->centerZ + pDoorBox->halfExtentZ <
             document.nLevelHeight );

    CypherTileMapGeometry_Shutdown( &geometry );
    CypherTileMapDocument_Shutdown( &document );
}

TEST_CASE( "Tile-map geometry emits one cliff for a shared elevation step",
           "[CypherTools][TileMap][Geometry][Elevation]" )
{
    tile_map_document_t document{};
    tile_map_geometry_t geometry{};
    REQUIRE( CypherTileMapDocument_Init(
                 &document,
                 Allocator_GetSystem(),
                 { 4u, 3u, 2.0f, 3.0f } ) ==
             tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapGeometry_Init(
                 &geometry,
                 Allocator_GetSystem() ) ==
             tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_PaintCell(
                 &document,
                 { 1, 1 },
                 { 0, 1u, 1u } ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_PaintCell(
                 &document,
                 { 2, 1 },
                 { 1, 1u, 2u } ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapGeometry_Build(
                 &document,
                 {},
                 &geometry ) == tile_map_document_status_t::OK );

    REQUIRE( CypherTileMapGeometry_CountKind(
                 &geometry,
                 tile_map_geometry_box_kind_t::FLOOR ) == 2u );
    REQUIRE( CypherTileMapGeometry_CountKind(
                 &geometry,
                 tile_map_geometry_box_kind_t::WALL ) == 7u );

    usize nSharedCliffs = 0u;
    for ( usize iBox = 0u;
          iBox < Vector_Count( &geometry.boxes );
          ++iBox ) {
        const tile_map_geometry_box_t &box = geometry.boxes.pData[iBox];
        const bool bHigherWestSide =
            box.kind == tile_map_geometry_box_kind_t::WALL &&
            box.sourceCell.x == 2 && box.sourceCell.y == 1 &&
            box.side == tile_map_geometry_side_t::WEST;
        const bool bLowerEastSide =
            box.kind == tile_map_geometry_box_kind_t::WALL &&
            box.sourceCell.x == 1 && box.sourceCell.y == 1 &&
            box.side == tile_map_geometry_side_t::EAST;
        if ( bHigherWestSide ) {
            ++nSharedCliffs;
            REQUIRE( box.centerZ == 1.5f );
            REQUIRE( box.halfExtentZ == 1.5f );
            REQUIRE( box.nMaterialSlot == 2u );
        }
        REQUIRE_FALSE( bLowerEastSide );
    }
    REQUIRE( nSharedCliffs == 1u );

    CypherTileMapGeometry_Shutdown( &geometry );
    CypherTileMapDocument_Shutdown( &document );
}

TEST_CASE( "Tile-map geometry rejects non-finite derived boxes transactionally",
           "[CypherTools][TileMap][Geometry][Transactional]" )
{
    failing_allocator_state_t allocatorState{};
    const allocator_t allocator = FailingAllocator( allocatorState );
    tile_map_document_t document{};
    tile_map_geometry_t geometry{};
    REQUIRE( CypherTileMapDocument_Init(
                 &document,
                 &allocator,
                 { 2u, 1u, 2.0f, 3.0f } ) ==
             tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapGeometry_Init( &geometry, &allocator ) ==
             tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_PaintCell(
                 &document,
                 { 1, 0 },
                 {} ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapGeometry_Build(
                 &document,
                 {},
                 &geometry ) == tile_map_document_status_t::OK );

    const usize nOldBoxCount = Vector_Count( &geometry.boxes );
    const f32 oldBoundsMaxX = geometry.boundsMaxX;
    const f32 oldFirstCenterX = geometry.boxes.pData[0].centerX;
    const usize nOutstandingBefore = allocatorState.nOutstandingAllocations;

    document.nCellSize = CY_F32_MAX;
    REQUIRE( CypherTileMapGeometry_Build(
                 &document,
                 {},
                 &geometry ) == tile_map_document_status_t::INVALID_METRICS );
    REQUIRE( Vector_Count( &geometry.boxes ) == nOldBoxCount );
    REQUIRE( geometry.boundsMaxX == oldBoundsMaxX );
    REQUIRE( geometry.boxes.pData[0].centerX == oldFirstCenterX );
    REQUIRE( allocatorState.nOutstandingAllocations == nOutstandingBefore );

    CypherTileMapGeometry_Shutdown( &geometry );
    CypherTileMapDocument_Shutdown( &document );
    REQUIRE( allocatorState.nOutstandingAllocations == 0u );
}

TEST_CASE( "Tile-map metrics cover the default generated thicknesses",
           "[CypherTools][TileMap][Metrics]" )
{
    tile_map_document_t document{};
    REQUIRE( CypherTileMapDocument_Init(
                 &document,
                 Allocator_GetSystem(),
                 { 4u, 4u, TILE_MAP_MIN_CELL_SIZE * 0.5f, 3.0f } ) ==
             tile_map_document_status_t::INVALID_METRICS );
    REQUIRE( CypherTileMapDocument_Init(
                 &document,
                 Allocator_GetSystem(),
                 { 4u, 4u, 2.0f, TILE_MAP_MIN_LEVEL_HEIGHT * 0.5f } ) ==
             tile_map_document_status_t::INVALID_METRICS );
    REQUIRE( CypherTileMapDocument_Init(
                 &document,
                 Allocator_GetSystem(),
                 {
                     4u,
                     4u,
                     TILE_MAP_MIN_CELL_SIZE,
                     TILE_MAP_MIN_LEVEL_HEIGHT
                 } ) == tile_map_document_status_t::OK );

    tile_map_geometry_t geometry{};
    REQUIRE( CypherTileMapGeometry_Init(
                 &geometry,
                 Allocator_GetSystem() ) ==
             tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_PaintCell(
                 &document,
                 { 0, 0 },
                 {} ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapGeometry_Build(
                 &document,
                 {},
                 &geometry ) == tile_map_document_status_t::OK );

    CypherTileMapGeometry_Shutdown( &geometry );
    CypherTileMapDocument_Shutdown( &document );
}

TEST_CASE( "Tile-map validation enforces authoring and empty-cell invariants",
           "[CypherTools][TileMap][Validation][Limits]" )
{
    tile_map_document_t document{};
    tile_map_validation_report_t report{};
    REQUIRE( CypherTileMapDocument_Init(
                 &document,
                 Allocator_GetSystem(),
                 { 513u, 512u, 2.0f, 3.0f } ) ==
             tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapValidationReport_Init(
                 &report,
                 Allocator_GetSystem() ) ==
             tile_map_document_status_t::OK );

    for ( usize iCell = 0u;
          iCell < TILE_MAP_MAX_ACTIVE_CELLS;
          ++iCell ) {
        document.cells.pData[iCell] = {
            0,
            1u,
            0u,
            TILE_MAP_CELL_FLAG_FLOOR
        };
    }
    REQUIRE( CypherTileMapDocument_Validate( &document, &report ) ==
             tile_map_document_status_t::OK );
    REQUIRE_FALSE( HasDiagnostic(
        report,
        tile_map_validation_code_t::ACTIVE_CELL_LIMIT_EXCEEDED ) );

    const usize iFirstDisallowed = TILE_MAP_MAX_ACTIVE_CELLS;
    const tile_map_grid_coord_t firstDisallowed{
        static_cast<i32>( iFirstDisallowed % document.nWidth ),
        static_cast<i32>( iFirstDisallowed / document.nWidth )
    };
    const usize nHistoryBefore = CypherTileMapDocument_HistoryCount( &document );
    REQUIRE( CypherTileMapDocument_PaintCell(
                 &document,
                 firstDisallowed,
                 {} ) ==
             tile_map_document_status_t::ACTIVE_CELL_LIMIT_REACHED );
    REQUIRE_FALSE( CypherTileMapDocument_CellHasFloor(
        &document,
        firstDisallowed ) );
    REQUIRE( CypherTileMapDocument_HistoryCount( &document ) ==
             nHistoryBefore );

    document.cells.pData[iFirstDisallowed].flags = TILE_MAP_CELL_FLAG_FLOOR;
    REQUIRE( CypherTileMapDocument_Validate( &document, &report ) ==
             tile_map_document_status_t::OK );
    REQUIRE( HasDiagnostic(
        report,
        tile_map_validation_code_t::ACTIVE_CELL_LIMIT_EXCEEDED ) );

    document.cells.pData[iFirstDisallowed] = {};
    const usize iNoncanonical = iFirstDisallowed + 1u;
    document.cells.pData[iNoncanonical].nMaterialSlot = 7u;
    REQUIRE_FALSE( CypherTileMapCell_IsCanonicalEmpty(
        document.cells.pData[iNoncanonical] ) );
    REQUIRE( CypherTileMapDocument_Validate( &document, &report ) ==
             tile_map_document_status_t::OK );
    REQUIRE( HasDiagnostic(
        report,
        tile_map_validation_code_t::NONCANONICAL_EMPTY_CELL ) );

    CypherTileMapValidationReport_Shutdown( &report );
    CypherTileMapDocument_Shutdown( &document );
}

TEST_CASE( "Tile-map edits roll back when history publication runs out of memory",
           "[CypherTools][TileMap][Transactional]" )
{
    failing_allocator_state_t allocatorState{};
    const allocator_t allocator = FailingAllocator( allocatorState );
    tile_map_document_t document{};
    REQUIRE( CypherTileMapDocument_Init(
                 &document,
                 &allocator,
                 { 4u, 4u, 2.0f, 3.0f } ) ==
             tile_map_document_status_t::OK );
    const u64 nRevisionBefore = document.nCurrentRevision;

    // The temporary one-cell batch may allocate. The subsequent exact history
    // copy fails after the live cell was tentatively changed, exercising the
    // automatic group's rollback path.
    allocatorState.nSuccessfulAllocationsRemaining = 1u;
    REQUIRE( CypherTileMapDocument_PaintCell(
                 &document,
                 { 1, 1 },
                 {} ) == tile_map_document_status_t::ALLOCATION_FAILED );
    REQUIRE_FALSE( CypherTileMapDocument_CellHasFloor( &document, { 1, 1 } ) );
    REQUIRE( document.nCurrentRevision == nRevisionBefore );
    REQUIRE( CypherTileMapDocument_HistoryCount( &document ) == 0u );
    REQUIRE_FALSE( CypherTileMapDocument_IsEditGroupOpen( &document ) );

    allocatorState.nSuccessfulAllocationsRemaining = CY_USIZE_MAX;
    CypherTileMapDocument_Shutdown( &document );
    REQUIRE( allocatorState.nOutstandingAllocations == 0u );
}

TEST_CASE( "Tile-map initialization failure leaves a canonical empty document",
           "[CypherTools][TileMap][Transactional][Init]" )
{
    failing_allocator_state_t allocatorState{};
    allocatorState.nSuccessfulAllocationsRemaining = 3u;
    const allocator_t allocator = FailingAllocator( allocatorState );
    tile_map_document_t document{};

    // Cells, marker capacity, and history state succeed. Initializing the
    // history's live-group storage fails and must release all three owners.
    REQUIRE( CypherTileMapDocument_Init(
                 &document,
                 &allocator,
                 { 4u, 4u, 2.0f, 3.0f } ) ==
             tile_map_document_status_t::ALLOCATION_FAILED );
    REQUIRE_FALSE( CypherTileMapDocument_IsInitialized( &document ) );
    REQUIRE( document.pAllocator == nullptr );
    REQUIRE( document.cells.pData == nullptr );
    REQUIRE( document.markers.pData == nullptr );
    REQUIRE( document.pHistoryState == nullptr );
    REQUIRE( allocatorState.nOutstandingAllocations == 0u );

    CypherTileMapDocument_Shutdown( &document );
    REQUIRE( allocatorState.nOutstandingAllocations == 0u );
}

TEST_CASE( "Tile-map history evicts complete oldest groups at its bound",
           "[CypherTools][TileMap][History][Bounded]" )
{
    tile_map_document_t document{};
    REQUIRE( CypherTileMapDocument_Init(
                 &document,
                 Allocator_GetSystem(),
                 { 16u, 16u, 2.0f, 3.0f } ) ==
             tile_map_document_status_t::OK );

    for ( usize iCell = 0u;
          iCell < TILE_MAP_HISTORY_MAX_ENTRIES + 2u;
          ++iCell ) {
        const tile_map_grid_coord_t coordinate{
            static_cast<i32>( iCell % 16u ),
            static_cast<i32>( iCell / 16u )
        };
        REQUIRE( CypherTileMapDocument_PaintCell(
                     &document,
                     coordinate,
                     {} ) == tile_map_document_status_t::OK );
    }
    REQUIRE( CypherTileMapDocument_HistoryCount( &document ) ==
             TILE_MAP_HISTORY_MAX_ENTRIES );

    usize nUndone = 0u;
    while ( CypherTileMapDocument_CanUndo( &document ) ) {
        REQUIRE( CypherTileMapDocument_Undo( &document ) ==
                 tile_map_document_status_t::OK );
        ++nUndone;
    }
    REQUIRE( nUndone == TILE_MAP_HISTORY_MAX_ENTRIES );
    REQUIRE( CypherTileMapDocument_CellHasFloor( &document, { 0, 0 } ) );
    REQUIRE( CypherTileMapDocument_CellHasFloor( &document, { 1, 0 } ) );
    REQUIRE_FALSE( CypherTileMapDocument_CellHasFloor( &document, { 2, 0 } ) );
    CypherTileMapDocument_Shutdown( &document );
}

TEST_CASE( "Stair shapes generate bounded solid steps toward each named edge",
           "[CypherTools][TileMap][Geometry][Stairs]" )
{
    constexpr tile_map_cell_shape_t shapes[]{
        tile_map_cell_shape_t::STAIRS_NORTH, tile_map_cell_shape_t::STAIRS_EAST,
        tile_map_cell_shape_t::STAIRS_SOUTH, tile_map_cell_shape_t::STAIRS_WEST
    };
    for ( const auto shape : shapes ) {
        CAPTURE( CypherTileMapCellShape_Name( shape ) );
        tile_map_document_t document{};
        tile_map_geometry_t geometry{};
        REQUIRE( CypherTileMapDocument_Init( &document, Allocator_GetSystem(),
                     { 4u, 4u, 4.0f, 3.0f } ) == tile_map_document_status_t::OK );
        REQUIRE( CypherTileMapGeometry_Init( &geometry, Allocator_GetSystem() ) ==
                 tile_map_document_status_t::OK );
        REQUIRE( CypherTileMapDocument_PaintCell( &document, { 1, 1 },
                     { -1, 1u, 7u, shape, 8u } ) == tile_map_document_status_t::OK );
        REQUIRE( CypherTileMapGeometry_Build( &document, {}, &geometry ) ==
                 tile_map_document_status_t::OK );
        REQUIRE( geometry.boxes.nCount == 8u );
        REQUIRE( CypherTileMapGeometry_CountKind( &geometry,
                     tile_map_geometry_box_kind_t::STAIR ) == 8u );
        REQUIRE( geometry.boundsMinX == Catch::Approx( 4.0f ) );
        REQUIRE( geometry.boundsMinY == Catch::Approx( 4.0f ) );
        REQUIRE( geometry.boundsMaxX == Catch::Approx( 8.0f ) );
        REQUIRE( geometry.boundsMaxY == Catch::Approx( 8.0f ) );
        REQUIRE( geometry.boundsMinZ == Catch::Approx( -3.25f ) );
        REQUIRE( geometry.boundsMaxZ == Catch::Approx( 0.0f ).margin( 0.00001f ) );
        for ( usize i = 0u; i < geometry.boxes.nCount; ++i ) {
            const auto &box = geometry.boxes.pData[i];
            REQUIRE( box.sourceCell.x == 1 );
            REQUIRE( box.sourceCell.y == 1 );
            REQUIRE( box.nMaterialSlot == 7u );
            REQUIRE( box.centerZ + box.halfExtentZ ==
                     Catch::Approx( -3.0f + ( i + 1u ) * 3.0f / 8.0f ).margin( 0.00001f ) );
            REQUIRE( box.centerZ - box.halfExtentZ == Catch::Approx( -3.25f ) );
            const float distance = static_cast<float>( i + 0.5 ) * 0.5f;
            const bool alongX = shape == tile_map_cell_shape_t::STAIRS_EAST ||
                                shape == tile_map_cell_shape_t::STAIRS_WEST;
            const bool reversed = shape == tile_map_cell_shape_t::STAIRS_NORTH ||
                                  shape == tile_map_cell_shape_t::STAIRS_WEST;
            REQUIRE( ( alongX ? box.centerX : box.centerY ) ==
                     Catch::Approx( reversed ? 8.0f - distance : 4.0f + distance ) );
            REQUIRE( ( alongX ? box.halfExtentX : box.halfExtentY ) == 0.25f );
        }
        CypherTileMapGeometry_Shutdown( &geometry );
        CypherTileMapDocument_Shutdown( &document );
    }
}

TEST_CASE( "Stair lower and upper landings remain open for every orientation",
           "[CypherTools][TileMap][Geometry][Stairs]" )
{
    struct orientation_t { tile_map_cell_shape_t shape; i32 dx; i32 dy; };
    constexpr orientation_t orientations[]{
        { tile_map_cell_shape_t::STAIRS_NORTH, 0, -1 },
        { tile_map_cell_shape_t::STAIRS_EAST, 1, 0 },
        { tile_map_cell_shape_t::STAIRS_SOUTH, 0, 1 },
        { tile_map_cell_shape_t::STAIRS_WEST, -1, 0 }
    };
    for ( const auto &orientation : orientations ) {
        tile_map_document_t document{};
        tile_map_geometry_t geometry{};
        REQUIRE( CypherTileMapDocument_Init( &document, Allocator_GetSystem(),
                     { 3u, 3u, 2.0f, 3.0f } ) == tile_map_document_status_t::OK );
        REQUIRE( CypherTileMapGeometry_Init( &geometry, Allocator_GetSystem() ) ==
                 tile_map_document_status_t::OK );
        REQUIRE( CypherTileMapDocument_PaintCell( &document, { 1, 1 },
                     { 0, 1u, 0u, orientation.shape, 4u } ) == tile_map_document_status_t::OK );
        REQUIRE( CypherTileMapDocument_PaintCell( &document,
                     { 1 + orientation.dx, 1 + orientation.dy },
                     { 1, 1u, 0u } ) == tile_map_document_status_t::OK );
        REQUIRE( CypherTileMapDocument_PaintCell( &document,
                     { 1 - orientation.dx, 1 - orientation.dy }, {} ) == tile_map_document_status_t::OK );
        REQUIRE( CypherTileMapGeometry_Build( &document, {}, &geometry ) ==
                 tile_map_document_status_t::OK );
        REQUIRE( CypherTileMapGeometry_CountKind( &geometry,
                     tile_map_geometry_box_kind_t::WALL ) == 6u );
        REQUIRE( CypherTileMapGeometry_CountKind( &geometry,
                     tile_map_geometry_box_kind_t::FLOOR ) == 2u );
        REQUIRE( geometry.boxes.nCount == 12u );
        CypherTileMapGeometry_Shutdown( &geometry );
        CypherTileMapDocument_Shutdown( &document );
    }
}

TEST_CASE( "Stair edits preserve shape and steps in undo and reject invalid values",
           "[CypherTools][TileMap][History][Stairs]" )
{
    tile_map_document_t document{};
    REQUIRE( CypherTileMapDocument_Init( &document, Allocator_GetSystem(),
                 { 2u, 2u, 2.0f, 3.0f } ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_PaintCell( &document, { 0, 0 },
                 { 0, 1u, 2u, tile_map_cell_shape_t::STAIRS_EAST, 8u } ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_BeginEditGroup( &document,
                 StringView_FromCString( "Change staircase" ) ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_PaintCell( &document, { 0, 0 },
                 { 0, 1u, 2u, tile_map_cell_shape_t::STAIRS_NORTH, 12u } ) == tile_map_document_status_t::OK );
    CypherTileMapDocument_CancelEditGroup( &document );
    auto *cell = CypherTileMapDocument_CellAt( &document, { 0, 0 } );
    REQUIRE( cell->shape == tile_map_cell_shape_t::STAIRS_EAST );
    REQUIRE( cell->nStairSteps == 8u );
    REQUIRE( CypherTileMapDocument_PaintCell( &document, { 0, 0 },
                 { 0, 1u, 2u, tile_map_cell_shape_t::STAIRS_EAST, 12u } ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_Undo( &document ) == tile_map_document_status_t::OK );
    REQUIRE( cell->nStairSteps == 8u );
    REQUIRE( CypherTileMapDocument_Redo( &document ) == tile_map_document_status_t::OK );
    REQUIRE( cell->nStairSteps == 12u );
    REQUIRE( CypherTileMapDocument_EraseCell( &document, { 0, 0 } ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapCell_IsCanonicalEmpty( *cell ) );
    for ( const u16 steps : { 0u, 1u, 33u } ) {
        REQUIRE( CypherTileMapDocument_PaintCell( &document, { 0, 0 },
                     { 0, 1u, 0u, tile_map_cell_shape_t::STAIRS_EAST, steps } ) ==
                 tile_map_document_status_t::INVALID_CELL );
    }
    REQUIRE( CypherTileMapDocument_PaintCell( &document, { 0, 0 },
                 { CY_I16_MAX, 1u, 0u, tile_map_cell_shape_t::STAIRS_EAST, 8u } ) ==
             tile_map_document_status_t::INVALID_CELL );
    REQUIRE( CypherTileMapCell_IsCanonicalEmpty( *cell ) );
    CypherTileMapDocument_Shutdown( &document );
}

TEST_CASE( "Stair marker restrictions and malformed authored cells are diagnosed",
           "[CypherTools][TileMap][Validation][Stairs]" )
{
    tile_map_document_t document{};
    tile_map_validation_report_t report{};
    REQUIRE( CypherTileMapDocument_Init( &document, Allocator_GetSystem(),
                 { 2u, 2u, 2.0f, 3.0f } ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapValidationReport_Init( &report, Allocator_GetSystem() ) ==
             tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_PaintCell( &document, { 0, 0 },
                 { 0, 1u, 0u, tile_map_cell_shape_t::STAIRS_EAST, 8u } ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_PlacePlayerSpawn( &document, { 0, 0 }, 0.0f ) ==
             tile_map_document_status_t::OK );
    unique_id_t doorId{};
    REQUIRE( CypherTileMapDocument_PlaceDoor( &document, { 0, 0 },
                 tile_map_marker_side_t::NORTH, &doorId ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_Validate( &document, &report ) == tile_map_document_status_t::OK );
    REQUIRE( HasDiagnostic( report, tile_map_validation_code_t::PLAYER_SPAWN_ON_STAIRS ) );
    REQUIRE( HasDiagnostic( report, tile_map_validation_code_t::DOOR_ON_STAIRS ) );
    auto *cell = CypherTileMapDocument_CellAt( &document, { 0, 0 } );
    cell->shape = static_cast<tile_map_cell_shape_t>( 255 );
    cell->nStairSteps = 0u;
    REQUIRE( CypherTileMapDocument_Validate( &document, &report ) == tile_map_document_status_t::OK );
    REQUIRE( HasDiagnostic( report, tile_map_validation_code_t::INVALID_CELL_SHAPE ) );
    REQUIRE( HasDiagnostic( report, tile_map_validation_code_t::INVALID_STAIR_STEPS ) );
    CypherTileMapValidationReport_Shutdown( &report );
    CypherTileMapDocument_Shutdown( &document );
}
