//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_CommandPalette.h
//  Purpose: Declares the command palette: a search box over every command
//           the editor has, opened with Ctrl+Shift+P.
//  Details: Commands first (ADR 0008): anything registered is reachable
//           here by name without knowing its menu or shortcut, and each row
//           shows the shortcut, so the palette also teaches the keymap.
//           Matching is the editor-wide fuzzy matcher over the label and the
//           command ID. Commands the palette ran most recently come first
//           when the box is empty. Disabled commands stay listed, dimmed, so
//           a user sees that a command exists but does not apply right now.
//
//  History:
//  - Created by Karlo Siric on 2026-09-29
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GUI_COMMAND_PALETTE_H
#define CYPHER_EDITOR_GUI_COMMAND_PALETTE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherEditorGui_Actions.h"

#include <QStringList>

class QWidget;

namespace cypher::editor
{
struct settings_registry_t;
}

namespace cypher::editor::gui
{

inline constexpr int EDITOR_PALETTE_MAX_RESULTS = 200;
inline constexpr int EDITOR_PALETTE_MAX_RECENT = 8;

// Creates the palette, hidden, over pWindow. pActions and optional settings
// must outlive it. Palette preferences are read each time results refill.
CYPHER_NODISCARD QWidget *EditorCommandPalette_Create(
    QWidget *pWindow, editor_actions_t *pActions, const settings_registry_t *pSettings = nullptr );

// Shows it at the top of its window with an empty query and focus in the box.
void EditorCommandPalette_Open( QWidget *pPalette );

// For tests and scripting: set the query, read the command IDs listed in
// order, and run the highlighted one (as Enter does). Accept returns false
// when nothing is highlighted or the command is disabled.
void EditorCommandPalette_SetQuery( QWidget *pPalette, const QString &query );
CYPHER_NODISCARD QStringList EditorCommandPalette_Results( QWidget *pPalette );
CYPHER_NODISCARD bool EditorCommandPalette_Accept( QWidget *pPalette );

} // namespace cypher::editor::gui

#endif // CYPHER_EDITOR_GUI_COMMAND_PALETTE_H
