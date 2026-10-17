//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_EntityLook.h
//  Purpose: Declares how entities look in the views when the game profile
//           describes their class (CYGAME.md 4): a point entity's box from
//           the class `size`, its colour (the entity's own `editor.color`,
//           else the class's `color_from` property, else its `color`), and
//           the helpers drawn for a selected entity - facing arrow, radius,
//           spot cone, line to a target, travel direction, and size box.
//  Details: The looks are worked out once per published document, right
//           after the live wireframe is rebuilt, by resizing the entity
//           boxes in the wireframe itself: picking, framing and every view
//           then agree on the box without knowing about profiles. Helpers
//           read the entity's values when drawn, and only for the few
//           selected entities.
//
//  History:
//  - Created by Karlo Siric on 2026-10-13
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_MAP_GUI_ENTITY_LOOK_H
#define CYPHER_EDITOR_MAP_GUI_ENTITY_LOOK_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMapGui_Workspace.h"

#include <vector>

#include <QColor>
#include <QString>

namespace cypher::editor::map
{

// Resizes point entity boxes in the live wireframe to their class size and
// records each entity's colour. Call whenever the live wireframe was
// rebuilt; harmless without a profile (the boxes stay the default cube).
void MapEntityLook_Refresh( map_workspace_t *pWorkspace ) noexcept;
void MapEntityLook_Forget( const map_workspace_t *pWorkspace ) noexcept;

// The entity's colour in the views; invalid when nothing gives one (the
// visgroup colour is used).
CYPHER_NODISCARD QColor MapEntityLook_Colour( const map_workspace_t *pWorkspace, common::u64 entity ) noexcept;

struct map_helper_line_t {
    math::vec3d_t a{};
    math::vec3d_t b{};
};

// The world-space segments of the entity's helpers (circles as polylines).
void MapEntityLook_Helpers( const map_workspace_t *pWorkspace, common::u64 entity, std::vector<map_helper_line_t> &out );

// TrenchBroom's per-class visibility: entities of a hidden class, and the
// brushes they own, are not drawn or picked. Session state, like the auto
// visgroups.
void MapEntityLook_SetClassHidden( map_workspace_t *pWorkspace, const QString &className, bool hidden );
CYPHER_NODISCARD bool MapEntityLook_IsClassHidden( const map_workspace_t *pWorkspace, const char *pClassName ) noexcept;
CYPHER_NODISCARD bool MapEntityLook_AnyClassHidden( const map_workspace_t *pWorkspace ) noexcept;

// The unit vector [pitch, yaw, roll] degrees face (CYMAP.md: yaw about +Z
// counter-clockwise from +X, positive pitch looks down).
CYPHER_NODISCARD math::vec3d_t MapEntityLook_Forward( math::vec3d_t angles ) noexcept;

} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_GUI_ENTITY_LOOK_H
