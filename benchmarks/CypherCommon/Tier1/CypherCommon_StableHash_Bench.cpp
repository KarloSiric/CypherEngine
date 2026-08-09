//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_StableHash_Bench.cpp
//  Purpose: Benchmarks canonical stable identifier construction.
//  Details: Measures a representative resource record containing typed scalar,
//           path, size, and content-fingerprint fields.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_StableHash.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

namespace
{

void BM_StableHash_ResourceRecord( benchmark::State &state )
{
    const string_view_t path =
        StringView_FromCString( "materials/world/metal_panel_01.cymat" );
    const hash128_t content{
        0x0123456789ABCDEFull,
        0xFEDCBA9876543210ull
    };

    for ( auto _ : state ) {
        stable_hash_builder_t builder{};
        hash64_t hash = 0u;
        bool_t bComplete =
            StableHash_Begin( &builder, 0x5245534F55524345ull, 3u ) &&
            StableHash_WriteString( &builder, path ) &&
            StableHash_WriteU32( &builder, 7u ) &&
            StableHash_WriteU64( &builder, 131072u ) &&
            StableHash_WriteHash128( &builder, content ) &&
            StableHash_End( &builder, &hash );
        benchmark::DoNotOptimize( bComplete );
        benchmark::DoNotOptimize( hash );
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

} // namespace

BENCHMARK( BM_StableHash_ResourceRecord );
