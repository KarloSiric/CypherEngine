//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_HashTable_Bench.cpp
//  Purpose: Benchmarks allocator-backed hash-table operations.
//  Details: Measures steady-state hit, miss, and reserved insertion workloads and
//           keeps a standard-library reference in the benchmark suite for context.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_HashTable.h"

#include <benchmark/benchmark.h>
#include <unordered_map>

using namespace cypher::common;

namespace
{

struct identity_hash_t {
    hash64_t operator()( u32 nValue ) const noexcept
    {
        return nValue;
    }
};

struct mixed_hash_t {
    hash64_t operator()( u32 nValue ) const noexcept
    {
        return hash_functor_t<u32>{}( nValue );
    }
};

void BM_HashTable_FindHit( benchmark::State &state )
{
    const usize nCount = static_cast<usize>( state.range( 0 ) );
    hash_table_t<u32, u32> table{};
    if ( !HashTable_Init( &table, Allocator_GetSystem(), nCount ) ) {
        state.SkipWithError( "HashTable initialization failed." );
        return;
    }
    for ( usize iKey = 0u; iKey < nCount; ++iKey ) {
        const u32 nKey = static_cast<u32>( iKey );
        if ( !HashTable_Insert( &table, nKey, nKey ).bInserted ) {
            state.SkipWithError( "HashTable population failed." );
            return;
        }
    }

    usize iKey = 0u;
    for ( auto _ : state ) {
        const u32 *pValue = HashTable_Find(
            &table,
            static_cast<u32>( iKey & ( nCount - 1u ) ) );
        benchmark::DoNotOptimize( pValue );
        iKey += 17u;
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

void BM_StdUnorderedMap_FindHit( benchmark::State &state )
{
    const usize nCount = static_cast<usize>( state.range( 0 ) );
    std::unordered_map<u32, u32> table;
    table.reserve( nCount );
    for ( usize iKey = 0u; iKey < nCount; ++iKey ) {
        const u32 nKey = static_cast<u32>( iKey );
        table.emplace( nKey, nKey );
    }

    usize iKey = 0u;
    for ( auto _ : state ) {
        auto found = table.find(
            static_cast<u32>( iKey & ( nCount - 1u ) ) );
        benchmark::DoNotOptimize( found );
        iKey += 17u;
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

template <typename hasher_t>
void BM_StdUnorderedMapPolicy_FindHit( benchmark::State &state )
{
    const usize nCount = static_cast<usize>( state.range( 0 ) );
    std::unordered_map<u32, u32, hasher_t> table;
    table.reserve( nCount );
    for ( usize iKey = 0u; iKey < nCount; ++iKey ) {
        const u32 nKey = static_cast<u32>( iKey );
        table.emplace( nKey, nKey );
    }

    usize iKey = 0u;
    for ( auto _ : state ) {
        auto found = table.find(
            static_cast<u32>( iKey & ( nCount - 1u ) ) );
        benchmark::DoNotOptimize( found );
        iKey += 17u;
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

void BM_HashTableIdentity_FindHit( benchmark::State &state )
{
    const usize nCount = static_cast<usize>( state.range( 0 ) );
    hash_table_t<u32, u32, identity_hash_t> table{};
    if ( !HashTable_Init( &table, Allocator_GetSystem(), nCount ) ) {
        state.SkipWithError( "HashTable initialization failed." );
        return;
    }
    for ( usize iKey = 0u; iKey < nCount; ++iKey ) {
        const u32 nKey = static_cast<u32>( iKey );
        if ( !HashTable_Insert( &table, nKey, nKey ).bInserted ) {
            state.SkipWithError( "HashTable population failed." );
            return;
        }
    }

    usize iKey = 0u;
    for ( auto _ : state ) {
        const u32 *pValue = HashTable_Find(
            &table,
            static_cast<u32>( iKey & ( nCount - 1u ) ) );
        benchmark::DoNotOptimize( pValue );
        iKey += 17u;
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

void BM_HashTable_FindMiss( benchmark::State &state )
{
    const usize nCount = static_cast<usize>( state.range( 0 ) );
    hash_table_t<u32, u32> table{};
    if ( !HashTable_Init( &table, Allocator_GetSystem(), nCount ) ) {
        state.SkipWithError( "HashTable initialization failed." );
        return;
    }
    for ( usize iKey = 0u; iKey < nCount; ++iKey ) {
        const u32 nKey = static_cast<u32>( iKey );
        if ( !HashTable_Insert( &table, nKey, nKey ).bInserted ) {
            state.SkipWithError( "HashTable population failed." );
            return;
        }
    }

    usize iKey = 0u;
    for ( auto _ : state ) {
        const u32 *pValue = HashTable_Find(
            &table,
            static_cast<u32>( nCount + iKey ) );
        benchmark::DoNotOptimize( pValue );
        iKey = ( iKey + 17u ) & ( nCount - 1u );
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

void BM_HashTable_ReservedInsert( benchmark::State &state )
{
    const usize nCount = static_cast<usize>( state.range( 0 ) );
    hash_table_t<u32, u32> table{};
    if ( !HashTable_Init( &table, Allocator_GetSystem(), nCount ) ) {
        state.SkipWithError( "HashTable initialization failed." );
        return;
    }

    for ( auto _ : state ) {
        state.PauseTiming();
        HashTable_Clear( &table );
        state.ResumeTiming();

        for ( usize iKey = 0u; iKey < nCount; ++iKey ) {
            const u32 nKey = static_cast<u32>( iKey );
            benchmark::DoNotOptimize(
                HashTable_Insert( &table, nKey, nKey ).pValue );
        }
    }
    state.SetItemsProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( nCount ) );
}

} // namespace

BENCHMARK( BM_HashTable_FindHit )->Arg( 256 )->Arg( 4096 )->Arg( 65536 );
BENCHMARK( BM_HashTableIdentity_FindHit )->Arg( 256 )->Arg( 4096 )->Arg( 65536 );
BENCHMARK( BM_StdUnorderedMap_FindHit )->Arg( 256 )->Arg( 4096 )->Arg( 65536 );
BENCHMARK_TEMPLATE( BM_StdUnorderedMapPolicy_FindHit, mixed_hash_t )
    ->Name( "BM_StdUnorderedMapMixed_FindHit" )
    ->Arg( 256 )->Arg( 4096 )->Arg( 65536 );
BENCHMARK_TEMPLATE( BM_StdUnorderedMapPolicy_FindHit, identity_hash_t )
    ->Name( "BM_StdUnorderedMapIdentity_FindHit" )
    ->Arg( 256 )->Arg( 4096 )->Arg( 65536 );
BENCHMARK( BM_HashTable_FindMiss )->Arg( 256 )->Arg( 4096 )->Arg( 65536 );
BENCHMARK( BM_HashTable_ReservedInsert )->Arg( 256 )->Arg( 4096 )->Arg( 65536 );
