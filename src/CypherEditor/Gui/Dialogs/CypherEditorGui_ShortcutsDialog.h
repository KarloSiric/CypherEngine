//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_ShortcutsDialog.h
//  Purpose: Declares the Keyboard Shortcuts reference: every command the
//           editor has, its keys in the active keymap, and the held keys
//           and mouse gestures of the views, grouped and searchable.
//  Details: Hammer's F1 command list and NetRadiant's Shortcuts window. Keys
//           Window rows describe the live QAction shortcuts. Context rows
//           describe keymap declarations resolved through inheritance and
//           the host platform overlay; the owning tool/view still decides
//           their activation and priority. Held keys and mouse gestures are
//           context-qualified declarations too. Copy includes the context.
//
//  History:
//  - Created by Karlo Siric on 2026-10-02
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GUI_SHORTCUTS_DIALOG_H
#define CYPHER_EDITOR_GUI_SHORTCUTS_DIALOG_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherEditorGui_Actions.h"
#include "CypherEditorGui_Application.h"

#include <QStringList>

class QDialog;
class QWidget;

namespace cypher::editor::gui
{

// The list is built when the dialog opens; pGui and pActions must outlive it.
CYPHER_NODISCARD QDialog *EditorShortcutsDialog_Create( QWidget *pParent, editor_gui_t *pGui, const editor_actions_t *pActions );

// Rebuilds from the current commands and keymap; active keymap changes also
// refresh the dialog automatically. This does not change any bindings.
void EditorShortcutsDialog_Refresh( QDialog *pDialog );
void EditorShortcutsDialog_SetSearch( QDialog *pDialog, const QString &text );
// Shows only rows that have valid keys or gestures.
void EditorShortcutsDialog_SetBoundOnly( QDialog *pDialog, bool bBoundOnly );

// Visible rows, "Group\tCommand\tPortableKeys\tContext", in display order.
// PortableKeys is empty for an unbound row; the UI explains its status.
CYPHER_NODISCARD QStringList EditorShortcutsDialog_Rows( QDialog *pDialog );
// The visible list as plain text, aligned, one command per line.
CYPHER_NODISCARD QString EditorShortcutsDialog_Text( QDialog *pDialog );

// "Map Tools" for "map.tool.block", "Edit" for "edit.undo": the group a
// command ID is listed under.
CYPHER_NODISCARD QString EditorShortcutsDialog_GroupOf( const QString &commandId );

} // namespace cypher::editor::gui

#endif // CYPHER_EDITOR_GUI_SHORTCUTS_DIALOG_H
