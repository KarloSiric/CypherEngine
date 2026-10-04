//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_ToolPanels.h
//  Purpose: Declares the Map workspace's Hammer 5 panels: Tool Properties,
//           Active Material, Auto Vis Groups, and Selection Sets.
//  Details: Tool Properties follows the active tool. Its sections are the
//           tool's keys (read from the keymap, so rebinding shows at once),
//           its options (settings, so the dialog, the panel, and the file
//           agree), and its operations (commands, so buttons, menus, and
//           the console run the same thing).
//
//  History:
//  - Created by Karlo Siric on 2026-09-30
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_MAP_GUI_TOOL_PANELS_H
#define CYPHER_EDITOR_MAP_GUI_TOOL_PANELS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMapGui_Workspace.h"

#include <QStringList>

class QWidget;
class QDialog;
class QEvent;

namespace cypher::editor::map
{

inline constexpr const char *MAP_PANEL_TOOL_PROPERTIES = "map.tool_properties";
inline constexpr const char *MAP_PANEL_ACTIVE_MATERIAL = "map.active_material";
inline constexpr const char *MAP_PANEL_VISGROUPS = "map.visgroups";
inline constexpr const char *MAP_PANEL_SELECTION_SETS = "map.selection_sets";

CYPHER_NODISCARD QWidget *MapToolProperties_Create( QWidget *pParent, map_workspace_t *pWorkspace );
// Optional, pane-owned cancellation bridge for focused tool buttons and the
// non-editable key reference. The caller resolves the active geometric pane's
// effective Cancel gesture. Return true only when the event was handled;
// handling may replace the tool and destroy the originating control.
// The borrowed context must outlive the panel. A null function detaches it.
using map_tool_cancel_event_fn = bool ( * )( void *pContext, QEvent *pEvent );
void MapToolProperties_SetCancelHandler( QWidget *pPanel, map_tool_cancel_event_fn pfnHandler, void *pContext );
// For tests: the current tool/selection-profile title, key rows ("[Shift+X]
// Clipping Tool"), setting paths and registered operation IDs in the active
// profile. Collapsed sections remain in the inventory; mode-hidden sections
// do not. Switching selection profiles keeps their controls alive.
CYPHER_NODISCARD QString MapToolProperties_Title( QWidget *pPanel );
CYPHER_NODISCARD QStringList MapToolProperties_KeyRows( QWidget *pPanel );
CYPHER_NODISCARD QStringList MapToolProperties_Options( QWidget *pPanel );
CYPHER_NODISCARD QStringList MapToolProperties_Operations( QWidget *pPanel );

// Numeric object transforms use the same transactional edits as viewport
// drags. Modeless: the selection can be changed while this window is open.
CYPHER_NODISCARD QDialog *MapTransformDialog_Create( QWidget *pParent, map_workspace_t *pWorkspace );

CYPHER_NODISCARD QWidget *MapActiveMaterial_Create( QWidget *pParent, map_workspace_t *pWorkspace );
// True when the preview shows the material's image rather than the stand-in.
CYPHER_NODISCARD bool MapActiveMaterial_HasImage( QWidget *pPanel );

CYPHER_NODISCARD QWidget *MapVisgroups_Create( QWidget *pParent, map_workspace_t *pWorkspace );
// For tests: each category's object count as "Brushes 6".
CYPHER_NODISCARD QStringList MapVisgroups_Rows( QWidget *pPanel );
// Hammer's Presets row: the current visibility saved by name (in
// editor.map.visgroup_presets, user scope) and loaded back.
CYPHER_NODISCARD bool MapVisgroups_SavePreset( QWidget *pPanel, const QString &name );
CYPHER_NODISCARD bool MapVisgroups_LoadPreset( QWidget *pPanel, const QString &name );
CYPHER_NODISCARD bool MapVisgroups_DeletePreset( QWidget *pPanel, const QString &name );
CYPHER_NODISCARD QStringList MapVisgroups_Presets( QWidget *pPanel );

CYPHER_NODISCARD QWidget *MapSelectionSets_Create( QWidget *pParent, map_workspace_t *pWorkspace );

} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_GUI_TOOL_PANELS_H
