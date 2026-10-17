//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_Instances.h
//  Purpose: Declares prefab instances in the editor (CYMAP.md 6.11, Hammer
//           5's instances): each instance record names a `.cyprefab` file
//           and places it (origin, angles, scale); the editor reads the file
//           and shows the prefab's contents there as one object that is
//           picked, moved, turned, duplicated and deleted as a whole.
//  Details: After every rebuild of the live wireframe the instances are
//           added to it as PREFAB objects (their prefab's lines and faces,
//           transformed), so every view, the Outliner and picking see them
//           without knowing about prefab files. Prefab files are read once
//           and kept until they change on disk or Reload Prefabs.
//
//           Create Instance from Selection writes the selection to a
//           prefab file and puts an instance in its place; Collapse
//           Instance copies a prefab's objects into the map (fresh IDs,
//           the instance's overrides applied to its entities); Edit Prefab
//           collapses an instance into a group to edit in place, and
//           Finish Editing Prefab writes the group back to the file, so
//           every instance of it follows.
//
//  History:
//  - Created by Karlo Siric on 2026-10-13
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_MAP_GUI_INSTANCES_H
#define CYPHER_EDITOR_MAP_GUI_INSTANCES_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMapGui_Workspace.h"

#include "CypherEditor_Commands.h"

#include <QString>

namespace cypher::editor::map
{

// Adds every instance to the live wireframe. Call whenever it was rebuilt.
void MapInstances_Refresh( map_workspace_t *pWorkspace ) noexcept;
void MapInstances_Forget( const map_workspace_t *pWorkspace ) noexcept;
// Reads every prefab file again (after editing one outside Mason).
void MapInstances_ReloadFiles( map_workspace_t *pWorkspace ) noexcept;

// Finish Editing Prefab saves an external file, then transitions the map back
// to its instance. These resources do not share an atomic commit. If the map
// transition fails after saving, the session retains that fact and offers
// Finish retry or Cancel; Cancel restores the instance but does not undo the
// earlier file save. Map undo/redo likewise does not version external files.

struct map_instance_info_t {
    QString source{};      // Virtual path of the prefab file.
    QString name{};
    math::vec3d_t origin{}, angles{}, scale{ 1.0, 1.0, 1.0 };
    bool bLoaded{ false }; // The prefab file was read.
    QString problem{};     // Why it was not.
};
CYPHER_NODISCARD bool MapInstances_Info( const map_workspace_t *pWorkspace, common::u64 instance, map_instance_info_t *pInfoOut );
CYPHER_NODISCARD bool MapInstances_IsInstance( const map_workspace_t *pWorkspace, common::u64 id ) noexcept;

// Distance along the ray to the instance's nearest face; negative on a miss.
CYPHER_NODISCARD common::f64 MapInstances_Raycast( const map_workspace_t *pWorkspace, common::u64 instance, math::vec3d_t origin, math::vec3d_t direction ) noexcept;

// The instance's placement as an affine transform (origin, then angles as
// yaw, pitch, roll, then scale).
CYPHER_NODISCARD math::affine3d_t MapInstances_Transform( math::vec3d_t origin, math::vec3d_t angles, math::vec3d_t scale ) noexcept;

// Collapses every instance of the live map into `copy` (a clone of it): what
// compiling does, since the compiled level holds the prefabs' objects. An
// instance whose prefab cannot be read is left out and named in *pProblems.
CYPHER_NODISCARD map_status_t MapInstances_CollapseAll( map_workspace_t *pWorkspace, map_document_t *pCopy, QStringList *pProblems );

CYPHER_NODISCARD command_registry_status_t MapInstances_Register( map_workspace_t *pWorkspace, command_registry_t *pRegistry ) noexcept;

} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_GUI_INSTANCES_H
