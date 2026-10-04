//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_Dialogs.h
//  Purpose: Declares the Map workspace's small dialogs: Map Info and Go To.
//  Details: Map Info is NetRadiant's Map Info and TrenchBroom's map
//           inspector summary: how many objects of each kind, entities by
//           class (select every entity of a class from there), materials,
//           layers, connections, and the map's extent. Go To is NetRadiant's
//           Find Brush and TrenchBroom's Move Camera To in one place: an
//           object ID or an entity name selects and frames that object; a
//           position "x y z" frames every view on that point.
//
//  History:
//  - Created by Karlo Siric on 2026-10-02
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_MAP_GUI_DIALOGS_H
#define CYPHER_EDITOR_MAP_GUI_DIALOGS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMapGui_Workspace.h"

#include <QString>
#include <QStringList>
#include <QVector>

class QDialog;
class QWidget;

namespace cypher::editor::map
{

struct map_info_class_t {
    QString className;
    int nCount{ 0 };
};

// What Map Info shows, computed from the open map.
struct map_info_t {
    int nBrushes{ 0 };
    int nMeshes{ 0 };
    int nPatches{ 0 };
    int nTerrains{ 0 };
    int nEntities{ 0 };
    int nPointEntities{ 0 };  // Entities with no geometry of their own.
    int nBrushEntities{ 0 };  // Entities that own geometry (doors, triggers).
    int nTiedObjects{ 0 };    // Geometry owned by an entity.
    int nLayers{ 0 };
    int nMaterials{ 0 };
    int nConnections{ 0 };
    int nBrokenConnections{ 0 }; // Outputs with no target.
    int nBrokenBrushes{ 0 };
    int nChunkFiles{ 0 };
    int nProblems{ 0 };
    map_bounds_t bounds{};
    QVector<map_info_class_t> classes{}; // Most common first, then by name.
};

CYPHER_NODISCARD map_info_t MapInfo_Compute( const map_workspace_t *pWorkspace );
// The summary as plain text, for the clipboard.
CYPHER_NODISCARD QString MapInfo_Text( const map_workspace_t *pWorkspace );

CYPHER_NODISCARD QDialog *MapInfoDialog_Create( QWidget *pParent, map_workspace_t *pWorkspace );
void MapInfoDialog_Refresh( QDialog *pDialog );
// Selects every entity of the class (NetRadiant's Select All Of Type).
// Returns how many were selected.
int MapInfo_SelectClass( map_workspace_t *pWorkspace, const QString &className );

// Asset paths the selected objects use: brush side, mesh face and patch
// materials (as edited, saved or not), and every asset path in their
// records (entity models, sounds, prefabs). Sorted, without duplicates.
// The Asset Browser window's Selection tab.
CYPHER_NODISCARD QStringList MapInfo_SelectionAssets( const map_workspace_t *pWorkspace );

enum class map_go_to_status_t : common::u8 {
    OBJECT = 0u,   // Selected and framed an object.
    POSITION,      // Framed the views on a point.
    NOT_FOUND,     // No object has that ID or name.
    INVALID        // Neither an ID, a name, nor three numbers.
};

// "1310" (object ID), "#1310", "spawn_a" (entity name, then class), or
// "128 -64 32" / "128, -64, 32" (position). The message says what happened.
CYPHER_NODISCARD map_go_to_status_t MapGoTo_Run( map_workspace_t *pWorkspace, const QString &text, QString *pMessageOut = nullptr );

CYPHER_NODISCARD QDialog *MapGoToDialog_Create( QWidget *pParent, map_workspace_t *pWorkspace );

} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_GUI_DIALOGS_H
