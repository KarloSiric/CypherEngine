//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditor_Layout.cpp
//  Purpose: Implements dock-layout building, decoding, and encoding.
//  Details: Decoding builds the tree bottom-up with the same builders the GUI
//           uses, into a scratch layout that replaces the caller's only on
//           success. Structural damage rejects the layout - showing half a
//           dock tree is worse than falling back to the default layout - but
//           unknown panel IDs and mismatched split sizes are tolerated.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditor_Layout.h"

#include "CypherCommon/Tier0/CypherCommon_Assert.h"
#include "CypherCommon/Tier2/CypherCommon_DataValidation.h"

#include <cmath>
#include <utility>

namespace cypher::editor
{

using namespace cypher::common;

namespace
{

template <usize nExtent>
CYPHER_NODISCARD constexpr string_view_t LayoutText( const char ( &text )[nExtent] ) noexcept
{
    static_assert( nExtent > 0u );
    return { text, nExtent - 1u };
}

CYPHER_NODISCARD bool_t StoreText( layout_t &layout, string_view_t text, layout_text_t &out ) noexcept
{
    out.iOffset = static_cast<u32>( Vector_Count( &layout.text ) );
    out.cchLength = static_cast<u32>( text.cchLength );
    for ( usize iChar = 0u; iChar < text.cchLength; ++iChar ) {
        if ( !Vector_PushBack( &layout.text, text.pData[iChar] ) ) { return CY_FALSE; }
    }
    return CY_TRUE;
}

// Panel IDs follow command-ID shape loosely: printable ASCII, no spaces.
CYPHER_NODISCARD bool_t IsPanelId( string_view_t id ) noexcept
{
    if ( id.cchLength == 0u || id.cchLength > EDITOR_LAYOUT_PANEL_ID_MAX_LENGTH ) { return CY_FALSE; }
    for ( usize iChar = 0u; iChar < id.cchLength; ++iChar ) {
        if ( id.pData[iChar] <= ' ' || id.pData[iChar] > '~' ) { return CY_FALSE; }
    }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t ReadNumber( const key_value_t *pValue, f64 &valueOut ) noexcept
{
    i64 nSigned = 0;
    u64 nUnsigned = 0u;
    if ( KeyValue_GetF64( pValue, &valueOut ) ) { return std::isfinite( valueOut ); }
    if ( KeyValue_GetI64( pValue, &nSigned ) ) { valueOut = static_cast<f64>( nSigned ); return CY_TRUE; }
    if ( KeyValue_GetU64( pValue, &nUnsigned ) ) { valueOut = static_cast<f64>( nUnsigned ); return CY_TRUE; }
    return CY_FALSE;
}

struct decoder_t {
    layout_t &layout;
    usize nNodes{ 0u };
};

CYPHER_NODISCARD layout_status_t DecodeNode(
    decoder_t &decoder,
    const key_value_t *pNode,
    usize nDepth,
    u32 &iNodeOut ) noexcept
{
    if ( KeyValue_Type( pNode ) != key_value_type_t::OBJECT || nDepth > EDITOR_LAYOUT_MAX_DEPTH ||
         ++decoder.nNodes > EDITOR_LAYOUT_MAX_NODES ) {
        return layout_status_t::INVALID_TREE;
    }
    const key_value_t *pTabs = KeyValue_Find( pNode, LayoutText( "tabs" ) );
    const key_value_t *pSplit = KeyValue_Find( pNode, LayoutText( "split" ) );
    if ( ( pTabs != nullptr ) == ( pSplit != nullptr ) ) {
        return layout_status_t::INVALID_TREE; // Exactly one shape per node.
    }

    if ( pTabs != nullptr ) {
        if ( KeyValue_Type( pTabs ) != key_value_type_t::ARRAY ) { return layout_status_t::INVALID_TREE; }
        string_view_t panels[EDITOR_LAYOUT_MAX_NODE_ITEMS]{};
        usize nPanels = 0u;
        for ( usize iPanel = 0u; iPanel < KeyValue_ChildCount( pTabs ) && nPanels < EDITOR_LAYOUT_MAX_NODE_ITEMS; ++iPanel ) {
            string_view_t id{};
            // A malformed entry is dropped; the rest of the group stays.
            if ( KeyValue_GetString( KeyValue_ChildAt( pTabs, iPanel ), &id ) && IsPanelId( id ) ) {
                panels[nPanels++] = id;
            }
        }
        if ( nPanels == 0u ) { return layout_status_t::INVALID_TREE; }
        f64 flCurrent = 0.0;
        const key_value_t *pCurrent = KeyValue_Find( pNode, LayoutText( "current" ) );
        u32 iCurrent = 0u;
        if ( pCurrent != nullptr && ReadNumber( pCurrent, flCurrent ) && flCurrent >= 0.0 &&
             flCurrent < static_cast<f64>( nPanels ) && flCurrent == std::floor( flCurrent ) ) {
            iCurrent = static_cast<u32>( flCurrent );
        }
        return EditorLayout_AddTabs( &decoder.layout, panels, nPanels, iCurrent, &iNodeOut );
    }

    string_view_t orientation{};
    const key_value_t *pChildren = KeyValue_Find( pNode, LayoutText( "children" ) );
    if ( !KeyValue_GetString( pSplit, &orientation ) ||
         !( StringView_Equals( orientation, LayoutText( "horizontal" ) ) ||
            StringView_Equals( orientation, LayoutText( "vertical" ) ) ) ||
         KeyValue_Type( pChildren ) != key_value_type_t::ARRAY || KeyValue_ChildCount( pChildren ) == 0u ||
         KeyValue_ChildCount( pChildren ) > EDITOR_LAYOUT_MAX_NODE_ITEMS ) {
        return layout_status_t::INVALID_TREE;
    }
    const usize nChildren = KeyValue_ChildCount( pChildren );
    u32 children[EDITOR_LAYOUT_MAX_NODE_ITEMS]{};
    for ( usize iChild = 0u; iChild < nChildren; ++iChild ) {
        const layout_status_t status = DecodeNode( decoder, KeyValue_ChildAt( pChildren, iChild ), nDepth + 1u, children[iChild] );
        if ( status != layout_status_t::OK ) { return status; }
    }
    // Sizes that do not match the children, or are not positive, fall back
    // to equal shares rather than rejecting the layout.
    f32 sizes[EDITOR_LAYOUT_MAX_NODE_ITEMS]{};
    const key_value_t *pSizes = KeyValue_Find( pNode, LayoutText( "sizes" ) );
    bool_t bSizesValid = KeyValue_Type( pSizes ) == key_value_type_t::ARRAY && KeyValue_ChildCount( pSizes ) == nChildren;
    for ( usize iChild = 0u; bSizesValid && iChild < nChildren; ++iChild ) {
        f64 flSize = 0.0;
        bSizesValid = ReadNumber( KeyValue_ChildAt( pSizes, iChild ), flSize ) && flSize > 0.0;
        sizes[iChild] = static_cast<f32>( flSize );
    }
    return EditorLayout_AddSplit( &decoder.layout, StringView_Equals( orientation, LayoutText( "vertical" ) ),
                                  children, bSizesValid ? sizes : nullptr, nChildren, &iNodeOut );
}

CYPHER_NODISCARD bool_t EncodeNode(
    const layout_t &layout,
    u32 iNode,
    key_value_document_t *pDocument,
    key_value_t *pOut ) noexcept
{
    CY_ASSERT_MSG( iNode < Vector_Count( &layout.nodes ), "Builders only reference existing nodes" );
    const layout_node_t &node = layout.nodes.pData[iNode];
    CY_ASSERT( node.kind != layout_node_kind_t::SPLIT ||
               static_cast<usize>( node.iFirstChild ) + node.nChildren <= Vector_Count( &layout.children ) );
    CY_ASSERT( node.kind != layout_node_kind_t::TABS ||
               static_cast<usize>( node.iFirstPanel ) + node.nPanels <= Vector_Count( &layout.panels ) );
    if ( !KeyValue_SetContainerType( pDocument, pOut, key_value_type_t::OBJECT ) ) { return CY_FALSE; }
    if ( node.kind == layout_node_kind_t::TABS ) {
        key_value_t *pTabs = KeyValue_ObjectInsert( pDocument, pOut, LayoutText( "tabs" ), key_value_type_t::ARRAY );
        if ( pTabs == nullptr ) { return CY_FALSE; }
        for ( u32 iPanel = 0u; iPanel < node.nPanels; ++iPanel ) {
            key_value_t *pPanel = KeyValue_ArrayAppend( pDocument, pTabs, key_value_type_t::NULL_VALUE );
            if ( pPanel == nullptr ||
                 !KeyValue_SetString( pDocument, pPanel, EditorLayout_Text( &layout, layout.panels.pData[node.iFirstPanel + iPanel] ) ) ) {
                return CY_FALSE;
            }
        }
        if ( node.iCurrent != 0u ) {
            key_value_t *pCurrent = KeyValue_ObjectInsert( pDocument, pOut, LayoutText( "current" ), key_value_type_t::NULL_VALUE );
            if ( pCurrent == nullptr || !KeyValue_SetI64( pDocument, pCurrent, node.iCurrent ) ) { return CY_FALSE; }
        }
        return CY_TRUE;
    }
    key_value_t *pSplit = KeyValue_ObjectInsert( pDocument, pOut, LayoutText( "split" ), key_value_type_t::NULL_VALUE );
    key_value_t *pSizes = KeyValue_ObjectInsert( pDocument, pOut, LayoutText( "sizes" ), key_value_type_t::ARRAY );
    key_value_t *pChildren = KeyValue_ObjectInsert( pDocument, pOut, LayoutText( "children" ), key_value_type_t::ARRAY );
    if ( pSplit == nullptr || pSizes == nullptr || pChildren == nullptr ||
         !KeyValue_SetString( pDocument, pSplit, node.bVertical ? LayoutText( "vertical" ) : LayoutText( "horizontal" ) ) ) {
        return CY_FALSE;
    }
    for ( u32 iChild = 0u; iChild < node.nChildren; ++iChild ) {
        key_value_t *pSize = KeyValue_ArrayAppend( pDocument, pSizes, key_value_type_t::NULL_VALUE );
        key_value_t *pChild = KeyValue_ArrayAppend( pDocument, pChildren, key_value_type_t::NULL_VALUE );
        if ( pSize == nullptr || pChild == nullptr ||
             !KeyValue_SetF64( pDocument, pSize, layout.sizes.pData[node.iFirstChild + iChild] ) ||
             !EncodeNode( layout, layout.children.pData[node.iFirstChild + iChild], pDocument, pChild ) ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

// ---------------------------------------------------------------------------
// V2 members
// ---------------------------------------------------------------------------

constexpr const char *kAreaNames[]{ "left", "right", "top", "bottom", "floating" };
constexpr const char *kArrangementNames[]{ "single",        "columns",    "rows",      "quad",       "three_left", "three_right",
                                           "three_top",     "three_bottom", "three_columns", "three_rows", "four_left",  "four_right",
                                           "four_top",      "four_bottom", "four_columns", "four_rows" };
constexpr const char *kViewNames[]{ "top", "front", "side", "perspective", "bottom", "back", "left", "uv" };
constexpr const char *kRenderNames[]{ "wireframe", "flat", "textured", "lit", "lighting_only", "normals" };
constexpr const char *kOverlayNames[]{ "entities", "entity_names", "helpers", "io", "models", "decals", "terrain", "patches", "cordon" };

// Index of a name in a table, or -1.
template <usize nNames>
CYPHER_NODISCARD i32 FindName( const key_value_t *pValue, const char *const ( &names )[nNames] ) noexcept
{
    string_view_t text{};
    if ( !KeyValue_GetString( pValue, &text ) ) { return -1; }
    for ( usize i = 0u; i < nNames; ++i ) {
        if ( StringView_Equals( text, StringView_FromCString( names[i] ) ) ) { return static_cast<i32>( i ); }
    }
    return -1;
}

// Reads an optional member with a fallback; a present but invalid value
// counts as a problem.
struct v2_reader_t {
    layout_t &layout;

    void Invalid() noexcept { ++layout.nInvalidMembers; }

    CYPHER_NODISCARD bool_t Bool( const key_value_t *pObject, const char *pKey, bool_t fallback ) noexcept
    {
        const key_value_t *pValue = KeyValue_Find( pObject, StringView_FromCString( pKey ) );
        bool_t value = fallback;
        if ( pValue != nullptr && !KeyValue_GetBool( pValue, &value ) ) { Invalid(); return fallback; }
        return value;
    }

    CYPHER_NODISCARD f64 Number( const key_value_t *pObject, const char *pKey, f64 fallback, f64 min, f64 max ) noexcept
    {
        const key_value_t *pValue = KeyValue_Find( pObject, StringView_FromCString( pKey ) );
        f64 value = fallback;
        if ( pValue == nullptr ) { return fallback; }
        if ( !ReadNumber( pValue, value ) || value < min || value > max ) { Invalid(); return fallback; }
        return value;
    }

    template <usize nNames>
    CYPHER_NODISCARD i32 Name( const key_value_t *pObject, const char *pKey, const char *const ( &names )[nNames], i32 fallback ) noexcept
    {
        const key_value_t *pValue = KeyValue_Find( pObject, StringView_FromCString( pKey ) );
        if ( pValue == nullptr ) { return fallback; }
        const i32 index = FindName( pValue, names );
        if ( index < 0 ) { Invalid(); return fallback; }
        return index;
    }

    // Stores an optional string member; false only when out of memory.
    CYPHER_NODISCARD bool_t Text( const key_value_t *pObject, const char *pKey, usize cchMax, layout_text_t &out ) noexcept
    {
        const key_value_t *pValue = KeyValue_Find( pObject, StringView_FromCString( pKey ) );
        string_view_t text{};
        if ( pValue == nullptr ) { return CY_TRUE; }
        if ( !KeyValue_GetString( pValue, &text ) || text.cchLength > cchMax ) { Invalid(); return CY_TRUE; }
        return StoreText( layout, text, out );
    }

    // Relative weights; anything but a list of positive numbers is ignored.
    void Weights( const key_value_t *pObject, const char *pKey, f32 ( &weights )[EDITOR_LAYOUT_MAX_GRID_TRACKS], u32 &nOut ) noexcept
    {
        const key_value_t *pValue = KeyValue_Find( pObject, StringView_FromCString( pKey ) );
        nOut = 0u;
        if ( pValue == nullptr ) { return; }
        const usize nChildren = KeyValue_ChildCount( pValue );
        if ( KeyValue_Type( pValue ) != key_value_type_t::ARRAY || nChildren == 0u || nChildren > EDITOR_LAYOUT_MAX_GRID_TRACKS ) { Invalid(); return; }
        for ( usize i = 0u; i < nChildren; ++i ) {
            f64 value = 0.0;
            if ( !ReadNumber( KeyValue_ChildAt( pValue, i ), value ) || !( value > 0.0 ) ) { Invalid(); nOut = 0u; return; }
            weights[i] = static_cast<f32>( value );
        }
        nOut = static_cast<u32>( nChildren );
    }
};

CYPHER_NODISCARD layout_status_t DecodeV2( layout_t &layout, const key_value_t *pRoot ) noexcept
{
    v2_reader_t read{ layout };
    if ( !read.Text( pRoot, "author", 128u, layout.author ) || !read.Text( pRoot, "description", 1024u, layout.description ) ) {
        return layout_status_t::OUT_OF_MEMORY;
    }

    const key_value_t *pWindow = KeyValue_Find( pRoot, LayoutText( "window" ) );
    if ( KeyValue_Type( pWindow ) == key_value_type_t::OBJECT ) {
        layout_main_window_t &window = layout.window;
        window.bPresent = CY_TRUE;
        window.x = static_cast<i32>( read.Number( pWindow, "x", 0.0, -1.0e6, 1.0e6 ) );
        window.y = static_cast<i32>( read.Number( pWindow, "y", 0.0, -1.0e6, 1.0e6 ) );
        window.width = static_cast<u32>( read.Number( pWindow, "width", 1280.0, 320.0, 1.0e5 ) );
        window.height = static_cast<u32>( read.Number( pWindow, "height", 800.0, 240.0, 1.0e5 ) );
        window.bMaximized = read.Bool( pWindow, "maximized", CY_FALSE );
        window.bFullscreen = read.Bool( pWindow, "fullscreen", CY_FALSE );
        if ( !read.Text( pWindow, "screen", 128u, window.screen ) ) { return layout_status_t::OUT_OF_MEMORY; }
    } else if ( pWindow != nullptr ) {
        read.Invalid();
    }

    const key_value_t *pHidden = KeyValue_Find( pRoot, LayoutText( "hidden" ) );
    for ( usize i = 0u; KeyValue_Type( pHidden ) == key_value_type_t::ARRAY && i < KeyValue_ChildCount( pHidden ); ++i ) {
        const key_value_t *pEntry = KeyValue_ChildAt( pHidden, i );
        string_view_t panel{};
        if ( KeyValue_Type( pEntry ) != key_value_type_t::OBJECT || !KeyValue_GetString( KeyValue_Find( pEntry, LayoutText( "panel" ) ), &panel ) ||
             !IsPanelId( panel ) || Vector_Count( &layout.hidden ) >= EDITOR_LAYOUT_MAX_HIDDEN ) {
            read.Invalid();
            continue;
        }
        layout_hidden_t hidden{};
        if ( !StoreText( layout, panel, hidden.panel ) || !read.Text( pEntry, "beside", EDITOR_LAYOUT_PANEL_ID_MAX_LENGTH, hidden.beside ) ) {
            return layout_status_t::OUT_OF_MEMORY;
        }
        hidden.area = static_cast<layout_area_t>( read.Name( pEntry, "area", kAreaNames, static_cast<i32>( layout_area_t::RIGHT ) ) );
        hidden.bTabbed = read.Bool( pEntry, "tabbed", CY_FALSE );
        if ( !Vector_PushBack( &layout.hidden, hidden ) ) { return layout_status_t::OUT_OF_MEMORY; }
    }
    if ( pHidden != nullptr && KeyValue_Type( pHidden ) != key_value_type_t::ARRAY ) { read.Invalid(); }

    const key_value_t *pToolbars = KeyValue_Find( pRoot, LayoutText( "toolbars" ) );
    for ( usize i = 0u; KeyValue_Type( pToolbars ) == key_value_type_t::ARRAY && i < KeyValue_ChildCount( pToolbars ); ++i ) {
        const key_value_t *pEntry = KeyValue_ChildAt( pToolbars, i );
        string_view_t id{};
        if ( KeyValue_Type( pEntry ) != key_value_type_t::OBJECT || !KeyValue_GetString( KeyValue_Find( pEntry, LayoutText( "id" ) ), &id ) ||
             !IsPanelId( id ) || Vector_Count( &layout.toolbars ) >= EDITOR_LAYOUT_MAX_TOOLBARS ) {
            read.Invalid();
            continue;
        }
        layout_toolbar_t toolbar{};
        toolbar.area = static_cast<layout_area_t>( read.Name( pEntry, "area", kAreaNames, static_cast<i32>( layout_area_t::TOP ) ) );
        toolbar.row = static_cast<u32>( read.Number( pEntry, "row", 0.0, 0.0, 64.0 ) );
        toolbar.order = static_cast<u32>( read.Number( pEntry, "order", 0.0, 0.0, 1024.0 ) );
        toolbar.bVisible = read.Bool( pEntry, "visible", CY_TRUE );
        toolbar.iconSize = read.Number( pEntry, "icon_size", 0.0, 8.0, 128.0 );
        toolbar.x = static_cast<i32>( read.Number( pEntry, "x", 0.0, -1.0e6, 1.0e6 ) );
        toolbar.y = static_cast<i32>( read.Number( pEntry, "y", 0.0, -1.0e6, 1.0e6 ) );
        const key_value_t *pItems = KeyValue_Find( pEntry, LayoutText( "items" ) );
        if ( KeyValue_Type( pItems ) == key_value_type_t::ARRAY ) {
            toolbar.bHasItems = CY_TRUE;
            toolbar.iFirstItem = static_cast<u32>( Vector_Count( &layout.toolbarItems ) );
            for ( usize iItem = 0u; iItem < KeyValue_ChildCount( pItems ) && toolbar.nItems < EDITOR_LAYOUT_MAX_TOOLBAR_ITEMS; ++iItem ) {
                string_view_t item{};
                layout_text_t stored{};
                if ( !KeyValue_GetString( KeyValue_ChildAt( pItems, iItem ), &item ) || !IsPanelId( item ) ) { read.Invalid(); continue; }
                if ( !StoreText( layout, item, stored ) || !Vector_PushBack( &layout.toolbarItems, stored ) ) { return layout_status_t::OUT_OF_MEMORY; }
                ++toolbar.nItems;
            }
        } else if ( pItems != nullptr ) {
            read.Invalid();
        }
        if ( !StoreText( layout, id, toolbar.id ) || !Vector_PushBack( &layout.toolbars, toolbar ) ) { return layout_status_t::OUT_OF_MEMORY; }
    }
    if ( pToolbars != nullptr && KeyValue_Type( pToolbars ) != key_value_type_t::ARRAY ) { read.Invalid(); }

    const key_value_t *pStatus = KeyValue_Find( pRoot, LayoutText( "status_bar" ) );
    if ( KeyValue_Type( pStatus ) == key_value_type_t::OBJECT ) {
        layout.bStatusBarVisible = read.Bool( pStatus, "visible", CY_TRUE );
    } else if ( pStatus != nullptr ) {
        read.Invalid();
    }

    const key_value_t *pViews = KeyValue_Find( pRoot, LayoutText( "views" ) );
    if ( KeyValue_Type( pViews ) == key_value_type_t::OBJECT ) {
        layout_views_t &views = layout.views;
        views.bPresent = CY_TRUE;
        views.arrangement = static_cast<layout_arrangement_t>( read.Name( pViews, "arrangement", kArrangementNames, static_cast<i32>( layout_arrangement_t::QUAD ) ) );
        read.Weights( pViews, "columns", views.columns, views.nColumns );
        read.Weights( pViews, "rows", views.rows, views.nRows );
        views.active = static_cast<i32>( read.Number( pViews, "active", 0.0, 0.0, static_cast<f64>( EDITOR_LAYOUT_MAX_PANES - 1u ) ) );
        views.maximized = static_cast<i32>( read.Number( pViews, "maximized", -1.0, -1.0, static_cast<f64>( EDITOR_LAYOUT_MAX_PANES - 1u ) ) );
        const key_value_t *pPanes = KeyValue_Find( pViews, LayoutText( "panes" ) );
        for ( usize i = 0u; KeyValue_Type( pPanes ) == key_value_type_t::ARRAY && i < KeyValue_ChildCount( pPanes ); ++i ) {
            const key_value_t *pPane = KeyValue_ChildAt( pPanes, i );
            if ( KeyValue_Type( pPane ) != key_value_type_t::OBJECT || views.nPanes >= EDITOR_LAYOUT_MAX_PANES ) { read.Invalid(); continue; }
            layout_pane_t pane{};
            pane.view = static_cast<layout_view_kind_t>( read.Name( pPane, "view", kViewNames, static_cast<i32>( layout_view_kind_t::TOP ) ) );
            const bool_t b3d = pane.view == layout_view_kind_t::PERSPECTIVE;
            pane.render = static_cast<layout_render_t>(
                read.Name( pPane, "render", kRenderNames, static_cast<i32>( b3d ? layout_render_t::TEXTURED : layout_render_t::WIREFRAME ) ) );
            pane.bGrid = read.Bool( pPane, "grid", !b3d );
            const key_value_t *pShow = KeyValue_Find( pPane, LayoutText( "show" ) );
            if ( KeyValue_Type( pShow ) == key_value_type_t::ARRAY ) {
                pane.show = 0u;
                for ( usize iShow = 0u; iShow < KeyValue_ChildCount( pShow ); ++iShow ) {
                    const i32 index = FindName( KeyValue_ChildAt( pShow, iShow ), kOverlayNames );
                    if ( index < 0 ) { read.Invalid(); continue; }
                    pane.show |= 1u << static_cast<u32>( index );
                }
            } else if ( pShow != nullptr ) {
                read.Invalid();
            }
            views.panes[views.nPanes++] = pane;
        }
    } else if ( pViews != nullptr ) {
        read.Invalid();
    }
    return layout_status_t::OK;
}

CYPHER_NODISCARD key_value_t *Insert( key_value_document_t *pDocument, key_value_t *pObject, const char *pKey, key_value_type_t type ) noexcept
{
    return KeyValue_ObjectInsert( pDocument, pObject, StringView_FromCString( pKey ), type );
}

CYPHER_NODISCARD bool_t InsertString( key_value_document_t *pDocument, key_value_t *pObject, const char *pKey, string_view_t value ) noexcept
{
    key_value_t *pValue = Insert( pDocument, pObject, pKey, key_value_type_t::NULL_VALUE );
    return pValue != nullptr && KeyValue_SetString( pDocument, pValue, value );
}

CYPHER_NODISCARD bool_t InsertI64( key_value_document_t *pDocument, key_value_t *pObject, const char *pKey, i64 value ) noexcept
{
    key_value_t *pValue = Insert( pDocument, pObject, pKey, key_value_type_t::NULL_VALUE );
    return pValue != nullptr && KeyValue_SetI64( pDocument, pValue, value );
}

CYPHER_NODISCARD bool_t InsertBool( key_value_document_t *pDocument, key_value_t *pObject, const char *pKey, bool_t value ) noexcept
{
    key_value_t *pValue = Insert( pDocument, pObject, pKey, key_value_type_t::NULL_VALUE );
    return pValue != nullptr && KeyValue_SetBool( pDocument, pValue, value );
}

CYPHER_NODISCARD bool_t InsertWeights( key_value_document_t *pDocument, key_value_t *pObject, const char *pKey, const f32 *pWeights, u32 nWeights ) noexcept
{
    key_value_t *pArray = Insert( pDocument, pObject, pKey, key_value_type_t::ARRAY );
    if ( pArray == nullptr ) { return CY_FALSE; }
    for ( u32 i = 0u; i < nWeights; ++i ) {
        key_value_t *pValue = KeyValue_ArrayAppend( pDocument, pArray, key_value_type_t::NULL_VALUE );
        if ( pValue == nullptr || !KeyValue_SetF64( pDocument, pValue, pWeights[i] ) ) { return CY_FALSE; }
    }
    return CY_TRUE;
}

// Replaces one top-level member: removed, then rebuilt when bWrite.
CYPHER_NODISCARD key_value_t *ResetMember( settings_document_t *pStore, const char *pKey, key_value_type_t type, bool_t bWrite, bool_t &bOk ) noexcept
{
    settings_path_t path{};
    bOk = SettingsPath_Append( &path, StringView_FromCString( pKey ) ) && SettingsDocument_Remove( pStore, path ) == settings_document_status_t::OK;
    if ( !bOk || !bWrite ) { return nullptr; }
    key_value_t *pNode = nullptr;
    bOk = SettingsDocument_Ensure( pStore, path, &pNode ) == settings_document_status_t::OK &&
          KeyValue_SetContainerType( pStore->pDocument, pNode, type );
    return bOk ? pNode : nullptr;
}

CYPHER_NODISCARD bool_t EncodeV2( const layout_t &layout, settings_document_t *pStore ) noexcept
{
    key_value_document_t *pDocument = pStore->pDocument;
    bool_t bOk = CY_TRUE;
    const auto text = [&layout]( layout_text_t t ) noexcept { return EditorLayout_Text( &layout, t ); };

    for ( const auto &field : { std::pair<const char *, layout_text_t>{ "author", layout.author }, std::pair<const char *, layout_text_t>{ "description", layout.description } } ) {
        settings_path_t path{};
        if ( !SettingsPath_Append( &path, StringView_FromCString( field.first ) ) ) { return CY_FALSE; }
        if ( field.second.cchLength == 0u ) {
            if ( SettingsDocument_Remove( pStore, path ) != settings_document_status_t::OK ) { return CY_FALSE; }
            continue;
        }
        key_value_t *pNode = nullptr;
        if ( SettingsDocument_Ensure( pStore, path, &pNode ) != settings_document_status_t::OK || !KeyValue_SetString( pDocument, pNode, text( field.second ) ) ) {
            return CY_FALSE;
        }
    }

    key_value_t *pWindow = ResetMember( pStore, "window", key_value_type_t::OBJECT, layout.window.bPresent, bOk );
    if ( !bOk ) { return CY_FALSE; }
    if ( pWindow != nullptr ) {
        const layout_main_window_t &w = layout.window;
        if ( !InsertI64( pDocument, pWindow, "x", w.x ) || !InsertI64( pDocument, pWindow, "y", w.y ) || !InsertI64( pDocument, pWindow, "width", w.width ) ||
             !InsertI64( pDocument, pWindow, "height", w.height ) || !InsertBool( pDocument, pWindow, "maximized", w.bMaximized ) ||
             !InsertBool( pDocument, pWindow, "fullscreen", w.bFullscreen ) ||
             ( w.screen.cchLength != 0u && !InsertString( pDocument, pWindow, "screen", text( w.screen ) ) ) ) {
            return CY_FALSE;
        }
    }

    key_value_t *pHidden = ResetMember( pStore, "hidden", key_value_type_t::ARRAY, Vector_Count( &layout.hidden ) != 0u, bOk );
    if ( !bOk ) { return CY_FALSE; }
    for ( usize i = 0u; pHidden != nullptr && i < Vector_Count( &layout.hidden ); ++i ) {
        const layout_hidden_t &hidden = layout.hidden.pData[i];
        key_value_t *pEntry = KeyValue_ArrayAppend( pDocument, pHidden, key_value_type_t::OBJECT );
        if ( pEntry == nullptr || !InsertString( pDocument, pEntry, "panel", text( hidden.panel ) ) ||
             ( hidden.beside.cchLength != 0u && !InsertString( pDocument, pEntry, "beside", text( hidden.beside ) ) ) ||
             !InsertString( pDocument, pEntry, "area", StringView_FromCString( kAreaNames[static_cast<usize>( hidden.area )] ) ) ||
             ( hidden.bTabbed && !InsertBool( pDocument, pEntry, "tabbed", CY_TRUE ) ) ) {
            return CY_FALSE;
        }
    }

    key_value_t *pToolbars = ResetMember( pStore, "toolbars", key_value_type_t::ARRAY, Vector_Count( &layout.toolbars ) != 0u, bOk );
    if ( !bOk ) { return CY_FALSE; }
    for ( usize i = 0u; pToolbars != nullptr && i < Vector_Count( &layout.toolbars ); ++i ) {
        const layout_toolbar_t &toolbar = layout.toolbars.pData[i];
        key_value_t *pEntry = KeyValue_ArrayAppend( pDocument, pToolbars, key_value_type_t::OBJECT );
        if ( pEntry == nullptr || !InsertString( pDocument, pEntry, "id", text( toolbar.id ) ) ||
             !InsertString( pDocument, pEntry, "area", StringView_FromCString( kAreaNames[static_cast<usize>( toolbar.area )] ) ) ||
             !InsertI64( pDocument, pEntry, "row", toolbar.row ) || !InsertI64( pDocument, pEntry, "order", toolbar.order ) ||
             ( !toolbar.bVisible && !InsertBool( pDocument, pEntry, "visible", CY_FALSE ) ) ) {
            return CY_FALSE;
        }
        if ( toolbar.iconSize > 0.0 ) {
            key_value_t *pSize = Insert( pDocument, pEntry, "icon_size", key_value_type_t::NULL_VALUE );
            if ( pSize == nullptr || !KeyValue_SetF64( pDocument, pSize, toolbar.iconSize ) ) { return CY_FALSE; }
        }
        if ( toolbar.area == layout_area_t::FLOATING && ( !InsertI64( pDocument, pEntry, "x", toolbar.x ) || !InsertI64( pDocument, pEntry, "y", toolbar.y ) ) ) {
            return CY_FALSE;
        }
        if ( toolbar.bHasItems ) {
            key_value_t *pItems = Insert( pDocument, pEntry, "items", key_value_type_t::ARRAY );
            if ( pItems == nullptr ) { return CY_FALSE; }
            for ( u32 iItem = 0u; iItem < toolbar.nItems; ++iItem ) {
                key_value_t *pItem = KeyValue_ArrayAppend( pDocument, pItems, key_value_type_t::NULL_VALUE );
                if ( pItem == nullptr || !KeyValue_SetString( pDocument, pItem, text( layout.toolbarItems.pData[toolbar.iFirstItem + iItem] ) ) ) {
                    return CY_FALSE;
                }
            }
        }
    }

    key_value_t *pStatus = ResetMember( pStore, "status_bar", key_value_type_t::OBJECT, CY_TRUE, bOk );
    if ( !bOk || pStatus == nullptr || !InsertBool( pDocument, pStatus, "visible", layout.bStatusBarVisible ) ) { return CY_FALSE; }

    key_value_t *pViews = ResetMember( pStore, "views", key_value_type_t::OBJECT, layout.views.bPresent, bOk );
    if ( !bOk ) { return CY_FALSE; }
    if ( pViews != nullptr ) {
        const layout_views_t &views = layout.views;
        if ( !InsertString( pDocument, pViews, "arrangement", StringView_FromCString( kArrangementNames[static_cast<usize>( views.arrangement )] ) ) ||
             ( views.nColumns != 0u && !InsertWeights( pDocument, pViews, "columns", views.columns, views.nColumns ) ) ||
             ( views.nRows != 0u && !InsertWeights( pDocument, pViews, "rows", views.rows, views.nRows ) ) ||
             !InsertI64( pDocument, pViews, "active", views.active ) || !InsertI64( pDocument, pViews, "maximized", views.maximized ) ) {
            return CY_FALSE;
        }
        key_value_t *pPanes = views.nPanes != 0u ? Insert( pDocument, pViews, "panes", key_value_type_t::ARRAY ) : nullptr;
        if ( views.nPanes != 0u && pPanes == nullptr ) { return CY_FALSE; }
        for ( u32 i = 0u; i < views.nPanes; ++i ) {
            const layout_pane_t &pane = views.panes[i];
            key_value_t *pPane = KeyValue_ArrayAppend( pDocument, pPanes, key_value_type_t::OBJECT );
            if ( pPane == nullptr || !InsertString( pDocument, pPane, "view", StringView_FromCString( kViewNames[static_cast<usize>( pane.view )] ) ) ||
                 !InsertString( pDocument, pPane, "render", StringView_FromCString( kRenderNames[static_cast<usize>( pane.render )] ) ) ||
                 !InsertBool( pDocument, pPane, "grid", pane.bGrid ) ) {
                return CY_FALSE;
            }
            if ( pane.show != LAYOUT_OVERLAYS_DEFAULT ) {
                key_value_t *pShow = Insert( pDocument, pPane, "show", key_value_type_t::ARRAY );
                if ( pShow == nullptr ) { return CY_FALSE; }
                for ( usize iName = 0u; iName < std::size( kOverlayNames ); ++iName ) {
                    if ( ( pane.show & ( 1u << iName ) ) == 0u ) { continue; }
                    key_value_t *pName = KeyValue_ArrayAppend( pDocument, pShow, key_value_type_t::NULL_VALUE );
                    if ( pName == nullptr || !KeyValue_SetString( pDocument, pName, StringView_FromCString( kOverlayNames[iName] ) ) ) { return CY_FALSE; }
                }
            }
        }
    }
    return CY_TRUE;
}

} // namespace

layout_status_t EditorLayout_Init( layout_t *pLayout, const allocator_t *pAllocator ) noexcept
{
    if ( pLayout == nullptr || pAllocator == nullptr ) { return layout_status_t::INVALID_ARGUMENT; }
    const bool_t bOk = Vector_Init( &pLayout->nodes, pAllocator ) && Vector_Init( &pLayout->children, pAllocator ) &&
                       Vector_Init( &pLayout->sizes, pAllocator ) && Vector_Init( &pLayout->panels, pAllocator ) &&
                       Vector_Init( &pLayout->windows, pAllocator ) && Vector_Init( &pLayout->text, pAllocator ) &&
                       Vector_Init( &pLayout->hidden, pAllocator ) && Vector_Init( &pLayout->toolbars, pAllocator ) &&
                       Vector_Init( &pLayout->toolbarItems, pAllocator );
    EditorLayout_Clear( pLayout );
    return bOk ? layout_status_t::OK : layout_status_t::OUT_OF_MEMORY;
}

void EditorLayout_Shutdown( layout_t *pLayout ) noexcept
{
    if ( pLayout == nullptr ) { return; }
    Vector_Shutdown( &pLayout->nodes );
    Vector_Shutdown( &pLayout->children );
    Vector_Shutdown( &pLayout->sizes );
    Vector_Shutdown( &pLayout->panels );
    Vector_Shutdown( &pLayout->windows );
    Vector_Shutdown( &pLayout->hidden );
    Vector_Shutdown( &pLayout->toolbars );
    Vector_Shutdown( &pLayout->toolbarItems );
    Vector_Shutdown( &pLayout->text );
}

void EditorLayout_Clear( layout_t *pLayout ) noexcept
{
    if ( pLayout == nullptr ) { return; }
    Vector_Clear( &pLayout->nodes );
    Vector_Clear( &pLayout->children );
    Vector_Clear( &pLayout->sizes );
    Vector_Clear( &pLayout->panels );
    Vector_Clear( &pLayout->windows );
    Vector_Clear( &pLayout->hidden );
    Vector_Clear( &pLayout->toolbars );
    Vector_Clear( &pLayout->toolbarItems );
    Vector_Clear( &pLayout->text );
    pLayout->id = {};
    pLayout->name = {};
    pLayout->workspace = {};
    pLayout->iRoot = 0u;
    pLayout->bHasRoot = CY_FALSE;
    pLayout->author = {};
    pLayout->description = {};
    pLayout->window = {};
    pLayout->bStatusBarVisible = CY_TRUE;
    pLayout->views = layout_views_t{};
    pLayout->nInvalidMembers = 0u;
}

string_view_t EditorLayout_Text( const layout_t *pLayout, layout_text_t text ) noexcept
{
    if ( pLayout == nullptr || static_cast<usize>( text.iOffset ) + text.cchLength > Vector_Count( &pLayout->text ) ) {
        return {};
    }
    return { pLayout->text.pData + text.iOffset, text.cchLength };
}

layout_status_t EditorLayout_AddTabs(
    layout_t *pLayout,
    const string_view_t *pPanels,
    usize nPanels,
    u32 iCurrent,
    u32 *piNodeOut ) noexcept
{
    if ( pLayout == nullptr || pPanels == nullptr || nPanels == 0u || iCurrent >= nPanels ||
         Vector_Count( &pLayout->nodes ) >= EDITOR_LAYOUT_MAX_NODES ) {
        return layout_status_t::INVALID_ARGUMENT;
    }
    for ( usize iPanel = 0u; iPanel < nPanels; ++iPanel ) {
        if ( !IsPanelId( pPanels[iPanel] ) ) { return layout_status_t::INVALID_ARGUMENT; }
    }
    layout_node_t node{};
    node.kind = layout_node_kind_t::TABS;
    node.iFirstPanel = static_cast<u32>( Vector_Count( &pLayout->panels ) );
    node.nPanels = static_cast<u32>( nPanels );
    node.iCurrent = iCurrent;
    for ( usize iPanel = 0u; iPanel < nPanels; ++iPanel ) {
        layout_text_t text{};
        if ( !StoreText( *pLayout, pPanels[iPanel], text ) || !Vector_PushBack( &pLayout->panels, text ) ) {
            return layout_status_t::OUT_OF_MEMORY;
        }
    }
    if ( !Vector_PushBack( &pLayout->nodes, node ) ) { return layout_status_t::OUT_OF_MEMORY; }
    if ( piNodeOut != nullptr ) { *piNodeOut = static_cast<u32>( Vector_Count( &pLayout->nodes ) - 1u ); }
    return layout_status_t::OK;
}

layout_status_t EditorLayout_AddSplit(
    layout_t *pLayout,
    bool_t bVertical,
    const u32 *pChildren,
    const f32 *pSizes,
    usize nChildren,
    u32 *piNodeOut ) noexcept
{
    if ( pLayout == nullptr || pChildren == nullptr || nChildren == 0u ||
         Vector_Count( &pLayout->nodes ) >= EDITOR_LAYOUT_MAX_NODES ) {
        return layout_status_t::INVALID_ARGUMENT;
    }
    f64 flTotal = 0.0;
    for ( usize iChild = 0u; iChild < nChildren; ++iChild ) {
        if ( pChildren[iChild] >= Vector_Count( &pLayout->nodes ) ) { return layout_status_t::INVALID_ARGUMENT; }
        if ( pSizes != nullptr ) {
            if ( !( pSizes[iChild] > 0.0f ) || !std::isfinite( pSizes[iChild] ) ) { return layout_status_t::INVALID_ARGUMENT; }
            flTotal += pSizes[iChild];
        }
    }
    layout_node_t node{};
    node.kind = layout_node_kind_t::SPLIT;
    node.bVertical = bVertical;
    node.iFirstChild = static_cast<u32>( Vector_Count( &pLayout->children ) );
    node.nChildren = static_cast<u32>( nChildren );
    for ( usize iChild = 0u; iChild < nChildren; ++iChild ) {
        // Normalised so every split's shares sum to 1 whatever units were saved.
        const f32 flShare = pSizes != nullptr ? static_cast<f32>( pSizes[iChild] / flTotal )
                                              : 1.0f / static_cast<f32>( nChildren );
        if ( !Vector_PushBack( &pLayout->children, pChildren[iChild] ) || !Vector_PushBack( &pLayout->sizes, flShare ) ) {
            return layout_status_t::OUT_OF_MEMORY;
        }
    }
    if ( !Vector_PushBack( &pLayout->nodes, node ) ) { return layout_status_t::OUT_OF_MEMORY; }
    if ( piNodeOut != nullptr ) { *piNodeOut = static_cast<u32>( Vector_Count( &pLayout->nodes ) - 1u ); }
    return layout_status_t::OK;
}

layout_status_t EditorLayout_AddWindow( layout_t *pLayout, const layout_window_t &window ) noexcept
{
    if ( pLayout == nullptr || window.width == 0u || window.height == 0u ||
         window.iRoot >= Vector_Count( &pLayout->nodes ) ||
         Vector_Count( &pLayout->windows ) >= EDITOR_LAYOUT_MAX_WINDOWS ) {
        return layout_status_t::INVALID_ARGUMENT;
    }
    return Vector_PushBack( &pLayout->windows, window ) ? layout_status_t::OK : layout_status_t::OUT_OF_MEMORY;
}

layout_status_t EditorLayout_SetHeader(
    layout_t *pLayout,
    string_view_t id,
    string_view_t name,
    string_view_t workspace ) noexcept
{
    if ( pLayout == nullptr || !DataValidation_Succeeded( DataValidation_CheckStableIdentifier( id, 64u ) ) ||
         name.cchLength > 128u ||
         ( workspace.cchLength != 0u && !DataValidation_Succeeded( DataValidation_CheckStableIdentifier( workspace, 64u ) ) ) ) {
        return layout_status_t::INVALID_ARGUMENT;
    }
    return StoreText( *pLayout, id, pLayout->id ) && StoreText( *pLayout, name, pLayout->name ) &&
                   StoreText( *pLayout, workspace, pLayout->workspace )
        ? layout_status_t::OK : layout_status_t::OUT_OF_MEMORY;
}

layout_status_t EditorLayout_SetDetails( layout_t *pLayout, string_view_t author, string_view_t description ) noexcept
{
    if ( pLayout == nullptr || author.cchLength > 128u || description.cchLength > 1024u ) { return layout_status_t::INVALID_ARGUMENT; }
    return StoreText( *pLayout, author, pLayout->author ) && StoreText( *pLayout, description, pLayout->description )
        ? layout_status_t::OK : layout_status_t::OUT_OF_MEMORY;
}

layout_status_t EditorLayout_SetWindow( layout_t *pLayout, const layout_main_window_t &window, string_view_t screen ) noexcept
{
    if ( pLayout == nullptr || window.width < 320u || window.height < 240u || screen.cchLength > 128u ) { return layout_status_t::INVALID_ARGUMENT; }
    pLayout->window = window;
    pLayout->window.bPresent = CY_TRUE;
    return StoreText( *pLayout, screen, pLayout->window.screen ) ? layout_status_t::OK : layout_status_t::OUT_OF_MEMORY;
}

layout_status_t EditorLayout_AddHidden( layout_t *pLayout, string_view_t panel, string_view_t beside, layout_area_t area, bool_t bTabbed ) noexcept
{
    if ( pLayout == nullptr || !IsPanelId( panel ) || ( beside.cchLength != 0u && !IsPanelId( beside ) ) ||
         Vector_Count( &pLayout->hidden ) >= EDITOR_LAYOUT_MAX_HIDDEN ) {
        return layout_status_t::INVALID_ARGUMENT;
    }
    layout_hidden_t hidden{};
    hidden.area = area;
    hidden.bTabbed = bTabbed;
    return StoreText( *pLayout, panel, hidden.panel ) && StoreText( *pLayout, beside, hidden.beside ) && Vector_PushBack( &pLayout->hidden, hidden )
        ? layout_status_t::OK : layout_status_t::OUT_OF_MEMORY;
}

layout_status_t EditorLayout_AddToolbar( layout_t *pLayout, string_view_t id, const layout_toolbar_t &placement, const string_view_t *pItems, usize nItems ) noexcept
{
    if ( pLayout == nullptr || !IsPanelId( id ) || Vector_Count( &pLayout->toolbars ) >= EDITOR_LAYOUT_MAX_TOOLBARS ||
         nItems > EDITOR_LAYOUT_MAX_TOOLBAR_ITEMS || ( pItems == nullptr && nItems != 0u ) ) {
        return layout_status_t::INVALID_ARGUMENT;
    }
    layout_toolbar_t toolbar = placement;
    toolbar.bHasItems = pItems != nullptr;
    toolbar.iFirstItem = static_cast<u32>( Vector_Count( &pLayout->toolbarItems ) );
    toolbar.nItems = 0u;
    for ( usize i = 0u; i < nItems; ++i ) {
        if ( !IsPanelId( pItems[i] ) ) { return layout_status_t::INVALID_ARGUMENT; }
        layout_text_t stored{};
        if ( !StoreText( *pLayout, pItems[i], stored ) || !Vector_PushBack( &pLayout->toolbarItems, stored ) ) { return layout_status_t::OUT_OF_MEMORY; }
        ++toolbar.nItems;
    }
    return StoreText( *pLayout, id, toolbar.id ) && Vector_PushBack( &pLayout->toolbars, toolbar ) ? layout_status_t::OK : layout_status_t::OUT_OF_MEMORY;
}

settings_document_identity_t EditorLayout_Identity() noexcept
{
    return { LayoutText( "cypher.layout" ), EDITOR_LAYOUT_OLDEST_SCHEMA_VERSION, EDITOR_LAYOUT_SCHEMA_VERSION };
}

layout_status_t EditorLayout_Decode( const key_value_document_t *pDocument, layout_t *pLayout ) noexcept
{
    if ( pDocument == nullptr || pLayout == nullptr || pLayout->nodes.pAllocator == nullptr ) {
        return layout_status_t::INVALID_ARGUMENT;
    }
    const key_value_document_header_t header = KeyValue_DocumentHeader( pDocument );
    const key_value_t *pRoot = KeyValue_Root( pDocument );
    if ( header.nLanguageVersion != CYKV_LANGUAGE_VERSION ||
         !StringView_Equals( header.schemaId, LayoutText( "cypher.layout" ) ) ||
         header.nSchemaVersion < EDITOR_LAYOUT_OLDEST_SCHEMA_VERSION || header.nSchemaVersion > EDITOR_LAYOUT_SCHEMA_VERSION ||
         KeyValue_Type( pRoot ) != key_value_type_t::OBJECT ) {
        return layout_status_t::INVALID_HEADER;
    }

    layout_t scratch{};
    if ( EditorLayout_Init( &scratch, pLayout->nodes.pAllocator ) != layout_status_t::OK ) {
        EditorLayout_Shutdown( &scratch );
        return layout_status_t::OUT_OF_MEMORY;
    }
    const auto fail = [&]( layout_status_t status ) noexcept {
        EditorLayout_Shutdown( &scratch );
        return status;
    };

    string_view_t id{};
    string_view_t name{};
    string_view_t workspace{};
    // Header strings are optional and tolerant: a bad one is simply dropped.
    if ( KeyValue_GetString( KeyValue_Find( pRoot, LayoutText( "id" ) ), &id ) &&
         !DataValidation_Succeeded( DataValidation_CheckStableIdentifier( id, 64u ) ) ) { id = {}; }
    if ( KeyValue_GetString( KeyValue_Find( pRoot, LayoutText( "name" ) ), &name ) && name.cchLength > 128u ) { name = {}; }
    if ( KeyValue_GetString( KeyValue_Find( pRoot, LayoutText( "workspace" ) ), &workspace ) &&
         !DataValidation_Succeeded( DataValidation_CheckStableIdentifier( workspace, 64u ) ) ) { workspace = {}; }
    if ( !StoreText( scratch, id, scratch.id ) || !StoreText( scratch, name, scratch.name ) ||
         !StoreText( scratch, workspace, scratch.workspace ) ) {
        return fail( layout_status_t::OUT_OF_MEMORY );
    }

    decoder_t decoder{ scratch };
    const key_value_t *pTree = KeyValue_Find( pRoot, LayoutText( "root" ) );
    if ( pTree == nullptr ) { return fail( layout_status_t::INVALID_TREE ); }
    layout_status_t status = DecodeNode( decoder, pTree, 0u, scratch.iRoot );
    if ( status != layout_status_t::OK ) {
        return fail( status == layout_status_t::INVALID_ARGUMENT ? layout_status_t::INVALID_TREE : status );
    }
    scratch.bHasRoot = CY_TRUE;

    const key_value_t *pFloating = KeyValue_Find( pRoot, LayoutText( "floating" ) );
    if ( pFloating != nullptr ) {
        if ( KeyValue_Type( pFloating ) != key_value_type_t::ARRAY ||
             KeyValue_ChildCount( pFloating ) > EDITOR_LAYOUT_MAX_WINDOWS ) {
            return fail( layout_status_t::INVALID_WINDOW );
        }
        for ( usize iWindow = 0u; iWindow < KeyValue_ChildCount( pFloating ); ++iWindow ) {
            const key_value_t *pWindow = KeyValue_ChildAt( pFloating, iWindow );
            f64 rect[4]{};
            constexpr string_view_t kKeys[4]{ LayoutText( "x" ), LayoutText( "y" ), LayoutText( "width" ), LayoutText( "height" ) };
            for ( usize iKey = 0u; iKey < 4u; ++iKey ) {
                if ( KeyValue_Type( pWindow ) != key_value_type_t::OBJECT ||
                     !ReadNumber( KeyValue_Find( pWindow, kKeys[iKey] ), rect[iKey] ) || std::fabs( rect[iKey] ) > 1.0e6 ) {
                    return fail( layout_status_t::INVALID_WINDOW );
                }
            }
            if ( rect[2] < 1.0 || rect[3] < 1.0 ) { return fail( layout_status_t::INVALID_WINDOW ); }
            layout_window_t window{};
            window.x = static_cast<i32>( rect[0] );
            window.y = static_cast<i32>( rect[1] );
            window.width = static_cast<u32>( rect[2] );
            window.height = static_cast<u32>( rect[3] );
            status = DecodeNode( decoder, KeyValue_Find( pWindow, LayoutText( "root" ) ), 0u, window.iRoot );
            if ( status != layout_status_t::OK ) {
                return fail( status == layout_status_t::INVALID_ARGUMENT ? layout_status_t::INVALID_WINDOW : status );
            }
            status = EditorLayout_AddWindow( &scratch, window );
            if ( status != layout_status_t::OK ) { return fail( status ); }
        }
    }

    // V2 members fall back to defaults instead of rejecting the layout.
    status = DecodeV2( scratch, pRoot );
    if ( status != layout_status_t::OK ) { return fail( status ); }

    // Commit: release the caller's storage, then move the scratch vectors in
    // (a move needs an empty destination and cannot fail).
    EditorLayout_Shutdown( pLayout );
    Vector_Move( &pLayout->nodes, &scratch.nodes );
    Vector_Move( &pLayout->children, &scratch.children );
    Vector_Move( &pLayout->sizes, &scratch.sizes );
    Vector_Move( &pLayout->panels, &scratch.panels );
    Vector_Move( &pLayout->windows, &scratch.windows );
    Vector_Move( &pLayout->text, &scratch.text );
    Vector_Move( &pLayout->hidden, &scratch.hidden );
    Vector_Move( &pLayout->toolbars, &scratch.toolbars );
    Vector_Move( &pLayout->toolbarItems, &scratch.toolbarItems );
    pLayout->id = scratch.id;
    pLayout->name = scratch.name;
    pLayout->workspace = scratch.workspace;
    pLayout->iRoot = scratch.iRoot;
    pLayout->bHasRoot = scratch.bHasRoot;
    pLayout->author = scratch.author;
    pLayout->description = scratch.description;
    pLayout->window = scratch.window;
    pLayout->bStatusBarVisible = scratch.bStatusBarVisible;
    pLayout->views = scratch.views;
    pLayout->nInvalidMembers = scratch.nInvalidMembers;
    return layout_status_t::OK;
}

layout_status_t EditorLayout_Encode( const layout_t *pLayout, settings_document_t *pStore ) noexcept
{
    if ( pLayout == nullptr || !pLayout->bHasRoot || !SettingsDocument_IsInitialized( pStore ) ) {
        return layout_status_t::INVALID_ARGUMENT;
    }
    key_value_document_t *pDocument = pStore->pDocument;
    const struct { const char *pKey; layout_text_t text; } header[]{
        { "id", pLayout->id }, { "name", pLayout->name }, { "workspace", pLayout->workspace }
    };
    for ( const auto &field : header ) {
        settings_path_t path{};
        if ( !SettingsPath_Append( &path, StringView_FromCString( field.pKey ) ) ) { return layout_status_t::INVALID_ARGUMENT; }
        if ( field.text.cchLength == 0u ) {
            if ( SettingsDocument_Remove( pStore, path ) != settings_document_status_t::OK ) { return layout_status_t::STORE_FAILED; }
            continue;
        }
        key_value_t *pNode = nullptr;
        if ( SettingsDocument_Ensure( pStore, path, &pNode ) != settings_document_status_t::OK ||
             !KeyValue_SetString( pDocument, pNode, EditorLayout_Text( pLayout, field.text ) ) ) {
            return layout_status_t::STORE_FAILED;
        }
    }

    settings_path_t rootPath{};
    key_value_t *pTree = nullptr;
    if ( !SettingsPath_Append( &rootPath, LayoutText( "root" ) ) ||
         SettingsDocument_Ensure( pStore, rootPath, &pTree ) != settings_document_status_t::OK ||
         !EncodeNode( *pLayout, pLayout->iRoot, pDocument, pTree ) ) {
        return layout_status_t::STORE_FAILED;
    }
    if ( !EncodeV2( *pLayout, pStore ) ) { return layout_status_t::STORE_FAILED; }
    settings_path_t floatingPath{};
    if ( !SettingsPath_Append( &floatingPath, LayoutText( "floating" ) ) ) { return layout_status_t::INVALID_ARGUMENT; }
    if ( Vector_Count( &pLayout->windows ) == 0u ) {
        return SettingsDocument_Remove( pStore, floatingPath ) == settings_document_status_t::OK
            ? layout_status_t::OK : layout_status_t::STORE_FAILED;
    }
    key_value_t *pFloating = nullptr;
    if ( SettingsDocument_Ensure( pStore, floatingPath, &pFloating ) != settings_document_status_t::OK ||
         !KeyValue_SetContainerType( pDocument, pFloating, key_value_type_t::ARRAY ) ) {
        return layout_status_t::STORE_FAILED;
    }
    for ( usize iWindow = 0u; iWindow < Vector_Count( &pLayout->windows ); ++iWindow ) {
        const layout_window_t &window = pLayout->windows.pData[iWindow];
        key_value_t *pWindow = KeyValue_ArrayAppend( pDocument, pFloating, key_value_type_t::OBJECT );
        if ( pWindow == nullptr ) { return layout_status_t::OUT_OF_MEMORY; }
        const struct { const char *pKey; i64 nValue; } rect[]{
            { "x", window.x }, { "y", window.y }, { "width", window.width }, { "height", window.height }
        };
        for ( const auto &field : rect ) {
            key_value_t *pField = KeyValue_ObjectInsert( pDocument, pWindow, StringView_FromCString( field.pKey ), key_value_type_t::NULL_VALUE );
            if ( pField == nullptr || !KeyValue_SetI64( pDocument, pField, field.nValue ) ) { return layout_status_t::OUT_OF_MEMORY; }
        }
        key_value_t *pWindowRoot = KeyValue_ObjectInsert( pDocument, pWindow, LayoutText( "root" ), key_value_type_t::OBJECT );
        if ( pWindowRoot == nullptr || !EncodeNode( *pLayout, window.iRoot, pDocument, pWindowRoot ) ) {
            return layout_status_t::OUT_OF_MEMORY;
        }
    }
    return layout_status_t::OK;
}

const char *EditorLayout_StatusName( layout_status_t status ) noexcept
{
    switch ( status ) {
        case layout_status_t::OK: return "OK";
        case layout_status_t::INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        case layout_status_t::INVALID_HEADER: return "INVALID_HEADER";
        case layout_status_t::INVALID_TREE: return "INVALID_TREE";
        case layout_status_t::INVALID_WINDOW: return "INVALID_WINDOW";
        case layout_status_t::OUT_OF_MEMORY: return "OUT_OF_MEMORY";
        case layout_status_t::STORE_FAILED: return "STORE_FAILED";
    }
    return "UNKNOWN";
}

} // namespace cypher::editor
