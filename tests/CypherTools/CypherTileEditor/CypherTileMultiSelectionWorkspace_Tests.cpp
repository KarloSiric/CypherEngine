//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Production workspace bulk-property and sparse-selection transactions.
//////////////////////////////////////////////////////////////////////////
#include "CypherTileCanvas.h"
#include "CypherTileDocumentBridge.h"
#include "CypherTileEditorConfig.h"
#include "CypherTileEditorMainWindow.h"
#include "CypherTileOrthoView.h"
#include "CypherTileRenderViewport.h"
#include "CypherTileViewWorkspace.h"

#include <catch2/catch_test_macros.hpp>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMetaObject>
#include <QSettings>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTreeWidget>

#include <algorithm>
#include <array>
#include <cmath>

using namespace cypher::common;
using namespace cypher::tools::tile_editor;

namespace
{
void MultiWorkspaceApplication()
{
    if ( QApplication::instance() ) return;
    qputenv( "QT_QPA_PLATFORM", "offscreen" );
    static int argc = 1;
    static char name[] = "TileMultiSelectionWorkspaceTests";
    static char *argv[]{ name, nullptr };
    static QApplication application( argc, argv );
}

struct multi_workspace_settings_t {
    QTemporaryDir directory;
    QString organization{ QCoreApplication::organizationName() };
    QString application{ QCoreApplication::applicationName() };
    QSettings::Format format{ QSettings::defaultFormat() };
    QString previousRoot;
    QString configPath;

    multi_workspace_settings_t()
    {
        REQUIRE( directory.isValid() );
        QSettings probe( QSettings::IniFormat, QSettings::UserScope, "CypherMultiSelectionProbe", "Path" );
        previousRoot = QFileInfo( QFileInfo( probe.fileName() ).absolutePath() ).absolutePath();
        QSettings::setDefaultFormat( QSettings::IniFormat );
        QSettings::setPath( QSettings::IniFormat, QSettings::UserScope, directory.path() );
        QCoreApplication::setOrganizationName( "CypherTests" );
        QCoreApplication::setApplicationName( "TileMultiSelection_" + QDir( directory.path() ).dirName() );
        configPath = TileEditorConfig_DefaultPath();
        QSettings settings;
        tile_editor_preferences_t preferences;
        preferences.startMaximized = false;
        preferences.activateViewOnHover = false;
        TileEditorPreferences_Save( settings, preferences );
    }

    ~multi_workspace_settings_t()
    {
        QFile::remove( configPath ); // This test's unique application profile only.
        QCoreApplication::setOrganizationName( organization );
        QCoreApplication::setApplicationName( application );
        QSettings::setPath( QSettings::IniFormat, QSettings::UserScope, previousRoot );
        QSettings::setDefaultFormat( format );
    }
};

template <typename T> T *MultiWidget( CypherTileEditorMainWindow &window, const char *name )
{
    auto *widget = window.findChild<QWidget *>( QString::fromLatin1( name ) );
    REQUIRE( widget );
    return static_cast<T *>( widget );
}

QAction *MultiAction( CypherTileEditorMainWindow &window, const char *name )
{
    auto *action = window.findChild<QAction *>( QString::fromLatin1( name ) );
    REQUIRE( action );
    return action;
}

void MultiTrigger( CypherTileEditorMainWindow &window, const char *name )
{
    auto *action = MultiAction( window, name );
    REQUIRE( action->isEnabled() );
    action->trigger();
}

QString MultiMap( const QTemporaryDir &directory )
{
    CypherTileDocumentBridge bridge;
    QString error;
    REQUIRE( bridge.newDocument( { 24u, 14u, 2.0f, 3.0f }, &error ) );
    REQUIRE( bridge.beginEdit( "Mixed sparse fixture", &error ) );
    REQUIRE( bridge.paintCell( { 2, 2 }, { 1, 2, 4 }, &error ) );
    REQUIRE( bridge.paintCell( { 3, 2 }, { 7, 7, 3 }, &error ) ); // Occupied hole; never selected by the sparse set.
    REQUIRE( bridge.paintCell( { 4, 2 }, { 3, 5, 6, tile_map_cell_shape_t::STAIRS_NORTH, 12 }, &error ) );
    REQUIRE( bridge.paintCell( { 10, 8 }, { 0, 1, 1 }, &error ) );
    REQUIRE( bridge.placePlayerSpawn( { 2, 2 }, 30.0f, &error ) );
    REQUIRE( bridge.placeDoor( { 2, 2 }, tile_map_marker_side_t::SOUTH, nullptr, &error ) );
    REQUIRE( bridge.commitEdit( &error ) );
    const auto path = directory.filePath( "multi.cymap" );
    REQUIRE( bridge.saveToFile( path, &error ) );
    return path;
}

void SelectSparse( CypherTileCanvas &canvas )
{
    const std::array<tile_map_grid_coord_t, 2> cells{{ { 2, 2 }, { 4, 2 } }};
    canvas.setSelectedCells( cells );
}

bool HasCell( const CypherTileCanvas &canvas, int x, int y )
{
    return std::any_of( canvas.selectedCells().begin(), canvas.selectedCells().end(),
        [=]( auto cell ) { return cell.x == x && cell.y == y; } );
}

void SaveRead( CypherTileEditorMainWindow &window, const QString &path, CypherTileDocumentBridge &loaded )
{
    MultiTrigger( window, "file.save" );
    QString error;
    REQUIRE( loaded.loadFromFile( path, &error ) );
}

const tile_map_cell_t &Cell( const CypherTileDocumentBridge &loaded, int x, int y )
{
    const auto *cell = CypherTileMapDocument_CellAt( loaded.document(), { x, y } );
    REQUIRE( cell );
    return *cell;
}

void SetInteger( CypherTileEditorMainWindow &window, const char *name, int value )
{
    auto *spin = MultiWidget<QSpinBox>( window, name );
    REQUIRE( spin->isEnabled() );
    spin->setValue( value );
    REQUIRE( QMetaObject::invokeMethod( spin, "editingFinished", Qt::DirectConnection ) );
}

void MultiKey( QWidget &widget, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier )
{
    QKeyEvent press( QEvent::KeyPress, key, modifiers );
    QKeyEvent release( QEvent::KeyRelease, key, modifiers );
    QApplication::sendEvent( &widget, &press );
    QApplication::sendEvent( &widget, &release );
}

void ShowTopOnly( CypherTileEditorMainWindow &window )
{
    auto *workspace = MultiWidget<CypherTileViewWorkspace>( window, "TileEditorFourViews" );
    workspace->focusView( tile_editor_view_t::TOP );
    if ( !workspace->isMaximized() ) workspace->toggleMaximize();
    window.resize( 1100, 800 );
    window.show(); // Keep unsupported offscreen OpenGL hidden.
    window.activateWindow();
    QApplication::processEvents();
}
} // namespace

TEST_CASE( "Workspace bulk property edits change only the named field and preserve sparse holes",
    "[TileEditor][MultiSelection][Integration]" )
{
    MultiWorkspaceApplication();
    multi_workspace_settings_t settings;
    QTemporaryDir maps;
    const auto path = MultiMap( maps );
    CypherTileEditorMainWindow window;
    REQUIRE( window.openFilePath( path, false ) );
    auto *canvas = MultiWidget<CypherTileCanvas>( window, "CypherTileCanvas" );
    SelectSparse( *canvas );
    auto *summary = MultiWidget<QLabel>( window, "TileSelectionSummary" );
    CHECK( summary->text().contains( "2 cells" ) );
    auto *shape = MultiWidget<QComboBox>( window, "TileSelectedShape" );
    REQUIRE( shape->isEnabled() );
    CHECK( shape->currentIndex() == -1 );
    CHECK( MultiWidget<QSpinBox>( window, "TileSelectionFloorLevel" )->property( "mixed" ).toBool() );
    SetInteger( window, "TileSelectionFloorLevel", 9 );
    CypherTileDocumentBridge loaded;
    SaveRead( window, path, loaded );
    CHECK( Cell( loaded, 2, 2 ).nFloorLevel == 9 );
    CHECK( Cell( loaded, 4, 2 ).nFloorLevel == 9 );
    CHECK( Cell( loaded, 2, 2 ).nWallHeightLevels == 2 );
    CHECK( Cell( loaded, 4, 2 ).nWallHeightLevels == 5 );
    CHECK( Cell( loaded, 2, 2 ).nMaterialSlot == 4 );
    CHECK( Cell( loaded, 4, 2 ).nMaterialSlot == 6 );
    CHECK( Cell( loaded, 2, 2 ).shape == tile_map_cell_shape_t::FLAT );
    CHECK( Cell( loaded, 4, 2 ).shape == tile_map_cell_shape_t::STAIRS_NORTH );
    CHECK( Cell( loaded, 4, 2 ).nStairSteps == 12 );
    CHECK( Cell( loaded, 3, 2 ).nFloorLevel == 7 );
    CHECK( Cell( loaded, 3, 2 ).nWallHeightLevels == 7 );
    SetInteger( window, "TileSelectionMaterialSlot", 11 );
    SaveRead( window, path, loaded );
    CHECK( Cell( loaded, 2, 2 ).nMaterialSlot == 11 );
    CHECK( Cell( loaded, 4, 2 ).nMaterialSlot == 11 );
    CHECK( Cell( loaded, 3, 2 ).nMaterialSlot == 3 );
    MultiTrigger( window, "edit.undo" );
    SaveRead( window, path, loaded );
    CHECK( Cell( loaded, 2, 2 ).nMaterialSlot == 4 );
    CHECK( Cell( loaded, 4, 2 ).nMaterialSlot == 6 );
    CHECK( Cell( loaded, 4, 2 ).nFloorLevel == 9 );
    MultiTrigger( window, "edit.undo" );
    SaveRead( window, path, loaded );
    CHECK( Cell( loaded, 2, 2 ).nFloorLevel == 1 );
    CHECK( Cell( loaded, 4, 2 ).nFloorLevel == 3 );
    CHECK_FALSE( MultiAction( window, "edit.undo" )->isEnabled() );
}

TEST_CASE( "Focusing mixed inspector fields without editing does not dirty the map",
    "[TileEditor][MultiSelection][Integration]" )
{
    MultiWorkspaceApplication();
    multi_workspace_settings_t settings;
    QTemporaryDir maps;
    CypherTileEditorMainWindow window;
    REQUIRE( window.openFilePath( MultiMap( maps ), false ) );
    auto *canvas = MultiWidget<CypherTileCanvas>( window, "CypherTileCanvas" );
    SelectSparse( *canvas );
    ShowTopOnly( window );
    for ( const auto *id : { "TileSelectionFloorLevel", "TileSelectionWallHeight", "TileSelectionMaterialSlot" } ) {
        auto *spin = MultiWidget<QSpinBox>( window, id );
        spin->setFocus();
        QApplication::processEvents();
        canvas->setFocus();
        QApplication::processEvents();
        REQUIRE( QMetaObject::invokeMethod( spin, "editingFinished", Qt::DirectConnection ) );
        CHECK_FALSE( window.windowTitle().startsWith( '*' ) );
        CHECK_FALSE( MultiAction( window, "edit.undo" )->isEnabled() );
    }
    CHECK( canvas->selectedCells().size() == 2 );
    window.hide();
}

TEST_CASE( "Unrelated document refresh preserves a typed selection property until it is committed",
    "[TileEditor][MultiSelection][Integration][Inspector]" )
{
    MultiWorkspaceApplication();
    multi_workspace_settings_t settings;
    QTemporaryDir maps;
    const auto path = MultiMap( maps );
    CypherTileEditorMainWindow window;
    REQUIRE( window.openFilePath( path, false ) );
    auto *canvas = MultiWidget<CypherTileCanvas>( window, "CypherTileCanvas" );
    SelectSparse( *canvas );
    ShowTopOnly( window );
    auto *floor = MultiWidget<QSpinBox>( window, "TileSelectionFloorLevel" );
    REQUIRE_FALSE( floor->keyboardTracking() );
    floor->setFocus();
    QApplication::processEvents();
    REQUIRE( floor->hasFocus() );
    floor->selectAll();
    QKeyEvent typeNine( QEvent::KeyPress, Qt::Key_9, Qt::NoModifier, QStringLiteral( "9" ) );
    QKeyEvent releaseNine( QEvent::KeyRelease, Qt::Key_9, Qt::NoModifier, QStringLiteral( "9" ) );
    QApplication::sendEvent( floor, &typeNine );
    QApplication::sendEvent( floor, &releaseNine );
    REQUIRE( floor->text() == QStringLiteral( "9" ) );
    REQUIRE_FALSE( MultiAction( window, "edit.undo" )->isEnabled() );

    // A material preparation completion uses this same refresh path while an
    // inspector edit still has focus. Change a different property, without
    // synthesizing a focus loss that would commit the pending floor value.
    MultiTrigger( window, "edit.wallRaise" );
    REQUIRE( floor->hasFocus() );
    REQUIRE( floor->text() == QStringLiteral( "9" ) );
    MultiKey( *floor, Qt::Key_Return );
    CypherTileDocumentBridge loaded;
    SaveRead( window, path, loaded );
    CHECK( Cell( loaded, 2, 2 ).nFloorLevel == 9 );
    CHECK( Cell( loaded, 4, 2 ).nFloorLevel == 9 );
    CHECK( Cell( loaded, 2, 2 ).nWallHeightLevels == 3 );
    CHECK( Cell( loaded, 4, 2 ).nWallHeightLevels == 6 );
    CHECK( Cell( loaded, 3, 2 ).nFloorLevel == 7 );
    CHECK( Cell( loaded, 3, 2 ).nWallHeightLevels == 7 );
    MultiTrigger( window, "edit.undo" );
    SaveRead( window, path, loaded );
    CHECK( Cell( loaded, 2, 2 ).nFloorLevel == 1 );
    CHECK( Cell( loaded, 4, 2 ).nFloorLevel == 3 );
    CHECK( Cell( loaded, 2, 2 ).nWallHeightLevels == 3 );
    CHECK( Cell( loaded, 4, 2 ).nWallHeightLevels == 6 );
    MultiTrigger( window, "edit.undo" );
    SaveRead( window, path, loaded );
    CHECK( Cell( loaded, 2, 2 ).nWallHeightLevels == 2 );
    CHECK( Cell( loaded, 4, 2 ).nWallHeightLevels == 5 );
    CHECK_FALSE( MultiAction( window, "edit.undo" )->isEnabled() );
    window.hide();
}

TEST_CASE( "Locating a validation issue centers the visible Top clone while the original is hidden",
    "[TileEditor][MultiSelection][Integration][DuplicateViews][Validation]" )
{
    MultiWorkspaceApplication();
    multi_workspace_settings_t settings;
    QTemporaryDir maps;
    CypherTileEditorMainWindow window;
    REQUIRE( window.openFilePath( MultiMap( maps ), false ) );
    // This case specifically verifies that a hidden clone retains its own
    // navigation transform; keep the optional linked-navigation mode disabled.
    MultiAction( window, "view.linkOrthographicCameras" )->setChecked( false );
    auto *original = MultiWidget<CypherTileCanvas>( window, "CypherTileCanvas" );
    original->selectCell( { 2, 2 } );
    auto *shape = MultiWidget<QComboBox>( window, "TileSelectedShape" );
    const int stairs = shape->findData( static_cast<int>( tile_map_cell_shape_t::STAIRS_EAST ) );
    REQUIRE( stairs >= 0 );
    shape->setCurrentIndex( stairs ); // Preserve the spawn and produce a real placement warning.
    auto *issues = MultiWidget<QListWidget>( window, "TileValidationList" );
    QListWidgetItem *spawnIssue = nullptr;
    for ( int row = 0; row < issues->count(); ++row ) {
        auto *item = issues->item( row );
        if ( item->data( Qt::UserRole + 4 ).toString() == QStringLiteral( "PLAYER_SPAWN_ON_STAIRS" ) )
            spawnIssue = item;
    }
    REQUIRE( spawnIssue );
    REQUIRE( spawnIssue->data( Qt::UserRole ).toBool() );
    REQUIRE( spawnIssue->data( Qt::UserRole + 1 ).toInt() == 2 );
    REQUIRE( spawnIssue->data( Qt::UserRole + 2 ).toInt() == 2 );

    auto *workspace = MultiWidget<CypherTileViewWorkspace>( window, "TileEditorFourViews" );
    REQUIRE( workspace->setPaneView( 2, tile_editor_view_t::TOP ) );
    auto *duplicate = static_cast<CypherTileCanvas *>( workspace->activeWidget() );
    REQUIRE( duplicate != original );
    REQUIRE( workspace->setPaneView( 1, tile_editor_view_t::FRONT ) );
    REQUIRE( workspace->setPaneView( 2, tile_editor_view_t::TOP ) );
    ShowTopOnly( window );
    REQUIRE( workspace->activeWidget() == duplicate );
    REQUIRE( duplicate->isVisible() );
    REQUIRE_FALSE( original->isVisible() );
    duplicate->setZoomFactor( 32.0 );
    duplicate->selectCell( { 20, 12 }, true );
    const QPointF originalOrigin = original->viewOrigin();
    const QPointF duplicateOrigin = duplicate->viewOrigin();
    REQUIRE( QMetaObject::invokeMethod( issues, "itemActivated", Qt::DirectConnection,
        Q_ARG( QListWidgetItem *, spawnIssue ) ) );
    CHECK( workspace->activeWidget() == duplicate );
    CHECK( HasCell( *duplicate, 2, 2 ) );
    CHECK( HasCell( *original, 2, 2 ) );
    CHECK( duplicate->selectedCells().size() == 1 );
    CHECK( duplicate->viewOrigin() != duplicateOrigin );
    const QPointF targetCenter = duplicate->viewOrigin() + QPointF( 2.5 * duplicate->zoomFactor(), 2.5 * duplicate->zoomFactor() );
    CHECK( std::abs( targetCenter.x() - duplicate->width() * 0.5 ) < 0.01 );
    CHECK( std::abs( targetCenter.y() - duplicate->height() * 0.5 ) < 0.01 );
    CHECK( original->viewOrigin() == originalOrigin );
    window.hide();
}

TEST_CASE( "Mixed playable-floor bulk toggle preserves existing floor properties and is undoable",
    "[TileEditor][MultiSelection][Integration]" )
{
    MultiWorkspaceApplication();
    multi_workspace_settings_t settings;
    QTemporaryDir maps;
    const auto path = MultiMap( maps );
    CypherTileEditorMainWindow window;
    REQUIRE( window.openFilePath( path, false ) );
    auto *canvas = MultiWidget<CypherTileCanvas>( window, "CypherTileCanvas" );
    const std::array<tile_map_grid_coord_t, 3> cells{{ { 2, 2 }, { 4, 2 }, { 6, 5 } }};
    canvas->setSelectedCells( cells );
    auto *floor = MultiWidget<QCheckBox>( window, "TileSelectionFloorEnabled" );
    REQUIRE( floor->checkState() == Qt::PartiallyChecked );
    floor->click();
    REQUIRE( floor->checkState() == Qt::Checked );
    CypherTileDocumentBridge loaded;
    SaveRead( window, path, loaded );
    CHECK( CypherTileMapDocument_CellHasFloor( loaded.document(), { 6, 5 } ) );
    CHECK( Cell( loaded, 2, 2 ).nFloorLevel == 1 );
    CHECK( Cell( loaded, 2, 2 ).nWallHeightLevels == 2 );
    CHECK( Cell( loaded, 2, 2 ).nMaterialSlot == 4 );
    CHECK( Cell( loaded, 4, 2 ).shape == tile_map_cell_shape_t::STAIRS_NORTH );
    CHECK( Cell( loaded, 4, 2 ).nStairSteps == 12 );
    CHECK( Cell( loaded, 4, 2 ).nWallHeightLevels == 5 );
    CHECK( Cell( loaded, 4, 2 ).nMaterialSlot == 6 );
    floor->click();
    REQUIRE( floor->checkState() == Qt::Unchecked );
    SaveRead( window, path, loaded );
    for ( const auto cell : cells ) CHECK_FALSE( CypherTileMapDocument_CellHasFloor( loaded.document(), cell ) );
    CHECK( Cell( loaded, 3, 2 ).nWallHeightLevels == 7 );
    CHECK( loaded.document()->markers.nCount == 0 );
    MultiTrigger( window, "edit.undo" );
    SaveRead( window, path, loaded );
    CHECK( Cell( loaded, 4, 2 ).shape == tile_map_cell_shape_t::STAIRS_NORTH );
    CHECK( loaded.document()->markers.nCount == 2 );
    MultiTrigger( window, "edit.undo" );
    SaveRead( window, path, loaded );
    CHECK_FALSE( CypherTileMapDocument_CellHasFloor( loaded.document(), { 6, 5 } ) );
    CHECK( Cell( loaded, 2, 2 ).nFloorLevel == 1 );
    CHECK( Cell( loaded, 4, 2 ).nFloorLevel == 3 );
}

TEST_CASE( "Sparse duplicate move delete and undo preserve the unselected occupied hole and marker ownership",
    "[TileEditor][MultiSelection][Integration]" )
{
    MultiWorkspaceApplication();
    multi_workspace_settings_t settings;
    QTemporaryDir maps;
    const auto path = MultiMap( maps );
    CypherTileEditorMainWindow window;
    REQUIRE( window.openFilePath( path, false ) );
    auto *canvas = MultiWidget<CypherTileCanvas>( window, "CypherTileCanvas" );
    SelectSparse( *canvas );
    MultiTrigger( window, "edit.duplicate" );
    REQUIRE( canvas->selectedCells().size() == 2 );
    CHECK( HasCell( *canvas, 5, 2 ) );
    CHECK( HasCell( *canvas, 7, 2 ) );
    CHECK_FALSE( HasCell( *canvas, 6, 2 ) );
    CypherTileDocumentBridge loaded;
    SaveRead( window, path, loaded );
    CHECK( Cell( loaded, 5, 2 ).nMaterialSlot == 4 );
    CHECK( Cell( loaded, 7, 2 ).shape == tile_map_cell_shape_t::STAIRS_NORTH );
    CHECK( CypherTileMapCell_IsCanonicalEmpty( Cell( loaded, 6, 2 ) ) );
    CHECK( loaded.document()->markers.nCount == 3 );
    CHECK( CypherTileMapDocument_PlayerSpawn( loaded.document() )->cell.x == 2 );
    MultiTrigger( window, "edit.moveDown" );
    CHECK( HasCell( *canvas, 5, 3 ) );
    CHECK( HasCell( *canvas, 7, 3 ) );
    MultiTrigger( window, "edit.delete" );
    CHECK_FALSE( canvas->hasSelection() );
    SaveRead( window, path, loaded );
    CHECK_FALSE( CypherTileMapDocument_CellHasFloor( loaded.document(), { 5, 3 } ) );
    CHECK_FALSE( CypherTileMapDocument_CellHasFloor( loaded.document(), { 7, 3 } ) );
    CHECK( Cell( loaded, 3, 2 ).nMaterialSlot == 3 );
    CHECK( Cell( loaded, 3, 2 ).nFloorLevel == 7 );
    CHECK( loaded.document()->markers.nCount == 2 );
    MultiTrigger( window, "edit.undo" );
    SaveRead( window, path, loaded );
    CHECK( CypherTileMapDocument_DoorAt( loaded.document(), { 5, 3 }, tile_map_marker_side_t::SOUTH ) != nullptr );
    CHECK( Cell( loaded, 7, 3 ).nStairSteps == 12 );
    CHECK( CypherTileMapCell_IsCanonicalEmpty( Cell( loaded, 6, 3 ) ) );
    MultiTrigger( window, "edit.undo" );
    MultiTrigger( window, "edit.undo" );
    SaveRead( window, path, loaded );
    CHECK_FALSE( CypherTileMapDocument_CellHasFloor( loaded.document(), { 5, 2 } ) );
    CHECK( Cell( loaded, 3, 2 ).nMaterialSlot == 3 );
    CHECK( loaded.document()->markers.nCount == 2 );
}

TEST_CASE( "Wall height commands adjust only selected walls and their shortcuts stay in viewports",
    "[TileEditor][MultiSelection][Integration][Shortcuts]" )
{
    MultiWorkspaceApplication();
    multi_workspace_settings_t settings;
    QTemporaryDir maps;
    const auto path = MultiMap( maps );
    CypherTileEditorMainWindow window;
    REQUIRE( window.openFilePath( path, false ) );
    auto *canvas = MultiWidget<CypherTileCanvas>( window, "CypherTileCanvas" );
    SelectSparse( *canvas );
    for ( const char *id : { "edit.wallRaise", "edit.wallLower", "tool.paint", "tool.erase", "tool.door", "tool.select" } ) {
        auto *action = MultiAction( window, id );
        CHECK( action->shortcutContext() == Qt::WidgetShortcut );
        CHECK( canvas->actions().contains( action ) );
        CHECK( MultiWidget<CypherTileOrthoView>( window, "CypherTileFrontView" )->actions().contains( action ) );
        CHECK( MultiWidget<CypherTileOrthoView>( window, "CypherTileSideView" )->actions().contains( action ) );
        CHECK( MultiWidget<CypherTileRenderViewport>( window, "CypherTileRenderViewport" )->actions().contains( action ) );
    }
    CHECK( MultiAction( window, "edit.wallRaise" )->shortcut() == QKeySequence( Qt::SHIFT | Qt::Key_PageUp ) );
    CHECK( MultiAction( window, "edit.wallLower" )->shortcut() == QKeySequence( Qt::SHIFT | Qt::Key_PageDown ) );
    ShowTopOnly( window );
    auto *console = MultiWidget<QDockWidget>( window, "TileEditorConsoleDock" );
    console->show(); console->raise();
    auto *input = MultiWidget<QLineEdit>( window, "TileConsoleInput" );
    input->setText( "wall height stays unchanged" );
    input->setFocus();
    QApplication::processEvents();
    REQUIRE( input->hasFocus() );
    MultiKey( *input, Qt::Key_PageUp, Qt::ShiftModifier );
    CHECK_FALSE( MultiAction( window, "edit.undo" )->isEnabled() );
    canvas->setFocus();
    QApplication::processEvents();
    REQUIRE( canvas->hasFocus() );
    MultiKey( *canvas, Qt::Key_PageUp, Qt::ShiftModifier );
    REQUIRE( MultiAction( window, "edit.undo" )->isEnabled() );
    CypherTileDocumentBridge loaded;
    SaveRead( window, path, loaded );
    CHECK( Cell( loaded, 2, 2 ).nWallHeightLevels == 3 );
    CHECK( Cell( loaded, 4, 2 ).nWallHeightLevels == 6 );
    CHECK( Cell( loaded, 3, 2 ).nWallHeightLevels == 7 );
    CHECK( Cell( loaded, 2, 2 ).nFloorLevel == 1 );
    CHECK( Cell( loaded, 4, 2 ).nFloorLevel == 3 );
    MultiTrigger( window, "edit.wallLower" );
    SaveRead( window, path, loaded );
    CHECK( Cell( loaded, 2, 2 ).nWallHeightLevels == 2 );
    CHECK( Cell( loaded, 4, 2 ).nWallHeightLevels == 5 );
    window.hide();
}

TEST_CASE( "Select All Authored and outliner multiselection share exact cell identity",
    "[TileEditor][MultiSelection][Integration][Outliner]" )
{
    MultiWorkspaceApplication();
    multi_workspace_settings_t settings;
    QTemporaryDir maps;
    CypherTileEditorMainWindow window;
    REQUIRE( window.openFilePath( MultiMap( maps ), false ) );
    auto *canvas = MultiWidget<CypherTileCanvas>( window, "CypherTileCanvas" );
    MultiTrigger( window, "edit.selectAll" );
    REQUIRE( canvas->selectedCells().size() == 4 );
    CHECK( HasCell( *canvas, 10, 8 ) );
    CHECK_FALSE( HasCell( *canvas, 8, 8 ) );
    MultiTrigger( window, "edit.selectNone" );
    auto *tree = MultiWidget<QTreeWidget>( window, "TileObjectTree" );
    CHECK( tree->selectionMode() == QAbstractItemView::ExtendedSelection );
    QTreeWidgetItem *floor = nullptr;
    QTreeWidgetItem *stair = nullptr;
    QTreeWidgetItem *door = nullptr;
    for ( int group = 0; group < tree->topLevelItemCount(); ++group ) {
        const auto *parent = tree->topLevelItem( group );
        for ( int row = 0; row < parent->childCount(); ++row ) {
            auto *item = parent->child( row );
            const auto cell = item->data( 0, Qt::UserRole ).toPoint();
            if ( cell == QPoint( 2, 2 ) && item->text( 0 ).startsWith( "Floor" ) ) floor = item;
            if ( cell == QPoint( 4, 2 ) ) stair = item;
            if ( cell == QPoint( 2, 2 ) && item->text( 0 ).startsWith( "Door" ) ) door = item;
        }
    }
    REQUIRE( floor ); REQUIRE( stair ); REQUIRE( door );
    floor->setSelected( true );
    stair->setSelected( true );
    REQUIRE( canvas->selectedCells().size() == 2 );
    CHECK( HasCell( *canvas, 2, 2 ) );
    CHECK( HasCell( *canvas, 4, 2 ) );
    CHECK_FALSE( HasCell( *canvas, 3, 2 ) );
    CHECK( door->isSelected() ); // A generated floor and its markers share an owning cell.
    door->setSelected( true );
    CHECK( canvas->selectedCells().size() == 2 );
    tree->clearSelection();
    CHECK_FALSE( canvas->hasSelection() );
    CHECK_FALSE( window.windowTitle().startsWith( '*' ) );
}

TEST_CASE( "Frame Active View frames the shared selection in Front and Side",
    "[TileEditor][MultiSelection][Integration][Framing]" )
{
    MultiWorkspaceApplication();
    multi_workspace_settings_t settings;
    QTemporaryDir maps;
    CypherTileEditorMainWindow window;
    REQUIRE( window.openFilePath( MultiMap( maps ), false ) );
    auto *canvas = MultiWidget<CypherTileCanvas>( window, "CypherTileCanvas" );
    SelectSparse( *canvas );
    auto *workspace = MultiWidget<CypherTileViewWorkspace>(
        window, "TileEditorFourViews" );
    auto *frame = MultiAction( window, "view.fit" );

    for ( const auto entry : {
              std::pair{ tile_editor_view_t::FRONT, "CypherTileFrontView" },
              std::pair{ tile_editor_view_t::SIDE, "CypherTileSideView" } } ) {
        auto *view = MultiWidget<CypherTileOrthoView>( window, entry.second );
        view->resize( 720, 420 );
        view->fitToView();
        const qreal mapScale = view->pixelsPerUnit();
        workspace->focusView( entry.first );
        frame->trigger();
        CHECK( workspace->activeWidget() == view );
        CHECK( view->pixelsPerUnit() > mapScale );
    }
}
