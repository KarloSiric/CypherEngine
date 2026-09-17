//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Exercises selection commands through the production Qt workspace.
//////////////////////////////////////////////////////////////////////////

#include "CypherTileCanvas.h"
#include "CypherTileDocumentBridge.h"
#include "CypherTileEditorMainWindow.h"
#include "CypherTileOrthoView.h"
#include "CypherTileRenderViewport.h"
#include "CypherTileViewWorkspace.h"

#include <catch2/catch_test_macros.hpp>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QFileInfo>
#include <QImage>
#include <QKeyEvent>
#include <QLineEdit>
#include <QSettings>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTabWidget>
#include <QTreeWidget>

using namespace cypher::common;
using namespace cypher::tools::tile_editor;

namespace
{

void EnsureRegionApplication()
{
    if ( QApplication::instance() != nullptr ) return;
    qputenv( "QT_QPA_PLATFORM", QByteArrayLiteral( "offscreen" ) );
    static int argc = 1;
    static char name[] = "CypherTileRegionWorkspaceTests";
    static char *argv[]{ name, nullptr };
    static QApplication application( argc, argv );
}

struct region_settings_t {
    QTemporaryDir directory;
    QString organization{ QCoreApplication::organizationName() };
    QString application{ QCoreApplication::applicationName() };
    QSettings::Format format{ QSettings::defaultFormat() };
    QString previousIniRoot;

    region_settings_t()
    {
        REQUIRE( directory.isValid() );
        const QSettings probe( QSettings::IniFormat, QSettings::UserScope,
                               "CypherRegionSettingsProbe", "Path" );
        previousIniRoot = QFileInfo( QFileInfo( probe.fileName() ).absolutePath() ).absolutePath();
        QSettings::setDefaultFormat( QSettings::IniFormat );
        QSettings::setPath( QSettings::IniFormat, QSettings::UserScope, directory.path() );
        QCoreApplication::setOrganizationName( "CypherTests" );
        QCoreApplication::setApplicationName( "TileRegionIntegration" );
        QSettings settings;
        tile_editor_preferences_t preferences;
        preferences.startMaximized = false;
        preferences.activateViewOnHover = false;
        TileEditorPreferences_Save( settings, preferences );
    }
    ~region_settings_t()
    {
        QCoreApplication::setOrganizationName( organization );
        QCoreApplication::setApplicationName( application );
        QSettings::setPath( QSettings::IniFormat, QSettings::UserScope, previousIniRoot );
        QSettings::setDefaultFormat( format );
    }
};

QAction *RegionAction( CypherTileEditorMainWindow &window, const char *name )
{
    auto *action = window.findChild<QAction *>( QString::fromLatin1( name ) );
    REQUIRE( action != nullptr );
    return action;
}

void TriggerRegionAction( CypherTileEditorMainWindow &window, const char *name )
{
    auto *action = RegionAction( window, name );
    REQUIRE( action->isEnabled() );
    action->trigger();
}

template <typename Widget>
Widget *RegionWidget( CypherTileEditorMainWindow &window, const char *name )
{
    auto *widget = window.findChild<QWidget *>( QString::fromLatin1( name ) );
    REQUIRE( widget != nullptr );
    // Editor widgets intentionally have no Q_OBJECT macro. Resolve their
    // documented object name through QWidget, then use their known type.
    return static_cast<Widget *>( widget );
}

QString RegionFixture( const QTemporaryDir &directory )
{
    CypherTileDocumentBridge document;
    QString error;
    REQUIRE( document.newDocument( { 10u, 8u, 2.0f, 3.0f }, &error ) );
    REQUIRE( document.beginEdit( "Region fixture", &error ) );
    REQUIRE( document.paintCell( { 1, 1 }, { 0, 2u, 4u }, &error ) );
    REQUIRE( document.paintCell( { 2, 1 },
                 { 0, 1u, 6u, tile_map_cell_shape_t::STAIRS_NORTH, 12u }, &error ) );
    REQUIRE( document.placeDoor( { 1, 1 }, tile_map_marker_side_t::SOUTH, nullptr, &error ) );
    REQUIRE( document.placePlayerSpawn( { 1, 1 }, 30.0f, &error ) );
    REQUIRE( document.commitEdit( &error ) );
    const QString path = directory.filePath( "region.cymap" );
    REQUIRE( document.saveToFile( path, &error ) );
    return path;
}

void SaveRegionAndLoad( CypherTileEditorMainWindow &window, const QString &path,
                        CypherTileDocumentBridge &loaded )
{
    TriggerRegionAction( window, "file.save" );
    QString error;
    REQUIRE( loaded.loadFromFile( path, &error ) );
}

void RequireRegion( CypherTileCanvas &canvas, i32 x, i32 y, u32 width, u32 height )
{
    REQUIRE( canvas.hasSelection() );
    const auto rectangle = canvas.selectionRect();
    CHECK( rectangle.x == x );
    CHECK( rectangle.y == y );
    CHECK( rectangle.nWidth == width );
    CHECK( rectangle.nHeight == height );
}

void RegionKey( QWidget &widget, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier )
{
    QKeyEvent press( QEvent::KeyPress, key, modifiers );
    QKeyEvent release( QEvent::KeyRelease, key, modifiers );
    QApplication::sendEvent( &widget, &press );
    QApplication::sendEvent( &widget, &release );
}

int RegionOutlinePixels( QWidget &view, const QColor &color )
{
    QImage image( view.size(), QImage::Format_ARGB32 );
    image.fill( Qt::transparent );
    view.render( &image );
    int count = 0;
    for ( int y = 0; y < image.height(); ++y ) {
        for ( int x = 0; x < image.width(); ++x ) {
            if ( image.pixelColor( x, y ).rgb() == color.rgb() ) ++count;
        }
    }
    return count;
}

} // namespace

TEST_CASE( "Workspace selection commands persist transformed cells and marker ownership",
           "[CypherTools][TileEditor][Region][Integration]" )
{
    EnsureRegionApplication();
    region_settings_t settings;
    QTemporaryDir maps;
    const QString path = RegionFixture( maps );
    CypherTileEditorMainWindow window;
    REQUIRE( window.openFilePath( path, false ) );
    auto *canvas = RegionWidget<CypherTileCanvas>( window, "CypherTileCanvas" );
    CHECK_FALSE( RegionAction( window, "edit.duplicate" )->isEnabled() );
    canvas->setSelectionRect( { 1, 1, 2u, 1u } );
    auto *shape = window.findChild<QComboBox *>( "TileSelectedShape" );
    REQUIRE( shape != nullptr );
    CHECK( shape->isEnabled() ); // Mixed selection fields support explicit per-property bulk edits.
    CHECK( shape->currentIndex() == -1 );
    TriggerRegionAction( window, "edit.duplicate" );
    RequireRegion( *canvas, 3, 1, 2u, 1u );
    CypherTileDocumentBridge loaded;
    SaveRegionAndLoad( window, path, loaded );
    CHECK( CypherTileMapDocument_CellAt( loaded.document(), { 3, 1 } )->nMaterialSlot == 4u );
    CHECK( CypherTileMapDocument_CellAt( loaded.document(), { 4, 1 } )->nStairSteps == 12u );
    CHECK( loaded.document()->markers.nCount == 3u );
    CHECK( CypherTileMapDocument_PlayerSpawn( loaded.document() )->cell.x == 1 );

    TriggerRegionAction( window, "edit.moveRight" );
    RequireRegion( *canvas, 4, 1, 2u, 1u );
    TriggerRegionAction( window, "edit.raise" );
    SaveRegionAndLoad( window, path, loaded );
    CHECK( CypherTileMapDocument_CellAt( loaded.document(), { 4, 1 } )->nFloorLevel == 1 );
    CHECK( CypherTileMapDocument_CellAt( loaded.document(), { 5, 1 } )->nFloorLevel == 1 );
    CHECK( CypherTileMapCell_IsCanonicalEmpty( *CypherTileMapDocument_CellAt( loaded.document(), { 3, 1 } ) ) );
    TriggerRegionAction( window, "edit.lower" );
    TriggerRegionAction( window, "edit.rotate" );
    RequireRegion( *canvas, 4, 1, 1u, 2u );
    SaveRegionAndLoad( window, path, loaded );
    const auto *rotated = CypherTileMapDocument_CellAt( loaded.document(), { 4, 2 } );
    CHECK( rotated->shape == tile_map_cell_shape_t::STAIRS_EAST );
    CHECK( rotated->nStairSteps == 12u );
    CHECK( rotated->nMaterialSlot == 6u );
    CHECK( rotated->nFloorLevel == 0 );
    CHECK( CypherTileMapDocument_DoorAt( loaded.document(), { 4, 1 }, tile_map_marker_side_t::WEST ) != nullptr );

    TriggerRegionAction( window, "edit.delete" );
    SaveRegionAndLoad( window, path, loaded );
    CHECK( loaded.document()->markers.nCount == 2u );
    CHECK_FALSE( CypherTileMapDocument_CellHasFloor( loaded.document(), { 4, 1 } ) );
    CHECK_FALSE( CypherTileMapDocument_CellHasFloor( loaded.document(), { 4, 2 } ) );
    TriggerRegionAction( window, "edit.undo" );
    SaveRegionAndLoad( window, path, loaded );
    CHECK( loaded.document()->markers.nCount == 3u );
    CHECK( CypherTileMapDocument_CellAt( loaded.document(), { 4, 2 } )->shape == tile_map_cell_shape_t::STAIRS_EAST );
    canvas->clearSelection();
    for ( const char *id : { "edit.move", "edit.duplicate", "edit.delete", "edit.raise", "edit.rotate" } ) {
        CHECK_FALSE( RegionAction( window, id )->isEnabled() );
    }
    canvas->selectCell( { 4, 2 } );
    CHECK( shape->isEnabled() );
}

TEST_CASE( "Workspace selection collisions leave the saved map and selection unchanged",
           "[CypherTools][TileEditor][Region][Integration]" )
{
    EnsureRegionApplication();
    region_settings_t settings;
    QTemporaryDir maps;
    const QString path = RegionFixture( maps );
    CypherTileEditorMainWindow window;
    REQUIRE( window.openFilePath( path, false ) );
    auto *canvas = RegionWidget<CypherTileCanvas>( window, "CypherTileCanvas" );
    canvas->selectCell( { 1, 1 } );
    TriggerRegionAction( window, "edit.moveRight" );
    RequireRegion( *canvas, 1, 1, 1u, 1u );
    CHECK_FALSE( RegionAction( window, "edit.undo" )->isEnabled() );
    CHECK_FALSE( window.windowTitle().startsWith( '*' ) );
    CypherTileDocumentBridge loaded;
    SaveRegionAndLoad( window, path, loaded );
    CHECK( CypherTileMapDocument_CellAt( loaded.document(), { 1, 1 } )->nMaterialSlot == 4u );
    CHECK( CypherTileMapDocument_CellAt( loaded.document(), { 2, 1 } )->shape == tile_map_cell_shape_t::STAIRS_NORTH );
    CHECK( loaded.document()->markers.nCount == 2u );
}

TEST_CASE( "Region shortcuts stay in viewports while console arrows and Delete edit text",
           "[CypherTools][TileEditor][Region][Integration]" )
{
    EnsureRegionApplication();
    region_settings_t settings;
    QTemporaryDir maps;
    const QString path = RegionFixture( maps );
    CypherTileEditorMainWindow window;
    REQUIRE( window.openFilePath( path, false ) );
    auto *canvas = RegionWidget<CypherTileCanvas>( window, "CypherTileCanvas" );
    auto *front = RegionWidget<CypherTileOrthoView>( window, "CypherTileFrontView" );
    auto *side = RegionWidget<CypherTileOrthoView>( window, "CypherTileSideView" );
    auto *render = RegionWidget<CypherTileRenderViewport>( window, "CypherTileRenderViewport" );
    auto *workspace = RegionWidget<CypherTileViewWorkspace>( window, "TileEditorFourViews" );
    for ( const char *id : { "edit.moveRight", "edit.delete", "edit.duplicate", "edit.raise", "edit.lower", "edit.rotate" } ) {
        auto *action = RegionAction( window, id );
        CHECK( action->shortcutContext() == Qt::WidgetShortcut );
        CHECK( canvas->actions().contains( action ) );
        CHECK( front->actions().contains( action ) );
        CHECK( side->actions().contains( action ) );
        CHECK( render->actions().contains( action ) );
    }
    workspace->focusView( tile_editor_view_t::TOP );
    if ( !workspace->isMaximized() ) workspace->toggleMaximize();
    window.resize( 1000, 760 );
    window.show(); // The unsupported offscreen OpenGL pane remains hidden.
    window.activateWindow();
    auto *consoleDock = window.findChild<QDockWidget *>( "TileEditorConsoleDock" );
    REQUIRE( consoleDock != nullptr );
    consoleDock->show();
    consoleDock->raise();
    QApplication::processEvents();
    canvas->selectCell( { 2, 1 } );
    auto *input = window.findChild<QLineEdit *>( "TileConsoleInput" );
    REQUIRE( input != nullptr );
    input->setText( "abcd" );
    input->setCursorPosition( 1 );
    input->setFocus();
    QApplication::processEvents();
    REQUIRE( input->hasFocus() );
    RegionKey( *input, Qt::Key_Right );
    CHECK( input->cursorPosition() == 2 );
    RegionKey( *input, Qt::Key_Delete );
    CHECK( input->text() == "abd" );
    RequireRegion( *canvas, 2, 1, 1u, 1u );
    CHECK_FALSE( RegionAction( window, "edit.undo" )->isEnabled() );
    canvas->setFocus();
    QApplication::processEvents();
    REQUIRE( canvas->hasFocus() );
    RegionKey( *canvas, Qt::Key_Right );
    RequireRegion( *canvas, 3, 1, 1u, 1u );
    CHECK( RegionAction( window, "edit.undo" )->isEnabled() );
    window.hide();
}

TEST_CASE( "Workspace region selection highlights both orthographic projections and frames the 3D region",
           "[CypherTools][TileEditor][Region][Integration]" )
{
    EnsureRegionApplication();
    region_settings_t settings;
    QTemporaryDir maps;
    const QString path = RegionFixture( maps );
    CypherTileEditorMainWindow window;
    REQUIRE( window.openFilePath( path, false ) );
    auto *canvas = RegionWidget<CypherTileCanvas>( window, "CypherTileCanvas" );
    auto *front = RegionWidget<CypherTileOrthoView>( window, "CypherTileFrontView" );
    auto *side = RegionWidget<CypherTileOrthoView>( window, "CypherTileSideView" );
    auto *render = RegionWidget<CypherTileRenderViewport>( window, "CypherTileRenderViewport" );
    tile_editor_preferences_t preferences;
    preferences.selectionColor = QColor( 253, 17, 227 );
    for ( auto *view : { front, side } ) {
        view->resize( 400, 320 );
        view->setPreferences( preferences );
        view->fitToView();
        CHECK( RegionOutlinePixels( *view, preferences.selectionColor ) == 0 );
    }
    canvas->setSelectionRect( { 1, 1, 2u, 1u } );
    for ( auto *view : { front, side } ) {
        CHECK( RegionOutlinePixels( *view, preferences.selectionColor ) > 20 );
    }
    render->frameSelection();
    const auto regionPosition = render->camera().position;
    canvas->selectCell( { 1, 1 } );
    render->frameSelection();
    const auto cellPosition = render->camera().position;
    CHECK( ( regionPosition.x != cellPosition.x || regionPosition.y != cellPosition.y ||
             regionPosition.z != cellPosition.z ) );
    canvas->clearSelection();
    for ( auto *view : { front, side } ) {
        CHECK( RegionOutlinePixels( *view, preferences.selectionColor ) == 0 );
    }
    auto *assets = window.findChild<QDockWidget *>( "TileEditorToolsDock" );
    auto *inspector = window.findChild<QDockWidget *>( "TileEditorInspectorDock" );
    auto *outliner = window.findChild<QDockWidget *>( "TileEditorOutlinerDock" );
    auto *console = window.findChild<QDockWidget *>( "TileEditorConsoleDock" );
    REQUIRE( assets != nullptr );
    REQUIRE( inspector != nullptr );
    REQUIRE( outliner != nullptr );
    REQUIRE( console != nullptr );
    CHECK( window.dockWidgetArea( assets ) == Qt::RightDockWidgetArea );
    CHECK( window.dockWidgetArea( inspector ) == Qt::RightDockWidgetArea );
    CHECK( window.dockWidgetArea( outliner ) == Qt::RightDockWidgetArea );
    CHECK( window.dockWidgetArea( console ) == Qt::BottomDockWidgetArea );
    CHECK_FALSE( window.tabifiedDockWidgets( assets ).contains( console ) );
    CHECK_FALSE( window.tabifiedDockWidgets( assets ).contains( inspector ) );
    CHECK_FALSE( window.tabifiedDockWidgets( assets ).contains( outliner ) );

    auto *tree = window.findChild<QTreeWidget *>( "TileObjectTree" );
    auto *filter = window.findChild<QLineEdit *>( "TileObjectFilter" );
    REQUIRE( tree != nullptr );
    REQUIRE( filter != nullptr );
    filter->setText( "Stair" );
    QTreeWidgetItem *stair = nullptr;
    int visibleRows = 0;
    for ( int i = 0; i < tree->topLevelItemCount(); ++i ) {
        auto *group = tree->topLevelItem( i );
        if ( group->isHidden() ) continue;
        visibleRows += group->childCount();
        if ( group->childCount() > 0 ) stair = group->child( 0 );
    }
    REQUIRE( visibleRows == 1 );
    REQUIRE( stair != nullptr );
    tree->setCurrentItem( stair );
    RequireRegion( *canvas, 2, 1, 1u, 1u );
}

TEST_CASE( "Legacy three-tab right workspace migrates to independent docks",
           "[CypherTools][TileEditor][Workspace][Migration]" )
{
    EnsureRegionApplication();
    region_settings_t settingsRoot;
    {
        CypherTileEditorMainWindow legacy;
        auto *assets = legacy.findChild<QDockWidget *>( "TileEditorToolsDock" );
        auto *outliner = legacy.findChild<QDockWidget *>( "TileEditorOutlinerDock" );
        auto *inspector = legacy.findChild<QDockWidget *>( "TileEditorInspectorDock" );
        REQUIRE( assets != nullptr );
        REQUIRE( outliner != nullptr );
        REQUIRE( inspector != nullptr );
        legacy.tabifyDockWidget( assets, outliner );
        legacy.tabifyDockWidget( assets, inspector );
        REQUIRE( legacy.tabifiedDockWidgets( assets ).contains( outliner ) );
        REQUIRE( legacy.tabifiedDockWidgets( assets ).contains( inspector ) );
        assets->hide();
        outliner->show();
        inspector->hide();
        QSettings settings;
        settings.setValue( "TileEditor/windowStateV1", legacy.saveState( 3 ) );
        settings.setValue( "TileEditor/independentMaterialsDockV1", true );
        settings.remove( "TileEditor/independentMaterialsDocksV2" );
    }

    CypherTileEditorMainWindow migrated;
    auto *assets = migrated.findChild<QDockWidget *>( "TileEditorToolsDock" );
    auto *outliner = migrated.findChild<QDockWidget *>( "TileEditorOutlinerDock" );
    auto *inspector = migrated.findChild<QDockWidget *>( "TileEditorInspectorDock" );
    REQUIRE( assets != nullptr );
    REQUIRE( outliner != nullptr );
    REQUIRE( inspector != nullptr );
    CHECK( migrated.tabifiedDockWidgets( assets ).isEmpty() );
    CHECK( migrated.tabifiedDockWidgets( outliner ).isEmpty() );
    CHECK( migrated.tabifiedDockWidgets( inspector ).isEmpty() );
    CHECK( migrated.dockWidgetArea( assets ) == Qt::RightDockWidgetArea );
    CHECK( migrated.dockWidgetArea( outliner ) == Qt::RightDockWidgetArea );
    CHECK( migrated.dockWidgetArea( inspector ) == Qt::RightDockWidgetArea );
    CHECK( assets->isHidden() );
    CHECK_FALSE( outliner->isHidden() );
    CHECK( inspector->isHidden() );
    QSettings settings;
    CHECK( settings.value( "TileEditor/independentMaterialsDockV1" ).toBool() );
    CHECK( settings.value( "TileEditor/independentMaterialsDocksV2" ).toBool() );
}

TEST_CASE( "Materials dock action is a pure visibility toggle and mirrors external visibility",
           "[CypherTools][TileEditor][Workspace][Docks]" )
{
    EnsureRegionApplication();
    region_settings_t settings;
    CypherTileEditorMainWindow window;
    window.resize( 1200, 800 );
    window.show();
    QApplication::processEvents();

    auto *assets = window.findChild<QDockWidget *>( "TileEditorToolsDock" );
    auto *inspector = window.findChild<QDockWidget *>( "TileEditorInspectorDock" );
    auto *outliner = window.findChild<QDockWidget *>( "TileEditorOutlinerDock" );
    auto *console = window.findChild<QDockWidget *>( "TileEditorConsoleDock" );
    auto *assetTabs = window.findChild<QTabWidget *>( "TileAssetTabs" );
    auto *inspectorTabs = window.findChild<QTabWidget *>( "TileInspectorTabs" );
    auto *materialsAction = RegionAction( window, "view.assets" );
    auto *propertiesAction = RegionAction( window, "view.properties" );
    auto *outlinerAction = RegionAction( window, "view.outliner" );
    auto *surfacePreviewAction = RegionAction( window, "view.orthoMaterials" );
    REQUIRE( assets );
    REQUIRE( inspector );
    REQUIRE( outliner );
    REQUIRE( console );
    REQUIRE( assetTabs );
    REQUIRE( inspectorTabs );
    REQUIRE( materialsAction->isCheckable() );
    REQUIRE( assetTabs->count() >= 3 );
    REQUIRE( inspectorTabs->count() >= 3 );

    // Use non-default pages so an accidental "show materials" implementation
    // that also chooses a palette or inspector page is observable.
    assetTabs->setCurrentIndex( assetTabs->count() - 1 );
    inspectorTabs->setCurrentIndex( inspectorTabs->count() - 1 );
    QWidget *const assetPage = assetTabs->currentWidget();
    QWidget *const inspectorPage = inspectorTabs->currentWidget();

    inspector->show();
    outliner->hide();
    console->hide();
    QApplication::processEvents();
    const bool propertiesChecked = propertiesAction->isChecked();
    const bool outlinerChecked = outlinerAction->isChecked();
    const bool surfacePreviewChecked = surfacePreviewAction->isChecked();

    materialsAction->setChecked( false );
    QApplication::processEvents();
    CHECK( assets->isHidden() );
    CHECK_FALSE( materialsAction->isChecked() );
    CHECK( assetTabs->currentWidget() == assetPage );
    CHECK( inspectorTabs->currentWidget() == inspectorPage );
    CHECK( inspector->isVisible() );
    CHECK( outliner->isHidden() );
    CHECK( console->isHidden() );
    CHECK( propertiesAction->isChecked() == propertiesChecked );
    CHECK( outlinerAction->isChecked() == outlinerChecked );
    CHECK( surfacePreviewAction->isChecked() == surfacePreviewChecked );

    materialsAction->setChecked( true );
    QApplication::processEvents();
    CHECK( assets->isVisible() );
    CHECK( materialsAction->isChecked() );
    CHECK( assetTabs->currentWidget() == assetPage );
    CHECK( inspectorTabs->currentWidget() == inspectorPage );
    CHECK( inspector->isVisible() );
    CHECK( outliner->isHidden() );
    CHECK( console->isHidden() );
    CHECK( propertiesAction->isChecked() == propertiesChecked );
    CHECK( outlinerAction->isChecked() == outlinerChecked );
    CHECK( surfacePreviewAction->isChecked() == surfacePreviewChecked );

    // Dock close buttons, layout restoration and native window controls act
    // on the dock directly. The toolbar/menu action must still mirror that
    // source of truth without feeding a second visibility change back.
    assets->hide();
    QApplication::processEvents();
    CHECK_FALSE( materialsAction->isChecked() );
    CHECK( assets->isHidden() );
    assets->show();
    QApplication::processEvents();
    CHECK( materialsAction->isChecked() );
    CHECK( assets->isVisible() );
    CHECK( assetTabs->currentWidget() == assetPage );
    CHECK( inspectorTabs->currentWidget() == inspectorPage );
    window.hide();
}

TEST_CASE( "Materials validation build and run actions have distinct renderable icons",
           "[CypherTools][TileEditor][Workspace][Icons]" )
{
    EnsureRegionApplication();
    region_settings_t settings;
    CypherTileEditorMainWindow window;
    QAction *const materialsDock = RegionAction( window, "view.assets" );
    QAction *const materialSurfaces = RegionAction( window, "view.orthoMaterials" );
    QAction *const validate = RegionAction( window, "map.validate" );
    QAction *const build = RegionAction( window, "map.build" );
    QAction *const run = RegionAction( window, "map.preview" );

    for ( QAction *action : { materialsDock, materialSurfaces, validate, build, run } ) {
        INFO( action->objectName().toStdString() );
        CHECK_FALSE( action->icon().isNull() );
        CHECK_FALSE( action->icon().pixmap( 24, 24 ).isNull() );
        CHECK( action->icon().cacheKey() != 0 );
    }
    CHECK( materialsDock->icon().cacheKey() != materialSurfaces->icon().cacheKey() );
    CHECK( validate->icon().cacheKey() != build->icon().cacheKey() );
    CHECK( validate->icon().cacheKey() != run->icon().cacheKey() );
    CHECK( build->icon().cacheKey() != run->icon().cacheKey() );
}
