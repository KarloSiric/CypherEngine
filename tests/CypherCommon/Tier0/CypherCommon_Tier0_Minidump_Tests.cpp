//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_Minidump_Tests.cpp
//  Purpose: Tests Tier0 portable diagnostic dump output.
//  Details: These checks protect path configuration, overflow rejection,
//           deterministic metadata, stack bounds, and I/O failure reporting.
//
//  History:
//  - Created by Karlo Siric on 2026-07-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Minidump.h"

#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

std::string PathToUtf8( const std::filesystem::path &path )
{
    const std::u8string utf8 = path.u8string();
    return {
        reinterpret_cast<const char *>( utf8.data() ),
        utf8.size()
    };
}

} // namespace

TEST_CASE( "Minidump writes deterministic portable diagnostic metadata", "[CypherCommon][Tier0][Minidump]" )
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "CypherCommon_Minidump_Test.txt";
    const std::string pathString = path.string();
    std::filesystem::remove( path );

    minidump_info_t info{};
    info.pApplicationName = "CypherTest";
    info.pVersion = "17";
    info.pOutputPath = pathString.c_str();
    info.pReason = "test failure";
    info.location = { "test.cpp", "TestFunction", 42u, 3u };
    info.maxFrames = 8u;

    REQUIRE( Cy_MinidumpWrite( info ) == minidump_result_t::Ok );

    std::ifstream file( path );
    REQUIRE( file.is_open() );
    const std::string contents{
        std::istreambuf_iterator<char>( file ),
        std::istreambuf_iterator<char>()
    };

    REQUIRE( contents.find( "format=cypher-text-dump" ) != std::string::npos );
    REQUIRE( contents.find( "application=CypherTest" ) != std::string::npos );
    REQUIRE( contents.find( "reason=test failure" ) != std::string::npos );
    REQUIRE( contents.find( "source_line=42" ) != std::string::npos );
    REQUIRE( contents.find( "stack_frame_count=" ) != std::string::npos );

    file.close();
    REQUIRE( std::filesystem::remove( path ) );
}

TEST_CASE( "Minidump accepts UTF-8 output paths", "[CypherCommon][Tier0][Minidump]" )
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        std::filesystem::path( u8"CypherCommon_Minidump_\u017D.txt" );
    const std::string pathString = PathToUtf8( path );
    std::filesystem::remove( path );

    minidump_info_t info{};
    info.pOutputPath = pathString.c_str();
    info.pReason = "UTF-8 path test";

    REQUIRE( Cy_MinidumpWrite( info ) == minidump_result_t::Ok );
    REQUIRE( std::filesystem::exists( path ) );
    REQUIRE( std::filesystem::remove( path ) );
}

TEST_CASE( "Minidump default path supports queries and bounded copies", "[CypherCommon][Tier0][Minidump]" )
{
    REQUIRE( Cy_MinidumpSetOutputPath( nullptr ) == minidump_result_t::Ok );

    const usize cchRequired = Cy_MinidumpGetOutputPath( nullptr, 0u );
    char szSmall[8] = {};
    REQUIRE( Cy_MinidumpGetOutputPath( szSmall, sizeof( szSmall ) ) == cchRequired );
    REQUIRE( cchRequired >= sizeof( szSmall ) );
    REQUIRE( szSmall[sizeof( szSmall ) - 1u] == '\0' );
}

TEST_CASE( "Minidump rejects path overflow without changing state", "[CypherCommon][Tier0][Minidump]" )
{
    REQUIRE( Cy_MinidumpSetOutputPath( "stable.txt" ) == minidump_result_t::Ok );

    std::string longPath( 1024u, 'x' );
    REQUIRE( Cy_MinidumpSetOutputPath( longPath.c_str() ) == minidump_result_t::PathTooLong );

    char szPath[64] = {};
    REQUIRE( Cy_MinidumpGetOutputPath( szPath, sizeof( szPath ) ) == 10u );
    REQUIRE( std::string( szPath ) == "stable.txt" );

    REQUIRE( Cy_MinidumpSetOutputPath( nullptr ) == minidump_result_t::Ok );
}

TEST_CASE( "Minidump reports output open failures", "[CypherCommon][Tier0][Minidump]" )
{
    const std::filesystem::path missingParent =
        std::filesystem::temp_directory_path() / "CypherMissingDumpDirectory" / "Cypher.crash.txt";
    const std::string missingPath = missingParent.string();
    std::filesystem::remove_all( missingParent.parent_path() );

    minidump_info_t info{};
    info.pOutputPath = missingPath.c_str();

    REQUIRE( Cy_MinidumpWrite( info ) == minidump_result_t::OpenFailed );
    REQUIRE( std::string( Cy_MinidumpResultName( minidump_result_t::OpenFailed ) ) == "OpenFailed" );
    REQUIRE( std::string( Cy_MinidumpResultName( minidump_result_t::Count ) ) == "Unknown" );
}
