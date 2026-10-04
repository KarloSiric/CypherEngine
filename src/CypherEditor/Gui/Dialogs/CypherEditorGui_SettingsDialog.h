//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_SettingsDialog.h
//  Purpose: Declares the settings dialog, generated from the settings
//           registry: a page tree, a search box, and one typed editor per
//           setting, writing into the scope the user picks.
//  Details: Nothing in the dialog knows any particular setting; a module
//           that registers descriptors gets its page for free, plugins
//           included (ADR 0008). Every row shows where its value comes from
//           (Default, User, Project, Workspace) and can reset the chosen
//           scope's own value so it inherits again. Changes apply at once
//           through the registry, so the editor follows while the dialog is
//           open. Search runs the editor-wide fuzzy matcher over labels,
//           paths, and descriptions across every page.
//           Some pages are richer than a column of rows - Appearance is a
//           theme studio - so an application can add a page that is a
//           widget of its own; it joins the page tree and the search.
//
//  History:
//  - Created by Karlo Siric on 2026-09-29
//  - 2026-10-01: custom pages
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GUI_SETTINGS_DIALOG_H
#define CYPHER_EDITOR_GUI_SETTINGS_DIALOG_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherEditor_SettingsRegistry.h"
#include "CypherEditorGui_SettingsTransfer.h"

#include <QStringList>

class QDialog;
class QWidget;

namespace cypher::editor::gui
{
struct editor_style_t;

// Creates the dialog (not shown). The registry and optional style outlive it.
CYPHER_NODISCARD QDialog *EditorSettingsDialog_Create( QWidget *pParent, settings_registry_t *pRegistry, const editor_style_t *pStyle = nullptr );

// Enables reviewed import using the application's scope persistence policy.
// Export is always available; import review works without hooks, but Apply
// stays disabled until an atomic persistence handler is attached.
void EditorSettingsDialog_SetTransferHooks( QDialog *pDialog, const settings_transfer_hooks_t &hooks );

// Asked before the dialog closes; false keeps it open (the page had unsaved
// work and the user cancelled).
using editor_settings_page_close_fn = bool ( * )( QWidget *pPage );

// Adds a page that is a widget rather than generated rows. Custom pages come
// first in the tree, in the order added, and the first one added becomes the
// page the dialog opens on. Keywords make the page a search result. The
// page path must not be one the registry uses. The dialog owns the widget.
void EditorSettingsDialog_AddPage(
    QDialog *pDialog,
    const QString &page,
    QWidget *pWidget,
    const QStringList &keywords = {},
    editor_settings_page_close_fn pfnCanClose = nullptr );

// The page shown; empty while searching.
CYPHER_NODISCARD QString EditorSettingsDialog_CurrentPage( QDialog *pDialog );

// Pages the current search lists as links (custom pages whose title or
// keywords match).
CYPHER_NODISCARD QStringList EditorSettingsDialog_PageResults( QDialog *pDialog );

// Shows one page ("Viewports/Camera") and clears the search.
void EditorSettingsDialog_ShowPage( QDialog *pDialog, const QString &page );
void EditorSettingsDialog_SetSearch( QDialog *pDialog, const QString &text );

// The scope edits go to; only scopes with a store attached are offered.
void EditorSettingsDialog_SetTargetScope( QDialog *pDialog, settings_scope_t scope );

// Paths of the settings shown, in order, and the editor widget of one of
// them (for tests and automation).
CYPHER_NODISCARD QStringList EditorSettingsDialog_VisibleSettings( QDialog *pDialog );
CYPHER_NODISCARD QWidget *EditorSettingsDialog_EditorFor( QDialog *pDialog, const QString &path );

// The source text a row shows ("Default", "User", ...).
CYPHER_NODISCARD QString EditorSettingsDialog_SourceFor( QDialog *pDialog, const QString &path );

// Presses a row's reset button.
void EditorSettingsDialog_Reset( QDialog *pDialog, const QString &path );

} // namespace cypher::editor::gui

#endif // CYPHER_EDITOR_GUI_SETTINGS_DIALOG_H
