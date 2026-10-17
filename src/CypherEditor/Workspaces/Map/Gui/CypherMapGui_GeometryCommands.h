//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_GeometryCommands.h
//  Purpose: Declares the registration of the map's geometry operation
//           commands: Hammer's vertex, edge and face tools, the mesh object
//           operations, Carve, flips, 90-degree rotation, alignment,
//           snapping and the selection queries built on mesh topology.
//  Details: These commands replace the disabled placeholders the workspace
//           listed for the same IDs, so menus, toolbars, Tool Properties and
//           keymaps light up without knowing who implements them. Every edit
//           publishes one undo step through MapWorkspace_ApplyEdit; every
//           state callback enables a command only for a selection it can
//           actually change.
//
//           Directions follow the last hovered 2D view (Top when none was):
//           Flip Horizontally mirrors across its vertical axis, Align Left
//           uses its horizontal minimum, Rotate 90 turns about its depth.
//
//  History:
//  - Created by Karlo Siric on 2026-10-04
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_MAP_GUI_GEOMETRY_COMMANDS_H
#define CYPHER_EDITOR_MAP_GUI_GEOMETRY_COMMANDS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMapGui_Workspace.h"
#include "CypherMap_TextureEdit.h"

#include <vector>

namespace cypher::editor::map
{

// Registers the operations with pWorkspace as their context. Call before the
// workspace registers its placeholders: an ID registered here is skipped there.
CYPHER_NODISCARD command_registry_status_t MapGeometryCommands_Register( map_workspace_t *pWorkspace, command_registry_t *pRegistry ) noexcept;

// What a geometry command needs before it can run, as a short phrase for a
// disabled button's tooltip ("two or more vertices"); null when none is
// recorded.
CYPHER_NODISCARD const char *MapGeometryCommands_Requirement( const char *pCommand ) noexcept;
// Why a command that ran found nothing to change ("the selection is already
// on the grid"), for the message after it; null when there is no phrase.
CYPHER_NODISCARD const char *MapGeometryCommands_NothingToDo( const char *pCommand ) noexcept;

// Apply Current Material (Shift+T): the active material on the picked brush
// face or mesh faces, or on every face of the selected brushes, meshes and
// patches. Posts a notice either way (what changed, or what is needed).
CYPHER_NODISCARD bool MapGeometryCommands_CanApplyMaterial( const map_workspace_t *pWorkspace ) noexcept;
CYPHER_NODISCARD bool MapGeometryCommands_ApplyMaterial( map_workspace_t *pWorkspace ) noexcept;

// The faces a texture tool acts on: the picked brush face or mesh faces,
// else every face of the selected brushes and meshes (patches excluded).
CYPHER_NODISCARD std::vector<map_surface_ref_t> MapGeometryCommands_TextureTargets( const map_workspace_t *pWorkspace );
// Fast Texture: fits the targets' texture coordinates into `rect` as one
// undo step, keeping the selection; posts a notice either way.
CYPHER_NODISCARD bool MapGeometryCommands_FitTexture( map_workspace_t *pWorkspace, const map_uv_rect_t &rect, map_uv_fit_mode_t mode ) noexcept;
// Apply Hotspots: every target face takes the hotspot region of its own
// material that fits it best, as one undo step; posts a notice either way.
CYPHER_NODISCARD bool MapGeometryCommands_ApplyHotspots( map_workspace_t *pWorkspace ) noexcept;

} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_GUI_GEOMETRY_COMMANDS_H
