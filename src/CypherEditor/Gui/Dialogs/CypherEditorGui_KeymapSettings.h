//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_KeymapSettings.h
//  Purpose: Declares the staged Keybindings page in Settings. Keyboard,
//           held-key and mouse declarations share the existing .cykeymap
//           format and resolver; editing never changes live input until Apply.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GUI_KEYMAP_SETTINGS_H
#define CYPHER_EDITOR_GUI_KEYMAP_SETTINGS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherEditorGui_Application.h"

#include <QStringList>

class QWidget;

namespace cypher::editor::gui
{

inline constexpr const char *EDITOR_KEYBINDINGS_PAGE = "Keybindings";

// The initialized GUI and its command registry must outlive the page.
// Apply saves user documents atomically and selects the resulting keymap.
CYPHER_NODISCARD QWidget *EditorKeymapSettings_Create( QWidget *pParent, editor_gui_t *pGui, const QString &userKeymapFolder );
CYPHER_NODISCARD QStringList EditorKeymapSettings_Keywords();
CYPHER_NODISCARD bool EditorKeymapSettings_CanClose( QWidget *pPage );

// Settings integration, automation and contracts. Selecting a preset and
// importing a file are staged, just like changing a trigger.
CYPHER_NODISCARD bool EditorKeymapSettings_SelectKeymap( QWidget *pPage, const QString &id );
CYPHER_NODISCARD QString EditorKeymapSettings_CurrentKeymap( QWidget *pPage );
// New starts a sparse child of an installed profile. Duplicate copies the
// current staged document, preserving its base and unknown members. Neither
// publishes until Apply; existing profile IDs/files are never overwritten.
CYPHER_NODISCARD bool EditorKeymapSettings_NewProfile( QWidget *pPage, const QString &id, const QString &name, const QString &baseId );
CYPHER_NODISCARD bool EditorKeymapSettings_DuplicateProfile( QWidget *pPage, const QString &id, const QString &name );
// The host owns the reference dialog and callback context; both must outlive
// the page. A null callback disables the reference button.
void EditorKeymapSettings_SetReferenceCallback( QWidget *pPage, void ( *pCallback )( void * ), void *pContext );
void EditorKeymapSettings_SetFilter( QWidget *pPage, const QString &text );
void EditorKeymapSettings_SetPlatform( QWidget *pPage, keymap_platform_t platform );
// Each visible row: category, ID, input kind, context, canonical triggers,
// origin, availability, separated by tabs. Empty triggers mean unbound.
CYPHER_NODISCARD QStringList EditorKeymapSettings_Rows( QWidget *pPage );
CYPHER_NODISCARD bool EditorKeymapSettings_SetTriggers( QWidget *pPage, keymap_section_t section, keymap_platform_t platform,
                                                      const QString &context, const QString &id, const QStringList &triggers );
CYPHER_NODISCARD bool EditorKeymapSettings_Reset( QWidget *pPage, keymap_section_t section, keymap_platform_t platform,
                                                const QString &context, const QString &id );
CYPHER_NODISCARD QStringList EditorKeymapSettings_Conflicts( QWidget *pPage );
CYPHER_NODISCARD bool EditorKeymapSettings_HasChanges( QWidget *pPage );
void EditorKeymapSettings_Revert( QWidget *pPage );
CYPHER_NODISCARD QString EditorKeymapSettings_DraftText( QWidget *pPage );
CYPHER_NODISCARD bool EditorKeymapSettings_Import( QWidget *pPage, const QString &path );
CYPHER_NODISCARD bool EditorKeymapSettings_Export( QWidget *pPage, const QString &path );
// Export above retains the authored base dependency. Portable export resolves
// all supported-platform declarations and removes base, preserving this
// document's metadata/unknown platforms. It requires a complete base chain.
CYPHER_NODISCARD bool EditorKeymapSettings_ExportPortable( QWidget *pPage, const QString &path );
// Empty ID/name use the page's editable identity fields. A modified built-in
// preset requires a distinct user ID; an unchanged preset can be selected.
CYPHER_NODISCARD bool EditorKeymapSettings_Apply( QWidget *pPage, const QString &id = {}, const QString &name = {} );
CYPHER_NODISCARD QString EditorKeymapSettings_Status( QWidget *pPage );

} // namespace cypher::editor::gui

#endif // CYPHER_EDITOR_GUI_KEYMAP_SETTINGS_H
