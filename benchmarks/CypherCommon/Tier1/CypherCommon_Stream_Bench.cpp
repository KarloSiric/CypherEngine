//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_Stream_Bench.cpp
//  Purpose: Benchmarks callback stream dispatch and contract validation.
//  Details: Measures the abstraction overhead around a fixed-size in-memory transfer,
//           excluding allocation, file-system, compression, and transport costs.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Stream.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

namespace
{

struct stream_bench_state_t {
    byte storage[64]{};
};

stream_io_result_t BenchRead(
    void *pUserData,
    void *pDest,
    usize cbRequested ) noexcept
{
    const auto *pState = static_cast<const stream_bench_state_t *>( pUserData );
    Cy_MemCopy( pDest, pState->storage, cbRequested );
    return { stream_status_t::OK, cbRequested };
}

stream_io_result_t BenchWrite(
    void *pUserData,
    const void *pSource,
    usize cbRequested ) noexcept
{
    auto *pState = static_cast<stream_bench_state_t *>( pUserData );
    Cy_MemCopy( pState->storage, pSource, cbRequested );
    return { stream_status_t::OK, cbRequested };
}

const stream_ops_t BENCH_OPS{ &BenchRead, &BenchWrite };

} // namespace

static void BM_Stream_WriteRead64( benchmark::State &state )
{
    stream_bench_state_t backend{};
    stream_t stream{
        &BENCH_OPS,
        &backend,
        STREAM_CAPABILITY_READ | STREAM_CAPABILITY_WRITE
    };
    byte data[64]{};

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Stream_WriteExact( &stream, data, sizeof( data ) ) );
        benchmark::DoNotOptimize( Stream_ReadExact( &stream, data, sizeof( data ) ) );
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( sizeof( data ) * 2u ) );
}

BENCHMARK( BM_Stream_WriteRead64 );

