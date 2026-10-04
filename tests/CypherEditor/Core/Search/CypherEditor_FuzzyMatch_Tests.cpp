//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditor_FuzzyMatch_Tests.cpp
//  Purpose: Contract tests for the editor's fuzzy matcher.
//
//  History:
//  - Created by Karlo Siric on 2026-09-29
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditor_FuzzyMatch.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace cypher::common;
using namespace cypher::editor;

namespace
{

i32 Score( const char *pText, const char *pQuery )
{
    return EditorFuzzy_Score( StringView_FromCString( pText ), StringView_FromCString( pQuery ) );
}

} // namespace

TEST_CASE( "Fuzzy matching requires the query's characters in order", "[editor][core][fuzzy]" )
{
    CHECK( Score( "Show Grid", "" ) == 0 );
    CHECK( Score( "Show Grid", "sg" ) > 0 );
    CHECK( Score( "Show Grid", "SG" ) == Score( "Show Grid", "sg" ) ); // Case does not matter.
    CHECK( Score( "Show Grid", "gs" ) == EDITOR_FUZZY_NO_MATCH );
    CHECK( Score( "Show Grid", "grids" ) == EDITOR_FUZZY_NO_MATCH );
    CHECK( Score( "", "a" ) == EDITOR_FUZZY_NO_MATCH );
}

TEST_CASE( "Fuzzy scores favour word starts, runs, and the beginning", "[editor][core][fuzzy]" )
{
    // Word starts beat letters inside words.
    CHECK( Score( "Grid Snap", "gs" ) > Score( "Paging Sets", "gs" ) );
    // A run of letters beats scattered ones.
    CHECK( Score( "Save As", "save" ) > Score( "Select All Vertex Edges", "save" ) );
    // Matching at the start beats matching later.
    CHECK( Score( "Undo", "un" ) > Score( "Run Undo", "un" ) );
    // Camel case counts as word starts.
    CHECK( Score( "centerSelection", "cs" ) > Score( "focusInViews", "cs" ) );
    // The programme finds the best placement, not the first one: in
    // "Paging Sets Grid Snap" the g and s of "Grid Snap" win.
    u32 matched[2]{};
    REQUIRE( EditorFuzzy_Score( StringView_FromCString( "Paging Sets Grid Snap" ), StringView_FromCString( "gs" ), matched ) > 0 );
    CHECK( matched[0] == 12u );
    CHECK( matched[1] == 17u );
}

TEST_CASE( "Command IDs and long texts match too", "[editor][core][fuzzy]" )
{
    CHECK( Score( "map.view.center_selection_2d", "cs2" ) > 0 );
    CHECK( Score( "map.view.center_selection_2d", "mvc" ) > Score( "map.visgroup.create", "mvc" ) );
    // Beyond the programme's tables the greedy pass still decides matching.
    std::string longText( 700u, 'x' );
    longText += "needle";
    CHECK( EditorFuzzy_Score( StringView_FromCString( longText.c_str() ), StringView_FromCString( "needle" ) ) > 0 );
    CHECK( EditorFuzzy_Score( StringView_FromCString( longText.c_str() ), StringView_FromCString( "needles" ) ) == EDITOR_FUZZY_NO_MATCH );
}
