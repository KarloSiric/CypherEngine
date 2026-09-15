//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherSystem/CypherSystem_Bench.cpp
//  Purpose: Measures hot, deterministic CypherSystem operations.
//  Details: OS scheduling, sleep, window creation, and dynamic-library calls are
//           intentionally excluded because their timings primarily describe the
//           host operating system rather than CypherSystem implementation cost.
//
//  History:
//  - Created by Karlo Siric on 2026-08-29
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherSystem_Public.h"

#include <benchmark/benchmark.h>

using namespace cypher::engine::sys;

namespace
{

sys_event_t MakeKeyEvent() noexcept
{
    sys_event_t event{};
    event.type = sys_event_type_t::KEY;
    event.timestampNanoseconds = 1u;
    event.payload.key.windowId = 1u;
    event.payload.key.key = sys_key_t::A;
    event.payload.key.action = sys_input_action_t::PRESSED;
    event.payload.key.modifiers = SYS_KEYMODIFIER_NONE;
    return event;
}

void BM_Sys_TimeNowNanoseconds( benchmark::State &state )
{
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Sys_TimeNowNanoseconds() );
    }

    state.SetItemsProcessed( state.iterations() );
}

void BM_Sys_TimeNowSeconds( benchmark::State &state )
{
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Sys_TimeNowSeconds() );
    }

    state.SetItemsProcessed( state.iterations() );
}

void BM_Sys_PathBasename( benchmark::State &state )
{
    constexpr const char *PATH =
        "/Users/developer/projects/cypher/base/materials/facility/wall_panel_01.cymat";

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Sys_PathBasename( PATH ) );
    }

    state.SetItemsProcessed( state.iterations() );
}

void BM_Sys_EventQueue_QueuePoll( benchmark::State &state )
{
    const sys_event_t input = MakeKeyEvent();
    sys_event_t output{};
    Sys_ClearEvents();

    for ( auto _ : state ) {
        bool queued = Sys_QueueEvent( input );
        bool polled = Sys_PollEvent( output );
        benchmark::DoNotOptimize( queued );
        benchmark::DoNotOptimize( polled );
        benchmark::DoNotOptimize( output );
    }

    Sys_ClearEvents();
    state.SetItemsProcessed( state.iterations() * 2 );
}

void BM_Sys_EventQueue_Burst( benchmark::State &state )
{
    const auto eventCount = static_cast<cypher::common::u32>( state.range( 0 ) );
    const sys_event_t input = MakeKeyEvent();
    sys_event_t output{};

    for ( auto _ : state ) {
        Sys_ClearEvents();
        for ( cypher::common::u32 eventIndex = 0u; eventIndex < eventCount; ++eventIndex ) {
            benchmark::DoNotOptimize( Sys_QueueEvent( input ) );
        }
        for ( cypher::common::u32 eventIndex = 0u; eventIndex < eventCount; ++eventIndex ) {
            benchmark::DoNotOptimize( Sys_PollEvent( output ) );
        }
        benchmark::DoNotOptimize( output );
        benchmark::ClobberMemory();
    }

    Sys_ClearEvents();
    state.SetItemsProcessed(
        state.iterations() * static_cast<benchmark::IterationCount>( eventCount ) * 2 );
}

} // namespace

BENCHMARK( BM_Sys_TimeNowNanoseconds );
BENCHMARK( BM_Sys_TimeNowSeconds );
BENCHMARK( BM_Sys_PathBasename );
BENCHMARK( BM_Sys_EventQueue_QueuePoll );
BENCHMARK( BM_Sys_EventQueue_Burst )->Arg( 16 )->Arg( 64 )->Arg( SYS_EVENT_QUEUE_CAPACITY );
