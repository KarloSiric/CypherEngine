//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_StringPath_Tests.cpp
//  Purpose: Tests allocation-free lexical path manipulation.
//  Details: The suite covers roots, borrowed components, dot resolution, traversal
//           rejection, separator policies, truncation, joins, and relative paths.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_StringPath.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "StringPath recognizes platform-independent lexical roots",
           "[CypherCommon][Tier1][StringPath]" )
{
    REQUIRE( StringPath_IsAbsolute(
        StringView_FromCString( "/assets/file" ),
        path_style_t::POSIX ) );
    REQUIRE( StringPath_IsAbsolute(
        StringView_FromCString( "C:\\assets\\file" ),
        path_style_t::WINDOWS ) );
    REQUIRE_FALSE( StringPath_IsAbsolute(
        StringView_FromCString( "C:assets\\file" ),
        path_style_t::WINDOWS ) );
    REQUIRE( StringPath_IsAbsolute(
        StringView_FromCString( "C:assets/file" ),
        path_style_t::VIRTUAL ) );
    REQUIRE( StringPath_HasRootName(
        StringView_FromCString( "\\\\server\\share\\asset" ),
        path_style_t::WINDOWS ) );
    REQUIRE( StringView_Equals(
        StringPath_RootName(
            StringView_FromCString( "\\\\server\\share\\asset" ),
            path_style_t::WINDOWS ),
        StringView_FromCString( "\\\\server\\share" ) ) );
}

TEST_CASE( "StringPath returns borrowed filename components",
           "[CypherCommon][Tier1][StringPath]" )
{
    const string_view_t path = StringView_FromCString( "materials/facility/wall.cymat" );
    REQUIRE( StringView_Equals(
        StringPath_Parent( path ),
        StringView_FromCString( "materials/facility" ) ) );
    REQUIRE( StringView_Equals(
        StringPath_FileName( path ),
        StringView_FromCString( "wall.cymat" ) ) );
    REQUIRE( StringView_Equals(
        StringPath_Stem( path ),
        StringView_FromCString( "wall" ) ) );
    REQUIRE( StringView_Equals(
        StringPath_Extension( path ),
        StringView_FromCString( ".cymat" ) ) );
    REQUIRE( StringPath_HasExtension( path, StringView_FromCString( "CYMAT" ), CY_TRUE ) );
    REQUIRE( StringPath_Extension( StringView_FromCString( ".editorconfig" ) ).cchLength == 0u );
    REQUIRE( StringPath_Extension( StringView_FromCString( ".." ) ).cchLength == 0u );
    REQUIRE( StringPath_FileName( StringView_FromCString( "materials/" ) ).cchLength == 0u );
}

TEST_CASE( "StringPath normalizes virtual asset paths deterministically",
           "[CypherCommon][Tier1][StringPath]" )
{
    constexpr flags32_t flags =
        PATH_NORMALIZE_FLAG_COLLAPSE_SEPARATORS |
        PATH_NORMALIZE_FLAG_RESOLVE_DOT |
        PATH_NORMALIZE_FLAG_RESOLVE_DOT_DOT |
        PATH_NORMALIZE_FLAG_LOWERCASE_ASCII |
        PATH_NORMALIZE_FLAG_REJECT_ABSOLUTE |
        PATH_NORMALIZE_FLAG_REJECT_ABOVE_ROOT;

    char output[128]{};
    const path_write_result_t result = StringPath_Normalize(
        StringView_FromCString( "Materials\\Facility//./Props/../WALL.CYMAT" ),
        path_style_t::VIRTUAL,
        flags,
        output,
        sizeof( output ) );
    REQUIRE( result.status == path_status_t::OK );
    REQUIRE( StringView_Equals(
        StringView_FromCString( output ),
        StringView_FromCString( "materials/facility/wall.cymat" ) ) );

    REQUIRE( StringPath_Normalize(
        StringView_FromCString( "../secret" ),
        path_style_t::VIRTUAL,
        flags,
        output,
        sizeof( output ) ).status == path_status_t::ABOVE_ROOT );
    REQUIRE( StringPath_Normalize(
        StringView_FromCString( "/absolute" ),
        path_style_t::VIRTUAL,
        flags,
        output,
        sizeof( output ) ).status == path_status_t::ABSOLUTE_PATH_REJECTED );
    REQUIRE( StringPath_Normalize(
        StringView_FromCString( "C:\\absolute" ),
        path_style_t::VIRTUAL,
        flags,
        output,
        sizeof( output ) ).status == path_status_t::ABSOLUTE_PATH_REJECTED );
}

TEST_CASE( "StringPath preserves requested separator and trailing policies",
           "[CypherCommon][Tier1][StringPath]" )
{
    char output[128]{};
    const path_write_result_t result = StringPath_Normalize(
        StringView_FromCString( "C:/Project//Assets/" ),
        path_style_t::WINDOWS,
        PATH_NORMALIZE_FLAG_KEEP_TRAILING_SLASH,
        output,
        sizeof( output ) );
    REQUIRE( result.status == path_status_t::OK );
    REQUIRE( StringView_Equals(
        StringView_FromCString( output ),
        StringView_FromCString( "C:\\Project\\\\Assets\\" ) ) );

    REQUIRE( StringPath_Normalize(
        StringView_FromCString( "/a/../../b" ),
        path_style_t::POSIX,
        PATH_NORMALIZE_FLAG_RESOLVE_DOT_DOT,
        output,
        sizeof( output ) ).status == path_status_t::OK );
    REQUIRE( StringView_Equals(
        StringView_FromCString( output ),
        StringView_FromCString( "/b" ) ) );
}

TEST_CASE( "StringPath writers report required size and remain terminated",
           "[CypherCommon][Tier1][StringPath]" )
{
    const path_write_result_t measured = StringPath_Join(
        StringView_FromCString( "materials" ),
        StringView_FromCString( "wall.cymat" ),
        path_style_t::VIRTUAL,
        nullptr,
        0u );
    REQUIRE( measured.status == path_status_t::OUTPUT_TRUNCATED );
    REQUIRE( measured.cchRequired == 20u );

    char output[8]{};
    const path_write_result_t truncated = StringPath_Join(
        StringView_FromCString( "materials" ),
        StringView_FromCString( "wall.cymat" ),
        path_style_t::VIRTUAL,
        output,
        sizeof( output ) );
    REQUIRE( truncated.status == path_status_t::OUTPUT_TRUNCATED );
    REQUIRE( output[sizeof( output ) - 1u] == '\0' );
    REQUIRE( truncated.cchRequired == measured.cchRequired );
}

TEST_CASE( "StringPath replaces extensions and constructs relative paths",
           "[CypherCommon][Tier1][StringPath]" )
{
    char output[128]{};
    REQUIRE( StringPath_ReplaceExtension(
        StringView_FromCString( "maps/facility.cymap" ),
        StringView_FromCString( "cymap_c" ),
        output,
        sizeof( output ) ).status == path_status_t::OK );
    REQUIRE( StringView_Equals(
        StringView_FromCString( output ),
        StringView_FromCString( "maps/facility.cymap_c" ) ) );

    REQUIRE( StringPath_ReplaceExtension(
        StringView_FromCString( "maps\\facility.cymap" ),
        StringView_FromCString( ".cymap_c" ),
        output,
        sizeof( output ) ).status == path_status_t::OK );
    REQUIRE( StringView_Equals(
        StringView_FromCString( output ),
        StringView_FromCString( "maps\\facility.cymap_c" ) ) );

    REQUIRE( StringPath_RemoveExtension(
        StringView_FromCString( "maps/facility.cymap" ),
        output,
        sizeof( output ) ).status == path_status_t::OK );
    REQUIRE( StringView_Equals(
        StringView_FromCString( output ),
        StringView_FromCString( "maps/facility" ) ) );

    REQUIRE( StringPath_MakeRelative(
        StringView_FromCString( "/project/assets/materials/wall.cymat" ),
        StringView_FromCString( "/project/assets/maps" ),
        path_style_t::POSIX,
        CY_FALSE,
        output,
        sizeof( output ) ).status == path_status_t::OK );
    REQUIRE( StringView_Equals(
        StringView_FromCString( output ),
        StringView_FromCString( "../materials/wall.cymat" ) ) );
}
