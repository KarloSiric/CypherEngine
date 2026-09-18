//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Verify selection-driven material inspection without asset reloads.
//////////////////////////////////////////////////////////////////////////
#include "CypherTileSelectionMaterialInspector.h"
#include "CypherTileCanvas.h"
#include "CypherTileDocumentBridge.h"
#include "CypherTileEditorMainWindow.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>

using namespace cypher::common;
using namespace cypher::tools::tile_editor;

namespace
{

void EnsureSelectionMaterialApplication()
{
    if ( QApplication::instance() != nullptr ) return;
    qputenv( "QT_QPA_PLATFORM", "offscreen" );
    static int argc = 1;
    static char name[] = "CypherTileSelectionMaterialInspectorTests";
    static char *argv[]{ name, nullptr };
    static QApplication application( argc, argv );
}

struct fixture_t {
    tile_map_document_t document{};

    fixture_t()
    {
        REQUIRE( CypherTileMapDocument_Init( &document, Allocator_GetSystem(),
            { 4u, 4u, 2.0f, 3.0f } ) == tile_map_document_status_t::OK );
    }

    ~fixture_t() { CypherTileMapDocument_Shutdown( &document ); }

    void paint( tile_map_grid_coord_t coordinate, u16 slot )
    {
        tile_map_paint_t paint{};
        paint.nMaterialSlot = slot;
        REQUIRE( CypherTileMapDocument_PaintCell( &document, coordinate, paint ) ==
            tile_map_document_status_t::OK );
    }
};

template <typename Widget>
Widget *Find( QWidget &root, const char *name )
{
    auto *widget = root.findChild<Widget *>( QString::fromLatin1( name ) );
    REQUIRE( widget != nullptr );
    return widget;
}

} // namespace

TEST_CASE( "Selection material inspection distinguishes empty, built-in, and mixed surfaces",
    "[TileEditor][SelectionMaterial]" )
{
    fixture_t fixture;
    CHECK( TileSelectionMaterial_Inspect( &fixture.document, {}, nullptr ).state ==
        tile_selection_material_state_t::NO_SELECTION );
    const tile_map_grid_coord_t empty[]{ { 0, 0 } };
    CHECK( TileSelectionMaterial_Inspect( &fixture.document, empty, nullptr ).state ==
        tile_selection_material_state_t::NO_SURFACES );

    fixture.paint( { 1, 1 }, 3u );
    const tile_map_grid_coord_t one[]{ { 1, 1 } };
    const auto builtIn = TileSelectionMaterial_Inspect( &fixture.document, one, nullptr );
    CHECK( builtIn.state == tile_selection_material_state_t::BUILTIN );
    CHECK( builtIn.nSlot == 3u );
    CHECK( builtIn.name == QStringLiteral( "Hazard Yellow" ) );
    CHECK( builtIn.stableId == QStringLiteral( "cypher.blockout.hazard" ) );
    CHECK( builtIn.path.isEmpty() );

    fixture.paint( { 2, 1 }, 6u );
    const tile_map_grid_coord_t two[]{ { 1, 1 }, { 2, 1 } };
    const auto mixed = TileSelectionMaterial_Inspect( &fixture.document, two, nullptr );
    CHECK( mixed.state == tile_selection_material_state_t::MIXED );
    CHECK( mixed.nFloorCells == 2u );
    CHECK( mixed.nDistinctSlots == 2u );
    CHECK( mixed.slotsText == QStringLiteral( "3, 6" ) );
    CHECK( mixed.swatches.size() == 2u );
}

TEST_CASE( "Selection material inspection publishes cached project thumbnail metadata and diagnostics",
    "[TileEditor][SelectionMaterial]" )
{
    fixture_t fixture;
    fixture.paint( { 1, 2 }, 8u );
    REQUIRE( CypherTileMapDocument_SetMaterialBinding( &fixture.document, 8u,
        StringView_FromCString( "materials/stone.cymat" ) ) == tile_map_document_status_t::OK );
    tile_ortho_material_cache_t cache{};
    tile_ortho_material_t ready{};
    ready.bound = true;
    ready.path = QStringLiteral( "materials/stone.cymat" );
    ready.label = QStringLiteral( "stone" );
    ready.image = QImage( 4, 2, QImage::Format_ARGB32 );
    ready.image.fill( QColor( 80, 92, 103 ) );
    ready.color = QColor( 80, 92, 103 );
    ready.textureWidth = 1024u;
    ready.textureHeight = 512u;
    ready.sRGB = true;
    ready.generateMips = true;
    ready.tint[0] = 0.25f;
    ready.tint[1] = 0.5f;
    ready.tint[2] = 0.75f;
    ready.uvScale[0] = 2.0f;
    ready.uvScale[1] = -1.0f;
    cache.entries.insert( 8u, ready );
    const tile_map_grid_coord_t selection[]{ { 1, 2 } };
    const auto inspected = TileSelectionMaterial_Inspect(
        &fixture.document, selection, &cache );
    CHECK( inspected.state == tile_selection_material_state_t::PROJECT_READY );
    CHECK( inspected.path == QStringLiteral( "materials/stone.cymat" ) );
    CHECK( inspected.preview.cacheKey() == ready.image.cacheKey() );
    CHECK( inspected.textureWidth == 1024u );
    CHECK( inspected.textureHeight == 512u );
    CHECK( inspected.generateMips );
    CHECK( inspected.tint[2] == 0.75f );
    CHECK( inspected.uvScale[1] == -1.0f );

    cache.entries[8u].image = {};
    cache.entries[8u].error = QStringLiteral( "Cooked texture is missing." );
    const auto unavailable = TileSelectionMaterial_Inspect(
        &fixture.document, selection, &cache );
    CHECK( unavailable.state == tile_selection_material_state_t::PROJECT_UNAVAILABLE );
    CHECK( unavailable.diagnostic == QStringLiteral( "Cooked texture is missing." ) );
}

TEST_CASE( "Selection material card exposes one non-mutating use and locate action",
    "[TileEditor][SelectionMaterial][Qt]" )
{
    EnsureSelectionMaterialApplication();
    fixture_t fixture;
    fixture.paint( { 2, 2 }, 5u );
    const tile_map_grid_coord_t selection[]{ { 2, 2 } };
    CypherTileSelectionMaterialInspector inspector;
    int used = -1;
    int located = -1;
    QString locatedPath;
    inspector.setUseForPaintCallback( [&]( u16 slot ) { used = slot; } );
    inspector.setBrowseCallback( [&]( u16 slot, const QString &path ) {
        located = slot;
        locatedPath = path;
    } );
    inspector.setSelection( &fixture.document, selection, nullptr );
    CHECK( Find<QLabel>( inspector, "TileSelectionMaterialName" )->text() ==
        QStringLiteral( "Exterior Stone" ) );
    auto *use = Find<QPushButton>( inspector, "TileSelectionMaterialUse" );
    auto *browse = Find<QPushButton>( inspector, "TileSelectionMaterialBrowse" );
    REQUIRE( use->isEnabled() );
    REQUIRE( browse->isEnabled() );
    use->click();
    browse->click();
    CHECK( used == 5 );
    CHECK( located == 5 );
    CHECK( locatedPath.isEmpty() );

    inspector.setSelection( &fixture.document, {}, nullptr );
    CHECK_FALSE( use->isEnabled() );
    CHECK_FALSE( browse->isEnabled() );
}

TEST_CASE( "Workspace selection immediately publishes its surface material in Properties",
    "[TileEditor][SelectionMaterial][Workspace]" )
{
    EnsureSelectionMaterialApplication();
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    CypherTileDocumentBridge authored;
    QString error;
    REQUIRE( authored.newDocument( { 6u, 6u, 2.0f, 3.0f }, &error ) );
    REQUIRE( authored.beginEdit( QStringLiteral( "Material inspector fixture" ), &error ) );
    REQUIRE( authored.paintCell( { 2, 3 }, { 0, 2, 6 }, &error ) );
    REQUIRE( authored.paintCell( { 3, 3 }, { 0, 2, 5 }, &error ) );
    REQUIRE( authored.commitEdit( &error ) );
    REQUIRE( CypherTileMapDocument_SetMaterialBinding( authored.document(), 6u,
        StringView_FromCString( "materials/selection_inspector_missing.cymat" ) ) ==
        tile_map_document_status_t::OK );
    const QString path = directory.filePath( QStringLiteral( "material_inspector.cymap" ) );
    REQUIRE( authored.saveToFile( path, &error ) );

    CypherTileEditorMainWindow window;
    REQUIRE( window.openFilePath( path, false ) );
    auto *canvasWidget = window.findChild<QWidget *>( QStringLiteral( "CypherTileCanvas" ) );
    auto *inspectorWidget = window.findChild<QWidget *>(
        QStringLiteral( "TileSelectionMaterialInspector" ) );
    REQUIRE( canvasWidget != nullptr );
    REQUIRE( inspectorWidget != nullptr );
    auto *canvas = static_cast<CypherTileCanvas *>( canvasWidget );
    auto *inspector = static_cast<CypherTileSelectionMaterialInspector *>( inspectorWidget );
    canvas->selectCell( { 3, 3 }, true );
    CHECK( inspector->inspection().state == tile_selection_material_state_t::BUILTIN );
    CHECK( inspector->inspection().nSlot == 5u );
    CHECK( Find<QLabel>( *inspector, "TileSelectionMaterialName" )->text() ==
        QStringLiteral( "Exterior Stone" ) );

    auto *paintSlot = Find<QSpinBox>( window, "TilePaintMaterialSlot" );
    auto *palette = Find<QListWidget>( window, "TileMaterialPalette" );
    auto *locate = Find<QPushButton>( *inspector, "TileSelectionMaterialBrowse" );
    paintSlot->setValue( 32 );
    CHECK( palette->currentItem() == nullptr );
    CHECK( inspector->inspection().nSlot == 5u );
    paintSlot->setValue( 1 );
    REQUIRE( inspector->inspection().state ==
             tile_selection_material_state_t::BUILTIN );
    CHECK( inspector->inspection().nSlot == 5u );
    REQUIRE( palette->currentItem() != nullptr );
    CHECK( palette->currentItem()->data( Qt::UserRole ).toUInt() == 1u );
    canvas->setTool( tile_canvas_tool_t::SELECT );
    locate->click();
    REQUIRE( palette->currentItem() != nullptr );
    CHECK( palette->currentItem()->data( Qt::UserRole ).toUInt() == 5u );
    CHECK( paintSlot->value() == 1 );
    CHECK( canvas->tool() == tile_canvas_tool_t::SELECT );

    canvas->selectCell( { 2, 3 }, true );
    REQUIRE( inspector->inspection().state ==
        tile_selection_material_state_t::PROJECT_UNAVAILABLE );
    locate->click();
    CHECK( Find<QSpinBox>( window, "TileProjectMaterialSlot" )->value() == 6 );
    CHECK( paintSlot->value() == 1 );
    CHECK( canvas->tool() == tile_canvas_tool_t::SELECT );
    canvas->clearSelection();
    CHECK( inspector->inspection().state == tile_selection_material_state_t::NO_SELECTION );

    // Choosing a current material is independent from choosing an authoring
    // tool. A material click with no selection must not force the editor back
    // into Paint while the user is selecting or placing pieces.
    QListWidgetItem *alternate = nullptr;
    for ( int row = 0; row < palette->count(); ++row ) {
        if ( palette->item( row )->data( Qt::UserRole ).toUInt() != 5u ) {
            alternate = palette->item( row );
            break;
        }
    }
    REQUIRE( alternate != nullptr );
    canvas->setTool( tile_canvas_tool_t::SELECT );
    palette->setCurrentItem( alternate );
    CHECK( paintSlot->value() == alternate->data( Qt::UserRole ).toInt() );
    CHECK( canvas->tool() == tile_canvas_tool_t::SELECT );
}
