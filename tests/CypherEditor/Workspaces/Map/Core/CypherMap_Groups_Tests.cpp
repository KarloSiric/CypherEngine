//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_Groups_Tests.cpp
//  Purpose: Tests group names, membership edits, pruning after deletes and
//           copying groups with duplicated objects.
//
//  History:
//  - Created by Karlo Siric on 2026-10-08
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMap_Groups.h"
#include "CypherMap_Edit.h"
#include "CypherMap_Clipboard.h"
#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace cypher;
using namespace cypher::common;
using namespace cypher::editor::map;

namespace
{
string_view_t SV( const char *p ) { return StringView_FromCString( p ); }
struct fixture_t {
    map_document_t map{};
    fixture_t() { REQUIRE( MapDocument_Create( &map, Allocator_GetSystem(), { SV( "Groups fixture" ), SV( "reap" ), {} } ) == map_status_t::OK ); }
    u64 Box( f64 x )
    {
        map_primitive_desc_t desc{}; desc.kind = map_primitive_kind_t::BOX; desc.bounds = { { x, 0, 0 }, { x + 64, 64, 64 } }; desc.axis = 2;
        u64 id = 0;
        REQUIRE( MapEdit_CreatePrimitive( &map, desc, SV( "materials/dev/dev_grid.cymat" ), {}, &id ) == map_status_t::OK );
        return id;
    }
    u64 Group( std::vector<u64> members, const char *name = "" )
    {
        u64 id = 0;
        REQUIRE( MapDocument_AddGroup( &map, SV( name ), { members.data(), members.size() }, &id ) == map_status_t::OK );
        return id;
    }
    std::vector<u64> Members( u64 group ) { std::vector<u64> out; REQUIRE( MapGroups_Members( &map, group, out ) == map_status_t::OK ); return out; }
    std::vector<u64> Leaves( u64 group )
    {
        vector_t<u64> all{}; REQUIRE( Vector_Init( &all, Allocator_GetSystem() ) );
        REQUIRE( MapDocument_GroupMembers( &map, group, &all ) == map_status_t::OK );
        std::vector<u64> out( all.pData, all.pData + all.nCount );
        Vector_Shutdown( &all );
        return out;
    }
};
} // namespace

TEST_CASE( "Groups rename, take members in and out without cycles, and dissolve when empty", "[map][groups]" )
{
    fixture_t f;
    const u64 a = f.Box( 0 ), b = f.Box( 128 ), c = f.Box( 256 ), d = f.Box( 384 );
    const u64 inner = f.Group( { a, b }, "pillars" ), outer = f.Group( { inner, c } );
    CHECK( MapGroups_Parent( &f.map, a ) == inner ); CHECK( MapGroups_Parent( &f.map, inner ) == outer ); CHECK( MapGroups_Parent( &f.map, d ) == 0 );
    CHECK( MapGroups_Name( &f.map, inner ) == "pillars" );
    bool changed = false;
    REQUIRE( MapGroups_Rename( &f.map, inner, SV( "columns" ), &changed ) == map_status_t::OK ); CHECK( changed );
    CHECK( MapGroups_Name( &f.map, inner ) == "columns" );
    REQUIRE( MapGroups_Rename( &f.map, inner, SV( "columns" ), &changed ) == map_status_t::OK ); CHECK_FALSE( changed );
    // D joins the outer group; nothing can go into a group nested in it.
    REQUIRE( MapGroups_Add( &f.map, outer, { &d, 1 }, &changed ) == map_status_t::OK ); CHECK( changed );
    CHECK( MapGroups_Parent( &f.map, d ) == outer );
    CHECK( MapGroups_Add( &f.map, inner, { &outer, 1 }, &changed ) == map_status_t::INVALID_ARGUMENT );
    CHECK( MapGroups_Add( &f.map, inner, { &inner, 1 }, &changed ) == map_status_t::INVALID_ARGUMENT );
    // C moves from the outer group to the inner one.
    REQUIRE( MapGroups_Add( &f.map, inner, { &c, 1 }, &changed ) == map_status_t::OK );
    CHECK( MapGroups_Parent( &f.map, c ) == inner ); CHECK( f.Members( outer ) == std::vector<u64>{ std::min( inner, d ), std::max( inner, d ) } ); // Sorted.
    // Taking A, B and C out empties the inner group: it dissolves, and the
    // outer group no longer lists it.
    const u64 leaving[]{ a, b, c };
    REQUIRE( MapGroups_Remove( &f.map, { leaving, 3 }, &changed ) == map_status_t::OK ); CHECK( changed );
    const auto all = MapGroups_All( &f.map );
    CHECK( std::find( all.begin(), all.end(), inner ) == all.end() );
    CHECK( f.Members( outer ) == std::vector<u64>{ d } );
}

TEST_CASE( "Deleted members drop out of their groups and empty groups dissolve", "[map][groups]" )
{
    fixture_t f;
    const u64 a = f.Box( 0 ), b = f.Box( 128 ), c = f.Box( 256 );
    const u64 inner = f.Group( { a, b } ), outer = f.Group( { inner, c } );
    const u64 gone[]{ a, b };
    REQUIRE( MapEdit_Delete( &f.map, { gone, 2 } ) == map_status_t::OK );
    bool changed = false;
    REQUIRE( MapGroups_Prune( &f.map, &changed ) == map_status_t::OK ); CHECK( changed );
    CHECK( MapGroups_All( &f.map ) == std::vector<u64>{ outer } );
    CHECK( f.Members( outer ) == std::vector<u64>{ c } );
    REQUIRE( MapGroups_Prune( &f.map, &changed ) == map_status_t::OK ); CHECK_FALSE( changed );
}

TEST_CASE( "Duplicating grouped objects copies their groups; a copy made inside a group stays in it", "[map][groups]" )
{
    fixture_t f;
    const u64 a = f.Box( 0 ), b = f.Box( 128 ), c = f.Box( 256 );
    const u64 inner = f.Group( { a, b }, "pillars" ), outer = f.Group( { inner, c }, "hall" );
    vector_t<u64> copies{}; REQUIRE( Vector_Init( &copies, Allocator_GetSystem() ) );
    // The whole inner group: a copy of it, named the same, in the outer group.
    const u64 pair[]{ a, b };
    REQUIRE( MapEdit_Duplicate( &f.map, { pair, 2 }, { 0, 512, 0 }, &copies ) == map_status_t::OK );
    REQUIRE( copies.nCount == 2 );
    const u64 copyGroup = MapGroups_Parent( &f.map, copies.pData[0] );
    REQUIRE( copyGroup != 0 ); CHECK( copyGroup != inner );
    CHECK( MapGroups_Parent( &f.map, copies.pData[1] ) == copyGroup );
    CHECK( MapGroups_Name( &f.map, copyGroup ) == "pillars" );
    CHECK( MapGroups_Parent( &f.map, copyGroup ) == outer );
    // One object of a group: its copy joins that same group.
    REQUIRE( MapEdit_Duplicate( &f.map, { &c, 1 }, { 0, 1024, 0 }, &copies ) == map_status_t::OK );
    CHECK( MapGroups_Parent( &f.map, copies.pData[0] ) == outer );
    // Everything in the outer group: a whole new nested copy, top level.
    const auto leaves = f.Leaves( outer );
    const usize groupsBefore = MapGroups_All( &f.map ).size();
    REQUIRE( MapEdit_Duplicate( &f.map, { leaves.data(), leaves.size() }, { 2048, 0, 0 }, &copies ) == map_status_t::OK );
    CHECK( MapGroups_All( &f.map ).size() == groupsBefore + 3 ); // Outer, and both pillar groups.
    const u64 copyOuter = MapDocument_GroupOf( &f.map, copies.pData[0] );
    REQUIRE( copyOuter != 0 ); CHECK( copyOuter != outer ); CHECK( MapGroups_Parent( &f.map, copyOuter ) == 0 );
    CHECK( MapGroups_Name( &f.map, copyOuter ) == "hall" );
    CHECK( f.Leaves( copyOuter ).size() == leaves.size() );
    Vector_Shutdown( &copies );
}

TEST_CASE( "Copy and paste carries whole groups, nested ones included; a partial copy pastes loose", "[map][groups][clipboard]" )
{
    fixture_t f;
    const u64 a = f.Box( 0 ), b = f.Box( 128 ), c = f.Box( 256 );
    const u64 inner = f.Group( { a, b }, "pillars" ), outer = f.Group( { inner, c }, "hall" );
    ( void )outer;
    text_buffer_t text{}; REQUIRE( TextBuffer_Init( &text, Allocator_GetSystem() ) );
    const u64 all[]{ a, b, c };
    REQUIRE( MapClipboard_WriteSelection( &f.map, { all, 3 }, &text ) == map_status_t::OK );
    vector_t<u64> pasted{}; REQUIRE( Vector_Init( &pasted, Allocator_GetSystem() ) );
    const usize groupsBefore = MapGroups_All( &f.map ).size();
    REQUIRE( MapClipboard_Paste( &f.map, TextBuffer_View( &text ), { 0, 512, 0 }, &pasted ) == map_status_t::OK );
    REQUIRE( pasted.nCount == 3 );
    CHECK( MapGroups_All( &f.map ).size() == groupsBefore + 2 );
    const u64 pastedOuter = MapDocument_GroupOf( &f.map, pasted.pData[0] );
    REQUIRE( pastedOuter != 0 ); CHECK( pastedOuter != outer );
    CHECK( MapGroups_Name( &f.map, pastedOuter ) == "hall" );
    const u64 pastedInner = MapGroups_Parent( &f.map, pasted.pData[0] );
    CHECK( MapGroups_Name( &f.map, pastedInner ) == "pillars" ); CHECK( MapGroups_Parent( &f.map, pastedInner ) == pastedOuter );
    // One member alone pastes as a loose object.
    TextBuffer_Clear( &text );
    REQUIRE( MapClipboard_WriteSelection( &f.map, { &a, 1 }, &text ) == map_status_t::OK );
    REQUIRE( MapClipboard_Paste( &f.map, TextBuffer_View( &text ), { 0, 1024, 0 }, &pasted ) == map_status_t::OK );
    CHECK( MapDocument_GroupOf( &f.map, pasted.pData[0] ) == 0 );
    Vector_Shutdown( &pasted ); TextBuffer_Shutdown( &text );
}
