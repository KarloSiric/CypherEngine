//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_Console.h
//  Purpose: Declares the editor console: the log from the first line of
//           start-up, filtered by level, channel, and text, with the
//           command line that runs every registered command - the same
//           console a player opens in the game.
//  Details: The console is a view of the editor's log store
//           (CypherEditorGui_Log.h); clearing it clears the store, so the
//           Problems panel and the counts agree. Lines read
//           "[   1.234] WARN  [Gui] message": uptime, level, channel, text,
//           coloured by the console theme tokens (CYTHEME.md 4.8).
//
//  History:
//  - Created by Karlo Siric on 2026-09-27
//  - 2026-09-30: rebuilt on the log store with filters, counts, and uptime
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GUI_CONSOLE_H
#define CYPHER_EDITOR_GUI_CONSOLE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherEditor_Commands.h"
#include "CypherEditorGui_Log.h"
#include "CypherEditorGui_Style.h"

#include <QString>

class QWidget;

namespace cypher::editor::gui
{

inline constexpr int EDITOR_CONSOLE_MAX_BLOCKS = 5000;   // Oldest lines drop from the view beyond this.
inline constexpr int EDITOR_CONSOLE_MAX_HISTORY = 200;

CYPHER_NODISCARD QWidget *EditorConsole_Create(
    QWidget *pParent,
    const command_registry_t *pRegistry,
    const editor_style_t *pStyle,
    editor_log_t *pLog );

// Adds a line to the log as console output (command results, help).
void EditorConsole_Append( QWidget *pConsole, common::log_level_t level, const QString &text );

// Runs a command line as if typed and entered.
void EditorConsole_Submit( QWidget *pConsole, const QString &line );

// console.clear and console.help, owned by the console widget.
CYPHER_NODISCARD command_registry_status_t EditorConsole_RegisterCommands( command_registry_t *pRegistry, QWidget *pConsole );

// Filters, for the toolbar and tests.
void EditorConsole_SetLevels( QWidget *pConsole, bool bMessages, bool bWarnings, bool bErrors );
void EditorConsole_SetSearch( QWidget *pConsole, const QString &text );

// The text the console shows now.
CYPHER_NODISCARD QString EditorConsole_Text( QWidget *pConsole );

} // namespace cypher::editor::gui

#endif // CYPHER_EDITOR_GUI_CONSOLE_H
