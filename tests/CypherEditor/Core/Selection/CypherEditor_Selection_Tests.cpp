//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditor_Selection_Tests.cpp
//  Purpose: Contract tests for the sorted ID selection.
//
//  History:
//  - Created by Karlo Siric on 2026-09-29
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditor_Selection.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;
using namespace cypher::editor;

namespace
{

bool_t IsEven( void *, u64 id ) noexcept
{
    return ( id % 2u ) == 0u;
}

} // namespace

TEST_CASE( "Selection modes keep a sorted set and report changes", "[editor][core][selection]" )
{
    editor_selection_t selection{};
    REQUIRE( EditorSelection_Init( &selection, Allocator_GetSystem() ) );
    CHECK( EditorSelection_Apply( &selection, 30u, EDITOR_SELECT_REPLACE ) );
    CHECK( EditorSelection_Apply( &selection, 10u, EDITOR_SELECT_ADD ) );
    CHECK( EditorSelection_Apply( &selection, 20u, EDITOR_SELECT_TOGGLE ) );
    REQUIRE( EditorSelection_Count( &selection ) == 3u );
    CHECK( EditorSelection_At( &selection, 0u ) == 10u );
    CHECK( EditorSelection_At( &selection, 2u ) == 30u );
    const u64 revision = selection.revision;

    CHECK_FALSE( EditorSelection_Apply( &selection, 10u, EDITOR_SELECT_ADD ) );
    CHECK_FALSE( EditorSelection_Apply( &selection, 99u, EDITOR_SELECT_REMOVE ) );
    CHECK_FALSE( EditorSelection_Apply( &selection, 0u, EDITOR_SELECT_TOGGLE ) );
    CHECK( selection.revision == revision ); // No change, no new revision.

    CHECK( EditorSelection_Apply( &selection, 20u, EDITOR_SELECT_TOGGLE ) );
    CHECK_FALSE( EditorSelection_Contains( &selection, 20u ) );
    CHECK( EditorSelection_Apply( &selection, 30u, EDITOR_SELECT_REMOVE ) );
    CHECK( EditorSelection_Apply( &selection, 10u, EDITOR_SELECT_REPLACE ) == CY_FALSE ); // Already only 10.
    CHECK( EditorSelection_Apply( &selection, 0u, EDITOR_SELECT_REPLACE ) );
    CHECK( EditorSelection_Count( &selection ) == 0u );
    CHECK_FALSE( EditorSelection_Clear( &selection ) );
    EditorSelection_Shutdown( &selection );
}

TEST_CASE( "Setting a list sorts, removes repeats and zero, and prunes", "[editor][core][selection]" )
{
    editor_selection_t selection{};
    REQUIRE( EditorSelection_Init( &selection, Allocator_GetSystem() ) );
    const u64 ids[]{ 7u, 3u, 7u, 0u, 4u, 8u };
    CHECK( EditorSelection_Set( &selection, ids, std::size( ids ) ) );
    REQUIRE( EditorSelection_Count( &selection ) == 4u );
    CHECK( EditorSelection_At( &selection, 0u ) == 3u );
    CHECK( EditorSelection_At( &selection, 3u ) == 8u );
    CHECK_FALSE( EditorSelection_Set( &selection, ids, std::size( ids ) ) ); // Same set.

    CHECK( EditorSelection_Prune( &selection, &IsEven, nullptr ) );
    REQUIRE( EditorSelection_Count( &selection ) == 2u );
    CHECK( EditorSelection_At( &selection, 0u ) == 4u );
    CHECK_FALSE( EditorSelection_Prune( &selection, &IsEven, nullptr ) );
    EditorSelection_Shutdown( &selection );
}
