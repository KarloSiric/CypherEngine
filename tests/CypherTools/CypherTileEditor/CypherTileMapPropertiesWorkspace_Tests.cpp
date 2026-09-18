//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Exercises live map properties through production editor controls.
//////////////////////////////////////////////////////////////////////////

#include "CypherTileCanvas.h"
#include "CypherTileDocumentBridge.h"
#include "CypherTileEditorConfig.h"
#include "CypherTileEditorMainWindow.h"
#include "CypherTileMapProperties.h"
#include "CypherTileOrthoView.h"
#include "CypherTileRenderViewport.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QAction>
#include <QAbstractItemView>
#include <QApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>

using namespace cypher::common;
using namespace cypher::tools::tile_editor;

namespace
{

void EnsureMapPropertiesApplication()
{
    if ( QApplication::instance() ) return;
    qputenv( "QT_QPA_PLATFORM", QByteArrayLiteral( "offscreen" ) );
    static int argc = 1;
    static char name[] = "CypherTileMapPropertiesWorkspaceTests";
    static char *argv[]{ name, nullptr };
    static QApplication application( argc, argv );
}

struct map_properties_settings_t {
    QTemporaryDir directory;
    QString organization{ QCoreApplication::organizationName() };
    QString application{ QCoreApplication::applicationName() };
    QSettings::Format format{ QSettings::defaultFormat() };
    QString previousIniRoot;
    QString configRoot;
    QString cacheRoot;

    map_properties_settings_t()
    {
        REQUIRE( directory.isValid() );
        const QSettings probe( QSettings::IniFormat, QSettings::UserScope, "CypherPropertiesProbe", "Path" );
        previousIniRoot = QFileInfo( QFileInfo( probe.fileName() ).absolutePath() ).absolutePath();
        QSettings::setDefaultFormat( QSettings::IniFormat );
        QSettings::setPath( QSettings::IniFormat, QSettings::UserScope, directory.path() );
        QCoreApplication::setOrganizationName( "CypherTests" );
        QCoreApplication::setApplicationName( "TileMapProperties-" + QFileInfo( directory.path() ).fileName() );
        configRoot = QFileInfo( TileEditorConfig_DefaultPath() ).absolutePath();
        cacheRoot = QStandardPaths::writableLocation( QStandardPaths::CacheLocation );
        tile_editor_preferences_t preferences;
        preferences.startMaximized = false;
        preferences.activateViewOnHover = false;
        QSettings settings;
        TileEditorPreferences_Save( settings, preferences );
        QString error;
        REQUIRE( TileEditorConfig_Save( TileEditorConfig_DefaultPath(), preferences, error ) );
    }

    ~map_properties_settings_t()
    {
        // These paths belong to the unique test profile, never the user's editor.
        if ( !configRoot.isEmpty() ) QDir( configRoot ).removeRecursively();
        if ( !cacheRoot.isEmpty() ) QDir( cacheRoot ).removeRecursively();
        QCoreApplication::setOrganizationName( organization );
        QCoreApplication::setApplicationName( application );
        QSettings::setPath( QSettings::IniFormat, QSettings::UserScope, previousIniRoot );
        QSettings::setDefaultFormat( format );
    }
};

template <typename Widget>
Widget *PropertiesWidget( CypherTileEditorMainWindow &window, const char *name )
{
    auto *widget = window.findChild<QWidget *>( QString::fromLatin1( name ) );
    REQUIRE( widget != nullptr );
    // The editor's own QWidget subclasses deliberately have no Q_OBJECT macro.
    return static_cast<Widget *>( widget );
}

QAction *PropertiesAction( CypherTileEditorMainWindow &window, const char *name )
{
    auto *action = window.findChild<QAction *>( QString::fromLatin1( name ) );
    REQUIRE( action != nullptr );
    return action;
}

void TriggerPropertiesAction( CypherTileEditorMainWindow &window, const char *name )
{
    auto *action = PropertiesAction( window, name );
    REQUIRE( action->isEnabled() );
    action->trigger();
}

QString PropertiesFixture( const QTemporaryDir &directory )
{
    REQUIRE( directory.isValid() );
    CypherTileDocumentBridge document;
    QString error;
    REQUIRE( document.newDocument( { 10u, 8u, 2.0f, 3.0f }, &error ) );
    REQUIRE( document.beginEdit( "Map properties fixture", &error ) );
    REQUIRE( document.paintCell( { 1, 1 }, { 0, 2u, 4u }, &error ) );
    REQUIRE( document.paintCell( { 2, 1 },
        { 0, 1u, 6u, tile_map_cell_shape_t::STAIRS_EAST, 12u }, &error ) );
    REQUIRE( document.paintCell( { 7, 5 }, { 1, 1u, 2u }, &error ) );
    REQUIRE( document.placeDoor( { 1, 1 }, tile_map_marker_side_t::NORTH, nullptr, &error ) );
    REQUIRE( document.placePlayerSpawn( { 1, 1 }, 30.0f, &error ) );
    REQUIRE( document.commitEdit( &error ) );
    const QString path = directory.filePath( "map-properties.cymap" );
    REQUIRE( document.saveToFile( path, &error ) );
    return path;
}

QByteArray ReadPropertiesFile( const QString &path )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    return file.readAll();
}

void StageProperties( CypherTileEditorMainWindow &window, int width, int height, double cell, double level )
{
    PropertiesWidget<QSpinBox>( window, "TileMapWidth" )->setValue( width );
    PropertiesWidget<QSpinBox>( window, "TileMapHeight" )->setValue( height );
    PropertiesWidget<QDoubleSpinBox>( window, "TileMapCellSize" )->setValue( cell );
    PropertiesWidget<QDoubleSpinBox>( window, "TileMapLevelHeight" )->setValue( level );
}

void ApplyProperties( CypherTileEditorMainWindow &window )
{
    auto *apply = PropertiesWidget<QPushButton>( window, "TileMapApply" );
    REQUIRE( apply->isEnabled() );
    apply->click();
}

void CheckPropertiesFields( CypherTileEditorMainWindow &window, int width, int height, double cell, double level )
{
    CHECK( PropertiesWidget<QSpinBox>( window, "TileMapWidth" )->value() == width );
    CHECK( PropertiesWidget<QSpinBox>( window, "TileMapHeight" )->value() == height );
    CHECK( PropertiesWidget<QDoubleSpinBox>( window, "TileMapCellSize" )->value() == Catch::Approx( cell ) );
    CHECK( PropertiesWidget<QDoubleSpinBox>( window, "TileMapLevelHeight" )->value() == Catch::Approx( level ) );
}

void SubmitPropertiesCommand( CypherTileEditorMainWindow &window, const QString &command )
{
    auto *input = PropertiesWidget<QLineEdit>( window, "TileConsoleInput" );
    input->setText( command );
    QKeyEvent event( QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier );
    QApplication::sendEvent( input, &event );
    REQUIRE( input->text().isEmpty() );
}

} // namespace

TEST_CASE( "Map properties update live views and preserve manual navigation until saved",
           "[TileEditor][MapProperties][Integration]" )
{
    EnsureMapPropertiesApplication();
    map_properties_settings_t settings;
    QTemporaryDir maps;
    const QString path = PropertiesFixture( maps );
    const QByteArray originalText = ReadPropertiesFile( path );
    CypherTileEditorMainWindow window;
    REQUIRE( window.openFilePath( path, false ) );
    // Keep this production window hidden: offscreen Qt tests behavior, not OpenGL.
    auto *canvas = PropertiesWidget<CypherTileCanvas>( window, "CypherTileCanvas" );
    auto *viewport = PropertiesWidget<CypherTileRenderViewport>( window, "CypherTileRenderViewport" );
    auto *front = PropertiesWidget<CypherTileOrthoView>( window, "CypherTileFrontView" );
    auto *side = PropertiesWidget<CypherTileOrthoView>( window, "CypherTileSideView" );
    canvas->selectCell( { 2, 1 }, true );
    canvas->setZoomFactor( 73 );
    REQUIRE( viewport->goToPlayerSpawn() );
    const auto camera = viewport->camera();
    const auto topOrigin = canvas->viewOrigin();
    const auto frontOrigin = front->viewOrigin();
    const auto sideOrigin = side->viewOrigin();
    const auto frontScale = front->pixelsPerUnit();
    const auto sideScale = side->pixelsPerUnit();

    TriggerPropertiesAction( window, "map.properties" );
    auto *tabs = PropertiesWidget<QTabWidget>( window, "TileInspectorTabs" );
    CHECK( tabs->currentWidget()->findChild<QWidget *>( "TileMapProperties" ) != nullptr );
    StageProperties( window, 16, 12, 4.0, 6.0 );
    CHECK_FALSE( window.windowTitle().startsWith( '*' ) );
    ApplyProperties( window );

    CHECK( window.windowTitle().startsWith( '*' ) );
    CHECK( ReadPropertiesFile( path ) == originalText );
    CHECK( canvas->selectedCell().x == 2 );
    CHECK( canvas->selectedCell().y == 1 );
    CHECK( canvas->zoomFactor() == 73 );
    CHECK( canvas->viewOrigin() == topOrigin );
    CHECK( front->viewOrigin() == frontOrigin );
    CHECK( side->viewOrigin() == sideOrigin );
    CHECK( front->pixelsPerUnit() == frontScale );
    CHECK( side->pixelsPerUnit() == sideScale );
    CHECK( viewport->camera().position.x == camera.position.x );
    CHECK( viewport->camera().position.y == camera.position.y );
    CHECK( viewport->camera().position.z == camera.position.z );
    CHECK( viewport->camera().yawRadians == camera.yawRadians );
    CHECK( viewport->camera().pitchRadians == camera.pitchRadians );
    CHECK( viewport->camera().boundsCenter.x == Catch::Approx( camera.boundsCenter.x * 2.0f ) );
    CHECK( viewport->camera().boundsRadius > camera.boundsRadius );
    CHECK( PropertiesAction( window, "edit.undo" )->isEnabled() );
    CHECK_FALSE( PropertiesWidget<QPushButton>( window, "TileMapApply" )->isEnabled() );

    CypherTileDocumentBridge before;
    CypherTileDocumentBridge after;
    QString error;
    REQUIRE( before.loadFromFile( path, &error ) );
    TriggerPropertiesAction( window, "file.save" );
    REQUIRE( after.loadFromFile( path, &error ) );
    CHECK_FALSE( window.windowTitle().startsWith( '*' ) );
    CHECK( after.filePath() == before.filePath() );
    CHECK( UniqueId_Equals( after.document()->mapId, before.document()->mapId ) );
    CHECK( after.document()->nWidth == 16u );
    CHECK( after.document()->nHeight == 12u );
    CHECK( after.document()->nCellSize == 4.0f );
    CHECK( after.document()->nLevelHeight == 6.0f );
    REQUIRE( after.document()->markers.nCount == before.document()->markers.nCount );
    for ( usize i = 0; i < after.document()->markers.nCount; ++i ) {
        CHECK( UniqueId_Equals( after.document()->markers.pData[i].id, before.document()->markers.pData[i].id ) );
        CHECK( after.document()->markers.pData[i].cell.x == before.document()->markers.pData[i].cell.x );
        CHECK( after.document()->markers.pData[i].cell.y == before.document()->markers.pData[i].cell.y );
    }
    const auto *stair = CypherTileMapDocument_CellAt( after.document(), { 2, 1 } );
    REQUIRE( stair != nullptr );
    CHECK( stair->shape == tile_map_cell_shape_t::STAIRS_EAST );
    CHECK( stair->nStairSteps == 12u );
    CHECK( stair->nMaterialSlot == 6u );
    CHECK( CypherTileMapDocument_CellHasFloor( after.document(), { 7, 5 } ) );
}

TEST_CASE( "One properties undo restores all fields and keeps editing history usable across map widths",
           "[TileEditor][MapProperties][Integration]" )
{
    EnsureMapPropertiesApplication();
    map_properties_settings_t settings;
    QTemporaryDir maps;
    const QString path = PropertiesFixture( maps );
    CypherTileEditorMainWindow window;
    REQUIRE( window.openFilePath( path, false ) );
    StageProperties( window, 14, 11, 2.5, 4.25 );
    ApplyProperties( window );
    TriggerPropertiesAction( window, "edit.undo" );
    CheckPropertiesFields( window, 10, 8, 2.0, 3.0 );
    CHECK_FALSE( window.windowTitle().startsWith( '*' ) );
    CHECK_FALSE( PropertiesAction( window, "edit.undo" )->isEnabled() );
    TriggerPropertiesAction( window, "edit.redo" );
    CheckPropertiesFields( window, 14, 11, 2.5, 4.25 );

    auto *canvas = PropertiesWidget<CypherTileCanvas>( window, "CypherTileCanvas" );
    canvas->selectCell( { 7, 5 } );
    TriggerPropertiesAction( window, "edit.moveRight" );
    TriggerPropertiesAction( window, "edit.undo" );
    TriggerPropertiesAction( window, "edit.undo" );
    CheckPropertiesFields( window, 10, 8, 2.0, 3.0 );
    TriggerPropertiesAction( window, "edit.redo" );
    TriggerPropertiesAction( window, "edit.redo" );
    TriggerPropertiesAction( window, "file.save" );
    CypherTileDocumentBridge saved;
    QString error;
    REQUIRE( saved.loadFromFile( path, &error ) );
    CHECK( saved.document()->nWidth == 14u );
    CHECK( CypherTileMapDocument_CellHasFloor( saved.document(), { 8, 5 } ) );
    CHECK_FALSE( CypherTileMapDocument_CellHasFloor( saved.document(), { 7, 5 } ) );
    CHECK( CypherTileMapDocument_CellHasFloor( saved.document(), { 1, 1 } ) );
}

TEST_CASE( "Properties history tab follows the shared document undo cursor",
           "[TileEditor][History][Integration]" )
{
    EnsureMapPropertiesApplication();
    map_properties_settings_t settings;
    QTemporaryDir maps;
    const QString path = PropertiesFixture( maps );
    CypherTileEditorMainWindow window;
    REQUIRE( window.openFilePath( path, false ) );

    auto *tabs = PropertiesWidget<QTabWidget>( window, "TileInspectorTabs" );
    auto *history = PropertiesWidget<QWidget>( window, "TileHistoryPanel" );
    const int historyTab = tabs->indexOf( history );
    REQUIRE( historyTab >= 0 );
    REQUIRE( tabs->count() == 5 );
    CHECK( tabs->tabText( 0 ) == "Selection" );
    CHECK( tabs->tabText( 1 ) == "Paint" );
    CHECK( tabs->tabText( 2 ) == "Map" );
    CHECK( tabs->tabText( 3 ).startsWith( "Checks" ) );
    CHECK( tabs->tabText( historyTab ) == "History" );
    CHECK( historyTab == 4 );

    auto *tree = PropertiesWidget<QTreeWidget>( window, "TileHistoryTree" );
    auto *badge = PropertiesWidget<QLabel>( window, "TileHistoryStateBadge" );
    auto *undo = PropertiesWidget<QToolButton>( window, "TileHistoryUndo" );
    auto *redo = PropertiesWidget<QToolButton>( window, "TileHistoryRedo" );
    REQUIRE( tree->topLevelItemCount() == 1 );
    CHECK( tree->selectionMode() == QAbstractItemView::NoSelection );
    CHECK( tree->currentItem() == nullptr );
    CHECK( tree->selectedItems().isEmpty() );
    CHECK( tree->topLevelItem( 0 )->text( 0 ).contains( "Current document" ) );
    CHECK( badge->text() == "SAVED" );
    CHECK_FALSE( undo->isEnabled() );
    CHECK_FALSE( redo->isEnabled() );

    StageProperties( window, 14, 11, 2.5, 4.25 );
    ApplyProperties( window );
    REQUIRE( tree->topLevelItemCount() == 2 );
    CHECK( tree->topLevelItem( 1 )->text( 0 ).contains( "Map properties" ) );
    CHECK( tree->topLevelItem( 1 )->text( 1 ).contains( "Current" ) );
    CHECK( tree->currentItem() == nullptr );
    CHECK( tree->selectedItems().isEmpty() );
    CHECK( badge->text() == "UNSAVED" );
    CHECK( undo->isEnabled() );
    CHECK_FALSE( redo->isEnabled() );

    undo->click();
    CheckPropertiesFields( window, 10, 8, 2.0, 3.0 );
    CHECK( tree->currentItem() == nullptr );
    CHECK( tree->selectedItems().isEmpty() );
    CHECK( tree->topLevelItem( 0 )->text( 1 ).contains( "Current" ) );
    CHECK( tree->topLevelItem( 1 )->text( 1 ).contains( "Redo" ) );
    CHECK( badge->text() == "SAVED" );
    CHECK( redo->isEnabled() );

    redo->click();
    CheckPropertiesFields( window, 14, 11, 2.5, 4.25 );
    CHECK( tree->currentItem() == nullptr );
    CHECK( tree->selectedItems().isEmpty() );
    CHECK( tree->topLevelItem( 1 )->text( 1 ).contains( "Current" ) );
    CHECK( badge->text() == "UNSAVED" );
}

TEST_CASE( "Rejected map shrink leaves the saved document and staged properties intact",
           "[TileEditor][MapProperties][Integration]" )
{
    EnsureMapPropertiesApplication();
    map_properties_settings_t settings;
    QTemporaryDir maps;
    const QString path = PropertiesFixture( maps );
    const auto originalText = ReadPropertiesFile( path );
    CypherTileEditorMainWindow window;
    REQUIRE( window.openFilePath( path, false ) );
    auto *output = PropertiesWidget<QPlainTextEdit>( window, "TileConsoleOutput" );
    output->clear();
    StageProperties( window, 7, 5, 5.0, 7.0 );
    ApplyProperties( window );
    CHECK_FALSE( window.windowTitle().startsWith( '*' ) );
    CHECK_FALSE( PropertiesAction( window, "edit.undo" )->isEnabled() );
    CHECK( ReadPropertiesFile( path ) == originalText );
    CHECK_FALSE( output->toPlainText().isEmpty() );
    CheckPropertiesFields( window, 7, 5, 5.0, 7.0 );
    CHECK( PropertiesWidget<QPushButton>( window, "TileMapApply" )->isEnabled() );
    CHECK_FALSE( PropertiesWidget<QLabel>( window, "TileMapPropertiesMessage" )->text().isEmpty() );
    PropertiesWidget<QPushButton>( window, "TileMapRevert" )->click();
    CheckPropertiesFields( window, 10, 8, 2.0, 3.0 );
    TriggerPropertiesAction( window, "file.save" );
    CHECK( ReadPropertiesFile( path ) == originalText );
}

TEST_CASE( "Shrinking empty map borders clamps selection without losing retained geometry",
           "[TileEditor][MapProperties][Integration]" )
{
    EnsureMapPropertiesApplication();
    map_properties_settings_t settings;
    QTemporaryDir maps;
    const QString path = PropertiesFixture( maps );
    CypherTileEditorMainWindow window;
    REQUIRE( window.openFilePath( path, false ) );
    auto *canvas = PropertiesWidget<CypherTileCanvas>( window, "CypherTileCanvas" );
    canvas->setSelectionRect( { 6, 4, 4u, 4u } );
    StageProperties( window, 8, 6, 2.0, 3.0 );
    ApplyProperties( window );
    REQUIRE( canvas->hasSelection() );
    CHECK( canvas->selectionRect().x == 6 );
    CHECK( canvas->selectionRect().y == 4 );
    CHECK( canvas->selectionRect().nWidth == 2u );
    CHECK( canvas->selectionRect().nHeight == 2u );
    TriggerPropertiesAction( window, "edit.undo" );
    canvas->setSelectionRect( { 9, 7, 1u, 1u } );
    TriggerPropertiesAction( window, "edit.redo" );
    CHECK_FALSE( canvas->hasSelection() );
    CHECK_FALSE( PropertiesAction( window, "edit.delete" )->isEnabled() );
    TriggerPropertiesAction( window, "file.save" );
    CypherTileDocumentBridge saved;
    QString error;
    REQUIRE( saved.loadFromFile( path, &error ) );
    CHECK( saved.document()->nWidth == 8u );
    CHECK( saved.document()->nHeight == 6u );
    CHECK( CypherTileMapDocument_CellHasFloor( saved.document(), { 7, 5 } ) );
    CHECK( saved.document()->markers.nCount == 2u );
}

TEST_CASE( "Map properties console rejects malformed commands and shares the live undoable operation",
           "[TileEditor][MapProperties][Integration]" )
{
    EnsureMapPropertiesApplication();
    map_properties_settings_t settings;
    QTemporaryDir maps;
    const QString path = PropertiesFixture( maps );
    const auto originalText = ReadPropertiesFile( path );
    CypherTileEditorMainWindow window;
    REQUIRE( window.openFilePath( path, false ) );
    auto *output = PropertiesWidget<QPlainTextEdit>( window, "TileConsoleOutput" );
    for ( const auto *command : { "map_properties 12", "map_properties 12 10 bad 3",
             "map_properties -1 10 2 3", "map_properties 12 10 nan 3",
             "map_properties 12 10 2 3 extra", "map_properties 1025 10 2 3" } ) {
        INFO( command );
        output->clear();
        SubmitPropertiesCommand( window, QString::fromLatin1( command ) );
        CHECK( output->toPlainText().contains( "error", Qt::CaseInsensitive ) );
        CHECK_FALSE( window.windowTitle().startsWith( '*' ) );
        CHECK_FALSE( PropertiesAction( window, "edit.undo" )->isEnabled() );
        CheckPropertiesFields( window, 10, 8, 2.0, 3.0 );
    }
    CHECK( ReadPropertiesFile( path ) == originalText );
    SubmitPropertiesCommand( window, "map_properties" );
    CHECK( PropertiesWidget<QTabWidget>( window, "TileInspectorTabs" )->currentWidget()
        ->findChild<QWidget *>( "TileMapProperties" ) != nullptr );
    SubmitPropertiesCommand( window, "map_properties 18 9 1.5 2.25" );
    CheckPropertiesFields( window, 18, 9, 1.5, 2.25 );
    CHECK( window.windowTitle().startsWith( '*' ) );
    TriggerPropertiesAction( window, "edit.undo" );
    CheckPropertiesFields( window, 10, 8, 2.0, 3.0 );
    CHECK_FALSE( window.windowTitle().startsWith( '*' ) );
}
