//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_ObjectTools.h
//  Purpose: Declares the everyday object commands Hammer 5 and TrenchBroom
//           have and Mason lacked (gap analysis 6.2): picking the next
//           object under the cursor, dropping objects onto the surface
//           below, rotating by the angle snap from the keyboard, clearing
//           entity rotation and scale, a bounding box around the selection,
//           and flipping, turning and nudging materials from the keyboard.
//  Details: The views report where the pointer is (a ray in 3D, a point
//           and its axes in 2D), so the cycling commands and Alt+click
//           agree on what is "under the cursor". Objects along a ray are
//           found on the wireframe the views draw (faces, and entity boxes),
//           nearest first; under a 2D point, smallest outline first, the
//           order the 2D pick already prefers.
//
//  History:
//  - Created by Karlo Siric on 2026-10-13
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_MAP_GUI_OBJECT_TOOLS_H
#define CYPHER_EDITOR_MAP_GUI_OBJECT_TOOLS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMapGui_Workspace.h"

#include "CypherEditor_Commands.h"

#include <functional>
#include <vector>

#include <QPointF>
#include <QRectF>
#include <QString>

class QObject;

namespace cypher::editor::map
{

// Where the pointer is, reported by the views as it moves.
void MapObjectTools_SetPointerRay( const map_workspace_t *pWorkspace, math::vec3d_t origin, math::vec3d_t direction ) noexcept;
void MapObjectTools_SetPointerPoint( const map_workspace_t *pWorkspace, math::vec3d_t point, common::u32 axisU, common::u32 axisV ) noexcept;
void MapObjectTools_Forget( const map_workspace_t *pWorkspace ) noexcept;

// Selectable objects the ray meets, nearest first.
CYPHER_NODISCARD std::vector<common::u64> MapObjectTools_ObjectsAlongRay( const map_workspace_t *pWorkspace, math::vec3d_t origin, math::vec3d_t direction );
// Selectable objects whose outline in a 2D view (axes u, v) holds the point,
// smallest outline first.
CYPHER_NODISCARD std::vector<common::u64> MapObjectTools_ObjectsUnderPoint( const map_workspace_t *pWorkspace, math::vec3d_t point, common::u32 axisU, common::u32 axisV );

// Selects the object `step` places after (or before, when negative) the
// selected one in `under`; the first when the selection is not one of them.
// Says which of how many it picked. False when nothing is under the cursor.
bool MapObjectTools_Cycle( map_workspace_t *pWorkspace, const std::vector<common::u64> &under, int step );

// How far the object can fall straight down before it rests on visible
// geometry that is not in `moving`; false when nothing is below it.
CYPHER_NODISCARD bool MapObjectTools_DropDistance( const map_workspace_t *pWorkspace, common::u64 object, const std::vector<common::u64> &moving, common::f64 *pDistanceOut );

// Hammer's Pick Entity: the next click in a view names an entity for a
// property (a target field's eyedropper). The entity under the click, or
// the one owning the geometry clicked, answers with its name; the pick is
// dropped when `owner` (the editor that asked) is gone. Any click ends it.
using map_entity_pick_fn = std::function<void( common::u64 entity, const QString &name )>;
void MapObjectTools_BeginEntityPick( map_workspace_t *pWorkspace, QObject *pOwner, map_entity_pick_fn pick );
CYPHER_NODISCARD bool MapObjectTools_EntityPickPending( const map_workspace_t *pWorkspace ) noexcept;
void MapObjectTools_CancelEntityPick( map_workspace_t *pWorkspace ) noexcept;
// A click in a view while a pick is pending (`clicked`: what the view picks
// there, 0 for nothing). True: the click was the pick's.
bool MapObjectTools_ResolveEntityPick( map_workspace_t *pWorkspace, common::u64 clicked );

// Hammer's Snap to Vertices (editor.map.snap_to_vertices): a move whose
// corner comes within `pixels` of a corner of other visible geometry on
// screen is adjusted so the two meet exactly. Only the world axes in `axes`
// change (a 2D view's two, a handle's one, all three for a free 3D move).
// The target corners are indexed once per drag (`pDrag`, `dragStart`
// identify it), so the test per mouse move costs one projection per moving
// corner. True when it snapped; *pTo is the corner it snapped onto.
using map_project_fn = std::function<bool( math::vec3d_t point, QPointF &screen )>;
bool MapObjectTools_SnapToVertices( const map_workspace_t *pWorkspace, const void *pDrag, QPointF dragStart, const map_project_fn &project,
    QRectF viewport, common::u32 axes, common::f64 pixels, math::vec3d_t *pDelta, math::vec3d_t *pTo = nullptr );
// The corner the move in progress last snapped onto (for a marker).
CYPHER_NODISCARD bool MapObjectTools_VertexSnapTarget( const map_workspace_t *pWorkspace, math::vec3d_t *pTo ) noexcept;
void MapObjectTools_ClearVertexSnap( const map_workspace_t *pWorkspace ) noexcept;

CYPHER_NODISCARD command_registry_status_t MapObjectTools_Register( map_workspace_t *pWorkspace, command_registry_t *pRegistry ) noexcept;

} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_GUI_OBJECT_TOOLS_H
