//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherMemory/CypherMemory_Bench.cpp
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

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <random>
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

    if ( Mem_ArenaInit( arena, desc ) != mem_error_t::OK ) {
        state.SkipWithError( "Mem_ArenaInit failed." );
        return;
    }

    for ( auto _ : state ) {
        Mem_ArenaReset( arena );
        for ( cypher::engine::common::usize i = 0u; i < kAllocCount; ++i ) {
            void *ptr = Mem_ArenaAlloc( arena, kAllocSize, 16u );
            benchmark::DoNotOptimize( ptr );
        }
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed( state.iterations() * static_cast<benchmark::IterationCount>( kAllocCount ) );
    Mem_ArenaShutdown( arena );
}

void BM_Arena_AllocZero64_ResetEachIteration( benchmark::State &state )
{
    arena_t arena{};
    arena_desc_t desc{};
    desc.name = "bench_arena_zero";
    desc.capacity = kArenaCapacity;
    desc.backing = arena_backing_t::ARENA_HEAP;

    if ( Mem_ArenaInit( arena, desc ) != mem_error_t::OK ) {
        state.SkipWithError( "Mem_ArenaInit failed." );
        return;
    }

    for ( auto _ : state ) {
        Mem_ArenaReset( arena );
        for ( cypher::engine::common::usize i = 0u; i < kAllocCount; ++i ) {
            void *ptr = Mem_ArenaAllocZero( arena, kAllocSize, 16u );
            benchmark::DoNotOptimize( ptr );
        }
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed( state.iterations() * static_cast<benchmark::IterationCount>( kAllocCount ) );
    Mem_ArenaShutdown( arena );
}

void BM_Arena_VirtualWriteClearReset( benchmark::State &state )
{
    const auto bytes = static_cast<cypher::engine::common::usize>( state.range( 0 ) );
    const bool decommit = state.range( 1 ) != 0;
    arena_t arena{};
    arena_desc_t desc{};
    desc.name = "bench_virtual_clear_reset";
    desc.capacity = bytes;
    desc.backing = arena_backing_t::ARENA_VIRTUAL_MEMORY;
    desc.flags = CYPHER_MEMORY_ARENA_FLAG_GROW_COMMIT_ON_ALLOC |
                 CYPHER_MEMORY_ARENA_FLAG_CLEAR_ON_RESET;
    if ( decommit ) {
        desc.flags |= CYPHER_MEMORY_ARENA_FLAG_DECOMMIT_ON_RESET;
    }
    if ( Mem_ArenaInit( arena, desc ) != mem_error_t::OK ) {
        state.SkipWithError( "Mem_ArenaInit failed." );
        return;
    }

    // Time the complete reuse cycle: allocate/commit, dirty the payload, clear
    // used bytes on reset, and optionally decommit. This is not memset alone.
    for ( auto _ : state ) {
        void *payload = Mem_ArenaAlloc( arena, bytes, 16u );
        if ( payload == nullptr ) {
            state.SkipWithError( "Mem_ArenaAlloc failed." );
            break;
        }
        std::memset( payload, 0xA5, bytes );
        benchmark::DoNotOptimize( payload );
        benchmark::ClobberMemory();
        Mem_ArenaReset( arena );
        if ( Mem_ArenaLastError( arena ) != mem_error_t::OK ) {
            state.SkipWithError( "Mem_ArenaReset failed." );
            break;
        }
        benchmark::ClobberMemory();
    }
    // Count logical payload bytes per full reuse cycle, not physical bus traffic.
    state.SetBytesProcessed( state.iterations() * static_cast<benchmark::IterationCount>( bytes ) );
    state.SetItemsProcessed( state.iterations() );
    Mem_ArenaShutdown( arena );
}

void BM_Pool_AllocFree64( benchmark::State &state )
{
    arena_t arena{};
    arena_desc_t arenaDesc{};
    arenaDesc.name = "bench_pool_arena";
    arenaDesc.capacity = kArenaCapacity;
    arenaDesc.backing = arena_backing_t::ARENA_HEAP;

    if ( Mem_ArenaInit( arena, arenaDesc ) != mem_error_t::OK ) {
        state.SkipWithError( "Mem_ArenaInit failed." );
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

    if ( Mem_PoolInit( pool, poolDesc ) != mem_error_t::OK ) {
        Mem_ArenaShutdown( arena );
        state.SkipWithError( "Mem_PoolInit failed." );
        return;
    }

    std::vector<void *> pointers;
    pointers.resize( kAllocCount );

    for ( auto _ : state ) {
        for ( cypher::engine::common::usize i = 0u; i < kAllocCount; ++i ) {
            pointers[i] = Mem_PoolAlloc( pool );
            if ( pointers[i] == nullptr ) {
                state.SkipWithError( "Mem_PoolAlloc failed." );
                Mem_PoolShutdown( pool );
                Mem_ArenaShutdown( arena );
                return;
            }
            benchmark::DoNotOptimize( pointers[i] );
        }
        for ( cypher::engine::common::usize i = 0u; i < kAllocCount; ++i ) {
            mem_error_t result = Mem_PoolFree( pool, pointers[i] );
            if ( result != mem_error_t::OK ) {
                state.SkipWithError( "Mem_PoolFree failed." );
                Mem_PoolShutdown( pool );
                Mem_ArenaShutdown( arena );
                return;
            }
            benchmark::DoNotOptimize( result );
        }
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed( state.iterations() * static_cast<benchmark::IterationCount>( kAllocCount * 2u ) );
    Mem_PoolShutdown( pool );
    Mem_ArenaShutdown( arena );
}

void BM_Pool_AllocFree64_ShuffledFree( benchmark::State &state )
{
    const auto slotCount = static_cast<cypher::engine::common::usize>( state.range( 0 ) );

    // Setup and permutation generation happen before timing starts. Reusing one
    // permutation makes the workload repeatable without timing a random generator.
    std::vector<void *> pointers( slotCount );
    std::vector<cypher::engine::common::usize> freeOrder( slotCount );
    std::iota( freeOrder.begin(), freeOrder.end(), cypher::engine::common::usize{ 0u } );
    std::mt19937 random( 0xC1F3u );
    std::shuffle( freeOrder.begin(), freeOrder.end(), random );

    arena_t arena{};
    arena_desc_t arenaDesc{};
    arenaDesc.name = "bench_pool_shuffled_arena";
    arenaDesc.capacity = kArenaCapacity;
    arenaDesc.backing = arena_backing_t::ARENA_HEAP;

    if ( Mem_ArenaInit( arena, arenaDesc ) != mem_error_t::OK ) {
        state.SkipWithError( "Mem_ArenaInit failed." );
        return;
    }

    pool_t pool{};
    pool_desc_t poolDesc{};
    poolDesc.name = "bench_pool_shuffled";
    poolDesc.arena = &arena;
    poolDesc.nSlotSize = kAllocSize;
    poolDesc.nSlotCount = slotCount;
    poolDesc.alignment = 16u;
    poolDesc.backing = pool_backing_t::POOL_ARENA;

    if ( Mem_PoolInit( pool, poolDesc ) != mem_error_t::OK ) {
        Mem_ArenaShutdown( arena );
        state.SkipWithError( "Mem_PoolInit failed." );
        return;
    }

    // Each batch fills the pool, then frees every live slot in shuffled order.
    // The resulting intrusive free list changes the next batch's reuse locality.
    // No caller payload is read or written, matching the ordered pool benchmark.
    for ( auto _ : state ) {
        for ( cypher::engine::common::usize i = 0u; i < slotCount; ++i ) {
            pointers[i] = Mem_PoolAlloc( pool );
            if ( pointers[i] == nullptr ) {
                state.SkipWithError( "Mem_PoolAlloc failed." );
                Mem_PoolShutdown( pool );
                Mem_ArenaShutdown( arena );
                return;
            }
            benchmark::DoNotOptimize( pointers[i] );
        }
        for ( const auto index : freeOrder ) {
            mem_error_t result = Mem_PoolFree( pool, pointers[index] );
            if ( result != mem_error_t::OK ) {
                state.SkipWithError( "Mem_PoolFree failed." );
                Mem_PoolShutdown( pool );
                Mem_ArenaShutdown( arena );
                return;
            }
            benchmark::DoNotOptimize( result );
        }
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed( state.iterations() * static_cast<benchmark::IterationCount>( slotCount * 2u ) );
    Mem_PoolShutdown( pool );
    Mem_ArenaShutdown( arena );
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
BENCHMARK( BM_Arena_VirtualWriteClearReset )
    ->Args( { 64 * 1024, 0 } )->Args( { 64 * 1024, 1 } )
    ->Args( { 1024 * 1024, 0 } )->Args( { 1024 * 1024, 1 } )
    ->ArgNames( { "bytes", "decommit" } );
BENCHMARK( BM_Pool_AllocFree64 );
BENCHMARK( BM_Pool_AllocFree64_ShuffledFree )->Arg( 64 )->Arg( 1024 )->Arg( 16384 );
BENCHMARK( BM_MallocFree64 );
BENCHMARK( BM_CallocFree64 );
