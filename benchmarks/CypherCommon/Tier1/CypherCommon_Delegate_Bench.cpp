//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_Delegate_Bench.cpp
//  Purpose: Benchmarks non-owning delegate dispatch.
//  Details: Compares one erased thunk call against an equivalent raw function-pointer
//           call so callback abstraction cost remains visible.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Delegate.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

namespace
{

u64 MixValue( u64 value ) noexcept
{
    return ( value ^ 0x9E3779B97F4A7C15ull ) * 0xBF58476D1CE4E5B9ull;
}

} // namespace

static void BM_Delegate_Invoke( benchmark::State &state )
{
    delegate_t<u64( u64 )> delegate =
        Delegate_BindFunction<u64( u64 ), MixValue>();
    benchmark::DoNotOptimize( delegate.pfnInvoke );
    u64 value = 1u;
    for ( auto _ : state ) {
        value = Delegate_Invoke( delegate, value );
        benchmark::DoNotOptimize( value );
    }
}

BENCHMARK( BM_Delegate_Invoke );

static void BM_Delegate_RawFunctionPointer( benchmark::State &state )
{
    u64 ( *pfnCall )( u64 ) noexcept = MixValue;
    benchmark::DoNotOptimize( pfnCall );
    u64 value = 1u;
    for ( auto _ : state ) {
        value = pfnCall( value );
        benchmark::DoNotOptimize( value );
    }
}

BENCHMARK( BM_Delegate_RawFunctionPointer );
