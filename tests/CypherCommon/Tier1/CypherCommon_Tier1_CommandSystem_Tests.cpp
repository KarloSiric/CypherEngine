//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_CommandSystem_Tests.cpp
//  Purpose: Tests the owned command and ConVar registry.
//  Details: Covers ownership, lookup policy, stale handles, dispatch permissions,
//           bounded reentrancy, callbacks, completion, bounds, and OOM rollback.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_CommandSystem.h"

#include <catch2/catch_test_macros.hpp>

#include <new>
#include <string>

using namespace cypher::common;

namespace
{

struct command_capture_t {
    command_system_t *pSystem{ nullptr };
    command_handle_t recursiveHandle{ CY_COMMAND_HANDLE_INVALID };
    usize cCalls{ 0u };
    usize cArguments{ 0u };
    u64 nCallerId{ 0u };
    std::string firstArgument{};
    error_code_t nestedError{ CY_ERROR_OK };
    command_register_result_t nestedRegistration{};
};

struct convar_capture_t {
    command_system_t *pSystem{ nullptr };
    convar_handle_t handle{ CY_CONVAR_HANDLE_INVALID };
    usize cCalls{ 0u };
    std::string oldText{};
    std::string newText{};
    i64 nOldValue{ 0 };
    i64 nNewValue{ 0 };
    error_code_t nestedError{ CY_ERROR_OK };
};

struct output_capture_t {
    std::string text{};
    usize cCalls{ 0u };
};

struct visit_capture_t {
    command_system_t *pSystem{ nullptr };
    usize cCommands{ 0u };
    usize cConVars{ 0u };
    error_code_t mutationError{ CY_ERROR_OK };
};

struct failing_allocator_state_t {
    bool_t bFail{ CY_FALSE };
};

std::string ToString( string_view_t text )
{
    return text.cchLength > 0u
        ? std::string( text.pData, text.cchLength )
        : std::string{};
}

error_code_t CaptureCommand(
    const command_context_t &context,
    const command_args_t &args,
    void *pUserData ) noexcept
{
    auto *pCapture = static_cast<command_capture_t *>( pUserData );
    ++pCapture->cCalls;
    pCapture->cArguments = args.nArgumentCount;
    pCapture->nCallerId = context.nCallerId;
    if ( args.nArgumentCount > 1u ) {
        pCapture->firstArgument = ToString( args.arguments[1] );
    }
    return CY_ERROR_OK;
}

error_code_t RecursiveCommand(
    const command_context_t &context,
    const command_args_t &,
    void *pUserData ) noexcept
{
    auto *pCapture = static_cast<command_capture_t *>( pUserData );
    ++pCapture->cCalls;
    pCapture->nestedError = CommandSystem_ExecuteLine(
        pCapture->pSystem,
        StringView_FromCString( "recursive" ),
        context );
    return pCapture->nestedError;
}

error_code_t RegisterDuringCallback(
    const command_context_t &,
    const command_args_t &,
    void *pUserData ) noexcept
{
    auto *pCapture = static_cast<command_capture_t *>( pUserData );
    const concommand_desc_t nested{
        StringView_FromCString( "nested" ),
        {},
        {},
        CONCOMMAND_FLAG_NONE,
        CaptureCommand,
        nullptr,
        pCapture
    };
    pCapture->nestedRegistration = CommandSystem_RegisterCommand(
        pCapture->pSystem,
        nested );
    return CY_ERROR_OK;
}

usize CompleteQuality(
    string_view_t partial,
    string_view_t *pSuggestions,
    usize nSuggestionCapacity,
    void * ) noexcept
{
    static constexpr char low[] = "low";
    static constexpr char high[] = "high";
    const string_view_t candidates[] = {
        StringView_FromRange( low, sizeof( low ) - 1u ),
        StringView_FromRange( high, sizeof( high ) - 1u )
    };

    usize nRequired = 0u;
    for ( const string_view_t candidate : candidates ) {
        if ( !StringView_StartsWithInsensitiveAscii( candidate, partial ) ) {
            continue;
        }
        if ( pSuggestions != nullptr && nRequired < nSuggestionCapacity ) {
            pSuggestions[nRequired] = candidate;
        }
        ++nRequired;
    }
    return nRequired;
}

void CaptureConVarChange(
    string_view_t,
    const convar_value_t &oldValue,
    const convar_value_t &newValue,
    void *pUserData ) noexcept
{
    auto *pCapture = static_cast<convar_capture_t *>( pUserData );
    ++pCapture->cCalls;
    if ( oldValue.value.type == variant_type_t::STRING_VIEW ) {
        string_view_t oldText{};
        string_view_t newText{};
        (void)Variant_GetString( oldValue.value, &oldText );
        (void)Variant_GetString( newValue.value, &newText );
        pCapture->oldText = ToString( oldText );
        pCapture->newText = ToString( newText );
    } else {
        (void)Variant_GetI64( oldValue.value, &pCapture->nOldValue );
        (void)Variant_GetI64( newValue.value, &pCapture->nNewValue );
    }
}

void RecursiveConVarChange(
    string_view_t,
    const convar_value_t &,
    const convar_value_t &,
    void *pUserData ) noexcept
{
    auto *pCapture = static_cast<convar_capture_t *>( pUserData );
    ++pCapture->cCalls;
    pCapture->nestedError = CommandSystem_SetConVar(
        pCapture->pSystem,
        pCapture->handle,
        StringView_FromCString( "99" ),
        {} );
}

void CaptureOutput( string_view_t text, void *pUserData ) noexcept
{
    auto *pCapture = static_cast<output_capture_t *>( pUserData );
    ++pCapture->cCalls;
    pCapture->text = ToString( text );
}

bool_t VisitCommand(
    command_handle_t handle,
    const concommand_desc_t &,
    void *pUserData ) noexcept
{
    auto *pCapture = static_cast<visit_capture_t *>( pUserData );
    ++pCapture->cCommands;
    pCapture->mutationError = CommandSystem_UnregisterCommand(
        pCapture->pSystem,
        handle );
    return CY_TRUE;
}

bool_t VisitConVar(
    convar_handle_t,
    const convar_desc_t &,
    const convar_value_t &,
    void *pUserData ) noexcept
{
    auto *pCapture = static_cast<visit_capture_t *>( pUserData );
    ++pCapture->cConVars;
    return CY_TRUE;
}

void *FailingAllocate(
    void *pUserData,
    usize cbSize,
    usize nAlignment ) noexcept
{
    auto *pState = static_cast<failing_allocator_state_t *>( pUserData );
    if ( pState->bFail ) {
        return nullptr;
    }
    return Allocator_GetSystem()->pfnAllocate(
        Allocator_GetSystem()->pUserData,
        cbSize,
        nAlignment );
}

void FailingFree(
    void *,
    void *pMemory,
    usize cbSize,
    usize nAlignment ) noexcept
{
    Allocator_GetSystem()->pfnFree(
        Allocator_GetSystem()->pUserData,
        pMemory,
        cbSize,
        nAlignment );
}

concommand_desc_t MakeCommandDesc(
    const char *pName,
    command_capture_t *pCapture,
    flags32_t flags = CONCOMMAND_FLAG_NONE ) noexcept
{
    return {
        StringView_FromCString( pName ),
        StringView_FromCString( "test command" ),
        StringView_FromCString( "<value>" ),
        flags,
        CaptureCommand,
        nullptr,
        pCapture
    };
}

convar_desc_t MakeI64Desc(
    const char *pName,
    const char *pDefault,
    const char *pMinimum,
    const char *pMaximum,
    flags32_t flags = CONVAR_FLAG_NONE,
    convar_changed_fn_t pfnChanged = nullptr,
    void *pUserData = nullptr ) noexcept
{
    return {
        StringView_FromCString( pName ),
        StringView_FromCString( "test ConVar" ),
        convar_type_t::I64,
        StringView_FromCString( pDefault ),
        StringView_FromCString( pMinimum ),
        StringView_FromCString( pMaximum ),
        flags,
        pfnChanged,
        pUserData
    };
}

} // namespace

TEST_CASE( "CommandSystem owns metadata and applies one case-aware namespace",
           "[CypherCommon][Tier1][CommandSystem]" )
{
    command_system_t *pSystem = CommandSystem_Create( {} );
    REQUIRE( pSystem != nullptr );

    char name[] = "Render.Mode";
    char help[] = "select backend";
    command_capture_t capture{};
    concommand_desc_t desc{
        StringView_FromCString( name ),
        StringView_FromCString( help ),
        {},
        CONCOMMAND_FLAG_NONE,
        CaptureCommand,
        nullptr,
        &capture
    };
    const command_register_result_t registered =
        CommandSystem_RegisterCommand( pSystem, desc );
    REQUIRE( Cy_ErrorSucceeded( registered.error ) );
    REQUIRE( Cy_Handle32IsValid( registered.handle ) );

    name[0] = 'X';
    help[0] = 'X';
    REQUIRE( CommandSystem_FindCommand(
        pSystem,
        StringView_FromCString( "render.mode" ) ).value == registered.handle.value );

    concommand_desc_t owned{};
    REQUIRE( CommandSystem_GetCommandDesc( pSystem, registered.handle, &owned ) );
    REQUIRE( ToString( owned.name ) == "Render.Mode" );
    REQUIRE( ToString( owned.help ) == "select backend" );

    const convar_register_result_t collision = CommandSystem_RegisterConVar(
        pSystem,
        MakeI64Desc( "RENDER.MODE", "1", "0", "2" ) );
    REQUIRE( Cy_ErrorDomain( collision.error ) == error_domain_t::CVAR );
    REQUIRE( Cy_ErrorLocalCode( collision.error ) ==
             static_cast<u16>( convar_system_error_t::ALREADY_EXISTS ) );
    REQUIRE( CommandSystem_CommandCount( pSystem ) == 1u );
    REQUIRE( CommandSystem_ConVarCount( pSystem ) == 0u );
    REQUIRE( CommandSystem_IsValid( pSystem ) );
    CommandSystem_Destroy( pSystem );
}

TEST_CASE( "CommandSystem exposes ConVar lookup descriptors removal and error domains",
           "[CypherCommon][Tier1][CommandSystem]" )
{
    command_system_t *pSystem = CommandSystem_Create( {} );
    REQUIRE( pSystem != nullptr );
    const convar_register_result_t registered = CommandSystem_RegisterConVar(
        pSystem,
        MakeI64Desc( "test.value", "7", "0", "10" ) );
    REQUIRE( Cy_ErrorSucceeded( registered.error ) );

    const convar_handle_t found = CommandSystem_FindConVar(
        pSystem,
        StringView_FromCString( "TEST.VALUE" ) );
    REQUIRE( found.value == registered.handle.value );

    convar_desc_t desc{};
    REQUIRE( CommandSystem_GetConVarDesc( pSystem, found, &desc ) );
    REQUIRE( ToString( desc.name ) == "test.value" );
    REQUIRE( ToString( desc.defaultValue ) == "7" );

    REQUIRE( CommandSystem_MakeError( command_system_error_t::OK ) == CY_ERROR_OK );
    REQUIRE( Cy_ErrorDomain( CommandSystem_MakeError(
        command_system_error_t::NOT_FOUND ) ) == error_domain_t::COMMAND );
    REQUIRE( CommandSystem_MakeError( convar_system_error_t::OK ) == CY_ERROR_OK );
    REQUIRE( Cy_ErrorDomain( CommandSystem_MakeError(
        convar_system_error_t::NOT_FOUND ) ) == error_domain_t::CVAR );

    REQUIRE( Cy_ErrorSucceeded(
        CommandSystem_UnregisterConVar( pSystem, registered.handle ) ) );
    REQUIRE_FALSE( Cy_Handle32IsValid( CommandSystem_FindConVar(
        pSystem,
        StringView_FromCString( "test.value" ) ) ) );
    REQUIRE_FALSE( CommandSystem_GetConVarDesc(
        pSystem,
        registered.handle,
        &desc ) );
    CommandSystem_Destroy( pSystem );
}

TEST_CASE( "CommandSystem rejects stale handles after unregister and reuse",
           "[CypherCommon][Tier1][CommandSystem]" )
{
    command_system_t *pSystem = CommandSystem_Create( {} );
    command_capture_t capture{};
    const command_register_result_t first = CommandSystem_RegisterCommand(
        pSystem,
        MakeCommandDesc( "first", &capture ) );
    REQUIRE( Cy_ErrorSucceeded( first.error ) );
    REQUIRE( Cy_ErrorSucceeded(
        CommandSystem_UnregisterCommand( pSystem, first.handle ) ) );

    const command_register_result_t replacement = CommandSystem_RegisterCommand(
        pSystem,
        MakeCommandDesc( "replacement", &capture ) );
    REQUIRE( Cy_ErrorSucceeded( replacement.error ) );
    REQUIRE( replacement.handle.value != first.handle.value );

    concommand_desc_t staleDesc{};
    REQUIRE_FALSE( CommandSystem_GetCommandDesc(
        pSystem,
        first.handle,
        &staleDesc ) );
    REQUIRE( Cy_ErrorLocalCode( CommandSystem_UnregisterCommand(
        pSystem,
        first.handle ) ) ==
        static_cast<u16>( command_system_error_t::NOT_FOUND ) );
    REQUIRE( CommandSystem_IsValid( pSystem ) );
    CommandSystem_Destroy( pSystem );
}

TEST_CASE( "CommandSystem supports an explicit case-sensitive name policy",
           "[CypherCommon][Tier1][CommandSystem]" )
{
    command_system_desc_t systemDesc{};
    systemDesc.bCaseInsensitiveAscii = CY_FALSE;
    command_system_t *pSystem = CommandSystem_Create( systemDesc );
    REQUIRE( pSystem != nullptr );

    command_capture_t capture{};
    const command_register_result_t upper = CommandSystem_RegisterCommand(
        pSystem,
        MakeCommandDesc( "Echo", &capture ) );
    const command_register_result_t lower = CommandSystem_RegisterCommand(
        pSystem,
        MakeCommandDesc( "echo", &capture ) );
    REQUIRE( Cy_ErrorSucceeded( upper.error ) );
    REQUIRE( Cy_ErrorSucceeded( lower.error ) );
    REQUIRE( upper.handle.value != lower.handle.value );
    REQUIRE( CommandSystem_FindCommand(
        pSystem,
        StringView_FromCString( "ECHO" ) ).value ==
        CY_COMMAND_HANDLE_INVALID.value );
    REQUIRE( CommandSystem_IsValid( pSystem ) );
    CommandSystem_Destroy( pSystem );
}

TEST_CASE( "CommandSystem dispatches arguments and enforces command permissions",
           "[CypherCommon][Tier1][CommandSystem]" )
{
    command_system_t *pSystem = CommandSystem_Create( {} );
    command_capture_t capture{};
    const command_register_result_t ordinary = CommandSystem_RegisterCommand(
        pSystem,
        MakeCommandDesc( "echo", &capture ) );
    REQUIRE( Cy_ErrorSucceeded( ordinary.error ) );

    command_context_t context{};
    context.nCallerId = 77u;
    REQUIRE( Cy_ErrorSucceeded( CommandSystem_ExecuteLine(
        pSystem,
        StringView_FromCString( "echo \"hello world\"" ),
        context ) ) );
    REQUIRE( capture.cCalls == 1u );
    REQUIRE( capture.cArguments == 2u );
    REQUIRE( capture.firstArgument == "hello world" );
    REQUIRE( capture.nCallerId == 77u );

    const command_register_result_t protectedCommand =
        CommandSystem_RegisterCommand(
            pSystem,
            MakeCommandDesc(
                "protected",
                &capture,
                CONCOMMAND_FLAG_CHEAT |
                    CONCOMMAND_FLAG_DEVELOPMENT |
                    CONCOMMAND_FLAG_SERVER_ONLY |
                    CONCOMMAND_FLAG_REMOTE_ALLOWED ) );
    REQUIRE( Cy_ErrorSucceeded( protectedCommand.error ) );

    command_context_t remote{};
    remote.source = command_source_t::REMOTE_CLIENT;
    REQUIRE( Cy_ErrorLocalCode( CommandSystem_ExecuteLine(
        pSystem,
        StringView_FromCString( "protected" ),
        remote ) ) ==
        static_cast<u16>( command_system_error_t::PERMISSION_DENIED ) );
    remote.bCheatsAllowed = CY_TRUE;
    remote.bDevelopmentAllowed = CY_TRUE;
    remote.bServerContext = CY_TRUE;
    REQUIRE( Cy_ErrorSucceeded( CommandSystem_ExecuteLine(
        pSystem,
        StringView_FromCString( "protected" ),
        remote ) ) );

    REQUIRE( Cy_ErrorLocalCode( CommandSystem_ExecuteLine(
        pSystem,
        StringView_FromCString( "echo" ),
        remote ) ) ==
        static_cast<u16>( command_system_error_t::PERMISSION_DENIED ) );
    CommandSystem_Destroy( pSystem );
}

TEST_CASE( "CommandSystem bounds recursive execution and blocks registry mutation",
           "[CypherCommon][Tier1][CommandSystem]" )
{
    command_system_t *pSystem = CommandSystem_Create( {} );
    command_capture_t recursive{};
    recursive.pSystem = pSystem;
    concommand_desc_t recursiveDesc = MakeCommandDesc( "recursive", &recursive );
    recursiveDesc.pfnExecute = RecursiveCommand;
    const command_register_result_t registered =
        CommandSystem_RegisterCommand( pSystem, recursiveDesc );
    REQUIRE( Cy_ErrorSucceeded( registered.error ) );

    const error_code_t recursionError = CommandSystem_ExecuteLine(
        pSystem,
        StringView_FromCString( "recursive" ),
        {} );
    REQUIRE( Cy_ErrorDomain( recursionError ) == error_domain_t::COMMAND );
    REQUIRE( Cy_ErrorLocalCode( recursionError ) ==
             static_cast<u16>( command_system_error_t::RECURSION_LIMIT ) );
    REQUIRE( recursive.cCalls == CY_COMMAND_MAX_EXECUTION_DEPTH );

    command_capture_t mutation{};
    mutation.pSystem = pSystem;
    concommand_desc_t mutationDesc = MakeCommandDesc( "mutate", &mutation );
    mutationDesc.pfnExecute = RegisterDuringCallback;
    REQUIRE( Cy_ErrorSucceeded(
        CommandSystem_RegisterCommand( pSystem, mutationDesc ).error ) );
    REQUIRE( Cy_ErrorSucceeded( CommandSystem_ExecuteLine(
        pSystem,
        StringView_FromCString( "mutate" ),
        {} ) ) );
    REQUIRE( Cy_ErrorLocalCode( mutation.nestedRegistration.error ) ==
             static_cast<u16>( command_system_error_t::BUSY ) );
    REQUIRE_FALSE( Cy_Handle32IsValid( mutation.nestedRegistration.handle ) );
    REQUIRE( CommandSystem_IsValid( pSystem ) );
    CommandSystem_Destroy( pSystem );
}

TEST_CASE( "CommandSystem owns ConVar strings and preserves callback old values",
           "[CypherCommon][Tier1][CommandSystem]" )
{
    command_system_t *pSystem = CommandSystem_Create( {} );
    convar_capture_t capture{};
    convar_desc_t desc{
        StringView_FromCString( "player.name" ),
        {},
        convar_type_t::STRING,
        StringView_FromCString( "ranger" ),
        {},
        {},
        CONVAR_FLAG_NONE,
        CaptureConVarChange,
        &capture
    };
    const convar_register_result_t registered =
        CommandSystem_RegisterConVar( pSystem, desc );
    REQUIRE( Cy_ErrorSucceeded( registered.error ) );

    char replacement[] = "marine";
    REQUIRE( Cy_ErrorSucceeded( CommandSystem_SetConVar(
        pSystem,
        registered.handle,
        StringView_FromCString( replacement ),
        {} ) ) );
    replacement[0] = 'X';

    convar_value_t value{};
    REQUIRE( CommandSystem_GetConVar( pSystem, registered.handle, &value ) );
    string_view_t current{};
    REQUIRE( Variant_GetString( value.value, &current ) );
    REQUIRE( ToString( current ) == "marine" );
    REQUIRE( capture.cCalls == 1u );
    REQUIRE( capture.oldText == "ranger" );
    REQUIRE( capture.newText == "marine" );

    REQUIRE( Cy_ErrorSucceeded( CommandSystem_ResetConVar(
        pSystem,
        registered.handle,
        {} ) ) );
    REQUIRE( CommandSystem_GetConVar( pSystem, registered.handle, &value ) );
    REQUIRE( Variant_GetString( value.value, &current ) );
    REQUIRE( ToString( current ) == "ranger" );
    REQUIRE( CommandSystem_IsValid( pSystem ) );
    CommandSystem_Destroy( pSystem );
}

TEST_CASE( "CommandSystem applies cached ConVar bounds permissions and callbacks",
           "[CypherCommon][Tier1][CommandSystem]" )
{
    command_system_t *pSystem = CommandSystem_Create( {} );
    convar_capture_t capture{};
    const convar_register_result_t registered = CommandSystem_RegisterConVar(
        pSystem,
        MakeI64Desc(
            "sv_speed",
            "10",
            "1",
            "20",
            CONVAR_FLAG_CHEAT | CONVAR_FLAG_REMOTE_WRITE_ALLOWED,
            CaptureConVarChange,
            &capture ) );
    REQUIRE( Cy_ErrorSucceeded( registered.error ) );

    command_context_t context{};
    REQUIRE( Cy_ErrorLocalCode( CommandSystem_SetConVar(
        pSystem,
        registered.handle,
        StringView_FromCString( "15" ),
        context ) ) ==
        static_cast<u16>( convar_system_error_t::PERMISSION_DENIED ) );
    context.bCheatsAllowed = CY_TRUE;
    REQUIRE( Cy_ErrorLocalCode( CommandSystem_SetConVar(
        pSystem,
        registered.handle,
        StringView_FromCString( "0" ),
        context ) ) ==
        static_cast<u16>( convar_system_error_t::BELOW_MINIMUM ) );
    REQUIRE( Cy_ErrorLocalCode( CommandSystem_SetConVar(
        pSystem,
        registered.handle,
        StringView_FromCString( "21" ),
        context ) ) ==
        static_cast<u16>( convar_system_error_t::ABOVE_MAXIMUM ) );
    REQUIRE( Cy_ErrorSucceeded( CommandSystem_SetConVar(
        pSystem,
        registered.handle,
        StringView_FromCString( "15" ),
        context ) ) );
    REQUIRE( capture.cCalls == 1u );
    REQUIRE( capture.nOldValue == 10 );
    REQUIRE( capture.nNewValue == 15 );

    context.source = command_source_t::REMOTE_CLIENT;
    REQUIRE( Cy_ErrorSucceeded( CommandSystem_SetConVar(
        pSystem,
        registered.handle,
        StringView_FromCString( "16" ),
        context ) ) );

    const convar_register_result_t readOnly = CommandSystem_RegisterConVar(
        pSystem,
        MakeI64Desc(
            "build.number",
            "1",
            "",
            "",
            CONVAR_FLAG_READ_ONLY ) );
    REQUIRE( Cy_ErrorSucceeded( readOnly.error ) );
    REQUIRE( Cy_ErrorLocalCode( CommandSystem_SetConVar(
        pSystem,
        readOnly.handle,
        StringView_FromCString( "2" ),
        {} ) ) ==
        static_cast<u16>( convar_system_error_t::READ_ONLY ) );
    CommandSystem_Destroy( pSystem );
}

TEST_CASE( "CommandSystem rejects same-ConVar recursive callbacks",
           "[CypherCommon][Tier1][CommandSystem]" )
{
    command_system_t *pSystem = CommandSystem_Create( {} );
    convar_capture_t capture{};
    capture.pSystem = pSystem;
    const convar_register_result_t registered = CommandSystem_RegisterConVar(
        pSystem,
        MakeI64Desc(
            "counter",
            "1",
            "0",
            "100",
            CONVAR_FLAG_NONE,
            RecursiveConVarChange,
            &capture ) );
    REQUIRE( Cy_ErrorSucceeded( registered.error ) );
    capture.handle = registered.handle;

    REQUIRE( Cy_ErrorSucceeded( CommandSystem_SetConVar(
        pSystem,
        registered.handle,
        StringView_FromCString( "2" ),
        {} ) ) );
    REQUIRE( capture.cCalls == 1u );
    REQUIRE( Cy_ErrorLocalCode( capture.nestedError ) ==
             static_cast<u16>( convar_system_error_t::BUSY ) );

    convar_value_t value{};
    i64 nValue = 0;
    REQUIRE( CommandSystem_GetConVar( pSystem, registered.handle, &value ) );
    REQUIRE( Variant_GetI64( value.value, &nValue ) );
    REQUIRE( nValue == 2 );
    CommandSystem_Destroy( pSystem );
}

TEST_CASE( "CommandSystem executes Source-style ConVar query and assignment",
           "[CypherCommon][Tier1][CommandSystem]" )
{
    output_capture_t output{};
    command_system_desc_t systemDesc{};
    systemDesc.pfnOutput = CaptureOutput;
    systemDesc.pOutputUserData = &output;
    command_system_t *pSystem = CommandSystem_Create( systemDesc );
    const convar_register_result_t registered = CommandSystem_RegisterConVar(
        pSystem,
        MakeI64Desc( "r_quality", "2", "0", "4" ) );
    REQUIRE( Cy_ErrorSucceeded( registered.error ) );

    REQUIRE( Cy_ErrorSucceeded( CommandSystem_ExecuteLine(
        pSystem,
        StringView_FromCString( "r_quality" ),
        {} ) ) );
    REQUIRE( output.cCalls == 1u );
    REQUIRE( output.text == "r_quality = 2" );

    REQUIRE( Cy_ErrorSucceeded( CommandSystem_ExecuteLine(
        pSystem,
        StringView_FromCString( "r_quality 4" ),
        {} ) ) );
    REQUIRE( Cy_ErrorLocalCode( CommandSystem_ExecuteLine(
        pSystem,
        StringView_FromCString( "r_quality 1 2" ),
        {} ) ) ==
        static_cast<u16>( convar_system_error_t::INVALID_ARGUMENT ) );
    CommandSystem_Destroy( pSystem );
}

TEST_CASE( "CommandSystem completes visible names and delegates arguments",
           "[CypherCommon][Tier1][CommandSystem]" )
{
    command_system_t *pSystem = CommandSystem_Create( {} );
    command_capture_t capture{};
    concommand_desc_t alpha = MakeCommandDesc( "Alpha", &capture );
    concommand_desc_t quality = MakeCommandDesc( "quality", &capture );
    quality.pfnComplete = CompleteQuality;
    REQUIRE( Cy_ErrorSucceeded(
        CommandSystem_RegisterCommand( pSystem, alpha ).error ) );
    REQUIRE( Cy_ErrorSucceeded(
        CommandSystem_RegisterCommand( pSystem, quality ).error ) );
    REQUIRE( Cy_ErrorSucceeded( CommandSystem_RegisterCommand(
        pSystem,
        MakeCommandDesc(
            "alpha_hidden",
            &capture,
            CONCOMMAND_FLAG_HIDDEN ) ).error ) );
    REQUIRE( Cy_ErrorSucceeded( CommandSystem_RegisterConVar(
        pSystem,
        MakeI64Desc( "albedo", "1", "0", "2" ) ).error ) );

    string_view_t suggestions[4]{};
    REQUIRE( CommandSystem_Complete(
        pSystem,
        StringView_FromCString( "a" ),
        suggestions,
        4u ) == 2u );
    REQUIRE( ToString( suggestions[0] ) == "albedo" );
    REQUIRE( ToString( suggestions[1] ) == "Alpha" );

    REQUIRE( CommandSystem_Complete(
        pSystem,
        StringView_FromCString( "quality h" ),
        suggestions,
        4u ) == 1u );
    REQUIRE( ToString( suggestions[0] ) == "high" );
    CommandSystem_Destroy( pSystem );
}

TEST_CASE( "CommandSystem visitors expose records and reject structural mutation",
           "[CypherCommon][Tier1][CommandSystem]" )
{
    command_system_t *pSystem = CommandSystem_Create( {} );
    command_capture_t commandCapture{};
    REQUIRE( Cy_ErrorSucceeded( CommandSystem_RegisterCommand(
        pSystem,
        MakeCommandDesc( "one", &commandCapture ) ).error ) );
    REQUIRE( Cy_ErrorSucceeded( CommandSystem_RegisterCommand(
        pSystem,
        MakeCommandDesc( "two", &commandCapture ) ).error ) );
    REQUIRE( Cy_ErrorSucceeded( CommandSystem_RegisterConVar(
        pSystem,
        MakeI64Desc( "value", "1", "0", "2" ) ).error ) );

    visit_capture_t visit{};
    visit.pSystem = pSystem;
    REQUIRE( CommandSystem_ForEachCommand(
        pSystem,
        VisitCommand,
        &visit ) == 2u );
    REQUIRE( visit.cCommands == 2u );
    REQUIRE( Cy_ErrorLocalCode( visit.mutationError ) ==
             static_cast<u16>( command_system_error_t::BUSY ) );
    REQUIRE( CommandSystem_ForEachConVar(
        pSystem,
        VisitConVar,
        &visit ) == 1u );
    REQUIRE( visit.cConVars == 1u );
    REQUIRE( CommandSystem_CommandCount( pSystem ) == 2u );
    REQUIRE( CommandSystem_IsValid( pSystem ) );
    CommandSystem_Destroy( pSystem );
}

TEST_CASE( "CommandSystem allocation failures preserve live registry state",
           "[CypherCommon][Tier1][CommandSystem]" )
{
    failing_allocator_state_t allocatorState{};
    const allocator_t allocator{
        FailingAllocate,
        nullptr,
        FailingFree,
        &allocatorState
    };
    command_system_desc_t systemDesc{};
    systemDesc.pAllocator = &allocator;
    systemDesc.nInitialCommands = 4u;
    systemDesc.nInitialConVars = 4u;
    command_system_t *pSystem = CommandSystem_Create( systemDesc );
    REQUIRE( pSystem != nullptr );

    command_capture_t capture{};
    const command_register_result_t existing = CommandSystem_RegisterCommand(
        pSystem,
        MakeCommandDesc( "existing", &capture ) );
    REQUIRE( Cy_ErrorSucceeded( existing.error ) );
    const convar_register_result_t text = CommandSystem_RegisterConVar(
        pSystem,
        convar_desc_t{
            StringView_FromCString( "text" ),
            {},
            convar_type_t::STRING,
            StringView_FromCString( "old" )
        } );
    REQUIRE( Cy_ErrorSucceeded( text.error ) );

    allocatorState.bFail = CY_TRUE;
    const command_register_result_t failed = CommandSystem_RegisterCommand(
        pSystem,
        MakeCommandDesc( "failed", &capture ) );
    REQUIRE( Cy_ErrorLocalCode( failed.error ) ==
             static_cast<u16>( command_system_error_t::OUT_OF_MEMORY ) );
    REQUIRE( CommandSystem_FindCommand(
        pSystem,
        StringView_FromCString( "existing" ) ).value == existing.handle.value );
    REQUIRE( CommandSystem_CommandCount( pSystem ) == 1u );

    REQUIRE( Cy_ErrorLocalCode( CommandSystem_SetConVar(
        pSystem,
        text.handle,
        StringView_FromCString( "replacement requiring storage" ),
        {} ) ) ==
        static_cast<u16>( convar_system_error_t::OUT_OF_MEMORY ) );
    convar_value_t current{};
    string_view_t currentText{};
    REQUIRE( CommandSystem_GetConVar( pSystem, text.handle, &current ) );
    REQUIRE( Variant_GetString( current.value, &currentText ) );
    REQUIRE( ToString( currentText ) == "old" );
    REQUIRE( CommandSystem_IsValid( pSystem ) );

    allocatorState.bFail = CY_FALSE;
    CommandSystem_Destroy( pSystem );
}

TEST_CASE( "CommandSystem exposes stable error names",
           "[CypherCommon][Tier1][CommandSystem]" )
{
    REQUIRE( std::string( CommandSystem_ErrorName(
        command_system_error_t::RECURSION_LIMIT ) ) == "RECURSION_LIMIT" );
    REQUIRE( std::string( CommandSystem_ErrorName(
        convar_system_error_t::BELOW_MINIMUM ) ) == "BELOW_MINIMUM" );
}
