//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_ThemeEditor.h
//  Purpose: Declares the theme editor: every registered theme token, grouped
//           as the catalogue groups them, edited live over the active theme
//           and saved as a complete `.cytheme`.
//  Details: The editor works on a draft: a complete copy of the theme that
//           was active when it opened, previewed through the whole editor on
//           every edit. Derived colours can be pinned to a value or set back
//           to "auto" to follow their formula again. Saving writes a complete
//           theme with no base (CYTHEME.md 3: every token, derived ones on
//           "auto" unless pinned) into the user theme folder, adds it to the
//           library, makes it active, and records it in editor.ui.theme;
//           closing without saving puts the previous theme back. Tokens no
//           loaded module registers are carried through unchanged.
//
//  History:
//  - Created by Karlo Siric on 2026-09-29
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GUI_THEME_EDITOR_H
#define CYPHER_EDITOR_GUI_THEME_EDITOR_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherEditorGui_Application.h"

#include <QColor>
#include <QString>
#include <QStringList>

class QApplication;
class QDialog;
class QWidget;

namespace cypher::editor::gui
{

// Creates the editor (not shown). Saved themes go to userThemeFolder.
CYPHER_NODISCARD QDialog *EditorThemeEditor_Create( QWidget *pParent, editor_gui_t *pGui, QApplication *pApplication, const QString &userThemeFolder );

// For tests and automation.
void EditorThemeEditor_SetSearch( QDialog *pEditor, const QString &text );
CYPHER_NODISCARD QStringList EditorThemeEditor_VisibleTokens( QDialog *pEditor );
void EditorThemeEditor_SetColor( QDialog *pEditor, const QString &tokenId, const QColor &color );
void EditorThemeEditor_SetAuto( QDialog *pEditor, const QString &tokenId );
void EditorThemeEditor_Revert( QDialog *pEditor );

// True when any token differs from the theme the editor opened on (or last
// saved).
CYPHER_NODISCARD bool EditorThemeEditor_HasChanges( QDialog *pEditor );

// Saves the draft as a complete theme under this ID and name; returns the
// file written, empty on failure.
CYPHER_NODISCARD QString EditorThemeEditor_Save( QDialog *pEditor, const QString &id, const QString &name );

} // namespace cypher::editor::gui

#endif // CYPHER_EDITOR_GUI_THEME_EDITOR_H
