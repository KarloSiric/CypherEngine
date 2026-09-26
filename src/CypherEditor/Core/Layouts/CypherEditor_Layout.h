//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditor_Layout.h
//  Purpose: Declares editor dock layouts (`.cylayout`, cypher.layout V1): a
//           tree of splits and tab groups plus floating windows, independent
//           of the docking library that shows it.
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
//  History:
//  - Created by Karlo Siric on 2026-09-25
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

inline constexpr common::u32 EDITOR_LAYOUT_SCHEMA_VERSION = 1u;
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
