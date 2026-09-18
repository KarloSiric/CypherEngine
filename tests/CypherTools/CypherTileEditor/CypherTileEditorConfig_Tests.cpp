//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Configuration transactions, readable serialization, and appearance UI.
//////////////////////////////////////////////////////////////////////////
#include "CypherTileEditorConfig.h"
#include "CypherTileEditorTheme.h"
#include "CypherTileEditorUserThemes.h"
#include "CypherTileViewportColors.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTemporaryDir>

using namespace cypher::tools::tile_editor;

namespace
{
QApplication &ConfigApplication()
{
    if ( auto *app = qobject_cast<QApplication *>( QApplication::instance() ) ) return *app;
    qputenv( "QT_QPA_PLATFORM", "offscreen" );
    static int argc = 1;
    static char name[] = "TileEditorConfigTests";
    static char *argv[]{ name, nullptr };
    static QApplication application( argc, argv );
    return application;
}

void WriteConfig( const QString &path, const QByteArray &bytes )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    REQUIRE( file.write( bytes ) == bytes.size() );
}
} // namespace

TEST_CASE( "Readable editor config round trips colors dimensions navigation and portable hotkeys",
    "[TileEditor][Config]" )
{
    ConfigApplication();
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto path = directory.filePath( "nested/editor.ini" );
    tile_editor_preferences_t original;
    original.canvasColor = QColor( 44, 66, 88, 220 );
    original.panelColor = QColor( "#242b34" );
    original.axisXColor = QColor( "#ff6677" );
    original.wireColor = QColor( "#99bbdd" );
    original.uiFontPointSize = 13;
    original.uiIconSize = 28;
    original.wireLineWidth = 1.8;
    original.depthCueWireframe = false;
    original.showViewMetrics = false;
    original.emptyViewCellPixels = 48;
    original.viewSplitterWidth = 10;
    original.cameraMoveSpeed = 27.5;
    original.shortcuts.insert( "app.settings", QKeySequence( "Ctrl+," ) );
    original.shortcuts.insert( "tool.fill", QKeySequence() );
    QString error;
    REQUIRE( TileEditorConfig_Save( path, original, error ) );
    CHECK( error.isEmpty() );
    QFile file( path );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    const auto bytes = file.readAll();
    CHECK( bytes.contains( "[Appearance]" ) );
    CHECK( bytes.contains( "panelColor=\"#ff242b34\"" ) );
    CHECK( bytes.contains( "uiFontPointSize=13" ) );
    CHECK( bytes.contains( "[Workspace]" ) );
    CHECK( bytes.contains( "viewSplitterWidth=10" ) );
    CHECK_FALSE( bytes.contains( "@Variant" ) );
    CHECK_FALSE( bytes.contains( "geometry=" ) );
    CHECK_FALSE( bytes.contains( "dockState=" ) );
    tile_editor_preferences_t loaded;
    REQUIRE( TileEditorConfig_Load( path, loaded, error ) );
    CHECK( loaded.canvasColor == original.canvasColor );
    CHECK( loaded.panelColor == original.panelColor );
    CHECK( loaded.axisXColor == original.axisXColor );
    CHECK( loaded.wireColor == original.wireColor );
    CHECK( loaded.uiFontPointSize == 13 );
    CHECK( loaded.uiIconSize == 28 );
    CHECK( loaded.wireLineWidth == Catch::Approx( 1.8 ) );
    CHECK( loaded.cameraMoveSpeed == Catch::Approx( 27.5 ) );
    CHECK_FALSE( loaded.depthCueWireframe );
    CHECK_FALSE( loaded.showViewMetrics );
    CHECK( loaded.emptyViewCellPixels == 48 );
    CHECK( loaded.viewSplitterWidth == 10 );
    CHECK( loaded.shortcuts.value( "app.settings" ) == original.shortcuts.value( "app.settings" ) );
    CHECK( loaded.shortcuts.value( "tool.fill" ).isEmpty() );
}

TEST_CASE( "Partial editor config preserves native preferences and clamps supported numeric ranges",
    "[TileEditor][Config]" )
{
    ConfigApplication();
    QTemporaryDir directory;
    const auto path = directory.filePath( "editor.ini" );
    tile_editor_preferences_t preferences;
    preferences.canvasColor = QColor( "#391842" );
    preferences.cameraMoveSpeed = 19.0;
    preferences.shortcuts.insert( "tool.fill", QKeySequence( "Alt+G" ) );
    QString error;
    REQUIRE( TileEditorConfig_Load( path, preferences, error ) );
    CHECK( preferences.canvasColor == QColor( "#391842" ) );
    WriteConfig( path,
        "[Editor]\nschemaVersion=1\n[Appearance]\nuiFontPointSize=90\nuiIconSize=-9\n"
        "[Viewport]\nwireLineWidth=12.5\nemptyViewCellPixels=0\n"
        "[Grid]\ngridSpacingCells=7\ngridMinimumPixels=200\n[Camera]\ncameraFieldOfView=180\n"
        "[Workspace]\nviewSplitterWidth=200\n" );
    REQUIRE( TileEditorConfig_Load( path, preferences, error ) );
    CHECK( preferences.canvasColor == QColor( "#391842" ) );
    CHECK( preferences.cameraMoveSpeed == Catch::Approx( 19.0 ) );
    CHECK( preferences.shortcuts.value( "tool.fill" ) == QKeySequence( "Alt+G" ) );
    CHECK( preferences.uiFontPointSize == 18 );
    CHECK( preferences.uiIconSize == 16 );
    CHECK( preferences.wireLineWidth == Catch::Approx( 3.0 ) );
    CHECK( preferences.emptyViewCellPixels == 12 );
    CHECK( preferences.gridSpacingCells == 4 );
    CHECK( preferences.gridMinimumPixels == 64 );
    CHECK( preferences.cameraFieldOfView == Catch::Approx( 100.0 ) );
    CHECK( preferences.viewSplitterWidth == 16 );
}

TEST_CASE( "Invalid editor config leaves every previously applied setting intact",
    "[TileEditor][Config]" )
{
    ConfigApplication();
    QTemporaryDir directory;
    const auto path = directory.filePath( "editor.ini" );
    const QByteArray invalidFields[]{
        "[Viewport]\ncanvasColor=not-a-color\n",
        "[Grid]\ngridMinimumPixels=garbage\n",
        "[Camera]\ncameraMoveSpeed=nan\n",
        "[Viewport]\nshowViewMetrics=occasionally\n",
        "[Shortcuts]\ntool.fill=Ctrl+NotARealKey\n",
        "[Shortcuts]\ntool.fill=F\n"
    };
    for ( const auto &invalid : invalidFields ) {
        INFO( invalid.constData() );
        WriteConfig( path, "[Editor]\nschemaVersion=1\n[Appearance]\nuiFontPointSize=14\n" + invalid );
        tile_editor_preferences_t preferences;
        preferences.uiFontPointSize = 12;
        preferences.canvasColor = QColor( "#123456" );
        QString error;
        CHECK_FALSE( TileEditorConfig_Load( path, preferences, error ) );
        CHECK_FALSE( error.isEmpty() );
        CHECK( preferences.uiFontPointSize == 12 );
        CHECK( preferences.canvasColor == QColor( "#123456" ) );
    }
    for ( const auto &version : { QByteArray( "" ), QByteArray( "schemaVersion=2\n" ) } ) {
        WriteConfig( path, "[Editor]\n" + version );
        tile_editor_preferences_t preferences;
        QString error;
        CHECK_FALSE( TileEditorConfig_Load( path, preferences, error ) );
        CHECK( error.contains( "schemaVersion=1" ) );
    }
}

TEST_CASE( "Editor config save failures cannot damage a previously written file",
    "[TileEditor][Config]" )
{
    ConfigApplication();
    QTemporaryDir directory;
    const auto path = directory.filePath( "editor.ini" );
    QString error;
    tile_editor_preferences_t preferences;
    REQUIRE( TileEditorConfig_Save( path, preferences, error ) );
    QFile file( path );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    const auto before = file.readAll();
    file.close();
    // A file cannot serve as a parent directory. Failure must preserve it exactly.
    CHECK_FALSE( TileEditorConfig_Save( path + "/child.ini", preferences, error ) );
    CHECK_FALSE( error.isEmpty() );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    CHECK( file.readAll() == before );
    file.close();
    CHECK_FALSE( TileEditorConfig_Load( directory.path(), preferences, error ) );
    CHECK_FALSE( TileEditorConfig_Save( QString(), preferences, error ) );
}

TEST_CASE( "Legacy linked-camera profiles migrate once to independent view navigation",
    "[TileEditor][Config][Migration][Navigation]" )
{
    ConfigApplication();
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const QString nativePath = directory.filePath( QStringLiteral( "native.ini" ) );
    const QString configurationPath = directory.filePath( QStringLiteral( "editor.ini" ) );
    QSettings native( nativePath, QSettings::IniFormat );
    tile_editor_preferences_t preferences;
    preferences.linkOrthographicCameras = true;
    TileEditorPreferences_Save( native, preferences );
    QString error;
    REQUIRE( TileEditorConfig_Save( configurationPath, preferences, error ) );

    bool migrated = false;
    REQUIRE( TileEditorConfig_MigrateIndependentOrthographicCameras(
        native, configurationPath, preferences, migrated, error ) );
    CHECK( migrated );
    CHECK_FALSE( preferences.linkOrthographicCameras );
    CHECK_FALSE( TileEditorPreferences_Load( native ).linkOrthographicCameras );
    CHECK( native.value(
        QStringLiteral( "TileEditor/independentOrthographicCamerasV1" ) ).toBool() );
    tile_editor_preferences_t migratedConfiguration;
    REQUIRE( TileEditorConfig_Load(
        configurationPath, migratedConfiguration, error ) );
    CHECK_FALSE( migratedConfiguration.linkOrthographicCameras );

    // Once the migration marker exists, a deliberate user opt-in survives.
    preferences.linkOrthographicCameras = true;
    TileEditorPreferences_Save( native, preferences );
    REQUIRE( TileEditorConfig_Save( configurationPath, preferences, error ) );
    migrated = true;
    REQUIRE( TileEditorConfig_MigrateIndependentOrthographicCameras(
        native, configurationPath, preferences, migrated, error ) );
    CHECK_FALSE( migrated );
    CHECK( preferences.linkOrthographicCameras );
    CHECK( TileEditorPreferences_Load( native ).linkOrthographicCameras );
    tile_editor_preferences_t optedInConfiguration;
    REQUIRE( TileEditorConfig_Load(
        configurationPath, optedInConfiguration, error ) );
    CHECK( optedInConfiguration.linkOrthographicCameras );
}

TEST_CASE( "Independent-view migration preserves an invalid editor configuration",
    "[TileEditor][Config][Migration][Navigation]" )
{
    ConfigApplication();
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const QString nativePath = directory.filePath( QStringLiteral( "native.ini" ) );
    const QString configurationPath = directory.filePath( QStringLiteral( "editor.ini" ) );
    const QByteArray malformed(
        "[Editor]\nschemaVersion=1\n[Workspace]\n"
        "linkOrthographicCameras=occasionally\n" );
    WriteConfig( configurationPath, malformed );

    QSettings native( nativePath, QSettings::IniFormat );
    tile_editor_preferences_t preferences;
    preferences.linkOrthographicCameras = true;
    TileEditorPreferences_Save( native, preferences );

    bool migrated = true;
    QString error;
    CHECK_FALSE( TileEditorConfig_MigrateIndependentOrthographicCameras(
        native, configurationPath, preferences, migrated, error ) );
    CHECK_FALSE( migrated );
    CHECK_FALSE( error.isEmpty() );
    CHECK( preferences.linkOrthographicCameras );
    CHECK( TileEditorPreferences_Load( native ).linkOrthographicCameras );
    CHECK_FALSE( native.contains(
        QStringLiteral( "TileEditor/independentOrthographicCamerasV1" ) ) );

    QFile preserved( configurationPath );
    REQUIRE( preserved.open( QIODevice::ReadOnly ) );
    CHECK( preserved.readAll() == malformed );
}

TEST_CASE( "Appearance controls and presets keep navigation preferences independent",
    "[TileEditor][Config][Settings]" )
{
    ConfigApplication();
    tile_editor_preferences_t original;
    original.canvasColor = QColor( "#713353" );
    original.cameraMoveSpeed = 25.0;
    original.uiFontPointSize = 12;
    original.shortcuts.insert( "tool.fill", QKeySequence( "Alt+G" ) );
    CypherTileEditorSettingsDialog dialog( original );
    auto *preset = dialog.findChild<QComboBox *>( "TileSettingsColorPreset" );
    auto *font = dialog.findChild<QSpinBox *>( "TileSettingsUiFontPointSize" );
    auto *icons = dialog.findChild<QSpinBox *>( "TileSettingsUiIconSize" );
    auto *width = dialog.findChild<QDoubleSpinBox *>( "TileSettingsWireLineWidth" );
    auto *metrics = dialog.findChild<QCheckBox *>( "TileSettingsShowViewMetrics" );
    auto *splitter = dialog.findChild<QSpinBox *>( "TileSettingsViewSplitterWidth" );
    REQUIRE( preset ); REQUIRE( font ); REQUIRE( icons ); REQUIRE( width ); REQUIRE( metrics );
    REQUIRE( splitter );
    CHECK( tile_editor_preferences_t{}.viewSplitterWidth == 6 );
    CHECK( splitter->minimum() == 3 );
    CHECK( splitter->maximum() == 16 );
    CHECK( splitter->value() == 6 );
    CHECK( dialog.preferences().canvasColor == original.canvasColor );
    preset->setCurrentIndex( preset->findData( "slate" ) );
    font->setValue( 14 );
    icons->setValue( 30 );
    width->setValue( 2.1 );
    metrics->setChecked( false );
    splitter->setValue( 9 );
    const auto edited = dialog.preferences();
    CHECK( edited.canvasColor == QColor( 51, 75, 96 ) );
    CHECK( edited.cameraMoveSpeed == Catch::Approx( 25.0 ) );
    CHECK( edited.shortcuts.value( "tool.fill" ) == QKeySequence( "Alt+G" ) );
    CHECK( edited.uiFontPointSize == 14 );
    CHECK( edited.uiIconSize == 30 );
    CHECK( edited.wireLineWidth == Catch::Approx( 2.1 ) );
    CHECK_FALSE( edited.showViewMetrics );
    CHECK( edited.viewSplitterWidth == 9 );
    const auto *path = dialog.findChild<QLabel *>( "TileSettingsConfigPath" );
    REQUIRE( path );
    CHECK( path->text().contains( TileEditorConfig_DefaultPath() ) );
    auto *buttons = dialog.findChild<QDialogButtonBox *>();
    REQUIRE( buttons );
    buttons->button( QDialogButtonBox::RestoreDefaults )->click();
    CHECK( dialog.preferences().uiFontPointSize == tile_editor_preferences_t{}.uiFontPointSize );
    CHECK_FALSE( dialog.preferences().showViewMetrics );
    CHECK( dialog.preferences().canvasColor == tile_editor_preferences_t{}.canvasColor );
    CHECK( dialog.preferences().viewSplitterWidth == 6 );
}

TEST_CASE( "Portable user themes round trip only semantic colors",
    "[TileEditor][Config][Theme][UserTheme]" )
{
    ConfigApplication();
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const QString path = directory.filePath( "nested/night-shift.cytheme" );
    tile_editor_preferences_t source;
    source.uiBackgroundColor = QColor( "#17202a" );
    source.panelColor = QColor( "#111820" );
    source.accentColor = QColor( "#e39a36" );
    source.canvasColor = QColor( "#18324a" );
    source.floorColor = QColor( "#7f9aaa" );
    source.axisZColor = QColor( "#65a9f1" );
    source.cameraMoveSpeed = 91.0;
    source.uiFontPointSize = 17;
    source.shortcuts.insert( "tool.fill", QKeySequence( "Alt+G" ) );
    QString error;
    REQUIRE( TileEditorUserTheme_Save( path, "Night Shift", source, error ) );
    CHECK( error.isEmpty() );

    QFile file( path );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    const QByteArray bytes = file.readAll();
    CHECK( bytes.contains( "[Theme]" ) );
    CHECK( bytes.contains( "[Colors]" ) );
    CHECK( bytes.contains( "name=\"Night Shift\"" ) );
    CHECK( bytes.contains( "accentColor=\"#e39a36\"" ) );
    CHECK_FALSE( bytes.contains( "Camera" ) );
    CHECK_FALSE( bytes.contains( "Shortcuts" ) );
    CHECK_FALSE( bytes.contains( "uiFontPointSize" ) );

    tile_editor_user_theme_t loaded;
    REQUIRE( TileEditorUserTheme_Load( path, loaded, error ) );
    CHECK( loaded.name == "Night Shift" );
    CHECK( loaded.path == QFileInfo( path ).absoluteFilePath() );
    CHECK( loaded.colors.uiBackgroundColor == source.uiBackgroundColor );
    CHECK( loaded.colors.panelColor == source.panelColor );
    CHECK( loaded.colors.accentColor == source.accentColor );
    CHECK( loaded.colors.canvasColor == source.canvasColor );
    CHECK( loaded.colors.floorColor == source.floorColor );
    CHECK( loaded.colors.axisZColor == source.axisZColor );

    tile_editor_preferences_t destination;
    destination.cameraMoveSpeed = 37.5;
    destination.uiFontPointSize = 13;
    destination.showGrid = false;
    destination.shortcuts.insert( "tool.fill", QKeySequence( "Ctrl+Shift+F" ) );
    TileEditorTheme_CopyColors( destination, loaded.colors );
    CHECK( destination.accentColor == source.accentColor );
    CHECK( destination.canvasColor == source.canvasColor );
    CHECK( destination.cameraMoveSpeed == Catch::Approx( 37.5 ) );
    CHECK( destination.uiFontPointSize == 13 );
    CHECK_FALSE( destination.showGrid );
    CHECK( destination.shortcuts.value( "tool.fill" ) == QKeySequence( "Ctrl+Shift+F" ) );
}

TEST_CASE( "User theme discovery is deterministic and malformed themes are transactional",
    "[TileEditor][Config][Theme][UserTheme]" )
{
    ConfigApplication();
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    QString error;
    tile_editor_preferences_t colors;
    colors.accentColor = QColor( "#f09b36" );
    REQUIRE( TileEditorUserTheme_Save(
        directory.filePath( "z.cytheme" ), "Zulu", colors, error ) );
    colors.accentColor = QColor( "#69b9dd" );
    REQUIRE( TileEditorUserTheme_Save(
        directory.filePath( "a.cytheme" ), "Alpha", colors, error ) );
    WriteConfig( directory.filePath( "broken.cytheme" ),
        "[Theme]\nschemaVersion=1\nname=Broken\n[Colors]\naccentColor=not-a-color\n" );

    QStringList errors;
    const auto themes = TileEditorUserThemes_Discover( directory.path(), &errors );
    REQUIRE( themes.size() == 2 );
    CHECK( themes[0].name == "Alpha" );
    CHECK( themes[1].name == "Zulu" );
    REQUIRE( errors.size() == 1 );
    CHECK( ( errors.first().contains( "missing required color" ) ||
             errors.first().contains( "invalid" ) ) );

    tile_editor_user_theme_t unchanged;
    unchanged.name = "Keep Me";
    unchanged.path = "sentinel";
    unchanged.colors.canvasColor = QColor( "#123456" );
    CHECK_FALSE( TileEditorUserTheme_Load(
        directory.filePath( "broken.cytheme" ), unchanged, error ) );
    CHECK( unchanged.name == "Keep Me" );
    CHECK( unchanged.path == "sentinel" );
    CHECK( unchanged.colors.canvasColor == QColor( "#123456" ) );
    CHECK( QFileInfo( TileEditorUserThemes_PathForName(
        directory.path(), "  My / Wild : Theme  " ) ).fileName() == "my-wild-theme.cytheme" );
}

TEST_CASE( "User themes accept only opaque hexadecimal RGB values",
    "[TileEditor][Config][Theme][UserTheme]" )
{
    ConfigApplication();
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const QString path = directory.filePath( "strict.cytheme" );
    tile_editor_preferences_t colors;
    QString error;
    REQUIRE( TileEditorUserTheme_Save( path, "Strict", colors, error ) );

    QFile file( path );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    QByteArray bytes = file.readAll();
    file.close();
    REQUIRE( bytes.contains( "#e1a03e" ) );
    bytes.replace( "#e1a03e", "orange" );
    WriteConfig( path, bytes );

    tile_editor_user_theme_t unchanged;
    unchanged.name = "Sentinel";
    unchanged.path = "sentinel";
    unchanged.colors.accentColor = QColor( "#123456" );
    CHECK_FALSE( TileEditorUserTheme_Load( path, unchanged, error ) );
    CHECK( error.contains( "opaque #RRGGBB" ) );
    CHECK( unchanged.name == "Sentinel" );
    CHECK( unchanged.path == "sentinel" );
    CHECK( unchanged.colors.accentColor == QColor( "#123456" ) );
}

TEST_CASE( "Viewport feedback colors remain readable for light and dark themes",
    "[TileEditor][Config][Theme][Viewport]" )
{
    for ( const QColor background : { QColor( "#0b1015" ), QColor( "#edf0f3" ) } ) {
        const bool dark = TileEditorViewportColor_Luminance( background ) < 0.5;
        const auto colors = TileEditorViewportColors_Derive(
            background,
            dark ? QColor( "#dce0e5" ) : QColor( "#252a31" ),
            QColor( "#e1a03e" ) );
        CHECK( TileEditorViewportColor_Contrast( colors.mutedText, background ) >= 3.0 );
        CHECK( TileEditorViewportColor_Contrast( colors.warning, background ) >= 3.0 );
        CHECK( TileEditorViewportColor_Contrast( colors.error, background ) >= 3.0 );
        CHECK( TileEditorViewportColor_Contrast( colors.success, background ) >= 3.0 );
        CHECK( TileEditorViewportColor_Contrast( colors.info, background ) >= 3.0 );
        CHECK( TileEditorViewportColor_Contrast( colors.underlay, background ) >= 4.5 );
    }
}

TEST_CASE( "Theme selection previews colors immediately without changing editor behavior",
    "[TileEditor][Config][Settings][Theme][Preview]" )
{
    ConfigApplication();
    tile_editor_preferences_t original;
    original.cameraMoveSpeed = 27.0;
    original.showGrid = false;
    original.shortcuts.insert( "tool.fill", QKeySequence( "Alt+G" ) );
    CypherTileEditorSettingsDialog dialog( original );
    int previewCount = 0;
    tile_editor_preferences_t previewed;
    dialog.setPreviewCallback( [&]( const tile_editor_preferences_t &value ) {
        ++previewCount;
        previewed = value;
    } );
    auto *selector = dialog.findChild<QComboBox *>( "TileSettingsColorPreset" );
    auto *reset = dialog.findChild<QPushButton *>( "TileSettingsResetColors" );
    auto *cameraSpeed = dialog.findChild<QDoubleSpinBox *>( "TileSettingsCameraSpeed" );
    REQUIRE( selector );
    REQUIRE( reset );
    REQUIRE( cameraSpeed );
    cameraSpeed->setValue( 88.0 ); // Staged outside Appearance; live theme preview must ignore it.
    selector->setCurrentIndex( selector->findData( "blueprint-blue" ) );
    REQUIRE( previewCount == 1 );
    CHECK( previewed.canvasColor == QColor( "#163047" ) );
    CHECK( previewed.cameraMoveSpeed == Catch::Approx( 27.0 ) );
    CHECK_FALSE( previewed.showGrid );
    CHECK( previewed.shortcuts.value( "tool.fill" ) == QKeySequence( "Alt+G" ) );
    reset->click();
    REQUIRE( previewCount == 2 );
    CHECK( previewed.canvasColor == tile_editor_preferences_t{}.canvasColor );
    CHECK( previewed.cameraMoveSpeed == Catch::Approx( 27.0 ) );
}

TEST_CASE( "Unmatched color edits are identified as a custom theme",
    "[TileEditor][Config][Settings][Theme]" )
{
    ConfigApplication();
    tile_editor_preferences_t custom;
    custom.canvasColor = QColor( "#123456" );
    custom.accentColor = QColor( "#abcdef" );
    CypherTileEditorSettingsDialog dialog( custom );
    auto *selector = dialog.findChild<QComboBox *>( "TileSettingsColorPreset" );
    auto *remove = dialog.findChild<QPushButton *>( "TileSettingsDeleteTheme" );
    REQUIRE( selector );
    REQUIRE( remove );
    CHECK( selector->currentText() == "Custom (modified)" );
    CHECK_FALSE( remove->isEnabled() );
    CHECK( dialog.findChild<QPushButton *>( "TileSettingsApplyColorPreset" ) == nullptr );
}

TEST_CASE( "Theme selector never chooses its separator when user themes exist",
    "[TileEditor][Config][Settings][Theme]" )
{
    ConfigApplication();
    const QString directory = TileEditorUserThemes_DefaultDirectory();
    REQUIRE( QDir().mkpath( directory ) );
    const QString path = QDir( directory ).filePath(
        QStringLiteral( "separator-regression-%1.cytheme" )
            .arg( QCoreApplication::applicationPid() ) );
    struct file_cleanup_t {
        QString path;
        ~file_cleanup_t() { QFile::remove( path ); }
    } cleanup{ path };

    QString error;
    tile_editor_preferences_t savedTheme;
    TileEditorPreferences_ApplyColorPreset( savedTheme, QStringLiteral( "midnight" ) );
    REQUIRE( TileEditorUserTheme_Save(
        path, QStringLiteral( "Separator Regression" ), savedTheme, error ) );

    tile_editor_preferences_t unmatched;
    unmatched.canvasColor = QColor( "#123456" );
    unmatched.accentColor = QColor( "#abcdef" );
    CypherTileEditorSettingsDialog dialog( unmatched );
    auto *selector = dialog.findChild<QComboBox *>( "TileSettingsColorPreset" );
    REQUIRE( selector );
    CHECK( selector->currentIndex() >= 0 );
    CHECK( selector->currentText() == QStringLiteral( "Custom (modified)" ) );
    CHECK_FALSE( selector->currentText().isEmpty() );
}

TEST_CASE( "Native preferences retain new appearance fields and theme applies without unresolved tokens",
    "[TileEditor][Config][Theme]" )
{
    auto &app = ConfigApplication();
    QTemporaryDir directory;
    QSettings settings( directory.filePath( "native.ini" ), QSettings::IniFormat );
    tile_editor_preferences_t preferences;
    preferences.uiFontPointSize = 13;
    preferences.uiIconSize = 28;
    preferences.accentColor = QColor( "#99bb44" );
    preferences.perspectiveColor = QColor( "#112233" );
    preferences.wallColor = QColor( "#8899aa" );
    preferences.showViewMetrics = false;
    preferences.viewSplitterWidth = 9;
    TileEditorPreferences_Save( settings, preferences );
    const auto loaded = TileEditorPreferences_Load( settings );
    CHECK( loaded.uiFontPointSize == 13 );
    CHECK( loaded.uiIconSize == 28 );
    CHECK( loaded.accentColor == preferences.accentColor );
    CHECK( loaded.wallColor == preferences.wallColor );
    CHECK_FALSE( loaded.showViewMetrics );
    CHECK( loaded.viewSplitterWidth == 9 );
    CypherTileEditorTheme_Apply( app, loaded );
    CHECK( app.font().pointSize() == 13 );
    CHECK( app.palette().color( QPalette::Window ) == preferences.uiBackgroundColor );
    CHECK( app.styleSheet().contains( "#99bb44" ) );
    CHECK( app.styleSheet().contains( "#112233" ) );
    CHECK_FALSE( app.styleSheet().contains( '@' ) );
    const auto *style = app.style();
    const QString sheet = app.styleSheet();
    CypherTileEditorTheme_Apply( app, loaded );
    CHECK( app.style() == style );
    CHECK( app.styleSheet() == sheet );
    CypherTileEditorTheme_Apply( app );
}
