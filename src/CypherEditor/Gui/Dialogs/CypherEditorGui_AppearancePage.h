//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_AppearancePage.h
//  Purpose: Declares the Appearance page of Settings: the editor's look in
//           one place, laid out after the TileEditor's appearance page.
//  Details: Three parts, top to bottom.
//           - Colour themes: every preset and user theme in one list;
//             choosing one applies it at once and records it in
//             editor.ui.theme. Save, Save As, Import, Export, Delete, and
//             the full Theme Editor sit beside it.
//           - Interface scale and focus: text sizes, toolbar, palette, and
//             menu icon sizes, density, and the active-pane cues.
//           - Every theme colour, grouped by section, each with a swatch
//             and hex value (click for a live colour dialog), Auto for
//             colours that follow a formula, and a per-colour revert.
//           Edits go to a theme draft (CypherEditorGui_ThemeDraft.h) and
//           preview through the whole editor while you work; the theme list
//           shows "Custom (modified)" until they are saved as a theme or
//           reverted. Closing the dialog with unsaved colours asks.
//
//  History:
//  - Created by Karlo Siric on 2026-10-01
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GUI_APPEARANCE_PAGE_H
#define CYPHER_EDITOR_GUI_APPEARANCE_PAGE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherEditorGui_Application.h"

#include <QColor>
#include <QStringList>

class QApplication;
class QWidget;

namespace cypher::editor::gui
{

inline constexpr const char *EDITOR_APPEARANCE_PAGE = "Appearance";

// pGui must be initialized and outlive the page. User themes are saved to,
// imported into, and deleted from userThemeFolder.
CYPHER_NODISCARD QWidget *EditorAppearancePage_Create( QWidget *pParent, editor_gui_t *pGui, QApplication *pApplication, const QString &userThemeFolder );

// The settings dialog's close hook: unsaved colours ask Save / Discard /
// Cancel while the page is visible; a hidden page discards them.
CYPHER_NODISCARD bool EditorAppearancePage_CanClose( QWidget *pPage );

// Search keywords for the settings dialog.
CYPHER_NODISCARD QStringList EditorAppearancePage_Keywords();

// Automation and tests.
CYPHER_NODISCARD QStringList EditorAppearancePage_Themes( QWidget *pPage );      // Theme IDs in list order.
CYPHER_NODISCARD QString EditorAppearancePage_CurrentTheme( QWidget *pPage );    // Empty while modified.
void EditorAppearancePage_SelectTheme( QWidget *pPage, const QString &id );
CYPHER_NODISCARD QStringList EditorAppearancePage_ColorTokens( QWidget *pPage ); // Colour rows shown.
void EditorAppearancePage_SetFilter( QWidget *pPage, const QString &text );
void EditorAppearancePage_SetColor( QWidget *pPage, const QString &tokenId, const QColor &color );
void EditorAppearancePage_SetAuto( QWidget *pPage, const QString &tokenId );
void EditorAppearancePage_RevertColor( QWidget *pPage, const QString &tokenId );
void EditorAppearancePage_Revert( QWidget *pPage );
CYPHER_NODISCARD bool EditorAppearancePage_HasChanges( QWidget *pPage );
// Saves the draft as a user theme; returns the file, empty on failure.
CYPHER_NODISCARD QString EditorAppearancePage_SaveAs( QWidget *pPage, const QString &id, const QString &name );
// Moves a user theme's file to the trash and unloads it.
CYPHER_NODISCARD bool EditorAppearancePage_DeleteTheme( QWidget *pPage, const QString &id );
CYPHER_NODISCARD QString EditorAppearancePage_Status( QWidget *pPage );

} // namespace cypher::editor::gui

#endif // CYPHER_EDITOR_GUI_APPEARANCE_PAGE_H
