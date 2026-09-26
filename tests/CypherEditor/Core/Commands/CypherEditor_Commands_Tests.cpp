//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditor_Commands_Tests.cpp
//  Purpose: Contract tests for the editor command registry.
//  Details: Registration is all-or-nothing, disabled commands cannot be run
//           from any entry point, console lines tokenize with quotes, and a
//           module's commands unload together.
//
//  History:
//  - Created by Karlo Siric on 2026-09-26
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditor_Commands.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace cypher::common;
using namespace cypher::editor;

namespace
{

struct test_context_t {
    int nRuns = 0;
    bool bEnabled = true;
    std::vector<std::string> lastArgs;
};

command_result_t Record( void *pContext, const command_args_t &args )
{
    test_context_t &context = *static_cast<test_context_t *>( pContext );
    ++context.nRuns;
    context.lastArgs.clear();
    for ( usize iArg = 0u; iArg < args.nArgs; ++iArg ) {
        context.lastArgs.emplace_back( args.pArgs[iArg].pData, args.pArgs[iArg].cchLength );
    }
    return command_result_t::OK;
}

u32 State( void *pContext )
{
    return static_cast<test_context_t *>( pContext )->bEnabled ? COMMAND_STATE_ENABLED | COMMAND_STATE_CHECKED
                                                               : COMMAND_STATE_NONE;
}

command_desc_t Command( const char *pId, test_context_t *pContext, command_state_fn pfnState = nullptr )
{
    command_desc_t command{};
    command.pId = pId;
    command.pLabel = pId;
    command.pfnExecute = Record;
    command.pfnState = pfnState;
    command.pContext = pContext;
    return command;
}

string_view_t View( const char *pText )
{
    return StringView_FromCString( pText );
}

struct observed_t {
    int nCalls = 0;
    command_result_t last = command_result_t::OK;
};

void Observe( void *pContext, const command_desc_t &, const command_args_t &, command_result_t result )
{
    observed_t &observed = *static_cast<observed_t *>( pContext );
    ++observed.nCalls;
    observed.last = result;
}

} // namespace

TEST_CASE( "Command registration is validated and all-or-nothing",
           "[CypherEditor][Core][Commands]" )
{
    test_context_t context;
    command_registry_t registry{};
    REQUIRE( EditorCommands_Init( &registry, Allocator_GetSystem() ) == command_registry_status_t::OK );
    const command_desc_t table[]{ Command( "map.tool.clip", &context ), Command( "file.save", &context ),
                                  Command( "edit.undo", &context ) };
    REQUIRE( EditorCommands_Register( &registry, table, 3u ) == command_registry_status_t::OK );
    REQUIRE( EditorCommands_Count( &registry ) == 3u );
    REQUIRE( std::string( EditorCommands_At( &registry, 0u )->pId ) == "edit.undo" ); // Sorted.

    const command_desc_t clash[]{ Command( "view.grid", &context ), Command( "file.save", &context ) };
    REQUIRE( EditorCommands_Register( &registry, clash, 2u ) == command_registry_status_t::DUPLICATE_COMMAND );
    REQUIRE( EditorCommands_Find( &registry, View( "view.grid" ) ) == nullptr );

    command_desc_t noExecute = Command( "view.grid", &context );
    noExecute.pfnExecute = nullptr;
    REQUIRE( EditorCommands_Register( &registry, &noExecute, 1u ) == command_registry_status_t::INVALID_ARGUMENT );
    const command_desc_t badId = Command( "grid", &context );
    REQUIRE( EditorCommands_Register( &registry, &badId, 1u ) == command_registry_status_t::INVALID_ARGUMENT );
    EditorCommands_Shutdown( &registry );
}

TEST_CASE( "Commands run through every entry point only when enabled",
           "[CypherEditor][Core][Commands]" )
{
    test_context_t context;
    observed_t observed;
    command_registry_t registry{};
    REQUIRE( EditorCommands_Init( &registry, Allocator_GetSystem() ) == command_registry_status_t::OK );
    const command_desc_t command = Command( "map.grid", &context, State );
    REQUIRE( EditorCommands_Register( &registry, &command, 1u ) == command_registry_status_t::OK );
    EditorCommands_SetObserver( &registry, Observe, &observed );

    REQUIRE( EditorCommands_ExecuteLine( &registry, View( "  map.grid 16 \"two words\" \"q\\\"uote\"  " ) ) == command_result_t::OK );
    REQUIRE( context.nRuns == 1 );
    REQUIRE( context.lastArgs == std::vector<std::string>{ "16", "two words", "q\"uote" } );
    REQUIRE( EditorCommands_State( &registry, View( "map.grid" ) ) == ( COMMAND_STATE_ENABLED | COMMAND_STATE_CHECKED ) );

    context.bEnabled = false;
    REQUIRE( EditorCommands_Execute( &registry, View( "map.grid" ), {} ) == command_result_t::DISABLED );
    REQUIRE( EditorCommands_ExecuteLine( &registry, View( "map.grid 8" ) ) == command_result_t::DISABLED );
    REQUIRE( context.nRuns == 1 );
    REQUIRE( observed.nCalls == 3 );
    REQUIRE( observed.last == command_result_t::DISABLED );

    REQUIRE( EditorCommands_ExecuteLine( &registry, View( "map.nothing" ) ) == command_result_t::UNKNOWN_COMMAND );
    REQUIRE( EditorCommands_ExecuteLine( &registry, View( "map.grid \"open" ) ) == command_result_t::BAD_LINE );
    REQUIRE( EditorCommands_ExecuteLine( &registry, View( "   " ) ) == command_result_t::BAD_LINE );
    REQUIRE( EditorCommands_ExecuteLine( &registry, View( "map.grid 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17" ) ) ==
             command_result_t::BAD_LINE );
    REQUIRE( EditorCommands_State( &registry, View( "map.nothing" ) ) == COMMAND_STATE_NONE );
    EditorCommands_Shutdown( &registry );
}

TEST_CASE( "Commands complete by prefix and unload by module",
           "[CypherEditor][Core][Commands]" )
{
    test_context_t context;
    command_registry_t registry{};
    REQUIRE( EditorCommands_Init( &registry, Allocator_GetSystem() ) == command_registry_status_t::OK );
    const command_desc_t table[]{
        Command( "map.tool.clip", &context ), Command( "map.tool.vertex", &context ), Command( "map.grid", &context ),
        Command( "mapping.export", &context ), Command( "file.save", &context ) };
    REQUIRE( EditorCommands_Register( &registry, table, 5u ) == command_registry_status_t::OK );

    const command_desc_t *matches[4]{};
    REQUIRE( EditorCommands_Complete( &registry, View( "map.tool." ), matches, 4u ) == 2u );
    REQUIRE( std::string( matches[0]->pId ) == "map.tool.clip" );
    REQUIRE( EditorCommands_Complete( &registry, View( "map" ), nullptr, 0u ) == 4u );
    REQUIRE( EditorCommands_Complete( &registry, View( "" ), nullptr, 0u ) == 5u );

    REQUIRE( EditorCommands_UnregisterModule( &registry, View( "map" ) ) == 3u );
    REQUIRE( EditorCommands_Find( &registry, View( "mapping.export" ) ) != nullptr );
    REQUIRE( EditorCommands_Count( &registry ) == 2u );
    EditorCommands_Shutdown( &registry );
}
