//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_CommandSystem_Bench.cpp
//  Purpose: Benchmarks command-registry runtime operations.
//  Details: Measures name lookup, command parse-and-dispatch, direct ConVar writes,
//           and console-line ConVar writes over allocator-owned registry state.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_CommandSystem.h"

#include <benchmark/benchmark.h>

#include <cstdio>
#include <string>
#include <vector>

using namespace cypher::common;

namespace
{

constexpr usize CY_COMMAND_SYSTEM_BENCH_ENTRY_COUNT = 1024u;

error_code_t NoOpCommand(
    const command_context_t &,
    const command_args_t &args,
    void * ) noexcept
{
    usize nArgumentCount = args.nArgumentCount;
    benchmark::DoNotOptimize( nArgumentCount );
    return CY_ERROR_OK;
}

std::vector<std::string> MakeCommandNames()
{
    std::vector<std::string> names;
    names.reserve( CY_COMMAND_SYSTEM_BENCH_ENTRY_COUNT );
    for ( usize iName = 0u;
          iName < CY_COMMAND_SYSTEM_BENCH_ENTRY_COUNT;
          ++iName ) {
        char name[32]{};
        std::snprintf( name, sizeof( name ), "command_%04zu", iName );
        names.emplace_back( name );
    }
    return names;
}

command_system_t *MakePopulatedCommandSystem(
    const std::vector<std::string> &names )
{
    command_system_desc_t systemDesc{};
    systemDesc.nInitialCommands = names.size();
    systemDesc.nInitialConVars = 8u;
    command_system_t *pSystem = CommandSystem_Create( systemDesc );
    if ( pSystem == nullptr ) {
        return nullptr;
    }

    for ( const std::string &name : names ) {
        const concommand_desc_t desc{
            StringView_FromRange( name.data(), name.size() ),
            {},
            {},
            CONCOMMAND_FLAG_NONE,
            NoOpCommand
        };
        if ( Cy_ErrorFailed(
                 CommandSystem_RegisterCommand( pSystem, desc ).error ) ) {
            CommandSystem_Destroy( pSystem );
            return nullptr;
        }
    }
    return pSystem;
}

convar_register_result_t RegisterWidthConVar( command_system_t *pSystem )
{
    return CommandSystem_RegisterConVar(
        pSystem,
        convar_desc_t{
            StringView_FromCString( "r_width" ),
            {},
            convar_type_t::I64,
            StringView_FromCString( "640" ),
            StringView_FromCString( "320" ),
            StringView_FromCString( "7680" )
        } );
}

} // namespace

static void BM_CommandSystem_FindCommand1024( benchmark::State &state )
{
    const std::vector<std::string> names = MakeCommandNames();
    command_system_t *pSystem = MakePopulatedCommandSystem( names );
    if ( pSystem == nullptr ) {
        state.SkipWithError( "CommandSystem population failed." );
        return;
    }

    usize iName = 0u;
    for ( auto _ : state ) {
        const std::string &name = names[iName];
        const command_handle_t handle = CommandSystem_FindCommand(
            pSystem,
            StringView_FromRange( name.data(), name.size() ) );
        u32 nHandleValue = handle.value;
        benchmark::DoNotOptimize( nHandleValue );
        iName = ( iName + 17u ) & ( names.size() - 1u );
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
    CommandSystem_Destroy( pSystem );
}

static void BM_CommandSystem_ExecuteCommand( benchmark::State &state )
{
    command_system_desc_t systemDesc{};
    systemDesc.nInitialCommands = 8u;
    command_system_t *pSystem = CommandSystem_Create( systemDesc );
    const concommand_desc_t desc{
        StringView_FromCString( "bench.run" ),
        {},
        {},
        CONCOMMAND_FLAG_NONE,
        NoOpCommand
    };
    if ( pSystem == nullptr ||
         Cy_ErrorFailed( CommandSystem_RegisterCommand( pSystem, desc ).error ) ) {
        state.SkipWithError( "Command registration failed." );
        CommandSystem_Destroy( pSystem );
        return;
    }

    const string_view_t line = StringView_FromCString( "bench.run 42" );
    const command_context_t context{};
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( CommandSystem_ExecuteLine(
            pSystem,
            line,
            context ) );
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
    CommandSystem_Destroy( pSystem );
}

static void BM_CommandSystem_SetConVar( benchmark::State &state )
{
    command_system_t *pSystem = CommandSystem_Create( {} );
    const convar_register_result_t registered = RegisterWidthConVar( pSystem );
    if ( pSystem == nullptr || Cy_ErrorFailed( registered.error ) ) {
        state.SkipWithError( "ConVar registration failed." );
        CommandSystem_Destroy( pSystem );
        return;
    }

    const string_view_t values[] = {
        StringView_FromCString( "640" ),
        StringView_FromCString( "641" )
    };
    const command_context_t context{};
    usize iValue = 0u;
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( CommandSystem_SetConVar(
            pSystem,
            registered.handle,
            values[iValue],
            context ) );
        iValue ^= 1u;
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
    CommandSystem_Destroy( pSystem );
}

static void BM_CommandSystem_ExecuteConVarSet( benchmark::State &state )
{
    command_system_t *pSystem = CommandSystem_Create( {} );
    const convar_register_result_t registered = RegisterWidthConVar( pSystem );
    if ( pSystem == nullptr || Cy_ErrorFailed( registered.error ) ) {
        state.SkipWithError( "ConVar registration failed." );
        CommandSystem_Destroy( pSystem );
        return;
    }

    const string_view_t lines[] = {
        StringView_FromCString( "r_width 640" ),
        StringView_FromCString( "r_width 641" )
    };
    const command_context_t context{};
    usize iLine = 0u;
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( CommandSystem_ExecuteLine(
            pSystem,
            lines[iLine],
            context ) );
        iLine ^= 1u;
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
    CommandSystem_Destroy( pSystem );
}

BENCHMARK( BM_CommandSystem_FindCommand1024 );
BENCHMARK( BM_CommandSystem_ExecuteCommand );
BENCHMARK( BM_CommandSystem_SetConVar );
BENCHMARK( BM_CommandSystem_ExecuteConVarSet );
