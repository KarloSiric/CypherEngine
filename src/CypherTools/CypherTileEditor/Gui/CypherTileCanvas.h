//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileCanvas.h
//  Purpose: Declares the interactive QPainter tile-map canvas.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_TOOLS_TILEEDITOR_CANVAS_H
#define CYPHER_TOOLS_TILEEDITOR_CANVAS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherTileDocumentBridge.h"
#include "CypherTileEditorSettingsDialog.h"

#include <QPointF>
#include <QSize>
#include <QWidget>

#include <functional>
#include <span>
#include <vector>

class QKeyEvent;
class QFocusEvent;
class QMouseEvent;
class QPaintEvent;
class QWheelEvent;

namespace cypher::tools::tile_editor
{

struct tile_ortho_material_cache_t;

enum class tile_canvas_tool_t : unsigned char {
    SELECT = 0u,
    PAINT,
    ERASE,
    RECTANGLE,
    PLAYER_SPAWN,
    DOOR,
    PAN,
    EYEDROPPER,
    LINE,
    FILL
};

class CypherTileCanvas final : public QWidget
{
public:
    using changed_callback_t = std::function<void()>;
    using selection_callback_t = std::function<void(
        bool,
        tile_map_grid_coord_t )>;
    using cursor_callback_t = std::function<void(
        bool,
        tile_map_grid_coord_t )>;
    using zoom_callback_t = std::function<void( qreal )>;
    using status_callback_t = std::function<void( const QString &, bool )>;
    using context_menu_callback_t = std::function<void( const QPoint & )>;

    explicit CypherTileCanvas( QWidget *pParent = nullptr );

    void setDocumentBridge( CypherTileDocumentBridge *pBridge );
    void setPreferences( const tile_editor_preferences_t &preferences );
    // Borrowed from the owning editor; the cache outlives the view.
    void setMaterialCache( const tile_ortho_material_cache_t *pCache );
    void setTool( tile_canvas_tool_t tool );
    tile_canvas_tool_t tool() const;
    void setPaint( const tile_map_paint_t &paint );
    const tile_map_paint_t &paint() const;
    void setDoorSide( tile_map_marker_side_t side );
    tile_map_marker_side_t doorSide() const;

    void setChangedCallback( changed_callback_t callback );
    void setPreviewChangedCallback( changed_callback_t callback );
    void setPaintPickedCallback( std::function<void( const tile_map_paint_t & )> callback );
    void setSelectionCallback( selection_callback_t callback );
    void setMoveSelectionCallback( std::function<void( int, int, bool )> callback );
    void setCursorCallback( cursor_callback_t callback );
    void setZoomCallback( zoom_callback_t callback );
    void setStatusCallback( status_callback_t callback );
    void setContextMenuCallback( context_menu_callback_t callback );

    bool hasSelection() const;
    tile_map_grid_coord_t selectedCell() const;
    tile_map_grid_rect_t selectionRect() const;
    const std::vector<tile_map_grid_coord_t> &selectedCells() const;
    void setSelectedCells( std::span<const tile_map_grid_coord_t> cells );
    void modifySelectedCells( std::span<const tile_map_grid_coord_t> cells, Qt::KeyboardModifiers modifiers );
    void selectAllOccupied();
    void setSelectionRect( const tile_map_grid_rect_t &rectangle );
    void clearSelection();
    void selectCell( tile_map_grid_coord_t coordinate, bool bCenter = false );
    void refreshDocument();

    qreal zoomFactor() const;
    void setZoomFactor( qreal zoom );
    void fitToView();
    void fitSelection();
    bool isPanning() const;
    QPointF viewOrigin() const;
    QString viewMetrics() const;
    QString materialDescription() const;
    bool stampRectangle( const QSize &size, const QString &label );

protected:
    bool event( QEvent *pEvent ) override;
    void paintEvent( QPaintEvent *pEvent ) override;
    void resizeEvent( QResizeEvent *pEvent ) override;
    void mousePressEvent( QMouseEvent *pEvent ) override;
    void mouseMoveEvent( QMouseEvent *pEvent ) override;
    void mouseReleaseEvent( QMouseEvent *pEvent ) override;
    void wheelEvent( QWheelEvent *pEvent ) override;
    void keyPressEvent( QKeyEvent *pEvent ) override;
    void keyReleaseEvent( QKeyEvent *pEvent ) override;
    void focusOutEvent( QFocusEvent *pEvent ) override;
    void leaveEvent( QEvent *pEvent ) override;

private:
    enum class selection_operation_t { REPLACE, ADD, TOGGLE, SUBTRACT };
    QPointF gridToScreen( tile_map_grid_coord_t coordinate ) const;
    tile_map_grid_coord_t screenToGrid( const QPointF &position ) const;
    bool contains( tile_map_grid_coord_t coordinate ) const;
    tile_map_grid_rect_t rectangleFromDrag() const;
    void applyDragCell( tile_map_grid_coord_t coordinate );
    void fillRegion( tile_map_grid_coord_t coordinate );
    void finishActiveEdit( bool bCommit );
    void cancelInteraction();
    void updateDragEndpoint( const QPointF &position );
    void notifySelection();
    void notifyChanged();
    void report( const QString &message, bool bError = false );
    void updateCursorShape();
    bool isSelectedCell( tile_map_grid_coord_t coordinate ) const;
    bool isAuthoredCell( tile_map_grid_coord_t coordinate ) const;
    bool isPreviewSelectedCell( tile_map_grid_coord_t coordinate ) const;
    void commitMarqueeSelection();
    void combineSelectedCells( std::span<const tile_map_grid_coord_t> cells, selection_operation_t operation );

    CypherTileDocumentBridge *m_pBridge{ nullptr };
    const tile_ortho_material_cache_t *m_pMaterialCache{ nullptr };
    tile_editor_preferences_t m_preferences{};
    tile_canvas_tool_t m_tool{ tile_canvas_tool_t::PAINT };
    tile_map_paint_t m_paint{};
    tile_map_marker_side_t m_doorSide{ tile_map_marker_side_t::NORTH };

    QPointF m_origin{ 40.0, 40.0 };
    qreal m_zoom{ 24.0 };
    bool m_bHasFit{ false };
    bool m_bUserNavigated{ false };
    bool m_bSpaceDown{ false };
    bool m_bPanning{ false };
    Qt::MouseButton m_panButton{ Qt::NoButton };
    bool m_bContextMenuCandidate{ false };
    QPointF m_contextMenuPress{};
    QPoint m_contextMenuGlobal{};
    bool m_bEditing{ false };
    bool m_bDraggingRectangle{ false };
    bool m_bDraggingSelection{ false };
    bool m_bMovingSelection{ false };
    bool m_bMoveStarted{ false };
    selection_operation_t m_selectionOperation{ selection_operation_t::REPLACE };
    QPointF m_dragPressPosition{};
    QPointF m_lastMouse{};
    tile_map_grid_coord_t m_lastEdited{ -1, -1 };
    tile_map_grid_coord_t m_hoverCell{ -1, -1 };
    tile_map_grid_coord_t m_dragAnchor{};
    tile_map_grid_coord_t m_dragCurrent{};
    bool m_bHasSelection{ false };
    tile_map_grid_coord_t m_selection{};
    tile_map_grid_rect_t m_selectionRect{};
    std::vector<tile_map_grid_coord_t> m_selectedCells{};

    changed_callback_t m_changedCallback{};
    changed_callback_t m_previewChangedCallback{};
    std::function<void( const tile_map_paint_t & )> m_paintPickedCallback{};
    selection_callback_t m_selectionCallback{};
    std::function<void( int, int, bool )> m_moveSelectionCallback{};
    cursor_callback_t m_cursorCallback{};
    zoom_callback_t m_zoomCallback{};
    status_callback_t m_statusCallback{};
    context_menu_callback_t m_contextMenuCallback{};
};

} // namespace cypher::tools::tile_editor

#endif // CYPHER_TOOLS_TILEEDITOR_CANVAS_H
