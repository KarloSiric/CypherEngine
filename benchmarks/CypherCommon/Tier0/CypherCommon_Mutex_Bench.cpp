//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier0/CypherCommon_Mutex_Bench.cpp
//  Purpose: Benchmarks CypherCommon Tier0 mutex primitive costs.
//  Details: These measurements track uncontended mutual exclusion overhead.
//           Contended behavior should be profiled in real systems and queues.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Mutex.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

namespace
{

void BM_MutexLockUnlock( benchmark::State &state )
{
    cy_mutex_t mutex{};
    if ( !Cy_MutexInit( &mutex ) ) {
        state.SkipWithError( "mutex initialization failed" );
        return;
    }

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_MutexLock( &mutex ) );
        benchmark::DoNotOptimize( &mutex );
        benchmark::DoNotOptimize( Cy_MutexUnlock( &mutex ) );
    }

    if ( !Cy_MutexShutdown( &mutex ) ) {
        state.SkipWithError( "mutex shutdown failed" );
    }
}

void BM_MutexTryLockUnlock( benchmark::State &state )
{
    cy_mutex_t mutex{};
    if ( !Cy_MutexInit( &mutex ) ) {
        state.SkipWithError( "mutex initialization failed" );
        return;
    }

    for ( auto _ : state ) {
        const bool_t bLocked = Cy_MutexTryLock( &mutex );
        benchmark::DoNotOptimize( &bLocked );
        if ( bLocked ) {
            benchmark::DoNotOptimize( Cy_MutexUnlock( &mutex ) );
        }
    }

    if ( !Cy_MutexShutdown( &mutex ) ) {
        state.SkipWithError( "mutex shutdown failed" );
    }
}

void BM_RecursiveMutexLockUnlock( benchmark::State &state )
{
    cy_recursive_mutex_t mutex{};
    if ( !Cy_RecursiveMutexInit( &mutex ) ) {
        state.SkipWithError( "recursive mutex initialization failed" );
        return;
    }

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_RecursiveMutexLock( &mutex ) );
        benchmark::DoNotOptimize( Cy_RecursiveMutexLock( &mutex ) );
        benchmark::DoNotOptimize( &mutex );
        benchmark::DoNotOptimize( Cy_RecursiveMutexUnlock( &mutex ) );
        benchmark::DoNotOptimize( Cy_RecursiveMutexUnlock( &mutex ) );
    }

    if ( !Cy_RecursiveMutexShutdown( &mutex ) ) {
        state.SkipWithError( "recursive mutex shutdown failed" );
    }
}

} // namespace

BENCHMARK( BM_MutexLockUnlock );
BENCHMARK( BM_MutexTryLockUnlock );
BENCHMARK( BM_RecursiveMutexLockUnlock );
