//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileStairs_Tests.cpp
//  Purpose: Exercises stair authoring through production Qt editor controls.
//  Details: Hidden widgets exercise editing, persistence, validation and camera
//           action wiring without requiring or claiming an OpenGL context.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherTileCanvas.h"
#include "CypherTileDocumentBridge.h"
#include "CypherTileEditorMainWindow.h"
#include "CypherTileRenderViewport.h"
#include "CypherTileViewWorkspace.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QMetaObject>
#include <QMouseEvent>
#include <QSettings>
#include <QSpinBox>
#include <QTemporaryDir>

#include <algorithm>

namespace
{

using namespace cypher::tools::tile_editor;

void EnsureStairApplication()
{
    if ( QApplication::instance() != nullptr ) return;
    qputenv( "QT_QPA_PLATFORM", QByteArrayLiteral( "offscreen" ) );
    static int argc = 1;
    static char name[] = "CypherTileStairTests";
    static char *argv[]{ name, nullptr };
    static QApplication application( argc, argv );
}

// The test binary shares one QApplication with other GUI suites. Restore its
// identity, format and INI root so temporary settings cannot escape this test.
struct isolated_settings_t {
    QTemporaryDir directory;
    QString organization{ QCoreApplication::organizationName() };
    QString application{ QCoreApplication::applicationName() };
    QSettings::Format format{ QSettings::defaultFormat() };
    QString previousIniRoot;

    isolated_settings_t()
    {
        REQUIRE( directory.isValid() );
        const QSettings probe( QSettings::IniFormat, QSettings::UserScope,
                               QStringLiteral( "CypherStairSettingsProbe" ),
                               QStringLiteral( "Path" ) );
        previousIniRoot = QFileInfo( QFileInfo( probe.fileName() ).absolutePath() ).absolutePath();
        QSettings::setDefaultFormat( QSettings::IniFormat );
        QSettings::setPath( QSettings::IniFormat, QSettings::UserScope, directory.path() );
        QCoreApplication::setOrganizationName( QStringLiteral( "CypherTests" ) );
        QCoreApplication::setApplicationName( QStringLiteral( "TileStairIntegration" ) );
    }

    ~isolated_settings_t()
    {
        QCoreApplication::setOrganizationName( organization );
        QCoreApplication::setApplicationName( application );
        QSettings::setPath( QSettings::IniFormat, QSettings::UserScope, previousIniRoot );
        QSettings::setDefaultFormat( format );
    }
};

QAction *Action( CypherTileEditorMainWindow &window, const char *name )
{
    auto *action = window.findChild<QAction *>( QString::fromLatin1( name ) );
    REQUIRE( action != nullptr );
    REQUIRE( action->isEnabled() );
    return action;
}

CypherTileCanvas *Canvas( CypherTileEditorMainWindow &window )
{
    auto *widget = window.findChild<QWidget *>( QStringLiteral( "CypherTileCanvas" ) );
    REQUIRE( widget != nullptr );
    return static_cast<CypherTileCanvas *>( widget );
}

void ClickCell( CypherTileCanvas &canvas, tile_map_grid_coord_t coordinate )
{
    canvas.selectCell( coordinate, true );
    const QPointF center( canvas.width() * 0.5, canvas.height() * 0.5 );
    QMouseEvent press( QEvent::MouseButtonPress, center, center,
                       Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
    QMouseEvent release( QEvent::MouseButtonRelease, center, center,
                         Qt::LeftButton, Qt::NoButton, Qt::NoModifier );
    QApplication::sendEvent( &canvas, &press );
    QApplication::sendEvent( &canvas, &release );
}

QListWidgetItem *Material( CypherTileEditorMainWindow &window, int slot )
{
    auto *palette = window.findChild<QListWidget *>( QStringLiteral( "TileMaterialPalette" ) );
    REQUIRE( palette != nullptr );
    for ( int i = 0; i < palette->count(); ++i ) {
        if ( palette->item( i )->data( Qt::UserRole ).toInt() == slot ) return palette->item( i );
    }
    FAIL( "Requested fixture material is not present in the real palette" );
    return nullptr;
}

QString WriteStairFixture( const QTemporaryDir &directory )
{
    CypherTileDocumentBridge document;
    QString error;
    REQUIRE( document.newDocument( { 4u, 3u, 2.0f, 3.0f }, &error ) );
    REQUIRE( document.beginEdit( QStringLiteral( "Create stair fixture" ), &error ) );
    REQUIRE( document.paintCell( { 0, 1 }, { 0, 1u, 2u }, &error ) );
    REQUIRE( document.paintCell( { 1, 1 },
                 { 0, 1u, 2u, tile_map_cell_shape_t::STAIRS_EAST, 12u }, &error ) );
    REQUIRE( document.paintCell( { 2, 1 },
                 { 0, 1u, 2u, tile_map_cell_shape_t::STAIRS_EAST, 12u }, &error ) );
    REQUIRE( document.paintCell( { 1, 2 },
                 { 0, 1u, 2u, tile_map_cell_shape_t::STAIRS_NORTH, 6u }, &error ) );
    REQUIRE( document.placePlayerSpawn( { 0, 1 }, 0.0f, &error ) );
    REQUIRE( document.commitEdit( &error ) );
    const QString path = directory.filePath( QStringLiteral( "stairs.cymap" ) );
    REQUIRE( document.saveToFile( path, &error ) );
    return path;
}

void SaveAndCheckCell( CypherTileEditorMainWindow &window, const QString &path,
                      tile_map_grid_coord_t coordinate, tile_map_cell_shape_t shape,
                      cypher::common::u16 steps, cypher::common::u16 material )
{
    Action( window, "file.save" )->trigger();
    CypherTileDocumentBridge loaded;
    QString error;
    REQUIRE( loaded.loadFromFile( path, &error ) );
    const auto *cell = CypherTileMapDocument_CellAt( loaded.document(), coordinate );
    REQUIRE( cell != nullptr );
    CHECK( ( cell->flags & TILE_MAP_CELL_FLAG_FLOOR ) != 0u );
    CHECK( cell->shape == shape );
    CHECK( cell->nStairSteps == steps );
    CHECK( cell->nMaterialSlot == material );
}

} // namespace

TEST_CASE( "Editor eyedropper and material palette preserve stair brush properties",
           "[CypherTools][CypherTileEditor][Stairs][Integration]" )
{
    EnsureStairApplication();
    isolated_settings_t settings;
    QTemporaryDir maps;
    REQUIRE( maps.isValid() );
    const QString path = WriteStairFixture( maps );
    CypherTileEditorMainWindow window;
    REQUIRE( window.openFilePath( path, false ) );
    auto *canvas = Canvas( window );
    Action( window, "tool.eyedropper" )->trigger();
    ClickCell( *canvas, { 1, 1 } );
    CHECK( canvas->tool() == tile_canvas_tool_t::EYEDROPPER );
    CHECK( canvas->paint().shape == tile_map_cell_shape_t::STAIRS_EAST );
    CHECK( canvas->paint().nStairSteps == 12u );
    CHECK( canvas->paint().nMaterialSlot == 2u );
    auto *palette = window.findChild<QListWidget *>( QStringLiteral( "TileMaterialPalette" ) );
    REQUIRE( palette != nullptr );
    REQUIRE( palette->currentItem() != nullptr );
    CHECK( palette->currentItem()->data( Qt::UserRole ).toInt() == 2 );
    auto *shape = window.findChild<QComboBox *>( QStringLiteral( "TilePaintShape" ) );
    auto *steps = window.findChild<QSpinBox *>( QStringLiteral( "TilePaintStairSteps" ) );
    REQUIRE( shape != nullptr );
    REQUIRE( steps != nullptr );
    CHECK( shape->currentData().toInt() == static_cast<int>( tile_map_cell_shape_t::STAIRS_EAST ) );
    CHECK( steps->isEnabled() );
    CHECK( steps->value() == 12 );

    palette->setCurrentItem( Material( window, 4 ) );
    CHECK( canvas->tool() == tile_canvas_tool_t::PAINT );
    CHECK( canvas->paint().shape == tile_map_cell_shape_t::STAIRS_EAST );
    CHECK( canvas->paint().nStairSteps == 12u );
    CHECK( canvas->paint().nMaterialSlot == 4u );
    ClickCell( *canvas, { 3, 0 } );
    SaveAndCheckCell( window, path, { 3, 0 }, tile_map_cell_shape_t::STAIRS_EAST, 12u, 4u );
}

TEST_CASE( "Stair fill respects shape boundaries and inspector edits survive history",
           "[CypherTools][CypherTileEditor][Stairs][Integration]" )
{
    EnsureStairApplication();
    isolated_settings_t settings;
    QTemporaryDir maps;
    REQUIRE( maps.isValid() );
    const QString path = WriteStairFixture( maps );
    CypherTileEditorMainWindow window;
    REQUIRE( window.openFilePath( path, false ) );
    auto *canvas = Canvas( window );
    Action( window, "tool.eyedropper" )->trigger();
    ClickCell( *canvas, { 1, 1 } );
    auto *palette = window.findChild<QListWidget *>( QStringLiteral( "TileMaterialPalette" ) );
    REQUIRE( palette != nullptr );
    palette->setCurrentItem( Material( window, 4 ) );
    Action( window, "tool.fill" )->trigger();
    ClickCell( *canvas, { 1, 1 } );
    SaveAndCheckCell( window, path, { 1, 1 }, tile_map_cell_shape_t::STAIRS_EAST, 12u, 4u );
    SaveAndCheckCell( window, path, { 2, 1 }, tile_map_cell_shape_t::STAIRS_EAST, 12u, 4u );
    SaveAndCheckCell( window, path, { 1, 2 }, tile_map_cell_shape_t::STAIRS_NORTH, 6u, 2u );
    SaveAndCheckCell( window, path, { 0, 1 }, tile_map_cell_shape_t::FLAT, 8u, 2u );
    Action( window, "edit.undo" )->trigger();
    SaveAndCheckCell( window, path, { 1, 1 }, tile_map_cell_shape_t::STAIRS_EAST, 12u, 2u );
    SaveAndCheckCell( window, path, { 2, 1 }, tile_map_cell_shape_t::STAIRS_EAST, 12u, 2u );
    Action( window, "edit.redo" )->trigger();
    SaveAndCheckCell( window, path, { 2, 1 }, tile_map_cell_shape_t::STAIRS_EAST, 12u, 4u );

    canvas->selectCell( { 1, 1 } );
    auto *shape = window.findChild<QComboBox *>( QStringLiteral( "TileSelectedShape" ) );
    auto *steps = window.findChild<QSpinBox *>( QStringLiteral( "TileSelectedStairSteps" ) );
    REQUIRE( shape != nullptr );
    REQUIRE( steps != nullptr );
    shape->setCurrentIndex( shape->findData( static_cast<int>( tile_map_cell_shape_t::STAIRS_WEST ) ) );
    steps->setValue( 20 );
    REQUIRE( QMetaObject::invokeMethod( steps, "editingFinished", Qt::DirectConnection ) );
    SaveAndCheckCell( window, path, { 1, 1 }, tile_map_cell_shape_t::STAIRS_WEST, 20u, 4u );
    Action( window, "edit.undo" )->trigger();
    SaveAndCheckCell( window, path, { 1, 1 }, tile_map_cell_shape_t::STAIRS_WEST, 12u, 4u );
    Action( window, "edit.undo" )->trigger();
    SaveAndCheckCell( window, path, { 1, 1 }, tile_map_cell_shape_t::STAIRS_EAST, 12u, 4u );
}

TEST_CASE( "Stair spawn warnings allow geometry and the F action frames stair selection",
           "[CypherTools][CypherTileEditor][Stairs][Integration][Camera]" )
{
    EnsureStairApplication();
    isolated_settings_t settings;
    QTemporaryDir maps;
    REQUIRE( maps.isValid() );
    CypherTileEditorMainWindow window;
    REQUIRE( window.openFilePath( WriteStairFixture( maps ), false ) );
    auto *canvas = Canvas( window );
    Action( window, "tool.spawn" )->trigger();
    ClickCell( *canvas, { 1, 1 } );
    auto *diagnostics = window.findChild<QListWidget *>( QStringLiteral( "TileValidationList" ) );
    auto *summary = window.findChild<QLabel *>( QStringLiteral( "TileValidationSummary" ) );
    REQUIRE( diagnostics != nullptr );
    REQUIRE( summary != nullptr );
    QListWidgetItem *warning = nullptr;
    for ( int i = 0; i < diagnostics->count(); ++i ) {
        if ( diagnostics->item( i )->data( Qt::UserRole + 4 ).toString() ==
             QStringLiteral( "PLAYER_SPAWN_ON_STAIRS" ) ) warning = diagnostics->item( i );
    }
    REQUIRE( warning != nullptr );
    CHECK( warning->data( Qt::UserRole + 3 ).toInt() == 1 );
    CHECK( summary->text().contains( QStringLiteral( "1 warning" ) ) );
    CHECK( summary->text().contains( QStringLiteral( "0 error" ) ) );

    auto *workspaceWidget = window.findChild<QWidget *>( QStringLiteral( "TileEditorFourViews" ) );
    auto *viewportWidget = window.findChild<QWidget *>( QStringLiteral( "CypherTileRenderViewport" ) );
    REQUIRE( workspaceWidget != nullptr );
    REQUIRE( viewportWidget != nullptr );
    auto *workspace = static_cast<CypherTileViewWorkspace *>( workspaceWidget );
    auto *viewport = static_cast<CypherTileRenderViewport *>( viewportWidget );
    workspace->focusView( tile_editor_view_t::PERSPECTIVE );
    auto *frameAction = Action( window, "view.fit" );
    CHECK( frameAction->shortcut() == QKeySequence( QStringLiteral( "F" ) ) );
    // Invoke the real action used by F. Hidden windows cannot receive native
    // shortcut routing; this checks that the production handler frames the
    // selected stair rather than calling whole-map fit.
    const auto mapBoundsCenter = viewport->camera().boundsCenter;
    auto expected = viewport->camera();
    // The selected 2×2 world-unit stair rises from the slab bottom to Z=3.
    // The action must frame this stair even though core framing offsets its
    // orbit pivot to center the projected silhouette.
    CypherTileCamera_FrameBounds( expected,
        { 2.0f, 2.0f, -TILE_MAP_DEFAULT_FLOOR_THICKNESS }, { 4.0f, 4.0f, 3.0f },
        static_cast<float>( std::max( viewport->width(), 1 ) ) / std::max( viewport->height(), 1 ) );
    frameAction->trigger();
    const auto &camera = viewport->camera();
    CHECK( cypher::math::Vec3_NearlyEquals( camera.position, expected.position, 0.001f, 0.00001f ) );
    CHECK( camera.orbitDistance == Catch::Approx( expected.orbitDistance ).margin( 0.001f ) );
    CHECK( camera.yawRadians == expected.yawRadians );
    CHECK( camera.pitchRadians == expected.pitchRadians );
    CHECK( cypher::math::Vec3_EqualsExact( camera.boundsCenter, mapBoundsCenter ) );
}
