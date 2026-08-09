//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_StringPool_Bench.cpp
//  Purpose: Benchmarks stable string interning and lookup.
//  Details: A populated pool measures existing-name lookup and duplicate interning
//           without mixing setup allocation into the timed engine-facing paths.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_StringPool.h"

#include <benchmark/benchmark.h>

#include <array>
#include <cstdio>

using namespace cypher::common;

namespace
{

constexpr usize CY_BENCH_STRING_COUNT = 1024u;

struct string_pool_fixture_t {
    std::array<std::array<char, 48u>, CY_BENCH_STRING_COUNT> names{};
    string_pool_t *pPool{ nullptr };

    string_pool_fixture_t()
    {
        string_pool_desc_t desc{};
        desc.pAllocator = Allocator_GetSystem();
        desc.nInitialBuckets = CY_BENCH_STRING_COUNT;
        desc.cbInitialBlock = 64u * CY_KIB;
        pPool = StringPool_Create( desc );
        if ( pPool == nullptr ) {
            return;
        }
        for ( usize iName = 0u; iName < names.size(); ++iName ) {
            std::snprintf(
                names[iName].data(),
                names[iName].size(),
                "materials/facility/material_%04zu.cymat",
                iName );
            if ( StringPool_Intern(
                     pPool,
                     StringView_FromCString( names[iName].data() ) ) == nullptr ) {
                StringPool_Destroy( pPool );
                pPool = nullptr;
                return;
            }
        }
    }

    ~string_pool_fixture_t()
    {
        StringPool_Destroy( pPool );
    }
};

} // namespace

static void BM_StringPoolFindExisting( benchmark::State &state )
{
    string_pool_fixture_t fixture{};
    if ( fixture.pPool == nullptr ) {
        state.SkipWithError( "StringPool fixture creation failed." );
        return;
    }
    const string_view_t search = StringView_FromCString(
        fixture.names.back().data() );

    for ( auto _ : state ) {
        const char *pFound = StringPool_Find( fixture.pPool, search );
        benchmark::DoNotOptimize( pFound );
    }
}

BENCHMARK( BM_StringPoolFindExisting );

static void BM_StringPoolInternDuplicate( benchmark::State &state )
{
    string_pool_fixture_t fixture{};
    if ( fixture.pPool == nullptr ) {
        state.SkipWithError( "StringPool fixture creation failed." );
        return;
    }
    const string_view_t search = StringView_FromCString(
        fixture.names[CY_BENCH_STRING_COUNT / 2u].data() );

    for ( auto _ : state ) {
        const char *pInterned = StringPool_Intern( fixture.pPool, search );
        benchmark::DoNotOptimize( pInterned );
    }
}

BENCHMARK( BM_StringPoolInternDuplicate );
