//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_ContentHash_Bench.cpp
//  Purpose: Benchmarks content fingerprints and canonical formatting.
//  Details: Measures bulk content hashing, composition, and the text conversion used
//           by caches, manifests, diagnostics, and tools.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ContentHash.h"

#include <benchmark/benchmark.h>

#include <array>

using namespace cypher::common;

namespace
{

const std::array<byte, 65536u> g_contentHashData{};

} // namespace

static void BM_ContentHash_Data( benchmark::State &state )
{
    const binary_block_t data{ g_contentHashData.data(), g_contentHashData.size() };
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( ContentHash_Data( data ) );
    }
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( data.cbSize ) );
}

static void BM_ContentHash_Combine( benchmark::State &state )
{
    const content_hash_t right = ContentHash_Data( {} );
    content_hash_t left = ContentHash_String( StringView_FromCString( "asset" ) );
    for ( auto _ : state ) {
        left = ContentHash_Combine( left, right );
        benchmark::DoNotOptimize( left );
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

static void BM_ContentHash_ToHex( benchmark::State &state )
{
    const content_hash_t hash = ContentHash_Data( {} );
    char text[CY_CONTENT_HASH_HEX_CAPACITY]{};
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( ContentHash_ToHex( hash, text, sizeof( text ) ) );
        benchmark::DoNotOptimize( text );
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

BENCHMARK( BM_ContentHash_Data );
BENCHMARK( BM_ContentHash_Combine );
BENCHMARK( BM_ContentHash_ToHex );
