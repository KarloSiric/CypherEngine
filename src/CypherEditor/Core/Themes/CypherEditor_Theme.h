//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditor_Theme.h
//  Purpose: Declares editor themes (`.cytheme`, cypher.theme V1): an open set
//           of named colour, font, and metric tokens, a registry of the tokens
//           the editor draws with, and resolution through a theme's base
//           chain down to each token's built-in default.
//  Details: Every colour, font, and size the editor draws with is a token that
//           its owning module registers with a default (ADR 0009). A theme
//           stores only the tokens it changes, and names the theme it builds
//           on, so a user theme stays valid when new tokens appear:
//
//             @cykv 1
//             @schema "cypher.theme" 1
//             {
//                 id = "midnight"
//                 name = "Midnight"
//                 base = "charcoal"
//                 colors = { "ui.background" = "#101418" "viewport.grid.minor" = "#1c2a36" }
//                 fonts = { "console" = { family = "JetBrains Mono" size = 10.0 weight = 400 } }
//                 metrics = { "ui.icon_size" = 20 }
//             }
//
//           Token IDs are dotted (area.part.detail) and stored verbatim as
//           member names, so a token ID never becomes nested objects. Themes
//           are edited through a settings-document store, which keeps members
//           this build does not know.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_CORE_THEME_H
#define CYPHER_EDITOR_CORE_THEME_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon/Tier1/CypherCommon_Vector.h"
#include "CypherCommon/Tier2/CypherCommon_SettingsDocument.h"

namespace cypher::editor
{

inline constexpr common::u32 EDITOR_THEME_SCHEMA_VERSION = 1u;
inline constexpr common::usize EDITOR_THEME_TOKEN_MAX_LENGTH = 96u;
inline constexpr common::usize EDITOR_THEME_ID_MAX_LENGTH = 64u;
inline constexpr common::usize EDITOR_THEME_MAX_BASE_DEPTH = 8u; // Base-chain bound; also stops cycles.

enum class theme_token_kind_t : common::u8 {
    COLOR = 0u, // 0xRRGGBBAA, written "#rrggbb" or "#rrggbbaa".
    FONT,       // Family, point size, weight.
    METRIC      // One number within [flMin, flMax]: sizes, widths, spacing.
};

struct theme_font_t {
    common::string_view_t family{}; // System family or .cyfont path.
    common::f64 flSize{ 10.0 };     // Points.
    common::u32 nWeight{ 400u };    // 1..1000.
};

// Static description of one token, registered by the module that draws it.
struct theme_token_t {
    const char *pId{ nullptr };             // "ui.background", "viewport.axis.x".
    theme_token_kind_t kind{ theme_token_kind_t::COLOR };
    common::u32 rgbaDefault{ 0x000000FFu }; // COLOR default.
    const char *pFontFamily{ "" };          // FONT default family.
    common::f64 flFontSize{ 10.0 };         // FONT default size.
    common::u32 nFontWeight{ 400u };        // FONT default weight.
    common::f64 flDefault{ 0.0 };           // METRIC default.
    common::f64 flMin{ 0.0 };               // METRIC inclusive minimum.
    common::f64 flMax{ 1.0e6 };             // METRIC inclusive maximum.
    const char *pLabel{ nullptr };          // Theme-editor label.
    const char *pGroup{ nullptr };          // Theme-editor group, e.g. "Viewport/Grid".
};

/*
================
Token Registry
================
*/
struct theme_registry_t {
    common::vector_t<const theme_token_t *> tokens{}; // Sorted by ID for lookup.
};

enum class theme_status_t : common::u8 {
    OK = 0u,
    INVALID_ARGUMENT,  // Null pointer, bad token ID, bad default, or bad value.
    DUPLICATE_TOKEN,   // A token ID is already registered; nothing was added.
    OUT_OF_MEMORY,
    STORE_FAILED       // The theme document could not be updated.
};

CYPHER_NODISCARD theme_status_t EditorThemeRegistry_Init(
    theme_registry_t *pRegistry,
    const common::allocator_t *pAllocator ) noexcept;

void EditorThemeRegistry_Shutdown( theme_registry_t *pRegistry ) noexcept;

// Registers a static table of tokens, all or nothing.
CYPHER_NODISCARD theme_status_t EditorThemeRegistry_Register(
    theme_registry_t *pRegistry,
    const theme_token_t *pTokens,
    common::usize nTokens ) noexcept;

CYPHER_NODISCARD const theme_token_t *EditorThemeRegistry_Find(
    const theme_registry_t *pRegistry,
    common::string_view_t id ) noexcept;

/*
================
Theme Documents And Chains
================
*/
CYPHER_NODISCARD common::settings_document_identity_t EditorTheme_Identity() noexcept;

// Identity members of a theme document; empty views when absent or invalid.
struct theme_header_t {
    common::string_view_t id{};   // Stable identifier other themes use as base.
    common::string_view_t name{}; // Display name.
    common::string_view_t base{}; // Base theme ID; empty for a root theme.
};

CYPHER_NODISCARD theme_header_t EditorTheme_Header( const common::key_value_t *pThemeRoot ) noexcept;

// Themes known by ID, for building base chains. Borrowed; not owned.
struct theme_library_t {
    common::vector_t<const common::key_value_t *> roots{};
};

CYPHER_NODISCARD theme_status_t EditorThemeLibrary_Init(
    theme_library_t *pLibrary,
    const common::allocator_t *pAllocator ) noexcept;

void EditorThemeLibrary_Shutdown( theme_library_t *pLibrary ) noexcept;

// Adds a theme root; a theme without a valid id cannot be a base.
CYPHER_NODISCARD theme_status_t EditorThemeLibrary_Add(
    theme_library_t *pLibrary,
    const common::key_value_t *pThemeRoot ) noexcept;

// Fills ppChainOut with the theme followed by its bases, most specific first.
// Stops at a missing base, a cycle, or EDITOR_THEME_MAX_BASE_DEPTH, and
// reports why through pbCompleteOut (false when a named base was not found
// or a cycle was cut).
CYPHER_NODISCARD common::usize EditorTheme_BuildChain(
    const theme_library_t *pLibrary,
    const common::key_value_t *pThemeRoot,
    const common::key_value_t **ppChainOut,
    common::usize nChainCapacity,
    common::bool_t *pbCompleteOut ) noexcept;

/*
================
Resolution

The first theme in the chain holding a valid value for the token wins;
otherwise the registered default applies. Invalid values are skipped and
counted so the theme editor can flag them.
================
*/
struct theme_resolution_t {
    common::usize iSource{ common::CY_INVALID_SIZE }; // Chain index; CY_INVALID_SIZE = default.
    common::usize nInvalidSkipped{ 0u };
};

CYPHER_NODISCARD common::u32 EditorTheme_ResolveColor(
    const common::key_value_t *const *ppChain,
    common::usize nChain,
    const theme_token_t &token,
    theme_resolution_t *pResolutionOut ) noexcept;

// The family view borrows either a theme document or the token table.
CYPHER_NODISCARD theme_font_t EditorTheme_ResolveFont(
    const common::key_value_t *const *ppChain,
    common::usize nChain,
    const theme_token_t &token,
    theme_resolution_t *pResolutionOut ) noexcept;

CYPHER_NODISCARD common::f64 EditorTheme_ResolveMetric(
    const common::key_value_t *const *ppChain,
    common::usize nChain,
    const theme_token_t &token,
    theme_resolution_t *pResolutionOut ) noexcept;

/*
================
Editing

Each setter keeps the theme sparse: storing the value its bases already
resolve to removes the theme's own entry instead.
================
*/
CYPHER_NODISCARD theme_status_t EditorTheme_SetHeader(
    common::settings_document_t *pTheme,
    common::string_view_t id,
    common::string_view_t name,
    common::string_view_t base ) noexcept;

CYPHER_NODISCARD theme_status_t EditorTheme_SetColor(
    common::settings_document_t *pTheme,
    const theme_token_t &token,
    common::u32 rgba,
    common::u32 inheritedRgba ) noexcept;

CYPHER_NODISCARD theme_status_t EditorTheme_SetFont(
    common::settings_document_t *pTheme,
    const theme_token_t &token,
    const theme_font_t &font,
    const theme_font_t &inherited ) noexcept;

CYPHER_NODISCARD theme_status_t EditorTheme_SetMetric(
    common::settings_document_t *pTheme,
    const theme_token_t &token,
    common::f64 flValue,
    common::f64 flInherited ) noexcept;

// Removes the theme's own value so the token inherits again.
CYPHER_NODISCARD theme_status_t EditorTheme_Reset(
    common::settings_document_t *pTheme,
    const theme_token_t &token ) noexcept;

/*
================
Audit

Lists entries the theme editor should flag: tokens no registered module
uses (kept, perhaps from a plugin that is not loaded) and values that are
invalid for their token.
================
*/
enum class theme_problem_code_t : common::u8 {
    UNKNOWN_TOKEN = 0u, // Not registered; preserved and ignored.
    WRONG_KIND,         // A colour under fonts, and so on.
    INVALID_VALUE       // Malformed colour, font, or out-of-range metric.
};

struct theme_problem_t {
    theme_problem_code_t code{ theme_problem_code_t::UNKNOWN_TOKEN };
    common::string_view_t token{}; // Borrows the theme document.
};

// Returns the number of problems found; at most nCapacity are written.
CYPHER_NODISCARD common::usize EditorTheme_Audit(
    const theme_registry_t *pRegistry,
    const common::key_value_t *pThemeRoot,
    theme_problem_t *pProblems,
    common::usize nCapacity ) noexcept;

CYPHER_NODISCARD const char *EditorTheme_StatusName( theme_status_t status ) noexcept;

} // namespace cypher::editor

#endif // CYPHER_EDITOR_CORE_THEME_H
