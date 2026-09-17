//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Verifies marquee selection and safe navigation in the top view.
//////////////////////////////////////////////////////////////////////////
#include "CypherTileCanvas.h"
#include "Core/CypherTileMapMaterials.h"
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QFocusEvent>
#include <QHideEvent>
#include <QKeyEvent>
#include <QImage>
#include <QMouseEvent>
#include <cmath>
#include <limits>

using namespace cypher::tools::tile_editor;

namespace {
void EnsureSelectionApplication()
{
    if ( QApplication::instance() ) return;
    qputenv( "QT_QPA_PLATFORM", "offscreen" );
    static int argc = 1;
    static char name[] = "TileSelectionTests";
    static char *argv[]{ name, nullptr };
    static QApplication application( argc, argv );
}

struct SelectionFixture {
    CypherTileDocumentBridge document;
    CypherTileCanvas canvas;

    SelectionFixture()
    {
        tile_map_document_desc_t description{};
        description.nWidth = 12;
        description.nHeight = 10;
        QString error;
        REQUIRE( document.newDocument( description, &error ) );
        document.markSaved();
        canvas.resize( 600, 480 );
        canvas.setDocumentBridge( &document );
        canvas.setTool( tile_canvas_tool_t::SELECT );
    }

    QPointF center( int x, int y ) const
    {
        return canvas.viewOrigin() + QPointF( ( x + 0.5 ) * canvas.zoomFactor(),
            ( y + 0.5 ) * canvas.zoomFactor() );
    }
};

void Mouse( CypherTileCanvas &canvas, QEvent::Type type, QPointF point,
    Qt::MouseButton button, Qt::MouseButtons buttons )
{
    QMouseEvent event( type, point, point, button, buttons, Qt::NoModifier );
    QApplication::sendEvent( &canvas, &event );
}

void Key( CypherTileCanvas &canvas, QEvent::Type type, int key )
{
    QKeyEvent event( type, key, Qt::NoModifier );
    QApplication::sendEvent( &canvas, &event );
}

void CheckSelection( const CypherTileCanvas &canvas, int x, int y, unsigned width, unsigned height )
{
    REQUIRE( canvas.hasSelection() );
    const auto rectangle = canvas.selectionRect();
    CHECK( rectangle.x == x );
    CHECK( rectangle.y == y );
    CHECK( rectangle.nWidth == width );
    CHECK( rectangle.nHeight == height );
    CHECK( canvas.selectedCell().x == x );
    CHECK( canvas.selectedCell().y == y );
}
}

TEST_CASE( "Marquee selection commits an inclusive rectangle without editing the document", "[TileEditor][Selection]" )
{
    EnsureSelectionApplication();
    SelectionFixture fixture;
    int changes = 0;
    fixture.canvas.setSelectionCallback( [&]( bool, tile_map_grid_coord_t ) { ++changes; } );
    Mouse( fixture.canvas, QEvent::MouseButtonPress, fixture.center( 5, 6 ), Qt::LeftButton, Qt::LeftButton );
    Mouse( fixture.canvas, QEvent::MouseMove, fixture.center( 2, 3 ), Qt::NoButton, Qt::LeftButton );
    CHECK_FALSE( fixture.canvas.hasSelection() );
    CHECK( changes == 0 );
    Mouse( fixture.canvas, QEvent::MouseButtonRelease, fixture.center( 2, 3 ), Qt::LeftButton, Qt::NoButton );
    CheckSelection( fixture.canvas, 2, 3, 4, 4 );
    CHECK( changes == 1 );
    CHECK_FALSE( fixture.document.isDirty() );
    CHECK_FALSE( fixture.document.canUndo() );

    // A release without an intervening move still uses the release location.
    Mouse( fixture.canvas, QEvent::MouseButtonPress, fixture.center( 10, 8 ), Qt::LeftButton, Qt::LeftButton );
    Mouse( fixture.canvas, QEvent::MouseButtonRelease, fixture.center( 30, 30 ), Qt::LeftButton, Qt::NoButton );
    CheckSelection( fixture.canvas, 10, 8, 2, 2 );
    CHECK( changes == 2 );
    Mouse( fixture.canvas, QEvent::MouseButtonPress, fixture.center( 4, 5 ), Qt::LeftButton, Qt::LeftButton );
    Mouse( fixture.canvas, QEvent::MouseButtonRelease, fixture.center( 4, 5 ), Qt::LeftButton, Qt::NoButton );
    CheckSelection( fixture.canvas, 4, 5, 1, 1 );
}

TEST_CASE( "Escape cancels a pending marquee before clearing the previous selection", "[TileEditor][Selection]" )
{
    EnsureSelectionApplication();
    SelectionFixture fixture;
    fixture.canvas.setSelectionRect( { 2, 2, 3, 2 } );
    int changes = 0;
    fixture.canvas.setSelectionCallback( [&]( bool, tile_map_grid_coord_t ) { ++changes; } );
    Mouse( fixture.canvas, QEvent::MouseButtonPress, fixture.center( 6, 6 ), Qt::LeftButton, Qt::LeftButton );
    Mouse( fixture.canvas, QEvent::MouseMove, fixture.center( 8, 8 ), Qt::NoButton, Qt::LeftButton );
    Key( fixture.canvas, QEvent::KeyPress, Qt::Key_Escape );
    Mouse( fixture.canvas, QEvent::MouseButtonRelease, fixture.center( 8, 8 ), Qt::LeftButton, Qt::NoButton );
    CheckSelection( fixture.canvas, 2, 2, 3, 2 );
    CHECK( changes == 0 );
    Key( fixture.canvas, QEvent::KeyPress, Qt::Key_Escape );
    CHECK_FALSE( fixture.canvas.hasSelection() );
    CHECK( fixture.canvas.selectionRect().nWidth == 0 );
    CHECK( changes == 1 );
    fixture.canvas.clearSelection();
    CHECK( changes == 1 );
    CHECK_FALSE( fixture.document.isDirty() );
}

TEST_CASE( "Programmatic selections clamp safely and framing centers the whole region", "[TileEditor][Selection]" )
{
    EnsureSelectionApplication();
    SelectionFixture fixture;
    fixture.canvas.setSelectionRect( { -3, -2, 6, 5 } );
    CheckSelection( fixture.canvas, 0, 0, 3, 3 );
    fixture.canvas.setSelectionRect( { 8, 7, std::numeric_limits<unsigned>::max(), std::numeric_limits<unsigned>::max() } );
    CheckSelection( fixture.canvas, 8, 7, 4, 3 );
    fixture.canvas.fitSelection();
    const auto origin = fixture.canvas.viewOrigin();
    CHECK( std::abs( origin.x() + 10.0 * fixture.canvas.zoomFactor() - fixture.canvas.width() * 0.5 ) < 0.01 );
    CHECK( std::abs( origin.y() + 8.5 * fixture.canvas.zoomFactor() - fixture.canvas.height() * 0.5 ) < 0.01 );
    fixture.canvas.setSelectionRect( { 50, 50, 2, 2 } );
    CHECK_FALSE( fixture.canvas.hasSelection() );
    fixture.canvas.setSelectionRect( { 1, 1, 0, 2 } );
    CHECK_FALSE( fixture.canvas.hasSelection() );
}

TEST_CASE( "Top-view pan owns its initiating button and never paints", "[TileEditor][Navigation]" )
{
    EnsureSelectionApplication();
    for ( const auto button : { Qt::MiddleButton, Qt::RightButton, Qt::LeftButton } ) {
        SelectionFixture fixture;
        fixture.canvas.setTool( tile_canvas_tool_t::PAINT );
        if ( button == Qt::LeftButton ) Key( fixture.canvas, QEvent::KeyPress, Qt::Key_Space );
        const QPointF start = fixture.center( 3, 3 );
        const auto origin = fixture.canvas.viewOrigin();
        Mouse( fixture.canvas, QEvent::MouseButtonPress, start, button, button );
        REQUIRE( fixture.canvas.isPanning() );
        const auto other = button == Qt::LeftButton ? Qt::RightButton : Qt::LeftButton;
        Mouse( fixture.canvas, QEvent::MouseButtonPress, start, other, button | other );
        Mouse( fixture.canvas, QEvent::MouseButtonRelease, start, other, button );
        CHECK( fixture.canvas.isPanning() );
        Mouse( fixture.canvas, QEvent::MouseMove, start + QPointF( 20, -15 ), Qt::NoButton, button );
        CHECK( fixture.canvas.viewOrigin() == origin + QPointF( 20, -15 ) );
        Mouse( fixture.canvas, QEvent::MouseButtonRelease, start + QPointF( 20, -15 ), button, Qt::NoButton );
        CHECK_FALSE( fixture.canvas.isPanning() );
        CHECK_FALSE( fixture.document.isDirty() );
        CHECK_FALSE( fixture.document.canUndo() );
    }
}

TEST_CASE( "Stationary RMB opens Top context options while RMB drag keeps panning",
    "[TileEditor][Navigation][ContextMenu]" )
{
    EnsureSelectionApplication();
    SelectionFixture fixture;
    int contextMenus = 0;
    QPoint requestedPosition;
    fixture.canvas.setContextMenuCallback( [&]( const QPoint &globalPosition ) {
        ++contextMenus;
        requestedPosition = globalPosition;
    } );

    const QPointF start = fixture.center( 3, 3 );
    const auto initialOrigin = fixture.canvas.viewOrigin();
    Mouse( fixture.canvas, QEvent::MouseButtonPress,
           start, Qt::RightButton, Qt::RightButton );
    REQUIRE( fixture.canvas.isPanning() );
    Mouse( fixture.canvas, QEvent::MouseButtonRelease,
           start, Qt::RightButton, Qt::NoButton );
    CHECK_FALSE( fixture.canvas.isPanning() );
    CHECK( fixture.canvas.viewOrigin() == initialOrigin );
    CHECK( contextMenus == 0 );
    QApplication::processEvents();
    CHECK( contextMenus == 1 );
    CHECK( requestedPosition == start.toPoint() );

    const QPointF dragDelta(
        QApplication::startDragDistance() + 12,
        -( QApplication::startDragDistance() + 4 ) );
    Mouse( fixture.canvas, QEvent::MouseButtonPress,
           start, Qt::RightButton, Qt::RightButton );
    REQUIRE( fixture.canvas.isPanning() );
    Mouse( fixture.canvas, QEvent::MouseMove,
           start + dragDelta, Qt::NoButton, Qt::RightButton );
    CHECK( fixture.canvas.isPanning() );
    CHECK( fixture.canvas.viewOrigin() == initialOrigin + dragDelta );
    Mouse( fixture.canvas, QEvent::MouseButtonRelease,
           start + dragDelta, Qt::RightButton, Qt::NoButton );
    CHECK_FALSE( fixture.canvas.isPanning() );
    QApplication::processEvents();
    CHECK( contextMenus == 1 );
    CHECK_FALSE( fixture.document.isDirty() );
}

TEST_CASE( "Pan and pending edits reset on interruption without leaking input", "[TileEditor][Navigation]" )
{
    EnsureSelectionApplication();
    for ( const auto interruption : { QEvent::FocusOut, QEvent::Hide, QEvent::WindowDeactivate, QEvent::UngrabMouse } ) {
        SelectionFixture fixture;
        fixture.canvas.setTool( tile_canvas_tool_t::PAINT );
        const QPointF start = fixture.center( 3, 3 );
        Key( fixture.canvas, QEvent::KeyPress, Qt::Key_Space );
        Mouse( fixture.canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton );
        REQUIRE( fixture.canvas.isPanning() );
        QFocusEvent focusEvent( QEvent::FocusOut );
        QHideEvent hideEvent;
        QEvent event( interruption );
        QEvent *pInterruption = interruption == QEvent::FocusOut ? static_cast<QEvent *>( &focusEvent )
            : interruption == QEvent::Hide ? static_cast<QEvent *>( &hideEvent ) : &event;
        QApplication::sendEvent( &fixture.canvas, pInterruption );
        CHECK_FALSE( fixture.canvas.isPanning() );
        const auto origin = fixture.canvas.viewOrigin();
        Mouse( fixture.canvas, QEvent::MouseMove, start + QPointF( 20, 20 ), Qt::NoButton, Qt::NoButton );
        CHECK( fixture.canvas.viewOrigin() == origin );

        // Space was also reset, so a fresh left click can paint normally.
        Mouse( fixture.canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton );
        CHECK( CypherTileMapDocument_CellHasFloor( fixture.document.document(), { 3, 3 } ) );
        QApplication::sendEvent( &fixture.canvas, pInterruption );
        Mouse( fixture.canvas, QEvent::MouseButtonRelease, start, Qt::LeftButton, Qt::NoButton );
        CHECK_FALSE( fixture.document.isDirty() );
        CHECK_FALSE( CypherTileMapDocument_CellHasFloor( fixture.document.document(), { 3, 3 } ) );
    }
}

TEST_CASE( "A lost mouse release ends panning on the next pointer event", "[TileEditor][Navigation]" )
{
    EnsureSelectionApplication();
    SelectionFixture fixture;
    fixture.canvas.setTool( tile_canvas_tool_t::PAN );
    const auto origin = fixture.canvas.viewOrigin();
    const QPointF start = fixture.center( 3, 3 );
    Mouse( fixture.canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton );
    REQUIRE( fixture.canvas.isPanning() );
    Mouse( fixture.canvas, QEvent::MouseMove, start + QPointF( 100, 100 ), Qt::NoButton, Qt::NoButton );
    CHECK_FALSE( fixture.canvas.isPanning() );
    CHECK( fixture.canvas.viewOrigin() == origin );
    CHECK_FALSE( fixture.document.isDirty() );
}

TEST_CASE( "Top view wireframe uses subtle material fills while preserving custom canvas colors", "[TileEditor][Display]" )
{
    EnsureSelectionApplication();
    SelectionFixture fixture;
    QString error;
    REQUIRE( fixture.document.beginEdit( QStringLiteral( "Display fixture" ), &error ) );
    REQUIRE( fixture.document.paintCell( { 3, 3 }, { 0, 1, 2 }, &error ) );
    REQUIRE( fixture.document.commitEdit( &error ) );
    tile_editor_preferences_t preferences;
    preferences.showOrthoMaterials = false; // Exercise the fallback wireframe/solid modes.
    preferences.showGrid = preferences.showMarkers = false;
    preferences.showCoordinateRulers = false; // Keep the corner sample on the canvas.
    preferences.canvasColor = QColor( 22, 37, 51 );
    preferences.wireframeOrtho = false;
    fixture.canvas.setPreferences( preferences );
    fixture.canvas.show();
    QApplication::processEvents();
    const QPoint point = fixture.center( 3, 3 ).toPoint();
    const QImage shaded = fixture.canvas.grab().toImage();
    preferences.wireframeOrtho = true;
    fixture.canvas.setPreferences( preferences );
    const QImage wireframe = fixture.canvas.grab().toImage();
    REQUIRE_FALSE( shaded.isNull() );
    REQUIRE_FALSE( wireframe.isNull() );
    const auto material = CypherTileMapMaterial_Resolve( 2 );
    const QColor materialColor = QColor::fromRgbF( material.colorR, material.colorG, material.colorB );
    CHECK( shaded.pixelColor( point ).rgb() == materialColor.rgb() );
    CHECK( wireframe.pixelColor( point ).rgb() != shaded.pixelColor( point ).rgb() );
    CHECK( wireframe.pixelColor( point ).rgb() != preferences.canvasColor.rgb() );
    CHECK( shaded.pixelColor( 0, 0 ).rgb() == preferences.canvasColor.rgb() );
    CHECK( wireframe.pixelColor( 0, 0 ).rgb() == preferences.canvasColor.rgb() );
}
