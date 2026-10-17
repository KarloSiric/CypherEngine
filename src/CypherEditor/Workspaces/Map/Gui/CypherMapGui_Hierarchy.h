//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_Hierarchy.h
//  Purpose: Declares Hammer 5's hierarchy commands for entity attachment
//           (CYMAP `parent`, CypherMap_Hierarchy.h).
//  Details: map.entity.reparent (P) attaches the selected entities to the
//           one picked last; map.entity.unparent (Ctrl+P) detaches them;
//           map.select.parent (Ctrl+PageUp) and map.select.children
//           (Ctrl+PageDown, every depth) walk the hierarchy. Selected
//           geometry stands for the entity that owns it. The selection is
//           kept sorted, so the order things were picked in is tracked
//           here, from the workspace's selection notifications.
//
//  History:
//  - Created by Karlo Siric on 2026-10-14
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_MAP_GUI_HIERARCHY_H
#define CYPHER_EDITOR_MAP_GUI_HIERARCHY_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMapGui_Workspace.h"

#include <vector>

namespace cypher::editor::map
{

CYPHER_NODISCARD command_registry_status_t MapHierarchy_Register( map_workspace_t *pWorkspace, command_registry_t *pRegistry ) noexcept;
// The selected entities (owners standing in for their geometry), each once,
// in the order they were picked.
CYPHER_NODISCARD std::vector<common::u64> MapHierarchy_SelectedEntities( const map_workspace_t *pWorkspace );
// The workspace calls this on every selection change, before listeners.
void MapHierarchy_SelectionChanged( map_workspace_t *pWorkspace ) noexcept;
void MapHierarchy_Forget( const map_workspace_t *pWorkspace ) noexcept;

} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_GUI_HIERARCHY_H
