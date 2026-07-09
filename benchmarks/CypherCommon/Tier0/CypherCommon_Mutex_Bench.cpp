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
    Cy_MutexInit( &mutex );

    for ( auto _ : state ) {
        Cy_MutexLock( &mutex );
        benchmark::DoNotOptimize( &mutex );
        Cy_MutexUnlock( &mutex );
    }
}

void BM_MutexTryLockUnlock( benchmark::State &state )
{
    cy_mutex_t mutex{};
    Cy_MutexInit( &mutex );

    for ( auto _ : state ) {
        const bool_t bLocked = Cy_MutexTryLock( &mutex );
        benchmark::DoNotOptimize( &bLocked );
        if ( bLocked ) {
            Cy_MutexUnlock( &mutex );
        }
    }
}

void BM_RecursiveMutexLockUnlock( benchmark::State &state )
{
    cy_recursive_mutex_t mutex{};
    Cy_RecursiveMutexInit( &mutex );

    for ( auto _ : state ) {
        Cy_RecursiveMutexLock( &mutex );
        Cy_RecursiveMutexLock( &mutex );
        benchmark::DoNotOptimize( &mutex );
        Cy_RecursiveMutexUnlock( &mutex );
        Cy_RecursiveMutexUnlock( &mutex );
    }
}

} // namespace

BENCHMARK( BM_MutexLockUnlock );
BENCHMARK( BM_MutexTryLockUnlock );
BENCHMARK( BM_RecursiveMutexLockUnlock );
