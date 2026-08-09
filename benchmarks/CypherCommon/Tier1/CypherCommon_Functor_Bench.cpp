//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_Functor_Bench.cpp
//  Purpose: Benchmarks default scalar and string hash policies.
//  Details: Measures hash-table key policy cost for integer IDs and representative
//           short and medium text keys.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Functor.h"

#include <benchmark/benchmark.h>

#include <array>

using namespace cypher::common;

namespace
{

std::array<char, 256u> MakeText() noexcept
{
    std::array<char, 256u> text{};
    for ( usize i = 0u; i < text.size(); ++i ) {
        text[i] = static_cast<char>( 'a' + static_cast<char>( i % 26u ) );
    }
    return text;
}

const std::array<char, 256u> g_text = MakeText();

} // namespace

static void BM_HashFunctorU64( benchmark::State &state )
{
    const hash_functor_t<u64> hasher{};
    u64 nValue = 0u;

    for ( auto _ : state ) {
        hash64_t hash = hasher( nValue );
        benchmark::DoNotOptimize( hash );
        ++nValue;
    }

    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

static void BM_HashFunctorStringView( benchmark::State &state )
{
    const usize cchLength = static_cast<usize>( state.range( 0 ) );
    const string_view_t text = StringView_FromRange( g_text.data(), cchLength );
    const hash_functor_t<string_view_t> hasher{};

    for ( auto _ : state ) {
        hash64_t hash = hasher( text );
        benchmark::DoNotOptimize( hash );
    }

    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( cchLength ) );
}

BENCHMARK( BM_HashFunctorU64 );
BENCHMARK( BM_HashFunctorStringView )->Arg( 8 )->Arg( 64 )->Arg( 256 );
