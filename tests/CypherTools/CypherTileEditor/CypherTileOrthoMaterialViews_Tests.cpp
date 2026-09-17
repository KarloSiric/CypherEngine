//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Verifies material surfaces and identities in all orthographic panes.
//////////////////////////////////////////////////////////////////////////
#include "CypherTileCanvas.h"
#include "CypherTileOrthoMaterials.h"
#include "CypherTileOrthoView.h"

#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QImage>
#include <QMouseEvent>

using namespace cypher::tools::tile_editor;

namespace {
QApplication &MaterialViewApplication()
{
    if ( QApplication::instance() ) return *static_cast<QApplication *>( QApplication::instance() );
    qputenv( "QT_QPA_PLATFORM", "offscreen" );
    static int argc = 1;
    static char name[] = "TileOrthoMaterialViewsTests";
    static char *argv[]{ name, nullptr };
    static QApplication application( argc, argv );
    return application;
}

void Mouse( QWidget &view, QEvent::Type type, QPointF point,
            Qt::MouseButton button = Qt::NoButton )
{
    QMouseEvent event( type, point, point, button, button, Qt::NoModifier );
    QApplication::sendEvent( &view, &event );
}

QImage Render( QWidget &view )
{
    QImage image( view.size(), QImage::Format_ARGB32_Premultiplied );
    image.fill( Qt::transparent );
    view.render( &image );
    return image;
}

tile_ortho_material_t Stripes( const QString &name, QColor bright, QColor dark )
{
    tile_ortho_material_t material;
    material.bound = true;
    material.label = name;
    material.path = QStringLiteral( "materials/%1.cymat" ).arg( name );
    material.color = bright;
    material.image = QImage( 8, 8, QImage::Format_ARGB32 );
    for ( int y = 0; y < 8; ++y )
        for ( int x = 0; x < 8; ++x ) material.image.setPixelColor( x, y, x < 4 ? bright : dark );
    material.textureBrush = QBrush( material.image );
    return material;
}

tile_ortho_material_cache_t Materials()
{
    tile_ortho_material_cache_t cache;
    cache.entries.insert( 0u, Stripes( QStringLiteral( "red_stripes" ), QColor( 230, 35, 25 ), QColor( 130, 20, 15 ) ) );
    cache.entries.insert( 1u, Stripes( QStringLiteral( "blue_stripes" ), QColor( 25, 35, 230 ), QColor( 15, 20, 130 ) ) );
    return cache;
}

tile_editor_preferences_t SurfacePreferences()
{
    tile_editor_preferences_t preferences;
    preferences.showGrid = false;
    preferences.showMarkers = false;
    preferences.showViewMetrics = false;
    preferences.showMaterialLabels = false;
    preferences.showOrthoMaterials = true;
    preferences.orthoMaterialOpacity = 1.0;
    preferences.wireframeOrtho = true;
    return preferences;
}

int ColorCount( const QImage &image, QColor color )
{
    int count = 0;
    for ( int y = 0; y < image.height(); ++y )
        for ( int x = 0; x < image.width(); ++x )
            if ( image.pixelColor( x, y ) == color ) ++count;
    return count;
}

void NewMap( CypherTileDocumentBridge &document )
{
    tile_map_document_desc_t description;
    description.nWidth = description.nHeight = 8;
    description.nCellSize = description.nLevelHeight = 2.0f;
    QString error;
    REQUIRE( document.newDocument( description, &error ) );
}
}

TEST_CASE( "Top view shows distinct cached textures over wireframe and restores wireframe on toggle", "[TileEditor][OrthoMaterials]" )
{
    MaterialViewApplication();
    CypherTileDocumentBridge document;
    NewMap( document );
    QString error;
    REQUIRE( document.beginEdit( QStringLiteral( "Material fixture" ), &error ) );
    REQUIRE( document.paintCell( { 1, 1 }, { 0, 1, 0 }, &error ) );
    REQUIRE( document.paintCell( { 2, 1 }, { 0, 1, 1 }, &error ) );
    REQUIRE( document.commitEdit( &error ) );
    const auto cache = Materials();
    auto preferences = SurfacePreferences();
    CypherTileCanvas view;
    view.resize( 640, 360 );
    view.setPreferences( preferences );
    view.setMaterialCache( &cache );
    view.setDocumentBridge( &document );
    view.setTool( tile_canvas_tool_t::SELECT );
    const QImage textured = Render( view );
    for ( const QColor color : { QColor( 230, 35, 25 ), QColor( 130, 20, 15 ),
                               QColor( 25, 35, 230 ), QColor( 15, 20, 130 ) } )
        CHECK( ColorCount( textured, color ) > 100 );

    preferences.orthoMaterialOpacity = 0.4;
    view.setPreferences( preferences );
    const QImage translucent = Render( view );
    CHECK( ColorCount( translucent, QColor( 230, 35, 25 ) ) == 0 );
    CHECK( textured != translucent );

    preferences.showOrthoMaterials = false;
    view.setPreferences( preferences );
    const QImage wireframe = Render( view );
    CHECK( ColorCount( wireframe, QColor( 230, 35, 25 ) ) == 0 );
    CHECK( ColorCount( wireframe, QColor( 25, 35, 230 ) ) == 0 );
    CHECK( wireframe != textured );
    // Cache changes must have no effect when the material overlay is disabled.
    view.setMaterialCache( nullptr );
    CHECK( Render( view ) == wireframe );
}

TEST_CASE( "Top material identity follows hover and selection independently of metrics and zoom", "[TileEditor][OrthoMaterials]" )
{
    MaterialViewApplication();
    CypherTileDocumentBridge document;
    NewMap( document );
    QString error;
    REQUIRE( document.beginEdit( QStringLiteral( "Material fixture" ), &error ) );
    REQUIRE( document.paintCell( { 1, 1 }, { 0, 1, 0 }, &error ) );
    REQUIRE( document.paintCell( { 2, 1 }, { 0, 1, 1 }, &error ) );
    REQUIRE( document.commitEdit( &error ) );
    const auto cache = Materials();
    auto preferences = SurfacePreferences();
    preferences.showMaterialLabels = true;
    CypherTileCanvas view;
    view.resize( 640, 360 );
    view.setPreferences( preferences );
    view.setMaterialCache( &cache );
    view.setDocumentBridge( &document );
    view.setTool( tile_canvas_tool_t::SELECT );
    view.selectCell( { 1, 1 } );
    Render( view );
    CHECK( view.materialDescription().contains( QStringLiteral( "red_stripes" ) ) );
    Mouse( view, QEvent::MouseMove, view.viewOrigin() + QPointF( 2.5, 1.5 ) * view.zoomFactor() );
    CHECK( view.materialDescription().contains( QStringLiteral( "blue_stripes" ) ) );
    const auto metrics = view.viewMetrics();
    preferences.showOrthoMaterials = false;
    view.setPreferences( preferences );
    CHECK( view.materialDescription().contains( QStringLiteral( "blue_stripes" ) ) );
    preferences.showMaterialLabels = false;
    view.setPreferences( preferences );
    CHECK( view.materialDescription().isEmpty() );
    CHECK( view.viewMetrics() == metrics );

    preferences.showMaterialLabels = true;
    view.setPreferences( preferences );
    QEvent leave( QEvent::Leave );
    QApplication::sendEvent( &view, &leave );
    view.setZoomFactor( 0.05 );
    CHECK( view.materialDescription().contains( QStringLiteral( "red_stripes" ) ) );
    CHECK( view.materialDescription().contains( QStringLiteral( "materials/red_stripes.cymat" ) ) );
}

TEST_CASE( "Front and Side show and identify the nearest material using their picking order", "[TileEditor][OrthoMaterials]" )
{
    MaterialViewApplication();
    const auto cache = Materials();
    for ( auto plane : { tile_editor_ortho_plane_t::FRONT, tile_editor_ortho_plane_t::SIDE } ) {
        const bool front = plane == tile_editor_ortho_plane_t::FRONT;
        CypherTileDocumentBridge document;
        NewMap( document );
        QString error;
        REQUIRE( document.beginEdit( QStringLiteral( "Depth fixture" ), &error ) );
        REQUIRE( document.paintCell( { 1, 1 }, { 0, 1, 0 }, &error ) );
        REQUIRE( document.paintCell( front ? tile_map_grid_coord_t{ 1, 3 } : tile_map_grid_coord_t{ 3, 1 },
                                    { 0, 1, 1 }, &error ) );
        REQUIRE( document.commitEdit( &error ) );
        auto preferences = SurfacePreferences();
        CypherTileOrthoView view( plane );
        view.resize( 640, 360 );
        view.setPreferences( preferences );
        view.setMaterialCache( &cache );
        view.setDocumentBridge( &document );
        const QImage textured = Render( view );
        const QColor bright = front ? QColor( 230, 35, 25 ) : QColor( 25, 35, 230 );
        const QColor dark = front ? QColor( 130, 20, 15 ) : QColor( 15, 20, 130 );
        CHECK( ColorCount( textured, bright ) > 100 );
        CHECK( ColorCount( textured, dark ) > 100 );
        const QPointF pointer = view.viewOrigin() + QPointF( 2.5, -1.0 ) * view.pixelsPerUnit();
        preferences.showMaterialLabels = true;
        view.setPreferences( preferences );
        Mouse( view, QEvent::MouseMove, pointer );
        const QString expected = front ? QStringLiteral( "red_stripes" ) : QStringLiteral( "blue_stripes" );
        CHECK( view.materialDescription().contains( expected ) );
        tile_map_grid_coord_t selected{ -1, -1 };
        view.setSelectionCallback( [&]( tile_map_grid_coord_t coordinate ) { selected = coordinate; } );
        Mouse( view, QEvent::MouseButtonPress, pointer, Qt::LeftButton );
        CHECK( selected.x == ( front ? 1 : 3 ) );
        CHECK( selected.y == 1 );
        QEvent leave( QEvent::Leave );
        QApplication::sendEvent( &view, &leave );
        CHECK( view.materialDescription().contains( expected ) );
        CHECK( ColorCount( Render( view ), preferences.selectionColor ) > 50 );
        const QString metrics = view.viewMetrics();
        preferences.showOrthoMaterials = false;
        view.setPreferences( preferences );
        CHECK( view.materialDescription().contains( expected ) );
        preferences.showMaterialLabels = false;
        view.setPreferences( preferences );
        CHECK( view.materialDescription().isEmpty() );
        CHECK( view.viewMetrics() == metrics );
        CHECK( ColorCount( Render( view ), bright ) == 0 );
    }
}

TEST_CASE( "Material panes fall back to named blockout colors without a borrowed cache", "[TileEditor][OrthoMaterials]" )
{
    MaterialViewApplication();
    CypherTileDocumentBridge document;
    NewMap( document );
    QString error;
    REQUIRE( document.beginEdit( QStringLiteral( "Unbound material fixture" ), &error ) );
    REQUIRE( document.paintCell( { 1, 1 }, { 0, 1, 3 }, &error ) );
    REQUIRE( document.commitEdit( &error ) );
    auto preferences = SurfacePreferences();
    preferences.showMaterialLabels = true;
    CypherTileCanvas top;
    top.resize( 480, 300 );
    top.setPreferences( preferences );
    top.setDocumentBridge( &document );
    top.selectCell( { 1, 1 } );
    CHECK( top.materialDescription().contains( QStringLiteral( "Hazard" ) ) );
    CHECK_FALSE( Render( top ).isNull() );
    CypherTileOrthoView front( tile_editor_ortho_plane_t::FRONT );
    front.resize( 480, 300 );
    front.setPreferences( preferences );
    front.setDocumentBridge( &document );
    front.setSelection( true, { 1, 1 } );
    CHECK( front.materialDescription().contains( QStringLiteral( "Hazard" ) ) );
    CHECK_FALSE( Render( front ).isNull() );
}

TEST_CASE( "Orthographic door markers retain diagnostic color and identity instead of inherited cell textures", "[TileEditor][OrthoMaterials]" )
{
    MaterialViewApplication();
    const auto cache = Materials();
    for ( auto plane : { tile_editor_ortho_plane_t::FRONT, tile_editor_ortho_plane_t::SIDE } ) {
        const bool front = plane == tile_editor_ortho_plane_t::FRONT;
        CypherTileDocumentBridge document;
        NewMap( document );
        QString error;
        REQUIRE( document.beginEdit( QStringLiteral( "Door material fixture" ), &error ) );
        REQUIRE( document.paintCell( { 1, 1 }, { 0, 1, 1 }, &error ) );
        REQUIRE( document.placeDoor( { 1, 1 }, front ? tile_map_marker_side_t::NORTH
            : tile_map_marker_side_t::EAST, nullptr, &error ) );
        REQUIRE( document.commitEdit( &error ) );
        auto preferences = SurfacePreferences();
        preferences.showMaterialLabels = true;
        CypherTileOrthoView view( plane );
        view.resize( 640, 360 );
        view.setPreferences( preferences );
        view.setMaterialCache( &cache );
        view.setDocumentBridge( &document );
        const QImage image = Render( view );
        const QPointF pointer = view.viewOrigin() + QPointF( 3.0, -0.8 ) * view.pixelsPerUnit();
        const QColor surface = image.pixelColor( pointer.toPoint() );
        CHECK( surface.red() > 220 );
        CHECK( surface.green() > 80 );
        CHECK( surface.green() < 125 );
        CHECK( surface.blue() < 40 );
        Mouse( view, QEvent::MouseMove, pointer );
        CHECK( view.materialDescription().contains( QStringLiteral( "Diagnostic door marker" ) ) );
        CHECK_FALSE( view.materialDescription().contains( QStringLiteral( "blue_stripes" ) ) );
        CHECK_FALSE( view.materialDescription().contains( QStringLiteral( "Slot" ) ) );
        preferences.showOrthoMaterials = false;
        view.setPreferences( preferences );
        CHECK( view.materialDescription().contains( QStringLiteral( "Diagnostic door marker" ) ) );
    }
}
