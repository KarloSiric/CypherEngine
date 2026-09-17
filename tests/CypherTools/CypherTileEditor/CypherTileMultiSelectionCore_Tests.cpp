//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Verify sparse selection transforms and atomic mixed-property edits.
//////////////////////////////////////////////////////////////////////////
#include "CypherTileMapDocument.h"

#include <catch2/catch_test_macros.hpp>
#include <vector>

using namespace cypher::common;
using namespace cypher::tools::tile_editor;

namespace {

struct fixture_t {
    tile_map_document_t document{};
    explicit fixture_t( const allocator_t *allocator = Allocator_GetSystem(), u32 width = 12u, u32 height = 12u )
    {
        REQUIRE( CypherTileMapDocument_Init( &document, allocator, { width, height, 2.0f, 3.0f } ) ==
            tile_map_document_status_t::OK );
    }
    ~fixture_t() { CypherTileMapDocument_Shutdown( &document ); }
    void paint( i32 x, i32 y, tile_map_paint_t paint = {} )
    {
        REQUIRE( CypherTileMapDocument_PaintCell( &document, { x, y }, paint ) == tile_map_document_status_t::OK );
    }
    tile_map_cell_t &cell( i32 x, i32 y ) { return *CypherTileMapDocument_CellAt( &document, { x, y } ); }
    unique_id_t door( i32 x, i32 y, tile_map_marker_side_t side = tile_map_marker_side_t::NORTH )
    {
        unique_id_t id{};
        REQUIRE( CypherTileMapDocument_PlaceDoor( &document, { x, y }, side, &id ) == tile_map_document_status_t::OK );
        return id;
    }
    unique_id_t spawn( i32 x, i32 y, float yaw = 0.0f )
    {
        REQUIRE( CypherTileMapDocument_PlacePlayerSpawn( &document, { x, y }, yaw ) == tile_map_document_status_t::OK );
        return CypherTileMapDocument_PlayerSpawn( &document )->id;
    }
};

void CheckCell( const tile_map_cell_t &actual, const tile_map_cell_t &expected )
{
    CHECK( actual.nFloorLevel == expected.nFloorLevel );
    CHECK( actual.nWallHeightLevels == expected.nWallHeightLevels );
    CHECK( actual.nMaterialSlot == expected.nMaterialSlot );
    CHECK( actual.flags == expected.flags );
    CHECK( actual.shape == expected.shape );
    CHECK( actual.nStairSteps == expected.nStairSteps );
}

struct snapshot_t {
    std::vector<tile_map_cell_t> cells;
    std::vector<tile_map_marker_t> markers;
    u64 revision;
    usize history;
    explicit snapshot_t( const tile_map_document_t &document )
        : cells( document.cells.pData, document.cells.pData + document.cells.nCount ),
          revision( document.nCurrentRevision ), history( CypherTileMapDocument_HistoryCount( &document ) )
    {
        for ( usize i = 0u; i < document.markers.nCount; ++i ) markers.push_back( document.markers.pData[i] );
    }
    void contents( const tile_map_document_t &document ) const
    {
        REQUIRE( document.cells.nCount == cells.size() );
        REQUIRE( document.markers.nCount == markers.size() );
        for ( usize i = 0u; i < cells.size(); ++i ) CheckCell( document.cells.pData[i], cells[i] );
        for ( usize i = 0u; i < markers.size(); ++i ) {
            const auto &actual = document.markers.pData[i];
            const auto &expected = markers[i];
            CHECK( UniqueId_Equals( actual.id, expected.id ) );
            CHECK( actual.cell.x == expected.cell.x );
            CHECK( actual.cell.y == expected.cell.y );
            CHECK( actual.kind == expected.kind );
            CHECK( actual.side == expected.side );
            CHECK( actual.yawDegrees == expected.yawDegrees );
        }
    }
    void unchanged( const tile_map_document_t &document ) const
    {
        contents( document );
        CHECK( document.nCurrentRevision == revision );
        CHECK( CypherTileMapDocument_HistoryCount( &document ) == history );
    }
};

struct allocation_state_t { usize remaining{ CY_USIZE_MAX }; usize outstanding{ 0u }; };
void *Allocate( void *user, usize bytes, usize alignment ) noexcept
{
    auto &state = *static_cast<allocation_state_t *>( user );
    if ( state.remaining == 0u ) return nullptr;
    if ( state.remaining != CY_USIZE_MAX ) --state.remaining;
    void *memory = Allocator_Allocate( Allocator_GetSystem(), bytes, alignment );
    if ( memory ) ++state.outstanding;
    return memory;
}
void Free( void *user, void *memory, usize bytes, usize alignment ) noexcept
{
    if ( memory ) --static_cast<allocation_state_t *>( user )->outstanding;
    Allocator_Free( Allocator_GetSystem(), memory, bytes, alignment );
}

} // namespace

TEST_CASE( "Sparse selection movement deduplicates cells preserves holes and supports overlap",
    "[TileEditor][MultiSelection]" )
{
    fixture_t fixture;
    auto &document = fixture.document;
    fixture.paint( 1, 1, { 2, 3u, 7u } );
    fixture.paint( 2, 1, { 4, 1u, 6u, tile_map_cell_shape_t::STAIRS_WEST, 12u } );
    fixture.paint( 4, 2, { -1, 5u, 3u } );
    fixture.paint( 3, 2, { 9, 2u, 4u } ); // An unselected hole in the bounding rectangle.
    const auto stair = fixture.cell( 2, 1 );
    const auto untouched = fixture.cell( 3, 2 );
    const auto spawn = fixture.spawn( 1, 1 );
    const auto door = fixture.door( 4, 2 );
    CypherTileMapDocument_MarkSaved( &document );
    const snapshot_t before( document );
    const tile_map_grid_coord_t points[]{ { 4, 2 }, { 1, 1 }, { 2, 1 }, { 1, 1 } };
    REQUIRE( CypherTileMapDocument_MoveSelection( &document, { points, 4u }, 1, 0 ) == tile_map_document_status_t::OK );
    CHECK( CypherTileMapDocument_HistoryCount( &document ) == before.history + 1u );
    CHECK( CypherTileMapCell_IsCanonicalEmpty( fixture.cell( 1, 1 ) ) );
    CHECK( CypherTileMapCell_IsCanonicalEmpty( fixture.cell( 4, 2 ) ) );
    CheckCell( fixture.cell( 3, 1 ), stair );
    CheckCell( fixture.cell( 3, 2 ), untouched );
    REQUIRE( CypherTileMapDocument_PlayerSpawn( &document ) != nullptr );
    CHECK( UniqueId_Equals( CypherTileMapDocument_PlayerSpawn( &document )->id, spawn ) );
    CHECK( CypherTileMapDocument_PlayerSpawn( &document )->cell.x == 2 );
    CHECK( CypherTileMapDocument_DoorById( &document, door )->cell.x == 5 );
    const snapshot_t moved( document );
    REQUIRE( CypherTileMapDocument_Undo( &document ) == tile_map_document_status_t::OK );
    before.contents( document );
    CHECK_FALSE( CypherTileMapDocument_IsDirty( &document ) );
    REQUIRE( CypherTileMapDocument_Redo( &document ) == tile_map_document_status_t::OK );
    moved.contents( document );
}

TEST_CASE( "Sparse selection copy allows occupied unselected holes and copies door identities once",
    "[TileEditor][MultiSelection]" )
{
    fixture_t fixture;
    auto &document = fixture.document;
    fixture.paint( 1, 1, { -2, 3u, 4u } );
    fixture.paint( 3, 2, { 1, 2u, 7u } );
    fixture.paint( 7, 1, { 8, 5u, 6u } );
    const auto hole = fixture.cell( 7, 1 );
    const auto originalDoor = fixture.door( 3, 2 );
    const auto spawn = fixture.spawn( 1, 1 );
    const snapshot_t before( document );
    const tile_map_grid_coord_t points[]{ { 1, 1 }, { 3, 2 }, { 3, 2 } };
    REQUIRE( CypherTileMapDocument_CopySelection( &document, { points, 3u }, 5, 0 ) == tile_map_document_status_t::OK );
    CheckCell( fixture.cell( 6, 1 ), fixture.cell( 1, 1 ) );
    CheckCell( fixture.cell( 8, 2 ), fixture.cell( 3, 2 ) );
    CheckCell( fixture.cell( 7, 1 ), hole );
    REQUIRE( document.markers.nCount == 3u );
    REQUIRE( CypherTileMapDocument_DoorAt( &document, { 8, 2 }, tile_map_marker_side_t::NORTH ) != nullptr );
    const auto copyId = CypherTileMapDocument_DoorAt( &document, { 8, 2 }, tile_map_marker_side_t::NORTH )->id;
    CHECK_FALSE( UniqueId_Equals( copyId, originalDoor ) );
    CHECK( UniqueId_Equals( CypherTileMapDocument_PlayerSpawn( &document )->id, spawn ) );
    CHECK( CypherTileMapDocument_PlayerSpawn( &document )->cell.x == 1 );
    REQUIRE( CypherTileMapDocument_Undo( &document ) == tile_map_document_status_t::OK );
    before.contents( document );
    REQUIRE( CypherTileMapDocument_Redo( &document ) == tile_map_document_status_t::OK );
    CHECK( CypherTileMapDocument_DoorById( &document, copyId ) != nullptr );
}

TEST_CASE( "Sparse selection delete removes only selected cells and their markers",
    "[TileEditor][MultiSelection]" )
{
    fixture_t fixture;
    auto &document = fixture.document;
    fixture.paint( 1, 1 ); fixture.paint( 2, 1 ); fixture.paint( 4, 3 );
    const auto first = fixture.door( 1, 1 );
    const auto outside = fixture.door( 2, 1 );
    const auto last = fixture.door( 4, 3 );
    fixture.spawn( 4, 3 );
    const snapshot_t before( document );
    const tile_map_grid_coord_t points[]{ { 4, 3 }, { 1, 1 }, { 4, 3 } };
    REQUIRE( CypherTileMapDocument_DeleteSelection( &document, { points, 3u } ) == tile_map_document_status_t::OK );
    CHECK( document.markers.nCount == 1u );
    CHECK( CypherTileMapDocument_DoorById( &document, outside ) != nullptr );
    CHECK( CypherTileMapDocument_DoorById( &document, first ) == nullptr );
    CHECK( CypherTileMapDocument_DoorById( &document, last ) == nullptr );
    CHECK( CypherTileMapDocument_PlayerSpawn( &document ) == nullptr );
    CHECK( CypherTileMapDocument_CellHasFloor( &document, { 2, 1 } ) );
    REQUIRE( CypherTileMapDocument_Undo( &document ) == tile_map_document_status_t::OK );
    before.contents( document );
    REQUIRE( CypherTileMapDocument_Redo( &document ) == tile_map_document_status_t::OK );
    CHECK( document.markers.nCount == 1u );
}

TEST_CASE( "Sparse rotation uses selected bounds and rotates stairs doors and spawn heading",
    "[TileEditor][MultiSelection]" )
{
    fixture_t fixture;
    auto &document = fixture.document;
    fixture.paint( 1, 1 );
    fixture.paint( 3, 1, { 2, 3u, 7u, tile_map_cell_shape_t::STAIRS_NORTH, 12u } );
    fixture.paint( 1, 2 );
    fixture.paint( 3, 2, { 7, 4u, 2u } );
    const auto hole = fixture.cell( 3, 2 );
    fixture.spawn( 1, 1, 350.0f );
    const auto doorId = fixture.door( 1, 2, tile_map_marker_side_t::WEST );
    const snapshot_t before( document );
    const tile_map_grid_coord_t points[]{ { 1, 1 }, { 3, 1 }, { 1, 2 }, { 3, 1 } };
    REQUIRE( CypherTileMapDocument_RotateSelectionClockwise( &document, { points, 4u } ) == tile_map_document_status_t::OK );
    CHECK( fixture.cell( 2, 3 ).shape == tile_map_cell_shape_t::STAIRS_EAST );
    CHECK( fixture.cell( 2, 3 ).nStairSteps == 12u );
    CheckCell( fixture.cell( 3, 2 ), hole );
    const auto *door = CypherTileMapDocument_DoorById( &document, doorId );
    REQUIRE( door != nullptr );
    CHECK( door->cell.x == 1 ); CHECK( door->cell.y == 1 );
    CHECK( door->side == tile_map_marker_side_t::NORTH );
    const auto *spawn = CypherTileMapDocument_PlayerSpawn( &document );
    REQUIRE( spawn != nullptr );
    CHECK( spawn->cell.x == 2 ); CHECK( spawn->cell.y == 1 );
    CHECK( spawn->yawDegrees == 80.0f );
    const snapshot_t rotated( document );
    REQUIRE( CypherTileMapDocument_Undo( &document ) == tile_map_document_status_t::OK );
    before.contents( document );
    REQUIRE( CypherTileMapDocument_Redo( &document ) == tile_map_document_status_t::OK );
    rotated.contents( document );
}

TEST_CASE( "Selective property edits preserve mixed unedited properties and do not create floors implicitly",
    "[TileEditor][MultiSelection]" )
{
    fixture_t fixture;
    auto &document = fixture.document;
    fixture.paint( 1, 1, { -3, 2u, 5u, tile_map_cell_shape_t::STAIRS_WEST, 12u } );
    fixture.paint( 3, 2, { 8, 7u, 6u, tile_map_cell_shape_t::FLAT, 20u } );
    const auto first = fixture.cell( 1, 1 );
    const auto second = fixture.cell( 3, 2 );
    const snapshot_t before( document );
    const tile_map_grid_coord_t points[]{ { 1, 1 }, { 3, 2 }, { 2, 2 }, { 1, 1 } };
    tile_map_selection_patch_t patch{};
    patch.fields = TILE_MAP_SELECTION_PROPERTY_MATERIAL | TILE_MAP_SELECTION_PROPERTY_STAIR_STEPS;
    patch.nMaterialSlot = 42u;
    patch.nStairSteps = 6u;
    patch.nFloorLevel = 100; // These values are intentionally unflagged.
    patch.nWallHeightLevels = 0u;
    patch.shape = static_cast<tile_map_cell_shape_t>( 255u );
    REQUIRE( CypherTileMapDocument_ApplySelectionProperties( &document, { points, 4u }, patch ) == tile_map_document_status_t::OK );
    auto firstExpected = first; firstExpected.nMaterialSlot = 42u; firstExpected.nStairSteps = 6u;
    auto secondExpected = second; secondExpected.nMaterialSlot = 42u; secondExpected.nStairSteps = 6u;
    CheckCell( fixture.cell( 1, 1 ), firstExpected );
    CheckCell( fixture.cell( 3, 2 ), secondExpected );
    CHECK( CypherTileMapCell_IsCanonicalEmpty( fixture.cell( 2, 2 ) ) );
    CHECK( CypherTileMapDocument_HistoryCount( &document ) == before.history + 1u );
    REQUIRE( CypherTileMapDocument_Undo( &document ) == tile_map_document_status_t::OK );
    before.contents( document );
    REQUIRE( CypherTileMapDocument_Redo( &document ) == tile_map_document_status_t::OK );
    CheckCell( fixture.cell( 3, 2 ), secondExpected );
}

TEST_CASE( "Floor property toggle creates defaults plus patch and erases owned markers atomically",
    "[TileEditor][MultiSelection]" )
{
    fixture_t fixture;
    auto &document = fixture.document;
    fixture.paint( 1, 1, { 4, 3u, 6u } );
    fixture.spawn( 1, 1 );
    fixture.door( 1, 1 );
    const auto existing = fixture.cell( 1, 1 );
    const tile_map_grid_coord_t points[]{ { 1, 1 }, { 3, 2 } };
    tile_map_selection_patch_t patch{};
    patch.fields = TILE_MAP_SELECTION_PROPERTY_FLOOR_ENABLED | TILE_MAP_SELECTION_PROPERTY_MATERIAL;
    patch.nMaterialSlot = 7u;
    patch.nFloorLevel = 100; // Not flagged: new floors use document defaults.
    patch.nWallHeightLevels = 55u;
    REQUIRE( CypherTileMapDocument_ApplySelectionProperties( &document, { points, 2u }, patch ) == tile_map_document_status_t::OK );
    CHECK( fixture.cell( 3, 2 ).flags == TILE_MAP_CELL_FLAG_FLOOR );
    CHECK( fixture.cell( 3, 2 ).nFloorLevel == 0 );
    CHECK( fixture.cell( 3, 2 ).nWallHeightLevels == 1u );
    CHECK( fixture.cell( 3, 2 ).nMaterialSlot == 7u );
    CHECK( fixture.cell( 1, 1 ).nFloorLevel == existing.nFloorLevel );
    CHECK( fixture.cell( 1, 1 ).nWallHeightLevels == existing.nWallHeightLevels );
    const snapshot_t enabled( document );
    patch.bFloorEnabled = CY_FALSE;
    REQUIRE( CypherTileMapDocument_ApplySelectionProperties( &document, { points, 2u }, patch ) == tile_map_document_status_t::OK );
    CHECK( document.markers.nCount == 0u );
    CHECK( CypherTileMapCell_IsCanonicalEmpty( fixture.cell( 1, 1 ) ) );
    CHECK( CypherTileMapCell_IsCanonicalEmpty( fixture.cell( 3, 2 ) ) );
    REQUIRE( CypherTileMapDocument_Undo( &document ) == tile_map_document_status_t::OK );
    enabled.contents( document );
    REQUIRE( CypherTileMapDocument_Redo( &document ) == tile_map_document_status_t::OK );
    CHECK( document.markers.nCount == 0u );
}

TEST_CASE( "Sparse edits validate all coordinates destinations and properties before mutation",
    "[TileEditor][MultiSelection]" )
{
    fixture_t fixture;
    auto &document = fixture.document;
    fixture.paint( 1, 1 ); fixture.paint( 3, 2 ); fixture.paint( 8, 2 );
    fixture.door( 6, 1 ); // An orphaned marker still owns its destination.
    const snapshot_t before( document );
    const tile_map_grid_coord_t points[]{ { 1, 1 }, { 3, 2 } };
    const tile_map_grid_coord_t invalid[]{ { 1, 1 }, { -1, 2 } };
    CHECK( CypherTileMapDocument_MoveSelection( &document, { invalid, 2u }, 0, 0 ) == tile_map_document_status_t::OUT_OF_BOUNDS );
    CHECK( CypherTileMapDocument_DeleteSelection( &document, { nullptr, 1u } ) == tile_map_document_status_t::INVALID_ARGUMENT );
    CHECK( CypherTileMapDocument_MoveSelection( &document, { points, 2u }, CY_I32_MAX, 0 ) == tile_map_document_status_t::OUT_OF_BOUNDS );
    CHECK( CypherTileMapDocument_MoveSelection( &document, { points, 2u }, CY_I32_MIN, 0 ) == tile_map_document_status_t::OUT_OF_BOUNDS );
    CHECK( CypherTileMapDocument_MoveSelection( &document, { points, 2u }, 5, 0 ) == tile_map_document_status_t::INVALID_STATE );
    CHECK( CypherTileMapDocument_CopySelection( &document, { points, 2u }, 0, 0 ) == tile_map_document_status_t::INVALID_STATE );
    CHECK( CypherTileMapDocument_CopySelection( &document, { points, 1u }, 5, 0 ) == tile_map_document_status_t::INVALID_STATE );
    tile_map_selection_patch_t patch{};
    patch.fields = TILE_MAP_SELECTION_PROPERTY_WALL_HEIGHT;
    patch.nWallHeightLevels = 0u;
    CHECK( CypherTileMapDocument_ApplySelectionProperties( &document, { points, 2u }, patch ) == tile_map_document_status_t::INVALID_CELL );
    patch.fields = CYPHER_BIT32( 31 );
    CHECK( CypherTileMapDocument_ApplySelectionProperties( &document, { points, 2u }, patch ) == tile_map_document_status_t::INVALID_ARGUMENT );
    before.unchanged( document );
}

TEST_CASE( "Floor and wall height operations deduplicate and reject overflow for the complete selection",
    "[TileEditor][MultiSelection]" )
{
    fixture_t fixture;
    auto &document = fixture.document;
    fixture.paint( 1, 1, { 2, 4u, 3u } );
    fixture.paint( 3, 2, { CY_I16_MAX - 1, CY_U16_MAX, 7u, tile_map_cell_shape_t::STAIRS_SOUTH, 12u } );
    const tile_map_grid_coord_t points[]{ { 1, 1 }, { 3, 2 }, { 1, 1 }, { 4, 2 } };
    const snapshot_t before( document );
    CHECK( CypherTileMapDocument_AdjustSelectionFloorLevel( &document, { points, 4u }, 1 ) == tile_map_document_status_t::INVALID_CELL );
    CHECK( CypherTileMapDocument_AdjustSelectionWallHeight( &document, { points, 4u }, 1 ) == tile_map_document_status_t::INVALID_CELL );
    CHECK( CypherTileMapDocument_AdjustSelectionWallHeight( &document, { points, 4u }, -4 ) == tile_map_document_status_t::INVALID_CELL );
    CHECK( CypherTileMapDocument_AdjustSelectionFloorLevel( &document, { points, 4u }, CY_I32_MIN ) == tile_map_document_status_t::INVALID_CELL );
    tile_map_selection_patch_t patch{};
    patch.fields = TILE_MAP_SELECTION_PROPERTY_FLOOR_LEVEL;
    patch.nFloorLevel = CY_I16_MAX;
    CHECK( CypherTileMapDocument_ApplySelectionProperties( &document, { points, 4u }, patch ) == tile_map_document_status_t::INVALID_CELL );
    before.unchanged( document );
    REQUIRE( CypherTileMapDocument_AdjustSelectionFloorLevel( &document, { points, 4u }, -1 ) == tile_map_document_status_t::OK );
    CHECK( fixture.cell( 1, 1 ).nFloorLevel == 1 );
    CHECK( fixture.cell( 3, 2 ).nFloorLevel == CY_I16_MAX - 2 );
    REQUIRE( CypherTileMapDocument_AdjustSelectionWallHeight( &document, { points, 4u }, -1 ) == tile_map_document_status_t::OK );
    CHECK( fixture.cell( 1, 1 ).nWallHeightLevels == 3u );
    CHECK( fixture.cell( 3, 2 ).nWallHeightLevels == CY_U16_MAX - 1u );
    CHECK( CypherTileMapCell_IsCanonicalEmpty( fixture.cell( 4, 2 ) ) );
    REQUIRE( CypherTileMapDocument_Undo( &document ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_Undo( &document ) == tile_map_document_status_t::OK );
    before.contents( document );
}

TEST_CASE( "Sparse no-ops preserve redo and selection commands cannot join another edit group",
    "[TileEditor][MultiSelection]" )
{
    fixture_t fixture;
    auto &document = fixture.document;
    fixture.paint( 1, 1 ); fixture.paint( 2, 1 );
    REQUIRE( CypherTileMapDocument_Undo( &document ) == tile_map_document_status_t::OK );
    const snapshot_t before( document );
    const tile_map_grid_coord_t points[]{ { 1, 1 } };
    const tile_map_grid_coord_t empty[]{ { 4, 4 }, { 5, 5 } };
    CHECK( CypherTileMapDocument_MoveSelection( &document, { points, 1u }, 0, 0 ) == tile_map_document_status_t::OK );
    CHECK( CypherTileMapDocument_DeleteSelection( &document, { empty, 2u } ) == tile_map_document_status_t::OK );
    CHECK( CypherTileMapDocument_DeleteSelection( &document, {} ) == tile_map_document_status_t::OK );
    CHECK( CypherTileMapDocument_AdjustSelectionFloorLevel( &document, { points, 1u }, 0 ) == tile_map_document_status_t::OK );
    CHECK( CypherTileMapDocument_AdjustSelectionWallHeight( &document, { points, 1u }, 0 ) == tile_map_document_status_t::OK );
    CHECK( CypherTileMapDocument_ApplySelectionProperties( &document, { points, 1u }, {} ) == tile_map_document_status_t::OK );
    before.unchanged( document );
    CHECK( CypherTileMapDocument_CanRedo( &document ) );
    REQUIRE( CypherTileMapDocument_BeginEditGroup( &document, StringView_FromCString( "Other edit" ) ) == tile_map_document_status_t::OK );
    CHECK( CypherTileMapDocument_MoveSelection( &document, { points, 1u }, 1, 0 ) == tile_map_document_status_t::INVALID_STATE );
    CHECK( CypherTileMapDocument_CopySelection( &document, { points, 1u }, 1, 0 ) == tile_map_document_status_t::INVALID_STATE );
    CHECK( CypherTileMapDocument_DeleteSelection( &document, { points, 1u } ) == tile_map_document_status_t::INVALID_STATE );
    CHECK( CypherTileMapDocument_RotateSelectionClockwise( &document, { points, 1u } ) == tile_map_document_status_t::INVALID_STATE );
    CHECK( CypherTileMapDocument_AdjustSelectionFloorLevel( &document, { points, 1u }, 1 ) == tile_map_document_status_t::INVALID_STATE );
    CHECK( CypherTileMapDocument_AdjustSelectionWallHeight( &document, { points, 1u }, 1 ) == tile_map_document_status_t::INVALID_STATE );
    CHECK( CypherTileMapDocument_ApplySelectionProperties( &document, { points, 1u }, {} ) == tile_map_document_status_t::INVALID_STATE );
    before.unchanged( document );
    CHECK( CypherTileMapDocument_IsEditGroupOpen( &document ) );
    CypherTileMapDocument_CancelEditGroup( &document );
}

TEST_CASE( "Sparse selection failures at every allocation preserve cells markers history and redo",
    "[TileEditor][MultiSelection]" )
{
    for ( const bool copy : { false, true } ) {
        bool succeeded = false;
        usize failures = 0u;
        for ( usize permitted = 0u; permitted < 40u; ++permitted ) {
            allocation_state_t state;
            const allocator_t allocator{ &Allocate, nullptr, &Free, &state };
            {
                fixture_t fixture( &allocator );
                auto &document = fixture.document;
                fixture.paint( 1, 1 ); fixture.paint( 3, 2 );
                fixture.door( 1, 1 ); fixture.door( 3, 2 );
                fixture.paint( 5, 5 );
                REQUIRE( CypherTileMapDocument_Undo( &document ) == tile_map_document_status_t::OK );
                const snapshot_t before( document );
                const tile_map_grid_coord_t points[]{ { 1, 1 }, { 3, 2 }, { 3, 2 } };
                tile_map_selection_patch_t patch{};
                patch.fields = TILE_MAP_SELECTION_PROPERTY_FLOOR_ENABLED;
                patch.bFloorEnabled = CY_FALSE;
                state.remaining = permitted;
                const auto status = copy ? CypherTileMapDocument_CopySelection( &document, { points, 3u }, 5, 0 )
                    : CypherTileMapDocument_ApplySelectionProperties( &document, { points, 3u }, patch );
                state.remaining = CY_USIZE_MAX;
                CHECK_FALSE( CypherTileMapDocument_IsEditGroupOpen( &document ) );
                if ( status == tile_map_document_status_t::OK ) {
                    succeeded = true;
                    CHECK( document.markers.nCount == ( copy ? 4u : 0u ) );
                    CHECK_FALSE( CypherTileMapDocument_CanRedo( &document ) );
                } else {
                    ++failures;
                    REQUIRE( status == tile_map_document_status_t::ALLOCATION_FAILED );
                    before.unchanged( document );
                    CHECK( CypherTileMapDocument_CanRedo( &document ) );
                }
            }
            CHECK( state.outstanding == 0u );
            if ( succeeded ) break;
        }
        CHECK( succeeded );
        CHECK( failures >= 4u );
    }
}

TEST_CASE( "Large duplicate selections apply a height delta only once per cell",
    "[TileEditor][MultiSelection]" )
{
    fixture_t fixture;
    fixture.paint( 1, 1 );
    std::vector<tile_map_grid_coord_t> points( 10000u, { 1, 1 } );
    const usize before = CypherTileMapDocument_HistoryCount( &fixture.document );
    REQUIRE( CypherTileMapDocument_AdjustSelectionFloorLevel( &fixture.document,
        { points.data(), points.size() }, 1 ) == tile_map_document_status_t::OK );
    CHECK( fixture.cell( 1, 1 ).nFloorLevel == 1 );
    CHECK( CypherTileMapDocument_HistoryCount( &fixture.document ) == before + 1u );
}
