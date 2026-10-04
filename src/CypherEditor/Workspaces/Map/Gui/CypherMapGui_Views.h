//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_Views.h
//  Purpose: Declares the Map workspace's viewports: the 2D orthographic
//           views (Top, Front, Side), the 3D camera view, and the four-way
//           arrangement Hammer made standard.
//  Details: All views draw the workspace wireframe with QPainter. That is
//           a stand-in for the 3D view until the renderer lands - it shows
//           the map's shape and lets the camera controls be settled now -
//           and it is the permanent way the 2D views draw.
//
//           2D controls follow Hammer: wheel zooms about the cursor, the
//           middle button or Space + left button pans, left click selects
//           (Ctrl toggles). The 3D view flies Hammer-style while the right
//           button is held: mouse to look, W/A/S/D to move, E/PageUp for
//           up, Q/PageDown for down, Shift faster and Alt slower.
//           Wheel direction, cursor targeting, sensitivity and horizontal FOV
//           follow editor camera settings.
//
//  History:
//  - Created by Karlo Siric on 2026-09-29
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_MAP_GUI_VIEWS_H
#define CYPHER_EDITOR_MAP_GUI_VIEWS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMapGui_Workspace.h"

#include <QByteArray>
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QString>

class QWidget;
class QEvent;

namespace cypher::editor::map
{

inline constexpr common::f64 MAP_VIEW_ZOOM_MIN = 1.0 / 128.0; // Pixels per world unit.
inline constexpr common::f64 MAP_VIEW_ZOOM_MAX = 64.0;
inline constexpr common::f64 MAP_VIEW_PICK_PIXELS = 4.0;      // Click tolerance.
inline constexpr int MAP_VIEW_GRID_MIN_PIXELS = 4;            // Default adaptive spacing; settings may override it.

// The world axes a 2D view shows across and up, Hammer's names.
enum class map_ortho_axes_t : common::u8 {
    TOP = 0u, // x across, y up
    FRONT,    // y across, z up
    SIDE      // x across, z up
};

CYPHER_NODISCARD QWidget *MapOrthoView_Create( QWidget *pParent, map_workspace_t *pWorkspace, map_ortho_axes_t axes );

// View state, for tests and for status displays.
CYPHER_NODISCARD common::f64 MapOrthoView_Zoom( const QWidget *pView );
// Actually drawn world-space step, including adaptive display coarsening.
// Snapping continues to use the workspace grid size.
CYPHER_NODISCARD common::f64 MapOrthoView_GridStep( const QWidget *pView );
CYPHER_NODISCARD QPointF MapOrthoView_WorldToView( const QWidget *pView, QPointF world );
CYPHER_NODISCARD QPointF MapOrthoView_ViewToWorld( const QWidget *pView, QPointF view );
// Visible wire/marker hit under a logical-pixel position; does not select.
CYPHER_NODISCARD common::u64 MapOrthoView_Pick( const QWidget *pView, QPointF position );

// Authored mesh component hit. An edge is a canonical pair of persistent
// vertex IDs, never a wireframe index or a transient mesh pool handle.
// These queries are active only in Select + Edges and do not change selection.
// Orthographic panes retain their wireframe depth view; camera hits require
// the edge to be unobscured by a physical surface at its projected position.
struct map_mesh_edge_hit_t {
    common::u64 object{ 0u };
    geometry::mesh_edge_ref_t edge{};
};
CYPHER_NODISCARD bool MapOrthoView_PickMeshEdge( const QWidget *pView, QPointF position, map_mesh_edge_hit_t *pOut );

CYPHER_NODISCARD QWidget *MapCameraView_Create( QWidget *pParent, map_workspace_t *pWorkspace );

// Camera position and facing, for tests.
CYPHER_NODISCARD math::vec3d_t MapCameraView_Position( const QWidget *pView );
CYPHER_NODISCARD math::vec3d_t MapCameraView_Forward( const QWidget *pView );
// Current held-key velocity in world units/second, used by the flight tick
// and diagnostics. Command modifiers suppress movement; diagonals normalize.
CYPHER_NODISCARD math::vec3d_t MapCameraView_NavigationVelocity( QWidget *pView, Qt::KeyboardModifiers modifiers = Qt::NoModifier );
// Project one world point for editor overlays; false behind the near plane.
// Returned coordinates may lie outside the viewport rectangle.
CYPHER_NODISCARD common::bool_t MapCameraView_WorldToView( QWidget *pView, math::vec3d_t point, QPointF *pOut );
// Closest visible authored surface or point-entity marker; patch views pick
// the displayed control net. No document/selection mutation is performed.
CYPHER_NODISCARD common::u64 MapCameraView_Pick( QWidget *pView, QPointF position );
CYPHER_NODISCARD bool MapCameraView_PickMeshEdge( QWidget *pView, QPointF position, map_mesh_edge_hit_t *pOut );

// The object drawn in viewport.hover: what a click in this view would select
// (editor.viewport.hover_highlight). 0 when the pointer is over nothing, is
// outside the view, or a button or navigation key is held. Either view kind.
CYPHER_NODISCARD common::u64 MapView_HoveredObject( const QWidget *pView );

// Last rendered camera-relative floor grid. Useful for viewport diagnostics;
// no more than this many candidate segments are submitted per paint.
inline constexpr common::u32 MAP_CAMERA_GRID_MAX_SEGMENTS = 1026u;
struct map_camera_grid_info_t {
    math::vec3d_t center{};
    common::f64 step{ 0.0 };
    common::f64 halfExtent{ 0.0 };
    common::u32 nCandidateSegments{ 0u };
    common::u32 nVisibleSegments{ 0u };
    QRectF screenBounds{};
};
CYPHER_NODISCARD map_camera_grid_info_t MapCameraView_GridInfo( const QWidget *pView );

// Combined visible selected geometry bounds in world units; point-entity
// picking helpers are excluded because they have no authored physical size.
CYPHER_NODISCARD map_bounds_t MapViews_SelectionGeometryBounds( const map_workspace_t *pWorkspace );

// What a view pane shows.
// How the 3D view draws: Hammer's 3d modes that a CPU view can honour.
enum class map_render_mode_t : common::u8 {
    WIREFRAME = 0u, // Edges only.
    SHADED,         // Material colours lit by a fixed sun (Hammer's F6).
    FULLBRIGHT,     // Material colours, unlit (F5).
    NORMALS,        // Face normals as colour (a tools visualisation, F7).
    COUNT
};

CYPHER_NODISCARD map_render_mode_t MapCameraView_RenderMode( const QWidget *pView );
void MapCameraView_SetRenderMode( QWidget *pView, map_render_mode_t mode );

enum class map_view_type_t : common::u8 {
    CAMERA = 0u, // 3D
    TOP,
    FRONT,
    SIDE,
    ASSETS,      // Rich asset browser embedded in this pane.
    DATABASE,    // Asset/entity recipe database.
    SHADERS,     // Shader recipe/stage editor.
    COUNT
};

// Application-owned content sources remain outside the geometry workspace.
// Configure before restoring presentation. The source/context must outlive
// the grid; each returned widget is owned by its pane and cached while hidden
// so switching views preserves drafts. Null reports unavailable content.
using map_view_content_create_fn = QWidget *( * )( QWidget *pParent, map_view_type_t type, void *pContext );
void MapViews_SetContentFactory( QWidget *pViews, map_view_content_create_fn pfnCreate, void *pContext );

inline constexpr int MAP_VIEW_PANE_COUNT = 4;

// Every arrangement of one to four panes. Panes fill an arrangement in
// reading order (pane 0 first); panes it does not use stay alive, hidden,
// with their cameras. Values are stored in saved sessions: append only.
enum class map_view_arrangement_t : common::u8 {
    FOUR = 0u,             // 2 x 2.
    HAMMER,                // One large pane above two.
    PERSPECTIVE,           // One pane.
    TWO,                   // Two side by side.
    TWO_ROWS,              // Two stacked.
    THREE_COLUMNS,
    THREE_ROWS,
    TWO_TOP_ONE_BOTTOM,
    ONE_LEFT_TWO_RIGHT,    // One large pane beside two stacked.
    TWO_LEFT_ONE_RIGHT,
    ONE_TOP_THREE_BOTTOM,
    THREE_TOP_ONE_BOTTOM,
    ONE_LEFT_THREE_RIGHT,
    THREE_LEFT_ONE_RIGHT,
    FOUR_COLUMNS,
    FOUR_ROWS,
    COUNT
};

// How an arrangement is built: up to two groups of panes, the groups
// stacked (rows) or side by side (columns), each group split the other way.
struct map_view_arrangement_shape_t {
    common::bool_t bColumns{ common::CY_FALSE };
    int counts[2]{ 1, 0 };
};
CYPHER_NODISCARD map_view_arrangement_shape_t MapViews_ArrangementShape( map_view_arrangement_t arrangement ) noexcept;
CYPHER_NODISCARD int MapViews_ArrangementPaneCount( map_view_arrangement_t arrangement ) noexcept;
// The arrangement's icon ("layout-one-top-two-bottom").
CYPHER_NODISCARD const char *MapViews_ArrangementIcon( map_view_arrangement_t arrangement ) noexcept;

// The view workspace, laid out like CypherTileEditor's: four panes in a
// continuous dark surface with gradient gutters, each with a compact overlay
// header (pane number, view chooser, frame, maximize, options, close)
// instead of a title bar, so the views keep their full area. Hammer's
// arrangement by default: 3D top left, Top top right, Front bottom left,
// Side bottom right, with one shared crosshair splitter. Double-clicking a
// header maximizes that pane; again restores the arrangement.
CYPHER_NODISCARD QWidget *MapViews_Create( QWidget *pParent, map_workspace_t *pWorkspace );

// NetRadiant's background image under a 2D pane (a plan or elevation to
// trace), across a world rectangle in that pane's axes. Each pane keeps one
// image per 2D projection; 3D panes ignore it. Not saved with the map.
void MapViews_SetPaneBackground( QWidget *pViews, int iPane, const QImage &image, const QRectF &worldRect, common::f64 opacity = 0.5 );
void MapViews_ClearPaneBackground( QWidget *pViews, int iPane );
CYPHER_NODISCARD bool MapViews_PaneHasBackground( QWidget *pViews, int iPane );

// Hammer's active viewport: the visible pane that last held keyboard focus.
// View commands (F2 Top, F3 Front, F4 Side, Ctrl+Space cycle) act on it.
CYPHER_NODISCARD int MapViews_ActivePane( QWidget *pViews );
// Hidden/closed panes cannot acquire focus. Embedded editors focus a usable
// child control rather than their non-interactive container.
void MapViews_SetActivePane( QWidget *pViews, int iPane );
// Advance focus through visible panes in reading order. A maximized/single
// pane is a no-op. Embedded editors receive focus in a usable child control;
// ordinary Tab traversal within those editors remains their own behavior.
CYPHER_NODISCARD bool MapViews_CycleActivePane( QWidget *pViews );
// Narrow tool-control bridge: resolve Cancel in the active geometric pane
// and reuse that pane's cancellation path without moving keyboard focus.
// Other keys, embedded editor panes, popups and dialogs keep their own input.
CYPHER_NODISCARD bool MapViews_HandleToolCancel( QWidget *pViews, QEvent *pEvent );

// Whole-object keyboard translation in the active orthographic projection.
// Horizontal/vertical are signed unit directions, with exactly one nonzero.
// The step is the authored grid size, or one world unit for fine movement;
// adaptive display spacing and the snap toggle do not alter it. Active edit,
// pan and construction gestures, component modes and unwritable selections
// are ineligible. Uses the normal atomic move/undo/persistence path.
CYPHER_NODISCARD bool MapViews_CanNudgeSelection( QWidget *pViews );
CYPHER_NODISCARD bool MapViews_NudgeSelection( QWidget *pViews, int horizontal, int vertical, bool fine = false );

// Presets reset divider proportions and pane visibility, preserving view
// widgets and their cameras. The pane's chosen view type is never changed.
void MapViews_SetArrangement( QWidget *pViews, map_view_arrangement_t arrangement );
CYPHER_NODISCARD map_view_arrangement_t MapViews_Arrangement( QWidget *pViews );
CYPHER_NODISCARD QString MapViews_ArrangementTitle( map_view_arrangement_t arrangement );
// The arrangements in the order the layout picker shows them: by pane count.
CYPHER_NODISCARD const map_view_arrangement_t *MapViews_PickerArrangements( common::usize *pnOut ) noexcept;

// Versioned presentation state for user preferences: pane types, visibility,
// maximization, arrangement and divider sizes. Camera transforms belong to
// the live views and are not serialized here. Invalid input changes nothing.
CYPHER_NODISCARD QByteArray MapViews_SavePresentation( QWidget *pViews );
// Pure preflight, shared with restoration; no view widgets are required.
CYPHER_NODISCARD bool MapViews_ValidatePresentation( const QByteArray &state );
CYPHER_NODISCARD bool MapViews_RestorePresentation( QWidget *pViews, const QByteArray &state );

// Pane control, for commands, layouts, and tests. Panes are numbered 0..3
// in reading order. Closing the final open pane is ignored; maximizing a
// closed pane reopens it.
void MapViews_SetPaneType( QWidget *pViews, int iPane, map_view_type_t type );
// The tab's choice: a 2D projection, or the 3D view in a mode (kept for the
// pane while it shows a 2D view).
void MapViews_SetPaneView( QWidget *pViews, int iPane, map_view_type_t type, map_render_mode_t render );
CYPHER_NODISCARD map_render_mode_t MapViews_PaneRenderMode( QWidget *pViews, int iPane );
void MapViews_SetPaneMeshEdges( QWidget *pViews, int iPane, bool bOn );
CYPHER_NODISCARD bool MapViews_PaneMeshEdges( QWidget *pViews, int iPane );
void MapViews_SetPaneWireOverlay( QWidget *pViews, int iPane, bool bOn );
CYPHER_NODISCARD bool MapViews_PaneWireOverlay( QWidget *pViews, int iPane );
CYPHER_NODISCARD map_view_type_t MapViews_PaneType( QWidget *pViews, int iPane );
CYPHER_NODISCARD QWidget *MapViews_PaneView( QWidget *pViews, int iPane );
CYPHER_NODISCARD QString MapViews_PaneTitle( QWidget *pViews, int iPane );
void MapViews_SetMaximized( QWidget *pViews, int iPane ); // -1 restores the arrangement.
CYPHER_NODISCARD int MapViews_MaximizedPane( QWidget *pViews );
void MapViews_SetPaneVisible( QWidget *pViews, int iPane, bool bVisible );
CYPHER_NODISCARD bool MapViews_IsPaneVisible( QWidget *pViews, int iPane );
void MapViews_ShowAllPanes( QWidget *pViews );
// Frame selection (or the map when empty) in this pane only. Other panes
// keep their camera/zoom state; workspace-wide framing remains separate.
void MapViews_FramePane( QWidget *pViews, int iPane );

// "Top", "Perspective": a view type's name, as Hammer's view menus write it.
CYPHER_NODISCARD QString MapViews_TypeTitle( map_view_type_t type );

} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_GUI_VIEWS_H
