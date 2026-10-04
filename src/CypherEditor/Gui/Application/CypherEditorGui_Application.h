//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_Application.h
//  Purpose: Declares the editor application context shared by every Qt tool
//           built on the framework: the command registry, theme tokens and
//           theme chain, keymap chain, and the resolved style.
//  Details: One editor_gui_t per process, owned by the executable (Mason,
//           later the TileEditor and Picasso). It is plain data plus free
//           functions; windows borrow it and must be destroyed before it.
//
//           Built-in documents ship as Qt resources and are read through the
//           same decoders as user files, so the defaults are ordinary,
//           readable `.cytheme`, `.cykeymap`, and `.cylayout` text:
//             :/cypher/editor/themes/charcoal.cytheme
//             :/cypher/editor/keymaps/cypher_default.cykeymap
//             :/cypher/editor/layouts/<name>.cylayout
//
//  History:
//  - Created by Karlo Siric on 2026-09-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GUI_APPLICATION_H
#define CYPHER_EDITOR_GUI_APPLICATION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherEditor_Commands.h"
#include "CypherEditor_Keymap.h"
#include "CypherEditor_Layout.h"
#include "CypherEditor_SettingsRegistry.h"
#include "CypherEditor_Theme.h"
#include "CypherEditorGui_Log.h"
#include "CypherEditorGui_Style.h"

#include <QString>
#include <QStringList>

class QApplication;

namespace cypher::editor::gui
{

inline constexpr const char *EDITOR_BUILTIN_THEME_RESOURCE = ":/cypher/editor/themes/charcoal.cytheme";
// Bundled preset themes (Radiant Dark, Slate, ...); each names the built-in
// theme as its base. Init loads them.
inline constexpr const char *EDITOR_PRESET_THEME_FOLDER = ":/cypher/editor/themes/presets";
inline constexpr const char *EDITOR_BUILTIN_KEYMAP_RESOURCE = ":/cypher/editor/keymaps/cypher_default.cykeymap";
inline constexpr const char *EDITOR_KEYMAP_CONTEXT_GLOBAL = "global";
inline constexpr common::usize EDITOR_GUI_MAX_STYLE_LISTENERS = 32u;

// Told after the style changed (another theme, or a live edit in the theme
// editor): views repaint, icons re-tint.
using editor_style_listener_fn = void ( * )( void *pContext ) noexcept;

struct editor_style_listener_t {
    editor_style_listener_fn pfnChanged{ nullptr };
    void *pContext{ nullptr };
};

// Told after the keymap chain changed (another keymap, or a live edit in the
// keymap editor): shortcuts are re-applied, views re-read their gestures.
using editor_keymap_listener_fn = void ( * )( void *pContext ) noexcept;

struct editor_keymap_listener_t {
    editor_keymap_listener_fn pfnChanged{ nullptr };
    void *pContext{ nullptr };
};

struct editor_gui_t {
    editor_gui_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( editor_gui_t );
    ~editor_gui_t() noexcept;

    const common::allocator_t *pAllocator{ nullptr };
    editor_log_t log{};                       // Every log record since Init; the Console, Output, and Problems panels show it.
    command_registry_t commands{};
    settings_registry_t settings{};           // Framework catalogue registered; the application attaches scopes.
    theme_registry_t themeTokens{};
    theme_library_t themeLibrary{};
    common::settings_document_t builtinTheme{};
    common::settings_document_t builtinKeymap{};
    const common::key_value_t *themeChain[EDITOR_THEME_MAX_BASE_DEPTH]{};
    common::usize nThemeChain{ 0u };
    const common::key_value_t *keymapChain[EDITOR_KEYMAP_MAX_DEPTH]{};
    common::usize nKeymapChain{ 0u };
    editor_style_t style{};
    common::vector_t<common::settings_document_t *> themes{}; // Themes loaded from files; owned, newest last.
    editor_style_listener_t styleListeners[EDITOR_GUI_MAX_STYLE_LISTENERS]{};
    common::usize nStyleListeners{ 0u };
    common::vector_t<common::settings_document_t *> keymaps{};       // Keymaps loaded from files; owned, newest last.
    common::vector_t<const common::key_value_t *> keymapLibrary{};   // Lookup order: loaded newest first, built-in last.
    editor_keymap_listener_t keymapListeners[EDITOR_GUI_MAX_STYLE_LISTENERS]{};
    common::usize nKeymapListeners{ 0u };
    common::bool_t bInitialized{ common::CY_FALSE };
};

enum class editor_gui_status_t : common::u8 {
    OK = 0u,
    INVALID_ARGUMENT,
    OUT_OF_MEMORY,
    RESOURCE_MISSING,  // A built-in resource is not in the binary.
    RESOURCE_INVALID,  // A built-in resource failed its decoder: a build defect.
    STYLE_FAILED,
    IO_ERROR
};

// Sets up registries, loads the built-in theme and keymap, registers the
// framework theme tokens, and applies the style to the application.
CYPHER_NODISCARD editor_gui_status_t EditorGui_Init( editor_gui_t *pGui, QApplication *pApplication, const common::allocator_t *pAllocator );

void EditorGui_Shutdown( editor_gui_t *pGui ) noexcept;

// Registers the framework's Qt resources; idempotent. EditorGui_Init calls
// it; code that reads resources earlier (tests, splash screens) calls it too.
void EditorGui_RegisterResources();

// Reads a settings-family document from a Qt resource into an initialized
// store.
CYPHER_NODISCARD editor_gui_status_t EditorGui_LoadResource( const char *pResourcePath, common::settings_document_t *pStore );

// Reads and decodes a layout from a Qt resource into an initialized layout.
CYPHER_NODISCARD editor_gui_status_t EditorGui_LoadLayoutResource( const char *pResourcePath, layout_t *pLayout, const common::allocator_t *pAllocator );

// Re-resolves the style from the current theme chain and applies it.
CYPHER_NODISCARD editor_gui_status_t EditorGui_ApplyStyle( editor_gui_t *pGui, QApplication *pApplication );

// Adds a theme from its text; a theme with the same ID replaces the earlier
// one, so a user theme shadows a project or built-in theme of that ID.
// Returns INVALID_ARGUMENT for text that is not a theme with an ID.
CYPHER_NODISCARD editor_gui_status_t EditorGui_AddTheme( editor_gui_t *pGui, const QString &text, QString *pIdOut = nullptr );

// Adds every `.cytheme` file of a folder; returns how many loaded. Files that
// fail are logged and skipped.
common::usize EditorGui_LoadThemeFolder( editor_gui_t *pGui, const QString &folder );

// Unloads a theme added from text or a file; the built-in theme stays. When
// the active chain uses it, the built-in theme becomes active first.
// RESOURCE_MISSING when no added theme has that ID. A user theme that
// shadowed a preset of the same ID takes the preset with it until restart.
CYPHER_NODISCARD editor_gui_status_t EditorGui_RemoveTheme( editor_gui_t *pGui, QApplication *pApplication, common::string_view_t id );

// Makes the theme with this ID (with its base chain) the active one and
// applies it. RESOURCE_MISSING when no theme has that ID.
CYPHER_NODISCARD editor_gui_status_t EditorGui_SelectTheme( editor_gui_t *pGui, QApplication *pApplication, common::string_view_t id );

// Applies a theme that is not in the library - the theme editor's working
// copy - with its base chain, for live preview.
CYPHER_NODISCARD editor_gui_status_t EditorGui_PreviewTheme( editor_gui_t *pGui, QApplication *pApplication, const common::key_value_t *pThemeRoot );

// The active theme's ID, empty when none has one.
CYPHER_NODISCARD common::string_view_t EditorGui_ActiveThemeId( const editor_gui_t *pGui ) noexcept;

// Theme IDs and names the library knows, for pickers.
CYPHER_NODISCARD QStringList EditorGui_ThemeIds( const editor_gui_t *pGui );

CYPHER_NODISCARD common::bool_t EditorGui_AddStyleListener( editor_gui_t *pGui, editor_style_listener_fn pfnChanged, void *pContext ) noexcept;
void EditorGui_RemoveStyleListener( editor_gui_t *pGui, editor_style_listener_fn pfnChanged, void *pContext ) noexcept;

// Keymaps work like themes: a library of loaded keymaps (newest first, the
// built-in keymap last), one active chain, and a preview for the keymap
// editor's working copy. A keymap with the same ID replaces the earlier one.
CYPHER_NODISCARD editor_gui_status_t EditorGui_AddKeymap( editor_gui_t *pGui, const QString &text, QString *pIdOut = nullptr );
// Saves a complete keymap and activates it. Parsing, library allocation and
// chain validation finish before the atomic file commit; any failure leaves
// both the previously published keymap and the destination file unchanged.
CYPHER_NODISCARD editor_gui_status_t EditorGui_SaveKeymap( editor_gui_t *pGui, const QString &text, const QString &path, QString *pErrorOut = nullptr );
common::usize EditorGui_LoadKeymapFolder( editor_gui_t *pGui, const QString &folder );
CYPHER_NODISCARD editor_gui_status_t EditorGui_SelectKeymap( editor_gui_t *pGui, common::string_view_t id );
CYPHER_NODISCARD editor_gui_status_t EditorGui_PreviewKeymap( editor_gui_t *pGui, const common::key_value_t *pKeymapRoot );
CYPHER_NODISCARD common::string_view_t EditorGui_ActiveKeymapId( const editor_gui_t *pGui ) noexcept;
CYPHER_NODISCARD QStringList EditorGui_KeymapIds( const editor_gui_t *pGui );
CYPHER_NODISCARD common::bool_t EditorGui_AddKeymapListener( editor_gui_t *pGui, editor_keymap_listener_fn pfnChanged, void *pContext ) noexcept;
void EditorGui_RemoveKeymapListener( editor_gui_t *pGui, editor_keymap_listener_fn pfnChanged, void *pContext ) noexcept;

CYPHER_NODISCARD const char *EditorGui_StatusName( editor_gui_status_t status ) noexcept;

} // namespace cypher::editor::gui

#endif // CYPHER_EDITOR_GUI_APPLICATION_H
