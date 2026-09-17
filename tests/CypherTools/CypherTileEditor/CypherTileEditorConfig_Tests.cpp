//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Configuration transactions, readable serialization, and appearance UI.
//////////////////////////////////////////////////////////////////////////
#include "CypherTileEditorConfig.h"
#include "CypherTileEditorTheme.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
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
    auto *apply = dialog.findChild<QPushButton *>( "TileSettingsApplyColorPreset" );
    auto *font = dialog.findChild<QSpinBox *>( "TileSettingsUiFontPointSize" );
    auto *icons = dialog.findChild<QSpinBox *>( "TileSettingsUiIconSize" );
    auto *width = dialog.findChild<QDoubleSpinBox *>( "TileSettingsWireLineWidth" );
    auto *metrics = dialog.findChild<QCheckBox *>( "TileSettingsShowViewMetrics" );
    auto *splitter = dialog.findChild<QSpinBox *>( "TileSettingsViewSplitterWidth" );
    REQUIRE( preset ); REQUIRE( apply ); REQUIRE( font ); REQUIRE( icons ); REQUIRE( width ); REQUIRE( metrics );
    REQUIRE( splitter );
    CHECK( tile_editor_preferences_t{}.viewSplitterWidth == 6 );
    CHECK( splitter->minimum() == 3 );
    CHECK( splitter->maximum() == 16 );
    CHECK( splitter->value() == 6 );
    CHECK( dialog.preferences().canvasColor == original.canvasColor );
    preset->setCurrentIndex( preset->findData( "slate" ) );
    apply->click();
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
