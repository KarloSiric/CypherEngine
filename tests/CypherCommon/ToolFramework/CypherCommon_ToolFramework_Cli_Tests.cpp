//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/ToolFramework/CypherCommon_ToolFramework_Cli_Tests.cpp
//  Purpose: Verifies descriptor-driven CLI parsing, help, and runner contracts.
//  Details: Tests long and short options, Boolean negation, repeated values,
//           positional cardinality, generated help, and runner validation.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ToolFramework.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string_view>

using namespace cypher::common;

namespace
{

struct cli_fixture_t {
    tool_option_desc_t options[3]{};
    tool_command_desc_t command{};
    tool_application_desc_t application{};

    cli_fixture_t() noexcept
    {
        options[0] = {
            StringView_FromCString( "force" ),
            'f',
            tool_option_type_t::BOOLEAN,
            {},
            StringView_FromCString( "Rebuilds outputs even when current." ),
            StringView_FromCString( "false" ),
            nullptr,
            0u,
            TOOL_OPTION_FLAG_NONE
        };
        options[1] = {
            StringView_FromCString( "define" ),
            'D',
            tool_option_type_t::STRING,
            StringView_FromCString( "NAME=VALUE" ),
            StringView_FromCString( "Adds one compiler definition." ),
            {},
            nullptr,
            0u,
            TOOL_OPTION_FLAG_REPEATABLE
        };
        options[2] = {
            StringView_FromCString( "jobs" ),
            'j',
            tool_option_type_t::U64,
            StringView_FromCString( "COUNT" ),
            StringView_FromCString( "Sets the worker count." ),
            {},
            nullptr,
            0u,
            TOOL_OPTION_FLAG_REQUIRED
        };
        command = {
            StringView_FromCString( "compile" ),
            StringView_FromCString( "Compiles authored resources." ),
            StringView_FromCString( "compile [options] <inputs...>" ),
            options,
            3u,
            TOOL_COMMAND_FLAG_ACCEPTS_INPUTS |
                TOOL_COMMAND_FLAG_ALLOW_MULTIPLE_INPUTS,
            StringView_FromCString(
                "Validates inputs and publishes outputs transactionally." )
        };
        application = {
            StringView_FromCString( "cypher-resource-compiler" ),
            StringView_FromCString( "CypherResourceCompiler" ),
            StringView_FromCString( "Compiles Cypher resources." ),
            tool_delivery_t::COMMAND_LINE,
            1u,
            TOOL_APPLICATION_FLAG_HEADLESS
        };
    }
};

tool_status_t NoopExecute(
    const tool_cli_parse_result_t &,
    const tool_host_t &,
    const tool_output_policy_t &,
    void * ) noexcept
{
    return tool_status_t::OK;
}

} // namespace

TEST_CASE( "CLI parser resolves typed repeated options and positional inputs",
           "[CypherCommon][ToolFramework][CLI]" )
{
    cli_fixture_t fixture{};
    const string_view_t arguments[]{
        StringView_FromCString( "compile" ),
        StringView_FromCString( "--force" ),
        StringView_FromCString( "--define=DEBUG=1" ),
        StringView_FromCString( "-DTRACE=1" ),
        StringView_FromCString( "-j4" ),
        StringView_FromCString( "one.cymat" ),
        StringView_FromCString( "two.cyshader" )
    };
    tool_option_value_t optionStorage[8]{};
    string_view_t inputStorage[4]{};
    tool_cli_parse_result_t result{};
    tool_cli_parse_error_t error{};

    REQUIRE( ToolCliArgumentParser_InitResult(
                 &result,
                 optionStorage,
                 8u,
                 inputStorage,
                 4u ) == tool_status_t::OK );
    REQUIRE( ToolCliArgumentParser_Parse(
                 Span_FromArray( arguments ),
                 &fixture.command,
                 1u,
                 &result,
                 &error ) == tool_status_t::OK );
    REQUIRE( result.action == tool_cli_parse_action_t::EXECUTE );
    REQUIRE( result.pCommand == &fixture.command );
    REQUIRE( result.nInputs == 2u );
    CHECK( StringView_Equals(
        result.pInputs[1],
        StringView_FromCString( "two.cyshader" ) ) );
    REQUIRE( ToolOptionSet_CountValues(
                 &result.options,
                 StringView_FromCString( "define" ) ) == 2u );
    CHECK( StringView_Equals(
        ToolOptionSet_Find( &result.options, StringView_FromCString( "jobs" ) )->value,
        StringView_FromCString( "4" ) ) );
}

TEST_CASE( "CLI parser handles Boolean negation and explicit option termination",
           "[CypherCommon][ToolFramework][CLI]" )
{
    cli_fixture_t fixture{};
    const string_view_t arguments[]{
        StringView_FromCString( "compile" ),
        StringView_FromCString( "--no-force" ),
        StringView_FromCString( "--jobs" ),
        StringView_FromCString( "2" ),
        StringView_FromCString( "--" ),
        StringView_FromCString( "--literal-input" )
    };
    tool_option_value_t optionStorage[6]{};
    string_view_t inputStorage[2]{};
    tool_cli_parse_result_t result{};
    REQUIRE( ToolCliArgumentParser_InitResult(
                 &result,
                 optionStorage,
                 6u,
                 inputStorage,
                 2u ) == tool_status_t::OK );
    REQUIRE( ToolCliArgumentParser_Parse(
                 Span_FromArray( arguments ),
                 &fixture.command,
                 1u,
                 &result,
                 nullptr ) == tool_status_t::OK );
    REQUIRE( StringView_Equals(
        ToolOptionSet_Find( &result.options, fixture.options[0].name )->value,
        StringView_FromCString( "false" ) ) );
    REQUIRE( result.nInputs == 1u );
    CHECK( StringView_Equals(
        result.pInputs[0],
        StringView_FromCString( "--literal-input" ) ) );
}

TEST_CASE( "CLI parser reports missing required options",
           "[CypherCommon][ToolFramework][CLI]" )
{
    cli_fixture_t fixture{};
    const string_view_t arguments[]{ StringView_FromCString( "compile" ) };
    tool_option_value_t optionStorage[4]{};
    string_view_t inputStorage[1]{};
    tool_cli_parse_result_t result{};
    tool_cli_parse_error_t error{};
    REQUIRE( ToolCliArgumentParser_InitResult(
                 &result,
                 optionStorage,
                 4u,
                 inputStorage,
                 1u ) == tool_status_t::OK );
    REQUIRE( ToolCliArgumentParser_Parse(
                 Span_FromArray( arguments ),
                 &fixture.command,
                 1u,
                 &result,
                 &error ) == tool_status_t::INVALID_OPTION );
    CHECK( StringView_Equals( error.argument, fixture.options[2].name ) );
}

TEST_CASE( "CLI help is generated from the same command descriptors",
           "[CypherCommon][ToolFramework][CLI]" )
{
    cli_fixture_t fixture{};
    tool_cli_help_options_t options{};
    options.version = StringView_FromCString( "1.0.0" );
    options.epilogue = StringView_FromCString(
        "Run '<command> --help' for command-specific options." );
    const tool_cli_help_result_t measured = ToolCliHelp_WriteCommand(
        fixture.application,
        fixture.command,
        options,
        nullptr,
        0u );
    REQUIRE( measured.status == tool_status_t::CAPACITY_EXCEEDED );

    std::array<char, 4096> output{};
    const tool_cli_help_result_t written = ToolCliHelp_WriteCommand(
        fixture.application,
        fixture.command,
        options,
        output.data(),
        output.size() );
    REQUIRE( written.status == tool_status_t::OK );
    const std::string_view text{ output.data(), written.cchWritten };
    CHECK( text.find( "CypherResourceCompiler compile" ) !=
           std::string_view::npos );
    CHECK( text.find( "publishes outputs transactionally" ) !=
           std::string_view::npos );
    CHECK( text.find( "-j, --jobs COUNT" ) != std::string_view::npos );
    CHECK( text.find( "(required)" ) != std::string_view::npos );

    const tool_cli_help_result_t measuredApplication =
        ToolCliHelp_WriteApplication(
            fixture.application,
            &fixture.command,
            1u,
            options,
            nullptr,
            0u );
    REQUIRE( measuredApplication.status == tool_status_t::CAPACITY_EXCEEDED );
    std::array<char, 4096> applicationOutput{};
    const tool_cli_help_result_t writtenApplication =
        ToolCliHelp_WriteApplication(
            fixture.application,
            &fixture.command,
            1u,
            options,
            applicationOutput.data(),
            applicationOutput.size() );
    REQUIRE( writtenApplication.status == tool_status_t::OK );
    const std::string_view applicationText{
        applicationOutput.data(),
        writtenApplication.cchWritten
    };
    CHECK( applicationText.find( "CypherResourceCompiler  1.0.0" ) !=
           std::string_view::npos );
    CHECK( applicationText.find( "GLOBAL OPTIONS" ) !=
           std::string_view::npos );
    CHECK( applicationText.find( "command-specific options" ) !=
           std::string_view::npos );
}

TEST_CASE( "CLI help emits ANSI only when the presentation requests it",
           "[CypherCommon][ToolFramework][CLI]" )
{
    cli_fixture_t fixture{};
    std::array<char, 4096> output{};
    tool_cli_help_options_t options{};
    options.bUseColor = CY_TRUE;
    const tool_cli_help_result_t written = ToolCliHelp_WriteCommand(
        fixture.application,
        fixture.command,
        options,
        output.data(),
        output.size() );
    REQUIRE( written.status == tool_status_t::OK );
    const std::string_view text{ output.data(), written.cchWritten };
    CHECK( text.find( "\x1b[36m" ) != std::string_view::npos );
    CHECK( text.find( "\x1b[0m" ) != std::string_view::npos );
}

TEST_CASE( "CLI runner validates product boundaries before touching the process",
           "[CypherCommon][ToolFramework][CLI]" )
{
    cli_fixture_t fixture{};
    tool_cli_run_desc_t desc{
        &fixture.application,
        &fixture.command,
        1u,
        StringView_FromCString( "0.1.0" ),
        nullptr,
        &NoopExecute,
        nullptr,
        nullptr
    };
    REQUIRE( ToolCliRunner_Validate( desc, {} ) == tool_status_t::OK );

    tool_application_desc_t invalidApplication = fixture.application;
    invalidApplication.delivery = tool_delivery_t::GUI;
    desc.pApplication = &invalidApplication;
    REQUIRE( ToolCliRunner_Validate( desc, {} ) ==
             tool_status_t::INVALID_CONFIGURATION );
}
