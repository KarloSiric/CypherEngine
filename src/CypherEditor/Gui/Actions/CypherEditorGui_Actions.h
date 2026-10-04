//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_Actions.h
//  Purpose: Declares the bridge from the command registry to Qt: one QAction
//           per command, shortcuts from the keymap chain, and menus and
//           toolbars built from plain tables of command IDs.
//  Details: Commands first (ADR 0008): a menu entry, a toolbar button, a
//           context-menu item, a shortcut, and a console line all run the
//           same registered command, so every action is also scriptable and
//           the menus can never offer something the console cannot do.
//
//           Menu tables name the menu path ("File", "View/Panels") and a
//           command ID; a null command is a separator. Unknown commands are
//           skipped with a log line, so a table may name commands a plugin
//           provides and still build when the plugin is not loaded.
//
//           Modifiers follow Qt's platform convention: the keymap's Ctrl is
//           Qt::ControlModifier, which Qt maps to Command on macOS, so
//           "Ctrl+S" means Cmd+S there and Ctrl+S elsewhere.
//
//  History:
//  - Created by Karlo Siric on 2026-09-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GUI_ACTIONS_H
#define CYPHER_EDITOR_GUI_ACTIONS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherEditor_Commands.h"
#include "CypherEditor_Keymap.h"
#include "CypherEditorGui_Style.h"

#include <QHash>
#include <QKeySequence>
#include <QString>

class QAction;
class QMenu;
class QMenuBar;
class QToolBar;
class QWidget;

namespace cypher::editor::gui
{

struct editor_menu_item_t {
    const char *pMenu{ nullptr };    // "File", "View/Panels"; '/' nests submenus.
    const char *pCommand{ nullptr }; // Command ID; null inserts a separator.
};

struct editor_actions_t {
    const command_registry_t *pRegistry{ nullptr };
    const editor_style_t *pStyle{ nullptr }; // Icons are tinted from it.
    QWidget *pOwner{ nullptr };              // Parent of every QAction; its window scopes shortcuts.
    QHash<QString, QAction *> actions{};     // By command ID; owned by pOwner.
};

void EditorActions_Init(
    editor_actions_t *pActions,
    const command_registry_t *pRegistry,
    const editor_style_t *pStyle,
    QWidget *pOwner ) noexcept;

// The action for a command, created on first use. Null for an unknown or
// console-only command.
CYPHER_NODISCARD QAction *EditorActions_Get( editor_actions_t *pActions, const char *pCommand );

// Sets shortcuts from the keymap chain (most specific first) for one
// context, e.g. "global". Every bound command gets an action added to the
// owner, so its shortcut works even when no menu or toolbar shows it.
// Commands the chain does not mention lose their shortcuts.
void EditorActions_ApplyKeymap(
    editor_actions_t *pActions,
    const common::key_value_t *const *ppChain,
    common::usize nChain,
    common::string_view_t context );

// As ApplyKeymap for a context stack (most specific first) and a platform:
// for each command the first context that mentions it decides, and the
// platform overlay (`platforms.<platform>`) applies within each keymap.
// Window-wide shortcuts suit contexts that are active whenever the window
// is (`global`, the workspace's own); focus contexts such as viewports and
// panels dispatch keys themselves.
void EditorActions_ApplyKeymapStack(
    editor_actions_t *pActions,
    const common::key_value_t *const *ppChain,
    common::usize nChain,
    keymap_platform_t platform,
    const common::string_view_t *pContexts,
    common::usize nContexts );

// Appends menus to a menu bar. Returns the number of entries skipped.
common::usize EditorActions_BuildMenus(
    editor_actions_t *pActions,
    QMenuBar *pMenuBar,
    const editor_menu_item_t *pItems,
    common::usize nItems );

// Appends one menu's entries (for context menus). Returns entries skipped.
common::usize EditorActions_FillMenu(
    editor_actions_t *pActions,
    QMenu *pMenu,
    const char *const *ppCommands,
    common::usize nCommands );

// Appends buttons; a null entry is a separator. Returns entries skipped.
common::usize EditorActions_BuildToolBar(
    editor_actions_t *pActions,
    QToolBar *pToolBar,
    const char *const *ppCommands,
    common::usize nCommands );

// A vertically scrollable tool palette in a toolbar: buttons in a grid of
// nColumns, a null entry closes the row and draws a divider. The buttons
// share the commands' actions, so checked and enabled states follow them,
// and follow the toolbar's icon size. Spacing and dividers use the theme's
// ui.tool_strip metrics and refresh alongside icons. Its minimum height does not force the
// editor taller than the screen. Returns entries skipped.
common::usize EditorActions_BuildToolPalette(
    editor_actions_t *pActions,
    QToolBar *pToolBar,
    const char *const *ppCommands,
    common::usize nCommands,
    int nColumns );

// Re-reads every action's enabled and checked state from its command.
void EditorActions_RefreshStates( editor_actions_t *pActions );

// Re-tints every action icon after a theme change.
void EditorActions_RefreshIcons( editor_actions_t *pActions );

CYPHER_NODISCARD QKeySequence EditorKeyChord_ToKeySequence( const key_chord_t &chord );

// False when a key has no keymap equivalent.
CYPHER_NODISCARD bool EditorKeyChord_FromKeySequence( const QKeySequence &sequence, key_chord_t *pChordOut );

} // namespace cypher::editor::gui

#endif // CYPHER_EDITOR_GUI_ACTIONS_H
