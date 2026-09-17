//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileOrthoView.cpp
//  Purpose: Projects tile-map boxes into interactive front and side views.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherTileOrthoView.h"
#include "CypherTileCanvas.h"
#include "CypherTileGrid.h"
#include "CypherTileOrthoMaterials.h"

#include "Core/CypherTileMapMaterials.h"

#include <QMouseEvent>
#include <QApplication>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QHelpEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QTimer>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <limits>
#include <utility>
#include <vector>

namespace cypher::tools::tile_editor
{

namespace
{

constexpr qreal MIN_PIXELS_PER_UNIT = 0.0001;
constexpr qreal MAX_PIXELS_PER_UNIT = 1024.0;
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

void DrawOrthoAxisIndicator(
    QPainter &painter,
    const QRect &viewport,
    const tile_editor_preferences_t &preferences,
    tile_editor_ortho_plane_t plane )
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
    const QPointF origin = panel.bottomLeft() + QPointF( 21.0, -20.0 );
    constexpr qreal axisLength = 35.0;

    QColor panelColor = preferences.panelColor;
    panelColor.setAlpha( 218 );
    QColor panelEdge = preferences.majorGridColor;
    panelEdge.setAlpha( 220 );
    const bool front = plane == tile_editor_ortho_plane_t::FRONT;
    const QColor horizontalColor = front
        ? preferences.axisXColor : preferences.axisYColor;
    const QColor hiddenColor = front
        ? preferences.axisYColor : preferences.axisXColor;
    const QString horizontalLabel = front
        ? QStringLiteral( "X" ) : QStringLiteral( "Y" );
    const QString hiddenLabel = front
        ? QStringLiteral( "Y" ) : QStringLiteral( "X" );

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
        QFont font = painter.font();
        font.setBold( true );
        painter.setFont( font );
        painter.setPen( color );
        painter.drawText( QRectF( endpoint.x() - 7.0, endpoint.y() - 9.0,
                                  14.0, 18.0 ), Qt::AlignCenter, label );
    };

    drawArrow( { 1.0, 0.0 }, horizontalColor, horizontalLabel );
    drawArrow( { 0.0, -1.0 }, preferences.axisZColor, QStringLiteral( "Z" ) );

    // The third axis is perpendicular to this projection. The center dot and
    // label keep the complete XYZ color language visible without implying that
    // the hidden coordinate can be chosen from this viewport.
    painter.setPen( QPen( QColor( 0, 0, 0, 180 ), 5.0 ) );
    painter.setBrush( hiddenColor );
    painter.drawEllipse( origin, 5.0, 5.0 );
    painter.setPen( Qt::NoPen );
    painter.setBrush( hiddenColor.lighter( 125 ) );
    painter.drawEllipse( origin, 1.8, 1.8 );
    painter.setPen( hiddenColor );
    painter.drawText( QRectF( origin.x() - 17.0, origin.y() + 3.0,
                              14.0, 16.0 ), Qt::AlignCenter, hiddenLabel );
    painter.restore();
}

bool CellBefore( tile_map_grid_coord_t left, tile_map_grid_coord_t right )
{
    return left.y < right.y || ( left.y == right.y && left.x < right.x );
}

bool SameCell( tile_map_grid_coord_t left, tile_map_grid_coord_t right )
{
    return left.x == right.x && left.y == right.y;
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
    const auto *authored = pDocument != nullptr
        ? CypherTileMapDocument_CellAt( pDocument, cell ) : nullptr;
    return authored != nullptr &&
        CypherTileMapMarkerSide_IsCardinal( side ) &&
        ( authored->flags & TILE_MAP_CELL_FLAG_FLOOR ) != 0u &&
        authored->shape == tile_map_cell_shape_t::FLAT &&
        !CypherTileMapDocument_CellHasFloor(
            pDocument, DoorNeighbor( cell, side ) );
}

} // namespace

CypherTileOrthoView::CypherTileOrthoView(
    tile_editor_ortho_plane_t plane,
    QWidget *pParent )
    : QWidget( pParent ), m_plane( plane )
{
    const bool bFront = plane == tile_editor_ortho_plane_t::FRONT;
    setObjectName( bFront ? QStringLiteral( "CypherTileFrontView" )
                         : QStringLiteral( "CypherTileSideView" ) );
    setAccessibleName( bFront ? tr( "Front XZ view" ) : tr( "Side YZ view" ) );
    setToolTip( tr( "Left-click geometry to select its cell. Middle/right drag or Space + left drag pans; wheel zooms at the pointer. H selects the Pan tool." ) );
    setFocusPolicy( Qt::StrongFocus );
    // Continue receiving movement after a missed release, so the held-button
    // check can terminate a stale pan instead of leaving its cursor captured.
    setMouseTracking( true );
    setMinimumSize( 160, 120 );
    updateInteractionDescription();
    m_bGeometryInitialized = CypherTileMapGeometry_Init(
        &m_geometry, Allocator_GetSystem() ) == tile_map_document_status_t::OK;
    if ( !m_bGeometryInitialized ) {
        m_geometryError = tr( "Unable to allocate orthographic geometry." );
    }
}

CypherTileOrthoView::~CypherTileOrthoView()
{
    if ( m_bGeometryInitialized ) CypherTileMapGeometry_Shutdown( &m_geometry );
}

void CypherTileOrthoView::setDocumentBridge(
    CypherTileDocumentBridge *pBridge )
{
    finishAuthoring( false );
    m_bSpaceHeld = false;
    stopPanning();
    cancelSelectionDrag();
    m_pBridge = pBridge;
    if ( m_preferences.frameMapOnOpen || !m_bHasFit ) {
        m_bHasFit = false;
        m_bUserNavigated = false;
    }
    m_selectedCells.clear();
    refreshDocument();
}

void CypherTileOrthoView::setPreferences(
    const tile_editor_preferences_t &preferences )
{
    const bool scaleChanged = m_preferences.emptyViewCellPixels != preferences.emptyViewCellPixels;
    m_preferences = preferences;
    if ( scaleChanged && !m_bUserNavigated ) fitToView();
    update();
}

void CypherTileOrthoView::setMaterialCache( const tile_ortho_material_cache_t *pCache )
{
    m_pMaterialCache = pCache;
    update();
}

void CypherTileOrthoView::refreshDocument()
{
    m_drawOrder.clear();
    if ( !m_bGeometryInitialized || m_pBridge == nullptr ||
         !m_pBridge->isInitialized() ) {
        update();
        return;
    }

    const tile_map_document_status_t status = CypherTileMapGeometry_Build(
        m_pBridge->document(), {}, &m_geometry );
    if ( status != tile_map_document_status_t::OK ) {
        m_geometryError = tr( "Geometry refresh failed: %1" ).arg(
            QString::fromLatin1( CypherTileMapDocument_StatusName( status ) ) );
        update();
        return;
    }
    m_geometryError.clear();
    m_drawOrder.resize( Vector_Count( &m_geometry.boxes ) );
    std::iota( m_drawOrder.begin(), m_drawOrder.end(), usize{ 0u } );
    // Front looks along +Y; side looks along -X. Back-to-front ordering
    // makes picking agree with the last (nearest) projected box drawn.
    std::stable_sort( m_drawOrder.begin(), m_drawOrder.end(),
        [this]( usize a, usize b ) {
            return depth( m_geometry.boxes.pData[a] ) >
                   depth( m_geometry.boxes.pData[b] );
        } );
    if ( !m_bHasFit ) fitToView();
    update();
}

void CypherTileOrthoView::fitToView()
{
    if ( m_pBridge == nullptr || !m_pBridge->isInitialized() ||
         width() <= 0 || height() <= 0 ) return;

    const tile_map_document_t &document = *m_pBridge->document();
    const bool bFront = m_plane == tile_editor_ortho_plane_t::FRONT;
    if ( !m_geometry.bHasBounds || !m_geometryError.isEmpty() ) {
        m_pixelsPerUnit = std::clamp<qreal>( m_preferences.emptyViewCellPixels / document.nCellSize,
            MIN_PIXELS_PER_UNIT, MAX_PIXELS_PER_UNIT );
        m_origin = QPointF( 40.0, ( height() - 40.0 ) * 0.72 );
    } else {
        const qreal minimumHorizontal = bFront ? m_geometry.boundsMinX : m_geometry.boundsMinY;
        const qreal maximumHorizontal = bFront ? m_geometry.boundsMaxX : m_geometry.boundsMaxY;
        const qreal minimumHeight = m_geometry.boundsMinZ;
        const qreal maximumHeight = m_geometry.boundsMaxZ;
        const qreal horizontalExtent = std::max<qreal>( document.nCellSize, maximumHorizontal - minimumHorizontal );
        const qreal heightExtent = std::max<qreal>( document.nLevelHeight, maximumHeight - minimumHeight );
        const qreal availableWidth = std::max( 20, width() - 80 );
        const qreal availableHeight = std::max( 20, height() - 104 );
        m_pixelsPerUnit = std::clamp(
            std::min( availableWidth / horizontalExtent, availableHeight / heightExtent ),
            MIN_PIXELS_PER_UNIT, MAX_PIXELS_PER_UNIT );
        const qreal centerHorizontal = ( minimumHorizontal + maximumHorizontal ) * 0.5;
        const qreal centerHeight = ( minimumHeight + maximumHeight ) * 0.5;
        m_origin = QPointF( width() * 0.5 - centerHorizontal * m_pixelsPerUnit,
                           ( height() - 24.0 ) * 0.5 + centerHeight * m_pixelsPerUnit );
    }
    m_bHasFit = true;
    m_bUserNavigated = false;
    update();
}

void CypherTileOrthoView::setSelection(
    bool bHasSelection,
    tile_map_grid_coord_t coordinate )
{
    setSelectedCells( bHasSelection ? std::span( &coordinate, 1 ) : std::span<const tile_map_grid_coord_t>{} );
}

void CypherTileOrthoView::setSelectionRect(
    bool bHasSelection, tile_map_grid_rect_t region )
{
    std::vector<tile_map_grid_coord_t> cells;
    if ( bHasSelection ) {
        const qint64 x0 = std::clamp<qint64>( region.x, 0, TILE_MAP_MAX_WIDTH );
        const qint64 y0 = std::clamp<qint64>( region.y, 0, TILE_MAP_MAX_HEIGHT );
        const qint64 x1 = std::clamp<qint64>( static_cast<qint64>( region.x ) + region.nWidth, 0, TILE_MAP_MAX_WIDTH );
        const qint64 y1 = std::clamp<qint64>( static_cast<qint64>( region.y ) + region.nHeight, 0, TILE_MAP_MAX_HEIGHT );
        for ( qint64 y = y0; y < y1; ++y )
            for ( qint64 x = x0; x < x1; ++x ) cells.push_back( { static_cast<i32>( x ), static_cast<i32>( y ) } );
    }
    setSelectedCells( cells );
}

void CypherTileOrthoView::setSelectedCells( std::span<const tile_map_grid_coord_t> cells )
{
    std::vector<tile_map_grid_coord_t> canonical( cells.begin(), cells.end() );
    std::erase_if( canonical, []( tile_map_grid_coord_t cell ) {
        return cell.x < 0 || cell.y < 0 || cell.x >= static_cast<i32>( TILE_MAP_MAX_WIDTH ) ||
            cell.y >= static_cast<i32>( TILE_MAP_MAX_HEIGHT );
    } );
    std::sort( canonical.begin(), canonical.end(), CellBefore );
    canonical.erase( std::unique( canonical.begin(), canonical.end(), SameCell ), canonical.end() );
    if ( std::equal( canonical.begin(), canonical.end(), m_selectedCells.begin(), m_selectedCells.end(), SameCell ) ) return;
    m_selectedCells = std::move( canonical );
    update();
}

void CypherTileOrthoView::setSelectionCallback( selection_callback_t callback )
{
    m_selectionCallback = std::move( callback );
}

void CypherTileOrthoView::setMultiSelectionCallback( multi_selection_callback_t callback )
{
    m_multiSelectionCallback = std::move( callback );
}

void CypherTileOrthoView::setSelectionCellsCallback( selection_cells_callback_t callback )
{
    m_selectionCellsCallback = std::move( callback );
}

void CypherTileOrthoView::setTool( tile_canvas_tool_t tool )
{
    if ( m_tool == tool ) return;
    finishAuthoring( false );
    cancelSelectionDrag();
    m_tool = tool;
    m_bPanToolEnabled = tool == tile_canvas_tool_t::PAN;
    updateInteractionDescription();
    updatePanCursor();
    update();
}

void CypherTileOrthoView::setPaint( const tile_map_paint_t &paint ) { m_paint = paint; update(); }
void CypherTileOrthoView::setDoorSide( tile_map_marker_side_t side ) { m_doorSide = side; }
void CypherTileOrthoView::setChangedCallback( std::function<void()> callback ) { m_changedCallback = std::move( callback ); }
void CypherTileOrthoView::setPreviewChangedCallback( std::function<void()> callback ) { m_previewChangedCallback = std::move( callback ); }
void CypherTileOrthoView::setPaintPickedCallback( std::function<void( const tile_map_paint_t & )> callback ) { m_paintPickedCallback = std::move( callback ); }
void CypherTileOrthoView::setStatusCallback( std::function<void( const QString &, bool )> callback ) { m_statusCallback = std::move( callback ); }
void CypherTileOrthoView::setContextMenuCallback( context_menu_callback_t callback ) { m_contextMenuCallback = std::move( callback ); }

void CypherTileOrthoView::setConstructionCell( tile_map_grid_coord_t cell )
{
    m_constructionCell = cell;
    updateInteractionDescription();
    update();
}

bool CypherTileOrthoView::supportsProjectedAuthoring() const
{
    switch ( m_tool ) {
        case tile_canvas_tool_t::PAINT:
        case tile_canvas_tool_t::ERASE:
        case tile_canvas_tool_t::RECTANGLE:
        case tile_canvas_tool_t::LINE:
        case tile_canvas_tool_t::PLAYER_SPAWN:
        case tile_canvas_tool_t::DOOR:
        case tile_canvas_tool_t::EYEDROPPER:
            return true;
        case tile_canvas_tool_t::SELECT:
        case tile_canvas_tool_t::PAN:
        case tile_canvas_tool_t::FILL:
            return false;
    }
    return false;
}

bool CypherTileOrthoView::floorLevelAt( const QPointF &position, i16 &level ) const
{
    if ( m_pBridge == nullptr || !m_pBridge->isInitialized() ) return false;
    const qreal projected = std::round( ( m_origin.y() - position.y() ) /
        ( m_pixelsPerUnit * m_pBridge->document()->nLevelHeight ) );
    if ( !std::isfinite( projected ) || projected < std::numeric_limits<i16>::min() ||
         projected > std::numeric_limits<i16>::max() - 1 ) return false;
    level = static_cast<i16>( projected );
    return true;
}

QString CypherTileOrthoView::constructionSliceDescription( const QPointF *pPosition ) const
{
    const bool front = m_plane == tile_editor_ortho_plane_t::FRONT;
    const int slice = front ? m_constructionCell.y : m_constructionCell.x;
    QString description = front
        ? tr( "AUTHOR X/Z · FIXED Y = %1" ).arg( slice )
        : tr( "AUTHOR Y/Z · FIXED X = %1" ).arg( slice );
    i16 level{};
    if ( m_bEditing )
        description += tr( " · STROKE FLOOR Z = %1" ).arg( m_strokePaint.nFloorLevel );
    else if ( pPosition != nullptr && floorLevelAt( *pPosition, level ) )
        description += tr( " · FLOOR Z = %1" ).arg( level );
    return description;
}

void CypherTileOrthoView::updateInteractionDescription()
{
    const bool front = m_plane == tile_editor_ortho_plane_t::FRONT;
    const int slice = front ? m_constructionCell.y : m_constructionCell.x;
    QString description;
    if ( m_tool == tile_canvas_tool_t::SELECT ) {
        description = tr( "Click geometry to select its XY cell. Shift/Ctrl/Alt modifies selection. "
            "Middle/right drag or Space + left drag pans; wheel zooms at the pointer." );
    } else if ( m_tool == tile_canvas_tool_t::PAN ) {
        description = tr( "Left, middle or right drag pans this projection; wheel zooms at the pointer." );
    } else if ( m_tool == tile_canvas_tool_t::FILL ) {
        description = tr( "Fill requires the Top view because hidden-axis connectivity is ambiguous. "
            "Middle/right drag or Space + left drag pans; wheel zooms at the pointer." );
    } else {
        description = tr( "This view authors %1 while keeping the hidden %2 coordinate fixed at %3. "
            "Vertical position sets floor level Z when the stroke begins; dragging edits only the visible horizontal axis. "
            "Tiles cannot stack, so painting replaces that XY cell's elevation. "
            "Shift/Ctrl/Alt click modifies selection. Middle/right drag or Space + left drag pans; wheel zooms at the pointer." )
            .arg( front ? tr( "X/Z" ) : tr( "Y/Z" ), front ? tr( "Y" ) : tr( "X" ) )
            .arg( slice );
    }
    setToolTip( description );
    setAccessibleDescription( description );
    setProperty( "constructionSlice", constructionSliceDescription() );
}

bool CypherTileOrthoView::constructionCellAt( const QPointF &position, tile_map_grid_coord_t &cell ) const
{
    if ( m_pBridge == nullptr || !m_pBridge->isInitialized() ) return false;
    const auto *document = m_pBridge->document();
    const qreal horizontal = std::floor( ( position.x() - m_origin.x() ) / ( m_pixelsPerUnit * document->nCellSize ) );
    const bool front = m_plane == tile_editor_ortho_plane_t::FRONT;
    const u32 limit = front ? document->nWidth : document->nHeight;
    if ( !std::isfinite( horizontal ) || horizontal < 0 || horizontal >= limit ) return false;
    cell = front ? tile_map_grid_coord_t{ static_cast<i32>( horizontal ), m_constructionCell.y }
                 : tile_map_grid_coord_t{ m_constructionCell.x, static_cast<i32>( horizontal ) };
    return CypherTileMapDocument_ContainsCell( document, cell );
}

void CypherTileOrthoView::notifyAuthoringPreview()
{
    refreshDocument();
    if ( m_previewChangedCallback ) m_previewChangedCallback();
}

void CypherTileOrthoView::beginAuthoring( const QPointF &position )
{
    tile_map_grid_coord_t cell;
    if ( !constructionCellAt( position, cell ) ) {
        if ( m_statusCallback ) m_statusCallback( tr( "The construction cell lies outside this map." ), true );
        return;
    }
    if ( m_tool == tile_canvas_tool_t::EYEDROPPER ) {
        const auto *picked = CypherTileMapDocument_CellAt( m_pBridge->document(), cell );
        if ( picked != nullptr && ( picked->flags & TILE_MAP_CELL_FLAG_FLOOR ) ) {
            m_paint = { picked->nFloorLevel, picked->nWallHeightLevels, picked->nMaterialSlot, picked->shape, picked->nStairSteps };
            if ( m_paintPickedCallback ) m_paintPickedCallback( m_paint );
        } else if ( m_statusCallback ) m_statusCallback( tr( "Pick an occupied construction cell to sample its brush." ), false );
        return;
    }
    if ( m_tool != tile_canvas_tool_t::PAINT && m_tool != tile_canvas_tool_t::ERASE &&
         m_tool != tile_canvas_tool_t::RECTANGLE && m_tool != tile_canvas_tool_t::LINE &&
         m_tool != tile_canvas_tool_t::PLAYER_SPAWN && m_tool != tile_canvas_tool_t::DOOR ) {
        if ( m_statusCallback ) m_statusCallback( tr( "This tool uses the Top view. Use Paint, Erase, Rectangle, Line, Spawn or Door here." ), false );
        return;
    }
    m_strokePaint = m_paint;
    i16 level{};
    if ( !floorLevelAt( position, level ) ) {
        if ( m_statusCallback ) m_statusCallback( tr( "The chosen floor level is outside the supported range." ), true );
        return;
    }
    m_strokePaint.nFloorLevel = level;
    QString error;
    const bool removeDoor = m_tool == tile_canvas_tool_t::DOOR &&
        CypherTileMapDocument_DoorAt( m_pBridge->document(), cell, m_doorSide ) != nullptr;
    if ( m_tool == tile_canvas_tool_t::DOOR && !removeDoor &&
         !DoorCanOccupyEdge( m_pBridge->document(), cell, m_doorSide ) ) {
        if ( m_statusCallback ) m_statusCallback(
            tr( "A door must be placed on an exposed edge of a flat floor cell in the fixed construction slice." ), true );
        return;
    }
    const QString editLabel = m_tool == tile_canvas_tool_t::ERASE
        ? tr( "Erase in orthographic view" )
        : m_tool == tile_canvas_tool_t::PLAYER_SPAWN
            ? tr( "Place player spawn" )
            : m_tool == tile_canvas_tool_t::DOOR
                ? ( removeDoor ? tr( "Remove door" ) : tr( "Place door" ) )
                : tr( "Author in orthographic view" );
    if ( !m_pBridge->beginEdit( editLabel, &error ) ) {
        if ( m_statusCallback ) m_statusCallback( error, true );
        return;
    }
    m_bEditing = true;
    m_authoringAnchor = m_authoringCurrent = cell;
    m_bDrawingRun = m_tool == tile_canvas_tool_t::RECTANGLE || m_tool == tile_canvas_tool_t::LINE;
    if ( m_tool == tile_canvas_tool_t::PLAYER_SPAWN || m_tool == tile_canvas_tool_t::DOOR ) {
        const bool applied = m_tool == tile_canvas_tool_t::PLAYER_SPAWN
            ? m_pBridge->placePlayerSpawn( cell, 0.0f, &error )
            : ( removeDoor
                ? m_pBridge->removeDoorAt( cell, m_doorSide, &error )
                : m_pBridge->placeDoor( cell, m_doorSide, nullptr, &error ) );
        finishAuthoring( applied );
        if ( !applied && m_statusCallback ) m_statusCallback( error, true );
    } else if ( !m_bDrawingRun ) applyAuthoring( position );
    update();
}

void CypherTileOrthoView::applyAuthoring( const QPointF &position )
{
    if ( !m_bEditing ) return;
    tile_map_grid_coord_t cell;
    if ( !constructionCellAt( position, cell ) ) return;
    if ( m_bDrawingRun ) { m_authoringCurrent = cell; update(); return; }
    const bool front = m_plane == tile_editor_ortho_plane_t::FRONT;
    const i32 begin = front ? m_authoringCurrent.x : m_authoringCurrent.y;
    const i32 end = front ? cell.x : cell.y;
    const i32 step = end >= begin ? 1 : -1;
    QString error;
    for ( i32 value = begin; ; value += step ) {
        const tile_map_grid_coord_t current = front ? tile_map_grid_coord_t{ value, cell.y }
                                                   : tile_map_grid_coord_t{ cell.x, value };
        const bool applied = m_tool == tile_canvas_tool_t::ERASE
            ? m_pBridge->eraseCell( current, &error ) : m_pBridge->paintCell( current, m_strokePaint, &error );
        if ( !applied ) {
            finishAuthoring( false );
            if ( m_statusCallback ) m_statusCallback( error, true );
            return;
        }
        if ( value == end ) break;
    }
    m_authoringCurrent = cell;
    notifyAuthoringPreview();
}

void CypherTileOrthoView::finishAuthoring( bool commit )
{
    if ( !m_bEditing || m_pBridge == nullptr ) return;
    m_bEditing = false;
    QString error;
    if ( commit && m_bDrawingRun ) {
        const tile_map_grid_rect_t region{ std::min( m_authoringAnchor.x, m_authoringCurrent.x ),
            std::min( m_authoringAnchor.y, m_authoringCurrent.y ),
            static_cast<u32>( std::abs( m_authoringCurrent.x - m_authoringAnchor.x ) + 1 ),
            static_cast<u32>( std::abs( m_authoringCurrent.y - m_authoringAnchor.y ) + 1 ) };
        commit = m_pBridge->paintRect( region, m_strokePaint, &error );
    }
    m_bDrawingRun = false;
    if ( commit ) commit = m_pBridge->commitEdit( &error );
    if ( !commit ) m_pBridge->cancelEdit();
    refreshDocument();
    if ( commit ) { if ( m_changedCallback ) m_changedCallback(); }
    else if ( m_previewChangedCallback ) m_previewChangedCallback();
    if ( !error.isEmpty() && m_statusCallback ) m_statusCallback( error, true );
}

QPointF CypherTileOrthoView::worldToScreen( qreal horizontal, qreal height ) const
{
    return m_origin + QPointF( horizontal * m_pixelsPerUnit,
                              -height * m_pixelsPerUnit );
}

QRectF CypherTileOrthoView::projectedBox( const tile_map_geometry_box_t &box ) const
{
    const bool bFront = m_plane == tile_editor_ortho_plane_t::FRONT;
    const qreal horizontal = bFront ? box.centerX : box.centerY;
    const qreal halfWidth = bFront ? box.halfExtentX : box.halfExtentY;
    return QRectF(
        worldToScreen( horizontal - halfWidth, box.centerZ + box.halfExtentZ ),
        worldToScreen( horizontal + halfWidth, box.centerZ - box.halfExtentZ ) );
}

qreal CypherTileOrthoView::depth( const tile_map_geometry_box_t &box ) const
{
    return m_plane == tile_editor_ortho_plane_t::FRONT
        ? box.centerY - box.halfExtentY
        : -( box.centerX + box.halfExtentX );
}

bool CypherTileOrthoView::isSelected( const tile_map_geometry_box_t &box ) const
{
    return std::binary_search( m_selectedCells.begin(), m_selectedCells.end(), box.sourceCell, CellBefore );
}

const tile_map_geometry_box_t *CypherTileOrthoView::boxAt( const QPointF &position ) const
{
    for ( auto i = m_drawOrder.rbegin(); i != m_drawOrder.rend(); ++i ) {
        const auto &box = m_geometry.boxes.pData[*i];
        // Match selection tolerance, including thin floor slabs at overview zoom.
        if ( projectedBox( box ).adjusted( -2.0, -2.0, 2.0, 2.0 ).contains( position ) )
            return &box;
    }
    return nullptr;
}

void CypherTileOrthoView::paintEvent( QPaintEvent * )
{
    QPainter painter( this );
    painter.fillRect( rect(), m_preferences.canvasColor );
    painter.setRenderHint( QPainter::Antialiasing, false );
    if ( m_pBridge == nullptr || !m_pBridge->isInitialized() ) {
        painter.setPen( QColor( 160, 173, 186 ) );
        painter.drawText( rect(), Qt::AlignCenter, tr( "No tile map is open" ) );
        return;
    }
    if ( !m_geometryError.isEmpty() ) {
        painter.setPen( QColor( 235, 129, 108 ) );
        painter.drawText( rect().adjusted( 12, 12, -12, -12 ),
                          Qt::AlignCenter | Qt::TextWordWrap, m_geometryError );
        return;
    }

    const tile_map_document_t &document = *m_pBridge->document();
    const int horizontalCells = TileEditorGrid_EffectiveSpacingCells(
        m_preferences.gridSpacingCells, document.nCellSize * m_pixelsPerUnit,
        m_preferences.adaptiveGrid, m_preferences.gridMinimumPixels );
    const int verticalLevels = TileEditorGrid_EffectiveSpacingCells(
        m_preferences.gridSpacingCells, document.nLevelHeight * m_pixelsPerUnit,
        m_preferences.adaptiveGrid, m_preferences.gridMinimumPixels );
    const qreal horizontalStep = document.nCellSize * static_cast<qreal>( horizontalCells );
    const qreal verticalStep = document.nLevelHeight * static_cast<qreal>( verticalLevels );
    const qreal left = -m_origin.x() / m_pixelsPerUnit;
    const qreal right = ( width() - m_origin.x() ) / m_pixelsPerUnit;
    const qreal bottom = ( m_origin.y() - height() ) / m_pixelsPerUnit;
    const qreal top = m_origin.y() / m_pixelsPerUnit;
    auto drawGridAxis = [&]( bool bHorizontalAxis, qreal step, int stepCells,
                             qreal minimumWorld, qreal maximumWorld ) {
        if ( !m_preferences.showGrid ) return;
        auto drawLine = [&]( qreal screenPosition, bool bMajor ) {
            painter.setPen( QPen( bMajor ? m_preferences.majorGridColor : m_preferences.minorGridColor,
                bMajor ? 1.25 : 1.0 ) );
            if ( bHorizontalAxis ) {
                painter.drawLine( QPointF( screenPosition, 0.0 ),
                                  QPointF( screenPosition, height() ) );
            } else {
                painter.drawLine( QPointF( 0.0, screenPosition ),
                                  QPointF( width(), screenPosition ) );
            }
        };
        const int screenExtent = bHorizontalAxis ? width() : height();
        const double majorPeriod = TileEditorGrid_EffectiveMajorSpacingCells(
            stepCells, m_preferences.majorGridEvery );
        if ( step * m_pixelsPerUnit < 1.0 ) {
            // Fixed grids may have millions of lines at extreme zoom. Each
            // pixel receives all actual grid lines that fall within it; this
            // preserves spacing and major positions without redundant draws.
            const qreal pixelsPerCell = m_pixelsPerUnit *
                ( bHorizontalAxis ? document.nCellSize : document.nLevelHeight );
            for ( int pixel = 0; pixel < screenExtent; ++pixel ) {
                const qreal minimumCell = bHorizontalAxis
                    ? ( pixel - m_origin.x() ) / pixelsPerCell
                    : ( m_origin.y() - pixel - 1.0 ) / pixelsPerCell;
                const qreal maximumCell = minimumCell + 1.0 / pixelsPerCell;
                const qreal firstCell = std::ceil( minimumCell / stepCells ) * stepCells;
                const qreal lastCell = std::floor( maximumCell / stepCells ) * stepCells;
                const bool bMajor = m_preferences.majorGridEvery > 0 &&
                    std::ceil( firstCell / majorPeriod ) * majorPeriod <= lastCell;
                drawLine( pixel, bMajor );
            }
            return;
        }
        const qreal firstLine = std::ceil( minimumWorld / step );
        const int lineCount = static_cast<int>( std::clamp(
            std::ceil( ( maximumWorld - minimumWorld ) / step ) + 1.0,
            0.0, static_cast<qreal>( screenExtent ) + 2.0 ) );
        for ( int i = 0; i < lineCount; ++i ) {
            const qreal line = firstLine + i;
            const bool bMajor = TileEditorGrid_IsMajorCoordinate(
                line * stepCells, majorPeriod );
            drawLine( bHorizontalAxis
                ? worldToScreen( line * step, 0.0 ).x()
                : worldToScreen( 0.0, line * step ).y(), bMajor );
        }
    };
    drawGridAxis( true, horizontalStep, horizontalCells, left, right );
    drawGridAxis( false, verticalStep, verticalLevels, bottom, top );
    if ( m_preferences.showViewAxes ) {
        painter.setPen( QPen( m_plane == tile_editor_ortho_plane_t::FRONT
            ? m_preferences.axisXColor : m_preferences.axisYColor, 1.25 ) );
        painter.drawLine( QPointF( 0.0, m_origin.y() ), QPointF( width(), m_origin.y() ) );
        painter.setPen( QPen( m_preferences.axisZColor, 1.25 ) );
        const qreal horizontal = m_preferences.centerViewAxes
            ? ( m_plane == tile_editor_ortho_plane_t::FRONT ? document.nWidth : document.nHeight ) * document.nCellSize * 0.5 : 0.0;
        const qreal axisX = worldToScreen( horizontal, 0 ).x();
        painter.drawLine( QPointF( axisX, 0.0 ), QPointF( axisX, height() ) );
    }

    // Inspection wires retain depth cues. With materials enabled, nearest
    // surfaces cover farther surfaces to agree with selection and the HUD.
    const qreal nearDepth = m_drawOrder.empty() ? 0.0 : depth( m_geometry.boxes.pData[m_drawOrder.back()] );
    const qreal farDepth = m_drawOrder.empty() ? 0.0 : depth( m_geometry.boxes.pData[m_drawOrder.front()] );
    const qreal depthRange = std::max<qreal>( 0.0001, farDepth - nearDepth );
    for ( const usize index : m_drawOrder ) {
        const tile_map_geometry_box_t &box = m_geometry.boxes.pData[index];
        const QRectF projected = projectedBox( box );
        if ( !projected.intersects( QRectF( rect() ) ) ) continue;
        const tile_map_material_definition_t material = CypherTileMapMaterial_Resolve( box.nMaterialSlot );
        QColor fill = QColor::fromRgbF( material.colorR, material.colorG, material.colorB );
        QColor edge = m_preferences.wireColor;
        if ( box.kind == tile_map_geometry_box_kind_t::WALL ) edge = m_preferences.wallColor;
        else if ( box.kind == tile_map_geometry_box_kind_t::STAIR ) edge = m_preferences.stairColor;
        else if ( box.kind == tile_map_geometry_box_kind_t::DOOR ) edge = m_preferences.doorColor;
        const qreal proximity = m_preferences.depthCueWireframe
            ? 1.0 - std::clamp( ( depth( box ) - nearDepth ) / depthRange, 0.0, 1.0 ) : 1.0;
        const qreal opacity = m_preferences.depthCueWireframe ? 0.28 + proximity * 0.72 : 1.0;
        // Opaque, pre-mixed edges avoid dozens of far overlapping outlines
        // accumulating into an incorrectly bright foreground silhouette.
        const auto background = m_preferences.canvasColor;
        edge = QColor::fromRgbF( edge.redF() * opacity + background.redF() * ( 1.0 - opacity ),
            edge.greenF() * opacity + background.greenF() * ( 1.0 - opacity ),
            edge.blueF() * opacity + background.blueF() * ( 1.0 - opacity ) );
        if ( m_preferences.showOrthoMaterials ) {
            // Opacity controls contrast against the viewport background, not
            // a blend of unrelated materials lying behind the picked surface.
            painter.fillRect( projected, m_preferences.canvasColor );
            if ( box.kind == tile_map_geometry_box_kind_t::DOOR ) {
                // Door boxes mark openings. Both renderer paths bypass their
                // inherited cell slot and use this diagnostic orange tint.
                painter.save();
                painter.setOpacity( m_preferences.orthoMaterialOpacity );
                painter.fillRect( projected, QColor::fromRgbF( 0.92, 0.39, 0.10 ) );
                painter.restore();
            } else {
                TileOrthoMaterials_Paint( painter, projected, m_pMaterialCache,
                    box.nMaterialSlot, m_preferences.orthoMaterialOpacity );
            }
            painter.setBrush( Qt::NoBrush );
        } else if ( m_preferences.wireframeOrtho ) {
            painter.setBrush( Qt::NoBrush );
        } else {
            fill.setAlpha( box.kind == tile_map_geometry_box_kind_t::WALL ? 24 : 105 );
            painter.setBrush( fill );
        }
        const qreal lineWidth = m_preferences.wireLineWidth +
            ( box.kind == tile_map_geometry_box_kind_t::DOOR ? 0.5 : 0.0 );
        painter.setPen( QPen( edge, lineWidth ) );
        painter.drawRect( projected );
    }

    // Draw selected outlines last so selection stays visible through walls.
    painter.setBrush( Qt::NoBrush );
    painter.setPen( QPen( m_preferences.selectionColor, 2.0 ) );
    for ( const usize index : m_drawOrder ) {
        const tile_map_geometry_box_t &box = m_geometry.boxes.pData[index];
        if ( isSelected( box ) ) painter.drawRect( projectedBox( box ) );
    }

    // Front/Side cannot derive the hidden map coordinate from the projection.
    // Preview the exact fixed-slice cell that will be edited so an author never
    // accidentally changes an unseen row or column.
    if ( m_bCursorInside && supportsProjectedAuthoring() ) {
        tile_map_grid_coord_t cell{};
        i16 cursorLevel{};
        if ( constructionCellAt( m_cursorPosition, cell ) &&
             floorLevelAt( m_cursorPosition, cursorLevel ) ) {
            const bool front = m_plane == tile_editor_ortho_plane_t::FRONT;
            const int horizontalCell = front ? cell.x : cell.y;
            i16 floorLevel = m_bEditing ? m_strokePaint.nFloorLevel : cursorLevel;
            u16 wallLevels = std::max<u16>( m_paint.nWallHeightLevels, 1u );
            if ( m_tool == tile_canvas_tool_t::ERASE ||
                 m_tool == tile_canvas_tool_t::PLAYER_SPAWN ||
                 m_tool == tile_canvas_tool_t::DOOR ||
                 m_tool == tile_canvas_tool_t::EYEDROPPER ) {
                if ( const auto *authored = CypherTileMapDocument_CellAt( &document, cell );
                     authored != nullptr && ( authored->flags & TILE_MAP_CELL_FLAG_FLOOR ) ) {
                    floorLevel = authored->nFloorLevel;
                    wallLevels = std::max<u16>( authored->nWallHeightLevels, 1u );
                }
            }
            const qreal floorZ = floorLevel * document.nLevelHeight;
            const qreal topZ = ( static_cast<qreal>( floorLevel ) + wallLevels ) * document.nLevelHeight;
            const qreal leftWorld = horizontalCell * document.nCellSize;
            QRectF ghost( worldToScreen( leftWorld, topZ ),
                          worldToScreen( leftWorld + document.nCellSize, floorZ ) );
            ghost = ghost.normalized();
            const QColor sliceColor = front ? m_preferences.axisYColor : m_preferences.axisXColor;
            QColor ghostFill = sliceColor;
            ghostFill.setAlpha( 38 );
            painter.setBrush( ghostFill );
            painter.setPen( QPen( sliceColor.lighter( 135 ), 1.75, Qt::DashLine ) );
            painter.drawRect( ghost );
            painter.setBrush( Qt::NoBrush );
        }
    }

    if ( m_bSelecting && m_bMarquee ) {
        painter.setPen( QPen( m_preferences.selectionColor, 1.5, Qt::DashLine ) );
        QColor fill = m_preferences.selectionColor;
        fill.setAlpha( 35 );
        painter.setBrush( fill );
        painter.drawRect( QRectF( m_selectionAnchor, m_selectionCurrent ).normalized() );
        painter.setBrush( Qt::NoBrush );
    }
    if ( m_bEditing && m_bDrawingRun ) {
        const bool front = m_plane == tile_editor_ortho_plane_t::FRONT;
        const int first = front ? std::min( m_authoringAnchor.x, m_authoringCurrent.x ) : std::min( m_authoringAnchor.y, m_authoringCurrent.y );
        const int last = front ? std::max( m_authoringAnchor.x, m_authoringCurrent.x ) : std::max( m_authoringAnchor.y, m_authoringCurrent.y );
        const qreal floorZ = m_strokePaint.nFloorLevel * document.nLevelHeight;
        painter.setPen( QPen( m_preferences.selectionColor, 3.0, Qt::DashLine ) );
        painter.drawLine( worldToScreen( first * document.nCellSize, floorZ ),
            worldToScreen( ( last + 1 ) * document.nCellSize, floorZ ) );
    }

    painter.setPen( m_preferences.wireColor.lighter( 125 ) );
    QStringList hudLines;
    if ( m_preferences.showViewMetrics ) hudLines = viewMetrics().split( QLatin1Char( '\n' ) );
    if ( supportsProjectedAuthoring() ) {
        const QPointF *position = m_bCursorInside ? &m_cursorPosition : nullptr;
        QString construction = constructionSliceDescription( position );
        if ( m_tool == tile_canvas_tool_t::EYEDROPPER )
            construction += tr( " · PICK FROM THIS SLICE" );
        else if ( m_tool == tile_canvas_tool_t::PLAYER_SPAWN || m_tool == tile_canvas_tool_t::DOOR )
            construction += tr( " · PLACE ON EXISTING CELL" );
        else
            construction += tr( " · DRAG HORIZONTAL AXIS ONLY" );
        hudLines.prepend( construction );
    } else if ( m_tool == tile_canvas_tool_t::FILL ) {
        hudLines.prepend( tr( "FILL USES XY TOP · hidden-axis connectivity is ambiguous in this projection" ) );
    }
    const QString material = materialDescription();
    if ( !material.isEmpty() ) hudLines.prepend( QString( material ).replace( QLatin1Char( '\n' ), QStringLiteral( " · " ) ) );
    if ( !hudLines.isEmpty() ) {
        const int labelHeight = painter.fontMetrics().height() * hudLines.size() + 10;
        const QRect bounds( 0, height() - labelHeight, width(), labelHeight );
        QColor background = m_preferences.canvasColor.darker( 135 ); background.setAlpha( 235 );
        painter.fillRect( bounds, background );
        if ( supportsProjectedAuthoring() || m_tool == tile_canvas_tool_t::FILL ) {
            const QColor sliceColor = m_plane == tile_editor_ortho_plane_t::FRONT
                ? m_preferences.axisYColor : m_preferences.axisXColor;
            painter.fillRect( QRect( bounds.left(), bounds.top(), 4, bounds.height() ), sliceColor );
        }
        painter.setPen( m_preferences.textColor );
        for ( int i = 0; i < hudLines.size(); ++i )
            painter.drawText( QRect( 8, bounds.y() + 4 + i * painter.fontMetrics().height(),
                width() - 16, painter.fontMetrics().height() ), Qt::AlignLeft | Qt::AlignVCenter,
                painter.fontMetrics().elidedText( hudLines[i], Qt::ElideRight, width() - 16 ) );
    }
    if ( m_drawOrder.empty() ) {
        painter.drawText( rect(), Qt::AlignCenter, tr( "Choose Paint to draw on the construction layer" ) );
    }

    if ( m_preferences.showCoordinateRulers ) {
        const QFontMetrics metrics( painter.font() );
        const double horizontalMajorUnits = TileEditorGrid_EffectiveMajorSpacingCells(
            horizontalCells, m_preferences.majorGridEvery ) * document.nCellSize;
        const double verticalMajorUnits = TileEditorGrid_EffectiveMajorSpacingCells(
            verticalLevels, m_preferences.majorGridEvery ) * document.nLevelHeight;
        const double horizontalSpacing = TileEditorGrid_EffectiveRulerSpacing(
            horizontalMajorUnits, m_pixelsPerUnit,
            std::max( 64, metrics.horizontalAdvance( QStringLiteral( "-000000" ) ) + 12 ) );
        const double verticalSpacing = TileEditorGrid_EffectiveRulerSpacing(
            verticalMajorUnits, m_pixelsPerUnit, metrics.height() + 8 );
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
                const double coordinate = first + i * spacing;
                const QPointF screen = horizontal
                    ? worldToScreen( coordinate, 0.0 )
                    : worldToScreen( 0.0, coordinate );
                const qreal position = horizontal ? screen.x() : screen.y();
                if ( std::isfinite( position ) )
                    ticks.push_back( { position, CoordinateLabel( coordinate ) } );
            }
        };
        appendTicks( left, right, horizontalSpacing, true, horizontalTicks );
        appendTicks( bottom, top, verticalSpacing, false, verticalTicks );
        DrawCoordinateRulers( painter, rect(), m_preferences,
            m_plane == tile_editor_ortho_plane_t::FRONT ? tr( "X / Z" ) : tr( "Y / Z" ),
            std::move( horizontalTicks ), std::move( verticalTicks ) );
    }
    if ( m_preferences.showViewAxes )
        DrawOrthoAxisIndicator( painter, rect(), m_preferences, m_plane );

}

QString CypherTileOrthoView::materialDescription() const
{
    if ( !m_preferences.showMaterialLabels ) return {};
    const auto *pBox = m_bCursorInside ? boxAt( m_cursorPosition ) : nullptr;
    const bool hovered = pBox != nullptr;
    if ( pBox == nullptr && !m_selectedCells.empty() ) {
        for ( auto i = m_drawOrder.rbegin(); i != m_drawOrder.rend(); ++i ) {
            if ( isSelected( m_geometry.boxes.pData[*i] ) ) {
                pBox = &m_geometry.boxes.pData[*i];
                break;
            }
        }
    }
    if ( pBox == nullptr ) return {};
    QString kind;
    switch ( pBox->kind ) {
        case tile_map_geometry_box_kind_t::FLOOR: kind = tr( "floor" ); break;
        case tile_map_geometry_box_kind_t::WALL: kind = tr( "wall" ); break;
        case tile_map_geometry_box_kind_t::DOOR: kind = tr( "door" ); break;
        case tile_map_geometry_box_kind_t::STAIR: kind = tr( "stair" ); break;
    }
    return tr( "%1 %2 · Cell %3, %4 · %5" ).arg( hovered ? tr( "Hover" ) : tr( "Selected" ), kind )
        .arg( pBox->sourceCell.x ).arg( pBox->sourceCell.y )
        .arg( pBox->kind == tile_map_geometry_box_kind_t::DOOR
            ? tr( "Diagnostic door marker · no authored surface material" )
            : TileOrthoMaterials_Describe( m_pMaterialCache, pBox->nMaterialSlot ) );
}

QString CypherTileOrthoView::viewMetrics() const
{
    if ( m_pBridge == nullptr || !m_pBridge->isInitialized() ) return {};
    const auto &document = *m_pBridge->document();
    const int horizontalCells = TileEditorGrid_EffectiveSpacingCells( m_preferences.gridSpacingCells,
        document.nCellSize * m_pixelsPerUnit, m_preferences.adaptiveGrid, m_preferences.gridMinimumPixels );
    const int verticalLevels = TileEditorGrid_EffectiveSpacingCells( m_preferences.gridSpacingCells,
        document.nLevelHeight * m_pixelsPerUnit, m_preferences.adaptiveGrid, m_preferences.gridMinimumPixels );
    QString detail = tr( "%1 px/u · %2" ).arg( m_pixelsPerUnit, 0, 'f', 1 )
        .arg( !m_preferences.showGrid ? tr( "Grid hidden" ) : tr( "Grid %1 × %2 u%3" )
            .arg( horizontalCells * document.nCellSize, 0, 'g', 4 )
            .arg( verticalLevels * document.nLevelHeight, 0, 'g', 4 )
            .arg( m_preferences.adaptiveGrid ? tr( " · auto" ) : tr( " · fixed" ) ) );
    QString location = m_bCursorInside ? tr( "%1 %2  Z %3 u" )
        .arg( m_plane == tile_editor_ortho_plane_t::FRONT ? QStringLiteral( "X" ) : QStringLiteral( "Y" ) )
        .arg( ( m_cursorPosition.x() - m_origin.x() ) / m_pixelsPerUnit, 0, 'f', 2 )
        .arg( ( m_origin.y() - m_cursorPosition.y() ) / m_pixelsPerUnit, 0, 'f', 2 )
        : tr( "%1 boxes%2" ).arg( m_drawOrder.size() )
            .arg( m_preferences.depthCueWireframe ? tr( " · depth cue" ) : QString{} );
    if ( !m_selectedCells.empty() ) location += tr( " · Selected %1 cells" ).arg( m_selectedCells.size() );
    return detail + QLatin1Char( '\n' ) + location;
}

void CypherTileOrthoView::leaveEvent( QEvent *pEvent )
{
    m_bCursorInside = false;
    update();
    QWidget::leaveEvent( pEvent );
}

void CypherTileOrthoView::resizeEvent( QResizeEvent *pEvent )
{
    if ( !m_bUserNavigated ) {
        fitToView();
    } else if ( pEvent->oldSize().isValid() ) {
        m_origin += QPointF( ( width() - pEvent->oldSize().width() ) * 0.5,
                             ( height() - pEvent->oldSize().height() ) * 0.5 );
    }
    QWidget::resizeEvent( pEvent );
}

void CypherTileOrthoView::mousePressEvent( QMouseEvent *pEvent )
{
    setFocus( Qt::MouseFocusReason );
    m_cursorPosition = pEvent->position();
    m_bCursorInside = true;
    if ( m_bPanning ) {
        pEvent->accept();
        return;
    }
    if ( pEvent->button() == Qt::MiddleButton || pEvent->button() == Qt::RightButton ||
         ( pEvent->button() == Qt::LeftButton && ( m_bSpaceHeld || m_bPanToolEnabled ) ) ) {
        finishAuthoring( false );
        cancelSelectionDrag();
        m_bPanning = true;
        m_panButton = pEvent->button();
        m_lastMouse = pEvent->position();
        m_bContextMenuCandidate = pEvent->button() == Qt::RightButton &&
            pEvent->modifiers() == Qt::NoModifier;
        m_contextMenuPress = pEvent->position();
        m_contextMenuGlobal = pEvent->globalPosition().toPoint();
        setCursor( Qt::ClosedHandCursor );
        pEvent->accept();
        return;
    }
    if ( pEvent->button() == Qt::LeftButton ) {
        const bool selectionModifier = pEvent->modifiers() & ( Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier );
        if ( m_tool != tile_canvas_tool_t::SELECT && !selectionModifier ) {
            beginAuthoring( pEvent->position() );
            pEvent->accept();
            return;
        }
        if ( m_multiSelectionCallback || m_selectionCellsCallback ) {
            m_bSelecting = true;
            m_bMarquee = false;
            m_selectionAnchor = m_selectionCurrent = pEvent->position();
            m_selectionModifiers = pEvent->modifiers();
            pEvent->accept();
            return;
        }
        if ( const auto *pBox = boxAt( pEvent->position() ) ) {
            setSelection( true, pBox->sourceCell );
            if ( m_selectionCallback ) m_selectionCallback( pBox->sourceCell );
            pEvent->accept();
            return;
        }
    }
    QWidget::mousePressEvent( pEvent );
}

void CypherTileOrthoView::mouseMoveEvent( QMouseEvent *pEvent )
{
    m_cursorPosition = pEvent->position();
    m_bCursorInside = true;
    if ( m_preferences.showViewMetrics || m_preferences.showMaterialLabels ) update();
    if ( m_tool != tile_canvas_tool_t::SELECT && m_tool != tile_canvas_tool_t::PAN ) update();
    if ( m_bPanning ) {
        if ( !pEvent->buttons().testFlag( m_panButton ) ) {
            stopPanning();
            pEvent->accept();
            return;
        }
        if ( m_bContextMenuCandidate &&
             ( pEvent->position() - m_contextMenuPress ).manhattanLength() >=
                 QApplication::startDragDistance() ) {
            m_bContextMenuCandidate = false;
        }
        m_origin += pEvent->position() - m_lastMouse;
        m_lastMouse = pEvent->position();
        m_bUserNavigated = true;
        update();
        pEvent->accept();
        return;
    }
    if ( m_bEditing ) {
        if ( pEvent->buttons().testFlag( Qt::LeftButton ) ) applyAuthoring( pEvent->position() );
        else finishAuthoring( false );
        pEvent->accept();
        return;
    }
    if ( m_bSelecting ) {
        if ( !pEvent->buttons().testFlag( Qt::LeftButton ) ) {
            cancelSelectionDrag();
            pEvent->accept();
            return;
        }
        m_selectionCurrent = pEvent->position();
        if ( m_selectionCellsCallback &&
             ( m_selectionCurrent - m_selectionAnchor ).manhattanLength() >= QApplication::startDragDistance() )
            m_bMarquee = true;
        update();
        pEvent->accept();
        return;
    }
    QWidget::mouseMoveEvent( pEvent );
}

void CypherTileOrthoView::mouseReleaseEvent( QMouseEvent *pEvent )
{
    if ( m_bEditing && pEvent->button() == Qt::LeftButton ) {
        applyAuthoring( pEvent->position() );
        finishAuthoring( true );
        pEvent->accept();
        return;
    }
    if ( m_bPanning && pEvent->button() == m_panButton ) {
        const bool showContextMenu = m_bContextMenuCandidate &&
            pEvent->button() == Qt::RightButton;
        const QPoint globalPosition = m_contextMenuGlobal;
        m_bContextMenuCandidate = false;
        stopPanning();
        if ( showContextMenu && m_contextMenuCallback ) {
            QTimer::singleShot( 0, this, [this, globalPosition] {
                if ( m_contextMenuCallback ) m_contextMenuCallback( globalPosition );
            } );
        }
        pEvent->accept();
        return;
    }
    if ( m_bSelecting && pEvent->button() == Qt::LeftButton ) {
        m_selectionCurrent = pEvent->position();
        const bool marquee = m_selectionCellsCallback && ( m_bMarquee ||
            ( m_selectionCurrent - m_selectionAnchor ).manhattanLength() >= QApplication::startDragDistance() );
        const Qt::KeyboardModifiers modifiers = m_selectionModifiers;
        const QRectF region = QRectF( m_selectionAnchor, m_selectionCurrent ).normalized();
        cancelSelectionDrag();
        if ( marquee ) {
            std::vector<tile_map_grid_coord_t> cells;
            for ( const usize index : m_drawOrder ) {
                const auto &box = m_geometry.boxes.pData[index];
                if ( projectedBox( box ).intersects( region ) ) cells.push_back( box.sourceCell );
            }
            std::sort( cells.begin(), cells.end(), CellBefore );
            cells.erase( std::unique( cells.begin(), cells.end(), SameCell ), cells.end() );
            m_selectionCellsCallback( cells, modifiers );
        } else {
            const auto *pBox = boxAt( pEvent->position() );
            const tile_map_grid_coord_t cell = pBox != nullptr ? pBox->sourceCell : tile_map_grid_coord_t{};
            if ( m_multiSelectionCallback ) m_multiSelectionCallback( pBox != nullptr, cell, modifiers );
            else {
                setSelection( pBox != nullptr, cell );
                if ( pBox != nullptr && m_selectionCallback ) m_selectionCallback( cell );
            }
        }
        pEvent->accept();
        return;
    }
    QWidget::mouseReleaseEvent( pEvent );
}

void CypherTileOrthoView::setPanToolEnabled( bool enabled )
{
    if ( enabled ) finishAuthoring( false );
    m_bPanToolEnabled = enabled;
    cancelSelectionDrag();
    stopPanning();
}

void CypherTileOrthoView::cancelSelectionDrag()
{
    const bool pending = m_bSelecting;
    m_bSelecting = false;
    m_bMarquee = false;
    if ( pending ) update();
}

void CypherTileOrthoView::updatePanCursor()
{
    if ( m_bPanning ) setCursor( Qt::ClosedHandCursor );
    else if ( m_bSpaceHeld || m_bPanToolEnabled ) setCursor( Qt::OpenHandCursor );
    else unsetCursor();
}

void CypherTileOrthoView::stopPanning()
{
    m_bPanning = false;
    m_panButton = Qt::NoButton;
    m_bContextMenuCandidate = false;
    updatePanCursor();
}

bool CypherTileOrthoView::event( QEvent *pEvent )
{
    if ( pEvent->type() == QEvent::ToolTip ) {
        const QString description = materialDescription();
        if ( !description.isEmpty() ) {
            const auto *pHelp = static_cast<QHelpEvent *>( pEvent );
            QToolTip::showText( pHelp->globalPos(), description +
                ( m_tool != tile_canvas_tool_t::SELECT && m_tool != tile_canvas_tool_t::PAN ? QLatin1Char( '\n' ) + toolTip() : QString{} ), this );
            pEvent->accept();
            return true;
        }
    }
    if ( pEvent->type() == QEvent::FocusOut || pEvent->type() == QEvent::Hide ||
         pEvent->type() == QEvent::WindowDeactivate || pEvent->type() == QEvent::UngrabMouse ) {
        finishAuthoring( false );
        m_bSpaceHeld = false;
        cancelSelectionDrag();
        stopPanning();
    }
    if ( pEvent->type() == QEvent::ShortcutOverride ) {
        const auto *pKey = static_cast<QKeyEvent *>( pEvent );
        if ( ( pKey->key() == Qt::Key_Space && pKey->modifiers() == Qt::NoModifier ) ||
             ( pKey->key() == Qt::Key_Escape && ( m_bPanning || m_bSpaceHeld || m_bSelecting || m_bEditing ) ) ) {
            pEvent->accept();
            return true;
        }
    }
    return QWidget::event( pEvent );
}

void CypherTileOrthoView::keyPressEvent( QKeyEvent *pEvent )
{
    if ( pEvent->key() == Qt::Key_Space && pEvent->modifiers() == Qt::NoModifier ) {
        m_bSpaceHeld = true;
        updatePanCursor();
        pEvent->accept();
        return;
    }
    if ( pEvent->key() == Qt::Key_Escape && ( m_bPanning || m_bSpaceHeld || m_bSelecting || m_bEditing ) ) {
        finishAuthoring( false );
        m_bSpaceHeld = false;
        cancelSelectionDrag();
        stopPanning();
        pEvent->accept();
        return;
    }
    QWidget::keyPressEvent( pEvent );
}

void CypherTileOrthoView::keyReleaseEvent( QKeyEvent *pEvent )
{
    if ( pEvent->key() == Qt::Key_Space ) {
        if ( !pEvent->isAutoRepeat() ) {
            m_bSpaceHeld = false;
            // Finish the active drag on mouse release, even if Space is
            // released first; it must never turn into a selection halfway.
            updatePanCursor();
        }
        pEvent->accept();
        return;
    }
    QWidget::keyReleaseEvent( pEvent );
}

void CypherTileOrthoView::wheelEvent( QWheelEvent *pEvent )
{
    const qreal delta = pEvent->angleDelta().y() != 0
        ? pEvent->angleDelta().y() / 120.0
        : pEvent->pixelDelta().y() / 40.0;
    if ( qFuzzyIsNull( delta ) ) {
        pEvent->ignore();
        return;
    }
    const qreal scale = std::clamp(
        m_pixelsPerUnit * std::pow( 1.18, delta ),
        MIN_PIXELS_PER_UNIT, MAX_PIXELS_PER_UNIT );
    const QPointF position = pEvent->position();
    const QPointF projectedWorld = ( position - m_origin ) / m_pixelsPerUnit;
    m_origin = position - projectedWorld * scale;
    m_pixelsPerUnit = scale;
    m_bUserNavigated = true;
    m_bHasFit = true;
    update();
    pEvent->accept();
}

} // namespace cypher::tools::tile_editor
