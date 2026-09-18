//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Verifies content framing, visible metrics, and depth-cued wires.
//////////////////////////////////////////////////////////////////////////
#include "CypherTileCanvas.h"
#include "CypherTileOrthoView.h"
#include "CypherTileGrid.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QFontMetrics>
#include <QImage>
#include <QMouseEvent>
#include <QWheelEvent>

using namespace cypher::tools::tile_editor;

namespace {
void EnsureReadabilityApplication()
{
    if ( QApplication::instance() ) return;
    qputenv( "QT_QPA_PLATFORM", "offscreen" );
    static int argc = 1;
    static char name[] = "TileViewReadabilityTests";
    static char *argv[]{ name, nullptr };
    static QApplication application( argc, argv );
}

QImage Render( QWidget &view )
{
    QImage frame( view.size(), QImage::Format_ARGB32_Premultiplied );
    frame.fill( Qt::transparent );
    view.render( &frame );
    return frame;
}

int PeakRed( const QImage &frame, QPoint point )
{
    int peak = 0;
    for ( int y = point.y() - 2; y <= point.y() + 2; ++y )
        for ( int x = point.x() - 2; x <= point.x() + 2; ++x )
            if ( frame.rect().contains( x, y ) ) peak = std::max( peak, frame.pixelColor( x, y ).red() );
    return peak;
}

int CountEdgeOverlayChanges( const QImage &before, const QImage &after )
{
    int changes = 0;
    for ( int y = 0; y < after.height(); ++y ) {
        for ( int x = 0; x < after.width(); ++x ) {
            if ( ( y < 24 || x < 48 ) &&
                 before.pixelColor( x, y ) != after.pixelColor( x, y ) ) ++changes;
        }
    }
    return changes;
}
}

TEST_CASE( "Empty maps use a configurable working scale regardless of document dimensions", "[TileEditor][Readability]" )
{
    EnsureReadabilityApplication();
    for ( unsigned extent : { 8u, 64u, 256u } ) {
        CypherTileDocumentBridge document;
        tile_map_document_desc_t description{};
        description.nWidth = description.nHeight = extent;
        QString error;
        REQUIRE( document.newDocument( description, &error ) );
        document.markSaved();
        tile_editor_preferences_t preferences{};
        preferences.emptyViewCellPixels = 48;
        CypherTileCanvas top;
        top.resize( 700, 420 );
        top.setPreferences( preferences );
        top.setDocumentBridge( &document );
        CHECK( top.zoomFactor() == 48 );
        CHECK( top.viewOrigin() == QPointF( 40, 40 ) );
        CypherTileOrthoView front( tile_editor_ortho_plane_t::FRONT );
        front.resize( 700, 420 );
        front.setPreferences( preferences );
        front.setDocumentBridge( &document );
        CHECK( front.pixelsPerUnit() * description.nCellSize == Catch::Approx( 48 ) );
        CHECK_FALSE( document.isDirty() );
    }
}

TEST_CASE( "Top view frames painted content without including unused map cells", "[TileEditor][Readability]" )
{
    EnsureReadabilityApplication();
    CypherTileDocumentBridge document;
    tile_map_document_desc_t description{};
    description.nWidth = description.nHeight = 256;
    QString error;
    REQUIRE( document.newDocument( description, &error ) );
    REQUIRE( document.beginEdit( QStringLiteral( "Small room" ), &error ) );
    REQUIRE( document.paintRect( { 141, 97, 4, 3 }, { 0, 1, 0 }, &error ) );
    REQUIRE( document.commitEdit( &error ) );
    CypherTileCanvas top;
    top.resize( 700, 420 );
    top.setDocumentBridge( &document );
    const QPointF topLeft = top.viewOrigin() + QPointF( 141, 97 ) * top.zoomFactor();
    const QPointF bottomRight = topLeft + QPointF( 4, 3 ) * top.zoomFactor();
    CHECK( top.zoomFactor() > 80 );
    CHECK( topLeft.x() >= 39 );
    CHECK( topLeft.y() >= 39 );
    CHECK( bottomRight.x() <= top.width() - 39 );
    CHECK( bottomRight.y() <= top.height() - 39 );
    top.setSelectionRect( { 141, 97, 2, 3 } );
    CHECK( top.viewMetrics().contains( QStringLiteral( "Selected 2 × 3" ) ) );
    CHECK( top.viewMetrics().contains( QStringLiteral( "px/cell" ) ) );
}

TEST_CASE( "Adaptive grid retains a hierarchy and never changes authored metrics or cells", "[TileEditor][Readability]" )
{
    CHECK( TileEditorGrid_EffectiveMajorSpacingCells( 1, 8 ) == 8 );
    CHECK( TileEditorGrid_EffectiveMajorSpacingCells( 8, 8 ) == 64 );
    CHECK( TileEditorGrid_EffectiveMajorSpacingCells( 32, 8 ) == 256 );
    CHECK( TileEditorGrid_EffectiveMajorSpacingCells( 3, 8 ) == 24 );
    EnsureReadabilityApplication();
    CypherTileDocumentBridge document;
    tile_map_document_desc_t description{};
    QString error;
    REQUIRE( document.newDocument( description, &error ) );
    document.markSaved();
    CypherTileCanvas top;
    top.resize( 500, 400 );
    top.setDocumentBridge( &document );
    top.setZoomFactor( 2 );
    CHECK( top.viewMetrics().contains( QStringLiteral( "Grid 8 cells" ) ) );
    top.setZoomFactor( 32 );
    CHECK( top.viewMetrics().contains( QStringLiteral( "Grid 1 cell" ) ) );
    CHECK( document.document()->nCellSize == description.nCellSize );
    CHECK( document.document()->nLevelHeight == description.nLevelHeight );
    CHECK_FALSE( document.isDirty() );
}

TEST_CASE( "Coordinate ruler spacing stays zero anchored and coarsens for readable labels",
    "[TileEditor][Readability][Rulers]" )
{
    CHECK( TileEditorGrid_EffectiveRulerSpacing( 8.0, 10.0, 64.0 ) == 8.0 );
    CHECK( TileEditorGrid_EffectiveRulerSpacing( 8.0, 4.0, 64.0 ) == 16.0 );
    CHECK( TileEditorGrid_EffectiveRulerSpacing( 8.0, 0.5, 64.0 ) == 128.0 );
    CHECK( TileEditorGrid_EffectiveRulerSpacing( 0.0, 10.0, 64.0 ) == 1.0 );
    CHECK( TileEditorGrid_EffectiveRulerSpacing( 8.0, 0.0, 64.0 ) == 8.0 );

    const double extreme = TileEditorGrid_EffectiveRulerSpacing( 16.0, 0.0001, 64.0 );
    CHECK( extreme * 0.0001 >= 64.0 );
    CHECK( extreme * 0.5 * 0.0001 < 64.0 );
    // Multiples of the returned interval remain anchored to authored zero,
    // including the negative coordinates visible in Front and Side views.
    CHECK( std::remainder( -3.0 * extreme, extreme ) == Catch::Approx( 0.0 ) );
}

TEST_CASE( "Coordinate rulers are themed overlays independent of grid visibility",
    "[TileEditor][Readability][Rulers]" )
{
    EnsureReadabilityApplication();
    CypherTileDocumentBridge document;
    QString error;
    REQUIRE( document.newDocument( { 16, 16, 2.0f, 2.0f }, &error ) );
    document.markSaved();

    tile_editor_preferences_t preferences{};
    preferences.showGrid = false;
    preferences.showViewMetrics = false;
    preferences.showMaterialLabels = false;
    preferences.showViewAxes = false;
    preferences.canvasColor = QColor( 3, 7, 11 );
    preferences.panelColor = QColor( 47, 53, 61 );
    preferences.textColor = QColor( 241, 243, 245 );
    preferences.majorGridColor = QColor( 83, 191, 227 );
    preferences.accentColor = QColor( 241, 151, 42 );

    CypherTileCanvas top;
    top.resize( 520, 360 );
    preferences.showCoordinateRulers = false;
    top.setPreferences( preferences );
    top.setDocumentBridge( &document );
    const QPointF topOrigin = top.viewOrigin();
    const auto revision = document.document()->nCurrentRevision;
    const QImage topWithout = Render( top );
    preferences.showCoordinateRulers = true;
    top.setPreferences( preferences );
    const QImage topWith = Render( top );
    CHECK( topWithout.pixelColor( top.width() - 2, 2 ) == preferences.canvasColor );
    CHECK( topWith.pixelColor( top.width() - 2, 2 ) == preferences.canvasColor );
    CHECK( topWith.pixelColor( 2, top.height() - 2 ) == preferences.canvasColor );
    CHECK( CountEdgeOverlayChanges( topWithout, topWith ) > 0 );
    CHECK( topWith.pixelColor( top.width() / 2, top.height() / 2 ) ==
           topWithout.pixelColor( top.width() / 2, top.height() / 2 ) );
    CHECK( top.viewOrigin() == topOrigin );
    CHECK( document.document()->nCurrentRevision == revision );
    CHECK_FALSE( document.isDirty() );

    CypherTileOrthoView front( tile_editor_ortho_plane_t::FRONT );
    front.resize( 520, 360 );
    preferences.showCoordinateRulers = false;
    front.setPreferences( preferences );
    front.setDocumentBridge( &document );
    const QPointF frontOrigin = front.viewOrigin();
    const QImage frontWithout = Render( front );
    preferences.showCoordinateRulers = true;
    front.setPreferences( preferences );
    const QImage frontWith = Render( front );
    CHECK( frontWithout.pixelColor( front.width() - 2, 2 ) == preferences.canvasColor );
    CHECK( frontWith.pixelColor( front.width() - 2, 2 ) == preferences.canvasColor );
    CHECK( frontWith.pixelColor( 2, front.height() - 2 ) == preferences.canvasColor );
    CHECK( CountEdgeOverlayChanges( frontWithout, frontWith ) > 0 );
    CHECK( frontWith.pixelColor( front.width() / 2, front.height() / 2 ) ==
           frontWithout.pixelColor( front.width() / 2, front.height() / 2 ) );
    CHECK( front.viewOrigin() == frontOrigin );
    CHECK( document.document()->nCurrentRevision == revision );
    CHECK_FALSE( document.isDirty() );
}

TEST_CASE( "Elevation rulers draw negative world-coordinate ticks after panning",
    "[TileEditor][Readability][Rulers]" )
{
    EnsureReadabilityApplication();
    CypherTileDocumentBridge document;
    QString error;
    REQUIRE( document.newDocument( { 16, 16, 2.0f, 2.0f }, &error ) );
    document.markSaved();

    tile_editor_preferences_t preferences{};
    preferences.showGrid = false;
    preferences.showCoordinateRulers = true;
    preferences.canvasColor = QColor( 2, 3, 5 );
    preferences.panelColor = QColor( 31, 37, 43 );
    preferences.majorGridColor = QColor( 23, 241, 109 );
    preferences.textColor = Qt::white;
    for ( const auto plane : { tile_editor_ortho_plane_t::FRONT,
                               tile_editor_ortho_plane_t::SIDE } ) {
        CypherTileOrthoView view( plane );
        view.resize( 600, 400 );
        view.setPreferences( preferences );
        view.setDocumentBridge( &document );

        const QPointF pressPosition( 100.0, 100.0 );
        const QPointF dragPosition( 390.0, 100.0 );
        QMouseEvent press( QEvent::MouseButtonPress, pressPosition, pressPosition,
            Qt::MiddleButton, Qt::MiddleButton, Qt::NoModifier );
        QApplication::sendEvent( &view, &press );
        QMouseEvent move( QEvent::MouseMove, dragPosition, dragPosition,
            Qt::NoButton, Qt::MiddleButton, Qt::NoModifier );
        QApplication::sendEvent( &view, &move );
        QMouseEvent release( QEvent::MouseButtonRelease, dragPosition, dragPosition,
            Qt::MiddleButton, Qt::NoButton, Qt::NoModifier );
        QApplication::sendEvent( &view, &release );

        const double baseUnits = TileEditorGrid_EffectiveMajorSpacingCells(
            1, preferences.majorGridEvery ) * document.document()->nCellSize;
        const double spacing = TileEditorGrid_EffectiveRulerSpacing(
            baseUnits, view.pixelsPerUnit(),
            std::max( 64, QFontMetrics( view.font() ).horizontalAdvance( QStringLiteral( "-000000" ) ) + 12 ) );
        const qreal negativeTickX = view.viewOrigin().x() - spacing * view.pixelsPerUnit();
        REQUIRE( negativeTickX > 40.0 );
        REQUIRE( negativeTickX < view.width() - 4.0 );
        const int tickY = 2;
        const QImage frame = Render( view );
        bool foundTick = false;
        for ( int x = qRound( negativeTickX ) - 1; x <= qRound( negativeTickX ) + 1; ++x )
            if ( frame.pixelColor( x, tickY ) == preferences.majorGridColor ) foundTick = true;
        CHECK( foundTick );
    }
    CHECK_FALSE( document.isDirty() );
}

TEST_CASE( "Projected authoring footer is hidden by default and independently configurable",
    "[TileEditor][Readability][Footer]" )
{
    EnsureReadabilityApplication();
    CypherTileDocumentBridge document;
    QString error;
    REQUIRE( document.newDocument( { 16, 16, 2.0f, 2.0f }, &error ) );
    document.markSaved();

    tile_editor_preferences_t preferences{};
    CHECK_FALSE( preferences.showAuthoringFooter );
    preferences.showGrid = false;
    preferences.showCoordinateRulers = false;
    preferences.showViewAxes = false;
    preferences.showViewMetrics = false;
    preferences.showMaterialLabels = false;
    preferences.canvasColor = QColor( 7, 13, 19 );

    CypherTileOrthoView front( tile_editor_ortho_plane_t::FRONT );
    front.resize( 520, 360 );
    front.setTool( tile_canvas_tool_t::PAINT );
    front.setPreferences( preferences );
    front.setDocumentBridge( &document );
    const QPointF origin = front.viewOrigin();
    const QPoint sample( front.width() / 2, front.height() - 2 );
    const QImage withoutFooter = Render( front );
    CHECK( withoutFooter.pixelColor( sample ) == preferences.canvasColor );

    preferences.showAuthoringFooter = true;
    front.setPreferences( preferences );
    const QImage withFooter = Render( front );
    CHECK( withFooter.pixelColor( sample ) != preferences.canvasColor );
    CHECK( front.viewOrigin() == origin );
    CHECK_FALSE( document.isDirty() );
}

TEST_CASE( "Orthographic depth cues dim far wires and selected outlines remain last", "[TileEditor][Readability]" )
{
    EnsureReadabilityApplication();
    CypherTileDocumentBridge document;
    tile_map_document_desc_t description{};
    description.nWidth = description.nHeight = 8;
    QString error;
    REQUIRE( document.newDocument( description, &error ) );
    REQUIRE( document.beginEdit( QStringLiteral( "Near and far floors" ), &error ) );
    REQUIRE( document.paintCell( { 1, 1 }, { 0, 1, 0 }, &error ) );
    REQUIRE( document.paintCell( { 4, 6 }, { 0, 1, 0 }, &error ) );
    REQUIRE( document.commitEdit( &error ) );
    CypherTileOrthoView front( tile_editor_ortho_plane_t::FRONT );
    front.resize( 600, 400 );
    tile_editor_preferences_t preferences{};
    preferences.canvasColor = preferences.axisXColor = preferences.axisYColor = preferences.axisZColor = Qt::black;
    preferences.wireColor = Qt::red;
    preferences.showGrid = preferences.showViewMetrics = false;
    preferences.wireframeOrtho = preferences.depthCueWireframe = true;
    preferences.showOrthoMaterials = false; // Isolate wire depth cues from surface previews.
    preferences.wireLineWidth = 2;
    preferences.selectionColor = Qt::green;
    front.setPreferences( preferences );
    front.setDocumentBridge( &document );
    // Sample each floor slab's bottom edge, below the wall rectangle, so the
    // semantic wall color cannot mask the wire color being tested. Walls are
    // present because authored cells require a positive wall-height field.
    const auto point = [&]( double x ) {
        return ( front.viewOrigin() + QPointF(
            x * description.nCellSize * front.pixelsPerUnit(),
            TILE_MAP_DEFAULT_FLOOR_THICKNESS * front.pixelsPerUnit() ) ).toPoint();
    };
    const auto near = point( 1.5 ), far = point( 4.5 );
    auto frame = Render( front );
    CHECK( PeakRed( frame, near ) >= 240 );
    CHECK( PeakRed( frame, far ) < 110 );
    CHECK( PeakRed( frame, far ) > 30 );
    preferences.depthCueWireframe = false;
    front.setPreferences( preferences );
    frame = Render( front );
    CHECK( PeakRed( frame, far ) >= 240 );
    front.setSelection( true, { 4, 6 } );
    frame = Render( front );
    int green = 0;
    for ( int y = far.y() - 2; y <= far.y() + 2; ++y )
        green = std::max( green, frame.pixelColor( far.x(), y ).green() );
    CHECK( green >= 240 );
}

TEST_CASE( "View metrics can be hidden and cursor coordinates follow each projection", "[TileEditor][Readability]" )
{
    EnsureReadabilityApplication();
    CypherTileDocumentBridge document;
    tile_map_document_desc_t description{};
    QString error;
    REQUIRE( document.newDocument( description, &error ) );
    for ( auto plane : { tile_editor_ortho_plane_t::FRONT, tile_editor_ortho_plane_t::SIDE } ) {
        CypherTileOrthoView view( plane );
        view.resize( 600, 400 );
        view.setDocumentBridge( &document );
        const auto cursor = view.viewOrigin() + QPointF( 3, -2 ) * view.pixelsPerUnit();
        QMouseEvent motion( QEvent::MouseMove, cursor, cursor, Qt::NoButton, Qt::NoButton, Qt::NoModifier );
        QApplication::sendEvent( &view, &motion );
        CHECK( view.viewMetrics().contains( plane == tile_editor_ortho_plane_t::FRONT
            ? QStringLiteral( "X 3.00  Z 2.00" ) : QStringLiteral( "Y 3.00  Z 2.00" ) ) );
        tile_editor_preferences_t preferences{};
        preferences.showViewMetrics = true;
        view.setPreferences( preferences );
        const auto withMetrics = Render( view );
        preferences.showViewMetrics = false;
        view.setPreferences( preferences );
        const auto withoutMetrics = Render( view );
        int different = 0;
        for ( int y = view.height() - 35; y < view.height(); ++y )
            for ( int x = 0; x < view.width(); ++x )
                if ( withMetrics.pixel( x, y ) != withoutMetrics.pixel( x, y ) ) ++different;
        CHECK( different > 100 );
    }
}

TEST_CASE( "Top wireframe can hide tile seams without hiding material or height boundaries", "[TileEditor][Readability]" )
{
    EnsureReadabilityApplication();
    CypherTileDocumentBridge document;
    QString error;
    REQUIRE( document.newDocument( { 8, 8, 2.0f, 2.0f }, &error ) );
    REQUIRE( document.beginEdit( "Adjacent tiles", &error ) );
    REQUIRE( document.paintRect( { 1, 1, 2, 1 }, { 0, 1, 0 }, &error ) );
    REQUIRE( document.commitEdit( &error ) );
    CypherTileCanvas top;
    top.resize( 600, 400 );
    tile_editor_preferences_t preferences;
    preferences.showGrid = preferences.showOrthoMaterials = false;
    preferences.wireframeOrtho = true;
    preferences.canvasColor = Qt::black;
    preferences.wireColor = Qt::red;
    preferences.wireLineWidth = 2.0;
    top.setPreferences( preferences );
    top.setTool( tile_canvas_tool_t::SELECT );
    top.setDocumentBridge( &document );
    const auto seam = ( top.viewOrigin() + QPointF( 2.0, 1.5 ) * top.zoomFactor() ).toPoint();
    const int clean = PeakRed( Render( top ), seam );
    preferences.showInternalTileEdges = true;
    top.setPreferences( preferences );
    CHECK( PeakRed( Render( top ), seam ) > clean + 30 );
    preferences.showInternalTileEdges = false;
    top.setPreferences( preferences );
    for ( const tile_map_paint_t paint : { tile_map_paint_t{ 0, 1, 3 }, tile_map_paint_t{ 1, 1, 0 }, tile_map_paint_t{ 0, 2, 0 } } ) {
        REQUIRE( document.beginEdit( "Tile boundary", &error ) );
        REQUIRE( document.paintCell( { 2, 1 }, paint, &error ) );
        REQUIRE( document.commitEdit( &error ) );
        CHECK( PeakRed( Render( top ), seam ) > clean + 30 );
        REQUIRE( document.undo( &error ) );
    }
}
