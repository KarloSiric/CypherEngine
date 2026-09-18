//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Material display preferences, persistence and settings controls.
//////////////////////////////////////////////////////////////////////////
#include "CypherTileEditorConfig.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>

#include <limits>

using namespace cypher::tools::tile_editor;

namespace
{
QApplication &MaterialPreferencesApplication()
{
    if ( auto *pApplication = qobject_cast<QApplication *>( QApplication::instance() ) )
        return *pApplication;
    qputenv( "QT_QPA_PLATFORM", "offscreen" );
    static int argc = 1;
    static char name[] = "TileMaterialDisplayPreferencesTests";
    static char *argv[]{ name, nullptr };
    static QApplication application( argc, argv );
    return application;
}
} // namespace

TEST_CASE( "Material display preferences default on and normalize opacity to useful finite limits",
    "[TileEditor][Materials][Preferences]" )
{
    tile_editor_preferences_t preferences;
    CHECK( preferences.showOrthoMaterials );
    CHECK_FALSE( preferences.showMaterialLabels );
    CHECK( preferences.orthoMaterialOpacity == Catch::Approx( 0.8 ) );
    preferences.orthoMaterialOpacity = -1.0;
    CHECK( TileEditorPreferences_Normalize( preferences ).orthoMaterialOpacity == Catch::Approx( 0.2 ) );
    preferences.orthoMaterialOpacity = 4.0;
    CHECK( TileEditorPreferences_Normalize( preferences ).orthoMaterialOpacity == Catch::Approx( 1.0 ) );
    preferences.orthoMaterialOpacity = std::numeric_limits<double>::quiet_NaN();
    CHECK( TileEditorPreferences_Normalize( preferences ).orthoMaterialOpacity == Catch::Approx( 0.8 ) );
    preferences.orthoMaterialOpacity = std::numeric_limits<double>::infinity();
    CHECK( TileEditorPreferences_Normalize( preferences ).orthoMaterialOpacity == Catch::Approx( 0.8 ) );
    preferences.orthoMaterialOpacity = 0.375;
    preferences.showOrthoMaterials = false;
    preferences.showMaterialLabels = false;
    TileEditorPreferences_ApplyColorPreset( preferences, "slate" );
    CHECK( preferences.orthoMaterialOpacity == Catch::Approx( 0.375 ) );
    CHECK_FALSE( preferences.showOrthoMaterials );
    CHECK_FALSE( preferences.showMaterialLabels );
}

TEST_CASE( "Material display configuration persists through native settings and readable Viewport fields",
    "[TileEditor][Materials][Config]" )
{
    MaterialPreferencesApplication();
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    tile_editor_preferences_t original;
    original.showOrthoMaterials = false;
    original.showMaterialLabels = false;
    original.orthoMaterialOpacity = 0.375;
    QSettings native( directory.filePath( "native.ini" ), QSettings::IniFormat );
    TileEditorPreferences_Save( native, original );
    const auto nativeLoaded = TileEditorPreferences_Load( native );
    CHECK_FALSE( nativeLoaded.showOrthoMaterials );
    CHECK_FALSE( nativeLoaded.showMaterialLabels );
    CHECK( nativeLoaded.orthoMaterialOpacity == Catch::Approx( 0.375 ) );
    const auto path = directory.filePath( "editor.ini" );
    QString error;
    REQUIRE( TileEditorConfig_Save( path, nativeLoaded, error ) );
    tile_editor_preferences_t reloaded;
    REQUIRE( TileEditorConfig_Load( path, reloaded, error ) );
    CHECK_FALSE( reloaded.showOrthoMaterials );
    CHECK_FALSE( reloaded.showMaterialLabels );
    CHECK( reloaded.orthoMaterialOpacity == Catch::Approx( 0.375 ) );
    QSettings serialized( path, QSettings::IniFormat );
    CHECK( serialized.contains( "Viewport/showOrthoMaterials" ) );
    CHECK( serialized.contains( "Viewport/showMaterialLabels" ) );
    CHECK( serialized.contains( "Viewport/orthoMaterialOpacity" ) );
    CHECK( serialized.value( "Viewport/orthoMaterialOpacity" ).toDouble() == Catch::Approx( 0.375 ) );
}

TEST_CASE( "Invalid material display configuration cannot partially replace current settings",
    "[TileEditor][Materials][Config]" )
{
    MaterialPreferencesApplication();
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto path = directory.filePath( "invalid.ini" );
    tile_editor_preferences_t preferences;
    preferences.orthoMaterialOpacity = 0.55;
    for ( const QByteArray &field : { QByteArray( "orthoMaterialOpacity=nan\n" ),
                                    QByteArray( "showMaterialLabels=maybe\n" ) } ) {
        INFO( field.constData() );
        QFile file( path );
        REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
        const QByteArray source = "[Editor]\nschemaVersion=1\n[Viewport]\nshowOrthoMaterials=false\n" + field;
        REQUIRE( file.write( source ) == source.size() );
        file.close();
        QString error;
        CHECK_FALSE( TileEditorConfig_Load( path, preferences, error ) );
        CHECK_FALSE( error.isEmpty() );
        CHECK( preferences.showOrthoMaterials );
        CHECK_FALSE( preferences.showMaterialLabels );
        CHECK( preferences.orthoMaterialOpacity == Catch::Approx( 0.55 ) );
    }
}

TEST_CASE( "Material display controls use percentages and Reset All stages all material options",
    "[TileEditor][Materials][Settings]" )
{
    MaterialPreferencesApplication();
    tile_editor_preferences_t original;
    original.showOrthoMaterials = false;
    original.showMaterialLabels = false;
    original.orthoMaterialOpacity = 0.375;
    CypherTileEditorSettingsDialog dialog( original );
    auto *pMaterials = dialog.findChild<QCheckBox *>( "TileSettingsShowOrthoMaterials" );
    auto *pLabels = dialog.findChild<QCheckBox *>( "TileSettingsShowMaterialLabels" );
    auto *pOpacity = dialog.findChild<QDoubleSpinBox *>( "TileSettingsOrthoMaterialOpacity" );
    auto *pButtons = dialog.findChild<QDialogButtonBox *>();
    REQUIRE( pMaterials ); REQUIRE( pLabels ); REQUIRE( pOpacity ); REQUIRE( pButtons );
    CHECK_FALSE( pMaterials->isChecked() );
    CHECK_FALSE( pLabels->isChecked() );
    CHECK_FALSE( pOpacity->isEnabled() );
    CHECK( pOpacity->value() == Catch::Approx( 37.5 ) );
    CHECK( dialog.preferences().orthoMaterialOpacity == Catch::Approx( 0.375 ) );
    pMaterials->setChecked( true );
    CHECK( pOpacity->isEnabled() );
    pOpacity->setValue( 65.0 );
    pLabels->setChecked( true );
    CHECK( dialog.preferences().orthoMaterialOpacity == Catch::Approx( 0.65 ) );
    CHECK( dialog.preferences().showOrthoMaterials );
    CHECK( dialog.preferences().showMaterialLabels );
    pMaterials->setChecked( false );
    CHECK( dialog.preferences().orthoMaterialOpacity == Catch::Approx( 0.65 ) );
    int calls = 0;
    tile_editor_preferences_t applied;
    dialog.setApplyCallback( [&]( const tile_editor_preferences_t &value ) {
        ++calls;
        applied = value;
        return true;
    } );
    pButtons->button( QDialogButtonBox::RestoreDefaults )->click();
    CHECK( calls == 0 );
    CHECK( pMaterials->isChecked() );
    CHECK_FALSE( pLabels->isChecked() );
    CHECK( pOpacity->isEnabled() );
    CHECK( pOpacity->value() == Catch::Approx( 80.0 ) );
    pButtons->button( QDialogButtonBox::Apply )->click();
    CHECK( calls == 1 );
    CHECK( applied.showOrthoMaterials );
    CHECK_FALSE( applied.showMaterialLabels );
    CHECK( applied.orthoMaterialOpacity == Catch::Approx( 0.8 ) );
}
