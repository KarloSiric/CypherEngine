//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Exact cell selection, modifiers, sparse highlighting and move gestures.
//////////////////////////////////////////////////////////////////////////
#include "CypherTileCanvas.h"

#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QFocusEvent>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>

#include <algorithm>
#include <array>

using namespace cypher::tools::tile_editor;

namespace
{
void MultiSelectionApplication()
{
    if ( QApplication::instance() ) return;
    qputenv( "QT_QPA_PLATFORM", "offscreen" );
    static int argc = 1;
    static char name[] = "TileMultiSelectionTests";
    static char *argv[]{ name, nullptr };
    static QApplication application( argc, argv );
}

struct MultiSelectionFixture {
    CypherTileDocumentBridge bridge;
    CypherTileCanvas canvas;

    MultiSelectionFixture()
    {
        QString error;
        REQUIRE( bridge.newDocument( { 12u, 10u, 2.0f, 2.0f }, &error ) );
        bridge.markSaved();
        canvas.resize( 600, 480 );
        canvas.setDocumentBridge( &bridge );
        canvas.setTool( tile_canvas_tool_t::SELECT );
    }

    QPointF center( int x, int y ) const
    {
        return canvas.viewOrigin() + QPointF( ( x + 0.5 ) * canvas.zoomFactor(),
            ( y + 0.5 ) * canvas.zoomFactor() );
    }

    void paint( int x, int y )
    {
        QString error;
        REQUIRE( bridge.beginEdit( "Fixture floor", &error ) );
        REQUIRE( bridge.paintCell( { x, y }, { 0, 1, 0 }, &error ) );
        REQUIRE( bridge.commitEdit( &error ) );
        bridge.markSaved();
        canvas.refreshDocument();
    }

    void mouse( QEvent::Type type, QPointF point, Qt::MouseButton button,
        Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers = Qt::NoModifier )
    {
        QMouseEvent event( type, point, point, button, buttons, modifiers );
        QApplication::sendEvent( &canvas, &event );
    }

    void click( int x, int y, Qt::KeyboardModifiers modifiers = Qt::NoModifier )
    {
        mouse( QEvent::MouseButtonPress, center( x, y ), Qt::LeftButton, Qt::LeftButton, modifiers );
        mouse( QEvent::MouseButtonRelease, center( x, y ), Qt::LeftButton, Qt::NoButton, modifiers );
    }

    void drag( int x0, int y0, int x1, int y1, Qt::KeyboardModifiers modifiers )
    {
        mouse( QEvent::MouseButtonPress, center( x0, y0 ), Qt::LeftButton, Qt::LeftButton, modifiers );
        mouse( QEvent::MouseMove, center( x1, y1 ), Qt::NoButton, Qt::LeftButton, modifiers );
        mouse( QEvent::MouseButtonRelease, center( x1, y1 ), Qt::LeftButton, Qt::NoButton, modifiers );
    }

    bool selected( int x, int y ) const
    {
        return std::any_of( canvas.selectedCells().begin(), canvas.selectedCells().end(),
            [=]( auto cell ) { return cell.x == x && cell.y == y; } );
    }
};
} // namespace

TEST_CASE( "Exact canvas selections deduplicate clip and retain holes through refresh",
    "[TileEditor][Selection][MultiSelection]" )
{
    MultiSelectionApplication();
    MultiSelectionFixture fixture;
    const std::array<tile_map_grid_coord_t, 6> input{{ { 5, 4 }, { 4, 2 }, { 2, 4 }, { 4, 2 }, { -1, 0 }, { 12, 9 } }};
    fixture.canvas.setSelectedCells( input );
    REQUIRE( fixture.canvas.selectedCells().size() == 3 );
    CHECK( fixture.canvas.selectedCell().x == 4 );
    CHECK( fixture.canvas.selectedCell().y == 2 );
    CHECK( fixture.canvas.selectionRect().x == 2 );
    CHECK( fixture.canvas.selectionRect().y == 2 );
    CHECK( fixture.canvas.selectionRect().nWidth == 4 );
    CHECK( fixture.canvas.selectionRect().nHeight == 3 );
    CHECK_FALSE( fixture.selected( 2, 2 ) );
    fixture.canvas.refreshDocument();
    CHECK( fixture.canvas.selectedCells().size() == 3 );
    CHECK_FALSE( fixture.selected( 3, 3 ) );
    fixture.canvas.setSelectionRect( fixture.canvas.selectionRect() );
    CHECK( fixture.canvas.selectedCells().size() == 12 );
    fixture.canvas.clearSelection();
    CHECK( fixture.canvas.selectedCells().empty() );
    CHECK_FALSE( fixture.canvas.hasSelection() );
}

TEST_CASE( "Modifier clicks add toggle and subtract cells without changing map history",
    "[TileEditor][Selection][MultiSelection]" )
{
    MultiSelectionApplication();
    MultiSelectionFixture fixture;
    fixture.click( 2, 2 );
    fixture.click( 5, 3, Qt::ShiftModifier );
    REQUIRE( fixture.canvas.selectedCells().size() == 2 );
    CHECK( fixture.selected( 2, 2 ) );
    CHECK( fixture.selected( 5, 3 ) );
    CHECK_FALSE( fixture.selected( 3, 2 ) );
    fixture.click( 2, 2, Qt::ControlModifier );
    CHECK_FALSE( fixture.selected( 2, 2 ) );
    fixture.click( 1, 1, Qt::ControlModifier );
    CHECK( fixture.selected( 1, 1 ) );
    fixture.click( 5, 3, Qt::AltModifier );
    REQUIRE( fixture.canvas.selectedCells().size() == 1 );
    CHECK( fixture.selected( 1, 1 ) );
    fixture.click( 1, 1, Qt::AltModifier );
    CHECK_FALSE( fixture.canvas.hasSelection() );
    CHECK_FALSE( fixture.bridge.isDirty() );
    CHECK_FALSE( fixture.bridge.canUndo() );
}

TEST_CASE( "Modifier marquee uses set operations and cancellation preserves committed cells",
    "[TileEditor][Selection][MultiSelection]" )
{
    MultiSelectionApplication();
    MultiSelectionFixture fixture;
    fixture.canvas.setSelectionRect( { 2, 2, 2, 2 } );
    fixture.drag( 3, 3, 4, 4, Qt::ShiftModifier );
    REQUIRE( fixture.canvas.selectedCells().size() == 7 );
    fixture.drag( 3, 2, 4, 3, Qt::ControlModifier );
    REQUIRE( fixture.canvas.selectedCells().size() == 5 );
    CHECK( fixture.selected( 4, 2 ) );
    CHECK_FALSE( fixture.selected( 3, 2 ) );
    fixture.drag( 2, 2, 2, 4, Qt::AltModifier );
    REQUIRE( fixture.canvas.selectedCells().size() == 3 );
    const auto before = fixture.canvas.selectedCells();
    fixture.mouse( QEvent::MouseButtonPress, fixture.center( 1, 1 ), Qt::LeftButton, Qt::LeftButton );
    fixture.mouse( QEvent::MouseMove, fixture.center( 6, 6 ), Qt::NoButton, Qt::LeftButton );
    QKeyEvent escape( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier );
    QApplication::sendEvent( &fixture.canvas, &escape );
    fixture.mouse( QEvent::MouseButtonRelease, fixture.center( 6, 6 ), Qt::LeftButton, Qt::NoButton );
    CHECK( fixture.canvas.selectedCells().size() == before.size() );
    for ( const auto cell : before ) CHECK( fixture.selected( cell.x, cell.y ) );
    CHECK_FALSE( fixture.bridge.isDirty() );
}

TEST_CASE( "Shared modifier API preserves selection on empty additive picks and replaces on plain picks",
    "[TileEditor][Selection][MultiSelection]" )
{
    MultiSelectionApplication();
    MultiSelectionFixture fixture;
    fixture.canvas.selectCell( { 2, 2 } );
    fixture.canvas.modifySelectedCells( {}, Qt::ShiftModifier );
    CHECK( fixture.selected( 2, 2 ) );
    const std::array<tile_map_grid_coord_t, 2> extra{{ { 2, 2 }, { 5, 4 } }};
    fixture.canvas.modifySelectedCells( extra, Qt::ControlModifier );
    REQUIRE( fixture.canvas.selectedCells().size() == 1 );
    CHECK( fixture.selected( 5, 4 ) );
    fixture.canvas.modifySelectedCells( {}, Qt::NoModifier );
    CHECK_FALSE( fixture.canvas.hasSelection() );
}

TEST_CASE( "Select all occupied excludes empty grid and includes authored marker cells",
    "[TileEditor][Selection][MultiSelection]" )
{
    MultiSelectionApplication();
    MultiSelectionFixture fixture;
    fixture.paint( 2, 2 );
    fixture.paint( 5, 4 );
    QString error;
    REQUIRE( fixture.bridge.beginEdit( "Fixture marker", &error ) );
    REQUIRE( fixture.bridge.placePlayerSpawn( { 8, 7 }, 0.0f, &error ) );
    REQUIRE( fixture.bridge.commitEdit( &error ) );
    fixture.bridge.markSaved();
    fixture.canvas.selectAllOccupied();
    REQUIRE( fixture.canvas.selectedCells().size() == 3 );
    CHECK( fixture.selected( 2, 2 ) );
    CHECK( fixture.selected( 5, 4 ) );
    CHECK( fixture.selected( 8, 7 ) );
    CHECK_FALSE( fixture.selected( 3, 3 ) );
    CHECK_FALSE( fixture.bridge.isDirty() );
}

TEST_CASE( "Dragging an occupied selection previews then requests one snapped move on release",
    "[TileEditor][Selection][MultiSelection]" )
{
    MultiSelectionApplication();
    MultiSelectionFixture fixture;
    fixture.paint( 2, 2 );
    fixture.paint( 4, 3 );
    const std::array<tile_map_grid_coord_t, 2> cells{{ { 2, 2 }, { 4, 3 } }};
    fixture.canvas.setSelectedCells( cells );
    int calls = 0, dx = 0, dy = 0;
    fixture.canvas.setMoveSelectionCallback( [&]( int x, int y, bool copy ) {
        ++calls; dx = x; dy = y; CHECK_FALSE( copy );
    } );
    fixture.click( 2, 2 );
    CHECK( calls == 0 );
    CHECK( fixture.canvas.selectedCells().size() == 2 );
    fixture.mouse( QEvent::MouseButtonPress, fixture.center( 2, 2 ), Qt::LeftButton, Qt::LeftButton );
    fixture.mouse( QEvent::MouseMove, fixture.center( 5, 4 ), Qt::NoButton, Qt::LeftButton );
    CHECK( calls == 0 );
    CHECK_FALSE( fixture.bridge.isDirty() );
    fixture.mouse( QEvent::MouseButtonRelease, fixture.center( 5, 4 ), Qt::LeftButton, Qt::NoButton );
    CHECK( calls == 1 );
    CHECK( dx == 3 ); CHECK( dy == 2 );
    CHECK( fixture.canvas.selectedCells().size() == 2 );
    CHECK_FALSE( fixture.bridge.isDirty() );
}

TEST_CASE( "Escape and lost focus cancel a pending selection move without issuing transforms",
    "[TileEditor][Selection][MultiSelection]" )
{
    MultiSelectionApplication();
    for ( const bool loseFocus : { false, true } ) {
        MultiSelectionFixture fixture;
        fixture.paint( 2, 2 );
        fixture.canvas.selectCell( { 2, 2 } );
        int calls = 0;
        fixture.canvas.setMoveSelectionCallback( [&]( int, int, bool ) { ++calls; } );
        fixture.mouse( QEvent::MouseButtonPress, fixture.center( 2, 2 ), Qt::LeftButton, Qt::LeftButton );
        fixture.mouse( QEvent::MouseMove, fixture.center( 6, 5 ), Qt::NoButton, Qt::LeftButton );
        if ( loseFocus ) {
            QFocusEvent event( QEvent::FocusOut );
            QApplication::sendEvent( &fixture.canvas, &event );
        } else {
            QKeyEvent event( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier );
            QApplication::sendEvent( &fixture.canvas, &event );
        }
        fixture.mouse( QEvent::MouseButtonRelease, fixture.center( 6, 5 ), Qt::LeftButton, Qt::NoButton );
        CHECK( calls == 0 );
        CHECK( fixture.selected( 2, 2 ) );
        CHECK_FALSE( fixture.bridge.isDirty() );
    }
}

TEST_CASE( "Sparse selection highlights only selected cells and previews toggle removal",
    "[TileEditor][Selection][MultiSelection][Display]" )
{
    MultiSelectionApplication();
    MultiSelectionFixture fixture;
    tile_editor_preferences_t preferences;
    preferences.showGrid = false;
    preferences.showMarkers = false;
    preferences.showViewMetrics = false;
    preferences.showMaterialLabels = false;
    fixture.canvas.setPreferences( preferences );
    fixture.canvas.show();
    QApplication::processEvents();
    const auto empty = fixture.canvas.grab().toImage();
    const std::array<tile_map_grid_coord_t, 2> selected{{ { 2, 2 }, { 4, 2 } }};
    fixture.canvas.setSelectedCells( selected );
    const auto image = fixture.canvas.grab().toImage();
    CHECK( image.pixelColor( fixture.center( 2, 2 ).toPoint() ) != empty.pixelColor( fixture.center( 2, 2 ).toPoint() ) );
    CHECK( image.pixelColor( fixture.center( 3, 2 ).toPoint() ) == empty.pixelColor( fixture.center( 3, 2 ).toPoint() ) );
    fixture.mouse( QEvent::MouseButtonPress, fixture.center( 2, 2 ), Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier );
    const auto preview = fixture.canvas.grab().toImage();
    CHECK( preview.pixelColor( fixture.center( 2, 2 ).toPoint() ) == empty.pixelColor( fixture.center( 2, 2 ).toPoint() ) );
    CHECK( fixture.canvas.selectedCells().size() == 2 );
    fixture.mouse( QEvent::MouseButtonRelease, fixture.center( 2, 2 ), Qt::LeftButton, Qt::NoButton, Qt::ControlModifier );
    CHECK_FALSE( fixture.selected( 2, 2 ) );
    CHECK( fixture.selected( 4, 2 ) );
}
