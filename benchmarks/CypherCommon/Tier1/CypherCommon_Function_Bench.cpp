//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_Function_Bench.cpp
//  Purpose: Benchmarks owning type-erased callables.
//  Details: Separates steady-state inline invocation from binding and reset costs.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Function.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

namespace
{

u64 FunctionMix( u64 value ) noexcept
{
    return ( value ^ 0x9E3779B97F4A7C15ull ) * 0xBF58476D1CE4E5B9ull;
}

} // namespace

static void BM_Function_InvokeInline( benchmark::State &state )
{
    function_t<u64( u64 )> function{};
    if ( !Function_Init( &function, Allocator_GetSystem() ) ||
         !Function_Bind( &function, FunctionMix ) ) {
        state.SkipWithError( "Function binding failed." );
        return;
    }
    benchmark::DoNotOptimize( function.pfnInvoke );

    u64 value = 1u;
    for ( auto _ : state ) {
        value = Function_Invoke( function, value );
        benchmark::DoNotOptimize( value );
    }
}

BENCHMARK( BM_Function_InvokeInline );

static void BM_Function_BindResetInline( benchmark::State &state )
{
    function_t<u64( u64 )> function{};
    if ( !Function_Init( &function, Allocator_GetSystem() ) ) {
        state.SkipWithError( "Function initialization failed." );
        return;
    }

    for ( auto _ : state ) {
        bool_t bBound = Function_Bind( &function, FunctionMix );
        benchmark::DoNotOptimize( bBound );
        Function_Reset( &function );
    }
}

BENCHMARK( BM_Function_BindResetInline );
