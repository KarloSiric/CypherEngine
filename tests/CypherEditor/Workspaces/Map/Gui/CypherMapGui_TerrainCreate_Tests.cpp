//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_TerrainCreate_Tests.cpp
//  Purpose: Verifies bounded terrain creation, authored persistence and
//           failure-atomic publication through the Map workspace.
//
//  History:
//  - Created by Karlo Siric on 2026-10-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMapGui_ToolPanels.h"
#include "CypherGeometry_DocumentSurfaces.h"
#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QToolButton>

#include <cstring>
#include <limits>
#include <memory>
#include <set>
#include <vector>

using namespace cypher;
using namespace cypher::common;
using namespace cypher::editor;
using namespace cypher::editor::map;
namespace geo = cypher::editor::geometry;

namespace
{
struct session_t {
    gui::editor_gui_t gui{};
    map_workspace_t ws{};
    settings_document_t settings{};
    explicit session_t( const allocator_t *allocator = Allocator_GetSystem() )
    {
        auto *app = qobject_cast<QApplication *>( QCoreApplication::instance() );
        REQUIRE( gui::EditorGui_Init( &gui, app, allocator ) == gui::editor_gui_status_t::OK );
        REQUIRE( SettingsDocument_Init( &settings, allocator, EditorSettings_FileIdentity() ) == settings_document_status_t::OK );
        EditorSettings_SetScope( &gui.settings, settings_scope_t::USER, &settings );
        REQUIRE( MapWorkspace_Init( &ws, &gui ) );
        REQUIRE( MapWorkspace_RegisterCommands( &ws, &gui.commands ) == command_registry_status_t::OK );
    }
    ~session_t()
    {
        EditorSettings_SetScope( &gui.settings, settings_scope_t::USER, nullptr );
        MapWorkspace_Shutdown( &ws );
        gui::EditorGui_Shutdown( &gui );
    }
    void SelectCreated( bool value )
    {
        const auto *descriptor = EditorSettings_Find( &gui.settings, StringView_FromCString( "editor.map.select_created" ) );
        REQUIRE( descriptor != nullptr );
        setting_value_t setting{}; setting.type = setting_type_t::BOOL; setting.bValue = value;
        REQUIRE( EditorSettings_Write( &gui.settings, settings_scope_t::USER, *descriptor, setting ) == settings_registry_status_t::OK );
    }
};

command_result_t Run( const session_t &session, const char *command )
{
    return EditorCommands_ExecuteLine( &session.gui.commands, StringView_FromCString( command ) );
}

void Point( math::vec3d_t actual, math::vec3d_t expected )
{
    CHECK( actual.x == expected.x ); CHECK( actual.y == expected.y ); CHECK( actual.z == expected.z );
}

const geo::heightfield_t &Field( const map_workspace_t &ws, u64 id )
{
    const auto *field = geo::GeometryDocument_FindHeightField( &ws.pDocument->geometry, { id } );
    REQUIRE( field != nullptr ); return *field;
}

void CheckField( const map_workspace_t &ws, u64 id, const map_terrain_create_desc_t &desc )
{
    const auto &field = Field( ws, id );
    CHECK( field.sourceId.value == id ); Point( field.origin, desc.origin ); CHECK( field.cellSize == desc.cellSize );
    CHECK( field.cCellsX == desc.cellsX ); CHECK( field.cCellsY == desc.cellsY ); CHECK( field.tileCells == 8u );
    CHECK( field.cTilesX == desc.cellsX / 8u ); CHECK( field.cTilesY == desc.cellsY / 8u );
    REQUIRE( field.heights.nCount == ( static_cast<usize>( desc.cellsX ) + 1u ) * ( desc.cellsY + 1u ) );
    REQUIRE( field.holes.nCount == static_cast<usize>( desc.cellsX ) * desc.cellsY );
    REQUIRE( field.tiles.nCount == static_cast<usize>( desc.cellsX / 8u ) * ( desc.cellsY / 8u ) );
    for ( usize i = 0; i < field.heights.nCount; ++i ) { CHECK( field.heights.pData[i] == 0.0 ); }
    for ( usize i = 0; i < field.holes.nCount; ++i ) { CHECK( field.holes.pData[i] == 0u ); }
    std::set<u64> ids{ id };
    REQUIRE( id != 0u );
    for ( usize i = 0; i < field.tiles.nCount; ++i ) {
        const auto tileId = field.tiles.pData[i].sourceId.value;
        CHECK( tileId != 0u ); CHECK( ids.insert( tileId ).second ); CHECK( tileId < ws.pDocument->nextId );
    }
    CHECK( id < ws.pDocument->nextId );
    const auto *object = MapWireframe_FindObject( ws.wire, id ); REQUIRE( object != nullptr );
    CHECK( object->kind == map_wire_kind_t::TERRAIN ); CHECK( object->owner == 0u ); CHECK( object->iLayer == 0u );
    REQUIRE( object->bounds.bHas ); Point( object->bounds.box.minimum, desc.origin );
    Point( object->bounds.box.maximum, { desc.origin.x + desc.cellsX * desc.cellSize,
        desc.origin.y + desc.cellsY * desc.cellSize, desc.origin.z } );
    CHECK( object->nLines != 0u ); CHECK( object->nPoints != 0u );
}

// Failure checks compare the retained allocation addresses and bytes. A failed
// private edit must not mutate an existing wire cache or consume live identities.
template <typename T> struct vector_state_t {
    const T *address{};
    usize count{}, capacity{};
    std::vector<unsigned char> bytes;
    explicit vector_state_t( const vector_t<T> &source ) : address( source.pData ), count( source.nCount ), capacity( source.nCapacity ),
        bytes( source.nCount * sizeof( T ) )
    {
        if ( !bytes.empty() ) { std::memcpy( bytes.data(), source.pData, bytes.size() ); }
    }
    void Check( const vector_t<T> &source ) const
    {
        CHECK( source.pData == address ); REQUIRE( source.nCount == count ); CHECK( source.nCapacity == capacity );
        if ( !bytes.empty() ) { CHECK( std::memcmp( source.pData, bytes.data(), bytes.size() ) == 0 ); }
    }
};

struct snapshot_t {
    const map_document_t *document{};
    u64 revision{}, nextId{}, nextGeometryId{}, selectionRevision{};
    usize fields{}, steps{}, applied{};
    undo_state_token_t token{};
    bool modified{};
    vector_state_t<u64> selection;
    vector_state_t<map_geometry_record_t> records;
    vector_state_t<math::vec3d_t> points;
    vector_state_t<u64> pointIds;
    vector_state_t<map_wire_line_t> lines;
    vector_state_t<map_wire_object_t> objects;
    vector_state_t<map_wire_face_t> faces;
    vector_state_t<u32> indices;
    explicit snapshot_t( const map_workspace_t &ws ) : document( ws.pDocument ), revision( document->geometry.revision ),
        nextId( document->nextId ), nextGeometryId( document->geometry.sourceIds.allocator.next.value ), selectionRevision( ws.selection.revision ),
        fields( document->geometry.heightFields.nCount ), steps( EditorHistory_StepCount( &ws.history ) ),
        applied( EditorHistory_AppliedStepCount( &ws.history ) ), token( UndoRedo_StateToken( ws.history.pUndo ) ),
        modified( MapWorkspace_IsModified( &ws ) ), selection( ws.selection.ids ), records( document->geometryRecords ),
        points( ws.wire.points ), pointIds( ws.wire.pointSourceIds ), lines( ws.wire.lines ), objects( ws.wire.objects ),
        faces( ws.wire.faces ), indices( ws.wire.faceIndices ) {}
    void Check( const map_workspace_t &ws ) const
    {
        REQUIRE( ws.pDocument == document ); CHECK( document->geometry.revision == revision ); CHECK( document->nextId == nextId );
        CHECK( document->geometry.sourceIds.allocator.next.value == nextGeometryId ); CHECK( document->geometry.heightFields.nCount == fields );
        CHECK( ws.selection.revision == selectionRevision ); CHECK( MapWorkspace_IsModified( &ws ) == modified );
        CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CHECK( EditorHistory_AppliedStepCount( &ws.history ) == applied );
        CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
        selection.Check( ws.selection.ids ); records.Check( document->geometryRecords ); points.Check( ws.wire.points );
        pointIds.Check( ws.wire.pointSourceIds ); lines.Check( ws.wire.lines ); objects.Check( ws.wire.objects );
        faces.Check( ws.wire.faces ); indices.Check( ws.wire.faceIndices );
    }
};

struct allocation_t { usize calls{}, failOn{}, rejected{}, live{}; };
void *Allocate( void *context, usize bytes, usize alignment ) noexcept
{
    auto &audit = *static_cast<allocation_t *>( context );
    if ( ++audit.calls == audit.failOn ) { ++audit.rejected; return nullptr; }
    void *memory = Allocator_Allocate( Allocator_GetSystem(), bytes, alignment );
    if ( memory != nullptr ) { ++audit.live; } return memory;
}
void Free( void *context, void *memory, usize bytes, usize alignment ) noexcept
{
    if ( memory != nullptr ) { --static_cast<allocation_t *>( context )->live; }
    Allocator_Free( Allocator_GetSystem(), memory, bytes, alignment );
}
u64 SeedTerrain( session_t &session )
{
    map_terrain_create_desc_t desc{}; desc.origin = { -512, -256, 32 };
    REQUIRE( MapWorkspace_CreateTerrain( &session.ws, desc ) );
    REQUIRE( session.ws.selection.ids.nCount == 1u ); return session.ws.selection.ids.pData[0];
}
template <typename T> T *Control( QWidget &panel, const char *name )
{
    auto *control = panel.findChild<T *>( QString::fromLatin1( name ) ); REQUIRE( control != nullptr ); return control;
}
} // namespace

TEST_CASE( "Terrain creation authors exact flat dimensions and one reversible edit", "[map][gui][terrain-create]" )
{
    session_t session; auto &ws = session.ws;
    map_terrain_create_desc_t desc{}; desc.origin = { -64, 128, 24 }; desc.cellSize = 32; desc.cellsX = 16; desc.cellsY = 24;
    REQUIRE( MapWorkspace_CanCreateTerrain( &ws, desc ) );
    const auto steps = EditorHistory_StepCount( &ws.history ); const auto materials = ws.pDocument->materials.entries.nCount;
    REQUIRE( MapWorkspace_CreateTerrain( &ws, desc ) ); REQUIRE( ws.selection.ids.nCount == 1u );
    const u64 id = ws.selection.ids.pData[0]; const auto nextId = ws.pDocument->nextId;
    REQUIRE( ws.pDocument->geometry.heightFields.nCount == 1u ); CheckField( ws, id, desc );
    CHECK( ws.pDocument->materials.entries.nCount == materials ); CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1u );
    CHECK( StringView_Equals( EditorHistory_UndoLabel( &ws.history ), StringView_FromCString( "Create Terrain" ) ) );
    CHECK( MapWorkspace_IsModified( &ws ) ); REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    CHECK( ws.pDocument->geometry.heightFields.nCount == 0u ); CHECK( ws.selection.ids.nCount == 0u ); CHECK_FALSE( MapWorkspace_IsModified( &ws ) );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); CheckField( ws, id, desc );
    CHECK( ws.selection.ids.nCount == 1u ); CHECK( ws.selection.ids.pData[0] == id ); CHECK( ws.pDocument->nextId == nextId );
    CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1u );
}

TEST_CASE( "Terrain creation retains selection when requested and allocates distinct field and tile identities", "[map][gui][terrain-create]" )
{
    session_t session; auto &ws = session.ws; const u64 first = SeedTerrain( session );
    const auto firstTile = Field( ws, first ).tiles.pData[0].sourceId.value;
    session.SelectCreated( false ); map_terrain_create_desc_t desc{}; desc.origin = { 128, 64, -16 };
    REQUIRE( MapWorkspace_CreateTerrain( &ws, desc ) ); REQUIRE( ws.pDocument->geometry.heightFields.nCount == 2u );
    REQUIRE( ws.selection.ids.nCount == 1u ); CHECK( ws.selection.ids.pData[0] == first );
    const auto &second = *ws.pDocument->geometry.heightFields.pData[1]; CheckField( ws, second.sourceId.value, desc );
    const std::set<u64> ids{ first, firstTile, second.sourceId.value, second.tiles.pData[0].sourceId.value }; CHECK( ids.size() == 4u );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); CHECK( ws.pDocument->geometry.heightFields.nCount == 1u );
    REQUIRE( ws.selection.ids.nCount == 1u ); CHECK( ws.selection.ids.pData[0] == first );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); REQUIRE( ws.selection.ids.nCount == 1u ); CHECK( ws.selection.ids.pData[0] == first );
}

TEST_CASE( "Terrain source survives save and reopen with exact identities and first-layer placement", "[map][gui][terrain-create][persistence]" )
{
    session_t session; auto &ws = session.ws;
    map_terrain_create_desc_t desc{}; desc.origin = { -128, 96, -48 }; desc.cellSize = 16; desc.cellsX = 24; desc.cellsY = 16;
    REQUIRE( MapWorkspace_CreateTerrain( &ws, desc ) ); const u64 id = ws.selection.ids.pData[0];
    std::vector<u64> tiles; const auto &field = Field( ws, id );
    for ( usize i = 0; i < field.tiles.nCount; ++i ) { tiles.push_back( field.tiles.pData[i].sourceId.value ); }
    QTemporaryDir temporary; REQUIRE( temporary.isValid() ); const auto path = temporary.filePath( QStringLiteral( "terrain.cymap" ) );
    REQUIRE( MapWorkspace_SaveAs( &ws, path ).status == map_files_status_t::OK ); CHECK_FALSE( MapWorkspace_IsModified( &ws ) );
    REQUIRE( MapWorkspace_Open( &ws, path ).status == map_files_status_t::OK ); REQUIRE( ws.pDocument->geometry.heightFields.nCount == 1u );
    CheckField( ws, id, desc ); const auto &reopened = Field( ws, id ); REQUIRE( reopened.tiles.nCount == tiles.size() );
    for ( usize i = 0; i < tiles.size(); ++i ) { CHECK( reopened.tiles.pData[i].sourceId.value == tiles[i] ); }
    CHECK_FALSE( ws.pDocument->bReadOnly ); CHECK_FALSE( MapWorkspace_IsModified( &ws ) );
    REQUIRE( ws.pDocument->geometryRecords.nCount == 1u ); CHECK( ws.pDocument->geometryRecords.pData[0].id == id );
    CHECK( ws.pDocument->geometryRecords.pData[0].iLayer == 0u ); CHECK( ws.pDocument->geometryRecords.pData[0].owner == 0u );
    REQUIRE( MapWorkspace_CreateTerrain( &ws, desc ) ); CHECK( ws.selection.ids.pData[0] != id );
}

TEST_CASE( "Invalid terrain descriptors are rejected without rounding or modifying the map", "[map][gui][terrain-create][validation]" )
{
    session_t session; auto &ws = session.ws; SeedTerrain( session );
    std::vector<map_terrain_create_desc_t> invalid;
    for ( const u32 cells : { 0u, 7u, 9u, 127u, 129u, CY_U32_MAX } ) {
        map_terrain_create_desc_t x{}, y{}; x.cellsX = cells; y.cellsY = cells; invalid.push_back( x ); invalid.push_back( y );
    }
    for ( const f64 spacing : { 0.0, 0.5, 8192.5, std::numeric_limits<f64>::infinity(), std::numeric_limits<f64>::quiet_NaN() } ) {
        map_terrain_create_desc_t desc{}; desc.cellSize = spacing; invalid.push_back( desc );
    }
    for ( const auto origin : { math::vec3d_t{ 1048577, 0, 0 }, math::vec3d_t{ 0, -1048577, 0 }, math::vec3d_t{ 0, 0, 1048577 },
        math::vec3d_t{ 1048576 - 511, 0, 0 }, math::vec3d_t{ 0, 1048576 - 511, 0 },
        math::vec3d_t{ 0, 0, std::numeric_limits<f64>::quiet_NaN() } } ) {
        map_terrain_create_desc_t desc{}; desc.origin = origin; invalid.push_back( desc );
    }
    const snapshot_t original( ws );
    for ( usize i = 0; i < invalid.size(); ++i ) {
        CAPTURE( i ); CHECK_FALSE( MapWorkspace_CanCreateTerrain( &ws, invalid[i] ) );
        CHECK_FALSE( MapWorkspace_CreateTerrain( &ws, invalid[i] ) ); original.Check( ws );
    }
    CHECK_FALSE( MapWorkspace_CanCreateTerrain( nullptr, {} ) ); CHECK_FALSE( MapWorkspace_CreateTerrain( nullptr, {} ) );
    map_terrain_create_desc_t largest{}; largest.cellsX = 128u; largest.cellsY = 128u; largest.cellSize = 1.0;
    CHECK( MapWorkspace_CanCreateTerrain( &ws, largest ) ); original.Check( ws );
    map_terrain_create_desc_t edge{}; edge.origin = { 1048576 - 512, -1048576, 1048576 };
    REQUIRE( MapWorkspace_CreateTerrain( &ws, edge ) ); CheckField( ws, ws.selection.ids.pData[0], edge );
}

TEST_CASE( "Terrain creation obeys authoring guards and preserves private previews", "[map][gui][terrain-create][validation]" )
{
    session_t session; auto &ws = session.ws; SeedTerrain( session );
    const auto reject = [&] {
        const snapshot_t original( ws ); CHECK_FALSE( MapWorkspace_CanCreateTerrain( &ws, {} ) );
        CHECK_FALSE( MapWorkspace_CreateTerrain( &ws, {} ) ); original.Check( ws );
    };
    ws.pDocument->bReadOnly = CY_TRUE; reject(); ws.pDocument->bReadOnly = CY_FALSE;
    map_bounds_t bounds{}; MapBounds_AddPoint( bounds, { 0, 0, 0 } ); MapBounds_AddPoint( bounds, { 64, 64, 64 } );
    MapWorkspace_SetEditPreview( &ws, bounds ); REQUIRE( ws.editPreview.bActive );
    const auto *preview = ws.editPreviewWire.points.pData; const auto primitive = ws.editPreview.primitive;
    reject(); CHECK( ws.editPreview.bActive ); CHECK( ws.editPreviewWire.points.pData == preview );
    Point( ws.editPreview.primitive.bounds.minimum, primitive.bounds.minimum ); Point( ws.editPreview.primitive.bounds.maximum, primitive.bounds.maximum );
    MapWorkspace_ClearEditPreview( &ws );
    REQUIRE( EditorHistory_Begin( &ws.history, StringView_FromCString( "terrain guard" ) ) == editor_history_status_t::OK );
    reject(); CHECK( EditorHistory_IsTransactionOpen( &ws.history ) ); EditorHistory_Cancel( &ws.history );
    const auto nextId = ws.pDocument->nextId; ws.pDocument->nextId = 0u; reject(); ws.pDocument->nextId = nextId;
    const auto nextGeometry = ws.pDocument->geometry.sourceIds.allocator.next;
    ws.pDocument->geometry.sourceIds.allocator.next.value = 0u; reject(); ws.pDocument->geometry.sourceIds.allocator.next = nextGeometry;
    ws.pDocument->nextId = CY_U64_MAX - 1u; reject(); ws.pDocument->nextId = nextId;
    ws.pDocument->geometry.sourceIds.allocator.next.value = CY_U64_MAX - 1u;
    reject(); ws.pDocument->geometry.sourceIds.allocator.next = nextGeometry;
    const auto revision = ws.pDocument->geometry.revision; ws.pDocument->geometry.revision = CY_U64_MAX; reject(); ws.pDocument->geometry.revision = revision;
    const auto layers = ws.pDocument->layers.nCount; ws.pDocument->layers.nCount = 0u; reject(); ws.pDocument->layers.nCount = layers;
    CHECK( MapWorkspace_CanCreateTerrain( &ws, {} ) );
}

TEST_CASE( "Every allocator-backed terrain publication failure leaves the live map and history intact", "[map][gui][terrain-create][allocation][atomic]" )
{
    allocation_t audit{}; const allocator_t allocator{ &Allocate, nullptr, &Free, &audit }; usize successfulCalls{};
    map_terrain_create_desc_t desc{}; desc.origin = { 128, 0, 64 };
    {
        session_t session( &allocator ); SeedTerrain( session ); audit.calls = 0;
        REQUIRE( MapWorkspace_CreateTerrain( &session.ws, desc ) ); successfulCalls = audit.calls; REQUIRE( successfulCalls != 0u );
    }
    REQUIRE( audit.live == 0u );
    for ( usize failure = 1; failure <= successfulCalls; ++failure ) {
        CAPTURE( failure, successfulCalls ); audit.calls = 0; audit.failOn = 0; audit.rejected = 0;
        {
            session_t session( &allocator ); auto &ws = session.ws; SeedTerrain( session );
            const snapshot_t original( ws ); const auto live = audit.live;
            audit.calls = 0; audit.failOn = failure;
            const bool created = MapWorkspace_CreateTerrain( &ws, desc ); audit.failOn = 0;
            CHECK_FALSE( created ); REQUIRE( audit.rejected == 1u ); original.Check( ws ); CHECK( audit.live == live );
            // After the failed publication, a retry must neither skip IDs nor
            // leave a half-finished operation in the history.
            REQUIRE( MapWorkspace_CreateTerrain( &ws, desc ) ); CheckField( ws, ws.selection.ids.pData[0], desc );
            CHECK( ws.selection.ids.pData[0] == original.nextId ); CHECK( EditorHistory_StepCount( &ws.history ) == original.steps + 1u );
        }
        CHECK( audit.live == 0u );
    }
}

TEST_CASE( "The Terrain command opens simple numeric creation; sculpt brushes are offered, painting is not yet", "[map][gui][terrain-create][tool-properties]" )
{
    session_t session; auto &ws = session.ws; MapWorkspace_SetCursor( &ws, { -64, 96, 24 }, 7u );
    const snapshot_t original( ws ); REQUIRE( Run( session, "map.terrain.create" ) == command_result_t::OK );
    CHECK( ws.tool == map_tool_t::TERRAIN ); CHECK( MapWorkspace_IsToolAvailable( map_tool_t::TERRAIN ) ); original.Check( ws );
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &ws ) ); REQUIRE( panel != nullptr );
    auto *x = Control<QDoubleSpinBox>( *panel, "MapTerrainOrigin0" ); auto *y = Control<QDoubleSpinBox>( *panel, "MapTerrainOrigin1" );
    auto *z = Control<QDoubleSpinBox>( *panel, "MapTerrainOrigin2" ); auto *cellsX = Control<QSpinBox>( *panel, "MapTerrainCells0" );
    auto *cellsY = Control<QSpinBox>( *panel, "MapTerrainCells1" ); auto *spacing = Control<QDoubleSpinBox>( *panel, "MapTerrainSpacing" );
    auto *footprint = Control<QLabel>( *panel, "MapTerrainFootprint" ); auto *create = Control<QPushButton>( *panel, "MapTerrainCreate" );
    CHECK( x->value() == -64 ); CHECK( y->value() == 96 ); CHECK( z->value() == 24 );
    CHECK( cellsX->value() == 8 ); CHECK( cellsY->value() == 8 ); CHECK( spacing->value() == 64 );
    REQUIRE_FALSE( footprint->text().isEmpty() );
    const QString capturePath = qEnvironmentVariable( "CYPHER_MASON_TERRAIN_CAPTURE_PATH" );
    if ( !capturePath.isEmpty() ) {
        panel->setFixedSize( 340, 900 ); panel->show(); QCoreApplication::processEvents();
        REQUIRE( panel->grab().save( capturePath ) );
    }
    // The sculpt brushes choose the Terrain tool's mode (they change no map
    // data until a stroke); material painting is still a placeholder.
    for ( const char *command : { "map.terrain.raise", "map.terrain.lower", "map.terrain.smooth", "map.terrain.flatten" } ) {
        CAPTURE( command ); CHECK( Control<QToolButton>( *panel, command )->isEnabled() );
    }
    CHECK( Run( session, "map.terrain.paint" ) == command_result_t::DISABLED );
    CHECK_FALSE( Control<QToolButton>( *panel, "map.terrain.paint" )->isEnabled() );
    CHECK_FALSE( MapToolProperties_Options( panel.get() ).contains( QStringLiteral( "editor.map.default_material" ) ) );
    cellsX->setValue( 12 ); CHECK_FALSE( create->isEnabled() ); CHECK_FALSE( Control<QLabel>( *panel, "MapTerrainFeedback" )->text().isEmpty() );
    cellsX->setValue( 16 ); cellsY->setValue( 24 ); spacing->setValue( 32 ); x->setValue( 128 ); y->setValue( -64 ); z->setValue( 48 );
    x->setValue( 1048576 - 511 ); CHECK_FALSE( create->isEnabled() );
    CHECK_FALSE( Control<QLabel>( *panel, "MapTerrainFeedback" )->text().isEmpty() ); x->setValue( 128 );
    MapWorkspace_DocumentChanged( &ws );
    CHECK( Control<QDoubleSpinBox>( *panel, "MapTerrainOrigin0" )->value() == 128 );
    CHECK( Control<QDoubleSpinBox>( *panel, "MapTerrainOrigin1" )->value() == -64 );
    CHECK( Control<QDoubleSpinBox>( *panel, "MapTerrainOrigin2" )->value() == 48 );
    CHECK( Control<QSpinBox>( *panel, "MapTerrainCells0" )->value() == 16 );
    CHECK( Control<QSpinBox>( *panel, "MapTerrainCells1" )->value() == 24 );
    CHECK( Control<QDoubleSpinBox>( *panel, "MapTerrainSpacing" )->value() == 32 );
    REQUIRE( create->isEnabled() ); const auto steps = EditorHistory_StepCount( &ws.history ); create->click();
    REQUIRE( ws.selection.ids.nCount == 1u ); map_terrain_create_desc_t desc{};
    desc.origin = { 128, -64, 48 }; desc.cellsX = 16; desc.cellsY = 24; desc.cellSize = 32;
    CheckField( ws, ws.selection.ids.pData[0], desc ); CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1u );
}
