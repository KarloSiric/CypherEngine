//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_ConCommand_Tests.cpp
//  Purpose: Tests console-command descriptor and argument contracts.
//  Details: Covers bounded parsing, borrowed quoted arguments, malformed input,
//           descriptor policy, and transactional output on failure.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ConCommand.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace cypher::common;

namespace
{

error_code_t TestCommandCallback(
    const command_context_t &,
    const command_args_t &,
    void * ) noexcept
{
    return CY_ERROR_OK;
}

bool ViewEquals( string_view_t view, const char *pExpected )
{
    return StringView_Equals( view, StringView_FromCString( pExpected ) );
}

} // namespace

TEST_CASE( "ConCommand validates bounded ASCII command names", "[CypherCommon][Tier1][ConCommand]" )
{
    REQUIRE( ConCommand_IsValidName( StringView_FromCString( "map" ) ) );
    REQUIRE( ConCommand_IsValidName( StringView_FromCString( "sv_cheats" ) ) );
    REQUIRE( ConCommand_IsValidName( StringView_FromCString( "render.wire-frame" ) ) );
    REQUIRE( ConCommand_IsValidName( StringView_FromCString( "_internal" ) ) );

    REQUIRE_FALSE( ConCommand_IsValidName( {} ) );
    REQUIRE_FALSE( ConCommand_IsValidName( StringView_FromCString( "2map" ) ) );
    REQUIRE_FALSE( ConCommand_IsValidName( StringView_FromCString( "bad name" ) ) );
    REQUIRE_FALSE( ConCommand_IsValidName( StringView_FromCString( "bad/name" ) ) );
    REQUIRE_FALSE( ConCommand_IsValidName( { nullptr, 1u } ) );

    std::string oversized( CY_COMMAND_MAX_NAME_BYTES + 1u, 'a' );
    REQUIRE_FALSE( ConCommand_IsValidName(
        StringView_FromRange( oversized.data(), oversized.size() ) ) );
}

TEST_CASE( "ConCommand validates descriptor callbacks flags and metadata", "[CypherCommon][Tier1][ConCommand]" )
{
    concommand_desc_t desc{};
    desc.name = StringView_FromCString( "map" );
    desc.help = StringView_FromCString( "Loads a map." );
    desc.usage = StringView_FromCString( "map <name>" );
    desc.pfnExecute = TestCommandCallback;
    REQUIRE( ConCommand_ValidateDesc( desc ) );

    desc.pfnExecute = nullptr;
    REQUIRE_FALSE( ConCommand_ValidateDesc( desc ) );
    desc.pfnExecute = TestCommandCallback;

    desc.flags = CONCOMMAND_FLAG_SERVER_ONLY | CONCOMMAND_FLAG_CLIENT_ONLY;
    REQUIRE_FALSE( ConCommand_ValidateDesc( desc ) );
    desc.flags = CYPHER_BIT32( 31 );
    REQUIRE_FALSE( ConCommand_ValidateDesc( desc ) );

    const char helpWithNull[] = { 'b', 'a', 'd', '\0', 'x' };
    desc.flags = CONCOMMAND_FLAG_NONE;
    desc.help = StringView_FromRange( helpWithNull, sizeof( helpWithNull ) );
    REQUIRE_FALSE( ConCommand_ValidateDesc( desc ) );
}

TEST_CASE( "ConCommand parses unquoted and quoted borrowed arguments", "[CypherCommon][Tier1][ConCommand]" )
{
    const string_view_t line = StringView_FromCString(
        "  map  \"facility sector\" 'night mode' \"\"  " );
    command_args_t args{};
    const command_parse_result_t result = ConCommand_ParseArgs( line, &args );

    REQUIRE( ConCommand_ParseSucceeded( result ) );
    REQUIRE( result.iError == CY_STRING_VIEW_NPOS );
    REQUIRE( args.nArgumentCount == 4u );
    REQUIRE( ViewEquals( args.arguments[0], "map" ) );
    REQUIRE( ViewEquals( args.arguments[1], "facility sector" ) );
    REQUIRE( ViewEquals( args.arguments[2], "night mode" ) );
    REQUIRE( args.arguments[3].cchLength == 0u );
    REQUIRE( args.commandLine.pData == line.pData );
}

TEST_CASE( "ConCommand reports malformed input at a stable byte offset", "[CypherCommon][Tier1][ConCommand]" )
{
    command_args_t args{};

    command_parse_result_t result = ConCommand_ParseArgs(
        StringView_FromCString( "map \"unfinished" ),
        &args );
    REQUIRE( result.status == command_parse_status_t::UNTERMINATED_QUOTE );
    REQUIRE( result.iError == 4u );

    result = ConCommand_ParseArgs(
        StringView_FromCString( "map \"value\"tail" ),
        &args );
    REQUIRE( result.status == command_parse_status_t::TRAILING_BYTES_AFTER_QUOTE );
    REQUIRE( result.iError == 11u );

    result = ConCommand_ParseArgs(
        StringView_FromCString( "map val\"ue" ),
        &args );
    REQUIRE( result.status == command_parse_status_t::UNEXPECTED_QUOTE );
    REQUIRE( result.iError == 7u );

    result = ConCommand_ParseArgs(
        StringView_FromCString( "map\nnext" ),
        &args );
    REQUIRE( result.status == command_parse_status_t::LINE_BREAK );
    REQUIRE( result.iError == 3u );
}

TEST_CASE( "ConCommand rejects empty oversized and embedded-null lines", "[CypherCommon][Tier1][ConCommand]" )
{
    command_args_t args{};

    REQUIRE( ConCommand_ParseArgs( {}, &args ).status ==
             command_parse_status_t::EMPTY_LINE );
    REQUIRE( ConCommand_ParseArgs( StringView_FromCString( "   \t" ), &args ).status ==
             command_parse_status_t::EMPTY_LINE );
    REQUIRE( ConCommand_ParseArgs( StringView_FromCString( "2map" ), &args ).status ==
             command_parse_status_t::INVALID_COMMAND_NAME );
    REQUIRE( ConCommand_ParseArgs( { nullptr, 1u }, &args ).status ==
             command_parse_status_t::INVALID_ARGUMENT );
    REQUIRE( ConCommand_ParseArgs( StringView_FromCString( "map" ), nullptr ).status ==
             command_parse_status_t::INVALID_ARGUMENT );

    std::string oversized( CY_COMMAND_MAX_LINE_BYTES + 1u, 'a' );
    REQUIRE( ConCommand_ParseArgs(
        StringView_FromRange( oversized.data(), oversized.size() ),
        &args ).status == command_parse_status_t::LINE_TOO_LONG );

    const char embeddedNull[] = { 'm', 'a', 'p', '\0', 'x' };
    REQUIRE( ConCommand_ParseArgs(
        StringView_FromRange( embeddedNull, sizeof( embeddedNull ) ),
        &args ).status == command_parse_status_t::EMBEDDED_NULL );
}

TEST_CASE( "ConCommand enforces the fixed argument limit transactionally", "[CypherCommon][Tier1][ConCommand]" )
{
    std::string line = "cmd";
    for ( usize iArgument = 1u; iArgument < CY_COMMAND_MAX_ARGUMENTS; ++iArgument ) {
        line += " x";
    }

    command_args_t args{};
    REQUIRE( ConCommand_ParseSucceeded( ConCommand_ParseArgs(
        StringView_FromRange( line.data(), line.size() ),
        &args ) ) );
    REQUIRE( args.nArgumentCount == CY_COMMAND_MAX_ARGUMENTS );

    line += " overflow";
    args.nArgumentCount = 7u;
    const command_parse_result_t result = ConCommand_ParseArgs(
        StringView_FromRange( line.data(), line.size() ),
        &args );
    REQUIRE( result.status == command_parse_status_t::TOO_MANY_ARGUMENTS );
    REQUIRE( args.nArgumentCount == 7u );
}

TEST_CASE( "ConCommand exposes stable parse status names", "[CypherCommon][Tier1][ConCommand]" )
{
    REQUIRE( std::string( ConCommand_ParseStatusName( command_parse_status_t::OK ) ) == "OK" );
    REQUIRE( std::string( ConCommand_ParseStatusName(
        static_cast<command_parse_status_t>( 0xFFu ) ) ) ==
        "UNKNOWN_COMMAND_PARSE_STATUS" );
}
