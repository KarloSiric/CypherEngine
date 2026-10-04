//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditor_Layout.h
//  Purpose: Declares editor layouts (`.cylayout`, cypher.layout V2): a tree
//           of splits and tab groups plus floating windows, independent of
//           the docking library that shows it, and the rest of a window's
//           arrangement - main window placement, closed panels, toolbars,
//           the status bar, and the viewport grid (CYLAYOUT.md).
//  Details: The GUI's docking layer translates this tree to and from its
//           widgets, so saved layouts survive a change of docking library and
//           stay readable and hand-editable:
//
//             @cykv 1
//             @schema "cypher.layout" 1
//             {
//                 id = "four_view"
//                 name = "Four views"
//                 workspace = "map"
//                 root = {
//                     split = "horizontal"
//                     sizes = [ 0.2, 0.6, 0.2 ]
//                     children = [
//                         { tabs = [ "map.tools" ] },
//                         { split = "vertical" sizes = [ 0.5, 0.5 ] children = [
//                             { tabs = [ "map.viewport.3d" ] },
//                             { tabs = [ "map.viewport.top" ] }
//                         ] },
//                         { tabs = [ "map.outliner", "map.properties" ] current = 1 }
//                     ]
//                 }
//                 floating = [ { x = 80 y = 80 width = 640 height = 320 root = { tabs = [ "console" ] } } ]
//             }
//
//           Panel IDs are kept even when no panel with that ID is registered
//           (a plugin may not be loaded); the GUI skips them when showing the
//           layout. Split sizes are relative weights, normalised on decode.
//           The layout owns copies of its strings, so it outlives the
//           document it came from.
//
//           Structural damage in the tree or a floating window rejects a
//           layout (the editor keeps the one it has); an invalid V2 member
//           falls back to its default and is counted in nInvalidMembers.
//           Each panel's own state (`panels`) lives in the layout store and
//           is untouched by decoding and encoding, so panels a build does
//           not have keep theirs.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//  - V2 on 2026-09-29: window, hidden panels, toolbars, status bar, views
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_CORE_LAYOUT_H
#define CYPHER_EDITOR_CORE_LAYOUT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon/Tier1/CypherCommon_Vector.h"
#include "CypherCommon/Tier2/CypherCommon_SettingsDocument.h"

namespace cypher::editor
{

inline constexpr common::u32 EDITOR_LAYOUT_SCHEMA_VERSION = 2u;        // Written.
inline constexpr common::u32 EDITOR_LAYOUT_OLDEST_SCHEMA_VERSION = 1u; // Still read; V1 is V2 without the additions.
inline constexpr common::usize EDITOR_LAYOUT_MAX_TOOLBARS = 128u;
inline constexpr common::usize EDITOR_LAYOUT_MAX_TOOLBAR_ITEMS = 256u; // Per toolbar.
inline constexpr common::usize EDITOR_LAYOUT_MAX_HIDDEN = 256u;
inline constexpr common::usize EDITOR_LAYOUT_MAX_PANES = 16u;
inline constexpr common::usize EDITOR_LAYOUT_MAX_GRID_TRACKS = 4u;     // Columns or rows of the view grid.
inline constexpr common::usize EDITOR_LAYOUT_MAX_NODES = 256u;
inline constexpr common::usize EDITOR_LAYOUT_MAX_DEPTH = 16u;
inline constexpr common::usize EDITOR_LAYOUT_MAX_NODE_ITEMS = 64u; // Children of one split or tabs of one group.
inline constexpr common::usize EDITOR_LAYOUT_MAX_WINDOWS = 32u;
inline constexpr common::usize EDITOR_LAYOUT_PANEL_ID_MAX_LENGTH = 96u;

enum class layout_node_kind_t : common::u8 {
    SPLIT = 0u, // Children side by side (horizontal) or stacked (vertical).
    TABS        // One tab group of panels.
};

struct layout_node_t {
    layout_node_kind_t kind{ layout_node_kind_t::TABS };
    common::bool_t bVertical{ common::CY_FALSE }; // SPLIT orientation.
    common::u32 iFirstChild{ 0u };                // SPLIT: range in layout.children/sizes.
    common::u32 nChildren{ 0u };
    common::u32 iFirstPanel{ 0u };                // TABS: range in layout.panels.
    common::u32 nPanels{ 0u };
    common::u32 iCurrent{ 0u };                   // TABS: selected tab.
};

struct layout_text_t {
    common::u32 iOffset{ 0u }; // Into layout.text.
    common::u32 cchLength{ 0u };
};

struct layout_window_t {
    common::i32 x{ 0 };
    common::i32 y{ 0 };
    common::u32 width{ 0u };
    common::u32 height{ 0u };
    common::u32 iRoot{ 0u }; // Node index.
};

enum class layout_area_t : common::u8 { LEFT = 0u, RIGHT, TOP, BOTTOM, FLOATING };

// Main window placement.
struct layout_main_window_t {
    common::bool_t bPresent{ common::CY_FALSE };
    common::i32 x{ 0 };
    common::i32 y{ 0 };
    common::u32 width{ 0u };  // Logical pixels, at least 320.
    common::u32 height{ 0u }; // At least 240.
    common::bool_t bMaximized{ common::CY_FALSE };
    common::bool_t bFullscreen{ common::CY_FALSE };
    layout_text_t screen{};   // Display name; empty means the primary display.
};

// A closed panel and where it returns when reopened.
struct layout_hidden_t {
    layout_text_t panel{};
    layout_text_t beside{};                          // Panel it was docked with; may be empty.
    layout_area_t area{ layout_area_t::RIGHT };      // Fallback when `beside` is not showing.
    common::bool_t bTabbed{ common::CY_FALSE };      // Tabbed with `beside` rather than docked next to it.
};

struct layout_toolbar_t {
    layout_text_t id{};
    layout_area_t area{ layout_area_t::TOP };
    common::u32 row{ 0u };                           // 0 is nearest the edge.
    common::u32 order{ 0u };                         // Position in the row.
    common::bool_t bVisible{ common::CY_TRUE };
    common::f64 iconSize{ 0.0 };                     // 0: the theme's size.
    common::i32 x{ 0 };                              // When floating.
    common::i32 y{ 0 };
    common::bool_t bHasItems{ common::CY_FALSE };    // Custom buttons replace the defaults.
    common::u32 iFirstItem{ 0u };                    // Range in layout.toolbarItems ("-" is a separator).
    common::u32 nItems{ 0u };
};

// THREE_LEFT is one large pane on the left beside two; FOUR_TOP one large
// pane above three; and so on. Appended values keep older files reading.
enum class layout_arrangement_t : common::u8 {
    SINGLE = 0u, COLUMNS, ROWS, QUAD, THREE_LEFT, THREE_RIGHT, THREE_TOP, THREE_BOTTOM,
    THREE_COLUMNS, THREE_ROWS, FOUR_LEFT, FOUR_RIGHT, FOUR_TOP, FOUR_BOTTOM, FOUR_COLUMNS, FOUR_ROWS
};
enum class layout_view_kind_t : common::u8 { TOP = 0u, FRONT, SIDE, PERSPECTIVE, BOTTOM, BACK, LEFT, UV };
enum class layout_render_t : common::u8 { WIREFRAME = 0u, FLAT, TEXTURED, LIT, LIGHTING_ONLY, NORMALS };

enum layout_overlay_flags_t : common::u32 {
    LAYOUT_OVERLAY_ENTITIES = 1u << 0u,
    LAYOUT_OVERLAY_ENTITY_NAMES = 1u << 1u,
    LAYOUT_OVERLAY_HELPERS = 1u << 2u,
    LAYOUT_OVERLAY_IO = 1u << 3u,
    LAYOUT_OVERLAY_MODELS = 1u << 4u,
    LAYOUT_OVERLAY_DECALS = 1u << 5u,
    LAYOUT_OVERLAY_TERRAIN = 1u << 6u,
    LAYOUT_OVERLAY_PATCHES = 1u << 7u,
    LAYOUT_OVERLAY_CORDON = 1u << 8u,
    LAYOUT_OVERLAYS_ALL = ( 1u << 9u ) - 1u,
    LAYOUT_OVERLAYS_DEFAULT = LAYOUT_OVERLAYS_ALL & ~LAYOUT_OVERLAY_ENTITY_NAMES
};

struct layout_pane_t {
    layout_view_kind_t view{ layout_view_kind_t::TOP };
    layout_render_t render{ layout_render_t::WIREFRAME };
    common::bool_t bGrid{ common::CY_TRUE };
    common::u32 show{ LAYOUT_OVERLAYS_DEFAULT }; // layout_overlay_flags_t bits.
};

// The central viewport grid of workspaces that have one.
struct layout_views_t {
    common::bool_t bPresent{ common::CY_FALSE };
    layout_arrangement_t arrangement{ layout_arrangement_t::QUAD };
    common::f32 columns[EDITOR_LAYOUT_MAX_GRID_TRACKS]{};
    common::u32 nColumns{ 0u };                  // 0: equal.
    common::f32 rows[EDITOR_LAYOUT_MAX_GRID_TRACKS]{};
    common::u32 nRows{ 0u };
    common::i32 active{ 0 };
    common::i32 maximized{ -1 };
    layout_pane_t panes[EDITOR_LAYOUT_MAX_PANES]{};
    common::u32 nPanes{ 0u };                    // 0: per arrangement.
};

struct layout_t {
    common::vector_t<layout_node_t> nodes{};
    common::vector_t<common::u32> children{};  // Node indices, grouped per split.
    common::vector_t<common::f32> sizes{};     // Parallel to children; each split sums to 1.
    common::vector_t<layout_text_t> panels{};  // Panel IDs, grouped per tab group.
    common::vector_t<layout_window_t> windows{};
    common::vector_t<char> text{};             // Owned string storage.
    layout_text_t id{};
    layout_text_t name{};
    layout_text_t workspace{};                  // Workspace kind this layout is for; may be empty.
    common::u32 iRoot{ 0u };
    common::bool_t bHasRoot{ common::CY_FALSE };
    // V2.
    layout_text_t author{};
    layout_text_t description{};
    layout_main_window_t window{};
    common::vector_t<layout_hidden_t> hidden{};
    common::vector_t<layout_toolbar_t> toolbars{};
    common::vector_t<layout_text_t> toolbarItems{}; // Command IDs and "-", grouped per toolbar.
    common::bool_t bStatusBarVisible{ common::CY_TRUE };
    layout_views_t views{};
    common::usize nInvalidMembers{ 0u };             // V2 members that fell back to defaults on decode.
};

enum class layout_status_t : common::u8 {
    OK = 0u,
    INVALID_ARGUMENT,
    INVALID_HEADER,  // Not cypher.layout V1, or the root is not an object.
    INVALID_TREE,    // Missing root, unknown node shape, empty split or tab group,
                     // bad orientation, depth or node limit exceeded.
    INVALID_WINDOW,  // Floating window without a valid rectangle or root.
    OUT_OF_MEMORY,
    STORE_FAILED
};

CYPHER_NODISCARD layout_status_t EditorLayout_Init( layout_t *pLayout, const common::allocator_t *pAllocator ) noexcept;
void EditorLayout_Shutdown( layout_t *pLayout ) noexcept;
void EditorLayout_Clear( layout_t *pLayout ) noexcept;

CYPHER_NODISCARD common::string_view_t EditorLayout_Text( const layout_t *pLayout, layout_text_t text ) noexcept;

// Builders, used by the GUI when capturing its docks and by decoding.
// Children must already exist; they are referenced by node index.
CYPHER_NODISCARD layout_status_t EditorLayout_AddTabs(
    layout_t *pLayout,
    const common::string_view_t *pPanels,
    common::usize nPanels,
    common::u32 iCurrent,
    common::u32 *piNodeOut ) noexcept;

CYPHER_NODISCARD layout_status_t EditorLayout_AddSplit(
    layout_t *pLayout,
    common::bool_t bVertical,
    const common::u32 *pChildren,
    const common::f32 *pSizes,   // Relative weights; null means equal.
    common::usize nChildren,
    common::u32 *piNodeOut ) noexcept;

CYPHER_NODISCARD layout_status_t EditorLayout_AddWindow( layout_t *pLayout, const layout_window_t &window ) noexcept;

CYPHER_NODISCARD layout_status_t EditorLayout_SetHeader(
    layout_t *pLayout,
    common::string_view_t id,
    common::string_view_t name,
    common::string_view_t workspace ) noexcept;

// V2 builders.
CYPHER_NODISCARD layout_status_t EditorLayout_SetDetails(
    layout_t *pLayout,
    common::string_view_t author,
    common::string_view_t description ) noexcept;

CYPHER_NODISCARD layout_status_t EditorLayout_SetWindow(
    layout_t *pLayout,
    const layout_main_window_t &window,
    common::string_view_t screen ) noexcept;

CYPHER_NODISCARD layout_status_t EditorLayout_AddHidden(
    layout_t *pLayout,
    common::string_view_t panel,
    common::string_view_t beside,
    layout_area_t area,
    common::bool_t bTabbed ) noexcept;

// pItems may be null for a toolbar that keeps its default buttons.
CYPHER_NODISCARD layout_status_t EditorLayout_AddToolbar(
    layout_t *pLayout,
    common::string_view_t id,
    const layout_toolbar_t &placement,
    const common::string_view_t *pItems,
    common::usize nItems ) noexcept;

CYPHER_NODISCARD common::settings_document_identity_t EditorLayout_Identity() noexcept;

// Decodes a layout document. pLayout is replaced only on success.
CYPHER_NODISCARD layout_status_t EditorLayout_Decode(
    const common::key_value_document_t *pDocument,
    layout_t *pLayout ) noexcept;

// Writes the layout into a layout store, replacing its layout members and
// keeping every other member.
CYPHER_NODISCARD layout_status_t EditorLayout_Encode(
    const layout_t *pLayout,
    common::settings_document_t *pStore ) noexcept;

CYPHER_NODISCARD const char *EditorLayout_StatusName( layout_status_t status ) noexcept;

} // namespace cypher::editor

#endif // CYPHER_EDITOR_CORE_LAYOUT_H
