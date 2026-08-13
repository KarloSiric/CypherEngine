//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/ToolFramework/CypherCommon_ToolFramework_ResponseFile_Tests.cpp
//  Purpose: Verifies bounded response-file ownership and recursive expansion.
//  Details: Tests quoted arguments, comments, relative nested files, stable copied
//           text, cycle rejection, and source locations for malformed input.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ToolCliResponseFile.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using namespace cypher::common;

namespace
{

struct temporary_directory_t {
    std::filesystem::path path{};

    temporary_directory_t()
    {
        const auto suffix = std::chrono::steady_clock::now()
                                .time_since_epoch()
                                .count();
        path = std::filesystem::temp_directory_path() /
               ( "cypher_tool_response_" + std::to_string( suffix ) );
        std::filesystem::create_directories( path );
    }

    ~temporary_directory_t()
    {
        std::error_code error{};
        std::filesystem::remove_all( path, error );
    }
};

void WriteText( const std::filesystem::path &path, std::string_view text )
{
    std::filesystem::create_directories( path.parent_path() );
    std::ofstream stream{ path, std::ios::binary | std::ios::trunc };
    REQUIRE( stream.is_open() );
    stream.write( text.data(), static_cast<std::streamsize>( text.size() ) );
    REQUIRE( stream.good() );
}

} // namespace

TEST_CASE( "Response files retain copied arguments and resolve nested paths locally",
           "[CypherCommon][ToolFramework][ResponseFile]" )
{
    temporary_directory_t temporary{};
    const std::filesystem::path outer = temporary.path / "outer.rsp";
    const std::filesystem::path inner = temporary.path / "nested" / "inner.rsp";
    const std::filesystem::path deeper = temporary.path / "nested" / "deeper.rsp";
    WriteText(
        outer,
        "# root response\n--define outer\n@nested/inner.rsp\n" );
    WriteText(
        inner,
        "--define \"nested value\" // retained as one argument\n@deeper.rsp\n" );
    WriteText( deeper, "asset.cymat\n" );

    const std::string responseArgument = "@" + outer.string();
    const string_view_t input[]{ StringView_FromCString( responseArgument.c_str() ) };
    tool_cli_response_file_result_t result{};
    REQUIRE( ToolCliResponseFile_InitResult(
                 &result,
                 Allocator_GetSystem() ) == tool_status_t::OK );
    REQUIRE( ToolCliResponseFile_Expand(
                 Span_FromArray( input ),
                 {},
                 &result ) == tool_status_t::OK );

    REQUIRE( result.arguments.nCount == 5u );
    CHECK( StringView_Equals(
        result.arguments.pData[0],
        StringView_FromCString( "--define" ) ) );
    CHECK( StringView_Equals(
        result.arguments.pData[1],
        StringView_FromCString( "outer" ) ) );
    CHECK( StringView_Equals(
        result.arguments.pData[3],
        StringView_FromCString( "nested value" ) ) );
    CHECK( StringView_Equals(
        result.arguments.pData[4],
        StringView_FromCString( "asset.cymat" ) ) );

    std::filesystem::remove_all( temporary.path );
    CHECK( StringView_Equals(
        result.arguments.pData[3],
        StringView_FromCString( "nested value" ) ) );
    ToolCliResponseFile_ShutdownResult( &result );
}

TEST_CASE( "Response files reject recursive inclusion cycles",
           "[CypherCommon][ToolFramework][ResponseFile]" )
{
    temporary_directory_t temporary{};
    const std::filesystem::path cycle = temporary.path / "cycle.rsp";
    WriteText( cycle, "@cycle.rsp\n" );

    const std::string responseArgument = "@" + cycle.string();
    const string_view_t input[]{ StringView_FromCString( responseArgument.c_str() ) };
    tool_cli_response_file_result_t result{};
    REQUIRE( ToolCliResponseFile_InitResult(
                 &result,
                 Allocator_GetSystem() ) == tool_status_t::OK );
    REQUIRE( ToolCliResponseFile_Expand(
                 Span_FromArray( input ),
                 {},
                 &result ) == tool_status_t::INVALID_CONFIGURATION );
    CHECK( result.nErrorLine == 1u );
    CHECK( result.nErrorColumn == 1u );
    ToolCliResponseFile_ShutdownResult( &result );
}

TEST_CASE( "Response files report unterminated quote locations",
           "[CypherCommon][ToolFramework][ResponseFile]" )
{
    temporary_directory_t temporary{};
    const std::filesystem::path malformed = temporary.path / "malformed.rsp";
    WriteText( malformed, "--define \"unterminated\n" );

    const std::string responseArgument = "@" + malformed.string();
    const string_view_t input[]{ StringView_FromCString( responseArgument.c_str() ) };
    tool_cli_response_file_result_t result{};
    REQUIRE( ToolCliResponseFile_InitResult(
                 &result,
                 Allocator_GetSystem() ) == tool_status_t::OK );
    REQUIRE( ToolCliResponseFile_Expand(
                 Span_FromArray( input ),
                 {},
                 &result ) == tool_status_t::INVALID_CONFIGURATION );
    CHECK( result.nErrorLine == 1u );
    CHECK( result.nErrorColumn == 10u );
    REQUIRE_FALSE( TextBuffer_IsEmpty( &result.errorPath ) );
    ToolCliResponseFile_ShutdownResult( &result );
}
