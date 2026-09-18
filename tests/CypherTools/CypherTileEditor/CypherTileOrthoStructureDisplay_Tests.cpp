//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Verifies independent floor, wall-height and wall-thickness display.
//////////////////////////////////////////////////////////////////////////
#include "CypherTileCanvas.h"
#include "CypherTileEditorConfig.h"
#include "CypherTileEditorSettingsDialog.h"
#include "CypherTileOrthoView.h"

#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QCheckBox>
#include <QImage>
#include <QSettings>
#include <QTemporaryDir>

using namespace cypher::tools::tile_editor;

namespace
{
QApplication &StructureDisplayApplication()
{
    if ( auto *application = qobject_cast<QApplication *>( QApplication::instance() ) )
        return *application;
    qputenv( "QT_QPA_PLATFORM", "offscreen" );
    static int argc = 1;
    static char name[] = "TileOrthoStructureDisplayTests";
    static char *argv[]{ name, nullptr };
    static QApplication application( argc, argv );
    return application;
}

QImage Render( QWidget &view )
{
    QImage image( view.size(), QImage::Format_ARGB32_Premultiplied );
    image.fill( Qt::transparent );
    view.render( &image );
    return image;
}

int ColorCount( const QImage &image, const QColor &color )
{
    int count = 0;
    for ( int y = 0; y < image.height(); ++y )
        for ( int x = 0; x < image.width(); ++x )
            count += image.pixelColor( x, y ) == color;
    return count;
}

int PixelDifference( const QImage &first, const QImage &second )
{
    REQUIRE( first.size() == second.size() );
    int count = 0;
    for ( int y = 0; y < first.height(); ++y )
        for ( int x = 0; x < first.width(); ++x )
            count += first.pixel( x, y ) != second.pixel( x, y );
    return count;
}

tile_editor_preferences_t StructurePreferences()
{
    tile_editor_preferences_t preferences;
    preferences.showGrid = false;
    preferences.showMarkers = false;
    preferences.showViewMetrics = false;
    preferences.showAuthoringFooter = false;
    preferences.showCoordinateRulers = false;
    preferences.showViewAxes = false;
    preferences.showOrthoMaterials = false;
    preferences.wireframeOrtho = true;
    preferences.depthCueWireframe = false;
    preferences.canvasColor = QColor( "#101820" );
    preferences.wireColor = QColor( "#35c9ed" );
    preferences.wallColor = QColor( "#f04dbe" );
    return preferences;
}

void NewStructureMap( CypherTileDocumentBridge &document )
{
    tile_map_document_desc_t description;
    description.nWidth = description.nHeight = 6;
    description.nCellSize = description.nLevelHeight = 2.0f;
    QString error;
    REQUIRE( document.newDocument( description, &error ) );
    REQUIRE( document.beginEdit( QStringLiteral( "Display structure" ), &error ) );
    REQUIRE( document.paintCell( { 2, 2 }, { 0, 2, 0 }, &error ) );
    REQUIRE( document.commitEdit( &error ) );
}
} // namespace

TEST_CASE( "Floor, wall-height and wall-thickness controls persist independently",
           "[TileEditor][Config][OrthoDisplay]" )
{
    StructureDisplayApplication();
    tile_editor_preferences_t preferences;
    CHECK( preferences.showFloorSurfaces );
    CHECK( preferences.showWallHeight );
    CHECK( preferences.showWallThickness );
    preferences.showFloorSurfaces = false;
    preferences.showWallHeight = false;
    preferences.showWallThickness = false;

    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    QSettings native( directory.filePath( "native.ini" ), QSettings::IniFormat );
    TileEditorPreferences_Save( native, preferences );
    const auto nativeLoaded = TileEditorPreferences_Load( native );
    CHECK_FALSE( nativeLoaded.showFloorSurfaces );
    CHECK_FALSE( nativeLoaded.showWallHeight );
    CHECK_FALSE( nativeLoaded.showWallThickness );

    QString error;
    const QString path = directory.filePath( "editor.ini" );
    REQUIRE( TileEditorConfig_Save( path, nativeLoaded, error ) );
    tile_editor_preferences_t loaded;
    REQUIRE( TileEditorConfig_Load( path, loaded, error ) );
    CHECK_FALSE( loaded.showFloorSurfaces );
    CHECK_FALSE( loaded.showWallHeight );
    CHECK_FALSE( loaded.showWallThickness );

    QSettings serialized( path, QSettings::IniFormat );
    CHECK( serialized.contains( "Viewport/showFloorSurfaces" ) );
    CHECK( serialized.contains( "Viewport/showWallHeight" ) );
    CHECK( serialized.contains( "Viewport/showWallThickness" ) );

    CypherTileEditorSettingsDialog dialog( loaded );
    auto *floors = dialog.findChild<QCheckBox *>( "TileSettingsShowFloorSurfaces" );
    auto *height = dialog.findChild<QCheckBox *>( "TileSettingsShowWallHeight" );
    auto *thickness = dialog.findChild<QCheckBox *>( "TileSettingsShowWallThickness" );
    REQUIRE( floors );
    REQUIRE( height );
    REQUIRE( thickness );
    CHECK_FALSE( floors->isChecked() );
    CHECK_FALSE( height->isChecked() );
    CHECK_FALSE( thickness->isChecked() );
    floors->setChecked( true );
    CHECK( dialog.preferences().showFloorSurfaces );
    CHECK_FALSE( dialog.preferences().showWallHeight );
    CHECK_FALSE( dialog.preferences().showWallThickness );
}

TEST_CASE( "Top view separates floor surfaces from physically thick wall strips",
           "[TileEditor][Display][OrthoStructure]" )
{
    StructureDisplayApplication();
    CypherTileDocumentBridge document;
    NewStructureMap( document );
    auto preferences = StructurePreferences();
    CypherTileCanvas view;
    view.resize( 640, 420 );
    view.setPreferences( preferences );
    view.setDocumentBridge( &document );
    view.setZoomFactor( 64.0 );

    const QImage detailed = Render( view );
    CHECK( ColorCount( detailed, preferences.wallColor ) > 0 );

    preferences.showWallThickness = false;
    view.setPreferences( preferences );
    const QImage centerLines = Render( view );
    CHECK( PixelDifference( detailed, centerLines ) > 100 );

    preferences.showFloorSurfaces = false;
    view.setPreferences( preferences );
    const QImage wallsOnly = Render( view );
    CHECK( ColorCount( wallsOnly, preferences.wallColor ) > 0 );
    CHECK( PixelDifference( centerLines, wallsOnly ) > 100 );
}

TEST_CASE( "Elevation views can isolate floor slabs from generated wall height",
           "[TileEditor][Display][OrthoStructure]" )
{
    StructureDisplayApplication();
    CypherTileDocumentBridge document;
    NewStructureMap( document );
    auto preferences = StructurePreferences();
    CypherTileOrthoView view( tile_editor_ortho_plane_t::FRONT );
    view.resize( 640, 420 );
    view.setPreferences( preferences );
    view.setDocumentBridge( &document );

    const QImage complete = Render( view );
    CHECK( ColorCount( complete, preferences.wallColor ) > 0 );
    CHECK( ColorCount( complete, preferences.wireColor ) > 0 );

    preferences.showWallHeight = false;
    view.setPreferences( preferences );
    const QImage floorOnly = Render( view );
    CHECK( ColorCount( floorOnly, preferences.wallColor ) == 0 );
    CHECK( ColorCount( floorOnly, preferences.wireColor ) > 0 );

    preferences.showFloorSurfaces = false;
    view.setPreferences( preferences );
    const QImage constructionOnly = Render( view );
    CHECK( ColorCount( constructionOnly, preferences.wallColor ) == 0 );
    CHECK( ColorCount( constructionOnly, preferences.wireColor ) == 0 );
    CHECK( PixelDifference( floorOnly, constructionOnly ) > 0 );
}
