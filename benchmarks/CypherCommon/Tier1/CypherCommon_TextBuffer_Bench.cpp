//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_TextBuffer_Bench.cpp
//  Purpose: Benchmarks allocator-backed text mutation.
//  Details: Separates reuse of reserved storage, geometric append growth, and
//           structural replacement to expose allocation and movement costs.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_TextBuffer.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

static void BM_TextBufferReservedAssign( benchmark::State &state )
{
    const string_view_t text = StringView_FromCString(
        "materials/facility/wall_panel_damaged.cymat" );
    text_buffer_t buffer{};
    if ( !TextBuffer_Init( &buffer, Allocator_GetSystem(), 128u ) ) {
        state.SkipWithError( "TextBuffer initialization failed." );
        return;
    }

    for ( auto _ : state ) {
        bool_t bAssigned = TextBuffer_Assign( &buffer, text );
        benchmark::DoNotOptimize( bAssigned );
        benchmark::DoNotOptimize( buffer.pData );
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( text.cchLength ) );
}

BENCHMARK( BM_TextBufferReservedAssign );

static void BM_TextBufferAppendWithGrowth( benchmark::State &state )
{
    const usize cFragments = static_cast<usize>( state.range( 0 ) );
    const string_view_t fragment = StringView_FromCString( "entity/" );

    for ( auto _ : state ) {
        text_buffer_t buffer{};
        bool_t bInitialized =
            TextBuffer_Init( &buffer, Allocator_GetSystem() );
        benchmark::DoNotOptimize( bInitialized );
        for ( usize iFragment = 0u; iFragment < cFragments; ++iFragment ) {
            bool_t bAppended = TextBuffer_Append( &buffer, fragment );
            benchmark::DoNotOptimize( bAppended );
        }
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( cFragments ) *
        static_cast<i64>( fragment.cchLength ) );
}

BENCHMARK( BM_TextBufferAppendWithGrowth )
    ->Arg( 8 )
    ->Arg( 64 );

static void BM_TextBufferReplaceMiddle( benchmark::State &state )
{
    const string_view_t initial = StringView_FromCString(
        "shaders/facility/world_geometry_default" );
    const string_view_t replacement = StringView_FromCString( "weapon" );
    text_buffer_t buffer{};
    if ( !TextBuffer_Init( &buffer, Allocator_GetSystem(), 128u ) ) {
        state.SkipWithError( "TextBuffer initialization failed." );
        return;
    }

    for ( auto _ : state ) {
        bool_t bAssigned = TextBuffer_Assign( &buffer, initial );
        bool_t bReplaced = TextBuffer_Replace(
            &buffer,
            8u,
            8u,
            replacement );
        benchmark::DoNotOptimize( bAssigned );
        benchmark::DoNotOptimize( bReplaced );
        benchmark::ClobberMemory();
    }
}

BENCHMARK( BM_TextBufferReplaceMiddle );
