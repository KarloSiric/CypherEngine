//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileRegion_Tests.cpp
//  Purpose: Verifies atomic region transformations and marker ownership.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherTileMapDocument.h"
#include "CypherCommon_Allocator.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace cypher::common;
using namespace cypher::tools::tile_editor;

namespace
{

struct region_fixture_t {
    tile_map_document_t document{};

    explicit region_fixture_t( const allocator_t *allocator = Allocator_GetSystem(),
                               u32 width = 12u, u32 height = 12u )
    {
        REQUIRE( CypherTileMapDocument_Init( &document, allocator,
                     { width, height, 2.0f, 3.0f } ) == tile_map_document_status_t::OK );
    }
    ~region_fixture_t() { CypherTileMapDocument_Shutdown( &document ); }

    void paint( tile_map_grid_coord_t point, tile_map_paint_t properties = {} )
    {
        REQUIRE( CypherTileMapDocument_PaintCell( &document, point, properties ) ==
                 tile_map_document_status_t::OK );
    }
    unique_id_t door( tile_map_grid_coord_t point, tile_map_marker_side_t side )
    {
        unique_id_t id{};
        REQUIRE( CypherTileMapDocument_PlaceDoor( &document, point, side, &id ) ==
                 tile_map_document_status_t::OK );
        return id;
    }
    tile_map_cell_t &cell( i32 x, i32 y )
    {
        return *CypherTileMapDocument_CellAt( &document, { x, y } );
    }
};

void RequireCellEqual( const tile_map_cell_t &actual, const tile_map_cell_t &expected )
{
    REQUIRE( actual.nFloorLevel == expected.nFloorLevel );
    REQUIRE( actual.nWallHeightLevels == expected.nWallHeightLevels );
    REQUIRE( actual.nMaterialSlot == expected.nMaterialSlot );
    REQUIRE( actual.flags == expected.flags );
    REQUIRE( actual.shape == expected.shape );
    REQUIRE( actual.nStairSteps == expected.nStairSteps );
}

struct region_snapshot_t {
    std::vector<tile_map_cell_t> cells;
    std::vector<tile_map_marker_t> markers;
    u64 revision;
    usize historyCount;

    explicit region_snapshot_t( const tile_map_document_t &document )
        : cells( document.cells.pData, document.cells.pData + document.cells.nCount ),
          revision( document.nCurrentRevision ),
          historyCount( CypherTileMapDocument_HistoryCount( &document ) )
    {
        for ( usize i = 0u; i < document.markers.nCount; ++i ) {
            markers.push_back( document.markers.pData[i] );
        }
    }
    void requireContents( const tile_map_document_t &document ) const
    {
        REQUIRE( document.cells.nCount == cells.size() );
        REQUIRE( document.markers.nCount == markers.size() );
        for ( usize i = 0u; i < cells.size(); ++i ) RequireCellEqual( document.cells.pData[i], cells[i] );
        for ( usize i = 0u; i < markers.size(); ++i ) {
            const auto &actual = document.markers.pData[i];
            const auto &expected = markers[i];
            REQUIRE( UniqueId_Equals( actual.id, expected.id ) );
            REQUIRE( actual.cell.x == expected.cell.x );
            REQUIRE( actual.cell.y == expected.cell.y );
            REQUIRE( actual.kind == expected.kind );
            REQUIRE( actual.side == expected.side );
            REQUIRE( actual.yawDegrees == expected.yawDegrees );
        }
    }
    void requireUnchanged( const tile_map_document_t &document ) const
    {
        requireContents( document );
        REQUIRE( document.nCurrentRevision == revision );
        REQUIRE( CypherTileMapDocument_HistoryCount( &document ) == historyCount );
    }
};

struct region_allocation_state_t {
    usize remaining{ CY_USIZE_MAX };
    usize outstanding{ 0u };
};

void *RegionAllocate( void *user, usize bytes, usize alignment ) noexcept
{
    auto &state = *static_cast<region_allocation_state_t *>( user );
    if ( state.remaining == 0u ) return nullptr;
    if ( state.remaining != CY_USIZE_MAX ) --state.remaining;
    void *memory = Allocator_Allocate( Allocator_GetSystem(), bytes, alignment );
    if ( memory ) ++state.outstanding;
    return memory;
}

void RegionFree( void *user, void *memory, usize bytes, usize alignment ) noexcept
{
    if ( memory ) --static_cast<region_allocation_state_t *>( user )->outstanding;
    Allocator_Free( Allocator_GetSystem(), memory, bytes, alignment );
}

} // namespace

TEST_CASE( "Region movement preserves overlapping source cells and marker identities",
           "[CypherTools][TileMap][Region]" )
{
    region_fixture_t fixture;
    auto &document = fixture.document;
    REQUIRE( CypherTileMapDocument_PaintRect( &document, { 1, 1, 3u, 2u }, {} ) ==
             tile_map_document_status_t::OK );
    fixture.paint( { 2, 1 }, { 3, 2u, 7u, tile_map_cell_shape_t::STAIRS_WEST, 12u } );
    const auto stair = fixture.cell( 2, 1 );
    REQUIRE( CypherTileMapDocument_PlacePlayerSpawn( &document, { 1, 1 }, 270.0f ) ==
             tile_map_document_status_t::OK );
    const auto spawnId = CypherTileMapDocument_PlayerSpawn( &document )->id;
    const auto doorId = fixture.door( { 3, 2 }, tile_map_marker_side_t::SOUTH );
    CypherTileMapDocument_MarkSaved( &document );
    const region_snapshot_t before( document );

    REQUIRE( CypherTileMapDocument_MoveRegion( &document, { 1, 1, 3u, 2u }, 1, 0 ) ==
             tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_HistoryCount( &document ) == before.historyCount + 1u );
    REQUIRE( CypherTileMapCell_IsCanonicalEmpty( fixture.cell( 1, 1 ) ) );
    RequireCellEqual( fixture.cell( 3, 1 ), stair );
    REQUIRE( CypherTileMapDocument_DoorById( &document, doorId )->cell.x == 4 );
    REQUIRE( CypherTileMapDocument_PlayerSpawn( &document )->cell.x == 2 );
    REQUIRE( UniqueId_Equals( CypherTileMapDocument_PlayerSpawn( &document )->id, spawnId ) );
    const region_snapshot_t moved( document );
    REQUIRE( CypherTileMapDocument_Undo( &document ) == tile_map_document_status_t::OK );
    before.requireContents( document );
    REQUIRE_FALSE( CypherTileMapDocument_IsDirty( &document ) );
    REQUIRE( CypherTileMapDocument_Redo( &document ) == tile_map_document_status_t::OK );
    moved.requireContents( document );
}

TEST_CASE( "Region copies duplicate doors with stable new IDs and retain one player spawn",
           "[CypherTools][TileMap][Region]" )
{
    region_fixture_t fixture;
    auto &document = fixture.document;
    fixture.paint( { 1, 1 }, { -2, 3u, 4u } );
    fixture.paint( { 2, 1 }, { 0, 1u, 2u, tile_map_cell_shape_t::STAIRS_NORTH, 16u } );
    REQUIRE( CypherTileMapDocument_PlacePlayerSpawn( &document, { 1, 1 }, 25.0f ) ==
             tile_map_document_status_t::OK );
    const auto spawnId = CypherTileMapDocument_PlayerSpawn( &document )->id;
    const auto doorId = fixture.door( { 1, 1 }, tile_map_marker_side_t::WEST );
    const region_snapshot_t before( document );
    REQUIRE( CypherTileMapDocument_CopyRegion( &document, { 1, 1, 3u, 2u }, 4, 0 ) ==
             tile_map_document_status_t::OK );
    RequireCellEqual( fixture.cell( 5, 1 ), fixture.cell( 1, 1 ) );
    RequireCellEqual( fixture.cell( 6, 1 ), fixture.cell( 2, 1 ) );
    REQUIRE( CypherTileMapCell_IsCanonicalEmpty( fixture.cell( 7, 2 ) ) );
    REQUIRE( document.markers.nCount == 3u );
    const auto *copiedDoor = CypherTileMapDocument_DoorAt( &document, { 5, 1 }, tile_map_marker_side_t::WEST );
    REQUIRE( copiedDoor != nullptr );
    const auto copiedId = copiedDoor->id;
    REQUIRE_FALSE( UniqueId_Equals( copiedId, doorId ) );
    REQUIRE( UniqueId_Equals( CypherTileMapDocument_PlayerSpawn( &document )->id, spawnId ) );
    REQUIRE( CypherTileMapDocument_PlayerSpawn( &document )->cell.x == 1 );
    REQUIRE( CypherTileMapDocument_HistoryCount( &document ) == before.historyCount + 1u );
    REQUIRE( CypherTileMapDocument_Undo( &document ) == tile_map_document_status_t::OK );
    before.requireContents( document );
    REQUIRE( CypherTileMapDocument_Redo( &document ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_DoorById( &document, copiedId ) != nullptr );

    const region_snapshot_t copied( document );
    REQUIRE( CypherTileMapDocument_CopyRegion( &document, { 1, 1, 3u, 2u }, 1, 0 ) ==
             tile_map_document_status_t::INVALID_STATE );
    copied.requireUnchanged( document );
}

TEST_CASE( "Deleting a region atomically removes its cells and owned markers",
           "[CypherTools][TileMap][Region]" )
{
    region_fixture_t fixture;
    auto &document = fixture.document;
    fixture.paint( { 1, 1 } );
    fixture.paint( { 2, 1 } );
    fixture.paint( { 8, 8 } );
    const auto first = fixture.door( { 1, 1 }, tile_map_marker_side_t::NORTH );
    const auto outside = fixture.door( { 8, 8 }, tile_map_marker_side_t::EAST );
    const auto last = fixture.door( { 2, 1 }, tile_map_marker_side_t::SOUTH );
    REQUIRE( CypherTileMapDocument_PlacePlayerSpawn( &document, { 2, 1 }, 0.0f ) ==
             tile_map_document_status_t::OK );
    const region_snapshot_t before( document );
    REQUIRE( CypherTileMapDocument_DeleteRegion( &document, { 1, 1, 2u, 1u } ) ==
             tile_map_document_status_t::OK );
    REQUIRE( document.markers.nCount == 1u );
    REQUIRE( CypherTileMapDocument_DoorById( &document, first ) == nullptr );
    REQUIRE( CypherTileMapDocument_DoorById( &document, last ) == nullptr );
    REQUIRE( CypherTileMapDocument_DoorById( &document, outside ) != nullptr );
    REQUIRE( CypherTileMapDocument_PlayerSpawn( &document ) == nullptr );
    REQUIRE( CypherTileMapCell_IsCanonicalEmpty( fixture.cell( 1, 1 ) ) );
    REQUIRE( CypherTileMapDocument_HistoryCount( &document ) == before.historyCount + 1u );
    REQUIRE( CypherTileMapDocument_Undo( &document ) == tile_map_document_status_t::OK );
    before.requireContents( document );
    REQUIRE( CypherTileMapDocument_Redo( &document ) == tile_map_document_status_t::OK );
    REQUIRE( document.markers.nCount == 1u );
}

TEST_CASE( "Clockwise region rotation swaps dimensions and rotates stairs doors and spawn yaw",
           "[CypherTools][TileMap][Region]" )
{
    region_fixture_t fixture;
    auto &document = fixture.document;
    REQUIRE( CypherTileMapDocument_PaintRect( &document, { 1, 1, 3u, 2u }, {} ) ==
             tile_map_document_status_t::OK );
    fixture.paint( { 1, 2 }, { 2, 3u, 7u, tile_map_cell_shape_t::STAIRS_NORTH, 12u } );
    REQUIRE( CypherTileMapDocument_PlacePlayerSpawn( &document, { 1, 1 }, 350.0f ) ==
             tile_map_document_status_t::OK );
    const auto doorId = fixture.door( { 3, 2 }, tile_map_marker_side_t::WEST );
    const region_snapshot_t before( document );
    REQUIRE( CypherTileMapDocument_RotateRegionClockwise( &document, { 1, 1, 3u, 2u } ) ==
             tile_map_document_status_t::OK );
    REQUIRE( fixture.cell( 1, 1 ).shape == tile_map_cell_shape_t::STAIRS_EAST );
    REQUIRE( fixture.cell( 1, 1 ).nStairSteps == 12u );
    REQUIRE( fixture.cell( 1, 1 ).nFloorLevel == 2 );
    REQUIRE( fixture.cell( 1, 1 ).nMaterialSlot == 7u );
    REQUIRE( CypherTileMapCell_IsCanonicalEmpty( fixture.cell( 3, 1 ) ) );
    const auto *spawn = CypherTileMapDocument_PlayerSpawn( &document );
    REQUIRE( spawn->cell.x == 2 );
    REQUIRE( spawn->cell.y == 1 );
    REQUIRE( spawn->yawDegrees == Catch::Approx( 80.0f ) );
    const auto *door = CypherTileMapDocument_DoorById( &document, doorId );
    REQUIRE( door->cell.x == 1 );
    REQUIRE( door->cell.y == 3 );
    REQUIRE( door->side == tile_map_marker_side_t::NORTH );
    REQUIRE( CypherTileMapDocument_RotateRegionClockwise( &document, { 1, 1, 2u, 3u } ) ==
             tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_RotateRegionClockwise( &document, { 1, 1, 3u, 2u } ) ==
             tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_RotateRegionClockwise( &document, { 1, 1, 2u, 3u } ) ==
             tile_map_document_status_t::OK );
    before.requireContents( document );
    REQUIRE( CypherTileMapDocument_HistoryCount( &document ) == before.historyCount + 4u );
    for ( int i = 0; i < 4; ++i ) {
        REQUIRE( CypherTileMapDocument_Undo( &document ) == tile_map_document_status_t::OK );
    }
    before.requireContents( document );
}

TEST_CASE( "Region transforms reject occupied footprints markers and extreme coordinates without mutation",
           "[CypherTools][TileMap][Region]" )
{
    region_fixture_t fixture;
    auto &document = fixture.document;
    fixture.paint( { 1, 1 } );
    fixture.paint( { 4, 1 } );
    fixture.door( { 7, 1 }, tile_map_marker_side_t::NORTH ); // An invalid source marker still owns its location.
    const region_snapshot_t before( document );
    REQUIRE( CypherTileMapDocument_MoveRegion( &document, { 1, 1, 2u, 1u }, 2, 0 ) ==
             tile_map_document_status_t::INVALID_STATE ); // Occupied cell lies under a source hole.
    REQUIRE( CypherTileMapDocument_CopyRegion( &document, { 1, 1, 1u, 1u }, 6, 0 ) ==
             tile_map_document_status_t::INVALID_STATE ); // Marker collision even without a floor.
    REQUIRE( CypherTileMapDocument_MoveRegion( &document, { 1, 1, 2u, 1u }, CY_I32_MAX, 0 ) ==
             tile_map_document_status_t::OUT_OF_BOUNDS );
    REQUIRE( CypherTileMapDocument_MoveRegion( &document, { 1, 1, 2u, 1u }, CY_I32_MIN, 0 ) ==
             tile_map_document_status_t::OUT_OF_BOUNDS );
    REQUIRE( CypherTileMapDocument_DeleteRegion( &document, { 1, 1, CY_U32_MAX, 1u } ) ==
             tile_map_document_status_t::OUT_OF_BOUNDS );
    REQUIRE( CypherTileMapDocument_RotateRegionClockwise( &document, { 10, 1, 1u, 3u } ) ==
             tile_map_document_status_t::OUT_OF_BOUNDS );
    before.requireUnchanged( document );

    fixture.paint( { 1, 3 } );
    const region_snapshot_t beforeRotation( document );
    REQUIRE( CypherTileMapDocument_RotateRegionClockwise( &document, { 1, 1, 3u, 2u } ) ==
             tile_map_document_status_t::INVALID_STATE );
    beforeRotation.requireUnchanged( document );
}

TEST_CASE( "Region elevation validates the complete selection before changing any tile",
           "[CypherTools][TileMap][Region]" )
{
    region_fixture_t fixture;
    auto &document = fixture.document;
    fixture.paint( { 1, 1 }, { 0, 2u, 5u } );
    fixture.paint( { 2, 1 }, { CY_I16_MAX - 1, 1u, 6u,
                              tile_map_cell_shape_t::STAIRS_SOUTH, 14u } );
    fixture.door( { 1, 1 }, tile_map_marker_side_t::NORTH );
    const region_snapshot_t before( document );
    REQUIRE( CypherTileMapDocument_AdjustRegionFloorLevel( &document, { 1, 1, 3u, 1u }, 1 ) ==
             tile_map_document_status_t::INVALID_CELL );
    REQUIRE( CypherTileMapDocument_AdjustRegionFloorLevel( &document, { 1, 1, 3u, 1u }, CY_I32_MIN ) ==
             tile_map_document_status_t::INVALID_CELL );
    before.requireUnchanged( document );
    REQUIRE( CypherTileMapDocument_AdjustRegionFloorLevel( &document, { 1, 1, 3u, 1u }, -1 ) ==
             tile_map_document_status_t::OK );
    REQUIRE( fixture.cell( 1, 1 ).nFloorLevel == -1 );
    REQUIRE( fixture.cell( 2, 1 ).nFloorLevel == CY_I16_MAX - 2 );
    REQUIRE( fixture.cell( 2, 1 ).shape == tile_map_cell_shape_t::STAIRS_SOUTH );
    REQUIRE( fixture.cell( 2, 1 ).nStairSteps == 14u );
    REQUIRE( fixture.cell( 2, 1 ).nMaterialSlot == 6u );
    REQUIRE( CypherTileMapCell_IsCanonicalEmpty( fixture.cell( 3, 1 ) ) );
    REQUIRE( CypherTileMapDocument_Undo( &document ) == tile_map_document_status_t::OK );
    before.requireContents( document );
    fixture.paint( { 1, 1 }, { CY_I16_MIN, 1u, 0u } );
    REQUIRE( CypherTileMapDocument_AdjustRegionFloorLevel( &document, { 1, 1, 1u, 1u }, -1 ) ==
             tile_map_document_status_t::INVALID_CELL );
}

TEST_CASE( "Region commands own their history action and leave unrelated groups and redo intact",
           "[CypherTools][TileMap][Region]" )
{
    region_fixture_t fixture;
    auto &document = fixture.document;
    fixture.paint( { 1, 1 } );
    fixture.paint( { 2, 1 } );
    REQUIRE( CypherTileMapDocument_Undo( &document ) == tile_map_document_status_t::OK );
    const region_snapshot_t before( document );
    REQUIRE( CypherTileMapDocument_MoveRegion( &document, { 1, 1, 1u, 1u }, 0, 0 ) ==
             tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_DeleteRegion( &document, { 4, 4, 2u, 2u } ) ==
             tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_AdjustRegionFloorLevel( &document, { 1, 1, 1u, 1u }, 0 ) ==
             tile_map_document_status_t::OK );
    before.requireUnchanged( document );
    REQUIRE( CypherTileMapDocument_CanRedo( &document ) );
    REQUIRE( CypherTileMapDocument_BeginEditGroup( &document, StringView_FromCString( "Existing drag" ) ) ==
             tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_MoveRegion( &document, { 1, 1, 1u, 1u }, 1, 0 ) ==
             tile_map_document_status_t::INVALID_STATE );
    REQUIRE( CypherTileMapDocument_CopyRegion( &document, { 1, 1, 1u, 1u }, 1, 0 ) ==
             tile_map_document_status_t::INVALID_STATE );
    REQUIRE( CypherTileMapDocument_DeleteRegion( &document, { 1, 1, 1u, 1u } ) ==
             tile_map_document_status_t::INVALID_STATE );
    REQUIRE( CypherTileMapDocument_RotateRegionClockwise( &document, { 1, 1, 1u, 1u } ) ==
             tile_map_document_status_t::INVALID_STATE );
    REQUIRE( CypherTileMapDocument_AdjustRegionFloorLevel( &document, { 1, 1, 1u, 1u }, 1 ) ==
             tile_map_document_status_t::INVALID_STATE );
    REQUIRE( CypherTileMapDocument_IsEditGroupOpen( &document ) );
    before.requireUnchanged( document );
    CypherTileMapDocument_CancelEditGroup( &document );
}

TEST_CASE( "Region copy allocation failures restore cells markers revisions and redo without leaks",
           "[CypherTools][TileMap][Region]" )
{
    bool reachedSuccess = false;
    usize failures = 0u;
    for ( usize permitted = 0u; permitted < 24u; ++permitted ) {
        region_allocation_state_t state;
        const allocator_t allocator{ &RegionAllocate, nullptr, &RegionFree, &state };
        {
            region_fixture_t fixture( &allocator );
            auto &document = fixture.document;
            fixture.paint( { 1, 1 } );
            fixture.door( { 1, 1 }, tile_map_marker_side_t::NORTH );
            fixture.paint( { 2, 1 } );
            REQUIRE( CypherTileMapDocument_Undo( &document ) == tile_map_document_status_t::OK );
            const region_snapshot_t before( document );
            state.remaining = permitted;
            const auto status = CypherTileMapDocument_CopyRegion( &document, { 1, 1, 1u, 1u }, 4, 0 );
            state.remaining = CY_USIZE_MAX;
            REQUIRE_FALSE( CypherTileMapDocument_IsEditGroupOpen( &document ) );
            if ( status == tile_map_document_status_t::OK ) {
                reachedSuccess = true;
                REQUIRE( document.markers.nCount == 2u );
                REQUIRE_FALSE( CypherTileMapDocument_CanRedo( &document ) );
            } else {
                ++failures;
                REQUIRE( status == tile_map_document_status_t::ALLOCATION_FAILED );
                before.requireUnchanged( document );
                REQUIRE( CypherTileMapDocument_CanRedo( &document ) );
            }
        }
        REQUIRE( state.outstanding == 0u );
        if ( reachedSuccess ) break;
    }
    REQUIRE( reachedSuccess );
    REQUIRE( failures >= 3u ); // Covers staging, marker capacity, and history commit allocations.
}

TEST_CASE( "Region commands enforce history and active-cell budgets atomically",
           "[CypherTools][TileMap][Region]" )
{
    region_fixture_t fixture( Allocator_GetSystem(), 1024u, 512u );
    auto &document = fixture.document;
    // Direct fixture setup avoids spending the action budget on preparation.
    for ( i32 y = 0; y < 512; ++y ) {
        for ( i32 x = 0; x < 512; ++x ) fixture.cell( x, y ).flags = TILE_MAP_CELL_FLAG_FLOOR;
    }
    CypherTileMapDocument_MarkSaved( &document );
    const u64 revision = document.nCurrentRevision;
    REQUIRE( CypherTileMapDocument_CopyRegion( &document, { 0, 0, 1u, 1u }, 700, 0 ) ==
             tile_map_document_status_t::ACTIVE_CELL_LIMIT_REACHED );
    REQUIRE( CypherTileMapCell_IsCanonicalEmpty( fixture.cell( 700, 0 ) ) );
    REQUIRE( document.nCurrentRevision == revision );
    REQUIRE( CypherTileMapDocument_HistoryCount( &document ) == 0u );
    REQUIRE_FALSE( CypherTileMapDocument_IsEditGroupOpen( &document ) );
    REQUIRE( CypherTileMapDocument_MoveRegion( &document, { 0, 0, 512u, 512u }, 512, 0 ) ==
             tile_map_document_status_t::HISTORY_LIMIT_REACHED );
    REQUIRE( CypherTileMapDocument_CellHasFloor( &document, { 0, 0 } ) );
    REQUIRE( CypherTileMapCell_IsCanonicalEmpty( fixture.cell( 700, 0 ) ) );
    REQUIRE( document.nCurrentRevision == revision );
    REQUIRE( CypherTileMapDocument_HistoryCount( &document ) == 0u );
    REQUIRE_FALSE( CypherTileMapDocument_IsDirty( &document ) );
}

TEST_CASE( "Marker growth shares the authored map limit and rejects region copies atomically",
           "[CypherTools][TileMap][Region]" )
{
    region_fixture_t fixture( Allocator_GetSystem(), 80u, 80u );
    auto &document = fixture.document;
    REQUIRE( Vector_Reserve( &document.markers, TILE_MAP_MAX_MARKERS ) );
    // Build a near-limit source directly so fixture setup does not consume
    // history. Each door has a unique identity and authored cell/edge.
    for ( usize i = 0u; i < TILE_MAP_MAX_MARKERS - 1u; ++i ) {
        tile_map_marker_t marker{};
        REQUIRE( UniqueId_CreateRandom( &marker.id ) );
        marker.kind = tile_map_marker_kind_t::DOOR;
        marker.side = tile_map_marker_side_t::NORTH;
        marker.cell = { 2 + static_cast<i32>( i % 64u ),
                        1 + static_cast<i32>( i / 64u ) };
        fixture.cell( marker.cell.x, marker.cell.y ).flags = TILE_MAP_CELL_FLAG_FLOOR;
        REQUIRE( Vector_PushBack( &document.markers, marker ) );
    }
    fixture.paint( { 78, 78 } );
    REQUIRE( CypherTileMapDocument_Undo( &document ) == tile_map_document_status_t::OK );
    const region_snapshot_t before( document );
    REQUIRE( CypherTileMapDocument_CopyRegion( &document, { 2, 1, 2u, 1u }, 68, -1 ) ==
             tile_map_document_status_t::MARKER_LIMIT_REACHED );
    before.requireUnchanged( document );
    REQUIRE( CypherTileMapDocument_CanRedo( &document ) );
    REQUIRE_FALSE( CypherTileMapDocument_IsEditGroupOpen( &document ) );

    // A single new marker still fits exactly at the cap.
    REQUIRE( CypherTileMapDocument_CopyRegion( &document, { 2, 1, 1u, 1u }, 68, -1 ) ==
             tile_map_document_status_t::OK );
    REQUIRE( document.markers.nCount == TILE_MAP_MAX_MARKERS );
    fixture.paint( { 78, 78 } );
    REQUIRE( CypherTileMapDocument_Undo( &document ) == tile_map_document_status_t::OK );
    const region_snapshot_t atLimit( document );
    REQUIRE( CypherTileMapDocument_CopyRegion( &document, { 2, 1, 1u, 1u }, 69, -1 ) ==
             tile_map_document_status_t::MARKER_LIMIT_REACHED );
    unique_id_t id{};
    REQUIRE( CypherTileMapDocument_PlaceDoor( &document, { 71, 0 }, tile_map_marker_side_t::NORTH, &id ) ==
             tile_map_document_status_t::MARKER_LIMIT_REACHED );
    REQUIRE_FALSE( UniqueId_IsValid( id ) );
    REQUIRE( CypherTileMapDocument_PlacePlayerSpawn( &document, { 2, 1 }, 0.0f ) ==
             tile_map_document_status_t::MARKER_LIMIT_REACHED );
    // Existing doors are still idempotent at the limit.
    REQUIRE( CypherTileMapDocument_PlaceDoor( &document, { 2, 1 }, tile_map_marker_side_t::NORTH, &id ) ==
             tile_map_document_status_t::OK );
    REQUIRE( UniqueId_Equals( id, document.markers.pData[0].id ) );
    atLimit.requireUnchanged( document );
    REQUIRE( CypherTileMapDocument_CanRedo( &document ) );
    REQUIRE_FALSE( CypherTileMapDocument_IsEditGroupOpen( &document ) );
}
