//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/ToolFramework/CypherCommon_ToolFramework_Core_Tests.cpp
//  Purpose: Verifies the core frontend-neutral ToolFramework contracts.
//  Details: Tests status mapping, application registration, option precedence,
//           output-policy consistency, report writing, and session transitions.
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

tool_application_desc_t MakeApplication() noexcept
{
    return {
        StringView_FromCString( "cypher-test" ),
        StringView_FromCString( "CypherTest" ),
        StringView_FromCString( "Exercises shared tool contracts." ),
        tool_delivery_t::COMMAND_LINE,
        1u,
        TOOL_APPLICATION_FLAG_HEADLESS
    };
}

} // namespace

TEST_CASE( "Tool statuses map to stable process exit classes",
           "[CypherCommon][ToolFramework][Core]" )
{
    REQUIRE( ToolStatus_Succeeded( tool_status_t::OK ) );
    REQUIRE_FALSE( ToolStatus_Failed( tool_status_t::OK ) );
    REQUIRE( ToolStatus_ExitCode( tool_status_t::INVALID_OPTION ) ==
             tool_exit_code_t::USAGE_ERROR );
    REQUIRE( ToolStatus_ExitCode( tool_status_t::INVALID_PROJECT ) ==
             tool_exit_code_t::CONFIGURATION_ERROR );
    REQUIRE( ToolStatus_ExitCode( tool_status_t::IO_ERROR ) ==
             tool_exit_code_t::INFRASTRUCTURE_ERROR );
    REQUIRE( ToolStatus_ExitCode( tool_status_t::CANCELLED ) ==
             tool_exit_code_t::CANCELLED );
}

TEST_CASE( "Tool registry rejects duplicate stable application IDs",
           "[CypherCommon][ToolFramework][Core]" )
{
    const tool_application_desc_t application = MakeApplication();
    const tool_application_desc_t *storage[2]{};
    tool_registry_t registry{};

    REQUIRE( ToolRegistry_Init( &registry, storage, 2u ) == tool_status_t::OK );
    REQUIRE( ToolRegistry_Register( &registry, &application ) == tool_status_t::OK );
    REQUIRE( ToolRegistry_Register( &registry, &application ) ==
             tool_status_t::ALREADY_EXISTS );
    REQUIRE( ToolRegistry_Find(
                 &registry,
                 StringView_FromCString( "cypher-test" ) ) == &application );
    REQUIRE( ToolRegistry_At( &registry, 0u ) == &application );
}

TEST_CASE( "Repeatable options retain only values from the strongest source",
           "[CypherCommon][ToolFramework][Core]" )
{
    const tool_option_desc_t includeOption{
        StringView_FromCString( "include" ),
        'I',
        tool_option_type_t::PATH,
        StringView_FromCString( "PATH" ),
        StringView_FromCString( "Adds one source include path." ),
        {},
        nullptr,
        0u,
        TOOL_OPTION_FLAG_REPEATABLE
    };
    tool_option_value_t storage[8]{};
    tool_option_set_t options{};
    REQUIRE( ToolOptionSet_Init( &options, storage, 8u ) == tool_status_t::OK );

    REQUIRE( ToolOptionSet_Resolve(
                 &options,
                 &includeOption,
                 StringView_FromCString( "project/a" ),
                 tool_option_source_t::PROJECT ) == tool_status_t::OK );
    REQUIRE( ToolOptionSet_Resolve(
                 &options,
                 &includeOption,
                 StringView_FromCString( "project/b" ),
                 tool_option_source_t::PROJECT ) == tool_status_t::OK );
    REQUIRE( ToolOptionSet_CountValues(
                 &options,
                 includeOption.name ) == 2u );

    REQUIRE( ToolOptionSet_Resolve(
                 &options,
                 &includeOption,
                 StringView_FromCString( "command/a" ),
                 tool_option_source_t::COMMAND_LINE ) == tool_status_t::OK );
    REQUIRE( ToolOptionSet_Resolve(
                 &options,
                 &includeOption,
                 StringView_FromCString( "command/b" ),
                 tool_option_source_t::COMMAND_LINE ) == tool_status_t::OK );

    REQUIRE( ToolOptionSet_CountValues(
                 &options,
                 includeOption.name ) == 2u );
    const tool_option_value_t *pFirst = ToolOptionSet_FindAt(
        &options,
        includeOption.name,
        0u );
    const tool_option_value_t *pSecond = ToolOptionSet_FindAt(
        &options,
        includeOption.name,
        1u );
    REQUIRE( pFirst != nullptr );
    REQUIRE( pSecond != nullptr );
    CHECK( StringView_Equals(
        pFirst->value,
        StringView_FromCString( "command/a" ) ) );
    CHECK( StringView_Equals(
        pSecond->value,
        StringView_FromCString( "command/b" ) ) );
    CHECK( pFirst->nOccurrence == 1u );
    CHECK( pSecond->nOccurrence == 2u );
}

TEST_CASE( "JSON diagnostics require JSON or disabled progress",
           "[CypherCommon][ToolFramework][Core]" )
{
    tool_output_policy_t policy{};
    policy.diagnosticsFormat = tool_output_format_t::JSON;
    policy.progressMode = tool_progress_mode_t::AUTO;
    REQUIRE( ToolOutput_ValidatePolicy( policy ) ==
             tool_status_t::INVALID_CONFIGURATION );

    policy.progressMode = tool_progress_mode_t::JSON;
    REQUIRE( ToolOutput_ValidatePolicy( policy ) == tool_status_t::OK );

    policy.progressMode = tool_progress_mode_t::NONE;
    REQUIRE( ToolOutput_ValidatePolicy( policy ) == tool_status_t::OK );

    policy.diagnosticsFormat = tool_output_format_t::TEXT;
    policy.flags = TOOL_OUTPUT_FLAG_FORCE_COLOR;
    REQUIRE( ToolOutput_ValidatePolicy( policy ) ==
             tool_status_t::INVALID_CONFIGURATION );
    policy.flags = TOOL_OUTPUT_FLAG_COLOR | TOOL_OUTPUT_FLAG_FORCE_COLOR;
    REQUIRE( ToolOutput_ValidatePolicy( policy ) == tool_status_t::OK );
}

TEST_CASE( "Tool reports serialize deterministically and reject truncation",
           "[CypherCommon][ToolFramework][Core]" )
{
    tool_report_t report{};
    report.operationId = 7u;
    report.status = tool_status_t::OK;
    report.nStartTicks = 10u;
    report.nEndTicks = 20u;
    report.nInputsDiscovered = 2u;
    report.nInputsProcessed = 2u;
    report.nSucceeded = 2u;
    report.nArtifacts = 2u;
    report.cbRead = 128u;
    report.cbWritten = 64u;

    const tool_report_write_options_t options{
        tool_output_format_t::JSON,
        CY_FALSE,
        CY_TRUE
    };
    const tool_report_write_result_t measured = ToolReportWriter_Write(
        report,
        options,
        nullptr,
        0u );
    REQUIRE( measured.status == tool_status_t::CAPACITY_EXCEEDED );
    REQUIRE( measured.cchRequired > 0u );

    std::array<char, 1024> output{};
    const tool_report_write_result_t written = ToolReportWriter_Write(
        report,
        options,
        output.data(),
        output.size() );
    REQUIRE( written.status == tool_status_t::OK );
    const std::string_view text{ output.data(), written.cchWritten };
    CHECK( text.find( "\"schema\":\"cypher.tool-report.v1\"" ) !=
           std::string_view::npos );
    CHECK( text.find( "\"operation_id\":7" ) != std::string_view::npos );
    CHECK( text.find( "\"status\":\"OK\"" ) != std::string_view::npos );
}

TEST_CASE( "Tool session IDs and terminal state progress monotonically",
           "[CypherCommon][ToolFramework][Core]" )
{
    tool_session_t session{};
    ToolSession_Init( &session );
    REQUIRE( ToolSession_State( &session ) == tool_session_state_t::READY );
    REQUIRE( ToolSession_Begin( &session ) == tool_status_t::OK );
    REQUIRE( ToolSession_NextOperationId( &session ) == 1u );
    REQUIRE( ToolSession_NextOperationId( &session ) == 2u );
    REQUIRE( ToolSession_NextSequence( &session ) == 1u );
    REQUIRE( ToolSession_Finish(
                 &session,
                 tool_status_t::CANCELLED ) == tool_status_t::OK );
    REQUIRE( ToolSession_State( &session ) ==
             tool_session_state_t::CANCELLED );
    REQUIRE( ToolSession_Status( &session ) == tool_status_t::CANCELLED );
    REQUIRE( ToolSession_Begin( &session ) == tool_status_t::INVALID_STATE );
}

TEST_CASE( "Tool session IDs saturate instead of wrapping into duplicates",
           "[CypherCommon][ToolFramework][Core]" )
{
    tool_session_t session{};
    ToolSession_Init( &session );
    REQUIRE( ToolSession_Begin( &session ) == tool_status_t::OK );
    Cy_AtomicStore(
        &session.nNextOperationId,
        static_cast<u64>( CY_U64_MAX - 1u ),
        CY_MEMORY_ORDER_RELAXED );

    REQUIRE( ToolSession_NextOperationId( &session ) == CY_U64_MAX - 1u );
    REQUIRE( ToolSession_NextOperationId( &session ) ==
             CY_TOOL_INVALID_OPERATION_ID );
    REQUIRE( ToolSession_NextOperationId( &session ) ==
             CY_TOOL_INVALID_OPERATION_ID );
}
