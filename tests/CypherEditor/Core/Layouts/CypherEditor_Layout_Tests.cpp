//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditor_Layout_Tests.cpp
//  Purpose: Contract tests for dock layouts.
//  Details: Decoding, normalised split sizes, tolerated damage, rejected
//           structure, and encode/decode round trips.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditor_Layout.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace cypher::common;
using namespace cypher::editor;

namespace
{

const char *kFourView = R"cykv(@cykv 1
@schema "cypher.layout" 1
{
    id = "four_view"
    name = "Four views"
    workspace = "map"
    root = {
        split = "horizontal"
        sizes = [ 1, 3, 1 ]
        children = [
            { tabs = [ "map.tools" ] },
            { split = "vertical" sizes = [ 0.5, 0.5 ] children = [
                { tabs = [ "map.viewport.3d" ] },
                { tabs = [ "map.viewport.top" ] }
            ] },
            { tabs = [ "map.outliner", "map.properties" ] current = 1 }
        ]
    }
    floating = [ { x = 80 y = 80 width = 640 height = 320 root = { tabs = [ "console" ] } } ]
    written_by_newer_editor = true
}
)cykv";

key_value_document_t *Parse( const char *pText )
{
    key_value_document_t *pDocument = KeyValue_CreateDocument( {} );
    REQUIRE( pDocument != nullptr );
    REQUIRE( KeyValue_ParseText( StringView_FromCString( pText ), {}, pDocument ).status == key_value_parse_status_t::OK );
    return pDocument;
}

bool_t Equals( string_view_t view, const char *pText )
{
    return StringView_Equals( view, StringView_FromCString( pText ) );
}

} // namespace

TEST_CASE( "Layouts decode splits, tab groups, and floating windows",
           "[CypherEditor][Core][Layout]" )
{
    key_value_document_t *pDocument = Parse( kFourView );
    layout_t layout{};
    REQUIRE( EditorLayout_Init( &layout, Allocator_GetSystem() ) == layout_status_t::OK );
    REQUIRE( EditorLayout_Decode( pDocument, &layout ) == layout_status_t::OK );
    KeyValue_DestroyDocument( pDocument ); // The layout owns its strings.

    REQUIRE( Equals( EditorLayout_Text( &layout, layout.id ), "four_view" ) );
    REQUIRE( Equals( EditorLayout_Text( &layout, layout.workspace ), "map" ) );
    const layout_node_t &root = layout.nodes.pData[layout.iRoot];
    REQUIRE( root.kind == layout_node_kind_t::SPLIT );
    REQUIRE( root.nChildren == 3u );
    REQUIRE( layout.sizes.pData[root.iFirstChild] == 0.2f );
    REQUIRE( layout.sizes.pData[root.iFirstChild + 1u] == 0.6f );

    const layout_node_t &right = layout.nodes.pData[layout.children.pData[root.iFirstChild + 2u]];
    REQUIRE( right.kind == layout_node_kind_t::TABS );
    REQUIRE( right.nPanels == 2u );
    REQUIRE( right.iCurrent == 1u );
    REQUIRE( Equals( EditorLayout_Text( &layout, layout.panels.pData[right.iFirstPanel + 1u] ), "map.properties" ) );
    const layout_node_t &middle = layout.nodes.pData[layout.children.pData[root.iFirstChild + 1u]];
    REQUIRE( middle.bVertical == CY_TRUE );

    REQUIRE( Vector_Count( &layout.windows ) == 1u );
    REQUIRE( layout.windows.pData[0].width == 640u );
    EditorLayout_Shutdown( &layout );
}

TEST_CASE( "Layouts tolerate bad sizes and panel entries but reject bad structure",
           "[CypherEditor][Core][Layout]" )
{
    layout_t layout{};
    REQUIRE( EditorLayout_Init( &layout, Allocator_GetSystem() ) == layout_status_t::OK );

    key_value_document_t *pDocument = Parse(
        "@cykv 1\n@schema \"cypher.layout\" 1\n"
        "{ root = { split = \"horizontal\" sizes = [ 1 ] children = [ { tabs = [ \"a\", 5, \"bad id\" ] }, { tabs = [ \"b\" ] } ] } }" );
    REQUIRE( EditorLayout_Decode( pDocument, &layout ) == layout_status_t::OK );
    const layout_node_t &root = layout.nodes.pData[layout.iRoot];
    REQUIRE( layout.sizes.pData[root.iFirstChild] == 0.5f );
    REQUIRE( layout.nodes.pData[layout.children.pData[root.iFirstChild]].nPanels == 1u );
    KeyValue_DestroyDocument( pDocument );
    const usize nNodesBefore = Vector_Count( &layout.nodes );

    struct case_t {
        const char *pBody;
        layout_status_t expected;
    };
    const case_t cases[]{
        { "{ id = \"x\" }", layout_status_t::INVALID_TREE },
        { "{ root = { tabs = [ \"a\" ] split = \"vertical\" } }", layout_status_t::INVALID_TREE },
        { "{ root = { split = \"diagonal\" children = [ { tabs = [ \"a\" ] } ] } }", layout_status_t::INVALID_TREE },
        { "{ root = { split = \"vertical\" children = [] } }", layout_status_t::INVALID_TREE },
        { "{ root = { tabs = [] } }", layout_status_t::INVALID_TREE },
        { "{ root = { tabs = [ \"a\" ] } floating = [ { x = 0 y = 0 width = 0 height = 10 root = { tabs = [ \"b\" ] } } ] }", layout_status_t::INVALID_WINDOW },
    };
    for ( const case_t &c : cases ) {
        const std::string source = std::string( "@cykv 1\n@schema \"cypher.layout\" 1\n" ) + c.pBody;
        key_value_document_t *pBad = Parse( source.c_str() );
        CAPTURE( c.pBody );
        REQUIRE( EditorLayout_Decode( pBad, &layout ) == c.expected );
        REQUIRE( Vector_Count( &layout.nodes ) == nNodesBefore ); // Previous layout kept.
        KeyValue_DestroyDocument( pBad );
    }

    // Nesting beyond the depth limit is rejected rather than recursing on.
    std::string deep = "{ tabs = [ \"leaf\" ] }";
    for ( usize iLevel = 0u; iLevel <= EDITOR_LAYOUT_MAX_DEPTH + 1u; ++iLevel ) {
        deep = "{ split = \"vertical\" children = [ " + deep + " ] }";
    }
    key_value_document_t *pDeep = Parse( ( "@cykv 1\n@schema \"cypher.layout\" 1\n{ root = " + deep + " }" ).c_str() );
    REQUIRE( EditorLayout_Decode( pDeep, &layout ) == layout_status_t::INVALID_TREE );
    KeyValue_DestroyDocument( pDeep );
    EditorLayout_Shutdown( &layout );
}

TEST_CASE( "Layouts round-trip through a store and keep unknown members",
           "[CypherEditor][Core][Layout]" )
{
    settings_document_t store{};
    REQUIRE( SettingsDocument_Init( &store, Allocator_GetSystem(), EditorLayout_Identity() ) == settings_document_status_t::OK );
    REQUIRE( SettingsDocument_Load( &store, StringView_FromCString( kFourView ) ).status == settings_document_status_t::OK );

    layout_t layout{};
    REQUIRE( EditorLayout_Init( &layout, Allocator_GetSystem() ) == layout_status_t::OK );
    REQUIRE( EditorLayout_Decode( store.pDocument, &layout ) == layout_status_t::OK );

    // Rebuild the layout from builders (as the GUI does) and encode it back.
    layout_t captured{};
    REQUIRE( EditorLayout_Init( &captured, Allocator_GetSystem() ) == layout_status_t::OK );
    const string_view_t left[]{ StringView_FromCString( "map.tools" ) };
    const string_view_t right[]{ StringView_FromCString( "map.viewport.3d" ), StringView_FromCString( "console" ) };
    u32 iLeft = 0u;
    u32 iRight = 0u;
    u32 iRoot = 0u;
    REQUIRE( EditorLayout_AddTabs( &captured, left, 1u, 0u, &iLeft ) == layout_status_t::OK );
    REQUIRE( EditorLayout_AddTabs( &captured, right, 2u, 1u, &iRight ) == layout_status_t::OK );
    const u32 children[]{ iLeft, iRight };
    const f32 sizes[]{ 250.0f, 750.0f };
    REQUIRE( EditorLayout_AddSplit( &captured, CY_FALSE, children, sizes, 2u, &iRoot ) == layout_status_t::OK );
    captured.iRoot = iRoot;
    captured.bHasRoot = CY_TRUE;
    REQUIRE( EditorLayout_SetHeader( &captured, StringView_FromCString( "two_pane" ), StringView_FromCString( "Two pane" ),
                                     StringView_FromCString( "map" ) ) == layout_status_t::OK );
    REQUIRE( EditorLayout_Encode( &captured, &store ) == layout_status_t::OK );

    text_buffer_t text{};
    REQUIRE( TextBuffer_Init( &text, Allocator_GetSystem() ) );
    REQUIRE( SettingsDocument_Write( &store, &text ) == settings_document_status_t::OK );
    const std::string written( TextBuffer_Data( &text ), TextBuffer_Length( &text ) );
    REQUIRE( written.find( "written_by_newer_editor" ) != std::string::npos );
    REQUIRE( written.find( "floating" ) == std::string::npos ); // No windows now.

    key_value_document_t *pDocument = Parse( written.c_str() );
    REQUIRE( EditorLayout_Decode( pDocument, &layout ) == layout_status_t::OK );
    REQUIRE( Equals( EditorLayout_Text( &layout, layout.id ), "two_pane" ) );
    const layout_node_t &root = layout.nodes.pData[layout.iRoot];
    REQUIRE( root.nChildren == 2u );
    REQUIRE( layout.sizes.pData[root.iFirstChild] == 0.25f );
    REQUIRE( layout.nodes.pData[layout.children.pData[root.iFirstChild + 1u]].iCurrent == 1u );
    KeyValue_DestroyDocument( pDocument );
    EditorLayout_Shutdown( &captured );
    EditorLayout_Shutdown( &layout );
}
