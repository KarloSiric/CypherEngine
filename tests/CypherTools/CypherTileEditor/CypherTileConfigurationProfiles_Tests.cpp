//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Verifies live editor profiles and camera commands preserve authoring state.
//////////////////////////////////////////////////////////////////////////

#include "CypherTileCanvas.h"
#include "CypherTileDocumentBridge.h"
#include "CypherTileEditorConfig.h"
#include "CypherTileEditorMainWindow.h"
#include "CypherTileRenderViewport.h"
#include "CypherTileViewWorkspace.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMenu>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QToolBar>
#include <QToolButton>

#include <array>

using namespace cypher::common;
using namespace cypher::tools::tile_editor;

namespace
{

void EnsureProfileApplication()
{
    if ( QApplication::instance() )
        return;
    qputenv( "QT_QPA_PLATFORM", QByteArrayLiteral( "offscreen" ) );
    static int argc = 1;
    static char name[] = "CypherTileConfigurationProfileTests";
    static char *argv[]{ name, nullptr };
    static QApplication application( argc, argv );
}

struct profile_settings_t
{
    QTemporaryDir directory;
    QString organization{ QCoreApplication::organizationName() };
    QString application{ QCoreApplication::applicationName() };
    QSettings::Format format{ QSettings::defaultFormat() };
    QString previousIniRoot;
    QString configRoot;
    QString cacheRoot;

    profile_settings_t()
    {
        REQUIRE( directory.isValid() );
        const QSettings probe( QSettings::IniFormat, QSettings::UserScope, "CypherProfileProbe", "Path" );
        previousIniRoot = QFileInfo( QFileInfo( probe.fileName() ).absolutePath() ).absolutePath();
        QSettings::setDefaultFormat( QSettings::IniFormat );
        QSettings::setPath( QSettings::IniFormat, QSettings::UserScope, directory.path() );
        QCoreApplication::setOrganizationName( "CypherTests" );
        QCoreApplication::setApplicationName( "TileProfiles-" + QFileInfo( directory.path() ).fileName() );
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

    ~profile_settings_t()
    {
        // Only this test's unique application profile is removed.
        if ( !configRoot.isEmpty() )
            QDir( configRoot ).removeRecursively();
        if ( !cacheRoot.isEmpty() )
            QDir( cacheRoot ).removeRecursively();
        QCoreApplication::setOrganizationName( organization );
        QCoreApplication::setApplicationName( application );
        QSettings::setPath( QSettings::IniFormat, QSettings::UserScope, previousIniRoot );
        QSettings::setDefaultFormat( format );
    }
};

template <typename Widget> Widget *ProfileWidget( CypherTileEditorMainWindow &window, const char *name )
{
    auto *widget = window.findChild<QWidget *>( QString::fromLatin1( name ) );
    REQUIRE( widget != nullptr );
    // Editor subclasses intentionally omit Q_OBJECT; find by production object name.
    return static_cast<Widget *>( widget );
}

QAction *ProfileAction( CypherTileEditorMainWindow &window, const char *name )
{
    auto *action = window.findChild<QAction *>( QString::fromLatin1( name ) );
    REQUIRE( action != nullptr );
    return action;
}

void TriggerProfileAction( CypherTileEditorMainWindow &window, const char *name )
{
    auto *action = ProfileAction( window, name );
    REQUIRE( action->isEnabled() );
    action->trigger();
}

QByteArray ReadProfileFile( const QString &path )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    return file.readAll();
}

tile_editor_preferences_t ReadProfile( const QString &path )
{
    REQUIRE( QFileInfo::exists( path ) );
    tile_editor_preferences_t preferences;
    QString error;
    REQUIRE( TileEditorConfig_Load( path, preferences, error ) );
    return preferences;
}

QString ProfileMapFixture( const QTemporaryDir &directory )
{
    REQUIRE( directory.isValid() );
    CypherTileDocumentBridge document;
    QString error;
    REQUIRE( document.newDocument( { 8u, 6u, 2.0f, 3.0f }, &error ) );
    REQUIRE( document.beginEdit( "Profile fixture", &error ) );
    REQUIRE( document.paintCell( { 1, 1 }, { 0, 1u, 3u }, &error ) );
    REQUIRE( document.paintCell( { 2, 1 }, { 0, 1u, 4u }, &error ) );
    REQUIRE( document.placePlayerSpawn( { 1, 1 }, 30.0f, &error ) );
    REQUIRE( document.commitEdit( &error ) );
    const QString path = directory.filePath( "profile-fixture.cymap" );
    REQUIRE( document.saveToFile( path, &error ) );
    return path;
}

void CheckProfilePose( const tile_camera_t &actual, const tile_camera_t &expected )
{
    CHECK( actual.position.x == expected.position.x );
    CHECK( actual.position.y == expected.position.y );
    CHECK( actual.position.z == expected.position.z );
    CHECK( actual.yawRadians == expected.yawRadians );
    CHECK( actual.pitchRadians == expected.pitchRadians );
}

} // namespace

TEST_CASE( "Importing an editor profile changes live preferences without replacing the open map or workspace",
           "[TileEditor][Configuration][Profiles][Integration]" )
{
    EnsureProfileApplication();
    profile_settings_t settings;
    QTemporaryDir files;
    const QString mapPath = ProfileMapFixture( files );
    const QByteArray originalMap = ReadProfileFile( mapPath );
    CypherTileDocumentBridge original;
    QString error;
    REQUIRE( original.loadFromFile( mapPath, &error ) );
    CypherTileEditorMainWindow window;
    REQUIRE( window.openFilePath( mapPath, false ) );
    // The production window stays hidden: this suite checks behavior, not OpenGL.
    auto *canvas = ProfileWidget<CypherTileCanvas>( window, "CypherTileCanvas" );
    auto *viewport = ProfileWidget<CypherTileRenderViewport>( window, "CypherTileRenderViewport" );
    auto *workspace = ProfileWidget<CypherTileViewWorkspace>( window, "TileEditorFourViews" );
    canvas->selectCell( { 2, 1 }, true );
    TriggerProfileAction( window, "edit.raise" );
    REQUIRE( window.windowTitle().startsWith( '*' ) );
    REQUIRE( viewport->goToPlayerSpawn() );
    REQUIRE( workspace->swapViews( tile_editor_view_t::PERSPECTIVE, tile_editor_view_t::FRONT ) );
    canvas->setZoomFactor( 71 );
    const auto camera = viewport->camera();
    const auto origin = canvas->viewOrigin();
    const auto title = window.windowTitle();
    const auto active = workspace->activeView();
    std::array<tile_editor_view_t, 4> order{};
    for ( int index = 0; index < 4; ++index )
        order[index] = workspace->viewAtPosition( index );

    auto imported = ReadProfile( TileEditorConfig_DefaultPath() );
    imported.uiFontPointSize = 13;
    imported.uiIconSize = 28;
    imported.accentColor = QColor( "#71c1d5" );
    imported.showActiveViewBorder = true;
    imported.cameraFlyMode = false;
    imported.cameraMoveSpeed = 27.5;
    imported.cameraLookSensitivity = 0.35;
    imported.cameraPanSensitivity = 1.7;
    imported.cameraZoomSensitivity = 2.2;
    imported.cameraFastMultiplier = 6.0;
    imported.cameraSlowMultiplier = 0.12;
    imported.cameraFieldOfView = 78.0;
    imported.cameraInvertY = true;
    imported.cameraInvertWheel = true;
    imported.showCameraHints = false;
    imported.shortcuts["view.console"] = QKeySequence( "Ctrl+Alt+Shift+9" );
    const QString source = files.filePath( "custom-editor.ini" );
    REQUIRE( TileEditorConfig_Save( source, imported, error ) );
    const QByteArray sourceBytes = ReadProfileFile( source );
    error = "stale error";
    REQUIRE( window.importConfigurationProfile( source, &error ) );
    CHECK( error.isEmpty() );

    CHECK( QApplication::font().pointSize() == 13 );
    CHECK( ProfileWidget<QToolBar>( window, "TileEditorMainToolbar" )->iconSize() == QSize( 28, 28 ) );
    CHECK( qApp->property( "TileEditorAccentColor" ).value<QColor>() == imported.accentColor );
    CHECK( ProfileAction( window, "view.console" )->shortcut() == imported.shortcuts["view.console"] );
    CHECK( ProfileAction( window, "view.activeBorder" )->isChecked() );
    CHECK_FALSE( ProfileAction( window, "view.cameraHints" )->isChecked() );
    CHECK( ProfileAction( window, "camera.orbit" )->isChecked() );
    CHECK( viewport->camera().mode == tile_camera_mode_t::ORBIT );
    CHECK( viewport->camera().settings.moveSpeed == Catch::Approx( 27.5 ) );
    CHECK( viewport->camera().settings.panSensitivity == Catch::Approx( 1.7 ) );
    CHECK( viewport->camera().settings.zoomSensitivity == Catch::Approx( 2.2 ) );
    CHECK( viewport->camera().settings.fastMultiplier == Catch::Approx( 6.0 ) );
    CHECK( viewport->camera().settings.slowMultiplier == Catch::Approx( 0.12 ) );
    CHECK( viewport->camera().settings.verticalFovDegrees == Catch::Approx( 78.0 ) );
    CHECK( viewport->camera().settings.invertMouseY );
    CHECK( viewport->camera().settings.invertWheel );
    CheckProfilePose( viewport->camera(), camera );
    CHECK( window.windowTitle() == title );
    CHECK( ReadProfileFile( mapPath ) == originalMap );
    CHECK( ReadProfileFile( source ) == sourceBytes );
    CHECK( canvas->selectedCell().x == 2 );
    CHECK( canvas->selectedCell().y == 1 );
    CHECK( canvas->zoomFactor() == 71 );
    CHECK( canvas->viewOrigin() == origin );
    CHECK( workspace->activeView() == active );
    for ( int index = 0; index < 4; ++index )
        CHECK( workspace->viewAtPosition( index ) == order[index] );

    const QString exported = files.filePath( "exported-editor.ini" );
    REQUIRE( window.exportConfigurationProfile( exported, &error ) );
    CHECK( ReadProfileFile( exported ) == ReadProfileFile( TileEditorConfig_DefaultPath() ) );
    const auto roundTrip = ReadProfile( exported );
    CHECK( roundTrip.uiFontPointSize == 13 );
    CHECK( roundTrip.accentColor == imported.accentColor );
    CHECK( roundTrip.cameraLookSensitivity == Catch::Approx( 0.35 ) );
    CHECK( roundTrip.cameraPanSensitivity == Catch::Approx( 1.7 ) );
    CHECK( roundTrip.cameraZoomSensitivity == Catch::Approx( 2.2 ) );
    CHECK( roundTrip.cameraFastMultiplier == Catch::Approx( 6.0 ) );
    CHECK( roundTrip.cameraSlowMultiplier == Catch::Approx( 0.12 ) );
    CHECK( roundTrip.shortcuts["view.console"] == imported.shortcuts["view.console"] );

    // Configuration changes never enter the document's undo history or rebind its path.
    TriggerProfileAction( window, "edit.undo" );
    CHECK_FALSE( window.windowTitle().startsWith( '*' ) );
    TriggerProfileAction( window, "edit.redo" );
    TriggerProfileAction( window, "file.save" );
    CypherTileDocumentBridge saved;
    REQUIRE( saved.loadFromFile( mapPath, &error ) );
    CHECK( saved.filePath() == original.filePath() );
    CHECK( UniqueId_Equals( saved.document()->mapId, original.document()->mapId ) );
    CHECK( saved.document()->nWidth == 8u );
    CHECK( saved.document()->nHeight == 6u );
    CHECK( ReadProfileFile( mapPath ) != originalMap );
    CHECK_FALSE( window.windowTitle().startsWith( '*' ) );
}

TEST_CASE( "Missing and malformed profiles preserve active configuration bytes and UI state",
           "[TileEditor][Configuration][Profiles][Integration]" )
{
    EnsureProfileApplication();
    profile_settings_t settings;
    QTemporaryDir files;
    REQUIRE( files.isValid() );
    CypherTileEditorMainWindow window;
    auto *viewport = ProfileWidget<CypherTileRenderViewport>( window, "CypherTileRenderViewport" );
    const auto camera = viewport->camera();
    const auto font = QApplication::font();
    const auto iconSize = ProfileWidget<QToolBar>( window, "TileEditorMainToolbar" )->iconSize();
    const auto shortcut = ProfileAction( window, "view.console" )->shortcut();
    const auto activeBytes = ReadProfileFile( TileEditorConfig_DefaultPath() );
    const auto title = window.windowTitle();
    const QString malformed = files.filePath( "broken.ini" );
    {
        QFile file( malformed );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        const QByteArray invalid = "[Editor]\nschemaVersion=1\n[Appearance]\nuiFontPointSize=17\n"
                                   "accentColor=definitely-not-a-color\n[Camera]\ncameraMoveSpeed=41\n";
        REQUIRE( file.write( invalid ) == invalid.size() );
    }
    const auto malformedBytes = ReadProfileFile( malformed );
    for ( const auto &path : { malformed, files.filePath( "missing.ini" ) } )
    {
        QString error;
        CHECK_FALSE( window.importConfigurationProfile( path, &error ) );
        CHECK_FALSE( error.isEmpty() );
        CHECK( ReadProfileFile( TileEditorConfig_DefaultPath() ) == activeBytes );
        CHECK( QApplication::font() == font );
        CHECK( ProfileWidget<QToolBar>( window, "TileEditorMainToolbar" )->iconSize() == iconSize );
        CHECK( ProfileAction( window, "view.console" )->shortcut() == shortcut );
        CHECK( window.windowTitle() == title );
        CheckProfilePose( viewport->camera(), camera );
        CHECK( viewport->camera().settings == camera.settings );
        CHECK( viewport->camera().mode == camera.mode );
    }
    CHECK( ReadProfileFile( malformed ) == malformedBytes );
}

TEST_CASE( "Camera menu commands export immediately and flush pending preferences on shutdown",
           "[TileEditor][Configuration][Profiles][Camera][Integration]" )
{
    EnsureProfileApplication();
    profile_settings_t settings;
    QTemporaryDir files;
    const QString mapPath = ProfileMapFixture( files );
    QString error;
    {
        CypherTileEditorMainWindow window;
        REQUIRE( window.openFilePath( mapPath, false ) );
        auto *viewport = ProfileWidget<CypherTileRenderViewport>( window, "CypherTileRenderViewport" );
        REQUIRE( viewport->goToPlayerSpawn() );
        const auto camera = viewport->camera();
        auto *menu = window.findChild<QMenu *>( "TileCameraMenu" );
        REQUIRE( menu != nullptr );
        CHECK( ProfileWidget<QToolButton>( window, "TileViewCameraOptions" )->menu() == menu );
        CHECK( menu->actions().contains( ProfileAction( window, "camera.frameMap" ) ) );
        CHECK( menu->actions().contains( ProfileAction( window, "camera.settings" ) ) );
        TriggerProfileAction( window, "camera.orbit" );
        CHECK( viewport->camera().mode == tile_camera_mode_t::ORBIT );
        CHECK( ProfileAction( window, "camera.orbit" )->isChecked() );
        CHECK_FALSE( ProfileAction( window, "camera.fly" )->isChecked() );
        TriggerProfileAction( window, "camera.fly" );
        CHECK( viewport->camera().mode == tile_camera_mode_t::FLY );
        CHECK( ProfileAction( window, "camera.fly" )->isChecked() );
        CHECK_FALSE( ProfileAction( window, "camera.orbit" )->isChecked() );
        auto *speed = ProfileWidget<QDoubleSpinBox>( window, "TileCameraSpeed" );
        speed->setValue( 12.5 );
        TriggerProfileAction( window, "camera.faster" );
        CHECK( speed->value() == Catch::Approx( 18.75 ) );
        CHECK( viewport->camera().settings.moveSpeed == Catch::Approx( 18.75 ) );
        TriggerProfileAction( window, "camera.slower" );
        CHECK( speed->value() == Catch::Approx( 12.5 ) );
        CheckProfilePose( viewport->camera(), camera );

        // Export reads the live preferences even before the camera debounce expires.
        const QString exportPath = files.filePath( "camera-now.ini" );
        REQUIRE( window.exportConfigurationProfile( exportPath, &error ) );
        CHECK( ReadProfile( exportPath ).cameraMoveSpeed == Catch::Approx( 12.5 ) );
        CHECK( ReadProfile( exportPath ).cameraFlyMode );
        ProfileAction( window, "view.cameraHints" )->setChecked( false );
        ProfileAction( window, "view.activeBorder" )->setChecked( true );
        REQUIRE( window.exportConfigurationProfile( exportPath, &error ) );
        CHECK_FALSE( ReadProfile( exportPath ).showCameraHints );
        CHECK( ReadProfile( exportPath ).showActiveViewBorder );
        CHECK_FALSE( window.windowTitle().startsWith( '*' ) );

        // No event-loop wait: destruction must persist the final debounced value.
        speed->setValue( 23.5 );
        TriggerProfileAction( window, "camera.orbit" );
    }
    const auto persisted = ReadProfile( TileEditorConfig_DefaultPath() );
    CHECK( persisted.cameraMoveSpeed == Catch::Approx( 23.5 ) );
    CHECK_FALSE( persisted.cameraFlyMode );
    CHECK_FALSE( persisted.showCameraHints );
    CHECK( persisted.showActiveViewBorder );
}

TEST_CASE( "Camera shortcuts are scoped to 3D and can be freely rebound",
           "[TileEditor][Configuration][Profiles][Camera][Shortcuts][Integration]" )
{
    EnsureProfileApplication();
    profile_settings_t settings;
    QTemporaryDir files;
    const QString mapPath = ProfileMapFixture( files );
    CypherTileEditorMainWindow window;
    REQUIRE( window.openFilePath( mapPath, false ) );
    auto *viewport = ProfileWidget<CypherTileRenderViewport>( window, "CypherTileRenderViewport" );
    auto *canvas = ProfileWidget<CypherTileCanvas>( window, "CypherTileCanvas" );
    auto *toggle = ProfileAction( window, "camera.toggleMode" );
    auto *frameMap = ProfileAction( window, "camera.frameMap" );
    REQUIRE( toggle->shortcut().isEmpty() );
    CHECK( toggle->shortcutContext() == Qt::WidgetWithChildrenShortcut );
    CHECK( viewport->actions().contains( toggle ) );
    CHECK_FALSE( canvas->actions().contains( toggle ) );
    CHECK_FALSE( ProfileWidget<QWidget>( window, "CypherTileFrontView" )->actions().contains( toggle ) );
    CHECK_FALSE( ProfileWidget<QWidget>( window, "CypherTileSideView" )->actions().contains( toggle ) );

    const auto originalMode = viewport->camera().mode;
    toggle->trigger();
    REQUIRE( viewport->camera().mode != originalMode );
    auto profile = ReadProfile( TileEditorConfig_DefaultPath() );
    profile.cameraFlyMode = false;
    profile.shortcuts["camera.toggleMode"] = QKeySequence( "Ctrl+Alt+M" );
    profile.shortcuts["camera.frameMap"] = QKeySequence( "Tab" );
    const QString path = files.filePath( "rebound-camera.ini" );
    QString error;
    REQUIRE( TileEditorConfig_Save( path, profile, error ) );
    REQUIRE( window.importConfigurationProfile( path, &error ) );
    REQUIRE( toggle->shortcut() == QKeySequence( "Ctrl+Alt+M" ) );
    REQUIRE( frameMap->shortcut() == QKeySequence( "Tab" ) );

    // The viewport must leave Tab to Qt's configured shortcut dispatcher. The
    // native workflow verifies key dispatch with a real focused OpenGL view.
    QKeyEvent shortcutOverride( QEvent::ShortcutOverride, Qt::Key_Tab, Qt::NoModifier );
    shortcutOverride.ignore();
    QApplication::sendEvent( viewport, &shortcutOverride );
    CHECK_FALSE( shortcutOverride.isAccepted() );
    const auto reboundMode = viewport->camera().mode;
    frameMap->trigger();
    CHECK( viewport->camera().mode == reboundMode );
    toggle->trigger();
    CHECK( viewport->camera().mode != reboundMode );

    profile.shortcuts["camera.toggleMode"] = {};
    profile.shortcuts["camera.frameMap"] = {};
    REQUIRE( TileEditorConfig_Save( path, profile, error ) );
    REQUIRE( window.importConfigurationProfile( path, &error ) );
    CHECK( toggle->shortcut().isEmpty() );
    CHECK( frameMap->shortcut().isEmpty() );
    QKeyEvent unboundOverride( QEvent::ShortcutOverride, Qt::Key_Tab, Qt::NoModifier );
    unboundOverride.ignore();
    QApplication::sendEvent( viewport, &unboundOverride );
    CHECK_FALSE( unboundOverride.isAccepted() );
}
