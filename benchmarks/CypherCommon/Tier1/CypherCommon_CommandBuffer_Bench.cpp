//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_CommandBuffer_Bench.cpp
//  Purpose: Benchmarks command queue storage and dispatch preparation.
//  Details: Reserved FIFO cycles isolate line append and delimiter scanning from
//           allocator growth, while compaction measures consumed-prefix movement.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_CommandBuffer.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

static void BM_CommandBufferReservedCycle( benchmark::State &state )
{
    const usize nCommands = static_cast<usize>( state.range( 0 ) );
    const string_view_t command = StringView_FromCString(
        "spawn enemy_grunt wave=12" );
    command_buffer_t buffer{};
    const usize cchCapacity = nCommands * ( command.cchLength + 1u );
    if ( !CommandBuffer_Init(
             &buffer,
             Allocator_GetSystem(),
             cchCapacity ) ) {
        state.SkipWithError( "CommandBuffer initialization failed." );
        return;
    }

    for ( auto _ : state ) {
        for ( usize iCommand = 0u; iCommand < nCommands; ++iCommand ) {
            bool_t bEnqueued = CommandBuffer_Enqueue( &buffer, command );
            benchmark::DoNotOptimize( bEnqueued );
        }
        string_view_t queuedCommand{};
        while ( CommandBuffer_Pop( &buffer, &queuedCommand ) ) {
            benchmark::DoNotOptimize( queuedCommand.pData );
        }
        CommandBuffer_Compact( &buffer );
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( nCommands ) );
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( nCommands ) *
        static_cast<i64>( command.cchLength ) );
}

BENCHMARK( BM_CommandBufferReservedCycle )
    ->Arg( 8 )
    ->Arg( 64 )
    ->Arg( 1024 );

static void BM_CommandBufferCompactHalf( benchmark::State &state )
{
    constexpr usize nCommands = 256u;
    const string_view_t command = StringView_FromCString(
        "set renderer.exposure 1.25" );
    command_buffer_t buffer{};
    if ( !CommandBuffer_Init(
             &buffer,
             Allocator_GetSystem(),
             nCommands * ( command.cchLength + 1u ) ) ) {
        state.SkipWithError( "CommandBuffer initialization failed." );
        return;
    }

    for ( auto _ : state ) {
        CommandBuffer_Clear( &buffer );
        for ( usize iCommand = 0u; iCommand < nCommands; ++iCommand ) {
            benchmark::DoNotOptimize(
                CommandBuffer_Enqueue( &buffer, command ) );
        }
        string_view_t queuedCommand{};
        for ( usize iCommand = 0u; iCommand < nCommands / 2u; ++iCommand ) {
            benchmark::DoNotOptimize(
                CommandBuffer_Pop( &buffer, &queuedCommand ) );
        }
        CommandBuffer_Compact( &buffer );
        benchmark::DoNotOptimize( buffer.text.pData );
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( nCommands / 2u ) *
        static_cast<i64>( command.cchLength + 1u ) );
}

BENCHMARK( BM_CommandBufferCompactHalf );
