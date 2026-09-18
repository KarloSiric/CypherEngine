//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Configurable viewport focus, camera preferences and live settings Apply.
//////////////////////////////////////////////////////////////////////////
#include "CypherTileEditorConfig.h"
#include "CypherTileEditorTheme.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTemporaryDir>

#include <limits>

using namespace cypher::tools::tile_editor;

namespace
{
QApplication &NavigationPreferencesApplication()
{
    if ( auto *pApplication = qobject_cast<QApplication *>( QApplication::instance() ) )
        return *pApplication;
    qputenv( "QT_QPA_PLATFORM", "offscreen" );
    static int argc = 1;
    static char name[] = "TileNavigationPreferencesTests";
    static char *argv[]{ name, nullptr };
    static QApplication application( argc, argv );
    return application;
}

QKeySequenceEdit *ShortcutEditor( CypherTileEditorSettingsDialog &dialog, const QString &id )
{
    auto *pTable = dialog.findChild<QTableWidget *>();
    if ( pTable == nullptr ) return nullptr;
    for ( int i = 0; i < pTable->rowCount(); ++i ) {
        if ( pTable->item( i, 0 )->data( Qt::UserRole ).toString() == id )
            return qobject_cast<QKeySequenceEdit *>( pTable->cellWidget( i, 1 ) );
    }
    return nullptr;
}

QString StyleRuleBody( const QString &style, const QString &selector )
{
    const qsizetype begin = style.indexOf( selector + QStringLiteral( " {" ) );
    if ( begin < 0 ) return {};
    const qsizetype bodyBegin = style.indexOf( QLatin1Char( '{' ), begin ) + 1;
    const qsizetype bodyEnd = style.indexOf( QLatin1Char( '}' ), bodyBegin );
    if ( bodyBegin <= 0 || bodyEnd < bodyBegin ) return {};
    return style.mid( bodyBegin, bodyEnd - bodyBegin ).trimmed();
}
} // namespace

TEST_CASE( "Camera navigation preferences normalize unsupported ranges and nonfinite input",
    "[TileEditor][Camera][Preferences]" )
{
    const tile_editor_preferences_t defaults;
    CHECK_FALSE( defaults.showActiveViewBorder );
    CHECK_FALSE( defaults.showCameraHints );
    CHECK_FALSE( defaults.cameraInvertWheel );
    CHECK_FALSE( defaults.linkOrthographicCameras );
    auto input = defaults;
    input.cameraPanSensitivity = -2.0;
    input.cameraZoomSensitivity = 900.0;
    input.cameraFastMultiplier = -1.0;
    input.cameraSlowMultiplier = 3.0;
    const auto clamped = TileEditorPreferences_Normalize( input );
    CHECK( clamped.cameraPanSensitivity == Catch::Approx( 0.1 ) );
    CHECK( clamped.cameraZoomSensitivity == Catch::Approx( 5.0 ) );
    CHECK( clamped.cameraFastMultiplier == Catch::Approx( 1.0 ) );
    CHECK( clamped.cameraSlowMultiplier == Catch::Approx( 1.0 ) );
    input.cameraPanSensitivity = std::numeric_limits<double>::quiet_NaN();
    input.cameraZoomSensitivity = std::numeric_limits<double>::infinity();
    input.cameraFastMultiplier = 100.0;
    input.cameraSlowMultiplier = -1.0;
    const auto finite = TileEditorPreferences_Normalize( input );
    CHECK( finite.cameraPanSensitivity == Catch::Approx( defaults.cameraPanSensitivity ) );
    CHECK( finite.cameraZoomSensitivity == Catch::Approx( defaults.cameraZoomSensitivity ) );
    CHECK( finite.cameraFastMultiplier == Catch::Approx( 20.0 ) );
    CHECK( finite.cameraSlowMultiplier == Catch::Approx( 0.01 ) );
}

TEST_CASE( "Camera and pane outline preferences survive both native and editable configuration",
    "[TileEditor][Camera][Config]" )
{
    NavigationPreferencesApplication();
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    tile_editor_preferences_t original;
    original.showActiveViewBorder = true;
    original.activeViewColor = QColor( "#53b399" );
    original.showCameraHints = false;
    original.cameraInvertWheel = true;
    original.cameraPanSensitivity = 1.75;
    original.cameraZoomSensitivity = 0.45;
    original.cameraFastMultiplier = 6.5;
    original.cameraSlowMultiplier = 0.15;
    original.linkOrthographicCameras = false;
    QSettings native( directory.filePath( "native.ini" ), QSettings::IniFormat );
    TileEditorPreferences_Save( native, original );
    const auto nativeLoaded = TileEditorPreferences_Load( native );
    QString error;
    const auto path = directory.filePath( "editor.ini" );
    REQUIRE( TileEditorConfig_Save( path, nativeLoaded, error ) );
    tile_editor_preferences_t reloaded;
    REQUIRE( TileEditorConfig_Load( path, reloaded, error ) );
    CHECK( reloaded.showActiveViewBorder );
    CHECK( reloaded.activeViewColor == original.activeViewColor );
    CHECK_FALSE( reloaded.showCameraHints );
    CHECK( reloaded.cameraInvertWheel );
    CHECK( reloaded.cameraPanSensitivity == Catch::Approx( 1.75 ) );
    CHECK( reloaded.cameraZoomSensitivity == Catch::Approx( 0.45 ) );
    CHECK( reloaded.cameraFastMultiplier == Catch::Approx( 6.5 ) );
    CHECK( reloaded.cameraSlowMultiplier == Catch::Approx( 0.15 ) );
    CHECK_FALSE( reloaded.linkOrthographicCameras );
    QFile file( path );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    const auto source = file.readAll();
    CHECK( source.contains( "showActiveViewBorder=true" ) );
    CHECK( source.contains( "cameraPanSensitivity=1.75" ) );
    CHECK( source.contains( "showCameraHints=false" ) );
    CHECK( source.contains( "linkOrthographicCameras=false" ) );
}

TEST_CASE( "Q3Edit pane theme uses unboxed surfaces and an opt-in active outline",
    "[TileEditor][Theme][Preferences]" )
{
    auto &application = NavigationPreferencesApplication();
    tile_editor_preferences_t preferences;
    CypherTileEditorTheme_Apply( application, preferences );
    const auto borderOff = application.styleSheet();
    CHECK( StyleRuleBody( borderOff,
        QStringLiteral( "QWidget[tileViewPane=\"true\"]" ) ) ==
        QStringLiteral( "background: transparent; border: 0;" ) );
    CHECK( StyleRuleBody( borderOff,
        QStringLiteral( "QWidget[tileViewPane=\"true\"][active=\"true\"]" ) ) ==
        QStringLiteral( "border: 0;" ) );
    CHECK( borderOff.contains( "QWidget#TileViewHeader[active=\"true\"] { background:" ) );

    const QString verticalGutter = StyleRuleBody(
        borderOff, QStringLiteral( "QSplitter#TileViewRows::handle:vertical" ) );
    const QString horizontalGutter = StyleRuleBody(
        borderOff,
        QStringLiteral( "QSplitter#TileViewRow0::handle:horizontal, QSplitter#TileViewRow1::handle:horizontal" ) );
    REQUIRE_FALSE( verticalGutter.isEmpty() );
    REQUIRE_FALSE( horizontalGutter.isEmpty() );
    CHECK( verticalGutter.contains(
        "qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1" ) );
    CHECK( horizontalGutter.contains(
        "qlineargradient(x1: 0, y1: 0, x2: 1, y2: 0" ) );
    for ( const QString &gutter : { verticalGutter, horizontalGutter } ) {
        CHECK( gutter.contains( "stop: 0 " ) );
        CHECK( gutter.contains( "stop: 0.20 " ) );
        CHECK( gutter.contains( "stop: 0.80 " ) );
        CHECK( gutter.contains( "stop: 1 " ) );
        CHECK( gutter.contains( "border: 0" ) );
    }

    const QString gutterHover = StyleRuleBody( borderOff, QStringLiteral(
        "QSplitter#TileViewRows::handle:vertical:hover, QSplitter#TileViewRow0::handle:horizontal:hover, "
        "QSplitter#TileViewRow1::handle:horizontal:hover" ) );
    REQUIRE_FALSE( gutterHover.isEmpty() );
    CHECK( gutterHover.contains( "background:" ) );
    CHECK_FALSE( gutterHover.contains( "border" ) );
    preferences.showActiveViewBorder = true;
    preferences.activeViewColor = QColor( "#53b399" );
    CypherTileEditorTheme_Apply( application, preferences );
    const auto borderOn = application.styleSheet();
    CHECK( borderOn.contains(
        "QWidget[tileViewPane=\"true\"][active=\"true\"] { border: 1px solid #53b399; }" ) );
    CHECK( borderOn != borderOff );
    preferences.showActiveViewBorder = false;
    CypherTileEditorTheme_Apply( application, preferences );
    CHECK( application.styleSheet() == borderOff );
    CypherTileEditorTheme_Apply( application );
}

TEST_CASE( "Settings Apply delivers current camera and appearance edits without closing the dialog",
    "[TileEditor][Settings][Camera]" )
{
    NavigationPreferencesApplication();
    tile_editor_preferences_t preferences;
    CypherTileEditorSettingsDialog dialog( preferences );
    auto *pButtons = dialog.findChild<QDialogButtonBox *>();
    auto *pPan = dialog.findChild<QDoubleSpinBox *>( "TileSettingsCameraPanSensitivity" );
    auto *pZoom = dialog.findChild<QDoubleSpinBox *>( "TileSettingsCameraZoomSensitivity" );
    auto *pFast = dialog.findChild<QDoubleSpinBox *>( "TileSettingsCameraFastMultiplier" );
    auto *pSlow = dialog.findChild<QDoubleSpinBox *>( "TileSettingsCameraSlowMultiplier" );
    auto *pBorder = dialog.findChild<QCheckBox *>( "TileSettingsShowActiveViewBorder" );
    auto *pHints = dialog.findChild<QCheckBox *>( "TileSettingsShowCameraHints" );
    auto *pInvert = dialog.findChild<QCheckBox *>( "TileSettingsCameraInvertWheel" );
    auto *pLinked = dialog.findChild<QCheckBox *>( "TileSettingsLinkOrthographicCameras" );
    REQUIRE( pButtons ); REQUIRE( pPan ); REQUIRE( pZoom ); REQUIRE( pFast ); REQUIRE( pSlow );
    REQUIRE( pBorder ); REQUIRE( pHints ); REQUIRE( pInvert ); REQUIRE( pLinked );
    int calls = 0;
    tile_editor_preferences_t applied;
    dialog.setApplyCallback( [&]( const tile_editor_preferences_t &value ) {
        ++calls;
        applied = value;
        return true;
    } );
    dialog.show();
    pPan->setValue( 2.25 );
    pZoom->setValue( 0.5 );
    pFast->setValue( 7.0 );
    pSlow->setValue( 0.1 );
    pBorder->setChecked( true );
    pHints->setChecked( false );
    pInvert->setChecked( true );
    pLinked->setChecked( false );
    pButtons->button( QDialogButtonBox::Apply )->click();
    CHECK( calls == 1 );
    CHECK( dialog.isVisible() );
    CHECK( applied.cameraPanSensitivity == Catch::Approx( 2.25 ) );
    CHECK( applied.cameraZoomSensitivity == Catch::Approx( 0.5 ) );
    CHECK( applied.cameraFastMultiplier == Catch::Approx( 7.0 ) );
    CHECK( applied.cameraSlowMultiplier == Catch::Approx( 0.1 ) );
    CHECK( applied.showActiveViewBorder );
    CHECK_FALSE( applied.showCameraHints );
    CHECK( applied.cameraInvertWheel );
    CHECK_FALSE( applied.linkOrthographicCameras );
    pPan->setValue( 3.0 );
    pButtons->button( QDialogButtonBox::Cancel )->click();
    CHECK( calls == 1 );
    CHECK_FALSE( dialog.isVisible() );
    CHECK( applied.cameraPanSensitivity == Catch::Approx( 2.25 ) );
}

TEST_CASE( "Camera settings describe Shift as fly acceleration instead of a second pan chord",
    "[TileEditor][Settings][Camera][Help]" )
{
    NavigationPreferencesApplication();
    CypherTileEditorSettingsDialog dialog( tile_editor_preferences_t{} );
    auto *pHelp = dialog.findChild<QLabel *>( "TileSettingsCameraHelp" );
    REQUIRE( pHelp != nullptr );
    CHECK( pHelp->text().contains( "Shift accelerates right-mouse flight" ) );
    CHECK_FALSE( pHelp->text().contains( "Shift before pressing right mouse also pans" ) );
}

TEST_CASE( "Shortcut conflicts prevent Apply and restoring defaults resets navigation controls",
    "[TileEditor][Settings][Shortcuts]" )
{
    NavigationPreferencesApplication();
    tile_editor_preferences_t original;
    original.cameraPanSensitivity = 3.0;
    original.cameraFastMultiplier = 12.0;
    original.cameraInvertWheel = true;
    original.showActiveViewBorder = true;
    original.showCameraHints = false;
    CypherTileEditorSettingsDialog dialog( original );
    auto *pButtons = dialog.findChild<QDialogButtonBox *>();
    auto *pCameraShortcut = ShortcutEditor( dialog, "camera.settings" );
    REQUIRE( pButtons ); REQUIRE( pCameraShortcut );
    int calls = 0;
    dialog.setApplyCallback( [&]( const tile_editor_preferences_t & ) {
        ++calls;
        return true;
    } );
    pCameraShortcut->setKeySequence( QKeySequence( "F" ) );
    CHECK_FALSE( pButtons->button( QDialogButtonBox::Apply )->isEnabled() );
    CHECK_FALSE( pButtons->button( QDialogButtonBox::Ok )->isEnabled() );
    pButtons->button( QDialogButtonBox::Apply )->click();
    CHECK( calls == 0 );
    pButtons->button( QDialogButtonBox::RestoreDefaults )->click();
    const auto restored = dialog.preferences();
    CHECK_FALSE( restored.showActiveViewBorder );
    CHECK_FALSE( restored.cameraInvertWheel );
    CHECK_FALSE( restored.showCameraHints );
    CHECK_FALSE( restored.linkOrthographicCameras );
    CHECK( restored.cameraPanSensitivity == Catch::Approx( 1.0 ) );
    CHECK( restored.cameraFastMultiplier == Catch::Approx( 4.0 ) );
    CHECK( restored.shortcuts.value( "camera.settings" ).isEmpty() );
    CHECK( pButtons->button( QDialogButtonBox::Apply )->isEnabled() );
    CHECK( pButtons->button( QDialogButtonBox::Ok )->isEnabled() );
    CHECK( calls == 0 );
    pButtons->button( QDialogButtonBox::Apply )->click();
    CHECK( calls == 1 );
}
