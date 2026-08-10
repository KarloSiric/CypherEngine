//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_Config_Tests.cpp
//  Purpose: Tests command and ConVar configuration text I/O.
//  Details: Covers line parsing, comments, command policy, archived filtering,
//           escaped string round trips, error counting, and streamed output.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Config.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace cypher::common;

namespace
{

error_code_t CountCommand(
    const command_context_t &,
    const command_args_t &,
    void *pUserData ) noexcept
{
    ++*static_cast<usize *>( pUserData );
    return CY_ERROR_OK;
}

bool_t CaptureConfig( string_view_t text, void *pUserData ) noexcept
{
    auto *pOutput = static_cast<std::string *>( pUserData );
    pOutput->append( text.pData, text.cchLength );
    return CY_TRUE;
}

convar_desc_t StringConVar(
    const char *pName,
    const char *pDefault,
    flags32_t flags ) noexcept
{
    return {
        StringView_FromCString( pName ),
        {},
        convar_type_t::STRING,
        StringView_FromCString( pDefault ),
        {},
        {},
        flags,
        nullptr,
        nullptr
    };
}

} // namespace

TEST_CASE( "Config loads ConVars commands and comments with explicit policy",
           "[CypherCommon][Tier1][Config]" )
{
    command_system_t *pSystem = CommandSystem_Create( {} );
    REQUIRE( pSystem != nullptr );
    const convar_register_result_t title = CommandSystem_RegisterConVar(
        pSystem,
        StringConVar( "game.title", "Default", CONVAR_FLAG_ARCHIVE ) );
    REQUIRE( Cy_ErrorSucceeded( title.error ) );
    usize nCommandCalls = 0u;
    const command_register_result_t command = CommandSystem_RegisterCommand(
        pSystem,
        {
            StringView_FromCString( "apply" ), {}, {},
            CONCOMMAND_FLAG_NONE,
            CountCommand,
            nullptr,
            &nCommandCalls
        } );
    REQUIRE( Cy_ErrorSucceeded( command.error ) );

    const config_source_t source{
        StringView_FromCString( "test.cfg" ),
        StringView_FromCString(
            "# comment\n"
            "game.title \"Cypher \\\"Arena\\\"\" // title\n"
            "apply\n" ),
        CONFIG_FLAG_ALLOW_COMMANDS | CONFIG_FLAG_STOP_ON_ERROR
    };
    const config_load_result_t loaded = Config_Load( source, pSystem, {} );
    REQUIRE( Cy_ErrorSucceeded( loaded.error ) );
    REQUIRE( loaded.nLinesRead == 3u );
    REQUIRE( loaded.nCommandsExecuted == 2u );
    REQUIRE( nCommandCalls == 1u );

    convar_value_t value{};
    REQUIRE( CommandSystem_GetConVar( pSystem, title.handle, &value ) );
    string_view_t text{};
    REQUIRE( Variant_GetString( value.value, &text ) );
    REQUIRE( StringView_Equals(
        text,
        StringView_FromCString( "Cypher \"Arena\"" ) ) );
    CommandSystem_Destroy( pSystem );
}

TEST_CASE( "Config reports command restrictions and stop-on-error position",
           "[CypherCommon][Tier1][Config]" )
{
    command_system_t *pSystem = CommandSystem_Create( {} );
    usize nCalls = 0u;
    REQUIRE( Cy_ErrorSucceeded( CommandSystem_RegisterCommand(
        pSystem,
        {
            StringView_FromCString( "run" ), {}, {},
            CONCOMMAND_FLAG_NONE,
            CountCommand,
            nullptr,
            &nCalls
        } ).error ) );
    const config_load_result_t loaded = Config_Load(
        {
            StringView_FromCString( "restricted.cfg" ),
            StringView_FromCString( "run\nrun\n" ),
            CONFIG_FLAG_STOP_ON_ERROR
        },
        pSystem,
        {} );
    REQUIRE( Cy_ErrorDomain( loaded.error ) == error_domain_t::CONFIG );
    REQUIRE( loaded.nErrors == 1u );
    REQUIRE( loaded.nLinesRead == 1u );
    REQUIRE( loaded.iErrorByte == 0u );
    REQUIRE( nCalls == 0u );
    CommandSystem_Destroy( pSystem );
}

TEST_CASE( "Config writes only archived ConVars as reloadable text",
           "[CypherCommon][Tier1][Config]" )
{
    command_system_t *pSystem = CommandSystem_Create( {} );
    REQUIRE( Cy_ErrorSucceeded( CommandSystem_RegisterConVar(
        pSystem,
        StringConVar( "saved", "line\ntext", CONVAR_FLAG_ARCHIVE ) ).error ) );
    REQUIRE( Cy_ErrorSucceeded( CommandSystem_RegisterConVar(
        pSystem,
        StringConVar( "temporary", "skip", CONVAR_FLAG_NONE ) ).error ) );

    std::string output;
    REQUIRE( Config_WriteArchivedConVars(
        pSystem,
        { CaptureConfig, &output } ) == CY_ERROR_OK );
    REQUIRE( output == "saved \"line\\ntext\"\n" );
    CommandSystem_Destroy( pSystem );
}
