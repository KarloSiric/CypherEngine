//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_Layers.h
//  Purpose: Declares TrenchBroom's layer controls for Mason: the current
//           layer new objects go to, hiding a layer, locking one so nothing
//           in it can be picked, isolating one, and selecting everything in
//           a layer.
//  Details: Hidden and locked are editor state for this session, like the
//           auto visgroups; they are not written into the map. The current
//           layer lives on the document (map_document_t::creationLayer) so
//           every operation that creates objects without naming a layer
//           follows it, and an edit's undo keeps the one in use.
//
//  History:
//  - Created by Karlo Siric on 2026-10-13
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_MAP_GUI_LAYERS_H
#define CYPHER_EDITOR_MAP_GUI_LAYERS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMapGui_Workspace.h"

#include "CypherEditor_Commands.h"

#include <QString>

namespace cypher::editor::map
{

// By the layer's index in map_document_t::layers (what wire objects carry).
CYPHER_NODISCARD bool MapLayers_IsHidden( const map_workspace_t *pWorkspace, common::u32 iLayer ) noexcept;
CYPHER_NODISCARD bool MapLayers_IsLocked( const map_workspace_t *pWorkspace, common::u32 iLayer ) noexcept;
// Lock-only checks, independent of selection mode and visibility. Object
// lookup includes originless entities and authored records absent from views.
// Unknown/zero identities and null workspaces are not reported as locked.
CYPHER_NODISCARD bool MapLayers_HasLocked( const map_workspace_t *pWorkspace ) noexcept;
CYPHER_NODISCARD bool MapLayers_IsObjectLocked( const map_workspace_t *pWorkspace, common::u64 id ) noexcept;

// By layer ID. Hiding or locking a layer drops its objects from the
// selection. A changed lock state also invalidates captured edits even when
// only an indirectly affected child belongs to the layer. Repeating the same
// lock state does nothing. False for a layer the map does not have.
bool MapLayers_SetHidden( map_workspace_t *pWorkspace, const QString &layer, bool hidden );
bool MapLayers_SetLocked( map_workspace_t *pWorkspace, const QString &layer, bool locked );
CYPHER_NODISCARD bool MapLayers_IsHiddenId( const map_workspace_t *pWorkspace, const QString &layer );
CYPHER_NODISCARD bool MapLayers_IsLockedId( const map_workspace_t *pWorkspace, const QString &layer );

// The layer new objects go to.
bool MapLayers_SetCurrent( map_workspace_t *pWorkspace, const QString &layer );
CYPHER_NODISCARD QString MapLayers_Current( const map_workspace_t *pWorkspace );

void MapLayers_Forget( const map_workspace_t *pWorkspace ) noexcept;

CYPHER_NODISCARD command_registry_status_t MapLayers_Register( map_workspace_t *pWorkspace, command_registry_t *pRegistry ) noexcept;

} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_GUI_LAYERS_H
