//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherEngine/CypherMemory/CypherMemory_Bench.cpp
//  Purpose: Benchmarks Memory Bench performance.
//  Details: This benchmark measures runtime cost for the corresponding low-level
//           path. Results should be treated as signals and compared across build
//           modes and platforms.
//
//  History:
//  - Created by Karlo Siric on 2026-07-03
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMemory_Arena.h"
#include "CypherMemory_Pool.h"

#include <benchmark/benchmark.h>

#include <cstdlib>
#include <vector>

using namespace cypher::engine::memory;

namespace
{

constexpr cypher::engine::common::usize kArenaCapacity = 8u * 1024u * 1024u;
constexpr cypher::engine::common::usize kAllocCount = 1024u;
constexpr cypher::engine::common::usize kAllocSize = 64u;

void BM_Arena_Alloc64_ResetEachIteration( benchmark::State &state )
{
    arena_t arena{};
    arena_desc_t desc{};
    desc.name = "bench_arena";
    desc.capacity = kArenaCapacity;
    desc.backing = arena_backing_t::ARENA_HEAP;

    if ( CypherMemory_ArenaInit( arena, desc ) != mem_error_t::OK ) {
        state.SkipWithError( "CypherMemory_ArenaInit failed." );
        return;
    }

    for ( auto _ : state ) {
        CypherMemory_ArenaReset( arena );
        for ( cypher::engine::common::usize i = 0u; i < kAllocCount; ++i ) {
            void *ptr = CypherMemory_ArenaAlloc( arena, kAllocSize, 16u );
            benchmark::DoNotOptimize( ptr );
        }
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed( state.iterations() * static_cast<benchmark::IterationCount>( kAllocCount ) );
    CypherMemory_ArenaShutdown( arena );
}

void BM_Arena_AllocZero64_ResetEachIteration( benchmark::State &state )
{
    arena_t arena{};
    arena_desc_t desc{};
    desc.name = "bench_arena_zero";
    desc.capacity = kArenaCapacity;
    desc.backing = arena_backing_t::ARENA_HEAP;

    if ( CypherMemory_ArenaInit( arena, desc ) != mem_error_t::OK ) {
        state.SkipWithError( "CypherMemory_ArenaInit failed." );
        return;
    }

    for ( auto _ : state ) {
        CypherMemory_ArenaReset( arena );
        for ( cypher::engine::common::usize i = 0u; i < kAllocCount; ++i ) {
            void *ptr = CypherMemory_ArenaAllocZero( arena, kAllocSize, 16u );
            benchmark::DoNotOptimize( ptr );
        }
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed( state.iterations() * static_cast<benchmark::IterationCount>( kAllocCount ) );
    CypherMemory_ArenaShutdown( arena );
}

void BM_Pool_AllocFree64( benchmark::State &state )
{
    arena_t arena{};
    arena_desc_t arenaDesc{};
    arenaDesc.name = "bench_pool_arena";
    arenaDesc.capacity = kArenaCapacity;
    arenaDesc.backing = arena_backing_t::ARENA_HEAP;

    if ( CypherMemory_ArenaInit( arena, arenaDesc ) != mem_error_t::OK ) {
        state.SkipWithError( "CypherMemory_ArenaInit failed." );
        return;
    }

    pool_t pool{};
    pool_desc_t poolDesc{};
    poolDesc.name = "bench_pool";
    poolDesc.arena = &arena;
    poolDesc.nSlotSize = kAllocSize;
    poolDesc.nSlotCount = kAllocCount;
    poolDesc.alignment = 16u;
    poolDesc.backing = pool_backing_t::POOL_ARENA;

    if ( CypherMemory_PoolInit( pool, poolDesc ) != mem_error_t::OK ) {
        CypherMemory_ArenaShutdown( arena );
        state.SkipWithError( "CypherMemory_PoolInit failed." );
        return;
    }

    std::vector<void *> pointers;
    pointers.resize( kAllocCount );

    for ( auto _ : state ) {
        for ( cypher::engine::common::usize i = 0u; i < kAllocCount; ++i ) {
            pointers[i] = CypherMemory_PoolAlloc( pool );
            benchmark::DoNotOptimize( pointers[i] );
        }
        for ( cypher::engine::common::usize i = 0u; i < kAllocCount; ++i ) {
            benchmark::DoNotOptimize( CypherMemory_PoolFree( pool, pointers[i] ) );
        }
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed( state.iterations() * static_cast<benchmark::IterationCount>( kAllocCount * 2u ) );
    CypherMemory_PoolShutdown( pool );
    CypherMemory_ArenaShutdown( arena );
}

void BM_MallocFree64( benchmark::State &state )
{
    std::vector<void *> pointers;
    pointers.resize( kAllocCount );

    for ( auto _ : state ) {
        for ( cypher::engine::common::usize i = 0u; i < kAllocCount; ++i ) {
            pointers[i] = std::malloc( kAllocSize );
            if ( pointers[i] == nullptr ) {
                state.SkipWithError( "std::malloc failed." );
                return;
            }
            benchmark::DoNotOptimize( pointers[i] );
        }
        for ( cypher::engine::common::usize i = 0u; i < kAllocCount; ++i ) {
            std::free( pointers[i] );
            pointers[i] = nullptr;
        }
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed( state.iterations() * static_cast<benchmark::IterationCount>( kAllocCount * 2u ) );
}

void BM_CallocFree64( benchmark::State &state )
{
    std::vector<void *> pointers;
    pointers.resize( kAllocCount );

    for ( auto _ : state ) {
        for ( cypher::engine::common::usize i = 0u; i < kAllocCount; ++i ) {
            pointers[i] = std::calloc( 1u, kAllocSize );
            if ( pointers[i] == nullptr ) {
                state.SkipWithError( "std::calloc failed." );
                return;
            }
            benchmark::DoNotOptimize( pointers[i] );
        }
        for ( cypher::engine::common::usize i = 0u; i < kAllocCount; ++i ) {
            std::free( pointers[i] );
            pointers[i] = nullptr;
        }
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed( state.iterations() * static_cast<benchmark::IterationCount>( kAllocCount * 2u ) );
}

} // namespace

BENCHMARK( BM_Arena_Alloc64_ResetEachIteration );
BENCHMARK( BM_Arena_AllocZero64_ResetEachIteration );
BENCHMARK( BM_Pool_AllocFree64 );
BENCHMARK( BM_MallocFree64 );
BENCHMARK( BM_CallocFree64 );
