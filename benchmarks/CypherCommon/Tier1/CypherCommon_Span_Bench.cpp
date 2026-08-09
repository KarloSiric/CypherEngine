//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_Span_Bench.cpp
//  Purpose: Benchmarks Tier1 span traversal against direct pointer traversal.
//  Details: Confirms that release-mode range wrappers compile down to the same
//           inner-loop work as raw pointer/count access for representative sizes.
//
//  History:
//  - Created by Karlo Siric on 2026-08-08
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Span.h"

#include <benchmark/benchmark.h>

#include <array>

using namespace cypher::common;

namespace
{

constexpr usize CY_SPAN_BENCH_MAX_COUNT = 4096u;

std::array<u32, CY_SPAN_BENCH_MAX_COUNT> MakeValues() noexcept
{
    std::array<u32, CY_SPAN_BENCH_MAX_COUNT> values{};
    for ( usize i = 0u; i < values.size(); ++i ) {
        values[i] = static_cast<u32>( i * 17u + 3u );
    }
    return values;
}

const std::array<u32, CY_SPAN_BENCH_MAX_COUNT> g_values = MakeValues();

} // namespace

static void BM_SpanTraversal( benchmark::State &state )
{
    const usize nCount = static_cast<usize>( state.range( 0 ) );
    const span_t<const u32> values = Span_Make( g_values.data(), nCount );

    for ( auto _ : state ) {
        u64 nSum = 0u;
        for ( usize i = 0u; i < nCount; ++i ) {
            nSum += *Span_At( values, i );
        }
        benchmark::DoNotOptimize( nSum );
    }

    state.SetItemsProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( nCount ) );
}

static void BM_RawPointerTraversal( benchmark::State &state )
{
    const usize nCount = static_cast<usize>( state.range( 0 ) );
    const u32 *pValues = g_values.data();

    for ( auto _ : state ) {
        u64 nSum = 0u;
        for ( usize i = 0u; i < nCount; ++i ) {
            nSum += pValues[i];
        }
        benchmark::DoNotOptimize( nSum );
    }

    state.SetItemsProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( nCount ) );
}

BENCHMARK( BM_SpanTraversal )->Arg( 16 )->Arg( 256 )->Arg( 4096 );
BENCHMARK( BM_RawPointerTraversal )->Arg( 16 )->Arg( 256 )->Arg( 4096 );
