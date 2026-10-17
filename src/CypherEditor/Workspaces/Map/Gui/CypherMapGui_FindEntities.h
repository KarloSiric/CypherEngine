//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_FindEntities.h
//  Purpose: Declares Hammer's Find Entities (Ctrl+F) and Find and Replace
//           (Ctrl+Shift+H): entities whose class, name or property values
//           contain some text, listed to select and frame, and that text
//           replaced in their values as one undo step.
//
//  History:
//  - Created by Karlo Siric on 2026-10-08
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_MAP_GUI_FIND_ENTITIES_H
#define CYPHER_EDITOR_MAP_GUI_FIND_ENTITIES_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMapGui_Workspace.h"

#include <QString>
#include <QVector>

class QDialog;
class QWidget;

namespace cypher::editor::map
{

struct map_entity_match_t {
    common::u64 id{ 0u };
    QString key{};   // "class", "name", or the property that matched.
    QString value{};
};

// Entities with `text` (case-insensitive) in their class, name or a
// property value; only in `key` when it is not empty. Empty text matches
// every entity (each once, by class).
CYPHER_NODISCARD QVector<map_entity_match_t> MapFindEntities_Search( const map_workspace_t *pWorkspace, const QString &text, const QString &key = {} );

// Replaces `text` with `with` in the matching string values (class and name
// included) of `entities`, as one undo step. Duplicate entity IDs are visited
// once. Returns the number of changed values, 0 for a successful no-op, or -1
// for refusal/failure (with a notice). Any locked entity whose value would
// change refuses the whole batch; no authored changes or history are published.
CYPHER_NODISCARD int MapFindEntities_Replace( map_workspace_t *pWorkspace, const QVector<common::u64> &entities, const QString &text,
    const QString &with, const QString &key = {} );

// The window (one per workspace), in Find or Find and Replace form.
CYPHER_NODISCARD QDialog *MapFindEntities_Show( map_workspace_t *pWorkspace, QWidget *pParent, bool bReplace ) noexcept;

} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_GUI_FIND_ENTITIES_H
