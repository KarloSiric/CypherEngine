//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_Game.h
//  Purpose: Declares the workspace's game profile: `game/<id>.cygame` for
//           the map's `game` (CYPROJECT.md's default profile path), read
//           through the workspace's asset resolver (CypherMapGui_Hotspots.h)
//           and kept until the map's game or the resolver changes, or
//           Reload Game Profile reads it again.
//  Details: Without a profile everything still works with raw keys; with
//           one, new entities get their class's defaults, the entity tool
//           and Entity I/O offer the game's classes, inputs and outputs, and
//           Check for Problems checks values against their definitions.
//
//  History:
//  - Created by Karlo Siric on 2026-10-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_MAP_GUI_GAME_H
#define CYPHER_EDITOR_MAP_GUI_GAME_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMapGui_Workspace.h"
#include "CypherMap_GameProfile.h"

#include <QString>
#include <QStringList>

namespace cypher::editor::map
{

// The profile, loaded on first use; null when the map names no game or its
// profile cannot be loaded (MapGame_Status says why).
CYPHER_NODISCARD const map_game_profile_t *MapGame_Profile( const map_workspace_t *pWorkspace );
// Reads the profile again; a summary for the user (classes, problems).
CYPHER_NODISCARD map_status_t MapGame_Reload( const map_workspace_t *pWorkspace, QString *pSummaryOut = nullptr );
// Why there is no profile, or how the last load went.
CYPHER_NODISCARD QString MapGame_Status( const map_workspace_t *pWorkspace );
void MapGame_Forget( const map_workspace_t *pWorkspace );

CYPHER_NODISCARD const map_game_class_t *MapGame_Class( const map_workspace_t *pWorkspace, common::string_view_t className );

// Writes each default of the class the entity has no value for (what the
// entity was made with stays). Call on a private copy, inside an edit.
CYPHER_NODISCARD map_status_t MapGame_WriteDefaults( const map_workspace_t *pWorkspace, map_document_t *pMap, common::u64 entity,
    common::string_view_t className ) noexcept;

// Placeable classes of a kind, in the profile's category order (unlisted
// categories alphabetically, then by name).
CYPHER_NODISCARD std::vector<const map_game_class_t *> MapGame_Placeable( const map_workspace_t *pWorkspace, map_game_kind_t kind );

// Inputs and outputs a class has, for Entity I/O.
CYPHER_NODISCARD QStringList MapGame_Outputs( const map_workspace_t *pWorkspace, common::string_view_t className );
CYPHER_NODISCARD QStringList MapGame_Inputs( const map_workspace_t *pWorkspace, common::string_view_t className );

} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_GUI_GAME_H
