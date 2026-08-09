//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_Blob_Bench.cpp
//  Purpose: Benchmarks owning byte storage append and growth.
//  Details: Measures reserved copy bandwidth separately from geometric-growth
//           lifecycle cost for representative asset and packet payload sizes.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Blob.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

static void BM_BlobAssignReserved( benchmark::State &state )
{
    const usize cbSize = static_cast<usize>( state.range( 0 ) );
    blob_t source{};
    blob_t destination{};
    bool_t bSourceReady = Blob_Init(
        &source,
        Allocator_GetSystem(),
        cbSize );
    bool_t bDestinationReady = Blob_Init(
        &destination,
        Allocator_GetSystem(),
        cbSize );
    if ( !bSourceReady || !bDestinationReady ||
         !Blob_Resize( &source, cbSize, 0xA5u ) ) {
        state.SkipWithError( "Blob benchmark initialization failed." );
        return;
    }

    const binary_block_t sourceBlock = Blob_Block( &source );
    for ( auto _ : state ) {
        bool_t bAssigned = Blob_Assign( &destination, sourceBlock );
        benchmark::DoNotOptimize( bAssigned );
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( cbSize ) );
}

BENCHMARK( BM_BlobAssignReserved )
    ->Arg( 64 )
    ->Arg( 4096 )
    ->Arg( 65536 );

static void BM_BlobAppendWithGrowth( benchmark::State &state )
{
    constexpr usize cbChunkSize = 64u;
    byte source[cbChunkSize]{};
    const usize nChunks = static_cast<usize>( state.range( 0 ) );

    for ( auto _ : state ) {
        blob_t blob{};
        bool_t bInitialized = Blob_Init( &blob, Allocator_GetSystem() );
        benchmark::DoNotOptimize( bInitialized );
        for ( usize iChunk = 0u; iChunk < nChunks; ++iChunk ) {
            bool_t bAppended = Blob_Append(
                &blob,
                BinaryBlock_FromData( source, sizeof( source ) ) );
            benchmark::DoNotOptimize( bAppended );
        }
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( nChunks * cbChunkSize ) );
}

BENCHMARK( BM_BlobAppendWithGrowth )
    ->Arg( 4 )
    ->Arg( 64 );
