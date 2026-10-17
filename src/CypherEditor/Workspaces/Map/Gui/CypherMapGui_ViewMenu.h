//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_ViewMenu.h
//  Purpose: Declares the editing menu a right click opens in a 2D or 3D
//           view (Hammer's and TrenchBroom's viewport context menu).
//  Details: The operations the current mode and selection allow come first,
//           then Cut/Copy/Paste with Paste Here, Place Entity Here and
//           Insert Prefab Here at the clicked point, and submenus for
//           selection, modes, objects, CSG, mesh, texture, patch and entity
//           work. Every item runs a registered command (same label, icon,
//           key and enabled state as the toolbar) or a "here" action that
//           uses the clicked point. The view's own display options stay one
//           submenu away.
//
//  History:
//  - Created by Karlo Siric on 2026-10-06
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_MAP_GUI_VIEW_MENU_H
#define CYPHER_EDITOR_MAP_GUI_VIEW_MENU_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMapGui_ToolGestures.h"

class QMenu;
class QWidget;

namespace cypher::editor::map
{

// Builds the menu for a right click described by `click` (the view's tool
// event at the cursor). pViewOptions (optional) is added as "View Options".
// The caller pops it up; it deletes itself when closed.
CYPHER_NODISCARD QMenu *MapViewMenu_Create( map_workspace_t *pWorkspace, const map_tool_event_t &click, QMenu *pViewOptions,
    QWidget *pParent ) noexcept;

// What a right click on `object` does to the selection first: in the modes
// that select whole objects, an unselected object becomes the selection, so
// the menu acts on what was clicked.
void MapViewMenu_SelectClicked( map_workspace_t *pWorkspace, common::u64 object ) noexcept;

// The "here" actions, for the menu and tests. Paste Here centres the
// clipboard's objects on the point (in a 2D pane only along its two axes;
// in 3D standing on it). Place Entity Here creates an entity of the class.
CYPHER_NODISCARD bool MapViewMenu_PasteAt( map_workspace_t *pWorkspace, const map_tool_event_t &click, math::vec3d_t point ) noexcept;
CYPHER_NODISCARD bool MapViewMenu_PlaceEntityAt( map_workspace_t *pWorkspace, common::string_view_t className, math::vec3d_t point ) noexcept;

} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_GUI_VIEW_MENU_H
