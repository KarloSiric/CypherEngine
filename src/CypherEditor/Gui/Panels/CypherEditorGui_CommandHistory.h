//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_CommandHistory.h
//  Purpose: Declares the Command History panel: every command run, however
//           it ran (menu, key, toolbar, console, script), with Repeat.
//  Details: Hammer 5's Command History and TrenchBroom's command
//           repetition. Select one or more entries and Repeat runs them
//           again in order with the same arguments - duplicate, move,
//           rotate, repeated for a spiral stair. With nothing selected,
//           Repeat runs the newest repeatable entry. Commands that only
//           look around (view, file, tools, help) are recorded but are not
//           repeatable, and the list hides them unless asked.
//           The application feeds the panel from the command registry's
//           observer, so the panel never replaces that observer.
//
//  History:
//  - Created by Karlo Siric on 2026-10-02
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GUI_COMMAND_HISTORY_H
#define CYPHER_EDITOR_GUI_COMMAND_HISTORY_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherEditorGui_Application.h"

#include <QStringList>

class QWidget;

namespace cypher::editor::gui
{

inline constexpr common::usize EDITOR_COMMAND_HISTORY_MAX = 1000u; // Oldest entries drop first.

CYPHER_NODISCARD QWidget *EditorCommandHistory_Create( QWidget *pParent, editor_gui_t *pGui );

// Appends one execution. Called from the application's command observer.
void EditorCommandHistory_Record( QWidget *pPanel, const command_desc_t &command, const command_args_t &args, command_result_t result );

// True for commands worth repeating: edits and selections, not looking
// around, files, dialogs, or undo itself.
CYPHER_NODISCARD bool EditorCommandHistory_IsRepeatable( common::string_view_t commandId ) noexcept;

// Runs the selected entries again in order, or the newest repeatable entry
// when none is selected. DISABLED when there is nothing to repeat.
CYPHER_NODISCARD command_result_t EditorCommandHistory_Repeat( QWidget *pPanel );

void EditorCommandHistory_Clear( QWidget *pPanel );
// Shows view, file, and dialog commands too (off by default).
void EditorCommandHistory_SetShowAll( QWidget *pPanel, bool bShowAll );
void EditorCommandHistory_SelectRows( QWidget *pPanel, const QList<int> &rows );

// Visible rows, oldest first: "map.grid.larger" or "map.grid 32".
CYPHER_NODISCARD QStringList EditorCommandHistory_Rows( QWidget *pPanel );

} // namespace cypher::editor::gui

#endif // CYPHER_EDITOR_GUI_COMMAND_HISTORY_H
