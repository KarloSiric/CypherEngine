//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_ToolGestures.h
//  Purpose: Declares the viewport gestures of the creation and modelling
//           tools: Entity, Path, Decal, Overlay, Polygon, Patch, Curve,
//           Measure, Eyedropper, Mirror, Paint, Knife, Loop Cut, Vertex,
//           Pivot and Workplane; and the Clip tool on mesh selections.
//  Details: Views forward mouse, confirm and cancel input while a tool here
//           owns the pane, and paint its overlay last. A view describes
//           itself once per event (projection, ray through the cursor), so
//           tools work identically in 2D and 3D panes without knowing which
//           widget they draw in. Staged points belong to the workspace's
//           gesture state; edits publish through the workspace's undo path.
//
//  History:
//  - Created by Karlo Siric on 2026-10-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_MAP_GUI_TOOL_GESTURES_H
#define CYPHER_EDITOR_MAP_GUI_TOOL_GESTURES_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMapGui_Workspace.h"

#include <QPointF>
#include <QRectF>

class QPainter;

namespace cypher::editor::map
{

// What a pane tells a tool about itself.
using map_tool_project_fn = bool ( * )( const void *pView, math::vec3d_t world, QPointF *pScreen );
// Edge tools and ordinary component selection share the pane's visibility,
// clipping and ordering policy. FAILED is never an empty-space click.
enum class map_tool_edge_query_result_t : common::u8 { MISS, HIT, FAILED };
struct map_tool_edge_query_t {
    QPointF screen{};
    common::f64 tolerance{ 0.0 };
    common::u64 onlyMesh{ 0u };
    const math::planed_t *pOnPlane{ nullptr };
    const geometry::mesh_edge_ref_t *pAllowedEdges{ nullptr }; // Null: unrestricted; otherwise only these edges.
    common::usize nAllowedEdges{ 0u };
    // Borrowed only for this synchronous query: the unmodified conversion of
    // the same live brush named by onlyMesh. Supplies canonical mesh vertex IDs
    // before publication; the live scene still governs visibility/occlusion.
    const geometry::mesh_source_t *pCandidateMesh{ nullptr };
};
struct map_tool_edge_hit_t {
    common::u64 mesh{ 0u };
    geometry::mesh_edge_ref_t edge{};
    common::f64 t{ 0.0 }; // On the original, unclipped canonical edge a -> b.
    math::vec3d_t a{}, b{}; // Positions correspond to edge.a and edge.b.
};
using map_tool_pick_edge_fn = map_tool_edge_query_result_t ( * )( const void *pView, const map_tool_edge_query_t &query, map_tool_edge_hit_t *pHit );
struct map_tool_view_t {
    const void *pView{ nullptr };
    map_tool_project_fn pfnProject{ nullptr }; // World to pane pixels; false when not visible (behind the camera).
    bool bOrtho{ false };
    common::u32 axisU{ 0u }, axisV{ 1u }; // 2D panes: world axes along screen x and y.
    common::f64 pixelsPerUnit{ 1.0 };     // 2D panes: zoom.
    math::vec3d_t eye{};                  // 3D panes: camera position.
    QRectF rect{};
    map_tool_pick_edge_fn pfnPickEdge{ nullptr }; // Native panes supply this; synthetic projection-only test views may omit it.
};

enum class map_tool_event_kind_t : common::u8 { PRESS, MOVE, RELEASE, CONFIRM, CANCEL, BACKSPACE };

struct map_tool_event_t {
    map_tool_event_kind_t kind{ map_tool_event_kind_t::MOVE };
    QPointF screen{};
    math::vec3d_t rayOrigin{}, rayDirection{}; // Through the cursor; 2D panes look along -depth from far in front.
    Qt::KeyboardModifiers modifiers{};
    bool bButtonDown{ false };
    map_tool_view_t view{};
};

// True when the workspace's active tool takes this pane's input here.
CYPHER_NODISCARD bool MapToolGestures_Owns( const map_workspace_t *pWorkspace, bool bOrthoPane ) noexcept;
// Tools this module implements; the workspace makes them available.
CYPHER_NODISCARD bool MapToolGestures_Implements( map_tool_t tool ) noexcept;
// True when the event was used (the pane repaints and stops handling it).
CYPHER_NODISCARD bool MapToolGestures_Event( map_workspace_t *pWorkspace, const map_tool_event_t &event ) noexcept;
// A component press lets the pane pick first. Capture that completed pick so
// a later selection/document change cannot resurrect the pre-press selection.
void MapToolGestures_CompletePick( map_workspace_t *pWorkspace, const void *pView ) noexcept;
// Retires invalid Paint/Knife/Sculpt/Arc/Shear input during workspace notification, before listeners
// can restore a hidden target or disabled conversion setting. Lookup only;
// does not allocate or notify. Returns true when its overlay was discarded.
CYPHER_NODISCARD bool MapToolGestures_ValidateCaptures( map_workspace_t *pWorkspace ) noexcept;
// Borrowed presentation for a valid active sculpt stroke; null otherwise.
// The live document/wire remain authoritative and unchanged until release.
// Use synchronously for drawing only; do not retain across input/notifications.
CYPHER_NODISCARD const map_wireframe_t *MapToolGestures_SculptPreviewWire( const map_workspace_t *pWorkspace ) noexcept;
// Cancels a pending/direct component drag, component gizmo, lasso, face extrusion
// or paint/sculpt/arc/shear drag owned by this pane when its input/projection is invalidated. Lookup only: keeps
// the pivot, workplane and staged multi-click construction in other tools.
// Returns true when a captured interaction was dropped; never commits an edit.
bool MapToolGestures_CancelViewDrag( map_workspace_t *pWorkspace, const void *pView ) noexcept;
void MapToolGestures_Paint( const map_workspace_t *pWorkspace, QPainter &painter, const map_tool_view_t &view ) noexcept;
// Drops all staged state of a workspace (its shutdown).
void MapToolGestures_Release( const map_workspace_t *pWorkspace ) noexcept;

// Where a click lands, snapped as the tools place things: the grid point
// under the cursor in a 2D pane; in 3D the surface under it, or the ground.
CYPHER_NODISCARD bool MapToolGestures_PlacePoint( const map_workspace_t *pWorkspace, const map_tool_event_t &event, math::vec3d_t *pPointOut ) noexcept;
// The visible face under a 3D cursor (brush, mesh, patch or terrain) and its
// outward normal; false in a 2D pane or over empty space. On an axis-aligned
// face the point lies exactly on the plane.
CYPHER_NODISCARD bool MapToolGestures_SurfacePoint( const map_workspace_t *pWorkspace, const map_tool_event_t &event, math::vec3d_t *pPointOut,
    math::vec3d_t *pNormalOut ) noexcept;
// Places the Pivot tool's point.
void MapToolGestures_SetPivot( map_workspace_t *pWorkspace, math::vec3d_t point ) noexcept;
// The Pivot tool's point, used by Rotate 90 and the flips; false when unset.
CYPHER_NODISCARD bool MapToolGestures_Pivot( const map_workspace_t *pWorkspace, math::vec3d_t *pPivotOut ) noexcept;
void MapToolGestures_ClearPivot( map_workspace_t *pWorkspace ) noexcept;
// Numeric transforms of the selected vertices, edges or face (the Move,
// Rotate and Scale panels in component modes): what a gizmo drag would do,
// from typed values. Rotation is degrees about X, Y, Z; scale factors must be
// positive. The pivot is the components' centre, the world origin, or the
// Pivot tool's point (the centre when none is placed). A brush face only
// translates.
enum class map_component_transform_t : common::u8 { TRANSLATE = 0u, ROTATE, SCALE };
enum class map_component_pivot_t : common::u8 { CENTER = 0u, WORLD_ORIGIN, PIVOT_POINT };
CYPHER_NODISCARD bool MapToolGestures_CanTransformComponents( const map_workspace_t *pWorkspace, map_component_transform_t kind ) noexcept;
CYPHER_NODISCARD bool MapToolGestures_TransformComponents( map_workspace_t *pWorkspace, map_component_transform_t kind,
    math::vec3d_t values, map_component_pivot_t pivot ) noexcept;

// Repeat the latest successful component transform/extension on the current
// components of its original mesh. Stores values, never stale component IDs.
// Presence includes a disabled/stale recipe: callers must not fall back to an
// unrelated command when RepeatModelling refuses the current context.
CYPHER_NODISCARD bool MapToolGestures_HasModellingRepeat( const map_workspace_t *pWorkspace ) noexcept;
CYPHER_NODISCARD bool MapToolGestures_RepeatModelling( map_workspace_t *pWorkspace ) noexcept;
void MapToolGestures_SupersedeModellingRepeat( const map_workspace_t *pWorkspace ) noexcept;
// History alone may rebind a recipe to a fully prepared document. Call only
// immediately before its non-failing publication; does not suppress capture cancellation.
void MapToolGestures_HistoryWillRestore( map_workspace_t *pWorkspace, const map_document_t *pPreparedDocument ) noexcept;

} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_GUI_TOOL_GESTURES_H
