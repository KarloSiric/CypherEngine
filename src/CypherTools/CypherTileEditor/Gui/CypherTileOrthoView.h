//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileOrthoView.h
//  Purpose: Declares orthographic views of the shared tile-map geometry.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_TOOLS_TILEEDITOR_ORTHOVIEW_H
#define CYPHER_TOOLS_TILEEDITOR_ORTHOVIEW_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "Core/CypherTileMapGeometry.h"
#include "CypherTileDocumentBridge.h"
#include "CypherTileEditorSettingsDialog.h"
#include "CypherTileOrthoCamera.h"

#include <QPointF>
#include <QRectF>
#include <QWidget>

#include <functional>
#include <span>
#include <vector>

class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QWheelEvent;
class QKeyEvent;

namespace cypher::tools::tile_editor
{

struct tile_ortho_material_cache_t;
enum class tile_canvas_tool_t : unsigned char;

enum class tile_editor_ortho_plane_t : unsigned char {
    FRONT = 0u,
    SIDE
};

// These views project the same generated boxes used by CypherRender. They
// share the authored document and cell selection, but keep independent view
// transforms. No second renderer or copied editing document is created.
class CypherTileOrthoView final : public QWidget
{
public:
    using selection_callback_t = std::function<void( tile_map_grid_coord_t )>;
    using multi_selection_callback_t = std::function<void( bool, tile_map_grid_coord_t, Qt::KeyboardModifiers )>;
    using selection_cells_callback_t = std::function<void( const std::vector<tile_map_grid_coord_t> &, Qt::KeyboardModifiers )>;
    using context_menu_callback_t = std::function<void( const QPoint & )>;
    using navigation_callback_t = std::function<void( const tile_ortho_camera_state_t & )>;
    using stamp_callback_t = std::function<void( tile_map_grid_coord_t, i16 )>;

    explicit CypherTileOrthoView(
        tile_editor_ortho_plane_t plane,
        QWidget *pParent = nullptr );
    ~CypherTileOrthoView() override;

    void setDocumentBridge( CypherTileDocumentBridge *pBridge );
    void setPreferences( const tile_editor_preferences_t &preferences );
    // Borrowed from the owning editor; the cache outlives the view.
    void setMaterialCache( const tile_ortho_material_cache_t *pCache );
    void refreshDocument();
    void fitToView();
    void fitSelection();
    void setSelection( bool bHasSelection, tile_map_grid_coord_t coordinate );
    void setSelectionRect( bool bHasSelection, tile_map_grid_rect_t region );
    void setSelectedCells( std::span<const tile_map_grid_coord_t> cells );
    std::span<const tile_map_grid_coord_t> selectedCells() const { return m_selectedCells; }
    void setSelectionCallback( selection_callback_t callback );
    void setMultiSelectionCallback( multi_selection_callback_t callback );
    void setSelectionCellsCallback( selection_cells_callback_t callback );
    void setTool( tile_canvas_tool_t tool );
    void setPaint( const tile_map_paint_t &paint );
    void setConstructionCell( tile_map_grid_coord_t cell );
    void setDoorSide( tile_map_marker_side_t side );
    void setChangedCallback( std::function<void()> callback );
    void setPreviewChangedCallback( std::function<void()> callback );
    void setPaintPickedCallback( std::function<void( const tile_map_paint_t & )> callback );
    void setStatusCallback( std::function<void( const QString &, bool )> callback );
    void setContextMenuCallback( context_menu_callback_t callback );
    void setNavigationChangedCallback( navigation_callback_t callback );
    void setStampCallback( stamp_callback_t callback );
    void setPanToolEnabled( bool enabled );
    QPointF viewOrigin() const { return m_origin; }
    bool isPanning() const { return m_bPanning; }
    qreal pixelsPerUnit() const { return m_pixelsPerUnit; }
    tile_editor_ortho_plane_t plane() const { return m_plane; }
    tile_ortho_camera_state_t orthographicCameraState() const;
    void synchronizeOrthographicCamera( const tile_ortho_camera_state_t &state );
    QString viewMetrics() const;
    QString materialDescription() const;

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
    void leaveEvent( QEvent *pEvent ) override;

private:
    QPointF worldToScreen( qreal horizontal, qreal height ) const;
    QRectF projectedBox( const tile_map_geometry_box_t &box ) const;
    QRectF displayedBox( const tile_map_geometry_box_t &box ) const;
    bool isBoxVisible( const tile_map_geometry_box_t &box ) const;
    qreal depth( const tile_map_geometry_box_t &box ) const;
    bool isSelected( const tile_map_geometry_box_t &box ) const;
    const tile_map_geometry_box_t *boxAt( const QPointF &position ) const;
    void stopPanning();
    void updatePanCursor();
    void cancelSelectionDrag();
    bool constructionCellAt( const QPointF &position, tile_map_grid_coord_t &cell ) const;
    bool floorLevelAt( const QPointF &position, i16 &level ) const;
    bool supportsProjectedAuthoring() const;
    QString constructionSliceDescription( const QPointF *pPosition = nullptr ) const;
    void updateInteractionDescription();
    void beginAuthoring( const QPointF &position );
    void applyAuthoring( const QPointF &position );
    void finishAuthoring( bool commit );
    void notifyAuthoringPreview();
    void notifyNavigationChanged();

    tile_editor_ortho_plane_t m_plane;
    CypherTileDocumentBridge *m_pBridge{ nullptr };
    const tile_ortho_material_cache_t *m_pMaterialCache{ nullptr };
    tile_editor_preferences_t m_preferences{};
    tile_map_geometry_t m_geometry{};
    std::vector<usize> m_drawOrder{};
    bool m_bGeometryInitialized{ false };
    QString m_geometryError{};

    QPointF m_origin{};
    qreal m_pixelsPerUnit{ 24.0 };
    bool m_bHasFit{ false };
    bool m_bUserNavigated{ false };
    bool m_bPanning{ false };
    bool m_bPanToolEnabled{ false };
    bool m_bSpaceHeld{ false };
    Qt::MouseButton m_panButton{ Qt::NoButton };
    bool m_bContextMenuCandidate{ false };
    QPointF m_contextMenuPress{};
    QPoint m_contextMenuGlobal{};
    QPointF m_lastMouse{};
    QPointF m_cursorPosition{};
    bool m_bCursorInside{ false };
    std::vector<tile_map_grid_coord_t> m_selectedCells{};
    bool m_bSelecting{ false };
    bool m_bMarquee{ false };
    QPointF m_selectionAnchor{};
    QPointF m_selectionCurrent{};
    Qt::KeyboardModifiers m_selectionModifiers{ Qt::NoModifier };
    tile_canvas_tool_t m_tool{};
    tile_map_paint_t m_paint{};
    tile_map_paint_t m_strokePaint{};
    tile_map_grid_coord_t m_constructionCell{};
    tile_map_marker_side_t m_doorSide{ tile_map_marker_side_t::NORTH };
    bool m_bEditing{ false };
    bool m_bDrawingRun{ false };
    tile_map_grid_coord_t m_authoringAnchor{};
    tile_map_grid_coord_t m_authoringCurrent{};
    std::function<void()> m_changedCallback{};
    std::function<void()> m_previewChangedCallback{};
    std::function<void( const tile_map_paint_t & )> m_paintPickedCallback{};
    std::function<void( const QString &, bool )> m_statusCallback{};
    context_menu_callback_t m_contextMenuCallback{};
    navigation_callback_t m_navigationCallback{};
    selection_callback_t m_selectionCallback{};
    multi_selection_callback_t m_multiSelectionCallback{};
    selection_cells_callback_t m_selectionCellsCallback{};
    stamp_callback_t m_stampCallback{};
};

} // namespace cypher::tools::tile_editor

#endif // CYPHER_TOOLS_TILEEDITOR_ORTHOVIEW_H
