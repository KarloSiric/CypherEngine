//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_Panels.h
//  Purpose: Declares the Map workspace's panels: the outliner (every object,
//           grouped by layer) and the properties panel (the selected
//           object's live geometry, placement, materials and retained CYKV fields).
//  Details: Geometry/source inspection stays read-only. The entity key section
//           edits typed authoring data through prepared undo transactions.
//
//  History:
//  - Created by Karlo Siric on 2026-09-29
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_MAP_GUI_PANELS_H
#define CYPHER_EDITOR_MAP_GUI_PANELS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMapGui_Workspace.h"

class QWidget;

namespace cypher::editor::map
{

inline constexpr const char *MAP_PANEL_OUTLINER = "map.outliner";
inline constexpr const char *MAP_PANEL_PROPERTIES = "map.properties";

// Presentation metadata for Object Properties rows. These roles never store
// document IDs or editing state; deferred rows use them before loading children.
inline constexpr int MAP_PROPERTY_ICON_ROLE = Qt::UserRole + 40;
inline constexpr int MAP_PROPERTY_KIND_ROLE = Qt::UserRole + 41;
enum class map_property_row_kind_t : common::u8 { GROUP, VALUE, OBJECT, ARRAY, ARRAY_PAGE };

// Map and layers, authored groups, entities with tied geometry, and world
// geometry. Only object rows select; group membership is read-only. Filters
// narrow by text, type, viewport visibility, and selection without changing
// the document. Filter state lasts for this panel's session; hierarchy lines
// and named-node ID visibility follow user settings. Unnamed nodes retain IDs.
CYPHER_NODISCARD QWidget *MapOutliner_Create( QWidget *pParent, map_workspace_t *pWorkspace );

// Visible layer/object rows, regardless of branch expansion. Presentation
// containers (map and groups) are excluded from this legacy count.
CYPHER_NODISCARD int MapOutliner_VisibleRowCount( QWidget *pOutliner );
void MapOutliner_SetFilter( QWidget *pOutliner, const QString &text );

// Detailed geometry is described on demand and large arrays are paged.
// Entity keys resolve selected tied geometry to its owner, support typed
// add/edit/rename/remove and multi-owner edits, and preserve custom values.
CYPHER_NODISCARD QWidget *MapProperties_Create( QWidget *pParent, map_workspace_t *pWorkspace );

// "key = value" lines of what the panel shows, for tests.
CYPHER_NODISCARD QString MapProperties_Text( QWidget *pProperties );

} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_GUI_PANELS_H
