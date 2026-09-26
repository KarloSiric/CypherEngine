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

} // namespace

layout_status_t EditorLayout_Init( layout_t *pLayout, const allocator_t *pAllocator ) noexcept
{
    if ( pLayout == nullptr || pAllocator == nullptr ) { return layout_status_t::INVALID_ARGUMENT; }
    const bool_t bOk = Vector_Init( &pLayout->nodes, pAllocator ) && Vector_Init( &pLayout->children, pAllocator ) &&
                       Vector_Init( &pLayout->sizes, pAllocator ) && Vector_Init( &pLayout->panels, pAllocator ) &&
                       Vector_Init( &pLayout->windows, pAllocator ) && Vector_Init( &pLayout->text, pAllocator );
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
    Vector_Clear( &pLayout->text );
    pLayout->id = {};
    pLayout->name = {};
    pLayout->workspace = {};
    pLayout->iRoot = 0u;
    pLayout->bHasRoot = CY_FALSE;
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

settings_document_identity_t EditorLayout_Identity() noexcept
{
    return { LayoutText( "cypher.layout" ), EDITOR_LAYOUT_SCHEMA_VERSION, EDITOR_LAYOUT_SCHEMA_VERSION };
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
         header.nSchemaVersion != EDITOR_LAYOUT_SCHEMA_VERSION || KeyValue_Type( pRoot ) != key_value_type_t::OBJECT ) {
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

    // Commit: release the caller's storage, then move the scratch vectors in
    // (a move needs an empty destination and cannot fail).
    EditorLayout_Shutdown( pLayout );
    Vector_Move( &pLayout->nodes, &scratch.nodes );
    Vector_Move( &pLayout->children, &scratch.children );
    Vector_Move( &pLayout->sizes, &scratch.sizes );
    Vector_Move( &pLayout->panels, &scratch.panels );
    Vector_Move( &pLayout->windows, &scratch.windows );
    Vector_Move( &pLayout->text, &scratch.text );
    pLayout->id = scratch.id;
    pLayout->name = scratch.name;
    pLayout->workspace = scratch.workspace;
    pLayout->iRoot = scratch.iRoot;
    pLayout->bHasRoot = scratch.bHasRoot;
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
