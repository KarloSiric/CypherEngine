//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_EntityIo.h
//  Purpose: Declares the Entity I/O window: Hammer's Outputs and Inputs tabs
//           for the selected entity.
//  Details: Outputs are edited in place (output, target, input, parameter,
//           delay, times) with Add, Duplicate, Remove and reordering; every
//           change is one undo step through CypherMap_EntityOutputs. Inputs
//           list the connections of other entities that reach this one, as
//           the wireframe resolves them. Name suggestions: entity names for
//           targets; the game profile's outputs of the entity's class and
//           inputs of the target's classes first, then those already used.
//
//  History:
//  - Created by Karlo Siric on 2026-10-06
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_MAP_GUI_ENTITY_IO_H
#define CYPHER_EDITOR_MAP_GUI_ENTITY_IO_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMapGui_Workspace.h"

class QDialog;
class QTabWidget;
class QWidget;

namespace cypher::editor::map
{

// The workspace's Entity I/O window, created on first use (modeless; it
// follows the selection). Owned by the workspace: MapEntityIo_Release.
// Queued table writes are bound to their document/selection generation and
// cancelled by refresh/takeover. Writes require an unlocked resolved entity,
// a writable document and no active preview or history transaction.
CYPHER_NODISCARD QDialog *MapEntityIo_Show( map_workspace_t *pWorkspace, QWidget *pParent ) noexcept;

// The one entity the window edits: the selected entity, or the owner of the
// selected entity geometry; 0 when the selection is not one entity.
CYPHER_NODISCARD common::u64 MapEntityIo_Entity( const map_workspace_t *pWorkspace ) noexcept;

// Adds the Outputs and Inputs pages to `pTabs` (Object Properties shows them
// beside the entity's properties, as Hammer 5 does). They follow the
// selection and are owned by the tab widget.
void MapEntityIo_AddTabs( map_workspace_t *pWorkspace, QTabWidget *pTabs ) noexcept;

// Destroys the workspace's window (its shutdown).
void MapEntityIo_Release( const map_workspace_t *pWorkspace ) noexcept;

} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_GUI_ENTITY_IO_H
