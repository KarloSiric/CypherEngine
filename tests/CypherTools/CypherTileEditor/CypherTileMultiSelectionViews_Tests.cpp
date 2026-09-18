//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Tests exact viewport selection and orthographic/3D tile authoring.
//////////////////////////////////////////////////////////////////////////
#include "CypherTileCanvas.h"
#include "CypherTileOrthoView.h"
#include "CypherTileRenderViewport.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QFocusEvent>
#include <QHideEvent>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

using namespace cypher::tools::tile_editor;
namespace math = ::cypher::math;

namespace {
void EnsureMultiViewApplication()
{
    if ( QApplication::instance() ) return;
    qputenv( "QT_QPA_PLATFORM", "offscreen" );
    static int argc = 1;
    static char name[] = "TileMultiSelectionViewsTests";
    static char *argv[]{ name, nullptr };
    static QApplication application( argc, argv );
}

void NewMap( CypherTileDocumentBridge &document )
{
    tile_map_document_desc_t description;
    description.nWidth = description.nHeight = 12;
    description.nCellSize = description.nLevelHeight = 2.0f;
    QString error;
    REQUIRE( document.newDocument( description, &error ) );
}

void Mouse( QWidget &view, QEvent::Type type, QPointF point, Qt::MouseButton button,
    Qt::KeyboardModifiers modifiers = Qt::NoModifier )
{
    QMouseEvent event( type, point, point,
        type == QEvent::MouseMove ? Qt::NoButton : button,
        type == QEvent::MouseButtonRelease ? Qt::NoButton : button, modifiers );
    QApplication::sendEvent( &view, &event );
}

void Key( QWidget &view, QEvent::Type type, int key )
{
    QKeyEvent event( type, key, Qt::NoModifier );
    QApplication::sendEvent( &view, &event );
}

void Interrupt( QWidget &view, QEvent::Type type )
{
    if ( type == QEvent::FocusOut ) {
        QFocusEvent event( type );
        QApplication::sendEvent( &view, &event );
    } else if ( type == QEvent::Hide ) {
        QHideEvent event;
        QApplication::sendEvent( &view, &event );
    } else {
        QEvent event( type );
        QApplication::sendEvent( &view, &event );
    }
}

QImage Render( QWidget &view )
{
    QImage image( view.size(), QImage::Format_ARGB32_Premultiplied );
    image.fill( Qt::transparent );
    view.render( &image );
    return image;
}

QPointF Project( const CypherTileOrthoView &view, double horizontal, double z )
{
    return view.viewOrigin() + QPointF( horizontal, -z ) * view.pixelsPerUnit();
}

bool HasCell( std::span<const tile_map_grid_coord_t> cells, int x, int y )
{
    return std::any_of( cells.begin(), cells.end(), [&]( auto cell ) { return cell.x == x && cell.y == y; } );
}

int ColoredPixels( const QImage &image, QRect region, QColor color )
{
    int count = 0;
    region = region.intersected( image.rect() );
    for ( int y = region.top(); y <= region.bottom(); ++y )
        for ( int x = region.left(); x <= region.right(); ++x )
            if ( image.pixelColor( x, y ) == color ) ++count;
    return count;
}

QPointF PixelForRay( const QWidget &view, float x, float y )
{
    return { ( x + 1.0 ) * view.width() * 0.5, ( 1.0 - y ) * view.height() * 0.5 };
}
}

TEST_CASE( "All inspection panes canonicalize exact sparse selections and retain rectangular compatibility", "[TileEditor][MultiSelection]" )
{
    EnsureMultiViewApplication();
    const std::array cells{ tile_map_grid_coord_t{ 5, 3 }, tile_map_grid_coord_t{ 1, 1 },
        tile_map_grid_coord_t{ 5, 3 }, tile_map_grid_coord_t{ -1, 2 } };
    CypherTileOrthoView front( tile_editor_ortho_plane_t::FRONT );
    CypherTileRenderViewport render;
    front.setSelectedCells( cells );
    render.setSelectedCells( cells );
    for ( auto selected : { front.selectedCells(), render.selectedCells() } ) {
        REQUIRE( selected.size() == 2 );
        CHECK( selected.front().x == 1 );
        CHECK( selected.front().y == 1 );
        CHECK_FALSE( HasCell( selected, 3, 2 ) );
    }
    front.setSelectionRect( true, { 2, 2, 3, 2 } );
    render.setSelectionRect( true, { 2, 2, 3, 2 } );
    CHECK( front.selectedCells().size() == 6 );
    CHECK( render.selectedCells().size() == 6 );
    front.setSelection( false, {} );
    render.setSelection( false, {} );
    CHECK( front.selectedCells().empty() );
    CHECK( render.selectedCells().empty() );
}

TEST_CASE( "Front and Side sparse highlights exclude occupied holes inside selection bounds", "[TileEditor][MultiSelection]" )
{
    EnsureMultiViewApplication();
    for ( auto plane : { tile_editor_ortho_plane_t::FRONT, tile_editor_ortho_plane_t::SIDE } ) {
        const bool front = plane == tile_editor_ortho_plane_t::FRONT;
        CypherTileDocumentBridge document;
        NewMap( document );
        QString error;
        REQUIRE( document.beginEdit( QStringLiteral( "Sparse fixture" ), &error ) );
        for ( const int horizontal : { 1, 3, 5 } )
            REQUIRE( document.paintCell( front ? tile_map_grid_coord_t{ horizontal, 1 } : tile_map_grid_coord_t{ 1, horizontal },
                { 0, 1, 0 }, &error ) );
        REQUIRE( document.commitEdit( &error ) );
        CypherTileOrthoView view( plane );
        view.resize( 800, 360 );
        view.setDocumentBridge( &document );
        std::array cells{ tile_map_grid_coord_t{ 1, 1 }, front ? tile_map_grid_coord_t{ 5, 1 } : tile_map_grid_coord_t{ 1, 5 } };
        view.setSelectedCells( cells );
        const QImage image = Render( view );
        auto cellArea = [&]( int horizontal ) {
            return QRectF( Project( view, horizontal * 2, 2 ), Project( view, ( horizontal + 1 ) * 2, -TILE_MAP_DEFAULT_FLOOR_THICKNESS ) )
                .normalized().toAlignedRect().adjusted( -3, -3, 3, 3 );
        };
        const auto color = tile_editor_preferences_t{}.selectionColor;
        CHECK( ColoredPixels( image, cellArea( 1 ), color ) > 40 );
        CHECK( ColoredPixels( image, cellArea( 5 ), color ) > 40 );
        CHECK( ColoredPixels( image, cellArea( 3 ), color ) == 0 );
    }
}

TEST_CASE( "Orthographic marquee deduplicates owner cells and click forwards modifier and miss semantics", "[TileEditor][MultiSelection]" )
{
    EnsureMultiViewApplication();
    CypherTileDocumentBridge document;
    NewMap( document );
    QString error;
    REQUIRE( document.beginEdit( QStringLiteral( "Marquee fixture" ), &error ) );
    REQUIRE( document.paintCell( { 1, 1 }, { 0, 1, 0 }, &error ) );
    REQUIRE( document.paintCell( { 3, 1 }, { 0, 1, 0 }, &error ) );
    REQUIRE( document.paintCell( { 5, 1 }, { 0, 1, 0 }, &error ) );
    REQUIRE( document.commitEdit( &error ) );
    CypherTileOrthoView view( tile_editor_ortho_plane_t::FRONT );
    view.resize( 800, 360 );
    view.setDocumentBridge( &document );
    Render( view );
    int clicks = 0, marquees = 0;
    bool hit = false;
    Qt::KeyboardModifiers modifiers{};
    std::vector<tile_map_grid_coord_t> picked;
    view.setMultiSelectionCallback( [&]( bool found, tile_map_grid_coord_t cell, Qt::KeyboardModifiers keys ) {
        ++clicks; hit = found; modifiers = keys; picked = { cell };
    } );
    view.setSelectionCellsCallback( [&]( const auto &cells, Qt::KeyboardModifiers keys ) {
        ++marquees; picked = cells; modifiers = keys;
    } );
    const auto point = Project( view, 3, 1 );
    Mouse( view, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::ControlModifier );
    CHECK( clicks == 0 );
    Mouse( view, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::ControlModifier );
    CHECK( clicks == 1 );
    CHECK( hit );
    CHECK( modifiers == Qt::ControlModifier );
    CHECK( HasCell( picked, 1, 1 ) );
    Mouse( view, QEvent::MouseButtonPress, { 5, 5 }, Qt::LeftButton, Qt::AltModifier );
    Mouse( view, QEvent::MouseButtonRelease, { 5, 5 }, Qt::LeftButton, Qt::AltModifier );
    CHECK_FALSE( hit );
    CHECK( modifiers == Qt::AltModifier );
    const auto first = Project( view, 1.8, 2.2 );
    const auto last = Project( view, 8.2, -0.3 );
    Mouse( view, QEvent::MouseButtonPress, first, Qt::LeftButton, Qt::ShiftModifier );
    Mouse( view, QEvent::MouseMove, last, Qt::LeftButton, Qt::ShiftModifier );
    Mouse( view, QEvent::MouseButtonRelease, last, Qt::LeftButton, Qt::ShiftModifier );
    CHECK( marquees == 1 );
    CHECK( modifiers == Qt::ShiftModifier );
    REQUIRE( picked.size() == 2 );
    CHECK( HasCell( picked, 1, 1 ) );
    CHECK( HasCell( picked, 3, 1 ) );
}

TEST_CASE( "Interrupted orthographic marquees do not publish selection and navigation remains separate", "[TileEditor][MultiSelection]" )
{
    EnsureMultiViewApplication();
    CypherTileOrthoView view( tile_editor_ortho_plane_t::SIDE );
    int callbacks = 0;
    view.setMultiSelectionCallback( [&]( bool, auto, auto ) { ++callbacks; } );
    view.setSelectionCellsCallback( [&]( const auto &, auto ) { ++callbacks; } );
    for ( auto interruption : { QEvent::FocusOut, QEvent::Hide, QEvent::WindowDeactivate, QEvent::UngrabMouse } ) {
        Mouse( view, QEvent::MouseButtonPress, { 20, 20 }, Qt::LeftButton );
        Mouse( view, QEvent::MouseMove, { 80, 80 }, Qt::LeftButton );
        Interrupt( view, interruption );
        Mouse( view, QEvent::MouseButtonRelease, { 80, 80 }, Qt::LeftButton );
    }
    Mouse( view, QEvent::MouseButtonPress, { 20, 20 }, Qt::LeftButton );
    Mouse( view, QEvent::MouseMove, { 80, 80 }, Qt::LeftButton );
    Key( view, QEvent::KeyPress, Qt::Key_Escape );
    Mouse( view, QEvent::MouseButtonRelease, { 80, 80 }, Qt::LeftButton );
    const auto origin = view.viewOrigin();
    Mouse( view, QEvent::MouseButtonPress, { 20, 20 }, Qt::MiddleButton );
    Mouse( view, QEvent::MouseMove, { 40, 45 }, Qt::MiddleButton );
    Mouse( view, QEvent::MouseButtonRelease, { 40, 45 }, Qt::MiddleButton );
    CHECK( view.viewOrigin() == origin + QPointF( 20, 25 ) );
    CHECK( callbacks == 0 );
}

TEST_CASE( "3D sparse framing excludes elevated unselected geometry inside the bounding rectangle", "[TileEditor][MultiSelection]" )
{
    EnsureMultiViewApplication();
    CypherTileDocumentBridge document;
    NewMap( document );
    QString error;
    REQUIRE( document.beginEdit( QStringLiteral( "Framing fixture" ), &error ) );
    REQUIRE( document.paintCell( { 1, 1 }, { 0, 1, 0 }, &error ) );
    REQUIRE( document.paintCell( { 2, 2 }, { 12, 1, 0 }, &error ) );
    REQUIRE( document.paintCell( { 3, 3 }, { 0, 1, 0 }, &error ) );
    REQUIRE( document.commitEdit( &error ) );
    CypherTileRenderViewport view;
    view.resize( 640, 480 );
    view.setDocumentBridge( &document );
    const std::array cells{ tile_map_grid_coord_t{ 1, 1 }, tile_map_grid_coord_t{ 3, 3 } };
    view.setSelectedCells( cells );
    auto expected = view.camera();
    CypherTileCamera_FrameBounds( expected, { 2, 2, -TILE_MAP_DEFAULT_FLOOR_THICKNESS }, { 8, 8, 2 }, 640.0f / 480.0f );
    view.frameSelection();
    CHECK( view.camera().orbitDistance == Catch::Approx( expected.orbitDistance ) );
    CHECK( math::Vec3_NearlyEquals( view.camera().position, expected.position, 0.001f, 0.00001f ) );
    CHECK( view.selectedCells().size() == 2 );
}

TEST_CASE( "Front and Side paint floor levels and stair brushes in fixed construction layers with one undo", "[TileEditor][ViewAuthoring]" )
{
    EnsureMultiViewApplication();
    for ( auto plane : { tile_editor_ortho_plane_t::FRONT, tile_editor_ortho_plane_t::SIDE } ) {
        const bool front = plane == tile_editor_ortho_plane_t::FRONT;
        CypherTileDocumentBridge document;
        NewMap( document );
        CypherTileOrthoView view( plane );
        view.resize( 800, 500 );
        view.setDocumentBridge( &document );
        view.setConstructionCell( { 2, 3 } );
        tile_map_paint_t brush{ 0, 2, 6, tile_map_cell_shape_t::STAIRS_EAST, 6 };
        view.setPaint( brush );
        view.setTool( tile_canvas_tool_t::PAINT );
        Render( view );
        int commits = 0;
        view.setChangedCallback( [&] { ++commits; } );
        Mouse( view, QEvent::MouseButtonPress, Project( view, 3, 4 ), Qt::LeftButton );
        Mouse( view, QEvent::MouseMove, Project( view, 7, 0 ), Qt::LeftButton );
        Mouse( view, QEvent::MouseButtonRelease, Project( view, 7, 0 ), Qt::LeftButton );
        CHECK( commits == 1 );
        for ( int horizontal = 1; horizontal <= 3; ++horizontal ) {
            const tile_map_grid_coord_t cell = front ? tile_map_grid_coord_t{ horizontal, 3 } : tile_map_grid_coord_t{ 2, horizontal };
            const auto *painted = CypherTileMapDocument_CellAt( document.document(), cell );
            REQUIRE( painted != nullptr );
            CHECK( ( painted->flags & TILE_MAP_CELL_FLAG_FLOOR ) != 0 );
            CHECK( painted->nFloorLevel == 2 );
            CHECK( painted->shape == tile_map_cell_shape_t::STAIRS_EAST );
            CHECK( painted->nStairSteps == 6 );
            CHECK( painted->nMaterialSlot == 6 );
        }
        QString error;
        REQUIRE( document.undo( &error ) );
        CHECK_FALSE( document.canUndo() );
        CHECK_FALSE( CypherTileMapDocument_CellHasFloor( document.document(), front ? tile_map_grid_coord_t{ 1, 3 } : tile_map_grid_coord_t{ 2, 1 } ) );
        REQUIRE( document.redo( &error ) );
        view.refreshDocument();
        view.setTool( tile_canvas_tool_t::ERASE );
        Mouse( view, QEvent::MouseButtonPress, Project( view, 3, 4 ), Qt::LeftButton );
        Mouse( view, QEvent::MouseButtonRelease, Project( view, 3, 4 ), Qt::LeftButton );
        CHECK_FALSE( CypherTileMapDocument_CellHasFloor( document.document(), front ? tile_map_grid_coord_t{ 1, 3 } : tile_map_grid_coord_t{ 2, 1 } ) );
        REQUIRE( document.undo( &error ) );
        CHECK( CypherTileMapDocument_CellHasFloor( document.document(), front ? tile_map_grid_coord_t{ 1, 3 } : tile_map_grid_coord_t{ 2, 1 } ) );
    }
}

TEST_CASE( "Orthographic rectangle is a single elevation run and interrupted edits roll back", "[TileEditor][ViewAuthoring]" )
{
    EnsureMultiViewApplication();
    CypherTileDocumentBridge document;
    NewMap( document );
    CypherTileOrthoView view( tile_editor_ortho_plane_t::FRONT );
    view.resize( 800, 500 );
    view.setDocumentBridge( &document );
    view.setConstructionCell( { 0, 2 } );
    view.setTool( tile_canvas_tool_t::RECTANGLE );
    Render( view );
    Mouse( view, QEvent::MouseButtonPress, Project( view, 3, 2 ), Qt::LeftButton );
    Mouse( view, QEvent::MouseMove, Project( view, 9, 6 ), Qt::LeftButton );
    CHECK_FALSE( CypherTileMapDocument_CellHasFloor( document.document(), { 1, 2 } ) );
    Mouse( view, QEvent::MouseButtonRelease, Project( view, 9, 6 ), Qt::LeftButton );
    for ( int x = 1; x <= 4; ++x ) {
        const auto *cell = CypherTileMapDocument_CellAt( document.document(), { x, 2 } );
        REQUIRE( cell != nullptr );
        CHECK( ( cell->flags & TILE_MAP_CELL_FLAG_FLOOR ) != 0 );
        CHECK( cell->nFloorLevel == 1 );
    }
    view.setTool( tile_canvas_tool_t::PAINT );
    for ( auto interruption : { QEvent::FocusOut, QEvent::Hide, QEvent::WindowDeactivate } ) {
        Mouse( view, QEvent::MouseButtonPress, Project( view, 13, 4 ), Qt::LeftButton );
        REQUIRE( CypherTileMapDocument_CellHasFloor( document.document(), { 6, 2 } ) );
        Interrupt( view, interruption );
        CHECK_FALSE( CypherTileMapDocument_CellHasFloor( document.document(), { 6, 2 } ) );
    }
    Mouse( view, QEvent::MouseButtonPress, Project( view, 13, 4 ), Qt::LeftButton );
    Key( view, QEvent::KeyPress, Qt::Key_Escape );
    CHECK_FALSE( CypherTileMapDocument_CellHasFloor( document.document(), { 6, 2 } ) );
}

TEST_CASE( "Front and Side expose their fixed hidden-axis construction slice even with metrics disabled", "[TileEditor][ViewAuthoring][Readability]" )
{
    EnsureMultiViewApplication();
    for ( auto plane : { tile_editor_ortho_plane_t::FRONT, tile_editor_ortho_plane_t::SIDE } ) {
        const bool front = plane == tile_editor_ortho_plane_t::FRONT;
        CypherTileDocumentBridge document;
        NewMap( document );
        CypherTileOrthoView view( plane );
        view.resize( 640, 360 );
        tile_editor_preferences_t preferences;
        preferences.showViewMetrics = false;
        preferences.showAuthoringFooter = true;
        preferences.showCoordinateRulers = false;
        view.setPreferences( preferences );
        view.setDocumentBridge( &document );
        view.setConstructionCell( { 4, 7 } );
        view.setTool( tile_canvas_tool_t::PAINT );
        Mouse( view, QEvent::MouseMove, Project( view, 5, 4 ), Qt::NoButton );

        const QString slice = view.property( "constructionSlice" ).toString();
        CHECK( slice.contains( front ? QStringLiteral( "FIXED Y = 7" )
                                    : QStringLiteral( "FIXED X = 4" ) ) );
        CHECK( view.toolTip().contains( front ? QStringLiteral( "hidden Y coordinate fixed at 7" )
                                             : QStringLiteral( "hidden X coordinate fixed at 4" ) ) );
        const QImage image = Render( view );
        const QColor expectedStripe = front ? preferences.axisYColor : preferences.axisXColor;
        CHECK( image.pixelColor( 1, image.height() - 2 ) == expectedStripe );
        const QRect axisIndicator( image.width() - 92, 0, 92, 100 );
        const QColor horizontalAxis = front ? preferences.axisXColor : preferences.axisYColor;
        const QColor hiddenAxis = front ? preferences.axisYColor : preferences.axisXColor;
        CHECK( ColoredPixels( image, axisIndicator, horizontalAxis ) > 0 );
        CHECK( ColoredPixels( image, axisIndicator, preferences.axisZColor ) > 0 );
        CHECK( ColoredPixels( image, axisIndicator, hiddenAxis ) > 0 );
    }
}

TEST_CASE( "Projected Fill explains why topology editing remains in Top view", "[TileEditor][ViewAuthoring]" )
{
    EnsureMultiViewApplication();
    CypherTileDocumentBridge document;
    NewMap( document );
    CypherTileOrthoView view( tile_editor_ortho_plane_t::FRONT );
    view.resize( 640, 360 );
    view.setDocumentBridge( &document );
    view.setConstructionCell( { 0, 5 } );
    view.setTool( tile_canvas_tool_t::FILL );
    QString status;
    bool statusError = true;
    view.setStatusCallback( [&]( const QString &message, bool error ) {
        status = message;
        statusError = error;
    } );
    Mouse( view, QEvent::MouseButtonPress, Project( view, 5, 0 ), Qt::LeftButton );
    CHECK_FALSE( statusError );
    CHECK( status.contains( QStringLiteral( "Top view" ) ) );
    CHECK_FALSE( document.canUndo() );
    CHECK_FALSE( CypherTileMapDocument_CellHasFloor( document.document(), { 2, 5 } ) );
}

TEST_CASE( "Front view places spawn and toggles only valid exposed-edge doors on its fixed row", "[TileEditor][ViewAuthoring]" )
{
    EnsureMultiViewApplication();
    CypherTileDocumentBridge document;
    NewMap( document );
    CypherTileOrthoView view( tile_editor_ortho_plane_t::FRONT );
    view.resize( 640, 360 );
    view.setDocumentBridge( &document );
    view.setConstructionCell( { 0, 4 } );
    view.setPaint( { 0, 2, 3 } );
    Render( view );

    const QPointF authoredCell = Project( view, 7, 0 ); // X cell 3 at fixed Y row 4.
    view.setTool( tile_canvas_tool_t::DOOR );
    view.setDoorSide( tile_map_marker_side_t::NORTH );
    QString status;
    bool statusError = false;
    view.setStatusCallback( [&]( const QString &message, bool error ) {
        status = message;
        statusError = error;
    } );
    Mouse( view, QEvent::MouseButtonPress, authoredCell, Qt::LeftButton );
    CHECK( statusError );
    CHECK( status.contains( QStringLiteral( "exposed edge" ) ) );
    CHECK( CypherTileMapDocument_DoorAt( document.document(), { 3, 4 }, tile_map_marker_side_t::NORTH ) == nullptr );

    view.setTool( tile_canvas_tool_t::PAINT );
    Mouse( view, QEvent::MouseButtonPress, authoredCell, Qt::LeftButton );
    Mouse( view, QEvent::MouseButtonRelease, authoredCell, Qt::LeftButton );
    REQUIRE( CypherTileMapDocument_CellHasFloor( document.document(), { 3, 4 } ) );

    view.setTool( tile_canvas_tool_t::DOOR );
    Mouse( view, QEvent::MouseButtonPress, authoredCell, Qt::LeftButton );
    REQUIRE( CypherTileMapDocument_DoorAt( document.document(), { 3, 4 }, tile_map_marker_side_t::NORTH ) != nullptr );
    Mouse( view, QEvent::MouseButtonPress, authoredCell, Qt::LeftButton );
    CHECK( CypherTileMapDocument_DoorAt( document.document(), { 3, 4 }, tile_map_marker_side_t::NORTH ) == nullptr );

    view.setTool( tile_canvas_tool_t::PLAYER_SPAWN );
    Mouse( view, QEvent::MouseButtonPress, authoredCell, Qt::LeftButton );
    const auto *spawn = CypherTileMapDocument_PlayerSpawn( document.document() );
    REQUIRE( spawn != nullptr );
    CHECK( spawn->cell.x == 3 );
    CHECK( spawn->cell.y == 4 );
}

TEST_CASE( "3D construction rays reject parallel behind-camera and out-of-map intersections", "[TileEditor][ViewAuthoring]" )
{
    EnsureMultiViewApplication();
    CypherTileDocumentBridge document;
    NewMap( document );
    tile_camera_t camera;
    camera.position = { 3, 3, 10 };
    camera.pitchRadians = -1.57079632679f;
    camera.yawRadians = 0;
    tile_map_grid_coord_t cell{};
    REQUIRE( CypherTileRenderViewport_PickPlane( camera, 0, 0, 1, 0, *document.document(), cell ) );
    CHECK( cell.x == 1 );
    CHECK( cell.y == 1 );
    CHECK_FALSE( CypherTileRenderViewport_PickPlane( camera, 0, 0, 1, 20, *document.document(), cell ) );
    camera.pitchRadians = 0;
    CHECK_FALSE( CypherTileRenderViewport_PickPlane( camera, 0, 0, 1, 0, *document.document(), cell ) );
    camera.pitchRadians = -1.57079632679f;
    camera.position.x = -3;
    CHECK_FALSE( CypherTileRenderViewport_PickPlane( camera, 0, 0, 1, 0, *document.document(), cell ) );
}

TEST_CASE( "3D Paint creates a cell on empty construction plane while modifiers remain selection", "[TileEditor][ViewAuthoring][MultiSelection]" )
{
    EnsureMultiViewApplication();
    CypherTileDocumentBridge document;
    NewMap( document );
    CypherTileRenderViewport view;
    view.resize( 640, 480 );
    view.setDocumentBridge( &document );
    view.setTool( tile_canvas_tool_t::PAINT );
    view.setPaint( { 0, 1, 5 } );
    int commits = 0, picks = 0;
    bool pickedHit = false;
    Qt::KeyboardModifiers pickedModifiers{};
    view.setChangedCallback( [&] { ++commits; } );
    view.setMultiSelectionCallback( [&]( bool hit, auto, auto modifiers ) { ++picks; pickedHit = hit; pickedModifiers = modifiers; } );
    QPointF target;
    tile_map_grid_coord_t targetCell;
    bool found = false;
    for ( int iy = -8; iy <= 8 && !found; ++iy ) {
        for ( int ix = -8; ix <= 8 && !found; ++ix ) {
            const float x = ix * 0.1f, y = iy * 0.1f;
            if ( CypherTileRenderViewport_PickPlane( view.camera(), x, y, 640.0f / 480.0f, 0, *document.document(), targetCell ) ) {
                target = PixelForRay( view, x, y );
                found = true;
            }
        }
    }
    REQUIRE( found );
    Mouse( view, QEvent::MouseButtonPress, target, Qt::LeftButton, Qt::ControlModifier );
    CHECK( picks == 1 );
    CHECK( pickedModifiers == Qt::ControlModifier );
    CHECK_FALSE( pickedHit );
    CHECK( commits == 0 );
    Mouse( view, QEvent::MouseButtonPress, target, Qt::LeftButton );
    CHECK( commits == 1 );
    REQUIRE( CypherTileMapDocument_CellHasFloor( document.document(), targetCell ) );
    CHECK( CypherTileMapDocument_CellAt( document.document(), targetCell )->nMaterialSlot == 5 );
    Mouse( view, QEvent::MouseButtonPress, target, Qt::LeftButton, Qt::ShiftModifier );
    CHECK( pickedHit );
    CHECK( pickedModifiers == Qt::ShiftModifier );
    CHECK( commits == 1 );
    view.setTool( tile_canvas_tool_t::ERASE );
    Mouse( view, QEvent::MouseButtonPress, target, Qt::LeftButton );
    CHECK( commits == 2 );
    CHECK_FALSE( CypherTileMapDocument_CellHasFloor( document.document(), targetCell ) );
    QString error;
    REQUIRE( document.undo( &error ) );
    CHECK( CypherTileMapDocument_CellHasFloor( document.document(), targetCell ) );
    REQUIRE( document.undo( &error ) );
    CHECK_FALSE( CypherTileMapDocument_CellHasFloor( document.document(), targetCell ) );
    const auto mode = view.camera().mode;
    Mouse( view, QEvent::MouseButtonPress, target, Qt::RightButton, Qt::AltModifier );
    CHECK( view.isNavigating() );
    CHECK( view.camera().mode == tile_camera_mode_t::ORBIT );
    Mouse( view, QEvent::MouseButtonRelease, target, Qt::RightButton, Qt::AltModifier );
    CHECK( view.camera().mode == mode );
}

TEST_CASE( "3D Place Piece publishes an empty-plane target while modifiers remain selection",
           "[TileEditor][ViewAuthoring][Pieces][MultiSelection]" )
{
    EnsureMultiViewApplication();
    CypherTileDocumentBridge document;
    NewMap( document );
    CypherTileRenderViewport view;
    view.resize( 640, 480 );
    view.setDocumentBridge( &document );
    view.setPaint( { 0, 1, 5 } );
    view.setStampLabel( QStringLiteral( "Small Room" ) );
    view.setTool( tile_canvas_tool_t::STAMP );

    int placements = 0;
    int selections = 0;
    tile_map_grid_coord_t placedCell{ -1, -1 };
    bool selectedHit = true;
    Qt::KeyboardModifiers selectedModifiers{};
    view.setStampCallback( [&]( tile_map_grid_coord_t cell ) {
        ++placements;
        placedCell = cell;
    } );
    view.setMultiSelectionCallback(
        [&]( bool hit, auto, auto modifiers ) {
            ++selections;
            selectedHit = hit;
            selectedModifiers = modifiers;
        } );

    QPointF target;
    tile_map_grid_coord_t targetCell{};
    bool found = false;
    for ( int iy = -8; iy <= 8 && !found; ++iy ) {
        for ( int ix = -8; ix <= 8 && !found; ++ix ) {
            const float x = ix * 0.1f;
            const float y = iy * 0.1f;
            if ( CypherTileRenderViewport_PickPlane(
                     view.camera(), x, y, 640.0f / 480.0f, 0.0f,
                     *document.document(), targetCell ) ) {
                target = PixelForRay( view, x, y );
                found = true;
            }
        }
    }
    REQUIRE( found );

    Mouse( view, QEvent::MouseButtonPress, target, Qt::LeftButton );
    CHECK( placements == 1 );
    CHECK( placedCell.x == targetCell.x );
    CHECK( placedCell.y == targetCell.y );
    CHECK( selections == 0 );
    CHECK_FALSE( CypherTileMapDocument_CellHasFloor(
        document.document(), targetCell ) );
    CHECK( view.toolTip().contains( QStringLiteral( "Small Room" ) ) );
    CHECK( view.toolTip().contains( QStringLiteral( "WASD" ) ) );
    CHECK( view.toolTip().contains( QStringLiteral( "Page Up/Down" ) ) );

    Mouse( view, QEvent::MouseButtonPress, target, Qt::LeftButton,
           Qt::ControlModifier );
    CHECK( placements == 1 );
    CHECK( selections == 1 );
    CHECK_FALSE( selectedHit );
    CHECK( selectedModifiers == Qt::ControlModifier );
}
