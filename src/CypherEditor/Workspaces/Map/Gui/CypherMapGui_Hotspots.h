//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_Hotspots.h
//  Purpose: Declares finding, loading and saving a material's hotspot
//           rectangle file on disk (CypherMap_Hotspots.h for the format).
//  Details: The map workspace never sees the asset catalogue: the
//           application gives each workspace a resolver from a virtual
//           asset path (materials/x.cymat) to the file on disk.
//
//  History:
//  - Created by Karlo Siric on 2026-10-08
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_MAP_GUI_HOTSPOTS_H
#define CYPHER_EDITOR_MAP_GUI_HOTSPOTS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMapGui_Workspace.h"
#include "CypherMap_Hotspots.h"

#include <functional>
#include <vector>

#include <QString>
#include <QStringList>

namespace cypher::editor::map
{

// The file a virtual asset path names, or empty when no content root has it.
using map_asset_file_fn = std::function<QString( const QString &virtualPath )>;
void MapHotspots_SetResolver( const map_workspace_t *pWorkspace, map_asset_file_fn resolver );

// The resolver for other content (game profiles): the file a virtual path
// names, and a number that changes whenever the resolver is set or cleared,
// so what was read through an old resolver is read again (0 without one).
CYPHER_NODISCARD QString MapAssets_File( const map_workspace_t *pWorkspace, const QString &virtualPath );
CYPHER_NODISCARD common::u64 MapAssets_Generation( const map_workspace_t *pWorkspace );

// The virtual paths of the content files with one of the extensions
// (".cysnd"; all files when empty), sorted, for asset pickers. The
// application installs it with its content roots; none without one.
using map_asset_list_fn = std::function<QStringList( const QStringList &extensions )>;
// The content folders, highest priority first: where new content files are
// saved and what a virtual path is relative to. Installed with the lister.
void MapAssets_SetRoots( const map_workspace_t *pWorkspace, const QStringList &roots );
CYPHER_NODISCARD QStringList MapAssets_Roots( const map_workspace_t *pWorkspace );
// The virtual path of a file inside a content folder; empty outside them.
CYPHER_NODISCARD QString MapAssets_VirtualPath( const map_workspace_t *pWorkspace, const QString &file );
void MapAssets_SetLister( const map_workspace_t *pWorkspace, map_asset_list_fn lister );
CYPHER_NODISCARD QStringList MapAssets_List( const map_workspace_t *pWorkspace, const QStringList &extensions );

// The material's rectangle file on disk: the existing one, else where a new
// one goes (beside the material's own file). Empty when the material cannot
// be found.
CYPHER_NODISCARD QString MapHotspots_File( const map_workspace_t *pWorkspace, const QString &materialPath );

// The material's regions (none when it has no rectangle file or it cannot
// be read; *pProblemOut says which).
CYPHER_NODISCARD std::vector<geometry::mesh_hotspot_rect_t> MapHotspots_Load( const map_workspace_t *pWorkspace, const QString &materialPath,
    QString *pProblemOut = nullptr );

// Writes the material's regions (an empty list removes the file).
CYPHER_NODISCARD bool MapHotspots_Save( const map_workspace_t *pWorkspace, const QString &materialPath,
    const std::vector<geometry::mesh_hotspot_rect_t> &rects, QString *pFileOut = nullptr );

} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_GUI_HOTSPOTS_H
