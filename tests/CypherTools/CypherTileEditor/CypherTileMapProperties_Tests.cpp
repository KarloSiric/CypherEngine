//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileMapProperties_Tests.cpp
//  Purpose: Tests atomic live map dimensions and world metrics with history.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherTileMapDocument.h"
#include "CypherTileMapGeometry.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>

using namespace cypher::common;
using namespace cypher::tools::tile_editor;

namespace
{

struct document_fixture_t {
    tile_map_document_t document{};
    ~document_fixture_t() { CypherTileMapDocument_Shutdown( &document ); }
};

struct allocation_state_t {
    usize remaining{ CY_USIZE_MAX };
    usize outstanding{ 0u };
    usize liveBytes{ 0u };
};

void *PropertiesAllocate( void *pUserData, usize bytes, usize alignment ) noexcept
{
    auto &state = *static_cast<allocation_state_t *>( pUserData );
    if ( state.remaining == 0u ) return nullptr;
    if ( state.remaining != CY_USIZE_MAX ) --state.remaining;
    void *memory = Allocator_Allocate( Allocator_GetSystem(), bytes, alignment );
    if ( memory != nullptr ) {
        ++state.outstanding;
        state.liveBytes += bytes;
    }
    return memory;
}

void PropertiesFree( void *pUserData, void *memory, usize bytes, usize alignment ) noexcept
{
    auto &state = *static_cast<allocation_state_t *>( pUserData );
    if ( memory != nullptr ) {
        --state.outstanding;
        state.liveBytes -= bytes;
    }
    Allocator_Free( Allocator_GetSystem(), memory, bytes, alignment );
}

allocator_t PropertiesAllocator( allocation_state_t &state ) noexcept
{
    return { &PropertiesAllocate, nullptr, &PropertiesFree, &state };
}

void RequireDescription( const tile_map_document_t &document,
    const tile_map_document_desc_t &desc )
{
    REQUIRE( document.nWidth == desc.nWidth );
    REQUIRE( document.nHeight == desc.nHeight );
    REQUIRE( document.nCellSize == desc.nCellSize );
    REQUIRE( document.nLevelHeight == desc.nLevelHeight );
    REQUIRE( document.cells.nCount == static_cast<usize>( desc.nWidth ) * desc.nHeight );
    REQUIRE( CypherTileMapDocument_IsInitialized( &document ) );
}

void RequireCornerPattern( const tile_map_document_t &document )
{
    for ( u32 y = 0u; y < document.nHeight; ++y ) {
        for ( u32 x = 0u; x < document.nWidth; ++x ) {
            const auto *cell = CypherTileMapDocument_CellAt( &document,
                { static_cast<i32>( x ), static_cast<i32>( y ) } );
            REQUIRE( cell != nullptr );
            if ( x < 3u && y < 3u ) {
                REQUIRE( cell->flags == TILE_MAP_CELL_FLAG_FLOOR );
                REQUIRE( cell->nMaterialSlot == 1u + x + 3u * y );
                REQUIRE( cell->nFloorLevel == static_cast<i16>( y ) );
            } else {
                REQUIRE( CypherTileMapCell_IsCanonicalEmpty( *cell ) );
            }
        }
    }
}

} // namespace

TEST_CASE( "Live map properties preserve cell coordinates and stable identities",
           "[CypherTools][TileMap][Properties]" )
{
    document_fixture_t fixture;
    auto &document = fixture.document;
    REQUIRE( CypherTileMapDocument_Init( &document, Allocator_GetSystem(),
        { 6u, 4u, 2.0f, 3.0f } ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_PaintCell( &document, { 4, 2 },
        { -2, 3u, 17u, tile_map_cell_shape_t::STAIRS_WEST, 12u } ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_PlacePlayerSpawn( &document, { 1, 1 }, 135.0f ) ==
        tile_map_document_status_t::OK );
    unique_id_t doorId{};
    REQUIRE( CypherTileMapDocument_PlaceDoor( &document, { 2, 2 },
        tile_map_marker_side_t::EAST, &doorId ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_SetMaterialBinding( &document, 17u,
        StringView_FromCString( "materials/blockout/stone.cymat" ) ) == tile_map_document_status_t::OK );
    const auto mapId = document.mapId;
    const auto spawnId = CypherTileMapDocument_PlayerSpawn( &document )->id;
    const auto *bindingsBefore = document.materialBindings.pData;
    const auto *markersBefore = document.markers.pData;
    const auto historyBefore = CypherTileMapDocument_HistoryCount( &document );
    CypherTileMapDocument_MarkSaved( &document );
    const auto savedRevision = document.nSavedRevision;

    REQUIRE( CypherTileMapDocument_SetDescription( &document, { 10u, 7u, 4.0f, 5.0f } ) ==
        tile_map_document_status_t::OK );
    RequireDescription( document, { 10u, 7u, 4.0f, 5.0f } );
    REQUIRE( CypherTileMapDocument_HistoryCount( &document ) == historyBefore + 1u );
    REQUIRE( UniqueId_Equals( document.mapId, mapId ) );
    REQUIRE( document.materialBindings.pData == bindingsBefore );
    REQUIRE( document.markers.pData == markersBefore );
    REQUIRE( UniqueId_Equals( CypherTileMapDocument_PlayerSpawn( &document )->id, spawnId ) );
    REQUIRE( CypherTileMapDocument_DoorById( &document, doorId )->cell.x == 2 );
    const auto *cell = CypherTileMapDocument_CellAt( &document, { 4, 2 } );
    REQUIRE( cell->nFloorLevel == -2 );
    REQUIRE( cell->nWallHeightLevels == 3u );
    REQUIRE( cell->nMaterialSlot == 17u );
    REQUIRE( cell->shape == tile_map_cell_shape_t::STAIRS_WEST );
    REQUIRE( cell->nStairSteps == 12u );
    REQUIRE( CypherTileMapCell_IsCanonicalEmpty( *CypherTileMapDocument_CellAt( &document, { 9, 6 } ) ) );
    REQUIRE( StringView_Equals( CypherTileMapDocument_UndoLabel( &document ),
        StringView_FromCString( "Map properties" ) ) );
    REQUIRE( CypherTileMapDocument_Undo( &document ) == tile_map_document_status_t::OK );
    RequireDescription( document, { 6u, 4u, 2.0f, 3.0f } );
    REQUIRE( document.nCurrentRevision == savedRevision );
    REQUIRE_FALSE( CypherTileMapDocument_IsDirty( &document ) );
    REQUIRE( CypherTileMapDocument_Redo( &document ) == tile_map_document_status_t::OK );
    REQUIRE( UniqueId_Equals( document.mapId, mapId ) );
    REQUIRE( UniqueId_Equals( CypherTileMapDocument_PlayerSpawn( &document )->id, spawnId ) );
    REQUIRE( CypherTileMapDocument_DoorById( &document, doorId ) != nullptr );
}

TEST_CASE( "Live dimensions remap rows for opposing width and height changes",
           "[CypherTools][TileMap][Properties][History]" )
{
    document_fixture_t fixture;
    auto &document = fixture.document;
    REQUIRE( CypherTileMapDocument_Init( &document, Allocator_GetSystem(),
        { 6u, 6u, 2.0f, 3.0f } ) == tile_map_document_status_t::OK );
    for ( i32 y = 0; y < 3; ++y ) {
        for ( i32 x = 0; x < 3; ++x ) {
            REQUIRE( CypherTileMapDocument_PaintCell( &document, { x, y },
                { static_cast<i16>( y ), 1u, static_cast<u16>( 1 + x + 3 * y ) } ) ==
                tile_map_document_status_t::OK );
        }
    }
    const tile_map_document_desc_t descriptions[] = {
        { 3u, 10u, 2.0f, 3.0f }, { 10u, 3u, 2.0f, 3.0f },
        { 12u, 12u, 2.0f, 3.0f }, { 3u, 3u, 2.0f, 3.0f }
    };
    for ( const auto &desc : descriptions ) {
        REQUIRE( CypherTileMapDocument_SetDescription( &document, desc ) == tile_map_document_status_t::OK );
        RequireDescription( document, desc );
        RequireCornerPattern( document );
    }
    for ( int i = 0; i < 4; ++i ) {
        REQUIRE( CypherTileMapDocument_Undo( &document ) == tile_map_document_status_t::OK );
        RequireCornerPattern( document );
    }
    RequireDescription( document, { 6u, 6u, 2.0f, 3.0f } );
    for ( const auto &desc : descriptions ) {
        REQUIRE( CypherTileMapDocument_Redo( &document ) == tile_map_document_status_t::OK );
        RequireDescription( document, desc );
        RequireCornerPattern( document );
    }
}

TEST_CASE( "Shrinking refuses authored cells and markers without losing redo",
           "[CypherTools][TileMap][Properties]" )
{
    document_fixture_t fixture;
    auto &document = fixture.document;
    REQUIRE( CypherTileMapDocument_Init( &document, Allocator_GetSystem(),
        { 8u, 8u, 2.0f, 3.0f } ) == tile_map_document_status_t::OK );
    SECTION( "Floor outside the new width" ) {
        REQUIRE( CypherTileMapDocument_PaintCell( &document, { 7, 1 }, {} ) == tile_map_document_status_t::OK );
    }
    SECTION( "Spawn on empty cell outside the new height" ) {
        REQUIRE( CypherTileMapDocument_PlacePlayerSpawn( &document, { 1, 7 }, 0.0f ) ==
            tile_map_document_status_t::OK );
    }
    SECTION( "Door on empty cell outside the new width" ) {
        unique_id_t id{};
        REQUIRE( CypherTileMapDocument_PlaceDoor( &document, { 7, 1 },
            tile_map_marker_side_t::NORTH, &id ) == tile_map_document_status_t::OK );
    }
    SECTION( "Noncanonical empty cell must not silently lose properties" ) {
        CypherTileMapDocument_CellAt( &document, { 7, 1 } )->nMaterialSlot = 9u;
    }
    REQUIRE( CypherTileMapDocument_PaintCell( &document, { 0, 0 }, {} ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_Undo( &document ) == tile_map_document_status_t::OK );
    const auto revision = document.nCurrentRevision;
    const auto nextRevision = document.nNextRevision;
    const auto historyCount = CypherTileMapDocument_HistoryCount( &document );
    const auto *storage = document.cells.pData;
    REQUIRE( CypherTileMapDocument_SetDescription( &document, { 4u, 4u, 4.0f, 5.0f } ) ==
        tile_map_document_status_t::OUT_OF_BOUNDS );
    RequireDescription( document, { 8u, 8u, 2.0f, 3.0f } );
    REQUIRE( document.cells.pData == storage );
    REQUIRE( document.nCurrentRevision == revision );
    REQUIRE( document.nNextRevision == nextRevision );
    REQUIRE( CypherTileMapDocument_HistoryCount( &document ) == historyCount );
    REQUIRE( CypherTileMapDocument_CanRedo( &document ) );
    REQUIRE( CypherTileMapDocument_Redo( &document ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_CellHasFloor( &document, { 0, 0 } ) );
}

TEST_CASE( "World metric edits change generated geometry without changing tile data",
           "[CypherTools][TileMap][Properties][Geometry]" )
{
    document_fixture_t fixture;
    auto &document = fixture.document;
    REQUIRE( CypherTileMapDocument_Init( &document, Allocator_GetSystem(),
        { 4u, 4u, 2.0f, 3.0f } ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_PaintCell( &document, { 2, 1 }, { 2, 3u, 7u } ) ==
        tile_map_document_status_t::OK );
    tile_map_geometry_t geometry{};
    REQUIRE( CypherTileMapGeometry_Init( &geometry, Allocator_GetSystem() ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapGeometry_Build( &document, {}, &geometry ) == tile_map_document_status_t::OK );
    REQUIRE( geometry.boundsMaxZ == Catch::Approx( 15.0f ) );
    REQUIRE( CypherTileMapDocument_SetDescription( &document, { 4u, 4u, 4.0f, 5.0f } ) ==
        tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapGeometry_Build( &document, {}, &geometry ) == tile_map_document_status_t::OK );
    REQUIRE( geometry.boundsMaxZ == Catch::Approx( 25.0f ) );
    REQUIRE( geometry.boxes.pData[0].kind == tile_map_geometry_box_kind_t::FLOOR );
    REQUIRE( geometry.boxes.pData[0].centerX == Catch::Approx( 10.0f ) );
    REQUIRE( geometry.boxes.pData[0].centerY == Catch::Approx( 6.0f ) );
    REQUIRE( geometry.boxes.pData[0].centerZ == Catch::Approx( 9.875f ) );
    REQUIRE( geometry.boxes.pData[0].halfExtentX == Catch::Approx( 2.0f ) );
    REQUIRE( geometry.boxes.pData[0].nMaterialSlot == 7u );
    REQUIRE( CypherTileMapDocument_CellAt( &document, { 2, 1 } )->nFloorLevel == 2 );
    REQUIRE( CypherTileMapDocument_Undo( &document ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapGeometry_Build( &document, {}, &geometry ) == tile_map_document_status_t::OK );
    REQUIRE( geometry.boundsMaxZ == Catch::Approx( 15.0f ) );
    CypherTileMapGeometry_Shutdown( &geometry );
}

TEST_CASE( "Cell edit history remains aligned across multiple map resizes",
           "[CypherTools][TileMap][Properties][History]" )
{
    document_fixture_t fixture;
    auto &document = fixture.document;
    REQUIRE( CypherTileMapDocument_Init( &document, Allocator_GetSystem(),
        { 4u, 4u, 2.0f, 3.0f } ) == tile_map_document_status_t::OK );
    CypherTileMapDocument_MarkSaved( &document );
    REQUIRE( CypherTileMapDocument_PaintCell( &document, { 2, 2 }, { 0, 1u, 5u } ) ==
        tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_SetDescription( &document, { 8u, 6u, 2.0f, 3.0f } ) ==
        tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_PaintCell( &document, { 6, 4 }, { 0, 1u, 9u } ) ==
        tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_SetDescription( &document, { 10u, 7u, 2.0f, 3.0f } ) ==
        tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_Undo( &document ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_CellAt( &document, { 6, 4 } )->nMaterialSlot == 9u );
    REQUIRE( CypherTileMapDocument_Undo( &document ) == tile_map_document_status_t::OK );
    REQUIRE_FALSE( CypherTileMapDocument_CellHasFloor( &document, { 6, 4 } ) );
    REQUIRE( CypherTileMapDocument_Undo( &document ) == tile_map_document_status_t::OK );
    RequireDescription( document, { 4u, 4u, 2.0f, 3.0f } );
    REQUIRE( CypherTileMapDocument_CellAt( &document, { 2, 2 } )->nMaterialSlot == 5u );
    REQUIRE( CypherTileMapDocument_Undo( &document ) == tile_map_document_status_t::OK );
    REQUIRE_FALSE( CypherTileMapDocument_IsDirty( &document ) );
    for ( int i = 0; i < 4; ++i ) {
        REQUIRE( CypherTileMapDocument_Redo( &document ) == tile_map_document_status_t::OK );
    }
    RequireDescription( document, { 10u, 7u, 2.0f, 3.0f } );
    REQUIRE( CypherTileMapDocument_CellAt( &document, { 2, 2 } )->nMaterialSlot == 5u );
    REQUIRE( CypherTileMapDocument_CellAt( &document, { 6, 4 } )->nMaterialSlot == 9u );
}

TEST_CASE( "Map property validation and no-ops preserve revision and redo",
           "[CypherTools][TileMap][Properties]" )
{
    document_fixture_t fixture;
    auto &document = fixture.document;
    REQUIRE( CypherTileMapDocument_SetDescription( &document, {} ) == tile_map_document_status_t::NOT_INITIALIZED );
    REQUIRE( CypherTileMapDocument_SetDescription( nullptr, {} ) == tile_map_document_status_t::NOT_INITIALIZED );
    REQUIRE( CypherTileMapDocument_Init( &document, Allocator_GetSystem(),
        { 4u, 4u, 2.0f, 3.0f } ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_PaintCell( &document, { 0, 0 }, {} ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_Undo( &document ) == tile_map_document_status_t::OK );
    const auto revision = document.nCurrentRevision;
    const auto nextRevision = document.nNextRevision;
    REQUIRE( CypherTileMapDocument_SetDescription( &document, { 4u, 4u, 2.0f, 3.0f } ) ==
        tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_SetDescription( &document, { 0u, 4u, 2.0f, 3.0f } ) ==
        tile_map_document_status_t::INVALID_DIMENSIONS );
    REQUIRE( CypherTileMapDocument_SetDescription( &document, { 4u, TILE_MAP_MAX_HEIGHT + 1u, 2.0f, 3.0f } ) ==
        tile_map_document_status_t::INVALID_DIMENSIONS );
    REQUIRE( CypherTileMapDocument_SetDescription( &document, { 4u, 4u, 0.1f, 3.0f } ) ==
        tile_map_document_status_t::INVALID_METRICS );
    REQUIRE( CypherTileMapDocument_SetDescription( &document, { 4u, 4u, 2.0f, 0.1f } ) ==
        tile_map_document_status_t::INVALID_METRICS );
    REQUIRE( CypherTileMapDocument_SetDescription( &document,
        { 4u, 4u, std::numeric_limits<f32>::quiet_NaN(), 3.0f } ) == tile_map_document_status_t::INVALID_METRICS );
    REQUIRE( CypherTileMapDocument_SetDescription( &document,
        { 4u, 4u, 2.0f, std::numeric_limits<f32>::infinity() } ) == tile_map_document_status_t::INVALID_METRICS );
    REQUIRE( CypherTileMapDocument_SetDescription( &document, { 4u, 4u, CY_F32_MAX, 3.0f } ) ==
        tile_map_document_status_t::INVALID_METRICS );
    REQUIRE( CypherTileMapDocument_BeginEditGroup( &document, StringView_FromCString( "Brush drag" ) ) ==
        tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_SetDescription( &document, { 8u, 8u, 2.0f, 3.0f } ) ==
        tile_map_document_status_t::INVALID_STATE );
    CypherTileMapDocument_CancelEditGroup( &document );
    REQUIRE( document.nCurrentRevision == revision );
    REQUIRE( document.nNextRevision == nextRevision );
    REQUIRE( CypherTileMapDocument_HistoryCount( &document ) == 1u );
    REQUIRE( CypherTileMapDocument_CanRedo( &document ) );
    REQUIRE( CypherTileMapDocument_Redo( &document ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_PaintCell( &document, { 1, 1 }, { 2, 3u, 0u } ) ==
        tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_SetDescription( &document, { 4u, 4u, 2.0f, CY_F32_MAX } ) ==
        tile_map_document_status_t::INVALID_METRICS );
}

TEST_CASE( "Allocation failures cannot partially publish map properties or destroy redo",
           "[CypherTools][TileMap][Properties][Allocation]" )
{
    for ( usize allowed = 0u; allowed <= 2u; ++allowed ) {
        allocation_state_t state;
        auto allocator = PropertiesAllocator( state );
        {
            document_fixture_t fixture;
            auto &document = fixture.document;
            REQUIRE( CypherTileMapDocument_Init( &document, &allocator, { 4u, 4u, 2.0f, 3.0f } ) ==
                tile_map_document_status_t::OK );
            REQUIRE( CypherTileMapDocument_PaintCell( &document, { 2, 2 }, {} ) == tile_map_document_status_t::OK );
            REQUIRE( CypherTileMapDocument_PaintCell( &document, { 0, 0 }, {} ) == tile_map_document_status_t::OK );
            REQUIRE( CypherTileMapDocument_Undo( &document ) == tile_map_document_status_t::OK );
            const auto revision = document.nCurrentRevision;
            const auto nextRevision = document.nNextRevision;
            const auto *storage = document.cells.pData;
            const auto bytes = state.liveBytes;
            const auto capacity = document.cells.nCapacity;
            state.remaining = allowed;
            const auto status = CypherTileMapDocument_SetDescription( &document, { 16u, 12u, 4.0f, 5.0f } );
            if ( allowed < 2u ) {
                REQUIRE( status == tile_map_document_status_t::ALLOCATION_FAILED );
                RequireDescription( document, { 4u, 4u, 2.0f, 3.0f } );
                REQUIRE( document.cells.pData == storage );
                REQUIRE( document.cells.nCapacity == capacity );
                REQUIRE( document.nCurrentRevision == revision );
                REQUIRE( document.nNextRevision == nextRevision );
                REQUIRE( state.liveBytes == bytes );
                REQUIRE( CypherTileMapDocument_CanRedo( &document ) );
                REQUIRE( CypherTileMapDocument_Redo( &document ) == tile_map_document_status_t::OK );
                REQUIRE( CypherTileMapDocument_CellHasFloor( &document, { 0, 0 } ) );
            } else {
                REQUIRE( status == tile_map_document_status_t::OK );
                RequireDescription( document, { 16u, 12u, 4.0f, 5.0f } );
                REQUIRE_FALSE( CypherTileMapDocument_CanRedo( &document ) );
            }
            REQUIRE( CypherTileMapDocument_HistoryCount( &document ) == 2u );
            REQUIRE( CypherTileMapDocument_CellHasFloor( &document, { 2, 2 } ) );
        }
        REQUIRE( state.outstanding == 0u );
        REQUIRE( state.liveBytes == 0u );
    }
}

TEST_CASE( "Description history traverses without allocation and respects revision exhaustion",
           "[CypherTools][TileMap][Properties][Allocation][History]" )
{
    allocation_state_t state;
    auto allocator = PropertiesAllocator( state );
    {
        document_fixture_t fixture;
        auto &document = fixture.document;
        REQUIRE( CypherTileMapDocument_Init( &document, &allocator, { 4u, 4u, 2.0f, 3.0f } ) ==
            tile_map_document_status_t::OK );
        REQUIRE( CypherTileMapDocument_PaintCell( &document, { 2, 2 }, {} ) == tile_map_document_status_t::OK );
        REQUIRE( CypherTileMapDocument_SetDescription( &document, { 16u, 12u, 4.0f, 5.0f } ) ==
            tile_map_document_status_t::OK );
        REQUIRE( CypherTileMapDocument_SetDescription( &document, { 3u, 3u, 2.0f, 3.0f } ) ==
            tile_map_document_status_t::OK );
        state.remaining = 0u;
        for ( int repeat = 0; repeat < 3; ++repeat ) {
            REQUIRE( CypherTileMapDocument_Undo( &document ) == tile_map_document_status_t::OK );
            RequireDescription( document, { 16u, 12u, 4.0f, 5.0f } );
            REQUIRE( CypherTileMapDocument_Undo( &document ) == tile_map_document_status_t::OK );
            RequireDescription( document, { 4u, 4u, 2.0f, 3.0f } );
            REQUIRE( CypherTileMapDocument_Redo( &document ) == tile_map_document_status_t::OK );
            REQUIRE( CypherTileMapDocument_Redo( &document ) == tile_map_document_status_t::OK );
            REQUIRE( CypherTileMapDocument_CellHasFloor( &document, { 2, 2 } ) );
        }
        const auto revision = document.nCurrentRevision;
        const auto nextRevision = document.nNextRevision;
        REQUIRE( CypherTileMapDocument_SetDescription( &document, { 3u, 3u, 4.0f, 5.0f } ) ==
            tile_map_document_status_t::ALLOCATION_FAILED );
        REQUIRE( document.nCurrentRevision == revision );
        REQUIRE( document.nNextRevision == nextRevision );
        document.nNextRevision = 0u;
        REQUIRE( CypherTileMapDocument_SetDescription( &document, { 4u, 4u, 4.0f, 5.0f } ) ==
            tile_map_document_status_t::HISTORY_LIMIT_REACHED );
        RequireDescription( document, { 3u, 3u, 2.0f, 3.0f } );
        REQUIRE( document.nCurrentRevision == revision );
        REQUIRE( CypherTileMapDocument_HistoryCount( &document ) == 3u );
    }
    REQUIRE( state.outstanding == 0u );
}

TEST_CASE( "Large map dimension history retains one grid and bounded metadata",
           "[CypherTools][TileMap][Properties][History]" )
{
    allocation_state_t state;
    auto allocator = PropertiesAllocator( state );
    {
        document_fixture_t fixture;
        auto &document = fixture.document;
        REQUIRE( CypherTileMapDocument_Init( &document, &allocator,
            { TILE_MAP_MAX_WIDTH, TILE_MAP_MAX_HEIGHT, 2.0f, 3.0f } ) == tile_map_document_status_t::OK );
        const auto baselineBytes = state.liveBytes;
        const auto *storage = document.cells.pData;
        for ( usize i = 0u; i < TILE_MAP_HISTORY_MAX_ENTRIES + 8u; ++i ) {
            const u32 extent = i % 2u == 0u ? 4u : TILE_MAP_MAX_WIDTH;
            REQUIRE( CypherTileMapDocument_SetDescription( &document, { extent, extent, 2.0f, 3.0f } ) ==
                tile_map_document_status_t::OK );
        }
        REQUIRE( document.cells.pData == storage );
        REQUIRE( CypherTileMapDocument_HistoryCount( &document ) == TILE_MAP_HISTORY_MAX_ENTRIES );
        REQUIRE( state.liveBytes < baselineBytes + 64u * 1024u );
        state.remaining = 0u;
        for ( usize i = 0u; i < TILE_MAP_HISTORY_MAX_ENTRIES; ++i ) {
            REQUIRE( CypherTileMapDocument_Undo( &document ) == tile_map_document_status_t::OK );
        }
        REQUIRE( CypherTileMapDocument_Undo( &document ) == tile_map_document_status_t::HISTORY_EMPTY );
        REQUIRE( CypherTileMapDocument_Redo( &document ) == tile_map_document_status_t::OK );
    }
    REQUIRE( state.outstanding == 0u );
    REQUIRE( state.liveBytes == 0u );
}
