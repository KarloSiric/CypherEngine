//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_Style.h
//  Purpose: Declares the editor style: the framework's theme token catalogue
//           (CYTHEME.md section 4), its resolution into one style value set,
//           and applying it to Qt as a palette, fonts, a generated
//           stylesheet, and tinted icons.
//  Details: Themes are data (ADR 0008) and cover everything the editor
//           draws: interface chrome, status colours, viewport backgrounds,
//           grids, objects, tools and gizmos, the console, fonts, sizes, and
//           a few style choices. Four base colours (background, panel,
//           text, accent) are authored; the rest of the chrome is derived
//           from them by formulas registered with each token, so a new theme
//           needs four colours to look finished and can override anything
//           token by token. Workspaces register their own tokens beside
//           these; resolution covers every registered token.
//
//           The stylesheet template refers to colour tokens by ID
//           (`@ui.border@`), so a style change needs no code change.
//
//           Font tokens accept two reserved families: "system" (the
//           platform interface font) and "system-mono" (the platform fixed
//           font), so the built-in theme looks native everywhere.
//
//  History:
//  - Created by Karlo Siric on 2026-09-27
//  - Full V2 catalogue, derived tokens, and token placeholders on 2026-09-29
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GUI_STYLE_H
#define CYPHER_EDITOR_GUI_STYLE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherEditor_Theme.h"

#include <QColor>
#include <QFont>
#include <QHash>
#include <QIcon>
#include <QString>

class QApplication;

namespace cypher::editor::gui
{

inline constexpr const char *EDITOR_STYLE_FONT_SYSTEM = "system";
inline constexpr const char *EDITOR_STYLE_FONT_SYSTEM_MONO = "system-mono";
inline constexpr const char *EDITOR_STYLE_TEMPLATE_RESOURCE = ":/cypher/editor/style/editor.qss";

// Colours read on every repaint get a slot; everything else is looked up by
// token ID with EditorStyle_TokenColor.
enum editor_style_color_t : common::u32 {
    STYLE_COLOR_BACKGROUND = 0u, // ui.background
    STYLE_COLOR_PANEL,           // ui.panel
    STYLE_COLOR_TEXT,            // ui.text
    STYLE_COLOR_ACCENT,          // ui.accent
    STYLE_COLOR_ACTIVE_VIEW,     // viewport.active_border
    STYLE_COLOR_VIEWPORT_3D,     // viewport.background.3d
    STYLE_COLOR_VIEWPORT_2D,     // viewport.background.2d
    STYLE_COLOR_GRID_MINOR,      // viewport.grid.minor
    STYLE_COLOR_GRID_MAJOR,      // viewport.grid.major
    STYLE_COLOR_SELECTION,       // viewport.selection
    STYLE_COLOR_WIRE,            // viewport.wire.world
    STYLE_COLOR_AXIS_X,          // viewport.axis.x
    STYLE_COLOR_AXIS_Y,          // viewport.axis.y
    STYLE_COLOR_AXIS_Z,          // viewport.axis.z
    STYLE_COLOR_COUNT
};

// Resolved values of every registered token.
struct editor_style_t {
    common::u32 colors[STYLE_COLOR_COUNT]{}; // 0xRRGGBBAA; the fast slots.
    QHash<QString, QColor> tokenColors{};    // Every colour token by ID.
    QHash<QString, QFont> fonts{};           // Every font token by ID.
    QHash<QString, double> metrics{};        // Every metric token by ID.
    QHash<QString, QString> choices{};       // Every choice token by ID.
    QFont uiFont{};                          // "ui"
    QFont consoleFont{};                     // "console"
    common::f64 iconSize{ 28.0 };            // "ui.icon_size", logical pixels.
    common::f64 density{ 1.0 };              // "ui.density": 0.8, 1, or 1.25.
    common::bool_t bLight{ common::CY_FALSE }; // Light background: platform and icon variants lean light.
    common::usize nInvalidSkipped{ 0u };     // Invalid theme values passed over.
};

// The framework token catalogue, in the order the theme editor lists and a
// complete theme saves them; static lifetime.
CYPHER_NODISCARD const theme_token_t *EditorStyle_Tokens( common::usize *pnTokensOut ) noexcept;

CYPHER_NODISCARD theme_status_t EditorStyle_RegisterTokens( theme_registry_t *pRegistry ) noexcept;

// Resolves every token in pRegistry through a theme chain (most specific
// first); an empty chain yields the built-in defaults and formulas.
CYPHER_NODISCARD editor_style_t EditorStyle_Resolve(
    const theme_registry_t *pRegistry,
    const common::key_value_t *const *ppChain,
    common::usize nChain );

CYPHER_NODISCARD QColor EditorStyle_Color( const editor_style_t &style, editor_style_color_t color ) noexcept;

// A colour token by ID. An ID nobody registered comes back magenta, so a
// typo shows on screen instead of hiding.
CYPHER_NODISCARD QColor EditorStyle_TokenColor( const editor_style_t &style, const char *pId );
CYPHER_NODISCARD double EditorStyle_Metric( const editor_style_t &style, const char *pId, double fallback );
CYPHER_NODISCARD QString EditorStyle_Choice( const editor_style_t &style, const char *pId );
CYPHER_NODISCARD QFont EditorStyle_Font( const editor_style_t &style, const char *pId );

// Fills the stylesheet template: `@token.id@` becomes the colour token's
// value; font, control-padding, and docking-icon placeholders use the
// corresponding resolved theme values. Returns an empty string if a placeholder names nothing,
// which is a template/code mismatch caught by tests.
CYPHER_NODISCARD QString EditorStyle_BuildStyleSheet( const editor_style_t &style, const QString &templateText );

// Applies Fusion, palette, interface font, and the generated stylesheet.
// False when the stylesheet template resource cannot be read.
CYPHER_NODISCARD bool EditorStyle_Apply( QApplication *pApplication, const editor_style_t &style );

// A scalable icon by name. Generated geometric art keeps its silhouette and
// face shading in the theme's neutral/disabled colours. Enabled hover and
// checked states restore original illustration colours by ui.icon.hover_color_strength
// and ui.icon.checked_color_strength; the button border carries the selection accent.
// Run, build and diagnostic icons keep semantic colours while enabled. Outline
// SVGs remain neutral. Command actions opt every checkable tool or setting into
// checked illustration colours; decorative icons can stay neutral. Rendering is lazy, DPI-aware,
// and cached by theme/state.
// Original SVGs and material/texture previews are unchanged. Null when missing.
CYPHER_NODISCARD QIcon EditorStyle_Icon( const editor_style_t &style, const char *pName, bool bColorChecked = false );

} // namespace cypher::editor::gui

#endif // CYPHER_EDITOR_GUI_STYLE_H
