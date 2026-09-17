//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileCanvas.cpp
//  Purpose: Implements grid drawing, navigation, selection, and tile tools.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherTileCanvas.h"
#include "CypherTileGrid.h"
#include "CypherTileOrthoMaterials.h"

#include "Core/CypherTileMapMaterials.h"
#include "Core/CypherTileMapGeometry.h"

#include "CypherCommon/Tier1/CypherCommon_Vector.h"

#include <QEvent>
#include <QApplication>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QTimer>
#include <QTransform>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>
#include <vector>

namespace cypher::tools::tile_editor
{

namespace
{

constexpr qreal TILE_CANVAS_MIN_ZOOM = 0.05;
constexpr qreal TILE_CANVAS_MAX_ZOOM = 128.0;
constexpr int TILE_RULER_MAX_TICKS = 512;

struct tile_ruler_tick_t {
    qreal screenPosition{};
    QString label{};
};

QString CoordinateLabel( double coordinate )
{
    if ( std::abs( coordinate ) < 0.0000005 ) coordinate = 0.0;
    return QString::number( coordinate, 'g', 6 );
}

void DrawCoordinateRulers(
    QPainter &painter,
    const QRect &viewport,
    const tile_editor_preferences_t &preferences,
    const QString &axisNames,
    std::vector<tile_ruler_tick_t> horizontalTicks,
    std::vector<tile_ruler_tick_t> verticalTicks )
{
    if ( viewport.isEmpty() ) return;

    std::sort( horizontalTicks.begin(), horizontalTicks.end(), []( const auto &left, const auto &right ) {
        return left.screenPosition < right.screenPosition;
    } );
    std::sort( verticalTicks.begin(), verticalTicks.end(), []( const auto &left, const auto &right ) {
        return left.screenPosition < right.screenPosition;
    } );

    const QFontMetrics metrics( painter.font() );
    int widestVerticalLabel = metrics.horizontalAdvance( axisNames );
    for ( const auto &tick : verticalTicks )
        widestVerticalLabel = std::max( widestVerticalLabel, metrics.horizontalAdvance( tick.label ) );
    const int topHeight = metrics.height() + 9;
    const int leftWidth = std::clamp( widestVerticalLabel + 10, 32, 76 );
    const QColor background = preferences.panelColor;
    const QColor edge = preferences.majorGridColor;

    painter.save();
    painter.setRenderHint( QPainter::TextAntialiasing, true );
    painter.fillRect( QRect( 0, 0, viewport.width(), topHeight ), background );
    painter.fillRect( QRect( 0, topHeight, leftWidth, viewport.height() - topHeight ), background );
    painter.setPen( QPen( edge, 1.0 ) );
    painter.drawLine( leftWidth, topHeight - 1, viewport.width(), topHeight - 1 );
    painter.drawLine( leftWidth - 1, topHeight, leftWidth - 1, viewport.height() );

    qreal previousRight = leftWidth - 4.0;
    for ( const auto &tick : horizontalTicks ) {
        const int textWidth = metrics.horizontalAdvance( tick.label );
        const qreal textLeft = tick.screenPosition - textWidth * 0.5;
        const qreal textRight = textLeft + textWidth;
        if ( tick.screenPosition < leftWidth || tick.screenPosition > viewport.width() ||
             textLeft < previousRight + 4.0 || textRight > viewport.width() - 2.0 ) continue;
        painter.setPen( preferences.textColor );
        painter.drawText( QRectF( textLeft, 1.0, textWidth + 1.0, metrics.height() ),
                          Qt::AlignCenter, tick.label );
        painter.setPen( QPen( edge, 1.0 ) );
        painter.drawLine( QPointF( tick.screenPosition, topHeight - 5.0 ),
                          QPointF( tick.screenPosition, topHeight - 1.0 ) );
        previousRight = textRight;
    }

    qreal previousBottom = topHeight - 2.0;
    for ( const auto &tick : verticalTicks ) {
        const qreal textTop = tick.screenPosition - metrics.height() * 0.5;
        const qreal textBottom = textTop + metrics.height();
        if ( tick.screenPosition < topHeight || tick.screenPosition > viewport.height() ||
             textTop < previousBottom + 2.0 || textBottom > viewport.height() - 2.0 ) continue;
        painter.setPen( preferences.textColor );
        painter.drawText( QRectF( 2.0, textTop, leftWidth - 9.0, metrics.height() ),
                          Qt::AlignRight | Qt::AlignVCenter, tick.label );
        painter.setPen( QPen( edge, 1.0 ) );
        painter.drawLine( QPointF( leftWidth - 5.0, tick.screenPosition ),
                          QPointF( leftWidth - 1.0, tick.screenPosition ) );
        previousBottom = textBottom;
    }

    painter.fillRect( QRect( 0, 0, leftWidth, topHeight ), background );
    painter.setPen( preferences.accentColor );
    painter.drawText( QRect( 2, 1, leftWidth - 4, topHeight - 3 ),
                      Qt::AlignCenter, axisNames );
    painter.restore();
}

void DrawTopAxisIndicator(
    QPainter &painter,
    const QRect &viewport,
    const tile_editor_preferences_t &preferences )
{
    if ( viewport.width() < 120 || viewport.height() < 120 ) return;

    const QFontMetrics metrics( painter.font() );
    const int rulerInset = preferences.showCoordinateRulers
        ? metrics.height() + 9 : 0;
    constexpr qreal panelSize = 76.0;
    constexpr qreal panelMargin = 8.0;
    const QRectF panel(
        viewport.width() - panelSize - panelMargin,
        rulerInset + panelMargin,
        panelSize,
        panelSize );
    const QPointF origin = panel.topLeft() + QPointF( 21.0, 21.0 );
    constexpr qreal axisLength = 34.0;

    QColor panelColor = preferences.panelColor;
    panelColor.setAlpha( 218 );
    QColor panelEdge = preferences.majorGridColor;
    panelEdge.setAlpha( 220 );

    painter.save();
    painter.setRenderHint( QPainter::Antialiasing, true );
    painter.setPen( QPen( panelEdge, 1.0 ) );
    painter.setBrush( panelColor );
    painter.drawRect( panel );

    auto drawArrow = [&]( QPointF direction, const QColor &color,
                          const QString &label ) {
        const QPointF endpoint = origin + direction * axisLength;
        const QPointF perpendicular( -direction.y(), direction.x() );
        const QPointF arrowBase = endpoint - direction * 7.0;

        painter.setPen( QPen( QColor( 0, 0, 0, 175 ), 4.5,
                              Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) );
        painter.drawLine( origin, endpoint );
        painter.setPen( QPen( color, 2.25, Qt::SolidLine,
                              Qt::RoundCap, Qt::RoundJoin ) );
        painter.drawLine( origin, endpoint );
        painter.drawLine( endpoint, arrowBase + perpendicular * 4.0 );
        painter.drawLine( endpoint, arrowBase - perpendicular * 4.0 );

        QFont labelFont = painter.font();
        labelFont.setBold( true );
        painter.setFont( labelFont );
        painter.setPen( color );
        painter.drawText( QRectF( endpoint.x() - 7.0, endpoint.y() - 9.0,
                                  14.0, 18.0 ), Qt::AlignCenter, label );
    };

    // Top looks down the +Z axis. X moves right, Y follows the tile map's
    // authored row direction, and the blue dot denotes +Z toward the viewer.
    drawArrow( { 1.0, 0.0 }, preferences.axisXColor, QStringLiteral( "X" ) );
    drawArrow( { 0.0, 1.0 }, preferences.axisYColor, QStringLiteral( "Y" ) );
    painter.setPen( QPen( QColor( 0, 0, 0, 180 ), 5.0 ) );
    painter.setBrush( preferences.axisZColor );
    painter.drawEllipse( origin, 5.0, 5.0 );
    painter.setPen( Qt::NoPen );
    painter.setBrush( preferences.axisZColor.lighter( 125 ) );
    painter.drawEllipse( origin, 1.8, 1.8 );
    painter.setPen( preferences.axisZColor );
    painter.drawText( QRectF( origin.x() - 17.0, origin.y() - 19.0,
                              14.0, 16.0 ), Qt::AlignCenter,
                      QStringLiteral( "Z" ) );
    painter.restore();
}

bool SameCoordinate( tile_map_grid_coord_t left, tile_map_grid_coord_t right )
{
    return left.x == right.x && left.y == right.y;
}

bool CoordinateBefore( tile_map_grid_coord_t left, tile_map_grid_coord_t right )
{
    return left.y != right.y ? left.y < right.y : left.x < right.x;
}

QLineF DoorEdgeLine(
    const QRectF &cellRect,
    tile_map_marker_side_t side,
    qreal insetFraction = 0.16 )
{
    const qreal horizontalInset = cellRect.width() * insetFraction;
    const qreal verticalInset = cellRect.height() * insetFraction;
    switch ( side ) {
        case tile_map_marker_side_t::NORTH:
            return {
                cellRect.topLeft() + QPointF( horizontalInset, 0.0 ),
                cellRect.topRight() - QPointF( horizontalInset, 0.0 ) };
        case tile_map_marker_side_t::EAST:
            return {
                cellRect.topRight() + QPointF( 0.0, verticalInset ),
                cellRect.bottomRight() - QPointF( 0.0, verticalInset ) };
        case tile_map_marker_side_t::SOUTH:
            return {
                cellRect.bottomRight() - QPointF( horizontalInset, 0.0 ),
                cellRect.bottomLeft() + QPointF( horizontalInset, 0.0 ) };
        case tile_map_marker_side_t::WEST:
            return {
                cellRect.bottomLeft() - QPointF( 0.0, verticalInset ),
                cellRect.topLeft() + QPointF( 0.0, verticalInset ) };
        case tile_map_marker_side_t::NONE:
            break;
    }
    return {};
}

QPointF DoorInwardDirection( tile_map_marker_side_t side )
{
    switch ( side ) {
        case tile_map_marker_side_t::NORTH: return { 0.0, 1.0 };
        case tile_map_marker_side_t::EAST: return { -1.0, 0.0 };
        case tile_map_marker_side_t::SOUTH: return { 0.0, -1.0 };
        case tile_map_marker_side_t::WEST: return { 1.0, 0.0 };
        case tile_map_marker_side_t::NONE: break;
    }
    return {};
}

tile_map_grid_coord_t DoorNeighbor(
    tile_map_grid_coord_t cell,
    tile_map_marker_side_t side )
{
    switch ( side ) {
        case tile_map_marker_side_t::NORTH: --cell.y; break;
        case tile_map_marker_side_t::EAST:  ++cell.x; break;
        case tile_map_marker_side_t::SOUTH: ++cell.y; break;
        case tile_map_marker_side_t::WEST:  --cell.x; break;
        case tile_map_marker_side_t::NONE: break;
    }
    return cell;
}

bool DoorCanOccupyEdge(
    const tile_map_document_t *pDocument,
    tile_map_grid_coord_t cell,
    tile_map_marker_side_t side )
{
    return pDocument != nullptr &&
        CypherTileMapMarkerSide_IsCardinal( side ) &&
        CypherTileMapDocument_CellHasFloor( pDocument, cell ) &&
        CypherTileMapDocument_CellAt( pDocument, cell )->shape == tile_map_cell_shape_t::FLAT &&
        !CypherTileMapDocument_CellHasFloor(
            pDocument,
            DoorNeighbor( cell, side ) );
}

void DrawStairFootprint( QPainter &painter, const QRectF &rect,
    tile_map_cell_shape_t shape, int steps, const QColor &stairColor, bool bWireframe = false )
{
    if ( shape == tile_map_cell_shape_t::FLAT || rect.width() < 12.0 ) return;
    painter.save();
    const QRectF inner = rect.adjusted( 2, 2, -2, -2 );
    const bool alongX = shape == tile_map_cell_shape_t::STAIRS_EAST || shape == tile_map_cell_shape_t::STAIRS_WEST;
    const int visibleSteps = std::clamp( steps, 2, std::max( 2, static_cast<int>( inner.width() / 3.0 ) ) );
    painter.setPen( QPen( bWireframe ? stairColor : QColor( 20, 24, 28, 210 ), 1 ) );
    for ( int i = 1; i < visibleSteps; ++i ) {
        const qreal t = static_cast<qreal>( i ) / visibleSteps;
        if ( alongX ) painter.drawLine( QPointF( inner.left() + t * inner.width(), inner.top() ), QPointF( inner.left() + t * inner.width(), inner.bottom() ) );
        else painter.drawLine( QPointF( inner.left(), inner.top() + t * inner.height() ), QPointF( inner.right(), inner.top() + t * inner.height() ) );
    }
    const QPointF direction = shape == tile_map_cell_shape_t::STAIRS_NORTH ? QPointF( 0, -1 )
        : shape == tile_map_cell_shape_t::STAIRS_EAST ? QPointF( 1, 0 )
        : shape == tile_map_cell_shape_t::STAIRS_SOUTH ? QPointF( 0, 1 ) : QPointF( -1, 0 );
    const QPointF across( -direction.y(), direction.x() );
    const qreal length = inner.width() * 0.32;
    const QPointF tip = inner.center() + direction * length;
    painter.setPen( QPen( stairColor.lighter( 125 ), 1.5 ) );
    painter.drawLine( inner.center() - direction * length, tip );
    painter.drawLine( tip, tip - direction * length * 0.5 + across * length * 0.4 );
    painter.drawLine( tip, tip - direction * length * 0.5 - across * length * 0.4 );
    painter.restore();
}

void DrawDoor(
    QPainter &painter,
    const QRectF &cellRect,
    tile_map_marker_side_t side,
    const QColor &color,
    bool bGhost )
{
    if ( !CypherTileMapMarkerSide_IsCardinal( side ) ) return;

    const QLineF edge = DoorEdgeLine( cellRect, side );
    const qreal shadowWidth = std::clamp( cellRect.width() * 0.20, 4.0, 8.0 );
    const qreal leafWidth = std::clamp( cellRect.width() * 0.09, 2.0, 4.0 );

    if ( !bGhost ) {
        QPen shadow( QColor( 15, 19, 22, 235 ), shadowWidth );
        shadow.setCapStyle( Qt::RoundCap );
        painter.setPen( shadow );
        painter.drawLine( edge );
    }

    QPen leaf( color, leafWidth, bGhost ? Qt::DashLine : Qt::SolidLine );
    leaf.setCapStyle( Qt::RoundCap );
    painter.setPen( leaf );
    painter.setBrush( Qt::NoBrush );
    painter.drawLine( edge );

    const QPointF center = edge.center();
    const QPointF inward = DoorInwardDirection( side );
    const qreal directionLength = std::clamp(
        cellRect.width() * 0.20,
        4.0,
        9.0 );
    painter.drawLine( center, center + inward * directionLength );

    const qreal handleRadius = std::clamp(
        cellRect.width() * 0.055,
        1.5,
        3.0 );
    painter.setPen( Qt::NoPen );
    painter.setBrush( color );
    painter.drawEllipse( center, handleRadius, handleRadius );
}

} // namespace

CypherTileCanvas::CypherTileCanvas( QWidget *pParent )
    : QWidget( pParent )
{
    setObjectName( QStringLiteral( "CypherTileCanvas" ) );
    setFocusPolicy( Qt::StrongFocus );
    setMouseTracking( true );
    setMinimumSize( 160, 120 );
    m_paint.nFloorLevel = 0;
    m_paint.nWallHeightLevels = 1u;
    m_paint.nMaterialSlot = 0u;
    updateCursorShape();
}

void CypherTileCanvas::setDocumentBridge( CypherTileDocumentBridge *pBridge )
{
    cancelInteraction();
    m_pBridge = pBridge;
    m_hoverCell = { -1, -1 };
    clearSelection();
    if ( m_preferences.frameMapOnOpen || !m_bHasFit ) {
        m_bHasFit = false;
        m_bUserNavigated = false;
        fitToView();
    }
    update();
}

void CypherTileCanvas::setPreferences(
    const tile_editor_preferences_t &preferences )
{
    const bool scaleChanged = m_preferences.emptyViewCellPixels != preferences.emptyViewCellPixels;
    m_preferences = preferences;
    if ( scaleChanged && !m_bUserNavigated ) fitToView();
    update();
}

void CypherTileCanvas::setMaterialCache( const tile_ortho_material_cache_t *pCache )
{
    m_pMaterialCache = pCache;
    update();
}

void CypherTileCanvas::setTool( tile_canvas_tool_t tool )
{
    if ( m_tool == tool ) return;
    cancelInteraction();
    m_tool = tool;
    updateCursorShape();
    update();
}

tile_canvas_tool_t CypherTileCanvas::tool() const
{
    return m_tool;
}

void CypherTileCanvas::setPaint( const tile_map_paint_t &paint )
{
    m_paint = paint;
    // Paint properties drive the live hover ghost, so repaint before the
    // next mouse move when the palette or numeric controls change.
    update();
}

const tile_map_paint_t &CypherTileCanvas::paint() const
{
    return m_paint;
}

void CypherTileCanvas::setDoorSide( tile_map_marker_side_t side )
{
    if ( !CypherTileMapMarkerSide_IsCardinal( side ) ||
         m_doorSide == side ) return;
    m_doorSide = side;
    update();
}

tile_map_marker_side_t CypherTileCanvas::doorSide() const
{
    return m_doorSide;
}

void CypherTileCanvas::setChangedCallback( changed_callback_t callback )
{
    m_changedCallback = std::move( callback );
}

void CypherTileCanvas::setPreviewChangedCallback( changed_callback_t callback )
{
    m_previewChangedCallback = std::move( callback );
}

void CypherTileCanvas::setPaintPickedCallback( std::function<void( const tile_map_paint_t & )> callback )
{
    m_paintPickedCallback = std::move( callback );
}

void CypherTileCanvas::setSelectionCallback( selection_callback_t callback )
{
    m_selectionCallback = std::move( callback );
}

void CypherTileCanvas::setMoveSelectionCallback( std::function<void( int, int, bool )> callback )
{
    m_moveSelectionCallback = std::move( callback );
}

void CypherTileCanvas::setCursorCallback( cursor_callback_t callback )
{
    m_cursorCallback = std::move( callback );
}

void CypherTileCanvas::setZoomCallback( zoom_callback_t callback )
{
    m_zoomCallback = std::move( callback );
}

void CypherTileCanvas::setStatusCallback( status_callback_t callback )
{
    m_statusCallback = std::move( callback );
}

void CypherTileCanvas::setContextMenuCallback( context_menu_callback_t callback )
{
    m_contextMenuCallback = std::move( callback );
}

bool CypherTileCanvas::hasSelection() const
{
    return m_bHasSelection;
}

tile_map_grid_coord_t CypherTileCanvas::selectedCell() const
{
    return m_selection;
}

tile_map_grid_rect_t CypherTileCanvas::selectionRect() const
{
    return m_bHasSelection ? m_selectionRect : tile_map_grid_rect_t{};
}

const std::vector<tile_map_grid_coord_t> &CypherTileCanvas::selectedCells() const
{
    return m_selectedCells;
}

void CypherTileCanvas::setSelectedCells( std::span<const tile_map_grid_coord_t> cells )
{
    std::vector<tile_map_grid_coord_t> selected;
    selected.reserve( cells.size() );
    for ( const auto coordinate : cells ) if ( contains( coordinate ) ) selected.push_back( coordinate );
    std::sort( selected.begin(), selected.end(), CoordinateBefore );
    selected.erase( std::unique( selected.begin(), selected.end(), SameCoordinate ), selected.end() );
    m_bDraggingSelection = false;
    m_bMovingSelection = false;
    m_bMoveStarted = false;
    if ( selected.size() == m_selectedCells.size() &&
         std::equal( selected.begin(), selected.end(), m_selectedCells.begin(), SameCoordinate ) ) {
        update();
        return;
    }
    m_selectedCells = std::move( selected );
    m_bHasSelection = !m_selectedCells.empty();
    m_selection = m_bHasSelection ? m_selectedCells.front() : tile_map_grid_coord_t{};
    m_selectionRect = {};
    if ( m_bHasSelection ) {
        int x0 = m_selection.x, x1 = m_selection.x, y0 = m_selection.y, y1 = m_selection.y;
        for ( const auto coordinate : m_selectedCells ) {
            x0 = std::min( x0, coordinate.x ); x1 = std::max( x1, coordinate.x );
            y0 = std::min( y0, coordinate.y ); y1 = std::max( y1, coordinate.y );
        }
        m_selectionRect = { x0, y0, static_cast<u32>( x1 - x0 + 1 ), static_cast<u32>( y1 - y0 + 1 ) };
    }
    notifySelection();
    update();
}

void CypherTileCanvas::modifySelectedCells(
    std::span<const tile_map_grid_coord_t> cells, Qt::KeyboardModifiers modifiers )
{
    const auto operation = modifiers.testFlag( Qt::AltModifier ) ? selection_operation_t::SUBTRACT
        : modifiers.testFlag( Qt::ControlModifier ) ? selection_operation_t::TOGGLE
        : modifiers.testFlag( Qt::ShiftModifier ) ? selection_operation_t::ADD
        : selection_operation_t::REPLACE;
    combineSelectedCells( cells, operation );
}

void CypherTileCanvas::combineSelectedCells(
    std::span<const tile_map_grid_coord_t> cells, selection_operation_t operation )
{
    if ( operation == selection_operation_t::REPLACE ) { setSelectedCells( cells ); return; }
    std::vector<tile_map_grid_coord_t> incoming;
    incoming.reserve( cells.size() );
    for ( const auto coordinate : cells ) if ( contains( coordinate ) ) incoming.push_back( coordinate );
    std::sort( incoming.begin(), incoming.end(), CoordinateBefore );
    incoming.erase( std::unique( incoming.begin(), incoming.end(), SameCoordinate ), incoming.end() );
    std::vector<tile_map_grid_coord_t> combined;
    combined.reserve( m_selectedCells.size() + incoming.size() );
    if ( operation == selection_operation_t::ADD ) {
        std::set_union( m_selectedCells.begin(), m_selectedCells.end(), incoming.begin(), incoming.end(),
            std::back_inserter( combined ), CoordinateBefore );
    } else if ( operation == selection_operation_t::TOGGLE ) {
        std::set_symmetric_difference( m_selectedCells.begin(), m_selectedCells.end(), incoming.begin(), incoming.end(),
            std::back_inserter( combined ), CoordinateBefore );
    } else {
        std::set_difference( m_selectedCells.begin(), m_selectedCells.end(), incoming.begin(), incoming.end(),
            std::back_inserter( combined ), CoordinateBefore );
    }
    setSelectedCells( combined );
}

void CypherTileCanvas::selectAllOccupied()
{
    std::vector<tile_map_grid_coord_t> selected;
    if ( m_pBridge != nullptr && m_pBridge->isInitialized() ) {
        const auto *document = m_pBridge->document();
        for ( u32 y = 0; y < document->nHeight; ++y ) {
            for ( u32 x = 0; x < document->nWidth; ++x ) {
                const tile_map_grid_coord_t coordinate{ static_cast<i32>( x ), static_cast<i32>( y ) };
                if ( CypherTileMapDocument_CellHasFloor( document, coordinate ) ) selected.push_back( coordinate );
            }
        }
        for ( usize i = 0; i < Vector_Count( &document->markers ); ++i )
            selected.push_back( document->markers.pData[i].cell );
    }
    setSelectedCells( selected );
}

void CypherTileCanvas::setSelectionRect( const tile_map_grid_rect_t &rectangle )
{
    const auto *pDocument = m_pBridge != nullptr ? m_pBridge->document() : nullptr;
    if ( pDocument == nullptr || !m_pBridge->isInitialized() ||
         rectangle.nWidth == 0 || rectangle.nHeight == 0 ) {
        clearSelection();
        return;
    }
    // Widen before adding so an out-of-bounds request cannot wrap around.
    const auto x0 = std::clamp<qint64>( rectangle.x, 0, pDocument->nWidth );
    const auto y0 = std::clamp<qint64>( rectangle.y, 0, pDocument->nHeight );
    const auto x1 = std::clamp<qint64>( static_cast<qint64>( rectangle.x ) + rectangle.nWidth, 0, pDocument->nWidth );
    const auto y1 = std::clamp<qint64>( static_cast<qint64>( rectangle.y ) + rectangle.nHeight, 0, pDocument->nHeight );
    if ( x1 <= x0 || y1 <= y0 ) {
        clearSelection();
        return;
    }
    std::vector<tile_map_grid_coord_t> selected;
    selected.reserve( static_cast<size_t>( ( x1 - x0 ) * ( y1 - y0 ) ) );
    for ( auto y = y0; y < y1; ++y )
        for ( auto x = x0; x < x1; ++x ) selected.push_back( { static_cast<i32>( x ), static_cast<i32>( y ) } );
    setSelectedCells( selected );
}

void CypherTileCanvas::clearSelection()
{
    m_bDraggingSelection = false;
    m_bMovingSelection = false;
    m_bMoveStarted = false;
    if ( !m_bHasSelection ) { update(); return; }
    m_bHasSelection = false;
    m_selectionRect = {};
    m_selectedCells.clear();
    m_selection = {};
    notifySelection();
    update();
}

void CypherTileCanvas::selectCell( tile_map_grid_coord_t coordinate, bool bCenter )
{
    if ( !contains( coordinate ) ) return;
    setSelectionRect( { coordinate.x, coordinate.y, 1, 1 } );
    if ( bCenter ) {
        m_origin = QPointF( width() * 0.5, height() * 0.5 ) -
            QPointF( ( coordinate.x + 0.5 ) * m_zoom, ( coordinate.y + 0.5 ) * m_zoom );
        m_bHasFit = true;
        m_bUserNavigated = true;
    }
    update();
}

void CypherTileCanvas::refreshDocument()
{
    if ( m_bHasSelection ) setSelectedCells( m_selectedCells );
    update();
}

qreal CypherTileCanvas::zoomFactor() const
{
    return m_zoom;
}

void CypherTileCanvas::setZoomFactor( qreal zoom )
{
    if ( !std::isfinite( zoom ) ) return;
    const qreal bounded = std::clamp(
        zoom,
        TILE_CANVAS_MIN_ZOOM,
        TILE_CANVAS_MAX_ZOOM );
    if ( qFuzzyCompare( bounded, m_zoom ) ) return;
    const QPointF anchor( width() * 0.5, height() * 0.5 );
    const QPointF gridPoint = ( anchor - m_origin ) / m_zoom;
    m_zoom = bounded;
    m_origin = anchor - gridPoint * m_zoom;
    m_bHasFit = true;
    m_bUserNavigated = true;
    if ( m_zoomCallback ) m_zoomCallback( m_zoom );
    update();
}

void CypherTileCanvas::fitToView()
{
    m_bUserNavigated = false;
    const tile_map_document_t *pDocument =
        m_pBridge != nullptr ? m_pBridge->document() : nullptr;
    if ( pDocument == nullptr ||
         !CypherTileMapDocument_IsInitialized( pDocument ) ||
         width() <= 0 || height() <= 0 ) return;

    // Frame authored content, not the unpainted extent of a 64/1024-cell
    // document. Empty maps start at a comfortable editing scale near (0, 0).
    int x0 = static_cast<int>( pDocument->nWidth );
    int y0 = static_cast<int>( pDocument->nHeight );
    int x1 = 0, y1 = 0;
    auto includeCell = [&]( tile_map_grid_coord_t cell ) {
        if ( !contains( cell ) ) return;
        x0 = std::min( x0, cell.x ); y0 = std::min( y0, cell.y );
        x1 = std::max( x1, cell.x + 1 ); y1 = std::max( y1, cell.y + 1 );
    };
    for ( u32 y = 0; y < pDocument->nHeight; ++y ) {
        for ( u32 x = 0; x < pDocument->nWidth; ++x ) {
            const tile_map_grid_coord_t cell{ static_cast<i32>( x ), static_cast<i32>( y ) };
            if ( CypherTileMapDocument_CellHasFloor( pDocument, cell ) ) includeCell( cell );
        }
    }
    for ( usize i = 0; i < Vector_Count( &pDocument->markers ); ++i )
        includeCell( pDocument->markers.pData[i].cell );

    constexpr qreal margin = 40.0;
    if ( x1 <= x0 || y1 <= y0 ) {
        m_zoom = std::clamp<qreal>( m_preferences.emptyViewCellPixels,
            TILE_CANVAS_MIN_ZOOM, TILE_CANVAS_MAX_ZOOM );
        m_origin = QPointF( margin, margin );
    } else {
        const qreal horizontal = std::max<qreal>( 20.0, width() - margin * 2.0 ) / ( x1 - x0 );
        const qreal vertical = std::max<qreal>( 20.0, height() - margin * 2.0 - 24.0 ) / ( y1 - y0 );
        m_zoom = std::clamp( std::min( horizontal, vertical ),
            TILE_CANVAS_MIN_ZOOM, TILE_CANVAS_MAX_ZOOM );
        m_origin = QPointF( width() * 0.5 - ( x0 + x1 ) * 0.5 * m_zoom,
                            ( height() - 24.0 ) * 0.5 - ( y0 + y1 ) * 0.5 * m_zoom );
    }
    m_bHasFit = true;
    if ( m_zoomCallback ) m_zoomCallback( m_zoom );
    update();
}

void CypherTileCanvas::fitSelection()
{
    if ( !m_bHasSelection ) { fitToView(); return; }
    constexpr qreal margin = 44.0;
    m_zoom = std::clamp( std::min(
        ( width() - margin * 2.0 ) / m_selectionRect.nWidth,
        ( height() - margin * 2.0 ) / m_selectionRect.nHeight ),
        TILE_CANVAS_MIN_ZOOM, TILE_CANVAS_MAX_ZOOM );
    m_origin = QPointF( width() * 0.5, height() * 0.5 ) - QPointF(
        ( m_selectionRect.x + m_selectionRect.nWidth * 0.5 ) * m_zoom,
        ( m_selectionRect.y + m_selectionRect.nHeight * 0.5 ) * m_zoom );
    m_bHasFit = true;
    m_bUserNavigated = true;
    if ( m_zoomCallback ) m_zoomCallback( m_zoom );
    update();
}

bool CypherTileCanvas::isPanning() const { return m_bPanning; }
QPointF CypherTileCanvas::viewOrigin() const { return m_origin; }

bool CypherTileCanvas::stampRectangle(
    const QSize &size,
    const QString &label )
{
    const tile_map_document_t *pDocument =
        m_pBridge != nullptr ? m_pBridge->document() : nullptr;
    if ( pDocument == nullptr || !m_pBridge->isInitialized() ||
         size.width() <= 0 || size.height() <= 0 ) return false;

    // Clicking a palette button necessarily moves the pointer off the canvas.
    // A deliberate selection therefore takes precedence over incidental hover.
    const tile_map_grid_coord_t center = m_bHasSelection
        ? m_selection
        : ( contains( m_hoverCell ) ? m_hoverCell : tile_map_grid_coord_t{
              static_cast<i32>( pDocument->nWidth / 2u ),
              static_cast<i32>( pDocument->nHeight / 2u ) } );
    const i32 x = std::clamp(
        center.x - size.width() / 2,
        0,
        std::max( 0, static_cast<int>( pDocument->nWidth ) - size.width() ) );
    const i32 y = std::clamp(
        center.y - size.height() / 2,
        0,
        std::max( 0, static_cast<int>( pDocument->nHeight ) - size.height() ) );
    const u32 nWidth = static_cast<u32>( std::min(
        size.width(), static_cast<int>( pDocument->nWidth ) ) );
    const u32 nHeight = static_cast<u32>( std::min(
        size.height(), static_cast<int>( pDocument->nHeight ) ) );

    QString error;
    if ( !m_pBridge->beginEdit( label, &error ) ) {
        report( error, true );
        return false;
    }
    if ( !m_pBridge->paintRect( { x, y, nWidth, nHeight }, m_paint, &error ) ||
         !m_pBridge->commitEdit( &error ) ) {
        m_pBridge->cancelEdit();
        report( error, true );
        return false;
    }
    setSelectionRect( { x, y, nWidth, nHeight } );
    notifyChanged();
    report( tr( "%1 applied at (%2, %3)" ).arg( label ).arg( x ).arg( y ) );
    return true;
}

void CypherTileCanvas::paintEvent( QPaintEvent * )
{
    QPainter painter( this );
    painter.fillRect( rect(), m_preferences.canvasColor );
    painter.setRenderHint( QPainter::Antialiasing, false );

    const tile_map_document_t *pDocument =
        m_pBridge != nullptr ? m_pBridge->document() : nullptr;
    if ( pDocument == nullptr ||
         !CypherTileMapDocument_IsInitialized( pDocument ) ) {
        painter.setPen( QColor( 150, 154, 156 ) );
        painter.drawText( rect(), Qt::AlignCenter, tr( "No tile map is open" ) );
        return;
    }

    const int xBegin = std::clamp(
        static_cast<int>( std::floor( -m_origin.x() / m_zoom ) ),
        0,
        static_cast<int>( pDocument->nWidth ) );
    const int yBegin = std::clamp(
        static_cast<int>( std::floor( -m_origin.y() / m_zoom ) ),
        0,
        static_cast<int>( pDocument->nHeight ) );
    const int xEnd = std::clamp(
        static_cast<int>( std::ceil( ( width() - m_origin.x() ) / m_zoom ) ),
        0,
        static_cast<int>( pDocument->nWidth ) );
    const int yEnd = std::clamp(
        static_cast<int>( std::ceil( ( height() - m_origin.y() ) / m_zoom ) ),
        0,
        static_cast<int>( pDocument->nHeight ) );

    const int gridStep = TileEditorGrid_EffectiveSpacingCells(
        m_preferences.gridSpacingCells, m_zoom,
        m_preferences.adaptiveGrid, m_preferences.gridMinimumPixels );
    const double majorStep = TileEditorGrid_EffectiveMajorSpacingCells(
        gridStep, m_preferences.majorGridEvery );
    for ( int x = xBegin; x <= xEnd; ++x ) {
        if ( !m_preferences.showGrid || x % gridStep != 0 ) continue;
        const bool bMajor = x == 0 || x == static_cast<int>( pDocument->nWidth ) ||
            TileEditorGrid_IsMajorCoordinate( x, majorStep );
        painter.setPen( QPen(
            bMajor ? m_preferences.majorGridColor
                   : m_preferences.minorGridColor,
            bMajor ? 1.25 : 1.0 ) );
        const qreal screenX = m_origin.x() + x * m_zoom;
        painter.drawLine(
            QPointF( screenX, m_origin.y() + yBegin * m_zoom ),
            QPointF( screenX, m_origin.y() + yEnd * m_zoom ) );
    }
    for ( int y = yBegin; y <= yEnd; ++y ) {
        if ( !m_preferences.showGrid || y % gridStep != 0 ) continue;
        const bool bMajor = y == 0 || y == static_cast<int>( pDocument->nHeight ) ||
            TileEditorGrid_IsMajorCoordinate( y, majorStep );
        painter.setPen( QPen(
            bMajor ? m_preferences.majorGridColor
                   : m_preferences.minorGridColor,
            bMajor ? 1.25 : 1.0 ) );
        const qreal screenY = m_origin.y() + y * m_zoom;
        painter.drawLine(
            QPointF( m_origin.x() + xBegin * m_zoom, screenY ),
            QPointF( m_origin.x() + xEnd * m_zoom, screenY ) );
    }

    // Centered axes are a display guide, not a change to authored coordinates.
    // With centering disabled, the lines pass through authored world (0, 0)
    // exactly: m_origin is the screen-space image of that point.
    if ( m_preferences.showViewAxes ) {
        const QPointF axes = m_preferences.centerViewAxes
            ? m_origin + QPointF( pDocument->nWidth * m_zoom * 0.5, pDocument->nHeight * m_zoom * 0.5 )
            : m_origin;
        painter.setPen( QPen( QColor( 0, 0, 0, 130 ), 3.75 ) );
        painter.drawLine( QPointF( 0, axes.y() ), QPointF( width(), axes.y() ) );
        painter.drawLine( QPointF( axes.x(), 0 ), QPointF( axes.x(), height() ) );
        painter.setPen( QPen( m_preferences.axisXColor, 1.75 ) );
        painter.drawLine( QPointF( 0, axes.y() ), QPointF( width(), axes.y() ) );
        painter.setPen( QPen( m_preferences.axisYColor, 1.75 ) );
        painter.drawLine( QPointF( axes.x(), 0 ), QPointF( axes.x(), height() ) );
    }


    painter.setPen( Qt::NoPen );
    // Keep room silhouettes filled at overview zoom; a fixed two-pixel
    // gutter would consume the whole tile once cells reach that size.
    const qreal cellInset = !m_preferences.wireframeOrtho && m_zoom >= 8.0 ? 1.0 : 0.0;
    for ( int y = yBegin; y < yEnd; ++y ) {
        for ( int x = xBegin; x < xEnd; ++x ) {
            const tile_map_cell_t *pCell = CypherTileMapDocument_CellAt(
                pDocument, { x, y } );
            if ( pCell == nullptr ||
                 ( pCell->flags & TILE_MAP_CELL_FLAG_FLOOR ) == 0u ) continue;
            const tile_map_material_definition_t material =
                CypherTileMapMaterial_Resolve( pCell->nMaterialSlot );
            QColor fill = QColor::fromRgbF(
                material.colorR,
                material.colorG,
                material.colorB );
            fill = fill.lighter( 100 + std::clamp<int>(
                pCell->nFloorLevel * 5, -30, 45 ) );
            if ( m_preferences.wireframeOrtho ) { fill = m_preferences.wireColor; fill.setAlpha( 12 ); }
            const QRectF cell(
                m_origin.x() + x * m_zoom + cellInset,
                m_origin.y() + y * m_zoom + cellInset,
                m_zoom - cellInset * 2.0,
                m_zoom - cellInset * 2.0 );
            if ( m_preferences.showOrthoMaterials ) {
                TileOrthoMaterials_Paint( painter, cell, m_pMaterialCache,
                    pCell->nMaterialSlot, m_preferences.orthoMaterialOpacity );
            } else painter.fillRect( cell, fill );
            if ( m_preferences.wireframeOrtho && m_zoom >= 4.0 ) {
                QColor edge = m_preferences.wireColor;
                edge.setAlpha( 125 );
                painter.setPen( QPen( edge, m_preferences.wireLineWidth ) );
                painter.setBrush( Qt::NoBrush );
                const auto visibleSeam = [&]( int nx, int ny ) {
                    if ( m_preferences.showInternalTileEdges ) return true;
                    const auto *neighbor = CypherTileMapDocument_CellAt( pDocument, { nx, ny } );
                    return !neighbor || !( neighbor->flags & TILE_MAP_CELL_FLAG_FLOOR ) ||
                        neighbor->nFloorLevel != pCell->nFloorLevel || neighbor->nMaterialSlot != pCell->nMaterialSlot ||
                        neighbor->nWallHeightLevels != pCell->nWallHeightLevels ||
                        neighbor->shape != pCell->shape || pCell->shape != tile_map_cell_shape_t::FLAT;
                };
                if ( visibleSeam( x, y - 1 ) ) painter.drawLine( cell.topLeft(), cell.topRight() );
                if ( visibleSeam( x + 1, y ) ) painter.drawLine( cell.topRight(), cell.bottomRight() );
                if ( visibleSeam( x, y + 1 ) ) painter.drawLine( cell.bottomRight(), cell.bottomLeft() );
                if ( visibleSeam( x - 1, y ) ) painter.drawLine( cell.bottomLeft(), cell.topLeft() );
                painter.setPen( Qt::NoPen );
            }
            DrawStairFootprint( painter, cell, pCell->shape, pCell->nStairSteps,
                m_preferences.stairColor, m_preferences.wireframeOrtho );
            // Zero is the overwhelmingly common blockout height. Repeating it
            // in every occupied tile hides the room silhouette, so reserve
            // the overlay for cells whose elevation carries information.
            if ( m_preferences.showViewMetrics && m_zoom >= 30.0 && pCell->nFloorLevel != 0 ) {
                painter.setPen( QColor( 225, 234, 237, 180 ) );
                painter.drawText(
                    cell,
                    Qt::AlignCenter,
                    QStringLiteral( "%1" ).arg( pCell->nFloorLevel ) );
                painter.setPen( Qt::NoPen );
            }
        }
    }

    // Distinguish generated walls from exposed floor edges. Wall strips use
    // the same world thickness as the geometry builder, never a fake scale.

    for ( int y = yBegin; y < yEnd; ++y ) {
        for ( int x = xBegin; x < xEnd; ++x ) {
            const tile_map_grid_coord_t cell{ x, y };
            if ( !CypherTileMapDocument_CellHasFloor( pDocument, cell ) ) continue;
            const tile_map_cell_t *pCell =
                CypherTileMapDocument_CellAt( pDocument, cell );
            if ( pCell == nullptr || pCell->shape != tile_map_cell_shape_t::FLAT ) continue;
            const qreal left = m_origin.x() + x * m_zoom;
            const qreal top = m_origin.y() + y * m_zoom;
            const qreal right = left + m_zoom;
            const qreal bottom = top + m_zoom;
            const auto hasVisibleSide = [pDocument, pCell, cell](
                tile_map_grid_coord_t neighborCoordinate ) {
                const tile_map_cell_t *pNeighbor =
                    CypherTileMapDocument_CellAt(
                        pDocument, neighborCoordinate );
                if ( pNeighbor != nullptr && ( pNeighbor->flags & TILE_MAP_CELL_FLAG_FLOOR ) &&
                     pCell->nFloorLevel == pNeighbor->nFloorLevel + 1 ) {
                    const bool highEdge =
                        ( neighborCoordinate.y < cell.y && pNeighbor->shape == tile_map_cell_shape_t::STAIRS_SOUTH ) ||
                        ( neighborCoordinate.y > cell.y && pNeighbor->shape == tile_map_cell_shape_t::STAIRS_NORTH ) ||
                        ( neighborCoordinate.x < cell.x && pNeighbor->shape == tile_map_cell_shape_t::STAIRS_EAST ) ||
                        ( neighborCoordinate.x > cell.x && pNeighbor->shape == tile_map_cell_shape_t::STAIRS_WEST );
                    if ( highEdge ) return false;
                }
                return pNeighbor == nullptr ||
                    ( pNeighbor->flags & TILE_MAP_CELL_FLAG_FLOOR ) == 0u ||
                    pCell->nFloorLevel > pNeighbor->nFloorLevel;
            };
            const bool hasWall = pCell->nWallHeightLevels > 0;
            const QColor edgeColor = hasWall ? m_preferences.wallColor : m_preferences.wireColor;
            const qreal thickness = TILE_MAP_DEFAULT_WALL_THICKNESS / pDocument->nCellSize * m_zoom;
            auto drawEdge = [&]( QPointF a, QPointF b, QPointF inward ) {
                painter.setPen( QPen( edgeColor, m_preferences.wireLineWidth + ( hasWall ? 0.6 : 0.0 ) ) );
                painter.drawLine( a, b );
                if ( !hasWall || thickness < 2.5 ) return;
                QColor fill = edgeColor; fill.setAlpha( 30 );
                const QPointF offset = inward * thickness;
                painter.setBrush( fill );
                painter.setPen( Qt::NoPen );
                painter.drawPolygon( QPolygonF{ a, b, b + offset, a + offset } );
                QColor inner = edgeColor; inner.setAlpha( 150 );
                painter.setPen( QPen( inner, m_preferences.wireLineWidth ) );
                painter.drawLine( a + offset, b + offset );
            };
            if ( hasVisibleSide( { x, y - 1 } ) ) drawEdge( { left, top }, { right, top }, { 0, 1 } );
            if ( hasVisibleSide( { x + 1, y } ) ) drawEdge( { right, top }, { right, bottom }, { -1, 0 } );
            if ( hasVisibleSide( { x, y + 1 } ) ) drawEdge( { right, bottom }, { left, bottom }, { 0, -1 } );
            if ( hasVisibleSide( { x - 1, y } ) ) drawEdge( { left, bottom }, { left, top }, { 1, 0 } );
        }
    }

    painter.setRenderHint( QPainter::Antialiasing, true );
    for ( usize i = 0u; i < Vector_Count( &pDocument->markers ); ++i ) {
        if ( !m_preferences.showMarkers ) break;
        const tile_map_marker_t *pMarker = Vector_At( &pDocument->markers, i );
        if ( pMarker == nullptr ) continue;
        if ( pMarker->kind == tile_map_marker_kind_t::DOOR ) {
            if ( !contains( pMarker->cell ) ||
                 !CypherTileMapMarkerSide_IsCardinal( pMarker->side ) ) {
                continue;
            }
            const QRectF cellRect(
                gridToScreen( pMarker->cell ),
                QSizeF( m_zoom, m_zoom ) );
            DrawDoor(
                painter,
                cellRect,
                pMarker->side,
                DoorCanOccupyEdge(
                    pDocument, pMarker->cell, pMarker->side )
                    ? m_preferences.doorColor
                    : QColor( 226, 91, 82 ),
                false );
            continue;
        }
        if ( pMarker->kind != tile_map_marker_kind_t::PLAYER_SPAWN ) continue;
        const QPointF center = gridToScreen( pMarker->cell ) +
            QPointF( m_zoom * 0.5, m_zoom * 0.5 );
        const qreal radius = std::clamp( m_zoom * 0.28, 5.0, 16.0 );
        painter.setPen( QPen( QColor( 98, 218, 131 ), 2.0 ) );
        painter.setBrush( QColor( 35, 82, 48, 220 ) );
        painter.drawEllipse( center, radius, radius );
        QTransform transform;
        transform.translate( center.x(), center.y() );
        transform.rotate( pMarker->yawDegrees );
        QPainterPath arrow;
        arrow.moveTo( radius * 0.9, 0.0 );
        arrow.lineTo( -radius * 0.45, -radius * 0.5 );
        arrow.lineTo( -radius * 0.45, radius * 0.5 );
        arrow.closeSubpath();
        painter.setBrush( QColor( 122, 240, 151 ) );
        painter.drawPath( transform.map( arrow ) );
    }

    if ( m_bDraggingRectangle || m_bDraggingSelection ) {
        const tile_map_grid_rect_t preview = rectangleFromDrag();
        QRectF previewRect(
            m_origin.x() + preview.x * m_zoom,
            m_origin.y() + preview.y * m_zoom,
            preview.nWidth * m_zoom,
            preview.nHeight * m_zoom );
        if ( !m_bDraggingSelection && m_tool != tile_canvas_tool_t::LINE )
            painter.fillRect( previewRect, QColor( 74, 157, 191, 72 ) );
        painter.setPen( QPen( m_bDraggingSelection ? m_preferences.selectionColor : QColor( 105, 203, 238 ), 2.0, Qt::DashLine ) );
        painter.setBrush( Qt::NoBrush );
        if ( m_tool == tile_canvas_tool_t::LINE ) {
            painter.drawLine( gridToScreen( m_dragAnchor ) + QPointF( m_zoom * 0.5, m_zoom * 0.5 ),
                              gridToScreen( m_dragCurrent ) + QPointF( m_zoom * 0.5, m_zoom * 0.5 ) );
        } else painter.drawRect( previewRect.adjusted( 1.0, 1.0, -1.0, -1.0 ) );
    }

    if ( contains( m_hoverCell ) && !m_bPanning && !m_bDraggingRectangle && !m_bDraggingSelection && !m_bMovingSelection ) {
        const QRectF hoverRect(
            gridToScreen( m_hoverCell ),
            QSizeF( m_zoom, m_zoom ) );
        QColor hoverColor( 225, 154, 68 );
        if ( m_tool == tile_canvas_tool_t::PAINT ) {
            const tile_map_material_definition_t material =
                CypherTileMapMaterial_Resolve( m_paint.nMaterialSlot );
            hoverColor = QColor::fromRgbF(
                material.colorR,
                material.colorG,
                material.colorB );
        } else if ( m_tool == tile_canvas_tool_t::ERASE ) {
            hoverColor = QColor( 226, 91, 82 );
        } else if ( m_tool == tile_canvas_tool_t::PLAYER_SPAWN ) {
            hoverColor = QColor( 95, 218, 126 );
        } else if ( m_tool == tile_canvas_tool_t::DOOR ) {
            const bool bRemoving = CypherTileMapDocument_DoorAt(
                pDocument,
                m_hoverCell,
                m_doorSide ) != nullptr;
            const bool bValidPlacement = DoorCanOccupyEdge(
                pDocument,
                m_hoverCell,
                m_doorSide );
            hoverColor = bRemoving || !bValidPlacement
                ? QColor( 226, 91, 82 ) : m_preferences.doorColor;
        }
        painter.setPen( QPen( hoverColor.lighter( 128 ), 1.5, Qt::DashLine ) );
        if ( m_preferences.showOrthoMaterials && m_tool == tile_canvas_tool_t::PAINT ) {
            TileOrthoMaterials_Paint( painter, hoverRect.adjusted( 2.0, 2.0, -2.0, -2.0 ),
                m_pMaterialCache, m_paint.nMaterialSlot, 0.30 );
        }
        painter.setBrush( QColor(
            hoverColor.red(), hoverColor.green(), hoverColor.blue(), 48 ) );
        painter.drawRect( hoverRect.adjusted( 2.0, 2.0, -2.0, -2.0 ) );
        if ( m_tool == tile_canvas_tool_t::PAINT || m_tool == tile_canvas_tool_t::RECTANGLE || m_tool == tile_canvas_tool_t::LINE )
            DrawStairFootprint( painter, hoverRect, m_paint.shape, m_paint.nStairSteps,
                m_preferences.stairColor, m_preferences.wireframeOrtho );
        if ( m_tool == tile_canvas_tool_t::ERASE ) {
            painter.drawLine(
                hoverRect.topLeft() + QPointF( 4.0, 4.0 ),
                hoverRect.bottomRight() - QPointF( 4.0, 4.0 ) );
            painter.drawLine(
                hoverRect.topRight() + QPointF( -4.0, 4.0 ),
                hoverRect.bottomLeft() + QPointF( 4.0, -4.0 ) );
        } else if ( m_tool == tile_canvas_tool_t::DOOR ) {
            DrawDoor(
                painter,
                hoverRect,
                m_doorSide,
                hoverColor,
                true );
        }
    }

    if ( m_bHasSelection || m_bDraggingSelection ) {
        QColor fill = m_preferences.selectionColor;
        fill.setAlpha( m_bDraggingSelection ? 56 : 36 );
        painter.setPen( QPen( m_preferences.selectionColor, 2.0 ) );
        painter.setBrush( fill );
        // Test only visible cells: sparse selections never tint their holes,
        // and a large selected map does not require drawing offscreen outlines.
        for ( int y = yBegin; y < yEnd; ++y ) {
            for ( int x = xBegin; x < xEnd; ++x ) {
                if ( !isPreviewSelectedCell( { x, y } ) ) continue;
                const QRectF selected( gridToScreen( { x, y } ), QSizeF( m_zoom, m_zoom ) );
                const qreal inset = std::min<qreal>( 1.5, m_zoom * 0.2 );
                painter.drawRect( selected.adjusted( inset, inset, -inset, -inset ) );
            }
        }
        if ( m_bHasSelection && !m_bDraggingSelection ) {
            const QRectF bounds( gridToScreen( { m_selectionRect.x, m_selectionRect.y } ),
                QSizeF( m_zoom * m_selectionRect.nWidth, m_zoom * m_selectionRect.nHeight ) );
            painter.setBrush( Qt::NoBrush );
            painter.setPen( QPen( m_preferences.selectionColor, 1.0, Qt::DashLine ) );
            painter.drawRect( bounds );
        }
    }
    if ( m_bMovingSelection && m_bMoveStarted ) {
        const int dx = m_dragCurrent.x - m_dragAnchor.x, dy = m_dragCurrent.y - m_dragAnchor.y;
        painter.setBrush( Qt::NoBrush );
        for ( const auto source : m_selectedCells ) {
            const tile_map_grid_coord_t destination{ source.x + dx, source.y + dy };
            const QRectF outline( gridToScreen( destination ), QSizeF( m_zoom, m_zoom ) );
            if ( !outline.intersects( rect() ) ) continue;
            painter.setPen( QPen( contains( destination ) ? m_preferences.selectionColor.lighter( 130 )
                : QColor( 230, 88, 80 ), 2.0, Qt::DashLine ) );
            painter.drawRect( outline );
        }
    }

    QStringList hudLines;
    if ( m_preferences.showViewMetrics ) hudLines = viewMetrics().split( QLatin1Char( '\n' ) );
    const QString material = materialDescription();
    if ( !material.isEmpty() ) hudLines.prepend( QString( material ).replace( QLatin1Char( '\n' ), QStringLiteral( " · " ) ) );
    if ( !hudLines.isEmpty() ) {
        const int labelHeight = painter.fontMetrics().height() * hudLines.size() + 10;
        const QRect labelBounds( 0, height() - labelHeight, width(), labelHeight );
        QColor background = m_preferences.canvasColor.darker( 135 ); background.setAlpha( 235 );
        painter.fillRect( labelBounds, background );
        painter.setPen( m_preferences.wireColor.lighter( 125 ) );
        for ( int i = 0; i < hudLines.size(); ++i )
            painter.drawText( QRect( 8, labelBounds.y() + 4 + i * painter.fontMetrics().height(),
                width() - 16, painter.fontMetrics().height() ), Qt::AlignLeft | Qt::AlignVCenter,
                painter.fontMetrics().elidedText( hudLines[i], Qt::ElideRight, width() - 16 ) );
    }

    if ( m_preferences.showCoordinateRulers ) {
        const QFontMetrics metrics( painter.font() );
        const double majorCells = TileEditorGrid_EffectiveMajorSpacingCells(
            gridStep, m_preferences.majorGridEvery );
        const double horizontalSpacing = TileEditorGrid_EffectiveRulerSpacing(
            majorCells, m_zoom,
            std::max( 64, metrics.horizontalAdvance( QStringLiteral( "-000000" ) ) + 12 ) );
        const double verticalSpacing = TileEditorGrid_EffectiveRulerSpacing(
            majorCells, m_zoom, metrics.height() + 8 );
        const double minimumX = std::max( 0.0, -m_origin.x() / m_zoom );
        const double maximumX = std::min<double>( pDocument->nWidth,
            ( width() - m_origin.x() ) / m_zoom );
        const double minimumY = std::max( 0.0, -m_origin.y() / m_zoom );
        const double maximumY = std::min<double>( pDocument->nHeight,
            ( height() - m_origin.y() ) / m_zoom );
        std::vector<tile_ruler_tick_t> horizontalTicks;
        std::vector<tile_ruler_tick_t> verticalTicks;
        auto appendTicks = [&]( double minimum, double maximum, double spacing,
                                bool horizontal, std::vector<tile_ruler_tick_t> &ticks ) {
            if ( !std::isfinite( minimum ) || !std::isfinite( maximum ) ||
                 !std::isfinite( spacing ) || spacing <= 0.0 || minimum > maximum ) return;
            const double first = std::ceil( minimum / spacing ) * spacing;
            const int count = static_cast<int>( std::clamp(
                std::floor( ( maximum - first ) / spacing ) + 1.0,
                0.0, static_cast<double>( TILE_RULER_MAX_TICKS ) ) );
            ticks.reserve( count );
            for ( int i = 0; i < count; ++i ) {
                const double cell = first + i * spacing;
                const qreal screen = horizontal
                    ? m_origin.x() + cell * m_zoom
                    : m_origin.y() + cell * m_zoom;
                if ( std::isfinite( screen ) ) {
                    ticks.push_back( { screen,
                        CoordinateLabel( cell * pDocument->nCellSize ) } );
                }
            }
        };
        appendTicks( minimumX, maximumX, horizontalSpacing, true, horizontalTicks );
        appendTicks( minimumY, maximumY, verticalSpacing, false, verticalTicks );
        DrawCoordinateRulers( painter, rect(), m_preferences,
                              tr( "X / Y" ),
                              std::move( horizontalTicks ),
                              std::move( verticalTicks ) );
    }

    if ( m_preferences.showViewAxes )
        DrawTopAxisIndicator( painter, rect(), m_preferences );

}

QString CypherTileCanvas::materialDescription() const
{
    if ( !m_preferences.showMaterialLabels || m_pBridge == nullptr ||
         !m_pBridge->isInitialized() ) return {};
    const auto *pDocument = m_pBridge->document();
    const bool hovered = CypherTileMapDocument_CellHasFloor( pDocument, m_hoverCell );
    const tile_map_grid_coord_t coordinate = hovered ? m_hoverCell : m_selection;
    if ( !hovered && !m_bHasSelection ) return {};
    const auto *pCell = CypherTileMapDocument_CellAt( pDocument, coordinate );
    if ( pCell == nullptr || !( pCell->flags & TILE_MAP_CELL_FLAG_FLOOR ) ) return {};
    return tr( "%1 cell %2, %3 · %4" ).arg( hovered ? tr( "Hover" ) : tr( "Selected" ) )
        .arg( coordinate.x ).arg( coordinate.y )
        .arg( TileOrthoMaterials_Describe( m_pMaterialCache, pCell->nMaterialSlot ) );
}

QString CypherTileCanvas::viewMetrics() const
{
    if ( m_pBridge == nullptr || !m_pBridge->isInitialized() ) return {};
    const auto &document = *m_pBridge->document();
    const int gridStep = TileEditorGrid_EffectiveSpacingCells( m_preferences.gridSpacingCells,
        m_zoom, m_preferences.adaptiveGrid, m_preferences.gridMinimumPixels );
    QString detail = tr( "XY · %1 px/cell · Cell %2 u · %3" ).arg( m_zoom, 0, 'f', 1 )
        .arg( document.nCellSize, 0, 'g', 4 )
        .arg( !m_preferences.showGrid ? tr( "Grid hidden" ) : tr( "Grid %1 %2 (%3 u)%4" )
            .arg( gridStep ).arg( gridStep == 1 ? tr( "cell" ) : tr( "cells" ) )
            .arg( gridStep * document.nCellSize, 0, 'g', 4 )
            .arg( m_preferences.adaptiveGrid ? tr( " · auto" ) : tr( " · fixed" ) ) );
    QString location = contains( m_hoverCell )
        ? tr( "Cell %1, %2 · X %3  Y %4 u" ).arg( m_hoverCell.x ).arg( m_hoverCell.y )
            .arg( m_hoverCell.x * document.nCellSize, 0, 'g', 5 )
            .arg( m_hoverCell.y * document.nCellSize, 0, 'g', 5 )
        : tr( "Map %1 × %2 cells" ).arg( document.nWidth ).arg( document.nHeight );
    if ( m_bHasSelection ) location += tr( " · Selected %1 × %2 bounds (%3 × %4 u) · %5 cells" )
        .arg( m_selectionRect.nWidth ).arg( m_selectionRect.nHeight )
        .arg( m_selectionRect.nWidth * document.nCellSize, 0, 'g', 4 )
        .arg( m_selectionRect.nHeight * document.nCellSize, 0, 'g', 4 )
        .arg( static_cast<qulonglong>( m_selectedCells.size() ) );
    return detail + QLatin1Char( '\n' ) + location;
}

void CypherTileCanvas::resizeEvent( QResizeEvent *pEvent )
{
    QWidget::resizeEvent( pEvent );
    if ( !m_bHasFit || !m_bUserNavigated ) fitToView();
    else if ( pEvent->oldSize().isValid() ) {
        m_origin += QPointF( ( pEvent->size().width() - pEvent->oldSize().width() ) * 0.5,
                             ( pEvent->size().height() - pEvent->oldSize().height() ) * 0.5 );
    }
}

void CypherTileCanvas::mousePressEvent( QMouseEvent *pEvent )
{
    setFocus( Qt::MouseFocusReason );
    // Additional buttons must not change ownership of a gesture already held.
    if ( m_bPanning ) { pEvent->accept(); return; }
    m_lastMouse = pEvent->position();
    const bool bPanGesture = pEvent->button() == Qt::MiddleButton ||
        pEvent->button() == Qt::RightButton ||
        ( pEvent->button() == Qt::LeftButton && ( m_bSpaceDown || m_tool == tile_canvas_tool_t::PAN ) );
    if ( bPanGesture ) {
        finishActiveEdit( false );
        m_bDraggingRectangle = false;
        m_bDraggingSelection = false;
        m_bMovingSelection = false;
        m_bMoveStarted = false;
        m_bPanning = true;
        m_panButton = pEvent->button();
        m_bContextMenuCandidate = pEvent->button() == Qt::RightButton &&
            pEvent->modifiers() == Qt::NoModifier;
        m_contextMenuPress = pEvent->position();
        m_contextMenuGlobal = pEvent->globalPosition().toPoint();
        setCursor( Qt::ClosedHandCursor );
        pEvent->accept();
        return;
    }
    if ( pEvent->button() != Qt::LeftButton || m_pBridge == nullptr ||
         !m_pBridge->isInitialized() ) {
        QWidget::mousePressEvent( pEvent );
        return;
    }

    const tile_map_grid_coord_t coordinate = screenToGrid( pEvent->position() );
    if ( !contains( coordinate ) ) {
        if ( m_tool == tile_canvas_tool_t::SELECT && pEvent->modifiers() == Qt::NoModifier ) clearSelection();
        return;
    }
    m_hoverCell = coordinate;

    if ( m_tool == tile_canvas_tool_t::EYEDROPPER ) {
        const tile_map_cell_t *pCell = CypherTileMapDocument_CellAt( m_pBridge->document(), coordinate );
        if ( pCell != nullptr && ( pCell->flags & TILE_MAP_CELL_FLAG_FLOOR ) != 0 ) {
            m_paint = { pCell->nFloorLevel, pCell->nWallHeightLevels, pCell->nMaterialSlot, pCell->shape, pCell->nStairSteps };
            selectCell( coordinate );
            if ( m_paintPickedCallback ) m_paintPickedCallback( m_paint );
            report( tr( "Picked material %1, floor level %2 and wall height %3" )
                .arg( m_paint.nMaterialSlot ).arg( m_paint.nFloorLevel ).arg( m_paint.nWallHeightLevels ) );
        } else report( tr( "Pick a floor cell to sample its material and dimensions" ) );
        return;
    }
    if ( m_tool == tile_canvas_tool_t::FILL ) { fillRegion( coordinate ); return; }

    if ( m_tool == tile_canvas_tool_t::SELECT ) {
        m_dragAnchor = coordinate;
        m_dragCurrent = coordinate;
        m_dragPressPosition = pEvent->position();
        m_bMoveStarted = false;
        m_selectionOperation = pEvent->modifiers().testFlag( Qt::AltModifier ) ? selection_operation_t::SUBTRACT
            : pEvent->modifiers().testFlag( Qt::ControlModifier ) ? selection_operation_t::TOGGLE
            : pEvent->modifiers().testFlag( Qt::ShiftModifier ) ? selection_operation_t::ADD
            : selection_operation_t::REPLACE;
        if ( pEvent->modifiers() == Qt::NoModifier && isSelectedCell( coordinate ) && isAuthoredCell( coordinate ) ) {
            m_bMovingSelection = true;
            update();
            return;
        }
        m_bDraggingSelection = true;
        update();
        return;
    }
    if ( m_tool == tile_canvas_tool_t::RECTANGLE || m_tool == tile_canvas_tool_t::LINE ) {
        m_dragAnchor = coordinate;
        m_dragCurrent = coordinate;
        m_bDraggingRectangle = true;
        update();
        return;
    }
    if ( m_tool == tile_canvas_tool_t::PLAYER_SPAWN ) {
        QString error;
        if ( m_pBridge->beginEdit( tr( "Place player spawn" ), &error ) &&
             m_pBridge->placePlayerSpawn( coordinate, 0.0f, &error ) &&
             m_pBridge->commitEdit( &error ) ) {
            selectCell( coordinate );
            notifyChanged();
        } else {
            m_pBridge->cancelEdit();
            report( error, true );
        }
        return;
    }
    if ( m_tool == tile_canvas_tool_t::DOOR ) {
        const bool bRemove = CypherTileMapDocument_DoorAt(
            m_pBridge->document(),
            coordinate,
            m_doorSide ) != nullptr;
        if ( !bRemove && !DoorCanOccupyEdge(
                 m_pBridge->document(), coordinate, m_doorSide ) ) {
            report( tr(
                "A door must be placed on an exposed edge of a floor cell." ),
                true );
            return;
        }
        QString error;
        const QString label = bRemove
            ? tr( "Remove door" )
            : tr( "Place door" );
        const bool bBegan = m_pBridge->beginEdit( label, &error );
        const bool bApplied = bBegan && ( bRemove
            ? m_pBridge->removeDoorAt( coordinate, m_doorSide, &error )
            : m_pBridge->placeDoor(
                  coordinate,
                  m_doorSide,
                  nullptr,
                  &error ) );
        if ( bApplied && m_pBridge->commitEdit( &error ) ) {
            selectCell( coordinate );
            notifyChanged();
            report( bRemove ? tr( "Door removed" ) : tr( "Door placed" ) );
        } else {
            if ( bBegan ) m_pBridge->cancelEdit();
            report( error, true );
        }
        return;
    }

    QString error;
    const QString label = m_tool == tile_canvas_tool_t::ERASE
        ? tr( "Erase cells" ) : tr( "Paint cells" );
    if ( !m_pBridge->beginEdit( label, &error ) ) {
        report( error, true );
        return;
    }
    m_bEditing = true;
    m_lastEdited = { -1, -1 };
    applyDragCell( coordinate );
}

void CypherTileCanvas::mouseMoveEvent( QMouseEvent *pEvent )
{
    if ( m_bPanning ) {
        if ( !pEvent->buttons().testFlag( m_panButton ) ) {
            cancelInteraction();
            return;
        }
        if ( m_bContextMenuCandidate &&
             ( pEvent->position() - m_contextMenuPress ).manhattanLength() >=
                 QApplication::startDragDistance() ) {
            m_bContextMenuCandidate = false;
        }
        m_origin += pEvent->position() - m_lastMouse;
        m_lastMouse = pEvent->position();
        m_bHasFit = true;
        m_bUserNavigated = true;
        update();
        return;
    }

    const tile_map_grid_coord_t coordinate = screenToGrid( pEvent->position() );
    const bool bHoverChanged = !SameCoordinate( coordinate, m_hoverCell );
    m_hoverCell = coordinate;
    if ( m_cursorCallback ) m_cursorCallback( contains( coordinate ), coordinate );
    if ( m_bEditing ) {
        if ( contains( coordinate ) ) applyDragCell( coordinate );
        return;
    }
    if ( m_bMovingSelection ) {
        if ( !pEvent->buttons().testFlag( Qt::LeftButton ) ) { cancelInteraction(); return; }
        m_bMoveStarted = m_bMoveStarted ||
            ( pEvent->position() - m_dragPressPosition ).manhattanLength() >= QApplication::startDragDistance();
        m_dragCurrent = coordinate;
        update();
        return;
    }
    if ( m_bDraggingRectangle || m_bDraggingSelection ) {
        updateDragEndpoint( pEvent->position() );
        update();
        return;
    }
    if ( bHoverChanged ) update();
}

void CypherTileCanvas::mouseReleaseEvent( QMouseEvent *pEvent )
{
    if ( m_bPanning ) {
        if ( pEvent->button() == m_panButton ) {
            const bool showContextMenu = m_bContextMenuCandidate &&
                pEvent->button() == Qt::RightButton;
            const QPoint globalPosition = m_contextMenuGlobal;
            m_bPanning = false;
            m_panButton = Qt::NoButton;
            m_bContextMenuCandidate = false;
            updateCursorShape();
            if ( showContextMenu && m_contextMenuCallback ) {
                QTimer::singleShot( 0, this, [this, globalPosition] {
                    if ( m_contextMenuCallback ) m_contextMenuCallback( globalPosition );
                } );
            }
        }
        pEvent->accept();
        return;
    }
    if ( pEvent->button() != Qt::LeftButton ) return;
    if ( m_bMovingSelection ) {
        const auto coordinate = screenToGrid( pEvent->position() );
        const bool moved = m_bMoveStarted ||
            ( pEvent->position() - m_dragPressPosition ).manhattanLength() >= QApplication::startDragDistance();
        const int dx = coordinate.x - m_dragAnchor.x, dy = coordinate.y - m_dragAnchor.y;
        m_bMovingSelection = false;
        m_bMoveStarted = false;
        if ( moved && ( dx != 0 || dy != 0 ) && m_moveSelectionCallback ) m_moveSelectionCallback( dx, dy, false );
        update();
        return;
    }
    if ( m_bDraggingSelection ) {
        updateDragEndpoint( pEvent->position() );
        m_bDraggingSelection = false;
        // Preserve a sparse set when clicking one of its cells. Empty selected
        // cells still support marquee drags because they cannot start a move.
        if ( m_selectionOperation != selection_operation_t::REPLACE ||
             !SameCoordinate( m_dragAnchor, m_dragCurrent ) || !isSelectedCell( m_dragAnchor ) )
            commitMarqueeSelection();
        update();
        return;
    }
    if ( m_bEditing ) {
        finishActiveEdit( true );
        return;
    }
    if ( m_bDraggingRectangle ) {
        updateDragEndpoint( pEvent->position() );
        m_bDraggingRectangle = false;
        if ( m_tool == tile_canvas_tool_t::LINE ) {
            QString error;
            if ( !m_pBridge->beginEdit( tr( "Paint line" ), &error ) ) { report( error, true ); return; }
            m_bEditing = true;
            m_lastEdited = { -1, -1 };
            applyDragCell( m_dragAnchor );
            if ( m_bEditing ) applyDragCell( m_dragCurrent );
            if ( m_bEditing ) finishActiveEdit( true );
            return;
        }
        const tile_map_grid_rect_t rectangle = rectangleFromDrag();
        QString error;
        if ( m_pBridge->beginEdit( tr( "Paint rectangle" ), &error ) &&
             m_pBridge->paintRect( rectangle, m_paint, &error ) &&
             m_pBridge->commitEdit( &error ) ) {
            setSelectionRect( rectangle );
            notifyChanged();
        } else {
            m_pBridge->cancelEdit();
            report( error, true );
        }
        update();
    }
}

void CypherTileCanvas::wheelEvent( QWheelEvent *pEvent )
{
    const QPoint numDegrees = pEvent->angleDelta();
    if ( numDegrees.y() == 0 ) return;
    const QPointF anchor = pEvent->position();
    const QPointF gridPoint = ( anchor - m_origin ) / m_zoom;
    const qreal steps = numDegrees.y() / 120.0;
    m_zoom = std::clamp(
        m_zoom * std::pow( 1.15, steps ),
        TILE_CANVAS_MIN_ZOOM,
        TILE_CANVAS_MAX_ZOOM );
    m_origin = anchor - gridPoint * m_zoom;
    m_bHasFit = true;
    m_bUserNavigated = true;
    if ( m_zoomCallback ) m_zoomCallback( m_zoom );
    update();
    pEvent->accept();
}

void CypherTileCanvas::keyPressEvent( QKeyEvent *pEvent )
{
    if ( pEvent->key() == Qt::Key_Space && !pEvent->isAutoRepeat() &&
         pEvent->modifiers() == Qt::NoModifier ) {
        m_bSpaceDown = true;
        updateCursorShape();
        pEvent->accept();
        return;
    }
    if ( pEvent->key() == Qt::Key_Escape ) {
        const bool bPending = m_bDraggingSelection || m_bMovingSelection || m_bDraggingRectangle || m_bEditing || m_bPanning;
        cancelInteraction();
        if ( !bPending ) clearSelection();
        report( bPending ? tr( "Tool operation cancelled" ) : tr( "Selection cleared" ) );
        pEvent->accept();
        return;
    }
    QWidget::keyPressEvent( pEvent );
}

void CypherTileCanvas::keyReleaseEvent( QKeyEvent *pEvent )
{
    if ( pEvent->key() == Qt::Key_Space && !pEvent->isAutoRepeat() && m_bSpaceDown ) {
        m_bSpaceDown = false;
        updateCursorShape();
        pEvent->accept();
        return;
    }
    QWidget::keyReleaseEvent( pEvent );
}

void CypherTileCanvas::focusOutEvent( QFocusEvent *pEvent )
{
    // A missed key-release after switching windows must never leave the canvas
    // in its temporary Space-to-pan mode.
    cancelInteraction();
    QWidget::focusOutEvent( pEvent );
}

bool CypherTileCanvas::event( QEvent *pEvent )
{
    if ( pEvent->type() == QEvent::ToolTip ) {
        const QString description = materialDescription();
        if ( !description.isEmpty() ) {
            const auto *pHelp = static_cast<QHelpEvent *>( pEvent );
            QToolTip::showText( pHelp->globalPos(), description, this );
            pEvent->accept();
            return true;
        }
    }
    if ( pEvent->type() == QEvent::Hide || pEvent->type() == QEvent::WindowDeactivate ||
         pEvent->type() == QEvent::UngrabMouse ) cancelInteraction();
    return QWidget::event( pEvent );
}

void CypherTileCanvas::cancelInteraction()
{
    finishActiveEdit( false );
    m_bDraggingRectangle = false;
    m_bDraggingSelection = false;
    m_bMovingSelection = false;
    m_bMoveStarted = false;
    m_bPanning = false;
    m_panButton = Qt::NoButton;
    m_bContextMenuCandidate = false;
    m_bSpaceDown = false;
    updateCursorShape();
    update();
}

void CypherTileCanvas::updateDragEndpoint( const QPointF &position )
{
    if ( m_pBridge == nullptr || !m_pBridge->isInitialized() ) return;
    const auto coordinate = screenToGrid( position );
    const auto *pDocument = m_pBridge->document();
    m_dragCurrent.x = std::clamp( coordinate.x, 0, static_cast<int>( pDocument->nWidth ) - 1 );
    m_dragCurrent.y = std::clamp( coordinate.y, 0, static_cast<int>( pDocument->nHeight ) - 1 );
}

void CypherTileCanvas::leaveEvent( QEvent *pEvent )
{
    if ( m_cursorCallback ) m_cursorCallback( false, m_hoverCell );
    m_hoverCell = { -1, -1 };
    update();
    QWidget::leaveEvent( pEvent );
}

QPointF CypherTileCanvas::gridToScreen( tile_map_grid_coord_t coordinate ) const
{
    return m_origin + QPointF( coordinate.x * m_zoom, coordinate.y * m_zoom );
}

tile_map_grid_coord_t CypherTileCanvas::screenToGrid(
    const QPointF &position ) const
{
    return {
        static_cast<i32>( std::floor( ( position.x() - m_origin.x() ) / m_zoom ) ),
        static_cast<i32>( std::floor( ( position.y() - m_origin.y() ) / m_zoom ) )
    };
}

bool CypherTileCanvas::contains( tile_map_grid_coord_t coordinate ) const
{
    return m_pBridge != nullptr && m_pBridge->isInitialized() &&
        CypherTileMapDocument_ContainsCell( m_pBridge->document(), coordinate );
}

bool CypherTileCanvas::isSelectedCell( tile_map_grid_coord_t coordinate ) const
{
    return std::binary_search( m_selectedCells.begin(), m_selectedCells.end(), coordinate, CoordinateBefore );
}

bool CypherTileCanvas::isAuthoredCell( tile_map_grid_coord_t coordinate ) const
{
    if ( !contains( coordinate ) ) return false;
    const auto *document = m_pBridge->document();
    if ( CypherTileMapDocument_CellHasFloor( document, coordinate ) ) return true;
    for ( usize i = 0; i < Vector_Count( &document->markers ); ++i )
        if ( SameCoordinate( document->markers.pData[i].cell, coordinate ) ) return true;
    return false;
}

bool CypherTileCanvas::isPreviewSelectedCell( tile_map_grid_coord_t coordinate ) const
{
    const bool selected = isSelectedCell( coordinate );
    if ( !m_bDraggingSelection ) return selected;
    const auto rectangle = rectangleFromDrag();
    const bool inside = coordinate.x >= rectangle.x && coordinate.y >= rectangle.y &&
        static_cast<u32>( coordinate.x - rectangle.x ) < rectangle.nWidth &&
        static_cast<u32>( coordinate.y - rectangle.y ) < rectangle.nHeight;
    switch ( m_selectionOperation ) {
        case selection_operation_t::ADD: return selected || inside;
        case selection_operation_t::TOGGLE: return selected != inside;
        case selection_operation_t::SUBTRACT: return selected && !inside;
        case selection_operation_t::REPLACE: return inside;
    }
    return selected;
}

void CypherTileCanvas::commitMarqueeSelection()
{
    const auto rectangle = rectangleFromDrag();
    std::vector<tile_map_grid_coord_t> cells;
    cells.reserve( static_cast<size_t>( rectangle.nWidth ) * rectangle.nHeight );
    for ( u32 y = 0; y < rectangle.nHeight; ++y )
        for ( u32 x = 0; x < rectangle.nWidth; ++x )
            cells.push_back( { rectangle.x + static_cast<i32>( x ), rectangle.y + static_cast<i32>( y ) } );
    combineSelectedCells( cells, m_selectionOperation );
}

tile_map_grid_rect_t CypherTileCanvas::rectangleFromDrag() const
{
    const i32 xMin = std::min( m_dragAnchor.x, m_dragCurrent.x );
    const i32 yMin = std::min( m_dragAnchor.y, m_dragCurrent.y );
    const i32 xMax = std::max( m_dragAnchor.x, m_dragCurrent.x );
    const i32 yMax = std::max( m_dragAnchor.y, m_dragCurrent.y );
    return {
        xMin,
        yMin,
        static_cast<u32>( xMax - xMin + 1 ),
        static_cast<u32>( yMax - yMin + 1 )
    };
}

void CypherTileCanvas::applyDragCell( tile_map_grid_coord_t coordinate )
{
    if ( SameCoordinate( coordinate, m_lastEdited ) ) return;
    QString error;
    auto applyOne = [this, &error]( tile_map_grid_coord_t cell ) {
        return m_tool == tile_canvas_tool_t::ERASE
            ? m_pBridge->eraseCell( cell, &error )
            : m_pBridge->paintCell( cell, m_paint, &error );
    };

    if ( m_lastEdited.x < 0 || m_lastEdited.y < 0 ) {
        if ( !applyOne( coordinate ) ) {
            finishActiveEdit( false );
            report( error, true );
            return;
        }
    } else {
        // Rasterize the pointer segment in grid space so a quick drag cannot
        // skip cells between sparsely delivered mouse-move events.
        int x = m_lastEdited.x;
        int y = m_lastEdited.y;
        const int dx = std::abs( coordinate.x - x );
        const int sx = x < coordinate.x ? 1 : -1;
        const int dy = -std::abs( coordinate.y - y );
        const int sy = y < coordinate.y ? 1 : -1;
        int accumulated = dx + dy;
        while ( x != coordinate.x || y != coordinate.y ) {
            const int twice = accumulated * 2;
            if ( twice >= dy ) {
                accumulated += dy;
                x += sx;
            }
            if ( twice <= dx ) {
                accumulated += dx;
                y += sy;
            }
            if ( !applyOne( { x, y } ) ) {
                finishActiveEdit( false );
                report( error, true );
                return;
            }
        }
    }
    m_lastEdited = coordinate;
    selectCell( coordinate );
    if ( m_previewChangedCallback ) m_previewChangedCallback();
    update();
}

void CypherTileCanvas::fillRegion( tile_map_grid_coord_t coordinate )
{
    const tile_map_document_t *pDocument = m_pBridge->document();
    const tile_map_cell_t source = *CypherTileMapDocument_CellAt( pDocument, coordinate );
    const bool bSourceFloor = ( source.flags & TILE_MAP_CELL_FLAG_FLOOR ) != 0;
    if ( bSourceFloor && source.nFloorLevel == m_paint.nFloorLevel &&
         source.nWallHeightLevels == m_paint.nWallHeightLevels &&
         source.nMaterialSlot == m_paint.nMaterialSlot && source.shape == m_paint.shape &&
         source.nStairSteps == m_paint.nStairSteps ) return;

    // Iterative four-connected fill avoids recursion limits. Match the whole
    // cell surface so filling a material does not cross elevation boundaries.
    std::vector<bool> visited( static_cast<usize>( pDocument->nWidth ) * pDocument->nHeight, false );
    std::vector<tile_map_grid_coord_t> cells;
    auto enqueue = [&]( tile_map_grid_coord_t cell ) {
        if ( !contains( cell ) ) return;
        const usize index = static_cast<usize>( cell.y ) * pDocument->nWidth + cell.x;
        if ( visited[index] ) return;
        visited[index] = true;
        const tile_map_cell_t &candidate = *CypherTileMapDocument_CellAt( pDocument, cell );
        const bool bFloor = ( candidate.flags & TILE_MAP_CELL_FLAG_FLOOR ) != 0;
        if ( bFloor != bSourceFloor ) return;
        if ( bFloor && ( candidate.nFloorLevel != source.nFloorLevel ||
             candidate.nWallHeightLevels != source.nWallHeightLevels ||
             candidate.nMaterialSlot != source.nMaterialSlot || candidate.shape != source.shape ||
             candidate.nStairSteps != source.nStairSteps ) ) return;
        cells.push_back( cell );
    };
    enqueue( coordinate );
    for ( usize i = 0; i < cells.size(); ++i ) {
        if ( cells.size() > TILE_MAP_MAX_ACTIVE_CELLS ) {
            report( tr( "Fill exceeds the active-cell limit; use a smaller region" ), true );
            return;
        }
        const auto cell = cells[i];
        enqueue( { cell.x - 1, cell.y } );
        enqueue( { cell.x + 1, cell.y } );
        enqueue( { cell.x, cell.y - 1 } );
        enqueue( { cell.x, cell.y + 1 } );
    }
    QString error;
    if ( !m_pBridge->beginEdit( tr( "Fill region" ), &error ) ) { report( error, true ); return; }
    for ( const auto cell : cells ) {
        if ( !m_pBridge->paintCell( cell, m_paint, &error ) ) {
            m_pBridge->cancelEdit();
            report( error, true );
            return;
        }
    }
    if ( !m_pBridge->commitEdit( &error ) ) {
        m_pBridge->cancelEdit();
        report( error, true );
        return;
    }
    selectCell( coordinate );
    notifyChanged();
    report( tr( "Filled %1 cells" ).arg( cells.size() ) );
}

void CypherTileCanvas::finishActiveEdit( bool bCommit )
{
    if ( !m_bEditing || m_pBridge == nullptr ) return;
    m_bEditing = false;
    if ( !bCommit ) {
        m_pBridge->cancelEdit();
        if ( m_previewChangedCallback ) m_previewChangedCallback();
        update();
        return;
    }
    QString error;
    if ( !m_pBridge->commitEdit( &error ) ) {
        m_pBridge->cancelEdit();
        if ( m_previewChangedCallback ) m_previewChangedCallback();
        report( error, true );
        return;
    }
    notifyChanged();
}

void CypherTileCanvas::notifySelection()
{
    if ( m_selectionCallback ) {
        m_selectionCallback( m_bHasSelection, m_selection );
    }
}

void CypherTileCanvas::notifyChanged()
{
    update();
    if ( m_changedCallback ) m_changedCallback();
}

void CypherTileCanvas::report( const QString &message, bool bError )
{
    if ( m_statusCallback ) m_statusCallback( message, bError );
}

void CypherTileCanvas::updateCursorShape()
{
    if ( m_bPanning ) {
        setCursor( Qt::ClosedHandCursor );
    } else if ( m_bSpaceDown || m_tool == tile_canvas_tool_t::PAN ) {
        setCursor( Qt::OpenHandCursor );
    } else if ( m_tool == tile_canvas_tool_t::SELECT ) {
        setCursor( Qt::ArrowCursor );
    } else {
        setCursor( Qt::CrossCursor );
    }
}

} // namespace cypher::tools::tile_editor
