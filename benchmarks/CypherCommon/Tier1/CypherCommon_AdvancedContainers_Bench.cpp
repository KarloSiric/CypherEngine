//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_AdvancedContainers_Bench.cpp
//  Purpose: Benchmarks Tier1 ordered, sparse, priority, and search primitives.
//  Details: Measures steady-state lookup, dense iteration, and heap churn after
//           setup so results represent the container operation under test.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Dictionary.h"
#include "CypherCommon_PriorityQueue.h"
#include "CypherCommon_RBTree.h"
#include "CypherCommon_Search.h"
#include "CypherCommon_SparseSet.h"

#include <benchmark/benchmark.h>

#include <array>
#include <cstdio>
#include <map>
#include <queue>
#include <vector>

using namespace cypher::common;

namespace
{

void BM_RBTree_FindHit( benchmark::State &state )
{
    const usize nCount = static_cast<usize>( state.range( 0 ) );
    rb_tree_t<u32, u32> tree{};
    if ( !RBTree_Init( &tree, Allocator_GetSystem() ) ) {
        state.SkipWithError( "RBTree initialization failed." );
        return;
    }
    for ( usize iKey = 0u; iKey < nCount; ++iKey ) {
        const u32 nKey = static_cast<u32>( iKey );
        if ( !RBTree_Insert( &tree, nKey, nKey ).bInserted ) {
            state.SkipWithError( "RBTree population failed." );
            return;
        }
    }

    usize iKey = 0u;
    for ( auto _ : state ) {
        const rb_tree_node_t<u32, u32> *pNode = RBTree_Find(
            &tree,
            static_cast<u32>( iKey & ( nCount - 1u ) ) );
        benchmark::DoNotOptimize( pNode );
        iKey += 17u;
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

void BM_StdMap_FindHit( benchmark::State &state )
{
    const usize nCount = static_cast<usize>( state.range( 0 ) );
    std::map<u32, u32> tree;
    for ( usize iKey = 0u; iKey < nCount; ++iKey ) {
        const u32 nKey = static_cast<u32>( iKey );
        tree.emplace( nKey, nKey );
    }

    usize iKey = 0u;
    for ( auto _ : state ) {
        auto found = tree.find(
            static_cast<u32>( iKey & ( nCount - 1u ) ) );
        benchmark::DoNotOptimize( found );
        iKey += 17u;
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

void BM_SparseSet_FindHit( benchmark::State &state )
{
    const usize nCount = static_cast<usize>( state.range( 0 ) );
    sparse_set_t<u32> set{};
    if ( !SparseSet_Init( &set, Allocator_GetSystem(), nCount * 2u ) ) {
        state.SkipWithError( "SparseSet initialization failed." );
        return;
    }
    for ( usize iKey = 0u; iKey < nCount; ++iKey ) {
        const u32 nKey = static_cast<u32>( iKey * 2u );
        if ( SparseSet_Insert( &set, nKey, nKey ) == nullptr ) {
            state.SkipWithError( "SparseSet population failed." );
            return;
        }
    }

    usize iKey = 0u;
    for ( auto _ : state ) {
        const u32 *pValue = SparseSet_Find(
            &set,
            static_cast<u32>( ( iKey & ( nCount - 1u ) ) * 2u ) );
        benchmark::DoNotOptimize( pValue );
        iKey += 17u;
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

void BM_SparseSet_DenseIteration( benchmark::State &state )
{
    const usize nCount = static_cast<usize>( state.range( 0 ) );
    sparse_set_t<u32> set{};
    if ( !SparseSet_Init( &set, Allocator_GetSystem(), nCount ) ) {
        state.SkipWithError( "SparseSet initialization failed." );
        return;
    }
    for ( usize iKey = 0u; iKey < nCount; ++iKey ) {
        const u32 nKey = static_cast<u32>( iKey );
        if ( SparseSet_Insert( &set, nKey, nKey ) == nullptr ) {
            state.SkipWithError( "SparseSet population failed." );
            return;
        }
    }
    const sparse_set_t<u32> &constSet = set;
    const span_t<const u32> values = SparseSet_Values( &constSet );

    for ( auto _ : state ) {
        u64 nSum = 0u;
        for ( usize iValue = 0u; iValue < values.nCount; ++iValue ) {
            nSum += values.pData[iValue];
        }
        benchmark::DoNotOptimize( nSum );
    }
    state.SetItemsProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( nCount ) );
}

void BM_PriorityQueue_PushPopCycle( benchmark::State &state )
{
    const usize nCount = static_cast<usize>( state.range( 0 ) );
    priority_queue_t<u32> queue{};
    if ( !PriorityQueue_Init( &queue, Allocator_GetSystem(), nCount ) ) {
        state.SkipWithError( "PriorityQueue initialization failed." );
        return;
    }

    for ( auto _ : state ) {
        for ( usize iValue = 0u; iValue < nCount; ++iValue ) {
            if ( !PriorityQueue_Push(
                     &queue,
                     static_cast<u32>( iValue * 2654435761u ) ) ) {
                state.SkipWithError( "PriorityQueue push failed." );
                return;
            }
        }
        for ( usize iValue = 0u; iValue < nCount; ++iValue ) {
            if ( !PriorityQueue_Pop( &queue ) ) {
                state.SkipWithError( "PriorityQueue pop failed." );
                return;
            }
        }
    }
    state.SetItemsProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( nCount * 2u ) );
}

void BM_StdPriorityQueue_PushPopCycle( benchmark::State &state )
{
    const usize nCount = static_cast<usize>( state.range( 0 ) );
    std::vector<u32> storage;
    storage.reserve( nCount );
    std::priority_queue<u32, std::vector<u32>> queue(
        std::less<u32>{},
        static_cast<std::vector<u32> &&>( storage ) );

    for ( auto _ : state ) {
        for ( usize iValue = 0u; iValue < nCount; ++iValue ) {
            queue.push( static_cast<u32>( iValue * 2654435761u ) );
        }
        for ( usize iValue = 0u; iValue < nCount; ++iValue ) {
            queue.pop();
        }
    }
    state.SetItemsProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( nCount * 2u ) );
}

void BM_Dictionary_FindHit( benchmark::State &state )
{
    constexpr usize CY_DICTIONARY_BENCH_KEY_BYTES = 32u;
    const usize nCount = static_cast<usize>( state.range( 0 ) );
    std::vector<std::array<char, CY_DICTIONARY_BENCH_KEY_BYTES>> keyStorage(
        nCount );
    std::vector<string_view_t> keys( nCount );
    dictionary_t<u32> dictionary{};
    if ( !Dictionary_Init( &dictionary, Allocator_GetSystem(), nCount ) ) {
        state.SkipWithError( "Dictionary initialization failed." );
        return;
    }
    for ( usize iKey = 0u; iKey < nCount; ++iKey ) {
        std::snprintf(
            keyStorage[iKey].data(),
            keyStorage[iKey].size(),
            "asset_%08zu",
            iKey );
        keys[iKey] = StringView_FromCString( keyStorage[iKey].data() );
        if ( !Dictionary_Insert(
                 &dictionary,
                 keys[iKey],
                 static_cast<u32>( iKey ) ).bInserted ) {
            state.SkipWithError( "Dictionary population failed." );
            return;
        }
    }

    usize iKey = 0u;
    for ( auto _ : state ) {
        const u32 *pValue = Dictionary_Find(
            &dictionary,
            keys[iKey & ( nCount - 1u )] );
        benchmark::DoNotOptimize( pValue );
        iKey += 17u;
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

void BM_Search_LinearHit( benchmark::State &state )
{
    const usize nCount = static_cast<usize>( state.range( 0 ) );
    std::vector<u32> storage( nCount );
    for ( usize iValue = 0u; iValue < nCount; ++iValue ) {
        storage[iValue] = static_cast<u32>( iValue );
    }
    const span_t<const u32> values{ storage.data(), storage.size() };

    usize iValue = 0u;
    for ( auto _ : state ) {
        usize iFound = Search_Linear(
            values,
            static_cast<u32>( iValue & ( nCount - 1u ) ) );
        benchmark::DoNotOptimize( iFound );
        iValue += 17u;
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

void BM_Search_BinaryHit( benchmark::State &state )
{
    const usize nCount = static_cast<usize>( state.range( 0 ) );
    std::vector<u32> storage( nCount );
    for ( usize iValue = 0u; iValue < nCount; ++iValue ) {
        storage[iValue] = static_cast<u32>( iValue );
    }
    const span_t<const u32> values{ storage.data(), storage.size() };

    usize iValue = 0u;
    for ( auto _ : state ) {
        usize iFound = Search_Binary(
            values,
            static_cast<u32>( iValue & ( nCount - 1u ) ) );
        benchmark::DoNotOptimize( iFound );
        iValue += 17u;
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

} // namespace

BENCHMARK( BM_RBTree_FindHit )->Arg( 256 )->Arg( 4096 )->Arg( 65536 );
BENCHMARK( BM_StdMap_FindHit )->Arg( 256 )->Arg( 4096 )->Arg( 65536 );
BENCHMARK( BM_SparseSet_FindHit )->Arg( 256 )->Arg( 4096 )->Arg( 65536 );
BENCHMARK( BM_SparseSet_DenseIteration )->Arg( 256 )->Arg( 4096 )->Arg( 65536 );
BENCHMARK( BM_PriorityQueue_PushPopCycle )->Arg( 64 )->Arg( 1024 )->Arg( 4096 );
BENCHMARK( BM_StdPriorityQueue_PushPopCycle )->Arg( 64 )->Arg( 1024 )->Arg( 4096 );
BENCHMARK( BM_Dictionary_FindHit )->Arg( 256 )->Arg( 4096 )->Arg( 65536 );
BENCHMARK( BM_Search_LinearHit )->Arg( 256 )->Arg( 4096 )->Arg( 65536 );
BENCHMARK( BM_Search_BinaryHit )->Arg( 256 )->Arg( 4096 )->Arg( 65536 );
