//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_Autosave.h
//  Purpose: Declares map autosave (TrenchBroom and Hammer keep numbered
//           backup copies of an open map while it has unsaved work).
//  Details: An autosave is a copy: the open map, its path and its modified
//           state are untouched. Copies go to an `autosave` folder beside
//           the map as <name>.<n>.cymap, n rising;
//           only the newest `keep` copies stay. An untitled map autosaves
//           into the application's data folder.
//
//  History:
//  - Created by Karlo Siric on 2026-10-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_MAP_GUI_AUTOSAVE_H
#define CYPHER_EDITOR_MAP_GUI_AUTOSAVE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMapGui_Workspace.h"

#include <QString>

namespace cypher::editor::map
{

enum class map_autosave_status_t : common::u8 {
    SAVED = 0u, // A new copy was written.
    UNCHANGED,  // No unsaved work, or none since the last copy.
    BUSY,       // A drag or other transaction is open; try again later.
    FAILED      // The copy could not be written (the map is unaffected).
};

struct map_autosave_t {
    // The published document last copied. Every edit publishes a new
    // document, so a different pointer means there is new work to copy.
    const map_document_t *pLastCopied{ nullptr };
};

// Where this workspace's autosaves go.
CYPHER_NODISCARD QString MapAutosave_Folder( const map_workspace_t *pWorkspace );

// Writes a numbered copy into `folder` when the map has unsaved work it
// has not copied yet, then deletes all but the newest `keep` copies of this
// map there. *pPathOut (optional) receives the copy's root path.
CYPHER_NODISCARD map_autosave_status_t MapAutosave_Run( const map_workspace_t *pWorkspace, map_autosave_t *pState, const QString &folder,
    common::u32 keep, QString *pPathOut );

} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_GUI_AUTOSAVE_H
