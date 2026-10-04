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

namespace
{

// CYLAYOUT.md section 2, verbatim in shape.
const char *kMapping = R"cykv(@cykv 1
@schema "cypher.layout" 2
{
    id = "mapping"
    name = "Mapping"
    author = "Karlo"
    description = "Four views, console under them, properties full height."
    workspace = "map"
    window = { x = 40 y = 30 width = 2400 height = 1320 maximized = true fullscreen = false screen = "DELL U2720Q" }
    root = {
        split = "horizontal"
        sizes = [ 0.78, 0.22 ]
        children = [
            { split = "vertical" sizes = [ 0.8, 0.2 ] children = [
                { tabs = [ "mason.views" ] },
                { tabs = [ "console", "map.problems" ] current = 0 }
            ] },
            { split = "vertical" sizes = [ 0.45, 0.55 ] children = [
                { tabs = [ "map.outliner", "map.layers" ] },
                { tabs = [ "map.properties" ] }
            ] }
        ]
    }
    floating = [ { x = 120 y = 120 width = 520 height = 640 root = { tabs = [ "map.materials" ] } } ]
    hidden = [ { panel = "map.history" beside = "map.outliner" area = "right" } ]
    toolbars = [
        { id = "mason.file" area = "top" row = 0 order = 0 },
        { id = "map.tools" area = "left" row = 0 order = 0 icon_size = 28 },
        { id = "map.render" area = "top" row = 1 order = 0 visible = false items = [ "map.render.wireframe", "-", "map.render.lit" ] }
    ]
    status_bar = { visible = false }
    views = {
        arrangement = "quad"
        columns = [ 0.5, 0.5 ]
        rows = [ 0.55, 0.45 ]
        active = 3
        maximized = -1
        panes = [
            { view = "top" render = "wireframe" grid = true },
            { view = "front" render = "wireframe" grid = true },
            { view = "side" render = "wireframe" grid = true },
            { view = "perspective" render = "textured" grid = false show = [ "entities", "entity_names" ] }
        ]
    }
    panels = {
        "map.outliner" = { columns = [ 220, 80, 60 ] group_by = "layer" }
    }
}
)cykv";

void CheckMapping( const layout_t &layout )
{
    CHECK( Equals( EditorLayout_Text( &layout, layout.author ), "Karlo" ) );
    CHECK( layout.window.bPresent );
    CHECK( layout.window.width == 2400u );
    CHECK( layout.window.bMaximized );
    CHECK( Equals( EditorLayout_Text( &layout, layout.window.screen ), "DELL U2720Q" ) );
    REQUIRE( Vector_Count( &layout.hidden ) == 1u );
    CHECK( Equals( EditorLayout_Text( &layout, layout.hidden.pData[0].beside ), "map.outliner" ) );
    CHECK( layout.hidden.pData[0].area == layout_area_t::RIGHT );
    REQUIRE( Vector_Count( &layout.toolbars ) == 3u );
    CHECK( layout.toolbars.pData[1].area == layout_area_t::LEFT );
    CHECK( layout.toolbars.pData[1].iconSize == 28.0 );
    const layout_toolbar_t &render = layout.toolbars.pData[2];
    CHECK_FALSE( render.bVisible );
    CHECK( render.row == 1u );
    REQUIRE( render.bHasItems );
    REQUIRE( render.nItems == 3u );
    CHECK( Equals( EditorLayout_Text( &layout, layout.toolbarItems.pData[render.iFirstItem + 1u] ), "-" ) );
    CHECK_FALSE( layout.bStatusBarVisible );
    REQUIRE( layout.views.bPresent );
    CHECK( layout.views.arrangement == layout_arrangement_t::QUAD );
    CHECK( layout.views.nRows == 2u );
    CHECK( layout.views.active == 3 );
    REQUIRE( layout.views.nPanes == 4u );
    CHECK( layout.views.panes[3].view == layout_view_kind_t::PERSPECTIVE );
    CHECK( layout.views.panes[3].render == layout_render_t::TEXTURED );
    CHECK_FALSE( layout.views.panes[3].bGrid );
    CHECK( layout.views.panes[3].show == ( LAYOUT_OVERLAY_ENTITIES | LAYOUT_OVERLAY_ENTITY_NAMES ) );
    CHECK( layout.views.panes[0].show == LAYOUT_OVERLAYS_DEFAULT );
    CHECK( layout.nInvalidMembers == 0u );
}

} // namespace

TEST_CASE( "V2 layouts decode window, hidden panels, toolbars, status bar, and views", "[CypherEditor][Core][Layout]" )
{
    layout_t layout{};
    REQUIRE( EditorLayout_Init( &layout, Allocator_GetSystem() ) == layout_status_t::OK );
    key_value_document_t *pDocument = Parse( kMapping );
    REQUIRE( EditorLayout_Decode( pDocument, &layout ) == layout_status_t::OK );
    KeyValue_DestroyDocument( pDocument );
    CheckMapping( layout );

    // Round trip through a store keeps everything, including panel state.
    settings_document_t store{};
    REQUIRE( SettingsDocument_Init( &store, Allocator_GetSystem(), EditorLayout_Identity() ) == settings_document_status_t::OK );
    REQUIRE( SettingsDocument_Load( &store, StringView_FromCString( kMapping ) ).status == settings_document_status_t::OK );
    REQUIRE( EditorLayout_Encode( &layout, &store ) == layout_status_t::OK );
    text_buffer_t text{};
    REQUIRE( TextBuffer_Init( &text, Allocator_GetSystem() ) );
    REQUIRE( SettingsDocument_Write( &store, &text ) == settings_document_status_t::OK );
    const std::string written( TextBuffer_CStr( &text ) );
    CHECK( written.find( "group_by" ) != std::string::npos );
    CHECK( written.find( "\"cypher.layout\" 2" ) != std::string::npos );
    layout_t again{};
    REQUIRE( EditorLayout_Init( &again, Allocator_GetSystem() ) == layout_status_t::OK );
    key_value_document_t *pWritten = Parse( written.c_str() );
    REQUIRE( EditorLayout_Decode( pWritten, &again ) == layout_status_t::OK );
    KeyValue_DestroyDocument( pWritten );
    CheckMapping( again );
    EditorLayout_Shutdown( &again );
    EditorLayout_Shutdown( &layout );
}

TEST_CASE( "Invalid V2 members fall back to defaults and are counted", "[CypherEditor][Core][Layout]" )
{
    layout_t layout{};
    REQUIRE( EditorLayout_Init( &layout, Allocator_GetSystem() ) == layout_status_t::OK );
    key_value_document_t *pDocument = Parse( R"cykv(@cykv 1
@schema "cypher.layout" 2
{
    id = "bad"
    root = { tabs = [ "mason.views" ] }
    window = { width = 100 height = 800 maximized = "yes" }
    hidden = [ { beside = "x" }, { panel = "map.history" area = "sideways" } ]
    toolbars = [ { id = "map.tools" area = "diagonal" row = -3 } ]
    status_bar = true
    views = { arrangement = "hexagon" columns = [ 0.5, -1 ] panes = [ { view = "under" render = "psychedelic" show = [ "ghosts" ] } ] }
}
)cykv" );
    REQUIRE( EditorLayout_Decode( pDocument, &layout ) == layout_status_t::OK ); // Never rejected for these.
    KeyValue_DestroyDocument( pDocument );
    CHECK( layout.window.width == 1280u );
    CHECK_FALSE( layout.window.bMaximized );
    REQUIRE( Vector_Count( &layout.hidden ) == 1u ); // The entry without a panel is dropped.
    CHECK( layout.hidden.pData[0].area == layout_area_t::RIGHT );
    REQUIRE( Vector_Count( &layout.toolbars ) == 1u );
    CHECK( layout.toolbars.pData[0].area == layout_area_t::TOP );
    CHECK( layout.toolbars.pData[0].row == 0u );
    CHECK( layout.bStatusBarVisible );
    CHECK( layout.views.arrangement == layout_arrangement_t::QUAD );
    CHECK( layout.views.nColumns == 0u );
    REQUIRE( layout.views.nPanes == 1u );
    CHECK( layout.views.panes[0].view == layout_view_kind_t::TOP );
    CHECK( layout.nInvalidMembers == 12u );
    EditorLayout_Shutdown( &layout );
}

TEST_CASE( "V2 builders fill what a captured layout saves", "[CypherEditor][Core][Layout]" )
{
    layout_t layout{};
    REQUIRE( EditorLayout_Init( &layout, Allocator_GetSystem() ) == layout_status_t::OK );
    layout_main_window_t window{};
    window.width = 1600u;
    window.height = 1000u;
    REQUIRE( EditorLayout_SetWindow( &layout, window, StringView_FromCString( "Built-in" ) ) == layout_status_t::OK );
    window.width = 100u;
    CHECK( EditorLayout_SetWindow( &layout, window, string_view_t{} ) == layout_status_t::INVALID_ARGUMENT );
    REQUIRE( EditorLayout_AddHidden( &layout, StringView_FromCString( "map.history" ), StringView_FromCString( "map.outliner" ),
                                     layout_area_t::RIGHT, CY_TRUE ) == layout_status_t::OK );
    layout_toolbar_t placement{};
    placement.area = layout_area_t::LEFT;
    const string_view_t items[]{ StringView_FromCString( "map.tool.select" ), StringView_FromCString( "-" ) };
    REQUIRE( EditorLayout_AddToolbar( &layout, StringView_FromCString( "map.tools" ), placement, items, 2u ) == layout_status_t::OK );
    REQUIRE( EditorLayout_SetDetails( &layout, StringView_FromCString( "Me" ), string_view_t{} ) == layout_status_t::OK );
    CHECK( layout.window.width == 1600u );
    CHECK( layout.hidden.pData[0].bTabbed );
    CHECK( layout.toolbars.pData[0].nItems == 2u );
    CHECK( Equals( EditorLayout_Text( &layout, layout.author ), "Me" ) );
    EditorLayout_Shutdown( &layout );
}
